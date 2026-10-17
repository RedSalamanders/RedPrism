#include "Document.Tests.SelectionGestureResidency.h"
#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/DocumentFixture.h"
#include "DocumentDraft.h"
#include "SharedPublicationMemory.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <wil/resource.h>
#include <windows.h>

namespace
{
unsigned checks = 0, failures = 0;
const char* current = "setup";
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s selection-gesture-residency.%s.%s\n", value ? "PASS" : "FAIL", current, label);
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL selection-gesture-residency.setup.%s\n", label);
    std::_Exit(1);
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("event-settlement");
}
template <typename Predicate> void Until(JobScheduler& scheduler, Predicate done)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!done())
    {
        if (PumpMessages().quit)
            SetupFailure("unexpected-quit");
        (void)DrainCompletions(scheduler);
        if (done())
            break;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("event-deadline");
        const HANDLE wake = scheduler.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            SetupFailure("event-wait");
    }
}
struct Memory final
{
    unsigned queries = 0;
    bool scarce = false;
    static bool Sample(void* raw, MemorySnapshot& value) noexcept
    {
        auto& self = *static_cast<Memory*>(raw);
        ++self.queries;
        const auto available = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 512ull * 1024 * 1024;
        value = {available, available};
        return true;
    }
};
constexpr std::uint64_t kObserverBytes = 8192;
class EndpointObserverJob final : public Job
{
  public:
    EndpointObserverJob(JobScheduler& scheduler, std::shared_ptr<SharedPublicationMemory>& output)
        : _scheduler(scheduler), _output(output)
    {
    }
    JobResult Run(JobContext&) override
    {
        _memory = SharedPublicationMemory::CreateForWork(kObserverBytes);
        return {};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status == JobStatus::Finished && _memory &&
            _memory->BindAfterCompletedWork(_scheduler, _ticket) == S_OK)
            _output = std::move(_memory);
    }

  private:
    JobScheduler& _scheduler;
    std::shared_ptr<SharedPublicationMemory>& _output;
    std::shared_ptr<SharedPublicationMemory> _memory;
    JobTicket _ticket;
};
class LeaseHolder final
{
  public:
    LeaseHolder()
        : request(CreateEventW(nullptr, TRUE, FALSE, nullptr)), entered(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          release(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (!request || !entered || !release)
            SetupFailure("holder-events");
        thread = std::jthread(
            [this]
            {
                if (WaitForSingleObject(request.get(), 10000) != WAIT_OBJECT_0 || cancel.Token().IsCancelled())
                    return;
                auto lease = Raster::AcquireResidencyLease(cancel.Token());
                held.store(lease.Held(), std::memory_order_release);
                (void)SetEvent(entered.get());
                if (lease.Held() && WaitForSingleObject(release.get(), 10000) != WAIT_OBJECT_0)
                    expired.store(true, std::memory_order_release);
            });
    }
    ~LeaseHolder()
    {
        ReleaseAndJoin();
    }
    void RequestAndWait() noexcept
    {
        if (!SetEvent(request.get()) || WaitForSingleObject(entered.get(), 10000) != WAIT_OBJECT_0 ||
            !held.load(std::memory_order_acquire))
            SetupFailure("holder-acquisition");
    }
    void ReleaseAndJoin() noexcept
    {
        (void)cancel.Cancel();
        (void)SetEvent(request.get());
        (void)SetEvent(release.get());
        if (thread.joinable())
            thread.join();
    }
    wil::unique_handle request, entered, release;
    CancelSource cancel;
    std::atomic<bool> held{false}, expired{false};
    std::jthread thread;
};
enum class Mode
{
    LateMove,
    LateClick,
    BusyRelease,
    BusyCancel,
    BusyClose,
    ForeignMove,
    ForeignClick
};
struct Probe final
{
    Raster retired;
    LeaseHolder* holder = nullptr;
    DWORD ownerThread = GetCurrentThreadId();
    bool click = false;
    bool grow = false;
    wil::unique_handle continueRefresh{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> refreshStarted{false};
    std::atomic<unsigned> prepared{0}, growths{0};
    unsigned terminalCallbacks = 0;
    bool terminalLeaseAvailable = false;
    static void Work(void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (stage == SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted)
        {
            self.refreshStarted.store(true, std::memory_order_release);
            if (!PostThreadMessageW(self.ownerThread, WM_APP + 0x6a0, 0, 0))
                SetupFailure("refresh-owner-wake");
            if (WaitForSingleObject(self.continueRefresh.get(), 10000) != WAIT_OBJECT_0)
                SetupFailure("refresh-inspection-gate");
            return;
        }
        const auto wanted = self.click ? SelectionOutlineGestureState::WorkStage::ClickPrepared
                                       : SelectionOutlineGestureState::WorkStage::ClosePrepared;
        if (stage != wanted)
            return;
        ++self.prepared;
        if (self.grow)
        {
            if (!self.retired.Data())
                SetupFailure("late-materialization");
            ++self.growths;
        }
        if (self.holder)
            self.holder->RequestAndWait();
    }
    static void Owner(void* raw, SelectionOutlineGestureState::Event event) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (event == SelectionOutlineGestureState::Event::Terminal)
        {
            ++self.terminalCallbacks;
            Raster::ResidencyLease lease;
            self.terminalLeaseAvailable = lease.TryAcquire();
        }
    }
};
void Run(Mode mode, const char* name)
{
    current = name;
    auto session = std::make_unique<DocumentSession>(512);
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 64;
    document.height = 64;
    document.selection = DocumentSelection{false, SelectionLoops{{{1, 2}, {7, 2}, {7, 6}, {1, 6}}}};
    auto oldImage = Raster::MakeSparse(64, 64, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    auto layer = ImageLayer::Blank(L"Residency source", {64, 64});
    layer.asset = RasterAsset{oldImage, {}, layer.name};
    const auto active = layer.id;
    document.layers.push_back(std::move(layer));
    session->ReplaceDocument(std::move(document).Seal(), active);
    session->BeginEdit(L"Seed raster");
    auto draft = CanvasDocumentDraft::Clone(*session->Document(), 1);
    if (!draft)
        SetupFailure("reference-draft");
    draft->EditLayers()[0].asset->image = Raster::MakeSparse(64, 64, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 1);
    auto after = draft->Seal();
    if (!after)
        SetupFailure("reference-seal");
    session->EditForTest([&](CanvasDocument& live) { live = std::move(*after); });
    session->EndEdit();
    draft.reset();
    after.reset();
    session->history.MarkSaved();
    if (session->history.CaptureState().PastCount() != 1 || oldImage.IsMaterialized())
        SetupFailure("history-only-sparse-seed");
    const auto generation = session->Generation();
    const auto revision = session->history.CurrentRevision();
    const auto description = DescribeSelectionOutlineGestureMemory(*session);
    if (!description)
        SetupFailure("complete-original-description");
    auto prefix = std::make_unique<HostSelectionPressInput>();
    HostSelectionPressInput::Context arrival;
    arrival.document = description->source.document;
    arrival.active = active;
    arrival.ordinal = 1;
    const bool click = mode == Mode::LateClick || mode == Mode::ForeignClick;
    const bool foreign = mode == Mode::ForeignMove || mode == Mode::ForeignClick;
    if (!prefix->Begin(29, arrival, {2, 3}, {}, false) ||
        (!click && prefix->Move(29, {4, 6}, {}, false, 2) != HostSelectionPressInput::Admission::Recorded) ||
        prefix->Finish(29, false, 3) != HostSelectionPressInput::Admission::Recorded)
        SetupFailure("accepted-prefix");
    std::unique_ptr<LeaseHolder> holder;
    if (mode == Mode::BusyRelease || mode == Mode::BusyCancel || mode == Mode::BusyClose || foreign)
        holder = std::make_unique<LeaseHolder>();
    Probe probe;
    if (!probe.continueRefresh)
        SetupFailure("refresh-event");
    probe.retired = oldImage;
    probe.holder = holder.get();
    probe.click = click;
    probe.grow = mode == Mode::LateMove || mode == Mode::LateClick;
    Memory memory;
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("scheduler");
    std::shared_ptr<SharedPublicationMemory> endpoint;
    std::unique_ptr<Job> observer = std::make_unique<EndpointObserverJob>(scheduler, endpoint);
    JobTicket observerTicket;
    if (scheduler.Submit(observer,
                         {.kind = JobClass::Selection,
                          .memoryBytes = kObserverBytes,
                          .deferredRetirement = true,
                          .allowMemoryWaiting = false,
                          .reserveRetainedMemory = true},
                         observerTicket) != S_OK)
        SetupFailure("independent-endpoint-observer");
    Settle(scheduler);
    if (!endpoint || endpoint->Bytes() != kObserverBytes)
        SetupFailure("endpoint-observer-publication");
    const auto expectedQueries = memory.queries + 1;
    auto state = std::make_unique<SelectionOutlineGestureState>(scheduler, *session, *prefix, 29, description->limits);
    state->admissionSource = description->source;
    state->workObserver = &probe;
    state->workCheckpoint = Probe::Work;
    state->observer = &probe;
    state->notify = Probe::Owner;
    std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
    if (scheduler.Submit(job,
                         {.kind = JobClass::Selection,
                          .serialKey = session->DocumentSerial(),
                          .memoryBytes = description->limits.totalBytes,
                          .deferredRetirement = true,
                          .allowMemoryWaiting = false,
                          .reserveRetainedMemory = true,
                          .continuingOutputLimit = description->limits.continuingOutputBytes},
                         state->original) != S_OK)
        SetupFailure("original-admission");
    memory.scarce = true; // A refresh cannot query or obtain any fresh admission.
    Until(scheduler, [&] { return probe.refreshStarted.load(std::memory_order_acquire); });
    Expect(state->captures == 1 && state->hits == 1 && state->closurePrepares == 1 && state->residencyTries >= 1 &&
               scheduler.ReservedMemoryBytes() >= description->limits.totalBytes + kObserverBytes &&
               state->remainingBudget == description->limits.totalBytes - state->openingBytes &&
               memory.queries == expectedQueries,
           "refresh-keeps-original-source-parent-reservation-and-admission");
    if (foreign)
    {
        const auto beforeGeneration = session->Generation();
        const auto beforeRevision = session->history.CurrentRevision();
        const auto beforeDepth = session->history.CaptureState().Depth();
        const auto beforePast = session->history.CaptureState().PastCount();
        const auto beforeOpening = session->SelectionMoveOpening();
        const auto beforeSelection = session->Selection()->loops.Identity();
        session->BeginTransform(true);
        const auto& transform = session->PendingTransformEdit();
        const auto* address = transform ? &transform.value() : nullptr;
        const auto foreignDraft = transform ? std::optional(transform->draft) : std::optional<LayerTransform>{};
        Expect(address && foreignDraft && session->Generation() == beforeGeneration &&
                   session->history.CurrentRevision() == beforeRevision &&
                   session->history.CaptureState().Depth() == beforeDepth,
               "foreign-transform-opens-during-refresh-without-generation-or-history-change");
        if (!SetEvent(probe.continueRefresh.get()))
            SetupFailure("foreign-refresh-release");
        holder->ReleaseAndJoin();
        Settle(scheduler);
        const auto& preserved = session->PendingTransformEdit();
        Expect(state->completed && state->outcome.status == JobStatus::Failed && state->outcome.error == E_ABORT &&
                   !state->aborted && state->closed == click && !state->clicked && state->inputAcknowledged &&
                   !prefix->Busy() && preserved && &preserved.value() == address && foreignDraft &&
                   preserved->draft == *foreignDraft && session->Generation() == beforeGeneration &&
                   session->history.CurrentRevision() == beforeRevision &&
                   session->history.CaptureState().Depth() == beforeDepth &&
                   session->history.CaptureState().PastCount() == beforePast &&
                   session->SelectionMoveOpening() == beforeOpening &&
                   session->Selection()->loops.Identity() == beforeSelection && session->IsMovingSelection() == !click,
               "stale-refresh-refuses-publication-and-preserves-exact-foreign-editor-and-opening");
        {
            Raster::ResidencyLease lease;
            Expect(state->sameTicket && state->residencyRefreshes == 1 && state->closurePrepares == 1 &&
                       state->clickPrepares == (click ? 1u : 0u) && state->captures == 1 &&
                       memory.queries == expectedQueries && probe.terminalCallbacks == 1 &&
                       probe.terminalLeaseAvailable && lease.TryAcquire() &&
                       !holder->expired.load(std::memory_order_acquire),
                   "refusal-keeps-original-ticket-and-releases-permit-before-terminal-callback");
        }
        Expect(endpoint->ResidentBytes() == kObserverBytes + (click ? 0 : state->openingBytes),
               "live-foreign-editor-retains-exact-original-opening-charge");
        probe.retired = Raster{};
        oldImage = Raster{};
        state.reset();
        session.reset();
        Settle(scheduler);
        Expect(endpoint->ResidentBytes() == kObserverBytes,
               "last-foreign-editor-and-history-readers-refund-all-original-memory");
        endpoint.reset();
        Settle(scheduler);
        scheduler.Close();
        return;
    }
    if (!SetEvent(probe.continueRefresh.get()))
        SetupFailure("release-refresh-inspection");
    const bool cancelled = mode == Mode::BusyCancel || mode == Mode::BusyClose;
    if (mode == Mode::BusyClose)
        scheduler.Close(); // Holder still owns the global lease; shutdown must cancel the original wait.
    else
    {
        if (mode == Mode::BusyCancel && !scheduler.Cancel(state->original))
            SetupFailure("cancel-original");
        if (mode == Mode::BusyRelease)
            holder->ReleaseAndJoin();
        Settle(scheduler);
    }
    Expect(state->sameTicket && state->inputAcknowledged && !prefix->Busy() &&
               (cancelled ? state->outcome.status == JobStatus::Cancelled && state->aborted && !state->closed
                          : state->completed && state->outcome.status == JobStatus::Finished && state->closed &&
                                (!click || state->clicked)),
           "same-original-ticket-finishes-or-cancels-while-residency-is-contended");
    const auto& selection = session->Selection();
    const bool geometry = click ? !selection
                                : selection && !selection->antialiased &&
                                      selection->Bounds() == (cancelled ? std::optional<SelectionBounds>{{1, 2, 7, 6}}
                                                                        : std::optional<SelectionBounds>{{3, 5, 9, 9}});
    const auto expectedPast = cancelled ? 1u : mode == Mode::BusyRelease ? 2u : 1u;
    Expect(geometry && !session->IsMovingSelection() && session->Generation() == generation + (cancelled ? 3 : 2) &&
               session->history.CaptureState().Depth() == 0 &&
               session->history.CaptureState().PastCount() == expectedPast &&
               (cancelled ? session->history.CurrentRevision() == revision
                          : session->history.UndoName() == (click ? L"Deselect" : L"Move Selection")),
           "literal-selection-generation-and-exact-oldest-history-trim");
    Expect(state->residencyRefreshes == 1 && state->closurePrepares == 1 && state->clickPrepares == (click ? 1u : 0u) &&
               probe.prepared == 1 && probe.growths == (probe.grow ? 1u : 0u) &&
               (!probe.grow || (oldImage.IsMaterialized() && oldImage.ByteSize() == 16384)) &&
               memory.queries == expectedQueries,
           "one-refresh-reuses-prepared-intent-and-observes-exact16384-byte-growth");
    if (holder)
        holder->ReleaseAndJoin();
    {
        Raster::ResidencyLease available;
        Expect(available.TryAcquire() && (!holder || !holder->expired.load(std::memory_order_acquire)) &&
                   (mode == Mode::BusyClose    ? probe.terminalCallbacks == 0
                    : mode == Mode::BusyCancel ? probe.terminalCallbacks == 1
                                               : probe.terminalCallbacks == 1 && probe.terminalLeaseAvailable),
               "permits-release-before-host-callbacks-cancel-and-joined-close");
    }
    const auto liveBytes =
        cancelled ? kObserverBytes
                  : kObserverBytes + description->limits.finalOutputBytes + (click ? 0 : state->openingBytes);
    Expect(endpoint->ResidentBytes() == liveBytes, "live-history-retains-exact-opening-and-final-publication-charges");
    probe.retired = Raster{};
    oldImage = Raster{};
    state.reset();
    session.reset();
    if (mode != Mode::BusyClose)
        Settle(scheduler);
    Expect(endpoint && endpoint->Bytes() == kObserverBytes && endpoint->ResidentBytes() == kObserverBytes,
           "last-prepared-and-live-history-readers-refund-all-original-memory");
    endpoint.reset();
    if (mode != Mode::BusyClose)
        Settle(scheduler);
    scheduler.Close();
}
} // namespace
int RunDocumentSelectionGestureResidencyTests()
{
    Run(Mode::LateMove, "late-move");
    Run(Mode::LateClick, "late-click");
    Run(Mode::BusyRelease, "busy-release");
    Run(Mode::BusyCancel, "busy-cancel");
    Run(Mode::BusyClose, "busy-close");
    Run(Mode::ForeignMove, "foreign-move");
    Run(Mode::ForeignClick, "foreign-click");
    std::printf("SUMMARY selection-gesture-residency checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

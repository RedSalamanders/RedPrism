#include "Document.Tests.HistoryOriginalTicket.h"

#include "Document.h"
#include "Jobs.h"
#include "SharedPublicationMemory.h"

#include "../Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4 * 1024 * 1024;
constexpr std::uint64_t kUsableBytes = 512ULL * 1024 * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + kUsableBytes;
constexpr std::uint32_t kGateFailureTimeoutMs = 10'000;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s history-original-ticket.%s\n", condition ? "PASS" : "FAIL", label);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* label, DWORD error = 0) noexcept
{
    std::printf("FAIL history-original-ticket.setup.%s error=%lu\n", label, error);
    std::fflush(stdout);
    std::_Exit(1);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

void Start(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("scheduler-initialize");
}

JobOptions OriginalDescriptor() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Commit;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    return options;
}

struct Documents final
{
    Raster leaf = Raster::Make(2, 1, RasterFormat::Bgra8888);
    Raster base = Raster::Make(3, 2, RasterFormat::Bgra8888);
    Raster sparse = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0, {{1, 0, leaf, 0, 0, 2, 1}}, 0);

    [[nodiscard]] std::optional<CanvasDocument> Make(int width, double opacity = 1.0) const
    {
        DocumentFixture fixture;
        fixture.width = width;
        fixture.height = 2;
        auto layer = ImageLayer::Blank(L"Ticket history", {4, 2});
        layer.opacity = opacity;
        layer.asset = RasterAsset{sparse, sparse, L"Sparse roots"};
        layer.shapeImage = leaf;
        fixture.layers.push_back(std::move(layer));
        return std::move(fixture).Seal();
    }
};

struct JobState final
{
    JobScheduler* scheduler = nullptr;
    DocumentHistory* history = nullptr;
    std::optional<CanvasDocument> before;
    std::optional<CanvasDocument> after;
    DocumentHistory::StateView view;
    std::optional<DocumentHistory::Publication> publication;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::weak_ptr<const SharedPublicationMemory> weakMemory;
    JobTicket ticket{};
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::int32_t bindResult = E_UNEXPECTED;
    bool ran = false;
    bool captured = false;
    bool capturedUnderAdmission = false;
    bool published = false;
    bool callbackOnOwner = false;
    bool boundInTerminalCallback = false;
    bool sparseRootsStayUnmaterialized = false;
    HANDLE enteredGate = nullptr;
    HANDLE resumeGate = nullptr;
};

class PrepareHistoryJob final : public Job
{
  public:
    explicit PrepareHistoryJob(JobState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        _state.ran = true;
        if (!_state.memory)
            _state.memory = SharedPublicationMemory::CreateForWork();
        if (!_state.captured)
            return {}; // Owner capture is deferred until this admitted first stage joins.
        if (_state.enteredGate)
        {
            if (!SetEvent(_state.enteredGate))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE waits[]{_state.resumeGate, static_cast<HANDLE>(context.shutdownEvent)};
            const DWORD waited =
                WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, kGateFailureTimeoutMs);
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        }
        _state.publication =
            DocumentHistory::PrepareEdit(std::move(_state.view), _state.before, {}, _state.after, {},
                                         L"Original ticket edit", context.cancellation, {}, _state.memory);
        _state.sparseRootsStayUnmaterialized = _state.before && _state.after &&
                                               !_state.before->layers[0].asset->image.IsMaterialized() &&
                                               !_state.after->layers[0].asset->image.IsMaterialized();
        return _state.publication ? JobResult{} : JobResult{JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_state.captured || result.status != JobStatus::Finished)
            return false;
        _state.capturedUnderAdmission = _state.scheduler->InUiCallback() &&
                                        _state.scheduler->ReservedMemoryBytes() >= kAdmissionBytes &&
                                        ticket == _state.ticket;
        _state.view = _state.history->CaptureState();
        _state.captured = true;
        return _state.scheduler->Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        _state.callbackOnOwner = _state.scheduler->InUiCallback();
        _state.terminal = result;
        if (result.status != JobStatus::Finished || !_state.publication || !_state.memory)
        {
            if (_state.memory)
                _state.bindResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
            _state.weakMemory = _state.memory;
            _state.memory.reset();
            return;
        }

        // This is the exact terminal callback. Bind the submitted descriptor's original ticket before
        // making the prepared publication or its owning sparse-root graphs visible in DocumentHistory.
        _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        _state.boundInTerminalCallback = _state.memory->BoundTo(*_state.scheduler, _state.ticket);
        _state.weakMemory = _state.memory;
        if (SUCCEEDED(_state.bindResult))
        {
            DocumentHistory::ResidencyPermit permit;
            if (permit.TryAcquire(_state.publication.value()) == DocumentHistory::ResidencyValidation::Ready)
                _state.published = _state.history->Publish(_state.publication.value());
        }
        _state.publication.reset();
        _state.memory.reset();
    }

  private:
    JobState& _state;
};

template <class State> void CaptureAdmittedState(JobScheduler& scheduler, State& state)
{
    // Deadline is a deadlock failure bound. Every idle iteration blocks on the actual scheduler wake event.
    const auto start = GetTickCount64();
    while (!state.captured)
    {
        (void)DrainCompletions(scheduler);
        if (state.captured)
            break;
        if (scheduler.IsIdle())
        {
            if constexpr (requires { state.terminal; })
                SetupFailure("terminal-before-admitted-owner-capture", static_cast<DWORD>(state.terminal.error));
            else
                SetupFailure("idle-before-admitted-owner-capture", ERROR_INVALID_STATE);
        }
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= kGateFailureTimeoutMs ||
            WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()),
                                static_cast<DWORD>(kGateFailureTimeoutMs - elapsed)) != WAIT_OBJECT_0)
            SetupFailure("admitted-owner-capture-wake", ERROR_TIMEOUT);
    }
    Expect(state.capturedUnderAdmission, "capture.real-owner-continuation-holds-original-admission-before-state-copy");
}

void Submit(JobScheduler& scheduler, JobState& state)
{
    state.scheduler = &scheduler;
    const auto preparation = state.history->EditMemoryBytes(state.before);
    const auto retirement = state.history->RetirementMemoryBytes(state.before);
    Expect(preparation != 0 && retirement != 0 &&
               preparation <= kAdmissionBytes - SharedPublicationMemory::OwnershipAllowance() &&
               retirement <= kAdmissionBytes - preparation - SharedPublicationMemory::OwnershipAllowance(),
           "admission.literal-descriptor-covers-cached-preparation-retirement-and-control-allowances");
    std::unique_ptr<Job> job = std::make_unique<PrepareHistoryJob>(state);
    if (scheduler.Submit(job, OriginalDescriptor(), state.ticket) != S_OK || job)
        SetupFailure("submit-original-descriptor");
    CaptureAdmittedState(scheduler, state);
}

void Finish(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, kGateFailureTimeoutMs) != SettleResult::Settled)
        SetupFailure("event-gated-scheduler-settlement", ERROR_TIMEOUT);
}

bool SampleBelowHeadroom(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kUsableBytes, kUsableBytes};
    return true;
}

void RefusedAdmissionPreservesHistoryWithoutCapture()
{
    JobScheduler scheduler;
    if (scheduler.Initialize(1, {}, {SampleBelowHeadroom, nullptr}) != S_OK)
        SetupFailure("refusal-scheduler-initialize");
    Documents documents;
    DocumentHistory history(8, 1024 * 1024);
    auto before = documents.Make(4);
    auto after = documents.Make(5);
    history.Begin(L"seed admission refusal redo", before, {});
    history.End(after, {});
    auto restored = history.Undo();
    if (!restored || !history.CanRedo())
        SetupFailure("refusal-seed-redo");
    const Guid revision = history.CurrentRevision();
    const auto undoCount = history.UndoCount();
    const auto redoName = history.RedoName();
    JobState state;
    state.scheduler = &scheduler;
    state.history = &history;
    state.before = restored->document;
    state.after = documents.Make(6);
    std::unique_ptr<Job> job = std::make_unique<PrepareHistoryJob>(state);
    if (scheduler.Submit(job, OriginalDescriptor(), state.ticket) != S_OK || job)
        SetupFailure("refusal-submit-original-descriptor");
    Finish(scheduler);
    Expect(state.terminal.status == JobStatus::Failed && state.terminal.error == E_OUTOFMEMORY,
           "refusal.below-headroom-actual-job-fails-with-out-of-memory");
    Expect(!state.ran && !state.captured && !state.published && !state.memory && !state.publication,
           "refusal.no-worker-capture-publication-or-memory-owner");
    Expect(history.CurrentRevision() == revision && history.UndoCount() == undoCount && history.CanRedo() &&
               history.RedoName() == redoName,
           "refusal.history-revision-undo-and-redo-unchanged");
    Expect(scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0, "refusal.scheduler-settles-without-reservation");
    scheduler.Close();
}

void SuccessfulHistoryPublicationUsesOriginalTicket()
{
    JobScheduler scheduler;
    Start(scheduler);
    Documents documents;
    JobState state;
    DocumentHistory history(8, 1024 * 1024);
    state.history = &history;
    state.before = documents.Make(4);
    state.after = documents.Make(5, 0.75);
    const Guid revisionBefore = history.CurrentRevision();
    Submit(scheduler, state);
    Finish(scheduler);

    Expect(state.ran && state.terminal.status == JobStatus::Finished, "success-real-job-ran-to-terminal-finished");
    Expect(state.callbackOnOwner && state.boundInTerminalCallback,
           "success-terminal-callback-binds-original-owner-ticket");
    Expect(state.bindResult == S_OK && !state.weakMemory.expired(),
           "success-terminal-binds-original-descriptor-before-release");
    Expect(state.published && history.UndoCount() == 1 && history.CurrentRevision() != revisionBefore,
           "success-publishes-actual-prepared-document-history");
    Expect(state.sparseRootsStayUnmaterialized && !documents.sparse.IsMaterialized(),
           "success-history-preparation-keeps-owning-sparse-roots-lazy");
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "success-original-admission-remains-charged-through-history-roots");
    scheduler.Close();
}

void OldCapturedStateRetainsOriginalAccountingAcrossScalarEdit()
{
    JobScheduler scheduler;
    Start(scheduler);
    Documents documents;
    DocumentHistory history(8, 1024 * 1024);
    auto firstBefore = documents.Make(4);
    auto firstAfter = documents.Make(5, 0.8);
    JobState first;
    first.history = &history;
    first.before = firstBefore;
    first.after = firstAfter;
    Submit(scheduler, first);
    Finish(scheduler);
    Expect(first.published && first.bindResult == S_OK, "retention.first-publication-owns-first-ticket");

    auto capturedOldState = history.CaptureState();
    auto scalarAfter = documents.Make(5, 0.4);
    JobState second;
    second.history = &history;
    second.before = firstAfter;
    second.after = scalarAfter;
    Submit(scheduler, second);
    Finish(scheduler);

    auto oldOwner = first.weakMemory.lock();
    Expect(second.published && history.UndoCount() == 2 && second.bindResult == S_OK,
           "retention.next-scheduler-job-publishes-scalar-history-update");
    Expect(oldOwner && oldOwner->IsBound() && oldOwner->BoundTo(scheduler, first.ticket),
           "retention.old-captured-roots-remain-bound-to-first-original-ticket");
    Expect(oldOwner && oldOwner->Bytes() == kAdmissionBytes && oldOwner->ResidentBytes() == 2 * kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == 2 * kAdmissionBytes,
           "retention.second-admission-does-not-refund-first-while-old-state-and-new-history-live");
    Expect(capturedOldState.PastCount() == 1 && capturedOldState.Revision() != history.CurrentRevision(),
           "retention.captured-state-remains-an-independent-old-history-root");

    oldOwner.reset(); // Diagnostic lock must not keep the old graph alive for the assertion.
    history.Reset();
    Expect(!first.weakMemory.expired() && scheduler.ReservedMemoryBytes() == 2 * kAdmissionBytes,
           "retention.captured-old-roots-keep-first-strong-accounting-owner-after-live-history-reset");
    capturedOldState = DocumentHistory::StateView{};
    oldOwner.reset();
    first.weakMemory.reset();
    second.weakMemory.reset();
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "retention.final-strong-and-weak-release-acknowledges-both-tickets");
    scheduler.Close();
}

void CancellationLeavesRedoAndRevisionUntouched()
{
    JobScheduler scheduler;
    Start(scheduler);
    Documents documents;
    DocumentHistory history(8, 1024 * 1024);
    auto before = documents.Make(4);
    auto after = documents.Make(5);
    history.Begin(L"seed redo", before, {});
    history.End(after, {});
    auto restored = history.Undo();
    if (!restored || !history.CanRedo())
        SetupFailure("seed-real-redo-state");

    JobState state;
    state.history = &history;
    state.before = restored->document;
    state.after = documents.Make(6, 0.5);
    // Hold the real worker on an event so cancellation is requested while its original descriptor is active.
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle resume{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !resume)
        SetupFailure("cancel-create-worker-gates", GetLastError());
    state.enteredGate = entered.get();
    state.resumeGate = resume.get();
    const Guid revision = history.CurrentRevision();
    const auto redoName = history.RedoName();
    Submit(scheduler, state);
    if (WaitForSingleObject(entered.get(), kGateFailureTimeoutMs) != WAIT_OBJECT_0)
        SetupFailure("cancel-wait-worker-entered", ERROR_TIMEOUT);
    if (!scheduler.Cancel(state.ticket))
        SetupFailure("cancel-original-ticket");
    if (!SetEvent(resume.get()))
        SetupFailure("cancel-release-worker-gate", GetLastError());
    Finish(scheduler);

    Expect(state.terminal.status == JobStatus::Cancelled && state.bindResult == S_OK,
           "cancel.terminal-callback-retires-original-admission");
    Expect(history.CurrentRevision() == revision && history.CanRedo() && history.RedoName() == redoName,
           "cancel.document-revision-and-existing-redo-remain-untouched");
    Expect(!state.published && history.UndoCount() == 0 && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "cancel.no-publication-and-retirement-charge-remains-with-weak-control");
    Expect(state.weakMemory.expired(), "cancel.strong-accounting-object-dies-while-weak-control-keeps-charge");
    state.weakMemory.reset();
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "cancel.final-weak-release-refunds-original-admission");
    scheduler.Close();
}

struct CloseState final
{
    JobScheduler* scheduler = nullptr;
    DocumentHistory* history = nullptr;
    bool captured = false;
    bool capturedUnderAdmission = false;
    DocumentHistory::StateView view;
    std::optional<CanvasDocument> before;
    std::optional<CanvasDocument> after;
    std::optional<DocumentHistory::Publication> prepared;
    JobTicket ticket{};
    HANDLE entered = nullptr;
    bool sawShutdown = false;
    bool joinedHook = false;
    bool boundToOriginalInsideHook = false;
    bool preparedBeforeClose = false;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::weak_ptr<const SharedPublicationMemory> weakMemory;
    std::int32_t bindResult = E_UNEXPECTED;
};

struct ObserverState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    SharedRetainedMemoryLease lease;
    std::int32_t result = E_UNEXPECTED;
};

class ObserverJob final : public Job
{
  public:
    explicit ObserverJob(ObserverState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status == JobStatus::Finished)
            _state.result = _state.scheduler->RetainMemory(_state.ticket, _state.lease);
    }

  private:
    ObserverState& _state;
};

class JoinedCloseJob final : public Job
{
  public:
    explicit JoinedCloseJob(CloseState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        if (!_state.memory)
            _state.memory = SharedPublicationMemory::CreateForWork();
        if (!_state.captured)
            return {}; // Owner capture is deferred until this admitted first stage joins.
        _state.prepared =
            DocumentHistory::PrepareEdit(std::move(_state.view), _state.before, {}, _state.after, {},
                                         L"Close-held sparse history", context.cancellation, {}, _state.memory);
        _state.preparedBeforeClose = _state.prepared.has_value();
        if (!_state.prepared)
            return {JobStatus::Failed, E_FAIL};
        if (!SetEvent(_state.entered))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE waits[]{static_cast<HANDLE>(context.shutdownEvent)};
        const DWORD result =
            WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, kGateFailureTimeoutMs);
        _state.sawShutdown = result == WAIT_OBJECT_0;
        return _state.sawShutdown ? JobResult{JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)}
                                  : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_state.captured || result.status != JobStatus::Finished)
            return false;
        _state.capturedUnderAdmission = _state.scheduler->InUiCallback() &&
                                        _state.scheduler->ReservedMemoryBytes() >= kAdmissionBytes &&
                                        ticket == _state.ticket;
        _state.view = _state.history->CaptureState();
        _state.captured = true;
        return _state.scheduler->Resume(ticket);
    }
    void Completed(JobResult) noexcept override {}
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _state.joinedHook = scheduler.InJoinedRetirementCallback(ticket);
        _state.bindResult = _state.memory->BindForRetirement(scheduler, ticket);
        _state.boundToOriginalInsideHook = _state.memory->BoundTo(scheduler, ticket);
        _state.weakMemory = _state.memory;
    }

  private:
    CloseState& _state;
};

void JoinedCloseKeepsHeldGraphAccounting()
{
    JobScheduler scheduler;
    Start(scheduler);
    Documents documents;
    DocumentHistory history(8, 1024 * 1024);
    ObserverState observer;
    observer.scheduler = &scheduler;
    std::unique_ptr<Job> observerJob = std::make_unique<ObserverJob>(observer);
    JobOptions observerOptions{};
    observerOptions.memoryBytes = 8192;
    observerOptions.reserveRetainedMemory = true;
    if (scheduler.Submit(observerJob, observerOptions, observer.ticket) != S_OK || observerJob ||
        SettleJobs(scheduler, kGateFailureTimeoutMs) != SettleResult::Settled || observer.result != S_OK)
        SetupFailure("close-submit-independent-endpoint-observer");
    HANDLE rawEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!rawEntered)
        SetupFailure("close-create-worker-gate", GetLastError());
    wil::unique_handle entered{rawEntered};
    CloseState state;
    state.scheduler = &scheduler;
    state.history = &history;
    state.before = documents.Make(4);
    state.after = documents.Make(5, 0.6);
    state.entered = entered.get();
    std::unique_ptr<Job> job = std::make_unique<JoinedCloseJob>(state);
    const auto preparation = history.EditMemoryBytes(state.before);
    const auto retirement = history.RetirementMemoryBytes(state.before);
    Expect(preparation != 0 && retirement != 0 &&
               preparation <= kAdmissionBytes - SharedPublicationMemory::OwnershipAllowance() &&
               retirement <= kAdmissionBytes - preparation - SharedPublicationMemory::OwnershipAllowance(),
           "admission.literal-descriptor-covers-cached-preparation-retirement-and-control-allowances");
    if (scheduler.Submit(job, OriginalDescriptor(), state.ticket) != S_OK || job)
        SetupFailure("close-submit-original-admission");
    CaptureAdmittedState(scheduler, state);
    if (WaitForSingleObject(entered.get(), kGateFailureTimeoutMs) != WAIT_OBJECT_0)
        SetupFailure("close-worker-prepared-event-gate");
    scheduler.Close();

    auto heldOwner = state.weakMemory.lock();
    Expect(state.preparedBeforeClose && state.sawShutdown && state.joinedHook && history.UndoCount() == 0,
           "close.actual-prepared-history-graph-drops-without-publication-after-worker-join");
    Expect(state.bindResult == S_OK && state.boundToOriginalInsideHook && heldOwner,
           "close.joined-hook-binds-accounting-to-original-descriptor-ticket");
    Expect(heldOwner && heldOwner->ResidentBytes() == kAdmissionBytes + 8192 &&
               observer.lease.ResidentBytes() == kAdmissionBytes + 8192 && state.prepared.has_value(),
           "close.closed-endpoint-retains-actual-history-graph-charge");
    state.prepared.reset();
    state.memory.reset();
    heldOwner.reset();
    Expect(state.weakMemory.expired() && observer.lease.ResidentBytes() == kAdmissionBytes + 8192,
           "close.expired-weak-control-keeps-original-charge-after-strong-graph-dies");
    state.weakMemory.reset();
    Expect(state.weakMemory.expired() && observer.lease.ResidentBytes() == 8192,
           "close.final-weak-release-refunds-only-original-graph-charge");
    observer.lease.Reset();
}
} // namespace

int RunHistoryOriginalTicketTests()
{
    gPassed = 0;
    gFailed = 0;
    const auto admission = EvaluateMemoryAdmission(kAdmissionBytes, kAvailableBytes, kAvailableBytes, 0, 0);
    Expect(admission.admission == MemoryAdmission::Ready && admission.availableAfterReservations == kUsableBytes,
           "admission.snapshot-exposes-512-mib-after-fixed-headroom");
    RefusedAdmissionPreservesHistoryWithoutCapture();
    SuccessfulHistoryPublicationUsesOriginalTicket();
    OldCapturedStateRetainsOriginalAccountingAcrossScalarEdit();
    CancellationLeavesRedoAndRevisionUntouched();
    JoinedCloseKeepsHeldGraphAccounting();
    std::printf("%s history-original-ticket.summary passed=%u failed=%u controls=%u expected=38\n",
                gFailed == 0 && gPassed == 38 ? "PASS" : "FAIL", gPassed, gFailed, gPassed + gFailed);
    return gFailed == 0 && gPassed == 38 ? 0 : 1;
}

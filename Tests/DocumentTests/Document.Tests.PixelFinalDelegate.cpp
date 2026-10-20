#include "Document.Tests.PixelFinalDelegate.h"

#include "../Support/DocumentFixture.h"
#include "PixelMoveClosureWork.h"
#include "SelectionOutlineGestureJob.h"
#include "SelectionOutlineGestureMemory.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <windows.h>

namespace PixelFinalDelegateTests
{
namespace
{
using Pixels = std::array<std::uint8_t, 5 * 3 * 4>;
constexpr std::array<std::uint8_t, 4> kRed{0, 0, 255, 255};
constexpr std::array<std::uint8_t, 4> kBlue{255, 0, 0, 255};

void Expect(bool condition, const char* fact, int& failures)
{
    std::printf("%s pixel-final-delegate.%s\n", condition ? "PASS" : "FAIL", fact);
    failures += condition ? 0 : 1;
}

[[noreturn]] void Broken(const char* fact)
{
    std::printf("FAIL pixel-final-delegate.setup.%s\n", fact);
    std::_Exit(5);
}

Pixels Source()
{
    Pixels pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        std::copy(kBlue.begin(), kBlue.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i));
    constexpr std::size_t source = (1 * 5 + 1) * 4;
    std::copy(kRed.begin(), kRed.end(), pixels.begin() + static_cast<std::ptrdiff_t>(source));
    return pixels;
}

Pixels Expected(bool duplicate)
{
    auto result = Source();
    constexpr std::size_t source = (1 * 5 + 1) * 4;
    constexpr std::size_t destination = (2 * 5 + 3) * 4;
    if (!duplicate)
        std::fill_n(result.begin() + static_cast<std::ptrdiff_t>(source), 4, std::uint8_t{});
    std::copy(kRed.begin(), kRed.end(), result.begin() + static_cast<std::ptrdiff_t>(destination));
    return result;
}

bool Matches(const DocumentSession& session, const Pixels& expected)
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset || layer->asset->image.Width() != 5 || layer->asset->image.Height() != 3)
        return false;
    Pixels actual{};
    layer->asset->image.ReadRegion(0, 0, 5, 3, actual.data(), 20, 0);
    return actual == expected;
}

struct Memory final
{
    std::atomic<unsigned> queries{0};
    static bool Read(void* raw, MemorySnapshot& snapshot) noexcept
    {
        static_cast<Memory*>(raw)->queries.fetch_add(1, std::memory_order_relaxed);
        constexpr std::uint64_t available = kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        snapshot = {available, available};
        return true;
    }
};

struct Probe final
{
    std::thread::id owner = std::this_thread::get_id();
    std::thread::id factoryThread{};
    std::thread::id firstRetireThread{};
    std::thread::id workThread{};
    std::thread::id refreshThread{};
    std::thread::id finalRetireThread{};
    std::atomic<unsigned> submissions{0};
    JobTicket submittedTicket{};
    JobTicket factoryTicket{};
    JobTicket initialRetireTicket{};
    JobTicket workTicket{};
    JobTicket refreshTicket{};
    JobTicket finalRetireTicket{};
    JobTicket completionTicket{};
    std::atomic<unsigned> factoryCalls{0}, firstRetires{0}, workRuns{0}, refreshRuns{0};
    std::atomic<unsigned> finalRetires{0}, ownerCompletions{0}, completedCalls{0};
    std::atomic<unsigned> deferredRetirements{0}, joinedRetirements{0};
    std::atomic<bool> applied{false}, sourceBeforeApply{false};
    std::atomic<bool> initialRetirementPrecededEveryRun{false};
    bool refuseFactory = false;
    static void Notice(void* raw, JobNotice notice) noexcept
    {
        if (notice.event == JobNoticeKind::Submitted)
        {
            auto& value = *static_cast<Probe*>(raw);
            value.submittedTicket = notice.ticket;
            value.submissions.fetch_add(1, std::memory_order_release);
        }
    }
};

struct Fixture;

class FinalStage final : public Job
{
  public:
    FinalStage(Probe& probe, SelectionOutlineGestureState& result, SelectionPixelFinalWork work,
               JobScheduler& scheduler, DocumentSession& session) noexcept
        : _probe(probe), _result(&result), _closure(std::move(work.closure)), _finalMemory(std::move(work.finalMemory)),
          _temporaryMemory(std::move(work.temporaryMemory)), _remainingCpuBytes(work.remainingCpuBytes),
          _finalOutputBytes(work.finalOutputBytes), _scheduler(scheduler), _session(session), _ticket(result.original)
    {
    }

    JobResult Run(JobContext& context) override
    {
        const auto id = std::this_thread::get_id();
        if (_retireOnly)
        {
            return {};
        }
        if (context.reservedMemoryBytes != _remainingCpuBytes)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_refreshRequested)
        {
            _probe.refreshThread = id;
            _probe.refreshTicket = _ticket;
            _probe.refreshRuns.fetch_add(1, std::memory_order_release);
            _refreshReady = _closure && _closure->RefreshHistoryResidency(context.cancellation);
            _refreshRequested = false;
            return _refreshReady ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
        }
        _probe.workThread = id;
        _probe.workTicket = _ticket;
        _probe.workRuns.fetch_add(1, std::memory_order_release);
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_retireOnly)
        {
            _probe.finalRetireThread = std::this_thread::get_id();
            _probe.finalRetireTicket = _ticket;
            _closure.reset();
            _finalMemory.reset();
            _temporaryMemory.reset();
            _probe.finalRetires.fetch_add(1, std::memory_order_release);
        }
        else if (result.status != JobStatus::Finished)
        {
            _closure.reset();
            _finalMemory.reset();
            _temporaryMemory.reset();
        }
        else if (_probe.firstRetires.fetch_add(1, std::memory_order_acq_rel) == 0)
        {
            _probe.firstRetireThread = std::this_thread::get_id();
            _probe.initialRetireTicket = _ticket;
            _probe.initialRetirementPrecededEveryRun.store(_probe.workRuns.load(std::memory_order_acquire) == 0,
                                                           std::memory_order_release);
        }
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        _probe.deferredRetirements.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        if (ticket != _result->original)
            return false;
        if (_probe.ownerCompletions.fetch_add(1, std::memory_order_acq_rel) == 0)
        {
            if (result.status != JobStatus::Finished)
                return false;
            return ResumeStage(); // Begin a useful delegated stage on the original ticket.
        }
        if (result.status != JobStatus::Finished)
            return false;
        if (!_closure)
            return false;

        if (_refreshReady)
            _refreshReady = false;
        DocumentHistory::ResidencyPermit permit;
        const auto residency = _closure->TryAcquireHistoryResidency(permit);
        if (residency == DocumentHistory::ResidencyValidation::Invalid)
            return RetireAfterRefusal({JobStatus::Failed, E_ABORT});
        if (residency != DocumentHistory::ResidencyValidation::Ready)
        {
            permit.Reset();
            _refreshRequested = true;
            return ResumeStage();
        }

        Pixels source{};
        const auto* layer = _session.ActiveLayer();
        if (layer && layer->asset)
            layer->asset->image.ReadRegion(0, 0, 5, 3, source.data(), 20, 0);
        _probe.sourceBeforeApply.store(layer && layer->asset && source == Source(), std::memory_order_release);
        const auto bound = _finalMemory && _temporaryMemory &&
                           _finalMemory->BindPersistentOutputForPublication(_scheduler, ticket, _finalOutputBytes,
                                                                            *_temporaryMemory) == S_OK;
        const bool applied = bound && _session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(*_closure);
        _probe.applied.store(applied, std::memory_order_release);
        if (!applied)
        {
            permit.Reset();
            return RetireAfterRefusal({JobStatus::Failed, E_ABORT});
        }
        _applied = true;
        permit.Reset();
        _retireOnly = true;
        return ResumeStage(); // Destroy closure and its retired source only on the worker.
    }

    void Completed(JobResult result) noexcept override
    {
        _probe.completedCalls.fetch_add(1, std::memory_order_relaxed);
        _probe.completionTicket = _ticket;
        _result->pixelFinalClosed = _applied;
        _result->pixelFinalOutcome =
            _applied ? JobResult{}
                     : _failure.value_or(result.status == JobStatus::Finished ? JobResult{JobStatus::Failed, E_ABORT}
                                                                              : result);
    }

    void RetainJoinedRetirement(JobScheduler&, JobTicket) noexcept override
    {
        _probe.joinedRetirements.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    bool ResumeStage() noexcept
    {
        return _scheduler.Resume(_ticket);
    }

    bool RetireAfterRefusal(JobResult failure) noexcept
    {
        _failure = failure;
        _retireOnly = true;
        return ResumeStage();
    }

    Probe& _probe;
    SelectionOutlineGestureState* const _result;
    std::optional<PreparedPixelMoveClosure> _closure;
    std::shared_ptr<SharedPublicationMemory> _finalMemory;
    std::shared_ptr<SharedPublicationMemory> _temporaryMemory;
    const std::uint64_t _remainingCpuBytes;
    const std::uint64_t _finalOutputBytes;
    JobScheduler& _scheduler;
    DocumentSession& _session;
    JobTicket _ticket{};
    bool _refreshRequested = false;
    bool _refreshReady = false;
    bool _retireOnly = false;
    bool _applied = false;
    std::optional<JobResult> _failure;
};

std::unique_ptr<Job> MakeFinalStage(void* raw, SelectionOutlineGestureState& result, SelectionPixelFinalWork work)
{
    auto& probe = *static_cast<Probe*>(raw);
    probe.factoryThread = std::this_thread::get_id();
    probe.factoryTicket = result.original;
    probe.factoryCalls.fetch_add(1, std::memory_order_release);
    if (probe.refuseFactory)
        return {};
    return std::make_unique<FinalStage>(probe, result, std::move(work), result.scheduler, result.session);
}

struct Fixture final
{
    Memory memory;
    Probe probe;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostSelectionPressInput> prefix = std::make_unique<HostSelectionPressInput>();
    std::unique_ptr<SelectionOutlineGestureState> state;
    std::optional<SelectionOutlineGestureMemory> admitted;
    Guid active{};

    explicit Fixture(bool duplicate, bool refuseFactory = false)
    {
        probe.refuseFactory = refuseFactory;
        DocumentFixture document;
        document.width = 5;
        document.height = 3;
        document.selection = DocumentSelection{false, {{{1, 1}, {2, 1}, {2, 2}, {1, 2}}}};
        auto layer = ImageLayer::Blank(L"Pixel final delegate", {5, 3});
        const auto source = Source();
        layer.asset =
            RasterAsset{Raster::Make(5, 3, RasterFormat::Bgra8888, source.data(), source.size()), {}, layer.name};
        active = layer.id;
        document.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(document).Seal(), active);
        session->SetSelection(DocumentSelection{false, {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}}, L"Redo seed");
        session->Undo();
        if (!session->history.CanRedo() || jobs.Initialize(2, {Probe::Notice, &probe}, {Memory::Read, &memory}) != 0 ||
            !jobs.EnableDeferredRelease())
            Broken("seed-redo-and-start-scheduler");
        admitted = DescribeSelectionPixelGestureMemory(*session, LassoKind::Rectangle, SelectionMode::New);
        if (!admitted)
            Broken("complete-pixel-admission");
        HostSelectionPressInput::Context arrival;
        arrival.document = admitted->source.document;
        arrival.ordinal = 1;
        arrival.active = active;
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        arrival.tool = HostSelectionPressInput::Tool::Marquee;
        arrival.kind = LassoKind::Rectangle;
        arrival.mode = SelectionMode::New;
        if (!prefix->Begin(81, arrival, {1.5, 1.5}, {false, true, duplicate}, false))
            Broken("captured-control-alt-down");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, *prefix, 81, admitted->limits);
        state->admissionSource = admitted->source;
        state->drawingPlan = admitted->drawing;
        state->allowInitialPixelMove = true;
        state->requireCapturedTarget = true;
        state->pixelFinalFactory = MakeFinalStage;
        state->pixelFinalHost = &probe;
    }

    ~Fixture()
    {
        jobs.Close();
    }

    void Finish(Vec2 point)
    {
        if (prefix->Move(81, point, {}, false, 2) != HostSelectionPressInput::Admission::Recorded ||
            prefix->Finish(81, false, 3, false) != HostSelectionPressInput::Admission::Recorded)
            Broken("accepted-move-and-up");
        std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
        if (jobs.Submit(job,
                        {.kind = JobClass::Selection,
                         .serialKey = session->DocumentSerial(),
                         .memoryBytes = admitted->limits.totalBytes,
                         .deferredRetirement = true,
                         .allowMemoryWaiting = false,
                         .reserveRetainedMemory = true,
                         .continuingOutputLimit = admitted->limits.continuingOutputBytes},
                        state->original) != 0)
            Broken("submit-one-original-ticket");
        if (SettleJobs(jobs, 15000) != SettleResult::Settled)
            Broken("settle-original-admission");
    }
};

void VerifyDelegated(bool duplicate, int& failures)
{
    Fixture fixture(duplicate);
    const auto initialGeneration = fixture.session->Generation();
    const auto initialUndo = fixture.session->history.UndoCount();
    fixture.Finish({3.5, 2.5});
    auto& p = fixture.probe;
    Expect(fixture.state->completed && fixture.state->sameTicket && fixture.state->inputAcknowledged &&
               fixture.state->pixelMove && fixture.state->closed &&
               fixture.state->outcome.status == JobStatus::Finished,
           duplicate ? "alt-duplicate-original-gesture-finishes" : "move-original-gesture-finishes", failures);
    const bool refreshConsistent =
        p.refreshRuns == 0 || (p.refreshThread != p.owner && p.refreshTicket == fixture.state->original);
    Expect(p.factoryCalls == 1 && p.workRuns == 1 && p.finalRetires == 1 && p.ownerCompletions == 3 + p.refreshRuns &&
               refreshConsistent && p.completedCalls == 1 && p.factoryTicket == fixture.state->original &&
               p.deferredRetirements == p.ownerCompletions && p.joinedRetirements == 0 &&
               p.initialRetirementPrecededEveryRun && p.initialRetireTicket == fixture.state->original &&
               p.workTicket == fixture.state->original && p.finalRetireTicket == fixture.state->original &&
               p.completionTicket == fixture.state->original && p.factoryThread != p.owner &&
               p.firstRetireThread != p.owner && p.workThread != p.owner && p.finalRetireThread != p.owner &&
               p.submissions == 1 && p.submittedTicket == fixture.state->original && fixture.memory.queries >= 1,
           "factory-work-and-retirement-use-worker-on-same-original-ticket", failures);
    Expect(p.applied && p.sourceBeforeApply && Matches(*fixture.session, Expected(duplicate)) &&
               fixture.session->Generation() == initialGeneration + 1 &&
               fixture.session->history.UndoCount() == initialUndo + 1 && !fixture.session->history.CanRedo(),
           "owner-publication-applies-literal-bgra-once-and-transfers-history-charge", failures);
    fixture.session->Undo();
    Expect(Matches(*fixture.session, Source()) && fixture.session->history.CanRedo(),
           "single-undo-restores-independent-literal-source", failures);
    fixture.session->Redo();
    Expect(Matches(*fixture.session, Expected(duplicate)) && fixture.session->history.UndoCount() == initialUndo + 1,
           "single-redo-restores-literal-published-pixels", failures);
}

void VerifyZeroOffset(int& failures)
{
    Fixture fixture(false);
    const auto generation = fixture.session->Generation();
    const auto revision = fixture.session->history.CurrentRevision();
    const auto undoCount = fixture.session->history.UndoCount();
    fixture.Finish({1.5, 1.5});
    Expect(fixture.state->completed && fixture.state->closed && fixture.state->pixelMove &&
               fixture.state->outcome.status == JobStatus::Finished && fixture.probe.factoryCalls == 0 &&
               Matches(*fixture.session, Source()) && fixture.session->Generation() == generation &&
               fixture.session->history.CurrentRevision() == revision &&
               fixture.session->history.UndoCount() == undoCount && fixture.session->history.CanRedo(),
           "zero-offset-bypasses-factory-and-preserves-pixels-and-redo", failures);
}

void VerifyNullFactory(int& failures)
{
    Fixture fixture(false, true);
    const auto generation = fixture.session->Generation();
    const auto revision = fixture.session->history.CurrentRevision();
    const auto undoCount = fixture.session->history.UndoCount();
    fixture.Finish({3.5, 2.5});
    Expect(fixture.probe.factoryCalls == 1 && fixture.state->completed &&
               fixture.state->outcome.status == JobStatus::Failed && fixture.state->outcome.error == E_OUTOFMEMORY &&
               !fixture.state->closed && fixture.probe.submissions == 1 && Matches(*fixture.session, Source()) &&
               fixture.session->Generation() == generation && fixture.session->history.CurrentRevision() == revision &&
               fixture.session->history.UndoCount() == undoCount && fixture.session->history.CanRedo(),
           "null-selected-factory-refuses-without-cpu-fallback-or-document-history-mutation", failures);
}
} // namespace

void Run(int& failures)
{
    VerifyDelegated(false, failures);
    VerifyDelegated(true, failures);
    VerifyZeroOffset(failures);
    VerifyNullFactory(failures);
}
} // namespace PixelFinalDelegateTests

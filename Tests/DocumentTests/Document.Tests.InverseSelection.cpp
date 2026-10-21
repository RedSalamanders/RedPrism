#include "Document.Tests.InverseSelection.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "InverseSelectionWork.h"

#include "Document.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"
#include <stop_token>
#include <thread>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s inverse-selection.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSession Session(int width = 4, int height = 4)
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    document.layers.push_back(
        ImageLayer::Blank(L"Inverse selection fixture", Vec2{static_cast<double>(width), static_cast<double>(height)}));
    const Guid active = document.layers.front().id;
    document.layers.push_back(ImageLayer::Blank(L"Inverse selection second target",
                                                Vec2{static_cast<double>(width), static_cast<double>(height)}));
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionGeometry::Rect(x, y, width, height)};
}

struct State final
{
    std::optional<CanvasDocument> document;
    Guid revision;
    int undoCount = 0;
    DocumentHistory::StateView history;
    std::wstring undoName;
    std::wstring redoName;
    std::uint64_t generation = 0;

    explicit State(const DocumentSession& session)
        : document(session.Document()), revision(session.history.CurrentRevision()),
          undoCount(session.history.UndoCount()), history(session.history.CaptureState()),
          undoName(session.history.UndoName()), redoName(session.history.RedoName()), generation(session.Generation())
    {
    }
};

bool SameState(const DocumentSession& session, const State& state)
{
    return session.Document() == state.document && session.history.CurrentRevision() == state.revision &&
           session.history.UndoCount() == state.undoCount && session.history.IsCurrent(state.history) &&
           session.history.CaptureState().FutureCount() == state.history.FutureCount() &&
           session.history.UndoName() == state.undoName && session.history.RedoName() == state.redoName &&
           session.Generation() == state.generation;
}

std::optional<PreparedInverseSelection> Prepare(DocumentSession& session, CancelToken cancellation = {},
                                                DocumentHistory::PrepareObserver historyObserver = {},
                                                std::shared_ptr<const SharedPublicationMemory> memory = {},
                                                const InverseSelectionObserver* observer = nullptr)
{
    auto input = session.PrepareInverseSelectionInput();
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareInverseSelectionPublication(std::move(input.value()), cancellation, historyObserver,
                                                               std::move(memory), observer);
}

void LegacyRectangleGoldenAndHistory()
{
    DocumentSession session = Session(100, 100);
    session.SetSelection(Rect(0, 0, 100, 100), L"Select All");
    session.SetSelection(Rect(0, 0, 50, 50), L"Lasso");
    auto prepared = Prepare(session);
    Expect(prepared && prepared->ChangesSelection() && session.ApplyPreparedInverseSelectionIfCurrent(prepared.value()),
           "legacy.inverse-publishes-changed-selection");
    const auto selection = session.Selection();
    if (selection)
    {
        const Raster coverage = SelectionGeometry::Coverage(selection->loops.Get(), 100, 100, selection->antialiased);
        const auto* pixels = coverage.Data();
        Expect(pixels && pixels[25 * 100 + 25] == 0 && pixels[75 * 100 + 75] == 255,
               "legacy.100-square-inverse-coverage-golden");
    }
    else
    {
        Expect(false, "legacy.100-square-inverse-coverage-golden");
    }
    Expect(session.history.UndoName() == L"Inverse", "history.inverse-title");
    session.Undo();
    Expect(session.Selection() == std::optional<DocumentSelection>{Rect(0, 0, 50, 50)} &&
               session.history.RedoName() == L"Inverse",
           "history.undo-restores-prior-selection-and-redo-name");
    session.Redo();
    Expect(session.Selection() == selection, "history.redo-restores-inverse-selection");
}

void AbsentAndExplicitEmptySemantics()
{
    DocumentSession absent = Session(4, 3);
    absent.SetSelection(Rect(0, 0, 2, 2), L"Undo seed");
    absent.Undo();
    Expect(absent.history.CanRedo(), "absent.redo-fixture-ready");
    const State before(absent);
    auto noOp = Prepare(absent);
    Expect(noOp && !noOp->ChangesSelection() && absent.ApplyPreparedInverseSelectionIfCurrent(noOp.value()) &&
               !absent.Selection() && SameState(absent, before) && absent.history.CanRedo(),
           "absent.inverse-is-no-op-retains-generation-history-and-redo");

    DocumentSession empty = Session(4, 3);
    empty.SetSelection(DocumentSelection{true, {}}, L"Explicit empty");
    auto fill = Prepare(empty);
    Expect(fill && fill->ChangesSelection() && empty.ApplyPreparedInverseSelectionIfCurrent(fill.value()) &&
               empty.Selection() && empty.Selection()->antialiased,
           "empty.inverse-publishes-full-canvas-and-preserves-antialiasing");
    const auto full = empty.Selection();
    const Raster fullCoverage = full ? SelectionGeometry::Coverage(full->loops.Get(), 4, 3, true) : Raster{};
    bool fullPixels = fullCoverage.Data() != nullptr;
    for (int pixel = 0; pixel < 12 && fullPixels; ++pixel)
        fullPixels = fullCoverage.Data()[pixel] == 255;
    Expect(full && full->loops.size() == 1 && full->loops.PointCount() == 4 && fullPixels,
           "empty.inverse-full-canvas-geometry");
    auto clear = Prepare(empty);
    Expect(clear && clear->ChangesSelection() && empty.ApplyPreparedInverseSelectionIfCurrent(clear.value()) &&
               empty.Selection() && empty.Selection()->loops.empty(),
           "full.inverse-becomes-explicit-empty-not-nullopt");
}

void BoundedGeometryGoldens()
{
    DocumentSession outside = Session(4, 3);
    outside.SetSelection(Rect(-2, -1, 8, 5), L"Outside canvas");
    auto outsidePrepared = Prepare(outside);
    Expect(outsidePrepared && outside.ApplyPreparedInverseSelectionIfCurrent(outsidePrepared.value()) &&
               outside.Selection() && outside.Selection()->loops.empty(),
           "geometry.out-of-canvas-selection-complements-to-explicit-empty");

    DocumentSession disjoint = Session(4, 3);
    disjoint.SetSelection(Rect(6, 5, 2, 2), L"Disjoint from canvas");
    auto disjointPrepared = Prepare(disjoint);
    const bool disjointApplied = disjointPrepared && disjoint.ApplyPreparedInverseSelectionIfCurrent(*disjointPrepared);
    const Raster disjointCoverage =
        disjoint.Selection() ? SelectionGeometry::Coverage(disjoint.Selection()->loops.Get(), 4, 3, false) : Raster{};
    bool disjointPixels = disjointCoverage.Data() != nullptr;
    for (int pixel = 0; pixel < 12 && disjointPixels; ++pixel)
        disjointPixels = disjointCoverage.Data()[pixel] == 255;
    Expect(disjointApplied && disjoint.Selection() && disjoint.Selection()->loops.size() == 1 &&
               disjoint.Selection()->loops.PointCount() == 4 && disjointPixels,
           "geometry.disjoint-out-of-canvas-selection-complements-to-full-canvas");

    // Outer loop is clockwise; inner loop is counterclockwise, defining a 2x2 square hole.
    DocumentSession hole = Session(4, 4);
    const DocumentSelection donut{false,
                                  {SelectionLoop{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 4}, Vec2{0, 4}},
                                   SelectionLoop{Vec2{1, 1}, Vec2{1, 3}, Vec2{3, 3}, Vec2{3, 1}}}};
    hole.SetSelection(donut, L"Hole");
    auto holePrepared = Prepare(hole);
    Expect(holePrepared && hole.ApplyPreparedInverseSelectionIfCurrent(holePrepared.value()) && hole.Selection(),
           "geometry.hole-complement-publishes");
    if (hole.Selection())
    {
        const Raster coverage = SelectionGeometry::Coverage(hole.Selection()->loops.Get(), 4, 4, false);
        const auto* pixels = coverage.Data();
        bool exact = pixels != nullptr;
        for (int y = 0; y < 4 && exact; ++y)
            for (int x = 0; x < 4; ++x)
                exact = exact && pixels[y * 4 + x] == ((x >= 1 && x < 3 && y >= 1 && y < 3) ? 255 : 0);
        Expect(exact, "geometry.hole-complement-literal-coverage");
    }
    else
    {
        Expect(false, "geometry.hole-complement-literal-coverage");
    }

    DocumentSession subpixel = Session(2, 1);
    subpixel.SetSelection(Rect(0.25, 0, 0.5, 1, true), L"Subpixel");
    auto subpixelPrepared = Prepare(subpixel);
    Expect(subpixelPrepared && subpixel.ApplyPreparedInverseSelectionIfCurrent(subpixelPrepared.value()) &&
               subpixel.Selection() && subpixel.Selection()->antialiased,
           "geometry.subpixel-complement-preserves-aa");
    if (subpixel.Selection())
    {
        const Raster coverage = SelectionGeometry::Coverage(subpixel.Selection()->loops.Get(), 2, 1, true);
        const auto* pixels = coverage.Data();
        Expect(pixels && pixels[0] == 128 && pixels[1] == 255,
               "geometry.subpixel-difference-hand-calculated-area-coverage");
    }
    else
    {
        Expect(false, "geometry.subpixel-difference-hand-calculated-area-coverage");
    }
}

struct HistoryFault final
{
    DocumentHistory::PrepareStage target = DocumentHistory::PrepareStage::BeforeSnapshot;
    int hits = 0;
};

void ThrowAtHistoryStage(void* context, DocumentHistory::PrepareStage stage)
{
    auto& fault = *static_cast<HistoryFault*>(context);
    if (stage == fault.target)
    {
        ++fault.hits;
        throw std::bad_alloc{};
    }
}

void CaptureCancellationAndMovedInput()
{
    DocumentSession session = Session();
    const State before(session);
    auto input = session.PrepareInverseSelectionInput();
    CancelSource cancel;
    static_cast<void>(cancel.Cancel());
    auto cancelled = input
                         ? DocumentSession::PrepareInverseSelectionPublication(std::move(input.value()), cancel.Token())
                         : std::nullopt;
    Expect(!cancelled && SameState(session, before), "cancel.cancelled-captured-input-does-not-publish");

    auto captured = session.PrepareInverseSelectionInput();
    Expect(captured.has_value(), "input.capture-succeeds");
    if (!captured)
        return;
    InverseSelectionInput moved(std::move(captured.value()));
    auto refused = DocumentSession::PrepareInverseSelectionPublication(std::move(captured.value()));
    Expect(!refused, "input.moved-from-capture-is-refused");
    auto valid = DocumentSession::PrepareInverseSelectionPublication(std::move(moved));
    Expect(valid && session.ApplyPreparedInverseSelectionIfCurrent(valid.value()),
           "input.moved-capture-remains-consumable");
}

void FaultsStaleAndAdmission()
{
    DocumentSession faulted = Session();
    faulted.SetSelection(Rect(0, 0, 2, 2), L"Fault seed");
    const State beforeFault(faulted);
    HistoryFault fault;
    const DocumentHistory::PrepareObserver historyObserver{&ThrowAtHistoryStage, &fault};
    bool caught = false;
    try
    {
        static_cast<void>(Prepare(faulted, {}, historyObserver));
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
    }
    Expect(caught && fault.hits == 1 && SameState(faulted, beforeFault),
           "history.before-snapshot-allocation-fault-preserves-document-and-history");

    DocumentSession staleTarget = Session();
    staleTarget.SetSelection(Rect(0, 0, 2, 2), L"Stale target seed");
    auto targetPrepared = Prepare(staleTarget);
    const auto otherTarget = staleTarget.Document()->layers.back().id;
    staleTarget.SelectLayer(otherTarget);
    const State changedTarget(staleTarget);
    Expect(targetPrepared && !staleTarget.IsPreparedInverseSelectionCurrent(targetPrepared.value()) &&
               !staleTarget.ApplyPreparedInverseSelectionIfCurrent(targetPrepared.value()) &&
               SameState(staleTarget, changedTarget),
           "stale.changed-target-is-rejected-with-live-state-untouched");

    DocumentSession staleDocument = Session();
    staleDocument.SetSelection(Rect(0, 0, 2, 2), L"Stale document seed");
    auto documentPrepared = Prepare(staleDocument);
    staleDocument.SetSelection(Rect(0, 0, 1, 1), L"Concurrent document edit");
    const State changedDocument(staleDocument);
    Expect(documentPrepared && !staleDocument.IsPreparedInverseSelectionCurrent(documentPrepared.value()) &&
               !staleDocument.ApplyPreparedInverseSelectionIfCurrent(documentPrepared.value()) &&
               SameState(staleDocument, changedDocument),
           "stale.changed-document-is-rejected-with-live-state-untouched");

    DocumentSession limits = Session();
    limits.SetSelection(Rect(0, 0, 2, 2), L"Limit seed");
    const State beforeLimits(limits);
    InverseSelectionObserver tiny{};
    tiny.limits = BoundedSelectionBoolean::Limits{};
    tiny.limits.workspaceBytes = 1;
    bool workspaceRefused = false;
    try
    {
        workspaceRefused = !Prepare(limits, {}, {}, {}, &tiny);
    }
    catch (const std::bad_alloc&)
    {
        workspaceRefused = true;
    }
    Expect(workspaceRefused && SameState(limits, beforeLimits), "limits.workspace-one-byte-refuses-without-mutation");
    InverseSelectionObserver noAllocation{};
    noAllocation.limits.allocations = 0;
    bool allocationRefused = false;
    try
    {
        allocationRefused = !Prepare(limits, {}, {}, {}, &noAllocation);
    }
    catch (const std::bad_alloc&)
    {
        allocationRefused = true;
    }
    Expect(allocationRefused && SameState(limits, beforeLimits),
           "limits.first-engine-allocation-refuses-without-mutation");
    InverseSelectionObserver raised{};
    raised.limits = BoundedSelectionBoolean::Limits{};
    ++raised.limits.workspaceBytes;
    auto overCeiling = Prepare(limits, {}, {}, {}, &raised);
    Expect(!overCeiling && SameState(limits, beforeLimits), "limits.test-observer-cannot-raise-production-ceiling");

    const auto plan = limits.PlanInverseSelectionMemory();
    Expect(plan && plan->hasSelection && plan->captureHistoryBytes != 0 && plan->geometryOutputBytes != 0 &&
               plan->workspaceBytes != 0 && plan->reservationBytes >= plan->preparationBytes,
           "limits.memory-plan-covers-selection-work-and-reservation");
}
struct InverseCheckpoint final
{
    std::stop_source* cancellation = nullptr;
    std::uint64_t lastWork = 0;
    std::uint32_t calls = 0;

    static void StopAtFirstEngineCheckpoint(void* context, std::uint64_t work) noexcept
    {
        auto& self = *static_cast<InverseCheckpoint*>(context);
        self.lastWork = work;
        ++self.calls;
        if (work == 64)
            static_cast<void>(self.cancellation->request_stop());
    }
};

void CancelInsideInverseBooleanLeavesLiveStateUntouched()
{
    DocumentSession session = Session(4, 4);
    session.SetSelection(Rect(0, 0, 2, 2), L"Inverse cancellation seed");

    // A simple, dense closed rectangle ensures InputPaths reaches the real engine's
    // 64-work-unit checkpoint. The geometry itself is not used as an expected result.
    SelectionLoop denseRectangle;
    denseRectangle.reserve(256);
    for (std::uint32_t i = 0; i < 64; ++i)
        denseRectangle.push_back(Vec2{static_cast<double>(i) / 16.0, 0});
    for (std::uint32_t i = 0; i < 64; ++i)
        denseRectangle.push_back(Vec2{4, static_cast<double>(i) / 16.0});
    for (std::uint32_t i = 0; i < 64; ++i)
        denseRectangle.push_back(Vec2{4.0 - static_cast<double>(i) / 16.0, 4});
    for (std::uint32_t i = 0; i < 64; ++i)
        denseRectangle.push_back(Vec2{0, 4.0 - static_cast<double>(i) / 16.0});
    session.SetSelection(DocumentSelection{false, SelectionLoops{std::move(denseRectangle)}}, L"Dense seed");
    const State originalBeforeWork(session);

    auto captured = session.PrepareInverseSelectionInput();
    Expect(captured.has_value(), "cancel.checkpoint-input-captured");
    if (!captured)
        return;

    std::stop_source cancellation;
    InverseCheckpoint checkpoint{&cancellation};
    InverseSelectionObserver observer{};
    observer.checkpoint = &InverseCheckpoint::StopAtFirstEngineCheckpoint;
    observer.context = &checkpoint;

    bool cancellationReachedCaller = false;
    try
    {
        static_cast<void>(DocumentSession::PrepareInverseSelectionPublication(
            std::move(captured.value()), CancelToken{cancellation.get_token()}, {}, {}, &observer));
    }
    catch (const std::bad_alloc&)
    {
        // BoundedSelectionBoolean::Workspace::Refuse is normalized by the production job runner;
        // the direct native test observes its documented cancellation exception boundary here.
        cancellationReachedCaller = cancellation.stop_requested();
    }

    Expect(checkpoint.calls != 0 && checkpoint.lastWork == 64 && cancellation.stop_requested() &&
               cancellationReachedCaller,
           "cancel.real-bounded-engine-checkpoint-hit-at-64-during-work");
    Expect(SameState(session, originalBeforeWork), "cancel.live-document-history-and-generation-unchanged");
}

// Spliced into the test's existing anonymous namespace after independent review.
struct InverseAccountingState final
{
    DocumentSession* session = nullptr;
    JobScheduler* scheduler = nullptr;
    std::thread::id ownerThread{};
    std::optional<InverseSelectionInput> input;
    std::optional<PreparedInverseSelection> prepared;
    std::shared_ptr<SharedPublicationMemory> output, temporary;
    std::uint64_t wholeBytes = 0, outputBytes = 0;
    const void* outputIdentity = nullptr;
    std::int32_t bindResult = E_UNEXPECTED;
    bool captured = false, admissionAtCapture = false, preparedOffOwner = false, applied = false;
    JobResult completed{JobStatus::Failed, E_UNEXPECTED};
};

class InverseAccountingJob final : public Job
{
  public:
    explicit InverseAccountingJob(InverseAccountingState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_state.captured)
        {
            _state.output = SharedPublicationMemory::CreateForWork();
            _state.temporary = SharedPublicationMemory::CreateForWork();
            return {};
        }
        if (!_state.input)
            return {JobStatus::Failed, E_UNEXPECTED};
        _state.prepared = DocumentSession::PrepareInverseSelectionPublication(std::move(*_state.input),
                                                                              context.cancellation, {}, _state.output);
        _state.preparedOffOwner = std::this_thread::get_id() != _state.ownerThread;
        if (!_state.prepared)
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                      : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
        _state.outputBytes = _state.prepared->OutputAllocationBytes();
        const auto* selected = _state.prepared->SelectionForDisplayForWork();
        _state.outputIdentity = selected ? selected->loops.Identity() : nullptr;
        return {};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished)
            return false;
        if (!_state.captured)
        {
            _state.captured = true;
            _state.admissionAtCapture = _state.scheduler->ReservedMemoryBytes() == _state.wholeBytes;
            try
            {
                _state.input = _state.session->PrepareInverseSelectionInput();
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            return _state.input && _state.scheduler->Resume(ticket);
        }
        if (!_state.prepared || !_state.session->IsPreparedInverseSelectionCurrent(*_state.prepared))
            return false;
        DocumentHistory::ResidencyPermit permit;
        if (_state.prepared->TryAcquireHistoryResidency(permit) != DocumentHistory::ResidencyValidation::Ready)
            return false;
        _state.bindResult = _state.output->BindPersistentOutputForPublication(*_state.scheduler, ticket,
                                                                              _state.outputBytes, *_state.temporary);
        if (_state.bindResult == S_OK)
            _state.applied = _state.session->ApplyPreparedInverseSelectionIfCurrent(*_state.prepared);
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.completed = result;
    }

  private:
    InverseAccountingState& _state;
};

bool InverseAccountingMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = kMemoryAdmissionHeadroomBytes + 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

void InverseOriginalTicketChargeFollowsHistoryAndLastWeak()
{
    DocumentSession session = Session();
    session.SetSelection(Rect(0, 0, 2, 2), L"Charged inverse seed");
    const auto plan = session.PlanInverseSelectionMemory();
    Expect(plan && plan->reservationBytes != 0, "accounting.original-complete-plan-present");
    if (!plan)
        return;
    // State outlives scheduler's joined Close even on a failed setup assertion.
    InverseAccountingState state;
    JobScheduler scheduler;
    state.session = &session;
    state.scheduler = &scheduler;
    state.wholeBytes = plan->reservationBytes;
    state.ownerThread = std::this_thread::get_id();
    const bool started =
        scheduler.Initialize(2, {}, {&InverseAccountingMemory, nullptr}) == S_OK && scheduler.EnableDeferredRelease();
    Expect(started, "accounting.real-scheduler-and-retirement-sink-start");
    if (!started)
        return;
    std::unique_ptr<Job> job = std::make_unique<InverseAccountingJob>(state);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.memoryBytes = state.wholeBytes;
    options.allowMemoryWaiting = false;
    options.reserveRetainedMemory = true;
    options.deferredRetirement = true;
    JobTicket original;
    const bool admitted = scheduler.Submit(job, options, original) == S_OK && !job;
    Expect(admitted, "accounting.original-ticket-admitted-before-capture");
    if (!admitted)
        return;
    const bool settled = SettleJobs(scheduler, 10000) == SettleResult::Settled;
    Expect(settled && state.completed.status == JobStatus::Finished && state.admissionAtCapture &&
               state.preparedOffOwner && state.bindResult == S_OK && state.applied,
           "accounting.same-ticket-worker-preparation-bind-and-current-apply");
    if (!settled || !state.applied || !session.Selection())
        return;
    Expect(state.outputBytes != 0 && state.outputBytes < state.wholeBytes &&
               state.output->BoundTo(scheduler, original) && state.temporary->BoundTo(scheduler, original) &&
               state.output->Bytes() == state.outputBytes &&
               state.temporary->Bytes() == state.wholeBytes - state.outputBytes,
           "accounting.exact-original-partition-with-no-new-ticket");
    SelectionLoops copied = session.Selection()->loops;
    auto weakGeometry = copied.WeakStorageForTest();
    std::weak_ptr<SharedPublicationMemory> weakMemory = state.output;
    state.input.reset();
    state.prepared.reset();
    state.output.reset();
    state.temporary.reset();
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled &&
               scheduler.ReservedMemoryBytes() == state.outputBytes && !weakMemory.expired(),
           "accounting.temporary-refunds-output-charge-remains");
    session.Undo();
    Expect(session.history.RedoName() == L"Inverse" && !weakGeometry.expired() &&
               scheduler.ReservedMemoryBytes() == state.outputBytes,
           "accounting.undo-history-retains-output-charge");
    session.Redo();
    Expect(session.Selection() && session.Selection()->loops.Identity() == state.outputIdentity &&
               scheduler.ReservedMemoryBytes() == state.outputBytes,
           "accounting.redo-restores-identical-charged-output");
    session.ClearDocument();
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled && !weakGeometry.expired() &&
               scheduler.ReservedMemoryBytes() == state.outputBytes,
           "accounting.copied-reader-retains-charge-after-document-history-close");
    copied = {};
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled && weakGeometry.expired() && !weakMemory.expired() &&
               scheduler.ReservedMemoryBytes() == state.outputBytes,
           "accounting.geometry-last-weak-control-retains-charge-after-worker-release");
    weakGeometry.reset();
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled && weakMemory.expired() &&
               scheduler.ReservedMemoryBytes() == state.outputBytes,
           "accounting.memory-last-weak-control-still-retains-original-charge");
    weakMemory.reset();
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled && scheduler.ReservedMemoryBytes() == 0,
           "accounting.final-weak-release-refunds-exact-original-once");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled && scheduler.ReservedMemoryBytes() == 0,
           "accounting.second-drain-does-not-refund-again");
}

} // namespace

int RunInverseSelectionTests()
{
    gAssertions = 0;
    gFailures = 0;
    LegacyRectangleGoldenAndHistory();
    AbsentAndExplicitEmptySemantics();
    BoundedGeometryGoldens();
    CaptureCancellationAndMovedInput();
    FaultsStaleAndAdmission();
    CancelInsideInverseBooleanLeavesLiveStateUntouched();
    InverseOriginalTicketChargeFollowsHistoryAndLastWeak();
    std::printf("inverse-selection.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

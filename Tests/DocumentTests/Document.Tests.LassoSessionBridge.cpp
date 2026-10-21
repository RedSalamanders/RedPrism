#include "Document.Tests.LassoSessionBridge.h"

#include "Document.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "LassoFinishWork.h"
#include "LassoOpeningWork.h"
#include "LocallyOwnedPublication.h"
#include "MemoryAdmission.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4ULL * 1024 * 1024;
constexpr std::uint64_t kSampleFixedSlack = 64ULL * 1024 * 1024;
constexpr std::uint32_t kOpeningBlocks = 1;
constexpr std::uint32_t kCapacityBlocks = 8;
constexpr std::uint32_t kGateTimeoutMs = 10'000;
unsigned int gChecks = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s lasso-session-bridge.%s\n", condition ? "PASS" : "FAIL", label);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = kMemoryAdmissionHeadroomBytes + kAdmissionBytes + kSampleFixedSlack;
    snapshot = {available, available};
    return true;
}

JobOptions OriginalDescriptor() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Selection;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.allowMemoryWaiting = false;
    options.reserveRetainedMemory = true;
    return options;
}

bool SamePoint(Vec2 left, Vec2 right) noexcept
{
    return left.x == right.x && left.y == right.y;
}

struct OpeningState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::uint64_t reservedBaseline = 0;
    std::uint64_t reservedForThisTicket = 0;
    std::optional<LassoOpeningInput> input;
    std::optional<LassoOpeningInput> heldValidInput;
    Vec2 capturePoint{};
    SelectionMode captureMode = SelectionMode::New;
    LassoKind captureKind = LassoKind::Freehand;
    bool captureMarquee = false;
    bool captureAttempted = false;
    bool captureSucceeded = false;
    bool captureReservationHeld = false;
    bool captureOnOwner = false;
    bool sameTicketResumed = false;
    bool cancelAfterCapture = false;
    bool moveBeforeWorker = false;
    std::optional<PreparedLassoOpening> prepared;
    std::shared_ptr<SharedPublicationMemory> memory;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t boundBytes = 0;
    std::uint64_t reservedInWorker = 0;
    std::thread::id ownerThread{};
    std::thread::id workerThread{};
    bool workerSawOriginalReservation = false;
    bool completedOnOwner = false;
    bool boundToOriginal = false;
    bool unboundApplyRefused = false;
    DocumentSession* session = nullptr;
};

class PrepareOpeningJob final : public Job
{
  public:
    PrepareOpeningJob(OpeningState& state, std::uint32_t blocks) noexcept : _state(state), _blocks(blocks) {}
    JobResult Run(JobContext& context) override
    {
        _state.workerThread = std::this_thread::get_id();
        _state.reservedInWorker = context.reservedMemoryBytes;
        _state.workerSawOriginalReservation = context.reservedMemoryBytes == kAdmissionBytes &&
                                              context.deferredRetirement && _state.workerThread != _state.ownerThread;
        if (!_preparationStage)
        {
            _contextAdmissionHeld = _state.workerSawOriginalReservation;
            return {JobStatus::Finished, S_OK};
        }
        if (!_state.input || !_contextAdmissionHeld || !_state.captureSucceeded)
            return {JobStatus::Failed, E_INVALIDARG};
        const auto localLimit = PreparedLassoOpening::MemoryBytes(_blocks);
        if (!localLimit || !context.reservedMemoryBytes)
            return {JobStatus::Failed, E_INVALIDARG};
        auto memory = SharedPublicationMemory::CreateForWork(localLimit);
        _state.memory = memory;
        auto result = DocumentSession::PrepareLassoOpening(std::move(_state.input.value()), _blocks,
                                                           context.cancellation, memory);
        if (!result)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _state.prepared.emplace(std::move(result.value()));
        return {JobStatus::Finished, S_OK};
    }
    [[nodiscard]] bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_preparationStage || result.status != JobStatus::Finished || result.error != S_OK ||
            !_contextAdmissionHeld || ticket != _state.ticket || !_state.scheduler->InUiCallback() ||
            _state.scheduler->ReservedMemoryBytes() != _state.reservedForThisTicket)
            return false;
        _state.captureReservationHeld = true;
        _state.captureOnOwner = true;
        _state.captureAttempted = true;
        if (_state.session)
        {
            const auto liveKind = _state.captureMarquee ? _state.session->marqueeKind : _state.session->lassoKind;
            auto captured = [&]() -> std::optional<LassoOpeningInput>
            {
                if (liveKind != _state.captureKind)
                    return std::nullopt;
                return _state.session->PrepareLassoOpeningInput(_state.capturePoint, _state.captureMode,
                                                                _state.captureMarquee);
            }();
            _state.captureSucceeded = captured.has_value();
            if (captured)
                _state.input.emplace(std::move(captured.value()));
            if (_state.cancelAfterCapture)
                _state.session->CancelLasso();
            if (_state.moveBeforeWorker && _state.input)
                _state.heldValidInput.emplace(std::move(_state.input.value()));
        }
        _preparationStage = true;
        _state.sameTicketResumed = _state.scheduler->Resume(ticket);
        return _state.captureOnOwner && _state.sameTicketResumed;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.terminal = result;
        _state.completedOnOwner = _state.scheduler->InUiCallback();
        if (!_state.memory)
            return;
        if (result.status == JobStatus::Finished && result.error == S_OK && _state.prepared)
        {
            _state.unboundApplyRefused = _state.session &&
                                         !_state.session->ApplyPreparedLassoOpeningIfCurrent(_state.prepared.value()) &&
                                         !_state.session->PendingLasso();
            _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
            _state.boundToOriginal = _state.memory->BoundTo(*_state.scheduler, _state.ticket);
            _state.boundBytes = _state.memory->Bytes();
            if (_state.bindResult != S_OK || !_state.boundToOriginal)
            {
                static_cast<void>(_state.memory->BindForRetirement(*_state.scheduler, _state.ticket));
                _state.prepared.reset();
            }
            return;
        }
        _state.bindResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
        _state.boundToOriginal = _state.memory->BoundTo(*_state.scheduler, _state.ticket);
        _state.boundBytes = _state.memory->Bytes();
        _state.prepared.reset();
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if (_state.memory && !_state.memory->IsBound())
            static_cast<void>(_state.memory->BindForRetirement(scheduler, ticket));
    }

  private:
    OpeningState& _state;
    const std::uint32_t _blocks;
    bool _preparationStage = false;
    bool _contextAdmissionHeld = false;
};
bool StartScheduler(JobScheduler& scheduler) noexcept
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        return false;
    if (!scheduler.EnableDeferredRelease())
    {
        scheduler.Close();
        return false;
    }
    return true;
}

bool SubmitInput(JobScheduler& scheduler, DocumentSession& session, OpeningState& state, Vec2 point, SelectionMode mode,
                 bool marquee, std::uint32_t blocks = kOpeningBlocks)
{
    if (SettleJobs(scheduler, kGateTimeoutMs) != SettleResult::Settled)
        return false;
    state.reservedBaseline = scheduler.ReservedMemoryBytes();
    if (state.reservedBaseline > (std::numeric_limits<std::uint64_t>::max)() - kAdmissionBytes)
        return false;
    state.reservedForThisTicket = state.reservedBaseline + kAdmissionBytes;
    state.scheduler = &scheduler;
    state.session = &session;
    state.capturePoint = point;
    state.captureMode = mode;
    state.captureKind = marquee ? session.marqueeKind : session.lassoKind;
    state.captureMarquee = marquee;
    state.ownerThread = std::this_thread::get_id();
    std::unique_ptr<Job> job = std::make_unique<PrepareOpeningJob>(state, blocks);
    if (scheduler.Submit(job, OriginalDescriptor(), state.ticket) != S_OK || job)
        return false;
    return SettleJobs(scheduler, kGateTimeoutMs) == SettleResult::Settled;
}

bool CaptureAndPrepare(DocumentSession& session, JobScheduler& scheduler, OpeningState& state, Vec2 point,
                       SelectionMode mode, bool marquee, std::uint32_t blocks = kOpeningBlocks)
{
    const bool settled = SubmitInput(scheduler, session, state, point, mode, marquee, blocks);
    return settled && state.captureAttempted && state.captureSucceeded && state.captureReservationHeld &&
           state.captureOnOwner && state.sameTicketResumed && state.workerSawOriginalReservation &&
           state.reservedInWorker == kAdmissionBytes && state.terminal.status == JobStatus::Finished &&
           state.terminal.error == S_OK && state.prepared && state.bindResult == S_OK && state.boundToOriginal &&
           state.boundBytes == kAdmissionBytes;
}
void ReleaseOpening(DocumentSession& session, OpeningState& state) noexcept
{
    session.CancelLasso();
    state.prepared.reset();
    state.memory.reset();
}

void CheckMemoryEnvelope()
{
    const auto oneBlock = PreparedLassoOpening::MemoryBytes(1);
    const auto expectedOneBlock =
        BoundedLassoPoints::MemoryBytes(1) + LocallyOwnedPublicationAllowance<BoundedLassoGesture>();
    Expect(oneBlock != 0 && oneBlock == expectedOneBlock &&
               SharedPublicationMemory::OwnershipAllowance() + oneBlock < kAdmissionBytes,
           "memory.literal-one-block-envelope-fits-original-reservation");
    Expect(PreparedLassoOpening::MemoryBytes(0) == 0 &&
               PreparedLassoOpening::MemoryBytes(BoundedLassoPoints::kMaxBlocks + 1u) == 0 &&
               PreparedLassoOpening::MemoryBytes(BoundedLassoPoints::kMaxBlocks) != 0,
           "memory.zero-max-and-first-invalid-block-boundaries");
}

void CheckCaptureAndSuccessfulApply(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    session.lassoKind = LassoKind::Polygonal;
    OpeningState polygon;
    const bool polygonReady = CaptureAndPrepare(session, scheduler, polygon, {10.25, 20.5}, SelectionMode::Add, false);
    const bool polygonCurrent =
        polygonReady && polygon.prepared && session.IsPreparedLassoOpeningCurrent(polygon.prepared.value());
    const bool polygonApplied = polygonCurrent && session.ApplyPreparedLassoOpeningIfCurrent(polygon.prepared.value());
    const auto& polygonDraft = session.PendingLasso();
    const bool polygonShape =
        polygonApplied && polygonDraft && polygonDraft.value().IsBounded() && polygonDraft.value().points.empty() &&
        polygonDraft.value().mode == SelectionMode::Add && polygonDraft.value().kind == LassoKind::Polygonal &&
        polygonDraft.value().PointCount() == 1 && polygonDraft.value().FirstPoint().has_value() &&
        SamePoint(polygonDraft.value().FirstPoint().value(), {10.25, 20.5}) && !polygonDraft.value().anchor &&
        !polygonDraft.value().cursor && polygon.completedOnOwner && polygon.workerSawOriginalReservation &&
        polygon.bindResult == S_OK && polygon.boundToOriginal && polygon.boundBytes == kAdmissionBytes &&
        polygon.unboundApplyRefused && !session.history.IsModified() && session.history.UndoCount() == 0;
    Expect(polygonReady && polygonCurrent && polygonApplied && polygonShape,
           "opening.worker-reservation-binds-original-ticket-and-publishes-bounded-polygon-without-vector");
    const bool reapplyRefused = polygonApplied && !session.ApplyPreparedLassoOpeningIfCurrent(polygon.prepared.value());
    Expect(reapplyRefused && session.PendingLasso() && session.PendingLasso().value().PointCount() == 1,
           "opening.single-use-prepared-result-refuses-reapply");
    ReleaseOpening(session, polygon);

    auto marqueeSessionFixture = std::make_unique<DocumentSession>();
    auto& marqueeSession = *marqueeSessionFixture;
    marqueeSession.CreateDocument(64, 64, true);
    marqueeSession.history.Reset();
    marqueeSession.marqueeKind = LassoKind::Rectangle;
    OpeningState marquee;
    const bool marqueeReady =
        CaptureAndPrepare(marqueeSession, scheduler, marquee, {10.4, 20.6}, SelectionMode::Subtract, true);
    const bool marqueeCurrent =
        marqueeReady && marquee.prepared && marqueeSession.IsPreparedLassoOpeningCurrent(marquee.prepared.value());
    const bool marqueeApplied =
        marqueeCurrent && marqueeSession.ApplyPreparedLassoOpeningIfCurrent(marquee.prepared.value());
    const auto& marqueeDraft = marqueeSession.PendingLasso();
    Expect(marqueeApplied && marquee.unboundApplyRefused && !marqueeSession.history.IsModified() &&
               marqueeSession.history.UndoCount() == 0 && marqueeDraft && marqueeDraft.value().points.empty() &&
               marqueeDraft.value().mode == SelectionMode::Subtract &&
               marqueeDraft.value().kind == LassoKind::Rectangle && marqueeDraft.value().PointCount() == 1 &&
               marqueeDraft.value().FirstPoint().has_value() &&
               SamePoint(marqueeDraft.value().FirstPoint().value(), {10, 21}) && marqueeDraft.value().anchor &&
               SamePoint(marqueeDraft.value().anchor.value(), {10, 21}) && marquee.boundToOriginal,
           "opening.rounds-marquee-first-point-and-anchor-preserves-mode-kind-and-empty-vector");
    ReleaseOpening(marqueeSession, marquee);
}

void CheckLivePolygonSnapshots(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(256, 256, true);
    session.history.Reset();
    session.lassoKind = LassoKind::Polygonal;
    OpeningState opening;
    bool opened = CaptureAndPrepare(session, scheduler, opening, {0, 0}, SelectionMode::New, false, kCapacityBlocks);
    opened = opened && session.ApplyPreparedLassoOpeningIfCurrent(opening.prepared.value());
    const bool duplicateAccepted = opened && session.ExtendBoundedLasso({0.1, 0.1});
    const bool duplicateSkipped = duplicateAccepted && session.PendingLasso().value().PointCount() == 1;
    bool allAccepted = duplicateSkipped;
    for (std::uint32_t index = 1; index < 65 && allAccepted; ++index)
        allAccepted = session.ExtendBoundedLasso({static_cast<double>(index), 0});
    auto prefixTerminal = session.CaptureBoundedLassoDisplayForWork();
    std::vector<Vec2> prefix;
    const bool prefixCopied = prefixTerminal && prefixTerminal.value().display.points.CopyToForWork(prefix);
    for (std::uint32_t index = 65; index < 129 && allAccepted; ++index)
        allAccepted = session.ExtendBoundedLasso({static_cast<double>(index), 0});
    auto fullTerminal = session.CaptureBoundedLassoDisplayForWork();
    std::vector<Vec2> full;
    const bool fullCopied = fullTerminal && fullTerminal.value().display.points.CopyToForWork(full);
    bool prefixMatches =
        prefix.size() == 65 && full.size() == 129 && prefixTerminal && fullTerminal &&
        prefixTerminal.value().display.points.Count() == 65 && fullTerminal.value().display.points.Count() == 129 &&
        prefixTerminal.value().display.points.ArenaIdentity() == fullTerminal.value().display.points.ArenaIdentity();
    for (std::size_t index = 0; index < prefix.size() && prefixMatches; ++index)
        prefixMatches = SamePoint(prefix[index], full[index]);
    const bool cursorMoved = session.MoveBoundedLassoCursor(Vec2{130, 4});
    auto cursorTerminal = session.CaptureBoundedLassoDisplayForWork();
    Expect(opened && duplicateSkipped && allAccepted && prefixCopied && fullCopied && prefixMatches && cursorMoved &&
               cursorTerminal && cursorTerminal.value().display.cursor &&
               SamePoint(cursorTerminal.value().display.cursor.value(), {130, 4}) &&
               cursorTerminal.value().display.points.Count() == 129 && session.PendingLasso().value().points.empty(),
           "polygon.quarter-pixel-skip-65-129-prefix-snapshot-reuse-and-cursor-remain-bounded");
    ReleaseOpening(session, opening);
    const bool retainedRead = prefixTerminal && prefixTerminal.value().display.points.CopyToForWork(prefix) &&
                              prefix.size() == 65 && SamePoint(prefix.front(), {0, 0}) &&
                              SamePoint(prefix.back(), {64, 0});
    Expect(retainedRead, "polygon.retained-snapshot-remains-readable-through-work-copy-after-cancel");
    cursorTerminal.reset();
    fullTerminal.reset();
    prefixTerminal.reset();
    static_cast<void>(SettleJobs(scheduler, kGateTimeoutMs));
}

void CheckMarqueeMutationAndRetirement(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(128, 128, true);
    session.history.Reset();
    session.marqueeKind = LassoKind::Rectangle;
    OpeningState opening;
    bool opened = CaptureAndPrepare(session, scheduler, opening, {20.4, 30.6}, SelectionMode::Add, true);
    opened = opened && session.ApplyPreparedLassoOpeningIfCurrent(opening.prepared.value());
    session.DragMarquee({25, 37}, true, true);
    auto display = session.CaptureBoundedLassoDisplayForWork();
    std::vector<Vec2> points;
    const bool copied = display && display.value().display.points.CopyToForWork(points);
    const std::array<Vec2, 4> expected{{{14, 25}, {26, 25}, {26, 37}, {14, 37}}};
    bool geometryMatches = copied && points.size() == expected.size();
    for (std::size_t index = 0; index < expected.size() && geometryMatches; ++index)
        geometryMatches = SamePoint(points[index], expected[index]);
    const bool metadataMatches =
        display && display.value().display.anchor && SamePoint(display.value().display.anchor.value(), {20, 31}) &&
        display.value().display.mode == SelectionMode::Add && display.value().display.kind == LassoKind::Rectangle;
    Expect(opened && geometryMatches && metadataMatches,
           "marquee.shift-square-center-drag-uses-rounded-anchor-and-independent-geometry");
    display.reset();
    ReleaseOpening(session, opening);

    auto popFixture = std::make_unique<DocumentSession>();
    auto& pop = *popFixture;
    pop.CreateDocument(64, 64, true);
    pop.history.Reset();
    pop.marqueeKind = LassoKind::Ellipse;
    OpeningState popOpening;
    bool popOpened = CaptureAndPrepare(pop, scheduler, popOpening, {2.4, 3.6}, SelectionMode::New, true);
    popOpened = popOpened && pop.ApplyPreparedLassoOpeningIfCurrent(popOpening.prepared.value());
    const bool removedAnchor = popOpened && pop.RemoveLastBoundedLassoPoint();
    Expect(removedAnchor && !pop.PendingLasso(), "marquee.pop-last-anchor-closes-empty-draft");
    ReleaseOpening(pop, popOpening);

    auto cancelledFixture = std::make_unique<DocumentSession>();
    auto& cancelled = *cancelledFixture;
    cancelled.CreateDocument(64, 64, true);
    cancelled.history.Reset();
    OpeningState cancelOpening;
    bool cancelOpened = CaptureAndPrepare(cancelled, scheduler, cancelOpening, {2, 3}, SelectionMode::New, true);
    cancelOpened = cancelOpened && cancelled.ApplyPreparedLassoOpeningIfCurrent(cancelOpening.prepared.value());
    auto retained = cancelled.CaptureBoundedLassoDisplayForWork();
    cancelled.CancelLasso();
    std::vector<Vec2> retainedPoints;
    const bool cancelSnapshot = retained && retained.value().display.points.CopyToForWork(retainedPoints) &&
                                retainedPoints.size() == 1 && !cancelled.PendingLasso();
    Expect(cancelOpened && cancelSnapshot, "marquee.cancel-closes-owner-draft-but-retained-snapshot-survives");
    retained.reset();
    ReleaseOpening(cancelled, cancelOpening);
}

void CheckExhaustionPreservesAndLegacyCancels(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(128, 128, true);
    session.history.Reset();
    OpeningState opening;
    bool opened = CaptureAndPrepare(session, scheduler, opening, {0, 0}, SelectionMode::New, false, kOpeningBlocks);
    opened = opened && session.ApplyPreparedLassoOpeningIfCurrent(opening.prepared.value());
    constexpr std::uint32_t kPointLimit = (kOpeningBlocks + 1u) * 64u; // admitted sealed slots plus one inline tail
    bool filled = opened;
    for (std::uint32_t index = 1; index < kPointLimit && filled; ++index)
        filled = session.ExtendBoundedLasso({static_cast<double>(index), 0});
    const auto acceptedPrefix = session.PendingLasso() ? session.PendingLasso().value().PointCount() : 0;
    const bool boolRefused = !session.ExtendBoundedLasso({static_cast<double>(kPointLimit), 0});
    const bool prefixPreserved = session.PendingLasso() &&
                                 session.PendingLasso().value().PointCount() == acceptedPrefix &&
                                 acceptedPrefix == kPointLimit;
    session.ExtendLasso({static_cast<double>(kPointLimit), 0});
    Expect(opened && filled && boolRefused && prefixPreserved && !session.PendingLasso(),
           "capacity.bool-refusal-preserves-accepted-prefix-and-legacy-void-wrapper-cancels");
    ReleaseOpening(session, opening);
}

void CheckStaleInvalidations(JobScheduler& scheduler)
{
    // A positive current fixture is recorded before each owner mutation so stale refusal cannot pass vacuously.
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.source.fixture-current-before-source-mutation");
        session.SelectAll();
        const auto undo = session.history.UndoCount();
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && session.history.UndoCount() == undo && !session.PendingLasso(),
               "stale.source-selection-change-refuses-without-overwriting-new-selection");
        ReleaseOpening(session, state);
    }
    {
        auto sourceFixture = std::make_unique<DocumentSession>();
        auto& source = *sourceFixture;
        source.CreateDocument(64, 64, true);
        source.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(source, scheduler, state, {5, 6}, SelectionMode::Add, false);
        auto otherFixture = std::make_unique<DocumentSession>();
        auto& other = *otherFixture;
        other.CreateDocument(64, 64, true);
        other.history.Reset();
        const bool currentBefore =
            prepared && state.prepared && source.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.session.fixture-current-before-foreign-apply");
        const bool refused = state.prepared && !other.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !other.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !other.PendingLasso(), "stale.session-foreign-document-session-refuses-input");
        ReleaseOpening(source, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.generation.fixture-current-before-new-document");
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso() && !session.Document()->selection,
               "stale.generation-same-size-new-document-refuses-old-opening");
        ReleaseOpening(session, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        session.lassoKind = LassoKind::Freehand;
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.kind.fixture-current-before-header-kind-change");
        session.lassoKind = LassoKind::Polygonal;
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso(), "stale.kind-header-lasso-kind-change-invalidates-opening");
        ReleaseOpening(session, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.AddBlankLayer();
        session.history.Reset();
        const auto priorLayer = session.Document()->layers[0].id;
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.active.fixture-current-before-active-layer-change");
        session.SelectLayerTarget(priorLayer, false);
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso(), "stale.active-active-layer-change-invalidates-opening");
        ReleaseOpening(session, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.crop.fixture-current-before-crop-change");
        session.SetCropFrame(CropRect{2, 3, 40, 41});
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso(), "stale.crop-frame-change-invalidates-opening");
        ReleaseOpening(session, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        const auto layer = session.ActiveLayerID();
        session.AddLayerMask();
        if (layer)
            session.SelectLayerTarget(layer.value(), false);
        session.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.mask.fixture-current-before-mask-target-change");
        if (layer)
            session.SelectLayerTarget(layer.value(), true);
        const bool refused = layer && state.prepared &&
                             !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso(), "stale.mask-mask-selection-change-invalidates-opening");
        ReleaseOpening(session, state);
    }
    {
        auto sessionFixture = std::make_unique<DocumentSession>();
        auto& session = *sessionFixture;
        session.CreateDocument(64, 64, true);
        session.history.Reset();
        OpeningState state;
        const bool prepared = CaptureAndPrepare(session, scheduler, state, {5, 6}, SelectionMode::New, false);
        const bool currentBefore =
            prepared && state.prepared && session.IsPreparedLassoOpeningCurrent(state.prepared.value());
        Expect(currentBefore, "stale.cancel.fixture-current-before-new-opening-and-cancel");
        session.BeginLasso({1, 1}, SelectionMode::New);
        session.CancelLasso();
        const bool refused = state.prepared && !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                             !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        Expect(refused && !session.PendingLasso(), "stale.cancel-cancelled-newer-opening-invalidates-prepared-result");
        ReleaseOpening(session, state);
    }
}

void CheckOverlapAndMovedInput(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    OpeningState older;
    OpeningState newer;
    const bool olderPrepared = CaptureAndPrepare(session, scheduler, older, {1, 2}, SelectionMode::New, false);
    const bool olderCurrent =
        olderPrepared && older.prepared && session.IsPreparedLassoOpeningCurrent(older.prepared.value());
    Expect(older.captureSucceeded && older.captureReservationHeld && older.captureOnOwner && older.sameTicketResumed &&
               olderCurrent,
           "overlap.fixture-both-same-opening-inputs-captured-before-apply");
    const bool newerPrepared = CaptureAndPrepare(session, scheduler, newer, {3, 4}, SelectionMode::Add, false);
    const bool olderSuperseded = newerPrepared && older.prepared && newer.prepared &&
                                 !session.IsPreparedLassoOpeningCurrent(older.prepared.value());
    const bool newestCurrent =
        newerPrepared && newer.prepared && session.IsPreparedLassoOpeningCurrent(newer.prepared.value());
    Expect(newer.reservedBaseline == kAdmissionBytes, "overlap.fixture-prior-bound-ticket-charge-is-in-owner-baseline");
    Expect(olderSuperseded && newestCurrent && newer.captureSucceeded && newer.captureReservationHeld &&
               newer.captureOnOwner && newer.sameTicketResumed,
           "overlap.newest-selected-capture-supersedes-old-result-before-either-apply");
    const bool newestApplied = newestCurrent && session.ApplyPreparedLassoOpeningIfCurrent(newer.prepared.value());
    const bool olderRefused =
        newestApplied && older.prepared && !session.ApplyPreparedLassoOpeningIfCurrent(older.prepared.value());
    Expect(newestApplied && olderRefused && session.PendingLasso() &&
               session.PendingLasso().value().mode == SelectionMode::Add &&
               session.PendingLasso().value().PointCount() == 1 &&
               SamePoint(session.PendingLasso().value().FirstPoint().value(), {3, 4}),
           "overlap.newest-selected-ticket-applies-and-old-ticket-cannot-resurrect");
    ReleaseOpening(session, older);
    ReleaseOpening(session, newer);

    auto movedSessionFixture = std::make_unique<DocumentSession>();
    auto& movedSession = *movedSessionFixture;
    movedSession.CreateDocument(64, 64, true);
    movedSession.history.Reset();
    OpeningState moved;
    moved.moveBeforeWorker = true;
    const bool movedSettled = SubmitInput(scheduler, movedSession, moved, {7, 8}, SelectionMode::New, false);
    Expect(moved.captureSucceeded && moved.captureReservationHeld && moved.captureOnOwner && moved.sameTicketResumed,
           "moved.fixture-valid-input-captured-before-move");
    Expect(movedSettled && moved.captureAttempted && moved.captureSucceeded &&
               moved.terminal.status != JobStatus::Finished && !moved.prepared && !movedSession.PendingLasso() &&
               moved.boundToOriginal && moved.boundBytes == kAdmissionBytes,
           "moved.moved-from-opening-input-refuses-worker-publication");
    ReleaseOpening(movedSession, moved);
}

void CheckCancelBeforeAcquisition(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    session.lassoKind = LassoKind::Polygonal;
    OpeningState state;
    state.cancelAfterCapture = true;
    const bool settled = SubmitInput(scheduler, session, state, {8, 9}, SelectionMode::Subtract, false);
    const bool refused = settled && state.captureAttempted && state.captureSucceeded && state.prepared &&
                         !session.IsPreparedLassoOpeningCurrent(state.prepared.value()) &&
                         !session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value()) &&
                         !session.PendingLasso() && state.boundToOriginal && state.boundBytes == kAdmissionBytes;
    Expect(state.captureSucceeded && state.captureReservationHeld && state.captureOnOwner && state.sameTicketResumed,
           "cancel-before-acquisition.fixture-captured-before-escape");
    Expect(refused, "cancel-before-acquisition.escape-clears-pending-intent-and-old-job-cannot-resurrect");
    ReleaseOpening(session, state);
}
bool OpenAndCloseBoundedPolygon(DocumentSession& session, JobScheduler& scheduler, OpeningState& opening,
                                LassoFinishTerminal& terminal, bool antialiasedAtClose,
                                bool headerAntialiasedAfterClose = false)
{
    session.lassoKind = LassoKind::Polygonal;
    if (!CaptureAndPrepare(session, scheduler, opening, {10, 10}, SelectionMode::New, false) ||
        !session.ApplyPreparedLassoOpeningIfCurrent(opening.prepared.value()) ||
        !session.ExtendBoundedLasso({50, 10}) || !session.ExtendBoundedLasso({50, 50}) ||
        !session.ExtendBoundedLasso({10, 50}))
        return false;
    session.selectionAntialiased = !antialiasedAtClose;
    auto closed = session.CloseBoundedLassoForWork(antialiasedAtClose);
    if (!closed)
        return false;
    terminal = std::move(closed.value());
    session.selectionAntialiased = headerAntialiasedAfterClose;
    return terminal.display.points.Count() == 4 && terminal.display.key.opening != 0 &&
           terminal.display.key.revision != 0;
}

bool ApplyStrictFinish(DocumentSession& session, LassoFinishInput input, bool expectedAA)
{
    auto prepared = DocumentSession::PrepareLassoFinishPublication(std::move(input), {}, {}, nullptr,
                                                                   SharedPublicationMemory::CreateForWork(1024 * 1024));
    if (!prepared || !session.IsPreparedLassoFinishCurrent(prepared.value()))
        return false;
    DocumentHistory::ResidencyPermit permit;
    auto residency = prepared.value().TryAcquireHistoryResidency(permit);
    if (residency == DocumentHistory::ResidencyValidation::Refresh && prepared.value().RefreshHistoryResidency())
        residency = prepared.value().TryAcquireHistoryResidency(permit);
    const auto* proposal = prepared.value().SelectionForDisplayForWork();
    const bool ready =
        residency == DocumentHistory::ResidencyValidation::Ready && proposal && proposal->antialiased == expectedAA;
    const bool applied = ready && session.ApplyPreparedLassoFinishIfCurrent(prepared.value());
    permit.Reset();
    return applied && session.Document()->selection &&
           session.Document()->selection.value().antialiased == expectedAA && session.history.UndoCount() == 1 &&
           session.history.UndoName() == L"Polygonal Lasso" && session.history.IsModified();
}

void CheckStrictTerminalBridge(JobScheduler& scheduler)
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    OpeningState opening;
    LassoFinishTerminal terminal{};
    const bool closed = OpenAndCloseBoundedPolygon(session, scheduler, opening, terminal, false);
    auto duplicate = terminal;
    auto strict = closed ? session.PrepareClosedBoundedLassoFinishInput(terminal) : std::nullopt;
    const bool firstCapture = strict.has_value();
    const bool duplicateRefused = !session.PrepareClosedBoundedLassoFinishInput(std::move(duplicate));
    session.selectionAntialiased = true; // The terminal must retain the value captured when close completed.
    const bool applied = firstCapture && strict && ApplyStrictFinish(session, std::move(strict.value()), false);
    bool undoRedo = false;
    if (applied)
    {
        session.Undo();
        const bool undo = !session.Document()->selection && !session.history.IsModified() && session.history.CanRedo();
        session.Redo();
        undoRedo = undo && session.Document()->selection && session.history.IsModified();
    }
    Expect(undoRedo, "terminal.strict-finish-undo-redo-restores-selection-and-saved-marker");
    Expect(closed && firstCapture && duplicateRefused && applied,
           "terminal.real-bounded-opening-close-strict-capture-residency-apply-and-aa-at-close");
    ReleaseOpening(session, opening);

    {
        auto aaFixture = std::make_unique<DocumentSession>();
        auto& aa = *aaFixture;
        aa.CreateDocument(64, 64, true);
        aa.history.Reset();
        OpeningState state;
        LassoFinishTerminal eventTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(aa, scheduler, state, eventTerminal, false, true);
        const bool captured = fixture && eventTerminal.display.antialiased == false && aa.selectionAntialiased;
        auto input = captured ? aa.PrepareClosedBoundedLassoFinishInput(std::move(eventTerminal)) : std::nullopt;
        const bool appliedEventAA = input && ApplyStrictFinish(aa, std::move(input.value()), false);
        Expect(fixture && captured && appliedEventAA, "terminal.event-aa-at-close-wins-over-later-header-aa");
        ReleaseOpening(aa, state);
    }

    const auto checkMetadataMismatch = [&](const char* label, auto mutate)
    {
        auto mismatchFixture = std::make_unique<DocumentSession>();
        auto& mismatch = *mismatchFixture;
        mismatch.CreateDocument(64, 64, true);
        mismatch.history.Reset();
        OpeningState state;
        LassoFinishTerminal candidate{};
        const bool fixture = OpenAndCloseBoundedPolygon(mismatch, scheduler, state, candidate, true);
        const auto before = mismatch.history.CaptureState();
        mutate(candidate);
        const bool refused = fixture && !mismatch.PrepareClosedBoundedLassoFinishInput(std::move(candidate));
        const bool unchanged = !mismatch.PendingLasso() && mismatch.Document() && !mismatch.Document()->selection &&
                               mismatch.history.IsCurrent(before) && mismatch.history.UndoCount() == 0;
        Expect(fixture && refused && unchanged, label);
        ReleaseOpening(mismatch, state);
    };
    checkMetadataMismatch("terminal.corrupted-kind-refuses-without-current-state-overwrite",
                          [](LassoFinishTerminal& value) { value.display.kind = LassoKind::Rectangle; });
    checkMetadataMismatch("terminal.corrupted-mode-refuses-without-current-state-overwrite",
                          [](LassoFinishTerminal& value) { value.display.mode = SelectionMode::Subtract; });
    checkMetadataMismatch("terminal.corrupted-aa-refuses-without-current-state-overwrite",
                          [](LassoFinishTerminal& value) { value.display.antialiased = false; });
    checkMetadataMismatch("terminal.corrupted-cursor-refuses-without-current-state-overwrite",
                          [](LassoFinishTerminal& value) { value.display.cursor = Vec2{1, 2}; });
    checkMetadataMismatch("terminal.corrupted-anchor-refuses-without-current-state-overwrite",
                          [](LassoFinishTerminal& value) { value.display.anchor = Vec2{1, 2}; });

    {
        auto olderSnapshotFixture = std::make_unique<DocumentSession>();
        auto& olderSnapshot = *olderSnapshotFixture;
        olderSnapshot.CreateDocument(64, 64, true);
        olderSnapshot.history.Reset();
        OpeningState state;
        LassoFinishTerminal terminalWithOldPrefix{};
        const bool opened = CaptureAndPrepare(olderSnapshot, scheduler, state, {10, 10}, SelectionMode::New, false) &&
                            olderSnapshot.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value()) &&
                            olderSnapshot.ExtendBoundedLasso({50, 10}) && olderSnapshot.ExtendBoundedLasso({50, 50});
        auto beforeLastPoint = opened ? olderSnapshot.CaptureBoundedLassoDisplayForWork() : std::nullopt;
        const bool completed = opened && olderSnapshot.ExtendBoundedLasso({10, 50});
        auto terminalValue = completed ? olderSnapshot.CloseBoundedLassoForWork(true) : std::nullopt;
        if (terminalValue && beforeLastPoint)
            terminalValue->display.points = beforeLastPoint->display.points;
        const auto historyBefore = olderSnapshot.history.CaptureState();
        const bool refused =
            terminalValue && !olderSnapshot.PrepareClosedBoundedLassoFinishInput(std::move(terminalValue.value()));
        const bool unchanged = !olderSnapshot.PendingLasso() && !olderSnapshot.Document()->selection &&
                               olderSnapshot.history.IsCurrent(historyBefore) && olderSnapshot.history.UndoCount() == 0;
        Expect(opened && beforeLastPoint && completed && terminalValue.has_value() && refused && unchanged,
               "terminal.older-same-arena-snapshot-refuses-without-current-state-overwrite");
        ReleaseOpening(olderSnapshot, state);
    }

    {
        auto firstArenaFixture = std::make_unique<DocumentSession>();
        auto& firstArena = *firstArenaFixture;
        firstArena.CreateDocument(64, 64, true);
        firstArena.history.Reset();
        OpeningState firstState;
        LassoFinishTerminal firstTerminal{};
        const bool firstFixture = OpenAndCloseBoundedPolygon(firstArena, scheduler, firstState, firstTerminal, true);
        auto otherArenaFixture = std::make_unique<DocumentSession>();
        auto& otherArena = *otherArenaFixture;
        otherArena.CreateDocument(64, 64, true);
        otherArena.history.Reset();
        OpeningState otherState;
        LassoFinishTerminal otherTerminal{};
        const bool otherFixture = OpenAndCloseBoundedPolygon(otherArena, scheduler, otherState, otherTerminal, true);
        if (firstFixture && otherFixture)
            firstTerminal.display.points = otherTerminal.display.points;
        const auto before = firstArena.history.CaptureState();
        const bool refused =
            firstFixture && otherFixture && !firstArena.PrepareClosedBoundedLassoFinishInput(std::move(firstTerminal));
        const bool unchanged = !firstArena.PendingLasso() && !firstArena.Document()->selection &&
                               firstArena.history.IsCurrent(before) && firstArena.history.UndoCount() == 0;
        Expect(firstFixture && otherFixture && refused && unchanged,
               "terminal.different-arena-snapshot-refuses-without-current-state-overwrite");
        ReleaseOpening(firstArena, firstState);
        ReleaseOpening(otherArena, otherState);
    }

    {
        auto cancelledFixture = std::make_unique<DocumentSession>();
        auto& cancelled = *cancelledFixture;
        cancelled.CreateDocument(64, 64, true);
        cancelled.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(cancelled, scheduler, state, closedTerminal, true);
        cancelled.CancelLasso(); // Cancellation after close invalidates the selected stamp before capture.
        const bool refused = fixture && !cancelled.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && refused, "terminal.cancel-before-strict-capture-clears-closed-stamp");
        ReleaseOpening(cancelled, state);
    }
    {
        auto staleFixture = std::make_unique<DocumentSession>();
        auto& stale = *staleFixture;
        stale.CreateDocument(64, 64, true);
        stale.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(stale, scheduler, state, closedTerminal, true);
        stale.SelectAll();
        const bool refused = fixture && !stale.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && refused, "terminal.source-mutation-after-close-refuses-strict-capture");
        ReleaseOpening(stale, state);
    }
    {
        auto corruptedFixture = std::make_unique<DocumentSession>();
        auto& corrupted = *corruptedFixture;
        corrupted.CreateDocument(64, 64, true);
        corrupted.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(corrupted, scheduler, state, closedTerminal, true);
        ++closedTerminal.display.key.revision;
        const bool refused = fixture && !corrupted.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && refused, "terminal.corrupted-closed-key-refuses-strict-capture");
        ReleaseOpening(corrupted, state);
    }
    {
        auto activeFixture = std::make_unique<DocumentSession>();
        auto& active = *activeFixture;
        active.CreateDocument(64, 64, true);
        const auto previousLayer = active.ActiveLayerID();
        active.AddBlankLayer();
        active.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(active, scheduler, state, closedTerminal, true);
        if (previousLayer)
            active.SelectLayerTarget(previousLayer.value(), false);
        const bool refused =
            fixture && previousLayer && !active.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && previousLayer && refused, "terminal.active-layer-change-after-close-refuses-strict-capture");
        ReleaseOpening(active, state);
    }
    {
        auto cropFixture = std::make_unique<DocumentSession>();
        auto& crop = *cropFixture;
        crop.CreateDocument(64, 64, true);
        crop.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(crop, scheduler, state, closedTerminal, true);
        crop.SetCropFrame(CropRect{2, 3, 40, 41});
        const bool refused = fixture && !crop.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && refused, "terminal.crop-change-after-close-refuses-strict-capture");
        ReleaseOpening(crop, state);
    }
    {
        auto maskFixture = std::make_unique<DocumentSession>();
        auto& mask = *maskFixture;
        mask.CreateDocument(64, 64, true);
        const auto imageLayer = mask.ActiveLayerID();
        mask.AddLayerMask();
        if (imageLayer)
            mask.SelectLayerTarget(imageLayer.value(), false);
        mask.history.Reset();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(mask, scheduler, state, closedTerminal, true);
        if (imageLayer)
            mask.SelectLayerTarget(imageLayer.value(), true);
        const bool refused =
            fixture && imageLayer && !mask.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && imageLayer && refused, "terminal.mask-target-change-after-close-refuses-strict-capture");
        ReleaseOpening(mask, state);
    }
    {
        auto historyFixture = std::make_unique<DocumentSession>();
        auto& history = *historyFixture;
        history.CreateDocument(64, 64, true);
        history.AddBlankLayer();
        OpeningState state;
        LassoFinishTerminal closedTerminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(history, scheduler, state, closedTerminal, true);
        history.Undo();
        const bool refused = fixture && !history.PrepareClosedBoundedLassoFinishInput(std::move(closedTerminal));
        Expect(fixture && refused, "terminal.history-change-after-close-refuses-strict-capture");
        ReleaseOpening(history, state);
    }
}
void CheckOpeningCounterTestSeam(JobScheduler& scheduler)
{
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    {
        auto openingMaxFixture = std::make_unique<DocumentSession>();
        auto& openingMax = *openingMaxFixture;
        openingMax.CreateDocument(64, 64, true);
        openingMax.history.Reset();
        const auto dimensions = std::pair{openingMax.Document()->width, openingMax.Document()->height};
        const bool seeded = openingMax.SetLassoOpeningCountersForTest(maximum, 0);
        const auto refused = openingMax.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, false);
        Expect(seeded && !refused && !openingMax.PendingLasso() && !openingMax.history.IsModified() &&
                   openingMax.history.UndoCount() == 0 && openingMax.Document() &&
                   std::pair{openingMax.Document()->width, openingMax.Document()->height} == dimensions,
               "counter.opening-uint64-max-refuses-capture-without-document-history-or-draft");
    }
    {
        auto intentMaxFixture = std::make_unique<DocumentSession>();
        auto& intentMax = *intentMaxFixture;
        intentMax.CreateDocument(64, 64, true);
        intentMax.history.Reset();
        const auto dimensions = std::pair{intentMax.Document()->width, intentMax.Document()->height};
        const bool seeded = intentMax.SetLassoOpeningCountersForTest(0, maximum);
        const auto refused = intentMax.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, false);
        Expect(seeded && !refused && !intentMax.PendingLasso() && !intentMax.history.IsModified() &&
                   intentMax.history.UndoCount() == 0 && intentMax.Document() &&
                   std::pair{intentMax.Document()->width, intentMax.Document()->height} == dimensions,
               "counter.intent-uint64-max-refuses-capture-without-document-history-or-draft");
    }
    {
        auto liveDraftFixture = std::make_unique<DocumentSession>();
        auto& liveDraft = *liveDraftFixture;
        liveDraft.CreateDocument(64, 64, true);
        liveDraft.history.Reset();
        liveDraft.BeginLasso({2, 3}, SelectionMode::New);
        const auto priorCount = liveDraft.PendingLasso() ? liveDraft.PendingLasso().value().PointCount() : 0;
        const bool refused = !liveDraft.SetLassoOpeningCountersForTest(9, 9);
        Expect(refused && liveDraft.PendingLasso() && liveDraft.PendingLasso().value().PointCount() == priorCount,
               "counter.test-seam-refuses-live-draft-without-changing-it");
        liveDraft.CancelLasso();
    }
    {
        auto pendingOpenFixture = std::make_unique<DocumentSession>();
        auto& pendingOpen = *pendingOpenFixture;
        pendingOpen.CreateDocument(64, 64, true);
        pendingOpen.history.Reset();
        auto pending = pendingOpen.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, false);
        const bool refused = pending && !pendingOpen.SetLassoOpeningCountersForTest(9, 9);
        Expect(pending && refused && !pendingOpen.PendingLasso(), "counter.test-seam-refuses-pending-opening-intent");
        pending.reset();
        pendingOpen.CancelLasso();
    }
    {
        auto closedFixture = std::make_unique<DocumentSession>();
        auto& closed = *closedFixture;
        closed.CreateDocument(64, 64, true);
        closed.history.Reset();
        OpeningState state;
        LassoFinishTerminal terminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(closed, scheduler, state, terminal, true);
        const bool refused = fixture && !closed.SetLassoOpeningCountersForTest(9, 9);
        Expect(fixture && refused, "counter.test-seam-refuses-closed-terminal-stamp");
        ReleaseOpening(closed, state);
    }
    {
        auto pendingFinishFixture = std::make_unique<DocumentSession>();
        auto& pendingFinish = *pendingFinishFixture;
        pendingFinish.CreateDocument(64, 64, true);
        pendingFinish.history.Reset();
        OpeningState state;
        LassoFinishTerminal terminal{};
        const bool fixture = OpenAndCloseBoundedPolygon(pendingFinish, scheduler, state, terminal, true);
        auto input = fixture ? pendingFinish.PrepareClosedBoundedLassoFinishInput(std::move(terminal)) : std::nullopt;
        const bool refused = input && !pendingFinish.SetLassoOpeningCountersForTest(9, 9);
        Expect(fixture && input && refused, "counter.test-seam-refuses-pending-finish-intent");
        input.reset();
        pendingFinish.CancelLasso();
        ReleaseOpening(pendingFinish, state);
    }
    {
        auto decreasingFixture = std::make_unique<DocumentSession>();
        auto& decreasing = *decreasingFixture;
        decreasing.CreateDocument(64, 64, true);
        decreasing.history.Reset();
        const auto maximumIntent = (std::numeric_limits<std::uint64_t>::max)();
        const bool seeded = decreasing.SetLassoOpeningCountersForTest(5, maximumIntent - 1);
        const bool openingDecreaseRefused = !decreasing.SetLassoOpeningCountersForTest(4, maximumIntent - 1);
        const bool intentDecreaseRefused = !decreasing.SetLassoOpeningCountersForTest(5, maximumIntent - 2);
        OpeningState state;
        const bool prepared = CaptureAndPrepare(decreasing, scheduler, state, {2, 3}, SelectionMode::New, false);
        const bool applied = prepared && decreasing.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
        auto display = decreasing.CaptureBoundedLassoDisplayForWork();
        const bool nextOpeningPreserved = display && display.value().display.key.opening == 6;
        display.reset();
        ReleaseOpening(decreasing, state);
        const auto saturatedIntentCapture = decreasing.PrepareLassoOpeningInput({4, 5}, SelectionMode::New, false);
        Expect(seeded && openingDecreaseRefused && intentDecreaseRefused && prepared && applied &&
                   nextOpeningPreserved && !saturatedIntentCapture,
               "counter.test-seam-refuses-decreases-preserving-opening-and-intent-through-uint64-max");
    }
}
void CheckCaptureOnlyWhenSettled()
{
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    auto valid = session.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, false);
    Expect(valid.has_value(), "capture.settled-session-produces-positive-fixture");
    auto invalidMode = session.PrepareLassoOpeningInput({2, 3}, static_cast<SelectionMode>(255), false);
    Expect(!invalidMode, "capture.invalid-selection-mode-is-refused");
    session.lassoKind = static_cast<LassoKind>(255);
    auto invalidLassoKind = session.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, false);
    Expect(!invalidLassoKind, "capture.invalid-lasso-kind-is-refused");
    session.marqueeKind = static_cast<LassoKind>(255);
    auto invalidMarqueeKind = session.PrepareLassoOpeningInput({2, 3}, SelectionMode::New, true);
    Expect(!invalidMarqueeKind, "capture.invalid-marquee-kind-is-refused");
    session.lassoKind = LassoKind::Freehand;
    session.marqueeKind = LassoKind::Rectangle;
    session.BeginLasso({1, 1}, SelectionMode::New);
    auto overlapping = session.PrepareLassoOpeningInput({4, 5}, SelectionMode::Add, false);
    Expect(!overlapping, "capture.active-legacy-draft-refuses-opening-input");
    session.CancelLasso();
}
} // namespace

int RunLassoSessionBridgeTests()
{
    gChecks = 0;
    gFailures = 0;
    CheckMemoryEnvelope();
    CheckCaptureOnlyWhenSettled();
    JobScheduler scheduler;
    const bool schedulerStarted = StartScheduler(scheduler);
    Expect(schedulerStarted, "scheduler.initializes-one-worker-for-original-ticket-tests");
    Expect(schedulerStarted, "admission.original-ticket-reservation-and-deferred-release-setup");
    if (schedulerStarted)
    {
        CheckCaptureAndSuccessfulApply(scheduler);
        CheckLivePolygonSnapshots(scheduler);
        CheckMarqueeMutationAndRetirement(scheduler);
        CheckExhaustionPreservesAndLegacyCancels(scheduler);
        CheckStaleInvalidations(scheduler);
        CheckCancelBeforeAcquisition(scheduler);
        CheckOverlapAndMovedInput(scheduler);
        CheckStrictTerminalBridge(scheduler);
        CheckOpeningCounterTestSeam(scheduler);
        const bool settled = SettleJobs(scheduler, kGateTimeoutMs) == SettleResult::Settled;
        Expect(settled && scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0,
               "scheduler.all-original-ticket-reservations-and-records-drain");
        scheduler.Close();
    }
    else
    {
        Expect(false, "cancel-before-acquisition.escape-clears-pending-intent-and-old-job-cannot-resurrect");
        Expect(false, "cancel-before-acquisition.fixture-captured-before-escape");
        Expect(false, "capacity.bool-refusal-preserves-accepted-prefix-and-legacy-void-wrapper-cancels");
        Expect(false, "counter.intent-uint64-max-refuses-capture-without-document-history-or-draft");
        Expect(false, "counter.opening-uint64-max-refuses-capture-without-document-history-or-draft");
        Expect(false, "counter.test-seam-refuses-closed-terminal-stamp");
        Expect(false, "counter.test-seam-refuses-decreases-preserving-opening-and-intent-through-uint64-max");
        Expect(false, "counter.test-seam-refuses-live-draft-without-changing-it");
        Expect(false, "counter.test-seam-refuses-pending-finish-intent");
        Expect(false, "counter.test-seam-refuses-pending-opening-intent");
        Expect(false, "marquee.cancel-closes-owner-draft-but-retained-snapshot-survives");
        Expect(false, "marquee.pop-last-anchor-closes-empty-draft");
        Expect(false, "marquee.shift-square-center-drag-uses-rounded-anchor-and-independent-geometry");
        Expect(false, "moved.fixture-valid-input-captured-before-move");
        Expect(false, "moved.moved-from-opening-input-refuses-worker-publication");
        Expect(false, "opening.rounds-marquee-first-point-and-anchor-preserves-mode-kind-and-empty-vector");
        Expect(false, "opening.single-use-prepared-result-refuses-reapply");
        Expect(false, "opening.worker-reservation-binds-original-ticket-and-publishes-bounded-polygon-without-vector");
        Expect(false, "overlap.fixture-both-same-opening-inputs-captured-before-apply");
        Expect(false, "overlap.fixture-prior-bound-ticket-charge-is-in-owner-baseline");
        Expect(false, "overlap.newest-selected-capture-supersedes-old-result-before-either-apply");
        Expect(false, "overlap.newest-selected-ticket-applies-and-old-ticket-cannot-resurrect");
        Expect(false, "polygon.quarter-pixel-skip-65-129-prefix-snapshot-reuse-and-cursor-remain-bounded");
        Expect(false, "polygon.retained-snapshot-remains-readable-through-work-copy-after-cancel");
        Expect(false, "scheduler.all-original-ticket-reservations-and-records-drain");
        Expect(false, "stale.active-active-layer-change-invalidates-opening");
        Expect(false, "stale.active.fixture-current-before-active-layer-change");
        Expect(false, "stale.cancel-cancelled-newer-opening-invalidates-prepared-result");
        Expect(false, "stale.cancel.fixture-current-before-new-opening-and-cancel");
        Expect(false, "stale.crop-frame-change-invalidates-opening");
        Expect(false, "stale.crop.fixture-current-before-crop-change");
        Expect(false, "stale.generation-same-size-new-document-refuses-old-opening");
        Expect(false, "stale.generation.fixture-current-before-new-document");
        Expect(false, "stale.kind-header-lasso-kind-change-invalidates-opening");
        Expect(false, "stale.kind.fixture-current-before-header-kind-change");
        Expect(false, "stale.mask-mask-selection-change-invalidates-opening");
        Expect(false, "stale.mask.fixture-current-before-mask-target-change");
        Expect(false, "stale.session-foreign-document-session-refuses-input");
        Expect(false, "stale.session.fixture-current-before-foreign-apply");
        Expect(false, "stale.source-selection-change-refuses-without-overwriting-new-selection");
        Expect(false, "stale.source.fixture-current-before-source-mutation");
        Expect(false, "terminal.active-layer-change-after-close-refuses-strict-capture");
        Expect(false, "terminal.cancel-before-strict-capture-clears-closed-stamp");
        Expect(false, "terminal.corrupted-closed-key-refuses-strict-capture");
        Expect(false, "terminal.corrupted-kind-refuses-without-current-state-overwrite");
        Expect(false, "terminal.corrupted-mode-refuses-without-current-state-overwrite");
        Expect(false, "terminal.corrupted-aa-refuses-without-current-state-overwrite");
        Expect(false, "terminal.corrupted-cursor-refuses-without-current-state-overwrite");
        Expect(false, "terminal.corrupted-anchor-refuses-without-current-state-overwrite");
        Expect(false, "terminal.crop-change-after-close-refuses-strict-capture");
        Expect(false, "terminal.different-arena-snapshot-refuses-without-current-state-overwrite");
        Expect(false, "terminal.event-aa-at-close-wins-over-later-header-aa");
        Expect(false, "terminal.history-change-after-close-refuses-strict-capture");
        Expect(false, "terminal.mask-target-change-after-close-refuses-strict-capture");
        Expect(false, "terminal.older-same-arena-snapshot-refuses-without-current-state-overwrite");
        Expect(false, "terminal.real-bounded-opening-close-strict-capture-residency-apply-and-aa-at-close");
        Expect(false, "terminal.source-mutation-after-close-refuses-strict-capture");
        Expect(false, "terminal.strict-finish-undo-redo-restores-selection-and-saved-marker");
    }
    std::printf("%s lasso-session-bridge.summary checks=%u failures=%u\n", gFailures ? "FAIL" : "PASS", gChecks,
                gFailures);
    return static_cast<int>(gFailures);
}

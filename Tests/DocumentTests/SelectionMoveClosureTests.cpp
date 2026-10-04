#include "SelectionMoveClosureTests.h"

#include "../Support/DocumentFixture.h"

#include "SelectionMoveWork.h"

#include <array>
#include <cstdio>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s selection-move-closure.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSelection Rect(double x)
{
    return DocumentSelection{false, SelectionLoops{{Vec2{x, 0}, Vec2{x + 1, 0}, Vec2{x + 1, 1}, Vec2{x, 1}}}};
}

bool IsRect(const std::optional<CanvasDocument>& document, double x)
{
    if (!document || !document->selection || document->selection->antialiased ||
        document->selection->loops.Get().size() != 1)
        return false;
    const SelectionLoop& loop = document->selection->loops.Get().front();
    return loop.size() == 4 && loop[0] == Vec2{x, 0} && loop[1] == Vec2{x + 1, 0} && loop[2] == Vec2{x + 1, 1} &&
           loop[3] == Vec2{x, 1};
}

struct Fixture final
{
    DocumentSession session;
    Guid movingLayer;
    Guid otherLayer;
    bool ready = false;

    Fixture()
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        document.selection = Rect(1);
        constexpr std::array<std::uint8_t, 16> pixels{11, 7,  31, 255, 21, 17, 41,  255,
                                                      51, 47, 71, 255, 81, 77, 101, 255};
        constexpr std::array<std::uint8_t, 4> masks{0, 128, 255, 255};
        const auto image = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        const auto mask = Raster::Make(4, 1, RasterFormat::Gray8, masks.data(), masks.size());
        auto first = ImageLayer::Blank(L"Literal selection-move source", Vec2{4, 1});
        movingLayer = first.id;
        first.asset = RasterAsset{image, {}, first.name};
        first.mask = LayerMask{RasterAsset{mask, mask, L"Literal selection-move mask"}, true, std::nullopt, true};
        document.layers.push_back(std::move(first));
        auto other = ImageLayer::Blank(L"Other selection-move layer", Vec2{4, 1});
        otherLayer = other.id;
        document.layers.push_back(std::move(other));
        session.ReplaceDocument(std::move(document).Seal(), movingLayer);

        // Literal current selection x=1..2, with one redo entry available for closure preservation checks.
        session.SetSelection(Rect(0), L"Seed redo selection");
        session.Undo();
        ready = session.Document().has_value() && session.ActiveLayerID() == movingLayer &&
                IsRect(session.Document(), 1) && session.history.CanRedo();
    }
};

struct ObservedState final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    std::optional<CropRect> crop;
    bool maskSelected = false;
    bool moving = false;
    std::uint64_t generation = 0;
    Guid revision;
    bool modified = false;
    DocumentHistory::StateView history;
    std::wstring undoName;
    std::wstring redoName;
};

ObservedState Capture(const DocumentSession& session)
{
    return ObservedState{session.Document(),
                         session.ActiveLayerID(),
                         session.CropFrame(),
                         session.IsMaskSelected(),
                         session.IsMovingSelection(),
                         session.Generation(),
                         session.history.CurrentRevision(),
                         session.history.IsModified(),
                         session.history.CaptureState(),
                         session.history.UndoName(),
                         session.history.RedoName()};
}

bool Matches(const DocumentSession& session, const ObservedState& before)
{
    const auto current = session.history.CaptureState();
    return session.Document() == before.document && session.ActiveLayerID() == before.active &&
           session.CropFrame() == before.crop && session.IsMaskSelected() == before.maskSelected &&
           session.IsMovingSelection() == before.moving && session.Generation() == before.generation &&
           session.history.CurrentRevision() == before.revision && session.history.IsModified() == before.modified &&
           session.history.IsCurrent(before.history) && current.Depth() == before.history.Depth() &&
           current.PastCount() == before.history.PastCount() && current.FutureCount() == before.history.FutureCount() &&
           session.history.UndoName() == before.undoName && session.history.RedoName() == before.redoName;
}

std::optional<PreparedSelectionMoveClosure> Prepare(Fixture& fixture, bool cancel,
                                                    DocumentHistory::PrepareObserver observer = {},
                                                    CancelToken cancellation = {})
{
    auto input = fixture.session.PrepareSelectionMoveClosureInput(cancel);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareSelectionMoveClosurePublication(std::move(input.value()), cancellation, observer);
}

struct Checkpoint final
{
    DocumentHistory::PrepareStage target{};
    bool throwAllocation = false;
    CancelSource cancellation;
    int hits = 0;

    static void Visit(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& self = *static_cast<Checkpoint*>(context);
        if (stage != self.target)
            return;
        ++self.hits;
        if (self.throwAllocation)
            throw std::bad_alloc{};
        (void)self.cancellation.Cancel();
    }
};

void EndCommitsMovedRectangle()
{
    Fixture fixture;
    Expect(fixture.ready, "end.fixture-with-redo");
    if (!fixture.ready)
        return;

    const int undoBefore = fixture.session.history.UndoCount();
    const auto revisionBefore = fixture.session.history.CurrentRevision();
    Expect(fixture.session.BeginSelectionMove(), "end.move-begins");
    fixture.session.MoveSelection(1, 0);
    Expect(IsRect(fixture.session.Document(), 2), "end.live-selection-is-literal-x2-through-x3");

    auto candidate = Prepare(fixture, false);
    Expect(candidate.has_value(), "end.prepared");
    if (!candidate)
        return;
    const std::uint64_t generationBeforeApply = fixture.session.Generation();
    Expect(fixture.session.IsPreparedSelectionMoveClosureCurrent(candidate.value()), "end.current-before-apply");
    Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()), "end.applied");
    Expect(fixture.session.Generation() == generationBeforeApply + 1, "end.acknowledgement-adds-one-generation");
    Expect(!fixture.session.IsMovingSelection() && IsRect(fixture.session.Document(), 2),
           "end.keeps-moved-selection-and-closes-move");
    Expect(fixture.session.history.UndoCount() == undoBefore + 1 && !fixture.session.history.CanRedo() &&
               fixture.session.history.UndoName() == L"Move Selection" &&
               fixture.session.history.CurrentRevision() != revisionBefore,
           "end.one-named-entry-clears-redo-and-advances-revision");
    Expect(fixture.session.history.IsModified(), "end.committed-revision-differs-from-saved-marker");

    fixture.session.Undo();
    Expect(IsRect(fixture.session.Document(), 1) && fixture.session.history.CanRedo(),
           "end.undo-restores-origin-selection");
    fixture.session.Redo();
    Expect(IsRect(fixture.session.Document(), 2), "end.redo-restores-moved-selection");
}

void CancelRestoresOriginAndPreservesRedo()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "cancel.fixture-and-begin");
    if (!fixture.ready || !fixture.session.IsMovingSelection())
        return;
    fixture.session.MoveSelection(1, 0);
    const int undoBefore = fixture.session.history.UndoCount();
    const auto revisionBefore = fixture.session.history.CurrentRevision();
    const bool modifiedBefore = fixture.session.history.IsModified();
    const std::uint64_t generationBeforeApply = fixture.session.Generation();
    const auto tokenBefore = fixture.session.CaptureJobToken();

    auto candidate = Prepare(fixture, true);
    Expect(candidate.has_value(), "cancel.prepared");
    if (!candidate)
        return;
    Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()), "cancel.applied");
    Expect(fixture.session.Generation() == generationBeforeApply + 2,
           "cancel-restore-and-acknowledgement-add-two-generations");
    Expect(!fixture.session.IsMovingSelection() && IsRect(fixture.session.Document(), 1),
           "cancel.restores-origin-and-closes-move");
    Expect(fixture.session.history.UndoCount() == undoBefore && fixture.session.history.CanRedo() &&
               fixture.session.history.RedoName() == L"Seed redo selection" &&
               fixture.session.history.CurrentRevision() == revisionBefore,
           "cancel.no-entry-and-redo-revision-preserved");
    Expect(fixture.session.history.IsModified() == modifiedBefore, "cancel.saved-marker-is-not-rewritten");
    Expect(tokenBefore && !fixture.session.CanApplyJobResult(tokenBefore.value(), 0),
           "cancel.expires-pre-acknowledgement-whole-document-token");
}

void NoOpAndNestedNamedTransaction()
{
    {
        Fixture fixture;
        Expect(fixture.ready && fixture.session.BeginSelectionMove(), "noop.fixture-and-begin");
        if (!fixture.ready || !fixture.session.IsMovingSelection())
            return;
        const auto documentBefore = fixture.session.Document();
        const auto revisionBefore = fixture.session.history.CurrentRevision();
        const auto oldToken = fixture.session.CaptureJobToken();
        auto candidate = Prepare(fixture, false);
        Expect(candidate && fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()),
               "noop.prepared-and-applied");
        Expect(fixture.session.Document() == documentBefore &&
                   fixture.session.history.CurrentRevision() == revisionBefore && fixture.session.history.CanRedo() &&
                   fixture.session.history.UndoName().empty() && fixture.session.history.UndoCount() == 0,
               "noop.document-revision-and-redo-preserved");
        Expect(oldToken && !fixture.session.CanApplyJobResult(oldToken.value(), 0),
               "noop-acknowledgement-expires-old-token");
    }
    {
        Fixture fixture;
        Expect(fixture.ready, "nested.fixture");
        if (!fixture.ready)
            return;
        fixture.session.BeginEdit(L"Outer named transaction");
        Expect(fixture.session.BeginSelectionMove(), "nested.move-begins");
        fixture.session.MoveSelection(1, 0);
        auto candidate = Prepare(fixture, false);
        Expect(candidate && fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()),
               "nested.inner-closure-published");
        Expect(fixture.session.history.CaptureState().Depth() == 1 && fixture.session.history.UndoCount() == 0 &&
                   fixture.session.history.UndoName().empty() && IsRect(fixture.session.Document(), 2),
               "nested.inner-close-keeps-outer-transaction-open");
        fixture.session.EndEdit();
        Expect(fixture.session.history.CaptureState().Depth() == 0 && fixture.session.history.UndoCount() == 1 &&
                   fixture.session.history.UndoName() == L"Outer named transaction" &&
                   IsRect(fixture.session.Document(), 2),
               "nested.outer-name-owns-single-history-entry");
    }
}

enum class StaleChange
{
    Active,
    Mask,
    Crop,
    Generation,
    History,
    ContinuedMove,
};

const char* StaleName(StaleChange change)
{
    switch (change)
    {
    case StaleChange::Active:
        return "stale-active";
    case StaleChange::Mask:
        return "stale-mask";
    case StaleChange::Crop:
        return "stale-crop";
    case StaleChange::Generation:
        return "stale-generation";
    case StaleChange::History:
        return "stale-history";
    case StaleChange::ContinuedMove:
        return "stale-continued-move";
    }
    return "stale-invalid";
}

void StaleCandidates()
{
    constexpr std::array changes{StaleChange::Active,     StaleChange::Mask,    StaleChange::Crop,
                                 StaleChange::Generation, StaleChange::History, StaleChange::ContinuedMove};
    for (const StaleChange change : changes)
    {
        Fixture fixture;
        const std::string name = StaleName(change);
        Expect(fixture.ready && fixture.session.BeginSelectionMove(), (name + ".fixture-and-begin").c_str());
        if (!fixture.ready || !fixture.session.IsMovingSelection())
            continue;
        fixture.session.MoveSelection(1, 0);
        auto candidate = Prepare(fixture, false);
        Expect(candidate.has_value(), (name + ".candidate-prepared").c_str());
        if (!candidate)
            continue;

        const auto generationBeforeChange = fixture.session.Generation();
        const auto historyBeforeChange = fixture.session.history.CaptureState();
        switch (change)
        {
        case StaleChange::Active:
            fixture.session.SelectLayerTarget(fixture.otherLayer, false);
            break;
        case StaleChange::Mask:
            fixture.session.SelectLayerTarget(fixture.movingLayer, true);
            break;
        case StaleChange::Crop:
            fixture.session.SetCropFrame(CropRect{0, 0, 3, 1});
            break;
        case StaleChange::Generation:
            EditLayerRecordsForTest(fixture.session, [](DocumentFixture& document)
                                    { document.layers.front().name = L"Changed metadata"; });
            break;
        case StaleChange::History:
        {
            const auto document = fixture.session.Document();
            const auto active = fixture.session.ActiveLayerID();
            fixture.session.history.Begin(L"Nested no-op authority change", document, active);
            fixture.session.history.End(document, active);
            break;
        }
        case StaleChange::ContinuedMove:
            fixture.session.MoveSelection(2, 0);
            break;
        }

        if (change == StaleChange::Active || change == StaleChange::Mask || change == StaleChange::Crop)
            Expect(fixture.session.Generation() == generationBeforeChange &&
                       fixture.session.history.IsCurrent(historyBeforeChange),
                   (name + ".independent-authority-field-without-generation-or-epoch-change").c_str());
        if (change == StaleChange::Mask)
            Expect(fixture.session.IsMaskSelected(), "stale-mask.real-existing-mask-target-was-reached");

        const ObservedState invalidated = Capture(fixture.session);
        Expect(!fixture.session.IsPreparedSelectionMoveClosureCurrent(candidate.value()),
               (name + ".current-refused").c_str());
        Expect(!fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()),
               (name + ".apply-refused").c_str());
        Expect(Matches(fixture.session, invalidated), (name + ".refusal-retains-live-and-history-state").c_str());
    }
}

void SavedMarkerAndOwnershipRules()
{
    {
        Fixture fixture;
        Expect(fixture.ready && fixture.session.BeginSelectionMove(), "saved.fixture-and-begin");
        if (!fixture.ready || !fixture.session.IsMovingSelection())
            return;
        fixture.session.MoveSelection(1, 0);
        auto candidate = Prepare(fixture, false);
        Expect(candidate.has_value(), "saved.candidate-prepared");
        if (candidate)
        {
            const auto openingRevision = fixture.session.history.CurrentRevision();
            fixture.session.history.MarkSaved(Guid::Generate());
            Expect(fixture.session.IsPreparedSelectionMoveClosureCurrent(candidate.value()) &&
                       fixture.session.history.IsModified(),
                   "saved.marker-does-not-invalidate-edit-authority");
            Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()) &&
                       fixture.session.history.IsModified(),
                   "saved.apply-keeps-later-marker");
            fixture.session.Undo();
            Expect(fixture.session.history.CurrentRevision() == openingRevision &&
                       fixture.session.history.IsModified() && IsRect(fixture.session.Document(), 1),
                   "saved.undo-proves-distinct-later-marker-was-not-overwritten");
        }
    }
    {
        Fixture fixture;
        Expect(fixture.ready && fixture.session.BeginSelectionMove(), "ownership.fixture-and-begin");
        if (!fixture.ready || !fixture.session.IsMovingSelection())
            return;
        fixture.session.MoveSelection(1, 0);
        auto input = fixture.session.PrepareSelectionMoveClosureInput(false);
        Expect(input.has_value(), "ownership.input-captured");
        if (!input)
            return;
        SelectionMoveClosureInput movedInput(std::move(input.value()));
        auto fromMovedInput = DocumentSession::PrepareSelectionMoveClosurePublication(std::move(input.value()));
        Expect(!fromMovedInput, "ownership.moved-from-input-refused");
        auto candidate = DocumentSession::PrepareSelectionMoveClosurePublication(std::move(movedInput));
        Expect(candidate.has_value(), "ownership.moved-to-input-accepted");
        if (!candidate)
            return;

        PreparedSelectionMoveClosure movedCandidate(std::move(candidate.value()));
        Expect(!fixture.session.IsPreparedSelectionMoveClosureCurrent(candidate.value()) &&
                   !fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()),
               "ownership.moved-from-prepared-refused");
        Expect(fixture.session.IsPreparedSelectionMoveClosureCurrent(movedCandidate) &&
                   fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(movedCandidate),
               "ownership.moved-to-prepared-applies-once");
        Expect(!fixture.session.IsPreparedSelectionMoveClosureCurrent(movedCandidate) &&
                   !fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(movedCandidate),
               "ownership.prepared-is-single-use");
    }
    {
        Fixture fixture;
        Expect(fixture.ready && fixture.session.BeginSelectionMove(), "ownership.session-copy-fixture");
        auto candidate = Prepare(fixture, false);
        DocumentSession copied = fixture.session;
        Expect(candidate && !copied.IsPreparedSelectionMoveClosureCurrent(*candidate) &&
                   !copied.ApplyPreparedSelectionMoveClosureIfCurrent(*candidate) &&
                   fixture.session.IsPreparedSelectionMoveClosureCurrent(*candidate),
               "ownership.session-copy-does-not-inherit-publication-authority");
        DocumentSession moved = std::move(fixture.session);
        Expect(candidate && !moved.IsPreparedSelectionMoveClosureCurrent(*candidate) &&
                   !fixture.session.IsPreparedSelectionMoveClosureCurrent(*candidate),
               "ownership.session-move-retires-source-and-does-not-inherit-authority");
    }
    {
        Fixture first;
        Fixture foreign;
        Expect(first.ready && foreign.ready && first.session.BeginSelectionMove() &&
                   foreign.session.BeginSelectionMove(),
               "ownership.foreign-fixtures");
        if (!first.ready || !foreign.ready || !first.session.IsMovingSelection() ||
            !foreign.session.IsMovingSelection())
            return;
        first.session.MoveSelection(1, 0);
        foreign.session.MoveSelection(1, 0);
        auto candidate = Prepare(first, false);
        Expect(candidate.has_value(), "ownership.foreign-candidate-prepared");
        if (candidate)
        {
            const ObservedState before = Capture(foreign.session);
            Expect(!foreign.session.IsPreparedSelectionMoveClosureCurrent(candidate.value()) &&
                       !foreign.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()),
                   "ownership.foreign-session-refused");
            Expect(Matches(foreign.session, before), "ownership.foreign-refusal-preserves-state");
        }
    }
}

void FailureAtChangedHistoryCheckpoint(DocumentHistory::PrepareStage stage, const char* stageName, bool cancelClosure,
                                       bool throwAllocation, bool restoreSelection)
{
    Fixture fixture;
    const std::string name =
        std::string(cancelClosure ? "cancel" : "end") + "-" + stageName + (throwAllocation ? "-oom" : "-cancel");
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), (name + ".fixture-and-begin").c_str());
    if (!fixture.ready || !fixture.session.IsMovingSelection())
        return;
    if (restoreSelection)
    {
        // This real metadata edit is inside the Move Selection history transaction. Cancel restores only the
        // outline, so the completed transaction still differs from its opening snapshot at depth one.
        EditLayerRecordsForTest(fixture.session,
                                [](DocumentFixture& document) { document.layers.front().name = L"Prior real edit"; });
    }
    fixture.session.MoveSelection(1, 0);
    auto input = fixture.session.PrepareSelectionMoveClosureInput(cancelClosure);
    Expect(input.has_value(), (name + ".input-captured").c_str());
    if (!input)
        return;

    const ObservedState before = Capture(fixture.session);
    Checkpoint checkpoint{stage, throwAllocation};
    DocumentHistory::PrepareObserver observer{&Checkpoint::Visit, &checkpoint};
    bool allocationPropagated = false;
    std::optional<PreparedSelectionMoveClosure> candidate;
    try
    {
        candidate = DocumentSession::PrepareSelectionMoveClosurePublication(
            std::move(input.value()), !throwAllocation ? checkpoint.cancellation.Token() : CancelToken{}, observer);
    }
    catch (const std::bad_alloc&)
    {
        allocationPropagated = true;
    }
    Expect(checkpoint.hits == 1, (name + ".target-checkpoint-reached-once").c_str());
    Expect(throwAllocation ? (!candidate && allocationPropagated)
                           : (!candidate && !allocationPropagated && checkpoint.cancellation.Token().IsCancelled()),
           (name + ".requested-failure-observed").c_str());
    Expect(Matches(fixture.session, before), (name + ".source-history-and-pending-move-unchanged").c_str());

    auto recovery = Prepare(fixture, cancelClosure);
    Expect(recovery && fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(recovery.value()),
           (name + ".retry-completes-closure").c_str());
}

void ChangedHistoryFailures()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    constexpr std::array names{"after-snapshot", "entry", "stack-growth", "trim-index"};
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        FailureAtChangedHistoryCheckpoint(stages[i], names[i], false, true, false);
        FailureAtChangedHistoryCheckpoint(stages[i], names[i], false, false, false);
        FailureAtChangedHistoryCheckpoint(stages[i], names[i], true, true, true);
        FailureAtChangedHistoryCheckpoint(stages[i], names[i], true, false, true);
    }
}

void FullyUndoneCancelSkipsChangedHistoryCheckpoints()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "undone-cancel.fixture-and-begin");
    if (!fixture.ready || !fixture.session.IsMovingSelection())
        return;
    fixture.session.MoveSelection(1, 0);
    const ObservedState before = Capture(fixture.session);
    Checkpoint checkpoint{DocumentHistory::PrepareStage::AfterSnapshot, true};
    DocumentHistory::PrepareObserver observer{&Checkpoint::Visit, &checkpoint};
    auto candidate = Prepare(fixture, true, observer);
    Expect(candidate.has_value() && checkpoint.hits == 0, "undone-cancel.no-changed-history-checkpoint-is-reached");
    if (candidate)
    {
        const std::uint64_t generationBeforeApply = fixture.session.Generation();
        Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(candidate.value()) &&
                   fixture.session.Generation() == generationBeforeApply + 2 && IsRect(fixture.session.Document(), 1) &&
                   fixture.session.history.CanRedo(),
               "undone-cancel.restores-outline-and-preserves-redo");
    }
    else
        Expect(Matches(fixture.session, before), "undone-cancel.failed-preparation-retains-source-state");
}

void NoActiveMoveIsRefused()
{
    Fixture fixture;
    Expect(fixture.ready && !fixture.session.PrepareSelectionMoveClosureInput(false) &&
               !fixture.session.PrepareSelectionMoveClosureInput(true),
           "eligibility.no-input-without-active-move");
    Expect(fixture.session.SelectionMoveClosureMemoryBytes() == 0, "eligibility.no-reservation-without-move");
    Expect(fixture.session.BeginSelectionMove() && fixture.session.SelectionMoveClosureMemoryBytes() > 0,
           "eligibility.valid-move-declares-complete-construction-peak");
    const auto before = Capture(fixture.session);
    CancelSource cancellation;
    (void)cancellation.Cancel();
    auto candidate = Prepare(fixture, false, {}, cancellation.Token());
    Expect(!candidate && Matches(fixture.session, before), "eligibility.pre-cancelled-work-retains-complete-state");
}
} // namespace

int RunSelectionMoveClosureTests()
{
    gFailures = 0;
    gAssertions = 0;
    EndCommitsMovedRectangle();
    CancelRestoresOriginAndPreservesRedo();
    NoOpAndNestedNamedTransaction();
    StaleCandidates();
    SavedMarkerAndOwnershipRules();
    ChangedHistoryFailures();
    FullyUndoneCancelSkipsChangedHistoryCheckpoints();
    NoActiveMoveIsRefused();
    std::printf("SUMMARY selection-move-closure assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

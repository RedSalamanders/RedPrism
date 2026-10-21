#include "Document.Tests.SelectionMoveOpening.h"

#include "../Support/Support.Tests.DocumentFixture.h"

#include "SelectionMoveWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
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
    std::printf("%s selection-move-opening.%s\n", condition ? "PASS" : "FAIL", name);
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

CanvasDocument MakeDocument(bool withSelection, Guid& activeLayer)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    if (withSelection)
        document.selection = Rect(1);
    auto layer = ImageLayer::Blank(L"Selection move opening fixture", Vec2{4, 1});
    activeLayer = layer.id;
    layer.asset = RasterAsset{Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, layer.name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

struct Fixture final
{
    DocumentSession session;
    Guid activeLayer;
    bool ready = false;

    Fixture()
    {
        InstallDocument(true);
    }

    void InstallDocument(bool withSelection)
    {
        auto document = MakeDocument(withSelection, activeLayer);
        session.ReplaceDocument(std::move(document), activeLayer);
        if (!withSelection)
        {
            ready = session.Document().has_value() && !session.Selection();
            return;
        }
        session.SetSelection(Rect(0), L"Seed redo selection");
        session.Undo();
        ready = session.Document().has_value() && session.ActiveLayerID() == activeLayer &&
                IsRect(session.Document(), 1) && session.history.CanRedo();
    }
};

struct ObservedState final
{
    std::optional<CanvasDocument> document;
    std::optional<DocumentSelection> selection;
    bool moving = false;
    std::optional<std::uint64_t> opening;
    std::uint64_t generation = 0;
    Guid revision;
    DocumentHistory::StateView history;
    int undoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
};

ObservedState Capture(const DocumentSession& session)
{
    return ObservedState{session.Document(),
                         session.Selection(),
                         session.IsMovingSelection(),
                         session.SelectionMoveOpening(),
                         session.Generation(),
                         session.history.CurrentRevision(),
                         session.history.CaptureState(),
                         session.history.UndoCount(),
                         session.history.UndoName(),
                         session.history.RedoName()};
}

bool Matches(const DocumentSession& session, const ObservedState& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.Selection() == before.selection &&
           session.IsMovingSelection() == before.moving && session.SelectionMoveOpening() == before.opening &&
           session.Generation() == before.generation && session.history.CurrentRevision() == before.revision &&
           session.history.IsCurrent(before.history) && history.Depth() == before.history.Depth() &&
           history.PastCount() == before.history.PastCount() && history.FutureCount() == before.history.FutureCount() &&
           session.history.UndoCount() == before.undoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName;
}

std::optional<PreparedSelectionMoveClosure> Prepare(Fixture& fixture, bool cancel, std::uint64_t opening)
{
    auto input = fixture.session.PrepareSelectionMoveClosureInput(cancel, opening);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareSelectionMoveClosurePublication(std::move(input.value()));
}

void OpeningEligibilityAndStability()
{
    DocumentSession missing;
    const auto beforeMissing = Capture(missing);
    Expect(!missing.BeginSelectionMove() && !missing.SelectionMoveOpening() && Matches(missing, beforeMissing),
           "missing-document-begin-refused-without-state-change");
    Fixture absent;
    Expect(absent.ready && !absent.session.SelectionMoveOpening() && !absent.session.IsMovingSelection(),
           "initially-absent");
    Expect(absent.session.BeginSelectionMove(), "first-begin-succeeds");
    const auto first = absent.session.SelectionMoveOpening();
    Expect(first && first.value() > 0, "first-opening-is-positive");
    const auto beforeNonfinite = Capture(absent.session);
    absent.session.MoveSelection((std::numeric_limits<double>::quiet_NaN)(), 0);
    absent.session.MoveSelection(0, (std::numeric_limits<double>::infinity)());
    Expect(Matches(absent.session, beforeNonfinite), "nonfinite-samples-preserve-opening-and-complete-state");
    absent.session.MoveSelection(1, 0);
    Expect(absent.session.SelectionMoveOpening() == first && IsRect(absent.session.Document(), 2),
           "opening-stable-after-first-move-sample");
    absent.session.MoveSelection(-1, 0);
    Expect(absent.session.SelectionMoveOpening() == first && IsRect(absent.session.Document(), 0),
           "opening-stable-after-second-move-sample");
    const ObservedState beforeDuplicate = Capture(absent.session);
    Expect(!absent.session.BeginSelectionMove() && absent.session.SelectionMoveOpening() == first &&
               Matches(absent.session, beforeDuplicate),
           "duplicate-begin-refused-without-document-history-or-opening-change");
    absent.session.EndSelectionMove();
    Expect(!absent.session.SelectionMoveOpening(), "direct-end-makes-opening-absent");

    Fixture empty;
    empty.InstallDocument(false);
    Expect(empty.ready && !empty.session.BeginSelectionMove() && !empty.session.SelectionMoveOpening(),
           "absent-selection-begin-refused");
    empty.session.SetSelection(DocumentSelection{false, SelectionLoops{}}, L"Explicit empty fixture");
    const auto beforeEmpty = Capture(empty.session);
    Expect(!empty.session.BeginSelectionMove() && !empty.session.SelectionMoveOpening() &&
               Matches(empty.session, beforeEmpty),
           "explicit-empty-selection-begin-refused-without-state-change");
}

void WrongExpectedOpeningsAreSideEffectFree()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "wrong-opening-fixture-begins");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening() ||
        fixture.session.SelectionMoveOpening().value() == 0)
        return;
    const auto priorOpening = fixture.session.SelectionMoveOpening().value();
    fixture.session.EndSelectionMove();
    Expect(fixture.session.BeginSelectionMove(), "wrong-opening-second-move-begins");
    if (!fixture.session.SelectionMoveOpening())
        return;
    const auto opening = fixture.session.SelectionMoveOpening().value();
    Expect(opening > priorOpening, "wrong-opening-prior-is-a-real-earlier-gesture");
    const std::array<std::uint64_t, 3> wrong{0, (std::numeric_limits<std::uint64_t>::max)(), priorOpening};
    constexpr std::array names{"zero", "maximum", "prior"};
    for (std::size_t i = 0; i < wrong.size(); ++i)
    {
        const ObservedState before = Capture(fixture.session);
        Expect(!fixture.session.PrepareSelectionMoveClosureInput(false, wrong[i]),
               (std::string("wrong-") + names[i] + "-refused-before-capture").c_str());
        Expect(
            Matches(fixture.session, before),
            (std::string("wrong-") + names[i] + "-selection-history-epoch-revision-generation-depth-intact").c_str());
        Expect(!fixture.session.PrepareSelectionMoveClosureInput(true, wrong[i]) && Matches(fixture.session, before),
               (std::string("wrong-") + names[i] + "-cancel-refused-without-state-change").c_str());
    }
}

void ReopeningUsesANewerOpening()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "reopen-fixture-begins");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto oldOpening = fixture.session.SelectionMoveOpening().value();
    fixture.session.EndSelectionMove();
    Expect(!fixture.session.SelectionMoveOpening() && IsRect(fixture.session.Document(), 1),
           "direct-end-keeps-identical-outline");
    Expect(fixture.session.BeginSelectionMove(), "identical-outline-reopens");
    const auto newOpening = fixture.session.SelectionMoveOpening();
    Expect(newOpening && newOpening.value() > oldOpening && IsRect(fixture.session.Document(), 1),
           "reopen-has-strictly-newer-opening-for-identical-geometry");
    const ObservedState before = Capture(fixture.session);
    Expect(!fixture.session.PrepareSelectionMoveClosureInput(false, oldOpening),
           "old-opening-refused-after-identical-outline-reopen");
    Expect(Matches(fixture.session, before), "old-opening-refusal-preserves-document-and-authority-state");
}

void PreparedClosureCannotCrossReopen()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "stale-fixture-begins");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto opening = fixture.session.SelectionMoveOpening().value();
    auto stale = Prepare(fixture, false, opening);
    Expect(stale.has_value(), "prepared-before-close");
    if (!stale)
        return;
    fixture.session.EndSelectionMove();
    Expect(fixture.session.BeginSelectionMove(), "new-move-opens-after-direct-close");
    const ObservedState reopened = Capture(fixture.session);
    Expect(!fixture.session.IsPreparedSelectionMoveClosureCurrent(stale.value()),
           "prepared-from-prior-opening-is-not-current");
    Expect(!fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(stale.value()),
           "prepared-from-prior-opening-cannot-apply");
    Expect(Matches(fixture.session, reopened), "stale-apply-refusal-preserves-reopened-move");
}

void FreshPreparedEndHasLiteralCommitSemantics()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "end-fixture-begins-with-redo");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto opening = fixture.session.SelectionMoveOpening().value();
    fixture.session.MoveSelection(1, 0);
    const int undoBefore = fixture.session.history.UndoCount();
    const auto revisionBefore = fixture.session.history.CurrentRevision();
    const auto generationBefore = fixture.session.Generation();
    auto prepared = Prepare(fixture, false, opening);
    Expect(prepared.has_value(), "end-prepared-for-exact-opening");
    if (!prepared)
        return;
    Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(prepared.value()), "end-prepared-applies");
    Expect(!fixture.session.SelectionMoveOpening() && IsRect(fixture.session.Document(), 2),
           "end-keeps-literal-moved-rectangle-and-closes");
    Expect(fixture.session.Generation() == generationBefore + 1, "end-adds-one-generation");
    Expect(fixture.session.history.UndoCount() == undoBefore + 1 && !fixture.session.history.CanRedo() &&
               fixture.session.history.UndoName() == L"Move Selection" &&
               fixture.session.history.CurrentRevision() != revisionBefore,
           "end-adds-one-named-history-entry-and-discards-redo");
    fixture.session.Undo();
    Expect(IsRect(fixture.session.Document(), 1), "end-undo-restores-literal-origin");
    fixture.session.Redo();
    Expect(IsRect(fixture.session.Document(), 2), "end-redo-restores-literal-moved-outline");
}

void FreshPreparedCancelHasLiteralRestoreSemantics()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "cancel-fixture-begins-with-redo");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto opening = fixture.session.SelectionMoveOpening().value();
    fixture.session.MoveSelection(1, 0);
    const int undoBefore = fixture.session.history.UndoCount();
    const auto revisionBefore = fixture.session.history.CurrentRevision();
    const auto generationBefore = fixture.session.Generation();
    auto prepared = Prepare(fixture, true, opening);
    Expect(prepared.has_value(), "cancel-prepared-for-exact-opening");
    if (!prepared)
        return;
    Expect(fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(prepared.value()), "cancel-prepared-applies");
    Expect(!fixture.session.SelectionMoveOpening() && IsRect(fixture.session.Document(), 1),
           "cancel-restores-literal-origin-and-closes");
    Expect(fixture.session.Generation() == generationBefore + 2, "cancel-restore-and-close-add-two-generations");
    Expect(fixture.session.history.UndoCount() == undoBefore && fixture.session.history.CanRedo() &&
               fixture.session.history.RedoName() == L"Seed redo selection" &&
               fixture.session.history.CurrentRevision() == revisionBefore,
           "cancel-adds-no-entry-and-preserves-redo-and-revision");
}

void NoOpClosePreservesRedo()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "noop-fixture-begins-with-redo");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto opening = fixture.session.SelectionMoveOpening().value();
    const auto before = fixture.session.Document();
    const auto revision = fixture.session.history.CurrentRevision();
    const auto generation = fixture.session.Generation();
    const int undoCount = fixture.session.history.UndoCount();
    auto prepared = Prepare(fixture, false, opening);
    Expect(prepared && fixture.session.ApplyPreparedSelectionMoveClosureIfCurrent(prepared.value()),
           "noop-prepared-and-applied");
    Expect(fixture.session.Document() == before && fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.UndoCount() == undoCount && fixture.session.history.CanRedo() &&
               fixture.session.history.RedoName() == L"Seed redo selection",
           "noop-close-preserves-document-revision-history-count-and-redo");
    Expect(!fixture.session.SelectionMoveOpening() && fixture.session.Generation() == generation + 1,
           "noop-close-retires-opening-and-acknowledges-one-generation");
}

void ReplacementAndClearKeepTheCounterMonotonic()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "replacement-fixture-begins");
    if (!fixture.ready || !fixture.session.SelectionMoveOpening())
        return;
    const auto first = fixture.session.SelectionMoveOpening().value();
    fixture.InstallDocument(true);
    Expect(!fixture.session.SelectionMoveOpening(), "replacement-clears-an-active-opening");
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "replacement-document-begins-move");
    const auto second = fixture.session.SelectionMoveOpening();
    Expect(second && second.value() > first, "replacement-document-does-not-reuse-opening");
    fixture.session.ClearDocument();
    Expect(!fixture.session.SelectionMoveOpening() && fixture.session.SelectionMoveClosureRetirementMemoryBytes() == 0,
           "clear-removes-document-move-and-retirement-reservation");
    fixture.InstallDocument(true);
    Expect(fixture.ready && fixture.session.BeginSelectionMove(), "recreated-document-begins-move");
    const auto third = fixture.session.SelectionMoveOpening();
    Expect(third && second && third.value() > second.value(), "clear-and-recreate-does-not-reuse-opening");
}

void RetirementBoundCountsOriginStorageAndDoesNotMaterializeSparseRaster()
{
    Fixture fixture;
    Expect(fixture.ready && fixture.session.SelectionMoveClosureRetirementMemoryBytes() == 0,
           "retirement-bound-zero-without-move");
    if (!fixture.ready || !fixture.session.ActiveLayer() || !fixture.session.ActiveLayer()->asset)
        return;
    const auto sparse = fixture.session.ActiveLayer()->asset->image;
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "fixture-raster-is-sparse-and-unmaterialized");
    const std::uint64_t originLoopBytes = fixture.session.Selection()->loops.StorageBytes();
    Expect(fixture.session.BeginSelectionMove(), "memory-fixture-begins");
    fixture.session.MoveSelection(1, 0);
    const std::uint64_t historyAndDisplayBytes = fixture.session.HistoryRestoreRetirementMemoryBytes();
    const std::uint64_t expected = historyAndDisplayBytes + originLoopBytes + sizeof(DocumentSelection) + 128u;
    const std::uint64_t actual = fixture.session.SelectionMoveClosureRetirementMemoryBytes();
    Expect(originLoopBytes > 0 && historyAndDisplayBytes > 0 && actual == expected,
           "retirement-bound-includes-cached-original-loops-and-session-graph");
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() &&
               !fixture.session.ActiveLayer()->asset->image.IsMaterialized(),
           "retirement-bound-does-not-materialize-sparse-raster");
    Expect(IsRect(fixture.session.Document(), 2), "retirement-bound-preserves-current-literal-outline");
}
} // namespace

int RunSelectionMoveOpeningTests()
{
    gFailures = 0;
    gAssertions = 0;
    OpeningEligibilityAndStability();
    WrongExpectedOpeningsAreSideEffectFree();
    ReopeningUsesANewerOpening();
    PreparedClosureCannotCrossReopen();
    FreshPreparedEndHasLiteralCommitSemantics();
    FreshPreparedCancelHasLiteralRestoreSemantics();
    NoOpClosePreservesRedo();
    ReplacementAndClearKeepTheCounterMonotonic();
    RetirementBoundCountsOriginStorageAndDoesNotMaterializeSparseRaster();
    std::printf("SUMMARY selection-move-opening assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

#include "Document.Tests.SelectionGestureAbort.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "SelectionMoveOpeningWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
unsigned int gAssertions = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s selection-gesture-abort.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0u : 1u;
}

constexpr std::array<Vec2, 4> kOrigin{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
constexpr std::array<Vec2, 4> kAfterFirstMove{{{3, 0}, {5, 0}, {5, 2}, {3, 2}}};
constexpr std::array<Vec2, 4> kAfterSecondMove{{{4, 1}, {6, 1}, {6, 3}, {4, 3}}};
constexpr std::array<Vec2, 4> kCommittedMove{{{4, 3}, {6, 3}, {6, 5}, {4, 5}}};

DocumentSelection SquareSelection(const std::array<Vec2, 4>& points)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{points[0], points[1], points[2], points[3]}}};
}

bool HasLiteral(const std::optional<DocumentSelection>& selection, const std::array<Vec2, 4>& expected)
{
    if (!selection || selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& loop = selection->loops.Get()[0];
    if (loop.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        if (loop[i] != expected[i])
            return false;
    }
    return true;
}

CanvasDocument MakeDocument(Guid& activeLayer, std::optional<Guid>& alternateLayer)
{
    DocumentFixture source;
    source.id = Guid::Generate();
    source.width = 8;
    source.height = 8;
    source.selection = SquareSelection(kOrigin);

    auto first = ImageLayer::Blank(L"Gesture abort first layer", Vec2{8, 8});
    activeLayer = first.id;
    first.asset = RasterAsset{Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, first.name};
    source.layers.push_back(std::move(first));

    auto second = ImageLayer::Blank(L"Gesture abort alternate layer", Vec2{8, 8});
    alternateLayer = second.id;
    second.asset =
        RasterAsset{Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, second.name};
    source.layers.push_back(std::move(second));
    return std::move(source).Seal();
}

struct Fixture final
{
    std::unique_ptr<DocumentSession> session{std::make_unique<DocumentSession>()};
    Guid activeLayer;
    std::optional<Guid> alternateLayer;
    bool ready = false;

    Fixture()
    {
        InstallDocument();
    }

    void InstallDocument()
    {
        alternateLayer.reset();
        auto document = MakeDocument(activeLayer, alternateLayer);
        session->ReplaceDocument(std::move(document), activeLayer);
        session->SetSelection(SquareSelection(std::array<Vec2, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}),
                              L"Seed redo selection");
        session->Undo();
        ready = session->Document().has_value() && session->ActiveLayerID() == activeLayer &&
                HasLiteral(session->Selection(), kOrigin) && session->history.CanRedo() &&
                session->history.RedoName() == L"Seed redo selection";
    }
};

using Prepared = std::optional<PreparedSelectionMoveOpening>;

Prepared Prepare(Fixture& fixture, bool prepareAbort = true, CancelToken cancellation = {})
{
    auto input = fixture.session->PrepareDeferredSelectionMoveOpeningInput();
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(input.value()), cancellation, {}, {},
                                                                prepareAbort);
}

bool Apply(Fixture& fixture, Prepared& prepared)
{
    return prepared && fixture.session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(prepared.value());
}

struct ObservedState final
{
    std::optional<CanvasDocument> document;
    std::optional<DocumentSelection> selection;
    const void* selectionRoot = nullptr;
    const void* layerRoot = nullptr;
    std::optional<Guid> activeLayer;
    bool moving = false;
    bool deferred = false;
    std::optional<std::uint64_t> opening;
    Vec2 displayedTranslation;
    std::uint64_t generation = 0;
    std::uint64_t documentSerial = 0;
    Guid historyRevision;
    DocumentHistory::StateView history;
    int undoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
};

ObservedState Capture(const DocumentSession& session)
{
    const auto& selection = session.Selection();
    const auto document = session.Document();
    const void* selectionRoot = selection ? selection->loops.Identity() : nullptr;
    const void* layerRoot = document ? document->layers.Identity() : nullptr;
    return ObservedState{document,
                         selection,
                         selectionRoot,
                         layerRoot,
                         session.ActiveLayerID(),
                         session.IsMovingSelection(),
                         session.IsDeferredSelectionMove(),
                         session.SelectionMoveOpening(),
                         session.DisplayedSelectionValue().translation,
                         session.Generation(),
                         session.DocumentSerial(),
                         session.history.CurrentRevision(),
                         session.history.CaptureState(),
                         session.history.UndoCount(),
                         session.history.UndoName(),
                         session.history.RedoName()};
}

bool Matches(const DocumentSession& session, const ObservedState& expected)
{
    const auto state = session.history.CaptureState();
    const auto& selection = session.Selection();
    const auto document = session.Document();
    const void* selectionRoot = selection ? selection->loops.Identity() : nullptr;
    const void* layerRoot = document ? document->layers.Identity() : nullptr;
    return document == expected.document && selection == expected.selection &&
           selectionRoot == expected.selectionRoot && layerRoot == expected.layerRoot &&
           session.ActiveLayerID() == expected.activeLayer && session.IsMovingSelection() == expected.moving &&
           session.IsDeferredSelectionMove() == expected.deferred &&
           session.SelectionMoveOpening() == expected.opening &&
           session.DisplayedSelectionValue().translation == expected.displayedTranslation &&
           session.Generation() == expected.generation && session.DocumentSerial() == expected.documentSerial &&
           session.history.CurrentRevision() == expected.historyRevision &&
           session.history.IsCurrent(expected.history) && state.Depth() == expected.history.Depth() &&
           state.PastCount() == expected.history.PastCount() && state.FutureCount() == expected.history.FutureCount() &&
           session.history.UndoCount() == expected.undoCount && session.history.UndoName() == expected.undoName &&
           session.history.RedoName() == expected.redoName;
}

bool HasSeedRedo(const DocumentSession& session, const ObservedState& beforeOpening)
{
    const auto current = session.history.CaptureState();
    return session.history.CurrentRevision() == beforeOpening.historyRevision &&
           session.history.UndoCount() == beforeOpening.undoCount && current.Depth() == beforeOpening.history.Depth() &&
           current.PastCount() == beforeOpening.history.PastCount() &&
           current.FutureCount() == beforeOpening.history.FutureCount() && session.history.CanRedo() &&
           session.history.RedoName() == L"Seed redo selection";
}

void AbortBeforeMoveRestoresExactSeededState()
{
    Fixture fixture;
    const auto beforeOpening = Capture(*fixture.session);
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared.has_value(), "before-move-prepares-begin-and-abort-end");
    if (!fixture.ready || !prepared)
        return;
    Expect(Apply(fixture, prepared), "before-move-publishes-prepared-opening");
    if (!fixture.session->IsDeferredSelectionMove())
        return;
    const auto generationBeforeAbort = fixture.session->Generation();
    const void* sourceRoot = fixture.session->Selection()->loops.Identity();
    Expect(fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "before-move-abort-publishes-prepared-noop-end");
    Expect(HasLiteral(fixture.session->Selection(), kOrigin) &&
               fixture.session->Selection()->loops.Identity() == sourceRoot,
           "before-move-abort-retains-exact-literal-source-root");
    Expect(fixture.session->Generation() == generationBeforeAbort + 2,
           "before-move-abort-applies-two-reference-cancel-generation-bumps");
    Expect(!fixture.session->IsMovingSelection() && !fixture.session->IsDeferredSelectionMove() &&
               !fixture.session->SelectionMoveOpening(),
           "before-move-abort-closes-only-its-opening");
    Expect(HasSeedRedo(*fixture.session, beforeOpening), "before-move-abort-preserves-history-depth-and-redo-seed");
}

void AbortAfterRoundedMovesRestoresOriginalGeometryAndRoot()
{
    Fixture fixture;
    const auto beforeOpening = Capture(*fixture.session);
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "rounded-move-opening-prepared-and-published");
    if (!prepared || !fixture.session->IsDeferredSelectionMove())
        return;
    const void* sourceRoot = fixture.session->Selection()->loops.Identity();
    Expect(fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(prepared.value(), 2.4, -1.6) &&
               fixture.session->DisplayedSelectionValue().translation == Vec2{2, -2} &&
               HasLiteral(fixture.session->DisplayedSelection(), kAfterFirstMove),
           "first-move-rounds-to-literal-two-minus-two-preview-square");
    Expect(fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(prepared.value(), 3.1, -0.6) &&
               fixture.session->DisplayedSelectionValue().translation == Vec2{3, -1} &&
               HasLiteral(fixture.session->DisplayedSelection(), kAfterSecondMove),
           "second-move-is-absolute-and-rounds-to-literal-three-minus-one-preview-square");
    const auto generationBeforeAbort = fixture.session->Generation();
    Expect(fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "rounded-moves-abort-with-prepared-end");
    Expect(HasLiteral(fixture.session->Selection(), kOrigin) &&
               fixture.session->Selection()->loops.Identity() == sourceRoot,
           "rounded-moves-abort-restores-exact-origin-loop-and-root");
    Expect(fixture.session->Generation() == generationBeforeAbort + 2,
           "rounded-moves-abort-adds-two-reference-cancel-generations");
    Expect(HasSeedRedo(*fixture.session, beforeOpening), "rounded-moves-abort-preserves-seeded-redo-and-history-depth");
}

void InvalidOffsetsLeaveThePreparedGestureAbortable()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "invalid-offset-opening-prepared-and-published");
    if (!prepared || !fixture.session->IsDeferredSelectionMove())
        return;
    const std::array<std::pair<double, double>, 2> invalid{{
        {(std::numeric_limits<double>::quiet_NaN)(), 0},
        {0, (std::numeric_limits<double>::infinity)()},
    }};
    constexpr std::array names{"nan-x", "infinite-y"};
    for (std::size_t i = 0; i < invalid.size(); ++i)
    {
        const auto before = Capture(*fixture.session);
        const bool moved = fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(
            prepared.value(), invalid[i].first, invalid[i].second);
        const std::string label = std::string("invalid-") + names[i] + "-refused-without-authority-or-source-change";
        Expect(!moved && Matches(*fixture.session, before), label.c_str());
    }
    const auto generationBeforeAbort = fixture.session->Generation();
    Expect(fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "invalid-offset-refusals-leave-abort-current");
    Expect(HasLiteral(fixture.session->Selection(), kOrigin) &&
               fixture.session->Generation() == generationBeforeAbort + 2,
           "invalid-offset-abort-retains-origin-and-cancel-generations");
}

void DuplicateAbortIsIdempotentlyRefused()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "duplicate-abort-opening-prepared-and-published");
    if (!prepared || !fixture.session->IsDeferredSelectionMove())
        return;
    Expect(fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(prepared.value(), -1.4, 2.2),
           "duplicate-abort-fixture-moves-once");
    Expect(fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()), "first-abort-succeeds");
    const auto afterFirstAbort = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "duplicate-abort-refuses-after-terminal-publication");
    Expect(Matches(*fixture.session, afterFirstAbort) && HasLiteral(fixture.session->Selection(), kOrigin),
           "duplicate-abort-refusal-preserves-closed-document-and-history");
}

void NormalEndConsumesAbortAuthority()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "normal-end-opening-prepared-and-published");
    if (!prepared || !fixture.session->IsDeferredSelectionMove())
        return;
    Expect(fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(prepared.value(), 2.7, 1.2),
           "normal-end-fixture-moves-to-literal-committed-offset");
    fixture.session->EndSelectionMove();
    Expect(HasLiteral(fixture.session->Selection(), kCommittedMove) && !fixture.session->IsMovingSelection(),
           "normal-end-commits-literal-geometry-and-closes");
    const auto afterEnd = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-after-normal-end");
    Expect(Matches(*fixture.session, afterEnd) && HasLiteral(fixture.session->Selection(), kCommittedMove),
           "abort-after-end-cannot-overwrite-committed-geometry-or-history");
}

void ReplacedDocumentCannotBeAbortedOver()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "foreign-document-opening-prepared-and-published");
    if (!prepared)
        return;
    Guid replacementActive;
    std::optional<Guid> replacementAlternate;
    auto replacement = MakeDocument(replacementActive, replacementAlternate);
    replacement.selection = SquareSelection(kAfterFirstMove);
    fixture.session->ReplaceDocument(std::move(replacement), replacementActive);
    const auto afterReplacement = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-after-foreign-document-replacement");
    Expect(Matches(*fixture.session, afterReplacement) && HasLiteral(fixture.session->Selection(), kAfterFirstMove),
           "foreign-document-refusal-preserves-replacement-document-and-active-layer");
}

void GenerationChangeCannotBeAbortedOver()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "generation-opening-prepared-and-published");
    if (!prepared)
        return;
    const auto generation = fixture.session->Generation();
    fixture.session->EditForTest([](CanvasDocument&) {});
    Expect(fixture.session->Generation() == generation + 1, "fixture-only-noop-edit-advances-only-document-generation");
    const auto afterGenerationChange = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-after-foreign-generation-change");
    Expect(Matches(*fixture.session, afterGenerationChange),
           "generation-refusal-does-not-overwrite-current-session-state");
}

void ForeignHistoryPublicationCannotBeAbortedOver()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "history-opening-prepared-and-published");
    if (!prepared)
        return;
    const auto generation = fixture.session->Generation();
    const auto historyBeforeReset = fixture.session->history.CaptureState();
    fixture.session->history.Reset();
    const auto historyAfterReset = fixture.session->history.CaptureState();
    Expect(fixture.session->Generation() == generation && !fixture.session->history.IsCurrent(historyBeforeReset) &&
               historyAfterReset.Depth() == 0 && historyAfterReset.FutureCount() == 0,
           "fixture-resets-history-without-document-generation-change");
    const auto afterHistoryChange = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-after-foreign-history-reset");
    Expect(Matches(*fixture.session, afterHistoryChange), "history-refusal-preserves-reset-history-and-document");
}

void ActiveLayerChangeCannotBeAbortedOver()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && fixture.alternateLayer && prepared && Apply(fixture, prepared),
           "active-layer-opening-prepared-and-published");
    if (!prepared || !fixture.alternateLayer)
        return;
    const auto generation = fixture.session->Generation();
    const auto revision = fixture.session->history.CurrentRevision();
    fixture.session->SelectLayer(fixture.alternateLayer);
    Expect(fixture.session->ActiveLayerID() == fixture.alternateLayer && fixture.session->Generation() == generation &&
               fixture.session->history.CurrentRevision() == revision,
           "fixture-selects-alternate-layer-without-generation-or-history-change");
    const auto afterActiveChange = Capture(*fixture.session);
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-after-foreign-active-layer-change");
    Expect(Matches(*fixture.session, afterActiveChange) && fixture.session->ActiveLayerID() == fixture.alternateLayer,
           "active-layer-refusal-preserves-foreign-selection-and-history");
}

void PendingTransformInvalidatesPreparedGesture()
{
    Fixture fixture;
    auto prepared = Prepare(fixture);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "transform-opening-prepared-and-published");
    if (!prepared || !fixture.session->IsDeferredSelectionMove())
        return;

    const auto generation = fixture.session->Generation();
    const auto history = fixture.session->history.CaptureState();
    const auto historyRevision = fixture.session->history.CurrentRevision();
    fixture.session->BeginTransform(true);
    const auto& pending = fixture.session->PendingTransformEdit();
    Expect(pending && pending->persistent && !pending->deferred,
           "persistent-transform-begins-while-selection-gesture-is-open");
    Expect(fixture.session->Generation() == generation && fixture.session->history.IsCurrent(history) &&
               fixture.session->history.CurrentRevision() == historyRevision,
           "begin-transform-changes-neither-document-generation-nor-history");
    if (!pending)
        return;

    const TransformEdit* const transformAddress = &pending.value();
    const Guid transformLayer = pending->layerID;
    const LayerTransform transformDraft = pending->draft;
    const auto afterTransform = Capture(*fixture.session);
    const auto transformUnchanged = [&fixture, transformAddress, &transformLayer, &transformDraft]
    {
        const auto& current = fixture.session->PendingTransformEdit();
        return current && &current.value() == transformAddress && current->layerID == transformLayer &&
               current->draft == transformDraft && current->persistent && !current->deferred;
    };

    Expect(!fixture.session->MovePreparedDeferredSelectionMoveIfCurrent(prepared.value(), 1.0, -1.0),
           "opaque-move-refuses-while-persistent-transform-is-pending");
    Expect(Matches(*fixture.session, afterTransform) && transformUnchanged(),
           "opaque-move-refusal-preserves-transform-document-and-history");
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "opaque-abort-refuses-while-persistent-transform-is-pending");
    Expect(Matches(*fixture.session, afterTransform) && transformUnchanged(),
           "opaque-abort-refusal-preserves-transform-document-and-history");
}
void MissingAbortOptInRefusesAbort()
{
    Fixture fixture;
    auto prepared = Prepare(fixture, false);
    Expect(fixture.ready && prepared && Apply(fixture, prepared), "default-opening-prepared-and-published");
    if (!prepared)
        return;
    const auto afterOpening = Capture(*fixture.session);
    Expect(!fixture.session->IsPreparedDeferredSelectionMoveGestureCurrent(prepared.value()),
           "default-opening-does-not-create-abort-gesture-authority");
    Expect(!fixture.session->AbortPreparedDeferredSelectionMoveIfCurrent(prepared.value()),
           "abort-refuses-without-explicit-preparation-opt-in");
    Expect(Matches(*fixture.session, afterOpening), "missing-opt-in-refusal-preserves-open-deferred-move");
}

void PreCancelledPreparationPublishesNothing()
{
    Fixture fixture;
    const auto before = Capture(*fixture.session);
    auto input = fixture.session->PrepareDeferredSelectionMoveOpeningInput();
    CancelSource cancellation;
    (void)cancellation.Cancel();
    auto prepared = input ? DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(input.value()),
                                                                                 cancellation.Token(), {}, {}, true)
                          : Prepared{};
    Expect(fixture.ready && !prepared, "pre-cancelled-opening-and-abort-preparation-refused");
    Expect(Matches(*fixture.session, before) && HasLiteral(fixture.session->Selection(), kOrigin),
           "pre-cancelled-preparation-preserves-source-generation-history-and-redo");
}
} // namespace

int RunSelectionGestureAbortTests()
{
    gAssertions = 0;
    gFailures = 0;
    AbortBeforeMoveRestoresExactSeededState();
    AbortAfterRoundedMovesRestoresOriginalGeometryAndRoot();
    InvalidOffsetsLeaveThePreparedGestureAbortable();
    DuplicateAbortIsIdempotentlyRefused();
    NormalEndConsumesAbortAuthority();
    ReplacedDocumentCannotBeAbortedOver();
    GenerationChangeCannotBeAbortedOver();
    ForeignHistoryPublicationCannotBeAbortedOver();
    ActiveLayerChangeCannotBeAbortedOver();
    PendingTransformInvalidatesPreparedGesture();
    MissingAbortOptInRefusesAbort();
    PreCancelledPreparationPublishesNothing();
    std::printf("SUMMARY selection-gesture-abort assertions=%u failures=%u\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

#include "TransformDeferredGuardTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "Executor.h"
#include "FloatingPixelsWork.h"
#include "TransformPreviewWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s transform-deferred-guard.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<Vec2, 4> Rectangle(double left, double top, double right, double bottom)
{
    return {Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}};
}

DocumentSelection Selection(std::array<Vec2, 4> points)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{points.begin(), points.end()}}};
}

bool Near(double actual, double expected) noexcept
{
    return std::abs(actual - expected) <= 1.0e-9;
}

bool SameSelection(const DocumentSelection& actual, const DocumentSelection& expected) noexcept
{
    if (actual.antialiased != expected.antialiased || actual.loops.size() != expected.loops.size())
        return false;
    for (std::size_t loopIndex = 0; loopIndex < actual.loops.size(); ++loopIndex)
    {
        const SelectionLoop& a = actual.loops[loopIndex];
        const SelectionLoop& e = expected.loops[loopIndex];
        if (a.size() != e.size())
            return false;
        for (std::size_t pointIndex = 0; pointIndex < a.size(); ++pointIndex)
            if (!Near(a[pointIndex].x, e[pointIndex].x) || !Near(a[pointIndex].y, e[pointIndex].y))
                return false;
    }
    return true;
}

LayerTransform Place(Vec2 origin, Vec2 size)
{
    LayerTransform result;
    result.origin = origin;
    result.size = size;
    result.sampling = LayerSampling::Nearest;
    return result;
}

struct Fixture final
{
    DocumentSession session;
    Guid active;
    Guid sibling;

    Fixture()
    {
        constexpr std::array<std::uint8_t, 16> imageBytes{11, 21, 31, 255, 41,  51,  61,  255,
                                                          71, 81, 91, 255, 101, 111, 121, 255};
        constexpr std::array<std::uint8_t, 4> maskBytes{255, 128, 64, 0};
        const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, imageBytes.data(), imageBytes.size());
        const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, maskBytes.data(), maskBytes.size());

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 8;
        document.selection = Selection(Rectangle(1, 1, 3, 3));

        ImageLayer first = ImageLayer::Blank(L"Deferred target", Vec2{2, 2});
        first.transform = Place({0, 0}, {2, 2});
        first.asset = RasterAsset{image, image, first.name};
        active = first.id;

        ImageLayer second = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
        second.transform = Place({3, 0}, {2, 2});
        second.asset = RasterAsset{image, image, second.name};
        second.mask = LayerMask{RasterAsset{mask, mask, L"Sibling mask"}, true, std::nullopt, true};
        sibling = second.id;
        document.layers = {first, second};

        session.ReplaceDocument(std::move(document).Seal(), active);
        static_cast<void>(session.CopySelection());
        session.SelectLayerTarget(active, false);
        static_cast<void>(session.BeginDeferredTransform());
        if (session.PendingTransformEdit())
        {
            LayerTransform draft = session.PendingTransformEdit()->draft;
            draft.origin = {1, 2};
            session.PreviewTransform(draft);
        }
    }
};

struct Snapshot final
{
    CanvasDocument document;
    DocumentHistory::StateView history;
    int undoCount;
    int historyDepth;
    std::size_t historyFutureCount;
    std::uint64_t generation;
    std::uint64_t documentSerial;
    std::uint64_t clipboardSerial;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    bool maskSelected;
    std::unordered_set<Guid> selected;
    std::optional<TransformEdit> edit;

    explicit Snapshot(DocumentSession& session)
        : document(session.Document().value()), history(session.history.CaptureState()),
          undoCount(session.history.UndoCount()), historyDepth(history.Depth()),
          historyFutureCount(history.FutureCount()), generation(session.Generation()),
          documentSerial(session.DocumentSerial()), clipboardSerial(session.ClipboardSerial()),
          clipboard(session.Clipboard()), active(session.ActiveLayerID()), maskSelected(session.IsMaskSelected()),
          selected(session.SelectedLayerIDs()), edit(session.PendingTransformEdit())
    {
    }

    [[nodiscard]] bool Matches(DocumentSession& session) const
    {
        const auto& now = session.PendingTransformEdit();
        const bool sameEdit =
            edit.has_value() == now.has_value() &&
            (!edit || (edit->layerID == now->layerID && edit->draft == now->draft && edit->corners == now->corners &&
                       edit->mask == now->mask && edit->deferred == now->deferred && edit->opening == now->opening &&
                       edit->persistent == now->persistent));
        const bool sameClipboard = clipboard.has_value() == session.Clipboard().has_value() &&
                                   (!clipboard || (clipboard->image == session.Clipboard()->image &&
                                                   clipboard->origin == session.Clipboard()->origin));
        return session.Document().has_value() && session.Document().value() == document &&
               session.history.IsCurrent(history) && session.history.UndoCount() == undoCount &&
               session.history.CaptureState().Depth() == historyDepth &&
               session.history.CaptureState().FutureCount() == historyFutureCount &&
               session.Generation() == generation && session.DocumentSerial() == documentSerial &&
               session.ClipboardSerial() == clipboardSerial && sameClipboard && session.ActiveLayerID() == active &&
               session.IsMaskSelected() == maskSelected && session.SelectedLayerIDs() == selected && sameEdit;
    }
};

template <typename Action> void RefusedWithoutMutation(const char* name, Action&& action)
{
    Fixture fixture;
    Expect(fixture.session.PendingTransformEdit() && fixture.session.PendingTransformEdit()->deferred,
           "fixture-has-deferred-draft");
    const Snapshot before(fixture.session);
    std::forward<Action>(action)(fixture);
    Expect(before.Matches(fixture.session), name);
}

void DirectReferenceRoutesAreGuarded()
{
    RefusedWithoutMutation("select-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.SelectLayer(f.sibling); });
    RefusedWithoutMutation("select-mask-target-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.SelectLayerTarget(f.sibling, true); });
    RefusedWithoutMutation("create-document-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.CreateDocument(5, 6); });
    RefusedWithoutMutation("clear-document-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.ClearDocument(); });
    RefusedWithoutMutation("replace-document-same-id-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               CanvasDocument same = f.session.Document().value();
                               f.session.ReplaceDocument(std::move(same), f.active);
                           });
    RefusedWithoutMutation("apply-size-same-document-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               CanvasDocument same = f.session.Document().value();
                               f.session.ApplyDocumentSize(std::move(same), L"Canvas Size");
                           });
    RefusedWithoutMutation("apply-size-token-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               auto token = f.session.CaptureJobToken();
                               Expect(token.has_value(), "apply-size-job-token-is-available");
                               if (token)
                                   Expect(!f.session.ApplyDocumentSizeIfCurrent(f.session.Document().value(),
                                                                                L"Canvas Size", token.value()),
                                          "apply-size-token-reports-refusal");
                           });
    RefusedWithoutMutation("apply-import-same-id-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               CanvasDocument same = f.session.Document().value();
                               Expect(!f.session.ApplyImportedDocument(std::move(same), f.active, L"Import"),
                                      "apply-import-same-id-reports-refusal");
                           });
}

void AppearanceAndStructureRoutesAreGuarded()
{
    RefusedWithoutMutation("rename-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.RenameLayer(f.active, L"Changed"); });
    RefusedWithoutMutation("toggle-visibility-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.ToggleVisibility(f.active); });
    RefusedWithoutMutation("toggle-mask-link-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.ToggleMaskLink(f.sibling); });
    RefusedWithoutMutation("link-mask-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               Expect(f.session.CanLinkMask(f.active, f.sibling), "link-mask-fixture-is-eligible");
                               Expect(!f.session.LinkMask(f.active, f.sibling), "link-mask-reports-refusal");
                           });
    RefusedWithoutMutation(
        "place-layer-leaves-entire-owner-snapshot", [](Fixture& f)
        { Expect(!f.session.PlaceLayer(f.active, std::nullopt, f.sibling), "place-layer-reports-refusal"); });
    RefusedWithoutMutation("toggle-clipping-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               Expect(f.session.CanToggleClippingMask(f.sibling), "clipping-fixture-is-eligible");
                               f.session.ToggleClippingMask(f.sibling);
                           });
    RefusedWithoutMutation("move-active-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.MoveActiveLayer(1); });
    RefusedWithoutMutation("add-blank-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.AddBlankLayer(); });
    RefusedWithoutMutation("add-group-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.AddGroup(); });
    RefusedWithoutMutation("group-selected-layers-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.GroupSelectedLayers(); });
    RefusedWithoutMutation("duplicate-active-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.DuplicateActiveLayer(); });
    RefusedWithoutMutation("delete-layer-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.DeleteLayer(f.active); });
    RefusedWithoutMutation("flip-layers-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.FlipLayers(true); });
    RefusedWithoutMutation("add-mask-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.AddMask(false); });
    RefusedWithoutMutation("set-opacity-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.SetOpacity(0.25); });
    RefusedWithoutMutation("set-blend-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.SetBlendMode(LayerBlendMode::Multiply); });
}

void ClipboardHistoryAndNestedEditRoutesAreGuarded()
{
    RefusedWithoutMutation("copy-selection-leaves-entire-owner-snapshot",
                           [](Fixture& f) { Expect(!f.session.CopySelection(), "copy-selection-reports-refusal"); });
    RefusedWithoutMutation("copy-merged-selection-leaves-entire-owner-snapshot",
                           [](Fixture& f)
                           {
                               constexpr std::array<std::uint8_t, 256> pixels{};
                               const Raster merged =
                                   Raster::Make(8, 8, RasterFormat::Bgra8888, pixels.data(), pixels.size());
                               Expect(!f.session.CopyMergedSelection(merged), "copy-merged-selection-reports-refusal");
                           });
    RefusedWithoutMutation("cut-selection-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.CutSelection(); });
    RefusedWithoutMutation("paste-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.Paste(); });
    RefusedWithoutMutation("layer-via-copy-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.LayerViaCopy(); });
    RefusedWithoutMutation("undo-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.Undo(); });
    RefusedWithoutMutation("redo-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.Redo(); });
    RefusedWithoutMutation("begin-edit-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.BeginEdit(L"Direct edit"); });
    RefusedWithoutMutation("end-edit-leaves-entire-owner-snapshot", [](Fixture& f) { f.session.EndEdit(); });
    RefusedWithoutMutation("begin-color-edit-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.BeginColorEdit(FilterKind::Exposure); });
    RefusedWithoutMutation("begin-selection-transform-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.BeginSelectionTransform(); });
    RefusedWithoutMutation("begin-duplicate-transform-leaves-entire-owner-snapshot",
                           [](Fixture& f) { f.session.BeginDuplicateTransform(); });
}

TransformEdit FloatingEdit(LayerTransform draft)
{
    TransformEdit edit;
    edit.layerID = Guid::Generate();
    edit.draft = draft;
    edit.floating = FloatingTransform{
        Guid::Generate(), std::make_shared<CanvasDocument>(), std::nullopt, Place({0, 0}, {4, 4}), 4, 4};
    return edit;
}

void CheckOutline(const char* name, const std::array<Vec2, 4>& expected, TransformEdit edit)
{
    DocumentSelection actual = Selection(Rectangle(1, 1, 3, 3));
    const bool ready = PrepareFloatingSelectionOutline(actual, edit);
    const DocumentSelection literal = Selection(expected);
    Expect(ready && SameSelection(actual, literal), name);
}

void LiteralAffineAndFlipOutlines()
{
    CheckOutline("outline-affine-translation-scale-literal", {Vec2{12, 23}, Vec2{16, 23}, Vec2{16, 29}, Vec2{12, 29}},
                 FloatingEdit(Place({10, 20}, {8, 12})));

    LayerTransform quarterTurn = Place({10, 20}, {4, 4});
    quarterTurn.rotation = 90;
    CheckOutline("outline-affine-quarter-turn-literal", {Vec2{13, 21}, Vec2{13, 23}, Vec2{11, 23}, Vec2{11, 21}},
                 FloatingEdit(quarterTurn));

    LayerTransform horizontal = Place({10, 20}, {8, 8});
    horizontal.flipX = true;
    CheckOutline("outline-affine-horizontal-flip-literal", {Vec2{16, 22}, Vec2{12, 22}, Vec2{12, 26}, Vec2{16, 26}},
                 FloatingEdit(horizontal));

    LayerTransform vertical = Place({10, 20}, {8, 8});
    vertical.flipY = true;
    CheckOutline("outline-affine-vertical-flip-literal", {Vec2{12, 26}, Vec2{16, 26}, Vec2{16, 22}, Vec2{12, 22}},
                 FloatingEdit(vertical));

    LayerTransform both = Place({10, 20}, {8, 8});
    both.flipX = true;
    both.flipY = true;
    CheckOutline("outline-affine-double-flip-literal", {Vec2{16, 26}, Vec2{12, 26}, Vec2{12, 22}, Vec2{16, 22}},
                 FloatingEdit(both));
}

void LiteralProjectiveOutlines()
{
    // Independent geometry: parallel horizontal edges with bottom/top widths 3/4 give denominator
    // 1 + v/3, x = 10 + 4u/(1 + v/3), y = 20 + (16v/3)/(1 + v/3).
    TransformEdit trapezoid = FloatingEdit(Place({10, 20}, {4, 4}));
    trapezoid.corners = std::array<Vec2, 4>{Vec2{10, 20}, Vec2{14, 20}, Vec2{13, 24}, Vec2{10, 24}};
    CheckOutline("outline-projective-trapezoid-literal",
                 {Vec2{10.923076923076923, 21.23076923076923}, Vec2{12.76923076923077, 21.23076923076923},
                  Vec2{12.4, 23.2}, Vec2{10.8, 23.2}},
                 trapezoid);

    TransformEdit flipped = FloatingEdit(Place({10, 20}, {4, 4}));
    flipped.draft.flipX = true;
    flipped.corners = std::array<Vec2, 4>{Vec2{10, 20}, Vec2{14, 20}, Vec2{13, 24}, Vec2{10, 24}};
    CheckOutline("outline-projective-horizontal-flip-literal",
                 {Vec2{12.76923076923077, 21.23076923076923}, Vec2{10.923076923076923, 21.23076923076923},
                  Vec2{10.8, 23.2}, Vec2{12.4, 23.2}},
                 flipped);

    TransformEdit parallelogram = FloatingEdit(Place({4, 6}, {8, 8}));
    parallelogram.corners = std::array<Vec2, 4>{Vec2{4, 6}, Vec2{12, 6}, Vec2{14, 14}, Vec2{6, 14}};
    CheckOutline("outline-projective-parallelogram-literal",
                 {Vec2{6.5, 8}, Vec2{10.5, 8}, Vec2{11.5, 12}, Vec2{7.5, 12}}, parallelogram);

    TransformEdit asymmetric = FloatingEdit(Place({-3, 5}, {6, 4}));
    // Widths 6 and 8 give denominator 1 - v/4, x = -3 + 6u/(1 - v/4), y = 5 + 3v/(1 - v/4).
    asymmetric.corners = std::array<Vec2, 4>{Vec2{-3, 5}, Vec2{3, 5}, Vec2{5, 9}, Vec2{-3, 9}};
    CheckOutline("outline-projective-asymmetric-literal",
                 {Vec2{-1.4, 5.8}, Vec2{1.8, 5.8}, Vec2{2.5384615384615383, 7.769230769230769},
                  Vec2{-1.1538461538461537, 7.769230769230769}},
                 asymmetric);

    TransformEdit secondFlip = FloatingEdit(Place({10, 20}, {4, 4}));
    secondFlip.draft.flipY = true;
    secondFlip.corners = std::array<Vec2, 4>{Vec2{10, 20}, Vec2{14, 20}, Vec2{13, 24}, Vec2{10, 24}};
    CheckOutline("outline-projective-vertical-flip-literal",
                 {Vec2{10.8, 23.2}, Vec2{12.4, 23.2}, Vec2{12.76923076923077, 21.23076923076923},
                  Vec2{10.923076923076923, 21.23076923076923}},
                 secondFlip);
}

void OutlineFailureIsTransactional()
{
    const DocumentSelection original = Selection(Rectangle(1, 1, 3, 3));
    DocumentSelection cancelled = original;
    CancelSource beforeStart;
    static_cast<void>(beforeStart.Cancel());
    TransformEdit malformed = FloatingEdit(Place({2, 3}, {4, 4}));
    malformed.floating->pixelWidth = 0;
    Expect(!PrepareFloatingSelectionOutline(cancelled, malformed, beforeStart.Token()) && cancelled == original,
           "outline-pre-cancel-and-invalid-pixel-dimension-refuses-without-partial-write");

    DocumentSelection invalid = original;
    TransformEdit badSize = FloatingEdit(Place({2, 3}, {4, 4}));
    badSize.floating->pixelHeight = -1;
    Expect(!PrepareFloatingSelectionOutline(invalid, badSize) && invalid == original,
           "outline-invalid-pixel-dimension-refuses-transactionally");

    DocumentSelection noFloat = original;
    TransformEdit ordinary = FloatingEdit(Place({2, 3}, {4, 4}));
    ordinary.floating.reset();
    Expect(!PrepareFloatingSelectionOutline(noFloat, ordinary) && noFloat == original,
           "outline-ordinary-transform-refuses-transactionally");

    DocumentSelection badProjective = original;
    TransformEdit malformedCorners = FloatingEdit(Place({2, 3}, {4, 4}));
    malformedCorners.corners = std::array<Vec2, 4>{Vec2{0, 0}, Vec2{4, 4}, Vec2{0, 4}, Vec2{4, 0}};
    Expect(!PrepareFloatingSelectionOutline(badProjective, malformedCorners) && badProjective == original,
           "outline-invalid-projective-geometry-refuses-transactionally");

    DocumentSelection invalidOriginal = original;
    TransformEdit badOriginal = FloatingEdit(Place({2, 3}, {4, 4}));
    badOriginal.floating->original.size.x = 0;
    Expect(!PrepareFloatingSelectionOutline(invalidOriginal, badOriginal) && invalidOriginal == original,
           "outline-invalid-original-placement-refuses-transactionally");

    DocumentSelection invalidDraft = original;
    TransformEdit badDraft = FloatingEdit(Place({2, 3}, {4, 4}));
    badDraft.draft.size.y = 0;
    Expect(!PrepareFloatingSelectionOutline(invalidDraft, badDraft) && invalidDraft == original,
           "outline-invalid-draft-placement-refuses-transactionally");
}

struct SelectionStageGate final
{
    CancelSource cancellation;
    bool reached = false;
};

void CancelAtSelectionStage(void* context, TransformPreviewStage stage)
{
    auto& gate = *static_cast<SelectionStageGate*>(context);
    if (stage == TransformPreviewStage::Selection)
    {
        gate.reached = true;
        static_cast<void>(gate.cancellation.Cancel());
    }
}

void PreviewMapsOutlineAtomicallyAndHonorsSelectionCheckpoint()
{
    constexpr std::array<std::uint8_t, 64> pixels{};
    const Raster image = Raster::Make(4, 4, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    ImageLayer base = ImageLayer::Blank(L"source", Vec2{4, 4});
    base.transform = Place({0, 0}, {4, 4});
    base.asset = RasterAsset{image, image, base.name};
    ImageLayer layer = ImageLayer::Blank(L"floating", Vec2{4, 4});
    layer.transform = Place({0, 0}, {4, 4});
    layer.asset = RasterAsset{image, image, layer.name};
    DocumentFixture sourceFixture;
    sourceFixture.id = Guid::Generate();
    sourceFixture.width = 8;
    sourceFixture.height = 8;
    sourceFixture.layers = {base, layer};
    sourceFixture.selection = Selection(Rectangle(1, 1, 3, 3));
    CanvasDocument source = std::move(sourceFixture).Seal();

    TransformEdit edit = FloatingEdit(Place({10, 20}, {8, 12}));
    edit.layerID = layer.id;
    edit.floating->sourceID = base.id;
    auto before = DocumentFixture::Copy(source);
    before.layers = {base};
    edit.floating->before = std::make_shared<CanvasDocument>(std::move(before).Seal());
    edit.floating->beforeActive = base.id;
    edit.floating->original = Place({0, 0}, {4, 4});
    edit.floating->pixelWidth = 4;
    edit.floating->pixelHeight = 4;
    InlineExecutor executor;
    const auto ready = PrepareTransformDisplay(source, edit, executor);
    const DocumentSelection expected = Selection(Rectangle(12, 23, 16, 29));
    Expect(ready.status == DistortWorkStatus::Ready && ready.document && ready.document->selection &&
               SameSelection(ready.document->selection.value(), expected) &&
               source.selection == Selection(Rectangle(1, 1, 3, 3)),
           "preview-publishes-complete-mapped-outline-and-keeps-source-selection");

    TransformEdit distorted = edit;
    distorted.draft.flipX = true;
    distorted.corners = std::array<Vec2, 4>{Vec2{10, 20}, Vec2{14, 20}, Vec2{13, 24}, Vec2{10, 24}};
    const auto warped = PrepareTransformDisplay(source, distorted, executor);
    const DocumentSelection warpedExpected =
        Selection({Vec2{12.76923076923077, 21.23076923076923}, Vec2{10.923076923076923, 21.23076923076923},
                   Vec2{10.8, 23.2}, Vec2{12.4, 23.2}});
    Expect(warped.status == DistortWorkStatus::Ready && warped.document && warped.document->selection &&
               SameSelection(warped.document->selection.value(), warpedExpected) &&
               source.selection == Selection(Rectangle(1, 1, 3, 3)),
           "preview-publishes-complete-projective-flipped-outline");

    SelectionStageGate gate;
    const auto cancelled = PrepareTransformDisplay(source, distorted, executor, gate.cancellation.Token(),
                                                   TransformPreviewObserver{&gate, CancelAtSelectionStage});
    Expect(gate.reached && cancelled.status == DistortWorkStatus::Cancelled && !cancelled.document &&
               source.selection == Selection(Rectangle(1, 1, 3, 3)),
           "preview-selection-checkpoint-cancellation-publishes-no-partial-document");
}
} // namespace

int RunTransformDeferredGuardTests()
{
    gFailures = 0;
    gAssertions = 0;
    DirectReferenceRoutesAreGuarded();
    AppearanceAndStructureRoutesAreGuarded();
    ClipboardHistoryAndNestedEditRoutesAreGuarded();
    LiteralAffineAndFlipOutlines();
    LiteralProjectiveOutlines();
    OutlineFailureIsTransactional();
    PreviewMapsOutlineAtomicallyAndHonorsSelectionCheckpoint();
    std::printf("TRANSFORM DEFERRED GUARD TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

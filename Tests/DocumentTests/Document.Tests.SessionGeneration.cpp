#include "Document.Tests.SessionGeneration.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s generation.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Pixels(std::uint8_t value = 64)
{
    std::array<std::uint8_t, 8 * 8 * 4> bytes;
    for (std::size_t i = 0; i < bytes.size(); i += 4)
    {
        bytes[i] = value;
        bytes[i + 1] = value;
        bytes[i + 2] = value;
        bytes[i + 3] = 255;
    }
    return Raster::Make(8, 8, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

DocumentSession Session()
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 16;
    document.height = 16;
    for (const wchar_t* name : {L"Bottom", L"Top"})
    {
        ImageLayer layer = ImageLayer::Blank(name, Vec2{8, 8});
        const Raster image = Pixels();
        layer.asset = RasterAsset{image, image, name};
        document.layers.push_back(std::move(layer));
    }
    const Guid active = document.layers.back().id;
    DocumentSession session;
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

void SelectRect(DocumentSession& session)
{
    session.ApplySelection(SelectionGeometry::Rect(1, 1, 4, 4), SelectionMode::New, L"Select");
}

// A mutation must change observable document state and invalidate a whole-document snapshot immediately,
// including while the outer history entry remains open. These checks do not assert an implementation count.
template <typename Edit> void Changes(DocumentSession& session, const char* name, Edit&& edit)
{
    const auto before = session.Document();
    const std::uint64_t generation = session.Generation();
    const auto token = session.CaptureJobToken();
    edit();
    Expect(session.Document() != before && session.Generation() > generation && token.has_value() &&
               !session.CanApplyJobResult(token.value(), 0),
           name);
}

template <typename Edit> void FreshChange(const char* name, Edit&& edit)
{
    DocumentSession session = Session();
    Changes(session, name, [&] { edit(session); });
}

void Mutations()
{
    FreshChange("rename", [](auto& s) { s.RenameLayer(s.ActiveLayerID().value(), L"Renamed"); });
    FreshChange("visibility", [](auto& s) { s.ToggleVisibility(s.ActiveLayerID().value()); });
    FreshChange("opacity", [](auto& s) { s.SetOpacity(0.5); });
    FreshChange("blend", [](auto& s) { s.SetBlendMode(LayerBlendMode::Multiply); });
    FreshChange("new-layer", [](auto& s) { s.AddBlankLayer(); });
    FreshChange("new-group", [](auto& s) { s.AddGroup(); });
    FreshChange("group-selected", [](auto& s) { s.GroupSelectedLayers(); });
    FreshChange("reorder", [](auto& s) { s.MoveActiveLayer(-1); });
    FreshChange("place",
                [](auto& s) { (void)s.PlaceLayer(s.ActiveLayerID().value(), std::nullopt, std::nullopt, true); });
    FreshChange("clipping", [](auto& s) { s.ToggleClippingMask(s.ActiveLayerID().value()); });
    FreshChange("link", [](auto& s) { (void)s.LinkMask(s.Document()->layers.front().id, s.ActiveLayerID().value()); });
    FreshChange("delete", [](auto& s) { s.DeleteLayer(s.ActiveLayerID().value()); });
    FreshChange("duplicate", [](auto& s) { s.DuplicateActiveLayer(); });
    FreshChange("flip-canvas", [](auto& s) { s.FlipCanvas(true); });
    FreshChange("flip-layer", [](auto& s) { s.FlipLayers(true); });
    FreshChange("nudge-layer", [](auto& s) { s.NudgeLayer(1, 0); });
    FreshChange("mask-add", [](auto& s) { s.AddLayerMask(false); });
    FreshChange("invert", [](auto& s) { s.InvertPixels(); });
    FreshChange("fill", [](auto& s) { (void)s.FillSelection(FillSource::Foreground); });
    FreshChange("paste", [](auto& s) { s.PasteImage(Pixels(127)); });
    FreshChange("selection", [](auto& s) { SelectRect(s); });
    FreshChange("adjustment-add", [](auto& s) { s.AddAdjustment(AdjustmentKind::Exposure); });
    FreshChange("size",
                [](auto& s)
                {
                    CanvasDocument d = s.Document().value();
                    d.width = 20;
                    s.ApplyDocumentSize(std::move(d), L"Size");
                });
    FreshChange("crop",
                [](auto& s)
                {
                    s.SetCropFrame(CropRect{1, 1, 12, 12});
                    (void)s.CommitCrop();
                });
    FreshChange("import",
                [](auto& s)
                {
                    DocumentFixture d = DocumentFixture::Copy(s.Document().value());
                    ImageLayer layer = ImageLayer::Blank(L"Import", Vec2{2, 2});
                    const Guid id = layer.id;
                    d.layers.push_back(std::move(layer));
                    Expect(s.ApplyImportedDocument(std::move(d).Seal(), id, L"Import"), "import-accepted");
                });
    FreshChange("fixture", [](auto& s) { s.EditForTest([](CanvasDocument& d) { d.resolution = 144; }); });
    FreshChange("brush-commit",
                [](auto& s)
                {
                    s.brushSettings = BrushSettings{2, 1, 1, 0, 0, 1};
                    Expect(s.BeginBrush(Vec2{3, 3}), "brush-begin");
                    s.FinishBrush();
                });
    FreshChange("shape",
                [](auto& s)
                {
                    s.BeginShape(Vec2{2, 2});
                    s.DragShape(Vec2{6, 6}, false, false);
                    s.FinishShape();
                });
    FreshChange("filter-commit",
                [](auto& s)
                {
                    s.BeginColorEdit(FilterKind::Exposure);
                    FilterSettings settings;
                    settings.exposure.exposure = 1;
                    s.UpdateColorEdit(settings, true);
                    s.CommitColorEdit();
                });
    FreshChange("gradient-commit",
                [](auto& s)
                {
                    Expect(s.BeginGradient(Vec2{1, 1}), "gradient-begin");
                    s.MoveGradient(Vec2{1, 1}, Vec2{6, 6});
                    s.EndGradientDrag();
                    s.CommitGradient();
                });
    FreshChange("merge",
                [](auto& s)
                {
                    std::array<std::uint8_t, 16 * 16 * 4> bytes;
                    bytes.fill(255);
                    const Raster composite = Raster::Make(16, 16, RasterFormat::Bgra8888, bytes.data(), bytes.size());
                    const auto plan = s.PlanMerge();
                    Expect(plan.has_value() && s.MergeLayers(plan.value(), composite), "merge-accepted");
                });
    {
        DocumentSession s = Session();
        s.BeginEdit(L"Outer");
        const Guid revision = s.history.CurrentRevision();
        Changes(s, "nested-edit-invalidates-before-outer-end", [&] { s.SetOpacity(0.75); });
        Expect(s.history.CurrentRevision() == revision, "nested-history-still-open");
        s.EndEdit();
        Changes(s, "undo", [&] { s.Undo(); });
        Changes(s, "redo", [&] { s.Redo(); });
        const int count = s.history.UndoCount();
        Changes(s, "opacity-preview", [&] { (void)s.PreviewLayerOpacity(s.ActiveLayerID().value(), 0.25); });
        Expect(s.history.UndoCount() == count, "opacity-preview-no-history");
    }
    {
        DocumentSession s = Session();
        SelectRect(s);
        Expect(s.BeginSelectionMove(), "move-begin");
        const Guid revision = s.history.CurrentRevision();
        Changes(s, "selection-move-open-entry", [&] { s.MoveSelection(2, 0); });
        Expect(s.history.CurrentRevision() == revision, "selection-history-still-open");
        Changes(s, "selection-cancel-restores", [&] { s.CancelSelectionMove(); });
        Changes(s, "floating-insertion-open-entry", [&] { s.BeginSelectionTransform(); });
        Changes(s, "floating-cancel-restores", [&] { s.CancelTransform(); });
        Changes(s, "mask-from-selection", [&] { s.AddMask(false); });
        Changes(s, "mask-enabled", [&] { s.ToggleLayerMask(s.ActiveLayerID().value()); });
        Changes(s, "mask-linked", [&] { s.ToggleMaskLink(s.ActiveLayerID().value()); });
        Changes(s, "mask-delete", [&] { s.DeleteLayerMask(); });
    }
    {
        DocumentSession s = Session();
        SelectRect(s);
        Changes(s, "selection-expand", [&] { s.ExpandSelection(1); });
        Changes(s, "selection-contract", [&] { s.ContractSelection(1); });
        Changes(s, "selection-invert", [&] { s.InvertSelection(); });
        Changes(s, "selection-all", [&] { s.SelectAll(); });
        Changes(s, "selection-deselect", [&] { s.Deselect(); });
        SelectRect(s);
        Changes(s, "pixel-move", [&] { s.NudgePixels(2, 0); });
        Changes(s, "clear-pixels", [&] { (void)s.ClearSelectedPixels(); });
    }
    {
        DocumentSession s = Session();
        s.AddGroup();
        const Guid group = s.ActiveLayerID().value();
        s.SelectLayer(s.Document()->layers.front().id);
        Changes(s, "place-in-group", [&] { (void)s.PlaceLayer(s.ActiveLayerID().value(), group); });
        Changes(s, "move-out-of-group", [&] { s.MoveActiveLayerOutOfGroup(); });
    }
    {
        DocumentSession s = Session();
        s.AddAdjustment(AdjustmentKind::Exposure);
        Expect(s.BeginAdjustmentEditing(s.ActiveLayerID().value(), Pixels()), "adjustment-edit-begin");
        const Guid revision = s.history.CurrentRevision();
        FilterSettings settings;
        settings.exposure.exposure = 1;
        Changes(s, "live-adjustment-open-entry", [&] { s.UpdateColorEdit(settings, true); });
        Expect(s.history.CurrentRevision() == revision, "adjustment-history-still-open");
        Changes(s, "live-adjustment-cancel", [&] { s.CancelColorEdit(); });
    }
    {
        DocumentSession s = Session();
        Changes(s, "duplicate-transform-open-entry", [&] { s.BeginDuplicateTransform(); });
        Changes(s, "duplicate-transform-cancel", [&] { s.CancelTransform(); });
        Changes(s, "new-canvas", [&] { s.CreateDocument(20, 10); });
        Changes(s, "clear", [&] { s.ClearDocument(); });
        Expect(!s.CaptureJobToken().has_value(), "empty-session-no-token");
    }
}

void Tokens()
{
    DocumentSession s = Session();
    {
        const auto ownerToken = s.CaptureJobToken().value();
        DocumentSession other = Session();
        Expect(!other.CanApplyJobResult(ownerToken, 0), "different-session-refused");
        DocumentSession copied = s;
        Expect(!copied.CanApplyJobResult(ownerToken, 0), "copied-session-has-new-owner");
        const auto copiedToken = copied.CaptureJobToken().value();
        DocumentSession moved = std::move(copied);
        Expect(!moved.CanApplyJobResult(copiedToken, 0) && !copied.CanApplyJobResult(copiedToken, 0),
               "move-retires-both-owner-tokens");
        const auto movedToken = moved.CaptureJobToken().value();
        other = moved;
        Expect(!other.CanApplyJobResult(movedToken, 0), "copy-assignment-has-new-owner");
        other = std::move(moved);
        Expect(!other.CanApplyJobResult(movedToken, 0) && !moved.CanApplyJobResult(movedToken, 0),
               "move-assignment-retires-both-owner-tokens");
        Expect(s.CanApplyJobResult(ownerToken, 0), "copy-leaves-original-token-valid");
    }
    const Guid id = s.ActiveLayerID().value();
    const JobToken layer = s.CaptureJobToken(id, 7).value();
    Expect(s.CanApplyJobResult(layer, 7), "layer-token-current");
    Expect(!s.CanApplyJobResult(layer, 8), "older-sequence-refused");
    Expect(!s.CaptureJobToken(Guid::Generate()).has_value(), "missing-target-no-token");
    const Raster source = s.ActiveLayer()->asset->image;
    s.RenameLayer(s.Document()->layers.front().id, L"Unrelated");
    Expect(s.CanApplyJobResult(layer, 7), "unrelated-edit-keeps-layer-token");
    s.Undo();
    Expect(!s.CanApplyJobResult(layer, 7), "undo-invalidates-unchanged-target");
    s.Redo();
    Expect(!s.CanApplyJobResult(layer, 7), "undo-redo-cannot-revive-token");
    auto token = s.CaptureJobToken(id).value();
    s.NudgeLayer(1, 0);
    Expect(!s.CanApplyJobResult(token, 0), "moved-target-refused");
    token = s.CaptureJobToken(id).value();
    s.InvertPixels();
    Expect(!s.CanApplyJobResult(token, 0), "replaced-image-refused");
    Expect(layer.target->image == source && layer.target->image.Data()[0] == 64, "token-retains-source-pixels");
    s.AddLayerMask(true);
    token = s.CaptureJobToken(id).value();
    s.ToggleLayerMask(id);
    Expect(!s.CanApplyJobResult(token, 0), "mask-enablement-refused");
    token = s.CaptureJobToken(id).value();
    s.ToggleMaskLink(id);
    Expect(!s.CanApplyJobResult(token, 0), "mask-link-refused");
    token = s.CaptureJobToken(id).value();
    EditLayerRecordsForTest(s,
                            [](DocumentFixture& d)
                            {
                                LayerTransform p = d.layers.back().transform;
                                p.origin.x += 2;
                                d.layers.back().mask->placement = p;
                            });
    Expect(!s.CanApplyJobResult(token, 0), "mask-placement-refused");
    token = s.CaptureJobToken(id).value();
    EditLayerRecordsForTest(s,
                            [](DocumentFixture& d)
                            {
                                const std::uint8_t zero = 0;
                                d.layers.back().mask->asset.image = Raster::Make(1, 1, RasterFormat::Gray8, &zero, 1);
                            });
    Expect(!s.CanApplyJobResult(token, 0), "mask-image-refused");
    s.SelectLayerTarget(id, false);
    token = s.CaptureJobToken(id).value();
    SelectRect(s);
    Expect(!s.CanApplyJobResult(token, 0), "changed-selection-refused");
    token = s.CaptureJobToken(id).value();
    s.DeleteLayer(id);
    Expect(!s.CanApplyJobResult(token, 0), "deleted-target-refused");
    const JobToken whole = s.CaptureJobToken().value();
    const CanvasDocument reopened = s.Document().value();
    const auto active = s.ActiveLayerID();
    s.ReplaceDocument(reopened, active);
    Expect(s.Document()->id == reopened.id && !s.CanApplyJobResult(whole, 0), "same-id-reopen-refused");
    const auto current = s.CaptureJobToken().value();
    s.ClearDocument();
    s.ReplaceDocument(reopened, active);
    Expect(!s.CanApplyJobResult(current, 0), "clear-reopen-refused");
    {
        DocumentSession sized = Session();
        const auto layerToken = sized.CaptureJobToken(sized.ActiveLayerID()).value();
        CanvasDocument resized = sized.Document().value();
        resized.width = 18;
        sized.ApplyDocumentSize(std::move(resized), L"Size");
        Expect(!sized.CanApplyJobResult(layerToken, 0), "canvas-resize-refused");
    }
    {
        DocumentSession typed = Session();
        typed.AddBlankLayer();
        const auto emptyToken = typed.CaptureJobToken(typed.ActiveLayerID()).value();
        EditLayerRecordsForTest(typed, [](DocumentFixture& d) { d.layers.back().isGroup = true; });
        Expect(!typed.CanApplyJobResult(emptyToken, 0), "layer-kind-refused");
        typed.AddAdjustment(AdjustmentKind::Exposure);
        const auto adjustmentToken = typed.CaptureJobToken(typed.ActiveLayerID()).value();
        LayerAdjustment value = typed.ActiveLayer()->adjustment.value();
        value.exposureSettings = ExposureSettings{1, 0, 1};
        (void)typed.UpdateAdjustment(typed.ActiveLayerID().value(), value);
        Expect(!typed.CanApplyJobResult(adjustmentToken, 0), "adjustment-change-refused");
    }
}

void SelectionSharing()
{
    static_assert(std::is_nothrow_copy_constructible_v<SelectionLoops>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<DocumentSession&>().Document())>>);
    static_assert(std::is_same_v<decltype(std::declval<DocumentSession&>().ActiveLayer()), const ImageLayer*>);
    DocumentSession s = Session();
    SelectRect(s);
    const CanvasDocument snapshot = s.Document().value();
    const void* identity = snapshot.selection->loops.Identity();
    Expect(identity != nullptr && identity == s.Selection()->loops.Identity(), "snapshot-shares-selection-points");
    const auto token = s.CaptureJobToken(s.ActiveLayerID()).value();
    Expect(token.selection->loops.Identity() == identity, "token-shares-selection-points");
    s.NudgeSelection(2, 0);
    Expect(s.Selection()->loops.Identity() != identity && snapshot.selection->Bounds() == SelectionBounds{1, 1, 5, 5} &&
               s.Selection()->Bounds() == SelectionBounds{3, 1, 7, 5},
           "edit-keeps-snapshot-points");
    s.Undo();
    Expect(s.Selection()->loops.Identity() == identity, "undo-restores-shared-points");
    const auto generation = s.Generation();
    const int undo = s.history.UndoCount();
    SelectRect(s);
    Expect(s.Generation() == generation && s.history.UndoCount() == undo && s.history.CanRedo(),
           "equal-geometry-keeps-redo");
    s.ClearDocument();
    Expect(snapshot.selection->Bounds() == SelectionBounds{1, 1, 5, 5}, "snapshot-outlives-session-document");
}

void Refusals()
{
    DocumentSession s = Session();
    const auto before = s.Document();
    const auto token = s.CaptureJobToken().value();
    const auto serial = s.DocumentSerial();
    DocumentFixture invalidFixture = DocumentFixture::Copy(before.value());
    invalidFixture.layers.push_back(invalidFixture.layers.front());
    CanvasDocument invalid = std::move(invalidFixture).Seal();
    Expect(!s.ApplyImportedDocument(invalid, s.ActiveLayerID(), L"Invalid"), "invalid-import-refused");
    s.ReplaceDocument(invalid, s.ActiveLayerID());
    s.ApplyDocumentSize(invalid, L"Invalid");
    CanvasDocument different = before.value();
    different.id = Guid::Generate();
    Expect(!s.ApplyImportedDocument(std::move(different), std::nullopt, L"Different"), "wrong-document-import-refused");
    s.CreateDocument(0, 1);
    Expect(!s.PreviewLayerOpacity(s.ActiveLayerID().value(), std::numeric_limits<double>::quiet_NaN()),
           "invalid-opacity-refused");
    Expect(s.Document() == before && s.DocumentSerial() == serial && s.CanApplyJobResult(token, 0),
           "refusals-preserve-document-and-token");
}

void ReplacementAlias()
{
    DocumentSession session = Session();
    const auto selected = session.ActiveLayerID();
    const auto image = session.ActiveLayer()->asset->image;
    const auto token = session.CaptureJobToken().value();
    const auto serial = session.DocumentSerial();
    session.ReplaceDocument(session.Document().value(), session.ActiveLayerID());
    Expect(session.ActiveLayerID() == selected && session.ActiveLayer() && session.ActiveLayer()->asset &&
               session.ActiveLayer()->asset->image == image,
           "replacement-captures-aliased-active-id-before-session-clear");
    Expect(session.DocumentSerial() != serial && !session.CanApplyJobResult(token, token.sequence) &&
               session.history.UndoCount() == 0 && !session.history.IsModified(),
           "aliased-replacement-has-fresh-lifetime-and-saved-history");
}
} // namespace

int RunSessionGenerationTests()
{
    gFailures = 0;
    Mutations();
    Tokens();
    SelectionSharing();
    Refusals();
    ReplacementAlias();
    return gFailures;
}

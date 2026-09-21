#include "BrushStroke.h"
#include "Document.h"
#include "DocumentHistory.h"
#include "Selection.h"
#include "TransformEdit.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
    ++gFailures;
}

ImageLayer RasterLayer(int width, int height, const wchar_t* name)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
    const Raster raster = Raster::Make(width, height, RasterFormat::Bgra8888);
    layer.asset = RasterAsset{raster, raster, name};
    return layer;
}

[[nodiscard]] const ImageLayer* Find(const std::optional<CanvasDocument>& document, const Guid& id) noexcept
{
    if (!document.has_value())
    {
        return nullptr;
    }
    for (const ImageLayer& layer : document.value().layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

[[nodiscard]] bool InRender(const std::optional<CanvasDocument>& document, const Guid& id) noexcept
{
    if (!document.has_value())
    {
        return false;
    }
    for (const Guid& visible : RenderLayerIds(document.value()))
    {
        if (visible == id)
        {
            return true;
        }
    }
    return false;
}

void TestHierarchy()
{
    ImageLayer group = ImageLayer::Blank(L"Group", Vec2{10, 10});
    group.isGroup = true;
    ImageLayer nested = ImageLayer::Blank(L"Nested", Vec2{10, 10});
    nested.isGroup = true;
    nested.parentID = group.id;
    group.parentID = nested.id;
    Expect(ValidateHierarchy(std::vector<ImageLayer>{group, nested}) == DocumentStatus::Invalid, "hierarchy.cycle");

    ImageLayer orphan = ImageLayer::Blank(L"Group", Vec2{10, 10});
    orphan.isGroup = true;
    orphan.parentID = Guid::Generate();
    Expect(ValidateHierarchy(std::vector<ImageLayer>{orphan}) == DocumentStatus::Invalid, "hierarchy.missing-parent");

    ImageLayer raster = ImageLayer::Blank(L"Pixels", Vec2{10, 10});
    ImageLayer child = ImageLayer::Blank(L"Nested", Vec2{10, 10});
    child.isGroup = true;
    child.parentID = raster.id;
    Expect(ValidateHierarchy(std::vector<ImageLayer>{raster, child}) == DocumentStatus::Invalid,
           "hierarchy.raster-parent");

    ImageLayer folder = ImageLayer::Blank(L"Folder", Vec2{10, 10});
    folder.isGroup = true;
    folder.asset = RasterAsset{Raster::Make(4, 4, RasterFormat::Bgra8888), {}, L"bad"};
    Expect(ValidateHierarchy(std::vector<ImageLayer>{folder}) == DocumentStatus::Invalid, "hierarchy.group-with-image");
}

void TestLiveMask()
{
    ImageLayer a = RasterLayer(2, 2, L"A");
    ImageLayer b = RasterLayer(2, 2, L"B");
    a.maskSourceID = b.id;
    b.maskSourceID = a.id;
    Expect(ValidateLiveMaskGraph(std::vector<ImageLayer>{a, b}) == DocumentStatus::Invalid, "livemask.cycle");

    ImageLayer group = ImageLayer::Blank(L"Folder", Vec2{2, 2});
    group.isGroup = true;
    ImageLayer clipped = RasterLayer(2, 2, L"Clip");
    clipped.maskSourceID = group.id;
    Expect(ValidateLiveMaskGraph(std::vector<ImageLayer>{group, clipped}) == DocumentStatus::Invalid,
           "livemask.group-source");

    ImageLayer adjustment = ImageLayer::Blank(L"Hue/Saturation", Vec2{2, 2});
    adjustment.adjustment = LayerAdjustment{};
    ImageLayer target = RasterLayer(2, 2, L"Target");
    target.maskSourceID = adjustment.id;
    Expect(ValidateLiveMaskGraph(std::vector<ImageLayer>{adjustment, target}) == DocumentStatus::Invalid,
           "livemask.adjustment-source");
}

void TestValidate()
{
    CanvasDocument document;
    document.width = 20;
    document.height = 10;
    ImageLayer folder = ImageLayer::Blank(L"Folder", Vec2{20, 10});
    folder.isGroup = true;
    folder.opacity = 0.5;
    document.layers.push_back(folder);
    Expect(ValidateDocument(document) == DocumentStatus::Invalid, "validate.group-opacity");

    folder.opacity = 1;
    document.layers[0] = folder;
    Expect(ValidateDocument(document, 1) == DocumentStatus::Invalid, "validate.v1-group");
    Expect(ValidateDocument(document, 7) == DocumentStatus::Ok, "validate.v7-group");

    CanvasDocument masked;
    masked.width = 8;
    masked.height = 8;
    ImageLayer layer = RasterLayer(8, 8, L"Paint");
    LayerMask mask;
    mask.asset.image = Raster::Make(8, 8, RasterFormat::Gray8);
    mask.asset.name = L"Layer Mask";
    layer.mask = mask;
    masked.layers.push_back(layer);
    Expect(ValidateDocument(masked, 3) == DocumentStatus::Invalid, "validate.v4-mask-too-old");
    Expect(ValidateDocument(masked, 4) == DocumentStatus::Ok, "validate.v4-mask");

    CanvasDocument folderMask;
    folderMask.width = 8;
    folderMask.height = 8;
    ImageLayer group = ImageLayer::Blank(L"Folder", Vec2{8, 8});
    group.isGroup = true;
    LayerMask groupMask;
    groupMask.asset.image = Raster::Make(8, 8, RasterFormat::Gray8);
    group.mask = groupMask;
    folderMask.layers.push_back(group);
    Expect(ValidateDocument(folderMask, 5) == DocumentStatus::Invalid, "validate.v6-folder-mask-too-old");
    Expect(ValidateDocument(folderMask, 6) == DocumentStatus::Ok, "validate.v6-folder-mask");

    CanvasDocument huge;
    huge.width = 30'001;
    huge.height = 1;
    Expect(ValidateDocument(huge) == DocumentStatus::TooLarge, "validate.dimension");

    CanvasDocument adjustmentDoc;
    adjustmentDoc.width = 4;
    adjustmentDoc.height = 4;
    ImageLayer adjustment = ImageLayer::Blank(L"Levels", Vec2{4, 4});
    adjustment.isGroup = true;
    LayerAdjustment levels;
    levels.kind = AdjustmentKind::Levels;
    adjustment.adjustment = levels;
    adjustmentDoc.layers.push_back(adjustment);
    Expect(ValidateDocument(adjustmentDoc, 7) == DocumentStatus::Invalid, "validate.adjustment-on-group");

    CanvasDocument color;
    color.width = 4;
    color.height = 4;
    ImageLayer paint = RasterLayer(4, 4, L"Paint");
    paint.blendMode = LayerBlendMode::Multiply;
    color.layers.push_back(paint);
    Expect(ValidateDocument(color, 2) == DocumentStatus::Invalid, "validate.v2-blend");
    Expect(ValidateDocument(color, 3) == DocumentStatus::Ok, "validate.v3-blend");

    LayerMask colorMask;
    colorMask.asset.image = Raster::Make(4, 4, RasterFormat::Bgra8888);
    Expect(!LayerMask::IsValid(colorMask.asset.image), "mask.gray8-required");
}

void TestSessionGroups()
{
    DocumentSession session;
    session.CreateDocument(100, 100);
    session.AddGroup();
    const Guid outer = session.ActiveLayerID().value();
    session.AddGroup();
    const Guid inner = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid child = session.ActiveLayerID().value();
    Expect(session.ActiveLayer() != nullptr && session.ActiveLayer()->parentID == inner, "session.child-parent");
    Expect(!session.PlaceLayer(outer, inner), "session.reject-outer-into-inner");
    Expect(!session.PlaceLayer(inner, inner), "session.reject-self-parent");
    session.CollapsedGroupIDs().insert(outer);
    const std::vector<HierarchyEntry> collapsed = session.LayerRows();
    Expect(collapsed.size() == 1 && collapsed[0].id == outer, "session.collapse-hides-children");
    session.CollapsedGroupIDs().erase(outer);
    const std::vector<HierarchyEntry> expanded = session.LayerRows();
    Expect(expanded.size() == 3 && expanded[0].depth == 0 && expanded[1].depth == 1 && expanded[2].depth == 2,
           "session.expand-depths");
    session.SelectLayer(child);
    session.MoveActiveLayerOutOfGroup();
    const ImageLayer* moved = Find(session.Document(), child);
    Expect(moved != nullptr && moved->parentID.has_value() && moved->parentID.value() == outer, "session.move-out");
    session.Undo();
    const ImageLayer* restored = Find(session.Document(), child);
    Expect(restored != nullptr && restored->parentID == inner, "session.move-out-undo");
    session.SelectLayer(outer);
    session.DeleteLayer(outer);
    Expect(session.Document().has_value() && session.Document().value().layers.empty(), "session.delete-folder");
    session.Undo();
    Expect(session.Document().has_value() && session.Document().value().layers.size() == 3,
           "session.delete-folder-undo");
    Expect(Find(session.Document(), child) != nullptr && Find(session.Document(), child)->parentID == inner,
           "session.delete-folder-undo-parent");
}

void TestHiddenParent()
{
    DocumentSession session;
    session.CreateDocument(64, 32);
    session.AddGroup();
    const Guid group = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid child = session.ActiveLayerID().value();
    session.ToggleVisibility(group);
    Expect(session.ActiveLayer() != nullptr && session.ActiveLayer()->visible, "session.child-still-flagged-visible");
    Expect(!InRender(session.Document(), child), "session.hidden-parent-render");
    session.ToggleVisibility(group);
    Expect(InRender(session.Document(), child), "session.shown-parent-render");
}

void TestAppearance()
{
    DocumentSession session;
    session.CreateDocument(4, 4);
    session.AddBlankLayer();
    session.SetBlendMode(LayerBlendMode::Multiply);
    Expect(session.ActiveLayer() != nullptr && session.ActiveLayer()->blendMode == LayerBlendMode::Multiply,
           "appearance.blend");
    session.AddGroup();
    session.SetOpacity(0.5);
    Expect(session.ActiveLayer() != nullptr && session.ActiveLayer()->opacity == 1, "appearance.group-skips-opacity");
}

void TestClippingLink()
{
    DocumentSession session;
    session.CreateDocument(2, 2);
    session.AddBlankLayer();
    const Guid source = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid target = session.ActiveLayerID().value();
    Expect(session.LinkMask(source, target), "livemask.link");
    Expect(Find(session.Document(), target) != nullptr && Find(session.Document(), target)->maskSourceID == source,
           "livemask.linked");
    Expect(!session.LinkMask(target, source), "livemask.reject-cycle");
    session.Undo();
    Expect(Find(session.Document(), target) != nullptr && !Find(session.Document(), target)->maskSourceID.has_value(),
           "livemask.undo");
    session.Redo();
    Expect(Find(session.Document(), target) != nullptr && Find(session.Document(), target)->maskSourceID == source,
           "livemask.redo");
}

void TestHistory()
{
    DocumentSession session;
    session.CreateDocument(800, 600);
    session.AddBlankLayer();
    const Guid id = session.ActiveLayerID().value();
    session.history.MarkSaved();
    Expect(!session.history.IsModified(), "history.saved");
    session.RenameLayer(id, L"Changed");
    Expect(session.history.IsModified(), "history.modified");
    session.Undo();
    Expect(!session.history.IsModified(), "history.undo-to-saved");
    const int count = session.history.UndoCount();
    session.RenameLayer(id, L"Layer 1");
    session.RenameLayer(id, L"   ");
    Expect(session.history.UndoCount() == count, "history.noop-preserves-count");
    Expect(session.history.CanRedo(), "history.noop-preserves-redo");
    session.Redo();
    Expect(session.history.IsModified(), "history.redo-modified");
    session.Undo();
    session.AddBlankLayer();
    Expect(!session.history.CanRedo(), "history.edit-clears-redo");

    DocumentSession nested;
    nested.CreateDocument(100, 200);
    nested.BeginEdit(L"Layer Setup");
    nested.AddBlankLayer();
    nested.AddBlankLayer();
    Expect(!nested.history.CanUndo(), "history.nested-blocks-undo");
    nested.EndEdit();
    Expect(nested.history.UndoName() == L"Layer Setup", "history.nested-name");
    nested.Undo();
    Expect(nested.Document().has_value() && nested.Document().value().layers.empty(), "history.nested-undo");
}

void TestHistoryBounds()
{
    Expect(DocumentHistory{}.EntryLimit() == kHistoryEntryLimit, "history.default-entries");
    Expect(DocumentHistory{}.RetainedByteLimit() == kHistoryRetainedByteLimit, "history.default-256mib");

    DocumentHistory limited(2, 0);
    CanvasDocument doc;
    doc.width = 64;
    doc.height = 32;
    ImageLayer layer = RasterLayer(8, 8, L"Shared");
    doc.layers.push_back(layer);
    const Guid id = layer.id;
    for (const wchar_t* name : {L"A", L"B", L"C"})
    {
        limited.Begin(L"Rename", doc, id);
        doc.layers[0].name = name;
        limited.End(doc, id);
    }
    Expect(limited.UndoCount() == 2, "history.entry-limit");
    Expect(limited.RetainedBytes(doc) == 0, "history.shared-raster-zero-retained");
    limited.Begin(L"Delete", doc, id);
    doc.layers.clear();
    limited.End(doc, std::nullopt);
    Expect(limited.UndoCount() == 0, "history.zero-budget-drops-unique");
    Expect(limited.RetainedBytes(doc) == 0, "history.zero-budget-bytes");
}

void TestHistoryTrim256MiB()
{
    DocumentHistory history;
    std::optional<CanvasDocument> document = CanvasDocument{};
    document.value().width = 64;
    document.value().height = 32;
    std::optional<Guid> selection;
    constexpr int kWidth = 1024;
    constexpr int kHeight = 1024;
    for (int i = 0; i < 70; ++i)
    {
        history.Begin(L"Replace", document, selection);
        ImageLayer layer = RasterLayer(kWidth, kHeight, L"Unique");
        selection = layer.id;
        document.value().layers = {std::move(layer)};
        history.End(document, selection);
    }
    Expect(history.RetainedBytes(document) <= static_cast<std::size_t>(kHistoryRetainedByteLimit),
           "history.trim-256mib");
    Expect(history.UndoCount() < 70, "history.trim-256mib-dropped-entries");
}

void TestSelection()
{
    CanvasDocument document;
    document.width = 8;
    document.height = 8;
    Expect(!document.selection.has_value(), "selection.none");
    document.selection = DocumentSelection{};
    Expect(document.selection.value().IsEmpty(), "selection.empty-outline");
    document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 0, 4)};
    Expect(document.selection.value().IsEmpty(), "selection.zero-area-is-empty");
    document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 3, 4)};
    Expect(!document.selection.value().IsEmpty() && document.selection.value().Bounds() == SelectionBounds{1, 1, 4, 5},
           "selection.bounds");
}

[[nodiscard]] Raster Gray(int width, int height, std::initializer_list<std::uint8_t> values)
{
    std::vector<std::uint8_t> pixels(values);
    pixels.resize(static_cast<std::size_t>(width * height), 0);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

void TestMaskPlacementHelpers()
{
    // Legacy LayerMask.background: the edge majority of the thumbnail decides what a placed mask shows beyond
    // its pixels.
    Expect(MaskEdgeBackground(Gray(2, 2, {255, 0, 128, 255})) == 1.0, "mask.background-mostly-white");
    Expect(MaskEdgeBackground(Gray(2, 2, {0, 0, 0, 0})) == 0.0, "mask.background-black");
    Expect(MaskEdgeBackground(Gray(1, 1, {128})) == 1.0, "mask.background-half-rounds-up");
    Expect(MaskEdgeBackground(Gray(1, 1, {127})) == 0.0, "mask.background-below-half");
    Expect(MaskEdgeBackground(Gray(3, 3, {0, 0, 0, 0, 255, 0, 0, 0, 0})) == 0.0, "mask.background-ignores-interior");
    Expect(MaskEdgeBackground(Raster{}) == 1.0, "mask.background-missing-thumbnail-reveals");
    Expect(MaskEdgeBackground(Raster::Make(1, 1, RasterFormat::Bgra8888)) == 1.0, "mask.background-color-reveals");

    ImageLayer layer = RasterLayer(4, 4, L"Paint");
    layer.transform.origin = Vec2{3, 5};
    Expect(MaskTransform(layer) == layer.transform, "mask.transform-without-mask");
    layer.mask = LayerMask{RasterAsset{Gray(1, 1, {255}), Gray(1, 1, {255}), L"Layer Mask"}, true, std::nullopt, true};
    Expect(MaskTransform(layer) == layer.transform, "mask.transform-linked-follows-layer");
    LayerTransform placement = layer.transform;
    placement.origin.x = 9;
    layer.mask.value().placement = placement;
    layer.mask.value().linked = false;
    Expect(MaskTransform(layer) == placement, "mask.transform-placed");
    Expect(!(layer.mask.value() == LayerMask{layer.mask.value().asset, true, std::nullopt, true}),
           "mask.placement-affects-equality");
}

void TestCollapsedRows()
{
    DocumentSession session;
    session.CreateDocument(10, 10);
    session.AddGroup();
    const Guid folder = session.ActiveLayerID().value();
    session.AddBlankLayer();
    std::vector<HierarchyEntry> rows = session.LayerRows();
    Expect(rows.size() == 2 && rows[0].isGroup && !rows[0].collapsed, "rows.expanded-flag");
    session.CollapsedGroupIDs().insert(folder);
    rows = session.LayerRows();
    Expect(rows.size() == 1 && rows[0].collapsed && rows[0].isGroup, "rows.collapsed-flag");
    Expect(!HierarchyEntries(session.Document().value().layers)[0].collapsed, "rows.default-not-collapsed");
}

void TestDeleteClearsClipping()
{
    DocumentSession session;
    session.CreateDocument(2, 2);
    session.AddBlankLayer();
    const Guid source = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid target = session.ActiveLayerID().value();
    Expect(session.LinkMask(source, target), "delete.link");
    session.DeleteLayer(source);
    const ImageLayer* remaining = Find(session.Document(), target);
    Expect(remaining != nullptr && !remaining->maskSourceID.has_value(), "delete.source-clears-clip");
    session.Undo();
    remaining = Find(session.Document(), target);
    Expect(remaining != nullptr && remaining->maskSourceID == source, "delete.undo-restores-clip");
}

void TestNamingAndInsertion()
{
    // Legacy LayerTests.blankLayersAreTransparentAndInsertedAboveSelection.
    DocumentSession session;
    session.CreateDocument(800, 600);
    for (int i = 0; i < 3; ++i)
    {
        session.AddBlankLayer();
    }
    const Guid first = session.Document().value().layers.front().id;
    session.SelectLayer(first);
    session.AddBlankLayer();
    const std::vector<ImageLayer>& layers = session.Document().value().layers;
    Expect(layers.size() == 4 && layers[0].name == L"Layer 1" && layers[1].name == L"Layer 4" &&
               layers[2].name == L"Layer 2" && layers[3].name == L"Layer 3",
           "insert.above-selection");
    Expect(layers[1].id == session.ActiveLayerID().value() && !layers[1].asset.has_value() &&
               layers[1].transform.size == Vec2{800, 600} && layers[1].transform.origin == Vec2{},
           "insert.blank-layer-shape");
    // Legacy: with a folder selected, the new layer goes to the top of the folder's contents.
    session.SelectLayer(std::nullopt);
    session.AddGroup();
    const Guid folder = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid inner1 = session.ActiveLayerID().value();
    session.SelectLayer(folder);
    session.AddBlankLayer();
    const Guid inner2 = session.ActiveLayerID().value();
    const std::vector<ImageLayer>& after = session.Document().value().layers;
    std::size_t index1 = 0;
    std::size_t index2 = 0;
    for (std::size_t i = 0; i < after.size(); ++i)
    {
        if (after[i].id == inner1)
        {
            index1 = i;
        }
        if (after[i].id == inner2)
        {
            index2 = i;
        }
    }
    Expect(index2 == index1 + 1 && after[index2].parentID == folder, "insert.folder-selected-goes-on-top");
    // Deletion chooses the neighbor (legacy deletionPreservesCanvasAndChoosesNeighbor).
    DocumentSession three;
    three.CreateDocument(800, 600);
    for (int i = 0; i < 3; ++i)
    {
        three.AddBlankLayer();
    }
    const std::vector<ImageLayer> before = three.Document().value().layers;
    three.SelectLayer(before[1].id);
    three.DeleteLayer(before[0].id);
    Expect(three.ActiveLayerID() == before[1].id, "delete.keeps-active-when-other-deleted");
    three.DeleteLayer(before[1].id);
    Expect(three.ActiveLayerID() == before[2].id, "delete.chooses-neighbor");
    three.DeleteLayer(before[2].id);
    Expect(!three.ActiveLayerID().has_value() && three.Document().value().layers.empty() &&
               three.Document().value().width == 800,
           "delete.last-layer-keeps-canvas");
}
void TestPaletteColors()
{
    DocumentSession session;
    Expect(session.ForegroundColor() == DocumentSession::kDefaultForegroundArgb &&
               session.BackgroundColor() == DocumentSession::kDefaultBackgroundArgb,
           "palette.defaults");
    session.SetForegroundColor(0x00336699u);
    Expect(session.ForegroundColor() == 0xFF336699u, "palette.opaque");
    session.SwapPaletteColors();
    Expect(session.ForegroundColor() == 0xFFFFFFFFu && session.BackgroundColor() == 0xFF336699u, "palette.swap");
    session.CreateDocument(8, 8, true);
    const bool couldUndo = session.history.CanUndo();
    const bool modified = session.history.IsModified();
    session.ResetPaletteColors();
    Expect(session.ForegroundColor() == DocumentSession::kDefaultForegroundArgb &&
               session.BackgroundColor() == DocumentSession::kDefaultBackgroundArgb,
           "palette.reset");
    Expect(session.history.CanUndo() == couldUndo && session.history.IsModified() == modified,
           "palette.outside-history");
}

// ── Transform (C8, ported from legacy TransformTests, MaskTransformTests and DistortTests) ────────────────

[[nodiscard]] bool Near(double a, double b, double epsilon = 0.0001) noexcept
{
    return std::abs(a - b) < epsilon;
}

[[nodiscard]] bool NearPoint(Vec2 a, Vec2 b, double epsilon = 0.0001) noexcept
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
}

[[nodiscard]] Raster SolidRaster(int width, int height, std::uint8_t blue, std::uint8_t green, std::uint8_t red,
                                 std::uint8_t alpha)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
    for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
    {
        bytes[index] = blue;
        bytes[index + 1] = green;
        bytes[index + 2] = red;
        bytes[index + 3] = alpha;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

[[nodiscard]] Raster GrayRaster(int width, int height, std::uint8_t value)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

[[nodiscard]] std::uint8_t AlphaAt(const Raster& raster, int x, int y) noexcept
{
    if (x < 0 || y < 0 || x >= raster.Width() || y >= raster.Height() || raster.Format() != RasterFormat::Bgra8888)
    {
        return 0;
    }
    return raster.Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(raster.Stride()) +
                         static_cast<std::size_t>(x) * 4u + 3u];
}

// A painted layer placed on the session's document and made active, as an import leaves it.
Guid InsertPainted(DocumentSession& session, int width, int height, Vec2 origin, const wchar_t* name = L"Painted")
{
    ImageLayer layer = RasterLayer(width, height, name);
    layer.asset->image = SolidRaster(width, height, 0, 0, 255, 255);
    layer.asset->thumbnail = layer.asset->image;
    layer.transform.origin = origin;
    session.Document().value().layers.push_back(layer);
    session.SelectLayer(layer.id);
    return layer.id;
}

void TestTransformGeometry()
{
    LayerTransform t;
    t.origin = Vec2{100, 200};
    t.size = Vec2{100, 50};
    t.rotation = 90;
    Expect(t.Contains(t.Center()), "transform.contains-center");
    Expect(t.Contains(Vec2{150, 265}) && !t.Contains(Vec2{190, 225}), "transform.rotated-contains");
    Expect(NearPoint(t.Point(Vec2{1, 1}), Vec2{125, 275}), "transform.rotated-corner");
    const std::array<Vec2, 4> corners = t.Corners();
    Expect(NearPoint(corners[0], Vec2{175, 175}) && NearPoint(corners[2], Vec2{125, 275}), "transform.corners-order");

    LayerTransform s;
    s.size = Vec2{200, 300};
    s.rotation = 30;
    Expect(s.ScalePercent(Vec2{100, 100}) == 200.0, "transform.scale-percent");
    const LayerTransform scaled = s.ScaledToPercent(50, Vec2{100, 100});
    Expect(scaled.size == Vec2{50, 50} && NearPoint(scaled.Center(), s.Center()) && scaled.rotation == 30 &&
               scaled.ScalePercent(Vec2{100, 100}) == 50.0,
           "transform.scaled-to-percent");

    LayerTransform r;
    r.origin = Vec2{1.4, 2.6};
    r.size = Vec2{0.2, 10.5};
    r.rotation = 12.4;
    const LayerTransform rounded = r.Rounded();
    Expect(rounded.origin == Vec2{1, 3} && rounded.size == Vec2{1, 11} && rounded.rotation == 12, "transform.rounded");

    LayerTransform m;
    m.origin = Vec2{10, 20};
    m.size = Vec2{40, 30};
    m.rotation = 15;
    const LayerTransform mirrored = m.Mirrored(true, 100);
    Expect(mirrored.flipX && !mirrored.flipY && mirrored.origin.x == 150 && mirrored.origin.y == 20 &&
               mirrored.rotation == -15,
           "transform.mirrored-horizontal");
    const LayerTransform vertical = m.Mirrored(false, 50);
    Expect(vertical.flipY && vertical.origin.y == 50 && vertical.origin.x == 10, "transform.mirrored-vertical");

    LayerTransform from;
    from.size = Vec2{100, 100};
    LayerTransform to = from;
    to.origin = Vec2{25, -10};
    LayerTransform placed;
    placed.origin = Vec2{10, 10};
    placed.size = Vec2{20, 20};
    placed.rotation = 33;
    const LayerTransform moved = placed.Following(from, to);
    Expect(moved.origin == Vec2{35, 0} && moved.rotation == 33, "transform.following-move");
    LayerTransform big = from;
    big.size = Vec2{200, 200};
    const LayerTransform grown = placed.Following(from, big);
    Expect(NearPoint(grown.Center(), Vec2{40, 40}) && NearPoint(grown.size, Vec2{40, 40}) && Near(grown.rotation, 33),
           "transform.following-scale");
    LayerTransform turned = from;
    turned.rotation = 90;
    const LayerTransform rotated = placed.Following(from, turned);
    Expect(NearPoint(rotated.Center(), Vec2{80, 20}) && Near(rotated.rotation, 123) &&
               NearPoint(rotated.size, Vec2{20, 20}) && !rotated.flipY,
           "transform.following-rotate");
    Expect(placed.Following(from, from) == placed, "transform.following-identity");

    LayerTransform sampled = placed;
    sampled.sampling = LayerSampling::Nearest;
    Expect(placed.SamePlacement(sampled) && !(placed == sampled), "transform.same-placement");
    Expect(NearPoint(placed.UnitToDocument().Apply(Vec2{0, 0}), placed.Point(Vec2{0, 0})) &&
               NearPoint(placed.UnitToDocument().Apply(Vec2{1, 1}), placed.Point(Vec2{1, 1})),
           "transform.unit-to-document");
    LayerTransform flipped = placed;
    flipped.flipX = true;
    Expect(NearPoint(flipped.UnitToDocument().Apply(Vec2{0, 0}), placed.Point(Vec2{1, 0})), "transform.unit-flip");
    const Affine2 map = Affine2::Translation(3, 4).Concat(Affine2::Scale(2, 2));
    Expect(map.Apply(Vec2{1, 1}) == Vec2{8, 10} && NearPoint(map.Inverted().Apply(Vec2{8, 10}), Vec2{1, 1}),
           "transform.affine-concat-inverse");
}

void TestTransformDrag()
{
    LayerTransform original;
    original.size = Vec2{100, 50};
    const TransformDrag move{TransformDrag::Mode::Move, -1, original, Vec2{40, 20}, std::nullopt};
    Expect(move.Updated(Vec2{60, 25}, true, true).origin == Vec2{20, 0}, "drag.move-shift-axis");
    Expect(move.Updated(Vec2{60, 25}, true, false).origin == Vec2{20, 5}, "drag.move-free");
    const TransformDrag rotate{TransformDrag::Mode::Rotate, -1, original, Vec2{100, 25}, std::nullopt};
    const LayerTransform rotated = rotate.Updated(Vec2{50, 75}, true, false);
    Expect(Near(rotated.rotation, 90) && rotated.Center() == original.Center(), "drag.rotate-90");
    Expect(std::fmod(rotate.Updated(Vec2{99, 45}, true, true).rotation, 15.0) == 0.0, "drag.rotate-shift-15");
    const TransformDrag resize{TransformDrag::Mode::Resize, 4, original, Vec2{100, 50}, std::nullopt};
    Expect(resize.Updated(Vec2{150, 50}, true, true).size == Vec2{150, 50}, "drag.resize-shift-inverts-lock");
    const LayerTransform locked = resize.Updated(Vec2{150, 50}, true, false);
    Expect(Near(locked.size.x / locked.size.y, 2.0) && locked.origin == Vec2{0, 0}, "drag.resize-lock-ratio-anchor");

    LayerTransform turned;
    turned.origin = Vec2{31, -19};
    turned.size = Vec2{200, 100};
    turned.rotation = 37;
    bool anchored = true;
    bool ratio = true;
    bool valid = true;
    for (int index = 0; index < 8; ++index)
    {
        const Vec2 handle = LayerTransform::kHandles[static_cast<std::size_t>(index)];
        const Vec2 opposite{1 - handle.x, 1 - handle.y};
        const Vec2 start = turned.Point(handle);
        const TransformDrag drag{TransformDrag::Mode::Resize, index, turned, start, std::nullopt};
        for (const bool lock : {true, false})
        {
            const LayerTransform changed = drag.Updated(Vec2{start.x + 34, start.y + 17}, lock, false);
            anchored = anchored && NearPoint(turned.Point(opposite), changed.Point(opposite));
            if (lock)
            {
                ratio = ratio && Near(changed.size.x / changed.size.y, 2.0);
            }
            valid = valid && changed.IsValid();
        }
    }
    Expect(anchored, "drag.resize-keeps-opposite-anchor");
    Expect(ratio, "drag.resize-locked-ratio");
    Expect(valid, "drag.resize-valid");
    const TransformDrag centered{TransformDrag::Mode::Resize, 4, original, Vec2{100, 50}, std::nullopt};
    const LayerTransform grown = centered.Updated(Vec2{110, 60}, false, false, true);
    Expect(NearPoint(grown.Center(), original.Center()) && NearPoint(grown.size, Vec2{120, 70}),
           "drag.resize-option-center");
    LayerTransform tiny = original;
    const TransformDrag collapse{TransformDrag::Mode::Resize, 4, tiny, Vec2{100, 50}, std::nullopt};
    Expect(collapse.Updated(Vec2{-500, -500}, false, false).IsValid(), "drag.resize-never-invalid");

    const std::array<Vec2, 4> corners = original.Corners();
    const TransformDrag corner{TransformDrag::Mode::Distort, 2, original, Vec2{100, 0}, corners};
    const std::optional<std::array<Vec2, 4>> one = corner.Corners(Vec2{110, 5}, false);
    Expect(one.has_value() && (*one)[1] == Vec2{110, 5} && (*one)[0] == corners[0] && (*one)[2] == corners[2],
           "drag.distort-corner");
    const TransformDrag edge{TransformDrag::Mode::Distort, 3, original, Vec2{100, 25}, corners};
    const std::optional<std::array<Vec2, 4>> two = edge.Corners(Vec2{105, 30}, true);
    Expect(two.has_value() && (*two)[1] == Vec2{105, 0} && (*two)[2] == Vec2{105, 50} && (*two)[0] == corners[0],
           "drag.distort-edge-shift");
    const TransformDrag body{TransformDrag::Mode::Move, -1, original, Vec2{50, 25}, corners};
    const std::optional<std::array<Vec2, 4>> all = body.Corners(Vec2{60, 35}, false);
    Expect(all.has_value() && (*all)[3] == Vec2{10, 60} && (*all)[0] == Vec2{10, 10}, "drag.distort-body");
    const TransformDrag spin{TransformDrag::Mode::Rotate, -1, original, Vec2{}, corners};
    Expect(!spin.Corners(Vec2{}, false).has_value(), "drag.distort-none-for-rotate");
    Expect(!move.Corners(Vec2{}, false).has_value(), "drag.distort-none-without-corners");
}

void TestTransformSnap()
{
    const std::array<double, 3> xs{0, 100, 200};
    const std::array<double, 2> ys{0, 50};
    const TransformSnap::Result nearest = TransformSnap::Offset(12, 3, 52, 23, xs, ys, 15);
    Expect(nearest.offset.x == -12 && nearest.x == 0.0 && nearest.offset.y == -3 && nearest.y == 0.0,
           "snap.nearest-target");
    const TransformSnap::Result none = TransformSnap::Offset(40, 300, 60, 320, xs, ys, 5);
    Expect(none.offset == Vec2{} && !none.x.has_value() && !none.y.has_value(), "snap.out-of-tolerance");
    const TransformSnap::Result tie = TransformSnap::Offset(98, 0, 198, 10, xs, ys, 5);
    Expect(tie.offset.x == 2 && tie.x == 100.0 && tie.offset.y == 0 && tie.y == 0.0, "snap.smallest-move");

    DocumentSession session;
    session.CreateDocument(400, 200, false);
    const Guid other = InsertPainted(session, 100, 100, Vec2{200, 50}, L"Other");
    const Guid id = InsertPainted(session, 50, 50, Vec2{0, 0});
    const SnapTargets targets = session.TransformSnapTargets({id});
    Expect(targets.xs.size() == 6 && targets.ys.size() == 6 && targets.xs[3] == 200 && targets.ys[4] == 100,
           "snap.targets-canvas-and-layers");
    LayerTransform draft = session.ActiveLayer()->transform;
    draft.origin = Vec2{147, 3};
    const LayerTransform snapped = session.SnappedMove(draft, {id}, 5);
    Expect(snapped.origin == Vec2{150, 0} && session.LastSnapGuides().xs.size() == 1 &&
               session.LastSnapGuides().xs[0] == 200 && session.LastSnapGuides().ys.size() == 1 &&
               session.LastSnapGuides().ys[0] == 0,
           "snap.session-move-with-guides");
    session.ClearSnapGuides();
    Expect(session.LastSnapGuides().xs.empty() && session.LastSnapGuides().ys.empty(), "snap.clear-guides");
    Expect(session.TransformSnapTargets({id, other}).xs.size() == 3, "snap.excludes-moving");
}

void TestTransformSession()
{
    DocumentSession session;
    session.CreateDocument(200, 100, false);
    Expect(!session.CanTransform(), "transform.no-layer");
    session.AddBlankLayer();
    const Guid blank = session.ActiveLayerID().value();
    Expect(!session.CanTransform(), "transform.blank-cannot");
    session.BeginTransform();
    Expect(!session.PendingTransformEdit().has_value(), "transform.blank-no-edit");
    const Guid id = InsertPainted(session, 200, 100, Vec2{0, 0});
    Expect(session.CanTransform() && !session.TransformsAsGroup(), "transform.painted-can");
    const ImageLayer original = *session.ActiveLayer();
    const int count = session.history.UndoCount();
    session.BeginTransform();
    LayerTransform value = original.transform;
    value.origin = Vec2{-45, 34};
    value.size = Vec2{80, 140};
    value.rotation = 23;
    value.flipX = true;
    for (int repeat = 0; repeat < 30; ++repeat)
    {
        session.PreviewTransform(value);
    }
    Expect(session.ActiveLayer()->transform == original.transform, "transform.preview-keeps-document");
    Expect(session.history.UndoCount() == count, "transform.preview-no-history");
    Expect(session.DisplayedTransform(*session.ActiveLayer()) == value &&
               session.EditedTransform(*session.ActiveLayer()) == value,
           "transform.preview-displayed");
    const CanvasDocument* display = session.DisplayDocument();
    Expect(display != nullptr && display != &session.Document().value() && display->layers.back().transform == value &&
               display->layers.back().asset->image == original.asset->image && display == session.DisplayDocument(),
           "transform.display-copy-shares-raster");
    session.CancelTransform();
    Expect(*session.ActiveLayer() == original && session.DisplayDocument() == &session.Document().value(),
           "transform.cancel-restores");
    session.BeginTransform();
    session.PreviewTransform(value);
    session.CommitTransform();
    Expect(session.history.UndoCount() == count + 1 && session.ActiveLayer()->transform == value &&
               session.history.UndoName() == L"Transform Layer",
           "transform.commit-history");
    session.Undo();
    Expect(*session.ActiveLayer() == original, "transform.undo");
    session.Redo();
    Expect(session.ActiveLayer()->transform == value && session.ActiveLayerID() == original.id, "transform.redo");
    session.Undo();

    const int afterUndo = session.history.UndoCount();
    session.BeginTransform();
    session.CommitTransform();
    Expect(session.history.UndoCount() == afterUndo, "transform.noop-commit");
    session.BeginTransform();
    LayerTransform invalid = session.PendingTransformEdit()->draft;
    invalid.size.x = 0;
    session.PreviewTransform(invalid);
    Expect(session.PendingTransformEdit()->draft.size.x == 200, "transform.invalid-zero-ignored");
    invalid.size.x = std::numeric_limits<double>::infinity();
    session.PreviewTransform(invalid);
    Expect(session.PendingTransformEdit()->draft.size.x == 200, "transform.invalid-inf-ignored");
    LayerTransform moved = session.PendingTransformEdit()->draft;
    moved.origin.x = 10;
    session.PreviewTransform(moved);
    session.SelectLayer(blank);
    Expect(!session.PendingTransformEdit().has_value() && Find(session.Document(), id)->transform.origin.x == 10 &&
               session.history.UndoCount() == afterUndo + 1,
           "transform.select-commits");
    session.SelectLayer(id);
    Expect(session.TransformPixelSize().has_value() && session.TransformPixelSize().value() == Vec2{200, 100},
           "transform.pixel-size");

    session.BeginTransform();
    session.PreviewTransform(value);
    const int pending = session.history.UndoCount();
    session.Undo();
    Expect(!session.PendingTransformEdit().has_value() && session.history.UndoCount() == pending &&
               session.ActiveLayer()->transform.origin.x == 10,
           "transform.undo-cancels-pending");

    session.BeginTransform();
    session.NudgeLayer(1, 0);
    Expect(session.PendingTransformEdit().has_value() && session.PendingTransformEdit()->draft.origin.x == 11 &&
               session.ActiveLayer()->transform.origin.x == 10,
           "transform.nudge-pending");
    session.CancelTransform();
    const int nudgeCount = session.history.UndoCount();
    session.NudgeLayer(0, -3);
    Expect(!session.PendingTransformEdit().has_value() && session.ActiveLayer()->transform.origin.y == -3 &&
               session.history.UndoCount() == nudgeCount + 1,
           "transform.nudge-commits");
}

void TestTransformDuplicate()
{
    DocumentSession session;
    session.CreateDocument(400, 200, false);
    InsertPainted(session, 100, 100, Vec2{150, 50});
    const ImageLayer original = *session.ActiveLayer();
    const int count = session.history.UndoCount();
    session.BeginDuplicateTransform();
    LayerTransform moved = original.transform;
    moved.origin.x += 50;
    session.PreviewTransform(moved);
    session.CommitTransform();
    Expect(session.Document().value().layers.size() == 2, "duplicate.two-layers");
    Expect(session.Document().value().layers[0].transform == original.transform, "duplicate.original-stays");
    Expect(session.ActiveLayer()->transform == moved && session.ActiveLayer()->name == L"Painted copy" &&
               session.ActiveLayer()->asset->image == original.asset->image,
           "duplicate.copy-moved-shares-pixels");
    Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Duplicate Layer",
           "duplicate.one-step");
    session.Undo();
    Expect(session.Document().value().layers.size() == 1 && session.ActiveLayerID() == original.id, "duplicate.undo");
    session.BeginDuplicateTransform();
    session.PreviewTransform(moved);
    session.CancelTransform();
    Expect(session.Document().value().layers.size() == 1 && session.ActiveLayerID() == original.id &&
               session.history.UndoCount() == count,
           "duplicate.cancel");
}

void TestTransformGroup()
{
    DocumentSession session;
    session.CreateDocument(400, 300, false);
    session.AddGroup();
    const Guid folder = session.ActiveLayerID().value();
    const Guid a = InsertPainted(session, 100, 50, Vec2{10, 20}, L"A");
    session.Document().value().layers.back().parentID = folder;
    const Guid b = InsertPainted(session, 40, 40, Vec2{200, 100}, L"B");
    session.Document().value().layers.back().parentID = folder;
    session.SelectLayer(folder);
    Expect(session.TransformsAsGroup() && session.CanTransform(), "group.folder-transforms-members");
    Expect(session.GroupTransformMemberIDs().size() == 2, "group.members");
    const std::optional<LayerTransform> box = session.GroupTransformBox();
    Expect(box.has_value() && box->origin == Vec2{10, 20} && box->size == Vec2{230, 120}, "group.box");
    Expect(session.EditedTransform(*session.ActiveLayer()) == box.value(), "group.edited-transform-is-box");
    Expect(session.TransformPixelSize().has_value() && session.TransformPixelSize().value() == box->size,
           "group.pixel-size");
    session.BeginTransform();
    LayerTransform draft = box.value();
    draft.origin.x += 100;
    draft.origin.y -= 5;
    session.PreviewTransform(draft);
    const ImageLayer* layerA = Find(session.Document(), a);
    Expect(layerA != nullptr && session.DisplayedTransform(*layerA).origin == Vec2{110, 15} &&
               layerA->transform.origin == Vec2{10, 20},
           "group.preview-follows");
    session.CommitTransform();
    Expect(Find(session.Document(), a)->transform.origin == Vec2{110, 15} &&
               Find(session.Document(), b)->transform.origin == Vec2{300, 95} &&
               session.history.UndoName() == L"Transform Layers",
           "group.commit-moves-all");
    session.BeginTransform();
    LayerTransform grown = session.PendingTransformEdit()->draft;
    grown.size = Vec2{460, 240};
    session.PreviewTransform(grown);
    session.CommitTransform();
    const ImageLayer* grownA = Find(session.Document(), a);
    Expect(grownA != nullptr && NearPoint(grownA->transform.size, Vec2{200, 100}) &&
               NearPoint(grownA->transform.origin, Vec2{110, 15}),
           "group.scale-follows");
    session.FlipLayers(true);
    Expect(Find(session.Document(), a)->transform.flipX && Find(session.Document(), b)->transform.flipX &&
               session.history.UndoName() == L"Flip Horizontal",
           "group.flip-members");
    session.Undo();
    Expect(!Find(session.Document(), a)->transform.flipX, "group.flip-undo");
}

void TestMaskTransform()
{
    DocumentSession session;
    session.CreateDocument(400, 200, false);
    const Guid id = InsertPainted(session, 400, 200, Vec2{0, 0});
    LayerMask mask;
    mask.asset.image = GrayRaster(400, 200, 255);
    mask.asset.thumbnail = mask.asset.image;
    mask.asset.name = L"mask";
    session.Document().value().layers.back().mask = mask;
    const LayerTransform original = session.ActiveLayer()->transform;

    session.SelectLayerTarget(id, true);
    Expect(session.IsMaskSelected() && !session.TransformTargetsMask(), "mask.linked-targets-layer");
    session.BeginTransform();
    LayerTransform moved = session.PendingTransformEdit()->draft;
    moved.origin.x += 100;
    session.PreviewTransform(moved);
    Expect(!session.PendingTransformEdit()->mask && !session.DisplayedMaskPlacement(*session.ActiveLayer()).has_value(),
           "mask.linked-follows-preview");
    session.CommitTransform();
    Expect(session.ActiveLayer()->transform == moved && !session.ActiveLayer()->mask->placement.has_value() &&
               session.ActiveLayer()->mask->asset.image == mask.asset.image,
           "mask.linked-commit");
    session.Undo();

    session.ToggleMaskLink(id);
    Expect(!session.ActiveLayer()->mask->linked && session.history.UndoName() == L"Unlink Layer Mask", "mask.unlink");
    session.SelectLayerTarget(id, false);
    session.BeginTransform();
    moved = session.PendingTransformEdit()->draft;
    moved.origin.x += 100;
    session.PreviewTransform(moved);
    Expect(session.DisplayedMaskPlacement(*session.ActiveLayer()) == original, "mask.unlinked-stays-preview");
    const CanvasDocument* display = session.DisplayDocument();
    Expect(display != nullptr && display->layers.back().transform == moved &&
               display->layers.back().mask->placement == original,
           "mask.unlinked-display-copy");
    session.CommitTransform();
    Expect(session.ActiveLayer()->transform == moved && session.ActiveLayer()->mask->placement == original,
           "mask.unlinked-layer-moves-alone");
    session.Undo();
    Expect(session.ActiveLayer()->transform == original && !session.ActiveLayer()->mask->placement.has_value() &&
               !session.ActiveLayer()->mask->linked,
           "mask.unlinked-undo");

    session.SelectLayerTarget(id, true);
    Expect(session.TransformTargetsMask(), "mask.targets-mask");
    session.BeginTransform();
    moved = session.PendingTransformEdit()->draft;
    moved.origin.x += 100;
    session.PreviewTransform(moved);
    Expect(session.PendingTransformEdit()->mask && session.DisplayedTransform(*session.ActiveLayer()) == original &&
               session.EditedTransform(*session.ActiveLayer()) == moved &&
               session.DisplayedMaskPlacement(*session.ActiveLayer()) == moved,
           "mask.mask-alone-preview");
    Expect(!session.TransformPixelSize().has_value(), "mask.mask-alone-no-pixel-size");
    session.CommitTransform();
    Expect(session.ActiveLayer()->transform == original && session.ActiveLayer()->mask->placement == moved &&
               session.ActiveLayer()->mask->asset.image == mask.asset.image &&
               session.history.UndoName() == L"Transform Layer Mask",
           "mask.mask-alone-commit");

    session.ToggleMaskLink(id);
    session.SelectLayerTarget(id, false);
    session.BeginTransform();
    LayerTransform together = session.PendingTransformEdit()->draft;
    together.origin.x += 50;
    session.PreviewTransform(together);
    session.CommitTransform();
    const std::optional<LayerTransform> placement = session.ActiveLayer()->mask->placement;
    Expect(placement.has_value() && Near(placement->origin.x, moved.origin.x + 50) &&
               Near(placement->origin.y, moved.origin.y) && Near(placement->size.x, moved.size.x),
           "mask.relinked-follows");

    LayerMask uniform;
    uniform.asset.image = GrayRaster(1, 1, 255);
    uniform.asset.thumbnail = uniform.asset.image;
    uniform.linked = false;
    Expect(!MaskPlacementMoving(uniform, original, moved).has_value(), "mask.uniform-follows-anywhere");
}

void TestFlipCommands()
{
    DocumentSession session;
    session.CreateDocument(400, 200, false);
    InsertPainted(session, 100, 50, Vec2{20, 30});
    LayerTransform before = session.ActiveLayer()->transform;
    before.rotation = 10;
    session.Document().value().layers.back().transform = before;
    session.FlipCanvas(true);
    const LayerTransform flipped = session.ActiveLayer()->transform;
    Expect(flipped.flipX && flipped.rotation == -10 && Near(flipped.Center().x, 400 - before.Center().x) &&
               flipped.origin.y == 30 && session.history.UndoName() == L"Flip Canvas Horizontal",
           "flip.canvas-horizontal");
    session.FlipCanvas(false);
    Expect(session.ActiveLayer()->transform.flipY &&
               Near(session.ActiveLayer()->transform.Center().y, 200 - before.Center().y) &&
               session.ActiveLayer()->transform.rotation == 10,
           "flip.canvas-vertical");
    session.Undo();
    session.Undo();
    Expect(session.ActiveLayer()->transform == before, "flip.canvas-undo");
    session.FlipLayers(false);
    Expect(session.ActiveLayer()->transform.flipY &&
               NearPoint(session.ActiveLayer()->transform.Center(), before.Center()) &&
               session.history.UndoName() == L"Flip Vertical",
           "flip.layer-about-center");
    session.Document().value().selection =
        DocumentSelection{true, {SelectionLoop{Vec2{10, 10}, Vec2{50, 10}, Vec2{50, 40}}}};
    session.FlipCanvas(true);
    Expect(session.Document().value().selection->loops[0][0] == Vec2{390, 10}, "flip.canvas-selection");
}

void TestDistortWarp()
{
    const std::array<Vec2, 4> shape{Vec2{10, 10}, Vec2{60, 10}, Vec2{30, 30}, Vec2{10, 30}};
    const DistortWarp::Homography map = DistortWarp::MakeHomography(shape);
    const std::array<Vec2, 4> units{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};
    bool hits = true;
    for (std::size_t index = 0; index < 4; ++index)
    {
        hits = hits && NearPoint(map.Map(units[index]), shape[index], 1e-6);
    }
    Expect(hits, "distort.homography-hits-corners");
    Expect(DistortWarp::IsUsable(shape), "distort.usable");
    Expect(!DistortWarp::IsUsable(std::array<Vec2, 4>{shape[0], shape[2], shape[1], shape[3]}),
           "distort.bow-tie-refused");
    Expect(!DistortWarp::IsUsable(std::array<Vec2, 4>{shape[0], shape[0], shape[2], shape[3]}),
           "distort.collapsed-refused");
    const std::optional<DistortWarp::Homography> inverse = map.Inverted();
    Expect(inverse.has_value() && NearPoint(inverse->Map(shape[2]), Vec2{1, 1}, 1e-6), "distort.inverse");

    DocumentSession session;
    session.CreateDocument(100, 60, false);
    InsertPainted(session, 20, 20, Vec2{10, 10});
    session.BeginTransform(false);
    session.BeginDistort();
    Expect(session.PendingTransformEdit()->corners.has_value() && session.PendingTransformEdit()->persistent,
           "distort.begin");
    session.PreviewCorners(std::array<Vec2, 4>{shape[0], shape[2], shape[1], shape[3]});
    Expect(session.PendingTransformEdit()->corners.value()[1] == Vec2{30, 10}, "distort.twisted-ignored");
    session.PreviewCorners(shape);
    const CanvasDocument* display = session.DisplayDocument();
    Expect(display != nullptr && display->layers.back().transform.origin == Vec2{10, 10} &&
               display->layers.back().transform.size == Vec2{50, 20},
           "distort.preview-bounds");
    const int count = session.history.UndoCount();
    session.CommitTransform();
    const ImageLayer* layer = session.ActiveLayer();
    Expect(!session.PendingTransformEdit().has_value() && session.history.UndoCount() == count + 1 &&
               session.history.UndoName() == L"Distort",
           "distort.commit-history");
    Expect(layer->transform.origin == Vec2{10, 10} && layer->transform.size == Vec2{50, 20} &&
               layer->transform.rotation == 0,
           "distort.commit-bounds");
    const Raster& warped = layer->asset->image;
    Expect(warped.Width() == 50 && warped.Height() == 20, "distort.warped-size");
    Expect(AlphaAt(warped, 40, 2) == 255 && AlphaAt(warped, 5, 15) == 255, "distort.inside-opaque");
    Expect(AlphaAt(warped, 40, 18) == 0, "distort.outside-transparent");
    session.Undo();
    Expect(session.ActiveLayer()->transform.size == Vec2{20, 20} && session.ActiveLayer()->asset->image.Width() == 20,
           "distort.undo");

    DocumentSession trim;
    trim.CreateDocument(100, 60, false);
    ImageLayer stroke = RasterLayer(40, 20, L"Stroke");
    std::vector<std::uint8_t> bytes(40u * 4u * 20u, 0);
    for (int y = 5; y < 15; ++y)
    {
        for (int x = 15; x < 25; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * 40u + static_cast<std::size_t>(x)) * 4u;
            bytes[offset + 2] = 255;
            bytes[offset + 3] = 255;
        }
    }
    stroke.asset->image = Raster::Make(40, 20, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    stroke.asset->thumbnail = stroke.asset->image;
    stroke.transform.origin = Vec2{10, 10};
    trim.Document().value().layers.push_back(stroke);
    trim.SelectLayer(stroke.id);
    trim.BeginTransform(false);
    trim.BeginDistort();
    trim.PreviewCorners(std::array<Vec2, 4>{Vec2{10, 10}, Vec2{60, 10}, Vec2{50, 30}, Vec2{10, 30}});
    trim.CommitTransform();
    const LayerTransform trimmed = trim.ActiveLayer()->transform;
    Expect(trimmed.size.x < 20 && trimmed.size.y <= 12 && trimmed.origin.x >= 20 && trimmed.origin.y >= 14,
           "distort.trimmed-bounds");
    Expect(AlphaAt(trim.ActiveLayer()->asset->image, static_cast<int>(30 - trimmed.origin.x),
                   static_cast<int>(20 - trimmed.origin.y)) == 255,
           "distort.trimmed-pixels-stay");
}

// ── Selection (C9) ────────────────────────────────────────────────────────

[[nodiscard]] int CoverageAt(const std::optional<DocumentSelection>& selection, int width, int height, int x, int y)
{
    if (!selection.has_value())
    {
        return -1;
    }
    const Raster coverage = SelectionGeometry::Coverage(selection->loops, width, height, selection->antialiased);
    if (!coverage.HasStorage() || x < 0 || y < 0 || x >= width || y >= height)
    {
        return -1;
    }
    return coverage.Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                           static_cast<std::size_t>(x)];
}

[[nodiscard]] std::vector<Vec2> Square(double x, double y, double size)
{
    return {Vec2{x, y}, Vec2{x + size, y}, Vec2{x + size, y + size}, Vec2{x, y + size}};
}

[[nodiscard]] bool NearBounds(const std::optional<DocumentSelection>& selection, double x, double y, double width,
                              double height, double epsilon = 0.01)
{
    if (!selection.has_value())
    {
        return false;
    }
    const std::optional<SelectionBounds> bounds = selection->Bounds();
    return bounds.has_value() && Near(bounds->minX, x, epsilon) && Near(bounds->minY, y, epsilon) &&
           Near(bounds->Width(), width, epsilon) && Near(bounds->Height(), height, epsilon);
}

DocumentSession SelectionSession(int width = 100, int height = 100)
{
    DocumentSession session;
    session.CreateDocument(width, height, true);
    return session;
}

void Lasso(DocumentSession& session, const std::vector<Vec2>& points, SelectionMode mode = SelectionMode::New)
{
    session.BeginLasso(points[0], mode);
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        session.ExtendLasso(points[index]);
    }
    session.FinishLasso();
}

void Marquee(DocumentSession& session, Vec2 from, Vec2 to, SelectionMode mode = SelectionMode::New, bool square = false,
             bool fromCenter = false)
{
    session.BeginMarquee(from, mode);
    session.DragMarquee(to, square, fromCenter);
    session.FinishLasso();
}

void TestSelectionGeometry()
{
    const std::vector<SelectionLoop> a = SelectionGeometry::Rect(0, 0, 10, 10);
    const std::vector<SelectionLoop> b = SelectionGeometry::Rect(5, 5, 10, 10);
    const std::optional<SelectionBounds> united = SelectionGeometry::Bounds(SelectionGeometry::Union(a, b));
    Expect(united == SelectionBounds{0, 0, 15, 15} && SelectionGeometry::Union(a, b).size() == 1, "geometry.union");
    const std::vector<SelectionLoop> difference = SelectionGeometry::Subtract(a, b);
    Expect(SelectionGeometry::Bounds(difference) == SelectionBounds{0, 0, 10, 10} &&
               SelectionGeometry::Contains(difference, Vec2{2, 2}) &&
               !SelectionGeometry::Contains(difference, Vec2{7, 7}),
           "geometry.subtract");
    Expect(SelectionGeometry::Bounds(SelectionGeometry::Intersect(a, b)) == SelectionBounds{5, 5, 10, 10},
           "geometry.intersect");
    Expect(SelectionGeometry::Subtract(a, a).empty() && SelectionGeometry::Intersect(a, {}).empty(),
           "geometry.empty-results");
    // A hole: the ring keeps its center out under the nonzero rule, whichever way the inner loop runs.
    const std::vector<SelectionLoop> ring =
        SelectionGeometry::Subtract(SelectionGeometry::Rect(0, 0, 30, 30), SelectionGeometry::Rect(10, 10, 10, 10));
    Expect(ring.size() == 2 && !SelectionGeometry::Contains(ring, Vec2{15, 15}) &&
               SelectionGeometry::Contains(ring, Vec2{5, 15}) && SelectionGeometry::Contains(ring, Vec2{0, 0}),
           "geometry.ring-contains");
    const std::vector<SelectionLoop> grown = SelectionGeometry::Offset(SelectionGeometry::Rect(40, 40, 20, 20), 5);
    const std::optional<SelectionBounds> grownBounds = SelectionGeometry::Bounds(grown);
    Expect(grownBounds.has_value() && Near(grownBounds->minX, 35, 0.01) && Near(grownBounds->Width(), 30, 0.01) &&
               !SelectionGeometry::Contains(grown, Vec2{35.5, 35.5}) &&
               SelectionGeometry::Contains(grown, Vec2{50, 36}),
           "geometry.offset-rounds-corners");
    const std::optional<SelectionBounds> shrunk = SelectionGeometry::Bounds(SelectionGeometry::Offset(grown, -8));
    Expect(shrunk.has_value() && Near(shrunk->minX, 43, 0.01) && Near(shrunk->Width(), 14, 0.01),
           "geometry.offset-inward");
    Expect(SelectionGeometry::Offset(SelectionGeometry::Rect(0, 0, 10, 10), -6).empty(), "geometry.offset-collapses");
    const std::vector<SelectionLoop> oval = SelectionGeometry::Ellipse(10, 20, 60, 40);
    Expect(SelectionGeometry::Bounds(oval) == SelectionBounds{10, 20, 70, 60} &&
               SelectionGeometry::Contains(oval, Vec2{40, 40}) && !SelectionGeometry::Contains(oval, Vec2{11, 21}),
           "geometry.ellipse");
    const std::vector<SelectionLoop> moved = SelectionGeometry::Translated(a, 3, -4);
    Expect(SelectionGeometry::Bounds(moved) == SelectionBounds{3, -4, 13, 6}, "geometry.translated");
    Expect(SelectionGeometry::Bounds(SelectionGeometry::Transformed(a, Affine2::Scale(2, 3))) ==
               SelectionBounds{0, 0, 20, 30},
           "geometry.transformed");
    const SelectionBounds box = DragBox(Vec2{10, 10}, Vec2{40.4, 20.6}, false, false);
    Expect(box == SelectionBounds{10, 10, 40, 21}, "geometry.dragbox-rounds");
    Expect(DragBox(Vec2{10, 10}, Vec2{40, 20}, true, false) == SelectionBounds{10, 10, 40, 40},
           "geometry.dragbox-square");
    Expect(DragBox(Vec2{50, 50}, Vec2{60, 55}, false, true) == SelectionBounds{40, 45, 60, 55},
           "geometry.dragbox-centered");
    Expect(DragBox(Vec2{50, 50}, Vec2{45, 58}, true, true) == SelectionBounds{42, 42, 58, 58},
           "geometry.dragbox-square-centered");
    Expect(DragBox(Vec2{60, 70}, Vec2{20.2, 30.3}, false, false) == SelectionBounds{20, 30, 60, 70},
           "geometry.dragbox-any-direction");
}

void TestSelectionCoverage()
{
    const DocumentSelection rect{true, SelectionGeometry::Rect(20, 30, 40, 41)};
    Expect(CoverageAt(rect, 100, 100, 20, 30) == 255 && CoverageAt(rect, 100, 100, 19, 30) == 0 &&
               CoverageAt(rect, 100, 100, 59, 70) == 255 && CoverageAt(rect, 100, 100, 60, 70) == 0 &&
               CoverageAt(rect, 100, 100, 59, 71) == 0,
           "coverage.rect-exact");
    // A half-pixel edge covers half a pixel when antialiased and follows the center when not.
    const DocumentSelection half{true, SelectionGeometry::Rect(10.5, 10, 10, 10)};
    Expect(CoverageAt(half, 40, 40, 10, 15) == 128 && CoverageAt(half, 40, 40, 20, 15) == 128 &&
               CoverageAt(half, 40, 40, 15, 15) == 255,
           "coverage.half-pixel-edge");
    // Hard edges follow pixel centers: an edge at 10.3 takes pixel 10 (center 10.5), one at 20.3 leaves pixel 20.
    const DocumentSelection hardPart{false, SelectionGeometry::Rect(10.3, 10, 10, 10)};
    Expect(CoverageAt(hardPart, 40, 40, 9, 15) == 0 && CoverageAt(hardPart, 40, 40, 10, 15) == 255 &&
               CoverageAt(hardPart, 40, 40, 19, 15) == 255 && CoverageAt(hardPart, 40, 40, 20, 15) == 0,
           "coverage.hard-center-rule");
    // A center exactly on an edge counts for the left and top edges only, as Direct3D rasterizes.
    const DocumentSelection hardHalf{false, SelectionGeometry::Rect(10.5, 10, 10, 10)};
    Expect(CoverageAt(hardHalf, 40, 40, 10, 15) == 255 && CoverageAt(hardHalf, 40, 40, 20, 15) == 0,
           "coverage.hard-top-left-rule");
    // Legacy antialiasingControlsEdgeCoverage: pixels where x + y = 99 straddle the diagonal x + y = 100.
    const std::vector<SelectionLoop> triangle{{Vec2{0, 0}, Vec2{100, 0}, Vec2{0, 100}}};
    const DocumentSelection soft{true, triangle};
    const DocumentSelection hard{false, triangle};
    bool partial = false;
    bool onlyHard = true;
    for (int x = 0; x < 100; ++x)
    {
        const int value = CoverageAt(soft, 100, 100, x, 99 - x);
        partial = partial || (value > 0 && value < 255);
        const int hardValue = CoverageAt(hard, 100, 100, x, 99 - x);
        onlyHard = onlyHard && (hardValue == 0 || hardValue == 255);
    }
    Expect(partial && CoverageAt(soft, 100, 100, 10, 10) == 255 && CoverageAt(soft, 100, 100, 90, 90) == 0,
           "coverage.antialiased-diagonal");
    Expect(onlyHard && CoverageAt(hard, 100, 100, 10, 10) == 255, "coverage.hard-diagonal");
    // Pixels straddling the diagonal are exactly half covered: (x, 99 - x) has x + y + 1 = 100 through its center.
    Expect(CoverageAt(soft, 100, 100, 30, 69) == 128, "coverage.diagonal-half");
    // Off-canvas geometry still covers the canvas pixels it overlaps.
    const DocumentSelection outside{true, SelectionGeometry::Rect(-15, 10, 20, 20)};
    Expect(CoverageAt(outside, 100, 100, 0, 20) == 255 && CoverageAt(outside, 100, 100, 4, 20) == 255 &&
               CoverageAt(outside, 100, 100, 5, 20) == 0,
           "coverage.off-canvas");
    // A hole and an overlapping second loop under the nonzero rule.
    const DocumentSelection ring{true, SelectionGeometry::Subtract(SelectionGeometry::Rect(0, 0, 30, 30),
                                                                   SelectionGeometry::Rect(10, 10, 10, 10))};
    Expect(CoverageAt(ring, 30, 30, 15, 15) == 0 && CoverageAt(ring, 30, 30, 5, 15) == 255 &&
               CoverageAt(ring, 30, 30, 9, 9) == 255 && CoverageAt(ring, 30, 30, 10, 10) == 0,
           "coverage.ring");
    Expect(!SelectionGeometry::Coverage({}, 0, 4, true).HasStorage() &&
               CoverageAt(DocumentSelection{}, 4, 4, 1, 1) == 0,
           "coverage.empty");
}

void TestSelectionSession()
{
    // Legacy replaceAddAndSubtractCombineOutlines.
    DocumentSession session = SelectionSession();
    Lasso(session, Square(10, 10, 40));
    Expect(CoverageAt(session.Selection(), 100, 100, 30, 30) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 70, 70) == 0,
           "selection.replace");
    Lasso(session, Square(50, 50, 40), SelectionMode::Add);
    Expect(CoverageAt(session.Selection(), 100, 100, 30, 30) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 70, 70) == 255,
           "selection.add");
    Lasso(session, Square(20, 20, 20), SelectionMode::Subtract);
    Expect(CoverageAt(session.Selection(), 100, 100, 30, 30) == 0 &&
               CoverageAt(session.Selection(), 100, 100, 15, 15) == 255,
           "selection.subtract");
    Lasso(session, Square(60, 10, 20));
    Expect(CoverageAt(session.Selection(), 100, 100, 70, 20) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 70, 70) == 0 &&
               CoverageAt(session.Selection(), 100, 100, 15, 15) == 0,
           "selection.replace-again");
    // Legacy modifiersPickModeAndSelectionIsClippedToCanvas.
    Expect(session.SelectionModeFor(false, false) == SelectionMode::New &&
               session.SelectionModeFor(true, false) == SelectionMode::Add &&
               session.SelectionModeFor(true, true) == SelectionMode::Subtract &&
               session.SelectionModeFor(false, true) == SelectionMode::Subtract,
           "selection.modifiers");
    Lasso(session, Square(-50, -50, 100));
    Expect(NearBounds(session.Selection(), 0, 0, 50, 50), "selection.clipped-to-canvas");
    // Legacy emptySelectionIsDistinctFromNoSelection.
    DocumentSession empty = SelectionSession();
    Lasso(empty, Square(0, 0, 50), SelectionMode::Subtract);
    Expect(!empty.Selection().has_value(), "selection.subtract-from-nothing");
    Lasso(empty, Square(10, 10, 20));
    Lasso(empty, Square(0, 0, 60), SelectionMode::Subtract);
    Expect(empty.Selection().has_value() && empty.Selection()->IsEmpty() &&
               CoverageAt(empty.Selection(), 100, 100, 20, 20) == 0,
           "selection.empty-is-distinct");
    empty.Deselect();
    Expect(!empty.Selection().has_value(), "selection.deselect");
    // Legacy clickDeselectsAndSelectionStepsUndo.
    DocumentSession steps = SelectionSession();
    const int count = steps.history.UndoCount();
    Lasso(steps, Square(10, 10, 40));
    Expect(steps.history.UndoCount() == count + 1 && steps.history.UndoName() == L"Lasso", "selection.undo-entry");
    Lasso(steps, {Vec2{5, 5}});
    Expect(!steps.Selection().has_value() && steps.history.UndoName() == L"Deselect", "selection.click-deselects");
    steps.Undo();
    Expect(steps.Selection().has_value() && !steps.Selection()->IsEmpty(), "selection.undo-restores");
    steps.Undo();
    Expect(!steps.Selection().has_value(), "selection.undo-again");
    steps.Redo();
    Expect(CoverageAt(steps.Selection(), 100, 100, 30, 30) == 255, "selection.redo");
    // Legacy polygonalCornersCanBeRemovedAndClosed.
    DocumentSession polygon = SelectionSession();
    polygon.lassoKind = LassoKind::Polygonal;
    polygon.BeginLasso(Vec2{10, 10}, SelectionMode::New);
    polygon.ExtendLasso(Vec2{90, 10});
    polygon.ExtendLasso(Vec2{50, 50}); // Misplaced corner.
    polygon.RemoveLastLassoPoint();
    polygon.ExtendLasso(Vec2{90, 90});
    polygon.ExtendLasso(Vec2{10, 90});
    Expect(polygon.PendingLasso().has_value() && polygon.PendingLasso()->points.size() == 4 &&
               polygon.PendingLasso()->kind == LassoKind::Polygonal,
           "selection.polygonal-points");
    polygon.FinishLasso();
    Expect(polygon.history.UndoName() == L"Polygonal Lasso" &&
               CoverageAt(polygon.Selection(), 100, 100, 80, 80) == 255 &&
               CoverageAt(polygon.Selection(), 100, 100, 5, 50) == 0,
           "selection.polygonal-closed");
    polygon.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    polygon.CancelLasso();
    Expect(!polygon.PendingLasso().has_value() && polygon.Selection().has_value() && !polygon.Selection()->IsEmpty(),
           "selection.cancel-keeps");
    polygon.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    polygon.RemoveLastLassoPoint();
    Expect(!polygon.PendingLasso().has_value(), "selection.remove-last-point-ends-draft");
    // Points closer than a quarter pixel are skipped.
    polygon.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    polygon.ExtendLasso(Vec2{1.1, 1.1});
    polygon.ExtendLasso(Vec2{5, 5});
    Expect(polygon.PendingLasso()->points.size() == 2, "selection.skips-close-points");
    polygon.CancelLasso();
    // Legacy antialiasingControlsEdgeCoverage: the flag at the time of the outline sticks to it.
    DocumentSession aliased = SelectionSession();
    const std::vector<Vec2> triangle{Vec2{0, 0}, Vec2{100, 0}, Vec2{0, 100}};
    Lasso(aliased, triangle);
    Expect(aliased.Selection()->antialiased, "selection.antialiased-default");
    aliased.selectionAntialiased = false;
    Lasso(aliased, triangle);
    Expect(!aliased.Selection()->antialiased && CoverageAt(aliased.Selection(), 100, 100, 30, 68) == 255 &&
               CoverageAt(aliased.Selection(), 100, 100, 30, 70) == 0 &&
               CoverageAt(aliased.Selection(), 100, 100, 70, 30) == 0,
           "selection.hard-edges");
    // Legacy selectAllInverseAndToolSwitchCancelsDraft.
    DocumentSession all = SelectionSession();
    all.SelectAll();
    Expect(all.history.UndoName() == L"Select All" && CoverageAt(all.Selection(), 100, 100, 0, 0) == 255 &&
               CoverageAt(all.Selection(), 100, 100, 99, 99) == 255,
           "selection.select-all");
    Lasso(all, Square(0, 0, 50));
    all.InvertSelection();
    Expect(all.history.UndoName() == L"Inverse" && CoverageAt(all.Selection(), 100, 100, 25, 25) == 0 &&
               CoverageAt(all.Selection(), 100, 100, 75, 75) == 255,
           "selection.inverse");
    // Legacy cursorBadgeFollowsModifiersButKeepsAnOutlinesStartingMode.
    DocumentSession modes = SelectionSession();
    Expect(modes.LassoCursorMode(false, false) == SelectionMode::New &&
               modes.LassoCursorMode(true, false) == SelectionMode::Add &&
               modes.LassoCursorMode(false, true) == SelectionMode::Subtract,
           "selection.cursor-mode");
    modes.selectionModeChoice = SelectionMode::Add;
    Expect(modes.LassoCursorMode(false, false) == SelectionMode::Add, "selection.cursor-choice");
    modes.BeginLasso(Vec2{5, 5}, SelectionMode::Subtract);
    Expect(modes.LassoCursorMode(false, false) == SelectionMode::Subtract &&
               modes.LassoCursorMode(true, false) == SelectionMode::Subtract,
           "selection.cursor-keeps-draft-mode");
    modes.CancelLasso();
    modes.selectionModeChoice = SelectionMode::New;
    Expect(modes.UpdateHeldSelectionKeys(true, false) && modes.DisplayedSelectionMode() == SelectionMode::Add &&
               modes.selectionModeChoice == SelectionMode::New,
           "selection.held-shift");
    Expect(modes.UpdateHeldSelectionKeys(true, true) && modes.DisplayedSelectionMode() == SelectionMode::Subtract,
           "selection.held-option");
    Expect(modes.UpdateHeldSelectionKeys(false, false) && modes.DisplayedSelectionMode() == SelectionMode::New &&
               !modes.UpdateHeldSelectionKeys(false, false),
           "selection.held-released");
    // A pending transform blocks selection edits; Undo drops an outline in progress.
    DocumentSession blocked = SelectionSession();
    InsertPainted(blocked, 20, 20, Vec2{0, 0});
    blocked.BeginTransform(true);
    Expect(!blocked.CanEditSelection(), "selection.transform-blocks");
    blocked.SelectAll();
    Expect(!blocked.Selection().has_value(), "selection.transform-blocks-select-all");
    blocked.CancelTransform();
    blocked.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    blocked.Undo();
    Expect(!blocked.PendingLasso().has_value(), "selection.undo-drops-draft");
    // No document: nothing happens.
    DocumentSession none;
    none.SelectAll();
    none.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    Expect(!none.Selection().has_value() && !none.PendingLasso().has_value() && !none.CanModifySelection(),
           "selection.no-document");
}

void TestSelectionMove()
{
    // Legacy draggingMovesTheOutlineInWholePixelsAsOneUndo.
    DocumentSession session = SelectionSession();
    Lasso(session, Square(10, 10, 20));
    Expect(session.CanMoveSelection(Vec2{20, 20}) && !session.CanMoveSelection(Vec2{60, 60}), "move.can-move-inside");
    const int count = session.history.UndoCount();
    Expect(session.BeginSelectionMove(), "move.begin");
    session.MoveSelection(10.4, 29.6);
    session.MoveSelection(40.2, 40.4);
    session.EndSelectionMove();
    Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Move Selection",
           "move.one-entry");
    Expect(NearBounds(session.Selection(), 50, 50, 20, 20) &&
               CoverageAt(session.Selection(), 100, 100, 55, 55) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 15, 15) == 0,
           "move.whole-pixels");
    session.Undo();
    Expect(NearBounds(session.Selection(), 10, 10, 20, 20), "move.undo");
    // Legacy movingOffCanvasAndBackKeepsTheWholeShape.
    Expect(session.BeginSelectionMove(), "move.begin-off");
    session.MoveSelection(-25, 0);
    session.EndSelectionMove();
    Expect(CoverageAt(session.Selection(), 100, 100, 0, 20) == 255 && NearBounds(session.Selection(), -15, 10, 20, 20),
           "move.off-canvas-not-clipped");
    Expect(session.BeginSelectionMove(), "move.begin-back");
    session.MoveSelection(25, 0);
    session.EndSelectionMove();
    Expect(NearBounds(session.Selection(), 10, 10, 20, 20), "move.back-intact");
    // Legacy arrowNudgesAndMoveIsOnlyForNewModeOnARealSelection.
    DocumentSession nudge = SelectionSession();
    Expect(!nudge.BeginSelectionMove(), "move.nothing-selected");
    Lasso(nudge, Square(10, 10, 20));
    const int before = nudge.history.UndoCount();
    nudge.NudgeSelection(1, 0);
    nudge.NudgeSelection(0, -10);
    Expect(NearBounds(nudge.Selection(), 11, 0, 20, 20) && nudge.history.UndoCount() == before + 2, "move.nudge");
    nudge.BeginLasso(Vec2{5, 5}, SelectionMode::Add);
    Expect(!nudge.CanMoveSelection(Vec2{20, 10}), "move.not-while-drafting");
    nudge.CancelLasso();
    Lasso(nudge, Square(0, 0, 60), SelectionMode::Subtract);
    Expect(nudge.Selection()->IsEmpty() && !nudge.BeginSelectionMove() && !nudge.IsMovingSelection(),
           "move.empty-cannot-move");
    // A second BeginSelectionMove during a move is refused; MoveSelection without one does nothing.
    DocumentSession twice = SelectionSession();
    Lasso(twice, Square(10, 10, 20));
    twice.MoveSelection(5, 5);
    Expect(NearBounds(twice.Selection(), 10, 10, 20, 20), "move.needs-begin");
    Expect(twice.BeginSelectionMove() && !twice.BeginSelectionMove() && twice.IsMovingSelection(), "move.once");
    twice.EndSelectionMove();
    Expect(!twice.IsMovingSelection(), "move.ended");
}

void TestSelectionModify()
{
    // Legacy expandAndContractGrowAndShrinkTheOutline.
    DocumentSession session = SelectionSession();
    Expect(!session.CanModifySelection(), "modify.nothing");
    Lasso(session, Square(40, 40, 20));
    Expect(session.CanModifySelection(), "modify.can");
    session.ExpandSelection(5);
    Expect(session.history.UndoName() == L"Expand Selection" && NearBounds(session.Selection(), 35, 35, 30, 30) &&
               CoverageAt(session.Selection(), 100, 100, 37, 50) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 33, 50) == 0,
           "modify.expand");
    session.ContractSelection(8);
    Expect(session.history.UndoName() == L"Contract Selection" && NearBounds(session.Selection(), 43, 43, 14, 14),
           "modify.contract");
    session.Undo();
    Expect(NearBounds(session.Selection(), 35, 35, 30, 30), "modify.undo");
    // Legacy expandStaysOnCanvasAndContractCanEmptyTheSelection.
    DocumentSession edges = SelectionSession();
    edges.SelectAll();
    edges.ExpandSelection(10);
    Expect(NearBounds(edges.Selection(), 0, 0, 100, 100), "modify.expand-stays-on-canvas");
    edges.ContractSelection(10);
    Expect(CoverageAt(edges.Selection(), 100, 100, 5, 50) == 0 &&
               CoverageAt(edges.Selection(), 100, 100, 50, 50) == 255,
           "modify.contract-from-edges");
    edges.ContractSelection(45);
    Expect(edges.Selection().has_value() && edges.Selection()->IsEmpty() && !edges.CanModifySelection(),
           "modify.contract-empties");
    // Out-of-range amounts and drafts are refused.
    DocumentSession limits = SelectionSession();
    Lasso(limits, Square(40, 40, 20));
    const int count = limits.history.UndoCount();
    limits.ExpandSelection(0);
    limits.ExpandSelection(501);
    limits.ContractSelection(-1);
    limits.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    Expect(!limits.CanModifySelection(), "modify.draft-blocks");
    limits.ExpandSelection(5);
    limits.CancelLasso();
    Expect(limits.history.UndoCount() == count && NearBounds(limits.Selection(), 40, 40, 20, 20), "modify.limits");
}

// A mask with a black square (hidden) and a white square hole inside it, as the legacy maskedLayer fixture.
Guid MaskedLayer(DocumentSession& session)
{
    const Guid id = session.ActiveLayerID().value();
    std::vector<std::uint8_t> bytes(100u * 100u, 255);
    for (int y = 30; y < 70; ++y)
    {
        for (int x = 20; x < 60; ++x)
        {
            const bool hole = x >= 30 && x < 40 && y >= 40 && y < 50;
            bytes[static_cast<std::size_t>(y) * 100u + static_cast<std::size_t>(x)] = hole ? 255 : 0;
        }
    }
    const Raster mask = Raster::Make(100, 100, RasterFormat::Gray8, bytes.data(), bytes.size());
    for (ImageLayer& layer : session.Document().value().layers)
    {
        if (layer.id == id)
        {
            layer.mask = LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, true};
        }
    }
    return id;
}

void TestSelectionLoad()
{
    // Legacy cmdClickingAMaskSelectsItsBlackAreas.
    DocumentSession session = SelectionSession();
    const Guid id = MaskedLayer(session);
    Expect(session.LoadMaskSelection(id) && session.history.UndoName() == L"Load Mask Selection", "load.mask-entry");
    Expect(CoverageAt(session.Selection(), 100, 100, 25, 35) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 35, 45) == 0 &&
               CoverageAt(session.Selection(), 100, 100, 80, 80) == 0,
           "load.mask-black-areas");
    Expect(CoverageAt(session.Selection(), 100, 100, 59, 69) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 60, 70) == 0,
           "load.mask-exact-edges");
    Lasso(session, Square(80, 80, 10));
    Expect(session.LoadMaskSelection(id, SelectionMode::Add) &&
               CoverageAt(session.Selection(), 100, 100, 85, 85) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 25, 35) == 255,
           "load.mask-add");
    Expect(session.LoadMaskSelection(id, SelectionMode::Subtract) &&
               CoverageAt(session.Selection(), 100, 100, 85, 85) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 25, 35) == 0,
           "load.mask-subtract");
    // Legacy maskSelectionFollowsTheLayerTransformAndIgnoresAllWhiteMasks.
    DocumentSession scaled = SelectionSession(200, 200);
    const Guid scaledId = MaskedLayer(scaled);
    for (ImageLayer& layer : scaled.Document().value().layers)
    {
        if (layer.id == scaledId)
        {
            layer.transform.origin = Vec2{0, 0};
            layer.transform.size = Vec2{200, 200};
        }
    }
    Expect(scaled.LoadMaskSelection(scaledId) && NearBounds(scaled.Selection(), 40, 60, 80, 80),
           "load.mask-follows-transform");
    scaled.Deselect();
    scaled.AddBlankLayer();
    const Guid blank = scaled.ActiveLayerID().value();
    const Raster white = GrayRaster(1, 1, 255);
    for (ImageLayer& layer : scaled.Document().value().layers)
    {
        if (layer.id == blank)
        {
            layer.mask = LayerMask{RasterAsset{white, white, L"mask"}, true, std::nullopt, true};
        }
    }
    Expect(!scaled.LoadMaskSelection(blank) && !scaled.Selection().has_value(), "load.mask-all-white-ignored");
    Expect(!scaled.LoadMaskSelection(Guid::Generate()), "load.mask-unknown-layer");
    // Legacy cmdClickingALayerSelectsItsOpaquePixels: a 50 × 50 image, opaque ring around a transparent center,
    // one faint (25 %) corner pixel, placed at 2× scale.
    DocumentSession layers = SelectionSession(200, 200);
    std::vector<std::uint8_t> ring(50u * 50u * 4u, 0);
    for (int y = 10; y < 40; ++y)
    {
        for (int x = 10; x < 40; ++x)
        {
            if (x >= 20 && x < 30 && y >= 20 && y < 30)
            {
                continue;
            }
            std::uint8_t* pixel = ring.data() + (static_cast<std::size_t>(y) * 50u + static_cast<std::size_t>(x)) * 4u;
            pixel[2] = 255;
            pixel[3] = 255;
        }
    }
    ring[2] = 64;
    ring[3] = 64;
    const Raster ringRaster = Raster::Make(50, 50, RasterFormat::Bgra8888, ring.data(), ring.size());
    ImageLayer ringLayer = RasterLayer(50, 50, L"Ring");
    ringLayer.asset->image = ringRaster;
    ringLayer.asset->thumbnail = ringRaster;
    ringLayer.transform.origin = Vec2{50, 50};
    ringLayer.transform.size = Vec2{100, 100};
    layers.Document().value().layers.push_back(ringLayer);
    layers.SelectLayer(ringLayer.id);
    Expect(layers.LoadLayerSelection(ringLayer.id) && layers.history.UndoName() == L"Load Layer Selection" &&
               NearBounds(layers.Selection(), 70, 70, 60, 60),
           "load.layer-scaled-bounds");
    Expect(CoverageAt(layers.Selection(), 200, 200, 75, 75) == 255 &&
               CoverageAt(layers.Selection(), 200, 200, 100, 100) == 0 &&
               CoverageAt(layers.Selection(), 200, 200, 50, 50) == 0,
           "load.layer-opaque-threshold");
    Lasso(layers, Square(0, 0, 20));
    Expect(layers.LoadLayerSelection(ringLayer.id, SelectionMode::Add) &&
               CoverageAt(layers.Selection(), 200, 200, 10, 10) == 255 &&
               CoverageAt(layers.Selection(), 200, 200, 75, 75) == 255,
           "load.layer-add");
    layers.AddBlankLayer();
    const std::optional<DocumentSelection> before = layers.Selection();
    Expect(!layers.LoadLayerSelection(layers.ActiveLayerID().value()) && layers.Selection() == before,
           "load.blank-layer-selects-nothing");
    layers.AddGroup();
    Expect(!layers.LoadLayerSelection(layers.ActiveLayerID().value()), "load.group-selects-nothing");
    // A flipped placement maps the traced pixels through the flip.
    DocumentSession flipped = SelectionSession(200, 200);
    ImageLayer flippedLayer = RasterLayer(50, 50, L"Ring");
    flippedLayer.asset->image = ringRaster;
    flippedLayer.asset->thumbnail = ringRaster;
    flippedLayer.transform.origin = Vec2{0, 0};
    flippedLayer.transform.size = Vec2{50, 50};
    flippedLayer.transform.flipX = true;
    flipped.Document().value().layers.push_back(flippedLayer);
    flipped.SelectLayer(flippedLayer.id);
    Expect(flipped.LoadLayerSelection(flippedLayer.id) && CoverageAt(flipped.Selection(), 200, 200, 49, 0) == 0 &&
               CoverageAt(flipped.Selection(), 200, 200, 15, 15) == 255,
           "load.layer-flipped");
}

void TestMarquee()
{
    // Legacy marqueeDrawsWholePixelRectanglesInAnyDirection.
    DocumentSession session = SelectionSession();
    Marquee(session, Vec2{60.4, 70.6}, Vec2{20.2, 30.3});
    Expect(session.history.UndoName() == L"Rectangular Marquee" && NearBounds(session.Selection(), 20, 30, 40, 41),
           "marquee.whole-pixels");
    Expect(CoverageAt(session.Selection(), 100, 100, 20, 30) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 19, 30) == 0,
           "marquee.exact");
    Marquee(session, Vec2{80, 80}, Vec2{90, 90}, SelectionMode::Add);
    Expect(CoverageAt(session.Selection(), 100, 100, 85, 85) == 255 &&
               CoverageAt(session.Selection(), 100, 100, 40, 50) == 255,
           "marquee.add");
    Marquee(session, Vec2{30, 40}, Vec2{50, 60}, SelectionMode::Subtract);
    Expect(CoverageAt(session.Selection(), 100, 100, 40, 50) == 0 &&
               CoverageAt(session.Selection(), 100, 100, 25, 35) == 255,
           "marquee.subtract");
    Marquee(session, Vec2{5, 5}, Vec2{5, 5}); // A click deselects.
    Expect(!session.Selection().has_value(), "marquee.click-deselects");
    // Legacy marqueeShiftMakesSquaresAndCenteredDragsGrowFromTheAnchor.
    Marquee(session, Vec2{10, 10}, Vec2{40, 20}, SelectionMode::New, true);
    Expect(NearBounds(session.Selection(), 10, 10, 30, 30), "marquee.square");
    Marquee(session, Vec2{50, 50}, Vec2{60, 55}, SelectionMode::New, false, true);
    Expect(NearBounds(session.Selection(), 40, 45, 20, 10), "marquee.from-center");
    Marquee(session, Vec2{50, 50}, Vec2{45, 58}, SelectionMode::New, true, true);
    Expect(NearBounds(session.Selection(), 42, 42, 16, 16), "marquee.square-from-center");
    // Legacy marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle.
    DocumentSession oval = SelectionSession();
    oval.marqueeKind = LassoKind::Ellipse;
    oval.BeginMarquee(Vec2{10, 20}, SelectionMode::New);
    oval.DragMarquee(Vec2{70, 60}, false, false);
    Expect(oval.PendingLasso().has_value() && oval.PendingLasso()->kind == LassoKind::Ellipse &&
               oval.PendingLasso()->points.size() == 4,
           "marquee.ellipse-draft");
    oval.FinishLasso();
    Expect(oval.history.UndoName() == L"Elliptical Marquee" && NearBounds(oval.Selection(), 10, 20, 60, 40, 0.5),
           "marquee.ellipse-bounds");
    Expect(CoverageAt(oval.Selection(), 100, 100, 40, 40) == 255 && CoverageAt(oval.Selection(), 100, 100, 11, 21) == 0,
           "marquee.ellipse-coverage");
    oval.BeginMarquee(Vec2{5, 5}, SelectionMode::New);
    oval.DragMarquee(Vec2{45, 25}, true, false);
    oval.FinishLasso();
    const std::optional<SelectionBounds> circle = oval.Selection()->Bounds();
    Expect(circle.has_value() && Near(circle->Width(), circle->Height(), 0.5), "marquee.circle");
    oval.ToggleMarqueeKind();
    Expect(oval.marqueeKind == LassoKind::Rectangle, "marquee.toggle-kind");
    oval.BeginMarquee(Vec2{5, 5}, SelectionMode::New);
    oval.ToggleLassoKind();
    Expect(oval.lassoKind == LassoKind::Polygonal && !oval.PendingLasso().has_value(), "marquee.toggle-cancels-draft");
    // The Marquee never extends like a lasso; a drag without a marquee draft does nothing.
    DocumentSession stray = SelectionSession();
    stray.DragMarquee(Vec2{10, 10}, false, false);
    stray.BeginLasso(Vec2{1, 1}, SelectionMode::New);
    stray.DragMarquee(Vec2{50, 50}, false, false);
    Expect(stray.PendingLasso()->points.size() == 1, "marquee.drag-needs-marquee-draft");
}

// Premultiplied BGRA pixels, top row first.
[[nodiscard]] Raster PixelImage(int width, int height,
                                const std::function<std::array<std::uint8_t, 4>(int, int)>& color)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::array<std::uint8_t, 4> pixel = color(x, y);
            std::uint8_t* out =
                bytes.data() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                out[channel] = pixel[static_cast<std::size_t>(channel)];
            }
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

// Pixel indices (y × width + x) an outline covers, hard-edged.
[[nodiscard]] std::set<int> Pixels(const std::optional<std::vector<SelectionLoop>>& outline, int width, int height)
{
    std::set<int> result;
    if (!outline.has_value())
    {
        return result;
    }
    const Raster coverage = SelectionGeometry::Coverage(outline.value(), width, height, false);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (coverage.Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                                static_cast<std::size_t>(x)] >= 128)
            {
                result.insert(y * width + x);
            }
        }
    }
    return result;
}

[[nodiscard]] std::set<int> Block(int column0, int column1, int row0, int row1, int width)
{
    std::set<int> result;
    for (int y = row0; y < row1; ++y)
    {
        for (int x = column0; x < column1; ++x)
        {
            result.insert(y * width + x);
        }
    }
    return result;
}

[[nodiscard]] WandSettings Wand(int tolerance = 32, WandSampleSize size = WandSampleSize::Point, bool contiguous = true)
{
    WandSettings settings;
    settings.tolerance = tolerance;
    settings.sampleSize = size;
    settings.contiguous = contiguous;
    return settings;
}

void TestWandKernel()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    // Legacy contiguousStopsAtOtherColorsWhileNonContiguousFindsEveryMatch.
    const Raster stripes = PixelImage(10, 4, [&](int x, int) { return x < 3 || x >= 6 ? red : blue; });
    const std::set<int> left = Block(0, 3, 0, 4, 10);
    std::set<int> both = left;
    both.merge(Block(6, 10, 0, 4, 10));
    Expect(Pixels(MagicWand::Select(stripes, Vec2{1.5, 2.5}, Wand()), 10, 4) == left, "wand.contiguous");
    Expect(Pixels(MagicWand::Select(stripes, Vec2{1.5, 2.5}, Wand(32, WandSampleSize::Point, false)), 10, 4) == both,
           "wand.everywhere");
    const Raster banded = PixelImage(4, 3, [&](int, int y) { return y == 0 ? red : blue; });
    Expect(Pixels(MagicWand::Select(banded, Vec2{1, 0}, Wand()), 4, 3) == std::set<int>{0, 1, 2, 3},
           "wand.rows-upright");
    Expect(!MagicWand::Select(banded, Vec2{9, 0}, Wand()).has_value() &&
               !MagicWand::Select(banded, Vec2{-1, 0}, Wand()).has_value(),
           "wand.outside-image");
    // Legacy toleranceAppliesToEveryChannelIncludingAlpha.
    const std::array<std::array<std::uint8_t, 4>, 4> columns{
        std::array<std::uint8_t, 4>{100, 100, 100, 255}, std::array<std::uint8_t, 4>{132, 100, 100, 255},
        std::array<std::uint8_t, 4>{133, 100, 100, 255}, std::array<std::uint8_t, 4>{100, 100, 100, 222}};
    const Raster row = PixelImage(4, 1, [&](int x, int) { return columns[static_cast<std::size_t>(x)]; });
    const auto run = [&](int tolerance)
    { return Pixels(MagicWand::Select(row, Vec2{0.5, 0.5}, Wand(tolerance, WandSampleSize::Point, false)), 4, 1); };
    Expect(run(0) == std::set<int>{0} && run(32) == std::set<int>{0, 1} && run(33) == std::set<int>{0, 1, 2, 3},
           "wand.tolerance-per-channel");
    // Legacy sampleSizeAveragesThePixelsAroundTheClick.
    const Raster dot = PixelImage(5, 5,
                                  [](int x, int y)
                                  {
                                      return x == 2 && y == 2 ? std::array<std::uint8_t, 4>{255, 255, 255, 255}
                                                              : std::array<std::uint8_t, 4>{0, 0, 0, 255};
                                  });
    Expect(Pixels(MagicWand::Select(dot, Vec2{2.5, 2.5}, Wand(10, WandSampleSize::Point, false)), 5, 5) ==
               std::set<int>{12},
           "wand.point-sample");
    // A 3 × 3 average is gray 28: black is within 30 of it, the white center is not.
    std::set<int> allButCenter = Block(0, 5, 0, 5, 5);
    allButCenter.erase(12);
    Expect(Pixels(MagicWand::Select(dot, Vec2{2.5, 2.5}, Wand(30, WandSampleSize::ThreeByThree, false)), 5, 5) ==
               allButCenter,
           "wand.averaged-sample");
    Expect(Wand(30, WandSampleSize::FiveByFive).Radius() == 2, "wand.sample-radius");
    // Legacy outlinesReproduceTheirPixelsWithHolesAndCornerTouches.
    const int width = 8;
    const int height = 6;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(width * height), 0);
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            if (!(x == 1 && y == 1))
            {
                mask[static_cast<std::size_t>(y * width + x)] = 255;
            }
        }
    }
    mask[static_cast<std::size_t>(4 * width + 5)] = 255;
    mask[static_cast<std::size_t>(5 * width + 6)] = 255;
    std::set<int> expected;
    for (int index = 0; index < width * height; ++index)
    {
        if (mask[static_cast<std::size_t>(index)] != 0)
        {
            expected.insert(index);
        }
    }
    const std::optional<std::vector<SelectionLoop>> traced = MagicWand::Trace(mask, width, height);
    Expect(traced.has_value() && Pixels(traced, width, height) == expected, "wand.trace-holes-and-corners");
    // Outer loops run clockwise (positive signed area in y-down coordinates), holes counterclockwise.
    bool orientation = traced.has_value() && traced->size() == 4;
    if (orientation)
    {
        int positive = 0;
        for (const SelectionLoop& loop : traced.value())
        {
            double area = 0;
            for (std::size_t index = 0; index < loop.size(); ++index)
            {
                const Vec2 a = loop[index];
                const Vec2 b = loop[(index + 1) % loop.size()];
                area += a.x * b.y - b.x * a.y;
            }
            positive += area > 0 ? 1 : 0;
        }
        orientation = positive == 3;
    }
    Expect(orientation, "wand.trace-orientation");
    Expect(!MagicWand::Trace(std::vector<std::uint8_t>(4, 0), 2, 2).has_value() &&
               !MagicWand::Trace(std::vector<std::uint8_t>(3, 255), 2, 2).has_value(),
           "wand.trace-empty");
    WandFailure failure = WandFailure::TooDetailed;
    Expect(MagicWand::Trace(std::vector<std::uint8_t>(4, 255), 2, 2, &failure).has_value() &&
               failure == WandFailure::None,
           "wand.trace-reports-no-failure");
    // MaskTracing thresholds: dark (< 128) for masks, opaque (>= 128 alpha) for layers.
    const Raster gray = Gray(2, 2, {0, 127, 128, 255});
    Expect(Pixels(MaskTracing::DarkPixels(gray), 2, 2) == std::set<int>{0, 1}, "wand.mask-dark-threshold");
    const Raster alpha = PixelImage(
        2, 2,
        [](int x, int y)
        {
            const std::uint8_t a = static_cast<std::uint8_t>(x + 2 * y == 0 ? 127 : x + 2 * y == 1 ? 128 : 255);
            return std::array<std::uint8_t, 4>{a, 0, 0, a};
        });
    Expect(Pixels(MaskTracing::OpaquePixels(alpha), 2, 2) == std::set<int>{1, 2, 3}, "wand.layer-opaque-threshold");
    Expect(!MaskTracing::DarkPixels(Raster{}).has_value() && !MaskTracing::OpaquePixels(gray).has_value(),
           "wand.tracing-rejects-formats");
    // The layer sampler places pixels 1:1 at whole offsets and honors a flip.
    LayerTransform placement;
    placement.origin = Vec2{2, 1};
    placement.size = Vec2{4, 3};
    const Raster halves = PixelImage(4, 3, [&](int x, int) { return x < 2 ? red : blue; });
    const Raster placed = RenderLayerPixels(halves, placement, 8, 6);
    const auto pixelAt = [&](const Raster& image, int x, int y)
    {
        const std::uint8_t* p = image.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride()) +
                                static_cast<std::size_t>(x) * 4u;
        return std::array<std::uint8_t, 4>{p[0], p[1], p[2], p[3]};
    };
    Expect(placed.Width() == 8 && pixelAt(placed, 2, 1) == red && pixelAt(placed, 5, 3) == blue &&
               pixelAt(placed, 1, 1)[3] == 0 && pixelAt(placed, 6, 1)[3] == 0 && pixelAt(placed, 3, 4)[3] == 0,
           "wand.sampler-places-pixels");
    placement.flipX = true;
    const Raster flipped = RenderLayerPixels(halves, placement, 8, 6);
    Expect(pixelAt(flipped, 2, 1) == blue && pixelAt(flipped, 5, 3) == red, "wand.sampler-flips");
    placement.flipX = false;
    placement.size = Vec2{8, 6};
    placement.origin = Vec2{0, 0};
    const Raster scaled = RenderLayerPixels(halves, placement, 8, 6);
    Expect(pixelAt(scaled, 0, 0) == red && pixelAt(scaled, 7, 5) == blue && pixelAt(scaled, 3, 2)[0] > 0 &&
               pixelAt(scaled, 3, 2)[2] > 0,
           "wand.sampler-scales-bilinear");
}

void TestWandSession()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    // Legacy theWandReadsTheActiveLayerOrEveryVisibleLayerAndCombinesModes.
    DocumentSession session;
    session.CreateDocument(20, 10, false);
    const Raster halves = PixelImage(20, 10, [&](int x, int) { return x < 10 ? red : blue; });
    ImageLayer layer = RasterLayer(20, 10, L"Halves");
    layer.asset->image = halves;
    layer.asset->thumbnail = halves;
    session.Document().value().layers.push_back(layer);
    session.SelectLayer(layer.id);
    session.AddBlankLayer();
    const std::set<int> left = Block(0, 10, 0, 10, 20);
    const auto selected = [&]
    { return session.Selection().has_value() ? Pixels(session.Selection()->loops, 20, 10) : std::set<int>{}; };
    // The blank active layer is transparent everywhere, so the whole canvas matches.
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::New) == WandFailure::None && selected().size() == 200 &&
               session.history.UndoName() == L"Magic Wand",
           "wandsession.blank-layer-matches-all");
    session.wandSettings.sampleAllLayers = true;
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::New) == WandFailure::None && selected().size() == 200,
           "wandsession.all-layers-needs-composite");
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::New, &halves) == WandFailure::None && selected() == left,
           "wandsession.all-layers");
    const int count = session.history.UndoCount();
    Expect(session.MagicWand(Vec2{15, 5}, SelectionMode::Add, &halves) == WandFailure::None &&
               selected().size() == 200 && session.history.UndoCount() == count + 1,
           "wandsession.add");
    std::set<int> right = Block(10, 20, 0, 10, 20);
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::Subtract, &halves) == WandFailure::None && selected() == right,
           "wandsession.subtract");
    session.Undo();
    Expect(selected().size() == 200, "wandsession.undo");
    // The active layer's own pixels, placed by its transform.
    session.wandSettings.sampleAllLayers = false;
    session.SelectLayer(layer.id);
    Expect(session.MagicWand(Vec2{15, 5}, SelectionMode::New) == WandFailure::None && selected() == right,
           "wandsession.active-layer");
    // Nothing matched in New mode deselects; outside the canvas nothing happens.
    session.wandSettings.tolerance = 0;
    session.wandSettings.sampleAllLayers = true;
    const Raster clear = PixelImage(20, 10, [](int, int) { return std::array<std::uint8_t, 4>{0, 0, 0, 0}; });
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::New, &halves) == WandFailure::None && selected() == left,
           "wandsession.tolerance-zero");
    const int before = session.history.UndoCount();
    Expect(session.MagicWand(Vec2{25, 5}, SelectionMode::New, &halves) == WandFailure::None &&
               session.history.UndoCount() == before,
           "wandsession.outside-canvas");
    static_cast<void>(clear);
    // A composite of the wrong size is refused rather than read.
    const Raster wrongSize = PixelImage(4, 4, [](int, int) { return std::array<std::uint8_t, 4>{0, 0, 0, 0}; });
    Expect(session.MagicWand(Vec2{2, 2}, SelectionMode::New, &wrongSize) == WandFailure::None && selected() == left,
           "wandsession.wrong-composite-ignored");
    // Add mode clips to the canvas and keeps the selection; the header defaults match the legacy settings.
    Expect(WandSettings{} == WandSettings{32, WandSampleSize::Point, true, false}, "wandsession.defaults");
}

// ── Brush (C10) ───────────────────────────────────────────────────────────

[[nodiscard]] std::array<std::uint8_t, 4> PixelOf(const Raster& raster, int x, int y) noexcept
{
    std::array<std::uint8_t, 4> out{0, 0, 0, 0};
    if (!raster.HasStorage() || x < 0 || y < 0 || x >= raster.Width() || y >= raster.Height())
    {
        return out;
    }
    raster.ReadRegion(x, y, 1, 1, out.data(), 4, 0);
    return out;
}

// The active layer's pixel under a document point (nearest), transparent outside its pixels.
[[nodiscard]] std::array<std::uint8_t, 4> LayerPixel(const DocumentSession& session, double x, double y) noexcept
{
    const ImageLayer* layer = session.ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value())
    {
        return {0, 0, 0, 0};
    }
    const Raster& image = layer->asset->image;
    const Vec2 pixel = PixelToDocument(layer->transform, image.Width(), image.Height()).Inverted().Apply(Vec2{x, y});
    return PixelOf(image, static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)));
}

[[nodiscard]] std::uint8_t MaskValue(const DocumentSession& session, double x, double y) noexcept
{
    const ImageLayer* layer = session.ActiveLayer();
    if (layer == nullptr || !layer->mask.has_value())
    {
        return 0;
    }
    const Raster& mask = layer->mask->asset.image;
    const LayerTransform placement = MaskTransform(*layer);
    const Vec2 pixel = PixelToDocument(placement, mask.Width(), mask.Height()).Inverted().Apply(Vec2{x, y});
    std::uint8_t value = 255;
    mask.ReadRegion(static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)), 1, 1, &value, 1, 255);
    return value;
}

// A pending stroke's pixel under a document point: the tile's composed image, its original pixels, or the fill.
[[nodiscard]] std::array<std::uint8_t, 4> StrokePixel(const BrushStroke& stroke, double x, double y) noexcept
{
    const Vec2 grid = stroke.PixelToDocument().Inverted().Apply(Vec2{x, y});
    const int gx = static_cast<int>(std::floor(grid.x));
    const int gy = static_cast<int>(std::floor(grid.y));
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        if (gx >= tile.rect.x && gy >= tile.rect.y && gx < tile.rect.x + tile.rect.width &&
            gy < tile.rect.y + tile.rect.height)
        {
            if (stroke.IsMask())
            {
                std::array<std::uint8_t, 4> out{0, 0, 0, 0};
                tile.image.ReadRegion(gx - tile.rect.x, gy - tile.rect.y, 1, 1, out.data(), 1, 255);
                out[3] = out[0];
                return out;
            }
            return PixelOf(tile.image, gx - tile.rect.x, gy - tile.rect.y);
        }
    }
    std::array<std::uint8_t, 4> out{0, 0, 0, 0};
    if (stroke.SourceImage().HasStorage())
    {
        stroke.SourceImage().ReadRegion(gx - stroke.SourceRect().x, gy - stroke.SourceRect().y, 1, 1, out.data(),
                                        stroke.IsMask() ? 1 : 4, stroke.FillValue());
        if (stroke.IsMask())
        {
            out[3] = out[0];
        }
    }
    return out;
}

DocumentSession BrushSession(int width = 600, int height = 80)
{
    DocumentSession session;
    session.CreateDocument(width, height, false);
    session.AddBlankLayer();
    session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1, false};
    return session;
}

[[nodiscard]] bool IsRed(const std::array<std::uint8_t, 4>& pixel) noexcept
{
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255;
}

void TestSparseRaster()
{
    // A base with a tile over part of it: reads resolve without assembling the pixels.
    std::vector<std::uint8_t> gradient(16);
    for (std::size_t i = 0; i < gradient.size(); ++i)
    {
        gradient[i] = static_cast<std::uint8_t>(i * 10);
    }
    const Raster base = Raster::Make(4, 4, RasterFormat::Gray8, gradient.data(), gradient.size());
    const std::vector<std::uint8_t> tileBytes{200, 201, 202, 203};
    const Raster tile = Raster::Make(2, 2, RasterFormat::Gray8, tileBytes.data(), tileBytes.size());
    const Raster sparse = Raster::MakeSparse(4, 4, RasterFormat::Gray8, base, 0, 0, {RasterTile{1, 1, tile}}, 0);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && sparse.ByteSize() == 0 && sparse.Tiles().size() == 1,
           "sparse.made");
    std::array<std::uint8_t, 16> read{};
    sparse.ReadRegion(0, 0, 4, 4, read.data(), 4, 0);
    Expect(read[0] == 0 && read[5] == 200 && read[6] == 201 && read[9] == 202 && read[10] == 203 && read[15] == 150 &&
               !sparse.IsMaterialized(),
           "sparse.read-region-without-materializing");
    const Raster thumb = Raster::MakeThumbnail(sparse, 2);
    Expect(thumb.Width() == 2 && !sparse.IsMaterialized(), "sparse.thumbnail-without-materializing");
    const std::uint8_t* data = sparse.Data();
    Expect(data != nullptr && sparse.IsMaterialized() && data[5] == 200 && data[15] == 150 && sparse.ByteSize() == 16,
           "sparse.materialize-on-data");
    int storages = 0;
    sparse.ForEachStorage([&](const Raster&) { ++storages; });
    Expect(storages == 3, "sparse.for-each-storage");
    // Masks reveal beyond their pixels: the fill is white.
    const std::uint8_t black = 0;
    const Raster dot = Raster::Make(1, 1, RasterFormat::Gray8, &black, 1);
    const Raster mask = Raster::MakeSparse(3, 3, RasterFormat::Gray8, dot, 1, 1, {}, 255);
    std::array<std::uint8_t, 9> maskRead{};
    mask.ReadRegion(0, 0, 3, 3, maskRead.data(), 3, 255);
    Expect(maskRead[0] == 255 && maskRead[4] == 0 && maskRead[8] == 255, "sparse.mask-fill-white");
    // Replacing: newer tiles split older ones, the crop shifts everything, the base is shared.
    const Raster red = SolidRaster(8, 8, 0, 0, 255, 255);
    const Raster green = SolidRaster(4, 4, 0, 255, 0, 255);
    const Raster first = Raster::Replacing(red, 0, 0, {RasterTile{2, 2, green}}, 1, 1, 6, 6, 0);
    Expect(first.Width() == 6 && first.IsSparse() && first.Base() == red && first.BaseX() == -1 &&
               first.Tiles().size() == 1,
           "sparse.replacing-crops");
    Expect(IsRed(PixelOf(first, 0, 0)) && PixelOf(first, 1, 1)[1] == 255 && PixelOf(first, 4, 4)[1] == 255 &&
               IsRed(PixelOf(first, 5, 5)) && !first.IsMaterialized(),
           "sparse.replacing-pixels");
    const Raster blue = SolidRaster(2, 2, 255, 0, 0, 255);
    const Raster second = Raster::Replacing(first, 0, 0, {RasterTile{0, 0, blue}}, 0, 0, 6, 6, 0);
    Expect(second.Tiles().size() == 3 && second.Base() == red && second.Tiles()[0].pixels == green &&
               PixelOf(second, 1, 1)[0] == 255 && PixelOf(second, 2, 1)[1] == 255 && PixelOf(second, 1, 2)[1] == 255 &&
               IsRed(PixelOf(second, 5, 0)),
           "sparse.replacing-splits-older-tiles");
    bool disjoint = true;
    for (std::size_t a = 0; a < second.Tiles().size(); ++a)
    {
        for (std::size_t b = a + 1; b < second.Tiles().size(); ++b)
        {
            const RasterTile& p = second.Tiles()[a];
            const RasterTile& q = second.Tiles()[b];
            const bool overlap =
                p.x < q.x + q.Width() && q.x < p.x + p.Width() && p.y < q.y + q.Height() && q.y < p.y + p.Height();
            disjoint = disjoint && !overlap;
        }
    }
    Expect(disjoint, "sparse.tiles-disjoint");
    // The assembled bytes agree with the region reads.
    std::vector<std::uint8_t> whole(6 * 6 * 4);
    second.ReadRegion(0, 0, 6, 6, whole.data(), 24, 0);
    const std::uint8_t* assembled = second.Data();
    Expect(assembled != nullptr && std::memcmp(assembled, whole.data(), whole.size()) == 0, "sparse.assembled-matches");
    Expect(Raster::Replacing(Raster{}, 0, 0, {RasterTile{0, 0, blue}}, 0, 0, 3, 3, 0).Width() == 3 &&
               !Raster::Replacing(red, 0, 0, {}, 0, 0, 0, 3, 0).HasStorage(),
           "sparse.replacing-edge-cases");
}

void TestBrushKernel()
{
    BrushKernelParams hard;
    hard.radius = 10;
    hard.hardness = 1;
    hard.antialiasWidth = 1;
    Expect(BrushKernel::TipCoverage(9.5, hard) == 1.0 && BrushKernel::TipCoverage(10.5, hard) == 0.0 &&
               Near(BrushKernel::TipCoverage(10.0, hard), 0.5),
           "kernel.hard-tip-edge");
    BrushKernelParams soft = hard;
    soft.hardness = 0;
    Expect(Near(BrushKernel::TipCoverage(0.0, soft), 1.0) && Near(BrushKernel::TipCoverage(10.0, soft), 0.0) &&
               BrushKernel::TipCoverage(5.0, soft) > 0.37 && BrushKernel::TipCoverage(5.0, soft) < 0.55,
           "kernel.soft-gaussian");
    Expect(BrushKernel::SegmentDistanceSquared(Vec2{5, 5}, BrushSegment{Vec2{0, 0}, Vec2{10, 0}}) == 25.0 &&
               BrushKernel::SegmentDistanceSquared(Vec2{-3, 4}, BrushSegment{Vec2{0, 0}, Vec2{10, 0}}) == 25.0,
           "kernel.segment-distance");
    // A straight run needs one segment; a curve several, each within the tolerance.
    const std::vector<BrushSegment> straight = BrushCurveSegments(Vec2{0, 0}, Vec2{10, 0}, Vec2{20, 0}, Vec2{30, 0});
    Expect(straight.size() == 1 && straight[0].from == Vec2{10, 0} && straight[0].to == Vec2{20, 0},
           "kernel.curve-straight");
    const std::vector<BrushSegment> bent = BrushCurveSegments(Vec2{0, 0}, Vec2{10, 0}, Vec2{10, 10}, Vec2{0, 10});
    Expect(bent.size() > 1 && bent.front().from == Vec2{10, 0} && bent.back().to == Vec2{10, 10}, "kernel.curve-bends");
    // The CPU kernel on one tile: a hard dab, then a soft tail that leaves nothing behind when replaced.
    std::vector<float> permanent(64 * 64, 0.0f);
    std::vector<std::uint8_t> preview(64 * 64, 0);
    BrushKernelParams params;
    params.radius = 8;
    params.hardness = 1;
    params.canvasWidth = 64;
    params.canvasHeight = 64;
    params.antialiasWidth = 1;
    params.spacing = 1;
    const BrushKernelTile tile{0, 0, 0, 64, 64, permanent.data(), preview.data()};
    const std::array<BrushSegment, 1> dab{BrushSegment{Vec2{32, 32}, Vec2{32, 32}}};
    const std::array<BrushSegment, 1> tail{BrushSegment{Vec2{32, 32}, Vec2{60, 32}}};
    BrushKernel::RenderTile(tile, dab, tail, params);
    Expect(preview[32 * 64 + 32] == 255 && preview[32 * 64 + 55] == 255 && preview[10 * 64 + 10] == 0,
           "kernel.dab-and-tail");
    BrushKernel::RenderTile(tile, {}, {}, params);
    Expect(preview[32 * 64 + 32] == 255 && preview[32 * 64 + 55] == 0, "kernel.tail-replaced");
}

void TestBrushStroke()
{
    // Legacy continuousStrokeCrossesTilesAndCommitsOneUndo.
    DocumentSession session = BrushSession();
    const int count = session.history.UndoCount();
    Expect(session.CanPaint() && session.BeginBrush(Vec2{20, 40}), "brush.begin");
    session.ContinueBrush(Vec2{580, 40});
    const BrushStroke* stroke = session.PendingBrush();
    Expect(stroke != nullptr && stroke->Tiles().size() == 3 && !session.ActiveLayer()->asset.has_value() &&
               session.history.UndoCount() == count,
           "brush.three-tiles-no-commit");
    bool live = stroke != nullptr;
    for (const int x : {20, 255, 256, 511, 512, 579})
    {
        live = live && IsRed(StrokePixel(*stroke, x, 40));
    }
    Expect(live, "brush.live-red-across-tiles");
    session.FinishBrush();
    Expect(session.PendingBrush() == nullptr && session.history.UndoCount() == count + 1 &&
               session.history.UndoName() == L"Brush Stroke",
           "brush.one-entry");
    bool committed = session.ActiveLayer()->asset.has_value();
    for (const int x : {20, 255, 256, 511, 512, 579})
    {
        committed = committed && IsRed(LayerPixel(session, x, 40));
    }
    Expect(committed && LayerPixel(session, 300, 0)[3] == 0 && session.ActiveLayer()->asset->image.IsSparse(),
           "brush.committed-pixels");
    session.Undo();
    Expect(!session.ActiveLayer()->asset.has_value(), "brush.undo");
    session.Redo();
    Expect(session.ActiveLayer()->asset.has_value(), "brush.redo");
}

void TestBrushSoftAndCancel()
{
    // Legacy softBrushProducesPartialAlphaAndCancelPreservesDocument.
    DocumentSession session = BrushSession(80, 80);
    session.brushSettings.diameter = 40;
    session.brushSettings.hardness = 0;
    const std::optional<CanvasDocument> before = session.Document();
    Expect(session.BeginBrush(Vec2{40, 40}), "brush.soft-begin");
    const BrushStroke* stroke = session.PendingBrush();
    const int center = StrokePixel(*stroke, 40, 40)[3];
    const int half = StrokePixel(*stroke, 50, 40)[3];
    Expect(center > 230 && half > 95 && half < 140 && StrokePixel(*stroke, 57, 40)[3] < 40 &&
               StrokePixel(*stroke, 64, 40)[3] == 0,
           "brush.soft-falloff");
    session.CancelBrush();
    Expect(session.PendingBrush() == nullptr && session.Document() == before, "brush.cancel-keeps-document");
    Expect(session.BeginBrush(Vec2{-100, -100}), "brush.begin-outside");
    session.FinishBrush();
    Expect(session.Document() == before, "brush.outside-paints-nothing");
}

void TestBrushMask()
{
    // Legacy softMaskPaintingPreviewMatchesCommitAndPersists and opacityAppliesToMaskPainting.
    DocumentSession session = BrushSession(80, 80);
    session.brushSettings.diameter = 200;
    Expect(session.BeginBrush(Vec2{40, 40}), "mask.paint-layer");
    session.FinishBrush();
    const Raster source = session.ActiveLayer()->asset->image;
    session.AddLayerMask(true);
    Expect(session.IsMaskSelected() && session.history.UndoName() == L"Add Reveal-All Mask", "mask.added");
    session.brushSettings.diameter = 40;
    session.brushSettings.hardness = 0;
    session.maskPaintWhite = false;
    Expect(session.BeginBrush(Vec2{40, 40}), "mask.paint-begin");
    const BrushStroke* stroke = session.PendingBrush();
    std::array<int, 4> live{};
    const std::array<int, 4> xs{10, 40, 54, 70};
    for (std::size_t i = 0; i < 4; ++i)
    {
        live[i] = StrokePixel(*stroke, xs[i], 40)[3];
    }
    session.FinishBrush();
    bool matches = session.history.UndoName() == L"Paint Mask";
    for (std::size_t i = 0; i < 4; ++i)
    {
        matches = matches && std::abs(static_cast<int>(MaskValue(session, xs[i], 40)) - live[i]) <= 1;
    }
    Expect(matches && MaskValue(session, 40, 40) < 25 && session.ActiveLayer()->asset->image == source &&
               session.ActiveLayer()->mask->asset.image.Width() == 80,
           "mask.preview-matches-commit");
    session.maskPaintWhite = true;
    session.brushSettings.hardness = 1;
    Expect(session.BeginBrush(Vec2{40, 40}), "mask.white-begin");
    session.FinishBrush();
    Expect(MaskValue(session, 40, 40) == 255, "mask.white-reveals");
    session.Undo();
    Expect(session.IsMaskSelected() && MaskValue(session, 40, 40) < 25, "mask.undo-keeps-target");
    session.Redo();
    Expect(session.IsMaskSelected() && MaskValue(session, 40, 40) == 255, "mask.redo");
    // Opacity applies to mask painting: a half-strength black dab leaves 128.
    DocumentSession half = BrushSession(80, 80);
    half.brushSettings.diameter = 200;
    Expect(half.BeginBrush(Vec2{40, 40}), "mask.opacity-layer");
    half.FinishBrush();
    half.AddLayerMask(true);
    half.brushSettings.diameter = 20;
    half.brushSettings.opacity = 0.5;
    half.maskPaintWhite = false;
    Expect(half.BeginBrush(Vec2{40, 40}), "mask.opacity-begin");
    half.ContinueBrush(Vec2{42, 40});
    half.FinishBrush();
    Expect(std::abs(static_cast<int>(MaskValue(half, 40, 40)) - 128) <= 1 && MaskValue(half, 5, 5) == 255,
           "mask.opacity-halves");
    Expect(!half.ActiveLayer()->mask->asset.image.IsMaterialized(), "mask.commit-not-materialized");
}

void TestBrushOpacityAndAccumulation()
{
    // Legacy opacityCapsTheWholeStrokeEvenWhereItOverlapsItself.
    DocumentSession session = BrushSession(200, 80);
    session.brushSettings.opacity = 0.5;
    Expect(session.BeginBrush(Vec2{20, 40}), "opacity.begin");
    for (const int x : {180, 20, 180, 20, 100})
    {
        session.ContinueBrush(Vec2{static_cast<double>(x), 40});
    }
    const std::array<std::uint8_t, 4> live = StrokePixel(*session.PendingBrush(), 100, 40);
    Expect(std::abs(live[3] - 128) <= 1 && std::abs(live[2] - 128) <= 1 && live[1] == 0, "opacity.capped-live");
    session.FinishBrush();
    Expect(LayerPixel(session, 100, 40) == live && LayerPixel(session, 100, 0)[3] == 0, "opacity.capped-committed");
    // Legacy softStrokeBuildsCoverageWhileKeepingItsFeatheredRim.
    DocumentSession soft = BrushSession(200, 80);
    soft.brushSettings.diameter = 40;
    soft.brushSettings.hardness = 0;
    Expect(soft.BeginBrush(Vec2{100, 40}), "accumulate.single");
    const int single = StrokePixel(*soft.PendingBrush(), 100, 50)[3];
    soft.CancelBrush();
    Expect(soft.BeginBrush(Vec2{20, 40}), "accumulate.stroke");
    soft.ContinueBrush(Vec2{180, 40});
    const int stroke = StrokePixel(*soft.PendingBrush(), 100, 50)[3];
    Expect(stroke > single + 60 && stroke <= 255, "accumulate.builds-coverage");
    soft.CancelBrush();
    // Legacy spacedDabsLeaveNoVisibleRippleAlongTheStroke.
    for (const double hardness : {0.0, 0.5, 1.0})
    {
        DocumentSession wide = BrushSession(900, 300);
        wide.brushSettings.diameter = 120;
        wide.brushSettings.hardness = hardness;
        Expect(wide.BeginBrush(Vec2{100, 150}), "ripple.begin");
        wide.ContinueBrush(Vec2{800, 150});
        const BrushStroke* pending = wide.PendingBrush();
        int worst = 0;
        for (const int offset : {0, 30, 50})
        {
            int lo = 255;
            int hi = 0;
            for (int x = 300; x <= 600; ++x)
            {
                const int alpha = StrokePixel(*pending, x, 150 + offset)[3];
                lo = std::min(lo, alpha);
                hi = std::max(hi, alpha);
            }
            worst = std::max(worst, hi - lo);
        }
        Expect(worst <= 16, hardness == 0.0   ? "ripple.hardness-0"
                            : hardness == 0.5 ? "ripple.hardness-50"
                                              : "ripple.hardness-100");
        wide.CancelBrush();
    }
}

void TestBrushCurveAndTail()
{
    // Legacy sparseMouseSamplesFollowACurveInsteadOfStraightChords.
    DocumentSession session = BrushSession(300, 300);
    session.brushSettings.diameter = 4;
    const Vec2 center{150, 150};
    const double radius = 100;
    const auto onCircle = [&](double degrees)
    {
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        return Vec2{center.x + std::cos(radians) * radius, center.y + std::sin(radians) * radius};
    };
    Expect(session.BeginBrush(onCircle(0)), "curve.begin");
    for (int degrees = 30; degrees <= 180; degrees += 30)
    {
        session.ContinueBrush(onCircle(degrees));
    }
    session.FinishBrush();
    bool onArc = true;
    for (const double degrees : {45.0, 75.0, 105.0, 135.0})
    {
        const Vec2 point = onCircle(degrees);
        onArc = onArc && LayerPixel(session, std::floor(point.x) + 0.5, std::floor(point.y) + 0.5)[3] > 0;
    }
    Expect(onArc, "curve.follows-arc");
    // Legacy liveStrokeReachesNewestSampleAndTailIsReplacedExactly.
    DocumentSession tail = BrushSession(300, 120);
    tail.brushSettings.diameter = 8;
    Expect(tail.BeginBrush(Vec2{20, 60}), "tail.begin");
    tail.ContinueBrush(Vec2{150, 20});
    tail.ContinueBrush(Vec2{280, 60});
    Expect(StrokePixel(*tail.PendingBrush(), 278, 60)[3] == 255, "tail.reaches-cursor");
    tail.FinishBrush();
    Expect(LayerPixel(tail, 215, 40)[3] == 0 && LayerPixel(tail, 278, 60)[3] == 255, "tail.replaced-by-curve");
}

// A raw stroke on a blank canvas-sized layer with the CPU engine (legacy BrushIntersectionTests.stroke).
struct RawStroke
{
    DocumentSession session;
    std::unique_ptr<BrushStroke> stroke;
    explicit RawStroke(double diameter = 120, double opacity = 1)
    {
        session.CreateDocument(800, 800, false);
        session.AddBlankLayer();
        stroke = std::make_unique<BrushStroke>(*session.ActiveLayer(), false,
                                               BrushSettings{diameter, 0, 1, 1, 1, opacity, false}, 800, 800, nullptr);
    }
    void Trace(const std::vector<Vec2>& points, double step = 12)
    {
        static_cast<void>(stroke->Append(points[0]));
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const Vec2 a = points[i - 1];
            const Vec2 b = points[i];
            const int count = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / step)));
            for (int index = 1; index <= count; ++index)
            {
                const double t = static_cast<double>(index) / count;
                static_cast<void>(stroke->Append(Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}));
            }
        }
        static_cast<void>(stroke->Flush());
    }
    [[nodiscard]] int Alpha(int x, int y) const noexcept
    {
        return StrokePixel(*stroke, x + 0.5, y + 0.5)[3];
    }
};

void TestBrushCrossings()
{
    // Legacy selfCrossingsBlendInsteadOfTakingTheStrongestEdge.
    RawStroke vertical;
    RawStroke horizontal;
    RawStroke crossing;
    vertical.Trace({Vec2{400, 100}, Vec2{400, 700}});
    horizontal.Trace({Vec2{700, 400}, Vec2{100, 400}});
    crossing.Trace({Vec2{400, 100}, Vec2{400, 700}, Vec2{700, 700}, Vec2{700, 400}, Vec2{100, 400}});
    bool blends = true;
    for (const int offset : {45, 48, 51})
    {
        const int a = vertical.Alpha(400 + offset, 400 + offset);
        const int b = horizontal.Alpha(400 + offset, 400 + offset);
        const int actual = crossing.Alpha(400 + offset, 400 + offset);
        const int expected = 255 - (255 - a) * (255 - b) / 255;
        blends = blends && actual > std::max(a, b) + 10 && std::abs(actual - expected) <= 3;
    }
    Expect(blends, "crossing.blends-densities");
    // Legacy accumulationDependsOnDistanceNotEventCount.
    for (const double diameter : {12.0, 120.0})
    {
        RawStroke sparse(diameter);
        RawStroke dense(diameter);
        const std::vector<Vec2> points{Vec2{60, 400}, Vec2{740, 400}};
        sparse.Trace(points, 1000);
        dense.Trace(points, 8);
        int worst = 0;
        for (int y = 400; y < std::min(800, 400 + static_cast<int>(diameter / 2)); ++y)
        {
            worst = std::max(worst, std::abs(sparse.Alpha(400, y) - dense.Alpha(400, y)));
        }
        Expect(worst <= 2, diameter == 12.0 ? "crossing.distance-not-events-12" : "crossing.distance-not-events-120");
    }
    // Legacy softCrossingsRespectStrokeOpacityAndFlushIsIdempotent.
    RawStroke paint(120, 0.4);
    paint.Trace({Vec2{400, 100}, Vec2{400, 700}, Vec2{700, 700}, Vec2{700, 400}, Vec2{100, 400}});
    Expect(paint.Alpha(400, 400) == 102, "crossing.opacity-cap");
    bool capped = true;
    for (const BrushStroke::Tile& tile : paint.stroke->Tiles())
    {
        const std::uint8_t* pixels = tile.image.Data();
        for (int i = 0; i < tile.rect.width * tile.rect.height && capped; ++i)
        {
            capped = pixels[static_cast<std::size_t>(i) * 4u + 3u] <= 102;
        }
    }
    Expect(capped, "crossing.everywhere-capped");
    std::vector<std::vector<std::uint8_t>> before;
    for (const BrushStroke::Tile& tile : paint.stroke->Tiles())
    {
        before.emplace_back(tile.image.Data(), tile.image.Data() + tile.image.ByteSize());
    }
    Expect(paint.stroke->Flush(), "crossing.flush-again");
    bool same = true;
    std::size_t index = 0;
    for (const BrushStroke::Tile& tile : paint.stroke->Tiles())
    {
        same = same && index < before.size() && tile.image.ByteSize() == before[index].size() &&
               std::memcmp(tile.image.Data(), before[index].data(), before[index].size()) == 0;
        ++index;
    }
    Expect(same, "crossing.flush-idempotent");
}

void TestBrushSnapshots()
{
    // Legacy mouseUpAndNextStrokeNeverFlattenTheDocument.
    DocumentSession session;
    session.CreateDocument(2000, 2000, false);
    session.AddBlankLayer();
    session.brushSettings = BrushSettings{400, 0, 1, 1, 1, 1, false};
    Expect(session.BeginBrush(Vec2{400, 1600}), "snapshot.begin");
    session.ContinueBrush(Vec2{400, 400});
    session.ContinueBrush(Vec2{1600, 400});
    session.FinishBrush();
    const Raster first = session.ActiveLayer()->asset->image;
    const std::size_t firstThumbnail = session.ActiveLayer()->asset->thumbnail.ByteSize();
    Expect(first.IsSparse() && !first.IsMaterialized() && session.CanPaint(), "snapshot.mouse-up-does-not-flatten");
    const int count = session.history.UndoCount();
    Expect(session.BeginBrush(Vec2{800, 800}), "snapshot.second-begin");
    session.ContinueBrush(Vec2{1100, 1100});
    session.FinishBrush();
    const Raster second = session.ActiveLayer()->asset->image;
    Expect(!first.IsMaterialized() && second.IsSparse() && !second.IsMaterialized() && second.Base() == first.Base() &&
               session.history.UndoCount() == count + 1,
           "snapshot.next-stroke-does-not-flatten");
    // Untouched tiles are shared: history retains the first snapshot's thumbnail and only the tiles the second
    // stroke replaced (the pixels Undo restores), never the shared tiles and never a flattened copy of the layer.
    std::vector<const void*> live;
    for (const RasterTile& tile : second.Tiles())
    {
        live.push_back(tile.pixels.Identity());
    }
    std::size_t replaced = 0;
    for (const RasterTile& tile : first.Tiles())
    {
        if (std::find(live.begin(), live.end(), tile.pixels.Identity()) == live.end())
        {
            replaced += tile.pixels.ByteSize();
        }
    }
    const std::size_t retained = session.history.RetainedBytes(session.Document());
    std::printf("  history retains %zu bytes after the second stroke, %zu in replaced tiles\n", retained, replaced);
    Expect(replaced > 0 && replaced < 2000u * 2000u && retained == replaced + firstThumbnail,
           "snapshot.history-shares-tiles");
    session.Undo();
    Expect(session.ActiveLayer()->asset->image == first, "snapshot.undo-identity");
    session.Redo();
    Expect(session.ActiveLayer()->asset->image == second, "snapshot.redo-identity");
    // Legacy maskMouseUpIsImmediateAndPreservesTheImage.
    session.AddLayerMask(true);
    session.brushSettings.diameter = 150;
    Expect(session.BeginBrush(Vec2{1000, 1000}), "snapshot.mask-begin");
    session.ContinueBrush(Vec2{1100, 1000});
    session.FinishBrush();
    const Raster mask = session.ActiveLayer()->mask->asset.image;
    Expect(session.ActiveLayer()->asset->image == second && LayerMask::IsValid(mask) && mask.IsSparse() &&
               !mask.IsMaterialized(),
           "snapshot.mask-immediate");
    // Legacy softwareFallbackKeepsTheSoftStrokeContinuous and eightHundredPixelSoftStrokeHasNoPeriodicRidges.
    DocumentSession ridges;
    ridges.CreateDocument(2000, 2000, false);
    ridges.AddBlankLayer();
    ridges.brushSettings = BrushSettings{400, 0, 1, 1, 1, 1, false};
    Expect(ridges.BeginBrush(Vec2{300, 800}), "ridges.begin");
    ridges.ContinueBrush(Vec2{1700, 800});
    ridges.FinishBrush();
    int worst = 0;
    for (const int y : {800, 850, 900, 950, 990})
    {
        int lo = 255;
        int hi = 0;
        for (int x = 500; x <= 1500; x += 4)
        {
            const int alpha = LayerPixel(ridges, x + 0.5, y + 0.5)[3];
            lo = std::min(lo, alpha);
            hi = std::max(hi, alpha);
        }
        worst = std::max(worst, hi - lo);
    }
    Expect(worst <= 1, "ridges.none-along-800px-stroke");
}

void TestBrushBoundsAndBudget()
{
    // Legacy paintedBoundsTrimTilePaddingAndKeepSoftEdges.
    for (const double hardness : {0.0, 1.0})
    {
        DocumentSession session = BrushSession(600, 200);
        session.brushSettings.hardness = hardness;
        Expect(session.BeginBrush(Vec2{300, 100}), "bounds.begin");
        std::vector<std::array<std::uint8_t, 4>> live;
        for (int x = 288; x <= 312; ++x)
        {
            live.push_back(StrokePixel(*session.PendingBrush(), x + 0.5, 100.5));
        }
        session.FinishBrush();
        const ImageLayer* layer = session.ActiveLayer();
        const Raster& image = layer->asset->image;
        Expect(image.Width() <= 20 && image.Height() <= 20 && layer->transform.origin.x >= 290 &&
                   layer->transform.origin.y >= 90,
               hardness == 0.0 ? "bounds.trimmed-soft" : "bounds.trimmed-hard");
        bool edges = true;
        for (const int side : {0, 1, 2, 3})
        {
            bool any = false;
            const int n = side < 2 ? image.Width() : image.Height();
            for (int i = 0; i < n; ++i)
            {
                const int x = side == 0 || side == 1 ? i : (side == 2 ? 0 : image.Width() - 1);
                const int y = side == 0 ? 0 : side == 1 ? image.Height() - 1 : i;
                any = any || PixelOf(image, x, y)[3] > 0;
            }
            edges = edges && any;
        }
        Expect(edges, "bounds.keeps-soft-edges");
        bool same = true;
        for (int x = 288; x <= 312; ++x)
        {
            same = same && LayerPixel(session, x + 0.5, 100.5) == live[static_cast<std::size_t>(x - 288)];
        }
        Expect(same, "bounds.committed-equals-live");
        const LayerTransform committed = layer->transform;
        session.Undo();
        Expect(!session.ActiveLayer()->asset.has_value(), "bounds.undo");
        session.Redo();
        Expect(session.ActiveLayer()->transform == committed, "bounds.redo-transform");
    }
    // Legacy largeBlankCanvasOnlyAllocatesTouchedTilesUntilCommit.
    DocumentSession large = BrushSession(10000, 10000);
    Expect(large.BeginBrush(Vec2{100, 100}), "budget.begin");
    const BrushStroke* pending = large.PendingBrush();
    std::size_t bytes = 0;
    for (const BrushStroke::Tile& tile : pending->Tiles())
    {
        bytes += tile.image.ByteSize();
    }
    Expect(pending->Tiles().size() == 1 && bytes <= 256u * 256u * 4u && !large.ActiveLayer()->asset.has_value(),
           "budget.one-tile");
    large.CancelBrush();
    // The pixel budget refuses a stroke that would grow the layer past it.
    DocumentSession budget = BrushSession(600, 80);
    BrushStroke limited(*budget.ActiveLayer(), false, budget.brushSettings, 600, 80, nullptr);
    limited.SetPixelLimit(1000);
    Expect(limited.IsValid() && !limited.Append(Vec2{20, 40}) && !limited.HasPaint(), "budget.refuses-over-limit");
    Expect(budget.RasterEditPixelLimit(*budget.ActiveLayer(), false) == 100'000'000, "budget.full-when-alone");
}

void TestBrushRejectsAndKeys()
{
    // Legacy foldersHiddenLayersAndDisabledMasksRejectPainting.
    DocumentSession session = BrushSession();
    session.AddGroup();
    Expect(!session.CanPaint() && !session.BeginBrush(Vec2{10, 10}), "reject.folder");
    session.SelectLayer(session.Document()->layers.front().id);
    session.ToggleVisibility(session.ActiveLayerID().value());
    Expect(!session.CanPaint(), "reject.hidden");
    session.ToggleVisibility(session.ActiveLayerID().value());
    Expect(session.CanPaint(), "reject.visible-again");
    session.AddLayerMask(true);
    session.ToggleLayerMask(session.ActiveLayerID().value());
    Expect(!session.CanPaint() && session.history.UndoName() == L"Disable Layer Mask", "reject.disabled-mask");
    // An empty selection leaves nothing paintable; a pending transform blocks too.
    DocumentSession empty = BrushSession();
    empty.SetSelection(DocumentSelection{}, L"Select");
    Expect(!empty.CanPaint(), "reject.empty-selection");
    empty.Deselect();
    InsertPainted(empty, 20, 20, Vec2{0, 0});
    empty.BeginTransform(true);
    Expect(!empty.CanPaint(), "reject.pending-transform");
    empty.CancelTransform();
    Expect(empty.CanPaint(), "reject.after-cancel");
    // Legacy shiftBracketsStepHardnessByQuarters.
    DocumentSession keys = BrushSession();
    keys.brushSettings.hardness = 0.8;
    keys.ChangeBrushHardness(true);
    Expect(keys.brushSettings.hardness == 1.0, "keys.hardness-up");
    keys.ChangeBrushHardness(true);
    Expect(keys.brushSettings.hardness == 1.0, "keys.hardness-caps");
    keys.brushSettings.hardness = 0.8;
    keys.ChangeBrushHardness(false);
    Expect(keys.brushSettings.hardness == 0.75, "keys.hardness-down");
    for (int i = 0; i < 5; ++i)
    {
        keys.ChangeBrushHardness(false);
    }
    Expect(keys.brushSettings.hardness == 0.0, "keys.hardness-floor");
    keys.ChangeBrushHardness(true);
    Expect(keys.brushSettings.hardness == 0.25, "keys.hardness-quarter");
    // Size steps by a fifth, at least a pixel, within 1–2000.
    keys.brushSettings.diameter = 20;
    keys.ChangeBrushSize(true);
    Expect(keys.brushSettings.diameter == 24, "keys.size-up");
    keys.ChangeBrushSize(false);
    keys.ChangeBrushSize(false);
    Expect(keys.brushSettings.diameter < 20, "keys.size-down");
    keys.brushSettings.diameter = 2;
    keys.ChangeBrushSize(false);
    Expect(keys.brushSettings.diameter == 1, "keys.size-min");
    keys.ChangeBrushSize(false);
    Expect(keys.brushSettings.diameter == 1, "keys.size-floor");
    keys.brushSettings.diameter = 1999;
    keys.ChangeBrushSize(true);
    Expect(keys.brushSettings.diameter == 2000, "keys.size-cap");
    // Legacy numberKeysSetBrushAndGradientOpacity (the brush part).
    keys.TypeOpacityDigit(5, 10);
    Expect(keys.brushSettings.opacity == 0.5, "keys.opacity-5");
    keys.TypeOpacityDigit(0, 20);
    Expect(keys.brushSettings.opacity == 1.0, "keys.opacity-0");
    keys.TypeOpacityDigit(4, 30);
    keys.TypeOpacityDigit(5, 30.3);
    Expect(keys.brushSettings.opacity == 0.45, "keys.opacity-45");
    keys.TypeOpacityDigit(0, 40);
    keys.TypeOpacityDigit(5, 40.2);
    Expect(keys.brushSettings.opacity == 0.05, "keys.opacity-05");
    Expect(keys.BeginBrush(Vec2{10, 10}), "keys.stroke");
    keys.ChangeBrushSize(true);
    keys.ChangeBrushHardness(false);
    keys.TypeOpacityDigit(9, 50);
    Expect(keys.brushSettings.diameter == 2000 && keys.brushSettings.hardness == 0.25 &&
               keys.brushSettings.opacity == 0.05,
           "keys.ignored-while-painting");
    keys.CancelBrush();
    // Shift-click line start follows the last stroke on the same target.
    DocumentSession line = BrushSession();
    Expect(!line.ShiftLineStart().has_value() && line.BeginBrush(Vec2{10, 20}), "line.begin");
    line.ContinueBrush(Vec2{30, 20});
    line.FinishBrush();
    Expect(line.ShiftLineStart() == Vec2{30, 20}, "line.start-at-last-point");
    line.AddBlankLayer();
    Expect(!line.ShiftLineStart().has_value(), "line.other-layer");
    Expect(!BrushSettings{0, 1, 0, 0, 0, 1, false}.IsValid() && !BrushSettings{40, 1.5, 0, 0, 0, 1, false}.IsValid() &&
               !BrushSettings{40, 1, 0, 0, 0, 0, false}.IsValid() && BrushSettings{}.IsValid(),
           "keys.settings-validity");
}

void TestBrushExpansion()
{
    // Legacy importedImageLayerExpandsAcrossCanvasWithoutMovingImageOrMask (0°, 37° and 90°, flipped).
    for (const double rotation : {0.0, 37.0, 90.0})
    {
        DocumentSession session = BrushSession(600, 200);
        ImageLayer imported = RasterLayer(40, 20, L"Imported");
        imported.asset->image = SolidRaster(40, 20, 0, 0, 255, 255);
        imported.asset->thumbnail = imported.asset->image;
        imported.transform.origin = Vec2{240, 80};
        imported.transform.size = Vec2{80, 40};
        imported.transform.rotation = rotation;
        imported.transform.flipX = true;
        imported.transform.sampling = LayerSampling::Nearest;
        const Raster gray = GrayRaster(1, 1, 128);
        imported.mask = LayerMask{RasterAsset{gray, gray, L"mask"}, true, std::nullopt, true};
        session.Document()->layers.push_back(imported);
        session.SelectLayer(imported.id);
        const std::optional<CanvasDocument> original = session.Document();
        session.brushSettings = BrushSettings{16, 1, 0, 1, 0, 1, false};
        Expect(session.BeginBrush(Vec2{12, 12}), "expand.begin");
        session.ContinueBrush(Vec2{580, 12});
        const BrushStroke* stroke = session.PendingBrush();
        Expect(StrokePixel(*stroke, 12.5, 12.5)[1] > 240 && StrokePixel(*stroke, 580.5, 12.5)[1] > 240,
               "expand.live-across-canvas");
        session.FinishBrush();
        const ImageLayer* layer = session.ActiveLayer();
        Expect(LayerPixel(session, 12.5, 12.5)[1] > 240 && LayerPixel(session, 580.5, 12.5)[1] > 240 &&
                   IsRed(LayerPixel(session, 280, 100)) && layer->transform.rotation == rotation &&
                   layer->transform.flipX,
               rotation == 0.0    ? "expand.committed-0"
               : rotation == 37.0 ? "expand.committed-37"
                                  : "expand.committed-90");
        Expect(layer->mask.has_value() && layer->mask->asset.image.PixelCount() == 1 &&
                   layer->mask->asset.image == gray,
               "expand.uniform-mask-kept");
        session.Undo();
        Expect(session.Document() == original, "expand.undo");
        session.Redo();
        Expect(session.ActiveLayer()->asset->image.IsSparse(), "expand.redo");
    }
    // A full-size mask on the layer's grid grows with it and reveals the new area.
    DocumentSession grown = BrushSession(300, 100);
    ImageLayer masked = RasterLayer(40, 20, L"Masked");
    masked.asset->image = SolidRaster(40, 20, 0, 0, 255, 255);
    masked.asset->thumbnail = masked.asset->image;
    masked.transform.origin = Vec2{100, 40};
    masked.transform.size = Vec2{40, 20};
    const Raster half = GrayRaster(40, 20, 128);
    masked.mask = LayerMask{RasterAsset{half, half, L"mask"}, true, std::nullopt, true};
    grown.Document()->layers.push_back(masked);
    grown.SelectLayer(masked.id);
    grown.brushSettings = BrushSettings{10, 1, 0, 1, 0, 1, false};
    Expect(grown.BeginBrush(Vec2{20, 50}), "expand.mask-begin");
    grown.FinishBrush();
    const Raster newMask = grown.ActiveLayer()->mask->asset.image;
    Expect(newMask.IsSparse() && newMask.Width() > 40 && MaskValue(grown, 20, 50) == 255 &&
               MaskValue(grown, 120, 50) == 128,
           "expand.mask-grows-revealing");
}

void TestBrushSelectionClip()
{
    // Legacy brushPaintsOnlyInsideTheSelection.
    DocumentSession session = BrushSession(100, 40);
    session.ApplySelection(SelectionGeometry::Rect(0, 0, 50, 40), SelectionMode::New, L"Select");
    Expect(session.BeginBrush(Vec2{5, 20}), "clip.begin");
    session.ContinueBrush(Vec2{95, 20});
    session.FinishBrush();
    Expect(IsRed(LayerPixel(session, 25.5, 20.5)) && LayerPixel(session, 75.5, 20.5)[3] == 0, "clip.inside-only");
    // Legacy clipFollowsScaledLayersAndSoftensEdges: an erase through an antialiased triangle on a 2× layer.
    DocumentSession scaled = BrushSession(100, 40);
    ImageLayer blue = RasterLayer(50, 20, L"Blue");
    blue.asset->image = SolidRaster(50, 20, 255, 0, 0, 255);
    blue.asset->thumbnail = blue.asset->image;
    blue.transform.origin = Vec2{0, 0};
    blue.transform.size = Vec2{100, 40};
    scaled.Document()->layers.push_back(blue);
    scaled.SelectLayer(blue.id);
    scaled.selectionAntialiased = true;
    scaled.ApplySelection({SelectionLoop{Vec2{0, 0}, Vec2{100, 0}, Vec2{0, 40}}}, SelectionMode::New, L"Select");
    scaled.brushSettings = BrushSettings{300, 1, 0, 0, 0, 1, false};
    Expect(scaled.BeginBrush(Vec2{50, 20}, true), "clip.erase-begin");
    scaled.ContinueBrush(Vec2{51, 20});
    scaled.FinishBrush();
    Expect(scaled.history.UndoName() == L"Erase" && LayerPixel(scaled, 10.5, 10.5)[3] == 0 &&
               LayerPixel(scaled, 90.5, 35.5)[3] == 255,
           "clip.erase-inside-triangle");
    bool partial = false;
    for (int x = 0; x < 100; ++x)
    {
        const int y = std::min(39, static_cast<int>((1.0 - x / 100.0) * 40.0));
        const int alpha = LayerPixel(scaled, x + 0.5, y + 0.5)[3];
        partial = partial || (alpha > 0 && alpha < 255);
    }
    Expect(partial, "clip.soft-edge-along-diagonal");
}
} // namespace

// ── Retouch (C11) ─────────────────────────────────────────────

Raster BytesRaster(int width, int height, const std::function<std::array<std::uint8_t, 4>(int, int)>& pixel)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::array<std::uint8_t, 4> value = pixel(x, y);
            std::memcpy(
                bytes.data() +
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u,
                value.data(), 4);
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Guid InsertPixels(DocumentSession& session, const Raster& image, const wchar_t* name)
{
    ImageLayer layer = RasterLayer(image.Width(), image.Height(), name);
    layer.asset->image = image;
    layer.asset->thumbnail = Raster::MakeThumbnail(image);
    session.Document().value().layers.push_back(layer);
    session.SelectLayer(layer.id);
    return layer.id;
}

// The active layer's pixel under document (x, y), read through the layer's origin; transparent beyond it.
std::array<std::uint8_t, 4> ActivePixel(const DocumentSession& session, int x, int y)
{
    std::array<std::uint8_t, 4> out{0, 0, 0, 0};
    const ImageLayer* layer = session.ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value())
    {
        return out;
    }
    const int lx = x - static_cast<int>(std::lround(layer->transform.origin.x));
    const int ly = y - static_cast<int>(std::lround(layer->transform.origin.y));
    layer->asset->image.ReadRegion(lx, ly, 1, 1, out.data(), 4, 0);
    return out;
}

std::uint8_t ActiveMaskValue(const DocumentSession& session, int x, int y)
{
    std::uint8_t out = 255;
    const ImageLayer* layer = session.ActiveLayer();
    if (layer == nullptr || !layer->mask.has_value())
    {
        return out;
    }
    const int lx = x - static_cast<int>(std::lround(layer->transform.origin.x));
    const int ly = y - static_cast<int>(std::lround(layer->transform.origin.y));
    layer->mask->asset.image.ReadRegion(lx, ly, 1, 1, &out, 1, 255);
    return out;
}

// A short stroke of `kind` at `point` (a half-pixel move), committed.
bool Dab(DocumentSession& session, Vec2 point, BrushKind kind, const Raster* composite = nullptr)
{
    if (!session.BeginBrush(point, kind, composite))
    {
        return false;
    }
    session.ContinueBrush(Vec2{point.x + 0.5, point.y});
    session.FinishBrush();
    return session.PendingBrush() == nullptr;
}

void TestSpotHealing()
{
    // Legacy healsTheBlemishUnderTheBrushAndNothingElse: vertical gray stripes, 2 px wide, with a 10 × 10 red
    // blemish in the middle, healed by every mode.
    const auto surfacePixel = [](int x, int y) -> std::array<std::uint8_t, 4>
    {
        const bool red = x >= 55 && x < 65 && y >= 35 && y < 45;
        const std::uint8_t gray = x % 4 < 2 ? 100 : 112;
        return red ? std::array<std::uint8_t, 4>{20, 20, 230, 255} : std::array<std::uint8_t, 4>{gray, gray, gray, 255};
    };
    const Raster surface = BytesRaster(120, 80, surfacePixel);
    const std::array<std::pair<SpotHealingMode, const char*>, 3> modes{
        std::pair{SpotHealingMode::ContentAware, "heal.content-aware"},
        std::pair{SpotHealingMode::CreateTexture, "heal.create-texture"},
        std::pair{SpotHealingMode::ProximityMatch, "heal.proximity-match"}};
    for (const auto& [mode, name] : modes)
    {
        DocumentSession session;
        session.CreateDocument(120, 80, false);
        InsertPixels(session, surface, L"Surface");
        session.spotHealingMode = mode;
        session.brushSettings = BrushSettings{24, 1, 0, 0, 0, 1, false};
        const int count = session.history.UndoCount();
        bool ok = session.BeginBrush(Vec2{60, 40}, BrushKind::Heal);
        // While painting, the spot shows as a dark wash.
        std::array<std::uint8_t, 4> wash{255, 255, 255, 255};
        if (ok)
        {
            for (const BrushStroke::Tile& tile : session.PendingBrush()->Tiles())
            {
                if (tile.rect.Intersect(GridRect{60, 40, 1, 1}).Empty())
                {
                    continue;
                }
                wash = PixelOf(tile.image, 60 - tile.rect.x, 40 - tile.rect.y);
            }
        }
        session.ContinueBrush(Vec2{60.5, 40});
        session.FinishBrush();
        ok = ok && wash[2] < 150 && session.history.UndoCount() == count + 1 &&
             session.history.UndoName() == L"Spot Healing" && session.PendingBrush() == nullptr;
        bool healed = true;
        for (const auto& [x, y] : {std::pair{60, 40}, std::pair{56, 36}, std::pair{64, 44}})
        {
            const std::array<std::uint8_t, 4> pixel = ActivePixel(session, x, y);
            healed = healed && static_cast<int>(pixel[2]) - pixel[1] < 30 && pixel[1] >= 80 && pixel[1] <= 130 &&
                     pixel[3] == 255;
        }
        // Away from the brush, nothing moves.
        bool untouched = true;
        for (const auto& [x, y] :
             {std::pair{10, 10}, std::pair{90, 40}, std::pair{30, 40}, std::pair{60, 10}, std::pair{60, 70}})
        {
            untouched = untouched && ActivePixel(session, x, y) == surfacePixel(x, y);
        }
        Expect(ok && healed && untouched, name);
    }
    // Healing reworks image pixels: it never starts on a mask.
    DocumentSession masked;
    masked.CreateDocument(60, 40, false);
    const Guid id = InsertPixels(masked, SolidRaster(60, 40, 0, 0, 255, 255), L"Red");
    masked.AddLayerMask(true);
    masked.SelectLayerTarget(id, true);
    Expect(masked.IsMaskSelected() && !masked.BeginBrush(Vec2{30, 20}, BrushKind::Heal) &&
               masked.PendingBrush() == nullptr,
           "heal.never-on-a-mask");
}

void TestCloneStamp()
{
    // Legacy copiesTheSourceUnderTheBrushKeepingAlignmentUntilItIsTurnedOff: left half red with a green square
    // (x 10–19, y 15–24), right half blue.
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> green{0, 255, 0, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const Raster colors = BytesRaster(80, 40,
                                      [&](int x, int y)
                                      {
                                          if (x >= 10 && x < 20 && y >= 15 && y < 25)
                                          {
                                              return green;
                                          }
                                          return x < 40 ? red : blue;
                                      });
    DocumentSession session;
    session.CreateDocument(80, 40, false);
    InsertPixels(session, colors, L"Colors");
    session.brushSettings = BrushSettings{6, 1, 0, 0, 0, 1, false};
    // Nothing to copy until a source is set.
    Expect(!session.BeginBrush(Vec2{60, 20}, BrushKind::Clone) && session.PendingBrush() == nullptr &&
               !session.CloneStrokeOffset(Vec2{60, 20}).has_value(),
           "clone.needs-a-source");
    session.SetCloneSource(Vec2{15, 20});
    const std::optional<Vec2> sample = session.CloneSamplePoint(Vec2{60, 20});
    Expect(sample.has_value() && sample->x == 15 && sample->y == 20, "clone.crosshair-at-the-source");
    const int count = session.history.UndoCount();
    Expect(Dab(session, Vec2{60, 20}, BrushKind::Clone) && session.history.UndoCount() == count + 1 &&
               session.history.UndoName() == L"Clone Stamp",
           "clone.one-entry");
    Expect(ActivePixel(session, 60, 20) == green && ActivePixel(session, 70, 5) == blue,
           "clone.copies-the-source-under-the-brush");
    // Aligned: the next stroke keeps the offset (−45, 0), so (66, 20) copies red from (21, 20).
    const std::optional<Vec2> aligned = session.CloneSamplePoint(Vec2{66, 20});
    Expect(aligned.has_value() && aligned->x == 21 && aligned->y == 20, "clone.crosshair-follows-when-aligned");
    Expect(Dab(session, Vec2{66, 20}, BrushKind::Clone) && ActivePixel(session, 66, 20) == red,
           "clone.aligned-keeps-the-offset");
    // Not aligned: every stroke starts at the source again.
    session.cloneSettings.aligned = false;
    Expect(Dab(session, Vec2{50, 10}, BrushKind::Clone) && ActivePixel(session, 50, 10) == green,
           "clone.unaligned-starts-at-the-source");
    // Sample All Layers copies from the composite the host passes in, and needs it.
    session.cloneSettings.sampleAllLayers = true;
    const Raster magenta = SolidRaster(80, 40, 255, 0, 255, 255);
    Expect(!session.BeginBrush(Vec2{30, 30}, BrushKind::Clone, nullptr), "clone.all-layers-needs-the-composite");
    Expect(Dab(session, Vec2{30, 30}, BrushKind::Clone, &magenta) &&
               ActivePixel(session, 30, 30) == std::array<std::uint8_t, 4>{255, 0, 255, 255},
           "clone.all-layers-copies-the-composite");
    // A mask is never cloned.
    DocumentSession masked;
    masked.CreateDocument(60, 40, false);
    const Guid id = InsertPixels(masked, SolidRaster(60, 40, 0, 0, 255, 255), L"Red");
    masked.AddLayerMask(true);
    masked.SelectLayerTarget(id, true);
    masked.SetCloneSource(Vec2{10, 10});
    Expect(!masked.BeginBrush(Vec2{30, 20}, BrushKind::Clone), "clone.never-on-a-mask");
    // Legacy cloneStampKeepsItsOwnSoftBrushTip: Clone Stamp (family 1) parks its own tip.
    DocumentSession tips;
    tips.CreateDocument(40, 20, false);
    tips.brushSettings.diameter = 30;
    Expect(tips.brushSettings.hardness == 1, "tips.brush-starts-hard");
    tips.SwitchBrushTipFamily(0, 1);
    Expect(tips.brushSettings.hardness == 0 && tips.brushSettings.diameter == 40, "tips.clone-has-its-own-soft-tip");
    tips.brushSettings.hardness = 0.5;
    tips.SwitchBrushTipFamily(1, 0);
    Expect(tips.brushSettings.hardness == 1 && tips.brushSettings.diameter == 30, "tips.brush-gets-its-tip-back");
    tips.SwitchBrushTipFamily(0, 1);
    Expect(tips.brushSettings.hardness == 0.5 && tips.brushSettings.diameter == 40, "tips.clone-remembers-its-change");
    tips.SwitchBrushTipFamily(1, 1);
    Expect(tips.brushSettings.hardness == 0.5, "tips.same-family-changes-nothing");
}

void TestBlurTool()
{
    // Blur paints a softened copy of the layer through the tip; going over an area again softens it further.
    const std::array<std::uint8_t, 4> black{0, 0, 0, 255};
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    const Raster edge = BytesRaster(40, 40, [&](int x, int) { return x < 20 ? black : white; });
    DocumentSession session;
    session.CreateDocument(40, 40, false);
    InsertPixels(session, edge, L"Edge");
    session.brushSettings = BrushSettings{20, 1, 0, 0, 0, 1, false};
    const int count = session.history.UndoCount();
    Expect(Dab(session, Vec2{20, 20}, BrushKind::Blur) && session.history.UndoCount() == count + 1 &&
               session.history.UndoName() == L"Blur",
           "blur.one-entry");
    const std::array<std::uint8_t, 4> at = ActivePixel(session, 19, 20);
    Expect(at[0] > 30 && at[0] < 225 && at[0] == at[1] && at[3] == 255, "blur.softens-the-edge");
    Expect(ActivePixel(session, 5, 20) == black && ActivePixel(session, 35, 20) == white, "blur.leaves-the-rest");
    const std::array<std::uint8_t, 4> once = ActivePixel(session, 17, 20);
    Expect(Dab(session, Vec2{20, 20}, BrushKind::Blur) && ActivePixel(session, 17, 20)[0] > once[0],
           "blur.again-softens-further");
    // On a mask it blurs the mask, keeping its tone beyond its pixels.
    DocumentSession masked;
    masked.CreateDocument(40, 40, false);
    const Guid id = InsertPixels(masked, SolidRaster(40, 40, 0, 0, 255, 255), L"Red");
    masked.AddLayerMask(true);
    std::vector<std::uint8_t> half(40u * 40u, 0);
    for (int y = 0; y < 40; ++y)
    {
        for (int x = 20; x < 40; ++x)
        {
            half[static_cast<std::size_t>(y) * 40u + static_cast<std::size_t>(x)] = 255;
        }
    }
    const Raster halfMask = Raster::Make(40, 40, RasterFormat::Gray8, half.data(), half.size());
    for (ImageLayer& layer : masked.Document().value().layers)
    {
        if (layer.id == id)
        {
            layer.mask->asset.image = halfMask;
            layer.mask->asset.thumbnail = halfMask;
        }
    }
    masked.SelectLayerTarget(id, true);
    masked.brushSettings = BrushSettings{20, 1, 0, 0, 0, 1, false};
    Expect(masked.IsMaskSelected() && Dab(masked, Vec2{20, 20}, BrushKind::Blur) &&
               masked.history.UndoName() == L"Blur",
           "blur.mask-entry");
    const std::uint8_t maskEdge = ActiveMaskValue(masked, 19, 20);
    Expect(maskEdge > 30 && maskEdge < 225 && ActiveMaskValue(masked, 5, 20) == 0 &&
               ActiveMaskValue(masked, 35, 20) == 255,
           "blur.softens-the-mask");
    Expect(ActivePixel(masked, 19, 20) == std::array<std::uint8_t, 4>{0, 0, 255, 255}, "blur.mask-leaves-the-pixels");
}

void TestSmearWarp()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const Raster halves = BytesRaster(60, 20, [&](int x, int) { return x < 30 ? red : blue; });
    // Smudge drags the color it picked up along the stroke.
    DocumentSession smudge;
    smudge.CreateDocument(60, 20, false);
    InsertPixels(smudge, halves, L"Halves");
    smudge.smearMode = SmearMode::Smudge;
    smudge.brushSettings = BrushSettings{10, 0.5, 0, 0, 0, 0.8, false};
    Expect(smudge.BeginWarp(Vec2{25, 10}) && smudge.PendingWarp() != nullptr && smudge.PendingBrush() == nullptr,
           "smudge.begins");
    smudge.ContinueWarp(Vec2{45, 10});
    const int count = smudge.history.UndoCount();
    smudge.FinishWarp();
    Expect(smudge.PendingWarp() == nullptr && smudge.history.UndoCount() == count + 1 &&
               smudge.history.UndoName() == L"Smudge",
           "smudge.one-entry");
    const std::array<std::uint8_t, 4> dragged = ActivePixel(smudge, 35, 10);
    Expect(dragged[2] > 0 && dragged[0] < 255 && dragged[3] == 255, "smudge.carries-color-along");
    Expect(ActivePixel(smudge, 5, 10) == red && ActivePixel(smudge, 55, 10) == blue, "smudge.leaves-the-rest");
    // Liquify pushes pixels along with the brush: the edge moves right.
    DocumentSession liquify;
    liquify.CreateDocument(60, 20, false);
    InsertPixels(liquify, halves, L"Halves");
    liquify.smearMode = SmearMode::Liquify;
    liquify.brushSettings = BrushSettings{12, 0.5, 0, 0, 0, 1, false};
    Expect(liquify.BeginWarp(Vec2{30, 10}), "liquify.begins");
    liquify.ContinueWarp(Vec2{38, 10});
    liquify.FinishWarp();
    const std::array<std::uint8_t, 4> pushed = ActivePixel(liquify, 33, 10);
    Expect(liquify.history.UndoName() == L"Liquify" && pushed[2] > pushed[0], "liquify.pushes-the-edge");
    Expect(ActivePixel(liquify, 5, 10) == red && ActivePixel(liquify, 55, 10) == blue, "liquify.leaves-the-rest");
    // The working copy hands the preview its changed tiles once; cancelling records nothing; masks refuse.
    DocumentSession cancel;
    cancel.CreateDocument(60, 20, false);
    const Guid id = InsertPixels(cancel, halves, L"Halves");
    cancel.smearMode = SmearMode::Smudge;
    cancel.brushSettings = BrushSettings{10, 0.5, 0, 0, 0, 0.8, false};
    const int before = cancel.history.UndoCount();
    Expect(cancel.BeginWarp(Vec2{25, 10}), "warp.cancel-begins");
    cancel.ContinueWarp(Vec2{35, 10});
    const std::vector<RasterTile> tiles = cancel.MutableWarp()->TakeChangedTiles();
    Expect(tiles.size() == 1 && tiles[0].pixels.Width() == 60 && tiles[0].pixels.Height() == 20 &&
               cancel.MutableWarp()->TakeChangedTiles().empty(),
           "warp.changed-tiles-once");
    cancel.CancelWarp();
    Expect(cancel.PendingWarp() == nullptr && cancel.history.UndoCount() == before &&
               ActivePixel(cancel, 35, 10) == blue,
           "warp.cancel-records-nothing");
    cancel.AddLayerMask(true);
    cancel.SelectLayerTarget(id, true);
    Expect(!cancel.BeginWarp(Vec2{25, 10}), "warp.never-on-a-mask");
}

[[nodiscard]] bool Near(int value, int target, int tolerance = 3) noexcept
{
    return std::abs(value - target) <= tolerance;
}

[[nodiscard]] bool NearPixel(const std::array<std::uint8_t, 4>& value, const std::array<int, 4>& target) noexcept
{
    return Near(value[0], target[0]) && Near(value[1], target[1]) && Near(value[2], target[2]) &&
           Near(value[3], target[3]);
}

DocumentSession GradientSession(int width = 101, int height = 4)
{
    DocumentSession session;
    session.CreateDocument(width, height, false);
    session.AddBlankLayer();
    Expect(session.gradientSettings.style == GradientStyle::ForegroundToTransparent, "gradient.default-style");
    session.gradientSettings.style = GradientStyle::ForegroundToBackground;
    return session;
}

void DragGradient(DocumentSession& session, Vec2 start, Vec2 end)
{
    session.BeginGradient(start);
    session.MoveGradient(std::nullopt, end);
    session.EndGradientDrag();
}

void TestGradientTool()
{
    // Legacy foregroundToBackgroundFillsCanvasAndCommitsOneUndo (pixels within 3 levels, as Core Graphics quantizes).
    {
        DocumentSession session = GradientSession();
        const int count = session.history.UndoCount();
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        Expect(session.PendingGradient() != nullptr && !session.ActiveLayer()->asset.has_value() &&
                   session.history.UndoCount() == count,
               "gradient.pending-leaves-the-document");
        session.CommitGradient();
        Expect(session.PendingGradient() == nullptr && session.history.UndoCount() == count + 1 &&
                   session.history.UndoName() == L"Gradient",
               "gradient.commits-one-entry");
        const std::array<std::uint8_t, 4> middle = ActivePixel(session, 50, 1);
        Expect(NearPixel(ActivePixel(session, 0, 0), {0, 0, 0, 255}) &&
                   NearPixel(ActivePixel(session, 100, 3), {255, 255, 255, 255}) && Near(middle[2], 128) &&
                   middle[2] == middle[1] && middle[3] == 255,
               "gradient.foreground-to-background");
        session.Undo();
        Expect(!session.ActiveLayer()->asset.has_value(), "gradient.undo-restores");
    }
    // Legacy radialSpreadsFromStartToRimInEveryDirection.
    {
        DocumentSession session = GradientSession(101, 101);
        session.gradientSettings.shape = GradientShape::Radial;
        DragGradient(session, Vec2{50.5, 50.5}, Vec2{90.5, 50.5});
        session.CommitGradient();
        std::array<int, 4> halfway{};
        int i = 0;
        for (const auto& [x, y] : {std::pair{70, 50}, std::pair{30, 50}, std::pair{50, 70}, std::pair{50, 30}})
        {
            halfway[static_cast<std::size_t>(i++)] = ActivePixel(session, x, y)[2];
        }
        bool even = true;
        for (const int value : halfway)
        {
            even = even && Near(value, 128, 5) && Near(value, halfway[0], 1);
        }
        Expect(NearPixel(ActivePixel(session, 50, 50), {0, 0, 0, 255}) && even &&
                   NearPixel(ActivePixel(session, 100, 50), {255, 255, 255, 255}) &&
                   NearPixel(ActivePixel(session, 0, 0), {255, 255, 255, 255}),
               "gradient.radial");
    }
    // Legacy reverseOpacityAndDirectionFollowSettings: white at the start with 50 % alpha on a blank layer.
    {
        DocumentSession session = GradientSession();
        session.gradientSettings.reversed = true;
        session.gradientSettings.opacity = 0.5;
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        session.CommitGradient();
        const std::array<std::uint8_t, 4> start = ActivePixel(session, 0, 2);
        const std::array<std::uint8_t, 4> end = ActivePixel(session, 100, 2);
        Expect(Near(start[3], 128) && Near(start[2], start[3]) && Near(end[2], 0) && Near(end[3], 128),
               "gradient.reverse-and-opacity");
    }
    // Legacy foregroundToTransparentPreservesUnderlyingPixelsAndAlpha.
    {
        DocumentSession session = GradientSession();
        session.SetForegroundColor(0xFFFF0000u);
        // Everything before the start is foreground: a solid red layer.
        DragGradient(session, Vec2{100.5, 2}, Vec2{101, 2});
        session.CommitGradient();
        Expect(NearPixel(ActivePixel(session, 50, 2), {0, 0, 255, 255}), "gradient.solid-fill");
        session.SetForegroundColor(0xFF000000u);
        session.gradientSettings.style = GradientStyle::ForegroundToTransparent;
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        session.CommitGradient();
        const std::array<std::uint8_t, 4> middle = ActivePixel(session, 50, 2);
        Expect(NearPixel(ActivePixel(session, 0, 2), {0, 0, 0, 255}) &&
                   NearPixel(ActivePixel(session, 100, 2), {0, 0, 255, 255}) && Near(middle[2], 128) &&
                   middle[1] == 0 && middle[3] == 255,
               "gradient.foreground-to-transparent");
    }
    // Legacy cancelUndoAndClicksLeaveDocumentUntouched.
    {
        DocumentSession session = GradientSession();
        const CanvasDocument before = session.Document().value();
        session.BeginGradient(Vec2{10, 2});
        session.EndGradientDrag();
        Expect(session.PendingGradient() == nullptr, "gradient.click-pends-nothing");
        DragGradient(session, Vec2{0, 2}, Vec2{100, 2});
        session.CancelGradient();
        Expect(session.PendingGradient() == nullptr && session.Document().value() == before, "gradient.cancel");
        DragGradient(session, Vec2{0, 2}, Vec2{100, 2});
        session.Undo();
        Expect(session.PendingGradient() == nullptr && session.Document().value() == before, "gradient.undo-drops");
    }
    // Legacy redraggingReplacesPendingLineWithoutAccumulating.
    {
        DocumentSession session = GradientSession();
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        DragGradient(session, Vec2{100.5, 2}, Vec2{0.5, 2});
        session.CommitGradient();
        Expect(NearPixel(ActivePixel(session, 0, 2), {255, 255, 255, 255}) &&
                   NearPixel(ActivePixel(session, 100, 2), {0, 0, 0, 255}),
               "gradient.redrag-replaces");
    }
    // Legacy maskGradientWritesCoverageInsideLayerBounds: the default mask palette paints black (hide) to white.
    {
        DocumentSession session = GradientSession();
        DragGradient(session, Vec2{0, 2}, Vec2{0.6, 2});
        session.CommitGradient(); // Opaque black layer.
        session.AddLayerMask(true);
        const Guid id = session.ActiveLayerID().value();
        session.SelectLayerTarget(id, true);
        Expect(session.IsMaskSelected(), "gradient.mask-target");
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        session.CommitGradient();
        Expect(session.history.UndoName() == L"Gradient Mask" && Near(ActiveMaskValue(session, 0, 2), 0) &&
                   Near(ActiveMaskValue(session, 100, 2), 255) && Near(ActiveMaskValue(session, 50, 2), 128),
               "gradient.mask");
    }
    // Legacy paletteChangesUpdatePendingPreview, and the pending tiles feed the preview.
    {
        DocumentSession session = GradientSession();
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        const BrushStroke& raster = *session.PendingGradient()->raster;
        Expect(!raster.Tiles().empty() && PixelOf(raster.Tiles().front().image, 0, 2)[2] == 0,
               "gradient.preview-tiles");
        session.SwapPaletteColors();
        Expect(session.GradientColors()[0][0] == 1.0 && PixelOf(raster.Tiles().front().image, 0, 2)[2] == 255,
               "gradient.palette-updates-preview");
        // Switching layers applies it.
        const int count = session.history.UndoCount();
        session.AddBlankLayer();
        Expect(session.PendingGradient() == nullptr && session.history.UndoCount() == count + 2,
               "gradient.layer-switch-applies");
    }
}

DocumentSession ShapeSession()
{
    DocumentSession session;
    session.CreateDocument(100, 80, true);
    session.SetForegroundColor(0xFFFF0000u);
    return session;
}

void DragShape(DocumentSession& session, Vec2 from, Vec2 to, bool square = false, bool fromCenter = false)
{
    session.BeginShape(from);
    session.DragShape(to, square, fromCenter);
    session.FinishShape();
}

void TestShapeTool()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    // Legacy rectangleFillsANewLayerWithTheForegroundColorAsOneUndoStep.
    {
        DocumentSession session = ShapeSession();
        session.SelectAll();
        const int count = session.history.UndoCount();
        DragShape(session, Vec2{10, 10}, Vec2{40, 30});
        const std::vector<ImageLayer>& layers = session.Document().value().layers;
        Expect(layers.size() == 2 && layers[0].name == L"Layer 1" && layers[1].name == L"Rectangle 1" &&
                   session.ActiveLayer()->name == L"Rectangle 1" && session.history.UndoCount() == count + 1 &&
                   session.history.UndoName() == L"Rectangle",
               "shape.rectangle-layer");
        Expect(session.ActiveLayer()->transform.origin == Vec2{10, 10} &&
                   session.ActiveLayer()->transform.size == Vec2{30, 20} && session.Selection().has_value() &&
                   LiveShape(*session.ActiveLayer()).has_value(),
               "shape.rectangle-placed-keeps-selection");
        Expect(ActivePixel(session, 25, 20) == red && ActivePixel(session, 10, 10) == red &&
                   ActivePixel(session, 39, 29) == red && ActivePixel(session, 9, 20)[3] == 0 &&
                   ActivePixel(session, 40, 20)[3] == 0 && ActivePixel(session, 25, 30)[3] == 0,
               "shape.rectangle-pixels");
        DragShape(session, Vec2{60, 10}, Vec2{70, 20});
        Expect(session.ActiveLayer()->name == L"Rectangle 2", "shape.next-name");
        session.Undo();
        session.Undo();
        Expect(session.Document().value().layers.size() == 1, "shape.undo");
    }
    // Legacy ellipseLeavesItsCornersClearWithShiftCircleAndOptionFromCenter.
    {
        DocumentSession session = ShapeSession();
        session.ToggleShapeKind();
        Expect(session.shapeKind == ShapeKind::Ellipse, "shape.toggle-kind");
        DragShape(session, Vec2{50, 40}, Vec2{60, 45}, true, true);
        Expect(session.ActiveLayer()->name == L"Ellipse 1" && session.history.UndoName() == L"Ellipse" &&
                   session.ActiveLayer()->transform.origin == Vec2{40, 30} &&
                   session.ActiveLayer()->transform.size == Vec2{20, 20},
               "shape.ellipse-layer");
        Expect(ActivePixel(session, 50, 40) == red && ActivePixel(session, 41, 40)[3] > 0 &&
                   ActivePixel(session, 50, 31)[3] > 0 && ActivePixel(session, 40, 30)[3] == 0 &&
                   ActivePixel(session, 59, 49)[3] == 0,
               "shape.ellipse-pixels");
    }
    // Legacy aClickEscapeOrToolSwitchMakesNoLayer (the host cancels the draft when the tool changes).
    {
        DocumentSession session = ShapeSession();
        const int count = session.history.UndoCount();
        session.BeginShape(Vec2{20, 20});
        session.FinishShape();
        session.BeginShape(Vec2{20, 20});
        session.DragShape(Vec2{50, 50}, false, false);
        const std::optional<ShapeDraft>& draft = session.PendingShape();
        Expect(draft.has_value() && draft->rect.minX == 20 && draft->rect.minY == 20 && draft->rect.maxX == 50 &&
                   draft->rect.maxY == 50,
               "shape.draft-box");
        session.CancelShape();
        Expect(!session.PendingShape().has_value() && session.history.UndoCount() == count &&
                   session.Document().value().layers.size() == 1,
               "shape.click-and-cancel-make-nothing");
    }
    // Legacy roundedRectanglesFollowTheRadiusAndClampToAPill.
    {
        DocumentSession session = ShapeSession();
        session.shapeCornerRadius = 8;
        DragShape(session, Vec2{10, 10}, Vec2{50, 40}); // 40 × 30
        session.shapeCornerRadius = 500;
        DragShape(session, Vec2{55, 50}, Vec2{95, 70}); // 40 × 20: radius 10
        session.SelectLayer(session.Document().value().layers[1].id);
        Expect(ActivePixel(session, 10, 10)[3] == 0 && ActivePixel(session, 11, 11)[3] == 0 &&
                   ActivePixel(session, 13, 13)[3] == 255 && ActivePixel(session, 30, 10)[3] == 255 &&
                   ActivePixel(session, 30, 25) == red,
               "shape.rounded-corners");
        session.SelectLayer(session.Document().value().layers[2].id);
        Expect(ActivePixel(session, 55, 50)[3] == 0 && ActivePixel(session, 75, 60) == red &&
                   session.Document().value().layers.size() == 3,
               "shape.pill");
        session.ToggleShapeKind();
        session.BeginShape(Vec2{5, 5});
        Expect(session.PendingShape().has_value() && session.PendingShape()->cornerRadius == 0,
               "shape.ellipse-takes-no-radius");
        session.CancelShape();
    }
    // A scaled shape layer draws its shape again at the new size (the corner keeps its radius); painting on it
    // makes it plain pixels that stretch instead.
    {
        DocumentSession session = ShapeSession();
        session.shapeCornerRadius = 8;
        DragShape(session, Vec2{10, 10}, Vec2{50, 40});
        session.BeginTransform(false);
        LayerTransform doubled = session.ActiveLayer()->transform;
        doubled.size = Vec2{80, 60};
        session.PreviewTransform(doubled);
        session.CommitTransform();
        const ImageLayer* scaled = session.ActiveLayer();
        Expect(scaled->asset->image.Width() == 80 && scaled->asset->image.Height() == 60 &&
                   LiveShape(*scaled).has_value() && ActivePixel(session, 11, 11)[3] == 0 &&
                   ActivePixel(session, 20, 20) == red,
               "shape.redraws-when-scaled");
        session.brushSettings = BrushSettings{10, 1, 0, 0, 1, 1, false};
        Expect(session.BeginBrush(Vec2{40, 30}, BrushKind::Paint), "shape.paint-begins");
        session.FinishBrush();
        Expect(!LiveShape(*session.ActiveLayer()).has_value(), "shape.painted-is-plain-pixels");
    }
}

void TestEyedropper()
{
    // The composite's straight sRGB color under a document point; nothing over transparency or off the canvas.
    std::vector<std::uint8_t> bytes(4u * 4u * 4u, 0);
    const std::array<std::uint8_t, 4> half{64, 0, 128, 128}; // premultiplied: blue 0.5, green 0, red 1 at 50 %
    std::memcpy(bytes.data() + (1u * 4u + 1u) * 4u, half.data(), 4);
    const std::array<std::uint8_t, 4> teal{200, 150, 20, 255};
    std::memcpy(bytes.data() + (2u * 4u + 3u) * 4u, teal.data(), 4);
    const Raster composite = Raster::Make(4, 4, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    Expect(DocumentSession::SampleCompositeColor(composite, Vec2{1.7, 1.2}) == 0xFFFF0080u,
           "eyedropper.unpremultiplies");
    Expect(DocumentSession::SampleCompositeColor(composite, Vec2{3.5, 2.5}) == 0xFF1496C8u, "eyedropper.opaque");
    Expect(!DocumentSession::SampleCompositeColor(composite, Vec2{0.5, 0.5}).has_value() &&
               !DocumentSession::SampleCompositeColor(composite, Vec2{4, 1}).has_value() &&
               !DocumentSession::SampleCompositeColor(composite, Vec2{-1, 1}).has_value(),
           "eyedropper.nothing-over-transparency-or-outside");
    DocumentSession session;
    session.CreateDocument(4, 4, true);
    Expect(session.showsSampleRing, "eyedropper.ring-shown-by-default");
}

// ── Pixel edits (C9b) ─────────────────────────────────────────

DocumentSession PixelEditSession(int width = 100, int height = 40)
{
    DocumentSession session;
    session.CreateDocument(width, height, true);
    return session;
}

void SelectRect(DocumentSession& session, double x, double y, double width, double height, bool antialiased = true)
{
    session.selectionAntialiased = antialiased;
    session.ApplySelection(SelectionGeometry::Rect(x, y, width, height), SelectionMode::New, L"Select");
}

// A layer's pixel under document (x, y), read through its transform (nearest); transparent beyond it.
std::array<std::uint8_t, 4> LayerPixelOf(const DocumentSession& session, const Guid& id, int x, int y)
{
    std::array<std::uint8_t, 4> out{0, 0, 0, 0};
    const ImageLayer* layer = Find(session.Document(), id);
    if (layer == nullptr || !layer->asset.has_value())
    {
        return out;
    }
    const Raster& image = layer->asset->image;
    const Vec2 pixel =
        PixelToDocument(layer->transform, image.Width(), image.Height()).Inverted().Apply(Vec2{x + 0.5, y + 0.5});
    if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= image.Width() || pixel.y >= image.Height())
    {
        return out;
    }
    image.ReadRegion(static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)), 1, 1, out.data(), 4,
                     0);
    return out;
}

// A 100×40 layer: red left half, blue right half, made with the palette fills.
void TwoColorLayer(DocumentSession& session)
{
    session.SetForegroundColor(0xFFFF0000u);
    session.SetBackgroundColor(0xFF0000FFu);
    session.FillSelection(FillSource::Background);
    SelectRect(session, 0, 0, 50, 40);
    session.FillSelection(FillSource::Foreground);
    session.Deselect();
}

std::optional<SelectionBounds> SelectionBox(const DocumentSession& session)
{
    return session.Selection().has_value() ? session.Selection()->Bounds() : std::nullopt;
}

bool BoxIs(const std::optional<SelectionBounds>& box, double minX, double minY, double maxX, double maxY)
{
    return box.has_value() && std::fabs(box->minX - minX) < 0.01 && std::fabs(box->minY - minY) < 0.01 &&
           std::fabs(box->maxX - maxX) < 0.01 && std::fabs(box->maxY - maxY) < 0.01;
}

void TestPixelEdits()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    // Legacy brushPaintsOnlyInsideTheSelection.
    {
        DocumentSession session = PixelEditSession();
        SelectRect(session, 0, 0, 50, 40);
        session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1, false};
        Expect(session.BeginBrush(Vec2{5, 20}), "edit.brush-begins");
        session.ContinueBrush(Vec2{95, 20});
        session.FinishBrush();
        Expect(ActivePixel(session, 25, 20) == red && ActivePixel(session, 75, 20)[3] == 0,
               "edit.brush-inside-selection");
    }
    // Legacy emptySelectionEditsNothing.
    {
        DocumentSession session = PixelEditSession();
        SelectRect(session, 10, 10, 10, 10);
        session.ApplySelection(SelectionGeometry::Rect(0, 0, 100, 40), SelectionMode::Subtract, L"Subtract");
        Expect(session.Selection().has_value() && session.Selection()->IsEmpty(), "edit.empty-selection");
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1, false};
        static_cast<void>(session.BeginBrush(Vec2{5, 20}));
        session.ContinueBrush(Vec2{95, 20});
        session.FinishBrush();
        session.FillSelection(FillSource::Foreground);
        session.ClearSelectedPixels();
        Expect(!session.BeginGradient(Vec2{0, 20}) && session.PendingGradient() == nullptr, "edit.empty-no-gradient");
        Expect(!session.CanPaint() && !session.CanEditPixels() && session.Document().value() == before &&
                   session.history.UndoCount() == count && ActivePixel(session, 50, 20)[3] == 0,
               "edit.empty-selection-edits-nothing");
    }
    // Legacy gradientStaysInsideTheSelection.
    {
        DocumentSession session = PixelEditSession();
        SelectRect(session, 0, 0, 50, 40);
        session.gradientSettings.style = GradientStyle::ForegroundToBackground;
        Expect(session.BeginGradient(Vec2{0.5, 20}), "edit.gradient-begins");
        session.MoveGradient(std::nullopt, Vec2{99.5, 20});
        session.CommitGradient();
        Expect(ActivePixel(session, 10, 20)[3] == 255 && ActivePixel(session, 75, 20)[3] == 0,
               "edit.gradient-inside-selection");
    }
    // Legacy fillUsesPaletteInsideSelectionOrWholeLayerWithoutOne.
    {
        DocumentSession session = PixelEditSession();
        session.SetForegroundColor(0xFFFF0000u);
        SelectRect(session, 20, 10, 30, 20);
        const int count = session.history.UndoCount();
        session.FillSelection(FillSource::Foreground);
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Fill" &&
                   ActivePixel(session, 30, 20) == red && ActivePixel(session, 5, 5)[3] == 0,
               "edit.fill-selection");
        session.Deselect();
        session.FillSelection(FillSource::Background);
        Expect(ActivePixel(session, 5, 5) == white && ActivePixel(session, 30, 20) == white, "edit.fill-whole-layer");
    }
    // Legacy deleteClearsSelectedPixelsOrDeletesTheLayerWithoutASelection.
    {
        DocumentSession session = PixelEditSession();
        session.SetForegroundColor(0xFFFF0000u);
        session.FillSelection(FillSource::Foreground);
        SelectRect(session, 20, 10, 30, 20);
        session.ClearSelectedPixels();
        Expect(session.history.UndoName() == L"Clear" && ActivePixel(session, 30, 20)[3] == 0 &&
                   ActivePixel(session, 5, 5) == red,
               "edit.clear");
        session.Undo();
        Expect(ActivePixel(session, 30, 20) == red, "edit.clear-undo");
        session.Deselect();
        session.DeleteKeyPressed();
        Expect(session.Document().value().layers.empty(), "edit.delete-key-deletes-the-layer");
    }
    // Legacy maskFillHidesOnlyTheSelectedArea.
    {
        DocumentSession session = PixelEditSession();
        session.SetForegroundColor(0xFFFF0000u);
        session.FillSelection(FillSource::Foreground);
        session.AddLayerMask(true);
        session.SelectLayerTarget(session.ActiveLayerID().value(), true);
        SelectRect(session, 20, 10, 30, 20);
        session.FillSelection(FillSource::Foreground);
        Expect(session.history.UndoName() == L"Fill Mask" && ActiveMaskValue(session, 30, 20) == 0 &&
                   ActiveMaskValue(session, 5, 5) == 255,
               "edit.mask-fill-hides-the-selection");
        session.ClearSelectedPixels();
        Expect(ActiveMaskValue(session, 30, 20) == 255, "edit.mask-clear-reveals");
    }
    // Legacy clipFollowsScaledLayersAndSoftensEdges: a 50×20 image stretched 2× over the 100×40 canvas.
    {
        DocumentSession session = PixelEditSession();
        const Guid id = InsertPixels(session, SolidRaster(50, 20, 255, 0, 0, 255), L"Blue");
        session.BeginTransform(true);
        LayerTransform stretched = session.ActiveLayer()->transform;
        stretched.origin = Vec2{0, 0};
        stretched.size = Vec2{100, 40};
        session.PreviewTransform(stretched);
        session.CommitTransform();
        session.selectionAntialiased = true;
        session.ApplySelection({SelectionLoop{Vec2{0, 0}, Vec2{100, 0}, Vec2{0, 40}}}, SelectionMode::New, L"Select");
        session.ClearSelectedPixels();
        bool soft = false;
        for (int x = 0; x < 100; ++x)
        {
            const int y = std::min(39, static_cast<int>((1.0 - x / 100.0) * 40));
            const std::uint8_t alpha = LayerPixelOf(session, id, x, y)[3];
            soft = soft || (alpha > 0 && alpha < 255);
        }
        Expect(LayerPixelOf(session, id, 10, 10)[3] == 0 && LayerPixelOf(session, id, 90, 35)[3] == 255 && soft,
               "edit.clip-follows-scaled-layers");
    }
    // Legacy maskButtonAddsWhiteMaskOrHidesTheSelection and layerMenuMasksUseTheSelection.
    {
        DocumentSession session = PixelEditSession();
        session.SetForegroundColor(0xFFFF0000u);
        session.FillSelection(FillSource::Foreground);
        session.AddMask();
        Expect(session.ActiveLayer()->mask.has_value() && session.history.UndoName() == L"Add Reveal-All Mask" &&
                   MaskValue(session, 30.5, 20.5) == 255,
               "edit.mask-button-without-selection");
        session.Undo();
        Expect(!session.ActiveLayer()->mask.has_value(), "edit.mask-button-undo");
        SelectRect(session, 20, 10, 30, 20);
        session.AddMask();
        Expect(session.history.UndoName() == L"Add Mask from Selection" && !session.Selection().has_value() &&
                   session.IsMaskSelected() && ActiveMaskValue(session, 30, 20) == 0 &&
                   ActiveMaskValue(session, 5, 5) == 255,
               "edit.mask-from-selection-hides-it");
        session.Undo();
        Expect(!session.ActiveLayer()->mask.has_value() && session.Selection().has_value(),
               "edit.mask-from-selection-undo");
        session.AddMask(false);
        Expect(session.history.UndoName() == L"Add Mask from Selection" && ActiveMaskValue(session, 30, 20) == 255 &&
                   ActiveMaskValue(session, 5, 5) == 0,
               "edit.black-mask-from-selection-shows-it");
        session.Undo();
        session.Deselect();
        session.AddMask(false);
        Expect(session.history.UndoName() == L"Add Hide-All Mask" && MaskValue(session, 30.5, 20.5) == 0,
               "edit.black-mask-without-selection");
    }
    // Legacy maskFromSelectionLinesUpOnScaledLayers.
    {
        DocumentSession session = PixelEditSession(100, 100);
        const Guid id = InsertPixels(session, SolidRaster(50, 50, 255, 0, 0, 255), L"Blue");
        for (ImageLayer& layer : session.Document().value().layers)
        {
            if (layer.id == id)
            {
                layer.transform.origin = Vec2{0, 0};
                layer.transform.size = Vec2{100, 100};
            }
        }
        SelectRect(session, 0, 0, 50, 50);
        session.AddMask();
        const ImageLayer* layer = session.ActiveLayer();
        std::uint8_t inside = 255;
        std::uint8_t outside = 0;
        std::uint8_t beside = 0;
        if (layer != nullptr && layer->mask.has_value())
        {
            layer->mask->asset.image.ReadRegion(12, 12, 1, 1, &inside, 1, 255);
            layer->mask->asset.image.ReadRegion(37, 37, 1, 1, &outside, 1, 0);
            layer->mask->asset.image.ReadRegion(37, 12, 1, 1, &beside, 1, 0);
        }
        Expect(layer != nullptr && layer->mask.has_value() && layer->mask->asset.image.Width() == 50 && inside == 0 &&
                   outside == 255 && beside == 255,
               "edit.mask-from-selection-uses-the-layer-grid");
    }
    // Legacy deletingWithTheMaskTargetedRemovesOnlyTheMask.
    {
        DocumentSession session = PixelEditSession();
        const Guid id = session.ActiveLayerID().value();
        session.AddLayerMask(false);
        Expect(session.IsMaskSelected(), "edit.mask-targeted");
        session.DeleteKeyPressed();
        Expect(session.ActiveLayer() != nullptr && session.ActiveLayer()->id == id &&
                   !session.ActiveLayer()->mask.has_value() && session.history.UndoName() == L"Delete Layer Mask" &&
                   !session.IsMaskSelected(),
               "edit.delete-removes-only-the-mask");
        session.Undo();
        Expect(session.ActiveLayer()->mask.has_value(), "edit.delete-mask-undo");
        session.SelectLayerTarget(id, true);
        session.DeleteLayerOrMask();
        Expect(session.Document().value().layers.size() == 1 && !session.ActiveLayer()->mask.has_value(),
               "edit.trash-follows-the-target");
        session.AddLayerMask(true);
        session.SelectLayerTarget(id, false);
        session.DeleteLayerOrMask();
        Expect(session.Document().value().layers.empty(), "edit.trash-deletes-the-layer");
    }
    // Legacy invertKeepsTransparencyStaysInSelectionAndWorksOnMasks.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 0, 0, 100, 20);
        session.InvertPixels();
        Expect(session.history.UndoName() == L"Invert" &&
                   ActivePixel(session, 10, 5) == std::array<std::uint8_t, 4>{255, 255, 0, 255} &&
                   ActivePixel(session, 90, 5) == std::array<std::uint8_t, 4>{0, 255, 255, 255} &&
                   ActivePixel(session, 10, 30) == red,
               "edit.invert-inside-selection");
        session.Deselect();
        DocumentSession blank = PixelEditSession();
        InsertPixels(blank, SolidRaster(100, 40, 128, 128, 128, 128), L"Half");
        blank.InvertPixels();
        const std::array<std::uint8_t, 4> half = ActivePixel(blank, 50, 20);
        Expect(std::abs(static_cast<int>(half[3]) - 128) <= 1 && half[0] == 0 && half[2] == 0,
               "edit.invert-keeps-alpha");
        session.AddLayerMask(true);
        session.InvertPixels();
        Expect(session.history.UndoName() == L"Invert Mask" && ActiveMaskValue(session, 50, 30) == 0,
               "edit.invert-mask");
    }
    // Legacy invertIsFastOnLargeImagesAndHandlesUniformMasksWithASelection, at a size the CPU pass covers in tests.
    {
        DocumentSession session = PixelEditSession(1000, 750);
        InsertPixels(session, SolidRaster(1000, 750, 0, 0, 255, 255), L"Big");
        session.InvertPixels();
        SelectRect(session, 0, 0, 500, 750);
        session.InvertPixels();
        Expect(ActivePixel(session, 100, 100) == red &&
                   ActivePixel(session, 900, 100) == std::array<std::uint8_t, 4>{255, 255, 0, 255},
               "edit.invert-twice-and-once");
        session.AddLayerMask(true);
        session.InvertPixels();
        Expect(ActiveMaskValue(session, 100, 100) == 0 && ActiveMaskValue(session, 900, 100) == 255,
               "edit.invert-uniform-mask-with-selection");
    }
    // Legacy invertWorksInEveryTool: a pending gradient is applied first, then inverted.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        Expect(session.CanInvert(), "edit.can-invert");
        Expect(session.BeginGradient(Vec2{0, 20}), "edit.invert-gradient-begins");
        session.MoveGradient(std::nullopt, Vec2{100, 20});
        Expect(session.PendingGradient() != nullptr && session.CanInvert(), "edit.invert-with-pending-gradient");
        session.InvertPixels();
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Invert",
               "edit.invert-applies-the-gradient");
        session.Undo();
        Expect(session.history.UndoName() == L"Gradient", "edit.invert-undo-leaves-the-gradient");
    }
}

void TestPixelMoves()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    // Legacy cmdDragMovesSelectedPixelsAndOutlineAsOneUndo.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 10, 10, 10, 10);
        const int count = session.history.UndoCount();
        Expect(session.BeginPixelMove(), "pixelmove.begins");
        session.MovePixels(30.4, 0);
        session.MovePixels(60.2, 5);
        session.FinishPixelMove();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Move Pixels" &&
                   BoxIs(SelectionBox(session), 70, 15, 80, 25),
               "pixelmove.one-entry-with-the-outline");
        Expect(ActivePixel(session, 15, 15)[3] == 0 && ActivePixel(session, 75, 20) == red &&
                   ActivePixel(session, 85, 20) == blue && ActivePixel(session, 5, 5) == red,
               "pixelmove.pixels-moved");
        session.Undo();
        Expect(BoxIs(SelectionBox(session), 10, 10, 20, 20) && ActivePixel(session, 15, 15) == red, "pixelmove.undo");
    }
    // Legacy duplicatePixelDragPreservesSourceAndUndoesTogether.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 10, 10, 10, 10);
        const int count = session.history.UndoCount();
        Expect(session.BeginPixelMove(true), "pixelmove.duplicate-begins");
        session.MovePixels(60, 5);
        session.FinishPixelMove();
        Expect(ActivePixel(session, 15, 15)[2] > 250 && ActivePixel(session, 75, 20)[2] > 250 &&
                   session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Duplicate Pixels",
               "pixelmove.duplicate-keeps-the-source");
        session.Undo();
        Expect(ActivePixel(session, 15, 15)[2] > 250 && ActivePixel(session, 75, 20)[0] > 250,
               "pixelmove.duplicate-undo");
    }
    // Legacy cmdArrowNudgesPixelsAndMasksRefuse.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 40, 0, 10, 40);
        session.NudgePixels(10, 0);
        Expect(ActivePixel(session, 45, 20)[3] == 0 && ActivePixel(session, 55, 20) == red, "pixelmove.nudge");
        session.AddLayerMask(true);
        Expect(session.IsMaskSelected() && !session.BeginPixelMove(), "pixelmove.masks-refuse");
        session.CancelPixelMove();
    }
    // Legacy pixelMoveNeverShowsTheOutlineAtItsOldSpot.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 10, 10, 10, 10);
        Expect(session.BeginPixelMove(), "pixelmove.outline-begins");
        session.MovePixels(30, 0);
        const std::optional<DocumentSelection> displayed = session.DisplayedSelection();
        Expect(displayed.has_value() && BoxIs(displayed->Bounds(), 40, 10, 50, 20) &&
                   BoxIs(SelectionBox(session), 10, 10, 20, 20),
               "pixelmove.outline-follows-the-drag");
        session.FinishPixelMove();
        Expect(BoxIs(SelectionBox(session), 40, 10, 50, 20) && session.PendingPixelMove() == nullptr,
               "pixelmove.outline-stays-moved");
        // The pending raster feeds the preview like a stroke.
        Expect(session.BeginPixelMove(), "pixelmove.preview-begins");
        session.MovePixels(5, 0);
        Expect(session.MutablePixelMove() != nullptr && !session.MutablePixelMove()->raster->Tiles().empty(),
               "pixelmove.preview-tiles");
        session.CancelPixelMove();
        Expect(session.PendingPixelMove() == nullptr && BoxIs(SelectionBox(session), 40, 10, 50, 20),
               "pixelmove.cancel");
    }
}

void TestClipboard()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const std::array<std::uint8_t, 4> green{0, 255, 0, 255};
    // Legacy copyAndPastePutsPixelsOnANewLayerInPlace.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const Guid source = session.ActiveLayerID().value();
        SelectRect(session, 40, 10, 20, 20);
        Expect(session.CopySelection() && session.CanPaste(), "clipboard.copy");
        const int count = session.history.UndoCount();
        session.Paste();
        const ImageLayer* pasted = session.ActiveLayer();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Paste" &&
                   pasted != nullptr && pasted->id != source && pasted->name == L"Layer 2" &&
                   !session.Selection().has_value() && pasted->transform.origin == Vec2{40, 10} &&
                   pasted->transform.size == Vec2{20, 20},
               "clipboard.paste-in-place");
        Expect(LayerPixelOf(session, pasted->id, 45, 15) == red && LayerPixelOf(session, pasted->id, 55, 15) == blue &&
                   LayerPixelOf(session, pasted->id, 30, 15)[3] == 0 && LayerPixelOf(session, source, 45, 15) == red,
               "clipboard.pasted-pixels");
        session.Undo();
        Expect(session.Document().value().layers.size() == 1, "clipboard.paste-undo");
    }
    // Legacy cutLeavesAHoleAndPasteRestoresThePixels.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const Guid source = session.ActiveLayerID().value();
        SelectRect(session, 10, 10, 10, 10);
        session.CutSelection();
        Expect(LayerPixelOf(session, source, 15, 15)[3] == 0 && session.history.UndoName() == L"Clear",
               "clipboard.cut");
        session.Paste();
        Expect(LayerPixelOf(session, session.ActiveLayerID().value(), 15, 15) == red, "clipboard.paste-restores");
    }
    // Legacy layerViaCopyCopiesTheSelectionOrDuplicatesTheLayer.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const ImageLayer source = *session.ActiveLayer();
        SelectRect(session, 60, 0, 10, 40);
        session.LayerViaCopy();
        Expect(session.history.UndoName() == L"Layer via Copy" && session.Document().value().layers.size() == 2 &&
                   session.ActiveLayer()->transform.size == Vec2{10, 40} && !session.Selection().has_value(),
               "clipboard.layer-via-copy");
        session.SelectLayer(source.id);
        session.LayerViaCopy();
        Expect(session.history.UndoName() == L"Duplicate Layer" &&
                   session.ActiveLayer()->name == source.name + L" copy" &&
                   session.ActiveLayer()->asset->image == source.asset->image,
               "clipboard.layer-via-copy-duplicates");
    }
    // Legacy lassoShapedSelectionCopiesAndPastes.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        Lasso(session, {Vec2{10, 5}, Vec2{40, 5}, Vec2{10, 35}});
        Expect(session.history.UndoName() == L"Lasso" && session.CanCopyPixels(), "clipboard.lasso-copyable");
        Expect(session.CopySelection(), "clipboard.lasso-copy");
        session.Paste();
        const ImageLayer* pasted = session.ActiveLayer();
        Expect(pasted != nullptr && pasted->transform.origin == Vec2{10, 5} && pasted->transform.size == Vec2{30, 30} &&
                   LayerPixelOf(session, pasted->id, 15, 10) == red &&
                   LayerPixelOf(session, pasted->id, 38, 33)[3] == 0,
               "clipboard.lasso-paste");
    }
    // Legacy copyMergedTakesEveryVisibleLayerNotJustTheActiveOne, with the host's composite built by hand.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const Guid greenLayer = InsertPixels(session, SolidRaster(20, 20, 0, 255, 0, 255), L"Green");
        for (ImageLayer& layer : session.Document().value().layers)
        {
            if (layer.id == greenLayer)
            {
                layer.transform.origin = Vec2{60, 10};
            }
        }
        SelectRect(session, 55, 5, 30, 30);
        Expect(session.CopySelection(), "clipboard.active-layer-copy");
        session.Paste();
        Expect(LayerPixelOf(session, session.ActiveLayerID().value(), 65, 15) == green &&
                   LayerPixelOf(session, session.ActiveLayerID().value(), 58, 8)[3] == 0,
               "clipboard.active-layer-only");
        session.Undo();
        session.SelectLayer(greenLayer);
        SelectRect(session, 55, 5, 30, 30);
        const Raster composite = BytesRaster(100, 40,
                                             [&](int x, int y)
                                             {
                                                 if (x >= 60 && x < 80 && y >= 10 && y < 30)
                                                 {
                                                     return green;
                                                 }
                                                 return x < 50 ? red : blue;
                                             });
        Expect(session.CanCopyMerged() && session.CopyMergedSelection(composite), "clipboard.copy-merged");
        session.Paste();
        Expect(LayerPixelOf(session, session.ActiveLayerID().value(), 65, 15) == green &&
                   LayerPixelOf(session, session.ActiveLayerID().value(), 58, 8) == blue,
               "clipboard.merged-takes-every-layer");
        // Pasting another app's image centers it.
        session.PasteImage(SolidRaster(10, 10, 0, 255, 255, 255));
        Expect(session.history.UndoName() == L"Paste" && session.ActiveLayer()->transform.origin == Vec2{45, 15},
               "clipboard.external-paste-centers");
    }
}

void TestFloatingSelection()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    // Legacy transformSelectionMovesPixelsAndOutlineAsOneUndo.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const Guid source = session.ActiveLayerID().value();
        SelectRect(session, 10, 10, 10, 10);
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        Expect(session.CanTransformSelection(), "floating.can-transform");
        session.TransformCommand();
        Expect(session.PendingTransformEdit().has_value() && session.PendingTransformEdit()->floating.has_value() &&
                   session.ActiveLayer()->name == L"Floating Selection",
               "floating.begins-on-a-floating-layer");
        LayerTransform draft = session.PendingTransformEdit()->draft;
        draft.origin = Vec2{70, 20};
        session.PreviewTransform(draft);
        const std::optional<DocumentSelection> displayed = session.DisplayedSelection();
        Expect(displayed.has_value() && BoxIs(displayed->Bounds(), 70, 20, 80, 30),
               "floating.outline-follows-the-handles");
        session.CommitTransform();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Transform Selection" &&
                   session.Document().value().layers.size() == 1 && session.ActiveLayerID() == source &&
                   BoxIs(SelectionBox(session), 70, 20, 80, 30),
               "floating.merges-as-one-entry");
        Expect(ActivePixel(session, 15, 15)[3] == 0 && ActivePixel(session, 75, 25) == red &&
                   ActivePixel(session, 85, 25) == blue,
               "floating.merged-pixels");
        session.Undo();
        Expect(session.Document().value() == before, "floating.undo");
    }
    // Legacy escapeRestoresExactlyWithoutAnUndoStep.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 10, 10, 10, 10);
        const CanvasDocument before = session.Document().value();
        const std::optional<Guid> active = session.ActiveLayerID();
        const int count = session.history.UndoCount();
        session.BeginSelectionTransform();
        LayerTransform draft = session.PendingTransformEdit()->draft;
        draft.origin.x += 30;
        session.PreviewTransform(draft);
        session.CancelTransform();
        Expect(session.Document().value() == before && session.ActiveLayerID() == active &&
                   !session.PendingTransformEdit().has_value() && session.history.UndoCount() == count,
               "floating.escape-restores-exactly");
    }
    // Legacy applyingAnUnchangedTransformLeavesSoftEdgesUntouched.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        session.selectionAntialiased = true;
        session.ApplySelection(SelectionGeometry::Ellipse(10.3, 5.7, 30, 25), SelectionMode::New, L"Select");
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        session.BeginSelectionTransform();
        session.CommitTransform();
        Expect(session.Document().value() == before && session.history.UndoCount() == count,
               "floating.unchanged-restores");
    }
    // Legacy scalingAndMovingPastTheLayerEdgeGrowsTheLayer (a tool switch applies it; here the commit itself).
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const Guid source = session.ActiveLayerID().value();
        SelectRect(session, 0, 0, 10, 10);
        session.BeginSelectionTransform();
        LayerTransform draft = session.PendingTransformEdit()->draft;
        draft.size = Vec2{20, 20};
        draft.origin = Vec2{90, 30};
        session.PreviewTransform(draft);
        session.CommitTransform();
        const ImageLayer* layer = Find(session.Document(), source);
        Expect(!session.PendingTransformEdit().has_value() && session.history.UndoName() == L"Transform Selection" &&
                   layer != nullptr && layer->transform.size == Vec2{110, 50},
               "floating.grows-the-layer");
        Expect(LayerPixelOf(session, source, 95, 35) == red && LayerPixelOf(session, source, 5, 5)[3] == 0 &&
                   BoxIs(SelectionBox(session), 90, 30, 110, 50),
               "floating.grown-pixels-and-outline");
    }
    // Undo during the edit drops it, like Escape.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 10, 10, 10, 10);
        const CanvasDocument before = session.Document().value();
        session.BeginSelectionTransform();
        session.Undo();
        Expect(!session.PendingTransformEdit().has_value() && session.Document().value() == before,
               "floating.undo-drops");
    }
}

int wmain()
{
    TestHierarchy();
    TestLiveMask();
    TestValidate();
    TestSessionGroups();
    TestHiddenParent();
    TestAppearance();
    TestClippingLink();
    TestHistory();
    TestHistoryBounds();
    TestHistoryTrim256MiB();
    TestSelection();
    TestMaskPlacementHelpers();
    TestCollapsedRows();
    TestDeleteClearsClipping();
    TestNamingAndInsertion();
    TestPaletteColors();
    TestTransformGeometry();
    TestTransformDrag();
    TestTransformSnap();
    TestTransformSession();
    TestTransformDuplicate();
    TestTransformGroup();
    TestMaskTransform();
    TestFlipCommands();
    TestDistortWarp();
    TestSelectionGeometry();
    TestSelectionCoverage();
    TestSelectionSession();
    TestSelectionMove();
    TestSelectionModify();
    TestSelectionLoad();
    TestMarquee();
    TestWandKernel();
    TestWandSession();
    TestSparseRaster();
    TestBrushKernel();
    TestBrushStroke();
    TestBrushSoftAndCancel();
    TestBrushMask();
    TestBrushOpacityAndAccumulation();
    TestBrushCurveAndTail();
    TestBrushCrossings();
    TestBrushSnapshots();
    TestBrushBoundsAndBudget();
    TestBrushRejectsAndKeys();
    TestBrushExpansion();
    TestBrushSelectionClip();
    TestSpotHealing();
    TestCloneStamp();
    TestBlurTool();
    TestSmearWarp();
    TestGradientTool();
    TestShapeTool();
    TestEyedropper();
    TestPixelEdits();
    TestPixelMoves();
    TestClipboard();
    TestFloatingSelection();
    if (gFailures != 0)
    {
        std::printf("%d document tests failed.\n", gFailures);
        return 1;
    }
    std::printf("All document tests passed.\n");
    return 0;
}

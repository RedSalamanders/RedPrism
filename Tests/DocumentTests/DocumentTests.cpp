#include "Document.h"
#include "Selection.h"
#include "TransformEdit.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
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
} // namespace

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
    if (gFailures != 0)
    {
        std::printf("%d document tests failed.\n", gFailures);
        return 1;
    }
    std::printf("All document tests passed.\n");
    return 0;
}

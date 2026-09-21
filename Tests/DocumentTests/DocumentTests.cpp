#include "Document.h"
#include "TransformEdit.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
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
    DocumentSelection empty;
    empty.empty = true;
    document.selection = empty;
    Expect(document.selection.value().IsEmpty(), "selection.empty-outline");
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
    session.Document().value().selection = DocumentSelection{true, false, {Vec2{10, 10}, Vec2{50, 10}, Vec2{50, 40}}};
    session.FlipCanvas(true);
    Expect(session.Document().value().selection->outline[0] == Vec2{390, 10}, "flip.canvas-selection");
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
    if (gFailures != 0)
    {
        std::printf("%d document tests failed.\n", gFailures);
        return 1;
    }
    std::printf("All document tests passed.\n");
    return 0;
}

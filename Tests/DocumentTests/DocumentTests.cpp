#include "Document.h"

#include <cstdio>
#include <initializer_list>
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
    if (gFailures != 0)
    {
        std::printf("%d document tests failed.\n", gFailures);
        return 1;
    }
    std::printf("All document tests passed.\n");
    return 0;
}

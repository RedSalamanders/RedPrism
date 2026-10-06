#include "../Support/DocumentFixture.h"
#include "../Support/FailureReports.h"
#include "Adjust.h"
#include "AdjustmentClosureTests.h"
#include "AdjustmentNestedTests.h"
#include "AdjustmentOpeningTests.h"
#include "BrushStroke.h"
#include "CanvasSize.h"
#include "ClipboardCommandTests.h"
#include "ClipboardPasteTests.h"
#include "CollapseRevisionTests.h"
#include "ColorDismissalTests.h"
#include "ColorDisplayPublicationTests.h"
#include "ColorDisplayWorkTests.h"
#include "ColorEditDeferredTests.h"
#include "ColorEditWorkTests.h"
#include "CopyPixelsTests.h"
#include "CopyPixelsTransformTests.h"
#include "Crop.h"
#include "DistortWorkTests.h"
#include "Document.Tests.CapturedCloneOffset.h"
#include "Document.h"
#include "DocumentDraftTests.h"
#include "DocumentFixtureTests.h"
#include "DocumentLifecycleTests.h"
#include "DocumentSizeHistoryTests.h"
#include "DocumentSizingWorkTests.h"
#include "DuplicateOpeningLimitTests.h"
#include "DuplicateOpeningWorkTests.h"
#include "ExportMemoryTests.h"
#include "FilterCancellationTests.h"
#include "FloatingMergeWorkTests.h"
#include "FloatingOpeningWorkTests.h"
#include "FloatingPixelsWorkTests.h"
#include "GradientResourceTests.h"
#include "GradientStateTests.h"
#include "GradientWorkTests.h"
#include "HistoryAccountingTests.h"
#include "HistoryEntriesTests.h"
#include "HistoryPreparationFaultTests.h"
#include "HistoryRestoreTests.h"
#include "HistoryRetirementBoundTests.h"
#include "HistoryRevisionTests.h"
#include "HistoryStateViewTests.h"
#include "HistoryWorkTests.h"
#include "ImageFilters.h"
#include "ImportHistoryTests.h"
#include "ImportPublicationTests.h"
#include "LayerCopyWorkTests.h"
#include "LayerMetadataWorkTests.h"
#include "LayerOpacityWorkTests.h"
#include "LayerReadViewTests.h"
#include "LayerRowsWorkTests.h"
#include "LiftReferenceTests.h"
#include "MaskEditWorkExtraTests.h"
#include "MaskEditWorkTests.h"
#include "MergeHistoryTests.h"
#include "MergePlanProbeTests.h"
#include "OptionalImageThumbnailTests.h"
#include "PixelMoveOpeningTests.h"
#include "PixelMoveOpeningWorkTests.h"
#include "PixelMoveStateTests.h"
#include "PixelMoveWorkTests.h"
#include "PreparedColorHistoryTests.h"
#include "PreparedHistoryFollowingTests.h"
#include "PreparedHistoryTests.h"
#include "PreparedRasterHistoryTests.h"
#include "PreservedLayerMetadataTests.h"
#include "PreservedLayerOpacityTests.h"
#include "ProjectSaveMemoryTests.h"
#include "RasterConcurrencyTests.h"
#include "RasterEditWorkTests.h"
#include "RasterResidencyManifestTests.h"
#include "RasterResidencyPlanTests.h"
#include "RasterUploadLayoutTests.h"
#include "RasterUploadReductionWorkTests.h"
#include "RasterUploadWorkTests.h"
#include "RasterWork.h"
#include "RasterWorkTests.h"
#include "RecordOwnershipBoundsTests.h"
#include "RecordRootAdoptTests.h"
#include "RecordRootBench.h"
#include "RecordRootTests.h"
#include "Selection.h"
#include "SelectionCommandTests.h"
#include "SelectionMoveClosureTests.h"
#include "SelectionMoveOpeningTests.h"
#include "SelectionNudgeTests.h"
#include "SelectionPreserveTests.h"
#include "SelectionStorageTests.h"
#include "SessionGenerationTests.h"
#include "SettingsStore.h"
#include "ShapeEditWorkTests.h"
#include "TransformCancellationWorkTests.h"
#include "TransformCommitWorkTests.h"
#include "TransformDeferredGuardTests.h"
#include "TransformDeferredTests.h"
#include "TransformEdit.h"
#include "TransformPixelsWorkTests.h"
#include "TransformPreviewWorkTests.h"
#include "WandCancellationTests.h"
#include "WandChunkBoundaryTests.h"
#include "WandCompositeSelectionTests.h"
#include "WandLayerSelectionTests.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <crtdbg.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", name);
    std::fflush(stdout);
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

[[nodiscard]] const ImageLayer* Find(const CanvasDocument& document, const Guid& id) noexcept
{
    for (const ImageLayer& layer : document.layers)
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
    DocumentFixture document;
    document.width = 20;
    document.height = 10;
    ImageLayer folder = ImageLayer::Blank(L"Folder", Vec2{20, 10});
    folder.isGroup = true;
    folder.opacity = 0.5;
    document.layers.push_back(folder);
    Expect(ValidateDocument(document.Seal()) == DocumentStatus::Invalid, "validate.group-opacity");

    folder.opacity = 1;
    document.layers[0] = folder;
    Expect(ValidateDocument(document.Seal(), 1) == DocumentStatus::Invalid, "validate.v1-group");
    Expect(ValidateDocument(document.Seal(), 7) == DocumentStatus::Ok, "validate.v7-group");

    DocumentFixture masked;
    masked.width = 8;
    masked.height = 8;
    ImageLayer layer = RasterLayer(8, 8, L"Paint");
    LayerMask mask;
    mask.asset.image = Raster::Make(8, 8, RasterFormat::Gray8);
    mask.asset.name = L"Layer Mask";
    layer.mask = mask;
    masked.layers.push_back(layer);
    Expect(ValidateDocument(masked.Seal(), 3) == DocumentStatus::Invalid, "validate.v4-mask-too-old");
    Expect(ValidateDocument(masked.Seal(), 4) == DocumentStatus::Ok, "validate.v4-mask");

    DocumentFixture folderMask;
    folderMask.width = 8;
    folderMask.height = 8;
    ImageLayer group = ImageLayer::Blank(L"Folder", Vec2{8, 8});
    group.isGroup = true;
    LayerMask groupMask;
    groupMask.asset.image = Raster::Make(8, 8, RasterFormat::Gray8);
    group.mask = groupMask;
    folderMask.layers.push_back(group);
    Expect(ValidateDocument(folderMask.Seal(), 5) == DocumentStatus::Invalid, "validate.v6-folder-mask-too-old");
    Expect(ValidateDocument(folderMask.Seal(), 6) == DocumentStatus::Ok, "validate.v6-folder-mask");

    CanvasDocument huge;
    huge.width = 30'001;
    huge.height = 1;
    Expect(ValidateDocument(huge) == DocumentStatus::TooLarge, "validate.dimension");

    DocumentFixture adjustmentDoc;
    adjustmentDoc.width = 4;
    adjustmentDoc.height = 4;
    ImageLayer adjustment = ImageLayer::Blank(L"Levels", Vec2{4, 4});
    adjustment.isGroup = true;
    LayerAdjustment levels;
    levels.kind = AdjustmentKind::Levels;
    adjustment.adjustment = levels;
    adjustmentDoc.layers.push_back(adjustment);
    Expect(ValidateDocument(adjustmentDoc.Seal(), 7) == DocumentStatus::Invalid, "validate.adjustment-on-group");

    DocumentFixture color;
    color.width = 4;
    color.height = 4;
    ImageLayer paint = RasterLayer(4, 4, L"Paint");
    paint.blendMode = LayerBlendMode::Multiply;
    color.layers.push_back(paint);
    Expect(ValidateDocument(color.Seal(), 2) == DocumentStatus::Invalid, "validate.v2-blend");
    Expect(ValidateDocument(color.Seal(), 3) == DocumentStatus::Ok, "validate.v3-blend");

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
    session.SetGroupCollapsed(outer, true);
    const std::vector<HierarchyEntry> collapsed = session.LayerRows();
    Expect(collapsed.size() == 1 && collapsed[0].id == outer, "session.collapse-hides-children");
    session.SetGroupCollapsed(outer, false);
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

    DocumentSession order;
    order.CreateDocument(32, 32, true);
    const Guid lower = order.ActiveLayerID().value();
    order.AddBlankLayer();
    const Guid upper = order.ActiveLayerID().value();
    Expect(order.PlaceLayer(upper, std::nullopt, lower, false, true), "session.place-before");
    Expect(order.Document().has_value() && order.Document()->layers.front().id == upper, "session.place-before-order");
    order.Undo();
    Expect(order.Document().has_value() && order.Document()->layers.back().id == upper, "session.place-before-undo");
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
    Expect(DocumentSession{}.history.RetainedByteLimit() == kHistoryRetainedByteLimit,
           "history.session-default-256mib");
    constexpr std::uint64_t gib = 1024ull * 1024 * 1024;
    Expect(HistoryLimitForPhysicalMemory(0) == 256ull * 1024 * 1024, "history.ram-query-fallback");
    Expect(HistoryLimitForPhysicalMemory(4 * gib) == gib, "history.ram-quarter");
    Expect(HistoryLimitForPhysicalMemory(8 * gib) == 2 * gib && HistoryLimitForPhysicalMemory(64 * gib) == 2 * gib &&
               HistoryLimitForPhysicalMemory(UINT64_MAX) == 2 * gib,
           "history.ram-cap-no-overflow");
    Expect(HistoryLimitForPhysicalMemory(7) == 1, "history.ram-quarter-rounds-down");
    Expect(DocumentHistory(100, 2 * gib).RetainedByteLimit() == 2 * gib &&
               DocumentSession(2 * gib).history.RetainedByteLimit() == 2 * gib,
           "history.uint64-session-limit");
    Expect(DocumentHistory(-1, UINT64_MAX).EntryLimit() == 0 &&
               DocumentHistory(-1, UINT64_MAX).RetainedByteLimit() == UINT64_MAX,
           "history.uint64-limit-preserved-entry-clamped");

    DocumentHistory limited(2, 0);
    DocumentFixture doc;
    doc.width = 64;
    doc.height = 32;
    ImageLayer layer = RasterLayer(8, 8, L"Shared");
    doc.layers.push_back(layer);
    const Guid id = layer.id;
    for (const wchar_t* name : {L"A", L"B", L"C"})
    {
        limited.Begin(L"Rename", doc.Seal(), id);
        doc.layers[0].name = name;
        limited.End(doc.Seal(), id);
    }
    Expect(limited.UndoCount() == 2, "history.entry-limit");
    Expect(limited.RetainedBytes(doc.Seal()) == 0, "history.shared-raster-zero-retained");
    limited.Begin(L"Delete", doc.Seal(), id);
    doc.layers.clear();
    limited.End(doc.Seal(), std::nullopt);
    Expect(limited.UndoCount() == 0, "history.zero-budget-drops-unique");
    Expect(limited.RetainedBytes(doc.Seal()) == 0, "history.zero-budget-bytes");
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
        EditDocumentFixture(document.value(), [&](DocumentFixture& fixture) { fixture.layers = {std::move(layer)}; });
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

void TestSelectionLoopsBounds()
{
    SelectionLoops empty;
    Expect(!empty.Bounds().has_value() && empty.PointCount() == 0, "selection-loops-bounds.empty");
    DocumentSelection emptySelection{true, empty};
    Expect(!emptySelection.Bounds().has_value() && emptySelection.IsEmpty(), "selection-loops-bounds.empty-selection");

    SelectionLoops point{{Vec2{3, 4}}};
    Expect(point.Bounds() == SelectionBounds{3, 4, 3, 4} && DocumentSelection{true, point}.IsEmpty(),
           "selection-loops-bounds.point-degenerate");
    SelectionLoops vertical{{Vec2{2, 1}, Vec2{2, 5}}};
    SelectionLoops horizontal{{Vec2{1, 7}, Vec2{5, 7}}};
    Expect(vertical.Bounds() == SelectionBounds{2, 1, 2, 5} && DocumentSelection{true, vertical}.IsEmpty() &&
               horizontal.Bounds() == SelectionBounds{1, 7, 5, 7} && DocumentSelection{true, horizontal}.IsEmpty(),
           "selection-loops-bounds.zero-width-height");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    SelectionLoops mixed{{Vec2{nan, 1}, Vec2{1, infinity}, Vec2{-2, 3}, Vec2{4, -5}, Vec2{-infinity, nan}}};
    SelectionLoops nonFinite{{Vec2{nan, 1}, Vec2{1, infinity}, Vec2{-infinity, nan}}};
    Expect(mixed.Bounds() == SelectionBounds{-2, -5, 4, 3} && !DocumentSelection{true, mixed}.IsEmpty(),
           "selection-loops-bounds.skips-nonfinite-points");
    Expect(!nonFinite.Bounds().has_value() && DocumentSelection{true, nonFinite}.IsEmpty(),
           "selection-loops-bounds.no-finite-points");

    SelectionLoops original{{Vec2{0, 1}, Vec2{6, 1}, Vec2{6, 8}, Vec2{0, 8}}};
    const SelectionLoops shared = original;
    Expect(shared.Identity() == original.Identity() && shared.Bounds() == SelectionBounds{0, 1, 6, 8} &&
               shared == original,
           "selection-loops-bounds.shared-copy");
    const std::vector<SelectionLoop>& converted = shared;
    Expect(&converted == &shared.Get() && converted.size() == 1, "selection-loops-bounds.vector-conversion");

    SelectionLoops copyAssigned;
    copyAssigned = original;
    Expect(copyAssigned.Identity() == original.Identity() && copyAssigned.Bounds() == original.Bounds(),
           "selection-loops-bounds.copy-assignment");
    SelectionLoops moved(std::move(original));
    Expect(moved.Bounds() == SelectionBounds{0, 1, 6, 8} && moved.PointCount() == 4 && !original.Bounds().has_value() &&
               original.PointCount() == 0 && original.empty(),
           "selection-loops-bounds.move-construction-source-empty");

    SelectionLoops moveAssigned{{Vec2{-1, -1}, Vec2{1, 1}}};
    moveAssigned = std::move(moved);
    Expect(moveAssigned.Bounds() == SelectionBounds{0, 1, 6, 8} && moveAssigned.PointCount() == 4 &&
               !moved.Bounds().has_value() && moved.PointCount() == 0 && moved.empty(),
           "selection-loops-bounds.move-assignment-source-empty");

    SelectionLoops equalGeometry{{Vec2{0, 1}, Vec2{6, 1}, Vec2{6, 8}, Vec2{0, 8}}};
    Expect(moveAssigned == equalGeometry && moveAssigned.Identity() != equalGeometry.Identity(),
           "selection-loops-bounds.geometry-equality-unchanged");
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
    session.SetGroupCollapsed(folder, true);
    rows = session.LayerRows();
    Expect(rows.size() == 1 && rows[0].collapsed && rows[0].isGroup, "rows.collapsed-flag");
    Expect(!HierarchyEntries(session.Document().value().layers)[0].collapsed, "rows.default-not-collapsed");
    // RF-907: a row carries its layer's mask for the layers panel's badge, and whether the mask is on.
    rows = session.LayerRows();
    const bool bare = rows.size() == 1 && !rows[0].hasMask && rows[0].maskEnabled;
    session.ClearCollapsedGroups();
    session.SelectLayer(session.LayerRows()[1].id);
    session.AddLayerMask(true);
    session.ToggleLayerMask(session.ActiveLayerID().value());
    rows = session.LayerRows();
    Expect(bare && rows.size() == 2 && !rows[0].hasMask && rows[1].hasMask && !rows[1].maskEnabled, "rows.mask-state");
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
    const auto& layers = session.Document().value().layers;
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
    const auto& after = session.Document().value().layers;
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
    const auto before = three.Document().value().layers;
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
    Expect(session.brushSettings.red == 0x33 / 255.0 && session.brushSettings.green == 0x66 / 255.0 &&
               session.brushSettings.blue == 0x99 / 255.0,
           "palette.foreground-is-brush-color");
    session.SwapPaletteColors();
    Expect(session.ForegroundColor() == 0xFFFFFFFFu && session.BackgroundColor() == 0xFF336699u, "palette.swap");
    Expect(session.brushSettings.red == 1.0 && session.brushSettings.green == 1.0 && session.brushSettings.blue == 1.0,
           "palette.swap-paints-white");
    session.CreateDocument(8, 8, true);
    const bool couldUndo = session.history.CanUndo();
    const bool modified = session.history.IsModified();
    session.ResetPaletteColors();
    Expect(session.ForegroundColor() == DocumentSession::kDefaultForegroundArgb &&
               session.BackgroundColor() == DocumentSession::kDefaultBackgroundArgb &&
               session.brushSettings.red == 0.0 && session.brushSettings.green == 0.0 &&
               session.brushSettings.blue == 0.0,
           "palette.reset");
    Expect(session.history.CanUndo() == couldUndo && session.history.IsModified() == modified,
           "palette.outside-history");
}

void TestPaletteSampleAuthority()
{
    DocumentSession session;
    session.CreateDocument(2, 2, true);
    const Guid revision = session.history.CurrentRevision();
    const auto generation = session.Generation();
    const auto undoCount = session.history.UndoCount();
    auto authority = session.CapturePaletteSampleAuthority();
    const auto first = authority;
    Expect(session.ApplySampledForegroundColor(authority, 0x003366CCu) && session.ForegroundColor() == 0xFF3366CCu &&
               authority != first,
           "palette-authority.current-sample-publishes-opaque-and-renews-own-stamp");
    Expect(session.ApplySampledForegroundColor(authority, 0xFF3366CCu) && authority != first,
           "palette-authority.repeated-own-sample-remains-authorized");
    auto stale = first;
    Expect(!session.ApplySampledForegroundColor(stale, 0xFFFFFFFFu) && session.ForegroundColor() == 0xFF3366CCu &&
               stale == first,
           "palette-authority.old-sample-refuses-without-changing-color-or-authority");
    session.SetForegroundColor(0xFF3366CCu);
    Expect(!session.ApplySampledForegroundColor(authority, 0xFFFFFFFFu),
           "palette-authority.explicit-equal-color-invalidates-older-sample");
    authority = session.CapturePaletteSampleAuthority();
    session.SetForegroundColor(0xFF112233u);
    session.SetForegroundColor(0xFF3366CCu);
    Expect(!session.ApplySampledForegroundColor(authority, 0xFFFFFFFFu) && session.ForegroundColor() == 0xFF3366CCu,
           "palette-authority.manual-a-b-a-refuses-old-result");
    authority = session.CapturePaletteSampleAuthority();
    session.SetBackgroundColor(session.BackgroundColor());
    Expect(!session.ApplySampledForegroundColor(authority, 0xFFFFFFFFu),
           "palette-authority.explicit-background-input-invalidates-old-result");
    authority = session.CapturePaletteSampleAuthority();
    session.SwapPaletteColors();
    session.SwapPaletteColors();
    Expect(!session.ApplySampledForegroundColor(authority, 0xFFFFFFFFu) && session.ForegroundColor() == 0xFF3366CCu,
           "palette-authority.double-swap-cannot-rebind-old-result");
    session.ResetPaletteColors();
    authority = session.CapturePaletteSampleAuthority();
    session.ResetPaletteColors();
    Expect(!session.ApplySampledForegroundColor(authority, 0xFF3366CCu),
           "palette-authority.equal-reset-invalidates-old-result");
    authority = session.CapturePaletteSampleAuthority();
    DocumentSession copied = session;
    auto copiedAuthority = authority;
    Expect(!copied.ApplySampledForegroundColor(copiedAuthority, 0xFF3366CCu),
           "palette-authority.copied-session-refuses-original-owner-stamp");
    const auto beforeMove = copied.CapturePaletteSampleAuthority();
    DocumentSession moved = std::move(copied);
    auto movedAuthority = beforeMove;
    auto movedFromAuthority = beforeMove;
    Expect(!moved.ApplySampledForegroundColor(movedAuthority, 0xFF3366CCu) &&
               !copied.ApplySampledForegroundColor(movedFromAuthority, 0xFF3366CCu),
           "palette-authority.moved-session-refuses-original-owner-stamp");
    Expect(session.history.CurrentRevision() == revision && session.Generation() == generation &&
               session.history.UndoCount() == undoCount,
           "palette-authority.sampling-and-input-authority-remain-outside-document-history");
    InputRevision boundary((std::numeric_limits<std::uint64_t>::max)() - 1);
    const auto beforeMaximum = boundary.Current();
    boundary.Advance();
    Expect(!boundary.Matches(beforeMaximum) && boundary.Matches((std::numeric_limits<std::uint64_t>::max)()),
           "palette-authority.last-valid-input-refuses-preceding-stamp");
    boundary.Advance();
    boundary.Advance();
    Expect(boundary.Current() == 0 && !boundary.Matches(0) && !boundary.Matches(1) &&
               !boundary.Matches((std::numeric_limits<std::uint64_t>::max)()),
           "palette-authority.exhaustion-permanently-refuses-instead-of-wrapping");
}

void TestPaletteIntentAuthority()
{
    const auto sampledOwner = std::make_unique<DocumentSession>();
    auto& sampled = *sampledOwner;
    sampled.CreateDocument(2, 2, true);
    const Guid sampledRevision = sampled.history.CurrentRevision();
    const auto sampledGeneration = sampled.Generation();
    const int sampledUndoCount = sampled.history.UndoCount();
    const auto manualAuthority = sampled.CapturePaletteIntentAuthority();
    const auto originalPublication = sampled.CapturePaletteSampleAuthority();
    auto publication = originalPublication;
    Expect(sampled.ApplySampledForegroundColor(publication, 0x00123456u) && publication != originalPublication &&
               sampled.IsPaletteIntentCurrent(manualAuthority) && sampled.ForegroundColor() == 0xFF123456u &&
               sampled.brushSettings.red == 0x12 / 255.0 && sampled.brushSettings.green == 0x34 / 255.0 &&
               sampled.brushSettings.blue == 0x56 / 255.0,
           "palette-intent.own-foreground-sample-renews-publication-and-updates-brush-rgb");
    const auto firstPublication = publication;
    Expect(sampled.ApplySampledBackgroundColor(publication, 0x00654321u) && publication != firstPublication &&
               sampled.IsPaletteIntentCurrent(manualAuthority) && sampled.ForegroundColor() == 0xFF123456u &&
               sampled.BackgroundColor() == 0xFF654321u && sampled.brushSettings.red == 0x12 / 255.0 &&
               sampled.brushSettings.green == 0x34 / 255.0 && sampled.brushSettings.blue == 0x56 / 255.0,
           "palette-intent.own-background-sample-renews-same-publication-authority");
    const auto secondPublication = publication;
    Expect(sampled.ApplySampledForegroundColor(publication, 0x00A1B2C3u) && publication != secondPublication &&
               sampled.IsPaletteIntentCurrent(manualAuthority) && sampled.ForegroundColor() == 0xFFA1B2C3u &&
               sampled.brushSettings.red == 0xA1 / 255.0 && sampled.brushSettings.green == 0xB2 / 255.0 &&
               sampled.brushSettings.blue == 0xC3 / 255.0,
           "palette-intent.successive-own-samples-keep-original-manual-authority-current");

    auto stalePublication = originalPublication;
    const uint32_t foregroundBeforeRefusal = sampled.ForegroundColor();
    const uint32_t backgroundBeforeRefusal = sampled.BackgroundColor();
    const double brushRedBeforeRefusal = sampled.brushSettings.red;
    const double brushGreenBeforeRefusal = sampled.brushSettings.green;
    const double brushBlueBeforeRefusal = sampled.brushSettings.blue;
    Expect(!sampled.ApplySampledBackgroundColor(stalePublication, 0x00DEADBEu) &&
               stalePublication == originalPublication && sampled.ForegroundColor() == foregroundBeforeRefusal &&
               sampled.BackgroundColor() == backgroundBeforeRefusal &&
               sampled.brushSettings.red == brushRedBeforeRefusal &&
               sampled.brushSettings.green == brushGreenBeforeRefusal &&
               sampled.brushSettings.blue == brushBlueBeforeRefusal && sampled.IsPaletteIntentCurrent(manualAuthority),
           "palette-intent.refused-stale-sample-changes-no-color-authority-or-brush-state");
    Expect(sampled.history.CurrentRevision() == sampledRevision && sampled.Generation() == sampledGeneration &&
               sampled.history.UndoCount() == sampledUndoCount,
           "palette-intent.sampled-colors-remain-outside-document-generation-and-history");

    const auto foregroundEqualOwner = std::make_unique<DocumentSession>();
    auto& foregroundEqual = *foregroundEqualOwner;
    foregroundEqual.CreateDocument(2, 2, true);
    auto foregroundManual = foregroundEqual.CapturePaletteIntentAuthority();
    auto foregroundPublication = foregroundEqual.CapturePaletteSampleAuthority();
    const uint32_t equalForeground = foregroundEqual.ForegroundColor();
    foregroundEqual.SetForegroundColor(equalForeground);
    const auto foregroundPublicationAfterIntent = foregroundPublication;
    Expect(!foregroundEqual.IsPaletteIntentCurrent(foregroundManual) &&
               !foregroundEqual.ApplySampledForegroundColor(foregroundPublication, 0xFF102938u) &&
               foregroundPublication == foregroundPublicationAfterIntent &&
               foregroundEqual.ForegroundColor() == equalForeground && foregroundEqual.brushSettings.red == 0.0 &&
               foregroundEqual.brushSettings.green == 0.0 && foregroundEqual.brushSettings.blue == 0.0,
           "palette-intent.equal-explicit-foreground-invalidates-manual-and-publication-authority");

    const auto backgroundEqualOwner = std::make_unique<DocumentSession>();
    auto& backgroundEqual = *backgroundEqualOwner;
    backgroundEqual.CreateDocument(2, 2, true);
    auto backgroundManual = backgroundEqual.CapturePaletteIntentAuthority();
    auto backgroundPublication = backgroundEqual.CapturePaletteSampleAuthority();
    const uint32_t equalBackground = backgroundEqual.BackgroundColor();
    backgroundEqual.SetBackgroundColor(equalBackground);
    const auto backgroundPublicationAfterIntent = backgroundPublication;
    Expect(!backgroundEqual.IsPaletteIntentCurrent(backgroundManual) &&
               !backgroundEqual.ApplySampledBackgroundColor(backgroundPublication, 0xFF102938u) &&
               backgroundPublication == backgroundPublicationAfterIntent &&
               backgroundEqual.BackgroundColor() == equalBackground,
           "palette-intent.equal-explicit-background-invalidates-manual-and-publication-authority");

    const auto foregroundAbaOwner = std::make_unique<DocumentSession>();
    auto& foregroundAba = *foregroundAbaOwner;
    foregroundAba.CreateDocument(2, 2, true);
    auto foregroundAbaManual = foregroundAba.CapturePaletteIntentAuthority();
    auto foregroundAbaPublication = foregroundAba.CapturePaletteSampleAuthority();
    const uint32_t foregroundOriginal = foregroundAba.ForegroundColor();
    foregroundAba.SetForegroundColor(0xFF123456u);
    foregroundAba.SetForegroundColor(foregroundOriginal);
    const auto foregroundAbaPublicationBeforeRefusal = foregroundAbaPublication;
    Expect(!foregroundAba.IsPaletteIntentCurrent(foregroundAbaManual) &&
               !foregroundAba.ApplySampledForegroundColor(foregroundAbaPublication, 0xFF998877u) &&
               foregroundAbaPublication == foregroundAbaPublicationBeforeRefusal &&
               foregroundAba.ForegroundColor() == foregroundOriginal,
           "palette-intent.foreground-a-b-a-refuses-old-manual-and-publication-authority");

    const auto backgroundAbaOwner = std::make_unique<DocumentSession>();
    auto& backgroundAba = *backgroundAbaOwner;
    backgroundAba.CreateDocument(2, 2, true);
    auto backgroundAbaManual = backgroundAba.CapturePaletteIntentAuthority();
    auto backgroundAbaPublication = backgroundAba.CapturePaletteSampleAuthority();
    const uint32_t backgroundOriginal = backgroundAba.BackgroundColor();
    backgroundAba.SetBackgroundColor(0xFF123456u);
    backgroundAba.SetBackgroundColor(backgroundOriginal);
    const auto backgroundAbaPublicationBeforeRefusal = backgroundAbaPublication;
    Expect(!backgroundAba.IsPaletteIntentCurrent(backgroundAbaManual) &&
               !backgroundAba.ApplySampledBackgroundColor(backgroundAbaPublication, 0xFF998877u) &&
               backgroundAbaPublication == backgroundAbaPublicationBeforeRefusal &&
               backgroundAba.BackgroundColor() == backgroundOriginal,
           "palette-intent.background-a-b-a-refuses-old-manual-and-publication-authority");

    const auto paletteCommandsOwner = std::make_unique<DocumentSession>();
    auto& paletteCommands = *paletteCommandsOwner;
    paletteCommands.CreateDocument(2, 2, true);
    auto swapManual = paletteCommands.CapturePaletteIntentAuthority();
    paletteCommands.SwapPaletteColors();
    Expect(!paletteCommands.IsPaletteIntentCurrent(swapManual),
           "palette-intent.palette-swap-invalidates-manual-authority");
    auto resetManual = paletteCommands.CapturePaletteIntentAuthority();
    paletteCommands.ResetPaletteColors();
    Expect(!paletteCommands.IsPaletteIntentCurrent(resetManual),
           "palette-intent.palette-reset-invalidates-manual-authority");

    const auto maskPaletteOwner = std::make_unique<DocumentSession>();
    auto& maskPalette = *maskPaletteOwner;
    maskPalette.CreateDocument(2, 2, true);
    maskPalette.AddLayerMask(true);
    const uint32_t maskForeground = maskPalette.ForegroundColor();
    const uint32_t maskBackground = maskPalette.BackgroundColor();
    auto maskSwapManual = maskPalette.CapturePaletteIntentAuthority();
    maskPalette.SwapPaletteColors();
    Expect(!maskPalette.IsPaletteIntentCurrent(maskSwapManual) && maskPalette.maskPaintWhite &&
               maskPalette.ForegroundColor() == maskForeground && maskPalette.BackgroundColor() == maskBackground,
           "palette-intent.mask-target-swap-invalidates-authority-without-changing-colors");
    auto maskResetManual = maskPalette.CapturePaletteIntentAuthority();
    maskPalette.ResetPaletteColors();
    Expect(!maskPalette.IsPaletteIntentCurrent(maskResetManual) && !maskPalette.maskPaintWhite &&
               maskPalette.ForegroundColor() == maskForeground && maskPalette.BackgroundColor() == maskBackground,
           "palette-intent.mask-target-reset-invalidates-authority-without-changing-colors");

    const auto ownerOwner = std::make_unique<DocumentSession>();
    auto& owner = *ownerOwner;
    owner.CreateDocument(2, 2, true);
    const auto ownerManual = owner.CapturePaletteIntentAuthority();
    const auto foreignOwner = std::make_unique<DocumentSession>();
    auto& foreign = *foreignOwner;
    foreign.CreateDocument(2, 2, true);
    auto foreignManual = ownerManual;
    auto foreignPublication = owner.CapturePaletteSampleAuthority();
    const auto foreignPublicationBefore = foreignPublication;
    const uint32_t foreignBackground = foreign.BackgroundColor();
    Expect(!foreign.IsPaletteIntentCurrent(foreignManual) &&
               !foreign.ApplySampledBackgroundColor(foreignPublication, 0xFF112233u) &&
               foreignPublication == foreignPublicationBefore && foreign.BackgroundColor() == foreignBackground,
           "palette-intent.foreign-session-refuses-manual-and-publication-authority");
    const auto copiedOwner = std::make_unique<DocumentSession>(owner);
    auto& copied = *copiedOwner;
    Expect(!copied.IsPaletteIntentCurrent(ownerManual), "palette-intent.copied-session-refuses-manual-authority");
    const auto movedOwner = std::make_unique<DocumentSession>(std::move(owner));
    auto& moved = *movedOwner;
    Expect(!moved.IsPaletteIntentCurrent(ownerManual) && !owner.IsPaletteIntentCurrent(ownerManual),
           "palette-intent.moved-session-and-source-refuse-original-manual-authority");
}

void TestDocumentReadAuthority()
{
    auto empty = std::make_unique<DocumentSession>();
    Expect(!empty->CaptureDocumentReadAuthority(), "document-read.no-document-has-no-authority");

    auto owner = std::make_unique<DocumentSession>();
    owner->CreateDocument(3, 2, true);
    const auto captured = owner->CaptureDocumentReadAuthority();
    if (!captured.has_value())
    {
        Expect(false, "document-read.created-document-captures-authority");
        return;
    }
    const DocumentReadAuthority authority = captured.value();
    const Guid historyRevision = owner->history.CurrentRevision();
    const int undoCount = owner->history.UndoCount();
    const std::uint64_t generation = owner->Generation();
    const std::uint64_t documentSerial = owner->DocumentSerial();
    Expect(owner->IsDocumentReadCurrent(authority) && authority.canvasWidth == 3 && authority.canvasHeight == 2 &&
               authority.generation == generation && authority.documentSerial == documentSerial &&
               owner->history.CurrentRevision() == historyRevision && owner->history.UndoCount() == undoCount,
           "document-read.capture-check-keeps-history-state");

    DocumentReadAuthority wrongOwner = authority;
    wrongOwner.sessionIdentity = Guid::Generate();
    DocumentReadAuthority wrongSerial = authority;
    ++wrongSerial.documentSerial;
    DocumentReadAuthority wrongGeneration = authority;
    ++wrongGeneration.generation;
    DocumentReadAuthority wrongRestore = authority;
    ++wrongRestore.restoreSerial;
    DocumentReadAuthority wrongWidth = authority;
    ++wrongWidth.canvasWidth;
    DocumentReadAuthority wrongHeight = authority;
    ++wrongHeight.canvasHeight;
    Expect(!owner->IsDocumentReadCurrent(wrongOwner), "document-read.foreign-owner-refused");
    Expect(!owner->IsDocumentReadCurrent(wrongSerial), "document-read.mismatched-document-serial-refused");
    Expect(!owner->IsDocumentReadCurrent(wrongGeneration), "document-read.mismatched-generation-refused");
    Expect(!owner->IsDocumentReadCurrent(wrongRestore), "document-read.mismatched-restore-serial-refused");
    Expect(!owner->IsDocumentReadCurrent(wrongWidth), "document-read.mismatched-width-refused");
    Expect(!owner->IsDocumentReadCurrent(wrongHeight), "document-read.mismatched-height-refused");
    Expect(owner->IsDocumentReadCurrent(authority) && owner->Generation() == generation &&
               owner->DocumentSerial() == documentSerial && owner->history.CurrentRevision() == historyRevision &&
               owner->history.UndoCount() == undoCount,
           "document-read.mismatch-refusals-are-atomic");

    auto foreign = std::make_unique<DocumentSession>();
    foreign->CreateDocument(3, 2, true);
    auto copied = std::make_unique<DocumentSession>(*owner);
    Expect(!foreign->IsDocumentReadCurrent(authority) && !copied->IsDocumentReadCurrent(authority),
           "document-read.foreign-and-copied-session-refuse-owner-authority");
    auto moved = std::make_unique<DocumentSession>(std::move(*owner));
    Expect(!moved->IsDocumentReadCurrent(authority) && !owner->IsDocumentReadCurrent(authority),
           "document-read.moved-session-and-source-refuse-owner-authority");

    auto changed = std::make_unique<DocumentSession>();
    changed->CreateDocument(3, 2, true);
    const auto beforeMutation = changed->CaptureDocumentReadAuthority().value();
    changed->AddBlankLayer();
    const auto afterMutation = changed->CaptureDocumentReadAuthority().value();
    Expect(!changed->IsDocumentReadCurrent(beforeMutation) && changed->IsDocumentReadCurrent(afterMutation),
           "document-read.real-document-mutation-invalidates-old-authority");
    changed->Undo();
    const auto afterUndo = changed->CaptureDocumentReadAuthority().value();
    Expect(!changed->IsDocumentReadCurrent(afterMutation) && changed->IsDocumentReadCurrent(afterUndo),
           "document-read.undo-restore-invalidates-prior-authority");
    changed->Redo();
    const auto afterRedo = changed->CaptureDocumentReadAuthority().value();
    Expect(!changed->IsDocumentReadCurrent(afterUndo) && changed->IsDocumentReadCurrent(afterRedo),
           "document-read.redo-restore-invalidates-prior-authority");

    auto replaced = std::make_unique<DocumentSession>();
    replaced->CreateDocument(3, 2, true);
    const auto beforeReplace = replaced->CaptureDocumentReadAuthority().value();
    const Guid originalDocumentId = replaced->Document().value().id;
    CanvasDocument sameIdReplacement = replaced->Document().value();
    replaced->ReplaceDocument(std::move(sameIdReplacement), std::nullopt);
    Expect(!replaced->IsDocumentReadCurrent(beforeReplace) && replaced->Document().value().id == originalDocumentId,
           "document-read.replace-same-document-id-invalidates-authority");

    auto reopened = std::make_unique<DocumentSession>();
    reopened->CreateDocument(3, 2, true);
    const auto beforeReopen = reopened->CaptureDocumentReadAuthority().value();
    const Guid closedDocumentId = reopened->Document().value().id;
    reopened->ClearDocument();
    reopened->CreateDocument(3, 2, true);
    Expect(!reopened->IsDocumentReadCurrent(beforeReopen) && reopened->Document().value().id != closedDocumentId,
           "document-read.close-and-reopen-equal-size-invalidates-authority");

    auto previews = std::make_unique<DocumentSession>();
    previews->CreateDocument(2, 2, true);
    const std::array<std::uint8_t, 16> previewPixels{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 32, 64, 128, 255};
    const Raster previewSource = Raster::Make(2, 2, RasterFormat::Bgra8888, previewPixels.data(), previewPixels.size());
    if (!previewSource.HasStorage())
    {
        Expect(false, "document-read.preview-fixture-raster-created");
        return;
    }
    EditLayerRecordsForTest(
        *previews, [&previewSource](DocumentFixture& fixture)
        { fixture.layers.front().asset = RasterAsset{previewSource, {}, L"Read authority preview"}; });
    const auto beforePaletteAndPreview = previews->CaptureDocumentReadAuthority().value();
    const Guid previewHistoryRevision = previews->history.CurrentRevision();
    const std::uint64_t previewGeneration = previews->Generation();
    const int previewUndoCount = previews->history.UndoCount();
    previews->SetForegroundColor(0xFF2468ACu);
    const bool paletteKeptReadCurrent = previews->IsDocumentReadCurrent(beforePaletteAndPreview);
    previews->BeginColorEdit(FilterKind::GaussianBlur);
    FilterSettings blur;
    blur.radius = 1.75;
    previews->UpdateColorEdit(blur, true);
    const bool destructivePreviewLive =
        previews->PendingColorEdit().has_value() && previews->PendingColorEdit().value().preview;
    const bool previewKeptReadCurrent = previews->IsDocumentReadCurrent(beforePaletteAndPreview);
    previews->CancelColorEdit();
    Expect(paletteKeptReadCurrent && destructivePreviewLive && previewKeptReadCurrent &&
               previews->IsDocumentReadCurrent(beforePaletteAndPreview) &&
               previews->history.CurrentRevision() == previewHistoryRevision &&
               previews->Generation() == previewGeneration && previews->history.UndoCount() == previewUndoCount,
           "document-read.palette-input-and-live-destructive-preview-do-not-require-history-restore-guard");
}

void TestCapturedToolInputs()
{
    constexpr GradientEdit::CapturedInputs imageInputs{
        GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToBackground, true, 0.375}, 0xFF2468ACu,
        0xFFCF7135u, false};
    auto image = std::make_unique<DocumentSession>();
    image->CreateDocument(3, 1, false);
    image->AddBlankLayer();
    image->SetForegroundColor(0xFF112233u);
    image->SetBackgroundColor(0xFF445566u);
    image->gradientSettings =
        GradientSettings{GradientShape::Linear, GradientStyle::ForegroundToTransparent, false, 1.0};
    Expect(image->BeginDeferredGradient(Vec2{0, 0}, imageInputs), "captured-tool.image-deferred-opening");
    image->MoveGradient(std::nullopt, Vec2{2, 0});
    const GradientEdit* const imagePending = image->PendingGradient();
    const auto imageColors = image->GradientColors();
    Expect(imagePending != nullptr && imagePending->capturedInputs == imageInputs &&
               imagePending->start == Vec2{0, 0} && imagePending->end == Vec2{2, 0} &&
               imageColors[0] == std::array<double, 4>{207.0 / 255.0, 113.0 / 255.0, 53.0 / 255.0, 1.0} &&
               imageColors[1] == std::array<double, 4>{36.0 / 255.0, 104.0 / 255.0, 172.0 / 255.0, 1.0},
           "captured-tool.image-fg-bg-reverse-settings-and-move-retain-event-inputs");

    constexpr GradientEdit::CapturedInputs transparentInputs{
        GradientSettings{GradientShape::Linear, GradientStyle::ForegroundToTransparent, true, 0.625}, 0xFF5A2DC1u,
        0xFF19A477u, false};
    auto transparent = std::make_unique<DocumentSession>();
    transparent->CreateDocument(2, 1, false);
    transparent->AddBlankLayer();
    Expect(transparent->BeginDeferredGradient(Vec2{0, 0}, transparentInputs),
           "captured-tool.transparent-deferred-opening");
    transparent->MoveGradient(std::nullopt, Vec2{1, 0});
    const auto transparentColors = transparent->GradientColors();
    Expect(transparent->PendingGradient()->capturedInputs == transparentInputs &&
               transparentColors[0] == std::array<double, 4>{90.0 / 255.0, 45.0 / 255.0, 193.0 / 255.0, 0.0} &&
               transparentColors[1] == std::array<double, 4>{90.0 / 255.0, 45.0 / 255.0, 193.0 / 255.0, 1.0},
           "captured-tool.reverse-transparent-endpoints-use-event-foreground");

    auto mask = std::make_unique<DocumentSession>();
    mask->CreateDocument(2, 1, false);
    mask->AddBlankLayer();
    mask->AddLayerMask(true);
    const auto maskLayer = mask->ActiveLayerID();
    if (maskLayer.has_value())
        mask->SelectLayerTarget(maskLayer.value(), true);
    mask->maskPaintWhite = false;
    mask->SetForegroundColor(0xFF778899u);
    mask->SetBackgroundColor(0xFFAABBCCu);
    mask->gradientSettings =
        GradientSettings{GradientShape::Linear, GradientStyle::ForegroundToTransparent, false, 1.0};
    constexpr GradientEdit::CapturedInputs maskInputs{
        GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToBackground, true, 0.25}, 0xFF010203u,
        0xFF040506u, true};
    const bool maskOpened = mask->IsMaskSelected() && mask->BeginDeferredGradient(Vec2{0, 0}, maskInputs);
    mask->MoveGradient(std::nullopt, Vec2{1, 0});
    const auto maskColors = mask->GradientColors();
    Expect(maskOpened && mask->PendingGradient()->capturedInputs == maskInputs &&
               maskColors[0] == std::array<double, 4>{0.0, 0.0, 0.0, 1.0} &&
               maskColors[1] == std::array<double, 4>{1.0, 1.0, 1.0, 1.0},
           "captured-tool.mask-event-inputs-override-pre-opening-live-palette-and-settings");

    constexpr GradientEdit::CapturedInputs initialLiveProbe{
        GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToTransparent, true, 0.2}, 0xFF010101u,
        0xFF020202u, false};
    auto live = std::make_unique<DocumentSession>();
    live->CreateDocument(2, 1, false);
    live->AddBlankLayer();
    live->gradientSettings = GradientSettings{GradientShape::Linear, GradientStyle::ForegroundToBackground, false, 1.0};
    Expect(live->BeginDeferredGradient(Vec2{0, 0}, initialLiveProbe), "captured-tool.live-refresh-opening");
    live->SetForegroundColor(0xFF102030u);
    Expect(live->PendingGradient()->capturedInputs == std::nullopt &&
               live->GradientColors()[0] == std::array<double, 4>{16.0 / 255.0, 32.0 / 255.0, 48.0 / 255.0, 1.0},
           "captured-tool.foreground-input-adopts-live-value");
    live->SetBackgroundColor(0xFF405060u);
    Expect(live->PendingGradient()->capturedInputs == std::nullopt &&
               live->GradientColors()[1] == std::array<double, 4>{64.0 / 255.0, 80.0 / 255.0, 96.0 / 255.0, 1.0},
           "captured-tool.background-input-adopts-live-value");
    live->SwapPaletteColors();
    Expect(live->PendingGradient()->capturedInputs == std::nullopt &&
               live->GradientColors()[0] == std::array<double, 4>{64.0 / 255.0, 80.0 / 255.0, 96.0 / 255.0, 1.0} &&
               live->GradientColors()[1] == std::array<double, 4>{16.0 / 255.0, 32.0 / 255.0, 48.0 / 255.0, 1.0},
           "captured-tool.palette-swap-adopts-live-values");
    live->ResetPaletteColors();
    Expect(live->PendingGradient()->capturedInputs == std::nullopt &&
               live->GradientColors()[0] == std::array<double, 4>{0.0, 0.0, 0.0, 1.0} &&
               live->GradientColors()[1] == std::array<double, 4>{1.0, 1.0, 1.0, 1.0},
           "captured-tool.palette-reset-adopts-live-values");
    live->SetForegroundColor(0xFF2468ACu);
    live->SetBackgroundColor(0xFFCF7135u);
    live->gradientSettings =
        GradientSettings{GradientShape::Radial, GradientStyle::ForegroundToTransparent, true, 0.75};
    const bool refreshedOpening = live->BeginDeferredGradient(Vec2{0, 0}, initialLiveProbe);
    live->RefreshGradient();
    Expect(refreshedOpening && live->PendingGradient() != nullptr &&
               live->PendingGradient()->capturedInputs == std::nullopt &&
               live->GradientColors()[0] == std::array<double, 4>{36.0 / 255.0, 104.0 / 255.0, 172.0 / 255.0, 0.0} &&
               live->GradientColors()[1] == std::array<double, 4>{36.0 / 255.0, 104.0 / 255.0, 172.0 / 255.0, 1.0},
           "captured-tool.header-refresh-adopts-live-settings-and-palette");

    auto click = std::make_unique<DocumentSession>();
    click->CreateDocument(1, 1, false);
    click->AddBlankLayer();
    Expect(click->BeginDeferredGradient(Vec2{0, 0}, imageInputs), "captured-tool.click-opening");
    click->EndGradientDrag();
    Expect(click->PendingGradient() == nullptr, "captured-tool.click-without-line-drops-metadata");

    auto shapes = std::make_unique<DocumentSession>();
    shapes->CreateDocument(8, 6, false);
    const ShapeKind originalKind = shapes->shapeKind;
    const double originalRadius = shapes->shapeCornerRadius;
    shapes->BeginShape(Vec2{2.2, 3.7}, ShapeKind::Rectangle, 2.5);
    const auto rectangleOpening = shapes->PendingShapeOpening();
    Expect(rectangleOpening.has_value() &&
               shapes->PendingShape() ==
                   ShapeDraft{ShapeKind::Rectangle, Vec2{2, 4}, SelectionBounds{2, 4, 2, 4}, 2.5} &&
               shapes->shapeKind == originalKind && shapes->shapeCornerRadius == originalRadius,
           "captured-tool.shape-rectangle-kind-radius-and-rounded-anchor-are-event-values");
    shapes->CancelShape();
    shapes->BeginShape(Vec2{2.2, 3.7}, ShapeKind::Ellipse, 9.0);
    const auto ellipseOpening = shapes->PendingShapeOpening();
    Expect(ellipseOpening.has_value() && rectangleOpening.has_value() && ellipseOpening != rectangleOpening &&
               shapes->PendingShape() == ShapeDraft{ShapeKind::Ellipse, Vec2{2, 4}, SelectionBounds{2, 4, 2, 4}, 0.0} &&
               shapes->shapeKind == originalKind && shapes->shapeCornerRadius == originalRadius,
           "captured-tool.shape-explicit-ellipse-kind-radius-do-not-change-global-defaults");
    shapes->CancelShape();
    shapes->BeginShape(Vec2{2.2, 3.7}, ShapeKind::Ellipse, 9.0);
    const auto reopenedShapeOpening = shapes->PendingShapeOpening();
    Expect(reopenedShapeOpening.has_value() && reopenedShapeOpening != ellipseOpening &&
               shapes->PendingShape() == ShapeDraft{ShapeKind::Ellipse, Vec2{2, 4}, SelectionBounds{2, 4, 2, 4}, 0.0},
           "captured-tool.shape-cancel-and-equal-reopen-issue-new-opening");
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
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

void TestTransformVisibility()
{
    DocumentSession standalone;
    standalone.CreateDocument(200, 100, false);
    const Guid solo = InsertPainted(standalone, 20, 20, Vec2{0, 0});
    const auto expectSolo = [&](bool expected, const char* name)
    {
        const CanvasDocument& document = standalone.Document().value();
        Expect(EffectivelyVisible(document, solo) == expected && standalone.CanTransform() == expected, name);
    };

    expectSolo(true, "transform.visibility-standalone-visible");
    EditLayerRecordsForTest(standalone, [](DocumentFixture& document) { document.layers.back().visible = false; });
    expectSolo(false, "transform.visibility-standalone-hidden");
    EditLayerRecordsForTest(standalone, [](DocumentFixture& document) { document.layers.back().visible = true; });
    expectSolo(true, "transform.visibility-standalone-shown");

    const auto root = standalone.Document()->layers;
    const std::uint64_t generation = standalone.Generation();
    const int undoCount = standalone.history.UndoCount();
    const Raster pixels = standalone.ActiveLayer()->asset->image;
    bool repeated = true;
    for (int repeat = 0; repeat < 8; ++repeat)
        repeated = standalone.CanTransform() && EffectivelyVisible(standalone.Document().value(), solo) && repeated;
    Expect(repeated, "transform.visibility-repeated-query");
    const ImageLayer* after = Find(standalone.Document(), solo);
    Expect(standalone.Document()->layers.Identity() == root.Identity() && standalone.Generation() == generation &&
               standalone.history.UndoCount() == undoCount && after != nullptr && after->asset.has_value() &&
               after->asset->image == pixels,
           "transform.visibility-query-preserves-root-history-pixels");

    DocumentSession nested;
    nested.CreateDocument(200, 100, false);
    nested.AddGroup();
    const Guid outer = nested.ActiveLayerID().value();
    nested.AddGroup();
    const Guid inner = nested.ActiveLayerID().value();
    const Guid child = InsertPainted(nested, 20, 20, Vec2{0, 0});
    EditLayerRecordsForTest(nested, [&](DocumentFixture& document) { document.layers.back().parentID = inner; });
    nested.SelectLayer(child);
    Expect(ValidateHierarchy(nested.Document().value().layers) == DocumentStatus::Ok,
           "transform.visibility-nested-fixture-valid");
    const auto expectNested = [&](bool expected, const char* name)
    {
        const CanvasDocument& document = nested.Document().value();
        Expect(EffectivelyVisible(document, child) == expected && nested.CanTransform() == expected, name);
    };

    nested.SetGroupCollapsed(outer, true);
    const std::vector<HierarchyEntry> collapsed = nested.LayerRows();
    Expect(collapsed.size() == 1 && collapsed.front().id == outer && collapsed.front().collapsed,
           "transform.visibility-collapse-hides-row-subtree");
    expectNested(true, "transform.visibility-selected-child-in-collapsed-group");

    const auto setVisible = [&](const Guid& id, bool visible)
    {
        EditLayerRecordsForTest(nested,
                                [&](DocumentFixture& document)
                                {
                                    for (ImageLayer& layer : document.layers)
                                    {
                                        if (layer.id == id)
                                        {
                                            layer.visible = visible;
                                            return;
                                        }
                                    }
                                });
    };
    setVisible(inner, false);
    expectNested(false, "transform.visibility-hidden-inner-ancestor");
    setVisible(inner, true);
    setVisible(child, false);
    expectNested(false, "transform.visibility-hidden-leaf");
    setVisible(child, true);
    setVisible(outer, false);
    expectNested(false, "transform.visibility-hidden-outer-ancestor");
    setVisible(outer, true);
    expectNested(true, "transform.visibility-restored-ancestors-and-leaf");

    nested.SelectLayer(outer);
    const std::vector<Guid> expectedMembers{child};
    Expect(nested.TransformsAsGroup() && nested.CanTransform() && nested.GroupTransformMemberIDs() == expectedMembers,
           "transform.visibility-group-path-retains-visible-member");

    DocumentSession nonPixels;
    nonPixels.CreateDocument(20, 20, false);
    nonPixels.AddBlankLayer();
    const Guid blank = nonPixels.ActiveLayerID().value();
    Expect(EffectivelyVisible(nonPixels.Document().value(), blank) && !nonPixels.CanTransform(),
           "transform.visibility-blank-still-not-transformable");
    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Levels;
    EditLayerRecordsForTest(nonPixels,
                            [&](DocumentFixture& document) { document.layers.back().adjustment = adjustment; });
    Expect(EffectivelyVisible(nonPixels.Document().value(), blank) && !nonPixels.CanTransform(),
           "transform.visibility-adjustment-still-not-transformable");
    nonPixels.AddGroup();
    const Guid emptyGroup = nonPixels.ActiveLayerID().value();
    Expect(EffectivelyVisible(nonPixels.Document().value(), emptyGroup) && !nonPixels.CanTransform(),
           "transform.visibility-empty-group-still-not-transformable");
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.back().parentID = folder; });
    const Guid b = InsertPainted(session, 40, 40, Vec2{200, 100}, L"B");
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.back().parentID = folder; });
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.back().mask = mask; });
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.back().transform = before; });
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
    EditLayerRecordsForTest(
        session, [&](DocumentFixture& document)
        { document.selection = DocumentSelection{true, {SelectionLoop{Vec2{10, 10}, Vec2{50, 10}, Vec2{50, 40}}}}; });
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
    EditLayerRecordsForTest(trim, [&](DocumentFixture& document) { document.layers.push_back(stroke); });
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
    auto sessionOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& session = *sessionOwner;
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
    auto emptyOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& empty = *emptyOwner;
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
    auto stepsOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& steps = *stepsOwner;
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
    auto polygonOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& polygon = *polygonOwner;
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
    auto aliasedOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& aliased = *aliasedOwner;
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
    auto allOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& all = *allOwner;
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
    auto modesOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& modes = *modesOwner;
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
    auto blockedOwner = std::make_unique<DocumentSession>(SelectionSession());
    auto& blocked = *blockedOwner;
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
    auto noneOwner = std::make_unique<DocumentSession>();
    auto& none = *noneOwner;
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
    // RF-904: Escape (CancelSelectionMove) and Undo during a move put the outline back where the move began and close
    // its entry unrecorded, keeping the redo it had; the next Undo moves history again.
    DocumentSession cancel = SelectionSession();
    Lasso(cancel, Square(10, 10, 20));
    Lasso(cancel, Square(40, 40, 10), SelectionMode::Add);
    cancel.Undo();
    const int kept = cancel.history.UndoCount();
    Expect(cancel.BeginSelectionMove(), "move.cancel-begins");
    cancel.MoveSelection(15, 5);
    cancel.CancelSelectionMove();
    Expect(!cancel.IsMovingSelection() && NearBounds(cancel.Selection(), 10, 10, 20, 20) &&
               cancel.history.UndoCount() == kept && cancel.history.CanUndo() && !cancel.history.RedoName().empty(),
           "move.cancel-puts-it-back");
    Expect(cancel.BeginSelectionMove(), "move.undo-begins");
    cancel.MoveSelection(15, 5);
    cancel.Undo();
    Expect(!cancel.IsMovingSelection() && NearBounds(cancel.Selection(), 10, 10, 20, 20) &&
               cancel.history.UndoCount() == kept && !cancel.history.RedoName().empty(),
           "move.undo-drops-the-move");
    cancel.Undo();
    Expect(cancel.history.UndoCount() == kept - 1, "move.next-undo-moves-history");
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
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                for (ImageLayer& layer : document.layers)
                                {
                                    if (layer.id == id)
                                    {
                                        layer.mask =
                                            LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, true};
                                    }
                                }
                            });
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
    EditLayerRecordsForTest(scaled,
                            [&](DocumentFixture& document)
                            {
                                for (ImageLayer& layer : document.layers)
                                {
                                    if (layer.id == scaledId)
                                    {
                                        layer.transform.origin = Vec2{0, 0};
                                        layer.transform.size = Vec2{200, 200};
                                    }
                                }
                            });
    Expect(scaled.LoadMaskSelection(scaledId) && NearBounds(scaled.Selection(), 40, 60, 80, 80),
           "load.mask-follows-transform");
    scaled.Deselect();
    scaled.AddBlankLayer();
    const Guid blank = scaled.ActiveLayerID().value();
    const Raster white = GrayRaster(1, 1, 255);
    EditLayerRecordsForTest(scaled,
                            [&](DocumentFixture& document)
                            {
                                for (ImageLayer& layer : document.layers)
                                {
                                    if (layer.id == blank)
                                    {
                                        layer.mask =
                                            LayerMask{RasterAsset{white, white, L"mask"}, true, std::nullopt, true};
                                    }
                                }
                            });
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
    EditLayerRecordsForTest(layers, [&](DocumentFixture& document) { document.layers.push_back(ringLayer); });
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
    EditLayerRecordsForTest(flipped, [&](DocumentFixture& document) { document.layers.push_back(flippedLayer); });
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
    // RF-802: Nearest reads the exact source pixel instead of blending, so Liquify/Clone/Blur (and the Wand) read a
    // pixel-art layer crisp, as the canvas draws it (legacy LayerRenderer.draw: sampling == .nearest ? .none : …).
    placement.sampling = LayerSampling::Nearest;
    const Raster nearestScaled = RenderLayerPixels(halves, placement, 8, 6);
    Expect(pixelAt(nearestScaled, 3, 2) == red, "wand.sampler-scales-nearest");
    const Raster maskHalves = Gray(4, 3, {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255});
    const Raster maskNearest = ImageFilters::RenderMaskPixels(maskHalves, placement, 8, 6, 0);
    Expect(maskNearest.Data()[static_cast<std::size_t>(2 * maskNearest.Stride() + 3)] == 0,
           "mask.sampler-scales-nearest");
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
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
    session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
    return session;
}

[[nodiscard]] bool IsRed(const std::array<std::uint8_t, 4>& pixel) noexcept
{
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255;
}

void TestPixelBuffer()
{
    PixelBuffer empty = PixelBuffer::TryAllocate(0);
    Expect(!empty && empty.Data() == nullptr && empty.Size() == 0, "buffer.empty");
    Expect(!PixelBuffer::TryAllocate(std::numeric_limits<std::size_t>::max()), "buffer.overflow-refused");
    PixelBuffer pixels = PixelBuffer::TryAllocate(24);
    Expect(pixels && reinterpret_cast<std::uintptr_t>(pixels.Data()) % PixelBuffer::kAlignment == 0,
           "buffer.aligned-64");
    if (!pixels)
    {
        return;
    }
    for (std::size_t i = 0; i < pixels.Size(); ++i)
    {
        pixels[i] = static_cast<std::uint8_t>(i * 7);
    }
    const std::uint8_t* original = pixels.Data();
    PixelBuffer moved(std::move(pixels));
    Expect(!pixels && pixels.Size() == 0 && moved.Data() == original, "buffer.move-construction");
    pixels = std::move(moved);
    Expect(!moved && moved.Size() == 0 && pixels.Data() == original, "buffer.move-assignment");
    Expect(!Raster::Adopt(0, 2, RasterFormat::Bgra8888, std::move(pixels)).HasStorage() && pixels.Data() == original,
           "buffer.invalid-grid-keeps-owner");
    Expect(!Raster::Adopt(3, 3, RasterFormat::Bgra8888, std::move(pixels)).HasStorage() && pixels.Data() == original,
           "buffer.invalid-size-keeps-owner");
    Expect(!Raster::Adopt(3, 2, static_cast<RasterFormat>(42), std::move(pixels)).HasStorage() &&
               pixels.Data() == original,
           "buffer.invalid-format-keeps-owner");
    Raster raster = Raster::Adopt(3, 2, RasterFormat::Bgra8888, std::move(pixels));
    Expect(!pixels && pixels.Size() == 0 && raster.Data() == original && raster.ByteSize() == 24 &&
               raster.IsMaterialized() && !raster.IsSparse(),
           "buffer.adopt-does-not-copy");
    const Raster retained = raster;
    raster = {};
    Expect(retained.Data() == original && retained.Data()[23] == 161, "buffer.shared-lifetime");
    std::array<std::uint8_t, 22> region;
    region.fill(193);
    retained.ReadRegion(1, 0, 2, 2, region.data(), 11, 219);
    const std::array<std::uint8_t, 22> expectedRegion{28,  35,  42,  49,  56,  63,  70,  77,  193, 193, 193,
                                                      112, 119, 126, 133, 140, 147, 154, 161, 193, 193, 193};
    Expect(region == expectedRegion, "buffer.contained-region-preserves-padding");
    const Raster sparse = Raster::MakeSparse(3, 2, RasterFormat::Bgra8888, retained, 0, 0, {}, 0);
    std::array<std::uint8_t, 24> read{};
    sparse.ReadRegion(0, 0, 3, 2, read.data(), 12, 0);
    Expect(std::memcmp(read.data(), original, read.size()) == 0 && !sparse.IsMaterialized(),
           "buffer.adopted-sparse-base");
    Expect(sparse.Data() != nullptr && std::memcmp(sparse.Data(), original, read.size()) == 0,
           "buffer.adopted-base-assembly");
    region.fill(193);
    sparse.ReadRegion(1, 0, 2, 2, region.data(), 11, 219);
    Expect(region == expectedRegion, "buffer.published-region-preserves-padding");
    PixelBuffer gray = PixelBuffer::TryAllocate(6);
    if (gray)
    {
        std::memset(gray.Data(), 73, gray.Size());
    }
    const Raster mask = Raster::Adopt(3, 2, RasterFormat::Gray8, std::move(gray));
    Expect(mask.HasStorage() && mask.Stride() == 3 && mask.ByteSize() == 6 && mask.Data()[5] == 73,
           "buffer.gray-adoption");
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
    // RF-201: a sparse source keeps its own fill over its extent (a painted hide-all mask stays hidden); only the
    // area a crop adds beyond it reads the new fill, as windows of one small shared block.
    const Raster dabs = GrayRaster(2, 2, 200);
    const Raster hidden = Raster::MakeSparse(4, 4, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 1, dabs}}, 0);
    const Raster same = Raster::Replacing(hidden, 0, 0, {}, 0, 0, 4, 4, 255);
    std::array<std::uint8_t, 16> sameRead{};
    same.ReadRegion(0, 0, 4, 4, sameRead.data(), 4, 255);
    Expect(same.Fill() == 0 && sameRead[0] == 0 && sameRead[5] == 200 && sameRead[15] == 0,
           "sparse.replacing-keeps-source-fill");
    const Raster wider = Raster::Replacing(hidden, 2, 0, {}, 0, 0, 600, 4, 255);
    std::vector<std::uint8_t> widerRead(600u * 4u, 7);
    wider.ReadRegion(0, 0, 600, 4, widerRead.data(), 600, 7);
    const auto at = [&](int x, int y)
    { return widerRead[static_cast<std::size_t>(y) * 600u + static_cast<std::size_t>(x)]; };
    Expect(wider.Fill() == 0 && at(0, 0) == 255 && at(1, 3) == 255 && at(2, 0) == 0 && at(3, 1) == 200 &&
               at(4, 2) == 200 && at(5, 3) == 0 && at(6, 0) == 255 && at(599, 3) == 255 && !wider.IsMaterialized(),
           "sparse.replacing-pads-only-new-area");
    std::set<const void*> widerStorages;
    wider.ForEachStorage([&](const Raster& storage) { widerStorages.insert(storage.Identity()); });
    Expect(widerStorages.size() == 3 && wider.Tiles().size() <= 5, "sparse.replacing-padding-shares-one-block");
    const std::uint8_t* widerData = wider.Data();
    Expect(widerData != nullptr && std::memcmp(widerData, widerRead.data(), widerRead.size()) == 0,
           "sparse.replacing-padding-assembles");
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
    // The palette foreground is the paint color, including when the brush channels were set earlier.
    DocumentSession colored;
    colored.CreateDocument(80, 80, false);
    colored.AddBlankLayer();
    colored.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
    colored.SetForegroundColor(0xFF00FF00u);
    Expect(colored.BeginBrush(Vec2{40, 40}), "brush.foreground-begin");
    const std::array<std::uint8_t, 4> green = StrokePixel(*colored.PendingBrush(), 40, 40);
    Expect(green[0] == 0 && green[1] == 255 && green[2] == 0 && green[3] == 255, "brush.paints-foreground");
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
                                               BrushSettings{diameter, 0, 1, 1, 1, opacity}, 800, 800, nullptr);
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
    session.brushSettings = BrushSettings{400, 0, 1, 1, 1, 1};
    Expect(session.BeginBrush(Vec2{400, 1600}), "snapshot.begin");
    session.ContinueBrush(Vec2{400, 400});
    session.ContinueBrush(Vec2{1600, 400});
    session.FinishBrush();
    const Raster first = session.ActiveLayer()->asset->image;
    // Supply the optional derived cache explicitly so this legacy retained-thumbnail accounting golden still
    // covers a distinct thumbnail owner after production image commits stop computing unused thumbnails.
    EditLayerRecordsForTest(
        session, [](DocumentFixture& document)
        { document.layers.back().asset->thumbnail = Raster::MakeThumbnail(document.layers.back().asset->image); });
    const std::size_t firstThumbnail = session.ActiveLayer()->asset->thumbnail.ByteSize();
    Expect(firstThumbnail > 0, "snapshot.optional-thumbnail-accounting-fixture-has-retained-bytes");
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
    ridges.brushSettings = BrushSettings{400, 0, 1, 1, 1, 1};
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
    Expect(!BrushSettings{0, 1, 0, 0, 0, 1}.IsValid() && !BrushSettings{40, 1.5, 0, 0, 0, 1}.IsValid() &&
               !BrushSettings{40, 1, 0, 0, 0, 0}.IsValid() && BrushSettings{}.IsValid(),
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
        EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(imported); });
        session.SelectLayer(imported.id);
        const std::optional<CanvasDocument> original = session.Document();
        session.brushSettings = BrushSettings{16, 1, 0, 1, 0, 1};
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
    EditLayerRecordsForTest(grown, [&](DocumentFixture& document) { document.layers.push_back(masked); });
    grown.SelectLayer(masked.id);
    grown.brushSettings = BrushSettings{10, 1, 0, 1, 0, 1};
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
    EditLayerRecordsForTest(scaled, [&](DocumentFixture& document) { document.layers.push_back(blue); });
    scaled.SelectLayer(blue.id);
    scaled.selectionAntialiased = true;
    scaled.ApplySelection({SelectionLoop{Vec2{0, 0}, Vec2{100, 0}, Vec2{0, 40}}}, SelectionMode::New, L"Select");
    scaled.brushSettings = BrushSettings{300, 1, 0, 0, 0, 1};
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
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
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
        session.brushSettings = BrushSettings{24, 1, 0, 0, 0, 1};
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
    session.brushSettings = BrushSettings{6, 1, 0, 0, 0, 1};
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
    session.brushSettings = BrushSettings{20, 1, 0, 0, 0, 1};
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
    EditLayerRecordsForTest(masked,
                            [&](DocumentFixture& document)
                            {
                                for (ImageLayer& layer : document.layers)
                                {
                                    if (layer.id == id)
                                    {
                                        layer.mask->asset.image = halfMask;
                                        layer.mask->asset.thumbnail = halfMask;
                                    }
                                }
                            });
    masked.SelectLayerTarget(id, true);
    masked.brushSettings = BrushSettings{20, 1, 0, 0, 0, 1};
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
    // Literal private-copy reference: a nested sparse tile window with an overlapping replacement.
    // Reading the working image must not grow either source cache or history's resident charge.
    const std::array<std::uint8_t, 16> leafBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const Raster leaf = Raster::Make(2, 2, RasterFormat::Bgra8888, leafBytes.data(), leafBytes.size());
    const Raster nested = Raster::MakeSparse(3, 2, RasterFormat::Bgra8888, leaf, 1, 0, {{1, 1, leaf, 1, 1, 1, 1}}, 11);
    const Raster sparse = Raster::MakeSparse(4, 3, RasterFormat::Bgra8888, {}, 0, 0, {{1, 1, nested, 1, 0, 2, 2}}, 5);
    const std::array<std::uint8_t, 48> expected{5, 5, 5, 5, 5,  5,  5,  5,   5,  5,  5,  5,   5, 5, 5, 5,
                                                5, 5, 5, 5, 1,  2,  3,  255, 4,  5,  6,  255, 5, 5, 5, 5,
                                                5, 5, 5, 5, 10, 11, 12, 255, 10, 11, 12, 255, 5, 5, 5, 5};
    const std::uint64_t emptyStorageBytes = sizeof(WarpStroke) + 1024;
    const Raster onePixel = Raster::Make(1, 1, RasterFormat::Bgra8888, leafBytes.data(), 4);
    WarpStroke emptyStroke(onePixel, SmearMode::Liquify, BrushSettings{});
    Expect(emptyStroke.IsValid() && emptyStroke.Points().empty() &&
               emptyStroke.RetirementMemoryBytes() > emptyStorageBytes,
           "warp.private-source.valid-empty-stroke-retains-working-copy");
    Expect(!sparse.IsMaterialized() && !nested.IsMaterialized() && sparse.ByteSize() == 0 && nested.ByteSize() == 0,
           "warp.private-source.fixture-caches-unassembled");
    for (const SmearMode mode : {SmearMode::Smudge, SmearMode::Liquify})
    {
        WarpStroke working(sparse, mode, BrushSettings{});
        const Raster copied = working.Image();
        Expect(working.IsValid() && working.Width() == 4 && working.Height() == 3 && working.Points().empty(),
               "warp.private-source.valid-initial-copy-without-dabs");
        Expect(copied.HasStorage() && copied.ByteSize() == expected.size() &&
                   std::equal(expected.begin(), expected.end(), copied.Data()),
               "warp.private-source.literal-nested-window-and-overlap-pixels");
        Expect(!sparse.IsMaterialized() && !nested.IsMaterialized() && sparse.ByteSize() == 0 && nested.ByteSize() == 0,
               "warp.private-source.leaves-complete-source-residency-unchanged");
        Expect(working.TakeChangedTiles().empty(), "warp.private-source.initial-copy-has-no-dirty-preview");
    }
    CancelSource stopped;
    (void)stopped.Cancel();
    WarpStroke cancelled(sparse, SmearMode::Smudge, BrushSettings{}, stopped.Token());
    Expect(!cancelled.IsValid() && !cancelled.Image().HasStorage() && cancelled.Points().empty() &&
               cancelled.TakeChangedTiles().empty() && cancelled.Width() == 0 && cancelled.Height() == 0 &&
               cancelled.RetirementMemoryBytes() == emptyStorageBytes &&
               cancelled.RetirementMemoryBytes() < emptyStroke.RetirementMemoryBytes(),
           "warp.private-source.cancelled-before-copy-exposes-no-partial-stroke");
    Expect(!sparse.IsMaterialized() && !nested.IsMaterialized(),
           "warp.private-source.cancelled-copy-leaves-source-caches-unassembled");
    WarpStroke shutdown(sparse, SmearMode::Liquify, BrushSettings{}, CancelToken{{}, stopped.Token().LocalToken()});
    Expect(!shutdown.IsValid() && !shutdown.Image().HasStorage() && !sparse.IsMaterialized() &&
               shutdown.RetirementMemoryBytes() == emptyStorageBytes,
           "warp.private-source.worker-shutdown-token-refuses-copy");
    const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8);
    WarpStroke invalid(gray, SmearMode::Liquify, BrushSettings{});
    Expect(!invalid.IsValid() && !invalid.Image().HasStorage() && invalid.RetirementMemoryBytes() == emptyStorageBytes,
           "warp.private-source.gray-input-refused");
    Raster tooDeep = leaf;
    for (std::size_t depth = 0; depth < kRasterWorkDepthLimit; ++depth)
        tooDeep = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, tooDeep}}, 0);
    WarpStroke refused(tooDeep, SmearMode::Liquify, BrushSettings{});
    Expect(!refused.IsValid() && !refused.Image().HasStorage() && refused.TakeChangedTiles().empty() &&
               !tooDeep.IsMaterialized() && refused.Width() == 0 && refused.Height() == 0 &&
               refused.RetirementMemoryBytes() == emptyStorageBytes &&
               refused.RetirementMemoryBytes() < emptyStroke.RetirementMemoryBytes(),
           "warp.private-source.depth-refusal-never-publishes-partial-pixels");
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    const std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const Raster halves = BytesRaster(60, 20, [&](int x, int) { return x < 30 ? red : blue; });
    // Smudge drags the color it picked up along the stroke.
    DocumentSession smudge;
    smudge.CreateDocument(60, 20, false);
    InsertPixels(smudge, halves, L"Halves");
    smudge.smearMode = SmearMode::Smudge;
    smudge.brushSettings = BrushSettings{10, 0.5, 0, 0, 0, 0.8};
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
    liquify.brushSettings = BrushSettings{12, 0.5, 0, 0, 0, 1};
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
    cancel.brushSettings = BrushSettings{10, 0.5, 0, 0, 0, 0.8};
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

void TestGradientPreviewPalette()
{
    // Legacy paletteChangesUpdatePendingPreview, and the pending tiles feed the preview. Kept in its own
    // function so this suite's many DocumentSession fixtures do not accumulate into one oversized stack frame.
    DocumentSession session = GradientSession();
    DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
    const BrushStroke& raster = *session.PendingGradient()->raster;
    Expect(!raster.Tiles().empty() && PixelOf(raster.Tiles().front().image, 0, 2)[2] == 0, "gradient.preview-tiles");
    session.SwapPaletteColors();
    Expect(session.GradientColors()[0][0] == 1.0 && PixelOf(raster.Tiles().front().image, 0, 2)[2] == 255,
           "gradient.palette-updates-preview");
    // Switching layers applies it.
    const int count = session.history.UndoCount();
    session.AddBlankLayer();
    Expect(session.PendingGradient() == nullptr && session.history.UndoCount() == count + 2,
           "gradient.layer-switch-applies");
}

void TestGradientForegroundToBackground()
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
}

void TestGradientRadial()
{
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
}

void TestGradientReverseAndOpacity()
{
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
}

void TestGradientForegroundToTransparent()
{
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
}

void TestGradientCancelAndUndo()
{
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
}

void TestGradientRedrag()
{
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
}

void TestGradientMask()
{
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
}

void TestGradientMaskPaletteColors()
{
    // The same legacy case with a colored palette (RF-205, legacy paletteColor): a mask gradient runs from the mask
    // palette's foreground (black unless `maskPaintWhite`) to its background, whatever the RGB colors are.
    for (const bool paintWhite : {false, true})
    {
        DocumentSession session = GradientSession();
        DragGradient(session, Vec2{0, 2}, Vec2{0.6, 2});
        session.CommitGradient();
        session.AddLayerMask(true);
        // Colors whose red channel would give the opposite gray, so the old "first component" rule shows.
        session.SetForegroundColor(paintWhite ? 0xFF0000FFu : 0xFFFF0000u);
        session.SetBackgroundColor(paintWhite ? 0xFFFF0000u : 0xFF0000FFu);
        session.maskPaintWhite = paintWhite;
        const std::array<double, 4> black{0, 0, 0, 1};
        const std::array<double, 4> white{1, 1, 1, 1};
        const std::array<std::array<double, 4>, 2> ends = session.GradientColors();
        Expect(session.IsMaskSelected() && ends[0] == (paintWhite ? white : black) &&
                   ends[1] == (paintWhite ? black : white),
               paintWhite ? "gradient.mask-palette-colors-white" : "gradient.mask-palette-colors");
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        session.CommitGradient();
        const int start = ActiveMaskValue(session, 0, 2);
        const int end = ActiveMaskValue(session, 100, 2);
        Expect(session.history.UndoName() == L"Gradient Mask" && Near(start, paintWhite ? 255 : 0) &&
                   Near(end, paintWhite ? 0 : 255) && Near(ActiveMaskValue(session, 50, 2), 128),
               paintWhite ? "gradient.mask-palette-white-first" : "gradient.mask-palette-ignores-rgb");
        // Off the mask the RGB palette is back.
        session.SelectLayerTarget(session.ActiveLayerID().value(), false);
        Expect(session.GradientColors()[0] ==
                   (paintWhite ? std::array<double, 4>{0, 0, 1, 1} : std::array<double, 4>{1, 0, 0, 1}),
               paintWhite ? "gradient.layer-palette-colors-after-white" : "gradient.layer-palette-colors");
    }
}

void TestGradientPaletteCommands()
{
    // X and D on a targeted mask (legacy ColorPalette.swift swapPaletteColors / resetPaletteColors) flip and reset
    // the mask palette, not the RGB colors, and a pending mask gradient follows; off the mask they change the colors.
    {
        DocumentSession session = GradientSession();
        DragGradient(session, Vec2{0, 2}, Vec2{0.6, 2});
        session.CommitGradient();
        session.AddLayerMask(true);
        session.SetForegroundColor(0xFFFF0000u);
        session.SetBackgroundColor(0xFF0000FFu);
        session.maskPaintWhite = false;
        DragGradient(session, Vec2{0.5, 2}, Vec2{100.5, 2});
        session.SwapPaletteColors();
        const bool flipped = session.IsMaskSelected() && session.maskPaintWhite &&
                             session.ForegroundColor() == 0xFFFF0000u && session.BackgroundColor() == 0xFF0000FFu;
        session.CommitGradient();
        Expect(flipped && Near(ActiveMaskValue(session, 0, 2), 255) && Near(ActiveMaskValue(session, 100, 2), 0),
               "palette.mask-swap-flips-mask-paint");
        session.ResetPaletteColors();
        Expect(!session.maskPaintWhite && session.ForegroundColor() == 0xFFFF0000u &&
                   session.BackgroundColor() == 0xFF0000FFu,
               "palette.mask-reset-paints-black");
        session.SelectLayerTarget(session.ActiveLayerID().value(), false);
        session.SwapPaletteColors();
        Expect(!session.maskPaintWhite && session.ForegroundColor() == 0xFF0000FFu &&
                   session.BackgroundColor() == 0xFFFF0000u,
               "palette.image-swap-leaves-mask-paint");
    }
}

void TestGradientTool()
{
    TestGradientForegroundToBackground();
    TestGradientRadial();
    TestGradientReverseAndOpacity();
    TestGradientForegroundToTransparent();
    TestGradientCancelAndUndo();
    TestGradientRedrag();
    TestGradientMask();
    TestGradientMaskPaletteColors();
    TestGradientPaletteCommands();
    TestGradientPreviewPalette();
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
        const auto& layers = session.Document().value().layers;
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
        session.brushSettings = BrushSettings{10, 1, 0, 0, 1, 1};
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
    // The comparison ring shows by default: the View > Sample Ring setting, which the host reads (RF-810).
    Expect(AppSettings{}.sampleRing, "eyedropper.ring-shown-by-default");
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
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    // Legacy brushPaintsOnlyInsideTheSelection.
    {
        DocumentSession session = PixelEditSession();
        SelectRect(session, 0, 0, 50, 40);
        session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
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
        session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
        static_cast<void>(session.BeginBrush(Vec2{5, 20}));
        session.ContinueBrush(Vec2{95, 20});
        session.FinishBrush();
        session.FillSelection(FillSource::Foreground);
        session.ClearSelectedPixels();
        Expect(!session.BeginGradient(Vec2{0, 20}) && session.PendingGradient() == nullptr, "edit.empty-no-gradient");
        Expect(!session.CanPaint() && session.Document().value() == before && session.history.UndoCount() == count &&
                   ActivePixel(session, 50, 20)[3] == 0,
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
}

void TestPixelEditMasksAndInvert()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
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
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    for (ImageLayer& layer : document.layers)
                                    {
                                        if (layer.id == id)
                                        {
                                            layer.transform.origin = Vec2{0, 0};
                                            layer.transform.size = Vec2{100, 100};
                                        }
                                    }
                                });
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
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    for (ImageLayer& layer : document.layers)
                                    {
                                        if (layer.id == greenLayer)
                                        {
                                            layer.transform.origin = Vec2{60, 10};
                                        }
                                    }
                                });
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

// ── Color adjustments and filters (C12) ───────────────────────

// A one-row raster from premultiplied BGRA pixels.
Raster RowRaster(const std::vector<std::array<std::uint8_t, 4>>& pixels)
{
    return BytesRaster(static_cast<int>(pixels.size()), 1,
                       [&](int x, int) { return pixels[static_cast<std::size_t>(x)]; });
}

std::vector<std::uint8_t> Bytes(const Raster& raster)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(raster.Width()) *
                                    static_cast<std::size_t>(raster.Height()) * 4u);
    raster.ReadRegion(0, 0, raster.Width(), raster.Height(), bytes.data(), raster.Width() * 4, 0);
    return bytes;
}

// The straight RGBA of a pixel (0 when transparent), the legacy tests' reading order.
std::array<int, 4> StraightOf(const Raster& raster, int x, int y)
{
    std::array<std::uint8_t, 4> p{};
    raster.ReadRegion(x, y, 1, 1, p.data(), 4, 0);
    const int a = p[3];
    const auto straight = [&](int c)
    { return a == 0 ? 0 : std::min(255, (p[static_cast<std::size_t>(c)] * 255 + a / 2) / a); };
    return {straight(2), straight(1), straight(0), a};
}

std::array<int, 4> ActiveStraight(const DocumentSession& session, int x, int y)
{
    const ImageLayer* layer = session.ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value())
    {
        return {0, 0, 0, 0};
    }
    return StraightOf(layer->asset->image, x - static_cast<int>(std::lround(layer->transform.origin.x)),
                      y - static_cast<int>(std::lround(layer->transform.origin.y)));
}

bool Near4(const std::array<int, 4>& value, const std::array<int, 4>& target, int tolerance = 8)
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (std::abs(value[i] - target[i]) > tolerance)
        {
            return false;
        }
    }
    return true;
}

Raster SolidStraight(int width, int height, double red, double green, double blue, double alpha = 1)
{
    const std::uint8_t a = static_cast<std::uint8_t>(std::lround(alpha * 255));
    return SolidRaster(width, height, static_cast<std::uint8_t>(std::lround(blue * a)),
                       static_cast<std::uint8_t>(std::lround(green * a)),
                       static_cast<std::uint8_t>(std::lround(red * a)), a);
}

LayerAdjustment Adjustment(AdjustmentKind kind)
{
    LayerAdjustment adjustment;
    adjustment.kind = kind;
    return adjustment;
}

// A 40×20 canvas: left half pure red, right half mid gray, with a half-transparent blue strip.
DocumentSession HueSession()
{
    DocumentSession session = PixelEditSession(40, 20);
    InsertPixels(session,
                 BytesRaster(40, 20,
                             [](int x, int y) -> std::array<std::uint8_t, 4>
                             {
                                 if (y >= 16)
                                 {
                                     return {128, 0, 0, 128};
                                 }
                                 return x < 20 ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                                               : std::array<std::uint8_t, 4>{128, 128, 128, 255};
                             }),
                 L"Colors");
    return session;
}

// A 40×20 canvas: left half pure red, right half pure blue, both opaque.
DocumentSession RedAndBlue()
{
    DocumentSession session = PixelEditSession(40, 20);
    InsertPixels(session,
                 BytesRaster(40, 20,
                             [](int x, int) -> std::array<std::uint8_t, 4>
                             {
                                 return x < 20 ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                                               : std::array<std::uint8_t, 4>{255, 0, 0, 255};
                             }),
                 L"RedBlue");
    return session;
}

void ApplyHue(DocumentSession& session, const HueSaturationSettings& settings)
{
    session.BeginColorEdit(FilterKind::HueSaturation);
    session.UpdateHueSaturation(settings, true);
    session.CommitColorEdit();
}

void TestLevels()
{
    const std::vector<std::array<std::uint8_t, 4>> ramp{{0, 0, 0, 255},       {64, 64, 64, 255}, {128, 128, 128, 255},
                                                        {255, 255, 255, 255}, {0, 32, 64, 128},  {0, 0, 0, 0}};
    const auto rampSession = [&]
    {
        DocumentSession session = PixelEditSession(6, 1);
        InsertPixels(session, RowRaster(ramp), L"Ramp");
        return session;
    };
    // Legacy identityAndChannelSelectionAreExactNoOps.
    {
        DocumentSession session = rampSession();
        const CanvasDocument before = session.Document().value();
        const Raster source = session.ActiveLayer()->asset->image;
        Expect(Adjust::ApplyAdjustment(Adjustment(AdjustmentKind::Levels), source) == source,
               "levels.identity-is-the-same-raster");
        const int count = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        LevelsSettings settings;
        settings.channel = LevelsChannel::Blue;
        session.UpdateLevels(settings, true);
        Expect(session.HasColorEdit(FilterKind::Levels) && !session.CanPaint(), "levels.other-edits-wait");
        session.CommitColorEdit();
        Expect(session.Document().value() == before && session.history.UndoCount() == count &&
                   !session.HasColorEdit(FilterKind::Levels),
               "levels.identity-and-channel-are-no-ops");
    }
    // Legacy inputClippingGammaOutputInversionAndAlpha.
    {
        const Raster source = RowRaster(ramp);
        LevelsSettings settings;
        Adjust::SetLevelsCurrent(settings, LevelRange{64, 1, 128, 0, 255});
        LayerAdjustment levels = Adjustment(AdjustmentKind::Levels);
        levels.levels = settings;
        const std::vector<std::uint8_t> clipped = Bytes(Adjust::ApplyAdjustment(levels, source));
        Expect(std::vector<std::uint8_t>(clipped.begin(), clipped.begin() + 12) ==
                   std::vector<std::uint8_t>{0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255},
               "levels.input-clipping");
        Adjust::SetLevelsCurrent(settings, LevelRange{0, 2, 255, 0, 255});
        levels.levels = settings;
        const std::vector<std::uint8_t> brightened = Bytes(Adjust::ApplyAdjustment(levels, source));
        Expect(std::abs(static_cast<int>(brightened[4]) - 128) <= 1 &&
                   std::abs(static_cast<int>(brightened[8]) - 181) <= 1,
               "levels.gamma");
        Expect(brightened[19] == 128 && brightened[23] == 0 && brightened[16] <= 128 && brightened[17] <= 128,
               "levels.gamma-keeps-alpha");
        Adjust::SetLevelsCurrent(settings, LevelRange{0, 1, 255, 255, 0});
        levels.levels = settings;
        const std::vector<std::uint8_t> inverted = Bytes(Adjust::ApplyAdjustment(levels, source));
        Expect(inverted[0] == 255 && inverted[12] == 0, "levels.output-inversion");
        Expect(inverted[16] == 128 && inverted[17] == 96 && inverted[18] == 64 && inverted[19] == 128,
               "levels.inversion-through-alpha");
    }
    // Legacy channelsCoexistAndUseDocumentedOrder.
    {
        LevelsSettings settings;
        settings.channel = LevelsChannel::Red;
        Adjust::SetLevelsCurrent(settings, LevelRange{0, 2, 255, 0, 255});
        settings.channel = LevelsChannel::Rgb;
        Adjust::SetLevelsCurrent(settings, LevelRange{40, 1, 210, 0, 255});
        LayerAdjustment levels = Adjustment(AdjustmentKind::Levels);
        levels.levels = settings;
        const std::vector<std::uint8_t> result = Bytes(Adjust::ApplyAdjustment(levels, RowRaster({{64, 64, 64, 255}})));
        const double expectedRed = Adjust::LevelRangeApply(
            LevelRange{40, 1, 210, 0, 255}, Adjust::LevelRangeApply(LevelRange{0, 2, 255, 0, 255}, 64.0 / 255));
        Expect(std::fabs(result[2] - expectedRed * 255) <= 1 && result[1] == result[0] && result[2] > result[1],
               "levels.channels-coexist");
        const LevelRange invalid = LevelRange{300, std::nan(""), -1, -100, 400}.Normalized();
        Expect(invalid.black < invalid.white && invalid.gamma == 1 && invalid.outputBlack == 0 &&
                   invalid.outputWhite == 255,
               "levels.normalized");
    }
    // Legacy histogramExcludesTransparencyAndWeightsSelection.
    {
        const Raster source = RowRaster({{0, 0, 255, 255}, {0, 128, 0, 128}, {0, 0, 0, 0}});
        const LevelsHistogram bins = Adjust::Histogram(source, nullptr);
        Expect(bins[1][255] == 1 && std::fabs(bins[2][255] - 128.0 / 255) < 0.00001, "levels.histogram-alpha-weighted");
        double total = 0;
        for (const double bin : bins[0])
        {
            total += bin;
        }
        Expect(std::fabs(total - (1.0 + 128.0 / 255)) < 0.00001, "levels.histogram-total");
        const Raster coverage =
            Raster::Make(3, 1, RasterFormat::Gray8, std::array<std::uint8_t, 3>{255, 0, 0}.data(), 3);
        const LevelsHistogram selected = Adjust::Histogram(source, &coverage);
        Expect(selected[1][255] == 1 && selected[2][255] == 0, "levels.histogram-selection");
        const Raster none = Raster::Make(3, 1, RasterFormat::Gray8, std::array<std::uint8_t, 3>{0, 0, 0}.data(), 3);
        const LevelsHistogram empty = Adjust::Histogram(source, &none);
        bool allZero = true;
        for (const auto& channel : empty)
        {
            for (const double bin : channel)
            {
                allZero = allZero && bin == 0;
            }
        }
        Expect(allZero, "levels.histogram-empty-selection");
    }
    // Legacy selectionPreviewCancelCommitUndoAndPersistence (the file round trip lives in IoTests).
    {
        DocumentSession session = rampSession();
        SelectRect(session, 0, 0, 2, 1, false);
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        Expect(session.HasColorEdit(FilterKind::Levels) && session.PendingColorEdit()->histogramReady,
               "levels.histogram-ready");
        LevelsSettings settings;
        Adjust::SetLevelsCurrent(settings, LevelRange{0, 1, 255, 255, 0});
        session.UpdateLevels(settings, true);
        const std::vector<std::uint8_t> preview = Bytes(session.PendingColorEdit()->previewRaster);
        Expect(preview.size() == 24 && preview[0] == 255 && preview[8] == 128 && session.Document().value() == before,
               "levels.preview-inside-selection");
        const CanvasDocument* shown = session.DisplayDocument();
        Expect(shown != nullptr && shown != &session.Document().value() && shown->layers.size() == 2 &&
                   Bytes(shown->layers.back().asset->image) == preview,
               "levels.display-shows-preview");
        session.UpdateLevels(settings, false);
        Expect(!session.PendingColorEdit()->previewRaster.HasStorage() &&
                   session.DisplayDocument() == &session.Document().value(),
               "levels.preview-off");
        session.CancelColorEdit();
        Expect(session.Document().value() == before && session.history.UndoCount() == count, "levels.cancel");
        session.BeginColorEdit(FilterKind::Levels);
        session.UpdateLevels(settings, false);
        session.CommitColorEdit();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Levels",
               "levels.commit-entry");
        const Raster committed = session.ActiveLayer()->asset->image;
        Expect(Bytes(committed) == preview, "levels.commit-matches-preview");
        session.Undo();
        Expect(session.Document().value() == before, "levels.undo");
        session.Redo();
        Expect(session.ActiveLayer()->asset->image == committed, "levels.redo");
    }
    // Legacy histogramDisplayKeepsDistributionVisibleBesideClippingSpikes.
    {
        std::array<double, 256> bins{};
        bins.fill(100.0);
        bins[255] = 100'000;
        Expect(Adjust::HistogramDisplayScale(bins) == 400, "levels.display-scale-spike");
        bins[0] = 200'000;
        Expect(Adjust::HistogramDisplayScale(bins) == 400, "levels.display-scale-two-spikes");
        bins[128] = 500'000;
        Expect(Adjust::HistogramDisplayScale(bins) == 400 && bins[128] == 500'000, "levels.display-scale-inner-spike");
        std::array<double, 256> flat{};
        flat.fill(100.0);
        Expect(Adjust::HistogramDisplayScale(flat) == 100, "levels.display-scale-flat");
        std::array<double, 256> sparse{};
        Expect(Adjust::HistogramDisplayScale(sparse) == 0, "levels.display-scale-empty");
        sparse[255] = 50;
        Expect(Adjust::HistogramDisplayScale(sparse) == 50, "levels.display-scale-end");
        sparse[0] = 100;
        Expect(Adjust::HistogramDisplayScale(sparse) == 100, "levels.display-scale-ends");
        sparse[128] = 200;
        Expect(Adjust::HistogramDisplayScale(sparse) == 200, "levels.display-scale-middle");
    }
    // Legacy autoAlgorithmsAndEyedropperCalibration.
    {
        LevelsHistogram bins{};
        for (std::size_t c = 1; c <= 3; ++c)
        {
            bins[c][20 * c] = 100;
            bins[c][200 + c * 10] = 100;
        }
        const LevelsSettings linked = Adjust::AutoLevels(LevelsAuto::Contrast, bins);
        Expect(linked.ranges[0].black == 20 && linked.ranges[0].white == 230, "levels.auto-contrast");
        const LevelsSettings color = Adjust::AutoLevels(LevelsAuto::Color, bins);
        Expect(color.ranges[1].black == 20 && color.ranges[3].black == 60 && color.ranges[0] == LevelRange{},
               "levels.auto-color");
        Expect(Adjust::AutoLevels(LevelsAuto::Neutral, bins).ranges[1].gamma == 1, "levels.auto-neutral");
        const LevelsHistogram empty{};
        Expect(Adjust::LevelsIsIdentity(Adjust::AutoLevels(LevelsAuto::Contrast, empty)) &&
                   Adjust::LevelsIsIdentity(Adjust::AutoLevels(LevelsAuto::Color, empty)) &&
                   Adjust::LevelsIsIdentity(Adjust::AutoLevels(LevelsAuto::Neutral, empty)),
               "levels.auto-empty");
        const std::array<double, 3> rgb{0.25, 0.4, 0.6};
        bool calibrated = true;
        for (const LevelsSample mode : {LevelsSample::Black, LevelsSample::Gray, LevelsSample::White})
        {
            const LevelsSettings settings = Adjust::SampledLevels(LevelsSettings{}, rgb, mode);
            const double target = mode == LevelsSample::Black ? 0 : mode == LevelsSample::White ? 1 : 0.5;
            const LevelsChannel channels[] = {LevelsChannel::Red, LevelsChannel::Green, LevelsChannel::Blue};
            for (std::size_t i = 0; i < 3; ++i)
            {
                calibrated =
                    calibrated && std::fabs(Adjust::LevelsApply(settings, rgb[i], channels[i]) - target) < 0.0001;
            }
        }
        Expect(calibrated, "levels.eyedropper-calibration");
    }
    // Legacy eyedropperSamplesOriginalAndRejectsTransparentPixels.
    {
        DocumentSession session = rampSession();
        session.BeginColorEdit(FilterKind::Levels);
        session.SetLevelsSampleMode(LevelsSample::Gray);
        session.SampleLevels(Vec2{2.5, 0.5});
        const LevelsSettings first = session.PendingColorEdit()->settings.levels;
        session.SampleLevels(Vec2{2.5, 0.5});
        Expect(session.PendingColorEdit()->settings.levels == first, "levels.sample-repeat");
        session.SampleLevels(Vec2{5.5, 0.5});
        Expect(session.PendingColorEdit()->settings.levels == first, "levels.sample-rejects-transparent");
        session.SampleLevels(Vec2{-1, 0.5});
        Expect(session.PendingColorEdit()->settings.levels == first, "levels.sample-rejects-outside");
        session.CancelColorEdit();
    }
}

void TestHueSaturation()
{
    // Legacy defaultsAreAnExactNoOp.
    {
        DocumentSession session = HueSession();
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::HueSaturation);
        Expect(session.HasColorEdit(FilterKind::HueSaturation) && !session.CanPaint(), "hsv.other-edits-wait");
        session.UpdateHueSaturation(HueSaturationSettings{}, true);
        session.CommitColorEdit();
        Expect(session.Document().value() == before && session.history.UndoCount() == count &&
                   !session.HasColorEdit(FilterKind::HueSaturation) && session.CanPaint(),
               "hsv.defaults-are-a-no-op");
    }
    // Legacy hueRotatesSaturationAndLightnessFollowPhotoshopRanges.
    {
        DocumentSession session = HueSession();
        ApplyHue(session, Adjust::HsvSettings(120));
        Expect(Near4(ActiveStraight(session, 5, 5), {0, 255, 0, 255}) &&
                   Near4(ActiveStraight(session, 30, 5), {128, 128, 128, 255}),
               "hsv.hue-rotates");
        session.Undo();
        ApplyHue(session, Adjust::HsvSettings(0, -100));
        const std::array<int, 4> gray = ActiveStraight(session, 5, 5);
        Expect(gray[0] == gray[1] && gray[1] == gray[2] && gray[3] == 255, "hsv.desaturates");
        session.Undo();
        ApplyHue(session, Adjust::HsvSettings(0, 0, 100));
        Expect(Near4(ActiveStraight(session, 5, 5), {255, 255, 255, 255}), "hsv.lightness-to-white");
        session.Undo();
        ApplyHue(session, Adjust::HsvSettings(0, 0, -100));
        Expect(Near4(ActiveStraight(session, 5, 5), {0, 0, 0, 255}), "hsv.lightness-to-black");
    }
    // Legacy colorizeGivesEverythingOneHueAndKeepsAlpha.
    {
        DocumentSession session = HueSession();
        ApplyHue(session, Adjust::HsvSettings(240, 100, 0, true));
        const std::array<int, 4> left = ActiveStraight(session, 5, 5);
        const std::array<int, 4> right = ActiveStraight(session, 30, 5);
        const std::array<int, 4> strip = ActiveStraight(session, 5, 18);
        Expect(left[2] > left[0] && right[2] > right[0] && left[3] == 255 && std::abs(strip[3] - 128) <= 2,
               "hsv.colorize");
    }
    // Legacy adjustmentStaysInsideTheSelectionAndIsOneUndoStep.
    {
        DocumentSession session = HueSession();
        SelectRect(session, 0, 0, 10, 20, false);
        const int count = session.history.UndoCount();
        ApplyHue(session, Adjust::HsvSettings(120));
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Hue/Saturation" &&
                   Near4(ActiveStraight(session, 5, 5), {0, 255, 0, 255}) &&
                   Near4(ActiveStraight(session, 15, 5), {255, 0, 0, 255}),
               "hsv.inside-selection");
        session.Undo();
        Expect(Near4(ActiveStraight(session, 5, 5), {255, 0, 0, 255}), "hsv.selection-undo");
    }
    // Legacy previewIsLiveDoesNotTouchTheDocumentAndNeverAccumulates.
    {
        DocumentSession session = HueSession();
        const CanvasDocument before = session.Document().value();
        const int count = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::HueSaturation);
        session.UpdateHueSaturation(Adjust::HsvSettings(120), true);
        Expect(Near4(StraightOf(session.PendingColorEdit()->previewRaster, 5, 5), {0, 255, 0, 255}) &&
                   session.Document().value() == before && session.history.UndoCount() == count,
               "hsv.preview-live");
        session.UpdateHueSaturation(Adjust::HsvSettings(240), true);
        Expect(Near4(StraightOf(session.PendingColorEdit()->previewRaster, 5, 5), {0, 0, 255, 255}),
               "hsv.preview-never-accumulates");
        session.UpdateHueSaturation(Adjust::HsvSettings(240), false);
        Expect(!session.PendingColorEdit()->previewRaster.HasStorage(), "hsv.preview-off");
        session.CancelColorEdit();
        Expect(session.Document().value() == before && !session.HasColorEdit(FilterKind::HueSaturation), "hsv.cancel");
        session.BeginColorEdit(FilterKind::HueSaturation);
        session.UpdateHueSaturation(Adjust::HsvSettings(120), true);
        session.CommitColorEdit();
        Expect(session.ActiveLayer()->asset->image.Width() == before.layers.back().asset->image.Width() &&
                   Near4(ActiveStraight(session, 5, 5), {0, 255, 0, 255}),
               "hsv.commit-full-size");
    }
    // Legacy bandWeightsRampThroughFalloffAndWrapAround.
    {
        const HueBand reds = Adjust::DefaultBand(ColorRange::Reds);
        Expect(Adjust::BandWeight(reds, 0) == 1 && Adjust::BandWeight(reds, 345) == 1 &&
                   Adjust::BandWeight(reds, 15) == 1,
               "hsv.band-plateau");
        Expect(std::fabs(Adjust::BandWeight(reds, 330) - 0.5) < 0.001 &&
                   std::fabs(Adjust::BandWeight(reds, 30) - 0.5) < 0.001,
               "hsv.band-shoulders");
        Expect(Adjust::BandWeight(reds, 315) == 0 && Adjust::BandWeight(reds, 45) == 0 &&
                   Adjust::BandWeight(reds, 180) == 0 &&
                   Adjust::BandWeight(Adjust::DefaultBand(ColorRange::Master), 123) == 1,
               "hsv.band-outside");
        HueBand band = Adjust::DefaultBand(ColorRange::Greens);
        Expect(!Adjust::BandSetHandle(band, 1, 200) && band == Adjust::DefaultBand(ColorRange::Greens),
               "hsv.band-refuses-crossing");
        Expect(Adjust::BandSetHandle(band, 1, 110) && band.rangeStart == 110, "hsv.band-moves-handle");
    }
}

void TestHueRangesAndSampling()
{
    // Legacy colorRangesAdjustIndependently.
    {
        DocumentSession session = RedAndBlue();
        HueSaturationSettings settings = Adjust::HsvSettings(60, 0, 0, false, ColorRange::Reds);
        settings.adjustments[static_cast<std::size_t>(ColorRange::Blues)] = RangeAdjustment{0, -100, 0};
        ApplyHue(session, settings);
        const std::array<int, 4> blue = ActiveStraight(session, 30, 5);
        Expect(Near4(ActiveStraight(session, 5, 5), {255, 255, 0, 255}) && std::abs(blue[0] - blue[1]) <= 1 &&
                   std::abs(blue[1] - blue[2]) <= 1,
               "hsv.ranges-independent");
    }
    // Legacy rangesLeaveOtherHuesAloneAndInvertFlipsTheBand.
    {
        DocumentSession session = RedAndBlue();
        const std::array<int, 4> before = ActiveStraight(session, 30, 5);
        ApplyHue(session, Adjust::HsvSettings(0, 0, -100, false, ColorRange::Reds));
        Expect(Near4(ActiveStraight(session, 5, 5), {0, 0, 0, 255}) && Near4(ActiveStraight(session, 30, 5), before),
               "hsv.range-leaves-others");
        session.Undo();
        HueSaturationSettings inverted = Adjust::HsvSettings(0, 0, -100, false, ColorRange::Reds);
        inverted.invertRange = true;
        ApplyHue(session, inverted);
        Expect(Near4(ActiveStraight(session, 5, 5), {255, 0, 0, 255}) &&
                   Near4(ActiveStraight(session, 30, 5), {0, 0, 0, 255}),
               "hsv.invert-range");
    }
    // Legacy slidersEditTheSelectedRangeAndTheAfterBarFollowsHueShifts.
    {
        HueSaturationSettings settings;
        Adjust::RangeOf(settings).hue = 30; // Master.
        settings.range = ColorRange::Greens;
        Expect(Adjust::RangeOf(settings).hue == 0, "hsv.greens-start-untouched");
        Adjust::RangeOf(settings).hue = -40;
        Expect(settings.adjustments[static_cast<std::size_t>(ColorRange::Master)].hue == 30 &&
                   settings.adjustments[static_cast<std::size_t>(ColorRange::Greens)].hue == -40,
               "hsv.sliders-edit-selected-range");
        settings.range = ColorRange::Master;
        Expect(Adjust::RangeOf(settings).hue == 30 && !Adjust::HsvIsIdentity(settings), "hsv.master-keeps-its-value");
        HueSaturationSettings greensOnly = Adjust::HsvSettings(60, 0, 0, false, ColorRange::Greens);
        Expect(std::fabs(Adjust::ShiftedHue(120, greensOnly) - 180) < 0.001 &&
                   std::fabs(Adjust::ShiftedHue(0, greensOnly)) < 0.001,
               "hsv.after-bar");
    }
    // Legacy eyedroppersRecenterWidenAndNarrowTheBand.
    {
        DocumentSession session = RedAndBlue();
        const Raster composite = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::HueSaturation);
        session.UpdateHueSaturation(Adjust::HsvSettings(10, 0, 0, false, ColorRange::Greens), false);
        session.SetHueSampleMode(HueSampleMode::Replace);
        session.SampleHueRange(composite, Vec2{5, 5});
        HueBand band = Adjust::BandOf(session.PendingColorEdit()->settings.hsv);
        Expect(Adjust::BandWeight(band, 0) == 1 && Adjust::BandWeight(band, 120) == 0, "hsv.eyedropper-recenters");
        session.SetHueSampleMode(HueSampleMode::Add);
        session.SampleHueRange(composite, Vec2{30, 5});
        band = Adjust::BandOf(session.PendingColorEdit()->settings.hsv);
        Expect(Adjust::BandWeight(band, 240) == 1 && Adjust::BandWeight(band, 0) == 1, "hsv.eyedropper-widens");
        session.SetHueSampleMode(HueSampleMode::Remove);
        session.SampleHueRange(composite, Vec2{30, 5});
        band = Adjust::BandOf(session.PendingColorEdit()->settings.hsv);
        Expect(Adjust::BandWeight(band, 240) == 0, "hsv.eyedropper-narrows");
        session.CancelColorEdit();
        Expect(!session.HasColorEdit(FilterKind::HueSaturation), "hsv.eyedropper-cancel");
    }
    // Legacy targetedAdjustmentPicksTheRangeUnderTheCursor.
    {
        DocumentSession session = RedAndBlue();
        const Raster composite = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::HueSaturation);
        session.SetHueTargeting(true);
        Expect(session.BeginHueTargeting(composite, Vec2{30, 5}) &&
                   session.PendingColorEdit()->settings.hsv.range == ColorRange::Blues,
               "hsv.targeting-picks-blues");
        session.DragHueTargeting(60, false);
        Expect(session.PendingColorEdit()
                       ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 30,
               "hsv.targeting-drag");
        session.DragHueTargeting(20, false);
        Expect(session.PendingColorEdit()
                       ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 10,
               "hsv.targeting-never-accumulates");
        session.DragHueTargeting(-40, true);
        Expect(session.PendingColorEdit()->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)].hue ==
                   -20,
               "hsv.targeting-hue");
        session.EndHueTargeting();
        session.CancelColorEdit();
        Expect(!session.HasColorEdit(FilterKind::HueSaturation), "hsv.targeting-cancel");
    }
    // Legacy samplingNeedsAColorRangeAndAColorfulPixel.
    {
        DocumentSession session = RedAndBlue();
        const Raster composite = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::HueSaturation);
        const HueBand untouched = Adjust::BandOf(session.PendingColorEdit()->settings.hsv);
        session.SetHueSampleMode(HueSampleMode::Replace);
        session.SampleHueRange(composite, Vec2{5, 5});
        Expect(Adjust::BandOf(session.PendingColorEdit()->settings.hsv) == untouched, "hsv.sampling-needs-a-range");
        DocumentSession gray = HueSession();
        const Raster grayComposite = gray.ActiveLayer()->asset->image;
        gray.BeginColorEdit(FilterKind::HueSaturation);
        gray.UpdateHueSaturation(Adjust::HsvSettings(0, 0, 0, false, ColorRange::Reds), false);
        const HueBand before = Adjust::BandOf(gray.PendingColorEdit()->settings.hsv);
        gray.SetHueSampleMode(HueSampleMode::Replace);
        gray.SampleHueRange(grayComposite, Vec2{30, 5});
        Expect(Adjust::BandOf(gray.PendingColorEdit()->settings.hsv) == before, "hsv.sampling-needs-a-colorful-pixel");
    }
}

void TestImageAdjustments()
{
    const auto exposed = [](const Raster& image, const ExposureSettings& settings)
    {
        LayerAdjustment adjustment = Adjustment(AdjustmentKind::Exposure);
        adjustment.exposureSettings = settings;
        return Adjust::ApplyAdjustment(adjustment, image);
    };
    const auto mapped = [](const Raster& image, const GradientMapSettings& settings)
    {
        LayerAdjustment adjustment = Adjustment(AdjustmentKind::GradientMap);
        adjustment.gradientMapSettings = settings;
        return Adjust::ApplyAdjustment(adjustment, image);
    };
    const auto grained = [](const Raster& image, const GrainSettings& settings, double originX = 0, double originY = 0)
    {
        LayerAdjustment adjustment = Adjustment(AdjustmentKind::Grain);
        adjustment.grainSettings = settings;
        return Adjust::ApplyAdjustment(adjustment, image, originX, originY, 1);
    };
    const Raster gray = SolidStraight(4, 4, 128.0 / 255, 128.0 / 255, 128.0 / 255);
    // Legacy exposureWorksInLinearLightWithOffsetAndGamma.
    {
        Expect(Bytes(exposed(gray, ExposureSettings{})) == Bytes(gray), "adjust.exposure-defaults");
        const std::array<int, 4> brighter = StraightOf(exposed(gray, ExposureSettings{1, 0, 1}), 0, 0);
        Expect(std::abs(brighter[0] - 176) <= 2 && brighter[0] == brighter[2], "adjust.exposure-stop");
        const std::array<int, 4> lifted = StraightOf(exposed(gray, ExposureSettings{0, 0, 2}), 0, 0);
        Expect(std::abs(lifted[0] - 181) <= 2, "adjust.exposure-gamma");
        const std::array<int, 4> offset =
            StraightOf(exposed(SolidStraight(4, 4, 0, 0, 0), ExposureSettings{0, 0.1, 1}), 0, 0);
        Expect(std::abs(offset[0] - 89) <= 2, "adjust.exposure-offset");
        const Raster translucent = SolidStraight(4, 4, 128.0 / 255, 128.0 / 255, 128.0 / 255, 0.5);
        Expect(StraightOf(exposed(translucent, ExposureSettings{1, 0, 1}), 0, 0)[3] == StraightOf(translucent, 0, 0)[3],
               "adjust.exposure-keeps-alpha");
    }
    // Legacy gradientMapColorsByBrightnessAndReverses.
    {
        GradientMapSettings settings{AdjustmentColor{1, 0, 0}, AdjustmentColor{0, 0, 1}, false};
        Expect(StraightOf(mapped(SolidStraight(4, 4, 0, 0, 0), settings), 0, 0) == std::array<int, 4>{255, 0, 0, 255} &&
                   StraightOf(mapped(SolidStraight(4, 4, 1, 1, 1), settings), 0, 0) ==
                       std::array<int, 4>{0, 0, 255, 255},
               "adjust.gradient-map-ends");
        const std::array<int, 4> middle = StraightOf(mapped(gray, settings), 0, 0);
        Expect(std::abs(middle[0] - 127) <= 2 && std::abs(middle[2] - 128) <= 2 && middle[1] == 0,
               "adjust.gradient-map-middle");
        const std::array<int, 4> translucent = StraightOf(mapped(SolidStraight(4, 4, 1, 1, 1, 0.5), settings), 0, 0);
        Expect(translucent[2] >= 250 && translucent[0] <= 5 && std::abs(translucent[3] - 128) <= 1,
               "adjust.gradient-map-alpha");
        settings.reversed = true;
        Expect(StraightOf(mapped(SolidStraight(4, 4, 0, 0, 0), settings), 0, 0) == std::array<int, 4>{0, 0, 255, 255},
               "adjust.gradient-map-reversed");
    }
    // Legacy grainIsFixedInDocumentSpaceAndLeavesTransparencyAlone.
    {
        GrainSettings settings{60, 2, 40, 7};
        const Raster big = SolidStraight(40, 40, 128.0 / 255, 128.0 / 255, 128.0 / 255);
        const Raster whole = grained(big, settings);
        std::set<int> values;
        bool neutral = true;
        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x < 40; ++x)
            {
                const std::array<int, 4> p = StraightOf(whole, x, y);
                values.insert(p[0]);
                neutral = neutral && p[0] == p[1] && p[1] == p[2];
            }
        }
        Expect(values.size() > 5 && neutral, "adjust.grain-varies-neutrally");
        const Raster part = grained(SolidStraight(20, 20, 128.0 / 255, 128.0 / 255, 128.0 / 255), settings, 10, 10);
        bool same = true;
        for (int y = 0; y < 20; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                same = same && StraightOf(part, x, y) == StraightOf(whole, x + 10, y + 10);
            }
        }
        Expect(same, "adjust.grain-fixed-in-document-space");
        GrainSettings reseeded = settings;
        reseeded.seed = 8;
        Expect(Bytes(grained(big, reseeded)) != Bytes(whole), "adjust.grain-reseeded");
        Expect(Bytes(grained(gray, GrainSettings{0, 1.5, 50, 0})) == Bytes(gray), "adjust.grain-zero-amount");
        const Raster cleared = grained(SolidStraight(4, 4, 128.0 / 255, 128.0 / 255, 128.0 / 255, 0), settings);
        Expect(StraightOf(cleared, 1, 1)[3] == 0 && StraightOf(cleared, 3, 3)[3] == 0,
               "adjust.grain-leaves-clear-pixels");
    }
    // Legacy settingsSaveAndOlderAdjustmentsStillOpen (the JSON round trip lives in IoTests).
    {
        LayerAdjustment grain = Adjustment(AdjustmentKind::Grain);
        grain.grainSettings = GrainSettings{40, 3, 10, 9};
        Expect(grain.IsValid(), "adjust.grain-valid");
        LayerAdjustment broken = Adjustment(AdjustmentKind::Exposure);
        broken.exposureSettings = ExposureSettings{0, 0, 0};
        Expect(!broken.IsValid(), "adjust.exposure-invalid-gamma");
        LayerAdjustment legacy = Adjustment(AdjustmentKind::Hsv);
        legacy.hue = 120;
        Expect(!legacy.hsvSettings.has_value() && Adjust::RangeOf(legacy.ResolvedHsv()).hue == 120,
               "adjust.legacy-hsv-resolves");
    }
    // RF-1109: while a panel owns the document the layer opacity waits (legacy canEditAppearance), so it cannot fold
    // into the panel's open entry, and the panel's Cancel leaves it as it was.
    {
        DocumentSession session = PixelEditSession(20, 20);
        const Raster base = SolidStraight(20, 20, 128.0 / 255, 128.0 / 255, 128.0 / 255);
        InsertPixels(session, base, L"Gray");
        session.AddAdjustment(AdjustmentKind::Levels);
        const Guid id = session.ActiveLayerID().value();
        const int entries = session.history.UndoCount();
        const bool editing = session.BeginAdjustmentEditing(id, base) && session.HasColorEdit();
        session.SetOpacity(0.5);
        const double during = session.ActiveLayer()->opacity;
        session.FinishAdjustmentEditing(false);
        Expect(editing && during == 1.0 && session.ActiveLayer()->opacity == 1.0 &&
                   session.history.UndoCount() == entries,
               "appearance.opacity-waits-for-an-open-panel");
        session.SetOpacity(0.5);
        Expect(session.ActiveLayer()->opacity == 0.5 && session.history.UndoName() == L"Layer Opacity",
               "appearance.opacity-after-the-panel");
    }
    // Legacy newAdjustmentLayersStartFromThePaletteRenderAndEditInThePanel.
    {
        DocumentSession session = PixelEditSession(20, 20);
        const Raster base = SolidStraight(20, 20, 128.0 / 255, 128.0 / 255, 128.0 / 255);
        InsertPixels(session, base, L"Gray");
        session.SetForegroundColor(0xFFFF0000u);
        session.SetBackgroundColor(0xFF0000FFu);
        session.AddAdjustment(AdjustmentKind::GradientMap);
        const Guid id = session.ActiveLayerID().value();
        const std::optional<LayerAdjustment> adjustment = session.ActiveLayer()->adjustment;
        Expect(adjustment.has_value() && adjustment->kind == AdjustmentKind::GradientMap &&
                   adjustment->GradientMap().shadows == AdjustmentColor{1, 0, 0} &&
                   adjustment->GradientMap().highlights == AdjustmentColor{0, 0, 1} &&
                   session.AdjustmentEditingID() == id &&
                   session.history.UndoName() == L"New Gradient Map Adjustment" &&
                   !session.ActiveLayer()->asset.has_value(),
               "adjust.new-gradient-map-from-palette");
        Expect(session.BeginAdjustmentEditing(id, base) && session.HasColorEdit() &&
                   session.PendingColorEdit()->kind == FilterKind::GradientMap,
               "adjust.gradient-map-edits-in-the-filter-panel");
        session.FinishAdjustmentEditing(false);
        const std::array<int, 4> middle = StraightOf(Adjust::ApplyAdjustment(adjustment.value(), base), 10, 10);
        Expect(middle[0] > 100 && middle[2] > 100 && middle[1] < 20, "adjust.gradient-map-renders");
        session.AddAdjustment(AdjustmentKind::Grain);
        const std::uint32_t first = session.ActiveLayer()->adjustment->Grain().seed;
        session.SetAdjustmentEditingID(std::nullopt);
        session.AddAdjustment(AdjustmentKind::Grain);
        int grains = 0;
        for (const ImageLayer& layer : session.Document().value().layers)
        {
            grains += layer.adjustment.has_value() && layer.adjustment->kind == AdjustmentKind::Grain ? 1 : 0;
        }
        Expect(grains == 2 && session.ActiveLayer()->adjustment->Grain().seed != first,
               "adjust.grain-layers-get-their-own-pattern");
        // Every adjustment layer kind edits in the panel of its Image menu command, and back.
        bool paired = true;
        for (const AdjustmentKind kind : {AdjustmentKind::Hsv, AdjustmentKind::Levels, AdjustmentKind::Curves,
                                          AdjustmentKind::Exposure, AdjustmentKind::GradientMap, AdjustmentKind::Grain})
        {
            paired = paired && FilterAdjustmentKind(AdjustmentFilterKind(kind)) == kind;
        }
        Expect(paired && AdjustmentFilterKind(AdjustmentKind::Exposure) == FilterKind::Exposure &&
                   AdjustmentFilterKind(AdjustmentKind::Levels) == FilterKind::Levels &&
                   AdjustmentFilterKind(AdjustmentKind::Hsv) == FilterKind::HueSaturation &&
                   !FilterAdjustmentKind(FilterKind::GaussianBlur).has_value(),
               "adjust.filter-kinds");
    }
    // Legacy imageMenuExposureChangesTheLayerInOneStep.
    {
        DocumentSession session = PixelEditSession(8, 8);
        InsertPixels(session, SolidStraight(8, 8, 128.0 / 255, 128.0 / 255, 128.0 / 255), L"Gray");
        session.BeginColorEdit(FilterKind::Exposure);
        Expect(session.HasColorEdit(), "adjust.exposure-panel-opens");
        FilterSettings settings = session.PendingColorEdit()->settings;
        settings.exposure.exposure = 1;
        session.UpdateColorEdit(settings, true);
        const int count = session.history.UndoCount();
        session.CommitColorEdit();
        Expect(session.history.UndoCount() == count + 1 && session.history.UndoName() == L"Exposure" &&
                   std::abs(ActiveStraight(session, 0, 0)[0] - 176) <= 2,
               "adjust.exposure-one-step");
        Expect(FilterIsImageAdjustment(FilterKind::Exposure) && !FilterIsImageAdjustment(FilterKind::GaussianBlur),
               "adjust.image-adjustment-kinds");
    }
}

void TestFilters()
{
    // Legacy gaussianBlurSoftensAHardEdgeWithoutFadingTheBordersAsOneUndoStep: the blur spreads into room grown
    // around the layer, so the layer's border softens too and the pixels are trimmed back to what is there.
    {
        DocumentSession session = PixelEditSession(40, 20);
        const Guid id = InsertPixels(session,
                                     BytesRaster(40, 20,
                                                 [](int x, int) -> std::array<std::uint8_t, 4>
                                                 {
                                                     return x < 20 ? std::array<std::uint8_t, 4>{255, 255, 255, 255}
                                                                   : std::array<std::uint8_t, 4>{0, 0, 0, 0};
                                                 }),
                                     L"Half");
        session.BeginColorEdit(FilterKind::GaussianBlur);
        Expect(session.HasColorEdit() && !session.CanPaint(), "filter.blur-opens");
        FilterSettings settings;
        settings.radius = 3;
        session.UpdateColorEdit(settings, true);
        Expect(session.PendingColorEdit()->grownImage.HasStorage() &&
                   session.PendingColorEdit()->previewRaster.HasStorage(),
               "filter.blur-grows-and-previews");
        const int count = session.history.UndoCount();
        session.CommitColorEdit();
        Expect(!session.HasColorEdit() && session.history.UndoCount() == count + 1 &&
                   session.history.UndoName() == L"Gaussian Blur" && session.filterSettings.radius == 3,
               "filter.blur-one-step");
        const ImageLayer* layer = Find(session.Document(), id);
        Expect(layer != nullptr && layer->transform.origin.x < 0 && LayerPixelOf(session, id, 10, 10)[3] == 255,
               "filter.blur-spreads-into-grown-room");
        const int border = LayerPixelOf(session, id, 0, 10)[3];
        const int edge = LayerPixelOf(session, id, 20, 10)[3];
        Expect(border > 60 && border < 200 && edge > 20 && edge < 235 && LayerPixelOf(session, id, 38, 10)[3] == 0,
               "filter.blur-softens-edges");
    }
    // Legacy motionBlurStreaksAlongItsAngleCounterclockwiseFromHorizontal.
    {
        const Raster dot = BytesRaster(41, 41,
                                       [](int x, int y) -> std::array<std::uint8_t, 4>
                                       {
                                           return x == 20 && y == 20 ? std::array<std::uint8_t, 4>{255, 255, 255, 255}
                                                                     : std::array<std::uint8_t, 4>{0, 0, 0, 0};
                                       });
        const auto streak = [&](double angle)
        {
            FilterSettings settings;
            settings.angle = angle;
            settings.distance = 16;
            return Adjust::RunFilter(FilterKind::MotionBlur, dot, settings, 1, 0);
        };
        const auto alpha = [](const Raster& image, int x, int y) { return StraightOf(image, x, y)[3]; };
        const Raster horizontal = streak(0);
        Expect(alpha(horizontal, 24, 20) > 0 && alpha(horizontal, 16, 20) > 0 && alpha(horizontal, 20, 24) == 0,
               "filter.motion-horizontal");
        const Raster vertical = streak(90);
        Expect(alpha(vertical, 20, 24) > 0 && alpha(vertical, 20, 16) > 0 && alpha(vertical, 24, 20) == 0,
               "filter.motion-vertical");
        const Raster diagonal = streak(45);
        Expect(alpha(diagonal, 23, 17) > 0 && alpha(diagonal, 17, 23) > 0 && alpha(diagonal, 17, 17) == 0,
               "filter.motion-diagonal");
    }
    // Legacy addNoiseChangesColorButNeverAlphaAndMonochromaticKeepsGrays.
    {
        const Raster gray = BytesRaster(32, 8,
                                        [](int x, int) -> std::array<std::uint8_t, 4>
                                        {
                                            return x < 16 ? std::array<std::uint8_t, 4>{128, 128, 128, 255}
                                                          : std::array<std::uint8_t, 4>{0, 0, 0, 0};
                                        });
        const auto noisy = [&](FilterSettings settings)
        { return Bytes(Adjust::RunFilter(FilterKind::AddNoise, gray, settings, 1, 7)); };
        FilterSettings settings;
        settings.amount = 10;
        const std::vector<std::uint8_t> color = noisy(settings);
        Expect(noisy(settings) == color, "filter.noise-same-seed");
        bool opaqueOk = true;
        bool clearOk = true;
        bool differs = false;
        std::set<int> values;
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4u;
                if (x < 16)
                {
                    opaqueOk = opaqueOk && color[offset + 3] == 255 && color[offset] >= 112 && color[offset] <= 144;
                    values.insert(color[offset]);
                    differs = differs || color[offset] != color[offset + 1];
                }
                else
                {
                    clearOk = clearOk && color[offset] == 0 && color[offset + 3] == 0;
                }
            }
        }
        Expect(opaqueOk && values.size() > 5 && differs && clearOk, "filter.noise-colors-not-alpha");
        settings.gaussian = true;
        settings.monochromatic = true;
        const std::vector<std::uint8_t> mono = noisy(settings);
        bool monoOk = true;
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4u;
                monoOk = monoOk && mono[offset] == mono[offset + 1] && mono[offset + 1] == mono[offset + 2] &&
                         mono[offset + 3] == 255;
            }
        }
        Expect(monoOk, "filter.noise-monochromatic");
    }
    // Legacy removeDistortionBendsAboutTheCenterAndOnlyPincushionCorrectionOpensTheCorners.
    {
        const Raster source = BytesRaster(40, 30,
                                          [](int x, int y) -> std::array<std::uint8_t, 4>
                                          {
                                              const int index = (x < 20 ? 0 : 1) + (y < 15 ? 0 : 2);
                                              return {static_cast<std::uint8_t>(255 - index * 85), 128,
                                                      static_cast<std::uint8_t>(index * 85), 255};
                                          });
        const auto distorted = [&](double distortion)
        {
            FilterSettings settings;
            settings.distortion = distortion;
            return Adjust::RunFilter(FilterKind::LensCorrection, source, settings, 1, 0);
        };
        const auto alpha = [](const Raster& image, int x, int y) { return StraightOf(image, x, y)[3]; };
        const Raster original = distorted(0);
        Expect(Bytes(original) == Bytes(source), "filter.lens-zero-copies");
        const Raster barrel = distorted(100);
        Expect(alpha(barrel, 0, 0) == 255 && alpha(barrel, 39, 29) == 255, "filter.lens-barrel-keeps-corners");
        const Raster pincushion = distorted(-100);
        Expect(alpha(pincushion, 0, 0) == 0 && alpha(pincushion, 39, 29) == 0 &&
                   StraightOf(pincushion, 20, 15) == StraightOf(source, 20, 15),
               "filter.lens-pincushion-opens-corners");
    }
}

// Legacy sharedEditorsKeepPixelsDynamicAndSupportCancel. Each kind is a separate fixture so the baseline
// test image needs no indirect enum dispatch across calls that mutate the session.
template <AdjustmentKind kind> void TestAdjustmentLayerSharedEditor()
{
    DocumentSession session = PixelEditSession(2, 2);
    const Raster white = SolidStraight(2, 2, 1, 1, 1);
    const Guid baseID = InsertPixels(session, white, L"White");
    session.AddAdjustment(kind);
    const std::optional<Guid> id = session.AdjustmentEditingID();
    Expect(id.has_value() && session.ActiveLayer()->adjustment.has_value(), "adjustlayer.editor-opens");
    const LayerAdjustment created = session.ActiveLayer()->adjustment.value();
    Expect(session.BeginAdjustmentEditing(id.value(), white) && session.AdjustmentOriginal().has_value() &&
               session.AdjustmentOriginal()->kind == kind,
           "adjustlayer.editor-begins");
    if constexpr (kind == AdjustmentKind::Levels)
    {
        Expect(session.HasColorEdit(FilterKind::Levels) && session.PendingColorEdit()->histogramReady &&
                   session.PendingColorEdit()->histogram[0][255] == 4,
               "adjustlayer.levels-histogram");
        LevelsSettings settings = session.PendingColorEdit()->settings.levels;
        settings.ranges[0].outputWhite = 0;
        session.UpdateLevels(settings, true);
        Expect(session.ActiveLayer()->adjustment->levels == settings, "adjustlayer.levels-live");
        session.UpdateLevels(settings, false);
        Expect(session.ActiveLayer()->adjustment == Adjustment(kind), "adjustlayer.levels-preview-off-restores");
        session.CommitColorEdit();
    }
    else if constexpr (kind == AdjustmentKind::Curves)
    {
        FilterSettings settings = session.PendingColorEdit()->settings;
        settings.curves.channels[0] = {CurvePoint{0, 0}, CurvePoint{255, 0}};
        session.UpdateColorEdit(settings, true);
        Expect(session.ActiveLayer()->adjustment->curves == settings.curves, "adjustlayer.curves-live");
        session.CommitColorEdit();
    }
    else if constexpr (kind == AdjustmentKind::Exposure || kind == AdjustmentKind::GradientMap ||
                       kind == AdjustmentKind::Grain)
    {
        Expect(session.HasColorEdit() && session.PendingColorEdit()->kind == AdjustmentFilterKind(kind),
               "adjustlayer.filter-kind");
        FilterSettings settings = session.PendingColorEdit()->settings;
        if (kind == AdjustmentKind::Exposure)
        {
            settings.exposure.exposure = 1;
        }
        else if (kind == AdjustmentKind::GradientMap)
        {
            settings.gradientMap.reversed = true;
        }
        else
        {
            settings.grain.amount = 70;
        }
        session.UpdateColorEdit(settings, true);
        const LayerAdjustment live = session.ActiveLayer()->adjustment.value();
        Expect(live.Exposure() == settings.exposure && live.GradientMap() == settings.gradientMap &&
                   live.Grain() == settings.grain,
               "adjustlayer.filter-live");
        session.CommitColorEdit();
    }
    else if constexpr (kind == AdjustmentKind::Hsv)
    {
        HueSaturationSettings settings = session.PendingColorEdit()->settings.hsv;
        settings.range = ColorRange::Reds;
        Adjust::RangeOf(settings).hue = 80;
        Adjust::BandOf(settings) = Adjust::BandCentered(Adjust::BandOf(settings), 25);
        settings.invertRange = true;
        session.UpdateHueSaturation(settings, true);
        Expect(session.ActiveLayer()->adjustment->ResolvedHsv() == settings, "adjustlayer.hsv-live");
        session.CommitColorEdit();
    }
    const LayerAdjustment saved = session.ActiveLayer()->adjustment.value();
    Expect(!session.AdjustmentEditingID().has_value() && !session.HasColorEdit() &&
               Find(session.Document(), baseID)->asset->image == white && !session.ActiveLayer()->asset.has_value(),
           "adjustlayer.commit-keeps-pixels-dynamic");
    session.SetAdjustmentEditingID(id);
    Expect(session.BeginAdjustmentEditing(id.value(), white), "adjustlayer.reopens");
    if constexpr (kind == AdjustmentKind::Levels)
    {
        Expect(session.PendingColorEdit()->settings.levels == saved.levels, "adjustlayer.reopened-levels");
        session.UpdateLevels(LevelsSettings{}, true);
        session.CancelColorEdit();
    }
    else if constexpr (kind == AdjustmentKind::Curves || kind == AdjustmentKind::Exposure ||
                       kind == AdjustmentKind::GradientMap || kind == AdjustmentKind::Grain)
    {
        const FilterSettings reopened = session.PendingColorEdit()->settings;
        Expect(reopened.curves == saved.curves && reopened.exposure == saved.Exposure() &&
                   reopened.gradientMap == saved.GradientMap() && reopened.grain == saved.Grain(),
               "adjustlayer.reopened-filter");
        session.UpdateColorEdit(FilterSettings{}, true);
        session.CancelColorEdit();
    }
    else if constexpr (kind == AdjustmentKind::Hsv)
    {
        Expect(session.PendingColorEdit()->settings.hsv == saved.ResolvedHsv(), "adjustlayer.reopened-hsv");
        session.UpdateHueSaturation(HueSaturationSettings{}, true);
        session.CancelColorEdit();
    }
    Expect(session.ActiveLayer()->adjustment == saved && !session.AdjustmentEditingID().has_value(),
           "adjustlayer.cancel-restores");
    session.Undo();
    Expect(session.ActiveLayer()->adjustment == created, "adjustlayer.undo-to-created");
    session.Redo();
    Expect(session.ActiveLayer()->adjustment == saved, "adjustlayer.redo-to-saved");
}

void TestAdjustmentLayers()
{
    // Legacy adjustmentPersistsDuplicatesAndUndoRestoresSettings (the file round trip lives in IoTests).
    {
        DocumentSession session = PixelEditSession(2, 2);
        InsertPixels(session, SolidStraight(2, 2, 1, 1, 1), L"White");
        session.AddAdjustment(AdjustmentKind::Curves);
        session.SetAdjustmentEditingID(std::nullopt);
        const Guid id = session.ActiveLayerID().value();
        LayerAdjustment value = Adjustment(AdjustmentKind::Curves);
        value.curves.channels[0].insert(value.curves.channels[0].begin() + 1, CurvePoint{128, 190});
        session.BeginEdit(L"Edit Curves");
        Expect(session.UpdateAdjustment(id, value), "adjustlayer.update");
        session.EndEdit();
        session.Undo();
        Expect(session.ActiveLayer()->adjustment->curves == CurvesSettings{}, "adjustlayer.undo-restores-settings");
        session.Redo();
        Expect(session.ActiveLayer()->adjustment == value, "adjustlayer.redo");
        session.DuplicateActiveLayer();
        Expect(session.ActiveLayer()->adjustment == value && session.Document().value().layers.size() == 4,
               "adjustlayer.duplicate");
    }
    // Legacy curvesIdentityAndImageCommandPreserveAlpha.
    {
        const Raster asset = BytesRaster(2, 2,
                                         [](int x, int y) -> std::array<std::uint8_t, 4>
                                         {
                                             const std::uint8_t alphas[] = {255, 128, 32, 0};
                                             const std::uint8_t a = alphas[y * 2 + x];
                                             return {static_cast<std::uint8_t>(std::lround(0.1 * a)),
                                                     static_cast<std::uint8_t>(std::lround(0.7 * a)),
                                                     static_cast<std::uint8_t>(std::lround(0.4 * a)), a};
                                         });
        Expect(Bytes(Adjust::ApplyAdjustment(Adjustment(AdjustmentKind::Curves), asset)) == Bytes(asset),
               "adjustlayer.curves-identity");
        DocumentSession session = PixelEditSession(2, 2);
        InsertPixels(session, asset, L"Fixture");
        session.BeginColorEdit(FilterKind::Curves);
        FilterSettings settings;
        settings.curves.channels[0] = {CurvePoint{0, 255}, CurvePoint{255, 255}};
        session.UpdateColorEdit(settings, true);
        session.CommitColorEdit();
        Expect(Bytes(session.ActiveLayer()->asset->image) ==
                   std::vector<std::uint8_t>{255, 255, 255, 255, 128, 128, 128, 128, 32, 32, 32, 32, 0, 0, 0, 0},
               "adjustlayer.curves-command-preserves-alpha");
        session.Undo();
        Expect(session.ActiveLayer()->asset->image == asset, "adjustlayer.curves-undo");
    }
    // Keep the legacy kind order and the complete shared-editor assertions for every kind.
    TestAdjustmentLayerSharedEditor<AdjustmentKind::Hsv>();
    TestAdjustmentLayerSharedEditor<AdjustmentKind::Levels>();
    TestAdjustmentLayerSharedEditor<AdjustmentKind::Curves>();
    TestAdjustmentLayerSharedEditor<AdjustmentKind::Exposure>();
    TestAdjustmentLayerSharedEditor<AdjustmentKind::GradientMap>();
    TestAdjustmentLayerSharedEditor<AdjustmentKind::Grain>();
    // Legacy shiftPlusAndMinusStepTheActiveLayersBlendModeWhereverFocusIsExceptTextFields (the session part).
    {
        DocumentSession session = PixelEditSession(40, 20);
        session.AddBlankLayer();
        session.StepBlendMode(true);
        Expect(session.ActiveLayer()->blendMode == LayerBlendMode::Multiply, "adjustlayer.blend-step-forward");
        session.StepBlendMode(false);
        session.StepBlendMode(false);
        Expect(session.ActiveLayer()->blendMode == LayerBlendMode::Luminosity, "adjustlayer.blend-step-wraps");
        session.Undo();
        Expect(session.ActiveLayer()->blendMode == LayerBlendMode::Normal, "adjustlayer.blend-step-undo");
    }
}

void TestBlends()
{
    const auto near3 = [](const std::array<double, 3>& a, const std::array<double, 3>& b)
    { return std::fabs(a[0] - b[0]) < 0.002 && std::fabs(a[1] - b[1]) < 0.002 && std::fabs(a[2] - b[2]) < 0.002; };
    const std::array<double, 3> red{1, 0, 0};
    const std::array<double, 3> gray{0.5, 0.5, 0.5};
    const std::array<double, 3> blue{0, 0, 1};
    // The PDF non-separable modes: Color keeps the backdrop's luminosity, Luminosity its color, Hue takes the
    // source hue at the backdrop's saturation, Saturation the source saturation on the backdrop's hue.
    Expect(near3(Adjust::BlendStraight(gray, red, LayerBlendMode::Color), {1.0, 2.0 / 7, 2.0 / 7}), "blend.color");
    Expect(near3(Adjust::BlendStraight(red, gray, LayerBlendMode::Luminosity), {1.0, 2.0 / 7, 2.0 / 7}),
           "blend.luminosity");
    Expect(near3(Adjust::BlendStraight(gray, blue, LayerBlendMode::Hue), gray), "blend.hue-of-gray-stays-gray");
    Expect(near3(Adjust::BlendStraight(red, gray, LayerBlendMode::Saturation), {0.3, 0.3, 0.3}), "blend.saturation");
    Expect(near3(Adjust::BlendStraight(gray, gray, LayerBlendMode::Multiply), {0.25, 0.25, 0.25}) &&
               near3(Adjust::BlendStraight(gray, red, LayerBlendMode::Normal), red),
           "blend.separable");
}

// ── Content-Aware Fill (C13) ──────────────────────────────────

// A 64×48 flat (0.2, 0.6, 0.8) layer with a red block, the block selected.
DocumentSession SmartFixture()
{
    DocumentSession session = PixelEditSession(64, 48);
    InsertPixels(session,
                 BytesRaster(64, 48,
                             [](int x, int y) -> std::array<std::uint8_t, 4>
                             {
                                 if (x >= 20 && x < 32 && y >= 16 && y < 26)
                                 {
                                     return {0, 0, 255, 255};
                                 }
                                 return {204, 153, 51, 255};
                             }),
                 L"Object");
    SelectRect(session, 20, 16, 12, 10, false);
    return session;
}

void TestSmartEdits()
{
    // Legacy fillContinuesRepeatingTexture: four-pixel stripes continue through the filled block.
    {
        DocumentSession session = PixelEditSession(80, 64);
        InsertPixels(session,
                     BytesRaster(80, 64,
                                 [](int x, int y) -> std::array<std::uint8_t, 4>
                                 {
                                     if (x >= 32 && x < 44 && y >= 26 && y < 36)
                                     {
                                         return {0, 0, 255, 255};
                                     }
                                     const std::uint8_t value = (x / 4) % 2 == 0 ? 51 : 204;
                                     return {value, value, value, 255};
                                 }),
                     L"Stripes");
        SelectRect(session, 32, 26, 12, 10, false);
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        Expect(session.HasColorEdit() && session.PendingColorEdit()->kind == FilterKind::ContentAwareFill &&
                   !session.PendingColorEdit()->previewFailed,
               "smart.fill-opens");
        session.CommitColorEdit();
        int matching = 0;
        for (int y = 26; y < 36; ++y)
        {
            for (int x = 32; x < 44; ++x)
            {
                const int expected = (x / 4) % 2 == 0 ? 51 : 204;
                matching += std::abs(ActivePixel(session, x, y)[0] - expected) <= 1 ? 1 : 0;
            }
        }
        Expect(!session.HasColorEdit() && session.history.UndoName() == L"Content-Aware Fill" && matching >= 114,
               "smart.fill-continues-texture");
    }
    // Legacy fillReconstructsBackgroundInsideSelectionAndUndoes.
    {
        DocumentSession session = SmartFixture();
        const Raster original = session.ActiveLayer()->asset->image;
        const int count = session.history.UndoCount();
        Expect(session.CanContentAwareFill(), "smart.can-fill");
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        session.CommitColorEdit();
        Expect(!session.HasColorEdit() && session.history.UndoCount() == count + 1, "smart.fill-one-entry");
        bool flat = true;
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                const std::array<std::uint8_t, 4> p = ActivePixel(session, x, y);
                flat = flat && std::abs(p[0] - 204) <= 1 && std::abs(p[1] - 153) <= 1 && std::abs(p[2] - 51) <= 1 &&
                       p[3] == 255;
            }
        }
        Expect(flat, "smart.fill-reconstructs-background");
        session.Undo();
        Expect(session.ActiveLayer()->asset->image == original, "smart.fill-undo");
    }
    // Legacy cancelAndNoSourceLeaveOriginalUntouched.
    {
        DocumentSession session = SmartFixture();
        const Raster original = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        session.CancelColorEdit();
        Expect(session.ActiveLayer()->asset->image == original && !session.HasColorEdit(), "smart.cancel");
        session.SelectAll();
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        Expect(session.HasColorEdit() && session.PendingColorEdit()->previewFailed, "smart.no-source-reports");
        session.CommitColorEdit();
        Expect(session.HasColorEdit() && session.ActiveLayer()->asset->image == original, "smart.no-source-refuses");
        session.CancelColorEdit();
        session.Deselect();
        Expect(!session.CanContentAwareFill(), "smart.needs-a-selection");
    }
    // A selection past the layer's edge grows the grid; the kernel refuses when no opaque pixel surrounds it.
    {
        DocumentSession session = PixelEditSession(64, 48);
        InsertPixels(session, SolidStraight(40, 30, 0.2, 0.6, 0.8), L"Small");
        SelectRect(session, 30, 20, 20, 16, false);
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        Expect(session.HasColorEdit() && session.PendingColorEdit()->grownImage.HasStorage() &&
                   session.PendingColorEdit()->grownImage.Width() == 50 &&
                   session.PendingColorEdit()->grownImage.Height() == 36,
               "smart.grows-to-the-selection");
        session.CommitColorEdit();
        Expect(!session.HasColorEdit() && session.ActiveLayer()->asset->image.Width() == 50 &&
                   ActivePixel(session, 45, 30)[3] == 255,
               "smart.fills-past-the-edge");
    }
}

void TestLayerOrdering()
{
    // Legacy LayerTests.reorderTranslatesVisibleOrderAndKeepsSelection: Move Layer Up / Down swap siblings.
    DocumentSession session;
    session.CreateDocument(800, 600);
    for (int i = 0; i < 3; ++i)
    {
        session.AddBlankLayer();
    }
    const auto names = [&]()
    {
        std::vector<std::wstring> result;
        for (const ImageLayer& layer : session.Document().value().layers)
        {
            result.push_back(layer.name);
        }
        return result;
    };
    const Guid top = session.ActiveLayerID().value();
    Expect(!session.CanMoveActiveLayer(1) && session.CanMoveActiveLayer(-1), "order.top-moves-down-only");
    session.MoveActiveLayer(1);
    Expect(names() == std::vector<std::wstring>{L"Layer 1", L"Layer 2", L"Layer 3"},
           "order.refused-move-changes-nothing");
    session.MoveActiveLayer(-1);
    Expect(names() == std::vector<std::wstring>{L"Layer 1", L"Layer 3", L"Layer 2"} && session.ActiveLayerID() == top &&
               session.history.UndoName() == L"Reorder Layers",
           "order.move-down-swaps");
    session.MoveActiveLayer(-1);
    Expect(names() == std::vector<std::wstring>{L"Layer 3", L"Layer 1", L"Layer 2"} && !session.CanMoveActiveLayer(-1),
           "order.bottom-stops");
    session.MoveActiveLayer(1);
    Expect(names() == std::vector<std::wstring>{L"Layer 1", L"Layer 3", L"Layer 2"}, "order.move-up-swaps");
    session.Undo();
    session.Undo();
    session.Undo();
    Expect(names() == std::vector<std::wstring>{L"Layer 1", L"Layer 2", L"Layer 3"}, "order.undo-restores");
    // Siblings only: a layer alone in its folder cannot move past the folder's contents.
    session.SelectLayer(std::nullopt);
    session.AddGroup();
    session.AddBlankLayer();
    Expect(!session.CanMoveActiveLayer(1) && !session.CanMoveActiveLayer(-1), "order.folder-child-alone");
    session.SelectLayer(std::nullopt);
    Expect(!session.CanMoveActiveLayer(1), "order.no-active-layer");
}

void TestGroupSelection()
{
    // Legacy GroupingSelectionTests.singleLayerAndFolderAreWrappedRatherThanCreatingAChildFolder.
    DocumentSession session;
    session.CreateDocument(100, 100);
    session.AddBlankLayer();
    const Guid layer = session.ActiveLayerID().value();
    session.GroupSelectedLayers();
    const Guid inner = session.ActiveLayerID().value();
    const ImageLayer* wrapped = Find(session.Document(), layer);
    const ImageLayer* innerFolder = Find(session.Document(), inner);
    Expect(wrapped != nullptr && wrapped->parentID == inner && innerFolder != nullptr && innerFolder->isGroup &&
               innerFolder->name == L"Folder 1" && session.history.UndoName() == L"Group Layers",
           "wrap.single-layer");
    session.GroupSelectedLayers();
    const Guid outer = session.ActiveLayerID().value();
    innerFolder = Find(session.Document(), inner);
    wrapped = Find(session.Document(), layer);
    Expect(innerFolder != nullptr && innerFolder->parentID == outer && wrapped != nullptr &&
               wrapped->parentID == inner && session.ActiveLayer() != nullptr &&
               !session.ActiveLayer()->parentID.has_value(),
           "wrap.folder-wrapped-not-child");
    session.Undo();
    innerFolder = Find(session.Document(), inner);
    Expect(innerFolder != nullptr && !innerFolder->parentID.has_value() &&
               session.Document().value().layers.size() == 2,
           "wrap.undo");
    // Legacy itemsFromDifferentFoldersUseCommonParentAndEmptySelectionCreatesEmptyGroup: nothing selected makes
    // an empty folder at the top.
    DocumentSession empty;
    empty.CreateDocument(100, 100);
    empty.GroupSelectedLayers();
    const ImageLayer* folder = empty.ActiveLayer();
    Expect(folder != nullptr && folder->isGroup && !folder->parentID.has_value() &&
               empty.Document().value().layers.size() == 1,
           "wrap.empty-selection-makes-folder");
    const Guid folderId = folder != nullptr ? folder->id : Guid{};
    empty.AddBlankLayer(); // the layers vector may reallocate: `folder` is stale from here on
    const Guid a = empty.ActiveLayerID().value();
    const ImageLayer* inside = Find(empty.Document(), a);
    Expect(inside != nullptr && inside->parentID == folderId, "wrap.blank-layer-joins-active-folder");
    // A layer inside a folder wraps inside that folder: the wrapper's parent is the common parent.
    empty.SelectLayer(a);
    empty.GroupSelectedLayers();
    const ImageLayer* wrapper = empty.ActiveLayer();
    inside = Find(empty.Document(), a);
    Expect(wrapper != nullptr && wrapper->isGroup && wrapper->name == L"Folder 2" && inside != nullptr &&
               inside->parentID == wrapper->id && wrapper->parentID == empty.Document().value().layers[0].id,
           "wrap.inside-folder-keeps-parent");
}

void TestClippingToggle()
{
    // Legacy LiveLayerMask.toggleClippingMask: Ctrl+Alt+G clips to the sibling below, sharing its base.
    DocumentSession session;
    session.CreateDocument(4, 4);
    session.AddBlankLayer();
    const Guid base = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid middle = session.ActiveLayerID().value();
    session.AddBlankLayer();
    const Guid top = session.ActiveLayerID().value();
    Expect(!session.CanToggleClippingMask(base), "clip.bottom-has-no-base");
    Expect(session.CanToggleClippingMask(middle), "clip.can-clip-to-below");
    session.ToggleClippingMask(middle);
    const ImageLayer* clipped = Find(session.Document(), middle);
    Expect(clipped != nullptr && clipped->maskSourceID == base && session.history.UndoName() == L"Create Clipping Mask",
           "clip.clips-to-below");
    session.ToggleClippingMask(top);
    const ImageLayer* shared = Find(session.Document(), top);
    Expect(shared != nullptr && shared->maskSourceID == base, "clip.shares-clipped-base");
    session.ToggleClippingMask(middle);
    clipped = Find(session.Document(), middle);
    shared = Find(session.Document(), top);
    // RF-208 (legacy LiveLayerMask.removeLiveMask): releasing a clipped layer releases the clipped layers above it
    // that share its base.
    Expect(clipped != nullptr && !clipped->maskSourceID.has_value() && shared != nullptr &&
               !shared->maskSourceID.has_value() && session.history.UndoName() == L"Release Clipping Mask",
           "clip.release");
    session.Undo();
    clipped = Find(session.Document(), middle);
    shared = Find(session.Document(), top);
    Expect(clipped != nullptr && clipped->maskSourceID == base && shared != nullptr && shared->maskSourceID == base,
           "clip.undo-restores");
    // Legacy LiveMaskTests.optionClickCreatesSharedStackAndDragOutReleases: releasing the top layer of a shared
    // stack leaves the one below clipped; releasing the lower one releases both; a layer dragged below its base
    // stops clipping.
    DocumentSession stack;
    stack.CreateDocument(2, 2);
    std::array<Guid, 3> ids{};
    for (Guid& id : ids)
    {
        stack.AddBlankLayer();
        id = stack.ActiveLayerID().value();
    }
    const auto sourceOf = [&](std::size_t index) { return stack.Document().value().layers[index].maskSourceID; };
    Expect(!stack.CanToggleClippingMask(ids[0]) && stack.CanToggleClippingMask(ids[1]), "clip.stack-can-toggle");
    stack.ToggleClippingMask(ids[0]);
    Expect(!sourceOf(0).has_value(), "clip.stack-bottom-stays");
    stack.ToggleClippingMask(ids[1]);
    stack.ToggleClippingMask(ids[2]);
    Expect(sourceOf(1) == ids[0] && sourceOf(2) == ids[0] && stack.CanToggleClippingMask(ids[2]),
           "clip.stack-shares-the-base");
    stack.ToggleClippingMask(ids[2]);
    Expect(!sourceOf(2).has_value() && sourceOf(1) == ids[0], "clip.stack-top-release-leaves-below");
    stack.ToggleClippingMask(ids[2]);
    stack.ToggleClippingMask(ids[1]);
    Expect(!sourceOf(1).has_value() && !sourceOf(2).has_value(), "clip.stack-lower-release-releases-above");
    stack.Undo();
    Expect(sourceOf(2) == ids[0], "clip.stack-undo");
    Expect(stack.PlaceLayer(ids[2], std::nullopt, std::nullopt, true), "clip.stack-drag-out");
    Expect(stack.Document().value().layers.front().id == ids[2] && !sourceOf(0).has_value() && sourceOf(2) == ids[0],
           "clip.stack-dragged-below-releases");
    stack.Undo();
    Expect(stack.Document().value().layers.back().id == ids[2] && sourceOf(2) == ids[0], "clip.stack-drag-undo");
    session.SelectLayer(std::nullopt);
    session.AddGroup();
    const Guid folder = session.ActiveLayerID().value();
    Expect(!session.CanToggleClippingMask(folder), "clip.folder-cannot-clip");
}

void TestClearDocument()
{
    DocumentSession session;
    session.CreateDocument(10, 10, true);
    session.AddBlankLayer();
    Expect(session.history.IsModified() && session.Document().has_value(), "clear.before");
    session.ClearDocument();
    Expect(!session.Document().has_value() && !session.ActiveLayerID().has_value() && !session.history.CanUndo() &&
               !session.history.IsModified() && session.LayerRows().empty() && session.DisplayDocument() == nullptr,
           "clear.empty-state");
    session.AddBlankLayer();
    session.GroupSelectedLayers();
    session.MoveActiveLayer(1);
    Expect(!session.Document().has_value() && !session.history.CanUndo(), "clear.edits-need-a-document");
    session.CreateDocument(20, 30, true);
    Expect(session.Document().has_value() && session.Document().value().width == 20 &&
               session.Document().value().layers.size() == 1 && session.ActiveLayerID().has_value(),
           "clear.next-document");
}

namespace
{
// The legacy ImageImportTests fixture: 64 × 32, the left half opaque red, the right half transparent.
[[nodiscard]] Raster RedLeftRaster(int width, int height)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width / 2; ++x)
        {
            std::uint8_t* pixel = bytes.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
            pixel[2] = 255;
            pixel[3] = 255;
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

[[nodiscard]] std::array<std::uint8_t, 4> PixelAt(const Raster& raster, int x, int y) noexcept
{
    std::array<std::uint8_t, 4> pixel{};
    const std::uint8_t* data = raster.Data();
    if (data == nullptr || x < 0 || y < 0 || x >= raster.Width() || y >= raster.Height())
    {
        return pixel;
    }
    const std::uint8_t* source =
        data + static_cast<std::size_t>(y) * raster.Stride() + static_cast<std::size_t>(x) * 4u;
    std::copy(source, source + 4, pixel.begin());
    return pixel;
}
} // namespace

void TestSizeDrafts()
{
    // Legacy CanvasSizeTests.relativeRatioAndUnitsUseFinalDimensions.
    CanvasSizeDraft draft(1000, 500, 100);
    draft.relative = true;
    draft.locked = true;
    draft.Set(200, true);
    Expect(draft.width == 1200 && draft.height == 600, "canvasdraft.relative-locked");
    Expect(draft.Displayed(false) == 100, "canvasdraft.displayed-relative");
    draft.Set(-250, false);
    Expect(draft.width == 500 && draft.height == 250, "canvasdraft.negative-relative");
    draft.relative = false;
    draft.unit = CanvasUnit::Inches;
    draft.Set(10, true);
    Expect(draft.width == 1000 && draft.height == 500, "canvasdraft.inches");
    draft.unit = CanvasUnit::Percent;
    draft.Set(50, true);
    Expect(draft.width == 500 && draft.height == 250, "canvasdraft.percent");
    draft.unit = CanvasUnit::Centimeters;
    Expect(std::abs(draft.Displayed(true) - 12.7) < 0.001, "canvasdraft.centimeters");
    draft.unit = CanvasUnit::Pixels;
    draft.Set(0, true);
    Expect(!draft.Valid(), "canvasdraft.zero-invalid");
    // The anchor offsets (legacy CanvasSizeOptions.offset): floor keeps the extra pixel right/bottom.
    CanvasSizeOptions grow;
    grow.width = 69;
    grow.height = 37;
    grow.anchor = 4;
    Expect(grow.Offset(64, 32) == Vec2{2, 2}, "canvasdraft.center-offset-floors");
    grow.anchor = 8;
    Expect(grow.Offset(64, 32) == Vec2{5, 5}, "canvasdraft.bottom-right-offset");
    grow.contentOffset = Vec2{-8, -4};
    Expect(grow.Offset(64, 32) == Vec2{-8, -4}, "canvasdraft.explicit-offset");
    // The Image Size sheet's math: percent with the ratio locked, no resample, resolution changes.
    ImageSizeDraft image(100, 50, 72);
    image.unit = CanvasUnit::Percent;
    image.Set(200, true);
    Expect(image.width == 200 && image.height == 100, "imagedraft.percent-locked");
    image.Set(50, false);
    Expect(image.width == 50 && image.height == 25, "imagedraft.height-moves-width");
    image.SetResample(false);
    Expect(image.width == 100 && image.height == 50 && image.locked && image.unit == CanvasUnit::Inches,
           "imagedraft.no-resample-resets");
    Expect(std::abs(image.Displayed(true) - 100.0 / 72.0) < 1e-9, "imagedraft.inches-displayed");
    image.Set(2, true);
    Expect(image.width == 100 && image.resolution == 50, "imagedraft.print-size-sets-resolution");
    image.SetResample(true);
    image.SetResolution(100);
    Expect(image.width == 200 && image.height == 100 && image.resolution == 100, "imagedraft.resolution-scales-pixels");
    image.SetResolution(0);
    Expect(!image.Valid(), "imagedraft.resolution-invalid");
    image.SetResolution(300);
    image.unit = CanvasUnit::Pixels;
    image.locked = false;
    image.Set(30000, true);
    image.Set(30000, false);
    Expect(!image.Valid(), "imagedraft.megapixel-limit");
    image.SetResample(false);
    Expect(image.Valid(), "imagedraft.no-resample-ignores-megapixels");
}

void TestCanvasResize()
{
    // Legacy CanvasSizeTests.everyAnchorPreservesSourceAndTransformForExpansionAndShrink.
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 64;
    document.height = 32;
    ImageLayer layer = ImageLayer::Blank(L"Fixture", Vec2{64, 32});
    const Raster fixture = RedLeftRaster(64, 32);
    layer.asset = RasterAsset{fixture, Raster::MakeThumbnail(fixture), layer.name};
    layer.transform.rotation = 37;
    layer.transform.flipX = true;
    document.layers.push_back(layer);
    bool anchorsHold = true;
    for (const int delta : {5, -5})
    {
        for (int anchor = 0; anchor <= 8; ++anchor)
        {
            CanvasSizeOptions options;
            options.width = 64 + delta;
            options.height = 32 + delta;
            options.anchor = anchor;
            CanvasDocument result;
            if (ResizeCanvas(document.Seal(), options, result) != ResizeStatus::Ok || result.layers.size() != 1)
            {
                anchorsHold = false;
                continue;
            }
            const std::array<double, 3> expected{0.0, delta == 5 ? 2.0 : -3.0, static_cast<double>(delta)};
            const ImageLayer& output = result.layers[0];
            anchorsHold = anchorsHold && output.transform.origin.x == expected[static_cast<std::size_t>(anchor % 3)] &&
                          output.transform.origin.y == expected[static_cast<std::size_t>(anchor / 3)] &&
                          output.transform.size == layer.transform.size && output.transform.rotation == 37 &&
                          output.transform.flipX && output.id == layer.id && output.asset.has_value() &&
                          output.asset->image == fixture && result.width == 64 + delta && result.height == 32 + delta;
        }
    }
    Expect(anchorsHold, "canvas.every-anchor-keeps-source-and-transform");
    // Legacy coloredExtensionPreservesOldTransparencyAndRoundTripsWithUndo: a mixed shrink/expand colors only the
    // left/right bands; the session applies it as one Canvas Size entry.
    DocumentSession session;
    session.CreateDocument(4, 4);
    session.AddBlankLayer();
    const std::optional<CanvasDocument> before = session.Document();
    const Guid active = session.ActiveLayerID().value();
    CanvasSizeOptions colored;
    colored.width = 8;
    colored.height = 2;
    colored.fill = CanvasExtensionColor{1, 0, 0};
    CanvasDocument output;
    Expect(ResizeCanvas(before.value(), colored, output) == ResizeStatus::Ok && output.layers.size() == 2 &&
               output.layers[0].name == L"Canvas Extension" && output.layers[1].id == active,
           "canvas.extension-layer-added-below");
    const Raster& extension = output.layers[0].asset.has_value() ? output.layers[0].asset->image : Raster{};
    Expect(extension.Width() == 8 && extension.Height() == 2 && PixelAt(extension, 0, 0)[3] == 255 &&
               PixelAt(extension, 0, 0)[2] == 255 && PixelAt(extension, 3, 0)[3] == 0 &&
               PixelAt(extension, 7, 1)[3] == 255,
           "canvas.extension-keeps-old-transparency");
    session.ApplyDocumentSize(output, L"Canvas Size");
    Expect(session.Document().value().width == 8 && session.Document().value().height == 2 &&
               session.ActiveLayerID() == active && session.history.UndoName() == L"Canvas Size",
           "canvas.session-applies");
    session.Undo();
    Expect(session.Document() == before, "canvas.undo-restores");
    session.Redo();
    Expect(session.Document().value().width == 8 && session.Document().value().height == 2, "canvas.redo");
    // Legacy transparentResizeIsAllocationFreeAndShrinkDoesNotAddFill.
    CanvasDocument empty;
    empty.id = Guid::Generate();
    empty.width = 4;
    empty.height = 4;
    CanvasSizeOptions large;
    large.width = 30000;
    large.height = 30000;
    CanvasDocument largeResult;
    Expect(ResizeCanvas(empty, large, largeResult) == ResizeStatus::Ok && largeResult.layers.empty(),
           "canvas.transparent-allocation-free");
    CanvasSizeOptions small;
    small.width = 2;
    small.height = 2;
    small.fill = CanvasExtensionColor{1, 1, 1};
    CanvasDocument smallResult;
    Expect(ResizeCanvas(empty, small, smallResult) == ResizeStatus::Ok && smallResult.layers.empty(),
           "canvas.shrink-adds-no-fill");
    large.fill = CanvasExtensionColor{1, 1, 1};
    Expect(ResizeCanvas(empty, large, largeResult) == ResizeStatus::TooLarge, "canvas.fill-over-budget");
    CanvasSizeOptions same;
    same.width = 4;
    same.height = 4;
    Expect(ResizeCanvas(empty, same, smallResult) == ResizeStatus::Unchanged, "canvas.same-size-unchanged");
    same.contentOffset = Vec2{1, 0};
    Expect(ResizeCanvas(empty, same, smallResult) == ResizeStatus::Ok && smallResult.width == 4,
           "canvas.offset-alone-resizes");
}

void TestImageResize()
{
    // Legacy ImageSizeTests.resizePreservesLayerIdentityAndUndoRestoresSource.
    DocumentSession session;
    session.CreateDocument(64, 32);
    ImageLayer layer = ImageLayer::Blank(L"Fixture", Vec2{64, 32});
    const Raster fixture = RedLeftRaster(64, 32);
    layer.asset = RasterAsset{fixture, Raster::MakeThumbnail(fixture), layer.name};
    session.BeginEdit(L"Import Image");
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
    session.EndEdit();
    session.SelectLayer(layer.id);
    const std::optional<CanvasDocument> original = session.Document();
    ImageSizeOptions options;
    options.width = 128;
    options.height = 96;
    options.resolution = 300;
    options.sampling = LayerSampling::Nearest;
    CanvasDocument result;
    Expect(ResizeImage(original.value(), options, result) == ResizeStatus::Ok, "imagesize.ok");
    session.ApplyDocumentSize(result, L"Image Size");
    const CanvasDocument& resized = session.Document().value();
    Expect(resized.width == 128 && resized.height == 96 && resized.resolution == 300 &&
               session.ActiveLayerID() == layer.id && session.history.UndoName() == L"Image Size",
           "imagesize.document");
    const ImageLayer* scaled = Find(session.Document(), layer.id);
    const Raster& asset = scaled != nullptr && scaled->asset.has_value() ? scaled->asset->image : Raster{};
    Expect(asset.Width() == 128 && asset.Height() == 96 && scaled != nullptr &&
               scaled->transform.origin == Vec2{0, 0} && scaled->transform.size == Vec2{128, 96} &&
               scaled->transform.sampling == LayerSampling::Nearest,
           "imagesize.layer-box");
    Expect(PixelAt(asset, 0, 0)[2] > 242 && PixelAt(asset, 0, 0)[3] == 255 && PixelAt(asset, 127, 0)[3] == 0,
           "imagesize.nearest-pixels");
    session.Undo();
    const ImageLayer* restored = Find(session.Document(), layer.id);
    Expect(session.Document() == original && restored != nullptr && restored->asset.has_value() &&
               restored->asset->image == fixture,
           "imagesize.undo-restores-source");
    session.Redo();
    Expect(session.Document().value().resolution == 300, "imagesize.redo");
    // Legacy resolutionOnlyRetainsPixelsAndSurvivesSaveAndExport: the same size changes only the resolution.
    DocumentSession plain;
    plain.CreateDocument(32, 16);
    plain.AddBlankLayer();
    ImageSizeOptions resolutionOnly;
    resolutionOnly.width = 32;
    resolutionOnly.height = 16;
    resolutionOnly.resolution = 300;
    CanvasDocument same;
    Expect(ResizeImage(plain.Document().value(), resolutionOnly, same) == ResizeStatus::Ok &&
               same.layers[0].transform == plain.Document().value().layers[0].transform && same.resolution == 300,
           "imagesize.resolution-only");
    plain.ApplyDocumentSize(same, L"Image Size");
    plain.Undo();
    Expect(plain.Document().value().resolution == 72, "imagesize.resolution-undo");
    // Legacy rotatedHiddenLayerScalesInDocumentAxesAndInvalidSizeIsRejected: a 90-degree 64 × 32 layer becomes
    // 32 × 64, then scales 2× horizontally and 3× vertically.
    DocumentFixture rotated = DocumentFixture::Copy(original.value());
    rotated.layers[0].visible = false;
    rotated.layers[0].transform.origin = Vec2{-16, 4};
    rotated.layers[0].transform.rotation = 90;
    CanvasDocument turned;
    Expect(ResizeImage(rotated.Seal(), options, turned) == ResizeStatus::Ok && !turned.layers[0].visible &&
               turned.layers[0].transform.rotation == 0 && std::abs(turned.layers[0].transform.size.x - 64) <= 1 &&
               std::abs(turned.layers[0].transform.size.y - 192) <= 1 && turned.layers[0].transform.origin.y < 0,
           "imagesize.rotated-hidden-box");
    ImageSizeOptions huge;
    huge.width = 30000;
    huge.height = 30000;
    huge.resolution = 72;
    Expect(ResizeImage(rotated.Seal(), huge, turned) == ResizeStatus::TooLarge, "imagesize.rejects-megapixels");
    Expect(session.Document().value().width == 128, "imagesize.rejection-leaves-document");
    // A scaled layer with a mask on its own placement keeps the mask pixels; a mask on the layer rasterizes.
    DocumentFixture masked = DocumentFixture::Copy(original.value());
    LayerMask mask;
    std::vector<std::uint8_t> gray(64u * 32u, 255);
    mask.asset.image = Raster::Make(64, 32, RasterFormat::Gray8, gray.data(), gray.size());
    mask.asset.thumbnail = Raster::MakeThumbnail(mask.asset.image);
    masked.layers[0].mask = mask;
    CanvasDocument maskedResult;
    Expect(ResizeImage(masked.Seal(), options, maskedResult) == ResizeStatus::Ok &&
               maskedResult.layers[0].mask.has_value() && maskedResult.layers[0].mask->asset.image.Width() == 128 &&
               maskedResult.layers[0].mask->asset.image.Height() == 96 &&
               maskedResult.layers[0].mask->asset.image.Data()[0] == 255,
           "imagesize.mask-rasterizes");
    masked.layers[0].mask->placement = LayerTransform{Vec2{8, 8}, Vec2{16, 8}};
    Expect(ResizeImage(masked.Seal(), options, maskedResult) == ResizeStatus::Ok &&
               maskedResult.layers[0].mask->asset.image == mask.asset.image &&
               maskedResult.layers[0].mask->placement.has_value() &&
               maskedResult.layers[0].mask->placement->origin == Vec2{16, 24} &&
               maskedResult.layers[0].mask->placement->size == Vec2{32, 24},
           "imagesize.placed-mask-scales-placement");
}

void TestCrop()
{
    // Legacy CropTests.dragGeometrySupportsReverseRatioMoveAndEveryHandle.
    const CropRect rect = CropGeometry::Create(Vec2{100, 100}, Vec2{20, 60}, 2.0);
    Expect(rect == CropRect{20, 60, 80, 40}, "crop.create-reverse-ratio");
    const CropDrag move{Vec2{40, 70}, rect, CropDrag::Mode::Move, -1};
    Expect(move.Updated(Vec2{30, 40}, std::nullopt) == rect.OffsetBy(-10, -30), "crop.move");
    bool handlesHold = true;
    for (int index = 0; index < 8; ++index)
    {
        const Vec2 unit = LayerTransform::kHandles[static_cast<std::size_t>(index)];
        const Vec2 start{rect.MinX() + unit.x * rect.width, rect.MinY() + unit.y * rect.height};
        const CropDrag drag{start, rect, CropDrag::Mode::Resize, index};
        const CropRect next = drag.Updated(Vec2{start.x + (unit.x * 2 - 1) * 20, start.y + (unit.y * 2 - 1) * 10}, 2.0);
        handlesHold = handlesHold && CropGeometry::Valid(next) && std::abs(next.width / next.height - 2) < 0.05 &&
                      !(next == rect);
    }
    Expect(handlesHold, "crop.every-handle-keeps-ratio");
    // Legacy cropTranslatesWithoutResamplingAndUndoRestoresBounds: the layer moves, its pixels stay.
    DocumentSession session;
    session.CreateDocument(64, 32);
    ImageLayer layer = ImageLayer::Blank(L"Fixture", Vec2{64, 32});
    const Raster fixture = RedLeftRaster(64, 32);
    layer.asset = RasterAsset{fixture, Raster::MakeThumbnail(fixture), layer.name};
    session.BeginEdit(L"Import Image");
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
    session.EndEdit();
    session.SelectLayer(layer.id);
    const std::optional<CanvasDocument> before = session.Document();
    session.SetCropFrame(CropRect{8, 4, 32, 16});
    Expect(session.CommitCrop() && !session.CropFrame().has_value() &&
               session.VisibleCropRect() == CropRect{0, 0, 32, 16} && session.Document().value().width == 32 &&
               session.Document().value().height == 16 && session.history.UndoName() == L"Crop",
           "crop.commit-resizes");
    const ImageLayer* moved = Find(session.Document(), layer.id);
    Expect(moved != nullptr && moved->transform.origin == Vec2{-8, -4} && moved->asset.has_value() &&
               moved->asset->image == fixture,
           "crop.translates-without-resampling");
    session.Undo();
    Expect(session.Document() == before, "crop.undo-restores-bounds");
    session.Redo();
    Expect(session.Document().value().width == 32, "crop.redo");
    // Legacy sameSizeOffsetCropAndExpansionUseExactBounds.
    DocumentSession offset;
    offset.CreateDocument(100, 50);
    offset.AddBlankLayer();
    const Guid blank = offset.ActiveLayerID().value();
    offset.SetCropFrame(CropRect{-20, 10, 100, 50});
    Expect(offset.CommitCrop() && Find(offset.Document(), blank)->transform.origin == Vec2{20, -10},
           "crop.same-size-offset");
    offset.SetCropFrame(CropRect{-10, -10, 140, 80});
    Expect(offset.CommitCrop() && offset.Document().value().width == 140 && offset.Document().value().height == 80 &&
               Find(offset.Document(), blank)->transform.origin == Vec2{30, 0},
           "crop.expansion-exact-bounds");
    // Legacy cancellationAndViewportMappingDoNotEditDocument.
    DocumentSession cancel;
    cancel.CreateDocument(1000, 500);
    const std::optional<CanvasDocument> original = cancel.Document();
    const int entries = cancel.history.UndoCount();
    cancel.SetCropFrame(cancel.VisibleCropRect());
    Expect(cancel.CropFrame() == CropRect{0, 0, 1000, 500} && cancel.Document() == original, "crop.tool-frames-canvas");
    cancel.SetCropFrame(CropRect{25, 20, 200, 100});
    cancel.CancelCrop();
    Expect(cancel.VisibleCropRect() == CropRect{0, 0, 1000, 500} && cancel.Document() == original &&
               cancel.history.UndoCount() == entries,
           "crop.cancel-edits-nothing");
    cancel.SetCropFrame(CropRect{0, 0, 1000, 500});
    Expect(cancel.CommitCrop() && cancel.history.UndoCount() == entries && cancel.Document() == original,
           "crop.whole-canvas-commit-is-no-entry");
    // The ratio choice re-shapes the frame about its center, keeping the width.
    cancel.SetCropFrame(CropRect{100, 100, 400, 100});
    cancel.cropRatioChoice = 2;
    cancel.ChangeCropRatio();
    Expect(cancel.CropFrame() == CropRect{100, -50, 400, 400}, "crop.ratio-reshapes");
    cancel.cropRatioChoice = 1;
    Expect(cancel.CropRatio().has_value() && std::abs(cancel.CropRatio().value() - 2.0) < 1e-9, "crop.original-ratio");
    // Legacy cropEdgesSnapToNearbyEdges.
    const CropSnap snap{{0, 200, 50, 150}, {0, 100, 20, 80}, 6};
    const CropRect frame{10, 10, 60, 40};
    const CropDrag snapMove{Vec2{30, 30}, frame, CropDrag::Mode::Move, -1};
    const Vec2 movedTo{26, 34};
    Expect(snap.Apply(snapMove.Updated(movedTo, std::nullopt), snapMove, movedTo, std::nullopt) ==
               CropRect{0, 20, 60, 40},
           "crop.snap-move");
    int cornerIndex = -1;
    for (int index = 0; index < 8; ++index)
    {
        if (LayerTransform::kHandles[static_cast<std::size_t>(index)] == Vec2{1, 1})
        {
            cornerIndex = index;
        }
    }
    const CropDrag resize{Vec2{70, 50}, frame, CropDrag::Mode::Resize, cornerIndex};
    // Not "near" and "far": <windows.h>, which the failure-report routing includes, defines both as empty macros.
    const Vec2 nearCorner{146, 83};
    Expect(snap.Apply(resize.Updated(nearCorner, std::nullopt), resize, nearCorner, std::nullopt) ==
               CropRect{10, 10, 140, 70},
           "crop.snap-resize-corner");
    const Vec2 farCorner{120, 60};
    Expect(snap.Apply(resize.Updated(farCorner, std::nullopt), resize, farCorner, std::nullopt) ==
               CropRect{10, 10, 110, 50},
           "crop.snap-too-far");
    const CropRect ratioRect{10, 10, 138, 69};
    Expect(snap.Apply(ratioRect, resize, Vec2{148, 79}, 2.0) == ratioRect, "crop.snap-keeps-ratio");
    const CropDrag create{Vec2{52, 18}, CropRect{}, CropDrag::Mode::Create, -1};
    const Vec2 dragged{147, 77};
    Expect(snap.Apply(create.Updated(dragged, std::nullopt), create, dragged, std::nullopt) == CropRect{52, 18, 98, 62},
           "crop.snap-create-dragged-corner");
    // Legacy snapTargetsAreTheCanvasAndLayerBounds: a 100 × 60 layer centered in 400 × 300.
    DocumentSession targets;
    targets.CreateDocument(400, 300);
    ImageLayer red = ImageLayer::Blank(L"Red", Vec2{100, 60});
    const Raster redRaster = RedLeftRaster(100, 60);
    red.asset = RasterAsset{redRaster, Raster::MakeThumbnail(redRaster), red.name};
    red.transform.origin = Vec2{150, 120};
    targets.BeginEdit(L"Import Image");
    EditLayerRecordsForTest(targets, [&](DocumentFixture& document) { document.layers.push_back(red); });
    targets.EndEdit();
    const SnapTargets found = targets.CropSnapTargets();
    const std::set<double> xs(found.xs.begin(), found.xs.end());
    const std::set<double> ys(found.ys.begin(), found.ys.end());
    Expect(xs == std::set<double>{0, 400, 150, 250} && ys == std::set<double>{0, 300, 120, 180}, "crop.snap-targets");
    // Clearing the session drops a frame; a new document has none.
    targets.SetCropFrame(CropRect{1, 1, 10, 10});
    targets.ClearDocument();
    Expect(!targets.CropFrame().has_value() && !targets.VisibleCropRect().has_value(), "crop.cleared");
}

void TestMerge()
{
    // Merge Down: the top layer and the one beneath it in the same folder become one layer named after the lower.
    DocumentSession session;
    session.CreateDocument(4, 4);
    session.AddBlankLayer();
    const Guid bottom = session.ActiveLayerID().value();
    session.SelectLayer(bottom);
    Expect(!session.PlanMerge().has_value(), "merge.bottom-layer-has-no-plan");
    ImageLayer red = RasterLayer(2, 2, L"Red");
    ImageLayer blue = RasterLayer(2, 2, L"Blue");
    blue.transform.origin = Vec2{2, 2};
    session.BeginEdit(L"Setup");
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                document.layers.push_back(red);
                                document.layers.push_back(blue);
                            });
    session.EndEdit();
    session.AddBlankLayer();
    const Guid clipped = session.ActiveLayerID().value();
    Expect(session.LinkMask(red.id, clipped), "merge.clip-setup");
    session.SelectLayer(blue.id);
    const std::optional<DocumentSession::MergePlan> plan = session.PlanMerge();
    Expect(plan.has_value() && plan->action == L"Merge Down" && plan->name == L"Red" && plan->anchor == blue.id &&
               plan->ids == std::vector<Guid>{red.id, blue.id} && plan->removed.size() == 2 &&
               !plan->parent.has_value(),
           "merge.down-plan");
    // A composite the composition would give: red at (1,1), blue at (2,2), with transparent canvas margins.
    std::vector<std::uint8_t> bytes(4u * 4u * 4u, 0);
    const auto paint = [&](int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r)
    {
        std::uint8_t* pixel = bytes.data() + (static_cast<std::size_t>(y) * 4 + x) * 4u;
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        pixel[3] = 255;
    };
    paint(1, 1, 0, 0, 255);
    paint(2, 2, 255, 0, 0);
    const Raster composite = Raster::Make(4, 4, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    const std::optional<CanvasDocument> before = session.Document();
    DocumentSession preparedSession;
    preparedSession.ReplaceDocument(before.value(), blue.id);
    const std::optional<DocumentSession::MergePlan> preparedPlan = preparedSession.PlanMerge();
    Expect(preparedPlan.has_value(), "merge.prepared-plan-exists");
    if (!preparedPlan)
        return;
    CancelSource cancelledMerge;
    (void)cancelledMerge.Cancel();
    Expect(!DocumentSession::PrepareMerge(preparedPlan.value(), composite, cancelledMerge.Token()) &&
               preparedSession.Document() == before,
           "merge.cancelled-preparation-publishes-no-result-or-mutation");
    const std::optional<DocumentSession::PreparedMerge> prepared =
        DocumentSession::PrepareMerge(preparedPlan.value(), composite);
    Expect(prepared.has_value() && prepared->CanvasWidth() == 4 && prepared->CanvasHeight() == 4 &&
               prepared->Layer().name == L"Red" && prepared->Layer().parentID == preparedPlan->parent &&
               prepared->Layer().asset.has_value() && prepared->Layer().asset->image.Width() == 2 &&
               prepared->Layer().asset->image.Height() == 2 && prepared->Layer().transform.origin == Vec2{1, 1} &&
               !prepared->Layer().asset->thumbnail.HasStorage(),
           "merge.prepare-complete-result");
    if (!prepared)
        return;
    DocumentSession::MergePlan mismatchedPlan = preparedPlan.value();
    mismatchedPlan.action = L"Other Action";
    Expect(!preparedSession.ApplyPreparedMerge(mismatchedPlan, prepared.value()) &&
               preparedSession.history.UndoName() != L"Other Action",
           "merge.prepared-plan-mismatch-refused");
    mismatchedPlan = preparedPlan.value();
    mismatchedPlan.name = L"Other Name";
    mismatchedPlan.parent = Guid::Generate();
    Expect(!preparedSession.ApplyPreparedMerge(mismatchedPlan, prepared.value()),
           "merge.prepared-name-parent-mismatch-refused");
    DocumentSession wrongCanvas;
    wrongCanvas.CreateDocument(5, 4);
    Expect(!wrongCanvas.ApplyPreparedMerge(preparedPlan.value(), prepared.value()),
           "merge.prepared-canvas-mismatch-refused");
    DocumentSession staleSession;
    staleSession.ReplaceDocument(before.value(), blue.id);
    staleSession.DeleteLayer(red.id);
    Expect(!staleSession.ApplyPreparedMerge(preparedPlan.value(), prepared.value()),
           "merge.prepared-missing-layer-refused");
    Expect(preparedSession.ApplyPreparedMerge(preparedPlan.value(), prepared.value()), "merge.prepared-applies");
    Expect(session.MergeLayers(plan.value(), composite), "merge.down-applies");
    const auto& layers = session.Document().value().layers;
    const ImageLayer* merged = session.ActiveLayer();
    const ImageLayer* preparedMerged = preparedSession.ActiveLayer();
    // The blank clipped layer sits above the bottom one, so the merged layer lands at the top, in Blue's slot.
    Expect(layers.size() == 3 && merged != nullptr && merged->name == L"Red" && merged->id != red.id &&
               layers[2].id == merged->id && session.history.UndoName() == L"Merge Down",
           "merge.down-result-in-place");
    Expect(merged != nullptr && merged->asset.has_value() && merged->asset->image.Width() == 2 &&
               merged->transform.origin == Vec2{1, 1} && merged->transform.size == Vec2{2, 2},
           "merge.down-trimmed-to-pixels");
    const std::uint8_t* wrapperPixels =
        merged != nullptr && merged->asset.has_value() ? merged->asset->image.Data() : nullptr;
    const std::uint8_t* preparedPixels =
        preparedMerged != nullptr && preparedMerged->asset.has_value() ? preparedMerged->asset->image.Data() : nullptr;
    Expect(
        merged != nullptr && preparedMerged != nullptr && merged->name == preparedMerged->name &&
            merged->transform == preparedMerged->transform && merged->asset.has_value() &&
            preparedMerged->asset.has_value() && merged->asset->image.Width() == preparedMerged->asset->image.Width() &&
            merged->asset->image.Height() == preparedMerged->asset->image.Height() && wrapperPixels != nullptr &&
            preparedPixels != nullptr && merged->asset->image.ByteSize() == preparedMerged->asset->image.ByteSize() &&
            std::memcmp(wrapperPixels, preparedPixels, merged->asset->image.ByteSize()) == 0 &&
            session.history.UndoName() == preparedSession.history.UndoName(),
        "merge.prepared-equivalent-to-wrapper");
    const ImageLayer* clip = Find(session.Document(), clipped);
    Expect(clip != nullptr && clip->maskSourceID == merged->id, "merge.clips-follow-the-result");
    session.Undo();
    Expect(session.Document() == before, "merge.undo");
    session.Redo();
    Expect(session.Document().value().layers.size() == 3, "merge.redo");
    // Merge Group: a folder with pixel layers merges its contents in its place and goes; an empty one has no plan.
    DocumentSession group;
    group.CreateDocument(4, 4);
    group.AddGroup();
    const Guid folder = group.ActiveLayerID().value();
    Expect(!group.PlanMerge().has_value(), "merge.empty-folder-has-no-plan");
    ImageLayer inner = RasterLayer(4, 4, L"Inner");
    inner.parentID = folder;
    group.BeginEdit(L"Setup");
    EditLayerRecordsForTest(group, [&](DocumentFixture& document) { document.layers.push_back(inner); });
    group.EndEdit();
    group.SelectLayer(folder);
    const std::optional<DocumentSession::MergePlan> groupPlan = group.PlanMerge();
    Expect(groupPlan.has_value() && groupPlan->action == L"Merge Group" && groupPlan->name == L"Folder 1" &&
               groupPlan->ids == std::vector<Guid>{folder, inner.id} && !groupPlan->parent.has_value(),
           "merge.group-plan");
    Expect(group.MergeLayers(groupPlan.value(), composite) && group.Document().value().layers.size() == 1 &&
               group.ActiveLayer() != nullptr && group.ActiveLayer()->name == L"Folder 1" &&
               !group.ActiveLayer()->isGroup && group.history.UndoName() == L"Merge Group",
           "merge.group-applies");
    // A stale plan (its layers gone) is refused.
    Expect(!group.MergeLayers(groupPlan.value(), composite), "merge.stale-plan-refused");
}

void TestPendingEdits()
{
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    // Undo during a brush stroke drops the stroke and leaves history where it was. Finish then paints nothing.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const int count = session.history.UndoCount();
        const CanvasDocument before = session.Document().value();
        session.brushSettings = BrushSettings{20, 1, 1, 0, 0, 1};
        Expect(session.BeginBrush(Vec2{10, 10}), "pending.brush-begins");
        session.Undo();
        Expect(session.PendingBrush() == nullptr && session.history.UndoCount() == count &&
                   session.Document().value() == before,
               "pending.undo-cancels-brush");
        session.FinishBrush();
        Expect(session.history.UndoCount() == count && ActivePixel(session, 10, 10) == red,
               "pending.finish-after-undo");
    }
    // Redo during a gradient drops the gradient. A second Redo still restores the undone entry.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        session.SetOpacity(0.5);
        session.Undo();
        Expect(session.history.RedoName() == L"Layer Opacity", "pending.redo-armed");
        Expect(session.BeginGradient(Vec2{1, 1}), "pending.gradient-begins");
        session.Redo();
        Expect(session.PendingGradient() == nullptr && session.history.RedoName() == L"Layer Opacity",
               "pending.redo-cancels-gradient");
        session.Redo();
        Expect(session.history.RedoName().empty() && session.ActiveLayer() != nullptr &&
                   std::fabs(session.ActiveLayer()->opacity - 0.5) < 1e-9,
               "pending.second-redo-restores");
    }
    // Warp and a pixel move are the same kind of pending edit.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        const int count = session.history.UndoCount();
        Expect(session.BeginWarp(Vec2{10, 10}), "pending.warp-begins");
        session.Undo();
        Expect(session.PendingWarp() == nullptr && session.history.UndoCount() == count, "pending.undo-cancels-warp");
        SelectRect(session, 0, 0, 20, 20, false);
        Expect(session.BeginPixelMove(), "pending.pixel-move-begins");
        session.Undo();
        Expect(session.PendingPixelMove() == nullptr && session.history.UndoName() == L"Select",
               "pending.undo-cancels-pixel-move");
        // RF-214 (legacy EditorSession.undo / restore): a crop frame, even the Crop tool's default whole-canvas one,
        // is not an edit in progress; Undo drops it and still undoes, and Redo does the same.
        session.SetCropFrame(CropRect{5, 5, 40, 20});
        const int cropped = session.history.UndoCount();
        session.Undo();
        Expect(!session.CropFrame().has_value() && session.history.UndoCount() == cropped - 1 &&
                   session.history.RedoName() == L"Select",
               "pending.undo-with-a-crop-frame-undoes");
        session.SetCropFrame(session.VisibleCropRect());
        session.Redo();
        Expect(!session.CropFrame().has_value() && session.history.UndoCount() == cropped &&
                   session.history.UndoName() == L"Select",
               "pending.redo-with-a-crop-frame-redoes");
        DocumentSession empty = PixelEditSession();
        empty.history.Reset();
        empty.SetCropFrame(CropRect{1, 1, 10, 10});
        empty.Undo();
        Expect(empty.CropFrame().has_value() && !empty.history.CanUndo(),
               "pending.undo-with-nothing-to-undo-keeps-the-frame");
    }
    // Cut on a hidden layer, and Cut while a color panel is open, leaves the clipboard and the pixels alone.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 0, 0, 20, 20, false);
        session.ToggleVisibility(session.ActiveLayerID().value());
        session.CutSelection();
        Expect(!session.Clipboard().has_value() && ActivePixel(session, 5, 5) == red,
               "pending.cut-hidden-keeps-pixels");
        session.ToggleVisibility(session.ActiveLayerID().value());
        session.BeginColorEdit(FilterKind::Levels);
        Expect(session.HasColorEdit(FilterKind::Levels), "pending.levels-open");
        session.CutSelection();
        Expect(!session.Clipboard().has_value() && session.HasColorEdit(FilterKind::Levels) &&
                   ActivePixel(session, 5, 5) == red,
               "pending.cut-during-levels-keeps-pixels");
        session.CancelColorEdit();
    }
    // A paintable cut clears and publishes one clipboard.
    {
        DocumentSession session = PixelEditSession();
        TwoColorLayer(session);
        SelectRect(session, 0, 0, 20, 20, false);
        session.CutSelection();
        Expect(session.Clipboard().has_value() && session.history.UndoName() == L"Clear" &&
                   ActivePixel(session, 5, 5)[3] == 0,
               "pending.cut-clears");
    }
    // A selection over transparent pixels lifts nothing to clear, so the float is not inserted.
    {
        DocumentSession session = PixelEditSession();
        session.SetForegroundColor(0xFFFF0000u);
        SelectRect(session, 0, 0, 10, 10, false);
        session.FillSelection(FillSource::Foreground);
        session.Deselect();
        SelectRect(session, 50, 10, 15, 10, false);
        const std::size_t layers = session.Document().value().layers.size();
        const int count = session.history.UndoCount();
        session.BeginSelectionTransform();
        Expect(!session.PendingTransformEdit().has_value() && session.Document().value().layers.size() == layers &&
                   session.history.UndoCount() == count && session.history.UndoName() == L"Select" &&
                   ActivePixel(session, 5, 5) == red,
               "pending.float-aborts-when-clear-paints-nothing");
    }
}

// ── Review fixes (Specs/Plans/WIP/ReviewFixes_2026-09-23.md, WP2) ──────────

void TestMaskFillAcrossCommits()
{
    // RF-201: a hide-all mask painted on two tiles of a 1024 × 1024 layer keeps hiding everything the strokes did not
    // touch; the second commit must not stamp the reveal value over the first commit's fill.
    DocumentSession session = PixelEditSession(1024, 1024);
    InsertPixels(session, SolidRaster(1024, 1024, 0, 0, 255, 255), L"Red");
    session.AddLayerMask(false);
    Expect(session.IsMaskSelected() && MaskValue(session, 600.5, 600.5) == 0, "maskfill.hide-all");
    session.maskPaintWhite = true;
    session.brushSettings = BrushSettings{20, 1, 1, 1, 1, 1};
    Expect(session.BeginBrush(Vec2{100, 100}), "maskfill.first-begin");
    session.FinishBrush();
    Expect(MaskValue(session, 100.5, 100.5) == 255 && MaskValue(session, 600.5, 600.5) == 0, "maskfill.first-stroke");
    Expect(session.BeginBrush(Vec2{800, 800}), "maskfill.second-begin");
    session.FinishBrush();
    const Raster& twice = session.ActiveLayer()->mask->asset.image;
    Expect(MaskValue(session, 800.5, 800.5) == 255 && MaskValue(session, 100.5, 100.5) == 255 &&
               MaskValue(session, 600.5, 600.5) == 0 && MaskValue(session, 1000.5, 20.5) == 0 && twice.IsSparse() &&
               !twice.IsMaterialized(),
           "maskfill.second-stroke-keeps-hiding");
    session.Undo();
    Expect(MaskValue(session, 800.5, 800.5) == 0 && MaskValue(session, 100.5, 100.5) == 255 &&
               MaskValue(session, 600.5, 600.5) == 0,
           "maskfill.undo");
    // The image growing past its edge carries the painted hide-all mask: its old area keeps hiding and only the new
    // area reveals (a 600 × 300 layer on a 1000 × 300 canvas, so the growth spans tiles).
    DocumentSession grown = PixelEditSession(1000, 300);
    const Guid id = InsertPixels(grown, SolidRaster(600, 300, 0, 0, 255, 255), L"Red");
    grown.AddLayerMask(false);
    grown.maskPaintWhite = true;
    grown.brushSettings = BrushSettings{20, 1, 1, 1, 1, 1};
    Expect(grown.BeginBrush(Vec2{50, 50}), "maskfill.growth-mask-begin");
    grown.FinishBrush();
    grown.SelectLayerTarget(id, false);
    grown.brushSettings = BrushSettings{20, 1, 0, 1, 0, 1};
    Expect(!grown.IsMaskSelected() && grown.BeginBrush(Vec2{900, 150}), "maskfill.growth-image-begin");
    grown.FinishBrush();
    const ImageLayer* layer = grown.ActiveLayer();
    Expect(layer != nullptr && layer->asset->image.Width() > 600 && layer->mask.has_value() &&
               layer->mask->asset.image.Width() == layer->asset->image.Width() &&
               layer->mask->asset.image.Height() == layer->asset->image.Height(),
           "maskfill.growth-carries-the-mask");
    Expect(MaskValue(grown, 50.5, 50.5) == 255 && MaskValue(grown, 300.5, 150.5) == 0 &&
               MaskValue(grown, 590.5, 290.5) == 0 && MaskValue(grown, 900.5, 150.5) == 255,
           "maskfill.growth-keeps-the-old-area-hidden");
}

void TestStretchedMaskPainting()
{
    // RF-211: a 300 × 150 mask stretched over a 600 × 300 layer (its left half hidden) keeps its place when painted:
    // the untouched pixels, in tiles the stroke never reached, are resampled onto the layer grid instead of
    // collapsing to the value of pixel (0, 0).
    std::vector<std::uint8_t> halves(300u * 150u);
    for (std::size_t index = 0; index < halves.size(); ++index)
    {
        halves[index] = index % 300u < 150u ? 0 : 255;
    }
    const Raster half = Raster::Make(300, 150, RasterFormat::Gray8, halves.data(), halves.size());
    DocumentSession session = PixelEditSession(600, 300);
    const Guid id = InsertPixels(session, SolidRaster(600, 300, 0, 0, 255, 255), L"Red");
    EditLayerRecordsForTest(
        session, [&](DocumentFixture& document)
        { document.layers.back().mask = LayerMask{RasterAsset{half, half, L"mask"}, true, std::nullopt, true}; });
    session.SelectLayerTarget(id, true);
    Expect(session.IsMaskSelected() && MaskValue(session, 280.5, 280.5) == 0 && MaskValue(session, 450.5, 200.5) == 255,
           "stretchmask.before");
    session.maskPaintWhite = true;
    session.brushSettings = BrushSettings{10, 1, 0, 0, 0, 1};
    Expect(session.BeginBrush(Vec2{40, 20}), "stretchmask.begin");
    session.FinishBrush();
    const Raster& painted = session.ActiveLayer()->mask->asset.image;
    Expect(painted.Width() == 600 && painted.Height() == 300 && MaskValue(session, 40.5, 20.5) == 255 &&
               MaskValue(session, 100.5, 100.5) == 0 && MaskValue(session, 280.5, 280.5) == 0 &&
               MaskValue(session, 450.5, 200.5) == 255 && MaskValue(session, 599.5, 299.5) == 255,
           "stretchmask.untouched-pixels-keep-their-place");
    // Growing the image carries the stretched mask onto the new grid in place, revealing only the new area.
    DocumentSession grown = PixelEditSession(1000, 300);
    const Guid grownId = InsertPixels(grown, SolidRaster(600, 300, 0, 0, 255, 255), L"Red");
    EditLayerRecordsForTest(
        grown, [&](DocumentFixture& document)
        { document.layers.back().mask = LayerMask{RasterAsset{half, half, L"mask"}, true, std::nullopt, true}; });
    grown.SelectLayer(grownId);
    grown.brushSettings = BrushSettings{10, 1, 0, 1, 0, 1};
    Expect(!grown.IsMaskSelected() && grown.BeginBrush(Vec2{900, 150}), "stretchmask.growth-begin");
    grown.FinishBrush();
    Expect(grown.ActiveLayer()->asset->image.Width() > 600 && MaskValue(grown, 100.5, 100.5) == 0 &&
               MaskValue(grown, 280.5, 280.5) == 0 && MaskValue(grown, 450.5, 200.5) == 255 &&
               MaskValue(grown, 900.5, 150.5) == 255,
           "stretchmask.growth-keeps-its-place");
}

void TestActiveLayerTargets()
{
    // RF-203 (legacy EditorSession.activeLayerID didSet): every change of the active layer drops the mask target and
    // makes the selection that layer alone. Add Mask, then New Layer: the new layer is the target, so it paints.
    DocumentSession session = PixelEditSession(100, 40);
    session.AddLayerMask(true);
    Expect(session.IsMaskSelected(), "active.mask-targeted");
    session.AddBlankLayer();
    Expect(!session.IsMaskSelected() && session.CanPaint(), "active.new-layer-drops-the-mask-target");
    session.brushSettings = BrushSettings{10, 1, 1, 0, 0, 1};
    Expect(session.BeginBrush(Vec2{20, 20}), "active.new-layer-paints");
    session.FinishBrush();
    Expect(session.ActiveLayer()->asset.has_value() && session.history.UndoName() == L"Brush Stroke",
           "active.new-layer-painted");
    // After File > New (CreateDocument) Ctrl+G wraps Layer 1 instead of making an empty folder.
    DocumentSession created;
    created.CreateDocument(50, 50, true);
    const Guid layer1 = created.ActiveLayerID().value();
    created.GroupSelectedLayers();
    const ImageLayer* wrapped = Find(created.Document(), layer1);
    Expect(wrapped != nullptr && created.ActiveLayer() != nullptr && created.ActiveLayer()->isGroup &&
               wrapped->parentID == created.ActiveLayerID(),
           "active.new-document-group-wraps-layer-1");
    // After New Folder the Move tool moves the folder's (empty) contents, never the layer that was active before.
    DocumentSession folder = PixelEditSession(100, 40);
    const Guid painted = InsertPixels(folder, SolidRaster(20, 20, 0, 0, 255, 255), L"Painted");
    folder.AddGroup();
    Expect(folder.GroupTransformMemberIDs().empty() && !folder.CanTransform(), "active.new-folder-moves-nothing-else");
    const LayerTransform before = Find(folder.Document(), painted)->transform;
    folder.NudgeLayer(5, 0);
    Expect(Find(folder.Document(), painted)->transform == before, "active.new-folder-nudge-leaves-the-image");
    // A duplicate, a merge, a deletion and a paste each leave the new active layer as the only selected one.
    DocumentSession mixed = PixelEditSession(40, 40);
    InsertPixels(mixed, SolidRaster(10, 10, 0, 0, 255, 255), L"Red");
    mixed.AddLayerMask(true);
    mixed.DuplicateActiveLayer();
    Expect(!mixed.IsMaskSelected() && mixed.ActiveLayer()->name == L"Red copy" && !mixed.TransformsAsGroup(),
           "active.duplicate-targets-the-copy");
    mixed.SelectLayerTarget(mixed.ActiveLayerID().value(), true);
    Expect(mixed.IsMaskSelected(), "active.copy-mask-targeted");
    mixed.DeleteLayer(mixed.ActiveLayerID().value());
    Expect(!mixed.IsMaskSelected() && mixed.ActiveLayer() != nullptr && mixed.ActiveLayer()->name == L"Red",
           "active.delete-drops-the-mask-target");
    // A reopened document never keeps a mask target, even on a layer with the same id.
    DocumentSession reopened = PixelEditSession(40, 40);
    reopened.AddLayerMask(true);
    const Guid same = reopened.ActiveLayerID().value();
    const CanvasDocument copy = reopened.Document().value();
    reopened.ReplaceDocument(copy, same);
    Expect(!reopened.IsMaskSelected() && reopened.ActiveLayerID() == same && !reopened.history.CanUndo() &&
               !reopened.history.IsModified() && reopened.Document().value() == copy,
           "active.replace-document-drops-the-mask-target");
    reopened.ReplaceDocument(copy, Guid::Generate());
    Expect(!reopened.ActiveLayerID().has_value() && !reopened.TransformsAsGroup(),
           "active.replace-document-unknown-id");
    // Selecting the same layer again after its mask is gone drops the target too.
    DocumentSession lost = PixelEditSession(40, 40);
    lost.AddLayerMask(true);
    EditLayerRecordsForTest(lost, [](DocumentFixture& document) { document.layers.back().mask.reset(); });
    lost.SelectLayer(lost.ActiveLayerID());
    Expect(!lost.IsMaskSelected() && lost.CanPaint(), "active.reselect-without-a-mask");
}

void TestCutWithFullHistory()
{
    // RF-204: Cut on a mask when the history is full (100 entries: every new entry trims one) still publishes the copy;
    // whether the clear painted comes from the fill itself, never from the entry count.
    DocumentSession session = PixelEditSession();
    const Guid id = InsertPixels(session, SolidRaster(100, 40, 0, 0, 255, 255), L"Red");
    session.AddLayerMask(false);
    SelectRect(session, 10, 10, 20, 20, false);
    for (int index = 0; index < kHistoryEntryLimit + 1; ++index)
    {
        session.RenameLayer(id, index % 2 == 0 ? L"Even" : L"Odd");
    }
    Expect(session.history.UndoCount() == kHistoryEntryLimit && session.IsMaskSelected(), "cutfull.history-full");
    session.CutSelection();
    Expect(session.Clipboard().has_value() && session.Clipboard()->image.Width() == 20 &&
               PixelOf(session.Clipboard()->image, 5, 5) == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
               session.history.UndoName() == L"Fill Mask" && session.history.UndoCount() == kHistoryEntryLimit &&
               ActiveMaskValue(session, 15, 15) == 255 && ActiveMaskValue(session, 50, 5) == 0,
           "cutfull.mask-cut-keeps-the-copy");
    // The image path reports the same way.
    session.SelectLayerTarget(id, false);
    SelectRect(session, 60, 10, 10, 10, false);
    Expect(session.ClearSelectedPixels() && ActivePixel(session, 65, 15)[3] == 0 &&
               session.history.UndoCount() == kHistoryEntryLimit,
           "cutfull.clear-reports-with-a-full-history");
}

void TestTransformWaitsForPanels()
{
    // RF-206: an open color or filter panel owns the layer until OK or Cancel, so a Move drag, a nudge, Free
    // Transform and both flips do nothing meanwhile; OK then applies at the layer's own place.
    DocumentSession session = PixelEditSession();
    InsertPixels(session, SolidRaster(40, 20, 0, 0, 200, 255), L"Red");
    const LayerTransform before = session.ActiveLayer()->transform;
    session.BeginColorEdit(FilterKind::Levels);
    Expect(session.HasColorEdit(FilterKind::Levels) && !session.CanTransform(), "panelowns.levels-blocks-transform");
    session.BeginTransform(false);
    session.NudgeLayer(10, 0);
    session.TransformCommand();
    session.FlipLayers(true);
    session.FlipCanvas(true);
    Expect(!session.PendingTransformEdit().has_value() && session.ActiveLayer()->transform == before,
           "panelowns.levels-transform-refused");
    LevelsSettings levels;
    levels.ranges[0].black = 50;
    session.UpdateLevels(levels, true);
    session.CommitColorEdit();
    Expect(session.history.UndoName() == L"Levels" && session.ActiveLayer()->transform == before &&
               ActivePixel(session, 5, 5)[2] < 200,
           "panelowns.levels-applies");
    session.BeginColorEdit(FilterKind::HueSaturation);
    session.NudgeLayer(0, 7);
    session.FlipCanvas(false);
    HueSaturationSettings hue;
    hue.adjustments[static_cast<std::size_t>(ColorRange::Master)].hue = 120;
    session.UpdateHueSaturation(hue, true);
    session.CommitColorEdit();
    Expect(session.history.UndoName() == L"Hue/Saturation" && session.ActiveLayer()->transform == before,
           "panelowns.hue-applies-in-place");
    session.BeginColorEdit(FilterKind::AddNoise);
    Expect(session.HasColorEdit() && !session.CanTransform(), "panelowns.filter-blocks-transform");
    session.CancelColorEdit();
    Expect(session.CanTransform(), "panelowns.transform-after-cancel");
}

void TestOneColorEditSession()
{
    // RF-801: Levels, Hue/Saturation, the Image menu's adjustments and the filters are one panel session with one
    // rule. Opening any panel drops a lasso draft, and OK installs only while the layer still holds the pixels and the
    // placement the panel opened on; otherwise the panel closes without an entry (Hue/Saturation used to keep the
    // draft and apply wherever the layer had gone).
    FilterSettings changes;
    changes.levels.ranges[0].black = 50;
    Adjust::RangeOf(changes.hsv).hue = 120;
    changes.curves.channels[0] = {CurvePoint{0, 255}, CurvePoint{255, 255}};
    changes.exposure.exposure = 1;
    for (const FilterKind kind : {FilterKind::Levels, FilterKind::HueSaturation, FilterKind::Curves,
                                  FilterKind::Exposure, FilterKind::AddNoise})
    {
        DocumentSession session = PixelEditSession();
        const Guid id = InsertPixels(session, SolidRaster(40, 20, 0, 0, 200, 255), L"Red");
        session.BeginLasso(Vec2{5, 5}, SelectionMode::New);
        session.ExtendLasso(Vec2{30, 5});
        const bool drafted = session.PendingLasso().has_value();
        session.BeginColorEdit(kind);
        Expect(drafted && session.HasColorEdit(kind) && !session.PendingLasso().has_value(),
               "coloredit.opening-drops-the-lasso-draft");
        session.UpdateColorEdit(changes, true);
        const bool previewed = session.PendingColorEdit()->previewRaster.HasStorage();
        // The layer moves under the open panel (a resize the host did not settle first).
        auto moved = DocumentFixture::Copy(session.Document().value());
        for (ImageLayer& layer : moved.layers)
        {
            if (layer.id == id)
            {
                layer.transform.origin.x += 10;
            }
        }
        session.ApplyDocumentSize(std::move(moved).Seal(), L"Canvas Size");
        const Raster original = session.ActiveLayer()->asset->image;
        const int count = session.history.UndoCount();
        session.CommitColorEdit();
        Expect(previewed && !session.HasColorEdit() && session.history.UndoCount() == count &&
                   session.history.UndoName() == L"Canvas Size" && session.ActiveLayer()->asset->image == original,
               "coloredit.moved-layer-closes-without-an-entry");
        // Back on the placement it opened on, the same edit applies as one entry named after the kind.
        session.Undo();
        session.BeginColorEdit(kind);
        session.UpdateColorEdit(changes, true);
        session.CommitColorEdit();
        Expect(!session.HasColorEdit() && session.history.UndoCount() == count &&
                   session.history.UndoName() == FilterKindName(kind) &&
                   !(session.ActiveLayer()->asset->image == original),
               "coloredit.in-place-edit-applies");
    }
    // An edit that changes nothing previews the layer's own pixels, whatever the kind: no distortion, the default
    // exposure, no grain, identity Levels and Hue/Saturation.
    {
        DocumentSession session = PixelEditSession();
        InsertPixels(session, SolidRaster(40, 20, 0, 0, 200, 255), L"Red");
        FilterSettings neutral;
        neutral.grain.amount = 0;
        bool unchanged = true;
        for (const FilterKind kind : {FilterKind::LensCorrection, FilterKind::Exposure, FilterKind::Grain,
                                      FilterKind::Levels, FilterKind::HueSaturation})
        {
            session.BeginColorEdit(kind);
            session.UpdateColorEdit(neutral, true);
            unchanged = unchanged && session.HasColorEdit(kind) &&
                        !session.PendingColorEdit()->previewRaster.HasStorage() &&
                        session.DisplayDocument() == &session.Document().value();
            session.CancelColorEdit();
        }
        FilterSettings bent = neutral;
        bent.distortion = 50;
        session.BeginColorEdit(FilterKind::LensCorrection);
        session.UpdateColorEdit(bent, true);
        Expect(unchanged && session.PendingColorEdit()->previewRaster.HasStorage(),
               "coloredit.no-change-previews-the-layer");
        session.CancelColorEdit();
    }
    // Levels and Hue/Saturation open at identity even after the last applied settings kept theirs.
    {
        DocumentSession session = PixelEditSession();
        InsertPixels(session, SolidRaster(40, 20, 0, 0, 200, 255), L"Red");
        session.BeginColorEdit(FilterKind::Levels);
        session.UpdateColorEdit(changes, true);
        session.CommitColorEdit();
        session.BeginColorEdit(FilterKind::HueSaturation);
        session.UpdateColorEdit(changes, true);
        session.CommitColorEdit();
        const bool kept =
            session.history.UndoName() == L"Hue/Saturation" && session.filterSettings.levels == changes.levels;
        session.BeginColorEdit(FilterKind::Levels);
        const bool levels = Adjust::LevelsIsIdentity(session.PendingColorEdit()->settings.levels);
        session.CancelColorEdit();
        session.BeginColorEdit(FilterKind::HueSaturation);
        const bool hue = Adjust::HsvIsIdentity(session.PendingColorEdit()->settings.hsv);
        session.CancelColorEdit();
        Expect(kept && levels && hue, "coloredit.levels-and-hue-open-at-identity");
    }
    // Only Add Noise and Grain draw a pattern seed when they open: after Levels, Hue/Saturation or a blur the next
    // Grain layer gets the seed it would have had, after Add Noise the next one along.
    {
        const auto nextGrainSeed = [](std::initializer_list<FilterKind> opened)
        {
            DocumentSession session = PixelEditSession();
            InsertPixels(session, SolidRaster(40, 20, 0, 0, 200, 255), L"Red");
            for (const FilterKind kind : opened)
            {
                session.BeginColorEdit(kind);
                session.CancelColorEdit();
            }
            session.AddAdjustment(AdjustmentKind::Grain);
            return session.ActiveLayer()->adjustment->Grain().seed;
        };
        const std::uint32_t untouched = nextGrainSeed({});
        Expect(nextGrainSeed({FilterKind::Levels, FilterKind::HueSaturation, FilterKind::GaussianBlur}) == untouched &&
                   nextGrainSeed({FilterKind::AddNoise}) != untouched,
               "coloredit.only-noise-and-grain-draw-a-seed");
    }
    // The Image menu's kinds run their adjustment layer's kernel; Grain sits in layer pixels at the job's seed.
    {
        const Raster swatch =
            BytesRaster(8, 8,
                        [](int x, int y) -> std::array<std::uint8_t, 4>
                        {
                            return {static_cast<std::uint8_t>(x * 30), static_cast<std::uint8_t>(y * 30),
                                    static_cast<std::uint8_t>(200 - x * 20), 255};
                        });
        FilterSettings settings = changes;
        settings.gradientMap = GradientMapSettings{AdjustmentColor{1, 0, 0}, AdjustmentColor{0, 0, 1}, true};
        settings.grain = GrainSettings{60, 2, 40, 7};
        bool same = true;
        for (const FilterKind kind : {FilterKind::Levels, FilterKind::HueSaturation, FilterKind::Curves,
                                      FilterKind::Exposure, FilterKind::GradientMap})
        {
            LayerAdjustment record = Adjustment(FilterAdjustmentKind(kind).value_or(AdjustmentKind::Hsv));
            const std::vector<std::uint8_t> expected =
                Bytes(Adjust::ApplyAdjustment(Adjust::AdjustmentWithSettings(record, settings), swatch));
            same = same && !expected.empty() && Bytes(Adjust::RunFilter(kind, swatch, settings, 1, 0)) == expected &&
                   expected != Bytes(swatch);
        }
        LayerAdjustment grain = Adjustment(AdjustmentKind::Grain);
        grain.grainSettings = GrainSettings{60, 2, 40, 9};
        const std::vector<std::uint8_t> grained = Bytes(Adjust::RunFilter(FilterKind::Grain, swatch, settings, 0.5, 9));
        Expect(same && grained == Bytes(Adjust::ApplyAdjustment(grain, swatch, 0, 0, 2)) &&
                   grained != Bytes(Adjust::RunFilter(FilterKind::Grain, swatch, settings, 0.5, 7)),
               "coloredit.image-kinds-run-the-layer-kernel");
    }
}

void TestDisplayCopyFollowsEdits()
{
    // RF-209: while a transform is pending the canvas draws a cached display copy; an edit the pending transform does
    // not own (opacity, blend mode, deleting another layer) must show in it at once.
    DocumentSession session = PixelEditSession();
    const Guid other = InsertPixels(session, SolidRaster(20, 20, 255, 0, 0, 255), L"Blue");
    const Guid moving = InsertPixels(session, SolidRaster(20, 20, 0, 0, 255, 255), L"Red");
    session.BeginTransform(true);
    LayerTransform draft = session.PendingTransformEdit()->draft;
    draft.origin.x += 30;
    session.PreviewTransform(draft);
    const CanvasDocument* display = session.DisplayDocument();
    Expect(display != nullptr && Find(*display, moving) != nullptr && Find(*display, moving)->transform == draft,
           "displaycopy.shows-the-draft");
    session.SetOpacity(0.25);
    display = session.DisplayDocument();
    Expect(display != nullptr && Find(*display, moving)->opacity == 0.25 && Find(*display, moving)->transform == draft,
           "displaycopy.follows-opacity");
    session.SetBlendMode(LayerBlendMode::Multiply);
    display = session.DisplayDocument();
    Expect(display != nullptr && Find(*display, moving)->blendMode == LayerBlendMode::Multiply,
           "displaycopy.follows-blend-mode");
    session.ToggleVisibility(other);
    display = session.DisplayDocument();
    Expect(display != nullptr && !Find(*display, other)->visible && session.PendingTransformEdit().has_value(),
           "displaycopy.follows-visibility");
}

void TestSelectionLoadRefusals()
{
    // RF-217: a mask or layer whose outline would pass 8 000 000 pixel edges (a 2100 × 2100 checkerboard: 8.8 M) is
    // refused with a reason the host can show, instead of silently loading nothing.
    constexpr int kSide = 2100;
    std::vector<std::uint8_t> checker(static_cast<std::size_t>(kSide) * static_cast<std::size_t>(kSide));
    std::vector<std::uint8_t> alpha(checker.size() * 4u, 0);
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(kSide) + static_cast<std::size_t>(x);
            const bool dark = (x + y) % 2 == 0;
            checker[index] = dark ? 0 : 255;
            alpha[index * 4u + 3u] = dark ? 255 : 0;
        }
    }
    const Raster detailed = Raster::Make(kSide, kSide, RasterFormat::Gray8, checker.data(), checker.size());
    WandFailure failure = WandFailure::None;
    Expect(!MaskTracing::DarkPixels(detailed, &failure).has_value() && failure == WandFailure::TooDetailed,
           "loadrefused.dark-pixels-too-detailed");
    DocumentSession session = SelectionSession(kSide, kSide);
    const Guid id = session.ActiveLayerID().value();
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                document.layers.back().mask =
                                    LayerMask{RasterAsset{detailed, Raster::MakeThumbnail(detailed), L"mask"}, true,
                                              std::nullopt, true};
                            });
    const int count = session.history.UndoCount();
    failure = WandFailure::None;
    Expect(!session.LoadMaskSelection(id, SelectionMode::New, &failure) && failure == WandFailure::TooDetailed &&
               !session.Selection().has_value() && session.history.UndoCount() == count,
           "loadrefused.mask-too-detailed");
    // Nothing to trace is not a refusal.
    const Raster white = GrayRaster(1, 1, 255);
    EditLayerRecordsForTest(
        session, [&](DocumentFixture& document)
        { document.layers.back().mask = LayerMask{RasterAsset{white, white, L"mask"}, true, std::nullopt, true}; });
    failure = WandFailure::TooDetailed;
    Expect(!session.LoadMaskSelection(id, SelectionMode::New, &failure) && failure == WandFailure::None,
           "loadrefused.nothing-is-not-a-refusal");
    const Guid layer =
        InsertPixels(session, Raster::Make(kSide, kSide, RasterFormat::Bgra8888, alpha.data(), alpha.size()), L"Dots");
    failure = WandFailure::None;
    Expect(!session.LoadLayerSelection(layer, SelectionMode::New, &failure) && failure == WandFailure::TooDetailed &&
               !session.Selection().has_value(),
           "loadrefused.layer-too-detailed");
}

void TestCropWholePixels()
{
    // RF-215 (Decision D6): an Alt-drawn (symmetric) frame mirrors about the press point rounded to the half-pixel
    // grid, so it lands on whole pixels even when a snap target moved the dragged edge; legacy mirrored about the
    // unrounded point and left the frame between pixels.
    const auto whole = [](const CropRect& rect)
    {
        return rect.x == std::round(rect.x) && rect.y == std::round(rect.y) && rect.width == std::round(rect.width) &&
               rect.height == std::round(rect.height) && rect.width >= 1 && rect.height >= 1;
    };
    const CropSnap snap{{0, 200, 90}, {0, 100, 70}, 6};
    const CropDrag create{Vec2{50.3, 40.3}, CropRect{}, CropDrag::Mode::Create, -1};
    const Vec2 dragged{80.7, 61.2};
    const CropRect free = snap.Apply(create.Updated(dragged, std::nullopt, true), create, dragged, std::nullopt, true);
    const Vec2 snapTo{87.4, 67.9};
    const CropRect snapped = snap.Apply(create.Updated(snapTo, std::nullopt, true), create, snapTo, std::nullopt, true);
    Expect(whole(free) && std::abs(free.MidX() - 50.3) <= 0.5 && std::abs(free.MidY() - 40.3) <= 0.5,
           "crop.symmetric-create-whole-pixels");
    Expect(whole(snapped) && snapped.MaxX() == 90 && snapped.MaxY() == 70 && snapped.MidX() == 50.5 &&
               snapped.MidY() == 40.5,
           "crop.symmetric-snap-whole-pixels");
    // Resizing an odd-sized frame about its center stays on whole pixels too.
    const CropRect odd{10, 10, 21, 11};
    const CropDrag resize{Vec2{31, 21}, odd, CropDrag::Mode::Resize, 4};
    const Vec2 corner{40.6, 25.3};
    Expect(whole(snap.Apply(resize.Updated(corner, std::nullopt, true), resize, corner, std::nullopt, true)),
           "crop.symmetric-resize-whole-pixels");
    // A frame between pixels commits on whole pixels: the canvas takes the rounded edges and the layers move by
    // whole pixels, never truncated or resampled.
    DocumentSession session;
    session.CreateDocument(64, 32);
    ImageLayer layer = ImageLayer::Blank(L"Fixture", Vec2{64, 32});
    const Raster fixture = RedLeftRaster(64, 32);
    layer.asset = RasterAsset{fixture, Raster::MakeThumbnail(fixture), layer.name};
    EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(layer); });
    session.SelectLayer(layer.id);
    session.SetCropFrame(CropRect{8.4, 4.6, 32.2, 15.7});
    Expect(session.CommitCrop() && session.Document().value().width == 33 && session.Document().value().height == 15,
           "crop.fractional-frame-commits-rounded");
    const ImageLayer* moved = Find(session.Document(), layer.id);
    Expect(moved != nullptr && moved->transform.origin == Vec2{-8, -5} && moved->asset->image == fixture,
           "crop.fractional-frame-moves-whole-pixels");
}

void TestDistortMasks()
{
    // RF-207 (legacy Distort.distort): an unlinked mask keeps its place on the document while its layer is
    // resampled into the shape, and a linked mask placed apart takes the same perspective over its own bounds, in
    // the preview and on Apply.
    const std::array<Vec2, 4> shape{Vec2{10, 10}, Vec2{60, 10}, Vec2{30, 30}, Vec2{10, 30}};
    std::vector<std::uint8_t> halves(20u * 20u);
    for (std::size_t index = 0; index < halves.size(); ++index)
    {
        halves[index] = index % 20u < 10u ? 0 : 255;
    }
    const Raster half = Raster::Make(20, 20, RasterFormat::Gray8, halves.data(), halves.size());
    {
        DocumentSession session;
        session.CreateDocument(100, 60, false);
        const Guid id = InsertPainted(session, 20, 20, Vec2{10, 10});
        EditLayerRecordsForTest(
            session, [&](DocumentFixture& document)
            { document.layers.back().mask = LayerMask{RasterAsset{half, half, L"mask"}, true, std::nullopt, false}; });
        const LayerTransform original = session.ActiveLayer()->transform;
        session.BeginTransform(false);
        session.BeginDistort();
        session.PreviewCorners(shape);
        const CanvasDocument* display = session.DisplayDocument();
        const ImageLayer* shown = display != nullptr ? Find(*display, id) : nullptr;
        Expect(shown != nullptr && shown->transform.size == Vec2{50, 20} && shown->mask->placement == original,
               "distortmask.unlinked-preview-stays");
        session.CommitTransform();
        const ImageLayer* layer = session.ActiveLayer();
        Expect(layer->transform.size == Vec2{50, 20} && layer->mask->placement == original &&
                   layer->mask->asset.image == half && !layer->mask->linked,
               "distortmask.unlinked-stays-on-the-document");
    }
    {
        DocumentSession session;
        session.CreateDocument(100, 60, false);
        const Guid id = InsertPainted(session, 20, 20, Vec2{10, 10});
        LayerTransform apart;
        apart.origin = Vec2{15, 12};
        apart.size = Vec2{20, 20};
        EditLayerRecordsForTest(
            session, [&](DocumentFixture& document)
            { document.layers.back().mask = LayerMask{RasterAsset{half, half, L"mask"}, true, apart, true}; });
        const LayerTransform original = session.ActiveLayer()->transform;
        const std::array<Vec2, 4> carried = DistortWarp::Carried(apart, original, shape);
        double minX = carried[0].x;
        double maxX = carried[0].x;
        double minY = carried[0].y;
        double maxY = carried[0].y;
        for (const Vec2& corner : carried)
        {
            minX = std::min(minX, corner.x);
            maxX = std::max(maxX, corner.x);
            minY = std::min(minY, corner.y);
            maxY = std::max(maxY, corner.y);
        }
        const Vec2 origin{std::floor(minX), std::floor(minY)};
        const Vec2 size{std::ceil(maxX) - std::floor(minX), std::ceil(maxY) - std::floor(minY)};
        session.BeginTransform(false);
        session.BeginDistort();
        session.PreviewCorners(shape);
        const CanvasDocument* display = session.DisplayDocument();
        const ImageLayer* shown = display != nullptr ? Find(*display, id) : nullptr;
        Expect(shown != nullptr && shown->mask->placement.has_value() && shown->mask->placement->origin == origin &&
                   shown->mask->placement->size == size,
               "distortmask.placed-preview-follows");
        session.CommitTransform();
        const ImageLayer* layer = session.ActiveLayer();
        Expect(layer->mask->placement.has_value() && layer->mask->placement->origin == origin &&
                   layer->mask->placement->size == size && layer->mask->linked &&
                   layer->mask->asset.image.Width() == static_cast<int>(size.x) && !(layer->mask->asset.image == half),
               "distortmask.placed-takes-the-perspective");
        session.Undo();
        Expect(session.ActiveLayer()->mask->placement == apart && session.ActiveLayer()->mask->asset.image == half,
               "distortmask.undo");
    }
}

// A hidden layer holding `width` × `height` image pixels of the document's budget without allocating them (an empty
// sparse raster), shown 100 × 100 at the canvas origin.
ImageLayer BudgetLayer(int width, int height)
{
    ImageLayer layer = ImageLayer::Blank(L"Budget", Vec2{100, 100});
    layer.asset = RasterAsset{Raster::MakeSparse(width, height, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0),
                              Raster::Make(1, 1, RasterFormat::Bgra8888), L"Budget"};
    layer.visible = false;
    return layer;
}

void TestDocumentPixelBudget()
{
    // RF-212 (Decision D7): every edit that grows pixels checks the document's 100 M image and mask budgets (legacy
    // enforced them only on save, where the document was then rejected), refuses when they would be exceeded and
    // reports it once through TakeBudgetRefusal.
    const Raster red = SolidRaster(100, 100, 0, 0, 255, 255);
    {
        DocumentSession session = PixelEditSession(200, 200);
        EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                { document.layers.push_back(BudgetLayer(10000, 10000)); });
        const std::size_t count = session.Document().value().layers.size();
        Expect(session.PixelBudgetLeft(false) == 0 && !session.TakeBudgetRefusal(), "docbudget.full");
        session.PasteImage(SolidRaster(20, 20, 0, 0, 255, 255));
        Expect(session.Document().value().layers.size() == count && session.TakeBudgetRefusal() &&
                   !session.TakeBudgetRefusal() && ValidateDocument(session.Document().value()) == DocumentStatus::Ok,
               "docbudget.paste-refused");
    }
    {
        // 100 000 pixels left for a 300 × 300 layer (90 000): room for its own edits, not for a copy or growth.
        DocumentSession session = PixelEditSession(300, 300);
        EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                { document.layers.push_back(BudgetLayer(10000, 9990)); });
        InsertPixels(session, SolidRaster(300, 300, 0, 0, 255, 255), L"Red");
        const std::size_t count = session.Document().value().layers.size();
        session.DuplicateActiveLayer();
        session.BeginDuplicateTransform();
        Expect(session.Document().value().layers.size() == count && !session.PendingTransformEdit().has_value() &&
                   session.TakeBudgetRefusal() && ValidateDocument(session.Document().value()) == DocumentStatus::Ok,
               "docbudget.duplicate-refused");
        session.filterSettings.radius = 10;
        session.BeginColorEdit(FilterKind::GaussianBlur);
        Expect(!session.HasColorEdit() && session.TakeBudgetRefusal(), "docbudget.blur-grid-refused");
        session.BeginColorEdit(FilterKind::AddNoise);
        Expect(session.HasColorEdit() && !session.TakeBudgetRefusal(), "docbudget.noise-needs-no-room");
        session.CancelColorEdit();
        // A floating selection moved past the layer's edge would grow it: the merge is refused and the document
        // restored.
        SelectRect(session, 40, 40, 20, 20, false);
        session.TransformCommand();
        Expect(session.PendingTransformEdit().has_value() && session.PendingTransformEdit()->floating.has_value(),
               "docbudget.float-begins");
        LayerTransform draft =
            session.PendingTransformEdit().has_value() ? session.PendingTransformEdit()->draft : LayerTransform{};
        draft.origin.x += 280;
        session.PreviewTransform(draft);
        session.CommitTransform();
        Expect(session.TakeBudgetRefusal() && session.Document().value().layers.size() == count &&
                   session.ActiveLayer()->name == L"Red" && session.ActiveLayer()->asset->image.Width() == 300 &&
                   ActivePixel(session, 50, 50) == std::array<std::uint8_t, 4>{0, 0, 255, 255} &&
                   ValidateDocument(session.Document().value()) == DocumentStatus::Ok,
               "docbudget.floating-merge-refused");
    }
    {
        DocumentSession session = PixelEditSession(200, 200);
        ImageLayer masked = BudgetLayer(100, 100);
        masked.mask =
            LayerMask{RasterAsset{Raster::MakeSparse(10000, 10000, RasterFormat::Gray8, Raster{}, 0, 0, {}, 255),
                                  Raster::Make(1, 1, RasterFormat::Gray8), L"mask"},
                      true, std::nullopt, true};
        EditLayerRecordsForTest(session, [&](DocumentFixture& document) { document.layers.push_back(masked); });
        InsertPixels(session, red, L"Red");
        SelectRect(session, 10, 10, 20, 20);
        session.AddMask(true);
        Expect(!session.ActiveLayer()->mask.has_value() && session.Selection().has_value() &&
                   session.TakeBudgetRefusal(),
               "docbudget.mask-from-selection-refused");
        session.Deselect();
        session.AddMask(true);
        Expect(!session.ActiveLayer()->mask.has_value() && session.TakeBudgetRefusal(),
               "docbudget.uniform-mask-refused");
    }
    {
        DocumentSession session = PixelEditSession(200, 200);
        EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                { document.layers.push_back(BudgetLayer(10000, 9999)); });
        const Guid a = InsertPixels(session, SolidRaster(20, 20, 0, 0, 255, 255), L"A");
        const Guid b = InsertPixels(session, SolidRaster(20, 20, 255, 0, 0, 255), L"B");
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    for (ImageLayer& layer : document.layers)
                                    {
                                        if (layer.id == b)
                                        {
                                            layer.transform.origin = Vec2{180, 180};
                                        }
                                    }
                                });
        const std::optional<DocumentSession::MergePlan> plan = session.PlanMerge();
        std::vector<std::uint8_t> bytes(200u * 200u * 4u, 0);
        for (const int corner : {0, 180})
        {
            for (int y = corner; y < corner + 20; ++y)
            {
                for (int x = corner; x < corner + 20; ++x)
                {
                    bytes[(static_cast<std::size_t>(y) * 200u + static_cast<std::size_t>(x)) * 4u + 3u] = 255;
                }
            }
        }
        const Raster composite = Raster::Make(200, 200, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        Expect(plan.has_value() && plan->ids == std::vector<Guid>{a, b} &&
                   !session.MergeLayers(plan.value(), composite) && session.TakeBudgetRefusal() &&
                   Find(session.Document(), a) != nullptr,
               "docbudget.merge-refused");
    }
    {
        DocumentSession session;
        session.CreateDocument(200, 200, false);
        EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                { document.layers.push_back(BudgetLayer(10000, 9999)); });
        InsertPainted(session, 20, 20, Vec2{10, 10});
        session.BeginTransform(false);
        session.BeginDistort();
        session.PreviewCorners(std::array<Vec2, 4>{Vec2{10, 10}, Vec2{190, 10}, Vec2{190, 190}, Vec2{10, 190}});
        session.CommitTransform();
        Expect(session.ActiveLayer()->asset->image.Width() == 20 && session.TakeBudgetRefusal() &&
                   ValidateDocument(session.Document().value()) == DocumentStatus::Ok,
               "docbudget.distort-refused");
        session.BeginTransform(false);
        session.BeginDistort();
        session.PreviewCorners(std::array<Vec2, 4>{Vec2{10, 10}, Vec2{90, 10}, Vec2{90, 90}, Vec2{10, 90}});
        session.CommitTransform();
        Expect(session.ActiveLayer()->asset->image.Width() == 80 && !session.TakeBudgetRefusal(),
               "docbudget.distort-that-fits");
    }
}

void TestPixelMoveTiles()
{
    // RF-216: a pixel drag rebuilds only the tiles its pixels land on now and landed on last time (and the hole's
    // tiles once), and nothing on a pointer update that rounds to the offset already shown; the committed pixels are
    // those of one direct move to the same offset.
    const auto pattern = [](int x, int y) -> std::array<std::uint8_t, 4>
    {
        const int alpha = 64 + (x * 7 + y * 13) % 192;
        return {static_cast<std::uint8_t>(alpha * (x % 3) / 2), static_cast<std::uint8_t>(alpha * (y % 5) / 4),
                static_cast<std::uint8_t>(alpha / 2), static_cast<std::uint8_t>(alpha)};
    };
    const Raster source = BytesRaster(1200, 300, pattern);
    const auto setup = [&](DocumentSession& session)
    {
        session.CreateDocument(1200, 300, true);
        InsertPixels(session, source, L"Pattern");
        SelectRect(session, 10.5, 20.25, 60, 40, true);
        return session.BeginPixelMove();
    };
    DocumentSession path;
    DocumentSession direct;
    Expect(setup(path) && setup(direct), "movetiles.begin");
    BrushStroke& raster = *path.MutablePixelMove()->raster;
    path.MovePixels(0, 0);
    static_cast<void>(raster.TakeChanged());
    path.MovePixels(300.2, 10);
    Expect(!raster.TakeChanged().empty(), "movetiles.move-rebuilds");
    path.MovePixels(299.8, 10.4);
    Expect(raster.TakeChanged().empty(), "movetiles.same-offset-rebuilds-nothing");
    path.MovePixels(900, 200);
    const std::vector<std::size_t> farTiles = raster.TakeChanged();
    bool onlyLandings = !farTiles.empty() && farTiles.size() <= 4;
    for (const std::size_t index : farTiles)
    {
        // The hole's tile (the first column) is never rebuilt again, nor anything between the landings.
        onlyLandings = onlyLandings && raster.Tiles()[index].rect.x >= 256;
    }
    Expect(onlyLandings, "movetiles.only-the-old-and-new-landings");
    path.MovePixels(650, 120);
    direct.MovePixels(650, 120);
    path.FinishPixelMove();
    direct.FinishPixelMove();
    const Raster& moved = path.ActiveLayer()->asset->image;
    const Raster& once = direct.ActiveLayer()->asset->image;
    bool same = moved.Width() == once.Width() && moved.Height() == once.Height() &&
                path.ActiveLayer()->transform == direct.ActiveLayer()->transform;
    if (same)
    {
        std::vector<std::uint8_t> a(static_cast<std::size_t>(moved.Width()) * static_cast<std::size_t>(moved.Height()) *
                                    4u);
        std::vector<std::uint8_t> b(a.size());
        moved.ReadRegion(0, 0, moved.Width(), moved.Height(), a.data(), moved.Width() * 4, 0);
        once.ReadRegion(0, 0, once.Width(), once.Height(), b.data(), once.Width() * 4, 0);
        same = a == b;
    }
    Expect(same && path.history.UndoName() == L"Move Pixels", "movetiles.path-matches-a-direct-move");
}

void PendGradient(DocumentSession& session)
{
    session.gradientSettings.style = GradientStyle::ForegroundToBackground;
    session.BeginGradient(Vec2{0.5, 20});
    session.MoveGradient(std::nullopt, Vec2{99.5, 20});
}

void TestGradientAppliesFirst()
{
    // RF-202 (Decision D3): an edit under a pending gradient applies the gradient first instead of dropping it
    // (legacy refuses those edits while a gradient is pending). A fill records both, the gradient first.
    {
        DocumentSession session = PixelEditSession();
        PendGradient(session);
        SelectRect(session, 0, 0, 10, 10);
        Expect(session.PendingGradient() != nullptr, "gradientfirst.fill-pending");
        session.FillSelection(FillSource::Foreground);
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Fill" &&
                   ActivePixel(session, 90, 30)[3] == 255 &&
                   ActivePixel(session, 5, 5) == std::array<std::uint8_t, 4>{0, 0, 0, 255},
               "gradientfirst.fill-applies-the-gradient");
        session.Undo();
        Expect(session.history.UndoName() == L"Gradient" && ActivePixel(session, 90, 30)[3] == 255,
               "gradientfirst.fill-undo-leaves-the-gradient");
    }
    // A layer flip records the gradient before the flip; the mirrored gradient runs white to black.
    {
        DocumentSession session = PixelEditSession();
        const Guid clear = InsertPixels(session, SolidRaster(100, 40, 0, 0, 0, 0), L"Clear");
        PendGradient(session);
        session.FlipLayers(true);
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Flip Horizontal" &&
                   LayerPixelOf(session, clear, 3, 20)[0] > 240 && LayerPixelOf(session, clear, 96, 20)[0] < 15,
               "gradientfirst.flip-layer");
        session.Undo();
        Expect(session.history.UndoName() == L"Gradient", "gradientfirst.flip-layer-undo");
    }
    // Flip Canvas applies the gradient and drops the crop frame (legacy flipCanvas).
    {
        DocumentSession session = PixelEditSession();
        InsertPixels(session, SolidRaster(100, 40, 0, 0, 0, 0), L"Clear");
        PendGradient(session);
        session.SetCropFrame(CropRect{5, 5, 20, 20});
        session.FlipCanvas(true);
        Expect(session.PendingGradient() == nullptr && !session.CropFrame().has_value() &&
                   session.history.UndoName() == L"Flip Canvas Horizontal",
               "gradientfirst.flip-canvas");
        session.Undo();
        Expect(session.history.UndoName() == L"Gradient", "gradientfirst.flip-canvas-undo");
    }
    // Cut copies the pixels the canvas shows: the gradient, applied first.
    {
        DocumentSession session = PixelEditSession();
        PendGradient(session);
        SelectRect(session, 0, 0, 20, 20, false);
        session.CutSelection();
        Expect(session.PendingGradient() == nullptr && session.Clipboard().has_value() &&
                   PixelOf(session.Clipboard()->image, 10, 10)[3] == 255 && session.history.UndoName() == L"Clear" &&
                   ActivePixel(session, 10, 10)[3] == 0 && ActivePixel(session, 50, 20)[3] == 255,
               "gradientfirst.cut");
    }
    // A pixel nudge moves the gradient's pixels.
    {
        DocumentSession session = PixelEditSession();
        PendGradient(session);
        SelectRect(session, 0, 0, 20, 40, false);
        session.NudgePixels(10, 0);
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Move Pixels" &&
                   ActivePixel(session, 5, 20)[3] == 0 && ActivePixel(session, 25, 20)[3] == 255,
               "gradientfirst.nudge-pixels");
    }
    // A brush stroke paints over the applied gradient.
    {
        DocumentSession session = PixelEditSession();
        PendGradient(session);
        session.brushSettings = BrushSettings{10, 1, 1, 0, 0, 1};
        Expect(session.BeginBrush(Vec2{50, 20}), "gradientfirst.brush-begins");
        session.FinishBrush();
        session.Undo();
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Gradient",
               "gradientfirst.brush");
    }
    // Deleting a layer records its gradient first, so Undo brings the layer back with it; a new gradient on the
    // next layer is its own edit.
    {
        DocumentSession session = PixelEditSession();
        session.AddBlankLayer();
        const Guid top = session.ActiveLayerID().value();
        PendGradient(session);
        session.DeleteLayer(top);
        Expect(session.PendingGradient() == nullptr && session.history.UndoName() == L"Delete Layer",
               "gradientfirst.delete-layer");
        PendGradient(session);
        session.Undo();
        session.Undo();
        const ImageLayer* restored = Find(session.Document(), top);
        Expect(restored != nullptr && restored->asset.has_value() && session.history.UndoName() == L"Gradient",
               "gradientfirst.delete-layer-undo-keeps-the-gradient");
    }
    // Switching to a new mask applies the layer's gradient; the mask's gradient is an entry of its own.
    {
        DocumentSession session = PixelEditSession();
        InsertPixels(session, SolidRaster(100, 40, 0, 0, 255, 255), L"Red");
        PendGradient(session);
        session.AddLayerMask(true);
        Expect(session.PendingGradient() == nullptr && session.IsMaskSelected(), "gradientfirst.mask-switch");
        PendGradient(session);
        session.CommitGradient();
        Expect(session.history.UndoName() == L"Gradient Mask", "gradientfirst.mask-gradient");
        session.Undo();
        session.Undo();
        Expect(session.history.UndoName() == L"Gradient", "gradientfirst.mask-switch-kept-the-gradient");
    }
}

int wmain(int argc, wchar_t** argv)
{
    // Before anything can fail a check: a CRT report, which can arrive on a concurrent raster reader, ends the run
    // with exit code 3 and no dialog (RF-702, Tests/Support/FailureReports.h).
    RedPrismTestFailureReports::RouteAwayFromDialogs();
    if (argc == 2 && std::wstring_view(argv[1]) == L"--failure-report-self-test")
    {
        return RedPrismTestFailureReports::RunSelfTest();
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--bench-record-roots")
        return RunRecordRootBench();
    if (argc > 1)
    {
        if (argc != 2 || std::wstring_view(argv[1]) != L"--stress")
        {
            std::fprintf(stderr, "Usage: DocumentTests.exe [--stress | --bench-record-roots]\n");
            return 2;
        }
        const int failures = RunRasterConcurrencyTests(256);
        return failures != 0 ? 1 : 0;
    }
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
    TestSelectionLoopsBounds();
    TestMaskPlacementHelpers();
    TestCollapsedRows();
    TestDeleteClearsClipping();
    TestNamingAndInsertion();
    TestPaletteColors();
    TestPaletteSampleAuthority();
    TestPaletteIntentAuthority();
    TestDocumentReadAuthority();
    TestCapturedToolInputs();
    TestTransformGeometry();
    TestTransformDrag();
    TestTransformSnap();
    TestTransformSession();
    TestTransformVisibility();
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
    TestPixelBuffer();
    gFailures += RunRasterConcurrencyTests(8);
    gFailures += RunHistoryRevisionTests();
    gFailures += RunHistoryStateViewTests();
    gFailures += RunHistoryEntriesTests();
    gFailures += RunRecordRootTests();
    gFailures += RunRecordOwnershipBoundsTests();
    gFailures += RunColorDisplayWorkTests();
    gFailures += RunColorDisplayPublicationTests();
    gFailures += RunRecordRootAdoptTests();
    gFailures += RunDocumentDraftTests();
    gFailures += RunDocumentFixtureTests();
    gFailures += RunLayerMetadataWorkTests();
    gFailures += RunPreservedLayerMetadataTests();
    gFailures += RunPreservedLayerOpacityTests();
    gFailures += RunLayerOpacityWorkTests();
    gFailures += RunLayerReadViewTests();
    gFailures += RunOptionalImageThumbnailTests();
    gFailures += RunPreparedHistoryTests();
    gFailures += RunHistoryPreparationFaultTests();
    gFailures += RunHistoryRetirementBoundTests();
    gFailures += RunPreparedRasterHistoryTests();
    gFailures += RunPreparedColorHistoryTests();
    gFailures += RunAdjustmentClosureTests();
    gFailures += RunAdjustmentOpeningTests();
    gFailures += RunSelectionCommandTests();
    gFailures += RunSelectionNudgeTests();
    gFailures += RunSelectionPreserveTests();
    gFailures += RunWandCancellationTests();
    gFailures += RunWandChunkBoundaryTests();
    gFailures += RunWandLayerSelectionTests();
    gFailures += RunWandCompositeSelectionTests();
    gFailures += RunSelectionMoveClosureTests();
    gFailures += RunSelectionMoveOpeningTests();
    gFailures += RunColorDismissalTests();
    gFailures += RunAdjustmentNestedTests();
    gFailures += RunMergeHistoryTests();
    gFailures += RunMergePlanProbeTests();
    gFailures += RunSessionGenerationTests();
    gFailures += RunHistoryWorkDocumentTests();
    gFailures += RunHistoryRestoreTests();
    gFailures += internal::RunImportHistoryTests();
    gFailures += internal::RunImportPublicationTests();
    gFailures += RunRasterUploadWorkTests();
    gFailures += RunRasterUploadReductionWorkTests();
    gFailures += RunRasterUploadLayoutTests();
    gFailures += RunRasterResidencyPlanTests();
    gFailures += RunRasterResidencyManifestTests();
    gFailures += RunCollapseRevisionTests();
    gFailures += RunLayerRowsWorkTests();
    gFailures += RunColorEditWorkTests();
    gFailures += RunColorEditDeferredTests();
    gFailures += RunRasterWorkTests();
    gFailures += RunRasterEditWorkTests();
    gFailures += RunGradientWorkTests();
    gFailures += RunGradientStateTests();
    gFailures += RunPixelMoveStateTests();
    gFailures += RunGradientResourceTests();
    PixelLiftReference::Run(gFailures);
    PixelMoveWorkTests::Run(gFailures);
    PixelMoveOpeningTests::Run(gFailures);
    PixelMoveOpeningWorkTests::Run(gFailures);
    gFailures += RunFilterCancellationTests();
    gFailures += RunCopyPixelsTests();
    gFailures += RunClipboardCommandTests();
    gFailures += RunClipboardPasteTests();
    gFailures += RunShapeEditWorkTests();
    gFailures += RunMaskEditWorkTests();
    gFailures += RunMaskEditWorkExtraTests();
    gFailures += RunLayerCopyWorkTests();
    gFailures += RunDistortWorkTests();
    gFailures += RunTransformPixelsWorkTests();
    gFailures += RunTransformCommitWorkTests();
    gFailures += RunTransformCancellationWorkTests();
    gFailures += RunTransformPreviewWorkTests();
    gFailures += RunTransformDeferredTests();
    gFailures += RunTransformDeferredGuardTests();
    gFailures += RunDuplicateOpeningWorkTests();
    gFailures += RunDuplicateOpeningLimitTests();
    gFailures += internal::RunDocumentLifecycleTests();
    gFailures += RunFloatingOpeningWorkTests();
    gFailures += RunFloatingMergeWorkTests();
    gFailures += RunFloatingPixelsWorkTests();
    gFailures += RunPreparedHistoryFollowingTests();
    gFailures += RunCopyPixelsTransformTests();
    gFailures += RunDocumentSizingWorkTests();
    gFailures += RunDocumentSizeHistoryTests();
    gFailures += RunSelectionStorageTests();
    gFailures += RunHistoryAccountingTests();
    gFailures += RunProjectSaveMemoryTests();
    gFailures += RunExportMemoryTests();
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
    gFailures += RunDocumentTestsCapturedCloneOffset();
    TestBlurTool();
    TestSmearWarp();
    TestGradientTool();
    TestShapeTool();
    TestEyedropper();
    TestPixelEdits();
    TestPixelEditMasksAndInvert();
    TestPixelMoves();
    TestClipboard();
    TestFloatingSelection();
    TestLevels();
    TestHueSaturation();
    TestHueRangesAndSampling();
    TestImageAdjustments();
    TestFilters();
    TestAdjustmentLayers();
    TestBlends();
    TestSmartEdits();
    TestLayerOrdering();
    TestGroupSelection();
    TestClippingToggle();
    TestClearDocument();
    TestSizeDrafts();
    TestCanvasResize();
    TestImageResize();
    TestCrop();
    TestMerge();
    TestPendingEdits();
    TestMaskFillAcrossCommits();
    TestStretchedMaskPainting();
    TestActiveLayerTargets();
    TestGradientAppliesFirst();
    TestCutWithFullHistory();
    TestTransformWaitsForPanels();
    TestOneColorEditSession();
    TestDisplayCopyFollowsEdits();
    TestSelectionLoadRefusals();
    TestCropWholePixels();
    TestDistortMasks();
    TestDocumentPixelBudget();
    TestPixelMoveTiles();
    if (gFailures != 0)
    {
        std::printf("%d document tests failed.\n", gFailures);
        return 1;
    }
    std::printf("All document tests passed.\n");
    return 0;
}

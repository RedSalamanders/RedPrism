#include "HistoryAccountingTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-accounting.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

SelectionLoops MakeLoops(double x, std::size_t outerCapacity, std::size_t pointCapacity)
{
    std::vector<SelectionLoop> loops;
    loops.reserve(outerCapacity);
    loops.emplace_back();
    loops.back().reserve(pointCapacity);
    loops.back().push_back(Vec2{x, 0});
    loops.back().push_back(Vec2{x + 4, 0});
    loops.back().push_back(Vec2{x + 4, 4});
    loops.back().push_back(Vec2{x, 4});
    return SelectionLoops(std::move(loops));
}

void CommitSelection(DocumentHistory& history, std::optional<CanvasDocument>& document, const SelectionLoops& loops)
{
    history.Begin(L"Selection", document, std::nullopt);
    document.value().selection = DocumentSelection{true, loops};
    history.End(document, std::nullopt);
}

void SelectionStorageCountsUniqueIdentitiesAndExcludesLiveDocument()
{
    DocumentHistory history(16, 1024 * 1024);
    std::optional<CanvasDocument> document = CanvasDocument{};
    const SelectionLoops first = MakeLoops(0, 2, 5);
    const SelectionLoops second = MakeLoops(8, 5, 11);
    const SelectionLoops current = MakeLoops(16, 8, 17);

    CommitSelection(history, document, first);
    CommitSelection(history, document, second);
    Expect(history.RetainedBytes(document) == first.StorageBytes(), "current-selection-identity-is-excluded");

    CommitSelection(history, document, current);
    Expect(history.RetainedBytes(document) == first.StorageBytes() + second.StorageBytes(),
           "unique-selection-identities-counted-once-and-live-identity-excluded");

    DocumentHistory sharedHistory(8, 1024 * 1024);
    std::optional<CanvasDocument> sharedDocument = CanvasDocument{};
    const SelectionLoops shared = MakeLoops(24, 4, 9);
    CommitSelection(sharedHistory, sharedDocument, shared);
    sharedDocument.value().width = 2;
    sharedHistory.Begin(L"Resize", sharedDocument, std::nullopt);
    sharedDocument.value().width = 3;
    sharedHistory.End(sharedDocument, std::nullopt);
    Expect(sharedHistory.RetainedBytes(sharedDocument) == 0,
           "selection-identity-shared-with-live-document-is-not-retained");
}

void EqualGeometryNoOpPreservesRedoAcrossDifferentCapacities()
{
    DocumentHistory history;
    std::optional<CanvasDocument> document = CanvasDocument{};
    const SelectionLoops original = MakeLoops(0, 2, 5);
    std::optional<Guid> activeLayer;
    document.value().selection = DocumentSelection{true, original};

    history.Begin(L"Resize", document, activeLayer);
    document.value().width = 2;
    history.End(document, activeLayer);
    const std::optional<DocumentHistory::Snapshot> undone = history.Undo();
    Expect(undone.has_value() && undone.value().document.value().width == 1 && history.CanRedo(), "redo-setup-undo");
    if (!undone.has_value())
    {
        return;
    }
    document = undone.value().document;

    const SelectionLoops equalWithDifferentCapacity = MakeLoops(0, 9, 19);
    Expect(equalWithDifferentCapacity == original && equalWithDifferentCapacity.Identity() != original.Identity() &&
               equalWithDifferentCapacity.StorageBytes() != original.StorageBytes(),
           "no-op-fixture-equal-geometry-distinct-storage");
    const Guid revisionBeforeNoOp = history.CurrentRevision();
    history.Begin(L"Equivalent selection", document, activeLayer);
    document.value().selection = DocumentSelection{true, equalWithDifferentCapacity};
    history.End(document, activeLayer);
    Expect(history.CurrentRevision() == revisionBeforeNoOp && history.CanRedo(), "equal-geometry-no-op-keeps-redo");
    const std::optional<DocumentHistory::Snapshot> redone = history.Redo();
    Expect(redone.has_value() && redone.value().document.value().width == 2, "redo-survives-equal-geometry-no-op");
}

void SelectionOnlyEntryTrimsToTinyBudget()
{
    DocumentHistory history(16, 1);
    std::optional<CanvasDocument> document = CanvasDocument{};
    document.value().selection = DocumentSelection{true, MakeLoops(0, 4, 9)};
    CommitSelection(history, document, MakeLoops(8, 7, 15));

    Expect(history.UndoCount() == 0 && !history.CanUndo() && history.RetainedBytes(document) == 0,
           "selection-only-entry-exceeds-tiny-history-budget");
}

ImageLayer RasterLayer(int edge, const wchar_t* name)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{static_cast<double>(edge), static_cast<double>(edge)});
    const Raster raster = Raster::Make(edge, edge, RasterFormat::Bgra8888);
    layer.asset = RasterAsset{raster, raster, name};
    return layer;
}

void UndoRestoreSurvivesTrimDroppingItsFutureEntry()
{
    DocumentHistory history(8, 256);
    DocumentFixture fixture;
    ImageLayer oldLayer = RasterLayer(1, L"Old");
    const Guid oldLayerID = oldLayer.id;
    fixture.layers.push_back(std::move(oldLayer));
    std::optional<CanvasDocument> document{std::move(fixture).Seal()};
    std::optional<Guid> activeLayer = oldLayerID;

    history.Begin(L"Replace raster", document, activeLayer);
    ImageLayer newLayer = RasterLayer(16, L"New");
    const Guid newLayerID = newLayer.id;
    DocumentFixture changed = DocumentFixture::Copy(document.value());
    changed.layers[0] = std::move(newLayer);
    document = std::move(changed).Seal();
    activeLayer = newLayerID;
    history.End(document, activeLayer);
    Expect(history.UndoCount() == 1 && history.RetainedBytes(document) <= 256,
           "small-before-snapshot-fits-history-budget");

    std::optional<DocumentHistory::Snapshot> restored = history.Undo();
    Expect(restored.has_value() && restored.value().document.has_value() &&
               restored.value().document.value().layers.size() == 1 &&
               restored.value().document.value().layers[0].asset.value().image.Width() == 1 &&
               restored.value().activeLayerID == oldLayerID,
           "undo-returns-owned-old-raster-and-active-layer");
    Expect(restored.has_value() && history.UndoCount() == 0 && !history.CanRedo() &&
               history.RetainedBytes(restored.value().document) == 0,
           "trim-may-drop-future-while-returned-restore-remains-valid");
}

void UndoRedoReturnSnapshotsAndRestoreRevisionAndActiveLayer()
{
    DocumentHistory history;
    std::optional<CanvasDocument> document = CanvasDocument{};
    std::optional<Guid> activeLayer = Guid::Generate();
    const Guid beforeRevision = history.CurrentRevision();
    const Guid beforeLayer = activeLayer.value();

    history.Begin(L"Change document", document, activeLayer);
    document.value().width = 7;
    activeLayer = Guid::Generate();
    const Guid afterLayer = activeLayer.value();
    history.End(document, activeLayer);
    const Guid afterRevision = history.CurrentRevision();

    const std::optional<DocumentHistory::Snapshot> undone = history.Undo();
    Expect(undone.has_value() && undone.value().document.value().width == 1 &&
               undone.value().activeLayerID == beforeLayer && undone.value().revision == beforeRevision &&
               history.CurrentRevision() == beforeRevision,
           "undo-returns-before-snapshot-revision-and-active-layer");
    const std::optional<DocumentHistory::Snapshot> redone = history.Redo();
    Expect(redone.has_value() && redone.value().document.value().width == 7 &&
               redone.value().activeLayerID == afterLayer && redone.value().revision == afterRevision &&
               history.CurrentRevision() == afterRevision,
           "redo-returns-after-snapshot-revision-and-active-layer");
}

void EditAdmissionIncludesWholeDocumentAndExistingHistory()
{
    DocumentHistory history;
    std::optional<CanvasDocument> document{CanvasDocument{}};
    const auto empty = history.EditMemoryBytes(document);
    DocumentFixture fixture = DocumentFixture::Copy(document.value());
    fixture.layers.push_back(ImageLayer::Blank(L"One", {2, 2}));
    document = std::move(fixture).Seal();
    const auto one = history.EditMemoryBytes(document);
    // Begin and End each copy the complete layer array, not only the edited target. This independent floor
    // includes those two arrays; the declared bound also covers growth, names and trim set bookkeeping.
    DocumentFixture manyFixture = DocumentFixture::Copy(document.value());
    manyFixture.layers.reserve(10000);
    for (int index = 1; index < 10000; ++index)
        manyFixture.layers.push_back(ImageLayer::Blank(L"Peer", {2, 2}));
    document = std::move(manyFixture).Seal();
    const auto many = history.EditMemoryBytes(document);
    Expect(empty > 0 && one > empty && many >= 2 * document->layers.size() * sizeof(ImageLayer),
           "edit-admission-whole-document-snapshot-metadata-floor");
    history.Begin(L"Change", document, {});
    document->width = 2;
    history.End(document, {});
    const auto recorded = history.EditMemoryBytes(document);
    Expect(recorded > many, "edit-admission-includes-existing-snapshot-bookkeeping");
    EditDocumentFixture(document.value(),
                        [](DocumentFixture& fixture) { fixture.layers.back().name.assign(16384, L'A'); });
    Expect(history.EditMemoryBytes(document) > recorded,
           "edit-admission-accounts-long-peer-name-not-only-active-target");
    const auto pendingBefore = history.CurrentRevision();
    history.Begin(L"Pending", document, {});
    const auto pending = history.EditMemoryBytes(document);
    Expect(pending >= recorded && history.CurrentRevision() == pendingBefore,
           "edit-admission-open-transaction-is-read-only");
    history.End(document, {});
    Expect(history.CurrentRevision() == pendingBefore, "edit-admission-query-leaves-no-op-transaction-unchanged");
}
} // namespace

int RunHistoryAccountingTests()
{
    gFailures = 0;
    SelectionStorageCountsUniqueIdentitiesAndExcludesLiveDocument();
    EqualGeometryNoOpPreservesRedoAcrossDifferentCapacities();
    SelectionOnlyEntryTrimsToTinyBudget();
    UndoRestoreSurvivesTrimDroppingItsFutureEntry();
    UndoRedoReturnSnapshotsAndRestoreRevisionAndActiveLayer();
    EditAdmissionIncludesWholeDocumentAndExistingHistory();
    return gFailures;
}

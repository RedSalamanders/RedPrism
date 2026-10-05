#include "HistoryRetirementBoundTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-retirement-bound.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void ExpectTopology(bool condition, const char* name) noexcept
{
    std::printf("%s history-topology.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void ExpectStorageSelf(bool condition, const char* name) noexcept
{
    std::printf("%s history-storage-self.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void ExpectStorageIndex(bool condition, const char* name) noexcept
{
    std::printf("%s history-storage-index.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

struct HistoryIndexCheckpoint final
{
    CancelSource* cancellation = nullptr;
    bool throwAllocation = false;
    bool reached = false;
    DocumentHistory::PrepareStage target = DocumentHistory::PrepareStage::TrimIndex;

    static void Run(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& checkpoint = *static_cast<HistoryIndexCheckpoint*>(context);
        if (stage != checkpoint.target)
            return;
        checkpoint.reached = true;
        if (checkpoint.cancellation)
            (void)checkpoint.cancellation->Cancel();
        if (checkpoint.throwAllocation)
            throw std::bad_alloc{};
    }
};

bool PublishIndexedEdit(DocumentHistory& history, const std::optional<CanvasDocument>& before,
                        const std::optional<CanvasDocument>& after, const wchar_t* name)
{
    auto publication = DocumentHistory::PrepareEdit(history.CaptureState(), before, {}, after, {}, name);
    return publication && history.CanPublish(publication.value()) && history.Publish(publication.value());
}

Raster HeldBgra(std::uint8_t seed)
{
    // One MiB exceeds the bookkeeping allowance, so an omitted pixel owner cannot pass a trivial byte floor.
    std::vector<std::uint8_t> pixels(512 * 512 * 4);
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        pixels[offset] = seed;
        pixels[offset + 1] = static_cast<std::uint8_t>(seed + 1);
        pixels[offset + 2] = static_cast<std::uint8_t>(seed + 2);
        pixels[offset + 3] = 255;
    }
    return Raster::Make(512, 512, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

CanvasDocument DocumentWithRaster(const Raster& raster, const wchar_t* name)
{
    DocumentFixture document{};
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    layer.asset = RasterAsset{raster, raster, name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

SelectionLoops SelectionWithSpareCapacity()
{
    std::vector<SelectionLoop> loops;
    loops.reserve(19);
    loops.emplace_back();
    loops.back().reserve(37);
    loops.back().push_back(Vec2{0, 0});
    loops.back().push_back(Vec2{4, 0});
    loops.back().push_back(Vec2{4, 4});
    loops.back().push_back(Vec2{0, 4});
    return SelectionLoops(std::move(loops));
}

void EmptyAndCurrentGraphHaveAllocationFloors()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> emptyDocument{CanvasDocument{}};
    const std::uint64_t empty = history.RetirementMemoryBytes(emptyDocument);
    Expect(empty > 0, "empty-history-includes-bookkeeping");

    const Raster held = HeldBgra(11);
    if (!held.HasStorage())
    {
        Expect(false, "current-raster-fixture-created");
        return;
    }
    std::optional<CanvasDocument> document{DocumentWithRaster(held, L"current")};
    const std::uint64_t current = history.RetirementMemoryBytes(document);
    Expect(current >= empty + held.ByteSize(), "current-raster-allocation-floor");

    const auto originalCapacity = document->layers.front().name.capacity();
    std::size_t reservedCapacity = 0;
    EditDocumentFixture(document.value(),
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.front().name.reserve(257);
                            reservedCapacity = fixture.layers.front().name.capacity();
                        });
    const std::uint64_t spareName = history.RetirementMemoryBytes(document);
    Expect(spareName >= current + (reservedCapacity - originalCapacity) * sizeof(wchar_t),
           "current-name-spare-capacity-floor");

    const SelectionLoops loops = SelectionWithSpareCapacity();
    document->selection = DocumentSelection{true, loops};
    const std::uint64_t withSelection = history.RetirementMemoryBytes(document);
    Expect(withSelection >= spareName + loops.StorageBytes(), "current-selection-spare-capacity-floor");
}

void CapturedPastFuturePendingAndAdditionalRasterAreIncluded()
{
    const Raster beforeRaster = HeldBgra(21);
    const Raster afterRaster = HeldBgra(31);
    const Raster secondEditRaster = HeldBgra(41);
    const Raster currentRaster = HeldBgra(51);
    const Raster additionalRaster = HeldBgra(61);
    if (!beforeRaster.HasStorage() || !afterRaster.HasStorage() || !currentRaster.HasStorage() ||
        !secondEditRaster.HasStorage() || !additionalRaster.HasStorage())
    {
        Expect(false, "history-raster-fixtures-created");
        return;
    }

    std::optional<CanvasDocument> document{DocumentWithRaster(beforeRaster, L"past name")};
    DocumentHistory history(16, 16 * 1024 * 1024);

    std::wstring operationName{L"past"};
    operationName.reserve(193);
    const std::size_t operationNameCapacity = operationName.capacity();
    history.Begin(std::move(operationName), document, std::nullopt);
    EditDocumentFixture(document.value(),
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.front().asset.value().image = afterRaster;
                            fixture.layers.front().asset.value().thumbnail = afterRaster;
                        });
    document->width = 2;
    history.End(document, std::nullopt);

    history.Begin(L"middle", document, std::nullopt);
    EditDocumentFixture(document.value(),
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.front().asset.value().image = secondEditRaster;
                            fixture.layers.front().asset.value().thumbnail = secondEditRaster;
                        });
    document->width = 3;
    history.End(document, std::nullopt);

    const std::optional<DocumentHistory::Snapshot> undo = history.Undo();
    Expect(undo.has_value() && history.CanRedo() && history.UndoCount() == 1,
           "undo-retains-both-past-and-future-snapshots");
    if (!undo.has_value() || !undo->document.has_value())
    {
        return;
    }
    document = undo->document;
    EditDocumentFixture(document.value(),
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.front().asset.value().image = currentRaster;
                            fixture.layers.front().asset.value().thumbnail = currentRaster;
                            fixture.layers.front().name.reserve(257);
                        });

    std::wstring pendingName{L"pending"};
    pendingName.reserve(257);
    const std::size_t pendingNameCapacity = pendingName.capacity();
    history.Begin(std::move(pendingName), document, std::nullopt);
    document->width = 4;
    const Guid revisionBeforeQuery = history.CurrentRevision();
    const std::uint64_t withPending = history.RetirementMemoryBytes(document);
    const std::uint64_t withAdditional = history.RetirementMemoryBytes(document, additionalRaster);

    const std::uint64_t heldPixelFloor =
        beforeRaster.ByteSize() + afterRaster.ByteSize() + secondEditRaster.ByteSize() + currentRaster.ByteSize();
    Expect(withPending >= heldPixelFloor, "past-future-current-and-pending-graph-raster-allocation-floor");
    Expect(withPending >= operationNameCapacity * sizeof(wchar_t) &&
               withPending >= pendingNameCapacity * sizeof(wchar_t),
           "retained-operation-name-allocation-floors");
    Expect(withAdditional >= withPending + additionalRaster.ByteSize(),
           "additional-raster-increases-conservative-bound");
    Expect(history.CurrentRevision() == revisionBeforeQuery && !history.CanRedo(),
           "query-does-not-alter-pending-history-state");

    history.End(document, std::nullopt);
}

void SparseSharedBaseAndTileGraphStaysUnmaterialized()
{
    const Raster sharedBase = HeldBgra(61);
    const Raster leaf = HeldBgra(71);
    if (!sharedBase.HasStorage() || !leaf.HasStorage())
    {
        Expect(false, "sparse-raster-fixtures-created");
        return;
    }
    const Raster inner =
        Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, sharedBase, 0, 0, {RasterTile{1, 1, leaf}}, 3);
    const Raster outer =
        Raster::MakeSparse(3, 3, RasterFormat::Bgra8888, sharedBase, 0, 0, {RasterTile{1, 1, inner}}, 5);
    if (!inner.HasStorage() || !outer.HasStorage())
    {
        Expect(false, "shared-sparse-graph-created");
        return;
    }

    const std::optional<CanvasDocument> document{DocumentWithRaster(outer, L"sparse")};
    DocumentHistory history;
    const std::uint64_t bound = history.RetirementMemoryBytes(document);
    Expect(bound >= sharedBase.ByteSize() + leaf.ByteSize(), "sparse-base-and-tile-pixel-floor");
    Expect(outer.IsSparse() && inner.IsSparse() && !outer.IsMaterialized() && !inner.IsMaterialized(),
           "bound-query-does-not-materialize-shared-sparse-graph");
}

void ExcessiveSparseTileDepthRefusesEstimate()
{
    Raster nested = HeldBgra(81);
    if (!nested.HasStorage())
    {
        Expect(false, "deep-sparse-leaf-created");
        return;
    }
    for (int depth = 0; depth < 130; ++depth)
    {
        nested = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, nested}}, 0);
        if (!nested.HasStorage())
        {
            Expect(false, "deep-sparse-tile-chain-created");
            return;
        }
    }

    const std::optional<CanvasDocument> document{DocumentWithRaster(nested, L"deep")};
    DocumentHistory history;
    Expect(history.RetirementMemoryBytes(document) == 0, "sparse-tile-depth-over-128-refuses-estimate");
    Expect(nested.IsSparse() && !nested.IsMaterialized(), "deep-depth-query-keeps-sparse-chain-unmaterialized");
}

void LargeHistoryNamesHaveIndependentCapacityFloors()
{
    DocumentHistory history;
    const std::optional<CanvasDocument> before{CanvasDocument{}};
    auto after = before;
    after->width = 2;
    const auto initial = history.RetirementMemoryBytes(before);
    std::wstring pendingName{L"pending"};
    pendingName.reserve(262144);
    const auto pendingCapacity = pendingName.capacity();
    history.Begin(std::move(pendingName), before, {});
    Expect(history.RetirementMemoryBytes(before) >= initial + pendingCapacity * sizeof(wchar_t),
           "large-pending-name-retained-capacity-floor");
    history.End(after, {});
    history.Reset();
    std::wstring entryName(262144, L'x');
    history.Begin(std::move(entryName), before, {});
    history.End(after, {});
    Expect(history.RetirementMemoryBytes(after) >= initial + history.UndoName().capacity() * sizeof(wchar_t),
           "large-entry-name-retained-capacity-floor");
}

void ShapeSourceStorageParticipatesInHistoryBudget()
{
    constexpr std::array<std::uint8_t, 4> assetBytes{1, 2, 3, 255};
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, assetBytes.data(), assetBytes.size());
    const Raster shapeSource = HeldBgra(12);
    std::optional<CanvasDocument> before{DocumentWithRaster(image, L"Shape source cache")};
    EditDocumentFixture(before.value(),
                        [&shapeSource](DocumentFixture& fixture) { fixture.layers[0].shapeImage = shapeSource; });
    auto after = before;
    EditDocumentFixture(after.value(),
                        [](DocumentFixture& fixture)
                        {
                            fixture.layers[0].name = L"Renamed shape source";
                            fixture.layers[0].shapeImage = Raster{};
                        });
    DocumentHistory retained;
    retained.Begin(L"Retire shape source", before, {});
    retained.End(after, {});
    constexpr std::size_t expectedBytes = 512 * 512 * 4;
    Expect(shapeSource.ByteSize() == expectedBytes && retained.UndoCount() == 1 &&
               retained.RetainedBytes(after) == expectedBytes,
           "shape-source-only-history-owner-counts-one-literal-mib");
    Expect(retained.RetainedBytes(before) == 0, "live-shape-source-owner-excludes-the-same-history-storage");

    DocumentHistory directBudget(100, expectedBytes / 2);
    directBudget.Begin(L"Budgeted shape source", before, {});
    directBudget.End(after, {});
    Expect(directBudget.UndoCount() == 0 && directBudget.RetainedBytes(after) == 0,
           "shape-source-direct-trim-drops-entry-above-half-mib-budget");
    DocumentHistory preparedBudget(100, expectedBytes / 2);
    auto publication =
        DocumentHistory::PrepareEdit(preparedBudget.CaptureState(), before, {}, after, {}, L"Prepared shape source");
    Expect(publication && preparedBudget.Publish(publication.value()) && preparedBudget.UndoCount() == 0 &&
               preparedBudget.RetainedBytes(after) == 0,
           "shape-source-prepared-trim-has-the-same-literal-budget-decision");
}

void CachedHistoryBoundsPreserveSourceAndPublication()
{
    DocumentHistory::Snapshot absent;
    const auto absentBounds = absent.OwnershipBounds();
    Expect(absentBounds && absentBounds->ownershipBytes >= sizeof(DocumentHistory::Snapshot) &&
               absentBounds->metadataBytes <= absentBounds->ownershipBytes && absentBounds->rasterNodes == 0,
           "cached-absent-snapshot-has-complete-own-header-bound");

    const Raster base = HeldBgra(9);
    const Raster sparse = Raster::MakeSparse(512, 512, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    const std::optional<CanvasDocument> before{DocumentWithRaster(sparse, L"Cached history source")};
    auto after = before;
    after->width = 2;
    DocumentHistory history;
    history.Begin(L"Cached bound edit", before, {});
    history.End(after, {});
    const auto state = history.CaptureState();
    const auto retainedRevision = history.CurrentRevision();
    const auto graphBound = history.RetirementMemoryBytes(after);
    const auto editBound = history.EditMemoryBytes(after);
    Expect(history.UndoCount() == 1 && graphBound >= sparse.RetirementMemoryBytes() && editBound != 0 &&
               !sparse.IsMaterialized(),
           "cached-stack-admission-covers-held-sparse-source-without-materializing");
    bool repeatedStable = graphBound != 0;
    for (unsigned int query = 0; query < 1024; ++query)
        repeatedStable = repeatedStable && history.RetirementMemoryBytes(after) == graphBound &&
                         history.EditMemoryBytes(after) == editBound;
    Expect(repeatedStable && history.IsCurrent(state) && history.CurrentRevision() == retainedRevision,
           "cached-stack-repeated-admission-queries-preserve-original-roots-and-epoch");
    Expect(sparse.Data() && sparse.IsMaterialized() && history.RetirementMemoryBytes(after) == graphBound &&
               history.EditMemoryBytes(after) == editBound && history.IsCurrent(state),
           "cached-stack-potential-assembly-charge-stays-valid-after-materialization");

    const auto restored = history.Undo();
    Expect(restored && restored->document == before && history.CanRedo() && history.UndoCount() == 0 &&
               history.RetirementMemoryBytes(before) != 0 && history.RestoreMemoryBytes(before, true) != 0,
           "cached-future-stack-bounds-preserve-exact-undo-source-and-redo-admission");
    const auto redone = history.Redo();
    Expect(redone && redone->document == after && history.UndoCount() == 1 && !history.CanRedo() &&
               history.RetirementMemoryBytes(after) != 0,
           "cached-past-stack-bounds-follow-redo-with-original-document-values");
    const auto baseline = history.RetirementMemoryBytes(std::nullopt);
    std::wstring pendingName(128 * 1024, L'p');
    history.Begin(std::move(pendingName), before, {});
    Expect(history.RetirementMemoryBytes(std::nullopt) >= baseline + 128 * 1024 * sizeof(wchar_t) &&
               history.EditMemoryBytes(before) != 0,
           "cached-pending-bound-retains-complete-name-and-snapshot-owners");
    history.End(before, {});
    history.Reset();
    Expect(history.RetirementMemoryBytes(std::nullopt) != 0 && history.RetirementMemoryBytes(std::nullopt) < baseline &&
               !history.CanUndo() && !history.CanRedo(),
           "cached-reset-removes-old-stack-and-pending-ownership-from-admission");

    Raster deep = Raster::Make(1, 1, RasterFormat::Bgra8888);
    for (int depth = 0; depth < 130; ++depth)
        deep = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, deep}}, 0);
    const std::optional<CanvasDocument> unavailable{DocumentWithRaster(deep, L"Unavailable snapshot")};
    DocumentHistory::Snapshot unavailableSnapshot{unavailable, {}, Guid::Generate()};
    Expect(deep.HasStorage() && !deep.RetirementMemoryBytes() && !unavailableSnapshot.OwnershipBounds(),
           "cached-snapshot-propagates-unavailable-complete-child-bound");
    auto unavailableAfter = unavailable;
    unavailableAfter->width = 2;
    DocumentHistory unmeasurableHistory;
    unmeasurableHistory.Begin(L"Unmeasurable", unavailable, {});
    unmeasurableHistory.End(unavailableAfter, {});
    const auto unmeasurableState = unmeasurableHistory.CaptureState();
    const auto unmeasurableRevision = unmeasurableHistory.CurrentRevision();
    Expect(unmeasurableHistory.UndoCount() == 1 && unmeasurableHistory.RetirementMemoryBytes(std::nullopt) == 0 &&
               unmeasurableHistory.EditMemoryBytes(std::nullopt) == 0 &&
               unmeasurableHistory.RestoreMemoryBytes(std::nullopt, false) == 0,
           "cached-stack-refuses-unavailable-history-even-without-live-document");
    Expect(unmeasurableHistory.IsCurrent(unmeasurableState) &&
               unmeasurableHistory.CurrentRevision() == unmeasurableRevision && !deep.IsMaterialized(),
           "cached-unavailable-queries-never-mutate-history-or-expand-sparse-source");
}

void CachedRasterBoundsCoverAdoptionCachesAndSharedDags()
{
    PixelBuffer pixels = PixelBuffer::TryAllocate(4);
    Expect(static_cast<bool>(pixels), "adopt-buffer-created");
    if (!pixels)
        return;
    const auto allocationBytes = pixels.AllocationBytes();
    pixels.Data()[0] = 1;
    pixels.Data()[1] = 2;
    pixels.Data()[2] = 3;
    pixels.Data()[3] = 255;
    const auto adopted = Raster::Adopt(1, 1, RasterFormat::Bgra8888, std::move(pixels));
    Expect(adopted.RetirementMemoryBytes() >= allocationBytes && adopted.RetirementNodeCount() == 1,
           "adopted-raster-caches-complete-capacity-bound");
    const auto sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, adopted}}, 0);
    const auto before = sparse.RetirementMemoryBytes();
    Expect(before > adopted.RetirementMemoryBytes() && !sparse.IsMaterialized(),
           "sparse-bound-includes-held-child-and-possible-cache");
    Expect(sparse.Data() && sparse.IsMaterialized() && sparse.RetirementMemoryBytes() == before,
           "materialization-cannot-increase-construction-time-bound");

    const std::array<std::uint8_t, 8> leafBytes{4, 5, 6, 255, 7, 8, 9, 255};
    Raster shared = Raster::Make(2, 1, RasterFormat::Bgra8888, leafBytes.data(), leafBytes.size());
    for (int level = 0; level < 66; ++level)
    {
        // Two disjoint windows retain the same previous immutable node. There are only 67 distinct records,
        // but occurrence expansion exceeds uint64. Cached bounds must refuse it without recursively expanding.
        shared = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                                    {RasterTile{0, 0, shared, 0, 0, 1, 1}, RasterTile{1, 0, shared, 1, 0, 1, 1}}, 0);
    }
    const std::optional<CanvasDocument> document{DocumentWithRaster(shared, L"shared DAG")};
    DocumentHistory history;
    Expect(shared.HasStorage() && !shared.IsMaterialized() && shared.RetirementMemoryBytes() == 0 &&
               shared.RetirementNodeCount() == 0 && history.RetirementMemoryBytes(document) == 0 &&
               history.EditMemoryBytes(document) == 0,
           "cached-overflow-refuses-shared-dag-without-owner-expansion");
}

void CachedRasterSelfStorageBounds()
{
    const Raster empty;
    ExpectStorageSelf(empty.StorageRetirementMemoryBytes() == 0, "empty-handle-has-zero-self-storage-bound");

    const Raster dense = Raster::Make(8, 8, RasterFormat::Bgra8888);
    const Raster sparseEmpty = Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    const auto denseSelf = dense.StorageRetirementMemoryBytes();
    const auto sparseSelf = sparseEmpty.StorageRetirementMemoryBytes();
    ExpectStorageSelf(dense.HasStorage() && sparseEmpty.HasStorage() && denseSelf >= dense.ByteSize() &&
                          sparseSelf >= dense.ByteSize() &&
                          dense.Tiles().capacity() == sparseEmpty.Tiles().capacity() && denseSelf == sparseSelf,
                      "equivalent-dense-and-empty-sparse-self-bounds-cover-same-logical-pixel-floor");

    PixelBuffer adoptedPixels = PixelBuffer::TryAllocate(8 * 8 * 4);
    if (!adoptedPixels)
    {
        ExpectStorageSelf(false, "adoption-buffer-created");
        return;
    }
    const std::uint64_t adoptedAllocation = adoptedPixels.AllocationBytes();
    for (std::size_t offset = 0; offset < adoptedPixels.Size(); ++offset)
    {
        adoptedPixels.Data()[offset] = static_cast<std::uint8_t>(offset & 0xffu);
    }
    const Raster adopted = Raster::Adopt(8, 8, RasterFormat::Bgra8888, std::move(adoptedPixels));
    ExpectStorageSelf(adopted.HasStorage() && adopted.StorageRetirementMemoryBytes() >= adoptedAllocation &&
                          adopted.StorageRetirementMemoryBytes() >= adopted.ByteSize(),
                      "adopted-self-bound-covers-actual-allocation-capacity");

    const Raster smallLeaf = Raster::Make(1, 1, RasterFormat::Bgra8888);
    const Raster largeLeaf = Raster::Make(64, 64, RasterFormat::Bgra8888);
    const Raster nested =
        Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, smallLeaf, 0, 0, 1, 1}}, 0);
    const auto makeOwner = [](const Raster& child, std::size_t occurrences)
    {
        std::vector<RasterTile> tiles;
        tiles.reserve(4);
        for (std::size_t index = 0; index < occurrences; ++index)
        {
            tiles.push_back(RasterTile{static_cast<int>(index), 0, child, 0, 0, 1, 1});
        }
        return Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, std::move(tiles), 0);
    };
    const Raster oneNestedOwner = makeOwner(nested, 1);
    const Raster sharedNestedOwners = makeOwner(nested, 2);
    const Raster sharedSmallChildOwners = makeOwner(smallLeaf, 2);
    const Raster sharedLargeChildOwners = makeOwner(largeLeaf, 2);
    const Raster noTileOwner = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    const bool equalTwoTileCapacities =
        sharedNestedOwners.Tiles().capacity() == sharedSmallChildOwners.Tiles().capacity() &&
        sharedNestedOwners.Tiles().capacity() == sharedLargeChildOwners.Tiles().capacity();
    ExpectStorageSelf(
        nested.HasStorage() && sharedNestedOwners.HasStorage() && sharedSmallChildOwners.HasStorage() &&
            sharedLargeChildOwners.HasStorage() && equalTwoTileCapacities &&
            largeLeaf.StorageRetirementMemoryBytes() > sharedLargeChildOwners.StorageRetirementMemoryBytes() &&
            sharedNestedOwners.StorageRetirementMemoryBytes() ==
                sharedSmallChildOwners.StorageRetirementMemoryBytes() &&
            sharedNestedOwners.StorageRetirementMemoryBytes() == sharedLargeChildOwners.StorageRetirementMemoryBytes(),
        "same-two-window-self-bound-excludes-small-large-and-shared-nested-child-storage");

    const auto oneTileCapacity = oneNestedOwner.Tiles().capacity();
    const auto sharedTileCapacity = sharedNestedOwners.Tiles().capacity();
    const bool sharedCapacityOrdered = sharedTileCapacity >= oneTileCapacity;
    const auto sharedCapacityDelta = sharedCapacityOrdered ? sharedTileCapacity - oneTileCapacity : 0;
    ExpectStorageSelf(
        oneNestedOwner.HasStorage() && sharedNestedOwners.HasStorage() && sharedCapacityOrdered &&
            sharedNestedOwners.StorageRetirementMemoryBytes() >= oneNestedOwner.StorageRetirementMemoryBytes() &&
            sharedNestedOwners.StorageRetirementMemoryBytes() - oneNestedOwner.StorageRetirementMemoryBytes() ==
                sharedCapacityDelta * sizeof(RasterTile),
        "shared-nested-occurrence-affects-self-only-by-actual-tile-capacity");

    const auto noTileCapacity = noTileOwner.Tiles().capacity();
    const bool tileCapacityOrdered = oneTileCapacity >= noTileCapacity;
    const auto tileCapacityDelta = tileCapacityOrdered ? oneTileCapacity - noTileCapacity : 0;
    ExpectStorageSelf(noTileOwner.HasStorage() && tileCapacityOrdered && tileCapacityDelta > 0 &&
                          oneNestedOwner.StorageRetirementMemoryBytes() >= noTileOwner.StorageRetirementMemoryBytes() &&
                          oneNestedOwner.StorageRetirementMemoryBytes() - noTileOwner.StorageRetirementMemoryBytes() ==
                              tileCapacityDelta * sizeof(RasterTile),
                      "equivalent-sparse-owner-difference-is-only-literal-tile-capacity");

    const auto nestedSelfBefore = nested.StorageRetirementMemoryBytes();
    const auto sharedSelfBefore = sharedNestedOwners.StorageRetirementMemoryBytes();
    const bool assembled = sharedNestedOwners.Data() != nullptr;
    ExpectStorageSelf(assembled && sharedNestedOwners.IsMaterialized() && nested.IsMaterialized() &&
                          nested.StorageRetirementMemoryBytes() == nestedSelfBefore &&
                          sharedNestedOwners.StorageRetirementMemoryBytes() == sharedSelfBefore,
                      "cached-self-bounds-stay-stable-before-and-after-real-materialization");
}

void SelfStorageBoundsSurviveWholeGraphRefusal()
{
    Raster depth129 = Raster::Make(1, 1, RasterFormat::Bgra8888);
    for (int depth = 0; depth < 129; ++depth)
    {
        depth129 =
            Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, depth129, 0, 0, 1, 1}}, 0);
    }
    ExpectStorageSelf(depth129.HasStorage() && depth129.RetirementMemoryBytes() == 0 &&
                          depth129.StorageRetirementMemoryBytes() > 0 && !depth129.IsMaterialized(),
                      "depth-129-self-bound-survives-unavailable-whole-graph-bound");

    const std::array<std::uint8_t, 8> leafPixels{1, 2, 3, 255, 4, 5, 6, 255};
    Raster shared = Raster::Make(2, 1, RasterFormat::Bgra8888, leafPixels.data(), leafPixels.size());
    for (int level = 0; level < 66; ++level)
    {
        shared = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                                    {RasterTile{0, 0, shared, 0, 0, 1, 1}, RasterTile{1, 0, shared, 1, 0, 1, 1}}, 0);
    }
    ExpectStorageSelf(shared.HasStorage() && shared.RetirementMemoryBytes() == 0 &&
                          shared.StorageRetirementMemoryBytes() > 0 && !shared.IsMaterialized(),
                      "exponential-shared-dag-self-bound-survives-overflowed-whole-graph-bound");
}

Raster TinyBgra(int width, int height, std::uint8_t seed)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index] = seed;
        pixels[index + 1] = static_cast<std::uint8_t>(seed + 1);
        pixels[index + 2] = static_cast<std::uint8_t>(seed + 2);
        pixels[index + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

bool HasEachExpectedRasterOnce(const HistoryStorageTopology& topology, const void* const* identities,
                               std::size_t expectedCount) noexcept
{
    if (topology.RasterOwners().size() != expectedCount)
        return false;
    for (std::size_t expectedIndex = 0; expectedIndex < expectedCount; ++expectedIndex)
    {
        std::size_t matches = 0;
        for (const Raster& raster : topology.RasterOwners())
        {
            if (raster.Identity() == identities[expectedIndex])
                ++matches;
        }
        if (matches != 1)
            return false;
    }
    for (const Raster& raster : topology.RasterOwners())
    {
        std::size_t matches = 0;
        for (std::size_t expectedIndex = 0; expectedIndex < expectedCount; ++expectedIndex)
        {
            if (raster.Identity() == identities[expectedIndex])
                ++matches;
        }
        if (matches != 1)
            return false;
    }
    return true;
}

SelectionLoops TopologySelection(std::size_t outerCapacity, std::size_t pointCapacity)
{
    std::vector<SelectionLoop> loops;
    loops.reserve(outerCapacity);
    loops.emplace_back();
    loops.back().reserve(pointCapacity);
    loops.back().push_back(Vec2{1, 2});
    loops.back().push_back(Vec2{5, 2});
    loops.back().push_back(Vec2{5, 7});
    loops.back().push_back(Vec2{1, 7});
    return SelectionLoops(std::move(loops));
}

std::uint64_t LiteralSelectionCapacityBytes(const SelectionLoops& loops) noexcept
{
    std::uint64_t bytes = sizeof(std::vector<SelectionLoop>) + loops.Get().capacity() * sizeof(SelectionLoop);
    for (const SelectionLoop& loop : loops)
        bytes += loop.capacity() * sizeof(Vec2);
    return bytes;
}

void TopologyCapturesNestedSharedGraphAndEveryLayerOwner()
{
    const Raster base = TinyBgra(2, 1, 11);
    const Raster leaf = TinyBgra(2, 1, 21);
    const Raster inner =
        Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                           {RasterTile{0, 0, leaf, 0, 0, 1, 1}, RasterTile{1, 0, leaf, 1, 0, 1, 1}}, 3);
    const Raster outer = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0,
                                            {RasterTile{1, 0, inner}, RasterTile{3, 1, leaf, 1, 0, 1, 1}}, 5);
    const Raster imageThumbnail = TinyBgra(1, 1, 31);
    const Raster maskImage = TinyBgra(2, 1, 41);
    const Raster maskThumbnail = TinyBgra(1, 1, 51);
    const Raster shapeSource = TinyBgra(1, 2, 61);
    if (!base.HasStorage() || !leaf.HasStorage() || !inner.HasStorage() || !outer.HasStorage() ||
        !imageThumbnail.HasStorage() || !maskImage.HasStorage() || !maskThumbnail.HasStorage() ||
        !shapeSource.HasStorage())
    {
        ExpectTopology(false, "nested-owner-fixtures-created");
        return;
    }

    ImageLayer layer = ImageLayer::Blank(L"topology owner layer", Vec2{4, 2});
    layer.asset = RasterAsset{outer, imageThumbnail, L"image asset"};
    LayerMask mask;
    mask.asset = RasterAsset{maskImage, maskThumbnail, L"mask asset"};
    layer.mask = std::move(mask);
    layer.shapeImage = shapeSource;
    DocumentFixture fixture{};
    fixture.layers.push_back(std::move(layer));
    fixture.selection = DocumentSelection{true, TopologySelection(17, 29)};
    const CanvasDocument document = std::move(fixture).Seal();

    const void* const expectedRasterIdentities[]{
        outer.Identity(),          base.Identity(),      inner.Identity(),         leaf.Identity(),
        imageThumbnail.Identity(), maskImage.Identity(), maskThumbnail.Identity(), shapeSource.Identity()};
    constexpr std::uint64_t kLiteralResidentFloor = 8 + 8 + 4 + 8 + 4 + 8;
    const auto topology = HistoryStorageTopology::Prepare(document);
    const std::uint64_t minimumTopologyHeader = sizeof(HistoryStorageTopology) + 64 + 8 * sizeof(Raster);
    ExpectTopology(topology && topology->Matches(document) &&
                       HasEachExpectedRasterOnce(*topology, expectedRasterIdentities, 8),
                   "nested-shared-dag-and-all-layer-raster-owners-appear-once");
    ExpectTopology(topology && topology->MetadataBytes() >= minimumTopologyHeader &&
                       topology->RetirementMemoryBytes() >= topology->MetadataBytes() + kLiteralResidentFloor,
                   "topology-metadata-and-retirement-cover-independent-literal-floor");
    ExpectTopology(topology && topology->SelectionOwner().Identity() == document.selection->loops.Identity() &&
                       topology->SelectionOwner().StorageBytes() ==
                           LiteralSelectionCapacityBytes(document.selection->loops),
                   "topology-retains-selection-owner-and-literal-spare-capacities");
    ExpectTopology(!inner.IsMaterialized() && !outer.IsMaterialized(),
                   "nested-topology-construction-does-not-materialize-sparse-pixels");
}

void TopologyOwnsSourcesAndTracksLateMaterialization()
{
    std::shared_ptr<const HistoryStorageTopology> topology;
    std::optional<DocumentHistory::Snapshot> retainedSnapshot;
    const void* expectedIdentities[8]{};
    const void* selectionIdentity = nullptr;
    {
        const Raster base = TinyBgra(2, 1, 71);
        const Raster leaf = TinyBgra(2, 1, 81);
        const Raster nested =
            Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                               {RasterTile{0, 0, leaf, 0, 0, 1, 1}, RasterTile{1, 0, leaf, 1, 0, 1, 1}}, 2);
        const Raster sparse = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0,
                                                 {RasterTile{1, 0, nested}, RasterTile{3, 1, leaf, 1, 0, 1, 1}}, 4);
        const Raster imageThumbnail = TinyBgra(1, 1, 91);
        const Raster maskImage = TinyBgra(1, 2, 101);
        const Raster maskThumbnail = TinyBgra(1, 1, 111);
        const Raster shapeSource = TinyBgra(2, 1, 121);
        ImageLayer layer = ImageLayer::Blank(L"source owner", Vec2{4, 2});
        layer.asset = RasterAsset{sparse, imageThumbnail, L"source image"};
        LayerMask mask;
        mask.asset = RasterAsset{maskImage, maskThumbnail, L"source mask"};
        layer.mask = std::move(mask);
        layer.shapeImage = shapeSource;
        DocumentFixture fixture{};
        fixture.layers.push_back(std::move(layer));
        fixture.selection = DocumentSelection{false, TopologySelection(13, 23)};
        const CanvasDocument document = std::move(fixture).Seal();
        expectedIdentities[0] = sparse.Identity();
        expectedIdentities[1] = base.Identity();
        expectedIdentities[2] = nested.Identity();
        expectedIdentities[3] = leaf.Identity();
        expectedIdentities[4] = imageThumbnail.Identity();
        expectedIdentities[5] = maskImage.Identity();
        expectedIdentities[6] = maskThumbnail.Identity();
        expectedIdentities[7] = shapeSource.Identity();
        selectionIdentity = document.selection->loops.Identity();
        topology = HistoryStorageTopology::Prepare(document);
        ExpectTopology(topology && topology->Matches(document), "prepared-topology-matches-source-document");
        DocumentHistory::Snapshot snapshot;
        snapshot.document = document;
        snapshot.storageTopology = topology;
        retainedSnapshot = std::move(snapshot);
    }

    ExpectTopology(retainedSnapshot && retainedSnapshot->storageTopology &&
                       retainedSnapshot->storageTopology->Matches(retainedSnapshot->document) &&
                       retainedSnapshot->OwnershipBounds().has_value(),
                   "topology-matches-retained-old-root-after-source-document-release");
    if (retainedSnapshot && retainedSnapshot->storageTopology && retainedSnapshot->document)
    {
        auto mismatchedDocument = retainedSnapshot->document;
        EditDocumentFixture(mismatchedDocument.value(), [](DocumentFixture& fixture)
                            { fixture.layers.front().name = L"Different immutable root"; });
        retainedSnapshot->document = std::move(mismatchedDocument);
        ExpectTopology(!retainedSnapshot->storageTopology->Matches(retainedSnapshot->document) &&
                           !retainedSnapshot->OwnershipBounds().has_value(),
                       "stale-topology-refuses-mismatched-root-bound");
    }
    retainedSnapshot.reset();
    const std::optional<CanvasDocument> absent;
    ExpectTopology(topology && !topology->Matches(absent) &&
                       HasEachExpectedRasterOnce(*topology, expectedIdentities, 8) &&
                       topology->SelectionOwner().Identity() == selectionIdentity,
                   "topology-alone-owns-all-raster-and-selection-identities-after-source-release");
    const auto originalRetirementBound = topology ? topology->RetirementMemoryBytes() : 0;
    bool foundSparse = false;
    bool identitiesStable = true;
    if (!topology)
    {
        ExpectTopology(false, "late-materialization-topology-was-prepared");
        return;
    }
    for (const Raster& raster : topology->RasterOwners())
    {
        identitiesStable = identitiesStable && raster.Identity() != nullptr;
        if (raster.Identity() == expectedIdentities[0])
        {
            foundSparse = true;
            ExpectTopology(!raster.IsMaterialized() && raster.ByteSize() == 0,
                           "sparse-topology-owner-starts-unmaterialized");
            ExpectTopology(raster.Data() != nullptr && raster.IsMaterialized() && raster.ByteSize() == 32,
                           "sparse-topology-owner-reports-late-literal-resident-bytes");
        }
    }
    const Raster replacement = TinyBgra(1, 1, 131);
    bool replacementIdentityIsDistinct = replacement.HasStorage();
    for (const void* identity : expectedIdentities)
        replacementIdentityIsDistinct = replacementIdentityIsDistinct && replacement.Identity() != identity;
    ExpectTopology(foundSparse && identitiesStable && replacementIdentityIsDistinct && topology &&
                       topology->RetirementMemoryBytes() == originalRetirementBound &&
                       HasEachExpectedRasterOnce(*topology, expectedIdentities, 8),
                   "late-materialization-keeps-topology-owners-and-construction-upper-bound");
}

void TopologyMatchesAbsentDefaultEmptyAndCancellation()
{
    const std::optional<CanvasDocument> absent;
    const auto absentTopology = HistoryStorageTopology::Prepare(absent);
    const std::optional<CanvasDocument> defaultDocument{CanvasDocument{}};
    const auto defaultTopology = HistoryStorageTopology::Prepare(defaultDocument);
    ExpectTopology(absentTopology && absentTopology->Matches(absent) && absentTopology->RasterOwners().empty() &&
                       absentTopology->SelectionOwner().Identity() == nullptr && absentTopology->MetadataBytes() > 0 &&
                       absentTopology->RetirementMemoryBytes() >= absentTopology->MetadataBytes(),
                   "absent-source-has-empty-valid-topology");
    ExpectTopology(defaultTopology && defaultTopology->Matches(defaultDocument) && !defaultTopology->Matches(absent) &&
                       defaultTopology->RasterOwners().empty() && defaultTopology->RetirementMemoryBytes() > 0,
                   "default-empty-document-remains-distinct-from-absent-source");

    auto explicitEmptySelection = defaultDocument;
    explicitEmptySelection->selection = DocumentSelection{};
    const auto emptySelectionTopology = HistoryStorageTopology::Prepare(explicitEmptySelection);
    ExpectTopology(emptySelectionTopology && emptySelectionTopology->Matches(explicitEmptySelection) &&
                       emptySelectionTopology->Matches(defaultDocument) &&
                       emptySelectionTopology->RasterOwners().empty() &&
                       emptySelectionTopology->SelectionOwner().Identity() == nullptr,
                   "empty-selection-adds-no-storage-owner");

    CancelSource cancellation;
    const bool cancellationRequested = cancellation.Cancel();
    ExpectTopology(cancellationRequested && !HistoryStorageTopology::Prepare(defaultDocument, cancellation.Token()),
                   "pre-cancelled-topology-preparation-refuses-result");
}

void PreparedTopologyChargesCompleteNestedGraphAgainstEmptyLiveRoot()
{
    const Raster base = TinyBgra(2, 1, 141);
    const Raster leaf = TinyBgra(2, 1, 151);
    const Raster nested = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                                             {RasterTile{0, 0, leaf}, RasterTile{1, 0, leaf, 1, 0, 1, 1}}, 2);
    const Raster sparse = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0,
                                             {RasterTile{1, 0, nested}, RasterTile{3, 1, leaf, 1, 0, 1, 1}}, 4);
    const Raster imageThumbnail = TinyBgra(1, 1, 161);
    const Raster maskImage = TinyBgra(2, 1, 171);
    const Raster maskThumbnail = TinyBgra(1, 1, 181);
    const Raster shapeSource = TinyBgra(1, 2, 191);
    ImageLayer layer = ImageLayer::Blank(L"prepared nested owner", Vec2{4, 2});
    layer.asset = RasterAsset{sparse, imageThumbnail, L"prepared image"};
    LayerMask mask;
    mask.asset = RasterAsset{maskImage, maskThumbnail, L"prepared mask"};
    layer.mask = std::move(mask);
    layer.shapeImage = shapeSource;
    DocumentFixture fixture{};
    fixture.layers.push_back(std::move(layer));
    const std::optional<CanvasDocument> before{std::move(fixture).Seal()};
    const std::optional<CanvasDocument> emptyLiveRoot{CanvasDocument{}};
    auto after = emptyLiveRoot;
    after->width = 2;

    // Independent sum of the distinct dense buffers: base 8, leaf 8, image thumbnail 4,
    // mask image 8, mask thumbnail 4, and shape source 8 bytes.
    constexpr std::uint64_t kLiteralNestedOwnerCharge = 8 + 8 + 4 + 8 + 4 + 8;
    DocumentHistory history;
    auto publication = DocumentHistory::PrepareEdit(history.CaptureState(), before, {}, after, {},
                                                    L"Prepared nested topology admission");
    const bool published = publication && history.Publish(publication.value());
    ExpectTopology(published && history.UndoCount() == 1 &&
                       history.RetainedBytes(emptyLiveRoot) == kLiteralNestedOwnerCharge && !sparse.IsMaterialized() &&
                       !nested.IsMaterialized(),
                   "prepared-publication-charges-complete-nested-owners-with-empty-live-root");

    DocumentHistory direct;
    direct.Begin(L"Reference complete nested topology", before, {});
    direct.End(after, {});
    ExpectTopology(direct.UndoCount() == 1 && direct.RetainedBytes(emptyLiveRoot) == kLiteralNestedOwnerCharge,
                   "reference-trim-charges-the-same-complete-nested-owners");
    DocumentHistory directBudget(100, kLiteralNestedOwnerCharge - 1);
    directBudget.Begin(L"Reference nested budget", before, {});
    directBudget.End(after, {});
    ExpectTopology(directBudget.UndoCount() == 0 && directBudget.RetainedBytes(emptyLiveRoot) == 0,
                   "reference-nested-budget-refuses-one-byte-over-literal-charge");

    auto renamed = before;
    EditDocumentFixture(renamed.value(),
                        [](DocumentFixture& value) { value.layers[0].name = L"Renamed nested owner"; });
    DocumentHistory live(100, 0);
    auto first = DocumentHistory::PrepareEdit(live.CaptureState(), {}, {}, before, {}, L"Prepared live nested source");
    const bool firstPublished = first && live.Publish(first.value());
    ExpectTopology(firstPublished && live.UndoCount() == 1 && live.RetainedBytes(before) == 0,
                   "prepared-nested-history-excludes-every-live-descendant");
    live.Begin(L"Mixed reference rename", before, {});
    live.End(renamed, {});
    ExpectTopology(firstPublished && live.UndoCount() == 2 && live.RetainedBytes(renamed) == 0 &&
                       !sparse.IsMaterialized() && !nested.IsMaterialized(),
                   "mixed-prepared-reference-trim-keeps-live-nested-owners-with-zero-budget");
}

void SelectionTopologyHonorsLiveExclusionInDirectAndPreparedTrim()
{
    const SelectionLoops loops = TopologySelection(37, 257);
    const std::uint64_t literalSelectionBytes = LiteralSelectionCapacityBytes(loops);
    ExpectTopology(literalSelectionBytes > 1 && loops.StorageBytes() == literalSelectionBytes,
                   "selection-charge-matches-literal-outer-and-inner-capacities");

    std::optional<CanvasDocument> before{CanvasDocument{}};
    before->selection = DocumentSelection{true, loops};
    auto sameSelectionAfter = before;
    sameSelectionAfter->width = 2;

    DocumentHistory directLive(8, 0);
    directLive.Begin(L"Live selection remains excluded", before, {});
    directLive.End(sameSelectionAfter, {});
    ExpectTopology(directLive.UndoCount() == 1 && directLive.RetainedBytes(sameSelectionAfter) == 0,
                   "direct-trim-excludes-current-selection-capacity-by-identity");

    DocumentHistory preparedLive(8, 0);
    auto livePublication = DocumentHistory::PrepareEdit(preparedLive.CaptureState(), before, {}, sameSelectionAfter, {},
                                                        L"Prepared live selection remains excluded");
    const bool livePublished = livePublication && preparedLive.Publish(livePublication.value());
    ExpectTopology(livePublished && preparedLive.UndoCount() == 1 &&
                       preparedLive.RetainedBytes(sameSelectionAfter) == 0,
                   "prepared-trim-excludes-current-selection-capacity-by-identity");

    auto selectionRemovedAfter = before;
    selectionRemovedAfter->selection.reset();
    selectionRemovedAfter->width = 3;
    DocumentHistory directRetired(8, 1);
    directRetired.Begin(L"Retired selection is charged", before, {});
    directRetired.End(selectionRemovedAfter, {});
    ExpectTopology(directRetired.UndoCount() == 0 && directRetired.RetainedBytes(selectionRemovedAfter) == 0,
                   "direct-trim-drops-history-only-selection-capacity");

    DocumentHistory preparedRetired(8, 1);
    auto retiredPublication =
        DocumentHistory::PrepareEdit(preparedRetired.CaptureState(), before, {}, selectionRemovedAfter, {},
                                     L"Prepared retired selection is charged");
    const bool retiredPublished = retiredPublication && preparedRetired.Publish(retiredPublication.value());
    ExpectTopology(retiredPublished && preparedRetired.UndoCount() == 0 &&
                       preparedRetired.RetainedBytes(selectionRemovedAfter) == 0,
                   "prepared-trim-drops-history-only-selection-capacity");
}

void PreparedIndexDeduplicatesSharedRasterAcrossEntries()
{
    const Raster shared = TinyBgra(2, 1, 201);
    auto first = std::optional<CanvasDocument>{DocumentWithRaster(shared, L"shared indexed owner")};
    auto second = first;
    second->width = 2;
    auto third = second;
    EditDocumentFixture(third.value(), [](DocumentFixture& value) { value.layers.front().asset.reset(); });
    third->width = 3;

    DocumentHistory history;
    const bool firstPublished = PublishIndexedEdit(history, first, second, L"shared before after");
    const bool secondPublished = firstPublished && PublishIndexedEdit(history, second, third, L"shared again");
    constexpr std::size_t kLiteralRasterBytes = 8;
    ExpectStorageIndex(secondPublished && history.UndoCount() == 2 &&
                           history.RetainedBytes(third) == kLiteralRasterBytes,
                       "shared-before-after-raster-is-charged-once-across-entry-roots");
}

void PreparedIndexExcludesCompleteLiveNestedRasterClosure()
{
    const Raster leaf = TinyBgra(1, 1, 211);
    const Raster inner = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 1);
    const Raster outer = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, inner}}, 1);
    const auto held = std::optional<CanvasDocument>{DocumentWithRaster(outer, L"nested live indexed owner")};
    auto empty = std::optional<CanvasDocument>{CanvasDocument{}};
    empty->width = 2;

    DocumentHistory history;
    const bool retired = PublishIndexedEdit(history, held, empty, L"retire nested owner");
    constexpr std::size_t kLiteralLeafBytes = 4;
    const bool retiredCharge = retired && history.RetainedBytes(empty) == kLiteralLeafBytes;
    auto liveAgain = held;
    liveAgain->width = 3;
    const bool live = retiredCharge && PublishIndexedEdit(history, empty, liveAgain, L"make nested owner live");
    ExpectStorageIndex(retiredCharge && live && history.RetainedBytes(liveAgain) == 0 && !outer.IsMaterialized() &&
                           !inner.IsMaterialized(),
                       "live-outer-owner-excludes-every-nested-child-without-materializing-sparse-owners");
}

void PreparedIndexTrimAndRestoreMatchReferenceOrder()
{
    std::array<std::optional<CanvasDocument>, 5> documents;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const int width = index == 4 ? 4 : 1;
        const Raster raster = TinyBgra(width, 1, static_cast<std::uint8_t>(31 + index));
        documents[index] = DocumentWithRaster(raster, L"trim parity owner");
        documents[index]->width = static_cast<int>(index + 1);
    }

    DocumentHistory prepared(8, 12);
    DocumentHistory reference(8, 12);
    bool published = true;
    for (std::size_t index = 0; index + 1 < documents.size(); ++index)
    {
        const std::wstring name = L"trim parity " + std::to_wstring(index + 1);
        reference.Begin(name, documents[index], {});
        reference.End(documents[index + 1], {});
        published = published && PublishIndexedEdit(prepared, documents[index], documents[index + 1], name.c_str());
    }
    constexpr std::size_t kLiteralRetainedBeforeUndo = 12;
    const bool oldestTrimMatches = published && prepared.UndoCount() == 3 && reference.UndoCount() == 3 &&
                                   prepared.RetainedBytes(documents.back()) == kLiteralRetainedBeforeUndo &&
                                   reference.RetainedBytes(documents.back()) == kLiteralRetainedBeforeUndo;

    auto restore = DocumentHistory::PrepareRestore(prepared.CaptureState(), false);
    const auto referenceRestore = reference.Undo();
    const bool preparedRestoreReady =
        restore && prepared.CanPublish(restore.value()) && restore.value().RestoreSnapshot().has_value();
    const bool restorePublished = preparedRestoreReady && prepared.Publish(restore.value());
    const auto transferredSnapshot = restorePublished ? restore.value().TakeRestoreSnapshot() : std::nullopt;
    constexpr std::size_t kLiteralAfterPastThenFutureTrim = 0;
    ExpectStorageIndex(oldestTrimMatches && restorePublished && transferredSnapshot && referenceRestore &&
                           transferredSnapshot.value().document->width == 4 &&
                           referenceRestore.value().document->width == 4 && prepared.UndoCount() == 0 &&
                           reference.UndoCount() == 0 && !prepared.CanRedo() && !reference.CanRedo() &&
                           prepared.RetainedBytes(documents[3]) == kLiteralAfterPastThenFutureTrim,
                       "trim-drops-oldest-past-before-future-and-restore-snapshot-survives-trim");

    DocumentHistory preparedRedo(8, 16);
    DocumentHistory referenceRedo(8, 16);
    published = true;
    for (std::size_t index = 0; index + 1 < documents.size(); ++index)
    {
        const std::wstring name = L"redo parity " + std::to_wstring(index + 1);
        referenceRedo.Begin(name, documents[index], {});
        referenceRedo.End(documents[index + 1], {});
        published = published && PublishIndexedEdit(preparedRedo, documents[index], documents[index + 1], name.c_str());
    }
    auto undo = DocumentHistory::PrepareRestore(preparedRedo.CaptureState(), false);
    const auto referenceUndo = referenceRedo.Undo();
    const bool undoPublished = published && undo && preparedRedo.Publish(undo.value());
    const auto undoSnapshot = undoPublished ? undo.value().TakeRestoreSnapshot() : std::nullopt;
    constexpr std::size_t kLiteralFutureBytes = 16;
    const bool pastRemovedFutureKept = undoPublished && undoSnapshot && referenceUndo &&
                                       preparedRedo.UndoCount() == 0 && referenceRedo.UndoCount() == 0 &&
                                       preparedRedo.CanRedo() && referenceRedo.CanRedo() &&
                                       preparedRedo.RetainedBytes(documents[3]) == kLiteralFutureBytes &&
                                       referenceRedo.RetainedBytes(documents[3]) == kLiteralFutureBytes;
    auto redo = DocumentHistory::PrepareRestore(preparedRedo.CaptureState(), true);
    const auto referenceRedoSnapshot = referenceRedo.Redo();
    const bool redoPublished = pastRemovedFutureKept && redo && preparedRedo.Publish(redo.value());
    const auto redoSnapshot = redoPublished ? redo.value().TakeRestoreSnapshot() : std::nullopt;
    ExpectStorageIndex(redoPublished && redoSnapshot && referenceRedoSnapshot &&
                           redoSnapshot.value().document->width == 5 &&
                           referenceRedoSnapshot.value().document->width == 5 && preparedRedo.UndoCount() == 1 &&
                           referenceRedo.UndoCount() == 1 && !preparedRedo.CanRedo() && !referenceRedo.CanRedo() &&
                           preparedRedo.RetainedBytes(documents.back()) == 4,
                       "undo-redo-transfers-entry-and-index-membership-with-reference-retention");
}

void PreparedIndexFailureAtTrimPreservesStateAndAllowsRetry()
{
    const Raster raster = TinyBgra(1, 1, 221);
    const auto first = std::optional<CanvasDocument>{DocumentWithRaster(raster, L"index failure owner")};
    auto second = std::optional<CanvasDocument>{CanvasDocument{}};
    second->width = 2;
    auto third = second;
    third->width = 3;
    DocumentHistory history;
    const bool baselinePublished = PublishIndexedEdit(history, first, second, L"index baseline");
    const Guid revision = history.CurrentRevision();
    const int count = history.UndoCount();
    const std::wstring name = history.UndoName();
    const std::size_t retained = history.RetainedBytes(second);

    CancelSource cancellation;
    HistoryIndexCheckpoint cancelCheckpoint{&cancellation, false, false};
    const auto cancelled =
        DocumentHistory::PrepareEdit(history.CaptureState(), second, {}, third, {}, L"cancel at trim index",
                                     cancellation.Token(), {&HistoryIndexCheckpoint::Run, &cancelCheckpoint});
    ExpectStorageIndex(baselinePublished && !cancelled && cancelCheckpoint.reached &&
                           history.CurrentRevision() == revision && history.UndoCount() == count &&
                           history.UndoName() == name && history.RetainedBytes(second) == retained,
                       "trim-index-cancellation-leaves-published-revision-stacks-and-index-unchanged");

    HistoryIndexCheckpoint allocationCheckpoint{nullptr, true, false};
    bool allocationCaught = false;
    try
    {
        (void)DocumentHistory::PrepareEdit(history.CaptureState(), second, {}, third, {}, L"allocation at trim index",
                                           {}, {&HistoryIndexCheckpoint::Run, &allocationCheckpoint});
    }
    catch (const std::bad_alloc&)
    {
        allocationCaught = true;
    }
    const bool preservedAfterAllocation = baselinePublished && allocationCaught && allocationCheckpoint.reached &&
                                          history.CurrentRevision() == revision && history.UndoCount() == count &&
                                          history.UndoName() == name && history.RetainedBytes(second) == retained;
    const bool retryPublished = preservedAfterAllocation && PublishIndexedEdit(history, second, third, L"index retry");
    ExpectStorageIndex(preservedAfterAllocation && retryPublished && history.UndoCount() == count + 1 &&
                           history.UndoName() == L"index retry",
                       "trim-index-allocation-failure-preserves-state-and-fresh-retry-publishes");
}

void CapturedIndexViewSurvivesResetAndPreventsRasterIdentityReuse()
{
    Raster oldRaster = TinyBgra(1, 1, 231);
    const void* const oldIdentity = oldRaster.Identity();
    auto oldBefore = std::optional<CanvasDocument>{DocumentWithRaster(oldRaster, L"old indexed owner")};
    auto oldAfter = std::optional<CanvasDocument>{CanvasDocument{}};
    oldAfter->width = 2;
    DocumentHistory history;
    const bool oldPublished = PublishIndexedEdit(history, oldBefore, oldAfter, L"old indexed root");
    const auto oldRevision = history.CurrentRevision();
    const auto oldView = history.CaptureState();
    oldBefore.reset();
    oldAfter.reset();
    oldRaster = {};
    history.Reset();

    const Raster replacement = TinyBgra(1, 1, 241);
    auto replacementBefore = std::optional<CanvasDocument>{DocumentWithRaster(replacement, L"new indexed owner")};
    auto replacementAfter = std::optional<CanvasDocument>{CanvasDocument{}};
    replacementAfter->width = 3;
    const bool newPublished = PublishIndexedEdit(history, replacementBefore, replacementAfter, L"new indexed root");
    auto staleCandidate =
        DocumentHistory::PrepareEdit(oldView, replacementBefore, {}, replacementAfter, {}, L"stale index view");
    ExpectStorageIndex(oldPublished && newPublished && replacement.Identity() != oldIdentity &&
                           oldView.PastCount() == 1 && oldView.UndoName() == L"old indexed root" &&
                           oldView.Revision() == oldRevision && !history.IsCurrent(oldView) && staleCandidate &&
                           !history.CanPublish(staleCandidate.value()) && history.UndoCount() == 1 &&
                           history.UndoName() == L"new indexed root" && history.RetainedBytes(replacementAfter) == 4,
                       "captured-old-index-view-owns-retired-raster-across-reset-and-rejects-stale-publication");
}

void PreparedIndexResamplesLateSparseMaterializationOnNextPreparation()
{
    const Raster leaf = TinyBgra(1, 1, 251);
    const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 1);
    const auto before = std::optional<CanvasDocument>{DocumentWithRaster(sparse, L"late indexed materialization")};
    auto empty = std::optional<CanvasDocument>{CanvasDocument{}};
    empty->width = 2;
    auto next = empty;
    next->width = 3;

    DocumentHistory history;
    const bool firstPublished = PublishIndexedEdit(history, before, empty, L"before materialization");
    constexpr std::size_t kLiteralBeforeMaterialization = 4;
    const bool initialBytes =
        firstPublished && history.RetainedBytes(empty) == kLiteralBeforeMaterialization && !sparse.IsMaterialized();
    DocumentHistory budgeted(100, 8);
    const bool budgetBaseline = PublishIndexedEdit(budgeted, before, empty, L"Sparse budget baseline");
    (void)sparse.Data();
    constexpr std::size_t kLiteralAfterMaterialization = 12;
    const bool secondPublished = initialBytes && PublishIndexedEdit(history, empty, next, L"after materialization");
    ExpectStorageIndex(secondPublished && sparse.IsMaterialized() && sparse.ByteSize() == 8 &&
                           history.RetainedBytes(next) == kLiteralAfterMaterialization,
                       "separate-preparation-resamples-materialized-sparse-owner-and-nested-leaf-bytes");
    const bool budgetRecheck = budgetBaseline && PublishIndexedEdit(budgeted, empty, next, L"Recheck sparse budget");
    ExpectStorageIndex(budgetRecheck && budgeted.UndoCount() == 1 && budgeted.UndoName() == L"Recheck sparse budget" &&
                           budgeted.RetainedBytes(next) == 0,
                       "next-preparation-drops-old-history-above-literal-eight-byte-budget-after-cache-growth");
}

void PreparedIndexBoundsRepeatedLargeRasterAcrossMetadataEdits()
{
    constexpr std::uint64_t kMiB = 1024 * 1024;
    constexpr std::uint64_t kLiteralPixelBytes = kMiB;
    constexpr std::uint64_t kPreparedBoundCeiling = 8 * kMiB;
    constexpr std::uint64_t kOccurrenceBoundFloor = 32 * kMiB;
    constexpr std::size_t kEditCount = 16;

    const Raster shared = HeldBgra(61);
    auto current = std::optional<CanvasDocument>{DocumentWithRaster(shared, L"shared 1 MiB raster")};
    DocumentHistory prepared;
    DocumentHistory reference;
    bool published = true;
    for (std::size_t index = 0; index < kEditCount; ++index)
    {
        auto next = current;
        const std::wstring layerName = L"metadata edit " + std::to_wstring(index + 1);
        EditDocumentFixture(next.value(), [&](DocumentFixture& value) { value.layers.front().name = layerName; });
        next->width = static_cast<int>(index + 2);
        const std::wstring historyName = L"shared raster edit " + std::to_wstring(index + 1);
        reference.Begin(historyName, current, {});
        reference.End(next, {});
        published = published && PublishIndexedEdit(prepared, current, next, historyName.c_str());
        current = std::move(next);
    }

    const std::uint64_t preparedRetirement = prepared.RetirementMemoryBytes(current);
    const std::uint64_t preparedAdmission = prepared.EditMemoryBytes(current);
    const std::uint64_t referenceOccurrenceBound = reference.RetirementMemoryBytes(current);
    ExpectStorageIndex(published && prepared.UndoCount() == static_cast<int>(kEditCount) &&
                           preparedRetirement >= kLiteralPixelBytes && preparedRetirement < kPreparedBoundCeiling,
                       "sixteen-prepared-metadata-edits-charge-shared-raster-once-with-literal-pixel-floor");
    ExpectStorageIndex(preparedAdmission != 0 && preparedAdmission < kPreparedBoundCeiling,
                       "prepared-index-metadata-edit-admission-remains-below-independent-eight-mib-ceiling");
    ExpectStorageIndex(reference.UndoCount() == static_cast<int>(kEditCount) &&
                           referenceOccurrenceBound > kOccurrenceBoundFloor,
                       "reference-occurrence-bound-grows-past-thirty-two-mib-for-repeated-shared-owner");
}

void PartialIndexFaultsPreserveRedoAndAllowACompleteRetry()
{
    struct Scenario final
    {
        DocumentHistory::PrepareStage stage;
        const char* name;
    };
    constexpr Scenario scenarios[]{{DocumentHistory::PrepareStage::StorageIndexClone, "clone"},
                                   {DocumentHistory::PrepareStage::StorageIndexMembership, "membership"},
                                   {DocumentHistory::PrepareStage::StorageIndexBounds, "bounds"}};
    for (const auto& scenario : scenarios)
    {
        const auto first = std::optional<CanvasDocument>{DocumentWithRaster(TinyBgra(1, 1, 11), L"First")};
        const auto second = std::optional<CanvasDocument>{DocumentWithRaster(TinyBgra(1, 1, 21), L"Second")};
        const auto third = std::optional<CanvasDocument>{DocumentWithRaster(TinyBgra(1, 1, 31), L"Third")};
        DocumentHistory history;
        const bool baseline = PublishIndexedEdit(history, first, second, L"Baseline");
        auto undo = DocumentHistory::PrepareRestore(history.CaptureState(), false);
        const bool restored = baseline && undo && history.Publish(*undo);
        const auto old = history.CaptureState();
        const Guid revision = history.CurrentRevision();
        const std::wstring redoName = history.RedoName();
        CancelSource cancellation;
        HistoryIndexCheckpoint cancelGate{&cancellation, false, false, scenario.stage};
        const auto cancelled = DocumentHistory::PrepareEdit(old, first, {}, third, {}, L"Branch", cancellation.Token(),
                                                            {&HistoryIndexCheckpoint::Run, &cancelGate});
        const std::string cancelName =
            std::string("partial-") + scenario.name + "-cancel-preserves-redo-and-live-index";
        ExpectStorageIndex(restored && !cancelled && cancelGate.reached && history.IsCurrent(old) &&
                               history.CurrentRevision() == revision && history.UndoCount() == 0 && history.CanRedo() &&
                               history.RedoName() == redoName && history.RetainedBytes(first) == 4,
                           cancelName.c_str());
        HistoryIndexCheckpoint allocationGate{nullptr, true, false, scenario.stage};
        bool caught = false;
        try
        {
            (void)DocumentHistory::PrepareEdit(old, first, {}, third, {}, L"Branch", {},
                                               {&HistoryIndexCheckpoint::Run, &allocationGate});
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        const std::string allocationName =
            std::string("partial-") + scenario.name + "-allocation-preserves-redo-and-live-index";
        ExpectStorageIndex(restored && caught && allocationGate.reached && history.IsCurrent(old) &&
                               history.CurrentRevision() == revision && history.UndoCount() == 0 && history.CanRedo() &&
                               history.RedoName() == redoName && history.RetainedBytes(first) == 4,
                           allocationName.c_str());
        const bool retry = restored && PublishIndexedEdit(history, first, third, L"Retry branch");
        const std::string retryName = std::string("partial-") + scenario.name + "-retry-publishes-exact-branch";
        ExpectStorageIndex(retry && history.UndoCount() == 1 && !history.CanRedo() &&
                               history.UndoName() == L"Retry branch" && history.RetainedBytes(third) == 4 &&
                               !history.IsCurrent(old),
                           retryName.c_str());
    }
}

struct IndexRetirementSink final
{
    std::array<DeferredReleaseNode*, 8> nodes{};
    std::size_t count = 0;
    std::size_t destroyed = 0;
    bool overflow = false;
    bool installed = false;
    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& sink = *static_cast<IndexRetirementSink*>(context);
        if (sink.count == sink.nodes.size())
            sink.overflow = true;
        else
            sink.nodes[sink.count++] = &node;
    }
    bool Close() noexcept
    {
        if (!installed)
            return true;
        if (!ClearDeferredReleaseSink(this))
            return false;
        installed = false;
        for (std::size_t index = 0; index < count; ++index)
        {
            nodes[index]->destroy(nodes[index]);
            ++destroyed;
            nodes[index] = nullptr;
        }
        count = 0;
        return true;
    }
    ~IndexRetirementSink()
    {
        (void)Close();
    }
};

void CompleteIndexesUseTheOwnerRetirementHandoff()
{
    for (const int side : {1, 2048})
    {
        const std::string prefix = side == 1 ? "small-index-" : "large-index-";
        const auto expect = [&](bool condition, const char* name)
        {
            const std::string qualified = prefix + name;
            ExpectStorageIndex(condition, qualified.c_str());
        };
        Raster large = Raster::Make(side, side, RasterFormat::Bgra8888);
        auto before = std::optional<CanvasDocument>{DocumentWithRaster(large, L"Deferred index")};
        const std::optional<CanvasDocument> after{CanvasDocument{}};
        DocumentHistory history;
        const bool published = PublishIndexedEdit(history, before, after, L"Retire large indexed source");
        const void* const heldIdentity = large.Identity();
        before.reset();
        large = {};
        IndexRetirementSink sink;
        sink.installed = InstallDeferredReleaseSink(&IndexRetirementSink::Enqueue, &sink);
        expect(published && sink.installed, "fixture-publishes-and-installs-exact-retirement-scope");
        if (!sink.installed)
            return;
        history.Reset();
        const bool declaredSizeMatches = sink.count == 1 && (side == 1 ? sink.nodes[0]->bytes < 16 * 1024 * 1024
                                                                       : sink.nodes[0]->bytes >= 16 * 1024 * 1024);
        expect(!history.CanUndo() && !history.CanRedo() && sink.count == 1 && !sink.overflow &&
                   sink.nodes[0]->destroy && declaredSizeMatches,
               "last-reference-hands-complete-graph-to-one-node-without-inflating-small-bound");
        const Raster replacement = TinyBgra(1, 1, 41);
        expect(replacement.HasStorage() && replacement.Identity() != heldIdentity && sink.destroyed == 0,
               "keeps-raster-identity-owned-until-consumer-destroys-node");
        const bool closed = sink.Close();
        expect(closed && sink.destroyed == 1 && sink.count == 0 && !sink.installed && !sink.overflow,
               "consumer-destroys-complete-index-once-after-clearing-owner-sink");
    }
}

void TestHistoryStorageTopology()
{
    TopologyCapturesNestedSharedGraphAndEveryLayerOwner();
    TopologyOwnsSourcesAndTracksLateMaterialization();
    TopologyMatchesAbsentDefaultEmptyAndCancellation();
    PreparedTopologyChargesCompleteNestedGraphAgainstEmptyLiveRoot();
    SelectionTopologyHonorsLiveExclusionInDirectAndPreparedTrim();
    PreparedIndexDeduplicatesSharedRasterAcrossEntries();
    PreparedIndexExcludesCompleteLiveNestedRasterClosure();
    PreparedIndexTrimAndRestoreMatchReferenceOrder();
    PreparedIndexFailureAtTrimPreservesStateAndAllowsRetry();
    CapturedIndexViewSurvivesResetAndPreventsRasterIdentityReuse();
    PreparedIndexResamplesLateSparseMaterializationOnNextPreparation();
    PreparedIndexBoundsRepeatedLargeRasterAcrossMetadataEdits();
    PartialIndexFaultsPreserveRedoAndAllowACompleteRetry();
    CompleteIndexesUseTheOwnerRetirementHandoff();
}
} // namespace

int RunHistoryRetirementBoundTests()
{
    gFailures = 0;
    EmptyAndCurrentGraphHaveAllocationFloors();
    CapturedPastFuturePendingAndAdditionalRasterAreIncluded();
    SparseSharedBaseAndTileGraphStaysUnmaterialized();
    ExcessiveSparseTileDepthRefusesEstimate();
    LargeHistoryNamesHaveIndependentCapacityFloors();
    CachedRasterBoundsCoverAdoptionCachesAndSharedDags();
    CachedRasterSelfStorageBounds();
    SelfStorageBoundsSurviveWholeGraphRefusal();
    CachedHistoryBoundsPreserveSourceAndPublication();
    ShapeSourceStorageParticipatesInHistoryBudget();
    TestHistoryStorageTopology();
    return gFailures;
}

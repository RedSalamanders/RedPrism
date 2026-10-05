#include "HistoryRetirementBoundTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    CachedHistoryBoundsPreserveSourceAndPublication();
    ShapeSourceStorageParticipatesInHistoryBudget();
    return gFailures;
}

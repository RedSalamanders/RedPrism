#include "Document.Tests.RecordOwnershipBounds.h"

#include "Document.h"
#include "RecordRoot.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s record-bounds.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct TestRecord final
{
    std::uint64_t metadata = 0;
    std::uint64_t ownership = 0;
    std::uint64_t nodes = 0;
    bool measurable = true;
    int value = 0;

    [[nodiscard]] bool operator==(const TestRecord&) const noexcept = default;
};

struct MeasureControl final
{
    inline static std::atomic<std::uint64_t> calls{0};
    inline static std::atomic<CancelSource*> cancelDuringMeasure{nullptr};
    inline static std::atomic<std::uint64_t> cancelAtCall{1};

    static void Reset() noexcept
    {
        calls.store(0, std::memory_order_relaxed);
        cancelDuringMeasure.store(nullptr, std::memory_order_relaxed);
        cancelAtCall.store(1, std::memory_order_relaxed);
    }
};
} // namespace

template <> struct RecordOwnershipPolicy<TestRecord>
{
    static constexpr bool enabled = true;

    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const TestRecord& record) noexcept
    {
        MeasureControl::calls.fetch_add(1, std::memory_order_relaxed);
        if (CancelSource* source = MeasureControl::cancelDuringMeasure.load(std::memory_order_relaxed);
            source != nullptr && MeasureControl::calls.load(std::memory_order_relaxed) ==
                                     MeasureControl::cancelAtCall.load(std::memory_order_relaxed))
            (void)source->Cancel();
        if (!record.measurable)
            return std::nullopt;
        return RecordOwnershipBounds{record.metadata, record.ownership, record.nodes};
    }
};

namespace
{
using TestRoot = RecordRoot<TestRecord>;
static_assert(sizeof(TestRoot) == 16);
static_assert(sizeof(RecordRoot<ImageLayer>) == 16);
static_assert(noexcept(RecordOwnershipPolicy<TestRecord>::Measure(std::declval<const TestRecord&>())));
static_assert(noexcept(RecordOwnershipPolicy<ImageLayer>::Measure(std::declval<const ImageLayer&>())));
static_assert(std::is_nothrow_move_constructible_v<TestRecord>);
static_assert(std::is_nothrow_move_assignable_v<TestRecord>);
static_assert(noexcept(std::declval<const TestRoot&>().OwnershipBounds()));

struct UnavailableRecord final
{
    int value = 0;
    [[nodiscard]] bool operator==(const UnavailableRecord&) const noexcept = default;
};

} // namespace

template <> struct RecordOwnershipPolicy<UnavailableRecord>
{
    static constexpr bool enabled = true;
    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const UnavailableRecord&) noexcept
    {
        return std::nullopt;
    }
};

namespace
{
using UnavailableRoot = RecordRoot<UnavailableRecord>;

struct DefaultRecord final
{
    int value = 0;
    [[nodiscard]] bool operator==(const DefaultRecord&) const noexcept = default;
};
using DefaultRoot = RecordRoot<DefaultRecord>;

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct AllocationProbe final
{
    std::atomic<bool> failNext{false};
    std::atomic<std::uint64_t> allocations{0};
};
#pragma warning(pop)

template <typename T> struct RetryAllocator final
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;
    AllocationProbe* probe = nullptr;

    RetryAllocator() noexcept = default;
    explicit RetryAllocator(AllocationProbe* value) noexcept : probe(value) {}
    template <typename U> RetryAllocator(const RetryAllocator<U>& other) noexcept : probe(other.probe) {}

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (probe != nullptr)
        {
            probe->allocations.fetch_add(1, std::memory_order_relaxed);
            if (probe->failNext.exchange(false, std::memory_order_relaxed))
                throw std::bad_alloc{};
        }
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* values, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(values, count);
    }
    template <typename U> [[nodiscard]] bool operator==(const RetryAllocator<U>& other) const noexcept
    {
        return probe == other.probe;
    }
};

using AllocatorRoot = RecordRoot<TestRecord, RetryAllocator<TestRecord>>;

[[nodiscard]] std::optional<TestRoot> SealRecords(std::initializer_list<TestRecord> records)
{
    TestRoot::Builder builder(records.size());
    for (const TestRecord& record : records)
        if (!builder.Append(record))
            return std::nullopt;
    return builder.Seal();
}

struct ImageLayerSample final
{
    RecordRoot<ImageLayer> root;
    std::optional<RecordOwnershipBounds> bounds;
    std::size_t capacity = 0;
};

[[nodiscard]] ImageLayerSample Sample(ImageLayer layer)
{
    RecordRoot<ImageLayer>::Builder builder(1);
    if (!builder.Append(std::move(layer)))
        return {};
    auto root = builder.Seal();
    if (!root.has_value())
        return {};
    const std::size_t capacity = root->capacity();
    RecordRoot<ImageLayer> sealed = std::move(root.value());
    const auto bounds = sealed.OwnershipBounds();
    return ImageLayerSample{std::move(sealed), bounds, capacity};
}

[[nodiscard]] ImageLayerSample SampleEmptyImageLayerRoot()
{
    RecordRoot<ImageLayer>::Builder builder(0);
    auto root = builder.Seal();
    if (!root.has_value())
        return {};
    const std::size_t capacity = root->capacity();
    RecordRoot<ImageLayer> sealed = std::move(root.value());
    const auto bounds = sealed.OwnershipBounds();
    return ImageLayerSample{std::move(sealed), bounds, capacity};
}

[[nodiscard]] std::uint64_t LayerChildMetadata(const ImageLayer& layer) noexcept
{
    std::uint64_t bytes = (layer.name.capacity() + 1) * sizeof(wchar_t) + 64;
    if (layer.asset.has_value())
        bytes += (layer.asset->name.capacity() + 1) * sizeof(wchar_t) + 64;
    if (layer.mask.has_value())
        bytes += (layer.mask->asset.name.capacity() + 1) * sizeof(wchar_t) + 64;
    if (layer.adjustment.has_value())
        for (const auto& channel : layer.adjustment->curves.channels)
            bytes += channel.capacity() * sizeof(CurvePoint) + 64;
    return bytes;
}

[[nodiscard]] ImageLayer LayerWithRaster(const Raster& raster, const wchar_t* layerName)
{
    ImageLayer layer =
        ImageLayer::Blank(layerName, Vec2{raster.Width() > 0 ? static_cast<double>(raster.Width()) : 8.0,
                                          raster.Height() > 0 ? static_cast<double>(raster.Height()) : 8.0});
    layer.asset = RasterAsset{raster, Raster{}, L"asset-name"};
    return layer;
}

[[nodiscard]] std::uint64_t SumRasterBytes(std::initializer_list<const Raster*> rasters) noexcept
{
    std::uint64_t total = 0;
    for (const Raster* raster : rasters)
        if (raster != nullptr)
            total += raster->RetirementMemoryBytes();
    return total;
}

void CanvasDocumentRetirementBounds()
{
    CanvasDocument defaultDocument;
    const std::uint64_t defaultBound = defaultDocument.RetirementMemoryBytes();
    Expect(defaultBound == 64ull * 1024 + sizeof(CanvasDocument),
           "canvas-default-empty-source-has-fixed-nonzero-bound");

    const ImageLayerSample emptyLayers = SampleEmptyImageLayerRoot();
    CanvasDocument sealedEmptyDocument;
    sealedEmptyDocument.layers = emptyLayers.root;
    const std::uint64_t sealedEmptyBound = sealedEmptyDocument.RetirementMemoryBytes();
    Expect(emptyLayers.bounds.has_value() && sealedEmptyBound > defaultBound,
           "canvas-published-empty-layer-root-has-own-nonzero-bound");

    const Raster base = Raster::Make(12, 12, RasterFormat::Bgra8888);
    std::vector<RasterTile> tiles;
    tiles.emplace_back(RasterTile{0, 0, Raster::Make(4, 4, RasterFormat::Bgra8888)});
    const Raster sparse = Raster::MakeSparse(24, 24, RasterFormat::Bgra8888, base, 3, 5, std::move(tiles), 0);
    ImageLayer layer = LayerWithRaster(sparse, L"canvas image child ownership");
    layer.asset->thumbnail = Raster::Make(3, 2, RasterFormat::Bgra8888);
    layer.mask = LayerMask{
        RasterAsset{Raster::Make(7, 5, RasterFormat::Gray8), Raster::Make(2, 2, RasterFormat::Gray8), L"mask child"},
        true, std::nullopt, true};
    layer.shapeImage = Raster::Make(5, 3, RasterFormat::Bgra8888);
    const ImageLayerSample fullLayer = Sample(std::move(layer));
    const bool fullLayerReady = fullLayer.bounds.has_value() && fullLayer.root.size() == 1;
    Expect(fullLayerReady, "canvas-full-raster-child-fixture-seals");
    if (fullLayerReady)
    {
        CanvasDocument source;
        source.width = 24;
        source.height = 24;
        source.layers = fullLayer.root;
        const ImageLayer& stored = source.layers[0];
        const bool allRasterFieldsReady = stored.asset.has_value() && stored.mask.has_value() &&
                                          stored.asset->image.HasStorage() && stored.asset->thumbnail.HasStorage() &&
                                          stored.mask->asset.image.HasStorage() &&
                                          stored.mask->asset.thumbnail.HasStorage() && stored.shapeImage.HasStorage();
        Expect(allRasterFieldsReady, "canvas-child-fixture-retains-image-thumbnail-mask-and-shape-rasters");
        if (allRasterFieldsReady)
        {
            const std::uint64_t childRasterBytes =
                SumRasterBytes({&stored.asset->image, &stored.asset->thumbnail, &stored.mask->asset.image,
                                &stored.mask->asset.thumbnail, &stored.shapeImage});
            const std::uint64_t requiredFloor = 64ull * 1024 + sizeof(CanvasDocument) + childRasterBytes;
            const std::uint64_t sourceBound = source.RetirementMemoryBytes();
            Expect(sourceBound >= requiredFloor,
                   "canvas-bound-covers-every-image-thumbnail-mask-and-shape-raster-owner");
            Expect(stored.asset->image.IsSparse() && !stored.asset->image.IsMaterialized(),
                   "canvas-bound-fixture-starts-as-unmaterialized-sparse-image");
            const auto repeatedOne = source.RetirementMemoryBytes();
            const auto repeatedTwo = source.RetirementMemoryBytes();
            Expect(sourceBound != 0 && repeatedOne == sourceBound && repeatedTwo == sourceBound,
                   "canvas-source-query-reuses-stable-published-ownership-cache");

            const std::uint8_t* const assembled = stored.asset->image.Data();
            Expect(assembled != nullptr && stored.asset->image.IsMaterialized(),
                   "canvas-explicit-sparse-data-access-materializes-image");
            Expect(source.RetirementMemoryBytes() == sourceBound,
                   "canvas-bound-remains-stable-after-late-sparse-assembly");

            CanvasDocument retained = source;
            const std::uint64_t retainedBound = retained.RetirementMemoryBytes();
            ImageLayerSample replacementLayer =
                Sample(LayerWithRaster(Raster::Make(2, 3, RasterFormat::Bgra8888), L"replacement source"));
            const bool replacementReady = replacementLayer.bounds.has_value();
            Expect(replacementReady, "canvas-replacement-source-layer-seals");
            if (replacementReady)
            {
                CanvasDocument replacement = source;
                replacement.layers = replacementLayer.root;
                Expect(source.RetirementMemoryBytes() == sourceBound &&
                           retained.RetirementMemoryBytes() == retainedBound && retainedBound == sourceBound,
                       "canvas-copy-and-replacement-preserve-original-source-bounds");
                Expect(replacement.RetirementMemoryBytes() != sourceBound && source.layers[0].asset->image.IsSparse(),
                       "canvas-replacement-publishes-new-bound-with-old-source-intact");
            }

            std::vector<Vec2> points;
            points.reserve(7);
            points.emplace_back(Vec2{1, 2});
            points.emplace_back(Vec2{8, 2});
            points.emplace_back(Vec2{8, 9});
            points.emplace_back(Vec2{1, 9});
            SelectionLoop loop = std::move(points);
            std::vector<SelectionLoop> loops;
            loops.reserve(3);
            loops.emplace_back(std::move(loop));
            DocumentSelection selection{false, SelectionLoops(std::move(loops))};
            const auto selectionIdentity = selection.loops.Identity();
            const auto selectionStorage = selection.loops.StorageBytes();
            CanvasDocument selected = source;
            selected.selection = selection;
            const auto selectedBefore = selected.selection.value();
            const std::uint64_t selectedBound = selected.RetirementMemoryBytes();
            Expect(selectedBound >= sourceBound + selectionStorage + 128,
                   "canvas-selection-bound-includes-retained-capacity-and-two-control-allowances");
            Expect(selected.selection.value() == selectedBefore &&
                       selected.selection->loops.Identity() == selectionIdentity &&
                       selected.selection->loops.StorageBytes() == selectionStorage &&
                       !selected.selection->loops.empty(),
                   "canvas-selection-query-does-not-mutate-shared-outline-or-capacity");

            const std::uint64_t sourceBeforeHistory = source.RetirementMemoryBytes();
            DocumentSession unrelatedHistory;
            unrelatedHistory.CreateDocument(2, 2, true);
            unrelatedHistory.AddBlankLayer();
            unrelatedHistory.AddBlankLayer();
            const auto unrelatedHistoryBytes =
                unrelatedHistory.history.RetirementMemoryBytes(unrelatedHistory.Document());
            Expect(unrelatedHistory.history.CanUndo() && unrelatedHistoryBytes > sourceBeforeHistory &&
                       source.RetirementMemoryBytes() == sourceBeforeHistory,
                   "canvas-source-bound-does-not-include-unrelated-session-history");
        }
    }

    {
        Raster deep = Raster::Make(1, 1, RasterFormat::Bgra8888);
        for (int depth = 0; depth < 130; ++depth)
            deep = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, deep}}, 0);
        const bool childStoredButUnmeasurable = deep.HasStorage() && deep.RetirementMemoryBytes() == 0;
        Expect(childStoredButUnmeasurable, "canvas-deep-sparse-child-is-stored-with-unavailable-bound");
        if (childStoredButUnmeasurable)
        {
            ImageLayerSample unavailableLayer = Sample(LayerWithRaster(deep, L"unmeasurable child"));
            CanvasDocument unavailableDocument;
            unavailableDocument.layers = unavailableLayer.root;
            Expect(unavailableLayer.root.size() == 1 && !unavailableLayer.bounds.has_value() &&
                       unavailableDocument.RetirementMemoryBytes() == 0,
                   "canvas-published-root-with-unmeasurable-child-is-refused");
        }
    }

    {
        constexpr std::size_t recordCount = 10'000;
        RecordRoot<ImageLayer>::Builder builder(recordCount);
        bool appended = true;
        for (std::size_t index = 0; index < recordCount; ++index)
        {
            ImageLayer record;
            record.id.bytes[0] = static_cast<std::uint8_t>(index & 0xFFu);
            record.id.bytes[1] = static_cast<std::uint8_t>((index >> 8) & 0xFFu);
            if (!builder.Append(std::move(record)))
            {
                appended = false;
                break;
            }
        }
        std::optional<RecordRoot<ImageLayer>> largeRoot;
        if (appended)
            largeRoot = builder.Seal();
        Expect(largeRoot.has_value(), "canvas-ten-thousand-record-root-seals");
        if (largeRoot.has_value())
        {
            CanvasDocument large;
            large.layers = largeRoot.value();
            const std::uint64_t cachedBound = large.RetirementMemoryBytes();
            bool stable = cachedBound != 0;
            for (int query = 0; query < 16; ++query)
                stable = stable && large.RetirementMemoryBytes() == cachedBound;
            Expect(stable, "canvas-large-root-repeated-queries-return-cached-source-bound");
            Expect(large.layers.size() == recordCount && large.layers[0].id.bytes[0] == 0 &&
                       large.layers[recordCount - 1].id.bytes[0] == 0x0F &&
                       large.layers[recordCount - 1].id.bytes[1] == 0x27,
                   "canvas-large-root-queries-preserve-all-ten-thousand-records");
        }
    }
}
} // namespace

int RunRecordOwnershipBoundsTests()
{
    gFailures = 0;
    gChecks = 0;
    MeasureControl::Reset();

    {
        TestRoot empty;
        TestRoot::Builder emptyBuilder(0);
        const auto emptySealed = emptyBuilder.Seal();
        const bool emptySealedOkay = emptySealed.has_value();
        Expect(!empty.OwnershipBounds().has_value() && empty.empty(), "default-handle-has-no-bound");
        Expect(sizeof(TestRoot) == 16, "bound-cache-does-not-enlarge-root-handle");
        Expect(emptySealedOkay, "measured-empty-root-seals");
        if (emptySealedOkay)
        {
            const auto emptyBounds = emptySealed->OwnershipBounds();
            Expect(emptyBounds.has_value() && emptySealed->empty(), "measured-empty-root-has-bound");
            Expect(emptyBounds.has_value() && emptyBounds->metadataBytes > 0 && emptyBounds->ownershipBytes > 0,
                   "empty-root-captures-opaque-root-and-control-overhead");
            Expect(emptyBounds.has_value() && emptyBounds->rasterNodes == 0, "empty-root-has-no-child-raster-nodes");
            TestRoot copiedEmpty = emptySealed.value();
            Expect(copiedEmpty.OwnershipBounds() == emptyBounds, "empty-root-copy-retains-cached-bound");
            TestRoot movedEmpty = std::move(copiedEmpty);
            Expect(movedEmpty.OwnershipBounds() == emptyBounds && !copiedEmpty.OwnershipBounds(),
                   "empty-root-move-transfers-bound-and-clears-source");
        }
    }

    {
        MeasureControl::Reset();
        auto empty = SealRecords({});
        const TestRecord first{31, 47, 2, true, 1};
        const TestRecord second{13, 23, 5, true, 2};
        auto one = SealRecords({first});
        auto two = SealRecords({first, second});
        auto equalValue = SealRecords({first});
        const auto emptyBound = empty.has_value() ? empty->OwnershipBounds() : std::nullopt;
        const auto oneBound = one.has_value() ? one->OwnershipBounds() : std::nullopt;
        const auto twoBound = two.has_value() ? two->OwnershipBounds() : std::nullopt;
        const bool literalRootsReady = empty.has_value() && one.has_value() && two.has_value() &&
                                       equalValue.has_value() && emptyBound.has_value() && oneBound.has_value() &&
                                       twoBound.has_value();
        Expect(literalRootsReady, "literal-cost-roots-seal-with-bounds");
        if (literalRootsReady)
        {
            const std::uint64_t oneCapacityBytes = static_cast<std::uint64_t>(one->capacity()) * sizeof(TestRecord);
            const std::uint64_t twoCapacityBytes = static_cast<std::uint64_t>(two->capacity()) * sizeof(TestRecord);
            Expect(oneBound->metadataBytes == emptyBound->metadataBytes + oneCapacityBytes + first.metadata,
                   "metadata-bound-adds-literal-child-and-actual-capacity");
            Expect(oneBound->ownershipBytes == emptyBound->ownershipBytes + oneCapacityBytes + first.ownership,
                   "ownership-bound-adds-literal-child-and-actual-capacity");
            Expect(oneBound->rasterNodes == emptyBound->rasterNodes + first.nodes,
                   "node-bound-adds-literal-child-nodes");
            Expect(twoBound->metadataBytes ==
                       emptyBound->metadataBytes + twoCapacityBytes + first.metadata + second.metadata,
                   "multi-record-metadata-sums-literal-child-costs");
            Expect(twoBound->ownershipBytes ==
                       emptyBound->ownershipBytes + twoCapacityBytes + first.ownership + second.ownership,
                   "multi-record-ownership-sums-literal-child-costs");
            Expect(twoBound->rasterNodes == emptyBound->rasterNodes + first.nodes + second.nodes,
                   "multi-record-nodes-sum-literal-child-costs");
            Expect(oneBound->metadataBytes <= oneBound->ownershipBytes,
                   "valid-bound-includes-metadata-in-total-ownership");
            Expect(one == equalValue && one->Identity() != equalValue->Identity(),
                   "independent-equal-roots-compare-by-value");
            Expect(one->OwnershipBounds() == oneBound, "bounds-value-equality-is-available");
            TestRoot moved = std::move(one.value());
            Expect(moved.OwnershipBounds() == oneBound && !one->OwnershipBounds(),
                   "move-construction-transfers-bound-with-shared-root");
            TestRoot assigned = two.value();
            assigned = std::move(moved);
            Expect(assigned.OwnershipBounds() == oneBound && !moved.OwnershipBounds(),
                   "move-assignment-replaces-cached-bound-and-clears-source");
        }
    }

    {
        MeasureControl::Reset();
        auto root = SealRecords({TestRecord{4, 8, 1, true, 4}, TestRecord{12, 16, 3, true, 5}});
        const bool rootReady = root.has_value();
        Expect(rootReady, "cached-read-root-seals");
        if (rootReady)
        {
            const std::uint64_t callsAtSeal = MeasureControl::calls.load(std::memory_order_relaxed);
            const auto first = root->OwnershipBounds();
            const auto second = root->OwnershipBounds();
            TestRoot copy = root.value();
            const auto copied = copy.OwnershipBounds();
            Expect(callsAtSeal == 2, "seal-measures-each-child-once");
            Expect(first == second && second == copied, "repeated-and-copied-bound-reads-are-identical");
            Expect(MeasureControl::calls.load(std::memory_order_relaxed) == callsAtSeal,
                   "bound-queries-and-root-copy-do-not-walk-records");

            constexpr int readerCount = 5;
            std::latch readersReady(readerCount + 1);
            std::atomic<int> readersDone{0};
            std::atomic<bool> readersOkay{true};
            std::vector<std::thread> readers;
            readers.reserve(readerCount);
            for (int index = 0; index < readerCount; ++index)
            {
                readers.emplace_back(
                    [capture = root.value(), &readersReady, &readersDone, &readersOkay]()
                    {
                        readersReady.count_down();
                        readersReady.wait();
                        const auto expected = capture.OwnershipBounds();
                        for (int read = 0; read < 64; ++read)
                            if (capture.OwnershipBounds() != expected)
                                readersOkay.store(false, std::memory_order_relaxed);
                        readersDone.fetch_add(1, std::memory_order_release);
                    });
            }
            readersReady.count_down();
            readersReady.wait();
            for (std::thread& reader : readers)
                reader.join();
            Expect(readersDone.load(std::memory_order_acquire) == readerCount,
                   "concurrent-bound-readers-finish-through-joined-state");
            Expect(readersOkay.load(std::memory_order_relaxed), "concurrent-readers-observe-stable-bound");
            Expect(MeasureControl::calls.load(std::memory_order_relaxed) == callsAtSeal,
                   "concurrent-bound-reads-never-remeasure-children");
        }
    }

    {
        DefaultRoot::Builder disabledBuilder(1);
        (void)disabledBuilder.Append(DefaultRecord{9});
        const auto disabled = disabledBuilder.Seal();
        UnavailableRoot::Builder unavailableBuilder(1);
        (void)unavailableBuilder.Append(UnavailableRecord{10});
        const auto unavailable = unavailableBuilder.Seal();
        Expect(disabled.has_value() && !disabled->OwnershipBounds().has_value(),
               "default-disabled-policy-yields-no-bound");
        Expect(unavailable.has_value() && !unavailable->OwnershipBounds().has_value(),
               "enabled-but-unavailable-child-yields-no-bound");
        Expect(disabled.has_value() && unavailable.has_value() && disabled->size() == 1 && unavailable->size() == 1,
               "missing-bound-does-not-reject-valid-root");
    }

    {
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        const auto metadataOverflow = SealRecords({TestRecord{maximum, maximum, 0, true, 1}});
        const auto ownershipOverflow = SealRecords({TestRecord{0, maximum, 0, true, 2}});
        const auto nodeOverflow = SealRecords({TestRecord{0, 0, maximum, true, 3}, TestRecord{0, 0, 1, true, 4}});
        const auto invalidRelation = SealRecords({TestRecord{9, 8, 1, true, 4}});
        Expect(metadataOverflow.has_value() && !metadataOverflow->OwnershipBounds().has_value(),
               "metadata-addition-overflow-disables-bound-only");
        Expect(ownershipOverflow.has_value() && !ownershipOverflow->OwnershipBounds().has_value(),
               "ownership-addition-overflow-disables-bound-only");
        Expect(nodeOverflow.has_value() && !nodeOverflow->OwnershipBounds().has_value(),
               "node-addition-overflow-disables-bound-only");
        Expect(invalidRelation.has_value() && !invalidRelation->OwnershipBounds().has_value(),
               "child-ownership-smaller-than-metadata-disables-bound");
        Expect(metadataOverflow.has_value() && ownershipOverflow.has_value() && nodeOverflow.has_value() &&
                   invalidRelation.has_value() && metadataOverflow->size() == 1 && ownershipOverflow->size() == 1 &&
                   nodeOverflow->size() == 2 && invalidRelation->size() == 1,
               "overflow-and-invalid-accounting-still-seal-record-roots");
    }

    {
        auto oldResult = SealRecords({TestRecord{1, 2, 3, true, 1}});
        const bool oldReady = oldResult.has_value();
        Expect(oldReady, "replacement-source-root-seals");
        if (oldReady)
        {
            TestRoot old = std::move(oldResult.value());
            TestRoot retained = old;
            const auto oldBound = old.OwnershipBounds();
            auto replacementBuilder = TestRoot::Builder::Clone(old, 2);
            const bool replacementBuilderReady = replacementBuilder.has_value();
            Expect(replacementBuilderReady, "replacement-builder-clones-old-root");
            if (replacementBuilderReady)
            {
                (void)replacementBuilder->Replace(0, TestRecord{17, 29, 4, true, 8});
                (void)replacementBuilder->Append(TestRecord{5, 11, 2, true, 9});
                const auto replacement = replacementBuilder->Seal();
                const bool replacementReady = replacement.has_value();
                Expect(replacementReady, "replacement-root-seals");
                if (replacementReady)
                {
                    const auto replacementBound = replacement->OwnershipBounds();
                    Expect(replacementBound.has_value(), "replacement-root-recomputes-bound-on-publication");
                    Expect(old.OwnershipBounds() == oldBound && retained.OwnershipBounds() == oldBound,
                           "replacement-preserves-captured-old-root-bounds");
                    Expect(old[0].value == 1 && retained[0].value == 1 && replacement->operator[](0).value == 8,
                           "replacement-keeps-old-records-immutable");
                    Expect(old != replacement.value(), "independent-roots-retain-value-equality-contract");
                    Expect(oldBound != replacementBound, "replacement-bound-reflects-new-child-costs");

                    TestRoot left = old;
                    TestRoot right = replacement.value();
                    left.swap(right);
                    Expect(left.OwnershipBounds() == replacement->OwnershipBounds() &&
                               right.OwnershipBounds() == oldBound,
                           "swap-transfers-cached-bounds-with-root-ownership");

                    TestRoot::Container records;
                    records.reserve(1);
                    (void)records.emplace_back(TestRecord{7, 13, 2, true, 77});
                    const std::size_t adoptedLimit = records.capacity();
                    const TestRecord* const adoptedBuffer = records.data();
                    auto adoptedBuilder = TestRoot::Builder::Adopt(std::move(records), adoptedLimit);
                    const bool adoptedBuilderReady = adoptedBuilder.has_value();
                    Expect(adoptedBuilder.has_value() && adoptedBuilder->Read().data() == adoptedBuffer,
                           "adoption-retains-original-record-buffer");
                    Expect(adoptedBuilderReady, "adopted-builder-is-created");
                    if (adoptedBuilderReady)
                    {
                        const auto adopted = adoptedBuilder->Seal();
                        Expect(adopted.has_value() && adopted->Read().data() == adoptedBuffer &&
                                   adopted->OwnershipBounds().has_value(),
                               "adopted-buffer-seals-with-bound-and-same-storage");
                        Expect(adopted.has_value() && old[0].value == 1 && adopted->operator[](0).value == 77,
                               "adoption-does-not-alter-an-existing-root");
                    }
                }
            }
        }
    }

    {
        TestRoot::Builder builder(1);
        (void)builder.Append(TestRecord{29, 37, 2, true, 29});
        MeasureControl::Reset();
        CancelSource alreadyCancelled;
        (void)alreadyCancelled.Cancel();
        const auto refused = builder.Seal(alreadyCancelled.Token());
        Expect(!refused.has_value() && MeasureControl::calls.load(std::memory_order_relaxed) == 0,
               "pre-cancelled-seal-stops-before-measuring-children");
        Expect(builder.CanSeal() && builder.Read().size() == 1 && builder.Read()[0].value == 29,
               "pre-cancelled-seal-preserves-builder");
        const auto retry = builder.Seal();
        Expect(retry.has_value() && retry->OwnershipBounds().has_value(),
               "pre-cancelled-builder-retries-with-fresh-token");
    }

    {
        TestRoot::Builder builder(4);
        (void)builder.Append(TestRecord{23, 31, 2, true, 23});
        CancelSource cancellation;
        (void)builder.Append(TestRecord{2, 3, 1, true, 24});
        (void)builder.Append(TestRecord{5, 7, 2, true, 25});
        (void)builder.Append(TestRecord{11, 13, 4, true, 26});
        MeasureControl::Reset();
        MeasureControl::cancelDuringMeasure.store(&cancellation, std::memory_order_relaxed);
        MeasureControl::cancelAtCall.store(2, std::memory_order_relaxed);
        const auto stopped = builder.Seal(cancellation.Token());
        MeasureControl::cancelDuringMeasure.store(nullptr, std::memory_order_relaxed);
        Expect(!stopped.has_value(), "cancellation-raised-by-measure-refuses-publication");
        Expect(MeasureControl::calls.load(std::memory_order_relaxed) == 2,
               "seal-checks-cancellation-between-individual-record-measures");
        Expect(builder.CanSeal() && builder.Read().size() == 4 && builder.Read()[0].value == 23 &&
                   builder.Read()[3].value == 26,
               "cancelled-measure-preserves-builder-records-for-retry");
        const auto retried = builder.Seal();
        Expect(retried.has_value() && retried->OwnershipBounds().has_value() && retried->operator[](0).value == 23,
               "cancelled-measure-builder-retries-successfully");
    }

    {
        AllocationProbe probe;
        AllocatorRoot::Builder builder(1, RetryAllocator<TestRecord>{&probe});
        (void)builder.Append(TestRecord{19, 27, 3, true, 19});
        probe.failNext.store(true, std::memory_order_relaxed);
        bool allocationFailed = false;
        try
        {
            (void)builder.Seal();
        }
        catch (const std::bad_alloc&)
        {
            allocationFailed = true;
        }
        Expect(allocationFailed, "real-sealing-allocation-fault-reaches-caller");
        Expect(builder.CanSeal() && builder.Read().size() == 1 && builder.Read()[0].value == 19,
               "allocation-fault-before-move-keeps-builder-retryable");
        const auto retried = builder.Seal();
        Expect(retried.has_value() && retried->OwnershipBounds().has_value() && retried->operator[](0).value == 19,
               "allocation-faulted-builder-retries-with-valid-bound");
        Expect(probe.allocations.load(std::memory_order_relaxed) >= 3,
               "allocator-observes-buffer-and-failed-plus-retried-root-allocations");
    }

    {
        const Raster base = Raster::Make(12, 12, RasterFormat::Bgra8888);
        std::vector<RasterTile> tiles;
        tiles.emplace_back(RasterTile{0, 0, Raster::Make(4, 4, RasterFormat::Bgra8888)});
        const Raster sparse = Raster::MakeSparse(24, 24, RasterFormat::Bgra8888, base, 3, 5, std::move(tiles), 0);
        ImageLayer layer = LayerWithRaster(sparse, L"sparse-layer-name-that-exceeds-small-string-storage");
        layer.asset->name = L"image-asset-name-with-retained-capacity";
        layer.mask = LayerMask{RasterAsset{Raster::Make(9, 9, RasterFormat::Gray8), Raster{}, L"mask-asset-name"}, true,
                               std::nullopt, true};
        layer.adjustment.emplace();
        layer.adjustment->kind = AdjustmentKind::Curves;
        layer.adjustment->curves.channels[0].reserve(11);
        layer.adjustment->curves.channels[0].emplace_back(CurvePoint{0, 0});
        layer.adjustment->curves.channels[0].emplace_back(CurvePoint{90, 120});
        layer.adjustment->curves.channels[0].emplace_back(CurvePoint{255, 255});
        layer.shape = LayerShapeStyle{};
        layer.shapeImage = sparse;
        const ImageLayerSample emptySample = SampleEmptyImageLayerRoot();
        ImageLayerSample sample = Sample(std::move(layer));
        const bool imageSampleReady =
            emptySample.bounds.has_value() && sample.bounds.has_value() && sample.root.size() == 1;
        Expect(imageSampleReady, "image-layer-policy-produces-bound-for-populated-layer");
        if (imageSampleReady)
        {
            const ImageLayer& stored = sample.root[0];
            const bool imageFieldsReady =
                stored.asset.has_value() && stored.mask.has_value() && stored.adjustment.has_value();
            Expect(imageFieldsReady, "image-layer-fixture-retains-image-mask-and-curves");
            if (imageFieldsReady)
            {
                const std::uint64_t knownRasterGraph =
                    SumRasterBytes({&stored.asset->image, &stored.asset->thumbnail, &stored.mask->asset.image,
                                    &stored.mask->asset.thumbnail, &stored.shapeImage});
                const std::uint64_t knownRasterNodes =
                    stored.asset->image.RetirementNodeCount() + stored.asset->thumbnail.RetirementNodeCount() +
                    stored.mask->asset.image.RetirementNodeCount() +
                    stored.mask->asset.thumbnail.RetirementNodeCount() + stored.shapeImage.RetirementNodeCount();
                // This one-record root owns one Guid/index entry in addition
                // to the record array. The empty root has no index entries.
                constexpr std::uint64_t oneLookupEntry = sizeof(std::pair<Guid, std::size_t>);
                const std::uint64_t imageRootBase =
                    emptySample.bounds->metadataBytes + sample.capacity * sizeof(ImageLayer) + oneLookupEntry;
                const std::uint64_t expectedChildMetadata = LayerChildMetadata(stored);
                Expect(sample.bounds->metadataBytes == imageRootBase + expectedChildMetadata,
                       "image-layer-metadata-uses-published-name-and-curve-capacities-plus-allowances");
                const std::uint64_t minimumEmptyCurveAllowance = 4 * 64;
                const std::uint64_t curveCapacityBytes =
                    stored.adjustment->curves.channels[0].capacity() * sizeof(CurvePoint) +
                    stored.adjustment->curves.channels[1].capacity() * sizeof(CurvePoint) +
                    stored.adjustment->curves.channels[2].capacity() * sizeof(CurvePoint) +
                    stored.adjustment->curves.channels[3].capacity() * sizeof(CurvePoint);
                Expect(expectedChildMetadata >= minimumEmptyCurveAllowance + curveCapacityBytes,
                       "image-layer-metadata-counts-empty-curve-channel-allowances");
                Expect(stored.adjustment->curves.channels[0].capacity() >= 11,
                       "moved-publication-retains-reserved-curve-capacity");
                const std::uint64_t imageOwnershipRootBase =
                    emptySample.bounds->ownershipBytes + sample.capacity * sizeof(ImageLayer) + oneLookupEntry;
                Expect(sample.bounds->ownershipBytes ==
                           imageOwnershipRootBase + expectedChildMetadata + knownRasterGraph,
                       "image-layer-total-adds-only-present-image-mask-shape-and-thumbnail-fields");
                Expect(sample.bounds->rasterNodes == emptySample.bounds->rasterNodes + knownRasterNodes,
                       "image-layer-node-total-adds-only-present-raster-field-occurrences");
                Expect(sample.capacity >= 1 && emptySample.capacity == 0,
                       "capacity-delta-uses-observed-allocator-provided-record-storage");
                Expect(stored.asset->image.IsSparse() && !stored.asset->image.IsMaterialized(),
                       "sparse-fixture-remains-unmaterialized-through-sealing");
                Expect(base.HasStorage() && stored.asset->image.HasStorage() && stored.mask->asset.image.HasStorage(),
                       "image-mask-and-sparse-raster-fixtures-own-storage");
                Expect(!stored.shapeImage.IsMaterialized() &&
                           stored.asset->image.Identity() == stored.shapeImage.Identity(),
                       "shape-image-and-layer-image-may-share-unmaterialized-owner");
                const auto beforeDataBound = sample.root.OwnershipBounds();
                const std::uint8_t* const assembled = stored.asset->image.Data();
                const auto afterDataBound = sample.root.OwnershipBounds();
                Expect(assembled != nullptr && stored.asset->image.IsMaterialized(),
                       "explicit-data-access-materializes-sparse-raster");
                Expect(beforeDataBound.has_value() && afterDataBound.has_value() && *beforeDataBound == *afterDataBound,
                       "post-data-access-image-layer-bound-remains-valid-and-stable");
                Expect(afterDataBound.has_value() && afterDataBound->ownershipBytes >= knownRasterGraph,
                       "precomputed-image-bound-covers-possible-sparse-assembly");
            }
        }
    }

    {
        const Raster sharedBase = Raster::Make(7, 5, RasterFormat::Bgra8888);
        std::vector<RasterTile> sharedTiles;
        sharedTiles.emplace_back(RasterTile{1, 1, Raster::Make(2, 2, RasterFormat::Bgra8888)});
        const Raster shared =
            Raster::MakeSparse(9, 9, RasterFormat::Bgra8888, sharedBase, 0, 0, std::move(sharedTiles), 0);
        ImageLayer repeated = LayerWithRaster(shared, L"repeated-owner");
        repeated.asset->thumbnail = shared;
        repeated.mask = LayerMask{RasterAsset{shared, shared, L"same-storage-again"}, true, std::nullopt, true};
        repeated.shape = LayerShapeStyle{};
        repeated.shapeImage = shared;
        ImageLayerSample repeatedSample = Sample(std::move(repeated));
        const bool repeatedSampleReady = repeatedSample.bounds.has_value() && repeatedSample.root.size() == 1;
        Expect(repeatedSampleReady, "repeated-raster-owners-still-have-bound");
        if (repeatedSampleReady)
        {
            const ImageLayer& stored = repeatedSample.root[0];
            const bool repeatedFieldsReady = stored.asset.has_value() && stored.mask.has_value();
            Expect(repeatedFieldsReady, "repeated-raster-fixture-retains-image-and-mask");
            if (repeatedFieldsReady)
            {
                const std::uint64_t occurrenceTotal =
                    SumRasterBytes({&stored.asset->image, &stored.asset->thumbnail, &stored.mask->asset.image,
                                    &stored.mask->asset.thumbnail, &stored.shapeImage});
                Expect(repeatedSample.bounds->ownershipBytes >= occurrenceTotal,
                       "repeated-raster-occurrences-are-conservatively-overcounted");
                Expect(repeatedSample.bounds->rasterNodes >= 5 * shared.RetirementNodeCount(),
                       "repeated-raster-node-occurrences-are-conservatively-counted");
                Expect(stored.shapeImage.IsSparse() && !stored.shapeImage.IsMaterialized(),
                       "repeated-owner-accounting-does-not-materialize-sparse-raster");
                Expect(shared.HasStorage(), "repeated-owner-sparse-fixture-has-storage");

                ImageLayer shorter = LayerWithRaster(shared, L"short");
                ImageLayerSample shortSample = Sample(std::move(shorter));
                Expect(shortSample.bounds.has_value() && shortSample.root.size() == 1 &&
                           repeatedSample.bounds->metadataBytes >= shortSample.bounds->metadataBytes,
                       "larger-retained-layer-and-asset-names-increase-metadata-bound");
            }
        }
    }

    {
        const Raster raster = Raster::Make(5, 5, RasterFormat::Bgra8888);
        ImageLayer original = LayerWithRaster(raster, L"old-layer");
        RecordRoot<ImageLayer>::Builder imageBuilder(1);
        (void)imageBuilder.Append(original);
        const auto imageRoot = imageBuilder.Seal();
        const bool imageRootReady = imageRoot.has_value();
        Expect(imageRootReady, "image-layer-source-root-seals");
        if (imageRootReady)
        {
            const auto originalBound = imageRoot->OwnershipBounds();
            Expect(original.asset.has_value() && original.asset->image.HasStorage(),
                   "image-layer-copy-fixture-retains-raster-storage");
            auto nextBuilder = RecordRoot<ImageLayer>::Builder::Clone(imageRoot.value(), 1);
            const bool nextBuilderReady = nextBuilder.has_value();
            Expect(nextBuilderReady, "image-layer-replacement-builder-clones-source");
            if (nextBuilderReady)
            {
                (void)nextBuilder->Replace(0, LayerWithRaster(raster, L"new-layer-name-with-more-capacity"));
                const auto next = nextBuilder->Seal();
                const bool nextReady = next.has_value();
                Expect(nextReady, "image-layer-replacement-root-seals");
                if (nextReady)
                {
                    RecordRoot<ImageLayer> retained = imageRoot.value();
                    Expect(originalBound.has_value() && next->OwnershipBounds().has_value(),
                           "image-layer-record-roots-cache-valid-bounds");
                    Expect(imageRoot->OwnershipBounds() == originalBound && retained.OwnershipBounds() == originalBound,
                           "image-layer-root-copies-retain-prior-bound");
                    Expect(original.name == L"old-layer" && retained[0].name == L"old-layer" &&
                               next->operator[](0).name != original.name,
                           "image-layer-replacement-preserves-old-record-values");
                }
            }
        }
    }

    CanvasDocumentRetirementBounds();
    std::printf("SUMMARY record-bounds checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

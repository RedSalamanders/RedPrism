#include "Document.Tests.PreparedRasterPreview.h"

#include "PreparedRasterPreview.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "RasterEditWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s prepared-raster-preview.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kBlackPixels{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
constexpr std::array<std::uint8_t, 16> kLinearGradientPixels{0,   0,   0,   255, 85,  85,  85,  255,
                                                             170, 170, 170, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 2> kMaskGradientPixels{0, 255};

Raster MakeBgra(int width, int height, const std::uint8_t* bytes, std::size_t count)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes, count);
}

Raster MakeGray(int width, int height, const std::uint8_t* bytes, std::size_t count)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes, count);
}

bool HasPixels(const Raster& raster, RasterFormat format, int width, int height, const std::uint8_t* expected,
               std::size_t count)
{
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    const std::size_t required =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        count != required)
        return false;
    std::vector<std::uint8_t> actual(required);
    raster.ReadRegion(0, 0, width, height, actual.data(), width * channels, 0);
    for (std::size_t i = 0; i < required; ++i)
    {
        if (actual[i] != expected[i])
            return false;
    }
    return true;
}

std::uint64_t ExpectedOwnershipBytes(const BrushStroke& stroke, const PreparedRasterPreview& prepared)
{
    std::uint64_t expected = static_cast<std::uint64_t>(sizeof(PreparedRasterPreview)) + 65536u +
                             static_cast<std::uint64_t>(prepared.tiles.capacity()) * sizeof(RasterTile);
    if (stroke.SourceImage().HasStorage())
        expected += stroke.SourceImage().RetirementMemoryBytes();
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        if (tile.touched)
            expected += tile.image.RetirementMemoryBytes();
    }
    return expected;
}

void CheckWorkerCopy(BrushStroke& stroke, std::thread::id owner, const char* prefix,
                     std::shared_ptr<const PreparedRasterPreview>& prepared)
{
    std::vector<bool> changedFlags;
    std::vector<std::size_t> changedList;
    changedFlags.reserve(stroke.Tiles().size());
    for (std::size_t index = 0; index < stroke.Tiles().size(); ++index)
    {
        const BrushStroke::Tile& tile = stroke.Tiles()[index];
        changedFlags.push_back(tile.changed);
        if (tile.changed)
            changedList.push_back(index);
    }

    std::jthread worker([&stroke, owner, &prepared] { prepared = PrepareRasterPreviewForWork(stroke, owner); });
    worker.join();
    Expect(!worker.joinable(), prefix);

    bool flagsPreserved = stroke.Tiles().size() == changedFlags.size();
    for (std::size_t index = 0; flagsPreserved && index < changedFlags.size(); ++index)
        flagsPreserved = stroke.Tiles()[index].changed == changedFlags[index];
    const std::vector<std::size_t> afterChangedList = stroke.TakeChanged();
    Expect(flagsPreserved && afterChangedList == changedList, "changed-state-and-pending-list-preserved");
}

void CheckMetadataAndTileOwners(const BrushStroke& stroke, const PreparedRasterPreview& prepared, const char* name)
{
    std::size_t touchedCount = 0;
    bool tilesMatch = true;
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        if (!tile.touched)
            continue;
        ++touchedCount;
        std::size_t found = prepared.tiles.size();
        for (std::size_t index = 0; index < prepared.tiles.size(); ++index)
        {
            if (prepared.tiles[index].x == tile.rect.x && prepared.tiles[index].y == tile.rect.y)
            {
                found = index;
                break;
            }
        }
        if (found == prepared.tiles.size())
        {
            tilesMatch = false;
            continue;
        }
        const RasterTile& copy = prepared.tiles[found];
        tilesMatch = tilesMatch && copy.Width() == tile.rect.width && copy.Height() == tile.rect.height &&
                     copy.pixels.Identity() == tile.image.Identity() && copy.sourceX == 0 && copy.sourceY == 0;
    }
    Expect(prepared.target == stroke.LayerID() && prepared.mask == stroke.IsMask() &&
               prepared.width == stroke.Width() && prepared.height == stroke.Height() &&
               prepared.transform == stroke.PaintTransform() && prepared.fill == stroke.FillValue() &&
               prepared.source.Identity() == stroke.SourceImage().Identity() &&
               prepared.sourceRect.x == stroke.SourceRect().x && prepared.sourceRect.y == stroke.SourceRect().y &&
               prepared.sourceRect.width == stroke.SourceRect().width &&
               prepared.sourceRect.height == stroke.SourceRect().height,
           name);
    Expect(tilesMatch && prepared.tiles.size() == touchedCount, "every-touched-tile-retained-by-identity");
    Expect(prepared.ownershipBytes == ExpectedOwnershipBytes(stroke, prepared), "cached-ownership-arithmetic");
}

void OwnerUnknownAndCancelledRefuse(BrushStroke& stroke, std::thread::id owner)
{
    Expect(!PrepareRasterPreviewForWork(stroke, owner), "owner-thread-refused-without-output");
    Expect(!PrepareRasterPreviewForWork(stroke, std::thread::id{}), "unknown-owner-refused-without-output");
    CancelSource cancellation;
    (void)cancellation.Cancel();
    std::shared_ptr<const PreparedRasterPreview> refused;
    std::jthread cancelledWorker([&] { refused = PrepareRasterPreviewForWork(stroke, owner, cancellation.Token()); });
    cancelledWorker.join();
    Expect(!refused, "worker-pre-cancel-refused-without-output");
}

void LastOwnerReleaseQueuesCompleteMetadata(std::shared_ptr<const PreparedRasterPreview>& prepared)
{
    if (!prepared)
    {
        Expect(false, "retirement-fixture-has-prepared-metadata");
        return;
    }
    struct PendingRelease
    {
        DeferredReleaseNode* node = nullptr;
        unsigned int calls = 0;
    } pending;
    const bool installed = InstallDeferredReleaseSink(
        [](void* context, DeferredReleaseNode& node) noexcept
        {
            auto& observed = *static_cast<PendingRelease*>(context);
            observed.node = &node;
            ++observed.calls;
        },
        &pending);
    Expect(installed, "owner-release-sink-installs");
    if (!installed)
        return;
    const auto* borrowed = prepared.get();
    const auto declared = prepared->ownershipBytes;
    const bool belowThreshold = declared < kDeferredReleaseThreshold;
    const Raster retainedTile = prepared->tiles.empty() ? Raster{} : prepared->tiles.front().pixels;
    const std::weak_ptr<const PreparedRasterPreview> weak = prepared;
    prepared.reset();
    Expect(belowThreshold && weak.expired() && pending.calls == 1 && pending.node && pending.node->destroy &&
               pending.node->bytes == declared && !borrowed->tiles.empty() &&
               borrowed->tiles.front().pixels.Identity() == retainedTile.Identity(),
           "below-threshold-last-owner-release-queues-complete-tile-graph");
    Expect(ClearDeferredReleaseSink(&pending), "owner-release-sink-clears-before-join");
    if (pending.node)
    {
        const auto owner = std::this_thread::get_id();
        bool offOwner = false;
        std::jthread retirement(
            [&]
            {
                offOwner = std::this_thread::get_id() != owner;
                pending.node->destroy(pending.node);
                pending.node = nullptr;
            });
        retirement.join();
        Expect(offOwner && !pending.node && !retirement.joinable(), "complete-metadata-retirement-worker-joins");
    }
}

void GradientImageRetainsLiteralPixelsAndSource()
{
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    const Raster source = MakeBgra(4, 1, kBlackPixels.data(), kBlackPixels.size());
    if (!source.HasStorage() || !session.ActiveLayer())
    {
        Expect(false, "gradient-image-fixture-ready");
        return;
    }
    const Guid target = session.ActiveLayer()->id;
    EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                            { document.layers[0].asset = RasterAsset{source, {}, L"Preview source"}; });
    const std::uint64_t generation = session.Generation();
    const ImageLayer layer = *session.ActiveLayer();
    const Raster original = layer.asset->image;
    RasterEditInput input;
    input.layer = layer;
    input.canvasWidth = 4;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::Gradient;
    input.start = Vec2{0.5, 0.5};
    input.end = Vec2{3.5, 0.5};
    input.color = {0, 0, 0, 1};
    input.endColor = {1, 1, 1, 1};

    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    Expect(preview.status == RasterEditStatus::Ready && preview.stroke &&
               preview.stroke->SourceImage().Identity() == original.Identity(),
           "gradient-private-completed-stroke-ready");
    if (!preview.stroke)
        return;
    Expect(HasPixels(preview.stroke->SourceImage(), RasterFormat::Bgra8888, 4, 1, kBlackPixels.data(),
                     kBlackPixels.size()),
           "gradient-source-retains-independent-literal");
    Expect(!preview.stroke->Tiles().empty() &&
               HasPixels(preview.stroke->Tiles().front().image, RasterFormat::Bgra8888, 4, 1,
                         kLinearGradientPixels.data(), kLinearGradientPixels.size()),
           "gradient-tile-retains-existing-four-pixel-literal");

    const std::thread::id owner = std::this_thread::get_id();
    OwnerUnknownAndCancelledRefuse(*preview.stroke, owner);
    std::shared_ptr<const PreparedRasterPreview> prepared;
    CheckWorkerCopy(*preview.stroke, owner, "gradient-worker-explicitly-joined", prepared);
    Expect(prepared != nullptr, "gradient-worker-publishes-metadata");
    if (prepared)
    {
        CheckMetadataAndTileOwners(*preview.stroke, *prepared, "gradient-image-metadata-and-source-identity");
        Expect(prepared->source.Width() == 4 && prepared->source.Height() == 1 && prepared->fill == 0,
               "gradient-image-source-dimensions-and-transparent-fill");
        Expect(prepared->sourceRect.x == 0 && prepared->sourceRect.y == 0 && prepared->sourceRect.width == 4 &&
                   prepared->sourceRect.height == 1 && prepared->tiles.size() == 1 && prepared->tiles[0].x == 0 &&
                   prepared->tiles[0].y == 0 && prepared->tiles[0].Width() == 4 && prepared->tiles[0].Height() == 1 &&
                   prepared->tiles[0].sourceX == 0 && prepared->tiles[0].sourceY == 0,
               "gradient-image-literal-source-rectangle-and-tile-window");
        LastOwnerReleaseQueuesCompleteMetadata(prepared);
    }
    Expect(session.Generation() == generation && session.ActiveLayerID() == target &&
               session.ActiveLayer()->asset->image.Identity() == original.Identity() &&
               HasPixels(original, RasterFormat::Bgra8888, 4, 1, kBlackPixels.data(), kBlackPixels.size()),
           "gradient-standalone-source-and-document-generation-unchanged");
}

void GradientMaskRetainsSourceGridAndPlacement()
{
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    constexpr std::array<std::uint8_t, 16> imagePixels{0, 0, 70, 255, 0, 0, 80, 255, 0, 0, 90, 255, 0, 0, 100, 255};
    constexpr std::array<std::uint8_t, 2> maskPixels{31, 219};
    const Raster image = MakeBgra(4, 1, imagePixels.data(), imagePixels.size());
    const Raster mask = MakeGray(2, 1, maskPixels.data(), maskPixels.size());
    if (!image.HasStorage() || !mask.HasStorage() || !session.ActiveLayer())
    {
        Expect(false, "mask-fixture-ready");
        return;
    }
    LayerTransform placement;
    placement.origin = Vec2{1, 0};
    placement.size = Vec2{2, 1};
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers[0];
                                layer.asset = RasterAsset{image, {}, L"Image"};
                                layer.mask = LayerMask{RasterAsset{mask, {}, L"Placed mask"}, true, placement, false};
                            });
    const std::uint64_t generation = session.Generation();
    RasterEditInput input;
    input.layer = *session.ActiveLayer();
    input.canvasWidth = 4;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.mask = true;
    input.kind = RasterEditKind::Gradient;
    input.start = Vec2{1.5, 0.5};
    input.end = Vec2{2.5, 0.5};
    input.color = {0, 0, 0, 1};
    input.endColor = {1, 1, 1, 1};

    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    Expect(preview.status == RasterEditStatus::Ready && preview.stroke && preview.stroke->IsMask() &&
               preview.stroke->SourceImage().Identity() == mask.Identity(),
           "gradient-mask-private-completed-stroke-ready");
    if (!preview.stroke)
        return;
    const std::thread::id owner = std::this_thread::get_id();
    OwnerUnknownAndCancelledRefuse(*preview.stroke, owner);
    std::shared_ptr<const PreparedRasterPreview> prepared;
    CheckWorkerCopy(*preview.stroke, owner, "mask-worker-explicitly-joined", prepared);
    Expect(prepared != nullptr, "mask-worker-publishes-metadata");
    if (prepared)
    {
        CheckMetadataAndTileOwners(*preview.stroke, *prepared, "gradient-mask-metadata-and-source-identity");
        Expect(prepared->tiles.size() == 1 && HasPixels(prepared->tiles[0].pixels, RasterFormat::Gray8, 2, 1,
                                                        kMaskGradientPixels.data(), kMaskGradientPixels.size()),
               "gradient-mask-two-pixel-literal");
        Expect(prepared->source.Width() == 2 && prepared->source.Height() == 1 && prepared->fill == 255,
               "mask-source-keeps-own-dimensions-and-white-fill");
        Expect(prepared->width == 2 && prepared->height == 1 && prepared->sourceRect.x == 0 &&
                   prepared->sourceRect.y == 0 && prepared->sourceRect.width == 2 && prepared->sourceRect.height == 1 &&
                   prepared->transform == placement && input.layer.mask->placement == placement &&
                   input.layer.mask->asset.image.Identity() == mask.Identity(),
               "mask-placement-and-original-sized-source-preserved");
        Expect(prepared->sourceRect.x == 0 && prepared->sourceRect.y == 0 && prepared->sourceRect.width == 2 &&
                   prepared->sourceRect.height == 1 && prepared->tiles.size() == 1 && prepared->tiles[0].x == 0 &&
                   prepared->tiles[0].y == 0 && prepared->tiles[0].Width() == 2 && prepared->tiles[0].Height() == 1 &&
                   prepared->tiles[0].sourceX == 0 && prepared->tiles[0].sourceY == 0,
               "gradient-mask-literal-source-rectangle-and-tile-window");
    }
    Expect(session.Generation() == generation &&
               session.ActiveLayer()->mask->asset.image.Identity() == mask.Identity() &&
               HasPixels(mask, RasterFormat::Gray8, 2, 1, maskPixels.data(), maskPixels.size()),
           "gradient-standalone-mask-and-document-generation-unchanged");
}

void PixelMoveImageRetainsCompletedTiles()
{
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    constexpr std::array<std::uint8_t, 16> sourcePixels{11, 22, 33, 255, 0, 0, 0, 0, 77, 88, 99, 255, 0, 0, 0, 0};
    const Raster source = MakeBgra(4, 1, sourcePixels.data(), sourcePixels.size());
    if (!source.HasStorage() || !session.ActiveLayer())
    {
        Expect(false, "pixel-move-image-fixture-ready");
        return;
    }
    EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                            { document.layers[0].asset = RasterAsset{source, {}, L"Pixel move source"}; });
    const std::uint64_t generation = session.Generation();
    RasterEditInput input;
    input.layer = *session.ActiveLayer();
    input.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};
    input.canvasWidth = 4;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{1, 0};

    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    Expect(preview.status == RasterEditStatus::Ready && preview.stroke && preview.pixelMoveOpening,
           "pixel-move-image-private-completed-stroke-ready");
    if (!preview.stroke)
        return;
    const std::thread::id owner = std::this_thread::get_id();
    OwnerUnknownAndCancelledRefuse(*preview.stroke, owner);
    std::shared_ptr<const PreparedRasterPreview> prepared;
    CheckWorkerCopy(*preview.stroke, owner, "pixel-move-worker-explicitly-joined", prepared);
    Expect(prepared != nullptr, "pixel-move-worker-publishes-metadata");
    if (prepared)
    {
        CheckMetadataAndTileOwners(*preview.stroke, *prepared, "pixel-move-image-metadata-and-source-identity");
        Expect(prepared->source.Width() == 4 && prepared->source.Height() == 1 && prepared->fill == 0,
               "pixel-move-image-source-dimensions-and-transparent-fill");
        Expect(prepared->sourceRect.x == 0 && prepared->sourceRect.y == 0 && prepared->sourceRect.width == 4 &&
                   prepared->sourceRect.height == 1 && prepared->tiles.size() == 1 && prepared->tiles[0].x == 0 &&
                   prepared->tiles[0].y == 0 && prepared->tiles[0].Width() == 4 && prepared->tiles[0].Height() == 1 &&
                   prepared->tiles[0].sourceX == 0 && prepared->tiles[0].sourceY == 0,
               "pixel-move-literal-source-rectangle-and-tile-window");
    }
    Expect(session.Generation() == generation && session.ActiveLayer()->asset->image.Identity() == source.Identity() &&
               HasPixels(source, RasterFormat::Bgra8888, 4, 1, sourcePixels.data(), sourcePixels.size()),
           "pixel-move-standalone-source-and-document-generation-unchanged");
}

void GradientMaskRetainsNearestStretchMetadata()
{
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    constexpr std::array<std::uint8_t, 2> sourcePixels{31, 219};
    constexpr std::array<std::uint8_t, 4> gradientPixels{0, 85, 170, 255};
    const auto source = MakeGray(2, 1, sourcePixels.data(), sourcePixels.size());
    EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                            { document.layers[0].mask = LayerMask{RasterAsset{source, {}, L"Stretched mask"}, true}; });
    RasterEditInput input;
    input.layer = *session.ActiveLayer();
    input.canvasWidth = 4;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.mask = true;
    input.kind = RasterEditKind::Gradient;
    input.start = {0.5, 0.5};
    input.end = {3.5, 0.5};
    input.color = {0, 0, 0, 1};
    input.endColor = {1, 1, 1, 1};
    auto preview = RunRasterEditPreview(input);
    Expect(preview.status == RasterEditStatus::Ready && preview.stroke,
           "unplaced-stretched-mask-private-completed-stroke-ready");
    if (!preview.stroke)
        return;
    std::shared_ptr<const PreparedRasterPreview> prepared;
    CheckWorkerCopy(*preview.stroke, std::this_thread::get_id(), "unplaced-mask-worker-joined", prepared);
    Expect(prepared && prepared->source == source && prepared->source.Width() == 2 && prepared->source.Height() == 1 &&
               prepared->sourceRect.x == 0 && prepared->sourceRect.y == 0 && prepared->sourceRect.width == 4 &&
               prepared->sourceRect.height == 1 && prepared->width == 4 && prepared->height == 1 &&
               prepared->transform == input.layer.transform && prepared->tiles.size() == 1 &&
               HasPixels(prepared->tiles[0].pixels, RasterFormat::Gray8, 4, 1, gradientPixels.data(),
                         gradientPixels.size()),
           "unplaced-mask-original-2x1-source-and-4x1-nearest-stretch-window");
    Expect(HasPixels(source, RasterFormat::Gray8, 2, 1, sourcePixels.data(), sourcePixels.size()),
           "unplaced-mask-original-literal-bytes-unchanged");
}
} // namespace

int RunPreparedRasterPreviewTests()
{
    gFailures = 0;
    GradientImageRetainsLiteralPixelsAndSource();
    GradientMaskRetainsSourceGridAndPlacement();
    GradientMaskRetainsNearestStretchMetadata();
    PixelMoveImageRetainsCompletedTiles();
    return gFailures;
}

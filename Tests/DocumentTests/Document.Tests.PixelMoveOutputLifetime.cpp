#include "Document.Tests.PixelMoveOutputLifetime.h"

#include "Document.h"
#include "RasterEditWork.h"
#include "RasterWork.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace PixelMoveOutputLifetimeTests
{
namespace
{
void Expect(int& failures, bool condition, const char* fact)
{
    std::printf("%s pixelmove-output-lifetime.%s\n", condition ? "PASS" : "FAIL", fact);
    failures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

ImageLayer Image(const Raster& pixels, int width, int height)
{
    ImageLayer layer =
        ImageLayer::Blank(L"Pixel move lifetime", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.asset = RasterAsset{pixels, Raster{}, layer.name};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

RasterEditInput PixelMoveInput(ImageLayer layer, int canvasWidth, int canvasHeight, DocumentSelection selection,
                               double offsetX, bool duplicate)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{offsetX, 0};
    input.duplicate = duplicate;
    return input;
}

bool PixelsEqual(const Raster& raster, RasterFormat format, int width, int height,
                 std::span<const std::uint8_t> expected)
{
    const std::size_t channels = format == RasterFormat::Gray8 ? 1u : 4u;
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels)
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, width, height, actual.data(), width * static_cast<int>(channels), 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

struct LifetimeFacts final
{
    bool ready = false;
    bool literalPixels = false;
    bool memoryAliveWithSelectedStrongStorage = false;
    bool selectedWeakStorageExpiresAfterStrongRelease = false;
    bool memoryAliveWithSelectedWeakStorage = false;
    bool memoryExpiresAfterSelectedWeakStorageRelease = false;
    bool oldSourceIdentityPreserved = false;
    bool outputRootReleased = false;
    bool noSparseAssembly = false;
    bool oldSourceDoesNotRetainOutputMemory = false;
    bool maskAloneRetainsOutputMemory = false;
    bool thumbnailAloneRetainsOutputMemory = false;
};

void RootRasterOwnsOutputMemory(LifetimeFacts& facts)
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{10, 20, 30, 255, 40,  50,  60,  255,
                                                       70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 16> expected{10, 20, 30, 255, 0, 0, 0, 0, 70, 80, 90, 255, 40, 50, 60, 255};
    Raster source = Bgra(4, 1, sourceBytes);
    RasterEditInput input = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1), 2, false);
    const void* const sourceIdentity = source.Identity();
    auto outputMemory = SharedPublicationMemory::CreateForWork();
    input.outputMemory = outputMemory;
    std::weak_ptr<const SharedPublicationMemory> memory = outputMemory;

    RasterEditResult result = RunRasterEdit(input);
    if (result.layer && result.layer->asset)
    {
        const Raster& output = result.layer->asset->image;
        facts.ready = result.status == RasterEditStatus::Ready;
        facts.literalPixels = PixelsEqual(output, RasterFormat::Bgra8888, 4, 1, expected);
        facts.oldSourceIdentityPreserved = source.Identity() == sourceIdentity && input.layer.asset &&
                                           input.layer.asset->image.Identity() == sourceIdentity;
        facts.noSparseAssembly = output.IsSparse() && !output.IsMaterialized();
        auto selectedWeakStorage = output.WeakStorageForTest();
        auto selectedStorage = selectedWeakStorage.lock();
        result = {};
        input = {};
        outputMemory.reset();
        facts.memoryAliveWithSelectedStrongStorage = !memory.expired();
        selectedStorage.reset();
        facts.selectedWeakStorageExpiresAfterStrongRelease = selectedWeakStorage.expired();
        facts.memoryAliveWithSelectedWeakStorage = !memory.expired();
        selectedWeakStorage.reset();
    }
    else
    {
        result = {};
        input = {};
        outputMemory.reset();
    }
    facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired();
    facts.oldSourceDoesNotRetainOutputMemory = !memory.expired() ? false : source.HasStorage();
    source = {};
}

void PaintedTileOwnsOutputMemory(LifetimeFacts& facts)
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{10, 20, 30, 255, 40,  50,  60,  255,
                                                       70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 16> expected{10, 20, 30, 255, 0, 0, 0, 0, 70, 80, 90, 255, 40, 50, 60, 255};
    Raster source = Bgra(4, 1, sourceBytes);
    RasterEditInput input = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1), 2, false);
    const void* const sourceIdentity = source.Identity();
    auto outputMemory = SharedPublicationMemory::CreateForWork();
    input.outputMemory = outputMemory;
    std::weak_ptr<const SharedPublicationMemory> memory = outputMemory;

    RasterEditResult result = RunRasterEdit(input);
    if (result.layer && result.layer->asset)
    {
        const Raster& output = result.layer->asset->image;
        facts.ready = result.status == RasterEditStatus::Ready;
        facts.literalPixels = PixelsEqual(output, RasterFormat::Bgra8888, 4, 1, expected);
        facts.oldSourceIdentityPreserved = source.Identity() == sourceIdentity && input.layer.asset &&
                                           input.layer.asset->image.Identity() == sourceIdentity;
        facts.noSparseAssembly = output.IsSparse() && !output.IsMaterialized() && output.Tiles().size() == 1;
        if (!output.Tiles().empty())
        {
            facts.literalPixels = facts.literalPixels && output.Tiles().front().pixels.Identity() != sourceIdentity;
            auto selectedWeakStorage = output.Tiles().front().pixels.WeakStorageForTest();
            auto selectedStorage = selectedWeakStorage.lock();
            auto outputRoot = output.WeakStorageForTest();
            result = {};
            input = {};
            outputMemory.reset();
            facts.outputRootReleased = outputRoot.expired();
            outputRoot.reset();
            facts.memoryAliveWithSelectedStrongStorage = !memory.expired();
            selectedStorage.reset();
            facts.selectedWeakStorageExpiresAfterStrongRelease = selectedWeakStorage.expired();
            facts.memoryAliveWithSelectedWeakStorage = !memory.expired();
            selectedWeakStorage.reset();
        }
    }
    result = {};
    input = {};
    outputMemory.reset();
    facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired();
    source = {};
}

void TranslatedLoopsOwnOutputMemory(LifetimeFacts& facts)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{0, 0, 255, 255, 255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 8> expected{0, 0, 255, 255, 0, 0, 255, 255};
    Raster source = Bgra(2, 1, sourceBytes);
    RasterEditInput input = PixelMoveInput(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1), 1, true);
    auto outputMemory = SharedPublicationMemory::CreateForWork();
    input.outputMemory = outputMemory;
    std::weak_ptr<const SharedPublicationMemory> memory = outputMemory;

    RasterEditResult result = RunRasterEdit(input);
    if (result.layer && result.layer->asset && result.selection)
    {
        facts.ready = result.status == RasterEditStatus::Ready;
        facts.literalPixels = PixelsEqual(result.layer->asset->image, RasterFormat::Bgra8888, 2, 1, expected) &&
                              result.selection->loops.size() == 1 && result.selection->loops[0].size() == 4 &&
                              result.selection->loops[0][0] == Vec2{1, 0} &&
                              result.selection->loops[0][2] == Vec2{2, 1};
        auto selectedWeakStorage = result.selection->loops.WeakStorageForTest();
        auto selectedStorage = selectedWeakStorage.lock();
        result = {};
        input = {};
        outputMemory.reset();
        facts.memoryAliveWithSelectedStrongStorage = !memory.expired();
        selectedStorage.reset();
        facts.selectedWeakStorageExpiresAfterStrongRelease = selectedWeakStorage.expired();
        facts.memoryAliveWithSelectedWeakStorage = !memory.expired();
        selectedWeakStorage.reset();
    }
    result = {};
    input = {};
    outputMemory.reset();
    facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired();
    source = {};
}

void GrownCarriedMaskAndThumbnailOwnOutputMemory(LifetimeFacts& facts, bool retainThumbnail)
{
    constexpr int sourceWidth = 98;
    constexpr int canvasWidth = 120;
    std::array<std::uint8_t, sourceWidth * 4> imageBytes{};
    std::array<std::uint8_t, sourceWidth> maskBytes{};
    imageBytes[4 * 96 + 2] = 255;
    imageBytes[4 * 96 + 3] = 255;
    std::fill(maskBytes.begin(), maskBytes.end(), std::uint8_t{255});
    Raster source = Bgra(sourceWidth, 1, imageBytes);
    Raster mask = Gray(sourceWidth, 1, maskBytes);
    ImageLayer layer = Image(source, sourceWidth, 1);
    layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"semantic mask"}, true, std::nullopt, true};
    RasterEditInput input = PixelMoveInput(std::move(layer), canvasWidth, 1, Rect(96, 0, 1, 1), 5, false);
    auto outputMemory = SharedPublicationMemory::CreateForWork();
    input.outputMemory = outputMemory;
    std::weak_ptr<const SharedPublicationMemory> memory = outputMemory;

    RasterEditResult result = RunRasterEdit(input);
    if (result.status == RasterEditStatus::Ready && result.layer && result.layer->mask)
    {
        const Raster& carried = result.layer->mask->asset.image;
        const Raster& thumbnail = result.layer->mask->asset.thumbnail;
        std::array<std::uint8_t, 96> expectedThumbnail{};
        expectedThumbnail.fill(255);
        facts.ready = carried.Width() > 96 && carried.Format() == RasterFormat::Gray8;
        facts.literalPixels = thumbnail.HasStorage() && thumbnail.Format() == RasterFormat::Gray8 &&
                              thumbnail.Width() == 96 && thumbnail != input.layer.mask->asset.thumbnail &&
                              carried.Width() > sourceWidth &&
                              PixelsEqual(thumbnail, RasterFormat::Gray8, 96, 1, expectedThumbnail);
        auto selectedWeakStorage = retainThumbnail ? thumbnail.WeakStorageForTest() : carried.WeakStorageForTest();
        auto selectedStorage = selectedWeakStorage.lock();
        result = {};
        input = {};
        outputMemory.reset();
        facts.memoryAliveWithSelectedStrongStorage = !memory.expired();
        selectedStorage.reset();
        facts.selectedWeakStorageExpiresAfterStrongRelease = selectedWeakStorage.expired();
        facts.memoryAliveWithSelectedWeakStorage = !memory.expired();
        if (retainThumbnail)
            facts.thumbnailAloneRetainsOutputMemory = facts.memoryAliveWithSelectedWeakStorage;
        else
            facts.maskAloneRetainsOutputMemory = facts.memoryAliveWithSelectedWeakStorage;
        selectedWeakStorage.reset();
        facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired();
        facts.oldSourceDoesNotRetainOutputMemory =
            facts.memoryExpiresAfterSelectedWeakStorageRelease && source.HasStorage() && mask.HasStorage();
    }
    result = {};
    input = {};
    outputMemory.reset();
    facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired();
    source = {};
    mask = {};
}

void SmallSparseThumbnailLifetime(LifetimeFacts& facts)
{
    constexpr std::array<std::uint8_t, 4> expected{7, 11, 13, 17};
    auto base = Gray(4, 1, expected);
    auto sparse = Raster::MakeSparse(4, 1, RasterFormat::Gray8, base, 0, 0, {}, 0);
    const auto identity = sparse.Identity();
    auto outputMemory = SharedPublicationMemory::CreateForWork();
    std::weak_ptr<const SharedPublicationMemory> memory = outputMemory;
    auto thumbnail = ThumbnailRasterForWork(sparse, 96, {}, outputMemory);
    facts.literalPixels = PixelsEqual(thumbnail, RasterFormat::Gray8, 4, 1, expected);
    facts.noSparseAssembly = sparse.IsSparse() && !sparse.IsMaterialized() && sparse.Identity() == identity;
    auto weak = thumbnail.WeakStorageForTest();
    thumbnail = {};
    outputMemory.reset();
    facts.selectedWeakStorageExpiresAfterStrongRelease = weak.expired();
    facts.memoryAliveWithSelectedWeakStorage = !memory.expired();
    weak.reset();
    facts.memoryExpiresAfterSelectedWeakStorageRelease = memory.expired() && sparse.HasStorage();
}

void WrongOperationMemoryRefused(bool& refused)
{
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    auto image = Bgra(1, 1, red);
    auto input = PixelMoveInput(Image(image, 1, 1), 1, 1, Rect(0, 0, 1, 1), 1, false);
    input.kind = RasterEditKind::Invert;
    input.outputMemory = SharedPublicationMemory::CreateForWork();
    const auto result = RunRasterEdit(input);
    input.outputMemory.reset();
    const auto positive = RunRasterEdit(input);
    refused = result.status == RasterEditStatus::Invalid && !result.layer &&
              positive.status == RasterEditStatus::Ready && positive.layer &&
              PixelsEqual(image, RasterFormat::Bgra8888, 1, 1, red);
}

struct PixelParityFacts final
{
    bool moveMemoryOutputIsLiteral = false;
    bool moveDefaultsHaveSamePixels = false;
    bool duplicateMemoryOutputIsLiteral = false;
    bool duplicateDefaultsHaveSamePixels = false;
};

void MoveDuplicateAndDefaultParity(PixelParityFacts& facts)
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255};
    constexpr std::array<std::uint8_t, 16> moveExpected{0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 255, 255, 0, 255, 0, 255};
    constexpr std::array<std::uint8_t, 16> duplicateExpected{0, 0, 255, 255, 255, 0,   0, 255,
                                                             0, 0, 255, 255, 0,   255, 0, 255};
    Raster source = Bgra(4, 1, sourceBytes);
    auto outputMemory = SharedPublicationMemory::CreateForWork();

    RasterEditInput move = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(0, 0, 1, 1), 2, false);
    move.outputMemory = outputMemory;
    RasterEditInput moveDefault = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(0, 0, 1, 1), 2, false);
    RasterEditResult moved = RunRasterEdit(move);
    RasterEditResult movedDefault = RunRasterEdit(moveDefault);
    facts.moveMemoryOutputIsLiteral =
        moved.status == RasterEditStatus::Ready && moved.layer && moved.layer->asset &&
        PixelsEqual(moved.layer->asset->image, RasterFormat::Bgra8888, 4, 1, moveExpected);
    facts.moveDefaultsHaveSamePixels =
        moved.layer && moved.layer->asset && movedDefault.layer && movedDefault.layer->asset &&
        PixelsEqual(moved.layer->asset->image, RasterFormat::Bgra8888, 4, 1, moveExpected) &&
        PixelsEqual(movedDefault.layer->asset->image, RasterFormat::Bgra8888, 4, 1, moveExpected);

    RasterEditInput duplicate = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(0, 0, 1, 1), 2, true);
    duplicate.outputMemory = outputMemory;
    RasterEditInput duplicateDefault = PixelMoveInput(Image(source, 4, 1), 4, 1, Rect(0, 0, 1, 1), 2, true);
    RasterEditResult duplicated = RunRasterEdit(duplicate);
    RasterEditResult duplicatedDefault = RunRasterEdit(duplicateDefault);
    facts.duplicateMemoryOutputIsLiteral =
        duplicated.status == RasterEditStatus::Ready && duplicated.layer && duplicated.layer->asset &&
        PixelsEqual(duplicated.layer->asset->image, RasterFormat::Bgra8888, 4, 1, duplicateExpected);
    facts.duplicateDefaultsHaveSamePixels =
        duplicated.layer && duplicated.layer->asset && duplicatedDefault.layer && duplicatedDefault.layer->asset &&
        PixelsEqual(duplicated.layer->asset->image, RasterFormat::Bgra8888, 4, 1, duplicateExpected) &&
        PixelsEqual(duplicatedDefault.layer->asset->image, RasterFormat::Bgra8888, 4, 1, duplicateExpected);

    moved = {};
    movedDefault = {};
    duplicated = {};
    duplicatedDefault = {};
    move = {};
    moveDefault = {};
    duplicate = {};
    duplicateDefault = {};
    outputMemory.reset();
    source = {};
}
} // namespace

void Run(int& failures)
{
    LifetimeFacts root;
    std::thread rootWorker([&root] { RootRasterOwnsOutputMemory(root); });
    rootWorker.join();
    Expect(failures, root.ready && root.literalPixels, "root-raster-literal-move-pixels");
    Expect(failures, root.oldSourceIdentityPreserved && root.noSparseAssembly,
           "root-raster-preserves-source-identity-without-assembly");
    Expect(failures, root.memoryAliveWithSelectedStrongStorage, "root-raster-strong-storage-keeps-output-memory");
    Expect(failures, root.selectedWeakStorageExpiresAfterStrongRelease && root.memoryAliveWithSelectedWeakStorage,
           "root-raster-weak-control-keeps-output-memory-after-storage-expiry");
    Expect(failures, root.memoryExpiresAfterSelectedWeakStorageRelease && root.oldSourceDoesNotRetainOutputMemory,
           "root-raster-weak-control-release-expires-memory-while-source-remains");

    LifetimeFacts tile;
    std::thread tileWorker([&tile] { PaintedTileOwnsOutputMemory(tile); });
    tileWorker.join();
    Expect(failures, tile.ready && tile.literalPixels && tile.oldSourceIdentityPreserved && tile.noSparseAssembly,
           "painted-tile-literal-move-pixels-and-sparse-root");
    Expect(failures, tile.outputRootReleased && tile.memoryAliveWithSelectedStrongStorage,
           "painted-tile-keeps-memory-after-expired-root-control-is-released");
    Expect(failures, tile.selectedWeakStorageExpiresAfterStrongRelease && tile.memoryAliveWithSelectedWeakStorage,
           "painted-tile-weak-control-alone-keeps-output-memory");
    Expect(failures, tile.memoryExpiresAfterSelectedWeakStorageRelease,
           "painted-tile-weak-control-release-expires-output-memory");

    LifetimeFacts loops;
    std::thread loopsWorker([&loops] { TranslatedLoopsOwnOutputMemory(loops); });
    loopsWorker.join();
    Expect(failures, loops.ready && loops.literalPixels, "translated-loops-and-duplicate-pixels-are-literal");
    Expect(failures, loops.memoryAliveWithSelectedStrongStorage, "translated-loops-strong-storage-keeps-output-memory");
    Expect(failures, loops.selectedWeakStorageExpiresAfterStrongRelease && loops.memoryAliveWithSelectedWeakStorage,
           "translated-loops-weak-control-keeps-memory-after-storage-expiry");
    Expect(failures, loops.memoryExpiresAfterSelectedWeakStorageRelease,
           "translated-loops-weak-control-release-expires-memory");

    LifetimeFacts mask;
    std::thread maskWorker([&mask] { GrownCarriedMaskAndThumbnailOwnOutputMemory(mask, false); });
    maskWorker.join();
    Expect(failures, mask.ready && mask.literalPixels, "grown-carried-gray-mask-has-distinct-semantic-thumbnail");
    Expect(failures, mask.memoryAliveWithSelectedStrongStorage && mask.maskAloneRetainsOutputMemory,
           "grown-mask-storage-keeps-output-memory");
    Expect(failures, mask.selectedWeakStorageExpiresAfterStrongRelease && mask.memoryAliveWithSelectedWeakStorage,
           "grown-mask-weak-control-keeps-memory-after-storage-expiry");
    Expect(failures, mask.memoryExpiresAfterSelectedWeakStorageRelease && mask.oldSourceDoesNotRetainOutputMemory,
           "grown-carried-mask-alone-retains-output-memory");

    LifetimeFacts thumbnail;
    std::thread thumbnailWorker([&thumbnail] { GrownCarriedMaskAndThumbnailOwnOutputMemory(thumbnail, true); });
    thumbnailWorker.join();
    Expect(failures,
           thumbnail.ready && thumbnail.literalPixels && thumbnail.memoryAliveWithSelectedStrongStorage &&
               thumbnail.thumbnailAloneRetainsOutputMemory,
           "grown-semantic-thumbnail-storage-keeps-output-memory");
    Expect(failures,
           thumbnail.selectedWeakStorageExpiresAfterStrongRelease && thumbnail.memoryAliveWithSelectedWeakStorage,
           "grown-thumbnail-weak-control-keeps-memory-after-storage-expiry");
    Expect(failures,
           thumbnail.memoryExpiresAfterSelectedWeakStorageRelease && thumbnail.oldSourceDoesNotRetainOutputMemory,
           "grown-thumbnail-weak-control-release-expires-memory-while-source-remains");

    LifetimeFacts smallThumbnail;
    std::thread smallWorker([&] { SmallSparseThumbnailLifetime(smallThumbnail); });
    smallWorker.join();
    Expect(failures, smallThumbnail.literalPixels && smallThumbnail.noSparseAssembly,
           "small-semantic-thumbnail-privately-copies-literal-gray-without-source-assembly");
    Expect(failures,
           smallThumbnail.selectedWeakStorageExpiresAfterStrongRelease &&
               smallThumbnail.memoryAliveWithSelectedWeakStorage,
           "small-semantic-thumbnail-weak-control-retains-original-output-owner");
    Expect(failures, smallThumbnail.memoryExpiresAfterSelectedWeakStorageRelease,
           "small-semantic-thumbnail-final-weak-release-refunds-owner-with-source-alive");
    bool refused = false;
    std::thread refusalWorker([&] { WrongOperationMemoryRefused(refused); });
    refusalWorker.join();
    Expect(failures, refused, "unsupported-output-memory-kind-refuses-with-positive-default-control");

    PixelParityFacts parity;
    std::thread parityWorker([&parity] { MoveDuplicateAndDefaultParity(parity); });
    parityWorker.join();
    Expect(failures, parity.moveMemoryOutputIsLiteral && parity.moveDefaultsHaveSamePixels,
           "move-output-and-default-memory-input-have-literal-pixel-parity");
    Expect(failures, parity.duplicateMemoryOutputIsLiteral && parity.duplicateDefaultsHaveSamePixels,
           "duplicate-output-and-default-memory-input-have-literal-pixel-parity");
}
} // namespace PixelMoveOutputLifetimeTests

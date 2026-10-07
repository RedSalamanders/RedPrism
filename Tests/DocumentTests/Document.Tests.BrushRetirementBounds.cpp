#include "Document.Tests.BrushRetirementBounds.h"

#include "BrushStroke.h"
#include "Document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s brush-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectHealing(bool condition, const char* mode, const char* check) noexcept
{
    std::printf("%s brush-retirement.heal.%s.%s\n", condition ? "PASS" : "FAIL", mode, check);
    gFailures += condition ? 0 : 1;
}

[[nodiscard]] bool Add(std::uint64_t& total, std::uint64_t amount) noexcept
{
    if (amount > std::numeric_limits<std::uint64_t>::max() - total)
        return false;
    total += amount;
    return true;
}

[[nodiscard]] bool AddRaster(std::uint64_t& total, const Raster& raster) noexcept
{
    if (!raster.HasStorage())
        return true;
    const std::uint64_t bytes = raster.RetirementMemoryBytes();
    return bytes != 0 && Add(total, bytes);
}

// Independent public-state floor. It includes every exposed tile capacity and raster owner but deliberately omits
// private samples, hash storage, optional selection/sample/lift owners and allocator headroom. It is not a copy of
// the production formula; zero means the floor could not be established safely.
[[nodiscard]] std::uint64_t PublicStorageLowerBound(const BrushStroke& stroke) noexcept
{
    std::uint64_t total = sizeof(stroke);
    if (!AddRaster(total, stroke.SourceImage()))
        return 0;
    const std::vector<BrushStroke::Tile>& tiles = stroke.Tiles();
    if (tiles.capacity() > std::numeric_limits<std::uint64_t>::max() / sizeof(BrushStroke::Tile) ||
        !Add(total, static_cast<std::uint64_t>(tiles.capacity()) * sizeof(BrushStroke::Tile)))
        return 0;
    for (const BrushStroke::Tile& tile : tiles)
    {
        if (tile.permanent.capacity() > std::numeric_limits<std::uint64_t>::max() / sizeof(float) ||
            !Add(total, static_cast<std::uint64_t>(tile.permanent.capacity()) * sizeof(float)) ||
            !Add(total, static_cast<std::uint64_t>(tile.coverage.capacity())) || !AddRaster(total, tile.base) ||
            !AddRaster(total, tile.image))
            return 0;
    }
    return total;
}

[[nodiscard]] bool CoversPublicStorage(const BrushStroke& stroke) noexcept
{
    const std::uint64_t floor = PublicStorageLowerBound(stroke);
    const std::uint64_t accounted = stroke.RetirementMemoryBytes();
    return floor != 0 && accounted != 0 && accounted >= floor;
}

Raster SolidSource(int width = 32, int height = 32)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 30;
        pixels[i + 1] = 20;
        pixels[i + 2] = 10;
        pixels[i + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

ImageLayer SourceLayer(const Raster& image)
{
    ImageLayer layer = ImageLayer::Blank(L"retirement source",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, {}, layer.name};
    return layer;
}

bool PixelEquals(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected) noexcept
{
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool GrayPixelEquals(const Raster& raster, int x, int y, std::uint8_t expected) noexcept
{
    std::uint8_t actual = 0;
    raster.ReadRegion(x, y, 1, 1, &actual, 1, 0);
    return actual == expected;
}
bool Gray8RasterEquals(const Raster& raster, std::uint8_t expected) noexcept
{
    if (raster.Width() != 32 || raster.Height() != 32 || raster.Format() != RasterFormat::Gray8)
        return false;
    std::array<std::uint8_t, 32u * 32u> actual{};
    raster.ReadRegion(0, 0, 32, 32, actual.data(), 32, 0);
    for (const std::uint8_t value : actual)
        if (value != expected)
            return false;
    return true;
}

bool SolidSourceUnchanged(const Raster& raster) noexcept
{
    if (raster.Width() != 32 || raster.Height() != 32 || raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, 32u * 32u * 4u> pixels{};
    raster.ReadRegion(0, 0, 32, 32, pixels.data(), 32 * 4, 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] != 30 || pixels[i + 1] != 20 || pixels[i + 2] != 10 || pixels[i + 3] != 255)
            return false;
    return true;
}
bool Gray128PixelsEqual(const Raster& raster) noexcept
{
    if (raster.Width() != 32 || raster.Height() != 32 || raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, 32u * 32u * 4u> pixels{};
    raster.ReadRegion(0, 0, 32, 32, pixels.data(), 32 * 4, 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] != 128 || pixels[i + 1] != 128 || pixels[i + 2] != 128 || pixels[i + 3] != 255)
            return false;
    return true;
}

ImageLayer BlankLayer(const wchar_t* name, int width, int height)
{
    return ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
}

void TestEmptyAndSourceOnlyBounds()
{
    ImageLayer empty = BlankLayer(L"empty", 32, 32);
    BrushStroke noTiles(empty, false, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    Expect(noTiles.IsValid() && noTiles.Tiles().empty() && noTiles.RetirementMemoryBytes() > 0 &&
               CoversPublicStorage(noTiles),
           "empty.valid-no-tiles-bound-covers-public-floor");

    const Raster source = SolidSource();
    ImageLayer withSource = SourceLayer(source);
    BrushStroke sourceOnly(withSource, false, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    Expect(sourceOnly.SourceImage().HasStorage() && sourceOnly.Tiles().empty() &&
               sourceOnly.RetirementMemoryBytes() >= source.RetirementMemoryBytes() && CoversPublicStorage(sourceOnly),
           "source-only.bound-covers-retained-source-graph");
    Expect(PixelEquals(source, 0, 0, {30, 20, 10, 255}), "source-only.literal-premultiplied-bgra-preserved");
}

void TestInitialRepeatAndDistantTileBounds()
{
    ImageLayer blank = BlankLayer(L"tile", 512, 512);
    BrushStroke stroke(blank, false, BrushSettings{4, 1, 1, 0, 0, 1}, 512, 512, &CpuBrushCoverage::Shared());
    const bool firstAppend = stroke.Append(Vec2{32, 32});
    Expect(firstAppend && stroke.Tiles().size() == 1 && CoversPublicStorage(stroke),
           "initial.append-and-bound-covers-tile-capacities-and-raster-owners");
    if (stroke.Tiles().size() == 1)
    {
        const Raster firstImageOwner = stroke.Tiles().front().image;
        const void* const firstIdentity = firstImageOwner.Identity();
        Expect(!firstImageOwner.IsSparse() && firstImageOwner.IsMaterialized(),
               "initial.working-tile-is-contiguous-and-readable");
        const bool repeated = stroke.Append(Vec2{40, 32});
        Expect(repeated && stroke.Tiles().size() == 1 && CoversPublicStorage(stroke),
               "repeat.same-tile-recompose-bound-covers-replaced-owners");
        if (stroke.Tiles().size() == 1)
        {
            const Raster& current = stroke.Tiles().front().image;
            Expect(firstImageOwner.Identity() == firstIdentity && current.Identity() != firstIdentity,
                   "repeat.retained-raster-owner-keeps-old-identity-live");
            Expect(!current.IsSparse() && current.IsMaterialized(),
                   "repeat.working-tile-remains-contiguous-and-readable");
        }
    }

    ImageLayer wide = BlankLayer(L"distant", 1024, 64);
    BrushStroke distant(wide, false, BrushSettings{4, 1, 1, 0, 0, 1}, 1024, 64, &CpuBrushCoverage::Shared());
    const bool traced = distant.Append(Vec2{32, 32}) && distant.Append(Vec2{800, 32}) && distant.Flush();
    Expect(traced && distant.Tiles().size() >= 4 && CoversPublicStorage(distant),
           "distant.path-bound-traverses-every-tile-owner");
    bool contiguousTiles = !distant.Tiles().empty();
    for (const BrushStroke::Tile& tile : distant.Tiles())
        contiguousTiles = contiguousTiles && !tile.image.IsSparse() && tile.image.IsMaterialized();
    Expect(contiguousTiles, "distant.working-tiles-are-contiguous-and-readable");
}

struct WorkerRasterResult final
{
    bool configured = false;
    bool filled = false;
    std::optional<BrushStroke::Commit> snapshot;
};

void TestWorkerRasterFillAndGrayMask()
{
    const Raster source = SolidSource();
    ImageLayer layer = SourceLayer(source);
    BrushStroke fill(layer, false, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    WorkerRasterResult result;
    std::thread worker(
        [&]
        {
            result.configured = fill.ConfigureWorkerRasterEdit(CancelToken{});
            result.filled = result.configured && fill.Fill({1, 0, 0, 1});
            if (result.filled)
                result.snapshot = fill.PaintSnapshot();
        });
    worker.join();
    Expect(result.configured && result.filled && result.snapshot.has_value() && CoversPublicStorage(fill),
           "raster-edit.configure-fill-compose-and-bound-on-joined-worker");
    Expect(result.snapshot.has_value() && PixelEquals(result.snapshot.value().image, 16, 16, {0, 0, 255, 255}),
           "raster-edit.literal-premultiplied-red-bgra");
    Expect(SolidSourceUnchanged(source), "raster-edit.source-raster-remains-unchanged");

    std::array<std::uint8_t, 32u * 32u> gray{};
    gray.fill(128);
    const Raster maskSource = Raster::Make(32, 32, RasterFormat::Gray8, gray.data(), gray.size());
    ImageLayer masked = BlankLayer(L"gray mask", 32, 32);
    LayerMask layerMask;
    layerMask.asset.image = maskSource;
    masked.mask = layerMask;
    BrushStroke maskEdit(masked, true, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    WorkerRasterResult maskResult;
    std::thread maskWorker(
        [&]
        {
            maskResult.configured = maskEdit.ConfigureWorkerRasterEdit(CancelToken{});
            maskResult.filled = maskResult.configured && maskEdit.Fill({1, 0, 0, 1});
            if (maskResult.filled)
                maskResult.snapshot = maskEdit.PaintSnapshot();
        });
    maskWorker.join();
    Expect(maskResult.configured && maskResult.filled && maskResult.snapshot.has_value() &&
               CoversPublicStorage(maskEdit),
           "mask-edit.gray8-fill-bound-covers-tile-owners-on-joined-worker");
    Expect(maskResult.snapshot.has_value() && maskResult.snapshot.value().image.Format() == RasterFormat::Gray8 &&
               GrayPixelEquals(maskResult.snapshot.value().image, 16, 16, 255) && Gray8RasterEquals(maskSource, 128),
           "mask-edit.gray8-white-result-and-retained-source-value");
}

void TestDetachedFinalCoverageOnJoinedWorker()
{
    ImageLayer layer = BlankLayer(L"detached finish", 32, 32);
    BrushStroke stroke(layer, false, BrushSettings{64, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    const bool appended = stroke.Append(Vec2{16, 16}) && stroke.Append(Vec2{16.5, 16});
    BrushCoverageEngine* const ownerEngine = stroke.DetachCoverageForFinish();
    Expect(appended && ownerEngine == &CpuBrushCoverage::Shared() && !stroke.HasCoverageEngineForTest(),
           "finish.owner-appends-and-detaches-before-worker");

    bool prepared = false;
    bool composed = false;
    std::optional<BrushStroke::Commit> snapshot;
    std::thread worker(
        [&]
        {
            const auto final = stroke.PrepareFinalCoverage();
            prepared = final.has_value() && !final.value().needsGpu && !final.value().tiles.empty();
            if (!prepared)
                return;
            composed = stroke.ComposeFinalCoverage(final.value());
            if (composed)
                snapshot = stroke.PaintSnapshot();
        });
    worker.join();
    if (ownerEngine != nullptr)
        ownerEngine->EndStroke(); // Owner cleanup is ordered after the worker join.
    Expect(prepared && composed && snapshot.has_value() && CoversPublicStorage(stroke),
           "finish.prepare-compose-bound-and-snapshot-after-joined-worker");
    Expect(snapshot.has_value() && PixelEquals(snapshot.value().image, 16, 16, {0, 0, 255, 255}),
           "finish.final-literal-premultiplied-red-bgra");
    Expect(snapshot.has_value() && snapshot.value().image.IsSparse() && !snapshot.value().image.IsMaterialized(),
           "finish.final-sparse-remains-unmaterialized");
}

constexpr std::array<std::array<int, 2>, 6> kOutsidePoints{{
    {11, 16},
    {21, 16},
    {16, 11},
    {16, 21},
    {0, 0},
    {31, 31},
}};

std::vector<std::uint8_t> Gray128DefectBytes()
{
    std::vector<std::uint8_t> pixels(32u * 32u * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 128;
        pixels[i + 1] = 128;
        pixels[i + 2] = 128;
        pixels[i + 3] = 255;
    }
    const std::size_t center = (16u * 32u + 16u) * 4u;
    pixels[center] = 0;
    pixels[center + 1] = 0;
    pixels[center + 2] = 0;
    return pixels;
}

bool CoverageAt(const BrushStroke& stroke, int x, int y, std::uint8_t expected) noexcept
{
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        if (tile.rect.Intersect(GridRect{x, y, 1, 1}).Empty())
            continue;
        const std::size_t index =
            static_cast<std::size_t>(y - tile.rect.y) * static_cast<std::size_t>(tile.rect.width) +
            static_cast<std::size_t>(x - tile.rect.x);
        return index < tile.coverage.size() && tile.coverage[index] == expected;
    }
    return false;
}

bool Gray128DefectSourceUnchanged(const Raster& source, const std::vector<std::uint8_t>& expected) noexcept
{
    if (source.Width() != 32 || source.Height() != 32 || source.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, 32u * 32u * 4u> actual{};
    source.ReadRegion(0, 0, 32, 32, actual.data(), 32 * 4, 0);
    return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

void TestHealingProducerCoverage()
{
    const std::array<std::pair<SpotHealingMode, const char*>, 3> modes{{
        {SpotHealingMode::ContentAware, "content-aware"},
        {SpotHealingMode::CreateTexture, "create-texture"},
        {SpotHealingMode::ProximityMatch, "proximity-match"},
    }};
    for (const auto& [mode, name] : modes)
    {
        const std::vector<std::uint8_t> sourceBytes = Gray128DefectBytes();
        const Raster source = Raster::Make(32, 32, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
        ImageLayer layer = SourceLayer(source);
        BrushStroke stroke(layer, false, BrushSettings{6, 1, 0, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared(),
                           BrushKind::Heal, mode);
        const bool appended = stroke.Append(Vec2{16, 16}) && stroke.Append(Vec2{16.5, 16});
        BrushCoverageEngine* const ownerEngine = stroke.DetachCoverageForFinish();
        ExpectHealing(appended && ownerEngine == &CpuBrushCoverage::Shared(), name, "owner-detach-before-worker");
        bool composed = false;
        bool coverageExpected = false;
        bool healed = false;
        std::optional<BrushStroke::Commit> snapshot;
        std::thread worker(
            [&]
            {
                const auto final = stroke.PrepareFinalCoverage();
                if (!final.has_value() || final.value().needsGpu || final.value().tiles.empty())
                    return;
                composed = stroke.ComposeFinalCoverage(final.value());
                if (!composed)
                    return;
                coverageExpected = CoverageAt(stroke, 16, 16, 255);
                for (const auto& point : kOutsidePoints)
                    coverageExpected = coverageExpected && CoverageAt(stroke, point[0], point[1], 0);
                healed = stroke.HealForFinish(0x1234U);
                if (healed)
                    snapshot = stroke.PaintSnapshot();
            });
        worker.join();
        if (ownerEngine != nullptr)
            ownerEngine->EndStroke();
        ExpectHealing(composed && coverageExpected, name, "joined-worker-final-coverage-and-outside-controls");
        ExpectHealing(healed && snapshot.has_value() && Gray128PixelsEqual(snapshot.value().image), name,
                      "black-defect-heals-to-exact-gray128-reference");
        ExpectHealing(CoversPublicStorage(stroke), name, "healed-bound-covers-public-tile-owners");
        ExpectHealing(Gray128DefectSourceUnchanged(source, sourceBytes), name, "source-defect-remains-unchanged");
    }
}

void TestSparseSourceOwnershipAndUnmaterializedReads()
{
    const Raster pixels = SolidSource();
    const Raster source = Raster::MakeSparse(32, 32, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, pixels}}, 0);
    ImageLayer layer = SourceLayer(source);
    BrushStroke stroke(layer, false, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    Expect(source.HasStorage() && source.IsSparse() && CoversPublicStorage(stroke),
           "sparse-source.initial-bound-covers-complete-retained-graph");
    Expect(!source.IsMaterialized(), "sparse-source.starts-unmaterialized");
    const bool appended = stroke.Append({16, 16}) && stroke.Flush();
    Expect(appended && CoversPublicStorage(stroke), "sparse-source.compose-caches-new-tile-owners");
    Expect(SolidSourceUnchanged(source) && !source.IsMaterialized(),
           "sparse-source.retained-pixels-unchanged-without-assembly");
    const auto snapshot = stroke.PaintSnapshot();
    Expect(snapshot && snapshot->image.IsSparse() && !snapshot->image.IsMaterialized(),
           "sparse-source.committed-snapshot-is-unmaterialized");
    Expect(snapshot && PixelEquals(snapshot->image, 16, 16, {0, 0, 255, 255}) && !snapshot->image.IsMaterialized(),
           "sparse-source.literal-red-result-without-assembly");
}

void TestCompatibilityHealingProducer()
{
    const auto pixels = Gray128DefectBytes();
    const Raster source = Raster::Make(32, 32, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const ImageLayer layer = SourceLayer(source);
    BrushStroke stroke(layer, false, BrushSettings{6, 1, 0, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared(),
                       BrushKind::Heal, SpotHealingMode::ContentAware);
    const bool composed = stroke.Append({16, 16}) && stroke.Append({16.5, 16}) && stroke.Flush();
    Expect(composed && CoversPublicStorage(stroke), "compatibility-heal.coverage-owners-accounted");
    const bool healed = composed && stroke.Heal(0x1234U);
    Expect(healed && CoversPublicStorage(stroke), "compatibility-heal.replaced-image-owners-accounted");
    const auto snapshot = healed ? stroke.PaintSnapshot() : std::nullopt;
    Expect(snapshot && Gray128PixelsEqual(snapshot->image), "compatibility-heal.exact-gray128-reference");
    Expect(Gray128DefectSourceUnchanged(source, pixels), "compatibility-heal.source-defect-unchanged");
}

void TestMoveLiftedProducerOnJoinedWorker()
{
    const Raster source = SolidSource();
    const ImageLayer layer = SourceLayer(source);
    std::array<std::uint8_t, 32 * 32> selected;
    selected.fill(255);
    const Raster coverage = Raster::Make(32, 32, RasterFormat::Gray8, selected.data(), selected.size());
    BrushStroke stroke(layer, false, BrushSettings{4, 1, 1, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    bool moved = false;
    std::optional<BrushStroke::Commit> snapshot;
    std::thread worker(
        [&]
        {
            if (!stroke.ConfigureWorkerRasterEdit({}))
                return;
            stroke.SetSelectionClip(coverage);
            bool empty = false;
            moved = stroke.LiftSelection(&empty) && !empty && stroke.MoveLifted({1, 0}, false);
            if (moved)
                snapshot = stroke.PaintSnapshot();
        });
    worker.join();
    Expect(moved && snapshot && CoversPublicStorage(stroke), "move-lifted.joined-worker-accounts-replaced-owners");
    Expect(snapshot && PixelEquals(snapshot->image, 1, 16, {30, 20, 10, 255}),
           "move-lifted.shifted-literal-source-pixel");
    Expect(snapshot && PixelEquals(snapshot->image, 0, 16, {0, 0, 0, 0}),
           "move-lifted.literal-transparent-cleared-edge");
    Expect(SolidSourceUnchanged(source), "move-lifted.retained-source-unchanged");
}

} // namespace

int RunBrushRetirementBoundsTests()
{
    gFailures = 0;
    TestEmptyAndSourceOnlyBounds();
    TestInitialRepeatAndDistantTileBounds();
    TestWorkerRasterFillAndGrayMask();
    TestDetachedFinalCoverageOnJoinedWorker();
    TestHealingProducerCoverage();
    TestSparseSourceOwnershipAndUnmaterializedReads();
    TestCompatibilityHealingProducer();
    TestMoveLiftedProducerOnJoinedWorker();
    return gFailures;
}

#include "Document.Tests.WarpWorkerSnapshots.h"

#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

constexpr int kWidth = 300;
constexpr int kHeight = 300;
constexpr std::array<std::uint8_t, 4> kPremultipliedBgra{12, 24, 48, 96};

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s warp-worker-snapshots.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Solid(int width, int height, std::array<std::uint8_t, 4> pixel)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < bytes.size(); offset += pixel.size())
        std::copy(pixel.begin(), pixel.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster SparseSource(const Raster& pixels)
{
    return Raster::MakeSparse(kWidth, kHeight, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, pixels}}, 0);
}

Raster SparseSolidSource()
{
    const Raster pixels = Solid(kWidth, kHeight, kPremultipliedBgra);
    return SparseSource(pixels);
}

bool IsLiteralRaster(const Raster& image, int width, int height) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 || !image.Data())
        return false;
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    for (std::size_t offset = 0; offset < byteCount; offset += kPremultipliedBgra.size())
    {
        if (!std::equal(kPremultipliedBgra.begin(), kPremultipliedBgra.end(), image.Data() + offset))
            return false;
    }
    return true;
}

struct ExpectedTile final
{
    int x;
    int y;
    int width;
    int height;
};

constexpr std::array<ExpectedTile, 4> kCrossingTiles{
    {{0, 0, 256, 256}, {256, 0, 44, 256}, {0, 256, 256, 44}, {256, 256, 44, 44}}};

bool HasExpectedTiles(const std::vector<RasterTile>& tiles) noexcept
{
    if (tiles.size() != kCrossingTiles.size())
        return false;
    for (std::size_t i = 0; i < kCrossingTiles.size(); ++i)
    {
        const ExpectedTile& expected = kCrossingTiles[i];
        const RasterTile& actual = tiles[i];
        if (actual.x != expected.x || actual.y != expected.y ||
            !IsLiteralRaster(actual.pixels, expected.width, expected.height))
            return false;
    }
    return true;
}

struct StopAtStage final
{
    std::stop_source* source = nullptr;
    WarpStroke::WorkStage target{};
    std::size_t hits = 0;
    std::size_t stopAt = 1;
};

void RequestStopAtStage(void* opaque, WarpStroke::WorkStage stage) noexcept
{
    auto& observer = *static_cast<StopAtStage*>(opaque);
    if (stage == observer.target)
    {
        ++observer.hits;
        if (observer.hits == observer.stopAt)
            static_cast<void>(observer.source->request_stop());
    }
}

WarpStroke::WorkObserver Observer(StopAtStage& state) noexcept
{
    return WarpStroke::WorkObserver{RequestStopAtStage, &state};
}

WarpStroke NewStroke(const Raster& source)
{
    return WarpStroke(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1});
}

bool SeedBoundaryCrossing(WarpStroke& stroke)
{
    return stroke.AppendForWork({256, 256}, {}) && stroke.AppendForWork({257, 256}, {});
}

void SnapshotDimensionsAndLiteralImage()
{
    const Raster sourcePixels = Solid(kWidth, kHeight, kPremultipliedBgra);
    const Raster source = SparseSource(sourcePixels);
    const void* const sourceIdentity = source.Identity();
    WarpStroke stroke = NewStroke(source);
    Expect(source.HasStorage() && source.IsSparse() && !source.IsMaterialized() && stroke.IsValid(),
           "image.sparse-source-admitted-without-materialization");
    Expect(SeedBoundaryCrossing(stroke), "image.boundary-crossing-dabs-complete");

    const Raster image = stroke.ImageForWork({});
    Expect(IsLiteralRaster(image, 300, 300), "image.snapshot-has-literal-300x300-premultiplied-bytes");
    Expect(stroke.IsValid() && source.Identity() == sourceIdentity && source.IsSparse() && !source.IsMaterialized() &&
               IsLiteralRaster(sourcePixels, 300, 300),
           "image.published-copy-keeps-stroke-valid-and-literal-source-sparse");
}

void ChangedTileExtentsAndReset()
{
    const Raster source = SparseSolidSource();
    WarpStroke stroke = NewStroke(source);
    Expect(SeedBoundaryCrossing(stroke), "tiles.boundary-crossing-dabs-complete");

    const std::vector<RasterTile> first = stroke.TakeChangedTilesForWork({});
    Expect(HasExpectedTiles(first), "tiles.crossing-publishes-four-literal-grid-extents-and-bytes");
    Expect(stroke.IsValid(), "tiles.successful-publication-keeps-stroke-valid");

    const std::vector<RasterTile> empty = stroke.TakeChangedTilesForWork({});
    Expect(empty.empty() && stroke.IsValid(), "tiles.second-publication-is-empty-success");

    Expect(stroke.AppendForWork({258, 256}, {}), "tiles.later-append-completes");
    const std::vector<RasterTile> later = stroke.TakeChangedTilesForWork({});
    Expect(HasExpectedTiles(later), "tiles.later-append-publishes-new-literal-changed-tiles");
}

void CancelImageSnapshot(WarpStroke::WorkStage stage, std::size_t stopAt, const char* prefix)
{
    const Raster source = SparseSolidSource();
    WarpStroke stroke = NewStroke(source);
    char label[128]{};
    std::snprintf(label, sizeof(label), "%s.setup-dabs-complete", prefix);
    Expect(SeedBoundaryCrossing(stroke), label);
    const Raster retained = stroke.ImageForWork({});
    std::snprintf(label, sizeof(label), "%s.setup-retained-snapshot-is-literal", prefix);
    Expect(IsLiteralRaster(retained, 300, 300), label);

    std::stop_source stop;
    StopAtStage observer{&stop, stage, 0, stopAt};
    const Raster cancelled = stroke.ImageForWork(CancelToken({}, stop.get_token()), Observer(observer));
    std::snprintf(label, sizeof(label), "%s.stop-observed-before-publication", prefix);
    Expect(observer.hits == stopAt && stop.stop_requested(), label);
    std::snprintf(label, sizeof(label), "%s.partial-and-stale-image-are-unpublished", prefix);
    Expect(!cancelled.HasStorage() && !stroke.IsValid() && !stroke.Image().HasStorage(), label);
    std::snprintf(label, sizeof(label), "%s.earlier-published-raster-remains-literal", prefix);
    Expect(IsLiteralRaster(retained, 300, 300), label);
}

void CancelTileSnapshot(WarpStroke::WorkStage stage, std::size_t stopAt, const char* prefix)
{
    const Raster source = SparseSolidSource();
    WarpStroke stroke = NewStroke(source);
    const bool seeded = SeedBoundaryCrossing(stroke);
    const std::vector<RasterTile> retained = stroke.TakeChangedTilesForWork({});
    char label[128]{};
    std::snprintf(label, sizeof(label), "%s.setup-retains-four-literal-published-tiles", prefix);
    Expect(seeded && HasExpectedTiles(retained), label);

    const bool appended = stroke.AppendForWork({258, 256}, {});
    std::stop_source stop;
    StopAtStage observer{&stop, stage, 0, stopAt};
    const std::vector<RasterTile> cancelled =
        stroke.TakeChangedTilesForWork(CancelToken({}, stop.get_token()), Observer(observer));

    std::snprintf(label, sizeof(label), "%s.stop-observed-before-publication", prefix);
    Expect(appended && observer.hits == stopAt && stop.stop_requested(), label);
    std::snprintf(label, sizeof(label), "%s.partial-tiles-hidden-and-draft-invalidated", prefix);
    Expect(cancelled.empty() && !stroke.IsValid() && !stroke.Image().HasStorage(), label);
    std::snprintf(label, sizeof(label), "%s.earlier-published-tiles-remain-literal", prefix);
    Expect(HasExpectedTiles(retained), label);
}

void PreCancelledSnapshotRefusal()
{
    const Raster source = SparseSolidSource();
    char label[128]{};
    for (const bool shutdown : {false, true})
    {
        std::stop_source localStop;
        std::stop_source shutdownStop;
        std::stop_source& selected = shutdown ? shutdownStop : localStop;
        static_cast<void>(selected.request_stop());
        const CancelToken token =
            shutdown ? CancelToken({}, shutdownStop.get_token()) : CancelToken(localStop.get_token(), {});
        const char* kind = shutdown ? "shutdown" : "local";

        WarpStroke imageStroke = NewStroke(source);
        std::snprintf(label, sizeof(label), "precancel-image.%s.setup-dabs-complete", kind);
        Expect(SeedBoundaryCrossing(imageStroke), label);
        const Raster image = imageStroke.ImageForWork(token);
        std::snprintf(label, sizeof(label), "precancel-image.%s-token-refuses-and-invalidates", kind);
        Expect(!image.HasStorage() && !imageStroke.IsValid() && !imageStroke.Image().HasStorage(), label);

        WarpStroke tileStroke = NewStroke(source);
        std::snprintf(label, sizeof(label), "precancel-tiles.%s.setup-dabs-complete", kind);
        Expect(SeedBoundaryCrossing(tileStroke), label);
        const std::vector<RasterTile> tiles = tileStroke.TakeChangedTilesForWork(token);
        std::snprintf(label, sizeof(label), "precancel-tiles.%s-token-refuses-and-invalidates", kind);
        Expect(tiles.empty() && !tileStroke.IsValid() && !tileStroke.Image().HasStorage(), label);
    }
}
} // namespace

int RunWarpWorkerSnapshotsTests()
{
    gAssertions = 0;
    gFailures = 0;
    SnapshotDimensionsAndLiteralImage();
    ChangedTileExtentsAndReset();
    CancelImageSnapshot(WarpStroke::WorkStage::ImageRow, 17, "cancel-image.row");
    CancelImageSnapshot(WarpStroke::WorkStage::Complete, 1, "cancel-image.complete");
    CancelTileSnapshot(WarpStroke::WorkStage::TileRow, 17, "cancel-tiles.row");
    CancelTileSnapshot(WarpStroke::WorkStage::Complete, 1, "cancel-tiles.complete");
    PreCancelledSnapshotRefusal();
    std::printf("SUMMARY warp-worker-snapshots checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

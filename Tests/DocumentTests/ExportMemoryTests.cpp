#include "ExportMemoryTests.h"

#include "ExportWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s export-readback-memory.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

CanvasDocument EmptyDocument(int width = 1, int height = 1)
{
    CanvasDocument document;
    document.width = width;
    document.height = height;
    return document;
}

void CommonCanvasSizesAreAccepted()
{
    const CanvasDocument fourK = EmptyDocument(3840, 2160);
    const CanvasDocument photo = EmptyDocument(9000, 5000);
    const CanvasDocument maximumPixels = EmptyDocument(10'000, 10'000);
    const std::uint64_t fourKBytes = ExportReadbackMemoryBytes(fourK, 260);
    const std::uint64_t photoBytes = ExportReadbackMemoryBytes(photo, 260);
    const std::uint64_t maximumBytes = ExportReadbackMemoryBytes(maximumPixels, 260);

    Expect(fourKBytes != 0, "4k-canvas-accepted");
    Expect(photoBytes != 0, "45-megapixel-canvas-accepted");
    Expect(maximumBytes != 0, "100-megapixel-canvas-accepted");
    Expect(fourKBytes < photoBytes && photoBytes < maximumBytes, "larger-canvas-reserves-more-memory");
}

void RefusesInvalidDimensionsPathsAndOversizedCanvas()
{
    const CanvasDocument valid = EmptyDocument();
    Expect(ExportReadbackMemoryBytes(valid, 0) == 0, "zero-path-length-refused");
    Expect(ExportReadbackMemoryBytes(valid, 1) != 0, "short-path-accepted");
    Expect(ExportReadbackMemoryBytes(valid, 32728) != 0, "maximum-path-with-codec-suffix-room-accepted");
    Expect(ExportReadbackMemoryBytes(valid, 32729) == 0, "path-without-codec-suffix-room-refused");
    Expect(ExportReadbackMemoryBytes(EmptyDocument(0, 1), 1) == 0 &&
               ExportReadbackMemoryBytes(EmptyDocument(-1, 1), 1) == 0 &&
               ExportReadbackMemoryBytes(EmptyDocument(1, 0), 1) == 0 &&
               ExportReadbackMemoryBytes(EmptyDocument(1, -1), 1) == 0 &&
               ExportReadbackMemoryBytes(EmptyDocument(kMaxCanvasSide + 1, 1), 1) == 0,
           "invalid-canvas-dimensions-refused");
    Expect(ExportReadbackMemoryBytes(EmptyDocument(10'001, 10'000), 1) == 0, "canvas-over-100-million-pixels-refused");
    Expect(ExportReadbackMemoryBytes(EmptyDocument(kMaxCanvasSide, kMaxCanvasSide), 1) == 0,
           "maximum-side-canvas-over-pixel-limit-refused");
}

void LayerAndCurveLimitsAreEnforced()
{
    CanvasDocument tooManyLayers = EmptyDocument();
    tooManyLayers.layers.resize(static_cast<std::size_t>(kMaxLayerCount) + 1);
    Expect(ExportReadbackMemoryBytes(tooManyLayers, 1) == 0, "layer-count-over-10000-refused");

    CanvasDocument invalidCurve = EmptyDocument();
    ImageLayer layer = ImageLayer::Blank(L"Curve", Vec2{1, 1});
    layer.adjustment.emplace();
    layer.adjustment.value().curves.channels[0].resize(33);
    invalidCurve.layers.push_back(layer);
    Expect(ExportReadbackMemoryBytes(invalidCurve, 1) == 0, "curve-channel-over-32-points-refused");
}

void PathAndLayerMetadataIncreaseTheReservation()
{
    CanvasDocument document = EmptyDocument();
    const std::uint64_t shortPathBytes = ExportReadbackMemoryBytes(document, 1);
    const std::uint64_t longPathBytes = ExportReadbackMemoryBytes(document, 32728);
    Expect(longPathBytes > shortPathBytes, "longer-path-increases-reservation");

    document.layers.push_back(ImageLayer::Blank(L"Layer one", Vec2{1, 1}));
    const std::uint64_t oneLayerBytes = ExportReadbackMemoryBytes(document, 260);
    document.layers.push_back(ImageLayer::Blank(L"Layer two", Vec2{1, 1}));
    const std::uint64_t twoLayerBytes = ExportReadbackMemoryBytes(document, 260);
    Expect(oneLayerBytes > 0 && twoLayerBytes > oneLayerBytes, "additional-layer-metadata-increases-reservation");
}

void ReservationIncludesPixelBufferReadbackAllowance()
{
    const CanvasDocument document = EmptyDocument(3840, 2160);
    const std::uint64_t exportBytes = ExportReadbackMemoryBytes(document, 260);
    const std::uint64_t snapshotAllowance = ProjectSaveMemoryBytes(document, 260);
    const std::size_t readbackSize =
        static_cast<std::size_t>(document.width) * static_cast<std::size_t>(document.height) * 4;
    const std::uint64_t pixelBufferAllowance = PixelBuffer::RequiredBytes(readbackSize);
    Expect(exportBytes >= snapshotAllowance && exportBytes - snapshotAllowance >= pixelBufferAllowance,
           "reservation-covers-full-pixel-buffer-readback");
}

void SparseRasterIdentityIsPreserved()
{
    constexpr std::array<std::uint8_t, 4> pixel{12, 34, 56, 255};
    const Raster base = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    const void* const identity = sparse.Identity();
    CanvasDocument document = EmptyDocument(2, 2);
    ImageLayer layer = ImageLayer::Blank(L"Sparse", Vec2{1, 1});
    layer.asset = RasterAsset{sparse, {}, layer.name};
    document.layers.push_back(layer);

    const std::uint64_t reservation = ExportReadbackMemoryBytes(document, 260);
    Expect(reservation != 0 && sparse.IsSparse(), "sparse-source-reservation-succeeds");
    Expect(sparse.Identity() == identity && sparse.Identity() == document.layers[0].asset.value().image.Identity(),
           "sparse-source-identity-is-unchanged");
    Expect(!sparse.IsMaterialized(), "sparse-source-remains-unmaterialized");
}
} // namespace

int RunExportMemoryTests()
{
    gFailures = 0;
    CommonCanvasSizesAreAccepted();
    RefusesInvalidDimensionsPathsAndOversizedCanvas();
    LayerAndCurveLimitsAreEnforced();
    PathAndLayerMetadataIncreaseTheReservation();
    ReservationIncludesPixelBufferReadbackAllowance();
    SparseRasterIdentityIsPreserved();
    return gFailures;
}

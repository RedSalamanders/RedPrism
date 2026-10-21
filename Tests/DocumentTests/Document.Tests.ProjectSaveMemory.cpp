#include "Document.Tests.ProjectSaveMemory.h"

#include "ProjectSaveWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s project-save-memory.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

CanvasDocument EmptyDocument(int width = 1, int height = 1)
{
    CanvasDocument document;
    document.width = width;
    document.height = height;
    return document;
}

ImageLayer BlankLayer(const wchar_t* name = L"Layer")
{
    return ImageLayer::Blank(name, Vec2{1, 1});
}

void InvalidInputsAreRefused()
{
    const CanvasDocument valid = EmptyDocument();
    Expect(ProjectSaveMemoryBytes(valid, 0) == 0, "zero-path-length-refused");
    Expect(ProjectSaveMemoryBytes(valid, 32638) == 0, "path-without-staged-codec-suffix-room-refused");
    Expect(ProjectSaveMemoryBytes(valid, 1) != 0 && ProjectSaveMemoryBytes(valid, 32637) != 0,
           "bounded-path-lengths-accepted");
    Expect(ProjectSaveMemoryBytes(EmptyDocument(-1, 1), 1) == 0 &&
               ProjectSaveMemoryBytes(EmptyDocument(1, -1), 1) == 0 &&
               ProjectSaveMemoryBytes(EmptyDocument(0, 1), 1) == 0 &&
               ProjectSaveMemoryBytes(EmptyDocument(1, 0), 1) == 0 &&
               ProjectSaveMemoryBytes(EmptyDocument(kMaxCanvasSide + 1, 1), 1) == 0 &&
               ProjectSaveMemoryBytes(EmptyDocument(1, kMaxCanvasSide + 1), 1) == 0,
           "invalid-canvas-dimensions-refused");

    CanvasDocument tooManyLayers = EmptyDocument();
    tooManyLayers.layers.resize(static_cast<std::size_t>(kMaxLayerCount) + 1);
    Expect(ProjectSaveMemoryBytes(tooManyLayers, 1) == 0, "layer-count-over-limit-refused");
}

void PixelFramesContributeTheirLargestStorage()
{
    constexpr std::array<std::uint8_t, 4> literalBgra{17, 33, 65, 255};
    constexpr std::array<std::uint8_t, 1> literalGray{173};
    constexpr int kFrameSide = 64;
    const std::vector<std::uint8_t> largerBgra(static_cast<std::size_t>(kFrameSide * kFrameSide * 4), 91);
    const std::vector<std::uint8_t> largerGray(static_cast<std::size_t>(kFrameSide * kFrameSide), 137);

    const Raster onePixelBgra = Raster::Make(1, 1, RasterFormat::Bgra8888, literalBgra.data(), literalBgra.size());
    const Raster largeBgra =
        Raster::Make(kFrameSide, kFrameSide, RasterFormat::Bgra8888, largerBgra.data(), largerBgra.size());
    const Raster onePixelGray = Raster::Make(1, 1, RasterFormat::Gray8, literalGray.data(), literalGray.size());
    const Raster largeGray =
        Raster::Make(kFrameSide, kFrameSide, RasterFormat::Gray8, largerGray.data(), largerGray.size());

    CanvasDocument smallBgraDocument = EmptyDocument();
    ImageLayer smallBgraLayer = BlankLayer(L"BGRA");
    smallBgraLayer.asset = RasterAsset{onePixelBgra, {}, smallBgraLayer.name};
    smallBgraDocument.layers.push_back(smallBgraLayer);
    CanvasDocument largeBgraDocument = EmptyDocument();
    ImageLayer largeBgraLayer = BlankLayer(L"BGRA");
    largeBgraLayer.asset = RasterAsset{largeBgra, {}, largeBgraLayer.name};
    largeBgraDocument.layers.push_back(largeBgraLayer);

    CanvasDocument smallGrayDocument = EmptyDocument();
    ImageLayer smallGrayLayer = BlankLayer(L"Gray");
    smallGrayLayer.asset = RasterAsset{onePixelGray, {}, smallGrayLayer.name};
    smallGrayDocument.layers.push_back(smallGrayLayer);
    CanvasDocument largeGrayDocument = EmptyDocument();
    ImageLayer largeGrayLayer = BlankLayer(L"Gray");
    largeGrayLayer.asset = RasterAsset{largeGray, {}, largeGrayLayer.name};
    largeGrayDocument.layers.push_back(largeGrayLayer);

    const std::uint64_t smallBgraBytes = ProjectSaveMemoryBytes(smallBgraDocument, 260);
    const std::uint64_t largeBgraBytes = ProjectSaveMemoryBytes(largeBgraDocument, 260);
    const std::uint64_t smallGrayBytes = ProjectSaveMemoryBytes(smallGrayDocument, 260);
    const std::uint64_t largeGrayBytes = ProjectSaveMemoryBytes(largeGrayDocument, 260);
    const std::uint64_t bgraFrameBytes = static_cast<std::uint64_t>(largeBgra.Stride()) * largeBgra.Height();
    const std::uint64_t grayFrameBytes = static_cast<std::uint64_t>(largeGray.Stride()) * largeGray.Height();

    Expect(onePixelBgra.HasStorage() && onePixelGray.HasStorage() && largeBgra.HasStorage() && largeGray.HasStorage(),
           "literal-and-larger-raster-fixtures-created");
    Expect(largeBgraBytes >= smallBgraBytes &&
               largeBgraBytes - smallBgraBytes >= bgraFrameBytes - onePixelBgra.ByteSize(),
           "larger-bgra-frame-adds-its-pixel-storage");
    Expect(largeGrayBytes >= smallGrayBytes &&
               largeGrayBytes - smallGrayBytes >= grayFrameBytes - onePixelGray.ByteSize(),
           "larger-gray-frame-adds-its-pixel-storage");
}

void SharedRastersAddBoundedSourceMetadata()
{
    constexpr int kFrameSide = 256;
    const std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kFrameSide * kFrameSide * 4), 44);
    const Raster shared = Raster::Make(kFrameSide, kFrameSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    CanvasDocument oneAsset = EmptyDocument();
    ImageLayer first = BlankLayer(L"Shared one");
    first.asset = RasterAsset{shared, {}, first.name};
    oneAsset.layers.push_back(first);
    CanvasDocument twoAssets = oneAsset;
    ImageLayer second = BlankLayer(L"Shared two");
    second.asset = RasterAsset{shared, {}, second.name};
    twoAssets.layers.push_back(second);
    CanvasDocument threeAssets = twoAssets;
    ImageLayer third = BlankLayer(L"Shared three");
    third.asset = RasterAsset{shared, {}, third.name};
    threeAssets.layers.push_back(third);

    const std::uint64_t oneBytes = ProjectSaveMemoryBytes(oneAsset, 260);
    const std::uint64_t twoBytes = ProjectSaveMemoryBytes(twoAssets, 260);
    const std::uint64_t threeBytes = ProjectSaveMemoryBytes(threeAssets, 260);
    const std::uint64_t frameBytes = static_cast<std::uint64_t>(shared.Stride()) * shared.Height();
    Expect(shared.HasStorage() && first.asset.value().image.Identity() == second.asset.value().image.Identity() &&
               second.asset.value().image.Identity() == third.asset.value().image.Identity(),
           "assets-share-one-raster-identity");
    Expect(twoBytes > oneBytes && threeBytes > twoBytes, "each-shared-asset-adds-path-and-metadata-reservation");
    Expect(twoBytes - oneBytes < frameBytes && threeBytes - twoBytes < frameBytes,
           "additional-shared-assets-do-not-reserve-an-encoded-frame-each");
}

void NamesAndPathsAffectReservation()
{
    constexpr std::array<std::uint8_t, 4> pixels{2, 4, 8, 255};
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    CanvasDocument document = EmptyDocument();
    ImageLayer layer = BlankLayer(L"A");
    layer.asset = RasterAsset{image, {}, L"A"};
    document.layers.push_back(layer);

    const std::uint64_t shortPath = ProjectSaveMemoryBytes(document, 1);
    const std::uint64_t longPath = ProjectSaveMemoryBytes(document, 32637);
    Expect(longPath > shortPath, "bounded-longer-path-increases-reservation");

    document.layers[0].name = std::wstring(256, L'L');
    const std::uint64_t longerLayerName = ProjectSaveMemoryBytes(document, 1);
    Expect(longerLayerName > shortPath, "longer-layer-name-increases-reservation");
    document.layers[0].asset.value().name = std::wstring(256, L'A');
    const std::uint64_t longerAssetName = ProjectSaveMemoryBytes(document, 1);
    Expect(longerAssetName > longerLayerName, "longer-asset-name-increases-reservation");
}

void CurvesAndCurrentLimits()
{
    CanvasDocument tooManyCurvePoints = EmptyDocument();
    ImageLayer curveLayer = BlankLayer(L"Curves");
    curveLayer.adjustment.emplace();
    curveLayer.adjustment.value().curves.channels[0].resize(33);
    tooManyCurvePoints.layers.push_back(curveLayer);
    Expect(ProjectSaveMemoryBytes(tooManyCurvePoints, 1) == 0, "curve-channel-over-32-points-refused");

    constexpr std::array<std::uint8_t, 4> tinyPixel{90, 80, 70, 255};
    const Raster tinyRaster = Raster::Make(1, 1, RasterFormat::Bgra8888, tinyPixel.data(), tinyPixel.size());
    CanvasDocument maximumCanvas = EmptyDocument(kMaxCanvasSide, kMaxCanvasSide);
    ImageLayer tinyLayer = BlankLayer(L"Tiny raster at maximum canvas bounds");
    tinyLayer.asset = RasterAsset{tinyRaster, {}, tinyLayer.name};
    maximumCanvas.layers.push_back(tinyLayer);
    Expect(ProjectSaveMemoryBytes(maximumCanvas, 1) != 0, "maximum-canvas-sides-accepted-with-tiny-raster");

    CanvasDocument maximumLayers = EmptyDocument();
    maximumLayers.layers.resize(static_cast<std::size_t>(kMaxLayerCount));
    Expect(ProjectSaveMemoryBytes(maximumLayers, 1) != 0, "maximum-layer-count-accepted");
}

void SparseSourcesStaySharedAndUnmaterialized()
{
    constexpr std::array<std::uint8_t, 4> basePixels{10, 20, 30, 255};
    const Raster base = Raster::Make(1, 1, RasterFormat::Bgra8888, basePixels.data(), basePixels.size());
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    const void* const sourceIdentity = sparse.Identity();
    CanvasDocument document = EmptyDocument(2, 2);
    ImageLayer layer = BlankLayer(L"Sparse");
    layer.asset = RasterAsset{sparse, {}, layer.name};
    document.layers.push_back(layer);

    const std::uint64_t reservation = ProjectSaveMemoryBytes(document, 260);
    Expect(reservation != 0 && sparse.IsSparse(), "sparse-source-reservation-succeeds");
    Expect(sparse.Identity() == sourceIdentity &&
               sparse.Identity() == document.layers[0].asset.value().image.Identity(),
           "sparse-source-identity-is-preserved");
    Expect(!sparse.IsMaterialized(), "sparse-source-remains-unmaterialized");
}
} // namespace

int RunProjectSaveMemoryTests()
{
    gFailures = 0;
    InvalidInputsAreRefused();
    PixelFramesContributeTheirLargestStorage();
    SharedRastersAddBoundedSourceMetadata();
    NamesAndPathsAffectReservation();
    CurvesAndCurrentLimits();
    SparseSourcesStaySharedAndUnmaterialized();
    return gFailures;
}

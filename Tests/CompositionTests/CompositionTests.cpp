#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "Document.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d3d11_1.h>
#include <initializer_list>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Goldens are the legacy Swift characterization tests (LayerMaskTests, LiveMaskTests, LayerAppearanceTests,
// TransformTests, CompositionTests) evaluated on WARP. Tolerances are named in Rendering_Composition.md.

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
    ++gFailures;
}

[[nodiscard]] Raster Solid(int width, int height, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = b;
        pixels[i + 1] = g;
        pixels[i + 2] = r;
        pixels[i + 3] = a;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

// Premultiplied BGRA from per-pixel alphas and one straight color (legacy LiveMaskTests.asset).
[[nodiscard]] Raster AlphaRaster(std::initializer_list<std::uint8_t> alphas, std::uint8_t straight)
{
    std::vector<std::uint8_t> pixels;
    for (const std::uint8_t a : alphas)
    {
        const std::uint8_t premultiplied = static_cast<std::uint8_t>((static_cast<int>(straight) * a) / 255);
        pixels.push_back(premultiplied);
        pixels.push_back(premultiplied);
        pixels.push_back(premultiplied);
        pixels.push_back(a);
    }
    return Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] Raster Gray(int width, int height, std::initializer_list<std::uint8_t> values)
{
    std::vector<std::uint8_t> pixels(values);
    pixels.resize(static_cast<std::size_t>(width * height), 0);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

[[nodiscard]] ImageLayer LayerFrom(const Raster& raster, LayerBlendMode mode, const wchar_t* name)
{
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(raster.Width()), static_cast<double>(raster.Height())});
    layer.asset = RasterAsset{raster, raster, name};
    layer.blendMode = mode;
    layer.transform.sampling = LayerSampling::Nearest;
    return layer;
}

[[nodiscard]] LayerMask MaskOf(const Raster& gray)
{
    return LayerMask{RasterAsset{gray, gray, L"Layer Mask"}, true, std::nullopt, true};
}

[[nodiscard]] bool ChannelClose(int actual, int expected, int slop) noexcept
{
    return std::abs(actual - expected) <= slop;
}

[[nodiscard]] bool AlphasClose(const std::vector<std::uint8_t>& pixels, std::initializer_list<int> expected, int slop)
{
    if (pixels.size() != expected.size() * 4)
    {
        return false;
    }
    std::size_t index = 0;
    for (const int value : expected)
    {
        if (!ChannelClose(pixels[index * 4 + 3], value, slop))
        {
            return false;
        }
        ++index;
    }
    return true;
}

void PrintPixels(const char* label, const std::vector<std::uint8_t>& pixels)
{
    std::printf("     %s:", label);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        std::printf(" (%d,%d,%d,%d)", pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]);
    }
    std::printf("\n");
}

HRESULT MakeWarp(wil::com_ptr_nothrow<ID3D11Device>& device,
                 wil::com_ptr_nothrow<ID3D11DeviceContext>& context) noexcept
{
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL used = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, device.put(), &used, context.put());
}

struct Harness
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    CanvasRenderer composition;

    [[nodiscard]] bool Ready() noexcept
    {
        if (FAILED(MakeWarp(device, context)))
        {
            Expect(false, "compose.warp-device");
            return false;
        }
        if (FAILED(composition.CreateDeviceResources(device.get(), context.get())))
        {
            Expect(false, "compose.pipeline");
            return false;
        }
        return true;
    }

    // Composites `document` after marking it changed and returns premultiplied BGRA rows.
    [[nodiscard]] std::vector<std::uint8_t> Render(const CanvasDocument& document)
    {
        composition.InvalidateDocument();
        std::vector<std::uint8_t> pixels;
        UINT width = 0;
        UINT height = 0;
        if (FAILED(composition.Compose(document)) || FAILED(composition.Readback(pixels, width, height)))
        {
            return {};
        }
        return pixels;
    }
};

[[nodiscard]] CanvasDocument Document2x2()
{
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 2;
    return document;
}

void TestViewport()
{
    CanvasViewport viewport;
    viewport.viewSize = Vec2{200, 200};
    viewport.backingScale = 1;
    viewport.Fit(Vec2{100, 100});
    Expect(viewport.followsFit, "viewport.fit-flag");
    const Vec2 middle = viewport.DocumentFromView(Vec2{100, 100}, Vec2{100, 100});
    Expect(std::abs(middle.x - 50) < 1 && std::abs(middle.y - 50) < 1, "viewport.center-map");
    viewport.SetZoom(8, Vec2{100, 100}, Vec2{100, 100});
    Expect(viewport.ShowsPixelGrid(), "viewport.pixel-grid");

    // Legacy CompositionTests: round trip at several zooms and backing scales, cursor pixel fixed under zoom.
    const Vec2 documentSize{1920, 1080};
    for (const double backing : {1.0, 2.0})
    {
        CanvasViewport v;
        v.Resize(Vec2{800, 600}, backing, documentSize);
        for (const double zoom : {0.25, 1.0, 3.75})
        {
            v.SetZoom(zoom, v.Center(), documentSize);
            v.Translate(Vec2{73.5, -44.25});
            const Vec2 pixel{183.25, 837.5};
            const Vec2 view = v.ViewFromDocument(pixel, documentSize);
            const Vec2 back = v.DocumentFromView(view, documentSize);
            Expect(std::abs(back.x - pixel.x) < 1e-6 && std::abs(back.y - pixel.y) < 1e-6, "viewport.round-trip");
            Expect(std::abs(documentSize.x * v.PointsPerPixel() * backing - documentSize.x * zoom) < 1e-6,
                   "viewport.actual-pixels");
        }
    }
    CanvasViewport anchored;
    anchored.Resize(Vec2{1000, 700}, 2, documentSize);
    const Vec2 anchor{157, 221};
    const Vec2 before = anchored.DocumentFromView(anchor, documentSize);
    anchored.SetZoom(4, anchor, documentSize);
    const Vec2 after = anchored.DocumentFromView(anchor, documentSize);
    Expect(std::abs(before.x - after.x) < 1e-6 && std::abs(before.y - after.y) < 1e-6, "viewport.zoom-keeps-cursor");

    CanvasViewport fit;
    fit.Resize(Vec2{800, 600}, 2, documentSize);
    const Vec2 origin = fit.DocumentRectOrigin(documentSize);
    const double width = documentSize.x * fit.PointsPerPixel();
    const double height = documentSize.y * fit.PointsPerPixel();
    Expect(width <= 704.000001 && height <= 504.000001, "viewport.fit-margin");
    Expect(std::abs(origin.x + width / 2 - 400) < 1e-6 && std::abs(origin.y + height / 2 - 300) < 1e-6,
           "viewport.fit-centered");
    fit.Translate(Vec2{60, -35});
    const Vec2 centerBefore = fit.DocumentFromView(fit.Center(), documentSize);
    const double zoomBefore = fit.zoom;
    fit.Resize(Vec2{1200, 800}, 1, documentSize);
    const Vec2 centerAfter = fit.DocumentFromView(fit.Center(), documentSize);
    Expect(fit.zoom == zoomBefore && std::abs(centerBefore.x - centerAfter.x) < 1e-6 &&
               std::abs(centerBefore.y - centerAfter.y) < 1e-6,
           "viewport.resize-keeps-center");
    fit.Fit(documentSize);
    Expect(fit.pan.x == 0 && fit.pan.y == 0 && fit.followsFit, "viewport.fit-resets-pan");
    CanvasViewport limits;
    limits.Resize(Vec2{800, 600}, 2, documentSize);
    limits.SetZoom(1000, limits.Center(), documentSize);
    Expect(limits.zoom == CanvasViewport::kMaxZoom, "viewport.max-zoom");
    limits.SetZoom(0, limits.Center(), documentSize);
    Expect(limits.zoom == CanvasViewport::kMinZoom, "viewport.min-zoom");
}

void TestBlendModes(Harness& h)
{
    // Legacy LayerAppearanceTests.blendModesAndOpacityMatchKnownPixels: 0.4 gray under 0.8 gray.
    const std::uint8_t base = 102;
    const std::uint8_t top = 204;
    const std::array<std::pair<LayerBlendMode, double>, 9> expected{{
        {LayerBlendMode::Normal, 0.8},
        {LayerBlendMode::Multiply, 0.32},
        {LayerBlendMode::Screen, 0.88},
        {LayerBlendMode::Overlay, 0.64},
        {LayerBlendMode::Darken, 0.4},
        {LayerBlendMode::Lighten, 0.8},
        {LayerBlendMode::Difference, 0.4},
        {LayerBlendMode::ColorDodge, 1.0},
        {LayerBlendMode::ColorBurn, 0.25},
    }};
    for (const auto& [mode, value] : expected)
    {
        CanvasDocument document = Document2x2();
        document.layers.push_back(LayerFrom(Solid(2, 2, base, base, base, 255), LayerBlendMode::Normal, L"Base"));
        document.layers.push_back(LayerFrom(Solid(2, 2, top, top, top, 255), mode, L"Top"));
        const std::vector<std::uint8_t> pixels = h.Render(document);
        const bool ok =
            pixels.size() == 16 && ChannelClose(pixels[0], static_cast<int>(value * 255 + 0.5), 5) && pixels[3] == 255;
        if (!ok)
        {
            PrintPixels(LayerBlendModeName(mode), pixels);
        }
        Expect(ok, "blend.golden");
    }
    CanvasDocument half = Document2x2();
    half.layers.push_back(LayerFrom(Solid(2, 2, base, base, base, 255), LayerBlendMode::Normal, L"Base"));
    half.layers.push_back(LayerFrom(Solid(2, 2, top, top, top, 255), LayerBlendMode::Normal, L"Top"));
    half.layers[1].opacity = 0.5;
    std::vector<std::uint8_t> pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 153, 5), "blend.opacity-half");
    half.layers[1].opacity = 0;
    pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 102, 5), "blend.opacity-zero");
    half.layers[1].visible = false;
    half.layers[1].opacity = 1;
    pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 102, 5), "blend.hidden-layer");

    // Legacy: Multiply of red over white is red; Color Dodge of red over mid gray saturates red only.
    CanvasDocument whiteOver = Document2x2();
    whiteOver.layers.push_back(LayerFrom(Solid(2, 2, 255, 255, 255, 255), LayerBlendMode::Normal, L"White"));
    whiteOver.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Multiply, L"Red"));
    pixels = h.Render(whiteOver);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 0, 2) && ChannelClose(pixels[1], 0, 2) &&
               ChannelClose(pixels[2], 255, 2) && pixels[3] == 255,
           "compose.multiply-golden");
    CanvasDocument dodge = Document2x2();
    dodge.layers.push_back(LayerFrom(Solid(2, 2, 128, 128, 128, 255), LayerBlendMode::Normal, L"Gray"));
    dodge.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::ColorDodge, L"Dodge"));
    pixels = h.Render(dodge);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 128, 3) && ChannelClose(pixels[1], 128, 3) &&
               ChannelClose(pixels[2], 255, 3) && pixels[3] == 255,
           "compose.dodge-golden");
}

void TestOrderAndBlankLayers(Harness& h)
{
    // Legacy LayerTests.compositingHonorsVisibilityOrderAndBlankLayers.
    CanvasDocument document = Document2x2();
    document.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red"));
    document.layers.push_back(LayerFrom(Solid(2, 2, 255, 0, 0, 255), LayerBlendMode::Normal, L"Blue"));
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[0] > 240 && pixels[2] < 10, "order.top-wins");
    document.layers[1].visible = false;
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] > 240 && pixels[0] < 10, "order.hidden-top");
    document.layers[1].visible = true;
    std::swap(document.layers[0], document.layers[1]);
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] > 240, "order.reordered");
    document.layers.push_back(ImageLayer::Blank(L"Blank", Vec2{2, 2}));
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] > 240, "order.blank-layer-transparent");
    CanvasDocument empty = Document2x2();
    pixels = h.Render(empty);
    Expect(pixels.size() == 16 && pixels[3] == 0, "order.empty-document-transparent");
}

void TestRasterMasks(Harness& h)
{
    // Legacy LayerMaskTests.coverageOpacityAndDisabledMasksRenderCorrectly.
    CanvasDocument document = Document2x2();
    document.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red"));
    ImageLayer& red = document.layers[0];
    red.mask = MaskOf(Gray(1, 1, {255}));
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "mask.reveal-all");
    red.mask = MaskOf(Gray(1, 1, {0}));
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "mask.hide-all");
    red.mask.value().enabled = false;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "mask.disabled");
    red.mask = MaskOf(Gray(2, 2, {255, 0, 128, 255}));
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "mask.coverage");
    red.opacity = 0.5;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {128, 0, 64, 128}, 1), "mask.coverage-times-opacity");
    red.opacity = 1;
    // Legacy transformedMaskResizeAndCanvasChangesStayAligned: flipX mirrors mask and pixels together.
    red.transform.flipX = true;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 255, 255, 128}, 1), "mask.flip-x");
    red.transform.flipX = false;
    // Rotation keeps the mask on the layer's local grid; total coverage is unchanged.
    red.transform.rotation = 90;
    pixels = h.Render(document);
    int total = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
    {
        total += pixels[i];
    }
    Expect(pixels.size() == 16 && ChannelClose(total, 255 + 0 + 128 + 255, 4), "mask.rotation-keeps-total");
    red.transform.rotation = 0;
    // An unlinked mask placed one pixel right: outside its pixels it shows its edge tone (white here).
    red.mask.value().linked = false;
    LayerTransform placement = red.transform;
    placement.origin.x += 1;
    red.mask.value().placement = placement;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 128}, 1), "mask.placement-shift");
    // A hide-all mask moved apart keeps hiding beyond its pixels (legacy aMovedHideAllMaskKeepsHidingPastItsPixels).
    red.mask = MaskOf(Gray(2, 2, {0, 0, 0, 0}));
    red.mask.value().placement = placement;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "mask.placement-hide-all-background");
}

void TestLiveMasks(Harness& h)
{
    // Legacy LiveMaskTests.fixture: white target clipped to a hidden black source with soft alpha.
    CanvasDocument document = Document2x2();
    document.layers.push_back(LayerFrom(AlphaRaster({255, 255, 255, 255}, 255), LayerBlendMode::Normal, L"Target"));
    document.layers.push_back(LayerFrom(AlphaRaster({255, 0, 128, 255}, 0), LayerBlendMode::Normal, L"Source"));
    document.layers[1].visible = false;
    document.layers[0].maskSourceID = document.layers[1].id;
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "live.hidden-source-supplies-alpha");
    Expect(pixels.size() == 16 && pixels[0] == 255 && pixels[2] == 255, "live.color-comes-from-target");
    document.layers[1].opacity = 0.5;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {128, 0, 64, 128}, 1), "live.source-opacity-scales-coverage");
    document.layers[1].opacity = 1;
    document.layers[0].mask = MaskOf(Gray(2, 2, {128, 128, 128, 128}));
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[3] < 200 && pixels[3] > 100, "live.raster-mask-multiplies");
    document.layers[0].mask.reset();
    // Moving the source moves the coverage (legacy movingSourceChangesCoverageAndChainsMultiply).
    document.layers[1].transform.origin.x += 1;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 255, 0, 128}, 1), "live.moved-source");
    document.layers[1].transform.origin.x -= 1;
    // Chains multiply: source clipped to a second hidden source.
    document.layers.push_back(LayerFrom(AlphaRaster({0, 255, 255, 255}, 255), LayerBlendMode::Normal, L"Upstream"));
    document.layers[2].visible = false;
    document.layers[1].maskSourceID = document.layers[2].id;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 128, 255}, 1), "live.chain-multiplies");
    // A blank (pixel-less) source hides the target entirely.
    CanvasDocument blankSource = Document2x2();
    blankSource.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Target"));
    blankSource.layers.push_back(ImageLayer::Blank(L"Blank", Vec2{2, 2}));
    blankSource.layers[0].maskSourceID = blankSource.layers[1].id;
    pixels = h.Render(blankSource);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "live.blank-source-hides");
}

void TestClippingStack(Harness& h)
{
    // Legacy LiveMaskTests.clippingColorPreservesSoftBaseAlphaWithoutBlackFringe: a white layer clipped to a soft
    // black base keeps the base alpha and takes the white color at full coverage, with no dark fringe.
    CanvasDocument document = Document2x2();
    document.layers.push_back(LayerFrom(AlphaRaster({255, 128, 32, 0}, 0), LayerBlendMode::Normal, L"Base"));
    document.layers.push_back(LayerFrom(AlphaRaster({255, 255, 255, 255}, 255), LayerBlendMode::Normal, L"White"));
    document.layers[1].maskSourceID = document.layers[0].id;
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 128, 32, 0}, 1), "stack.keeps-base-alpha");
    bool whiteAtAlpha = pixels.size() == 16;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        whiteAtAlpha = whiteAtAlpha && ChannelClose(pixels[i], pixels[i + 3], 1) &&
                       ChannelClose(pixels[i + 1], pixels[i + 3], 1) && ChannelClose(pixels[i + 2], pixels[i + 3], 1);
    }
    if (!whiteAtAlpha)
    {
        PrintPixels("stack", pixels);
    }
    Expect(whiteAtAlpha, "stack.white-fills-base-coverage-without-fringe");
    document.layers[1].opacity = 0.5;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 128, 32, 0}, 1), "stack.child-opacity-keeps-alpha");
    document.layers[1].opacity = 1;
    // A white background under the stack: the result is fully white and opaque.
    document.layers.insert(document.layers.begin(),
                           LayerFrom(Solid(2, 2, 255, 255, 255, 255), LayerBlendMode::Normal, L"Background"));
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "stack.background-opaque");
    bool allWhite = pixels.size() == 16;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        allWhite = allWhite && pixels[i + 2] >= 254 && pixels[i + 1] >= 254 && pixels[i] >= 254;
    }
    if (!allWhite)
    {
        PrintPixels("stack+bg", pixels);
    }
    Expect(allWhite, "stack.background-white");
    // The base blend mode applies to the whole stack (legacy stackModes): Multiply of the stack over the background.
    document.layers[1].blendMode = LayerBlendMode::Multiply;
    document.layers[2] = LayerFrom(AlphaRaster({255, 255, 255, 255}, 128), LayerBlendMode::Normal, L"Gray");
    document.layers[2].maskSourceID = document.layers[1].id;
    document.layers[0] = LayerFrom(Solid(2, 2, 200, 200, 200, 255), LayerBlendMode::Normal, L"Background");
    pixels = h.Render(document);
    // Base alpha [255,128,32,0] of a 128-gray stack multiplied over 200 gray: at full alpha 200*128/255 ≈ 100.
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 100, 3) && ChannelClose(pixels[12], 200, 1),
           "stack.base-blend-mode-applies-to-stack");
}

void TestFolderMasks(Harness& h)
{
    // Legacy LayerMaskTests.folderMasksClipEveryLayerInsideAndMultiplyWithTheirOwnMasks.
    CanvasDocument document = Document2x2();
    ImageLayer folder = ImageLayer::Blank(L"Folder", Vec2{2, 2});
    folder.isGroup = true;
    ImageLayer red = LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red");
    red.parentID = folder.id;
    document.layers.push_back(folder);
    document.layers.push_back(red);
    ImageLayer& group = document.layers[0];
    ImageLayer& child = document.layers[1];
    group.mask = MaskOf(Gray(1, 1, {0}));
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "folder.hide-all");
    group.mask.value().enabled = false;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "folder.disabled");
    group.mask = MaskOf(Gray(2, 2, {255, 0, 128, 255}));
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "folder.coverage");
    child.mask = MaskOf(Gray(2, 2, {255, 0, 128, 255}));
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 64, 255}, 1), "folder.multiplies-layer-mask");
    child.mask.reset();
    // An enclosing folder's mask applies as well.
    ImageLayer outer = ImageLayer::Blank(L"Outer", Vec2{2, 2});
    outer.isGroup = true;
    outer.mask = MaskOf(Gray(1, 1, {0}));
    document.layers.insert(document.layers.begin(), outer);
    document.layers[1].parentID = document.layers[0].id;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "folder.nested-hide-all");
    document.layers[0].mask.reset();
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "folder.nested-removed");
    Expect(h.composition.CoverageTextureCount() <= 2, "folder.coverage-pool-bounded");
    // A hidden folder hides its children regardless of masks.
    document.layers[1].visible = false;
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "folder.hidden-parent");
}

void TestTransformRendering(Harness& h)
{
    // Legacy TransformTests.renderingRotatesScalesAndFlipsWithoutReplacingPixels: 8x8 image, left half red, right
    // half blue, placed at (8,8) 16x16 rotated 90 clockwise; the red half ends up on top. flipX puts blue on top.
    std::vector<std::uint8_t> bytes(8 * 8 * 4);
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            std::uint8_t* pixel = bytes.data() + (static_cast<std::size_t>(y) * 8 + static_cast<std::size_t>(x)) * 4;
            pixel[0] = x < 4 ? 0 : 255;
            pixel[1] = 0;
            pixel[2] = x < 4 ? 255 : 0;
            pixel[3] = 255;
        }
    }
    const Raster image = Raster::Make(8, 8, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = 32;
    document.height = 32;
    ImageLayer layer = LayerFrom(image, LayerBlendMode::Normal, L"Two colors");
    layer.transform.origin = Vec2{8, 8};
    layer.transform.size = Vec2{16, 16};
    layer.transform.rotation = 90;
    document.layers.push_back(layer);
    std::vector<std::uint8_t> pixels = h.Render(document);
    const auto at = [&](int x, int y, int channel) -> int
    {
        return pixels.size() == 32 * 32 * 4
                   ? pixels[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4 +
                            static_cast<std::size_t>(channel)]
                   : -1;
    };
    Expect(at(16, 12, 2) > 240 && at(16, 12, 0) < 10, "transform.rotate-90-red-on-top");
    Expect(at(16, 20, 0) > 240 && at(16, 20, 2) < 10, "transform.rotate-90-blue-below");
    Expect(at(2, 2, 3) == 0 && at(30, 30, 3) == 0, "transform.outside-bounds-transparent");
    document.layers[0].transform.flipX = true;
    pixels = h.Render(document);
    Expect(at(16, 12, 0) > 240 && at(16, 12, 2) < 10, "transform.flip-x-blue-on-top");
    document.layers[0].transform.flipX = false;
    document.layers[0].transform.rotation = 0;
    document.layers[0].transform.origin = Vec2{0, 0};
    document.layers[0].transform.size = Vec2{32, 32};
    pixels = h.Render(document);
    Expect(at(4, 4, 2) > 240 && at(28, 4, 0) > 240, "transform.scale-4x");
    document.layers[0].transform.flipY = true;
    pixels = h.Render(document);
    Expect(at(4, 4, 2) > 240 && at(28, 28, 0) > 240, "transform.flip-y-keeps-columns");
}

void TestResidencyAndCaching(Harness& h)
{
    CanvasDocument document = Document2x2();
    document.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red"));
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(pixels.size() == 16, "cache.first-compose");
    const std::uint64_t composites = h.composition.CompositeCount();
    Expect(SUCCEEDED(h.composition.Compose(document)) && h.composition.CompositeCount() == composites,
           "cache.settled-document-not-recomposited");
    h.composition.InvalidateDocument();
    Expect(SUCCEEDED(h.composition.Compose(document)) && h.composition.CompositeCount() == composites + 1,
           "cache.invalidate-recomposites");
    Expect(h.composition.CanvasSurfaceBytes() >= 2 * 2 * 2 * 4, "cache.surface-bytes-reported");
    h.composition.ReleaseDocumentSurfaces();
    Expect(h.composition.CanvasSurfaceBytes() == 0, "compose.hidden-surfaces");
    h.composition.ReleaseDeviceResources();
    Expect(h.composition.CanvasSurfaceBytes() == 0 && !h.composition.HasDevice(), "compose.device-released");
    Expect(SUCCEEDED(h.composition.CreateDeviceResources(h.device.get(), h.context.get())),
           "compose.device-loss-recreate");
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] == 255, "compose.after-recreate");
    // A document size change rebuilds the canvas and re-composites.
    document.width = 3;
    document.layers[0].transform.size = Vec2{3, 2};
    pixels = h.Render(document);
    Expect(pixels.size() == 3 * 2 * 4, "compose.resize-canvas");
}
} // namespace

int wmain()
{
    TestViewport();
    Harness harness;
    if (harness.Ready())
    {
        TestBlendModes(harness);
        TestOrderAndBlankLayers(harness);
        TestRasterMasks(harness);
        TestLiveMasks(harness);
        TestClippingStack(harness);
        TestFolderMasks(harness);
        TestTransformRendering(harness);
        TestResidencyAndCaching(harness);
    }
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All composition tests passed.\n");
    return 0;
}

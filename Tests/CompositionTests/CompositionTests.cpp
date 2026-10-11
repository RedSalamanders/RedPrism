#include "../Support/DocumentFixture.h"
#include "../Support/FailureReports.h"
#include "Adjust.h"
#include "BrushCompute.h"
#include "BrushStroke.h"
#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "Composition.Tests.AdjustmentDisplayAdmission.h"
#include "Composition.Tests.BrushDensityClear.h"
#include "Composition.Tests.BrushEpoch.h"
#include "Composition.Tests.CapturedCompletion.h"
#include "Composition.Tests.ChargedRaster.h"
#include "Composition.Tests.CommandStreamRestart.h"
#include "Composition.Tests.CommittedDisplay.h"
#include "Composition.Tests.CopyCompletionRetirement.h"
#include "Composition.Tests.DetachedEpoch.h"
#include "Composition.Tests.DisplayRebind.h"
#include "Composition.Tests.DisplayVisibility.h"
#include "Composition.Tests.EmptyJoinedShutdown.h"
#include "Composition.Tests.GpuBufferReadback.h"
#include "Composition.Tests.GpuBufferSlots.h"
#include "Composition.Tests.GpuCommandPump.h"
#include "Composition.Tests.GpuCommandStream.h"
#include "Composition.Tests.GpuCompletion.h"
#include "Composition.Tests.GpuResidencyGrowth.h"
#include "Composition.Tests.PublicationParent.h"
#include "Composition.Tests.RecordedAdjustment.h"
#include "Composition.Tests.RecordedBrush.h"
#include "Composition.Tests.RecordedDisplayParent.h"
#include "Composition.Tests.RecordedParentCancellation.h"
#include "Composition.Tests.RecordedParentPixels.h"
#include "Composition.Tests.RecordedParentSampler.h"
#include "Composition.Tests.RecordedUtilityRegions.h"
#include "Composition.Tests.RecordedWarpPreview.h"
#include "Composition.Tests.RestartDeviceOwnerControls.h"
#include "Composition.Tests.UtilityAdmission.h"
#include "Composition.Tests.UtilityCurrentContext.h"
#include "Composition.Tests.UtilityJoinedClose.h"
#include "Composition.Tests.UtilityOwnerService.h"
#include "Composition.Tests.UtilitySourceCapacity.h"
#include "Composition.Tests.VideoMemorySnapshot.h"
#include "Composition.Tests.WarpReplayCompute.h"
#include "CompositionStageTimingTests.h"
#include "Document.h"
#include "DocumentReadbackTests.h"
#include "FrameScheduler.h"
#include "GpuCapabilityTests.h"
#include "GpuCreatedUploadJobTests.h"
#include "GpuMipChainJobTests.h"
#include "GpuRasterTextureCreationTests.h"
#include "GpuRasterUploadJobTests.h"
#include "GpuResidencyBudgetTests.h"
#include "GpuUploadAdmissionTests.h"
#include "GpuUploadFaultTests.h"
#include "GpuUploadInitializationTests.h"
#include "GpuUploadTransportTests.h"
#include "GpuWorkTests.h"
#include "TransformOverlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d11_1.h>
#include <initializer_list>
#include <string_view>
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
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", name);
    std::fflush(stdout);
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

    // Legacy CompositionTests: round trip at several zooms and backing scales, cursor pixel fixed under zoom. The
    // legacy view is AppKit points; RedPrism's is device pixels (the Win32 client area), so the legacy 800 x 600
    // point view at backing 2 is 1600 x 1200 here and point offsets double.
    const Vec2 documentSize{1920, 1080};
    for (const double backing : {1.0, 2.0})
    {
        CanvasViewport v;
        v.Resize(Vec2{800 * backing, 600 * backing}, backing, documentSize);
        for (const double zoom : {0.25, 1.0, 3.75})
        {
            v.SetZoom(zoom, v.Center(), documentSize);
            v.Translate(Vec2{73.5 * backing, -44.25 * backing});
            const Vec2 pixel{183.25, 837.5};
            const Vec2 view = v.ViewFromDocument(pixel, documentSize);
            const Vec2 back = v.DocumentFromView(view, documentSize);
            Expect(std::abs(back.x - pixel.x) < 1e-6 && std::abs(back.y - pixel.y) < 1e-6, "viewport.round-trip");
            // Legacy `documentRect.width * backing == document.width * zoom`: zoom 1 is actual display pixels, so
            // the document is `width x zoom` device pixels wide on any display (RF-403; the golden used to multiply
            // a points width by the backing scale, which encoded the view-in-points wiring).
            const double drawn = v.ViewFromDocument(Vec2{documentSize.x, 0}, documentSize).x -
                                 v.ViewFromDocument(Vec2{0, 0}, documentSize).x;
            Expect(std::abs(drawn - documentSize.x * zoom) < 1e-6, "viewport.actual-pixels");
        }
    }
    CanvasViewport anchored;
    anchored.Resize(Vec2{2000, 1400}, 2, documentSize);
    const Vec2 anchor{314, 442};
    const Vec2 before = anchored.DocumentFromView(anchor, documentSize);
    anchored.SetZoom(4, anchor, documentSize);
    const Vec2 after = anchored.DocumentFromView(anchor, documentSize);
    Expect(std::abs(before.x - after.x) < 1e-6 && std::abs(before.y - after.y) < 1e-6, "viewport.zoom-keeps-cursor");

    // Legacy fitAndResizeModes: 96 points of margin in an 800 x 600 point view at backing 2 (1600 x 1200 device
    // pixels): the fitted document is at most 704 x 504 points, 1408 x 1008 device pixels, centered.
    CanvasViewport fit;
    fit.Resize(Vec2{1600, 1200}, 2, documentSize);
    const Vec2 origin = fit.DocumentRectOrigin(documentSize);
    const double width = documentSize.x * fit.ViewScale();
    const double height = documentSize.y * fit.ViewScale();
    Expect(width <= 1408.000001 && height <= 1008.000001 && width >= 1407.999, "viewport.fit-margin");
    Expect(std::abs(origin.x + width / 2 - 800) < 1e-6 && std::abs(origin.y + height / 2 - 600) < 1e-6,
           "viewport.fit-centered");
    fit.Translate(Vec2{120, -70});
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

    // RF-403: chrome constants are DIPs and the view is device pixels; the fit margin is 96 DIP at 150 % too.
    CanvasViewport scaled;
    scaled.Resize(Vec2{900, 600}, 2.25, documentSize);
    scaled.SetZoom(0.5, scaled.Center(), documentSize);
    Expect(std::abs(scaled.DipToView(10) - 22.5) < 1e-9 && std::abs(scaled.DipToDocument(10) - 45.0) < 1e-9,
           "viewport.dip-conversions");
    CanvasViewport fitted;
    fitted.Resize(Vec2{1200, 900}, 1.5, Vec2{1000, 1000});
    Expect(std::abs(fitted.zoom - (900.0 - 144.0) / 1000.0) < 1e-12, "viewport.fit-margin-in-dip");
    // One zoom scale decides crisp pixels (from 200 %) and the grid (from 800 %) on every display.
    bool thresholds = true;
    for (const double backing : {1.0, 1.5, 2.0})
    {
        CanvasViewport v;
        v.Resize(Vec2{800, 600}, backing, documentSize);
        v.SetZoom(1.5, v.Center(), documentSize);
        thresholds = thresholds && !v.SamplesCrisp() && !v.ShowsPixelGrid();
        v.SetZoom(4, v.Center(), documentSize);
        thresholds = thresholds && v.SamplesCrisp() && !v.ShowsPixelGrid();
        v.SetZoom(8, v.Center(), documentSize);
        thresholds = thresholds && v.SamplesCrisp() && v.ShowsPixelGrid();
    }
    Expect(thresholds, "viewport.crisp-and-grid-one-scale");
    // Actual pixels puts document pixels on whole device pixels, even with an odd hole at 150 %.
    CanvasViewport actual;
    actual.Resize(Vec2{1425, 975}, 1.5, Vec2{1280, 800});
    actual.SetZoom(1.0, actual.Center(), Vec2{1280, 800});
    const Vec2 actualOrigin = actual.DocumentRectOrigin(Vec2{1280, 800});
    Expect(actualOrigin.x == std::round(actualOrigin.x) && actualOrigin.y == std::round(actualOrigin.y),
           "viewport.actual-pixels-on-whole-pixels");

    // RF-403: a handle hits where it is drawn: 10 DIP of hit radius and a knob 28 DIP up, at 225 % and at 100 %.
    LayerTransform box;
    box.origin = Vec2{100, 100};
    box.size = Vec2{200, 100};
    CanvasViewport at225;
    at225.Resize(Vec2{1800, 1200}, 2.25, Vec2{1000, 800});
    at225.SetZoom(1.0, at225.Center(), Vec2{1000, 800});
    const TransformOverlayGeometry magnified = TransformOverlayGeometry::FromTransform(box, at225, Vec2{1000, 800});
    const std::optional<TransformHit> largeHit =
        magnified.Hit(Vec2{magnified.handles[0].x - 15, magnified.handles[0].y});
    Expect(largeHit.has_value() && largeHit->mode == TransformDrag::Mode::Resize && largeHit->handle == 0 &&
               std::abs(magnified.handles[1].y - magnified.rotationHandle.y - 28 * 2.25) < 1e-9,
           "overlay.hit-radius-and-knob-in-dip");
    CanvasViewport at100;
    at100.Resize(Vec2{800, 600}, 1, Vec2{1000, 800});
    at100.SetZoom(1.0, at100.Center(), Vec2{1000, 800});
    const TransformOverlayGeometry normal = TransformOverlayGeometry::FromTransform(box, at100, Vec2{1000, 800});
    Expect(!normal.Hit(Vec2{normal.handles[0].x - 15, normal.handles[0].y}).has_value() &&
               std::abs(normal.handles[1].y - normal.rotationHandle.y - 28) < 1e-9,
           "overlay.hit-radius-at-100");

    // RF-404: wheel zoom follows the delta: 1.1 per 120, the same total from a touchpad's ten 12s, nothing for 0.
    double partial = 1.0;
    for (int step = 0; step < 10; ++step)
    {
        partial *= CanvasViewport::WheelZoomFactor(12);
    }
    Expect(std::abs(CanvasViewport::WheelZoomFactor(120) - 1.1) < 1e-12 &&
               std::abs(CanvasViewport::WheelZoomFactor(240) - 1.21) < 1e-12 &&
               std::abs(CanvasViewport::WheelZoomFactor(-120) * 1.1 - 1.0) < 1e-12 &&
               CanvasViewport::WheelZoomFactor(0) == 1.0 && std::abs(partial - 1.1) < 1e-12,
           "wheel.zoom-follows-delta");
    // The Zoom tool: twice per 100 DIP dragged right, half per 100 left (legacy EditorCanvas).
    Expect(std::abs(CanvasViewport::DragZoomFactor(100) - 2.0) < 1e-12 &&
               std::abs(CanvasViewport::DragZoomFactor(-100) - 0.5) < 1e-12 &&
               CanvasViewport::DragZoomFactor(0) == 1.0 &&
               std::abs(CanvasViewport::DragZoomFactor(50) * CanvasViewport::DragZoomFactor(50) - 2.0) < 1e-12,
           "zoomtool.doubles-per-100-dip");
}

// RF-408, RF-409, RF-410: the frame loop's pure decisions (FrameScheduler.h).
void TestFrameLoop()
{
    // Marching ants step one DIP every 120 ms through the 8 DIP cycle (legacy EditorCanvas timer).
    Expect(MarchingAntsPhase(0) == 0.0f && MarchingAntsPhase(119) == 0.0f && MarchingAntsPhase(120) == 1.0f &&
               MarchingAntsPhase(959) == 7.0f && MarchingAntsPhase(960) == 0.0f,
           "ants.step-cadence");
    Expect(MillisecondsToNextAntStep(0) == 120 && MillisecondsToNextAntStep(119) == 1 &&
               MillisecondsToNextAntStep(125) == 115,
           "ants.wait-until-next-step");
    // Occluded without the DXGI occlusion message: probe on each wake instead of waiting for it forever.
    HostFrameState occluded;
    occluded.windowVisible = true;
    occluded.displayPoweredOn = true;
    occluded.rendererSuspended = false;
    occluded.rendererOccluded = true;
    occluded.frameInvalidated = false;
    Expect(SelectHostFrameAction(occluded) == HostFrameAction::WaitForMessage, "frame.occluded-waits-for-message");
    occluded.occlusionMessages = false;
    Expect(SelectHostFrameAction(occluded) == HostFrameAction::ProbeOcclusion, "frame.occluded-unregistered-probes");
    // A screenshot capture draws whatever the display's power, the window's visibility or its occlusion say.
    HostFrameState capture;
    capture.windowVisible = false;
    capture.displayPoweredOn = false;
    capture.rendererSuspended = false;
    capture.rendererOccluded = true;
    capture.frameInvalidated = true;
    capture.capturing = true;
    Expect(SelectHostFrameAction(capture) == HostFrameAction::Render, "frame.capture-draws-with-display-off");
    capture.capturing = false;
    Expect(SelectHostFrameAction(capture) == HostFrameAction::WaitForMessage, "frame.display-off-waits");
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
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture)
                            {
                                fixture.layers.push_back(
                                    LayerFrom(Solid(2, 2, base, base, base, 255), LayerBlendMode::Normal, L"Base"));
                            });
        EditDocumentFixture(document, [&](DocumentFixture& fixture)
                            { fixture.layers.push_back(LayerFrom(Solid(2, 2, top, top, top, 255), mode, L"Top")); });
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
    EditDocumentFixture(
        half, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, base, base, base, 255), LayerBlendMode::Normal, L"Base")); });
    EditDocumentFixture(
        half, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, top, top, top, 255), LayerBlendMode::Normal, L"Top")); });
    EditDocumentFixture(half, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 0.5; });
    std::vector<std::uint8_t> pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 153, 5), "blend.opacity-half");
    EditDocumentFixture(half, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 0; });
    pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 102, 5), "blend.opacity-zero");
    EditDocumentFixture(half, [&](DocumentFixture& fixture) { fixture.layers[1].visible = false; });
    EditDocumentFixture(half, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 1; });
    pixels = h.Render(half);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 102, 5), "blend.hidden-layer");

    // Legacy: Multiply of red over white is red; Color Dodge of red over mid gray saturates red only.
    CanvasDocument whiteOver = Document2x2();
    EditDocumentFixture(
        whiteOver, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 255, 255, 255, 255), LayerBlendMode::Normal, L"White")); });
    EditDocumentFixture(
        whiteOver, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Multiply, L"Red")); });
    pixels = h.Render(whiteOver);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 0, 2) && ChannelClose(pixels[1], 0, 2) &&
               ChannelClose(pixels[2], 255, 2) && pixels[3] == 255,
           "compose.multiply-golden");
    CanvasDocument dodge = Document2x2();
    EditDocumentFixture(
        dodge, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 128, 128, 128, 255), LayerBlendMode::Normal, L"Gray")); });
    EditDocumentFixture(
        dodge, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::ColorDodge, L"Dodge")); });
    pixels = h.Render(dodge);
    Expect(pixels.size() == 16 && ChannelClose(pixels[0], 128, 3) && ChannelClose(pixels[1], 128, 3) &&
               ChannelClose(pixels[2], 255, 3) && pixels[3] == 255,
           "compose.dodge-golden");
}

void TestOrderAndBlankLayers(Harness& h)
{
    // Legacy LayerTests.compositingHonorsVisibilityOrderAndBlankLayers.
    CanvasDocument document = Document2x2();
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red")); });
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 255, 0, 0, 255), LayerBlendMode::Normal, L"Blue")); });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[0] > 240 && pixels[2] < 10, "order.top-wins");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].visible = false; });
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] > 240 && pixels[0] < 10, "order.hidden-top");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].visible = true; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { std::swap(fixture.layers[0], fixture.layers[1]); });
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[2] > 240, "order.reordered");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(ImageLayer::Blank(L"Blank", Vec2{2, 2})); });
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
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red")); });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(1, 1, {255})); });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "mask.reveal-all");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(1, 1, {0})); });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "mask.hide-all");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.value().enabled = false; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "mask.disabled");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].mask = MaskOf(Gray(2, 2, {255, 0, 128, 255})); });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "mask.coverage");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].opacity = 0.5; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {128, 0, 64, 128}, 1), "mask.coverage-times-opacity");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].opacity = 1; });
    // Legacy transformedMaskResizeAndCanvasChangesStayAligned: flipX mirrors mask and pixels together.
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.flipX = true; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 255, 255, 128}, 1), "mask.flip-x");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.flipX = false; });
    // Rotation keeps the mask on the layer's local grid; total coverage is unchanged.
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.rotation = 90; });
    pixels = h.Render(document);
    int total = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
    {
        total += pixels[i];
    }
    Expect(pixels.size() == 16 && ChannelClose(total, 255 + 0 + 128 + 255, 4), "mask.rotation-keeps-total");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.rotation = 0; });
    // An unlinked mask placed one pixel right: outside its pixels it shows its edge tone (white here).
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.value().linked = false; });
    LayerTransform placement = document.layers[0].transform;
    placement.origin.x += 1;
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].mask.value().placement = placement; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 128}, 1), "mask.placement-shift");
    // A hide-all mask moved apart keeps hiding beyond its pixels (legacy aMovedHideAllMaskKeepsHidingPastItsPixels).
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(2, 2, {0, 0, 0, 0})); });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].mask.value().placement = placement; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "mask.placement-hide-all-background");
}

void TestAffineSamplingFootprint(Harness& h)
{
    // Independent pixel-center reference for the existing unit-square sampling contract. Two texels cover
    // four document pixels; nearest reads [0,0,1,1], linear clamp reads [0,1/4,3/4,1] between them. Thin targets
    // leave helper lanes outside the layer and exposed optimized ddx/ddy sinking in Release r460.
    constexpr std::array<std::uint8_t, 8> source{31, 31, 31, 31, 219, 219, 219, 219};
    const auto image = Raster::Make(2, 1, RasterFormat::Bgra8888, source.data(), source.size());
    CanvasDocument document;
    document.width = 4;
    document.height = 1;
    ImageLayer layer = LayerFrom(image, LayerBlendMode::Normal, L"Thin source");
    layer.transform.size = {4, 1};
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(layer); });
    const auto check = [&](std::initializer_list<int> expected, int tolerance, const char* name)
    {
        const auto pixels = h.Render(document);
        bool matches = pixels.size() == expected.size() * 4;
        std::size_t offset = 0;
        for (const auto value : expected)
        {
            for (std::size_t channel = 0; channel < 4 && matches; ++channel)
                matches = ChannelClose(pixels[offset + channel], value, tolerance);
            offset += 4;
        }
        if (!matches)
            PrintPixels(name, pixels);
        Expect(matches, name);
    };
    check({31, 31, 219, 219}, 0, "samplingfootprint.image-thin-nearest");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].transform.sampling = LayerSampling::Smooth; });
    check({31, 78, 172, 219}, 1, "samplingfootprint.image-thin-linear");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers[0].asset->image = Solid(4, 1, 255, 255, 255, 255);
                            fixture.layers[0].transform.sampling = LayerSampling::Nearest;
                            fixture.layers[0].mask = MaskOf(Gray(2, 1, {31, 219}));
                        });
    check({31, 31, 219, 219}, 0, "samplingfootprint.mask-unplaced-stretched");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].mask->placement = LayerTransform{Vec2{1, 0}, Vec2{2, 1}}; });
    check({0, 31, 219, 0}, 0, "samplingfootprint.mask-placed-hide-outside");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers[0].mask->placement.reset();
                            fixture.layers[0].transform.flipX = true;
                        });
    check({219, 219, 31, 31}, 0, "samplingfootprint.mask-flip-x");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers[0].transform.flipX = false;
                            fixture.layers[0].transform.sampling = LayerSampling::Smooth;
                        });
    check({31, 78, 172, 219}, 1, "samplingfootprint.mask-linear-stretched");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.width = 1;
                            fixture.height = 4;
                            fixture.layers[0].transform.sampling = LayerSampling::Nearest;
                            fixture.layers[0].transform.origin = {-1.5, 1.5};
                            fixture.layers[0].transform.rotation = 90;
                        });
    check({31, 31, 219, 219}, 0, "samplingfootprint.mask-rotated-thin-column");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(1, 1, {128})); });
    check({128, 128, 128, 128}, 0, "samplingfootprint.mask-uniform-rotated-column");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.width = 2;
                            fixture.height = 1;
                            fixture.layers.clear();
                            constexpr std::array<std::uint8_t, 16> alternating{0, 0, 0, 0, 255, 255, 255, 255,
                                                                               0, 0, 0, 0, 255, 255, 255, 255};
                            auto scaledLayer = LayerFrom(
                                Raster::Make(4, 1, RasterFormat::Bgra8888, alternating.data(), alternating.size()),
                                LayerBlendMode::Normal, L"Minified source");
                            scaledLayer.transform.size = {2, 1};
                            fixture.layers.push_back(std::move(scaledLayer));
                        });
    // A two-texel footprint selects mip 1; each pair averages to 127.5 (UNORM rounds to 128).
    check({128, 128}, 1, "samplingfootprint.image-minification-retains-mips");
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.width = 4;
                            fixture.layers.clear();
                            auto folder = ImageLayer::Blank(L"Thin folder", Vec2{4, 1});
                            folder.isGroup = true;
                            folder.transform.sampling = LayerSampling::Nearest;
                            folder.mask = MaskOf(Gray(2, 1, {31, 219}));
                            auto child = LayerFrom(Solid(4, 1, 255, 255, 255, 255), LayerBlendMode::Normal, L"Child");
                            child.parentID = folder.id;
                            fixture.layers.push_back(std::move(folder));
                            fixture.layers.push_back(std::move(child));
                        });
    check({31, 31, 219, 219}, 0, "samplingfootprint.folder-mask-stretched");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].mask->placement = LayerTransform{Vec2{1, 0}, Vec2{2, 1}}; });
    check({0, 31, 219, 0}, 0, "samplingfootprint.folder-mask-placed-clipped");
}

void TestLiveMasks(Harness& h)
{
    // Legacy LiveMaskTests.fixture: white target clipped to a hidden black source with soft alpha.
    CanvasDocument document = Document2x2();
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(
                                LayerFrom(AlphaRaster({255, 255, 255, 255}, 255), LayerBlendMode::Normal, L"Target"));
                        });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(
                                LayerFrom(AlphaRaster({255, 0, 128, 255}, 0), LayerBlendMode::Normal, L"Source"));
                        });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].visible = false; });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[0].maskSourceID = fixture.layers[1].id; });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "live.hidden-source-supplies-alpha");
    Expect(pixels.size() == 16 && pixels[0] == 255 && pixels[2] == 255, "live.color-comes-from-target");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 0.5; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {128, 0, 64, 128}, 1), "live.source-opacity-scales-coverage");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 1; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].mask = MaskOf(Gray(2, 2, {128, 128, 128, 128})); });
    pixels = h.Render(document);
    Expect(pixels.size() == 16 && pixels[3] < 200 && pixels[3] > 100, "live.raster-mask-multiplies");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.reset(); });
    // Moving the source moves the coverage (legacy movingSourceChangesCoverageAndChainsMultiply).
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].transform.origin.x += 1; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 255, 0, 128}, 1), "live.moved-source");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].transform.origin.x -= 1; });
    // Chains multiply: source clipped to a second hidden source.
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(
                                LayerFrom(AlphaRaster({0, 255, 255, 255}, 255), LayerBlendMode::Normal, L"Upstream"));
                        });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[2].visible = false; });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[1].maskSourceID = fixture.layers[2].id; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 128, 255}, 1), "live.chain-multiplies");
    // A blank (pixel-less) source hides the target entirely.
    CanvasDocument blankSource = Document2x2();
    EditDocumentFixture(
        blankSource, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Target")); });
    EditDocumentFixture(blankSource, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(ImageLayer::Blank(L"Blank", Vec2{2, 2})); });
    EditDocumentFixture(blankSource,
                        [&](DocumentFixture& fixture) { fixture.layers[0].maskSourceID = fixture.layers[1].id; });
    pixels = h.Render(blankSource);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "live.blank-source-hides");
}

void TestClippingStack(Harness& h)
{
    // Legacy LiveMaskTests.clippingColorPreservesSoftBaseAlphaWithoutBlackFringe: a white layer clipped to a soft
    // black base keeps the base alpha and takes the white color at full coverage, with no dark fringe.
    CanvasDocument document = Document2x2();
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(AlphaRaster({255, 128, 32, 0}, 0), LayerBlendMode::Normal, L"Base")); });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(
                                LayerFrom(AlphaRaster({255, 255, 255, 255}, 255), LayerBlendMode::Normal, L"White"));
                        });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[1].maskSourceID = fixture.layers[0].id; });
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
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 0.5; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 128, 32, 0}, 1), "stack.child-opacity-keeps-alpha");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 1; });
    // A white background under the stack: the result is fully white and opaque.
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.insert(
                                fixture.layers.begin(),
                                LayerFrom(Solid(2, 2, 255, 255, 255, 255), LayerBlendMode::Normal, L"Background"));
                        });
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
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[1].blendMode = LayerBlendMode::Multiply; });
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers[2] = LayerFrom(AlphaRaster({255, 255, 255, 255}, 128), LayerBlendMode::Normal, L"Gray"); });
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers[2].maskSourceID = fixture.layers[1].id; });
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers[0] = LayerFrom(Solid(2, 2, 200, 200, 200, 255), LayerBlendMode::Normal, L"Background"); });
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
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(folder); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(red); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(1, 1, {0})); });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "folder.hide-all");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.value().enabled = false; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 255, 255, 255}, 0), "folder.disabled");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].mask = MaskOf(Gray(2, 2, {255, 0, 128, 255})); });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "folder.coverage");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[1].mask = MaskOf(Gray(2, 2, {255, 0, 128, 255})); });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 64, 255}, 1), "folder.multiplies-layer-mask");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].mask.reset(); });
    // An enclosing folder's mask applies as well.
    ImageLayer outer = ImageLayer::Blank(L"Outer", Vec2{2, 2});
    outer.isGroup = true;
    outer.mask = MaskOf(Gray(1, 1, {0}));
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture) { fixture.layers.insert(fixture.layers.begin(), outer); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].parentID = fixture.layers[0].id; });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {0, 0, 0, 0}, 0), "folder.nested-hide-all");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.reset(); });
    pixels = h.Render(document);
    Expect(AlphasClose(pixels, {255, 0, 128, 255}, 1), "folder.nested-removed");
    Expect(h.composition.CoverageTextureCount() <= 2, "folder.coverage-pool-bounded");
    // A hidden folder hides its children regardless of masks.
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].visible = false; });
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
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(layer); });
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
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.flipX = true; });
    pixels = h.Render(document);
    Expect(at(16, 12, 0) > 240 && at(16, 12, 2) < 10, "transform.flip-x-blue-on-top");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.flipX = false; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.rotation = 0; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.origin = Vec2{0, 0}; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.size = Vec2{32, 32}; });
    pixels = h.Render(document);
    Expect(at(4, 4, 2) > 240 && at(28, 4, 0) > 240, "transform.scale-4x");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.flipY = true; });
    pixels = h.Render(document);
    Expect(at(4, 4, 2) > 240 && at(28, 28, 0) > 240, "transform.flip-y-keeps-columns");
}

void TestResidencyAndCaching(Harness& h)
{
    CanvasDocument document = Document2x2();
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red")); });
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
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.size = Vec2{3, 2}; });
    pixels = h.Render(document);
    Expect(pixels.size() == 3 * 2 * 4, "compose.resize-canvas");
}

// ── Brush (C10, Rendering_Brush.md) ──────────────────────────────────────

[[nodiscard]] int At(const std::vector<std::uint8_t>& pixels, int width, int x, int y, int channel) noexcept
{
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4 +
        static_cast<std::size_t>(channel);
    return index < pixels.size() ? pixels[index] : -1;
}

void TestSparseUpload(Harness& h)
{
    // A painted layer's raster reaches the GPU as its base texture plus tile uploads; its pixels are never assembled.
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 4;
    const Raster base = Solid(2, 2, 0, 0, 255, 255);
    const Raster green = Solid(1, 1, 0, 255, 0, 255);
    const Raster sparse = Raster::MakeSparse(4, 4, RasterFormat::Bgra8888, base, 1, 1, {RasterTile{0, 0, green}}, 0);
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(LayerFrom(sparse, LayerBlendMode::Normal, L"Painted")); });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(At(pixels, 4, 0, 0, 1) == 255 && At(pixels, 4, 1, 1, 2) == 255 && At(pixels, 4, 2, 2, 2) == 255 &&
               At(pixels, 4, 3, 3, 3) == 0 && At(pixels, 4, 0, 3, 3) == 0 && !sparse.IsMaterialized(),
           "sparse.upload-composes-base-and-tiles");
    // The same pixels assembled on the CPU render identically.
    const Raster twin = Raster::MakeSparse(4, 4, RasterFormat::Bgra8888, base, 1, 1, {RasterTile{0, 0, green}}, 0);
    const std::uint8_t* twinBytes = twin.Data(); // assembles the pixels first; ByteSize() is 0 until then
    const Raster assembled = Raster::Make(4, 4, RasterFormat::Bgra8888, twinBytes, twin.ByteSize());
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].asset = RasterAsset{assembled, assembled, L"Painted"}; });
    const std::vector<std::uint8_t> reference = h.Render(document);
    Expect(reference == pixels, "sparse.upload-matches-assembled");
    // A sparse mask reveals beyond its tiles (fill 255) and hides where a black tile lies.
    const Raster black = Gray(2, 2, {0, 0, 0, 0});
    const Raster mask = Raster::MakeSparse(4, 4, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 1, black}}, 255);
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers[0] = LayerFrom(Solid(4, 4, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red"); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(mask); });
    pixels = h.Render(document);
    Expect(At(pixels, 4, 0, 0, 3) == 255 && At(pixels, 4, 1, 1, 3) == 0 && At(pixels, 4, 2, 2, 3) == 0 &&
               At(pixels, 4, 3, 3, 3) == 255 && !mask.IsMaterialized(),
           "sparse.mask-upload");
    // A window into shared tile pixels uploads only its part.
    const Raster twoTone = AlphaRaster({255, 255, 255, 255}, 255); // 2 x 2 white
    RasterTile window{0, 0, twoTone};
    window.sourceX = 1;
    window.sourceY = 1;
    window.width = 1;
    window.height = 1;
    const Raster windowed = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {window}, 0);
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0] = LayerFrom(windowed, LayerBlendMode::Normal, L"Window"); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.reset(); });
    document.width = 2;
    document.height = 2;
    pixels = h.Render(document);
    Expect(At(pixels, 2, 0, 0, 3) == 255 && At(pixels, 2, 1, 1, 3) == 0 && At(pixels, 2, 1, 0, 3) == 0,
           "sparse.tile-window-upload");
}

void TestStrokePreview(Harness& h)
{
    // A stroke on a blank layer draws its working texture; the commit adopts that texture without an upload.
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    ImageLayer blank = ImageLayer::Blank(L"Blank", Vec2{8, 8});
    blank.transform.sampling = LayerSampling::Nearest;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(blank); });
    std::vector<std::uint8_t> pixels = h.Render(document);
    Expect(At(pixels, 8, 3, 3, 3) == 0, "stroke.blank-before");
    CanvasRenderer::StrokePreview preview;
    preview.layerID = blank.id;
    preview.mask = false;
    preview.width = 8;
    preview.height = 8;
    preview.transform = blank.transform;
    preview.fill = 0;
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && h.composition.StrokeTextureMissing(), "stroke.begin");
    const Raster red = Solid(4, 4, 0, 0, 255, 255);
    const std::array<RasterTile, 1> first{RasterTile{2, 2, red}};
    Expect(SUCCEEDED(h.composition.UpdateStrokeTiles(first)) && !h.composition.StrokeTextureMissing(),
           "stroke.first-tiles");
    pixels = h.Render(document);
    Expect(At(pixels, 8, 2, 2, 2) == 255 && At(pixels, 8, 5, 5, 2) == 255 && At(pixels, 8, 1, 1, 3) == 0 &&
               At(pixels, 8, 6, 6, 3) == 0,
           "stroke.preview-drawn");
    const std::uint64_t bytes = h.composition.CanvasSurfaceBytes();
    Expect(bytes >= 2ull * 8 * 8 * 4 + 8ull * 8 * 4, "stroke.working-texture-counted");
    const Raster green = Solid(2, 2, 0, 255, 0, 255);
    const std::array<RasterTile, 1> second{RasterTile{2, 2, green}};
    Expect(SUCCEEDED(h.composition.UpdateStrokeTiles(second)), "stroke.second-tiles");
    pixels = h.Render(document);
    Expect(At(pixels, 8, 2, 2, 1) == 255 && At(pixels, 8, 4, 4, 2) == 255, "stroke.tiles-replace");
    // Commit: the snapshot covering the grid from (2, 2) is adopted from the working texture.
    const Raster committed =
        Raster::Replacing(Raster{}, 0, 0, {RasterTile{2, 2, red}, RasterTile{2, 2, green}}, 2, 2, 4, 4, 0);
    h.composition.EndStroke(&committed, 2, 2, &blank.id);
    Expect(!h.composition.HasStroke() && !committed.IsMaterialized(), "stroke.ended");
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].asset = RasterAsset{committed, committed, L"Blank"}; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.origin = Vec2{2, 2}; });
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].transform.size = Vec2{4, 4}; });
    pixels = h.Render(document);
    Expect(At(pixels, 8, 2, 2, 1) == 255 && At(pixels, 8, 4, 4, 2) == 255 && At(pixels, 8, 1, 1, 3) == 0 &&
               At(pixels, 8, 6, 6, 3) == 0 && !committed.IsMaterialized(),
           "stroke.adopted-texture-draws");
    // A mask stroke: the working texture stands in for the layer's mask (a uniform mask becomes the fill).
    CanvasDocument masked{};
    masked.id = Guid::Generate();
    masked.width = 8;
    masked.height = 8;
    ImageLayer layer = LayerFrom(Solid(8, 8, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red");
    layer.mask = MaskOf(Gray(1, 1, {255}));
    EditDocumentFixture(masked, [&](DocumentFixture& fixture) { fixture.layers.push_back(layer); });
    CanvasRenderer::StrokePreview maskPreview;
    maskPreview.layerID = layer.id;
    maskPreview.mask = true;
    maskPreview.width = 8;
    maskPreview.height = 8;
    maskPreview.transform = layer.transform;
    maskPreview.fill = 255;
    Expect(SUCCEEDED(h.composition.BeginStroke(maskPreview)), "stroke.mask-begin");
    const std::array<RasterTile, 1> hide{
        RasterTile{2, 2, Gray(4, 4, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})}};
    Expect(SUCCEEDED(h.composition.UpdateStrokeTiles(hide)), "stroke.mask-tiles");
    pixels = h.Render(masked);
    Expect(At(pixels, 8, 0, 0, 3) == 255 && At(pixels, 8, 3, 3, 3) == 0 && At(pixels, 8, 7, 7, 3) == 255,
           "stroke.mask-preview");
    h.composition.EndStroke(nullptr, 0, 0);
    pixels = h.Render(masked);
    Expect(At(pixels, 8, 3, 3, 3) == 255, "stroke.mask-ended-restores");
    // Dropping the document surfaces drops the working texture; the next update rebuilds it from every tile.
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(first)),
           "stroke.again");
    h.composition.ReleaseDocumentSurfaces();
    Expect(h.composition.HasStroke() && h.composition.StrokeTextureMissing(), "stroke.texture-dropped-with-surfaces");
    Expect(SUCCEEDED(h.composition.UpdateStrokeTiles(first)) && !h.composition.StrokeTextureMissing(), "stroke.rebuilt");
    // The rebuilt texture draws the grid again from its placement: the red tile at (2, 2), nothing around it.
    pixels = h.Render(document);
    const bool rebuiltDrawn = At(pixels, 8, 2, 2, 2) == 255 && At(pixels, 8, 5, 5, 2) == 255 &&
                              At(pixels, 8, 1, 1, 3) == 0 && At(pixels, 8, 6, 6, 3) == 0;
    if (!rebuiltDrawn)
    {
        PrintPixels("rebuilt", pixels);
    }
    Expect(rebuiltDrawn, "stroke.rebuilt-preview-drawn");
    h.composition.EndStroke(nullptr, 0, 0);
}

void TestGpuBrushCoverage(Harness& h)
{
    // The compute kernel and the CPU kernel agree on every tile pixel, tails included.
    GpuBrushCoverage gpu;
    Expect(SUCCEEDED(gpu.Initialize(h.device.get(), h.context.get())) && gpu.Ready() && !gpu.UsesCpuPermanent(),
           "gpubrush.initialize");
    for (const double hardness : {1.0, 0.0})
    {
        BrushKernelParams params;
        params.radius = 12;
        params.hardness = hardness;
        params.canvasWidth = 64;
        params.canvasHeight = 64;
        params.antialiasWidth = 1;
        params.spacing = std::max(0.25, 24.0 * BrushSettings::SpacingFraction(hardness));
        std::vector<float> permanent(64 * 64, 0.0f);
        std::vector<std::uint8_t> cpuPreview(64 * 64, 0);
        std::vector<std::uint8_t> gpuPreview(64 * 64, 0);
        const BrushKernelTile cpuTile{7, 0, 0, 64, 64, permanent.data(), cpuPreview.data()};
        const std::array<BrushKernelTile, 1> gpuTiles{BrushKernelTile{7, 0, 0, 64, 64, nullptr, gpuPreview.data()}};
        gpu.BeginStroke();
        const auto compare = [&](const char* name)
        {
            int worst = 0;
            for (std::size_t i = 0; i < cpuPreview.size(); ++i)
            {
                worst = std::max(worst, std::abs(static_cast<int>(cpuPreview[i]) - static_cast<int>(gpuPreview[i])));
            }
            Expect(worst <= 2, name);
        };
        const std::array<BrushSegment, 1> dab{BrushSegment{Vec2{32, 32}, Vec2{32, 32}}};
        const std::array<BrushSegment, 1> firstTail{BrushSegment{Vec2{32, 32}, Vec2{56, 32}}};
        BrushKernel::RenderTile(cpuTile, dab, firstTail, params);
        Expect(gpu.Render(gpuTiles, dab, firstTail, params) && gpu.TileCount() == 1, "gpubrush.render");
        compare(hardness >= 1.0 ? "gpubrush.hard-dab-and-tail" : "gpubrush.soft-dab-and-tail");
        const std::array<BrushSegment, 1> settled{BrushSegment{Vec2{32, 32}, Vec2{56, 32}}};
        const std::array<BrushSegment, 1> secondTail{BrushSegment{Vec2{56, 32}, Vec2{56, 56}}};
        BrushKernel::RenderTile(cpuTile, settled, secondTail, params);
        Expect(gpu.Render(gpuTiles, settled, secondTail, params), "gpubrush.render-again");
        compare(hardness >= 1.0 ? "gpubrush.hard-settled" : "gpubrush.soft-settled");
        BrushKernel::RenderTile(cpuTile, {}, {}, params);
        Expect(gpu.Render(gpuTiles, {}, {}, params), "gpubrush.render-empty");
        compare(hardness >= 1.0 ? "gpubrush.hard-tail-removed" : "gpubrush.soft-tail-removed");
        Expect(gpuPreview[32 * 64 + 44] > 0 && gpuPreview[50 * 64 + 56] == 0, "gpubrush.tail-gone");
        gpu.EndStroke();
        Expect(gpu.TileCount() == 0, "gpubrush.stroke-ended");
    }
    // A whole stroke through the session with the GPU engine matches the CPU engine's pixels.
    DocumentSession cpuSession;
    cpuSession.CreateDocument(300, 120, false);
    cpuSession.AddBlankLayer();
    cpuSession.brushSettings = BrushSettings{40, 0, 1, 0, 0, 0.8};
    DocumentSession gpuSession;
    gpuSession.CreateDocument(300, 120, false);
    gpuSession.AddBlankLayer();
    gpuSession.brushSettings = cpuSession.brushSettings;
    gpuSession.SetBrushEngine(&gpu);
    for (DocumentSession* session : {&cpuSession, &gpuSession})
    {
        Expect(session->BeginBrush(Vec2{20, 60}), "gpubrush.session-begin");
        session->ContinueBrush(Vec2{150, 20});
        session->ContinueBrush(Vec2{280, 60});
        session->FinishBrush();
    }
    const Raster cpuImage = cpuSession.ActiveLayer()->asset->image;
    const Raster gpuImage = gpuSession.ActiveLayer()->asset->image;
    bool same = cpuImage.Width() == gpuImage.Width() && cpuImage.Height() == gpuImage.Height() &&
                cpuSession.ActiveLayer()->transform == gpuSession.ActiveLayer()->transform;
    if (same)
    {
        std::vector<std::uint8_t> a(cpuImage.ByteSize() == 0
                                        ? static_cast<std::size_t>(cpuImage.Stride()) * cpuImage.Height()
                                        : cpuImage.ByteSize());
        std::vector<std::uint8_t> b(a.size());
        cpuImage.ReadRegion(0, 0, cpuImage.Width(), cpuImage.Height(), a.data(), cpuImage.Stride(), 0);
        gpuImage.ReadRegion(0, 0, gpuImage.Width(), gpuImage.Height(), b.data(), gpuImage.Stride(), 0);
        int worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
        }
        same = worst <= 2;
    }
    Expect(same && gpuImage.IsSparse() && !gpuImage.IsMaterialized(), "gpubrush.session-matches-cpu");
    gpu.Release();
    Expect(!gpu.Ready(), "gpubrush.released");
}
} // namespace

// ── Adjustment layers and the HSL blends (C12) ─────────────────

[[nodiscard]] Raster Pixels2x2(std::initializer_list<std::array<std::uint8_t, 4>> pixels)
{
    std::vector<std::uint8_t> bytes;
    for (const std::array<std::uint8_t, 4>& pixel : pixels)
    {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    return Raster::Make(2, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

[[nodiscard]] LayerAdjustment AdjustmentOf(AdjustmentKind kind)
{
    LayerAdjustment adjustment;
    adjustment.kind = kind;
    return adjustment;
}

[[nodiscard]] ImageLayer AdjustmentLayer(const LayerAdjustment& adjustment, const wchar_t* name)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{2, 2});
    layer.adjustment = adjustment;
    return layer;
}

// Every channel of `pixels` within `slop` of `expected`, printing both when not.
[[nodiscard]] bool PixelsClose(const std::vector<std::uint8_t>& pixels, const std::vector<std::uint8_t>& expected,
                               int slop, const char* label)
{
    bool ok = pixels.size() == expected.size();
    for (std::size_t i = 0; ok && i < pixels.size(); ++i)
    {
        ok = ChannelClose(pixels[i], expected[i], slop);
    }
    if (!ok)
    {
        PrintPixels(label, pixels);
        PrintPixels("expected", expected);
    }
    return ok;
}

// The adjustment a failing parity case ran, for the failure's pixel dump.
[[nodiscard]] const char* AdjustmentLabel(AdjustmentKind kind) noexcept
{
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        return "hue/saturation gpu vs cpu";
    case AdjustmentKind::Levels:
        return "levels gpu vs cpu";
    case AdjustmentKind::Curves:
        return "curves gpu vs cpu";
    case AdjustmentKind::Exposure:
        return "exposure gpu vs cpu";
    case AdjustmentKind::GradientMap:
        return "gradient map gpu vs cpu";
    case AdjustmentKind::Grain:
        return "grain gpu vs cpu";
    }
    return "unknown adjustment gpu vs cpu";
}

[[nodiscard]] std::vector<std::uint8_t> RasterBytes(const Raster& raster)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(raster.Width()) *
                                    static_cast<std::size_t>(raster.Height()) * 4u);
    raster.ReadRegion(0, 0, raster.Width(), raster.Height(), bytes.data(), raster.Width() * 4, 0);
    return bytes;
}

void TestAdjustmentLayers(Harness& h)
{
    // Legacy AdjustmentLayerTests.globalAdjustmentAffectsBelowButNotAboveAndRemainsLive.
    {
        CanvasDocument document = Document2x2();
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture)
                            {
                                fixture.layers.push_back(
                                    LayerFrom(Solid(2, 2, 255, 255, 255, 255), LayerBlendMode::Normal, L"White"));
                            });
        LayerAdjustment levels = AdjustmentOf(AdjustmentKind::Levels);
        levels.levels.ranges[0].outputWhite = 0;
        EditDocumentFixture(document, [&](DocumentFixture& fixture)
                            { fixture.layers.push_back(AdjustmentLayer(levels, L"Levels")); });
        std::vector<std::uint8_t> pixels = h.Render(document);
        Expect(PixelsClose(pixels, {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255}, 0, "levels below"),
               "adjust.affects-below");
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture)
                            {
                                fixture.layers.push_back(
                                    LayerFrom(Pixels2x2({{0, 0, 255, 255}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}),
                                              LayerBlendMode::Normal, L"Red"));
                            });
        pixels = h.Render(document);
        Expect(pixels.size() == 16 &&
                   PixelsClose({pixels.begin(), pixels.begin() + 4}, {0, 0, 255, 255}, 0, "red above"),
               "adjust.not-above");
        // The base changes: the adjustment stays live.
        EditDocumentFixture(document, [&](DocumentFixture& fixture)
                            { fixture.layers[0].asset->image = Solid(2, 2, 255, 0, 0, 255); });
        pixels = h.Render(document);
        Expect(pixels.size() == 16 && PixelsClose({pixels.begin() + 4, pixels.begin() + 8}, {0, 0, 0, 255}, 0, "live"),
               "adjust.remains-live");
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].visible = false; });
        pixels = h.Render(document);
        Expect(pixels.size() == 16 &&
                   PixelsClose({pixels.begin() + 4, pixels.begin() + 8}, {255, 0, 0, 255}, 0, "hidden"),
               "adjust.hidden-does-nothing");
    }
    // Legacy clippedCurveChangesOnlyItsBaseAndCopyMergedMatchesExport.
    {
        CanvasDocument document = Document2x2();
        EditDocumentFixture(
            document, [&](DocumentFixture& fixture)
            { fixture.layers.push_back(LayerFrom(Solid(2, 2, 255, 0, 0, 255), LayerBlendMode::Normal, L"Blue")); });
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture)
                            {
                                fixture.layers.push_back(LayerFrom(
                                    Pixels2x2({{0, 255, 0, 255}, {0, 0, 0, 0}, {0, 128, 0, 128}, {0, 0, 0, 0}}),
                                    LayerBlendMode::Normal, L"Green"));
                            });
        LayerAdjustment curves = AdjustmentOf(AdjustmentKind::Curves);
        curves.curves.channels[0] = {CurvePoint{0, 255}, CurvePoint{255, 0}};
        ImageLayer adjustment = AdjustmentLayer(curves, L"Curves");
        adjustment.maskSourceID = document.layers[1].id;
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(adjustment); });
        std::vector<std::uint8_t> pixels = h.Render(document);
        Expect(pixels.size() == 16 &&
                   PixelsClose({pixels.begin(), pixels.begin() + 8}, {255, 0, 255, 255, 255, 0, 0, 255}, 1, "clipped"),
               "adjust.clipped-changes-only-its-base");
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[2].maskSourceID.reset(); });
        pixels = h.Render(document);
        Expect(pixels.size() == 16 &&
                   PixelsClose({pixels.begin() + 4, pixels.begin() + 8}, {0, 255, 255, 255}, 1, "unclipped"),
               "adjust.unclipped-changes-everything-below");
    }
    // Legacy hueOpacityAndMaskPreserveOriginalPixels.
    {
        CanvasDocument document = Document2x2();
        EditDocumentFixture(
            document, [&](DocumentFixture& fixture)
            { fixture.layers.push_back(LayerFrom(Solid(2, 2, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red")); });
        LayerAdjustment hsv = AdjustmentOf(AdjustmentKind::Hsv);
        hsv.hue = 120;
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture) { fixture.layers.push_back(AdjustmentLayer(hsv, L"Hue")); });
        std::vector<std::uint8_t> pixels = h.Render(document);
        Expect(pixels.size() == 16 && ChannelClose(pixels[1], 255, 8) && ChannelClose(pixels[0], 0, 8) &&
                   ChannelClose(pixels[2], 0, 8) && pixels[3] == 255,
               "adjust.hue-rotates");
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 0; });
        pixels = h.Render(document);
        Expect(PixelsClose({pixels.begin(), pixels.begin() + 4}, {0, 0, 255, 255}, 0, "opacity 0"),
               "adjust.opacity-zero-keeps-pixels");
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = 1; });
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture) { fixture.layers[1].mask = MaskOf(Gray(1, 1, {0})); });
        pixels = h.Render(document);
        Expect(PixelsClose({pixels.begin(), pixels.begin() + 4}, {0, 0, 255, 255}, 0, "black mask"),
               "adjust.black-mask-keeps-pixels");
    }
    // Legacy adjustmentBlendAndSoftMaskPreserveCoverage.
    {
        CanvasDocument document = Document2x2();
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture)
                            {
                                fixture.layers.push_back(LayerFrom(
                                    Pixels2x2({{0, 0, 255, 255}, {0, 0, 128, 128}, {0, 0, 32, 32}, {0, 0, 0, 0}}),
                                    LayerBlendMode::Normal, L"Red"));
                            });
        LayerAdjustment hsv = AdjustmentOf(AdjustmentKind::Hsv);
        hsv.hue = 120;
        ImageLayer adjustment = AdjustmentLayer(hsv, L"Hue");
        adjustment.blendMode = LayerBlendMode::Multiply;
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(adjustment); });
        std::vector<std::uint8_t> pixels = h.Render(document);
        Expect(PixelsClose(pixels, {0, 0, 0, 255, 0, 0, 0, 128, 0, 0, 0, 32, 0, 0, 0, 0}, 1, "multiply"),
               "adjust.blend-keeps-coverage");
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture) { fixture.layers[1].blendMode = LayerBlendMode::Normal; });
        EditDocumentFixture(document,
                            [&](DocumentFixture& fixture) { fixture.layers[1].mask = MaskOf(Gray(1, 1, {128})); });
        pixels = h.Render(document);
        Expect(AlphasClose(pixels, {255, 128, 32, 0}, 0) && ChannelClose(pixels[2], 127, 3) &&
                   ChannelClose(pixels[1], 128, 3),
               "adjust.soft-mask-mixes");
    }
    // Every kind on the GPU against the CPU reference (Adjust::ApplyAdjustment).
    {
        const Raster base = Pixels2x2({{10, 60, 200, 255}, {120, 30, 90, 255}, {200, 200, 40, 255}, {0, 64, 32, 128}});
        std::vector<std::pair<LayerAdjustment, int>> cases;
        LayerAdjustment levels = AdjustmentOf(AdjustmentKind::Levels);
        levels.levels.channel = LevelsChannel::Red;
        Adjust::SetLevelsCurrent(levels.levels, LevelRange{20, 1.4, 230, 10, 245});
        levels.levels.channel = LevelsChannel::Rgb;
        Adjust::SetLevelsCurrent(levels.levels, LevelRange{0, 0.8, 255, 0, 255});
        cases.emplace_back(levels, 1);
        LayerAdjustment curves = AdjustmentOf(AdjustmentKind::Curves);
        curves.curves.channels[0] = {CurvePoint{0, 0}, CurvePoint{96, 140}, CurvePoint{255, 255}};
        curves.curves.channels[3] = {CurvePoint{0, 30}, CurvePoint{255, 200}};
        cases.emplace_back(curves, 1);
        LayerAdjustment exposure = AdjustmentOf(AdjustmentKind::Exposure);
        exposure.exposureSettings = ExposureSettings{1, 0.05, 1.5};
        cases.emplace_back(exposure, 1);
        LayerAdjustment map = AdjustmentOf(AdjustmentKind::GradientMap);
        map.gradientMapSettings =
            GradientMapSettings{AdjustmentColor{0.9, 0.1, 0.2}, AdjustmentColor{0.1, 0.3, 1}, false};
        cases.emplace_back(map, 1);
        LayerAdjustment hsv = AdjustmentOf(AdjustmentKind::Hsv);
        HueSaturationSettings settings = Adjust::HsvSettings(60, 30, 10);
        settings.adjustments[static_cast<std::size_t>(ColorRange::Blues)] = RangeAdjustment{-40, 20, -10};
        hsv.hsvSettings = settings;
        cases.emplace_back(hsv, 4);
        LayerAdjustment grain = AdjustmentOf(AdjustmentKind::Grain);
        grain.grainSettings = GrainSettings{60, 2, 40, 7};
        cases.emplace_back(grain, 2);
        for (const auto& [adjustment, slop] : cases)
        {
            CanvasDocument document = Document2x2();
            EditDocumentFixture(document, [&](DocumentFixture& fixture)
                                { fixture.layers.push_back(LayerFrom(base, LayerBlendMode::Normal, L"Base")); });
            EditDocumentFixture(document, [&](DocumentFixture& fixture)
                                { fixture.layers.push_back(AdjustmentLayer(adjustment, L"Adjustment")); });
            const std::vector<std::uint8_t> pixels = h.Render(document);
            const std::vector<std::uint8_t> reference = RasterBytes(Adjust::ApplyAdjustment(adjustment, base));
            Expect(PixelsClose(pixels, reference, slop, AdjustmentLabel(adjustment.kind)), "adjust.gpu-matches-cpu");
        }
    }
    // Reuse lookup textures for settings ticks on one layer; compare every composition with the scalar reference.
    {
        const Raster base = Pixels2x2({{10, 60, 200, 255}, {120, 30, 90, 255}, {200, 200, 40, 255}, {0, 32, 16, 128}});
        LayerAdjustment levels = AdjustmentOf(AdjustmentKind::Levels);
        Adjust::SetLevelsCurrent(levels.levels, LevelRange{0, 1.25, 255, 0, 255});

        CanvasDocument document = Document2x2();
        EditDocumentFixture(document, [&](DocumentFixture& fixture)
                            { fixture.layers.push_back(LayerFrom(base, LayerBlendMode::Normal, L"Base")); });
        EditDocumentFixture(document, [&](DocumentFixture& fixture)
                            { fixture.layers.push_back(AdjustmentLayer(levels, L"Reusable adjustment")); });
        const Guid adjustmentId = document.layers[1].id;
        const std::uint64_t createsBefore = h.composition.AdjustmentTextureCreates();
        const std::uint64_t updatesBefore = h.composition.AdjustmentTextureUpdates();

        const auto renderAndCheck =
            [&](const char* step, int slop, std::uint64_t createDelta, std::uint64_t updateDelta)
        {
            const std::vector<std::uint8_t> pixels = h.Render(document);
            const LayerAdjustment& current = document.layers[1].adjustment.value();
            const std::vector<std::uint8_t> reference = RasterBytes(Adjust::ApplyAdjustment(current, base));
            char label[96]{};
            std::snprintf(label, sizeof(label), "adjust.reuse.%s.pixel", step);
            Expect(PixelsClose(pixels, reference, slop, label), label);
            std::snprintf(label, sizeof(label), "adjust.reuse.%s.alpha", step);
            Expect(AlphasClose(pixels, {255, 255, 255, 128}, 0), label);
            std::snprintf(label, sizeof(label), "adjust.reuse.%s.same-guid", step);
            Expect(document.layers[1].id == adjustmentId, label);
            std::snprintf(label, sizeof(label), "adjust.reuse.%s.creates", step);
            Expect(h.composition.AdjustmentTextureCreates() == createsBefore + createDelta, label);
            std::snprintf(label, sizeof(label), "adjust.reuse.%s.updates", step);
            Expect(h.composition.AdjustmentTextureUpdates() == updatesBefore + updateDelta, label);
        };

        renderAndCheck("levels-warm", 1, 1, 1);
        const std::uint64_t lutBytes = 256ull * 16ull;
        const std::uint64_t cubeBytes = static_cast<std::uint64_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension *
                                        Adjust::kCubeDimension * 16ull;
        std::uint64_t bytesBeforeSwitch = h.composition.CanvasSurfaceBytes();
        Expect(bytesBeforeSwitch >= lutBytes, "adjust.reuse.lut-counted");

        renderAndCheck("unchanged-invalidated", 1, 1, 1);
        levels.levels.ranges[0].gamma = 1.75;
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = levels; });
        renderAndCheck("levels-gamma", 1, 1, 2);

        LayerAdjustment curves = AdjustmentOf(AdjustmentKind::Curves);
        curves.curves.channels[0] = {CurvePoint{0, 10}, CurvePoint{128, 170}, CurvePoint{255, 245}};
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = curves; });
        renderAndCheck("curves", 1, 1, 3);

        LayerAdjustment exposure = AdjustmentOf(AdjustmentKind::Exposure);
        exposure.exposureSettings = ExposureSettings{1, 0.05, 1.5};
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = exposure; });
        renderAndCheck("exposure", 1, 1, 4);

        LayerAdjustment map = AdjustmentOf(AdjustmentKind::GradientMap);
        map.gradientMapSettings =
            GradientMapSettings{AdjustmentColor{0.9, 0.1, 0.2}, AdjustmentColor{0.1, 0.3, 1}, false};
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = map; });
        renderAndCheck("gradient-map", 1, 1, 5);

        LayerAdjustment hsv = AdjustmentOf(AdjustmentKind::Hsv);
        hsv.hsvSettings = Adjust::HsvSettings(45, 25, 5);
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = hsv; });
        renderAndCheck("hsv-warm", 4, 2, 6);
        renderAndCheck("unchanged-cube-invalidated", 4, 2, 6);
        Expect(h.composition.CanvasSurfaceBytes() - bytesBeforeSwitch == cubeBytes - lutBytes,
               "adjust.reuse.switch-lut-to-cube-releases-lut");
        bytesBeforeSwitch = h.composition.CanvasSurfaceBytes();

        hsv.hsvSettings = Adjust::HsvSettings(115, -10, 15);
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = hsv; });
        renderAndCheck("hsv-settings", 4, 2, 7);

        exposure.exposureSettings = ExposureSettings{1.5, 0.02, 1.2};
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = exposure; });
        renderAndCheck("switch-back-to-lut", 1, 3, 8);
        Expect(bytesBeforeSwitch - h.composition.CanvasSurfaceBytes() == cubeBytes - lutBytes,
               "adjust.reuse.switch-cube-to-lut-releases-cube");

        hsv.hsvSettings = Adjust::HsvSettings(75, 35, -5);
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = hsv; });
        renderAndCheck("cube-recreated-after-kind-switch", 4, 4, 9);
        h.composition.ReleaseDocumentSurfaces();
        Expect(h.composition.CanvasSurfaceBytes() == 0, "adjust.reuse.hidden-surfaces-released");
        renderAndCheck("cube-rebuilt-after-hidden-release", 4, 5, 10);

        exposure.exposureSettings = ExposureSettings{0.75, 0.01, 1.3};
        EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].adjustment = exposure; });
        renderAndCheck("lut-rebuilt-after-hidden-release", 1, 6, 11);
    }
    // The tables and the cube are released with the document surfaces.
    Expect(h.composition.CanvasSurfaceBytes() > 0, "adjust.textures-counted");
    h.composition.ReleaseDocumentSurfaces();
    Expect(h.composition.CanvasSurfaceBytes() == 0, "adjust.textures-released");
}

void TestHslBlends(Harness& h)
{
    // Hue, Saturation, Color and Luminosity against the CPU PDF formulas (Adjust::BlendStraight), opaque and at
    // half opacity (Co = (1 - as) Cb + as B over an opaque backdrop).
    struct Case
    {
        std::array<std::uint8_t, 3> backdrop; // b, g, r
        std::array<std::uint8_t, 3> source;
        LayerBlendMode mode;
    };
    const std::vector<Case> cases{
        {{128, 128, 128}, {0, 0, 255}, LayerBlendMode::Color},
        {{0, 0, 255}, {128, 128, 128}, LayerBlendMode::Luminosity},
        {{128, 128, 128}, {255, 0, 0}, LayerBlendMode::Hue},
        {{0, 0, 255}, {128, 128, 128}, LayerBlendMode::Saturation},
        {{40, 200, 90}, {220, 30, 160}, LayerBlendMode::Hue},
        {{40, 200, 90}, {220, 30, 160}, LayerBlendMode::Saturation},
        {{40, 200, 90}, {220, 30, 160}, LayerBlendMode::Color},
        {{40, 200, 90}, {220, 30, 160}, LayerBlendMode::Luminosity},
    };
    for (const Case& test : cases)
    {
        for (const double opacity : {1.0, 0.5})
        {
            CanvasDocument document = Document2x2();
            EditDocumentFixture(document,
                                [&](DocumentFixture& fixture)
                                {
                                    fixture.layers.push_back(LayerFrom(
                                        Solid(2, 2, test.backdrop[0], test.backdrop[1], test.backdrop[2], 255),
                                        LayerBlendMode::Normal, L"Backdrop"));
                                });
            EditDocumentFixture(document,
                                [&](DocumentFixture& fixture)
                                {
                                    fixture.layers.push_back(
                                        LayerFrom(Solid(2, 2, test.source[0], test.source[1], test.source[2], 255),
                                                  test.mode, L"Source"));
                                });
            EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[1].opacity = opacity; });
            const std::vector<std::uint8_t> pixels = h.Render(document);
            const std::array<double, 3> blended = Adjust::BlendStraight(
                {test.backdrop[2] / 255.0, test.backdrop[1] / 255.0, test.backdrop[0] / 255.0},
                {test.source[2] / 255.0, test.source[1] / 255.0, test.source[0] / 255.0}, test.mode);
            std::vector<std::uint8_t> expected(16, 255);
            for (std::size_t i = 0; i < 4; ++i)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const double backdrop = test.backdrop[c] / 255.0;
                    const double value = (1 - opacity) * backdrop + opacity * blended[2 - c];
                    expected[i * 4 + c] = static_cast<std::uint8_t>(std::lround(value * 255));
                }
            }
            Expect(PixelsClose(pixels, expected, 2, LayerBlendModeName(test.mode)), "blend.hsl-golden");
        }
    }
}

// ── Review fixes WP4: blit, oversize documents, readbacks, stroke previews ──────

// `viewport` blitted into a width x height RGBA target over its whole area; RGBA rows.
[[nodiscard]] std::vector<std::uint8_t> Blit(Harness& h, const CanvasViewport& viewport, UINT width, UINT height)
{
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
    if (FAILED(h.device->CreateTexture2D(&description, nullptr, target.put())) ||
        FAILED(h.device->CreateRenderTargetView(target.get(), nullptr, view.put())))
    {
        return {};
    }
    const std::array<float, 4> clear{0, 0, 0, 0};
    h.context->ClearRenderTargetView(view.get(), clear.data());
    if (FAILED(h.composition.BlitToTarget(view.get(), width, height, viewport, 0, 0, static_cast<float>(width),
                                         static_cast<float>(height))))
    {
        return {};
    }
    description.BindFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(h.device->CreateTexture2D(&description, nullptr, staging.put())))
    {
        return {};
    }
    h.context->CopyResource(staging.get(), target.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(h.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
    {
        return {};
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (UINT y = 0; y < height; ++y)
    {
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4u,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4u);
    }
    h.context->Unmap(staging.get(), 0);
    return pixels;
}

// Opaque BGRA columns: `left` columns black and the rest white, or every `period` columns alternating.
[[nodiscard]] Raster Columns(int width, int height, int split)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::uint8_t value = x < split ? 0 : 255;
            std::uint8_t* pixel = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] CanvasDocument DocumentOf(int width, int height)
{
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    return document;
}

void TestBlit(Harness& h)
{
    // RF-403: zoom is device pixels per document pixel. A 2 x 1 document (black, white) at zoom 2 on a backing-2
    // display covers 4 device pixels, centered in a 16 x 8 hole: columns 6..9.
    CanvasDocument pair = DocumentOf(2, 1);
    EditDocumentFixture(pair, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(LayerFrom(Columns(2, 1, 1), LayerBlendMode::Normal, L"Pair")); });
    (void)h.Render(pair);
    const auto red = [](const std::vector<std::uint8_t>& pixels, int width, int x, int y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
        return index < pixels.size() ? static_cast<int>(pixels[index]) : -1;
    };
    CanvasViewport view;
    view.Resize(Vec2{16, 8}, 2, Vec2{2, 1});
    view.SetZoom(2, view.Center(), Vec2{2, 1});
    std::vector<std::uint8_t> pixels = Blit(h, view, 16, 8);
    const bool checkerLeft = red(pixels, 16, 5, 4) == 46 || red(pixels, 16, 5, 4) == 56;
    const bool checkerRight = red(pixels, 16, 10, 4) == 46 || red(pixels, 16, 10, 4) == 56;
    const bool devicePixels = checkerLeft && checkerRight && red(pixels, 16, 6, 4) < 10 && red(pixels, 16, 9, 4) > 245;
    if (!devicePixels)
    {
        PrintPixels("zoom 2 at backing 2", pixels);
    }
    Expect(devicePixels, "blit.zoom-is-device-pixels");
    // From 200 % the pixels are hard-edged: at zoom 4 the device pixel left of the boundary is pure black.
    view.SetZoom(4, view.Center(), Vec2{2, 1});
    pixels = Blit(h, view, 16, 8);
    Expect(red(pixels, 16, 7, 4) < 3 && red(pixels, 16, 8, 4) > 252, "blit.crisp-from-200");
    // Below 200 % they are filtered: at 150 % the same boundary blends.
    view.SetZoom(1.5, view.Center(), Vec2{2, 1});
    pixels = Blit(h, view, 16, 8);
    Expect(red(pixels, 16, 7, 4) > 20 && red(pixels, 16, 7, 4) < 235, "blit.smooth-below-200");

    // RF-412: zoomed out, the canvas reduces through its mips. One-pixel black and white columns at 25 %, sampled at
    // exact texel centers of the black columns, read mid gray, not black.
    CanvasDocument stripes = DocumentOf(64, 64);
    std::vector<std::uint8_t> striped(64u * 64u * 4u);
    for (std::size_t i = 0; i < 64u * 64u; ++i)
    {
        const std::uint8_t value = (i % 64u) % 2u == 0 ? 0 : 255;
        striped[i * 4] = value;
        striped[i * 4 + 1] = value;
        striped[i * 4 + 2] = value;
        striped[i * 4 + 3] = 255;
    }
    EditDocumentFixture(stripes,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers.push_back(
                                LayerFrom(Raster::Make(64, 64, RasterFormat::Bgra8888, striped.data(), striped.size()),
                                          LayerBlendMode::Normal, L"Stripes"));
                        });
    (void)h.Render(stripes);
    CanvasViewport out;
    out.Resize(Vec2{16, 16}, 1, Vec2{64, 64});
    out.SetZoom(0.25, out.Center(), Vec2{64, 64});
    out.pan = Vec2{0.375, 0.375};
    pixels = Blit(h, out, 16, 16);
    bool gray = pixels.size() == 16u * 16u * 4u;
    for (std::size_t i = 0; gray && i < pixels.size(); i += 4)
    {
        gray = pixels[i] >= 100 && pixels[i] <= 155;
    }
    if (!gray)
    {
        PrintPixels("25 % stripes", pixels);
    }
    Expect(gray, "blit.zoomed-out-reduces-through-mips");
}

void TestOversize(Harness& h)
{
    // Decision D1 (RF-401): a document and layers larger than the device's textures, with the limit lowered to 64
    // (16 384 on the device) so a small document proves the paths.
    h.composition.SetTextureLimitForTest(64);
    CanvasDocument document = DocumentOf(100, 10);
    std::vector<std::uint8_t> halves(100u * 10u * 4u);
    for (std::size_t i = 0; i < 100u * 10u; ++i)
    {
        const bool left = i % 100u < 50u;
        halves[i * 4] = left ? 0 : 255;
        halves[i * 4 + 1] = 0;
        halves[i * 4 + 2] = left ? 255 : 0;
        halves[i * 4 + 3] = 255;
    }
    const Raster wide = Raster::Make(100, 10, RasterFormat::Bgra8888, halves.data(), halves.size());
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(LayerFrom(wide, LayerBlendMode::Normal, L"Wide")); });
    h.composition.InvalidateDocument();
    Expect(SUCCEEDED(h.composition.Compose(document)) && h.composition.CanvasScale() == 2 &&
               h.composition.RegionTextureCount() == 1,
           "oversize.display-proxy");
    std::vector<std::uint8_t> ignored;
    UINT ignoredWidth = 0;
    UINT ignoredHeight = 0;
    Expect(FAILED(h.composition.Readback(ignored, ignoredWidth, ignoredHeight)), "oversize.proxy-is-not-the-document");
    // The display draws the proxy: red on the left, blue on the right, at actual pixels.
    CanvasViewport view;
    view.Resize(Vec2{100, 10}, 1, Vec2{100, 10});
    view.SetZoom(1, view.Center(), Vec2{100, 10});
    const std::vector<std::uint8_t> shown = Blit(h, view, 100, 10);
    Expect(shown.size() == 100u * 10u * 4u && shown[(5u * 100u + 10u) * 4u] > 250 &&
               shown[(5u * 100u + 90u) * 4u + 2u] > 250,
           "oversize.display-shows-proxy");
    // A readback composes the document at full resolution in tiles, each layer as the part the tile shows.
    std::vector<std::uint8_t> flat;
    UINT width = 0;
    UINT height = 0;
    bool exact = SUCCEEDED(h.composition.ComposeReadback(document, flat, width, height)) && width == 100 &&
                 height == 10 && flat.size() == halves.size();
    exact = exact && std::equal(flat.begin(), flat.end(), halves.begin());
    Expect(exact, "oversize.readback-full-resolution");
    // A mask larger than the limit: revealing the left 30 columns only.
    std::vector<std::uint8_t> maskBytes(100u * 10u, 0);
    for (std::size_t i = 0; i < maskBytes.size(); ++i)
    {
        maskBytes[i] = i % 100u < 30u ? 255 : 0;
    }
    EditDocumentFixture(document,
                        [&](DocumentFixture& fixture)
                        {
                            fixture.layers[0].mask =
                                MaskOf(Raster::Make(100, 10, RasterFormat::Gray8, maskBytes.data(), maskBytes.size()));
                        });
    exact = SUCCEEDED(h.composition.ComposeReadback(document, flat, width, height)) && flat.size() == halves.size() &&
            flat[(4u * 100u + 29u) * 4u + 3u] == 255 && flat[(4u * 100u + 30u) * 4u + 3u] == 0 &&
            flat[(4u * 100u + 99u) * 4u + 3u] == 0;
    Expect(exact, "oversize.mask-readback");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.reset(); });
    // A stroke whose grid is larger than the limit previews through a reduced working texture and is never adopted:
    // the committed raster uploads on its own and reads back exactly.
    CanvasDocument painting = DocumentOf(100, 10);
    ImageLayer blank = ImageLayer::Blank(L"Blank", Vec2{100, 10});
    blank.transform.sampling = LayerSampling::Nearest;
    EditDocumentFixture(painting, [&](DocumentFixture& fixture) { fixture.layers.push_back(blank); });
    CanvasRenderer::StrokePreview preview;
    preview.layerID = blank.id;
    preview.width = 100;
    preview.height = 10;
    preview.transform = blank.transform;
    h.composition.InvalidateDocument();
    Expect(SUCCEEDED(h.composition.Compose(painting)), "oversize.stroke-document");
    const std::array<RasterTile, 1> tile{RasterTile{0, 0, wide}};
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(tile)) &&
               !h.composition.StrokeTextureMissing(),
           "oversize.stroke-working-texture");
    h.composition.InvalidateDocument();
    Expect(SUCCEEDED(h.composition.Compose(painting)), "oversize.stroke-composes");
    const std::vector<std::uint8_t> painted = Blit(h, view, 100, 10);
    Expect(painted.size() == 100u * 10u * 4u && painted[(5u * 100u + 10u) * 4u] > 250 &&
               painted[(5u * 100u + 90u) * 4u + 2u] > 250,
           "oversize.stroke-preview-drawn");
    h.composition.EndStroke(&wide, 0, 0, &blank.id);
    EditDocumentFixture(painting,
                        [&](DocumentFixture& fixture) { fixture.layers[0].asset = RasterAsset{wide, wide, L"Blank"}; });
    exact = SUCCEEDED(h.composition.ComposeReadback(painting, flat, width, height)) && flat.size() == halves.size() &&
            std::equal(flat.begin(), flat.end(), halves.begin());
    Expect(exact, "oversize.stroke-commit-reads-back");
    h.composition.SetTextureLimitForTest(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);

    // The device's own limit: a 17 000 pixel document and layer (legacy allows 30 000 a side).
    CanvasDocument panorama = DocumentOf(17000, 10);
    std::vector<std::uint8_t> wideBytes(17000u * 10u * 4u);
    for (std::size_t i = 0; i < 17000u * 10u; ++i)
    {
        const bool left = i % 17000u < 8500u;
        wideBytes[i * 4] = left ? 0 : 255;
        wideBytes[i * 4 + 1] = 0;
        wideBytes[i * 4 + 2] = left ? 255 : 0;
        wideBytes[i * 4 + 3] = 255;
    }
    const Raster wideLayer = Raster::Make(17000, 10, RasterFormat::Bgra8888, wideBytes.data(), wideBytes.size());
    EditDocumentFixture(panorama, [&](DocumentFixture& fixture)
                        { fixture.layers.push_back(LayerFrom(wideLayer, LayerBlendMode::Normal, L"Panorama")); });
    h.composition.InvalidateDocument();
    Expect(SUCCEEDED(h.composition.Compose(panorama)) && h.composition.CanvasScale() == 2, "oversize.device-limit-proxy");
    exact = SUCCEEDED(h.composition.ComposeReadback(panorama, flat, width, height)) && width == 17000 && height == 10 &&
            std::equal(flat.begin(), flat.end(), wideBytes.begin(), wideBytes.end());
    Expect(exact, "oversize.device-limit-readback");
    Raster adopted;
    Expect(SUCCEEDED(h.composition.ComposeReadback(panorama, adopted)) && adopted.Width() == 17000 &&
               adopted.Height() == 10 && adopted.ByteSize() == wideBytes.size() &&
               std::equal(wideBytes.begin(), wideBytes.end(), adopted.Data()),
           "oversize.device-limit-adopted-readback");
    h.composition.ReleaseDocumentSurfaces();
}

void TestReadbackIgnoresPreview(Harness& h)
{
    // RF-402: export, Copy Merged, the wand and the eyedroppers read the document, never the stroke preview.
    CanvasDocument document = DocumentOf(8, 8);
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(8, 8, 255, 255, 255, 255), LayerBlendMode::Normal, L"White")); });
    CanvasRenderer::StrokePreview preview;
    preview.layerID = document.layers[0].id;
    preview.width = 8;
    preview.height = 8;
    preview.transform = document.layers[0].transform;
    preview.source = document.layers[0].asset->image;
    const std::array<RasterTile, 1> red{RasterTile{0, 0, Solid(8, 8, 0, 0, 255, 255)}};
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(red)),
           "readback.preview-begins");
    std::vector<std::uint8_t> shown = h.Render(document);
    std::vector<std::uint8_t> flat;
    UINT width = 0;
    UINT height = 0;
    const bool read = SUCCEEDED(h.composition.ComposeReadback(document, flat, width, height));
    Expect(shown.size() == 256 && At(shown, 8, 3, 3, 2) == 255 && At(shown, 8, 3, 3, 0) == 0 && read &&
               At(flat, 8, 3, 3, 0) == 255 && At(flat, 8, 3, 3, 1) == 255 && At(flat, 8, 3, 3, 2) == 255,
           "readback.document-not-preview");
    Raster adopted;
    Expect(SUCCEEDED(h.composition.ComposeReadback(document, adopted)) && adopted.Width() == 8 &&
               adopted.Height() == 8 && adopted.ByteSize() == flat.size() &&
               std::equal(flat.begin(), flat.end(), adopted.Data()) &&
               reinterpret_cast<std::uintptr_t>(adopted.Data()) % PixelBuffer::kAlignment == 0,
           "readback.adopted-document-not-preview");
    CanvasDocument invalid = document;
    invalid.width = kMaxCanvasSide + 1;
    Expect(h.composition.ComposeReadback(invalid, adopted) == E_INVALIDARG && !adopted.HasStorage(),
           "readback.invalid-grid-clears-adopted-output");
    CanvasRenderer unbound;
    adopted = Raster::Make(1, 1, RasterFormat::Bgra8888);
    Expect(unbound.ComposeReadback(document, adopted) == E_UNEXPECTED && !adopted.HasStorage(),
           "readback.missing-device-clears-adopted-output");
    // The display composes its preview again on the next frame.
    std::vector<std::uint8_t> again;
    Expect(SUCCEEDED(h.composition.Compose(document)) && SUCCEEDED(h.composition.Readback(again, width, height)) &&
               At(again, 8, 3, 3, 2) == 255 && At(again, 8, 3, 3, 0) == 0,
           "readback.display-composes-again");
    h.composition.EndStroke();
}

void TestLayerAdjustmentPreview(Harness& h)
{
    // P1.2: the selected layer alone is adjusted, then follows its ordinary clipping, mask, folder, and upper passes.
    CanvasDocument document = DocumentOf(6, 2);
    EditDocumentFixture(
        document, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(Solid(6, 2, 40, 20, 10, 255), LayerBlendMode::Normal, L"Background")); });

    ImageLayer folder = ImageLayer::Blank(L"Masked folder", Vec2{6, 2});
    folder.isGroup = true;
    folder.mask = MaskOf(Gray(6, 2, {255, 255, 160, 255, 200, 255, 255, 192, 128, 255, 160, 255}));
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(folder); });

    std::vector<std::uint8_t> baseBytes(6u * 2u * 4u, 0);
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 1; x <= 4; ++x)
        {
            const std::uint8_t alpha = static_cast<std::uint8_t>(x == 2 ? 160 : (x == 4 ? 192 : 255));
            std::uint8_t* pixel = baseBytes.data() + (static_cast<std::size_t>(y) * 6u + x) * 4u;
            pixel[0] = static_cast<std::uint8_t>(alpha / 3);
            pixel[1] = static_cast<std::uint8_t>(alpha / 2);
            pixel[2] = alpha;
            pixel[3] = alpha;
        }
    }
    const Raster baseRaster = Raster::Make(6, 2, RasterFormat::Bgra8888, baseBytes.data(), baseBytes.size());
    ImageLayer base = LayerFrom(baseRaster, LayerBlendMode::Normal, L"Clipping base");
    base.parentID = folder.id;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(base); });

    const Raster source = Pixels2x2({{20, 40, 200, 255}, {30, 10, 120, 160}, {80, 100, 180, 192}, {10, 60, 100, 128}});
    ImageLayer target = LayerFrom(source, LayerBlendMode::Normal, L"Preview target");
    target.parentID = folder.id;
    target.maskSourceID = base.id;
    target.transform.origin = Vec2{1, 0};
    target.transform.size = Vec2{4, 2}; // translated one pixel and scaled 2x horizontally
    target.transform.sampling = LayerSampling::Nearest;
    target.opacity = 0.75;
    target.mask = MaskOf(Gray(2, 2, {255, 192, 128, 64}));
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(target); });
    const std::size_t targetIndex = document.layers.size() - 1;

    std::vector<std::uint8_t> upperBytes(6u * 2u * 4u, 0);
    const auto setUpper = [&](int x, int y, std::array<std::uint8_t, 4> bgra)
    {
        std::copy(bgra.begin(), bgra.end(),
                  upperBytes.begin() + (static_cast<std::size_t>(y) * 6u + static_cast<std::size_t>(x)) * 4u);
    };
    setUpper(2, 0, {20, 240, 20, 255});
    setUpper(4, 1, {20, 20, 120, 160});
    ImageLayer upper = LayerFrom(Raster::Make(6, 2, RasterFormat::Bgra8888, upperBytes.data(), upperBytes.size()),
                                 LayerBlendMode::Normal, L"Upper layer");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(upper); });

    const std::vector<std::uint8_t> committedBaseline = h.Render(document);
    Expect(committedBaseline.size() == 6u * 2u * 4u, "adjust-preview.committed-baseline-rendered");
    std::vector<std::uint8_t> committedReadback;
    UINT readbackWidth = 0;
    UINT readbackHeight = 0;
    const bool initialReadback =
        SUCCEEDED(h.composition.ComposeReadback(document, committedReadback, readbackWidth, readbackHeight));
    Expect(initialReadback && readbackWidth == 6 && readbackHeight == 2 && committedReadback == committedBaseline,
           "adjust-preview.committed-baseline");

    struct Case
    {
        LayerAdjustment adjustment;
        FilterKind filter;
        int tolerance;
        const char* name;
    };
    LayerAdjustment levels = AdjustmentOf(AdjustmentKind::Levels);
    Adjust::SetLevelsCurrent(levels.levels, LevelRange{8, 1.35, 245, 4, 250});
    LayerAdjustment curves = AdjustmentOf(AdjustmentKind::Curves);
    curves.curves.channels[0] = {CurvePoint{0, 12}, CurvePoint{96, 150}, CurvePoint{255, 240}};
    curves.curves.channels[3] = {CurvePoint{0, 28}, CurvePoint{255, 210}};
    LayerAdjustment exposure = AdjustmentOf(AdjustmentKind::Exposure);
    exposure.exposureSettings = ExposureSettings{0.8, 0.04, 1.35};
    LayerAdjustment gradient = AdjustmentOf(AdjustmentKind::GradientMap);
    gradient.gradientMapSettings =
        GradientMapSettings{AdjustmentColor{0.85, 0.08, 0.18}, AdjustmentColor{0.12, 0.35, 0.95}, false};
    LayerAdjustment hsv = AdjustmentOf(AdjustmentKind::Hsv);
    HueSaturationSettings hsvSettings = Adjust::HsvSettings(72, 28, -8);
    hsvSettings.adjustments[static_cast<std::size_t>(ColorRange::Blues)] = RangeAdjustment{-32, 18, 9};
    hsv.hsvSettings = hsvSettings;
    LayerAdjustment grain = AdjustmentOf(AdjustmentKind::Grain);
    grain.grainSettings = GrainSettings{58, 1.75, 42, 0x51a7u};
    const std::array<Case, 6> cases{{{levels, FilterKind::Levels, 1, "levels"},
                                     {curves, FilterKind::Curves, 1, "curves"},
                                     {exposure, FilterKind::Exposure, 1, "exposure"},
                                     {gradient, FilterKind::GradientMap, 1, "gradient-map"},
                                     {hsv, FilterKind::HueSaturation, 4, "hsv"},
                                     {grain, FilterKind::Grain, 2, "grain"}}};

    const Raster coverage = Gray(2, 2, {255, 128, 0, 192});
    std::optional<CanvasRenderer::AdjustmentPreview> lastPreview;
    std::vector<std::uint8_t> lastExpectedDisplay;
    int lastTolerance = 1;
    for (const Case& test : cases)
    {
        const Raster adjusted = Adjust::RunFilter(test.filter, source, Adjust::AdjustmentSettings(test.adjustment), 1.0,
                                                  test.adjustment.Grain().seed);
        const Raster blended = Adjust::BlendThrough(adjusted, source, coverage);
        const std::shared_ptr<const AdjustmentLookup> lookup = Adjust::BuildAdjustmentLookup(test.adjustment);
        char label[96]{};
        std::snprintf(label, sizeof(label), "adjust-preview.%s.cpu-reference-ready", test.name);
        Expect(adjusted.HasStorage() && blended.HasStorage() && lookup != nullptr, label);
        if (!adjusted.HasStorage() || !blended.HasStorage() || !lookup)
        {
            continue;
        }

        CanvasRenderer::AdjustmentPreview preview;
        preview.layerId = document.layers[targetIndex].id;
        preview.source = source;
        preview.coverage = coverage;
        preview.lookup = lookup;
        preview.previewScale = 1.0;
        lastPreview = preview;
        lastTolerance = test.tolerance;
        h.composition.SetAdjustmentPreview(preview);
        const std::vector<std::uint8_t> shown = h.Render(document);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.cpu-composite", test.name);

        CanvasDocument expectedDocument = document;
        EditDocumentFixture(expectedDocument,
                            [&](DocumentFixture& fixture) { fixture.layers[targetIndex].asset->image = blended; });
        EditDocumentFixture(expectedDocument,
                            [&](DocumentFixture& fixture) { fixture.layers[targetIndex].asset->thumbnail = blended; });
        h.composition.SetAdjustmentPreview(std::nullopt);
        lastExpectedDisplay = h.Render(expectedDocument);
        Expect(PixelsClose(shown, lastExpectedDisplay, test.tolerance, label), label);

        const auto samePixel =
            [](const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right, int x, int y)
        {
            for (int channel = 0; channel < 4; ++channel)
            {
                if (At(left, 6, x, y, channel) != At(right, 6, x, y, channel))
                    return false;
            }
            return true;
        };
        const bool unaffected = samePixel(shown, committedBaseline, 0, 0) &&
                                samePixel(shown, committedBaseline, 5, 1) && samePixel(shown, committedBaseline, 2, 0);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.other-layers-unchanged", test.name);
        Expect(unaffected, label);

        std::vector<std::uint8_t> readback;
        UINT width = 0;
        UINT height = 0;
        h.composition.SetAdjustmentPreview(preview);
        const HRESULT readbackResult = h.composition.ComposeReadback(document, readback, width, height);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.readback-bypasses-preview", test.name);
        Expect(SUCCEEDED(readbackResult) && width == 6 && height == 2 && readback == committedBaseline, label);
    }

    if (lastPreview && !lastExpectedDisplay.empty())
    {
        h.composition.SetAdjustmentPreview(lastPreview);
        const std::vector<std::uint8_t> shown = h.Render(document);
        const std::uint64_t settledCount = h.composition.CompositeCount();
        const bool settled = SUCCEEDED(h.composition.Compose(document)) && h.composition.CompositeCount() == settledCount;
        std::vector<std::uint8_t> settledPixels;
        UINT width = 0;
        UINT height = 0;
        const bool settledReadback = SUCCEEDED(h.composition.Readback(settledPixels, width, height));
        Expect(settled && settledReadback && settledPixels == shown, "adjust-preview.settled-no-extra-composition");

        h.composition.ReleaseDocumentSurfaces();
        Expect(h.composition.CanvasSurfaceBytes() == 0, "adjust-preview.hidden-surfaces-released");
        const std::vector<std::uint8_t> afterHidden = h.Render(document);
        Expect(PixelsClose(afterHidden, lastExpectedDisplay, lastTolerance, "adjust-preview.hidden-rebuild"),
               "adjust-preview.hidden-rebuilds-preview");

        wil::com_ptr_nothrow<ID3D11Device> reboundDevice;
        wil::com_ptr_nothrow<ID3D11DeviceContext> reboundContext;
        const HRESULT deviceResult = MakeWarp(reboundDevice, reboundContext);
        const HRESULT rebindResult = SUCCEEDED(deviceResult)
                                         ? h.composition.CreateDeviceResources(reboundDevice.get(), reboundContext.get())
                                         : deviceResult;
        if (SUCCEEDED(rebindResult))
        {
            h.device = std::move(reboundDevice);
            h.context = std::move(reboundContext);
        }
        const std::vector<std::uint8_t> afterRebind =
            SUCCEEDED(rebindResult) ? h.Render(document) : std::vector<std::uint8_t>{};
        Expect(SUCCEEDED(rebindResult) &&
                   PixelsClose(afterRebind, lastExpectedDisplay, lastTolerance, "adjust-preview.device-rebuild"),
               "adjust-preview.device-rebind-rebuilds-preview");

        h.composition.SetAdjustmentPreview(std::nullopt);
        const std::vector<std::uint8_t> cleared = h.Render(document);
        Expect(cleared == committedBaseline, "adjust-preview.clear-restores-committed-layer");
    }

    // A pure layer keeps its own alpha, including transparent pixels; grain uses layer-center coordinates at scale 2.
    const Raster alphaSource = Pixels2x2({{20, 40, 200, 255}, {30, 10, 120, 160}, {0, 0, 0, 0}, {10, 60, 100, 128}});
    CanvasDocument pureDocument = Document2x2();
    EditDocumentFixture(
        pureDocument, [&](DocumentFixture& fixture)
        { fixture.layers.push_back(LayerFrom(alphaSource, LayerBlendMode::Normal, L"Alpha preview")); });
    const Raster alphaCoverage = Gray(2, 2, {255, 128, 255, 192});
    char label[96]{};
    for (const Case& test : cases)
    {
        const double previewScale = test.filter == FilterKind::Grain ? 0.5 : 1.0;
        const Raster adjusted = Adjust::RunFilter(test.filter, alphaSource, Adjust::AdjustmentSettings(test.adjustment),
                                                  previewScale, test.adjustment.Grain().seed);
        const Raster blended = Adjust::BlendThrough(adjusted, alphaSource, alphaCoverage);
        const std::shared_ptr<const AdjustmentLookup> lookup = Adjust::BuildAdjustmentLookup(test.adjustment);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.alpha-reference-ready", test.name);
        Expect(adjusted.HasStorage() && blended.HasStorage() && lookup != nullptr, label);
        if (!adjusted.HasStorage() || !blended.HasStorage() || !lookup)
        {
            continue;
        }

        CanvasRenderer::AdjustmentPreview preview;
        preview.layerId = pureDocument.layers[0].id;
        preview.source = alphaSource;
        preview.coverage = alphaCoverage;
        preview.lookup = lookup;
        preview.previewScale = previewScale;
        h.composition.SetAdjustmentPreview(preview);
        const std::vector<std::uint8_t> shown = h.Render(pureDocument);

        CanvasDocument expectedDocument = pureDocument;
        EditDocumentFixture(expectedDocument,
                            [&](DocumentFixture& fixture) { fixture.layers[0].asset->image = blended; });
        EditDocumentFixture(expectedDocument,
                            [&](DocumentFixture& fixture) { fixture.layers[0].asset->thumbnail = blended; });
        h.composition.SetAdjustmentPreview(std::nullopt);
        const std::vector<std::uint8_t> expected = h.Render(expectedDocument);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.alpha-only-composite", test.name);
        Expect(PixelsClose(shown, expected, test.tolerance, label), label);
        std::snprintf(label, sizeof(label), "adjust-preview.%s.alpha-exact", test.name);
        Expect(AlphasClose(shown, {255, 160, 0, 128}, 0), label);

        if (test.filter == FilterKind::Grain)
        {
            const Raster wrongScale =
                Adjust::RunFilter(test.filter, alphaSource, Adjust::AdjustmentSettings(test.adjustment), 1.0,
                                  test.adjustment.Grain().seed);
            Expect(wrongScale.HasStorage() && RasterBytes(wrongScale) != RasterBytes(adjusted),
                   "adjust-preview.grain-scale-changes-layer-coordinate-key");
        }
    }
    h.composition.SetAdjustmentPreview(std::nullopt);
}

void TestStrokePreviewMask(Harness& h)
{
    // RF-406: a preview whose grid grew past the layer keeps an unplaced mask on the layer's own pixels and reveals
    // the paint beyond them, as the commit grows the mask with 255. A 4 x 4 red layer at the top-left of an 8 x 8
    // canvas, its mask revealing the left two columns; the stroke paints the whole grown 8 x 8 grid red.
    CanvasDocument document = DocumentOf(8, 8);
    ImageLayer layer = LayerFrom(Solid(4, 4, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red");
    layer.mask = MaskOf(Gray(4, 4, {255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0}));
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(layer); });
    CanvasRenderer::StrokePreview preview;
    preview.layerID = layer.id;
    preview.width = 8;
    preview.height = 8;
    preview.transform.size = Vec2{8, 8};
    preview.transform.sampling = LayerSampling::Nearest;
    preview.source = layer.asset->image;
    const std::array<RasterTile, 1> paint{RasterTile{0, 0, Solid(8, 8, 0, 0, 255, 255)}};
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(paint)),
           "strokemask.preview-begins");
    const std::vector<std::uint8_t> pixels = h.Render(document);
    // Inside the layer's old pixels the mask decides (column 1 shown, column 2 hidden); beyond them paint shows.
    const bool placed = At(pixels, 8, 1, 1, 3) == 255 && At(pixels, 8, 2, 1, 3) == 0 && At(pixels, 8, 5, 1, 3) == 255 &&
                        At(pixels, 8, 6, 6, 3) == 255;
    if (!placed)
    {
        PrintPixels("preview mask", pixels);
    }
    Expect(placed, "strokemask.unplaced-mask-stays-on-layer");
    h.composition.EndStroke();
    // A uniform 1 x 1 mask stays uniform over the grown grid.
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(Gray(1, 1, {0})); });
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(paint)),
           "strokemask.uniform-begins");
    const std::vector<std::uint8_t> hidden = h.Render(document);
    Expect(At(hidden, 8, 1, 1, 3) == 0 && At(hidden, 8, 6, 6, 3) == 0, "strokemask.uniform-mask-stays-uniform");
    h.composition.EndStroke();
}

void TestStrokeAdoption(Harness& h)
{
    // RF-407: the working texture becomes a committed raster's texture only for its own layer and target.
    CanvasDocument document = DocumentOf(4, 4);
    ImageLayer first = LayerFrom(Solid(4, 4, 0, 0, 255, 255), LayerBlendMode::Normal, L"First");
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(first); });
    CanvasRenderer::StrokePreview preview;
    preview.layerID = first.id;
    preview.width = 4;
    preview.height = 4;
    preview.transform = first.transform;
    const std::array<RasterTile, 1> red{RasterTile{0, 0, Solid(4, 4, 0, 0, 255, 255)}};
    // Another layer's commit: blue pixels installed on `second`.
    const Raster blue = Solid(4, 4, 255, 0, 0, 255);
    ImageLayer second = LayerFrom(blue, LayerBlendMode::Normal, L"Second");
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(red)),
           "adopt.preview-begins");
    const std::uint64_t adoptions = h.composition.AdoptionCount();
    h.composition.EndStroke(&blue, 0, 0, &second.id);
    Expect(h.composition.AdoptionCount() == adoptions, "adopt.other-layer-not-adopted");
    CanvasDocument other = DocumentOf(4, 4);
    EditDocumentFixture(other, [&](DocumentFixture& fixture) { fixture.layers.push_back(second); });
    std::vector<std::uint8_t> pixels = h.Render(other);
    Expect(At(pixels, 4, 1, 1, 0) == 255 && At(pixels, 4, 1, 1, 2) == 0, "adopt.other-layer-uploads-its-own");
    // A mask commit (Gray8) never takes an image preview's texture.
    const Raster reveal = Gray(4, 4, {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255});
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(red)),
           "adopt.preview-again");
    h.composition.EndStroke(&reveal, 0, 0, &first.id);
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask = MaskOf(reveal); });
    pixels = h.Render(document);
    Expect(At(pixels, 4, 2, 2, 3) == 255 && At(pixels, 4, 2, 2, 2) == 255, "adopt.mask-commit-uploads-its-own");
    // Its own commit is adopted: the cache holds the working texture for the committed raster, so the canvas shows
    // the texture's red even though this raster's own bytes are blue (proof that nothing was uploaded).
    const Raster committed =
        Raster::Replacing(Raster{}, 0, 0, {RasterTile{0, 0, Solid(4, 4, 255, 0, 0, 255)}}, 0, 0, 4, 4, 0);
    Expect(SUCCEEDED(h.composition.BeginStroke(preview)) && SUCCEEDED(h.composition.UpdateStrokeTiles(red)),
           "adopt.own-preview");
    h.composition.EndStroke(&committed, 0, 0, &first.id);
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers[0].mask.reset(); });
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers[0].asset = RasterAsset{committed, committed, L"First"}; });
    pixels = h.Render(document);
    Expect(At(pixels, 4, 2, 2, 2) == 255 && At(pixels, 4, 2, 2, 0) == 0 && !committed.IsMaterialized(),
           "adopt.own-commit-draws-the-texture");
}

int wmain(int argc, wchar_t** argv)
{
    // Before anything can fail a check: a CRT report ends the run with exit code 3 and no dialog (RF-702,
    // Tests/Support/FailureReports.h).
    RedPrismTestFailureReports::RouteAwayFromDialogs();
    if (argc == 2 && std::wstring_view{argv[1]} == L"--failure-report-self-test")
    {
        return RedPrismTestFailureReports::RunSelfTest();
    }
    TestViewport();
    gFailures += RunGpuCapabilityTests();
    gFailures += RunGpuWorkTests();
    gFailures += RunGpuBufferReadbackTests();
    gFailures += RunBrushEpochTests();
    gFailures += RunDetachedEpochTests();
    gFailures += RunGpuCompletionBarrierTests();
    gFailures += RunCapturedCompletionContextTests();
    gFailures += RunGpuCommandStreamTests();
    gFailures += RedPrism::CompositionTests::RunGpuResidencyGrowthTests();
    gFailures += RedPrism::CompositionTests::RunCommandStreamRestartTests();
    gFailures += RedPrism::CompositionTests::RunUtilityAdmissionTests();
    gFailures += RedPrism::CompositionTests::RunRecordedUtilityRegionTests();
    gFailures += RunGpuCommandPumpTests();
    gFailures += RunUtilityOwnerServiceRoutingTests();
    gFailures += RedPrism::CompositionTests::RunUtilityJoinedCloseTests();
    gFailures += RedPrism::CompositionTests::RunUtilityCurrentContextTests();
    gFailures += RunUtilitySourceCapacityTests();
    gFailures += RedPrism::CompositionTests::RunCopyCompletionRetirementTests();
    gFailures += RunRecordedDisplayParentTests();
    gFailures += RedPrism::CompositionTests::RunRecordedParentPixelTests();
    gFailures += RedPrism::CompositionTests::RunRecordedParentCancellationTests();
    gFailures += RedPrism::CompositionTests::RunRecordedParentSamplerTests();
    gFailures += RedPrism::CompositionTests::RunRestartDeviceOwnerControlsTests();
    gFailures += RunRecordedAdjustmentParentTests();
    gFailures += RedPrism::CompositionTests::RunAdjustmentDisplayAdmissionTests();
    gFailures += RunCommittedDisplayControllerTests();
    gFailures += RunPublicationParentTests();
    gFailures += RunCommittedDisplayVisibilityTests();
    gFailures += RunCommittedDisplayDeviceRebindTests();
    gFailures += RunRecordedBrushCoverageTests();
    gFailures += RunWarpReplayComputeTests();
    gFailures += RunRecordedWarpPreviewParentTests();
    gFailures += RunEmptyJoinedShutdownTests();
    gFailures += RunVideoMemorySnapshotTests();
    gFailures += RunBrushDensityClearTests();
    gFailures += RunGpuBufferSlotsTests();
    gFailures += RunGpuUploadAdmissionTests();
    gFailures += RunGpuUploadTransportTests();
    gFailures += RunGpuUploadInitializationTests();
    gFailures += RunGpuUploadFaultTests();
    gFailures += RunGpuRasterUploadJobTests();
    gFailures += RunGpuResidencyBudgetTests();
    gFailures += RunGpuRasterTextureCreationTests();
    gFailures += RunGpuCreatedUploadJobTests();
    gFailures += RunGpuMipChainJobTests();
    gFailures += RunCompositionStageTimingTests();
    gFailures += RunDocumentReadbackTests();
    gFailures += RedPrism::CompositionTests::RunChargedRasterTests();
    TestFrameLoop();
    Harness harness;
    if (harness.Ready())
    {
        TestBlendModes(harness);
        TestOrderAndBlankLayers(harness);
        TestRasterMasks(harness);
        TestAffineSamplingFootprint(harness);
        TestLiveMasks(harness);
        TestClippingStack(harness);
        TestFolderMasks(harness);
        TestTransformRendering(harness);
        TestResidencyAndCaching(harness);
        TestSparseUpload(harness);
        TestStrokePreview(harness);
        TestGpuBrushCoverage(harness);
        TestAdjustmentLayers(harness);
        TestLayerAdjustmentPreview(harness);
        TestHslBlends(harness);
        TestBlit(harness);
        TestOversize(harness);
        TestReadbackIgnoresPreview(harness);
        TestStrokePreviewMask(harness);
        TestStrokeAdoption(harness);
    }
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All composition tests passed.\n");
    return 0;
}

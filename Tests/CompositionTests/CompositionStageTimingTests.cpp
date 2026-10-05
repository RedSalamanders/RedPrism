#include "CompositionStageTimingTests.h"

#include "../../Common/Document.h"
#include "../../Common/UiTiming.h"
#include "../Support/DocumentFixture.h"
#include "CanvasRenderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <d3d11_1.h>
#include <memory>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

[[nodiscard]] std::uint64_t Samples(const UiTimingStats& stats, UiTimingKind kind) noexcept
{
    return stats.durations[static_cast<std::size_t>(kind)].SampleCount();
}

[[nodiscard]] bool MakeWarp(wil::com_ptr_nothrow<ID3D11Device>& device,
                            wil::com_ptr_nothrow<ID3D11DeviceContext>& context) noexcept
{
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL used = D3D_FEATURE_LEVEL_11_0;
    return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                       1, D3D11_SDK_VERSION, device.put(), &used, context.put()));
}

[[nodiscard]] CanvasDocument LiteralDocument(const Raster& raster)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 2;

    ImageLayer layer = ImageLayer::Blank(L"Timing fixture", Vec2{2, 2});
    layer.asset = RasterAsset{raster, raster, L"Timing fixture"};
    layer.transform.sampling = LayerSampling::Nearest;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

void ExpectNoSourceResourceSamples(const UiTimingStats& stats, const char* textureName, const char* viewName,
                                   const char* mipName, const char* uploadName) noexcept
{
    Expect(Samples(stats, UiTimingKind::TextureCreate) == 0, textureName);
    Expect(Samples(stats, UiTimingKind::ShaderViewCreate) == 0, viewName);
    Expect(Samples(stats, UiTimingKind::MipGeneration) == 0, mipName);
    Expect(Samples(stats, UiTimingKind::RasterUpload) == 0, uploadName);
}
} // namespace

int RunCompositionStageTimingTests()
{
    gFailures = 0;

    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    if (!MakeWarp(device, context))
    {
        Expect(false, "stage-timing.warp-device");
        return gFailures;
    }

    CanvasRenderer composition;
    if (FAILED(composition.CreateDeviceResources(device.get(), context.get())))
    {
        Expect(false, "stage-timing.device-resources");
        return gFailures;
    }

    constexpr std::array<std::uint8_t, 16> literalBgra{
        17, 83, 201, 255, 39, 149, 71, 255, 223, 31, 107, 255, 61, 191, 13, 255,
    };
    const Raster source = Raster::Make(2, 2, RasterFormat::Bgra8888, literalBgra.data(), literalBgra.size());
    const CanvasDocument document = LiteralDocument(source);

    // Keep capture storage off the measured paths. Each capture observes only the operation named by its scope.
    const auto firstComposeStats = std::make_unique<UiTimingStats>();
    const auto readbackStats = std::make_unique<UiTimingStats>();
    const auto invalidatedComposeStats = std::make_unique<UiTimingStats>();
    const auto settledComposeStats = std::make_unique<UiTimingStats>();

    const std::uint64_t beforeFirstCompose = composition.CompositeCount();
    HRESULT composeResult = E_FAIL;
    {
        UiTimingCapture capture(*firstComposeStats);
        composeResult = composition.Compose(document);
    }
    Expect(SUCCEEDED(composeResult), "stage-timing.first-compose-succeeds");
    Expect(composition.CompositeCount() == beforeFirstCompose + 1, "stage-timing.first-compose-recorded");
    Expect(Samples(*firstComposeStats, UiTimingKind::TextureCreate) != 0, "stage-timing.first-compose-texture-create");
    Expect(Samples(*firstComposeStats, UiTimingKind::ShaderViewCreate) != 0,
           "stage-timing.first-compose-shader-view-create");
    Expect(Samples(*firstComposeStats, UiTimingKind::MipGeneration) != 0, "stage-timing.first-compose-mip-generation");
    Expect(Samples(*firstComposeStats, UiTimingKind::RasterUpload) != 0, "stage-timing.first-compose-raster-upload");
    Expect(Samples(*firstComposeStats, UiTimingKind::CompositionRecord) != 0,
           "stage-timing.first-compose-record-stage");

    const std::uint64_t createSamplesBeforeReadback = Samples(*firstComposeStats, UiTimingKind::TextureCreate);
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    HRESULT readbackResult = E_FAIL;
    {
        UiTimingCapture capture(*readbackStats);
        readbackResult = composition.Readback(pixels, width, height);
    }
    Expect(SUCCEEDED(readbackResult) && width == 2 && height == 2, "stage-timing.readback-dimensions");
    Expect(pixels.size() == literalBgra.size() &&
               std::equal(pixels.begin(), pixels.end(), literalBgra.begin(), literalBgra.end()),
           "stage-timing.readback-exact-premultiplied-bgra");
    Expect(Samples(*firstComposeStats, UiTimingKind::TextureCreate) == createSamplesBeforeReadback,
           "stage-timing.readback-capture-is-separate");
    Expect(Samples(*readbackStats, UiTimingKind::TextureCreate) != 0, "stage-timing.readback-staging-create-isolated");

    composition.InvalidateDocument();
    const std::uint64_t beforeInvalidatedCompose = composition.CompositeCount();
    {
        UiTimingCapture capture(*invalidatedComposeStats);
        composeResult = composition.Compose(document);
    }
    Expect(SUCCEEDED(composeResult) && composition.CompositeCount() == beforeInvalidatedCompose + 1,
           "stage-timing.invalidated-recompose-renders-once");
    ExpectNoSourceResourceSamples(*invalidatedComposeStats, "stage-timing.recompose-no-texture-create",
                                  "stage-timing.recompose-no-shader-view-create", "stage-timing.recompose-no-mip-work",
                                  "stage-timing.recompose-no-source-upload-loop");
    Expect(Samples(*invalidatedComposeStats, UiTimingKind::CompositionRecord) != 0,
           "stage-timing.invalidated-recompose-record-stage");

    const std::uint64_t beforeSettledCompose = composition.CompositeCount();
    {
        UiTimingCapture capture(*settledComposeStats);
        composeResult = composition.Compose(document);
    }
    Expect(SUCCEEDED(composeResult) && composition.CompositeCount() == beforeSettledCompose,
           "stage-timing.settled-compose-does-not-render");
    ExpectNoSourceResourceSamples(*settledComposeStats, "stage-timing.settled-no-texture-create",
                                  "stage-timing.settled-no-shader-view-create", "stage-timing.settled-no-mip-work",
                                  "stage-timing.settled-no-source-upload-loop");
    Expect(Samples(*settledComposeStats, UiTimingKind::CompositionRecord) == 0,
           "stage-timing.settled-compose-no-record-stage");

    return gFailures;
}

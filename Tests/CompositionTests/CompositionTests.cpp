#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "Document.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d3d11_1.h>
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

[[nodiscard]] ImageLayer LayerFrom(const Raster& raster, LayerBlendMode mode, const wchar_t* name)
{
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(raster.Width()), static_cast<double>(raster.Height())});
    layer.asset = RasterAsset{raster, raster, name};
    layer.blendMode = mode;
    return layer;
}

[[nodiscard]] bool ChannelClose(int actual, int expected, int slop) noexcept
{
    return std::abs(actual - expected) <= slop;
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
}

HRESULT MakeWarp(wil::com_ptr_nothrow<ID3D11Device>& device,
                 wil::com_ptr_nothrow<ID3D11DeviceContext>& context) noexcept
{
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL used = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, device.put(), &used, context.put());
}

void TestCompose()
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    if (FAILED(MakeWarp(device, context)))
    {
        Expect(false, "compose.warp-device");
        return;
    }
    CanvasRenderer composition;
    if (FAILED(composition.CreateDeviceResources(device.get(), context.get())))
    {
        Expect(false, "compose.pipeline");
        return;
    }

    CanvasDocument whiteOver{};
    whiteOver.width = 1;
    whiteOver.height = 1;
    whiteOver.layers.push_back(LayerFrom(Solid(1, 1, 255, 255, 255, 255), LayerBlendMode::Normal, L"White"));
    whiteOver.layers.push_back(LayerFrom(Solid(1, 1, 0, 0, 255, 255), LayerBlendMode::Multiply, L"Red"));
    Expect(SUCCEEDED(composition.Compose(whiteOver)), "compose.multiply-run");
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    Expect(SUCCEEDED(composition.Readback(pixels, width, height)) && width == 1 && height == 1 && pixels.size() == 4,
           "compose.multiply-readback");
    if (pixels.size() == 4)
    {
        Expect(ChannelClose(pixels[0], 0, 2) && ChannelClose(pixels[1], 0, 2) && ChannelClose(pixels[2], 255, 2) &&
                   ChannelClose(pixels[3], 255, 2),
               "compose.multiply-golden");
    }

    CanvasDocument dodge{};
    dodge.width = 1;
    dodge.height = 1;
    dodge.layers.push_back(LayerFrom(Solid(1, 1, 128, 128, 128, 255), LayerBlendMode::Normal, L"Gray"));
    dodge.layers.push_back(LayerFrom(Solid(1, 1, 0, 0, 255, 255), LayerBlendMode::ColorDodge, L"Dodge"));
    Expect(SUCCEEDED(composition.Compose(dodge)), "compose.dodge-run");
    Expect(SUCCEEDED(composition.Readback(pixels, width, height)) && pixels.size() == 4, "compose.dodge-readback");
    if (pixels.size() == 4)
    {
        Expect(ChannelClose(pixels[0], 128, 3) && ChannelClose(pixels[1], 128, 3) && ChannelClose(pixels[2], 255, 3) &&
                   ChannelClose(pixels[3], 255, 3),
               "compose.dodge-golden");
    }

    composition.ReleaseDeviceResources();
    Expect(composition.CanvasSurfaceBytes() == 0, "compose.hidden-surfaces");
    Expect(SUCCEEDED(composition.CreateDeviceResources(device.get(), context.get())), "compose.device-loss-recreate");
    Expect(SUCCEEDED(composition.Compose(dodge)), "compose.after-recreate");

    CanvasDocument masked{};
    masked.width = 1;
    masked.height = 1;
    ImageLayer maskedLayer = LayerFrom(Solid(1, 1, 0, 0, 255, 255), LayerBlendMode::Normal, L"Red");
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8);
    maskedLayer.mask = LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, true};
    masked.layers.push_back(std::move(maskedLayer));
    Expect(SUCCEEDED(composition.Compose(masked)), "compose.mask-run");
}
} // namespace

int wmain()
{
    TestViewport();
    TestCompose();
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All composition tests passed.\n");
    return 0;
}

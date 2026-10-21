#include "Composition.Tests.SelectionOutlineGpu.h"

#include "CanvasRenderer.h"

#include <d2d1.h>
#include <objbase.h>
#include <wincodec.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <vector>

namespace
{
unsigned int passed = 0;
unsigned int failed = 0;
using Pixel = std::array<std::uint8_t, 4>;

void Expect(bool value, const char* scenario, const char* control) noexcept
{
    std::printf("%s selection-outline-gpu.%s.%s\n", value ? "PASS" : "FAIL", scenario, control);
    value ? ++passed : ++failed;
}

struct Mask final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
};

bool CreateMask(ID3D11Device* device, unsigned int width, unsigned int height, std::span<const std::uint8_t> bytes,
                Mask& output) noexcept
{
    if (bytes.size() != static_cast<std::size_t>(width) * height)
        return false;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{bytes.data(), width, 0};
    return SUCCEEDED(device->CreateTexture2D(&description, &data, output.texture.put())) &&
           SUCCEEDED(device->CreateShaderResourceView(output.texture.get(), nullptr, output.view.put()));
}

struct Target final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
};

bool CreateTarget(ID3D11Device* device, unsigned int width, unsigned int height, Pixel base, Target& output)
{
    std::vector<Pixel> pixels(static_cast<std::size_t>(width) * height, base);
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4u, 0};
    return SUCCEEDED(device->CreateTexture2D(&description, &data, output.texture.put())) &&
           SUCCEEDED(device->CreateRenderTargetView(output.texture.get(), nullptr, output.view.put()));
}

// Independent integer source-over reference: white with solid coverage, then black with dash coverage.
// Both masks are UNORM8; a single final rounding replaces intermediate real-number conversions.
Pixel Reference(Pixel base, unsigned int solid, unsigned int dash) noexcept
{
    constexpr unsigned int denominator = 255u * 255u;
    const auto remaining = (255u - solid) * (255u - dash);
    Pixel result{};
    for (std::size_t channel = 0; channel < 3; ++channel)
        result[channel] = static_cast<std::uint8_t>(
            (255u * solid * (255u - dash) + base[channel] * remaining + denominator / 2u) / denominator);
    result[3] = static_cast<std::uint8_t>(
        (255u * (255u * dash + solid * (255u - dash)) + base[3] * remaining + denominator / 2u) / denominator);
    return result;
}

Pixel ReferenceClipped(Pixel base, unsigned int solid, unsigned int dash, unsigned int numerator) noexcept
{
    // Independent rational area in sixteenths. Clip the complete composite, not individual coverages.
    constexpr std::uint64_t denominator = 255u * 255u * 16u;
    const std::uint64_t alpha = (255u * dash + solid * (255u - dash)) * numerator;
    const std::uint64_t white = static_cast<std::uint64_t>(solid) * (255u - dash) * numerator;
    Pixel result{};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        const std::uint64_t component = channel == 3 ? alpha : white;
        result[channel] = static_cast<std::uint8_t>(
            (255u * component + base[channel] * (denominator - alpha) + denominator / 2u) / denominator);
    }
    return result;
}
unsigned int ClipNumerator(const std::array<int, 4>& quarters, int x, int y) noexcept
{
    const int horizontal = std::max(0, std::min((x + 1) * 4, quarters[2]) - std::max(x * 4, quarters[0]));
    const int vertical = std::max(0, std::min((y + 1) * 4, quarters[3]) - std::max(y * 4, quarters[1]));
    return static_cast<unsigned int>(horizontal * vertical);
}
bool Direct2DClipOnceReference()
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment))
        return false;
    const auto uninitialize = wil::scope_exit([]() noexcept { CoUninitialize(); });
    wil::com_ptr_nothrow<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put())))
        return false;
    wil::com_ptr_nothrow<IWICImagingFactory> imaging;
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    wil::com_ptr_nothrow<ID2D1RenderTarget> target;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> white, black;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(imaging.put()))) ||
        FAILED(imaging->CreateBitmap(4, 4, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put())) ||
        FAILED(factory->CreateWicBitmapRenderTarget(
            bitmap.get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                         96, 96),
            target.put())) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), white.put())) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), black.put())))
        return false;
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->PushAxisAlignedClip(D2D1::RectF(0.25f, 0.5f, 3.75f, 3.25f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->FillRectangle(D2D1::RectF(0, 0, 4, 4), white.get());
    target->FillRectangle(D2D1::RectF(0, 0, 4, 4), black.get());
    target->PopAxisAlignedClip();
    if (FAILED(target->EndDraw()))
        return false;
    WICRect rect{0, 0, 4, 4};
    wil::com_ptr_nothrow<IWICBitmapLock> lock;
    UINT stride = 0, size = 0;
    BYTE* pixels = nullptr;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, lock.put())) || FAILED(lock->GetStride(&stride)) ||
        FAILED(lock->GetDataPointer(&size, &pixels)) || !pixels || stride < 16 || size < stride * 3 + 16)
        return false;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const auto expected = ReferenceClipped({0, 0, 0, 0}, 255, 255, ClipNumerator({1, 2, 15, 13}, x, y));
            for (std::size_t channel = 0; channel < 4; ++channel)
                if (std::abs(static_cast<int>(pixels[static_cast<std::size_t>(y) * stride + x * 4 + channel]) -
                             static_cast<int>(expected[channel])) > 1)
                    return false;
        }
    return true;
}

bool ReadMatches(ID3D11Device* device, ID3D11DeviceContext* context, const Target& target, Pixel base,
                 unsigned int width, unsigned int height, std::span<const std::uint8_t> solid,
                 std::span<const std::uint8_t> dash, int left, int top,
                 const std::array<int, 4>* clipQuarters = nullptr)
{
    D3D11_TEXTURE2D_DESC description{};
    target.texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&description, nullptr, staging.put())))
        return false;
    context->CopyResource(staging.get(), target.texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    // Numeric component fixture only. Production readback continues to require its owner/event/row lease.
    if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { context->Unmap(staging.get(), 0); });
    bool same = mapped.pData && mapped.RowPitch >= width * 4u;
    for (unsigned int y = 0; same && y < height; ++y)
    {
        const auto* row =
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        for (unsigned int x = 0; same && x < width; ++x)
        {
            const int localX = static_cast<int>(x) - left;
            const int localY = static_cast<int>(y) - top;
            Pixel expected = base;
            if (localX >= 0 && localY >= 0 && localX < 256 && localY < 256)
            {
                const auto index = static_cast<std::size_t>(localY) * 256u + static_cast<unsigned int>(localX);
                expected = clipQuarters ? ReferenceClipped(base, solid[index], dash[index],
                                                           ClipNumerator(*clipQuarters, localX, localY))
                                        : Reference(base, solid[index], dash[index]);
            }
            for (std::size_t channel = 0; channel < 4; ++channel)
                same = same && std::abs(static_cast<int>(row[static_cast<std::size_t>(x) * 4u + channel]) -
                                        static_cast<int>(expected[channel])) <= 1;
        }
    }
    return same;
}

bool BorrowedInputsRetired(ID3D11DeviceContext* context) noexcept
{
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> first;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> third;
    wil::com_ptr_nothrow<ID3D11BlendState> blend;
    context->PSGetShaderResources(0, 1, first.put());
    context->PSGetShaderResources(2, 1, third.put());
    context->OMGetBlendState(blend.put(), nullptr, nullptr);
    return !first && !third && !blend;
}
} // namespace

int RunSelectionOutlineGpuTests()
{
    passed = failed = 0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                              &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    Expect(SUCCEEDED(created), "setup", "warp-device");
    if (FAILED(created))
        return static_cast<int>(failed);
    CanvasRenderer composition;
    const HRESULT ready = composition.CreateDeviceResources(device.get(), context.get());
    Expect(SUCCEEDED(ready), "setup", "actual-built-in-shader-and-premultiplied-blend");
    if (FAILED(ready))
        return static_cast<int>(failed);
    std::vector<std::uint8_t> solid(256u * 256u);
    std::vector<std::uint8_t> dash(solid.size());
    for (unsigned int y = 0; y < 256; ++y)
        for (unsigned int x = 0; x < 256; ++x)
        {
            solid[static_cast<std::size_t>(y) * 256u + x] = static_cast<std::uint8_t>(x);
            dash[static_cast<std::size_t>(y) * 256u + x] = static_cast<std::uint8_t>(y);
        }
    Mask solidMask;
    Mask dashMask;
    const bool masks =
        CreateMask(device.get(), 256, 256, solid, solidMask) && CreateMask(device.get(), 256, 256, dash, dashMask);
    Expect(masks, "setup", "immutable-r8-mask-resources");
    if (!masks)
        return static_cast<int>(failed);
    struct Scenario final
    {
        const char* name;
        Pixel base;
        unsigned int width, height;
        int left, top;
    };
    constexpr std::array scenarios{
        Scenario{"all-pairs-translucent", {17, 34, 51, 85}, 256, 256, 0, 0},
        Scenario{"all-pairs-transparent", {0, 0, 0, 0}, 256, 256, 0, 0},
        Scenario{"all-pairs-opaque", {20, 40, 60, 255}, 256, 256, 0, 0},
        Scenario{"positive-clipped-origin", {17, 34, 51, 85}, 9, 8, 3, 2},
        Scenario{"negative-clipped-origin", {17, 34, 51, 85}, 9, 8, -250, -250},
        Scenario{"offscreen-origin", {17, 34, 51, 85}, 9, 8, 20, 20},
    };
    const auto compositeCount = composition.CompositeCount();
    const auto utilityCount = composition.UtilityCompositeCount();
    for (const auto& scenario : scenarios)
    {
        Target target;
        const bool targetReady = CreateTarget(device.get(), scenario.width, scenario.height, scenario.base, target);
        const HRESULT result = targetReady ? composition.DrawSelectionOutlineToTarget(
                                                 target.view.get(), scenario.width, scenario.height,
                                                 solidMask.view.get(), dashMask.view.get(), 256, 256,
                                                 static_cast<float>(scenario.left), static_cast<float>(scenario.top))
                                           : E_FAIL;
        Expect(result == S_OK, scenario.name, "actual-draw");
        Expect(result == S_OK && ReadMatches(device.get(), context.get(), target, scenario.base, scenario.width,
                                             scenario.height, solid, dash, scenario.left, scenario.top),
               scenario.name, "independent-source-over-reference-tolerance-one-unorm");
        Expect(composition.CompositeCount() == compositeCount && composition.UtilityCompositeCount() == utilityCount,
               scenario.name, "no-document-or-utility-composition");
        Expect(BorrowedInputsRetired(context.get()), scenario.name, "mask-srvs-and-blend-unbound");
    }
    // Each phase is a distinct immutable SRV, selected without changing solid coverage or recomposing.
    // This proves phase resource selection; the independent CPU contour suite owns dash geometry semantics.
    for (unsigned int phase = 0; phase < 8; ++phase)
    {
        for (unsigned int y = 0; y < 256; ++y)
            for (unsigned int x = 0; x < 256; ++x)
                dash[static_cast<std::size_t>(y) * 256u + x] = static_cast<std::uint8_t>((y + phase * 31u) & 255u);
        Mask phaseMask;
        Target phaseTarget;
        std::array<char, 32> name{};
        std::snprintf(name.data(), name.size(), "phase-%u", phase);
        const bool resources = CreateMask(device.get(), 256, 256, dash, phaseMask) &&
                               CreateTarget(device.get(), 256, 256, {17, 34, 51, 85}, phaseTarget);
        const HRESULT result =
            resources ? composition.DrawSelectionOutlineToTarget(phaseTarget.view.get(), 256, 256, solidMask.view.get(),
                                                                phaseMask.view.get(), 256, 256, 0, 0)
                      : E_FAIL;
        Expect(result == S_OK, name.data(), "actual-draw");
        Expect(result == S_OK &&
                   ReadMatches(device.get(), context.get(), phaseTarget, {17, 34, 51, 85}, 256, 256, solid, dash, 0, 0),
               name.data(), "independent-source-over-reference-tolerance-one-unorm");
        Expect(composition.CompositeCount() == compositeCount && composition.UtilityCompositeCount() == utilityCount,
               name.data(), "no-document-or-utility-composition");
        Expect(BorrowedInputsRetired(context.get()), name.data(), "mask-srvs-and-blend-unbound");
    }

    // Restore the exhaustive 0..255 dash corpus after the existing phase loop mutates it.
    for (unsigned int y = 0; y < 256; ++y)
        for (unsigned int x = 0; x < 256; ++x)
            dash[static_cast<std::size_t>(y) * 256u + x] = static_cast<std::uint8_t>(y);
    constexpr std::array<std::array<int, 4>, 3> clips{
        {{{1, 2, 1023, 1021}}, {{13, 17, 979, 985}}, {{0, 0, 1024, 1024}}}};
    constexpr std::array<Pixel, 3> bases{{{{0, 0, 0, 0}}, {{17, 34, 51, 85}}, {{20, 40, 60, 255}}}};
    for (std::size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex)
        for (std::size_t baseIndex = 0; baseIndex < bases.size(); ++baseIndex)
        {
            const auto& quarters = clips[clipIndex];
            const std::array<float, 4> clip{quarters[0] / 4.0f, quarters[1] / 4.0f, quarters[2] / 4.0f,
                                            quarters[3] / 4.0f};
            Target clippedTarget;
            const HRESULT result =
                CreateTarget(device.get(), 256, 256, bases[baseIndex], clippedTarget)
                    ? composition.DrawSelectionOutlineToTarget(clippedTarget.view.get(), 256, 256, solidMask.view.get(),
                                                              dashMask.view.get(), 256, 256, 0, 0, &clip)
                    : E_FAIL;
            std::array<char, 32> name{};
            std::snprintf(name.data(), name.size(), "clip-%zu-base-%zu", clipIndex, baseIndex);
            Expect(result == S_OK, name.data(), "actual-draw");
            Expect(result == S_OK && ReadMatches(device.get(), context.get(), clippedTarget, bases[baseIndex], 256, 256,
                                                 solid, dash, 0, 0, &quarters),
                   name.data(), "independent-quarter-pixel-composite-clip-tolerance-one-unorm");
            Expect(composition.CompositeCount() == compositeCount && composition.UtilityCompositeCount() == utilityCount,
                   name.data(), "no-document-or-utility-composition");
            Expect(BorrowedInputsRetired(context.get()), name.data(), "mask-srvs-and-blend-unbound");
        }
    Expect(Direct2DClipOnceReference(), "clip-reference",
           "direct-wic-white-then-black-clip-once-literal-quarter-areas");
    {
        Target guardTarget;
        if (!CreateTarget(device.get(), 8, 8, {0, 0, 0, 0}, guardTarget))
            return 1;
        constexpr std::array<std::array<float, 4>, 4> invalidClips{
            {{{-0.25f, 0, 4, 4}}, {{0, 0, 257, 4}}, {{4, 0, 4, 4}}, {{0, 4, 4, 3}}}};
        for (std::size_t index = 0; index < invalidClips.size(); ++index)
        {
            std::array<char, 32> name{};
            std::snprintf(name.data(), name.size(), "clip-invalid-%zu", index);
            Expect(composition.DrawSelectionOutlineToTarget(guardTarget.view.get(), 8, 8, solidMask.view.get(),
                                                           dashMask.view.get(), 256, 256, 0, 0,
                                                           &invalidClips[index]) == E_INVALIDARG,
                   name.data(), "refuses-before-recording");
        }
    }

    Target target;
    const bool targetReady = CreateTarget(device.get(), 8, 8, {17, 34, 51, 85}, target);
    Expect(targetReady, "guards", "target-ready");
    if (targetReady)
    {
        const auto draw = [&](unsigned int width, unsigned int height, float left, float top) noexcept
        {
            return composition.DrawSelectionOutlineToTarget(target.view.get(), 8, 8, solidMask.view.get(),
                                                           dashMask.view.get(), width, height, left, top);
        };
        Expect(draw(0, 256, 0, 0) == E_INVALIDARG && draw(256, 0, 0, 0) == E_INVALIDARG, "guards", "zero-mask-refuses");
        Expect(draw(16385, 1, 0, 0) == E_INVALIDARG && draw(16384, 16384, 0, 0) == E_INVALIDARG, "guards",
               "side-and-pixel-budget-overflow-refuses");
        Expect(draw(256, 256, 0.5f, 0) == E_INVALIDARG && draw(256, 256, 0, -0.5f) == E_INVALIDARG, "guards",
               "fractional-grid-origin-refuses");
        Expect(draw(256, 256, std::numeric_limits<float>::infinity(), 0) == E_INVALIDARG &&
                   draw(256, 256, 0, std::numeric_limits<float>::quiet_NaN()) == E_INVALIDARG,
               "guards", "non-finite-grid-origin-refuses");
        Expect(composition.DrawSelectionOutlineToTarget(nullptr, 8, 8, solidMask.view.get(), dashMask.view.get(), 256,
                                                       256, 0, 0) == S_FALSE &&
                   composition.DrawSelectionOutlineToTarget(target.view.get(), 8, 8, nullptr, dashMask.view.get(), 256,
                                                           256, 0, 0) == S_FALSE &&
                   composition.DrawSelectionOutlineToTarget(target.view.get(), 8, 8, solidMask.view.get(), nullptr, 256,
                                                           256, 0, 0) == S_FALSE,
               "guards", "incomplete-input-is-noop");
        Expect(composition.DrawSelectionOutlineToTarget(target.view.get(), 16385, 8, solidMask.view.get(),
                                                       dashMask.view.get(), 256, 256, 0, 0) == E_INVALIDARG &&
                   composition.DrawSelectionOutlineToTarget(target.view.get(), 8, 16385, solidMask.view.get(),
                                                           dashMask.view.get(), 256, 256, 0, 0) == E_INVALIDARG,
               "guards", "oversized-target-grid-refuses");
        Expect(composition.DrawSelectionOutlineToTarget(target.view.get(), 0, 8, solidMask.view.get(),
                                                       dashMask.view.get(), 256, 256, 0, 0) == S_FALSE,
               "guards", "empty-target-grid-is-noop");
        composition.ReleaseDeviceResources();
        Expect(draw(256, 256, 0, 0) == S_FALSE, "guards", "released-device-is-noop");
        Expect(BorrowedInputsRetired(context.get()), "guards", "no-borrowed-mask-or-blend-survives");
    }
    context->ClearState();
    std::printf("%s selection-outline-gpu.summary passed=%u failed=%u controls=%u\n", failed == 0 ? "PASS" : "FAIL",
                passed, failed, passed + failed);
    return static_cast<int>(failed);
}

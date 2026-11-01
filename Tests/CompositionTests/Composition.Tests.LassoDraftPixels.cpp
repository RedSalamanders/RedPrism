#include "Composition.Tests.LassoDraftPixels.h"

#include "CanvasRenderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <d3d11.h>
#include <span>
#include <vector>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // Stored as premultiplied BGRA.
constexpr unsigned int kSourceWidth = 4;
constexpr unsigned int kSourceHeight = 1;
constexpr unsigned int kTargetWidth = 8;
constexpr unsigned int kTargetHeight = 4;
constexpr Pixel kBase{32, 64, 96, 128};
// Handwritten immutable premultiplied BGRA: opaque white, black at alpha 102,
// transparent, and premultiplied gray (B=40, G=80, R=120, A=160).
constexpr std::array<Pixel, kSourceWidth> kSource{
    {Pixel{255, 255, 255, 255}, Pixel{0, 0, 0, 102}, Pixel{0, 0, 0, 0}, Pixel{40, 80, 120, 160}}};
unsigned int gChecks = 0;
unsigned int gFailures = 0;

void Expect(bool value, const char* caseName) noexcept
{
    ++gChecks;
    gFailures += value ? 0u : 1u;
    std::printf("%s lasso-draft-pixels.%s\n", value ? "PASS" : "FAIL", caseName);
}

struct ColorInput final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
};
struct Target final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
};
struct Mask final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
};

bool CreateColorInput(ID3D11Device* device, ColorInput& output) noexcept
{
    D3D11_TEXTURE2D_DESC description{};
    description.Width = kSourceWidth;
    description.Height = kSourceHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{kSource.data(), kSourceWidth * 4u, 0};
    return SUCCEEDED(device->CreateTexture2D(&description, &data, output.texture.put())) &&
           SUCCEEDED(device->CreateShaderResourceView(output.texture.get(), nullptr, output.view.put()));
}

bool CreateMask(ID3D11Device* device, const std::array<std::uint8_t, kSourceWidth>& coverage, Mask& output) noexcept
{
    D3D11_TEXTURE2D_DESC description{};
    description.Width = kSourceWidth;
    description.Height = 1;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{coverage.data(), kSourceWidth, 0};
    return SUCCEEDED(device->CreateTexture2D(&description, &data, output.texture.put())) &&
           SUCCEEDED(device->CreateShaderResourceView(output.texture.get(), nullptr, output.view.put()));
}

bool CreateTarget(ID3D11Device* device, Target& output) noexcept
{
    std::array<Pixel, kTargetWidth * kTargetHeight> pixels{};
    pixels.fill(kBase);
    D3D11_TEXTURE2D_DESC description{};
    description.Width = kTargetWidth;
    description.Height = kTargetHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), kTargetWidth * 4u, 0};
    return SUCCEEDED(device->CreateTexture2D(&description, &data, output.texture.put())) &&
           SUCCEEDED(device->CreateRenderTargetView(output.texture.get(), nullptr, output.view.put()));
}

bool ReadTarget(ID3D11Device* device, ID3D11DeviceContext* context, const Target& target,
                std::array<Pixel, kTargetWidth * kTargetHeight>& output) noexcept
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
    if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { context->Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < kTargetWidth * 4u)
        return false;
    for (unsigned int y = 0; y < kTargetHeight; ++y)
    {
        const auto* row =
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        for (unsigned int x = 0; x < kTargetWidth; ++x)
            for (std::size_t channel = 0; channel < 4; ++channel)
                output[static_cast<std::size_t>(y) * kTargetWidth + x][channel] = row[x * 4u + channel];
    }
    return true;
}

// Integer source-over for premultiplied source bytes. For fractional clip, keep
// the clip rational through source-over and round once, matching the independent
// area model rather than quantizing a clipped source intermediate.
Pixel ReferenceOver(Pixel destination, Pixel source, unsigned int area16 = 16) noexcept
{
    constexpr std::uint64_t denominator = 255u * 16u;
    Pixel result{};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        const std::uint64_t numerator = static_cast<std::uint64_t>(source[channel]) * area16 * 255u +
                                        static_cast<std::uint64_t>(destination[channel]) *
                                            (denominator - static_cast<std::uint64_t>(source[3]) * area16);
        result[channel] = static_cast<std::uint8_t>((numerator + denominator / 2u) / denominator);
    }
    return result;
}

int ClipArea16(const std::array<int, 4>& quarters, int x, int y) noexcept
{
    const int horizontal = (std::max)(0, (std::min)((x + 1) * 4, quarters[2]) - (std::max)(x * 4, quarters[0]));
    const int vertical = (std::max)(0, (std::min)((y + 1) * 4, quarters[3]) - (std::max)(y * 4, quarters[1]));
    return horizontal * vertical;
}

bool Matches(const std::array<Pixel, kTargetWidth * kTargetHeight>& actual, int left, int top,
             const std::array<int, 4>* clipQuarters, bool followedBySelection = false) noexcept
{
    for (unsigned int y = 0; y < kTargetHeight; ++y)
        for (unsigned int x = 0; x < kTargetWidth; ++x)
        {
            Pixel expected = kBase;
            const int localX = static_cast<int>(x) - left;
            const int localY = static_cast<int>(y) - top;
            if (localX >= 0 && localX < static_cast<int>(kSourceWidth) && localY == 0)
            {
                const unsigned int area =
                    clipQuarters ? static_cast<unsigned int>(ClipArea16(*clipQuarters, localX, localY)) : 16u;
                expected = ReferenceOver(expected, kSource[static_cast<std::size_t>(localX)], area);
                if (followedBySelection && localX == 3)
                {
                    // Existing outline pass: solid=128, dash=0 -> premultiplied white at 128/255.
                    const Pixel selection{128, 128, 128, 128};
                    expected = ReferenceOver(expected, selection);
                }
            }
            const auto& got = actual[static_cast<std::size_t>(y) * kTargetWidth + x];
            for (std::size_t channel = 0; channel < 4; ++channel)
                if (std::abs(static_cast<int>(got[channel]) - static_cast<int>(expected[channel])) > 1)
                {
                    std::printf("draft pixel mismatch x=%u y=%u channel=%zu actual=%u expected=%u\n", x, y, channel,
                                static_cast<unsigned int>(got[channel]), static_cast<unsigned int>(expected[channel]));
                    return false;
                }
        }
    return true;
}

bool InputsAndBlendReset(ID3D11DeviceContext* context) noexcept
{
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> first;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> third;
    wil::com_ptr_nothrow<ID3D11BlendState> blend;
    context->PSGetShaderResources(0, 1, first.put());
    context->PSGetShaderResources(2, 1, third.put());
    context->OMGetBlendState(blend.put(), nullptr, nullptr);
    return !first && !third && !blend;
}

bool DrawAndMatch(CanvasRenderer& composition, ID3D11Device* device, ID3D11DeviceContext* context,
                  const ColorInput& color, int left, int top, const std::array<int, 4>* clip,
                  bool overlay = false) noexcept
{
    Target target;
    if (!CreateTarget(device, target))
        return false;
    std::array<float, 4> clipFloat{};
    const std::array<float, 4>* clipPointer = nullptr;
    if (clip)
    {
        for (std::size_t index = 0; index < 4; ++index)
            clipFloat[index] = static_cast<float>((*clip)[index]) / 4.0f;
        clipPointer = &clipFloat;
    }
    const HRESULT drawn = composition.DrawLassoDraftToTarget(
        target.view.get(), kTargetWidth, kTargetHeight, color.view.get(), kSourceWidth, kSourceHeight,
        static_cast<float>(left), static_cast<float>(top), clipPointer);
    if (drawn != S_OK)
        return false;
    if (overlay)
    {
        Mask solid;
        Mask dash;
        constexpr std::array<std::uint8_t, kSourceWidth> solidBytes{0, 0, 0, 128};
        constexpr std::array<std::uint8_t, kSourceWidth> dashBytes{};
        if (!CreateMask(device, solidBytes, solid) || !CreateMask(device, dashBytes, dash) ||
            composition.DrawSelectionOutlineToTarget(target.view.get(), kTargetWidth, kTargetHeight, solid.view.get(),
                                                     dash.view.get(), kSourceWidth, 1, static_cast<float>(left),
                                                     static_cast<float>(top)) != S_OK)
            return false;
    }
    std::array<Pixel, kTargetWidth * kTargetHeight> actual{};
    return ReadTarget(device, context, target, actual) && Matches(actual, left, top, clip, overlay);
}
} // namespace

int RunLassoDraftPixelTests()
{
    gChecks = 0;
    gFailures = 0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                              &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    Expect(SUCCEEDED(created), "setup.warp-device");
    if (FAILED(created))
        goto summary;
    {
        CanvasRenderer composition;
        const HRESULT ready = composition.CreateDeviceResources(device.get(), context.get());
        Expect(SUCCEEDED(ready), "setup.actual-build-time-composition-shader");
        if (FAILED(ready))
            goto summary;
        ColorInput color;
        Expect(CreateColorInput(device.get(), color), "setup.immutable-premultiplied-bgra-input");
        if (!color.view)
            goto summary;

        Expect(DrawAndMatch(composition, device.get(), context.get(), color, 2, 1, nullptr),
               "draw.pixel-aligned-offset-exact-premultiplied-source-over-tolerance-one");
        Expect(DrawAndMatch(composition, device.get(), context.get(), color, 10, 10, nullptr),
               "draw.offgrid-outside-target-contributes-no-pixels");
        constexpr std::array<int, 4> fractionalClip{1, 1, 15, 3};
        Expect(DrawAndMatch(composition, device.get(), context.get(), color, 1, 1, &fractionalClip),
               "clip.quarter-coordinate-edge-areas-applied-once");
        // The transparent third input pixel must leave the fractional base unchanged.
        Expect(DrawAndMatch(composition, device.get(), context.get(), color, 1, 1, nullptr),
               "pixels.transparent-source-is-no-contribution");

        Target orderedTarget;
        bool ordered = CreateTarget(device.get(), orderedTarget);
        Mask solid;
        Mask dash;
        constexpr std::array<std::uint8_t, kSourceWidth> solidBytes{0, 0, 0, 128};
        constexpr std::array<std::uint8_t, kSourceWidth> dashBytes{};
        ordered = ordered && CreateMask(device.get(), solidBytes, solid) && CreateMask(device.get(), dashBytes, dash);
        const HRESULT draft =
            ordered ? composition.DrawLassoDraftToTarget(orderedTarget.view.get(), kTargetWidth, kTargetHeight,
                                                         color.view.get(), kSourceWidth, kSourceHeight, 2, 1)
                    : E_FAIL;
        Expect(draft == S_OK && InputsAndBlendReset(context.get()),
               "draw.draft-pass-releases-srv-and-blend-before-next-consumer");
        const HRESULT selection =
            draft == S_OK
                ? composition.DrawSelectionOutlineToTarget(orderedTarget.view.get(), kTargetWidth, kTargetHeight,
                                                           solid.view.get(), dash.view.get(), kSourceWidth, 1, 2, 1)
                : E_FAIL;
        std::array<Pixel, kTargetWidth * kTargetHeight> orderedPixels{};
        ordered = ordered && selection == S_OK &&
                  ReadTarget(device.get(), context.get(), orderedTarget, orderedPixels) &&
                  Matches(orderedPixels, 2, 1, nullptr, true);
        Expect(ordered, "draw.draft-then-existing-selection-overlay-source-over-order");
        Expect(ordered && InputsAndBlendReset(context.get()),
               "draw.borrowed-srvs-and-blend-state-reset-for-next-consumer");

        Target guarded;
        bool guards = CreateTarget(device.get(), guarded);
        std::array<Pixel, kTargetWidth * kTargetHeight> before{};
        guards = guards && ReadTarget(device.get(), context.get(), guarded, before);
        constexpr std::array<float, 4> invalidClip{-0.25f, 0.0f, 3.0f, 1.0f};
        const HRESULT invalid =
            guards
                ? composition.DrawLassoDraftToTarget(guarded.view.get(), kTargetWidth, kTargetHeight, color.view.get(),
                                                     kSourceWidth, kSourceHeight, 0, 0, &invalidClip)
                : E_FAIL;
        std::array<Pixel, kTargetWidth * kTargetHeight> after{};
        guards = guards && invalid == E_INVALIDARG && ReadTarget(device.get(), context.get(), guarded, after) &&
                 before == after;
        Expect(guards, "guards.invalid-clip-e-invalidarg-before-target-change");
        const HRESULT fractionalX = composition.DrawLassoDraftToTarget(
            guarded.view.get(), kTargetWidth, kTargetHeight, color.view.get(), kSourceWidth, kSourceHeight, 0.5f, 0.0f);
        const HRESULT fractionalY =
            composition.DrawLassoDraftToTarget(guarded.view.get(), kTargetWidth, kTargetHeight, color.view.get(),
                                               kSourceWidth, kSourceHeight, 0.0f, -0.25f);
        Expect(fractionalX == E_INVALIDARG && fractionalY == E_INVALIDARG &&
                   ReadTarget(device.get(), context.get(), guarded, after) && before == after,
               "guards.fractional-origin-x-and-y-e-invalidarg");
        Expect(InputsAndBlendReset(context.get()), "guards.no-borrowed-srv-or-blend-state-left-bound");
    }
summary:
    std::printf("SUMMARY lasso-draft-pixels checks=%u failures=%u\n", gChecks, gFailures);
    return static_cast<int>(gFailures);
}

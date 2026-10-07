#include "Composition.Tests.BrushDensityClear.h"

#include "BrushCompute.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <d3d11sdklayers.h>
#include <limits>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool ok, const char* control) noexcept
{
    std::printf("%s brush-density-clear.%s\n", ok ? "PASS" : "FAIL", control);
    gFailures += ok ? 0 : 1;
}

void ExpectCase(bool ok, int side, bool hard, const char* control) noexcept
{
    std::printf("%s brush-density-clear.%d.%s.%s\n", ok ? "PASS" : "FAIL", side, hard ? "hard" : "soft", control);
    gFailures += ok ? 0 : 1;
}

void CheckFreshAndExistingTile(GpuBrushCoverage& engine, int side, bool hard)
{
    const auto count = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
    std::vector<std::uint8_t> preview(count, 0xA5);
    std::vector<std::uint8_t> cpu(count, 0xA5);
    std::vector<float> density(count, 0);
    const BrushKernelTile tile{19, 0, 0, side, side, nullptr, preview.data()};
    const BrushKernelTile reference{19, 0, 0, side, side, density.data(), cpu.data()};
    const std::array<BrushKernelTile, 1> tiles{tile};
    BrushKernelParams params;
    params.radius = 1000;
    params.hardness = hard ? 1 : 0;
    params.canvasWidth = side;
    params.canvasHeight = side;
    params.antialiasWidth = 1;
    params.spacing = 1;
    const Vec2 center{side / 2.0, side / 2.0};
    const std::array<BrushSegment, 1> dab{BrushSegment{center, center}};
    const auto zero = [&]() noexcept
    { return std::all_of(preview.begin(), preview.end(), [](auto value) noexcept { return value == 0; }); };

    engine.BeginStroke();
    const bool empty = engine.Render(tiles, {}, {}, params);
    ExpectCase(empty, side, hard, "fresh-empty-dispatch");
    ExpectCase(empty && zero(), side, hard, "fresh-density-produces-literal-zero-coverage");
    const bool painted = engine.Render(tiles, dab, {}, params);
    BrushKernel::RenderTile(reference, dab, {}, params);
    ExpectCase(painted, side, hard, "settled-dab-dispatch");
    bool matches = painted;
    bool positive = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        positive |= preview[i] > 0;
        matches &= hard ? preview[i] == 255 : std::abs(static_cast<int>(preview[i]) - static_cast<int>(cpu[i])) <= 2;
    }
    ExpectCase(matches && positive, side, hard, "hard-literal-255-or-soft-independent-cpu-tolerance-2");
    const std::vector<std::uint8_t> retained = preview;
    const bool settledOnly = engine.Render(tiles, {}, {}, params);
    ExpectCase(settledOnly, side, hard, "existing-tile-empty-dispatch");
    ExpectCase(settledOnly && preview == retained, side, hard, "existing-density-is-preserved");
    engine.EndStroke();
    ExpectCase(engine.TileCount() == 0, side, hard, "end-stroke-releases-tile-owner");

    std::fill(preview.begin(), preview.end(), static_cast<std::uint8_t>(0xA5));
    engine.BeginStroke();
    const bool restarted = engine.Render(tiles, {}, {}, params);
    ExpectCase(restarted, side, hard, "reused-key-in-new-stroke-empty-dispatch");
    ExpectCase(restarted && zero(), side, hard, "reused-key-starts-from-literal-zero-density");
    engine.EndStroke();
}
} // namespace

int RunBrushDensityClearTests()
{
    gFailures = 0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const auto create = [&](UINT flags) noexcept
    {
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, &level, 1, D3D11_SDK_VERSION,
                                 device.put(), nullptr, context.put());
    };
    HRESULT result = create(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG);
    bool debug = SUCCEEDED(result);
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
        result = create(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    Expect(SUCCEEDED(result), "setup.warp-device");
    if (FAILED(result))
        return gFailures;
    std::printf("INFO brush-density-clear.debug-layer=%s\n", debug ? "active" : "unavailable");
    wil::com_ptr_nothrow<ID3D11InfoQueue> messages;
    if (debug)
    {
        result = device->QueryInterface(IID_PPV_ARGS(messages.put()));
        Expect(SUCCEEDED(result), "setup.debug-message-interface");
        if (FAILED(result))
            return gFailures;
        messages->ClearStoredMessages();
    }
    GpuBrushCoverage engine;
    result = engine.Initialize(device.get(), context.get());
    Expect(SUCCEEDED(result), "setup.compute-engine");
    if (FAILED(result))
        return gFailures;
    CheckFreshAndExistingTile(engine, 4, true);
    CheckFreshAndExistingTile(engine, 4, false);
    CheckFreshAndExistingTile(engine, 256, true);
    CheckFreshAndExistingTile(engine, 256, false);
    engine.Release();
    if (messages)
    {
        bool clean = true;
        std::vector<std::uint8_t> bytes;
        const auto count = messages->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T length = 0;
            if (FAILED(messages->GetMessage(i, nullptr, &length)) || length < sizeof(D3D11_MESSAGE))
            {
                clean = false;
                break;
            }
            bytes.resize(length);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
            if (FAILED(messages->GetMessage(i, message, &length)))
            {
                clean = false;
                break;
            }
            if (message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
                message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D11_MESSAGE_SEVERITY_WARNING)
            {
                clean = false;
                std::printf("INFO brush-density-clear.debug-message=%.*s\n",
                            static_cast<int>((std::min)(message->DescriptionByteLength,
                                                        static_cast<SIZE_T>((std::numeric_limits<int>::max)()))),
                            message->pDescription);
            }
        }
        Expect(clean, "debug-layer.no-warning-error-or-corruption");
    }
    return gFailures;
}

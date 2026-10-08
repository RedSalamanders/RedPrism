#include "Composition.Tests.RecordedBlit.h"

#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "UiTiming.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <d3d11_1.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
unsigned int gFailures = 0;

void Expect(bool passed, const char* name) noexcept
{
    std::printf("%s recorded-blit.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
    {
        ++gFailures;
    }
}

[[nodiscard]] std::uint64_t SampleCount(const UiTimingStats& stats, UiTimingKind kind) noexcept
{
    return stats.durations[static_cast<std::size_t>(kind)].SampleCount();
}

[[nodiscard]] int Channel(const std::vector<std::uint8_t>& pixels, UINT width, UINT height, UINT x, UINT y,
                          std::size_t channel) noexcept
{
    if (x >= width || y >= height || channel >= 4)
    {
        return -1;
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4 + channel;
    return offset < pixels.size() ? pixels[offset] : -1;
}

[[nodiscard]] bool BlitAndRead(RecordedBlitGraphHarness& graph, const CanvasViewport& viewport, UINT width, UINT height,
                               UiTimingStats& blitStats, std::vector<std::uint8_t>& pixels)
{
    if (graph.device == nullptr || graph.immediateContext == nullptr)
    {
        return false;
    }

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
    if (FAILED(graph.device->CreateTexture2D(&description, nullptr, target.put())) ||
        FAILED(graph.device->CreateRenderTargetView(target.get(), nullptr, view.put())))
    {
        return false;
    }

    constexpr std::array<float, 4> clear{0, 0, 0, 0};
    graph.immediateContext->ClearRenderTargetView(view.get(), clear.data());
    HRESULT result = E_FAIL;
    {
        UiTimingCapture capture{blitStats};
        result = graph.composition.BlitToTarget(view.get(), width, height, viewport, 0, 0, static_cast<float>(width),
                                               static_cast<float>(height));
    }
    if (FAILED(result))
    {
        return false;
    }

    description.BindFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(graph.device->CreateTexture2D(&description, nullptr, staging.put())))
    {
        return false;
    }
    graph.immediateContext->CopyResource(staging.get(), target.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = graph.immediateContext->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result))
    {
        return false;
    }
    const auto unmap = wil::scope_exit([&]() noexcept { graph.immediateContext->Unmap(staging.get(), 0); });

    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
    {
        const auto* source =
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        auto* destination = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        std::copy_n(source, static_cast<std::size_t>(width) * 4, destination);
    }
    return true;
}

void CheckBlitOnly(RecordedBlitGraphHarness& graph, const CanvasViewport& view, UiTimingStats& stats,
                   std::vector<std::uint8_t>& pixels, const char* name)
{
    const std::uint64_t composites = graph.composition.CompositeCount();
    const bool succeeded = BlitAndRead(graph, view, 16, 8, stats, pixels);
    Expect(succeeded, name);
    Expect(graph.composition.CompositeCount() == composites, "pan-zoom-does-not-recompose-document");
}

} // namespace

int RunRecordedBlitTests(RecordedBlitGraphHarness& pair, RecordedBlitGraphHarness& stripes,
                         RecordedBlitGraphHarness& tiled)
{
    gFailures = 0;
    std::vector<std::uint8_t> pixels;
    Expect(pair.composition.PreparedDisplayTextureForTest() != nullptr &&
               stripes.composition.PreparedDisplayTextureForTest() != nullptr &&
               tiled.composition.PreparedDisplayTextureForTest() != nullptr,
           "all-inputs-are-sealed-prepared-display-graphs");

    CanvasViewport tiledView;
    tiledView.Resize(Vec2{16, 8}, 1, Vec2{513, 513});
    tiledView.SetZoom(1, tiledView.Center(), Vec2{513, 513});
    UiTimingStats tiledStats{};
    const std::uint64_t tiledComposites = tiled.composition.CompositeCount();
    std::vector<std::uint8_t> baseline;
    const bool baselineBlit = BlitAndRead(tiled, tiledView, 16, 8, tiledStats, baseline);
    Expect(baselineBlit && Channel(baseline, 16, 8, 8, 4, 3) > 250, "sealed-graph-owner-blit-produces-opaque-center");
    constexpr D3D11_RECT staleScissor{512, 512, 1024, 1024};
    if (tiled.immediateContext != nullptr)
    {
        tiled.immediateContext->RSSetScissorRects(1, &staleScissor);
    }
    D3D11_RECT installedScissor{};
    UINT installedScissorCount = 1;
    if (tiled.immediateContext != nullptr)
    {
        tiled.immediateContext->RSGetScissorRects(&installedScissorCount, &installedScissor);
    }
    Expect(tiled.immediateContext != nullptr && installedScissorCount == 1 &&
               installedScissor.left == staleScissor.left && installedScissor.top == staleScissor.top &&
               installedScissor.right == staleScissor.right && installedScissor.bottom == staleScissor.bottom,
           "owner-context-has-deliberate-stale-512-tile-scissor");
    const bool tiledBlit =
        tiled.immediateContext != nullptr && BlitAndRead(tiled, tiledView, 16, 8, tiledStats, pixels);
    Expect(tiledBlit && pixels == baseline, "sealed-graph-blit-restores-full-output-with-512-tile-scissor-installed");
    D3D11_RECT actualScissor{};
    UINT actualScissorCount = 1;
    if (tiledBlit)
    {
        tiled.immediateContext->RSGetScissorRects(&actualScissorCount, &actualScissor);
    }
    wil::com_ptr_nothrow<ID3D11RasterizerState> blitRasterizer;
    D3D11_RASTERIZER_DESC blitRasterDescription{};
    if (tiledBlit)
    {
        tiled.immediateContext->RSGetState(blitRasterizer.put());
        if (blitRasterizer)
        {
            blitRasterizer->GetDesc(&blitRasterDescription);
        }
    }
    Expect(tiledBlit && blitRasterizer && blitRasterDescription.ScissorEnable == TRUE,
           "sealed-blit-binds-scissor-enabled-rasterizer");
    constexpr D3D11_RECT expectedScissor{0, 0, 16, 8};
    Expect(tiledBlit && actualScissorCount == 1 && actualScissor.left == expectedScissor.left &&
               actualScissor.top == expectedScissor.top && actualScissor.right == expectedScissor.right &&
               actualScissor.bottom == expectedScissor.bottom,
           "successful-blit-leaves-literal-whole-target-scissor");
    Expect(tiled.composition.CompositeCount() == tiledComposites, "tiled-blit-does-not-recompose-document");
    Expect(SampleCount(tiledStats, UiTimingKind::MipGeneration) == 0, "tiled-blit-does-not-generate-canvas-mips");

    UiTimingStats pairBlitStats{};
    CanvasViewport view;
    view.Resize(Vec2{16, 8}, 2, Vec2{2, 1});
    view.SetZoom(2, view.Center(), Vec2{2, 1});
    const std::uint64_t pairComposites = pair.composition.CompositeCount();
    bool rendered = BlitAndRead(pair, view, 16, 8, pairBlitStats, pixels);
    Expect(rendered, "pair-graph-blits-through-acknowledged-context");
    const bool checkerLeft = Channel(pixels, 16, 8, 5, 4, 0) == 46 || Channel(pixels, 16, 8, 5, 4, 0) == 56;
    const bool checkerRight = Channel(pixels, 16, 8, 10, 4, 0) == 46 || Channel(pixels, 16, 8, 10, 4, 0) == 56;
    Expect(checkerLeft && checkerRight && Channel(pixels, 16, 8, 6, 4, 0) < 10 &&
               Channel(pixels, 16, 8, 7, 4, 0) < 10 && Channel(pixels, 16, 8, 8, 4, 0) > 245 &&
               Channel(pixels, 16, 8, 9, 4, 0) > 245,
           "zoom-2-backing-2-covers-literal-columns-6-through-9");
    Expect(pair.composition.CompositeCount() == pairComposites, "pair-blit-keeps-composite-count");

    view.SetZoom(4, view.Center(), Vec2{2, 1});
    rendered = BlitAndRead(pair, view, 16, 8, pairBlitStats, pixels);
    Expect(rendered && Channel(pixels, 16, 8, 7, 4, 0) < 3 && Channel(pixels, 16, 8, 8, 4, 0) > 252,
           "crisp-boundary-from-200-percent");
    Expect(pair.composition.CompositeCount() == pairComposites, "zoom-blit-keeps-composite-count");

    view.SetZoom(1.5, view.Center(), Vec2{2, 1});
    rendered = BlitAndRead(pair, view, 16, 8, pairBlitStats, pixels);
    const int blendedBoundary = Channel(pixels, 16, 8, 7, 4, 0);
    Expect(rendered && blendedBoundary > 20 && blendedBoundary < 235, "150-percent-boundary-is-filtered");
    Expect(pair.composition.CompositeCount() == pairComposites, "filtered-blit-keeps-composite-count");

    view.pan.x += 0.25;
    CheckBlitOnly(pair, view, pairBlitStats, pixels, "pair-pan-blit-succeeds");
    Expect(pair.composition.CompositeCount() == pairComposites, "pair-pan-does-not-recompose");

    UiTimingStats stripeBlitStats{};
    CanvasViewport reduced;
    reduced.Resize(Vec2{16, 16}, 1, Vec2{64, 64});
    reduced.SetZoom(0.25, reduced.Center(), Vec2{64, 64});
    reduced.pan = Vec2{0.375, 0.375};
    const std::uint64_t stripeComposites = stripes.composition.CompositeCount();
    rendered = BlitAndRead(stripes, reduced, 16, 16, stripeBlitStats, pixels);
    bool gray = rendered && pixels.size() == 16u * 16u * 4u;
    for (std::size_t index = 0; gray && index < pixels.size(); index += 4)
    {
        gray = pixels[index] >= 100 && pixels[index] <= 155;
    }
    Expect(gray, "25-percent-stripes-read-literal-mid-gray-range-100-through-155");
    Expect(stripes.composition.CompositeCount() == stripeComposites, "stripe-blit-does-not-recompose");
    Expect(SampleCount(pairBlitStats, UiTimingKind::MipGeneration) == 0 &&
               SampleCount(stripeBlitStats, UiTimingKind::MipGeneration) == 0,
           "blit-only-pan-zoom-generates-zero-canvas-mips");
    Expect(SampleCount(pairBlitStats, UiTimingKind::CompositionRecord) == 0 &&
               SampleCount(stripeBlitStats, UiTimingKind::CompositionRecord) == 0,
           "blit-only-pan-zoom-records-zero-composition-work");

    return static_cast<int>(gFailures);
}

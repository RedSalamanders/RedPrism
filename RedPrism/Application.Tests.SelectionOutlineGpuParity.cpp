#include "Application.h"
#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
struct FrameCapture final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    std::vector<std::uint8_t> pixels;
    unsigned int width = 0, height = 0;
    unsigned int calls = 0;
    bool Initialize(ID3D11Device* device, unsigned int w, unsigned int h)
    {
        width = w;
        height = h;
        pixels.resize(static_cast<std::size_t>(w) * h * 4u);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.put()));
    }
    static HRESULT Read(void* pointer, ID3D11Device*, ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* target) noexcept
    {
        auto& capture = *static_cast<FrameCapture*>(pointer);
        wil::com_ptr_nothrow<ID3D11Resource> resource;
        target->GetResource(resource.put());
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        HRESULT result = resource->QueryInterface(IID_PPV_ARGS(texture.put()));
        if (FAILED(result))
            return result;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Width != capture.width || desc.Height != capture.height || desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1)
            return E_INVALIDARG;
        context->CopyResource(capture.staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // Numeric fixture only; ordinary renderer leaves the callback null and never maps pixels.
        result = context->Map(capture.staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(result))
            return result;
        const auto unmap = wil::scope_exit([&]() noexcept { context->Unmap(capture.staging.get(), 0); });
        if (!mapped.pData || mapped.RowPitch < capture.width * 4u)
            return E_UNEXPECTED;
        for (unsigned int row = 0; row < capture.height; ++row)
            std::memcpy(capture.pixels.data() + static_cast<std::size_t>(row) * capture.width * 4u,
                        static_cast<const std::uint8_t*>(mapped.pData) +
                            static_cast<std::size_t>(row) * mapped.RowPitch,
                        static_cast<std::size_t>(capture.width) * 4u);
        ++capture.calls;
        return S_OK;
    }
};
CanvasDocument ParityDocument()
{
    DocumentFixture fixture;
    fixture.width = fixture.height = 64;
    fixture.layers.push_back(ImageLayer::Blank(L"Parity", {64, 64}));
    fixture.selection =
        DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{{{8, 8}, {48, 8}, {48, 48}, {8, 48}}}}};
    return std::move(fixture).Seal();
}
} // namespace
int Application::RunStandaloneSelectionOutlineParitySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0, failures = 0;
    for (unsigned int dpi : {96u, 144u, 192u})
    {
        const auto expect = [&](bool value, const char* name) noexcept
        {
            std::printf("%s application-selection-outline-parity.dpi%u.%s\n", value ? "PASS" : "FAIL", dpi, name);
            ++checks;
            failures += value ? 0u : 1u;
            return value;
        };
        auto owner = std::make_unique<Application>(instance, true, L"", dpi, nullptr, nullptr);
        if (!expect(static_cast<bool>(owner), "actual-owner-allocation"))
            return 5;
        FrameCapture capture;
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                owner->_renderer.SetFrameReadbackForTest(nullptr, nullptr);
                owner->_renderer.SuppressSelectionOutlineForTest(false);
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()) && owner->EnableCommittedDisplayForTest(true) &&
                        owner->EnableSelectionOutlineForTest(true),
                    "actual-dpi-harness-host-initializes"))
            return 5;
        auto document = ParityDocument();
        const auto target = document.layers[0].id;
        owner->_session.ReplaceDocument(std::move(document), target);
        owner->_viewport.followsFit = false;
        owner->_viewport.zoom = 1;
        owner->_chrome->SetLayersWidth(240.25f);
        owner->DocumentChanged();
        const auto hole = owner->_chrome->HolePixels();
        const auto origin = owner->_viewport.DocumentRectOrigin({64, 64});
        owner->_viewport.pan.x += hole.width - 0.25 - 8.0 - origin.x;
        owner->_viewport.pan.y += hole.height / 2.0 - 8.0 - origin.y;
        const auto shown = owner->_session.DisplayedSelectionValue();
        owner->SyncSelectionOverlay(&shown);
        if (!expect(SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled &&
                        static_cast<bool>(owner->_selectionOutlineDisplay.BorrowFrameOwner(0)),
                    "actual-worker-source-settles-at-fractional-view-edge"))
            return 5;
        expect(owner->ChromeDpi() == dpi && owner->_selectionOutlineGrid.dpi == dpi,
               "actual-chrome-and-worker-grid-use-requested-dpi");
        const double right = static_cast<double>(hole.left) + hole.width;
        expect(std::floor(right) != right, "actual-chrome-hole-has-fractional-right-boundary");
        if (!expect(capture.Initialize(owner->_renderer.Device(), owner->_renderer.Width(), owner->_renderer.Height()),
                    "harness-target-readback-storage-preallocated"))
            return 5;
        owner->_renderer.SetFrameReadbackForTest(FrameCapture::Read, &capture);
        owner->_renderer.SuppressSelectionOutlineForTest(true);
        owner->RenderFrame();
        if (!expect(capture.calls == 1, "actual-background-frame-captured-before-present"))
            return 5;
        const auto background = capture.pixels;
        bool anyOutline = false;
        const int firstX = std::max(0, static_cast<int>(std::floor(right)) - 4);
        const int lastX = std::min(static_cast<int>(capture.width), static_cast<int>(std::ceil(right)) + 2);
        const int firstY = std::max(0, static_cast<int>(std::floor(hole.top + hole.height / 2.0f)) - 4);
        const int lastY = std::min(static_cast<int>(capture.height), firstY + 48);
        for (unsigned int phase = 0; phase < 8; ++phase)
        {
            owner->_chrome->SetSelectionPaintExternal(true);
            owner->_renderer.SuppressSelectionOutlineForTest(false);
            owner->_chrome->SetSelectionAntPhase(static_cast<float>(phase));
            owner->RenderFrame();
            const auto prepared = capture.pixels;
            expect(capture.calls == 2 + phase * 2 && owner->_renderer.SelectionOutlineFailure() == S_OK,
                   "actual-prepared-frame-captured-with-late-outline-borrow");
            // Genuine reference branch: retain the original production numeric mapping and D2D paint pair.
            // Its four-point owner work is deliberately confined to this comparison fixture.
            owner->_selectionOutlineInitialized = false;
            owner->_overlaySelection.reset();
            owner->SyncSelectionOverlay(&shown);
            owner->_selectionOutlineInitialized = true;
            owner->_chrome->SetSelectionPaintExternal(false);
            owner->_renderer.SuppressSelectionOutlineForTest(true);
            owner->RenderFrame();
            bool equal = capture.calls == 3 + phase * 2;
            int largest = 0;
            for (int y = firstY; y < lastY; ++y)
                for (int x = firstX; x < lastX; ++x)
                    for (std::size_t channel = 0; channel < 4; ++channel)
                    {
                        const auto index = (static_cast<std::size_t>(y) * capture.width + x) * 4u + channel;
                        const int delta = std::abs(static_cast<int>(prepared[index]) - capture.pixels[index]);
                        largest = std::max(largest, delta);
                        equal = equal && delta <= 1;
                        anyOutline = anyOutline || std::abs(static_cast<int>(prepared[index]) - background[index]) > 8;
                    }
            if (!equal)
                std::printf("OUTLINE PARITY DIAG dpi=%u phase=%u maxDelta=%d right=%.9f\n", dpi, phase, largest, right);
            expect(equal, "actual-fractional-frame-matches-original-d2d-pair-within-one-byte");
        }
        expect(anyOutline, "comparison-region-contains-visible-outline-not-two-empty-frames");
        owner->_chrome->SetSelectionPaintExternal(true);
        owner->_chrome->SetSelectionOutline({});
        owner->_renderer.SetFrameReadbackForTest(nullptr, nullptr);
        owner->_renderer.SuppressSelectionOutlineForTest(false);
        owner->Shutdown();
        expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_graphGpuBudget.ReservedOwner() == 0,
               "actual-joined-close-refunds-all-original-source-and-residency-charges");
    }
    std::printf("%s application-selection-outline-parity.summary.%u-passed.%u-failed\n", failures ? "FAIL" : "PASS",
                checks - failures, failures);
    return failures ? 5 : 0;
}

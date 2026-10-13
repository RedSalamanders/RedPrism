#pragma once

#include "AdapterSelection.h"

#include <array>
#include <cstdint>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;
class CanvasRenderer;
class RecordedDisplayGraph;
struct CanvasViewport;
struct CanvasDocument;

class Renderer final
{
  public:
    static constexpr UINT kOcclusionStatusMessage = WM_APP + 1;

    // The stages of a frame, in order (Rendering_Composition.md "Present order").
    enum class Stage : std::uint8_t
    {
        ChromePrepare,
        Compose,
        Blit,
        ChromeComposite,
        Present,
    };

    // Harness-only numeric capture of the actual completed frame before Present. No ordinary readback.
    using FrameReadbackForTest = HRESULT (*)(void*, ID3D11Device*, ID3D11DeviceContext*,
                                             ID3D11RenderTargetView*) noexcept;
    void SetFrameReadbackForTest(FrameReadbackForTest callback, void* context) noexcept
    {
        _frameReadbackForTest = callback;
        _frameReadbackContextForTest = context;
    }
    void SuppressSelectionOutlineForTest(bool suppress) noexcept
    {
        _suppressSelectionOutlineForTest = suppress;
    }

    struct SelectionOutlineFrame final
    {
        CanvasRenderer* composition = nullptr;
        ID3D11ShaderResourceView* solid = nullptr;
        ID3D11ShaderResourceView* dashes = nullptr;
        unsigned int width = 0;
        unsigned int height = 0;
        float pixelLeft = 0.0f;
        float pixelTop = 0.0f;
        bool clipped = false;
        std::array<float, 4> clip{};
    };
    class SelectionOutlineFrameSource
    {
      public:
        virtual ~SelectionOutlineFrameSource() = default;
        [[nodiscard]] virtual SelectionOutlineFrame BorrowSelectionOutlineFrameOwner(float left, float top, float width,
                                                                                     float height) noexcept = 0;
    };
    [[nodiscard]] std::uint64_t SelectionOutlineDrawCount() const noexcept
    {
        return _selectionOutlineDraws;
    }
    [[nodiscard]] HRESULT SelectionOutlineFailure() const noexcept
    {
        return _selectionOutlineFailure;
    }

    struct DeviceIdentity final
    {
        LUID adapterLuid{};
        bool warp = false;
        bool adapterOwnsWindowMonitor = false;
        std::array<wchar_t, 128> adapterName{};
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    HRESULT Initialize(HWND window, bool forceWarp) noexcept;
    void Shutdown() noexcept;
    void SetChrome(ChromeHost* chrome) noexcept;
    HRESULT Resize(UINT width, UINT height) noexcept;
    // One frame. S_OK when it was drawn (presented, or occluded); S_FALSE when a stage found the device removed and
    // the device, swap chain and target were rebuilt (the host rebinds and draws again); a failure otherwise. A
    // canvas stage that fails without device loss leaves the hole empty and still presents the chrome
    // (CanvasFailure reports it), so the window keeps working (Decision D1).
    HRESULT Render(ChromeHost* chrome, CanvasRenderer* composition, const CanvasViewport* viewport,
                   const CanvasDocument* document, RecordedDisplayGraph* acknowledgedDisplay = nullptr,
                   SelectionOutlineFrameSource* selectionOutline = nullptr) noexcept;
    // The canvas stage's failure in the last Render, S_OK when the canvas drew.
    [[nodiscard]] HRESULT CanvasFailure() const noexcept
    {
        return _canvasFailure;
    }
    // The stage and result of the last Render that failed (diagnostics).
    [[nodiscard]] Stage FailedStage() const noexcept
    {
        return _failedStage;
    }
    // True only when this Render attempt completed a canvas blit, distinct from a chrome-only Present.
    [[nodiscard]] bool CanvasDrawn() const noexcept
    {
        return _canvasDrawn;
    }
    // Frames that reached the display (Present neither failed nor reported occlusion).
    [[nodiscard]] std::uint64_t PresentCount() const noexcept
    {
        return _presentCount;
    }
    // Bumped by every device rebuild, so a borrower can tell a new device from an old one at the same address.
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _deviceGeneration;
    }
    // Whether DXGI's occlusion-status message is registered; without it an occluded window probes on each wake.
    [[nodiscard]] bool OcclusionMessages() const noexcept
    {
        return _occlusionStatusRegistered;
    }
    // The color behind the canvas (Specs/UI/UI_ApplicationChrome.md, Theme): dark, light or the system window
    // color under high contrast.
    void SetClearColor(const std::array<float, 4>& color) noexcept
    {
        _clearColor = color;
    }
    [[nodiscard]] const std::array<float, 4>& ClearColor() const noexcept
    {
        return _clearColor;
    }
    void SetLayoutDpi(float dpi) noexcept
    {
        _layoutDpi = dpi;
    }
    [[nodiscard]] ID3D11Device* Device() const noexcept;
    [[nodiscard]] ID3D11DeviceContext* Context() const noexcept;
    [[nodiscard]] UINT Width() const noexcept;
    [[nodiscard]] UINT Height() const noexcept;
    HRESULT ProbeOcclusion() noexcept;
    HRESULT EnsureDeviceForWindowMonitor() noexcept;
    [[nodiscard]] HANDLE FrameLatencyWaitableObject() const noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;
    [[nodiscard]] bool IsOccluded() const noexcept;
    [[nodiscard]] bool IsWarp() const noexcept;
    // Self-test seam: the same rebuild Present runs after a device-removed result.
    HRESULT RecoverDeviceForTest() noexcept;
    // Utility work can detect removal without a frame/Present, including while hidden. Rebuild only for a
    // device-lost result or a removed current device; the host rebinds its borrowers afterwards.
    HRESULT RecoverDeviceForUtility(HRESULT result) noexcept;
    // Self-test seam: the next time `stage` runs it reports `result` instead of its own (a device-lost code makes
    // the stage look like a removed device).
    void FailNextStageForTest(Stage stage, HRESULT result) noexcept
    {
        _injectedStage = stage;
        _injectedResult = result;
        _injecting = true;
    }
    // Self-test seam: IsOccluded() reports `occluded` (the state a Present that reported DXGI_STATUS_OCCLUDED
    // leaves, or not) until cleared with nullopt, whatever the real Present of the hidden harness window returns
    // meanwhile (DWM may report it occluded while the displays sleep).
    void SetOccludedForTest(std::optional<bool> occluded) noexcept
    {
        _occludedForTest = occluded;
    }

  private:
    static constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    HRESULT CreateDeviceResources() noexcept;
    HRESULT CreateDevice(bool useWarp) noexcept;
    static HRESULT FindAdapterForMonitor(HMONITOR monitor, wil::com_ptr_nothrow<IDXGIAdapter1>& adapter,
                                         LUID& adapterLuid) noexcept;
    void RecordDeviceIdentity(bool warp, bool adapterOwnsWindowMonitor) noexcept;
    HRESULT CreateSwapChain() noexcept;
    HRESULT CreateRenderTarget(UINT width, UINT height) noexcept;
    HRESULT RecoverDevice() noexcept;
    void ReleaseDeviceResources() noexcept;
    static bool IsDeviceLost(HRESULT result) noexcept;
    // A failed stage whose result, or the device itself (GetDeviceRemovedReason), says the device is gone.
    [[nodiscard]] bool DeviceLostAfter(HRESULT result) const noexcept;
    // Rebuilds after a stage found the device gone: S_FALSE when rebuilt, the rebuild's failure otherwise.
    HRESULT RecoverFromStage() noexcept;
    [[nodiscard]] HRESULT StageResult(Stage stage, HRESULT result) noexcept;
    void RegisterOcclusionStatus() noexcept;

    std::uint64_t _selectionOutlineDraws = 0;
    HRESULT _selectionOutlineFailure = S_OK;
    HWND _window = nullptr;
    ChromeHost* _chrome = nullptr;
    bool _forceWarp = false;
    bool _suspended = true;
    bool _occluded = false;
    std::optional<bool> _occludedForTest;
    FrameReadbackForTest _frameReadbackForTest = nullptr;
    void* _frameReadbackContextForTest = nullptr;
    bool _suppressSelectionOutlineForTest = false;
    UINT _width = 0;
    UINT _height = 0;
    D3D_FEATURE_LEVEL _featureLevel = D3D_FEATURE_LEVEL_11_0;

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<ID3D11DeviceContext1> _context1;
    wil::com_ptr_nothrow<IDXGIFactory2> _factory;
    wil::com_ptr_nothrow<IDXGISwapChain1> _swapChain;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTarget;
    wil::unique_handle _frameLatencyWaitable;
    DeviceIdentity _deviceInfo{};
    HMONITOR _deviceMonitor = nullptr;
    DWORD _occlusionStatusCookie = 0;
    bool _occlusionStatusRegistered = false;
    HRESULT _canvasFailure = S_OK;
    bool _canvasDrawn = false;
    std::uint64_t _presentCount = 0;
    std::uint64_t _deviceGeneration = 0;
    Stage _injectedStage = Stage::Present;
    HRESULT _injectedResult = S_OK;
    bool _injecting = false;
    Stage _failedStage = Stage::Present;
    float _layoutDpi = 0;
    std::array<float, 4> _clearColor{0.118f, 0.118f, 0.133f, 1.0f};
};

#pragma once

#include "AdapterSelection.h"

#include <array>
#include <cstdint>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;
class CanvasRenderer;
struct CanvasViewport;
struct CanvasDocument;

class Renderer final
{
  public:
    static constexpr UINT kOcclusionStatusMessage = WM_APP + 1;

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
    HRESULT Render(ChromeHost* chrome, CanvasRenderer* composition, const CanvasViewport* viewport,
                   const CanvasDocument* document) noexcept;
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
    [[nodiscard]] DeviceIdentity DeviceInfo() const noexcept;
    [[nodiscard]] HANDLE FrameLatencyWaitableObject() const noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;
    [[nodiscard]] bool IsOccluded() const noexcept;
    [[nodiscard]] bool IsWarp() const noexcept;
    // Self-test seam: the same rebuild Present runs after a device-removed result.
    HRESULT RecoverDeviceForTest() noexcept;

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

    HWND _window = nullptr;
    ChromeHost* _chrome = nullptr;
    bool _forceWarp = false;
    bool _suspended = true;
    bool _occluded = false;
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
    float _layoutDpi = 0;
    std::array<float, 4> _clearColor{0.118f, 0.118f, 0.133f, 1.0f};
};

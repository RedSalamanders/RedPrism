#include "Renderer.h"
#include "MonitorDiagnostics.h"
#include "RecordedDisplayGraph.h"
#include <cstddef>

#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "ChromeHost.h"
#include "Document.h"
#include "UiTiming.h"

#include <array>
#include <cstring>
#include <format>
#include <span>
#include <utility>

namespace
{
constexpr UINT kSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
constexpr size_t kMaximumEnumeratedAdapters = 8;
constexpr size_t kMaximumEnumeratedOutputs = 32;
} // namespace

Renderer::~Renderer()
{
    Shutdown();
}

HRESULT Renderer::Initialize(HWND window, bool forceWarp) noexcept
{
    if (_window || !window)
    {
        return E_INVALIDARG;
    }

    _window = window;
    _forceWarp = forceWarp;
    return CreateDeviceResources();
}

void Renderer::Shutdown() noexcept
{
    _chrome = nullptr;
    _canvasDrawn = false;
    ReleaseDeviceResources();
    _window = nullptr;
    _forceWarp = false;
}

void Renderer::SetChrome(ChromeHost* chrome) noexcept
{
    _chrome = chrome;
}

HRESULT Renderer::CreateDeviceResources() noexcept
{
    ReleaseDeviceResources();

    HRESULT result = CreateDevice(_forceWarp);
    if (FAILED(result) && !_forceWarp)
    {
        OutputDebugStringW(L"Hardware Direct3D initialization failed; trying WARP.\n");
        result = CreateDevice(true);
    }
    if (FAILED(result))
    {
        return result;
    }

    // Chrome still holds the previous device. Hand it the new one before the HWND takes a swap chain, or the
    // old factory keeps the window and CreateSwapChainForHwnd returns E_ACCESSDENIED.
    if (_chrome != nullptr && _device)
    {
        result = _chrome->ReplaceDevice(_device.get());
        if (FAILED(result))
        {
            return result;
        }
    }

    result = CreateSwapChain();
    if (FAILED(result))
    {
        return result;
    }
    // Borrowers (the composition, the brush engine) rebind when this changes, even at a reused address.
    ++_deviceGeneration;

    RECT client{};
    if (!GetClientRect(_window, &client))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width == 0 || height == 0)
    {
        _suspended = true;
        return S_OK;
    }

    return CreateRenderTarget(width, height);
}

HRESULT Renderer::FindAdapterForMonitor(HMONITOR monitor, wil::com_ptr_nothrow<IDXGIAdapter1>& adapter,
                                        LUID& adapterLuid) noexcept
{
    adapter.reset();
    adapterLuid = {};
    if (!monitor)
    {
        return E_INVALIDARG;
    }
    wil::com_ptr_nothrow<IDXGIFactory1> factory;
    const HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(factory.put()));
    if (FAILED(factoryResult))
    {
        return factoryResult;
    }

    std::array<wil::com_ptr_nothrow<IDXGIAdapter1>, kMaximumEnumeratedAdapters> adapters;
    std::array<AdapterOutputRecord, kMaximumEnumeratedOutputs> records{};
    std::array<size_t, kMaximumEnumeratedOutputs> recordAdapters{};
    size_t recordCount = 0;
    for (UINT adapterIndex = 0; adapterIndex < adapters.size(); ++adapterIndex)
    {
        wil::com_ptr_nothrow<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(adapterIndex, candidate.put()) != S_OK)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(candidate->GetDesc1(&description)))
        {
            continue;
        }
        const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        for (UINT outputIndex = 0; recordCount < records.size(); ++outputIndex)
        {
            wil::com_ptr_nothrow<IDXGIOutput> output;
            if (candidate->EnumOutputs(outputIndex, output.put()) != S_OK)
            {
                break;
            }
            DXGI_OUTPUT_DESC outputDescription{};
            if (FAILED(output->GetDesc(&outputDescription)))
            {
                continue;
            }
            records[recordCount] = AdapterOutputRecord{description.AdapterLuid, outputDescription.Monitor, software};
            recordAdapters[recordCount] = adapterIndex;
            ++recordCount;
        }
        adapters[adapterIndex] = std::move(candidate);
    }

    const size_t selected =
        SelectAdapterRecordForMonitor(std::span<const AdapterOutputRecord>(records.data(), recordCount), monitor);
    if (selected == SIZE_MAX)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    adapter = adapters[recordAdapters[selected]];
    adapterLuid = records[selected].adapterLuid;
    return S_OK;
}

void Renderer::RecordDeviceIdentity(bool warp, bool adapterOwnsWindowMonitor) noexcept
{
    _deviceInfo = {};
    _deviceInfo.warp = warp;
    _deviceInfo.adapterOwnsWindowMonitor = adapterOwnsWindowMonitor;
    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC description{};
    if (_device && SUCCEEDED(_device.query_to(dxgiDevice.put())) && SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) &&
        SUCCEEDED(adapter->GetDesc(&description)))
    {
        _deviceInfo.adapterLuid = description.AdapterLuid;
        static_assert(sizeof(_deviceInfo.adapterName) == sizeof(description.Description));
        std::memcpy(_deviceInfo.adapterName.data(), description.Description, sizeof(description.Description));
        _deviceInfo.adapterName.back() = L'\0';
    }

    std::array<wchar_t, 384> message{};
    const auto written =
        std::format_to_n(message.data(), static_cast<std::ptrdiff_t>(message.size() - 1),
                         L"Direct3D device created on {} (LUID {:08X}-{:08X}, {}, adapter owns "
                         L"window monitor: {}).\n",
                         _deviceInfo.adapterName.data(), static_cast<unsigned long>(_deviceInfo.adapterLuid.HighPart),
                         static_cast<unsigned long>(_deviceInfo.adapterLuid.LowPart), warp ? L"WARP" : L"hardware",
                         adapterOwnsWindowMonitor ? L"yes" : L"no");
    const auto n = static_cast<size_t>(written.size);
    message[n < message.size() ? n : message.size() - 1] = L'\0';
    OutputDebugStringW(message.data());
}

HRESULT Renderer::CreateDevice(bool useWarp) noexcept
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    _deviceMonitor = nullptr;
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
    if (!useWarp)
    {
        const HMONITOR monitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONULL);
        LUID adapterLuid{};
        if (monitor && SUCCEEDED(FindAdapterForMonitor(monitor, adapter, adapterLuid)))
        {
            _deviceMonitor = monitor;
        }
        else
        {
            adapter.reset();
        }
    }
    const D3D_DRIVER_TYPE driverType =
        useWarp ? D3D_DRIVER_TYPE_WARP : (adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE);

    auto create = [&](UINT requestedFlags) noexcept
    {
        _device.reset();
        _context.reset();
        _context1.reset();
        return D3D11CreateDevice(adapter.get(), driverType, nullptr, requestedFlags, featureLevels.data(),
                                 static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, _device.put(),
                                 &_featureLevel, _context.put());
    };

    HRESULT result = create(flags);
#if defined(_DEBUG)
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        OutputDebugStringW(L"Direct3D debug layer is unavailable; continuing without it.\n");
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = create(flags);
    }
#endif
    if (FAILED(result))
    {
        return result;
    }
    result = _context.query_to(_context1.put());
    if (FAILED(result))
    {
        _context1.reset();
        _context.reset();
        _device.reset();
        return result;
    }
    RecordDeviceIdentity(useWarp, adapter != nullptr);
    return result;
}

HRESULT Renderer::EnsureDeviceForWindowMonitor() noexcept
{
    if (!_window || !_device || _forceWarp || _deviceInfo.warp)
    {
        return S_FALSE;
    }
    const HMONITOR monitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONULL);
    if (!monitor || monitor == _deviceMonitor)
    {
        return S_FALSE;
    }
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
    LUID adapterLuid{};
    if (FAILED(FindAdapterForMonitor(monitor, adapter, adapterLuid)))
    {
        return S_FALSE;
    }
    if (SameLuid(adapterLuid, _deviceInfo.adapterLuid))
    {
        _deviceMonitor = monitor;
        return S_FALSE;
    }
    return CreateDeviceResources();
}

HANDLE Renderer::FrameLatencyWaitableObject() const noexcept
{
    return _frameLatencyWaitable.get();
}

HRESULT Renderer::CreateSwapChain() noexcept
{
    wil::com_ptr_nothrow<IDXGIDevice1> dxgiDevice;
    HRESULT result = _device.query_to(dxgiDevice.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Format = kTargetFormat;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = kSwapChainFlags;

    result = factory->CreateSwapChainForHwnd(_device.get(), _window, &description, nullptr, nullptr, _swapChain.put());
    if (result == E_ACCESSDENIED)
    {
        // The previous flip-model chain can stay bound to the HWND until the window processes a message.
        SendMessageW(_window, WM_NULL, 0, 0);
        result =
            factory->CreateSwapChainForHwnd(_device.get(), _window, &description, nullptr, nullptr, _swapChain.put());
    }
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGISwapChain2> swapChain2;
    result = _swapChain.query_to(swapChain2.put());
    if (FAILED(result))
    {
        return result;
    }
    result = swapChain2->SetMaximumFrameLatency(1);
    if (FAILED(result))
    {
        return result;
    }
    _frameLatencyWaitable.reset(swapChain2->GetFrameLatencyWaitableObject());
    if (!_frameLatencyWaitable)
    {
        return E_UNEXPECTED;
    }

    result = factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result))
    {
        return result;
    }

    _factory = std::move(factory);
    _occlusionStatusCookie = 0;
    _occlusionStatusRegistered = false;
    RegisterOcclusionStatus();
    return S_OK;
}

void Renderer::RegisterOcclusionStatus() noexcept
{
    if (!_factory || !_window || _occlusionStatusRegistered)
    {
        return;
    }
    DWORD occlusionStatusCookie = 0;
    const HRESULT result =
        _factory->RegisterOcclusionStatusWindow(_window, kOcclusionStatusMessage, &occlusionStatusCookie);
    if (FAILED(result))
    {
        // A second registration on the same HWND (device recovery) can return E_ACCESSDENIED while the swap chain
        // is already usable. Present still runs; an occluded window then probes on each wake (OcclusionMessages)
        // and tries registering again, instead of waiting for a message that never comes (RF-410).
        OutputDebugStringW(L"Occlusion status was not registered; an occluded window probes on each wake.\n");
        return;
    }
    _occlusionStatusCookie = occlusionStatusCookie;
    _occlusionStatusRegistered = true;
}

HRESULT Renderer::CreateRenderTarget(UINT width, UINT height) noexcept
{
    wil::com_ptr_nothrow<ID3D11Texture2D> backBuffer;
    HRESULT result = _swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()));
    if (FAILED(result))
    {
        return result;
    }

    result = _device->CreateRenderTargetView(backBuffer.get(), nullptr, _renderTarget.put());
    if (FAILED(result))
    {
        return result;
    }

    _width = width;
    _height = height;
    _suspended = false;
    _occluded = false;
    return S_OK;
}

HRESULT Renderer::Resize(UINT width, UINT height) noexcept
{
    if (!_swapChain)
    {
        return E_UNEXPECTED;
    }

    if (width == 0 || height == 0)
    {
        _width = 0;
        _height = 0;
        _suspended = true;
        _occluded = false;
        return S_OK;
    }

    if (width == _width && height == _height && _renderTarget)
    {
        _suspended = false;
        _occluded = false;
        return S_OK;
    }

    _context->OMSetRenderTargets(0, nullptr, nullptr);
    _renderTarget.reset();
    HRESULT result = _swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, kSwapChainFlags);
    if (SUCCEEDED(result))
    {
        result = CreateRenderTarget(width, height);
    }
    if (FAILED(result) && DeviceLostAfter(result))
    {
        return RecoverDevice();
    }
    return result;
}

ID3D11Device* Renderer::Device() const noexcept
{
    return _device.get();
}

UINT Renderer::Width() const noexcept
{
    return _width;
}

UINT Renderer::Height() const noexcept
{
    return _height;
}

ID3D11DeviceContext* Renderer::Context() const noexcept
{
    return _context.get();
}

HRESULT Renderer::StageResult(Stage stage, HRESULT result) noexcept
{
    if (_injecting && _injectedStage == stage)
    {
        _injecting = false;
        return _injectedResult;
    }
    return result;
}

bool Renderer::DeviceLostAfter(HRESULT result) const noexcept
{
    // A removed device surfaces from whichever call touched it first: chrome Prepare, a composition pass, the blit,
    // the chrome composite or Present. The device itself says so whatever the stage returned (RF-401).
    return IsDeviceLost(result) || (_device && FAILED(_device->GetDeviceRemovedReason()));
}

HRESULT Renderer::RecoverFromStage() noexcept
{
    const HRESULT recoveryResult = RecoverDevice();
    return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
}

HRESULT Renderer::RecoverDeviceForUtility(HRESULT result) noexcept
{
    return DeviceLostAfter(result) ? RecoverDevice() : S_OK;
}

HRESULT Renderer::Render(ChromeHost* chrome, CanvasRenderer* composition, const CanvasViewport* viewport,
                         const CanvasDocument* document, RecordedDisplayGraph* acknowledgedDisplay,
                         SelectionOutlineFrameSource* selectionOutline) noexcept
{
    _canvasFailure = S_OK;
    _selectionOutlineFailure = S_OK;
    _lassoDraftFailure = S_OK;
    _canvasDrawn = false;
    if (_suspended)
    {
        return S_OK;
    }
    if (!_context || !_swapChain || !_renderTarget)
    {
        return E_UNEXPECTED;
    }

    ID3D11RenderTargetView* renderTargets[] = {_renderTarget.get()};
    {
        const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1000};
        _context->OMSetRenderTargets(1, renderTargets, nullptr);
        _context->ClearRenderTargetView(_renderTarget.get(), _clearColor.data());
    }

    // Order: chrome layout (the hole), then the document into the hole, then the chrome surface over it. The DxUi
    // surface is transparent where no control paints, so tooltips and popups that overlap the canvas stay visible.
    D3D11_VIEWPORT viewportDx{};
    viewportDx.Width = static_cast<float>(_width);
    viewportDx.Height = static_cast<float>(_height);
    viewportDx.MaxDepth = 1.0f;
    if (chrome)
    {
        const HRESULT chromeResult = [&]() noexcept
        {
            const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1001};
            return StageResult(
                Stage::ChromePrepare,
                chrome->Prepare(_width, _height,
                                _layoutDpi > 0.0f ? _layoutDpi : static_cast<float>(GetDpiForWindow(_window))));
        }();
        if (FAILED(chromeResult))
        {
            _failedStage = Stage::ChromePrepare;
            return DeviceLostAfter(chromeResult) ? RecoverFromStage() : chromeResult;
        }
    }

    if (acknowledgedDisplay && viewport && chrome)
    {
        // A sealed, originally acknowledged graph has no owner Compose/upload/create stage. Validate both
        // actual interfaces before any command: a recovered renderer cannot blit a retired device's graph.
        const CanvasHole hole = chrome->HolePixels();
        const HRESULT canvasResult = [&]() noexcept
        {
            const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1003};
            return StageResult(Stage::Blit,
                               acknowledgedDisplay->IsPublishedOnOwner(_device.get(), _context.get())
                                   ? acknowledgedDisplay->BlitOwner(_renderTarget.get(), _width, _height, *viewport,
                                                                    hole.left, hole.top, hole.width, hole.height)
                                   : E_ABORT);
        }();
        _canvasDrawn = canvasResult == S_OK;
        if (FAILED(canvasResult))
        {
            if (DeviceLostAfter(canvasResult))
                return RecoverFromStage();
            _canvasFailure = canvasResult;
        }
        _context->OMSetRenderTargets(1, renderTargets, nullptr);
    }
    else if (composition && composition->HasDevice() && document && viewport && chrome)
    {
        HRESULT canvasResult = [&]() noexcept
        {
            const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1002};
            return StageResult(Stage::Compose, composition->Compose(*document));
        }();
        if (SUCCEEDED(canvasResult))
        {
            const CanvasHole hole = chrome->HolePixels();
            canvasResult = [&]() noexcept
            {
                const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1003};
                return StageResult(Stage::Blit,
                                   composition->BlitToTarget(_renderTarget.get(), _width, _height, *viewport, hole.left,
                                                            hole.top, hole.width, hole.height));
            }();
            _canvasDrawn = canvasResult == S_OK;
        }
        if (FAILED(canvasResult))
        {
            if (DeviceLostAfter(canvasResult))
            {
                return RecoverFromStage();
            }
            // The hole stays empty this frame; the chrome still presents, so the window keeps working and the
            // document can be saved (Decision D1). The host reports it.
            _canvasFailure = canvasResult;
        }
        _context->OMSetRenderTargets(1, renderTargets, nullptr);
    }

    if (selectionOutline && chrome && !_suppressSelectionOutlineForTest)
    {
        // Preparation can publish UIA callbacks. Re-borrow exact source/view/device authority AFTER it.
        const CanvasHole hole = chrome->HolePixels();
        const SelectionOutlineFrame outline =
            selectionOutline->BorrowSelectionOutlineFrameOwner(hole.left, hole.top, hole.width, hole.height);
        if (outline.composition && outline.solid && outline.dashes)
        {
            const HRESULT drawn = outline.composition->Device() == _device.get()
                                      ? outline.composition->DrawSelectionOutlineToTarget(
                                            _renderTarget.get(), _width, _height, outline.solid, outline.dashes,
                                            outline.width, outline.height, outline.pixelLeft, outline.pixelTop,
                                            outline.clipped ? &outline.clip : nullptr)
                                      : E_ABORT;
            _selectionOutlineFailure = drawn;
            if (FAILED(drawn) && DeviceLostAfter(drawn))
                return RecoverFromStage();
            if (drawn == S_OK)
                ++_selectionOutlineDraws;
            _context->OMSetRenderTargets(1, renderTargets, nullptr);
        }
    }

    if (selectionOutline && chrome && !_suppressSelectionOutlineForTest)
    {
        const CanvasHole hole = chrome->HolePixels();
        const LassoDraftFrame draft =
            selectionOutline->BorrowLassoDraftFrameOwner(hole.left, hole.top, hole.width, hole.height);
        if (draft.composition && draft.color)
        {
            const HRESULT drawn =
                draft.composition->Device() == _device.get()
                    ? draft.composition->DrawLassoDraftToTarget(_renderTarget.get(), _width, _height, draft.color,
                                                               draft.width, draft.height, draft.pixelLeft,
                                                               draft.pixelTop, draft.clipped ? &draft.clip : nullptr)
                    : E_ABORT;
            _lassoDraftFailure = drawn;
            if (FAILED(drawn) && DeviceLostAfter(drawn))
                return RecoverFromStage();
            if (drawn == S_OK)
                ++_lassoDraftDraws;
            _context->OMSetRenderTargets(1, renderTargets, nullptr);
        }
    }

    if (chrome)
    {
        const HRESULT chromeResult = [&]() noexcept
        {
            const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1004};
            return StageResult(Stage::ChromeComposite, chrome->Composite(_context.get(), viewportDx));
        }();
        if (FAILED(chromeResult))
        {
            _failedStage = Stage::ChromeComposite;
            return DeviceLostAfter(chromeResult) ? RecoverFromStage() : chromeResult;
        }
    }

    if (_frameReadbackForTest)
    {
        const HRESULT captured =
            _frameReadbackForTest(_frameReadbackContextForTest, _device.get(), _context.get(), _renderTarget.get());
        if (FAILED(captured))
            return captured;
    }
    ++_presentAttemptCount;
    const HRESULT presentResult = [&]() noexcept
    {
        const ScopedUiTimingAttribution phase{UiTimingKind::Frame, UiTimingAttributionPhase::Operation, 1005};
        return StageResult(Stage::Present, _swapChain->Present(1, 0));
    }();
    _lastPresentResult = presentResult;
    if (FAILED(presentResult) && DeviceLostAfter(presentResult))
    {
        return RecoverFromStage();
    }
    if (FAILED(presentResult))
    {
        _failedStage = Stage::Present;
        return presentResult;
    }
    _occluded = presentResult == DXGI_STATUS_OCCLUDED;
    if (!_occluded)
    {
        ++_presentCount;
    }
    return S_OK;
}

HRESULT Renderer::RecoverDeviceForTest() noexcept
{
    return RecoverDevice();
}

HRESULT Renderer::ProbeOcclusion() noexcept
{
    if (!_swapChain || !_occluded)
    {
        return E_UNEXPECTED;
    }
    // A registration that failed is retried here, so the next occlusion can wait for its message again.
    RegisterOcclusionStatus();

    const HRESULT result = _swapChain->Present(0, DXGI_PRESENT_TEST);
    if (FAILED(result) && DeviceLostAfter(result))
    {
        // The rebuilt swap chain is not occluded: the next frame draws.
        return RecoverDevice();
    }
    if (result == DXGI_STATUS_OCCLUDED)
    {
        return S_FALSE;
    }
    if (FAILED(result))
    {
        return result;
    }

    _occluded = false;
    return S_OK;
}

bool Renderer::IsSuspended() const noexcept
{
    return _suspended;
}

bool Renderer::IsOccluded() const noexcept
{
    return _occludedForTest.value_or(_occluded);
}

bool Renderer::IsWarp() const noexcept
{
    return _deviceInfo.warp;
}

HRESULT Renderer::RecoverDevice() noexcept
{
    OutputDebugStringW(L"Direct3D device was lost; rebuilding graphics resources.\n");
    MonitorDiagnostics::Write(RedMonitor::Severity::Warning, L"Direct3D device lost; rebuilding graphics resources");
    return CreateDeviceResources();
}

void Renderer::ReleaseDeviceResources() noexcept
{
    // An outstanding Present keeps the HWND bound to the swap chain. Wait it out before the handles go.
    if (_frameLatencyWaitable)
    {
        (void)WaitForSingleObject(_frameLatencyWaitable.get(), 1000);
    }
    if (_factory && _occlusionStatusRegistered)
    {
        _factory->UnregisterOcclusionStatus(_occlusionStatusCookie);
        _occlusionStatusCookie = 0;
        _occlusionStatusRegistered = false;
    }
    if (_context)
    {
        _context->ClearState();
        _context->Flush();
    }

    _renderTarget.reset();
    _frameLatencyWaitable.reset();
    _swapChain.reset();
    _factory.reset();
    _context1.reset();
    _context.reset();
    _device.reset();
    _deviceInfo = {};
    _deviceMonitor = nullptr;
    _width = 0;
    _height = 0;
    _suspended = true;
    _occluded = false;
}

bool Renderer::IsDeviceLost(HRESULT result) noexcept
{
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

#include "Application.h"

#include "ChromeHost.h"
#include "FrameScheduler.h"
#include "WindowCapture.h"

#include <cstring>
#include <new>
#include <powrprof.h>
#include <windowsx.h>

namespace
{
constexpr int kDefaultClientWidth = 1280;
constexpr int kDefaultClientHeight = 800;
constexpr UINT kScreenshotDelayMilliseconds = 300;

[[nodiscard]] SIZE ScaleDefaultClientSize(UINT dpi) noexcept
{
    return SIZE{MulDiv(kDefaultClientWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                MulDiv(kDefaultClientHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
}

HRESULT CalculateWindowSizeForDpi(DWORD windowStyle, DWORD extendedStyle, UINT dpi, SIZE& windowSize) noexcept
{
    if (dpi == 0)
    {
        return E_INVALIDARG;
    }

    const SIZE clientSize = ScaleDefaultClientSize(dpi);
    RECT bounds{0, 0, clientSize.cx, clientSize.cy};
    if (!AdjustWindowRectExForDpi(&bounds, windowStyle, FALSE, extendedStyle, dpi))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    windowSize = SIZE{bounds.right - bounds.left, bounds.bottom - bounds.top};
    return S_OK;
}

[[nodiscard]] UINT KeyModifiers() noexcept
{
    UINT modifiers = 0;
    if (GetKeyState(VK_SHIFT) < 0)
    {
        modifiers |= MK_SHIFT;
    }
    if (GetKeyState(VK_CONTROL) < 0)
    {
        modifiers |= MK_CONTROL;
    }
    return modifiers;
}
} // namespace

Application::Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath) noexcept
    : _instance(instance), _forceWarp(forceWarp)
{
    if (screenshotPath && screenshotPath[0] != L'\0')
    {
        _screenshotPath = screenshotPath;
    }
}

Application::~Application()
{
    _displayPowerNotification.reset();
    CloseMainWindow();
    if (_classRegistered)
    {
        UnregisterClassW(kWindowClassName, _instance);
        _classRegistered = false;
    }
}

void Application::RequestFrameThunk(void* context) noexcept
{
    auto* application = static_cast<Application*>(context);
    if (application)
    {
        application->_frameInvalidated = true;
    }
}

int Application::Run(int showCommand) noexcept
{
    HRESULT result = RegisterWindowClass();
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterWindowClass failed.\n");
        return 2;
    }

    result = CreateMainWindow(true);
    if (FAILED(result))
    {
        OutputDebugStringW(L"CreateMainWindow failed.\n");
        return 2;
    }

    result = InitializeRenderer();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Renderer initialization failed.\n");
        return 5;
    }

    result = InitializeChrome();
    if (FAILED(result))
    {
        OutputDebugStringW(L"DxUi chrome initialization failed.\n");
        return 5;
    }

    if (!_screenshotPath.empty())
    {
        ShowWindow(_window.get(), SW_SHOWNOACTIVATE);
    }
    else
    {
        ShowWindow(_window.get(), showCommand);
    }

    MSG message{};
    while (_window)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                if (!_screenshotPath.empty())
                {
                    return FAILED(_screenshotResult) ? 8 : 0;
                }
                return FAILED(_runtimeFailure) ? 5 : static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!_window)
        {
            break;
        }

        if (!_renderer.IsOccluded())
        {
            _occlusionStatusChanged = false;
        }

        const bool chromeAnimating = _chrome && _chrome->NeedsAnimation();
        if (_chrome && _chrome->NeedsPreparation())
        {
            _frameInvalidated = true;
        }

        HostFrameAction frameAction = SelectHostFrameAction(HostFrameState{
            _windowVisible,
            _displayPoweredOn,
            _renderer.IsSuspended(),
            _renderer.IsOccluded(),
            _occlusionStatusChanged,
            _frameInvalidated,
            chromeAnimating,
        });
        if (frameAction == HostFrameAction::ProbeOcclusion)
        {
            _occlusionStatusChanged = false;
            result = _renderer.ProbeOcclusion();
            if (FAILED(result))
            {
                _runtimeFailure = result;
                OutputDebugStringW(L"Swap-chain occlusion probe failed.\n");
                CloseMainWindow();
                continue;
            }
            if (!_renderer.IsOccluded())
            {
                _frameInvalidated = true;
            }
            frameAction = SelectHostFrameAction(HostFrameState{
                _windowVisible,
                _displayPoweredOn,
                _renderer.IsSuspended(),
                _renderer.IsOccluded(),
                false,
                _frameInvalidated,
                _chrome && _chrome->NeedsAnimation(),
            });
        }

        if (frameAction == HostFrameAction::WaitForMessage)
        {
            (void)WaitUntilMessage();
            continue;
        }

        if (!WaitForFrameLatency())
        {
            continue;
        }

        if (_chrome)
        {
            (void)_chrome->AdvanceAnimation(GetTickCount64());
        }
        result = _renderer.Render(_chrome.get());
        if (FAILED(result))
        {
            _runtimeFailure = result;
            OutputDebugStringW(L"Frame rendering failed.\n");
            CloseMainWindow();
            continue;
        }
        _frameInvalidated = false;
        if (!_screenshotPath.empty() && !_screenshotArmed && _window)
        {
            _screenshotArmed = true;
            (void)SetTimer(_window.get(), kScreenshotTimerId, kScreenshotDelayMilliseconds, nullptr);
        }
    }

    if (!_screenshotPath.empty())
    {
        return FAILED(_screenshotResult) ? 8 : 0;
    }
    return FAILED(_runtimeFailure) ? 5 : 0;
}

int Application::RunSelfTest() noexcept
{
    HRESULT result = RegisterWindowClass();
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterWindowClass failed.\n");
        return 2;
    }

    result = CreateMainWindow(false);
    if (FAILED(result))
    {
        OutputDebugStringW(L"CreateMainWindow failed.\n");
        return 2;
    }

    RECT clientBounds{};
    const UINT windowDpi = GetDpiForWindow(_window.get());
    const SIZE expectedClientSize = ScaleDefaultClientSize(windowDpi);
    const DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrW(_window.get(), GWL_STYLE));
    const DWORD windowExtendedStyle = static_cast<DWORD>(GetWindowLongPtrW(_window.get(), GWL_EXSTYLE));
    if (windowDpi == 0 || (windowStyle & WS_CLIPCHILDREN) == 0 ||
        (windowExtendedStyle & WS_EX_NOREDIRECTIONBITMAP) == 0 || !GetClientRect(_window.get(), &clientBounds) ||
        clientBounds.right - clientBounds.left != expectedClientSize.cx ||
        clientBounds.bottom - clientBounds.top != expectedClientSize.cy)
    {
        OutputDebugStringW(L"The default client area does not match the current monitor DPI.\n");
        return 2;
    }

    result = InitializeRenderer();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Renderer initialization failed.\n");
        return 5;
    }
    if (!_renderer.IsWarp())
    {
        OutputDebugStringW(L"Self-test requires a WARP device.\n");
        return 5;
    }

    result = InitializeChrome();
    if (FAILED(result))
    {
        OutputDebugStringW(L"DxUi chrome initialization failed.\n");
        return 5;
    }

    result = _renderer.Render(_chrome.get());
    if (FAILED(result))
    {
        OutputDebugStringW(L"Self-test Present failed.\n");
        return 5;
    }
    const DxUi::EmbeddedStatistics visible = _chrome->Statistics();
    if (visible.surfaceBytes == 0 || visible.composites == 0)
    {
        OutputDebugStringW(L"Visible DxUi chrome did not allocate a surface or composite.\n");
        return 5;
    }
    if (visible.surfaceBytes > (64ull * 1024ull * 1024ull))
    {
        OutputDebugStringW(L"Visible DxUi chrome exceeded the 64 MiB surface cap.\n");
        return 5;
    }

    _chrome->SetVisible(false);
    const DxUi::EmbeddedStatistics hidden = _chrome->Statistics();
    if (hidden.surfaceBytes != 0)
    {
        OutputDebugStringW(L"Hidden DxUi chrome retained a cached surface.\n");
        return 5;
    }

    CloseMainWindow();
    return 0;
}

HRESULT Application::RegisterWindowClass() noexcept
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = _instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&windowClass))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    _classRegistered = true;
    return S_OK;
}

HRESULT Application::CreateMainWindow(bool visible) noexcept
{
    constexpr DWORD extendedStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

    const UINT dpi = GetDpiForSystem();
    SIZE windowSize{};
    HRESULT result = CalculateWindowSizeForDpi(windowStyle, extendedStyle, dpi, windowSize);
    if (FAILED(result))
    {
        return result;
    }

    const HWND window = CreateWindowExW(extendedStyle, kWindowClassName, L"RedPrism", windowStyle, CW_USEDEFAULT,
                                        CW_USEDEFAULT, windowSize.cx, windowSize.cy, nullptr, nullptr, _instance, this);
    if (!window)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const UINT windowDpi = GetDpiForWindow(window);
    result = CalculateWindowSizeForDpi(windowStyle, extendedStyle, windowDpi, windowSize);
    if (FAILED(result))
    {
        return result;
    }
    if (!SetWindowPos(window, nullptr, 0, 0, windowSize.cx, windowSize.cy, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    result = RegisterDisplayPowerNotification(window);
    if (FAILED(result))
    {
        return result;
    }

    if (!visible)
    {
        ShowWindow(window, SW_HIDE);
    }
    return S_OK;
}

HRESULT Application::RegisterDisplayPowerNotification(HWND window) noexcept
{
    _displayPowerNotification.reset(
        RegisterPowerSettingNotification(window, &GUID_SESSION_DISPLAY_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE));
    if (!_displayPowerNotification)
    {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    return S_OK;
}

HRESULT Application::InitializeRenderer() noexcept
{
    const HRESULT result = _renderer.Initialize(_window.get(), _forceWarp);
    if (FAILED(result))
    {
        return result;
    }
    _rendererReady = true;
    _frameInvalidated = true;
    return S_OK;
}

HRESULT Application::InitializeChrome() noexcept
{
    auto chrome = std::unique_ptr<ChromeHost>(new (std::nothrow) ChromeHost());
    if (!chrome)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = chrome->Attach(_renderer.Device(), this, &Application::RequestFrameThunk);
    if (FAILED(result))
    {
        return result;
    }
    _chrome = std::move(chrome);
    _renderer.SetChrome(_chrome.get());
    _frameInvalidated = true;
    return S_OK;
}

void Application::CloseMainWindow() noexcept
{
    if (_window)
    {
        (void)KillTimer(_window.get(), kScreenshotTimerId);
    }
    _renderer.SetChrome(nullptr);
    if (_chrome)
    {
        _chrome->Detach();
    }
    _chrome.reset();
    _renderer.Shutdown();
    _rendererReady = false;
    _window.reset();
}

bool Application::WaitUntilMessage() noexcept
{
    const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
    _runtimeFailure = error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    CloseMainWindow();
    return false;
}

bool Application::WaitForFrameLatency() noexcept
{
    const HANDLE waitable = _renderer.FrameLatencyWaitableObject();
    if (!waitable)
    {
        return true;
    }
    const DWORD waitResult = MsgWaitForMultipleObjectsEx(1, &waitable, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_OBJECT_0 + 1)
    {
        return false;
    }
    const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
    _runtimeFailure = error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    CloseMainWindow();
    return false;
}

void Application::CheckDeviceAdapter() noexcept
{
    if (!_rendererReady || !_window)
    {
        return;
    }
    const HRESULT result = _renderer.EnsureDeviceForWindowMonitor();
    if (result == S_OK)
    {
        _frameInvalidated = true;
        return;
    }
    if (FAILED(result))
    {
        _runtimeFailure = result;
        OutputDebugStringW(L"Direct3D device could not follow the window to its new adapter.\n");
        PostMessageW(_window.get(), WM_CLOSE, 0, 0);
    }
}

void Application::OnPointer(UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (!_chrome)
    {
        return;
    }

    DxUi::PointerEvent event{};
    event.modifiers = GET_KEYSTATE_WPARAM(wParam) | KeyModifiers();
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (message == WM_MOUSEWHEEL)
    {
        POINT screen{point};
        if (!ScreenToClient(_window.get(), &screen))
        {
            return;
        }
        point = screen;
        event.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam));
        event.action = DxUi::PointerAction::Wheel;
    }
    else if (message == WM_MOUSELEAVE)
    {
        event.action = DxUi::PointerAction::Leave;
        _mouseTracked = false;
    }
    else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN)
    {
        event.action = DxUi::PointerAction::Down;
        (void)SetCapture(_window.get());
    }
    else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP)
    {
        event.action = DxUi::PointerAction::Up;
        if (GetCapture() == _window.get())
        {
            ReleaseCapture();
        }
    }
    else
    {
        event.action = DxUi::PointerAction::Move;
        if (!_mouseTracked)
        {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = _window.get();
            _mouseTracked = TrackMouseEvent(&track) != FALSE;
        }
    }
    event.xPixels = static_cast<float>(point.x);
    event.yPixels = static_cast<float>(point.y);
    if (_chrome->DispatchPointer(event))
    {
        _frameInvalidated = true;
    }
}

LRESULT CALLBACK Application::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        application->_window.reset(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }

    if (application)
    {
        return application->HandleMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_SIZE:
        return OnSize(window, LOWORD(lParam), HIWORD(lParam));
    case WM_DPICHANGED:
    {
        const LRESULT dpiResult = OnDpiChanged(window, reinterpret_cast<const RECT*>(lParam));
        CheckDeviceAdapter();
        return dpiResult;
    }
    case WM_EXITSIZEMOVE:
        CheckDeviceAdapter();
        break;
    case WM_DISPLAYCHANGE:
        CheckDeviceAdapter();
        break;
    case WM_SHOWWINDOW:
        _windowVisible = wParam != FALSE;
        if (_chrome)
        {
            _chrome->SetVisible(_windowVisible);
        }
        if (_windowVisible)
        {
            _frameInvalidated = true;
        }
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSELEAVE:
        OnPointer(message, wParam, lParam);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (_chrome && _chrome->DispatchKey(static_cast<UINT>(wParam), true, KeyModifiers()))
        {
            _frameInvalidated = true;
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (_chrome && _chrome->DispatchKey(static_cast<UINT>(wParam), false, KeyModifiers()))
        {
            _frameInvalidated = true;
        }
        break;
    case WM_CHAR:
        if (_chrome && _chrome->DispatchCharacter(static_cast<wchar_t>(wParam), KeyModifiers()))
        {
            _frameInvalidated = true;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kScreenshotTimerId && !_screenshotPath.empty() && _window)
        {
            (void)KillTimer(window, kScreenshotTimerId);
            _screenshotResult = RedPrism::SaveWindowScreenshot(window, _screenshotPath.c_str());
            if (FAILED(_screenshotResult))
            {
                OutputDebugStringW(L"Screenshot capture failed.\n");
            }
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0)
        {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (setting && setting->PowerSetting == GUID_SESSION_DISPLAY_STATUS && setting->DataLength >= sizeof(DWORD))
            {
                DWORD displayStatus = 0;
                std::memcpy(&displayStatus, setting->Data, sizeof(displayStatus));
                _displayPoweredOn = displayStatus != 0;
                if (_displayPoweredOn)
                {
                    _frameInvalidated = true;
                }
            }
        }
        return TRUE;
    case Renderer::kOcclusionStatusMessage:
        _occlusionStatusChanged = true;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        _frameInvalidated = true;
        ValidateRect(window, nullptr);
        return 0;
    case WM_CLOSE:
        CloseMainWindow();
        return 0;
    case WM_DESTROY:
        _displayPowerNotification.reset();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (_window.get() == window)
        {
            (void)_window.release();
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::OnSize(HWND window, UINT width, UINT height) noexcept
{
    if (!_rendererReady)
    {
        return 0;
    }

    const HRESULT result = _renderer.Resize(width, height);
    if (SUCCEEDED(result) && width != 0 && height != 0)
    {
        _frameInvalidated = true;
    }
    if (FAILED(result))
    {
        _runtimeFailure = result;
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return 0;
}

LRESULT Application::OnDpiChanged(HWND window, const RECT* suggestedBounds) noexcept
{
    if (!suggestedBounds)
    {
        return 0;
    }
    if (!SetWindowPos(window, nullptr, suggestedBounds->left, suggestedBounds->top,
                      suggestedBounds->right - suggestedBounds->left, suggestedBounds->bottom - suggestedBounds->top,
                      SWP_NOZORDER | SWP_NOACTIVATE))
    {
        return 0;
    }
    _frameInvalidated = true;
    return 0;
}

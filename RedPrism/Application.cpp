#include "Application.h"

#include "ChromeHost.h"
#include "FrameScheduler.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "WindowCapture.h"

#include <cstring>
#include <format>
#include <new>
#include <powrprof.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <string_view>
#include <windowsx.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr int kDefaultClientWidth = 1280;
constexpr int kDefaultClientHeight = 800;
constexpr UINT kScreenshotDelayMilliseconds = 800;

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

[[nodiscard]] std::wstring FileTitle(const std::wstring& path)
{
    if (path.empty())
    {
        return L"Untitled";
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] bool PickOpenPath(HWND owner, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    const COMDLG_FILTERSPEC filters[] = {
        {L"RedPrism project", L"*.prism"},
        {L"Images", L"*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.heic;*.heif"},
        {L"All files", L"*.*"},
    };
    (void)dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    FILEOPENDIALOGOPTIONS options = 0;
    (void)dialog->GetOptions(&options);
    (void)dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    result = dialog->Show(owner);
    if (FAILED(result))
    {
        return false;
    }
    wil::com_ptr_nothrow<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
    {
        return false;
    }
    wil::unique_cotaskmem_string file;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, file.put())))
    {
        return false;
    }
    path = file.get();
    return !path.empty();
}

[[nodiscard]] bool PickSavePath(HWND owner, const wchar_t* defaultName, const wchar_t* defaultExt,
                                const COMDLG_FILTERSPEC* filters, UINT filterCount, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IFileSaveDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    if (filters && filterCount != 0)
    {
        (void)dialog->SetFileTypes(filterCount, filters);
    }
    if (defaultExt)
    {
        (void)dialog->SetDefaultExtension(defaultExt);
    }
    if (defaultName)
    {
        (void)dialog->SetFileName(defaultName);
    }
    FILEOPENDIALOGOPTIONS options = 0;
    (void)dialog->GetOptions(&options);
    (void)dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    result = dialog->Show(owner);
    if (FAILED(result))
    {
        return false;
    }
    wil::com_ptr_nothrow<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
    {
        return false;
    }
    wil::unique_cotaskmem_string file;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, file.put())))
    {
        return false;
    }
    path = file.get();
    return !path.empty();
}

[[nodiscard]] EditorTool ToolFromVirtualKey(UINT virtualKey) noexcept
{
    switch (virtualKey)
    {
    case 'V':
        return EditorTool::Move;
    case 'M':
        return EditorTool::Marquee;
    case 'L':
        return EditorTool::Lasso;
    case 'W':
        return EditorTool::Wand;
    case 'C':
        return EditorTool::Crop;
    case 'B':
        return EditorTool::Brush;
    case 'E':
        return EditorTool::Eraser;
    case 'J':
        return EditorTool::Heal;
    case 'S':
        return EditorTool::Clone;
    case 'R':
        return EditorTool::Smear;
    case 'G':
        return EditorTool::Gradient;
    case 'U':
        return EditorTool::Shape;
    case 'I':
        return EditorTool::Eyedropper;
    case 'H':
        return EditorTool::Hand;
    case 'Z':
        return EditorTool::Zoom;
    case 'A':
        return EditorTool::Idle;
    default:
        return EditorTool::Idle;
    }
}

[[nodiscard]] bool IsToolVirtualKey(UINT virtualKey) noexcept
{
    switch (virtualKey)
    {
    case 'V':
    case 'M':
    case 'L':
    case 'W':
    case 'C':
    case 'B':
    case 'E':
    case 'J':
    case 'S':
    case 'R':
    case 'G':
    case 'U':
    case 'I':
    case 'H':
    case 'Z':
    case 'A':
        return true;
    default:
        return false;
    }
}
} // namespace

Application::Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath,
                         uint32_t screenshotDpi) noexcept
    : _instance(instance), _forceWarp(forceWarp)
{
    if (screenshotPath && screenshotPath[0] != L'\0')
    {
        _screenshotPath = screenshotPath;
        _quietUi = true;
    }
    if (screenshotDpi == 96 || screenshotDpi == 144)
    {
        _screenshotDpi = screenshotDpi;
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

    result = InitializeDocument();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Document initialization failed.\n");
        return 5;
    }

    if (!_screenshotPath.empty())
    {
        ShowWindow(_window.get(), SW_SHOWNOACTIVATE);
        _windowVisible = true;
        _frameInvalidated = true;
        if (_chrome)
        {
            _chrome->SetVisible(true);
        }
        if (!_screenshotArmed)
        {
            _screenshotArmed = true;
            (void)SetTimer(_window.get(), kScreenshotTimerId, kScreenshotDelayMilliseconds, nullptr);
        }
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

        if (!_renderer.IsOccluded() || !_screenshotPath.empty())
        {
            _occlusionStatusChanged = false;
        }

        const bool chromeAnimating = _chrome && _chrome->NeedsAnimation();
        if (_chrome && _chrome->NeedsPreparation())
        {
            _frameInvalidated = true;
        }

        const bool forceCaptureFrame = !_screenshotPath.empty();
        HostFrameAction frameAction = SelectHostFrameAction(HostFrameState{
            _windowVisible || forceCaptureFrame,
            _displayPoweredOn,
            _renderer.IsSuspended() && !forceCaptureFrame,
            _renderer.IsOccluded() && !forceCaptureFrame,
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
                _windowVisible || forceCaptureFrame,
                _displayPoweredOn,
                _renderer.IsSuspended() && !forceCaptureFrame,
                _renderer.IsOccluded() && !forceCaptureFrame,
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
        BindCompositionDevice();
        const CanvasDocument* document = _session.Document().has_value() ? &_session.Document().value() : nullptr;
        result = _renderer.Render(_chrome.get(), &_composition, &_viewport, document);
        if (FAILED(result))
        {
            BindCompositionDevice();
            result = _renderer.Render(_chrome.get(), &_composition, &_viewport, document);
        }
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
    _screenshotDpi = 0;
    _renderer.SetLayoutDpi(0);
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
    result = InitializeDocument();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Document initialization failed.\n");
        return 5;
    }

    BindCompositionDevice();
    SyncChrome();
    const CanvasDocument* document = _session.Document().has_value() ? &_session.Document().value() : nullptr;
    result = _renderer.Render(_chrome.get(), &_composition, &_viewport, document);
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
    _composition.ReleaseDocumentSurfaces();
    const DxUi::EmbeddedStatistics hidden = _chrome->Statistics();
    if (hidden.surfaceBytes != 0)
    {
        OutputDebugStringW(L"Hidden DxUi chrome retained a cached surface.\n");
        return 5;
    }
    if (_composition.CanvasSurfaceBytes() != 0)
    {
        OutputDebugStringW(L"Hidden composition retained canvas surfaces.\n");
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

    const UINT dpi = _screenshotDpi != 0 ? _screenshotDpi : GetDpiForSystem();
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

    const UINT windowDpi = _screenshotDpi != 0 ? _screenshotDpi : GetDpiForWindow(window);
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

    DragAcceptFiles(window, TRUE);
    if (_screenshotDpi != 0)
    {
        _renderer.SetLayoutDpi(static_cast<float>(_screenshotDpi));
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
    chrome->SetOwnerWindow(_window.get());
    chrome->SetCommandHandler([this](int command) { OnCommand(command); });
    chrome->SetToolHandler([this](EditorTool tool) { OnTool(tool); });
    chrome->SetLayerHandler(
        [this](Guid id)
        {
            _session.SelectLayer(id);
            SyncChrome();
            _frameInvalidated = true;
        });
    _chrome = std::move(chrome);
    _renderer.SetChrome(_chrome.get());
    _frameInvalidated = true;
    return S_OK;
}

HRESULT Application::InitializeDocument() noexcept
{
    _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    _session.history.MarkSaved();
    _documentPath.clear();
    _tool = EditorTool::Move;
    if (_chrome)
    {
        _chrome->SetTool(_tool);
    }
    BindCompositionDevice();
    SyncChrome();
    return S_OK;
}

void Application::SyncChrome() noexcept
{
    if (!_chrome)
    {
        return;
    }
    const bool dirty = _session.history.IsModified();
    _chrome->SetDocumentTitle(FileTitle(_documentPath), dirty);
    _chrome->SetLayers(_session.LayerRows(), _session.ActiveLayerID());
    const CanvasDocument* document = _session.Document().has_value() ? &_session.Document().value() : nullptr;
    const int percent = static_cast<int>(_viewport.zoom * 100.0 + 0.5);
    const std::wstring zoom = std::format(L"{}%", percent);
    const std::wstring size =
        document ? std::format(L"{} × {}", document->width, document->height) : std::wstring{L"—"};
    const std::wstring tool = std::format(L"{} ({})", EditorToolName(_tool), EditorToolKey(_tool));
    _chrome->SetStatus(zoom, size, tool);
    _chrome->SetTool(_tool);

    const float dpi = _screenshotDpi != 0 ? static_cast<float>(_screenshotDpi)
                                          : (_window ? static_cast<float>(GetDpiForWindow(_window.get())) : 96.0f);
    (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), dpi > 0.0f ? dpi : 96.0f);
    const CanvasHole hole = _chrome->HolePixels();
    const Vec2 documentSize =
        document ? Vec2{static_cast<double>(document->width), static_cast<double>(document->height)} : Vec2{1, 1};
    _viewport.Resize(Vec2{std::max(1.0f, hole.width), std::max(1.0f, hole.height)}, std::max(1.0f, dpi / 96.0f),
                     documentSize);
}

void Application::BindCompositionDevice() noexcept
{
    ID3D11Device* device = _renderer.Device();
    ID3D11DeviceContext* context = _renderer.Context();
    if (!device || !context)
    {
        _composition.ReleaseDeviceResources();
        return;
    }
    if (_composition.Device() != device)
    {
        (void)_composition.CreateDeviceResources(device, context);
    }
}

void Application::CloseMainWindow() noexcept
{
    if (_window)
    {
        (void)KillTimer(_window.get(), kScreenshotTimerId);
    }
    _renderer.SetChrome(nullptr);
    _composition.ReleaseDeviceResources();
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
        BindCompositionDevice();
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
        _canvasDrag = false;
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
        return;
    }

    const CanvasHole hole = _chrome->HolePixels();
    const bool inHole = event.xPixels >= hole.left && event.yPixels >= hole.top &&
                        event.xPixels < hole.left + hole.width && event.yPixels < hole.top + hole.height;
    const Vec2 documentSize = _session.Document().has_value()
                                  ? Vec2{static_cast<double>(_session.Document().value().width),
                                         static_cast<double>(_session.Document().value().height)}
                                  : Vec2{1, 1};
    const Vec2 view{event.xPixels - hole.left, event.yPixels - hole.top};
    const bool panMode = _spacePan || _tool == EditorTool::Hand;
    if (event.action == DxUi::PointerAction::Wheel && inHole)
    {
        const double factor = event.wheelDelta > 0 ? 1.1 : (1.0 / 1.1);
        _viewport.SetZoom(_viewport.zoom * factor, view, documentSize);
        SyncChrome();
        _frameInvalidated = true;
        return;
    }
    if (event.action == DxUi::PointerAction::Down && inHole)
    {
        _canvasDrag = true;
        _lastPointer = point;
        _frameInvalidated = true;
        return;
    }
    if (event.action == DxUi::PointerAction::Move && _canvasDrag)
    {
        const Vec2 delta{static_cast<double>(point.x - _lastPointer.x), static_cast<double>(point.y - _lastPointer.y)};
        if (panMode)
        {
            _viewport.Translate(delta);
            SyncChrome();
            _frameInvalidated = true;
        }
        else if (_tool == EditorTool::Zoom)
        {
            const double factor = delta.y < 0 ? 1.02 : (delta.y > 0 ? (1.0 / 1.02) : 1.0);
            if (factor != 1.0)
            {
                _viewport.SetZoom(_viewport.zoom * factor, view, documentSize);
                SyncChrome();
                _frameInvalidated = true;
            }
        }
        _lastPointer = point;
        return;
    }
    if (event.action == DxUi::PointerAction::Up)
    {
        _canvasDrag = false;
    }
}

void Application::OnCommand(int command) noexcept
{
    switch (command)
    {
    case ChromeHost::kCommandNew:
        NewDocument();
        break;
    case ChromeHost::kCommandOpen:
        OpenDocument();
        break;
    case ChromeHost::kCommandSave:
        SaveDocument(false);
        break;
    case ChromeHost::kCommandSaveAs:
        SaveDocument(true);
        break;
    case ChromeHost::kCommandExportPng:
        ExportFlattened(false);
        break;
    case ChromeHost::kCommandExportJpeg:
        ExportFlattened(true);
        break;
    case ChromeHost::kCommandUndo:
        _session.Undo();
        SyncChrome();
        _frameInvalidated = true;
        break;
    case ChromeHost::kCommandRedo:
        _session.Redo();
        SyncChrome();
        _frameInvalidated = true;
        break;
    default:
        break;
    }
}

void Application::OnTool(EditorTool tool) noexcept
{
    _tool = tool;
    if (_chrome)
    {
        _chrome->SetTool(tool);
    }
    SyncChrome();
    _frameInvalidated = true;
}

bool Application::HandleAccelerator(UINT virtualKey, UINT modifiers) noexcept
{
    const bool control = (modifiers & MK_CONTROL) != 0;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    if (control && virtualKey == 'N')
    {
        NewDocument();
        return true;
    }
    if (control && virtualKey == 'O')
    {
        OpenDocument();
        return true;
    }
    if (control && virtualKey == 'S')
    {
        SaveDocument(shift);
        return true;
    }
    if (control && virtualKey == 'Z')
    {
        if (shift)
        {
            _session.Redo();
        }
        else
        {
            _session.Undo();
        }
        SyncChrome();
        _frameInvalidated = true;
        return true;
    }
    if (control && virtualKey == 'Y')
    {
        _session.Redo();
        SyncChrome();
        _frameInvalidated = true;
        return true;
    }
    if (virtualKey == VK_SPACE)
    {
        _spacePan = true;
        return true;
    }
    if (!control && _chrome && _chrome->CanvasFocused() && IsToolVirtualKey(virtualKey))
    {
        OnTool(ToolFromVirtualKey(virtualKey));
        return true;
    }
    return false;
}

void Application::NewDocument() noexcept
{
    _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    _session.history.MarkSaved();
    _documentPath.clear();
    _viewport.followsFit = true;
    SyncChrome();
    _frameInvalidated = true;
}

void Application::OpenDocument() noexcept
{
    if (!_window)
    {
        return;
    }
    std::wstring path;
    if (!PickOpenPath(_window.get(), path))
    {
        return;
    }
    OpenPath(path.c_str());
}

void Application::OpenPath(const wchar_t* path) noexcept
{
    if (path == nullptr || path[0] == L'\0')
    {
        return;
    }
    const std::wstring_view view{path};
    const DWORD attributes = GetFileAttributesW(path);
    const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool project =
        PathLooksLikeZipProject(path) || view.ends_with(L".prism") || view.ends_with(L".PRISM") || directory;
    if (project)
    {
        const ProjectLoadResult loaded = LoadProject(path);
        if (loaded.status != ProjectIoStatus::Ok || !loaded.document.width)
        {
            if (!_quietUi)
            {
                MessageBoxW(_window.get(), L"RedPrism could not open that project.", L"RedPrism", MB_OK | MB_ICONERROR);
            }
            return;
        }
        _session.history.Reset();
        _session.Document() = loaded.document;
        _session.SelectLayer(loaded.activeLayerID);
        _session.history.MarkSaved();
        _documentPath = path;
        _viewport.followsFit = true;
        SyncChrome();
        _frameInvalidated = true;
        return;
    }

    if (!_session.Document().has_value())
    {
        _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    }
    std::optional<Guid> active = _session.ActiveLayerID();
    const ProjectIoStatus status = ImportImageOnto(_session.Document().value(), path, active);
    if (status != ProjectIoStatus::Ok)
    {
        if (!_quietUi)
        {
            MessageBoxW(_window.get(), L"RedPrism could not import that image.", L"RedPrism", MB_OK | MB_ICONERROR);
        }
        return;
    }
    _session.SelectLayer(active);
    _viewport.followsFit = true;
    SyncChrome();
    _frameInvalidated = true;
}

void Application::SaveDocument(bool saveAs) noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    std::wstring path = _documentPath;
    if (saveAs || path.empty())
    {
        if (!_window)
        {
            return;
        }
        const COMDLG_FILTERSPEC filters[] = {
            {L"RedPrism project", L"*.prism"},
            {L"Exploded project folder", L"*.*"},
        };
        if (!PickSavePath(_window.get(), L"Untitled.prism", L"prism", filters, 2, path))
        {
            return;
        }
    }
    const bool zip = PathLooksLikeZipProject(path.c_str()) || std::wstring_view{path}.ends_with(L".prism") ||
                     std::wstring_view{path}.ends_with(L".PRISM");
    const ProjectIoStatus status =
        SaveProject(path.c_str(), _session.Document().value(), _session.ActiveLayerID(), zip);
    if (status != ProjectIoStatus::Ok)
    {
        if (!_quietUi)
        {
            MessageBoxW(_window.get(), L"RedPrism could not save the project.", L"RedPrism", MB_OK | MB_ICONERROR);
        }
        return;
    }
    _documentPath = std::move(path);
    _session.history.MarkSaved();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::ExportFlattened(bool jpeg) noexcept
{
    if (!_session.Document().has_value() || !_window)
    {
        return;
    }
    BindCompositionDevice();
    if (FAILED(_composition.Compose(_session.Document().value())))
    {
        if (!_quietUi)
        {
            MessageBoxW(_window.get(), L"RedPrism could not flatten the document.", L"RedPrism", MB_OK | MB_ICONERROR);
        }
        return;
    }
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(_composition.Readback(bgra, width, height)) || width == 0 || height == 0)
    {
        if (!_quietUi)
        {
            MessageBoxW(_window.get(), L"RedPrism could not read the flattened image.", L"RedPrism",
                        MB_OK | MB_ICONERROR);
        }
        return;
    }
    const Raster raster = Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888,
                                       bgra.data(), bgra.size());
    std::wstring path;
    if (jpeg)
    {
        const COMDLG_FILTERSPEC filters[] = {{L"JPEG image", L"*.jpg;*.jpeg"}};
        if (!PickSavePath(_window.get(), L"Export.jpg", L"jpg", filters, 1, path))
        {
            return;
        }
        if (ExportDocumentJpeg(raster, JpegExportOptions{0.8, _session.Document().value().resolution}, path.c_str()) !=
            ProjectIoStatus::Ok)
        {
            if (!_quietUi)
            {
                MessageBoxW(_window.get(), L"RedPrism could not export JPEG.", L"RedPrism", MB_OK | MB_ICONERROR);
            }
        }
        return;
    }
    const COMDLG_FILTERSPEC filters[] = {{L"PNG image", L"*.png"}};
    if (!PickSavePath(_window.get(), L"Export.png", L"png", filters, 1, path))
    {
        return;
    }
    if (ExportDocumentPng(raster, _session.Document().value().resolution, path.c_str()) != ProjectIoStatus::Ok)
    {
        if (!_quietUi)
        {
            MessageBoxW(_window.get(), L"RedPrism could not export PNG.", L"RedPrism", MB_OK | MB_ICONERROR);
        }
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
        if (!_windowVisible)
        {
            _composition.ReleaseDocumentSurfaces();
        }
        if (_windowVisible)
        {
            _frameInvalidated = true;
        }
        return 0;
    case WM_DROPFILES:
    {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH) != 0)
        {
            OpenPath(path);
        }
        DragFinish(drop);
        return 0;
    }
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
        if (HandleAccelerator(static_cast<UINT>(wParam), KeyModifiers()))
        {
            return 0;
        }
        if (_chrome && _chrome->DispatchKey(static_cast<UINT>(wParam), true, KeyModifiers()))
        {
            _frameInvalidated = true;
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (static_cast<UINT>(wParam) == VK_SPACE)
        {
            _spacePan = false;
            return 0;
        }
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
        SyncChrome();
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
    SyncChrome();
    _frameInvalidated = true;
    return 0;
}

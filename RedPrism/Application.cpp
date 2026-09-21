#include "Application.h"

#include "Adjust.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "FrameScheduler.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "Resources.h"
#include "Selection.h"
#include "SystemClipboard.h"
#include "Version.h"
#include "WarpStroke.h"
#include "WindowCapture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <new>
#include <powrprof.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <string_view>
#include <vector>
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
constexpr double kWheelZoomStep = 1.1;
constexpr double kCommandZoomStep = 1.25;

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
        return LoadStringResource(nullptr, IDS_UNTITLED);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] bool EndsWithNoCase(std::wstring_view text, std::wstring_view suffix) noexcept
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    const std::wstring_view tail = text.substr(text.size() - suffix.size());
    return CompareStringOrdinal(tail.data(), static_cast<int>(tail.size()), suffix.data(),
                                static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool LooksLikeProjectPath(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return directory || EndsWithNoCase(path, L".prism") || PathLooksLikeZipProject(path);
}

struct FilterStrings
{
    std::wstring project;
    std::wstring images;
    std::wstring all;
    std::wstring exploded;
    std::wstring jpeg;
    std::wstring png;
};

[[nodiscard]] FilterStrings LoadFilterStrings()
{
    return FilterStrings{
        LoadStringResource(nullptr, IDS_FILTER_PROJECT), LoadStringResource(nullptr, IDS_FILTER_IMAGES),
        LoadStringResource(nullptr, IDS_FILTER_ALL),     LoadStringResource(nullptr, IDS_FILTER_EXPLODED),
        LoadStringResource(nullptr, IDS_FILTER_JPEG),    LoadStringResource(nullptr, IDS_FILTER_PNG)};
}

[[nodiscard]] bool ReadDialogResult(IFileDialog* dialog, std::wstring& path) noexcept
{
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

[[nodiscard]] bool PickOpenPath(HWND owner, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    const FilterStrings strings = LoadFilterStrings();
    const COMDLG_FILTERSPEC filters[] = {
        {strings.project.c_str(), L"*.prism"},
        {strings.images.c_str(), L"*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.heic;*.heif"},
        {strings.all.c_str(), L"*.*"},
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
    return ReadDialogResult(dialog.get(), path);
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
    return ReadDialogResult(dialog.get(), path);
}

[[nodiscard]] UINT LoadErrorStringId(ProjectIoStatus status) noexcept
{
    switch (status)
    {
    case ProjectIoStatus::UnsupportedVersion:
        return IDS_ERROR_UNSUPPORTED_VERSION;
    case ProjectIoStatus::TooLarge:
        return IDS_ERROR_TOO_LARGE;
    case ProjectIoStatus::MissingImage:
        return IDS_ERROR_MISSING_IMAGE;
    case ProjectIoStatus::Ok:
    case ProjectIoStatus::Invalid:
    case ProjectIoStatus::Encode:
    default:
        return IDS_ERROR_OPEN_PROJECT;
    }
}
} // namespace

namespace
{
// A solid premultiplied BGRA raster for the demo document and the self-test.
[[nodiscard]] Raster SolidRaster(int width, int height, std::uint32_t argb)
{
    const std::uint8_t alpha = static_cast<std::uint8_t>(argb >> 24);
    const auto premultiply = [alpha](std::uint32_t channel)
    { return static_cast<std::uint8_t>((channel & 0xFFu) * alpha / 255u); };
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
    for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
    {
        bytes[index] = premultiply(argb);
        bytes[index + 1] = premultiply(argb >> 8);
        bytes[index + 2] = premultiply(argb >> 16);
        bytes[index + 3] = alpha;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

// A painted layer added on top as one Import Image history entry, then made active.
Guid InsertPaintedLayer(DocumentSession& session, const wchar_t* name, int width, int height, Vec2 origin,
                        double rotation, std::uint32_t argb)
{
    if (!session.Document().has_value())
    {
        return Guid{};
    }
    ImageLayer layer = ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
    const Raster raster = SolidRaster(width, height, argb);
    layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), name};
    layer.transform.origin = origin;
    layer.transform.rotation = rotation;
    session.BeginEdit(L"Import Image");
    session.Document().value().layers.push_back(layer);
    session.EndEdit();
    session.SelectLayer(layer.id);
    return layer.id;
}
} // namespace

Application::Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath, uint32_t screenshotDpi,
                         const wchar_t* demo, const wchar_t* bench) noexcept
    : _instance(instance), _forceWarp(forceWarp)
{
    if (demo != nullptr && demo[0] != L'\0')
    {
        _demo = demo;
    }
    if (bench != nullptr && bench[0] != L'\0')
    {
        _bench = bench;
    }
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
    ApplySettings(_settings);

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
        const bool plain = showCommand == SW_SHOWNORMAL || showCommand == SW_SHOWDEFAULT || showCommand == SW_SHOW;
        ShowWindow(_window.get(), _restoreMaximized && plain ? SW_SHOWMAXIMIZED : showCommand);
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
        if (_layoutDirty)
        {
            // A splitter change moved the canvas hole; re-sync the viewport before the frame that shows it.
            _layoutDirty = false;
            SyncChrome();
        }
        BindCompositionDevice();
        const CanvasDocument* document = _session.DisplayDocument();
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

// Self-test diagnostics go to the debugger and, when the parent redirected it, to stderr so test logs name the check.
void SelfTestFailure(const wchar_t* message) noexcept
{
    OutputDebugStringW(message);
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error == nullptr || error == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int length = static_cast<int>(wcslen(message));
    const int size = WideCharToMultiByte(CP_UTF8, 0, message, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, message, length, utf8.data(), size, nullptr, nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

int Application::RunSelfTest() noexcept
{
    // A self-test never waits on a message box: errors it provokes must report through its own checks.
    _quietUi = true;
    _screenshotDpi = 0;
    _renderer.SetLayoutDpi(0);
    HRESULT result = RegisterWindowClass();
    if (FAILED(result))
    {
        SelfTestFailure(L"RegisterWindowClass failed.\n");
        return 2;
    }

    result = CreateMainWindow(false);
    if (FAILED(result))
    {
        SelfTestFailure(L"CreateMainWindow failed.\n");
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
        SelfTestFailure(L"The default client area does not match the current monitor DPI.\n");
        return 2;
    }

    result = InitializeRenderer();
    if (FAILED(result))
    {
        SelfTestFailure(L"Renderer initialization failed.\n");
        return 5;
    }
    if (!_renderer.IsWarp())
    {
        SelfTestFailure(L"Self-test requires a WARP device.\n");
        return 5;
    }

    result = InitializeChrome();
    if (FAILED(result))
    {
        SelfTestFailure(L"DxUi chrome initialization failed.\n");
        return 5;
    }
    result = InitializeDocument();
    if (FAILED(result))
    {
        SelfTestFailure(L"Document initialization failed.\n");
        return 5;
    }
    ApplySettings(_settings);

    BindCompositionDevice();
    SyncChrome();
    const CanvasDocument* document = _session.DisplayDocument();
    result = _renderer.Render(_chrome.get(), &_composition, &_viewport, document);
    if (FAILED(result))
    {
        SelfTestFailure(L"Self-test Present failed.\n");
        return 5;
    }
    const DxUi::EmbeddedStatistics visible = _chrome->Statistics();
    if (visible.surfaceBytes == 0 || visible.composites == 0)
    {
        SelfTestFailure(L"Visible DxUi chrome did not allocate a surface or composite.\n");
        return 5;
    }
    if (visible.surfaceBytes > (64ull * 1024ull * 1024ull))
    {
        SelfTestFailure(L"Visible DxUi chrome exceeded the 64 MiB surface cap.\n");
        return 5;
    }
    if (_composition.CanvasSurfaceBytes() == 0)
    {
        SelfTestFailure(L"Visible document did not allocate canvas surfaces.\n");
        return 5;
    }
    // A settled document must not re-composite: the second frame reuses the canvas.
    const std::uint64_t composites = _composition.CompositeCount();
    result = _renderer.Render(_chrome.get(), &_composition, &_viewport, document);
    if (FAILED(result) || _composition.CompositeCount() != composites)
    {
        SelfTestFailure(L"A settled document was re-composited.\n");
        return 5;
    }

    // The DxUi splitter, palette wells with the color picker, and the opacity stepper are wired to the session.
    {
        const float scale = static_cast<float>(windowDpi) / 96.0f;
        const CanvasHole before = _chrome->HolePixels();
        const float beforeWidth = _chrome->LayersWidthDip();
        const D2D1_RECT_F separator = _chrome->SplitterBoundsDip();
        const float centerX = (separator.left + separator.right) * 0.5f;
        const float centerY = (separator.top + separator.bottom) * 0.5f;
        DxUi::PointerEvent pointer{};
        pointer.action = DxUi::PointerAction::Down;
        pointer.xPixels = centerX * scale;
        pointer.yPixels = centerY * scale;
        if (!_chrome->DispatchPointer(pointer))
        {
            SelfTestFailure(L"The layers splitter did not take the pointer press.\n");
            return 5;
        }
        pointer.action = DxUi::PointerAction::Move;
        pointer.xPixels = (centerX - 40.0f) * scale;
        if (!_chrome->DispatchPointer(pointer))
        {
            SelfTestFailure(L"The layers splitter did not take the pointer move.\n");
            return 5;
        }
        pointer.action = DxUi::PointerAction::Up;
        if (!_chrome->DispatchPointer(pointer))
        {
            SelfTestFailure(L"The layers splitter did not take the pointer release.\n");
            return 5;
        }
        if (!_layoutDirty)
        {
            SelfTestFailure(L"The layers splitter did not report a layout change.\n");
            return 5;
        }
        if (std::fabs(_chrome->LayersWidthDip() - (beforeWidth + 40.0f)) > 0.5f)
        {
            const std::wstring text =
                std::format(L"The layers splitter did not resize the panel: {} DIP, expected {} DIP.\n",
                            _chrome->LayersWidthDip(), beforeWidth + 40.0f);
            SelfTestFailure(text.c_str());
            return 5;
        }
        _layoutDirty = false;
        SyncChrome();
        if (_chrome->HolePixels().width >= before.width)
        {
            SelfTestFailure(L"The canvas hole did not follow the splitter.\n");
            return 5;
        }

        const D2D1_RECT_F well = _chrome->PaletteWellBoundsDip(false);
        pointer.action = DxUi::PointerAction::Down;
        pointer.xPixels = (well.left + well.right) * 0.5f * scale;
        pointer.yPixels = (well.top + well.bottom) * 0.5f * scale;
        bool handled = _chrome->DispatchPointer(pointer);
        pointer.action = DxUi::PointerAction::Up;
        handled = _chrome->DispatchPointer(pointer) && handled;
        if (!handled || !_chrome->ColorPickerOpen())
        {
            SelfTestFailure(L"The foreground well did not open the color picker.\n");
            return 5;
        }
        _chrome->SampleColor(0xFF336699u);
        _chrome->CloseColorPicker(true);
        if (_chrome->ColorPickerOpen() || _session.ForegroundColor() != 0xFF336699u)
        {
            SelfTestFailure(L"The color picker did not commit the foreground color.\n");
            return 5;
        }
        _chrome->OpenColorPicker(true);
        _chrome->SampleColor(0xFF112233u);
        _chrome->CloseColorPicker(false);
        if (_chrome->ColorPickerOpen() || _session.BackgroundColor() != DocumentSession::kDefaultBackgroundArgb)
        {
            SelfTestFailure(L"Cancelling the color picker changed the palette.\n");
            return 5;
        }
        // Palette keys apply while the canvas has focus; the well click and the picker moved focus to chrome.
        _chrome->SetCanvasFocused(true);
        if (!HandleAccelerator('X', 0) || _session.ForegroundColor() != DocumentSession::kDefaultBackgroundArgb ||
            _session.BackgroundColor() != 0xFF336699u)
        {
            SelfTestFailure(L"X did not swap the palette.\n");
            return 5;
        }
        if (!HandleAccelerator('D', 0) || _chrome->ForegroundColor() != DocumentSession::kDefaultForegroundArgb ||
            _chrome->BackgroundColor() != DocumentSession::kDefaultBackgroundArgb)
        {
            SelfTestFailure(L"D did not reset the palette into the wells.\n");
            return 5;
        }

        if (!_chrome->RequestLayerOpacity(50.0))
        {
            SelfTestFailure(L"The opacity stepper refused a value.\n");
            return 5;
        }
        const ImageLayer* layer = _session.ActiveLayer();
        if (layer == nullptr || std::fabs(layer->opacity - 0.5) > 1e-9 || !_session.history.CanUndo() ||
            _session.history.UndoName() != L"Layer Opacity")
        {
            SelfTestFailure(L"The opacity stepper did not record an undoable edit.\n");
            return 5;
        }
    }

    // Move tool: the transform box, a pointer drag, Free Transform with the header actions, flips and keys are wired.
    {
        InsertPaintedLayer(_session, L"Painted", 200, 150, Vec2{300, 200}, 0.0, 0xFF2F6FB0u);
        DocumentChanged();
        if (!_overlayGeometry.has_value())
        {
            SelfTestFailure(L"The Move tool did not show a transform box for a painted layer.\n");
            return 5;
        }
        const CanvasHole hole = _chrome->HolePixels();
        const ImageLayer* layer = _session.ActiveLayer();
        const LayerTransform before = layer->transform;
        const Vec2 center = _viewport.ViewFromDocument(before.Center(), DocumentSize());
        const auto pointer = [&](UINT message, double x, double y)
        { OnPointer(message, 0, MAKELPARAM(static_cast<int>(hole.left + x), static_cast<int>(hole.top + y))); };
        pointer(WM_LBUTTONDOWN, center.x, center.y);
        pointer(WM_MOUSEMOVE, center.x + 40.0, center.y + 24.0);
        pointer(WM_LBUTTONUP, center.x + 40.0, center.y + 24.0);
        layer = _session.ActiveLayer();
        if (_transformDrag.has_value() || layer == nullptr || layer->transform.origin.x <= before.origin.x ||
            layer->transform.origin.y <= before.origin.y || _session.PendingTransformEdit().has_value() ||
            _session.history.UndoName() != L"Transform Layer")
        {
            SelfTestFailure(L"Dragging with the Move tool did not move the layer as one history entry.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandFreeTransform);
        OnTransformAction(TransformAction::SetRotation, 30.0);
        if (!_session.PendingTransformEdit().has_value() || _session.PendingTransformEdit()->draft.rotation != 30.0 ||
            _session.ActiveLayer()->transform.rotation != 0.0)
        {
            SelfTestFailure(L"Free Transform did not preview a typed rotation.\n");
            return 5;
        }
        OnTransformAction(TransformAction::Apply, 0.0);
        if (_session.PendingTransformEdit().has_value() || _session.ActiveLayer()->transform.rotation != 30.0)
        {
            SelfTestFailure(L"Apply did not commit the typed rotation.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandFreeTransform);
        OnTransformAction(TransformAction::SetX, 999.0);
        _chrome->SetCanvasFocused(true);
        if (!HandleAccelerator(VK_ESCAPE, 0) || _session.PendingTransformEdit().has_value() ||
            _session.ActiveLayer()->transform.origin.x == 999.0)
        {
            SelfTestFailure(L"Escape did not cancel the pending transform.\n");
            return 5;
        }
        const double x = _session.ActiveLayer()->transform.origin.x;
        if (!HandleAccelerator(VK_RIGHT, MK_SHIFT) || _session.ActiveLayer()->transform.origin.x != x + 10.0)
        {
            SelfTestFailure(L"Shift+Right did not nudge the layer by ten pixels.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandFlipCanvasHorizontal);
        if (!_session.ActiveLayer()->transform.flipX || _session.history.UndoName() != L"Flip Canvas Horizontal")
        {
            SelfTestFailure(L"Flip Canvas Horizontal did not mirror the layer.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandFlipLayerVertical);
        if (!_session.ActiveLayer()->transform.flipY || _session.history.UndoName() != L"Flip Vertical")
        {
            SelfTestFailure(L"Flip Layer Vertical did not mirror the layer.\n");
            return 5;
        }
    }

    // Selection tools: a marquee drag, Shift adding, M switching the shape, an outline move, a click deselecting,
    // the wand, the Select menu keys, a polygonal lasso closed with Enter, Expand from the header and Escape.
    {
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        const CanvasHole hole = _chrome->HolePixels();
        const auto pointer = [&](UINT message, WPARAM wParam, double documentX, double documentY)
        {
            const Vec2 view = _viewport.ViewFromDocument(Vec2{documentX, documentY}, DocumentSize());
            OnPointer(message, wParam,
                      MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                 static_cast<int>(std::lround(hole.top + view.y))));
        };
        const auto bounds = [&]() -> std::optional<SelectionBounds>
        {
            const std::optional<DocumentSelection>& selection = _session.Selection();
            return selection.has_value() ? selection->Bounds() : std::nullopt;
        };
        const auto closeTo = [](double a, double b) { return std::fabs(a - b) <= 2.0; };
        OnTool(EditorTool::Marquee);
        _chrome->SetCanvasFocused(true);
        pointer(WM_LBUTTONDOWN, 0, 40.0, 40.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 120.0, 100.0);
        pointer(WM_LBUTTONUP, 0, 120.0, 100.0);
        std::optional<SelectionBounds> box = bounds();
        if (!box.has_value() || !closeTo(box->minX, 40.0) || !closeTo(box->minY, 40.0) || !closeTo(box->maxX, 120.0) ||
            !closeTo(box->maxY, 100.0) || _session.history.UndoName() != L"Rectangular Marquee" ||
            _session.PendingLasso().has_value())
        {
            SelfTestFailure(L"Dragging the Marquee did not select a whole-pixel rectangle.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, MK_SHIFT, 200.0, 40.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON | MK_SHIFT, 280.0, 100.0);
        pointer(WM_LBUTTONUP, MK_SHIFT, 280.0, 100.0);
        box = bounds();
        if (!box.has_value() || !closeTo(box->minX, 40.0) || !closeTo(box->maxX, 280.0) || !closeTo(box->maxY, 100.0))
        {
            SelfTestFailure(L"Shift-dragging the Marquee did not add a second rectangle.\n");
            return 5;
        }
        if (!HandleAccelerator('M', 0, false) || _session.marqueeKind != LassoKind::Ellipse ||
            !HandleAccelerator('M', 0, true) || _session.marqueeKind != LassoKind::Ellipse ||
            !HandleAccelerator('M', 0, false) || _session.marqueeKind != LassoKind::Rectangle)
        {
            SelfTestFailure(L"M did not switch the Marquee's shape exactly once per press.\n");
            return 5;
        }
        const double before = box->minX;
        pointer(WM_LBUTTONDOWN, 0, 60.0, 60.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 90.0, 70.0);
        pointer(WM_LBUTTONUP, 0, 90.0, 70.0);
        box = bounds();
        if (!box.has_value() || !closeTo(box->minX, before + 30.0) ||
            _session.history.UndoName() != L"Move Selection" || _session.IsMovingSelection())
        {
            SelfTestFailure(L"Dragging inside the selection did not move its outline as one entry.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, 0, 100.0, 70.0);
        pointer(WM_LBUTTONUP, 0, 100.0, 70.0);
        if (_session.Selection().has_value() || _session.history.UndoName() != L"Deselect")
        {
            SelfTestFailure(L"A click inside the selection did not deselect.\n");
            return 5;
        }
        InsertPaintedLayer(_session, L"Swatch", 120, 80, Vec2{40, 40}, 0.0, 0xFFE58A2Eu);
        DocumentChanged();
        OnTool(EditorTool::Wand);
        pointer(WM_LBUTTONDOWN, 0, 100.0, 80.0);
        pointer(WM_LBUTTONUP, 0, 100.0, 80.0);
        box = bounds();
        if (!box.has_value() || *box != SelectionBounds{40.0, 40.0, 160.0, 120.0} ||
            _session.history.UndoName() != L"Magic Wand")
        {
            SelfTestFailure(L"The Magic Wand did not select the painted layer's pixels exactly.\n");
            return 5;
        }
        if (!HandleAccelerator('I', MK_CONTROL | MK_SHIFT) || _session.history.UndoName() != L"Inverse" ||
            !HandleAccelerator('A', MK_CONTROL) || _session.history.UndoName() != L"Select All" ||
            bounds() != SelectionBounds{0.0, 0.0, DocumentSize().x, DocumentSize().y} ||
            !HandleAccelerator('D', MK_CONTROL) || _session.Selection().has_value() ||
            _session.history.UndoName() != L"Deselect")
        {
            SelfTestFailure(L"Ctrl+Shift+I, Ctrl+A and Ctrl+D did not drive the Select menu.\n");
            return 5;
        }
        OnTool(EditorTool::Lasso);
        _session.lassoKind = LassoKind::Polygonal;
        pointer(WM_LBUTTONDOWN, 0, 300.0, 300.0);
        pointer(WM_LBUTTONUP, 0, 300.0, 300.0);
        pointer(WM_LBUTTONDOWN, 0, 400.0, 300.0);
        pointer(WM_LBUTTONUP, 0, 400.0, 300.0);
        pointer(WM_LBUTTONDOWN, 0, 400.0, 400.0);
        pointer(WM_LBUTTONUP, 0, 400.0, 400.0);
        if (!_session.PendingLasso().has_value() || _session.PendingLasso()->points.size() != 3 ||
            !HandleAccelerator(VK_RETURN, 0) || _session.PendingLasso().has_value() ||
            _session.history.UndoName() != L"Polygonal Lasso")
        {
            SelfTestFailure(L"Polygonal lasso clicks and Enter did not close an outline.\n");
            return 5;
        }
        OnSelectionAction(SelectionAction::SetExpandAmount, 5.0);
        OnSelectionAction(SelectionAction::Expand, 0.0);
        box = bounds();
        if (_session.history.UndoName() != L"Expand Selection" || !box.has_value() || !closeTo(box->minX, 295.0))
        {
            SelfTestFailure(L"Expand from the header did not grow the selection by five pixels.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, 0, 500.0, 500.0);
        pointer(WM_LBUTTONUP, 0, 500.0, 500.0);
        if (!_session.PendingLasso().has_value() || !HandleAccelerator(VK_ESCAPE, 0) ||
            _session.PendingLasso().has_value())
        {
            SelfTestFailure(L"Escape did not cancel the lasso outline in progress.\n");
            return 5;
        }
        OnTool(EditorTool::Move);
    }

    // Brush and Eraser: a stroke through the compute engine commits one sparse entry that never assembles its
    // pixels, the eraser clears them, [ and ] and the digit keys change the tip, Escape drops a stroke.
    {
        OnTool(EditorTool::Brush);
        _chrome->SetCanvasFocused(true);
        // The selection block leaves a lasso outline behind, and strokes are clipped to the selection.
        OnCommand(ChromeHost::kCommandDeselect);
        OnCommand(ChromeHost::kCommandNewLayer);
        _session.brushSettings = BrushSettings{30, 1, 1, 0, 0, 1, false};
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        const CanvasHole hole = _chrome->HolePixels();
        const auto pointer = [&](UINT message, WPARAM wParam, double documentX, double documentY)
        {
            const Vec2 view = _viewport.ViewFromDocument(Vec2{documentX, documentY}, DocumentSize());
            OnPointer(message, wParam,
                      MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                 static_cast<int>(std::lround(hole.top + view.y))));
        };
        const auto pixelAt = [&](int x, int y, std::array<int, 4>& out) -> bool
        {
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
            std::vector<std::uint8_t> bgra;
            UINT width = 0;
            UINT height = 0;
            if (FAILED(_composition.Readback(bgra, width, height)) || x < 0 || y < 0 || static_cast<UINT>(x) >= width ||
                static_cast<UINT>(y) >= height)
            {
                return false;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            out = {bgra[index], bgra[index + 1], bgra[index + 2], bgra[index + 3]};
            return true;
        };
        if (!_brushEngine.Ready())
        {
            SelfTestFailure(L"The brush compute engine did not initialize on the WARP device.\n");
            return 5;
        }
        std::array<int, 4> before{};
        std::array<int, 4> besideBefore{};
        if (!pixelAt(300, 600, before) || !pixelAt(300, 500, besideBefore))
        {
            SelfTestFailure(L"The canvas could not be read back before the stroke.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, 0, 200.0, 600.0);
        if (!_brushDragging || _session.PendingBrush() == nullptr || !_composition.HasStroke())
        {
            SelfTestFailure(L"Pressing with the Brush did not start a stroke with a live preview.\n");
            return 5;
        }
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 400.0, 600.0);
        std::array<int, 4> live{};
        if (!pixelAt(300, 600, live) || live[2] < 250 || live[1] > 5 || live[0] > 5 || live[3] < 250)
        {
            SelfTestFailure(L"The live stroke did not show red paint on the canvas.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, 0, 400.0, 600.0);
        const ImageLayer* painted = _session.ActiveLayer();
        if (_brushDragging || _session.PendingBrush() != nullptr || _composition.HasStroke() || painted == nullptr ||
            !painted->asset.has_value() || !painted->asset->image.IsSparse() ||
            painted->asset->image.IsMaterialized() || _session.history.UndoName() != L"Brush Stroke")
        {
            SelfTestFailure(L"Releasing the Brush did not commit one sparse Brush Stroke entry.\n");
            return 5;
        }
        std::array<int, 4> committed{};
        std::array<int, 4> beside{};
        if (!pixelAt(300, 600, committed) || committed[2] < 250 || committed[3] < 250 || !pixelAt(300, 500, beside) ||
            beside != besideBefore || painted->asset->image.IsMaterialized())
        {
            SelfTestFailure(L"The committed stroke did not draw from the adopted texture.\n");
            return 5;
        }
        OnTool(EditorTool::Eraser);
        pointer(WM_LBUTTONDOWN, 0, 300.0, 600.0);
        pointer(WM_LBUTTONUP, 0, 300.0, 600.0);
        std::array<int, 4> erased{};
        if (_session.history.UndoName() != L"Erase" || !pixelAt(300, 600, erased) || erased != before)
        {
            SelfTestFailure(L"The Eraser did not clear the painted pixels.\n");
            return 5;
        }
        OnTool(EditorTool::Brush);
        _session.brushSettings.diameter = 30;
        if (!HandleAccelerator(VK_OEM_6, 0) || _session.brushSettings.diameter != 36 ||
            !HandleAccelerator(VK_OEM_4, MK_SHIFT) || _session.brushSettings.hardness != 0.75 ||
            !HandleAccelerator('5', 0) || _session.brushSettings.opacity != 0.5)
        {
            SelfTestFailure(L"The bracket and digit keys did not change the brush tip.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, 0, 500.0, 500.0);
        if (_session.PendingBrush() == nullptr || !HandleAccelerator(VK_ESCAPE, 0) ||
            _session.PendingBrush() != nullptr || _composition.HasStroke() || _brushDragging ||
            _session.history.UndoName() != L"Erase")
        {
            SelfTestFailure(L"Escape did not drop the stroke in progress.\n");
            return 5;
        }
        OnTool(EditorTool::Move);
    }

    // Retouch tools: Clone Stamp copies from a source, Spot Healing blends the copy away, Smudge and Blur commit
    // their entries, a Gradient previews until Enter and takes the digit keys, a Shape drag adds a layer, Shift-U
    // switches the kind, Escape drops a draft, and the Eyedropper reads the canvas into the foreground.
    {
        OnTool(EditorTool::Brush);
        _chrome->SetCanvasFocused(true);
        InsertPaintedLayer(_session, L"Plate", 300, 300, Vec2{0, 0}, 0.0, 0xFF808080u);
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        const CanvasHole hole = _chrome->HolePixels();
        const auto pointer = [&](UINT message, WPARAM wParam, double documentX, double documentY)
        {
            const Vec2 view = _viewport.ViewFromDocument(Vec2{documentX, documentY}, DocumentSize());
            OnPointer(message, wParam,
                      MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                 static_cast<int>(std::lround(hole.top + view.y))));
        };
        const auto pixelAt = [&](int x, int y, std::array<int, 4>& out) -> bool
        {
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
            std::vector<std::uint8_t> bgra;
            UINT width = 0;
            UINT height = 0;
            if (FAILED(_composition.Readback(bgra, width, height)) || x < 0 || y < 0 || static_cast<UINT>(x) >= width ||
                static_cast<UINT>(y) >= height)
            {
                return false;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            out = {bgra[index], bgra[index + 1], bgra[index + 2], bgra[index + 3]};
            return true;
        };
        // A red mark to copy.
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{12, 1, 1, 0, 0, 1, false};
        pointer(WM_LBUTTONDOWN, 0, 100.0, 100.0);
        pointer(WM_LBUTTONUP, 0, 100.0, 100.0);
        std::array<int, 4> mark{};
        if (_session.history.UndoName() != L"Brush Stroke" || !pixelAt(100, 100, mark) || mark[2] < 250 || mark[1] > 5)
        {
            SelfTestFailure(L"The red mark for the retouch block did not paint.\n");
            return 5;
        }
        // Clone Stamp: its own soft tip, nothing without a source, then a stamp of the mark elsewhere.
        OnTool(EditorTool::Clone);
        if (_session.brushSettings.hardness != 0 || _session.brushSettings.diameter != 40)
        {
            SelfTestFailure(L"Clone Stamp did not get its own soft tip.\n");
            return 5;
        }
        _session.brushSettings = BrushSettings{12, 1, 0, 0, 0, 1, false};
        pointer(WM_LBUTTONDOWN, 0, 200.0, 200.0);
        pointer(WM_LBUTTONUP, 0, 200.0, 200.0);
        if (_session.history.UndoName() != L"Brush Stroke" || _brushDragging)
        {
            SelfTestFailure(L"Clone Stamp stamped without a source.\n");
            return 5;
        }
        _session.SetCloneSource(Vec2{100, 100});
        pointer(WM_LBUTTONDOWN, 0, 200.0, 200.0);
        pointer(WM_LBUTTONUP, 0, 200.0, 200.0);
        std::array<int, 4> stamped{};
        if (_session.history.UndoName() != L"Clone Stamp" || !pixelAt(200, 200, stamped) || stamped[2] < 250 ||
            stamped[1] > 5)
        {
            SelfTestFailure(L"Clone Stamp did not copy the mark.\n");
            return 5;
        }
        // Spot Healing blends the stamped mark into the plate.
        OnTool(EditorTool::Heal);
        _session.brushSettings = BrushSettings{24, 1, 0, 0, 0, 1, false};
        pointer(WM_LBUTTONDOWN, 0, 200.0, 200.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 200.5, 200.0);
        pointer(WM_LBUTTONUP, 0, 200.5, 200.0);
        std::array<int, 4> healed{};
        if (_session.history.UndoName() != L"Spot Healing" || !pixelAt(200, 200, healed) || healed[2] - healed[1] >= 30)
        {
            SelfTestFailure(L"Spot Healing did not blend the mark away.\n");
            return 5;
        }
        // Smear: Smudge drags the red along through a live warp; Blur commits its own entry.
        OnTool(EditorTool::Smear);
        _session.smearMode = SmearMode::Smudge;
        _session.brushSettings = BrushSettings{10, 0.5, 0, 0, 0, 0.8, false};
        pointer(WM_LBUTTONDOWN, 0, 100.0, 100.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 130.0, 100.0);
        if (!_warpDragging || _session.PendingWarp() == nullptr || !_composition.HasStroke())
        {
            SelfTestFailure(L"Smudge did not start a warp with a live preview.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, 0, 130.0, 100.0);
        std::array<int, 4> smudged{};
        if (_warpDragging || _session.history.UndoName() != L"Smudge" || !pixelAt(115, 100, smudged) ||
            smudged[2] <= smudged[0])
        {
            SelfTestFailure(L"Smudge did not drag the red mark along.\n");
            return 5;
        }
        _session.smearMode = SmearMode::Blur;
        pointer(WM_LBUTTONDOWN, 0, 100.0, 100.0);
        pointer(WM_LBUTTONUP, 0, 100.0, 100.0);
        if (_session.history.UndoName() != L"Blur")
        {
            SelfTestFailure(L"Blur did not commit.\n");
            return 5;
        }
        // Gradient: a drag previews, a digit sets its opacity, Enter applies one entry.
        OnTool(EditorTool::Gradient);
        _session.SetForegroundColor(0xFF000000u);
        _session.SetBackgroundColor(0xFFFFFFFFu);
        _session.gradientSettings = GradientSettings{};
        const int beforeGradient = _session.history.UndoCount();
        pointer(WM_LBUTTONDOWN, 0, 10.0, 250.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 290.0, 250.0);
        pointer(WM_LBUTTONUP, 0, 290.0, 250.0);
        std::array<int, 4> pendingPixel{};
        if (_session.PendingGradient() == nullptr || !_composition.HasStroke() || !pixelAt(20, 250, pendingPixel) ||
            pendingPixel[2] > 40 || _session.history.UndoCount() != beforeGradient)
        {
            SelfTestFailure(L"Dragging the Gradient did not show a pending line on the canvas.\n");
            return 5;
        }
        if (!HandleAccelerator('5', 0) || _session.gradientSettings.opacity != 0.5)
        {
            SelfTestFailure(L"The digit keys did not set the gradient opacity.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_RETURN, 0) || _session.PendingGradient() != nullptr || _composition.HasStroke() ||
            _session.history.UndoCount() != beforeGradient + 1 || _session.history.UndoName() != L"Gradient")
        {
            SelfTestFailure(L"Enter did not apply the gradient as one entry.\n");
            return 5;
        }
        std::array<int, 4> gradientStart{};
        std::array<int, 4> gradientEnd{};
        if (!pixelAt(20, 250, gradientStart) || !pixelAt(280, 250, gradientEnd) || gradientStart[2] >= gradientEnd[2])
        {
            SelfTestFailure(L"The committed gradient did not darken its start more than its end.\n");
            return 5;
        }
        // Shape: a drag adds a filled layer, Shift-U switches the kind, Escape drops a draft.
        OnTool(EditorTool::Shape);
        _session.SetForegroundColor(0xFF3366CCu);
        const std::size_t layersBefore = _session.Document().value().layers.size();
        pointer(WM_LBUTTONDOWN, 0, 20.0, 20.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 60.0, 50.0);
        if (!_session.PendingShape().has_value())
        {
            SelfTestFailure(L"Dragging the Shape did not start a draft.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, 0, 60.0, 50.0);
        std::array<int, 4> filled{};
        if (_session.Document().value().layers.size() != layersBefore + 1 || _session.ActiveLayer() == nullptr ||
            _session.ActiveLayer()->name != L"Rectangle 1" || _session.history.UndoName() != L"Rectangle" ||
            !pixelAt(40, 35, filled) || filled[0] != 0xCC || filled[1] != 0x66 || filled[2] != 0x33)
        {
            SelfTestFailure(L"Releasing the Shape did not add a Rectangle layer in the foreground color.\n");
            return 5;
        }
        if (!HandleAccelerator('U', MK_SHIFT) || _session.shapeKind != ShapeKind::Ellipse)
        {
            SelfTestFailure(L"Shift-U did not switch the shape kind.\n");
            return 5;
        }
        pointer(WM_LBUTTONDOWN, 0, 100.0, 100.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 140.0, 140.0);
        if (!HandleAccelerator(VK_ESCAPE, 0) || _session.PendingShape().has_value())
        {
            SelfTestFailure(L"Escape did not drop the shape draft.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, 0, 140.0, 140.0);
        if (_session.Document().value().layers.size() != layersBefore + 1)
        {
            SelfTestFailure(L"A dropped shape draft still made a layer.\n");
            return 5;
        }
        // Eyedropper: pressing reads the canvas into the foreground; releasing ends the sampling.
        OnTool(EditorTool::Eyedropper);
        _session.SetForegroundColor(0xFF000000u);
        pointer(WM_LBUTTONDOWN, 0, 40.0, 35.0);
        if (!_samplingColor || _session.ForegroundColor() != 0xFF3366CCu)
        {
            SelfTestFailure(L"The Eyedropper did not sample the shape's color.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, 0, 40.0, 35.0);
        if (_samplingColor)
        {
            SelfTestFailure(L"Releasing the Eyedropper did not end sampling.\n");
            return 5;
        }
        OnTool(EditorTool::Move);
    }

    // Pixel edits: Fill with Foreground Color from the Edit menu, Ctrl+X cuts to the clipboard (mirrored to the
    // system clipboard), Ctrl+V pastes in place as a new layer, Ctrl+I inverts it, a Ctrl-drag with the Marquee
    // moves the selected pixels through the live preview as one `Move Pixels` entry, Ctrl+Right nudges them, Delete
    // clears them, Ctrl+T on a selection floats them (Escape restores exactly, Enter merges one `Transform
    // Selection` entry with the outline moved), Ctrl+J copies them to a layer, Add Mask hides them and Ctrl+Shift+C
    // copies the composite.
    {
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        const CanvasHole hole = _chrome->HolePixels();
        const auto pointer = [&](UINT message, WPARAM wParam, double documentX, double documentY)
        {
            const Vec2 view = _viewport.ViewFromDocument(Vec2{documentX, documentY}, DocumentSize());
            OnPointer(message, wParam,
                      MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                 static_cast<int>(std::lround(hole.top + view.y))));
        };
        const auto pixelAt = [&](int x, int y, std::array<int, 4>& out) -> bool
        {
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
            std::vector<std::uint8_t> bgra;
            UINT width = 0;
            UINT height = 0;
            if (FAILED(_composition.Readback(bgra, width, height)) || x < 0 || y < 0 || static_cast<UINT>(x) >= width ||
                static_cast<UINT>(y) >= height)
            {
                return false;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            out = {bgra[index], bgra[index + 1], bgra[index + 2], bgra[index + 3]};
            return true;
        };
        const auto bounds = [&]() -> std::optional<SelectionBounds>
        {
            const std::optional<DocumentSelection>& selection = _session.Selection();
            return selection.has_value() ? selection->Bounds() : std::nullopt;
        };
        const auto shown = [&]() -> std::optional<SelectionBounds>
        {
            const std::optional<DocumentSelection> displayed = _session.DisplayedSelection();
            return displayed.has_value() ? displayed->Bounds() : std::nullopt;
        };
        const auto closeTo = [](double a, double b) { return std::fabs(a - b) <= 2.0; };
        const auto layerPixel = [&](int x, int y, std::array<std::uint8_t, 4>& out) -> bool
        {
            const ImageLayer* layer = _session.ActiveLayer();
            if (layer == nullptr || !layer->asset.has_value())
            {
                return false;
            }
            const Raster& image = layer->asset->image;
            const Vec2 pixel = PixelToDocument(layer->transform, image.Width(), image.Height())
                                   .Inverted()
                                   .Apply(Vec2{x + 0.5, y + 0.5});
            image.ReadRegion(static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)), 1, 1,
                             out.data(), 4, 0);
            return true;
        };
        const Guid sheet = InsertPaintedLayer(_session, L"Sheet", 200, 200, Vec2{0, 0}, 0.0, 0xFF2F6FB0u);
        OnTool(EditorTool::Marquee);
        _chrome->SetCanvasFocused(true);
        pointer(WM_LBUTTONDOWN, 0, 20.0, 20.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 60.0, 60.0);
        pointer(WM_LBUTTONUP, 0, 60.0, 60.0);
        _session.SetForegroundColor(0xFFFF0000u);
        OnCommand(ChromeHost::kCommandFillForeground);
        std::array<int, 4> pixel{};
        if (_session.history.UndoName() != L"Fill" || !pixelAt(40, 40, pixel) || pixel[2] < 250 || pixel[0] > 5)
        {
            SelfTestFailure(L"Fill with Foreground Color did not fill the selection.\n");
            return 5;
        }
        const std::size_t layersBefore = _session.Document()->layers.size();
        if (!HandleAccelerator('X', MK_CONTROL) || _session.history.UndoName() != L"Clear" || !_session.CanPaste() ||
            !pixelAt(40, 40, pixel) || pixel[2] > 250 || !SystemClipboard::HasImage())
        {
            SelfTestFailure(L"Ctrl+X did not cut the selection to the clipboard.\n");
            return 5;
        }
        if (!HandleAccelerator('V', MK_CONTROL) || _session.history.UndoName() != L"Paste" ||
            _session.Document()->layers.size() != layersBefore + 1 || _session.Selection().has_value() ||
            _session.ActiveLayer() == nullptr || _session.ActiveLayer()->transform.origin != Vec2{20, 20} ||
            !pixelAt(40, 40, pixel) || pixel[2] < 250 || pixel[0] > 5)
        {
            SelfTestFailure(L"Ctrl+V did not paste the cut pixels in place as a new layer.\n");
            return 5;
        }
        if (!HandleAccelerator('I', MK_CONTROL) || _session.history.UndoName() != L"Invert" ||
            !pixelAt(40, 40, pixel) || pixel[2] > 5 || pixel[1] < 250 || pixel[0] < 250)
        {
            SelfTestFailure(L"Ctrl+I did not invert the pasted layer.\n");
            return 5;
        }
        // A Ctrl-drag with the Marquee lifts the selected pixels into the live preview; the outline follows.
        pointer(WM_LBUTTONDOWN, 0, 20.0, 20.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 60.0, 60.0);
        pointer(WM_LBUTTONUP, 0, 60.0, 60.0);
        pointer(WM_LBUTTONDOWN, MK_CONTROL, 40.0, 40.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON | MK_CONTROL, 140.0, 40.0);
        std::optional<SelectionBounds> box = bounds();
        std::optional<SelectionBounds> displayed = shown();
        if (!_pixelDragStart.has_value() || _session.PendingPixelMove() == nullptr)
        {
            SelfTestFailure(L"Ctrl-dragging inside the selection did not lift the selected pixels.\n");
            return 5;
        }
        if (!_composition.HasStroke())
        {
            SelfTestFailure(L"The pixel move did not start the stroke preview.\n");
            return 5;
        }
        if (!displayed.has_value() || !closeTo(displayed->minX, 120.0) || !box.has_value() || !closeTo(box->minX, 20.0))
        {
            SelfTestFailure(L"The outline did not follow the pixel drag while the stored selection stayed put.\n");
            return 5;
        }
        if (!pixelAt(140, 40, pixel) || pixel[1] < 250)
        {
            SelfTestFailure(L"The moving pixels did not show at their new place through the preview.\n");
            return 5;
        }
        if (!pixelAt(40, 40, pixel) || pixel[1] > 250)
        {
            SelfTestFailure(L"The moved pixels still showed at their old place through the preview.\n");
            return 5;
        }
        pointer(WM_LBUTTONUP, MK_CONTROL, 140.0, 40.0);
        box = bounds();
        if (_pixelDragStart.has_value() || _session.PendingPixelMove() != nullptr || _composition.HasStroke() ||
            _session.history.UndoName() != L"Move Pixels" || !box.has_value() || !closeTo(box->minX, 120.0) ||
            !pixelAt(140, 40, pixel) || pixel[1] < 250)
        {
            SelfTestFailure(L"Releasing the Ctrl-drag did not commit one Move Pixels entry.\n");
            return 5;
        }
        // The Marquee landed on whole pixels near 20; the nudge is exact relative to where the drag left the outline.
        const double beforeNudge = box.has_value() ? box->minX : 0.0;
        if (!HandleAccelerator(VK_RIGHT, MK_CONTROL) || _session.history.UndoName() != L"Move Pixels" ||
            !bounds().has_value() || std::fabs(bounds()->minX - (beforeNudge + 1.0)) > 0.01)
        {
            SelfTestFailure(L"Ctrl+Right did not nudge the selected pixels by one.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_DELETE, 0) || _session.history.UndoName() != L"Clear" || !pixelAt(141, 40, pixel) ||
            pixel[1] > 250)
        {
            SelfTestFailure(L"Delete did not clear the selected pixels.\n");
            return 5;
        }
        // Ctrl+T on a selection floats the pixels: Escape restores exactly, Enter merges one entry.
        OnCommand(ChromeHost::kCommandDeselect);
        _session.SelectLayer(sheet);
        OnTool(EditorTool::Marquee);
        pointer(WM_LBUTTONDOWN, 0, 100.0, 100.0);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, 140.0, 140.0);
        pointer(WM_LBUTTONUP, 0, 140.0, 140.0);
        const std::wstring entryBefore = _session.history.UndoName();
        const std::size_t layersFloating = _session.Document()->layers.size();
        if (!HandleAccelerator('T', MK_CONTROL) || _tool != EditorTool::Move ||
            !_session.PendingTransformEdit().has_value() || !_session.PendingTransformEdit()->floating.has_value() ||
            _session.Document()->layers.size() != layersFloating + 1)
        {
            SelfTestFailure(L"Ctrl+T with a selection did not float the selected pixels.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_ESCAPE, 0) || _session.PendingTransformEdit().has_value() ||
            _session.Document()->layers.size() != layersFloating || _session.history.UndoName() != entryBefore ||
            _session.ActiveLayerID() != sheet)
        {
            SelfTestFailure(L"Escape did not restore the floating selection exactly.\n");
            return 5;
        }
        if (!HandleAccelerator('T', MK_CONTROL) || !_session.PendingTransformEdit().has_value())
        {
            SelfTestFailure(L"Ctrl+T did not float the selection a second time.\n");
            return 5;
        }
        LayerTransform draft = _session.PendingTransformEdit()->draft;
        draft.origin.x += 50.0;
        _session.PreviewTransform(draft);
        DocumentChanged();
        displayed = shown();
        if (!displayed.has_value() || !closeTo(displayed->minX, 150.0) || !closeTo(bounds()->minX, 100.0))
        {
            SelfTestFailure(L"The outline did not follow the floating pixels.\n");
            return 5;
        }
        std::array<std::uint8_t, 4> cleared{};
        std::array<std::uint8_t, 4> landed{};
        if (!HandleAccelerator(VK_RETURN, 0) || _session.PendingTransformEdit().has_value() ||
            _session.history.UndoName() != L"Transform Selection" ||
            _session.Document()->layers.size() != layersFloating || _session.ActiveLayerID() != sheet ||
            !closeTo(bounds()->minX, 150.0) || !layerPixel(120, 120, cleared) || cleared[3] != 0 ||
            !layerPixel(170, 120, landed) || landed[3] != 255 || landed[0] != 0xB0)
        {
            SelfTestFailure(L"Enter did not merge the floating selection as one Transform Selection entry.\n");
            return 5;
        }
        // Layer via Copy and Add Mask use the selection; Copy Merged takes the composite.
        if (!HandleAccelerator('J', MK_CONTROL) || _session.history.UndoName() != L"Layer via Copy" ||
            _session.Document()->layers.size() != layersFloating + 1 || _session.Selection().has_value())
        {
            SelfTestFailure(L"Ctrl+J did not copy the selection to a new layer.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandUndo);
        OnCommand(ChromeHost::kCommandAddMask);
        if (_session.history.UndoName() != L"Add Mask from Selection" || !_session.IsMaskSelected() ||
            _session.Selection().has_value() || _session.ActiveLayer() == nullptr ||
            !_session.ActiveLayer()->mask.has_value())
        {
            SelfTestFailure(L"Add Mask did not make a mask from the selection.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandSelectAll);
        if (!HandleAccelerator('C', MK_CONTROL | MK_SHIFT) || !_session.Clipboard().has_value() ||
            _session.Clipboard()->image.Width() != _session.Document()->width ||
            _session.Clipboard()->image.Height() != _session.Document()->height)
        {
            SelfTestFailure(L"Ctrl+Shift+C did not copy the merged canvas.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandDeselect);
        OnTool(EditorTool::Move);
    }

    // Color adjustments: Ctrl+L opens Levels on a painted layer and its output white previews black through the
    // display document, Escape restores, an input white of 128 turns the gray white and Enter records one `Levels`
    // entry; the Filter menu's Gaussian Blur applies as one entry; a Hue/Saturation adjustment layer opens its panel
    // over the red beneath, a hue of 120 rotates it live on the canvas and Enter keeps the record as one entry;
    // Shift+= steps the blend mode.
    {
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        const auto pixelAt = [&](int x, int y, std::array<int, 4>& out) -> bool
        {
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
            std::vector<std::uint8_t> bgra;
            UINT width = 0;
            UINT height = 0;
            if (FAILED(_composition.Readback(bgra, width, height)) || x < 0 || y < 0 || static_cast<UINT>(x) >= width ||
                static_cast<UINT>(y) >= height)
            {
                return false;
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            out = {bgra[index], bgra[index + 1], bgra[index + 2], bgra[index + 3]};
            return true;
        };
        InsertPaintedLayer(_session, L"Gray", 120, 120, Vec2{0, 0}, 0.0, 0xFF808080u);
        OnTool(EditorTool::Move);
        _chrome->SetCanvasFocused(true);
        std::array<int, 4> pixel{};
        if (!HandleAccelerator('L', MK_CONTROL) || !_session.PendingLevels().has_value() || !_chrome->AdjustPanelOpen())
        {
            SelfTestFailure(L"Ctrl+L did not open the Levels panel.\n");
            return 5;
        }
        OnAdjustAction(AdjustAction::SetOutputWhite, 0.0);
        if (!pixelAt(50, 50, pixel) || pixel[0] > 5)
        {
            SelfTestFailure(L"The Levels preview did not show through the canvas.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_ESCAPE, 0) || _session.PendingLevels().has_value() || _chrome->AdjustPanelOpen() ||
            !pixelAt(50, 50, pixel) || pixel[0] < 120)
        {
            SelfTestFailure(L"Escape did not cancel the Levels edit.\n");
            return 5;
        }
        const int levelsCount = _session.history.UndoCount();
        static_cast<void>(HandleAccelerator('L', MK_CONTROL));
        OnAdjustAction(AdjustAction::SetInputWhite, 128.0);
        if (!HandleAccelerator(VK_RETURN, 0) || _session.PendingLevels().has_value() ||
            _session.history.UndoCount() != levelsCount + 1 || _session.history.UndoName() != L"Levels" ||
            !pixelAt(50, 50, pixel) || pixel[0] < 250)
        {
            SelfTestFailure(L"Enter did not apply the Levels edit as one entry.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandGaussianBlur);
        if (!_session.PendingFilter().has_value() || _session.PendingFilter()->kind != FilterKind::GaussianBlur ||
            !_chrome->AdjustPanelOpen())
        {
            SelfTestFailure(L"The Filter menu did not open Gaussian Blur.\n");
            return 5;
        }
        OnAdjustAction(AdjustAction::SetRadius, 2.0);
        OnAdjustAction(AdjustAction::Apply, 0.0);
        if (_session.PendingFilter().has_value() || _session.history.UndoName() != L"Gaussian Blur")
        {
            SelfTestFailure(L"Applying Gaussian Blur did not record one entry.\n");
            return 5;
        }
        // Content-Aware Fill: a red block filled into a selection on the white plate fills back to white through
        // Shift+Delete and Enter as one entry; with everything selected there is nothing to copy from, so Enter
        // refuses and Escape closes the panel.
        _session.ApplySelection(SelectionGeometry::Rect(40.0, 40.0, 20.0, 20.0), SelectionMode::New, L"Select");
        _session.SetForegroundColor(0xFFFF0000u);
        OnCommand(ChromeHost::kCommandFillForeground);
        const int fillCount = _session.history.UndoCount();
        if (!pixelAt(50, 50, pixel) || pixel[2] < 250 || pixel[0] > 5 || !HandleAccelerator(VK_DELETE, MK_SHIFT) ||
            !_session.PendingFilter().has_value() || _session.PendingFilter()->kind != FilterKind::ContentAwareFill ||
            _session.PendingFilter()->previewFailed || !_chrome->AdjustPanelOpen())
        {
            SelfTestFailure(L"Shift+Delete did not open Content-Aware Fill on the selection.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_RETURN, 0) || _session.PendingFilter().has_value() ||
            _session.history.UndoCount() != fillCount + 1 || _session.history.UndoName() != L"Content-Aware Fill" ||
            !pixelAt(50, 50, pixel) || pixel[0] < 240 || pixel[1] < 240 || pixel[2] < 240)
        {
            SelfTestFailure(L"Enter did not apply Content-Aware Fill as one entry.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandSelectAll);
        if (!HandleAccelerator(VK_DELETE, MK_SHIFT) || !_session.PendingFilter().has_value() ||
            !_session.PendingFilter()->previewFailed || !HandleAccelerator(VK_RETURN, 0) ||
            !_session.PendingFilter().has_value() || !HandleAccelerator(VK_ESCAPE, 0) ||
            _session.PendingFilter().has_value() || _chrome->AdjustPanelOpen())
        {
            SelfTestFailure(
                L"Content-Aware Fill with nothing to copy from did not refuse Enter and close on Escape.\n");
            return 5;
        }
        OnCommand(ChromeHost::kCommandDeselect);
        InsertPaintedLayer(_session, L"Red", 120, 120, Vec2{0, 0}, 0.0, 0xFFFF0000u);
        const std::size_t layersBefore = _session.Document()->layers.size();
        OnCommand(ChromeHost::kCommandNewHueSaturationAdjustment);
        if (_session.Document()->layers.size() != layersBefore + 1 || _session.ActiveLayer() == nullptr ||
            !_session.ActiveLayer()->adjustment.has_value() || !_session.PendingHueSaturation().has_value() ||
            !_chrome->AdjustPanelOpen())
        {
            SelfTestFailure(L"New Hue/Saturation Adjustment did not add a layer and open its panel.\n");
            return 5;
        }
        OnAdjustAction(AdjustAction::SetHue, 120.0);
        if (!pixelAt(50, 50, pixel) || pixel[1] < 200 || pixel[2] > 60)
        {
            SelfTestFailure(L"The adjustment layer did not rotate the red beneath it live.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_RETURN, 0) || _session.PendingHueSaturation().has_value() ||
            _session.history.UndoName() != L"Edit Hue/Saturation Adjustment" ||
            std::fabs(Adjust::RangeOf(_session.ActiveLayer()->adjustment->ResolvedHsv()).hue - 120.0) > 0.01 ||
            !pixelAt(50, 50, pixel) || pixel[1] < 200)
        {
            SelfTestFailure(L"Enter did not keep the adjustment layer's settings.\n");
            return 5;
        }
        if (!HandleAccelerator(VK_OEM_PLUS, MK_SHIFT) ||
            _session.ActiveLayer()->blendMode != LayerBlendMode::Multiply ||
            _session.history.UndoName() != L"Layer Blend Mode")
        {
            SelfTestFailure(L"Shift+= did not step the blend mode.\n");
            return 5;
        }
        OnTool(EditorTool::Move);
    }

    // Product hardening (C14): accessible names and the UIA root, the settings file's live reload and the theme.
    if (const int hardening = RunProductHardeningSelfTest(); hardening != 0)
    {
        return hardening;
    }

    _chrome->SetVisible(false);
    _composition.ReleaseDocumentSurfaces();
    const DxUi::EmbeddedStatistics hidden = _chrome->Statistics();
    if (hidden.surfaceBytes != 0)
    {
        SelfTestFailure(L"Hidden DxUi chrome retained a cached surface.\n");
        return 5;
    }
    if (_composition.CanvasSurfaceBytes() != 0)
    {
        SelfTestFailure(L"Hidden composition retained canvas surfaces.\n");
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
    windowClass.hIcon =
        static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    windowClass.hIconSm =
        static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!windowClass.hIcon || !windowClass.hIconSm)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
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

    const std::wstring title = LoadStringResource(nullptr, IDS_APP_TITLE);
    const HWND window = CreateWindowExW(extendedStyle, kWindowClassName, title.c_str(), windowStyle, CW_USEDEFAULT,
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

    // A saved placement (Specs/Core/Core_Settings.md) replaces the default size when it still meets a monitor.
    if (_settings.window.valid && _screenshotDpi == 0 && visible)
    {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        placement.showCmd = SW_HIDE;
        placement.rcNormalPosition =
            RECT{_settings.window.x, _settings.window.y, _settings.window.x + _settings.window.width,
                 _settings.window.y + _settings.window.height};
        if (MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL) != nullptr &&
            SetWindowPlacement(window, &placement))
        {
            _restoreMaximized = _settings.window.maximized;
        }
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
    chrome->SetLayerExpandHandler(
        [this](Guid id, bool expanded)
        {
            if (expanded)
            {
                _session.CollapsedGroupIDs().erase(id);
            }
            else
            {
                _session.CollapsedGroupIDs().insert(id);
            }
            SyncChrome();
            _frameInvalidated = true;
        });
    chrome->SetPaletteHandler(
        [this](uint32_t foreground, uint32_t background)
        {
            // Reached from inside a DxUi callback: sync chrome on the next frame rather than re-entering Prepare.
            _session.SetForegroundColor(foreground);
            _session.SetBackgroundColor(background);
            _layoutDirty = true;
            _frameInvalidated = true;
        });
    chrome->SetOpacityHandler(
        [this](double opacity, ChromeEditPhase phase)
        {
            ImageLayer* layer = _session.ActiveLayer();
            if (layer == nullptr || layer->isGroup)
            {
                return;
            }
            const double clamped = std::min(1.0, std::max(0.0, opacity));
            switch (phase)
            {
            case ChromeEditPhase::Preview:
                // One history entry spans the whole typed edit; the snapshot is taken before the first preview.
                if (!_opacityEditing)
                {
                    _session.BeginEdit(L"Layer Opacity");
                    _opacityEditing = true;
                }
                layer->opacity = clamped;
                break;
            case ChromeEditPhase::Commit:
                if (_opacityEditing)
                {
                    layer->opacity = clamped;
                    _session.EndEdit();
                    _opacityEditing = false;
                }
                else
                {
                    _session.SetOpacity(clamped);
                }
                break;
            case ChromeEditPhase::Cancel:
            default:
                // Restoring the start value makes the pending snapshot equal to the document: no entry is recorded.
                if (_opacityEditing)
                {
                    layer->opacity = clamped;
                    _session.EndEdit();
                    _opacityEditing = false;
                }
                break;
            }
            DocumentChanged();
        });
    chrome->SetLayoutHandler(
        [this]
        {
            _layoutDirty = true;
            _frameInvalidated = true;
        });
    chrome->SetTransformHandler([this](TransformAction action, double value) { OnTransformAction(action, value); });
    chrome->SetSelectionHandler([this](SelectionAction action, double value) { OnSelectionAction(action, value); });
    chrome->SetBrushHandler([this](BrushAction action, double value) { OnBrushAction(action, value); });
    chrome->SetGradientHandler([this](GradientAction action, double value) { OnGradientAction(action, value); });
    chrome->SetShapeHandler([this](ShapeAction action, double value) { OnShapeAction(action, value); });
    chrome->SetAdjustHandler([this](AdjustAction action, double value) { OnAdjustAction(action, value); });
    chrome->SetCurveHandler([this](std::vector<CurvePoint> points) { OnCurveChanged(std::move(points)); });
    chrome->SetBandHandler([this](int index, double degrees) { OnBandChanged(index, degrees); });
    chrome->SetAdjustColorHandler([this](ChromeEditPhase phase, uint32_t argb, bool highlights)
                                  { OnAdjustColor(phase, argb, highlights); });
    _chrome = std::move(chrome);
    _renderer.SetChrome(_chrome.get());
    _frameInvalidated = true;
    return S_OK;
}

HRESULT Application::InitializeDocument() noexcept
{
    _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    _tool = EditorTool::Move;
    if (_chrome)
    {
        _chrome->SetTool(_tool);
    }
    // A demo may pick its own tool; its layers and selection count as saved.
    ApplyDemo();
    _session.history.MarkSaved();
    _documentPath.clear();
    BindCompositionDevice();
    DocumentChanged();
    return S_OK;
}

Vec2 Application::DocumentSize() const noexcept
{
    if (!_session.Document().has_value())
    {
        return Vec2{1, 1};
    }
    return Vec2{static_cast<double>(_session.Document().value().width),
                static_cast<double>(_session.Document().value().height)};
}

void Application::DocumentChanged() noexcept
{
    _composition.InvalidateDocument();
    SyncChrome();
    _frameInvalidated = true;
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
    _chrome->SetUndoRedoNames(_session.history.UndoName(), _session.history.RedoName(), _session.history.CanUndo(),
                              _session.history.CanRedo());
    _chrome->SetEditTargets(_session.IsMaskSelected() && _session.ActiveLayer() != nullptr &&
                                _session.ActiveLayer()->mask.has_value(),
                            _session.CanTransformSelection());
    SyncAdjustPanel();
    const CanvasDocument* document = _session.DisplayDocument();
    const int percent = static_cast<int>(_viewport.zoom * 100.0 + 0.5);
    std::wstring zoom = FormatStringResource(nullptr, IDS_STATUS_ZOOM, percent);
    std::wstring size = document ? FormatStringResource(nullptr, IDS_STATUS_SIZE, document->width, document->height)
                                 : LoadStringResource(nullptr, IDS_STATUS_NO_DOCUMENT);
    std::wstring tool =
        FormatStringResource(nullptr, IDS_STATUS_TOOL, LoadStringResource(nullptr, EditorToolNameId(_tool)),
                             std::wstring(1, EditorToolKey(_tool)));
    _chrome->SetStatus(std::move(zoom), std::move(size), std::move(tool));
    _chrome->SetTool(_tool);
    _chrome->SetPalette(_session.ForegroundColor(), _session.BackgroundColor());
    const ImageLayer* activeLayer = _session.ActiveLayer();
    _chrome->SetActiveLayerOpacity(
        activeLayer != nullptr && !activeLayer->isGroup ? std::optional<double>(activeLayer->opacity) : std::nullopt);

    const float dpi = _screenshotDpi != 0 ? static_cast<float>(_screenshotDpi)
                                          : (_window ? static_cast<float>(GetDpiForWindow(_window.get())) : 96.0f);
    (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), dpi > 0.0f ? dpi : 96.0f);
    const CanvasHole hole = _chrome->HolePixels();
    _viewport.Resize(Vec2{std::max(1.0f, hole.width), std::max(1.0f, hole.height)}, std::max(1.0f, dpi / 96.0f),
                     DocumentSize());

    // The Move tool's box, snap guides and header follow the pending edit or the active layer.
    std::optional<TransformOverlayGeometry> geometry;
    std::vector<float> guideXs;
    std::vector<float> guideYs;
    TransformInspectorState inspector;
    inspector.visible = _tool == EditorTool::Move;
    if (document != nullptr && inspector.visible)
    {
        const Vec2 documentSize = DocumentSize();
        const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
        const ImageLayer* active = _session.ActiveLayer();
        const bool shows = _session.showsTransformControls || (edit.has_value() && edit->persistent);
        std::optional<LayerTransform> value;
        if (edit.has_value())
        {
            value = edit->draft;
        }
        else if (active != nullptr)
        {
            value = _session.EditedTransform(*active);
        }
        if (shows)
        {
            if ((edit.has_value() && edit->group.has_value()) || (!edit.has_value() && _session.TransformsAsGroup()))
            {
                if (edit.has_value() && edit->corners.has_value())
                {
                    geometry = TransformOverlayGeometry::FromCorners(edit->corners.value(), _viewport, documentSize);
                }
                else
                {
                    const std::optional<LayerTransform> box =
                        edit.has_value() ? std::optional<LayerTransform>(edit->draft) : _session.GroupTransformBox();
                    if (box.has_value())
                    {
                        geometry = TransformOverlayGeometry::FromTransform(box.value(), _viewport, documentSize);
                    }
                }
            }
            else if (active != nullptr && active->asset.has_value() && !active->isGroup && _session.CanTransform())
            {
                if (edit.has_value() && edit->layerID == active->id && edit->corners.has_value())
                {
                    geometry = TransformOverlayGeometry::FromCorners(edit->corners.value(), _viewport, documentSize);
                }
                else
                {
                    geometry = TransformOverlayGeometry::FromTransform(_session.EditedTransform(*active), _viewport,
                                                                       documentSize);
                }
            }
        }
        for (const double x : _session.LastSnapGuides().xs)
        {
            guideXs.push_back(static_cast<float>(_viewport.ViewFromDocument(Vec2{x, 0.0}, documentSize).x));
        }
        for (const double y : _session.LastSnapGuides().ys)
        {
            guideYs.push_back(static_cast<float>(_viewport.ViewFromDocument(Vec2{0.0, y}, documentSize).y));
        }
        inspector.enabled = _session.CanTransform() || edit.has_value();
        inspector.editing = edit.has_value();
        inspector.distorted = edit.has_value() && edit->corners.has_value();
        inspector.targetsMask = _session.TransformTargetsMask();
        if (value.has_value())
        {
            const std::optional<Vec2> pixels = _session.TransformPixelSize();
            Vec2 pixelSize = value->size;
            if (pixels.has_value())
            {
                pixelSize = pixels.value();
            }
            else if (active != nullptr && active->asset.has_value() && active->asset->image.HasStorage())
            {
                pixelSize = Vec2{static_cast<double>(active->asset->image.Width()),
                                 static_cast<double>(active->asset->image.Height())};
            }
            inspector.x = value->origin.x;
            inspector.y = value->origin.y;
            inspector.width = value->size.x;
            inspector.height = value->size.y;
            inspector.scale = value->ScalePercent(pixelSize);
            inspector.rotation = value->rotation;
            inspector.sampling = value->sampling == LayerSampling::Nearest  ? 0
                                 : value->sampling == LayerSampling::Smooth ? 1
                                                                            : 2;
        }
        inspector.autoSelect = _session.transformAutoSelect;
        inspector.showControls = _session.showsTransformControls;
        inspector.lockRatio = _session.locksTransformRatio;
    }
    _overlayGeometry = geometry;
    _chrome->SetTransformOverlay(geometry, guideXs, guideYs);
    _chrome->SetTransformInspector(inspector);

    // Selection tools: the header mirrors the session; the outline shows in every tool.
    SelectionInspectorState selection;
    selection.visible = EditorToolIsSelection(_tool);
    selection.enabled = document != nullptr;
    selection.tool = selection.visible ? _tool : EditorTool::Marquee;
    selection.marqueeKind = _session.marqueeKind == LassoKind::Ellipse ? 1 : 0;
    selection.lassoKind = _session.lassoKind == LassoKind::Polygonal ? 1 : 0;
    selection.mode = static_cast<int>(_session.DisplayedSelectionMode());
    selection.antialiasVisible = _tool == EditorTool::Lasso || _tool == EditorTool::Wand ||
                                 (_tool == EditorTool::Marquee && _session.marqueeKind == LassoKind::Ellipse);
    selection.antialiased = _session.selectionAntialiased;
    selection.tolerance = _session.wandSettings.tolerance;
    selection.sampleSize = static_cast<int>(_session.wandSettings.sampleSize);
    selection.sampleAllLayers = _session.wandSettings.sampleAllLayers;
    selection.contiguous = _session.wandSettings.contiguous;
    selection.canModify = _session.CanModifySelection();
    selection.expandAmount = _session.selectionExpandAmount;
    selection.contractAmount = _session.selectionContractAmount;
    // The outline follows a pixel move and the floating pixels of a selection transform.
    const std::optional<DocumentSelection> current = _session.DisplayedSelection();
    selection.hasSelection = current.has_value();
    selection.isEmpty = current.has_value() && current->IsEmpty();
    _chrome->SetSelectionInspector(selection);
    const Vec2 documentSize = DocumentSize();
    const Vec2 origin = _viewport.DocumentRectOrigin(documentSize);
    const double scale = _viewport.PointsPerPixel();
    // The outline geometry is rebuilt only when the selection or the view mapping changes.
    if (document == nullptr)
    {
        if (_overlaySelection.has_value())
        {
            _overlaySelection.reset();
            _chrome->SetSelectionOutline({});
        }
    }
    else if (current != _overlaySelection || origin != _overlayOrigin || scale != _overlayScale)
    {
        std::vector<std::vector<D2D1_POINT_2F>> loops;
        if (current.has_value())
        {
            loops.reserve(current->loops.size());
            for (const SelectionLoop& loop : current->loops)
            {
                std::vector<D2D1_POINT_2F> converted;
                converted.reserve(loop.size());
                for (const Vec2& point : loop)
                {
                    converted.push_back(D2D1::Point2F(static_cast<float>(origin.x + point.x * scale),
                                                      static_cast<float>(origin.y + point.y * scale)));
                }
                loops.push_back(std::move(converted));
            }
        }
        _overlaySelection = current;
        _overlayOrigin = origin;
        _overlayScale = scale;
        _chrome->SetSelectionOutline(std::move(loops));
    }
    std::optional<CanvasOverlay::Draft> draft;
    if (document != nullptr && _session.PendingLasso().has_value())
    {
        const LassoDraft& pending = _session.PendingLasso().value();
        CanvasOverlay::Draft converted;
        converted.kind = pending.kind;
        converted.points.reserve(pending.points.size());
        for (const Vec2& point : pending.points)
        {
            converted.points.push_back(D2D1::Point2F(static_cast<float>(origin.x + point.x * scale),
                                                     static_cast<float>(origin.y + point.y * scale)));
        }
        if (pending.cursor.has_value())
        {
            converted.cursor = D2D1::Point2F(static_cast<float>(origin.x + pending.cursor->x * scale),
                                             static_cast<float>(origin.y + pending.cursor->y * scale));
        }
        draft = std::move(converted);
    }
    _chrome->SetLassoDraft(std::move(draft));

    // Brush and Eraser: the header mirrors the session; the tip circle follows the pointer.
    BrushInspectorState brush;
    brush.visible = EditorToolIsBrush(_tool);
    brush.enabled = document != nullptr;
    brush.tool = EditorToolIsBrush(_tool) ? _tool : EditorTool::Brush;
    brush.mode = _session.brushMode == BrushToolMode::Erase ? 1 : 0;
    brush.size = _session.brushSettings.diameter;
    brush.hardness = _session.brushSettings.hardness * 100.0;
    brush.opacity = _session.brushSettings.opacity * 100.0;
    brush.maskSelected = _session.IsMaskSelected();
    brush.maskPaintWhite = _session.maskPaintWhite;
    brush.foregroundArgb = _session.ForegroundColor();
    brush.healMode = static_cast<int>(_session.spotHealingMode);
    brush.cloneAligned = _session.cloneSettings.aligned;
    brush.cloneAllLayers = _session.cloneSettings.sampleAllLayers;
    brush.cloneHasSource = _session.cloneSource.has_value();
    brush.smearMode = static_cast<int>(_session.smearMode);
    _chrome->SetBrushInspector(brush);
    SyncBrushCursor();

    // Gradient and Shape headers.
    const auto argbOf = [](const std::array<double, 4>& color)
    {
        const auto byte = [](double value)
        { return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0)); };
        return (byte(color[3]) << 24) | (byte(color[0]) << 16) | (byte(color[1]) << 8) | byte(color[2]);
    };
    const std::array<std::array<double, 4>, 2> ends = _session.GradientColors();
    GradientInspectorState gradient;
    gradient.enabled = document != nullptr;
    gradient.shape = _session.gradientSettings.shape == GradientShape::Radial ? 1 : 0;
    gradient.style = _session.gradientSettings.style == GradientStyle::ForegroundToTransparent ? 1 : 0;
    gradient.reversed = _session.gradientSettings.reversed;
    gradient.opacity = _session.gradientSettings.opacity * 100.0;
    gradient.maskSelected = _session.IsMaskSelected();
    gradient.pending = _session.PendingGradient() != nullptr;
    gradient.startArgb = argbOf(ends[0]);
    gradient.endArgb = argbOf(ends[1]);
    _chrome->SetGradientInspector(gradient);
    ShapeInspectorState shape;
    shape.enabled = document != nullptr;
    shape.kind = _session.shapeKind == ShapeKind::Ellipse ? 1 : 0;
    shape.radius = _session.shapeCornerRadius;
    shape.foregroundArgb = _session.ForegroundColor();
    _chrome->SetShapeInspector(shape);
    SyncToolOverlays();
    SyncGradientPreview();
}

void Application::BindCompositionDevice() noexcept
{
    ID3D11Device* device = _renderer.Device();
    ID3D11DeviceContext* context = _renderer.Context();
    if (!device || !context)
    {
        _composition.ReleaseDeviceResources();
        DropBrushEngine();
        return;
    }
    if (_composition.Device() != device)
    {
        (void)_composition.CreateDeviceResources(device, context);
        // The brush computes coverage on the same device; without it the CPU kernel paints. A stroke in
        // flight on the old device is dropped with it.
        DropBrushEngine();
        (void)_brushEngine.Initialize(device, context);
        _session.SetBrushEngine(_brushEngine.Ready() ? &_brushEngine : nullptr);
    }
}

void Application::DropBrushEngine() noexcept
{
    _session.CancelBrush();
    _brushDragging = false;
    _brushEngine.Release();
    _session.SetBrushEngine(nullptr);
}

void Application::CloseMainWindow() noexcept
{
    SaveSettings();
    if (_window)
    {
        (void)KillTimer(_window.get(), kScreenshotTimerId);
    }
    _renderer.SetChrome(nullptr);
    DropBrushEngine();
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
    // The settings directory watch waits beside the queue: a change reloads on this thread, never a poll.
    HANDLE handles[1]{};
    DWORD count = 0;
    if (const HANDLE watch = SettingsWatchHandle(); watch != nullptr)
    {
        handles[count++] = watch;
    }
    const DWORD waitResult =
        MsgWaitForMultipleObjectsEx(count, count ? handles : nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0 + count)
    {
        return true;
    }
    if (count != 0 && waitResult == WAIT_OBJECT_0)
    {
        OnSettingsDirectoryChanged(false);
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
    HANDLE handles[2]{waitable, nullptr};
    DWORD count = 1;
    if (const HANDLE watch = SettingsWatchHandle(); watch != nullptr)
    {
        handles[count++] = watch;
    }
    const DWORD waitResult = MsgWaitForMultipleObjectsEx(count, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (count == 2 && waitResult == WAIT_OBJECT_0 + 1)
    {
        OnSettingsDirectoryChanged(false);
        return false;
    }
    if (waitResult == WAIT_OBJECT_0 + count)
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

void Application::ZoomBy(double factor) noexcept
{
    _viewport.SetZoom(_viewport.zoom * factor, _viewport.Center(), DocumentSize());
    SyncChrome();
    _frameInvalidated = true;
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
        if (_brushHover.has_value() && !_brushDragging)
        {
            // The pointer is over the chrome: the tip circle leaves the canvas.
            _brushHover.reset();
            SyncBrushCursor();
        }
        _frameInvalidated = true;
        return;
    }

    const CanvasHole hole = _chrome->HolePixels();
    const bool inHole = event.xPixels >= hole.left && event.yPixels >= hole.top &&
                        event.xPixels < hole.left + hole.width && event.yPixels < hole.top + hole.height;
    const Vec2 documentSize = DocumentSize();
    const Vec2 view{event.xPixels - hole.left, event.yPixels - hole.top};
    const bool panMode = _spacePan || _tool == EditorTool::Hand;
    if (event.action == DxUi::PointerAction::Wheel && inHole)
    {
        const double factor = event.wheelDelta > 0 ? kWheelZoomStep : (1.0 / kWheelZoomStep);
        _viewport.SetZoom(_viewport.zoom * factor, view, documentSize);
        SyncChrome();
        _frameInvalidated = true;
        return;
    }
    if (!panMode && AdjustPointer(event, view, inHole))
    {
        return;
    }
    if ((SamplesColor() || _samplingColor) && !panMode)
    {
        // The Eyedropper (or Alt with the Brush, Spot Healing or Gradient) reads the canvas while pressed.
        if (event.action == DxUi::PointerAction::Down && inHole && message == WM_LBUTTONDOWN)
        {
            _canvasDrag = false;
            _samplingColor = true;
            _samplingOriginal = _chrome->ColorPickerOpen() ? _chrome->PickerColor() : _session.ForegroundColor();
            _sampleComposite = WandComposite();
            SampleColorAt(view);
            return;
        }
        if (event.action == DxUi::PointerAction::Move && _samplingColor)
        {
            SampleColorAt(view);
            return;
        }
        if ((event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave) && _samplingColor)
        {
            EndColorSampling();
            return;
        }
    }
    if (EditorToolIsBrush(_tool) && !panMode)
    {
        if (event.action == DxUi::PointerAction::Down && inHole && message == WM_LBUTTONDOWN)
        {
            _canvasDrag = false;
            if (_tool == EditorTool::Clone && GetKeyState(VK_MENU) < 0)
            {
                // Alt-click with Clone Stamp sets where it copies from.
                _session.SetCloneSource(_viewport.DocumentFromView(view, documentSize));
                SyncChrome();
                return;
            }
            if (WarpsWithSmear())
            {
                BeginWarpStroke(view);
            }
            else
            {
                BeginBrushStroke(view, event.modifiers);
            }
            return;
        }
        if (event.action == DxUi::PointerAction::Move)
        {
            if (_warpDragging)
            {
                ContinueWarpStroke(view);
                return;
            }
            if (_brushDragging)
            {
                ContinueBrushStroke(view);
                return;
            }
            _brushHover = inHole ? std::optional<Vec2>(_viewport.DocumentFromView(view, documentSize)) : std::nullopt;
            SyncBrushCursor();
            if (_tool == EditorTool::Clone)
            {
                SyncToolOverlays();
            }
        }
        if (event.action == DxUi::PointerAction::Up && (_brushDragging || _warpDragging))
        {
            if (_warpDragging)
            {
                ContinueWarpStroke(view);
                FinishWarpStroke();
            }
            else
            {
                ContinueBrushStroke(view);
                FinishBrushStroke();
            }
            return;
        }
        if (event.action == DxUi::PointerAction::Leave)
        {
            _brushHover.reset();
            FinishWarpStroke();
            if (_brushDragging)
            {
                FinishBrushStroke();
            }
            SyncBrushCursor();
        }
    }
    if (_tool == EditorTool::Gradient && !panMode)
    {
        if (event.action == DxUi::PointerAction::Down && inHole && message == WM_LBUTTONDOWN)
        {
            _canvasDrag = false;
            BeginGradientDrag(view);
            return;
        }
        if (event.action == DxUi::PointerAction::Move && _gradientHandle.has_value())
        {
            DragGradient(view, event.modifiers);
            return;
        }
        if ((event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave) &&
            _gradientHandle.has_value())
        {
            EndGradientDrag();
            return;
        }
    }
    if (_tool == EditorTool::Shape && !panMode)
    {
        const Vec2 pixel = _viewport.DocumentFromView(view, documentSize);
        const bool square = (event.modifiers & MK_SHIFT) != 0;
        // Unlike the Marquee, Alt has no other job here, so it draws from the center as in Photoshop.
        const bool fromCenter = GetKeyState(VK_MENU) < 0;
        if (event.action == DxUi::PointerAction::Down && inHole && message == WM_LBUTTONDOWN)
        {
            _canvasDrag = false;
            _session.BeginShape(pixel);
            _shapeDragging = _session.PendingShape().has_value();
            SyncToolOverlays();
            return;
        }
        if (event.action == DxUi::PointerAction::Move && _shapeDragging)
        {
            _session.DragShape(pixel, square, fromCenter);
            SyncToolOverlays();
            return;
        }
        if ((event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave) && _shapeDragging)
        {
            if (event.action == DxUi::PointerAction::Up)
            {
                _session.DragShape(pixel, square, fromCenter);
            }
            _shapeDragging = false;
            _session.FinishShape();
            DocumentChanged();
            return;
        }
    }
    if (EditorToolIsSelection(_tool) && !panMode)
    {
        if (event.action == DxUi::PointerAction::Down && inHole)
        {
            _canvasDrag = false;
            SelectionPress(view, event.modifiers, point);
            return;
        }
        if (event.action == DxUi::PointerAction::Move && SelectionPointerMove(view, event.modifiers))
        {
            return;
        }
        if (event.action == DxUi::PointerAction::Up &&
            (_selectionDragStart.has_value() || _lassoDragging || _pixelDragStart.has_value()))
        {
            SelectionRelease(view);
            return;
        }
        if (event.action == DxUi::PointerAction::Leave)
        {
            EndSelectionInteraction(true);
        }
    }
    if (_tool == EditorTool::Move && !panMode)
    {
        if (event.action == DxUi::PointerAction::Down && inHole)
        {
            _canvasDrag = false;
            BeginTransformDrag(view, event.modifiers);
            _frameInvalidated = true;
            return;
        }
        if (event.action == DxUi::PointerAction::Move && _transformDrag.has_value())
        {
            ContinueTransformDrag(view, event.modifiers);
            return;
        }
        if ((event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave) &&
            _transformDrag.has_value())
        {
            EndTransformDrag();
            return;
        }
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
    // Commands that change layers or files settle a pending transform first; view, history and About do not.
    switch (command)
    {
    case ChromeHost::kCommandUndo:
    case ChromeHost::kCommandRedo:
    case ChromeHost::kCommandZoomFit:
    case ChromeHost::kCommandZoomActual:
    case ChromeHost::kCommandZoomIn:
    case ChromeHost::kCommandZoomOut:
    case ChromeHost::kCommandSampleRing:
    case ChromeHost::kCommandAbout:
    case ChromeHost::kCommandFreeTransform:
        break;
    default:
        _transformDrag.reset();
        _session.CommitTransform();
        break;
    }
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
    case ChromeHost::kCommandExit:
        if (_window)
        {
            PostMessageW(_window.get(), WM_CLOSE, 0, 0);
        }
        break;
    case ChromeHost::kCommandUndo:
        _session.Undo();
        DocumentChanged();
        break;
    case ChromeHost::kCommandRedo:
        _session.Redo();
        DocumentChanged();
        break;
    case ChromeHost::kCommandNewLayer:
        _session.AddBlankLayer();
        DocumentChanged();
        break;
    case ChromeHost::kCommandNewFolder:
        _session.AddGroup();
        DocumentChanged();
        break;
    case ChromeHost::kCommandDeleteLayer:
        // The item follows the layers panel target: the mask when it is targeted, else the layer.
        _session.DeleteLayerOrMask();
        DocumentChanged();
        break;
    case ChromeHost::kCommandToggleLayerVisibility:
        if (_session.ActiveLayerID().has_value())
        {
            _session.ToggleVisibility(_session.ActiveLayerID().value());
            DocumentChanged();
        }
        break;
    case ChromeHost::kCommandMoveOutOfGroup:
        _session.MoveActiveLayerOutOfGroup();
        DocumentChanged();
        break;
    case ChromeHost::kCommandZoomFit:
        _viewport.Fit(DocumentSize());
        SyncChrome();
        _frameInvalidated = true;
        break;
    case ChromeHost::kCommandZoomActual:
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        SyncChrome();
        _frameInvalidated = true;
        break;
    case ChromeHost::kCommandZoomIn:
        ZoomBy(kCommandZoomStep);
        break;
    case ChromeHost::kCommandZoomOut:
        ZoomBy(1.0 / kCommandZoomStep);
        break;
    case ChromeHost::kCommandSampleRing:
        _session.showsSampleRing = !_session.showsSampleRing;
        if (_chrome)
        {
            _chrome->SetCommandChecked(ChromeHost::kCommandSampleRing, _session.showsSampleRing);
        }
        break;
    case ChromeHost::kCommandAbout:
        if (!_quietUi && _window)
        {
            const std::wstring text =
                FormatStringResource(nullptr, IDS_ABOUT_TEXT, std::wstring(L"" REDPRISM_VERSION_STRING_W));
            const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
            MessageBoxW(_window.get(), text.c_str(), caption.c_str(), MB_OK | MB_ICONINFORMATION);
        }
        break;
    case ChromeHost::kCommandFreeTransform:
        if (_tool != EditorTool::Move)
        {
            OnTool(EditorTool::Move);
        }
        // With a selection the selected pixels float and transform; otherwise the layer.
        _session.TransformCommand();
        DocumentChanged();
        break;
    case ChromeHost::kCommandFlipLayerHorizontal:
        _session.FlipLayers(true);
        DocumentChanged();
        break;
    case ChromeHost::kCommandFlipLayerVertical:
        _session.FlipLayers(false);
        DocumentChanged();
        break;
    case ChromeHost::kCommandFlipCanvasHorizontal:
        _session.FlipCanvas(true);
        DocumentChanged();
        break;
    case ChromeHost::kCommandFlipCanvasVertical:
        _session.FlipCanvas(false);
        DocumentChanged();
        break;
    case ChromeHost::kCommandCut:
        EndSelectionInteraction(false);
        if (_session.Selection().has_value() && _session.CanCopyPixels())
        {
            _session.CutSelection();
            MirrorClipboard();
            static_cast<void>(_session.TakeLastBrushCommit());
            DocumentChanged();
        }
        break;
    case ChromeHost::kCommandCopy:
        if (_session.CopySelection())
        {
            MirrorClipboard();
        }
        break;
    case ChromeHost::kCommandCopyMerged:
        if (_session.CanCopyMerged())
        {
            const std::optional<Raster> composite = WandComposite();
            if (composite.has_value() && _session.CopyMergedSelection(composite.value()))
            {
                MirrorClipboard();
            }
        }
        break;
    case ChromeHost::kCommandPaste:
        EndSelectionInteraction(false);
        PasteFromClipboard();
        break;
    case ChromeHost::kCommandFillForeground:
    case ChromeHost::kCommandFillBackground:
        _session.FillSelection(command == ChromeHost::kCommandFillForeground ? FillSource::Foreground
                                                                             : FillSource::Background);
        static_cast<void>(_session.TakeLastBrushCommit());
        DocumentChanged();
        break;
    case ChromeHost::kCommandClearPixels:
        _session.ClearSelectedPixels();
        static_cast<void>(_session.TakeLastBrushCommit());
        DocumentChanged();
        break;
    case ChromeHost::kCommandInvert:
        _session.InvertPixels();
        if (_composition.HasStroke() && _session.PendingGradient() == nullptr && _session.PendingBrush() == nullptr &&
            _session.PendingWarp() == nullptr && _session.PendingPixelMove() == nullptr)
        {
            // A pending gradient applied under the invert; its preview never showed the inverted pixels.
            _composition.EndStroke(nullptr, 0, 0);
        }
        static_cast<void>(_session.TakeLastBrushCommit());
        DocumentChanged();
        break;
    case ChromeHost::kCommandLayerViaCopy:
        EndSelectionInteraction(false);
        _session.LayerViaCopy();
        DocumentChanged();
        break;
    case ChromeHost::kCommandCurves:
    case ChromeHost::kCommandLevels:
    case ChromeHost::kCommandHueSaturation:
    case ChromeHost::kCommandExposure:
    case ChromeHost::kCommandGradientMap:
    case ChromeHost::kCommandGrain:
    case ChromeHost::kCommandGaussianBlur:
    case ChromeHost::kCommandMotionBlur:
    case ChromeHost::kCommandAddNoise:
    case ChromeHost::kCommandLensCorrection:
    case ChromeHost::kCommandContentAwareFill:
        EndSelectionInteraction(false);
        static_cast<void>(BeginColorEdit(command));
        break;
    case ChromeHost::kCommandNewLevelsAdjustment:
    case ChromeHost::kCommandNewCurvesAdjustment:
    case ChromeHost::kCommandNewHueSaturationAdjustment:
    case ChromeHost::kCommandNewExposureAdjustment:
    case ChromeHost::kCommandNewGradientMapAdjustment:
    case ChromeHost::kCommandNewGrainAdjustment:
        EndSelectionInteraction(false);
        AddAdjustmentLayer(command);
        break;
    case ChromeHost::kCommandEditAdjustment:
        EditActiveAdjustment();
        break;
    case ChromeHost::kCommandAddMask:
    case ChromeHost::kCommandAddBlackMask:
        EndSelectionInteraction(false);
        _session.AddMask(command == ChromeHost::kCommandAddMask);
        DocumentChanged();
        break;
    case ChromeHost::kCommandSelectAll:
        EndSelectionInteraction(false);
        _session.SelectAll();
        SelectionChanged();
        break;
    case ChromeHost::kCommandDeselect:
        EndSelectionInteraction(false);
        _session.Deselect();
        SelectionChanged();
        break;
    case ChromeHost::kCommandInverse:
        EndSelectionInteraction(false);
        _session.InvertSelection();
        SelectionChanged();
        break;
    case ChromeHost::kCommandLoadLayerSelection:
        EndSelectionInteraction(false);
        if (_session.ActiveLayerID().has_value())
        {
            static_cast<void>(_session.LoadLayerSelection(_session.ActiveLayerID().value()));
        }
        SelectionChanged();
        break;
    case ChromeHost::kCommandLoadMaskSelection:
        EndSelectionInteraction(false);
        if (_session.ActiveLayerID().has_value())
        {
            static_cast<void>(_session.LoadMaskSelection(_session.ActiveLayerID().value()));
        }
        SelectionChanged();
        break;
    default:
        break;
    }
}

void Application::OnTool(EditorTool tool) noexcept
{
    if (tool != _tool)
    {
        // Switching tools settles a pending transform, as selecting another layer does.
        _transformDrag.reset();
        _duplicateOnDrag = false;
        _session.CommitTransform();
        _session.ClearSnapGuides();
        EndSelectionInteraction(false);
        FinishBrushStroke();
        FinishWarpStroke();
        CommitGradient();
        ApplyColorEdit();
        if (_session.PendingFilter().has_value() && _session.PendingFilter()->previewFailed)
        {
            // A fill with nothing to copy from cannot apply; the tool switch drops it rather than trapping the panel.
            CancelColorEdit();
        }
        if (_session.PendingShape().has_value())
        {
            _shapeDragging = false;
            _session.CancelShape();
        }
        EndColorSampling();
        _session.SwitchBrushTipFamily(EditorToolTipFamily(_tool), EditorToolTipFamily(tool));
        _session.ClearPendingOpacityDigit();
    }
    _tool = tool;
    SyncBrushCursor();
    if (_chrome)
    {
        _chrome->SetTool(tool);
    }
    SyncChrome();
    _frameInvalidated = true;
}

bool Application::HandleAccelerator(UINT virtualKey, UINT modifiers, bool repeat) noexcept
{
    const bool control = (modifiers & MK_CONTROL) != 0;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    if (control)
    {
        switch (virtualKey)
        {
        case 'N':
            OnCommand(shift ? ChromeHost::kCommandNewLayer : ChromeHost::kCommandNew);
            return true;
        case 'O':
            OnCommand(ChromeHost::kCommandOpen);
            return true;
        case 'S':
            OnCommand(shift ? ChromeHost::kCommandSaveAs : ChromeHost::kCommandSave);
            return true;
        case 'Z':
            OnCommand(shift ? ChromeHost::kCommandRedo : ChromeHost::kCommandUndo);
            return true;
        case 'Y':
            OnCommand(ChromeHost::kCommandRedo);
            return true;
        case 'T':
            OnCommand(ChromeHost::kCommandFreeTransform);
            return true;
        case 'A':
            OnCommand(ChromeHost::kCommandSelectAll);
            return true;
        case 'D':
            OnCommand(ChromeHost::kCommandDeselect);
            return true;
        case 'I':
            OnCommand(shift ? ChromeHost::kCommandInverse : ChromeHost::kCommandInvert);
            return true;
        case 'X':
            OnCommand(ChromeHost::kCommandCut);
            return true;
        case 'C':
            OnCommand(shift ? ChromeHost::kCommandCopyMerged : ChromeHost::kCommandCopy);
            return true;
        case 'V':
            OnCommand(ChromeHost::kCommandPaste);
            return true;
        case 'J':
            OnCommand(ChromeHost::kCommandLayerViaCopy);
            return true;
        case VK_BACK:
            OnCommand(ChromeHost::kCommandFillBackground);
            return true;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
            if (_session.Selection().has_value() && !_session.Selection()->IsEmpty() &&
                !_session.PendingLasso().has_value() && GetKeyState(VK_MENU) >= 0)
            {
                // Ctrl-arrow moves the selected pixels in any tool, one pixel or ten with Shift, one entry each.
                const double step = shift ? 10.0 : 1.0;
                _session.NudgePixels(virtualKey == VK_LEFT ? -step : (virtualKey == VK_RIGHT ? step : 0.0),
                                     virtualKey == VK_UP ? -step : (virtualKey == VK_DOWN ? step : 0.0));
                static_cast<void>(_session.TakeLastBrushCommit());
                DocumentChanged();
                return true;
            }
            break;
        case 'G':
            OnCommand(shift ? ChromeHost::kCommandMoveOutOfGroup : ChromeHost::kCommandNewFolder);
            return true;
        case 'M':
            OnCommand(ChromeHost::kCommandCurves);
            return true;
        case 'L':
            OnCommand(ChromeHost::kCommandLevels);
            return true;
        case 'U':
            OnCommand(ChromeHost::kCommandHueSaturation);
            return true;
        case '0':
            OnCommand(ChromeHost::kCommandZoomFit);
            return true;
        case '1':
            OnCommand(ChromeHost::kCommandZoomActual);
            return true;
        case VK_OEM_PLUS:
        case VK_ADD:
            OnCommand(ChromeHost::kCommandZoomIn);
            return true;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            OnCommand(ChromeHost::kCommandZoomOut);
            return true;
        default:
            break;
        }
        return false;
    }
    if (virtualKey == VK_SPACE)
    {
        _spacePan = true;
        return true;
    }
    if (_chrome && _chrome->CanvasFocused())
    {
        if (_session.HasColorEdit() && (virtualKey == VK_RETURN || virtualKey == VK_ESCAPE))
        {
            // Enter is the panel's OK, Escape its Cancel.
            if (virtualKey == VK_ESCAPE)
            {
                CancelColorEdit();
            }
            else
            {
                ApplyColorEdit();
            }
            return true;
        }
        if (shift && (virtualKey == VK_OEM_PLUS || virtualKey == VK_OEM_MINUS))
        {
            // Shift+= and Shift+- step the active layer's blend mode, wrapping.
            _session.StepBlendMode(virtualKey == VK_OEM_PLUS);
            DocumentChanged();
            return true;
        }
        if (virtualKey == VK_ESCAPE && _pixelDragStart.has_value())
        {
            CancelPixelDrag();
            return true;
        }
        if (virtualKey == VK_BACK && GetKeyState(VK_MENU) < 0 && !_session.PendingLasso().has_value())
        {
            // Alt+Backspace fills with the foreground color (Ctrl+Backspace the background, above).
            OnCommand(ChromeHost::kCommandFillForeground);
            return true;
        }
        if (virtualKey == VK_ESCAPE && (_warpDragging || _session.PendingWarp() != nullptr))
        {
            CancelWarpStroke();
            return true;
        }
        if (virtualKey == VK_ESCAPE && _session.PendingShape().has_value())
        {
            _shapeDragging = false;
            _session.CancelShape();
            SyncToolOverlays();
            _frameInvalidated = true;
            return true;
        }
        if (_session.PendingGradient() != nullptr && (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN))
        {
            // Escape drops the pending gradient, Enter applies it.
            if (virtualKey == VK_ESCAPE)
            {
                CancelGradient();
            }
            else
            {
                CommitGradient();
            }
            return true;
        }
        if (virtualKey == VK_ESCAPE && (_brushDragging || _session.PendingBrush() != nullptr))
        {
            CancelBrushStroke();
            return true;
        }
        if (EditorToolIsBrush(_tool) && (virtualKey == VK_OEM_4 || virtualKey == VK_OEM_6))
        {
            // [ and ] step the size; with Shift the hardness, in quarters.
            if (shift)
            {
                _session.ChangeBrushHardness(virtualKey == VK_OEM_6);
            }
            else
            {
                _session.ChangeBrushSize(virtualKey == VK_OEM_6);
            }
            SyncChrome();
            return true;
        }
        if ((EditorToolIsBrush(_tool) || _tool == EditorTool::Move || _tool == EditorTool::Gradient) &&
            virtualKey >= '0' && virtualKey <= '9' && !shift && !control)
        {
            // Photoshop's opacity keys: brush opacity with a brush tool, the active layer's with the Move tool.
            const int digit = static_cast<int>(virtualKey - '0');
            if (EditorToolIsBrush(_tool))
            {
                _session.TypeOpacityDigit(digit, static_cast<double>(GetTickCount64()) / 1000.0);
            }
            else if (_tool == EditorTool::Gradient)
            {
                const int percent = _session.TypedOpacityPercent(digit, static_cast<double>(GetTickCount64()) / 1000.0);
                if (percent > 0)
                {
                    _session.gradientSettings.opacity = percent / 100.0;
                    _session.RefreshGradient();
                }
            }
            else
            {
                _session.SetOpacity(digit == 0 ? 1.0 : digit / 10.0);
            }
            DocumentChanged();
            return true;
        }
        if (_session.PendingLasso().has_value() &&
            (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || virtualKey == VK_BACK || virtualKey == VK_DELETE))
        {
            // Escape drops the outline, Enter closes it, Backspace or Delete removes the last polygonal corner.
            if (virtualKey == VK_ESCAPE)
            {
                _session.CancelLasso();
            }
            else if (virtualKey == VK_RETURN)
            {
                _session.FinishLasso();
            }
            else
            {
                _session.RemoveLastLassoPoint();
            }
            _lassoDragging = false;
            _marqueeDragPixel.reset();
            SelectionChanged();
            return true;
        }
        if (EditorToolIsSelection(_tool) && !_session.PendingLasso().has_value() && _session.Selection().has_value() &&
            !_session.Selection()->IsEmpty() &&
            (virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP || virtualKey == VK_DOWN))
        {
            // Arrow keys move the outline by a pixel, ten with Shift; each press is one entry.
            const double step = shift ? 10.0 : 1.0;
            _session.NudgeSelection(virtualKey == VK_LEFT ? -step : (virtualKey == VK_RIGHT ? step : 0.0),
                                    virtualKey == VK_UP ? -step : (virtualKey == VK_DOWN ? step : 0.0));
            SelectionChanged();
            return true;
        }
        if (virtualKey == VK_ESCAPE && (_transformDrag.has_value() || _session.PendingTransformEdit().has_value()))
        {
            _transformDrag.reset();
            _duplicateOnDrag = false;
            _session.CancelTransform();
            DocumentChanged();
            return true;
        }
        if (virtualKey == VK_RETURN && _session.PendingTransformEdit().has_value())
        {
            _transformDrag.reset();
            _session.CommitTransform();
            DocumentChanged();
            return true;
        }
        if (_tool == EditorTool::Move &&
            (virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP || virtualKey == VK_DOWN))
        {
            // Arrow keys nudge by a pixel, ten with Shift; a pending edit moves its draft, otherwise the move commits.
            const double step = shift ? 10.0 : 1.0;
            _session.NudgeLayer(virtualKey == VK_LEFT ? -step : (virtualKey == VK_RIGHT ? step : 0.0),
                                virtualKey == VK_UP ? -step : (virtualKey == VK_DOWN ? step : 0.0));
            DocumentChanged();
            return true;
        }
        if (virtualKey == VK_DELETE && shift)
        {
            // Shift+Delete fills the selection from its surroundings (Edit > Content-Aware Fill).
            EndSelectionInteraction(false);
            static_cast<void>(BeginColorEdit(ChromeHost::kCommandContentAwareFill));
            return true;
        }
        if (virtualKey == VK_DELETE)
        {
            // Delete clears the selected pixels; without a selection it deletes the targeted mask, or the layer.
            _transformDrag.reset();
            _session.CommitTransform();
            _session.DeleteKeyPressed();
            static_cast<void>(_session.TakeLastBrushCommit());
            DocumentChanged();
            return true;
        }
        if (virtualKey == 'X' || virtualKey == 'D')
        {
            // Legacy palette keys: X swaps foreground and background, D restores black over white.
            if (virtualKey == 'X')
            {
                _session.SwapPaletteColors();
            }
            else
            {
                _session.ResetPaletteColors();
            }
            SyncChrome();
            _frameInvalidated = true;
            return true;
        }
        const EditorTool tool = EditorToolFromKey(static_cast<wchar_t>(virtualKey));
        if (tool != EditorTool::Count)
        {
            // M or L pressed again switches the Marquee's shape or the Lasso's kind; holding the key does not keep
            // switching, and the choice stays as last set while the project is open.
            if (tool == _tool && !repeat && tool == EditorTool::Marquee)
            {
                _session.ToggleMarqueeKind();
            }
            else if (tool == _tool && !repeat && tool == EditorTool::Lasso)
            {
                _session.ToggleLassoKind();
            }
            else if (tool == _tool && !repeat && shift && tool == EditorTool::Shape)
            {
                // Shift-U: the Shape tool switches between Rectangle and Ellipse.
                _shapeDragging = false;
                _session.ToggleShapeKind();
            }
            OnTool(tool);
            return true;
        }
    }
    return false;
}

void Application::NewDocument() noexcept
{
    _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    _session.history.MarkSaved();
    _documentPath.clear();
    _viewport.followsFit = true;
    DocumentChanged();
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

void Application::ShowError(UINT stringId) noexcept
{
    if (_quietUi || !_window)
    {
        return;
    }
    const std::wstring text = LoadStringResource(nullptr, stringId);
    const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
    MessageBoxW(_window.get(), text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
}

void Application::OpenPath(const wchar_t* path) noexcept
{
    if (path == nullptr || path[0] == L'\0')
    {
        return;
    }
    if (LooksLikeProjectPath(path))
    {
        const ProjectLoadResult loaded = LoadProject(path);
        if (loaded.status != ProjectIoStatus::Ok || !loaded.document.width)
        {
            if (loaded.status == ProjectIoStatus::UnsupportedVersion && !_quietUi && _window)
            {
                const std::wstring text =
                    FormatStringResource(nullptr, IDS_ERROR_UNSUPPORTED_VERSION, loaded.formatVersion);
                const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
                MessageBoxW(_window.get(), text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
            }
            else
            {
                ShowError(LoadErrorStringId(loaded.status));
            }
            return;
        }
        // The loaded project replaces the document: no outline in progress or outline move may carry over.
        EndSelectionInteraction(false);
        _session.history.Reset();
        _session.Document() = loaded.document;
        _session.SelectLayer(loaded.activeLayerID);
        _session.CollapsedGroupIDs().clear();
        _session.history.MarkSaved();
        _documentPath = path;
        _viewport.followsFit = true;
        DocumentChanged();
        return;
    }

    if (!_session.Document().has_value())
    {
        _session.CreateDocument(kDefaultClientWidth, kDefaultClientHeight, true);
    }
    std::optional<Guid> active = _session.ActiveLayerID();
    _session.BeginEdit(L"Import Image");
    const ProjectIoStatus status = ImportImageOnto(_session.Document().value(), path, active);
    _session.SelectLayer(active);
    _session.EndEdit();
    if (status != ProjectIoStatus::Ok)
    {
        ShowError(status == ProjectIoStatus::TooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
        return;
    }
    _viewport.followsFit = true;
    DocumentChanged();
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
        const FilterStrings strings = LoadFilterStrings();
        const COMDLG_FILTERSPEC filters[] = {
            {strings.project.c_str(), L"*.prism"},
            {strings.exploded.c_str(), L"*.*"},
        };
        const std::wstring defaultName = LoadStringResource(nullptr, IDS_DEFAULT_PROJECT_NAME);
        if (!PickSavePath(_window.get(), defaultName.c_str(), L"prism", filters, 2, path))
        {
            return;
        }
    }
    const bool zip = PathLooksLikeZipProject(path.c_str()) || EndsWithNoCase(path, L".prism");
    const ProjectIoStatus status =
        SaveProject(path.c_str(), _session.Document().value(), _session.ActiveLayerID(), zip);
    if (status != ProjectIoStatus::Ok)
    {
        ShowError(status == ProjectIoStatus::TooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_SAVE_PROJECT);
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
        ShowError(IDS_ERROR_FLATTEN);
        return;
    }
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(_composition.Readback(bgra, width, height)) || width == 0 || height == 0)
    {
        ShowError(IDS_ERROR_READBACK);
        return;
    }
    const Raster raster = Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888,
                                       bgra.data(), bgra.size());
    const FilterStrings strings = LoadFilterStrings();
    std::wstring path;
    if (jpeg)
    {
        const COMDLG_FILTERSPEC filters[] = {{strings.jpeg.c_str(), L"*.jpg;*.jpeg"}};
        const std::wstring defaultName = LoadStringResource(nullptr, IDS_DEFAULT_EXPORT_JPEG);
        if (!PickSavePath(_window.get(), defaultName.c_str(), L"jpg", filters, 1, path))
        {
            return;
        }
        if (ExportDocumentJpeg(raster, JpegExportOptions{_settings.jpegQuality, _session.Document().value().resolution},
                               path.c_str()) != ProjectIoStatus::Ok)
        {
            ShowError(IDS_ERROR_EXPORT_JPEG);
        }
        return;
    }
    const COMDLG_FILTERSPEC filters[] = {{strings.png.c_str(), L"*.png"}};
    const std::wstring defaultName = LoadStringResource(nullptr, IDS_DEFAULT_EXPORT_PNG);
    if (!PickSavePath(_window.get(), defaultName.c_str(), L"png", filters, 1, path))
    {
        return;
    }
    if (ExportDocumentPng(raster, _session.Document().value().resolution, path.c_str()) != ProjectIoStatus::Ok)
    {
        ShowError(IDS_ERROR_EXPORT_PNG);
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
    case WM_GETOBJECT:
        if (_chrome)
        {
            if (const LRESULT provider = _chrome->HandleGetObject(wParam, lParam); provider != 0)
            {
                return provider;
            }
        }
        break;
    case WM_MOVE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (_chrome)
        {
            _chrome->UpdateAccessibilityPlacement();
        }
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        // High contrast or the app mode changed: the palette follows (Specs/UI/UI_ApplicationChrome.md, Theme).
        RefreshTheme();
        break;
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
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        if (length != 0)
        {
            std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
            if (DragQueryFileW(drop, 0, path.data(), length + 1) != 0)
            {
                path.resize(length);
                OpenPath(path.c_str());
            }
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
        UpdateSelectionModifiers();
        if (HandleAccelerator(static_cast<UINT>(wParam), KeyModifiers(), (lParam & 0x40000000) != 0))
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
        UpdateSelectionModifiers();
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
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            if (ApplyCanvasCursor())
            {
                return TRUE;
            }
            // Off the canvas the chrome decides (the arrow, the splitter's resize arrows): a tool cursor never lingers.
            POINT point{};
            if (_chrome && GetCursorPos(&point) && ScreenToClient(window, &point) &&
                _chrome->ApplyChromeCursor(static_cast<float>(point.x), static_cast<float>(point.y)))
            {
                return TRUE;
            }
        }
        break;
    case WM_CHAR:
        if (_chrome && _chrome->DispatchCharacter(static_cast<wchar_t>(wParam), KeyModifiers()))
        {
            _frameInvalidated = true;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kSettingsTimerId)
        {
            (void)KillTimer(window, kSettingsTimerId);
            (void)ReloadSettingsIfChanged();
            return 0;
        }
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
    if (_chrome)
    {
        _chrome->UpdateAccessibilityPlacement();
    }
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

// ── Move tool (C8) ───────────────────────────────────────────────────────

void Application::ApplyDemo() noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    if (_demo == L"transform")
    {
        // Two painted layers, the active one rotated, so the documentation screenshot shows the Move tool's box.
        InsertPaintedLayer(_session, L"Backdrop", 520, 340, Vec2{110, 120}, 0.0, 0xFF2F6FB0u);
        InsertPaintedLayer(_session, L"Card", 340, 220, Vec2{560, 320}, 20.0, 0xFFE58A2Eu);
        return;
    }
    if (_demo == L"selection")
    {
        // The same layers with an elliptical marquee plus a rectangle, so the screenshot shows the outline and the
        // Marquee's header.
        InsertPaintedLayer(_session, L"Backdrop", 520, 340, Vec2{110, 120}, 0.0, 0xFF2F6FB0u);
        InsertPaintedLayer(_session, L"Card", 340, 220, Vec2{560, 320}, 0.0, 0xFFE58A2Eu);
        _session.marqueeKind = LassoKind::Ellipse;
        _session.ApplySelection(SelectionGeometry::Ellipse(180, 160, 380, 260), SelectionMode::New,
                                L"Elliptical Marquee");
        _session.ApplySelection(SelectionGeometry::Rect(640, 380, 220, 130), SelectionMode::Add,
                                L"Rectangular Marquee");
        OnTool(EditorTool::Marquee);
        return;
    }
    if (_demo == L"brush")
    {
        // A backdrop with a few strokes (soft, hard, erased) on a layer above it, and the Brush's header.
        InsertPaintedLayer(_session, L"Backdrop", 620, 400, Vec2{160, 120}, 0.0, 0xFF2F6FB0u);
        _session.AddBlankLayer();
        const auto stroke = [&](const BrushSettings& settings, std::initializer_list<Vec2> points, bool erase)
        {
            _session.brushSettings = settings;
            bool first = true;
            for (const Vec2& point : points)
            {
                if (first)
                {
                    if (!_session.BeginBrush(point, erase))
                    {
                        return;
                    }
                    first = false;
                }
                else
                {
                    _session.ContinueBrush(point);
                }
            }
            _session.FinishBrush();
        };
        stroke(BrushSettings{90, 0, 0.90, 0.54, 0.18, 1, false},
               {Vec2{220, 200}, Vec2{420, 160}, Vec2{620, 260}, Vec2{800, 200}}, false);
        stroke(BrushSettings{36, 1, 0.96, 0.96, 0.96, 1, false}, {Vec2{230, 360}, Vec2{500, 300}, Vec2{760, 380}},
               false);
        stroke(BrushSettings{60, 0.5, 0.20, 0.72, 0.36, 0.6, false}, {Vec2{300, 440}, Vec2{700, 430}}, false);
        stroke(BrushSettings{70, 1, 0, 0, 0, 1, false}, {Vec2{560, 330}, Vec2{620, 420}}, true);
        _session.brushSettings = BrushSettings{60, 0.5, 0.90, 0.54, 0.18, 1, false};
        _brushHover = Vec2{880, 430};
        OnTool(EditorTool::Brush);
        return;
    }
    if (_demo == L"adjust")
    {
        // A painted backdrop and swatch with a Levels adjustment layer above them, its panel open on the histogram.
        InsertPaintedLayer(_session, L"Backdrop", 620, 400, Vec2{160, 120}, 0.0, 0xFF2F6FB0u);
        InsertPaintedLayer(_session, L"Swatch", 260, 180, Vec2{300, 200}, 12.0, 0xFFE58A2Eu);
        OnTool(EditorTool::Move);
        _session.AddAdjustment(AdjustmentKind::Levels);
        OpenAdjustmentEditor();
        if (_session.PendingLevels().has_value())
        {
            LevelsSettings settings = _session.PendingLevels()->settings;
            Adjust::SetLevelsCurrent(settings, LevelRange{20, 1.2, 230, 0, 255});
            _session.UpdateLevels(settings, true);
            DocumentChanged();
        }
    }
    if (_demo == L"retouch")
    {
        // A backdrop, a radial gradient on a layer above it, a rounded rectangle and an ellipse, and the Shape header.
        InsertPaintedLayer(_session, L"Backdrop", 620, 400, Vec2{160, 120}, 0.0, 0xFF2F6FB0u);
        _session.AddBlankLayer();
        _session.SetForegroundColor(0xFFFFFFFFu);
        _session.gradientSettings.shape = GradientShape::Radial;
        _session.gradientSettings.style = GradientStyle::ForegroundToTransparent;
        if (_session.BeginGradient(Vec2{470, 320}))
        {
            _session.MoveGradient(std::nullopt, Vec2{760, 320});
            _session.EndGradientDrag();
            _session.CommitGradient();
        }
        _session.SetForegroundColor(0xFFE58A2Eu);
        _session.shapeCornerRadius = 28;
        _session.BeginShape(Vec2{230, 200});
        _session.DragShape(Vec2{450, 330}, false, false);
        _session.FinishShape();
        _session.SetForegroundColor(0xFF3AA66Bu);
        _session.shapeKind = ShapeKind::Ellipse;
        _session.BeginShape(Vec2{520, 180});
        _session.DragShape(Vec2{720, 340}, false, false);
        _session.FinishShape();
        _session.shapeKind = ShapeKind::Rectangle;
        _session.SetForegroundColor(0xFFE58A2Eu);
        OnTool(EditorTool::Shape);
    }
}

void Application::OnTransformAction(TransformAction action, double value) noexcept
{
    switch (action)
    {
    case TransformAction::Apply:
        _transformDrag.reset();
        _session.CommitTransform();
        DocumentChanged();
        return;
    case TransformAction::Cancel:
        _transformDrag.reset();
        _session.CancelTransform();
        DocumentChanged();
        return;
    case TransformAction::AutoSelect:
        _session.transformAutoSelect = value != 0.0;
        SyncChrome();
        return;
    case TransformAction::ShowControls:
        _session.showsTransformControls = value != 0.0;
        SyncChrome();
        _frameInvalidated = true;
        return;
    case TransformAction::LockRatio:
        _session.locksTransformRatio = value != 0.0;
        SyncChrome();
        return;
    case TransformAction::FlipHorizontal:
        ChangeTransform([](LayerTransform& transform) { transform.flipX = !transform.flipX; });
        return;
    case TransformAction::FlipVertical:
        ChangeTransform([](LayerTransform& transform) { transform.flipY = !transform.flipY; });
        return;
    case TransformAction::SetX:
        ChangeTransform([value](LayerTransform& transform) { transform.origin.x = value; });
        return;
    case TransformAction::SetY:
        ChangeTransform([value](LayerTransform& transform) { transform.origin.y = value; });
        return;
    case TransformAction::SetWidth:
        if (value >= 1.0)
        {
            const bool lock = _session.locksTransformRatio;
            ChangeTransform(
                [value, lock](LayerTransform& transform)
                {
                    if (lock)
                    {
                        transform.size.y *= value / transform.size.x;
                    }
                    transform.size.x = value;
                });
        }
        return;
    case TransformAction::SetHeight:
        if (value >= 1.0)
        {
            const bool lock = _session.locksTransformRatio;
            ChangeTransform(
                [value, lock](LayerTransform& transform)
                {
                    if (lock)
                    {
                        transform.size.x *= value / transform.size.y;
                    }
                    transform.size.y = value;
                });
        }
        return;
    case TransformAction::SetScale:
        if (value > 0.0)
        {
            // 100 % draws the layer's pixels 1:1 (a blank layer's size before this edit, so typing never compounds).
            const std::optional<Vec2> pixels = _session.TransformPixelSize();
            ChangeTransform(
                [value, pixels](LayerTransform& transform)
                {
                    transform = transform.ScaledToPercent(value, pixels.has_value() ? pixels.value() : transform.size);
                });
        }
        return;
    case TransformAction::SetRotation:
        ChangeTransform([value](LayerTransform& transform) { transform.rotation = std::fmod(value, 360.0); });
        return;
    case TransformAction::SetSampling:
        ChangeTransform(
            [value](LayerTransform& transform)
            {
                transform.sampling = value <= 0.5   ? LayerSampling::Nearest
                                     : value <= 1.5 ? LayerSampling::Smooth
                                                    : LayerSampling::High;
            });
        return;
    }
}

void Application::ChangeTransform(const std::function<void(LayerTransform&)>& update) noexcept
{
    if (!_session.PendingTransformEdit().has_value())
    {
        _session.BeginTransform(true);
    }
    const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
    // Numbers describe an ordinary transform; while distorted, the handles are the controls.
    if (!edit.has_value() || edit->corners.has_value() || !update)
    {
        return;
    }
    LayerTransform value = edit->draft;
    update(value);
    _session.PreviewTransform(value);
    DocumentChanged();
}

std::optional<std::pair<Guid, bool>> Application::TransformPressLayer(Vec2 pixel, bool control) const
{
    if (!_session.Document().has_value())
    {
        return std::nullopt;
    }
    const CanvasDocument& document = _session.Document().value();
    // The top-most visible pixel layer under the pointer.
    std::optional<Guid> underPointer;
    const std::vector<Guid> order = RenderLayerIds(document);
    for (auto it = order.rbegin(); it != order.rend() && !underPointer.has_value(); ++it)
    {
        for (const ImageLayer& layer : document.layers)
        {
            if (layer.id == *it)
            {
                if (layer.asset.has_value() && _session.DisplayedTransform(layer).Contains(pixel))
                {
                    underPointer = layer.id;
                }
                break;
            }
        }
    }
    const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
    const ImageLayer* active = _session.ActiveLayer();
    const bool activeUsable = active != nullptr && active->asset.has_value() && !active->isGroup &&
                              !_session.TransformsAsGroup() && _session.CanTransform();
    const bool picks = !edit.has_value();
    // Ctrl picks the layer under the pointer; otherwise the active layer, unless auto-select finds another layer
    // under a press outside it. A press on empty canvas still drags the active layer.
    if (control && picks && underPointer.has_value())
    {
        return std::make_pair(underPointer.value(), true);
    }
    if (_session.TransformsAsGroup() && _session.ActiveLayerID().has_value())
    {
        const std::optional<LayerTransform> box =
            edit.has_value() ? std::optional<LayerTransform>(edit->draft) : _session.GroupTransformBox();
        if ((box.has_value() && box->Contains(pixel)) || !(picks && _session.transformAutoSelect) ||
            !underPointer.has_value())
        {
            return std::make_pair(_session.ActiveLayerID().value(), false);
        }
    }
    if (activeUsable && _session.EditedTransform(*active).Contains(pixel))
    {
        return std::make_pair(active->id, false);
    }
    if (picks && (_session.transformAutoSelect || control) && underPointer.has_value())
    {
        return std::make_pair(underPointer.value(), true);
    }
    if (activeUsable)
    {
        return std::make_pair(active->id, false);
    }
    return std::nullopt;
}

void Application::BeginTransformDrag(Vec2 view, UINT modifiers) noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    const bool control = (modifiers & MK_CONTROL) != 0;
    const bool alt = GetKeyState(VK_MENU) < 0;
    std::optional<TransformHit> hit;
    if (_overlayGeometry.has_value())
    {
        hit = _overlayGeometry->Hit(view);
    }
    TransformDrag::Mode mode = TransformDrag::Mode::Move;
    int handle = -1;
    if (hit.has_value())
    {
        mode = hit->mode;
        handle = hit->handle;
    }
    else
    {
        const std::optional<std::pair<Guid, bool>> target = TransformPressLayer(pixel, control);
        if (!target.has_value())
        {
            return;
        }
        if (target->second)
        {
            _session.SelectLayer(target->first);
        }
    }
    // Alt-dragging the body duplicates the layer once the pointer moves.
    _duplicateOnDrag = mode == TransformDrag::Mode::Move && alt;
    if (!_session.PendingTransformEdit().has_value())
    {
        _session.BeginTransform(false);
    }
    // Ctrl-dragging a handle distorts, as in Photoshop; once distorted, handles keep distorting.
    if (mode == TransformDrag::Mode::Resize && _session.PendingTransformEdit().has_value() &&
        (control || _session.PendingTransformEdit()->corners.has_value()))
    {
        _session.BeginDistort();
        if (_session.PendingTransformEdit()->corners.has_value())
        {
            mode = TransformDrag::Mode::Distort;
        }
    }
    const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
    if (!edit.has_value())
    {
        return;
    }
    _transformDrag = TransformDrag{mode, handle, edit->draft, pixel, edit->corners};
    SyncChrome();
}

void Application::ContinueTransformDrag(Vec2 view, UINT modifiers) noexcept
{
    if (!_transformDrag.has_value() || !_session.Document().has_value())
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool control = (modifiers & MK_CONTROL) != 0;
    const bool alt = GetKeyState(VK_MENU) < 0;
    if (_duplicateOnDrag)
    {
        _duplicateOnDrag = false;
        _session.BeginDuplicateTransform();
    }
    const std::optional<std::array<Vec2, 4>> corners = _transformDrag->Corners(pixel, shift);
    if (corners.has_value())
    {
        _session.PreviewCorners(corners.value());
    }
    else
    {
        // Dragging, scaling and rotating land on whole pixels and degrees; typed values stay exact. Moving snaps
        // to the canvas and the other layers unless Ctrl is held; resizing and rotating are left alone.
        LayerTransform draft = _transformDrag->Updated(pixel, _session.locksTransformRatio, shift, alt).Rounded();
        if (_transformDrag->mode == TransformDrag::Mode::Move && !control)
        {
            std::unordered_set<Guid> moving;
            const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
            if (edit.has_value())
            {
                if (edit->group.has_value())
                {
                    for (const auto& [id, original] : edit->group->originals)
                    {
                        moving.insert(id);
                    }
                }
                else
                {
                    moving.insert(edit->layerID);
                }
            }
            draft = _session.SnappedMove(draft, moving,
                                         TransformSnap::kDistanceDip / std::max(_viewport.PointsPerPixel(), 0.0001));
        }
        _session.PreviewTransform(draft);
    }
    DocumentChanged();
}

void Application::EndTransformDrag() noexcept
{
    _duplicateOnDrag = false;
    _transformDrag.reset();
    _session.ClearSnapGuides();
    const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
    if (edit.has_value() && !edit->persistent)
    {
        _session.CommitTransform();
    }
    DocumentChanged();
}

bool Application::ApplyCanvasCursor() noexcept
{
    if (!_window || !_chrome)
    {
        return false;
    }
    POINT point{};
    if (!GetCursorPos(&point) || !ScreenToClient(_window.get(), &point))
    {
        return false;
    }
    const CanvasHole hole = _chrome->HolePixels();
    const bool inHole = _chrome->IsCanvasPoint(static_cast<float>(point.x), static_cast<float>(point.y));
    if (!inHole && !_transformDrag.has_value())
    {
        return false;
    }
    LPCWSTR cursor = IDC_ARROW;
    const Vec2 view{static_cast<double>(point.x) - hole.left, static_cast<double>(point.y) - hole.top};
    if (_spacePan || _tool == EditorTool::Hand)
    {
        cursor = IDC_HAND;
    }
    else if (_tool == EditorTool::Zoom)
    {
        cursor = IDC_CROSS;
    }
    else if (_samplingColor || SamplesColor() || _tool == EditorTool::Gradient || _tool == EditorTool::Shape)
    {
        cursor = IDC_CROSS;
    }
    else if (EditorToolIsBrush(_tool))
    {
        // The tip circle in the overlay is the cursor; the arrow would only hide the pixels under it.
        SetCursor(_session.Document().has_value() ? nullptr : LoadCursorW(nullptr, IDC_CROSS));
        return true;
    }
    else if (EditorToolIsSelection(_tool))
    {
        // A crosshair draws; over the selection in New mode (and while moving it) the four-way arrow moves the
        // outline. The add/subtract badges of the legacy cursors wait on custom cursors.
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool alt = GetKeyState(VK_MENU) < 0;
        const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
        cursor = IDC_CROSS;
        const bool control = GetKeyState(VK_CONTROL) < 0;
        if (_selectionDragStart.has_value() || _pixelDragStart.has_value() ||
            ((control || _session.LassoCursorMode(shift, alt) == SelectionMode::New) &&
             _session.CanMoveSelection(pixel)))
        {
            cursor = IDC_SIZEALL;
        }
    }
    else if (_tool == EditorTool::Move)
    {
        std::optional<TransformHit> hit;
        if (_transformDrag.has_value())
        {
            hit = TransformHit{_transformDrag->mode, _transformDrag->handle};
        }
        else if (_overlayGeometry.has_value())
        {
            hit = _overlayGeometry->Hit(view);
        }
        const bool control = GetKeyState(VK_CONTROL) < 0;
        if (hit.has_value())
        {
            const bool distorting = hit->mode == TransformDrag::Mode::Distort ||
                                    (hit->mode == TransformDrag::Mode::Resize &&
                                     (control || (_session.PendingTransformEdit().has_value() &&
                                                  _session.PendingTransformEdit()->corners.has_value())));
            if (distorting || hit->mode == TransformDrag::Mode::Rotate)
            {
                cursor = IDC_CROSS;
            }
            else if (hit->mode == TransformDrag::Mode::Resize)
            {
                const int direction =
                    _overlayGeometry.has_value() ? _overlayGeometry->ResizeCursorDirection(hit->handle) : 0;
                cursor = direction == 0   ? IDC_SIZEWE
                         : direction == 1 ? IDC_SIZENWSE
                         : direction == 2 ? IDC_SIZENS
                                          : IDC_SIZENESW;
            }
            else
            {
                cursor = IDC_SIZEALL;
            }
        }
        else if (TransformPressLayer(_viewport.DocumentFromView(view, DocumentSize()), control).has_value())
        {
            cursor = IDC_SIZEALL;
        }
    }
    SetCursor(LoadCursorW(nullptr, cursor));
    return true;
}

void Application::OnSelectionAction(SelectionAction action, double value) noexcept
{
    const auto whole = [value](int minimum, int maximum)
    { return std::clamp(static_cast<int>(std::lround(value)), minimum, maximum); };
    switch (action)
    {
    case SelectionAction::SetMarqueeKind:
        EndSelectionInteraction(false);
        _session.marqueeKind = value >= 0.5 ? LassoKind::Ellipse : LassoKind::Rectangle;
        break;
    case SelectionAction::SetLassoKind:
        EndSelectionInteraction(false);
        _session.lassoKind = value >= 0.5 ? LassoKind::Polygonal : LassoKind::Freehand;
        break;
    case SelectionAction::SetMode:
        _session.selectionModeChoice = static_cast<SelectionMode>(whole(0, 2));
        break;
    case SelectionAction::SetAntialias:
        _session.selectionAntialiased = value != 0.0;
        break;
    case SelectionAction::SetTolerance:
        _session.wandSettings.tolerance = whole(0, 255);
        break;
    case SelectionAction::SetSampleSize:
        _session.wandSettings.sampleSize = static_cast<WandSampleSize>(whole(0, 2));
        break;
    case SelectionAction::SetSampleAllLayers:
        _session.wandSettings.sampleAllLayers = value != 0.0;
        break;
    case SelectionAction::SetContiguous:
        _session.wandSettings.contiguous = value != 0.0;
        break;
    case SelectionAction::SetExpandAmount:
        _session.selectionExpandAmount = whole(1, DocumentSession::kSelectionModifyLimit);
        break;
    case SelectionAction::Expand:
        _session.ExpandSelection(_session.selectionExpandAmount);
        break;
    case SelectionAction::SetContractAmount:
        _session.selectionContractAmount = whole(1, DocumentSession::kSelectionModifyLimit);
        break;
    case SelectionAction::Contract:
        _session.ContractSelection(_session.selectionContractAmount);
        break;
    case SelectionAction::Deselect:
        EndSelectionInteraction(false);
        _session.Deselect();
        break;
    }
    SelectionChanged();
}

void Application::SelectionPress(Vec2 view, UINT modifiers, POINT client) noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    // A second press within the system double-click time and distance closes a polygonal outline.
    const ULONGLONG now = GetTickCount64();
    const bool doubleClick = now - _lastPressTick <= GetDoubleClickTime() &&
                             std::abs(client.x - _lastPressPoint.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                             std::abs(client.y - _lastPressPoint.y) <= GetSystemMetrics(SM_CYDOUBLECLK);
    _lastPressTick = now;
    _lastPressPoint = client;
    const Vec2 documentSize = DocumentSize();
    const Vec2 pixel = _viewport.DocumentFromView(view, documentSize);
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool alt = GetKeyState(VK_MENU) < 0;
    // A Shift held at the press means Add; for the Marquee it squares only once pressed afresh.
    _marqueeConstrainArmed = !shift;
    _marqueeDragPixel.reset();
    const std::optional<LassoDraft>& draft = _session.PendingLasso();
    if (draft.has_value() && draft->kind == LassoKind::Polygonal && !draft->points.empty())
    {
        // Polygonal adds a corner per click and closes on a click near the first corner or a double-click.
        const Vec2 first = _viewport.ViewFromDocument(draft->points.front(), documentSize);
        const double closeDistance = 8.0 * std::max(1.0, _viewport.backingScale);
        if (doubleClick ||
            (draft->points.size() >= 3 && std::hypot(view.x - first.x, view.y - first.y) <= closeDistance))
        {
            _session.FinishLasso();
        }
        else
        {
            _session.ExtendLasso(pixel);
        }
        SelectionChanged();
        return;
    }
    // Ctrl-drag inside the selection cuts and moves its pixels (Photoshop's temporary Move tool); Alt duplicates.
    const bool control = (modifiers & MK_CONTROL) != 0 || GetKeyState(VK_CONTROL) < 0;
    if (control && _session.CanMoveSelection(pixel))
    {
        BeginPixelDrag(pixel, alt);
        return;
    }
    const SelectionMode mode = _session.SelectionModeFor(shift, alt);
    // In New mode, dragging inside the selection moves its outline instead of drawing.
    if (mode == SelectionMode::New && _session.CanMoveSelection(pixel) && _session.BeginSelectionMove())
    {
        _selectionDragStart = pixel;
        _selectionMoveDelta = {};
        SelectionChanged();
        return;
    }
    if (_tool == EditorTool::Wand)
    {
        RunMagicWand(pixel, mode);
        return;
    }
    if (_tool == EditorTool::Marquee)
    {
        _session.BeginMarquee(pixel, mode);
    }
    else
    {
        _session.BeginLasso(pixel, mode);
    }
    _lassoDragging = _session.PendingLasso().has_value();
    SelectionChanged();
}

bool Application::SelectionPointerMove(Vec2 view, UINT modifiers) noexcept
{
    if (!_session.Document().has_value())
    {
        return false;
    }
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool alt = GetKeyState(VK_MENU) < 0;
    // Keys may have changed while the pointer was elsewhere; the header shows the mode a press would use.
    bool changed = _session.UpdateHeldSelectionKeys(shift, alt);
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    if (_pixelDragStart.has_value())
    {
        DragPixels(pixel, shift);
        return true;
    }
    if (_selectionDragStart.has_value())
    {
        // Shift keeps the move on one axis: whichever way the drag has gone further.
        double dx = pixel.x - _selectionDragStart->x;
        double dy = pixel.y - _selectionDragStart->y;
        if (shift)
        {
            if (std::fabs(dx) >= std::fabs(dy))
            {
                dy = 0.0;
            }
            else
            {
                dx = 0.0;
            }
        }
        _selectionMoveDelta = Vec2{std::round(dx), std::round(dy)};
        _session.MoveSelection(dx, dy);
        SelectionChanged();
        return true;
    }
    const std::optional<LassoDraft>& draft = _session.PendingLasso();
    if (draft.has_value())
    {
        switch (draft->kind)
        {
        case LassoKind::Freehand:
            if (_lassoDragging)
            {
                _session.ExtendLasso(pixel);
            }
            break;
        case LassoKind::Polygonal:
            _session.MoveLassoCursor(pixel);
            break;
        case LassoKind::Rectangle:
        case LassoKind::Ellipse:
            if (_lassoDragging)
            {
                // Shift squares the box, except a Shift already held when the drag began (it chose Add) until it
                // has been let go and pressed again.
                if (!shift)
                {
                    _marqueeConstrainArmed = true;
                }
                _marqueeDragPixel = pixel;
                _session.DragMarquee(pixel, _marqueeConstrainArmed && shift, false);
            }
            break;
        }
        SelectionChanged();
        return true;
    }
    if (changed)
    {
        SelectionChanged();
    }
    return false;
}

void Application::SelectionRelease(Vec2 view) noexcept
{
    static_cast<void>(view);
    if (_pixelDragStart.has_value())
    {
        FinishPixelDrag();
        return;
    }
    if (_selectionDragStart.has_value())
    {
        const Vec2 start = _selectionDragStart.value();
        const bool moved = _selectionMoveDelta.x != 0.0 || _selectionMoveDelta.y != 0.0;
        _selectionDragStart.reset();
        _session.EndSelectionMove();
        if (!moved && _tool == EditorTool::Wand)
        {
            // The wand's click inside the selection selects afresh from that pixel.
            RunMagicWand(start, SelectionMode::New);
            return;
        }
        if (!moved)
        {
            // A click without a drag deselects, as anywhere else with the lasso.
            _session.Deselect();
        }
        SelectionChanged();
        return;
    }
    const std::optional<LassoDraft>& draft = _session.PendingLasso();
    if (draft.has_value() && draft->kind != LassoKind::Polygonal)
    {
        _session.FinishLasso();
    }
    _lassoDragging = false;
    _marqueeDragPixel.reset();
    SelectionChanged();
}

void Application::EndSelectionInteraction(bool keepDraft) noexcept
{
    // Capture lost or a tool change: a polygonal outline survives a pointer leave, a drag does not.
    const std::optional<LassoDraft>& draft = _session.PendingLasso();
    if (draft.has_value() && (!keepDraft || draft->kind != LassoKind::Polygonal))
    {
        _session.CancelLasso();
    }
    if (_session.IsMovingSelection())
    {
        _session.EndSelectionMove();
    }
    if (_pixelDragStart.has_value())
    {
        CancelPixelDrag();
    }
    _selectionDragStart.reset();
    _lassoDragging = false;
    _marqueeDragPixel.reset();
    SelectionChanged();
}

void Application::UpdateSelectionModifiers() noexcept
{
    if (!EditorToolIsSelection(_tool))
    {
        return;
    }
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool alt = GetKeyState(VK_MENU) < 0;
    bool changed = _session.UpdateHeldSelectionKeys(shift, alt);
    // A Marquee drag reshapes as Shift goes down or up, without waiting for the pointer to move.
    if (_marqueeDragPixel.has_value() && _lassoDragging)
    {
        if (!shift)
        {
            _marqueeConstrainArmed = true;
        }
        _session.DragMarquee(_marqueeDragPixel.value(), _marqueeConstrainArmed && shift, false);
        changed = true;
    }
    if (changed)
    {
        SelectionChanged();
    }
}

void Application::SelectionChanged() noexcept
{
    SyncChrome();
    _frameInvalidated = true;
}

void Application::RunMagicWand(Vec2 pixel, SelectionMode mode) noexcept
{
    std::optional<Raster> composite;
    if (_session.wandSettings.sampleAllLayers)
    {
        composite = WandComposite();
        if (!composite.has_value())
        {
            ShowError(IDS_ERROR_READBACK);
            return;
        }
    }
    const WandFailure failure = _session.MagicWand(pixel, mode, composite.has_value() ? &composite.value() : nullptr);
    if (failure != WandFailure::None)
    {
        ShowError(failure == WandFailure::TooDetailed ? IDS_ERROR_WAND_DETAILED : IDS_ERROR_WAND_MEMORY);
    }
    SelectionChanged();
}

std::optional<Raster> Application::WandComposite() noexcept
{
    // Every visible layer as shown on the canvas: the composition's flattened readback at document size.
    if (!_session.Document().has_value())
    {
        return std::nullopt;
    }
    BindCompositionDevice();
    if (FAILED(_composition.Compose(_session.Document().value())))
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(_composition.Readback(bgra, width, height)) || width == 0 || height == 0)
    {
        return std::nullopt;
    }
    const Raster raster = Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888,
                                       bgra.data(), bgra.size());
    if (!raster.HasStorage())
    {
        return std::nullopt;
    }
    return raster;
}

// ── Brush and Eraser ──────────────────────────────────────────────────────

bool Application::BrushErases() const noexcept
{
    return _tool == EditorTool::Eraser || (_tool == EditorTool::Brush && _session.brushMode == BrushToolMode::Erase);
}

void Application::OnBrushAction(BrushAction action, double value) noexcept
{
    // Reached from inside a DxUi callback: chrome re-syncs on the next frame rather than re-entering Prepare.
    switch (action)
    {
    case BrushAction::SetMode:
        _session.brushMode = value >= 0.5 ? BrushToolMode::Erase : BrushToolMode::Paint;
        break;
    case BrushAction::SetSize:
        if (std::isfinite(value))
        {
            _session.brushSettings.diameter = std::clamp(std::round(value), 1.0, 2000.0);
        }
        break;
    case BrushAction::SetHardness:
        if (std::isfinite(value))
        {
            _session.brushSettings.hardness = std::clamp(value, 0.0, 100.0) / 100.0;
        }
        break;
    case BrushAction::SetOpacity:
        if (std::isfinite(value))
        {
            _session.brushSettings.opacity = std::clamp(value, 1.0, 100.0) / 100.0;
        }
        break;
    case BrushAction::SetMaskPaint:
        _session.maskPaintWhite = value >= 0.5;
        break;
    case BrushAction::OpenColor:
        if (_chrome)
        {
            _chrome->OpenColorPicker(false);
        }
        break;
    case BrushAction::SetHealMode:
        _session.spotHealingMode = value >= 1.5   ? SpotHealingMode::ProximityMatch
                                   : value >= 0.5 ? SpotHealingMode::CreateTexture
                                                  : SpotHealingMode::ContentAware;
        break;
    case BrushAction::SetCloneAligned:
        _session.cloneSettings.aligned = value >= 0.5;
        break;
    case BrushAction::SetCloneSample:
        _session.cloneSettings.sampleAllLayers = value >= 0.5;
        break;
    case BrushAction::SetSmearMode:
        FinishWarpStroke();
        _session.smearMode = value >= 1.5 ? SmearMode::Smudge : value >= 0.5 ? SmearMode::Blur : SmearMode::Liquify;
        break;
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::BeginBrushStroke(Vec2 view, UINT modifiers) noexcept
{
    if (!_session.Document().has_value() || _brushDragging)
    {
        return;
    }
    // The stroke's coverage and its preview live on the current device.
    BindCompositionDevice();
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    const BrushKind kind = BrushKindFor();
    const bool couldPaint = _session.CanPaint();
    if (kind == BrushKind::Clone && !_session.CloneStrokeOffset(pixel).has_value())
    {
        ShowError(IDS_ERROR_CLONE_SOURCE);
        return;
    }
    std::optional<Raster> composite;
    if (kind == BrushKind::Clone && _session.cloneSettings.sampleAllLayers)
    {
        composite = WandComposite();
    }
    const Raster* sample = composite.has_value() ? &composite.value() : nullptr;
    bool begun = false;
    if ((modifiers & MK_SHIFT) != 0)
    {
        // Shift-click paints a straight line from the end of the last stroke on this layer or mask.
        const std::optional<Vec2> start = _session.ShiftLineStart();
        if (start.has_value())
        {
            begun = _session.BeginBrush(start.value(), kind, sample);
            if (begun)
            {
                _session.ContinueBrush(pixel);
            }
        }
    }
    if (!begun)
    {
        begun = _session.BeginBrush(pixel, kind, sample);
    }
    const BrushStroke* stroke = _session.PendingBrush();
    if (!begun || stroke == nullptr)
    {
        // Spot Healing and Clone Stamp have nothing to do on a mask; anything else that could paint ran out of
        // budget.
        const bool maskRefusal = _session.IsMaskSelected() && (kind == BrushKind::Heal || kind == BrushKind::Clone);
        if (couldPaint && !maskRefusal)
        {
            ShowError(IDS_ERROR_BRUSH_BUDGET);
        }
        return;
    }
    CanvasRenderer::StrokePreview preview;
    preview.layerID = stroke->LayerID();
    preview.mask = stroke->IsMask();
    preview.width = stroke->Width();
    preview.height = stroke->Height();
    preview.transform = stroke->PaintTransform();
    preview.fill = stroke->FillValue();
    const Raster& source = stroke->SourceImage();
    if (source.HasStorage() && source.Width() == stroke->SourceRect().width &&
        source.Height() == stroke->SourceRect().height)
    {
        preview.source = source;
        preview.sourceX = stroke->SourceRect().x;
        preview.sourceY = stroke->SourceRect().y;
    }
    else if (source.HasStorage())
    {
        // A uniform mask stretched over the grid is its one value everywhere.
        std::uint8_t uniform = preview.fill;
        source.ReadRegion(0, 0, 1, 1, &uniform, source.Channels(), preview.fill);
        preview.fill = uniform;
    }
    (void)_composition.BeginStroke(preview);
    _brushDragging = true;
    _brushHover = pixel;
    PushStrokeTiles();
    SyncBrushCursor();
}

void Application::PushStrokeTiles() noexcept
{
    BrushStroke* stroke = _session.MutableBrush();
    if (stroke == nullptr || !_composition.HasStroke())
    {
        _frameInvalidated = true;
        return;
    }
    std::vector<RasterTile> tiles;
    if (_composition.StrokeTextureMissing())
    {
        // The working texture is (re)built from every touched tile.
        tiles = stroke->Patches();
        static_cast<void>(stroke->TakeChanged());
    }
    else
    {
        for (const std::size_t index : stroke->TakeChanged())
        {
            const BrushStroke::Tile& tile = stroke->Tiles()[index];
            tiles.push_back(RasterTile{tile.rect.x, tile.rect.y, tile.image});
        }
    }
    if (!tiles.empty())
    {
        (void)_composition.UpdateStrokeTiles(tiles);
    }
    _frameInvalidated = true;
}

void Application::ContinueBrushStroke(Vec2 view) noexcept
{
    if (!_brushDragging)
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    _brushHover = pixel;
    _session.ContinueBrush(pixel);
    if (_session.PendingBrush() == nullptr)
    {
        // The stroke ran out of pixel budget or the engine failed: it is gone.
        _brushDragging = false;
        _composition.EndStroke(nullptr, 0, 0);
        ShowError(IDS_ERROR_BRUSH_BUDGET);
        DocumentChanged();
        return;
    }
    PushStrokeTiles();
    SyncBrushCursor();
}

void Application::FinishBrushStroke() noexcept
{
    if (!_brushDragging)
    {
        return;
    }
    _brushDragging = false;
    // Spot Healing rewrites the tiles after the last preview update, so its result uploads on its own instead of
    // adopting the working texture, which still shows the wash.
    const BrushStroke* pending = _session.PendingBrush();
    const bool healing = pending != nullptr && pending->Settings().healing;
    _session.FinishBrush();
    const std::optional<BrushCommitInfo> commit = _session.TakeLastBrushCommit();
    if (commit.has_value() && !healing)
    {
        _composition.EndStroke(&commit->image, commit->x, commit->y);
    }
    else
    {
        _composition.EndStroke(nullptr, 0, 0);
    }
    DocumentChanged();
}

void Application::CancelBrushStroke() noexcept
{
    if (!_brushDragging && _session.PendingBrush() == nullptr)
    {
        return;
    }
    _brushDragging = false;
    _session.CancelBrush();
    _composition.EndStroke(nullptr, 0, 0);
    DocumentChanged();
}

void Application::SyncBrushCursor() noexcept
{
    if (!_chrome)
    {
        return;
    }
    std::optional<CanvasOverlay::BrushCursor> cursor;
    if (EditorToolIsBrush(_tool) && _brushHover.has_value() && _session.Document().has_value())
    {
        const Vec2 view = _viewport.ViewFromDocument(_brushHover.value(), DocumentSize());
        CanvasOverlay::BrushCursor circle;
        circle.center = D2D1::Point2F(static_cast<float>(view.x), static_cast<float>(view.y));
        circle.radius = static_cast<float>(_session.brushSettings.diameter * _viewport.PointsPerPixel() / 2.0);
        circle.hardness = static_cast<float>(_session.brushSettings.hardness);
        cursor = circle;
    }
    _chrome->SetBrushCursor(cursor);
    _frameInvalidated = true;
}

namespace
{
// Benchmark lines go to stdout when the parent redirected it (and always to the debugger).
void BenchmarkReport(const std::wstring& text) noexcept
{
    OutputDebugStringW(text.c_str());
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr,
                            nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}
} // namespace

int Application::RunBenchmark() noexcept
{
    if (_bench != L"brush")
    {
        return 2;
    }
    _quietUi = true;
    _screenshotDpi = 0;
    _renderer.SetLayoutDpi(0);
    if (FAILED(RegisterWindowClass()) || FAILED(CreateMainWindow(false)))
    {
        return 2;
    }
    if (FAILED(InitializeRenderer()) || FAILED(InitializeChrome()) || FAILED(InitializeDocument()))
    {
        return 5;
    }
    BindCompositionDevice();
    SyncChrome();
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const auto now = [&]
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    };
    std::wstring report = std::format(L"BRUSH BENCH device={} engine={}\n", _renderer.IsWarp() ? L"WARP" : L"hardware",
                                      _brushEngine.Ready() ? L"compute" : L"cpu");
    // The legacy scenario: 4000 x 4000, 40 and 800 px soft white strokes of 120 updates 40 px apart, blank and
    // opaque layers, two passes each; times include the frame that shows each update.
    for (const double diameter : {40.0, 800.0})
    {
        for (const bool opaque : {false, true})
        {
            _session.CreateDocument(4000, 4000, false);
            if (opaque)
            {
                InsertPaintedLayer(_session, L"Opaque 4K", 4000, 4000, Vec2{0, 0}, 0.0, 0xFF000000u);
            }
            else
            {
                _session.AddBlankLayer();
            }
            _session.history.MarkSaved();
            _viewport.followsFit = true;
            _session.brushSettings = BrushSettings{diameter, 0, 1, 1, 1, 1, false};
            OnTool(EditorTool::Brush);
            DocumentChanged();
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
            // `update` is the pointer update alone (curve, kernel dispatch and readback, tile composition, tile
            // upload); `frame` adds the composition, blit and Present that show it. Presenting paces the GPU queue
            // at the display's refresh and the preview readback waits behind it, so two more passes run the updates
            // without a frame in between (`alone`): the brush's own cost.
            std::vector<double> updates;
            std::vector<double> frames;
            std::vector<double> alone;
            for (int pass = 0; pass < 4; ++pass)
            {
                const bool presented = pass < 2;
                const double start = now();
                BeginBrushStroke(_viewport.ViewFromDocument(Vec2{700, 3200}, DocumentSize()), 0);
                for (int i = 1; i <= 120; ++i)
                {
                    const double t = now();
                    const Vec2 point = i <= 60 ? Vec2{700.0, 3200.0 - i * 40.0} : Vec2{700.0 + (i - 60) * 40.0, 800.0};
                    ContinueBrushStroke(_viewport.ViewFromDocument(point, DocumentSize()));
                    if (!presented)
                    {
                        alone.push_back(now() - t);
                        continue;
                    }
                    updates.push_back(now() - t);
                    (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
                    frames.push_back(now() - t);
                }
                const double end = now();
                FinishBrushStroke();
                const double committedAt = now();
                (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
                const bool committed = _session.history.UndoName() == L"Brush Stroke";
                report += std::format(L"BRUSH BENCH diameter={} opaque={} pass={} presented={} draw={:.1f}ms "
                                      L"commit={:.1f}ms mouseUp={:.1f}ms committed={}\n",
                                      static_cast<int>(diameter), opaque ? L"true" : L"false", pass,
                                      presented ? L"true" : L"false", end - start, committedAt - end, now() - end,
                                      committed ? L"true" : L"false");
            }
            const auto stats = [](std::vector<double>& samples)
            {
                std::sort(samples.begin(), samples.end());
                return std::format(L"median={:.2f} p95={:.2f} max={:.2f}ms", samples[samples.size() / 2],
                                   samples[static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95)],
                                   samples.back());
            };
            report += std::format(L"BRUSH BENCH diameter={} opaque={} update {} frame {} alone {}\n",
                                  static_cast<int>(diameter), opaque ? L"true" : L"false", stats(updates),
                                  stats(frames), stats(alone));
        }
    }
    BenchmarkReport(report);
    CloseMainWindow();
    return 0;
}

// ── Retouch tools (C11) ───────────────────────────────────────────────────

namespace
{
// Shift constrains a gradient line to 45° steps around the other end, as in Photoshop.
[[nodiscard]] Vec2 Snapped(Vec2 point, Vec2 anchor) noexcept
{
    const double dx = point.x - anchor.x;
    const double dy = point.y - anchor.y;
    const double length = std::hypot(dx, dy);
    const double step = 3.14159265358979323846 / 4.0;
    const double angle = std::round(std::atan2(dy, dx) / step) * step;
    return Vec2{anchor.x + std::cos(angle) * length, anchor.y + std::sin(angle) * length};
}

// The stroke preview a raster edit shows through: its grid, placement and the pixels it started from.
[[nodiscard]] CanvasRenderer::StrokePreview PreviewFor(const BrushStroke& stroke)
{
    CanvasRenderer::StrokePreview preview;
    preview.layerID = stroke.LayerID();
    preview.mask = stroke.IsMask();
    preview.width = stroke.Width();
    preview.height = stroke.Height();
    preview.transform = stroke.PaintTransform();
    preview.fill = stroke.FillValue();
    const Raster& source = stroke.SourceImage();
    if (source.HasStorage() && source.Width() == stroke.SourceRect().width &&
        source.Height() == stroke.SourceRect().height)
    {
        preview.source = source;
        preview.sourceX = stroke.SourceRect().x;
        preview.sourceY = stroke.SourceRect().y;
    }
    else if (source.HasStorage())
    {
        // A uniform mask stretched over the grid is its one value everywhere.
        std::uint8_t uniform = preview.fill;
        source.ReadRegion(0, 0, 1, 1, &uniform, source.Channels(), preview.fill);
        preview.fill = uniform;
    }
    return preview;
}
} // namespace

BrushKind Application::BrushKindFor() const noexcept
{
    switch (_tool)
    {
    case EditorTool::Eraser:
        return BrushKind::Erase;
    case EditorTool::Heal:
        return BrushKind::Heal;
    case EditorTool::Clone:
        return BrushKind::Clone;
    case EditorTool::Smear:
        return BrushKind::Blur;
    default:
        return _session.brushMode == BrushToolMode::Erase ? BrushKind::Erase : BrushKind::Paint;
    }
}

bool Application::WarpsWithSmear() const noexcept
{
    return _tool == EditorTool::Smear && _session.smearMode != SmearMode::Blur;
}

void Application::BeginWarpStroke(Vec2 view) noexcept
{
    if (!_session.Document().has_value() || _warpDragging || _brushDragging)
    {
        return;
    }
    BindCompositionDevice();
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    if (_session.IsMaskSelected() && _session.CanPaint())
    {
        ShowError(IDS_ERROR_SMEAR_MASK);
        return;
    }
    if (!_session.BeginWarp(pixel))
    {
        return;
    }
    WarpStroke* warp = _session.MutableWarp();
    const ImageLayer* layer = _session.ActiveLayer();
    if (warp == nullptr || layer == nullptr)
    {
        _session.CancelWarp();
        return;
    }
    // The working copy is document-sized: it shows placed over the whole canvas in the layer's stead.
    CanvasRenderer::StrokePreview preview;
    preview.layerID = layer->id;
    preview.mask = false;
    preview.width = warp->Width();
    preview.height = warp->Height();
    preview.transform.origin = Vec2{0, 0};
    preview.transform.size = Vec2{static_cast<double>(warp->Width()), static_cast<double>(warp->Height())};
    preview.source = warp->Image();
    preview.sourceX = 0;
    preview.sourceY = 0;
    preview.fill = 0;
    (void)_composition.BeginStroke(preview);
    _warpDragging = true;
    _brushHover = pixel;
    PushWarpTiles();
    SyncBrushCursor();
}

void Application::PushWarpTiles() noexcept
{
    WarpStroke* warp = _session.MutableWarp();
    if (warp == nullptr || !_composition.HasStroke())
    {
        _frameInvalidated = true;
        return;
    }
    const std::vector<RasterTile> tiles = warp->TakeChangedTiles();
    if (!tiles.empty())
    {
        (void)_composition.UpdateStrokeTiles(tiles);
    }
    _frameInvalidated = true;
}

void Application::ContinueWarpStroke(Vec2 view) noexcept
{
    if (!_warpDragging)
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    _brushHover = pixel;
    _session.ContinueWarp(pixel);
    PushWarpTiles();
    SyncBrushCursor();
}

void Application::FinishWarpStroke() noexcept
{
    if (!_warpDragging)
    {
        return;
    }
    _warpDragging = false;
    _session.FinishWarp();
    // The working copy sat on the document grid, not the layer's: the committed raster uploads on its own.
    static_cast<void>(_session.TakeLastBrushCommit());
    _composition.EndStroke(nullptr, 0, 0);
    DocumentChanged();
}

void Application::CancelWarpStroke() noexcept
{
    if (!_warpDragging && _session.PendingWarp() == nullptr)
    {
        return;
    }
    _warpDragging = false;
    _session.CancelWarp();
    _composition.EndStroke(nullptr, 0, 0);
    DocumentChanged();
}

void Application::OnGradientAction(GradientAction action, double value) noexcept
{
    switch (action)
    {
    case GradientAction::SetShape:
        _session.gradientSettings.shape = value >= 0.5 ? GradientShape::Radial : GradientShape::Linear;
        break;
    case GradientAction::SetStyle:
        _session.gradientSettings.style =
            value >= 0.5 ? GradientStyle::ForegroundToTransparent : GradientStyle::ForegroundToBackground;
        break;
    case GradientAction::SetReversed:
        _session.gradientSettings.reversed = value >= 0.5;
        break;
    case GradientAction::SetOpacity:
        if (std::isfinite(value))
        {
            _session.gradientSettings.opacity = std::clamp(value, 1.0, 100.0) / 100.0;
        }
        break;
    case GradientAction::Cancel:
        CancelGradient();
        return;
    case GradientAction::Apply:
        CommitGradient();
        return;
    case GradientAction::OpenColor:
        if (_chrome)
        {
            _chrome->OpenColorPicker(false);
        }
        break;
    }
    _session.RefreshGradient();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::BeginGradientDrag(Vec2 view) noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    BindCompositionDevice();
    const GradientEdit* pending = _session.PendingGradient();
    if (pending != nullptr && pending->HasLine())
    {
        // A press near an end of the pending line drags that end.
        const double reach = 10.0 * (_window ? GetDpiForWindow(_window.get()) : 96u) / 96.0;
        const Vec2 start = _viewport.ViewFromDocument(pending->start, DocumentSize());
        const Vec2 end = _viewport.ViewFromDocument(pending->end, DocumentSize());
        if (std::hypot(view.x - end.x, view.y - end.y) <= reach)
        {
            _gradientHandle = 1;
            return;
        }
        if (std::hypot(view.x - start.x, view.y - start.y) <= reach)
        {
            _gradientHandle = 0;
            return;
        }
    }
    if (!_session.BeginGradient(_viewport.DocumentFromView(view, DocumentSize())))
    {
        return;
    }
    _gradientHandle = 1;
    SyncGradientPreview();
    SyncToolOverlays();
}

void Application::DragGradient(Vec2 view, UINT modifiers) noexcept
{
    const GradientEdit* pending = _session.PendingGradient();
    if (!_gradientHandle.has_value() || pending == nullptr)
    {
        return;
    }
    Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    if ((modifiers & MK_SHIFT) != 0)
    {
        pixel = Snapped(pixel, _gradientHandle.value() == 0 ? pending->end : pending->start);
    }
    if (_gradientHandle.value() == 0)
    {
        _session.MoveGradient(pixel, std::nullopt);
    }
    else
    {
        _session.MoveGradient(std::nullopt, pixel);
    }
    SyncGradientPreview();
    SyncToolOverlays();
}

void Application::EndGradientDrag() noexcept
{
    _gradientHandle.reset();
    _session.EndGradientDrag();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::CommitGradient() noexcept
{
    if (_session.PendingGradient() == nullptr)
    {
        return;
    }
    _gradientHandle.reset();
    _session.CommitGradient();
    DocumentChanged();
}

void Application::CancelGradient() noexcept
{
    if (_session.PendingGradient() == nullptr)
    {
        return;
    }
    _gradientHandle.reset();
    _session.CancelGradient();
    DocumentChanged();
}

void Application::SyncGradientPreview() noexcept
{
    // The pending raster shows through the stroke preview; once it is gone the preview ends, adopting a commit the
    // session made (Apply, Enter, or a tool, layer or target switch).
    GradientEdit* pending = _session.MutableGradient();
    if (pending != nullptr && pending->raster)
    {
        if (!_composition.HasStroke())
        {
            BindCompositionDevice();
            (void)_composition.BeginStroke(PreviewFor(*pending->raster));
        }
        PushGradientTiles();
        return;
    }
    if (_composition.HasStroke() && !_brushDragging && !_warpDragging && _session.PendingBrush() == nullptr &&
        _session.PendingWarp() == nullptr && _session.PendingPixelMove() == nullptr)
    {
        const std::optional<BrushCommitInfo> commit = _session.TakeLastBrushCommit();
        if (commit.has_value())
        {
            _composition.EndStroke(&commit->image, commit->x, commit->y);
        }
        else
        {
            _composition.EndStroke(nullptr, 0, 0);
        }
        _frameInvalidated = true;
    }
}

void Application::PushGradientTiles() noexcept
{
    GradientEdit* pending = _session.MutableGradient();
    if (pending == nullptr || !pending->raster || !_composition.HasStroke())
    {
        return;
    }
    BrushStroke& raster = *pending->raster;
    std::vector<RasterTile> tiles;
    if (_composition.StrokeTextureMissing())
    {
        tiles = raster.Patches();
        static_cast<void>(raster.TakeChanged());
    }
    else
    {
        for (const std::size_t index : raster.TakeChanged())
        {
            const BrushStroke::Tile& tile = raster.Tiles()[index];
            tiles.push_back(RasterTile{tile.rect.x, tile.rect.y, tile.image});
        }
    }
    if (!tiles.empty())
    {
        (void)_composition.UpdateStrokeTiles(tiles);
        _frameInvalidated = true;
    }
}

void Application::OnShapeAction(ShapeAction action, double value) noexcept
{
    switch (action)
    {
    case ShapeAction::SetKind:
        _shapeDragging = false;
        _session.CancelShape();
        _session.shapeKind = value >= 0.5 ? ShapeKind::Ellipse : ShapeKind::Rectangle;
        break;
    case ShapeAction::SetRadius:
        if (std::isfinite(value))
        {
            _session.shapeCornerRadius = std::clamp(std::round(value), 0.0, 5000.0);
        }
        break;
    case ShapeAction::OpenColor:
        if (_chrome)
        {
            _chrome->OpenColorPicker(false);
        }
        break;
    }
    SyncChrome();
    _frameInvalidated = true;
}

bool Application::SamplesColor() const noexcept
{
    if (_tool == EditorTool::Eyedropper)
    {
        return true;
    }
    // Alt turns the Brush, Spot Healing and Gradient into the eyedropper between strokes.
    const bool alt = GetKeyState(VK_MENU) < 0;
    return alt && (_tool == EditorTool::Brush || _tool == EditorTool::Heal || _tool == EditorTool::Gradient) &&
           !_brushDragging && !_gradientHandle.has_value();
}

void Application::SampleColorAt(Vec2 view) noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    _samplePoint = pixel;
    if (_sampleComposite.has_value())
    {
        const std::optional<uint32_t> color = DocumentSession::SampleCompositeColor(_sampleComposite.value(), pixel);
        if (color.has_value())
        {
            if (_chrome && _chrome->ColorPickerOpen())
            {
                _chrome->SetPickerColor(color.value());
            }
            else if (_session.PendingBrush() == nullptr)
            {
                _session.SetForegroundColor(color.value());
            }
        }
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::EndColorSampling() noexcept
{
    if (!_samplingColor)
    {
        return;
    }
    _samplingColor = false;
    _sampleComposite.reset();
    _samplePoint.reset();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::SyncToolOverlays() noexcept
{
    if (!_chrome)
    {
        return;
    }
    const bool hasDocument = _session.Document().has_value();
    const Vec2 size = DocumentSize();
    const auto viewPoint = [&](Vec2 document)
    {
        const Vec2 view = _viewport.ViewFromDocument(document, size);
        return D2D1::Point2F(static_cast<float>(view.x), static_cast<float>(view.y));
    };
    std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> line;
    if (const GradientEdit* pending = _session.PendingGradient();
        hasDocument && pending != nullptr && pending->HasLine() && _tool == EditorTool::Gradient)
    {
        line = std::make_pair(viewPoint(pending->start), viewPoint(pending->end));
    }
    _chrome->SetGradientLine(line);
    std::optional<CanvasOverlay::ShapeDraftOverlay> draft;
    if (const std::optional<ShapeDraft>& shape = _session.PendingShape();
        hasDocument && shape.has_value() && shape->rect.maxX > shape->rect.minX && shape->rect.maxY > shape->rect.minY)
    {
        const D2D1_POINT_2F a = viewPoint(Vec2{shape->rect.minX, shape->rect.minY});
        const D2D1_POINT_2F b = viewPoint(Vec2{shape->rect.maxX, shape->rect.maxY});
        CanvasOverlay::ShapeDraftOverlay overlay;
        overlay.ellipse = shape->kind == ShapeKind::Ellipse;
        overlay.rect = D2D1::RectF(a.x, a.y, b.x, b.y);
        overlay.cornerRadius = static_cast<float>(shape->cornerRadius * _viewport.PointsPerPixel());
        overlay.argb = _session.ForegroundColor();
        draft = overlay;
    }
    _chrome->SetShapeDraft(draft);
    std::optional<CanvasOverlay::SampleRing> ring;
    if (hasDocument && _samplingColor && _samplePoint.has_value() && _session.showsSampleRing)
    {
        CanvasOverlay::SampleRing sample;
        sample.center = viewPoint(_samplePoint.value());
        sample.originalArgb = _samplingOriginal;
        sample.sampledArgb = _chrome->ColorPickerOpen() ? _chrome->PickerColor() : _session.ForegroundColor();
        ring = sample;
    }
    _chrome->SetSampleRing(ring);
    std::optional<D2D1_POINT_2F> source;
    if (hasDocument && _tool == EditorTool::Clone && _session.cloneSource.has_value())
    {
        const std::optional<Vec2> point = _session.CloneSamplePoint(_brushHover.value_or(_session.cloneSource.value()));
        if (point.has_value())
        {
            source = viewPoint(point.value());
        }
    }
    _chrome->SetCloneSourceMark(source);
}

// ── Pixel edits (C9b) ────────────────────────────────────────────────────

void Application::BeginPixelDrag(Vec2 pixel, bool duplicate) noexcept
{
    _transformDrag.reset();
    static_cast<void>(_session.TakeLastBrushCommit());
    if (!_session.BeginPixelMove(duplicate))
    {
        return;
    }
    _pixelDragStart = pixel;
    SyncPixelMovePreview();
    SelectionChanged();
}

void Application::DragPixels(Vec2 pixel, bool constrain) noexcept
{
    if (!_pixelDragStart.has_value())
    {
        return;
    }
    double dx = pixel.x - _pixelDragStart->x;
    double dy = pixel.y - _pixelDragStart->y;
    if (constrain)
    {
        // Shift keeps the move on one axis: whichever way the drag has gone further.
        if (std::fabs(dx) >= std::fabs(dy))
        {
            dy = 0.0;
        }
        else
        {
            dx = 0.0;
        }
    }
    _session.MovePixels(dx, dy);
    if (_session.PendingPixelMove() == nullptr)
    {
        // The move ran out of pixel budget: the drag ends with nothing to show.
        _pixelDragStart.reset();
    }
    SyncPixelMovePreview();
    SelectionChanged();
}

void Application::FinishPixelDrag() noexcept
{
    _pixelDragStart.reset();
    _session.FinishPixelMove();
    SyncPixelMovePreview();
    DocumentChanged();
}

void Application::CancelPixelDrag() noexcept
{
    _pixelDragStart.reset();
    _session.CancelPixelMove();
    SyncPixelMovePreview();
    DocumentChanged();
}

void Application::SyncPixelMovePreview() noexcept
{
    // The lifted pixels show through the stroke preview while they drag; the commit adopts the texture like a
    // stroke, a cancel drops it.
    PixelMove* pending = _session.MutablePixelMove();
    if (pending != nullptr && pending->raster)
    {
        if (!_composition.HasStroke())
        {
            BindCompositionDevice();
            (void)_composition.BeginStroke(PreviewFor(*pending->raster));
        }
        PushPixelMoveTiles();
        return;
    }
    if (_composition.HasStroke() && !_brushDragging && !_warpDragging && _session.PendingBrush() == nullptr &&
        _session.PendingWarp() == nullptr && _session.PendingGradient() == nullptr)
    {
        const std::optional<BrushCommitInfo> commit = _session.TakeLastBrushCommit();
        if (commit.has_value())
        {
            _composition.EndStroke(&commit->image, commit->x, commit->y);
        }
        else
        {
            _composition.EndStroke(nullptr, 0, 0);
        }
        _frameInvalidated = true;
    }
}

void Application::PushPixelMoveTiles() noexcept
{
    PixelMove* pending = _session.MutablePixelMove();
    if (pending == nullptr || !pending->raster || !_composition.HasStroke())
    {
        return;
    }
    BrushStroke& raster = *pending->raster;
    std::vector<RasterTile> tiles;
    if (_composition.StrokeTextureMissing())
    {
        tiles = raster.Patches();
        static_cast<void>(raster.TakeChanged());
    }
    else
    {
        for (const std::size_t index : raster.TakeChanged())
        {
            const BrushStroke::Tile& tile = raster.Tiles()[index];
            tiles.push_back(RasterTile{tile.rect.x, tile.rect.y, tile.image});
        }
    }
    if (!tiles.empty())
    {
        (void)_composition.UpdateStrokeTiles(tiles);
        _frameInvalidated = true;
    }
}

void Application::MirrorClipboard() noexcept
{
    const std::optional<PixelClipboard>& clip = _session.Clipboard();
    if (!clip.has_value() || !_window)
    {
        return;
    }
    // Other apps read the copy as PNG or a 32-bit DIB. The sequence number lets a later Paste tell that the system
    // clipboard still holds this copy, so the exact pixels and their place come from the session instead.
    if (SystemClipboard::WriteImage(_window.get(), clip->image))
    {
        _clipboardSequence = SystemClipboard::SequenceNumber();
    }
}

void Application::PasteFromClipboard() noexcept
{
    if (!_session.Document().has_value())
    {
        return;
    }
    const bool foreign = !_session.CanPaste() || _clipboardSequence != SystemClipboard::SequenceNumber();
    if (foreign && SystemClipboard::HasImage())
    {
        Raster image;
        if (SystemClipboard::ReadImage(_window.get(), DocumentSession::kMaxShapePixels, image))
        {
            _session.PasteImage(image);
            DocumentChanged();
        }
        return;
    }
    _session.Paste();
    DocumentChanged();
}

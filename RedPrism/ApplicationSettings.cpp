#include "Application.h"

#include "ChromeHost.h"
#include "SettingsStore.h"
#include "ThemePalette.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <UIAutomation.h>

// The settings and accessibility side of Application (Specs/Core/Core_Settings.md, Specs/UI/UI_ApplicationChrome.md):
// the file the app reads and writes, the event-driven reload on the UI thread, the theme and the self-test gates.

namespace
{
constexpr UINT kSettingsReloadDelayMilliseconds = 250;
constexpr std::array<float, 4> kDarkClearColor{0.118f, 0.118f, 0.133f, 1.0f};
constexpr std::array<float, 4> kLightClearColor{0.898f, 0.898f, 0.910f, 1.0f};

[[nodiscard]] std::wstring DirectoryOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

[[nodiscard]] std::array<float, 4> SystemClearColor() noexcept
{
    const COLORREF color = GetSysColor(COLOR_WINDOW);
    return {GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 1.0f};
}

// Self-test diagnostics go to the debugger and, when the parent redirected it, to stderr (as Application.cpp does).
void ReportSelfTestFailure(const std::wstring& message) noexcept
{
    OutputDebugStringW(message.c_str());
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error == nullptr || error == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), utf8.data(), size, nullptr,
                            nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

// Counts the UIA elements under `fragment` (depth first, at most `budget` visits) that carry a Name.
void CountNamedElements(IRawElementProviderFragment* fragment, int& named, int& budget) noexcept
{
    if (fragment == nullptr || budget <= 0)
    {
        return;
    }
    --budget;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    if (SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))) && simple)
    {
        VARIANT name;
        VariantInit(&name);
        if (SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR &&
            name.bstrVal != nullptr && SysStringLen(name.bstrVal) > 0)
        {
            ++named;
        }
        (void)VariantClear(&name);
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, child.put())))
    {
        while (child && budget > 0)
        {
            CountNamedElements(child.get(), named, budget);
            wil::com_ptr_nothrow<IRawElementProviderFragment> next;
            if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
            {
                break;
            }
            child = std::move(next);
        }
    }
}
} // namespace

void Application::UseSettings(std::wstring path, const AppSettings& settings) noexcept
{
    StopSettingsWatch();
    _settingsPath = std::move(path);
    if (!_settingsPath.empty())
    {
        (void)StartSettingsWatch();
    }
    ApplySettings(settings);
}

void Application::ApplySettings(const AppSettings& settings) noexcept
{
    _settings = settings;
    _session.showsSampleRing = settings.sampleRing;
    // One effective binding table (RedPrism/CommandBindings.h): the key path dispatches from it and the chrome
    // (menus, Command List, tooltips) shows its chords.
    _bindings.Build(settings.shortcuts);
    if (_chrome)
    {
        _chrome->SetCommandChecked(ChromeHost::kCommandSampleRing, settings.sampleRing);
        _chrome->SetLayersWidth(static_cast<float>(settings.layersPanelWidth));
        _chrome->SetCommandBindings(&_bindings);
    }
    RefreshTheme();
    _layoutDirty = true;
    _frameInvalidated = true;
}

void Application::RefreshTheme() noexcept
{
    const bool highContrast = RedPrism::SystemHighContrast();
    bool dark = true;
    switch (_settings.theme)
    {
    case ThemePreference::Light:
        dark = false;
        break;
    case ThemePreference::Dark:
        dark = true;
        break;
    case ThemePreference::System:
        dark = !RedPrism::SystemAppsUseLightTheme();
        break;
    }
    if (_chrome)
    {
        _chrome->SetTheme(highContrast ? RedPrism::MakeHighContrastThemePalette()
                                       : RedPrism::MakeRedPrismThemePalette(dark));
    }
    _renderer.SetClearColor(highContrast ? SystemClearColor() : dark ? kDarkClearColor : kLightClearColor);
    _frameInvalidated = true;
}

bool Application::StartSettingsWatch() noexcept
{
    const std::wstring directory = DirectoryOf(_settingsPath);
    if (directory.empty())
    {
        return false;
    }
    // One directory watch, signalled by the OS; nothing polls. A change arms a short timer so an editor's several
    // writes coalesce into one reload on the UI thread.
    _settingsWatch.reset(FindFirstChangeNotificationW(directory.c_str(), FALSE,
                                                      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                                                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION));
    NoteSettingsFileState();
    return static_cast<bool>(_settingsWatch);
}

void Application::StopSettingsWatch() noexcept
{
    if (_window)
    {
        (void)KillTimer(_window.get(), kSettingsTimerId);
    }
    _settingsWatch.reset();
}

HANDLE Application::SettingsWatchHandle() const noexcept
{
    return _settingsWatch ? _settingsWatch.get() : nullptr;
}

void Application::NoteSettingsFileState() noexcept
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!_settingsPath.empty() && GetFileAttributesExW(_settingsPath.c_str(), GetFileExInfoStandard, &data))
    {
        _settingsSeenWrite = data.ftLastWriteTime;
        _settingsSeenSize = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    else
    {
        _settingsSeenWrite = FILETIME{};
        _settingsSeenSize = 0;
    }
}

void Application::OnSettingsDirectoryChanged(bool immediate) noexcept
{
    // A watch on a directory that was deleted or renamed away stays signaled (or cannot be re-armed), and the
    // message wait would wake at once, forever. Watch the directory afresh; when it is gone, stop watching.
    if (_settingsWatch && (!FindNextChangeNotification(_settingsWatch.get()) ||
                           GetFileAttributesW(DirectoryOf(_settingsPath).c_str()) == INVALID_FILE_ATTRIBUTES))
    {
        const FILETIME seenWrite = _settingsSeenWrite;
        const std::uint64_t seenSize = _settingsSeenSize;
        _settingsWatch.reset();
        if (!StartSettingsWatch())
        {
            OutputDebugStringW(L"The settings directory is gone; live reload stops until the next start.\n");
        }
        // The change that signaled is still to be read.
        _settingsSeenWrite = seenWrite;
        _settingsSeenSize = seenSize;
    }
    if (immediate || !_window)
    {
        (void)ReloadSettingsIfChanged();
        return;
    }
    (void)SetTimer(_window.get(), kSettingsTimerId, kSettingsReloadDelayMilliseconds, nullptr);
}

bool Application::ReloadSettingsIfChanged() noexcept
{
    if (_settingsPath.empty())
    {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(_settingsPath.c_str(), GetFileExInfoStandard, &data))
    {
        return false; // gone: the last-good settings stay
    }
    const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    if (CompareFileTime(&data.ftLastWriteTime, &_settingsSeenWrite) == 0 && size == _settingsSeenSize)
    {
        return false;
    }
    _settingsSeenWrite = data.ftLastWriteTime;
    _settingsSeenSize = size;
    const std::optional<AppSettings> live = SettingsStore::LoadLive(_settingsPath);
    if (!live.has_value())
    {
        // Live invalid: keep the file as the user left it and the last-good settings in memory.
        OutputDebugStringW(L"The settings file is not a settings document; keeping the previous settings.\n");
        return true;
    }
    if (live.value() == _settings)
    {
        return true;
    }
    // The language and the window placement apply at the next start; everything else applies now.
    ApplySettings(live.value());
    return true;
}

void Application::SaveSettings() noexcept
{
    if (_settingsPath.empty())
    {
        return;
    }
    if (_window)
    {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(_window.get(), &placement))
        {
            const RECT& normal = placement.rcNormalPosition;
            _settings.window = WindowPlacementSetting{true,
                                                      static_cast<int>(normal.left),
                                                      static_cast<int>(normal.top),
                                                      static_cast<int>(normal.right - normal.left),
                                                      static_cast<int>(normal.bottom - normal.top),
                                                      placement.showCmd == SW_SHOWMAXIMIZED};
        }
    }
    if (_chrome && _chrome->LayersWidthDip() >= static_cast<float>(AppSettings::kLayersPanelMin))
    {
        _settings.layersPanelWidth = _chrome->LayersWidthDip();
    }
    _settings.sampleRing = _session.showsSampleRing;
    if (SettingsStore::Save(_settingsPath, _settings))
    {
        NoteSettingsFileState(); // our own write is not a change to reload
    }
}

int Application::RunProductHardeningSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        ReportSelfTestFailure(L"The product-hardening self-test needs the chrome and the window.\n");
        return 5;
    }
    // Chrome geometry: the File menu's flyout anchors under its item on screen, and a floating card is chrome, not
    // canvas, while the hole beside it is canvas.
    RECT client{};
    POINT origin{};
    if (!GetClientRect(_window.get(), &client) || !ClientToScreen(_window.get(), &origin))
    {
        ReportSelfTestFailure(L"The window's client rectangle is unavailable.\n");
        return 5;
    }
    const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
    const POINT anchor = _chrome->MenuAnchorScreen(0, POINT{0, 0});
    // The item rectangle DxUi reports sits inside the bar (a couple of DIP inset), so the anchor lands in the bar's
    // lower part: below its middle, not below its bottom edge.
    const LONG menuBottom = origin.y + static_cast<LONG>(std::lround(DxUi::MenuBar::kDefaultHeightDip * scale));
    const LONG menuMiddle = origin.y + static_cast<LONG>(std::lround(DxUi::MenuBar::kDefaultHeightDip * scale / 2.0f));
    if (anchor.x < origin.x || anchor.x >= origin.x + client.right || anchor.y <= menuMiddle ||
        anchor.y > menuBottom + 1)
    {
        ReportSelfTestFailure(std::format(
            L"The File menu anchor ({}, {}) is not under its item (client origin {}, {}; menu bottom {}).\n", anchor.x,
            anchor.y, origin.x, origin.y, menuBottom));
        return 5;
    }
    const CanvasHole hole = _chrome->HolePixels();
    const float holeX = hole.left + hole.width / 2.0f;
    const float holeY = hole.top + hole.height / 2.0f;
    if (!_chrome->IsCanvasPoint(holeX, holeY) || _chrome->IsCanvasPoint(hole.left / 2.0f, holeY))
    {
        ReportSelfTestFailure(L"The canvas hit test does not tell the hole from the rail.\n");
        return 5;
    }
    // A rail click through the pointer path activates the tool: the Toolbar button's click reaches OnTool.
    const auto clickRail = [&](EditorTool tool) noexcept
    {
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(tool);
        const int x = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return _tool == tool && _chrome->Tool() == tool;
    };
    OnTool(EditorTool::Move);
    if (!clickRail(EditorTool::Marquee))
    {
        _chrome->DetachAccessibility();
        const bool withoutAccessibility = clickRail(EditorTool::Marquee);
        ReportSelfTestFailure(std::format(L"Clicking the Marquee rail button did not activate the tool (tool {}; "
                                          L"without accessibility attached: {}).\n",
                                          static_cast<int>(_tool), withoutAccessibility ? L"works" : L"fails"));
        return 5;
    }
    // The padding to the left of the glyph is part of the button: a press there activates the same tool.
    OnTool(EditorTool::Move);
    {
        const D2D1_RECT_F marquee = _chrome->ToolButtonBoundsDip(EditorTool::Marquee);
        const int x = static_cast<int>(std::lround(scale));
        const int y = static_cast<int>(std::lround((marquee.top + marquee.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        if (_tool != EditorTool::Marquee || _chrome->Tool() != EditorTool::Marquee)
        {
            ReportSelfTestFailure(L"The left edge of the rail did not activate the Marquee.\n");
            return 5;
        }
    }
    // The same click with the keyboard focus in the window (as a user's click has), so the accessibility placement
    // reports the chrome focused and the canvas-focus flip takes the real path.
    const HWND previousFocus = GetFocus();
    (void)SetFocus(_window.get());
    const bool windowFocused = GetFocus() == _window.get();
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    const bool focusedClick = clickRail(EditorTool::Lasso);
    (void)SetFocus(previousFocus);
    if (!windowFocused)
    {
        ReportSelfTestFailure(L"note: the hidden self-test window could not take the keyboard focus; the focused rail "
                              L"click was not exercised.\n");
    }
    if (windowFocused && !focusedClick)
    {
        ReportSelfTestFailure(std::format(L"Clicking the Lasso rail button with the window focused did not activate "
                                          L"the tool (tool {}).\n",
                                          static_cast<int>(_tool)));
        return 5;
    }
    // A user holds a rail button for a moment, so frames render between the press and the release: the real loop's
    // animation step, the chrome sync and a frame. Between two clicks the loop also runs (settle), so a previous
    // tool switch has laid out already; what a previous click leaves behind is DxUi's focus on that button.
    const auto settle = [&]() noexcept
    {
        for (int frame = 0; frame < 3; ++frame)
        {
            Sleep(16);
            (void)_chrome->AdvanceAnimation(GetTickCount64());
            SyncChrome();
            (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
        }
    };
    const auto heldClick = [&](EditorTool tool) noexcept
    {
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(tool);
        const int x = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        settle();
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return _tool == tool;
    };
    OnTool(EditorTool::Lasso);
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    settle();
    if (!heldClick(EditorTool::Brush))
    {
        ReportSelfTestFailure(std::format(L"Clicking the Brush rail button after a tool switch, with frames between "
                                          L"press and release, did not activate the tool (tool {}).\n",
                                          static_cast<int>(_tool)));
        return 5;
    }
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    settle();
    const bool lassoClicked = clickRail(EditorTool::Lasso);
    settle();
    if (!lassoClicked || !heldClick(EditorTool::Wand))
    {
        ReportSelfTestFailure(std::format(L"Clicking a rail button after another rail click, with frames between "
                                          L"press and release, did not activate the tool (tool {}).\n",
                                          static_cast<int>(_tool)));
        return 5;
    }
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    _chrome->FocusLayerOpacityForTest();
    settle();
    if (!heldClick(EditorTool::Brush))
    {
        ReportSelfTestFailure(L"Clicking the Brush rail button after the opacity stepper had the focus did not "
                              L"activate the tool.\n");
        return 5;
    }
    // A user hovers before clicking, so the button's tooltip is showing at the moment of the click.
    OnTool(EditorTool::Move);
    {
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(EditorTool::Wand);
        const int x = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        const bool tooltip = _chrome->ForceTooltipForTest();
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x + 1, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x + 1, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x + 1, y));
        if (_tool != EditorTool::Wand)
        {
            ReportSelfTestFailure(std::format(L"Clicking the Wand rail button under its tooltip did not activate the "
                                              L"tool (tool {}; tooltip shown: {}).\n",
                                              static_cast<int>(_tool), tooltip ? L"yes" : L"no"));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    _chrome->OpenColorPicker(PickerTarget::Foreground, _session.ForegroundColor());
    (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    const D2D1_RECT_F card = _chrome->ColorPickerBoundsDip();
    const float cardX = (card.left + card.right) / 2.0f * scale;
    const float cardY = (card.top + card.bottom) / 2.0f * scale;
    const bool cardIsChrome =
        _chrome->ColorPickerOpen() && card.right > card.left && !_chrome->IsCanvasPoint(cardX, cardY);
    _chrome->CloseColorPicker(false);
    if (!cardIsChrome)
    {
        ReportSelfTestFailure(L"The open color picker card counts as canvas.\n");
        return 5;
    }

    // Accessibility: every interactive control carries a name and the window's UIA root exposes the chrome.
    const std::vector<std::wstring> unnamed = _chrome->UnnamedInteractiveControls();
    if (!unnamed.empty())
    {
        std::wstring text = L"Chrome controls without an accessible name:";
        for (const std::wstring& name : unnamed)
        {
            text += L" [" + name + L"]";
        }
        text += L"\n";
        ReportSelfTestFailure(text);
        return 5;
    }
    IRawElementProviderFragmentRoot* root = _chrome->AccessibilityRoot();
    wil::com_ptr_nothrow<IRawElementProviderFragment> rootFragment;
    wil::com_ptr_nothrow<IRawElementProviderFragment> chrome;
    if (root == nullptr || FAILED(root->QueryInterface(IID_PPV_ARGS(rootFragment.put()))) || !rootFragment ||
        FAILED(rootFragment->Navigate(NavigateDirection_FirstChild, chrome.put())) || !chrome)
    {
        ReportSelfTestFailure(std::format(
            L"The window's UIA root does not expose the DxUi chrome as its child (attach status 0x{:08X}).\n",
            static_cast<unsigned long>(_chrome->AccessibilityStatus())));
        return 5;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> parent;
    if (FAILED(chrome->Navigate(NavigateDirection_Parent, parent.put())) || !parent)
    {
        ReportSelfTestFailure(L"The chrome's UIA fragment does not name the window root as its parent.\n");
        return 5;
    }
    int named = 0;
    int budget = 400;
    CountNamedElements(chrome.get(), named, budget);
    if (named < 8)
    {
        ReportSelfTestFailure(L"The chrome's UIA tree carries too few named elements.\n");
        return 5;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> hit;
    if (FAILED(root->ElementProviderFromPoint(origin.x + 8.0, origin.y + 8.0, hit.put())) || !hit)
    {
        ReportSelfTestFailure(L"ElementProviderFromPoint found no chrome element under the menu bar.\n");
        return 5;
    }

    // Settings: a file of the self-test's own drives the live reload; the user's directory is never touched.
    std::error_code error;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path(error) /
                                          (L"RedPrism.SelfTest.settings-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(scratch, error);
    const std::wstring path = (scratch / L"settings.json").wstring();
    AppSettings initial;
    initial.theme = ThemePreference::Dark;
    if (!SettingsStore::Save(path, initial))
    {
        ReportSelfTestFailure(L"The self-test could not write its settings file.\n");
        return 5;
    }
    UseSettings(path, initial);
    if (!_settingsWatch || !_chrome->ThemeIsDark() || !_session.showsSampleRing)
    {
        ReportSelfTestFailure(L"The settings watch did not start from the self-test file.\n");
        return 5;
    }
    const auto waitForChange = [this]() noexcept
    { return _settingsWatch && WaitForSingleObject(_settingsWatch.get(), 5000) == WAIT_OBJECT_0; };
    AppSettings edited = initial;
    edited.theme = ThemePreference::Light;
    edited.sampleRing = false;
    edited.layersPanelWidth = 300;
    if (!SettingsStore::Save(path, edited) || !waitForChange())
    {
        ReportSelfTestFailure(L"Editing the settings file did not signal the directory watch.\n");
        return 5;
    }
    OnSettingsDirectoryChanged(true);
    if (_chrome->ThemeIsDark() || _session.showsSampleRing || _settings.layersPanelWidth != 300 ||
        _renderer.ClearColor() != kLightClearColor)
    {
        ReportSelfTestFailure(L"A valid edit of the settings file did not apply live.\n");
        return 5;
    }
    (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    if (std::abs(_chrome->LayersWidthDip() - 300.0f) > 0.5f)
    {
        ReportSelfTestFailure(L"The layers panel width did not follow the settings file.\n");
        return 5;
    }
    if (!SettingsStore::WriteFileAtomic(path, "{ broken") || !waitForChange())
    {
        ReportSelfTestFailure(L"Breaking the settings file did not signal the directory watch.\n");
        return 5;
    }
    OnSettingsDirectoryChanged(true);
    std::string bytes;
    if (_chrome->ThemeIsDark() || _session.showsSampleRing || !SettingsStore::ReadFileBytes(path, bytes) ||
        bytes != "{ broken")
    {
        ReportSelfTestFailure(L"An invalid settings file changed the live settings or was rewritten.\n");
        return 5;
    }
    if (!SettingsStore::Save(path, initial) || !waitForChange())
    {
        ReportSelfTestFailure(L"Restoring the settings file did not signal the directory watch.\n");
        return 5;
    }
    OnSettingsDirectoryChanged(true);
    if (!_chrome->ThemeIsDark() || !_session.showsSampleRing || _renderer.ClearColor() != kDarkClearColor)
    {
        ReportSelfTestFailure(L"Restoring the settings file did not apply live.\n");
        return 5;
    }
    // High contrast: the system palette applies through the same path and RefreshTheme follows the OS state.
    _chrome->SetTheme(RedPrism::MakeHighContrastThemePalette());
    const bool highContrastApplied = _chrome->ThemeIsHighContrast();
    RefreshTheme();
    if (!highContrastApplied || _chrome->ThemeIsHighContrast() != RedPrism::SystemHighContrast())
    {
        ReportSelfTestFailure(L"The high-contrast palette did not apply, or RefreshTheme ignored the system state.\n");
        return 5;
    }
    SaveSettings();
    const std::optional<AppSettings> saved = SettingsStore::LoadLive(path);
    if (!saved.has_value() || !saved->window.valid || saved->theme != ThemePreference::Dark ||
        saved->window.width < AppSettings::kWindowMinWidth)
    {
        ReportSelfTestFailure(L"Saving the settings did not record the window placement.\n");
        return 5;
    }
    // Deleting the watched directory must not leave a watch that stays signaled: the message wait would wake at
    // once, forever. The watch goes (there is no directory left to watch) and the last-good settings stay.
    std::filesystem::remove_all(scratch, error);
    const bool deletionSignaled = waitForChange();
    OnSettingsDirectoryChanged(true);
    if (!deletionSignaled || _settingsWatch || !_chrome->ThemeIsDark())
    {
        ReportSelfTestFailure(std::format(L"Deleting the settings directory left the watch armed (signaled {}, "
                                          L"watch {}).\n",
                                          deletionSignaled, static_cast<bool>(_settingsWatch)));
        return 5;
    }
    StopSettingsWatch();
    _settingsPath.clear();
    std::filesystem::remove_all(scratch, error);
    return 0;
}

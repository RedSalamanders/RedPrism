#include "Application.h"

#include "ChromeHost.h"
#include "GuardedFileCommit.h"
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
constexpr DWORD kSettingsReloadDelayMilliseconds = 250;
constexpr DWORD kSettingsWatchFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION;
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
        if (SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR && name.bstrVal != nullptr && SysStringLen(name.bstrVal) > 0)
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

void Application::UseSettings(std::wstring path, const AppSettings& settings, std::optional<FileOutputTarget> acceptedTarget) noexcept
{
    StopSettingsWatch();
    _settingsPath        = std::move(path);
    _settingsFileTarget  = std::move(acceptedTarget);
    _settingsSeenVersion = _settingsFileTarget ? _settingsFileTarget->acceptedVersion : std::nullopt;
    ApplySettings(settings);
    if (_window && ! _settingsPath.empty() && ! StartSettingsWatch())
    {
        OutputDebugStringW(L"Settings live reload could not start.\n");
    }
}

void Application::ApplySettings(const AppSettings& settings) noexcept
{
    _settings = settings;
    // One effective binding table (RedPrism/CommandBindings.h): the key path dispatches from it and the chrome
    // (menus, Command List, tooltips) shows its chords.
    _bindings.Build(settings.shortcuts, &NormalizeLayoutChord);
    if (_chrome)
    {
        _chrome->SetCommandChecked(ChromeHost::kCommandSampleRing, settings.sampleRing);
        _chrome->SetLayersWidth(static_cast<float>(settings.layersPanelWidth));
        _chrome->SetCommandBindings(&_bindings);
    }
    RefreshTheme();
    _layoutDirty = true;
    InvalidateFrame();
}

void Application::RefreshTheme() noexcept
{
    const bool highContrast = RedPrism::SystemHighContrast();
    bool dark               = true;
    switch (_settings.theme)
    {
        case ThemePreference::Light: dark = false; break;
        case ThemePreference::Dark: dark = true; break;
        case ThemePreference::System: dark = ! RedPrism::SystemAppsUseLightTheme(); break;
    }
    if (_chrome)
    {
        _chrome->SetTheme(highContrast ? RedPrism::MakeHighContrastThemePalette() : RedPrism::MakeRedPrismThemePalette(dark));
    }
    _renderer.SetClearColor(highContrast ? SystemClearColor() : dark ? kDarkClearColor : kLightClearColor);
    InvalidateFrame();
}

bool Application::StartSettingsWatch() noexcept
{
    try
    {
        auto directory = DirectoryOf(_settingsPath);
        if (! _window || directory.empty())
            return false;

        wil::unique_hfind_change change{FindFirstChangeNotificationW(directory.c_str(), FALSE, kSettingsWatchFilter)};
        if (! change)
            return false;
        _settingsWatchStop.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (! _settingsWatchStop)
            return false;

        const HWND owner        = _window.get();
        const WPARAM generation = _settingsWatchGeneration;
        const HANDLE stop       = _settingsWatchStop.get();
        _settingsWatchThread    = std::jthread([this, owner, generation, stop, directory = std::move(directory), change = std::move(change)]() mutable noexcept
        {
            const auto postChange = [this, owner, generation]() noexcept
            {
                if (! _settingsWatchPending.exchange(true, std::memory_order_acq_rel) && ! PostMessageW(owner, kSettingsChangedMessage, generation, 0))
                {
                    _settingsWatchPending.store(false, std::memory_order_release);
                }
            };

            // Catch a change between loading the initial file and arming the directory watch.
            postChange();
            HANDLE handles[]{stop, change.get()};
            DWORD timeout = INFINITE;
            for (;;)
            {
                const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(std::size(handles)), handles, FALSE, timeout);
                if (result == WAIT_OBJECT_0)
                    return;
                if (result == WAIT_TIMEOUT)
                {
                    postChange();
                    timeout = INFINITE;
                    continue;
                }
                if (result != WAIT_OBJECT_0 + 1)
                    break;
                if (! FindNextChangeNotification(change.get()) || GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    change.reset(FindFirstChangeNotificationW(directory.c_str(), FALSE, kSettingsWatchFilter));
                    if (! change)
                        break;
                    handles[1] = change.get();
                }
                timeout = kSettingsReloadDelayMilliseconds;
            }
            (void)PostMessageW(owner, kSettingsChangedMessage, generation, 1);
        });
        return true;
    }
    catch (...)
    {
        StopSettingsWatch();
        return false;
    }
}

void Application::StopSettingsWatch() noexcept
{
    ++_settingsWatchGeneration;
    if (_settingsWatchStop)
        (void)SetEvent(_settingsWatchStop.get());
    if (_settingsWatchThread.joinable())
        _settingsWatchThread.join();
    _settingsWatchStop.reset();
    _settingsWatchPending.store(false, std::memory_order_release);
}

void Application::ReloadSettingsIfChanged() noexcept
{
    if (_settingsPath.empty())
        return;
    FileOutputTarget observed;
    if (FAILED(ObserveFileOutputTarget(_settingsPath.c_str(), observed)) || ! observed.acceptedVersion || observed.acceptedVersion == _settingsSeenVersion)
        return;
    _settingsSeenVersion = observed.acceptedVersion;

    std::optional<FileOutputTarget> acceptedTarget;
    const auto live = SettingsStore::LoadLive(_settingsPath, &acceptedTarget);
    if (! live)
    {
        OutputDebugStringW(L"The settings file is not a settings document; keeping the previous settings.\n");
        return;
    }
    _settingsFileTarget = std::move(acceptedTarget);
    if (_settingsFileTarget && _settingsFileTarget->acceptedVersion)
        _settingsSeenVersion = _settingsFileTarget->acceptedVersion;
    if (*live != _settings)
        ApplySettings(*live);
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
            _settings.window   = WindowPlacementSetting{true,
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
    if (! _settingsFileTarget)
    {
        OutputDebugStringW(L"Settings save deferred because the current file version could not be accepted.\n");
        return;
    }
    std::optional<FileOutputTarget> published;
    const HRESULT saved = SettingsStore::SaveAccepted(*_settingsFileTarget, _settings, &published);
    if (SUCCEEDED(saved))
    {
        _settingsFileTarget  = std::move(published);
        _settingsSeenVersion = _settingsFileTarget ? _settingsFileTarget->acceptedVersion : std::nullopt;
    }
    else if (saved == kOutputVersionConflict)
    {
        OutputDebugStringW(L"Settings changed on disk. The newer file was preserved.\n");
    }
}

int Application::RunProductHardeningSelfTest() noexcept
{
    if (! _chrome || ! _window)
    {
        SelfTestFailure(L"The product-hardening self-test needs the chrome and the window.\n");
        return 5;
    }
    // Chrome geometry: the File menu's flyout anchors under its item on screen, and a floating card is chrome, not
    // canvas, while the hole beside it is canvas.
    RECT client{};
    POINT origin{};
    if (! GetClientRect(_window.get(), &client) || ! ClientToScreen(_window.get(), &origin))
    {
        SelfTestFailure(L"The window's client rectangle is unavailable.\n");
        return 5;
    }
    const float scale  = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
    const POINT anchor = _chrome->MenuAnchorScreen(0, POINT{0, 0});
    // The item rectangle DxUi reports sits inside the bar (a couple of DIP inset), so the anchor lands in the bar's
    // lower part: below its middle, not below its bottom edge.
    const LONG menuBottom = origin.y + static_cast<LONG>(std::lround(DxUi::MenuBar::kDefaultHeightDip * scale));
    const LONG menuMiddle = origin.y + static_cast<LONG>(std::lround(DxUi::MenuBar::kDefaultHeightDip * scale / 2.0f));
    if (anchor.x < origin.x || anchor.x >= origin.x + client.right || anchor.y <= menuMiddle || anchor.y > menuBottom + 1)
    {
        SelfTestFailure(std::format(L"The File menu anchor ({}, {}) is not under its item (client origin {}, {}; menu bottom {}).\n",
                                    anchor.x,
                                    anchor.y,
                                    origin.x,
                                    origin.y,
                                    menuBottom));
        return 5;
    }
    const CanvasHole hole = _chrome->HolePixels();
    const float holeX     = hole.left + hole.width / 2.0f;
    const float holeY     = hole.top + hole.height / 2.0f;
    if (! _chrome->IsCanvasPoint(holeX, holeY) || _chrome->IsCanvasPoint(hole.left / 2.0f, holeY))
    {
        SelfTestFailure(L"The canvas hit test does not tell the hole from the rail.\n");
        return 5;
    }
    // A rail click through the pointer path activates the tool: the Toolbar button's click reaches OnTool.
    const auto clickRail = [&](EditorTool tool) noexcept
    {
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(tool);
        const int x              = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y              = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return _tool == tool && _chrome->Tool() == tool;
    };
    OnTool(EditorTool::Move);
    if (! clickRail(EditorTool::Marquee))
    {
        _chrome->DetachAccessibility();
        const bool withoutAccessibility = clickRail(EditorTool::Marquee);
        SelfTestFailure(std::format(L"Clicking the Marquee rail button did not activate the tool (tool {}; "
                                    L"without accessibility attached: {}).\n",
                                    static_cast<int>(_tool),
                                    withoutAccessibility ? L"works" : L"fails"));
        return 5;
    }
    // The padding to the left of the glyph is part of the button: a press there activates the same tool.
    OnTool(EditorTool::Move);
    {
        const D2D1_RECT_F marquee = _chrome->ToolButtonBoundsDip(EditorTool::Marquee);
        const int x               = static_cast<int>(std::lround(scale));
        const int y               = static_cast<int>(std::lround((marquee.top + marquee.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        if (_tool != EditorTool::Marquee || _chrome->Tool() != EditorTool::Marquee)
        {
            SelfTestFailure(L"The left edge of the rail did not activate the Marquee.\n");
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
    if (! windowFocused)
    {
        SelfTestFailure(L"note: the hidden self-test window could not take the keyboard focus; the focused rail "
                        L"click was not exercised.\n");
    }
    if (windowFocused && ! focusedClick)
    {
        SelfTestFailure(std::format(L"Clicking the Lasso rail button with the window focused did not activate "
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
        const int x              = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y              = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
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
    if (! heldClick(EditorTool::Brush))
    {
        SelfTestFailure(std::format(L"Clicking the Brush rail button after a tool switch, with frames between "
                                    L"press and release, did not activate the tool (tool {}).\n",
                                    static_cast<int>(_tool)));
        return 5;
    }
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    settle();
    const bool lassoClicked = clickRail(EditorTool::Lasso);
    settle();
    if (! lassoClicked || ! heldClick(EditorTool::Wand))
    {
        SelfTestFailure(std::format(L"Clicking a rail button after another rail click, with frames between "
                                    L"press and release, did not activate the tool (tool {}).\n",
                                    static_cast<int>(_tool)));
        return 5;
    }
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    _chrome->FocusLayerOpacityForTest();
    const bool opacityFieldFocused = _chrome->TextInputFocused();
    settle();
    if (! opacityFieldFocused || ! heldClick(EditorTool::Brush))
    {
        SelfTestFailure(std::format(L"Clicking the Brush rail button after the opacity field had the focus did not "
                                    L"activate the tool (the field had the focus: {}).\n",
                                    opacityFieldFocused));
        return 5;
    }
    // A user hovers before clicking, so the button's tooltip is showing at the moment of the click.
    OnTool(EditorTool::Move);
    {
        const D2D1_RECT_F button = _chrome->ToolButtonBoundsDip(EditorTool::Wand);
        const int x              = static_cast<int>(std::lround((button.left + button.right) / 2.0f * scale));
        const int y              = static_cast<int>(std::lround((button.top + button.bottom) / 2.0f * scale));
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        const bool tooltip = _chrome->ForceTooltipForTest();
        OnPointer(WM_MOUSEMOVE, 0, MAKELPARAM(x + 1, y));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x + 1, y));
        OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x + 1, y));
        if (_tool != EditorTool::Wand)
        {
            SelfTestFailure(std::format(L"Clicking the Wand rail button under its tooltip did not activate the "
                                        L"tool (tool {}; tooltip shown: {}).\n",
                                        static_cast<int>(_tool),
                                        tooltip ? L"yes" : L"no"));
            return 5;
        }
    }
    OnTool(EditorTool::Move);
    _chrome->OpenColorPicker(PickerTarget::Foreground, _session.ForegroundColor());
    (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    const D2D1_RECT_F card  = _chrome->ColorPickerBoundsDip();
    const float cardX       = (card.left + card.right) / 2.0f * scale;
    const float cardY       = (card.top + card.bottom) / 2.0f * scale;
    const bool cardIsChrome = _chrome->ColorPickerOpen() && card.right > card.left && ! _chrome->IsCanvasPoint(cardX, cardY);
    _chrome->CloseColorPicker(false);
    if (! cardIsChrome)
    {
        SelfTestFailure(L"The open color picker card counts as canvas.\n");
        return 5;
    }

    // Accessibility: every interactive control carries a name and the window's UIA root exposes the chrome.
    const std::vector<std::wstring> unnamed = _chrome->UnnamedInteractiveControls();
    if (! unnamed.empty())
    {
        std::wstring text = L"Chrome controls without an accessible name:";
        for (const std::wstring& name : unnamed)
        {
            text += L" [" + name + L"]";
        }
        text += L"\n";
        SelfTestFailure(text);
        return 5;
    }
    IRawElementProviderFragmentRoot* root = _chrome->AccessibilityRoot();
    wil::com_ptr_nothrow<IRawElementProviderFragment> rootFragment;
    wil::com_ptr_nothrow<IRawElementProviderFragment> chrome;
    if (root == nullptr || FAILED(root->QueryInterface(IID_PPV_ARGS(rootFragment.put()))) || ! rootFragment ||
        FAILED(rootFragment->Navigate(NavigateDirection_FirstChild, chrome.put())) || ! chrome)
    {
        SelfTestFailure(std::format(L"The window's UIA root does not expose the DxUi chrome as its child (attach status 0x{:08X}).\n",
                                    static_cast<unsigned long>(_chrome->AccessibilityStatus())));
        return 5;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> parent;
    if (FAILED(chrome->Navigate(NavigateDirection_Parent, parent.put())) || ! parent)
    {
        SelfTestFailure(L"The chrome's UIA fragment does not name the window root as its parent.\n");
        return 5;
    }
    int named  = 0;
    int budget = 400;
    CountNamedElements(chrome.get(), named, budget);
    if (named < 8)
    {
        SelfTestFailure(L"The chrome's UIA tree carries too few named elements.\n");
        return 5;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> hit;
    if (FAILED(root->ElementProviderFromPoint(origin.x + 8.0, origin.y + 8.0, hit.put())) || ! hit)
    {
        SelfTestFailure(L"ElementProviderFromPoint found no chrome element under the menu bar.\n");
        return 5;
    }

    // Settings: a file of the self-test's own drives the live reload; the user's directory is never touched.
    std::error_code error;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path(error) / (L"RedPrism.SelfTest.settings-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(scratch, error);
    const std::wstring path = (scratch / L"settings.json").wstring();
    AppSettings initial;
    initial.theme = ThemePreference::Dark;
    if (! SettingsStore::Save(path, initial))
    {
        SelfTestFailure(L"The self-test could not write its settings file.\n");
        return 5;
    }
    SettingsLoadResult loaded = SettingsStore::LoadCold(path);
    UseSettings(path, initial, std::move(loaded.acceptedTarget));
    const auto waitForChange = [this](bool watchStops = false) noexcept
    {
        const auto started = GetTickCount64();
        for (;;)
        {
            MSG message{};
            while (PeekMessageW(&message, _window.get(), kSettingsChangedMessage, kSettingsChangedMessage, PM_REMOVE))
            {
                const bool expected = message.wParam == _settingsWatchGeneration && (message.lParam != 0) == watchStops;
                DispatchMessageW(&message);
                if (expected)
                    return true;
            }
            const auto elapsed = GetTickCount64() - started;
            if (elapsed >= 5000 || MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(5000 - elapsed), QS_POSTMESSAGE, 0) != WAIT_OBJECT_0)
                return false;
        }
    };
    if (! waitForChange() || ! _chrome->ThemeIsDark() || ! _settings.sampleRing)
    {
        SelfTestFailure(L"The settings worker did not post its initial notification.\n");
        return 5;
    }
    AppSettings edited      = initial;
    edited.theme            = ThemePreference::Light;
    edited.sampleRing       = false;
    edited.layersPanelWidth = 300;
    if (! SettingsStore::Save(path, edited) || ! waitForChange())
    {
        SelfTestFailure(L"Editing the settings file did not signal the directory watch.\n");
        return 5;
    }
    if (_chrome->ThemeIsDark() || _settings.sampleRing || _settings.layersPanelWidth != 300 || _renderer.ClearColor() != kLightClearColor)
    {
        SelfTestFailure(L"A valid edit of the settings file did not apply live.\n");
        return 5;
    }
    (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    if (std::abs(_chrome->LayersWidthDip() - 300.0f) > 0.5f)
    {
        SelfTestFailure(L"The layers panel width did not follow the settings file.\n");
        return 5;
    }
    if (! SettingsStore::WriteFileAtomic(path, "{ broken") || ! waitForChange())
    {
        SelfTestFailure(L"Breaking the settings file did not signal the directory watch.\n");
        return 5;
    }
    std::string bytes;
    if (_chrome->ThemeIsDark() || _settings.sampleRing || ! SettingsStore::ReadFileBytes(path, bytes) || bytes != "{ broken")
    {
        SelfTestFailure(L"An invalid settings file changed the live settings or was rewritten.\n");
        return 5;
    }
    if (! SettingsStore::Save(path, initial) || ! waitForChange())
    {
        SelfTestFailure(L"Restoring the settings file did not signal the directory watch.\n");
        return 5;
    }
    if (! _chrome->ThemeIsDark() || ! _settings.sampleRing || _renderer.ClearColor() != kDarkClearColor)
    {
        SelfTestFailure(L"Restoring the settings file did not apply live.\n");
        return 5;
    }
    // High contrast: the system palette applies through the same path and RefreshTheme follows the OS state.
    _chrome->SetTheme(RedPrism::MakeHighContrastThemePalette());
    const bool highContrastApplied = _chrome->ThemeIsHighContrast();
    RefreshTheme();
    if (! highContrastApplied || _chrome->ThemeIsHighContrast() != RedPrism::SystemHighContrast())
    {
        SelfTestFailure(L"The high-contrast palette did not apply, or RefreshTheme ignored the system state.\n");
        return 5;
    }
    SaveSettings();
    const std::optional<AppSettings> saved = SettingsStore::LoadLive(path);
    if (! saved.has_value() || ! saved->window.valid || saved->theme != ThemePreference::Dark || saved->window.width < AppSettings::kWindowMinWidth)
    {
        SelfTestFailure(L"Saving the settings did not record the window placement.\n");
        return 5;
    }
    // A removed directory retires the worker and keeps the last accepted settings.
    std::filesystem::remove_all(scratch, error);
    if (! waitForChange(true) || _settingsWatchThread.joinable() || ! _chrome->ThemeIsDark())
    {
        SelfTestFailure(L"Deleting the settings directory did not retire its worker.\n");
        return 5;
    }
    StopSettingsWatch();
    _settingsPath.clear();
    _settingsFileTarget.reset();
    std::filesystem::remove_all(scratch, error);
    return 0;
}

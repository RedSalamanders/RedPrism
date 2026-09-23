#include "Application.h"

#include "ChromeHost.h"
#include "Resources.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// The keyboard side of Application (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators", Specs/Core/
// Core_Settings.md "Command chords"). A key down goes, in order, to: the Escape that closes a floating card; the
// gestures of an edit in progress on the canvas (Enter and Escape of a pending transform, gradient, crop, panel or
// card, the lasso's corner keys); the effective binding table (`_bindings`: the settings' replacements, then the
// catalog defaults), which runs a command through OnCommand; the canvas's own keys (opacity digits, Space to pan);
// and last the chrome. A key a command or a gesture took types no character. The menu bar takes a tapped Alt, F10
// and Alt+letter; Alt+F4 and Alt+Space stay the window manager's.

void SelfTestFailure(const wchar_t* message) noexcept; // Application.cpp

namespace
{
constexpr LPARAM kKeyRepeatFlag = static_cast<LPARAM>(1) << 30; // the key was already down
constexpr LPARAM kKeyUpFlags = (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
constexpr UINT_PTR kMenuLoopGuardTimerId = 9;

// Whether the active layout types a character for the key with the keyboard state as it is: with Ctrl and Alt held,
// AltGr text (€, @, Polish ż) rather than a chord. Flag bit 2 leaves the keyboard state, dead keys included, as it
// was (Windows 10 1607 and later).
[[nodiscard]] bool KeyTypesCharacter(UINT virtualKey, UINT scanCode) noexcept
{
    std::array<BYTE, 256> state{};
    if (!GetKeyboardState(state.data()))
    {
        return false;
    }
    std::array<wchar_t, 8> buffer{};
    const int count = ToUnicodeEx(virtualKey, scanCode, state.data(), buffer.data(), static_cast<int>(buffer.size()),
                                  1u << 2, GetKeyboardLayout(0));
    if (count < 0)
    {
        return true; // a dead key: it types the accent of the next key
    }
    for (int index = 0; index < count; ++index)
    {
        if (buffer[static_cast<std::size_t>(index)] >= L' ' && buffer[static_cast<std::size_t>(index)] != 0x7F)
        {
            return true;
        }
    }
    return false;
}

// One key down from its message: the virtual key, the position its set-1 scan code names, the modifiers held (the
// thread's key state as of this message; AltGr reports Ctrl and Alt) and the auto-repeat bit.
[[nodiscard]] KeyPress KeyPressFromMessage(WPARAM wParam, LPARAM lParam) noexcept
{
    KeyPress press;
    press.virtualKey = static_cast<std::uint16_t>(wParam & 0xFFFFu);
    const UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFFu;
    const bool extended = (static_cast<UINT>(lParam) & (1u << 24)) != 0;
    press.position = KeyPositionFromScanCode(scanCode, extended);
    press.ctrl = GetKeyState(VK_CONTROL) < 0;
    press.alt = GetKeyState(VK_MENU) < 0;
    press.shift = GetKeyState(VK_SHIFT) < 0;
    press.repeat = (lParam & kKeyRepeatFlag) != 0;
    if (press.ctrl && press.alt)
    {
        press.typesCharacter = KeyTypesCharacter(press.virtualKey, scanCode);
    }
    return press;
}

[[nodiscard]] UINT HeldModifiers() noexcept
{
    return (GetKeyState(VK_SHIFT) < 0 ? MK_SHIFT : 0u) | (GetKeyState(VK_CONTROL) < 0 ? MK_CONTROL : 0u);
}

// The modifiers a chrome control reads for a key. AltGr text carries no Ctrl, so a text field types AltGr+Z (Polish
// ż) instead of taking it for Ctrl+Z.
[[nodiscard]] UINT ChromeKeyModifiers(const KeyPress& press) noexcept
{
    UINT modifiers = press.shift ? MK_SHIFT : 0u;
    if (press.ctrl && !(press.alt && press.typesCharacter))
    {
        modifiers |= MK_CONTROL;
    }
    return modifiers;
}

// Keys the window manager owns: Alt itself (DefWindowProc tells a tap from a chord and sends SC_KEYMENU), F10, and
// Alt+F4 and Alt+Space, which no setting can take (Decision D5).
[[nodiscard]] bool IsWindowManagerKey(const KeyPress& press) noexcept
{
    return press.virtualKey == VK_MENU || press.virtualKey == VK_LMENU || press.virtualKey == VK_RMENU ||
           (press.virtualKey == VK_F10 && !press.ctrl && !press.alt) ||
           (press.alt && !press.ctrl && (press.virtualKey == VK_F4 || press.virtualKey == VK_SPACE));
}

bool gMenuLoopGuardFired = false;

// Self-test guard: a modal system-menu loop would wait forever for input a hidden window never gets.
void CALLBACK EndMenuLoopForTest(HWND, UINT, UINT_PTR, DWORD) noexcept
{
    gMenuLoopGuardFired = true;
    (void)EndMenu();
}
} // namespace

LRESULT Application::OnKeyDown(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const KeyPress press = KeyPressFromMessage(wParam, lParam);
    _suppressCharacter = false;
    _lastKeyHandled = false;
    UpdateSelectionModifiers();
    if (press.virtualKey == VK_MENU)
    {
        (void)ApplyCanvasCursor();
        SyncBrushCursor();
    }
    if (IsWindowManagerKey(press))
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (_chrome && _chrome->MenuKeyboardActive())
    {
        if (!press.ctrl && !press.alt)
        {
            // The menu bar has the keyboard: arrows move, Enter, Space or Down open, Escape leaves, and a letter
            // (its WM_CHAR) opens that menu.
            if (_chrome->DispatchKey(press.virtualKey, true, ChromeKeyModifiers(press)))
            {
                _frameInvalidated = true;
            }
            return 0;
        }
        _chrome->LeaveMenuKeyboard(); // a chord leaves the menu bar and runs as usual
    }
    if (HandleKey(press))
    {
        _lastKeyHandled = true;
        _suppressCharacter = true;
        _frameInvalidated = true;
        return 0;
    }
    const bool chromeHandled = _chrome && _chrome->DispatchKey(press.virtualKey, true, ChromeKeyModifiers(press));
    if (chromeHandled)
    {
        _frameInvalidated = true;
    }
    if (message != WM_SYSKEYDOWN)
    {
        return 0;
    }
    // An Alt+key a control took does not also open a menu through its WM_SYSCHAR.
    _suppressCharacter = chromeHandled;
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::OnKeyUp(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const UINT virtualKey = static_cast<UINT>(wParam);
    // The key down's character, if it had one, arrived before this key up.
    _suppressCharacter = false;
    UpdateSelectionModifiers();
    if (virtualKey == VK_MENU)
    {
        (void)ApplyCanvasCursor();
        SyncBrushCursor();
    }
    if (virtualKey == VK_SPACE && _spacePan)
    {
        // The canvas took this Space down; a pan that already ended (the focus left) lets its key up reach the
        // chrome, which may have taken the key down.
        _spacePan = false;
        return 0;
    }
    if (virtualKey == VK_MENU || virtualKey == VK_F10)
    {
        // A tapped Alt or F10: DefWindowProc sends SC_KEYMENU, which OnKeyMenu gives to the menu bar.
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (_chrome && _chrome->DispatchKey(virtualKey, false, HeldModifiers()))
    {
        _frameInvalidated = true;
    }
    return 0;
}

LRESULT Application::OnCharacter(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const wchar_t character = static_cast<wchar_t>(wParam);
    if (_suppressCharacter)
    {
        // The key down ran a command or a gesture: its character types nothing (and a handled Alt chord does not
        // beep through the menu loop).
        _suppressCharacter = false;
        return 0;
    }
    if (message == WM_SYSCHAR)
    {
        if (character == L' ')
        {
            return DefWindowProcW(window, message, wParam, lParam); // Alt+Space: the system menu
        }
        // Alt+letter opens the menu with that mnemonic; an Alt+key nothing took does nothing, without a beep.
        if (_chrome && _chrome->OpenMenuByMnemonic(character))
        {
            _frameInvalidated = true;
        }
        return 0;
    }
    if (_chrome && _chrome->DispatchCharacter(character, HeldModifiers()))
    {
        _frameInvalidated = true;
    }
    return 0;
}

void Application::OnKeyMenu(wchar_t character) noexcept
{
    if (!_chrome)
    {
        return;
    }
    if (character == L'\0')
    {
        _chrome->ToggleMenuKeyboard();
    }
    else
    {
        (void)_chrome->OpenMenuByMnemonic(character);
    }
    _frameInvalidated = true;
}

KeyState Application::CurrentKeyState() const noexcept
{
    KeyState state;
    state.focus = _chrome == nullptr || _chrome->CanvasFocused() ? KeyFocus::Canvas
                  : _chrome->TextInputFocused()                  ? KeyFocus::TextInput
                                                                 : KeyFocus::Chrome;
    state.tool = _tool;
    state.selection = _session.Selection().has_value() && !_session.Selection()->IsEmpty();
    state.lassoDraft = _session.PendingLasso().has_value();
    return state;
}

bool Application::HandleKey(const KeyPress& press) noexcept
{
    if (!_chrome)
    {
        return false;
    }
    // A nudge reads Shift as its step (ten pixels) while it runs.
    _acceleratorShift = press.shift;
    const auto clearAcceleratorShift = wil::scope_exit([this]() noexcept { _acceleratorShift = false; });
    // Escape closes the floating card on top, wherever the focus is: More tools, the notice, the Command List.
    if (press.virtualKey == VK_ESCAPE && !press.ctrl)
    {
        if (_chrome->MoreToolsOpen())
        {
            _chrome->CloseMoreTools();
            return true;
        }
        if (_chrome->NoticeOpen())
        {
            _chrome->CloseNotice();
            return true;
        }
        if (_chrome->ShortcutsOpen())
        {
            _chrome->CloseShortcuts();
            return true;
        }
    }
    const KeyState state = CurrentKeyState();
    if (state.focus == KeyFocus::Canvas && !press.ctrl && HandleEditGesture(press))
    {
        return true;
    }
    const KeyMatch match = _bindings.Match(press, state);
    if (match.result == KeyMatchResult::Command)
    {
        OnCommand(match.command);
        return true;
    }
    if (match.result == KeyMatchResult::HeldKey)
    {
        return true; // holding the key runs its command once (the Marquee's M does not keep switching)
    }
    return state.focus == KeyFocus::Canvas && HandleCanvasKey(press);
}

bool Application::HandleEditGesture(const KeyPress& press) noexcept
{
    const UINT key = press.virtualKey;
    const bool enter = key == VK_RETURN;
    const bool escape = key == VK_ESCAPE;
    if (SizeCardOpen() && (enter || escape))
    {
        // Enter is the size card's OK, Escape its Cancel.
        if (escape)
        {
            CancelSizeCard();
        }
        else
        {
            ApplySizeCard();
        }
        return true;
    }
    if (_session.HasColorEdit() && (enter || escape))
    {
        // Enter is the panel's OK, Escape its Cancel.
        if (escape)
        {
            CancelColorEdit();
        }
        else
        {
            ApplyColorEdit();
        }
        return true;
    }
    if (escape && _pixelDragStart.has_value())
    {
        CancelPixelDrag();
        return true;
    }
    if (escape && (_warpDragging || _session.PendingWarp() != nullptr))
    {
        CancelWarpStroke();
        return true;
    }
    if (_tool == EditorTool::Crop && (enter || escape))
    {
        // Enter applies the crop frame, Escape drops it (the whole canvas frames again).
        if (escape)
        {
            CancelCropFrame();
        }
        else
        {
            ApplyCrop();
        }
        return true;
    }
    if (escape && _session.PendingShape().has_value())
    {
        _shapeDragging = false;
        _session.CancelShape();
        SyncToolOverlays();
        _frameInvalidated = true;
        return true;
    }
    if (_session.PendingGradient() != nullptr && (enter || escape))
    {
        // Escape drops the pending gradient, Enter applies it.
        if (escape)
        {
            CancelGradient();
        }
        else
        {
            CommitGradient();
        }
        return true;
    }
    if (escape && (_brushDragging || _session.PendingBrush() != nullptr))
    {
        CancelBrushStroke();
        return true;
    }
    if (_session.PendingLasso().has_value() && (escape || enter || key == VK_BACK || key == VK_DELETE))
    {
        // Escape drops the outline, Enter closes it, Backspace or Delete removes the last polygonal corner.
        if (escape)
        {
            _session.CancelLasso();
        }
        else if (enter)
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
    if (escape && (_transformDrag.has_value() || _session.PendingTransformEdit().has_value()))
    {
        _transformDrag.reset();
        _duplicateOnDrag = false;
        _session.CancelTransform();
        DocumentChanged();
        return true;
    }
    if (enter && _session.PendingTransformEdit().has_value())
    {
        _transformDrag.reset();
        _session.CommitTransform();
        DocumentChanged();
        return true;
    }
    return false;
}

bool Application::HandleCanvasKey(const KeyPress& press) noexcept
{
    const UINT key = press.virtualKey;
    if (key == VK_SPACE && !press.ctrl && !press.alt)
    {
        // Space pans while held, over any tool (with the canvas focused only: a focused chrome control keeps it).
        _spacePan = true;
        return true;
    }
    const bool plain = !press.ctrl && !press.alt && !press.shift;
    if (plain && key >= '0' && key <= '9' &&
        (EditorToolIsBrush(_tool) || _tool == EditorTool::Move || _tool == EditorTool::Gradient))
    {
        // Photoshop's opacity keys (legacy typeOpacityDigit): 1–9 are 10–90 %, 0 is 100 %, two digits within 0.6 s
        // an exact percent. The brush's opacity, the gradient's, or with the Move tool the active layer's: one
        // Layer Opacity entry per key that changes it.
        const int digit = static_cast<int>(key - '0');
        const double now = static_cast<double>(GetTickCount64()) / 1000.0;
        if (EditorToolIsBrush(_tool))
        {
            _session.TypeOpacityDigit(digit, now);
        }
        else if (const int percent = _session.TypedOpacityPercent(digit, now); percent > 0)
        {
            if (_tool == EditorTool::Gradient)
            {
                _session.gradientSettings.opacity = percent / 100.0;
                _session.RefreshGradient();
            }
            else
            {
                _session.SetOpacity(percent / 100.0);
            }
        }
        DocumentChanged();
        return true;
    }
    return false;
}

bool Application::HandleAccelerator(UINT virtualKey, UINT modifiers, bool repeat) noexcept
{
    const HWND window = _window.get();
    if (window == nullptr)
    {
        return false;
    }
    const bool ctrl = (modifiers & MK_CONTROL) != 0;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool alt = (modifiers & kKeyAlt) != 0;
    // The self-tests name the bracket and number-row keys by their US virtual keys. The message carries that
    // physical key's scan code and the virtual key the active layout gives it; any other key its layout scan code.
    UINT scanCode = 0;
    switch (virtualKey)
    {
    case VK_OEM_MINUS:
        scanCode = KeyPositionScanCode(KeyPosition::NumberRowMinus);
        break;
    case VK_OEM_PLUS:
        scanCode = KeyPositionScanCode(KeyPosition::NumberRowPlus);
        break;
    case VK_OEM_4:
        scanCode = KeyPositionScanCode(KeyPosition::BracketLeft);
        break;
    case VK_OEM_6:
        scanCode = KeyPositionScanCode(KeyPosition::BracketRight);
        break;
    default:
        break;
    }
    UINT key = virtualKey;
    bool extended = false;
    if (scanCode != 0)
    {
        if (const UINT layoutKey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK); layoutKey != 0)
        {
            key = layoutKey;
        }
    }
    else
    {
        const UINT mapped = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
        scanCode = mapped & 0xFFu;
        extended = (mapped & 0xFF00u) != 0 || IsExtendedNavigationKey(static_cast<std::uint16_t>(virtualKey));
    }
    std::array<BYTE, 256> saved{};
    const bool restore = GetKeyboardState(saved.data()) != FALSE;
    std::array<BYTE, 256> state = saved;
    const auto hold = [&state](UINT which, bool down) noexcept
    { state[which & 0xFFu] = down ? static_cast<BYTE>(0x80) : static_cast<BYTE>(0x00); };
    hold(VK_CONTROL, ctrl);
    hold(VK_LCONTROL, ctrl);
    hold(VK_RCONTROL, false);
    hold(VK_MENU, alt);
    hold(VK_LMENU, alt);
    hold(VK_RMENU, false);
    hold(VK_SHIFT, shift);
    hold(VK_LSHIFT, shift);
    hold(VK_RSHIFT, false);
    hold(key, true);
    (void)SetKeyboardState(state.data());
    // As Windows sends them: a system key with Alt (not with Ctrl too) or F10.
    const bool system = (alt && !ctrl) || key == VK_F10;
    const LPARAM down = 1 | (static_cast<LPARAM>(scanCode) << 16) | (extended ? static_cast<LPARAM>(1) << 24 : 0) |
                        (alt ? static_cast<LPARAM>(1) << 29 : 0) | (repeat ? kKeyRepeatFlag : 0);
    _lastKeyHandled = false;
    (void)SendMessageW(window, system ? WM_SYSKEYDOWN : WM_KEYDOWN, key, down);
    const bool handled = _lastKeyHandled;
    // The characters TranslateMessage would post for this key.
    std::array<wchar_t, 4> characters{};
    const int count = ToUnicodeEx(key, scanCode, state.data(), characters.data(), static_cast<int>(characters.size()),
                                  1u << 2, GetKeyboardLayout(0));
    for (int index = 0; index < count; ++index)
    {
        (void)SendMessageW(window, system ? WM_SYSCHAR : WM_CHAR, characters[static_cast<std::size_t>(index)], down);
    }
    hold(key, false);
    (void)SetKeyboardState(state.data());
    (void)SendMessageW(window, system ? WM_SYSKEYUP : WM_KEYUP, key, down | kKeyUpFlags);
    if (restore)
    {
        (void)SetKeyboardState(saved.data());
    }
    UpdateSelectionModifiers();
    return handled;
}

int Application::RunKeyboardSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The keyboard self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto fail = [](const std::wstring& message) noexcept
    {
        SelfTestFailure(message.c_str());
        return 5;
    };
    // A user sees frames between two keys: the chrome lays out and renders (DxUi takes no key while a layout change
    // waits for its preparation), then the key goes through the window procedure.
    const auto settle = [this]()
    {
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    };
    const auto key = [this, &settle](UINT virtualKey, UINT modifiers, bool repeat = false)
    {
        settle();
        return HandleAccelerator(virtualKey, modifiers, repeat);
    };
    const AppSettings original = _settings;
    const auto replace = [this, &original](std::initializer_list<ShortcutBinding> bindings)
    {
        AppSettings changed = original;
        changed.shortcuts = bindings;
        ApplySettings(changed);
    };
    const auto binding = [](const wchar_t* commandId, KeyChord chord) { return ShortcutBinding{commandId, chord}; };
    const auto undoTo = [this](int count)
    {
        while (_session.history.CanUndo() && _session.history.UndoCount() > count)
        {
            _session.Undo();
        }
        DocumentChanged();
    };
    const auto typeText = [&key](std::wstring_view text)
    {
        for (const wchar_t character : text)
        {
            const SHORT scan = VkKeyScanW(character);
            if (scan == -1)
            {
                continue;
            }
            const UINT shift = (scan & 0x100) != 0 ? MK_SHIFT : 0u;
            (void)key(static_cast<UINT>(scan & 0xFF), shift);
        }
    };
    const auto keyName = [](UINT scanCode, bool extended)
    {
        wchar_t name[64]{};
        const LONG param = static_cast<LONG>((scanCode << 16) | (extended ? 1u << 24 : 0u));
        const int length = GetKeyNameTextW(param, name, static_cast<int>(std::size(name)));
        return length > 0 ? std::wstring(name, static_cast<std::size_t>(length)) : std::wstring{};
    };
    const auto join = [](const std::wstring& left, const std::wstring& right)
    { return FormatStringResource(nullptr, IDS_KEY_JOIN, left, right); };
    const std::wstring ctrlName = LoadStringResource(nullptr, IDS_KEY_CTRL);
    const std::wstring shiftName = LoadStringResource(nullptr, IDS_KEY_SHIFT);
    _chrome->SetMenusRecordOnlyForTest(true);
    const auto restoreSettings = wil::scope_exit(
        [this, &original]() noexcept
        {
            ApplySettings(original);
            _chrome->SetMenusRecordOnlyForTest(false);
        });
    const int historyStart = _session.history.UndoCount();
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);

    // A replacement moves the command to its chord (X no longer swaps, Q does); the palette keys ignore Shift, on
    // a replacement too.
    const std::wstring swapId = L"cmd/edit/swapPalette";
    replace({binding(swapId.c_str(), Key('Q'))});
    _chrome->SetCanvasFocused(true);
    const std::uint32_t foreground = _session.ForegroundColor();
    if (key('X', 0) || _session.ForegroundColor() != foreground)
    {
        return fail(L"X still swapped the palette after its chord was replaced.\n");
    }
    if (!key('Q', 0) || _session.ForegroundColor() == foreground || !key('Q', MK_SHIFT) ||
        _session.ForegroundColor() != foreground)
    {
        return fail(L"The replacement chord Q (and Shift+Q) did not swap the palette.\n");
    }
    // Two entries for one command: the later one wins, and the chrome shows it.
    replace({binding(swapId.c_str(), Key('Q')), binding(swapId.c_str(), Key('K'))});
    _chrome->SetCanvasFocused(true);
    if (key('Q', 0) || _session.ForegroundColor() != foreground || !key('K', 0) ||
        _session.ForegroundColor() == foreground || !key('K', 0) ||
        _chrome->CommandChordText(ChromeHost::kCommandSwapPalette) != L"K" ||
        _chrome->PaletteSwapTooltipForTest() != FormatStringResource(nullptr, IDS_TOOL_TOOLTIP,
                                                                     LoadStringResource(nullptr, IDS_PALETTE_SWAP),
                                                                     std::wstring(L"K")))
    {
        return fail(std::format(L"The later of two entries for Swap did not win (chord '{}', tooltip '{}').\n",
                                _chrome->CommandChordText(ChromeHost::kCommandSwapPalette),
                                _chrome->PaletteSwapTooltipForTest()));
    }

    // A replacement takes every default chord of its command, aliases and Shift variants included.
    const double zoom = _viewport.zoom;
    const auto restoreZoom = [this, zoom]()
    {
        _viewport.SetZoom(zoom, _viewport.Center(), DocumentSize());
        SyncChrome();
    };
    replace({});
    _chrome->SetCanvasFocused(true);
    if (!key(VK_OEM_PLUS, MK_CONTROL | MK_SHIFT) || !(_viewport.zoom > zoom))
    {
        return fail(L"Ctrl+Shift with the number-row plus key did not zoom in.\n");
    }
    restoreZoom();
    replace({binding(L"cmd/view/zoomIn", Ctrl(Key('K')))});
    _chrome->SetCanvasFocused(true);
    if (key(VK_OEM_PLUS, MK_CONTROL) || key(VK_OEM_PLUS, MK_CONTROL | MK_SHIFT) || key(VK_ADD, MK_CONTROL) ||
        _viewport.zoom != zoom || !key('K', MK_CONTROL) || !(_viewport.zoom > zoom))
    {
        return fail(L"Replacing Zoom In left a default chord or an alias live, or Ctrl+K did not zoom.\n");
    }
    restoreZoom();

    // Replacing the Delete key's command: Delete does nothing, the new key clears the selected pixels, and Delete
    // Layer's shared Delete label goes with it.
    OnCommand(ChromeHost::kCommandNewLayer);
    _session.ApplySelection(SelectionGeometry::Rect(12, 12, 20, 20), SelectionMode::New, L"Select");
    OnCommand(ChromeHost::kCommandFillForeground);
    replace({binding(L"cmd/edit/delete", Key('Q'))});
    _chrome->SetCanvasFocused(true);
    const int beforeDelete = _session.history.UndoCount();
    if (key(VK_DELETE, 0) || _session.history.UndoCount() != beforeDelete ||
        _chrome->MenuItemCaption(ChromeHost::kCommandDeleteLayer).find(L'\t') != std::wstring::npos || !key('Q', 0) ||
        _session.history.UndoName() != L"Clear")
    {
        return fail(std::format(L"The Delete key's replacement did not take over (undo '{}', Delete Layer '{}').\n",
                                _session.history.UndoName(),
                                _chrome->MenuItemCaption(ChromeHost::kCommandDeleteLayer)));
    }
    replace({});
    const std::wstring deleteName = keyName(0x53, true);
    if (!deleteName.empty() && !_chrome->MenuItemCaption(ChromeHost::kCommandDeleteLayer).ends_with(L"\t" + deleteName))
    {
        return fail(std::format(L"Delete Layer did not show the layout's Delete key again ('{}', key '{}').\n",
                                _chrome->MenuItemCaption(ChromeHost::kCommandDeleteLayer), deleteName));
    }
    undoTo(historyStart);

    // A replacement keeps its command's repeat rule: holding the key switches the Marquee's shape once.
    replace({binding(L"cmd/tool/toggleMarquee", Key('Q'))});
    OnTool(EditorTool::Marquee);
    _chrome->SetCanvasFocused(true);
    const LassoKind marquee = _session.marqueeKind;
    const bool firstTaken = key('Q', 0);
    const bool firstSwitched = _session.marqueeKind != marquee;
    const bool heldTaken = key('Q', 0, true);
    const bool heldKept = _session.marqueeKind != marquee;
    const bool secondTaken = key('Q', 0);
    const bool secondBack = _session.marqueeKind == marquee;
    const bool toolTaken = key('M', 0);
    const bool toolKept = _session.marqueeKind == marquee;
    if (!firstTaken || !firstSwitched || !heldTaken || !heldKept || !secondTaken || !secondBack || !toolTaken ||
        !toolKept)
    {
        return fail(std::format(L"The replaced Marquee switch did not run once per press, or M still switched it "
                                L"(Q {}/{}, held {}/{}, Q {}/{}, M {}/{}, tool {}, canvas {}).\n",
                                firstTaken, firstSwitched, heldTaken, heldKept, secondTaken, secondBack, toolTaken,
                                toolKept, static_cast<int>(_tool), _chrome->CanvasFocused()));
    }
    OnTool(EditorTool::Move);

    // Decision D4: while a chrome text input has the focus, the field keeps the keys; no document command runs.
    replace({binding(swapId.c_str(), Key('Q'))});
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    const int beforeTyping = _session.history.UndoCount();
    if (!_chrome->ShortcutsOpen() || !_chrome->TextInputFocused() || key('Q', 0) ||
        _session.ForegroundColor() != foreground || _chrome->ShortcutSearchTextForTest() != L"q" ||
        key('A', MK_CONTROL) || _session.Selection().has_value() || key(VK_BACK, MK_CONTROL) || key('Z', MK_CONTROL) ||
        key(VK_DELETE, 0) || _session.history.UndoCount() != beforeTyping)
    {
        return fail(std::format(L"Keys in the Command List's search field ran document commands (text '{}').\n",
                                _chrome->ShortcutSearchTextForTest()));
    }
    if (!key(VK_ESCAPE, 0) || _chrome->ShortcutsOpen() || !_chrome->CanvasFocused())
    {
        return fail(L"Escape did not close the Command List and give the keyboard back to the canvas.\n");
    }

    // AltGr arrives as Ctrl+Alt: it never runs a Ctrl chord (AZERTY @ is AltGr+0, € AltGr+E, Polish ż AltGr+Z).
    if (key('E', MK_CONTROL | kKeyAlt) || key('0', MK_CONTROL | kKeyAlt) || key('Z', MK_CONTROL | kKeyAlt) ||
        _session.history.UndoCount() != beforeTyping || _viewport.zoom != zoom)
    {
        return fail(L"Ctrl+Alt (AltGr) ran a Ctrl chord.\n");
    }

    // The Command List: Enter in the search field runs the only match, and a typed space is text there, not a pan.
    const bool sampleRing = _session.showsSampleRing;
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    typeText(PlainCommandName(LoadStringResource(nullptr, IDS_CMD_SAMPLE_RING)));
    if (_spacePan || _chrome->ShortcutRowCountForTest() != 1 ||
        _chrome->ShortcutCommandAtForTest(0) != ChromeHost::kCommandSampleRing)
    {
        return fail(std::format(L"Typing a name did not filter the Command List to it ({} rows, pan {}).\n",
                                _chrome->ShortcutRowCountForTest(), _spacePan));
    }
    (void)key(VK_RETURN, 0);
    if (_chrome->ShortcutsOpen() || _session.showsSampleRing == sampleRing)
    {
        return fail(L"Enter in the Command List's search field did not run the matching command.\n");
    }
    OnCommand(ChromeHost::kCommandSampleRing);
    // A row stays selected while its command is listed and is never replaced by another command's row: Down selects
    // the first match, narrowing the list past it clears the selection, and Enter then runs the first match.
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    typeText(L"zoom");
    (void)key(VK_DOWN, 0);
    const int firstZoom = _chrome->SelectedShortcutForTest();
    typeText(L" out");
    if (firstZoom != ChromeHost::kCommandZoomIn || _chrome->SelectedShortcutForTest() != 0 ||
        _chrome->ShortcutRowCountForTest() != 1)
    {
        return fail(std::format(L"The Command List selection moved with the filter (first {}, then {}, rows {}).\n",
                                firstZoom, _chrome->SelectedShortcutForTest(), _chrome->ShortcutRowCountForTest()));
    }
    (void)key(VK_RETURN, 0);
    if (_chrome->ShortcutsOpen() || !(_viewport.zoom < zoom))
    {
        return fail(L"Enter with no selection did not run the first match.\n");
    }
    restoreZoom();
    // Enter on the list itself runs its selected row.
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    _chrome->SelectShortcutForTest(ChromeHost::kCommandSampleRing);
    _chrome->FocusShortcutListForTest();
    (void)key(VK_RETURN, 0);
    if (_chrome->ShortcutsOpen() || _session.showsSampleRing == sampleRing)
    {
        return fail(L"Enter on the Command List did not run its selected row.\n");
    }
    OnCommand(ChromeHost::kCommandSampleRing);

    // Enter runs a row only while the list has the keyboard: in the Rename card's field it renames, even with the
    // Delete Layer row selected in the list that stays open behind.
    const std::size_t layerCount = _session.Document().has_value() ? _session.Document()->layers.size() : 0;
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    _chrome->SelectShortcutForTest(ChromeHost::kCommandDeleteLayer);
    OnCommand(ChromeHost::kCommandRenameLayer);
    typeText(L"x");
    (void)key(VK_RETURN, 0);
    const ImageLayer* renamed = _session.ActiveLayer();
    if (!_session.Document().has_value() || _session.Document()->layers.size() != layerCount || renamed == nullptr ||
        renamed->name != L"x" || _chrome->RenameCardOpen() || !_chrome->ShortcutsOpen())
    {
        return fail(std::format(L"Enter in the Rename card ran the Command List row (layers {} of {}, name '{}').\n",
                                _session.Document().has_value() ? _session.Document()->layers.size() : 0, layerCount,
                                renamed != nullptr ? renamed->name : std::wstring{}));
    }
    (void)key(VK_ESCAPE, 0);
    // Closing the list gives the keyboard back to the card underneath: typing reaches the Rename card again.
    OnCommand(ChromeHost::kCommandRenameLayer);
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    (void)key(VK_ESCAPE, 0);
    const std::wstring focusedName = _chrome->FocusedControlNameForTest();
    typeText(L"y");
    (void)key(VK_RETURN, 0);
    renamed = _session.ActiveLayer();
    if (_chrome->ShortcutsOpen() || focusedName != LoadStringResource(nullptr, IDS_RENAME_NAME) || renamed == nullptr ||
        !renamed->name.ends_with(L"y") || _chrome->RenameCardOpen())
    {
        return fail(std::format(L"Closing the Command List did not give the keyboard back to the Rename card "
                                L"(focus '{}', name '{}').\n",
                                focusedName, renamed != nullptr ? renamed->name : std::wstring{}));
    }
    undoTo(historyStart);
    _chrome->SetCanvasFocused(true);

    // Space pans with the canvas focused, for as long as it is held.
    const auto spaceMessage = [this](UINT message)
    {
        const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC)) << 16;
        (void)SendMessageW(_window.get(), message, VK_SPACE, 1 | scan | (message == WM_KEYUP ? kKeyUpFlags : 0));
    };
    settle();
    _lastKeyHandled = false;
    spaceMessage(WM_KEYDOWN);
    const bool panning = _lastKeyHandled && _spacePan;
    spaceMessage(WM_KEYUP);
    if (!panning || _spacePan)
    {
        return fail(std::format(L"Space on the canvas did not pan until its key up (held {}, after {}).\n", panning,
                                _spacePan));
    }

    // The menu bar takes a tapped Alt and F10 (no system-menu loop), Escape gives the keyboard back, and
    // Alt+letter opens that menu; a handled Alt chord opens nothing.
    gMenuLoopGuardFired = false;
    (void)SetTimer(_window.get(), kMenuLoopGuardTimerId, 3000, &EndMenuLoopForTest);
    (void)key(VK_MENU, kKeyAlt);
    const bool altTapped = _chrome->MenuKeyboardActive() && !_chrome->CanvasFocused();
    (void)key(VK_ESCAPE, 0);
    const bool altLeft = !_chrome->MenuKeyboardActive() && _chrome->CanvasFocused();
    (void)key(VK_F10, 0);
    const bool f10Pressed = _chrome->MenuKeyboardActive();
    (void)key(VK_ESCAPE, 0);
    (void)SendMessageW(_window.get(), WM_SYSCOMMAND, SC_KEYMENU, 0);
    const bool keyMenu = _chrome->MenuKeyboardActive();
    (void)SendMessageW(_window.get(), WM_SYSCOMMAND, SC_KEYMENU, 0);
    const bool keyMenuLeft = !_chrome->MenuKeyboardActive() && _chrome->CanvasFocused();
    (void)KillTimer(_window.get(), kMenuLoopGuardTimerId);
    if (gMenuLoopGuardFired || !altTapped || !altLeft || !f10Pressed || !keyMenu || !keyMenuLeft)
    {
        return fail(std::format(L"The menu bar did not take the keyboard from a tapped Alt or F10 (loop {}, Alt {}, "
                                L"left {}, F10 {}, SC_KEYMENU {}/{}).\n",
                                gMenuLoopGuardFired, altTapped, altLeft, f10Pressed, keyMenu, keyMenuLeft));
    }
    (void)_chrome->TakeRecordedMenuForTest();
    (void)key('F', kKeyAlt);
    const std::optional<std::size_t> fileMenu = _chrome->TakeRecordedMenuForTest();
    replace({binding(swapId.c_str(), Alt(Key('F')))});
    _chrome->SetCanvasFocused(true);
    const bool altChord = key('F', kKeyAlt) && _session.ForegroundColor() != foreground;
    const std::optional<std::size_t> chordMenu = _chrome->TakeRecordedMenuForTest();
    (void)key('F', kKeyAlt);
    if (fileMenu != std::optional<std::size_t>(0) || !altChord || chordMenu.has_value() || !_chrome->CanvasFocused())
    {
        return fail(std::format(L"Alt+F did not open the File menu, or a handled Alt chord opened one too (menu {}, "
                                L"chord {}).\n",
                                fileMenu.has_value() ? static_cast<int>(fileMenu.value()) : -1, altChord));
    }

    // The Move tool's digits follow the legacy golden (LayerAppearanceTests.moveToolNumberKeysSetSelectedLayers-
    // OpacityAsOneUndo): 5 is 50 % as one entry, a 0 later is 100 %, Undo is 50 % again; 4 then 5 at once is 45 %;
    // a brush tool's digit leaves the layer alone.
    replace({});
    OnCommand(ChromeHost::kCommandNewLayer);
    _chrome->SetCanvasFocused(true);
    const int beforeDigits = _session.history.UndoCount();
    _session.ClearPendingOpacityDigit();
    const auto opacity = [this]()
    { return _session.ActiveLayer() != nullptr ? _session.ActiveLayer()->opacity : -1.0; };
    const bool fifty = key('5', 0) && std::fabs(opacity() - 0.5) < 1e-9 &&
                       _session.history.UndoCount() == beforeDigits + 1 &&
                       _session.history.UndoName() == L"Layer Opacity";
    _session.ClearPendingOpacityDigit(); // as if the 0.6 s window had passed
    const bool hundred = key('0', 0) && std::fabs(opacity() - 1.0) < 1e-9;
    _session.Undo();
    const bool undone = std::fabs(opacity() - 0.5) < 1e-9;
    _session.ClearPendingOpacityDigit();
    (void)key('4', 0);
    (void)key('5', 0);
    const bool twoDigits = std::fabs(opacity() - 0.45) < 1e-9;
    OnTool(EditorTool::Brush);
    _chrome->SetCanvasFocused(true);
    _session.ClearPendingOpacityDigit();
    (void)key('3', 0);
    const bool brushOnly = std::fabs(opacity() - 0.45) < 1e-9 && std::fabs(_session.brushSettings.opacity - 0.3) < 1e-9;
    OnTool(EditorTool::Move);
    if (!fifty || !hundred || !undone || !twoDigits || !brushOnly)
    {
        return fail(std::format(L"The Move tool's digit keys did not follow the two-digit rule (50 % {}, 100 % {}, "
                                L"undo {}, 45 % {}, brush {}).\n",
                                fifty, hundred, undone, twoDigits, brushOnly));
    }
    undoTo(historyStart);

    // Every key the settings grammar names shows as text (a named key, a function key, a VK_XX key), numpad plus and
    // minus are not the number row's keys, and a replacement on a named key shows in the Command List.
    for (const KeyChord chord : {Key(VK_F5), Ctrl(Key(VK_F5)), Key(VK_SPACE), Key(VK_HOME), Key(VK_RETURN), Key(0xE7)})
    {
        if (ChromeHost::ChordDisplayText(chord).empty())
        {
            return fail(std::format(L"The chord {} shows no text.\n", CanonicalChordText(chord)));
        }
    }
    if (ChromeHost::ChordDisplayText(Key(VK_ADD)) == ChromeHost::ChordDisplayText(At(KeyPosition::NumberRowPlus)) ||
        ChromeHost::ChordDisplayText(Key(VK_SUBTRACT)) == ChromeHost::ChordDisplayText(At(KeyPosition::NumberRowMinus)))
    {
        return fail(L"A numpad key showed the number row's name.\n");
    }
    replace({binding(L"cmd/view/sampleRing", Key(VK_F5))});
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    std::size_t ringRow = 0;
    while (ringRow < _chrome->ShortcutRowCountForTest() &&
           _chrome->ShortcutCommandAtForTest(ringRow) != ChromeHost::kCommandSampleRing)
    {
        ++ringRow;
    }
    const std::wstring ringShown = _chrome->ShortcutChordAt(ringRow);
    _chrome->CloseShortcuts();
    if (ringShown.empty() || ringShown != ChromeHost::ChordDisplayText(Key(VK_F5)) ||
        !_chrome->MenuItemCaption(ChromeHost::kCommandSampleRing).ends_with(L"\t" + ringShown))
    {
        return fail(std::format(L"A replacement on F5 did not show ('{}').\n", ringShown));
    }

    // What the chrome shows is the effective chord: layout key names (the number-row plus, the bracket, Delete),
    // STRINGTABLE modifier names, and a replacement wherever the command appears (the dynamic Undo and Invert
    // captions, the rail tooltip).
    replace({});
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    std::size_t zoomRow = 0;
    while (zoomRow < _chrome->ShortcutRowCountForTest() &&
           _chrome->ShortcutCommandAtForTest(zoomRow) != ChromeHost::kCommandZoomIn)
    {
        ++zoomRow;
    }
    const std::wstring plusName = keyName(KeyPositionScanCode(KeyPosition::NumberRowPlus), false);
    const std::wstring bracketName = keyName(KeyPositionScanCode(KeyPosition::BracketLeft), false);
    const std::wstring zoomExpected = join(ctrlName, plusName.empty() ? std::wstring(L"+") : plusName);
    const std::wstring layerExpected = join(ctrlName, bracketName.empty() ? std::wstring(L"[") : bracketName);
    const std::wstring zoomShown = _chrome->ShortcutChordAt(zoomRow);
    const std::wstring zoomMenu = _chrome->MenuItemCaption(ChromeHost::kCommandZoomIn);
    const std::wstring layerMenu = _chrome->MenuItemCaption(ChromeHost::kCommandMoveLayerDown);
    _chrome->CloseShortcuts();
    if (zoomShown != zoomExpected || !zoomMenu.ends_with(L"\t" + zoomExpected) ||
        !layerMenu.ends_with(L"\t" + layerExpected))
    {
        return fail(std::format(L"Keyboard shortcut display did not follow the layout. Zoom badge '{}' expected '{}', "
                                L"zoom menu '{}', layer menu '{}'.\n",
                                zoomShown, zoomExpected, zoomMenu, layerMenu));
    }
    replace({binding(L"cmd/edit/undo", Ctrl(Key('K'))), binding(L"cmd/edit/invert", Ctrl(Shift(Key('K')))),
             binding(L"cmd/tool/brush", Key('K'))});
    SyncChrome();
    const std::wstring undoMenu = _chrome->MenuItemCaption(ChromeHost::kCommandUndo);
    const std::wstring invertMenu = _chrome->MenuItemCaption(ChromeHost::kCommandInvert);
    const std::wstring brushTip = _chrome->ToolTooltipForTest(EditorTool::Brush);
    if (!undoMenu.ends_with(L"\t" + join(ctrlName, L"K")) ||
        !invertMenu.ends_with(L"\t" + join(ctrlName, join(shiftName, L"K"))) ||
        brushTip != FormatStringResource(nullptr, IDS_TOOL_TOOLTIP, LoadStringResource(nullptr, IDS_TOOL_BRUSH),
                                         std::wstring(L"K")))
    {
        return fail(std::format(L"A replacement did not show in the menus and tooltips (Undo '{}', Invert '{}', "
                                L"Brush '{}').\n",
                                undoMenu, invertMenu, brushTip));
    }
    replace({});
    SyncChrome();
    undoTo(historyStart);
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    return 0;
}

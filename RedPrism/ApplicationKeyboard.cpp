#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Resources.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// The keyboard side of Application (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators", Specs/Core/
// Core_Settings.md "Command chords"). A key down goes, in order, to: the Escape that closes a floating card; the
// gestures of an edit in progress on the canvas (Enter and Escape of a pending transform, gradient, crop, panel or
// card, the lasso's corner keys); the effective binding table (`_bindings`: the settings' replacements, then the
// catalog defaults), which runs a command through OnCommand; Space, which pans unless a chrome text input or a
// control the keyboard reached has the focus; the canvas's own keys (opacity digits); and last the chrome. A key a
// command or a gesture took types no character, and neither does one that moved the keyboard. The menu bar takes a
// tapped Alt, F10 and Alt+letter; Alt+F4 and Alt+Space stay the window manager's.

void SelfTestFailure(const wchar_t* message) noexcept; // Application.cpp

namespace
{
constexpr LPARAM kKeyAltContext = static_cast<LPARAM>(1) << 29; // a system key pressed with Alt held
constexpr LPARAM kKeyRepeatFlag = static_cast<LPARAM>(1) << 30; // the key was already down
constexpr LPARAM kKeyUpFlags = (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
constexpr UINT_PTR kMenuLoopGuardTimerId = 9;

// Whether the active layout types a character for the key with the keyboard state as it is: with Ctrl and Alt held,
// AltGr text (€, @, Polish ż) rather than a chord. Flag bit 2 leaves the keyboard state, dead keys included, as it
// was (Windows 10 1607 and later).
[[nodiscard]] bool KeyTypesCharacter(UINT virtualKey, UINT scanCode) noexcept
{
    std::array<BYTE, 256> state{};
    if (!KeyboardState::Read(state))
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
// thread's key state as of this message; AltGr reports Ctrl and Alt) and the auto-repeat bit. `textInput`: a chrome
// text input has the keyboard.
[[nodiscard]] KeyPress KeyPressFromMessage(WPARAM wParam, LPARAM lParam, bool textInput) noexcept
{
    KeyPress press;
    press.virtualKey = static_cast<std::uint16_t>(wParam & 0xFFFFu);
    UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFFu;
    bool extended = (static_cast<UINT>(lParam) & (1u << 24)) != 0;
    if (scanCode == 0)
    {
        // A message without a scan code (a macro pad, SendInput with a virtual key only): the key the active layout
        // gives that virtual key, so [ ], the number-row plus and minus keys and the rest keep their position.
        const UINT mapped = MapVirtualKeyW(press.virtualKey, MAPVK_VK_TO_VSC_EX);
        scanCode = mapped & 0xFFu;
        extended = (mapped & 0xFF00u) != 0;
    }
    press.position = KeyPositionFromScanCode(scanCode, extended);
    press.ctrl = KeyboardState::Held(VK_CONTROL);
    press.alt = KeyboardState::Held(VK_MENU);
    press.shift = KeyboardState::Held(VK_SHIFT);
    press.repeat = (lParam & kKeyRepeatFlag) != 0;
    // Ctrl and Alt are text only from AltGr (the right Alt without the left one) or in a text input, and only for a
    // key the layout types a character for. Left Ctrl with left Alt stays a chord where AltGr would type: Ctrl+Alt+G
    // still toggles the clipping mask on Czech, Slovak or Hungarian, where AltGr+G is "]".
    const bool altGr = KeyboardState::Held(VK_RMENU) && !KeyboardState::Held(VK_LMENU);
    if (press.ctrl && press.alt && (altGr || textInput))
    {
        press.typesCharacter = KeyTypesCharacter(press.virtualKey, scanCode);
    }
    return press;
}

[[nodiscard]] UINT HeldModifiers() noexcept
{
    return (KeyboardState::Held(VK_SHIFT) ? MK_SHIFT : 0u) | (KeyboardState::Held(VK_CONTROL) ? MK_CONTROL : 0u);
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

// Keys that move the keyboard by design: holding one keeps moving it.
[[nodiscard]] bool IsNavigationKey(std::uint16_t virtualKey) noexcept
{
    return virtualKey == VK_TAB || (virtualKey >= VK_PRIOR && virtualKey <= VK_DOWN);
}

bool gMenuLoopGuardFired = false;
bool gMenuLoopGuardArmed = false;

// Self-test guard: a modal system-menu loop would wait forever for input a hidden window never gets.
void CALLBACK EndMenuLoopForTest(HWND, UINT, UINT_PTR, DWORD) noexcept
{
    if (!gMenuLoopGuardArmed)
        return;
    gMenuLoopGuardFired = true;
    (void)EndMenu();
}

[[nodiscard]] auto GuardMenuLoopForTest(HWND window) noexcept
{
    if (!SetTimer(window, kMenuLoopGuardTimerId, 3000, &EndMenuLoopForTest))
    {
        SelfTestFailure(L"The keyboard harness could not arm its menu-loop guard.\n");
        std::_Exit(5);
    }
    gMenuLoopGuardArmed = true;
    return wil::scope_exit(
        [window]() noexcept
        {
            gMenuLoopGuardArmed = false;
            (void)KillTimer(window, kMenuLoopGuardTimerId);
        });
}
} // namespace

LRESULT Application::OnKeyDown(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (_uiBenchmark)
        BeginUiCancellationKey(message, wParam, lParam);
    const auto endCancellationDispatch = wil::scope_exit(
        [this]() noexcept
        {
            if (_uiBenchmark)
                EndUiCancellationKey();
        });
    const KeyPress press = KeyPressFromMessage(wParam, lParam, _chrome && _chrome->TextInputFocused());
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
    if (press.repeat && press.virtualKey == _keyMovedFocus)
    {
        // The key's press moved the keyboard (Space or Enter on a row that opened the Rename card, an Escape that
        // closed a card): until it is released, its repeats neither type into nor press what the keyboard landed on.
        _suppressCharacter = true;
        return message == WM_SYSKEYDOWN ? DefWindowProcW(window, message, wParam, lParam) : 0;
    }
    if (!press.repeat)
    {
        _keyMovedFocus = 0;
    }
    if (_chrome && _chrome->MenuKeyboardActive())
    {
        if (!press.ctrl && !press.alt)
        {
            // The menu bar has the keyboard: arrows move, Enter, Space or Down open, Escape leaves, and a letter
            // (its WM_CHAR) opens that menu. A key the bar used types nothing where the keyboard goes next.
            if (_chrome->DispatchKey(press.virtualKey, true, ChromeKeyModifiers(press)))
            {
                InvalidateFrame();
                _suppressCharacter = true;
            }
            return 0;
        }
        _chrome->LeaveMenuKeyboard(); // a chord leaves the menu bar and runs as usual
    }
    else if (_chrome)
    {
        _chrome->LeaveMenuKeyboard(); // a bar that lost DxUi's focus gives the keyboard back first (else nothing)
    }
    // A key down that moved the keyboard (Tab, a button or a row that opened a card, a command that closed one) types
    // nothing where it landed, and its repeats go nowhere; Tab and the arrows keep moving it while held.
    const ChromeHost::KeyboardFocusMark focusBefore =
        _chrome ? _chrome->KeyboardFocus() : ChromeHost::KeyboardFocusMark{};
    const auto movedKeyboard = [this, &press, &focusBefore]() noexcept
    {
        const bool navigation = IsNavigationKey(press.virtualKey);
        if (!_chrome || !_chrome->NoteKeyFocusMove(focusBefore, navigation))
        {
            return false;
        }
        if (!navigation)
        {
            _keyMovedFocus = press.virtualKey;
        }
        return true;
    };
    if (HandleKey(press))
    {
        _lastKeyHandled = true;
        _suppressCharacter = true;
        InvalidateFrame();
        (void)movedKeyboard();
        // DefWindowProc still sees an Alt chord a command took (its character stays dropped), so it knows Alt was
        // not tapped alone: releasing Alt, before or after the chord's key, gives the menu bar nothing.
        return message == WM_SYSKEYDOWN && (lParam & kKeyAltContext) != 0
                   ? DefWindowProcW(window, message, wParam, lParam)
                   : 0;
    }
    // A held Space or Enter presses a chrome control (or runs a list row) once, not at the key's repeat rate.
    const bool heldPress = press.repeat && (press.virtualKey == VK_SPACE || press.virtualKey == VK_RETURN) &&
                           !(_chrome && _chrome->TextInputFocused());
    const bool chromeHandled =
        !heldPress && _chrome && _chrome->DispatchKey(press.virtualKey, true, ChromeKeyModifiers(press));
    if (chromeHandled)
    {
        InvalidateFrame();
    }
    if (heldPress || movedKeyboard())
    {
        _suppressCharacter = true;
    }
    if (message != WM_SYSKEYDOWN)
    {
        return 0;
    }
    // An Alt+key a control took does not also open a menu through its WM_SYSCHAR.
    _suppressCharacter = _suppressCharacter || chromeHandled;
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::OnKeyUp(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (_uiBenchmark)
        NoteUiCancellationKeyUp(message, wParam);
    const UINT virtualKey = static_cast<UINT>(wParam);
    // The key down's character, if it had one, arrived before this key up.
    _suppressCharacter = false;
    if (virtualKey == _keyMovedFocus)
    {
        _keyMovedFocus = 0;
    }
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
    }
    else if (virtualKey != VK_MENU && virtualKey != VK_F10 && _chrome &&
             _chrome->DispatchKey(virtualKey, false, HeldModifiers()))
    {
        InvalidateFrame();
    }
    // Every key up reaches DefWindowProc, which tells a tapped Alt or F10 (it sends SC_KEYMENU, which OnKeyMenu gives
    // to the menu bar) from a key up that ends the tap: releasing Alt after Alt+Backspace opens nothing.
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::OnCharacter(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const wchar_t character = static_cast<wchar_t>(wParam);
    if (_suppressCharacter)
    {
        // The key down ran a command or a gesture: its character types nothing (and a handled Alt chord does not
        // beep through the menu loop). The main loop never translates such a key (DispatchQueuedMessage); a loop
        // that translates first (a modal one) still posts the character, which this drops.
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
            InvalidateFrame();
        }
        return 0;
    }
    if (_chrome && _chrome->DispatchCharacter(character, HeldModifiers()))
    {
        InvalidateFrame();
    }
    return 0;
}

KeyChord Application::NormalizeLayoutChord(KeyChord chord) noexcept
{
    const std::uint16_t key = chord.virtualKey;
    if (chord.position != KeyPosition::None || (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
    {
        return chord; // a position already, or a letter or digit, which follows the layout's legend by design
    }
    const UINT mapped = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    const KeyPosition position = KeyPositionFromScanCode(mapped & 0xFFu, (mapped & 0xFF00u) != 0);
    if (position != KeyPosition::None)
    {
        chord.virtualKey = 0;
        chord.position = position;
    }
    return chord;
}

void Application::OnKeyboardLayoutChanged() noexcept
{
    _bindings.Build(_settings.shortcuts, &NormalizeLayoutChord);
    if (_chrome)
    {
        _chrome->SetCommandBindings(&_bindings); // retitles the menus, the Command List and the tooltips
        InvalidateFrame();
    }
}

void Application::DispatchQueuedMessage(MSG& message) noexcept
{
    if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && message.hwnd == _window.get())
    {
        // The key down runs first, as TranslateAccelerator runs before TranslateMessage: a key a command, a gesture
        // or the chrome took, or one that moved the keyboard, is never translated. No character reaches a dialog the
        // command opened, no dead key it sits on (French ^ is Brush Smaller) arms an accent for the next text, and no
        // flag is left behind to drop a later character.
        _suppressCharacter = false;
        (void)DispatchMessageW(&message);
        if (!std::exchange(_suppressCharacter, false))
        {
            (void)TranslateMessage(&message);
        }
        return;
    }
    (void)TranslateMessage(&message);
    (void)DispatchMessageW(&message);
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
    InvalidateFrame();
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
    // Escape closes the floating card on top, wherever the focus is: More tools, the notice, the Command List.
    if (press.virtualKey == VK_ESCAPE && !press.ctrl && _chrome->EscapeFloatingCard())
    {
        SealSamplerForBoundary();
        return true;
    }
    if (press.virtualKey == VK_ESCAPE && !press.ctrl && !press.alt && CanvasBrushSealedStrokePending())
    {
        CancelHostContinuations(true); // The accepted terminal and original worker retain their authority.
        return true;
    }
    if (press.virtualKey == VK_ESCAPE && !press.ctrl && !press.alt &&
        (_samplerHostTicket || (std::get_if<ColorSampling>(&_drag) && std::get<ColorSampling>(_drag).opening)))
    {
        SealSamplerForBoundary();  // Release the gesture/capture even while memory admission awaits cancellation.
        CancelHostContinuations(); // Retain published color; acknowledge cancellation of the remaining prefix.
        return true;
    }
    if (press.virtualKey == VK_ESCAPE && !press.ctrl && !press.alt && CancelCanvasBrush(true))
        return true;
    if (press.virtualKey == VK_ESCAPE && !press.ctrl && !press.alt &&
        (_documentLifecycleTicket || _sizingTicket || _pathClassificationTicket || _rasterEditTicket ||
         _gradientCommitTicket || _pixelMoveCommitTicket))
    {
        CancelHostContinuations(); // retain the selected request until worker acknowledgement
        return true;
    }
    const KeyState state = CurrentKeyState();
    // The gestures of an edit under way take the key with the canvas focused, and Enter and Escape also from a chrome
    // control a click focused: a clicked header button is not pressed again by the Enter that applies the transform,
    // and Escape still cancels it (legacy Apply and Cancel are the default and cancel buttons). A control the keyboard
    // reached, a text input, an open dropdown and a card that took the keyboard keep both keys.
    const bool clickedEditKey = (press.virtualKey == VK_RETURN || press.virtualKey == VK_ESCAPE) &&
                                state.focus == KeyFocus::Chrome && !_chrome->FocusTakesEnterAndEscape();
    if ((state.focus == KeyFocus::Canvas || clickedEditKey) && !press.ctrl && HandleEditGesture(press))
    {
        return true;
    }
    const KeyMatch match = _bindings.Match(press, state);
    if (match.result == KeyMatchResult::Command)
    {
        // A nudge steps by ten while it runs when Shift was added to its chord (Shift+Right), never when the chord
        // itself holds Shift (a replacement on Shift+J).
        _acceleratorShift = match.shiftAdded;
        const auto clearAcceleratorShift = wil::scope_exit([this]() noexcept { _acceleratorShift = false; });
        OnCommand(match.command);
        return true;
    }
    if (match.result == KeyMatchResult::HeldKey)
    {
        return true; // holding the key runs its command once (the Marquee's M does not keep switching)
    }
    // Space pans while held, over any tool, wherever the keyboard is but in a chrome text input and on a chrome
    // control the keyboard reached (Tab), which takes Space as its press. A click leaves a control focused, and Space
    // after it still pans: the clicked layer row does not open its Rename card, a clicked button does not run again.
    if (press.virtualKey == VK_SPACE && !press.ctrl && !press.alt &&
        (state.focus == KeyFocus::Canvas || (state.focus == KeyFocus::Chrome && !_chrome->FocusFromKeyboard())))
    {
        _spacePan = true;
        return true;
    }
    return state.focus == KeyFocus::Canvas && HandleCanvasKey(press);
}

bool Application::HandleEditGesture(const KeyPress& press) noexcept
{
    const UINT key = press.virtualKey;
    if (key == VK_RETURN || key == VK_ESCAPE)
    {
        // Enter applies and Escape cancels the first edit under way that takes the key, in the one order Undo and a
        // settle use (RF-904): the size card's and the panel's OK and Cancel first, then the canvas's edits.
        return EndFirstCanvasEdit(key == VK_RETURN ? EditEnd::Enter : EditEnd::Escape);
    }
    if ((key == VK_BACK || key == VK_DELETE) && _session.PendingLasso().has_value())
    {
        // Backspace or Delete removes the last polygonal corner.
        _session.RemoveLastLassoPoint();
        DropDrag<DraftDrag>();
        SelectionChanged();
        return true;
    }
    return false;
}

bool Application::HandleCanvasKey(const KeyPress& press) noexcept
{
    if (_historyWork.Busy() || _activeHostRequest || _hostRequests.Size())
    {
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
        return true;
    }
    const UINT key = press.virtualKey;
    const bool plain = !press.ctrl && !press.alt && !press.shift;
    if (plain && key >= '0' && key <= '9' &&
        (EditorToolIsBrush(_tool) || _tool == EditorTool::Move || _tool == EditorTool::Gradient))
    {
        // Photoshop's opacity keys (legacy typeOpacityDigit): 1–9 are 10–90 %, 0 is 100 %, two digits within 0.6 s
        // an exact percent. The brush's opacity, the gradient's, or with the Move tool the active layer's: one
        // Layer Opacity entry per key that changes it. Holding a digit sets it once: an auto-repeat would pair with
        // the press (5 then 5 is 55 %) and add an entry per repeat.
        if (press.repeat)
        {
            return true;
        }
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
    const bool altGr = (modifiers & kKeyAltGr) != 0;
    const bool ctrl = (modifiers & MK_CONTROL) != 0 || altGr;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool alt = (modifiers & kKeyAlt) != 0 || altGr;
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
    const bool restore = KeyboardState::Read(saved);
    std::array<BYTE, 256> state = saved;
    const auto hold = [&state](UINT which, bool down) noexcept
    { state[which & 0xFFu] = down ? static_cast<BYTE>(0x80) : static_cast<BYTE>(0x00); };
    hold(VK_CONTROL, ctrl);
    hold(VK_LCONTROL, ctrl);
    hold(VK_RCONTROL, false);
    hold(VK_MENU, alt);
    hold(VK_LMENU, alt && !altGr);
    hold(VK_RMENU, altGr);
    hold(VK_SHIFT, shift);
    hold(VK_LSHIFT, shift);
    hold(VK_RSHIFT, false);
    hold(key, true);
    KeyboardState::Write(state);
    // As Windows sends them: a system key with Alt (not with Ctrl too) or F10.
    const bool system = (alt && !ctrl) || key == VK_F10;
    const LPARAM down = 1 | (static_cast<LPARAM>(scanCode) << 16) | (extended ? static_cast<LPARAM>(1) << 24 : 0) |
                        (alt ? static_cast<LPARAM>(1) << 29 : 0) | (repeat ? kKeyRepeatFlag : 0);
    _lastKeyHandled = false;
    (void)SendMessageW(window, system ? WM_SYSKEYDOWN : WM_KEYDOWN, key, down);
    const bool handled = _lastKeyHandled;
    // The characters TranslateMessage would post for this key, as the message loop translates it: only when the key
    // down left its character (DispatchQueuedMessage).
    std::array<wchar_t, 4> characters{};
    const int count = std::exchange(_suppressCharacter, false)
                          ? 0
                          : ToUnicodeEx(key, scanCode, state.data(), characters.data(),
                                        static_cast<int>(characters.size()), 1u << 2, GetKeyboardLayout(0));
    for (int index = 0; index < count; ++index)
    {
        (void)SendMessageW(window, system ? WM_SYSCHAR : WM_CHAR, characters[static_cast<std::size_t>(index)], down);
    }
    hold(key, false);
    KeyboardState::Write(state);
    (void)SendMessageW(window, system ? WM_SYSKEYUP : WM_KEYUP, key, down | kKeyUpFlags);
    if (restore)
    {
        KeyboardState::Write(saved);
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
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The keyboard harness could not settle admitted work.\n");
            std::_Exit(5);
        }
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    };
    const auto key = [this, &settle](UINT virtualKey, UINT modifiers, bool repeat = false)
    {
        settle();
        bool handled = false;
        {
            // Only a nested system-menu loop needs the timer. Worker acknowledgement and frames may take
            // longer under load; pumping them with the guard armed would report an unrelated timeout.
            const auto guard = GuardMenuLoopForTest(_window.get());
            handled = HandleAccelerator(virtualKey, modifiers, repeat);
        }
        settle();
        return handled;
    };
    const AppSettings original = _settings;
    const auto replace = [this, &original](std::initializer_list<ShortcutBinding> bindings)
    {
        AppSettings changed = original;
        changed.shortcuts = bindings;
        ApplySettings(changed);
    };
    const auto binding = [](const wchar_t* commandId, KeyChord chord) { return ShortcutBinding{commandId, chord}; };
    const auto undoTo = [this, &settle](int count)
    {
        const int allowance = (std::max)(0, _session.history.UndoCount() - count) + 2;
        int attempts = 0;
        while (_session.history.CanUndo() && _session.history.UndoCount() > count)
        {
            if (attempts++ >= allowance)
            {
                SelfTestFailure(L"The keyboard Undo fixture made no bounded progress toward its original history.\n");
                std::_Exit(5);
            }
            OnCommand(ChromeHost::kCommandUndo);
            settle();
        }
        if (_session.history.UndoCount() != count)
        {
            SelfTestFailure(L"The keyboard Undo fixture did not restore its original entry count.\n");
            std::_Exit(5);
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
    // A replacement written by virtual key for a position key (vk VK_BB, the US =) takes that key's default: Zoom In
    // no longer shows the chord it lost, Zoom Out shows it, and the key zooms out.
    {
        const auto plusKey = static_cast<std::uint16_t>(
            MapVirtualKeyW(KeyPositionScanCode(KeyPosition::NumberRowPlus), MAPVK_VSC_TO_VK));
        replace({binding(L"cmd/view/zoomOut", Ctrl(Key(plusKey)))});
        _chrome->SetCanvasFocused(true);
        const std::wstring lost = L"\t" + ChromeHost::ChordDisplayText(Ctrl(At(KeyPosition::NumberRowPlus)));
        const std::wstring zoomInMenu = _chrome->MenuItemCaption(ChromeHost::kCommandZoomIn);
        const bool menusFollow =
            !zoomInMenu.ends_with(lost) && _chrome->MenuItemCaption(ChromeHost::kCommandZoomOut).ends_with(lost);
        const bool zoomsOut = key(VK_OEM_PLUS, MK_CONTROL) && _viewport.zoom < zoom;
        restoreZoom();
        replace({});
        _chrome->SetCanvasFocused(true);
        if (!menusFollow || !zoomsOut)
        {
            return fail(std::format(L"A replacement on the number-row plus key's virtual key left Zoom In showing it "
                                    L"(Zoom In '{}', key zooms out {}).\n",
                                    zoomInMenu, zoomsOut));
        }
    }

    // Replacing the Delete key's command: Delete does nothing, the new key clears the selected pixels, and Delete
    // Layer's shared Delete label goes with it.
    OnCommand(ChromeHost::kCommandNewLayer);
    _session.ApplySelection(SelectionGeometry::Rect(12, 12, 20, 20), SelectionMode::New, L"Select");
    OnCommand(ChromeHost::kCommandFillForeground);
    settle(); // the Fill command now owns an asynchronous receipt; observe its entry before the next key
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

    // Shift steps a nudge by ten only when Shift was added to its chord: a replacement on Shift+J moves the layer
    // one pixel, Shift+Left ten.
    {
        replace({binding(L"cmd/nudge/layerLeft", Shift(Key('J')))});
        OnCommand(ChromeHost::kCommandNewLayer);
        _session.ApplySelection(SelectionGeometry::Rect(12, 12, 20, 20), SelectionMode::New, L"Select");
        OnCommand(ChromeHost::kCommandFillForeground);
        settle();
        _session.Deselect();
        _chrome->SetCanvasFocused(true);
        const auto originX = [this]()
        { return _session.ActiveLayer() != nullptr ? _session.ActiveLayer()->transform.origin.x : 0.0; };
        const double start = originX();
        const bool one = key('J', MK_SHIFT) && std::fabs(originX() - (start - 1.0)) < 1e-9;
        replace({});
        _chrome->SetCanvasFocused(true);
        const bool ten = key(VK_LEFT, MK_SHIFT) && std::fabs(originX() - (start - 11.0)) < 1e-9;
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (!one || !ten)
        {
            return fail(std::format(L"Shift on a nudge whose chord holds Shift stepped by ten, or Shift+Left did not "
                                    L"(Shift+J one pixel {}, Shift+Left ten {}).\n",
                                    one, ten));
        }
    }

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
    // Left Ctrl with left Alt is a chord even where AltGr types a character for the key; AltGr (the right Alt) types
    // it. On a layout where AltGr types a letter key (French and German: E is €), a replacement on Ctrl+Alt with that
    // key runs from the left keys and not from AltGr, as Ctrl+Alt+G (the clip toggle) must where AltGr+G is text
    // (Czech, Slovak, Hungarian: "]"). A layout without such a letter (US) has nothing to show here.
    {
        std::uint16_t altGrLetter = 0;
        for (std::uint16_t letter = 'A'; letter <= 'Z' && altGrLetter == 0; ++letter)
        {
            std::array<BYTE, 256> altGr{};
            altGr[VK_CONTROL] = 0x80;
            altGr[VK_LCONTROL] = 0x80;
            altGr[VK_MENU] = 0x80;
            altGr[VK_RMENU] = 0x80;
            std::array<wchar_t, 4> typed{};
            const int count = ToUnicodeEx(letter, MapVirtualKeyW(letter, MAPVK_VK_TO_VSC), altGr.data(), typed.data(),
                                          static_cast<int>(typed.size()), 1u << 2, GetKeyboardLayout(0));
            if (count > 0 && typed[0] >= L' ')
            {
                altGrLetter = letter;
            }
        }
        if (altGrLetter != 0)
        {
            replace({binding(swapId.c_str(), Ctrl(Alt(Key(altGrLetter))))});
            _chrome->SetCanvasFocused(true);
            const std::uint32_t unswapped = _session.ForegroundColor();
            const bool leftKeys = key(altGrLetter, MK_CONTROL | kKeyAlt) && _session.ForegroundColor() != unswapped;
            const std::uint32_t swapped = _session.ForegroundColor();
            const bool altGrText = !key(altGrLetter, kKeyAltGr) && _session.ForegroundColor() == swapped;
            if (_session.ForegroundColor() != unswapped)
            {
                OnCommand(ChromeHost::kCommandSwapPalette);
            }
            replace({});
            _chrome->SetCanvasFocused(true);
            if (!leftKeys || !altGrText)
            {
                return fail(std::format(L"Ctrl+Alt+{} was not a chord from the left keys and text from AltGr (left "
                                        L"{}, AltGr {}).\n",
                                        static_cast<wchar_t>(altGrLetter), leftKeys, altGrText));
            }
        }
    }

    // The legacy File and Image key equivalents (RF-1117): Ctrl+Alt+C and Ctrl+Alt+I open the Canvas Size and Image
    // Size cards from the left Ctrl and Alt on every layout, and from AltGr only where the layout types nothing for the
    // key; Ctrl+Shift+E and Ctrl+Alt+Shift+S run the exports (a quiet run opens no dialog). The menus and the Command
    // List show all four.
    {
        const auto altGrTypes = [](UINT letter, bool shift) noexcept
        {
            std::array<BYTE, 256> saved{};
            const bool restore = KeyboardState::Read(saved);
            std::array<BYTE, 256> altGr{};
            for (const UINT held : {VK_CONTROL, VK_LCONTROL, VK_MENU, VK_RMENU})
            {
                altGr[held] = 0x80;
            }
            if (shift)
            {
                altGr[VK_SHIFT] = 0x80;
                altGr[VK_LSHIFT] = 0x80;
            }
            KeyboardState::Write(altGr);
            const bool text = KeyTypesCharacter(letter, MapVirtualKeyW(letter, MAPVK_VK_TO_VSC));
            if (restore)
            {
                KeyboardState::Write(saved);
            }
            return text;
        };
        const auto opensCard = [this, &key](UINT letter, UINT modifiers, SizeCardKind kind)
        {
            _chrome->SetCanvasFocused(true);
            const bool opened = key(letter, modifiers) && SizeCardOpen() && _sizeDialog.kind == kind;
            if (SizeCardOpen())
            {
                CancelSizeCard();
            }
            return opened;
        };
        const bool canvasCard = opensCard('C', MK_CONTROL | kKeyAlt, SizeCardKind::CanvasSize);
        const bool imageCard = opensCard('I', MK_CONTROL | kKeyAlt, SizeCardKind::ImageSize);
        const bool altGrCanvas = opensCard('C', kKeyAltGr, SizeCardKind::CanvasSize) != altGrTypes('C', false);
        const bool altGrImage = opensCard('I', kKeyAltGr, SizeCardKind::ImageSize) != altGrTypes('I', false);
        _chrome->SetCanvasFocused(true);
        const bool exportsRan = key('E', MK_CONTROL | MK_SHIFT) && key('S', MK_CONTROL | MK_SHIFT | kKeyAlt);
        const bool altGrJpeg = key('S', MK_SHIFT | kKeyAltGr) != altGrTypes('S', true);
        const std::array<std::pair<int, KeyChord>, 4> legacy{{
            {ChromeHost::kCommandExportPng, Ctrl(Shift(Key('E')))},
            {ChromeHost::kCommandExportJpeg, Ctrl(Alt(Shift(Key('S'))))},
            {ChromeHost::kCommandCanvasSize, Ctrl(Alt(Key('C')))},
            {ChromeHost::kCommandImageSize, Ctrl(Alt(Key('I')))},
        }};
        bool shown = true;
        OnCommand(ChromeHost::kCommandKeyboardShortcuts);
        for (const auto& [command, chord] : legacy)
        {
            std::size_t row = 0;
            while (row < _chrome->ShortcutRowCountForTest() && _chrome->ShortcutCommandAtForTest(row) != command)
            {
                ++row;
            }
            const std::wstring text = ChromeHost::ChordDisplayText(chord);
            shown = shown && _chrome->ShortcutChordAt(row) == text &&
                    _chrome->MenuItemCaption(command).ends_with(L"\t" + text);
        }
        _chrome->CloseShortcuts();
        _chrome->SetCanvasFocused(true);
        if (!canvasCard || !imageCard || !altGrCanvas || !altGrImage || !exportsRan || !altGrJpeg || !shown)
        {
            return fail(std::format(L"The legacy File and Image chords did not run or show (Canvas Size {}, Image Size "
                                    L"{}, AltGr {}/{}/{}, exports {}, shown {}).\n",
                                    canvasCard, imageCard, altGrCanvas, altGrImage, altGrJpeg, exportsRan, shown));
        }
    }

    // The Command List: Enter in the search field runs the only match, and a typed space is text there, not a pan.
    const bool sampleRing = _settings.sampleRing;
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    typeText(PlainCommandName(LoadStringResource(nullptr, IDS_CMD_SAMPLE_RING)));
    if (_spacePan || _chrome->ShortcutRowCountForTest() != 1 ||
        _chrome->ShortcutCommandAtForTest(0) != ChromeHost::kCommandSampleRing)
    {
        return fail(std::format(L"Typing a name did not filter the Command List to it ({} rows, pan {}).\n",
                                _chrome->ShortcutRowCountForTest(), _spacePan));
    }
    (void)key(VK_RETURN, 0);
    if (_chrome->ShortcutsOpen() || _settings.sampleRing == sampleRing)
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
    if (_chrome->ShortcutsOpen() || _settings.sampleRing == sampleRing)
    {
        return fail(L"Enter on the Command List did not run its selected row.\n");
    }
    OnCommand(ChromeHost::kCommandSampleRing);
    // With no row selected, Enter or Space on the list (reached with Tab) selects the first row and runs nothing: a
    // stray Enter once ran New. The next Enter runs the row.
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    typeText(L"zoom");
    (void)key(VK_TAB, 0);
    (void)key(VK_RETURN, 0);
    const bool enterSelects = _chrome->ShortcutsOpen() &&
                              _chrome->SelectedShortcutForTest() == ChromeHost::kCommandZoomIn &&
                              _viewport.zoom == zoom;
    _chrome->SelectShortcutForTest(0);
    (void)key(VK_SPACE, 0);
    const bool spaceSelects = _chrome->ShortcutsOpen() &&
                              _chrome->SelectedShortcutForTest() == ChromeHost::kCommandZoomIn &&
                              _viewport.zoom == zoom && !_spacePan;
    (void)key(VK_RETURN, 0);
    const bool enterRuns = !_chrome->ShortcutsOpen() && _viewport.zoom > zoom;
    restoreZoom();
    _chrome->CloseShortcuts();
    if (!enterSelects || !spaceSelects || !enterRuns)
    {
        return fail(std::format(L"Enter or Space on the Command List with no row selected ran a row (Enter selects {}, "
                                L"Space selects {}, next Enter runs {}).\n",
                                enterSelects, spaceSelects, enterRuns));
    }
    // A repaint of the open list allocates nothing per row: the model assigns into the item DxUi reuses for every
    // row (as the layers tree does, RF-506).
    OnCommand(ChromeHost::kCommandKeyboardShortcuts);
    const bool rowsKeepBuffers = _chrome->ShortcutRowsKeepBuffersForTest();
    _chrome->CloseShortcuts();
    _chrome->SetCanvasFocused(true);
    if (!rowsKeepBuffers)
    {
        return fail(L"The Command List's model dropped the item's text buffer for every row it filled.\n");
    }

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

    // A card gives the keyboard back when it closes: a notice raised from the canvas and the Rename card opened with
    // F2 return it to the canvas. An Escape the focused control leaves is the top card's Cancel (DxUi's embedded host
    // routes no cancel button): on the layers panel it closes the Canvas Size card, and the panel keeps the keyboard.
    _chrome->ShowNotice(L"Notice");
    const bool noticeHeld = _chrome->NoticeOpen() && !_chrome->CanvasFocused();
    (void)key(VK_ESCAPE, 0);
    const bool noticeReturned = !_chrome->NoticeOpen() && _chrome->CanvasFocused();
    OnCommand(ChromeHost::kCommandRenameLayer);
    const bool renameHeld = _chrome->RenameCardOpen() && _chrome->TextInputFocused();
    (void)key(VK_ESCAPE, 0);
    const bool renameReturned = !_chrome->RenameCardOpen() && _chrome->CanvasFocused();
    OnCommand(ChromeHost::kCommandCanvasSize);
    const bool sizeOpened = SizeCardOpen() && _chrome->SizeCardOpen() && _chrome->CanvasFocused();
    _chrome->FocusLayersForTest();
    (void)key(VK_ESCAPE, 0);
    const bool sizeCancelled = !SizeCardOpen() && !_chrome->SizeCardOpen() && !_chrome->CanvasFocused() &&
                               _chrome->FocusedControlNameForTest() == LoadStringResource(nullptr, IDS_A11Y_LAYERS);
    if (!noticeHeld || !noticeReturned || !renameHeld || !renameReturned || !sizeOpened || !sizeCancelled)
    {
        return fail(std::format(L"A card did not give the keyboard back or take the Escape its focused control left "
                                L"(notice {}/{}, rename {}/{}, Canvas Size {}/{}).\n",
                                noticeHeld, noticeReturned, renameHeld, renameReturned, sizeOpened, sizeCancelled));
    }
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

    // Space pans wherever the keyboard is but in a text input or on a chrome control the keyboard reached: after a
    // click on a layer row it pans (no Rename card), after a click on the palette's swap zone it pans (no second
    // swap). A control the keyboard reached (Tab) takes Space as its press, once per press (a held Space repeats
    // nothing), and a key down that moves the keyboard types nothing where it lands (the Rename card a row opens).
    {
        const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
        const auto click = [this, scale](const D2D1_RECT_F& bounds)
        {
            const LPARAM at = MAKELPARAM(static_cast<int>(std::lround((bounds.left + bounds.right) * 0.5f * scale)),
                                         static_cast<int>(std::lround((bounds.top + bounds.bottom) * 0.5f * scale)));
            OnPointer(WM_MOUSEMOVE, 0, at);
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, at);
            OnPointer(WM_LBUTTONUP, 0, at);
        };
        const auto spaceDown = [this, &spaceMessage]()
        {
            _lastKeyHandled = false;
            spaceMessage(WM_KEYDOWN);
            return _lastKeyHandled && _spacePan;
        };
        OnCommand(ChromeHost::kCommandNewLayer);
        settle();
        click(_chrome->LayerRowBoundsDip(0));
        const bool rowFocused = !_chrome->CanvasFocused() && !_chrome->FocusFromKeyboard();
        const bool rowPans = spaceDown() && !_chrome->RenameCardOpen();
        spaceMessage(WM_KEYUP);
        const bool rowReleased = !_spacePan;
        settle();
        const std::uint32_t unswapped = _session.ForegroundColor();
        click(_chrome->PaletteHotBoundsDip(false));
        const std::uint32_t swapped = _session.ForegroundColor();
        const bool zonePans = swapped != unswapped && spaceDown() && _session.ForegroundColor() == swapped;
        spaceMessage(WM_KEYUP);
        OnCommand(ChromeHost::kCommandSwapPalette);
        _chrome->CloseRenameCard(false);
        if (!rowFocused || !rowPans || !rowReleased || !zonePans || _session.ForegroundColor() != unswapped)
        {
            return fail(std::format(L"Space after a click in the chrome pressed the clicked control instead of panning "
                                    L"(row focus {}, row pans {}, released {}, swap zone pans {}).\n",
                                    rowFocused, rowPans, rowReleased, zonePans));
        }
        const bool ring = _settings.sampleRing;
        OnCommand(ChromeHost::kCommandKeyboardShortcuts);
        _chrome->SelectShortcutForTest(ChromeHost::kCommandSampleRing);
        (void)key(VK_TAB, 0);
        const bool listReached = _chrome->FocusFromKeyboard() && _chrome->FocusedControlNameForTest() ==
                                                                     LoadStringResource(nullptr, IDS_SHORTCUTS_TITLE);
        (void)key(VK_SPACE, 0, true);
        const bool heldNothing = _chrome->ShortcutsOpen() && _settings.sampleRing == ring && !_spacePan;
        (void)key(VK_SPACE, 0);
        const bool pressed = !_chrome->ShortcutsOpen() && _settings.sampleRing != ring && !_spacePan;
        OnCommand(ChromeHost::kCommandSampleRing);
        // Space held on the Rename Layer row: its press opens the card, and with the card laid out (a frame) the
        // repeats type nothing into its field.
        const auto spaceStroke = [this](bool repeat)
        {
            const LPARAM lParam = 1 | (static_cast<LPARAM>(MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC)) << 16) |
                                  (repeat ? kKeyRepeatFlag : 0);
            (void)SendMessageW(_window.get(), WM_KEYDOWN, VK_SPACE, lParam);
            if (!std::exchange(_suppressCharacter, false)) // as the message loop translates it
            {
                (void)SendMessageW(_window.get(), WM_CHAR, L' ', lParam);
            }
        };
        OnCommand(ChromeHost::kCommandKeyboardShortcuts);
        _chrome->SelectShortcutForTest(ChromeHost::kCommandRenameLayer);
        (void)key(VK_TAB, 0);
        settle();
        spaceStroke(false);
        const std::wstring opened = _chrome->FocusedTextForTest();
        settle();
        spaceStroke(true);
        const std::wstring held = _chrome->FocusedTextForTest();
        spaceMessage(WM_KEYUP);
        const ImageLayer* layer = _session.ActiveLayer();
        const bool nameKept =
            _chrome->RenameCardOpen() && layer != nullptr && opened == layer->name && held == layer->name;
        _chrome->CloseRenameCard(false);
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (!listReached || !heldNothing || !pressed || !nameKept)
        {
            return fail(std::format(L"Space on a control the keyboard reached did not press it once per press, or it "
                                    L"typed into the card it opened (Tab {}, held {}, pressed {}, field '{}' then "
                                    L"'{}').\n",
                                    listReached, heldNothing, pressed, opened, held));
        }
    }

    // Enter and Escape as Space (RF-1118, RF-1119): a control a click focused does not take the Enter that applies or
    // the Escape that cancels the edit under way. After Ctrl+T and a click on Flip Horizontal, Escape cancels the
    // transform with no entry, and Enter applies it with that one flip instead of pressing the button again (legacy
    // Apply and Cancel are the default and cancel buttons). The button keeps Enter once the keyboard reached it (Tab,
    // Shift+Tab back), and the opacity field keeps both keys. In the Canvas Size card, the units dropdown the
    // self-test focused keeps the Enter or Escape that closes its open list; the next one is the card's OK or Cancel.
    {
        const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
        const auto click = [this, scale](const D2D1_RECT_F& bounds)
        {
            const LPARAM at = MAKELPARAM(static_cast<int>(std::lround((bounds.left + bounds.right) * 0.5f * scale)),
                                         static_cast<int>(std::lround((bounds.top + bounds.bottom) * 0.5f * scale)));
            OnPointer(WM_MOUSEMOVE, 0, at);
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, at);
            OnPointer(WM_LBUTTONUP, 0, at);
        };
        const auto pendingFlip = [this]() -> std::optional<bool>
        {
            const std::optional<TransformEdit>& edit = _session.PendingTransformEdit();
            return edit.has_value() ? std::optional<bool>(edit->draft.flipX) : std::nullopt;
        };
        OnTool(EditorTool::Move);
        const Guid painted = InsertPaintedLayer(_session, L"Enter", 64, 64, Vec2{32.0, 32.0}, 0.0, 0xFF3366CCu);
        DocumentChanged();
        // The minimum layers width leaves the header room for Flip Horizontal in the self-test's 1280 DIP window.
        _chrome->SetLayersWidth(ChromeHost::kLayersMinDip);
        _chrome->SetCanvasFocused(true);
        const int entries = _session.history.UndoCount();
        (void)key('T', MK_CONTROL);
        settle();
        const D2D1_RECT_F flip = _chrome->FlipHorizontalBoundsDipForTest();
        click(flip);
        const bool escapeClicked =
            pendingFlip() == std::optional<bool>(true) && !_chrome->CanvasFocused() && !_chrome->FocusFromKeyboard();
        (void)key(VK_ESCAPE, 0);
        const ImageLayer* const unflipped = _session.ActiveLayer();
        const bool cancelled = !pendingFlip().has_value() && unflipped != nullptr && unflipped->id == painted &&
                               !unflipped->transform.flipX && _session.history.UndoCount() == entries;
        (void)key('T', MK_CONTROL);
        settle();
        click(flip);
        const bool clicked =
            pendingFlip() == std::optional<bool>(true) && !_chrome->CanvasFocused() && !_chrome->FocusFromKeyboard();
        (void)key(VK_RETURN, 0);
        const ImageLayer* const active = _session.ActiveLayer();
        const bool applied =
            !pendingFlip().has_value() && active != nullptr && active->id == painted && active->transform.flipX;
        // No key has moved the keyboard since the click: the dropdown's focus is not the keyboard's either.
        const auto sizeCardRound = [&](UINT closeKey)
        {
            OnCommand(ChromeHost::kCommandCanvasSize);
            settle();
            const bool unitFocused = _chrome->FocusSizeUnitForTest();
            (void)key(VK_F4, 0);
            const bool listOpen = _chrome->FocusTakesEnterAndEscape();
            (void)key(closeKey, 0);
            const bool listClosed = SizeCardOpen() && !_chrome->FocusTakesEnterAndEscape();
            (void)key(closeKey, 0);
            const bool cardClosed = !SizeCardOpen();
            if (SizeCardOpen())
            {
                CancelSizeCard();
            }
            return unitFocused && listOpen && listClosed && cardClosed;
        };
        const bool cardApplied = sizeCardRound(VK_RETURN);
        const bool cardCancelled = sizeCardRound(VK_ESCAPE);
        _chrome->SetCanvasFocused(true);
        (void)key('T', MK_CONTROL);
        settle();
        click(flip);
        const ChromeHost::KeyboardFocusMark button = _chrome->KeyboardFocus();
        (void)key(VK_TAB, 0);
        (void)key(VK_TAB, MK_SHIFT);
        const bool reached = _chrome->FocusFromKeyboard() && _chrome->KeyboardFocus() == button;
        (void)key(VK_RETURN, 0);
        const bool buttonPressed = pendingFlip() == std::optional<bool>(true);
        _chrome->FocusLayerOpacityForTest();
        (void)key(VK_RETURN, 0);
        _chrome->FocusLayerOpacityForTest();
        (void)key(VK_ESCAPE, 0);
        const bool fieldKept = pendingFlip().has_value();
        (void)EndCanvasEdit(CanvasEdit::Transform, EditEnd::Escape);
        _chrome->SetLayersWidth(static_cast<float>(_settings.layersPanelWidth));
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (!escapeClicked || !cancelled || !clicked || !applied || !reached || !buttonPressed || !fieldKept ||
            !cardApplied || !cardCancelled)
        {
            return fail(std::format(L"Enter or Escape on a clicked control did not apply or cancel the edit under way, "
                                    L"or a control that keeps them lost one (Flip Horizontal at {},{} clicked {}/{}, "
                                    L"cancelled {}, applied {}, reached {}, pressed {}, field {}, size card OK {}, "
                                    L"Cancel {}).\n",
                                    flip.left, flip.top, escapeClicked, clicked, cancelled, applied, reached,
                                    buttonPressed, fieldKept, cardApplied, cardCancelled));
        }
    }

    // The menu bar takes a tapped Alt and F10 (no system-menu loop), Escape gives the keyboard back, and
    // Alt+letter opens that menu; a handled Alt chord opens nothing.
    gMenuLoopGuardFired = false;
    (void)key(VK_MENU, kKeyAlt);
    const bool altTapped = _chrome->MenuKeyboardActive() && !_chrome->CanvasFocused();
    (void)key(VK_ESCAPE, 0);
    const bool altLeft = !_chrome->MenuKeyboardActive() && _chrome->CanvasFocused();
    (void)key(VK_F10, 0);
    const bool f10Pressed = _chrome->MenuKeyboardActive();
    (void)key(VK_ESCAPE, 0);
    {
        const auto guard = GuardMenuLoopForTest(_window.get());
        (void)SendMessageW(_window.get(), WM_SYSCOMMAND, SC_KEYMENU, 0);
    }
    const bool keyMenu = _chrome->MenuKeyboardActive();
    {
        const auto guard = GuardMenuLoopForTest(_window.get());
        (void)SendMessageW(_window.get(), WM_SYSCOMMAND, SC_KEYMENU, 0);
    }
    const bool keyMenuLeft = !_chrome->MenuKeyboardActive() && _chrome->CanvasFocused();
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

    // A press on the canvas takes the keyboard back from the menu bar a tapped Alt gave it (the canvas's keys work
    // again: B chooses the Brush), and so does the focus or the activation leaving the window.
    replace({});
    OnTool(EditorTool::Hand);
    _chrome->SetCanvasFocused(true);
    gMenuLoopGuardFired = false;
    (void)key(VK_MENU, kKeyAlt);
    const bool pressArmed = _chrome->MenuKeyboardActive();
    {
        const CanvasHole hole = _chrome->HolePixels();
        const LPARAM center = MAKELPARAM(static_cast<int>(hole.left + hole.width / 2.0f),
                                         static_cast<int>(hole.top + hole.height / 2.0f));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, center);
        OnPointer(WM_LBUTTONUP, 0, center);
    }
    const bool pressLeft = !_chrome->MenuKeyboardActive() && _chrome->CanvasFocused() &&
                           _chrome->FocusedControlNameForTest() != LoadStringResource(nullptr, IDS_A11Y_MENU_BAR);
    const bool brushKey = key('B', 0) && _tool == EditorTool::Brush;
    (void)key(VK_MENU, kKeyAlt);
    const bool killArmed = _chrome->MenuKeyboardActive();
    (void)SendMessageW(_window.get(), WM_KILLFOCUS, 0, 0);
    const bool killLeft = !_chrome->MenuKeyboardActive();
    (void)key(VK_MENU, kKeyAlt);
    const bool activateArmed = _chrome->MenuKeyboardActive();
    (void)SendMessageW(_window.get(), WM_ACTIVATEAPP, FALSE, 0);
    const bool activateLeft = !_chrome->MenuKeyboardActive();
    // A bar that lost DxUi's focus to nothing (DxUi prunes a hidden or disabled control) gives the keyboard back to
    // the canvas on the next key: B chooses the Brush.
    OnTool(EditorTool::Hand);
    _chrome->SetCanvasFocused(true);
    (void)key(VK_MENU, kKeyAlt);
    const bool dropArmed = _chrome->MenuKeyboardActive();
    _chrome->DropChromeFocusForTest();
    const bool dropBrush = key('B', 0) && _tool == EditorTool::Brush && _chrome->CanvasFocused();
    OnTool(EditorTool::Move);
    _chrome->SetCanvasFocused(true);
    if (gMenuLoopGuardFired || !pressArmed || !pressLeft || !brushKey || !killArmed || !killLeft || !activateArmed ||
        !activateLeft || !dropArmed || !dropBrush)
    {
        return fail(std::format(L"The menu bar kept the keyboard after a canvas press or a focus loss (loop {}, press "
                                L"{}/{}, B {}, WM_KILLFOCUS {}/{}, WM_ACTIVATEAPP {}/{}, focus dropped {}/{}).\n",
                                gMenuLoopGuardFired, pressArmed, pressLeft, brushKey, killArmed, killLeft,
                                activateArmed, activateLeft, dropArmed, dropBrush));
    }

    // Releasing Alt after an Alt chord a command took is no tap: Alt down, Backspace down and up (Fill with
    // Foreground), Alt up leaves the menu bar alone, and so does letting Alt go before Backspace. Each part is its
    // own message through the window procedure with the key state Windows would hold, as a keyboard sends them.
    const auto keyMessage = [this](UINT message, UINT virtualKey, std::initializer_list<UINT> held)
    {
        std::array<BYTE, 256> state{};
        for (const UINT down : held)
        {
            state[down & 0xFFu] = 0x80;
        }
        KeyboardState::Write(state);
        const bool system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        const LPARAM lParam = 1 | (static_cast<LPARAM>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC)) << 16) |
                              (system ? kKeyAltContext : 0) | (up ? kKeyUpFlags : 0);
        const auto guard = GuardMenuLoopForTest(_window.get());
        (void)SendMessageW(_window.get(), message, virtualKey, lParam);
    };
    {
        std::array<BYTE, 256> saved{};
        const bool restore = KeyboardState::Read(saved);
        OnCommand(ChromeHost::kCommandNewLayer);
        _session.ApplySelection(SelectionGeometry::Rect(12, 12, 20, 20), SelectionMode::New, L"Select");
        _chrome->SetCanvasFocused(true);
        gMenuLoopGuardFired = false;
        const int beforeFill = _session.history.UndoCount();
        keyMessage(WM_SYSKEYDOWN, VK_MENU, {VK_MENU, VK_LMENU});
        keyMessage(WM_SYSKEYDOWN, VK_BACK, {VK_MENU, VK_LMENU, VK_BACK});
        keyMessage(WM_SYSKEYUP, VK_BACK, {VK_MENU, VK_LMENU});
        keyMessage(WM_KEYUP, VK_MENU, {});
        const bool chordQuiet = !_chrome->MenuKeyboardActive();
        settle(); // retain the immediate Alt/menu assertion; observe pixels/history after worker acknowledgement
        const bool filled = _session.history.UndoCount() == beforeFill + 1;
        _chrome->LeaveMenuKeyboard();
        _chrome->SetCanvasFocused(true);
        keyMessage(WM_SYSKEYDOWN, VK_MENU, {VK_MENU, VK_LMENU});
        keyMessage(WM_SYSKEYDOWN, VK_BACK, {VK_MENU, VK_LMENU, VK_BACK});
        keyMessage(WM_KEYUP, VK_MENU, {VK_BACK});
        keyMessage(WM_KEYUP, VK_BACK, {});
        const bool altFirstQuiet = !_chrome->MenuKeyboardActive();
        settle();
        const bool filledAgain = _session.history.UndoCount() == beforeFill + 2;
        _chrome->LeaveMenuKeyboard();
        if (restore)
        {
            KeyboardState::Write(saved);
        }
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (gMenuLoopGuardFired || !filled || !chordQuiet || !filledAgain || !altFirstQuiet)
        {
            return fail(std::format(L"Releasing Alt after Alt+Backspace gave the menu bar the keyboard (loop {}, fill "
                                    L"{}/{}, Backspace released first {}, Alt released first {}).\n",
                                    gMenuLoopGuardFired, filled, filledAgain, chordQuiet, altFirstQuiet));
        }
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
    // Straight after the 4, with no frame between: a slow frame (ASan on a busy machine) must not stretch the pair
    // past the 0.6 s the two digits share.
    (void)HandleAccelerator('5', 0);
    const bool twoDigits = std::fabs(opacity() - 0.45) < 1e-9;
    // Holding a digit sets the opacity once: its auto-repeats (straight after it) neither make 55 % nor add entries.
    _session.ClearPendingOpacityDigit();
    (void)key('5', 0);
    const int heldEntries = _session.history.UndoCount();
    (void)HandleAccelerator('5', 0, true);
    (void)HandleAccelerator('5', 0, true);
    const bool heldOnce = std::fabs(opacity() - 0.5) < 1e-9 && _session.history.UndoCount() == heldEntries;
    OnTool(EditorTool::Brush);
    _chrome->SetCanvasFocused(true);
    _session.ClearPendingOpacityDigit();
    (void)key('3', 0);
    const bool brushOnly = std::fabs(opacity() - 0.5) < 1e-9 && std::fabs(_session.brushSettings.opacity - 0.3) < 1e-9;
    OnTool(EditorTool::Move);
    if (!fifty || !hundred || !undone || !twoDigits || !heldOnce || !brushOnly)
    {
        return fail(std::format(L"The Move tool's digit keys did not follow the two-digit rule (50 % {}, 100 % {}, "
                                L"undo {}, 45 % {}, held 5 once {}, brush {}).\n",
                                fifty, hundred, undone, twoDigits, heldOnce, brushOnly));
    }
    undoTo(historyStart);

    // RF-1109: with the layer opacity field focused (as a click on its value leaves it), a press on the canvas takes
    // the keys: the Move tool's 7 sets the layer to 70 % and types into no field, and with the Marquee (no digit keys)
    // a 3 types into nothing either.
    {
        const auto pressCanvas = [this]()
        {
            const CanvasHole hole = _chrome->HolePixels();
            const LPARAM center = MAKELPARAM(static_cast<int>(hole.left + hole.width / 2.0f),
                                             static_cast<int>(hole.top + hole.height / 2.0f));
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, center);
            OnPointer(WM_LBUTTONUP, 0, center);
        };
        OnTool(EditorTool::Move);
        OnCommand(ChromeHost::kCommandNewLayer);
        settle();
        _chrome->FocusLayerOpacityForTest();
        const bool fieldFocused = _chrome->TextInputFocused();
        pressCanvas();
        _session.ClearPendingOpacityDigit();
        const bool moveDigit = key('7', 0) && std::fabs(opacity() - 0.7) < 1e-9;
        OnTool(EditorTool::Marquee);
        settle();
        _chrome->FocusLayerOpacityForTest();
        const std::wstring fieldShown = _chrome->FocusedTextForTest();
        pressCanvas();
        (void)key('3', 0);
        const std::wstring fieldAfter = _chrome->FocusedTextForTest();
        const bool marqueeDigit = fieldAfter == fieldShown && std::fabs(opacity() - 0.7) < 1e-9;
        OnTool(EditorTool::Move);
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (!fieldFocused || !moveDigit || !marqueeDigit)
        {
            return fail(std::format(L"A digit after a canvas press reached the focused opacity field (field focused "
                                    L"{}, Move 7 is 70 % {}, field '{}' then '{}').\n",
                                    fieldFocused, moveDigit, fieldShown, fieldAfter));
        }
    }
    // RF-1109: while a panel owns the document (an adjustment layer's Levels), the layer opacity waits, as legacy
    // canEditAppearance: neither the Move tool's 5 nor the layers panel's stepper changes it, so nothing folds into
    // the panel's entry and its Cancel leaves the opacity as it was.
    {
        OnTool(EditorTool::Move);
        OnCommand(ChromeHost::kCommandNewLevelsAdjustment);
        settle(); // harness observes the acknowledged asynchronous opening
        _chrome->SetCanvasFocused(true);
        const bool panelOpen = _session.HasColorEdit();
        const double start = opacity();
        _session.ClearPendingOpacityDigit();
        (void)key('5', 0);
        const bool digitWaits = std::fabs(opacity() - start) < 1e-9;
        const bool stepperWaits = !_chrome->RequestLayerOpacity(40.0) && std::fabs(opacity() - start) < 1e-9;
        OnLayerOpacity(0.4, ChromeEditPhase::Preview); // a typed value that reached the host all the same
        const bool previewWaits = std::fabs(opacity() - start) < 1e-9;
        OnLayerOpacity(0.4, ChromeEditPhase::Cancel);
        CancelColorEditForTest();
        const bool cancelKeeps = !_session.HasColorEdit() && std::fabs(opacity() - start) < 1e-9;
        undoTo(historyStart);
        _chrome->SetCanvasFocused(true);
        if (!panelOpen || !digitWaits || !stepperWaits || !previewWaits || !cancelKeeps)
        {
            return fail(std::format(L"The layer opacity changed under an open adjustment panel (panel {}, digit {}, "
                                    L"stepper {}, typed preview {}, after Cancel {}).\n",
                                    panelOpen, digitWaits, stepperWaits, previewWaits, cancelKeeps));
        }
    }

    // A key message without a scan code (a macro pad, SendInput with a virtual key only) still names its position:
    // the virtual key the layout gives the right bracket key grows the brush, and with Ctrl the number-row plus key
    // zooms in.
    {
        const auto scanless = [this](UINT virtualKey, bool ctrl)
        {
            std::array<BYTE, 256> saved{};
            const bool restore = KeyboardState::Read(saved);
            std::array<BYTE, 256> state{};
            state[VK_CONTROL] = ctrl ? 0x80 : 0;
            state[VK_LCONTROL] = ctrl ? 0x80 : 0;
            state[virtualKey & 0xFFu] = 0x80;
            KeyboardState::Write(state);
            _lastKeyHandled = false;
            (void)SendMessageW(_window.get(), WM_KEYDOWN, virtualKey, 1);
            const bool handled = _lastKeyHandled;
            state[virtualKey & 0xFFu] = 0;
            KeyboardState::Write(state);
            (void)SendMessageW(_window.get(), WM_KEYUP, virtualKey, 1 | kKeyUpFlags);
            if (restore)
            {
                KeyboardState::Write(saved);
            }
            return handled;
        };
        OnTool(EditorTool::Brush);
        _chrome->SetCanvasFocused(true);
        settle();
        const double size = _session.brushSettings.diameter;
        const bool larger =
            scanless(MapVirtualKeyW(KeyPositionScanCode(KeyPosition::BracketRight), MAPVK_VSC_TO_VK), false) &&
            _session.brushSettings.diameter > size;
        _session.brushSettings.diameter = size;
        OnTool(EditorTool::Move);
        _chrome->SetCanvasFocused(true);
        settle();
        const bool zoomed =
            scanless(MapVirtualKeyW(KeyPositionScanCode(KeyPosition::NumberRowPlus), MAPVK_VSC_TO_VK), true) &&
            _viewport.zoom > zoom;
        restoreZoom();
        if (!larger || !zoomed)
        {
            return fail(std::format(L"A key message without a scan code lost its position (bracket {}, Ctrl with the "
                                    L"number-row plus key {}).\n",
                                    larger, zoomed));
        }
    }

    // The message loop runs a key down before it translates it (DispatchQueuedMessage): a key a command took posts
    // no character, so a dialog the command opens never receives one and no flag is left to drop a later one; where
    // the left bracket key is a dead key (French ^), Brush Smaller arms no accent for the next text; a key nothing
    // took still types. Posted messages go through the loop's own step.
    {
        const auto queueKey = [this](UINT message, UINT virtualKey)
        {
            const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC)) << 16;
            (void)PostMessageW(_window.get(), message, virtualKey, 1 | scan | (message == WM_KEYUP ? kKeyUpFlags : 0));
        };
        const auto dispatchNext = [this]()
        {
            MSG message{};
            if (PeekMessageW(&message, _window.get(), WM_KEYFIRST, WM_KEYLAST, PM_REMOVE) == FALSE)
            {
                return false;
            }
            DispatchQueuedMessage(message);
            return true;
        };
        const auto characterQueued = [this]()
        {
            MSG message{};
            return PeekMessageW(&message, _window.get(), WM_CHAR, WM_SYSDEADCHAR, PM_NOREMOVE) != FALSE;
        };
        const auto drain = [&dispatchNext]()
        {
            while (dispatchNext())
            {
            }
        };
        const std::array<BYTE, 256> none{};
        const HKL layout = GetKeyboardLayout(0);
        KeyboardState::Write(none);
        OnTool(EditorTool::Move);
        _chrome->SetCanvasFocused(true);
        settle();
        const std::uint32_t unswapped = _session.ForegroundColor();
        queueKey(WM_KEYDOWN, 'X');
        (void)dispatchNext();
        const bool commandTyped = characterQueued();
        queueKey(WM_KEYUP, 'X');
        drain();
        const bool swapped = _session.ForegroundColor() != unswapped && !_suppressCharacter;
        OnCommand(ChromeHost::kCommandSwapPalette);
        // A dead key's accent would join the next letter: E would type ê instead of e.
        const UINT bracket = MapVirtualKeyW(KeyPositionScanCode(KeyPosition::BracketLeft), MAPVK_VSC_TO_VK);
        std::array<wchar_t, 4> probe{};
        bool deadQuiet = true;
        if (ToUnicodeEx(bracket, KeyPositionScanCode(KeyPosition::BracketLeft), none.data(), probe.data(),
                        static_cast<int>(probe.size()), 1u << 2, layout) < 0)
        {
            OnTool(EditorTool::Brush);
            _chrome->SetCanvasFocused(true);
            settle();
            const double size = _session.brushSettings.diameter;
            queueKey(WM_KEYDOWN, bracket);
            (void)dispatchNext();
            const bool deadQueued = characterQueued();
            queueKey(WM_KEYUP, bracket);
            drain();
            const int count = ToUnicodeEx('E', MapVirtualKeyW('E', MAPVK_VK_TO_VSC), none.data(), probe.data(),
                                          static_cast<int>(probe.size()), 1u << 2, layout);
            const bool accented = count != 1 || probe[0] != L'e';
            if (accented)
            {
                // Consume the armed accent so the steps after this one type plainly.
                (void)ToUnicodeEx(VK_SPACE, MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC), none.data(), probe.data(),
                                  static_cast<int>(probe.size()), 0, layout);
            }
            deadQuiet = _session.brushSettings.diameter < size && !deadQueued && !accented;
            _session.brushSettings.diameter = size;
            OnTool(EditorTool::Move);
            _chrome->SetCanvasFocused(true);
        }
        // A key nothing took (Q on the canvas) is still translated. Which character TranslateMessage makes depends on
        // the thread's key state, which a key held elsewhere reaches (RF-1110), so only its arrival is checked.
        settle();
        queueKey(WM_KEYDOWN, 'Q');
        (void)dispatchNext();
        const bool plainTranslated = characterQueued();
        queueKey(WM_KEYUP, 'Q');
        drain();
        if (commandTyped || !swapped || !deadQuiet || !plainTranslated)
        {
            return fail(
                std::format(L"The message loop translated a key a command took, or not a key nothing took "
                            L"(character after X {}, swapped {}, dead bracket quiet {}, character after Q {}).\n",
                            commandTyped, swapped, deadQuiet, plainTranslated));
        }
    }

    // Every key the settings grammar names shows as text (a named key, a function key, a VK_XX key), numpad plus and
    // minus are not the number row's keys, and a replacement on a named key shows in the Command List.
    for (const KeyChord chord : {Key(VK_F5), Ctrl(Key(VK_F5)), Key(VK_SPACE), Key(VK_HOME), Key(VK_RETURN), Key(0xE7)})
    {
        if (ChromeHost::ChordDisplayText(chord).empty())
        {
            return fail(std::format(L"The chord {} shows no text.\n", CanonicalChordText(chord)));
        }
    }
    // A key the layout has no name for (VK_E7) shows the table's name for it, never an English one from code.
    if (ChromeHost::ChordDisplayText(Key(0xE7)) != FormatStringResource(nullptr, IDS_KEY_VIRTUAL, 0xE7u))
    {
        return fail(
            std::format(L"A key without a layout name showed '{}'.\n", ChromeHost::ChordDisplayText(Key(0xE7))));
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
    const std::wstring zoomExpected =
        join(ctrlName, plusName.empty() ? LoadStringResource(nullptr, IDS_KEY_NUMBER_ROW_PLUS) : plusName);
    const std::wstring layerExpected =
        join(ctrlName, bracketName.empty() ? LoadStringResource(nullptr, IDS_KEY_BRACKET_LEFT) : bracketName);
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

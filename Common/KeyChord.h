#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// A keyboard chord (Specs/Core/Core_Settings.md "Command chords", Specs/UI/UI_ApplicationChrome.md "Menus and
// accelerators"): Ctrl, Alt and Shift plus one key, named either by its Windows virtual key (letters, digits and
// named keys follow the key the active layout gives that meaning) or by its physical position (a set-1 scan code
// that stays on the same key when the layout changes). Pure data: no windows.h, so any test can include it first.

// The physical keys a chord can name by position. The numbers are the settings file's `keyPosition` order.
enum class KeyPosition : std::uint8_t
{
    None = 0,
    NumberRowMinus = 1, // scan 0x0C, the key immediately right of 0 (US - and _)
    NumberRowPlus = 2,  // scan 0x0D, the key immediately left of Backspace (US = and +)
    BracketLeft = 3,    // scan 0x1A (US [)
    BracketRight = 4,   // scan 0x1B (US ])
};

// The Windows virtual-key codes the catalog and the settings grammar name (winuser.h values).
namespace VirtualKey
{
inline constexpr std::uint16_t kBack = 0x08;
inline constexpr std::uint16_t kTab = 0x09;
inline constexpr std::uint16_t kReturn = 0x0D;
inline constexpr std::uint16_t kShift = 0x10;
inline constexpr std::uint16_t kControl = 0x11;
inline constexpr std::uint16_t kMenu = 0x12;
inline constexpr std::uint16_t kEscape = 0x1B;
inline constexpr std::uint16_t kSpace = 0x20;
inline constexpr std::uint16_t kPrior = 0x21;
inline constexpr std::uint16_t kNext = 0x22;
inline constexpr std::uint16_t kEnd = 0x23;
inline constexpr std::uint16_t kHome = 0x24;
inline constexpr std::uint16_t kLeft = 0x25;
inline constexpr std::uint16_t kUp = 0x26;
inline constexpr std::uint16_t kRight = 0x27;
inline constexpr std::uint16_t kDown = 0x28;
inline constexpr std::uint16_t kInsert = 0x2D;
inline constexpr std::uint16_t kDelete = 0x2E;
inline constexpr std::uint16_t kLeftWindows = 0x5B;
inline constexpr std::uint16_t kRightWindows = 0x5C;
inline constexpr std::uint16_t kAdd = 0x6B;
inline constexpr std::uint16_t kSubtract = 0x6D;
inline constexpr std::uint16_t kF1 = 0x70;
inline constexpr std::uint16_t kF2 = 0x71;
inline constexpr std::uint16_t kF4 = 0x73;
inline constexpr std::uint16_t kF10 = 0x79;
inline constexpr std::uint16_t kF24 = 0x87;
inline constexpr std::uint16_t kLeftShift = 0xA0;
inline constexpr std::uint16_t kRightMenu = 0xA5;
} // namespace VirtualKey

struct KeyChord
{
    std::uint16_t virtualKey = 0;             // the key's virtual key; 0 when `position` names the key
    KeyPosition position = KeyPosition::None; // the key's physical position; None when `virtualKey` names it
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        return virtualKey == 0 && position == KeyPosition::None;
    }
    [[nodiscard]] constexpr bool operator==(const KeyChord&) const noexcept = default;
};

// Catalog shorthands: Key('S'), Ctrl(Shift(Key('S'))), At(KeyPosition::BracketLeft).
[[nodiscard]] constexpr KeyChord Key(std::uint16_t virtualKey) noexcept
{
    KeyChord chord;
    chord.virtualKey = virtualKey;
    return chord;
}
[[nodiscard]] constexpr KeyChord At(KeyPosition position) noexcept
{
    KeyChord chord;
    chord.position = position;
    return chord;
}
[[nodiscard]] constexpr KeyChord Ctrl(KeyChord chord) noexcept
{
    chord.ctrl = true;
    return chord;
}
[[nodiscard]] constexpr KeyChord Alt(KeyChord chord) noexcept
{
    chord.alt = true;
    return chord;
}
[[nodiscard]] constexpr KeyChord Shift(KeyChord chord) noexcept
{
    chord.shift = true;
    return chord;
}

// The set-1 scan code of a position key; 0 for None.
[[nodiscard]] constexpr unsigned KeyPositionScanCode(KeyPosition position) noexcept
{
    switch (position)
    {
    case KeyPosition::NumberRowMinus:
        return 0x0C;
    case KeyPosition::NumberRowPlus:
        return 0x0D;
    case KeyPosition::BracketLeft:
        return 0x1A;
    case KeyPosition::BracketRight:
        return 0x1B;
    case KeyPosition::None:
        break;
    }
    return 0;
}

// The position a key message's scan code names (bits 16–23 of its lParam, bit 24 the extended flag).
[[nodiscard]] constexpr KeyPosition KeyPositionFromScanCode(unsigned scanCode, bool extended) noexcept
{
    if (extended)
    {
        return KeyPosition::None;
    }
    switch (scanCode & 0xFFu)
    {
    case 0x0C:
        return KeyPosition::NumberRowMinus;
    case 0x0D:
        return KeyPosition::NumberRowPlus;
    case 0x1A:
        return KeyPosition::BracketLeft;
    case 0x1B:
        return KeyPosition::BracketRight;
    default:
        return KeyPosition::None;
    }
}

// The navigation cluster's keys share scan codes with the numpad (Delete is 0x53, as the numpad's decimal key) and
// differ by the extended flag, which MapVirtualKeyW does not always report: a key name or a key message for one of
// them sets it.
[[nodiscard]] constexpr bool IsExtendedNavigationKey(std::uint16_t virtualKey) noexcept
{
    return (virtualKey >= VirtualKey::kPrior && virtualKey <= VirtualKey::kDown) || virtualKey == VirtualKey::kInsert ||
           virtualKey == VirtualKey::kDelete;
}

// Modifier keys (Shift, Ctrl, Alt and their sides, the Windows keys) never name a chord's key.
[[nodiscard]] constexpr bool IsModifierVirtualKey(std::uint16_t virtualKey) noexcept
{
    return (virtualKey >= VirtualKey::kShift && virtualKey <= VirtualKey::kMenu) ||
           (virtualKey >= VirtualKey::kLeftShift && virtualKey <= VirtualKey::kRightMenu) ||
           virtualKey == VirtualKey::kLeftWindows || virtualKey == VirtualKey::kRightWindows;
}

namespace KeyChordNames
{
struct NamedKey
{
    std::string_view name;
    std::uint16_t virtualKey;
};
// RedSalamander's stable names. The first spelling of a key is the one the store writes.
inline constexpr std::array<NamedKey, 18> kNamedKeys{{
    {"Backspace", VirtualKey::kBack},
    {"Tab", VirtualKey::kTab},
    {"Enter", VirtualKey::kReturn},
    {"Return", VirtualKey::kReturn},
    {"Space", VirtualKey::kSpace},
    {"PageUp", VirtualKey::kPrior},
    {"PageDown", VirtualKey::kNext},
    {"End", VirtualKey::kEnd},
    {"Home", VirtualKey::kHome},
    {"Left", VirtualKey::kLeft},
    {"Up", VirtualKey::kUp},
    {"Right", VirtualKey::kRight},
    {"Down", VirtualKey::kDown},
    {"Insert", VirtualKey::kInsert},
    {"Delete", VirtualKey::kDelete},
    {"Escape", VirtualKey::kEscape},
    {"NumpadPlus", VirtualKey::kAdd},
    {"NumpadMinus", VirtualKey::kSubtract},
}};
inline constexpr std::array<std::string_view, 5> kPositionNames{"", "numberRowMinus", "numberRowPlus", "bracketLeft",
                                                                "bracketRight"};

[[nodiscard]] constexpr char AsciiUpper(char character) noexcept
{
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
}

[[nodiscard]] constexpr bool EqualsIgnoringCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (AsciiUpper(left[index]) != AsciiUpper(right[index]))
        {
            return false;
        }
    }
    return true;
}
} // namespace KeyChordNames

// The settings file's `vk` spelling: a letter or digit, F1–F24, a named key, or VK_XX (two hex digits).
[[nodiscard]] inline std::string StableKeyName(std::uint16_t virtualKey)
{
    const unsigned key = virtualKey & 0xFFu;
    if (key >= VirtualKey::kF1 && key <= VirtualKey::kF24)
    {
        return std::format("F{}", key - VirtualKey::kF1 + 1u);
    }
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
    {
        return std::string(1, static_cast<char>(key));
    }
    for (const KeyChordNames::NamedKey& named : KeyChordNames::kNamedKeys)
    {
        if (named.virtualKey == key)
        {
            return std::string{named.name};
        }
    }
    return std::format("VK_{:02X}", key);
}

// Parses a `vk` spelling, ignoring case ("delete", "f5", "vk_e7" and "q" are all keys). False for anything else.
[[nodiscard]] inline bool ParseStableKeyName(std::string_view text, std::uint16_t& virtualKey) noexcept
{
    using KeyChordNames::AsciiUpper;
    if (text.empty())
    {
        return false;
    }
    if (text.size() == 1)
    {
        const char character = AsciiUpper(text[0]);
        if ((character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z'))
        {
            virtualKey = static_cast<std::uint16_t>(static_cast<unsigned char>(character));
            return true;
        }
        return false;
    }
    if (AsciiUpper(text[0]) == 'F' && text.size() <= 3)
    {
        unsigned number = 0;
        for (std::size_t index = 1; index < text.size(); ++index)
        {
            if (text[index] < '0' || text[index] > '9')
            {
                number = 0;
                break;
            }
            number = number * 10u + static_cast<unsigned>(text[index] - '0');
        }
        if (number >= 1 && number <= 24)
        {
            virtualKey = static_cast<std::uint16_t>(VirtualKey::kF1 + number - 1u);
            return true;
        }
    }
    if (text.size() == 5 && AsciiUpper(text[0]) == 'V' && AsciiUpper(text[1]) == 'K' && text[2] == '_')
    {
        unsigned value = 0;
        for (std::size_t index = 3; index < 5; ++index)
        {
            const char digit = AsciiUpper(text[index]);
            const bool decimal = digit >= '0' && digit <= '9';
            const bool hex = digit >= 'A' && digit <= 'F';
            if (!decimal && !hex)
            {
                return false;
            }
            value = value * 16u + static_cast<unsigned>(decimal ? digit - '0' : digit - 'A' + 10);
        }
        virtualKey = static_cast<std::uint16_t>(value);
        return true;
    }
    for (const KeyChordNames::NamedKey& named : KeyChordNames::kNamedKeys)
    {
        if (KeyChordNames::EqualsIgnoringCase(text, named.name))
        {
            virtualKey = named.virtualKey;
            return true;
        }
    }
    return false;
}

// The settings file's `keyPosition` spelling ("numberRowPlus"); empty for None.
[[nodiscard]] inline std::string_view KeyPositionName(KeyPosition position) noexcept
{
    const std::size_t index = static_cast<std::size_t>(position);
    return index < KeyChordNames::kPositionNames.size() ? KeyChordNames::kPositionNames[index] : std::string_view{};
}

// Parses a `keyPosition` spelling, ignoring case; None for anything else.
[[nodiscard]] inline KeyPosition ParseKeyPositionName(std::string_view text) noexcept
{
    for (std::size_t index = 1; index < KeyChordNames::kPositionNames.size(); ++index)
    {
        if (KeyChordNames::EqualsIgnoringCase(text, KeyChordNames::kPositionNames[index]))
        {
            return static_cast<KeyPosition>(index);
        }
    }
    return KeyPosition::None;
}

// The canonical English label of a chord: the STRINGTABLE's accelerator column ("Ctrl+Shift+S", "Ctrl++",
// "Shift+=", "Alt+F4"). Modifiers in the order Ctrl, Alt, Shift. The number-row plus key reads "+" with Ctrl
// and "=" without (the menus say Ctrl++ for Zoom In and Shift+= for Next Blend Mode). The chrome shows a chord
// with the layout's key names instead (ChromeHost).
[[nodiscard]] inline std::wstring CanonicalChordText(const KeyChord& chord)
{
    if (chord.IsEmpty())
    {
        return {};
    }
    std::wstring text;
    if (chord.ctrl)
    {
        text += L"Ctrl+";
    }
    if (chord.alt)
    {
        text += L"Alt+";
    }
    if (chord.shift)
    {
        text += L"Shift+";
    }
    switch (chord.position)
    {
    case KeyPosition::NumberRowMinus:
        return text + L"-";
    case KeyPosition::NumberRowPlus:
        return text + (chord.ctrl ? L"+" : L"=");
    case KeyPosition::BracketLeft:
        return text + L"[";
    case KeyPosition::BracketRight:
        return text + L"]";
    case KeyPosition::None:
        break;
    }
    const std::string name = StableKeyName(chord.virtualKey);
    text.append(name.begin(), name.end());
    return text;
}

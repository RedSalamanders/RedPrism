#pragma once

#include "CommandCatalog.h"
#include "SettingsStore.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

// The effective binding table (Specs/Core/Core_Settings.md "Command chords", Specs/UI/UI_ApplicationChrome.md
// "Menus and accelerators"): the settings file's replacement chords, then the catalog defaults of every command
// they do not replace. The key path dispatches from it, and the menus, the Command List, tooltips and the status
// strip show its chords, so what a key does and what the chrome says it does cannot drift apart. Built once per
// settings change; matching a key walks the table without allocating.

// Where the keyboard focus is when a key arrives.
enum class KeyFocus : std::uint8_t
{
    Canvas,    // the canvas hole
    Chrome,    // a chrome control that is not a text input (a button, the layers tree, the menu bar)
    TextInput, // a chrome text input (the Rename card, the Command List search, a stepper being typed in)
};

// The session state the commands' conditions read.
struct KeyState
{
    KeyFocus focus = KeyFocus::Canvas;
    EditorTool tool = EditorTool::Move;
    bool selection = false;  // a non-empty selection
    bool lassoDraft = false; // a lasso outline in progress
};

// One key down as the window procedure sees it.
struct KeyPress
{
    std::uint16_t virtualKey = 0;
    KeyPosition position = KeyPosition::None; // the physical key its set-1 scan code names, if a position key
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool repeat = false;         // auto-repeat (lParam bit 30)
    bool typesCharacter = false; // with Ctrl and Alt held (AltGr): the active layout types a character for the key
};

struct CommandBinding
{
    int command = 0;
    KeyChord chord;
    DefaultChordKind kind = DefaultChordKind::Dispatch;
    bool replacement = false; // from the settings file
    const CommandCatalogEntry* entry = nullptr;
};

enum class KeyMatchResult : std::uint8_t
{
    None,
    Command, // run `command`
    HeldKey, // the key is an auto-repeat of a command that runs once per press: take it, run nothing
};

struct KeyMatch
{
    KeyMatchResult result = KeyMatchResult::None;
    int command = 0;
};

// Window-manager chords no binding may take (Decision D5): Alt+F4 closes the window and Alt+Space opens its system
// menu whatever the settings say; F10 (and Shift+F10) belong to the menu bar; a modifier key is never a chord's key.
[[nodiscard]] constexpr bool IsReservedChord(const KeyChord& chord) noexcept
{
    if (chord.position != KeyPosition::None)
    {
        return false;
    }
    const std::uint16_t key = chord.virtualKey;
    return (key == VirtualKey::kF4 && chord.alt && !chord.ctrl) ||
           (key == VirtualKey::kSpace && chord.alt && !chord.ctrl) ||
           (key == VirtualKey::kF10 && !chord.ctrl && !chord.alt) || IsModifierVirtualKey(key);
}

// Whether `chord` names `press`: the same key (virtual key, or position from the scan code), the same Ctrl and the
// same Alt; the same Shift, or, `shiftAdded`, a chord without Shift pressed with Shift.
[[nodiscard]] constexpr bool ChordMatches(const KeyChord& chord, const KeyPress& press, bool shiftAdded) noexcept
{
    const bool sameKey = chord.position != KeyPosition::None
                             ? chord.position == press.position
                             : chord.virtualKey != 0 && chord.virtualKey == press.virtualKey;
    if (!sameKey || chord.ctrl != press.ctrl || chord.alt != press.alt)
    {
        return false;
    }
    return shiftAdded ? !chord.shift && press.shift : chord.shift == press.shift;
}

// Whether a command of `scope` takes `chord` with the focus where it is (Decision D4: a text input keeps every key
// but a global command's chord with Ctrl, Alt or a function key).
[[nodiscard]] constexpr bool ScopeAllows(CommandScope scope, const KeyChord& chord, KeyFocus focus) noexcept
{
    switch (scope)
    {
    case CommandScope::Canvas:
        return focus == KeyFocus::Canvas;
    case CommandScope::App:
        return focus != KeyFocus::TextInput;
    case CommandScope::Global:
        return focus != KeyFocus::TextInput || chord.ctrl || chord.alt ||
               (chord.position == KeyPosition::None && chord.virtualKey >= VirtualKey::kF1 &&
                chord.virtualKey <= VirtualKey::kF24);
    }
    return false;
}

[[nodiscard]] constexpr bool ConditionHolds(CommandCondition condition, const KeyState& state) noexcept
{
    switch (condition)
    {
    case CommandCondition::None:
        return true;
    case CommandCondition::PixelsSelected:
        return state.selection && !state.lassoDraft;
    case CommandCondition::SelectionOnOutline:
        return EditorToolIsSelection(state.tool) && state.selection && !state.lassoDraft;
    case CommandCondition::MoveTool:
        return state.tool == EditorTool::Move;
    case CommandCondition::BrushTool:
        return EditorToolIsBrush(state.tool);
    case CommandCondition::NoLassoDraft:
        return !state.lassoDraft;
    case CommandCondition::MarqueeTool:
        return state.tool == EditorTool::Marquee;
    case CommandCondition::LassoTool:
        return state.tool == EditorTool::Lasso;
    case CommandCondition::ShapeTool:
        return state.tool == EditorTool::Shape;
    }
    return false;
}

class CommandBindings final
{
  public:
    // The catalog defaults alone.
    CommandBindings()
    {
        Build({});
    }

    // One shared table of the catalog defaults, for chrome that has not been given the application's table.
    [[nodiscard]] static const CommandBindings& Defaults()
    {
        static const CommandBindings defaults;
        return defaults;
    }

    // Rebuilds the table from the settings file's replacements, in file order:
    // - an entry whose commandId names no command, whose key is empty, or whose chord is reserved is ignored;
    // - a later entry for a command replaces an earlier one, and a later entry takes a chord from an earlier entry
    //   of another command (that command is then left without a chord);
    // - a replaced command loses every default chord, aliases included (Ctrl+Y for Redo, the numpad plus for Zoom
    //   In), and a chord a replacement uses is taken from every other command's defaults;
    // - a shared label (Delete Layer's Delete) stays only while a command still dispatches that chord.
    void Build(std::span<const ShortcutBinding> replacements)
    {
        _bindings.clear();
        std::vector<int> replaced;
        std::vector<CommandBinding> chosen;
        for (auto binding = replacements.rbegin(); binding != replacements.rend(); ++binding)
        {
            const int command = CommandForTextId(binding->commandId);
            if (command == 0 || binding->chord.IsEmpty() || IsReservedChord(binding->chord) ||
                std::find(replaced.begin(), replaced.end(), command) != replaced.end())
            {
                continue;
            }
            replaced.push_back(command);
            const bool taken = std::any_of(chosen.begin(), chosen.end(),
                                           [&](const CommandBinding& later) { return later.chord == binding->chord; });
            if (!taken)
            {
                chosen.push_back(
                    CommandBinding{command, binding->chord, DefaultChordKind::Dispatch, true, FindCommand(command)});
            }
        }
        _bindings.assign(chosen.rbegin(), chosen.rend());
        for (const DefaultChord& fallback : DefaultChords())
        {
            const bool commandReplaced =
                std::find(replaced.begin(), replaced.end(), fallback.command) != replaced.end();
            const bool chordTaken = std::any_of(chosen.begin(), chosen.end(), [&](const CommandBinding& replacement)
                                                { return replacement.chord == fallback.chord; });
            if (!commandReplaced && !chordTaken)
            {
                _bindings.push_back(CommandBinding{fallback.command, fallback.chord, fallback.kind, false,
                                                   FindCommand(fallback.command)});
            }
        }
        std::vector<KeyChord> dispatched;
        for (const CommandBinding& binding : _bindings)
        {
            if (binding.kind == DefaultChordKind::Dispatch)
            {
                dispatched.push_back(binding.chord);
            }
        }
        std::erase_if(_bindings,
                      [&](const CommandBinding& label)
                      {
                          return label.kind == DefaultChordKind::SharedLabel &&
                                 std::find(dispatched.begin(), dispatched.end(), label.chord) == dispatched.end();
                      });
    }

    // The command a key runs, if any. A chord matches with the same modifiers first; a command that allows Shift
    // then also takes its chord with Shift added. With Ctrl and Alt held, a key the layout types a character for
    // (AltGr) matches nothing: it is text, not a chord.
    [[nodiscard]] KeyMatch Match(const KeyPress& press, const KeyState& state) const noexcept
    {
        if (press.ctrl && press.alt && press.typesCharacter)
        {
            return {};
        }
        for (const bool shiftAdded : {false, true})
        {
            if (shiftAdded && !press.shift)
            {
                break;
            }
            for (const CommandBinding& binding : _bindings)
            {
                if (binding.kind != DefaultChordKind::Dispatch || binding.entry == nullptr ||
                    !ChordMatches(binding.chord, press, shiftAdded) || (shiftAdded && !binding.entry->shiftAllowed) ||
                    !ScopeAllows(binding.entry->scope, binding.chord, state.focus) ||
                    !ConditionHolds(binding.entry->condition, state))
                {
                    continue;
                }
                if (press.repeat && !binding.entry->repeats)
                {
                    return KeyMatch{KeyMatchResult::HeldKey, binding.command};
                }
                return KeyMatch{KeyMatchResult::Command, binding.command};
            }
        }
        return {};
    }

    // The chord the chrome shows for `command`: its replacement, else its first default; nullopt without one.
    [[nodiscard]] std::optional<KeyChord> ChordFor(int command) const noexcept
    {
        for (const CommandBinding& binding : _bindings)
        {
            if (binding.command == command)
            {
                return binding.chord;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::span<const CommandBinding> Bindings() const noexcept
    {
        return _bindings;
    }

  private:
    std::vector<CommandBinding> _bindings;
};

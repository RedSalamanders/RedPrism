#pragma once

#include "HostPolygonalInputJournal.h"
#include "HostSelectionPressInput.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>

// One original parent's scalar borrowing view. Ending this borrow never ends a Polygonal journal family:
// an inside Up can precede accepted younger Down records which belong to later ordered parents.
// The owner keeps the journal alive through every borrowing parent's completion and joined retirement.
class HostSelectionGestureInput final
{
  public:
    using Context = HostSelectionPressInput::Context;
    using Modifiers = HostSelectionPressInput::Modifiers;
    enum class Action : std::uint8_t
    {
        Down,
        Move,
        Up,
        Lost,
        Leave,
        Backspace,
        Enter,
        Escape,
    };
    using Sample = HostPolygonalInputJournal::Sample;
    struct Event final
    {
        Action action;
        Sample sample;
        bool emergency = false;
    };
    explicit HostSelectionGestureInput(HostSelectionPressInput& press) noexcept : _press(&press) {}
    HostSelectionGestureInput(HostPolygonalInputJournal& journal, std::uint64_t opening) noexcept
        : _journal(&journal), _opening(opening)
    {
        const auto down = journal.Peek(opening);
        if (down && down->action == HostPolygonalInputJournal::Action::Down && down->down)
            _captured = down->down;
    }
    HostSelectionGestureInput(const HostSelectionGestureInput&) = delete;
    HostSelectionGestureInput& operator=(const HostSelectionGestureInput&) = delete;
    HostSelectionGestureInput(HostSelectionGestureInput&&) = delete;
    HostSelectionGestureInput& operator=(HostSelectionGestureInput&&) = delete;

    [[nodiscard]] const Context* CapturedContext(std::uint64_t opening) const noexcept
    {
        if (_press)
            return _press->CapturedContext(opening);
        return Current(opening) && _captured ? &_captured.value() : nullptr;
    }
    [[nodiscard]] std::optional<Event> Peek(std::uint64_t opening) const noexcept
    {
        if (_press)
        {
            const auto event = _press->Peek(opening);
            return event ? std::optional{Convert(event.value())} : std::nullopt;
        }
        if (!Current(opening) || !_captured || _terminal)
            return std::nullopt;
        const auto event = _journal->Peek(opening);
        return event ? std::optional{Convert(event.value())} : std::nullopt;
    }
    [[nodiscard]] std::optional<Event> Take(std::uint64_t opening) noexcept
    {
        if (_press)
        {
            const auto event = _press->Take(opening);
            if (event)
                _lastTakenOrdinal = event->sample.ordinal;
            return event ? std::optional{Convert(event.value())} : std::nullopt;
        }
        if (!Current(opening) || !_captured || _terminal)
            return std::nullopt;
        const auto event = _journal->Take(opening);
        if (!event)
            return std::nullopt;
        _lastTakenOrdinal = event->sample.ordinal;
        return Convert(event.value());
    }
    // The branch selects its own terminal AFTER consuming the exact physical record. A later journal
    // loss must not retroactively turn an already selected inside Up into a cancelled original parent.
    [[nodiscard]] bool SelectTerminal(std::uint64_t opening, std::uint64_t ordinal) noexcept
    {
        if (!CapturedContext(opening) || _terminal || !ordinal || ordinal != _lastTakenOrdinal)
            return false;
        _terminal = true;
        return true;
    }
    [[nodiscard]] bool Acknowledge(std::uint64_t opening) noexcept
    {
        if (_press)
            return _terminal && _press->Acknowledge(opening);
        if (!Current(opening) || !_captured || !_terminal)
            return false;
        _captured.reset();
        return true; // No journal CloseInput/Finish/Take: accepted younger gestures remain untouched.
    }
    [[nodiscard]] bool Drop(std::uint64_t opening) noexcept
    {
        if (_press)
            return _press->Drop(opening);
        if (!Current(opening) || !_captured)
            return false;
        _captured.reset();
        return true; // Host dispatcher separately retires this failed parent's exact conditional receipts.
    }
    [[nodiscard]] std::size_t PendingCount() const noexcept
    {
        if (_press)
            return _press->PendingCount();
        return Current(_opening) && _captured && !_terminal ? _journal->PendingCount() : 0;
    }
    [[nodiscard]] bool Overflowed() const noexcept
    {
        return _press ? _press->Overflowed() : Current(_opening) && _journal->Overflowed();
    }
    [[nodiscard]] const HostPolygonalInputJournal* Family() const noexcept
    {
        return _journal;
    }
    [[nodiscard]] bool IsJournal() const noexcept
    {
        return _journal != nullptr;
    }

  private:
    [[nodiscard]] bool Current(std::uint64_t opening) const noexcept
    {
        return _journal && opening == _opening && opening && _journal->Stream() == opening;
    }
    [[nodiscard]] static Event Convert(const HostSelectionPressInput::Event& event) noexcept
    {
        const auto& sample = event.sample;
        Action action;
        switch (event.action)
        {
        case HostSelectionPressInput::Action::Down:
            action = Action::Down;
            break;
        case HostSelectionPressInput::Action::Move:
            action = Action::Move;
            break;
        case HostSelectionPressInput::Action::Up:
            action = Action::Up;
            break;
        case HostSelectionPressInput::Action::Lost:
            action = Action::Lost;
            break;
        default:
            std::terminate();
        }
        Sample converted;
        converted.point = sample.point;
        converted.modifiers = sample.modifiers;
        converted.ordinal = sample.ordinal;
        converted.antialiased = sample.antialiased;
        return {action, converted};
    }
    [[nodiscard]] static Event Convert(const HostPolygonalInputJournal::Event& event) noexcept
    {
        Action action;
        switch (event.action)
        {
        case HostPolygonalInputJournal::Action::Down:
            action = Action::Down;
            break;
        case HostPolygonalInputJournal::Action::Move:
            action = Action::Move;
            break;
        case HostPolygonalInputJournal::Action::Up:
            action = Action::Up;
            break;
        case HostPolygonalInputJournal::Action::Lost:
            action = Action::Lost;
            break;
        case HostPolygonalInputJournal::Action::Leave:
            action = Action::Leave;
            break;
        case HostPolygonalInputJournal::Action::Backspace:
            action = Action::Backspace;
            break;
        case HostPolygonalInputJournal::Action::Enter:
            action = Action::Enter;
            break;
        case HostPolygonalInputJournal::Action::Escape:
            action = Action::Escape;
            break;
        default:
            std::terminate();
        }
        return {action, event.sample, event.emergency};
    }
    HostSelectionPressInput* _press = nullptr;
    HostPolygonalInputJournal* _journal = nullptr;
    std::optional<Context> _captured;
    std::uint64_t _opening = 0;
    std::uint64_t _lastTakenOrdinal = 0;
    bool _terminal = false;
};
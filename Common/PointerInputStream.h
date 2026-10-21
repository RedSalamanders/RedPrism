#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

// Owner-only storage for one admitted canvas gesture. Context and samples own their event-time values; neither
// may borrow a live control/session. The terminal has a separate slot, so a full move ring cannot lose Up/Lost.
template <class Context, class Sample, std::size_t Capacity> class PointerInputStream final
{
    static_assert(Capacity >= 1);
    static_assert(std::is_trivially_copyable_v<Context> && std::is_trivially_destructible_v<Context>);
    static_assert(std::is_trivially_copyable_v<Sample> && std::is_trivially_destructible_v<Sample>);
    static_assert(std::is_nothrow_copy_constructible_v<Context> && std::is_nothrow_copy_assignable_v<Context>);
    static_assert(std::is_nothrow_copy_constructible_v<Sample> && std::is_nothrow_copy_assignable_v<Sample>);
    static_assert(std::is_nothrow_destructible_v<Context> && std::is_nothrow_destructible_v<Sample>);

  public:
    enum class Action : std::uint8_t
    {
        Down,
        Move,
        Up,
        Lost,
    };

    enum class Admission : std::uint8_t
    {
        Recorded,
        Stale,
        Closed,
        Overflow,
    };

    struct Event final
    {
        Action action;
        Sample sample;
    };

    PointerInputStream() noexcept = default;
    PointerInputStream(const PointerInputStream&) = delete;
    PointerInputStream& operator=(const PointerInputStream&) = delete;
    PointerInputStream(PointerInputStream&&) = delete;
    PointerInputStream& operator=(PointerInputStream&&) = delete;

    [[nodiscard]] bool Begin(std::uint64_t opening, const Context& context, const Sample& down) noexcept
    {
        // Monotonic accepted openings prevent an old producer from appending to a reused slot. Refusal consumes
        // no opening. Exhaustion is handled by the producer, never by wrapping this authority.
        if (_context || opening == 0 || opening <= _lastOpening)
            return false;
        _context.emplace(context);
        _opening = opening;
        _lastOpening = opening;
        _events[0].emplace(Event{Action::Down, down});
        _front = 0;
        _size = 1;
        _lastSample.emplace(down);
        return true;
    }

    [[nodiscard]] Admission Move(std::uint64_t opening, const Sample& sample) noexcept
    {
        if (!_context || opening != _opening)
            return Admission::Stale;
        if (_terminal || _terminalConsumed)
            return Admission::Closed;
        if (_size == Capacity)
        {
            // Do not replace accepted path samples with the refused sample. End their accepted prefix once with
            // the documented pointer-loss policy, and let the host report capacity refusal. Late Up is Closed.
            _terminal.emplace(Event{Action::Lost, _lastSample.value()});
            _overflow = true;
            return Admission::Overflow;
        }
        _events[(_front + _size) % Capacity].emplace(Event{Action::Move, sample});
        ++_size;
        _lastSample = sample;
        return Admission::Recorded;
    }

    [[nodiscard]] Admission End(std::uint64_t opening, const Sample& sample, bool lost) noexcept
    {
        if (!_context || opening != _opening)
            return Admission::Stale;
        if (_terminal || _terminalConsumed)
            return Admission::Closed;
        _terminal.emplace(Event{lost ? Action::Lost : Action::Up, sample});
        return Admission::Recorded;
    }

    [[nodiscard]] std::optional<Event> Peek(std::uint64_t opening) const noexcept
    {
        if (!_context || opening != _opening)
            return std::nullopt;
        if (_size != 0)
            return _events[_front];
        return _terminal;
    }

    [[nodiscard]] std::optional<Event> Take(std::uint64_t opening) noexcept
    {
        if (!_context || opening != _opening)
            return std::nullopt;
        if (_size != 0)
        {
            const Event event = _events[_front].value();
            _events[_front].reset();
            _front = (_front + 1) % Capacity;
            --_size;
            return event;
        }
        if (_terminal)
        {
            const Event event = _terminal.value();
            _terminal.reset();
            _terminalConsumed = true;
            return event;
        }
        return std::nullopt;
    }

    // A drained terminal remains owned until its consumer acknowledges the gesture/closure result. A next press
    // cannot reuse the buffer while a dependent history worker still owns the selected host ordinal.
    [[nodiscard]] bool Acknowledge(std::uint64_t opening) noexcept
    {
        if (!_context || opening != _opening || !_terminalConsumed || _size != 0 || _terminal)
            return false;
        Clear();
        return true;
    }

    // Explicit failed/stale/shutdown acknowledgement. The caller must terminate any already replayed interaction
    // according to the owning tool contract; dropping storage alone is not a canvas cancellation policy.
    [[nodiscard]] bool Drop(std::uint64_t opening) noexcept
    {
        if (!_context || opening != _opening)
            return false;
        Clear();
        return true;
    }

    [[nodiscard]] const Context* CapturedContext(std::uint64_t opening) const noexcept
    {
        return _context && opening == _opening ? &_context.value() : nullptr;
    }
    [[nodiscard]] std::uint64_t Opening() const noexcept
    {
        return _context ? _opening : 0;
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return _context.has_value();
    }
    [[nodiscard]] bool Sampling() const noexcept
    {
        return _context && !_terminal && !_terminalConsumed;
    }
    [[nodiscard]] bool TerminalConsumed() const noexcept
    {
        return _terminalConsumed;
    }
    [[nodiscard]] bool Overflowed() const noexcept
    {
        return _overflow;
    }
    [[nodiscard]] std::size_t PendingCount() const noexcept
    {
        return _size + (_terminal ? 1 : 0);
    }
    static constexpr std::size_t kCapacity = Capacity;

  private:
    void Clear() noexcept
    {
        // Capacity is fixed. No allocation, callback or unbounded graph release is allowed in these value slots.
        while (_size != 0)
        {
            _events[_front].reset();
            _front = (_front + 1) % Capacity;
            --_size;
        }
        _context.reset();
        _lastSample.reset();
        _terminal.reset();
        _opening = 0;
        _front = 0;
        _size = 0;
        _terminalConsumed = false;
        _overflow = false;
    }

    std::array<std::optional<Event>, Capacity> _events{};
    std::optional<Context> _context;
    std::optional<Sample> _lastSample;
    std::optional<Event> _terminal;
    std::uint64_t _opening = 0;
    std::uint64_t _lastOpening = 0;
    std::size_t _front = 0;
    std::size_t _size = 0;
    bool _terminalConsumed = false;
    bool _overflow = false;
};

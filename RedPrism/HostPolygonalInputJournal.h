#pragma once

#include "HostSelectionPressInput.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

// Owner-only physical records while the original Polygonal press's exact hit is unresolved.
// Up is a record, not the journal's terminal: it can end an inside drag or precede more outside corners.
// This class owns scalar metadata only. The dispatcher must reserve each conditional host descriptor
// before accepting its Down, preserve shared ordinals, and capture source/history only after admission.
// A journal lifetime is separate from an original job lifetime; consuming the first Up must not discard
// accepted younger Down records when the first original parent retires.
class HostPolygonalInputJournal final
{
  public:
    using Context = HostSelectionPressInput::Context;
    using Modifiers = HostSelectionPressInput::Modifiers;
    enum class Action : std::uint8_t
    {
        Down,
        Move,
        Up,
        Leave,
        Backspace,
        Enter,
        Escape,
        Lost,
    };
    enum class Admission : std::uint8_t
    {
        Recorded,
        Stale,
        Closed,
        Invalid,
        Overflow,
    };
    struct Sample final
    {
        Vec2 point{};
        Modifiers modifiers{};
        std::uint64_t ordinal = 0;
        double viewScale = 1; // Physical view pixels per document pixel at this event.
        bool antialiased = true;
        bool doubleClick = false;
        bool leftDown = false;
        Vec2 viewPoint{};
        Vec2 documentOrigin{};
        double dipScale = 1; // Physical view pixels per DIP, including the event-time DPI.
    };
    struct Event final
    {
        Action action = Action::Move;
        Sample sample{};
        std::optional<Context> down;
        bool emergency = false;
    };
    static constexpr std::size_t kCapacity = 512;
    static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_destructible_v<Event>);
    static_assert(std::is_nothrow_copy_constructible_v<Event> && std::is_nothrow_copy_assignable_v<Event>);

    HostPolygonalInputJournal() noexcept = default;
    HostPolygonalInputJournal(const HostPolygonalInputJournal&) = delete;
    HostPolygonalInputJournal& operator=(const HostPolygonalInputJournal&) = delete;
    HostPolygonalInputJournal(HostPolygonalInputJournal&&) = delete;
    HostPolygonalInputJournal& operator=(HostPolygonalInputJournal&&) = delete;

    // Same arithmetic and 8-DIP radius as EditorCanvas/SelectionPress. The original first point is
    // chosen by replay; all view operands come from this physical event, never the current viewport.
    [[nodiscard]] static std::optional<bool> NearFirst(const Sample& sample, Vec2 first) noexcept
    {
        if (!Valid(sample) || !std::isfinite(first.x) || !std::isfinite(first.y))
            return std::nullopt;
        const Vec2 firstView{sample.documentOrigin.x + first.x * sample.viewScale,
                             sample.documentOrigin.y + first.y * sample.viewScale};
        return std::hypot(sample.viewPoint.x - firstView.x, sample.viewPoint.y - firstView.y) <= 8.0 * sample.dipScale;
    }

    [[nodiscard]] bool Begin(std::uint64_t stream, const Context& context, Sample sample) noexcept
    {
        if (_stream || !stream || stream <= _lastStream || !Valid(context, sample))
            return false;
        _stream = _lastStream = stream;
        _document = context.document;
        _front = 0;
        _size = 1;
        _events[0].emplace(Event{Action::Down, sample, context});
        _last = sample;
        _closed = _overflow = _lostConsumed = false;
        _lost.reset();
        return true;
    }
    [[nodiscard]] Admission Down(std::uint64_t stream, const Context& context, Sample sample) noexcept
    {
        if (!_stream || stream != _stream)
            return Admission::Stale;
        if (_closed)
            return Admission::Closed;
        if (!Valid(context, sample))
            return Admission::Invalid;
        if (context.document.sessionIdentity != _document.sessionIdentity ||
            context.document.documentSerial != _document.documentSerial ||
            context.document.canvasWidth != _document.canvasWidth ||
            context.document.canvasHeight != _document.canvasHeight)
            return Admission::Stale;
        return Append(stream, Event{Action::Down, sample, context});
    }
    [[nodiscard]] Admission Record(std::uint64_t stream, Action action, Sample sample) noexcept
    {
        if (!_stream || stream != _stream)
            return Admission::Stale;
        if (_closed)
            return Admission::Closed;
        if (action == Action::Down || action > Action::Lost)
            return Admission::Invalid;
        return Append(stream, Event{action, sample});
    }
    [[nodiscard]] Admission SealLost(std::uint64_t stream) noexcept
    {
        if (!_stream || stream != _stream)
            return Admission::Stale;
        if (_closed)
            return Admission::Closed;
        // Capacity/ordinal/lifetime refusal is not a newly accepted physical sample. The separate slot
        // preserves every already accepted record and reuses only its last ordinal, never wrapping it.
        _lost.emplace(Event{Action::Lost, _last, std::nullopt, true});
        _closed = true;
        return Admission::Recorded;
    }
    [[nodiscard]] std::optional<Event> Peek(std::uint64_t stream) const noexcept
    {
        if (!_stream || stream != _stream)
            return std::nullopt;
        if (_size)
            return _events[_front];
        return _lostConsumed ? std::nullopt : _lost;
    }
    [[nodiscard]] std::optional<Event> Take(std::uint64_t stream) noexcept
    {
        auto result = Peek(stream);
        if (!result)
            return std::nullopt;
        if (_size)
        {
            _events[_front].reset();
            _front = (_front + 1) % kCapacity;
            --_size;
        }
        else
            _lostConsumed = true;
        return result;
    }
    // An explicit owner lifecycle decision after all borrowing parents/conditional receipts joined.
    // Physical Up and one original parent's completion must never call this for a still-live family.
    [[nodiscard]] bool CloseInput(std::uint64_t stream) noexcept
    {
        if (!_stream || stream != _stream || _closed)
            return false;
        _closed = true;
        return true;
    }
    // This ends a fully handled journal, not the first original parent. The dispatcher must retain
    // unread conditional gestures across that parent's acknowledgement and admit them in host order.
    [[nodiscard]] bool Finish(std::uint64_t stream) noexcept
    {
        if (!_stream || stream != _stream || !_closed || _size || (_lost && !_lostConsumed))
            return false;
        _stream = 0;
        _document = {};
        _last = {};
        _lost.reset();
        _closed = _overflow = _lostConsumed = false;
        return true;
    }
    [[nodiscard]] std::size_t PendingCount() const noexcept
    {
        return _size + (_lost && !_lostConsumed ? 1 : 0);
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return _stream != 0;
    }
    [[nodiscard]] bool Closed() const noexcept
    {
        return _closed;
    }
    [[nodiscard]] bool Overflowed() const noexcept
    {
        return _overflow;
    }
    [[nodiscard]] std::uint64_t Stream() const noexcept
    {
        return _stream;
    }

  private:
    [[nodiscard]] static bool Valid(Sample sample) noexcept
    {
        return sample.ordinal && std::isfinite(sample.point.x) && std::isfinite(sample.point.y) &&
               std::isfinite(sample.viewScale) && sample.viewScale > 0 && std::isfinite(sample.viewPoint.x) &&
               std::isfinite(sample.viewPoint.y) && std::isfinite(sample.documentOrigin.x) &&
               std::isfinite(sample.documentOrigin.y) && std::isfinite(sample.dipScale) && sample.dipScale > 0;
    }
    [[nodiscard]] static bool Valid(const Context& context, Sample sample) noexcept
    {
        return Valid(sample) && context.ordinal == sample.ordinal && context.document.documentSerial &&
               context.document.canvasWidth > 0 && context.document.canvasHeight > 0 &&
               context.tool == HostSelectionPressInput::Tool::Lasso && context.kind == LassoKind::Polygonal &&
               (context.mode == SelectionMode::New || context.mode == SelectionMode::Add ||
                context.mode == SelectionMode::Subtract);
    }
    [[nodiscard]] Admission Append(std::uint64_t stream, Event event) noexcept
    {
        if (!_stream || stream != _stream)
            return Admission::Stale;
        if (_closed)
            return Admission::Closed;
        if (!Valid(event.sample) || event.sample.ordinal <= _last.ordinal)
            return Admission::Invalid;
        if (_size == kCapacity)
        {
            _overflow = true;
            (void)SealLost(stream);
            return Admission::Overflow;
        }
        _events[(_front + _size) % kCapacity].emplace(event);
        ++_size;
        _last = event.sample;
        _closed = event.action == Action::Lost;
        return Admission::Recorded;
    }
    std::array<std::optional<Event>, kCapacity> _events;
    std::optional<Event> _lost;
    DocumentReadAuthority _document{};
    Sample _last{};
    std::uint64_t _stream = 0, _lastStream = 0;
    std::size_t _front = 0, _size = 0;
    bool _closed = false, _overflow = false, _lostConsumed = false;
};

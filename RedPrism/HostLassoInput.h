#pragma once

#include "Document.h"
#include "PointerInputStream.h"

#include <cmath>
#include <cstdint>
#include <optional>

// Fixed owner storage for event-time lasso input while the admitted opening worker is pending.
// The caller admits this whole member and its job before Begin; this descriptor holds no geometry owner.
// Point values already use the physical event's document mapping. Later headers/views never reinterpret them.
class HostLassoInput final
{
  public:
    struct Context final
    {
        DocumentReadAuthority document;
        std::optional<Guid> active;
        std::optional<CropRect> crop;
        std::uint64_t ordinal = 0;
        std::uint64_t viewEpoch = 0;
        LassoKind kind = LassoKind::Freehand;
        SelectionMode mode = SelectionMode::New;
        bool mask = false;
    };
    enum class Operation : std::uint8_t
    {
        Point,
        Cursor,
        Marquee,
        Backspace,
        PolygonPress,
        Finish,
    };
    struct Sample final
    {
        Vec2 point{};
        Operation operation = Operation::Point;
        bool cursorPresent = false;
        bool square = false;
        bool fromCenter = false;
        bool antialiased = true;
        bool nearFirst = false;
        bool doubleClick = false;
        std::uint64_t closingIntent = 0;
        std::uint64_t ordinal = 0; // shared event-time host order; zero is a component-only descriptor
    };
    using Stream = PointerInputStream<Context, Sample, 512>;
    using Event = Stream::Event;
    using Action = Stream::Action;
    enum class Admission : std::uint8_t
    {
        Recorded,
        Stale,
        Closed,
        Overflow,
        Invalid,
    };
    [[nodiscard]] bool Begin(std::uint64_t opening, Context context, Vec2 point) noexcept
    {
        if (_selectedTerminal || !context.ordinal || !context.document.documentSerial || !context.viewEpoch ||
            context.document.canvasWidth <= 0 || context.document.canvasHeight <= 0 || !Finite(point) ||
            context.kind > LassoKind::Ellipse || context.mode > SelectionMode::Subtract)
            return false;
        Sample down{point};
        down.ordinal = context.ordinal;
        return _stream.Begin(opening, context, down);
    }
    [[nodiscard]] Admission Point(std::uint64_t opening, Vec2 point, std::uint64_t ordinal = 0) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!Finite(point) || (context->kind != LassoKind::Freehand && context->kind != LassoKind::Polygonal))
            return Admission::Invalid;
        Sample sample{point};
        sample.ordinal = ordinal;
        return Convert(_stream.Move(opening, sample));
    }
    [[nodiscard]] Admission Cursor(std::uint64_t opening, std::optional<Vec2> point, std::uint64_t ordinal = 0) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (context->kind != LassoKind::Polygonal || (point && !Finite(point.value())))
            return Admission::Invalid;
        Sample sample{point.value_or(Vec2{}), Operation::Cursor, point.has_value()};
        sample.ordinal = ordinal;
        return Convert(_stream.Move(opening, sample));
    }
    [[nodiscard]] Admission Marquee(std::uint64_t opening, Vec2 point, bool square, bool fromCenter,
                                    std::uint64_t ordinal = 0) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!Finite(point) || (context->kind != LassoKind::Rectangle && context->kind != LassoKind::Ellipse))
            return Admission::Invalid;
        Sample sample{point, Operation::Marquee, false, square, fromCenter};
        sample.ordinal = ordinal;
        return Convert(_stream.Move(opening, sample));
    }
    [[nodiscard]] Admission Backspace(std::uint64_t opening, std::uint64_t ordinal = 0) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (context->kind != LassoKind::Polygonal)
            return Admission::Invalid;
        Sample sample{{}, Operation::Backspace};
        sample.ordinal = ordinal;
        return Convert(_stream.Move(opening, sample));
    }
    // Store the physical click's close decision inputs. Replay evaluates the three-corner condition only after
    // all older accepted corners/backspaces have reached the actual writer; acquisition timing changes no rule.
    [[nodiscard]] Admission PolygonPress(std::uint64_t opening, Vec2 point, bool nearFirst, bool doubleClick,
                                         bool antialiased, std::uint64_t closingIntent = 0,
                                         std::uint64_t ordinal = 0) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!Sampling())
            return Admission::Closed;
        if (context->kind != LassoKind::Polygonal || !Finite(point))
            return Admission::Invalid;
        Sample sample{point, Operation::PolygonPress};
        sample.antialiased = antialiased;
        sample.nearFirst = nearFirst;
        sample.doubleClick = doubleClick;
        sample.closingIntent = closingIntent;
        sample.ordinal = ordinal;
        return Convert(_stream.Move(opening, sample));
    }
    [[nodiscard]] bool SelectReplayedPolygonClose(std::uint64_t opening) noexcept
    {
        const auto* context = _stream.CapturedContext(opening);
        if (_selectedTerminal || !context || context->kind != LassoKind::Polygonal || _stream.Overflowed() ||
            !_lastTaken || _lastTakenOpening != opening || _lastTaken->action != Action::Move ||
            _lastTaken->sample.operation != Operation::PolygonPress ||
            (!_lastTaken->sample.nearFirst && !_lastTaken->sample.doubleClick))
            return false;
        const auto captured = *context;
        Sample finish{{}, Operation::Finish};
        finish.antialiased = _lastTaken->sample.antialiased;
        // Only fixed trivially destructible samples are discarded. No point arena/document owner is released.
        if (!_stream.Drop(opening))
            return false;
        _selectedTerminal.emplace(SelectedTerminal{captured, {Action::Up, finish}, opening});
        _lastTaken.reset();
        _lastTakenOpening = 0;
        return true;
    }
    [[nodiscard]] Admission Finish(std::uint64_t opening, bool antialiased, bool cancelled = false) noexcept
    {
        // The separate terminal slot survives a full move ring. Overflow already owns Lost and preserves
        // the accepted prefix; a later Finish cannot turn that refused gesture into successful history.
        if (_selectedTerminal)
            return _selectedTerminal->opening == opening ? Admission::Closed : Admission::Stale;
        return Convert(_stream.End(opening, {{}, Operation::Finish, false, false, false, antialiased}, cancelled));
    }
    [[nodiscard]] std::optional<Event> Peek(std::uint64_t opening) const noexcept
    {
        if (_selectedTerminal)
            return _selectedTerminal->opening == opening && !_selectedTerminal->consumed
                       ? std::optional(_selectedTerminal->event)
                       : std::nullopt;
        return _stream.Peek(opening);
    }
    [[nodiscard]] std::optional<Event> Take(std::uint64_t opening) noexcept
    {
        auto result = Peek(opening);
        if (_selectedTerminal)
        {
            if (result)
                _selectedTerminal->consumed = true;
        }
        else
            result = _stream.Take(opening);
        _lastTaken = result;
        _lastTakenOpening = result ? opening : 0;
        return result;
    }
    [[nodiscard]] const Context* CapturedContext(std::uint64_t opening) const noexcept
    {
        if (_selectedTerminal)
            return _selectedTerminal->opening == opening ? &_selectedTerminal->context : nullptr;
        return _stream.CapturedContext(opening);
    }
    [[nodiscard]] bool Acknowledge(std::uint64_t opening) noexcept
    {
        if (_selectedTerminal)
        {
            if (_selectedTerminal->opening != opening || !_selectedTerminal->consumed)
                return false;
            _selectedTerminal.reset();
            _lastTaken.reset();
            _lastTakenOpening = 0;
            return true;
        }
        return _stream.Acknowledge(opening);
    }
    [[nodiscard]] bool Drop(std::uint64_t opening) noexcept
    {
        if (_selectedTerminal)
        {
            if (_selectedTerminal->opening != opening)
                return false;
            _selectedTerminal.reset();
            _lastTaken.reset();
            _lastTakenOpening = 0;
            return true;
        }
        const bool dropped = _stream.Drop(opening);
        if (dropped)
        {
            _lastTaken.reset();
            _lastTakenOpening = 0;
        }
        return dropped;
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return _selectedTerminal.has_value() || _stream.Busy();
    }
    [[nodiscard]] bool Sampling() const noexcept
    {
        return _stream.Sampling();
    }
    [[nodiscard]] bool Overflowed() const noexcept
    {
        return _stream.Overflowed();
    }
    [[nodiscard]] std::size_t PendingCount() const noexcept
    {
        return _selectedTerminal ? (_selectedTerminal->consumed ? 0u : 1u) : _stream.PendingCount();
    }
    [[nodiscard]] std::uint64_t Opening() const noexcept
    {
        return _selectedTerminal ? _selectedTerminal->opening : _stream.Opening();
    }
    static constexpr std::size_t kCapacity = Stream::kCapacity;

  private:
    [[nodiscard]] static bool Finite(Vec2 point) noexcept
    {
        return std::isfinite(point.x) && std::isfinite(point.y);
    }
    [[nodiscard]] static Admission Convert(Stream::Admission result) noexcept
    {
        switch (result)
        {
        case Stream::Admission::Recorded:
            return Admission::Recorded;
        case Stream::Admission::Stale:
            return Admission::Stale;
        case Stream::Admission::Closed:
            return Admission::Closed;
        case Stream::Admission::Overflow:
            return Admission::Overflow;
        }
        return Admission::Invalid;
    }
    struct SelectedTerminal final
    {
        Context context;
        Event event;
        std::uint64_t opening;
        bool consumed = false;
    };
    Stream _stream;
    std::optional<Event> _lastTaken;
    std::uint64_t _lastTakenOpening = 0;
    std::optional<SelectedTerminal> _selectedTerminal;
};

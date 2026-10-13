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
        if (!context.ordinal || !context.document.documentSerial || !context.viewEpoch ||
            context.document.canvasWidth <= 0 || context.document.canvasHeight <= 0 || !Finite(point) ||
            context.kind > LassoKind::Ellipse || context.mode > SelectionMode::Subtract)
            return false;
        return _stream.Begin(opening, context, {point});
    }
    [[nodiscard]] Admission Point(std::uint64_t opening, Vec2 point) noexcept
    {
        const auto* context = _stream.CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!Finite(point) || (context->kind != LassoKind::Freehand && context->kind != LassoKind::Polygonal))
            return Admission::Invalid;
        return Convert(_stream.Move(opening, {point}));
    }
    [[nodiscard]] Admission Cursor(std::uint64_t opening, std::optional<Vec2> point) noexcept
    {
        const auto* context = _stream.CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (context->kind != LassoKind::Polygonal || (point && !Finite(point.value())))
            return Admission::Invalid;
        return Convert(_stream.Move(opening, {point.value_or(Vec2{}), Operation::Cursor, point.has_value()}));
    }
    [[nodiscard]] Admission Marquee(std::uint64_t opening, Vec2 point, bool square, bool fromCenter) noexcept
    {
        const auto* context = _stream.CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!Finite(point) || (context->kind != LassoKind::Rectangle && context->kind != LassoKind::Ellipse))
            return Admission::Invalid;
        return Convert(_stream.Move(opening, {point, Operation::Marquee, false, square, fromCenter}));
    }
    [[nodiscard]] Admission Backspace(std::uint64_t opening) noexcept
    {
        const auto* context = _stream.CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (context->kind != LassoKind::Polygonal)
            return Admission::Invalid;
        return Convert(_stream.Move(opening, {{}, Operation::Backspace}));
    }
    [[nodiscard]] Admission Finish(std::uint64_t opening, bool antialiased, bool cancelled = false) noexcept
    {
        // The separate terminal slot survives a full move ring. Overflow already owns Lost and preserves
        // the accepted prefix; a later Finish cannot turn that refused gesture into successful history.
        return Convert(_stream.End(opening, {{}, Operation::Finish, false, false, false, antialiased}, cancelled));
    }
    [[nodiscard]] std::optional<Event> Peek(std::uint64_t opening) const noexcept
    {
        return _stream.Peek(opening);
    }
    [[nodiscard]] std::optional<Event> Take(std::uint64_t opening) noexcept
    {
        return _stream.Take(opening);
    }
    [[nodiscard]] const Context* CapturedContext(std::uint64_t opening) const noexcept
    {
        return _stream.CapturedContext(opening);
    }
    [[nodiscard]] bool Acknowledge(std::uint64_t opening) noexcept
    {
        return _stream.Acknowledge(opening);
    }
    [[nodiscard]] bool Drop(std::uint64_t opening) noexcept
    {
        return _stream.Drop(opening);
    }
    [[nodiscard]] bool Busy() const noexcept
    {
        return _stream.Busy();
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
        return _stream.PendingCount();
    }
    [[nodiscard]] std::uint64_t Opening() const noexcept
    {
        return _stream.Opening();
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
    Stream _stream;
};

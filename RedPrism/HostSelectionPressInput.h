#pragma once

#include "Document.h"
#include "PointerInputStream.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

// Owner-only accepted physical prefix while an admitted selection press waits for exact worker containment.
// No geometry, document, control or GPU owner is held here. The original job owns and validates those roots.
// Replay uses these event-time document coordinates and modifier values, never current keyboard/view state.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class HostSelectionPressInput final
{
  public:
    enum class Tool : std::uint8_t
    {
        Marquee,
        Lasso,
        Wand,
    };
    struct Modifiers final
    {
        bool shift = false;
        bool control = false;
        bool alt = false;
        bool operator==(const Modifiers&) const noexcept = default;
    };
    struct Context final
    {
        DocumentReadAuthority document;
        std::optional<Guid> active;
        std::optional<CropRect> crop;
        WandSettings wand;
        std::uint64_t ordinal = 0;
        Tool tool = Tool::Marquee;
        LassoKind kind = LassoKind::Rectangle;
        SelectionMode mode = SelectionMode::New;
        bool mask = false;
    };
    struct Sample final
    {
        Vec2 point{};
        Modifiers modifiers;
        std::uint64_t ordinal = 0;
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
    [[nodiscard]] bool Begin(std::uint64_t opening, const Context& context, Vec2 point, Modifiers modifiers,
                             bool antialiased) noexcept
    {
        const bool kindMatches = context.tool == Tool::Wand ||
                                 (context.tool == Tool::Marquee
                                      ? context.kind == LassoKind::Rectangle || context.kind == LassoKind::Ellipse
                                      : context.kind == LassoKind::Freehand || context.kind == LassoKind::Polygonal);
        if (!context.document.documentSerial || context.document.canvasWidth <= 0 ||
            context.document.canvasHeight <= 0 || !context.ordinal || context.tool > Tool::Wand ||
            context.kind > LassoKind::Ellipse || context.mode > SelectionMode::Subtract || !kindMatches ||
            (context.tool == Tool::Wand && (context.wand.tolerance < 0 || context.wand.tolerance > 255 ||
                                            context.wand.sampleSize > WandSampleSize::FiveByFive)) ||
            !Finite(point))
            return false;
        const Sample down{point, modifiers, context.ordinal, antialiased};
        if (!_stream.Begin(opening, context, down))
            return false;
        _lastAccepted = down;
        return true;
    }
    [[nodiscard]] Admission Move(std::uint64_t opening, Vec2 point, Modifiers modifiers, bool antialiased,
                                 std::uint64_t ordinal) noexcept
    {
        if (!CapturedContext(opening))
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!Finite(point) || !ordinal || ordinal <= _lastAccepted.ordinal)
            return Admission::Invalid;
        const Sample sample{point, modifiers, ordinal, antialiased};
        const auto result = Convert(_stream.Move(opening, sample));
        if (result == Admission::Recorded)
            _lastAccepted = sample;
        return result;
    }
    [[nodiscard]] Admission Finish(std::uint64_t opening, bool antialiased, std::uint64_t ordinal,
                                   bool lost = false) noexcept
    {
        if (!CapturedContext(opening))
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!ordinal || ordinal <= _lastAccepted.ordinal)
            return Admission::Invalid;
        // SelectionRelease ignores release coordinates/modifiers. Finish retains the last accepted sample
        // and the physical release's antialias choice for a drawing branch. Loss never invents an extra Move.
        auto terminal = _lastAccepted;
        terminal.ordinal = ordinal;
        terminal.antialiased = antialiased;
        return Convert(_stream.End(opening, terminal, lost));
    }
    // Lifecycle/capacity/ordinal exhaustion closes an existing accepted prefix without admitting a new
    // physical sample. Reuse its last accepted ordinal, as the stream's capacity-loss terminal does.
    // The host still owns the original ticket and must apply the selected branch's pointer-loss policy.
    [[nodiscard]] Admission SealLost(std::uint64_t opening) noexcept
    {
        if (!CapturedContext(opening))
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        return Convert(_stream.End(opening, _lastAccepted, true));
    }
    [[nodiscard]] const Context* CapturedContext(std::uint64_t opening) const noexcept
    {
        return _stream.CapturedContext(opening);
    }
    [[nodiscard]] std::optional<Event> Peek(std::uint64_t opening) const noexcept
    {
        return _stream.Peek(opening);
    }
    [[nodiscard]] std::optional<Event> Take(std::uint64_t opening) noexcept
    {
        return _stream.Take(opening);
    }
    [[nodiscard]] bool Acknowledge(std::uint64_t opening) noexcept
    {
        if (!_stream.Acknowledge(opening))
            return false;
        _lastAccepted = {};
        return true;
    }
    [[nodiscard]] bool Drop(std::uint64_t opening) noexcept
    {
        if (!_stream.Drop(opening))
            return false;
        _lastAccepted = {};
        return true;
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
    [[nodiscard]] static Admission Convert(Stream::Admission value) noexcept
    {
        switch (value)
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
    Sample _lastAccepted{};
};
#pragma warning(pop)

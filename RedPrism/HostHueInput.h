#pragma once

#include "Document.h"
#include "PointerInputStream.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

// Owner-only event descriptors while an admitted Hue/Saturation press readback waits. No document,
// pixels, panel pointer or GPU owner is captured. The caller validates the edit/document again at publication.
class HostHueInput final
{
  public:
    enum class Purpose : std::uint8_t
    {
        RangeSample,
        Targeting,
    };
    struct Context final
    {
        DocumentReadAuthority document;
        std::uint64_t panelOpening = 0;
        std::uint64_t ordinal = 0;
        Vec2 documentPoint{};
        double startViewX = 0;
        Purpose purpose = Purpose::RangeSample;
        HueSampleMode mode = HueSampleMode::Replace;
    };
    struct Sample final
    {
        double viewDelta = 0;
        std::uint64_t ordinal = 0;
        bool adjustsHue = false;
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
    [[nodiscard]] bool Begin(std::uint64_t opening, const Context& context) noexcept
    {
        if (!context.document.documentSerial || context.document.canvasWidth <= 0 ||
            context.document.canvasHeight <= 0 || !context.panelOpening || !context.ordinal ||
            context.purpose > Purpose::Targeting || context.mode > HueSampleMode::Remove ||
            !std::isfinite(context.documentPoint.x) || !std::isfinite(context.documentPoint.y) ||
            context.documentPoint.x < 0 || context.documentPoint.y < 0 ||
            context.documentPoint.x >= context.document.canvasWidth ||
            context.documentPoint.y >= context.document.canvasHeight || !std::isfinite(context.startViewX))
            return false;
        const Sample down{0, context.ordinal, false};
        if (!_stream.Begin(opening, context, down))
            return false;
        _lastAccepted = down;
        return true;
    }
    [[nodiscard]] Admission Move(std::uint64_t opening, double viewX, bool adjustsHue, std::uint64_t ordinal) noexcept
    {
        const auto* context = CapturedContext(opening);
        if (!context)
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed; // Late input cannot invalidate an accepted terminal at clock exhaustion.
        const double delta = viewX - context->startViewX;
        if (context->purpose != Purpose::Targeting || !std::isfinite(viewX) || !std::isfinite(delta) || !ordinal ||
            ordinal <= _lastAccepted.ordinal)
            return Admission::Invalid;
        const Sample sample{delta, ordinal, adjustsHue};
        const auto result = Convert(_stream.Move(opening, sample));
        if (result == Admission::Recorded)
            _lastAccepted = sample;
        return result; // Overflow retains the accepted prefix and the stream's separate Lost terminal.
    }
    [[nodiscard]] Admission Finish(std::uint64_t opening, std::uint64_t ordinal, bool lost = false) noexcept
    {
        if (!CapturedContext(opening))
            return Admission::Stale;
        if (!_stream.Sampling())
            return Admission::Closed;
        if (!ordinal || ordinal <= _lastAccepted.ordinal)
            return Admission::Invalid;
        // Legacy release ends the drag without applying a new release coordinate/modifier. Retain only
        // its physical ordinal; the preceding accepted Move still owns the last value and Ctrl choice.
        auto terminal = _lastAccepted;
        terminal.ordinal = ordinal;
        return Convert(_stream.End(opening, terminal, lost));
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
    Sample _lastAccepted{};
};

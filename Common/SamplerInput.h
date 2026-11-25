#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

struct SamplerPointStamp final
{
    std::uint64_t opening;
    std::uint64_t sequence;
};

struct SamplerPoint final
{
    std::int32_t x;
    std::int32_t y;
    SamplerPointStamp stamp;
};

struct SamplerOpaqueColor final
{
    std::uint32_t argb;
    SamplerPointStamp stamp;
};

// Scalar input authority only. The owner must bind each opening to its source,
// palette/picker target and document session, and coordinate worker completion
// with the exact active stamp before acknowledging it.
class SamplerInput final
{
public:
    static constexpr std::uint32_t kPendingCapacity = 512;

    enum class Admission : std::uint8_t
    {
        Recorded,
        Coalesced,
        Refused,
        Overflow,
    };

    // The optional seed is primarily useful for exercising counter exhaustion.
    explicit SamplerInput(std::uint64_t sequenceSeed = 0) noexcept : _lastSequence(sequenceSeed)
    {
    }

    SamplerInput(const SamplerInput&)            = delete;
    SamplerInput& operator=(const SamplerInput&) = delete;

    SamplerInput(SamplerInput&& other) noexcept
    {
        MoveFrom(other);
    }

    SamplerInput& operator=(SamplerInput&& other) noexcept
    {
        if (this != &other)
        {
            MoveFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool Begin(std::uint64_t opening, std::int32_t width, std::int32_t height) noexcept
    {
        if (opening == 0 || opening <= _lastOpening || width <= 0 || height <= 0 || _active.has_value() || _pendingCount != 0 || (_sessionOpen && ! _sealed))
        {
            return false;
        }

        _currentOpening = opening;
        _lastOpening    = opening;
        _width          = width;
        _height         = height;
        _head           = 0;
        _pendingCount   = 0;
        _active.reset();
        _latestOpaqueColor.reset();
        _sealed      = false;
        _cancelled   = false;
        _sessionOpen = true;
        return true;
    }

    [[nodiscard]] Admission Accept(std::uint64_t opening, std::int32_t x, std::int32_t y, bool coalesce = true) noexcept
    {
        if (! MatchesOpenSession(opening) || _sealed || x < 0 || y < 0 || x >= _width || y >= _height ||
            _lastSequence == (std::numeric_limits<std::uint64_t>::max)())
        {
            return Admission::Refused;
        }

        const SamplerPointStamp stamp{opening, _lastSequence + 1};
        if (coalesce && _pendingCount != 0)
        {
            const std::uint32_t tail = (_head + _pendingCount - 1) % kPendingCapacity;
            SamplerPoint& pending    = _pending[tail];
            if (pending.x == x && pending.y == y)
            {
                pending.stamp = stamp;
                _lastSequence = stamp.sequence;
                return Admission::Coalesced;
            }
        }

        if (_pendingCount == kPendingCapacity)
        {
            _sealed = true;
            return Admission::Overflow;
        }

        const std::uint32_t tail = (_head + _pendingCount) % kPendingCapacity;
        _pending[tail]           = SamplerPoint{x, y, stamp};
        ++_pendingCount;
        _lastSequence = stamp.sequence;
        return Admission::Recorded;
    }

    // Owner-only. An active point is never replaced until its exact stamp is acknowledged.
    [[nodiscard]] std::optional<SamplerPoint> SelectNext(std::uint64_t opening) noexcept
    {
        if (! MatchesSession(opening) || _active.has_value() || _pendingCount == 0)
        {
            return std::nullopt;
        }

        _active = _pending[_head];
        _head   = (_head + 1) % kPendingCapacity;
        --_pendingCount;
        return _active;
    }

    [[nodiscard]] bool Acknowledge(SamplerPointStamp stamp, std::optional<std::uint32_t> opaqueArgb) noexcept
    {
        if (! _active.has_value() || ! MatchesStamp(_active.value().stamp, stamp))
        {
            return false;
        }

        if (! _cancelled && opaqueArgb.has_value() && (opaqueArgb.value() & 0xFF000000U) == 0xFF000000U)
        {
            _latestOpaqueColor = SamplerOpaqueColor{opaqueArgb.value(), stamp};
        }
        _active.reset();
        return true;
    }

    [[nodiscard]] bool Seal(std::uint64_t opening) noexcept
    {
        if (! MatchesOpenSession(opening) || _sealed)
        {
            return false;
        }
        _sealed = true;
        return true;
    }

    [[nodiscard]] bool Cancel(std::uint64_t opening) noexcept
    {
        if (! MatchesOpenSession(opening) || _cancelled || (_sealed && ! _active.has_value() && _pendingCount == 0))
        {
            return false;
        }
        _sealed       = true;
        _cancelled    = true;
        _head         = 0;
        _pendingCount = 0;
        return true;
    }

    [[nodiscard]] bool IsFinished(std::uint64_t opening) const noexcept
    {
        return MatchesSession(opening) && _sealed && ! _active.has_value() && _pendingCount == 0;
    }

    [[nodiscard]] bool HasNoWork(std::uint64_t opening) const noexcept
    {
        return MatchesOpenSession(opening) && ! _sealed && ! _active.has_value() && _pendingCount == 0;
    }

    [[nodiscard]] std::optional<SamplerPoint> ActivePoint(std::uint64_t opening) const noexcept
    {
        if (! MatchesSession(opening))
        {
            return std::nullopt;
        }
        return _active;
    }

    [[nodiscard]] std::optional<SamplerOpaqueColor> LatestOpaqueColor() const noexcept
    {
        return _latestOpaqueColor;
    }

    [[nodiscard]] std::uint32_t PendingCount() const noexcept
    {
        return _pendingCount;
    }

    [[nodiscard]] std::uint64_t LastSequence() const noexcept
    {
        return _lastSequence;
    }

private:
    [[nodiscard]] bool MatchesOpenSession(std::uint64_t opening) const noexcept
    {
        return _sessionOpen && opening != 0 && opening == _currentOpening;
    }

    [[nodiscard]] bool MatchesSession(std::uint64_t opening) const noexcept
    {
        return opening != 0 && opening == _currentOpening;
    }

    [[nodiscard]] static bool MatchesStamp(SamplerPointStamp left, SamplerPointStamp right) noexcept
    {
        return left.opening == right.opening && left.sequence == right.sequence;
    }

    void MoveFrom(SamplerInput& other) noexcept
    {
        _pending           = other._pending;
        _active            = other._active;
        _latestOpaqueColor = other._latestOpaqueColor;
        _currentOpening    = other._currentOpening;
        _lastOpening       = other._lastOpening;
        _lastSequence      = other._lastSequence;
        _width             = other._width;
        _height            = other._height;
        _head              = other._head;
        _pendingCount      = other._pendingCount;
        _sessionOpen       = other._sessionOpen;
        _sealed            = other._sealed;
        _cancelled         = other._cancelled;

        other._active.reset();
        other._latestOpaqueColor.reset();
        other._currentOpening = 0;
        other._width          = 0;
        other._height         = 0;
        other._head           = 0;
        other._pendingCount   = 0;
        other._sessionOpen    = false;
        other._sealed         = true;
        other._cancelled      = true;
    }

    std::array<SamplerPoint, kPendingCapacity> _pending{};
    std::optional<SamplerPoint> _active;
    std::optional<SamplerOpaqueColor> _latestOpaqueColor;
    std::uint64_t _currentOpening = 0;
    std::uint64_t _lastOpening    = 0;
    std::uint64_t _lastSequence   = 0;
    std::int32_t _width           = 0;
    std::int32_t _height          = 0;
    std::uint32_t _head           = 0;
    std::uint32_t _pendingCount   = 0;
    bool _sessionOpen             = false;
    bool _sealed                  = false;
    bool _cancelled               = false;
};

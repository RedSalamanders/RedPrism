#pragma once

#include "SamplerInput.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

struct SamplerPointIntent final
{
    std::uint64_t paletteIntent = 0;
    std::uint64_t pickerIntent  = 0;
    std::uint32_t baselineArgb  = 0xFF000000U;

    bool operator==(const SamplerPointIntent&) const = default;
};

struct SamplerIntentPoint final
{
    SamplerPoint point;
    SamplerPointIntent intent;
};

// Fixed-storage scalar context paired with SamplerInput's accepted point FIFO.
class SamplerIntentInput final
{
public:
    using Admission                                 = SamplerInput::Admission;
    static constexpr std::uint32_t kPendingCapacity = SamplerInput::kPendingCapacity;

    explicit SamplerIntentInput(std::uint64_t sequenceSeed = 0) noexcept : _input(sequenceSeed)
    {
    }

    SamplerIntentInput(const SamplerIntentInput&)            = delete;
    SamplerIntentInput& operator=(const SamplerIntentInput&) = delete;

    SamplerIntentInput(SamplerIntentInput&& other) noexcept : _input(std::move(other._input))
    {
        MoveMetadataFrom(other);
    }

    SamplerIntentInput& operator=(SamplerIntentInput&& other) noexcept
    {
        if (this != &other)
        {
            _input = std::move(other._input);
            MoveMetadataFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool Begin(std::uint64_t opening, std::int32_t width, std::int32_t height) noexcept
    {
        if (! _input.Begin(opening, width, height))
        {
            return false;
        }
        _head         = 0;
        _pendingCount = 0;
        _activeIntent.reset();
        _hasPendingTail = false;
        return true;
    }

    [[nodiscard]] Admission Accept(std::uint64_t opening, std::int32_t x, std::int32_t y, SamplerPointIntent intent) noexcept
    {
        bool samePendingPointAndIntent = false;
        if (_hasPendingTail)
        {
            const std::uint32_t tail  = (_head + _pendingCount - 1) % kPendingCapacity;
            samePendingPointAndIntent = _pendingTailX == x && _pendingTailY == y && _pendingIntents[tail] == intent;
        }

        const Admission admission = _input.Accept(opening, x, y, samePendingPointAndIntent);
        if (admission == Admission::Recorded)
        {
            const std::uint32_t tail = (_head + _pendingCount) % kPendingCapacity;
            _pendingIntents[tail]    = intent;
            ++_pendingCount;
            _pendingTailX   = x;
            _pendingTailY   = y;
            _hasPendingTail = true;
        }
        else if (admission == Admission::Coalesced)
        {
            const std::uint32_t tail = (_head + _pendingCount - 1) % kPendingCapacity;
            _pendingIntents[tail]    = intent;
        }
        return admission;
    }

    [[nodiscard]] std::optional<SamplerIntentPoint> SelectNext(std::uint64_t opening) noexcept
    {
        const std::optional<SamplerPoint> point = _input.SelectNext(opening);
        if (! point.has_value())
        {
            return std::nullopt;
        }
        _activeIntent = _pendingIntents[_head];
        _head         = (_head + 1) % kPendingCapacity;
        --_pendingCount;
        _hasPendingTail = _pendingCount != 0;
        return SamplerIntentPoint{point.value(), _activeIntent.value()};
    }

    [[nodiscard]] bool Acknowledge(SamplerPointStamp stamp, std::optional<std::uint32_t> opaqueArgb) noexcept
    {
        if (! _input.Acknowledge(stamp, opaqueArgb))
        {
            return false;
        }
        _activeIntent.reset();
        return true;
    }

    [[nodiscard]] bool Seal(std::uint64_t opening) noexcept
    {
        return _input.Seal(opening);
    }

    [[nodiscard]] bool Cancel(std::uint64_t opening) noexcept
    {
        if (! _input.Cancel(opening))
        {
            return false;
        }
        _head           = 0;
        _pendingCount   = 0;
        _hasPendingTail = false;
        return true;
    }

    [[nodiscard]] bool IsFinished(std::uint64_t opening) const noexcept
    {
        return _input.IsFinished(opening);
    }

    [[nodiscard]] bool HasNoWork(std::uint64_t opening) const noexcept
    {
        return _input.HasNoWork(opening);
    }

    [[nodiscard]] std::optional<SamplerIntentPoint> ActivePoint(std::uint64_t opening) const noexcept
    {
        const std::optional<SamplerPoint> point = _input.ActivePoint(opening);
        if (! point.has_value() || ! _activeIntent.has_value())
        {
            return std::nullopt;
        }
        return SamplerIntentPoint{point.value(), _activeIntent.value()};
    }

    [[nodiscard]] std::optional<SamplerOpaqueColor> LatestOpaqueColor() const noexcept
    {
        return _input.LatestOpaqueColor();
    }

    [[nodiscard]] std::uint32_t PendingCount() const noexcept
    {
        return _pendingCount;
    }

    [[nodiscard]] std::uint64_t LastSequence() const noexcept
    {
        return _input.LastSequence();
    }

private:
    void MoveMetadataFrom(SamplerIntentInput& other) noexcept
    {
        _pendingIntents = other._pendingIntents;
        _activeIntent   = other._activeIntent;
        _head           = other._head;
        _pendingCount   = other._pendingCount;
        _pendingTailX   = other._pendingTailX;
        _pendingTailY   = other._pendingTailY;
        _hasPendingTail = other._hasPendingTail;
        other._activeIntent.reset();
        other._head           = 0;
        other._pendingCount   = 0;
        other._hasPendingTail = false;
    }

    SamplerInput _input;
    std::array<SamplerPointIntent, kPendingCapacity> _pendingIntents{};
    std::optional<SamplerPointIntent> _activeIntent;
    std::uint32_t _head         = 0;
    std::uint32_t _pendingCount = 0;
    std::int32_t _pendingTailX  = 0;
    std::int32_t _pendingTailY  = 0;
    bool _hasPendingTail        = false;
};

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
    explicit SamplerInput(std::uint64_t sequenceSeed = 0) noexcept : lastSequence_(sequenceSeed) {}

    SamplerInput(const SamplerInput&) = delete;
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
        if (opening == 0 || opening <= lastOpening_ || width <= 0 || height <= 0 || active_.has_value() ||
            pendingCount_ != 0 || (sessionOpen_ && !sealed_))
        {
            return false;
        }

        currentOpening_ = opening;
        lastOpening_ = opening;
        width_ = width;
        height_ = height;
        head_ = 0;
        pendingCount_ = 0;
        active_.reset();
        latestOpaqueColor_.reset();
        sealed_ = false;
        cancelled_ = false;
        sessionOpen_ = true;
        return true;
    }

    [[nodiscard]] Admission Accept(std::uint64_t opening, std::int32_t x, std::int32_t y) noexcept
    {
        if (!MatchesOpenSession(opening) || sealed_ || x < 0 || y < 0 || x >= width_ || y >= height_ ||
            lastSequence_ == (std::numeric_limits<std::uint64_t>::max)())
        {
            return Admission::Refused;
        }

        const SamplerPointStamp stamp{opening, lastSequence_ + 1};
        if (pendingCount_ != 0)
        {
            const std::uint32_t tail = (head_ + pendingCount_ - 1) % kPendingCapacity;
            SamplerPoint& pending = pending_[tail];
            if (pending.x == x && pending.y == y)
            {
                pending.stamp = stamp;
                lastSequence_ = stamp.sequence;
                return Admission::Coalesced;
            }
        }

        if (pendingCount_ == kPendingCapacity)
        {
            sealed_ = true;
            return Admission::Overflow;
        }

        const std::uint32_t tail = (head_ + pendingCount_) % kPendingCapacity;
        pending_[tail] = SamplerPoint{x, y, stamp};
        ++pendingCount_;
        lastSequence_ = stamp.sequence;
        return Admission::Recorded;
    }

    // Owner-only. An active point is never replaced until its exact stamp is acknowledged.
    [[nodiscard]] std::optional<SamplerPoint> SelectNext(std::uint64_t opening) noexcept
    {
        if (!MatchesSession(opening) || active_.has_value() || pendingCount_ == 0)
        {
            return std::nullopt;
        }

        active_ = pending_[head_];
        head_ = (head_ + 1) % kPendingCapacity;
        --pendingCount_;
        return active_;
    }

    [[nodiscard]] bool Acknowledge(SamplerPointStamp stamp, std::optional<std::uint32_t> opaqueArgb) noexcept
    {
        if (!active_.has_value() || !MatchesStamp(active_.value().stamp, stamp))
        {
            return false;
        }

        if (!cancelled_ && opaqueArgb.has_value() && (opaqueArgb.value() & 0xFF000000U) == 0xFF000000U)
        {
            latestOpaqueColor_ = SamplerOpaqueColor{opaqueArgb.value(), stamp};
        }
        active_.reset();
        return true;
    }

    [[nodiscard]] bool Seal(std::uint64_t opening) noexcept
    {
        if (!MatchesOpenSession(opening) || sealed_)
        {
            return false;
        }
        sealed_ = true;
        return true;
    }

    [[nodiscard]] bool Cancel(std::uint64_t opening) noexcept
    {
        if (!MatchesOpenSession(opening) || cancelled_ || (sealed_ && !active_.has_value() && pendingCount_ == 0))
        {
            return false;
        }
        sealed_ = true;
        cancelled_ = true;
        head_ = 0;
        pendingCount_ = 0;
        return true;
    }

    [[nodiscard]] bool IsFinished(std::uint64_t opening) const noexcept
    {
        return MatchesSession(opening) && sealed_ && !active_.has_value() && pendingCount_ == 0;
    }

    [[nodiscard]] bool HasNoWork(std::uint64_t opening) const noexcept
    {
        return MatchesOpenSession(opening) && !sealed_ && !active_.has_value() && pendingCount_ == 0;
    }

    [[nodiscard]] std::optional<SamplerPoint> ActivePoint(std::uint64_t opening) const noexcept
    {
        if (!MatchesSession(opening))
        {
            return std::nullopt;
        }
        return active_;
    }

    [[nodiscard]] std::optional<SamplerOpaqueColor> LatestOpaqueColor() const noexcept
    {
        return latestOpaqueColor_;
    }

    [[nodiscard]] std::uint32_t PendingCount() const noexcept
    {
        return pendingCount_;
    }

    [[nodiscard]] std::uint64_t LastSequence() const noexcept
    {
        return lastSequence_;
    }

  private:
    [[nodiscard]] bool MatchesOpenSession(std::uint64_t opening) const noexcept
    {
        return sessionOpen_ && opening != 0 && opening == currentOpening_;
    }

    [[nodiscard]] bool MatchesSession(std::uint64_t opening) const noexcept
    {
        return opening != 0 && opening == currentOpening_;
    }

    [[nodiscard]] static bool MatchesStamp(SamplerPointStamp left, SamplerPointStamp right) noexcept
    {
        return left.opening == right.opening && left.sequence == right.sequence;
    }

    void MoveFrom(SamplerInput& other) noexcept
    {
        pending_ = other.pending_;
        active_ = other.active_;
        latestOpaqueColor_ = other.latestOpaqueColor_;
        currentOpening_ = other.currentOpening_;
        lastOpening_ = other.lastOpening_;
        lastSequence_ = other.lastSequence_;
        width_ = other.width_;
        height_ = other.height_;
        head_ = other.head_;
        pendingCount_ = other.pendingCount_;
        sessionOpen_ = other.sessionOpen_;
        sealed_ = other.sealed_;
        cancelled_ = other.cancelled_;

        other.active_.reset();
        other.latestOpaqueColor_.reset();
        other.currentOpening_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.head_ = 0;
        other.pendingCount_ = 0;
        other.sessionOpen_ = false;
        other.sealed_ = true;
        other.cancelled_ = true;
    }

    std::array<SamplerPoint, kPendingCapacity> pending_{};
    std::optional<SamplerPoint> active_;
    std::optional<SamplerOpaqueColor> latestOpaqueColor_;
    std::uint64_t currentOpening_ = 0;
    std::uint64_t lastOpening_ = 0;
    std::uint64_t lastSequence_ = 0;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
    std::uint32_t head_ = 0;
    std::uint32_t pendingCount_ = 0;
    bool sessionOpen_ = false;
    bool sealed_ = false;
    bool cancelled_ = false;
};

#pragma once

#include "SamplerInput.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

struct SamplerPointIntent final
{
    std::uint64_t paletteIntent = 0;
    std::uint64_t pickerIntent = 0;
    std::uint32_t baselineArgb = 0xFF000000U;

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
    using Admission = SamplerInput::Admission;
    static constexpr std::uint32_t kPendingCapacity = SamplerInput::kPendingCapacity;

    explicit SamplerIntentInput(std::uint64_t sequenceSeed = 0) noexcept : input_(sequenceSeed) {}

    SamplerIntentInput(const SamplerIntentInput&) = delete;
    SamplerIntentInput& operator=(const SamplerIntentInput&) = delete;

    SamplerIntentInput(SamplerIntentInput&& other) noexcept : input_(std::move(other.input_))
    {
        MoveMetadataFrom(other);
    }

    SamplerIntentInput& operator=(SamplerIntentInput&& other) noexcept
    {
        if (this != &other)
        {
            input_ = std::move(other.input_);
            MoveMetadataFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool Begin(std::uint64_t opening, std::int32_t width, std::int32_t height) noexcept
    {
        if (!input_.Begin(opening, width, height))
        {
            return false;
        }
        head_ = 0;
        pendingCount_ = 0;
        activeIntent_.reset();
        hasPendingTail_ = false;
        return true;
    }

    [[nodiscard]] Admission Accept(std::uint64_t opening, std::int32_t x, std::int32_t y,
                                   SamplerPointIntent intent) noexcept
    {
        bool samePendingPointAndIntent = false;
        if (hasPendingTail_)
        {
            const std::uint32_t tail = (head_ + pendingCount_ - 1) % kPendingCapacity;
            samePendingPointAndIntent = pendingTailX_ == x && pendingTailY_ == y && pendingIntents_[tail] == intent;
        }

        const Admission admission = input_.Accept(opening, x, y, samePendingPointAndIntent);
        if (admission == Admission::Recorded)
        {
            const std::uint32_t tail = (head_ + pendingCount_) % kPendingCapacity;
            pendingIntents_[tail] = intent;
            ++pendingCount_;
            pendingTailX_ = x;
            pendingTailY_ = y;
            hasPendingTail_ = true;
        }
        else if (admission == Admission::Coalesced)
        {
            const std::uint32_t tail = (head_ + pendingCount_ - 1) % kPendingCapacity;
            pendingIntents_[tail] = intent;
        }
        return admission;
    }

    [[nodiscard]] std::optional<SamplerIntentPoint> SelectNext(std::uint64_t opening) noexcept
    {
        const std::optional<SamplerPoint> point = input_.SelectNext(opening);
        if (!point.has_value())
        {
            return std::nullopt;
        }
        activeIntent_ = pendingIntents_[head_];
        head_ = (head_ + 1) % kPendingCapacity;
        --pendingCount_;
        hasPendingTail_ = pendingCount_ != 0;
        return SamplerIntentPoint{point.value(), activeIntent_.value()};
    }

    [[nodiscard]] bool Acknowledge(SamplerPointStamp stamp, std::optional<std::uint32_t> opaqueArgb) noexcept
    {
        if (!input_.Acknowledge(stamp, opaqueArgb))
        {
            return false;
        }
        activeIntent_.reset();
        return true;
    }

    [[nodiscard]] bool Seal(std::uint64_t opening) noexcept
    {
        return input_.Seal(opening);
    }

    [[nodiscard]] bool Cancel(std::uint64_t opening) noexcept
    {
        if (!input_.Cancel(opening))
        {
            return false;
        }
        head_ = 0;
        pendingCount_ = 0;
        hasPendingTail_ = false;
        return true;
    }

    [[nodiscard]] bool IsFinished(std::uint64_t opening) const noexcept
    {
        return input_.IsFinished(opening);
    }

    [[nodiscard]] bool HasNoWork(std::uint64_t opening) const noexcept
    {
        return input_.HasNoWork(opening);
    }

    [[nodiscard]] std::optional<SamplerIntentPoint> ActivePoint(std::uint64_t opening) const noexcept
    {
        const std::optional<SamplerPoint> point = input_.ActivePoint(opening);
        if (!point.has_value() || !activeIntent_.has_value())
        {
            return std::nullopt;
        }
        return SamplerIntentPoint{point.value(), activeIntent_.value()};
    }

    [[nodiscard]] std::optional<SamplerOpaqueColor> LatestOpaqueColor() const noexcept
    {
        return input_.LatestOpaqueColor();
    }

    [[nodiscard]] std::uint32_t PendingCount() const noexcept
    {
        return pendingCount_;
    }

    [[nodiscard]] std::uint64_t LastSequence() const noexcept
    {
        return input_.LastSequence();
    }

  private:
    void MoveMetadataFrom(SamplerIntentInput& other) noexcept
    {
        pendingIntents_ = other.pendingIntents_;
        activeIntent_ = other.activeIntent_;
        head_ = other.head_;
        pendingCount_ = other.pendingCount_;
        pendingTailX_ = other.pendingTailX_;
        pendingTailY_ = other.pendingTailY_;
        hasPendingTail_ = other.hasPendingTail_;
        other.activeIntent_.reset();
        other.head_ = 0;
        other.pendingCount_ = 0;
        other.hasPendingTail_ = false;
    }

    SamplerInput input_;
    std::array<SamplerPointIntent, kPendingCapacity> pendingIntents_{};
    std::optional<SamplerPointIntent> activeIntent_;
    std::uint32_t head_ = 0;
    std::uint32_t pendingCount_ = 0;
    std::int32_t pendingTailX_ = 0;
    std::int32_t pendingTailY_ = 0;
    bool hasPendingTail_ = false;
};

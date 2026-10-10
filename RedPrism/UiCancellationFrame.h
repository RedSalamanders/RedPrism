#pragma once

#include "Jobs.h"

#include <cstdint>
#include <limits>

// Owner-only proof of the first successful visible Present after accepted Escape. Original raster,
// history and document-read authority remain owned and compared by the caller, never hashed here.
class UiCancellationFrame final
{
  public:
    enum class State : std::uint8_t
    {
        AwaitingAcceptance,
        AwaitingFirstVisibleFrame,
        Qualified,
        Rejected
    };
    enum class PresentAttempt : std::uint8_t
    {
        None,
        TestOnly,
        Failed,
        Occluded,
        SuccessfulVisible
    };
    struct Acceptance final
    {
        JobTicket original;
        std::uint64_t documentSerial = 0;
        std::uint64_t opening = 0;
        std::uint64_t presentOrdinal = 0;
        bool originalStateCurrent = false;
        bool cardHidden = false;
    };
    struct Frame final
    {
        JobTicket original;
        std::uint64_t documentSerial = 0;
        std::uint64_t opening = 0;
        std::uint64_t presentOrdinal = 0;
        PresentAttempt attempt = PresentAttempt::None;
        bool originalStateCurrent = false;
        bool committedSourceSelected = false;
        bool previewSelected = false;
        bool cardHidden = false;
    };

    [[nodiscard]] bool Accept(Acceptance acceptance) noexcept
    {
        if (_state != State::AwaitingAcceptance || acceptance.original.slot >= JobScheduler::kCapacity ||
            acceptance.original.generation == 0 || acceptance.documentSerial == 0 || acceptance.opening == 0 ||
            acceptance.presentOrdinal == (std::numeric_limits<std::uint64_t>::max)() ||
            !acceptance.originalStateCurrent || !acceptance.cardHidden)
        {
            _state = State::Rejected;
            return false;
        }
        _accepted = acceptance;
        _state = State::AwaitingFirstVisibleFrame;
        return true;
    }
    [[nodiscard]] State Observe(Frame frame) noexcept
    {
        if (_state != State::AwaitingFirstVisibleFrame)
            return _state;
        if (frame.original != _accepted.original || frame.documentSerial != _accepted.documentSerial ||
            frame.opening != _accepted.opening || frame.presentOrdinal < _accepted.presentOrdinal)
            return Reject();
        switch (frame.attempt)
        {
        case PresentAttempt::None:
        case PresentAttempt::TestOnly:
        case PresentAttempt::Failed:
        case PresentAttempt::Occluded:
            return frame.presentOrdinal == _accepted.presentOrdinal ? _state : Reject();
        case PresentAttempt::SuccessfulVisible:
            break;
        default:
            return Reject();
        }
        if (frame.presentOrdinal != _accepted.presentOrdinal + 1 || !frame.originalStateCurrent ||
            !frame.committedSourceSelected || frame.previewSelected || !frame.cardHidden)
            return Reject();
        _state = State::Qualified;
        return _state;
    }
    [[nodiscard]] State Current() const noexcept
    {
        return _state;
    }
    [[nodiscard]] std::uint64_t QualifiedOrdinal() const noexcept
    {
        return _state == State::Qualified ? _accepted.presentOrdinal + 1 : 0;
    }

  private:
    [[nodiscard]] State Reject() noexcept
    {
        _state = State::Rejected;
        return _state;
    }
    State _state = State::AwaitingAcceptance;
    Acceptance _accepted{};
};

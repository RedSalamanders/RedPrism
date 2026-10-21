#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

// Owner-only field input ordering, independent of the model's acknowledged edit opening and history.
// A proposal may be used only during synchronous admission while this controller remains alive. Workers
// receive the resulting scalar stamp, never a proposal or a pointer to the controller.
class OpacityInputOrder final
{
  public:
    enum class Phase : std::uint8_t
    {
        Preview,
        Commit,
        Cancel
    };
    struct Stamp final
    {
        std::uint64_t stream = 0, sequence = 0;
        [[nodiscard]] bool operator==(const Stamp&) const noexcept = default;
    };
    struct Counters final
    {
        std::uint64_t lastStream = 0, lastSequence = 0, epoch = 0, currentStream = 0;
        [[nodiscard]] bool operator==(const Counters&) const noexcept = default;
    };
    class Proposal final
    {
      public:
        Proposal(const Proposal&) = delete;
        Proposal& operator=(const Proposal&) = delete;
        Proposal(Proposal&& source) noexcept
            : _owner(source._owner), _epoch(source._epoch), _stamp(source._stamp), _phase(source._phase),
              _minted(source._minted), _valid(std::exchange(source._valid, false))
        {
        }
        Proposal& operator=(Proposal&&) = delete;
        [[nodiscard]] Stamp Identity() const noexcept
        {
            return _stamp;
        }

      private:
        friend class OpacityInputOrder;
        Proposal(const OpacityInputOrder* owner, std::uint64_t epoch, Stamp stamp, Phase phase, bool minted) noexcept
            : _owner(owner), _epoch(epoch), _stamp(stamp), _phase(phase), _minted(minted)
        {
        }
        const OpacityInputOrder* _owner;
        std::uint64_t _epoch;
        Stamp _stamp;
        Phase _phase;
        bool _minted, _valid = true;
    };

    // Production starts at zero. Explicit counters allow deterministic qualification of exhaustion.
    OpacityInputOrder() noexcept = default;
    explicit OpacityInputOrder(Counters counters) noexcept
        : _counters(counters), _validCounters(counters.currentStream <= counters.lastStream)
    {
    }
    OpacityInputOrder(const OpacityInputOrder&) = delete;
    OpacityInputOrder& operator=(const OpacityInputOrder&) = delete;
    OpacityInputOrder(OpacityInputOrder&&) = delete;
    OpacityInputOrder& operator=(OpacityInputOrder&&) = delete;

    [[nodiscard]] Counters Read() const noexcept
    {
        return _counters;
    }
    // continues is the owner's exact document/target predicate. A rejected queue admission consumes nothing.
    [[nodiscard]] std::optional<Proposal> Propose(Phase phase, bool continues) const noexcept
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (!_validCounters || phase > Phase::Cancel || _counters.lastSequence == maximum || _counters.epoch == maximum)
            return std::nullopt;
        std::uint64_t stream = continues ? _counters.currentStream : 0;
        const bool minted = phase == Phase::Preview && stream == 0;
        if (minted)
        {
            if (_counters.lastStream == maximum)
                return std::nullopt;
            stream = _counters.lastStream + 1;
        }
        return Proposal(this, _counters.epoch, {stream, _counters.lastSequence + 1}, phase, minted);
    }
    [[nodiscard]] bool Accept(Proposal& proposal) noexcept
    {
        if (!proposal._valid || proposal._owner != this || proposal._epoch != _counters.epoch)
            return false;
        if (proposal._minted)
            _counters.lastStream = proposal._stamp.stream;
        _counters.lastSequence = proposal._stamp.sequence;
        _counters.currentStream = proposal._phase == Phase::Preview ? proposal._stamp.stream : 0;
        ++_counters.epoch; // Propose checked exhaustion before any request was admitted.
        proposal._valid = false;
        return true;
    }
    // Only an accepted input that ends the field stream may place this barrier. View input may preserve it.
    // A barrier also expires an outstanding proposal, even when no stream was open.
    [[nodiscard]] bool Barrier() noexcept
    {
        if (!_validCounters || _counters.epoch == (std::numeric_limits<std::uint64_t>::max)())
            return false;
        _counters.currentStream = 0;
        ++_counters.epoch;
        return true;
    }

  private:
    Counters _counters{};
    bool _validCounters = true;
};

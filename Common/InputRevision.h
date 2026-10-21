#pragma once

#include <cstdint>
#include <limits>

// Owner-thread input authority. Zero is permanently invalid after exhaustion; equal values never rebind
// an earlier accepted input. A containing session/host identity supplies the separate owner lifetime.
class InputRevision final
{
  public:
    explicit InputRevision(std::uint64_t initial = 1) noexcept : _value(initial) {}
    [[nodiscard]] std::uint64_t Current() const noexcept
    {
        return _value;
    }
    [[nodiscard]] bool Matches(std::uint64_t revision) const noexcept
    {
        return revision != 0 && revision == _value;
    }
    void Advance() noexcept
    {
        if (_value != 0)
            _value = _value == (std::numeric_limits<std::uint64_t>::max)() ? 0 : _value + 1;
    }

  private:
    std::uint64_t _value;
};

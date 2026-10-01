#pragma once

#include "Jobs.h"

#include <array>
#include <cstddef>

// Owner-thread bounded routing for the scheduler's one borrowed source. Leaf producers retain their own
// event/callback lifetime and call NotifyOwnerWork after publishing readiness. Join them before detaching.
// No producer may mutate this list; attach/detach are startup/teardown outside all owner callbacks.
class OwnerWorkMultiplexer final : public OwnerWorkPump
{
  public:
    static constexpr std::size_t kCapacity = 8;

    [[nodiscard]] bool Attach(OwnerWorkPump& source) noexcept
    {
        if (_pumping || &source == this)
            return false;
        for (const auto* current : _sources)
            if (current == &source)
                return false;
        for (auto*& current : _sources)
        {
            if (current == nullptr)
            {
                current = &source;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool Detach(OwnerWorkPump& source) noexcept
    {
        if (_pumping)
            return false;
        for (auto*& current : _sources)
        {
            if (current == &source)
            {
                if (source.HasPending() || source.HasReady())
                    return false;
                current = nullptr;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool HasPending() const noexcept override
    {
        for (const auto* source : _sources)
            if (source && source->HasPending())
                return true;
        return false;
    }

    [[nodiscard]] bool HasReady() const noexcept override
    {
        for (const auto* source : _sources)
            if (source && source->HasReady())
                return true;
        return false;
    }

    void Pump() noexcept override
    {
        if (_pumping)
            return;
        _pumping = true;
        // One leaf per outer owner pass. Rotation prevents a continuously ready device source from starving
        // timer work; waiting leaves neither run nor create polling wakes. The scheduler rechecks HasReady.
        for (std::size_t offset = 0; offset < kCapacity; ++offset)
        {
            const auto slot = (_next + offset) % kCapacity;
            if (auto* source = _sources[slot]; source && source->HasReady())
            {
                _next = (slot + 1) % kCapacity;
                source->Pump();
                break;
            }
        }
        _pumping = false;
    }

  private:
    std::array<OwnerWorkPump*, kCapacity> _sources{};
    std::size_t _next = 0;
    bool _pumping = false;
};

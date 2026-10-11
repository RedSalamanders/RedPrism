#include "SharedRetainedMemoryState.h"

#include <cassert>
#include <utility>

SharedRetainedMemoryLease::~SharedRetainedMemoryLease()
{
    Reset();
}

SharedRetainedMemoryLease::SharedRetainedMemoryLease(SharedRetainedMemoryLease&& other) noexcept
    : _state(std::move(other._state)), _slot(std::exchange(other._slot, 64)), _part(std::exchange(other._part, 0)),
      _generation(std::exchange(other._generation, 0)), _bytes(std::exchange(other._bytes, 0))
{
}

SharedRetainedMemoryLease& SharedRetainedMemoryLease::operator=(SharedRetainedMemoryLease&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        _state = std::move(other._state);
        _slot = std::exchange(other._slot, 64);
        _part = std::exchange(other._part, 0);
        _generation = std::exchange(other._generation, 0);
        _bytes = std::exchange(other._bytes, 0);
    }
    return *this;
}

void SharedRetainedMemoryLease::Reset() noexcept
{
    auto state = std::move(_state);
    if (!state)
        return;
    const auto slot = std::exchange(_slot, 64);
    const auto part = std::exchange(_part, 0);
    const auto generation = std::exchange(_generation, 0);
    const auto bytes = std::exchange(_bytes, 0);
    assert(slot < JobScheduler::kRetainedMemoryCapacity && part < 2 && generation != 0 && bytes != 0);
    // Destruction of the enclosing graph precedes publication. A slot cannot be reused until the owner has
    // consumed its bit; the old producer performs no further slot writes after that publication.
    const auto previous = state->residentBytes.fetch_sub(bytes, std::memory_order_acq_rel);
    assert(previous >= bytes);
    (void)previous;
    state->acknowledged[part * JobScheduler::kRetainedMemoryCapacity + slot].store(generation,
                                                                                   std::memory_order_relaxed);
    const auto pending = state->pending[part].fetch_or(std::uint64_t{1} << slot, std::memory_order_release);
    if (pending == 0)
        (void)SetEvent(state->wake.get()); // One wake per word empty-to-ready transition; at most two, no polling.
}

std::uint64_t SharedRetainedMemoryLease::ResidentBytes() const noexcept
{
    return _state ? _state->residentBytes.load(std::memory_order_acquire) : 0;
}

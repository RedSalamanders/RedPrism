#pragma once

#include <cstddef>

namespace RedPrismTestCapacity
{
// Keep capacity checks observable at runtime rather than folding the literal comparison away.
// This local value is not shared synchronization. Preserve the target's existing volatile model.
// Owner: test maintainers; review: 2027-01-31.
#if defined(_M_ARM64)
#pragma warning(push)
#pragma warning(disable : 4746)
#endif
[[nodiscard]] __declspec(noinline) inline std::size_t Observe(std::size_t capacity) noexcept
{
    const volatile std::size_t observed = capacity;
    return observed;
}
#if defined(_M_ARM64)
#pragma warning(pop)
#endif
} // namespace RedPrismTestCapacity

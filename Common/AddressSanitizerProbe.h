#pragma once

#include <memory>

// Deliberately read freed storage so the isolated sanitizer child proves real instrumentation.
// This private observation is not synchronization; preserve the target's volatile model.
// Owner: test maintainers; review: 2027-01-31.
#if defined(_M_ARM64) && defined(__SANITIZE_ADDRESS__)
#pragma warning(push)
#pragma warning(disable : 4746)
#endif
__declspec(noinline) inline int RunAddressSanitizerProbe()
{
#if defined(__SANITIZE_ADDRESS__)
    auto allocation = std::make_unique<char[]>(8);
    allocation[0] = 1;
    volatile char* borrowed = allocation.get();
    allocation.reset();
    return *borrowed;
#else
    return 2;
#endif
}
#if defined(_M_ARM64) && defined(__SANITIZE_ADDRESS__)
#pragma warning(pop)
#endif

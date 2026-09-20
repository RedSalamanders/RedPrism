#pragma once

#include <memory>

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

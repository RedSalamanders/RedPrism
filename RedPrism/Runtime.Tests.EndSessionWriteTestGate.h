#pragma once

#include <array>
#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Optional harness-only handshake. A helper releases the actual producer when confirmed session end enters
// its bounded native wait. It neither pumps owner completions nor substitutes a result for the file writer.
struct EndSessionWriteTestGate final
{
    wil::unique_handle waitEntered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> waitVisits{0};
    std::atomic<DWORD> waitResult{WAIT_FAILED};
    struct Observation final
    {
        ULONGLONG deadline = 0;
        DWORD remaining = 0;
        DWORD result = WAIT_FAILED;
    };
    // Owner-only records. The helper reads only the event and atomic fields above.
    std::array<Observation, 2> waits{};
    bool traceTruncated = false;
};

#pragma once

#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Test-only gates at actual owner pre-copy and joined detached-worker boundaries. No production wait.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CloneReadbackTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5f2;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool holdAfterCopy = false;
    bool throwCaptureFailure = false;
    bool observeAdoption = false; // Message 2: source lease adopted; message 3: opening completed and Down replayed.
    unsigned int rebindsBeforeCopy = 0;
    JobResult result;
    std::atomic<unsigned int> captures{0};
    std::atomic<unsigned int> preparations{0};
    std::atomic<unsigned int> recoveries{0};
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> retired{false};
    std::atomic<bool> retiredOffOwner{false};
};
#pragma warning(pop)

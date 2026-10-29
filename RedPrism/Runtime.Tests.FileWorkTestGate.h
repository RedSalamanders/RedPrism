#pragma once

#include "Jobs.h"

#include <atomic>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Immutable harness controls captured by one job. Production holds no gate. The owner releases only the event.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct FileWorkTestGate final
{
    static constexpr std::uint32_t kMessage = WM_APP + 0x4B4;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool afterPreparation = false;
    bool throwAllocationFailure = false;
    JobResult result;
    std::atomic<unsigned int> visits{0};
};
#pragma warning(pop)

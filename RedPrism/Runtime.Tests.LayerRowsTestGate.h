#pragma once

#include <atomic>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Absent in production. Events exercise the actual admitted capture, worker and owner publication lanes.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct LayerRowsTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5d1;
    wil::unique_handle release;
    bool hold = true;
    bool holdAfterPrepare = false;
    bool throwAllocate = false;
    bool throwCapture = false;
    bool throwPrepare = false;
    std::uint64_t reservationBytes = 0;
    std::atomic<unsigned int> captures{0}, preparations{0}, publications{0};
    std::atomic<bool> allocated{false}, allocatedOffOwner{false}, allocationReserved{false};
    std::atomic<bool> captureReserved{false}, retired{false}, retiredOffOwner{false};
};
#pragma warning(pop)

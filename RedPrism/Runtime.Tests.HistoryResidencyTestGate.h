#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Absent in production. Tiny fixture hooks use the real admission/staging/owner and worker retirement paths.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct HistoryResidencyTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5c1;
    wil::unique_handle release;
    bool hold = true;
    bool throwCaptureFailure = false;
    bool throwPrepareFailure = false;
    std::uint64_t reservationBytes = 0;
    std::atomic<unsigned int> captures{0}, preparations{0}, refreshes{0};
    std::atomic<bool> reservationHeld{false}, retired{false}, retiredOffOwner{false};
    std::atomic<bool> published{false}, cancelled{false}, failed{false}, stale{false};
};
#pragma warning(pop)

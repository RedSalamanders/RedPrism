#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct HistoryRestoreTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4b4;
    wil::unique_handle release;
    JobResult result;
    bool hold = true, throwCaptureFailure = false;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    Raster lateResidencySource;
    bool throwResidencyRefresh = false, cancelResidencyRefresh = false;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> reservationHeld{false}, refreshReservationHeld{false}, retired{false}, retiredOffOwner{false};
    std::atomic<bool> shutdownObserved{false};
    std::atomic<unsigned int> visits{0}, historyFaultHits{0}, captures{0}, residentGrowths{0}, residencyRefreshes{0};
};
#pragma warning(pop)

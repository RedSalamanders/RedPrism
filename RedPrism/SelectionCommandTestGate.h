#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct SelectionCommandTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4c5;
    wil::unique_handle release;
    JobResult result;
    bool hold = true, throwCaptureFailure = false;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    Raster lateResidencySource;
    bool throwResidencyRefresh = false, cancelResidencyRefresh = false;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> reservationHeld{false}, refreshReservationHeld{false}, retired{false}, retiredOffOwner{false};
    std::atomic<unsigned int> visits{0}, captures{0}, historyFaultHits{0}, residentGrowths{0}, residencyRefreshes{0};
};

#pragma once

#include "Jobs.h"
#include "LayerMetadataWork.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct LayerMetadataTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5c0;
    wil::unique_handle release;
    JobResult result;
    bool hold = true, throwCaptureFailure = false;
    bool throwResidencyRefresh = false, cancelResidencyRefresh = false;
    Raster lateResidencySource; // small literal harness owner; production never supplies this gate
    std::optional<LayerMetadataPrepareStage> throwMetadataStage;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> reservationHeld{false}, retired{false}, retiredOffOwner{false};
    std::atomic<unsigned int> visits{0}, faultHits{0}, captures{0}, residentGrowths{0}, residencyRefreshes{0};
    std::atomic<bool> refreshReservationHeld{false};
};

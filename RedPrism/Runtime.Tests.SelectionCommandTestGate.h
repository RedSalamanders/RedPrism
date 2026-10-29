#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <memory>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class SharedPublicationMemory;

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
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
    // Worker observations are read only after its visits counter or completed owner acknowledgement.
    bool observeInverseOutputLifetime = false;
    std::weak_ptr<const void> inverseOutlineOwner;
    std::weak_ptr<SharedPublicationMemory> inverseMemoryOwner;
    std::atomic<const void*> inversePointBuffer{nullptr};
    std::atomic<bool> inverseOutlinePreparedOffOwner{false};
    std::uint64_t inversePublishedBytes = 0;
    unsigned int inversePublishedOutputs = 0;
    bool inverseOutputBoundToOriginal = true;
    std::atomic<bool> reservationHeld{false}, refreshReservationHeld{false}, retired{false}, retiredOffOwner{false};
    std::atomic<unsigned int> visits{0}, captures{0}, historyFaultHits{0}, residentGrowths{0}, residencyRefreshes{0};
};
#pragma warning(pop)

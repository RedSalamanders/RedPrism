#pragma once

#include "Jobs.h"
#include "ShapeEditWork.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ShapeWorkTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x57;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> retired{false}, retiredOffOwner{false};
    std::atomic<unsigned int> historyFaultHits{0};
    std::atomic<unsigned int> captures{0}, preparations{0}, residentGrowths{0}, residencyRefreshes{0};
    std::atomic<bool> refreshReservationHeld{false}, refreshTicketMatches{false};
    std::optional<JobTicket> capturedTicket; // Owner-only capture/completion witness.
    Raster lateResidencySource;
    bool throwResidencyRefresh = false, cancelResidencyRefresh = false;
    bool afterPreparation = false, throwAllocationFailure = false, throwAdmissionFailure = false;
    bool throwCaptureFailure = false, captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
    std::optional<DocumentHistory::PrepareStage> historyFailure;
    JobResult result{};
};
#pragma warning(pop)

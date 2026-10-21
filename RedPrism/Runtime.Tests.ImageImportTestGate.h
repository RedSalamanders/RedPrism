#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class ImageImportTestPhase
{
    Probe,
    Prepare
};
struct ImageImportTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4B8;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0};
    ImageImportTestPhase phase = ImageImportTestPhase::Probe;
    bool afterPreparation = false;
    bool throwAllocationFailure = false;
    bool throwAdmissionFailure = false;
    bool throwCaptureFailure = false;
    bool captureReservationHeld = false; // observed in the real admitted owner capture hook
    std::atomic<bool> retired{false}, retiredOffOwner{false};
    std::atomic<unsigned int> historyFaultHits{0};
    std::atomic<unsigned int> captures{0}, preparations{0}, residentGrowths{0}, residencyRefreshes{0};
    std::atomic<bool> refreshReservationHeld{false};
    std::atomic<bool> refreshTicketMatches{false};
    std::optional<JobTicket> capturedTicket; // owner-only capture/completion witness
    Raster lateResidencySource;
    bool throwResidencyRefresh = false, cancelResidencyRefresh = false;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    std::uint64_t preparationReservationBytes = 0; // owner-only assertion
    JobResult result{};
};

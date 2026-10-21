#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Owned harness gate. Absent in production; a worker posts the message after copying, while its rows remain lent.
struct UtilityWorkTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4b2;
    wil::unique_handle release;
    std::atomic<unsigned int> visits{0};
    JobResult result;
    bool throwCaptureFailure = false;
    bool throwProbeCaptureFailure = false;
    bool throwProbeAllocationFailure = false;
    bool holdProbe = false;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    std::atomic<bool> captured{false};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<unsigned int> retiredNodes{0}, retiredOwnerNodes{0};
    std::atomic<unsigned int> probePreparations{0}, readbackCaptures{0};
    std::uint64_t probeReservationBytes = 0;
    std::uint64_t readbackReservationBytes = 0;
    bool probeReservationHeld = false;
};

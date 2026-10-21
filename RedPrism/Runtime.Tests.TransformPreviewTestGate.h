#pragma once

#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct TransformPreviewTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5B;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> retired{false}, retiredOffOwner{false};
    bool afterPreparation = false, throwAllocationFailure = false, throwCaptureFailure = false;
    bool throwAdmissionFailure = false, captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
    JobResult result{};
};

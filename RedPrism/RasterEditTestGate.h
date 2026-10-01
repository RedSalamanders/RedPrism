#pragma once

#include "Jobs.h"
#include "RasterEditWork.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Null in production; immutable after submission except the event/atomics and owner-only capture evidence.
struct RasterEditTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4B9;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadRetiredOffOwner{false};
    bool afterPreparation = false;
    bool throwAllocationFailure = false;
    bool throwAdmissionFailure = false;
    bool throwCaptureFailure = false;
    bool captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
    std::optional<RasterEditStatus> status;
    JobResult result{};
};

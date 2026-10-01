#pragma once

#include "Jobs.h"
#include "RasterEditWork.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class PixelMoveWorkTestKind : std::uint8_t
{
    Preview,
    Commit,
};

// Harness-only worker gate. No polling or timeout performance assertion; owners survive acknowledgement.
struct PixelMoveWorkTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 54;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    PixelMoveWorkTestKind kind = PixelMoveWorkTestKind::Preview;
    bool afterPreparation = false;
    bool throwAdmissionFailure = false;
    bool throwCaptureFailure = false;
    bool throwAllocationFailure = false;
    JobResult result;
    std::optional<RasterEditStatus> status;
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadReleaseRequired{true};
    std::atomic<bool> payloadRetiredOffOwner{false};
    bool captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
};

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

enum class GradientWorkTestKind : std::uint8_t
{
    Preview,
    Commit,
};

// Harness-only worker gate. No polling or timeout performance assertion; owners survive acknowledgement.
struct GradientWorkTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 53;
    // One worker owns the preview ticket at a time. Each visit consumes its own owner release, so a
    // coalesced preview cannot pass a later gate using the preceding visit's signaled event.
    wil::unique_handle release{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    GradientWorkTestKind kind = GradientWorkTestKind::Preview;
    bool afterPreparation = false;
    bool throwAdmissionFailure = false;
    bool throwCaptureFailure = false;
    bool throwAllocationFailure = false;
    JobResult result;
    std::optional<RasterEditStatus> status;
    std::atomic<unsigned int> visits{0};
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadRetiredOffOwner{false};
    bool captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
};

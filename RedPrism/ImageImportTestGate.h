#pragma once

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
    bool captureReservationHeld = false;           // observed in the real admitted owner capture hook
    std::uint64_t preparationReservationBytes = 0; // owner-only assertion
    JobResult result{};
};

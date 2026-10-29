#pragma once

#include "CanvasSize.h"
#include "Jobs.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Harness-only event gate. Every production job has a null gate. The gate is immutable after submission apart
// from its events and atomics, and stays owned by the job through acknowledgement and final apply/drop.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SizingWorkTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 0x4B3;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool afterPreparation = false;
    bool throwAllocationFailure = false;
    bool throwCaptureFailure = false;
    std::optional<DocumentHistory::PrepareStage> historyFailure;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> captureReservationHeld{false};
    std::atomic<unsigned int> historyFaultHits{0};
    std::atomic<bool> payloadAllocatedOffOwner{false};
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadRetiredOffOwner{false};
    std::atomic<bool> failureRetiredByOriginalWorker{false};
    std::atomic<bool> finalWaitingForDisplay{false};
    JobResult result{};
    std::optional<ResizeStatus> status;
    std::atomic<unsigned int> visits{0};
};
#pragma warning(pop)

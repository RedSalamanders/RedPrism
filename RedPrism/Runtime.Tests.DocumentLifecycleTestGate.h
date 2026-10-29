#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Harness-only fixed event gate. A production job has no gate; immutable controls are set before submission.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct DocumentLifecycleTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 0x5C;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool afterPreparation = false;
    bool throwPreparationFailure = false;
    bool throwCaptureFailure = false;
    bool throwAdmissionFailure = false;
    std::optional<DocumentHistory::PrepareStage> historyFailure;
    JobResult result{};
    std::uint64_t reservationBytes = 0;
    std::atomic<unsigned int> visits{0}, historyFaultHits{0}, retired{0};
    std::atomic<bool> captureReservationHeld{false}, retiredOffOwner{false};
};
#pragma warning(pop)

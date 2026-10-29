#pragma once

#include "ClipboardTransfer.h"
#include "Jobs.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class ClipboardWorkPhase : std::uint8_t
{
    CopyCaptured,
    CopyPrepared,
    CopyPrivateApplied,
    CopyEncoded,
    CopyPublished,
    MergedSnapshot,
    PrefillInspected,
    PasteProbe,
    PasteInspected,
    PasteCaptured,
    PasteCopied,
    PastePrepared,
    CopyPublicationSucceeded, // only a successful native publication, after CloseClipboard and owner teardown
};

// Harness-only event gate. Production does not allocate a gate or enter its waits.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ClipboardWorkTestGate final
{
    static constexpr UINT kMessage = WM_APP + 55;
    static constexpr UINT kPublicationMessage = WM_APP + 0x5f3;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ClipboardWorkPhase phase = ClipboardWorkPhase::CopyCaptured;
    JobResult result;
    bool throwAdmissionFailure = false;
    bool throwMainAdmissionFailure = false;
    bool throwCaptureFailure = false;
    bool throwWorkerFailure = false;
    bool forceUnavailableProbeSequence = false; // negative query result; never fakes a transfer
    std::optional<ClipboardTransfer::Fault> fault;
    std::uint32_t nativeError = ERROR_ACCESS_DENIED;
    std::atomic<unsigned int> openFailures{0};
    std::atomic<unsigned int> forcedBusyAttempts{0};
    std::atomic<unsigned int> visits{0}, openAttempts{0}, emptyAttempts{0}, publications{0};
    std::atomic<unsigned int> openedScopes{0}, openAttemptsAtEmpty{0};
    std::atomic<unsigned int> receiptPlacements{0}, pngPlacements{0}, dibPlacements{0};
    std::atomic<unsigned int> retiredNodes{0}, retiredOwnerNodes{0};
    std::atomic<HANDLE> workerShutdownEvent{nullptr}; // borrowed only until the gated callback acknowledges
    std::atomic<bool> workerDibRead{false};           // native inspection read boundary; delayed-producer harness only
    std::uint64_t reservationBytes = 0, probeReservationBytes = 0;
    bool captureReservationHeld = false;
    unsigned int detachedCaptures = 0;
    bool utilityReleasedBeforeEncoding = false;
    // Configured before submission; absent in production. Shares the existing release event.
    std::optional<ClipboardTransfer::Stage> publicationStage;
    DWORD publicationOwnerThread = 0;
    std::atomic<unsigned int> publicationVisits{0};
    std::atomic<bool> publicationGateFailed{false};
};
#pragma warning(pop)

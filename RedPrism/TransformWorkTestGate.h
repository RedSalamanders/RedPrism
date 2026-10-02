#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class TransformWorkTestKind : std::uint8_t
{
    OpenFloating,
    Commit,
    Cancel,
    OpenDuplicate
};

struct TransformWorkTestGate final
{
    TransformWorkTestKind kind = TransformWorkTestKind::Commit;
    static constexpr UINT kMessage = WM_APP + 0x5A;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0}, historyFaultHits{0};
    std::atomic<bool> retired{false}, retiredOffOwner{false};
    std::atomic<bool> restoreRetired{false}, restoreRetiredOffOwner{false};
    bool afterPreparation = false, throwAllocationFailure = false, throwAdmissionFailure = false;
    bool throwCaptureFailure = false, captureReservationHeld = false;
    std::uint64_t reservationBytes = 0;
    std::optional<DocumentHistory::PrepareStage> historyFailure;
    JobResult result{};
};

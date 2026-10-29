#pragma once

#include "ContinuationReceipt.h"
#include "Document.h"
#include "UiCancellationFrame.h"
#include "UiCancellationObservation.h"
#include "UiCancellationRefusal.h"
#include "UiTiming.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

// Opt-in only. The execution observer owns no Application pointer; its event and fixed log survive joined Close.
// Trial fields below belong exclusively to the UI owner and are never accessed by a worker callback.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct UiCancellationBenchmark final
{
    enum class Phase
    {
        Preparing,
        AwaitingPreview,
        AwaitingRefusal,
        ApplyPosted,
        AwaitingBody,
        DelayArmed,
        EscapePosted,
        Settling,
        Done
    };
    std::unique_ptr<UiCancellationObservation> observation;
    std::optional<UiCancellationBody> body;
    UiCancellationFrame firstFrame;
    std::uint64_t acceptancePresentOrdinal = 0;
    std::size_t cursor = 0; // Write-once log: never rewind or reuse published storage between trials.
    Phase phase = Phase::Preparing;
    std::uint64_t opening = 0;
    std::uint64_t documentSerial = 0;
    std::optional<DocumentReadAuthority> authority;
    std::optional<JobTicket> original;
    std::optional<ContinuationReceipt> applyReceipt;
    std::optional<ContinuationReceipt> cancelReceipt;
    std::uintptr_t nextTimer = 64;
    std::uintptr_t activeTimer = 0;
    std::int64_t deadline = -1;
    std::int64_t postedAt = -1;
    std::int64_t dispatchBegin = -1;
    std::int64_t dispatchEnd = -1;
    std::int64_t stopBegin = -1;
    std::int64_t stopAccepted = -1;
    std::int64_t receiptAccepted = -1;
    std::int64_t feedbackPresented = -1;
    std::int64_t settledAt = -1;
    bool dispatchActive = false;
    bool tokenAccepted = false;
    bool cardHiddenAtAcceptance = false;
    bool ambientInput = false;
    bool previewEnabled = false;
    unsigned int escapeDowns = 0;
    unsigned int escapeUps = 0;
    unsigned int deadlineRearms = 0;
    int preparationBudgetNotices = 0;
    UiCancellationRefusalKind expectedRefusal = UiCancellationRefusalKind::None;

    // Called only after all previous owner/worker/GPU work settles. The event/log remain immutable and live.
    void ResetTrialOwner() noexcept
    {
        body.reset();
        firstFrame = {};
        acceptancePresentOrdinal = 0;
        original.reset();
        applyReceipt.reset();
        cancelReceipt.reset();
        authority.reset();
        phase = Phase::Preparing;
        opening = documentSerial = 0;
        activeTimer = 0;
        deadline = postedAt = dispatchBegin = dispatchEnd = stopBegin = stopAccepted = receiptAccepted =
            feedbackPresented = settledAt = -1;
        dispatchActive = tokenAccepted = cardHiddenAtAcceptance = ambientInput = previewEnabled = false;
        escapeDowns = escapeUps = deadlineRearms = 0;
        preparationBudgetNotices = 0;
        expectedRefusal = UiCancellationRefusalKind::None;
    }
};
#pragma warning(pop)

[[nodiscard]] inline bool IsUiCancellationBenchmark(std::wstring_view mode) noexcept
{
    return mode == L"ui-cancel" || mode == L"ui-cancel45" || mode == L"ui-cancel100" || mode == L"ui-cancel-smoke" ||
           mode == L"ui-cancel-prepared" || mode == L"ui-cancel-prepared45" || mode == L"ui-cancel-prepared100" ||
           mode == L"ui-cancel-prepared-smoke";
}

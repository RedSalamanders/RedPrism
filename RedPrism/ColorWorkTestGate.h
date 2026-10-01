#pragma once

#include "Document.h"
#include "Jobs.h"

#include <array>
#include <atomic>
#include <memory>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

enum class ColorWorkTestKind
{
    Preview,
    Histogram,
    Commit
};

// Harness-owned events travel with the submitted job. Production submissions have no gate and never wait here.
struct ColorWorkTestGate final
{
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle computed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ColorWorkTestKind kind = ColorWorkTestKind::Preview;
    JobResult releasedResult{}; // immutable after submission; harness can inject a runner failure
    bool throwAllocationFailure = false;
    bool throwCaptureFailure = false;
    std::optional<DocumentHistory::PrepareStage> historyFailure;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> captureReservationHeld{false};
    std::atomic<unsigned int> historyFaultHits{0};
    std::atomic<bool> payloadRetired{false};
    std::atomic<bool> payloadRetiredOffOwner{false};
    std::atomic<bool> unchangedCommit{false};
    std::atomic<unsigned int> visits{0};
};

[[nodiscard]] JobResult PassColorWorkTestGate(const std::shared_ptr<ColorWorkTestGate>& gate, ColorWorkTestKind kind,
                                              JobContext& context) noexcept;

// Harness-only admission wait. Capture needs an owner completion pass after reservation. Continuation tests
// can omit UI actions to keep ordinary queued actions and receipt publication at their asserted boundaries.
[[nodiscard]] inline bool WaitForColorWorkTestEvent(JobScheduler& jobs, HANDLE event, MessageDispatch dispatch,
                                                    void* context, bool runActions = true) noexcept
{
    const auto start = GetTickCount64();
    for (;;)
    {
        const auto messages = PumpMessages(dispatch, context);
        if (messages.quit)
        {
            PostQuitMessage(messages.exitCode);
            return false;
        }
        (void)DrainCompletions(jobs);
        if (runActions)
            (void)RunUiActions(jobs);
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
            return true;
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= 10000 || !jobs.WakeHandle())
            return false;
        if (runActions && jobs.HasUiWork())
            continue;
        const std::array<HANDLE, 2> wake{event, jobs.WakeHandle()};
        const auto waited =
            MsgWaitForMultipleObjectsEx(static_cast<DWORD>(wake.size()), wake.data(),
                                        static_cast<DWORD>(10000 - elapsed), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waited > WAIT_OBJECT_0 + wake.size())
            return false;
    }
}

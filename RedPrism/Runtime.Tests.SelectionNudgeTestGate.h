#pragma once

#include "Document.h"
#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct SelectionNudgeTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4cb;
    wil::unique_handle release;
    JobResult result;
    bool hold = true;
    bool throwCaptureFailure = false;
    std::optional<DocumentHistory::PrepareStage> throwHistoryStage;
    std::uint64_t reservationBytes = 0;
    std::atomic<bool> reservationHeld{false};
    std::atomic<bool> retired{false};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<unsigned int> visits{0};
    std::atomic<unsigned int> historyFaultHits{0};
    std::atomic<unsigned int> captures{0};
};

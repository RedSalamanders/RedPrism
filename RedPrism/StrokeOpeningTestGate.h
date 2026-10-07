#pragma once

#include "Jobs.h"

#include <atomic>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Test-only pause after a real StrokeOpeningWork worker has prepared the typed publication.
// Production waits for release OR context.shutdownEvent, with its existing 10 s deadlock bound.
struct StrokeOpeningTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x5f3;

    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool holdAfterPreparation = false;      // Set before StartStrokeOpening submits the worker.
    std::optional<JobResult> failureResult; // Optional deterministic job-result injection, if needed later.

    std::atomic<unsigned int> captures{0};
    std::atomic<unsigned int> prepares{0};
    std::atomic<unsigned int> offowner{0};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<bool> retired{false};
};

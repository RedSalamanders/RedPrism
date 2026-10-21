#pragma once

#include "../Common/Jobs.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Test hook contract: after real final coverage preparation succeeds, the worker increments prepares,
// records offowner when it differs from ownerThread, signals reached, notifies owner work, and waits on
// release plus the scheduler shutdown event for at most 10 seconds. It checks cancellation after the wait.
// The worker-retirement payload records retiredOffOwner/retired when its real destructor runs.
struct StrokeFinishTestGate final
{
    enum class SampleBoundary : std::uint8_t
    {
        None,
        OpeningPrepared,
        FinalComposed,
        Prepared,
        Composed,
        PreviewRecorded,
        ReturningDraft
    };
    static constexpr std::uint32_t kWaitTimeoutMs = 10000;

    explicit StrokeFinishTestGate(bool hold) noexcept : ownerThread(GetCurrentThreadId()), holdAfterPreparation(hold) {}

    wil::unique_handle reached{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle quarantined{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle cancelled{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    const DWORD ownerThread;
    const bool holdAfterPreparation;
    std::optional<JobResult> failureResult;
    SampleBoundary holdSample = SampleBoundary::None; // Immutable after original admission.
    unsigned holdSampleNumber = 1;
    std::atomic<unsigned> sampleBoundaryVisits{0};
    std::atomic<unsigned> sampleBoundaryOffOwner{0};
    std::atomic<unsigned> prepares{0};
    std::atomic<unsigned> offowner{0};
    std::atomic<unsigned> samplesPrepared{0};
    std::atomic<unsigned> samplesOffOwner{0};
    std::atomic<unsigned> samplesAcknowledged{0};
    std::atomic<unsigned> previewsPublished{0};
    std::atomic<unsigned> previewsReplaced{0};
    std::atomic<unsigned> quarantines{0};
    std::atomic<std::int32_t> lastRetirementRefusal{0};
    std::atomic<unsigned> cancellationAcknowledgements{0};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<bool> retired{false};
};

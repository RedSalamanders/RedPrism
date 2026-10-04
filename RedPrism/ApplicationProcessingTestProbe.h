#pragma once

#include "MemoryBudget.h"
#include "RetryWaitQueue.h"

#include <cstdint>

enum class ApplicationProcessingStage : std::uint8_t
{
    Trace,
    Jobs,
    OwnerWork,
    GpuWork,
    RetryWait,
    Continuations,
    DeferredRelease,
    Recycling,
    Count,
};

// Borrowed startup-only controls. afterStage runs only after the named subsystem successfully initializes;
// it tests acquired-prefix cleanup, not failures inside that subsystem. retryWait uses the queue's existing
// reached event/registration fault seam. Its context outlives InitializeProcessing and Shutdown.
struct ApplicationProcessingTestProbe final
{
    void* context = nullptr;
    std::int32_t (*afterStage)(void*, ApplicationProcessingStage) noexcept = nullptr;
    RetryWaitObserver retryWait;
    MemorySource memory; // Optional fixed admission source, alive through scheduler Close.
};

#pragma once

#include "MemoryBudget.h"
#include "PathReadWait.h"
#include "RetryWaitQueue.h"

#include <cstdint>
#include <memory>

struct LassoWorkTestGate;
class RecordedDisplayGraph;

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
    NativeRead,
    UtilityReadback,
    RecordedCommands,
    Count,
};

enum class ApplicationResponsiveSourceStage : std::uint8_t
{
    BeforeAcquisition,
    LayerRows,
    Display,
    Stroke,
    ReadyToEnable,
    SelectionOutline,
    LassoDraft
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
    // Actual Run startup checkpoint, after document/settings and real runtime notification subscription.
    // Absent in production. Its borrowed context survives Run and Shutdown; failure exercises acquired cleanup.
    std::int32_t (*afterRuntimeSources)(void*) noexcept = nullptr;
    // Explicit actual-Run startup fixture only. Borrowed context survives Run and joined Shutdown.
    bool responsiveSourcesForTest = false;
    bool responsiveSelectionSourcesForTest = false; // Explicit complete-source cold-start fixture only.
    std::int32_t (*afterResponsiveSourceStage)(void*, ApplicationResponsiveSourceStage) noexcept = nullptr;
    // Owner-only observation after an actual committed graph becomes visible. Absent in production.
    void (*committedDisplayPublished)(void*, RecordedDisplayGraph*) noexcept = nullptr;
    unsigned int workerCount = 0; // Startup-only; zero preserves the production automatic count.
    // Reached lasso fixture controls, configured before each admitted operation. The borrowed probe and
    // this shared owner survive joined Close; absent in production and no Application layout cost.
    std::shared_ptr<LassoWorkTestGate> lassoWork;
    PathReadWaitObserver pathReadWait; // Reached-resource faults, alive through partial-startup Close.
};

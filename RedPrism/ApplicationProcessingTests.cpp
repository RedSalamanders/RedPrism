#include "Application.h"
#include "ApplicationProcessingTestProbe.h"
#include "ApplicationShutdownTestProbe.h"

#include "DeferredRelease.h"

#include <array>
#include <cstdio>
#include <format>
#include <memory>

namespace
{
constexpr std::size_t kStageCount = static_cast<std::size_t>(ApplicationProcessingStage::Count);
constexpr std::size_t kRetryCaseCount = 2 * RetryWaitQueue::kCapacity;
constexpr HRESULT kCheckpointError = HRESULT_FROM_WIN32(ERROR_CANCELLED);
constexpr HRESULT kRetryError = E_ACCESSDENIED;

struct ProcessingCase final
{
    ApplicationProcessingStage failAfter = ApplicationProcessingStage::Count;
    bool failRetry = false;
    RetryWaitFault retryFault = RetryWaitFault::EventCreation;
    std::size_t retrySlot = 0;
    std::array<ApplicationProcessingStage, kStageCount> stages{};
    std::size_t stageVisits = 0;
    std::size_t retryVisits = 0;
    bool retryReached = false;

    static std::int32_t AfterStage(void* opaque, ApplicationProcessingStage stage) noexcept
    {
        auto& state = *static_cast<ProcessingCase*>(opaque);
        if (state.stageVisits < state.stages.size())
            state.stages[state.stageVisits] = stage;
        ++state.stageVisits;
        return stage == state.failAfter ? kCheckpointError : S_OK;
    }

    static std::int32_t RetryFault(void* opaque, RetryWaitFault fault, std::size_t slot) noexcept
    {
        auto& state = *static_cast<ProcessingCase*>(opaque);
        ++state.retryVisits;
        if (state.failRetry && fault == state.retryFault && slot == state.retrySlot)
        {
            state.retryReached = true;
            return kRetryError;
        }
        return S_OK;
    }
};
} // namespace

int Application::RunStandaloneProcessingSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    for (std::size_t caseIndex = 0; caseIndex < kStageCount + kRetryCaseCount + 2; ++caseIndex)
    {
        ProcessingCase state;
        const bool checkpointCase = caseIndex < kStageCount;
        const bool retryCase = caseIndex >= kStageCount && caseIndex < kStageCount + kRetryCaseCount;
        const bool neverStarted = caseIndex == kStageCount + kRetryCaseCount + 1;
        char label[64]{};
        if (checkpointCase)
        {
            state.failAfter = static_cast<ApplicationProcessingStage>(caseIndex);
            (void)std::format_to_n(label, sizeof(label) - 1, "checkpoint-{}", caseIndex);
        }
        else if (retryCase)
        {
            const std::size_t retryIndex = caseIndex - kStageCount;
            state.failRetry = true;
            state.retrySlot = retryIndex % RetryWaitQueue::kCapacity;
            state.retryFault =
                retryIndex < RetryWaitQueue::kCapacity ? RetryWaitFault::EventCreation : RetryWaitFault::RegisterWait;
            (void)std::format_to_n(label, sizeof(label) - 1, "retry-{}-{}",
                                   state.retryFault == RetryWaitFault::EventCreation ? "event" : "registration",
                                   state.retrySlot);
        }
        else
            (void)std::format_to_n(label, sizeof(label) - 1, "{}", neverStarted ? "never-started" : "complete");
        const auto expect = [&](bool condition, const char* name) noexcept
        {
            ++checks;
            failures += condition ? 0 : 1;
            std::printf("%s application-processing.%s.%s\n", condition ? "PASS" : "FAIL", label, name);
        };
        expect(!DeferredReleaseDetail::sink.enqueue, "owns-no-surrogate-sink");
        APTTYPE original{};
        APTTYPEQUALIFIER originalQualifier{};
        const HRESULT originalApartment = CoGetApartmentType(&original, &originalQualifier);
        ApplicationProcessingTestProbe control{
            &state, ProcessingCase::AfterStage, {&state, ProcessingCase::RetryFault, nullptr, nullptr}};
        ApplicationShutdownTestProbe retirement;
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &control;
        owner->_shutdownTestProbe = &retirement;
        const HRESULT result = neverStarted ? S_FALSE : owner->InitializeProcessing();
        const HRESULT expectedResult = neverStarted     ? S_FALSE
                                       : checkpointCase ? kCheckpointError
                                       : retryCase      ? kRetryError
                                                        : S_OK;
        expect(result == expectedResult, "exact-stage-result");
        const std::size_t expectedStages = neverStarted     ? 0
                                           : checkpointCase ? caseIndex + 1
                                           : retryCase      ? 4
                                                            : kStageCount;
        bool exactPrefix = state.stageVisits == expectedStages;
        for (std::size_t stage = 0; stage < expectedStages; ++stage)
            exactPrefix = exactPrefix && state.stages[stage] == static_cast<ApplicationProcessingStage>(stage);
        expect(exactPrefix, "each-acquired-stage-once-no-later-stage");
        const std::size_t expectedRetryVisits =
            neverStarted || (checkpointCase && caseIndex < 4) ? 0
            : retryCase ? 2 * state.retrySlot + (state.retryFault == RetryWaitFault::EventCreation ? 1 : 2)
                        : 2 * RetryWaitQueue::kCapacity;
        expect(state.retryVisits == expectedRetryVisits && state.retryReached == retryCase,
               "retry-fault-reached-after-exact-successful-prefix");
        const bool poolExpected = !neverStarted && (!checkpointCase || caseIndex >= 1);
        const bool gpuExpected = !neverStarted && (!checkpointCase || caseIndex >= 3);
        const bool sourceExpected = !neverStarted && !retryCase && (!checkpointCase || caseIndex >= 5);
        const bool sinkExpected = !neverStarted && !retryCase && (!checkpointCase || caseIndex >= 6);
        expect((owner->_jobs.WorkerCount() != 0) == poolExpected &&
                   (owner->_jobs.WakeHandle() != nullptr) == poolExpected,
               "pool-and-wake-live-for-acquired-prefix");
        expect(owner->_gpuWorkInitialized == gpuExpected && owner->_hostSourceRegistered == sourceExpected &&
                   (DeferredReleaseDetail::sink.enqueue != nullptr) == sinkExpected,
               "gpu-source-and-sink-match-acquired-prefix");
        expect(!owner->_window && !owner->_chrome && !owner->_rendererReady, "startup-prefix-has-no-window-or-chrome");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_hostSourceRegistered && !owner->_gpuWorkInitialized,
               "first-shutdown-completes-and-detaches-sources");
        expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !DeferredReleaseDetail::sink.enqueue,
               "pool-events-reservations-and-sink-retired");
        expect(!owner->_retryWait.HasPending() && !owner->_retryWait.HasReady() && !owner->_gpuWork.HasPending() &&
                   !owner->_gpuWork.HasReady() && owner->_gpuWork.LentCount() == 0 && !owner->_ownerWork.HasPending() &&
                   !owner->_ownerWork.HasReady(),
               "partial-queues-have-no-receiver-or-ready-work");
        // An empty route can be reattached; an overlooked old registration would refuse this exact source.
        const bool retryDetached = owner->_ownerWork.Attach(owner->_retryWait);
        const bool retryRemoved = retryDetached && owner->_ownerWork.Detach(owner->_retryWait);
        expect(retryDetached && retryRemoved, "retry-route-detached-after-shutdown");
        const bool gpuDetached = owner->_ownerWork.Attach(owner->_gpuWork);
        const bool gpuRemoved = gpuDetached && owner->_ownerWork.Detach(owner->_gpuWork);
        expect(gpuDetached && gpuRemoved, "gpu-route-detached-after-shutdown");
        expect(retirement.ownerCalls == (poolExpected ? 1u : 0u) &&
                   retirement.workerCalls.load() == (poolExpected ? 1u : 0u) &&
                   (!poolExpected || (retirement.offOwner.load() && retirement.mta.load())),
               "acquired-pool-acknowledges-exactly-one-final-mta-hook");
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        expect(SUCCEEDED(originalApartment) && SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
                   apartment == original && qualifier == originalQualifier,
               "caller-apartment-preserved");
        const std::size_t stageVisits = state.stageVisits;
        const std::size_t retryVisits = state.retryVisits;
        owner->Shutdown();
        expect(owner->_shutdownComplete && retirement.ownerCalls == (poolExpected ? 1u : 0u) &&
                   retirement.workerCalls.load() == (poolExpected ? 1u : 0u) && stageVisits == state.stageVisits &&
                   retryVisits == state.retryVisits && !DeferredReleaseDetail::sink.enqueue,
               "second-shutdown-no-repeated-hooks-or-stage-visits");
        owner->_processingTestProbe = nullptr;
        owner->_shutdownTestProbe = nullptr;
    }
    std::printf("application-processing summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}

#include "Host.Tests.JobUiPolicy.h"

#include "../../Common/JobUiPolicy.h"
#include "../../Common/Jobs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include <windows.h>

namespace
{
bool SourceTestMemory(void*, MemorySnapshot& snapshot) noexcept
{
    // Source wake/idle assertions must not include independent OS pressure-notification callbacks.
    snapshot = {8ULL * 1024 * 1024 * 1024, 8ULL * 1024 * 1024 * 1024};
    return true;
}

void ExpectSource(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s job-ui-policy.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++failures;
    }
}

struct SourceProbe final : UiAction
{
    SourceProbe(JobScheduler& owner, std::vector<int>& trace, int marker, int& destroyed) noexcept
        : scheduler(&owner), order(&trace), value(marker), destructorCount(&destroyed)
    {
    }

    ~SourceProbe() override
    {
        ++*destructorCount;
    }

    JobScheduler* scheduler;
    std::vector<int>* order;
    int value;
    int* destructorCount;
    unsigned int runs = 0;
    unsigned int rearmCount = 0;
    bool inCallback = false;
    bool nestedActionRan = true;
    std::size_t nestedDrainCount = 99;
    bool unregisterInsideCallback = true;
    bool selfRearmAccepted = false;

    void Run() noexcept override
    {
        ++runs;
        order->push_back(value);
        inCallback = scheduler->InUiCallback();
        nestedDrainCount = DrainCompletions(*scheduler);
        nestedActionRan = RunUiActions(*scheduler);
        unregisterInsideCallback = scheduler->UnregisterUiActionSource(*this);
        if (rearmCount != 0)
        {
            --rearmCount;
            selfRearmAccepted = scheduler->RequestUiActionSource(*this);
        }
    }
};

struct OrdinaryProbe final : UiAction
{
    OrdinaryProbe(std::vector<int>& trace, int marker) noexcept : order(&trace), value(marker) {}
    std::vector<int>* order;
    int value;
    void Run() noexcept override
    {
        order->push_back(value);
    }
};

void TestUiActionSourceCapacity(int& failures)
{
    JobScheduler scheduler;
    std::vector<int> trace;
    int destroyed = 0;
    SourceProbe source{scheduler, trace, 100, destroyed};
    SourceProbe other{scheduler, trace, 200, destroyed};
    ExpectSource(!scheduler.RegisterUiActionSource(source) && !scheduler.RequestUiActionSource(source),
                 "source-uninitialized-scheduler-refuses-register-and-request", failures);
    const bool initialized = scheduler.Initialize(1, {}, {SourceTestMemory, nullptr}) >= 0;
    ExpectSource(initialized, "source-capacity-initialize", failures);
    if (!initialized)
    {
        return;
    }
    ExpectSource(scheduler.RegisterUiActionSource(source), "source-register", failures);
    ExpectSource(!scheduler.RegisterUiActionSource(source), "source-same-identity-register-refused", failures);
    ExpectSource(!scheduler.RegisterUiActionSource(other), "source-duplicate-refused-without-replacement", failures);
    ExpectSource(!scheduler.RequestUiActionSource(other), "source-wrong-identity-request-refused", failures);
    ExpectSource(!scheduler.UnregisterUiActionSource(other), "source-wrong-identity-unregister-refused", failures);
    ExpectSource(scheduler.IsIdle() && !scheduler.HasUiWork(), "source-registration-alone-is-idle", failures);
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    ExpectSource(wake != nullptr && WaitForSingleObject(wake, 0) == WAIT_TIMEOUT,
                 "source-registration-alone-does-not-signal-wake", failures);
    trace.reserve(JobScheduler::kUiActionCapacity + 4);

    for (std::size_t index = 0; index < JobScheduler::kUiActionCapacity; ++index)
    {
        std::unique_ptr<UiAction> action = std::make_unique<OrdinaryProbe>(trace, static_cast<int>(index + 1));
        ExpectSource(scheduler.PostUiAction(action) && !action, "source-fill-ordinary-queue-accepts", failures);
    }
    std::unique_ptr<UiAction> overflow = std::make_unique<OrdinaryProbe>(trace, 999);
    UiAction* const refusedIdentity = overflow.get();
    ExpectSource(!scheduler.PostUiAction(overflow) && overflow.get() == refusedIdentity,
                 "source-full-ordinary-queue-retains-refused-ninth", failures);
    // Consume the ordinary queue's auto-reset wake so the following observation proves the source request wakes it.
    ExpectSource(WaitForSingleObject(wake, 0) == WAIT_OBJECT_0, "source-ordinary-queue-signals-wake", failures);
    ExpectSource(scheduler.RequestUiActionSource(source), "source-request-while-ordinary-queue-full", failures);
    ExpectSource(scheduler.RequestUiActionSource(source) && scheduler.RequestUiActionSource(source),
                 "source-repeated-requests-coalesce", failures);
    ExpectSource(scheduler.HasUiWork() && !scheduler.IsIdle(), "source-ready-is-ui-work", failures);
    ExpectSource(wake != nullptr && WaitForSingleObject(wake, 0) == WAIT_OBJECT_0,
                 "source-request-signals-scheduler-wake", failures);

    ExpectSource(RunUiActions(scheduler), "source-runs-when-ordinary-queue-is-full", failures);
    ExpectSource(source.runs == 1 && trace.size() == 1 && trace[0] == 100,
                 "source-first-dispatch-precedes-full-ordinary-queue", failures);
    ExpectSource(source.inCallback && !source.nestedActionRan && source.nestedDrainCount == 0,
                 "source-callback-reentrant-drains-are-ignored", failures);
    ExpectSource(!source.unregisterInsideCallback, "source-unregister-refused-inside-callback", failures);

    for (std::size_t index = 0; index < JobScheduler::kUiActionCapacity; ++index)
    {
        ExpectSource(RunUiActions(scheduler), "source-ordinary-action-drains", failures);
        ExpectSource(trace.size() > index + 1 && trace[index + 1] == static_cast<int>(index + 1),
                     "source-ordinary-fifo-preserved", failures);
    }
    ExpectSource(!RunUiActions(scheduler) && !scheduler.HasUiWork() && scheduler.IsIdle(),
                 "source-capacity-drained-and-idle", failures);
    ExpectSource(scheduler.RequestUiActionSource(source) && !scheduler.IsIdle(), "source-alone-prevents-idle",
                 failures);
    ExpectSource(scheduler.UnregisterUiActionSource(source), "source-unregister-exact-identity", failures);
    ExpectSource(scheduler.IsIdle() && !scheduler.HasUiWork() && !RunUiActions(scheduler) && source.runs == 1,
                 "source-unregister-drops-readiness-without-callback", failures);
    ExpectSource(!scheduler.RequestUiActionSource(source), "source-request-after-unregister-refused", failures);
    ExpectSource(scheduler.RegisterUiActionSource(other) && scheduler.RequestUiActionSource(other),
                 "source-new-registration-can-request", failures);
    ExpectSource(RunUiActions(scheduler) && other.runs == 1 && source.runs == 1,
                 "source-new-registration-dispatches-only-new-owner", failures);
    ExpectSource(scheduler.UnregisterUiActionSource(other), "source-new-registration-unregister", failures);
    scheduler.Close();
    ExpectSource(destroyed == 0, "source-close-does-not-destroy-borrowed-sources", failures);
}

void TestUiActionSourceFairness(int& failures)
{
    JobScheduler scheduler;
    const bool initialized = scheduler.Initialize(1, {}, {SourceTestMemory, nullptr}) >= 0;
    ExpectSource(initialized, "source-fairness-initialize", failures);
    if (!initialized)
    {
        return;
    }
    std::vector<int> trace;
    trace.reserve(8);
    int destroyed = 0;
    SourceProbe source{scheduler, trace, 100, destroyed};
    source.rearmCount = 1;
    ExpectSource(scheduler.RegisterUiActionSource(source), "source-fairness-register", failures);
    for (int marker : {10, 20})
    {
        std::unique_ptr<UiAction> action = std::make_unique<OrdinaryProbe>(trace, marker);
        ExpectSource(scheduler.PostUiAction(action) && !action, "source-fairness-ordinary-queued", failures);
    }
    ExpectSource(scheduler.RequestUiActionSource(source) && scheduler.RequestUiActionSource(source),
                 "source-fairness-repeated-request-coalesces", failures);
    ExpectSource(RunUiActions(scheduler) && source.runs == 1 && trace.back() == 100,
                 "source-fairness-first-ready-source-favored", failures);
    ExpectSource(source.selfRearmAccepted && scheduler.HasUiWork(), "source-self-rearm-preserved-for-next-pass",
                 failures);
    ExpectSource(RunUiActions(scheduler) && trace.back() == 10, "source-fairness-ordinary-follows-source", failures);
    ExpectSource(RunUiActions(scheduler) && trace.back() == 100 && source.runs == 2,
                 "source-fairness-source-follows-ordinary", failures);
    ExpectSource(RunUiActions(scheduler) && trace.back() == 20, "source-fairness-ordinary-follows-source-again",
                 failures);
    ExpectSource(!RunUiActions(scheduler) && trace.size() == 4 && scheduler.IsIdle(),
                 "source-fairness-one-dispatch-per-call-and-drained", failures);
    ExpectSource(scheduler.UnregisterUiActionSource(source), "source-fairness-unregister", failures);
    scheduler.Close();
}

void TestUiActionSourceStopAndClose(int& failures)
{
    {
        JobScheduler scheduler;
        const bool initialized = scheduler.Initialize(1, {}, {SourceTestMemory, nullptr}) >= 0;
        ExpectSource(initialized, "source-stop-initialize", failures);
        if (!initialized)
        {
            return;
        }
        std::vector<int> trace;
        trace.reserve(8);
        int destroyed = 0;
        auto source = std::make_unique<SourceProbe>(scheduler, trace, 100, destroyed);
        SourceProbe replacement{scheduler, trace, 200, destroyed};
        ExpectSource(scheduler.RegisterUiActionSource(*source), "source-stop-register", failures);
        ExpectSource(scheduler.RequestUiActionSource(*source), "source-stop-pending-request", failures);
        scheduler.RequestStop();
        ExpectSource(scheduler.IsIdle() && !scheduler.HasUiWork(), "source-stop-clears-ready-work", failures);
        ExpectSource(!scheduler.RequestUiActionSource(*source), "source-request-refused-after-stop", failures);
        ExpectSource(!scheduler.RegisterUiActionSource(replacement), "source-register-refused-after-stop", failures);
        ExpectSource(!RunUiActions(scheduler) && source->runs == 0, "source-never-runs-after-stop", failures);
        scheduler.Close();
        ExpectSource(destroyed == 0, "source-stop-close-does-not-destroy-borrowed-source", failures);
        source.reset();
        ExpectSource(destroyed == 1, "source-stop-owner-can-destroy-after-close", failures);
    }

    {
        JobScheduler scheduler;
        const bool initialized = scheduler.Initialize(1, {}, {SourceTestMemory, nullptr}) >= 0;
        ExpectSource(initialized, "source-close-initialize", failures);
        if (!initialized)
        {
            return;
        }
        std::vector<int> trace;
        trace.reserve(8);
        int destroyed = 0;
        auto source = std::make_unique<SourceProbe>(scheduler, trace, 100, destroyed);
        SourceProbe replacement{scheduler, trace, 200, destroyed};
        ExpectSource(scheduler.RegisterUiActionSource(*source), "source-close-register", failures);
        ExpectSource(scheduler.RequestUiActionSource(*source), "source-close-ready", failures);
        scheduler.Close();
        ExpectSource(source->runs == 0 && destroyed == 0, "source-close-drops-ready-without-callback-or-destruction",
                     failures);
        ExpectSource(!RunUiActions(scheduler), "source-close-no-action-after-close", failures);
        ExpectSource(!scheduler.RequestUiActionSource(*source) && !scheduler.RegisterUiActionSource(replacement),
                     "source-close-refuses-registration-and-request", failures);
        source.reset();
        ExpectSource(destroyed == 1, "source-close-borrowed-lifetime-ends-with-owner", failures);
    }
}

void TestUiActionSources(int& failures)
{
    TestUiActionSourceCapacity(failures);
    TestUiActionSourceFairness(failures);
    TestUiActionSourceStopAndClose(failures);
}
} // namespace

int RunJobUiPolicyTests()
{
    int failures = 0;
    const auto expect = [&failures](bool condition, const char* name)
    {
        std::printf("%s job-ui-policy.%s\n", condition ? "PASS" : "FAIL", name);
        if (!condition)
        {
            ++failures;
        }
    };

    // Legacy timing reference: archived behavioral reference,
    // busyIndicatorDelay = 250 ms; retained numeric golden cases,
    // quickOperationsNeverDimTheInterface. The threshold is a time rule, not a
    // wall-clock sleep in this test.
    expect(!BusyIndicatorVisible(1000, 1249, 1000), "busy-before-250ms");
    expect(BusyIndicatorVisible(1000, 1250, 1000), "busy-at-250ms");
    expect(BusyIndicatorVisible(1000, 1251, 1000), "busy-after-250ms");

    // For 101 ticks/second, ceil(101 / 4) is 26: 25 ticks are below the
    // boundary and the 26th tick reaches it without floating-point rounding.
    expect(!BusyIndicatorVisible(17, 42, 101), "busy-odd-frequency-before");
    expect(BusyIndicatorVisible(17, 43, 101), "busy-odd-frequency-at");
    expect(BusyIndicatorVisible(17, 44, 101), "busy-odd-frequency-after");
    expect(!BusyIndicatorVisible(0, 0, 0), "busy-zero-frequency");
    expect(!BusyIndicatorVisible(20, 19, 1000), "busy-backward-time");

    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    expect(BusyIndicatorVisible(0, kMax, kMax), "busy-maximum-values");
    expect(BusyIndicatorVisible(kMax - 1000, kMax, 4000), "busy-maximum-now-boundary");
    expect(!BusyIndicatorVisible(kMax - 1000, kMax - 1, 4000), "busy-maximum-now-before");
    expect(!BusyIndicatorVisible(kMax, kMax - 1, kMax), "busy-maximum-backward-time");

    // Independent D8 state-table reference: the five input flags enumerate all
    // 32 states. Expected outputs are literal per-state outcomes, not computed
    // from the policy under test. Query blocking applies only to noncritical
    // worker writes; actual-end waits cover save/export-worker only, while GPU
    // export and clipboard writes are cancellation requests.
    struct EndSessionCase final
    {
        bool saving;
        bool exportWorker;
        bool exportGpu;
        bool clipboardWrite;
        bool critical;
        EndSessionWorkPolicy expected;
    };

    constexpr std::array<EndSessionCase, 32> cases{{
        {false, false, false, false, false, {false, false, false, false}},
        {true, false, false, false, false, {true, true, false, false}},
        {false, true, false, false, false, {true, true, false, false}},
        {true, true, false, false, false, {true, true, false, false}},
        {false, false, true, false, false, {false, false, true, false}},
        {true, false, true, false, false, {true, true, true, false}},
        {false, true, true, false, false, {true, true, true, false}},
        {true, true, true, false, false, {true, true, true, false}},
        {false, false, false, true, false, {false, false, false, true}},
        {true, false, false, true, false, {true, true, false, true}},
        {false, true, false, true, false, {true, true, false, true}},
        {true, true, false, true, false, {true, true, false, true}},
        {false, false, true, true, false, {false, false, true, true}},
        {true, false, true, true, false, {true, true, true, true}},
        {false, true, true, true, false, {true, true, true, true}},
        {true, true, true, true, false, {true, true, true, true}},
        {false, false, false, false, true, {false, false, false, false}},
        {true, false, false, false, true, {false, true, false, false}},
        {false, true, false, false, true, {false, true, false, false}},
        {true, true, false, false, true, {false, true, false, false}},
        {false, false, true, false, true, {false, false, true, false}},
        {true, false, true, false, true, {false, true, true, false}},
        {false, true, true, false, true, {false, true, true, false}},
        {true, true, true, false, true, {false, true, true, false}},
        {false, false, false, true, true, {false, false, false, true}},
        {true, false, false, true, true, {false, true, false, true}},
        {false, true, false, true, true, {false, true, false, true}},
        {true, true, false, true, true, {false, true, false, true}},
        {false, false, true, true, true, {false, false, true, true}},
        {true, false, true, true, true, {false, true, true, true}},
        {false, true, true, true, true, {false, true, true, true}},
        {true, true, true, true, true, {false, true, true, true}},
    }};

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const EndSessionCase& testCase = cases[index];
        const EndSessionWorkPolicy actual = GetEndSessionWorkPolicy(
            {testCase.saving, testCase.exportWorker, testCase.exportGpu, testCase.clipboardWrite, testCase.critical});
        const bool matches = actual.blockQuery == testCase.expected.blockQuery &&
                             actual.waitForWorkerWrites == testCase.expected.waitForWorkerWrites &&
                             actual.cancelGpuExport == testCase.expected.cancelGpuExport &&
                             actual.cancelClipboardWrite == testCase.expected.cancelClipboardWrite;
        char name[48]{};
        std::snprintf(name, sizeof(name), "end-session-state-%02zu", index);
        expect(matches, name);
    }

    TestUiActionSources(failures);

    return failures;
}

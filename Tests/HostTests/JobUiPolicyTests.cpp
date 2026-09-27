#include "JobUiPolicyTests.h"

#include "../../Common/JobUiPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

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

    return failures;
}

#include "Host.Tests.UiTimingAttribution.h"
#include "../../Common/UiTiming.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <source_location>
#include <thread>

namespace
{
int g_failures = 0;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s ui-timing-attribution.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++g_failures;
}
bool Admit(UiTimingStats& stats, std::int64_t begin, std::int64_t end, bool cyclesValid = false,
           std::uint64_t cycleBegin = 0, std::uint64_t cycleEnd = 0,
           std::source_location location = std::source_location::current()) noexcept
{
    return TryRecordUiTimingAttribution(stats, UiTimingKind::ResultApply, 17, begin, end, 1'000'000, cyclesValid,
                                        cycleBegin, cycleEnd, location);
}
} // namespace

int RunUiTimingAttributionTests()
{
    g_failures = 0;
    UiTimingStats disabled;
    Expect(!Admit(disabled, 0, 2000), "disabled-no-record");
    Expect(disabled.attributionCursor == 0 && disabled.attributionOverflowCount == 0, "disabled-counters-unchanged");

    UiTimingStats boundary;
    boundary.attributionEnabled = true;
    Expect(!Admit(boundary, 20, 1019), "below-one-ms-excluded");
    Expect(Admit(boundary, 100, 1100), "exact-one-ms-included");
    Expect(boundary.attributionCursor == 1, "one-record-at-boundary");
    const auto& first = boundary.attributionRecords[0];
    Expect(first.wallMicroseconds == 1000 && first.qpcBegin == 100 && first.qpcEnd == 1100 &&
               first.qpcFrequency == 1'000'000,
           "wall-and-raw-qpc-preserved");
    Expect(first.kind == UiTimingKind::ResultApply && first.detail == 17, "kind-and-detail-preserved");
    Expect(first.file != nullptr && first.function != nullptr && first.line != 0, "lexical-location-captured");

    UiTimingStats order;
    order.attributionEnabled = true;
    Expect(Admit(order, 0, 1500) && Admit(order, 2000, 3500), "two-admissions");
    Expect(order.attributionRecords[0].qpcBegin == 0 && order.attributionRecords[1].qpcBegin == 2000,
           "first-record-order");

    UiTimingStats capacity;
    capacity.attributionEnabled = true;
    for (std::uint32_t index = 0; index < kUiTimingAttributionCapacity + 2; ++index)
        (void)Admit(capacity, static_cast<std::int64_t>(index) * 2000, static_cast<std::int64_t>(index) * 2000 + 1000);
    Expect(capacity.attributionCursor == kUiTimingAttributionCapacity, "fixed-capacity-cursor");
    Expect(capacity.attributionOverflowCount == 2, "overflow-count-after-capacity");
    Expect(capacity.attributionRecords[0].qpcBegin == 0 &&
               capacity.attributionRecords[kUiTimingAttributionCapacity - 1].qpcBegin ==
                   static_cast<std::int64_t>(kUiTimingAttributionCapacity - 1) * 2000,
           "capacity-keeps-first-records");

    UiTimingStats cycles;
    cycles.attributionEnabled = true;
    Expect(Admit(cycles, 0, 1000, true, 500, 900), "valid-cycle-admission");
    Expect(cycles.attributionRecords[0].threadCycleKnown && cycles.attributionRecords[0].threadCycleDelta == 400,
           "valid-cycle-delta");
    Expect(Admit(cycles, 2000, 3000, false, 50, 70), "failed-cycle-sample-admission");
    Expect(!cycles.attributionRecords[1].threadCycleKnown && cycles.attributionRecords[1].threadCycleDelta == 0,
           "failed-cycle-is-unknown");
    Expect(Admit(cycles, 4000, 5000, true, 90, 80), "nonmonotone-cycle-admission");
    Expect(!cycles.attributionRecords[2].threadCycleKnown && cycles.attributionRecords[2].threadCycleDelta == 0,
           "nonmonotone-cycle-is-unknown");
    Expect(cycles.clockFailures == 0, "optional-cycle-errors-do-not-touch-clock-failures");

    UiTimingStats invalidWall;
    invalidWall.attributionEnabled = true;
    Expect(!Admit(invalidWall, 10, 9), "nonmonotone-qpc-refused");
    Expect(!Admit(invalidWall, -1, 1001), "negative-qpc-refused");
    Expect(!TryRecordUiTimingAttribution(invalidWall, UiTimingKind::Frame, 0, 0, 1000, 0, false, 0, 0),
           "invalid-frequency-refused");
    Expect(invalidWall.attributionCursor == 0 && invalidWall.attributionOverflowCount == 0,
           "invalid-wall-does-not-consume-capacity");
    Expect(!TryRecordUiTimingAttribution(invalidWall, UiTimingKind::Count, 0, 0, 1000, 1'000'000, false, 0, 0),
           "invalid-kind-refused");
    Expect(!TryRecordUiTimingAttribution(invalidWall, UiTimingKind::ResultApply, 0, 0, 1000, 1'000'000, false, 0, 0,
                                         std::source_location::current(), {},
                                         static_cast<UiTimingAttributionPhase>(12)),
           "invalid-phase-refused");

    UiTimingStats identified;
    identified.attributionEnabled = true;
    const auto origin = std::source_location::current();
    Expect(TryRecordUiTimingAttribution(identified, UiTimingKind::ResultApply, 2, 100, 11100, 10'000'000, true, 500,
                                        1500, origin, {7, 19}, UiTimingAttributionPhase::PayloadDestruction),
           "identified-phase-admission");
    const auto& identifiedRecord = identified.attributionRecords[0];
    Expect(identifiedRecord.identity.slot == 7 && identifiedRecord.identity.generation == 19 &&
               identifiedRecord.phase == UiTimingAttributionPhase::PayloadDestruction,
           "original-ticket-and-phase-preserved");
    Expect(identifiedRecord.wallMicroseconds == 1100 && identifiedRecord.threadCycleDelta == 1000 &&
               identifiedRecord.line == origin.line() && std::strcmp(identifiedRecord.file, origin.file_name()) == 0 &&
               std::strcmp(identifiedRecord.function, origin.function_name()) == 0,
           "explicit-lexical-origin-and-independent-clock-units");
    {
        UiTimingCapture capture{disabled};
        const ScopedUiTimingAttribution phase{UiTimingKind::ResultApply, UiTimingAttributionPhase::CompletedCallback};
    }
    Expect(disabled.attributionCursor == 0 && disabled.clockFailures == 0 && disabled.attributionClockFailures == 0,
           "disabled-phase-scope-has-no-observation");
    {
        UiTimingCapture capture{identified};
        const ScopedUiTimingAttribution phase{UiTimingKind::ResultApply, UiTimingAttributionPhase::CompletedCallback};
    }
    Expect(identified.durations[static_cast<std::size_t>(UiTimingKind::ResultApply)].SampleCount() == 0 &&
               identified.clockFailures == 0 && identified.attributionClockFailures == 0,
           "optional-phase-does-not-write-legacy-histogram");
    Expect(TryRecordUiTimingAttribution(identified, UiTimingKind::ResultApply, 1, 100, 11100, 10'000'000, false, 0, 0,
                                        origin, {7, 19}, UiTimingAttributionPhase::WorkerWake) &&
               identified.attributionRecords[identified.attributionCursor - 1].phase ==
                   UiTimingAttributionPhase::WorkerWake,
           "worker-wake-phase-admitted");
    Expect(TryRecordUiTimingAttribution(identified, UiTimingKind::ResultApply, 4, 100, 11100, 10'000'000, false, 0, 0,
                                        origin, {7, 19}, UiTimingAttributionPhase::CancellationSourceDestruction) &&
               identified.attributionRecords[identified.attributionCursor - 1].phase ==
                   UiTimingAttributionPhase::CancellationSourceDestruction,
           "cancellation-source-destruction-phase-admitted");
    {
        UiTimingCapture capture{disabled};
        const ScopedUiTiming scope{UiTimingKind::ResultApply, 4, {7, 19}};
        Expect(CurrentUiTimingAttributionIdentity().generation == 0, "disabled-capture-does-not-install-job-ancestry");
    }
    {
        UiTimingCapture capture{identified};
        const ScopedUiTiming scope{UiTimingKind::ResultApply, 4, {7, 19}};
        Expect(CurrentUiTimingAttributionIdentity().slot == 7 && CurrentUiTimingAttributionIdentity().generation == 19,
               "enabled-scope-installs-original-ticket-ancestry");
        {
            const ScopedUiTiming nested{UiTimingKind::ResultApply, 4, {3, 44}};
            Expect(CurrentUiTimingAttributionIdentity().slot == 3 &&
                       CurrentUiTimingAttributionIdentity().generation == 44,
                   "nested-original-ticket-replaces-ancestry");
        }
        Expect(CurrentUiTimingAttributionIdentity().slot == 7 && CurrentUiTimingAttributionIdentity().generation == 19,
               "nested-scope-restores-parent-ticket");
        bool workerEmpty = false;
        std::thread worker([&]() { workerEmpty = CurrentUiTimingAttributionIdentity().generation == 0; });
        worker.join();
        Expect(workerEmpty && CurrentUiTimingAttributionIdentity().generation == 19,
               "job-ancestry-never-crosses-threads");
    }
    Expect(CurrentUiTimingAttributionIdentity().generation == 0, "completed-scope-restores-empty-ancestry");

    UiTimingStats saturation;
    saturation.attributionEnabled = true;
    saturation.attributionCursor = static_cast<std::uint32_t>(kUiTimingAttributionCapacity);
    saturation.attributionOverflowCount = (std::numeric_limits<std::uint64_t>::max)() - 1;
    Expect(Admit(saturation, 0, 1000) && Admit(saturation, 2000, 3000), "saturated-overflow-admission");
    Expect(saturation.attributionOverflowCount == (std::numeric_limits<std::uint64_t>::max)(),
           "overflow-counter-saturates");
    Expect(Admit(saturation, 4000, 5000), "already-saturated-admission");
    Expect(saturation.attributionOverflowCount == (std::numeric_limits<std::uint64_t>::max)(),
           "overflow-counter-remains-saturated");
    return g_failures;
}

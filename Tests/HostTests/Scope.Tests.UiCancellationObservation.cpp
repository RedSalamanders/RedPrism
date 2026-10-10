#include "Scope.Tests.UiCancellationObservation.h"

#include "../../RedPrism/UiCancellationObservation.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <type_traits>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kWaitMs = 10000;
constexpr std::size_t kBatchPayloads = UiCancellationObservation::kOwnerBatch + 8;
constexpr std::size_t kConcurrentCallbacks = 8;
unsigned int gFailures = 0;
unsigned int gChecks = 0;

void Expect(bool passed, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s ui-cancellation-observation.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
        ++gFailures;
    std::fflush(stdout);
}

static_assert(std::is_same_v<decltype(&Expect), void (*)(bool, const char*) noexcept>);

wil::unique_handle Event(bool signaled = false)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, signaled ? TRUE : FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL ui-cancellation-observation.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

wil::unique_handle Semaphore(LONG maximum)
{
    wil::unique_handle semaphore{CreateSemaphoreW(nullptr, 0, maximum, nullptr)};
    if (!semaphore)
    {
        std::printf("FAIL ui-cancellation-observation.create-semaphore\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return semaphore;
}

bool IsSignaled(HANDLE handle) noexcept
{
    return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
}

bool WaitOne(HANDLE handle) noexcept
{
    return WaitForSingleObject(handle, kWaitMs) == WAIT_OBJECT_0;
}

bool ReadIs(const UiCancellationObservation& observation, std::size_t cursor, const JobExecutionNotice& expected,
            std::int64_t* ticks = nullptr) noexcept
{
    UiCancellationObservation::Sample sample;
    if (observation.Read(cursor, sample) != UiCancellationObservation::ReadResult::Published)
        return false;
    const auto& actual = sample.notice;
    const bool sameResult = actual.result.has_value() == expected.result.has_value() &&
                            (!expected.result || (actual.result.value().status == expected.result.value().status &&
                                                  actual.result.value().error == expected.result.value().error));
    if (ticks)
        *ticks = sample.ticks;
    return actual.boundary == expected.boundary && actual.ticket == expected.ticket && actual.kind == expected.kind &&
           actual.lane == expected.lane && sameResult;
}

JobExecutionNotice MakeNotice(std::size_t index) noexcept
{
    const auto kind = static_cast<JobClass>(index % static_cast<std::size_t>(JobClass::Count));
    const auto lane = static_cast<JobExecutionLane>(index % static_cast<std::size_t>(JobExecutionLane::Count));
    return {JobExecutionBoundary::BodyExited, JobTicket{index + 3, 9000 + static_cast<std::uint64_t>(index)}, kind,
            lane, JobResult{JobStatus::Failed, E_ACCESSDENIED}};
}

void TestCreateManualResetAndExactPayload()
{
    std::unique_ptr<UiCancellationObservation> observation;
    const HRESULT created = UiCancellationObservation::TryCreate(observation);
    Expect(created == S_OK && observation, "payload.factory-creates-live-owner");
    if (!observation)
        return;

    const HANDLE wake = observation->WakeHandle();
    Expect(wake != nullptr && !IsSignaled(wake), "payload.real-manual-reset-event-starts-clear");
    const auto observer = observation->Observer();
    const auto expected = MakeNotice(4);
    Expect(observer.notify != nullptr && observer.context == observation.get(),
           "payload.observer-borrows-stable-owner");
    if (observer.notify)
        observer.notify(observer.context, expected);
    const bool woke = WaitOne(wake);
    UiCancellationObservation::Sample sample;
    const auto read = observation->Read(0, sample);
    Expect(woke && read == UiCancellationObservation::ReadResult::Published,
           "payload.event-wake-observes-published-record");
    Expect(read == UiCancellationObservation::ReadResult::Published && ReadIs(*observation, 0, expected),
           "payload-preserves-boundary-ticket-kind-lane-and-raw-result");
    Expect(read == UiCancellationObservation::ReadResult::Published && sample.ticks >= 0,
           "payload-observer-supplies-valid-qpc-sample");
    Expect(IsSignaled(wake), "payload-manual-reset-remains-signaled-after-wait");
    Expect(observation->BeginOwnerDrain() == S_OK && !IsSignaled(wake), "payload-owner-reset-clears-wake-before-drain");
    Expect(observation->Read(1, sample) == UiCancellationObservation::ReadResult::Exhausted,
           "payload-next-cursor-reports-exhausted");
    Expect(observation->EndOwnerBatch(1) == S_OK && !IsSignaled(wake), "payload-exhausted-batch-does-not-rearm-event");

    auto* const original = observation.get();
    const HRESULT rejected = UiCancellationObservation::TryCreate(observation);
    Expect(rejected == E_UNEXPECTED && observation.get() == original,
           "payload-nonempty-output-refusal-preserves-existing-owner");
}

void TestBoundedBatchRearmAndFinalClear()
{
    std::unique_ptr<UiCancellationObservation> observation;
    const HRESULT created = UiCancellationObservation::TryCreate(observation);
    Expect(created == S_OK && observation, "batch.factory-creates-live-owner");
    if (!observation)
        return;

    const auto observer = observation->Observer();
    bool callbackValid = observer.notify != nullptr;
    for (std::size_t index = 0; index < kBatchPayloads; ++index)
    {
        if (!observer.notify)
            break;
        observer.notify(observer.context, MakeNotice(index));
    }
    Expect(callbackValid && WaitOne(observation->WakeHandle()), "batch.actual-observer-signals-after-publication");
    Expect(observation->BeginOwnerDrain() == S_OK && !IsSignaled(observation->WakeHandle()),
           "batch-reset-precedes-first-owner-drain");

    bool firstBatchMatches = true;
    for (std::size_t index = 0; index < UiCancellationObservation::kOwnerBatch; ++index)
        firstBatchMatches = ReadIs(*observation, index, MakeNotice(index)) && firstBatchMatches;
    Expect(firstBatchMatches, "batch-first-32-records-match-in-publication-order");
    Expect(observation->EndOwnerBatch(UiCancellationObservation::kOwnerBatch) == S_OK &&
               IsSignaled(observation->WakeHandle()),
           "batch-known-published-record-rearms-owner-event");

    const bool secondWake = WaitOne(observation->WakeHandle());
    const HRESULT secondReset = observation->BeginOwnerDrain();
    bool secondBatchMatches = true;
    for (std::size_t index = UiCancellationObservation::kOwnerBatch; index < kBatchPayloads; ++index)
        secondBatchMatches = ReadIs(*observation, index, MakeNotice(index)) && secondBatchMatches;
    Expect(secondWake && secondReset == S_OK && secondBatchMatches, "batch-rearmed-drain-consumes-exact-remainder");
    Expect(observation->EndOwnerBatch(kBatchPayloads) == S_OK && !IsSignaled(observation->WakeHandle()),
           "batch-exhausting-final-batch-leaves-event-nonsignaled");
    UiCancellationObservation::Sample sample;
    Expect(observation->Read(kBatchPayloads, sample) == UiCancellationObservation::ReadResult::Exhausted,
           "batch-final-cursor-is-exhausted");
}

void TestConcurrentEventGatedObserverCallbacks()
{
    std::unique_ptr<UiCancellationObservation> observation;
    const HRESULT created = UiCancellationObservation::TryCreate(observation);
    Expect(created == S_OK && observation, "concurrent.factory-creates-live-owner");
    if (!observation)
        return;

    auto ready = Semaphore(static_cast<LONG>(kConcurrentCallbacks));
    auto completed = Semaphore(static_cast<LONG>(kConcurrentCallbacks));
    auto start = Event();
    auto finish = Event();
    const auto observer = observation->Observer();
    std::array<std::thread, kConcurrentCallbacks> workers;
    std::atomic<unsigned int> callbackCount{0};

    const auto joinWorkers = [&]() noexcept
    {
        for (auto& worker : workers)
            if (worker.joinable())
                worker.join();
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(start.get());
            (void)SetEvent(finish.get());
            joinWorkers();
        });

    for (std::size_t index = 0; index < kConcurrentCallbacks; ++index)
    {
        workers[index] = std::thread(
            [&, index]() noexcept
            {
                (void)ReleaseSemaphore(ready.get(), 1, nullptr);
                if (WaitForSingleObject(start.get(), kWaitMs) == WAIT_OBJECT_0 && observer.notify)
                {
                    observer.notify(observer.context, MakeNotice(index + 100));
                    callbackCount.fetch_add(1, std::memory_order_release);
                }
                (void)ReleaseSemaphore(completed.get(), 1, nullptr);
                (void)WaitForSingleObject(finish.get(), INFINITE);
            });
    }

    bool allReady = true;
    for (std::size_t index = 0; index < kConcurrentCallbacks && allReady; ++index)
        allReady = WaitOne(ready.get());
    Expect(allReady, "concurrent-workers-are-event-gated-before-release");

    (void)SetEvent(start.get());
    bool allCompleted = true;
    for (std::size_t index = 0; index < kConcurrentCallbacks && allCompleted; ++index)
        allCompleted = WaitOne(completed.get());

    const bool callbacksHeld = allReady && allCompleted && !IsSignaled(finish.get());
    Expect(callbacksHeld && IsSignaled(observation->WakeHandle()), "concurrent-real-callbacks-publish-wake-event");
    bool allSamplesMatch = callbacksHeld && callbackCount.load(std::memory_order_acquire) == kConcurrentCallbacks;
    std::array<bool, kConcurrentCallbacks> seen{};
    for (std::size_t cursor = 0; cursor < kConcurrentCallbacks; ++cursor)
    {
        UiCancellationObservation::Sample sample;
        if (observation->Read(cursor, sample) != UiCancellationObservation::ReadResult::Published || sample.ticks < 0)
        {
            allSamplesMatch = false;
            continue;
        }
        const std::size_t slot = sample.notice.ticket.slot;
        if (slot < 103 || slot >= 103 + kConcurrentCallbacks)
        {
            allSamplesMatch = false;
            continue;
        }
        const auto sourceIndex = slot - 103;
        if (seen[sourceIndex] || !ReadIs(*observation, cursor, MakeNotice(sourceIndex + 100)))
            allSamplesMatch = false;
        seen[sourceIndex] = true;
    }
    for (const bool didSee : seen)
        allSamplesMatch = didSee && allSamplesMatch;
    Expect(allSamplesMatch, "concurrent-callbacks-retain-each-exact-payload-once");
    Expect(callbacksHeld && observation->Failure() == S_OK, "concurrent-callbacks-report-no-transport-failure");
    const HRESULT drainResult = observation->BeginOwnerDrain();
    const HRESULT batchResult = observation->EndOwnerBatch(kConcurrentCallbacks);
    Expect(callbacksHeld && drainResult == S_OK && batchResult == S_OK && !IsSignaled(observation->WakeHandle()),
           "concurrent-final-empty-drain-clears-event");

    // All payload and event assertions above run before callbacks are allowed to return from their hold.
    (void)SetEvent(finish.get());
    joinWorkers();
}
} // namespace

int RunUiCancellationObservationTransportTests()
{
    gFailures = 0;
    gChecks = 0;
    TestCreateManualResetAndExactPayload();
    TestBoundedBatchRearmAndFinalClear();
    TestConcurrentEventGatedObserverCallbacks();
    constexpr unsigned int kExpectedChecks = 25;
    std::printf("%s ui-cancellation-observation.summary controls=%u failed=%u\n",
                gFailures == 0 && gChecks == kExpectedChecks ? "PASS" : "FAIL", gChecks, gFailures);
    std::fflush(stdout);
    return static_cast<int>(gFailures + (gChecks != kExpectedChecks ? 1 : 0));
}

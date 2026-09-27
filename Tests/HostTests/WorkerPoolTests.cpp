#include "WorkerPoolTests.h"

#include "WorkerPool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <thread>
#include <vector>
#include <wincodec.h>

#include <wil/com.h>
#pragma comment(lib, "windowscodecs.lib")

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s worker.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

wil::unique_handle Event(bool manual = true)
{
    wil::unique_handle event{CreateEventW(nullptr, manual, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL worker.create-event\n");
        std::_Exit(1);
    }
    return event;
}

// Failure bounds only. No expected duration, sleep, or repeated polling. Continuing after a failed gate could
// leave callbacks referring to a destroyed test frame, so a timeout fails this isolated test process immediately.
void Wait(HANDLE event) noexcept
{
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL worker.gate-timeout\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}

constexpr std::uint32_t Pixel(std::uint32_t x, std::uint32_t y, unsigned int job) noexcept
{
    return (x * 17u + y * 31u + job * 13u) ^ 0x5a731fu;
}

struct Batch final
{
    WorkerPool* pool = nullptr;
    unsigned int jobs = 1;
    bool nested = false;
    std::array<std::vector<std::uint32_t>, 4> output;
    std::array<std::vector<std::atomic<unsigned int>>, 4> visits;
    std::atomic<unsigned int> next{0};
    std::atomic<unsigned int> started{0};
    std::atomic<unsigned int> finished{0};
    std::atomic<bool> valid{true};
    wil::unique_handle ready = Event();
    wil::unique_handle go = Event();
    wil::unique_handle done = Event();
};

void RunBatch(void* context, WorkPriority, CancelToken token) noexcept
{
    auto& batch = *static_cast<Batch*>(context);
    const unsigned int job = batch.next.fetch_add(1);
    if (job >= batch.jobs)
    {
        return;
    }
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    if (FAILED(CoGetApartmentType(&apartment, &qualifier)) || apartment != APTTYPE_MTA)
    {
        batch.valid.store(false);
    }
    wil::com_ptr_nothrow<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()))))
    {
        batch.valid.store(false);
    }
    if (batch.started.fetch_add(1) + 1 == batch.jobs)
    {
        (void)SetEvent(batch.ready.get());
    }
    Wait(batch.go.get()); // test driver occupies all N callbacks before any N-way parallel call begins
    bool complete = false;
    if (batch.nested)
    {
        complete = ParallelFor(*batch.pool, TileGrid{7, 3, 1, 1}, token,
                               [&](Tile outer, CancelToken cancellation) noexcept
                               {
                                   const std::uint32_t outerIndex = outer.y * 7 + outer.x;
                                   return ParallelFor(*batch.pool, TileGrid{17, 3, 1, 1}, cancellation,
                                                      [&](Tile inner, CancelToken stop) noexcept
                                                      {
                                                          if (stop.IsCancelled())
                                                          {
                                                              return false;
                                                          }
                                                          const std::uint32_t index =
                                                              outerIndex * 51 + inner.y * 17 + inner.x;
                                                          batch.visits[job][index].fetch_add(1);
                                                          batch.output[job][index] = Pixel(index, outerIndex, job);
                                                          return true;
                                                      });
                               });
    }
    else
    {
        complete = ParallelFor(*batch.pool, TileGrid{277, 259, 64, 32}, token,
                               [&](Tile tile, CancelToken cancellation) noexcept
                               {
                                   batch.visits[job][(tile.y / 32) * 5 + tile.x / 64].fetch_add(1);
                                   for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y)
                                   {
                                       if (cancellation.IsCancelled())
                                       {
                                           return false;
                                       }
                                       for (std::uint32_t x = tile.x; x < tile.x + tile.width; ++x)
                                       {
                                           batch.output[job][y * 277 + x] = Pixel(x, y, job);
                                       }
                                   }
                                   return true;
                               });
    }
    if (!complete)
    {
        batch.valid.store(false);
    }
    if (batch.finished.fetch_add(1) + 1 == batch.jobs)
    {
        (void)SetEvent(batch.done.get());
    }
}

bool SaturatedBatch(unsigned int threads, bool nested)
{
    WorkerPool pool;
    Batch batch;
    batch.pool = &pool;
    batch.jobs = threads;
    batch.nested = nested;
    for (unsigned int job = 0; job < threads; ++job)
    {
        batch.output[job].resize(nested ? 7u * 3u * 17u * 3u : 277u * 259u);
        batch.visits[job] = std::vector<std::atomic<unsigned int>>(nested ? 1071u : 45u);
    }
    if (FAILED(pool.Initialize(threads, RunBatch, &batch)))
    {
        return false;
    }
    for (unsigned int index = 0; index < threads; ++index)
    {
        if (!pool.Wake(WorkPriority::High))
        {
            (void)SetEvent(batch.go.get());
            pool.Close();
            return false;
        }
    }
    Wait(batch.ready.get());
    (void)SetEvent(batch.go.get());
    Wait(batch.done.get());
    pool.Close();
    bool valid = batch.valid.load() && batch.finished.load() == threads;
    for (unsigned int job = 0; job < threads; ++job)
    {
        for (const auto& visits : batch.visits[job])
        {
            valid = valid && visits.load() == 1;
        }
        if (nested)
        {
            for (std::uint32_t index = 0; index < 1071; ++index)
            {
                valid = valid && batch.output[job][index] == Pixel(index, index / 51, job);
            }
        }
        else
        {
            for (std::uint32_t y = 0; y < 259; ++y)
            {
                for (std::uint32_t x = 0; x < 277; ++x)
                {
                    valid = valid && batch.output[job][y * 277 + x] == Pixel(x, y, job);
                }
            }
        }
    }
    return valid;
}

void TestParallelHelpers()
{
    WorkerPool pool;
    Expect(SUCCEEDED(pool.Initialize(2)), "helpers-initialize");
    auto ready = Event();
    auto release = Event();
    auto done = Event();
    std::atomic<unsigned int> entrants{0};
    std::array<std::atomic<unsigned int>, 64> visits{};
    std::array<DWORD, 2> threads{};
    bool result = false;
    std::thread caller(
        [&]
        {
            result = ParallelFor(pool, TileGrid{64, 1, 1, 1}, {},
                                 [&](Tile tile, CancelToken) noexcept
                                 {
                                     const unsigned int entrant = entrants.fetch_add(1);
                                     if (entrant < 2)
                                     {
                                         threads[entrant] = GetCurrentThreadId();
                                         if (entrant == 1)
                                         {
                                             (void)SetEvent(ready.get());
                                         }
                                         // Test-only gate forces one real helper and the participating caller to
                                         // overlap.
                                         Wait(release.get());
                                     }
                                     visits[tile.x].fetch_add(1);
                                     return true;
                                 });
            (void)SetEvent(done.get());
        });
    Wait(ready.get());
    (void)SetEvent(release.get());
    Wait(done.get());
    caller.join();
    pool.Close();
    Expect(result && threads[0] != threads[1], "helper-and-caller-overlap");
    Expect(std::all_of(visits.begin(), visits.end(), [](const auto& count) { return count.load() == 1; }),
           "parallel-visits-exactly-once");
}

void TestCloseDuringParallel()
{
    WorkerPool pool;
    Expect(SUCCEEDED(pool.Initialize(2)), "close-initialize");
    auto entered = Event();
    auto release = Event();
    auto stopped = Event();
    auto closed = Event();
    auto completed = Event();
    std::atomic<bool> result{true};
    std::atomic<unsigned int> rows{0};
    std::stop_callback stopObserved{pool.ShutdownToken(), [&] { (void)SetEvent(stopped.get()); }};
    std::thread caller(
        [&]
        {
            result.store(ParallelFor(pool, TileGrid{1, 128, 1, 1}, {},
                                     [&](Tile, CancelToken token) noexcept
                                     {
                                         (void)SetEvent(entered.get());
                                         // Deliberate test-only gate holds a claimed tile so Close is proven to retain
                                         // its borrowed state.
                                         Wait(release.get());
                                         if (token.IsCancelled())
                                         {
                                             return false;
                                         }
                                         rows.fetch_add(1);
                                         return true;
                                     }));
            (void)SetEvent(completed.get());
        });
    Wait(entered.get());
    std::thread closer(
        [&]
        {
            pool.Close();
            (void)SetEvent(closed.get());
        });
    Wait(stopped.get());
    Expect(WaitForSingleObject(closed.get(), 0) == WAIT_TIMEOUT, "close-retains-running-frame");
    (void)SetEvent(release.get());
    Wait(completed.get());
    Wait(closed.get());
    caller.join();
    closer.join();
    Expect(!result.load() && rows.load() == 0, "close-cancels-before-next-row");
    Expect(!pool.Wake(WorkPriority::High), "closed-refuses-wake");
    Expect(FAILED(pool.Initialize(1)), "closed-refuses-reinitialize");
    pool.Close();
}

void TestWaitPool()
{
    WaitPool waits;
    Expect(SUCCEEDED(waits.Initialize()), "wait-initialize");
    auto signal = Event(false);
    auto done = Event(false);
    struct Counter final
    {
        std::atomic<unsigned int> calls{0};
        HANDLE done;
    } counter{{0}, done.get()};
    const auto count = [](void* context) noexcept
    {
        auto& value = *static_cast<Counter*>(context);
        value.calls.fetch_add(1);
        (void)SetEvent(value.done);
    };
    WaitPool::Ticket first;
    Expect(waits.Add(nullptr, count, &counter, first) == E_INVALIDARG, "wait-invalid-handle");
    Expect(!waits.Rearm({}), "wait-invalid-ticket");
    Expect(SUCCEEDED(waits.Add(signal.get(), count, &counter, first)), "wait-add");
    (void)SetEvent(signal.get());
    Wait(done.get());
    Expect(counter.calls.load() == 1, "wait-first-edge");
    Expect(waits.Rearm(first), "wait-rearm");
    (void)SetEvent(signal.get());
    Wait(done.get());
    waits.Remove(first);
    Expect(counter.calls.load() == 2 && !waits.Rearm(first), "wait-remove-disarms");
    WaitPool::Ticket replacement;
    Expect(SUCCEEDED(waits.Add(signal.get(), count, &counter, replacement)) && replacement.slot == first.slot &&
               replacement.generation != first.generation && !waits.Rearm(first),
           "wait-stale-ticket-refused");
    waits.Remove(first); // an old ticket cannot remove the replacement
    (void)SetEvent(signal.get());
    Wait(done.get());
    Expect(counter.calls.load() == 3, "wait-replacement-survives-old-remove");
    waits.Remove(replacement);
    std::array<WaitPool::Ticket, WaitPool::kCapacity> tickets;
    bool allAdded = true;
    for (auto& ticket : tickets)
    {
        allAdded = allAdded && SUCCEEDED(waits.Add(signal.get(), count, &counter, ticket));
    }
    WaitPool::Ticket extra;
    Expect(allAdded && waits.Add(signal.get(), count, &counter, extra) == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
           "wait-capacity-bounded");
    waits.Close();
    (void)SetEvent(signal.get());
    Expect(counter.calls.load() == 3 && !waits.Rearm(replacement), "wait-close-cancels-pending");
    Expect(waits.Add(signal.get(), count, &counter, extra) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE),
           "wait-closed-refuses-add");
    waits.Close();
}

void TestWaitsIndependentOfWorkers()
{
    struct Blocked final
    {
        wil::unique_handle entered = Event();
        wil::unique_handle release = Event();
        wil::unique_handle done = Event();
    } blocked;
    WorkerPool workers;
    WaitPool waits;
    Expect(SUCCEEDED(workers.Initialize(
               1,
               [](void* context, WorkPriority, CancelToken) noexcept
               {
                   auto& state = *static_cast<Blocked*>(context);
                   (void)SetEvent(state.entered.get());
                   Wait(state.release.get());
                   (void)SetEvent(state.done.get());
               },
               &blocked)),
           "independent-worker-initialize");
    Expect(SUCCEEDED(waits.Initialize()), "independent-wait-initialize");
    auto signal = Event(false);
    WaitPool::Ticket ticket;
    Expect(SUCCEEDED(waits.Add(
               signal.get(),
               [](void* context) noexcept
               {
                   auto& state = *static_cast<Blocked*>(context);
                   (void)SetEvent(state.release.get());
               },
               &blocked, ticket)),
           "independent-wait-add");
    Expect(workers.Wake(WorkPriority::Normal), "independent-worker-wake");
    Wait(blocked.entered.get());
    (void)SetEvent(signal.get());
    Wait(blocked.done.get()); // only the separate wait pool can release the fully occupied worker pool
    waits.Close();
    workers.Close();
    Expect(true, "wait-completes-with-all-workers-occupied");
}

void TestQueuedCancellation()
{
    struct State final
    {
        std::atomic<unsigned int> calls{0};
        wil::unique_handle started = Event();
        wil::unique_handle release = Event();
    } state;
    WorkerPool pool;
    Expect(SUCCEEDED(pool.Initialize(
               1,
               [](void* context, WorkPriority, CancelToken) noexcept
               {
                   auto& work = *static_cast<State*>(context);
                   work.calls.fetch_add(1);
                   (void)SetEvent(work.started.get());
                   Wait(work.release.get());
               },
               &state)),
           "queued-cancel-initialize");
    Expect(pool.Wake(WorkPriority::Background), "background-dispatch");
    Wait(state.started.get());
    // A queued wake is payload-free; stopping can discard it without running an operation or leaking inputs.
    Expect(pool.Wake(WorkPriority::High) && pool.Wake(WorkPriority::Normal), "queue-other-priorities");
    pool.RequestStop();
    Expect(!pool.Wake(WorkPriority::Background), "stop-refuses-new-wake");
    (void)SetEvent(state.release.get());
    pool.Close();
    Expect(state.calls.load() == 1, "queued-callbacks-never-execute-after-stop");
}
} // namespace

int RunWorkerPoolTests(bool stress)
{
    g_failures = 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
    {
        Expect(false, "sta-initialize");
        return g_failures;
    }
    wil::unique_couninitialize_call apartmentLifetime{true};
    APTTYPE originalApartment{};
    APTTYPEQUALIFIER originalQualifier{};
    Expect(SUCCEEDED(CoGetApartmentType(&originalApartment, &originalQualifier)) &&
               (originalApartment == APTTYPE_STA || originalApartment == APTTYPE_MAINSTA),
           "owner-starts-in-sta");
    WorkerPool invalid;
    Expect(invalid.Initialize(1025) == E_INVALIDARG, "thread-limit");
    Expect(!invalid.Wake(WorkPriority::Normal), "uninitialized-refuses-wake");
    for (const unsigned int count : {1u, 2u, 4u})
    {
        Expect(SaturatedBatch(count, false), "saturated-output-and-mta");
        Expect(SaturatedBatch(count, true), "nested-output-and-mta");
    }
    TestParallelHelpers();
    TestCloseDuringParallel();
    TestWaitPool();
    TestWaitsIndependentOfWorkers();
    TestQueuedCancellation();
    if (stress)
    {
        bool valid = true;
        for (unsigned int round = 0; round < 64; ++round)
        {
            valid = SaturatedBatch(4, (round & 1) != 0) && valid;
            TestCloseDuringParallel();
        }
        Expect(valid, "stress-64-saturated-nested-close-rounds");
    }
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    Expect(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == originalApartment &&
               qualifier == originalQualifier,
           "pool-cookies-preserve-owner-sta");
    std::fflush(stdout);
    return g_failures;
}

int RunWorkerPoolBenchmark()
{
    using Clock = std::chrono::steady_clock;
    THREAD_POWER_THROTTLING_STATE qos{THREAD_POWER_THROTTLING_CURRENT_VERSION, THREAD_POWER_THROTTLING_EXECUTION_SPEED,
                                      0};
    (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &qos, sizeof(qos));
    const auto restoreQos = wil::scope_exit(
        []
        {
            THREAD_POWER_THROTTLING_STATE state{THREAD_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
            (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
        });
    constexpr std::uint32_t width = 4096;
    constexpr std::uint32_t height = 4096;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    std::uint64_t expected = 0;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            expected += Pixel(x, y, 0);
        }
    }
    auto measure = [&](Executor& executor, const char* name)
    {
        std::array<double, 11> times{};
        for (int sample = -1; sample < 11; ++sample)
        {
            const auto start = Clock::now();
            const bool complete = ParallelFor(executor, TileGrid{width, height}, {},
                                              [&](Tile tile, CancelToken stop) noexcept
                                              {
                                                  for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y)
                                                  {
                                                      if (stop.IsCancelled())
                                                      {
                                                          return false;
                                                      }
                                                      for (std::uint32_t x = tile.x; x < tile.x + tile.width; ++x)
                                                      {
                                                          pixels[static_cast<std::size_t>(y) * width + x] =
                                                              Pixel(x, y, 0);
                                                      }
                                                  }
                                                  return true;
                                              });
            const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (!complete || std::accumulate(pixels.begin(), pixels.end(), std::uint64_t{0}) != expected)
            {
                return false;
            }
            if (sample >= 0)
            {
                times[static_cast<std::size_t>(sample)] = milliseconds;
            }
        }
        std::sort(times.begin(), times.end());
        std::printf("EXECUTOR BENCH engine=%s threads=%u pixels=%u median_ms=%.6f p95_ms=%.6f checksum=%llu\n", name,
                    executor.Concurrency(), width * height, times[5], times[10],
                    static_cast<unsigned long long>(expected));
        std::array<double, 101> overhead{};
        for (int sample = -1; sample < 101; ++sample)
        {
            const auto start = Clock::now();
            if (!ParallelFor(executor, TileGrid{executor.Concurrency(), 1, 1, 1}, {},
                             [](Tile, CancelToken) noexcept { return true; }))
            {
                return false;
            }
            if (sample >= 0)
            {
                overhead[static_cast<std::size_t>(sample)] =
                    std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            }
        }
        std::sort(overhead.begin(), overhead.end());
        std::printf("EXECUTOR BENCH empty_tiles engine=%s threads=%u median_us=%.3f p95_us=%.3f\n", name,
                    executor.Concurrency(), overhead[50], overhead[95]);
        return true;
    };
    InlineExecutor inlineExecutor;
    if (!measure(inlineExecutor, "inline"))
    {
        return 1;
    }
    const auto logical = static_cast<unsigned int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    std::vector<unsigned int> counts{1, 2, 4, 8, 16, logical};
    std::sort(counts.begin(), counts.end());
    counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
    for (const unsigned int count : counts)
    {
        if (count > logical)
        {
            continue;
        }
        WorkerPool pool;
        if (FAILED(pool.Initialize(count)) || !measure(pool, "pool"))
        {
            return 1;
        }
    }
    CancelSource cancel;
    const auto token = cancel.Token();
    constexpr unsigned int checks = 1000000;
    unsigned int cancelled = 0;
    const auto start = Clock::now();
    for (unsigned int index = 0; index < checks; ++index)
    {
        cancelled += token.IsCancelled() ? 1u : 0u;
    }
    const double nanoseconds = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / checks;
    std::printf("EXECUTOR BENCH cancel_checks=%u ns_per_check=%.3f cancelled=%u\n", checks, nanoseconds, cancelled);
    // Instrument a separate run so timestamp calls cannot distort the throughput numbers. This qualifies only
    // this synthetic 256-pixel row body; each migrated image kernel must measure its own longest polling gap.
    double longestGap = 0.0;
    auto previousCheck = Clock::now();
    const bool instrumented = ParallelFor(
        inlineExecutor, TileGrid{width, height}, token,
        [&](Tile tile, CancelToken stop) noexcept
        {
            for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y)
            {
                const auto now = Clock::now();
                longestGap =
                    (std::max)(longestGap, std::chrono::duration<double, std::micro>(now - previousCheck).count());
                previousCheck = now;
                if (stop.IsCancelled())
                {
                    return false;
                }
                for (std::uint32_t x = tile.x; x < tile.x + tile.width; ++x)
                {
                    pixels[static_cast<std::size_t>(y) * width + x] = Pixel(x, y, 0);
                }
            }
            return true;
        });
    std::printf("EXECUTOR BENCH synthetic_row_poll_gap_max_us=%.3f\n", longestGap);
    if (!instrumented || std::accumulate(pixels.begin(), pixels.end(), std::uint64_t{0}) != expected)
    {
        return 1;
    }
    return cancelled == 0 ? 0 : 1;
}

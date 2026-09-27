#include "../../Common/Jobs.h"
#include "../../Common/PixelBuffer.h"

#include "MemoryBenchmark.h"

#include <windows.h>

#include <psapi.h>
#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#pragma comment(lib, "Psapi.lib")

namespace
{
constexpr std::size_t kWidth = 8192;
constexpr std::size_t kHeight = 5464;
constexpr std::size_t kBytesPerPixel = 4;
constexpr std::size_t kBufferBytes = kWidth * kHeight * kBytesPerPixel;
constexpr std::size_t kWarmupPairs = 2;
constexpr std::size_t kSamplePairs = 10;
constexpr std::uint8_t kFillValue = 0x5Au;
constexpr std::uint64_t kExpectedChecksum = static_cast<std::uint64_t>(kBufferBytes) * kFillValue;

struct Distribution final
{
    double median = 0;
    double p95 = 0;
    double maximum = 0;
};

struct ProcessMemory final
{
    DWORD pageFaults = 0;
    SIZE_T workingSet = 0;
    SIZE_T privateUsage = 0;
};

bool CaptureProcessMemory(ProcessMemory& memory) noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
                              static_cast<DWORD>(sizeof(counters))))
    {
        return false;
    }

    memory.pageFaults = counters.PageFaultCount;
    memory.workingSet = counters.WorkingSetSize;
    memory.privateUsage = counters.PrivateUsage;
    return true;
}

bool ReadCounter(LARGE_INTEGER& counter) noexcept
{
    return QueryPerformanceCounter(&counter) != FALSE;
}

double ElapsedMicroseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& finish,
                           const LARGE_INTEGER& frequency) noexcept
{
    return static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 /
           static_cast<double>(frequency.QuadPart);
}

std::uint64_t Checksum(const PixelBuffer& pixels) noexcept
{
    std::uint64_t checksum = 0;
    for (std::size_t index = 0; index < pixels.Size(); ++index)
    {
        checksum += pixels.Data()[index];
    }
    return checksum;
}

Distribution Summarize(const std::array<double, kSamplePairs>& samples) noexcept
{
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    constexpr std::size_t p95Index = (kSamplePairs * 95 + 99) / 100 - 1;
    return {(sorted[kSamplePairs / 2 - 1] + sorted[kSamplePairs / 2]) / 2.0, sorted[p95Index], sorted.back()};
}

bool MeasureFresh(const LARGE_INTEGER& frequency, double& allocateUs, double& touchUs, double& releaseUs,
                  std::uint32_t& pageFaultDelta, ProcessMemory& afterTouchMemory) noexcept
{
    ProcessMemory before{};
    if (!CaptureProcessMemory(before))
    {
        return false;
    }

    LARGE_INTEGER allocateStart{};
    LARGE_INTEGER allocateFinish{};
    if (!ReadCounter(allocateStart))
    {
        return false;
    }
    PixelBuffer pixels = PixelBuffer::TryAllocate(kBufferBytes);
    if (!pixels || pixels.Size() != kBufferBytes)
    {
        std::printf("MEMORY BENCH status=failed reason=allocation bytes=%zu\n", kBufferBytes);
        return false;
    }
    if (!ReadCounter(allocateFinish))
    {
        return false;
    }

    LARGE_INTEGER touchStart{};
    LARGE_INTEGER touchFinish{};
    if (!ReadCounter(touchStart))
    {
        return false;
    }
    std::memset(pixels.Data(), kFillValue, pixels.Size());
    if (!ReadCounter(touchFinish))
    {
        return false;
    }

    ProcessMemory afterTouch{};
    if (!CaptureProcessMemory(afterTouch))
    {
        return false;
    }
    afterTouchMemory = afterTouch;
    if (Checksum(pixels) != kExpectedChecksum)
    {
        std::printf("MEMORY BENCH status=failed reason=fresh-checksum\n");
        return false;
    }

    LARGE_INTEGER releaseStart{};
    LARGE_INTEGER releaseFinish{};
    if (!ReadCounter(releaseStart))
    {
        return false;
    }
    pixels = PixelBuffer{};
    if (!ReadCounter(releaseFinish))
    {
        return false;
    }

    allocateUs = ElapsedMicroseconds(allocateStart, allocateFinish, frequency);
    touchUs = ElapsedMicroseconds(touchStart, touchFinish, frequency);
    releaseUs = ElapsedMicroseconds(releaseStart, releaseFinish, frequency);
    pageFaultDelta = static_cast<std::uint32_t>(afterTouch.pageFaults - before.pageFaults);
    return true;
}

bool MeasureReuse(PixelBuffer& pixels, const LARGE_INTEGER& frequency, double& touchUs, std::uint32_t& pageFaultDelta,
                  ProcessMemory& afterTouchMemory) noexcept
{
    ProcessMemory before{};
    if (!CaptureProcessMemory(before))
    {
        return false;
    }

    LARGE_INTEGER touchStart{};
    LARGE_INTEGER touchFinish{};
    if (!ReadCounter(touchStart))
    {
        return false;
    }
    std::memset(pixels.Data(), kFillValue, pixels.Size());
    if (!ReadCounter(touchFinish))
    {
        return false;
    }

    ProcessMemory afterTouch{};
    if (!CaptureProcessMemory(afterTouch))
    {
        return false;
    }
    afterTouchMemory = afterTouch;
    if (Checksum(pixels) != kExpectedChecksum)
    {
        std::printf("MEMORY BENCH status=failed reason=reuse-checksum\n");
        return false;
    }

    touchUs = ElapsedMicroseconds(touchStart, touchFinish, frequency);
    pageFaultDelta = static_cast<std::uint32_t>(afterTouch.pageFaults - before.pageFaults);
    return true;
}

void PrintMemory(const char* label, const ProcessMemory& memory) noexcept
{
    std::printf(" %s_ws_bytes=%llu %s_private_bytes=%llu", label, static_cast<unsigned long long>(memory.workingSet),
                label, static_cast<unsigned long long>(memory.privateUsage));
}

bool RunRecycleBenchmark(const LARGE_INTEGER& frequency) noexcept
{
    const std::uint64_t requiredBytes = PixelBuffer::RequiredBytes(kBufferBytes);
    if (requiredBytes == 0 || requiredBytes > (std::numeric_limits<std::uint64_t>::max)() / 2u)
    {
        std::printf("MEMORY RECYCLE BENCH status=failed reason=required-bytes\n");
        return false;
    }

    bool recycleInitialized = false;
    bool schedulerInitialized = false;
    JobScheduler scheduler;
    const auto closeResources = wil::scope_exit(
        [&]() noexcept
        {
            if (schedulerInitialized)
            {
                scheduler.Close();
            }
            if (recycleInitialized)
            {
                ClosePixelBufferRecycle();
            }
        });

    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(2));
    if (FAILED(initialized))
    {
        std::printf("MEMORY RECYCLE BENCH status=failed reason=scheduler-initialize hr=0x%08lX\n",
                    static_cast<unsigned long>(initialized));
        return false;
    }
    schedulerInitialized = true;
    if (!scheduler.EnableDeferredRelease())
    {
        std::printf("MEMORY RECYCLE BENCH status=failed reason=deferred-release-enable\n");
        return false;
    }
    if (!InitializePixelBufferRecycle(2u * requiredBytes))
    {
        std::printf("MEMORY RECYCLE BENCH status=failed reason=recycle-initialize\n");
        return false;
    }
    recycleInitialized = true;

    std::array<double, kSamplePairs> allocateSamples{};
    std::array<double, kSamplePairs> touchSamples{};
    std::array<double, kSamplePairs> uiRetireSamples{};
    std::array<double, kSamplePairs> settleSamples{};
    std::uint64_t pageFaultTotal = 0;
    std::uint64_t checksumTotal = 0;

    for (std::size_t iteration = 0; iteration < kWarmupPairs + kSamplePairs; ++iteration)
    {
        const std::uint64_t expectedCacheBytes = iteration == 0 ? 0 : requiredBytes;
        const std::uint64_t cacheBeforeReuse = PixelBufferRecycleBytes();
        if (cacheBeforeReuse != expectedCacheBytes)
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=cache-before-reuse iteration=%zu expected=%llu "
                        "actual=%llu\n",
                        iteration, static_cast<unsigned long long>(expectedCacheBytes),
                        static_cast<unsigned long long>(cacheBeforeReuse));
            return false;
        }

        ProcessMemory before{};
        if (!CaptureProcessMemory(before))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=process-memory-before iteration=%zu\n", iteration);
            return false;
        }

        LARGE_INTEGER allocateStart{};
        LARGE_INTEGER allocateFinish{};
        if (!ReadCounter(allocateStart))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-allocate-start iteration=%zu\n", iteration);
            return false;
        }
        PixelBuffer pixels = PixelBuffer::TryAllocate(kBufferBytes);
        if (!pixels || pixels.Size() != kBufferBytes || pixels.AllocationBytes() != requiredBytes)
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=allocation iteration=%zu bytes=%zu\n", iteration,
                        kBufferBytes);
            return false;
        }
        if (!ReadCounter(allocateFinish))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-allocate-finish iteration=%zu\n", iteration);
            return false;
        }

        LARGE_INTEGER touchStart{};
        LARGE_INTEGER touchFinish{};
        if (!ReadCounter(touchStart))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-touch-start iteration=%zu\n", iteration);
            return false;
        }
        std::memset(pixels.Data(), kFillValue, pixels.Size());
        if (!ReadCounter(touchFinish))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-touch-finish iteration=%zu\n", iteration);
            return false;
        }

        const std::uint64_t checksum = Checksum(pixels);
        if (checksum != kExpectedChecksum)
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=checksum iteration=%zu actual=%llu\n", iteration,
                        static_cast<unsigned long long>(checksum));
            return false;
        }
        ProcessMemory afterTouch{};
        if (!CaptureProcessMemory(afterTouch))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=process-memory-after iteration=%zu\n", iteration);
            return false;
        }

        LARGE_INTEGER retireStart{};
        LARGE_INTEGER retireFinish{};
        if (!ReadCounter(retireStart))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-retire-start iteration=%zu\n", iteration);
            return false;
        }
        pixels = PixelBuffer{};
        if (!ReadCounter(retireFinish))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-retire-finish iteration=%zu\n", iteration);
            return false;
        }

        LARGE_INTEGER settleStart{};
        LARGE_INTEGER settleFinish{};
        if (!ReadCounter(settleStart))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-settle-start iteration=%zu\n", iteration);
            return false;
        }
        const SettleResult settleResult = SettleJobs(scheduler, 30000);
        if (!ReadCounter(settleFinish))
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=qpc-settle-finish iteration=%zu\n", iteration);
            return false;
        }
        if (settleResult != SettleResult::Settled)
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=settle iteration=%zu result=%u\n", iteration,
                        static_cast<unsigned int>(settleResult));
            return false;
        }
        const std::uint64_t cacheAfterSettle = PixelBufferRecycleBytes();
        if (cacheAfterSettle != requiredBytes)
        {
            std::printf("MEMORY RECYCLE BENCH status=failed reason=cache-after-settle iteration=%zu expected=%llu "
                        "actual=%llu\n",
                        iteration, static_cast<unsigned long long>(requiredBytes),
                        static_cast<unsigned long long>(cacheAfterSettle));
            return false;
        }

        if (iteration >= kWarmupPairs)
        {
            const std::size_t sample = iteration - kWarmupPairs;
            allocateSamples[sample] = ElapsedMicroseconds(allocateStart, allocateFinish, frequency);
            touchSamples[sample] = ElapsedMicroseconds(touchStart, touchFinish, frequency);
            uiRetireSamples[sample] = ElapsedMicroseconds(retireStart, retireFinish, frequency);
            settleSamples[sample] = ElapsedMicroseconds(settleStart, settleFinish, frequency);
            pageFaultTotal += static_cast<std::uint32_t>(afterTouch.pageFaults - before.pageFaults);
            checksumTotal += checksum;
        }
    }

    const Distribution allocate = Summarize(allocateSamples);
    const Distribution touch = Summarize(touchSamples);
    const Distribution uiRetire = Summarize(uiRetireSamples);
    const Distribution settle = Summarize(settleSamples);
    std::printf(
        "MEMORY RECYCLE BENCH width=%zu height=%zu bytes=%zu required_bytes=%llu warmup=%zu samples=%zu threads=2 "
        "allocate_median_us=%.3f allocate_p95_us=%.3f allocate_max_us=%.3f "
        "touch_median_us=%.3f touch_p95_us=%.3f touch_max_us=%.3f "
        "ui_retire_median_us=%.3f ui_retire_p95_us=%.3f ui_retire_max_us=%.3f "
        "settle_median_us=%.3f settle_p95_us=%.3f settle_max_us=%.3f "
        "cache_bytes=%llu page_faults_total=%llu checksum=%llu "
        "ui_retire=enqueue_only worker_offload=deferred_release_worker status=ok\n",
        kWidth, kHeight, kBufferBytes, static_cast<unsigned long long>(requiredBytes), kWarmupPairs, kSamplePairs,
        allocate.median, allocate.p95, allocate.maximum, touch.median, touch.p95, touch.maximum, uiRetire.median,
        uiRetire.p95, uiRetire.maximum, settle.median, settle.p95, settle.maximum,
        static_cast<unsigned long long>(PixelBufferRecycleBytes()), static_cast<unsigned long long>(pageFaultTotal),
        static_cast<unsigned long long>(checksumTotal));

    scheduler.Close();
    schedulerInitialized = false;
    ClosePixelBufferRecycle();
    recycleInitialized = false;
    return true;
}
} // namespace

int RunMemoryBenchmark()
{
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        std::printf("MEMORY BENCH status=failed reason=qpc-frequency\n");
        return 1;
    }

    PixelBuffer reused = PixelBuffer::TryAllocate(kBufferBytes);
    if (!reused || reused.Size() != kBufferBytes)
    {
        std::printf("MEMORY BENCH status=failed reason=reuse-allocation bytes=%zu\n", kBufferBytes);
        return 1;
    }
    std::memset(reused.Data(), kFillValue, reused.Size());
    if (Checksum(reused) != kExpectedChecksum)
    {
        std::printf("MEMORY BENCH status=failed reason=reuse-prefault-checksum\n");
        return 1;
    }

    ProcessMemory initialMemory{};
    if (!CaptureProcessMemory(initialMemory))
    {
        std::printf("MEMORY BENCH status=failed reason=process-memory\n");
        return 1;
    }

    std::array<double, kSamplePairs> allocateSamples{};
    std::array<double, kSamplePairs> freshTouchSamples{};
    std::array<double, kSamplePairs> releaseSamples{};
    std::array<double, kSamplePairs> reuseTouchSamples{};
    std::array<std::uint32_t, kSamplePairs> freshPageFaults{};
    std::array<std::uint32_t, kSamplePairs> reusePageFaults{};
    ProcessMemory lastFreshTouchMemory{};
    ProcessMemory lastReuseTouchMemory{};

    for (std::size_t pair = 0; pair < kWarmupPairs + kSamplePairs; ++pair)
    {
        const bool freshFirst = (pair & 1u) == 0;
        double allocateUs = 0;
        double freshTouchUs = 0;
        double releaseUs = 0;
        double reuseTouchUs = 0;
        std::uint32_t freshFaults = 0;
        std::uint32_t reuseFaults = 0;
        ProcessMemory freshTouchMemory{};
        ProcessMemory reuseTouchMemory{};

        const auto runFresh = [&]() noexcept
        { return MeasureFresh(frequency, allocateUs, freshTouchUs, releaseUs, freshFaults, freshTouchMemory); };
        const auto runReuse = [&]() noexcept
        { return MeasureReuse(reused, frequency, reuseTouchUs, reuseFaults, reuseTouchMemory); };

        const bool succeeded = freshFirst ? (runFresh() && runReuse()) : (runReuse() && runFresh());
        if (!succeeded)
        {
            if (pair >= kWarmupPairs)
            {
                std::printf("MEMORY BENCH status=failed pair=%zu\n", pair - kWarmupPairs);
            }
            return 1;
        }

        if (pair >= kWarmupPairs)
        {
            const std::size_t sample = pair - kWarmupPairs;
            allocateSamples[sample] = allocateUs;
            freshTouchSamples[sample] = freshTouchUs;
            releaseSamples[sample] = releaseUs;
            reuseTouchSamples[sample] = reuseTouchUs;
            freshPageFaults[sample] = freshFaults;
            reusePageFaults[sample] = reuseFaults;
            lastFreshTouchMemory = freshTouchMemory;
            lastReuseTouchMemory = reuseTouchMemory;
        }
    }

    const Distribution allocate = Summarize(allocateSamples);
    const Distribution freshTouch = Summarize(freshTouchSamples);
    const Distribution release = Summarize(releaseSamples);
    const Distribution reuseTouch = Summarize(reuseTouchSamples);
    std::uint64_t freshFaultTotal = 0;
    std::uint64_t reuseFaultTotal = 0;
    for (std::size_t index = 0; index < kSamplePairs; ++index)
    {
        freshFaultTotal += freshPageFaults[index];
        reuseFaultTotal += reusePageFaults[index];
    }

    std::printf("MEMORY BENCH width=%zu height=%zu bytes=%zu warmup_pairs=%zu samples=%zu threads=1 "
                "fresh_allocate_median_us=%.3f fresh_allocate_p95_us=%.3f fresh_allocate_max_us=%.3f "
                "fresh_touch_median_us=%.3f fresh_touch_p95_us=%.3f fresh_touch_max_us=%.3f "
                "fresh_release_median_us=%.3f fresh_release_p95_us=%.3f fresh_release_max_us=%.3f "
                "reuse_touch_median_us=%.3f reuse_touch_p95_us=%.3f reuse_touch_max_us=%.3f "
                "fresh_page_faults_total=%llu reuse_page_faults_total=%llu checksum=%llu scope=timed_qpc_phases_only",
                kWidth, kHeight, kBufferBytes, kWarmupPairs, kSamplePairs, allocate.median, allocate.p95,
                allocate.maximum, freshTouch.median, freshTouch.p95, freshTouch.maximum, release.median, release.p95,
                release.maximum, reuseTouch.median, reuseTouch.p95, reuseTouch.maximum,
                static_cast<unsigned long long>(freshFaultTotal), static_cast<unsigned long long>(reuseFaultTotal),
                static_cast<unsigned long long>(kExpectedChecksum));
    PrintMemory("initial", initialMemory);
    PrintMemory("fresh_touch", lastFreshTouchMemory);
    PrintMemory("reuse_touch", lastReuseTouchMemory);
    std::printf(" status=ok\n");
    return RunRecycleBenchmark(frequency) ? 0 : 1;
}

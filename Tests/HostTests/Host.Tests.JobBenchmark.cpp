#include "Jobs.h"

#include <windows.h>

#include "Host.Tests.JobBenchmark.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
constexpr std::size_t kWarmupCount = 8;
constexpr std::size_t kSampleCount = 200;
constexpr std::uint32_t kSettleTimeoutMs = 10000;
constexpr std::uint32_t kChecksumInput = 0x00001234u;
constexpr std::uint32_t kExpectedJobResult = 0xB8FD5370u;

struct CompletionState final
{
    std::uint64_t expectedChecksum = 0;
    std::uint64_t checksum = 0;
    std::size_t count = 0;
    bool valid = true;
};

class BenchmarkJob final : public Job
{
  public:
    BenchmarkJob(std::uint32_t input, CompletionState& completion) noexcept : _input(input), _completion(completion) {}

    JobResult Run(JobContext&) override
    {
        _result = (_input ^ 0xA5A5A5A5u) + 0x13579BDFu;
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _completion.valid = _completion.valid && result.status == JobStatus::Finished && result.error == 0 &&
                            _input == kChecksumInput && _result == kExpectedJobResult;
        _completion.checksum += _result;
        ++_completion.count;
    }

  private:
    const std::uint32_t _input;
    CompletionState& _completion;
    std::uint32_t _result = 0;
};

struct Distribution final
{
    double median = 0;
    double p95 = 0;
    double maximum = 0;
};

Distribution Summarize(const std::array<double, kSampleCount>& samples) noexcept
{
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    constexpr std::size_t p95Index = (kSampleCount * 95 + 99) / 100 - 1;
    return {(sorted[kSampleCount / 2 - 1] + sorted[kSampleCount / 2]) / 2.0, sorted[p95Index], sorted.back()};
}

double ElapsedMicroseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& finish,
                           const LARGE_INTEGER& frequency) noexcept
{
    return (static_cast<double>(finish.QuadPart - start.QuadPart) * 1000000.0) /
           static_cast<double>(frequency.QuadPart);
}
} // namespace

int RunJobBenchmark()
{
    CompletionState completion;
    JobScheduler scheduler;
    const HRESULT initializeResult = static_cast<HRESULT>(scheduler.Initialize());
    if (FAILED(initializeResult))
    {
        scheduler.Close();
        std::printf("JOB BENCH samples=0 warmup=%zu threads=0 class=commit submit_median_us=0 submit_p95_us=0 "
                    "submit_max_us=0 roundtrip_median_us=0 roundtrip_p95_us=0 roundtrip_max_us=0 checksum=0 "
                    "status=failed hr=0x%08lX\n",
                    kWarmupCount, static_cast<unsigned long>(initializeResult));
        return 1;
    }

    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        const unsigned long threadCount = static_cast<unsigned long>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        scheduler.Close();
        std::printf("JOB BENCH samples=0 warmup=%zu threads=%lu class=commit submit_median_us=0 submit_p95_us=0 "
                    "submit_max_us=0 roundtrip_median_us=0 roundtrip_p95_us=0 roundtrip_max_us=0 checksum=0 "
                    "status=failed reason=qpc-frequency\n",
                    kWarmupCount, threadCount);
        return 1;
    }

    const unsigned long threadCount = static_cast<unsigned long>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    const std::size_t totalCount = kWarmupCount + kSampleCount;
    completion.expectedChecksum = static_cast<std::uint64_t>(kExpectedJobResult) * totalCount;
    std::array<double, kSampleCount> submitSamples{};
    std::array<double, kSampleCount> roundtripSamples{};
    bool success = threadCount != 0;

    for (std::size_t index = 0; index < totalCount && success; ++index)
    {
        // Allocate the owned payload before either timing interval starts.
        std::unique_ptr<Job> job = std::make_unique<BenchmarkJob>(kChecksumInput, completion);
        JobTicket ticket;
        LARGE_INTEGER roundtripStart{};
        LARGE_INTEGER submitStart{};
        LARGE_INTEGER submitFinish{};
        LARGE_INTEGER roundtripFinish{};

        if (!QueryPerformanceCounter(&roundtripStart) || !QueryPerformanceCounter(&submitStart))
        {
            success = false;
            break;
        }
        const JobOptions options{JobClass::Commit, 0};
        const HRESULT submitResult = static_cast<HRESULT>(scheduler.Submit(job, options, ticket));
        if (!QueryPerformanceCounter(&submitFinish))
        {
            success = false;
            break;
        }
        if (FAILED(submitResult) || job)
        {
            success = false;
            break;
        }

        const SettleResult settleResult = SettleJobs(scheduler, kSettleTimeoutMs);
        if (!QueryPerformanceCounter(&roundtripFinish))
        {
            success = false;
            break;
        }
        if (settleResult != SettleResult::Settled || !scheduler.IsIdle())
        {
            success = false;
            break;
        }

        if (index >= kWarmupCount)
        {
            const std::size_t sampleIndex = index - kWarmupCount;
            submitSamples[sampleIndex] = ElapsedMicroseconds(submitStart, submitFinish, frequency);
            roundtripSamples[sampleIndex] = ElapsedMicroseconds(roundtripStart, roundtripFinish, frequency);
        }
    }

    scheduler.Close();
    success = success && completion.valid && completion.count == totalCount &&
              completion.checksum == completion.expectedChecksum;

    if (success)
    {
        const Distribution submit = Summarize(submitSamples);
        const Distribution roundtrip = Summarize(roundtripSamples);
        std::printf("JOB BENCH samples=%zu warmup=%zu threads=%lu class=commit submit_median_us=%.3f "
                    "submit_p95_us=%.3f submit_max_us=%.3f roundtrip_median_us=%.3f roundtrip_p95_us=%.3f "
                    "roundtrip_max_us=%.3f checksum=%llu status=ok\n",
                    kSampleCount, kWarmupCount, threadCount, submit.median, submit.p95, submit.maximum,
                    roundtrip.median, roundtrip.p95, roundtrip.maximum,
                    static_cast<unsigned long long>(completion.checksum));
        return 0;
    }

    std::printf("JOB BENCH samples=0 warmup=%zu threads=%lu class=commit submit_median_us=0 submit_p95_us=0 "
                "submit_max_us=0 roundtrip_median_us=0 roundtrip_p95_us=0 roundtrip_max_us=0 checksum=%llu "
                "status=failed\n",
                kWarmupCount, threadCount, static_cast<unsigned long long>(completion.checksum));
    return 1;
}

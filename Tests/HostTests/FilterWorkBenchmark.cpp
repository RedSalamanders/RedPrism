#include "FilterWorkBenchmark.h"

#include "../Support/DocumentFixture.h"
#include "Adjust.h"
#include "BenchmarkMachine.h"
#include "CanvasSize.h"
#include "ColorEditWork.h"
#include "Jobs.h"
#include "Kernels.h"
#include "RasterWork.h"
#include "Selection.h"

#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
constexpr int kWidth = 8192;
constexpr int kHeight = 5464;
constexpr int kSamples = 10;
constexpr DWORD kCancelDelayMs = 20;

struct Observation final
{
    wil::unique_event_nothrow started;
    wil::unique_event_nothrow returned;
    JobTicket ticket{};
    bool returnClockValid = false;
    LARGE_INTEGER finish{};
    JobResult completion;
    bool produced = false;
    std::optional<ResizeStatus> resizeStatus;
    CanvasDocument resized;
};

// One original job per sample. Set current before Submit; ticket assignment precedes dispatch. Change/clear
// only after joined SettleJobs or Close. The context and borrowed event outlive the scheduler's Close.
struct FilterExecutionObservation final
{
    Observation* current = nullptr;
};

void NoteFilterBodyBoundary(void* opaque, JobExecutionNotice notice) noexcept
{
    auto& context = *static_cast<FilterExecutionObservation*>(opaque);
    auto* const current = context.current;
    if (!current || notice.ticket != current->ticket || notice.boundary != JobExecutionBoundary::BodyExited)
        return;
    // Run's locals and its stack have unwound. This timestamp excludes later worker retirement and owner apply.
    current->returnClockValid = QueryPerformanceCounter(&current->finish) != FALSE;
    current->returned.SetEvent();
}

class FilterWork final : public Job
{
  public:
    FilterWork(const Raster& source, const Raster& mask, int operation, Observation& observed)
        : _source(source), _mask(mask), _operation(operation), _observed(observed)
    {
    }
    JobResult Run(JobContext& context) override
    {
        const CancelToken& token = context.cancellation;
        constexpr std::array<FilterKind, 11> kinds{
            FilterKind::Levels,           FilterKind::HueSaturation, FilterKind::Curves,
            FilterKind::Exposure,         FilterKind::GradientMap,   FilterKind::Grain,
            FilterKind::GaussianBlur,     FilterKind::MotionBlur,    FilterKind::AddNoise,
            FilterKind::ContentAwareFill, FilterKind::LensCorrection};
        FilterSettings settings;
        settings.levels.ranges[0].gamma = 1.5;
        settings.hsv = Adjust::HsvSettings(25, 20, 10);
        settings.exposure.exposure = 0.5;
        settings.grain.amount = 40;
        settings.radius = 250; // Slowest supported Gaussian taps, with its real initialization/cleanup.
        settings.distance = 2000;
        settings.amount = 20;
        settings.distortion = 35;
        const auto loops = SelectionGeometry::Rect(0.25, 0.25, kWidth - 0.5, kHeight - 0.5);
        _observed.started.SetEvent();
        Raster output;
        if (_operation < static_cast<int>(kinds.size()))
        {
            output = Adjust::RunFilter(kinds[static_cast<std::size_t>(_operation)], _source, settings, 1.0, 0x1234ABCDu,
                                       &_mask, nullptr, token);
        }
        else
        {
            switch (_operation)
            {
            case 11:
                _observed.produced = Adjust::Histogram(_source, &_mask, token)[0] != std::array<double, 256>{};
                break;
            case 12:
                output = SelectionGeometry::Coverage(loops, kWidth, kHeight, true, token);
                break;
            case 13:
                output = SelectionGeometry::Coverage(loops, kWidth, kHeight, false, token);
                break;
            case 14:
                output = Adjust::Downscaled(_source, 1024, token);
                break;
            case 15:
                output = CarriedMaskForWork(_mask, kWidth, kHeight, 16, 16, kWidth + 32, kHeight + 32, 255, token);
                break;
            case 16:
            {
                const Raster sparse = Raster::MakeSparse(kWidth, kHeight, RasterFormat::Bgra8888, _source, 0, 0, {}, 0);
                output = ContiguousRasterForWork(sparse, token);
                break;
            }
            }
        }
        _observed.produced = _observed.produced || output.HasStorage();

        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _observed.completion = result;
    }

  private:
    Raster _source;
    Raster _mask;
    int _operation;
    Observation& _observed;
};

class SizingWork final : public Job
{
  public:
    SizingWork(const CanvasDocument& source, std::variant<CanvasSizeOptions, ImageSizeOptions> options,
               Observation& observed)
        : _source(source), _options(std::move(options)), _observed(observed)
    {
    }

    JobResult Run(JobContext& context) override
    {
        _observed.started.SetEvent();
        _observed.resizeStatus = std::visit(
            [&](const auto& options)
            {
                using Options = std::decay_t<decltype(options)>;
                if constexpr (std::is_same_v<Options, CanvasSizeOptions>)
                    return ResizeCanvas(_source, options, _observed.resized, context.cancellation);
                else
                    return ResizeImage(_source, options, _observed.resized, context.cancellation);
            },
            _options);
        const ResizeStatus status = _observed.resizeStatus.value();
        _observed.produced = status == ResizeStatus::Ok || status == ResizeStatus::Unchanged;

        return context.cancellation.IsCancelled() || status == ResizeStatus::Cancelled ? JobResult{JobStatus::Cancelled}
                                                                                       : JobResult{};
    }

    void Completed(JobResult result) noexcept override
    {
        _observed.completion = result;
    }

  private:
    const CanvasDocument& _source;
    const std::variant<CanvasSizeOptions, ImageSizeOptions> _options;
    Observation& _observed;
};
} // namespace

int RunFilterWorkBenchmark()
{
    // Explicit scalar reference diagnostic; topology/report allocation is outside every measured interval.
    if (InitializeKernels(KernelIsa::Scalar) != KernelSelection::Selected)
        return 1;
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    PixelBuffer bytes = PixelBuffer::TryAllocate(pixels * 4);
    PixelBuffer maskBytes = PixelBuffer::TryAllocate(pixels);
    if (!bytes || !maskBytes)
    {
        return 1;
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel)
    {
        bytes[pixel * 4] = static_cast<std::uint8_t>(pixel * 17u);
        bytes[pixel * 4 + 1] = static_cast<std::uint8_t>(pixel * 13u);
        bytes[pixel * 4 + 2] = static_cast<std::uint8_t>(pixel * 7u);
        bytes[pixel * 4 + 3] = 255;
        const auto x = pixel % kWidth;
        const auto y = pixel / kWidth;
        maskBytes[pixel] = x > kWidth / 3 && x < kWidth * 2 / 3 && y > kHeight / 3 && y < kHeight * 2 / 3 ? 255 : 0;
    }
    const Raster source = Raster::Adopt(kWidth, kHeight, RasterFormat::Bgra8888, std::move(bytes));
    const Raster mask = Raster::Adopt(kWidth, kHeight, RasterFormat::Gray8, std::move(maskBytes));
    constexpr std::array names{"levels",
                               "hsv",
                               "curves",
                               "exposure",
                               "gradient-map",
                               "grain",
                               "gaussian-250",
                               "motion-2000",
                               "noise",
                               "content-fill",
                               "lens",
                               "histogram",
                               "coverage-aa",
                               "coverage-hard",
                               "downscale",
                               "carried-mask",
                               "sparse-copy",
                               "sizing-canvas-fill",
                               "sizing-image-nearest",
                               "sizing-image-high"};
    DocumentFixture sizingFixture;
    sizingFixture.id = Guid::Generate();
    sizingFixture.width = kWidth;
    sizingFixture.height = kHeight;
    ImageLayer sizingLayer =
        ImageLayer::Blank(L"Benchmark source", {static_cast<double>(kWidth), static_cast<double>(kHeight)});
    sizingLayer.asset = RasterAsset{source, {}, sizingLayer.name};
    sizingFixture.layers.push_back(std::move(sizingLayer));
    const CanvasDocument sizingSource = std::move(sizingFixture).Seal();
    const CanvasDocument originalSizingSource = sizingSource;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        return 1;
    }
    FilterExecutionObservation executionObservation;
    JobScheduler scheduler;
    if (scheduler.Initialize(0, {}, {}, {NoteFilterBodyBoundary, &executionObservation}) < 0)
    {
        return 1;
    }
    auto machine = ReadBenchmarkMachine();
    machine.isa = "scalar";
    machine.workerCount = scheduler.WorkerCount();
    std::printf("BENCH MACHINE %s harness=filter-cancel\n", FormatBenchmarkMachineInfo(machine).c_str());
    std::printf("FILTER CANCEL METHOD clock=QPC returnBoundary=job-body-exited warmup=1 samplesPerCase=10 "
                "excludes=worker-retirement,owner-apply\n");
    bool succeeded = true;
    for (std::size_t operation = 0; operation < names.size() && succeeded; ++operation)
    {
        if (operation >= names.size())
        {
            succeeded = false;
            break;
        }
        const char* const operationName = names[operation];
        std::vector<double> latencies;
        latencies.reserve(kSamples);
        int early = 0;
        int overlap = 0;
        int cancelled = 0;
        const bool sizing = operation >= 17;
        std::variant<CanvasSizeOptions, ImageSizeOptions> sizingOptions;
        std::uint64_t reservation = 2ull * 1024 * 1024 * 1024;
        if (operation == 17)
        {
            CanvasSizeOptions options;
            options.width = kWidth + 128;
            options.height = kHeight + 128;
            options.anchor = 8;
            options.fill = CanvasExtensionColor{1, 0, 0};
            sizingOptions = options;
            reservation = SizingMemoryBytes(sizingSource, options);
        }
        else if (operation >= 18)
        {
            ImageSizeOptions options;
            options.width = kWidth * 3 / 4;
            options.height = kHeight * 3 / 4;
            options.resolution = 72;
            options.sampling = operation == 18 ? LayerSampling::Nearest : LayerSampling::High;
            sizingOptions = options;
            reservation = SizingMemoryBytes(sizingSource, options);
        }
        if (reservation == 0 || reservation > 2ull * 1024 * 1024 * 1024)
        {
            succeeded = false;
        }
        for (int sample = -1; sample < kSamples; ++sample)
        {
            if (!succeeded)
                break;
            Observation observation;
            if (FAILED(observation.started.create(wil::EventOptions::ManualReset)) ||
                FAILED(observation.returned.create(wil::EventOptions::ManualReset)))
            {
                succeeded = false;
                break;
            }
            std::unique_ptr<Job> job;
            if (sizing)
                job = std::make_unique<SizingWork>(sizingSource, sizingOptions, observation);
            else
                job = std::make_unique<FilterWork>(source, mask, static_cast<int>(operation), observation);
            executionObservation.current = &observation;
            // This benchmark runs one job at a time. 2 GiB covers the largest 45 MP case's whole graph;
            // production filter jobs use ColorEditJobMemoryBytes instead of this measurement-only ceiling.
            if (scheduler.Submit(job, {JobClass::Commit, 0, reservation}, observation.ticket) < 0 ||
                WaitForSingleObject(observation.started.get(), 5000) != WAIT_OBJECT_0)
            {
                scheduler.Close(); // Join before the observation and its handles leave scope.
                succeeded = false;
                break;
            }
            const DWORD wait = WaitForSingleObject(observation.returned.get(), kCancelDelayMs);
            LARGE_INTEGER requested{};
            if (wait == WAIT_TIMEOUT)
            {
                if (!QueryPerformanceCounter(&requested) || !scheduler.Cancel(observation.ticket))
                {
                    scheduler.Close(); // Join every observer before the current sample leaves scope.
                    executionObservation.current = nullptr;
                    succeeded = false;
                    break;
                }
            }
            if ((wait != WAIT_TIMEOUT && wait != WAIT_OBJECT_0) ||
                SettleJobs(scheduler, 10000) != SettleResult::Settled)
            {
                scheduler.Close();
                succeeded = false;
                break;
            }
            executionObservation.current = nullptr; // Exact original completion joined every body observation.
            if (!observation.returnClockValid || observation.finish.QuadPart < 0)
            {
                std::printf("FILTER CANCEL ERROR name=%s reason=body-return-clock\n", operationName);
                succeeded = false;
                break;
            }
            if (observation.completion.status == JobStatus::Failed)
            {
                std::printf("FILTER CANCEL ERROR name=%s hr=0x%08X\n", operationName,
                            static_cast<unsigned int>(observation.completion.error));
                succeeded = false;
                break;
            }
            if (sizing)
            {
                const ResizeStatus status = observation.resizeStatus.value_or(ResizeStatus::Invalid);
                const bool emptyResult = observation.resized == CanvasDocument{};
                const bool validFinishedResult = (status == ResizeStatus::Ok || status == ResizeStatus::Unchanged) &&
                                                 observation.resized.width > 0 && observation.resized.height > 0 &&
                                                 !observation.resized.layers.empty();
                const bool validCancelledResult = status == ResizeStatus::Cancelled && emptyResult;
                if ((observation.produced && !validFinishedResult) ||
                    (status == ResizeStatus::Cancelled && !validCancelledResult) ||
                    (!observation.produced && status != ResizeStatus::Cancelled) ||
                    (observation.completion.status == JobStatus::Finished && !observation.produced) ||
                    (status == ResizeStatus::Cancelled && observation.completion.status != JobStatus::Cancelled) ||
                    !(sizingSource == originalSizingSource))
                {
                    std::printf("FILTER CANCEL ERROR name=%s reason=invalid-sizing-result resize_status=%u "
                                "completion=%u empty=%u\n",
                                operationName, static_cast<unsigned int>(status),
                                static_cast<unsigned int>(observation.completion.status), emptyResult ? 1u : 0u);
                    succeeded = false;
                    break;
                }
            }
            if (wait == WAIT_OBJECT_0 && !observation.produced)
            {
                std::printf("FILTER CANCEL ERROR name=%s reason=empty-before-cancellation\n", operationName);
                succeeded = false;
                break;
            }
            if (sample < 0)
            {
                continue;
            }
            if (wait == WAIT_OBJECT_0)
            {
                ++early;
                continue;
            }
            if (observation.produced || observation.finish.QuadPart < requested.QuadPart)
            {
                ++overlap; // Work can finish between the timeout and stop request; never call this cancellation.
                continue;
            }
            if (sizing)
            {
                if (observation.resizeStatus != ResizeStatus::Cancelled || !(observation.resized == CanvasDocument{}) ||
                    observation.completion.status != JobStatus::Cancelled)
                {
                    std::printf("FILTER CANCEL ERROR name=%s reason=cancelled-sizing-result-not-empty-safe\n",
                                operationName);
                    succeeded = false;
                    break;
                }
                ++cancelled;
            }
            latencies.push_back(static_cast<double>(observation.finish.QuadPart - requested.QuadPart) * 1000.0 /
                                static_cast<double>(frequency.QuadPart));
        }
        std::sort(latencies.begin(), latencies.end());
        const auto count = latencies.size();
        const double median = count == 0 ? 0 : (latencies[(count - 1) / 2] + latencies[count / 2]) / 2;
        const double p95 = count == 0 ? 0 : latencies[(count * 95 + 99) / 100 - 1];
        const double maximum = count == 0 ? 0 : latencies.back();
        if (sizing)
            std::printf("FILTER CANCEL name=%s width=%d height=%d delay_ms=%lu samples=%zu early=%d overlap=%d "
                        "cancelled_empty=%d peak_bytes=%llu median_ms=%.3f p95_ms=%.3f max_ms=%.3f status=%s\n",
                        operationName, kWidth, kHeight, kCancelDelayMs, count, early, overlap, cancelled,
                        static_cast<unsigned long long>(reservation), median, p95, maximum,
                        succeeded ? "ok" : "failed");
        else
            std::printf("FILTER CANCEL name=%s width=%d height=%d delay_ms=%lu samples=%zu early=%d overlap=%d "
                        "median_ms=%.3f p95_ms=%.3f max_ms=%.3f status=%s\n",
                        operationName, kWidth, kHeight, kCancelDelayMs, count, early, overlap, median, p95, maximum,
                        succeeded ? "ok" : "failed");
        std::fflush(stdout);
    }
    scheduler.Close();
    return succeeded ? 0 : 1;
}

#include "KernelBenchmark.h"

#include "../Common/Adjust.h"
#include "../Common/ImageFilters.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace
{
#if defined(_M_ARM64)
constexpr const char* kArch = "arm64";
#elif defined(_M_X64)
constexpr const char* kArch = "x64";
#else
constexpr const char* kArch = "unknown";
#endif

#if defined(__SANITIZE_ADDRESS__)
constexpr const char* kConfiguration = "asan_debug";
#elif defined(_DEBUG)
constexpr const char* kConfiguration = "debug";
#else
constexpr const char* kConfiguration = "release";
#endif

[[nodiscard]] std::uint64_t HashBytes(const std::uint8_t* bytes, std::size_t count) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < count; ++i)
    {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::uint64_t HashHistogram(const LevelsHistogram& histogram) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& channel : histogram)
    {
        for (const double bin : channel)
        {
            hash = (hash ^ std::bit_cast<std::uint64_t>(bin)) * 1099511628211ull;
        }
    }
    return hash;
}

template <typename Prepare, typename Invoke, typename Consume>
[[nodiscard]] bool Measure(const char* name, int width, int height, int samples, Prepare prepare, Invoke invoke,
                           Consume consume)
{
    std::vector<double> milliseconds;
    milliseconds.reserve(static_cast<std::size_t>(samples));
    std::uint64_t checksum = 0;
    for (int run = -1; run < samples; ++run)
    {
        prepare();
        const auto begin = std::chrono::steady_clock::now();
        const bool succeeded = invoke();
        const auto end = std::chrono::steady_clock::now();
        if (!succeeded)
        {
            std::fprintf(stderr, "KERNEL BENCH ERROR name=%s width=%d height=%d run=%d\n", name, width, height, run);
            return false;
        }
        const std::uint64_t output = consume();
        if (run == -1)
        {
            checksum = output;
        }
        else
        {
            if (output != checksum)
            {
                std::fprintf(stderr,
                             "KERNEL BENCH ERROR name=%s width=%d height=%d run=%d checksum=%016llx "
                             "expected=%016llx\n",
                             name, width, height, run, static_cast<unsigned long long>(output),
                             static_cast<unsigned long long>(checksum));
                return false;
            }
            milliseconds.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    const std::size_t count = milliseconds.size();
    const double median =
        count % 2 == 0 ? (milliseconds[count / 2 - 1] + milliseconds[count / 2]) / 2.0 : milliseconds[count / 2];
    const std::size_t p95Index = (count * 95 + 99) / 100 - 1; // nearest rank
    std::printf("KERNEL BENCH name=%s width=%d height=%d samples=%zu warmup=1 median_ms=%.3f p95_ms=%.3f "
                "max_ms=%.3f checksum=%016llx arch=%s config=%s\n",
                name, width, height, count, median, milliseconds[p95Index], milliseconds.back(),
                static_cast<unsigned long long>(checksum), kArch, kConfiguration);
    std::fflush(stdout);
    return true;
}

[[nodiscard]] Raster MakeInput(int width, int height)
{
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<std::uint8_t> pixels(count);
    for (std::size_t pixel = 0; pixel < count / 4; ++pixel)
    {
        const std::uint8_t alpha = static_cast<std::uint8_t>((pixel * 37u + pixel / 97u) & 255u);
        const std::size_t offset = pixel * 4;
        pixels[offset] = static_cast<std::uint8_t>((static_cast<unsigned>(alpha) * ((pixel * 17u) & 255u)) / 255u);
        pixels[offset + 1] = static_cast<std::uint8_t>((static_cast<unsigned>(alpha) * ((pixel * 29u) & 255u)) / 255u);
        pixels[offset + 2] = static_cast<std::uint8_t>((static_cast<unsigned>(alpha) * ((pixel * 43u) & 255u)) / 255u);
        pixels[offset + 3] = alpha;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] bool Run(int width, int height, int iterations)
{
    // This benchmark caps work at 100 MP and 10,000 pixels per side; Raster itself permits sides up to 30,000.
    if (width < 1 || height < 1 || width > 10000 || height > 10000 || iterations < 1 || iterations > 100 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 100000000ull ||
        width > std::numeric_limits<int>::max() / 4)
    {
        std::fputs("KERNEL BENCH ERROR invalid_dimensions_or_iterations\n", stderr);
        return false;
    }

    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    const int stride = width * 4;
    const Raster source = MakeInput(width, height);
    if (!source.HasStorage() || source.Data() == nullptr || source.ByteSize() != byteCount || source.Width() != width ||
        source.Height() != height || source.Stride() != stride)
    {
        std::fputs("KERNEL BENCH ERROR input_allocation\n", stderr);
        return false;
    }

    LevelsSettings levels;
    Adjust::SetLevelsCurrent(levels, LevelRange{18, 1.2, 238, 4, 250});
    const Adjust::ChannelTables levelsTable = Adjust::LevelsTables(levels);
    const Adjust::ChannelTables exposureTable = Adjust::ExposureTables(ExposureSettings{0.75, 0.02, 1.1});
    const std::vector<float> cube = Adjust::ColorCube(Adjust::HsvSettings(23, 15, 4));
    if (cube.size() !=
        static_cast<std::size_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension * Adjust::kCubeDimension * 4)
    {
        std::fputs("KERNEL BENCH ERROR cube_setup\n", stderr);
        return false;
    }

    {
        std::vector<std::uint8_t> work(byteCount);
        const auto prepare = [&] { std::memcpy(work.data(), source.Data(), byteCount); };
        const auto consume = [&] { return HashBytes(work.data(), work.size()); };
        if (!Measure(
                "levels_scalar", width, height, iterations, prepare,
                [&]
                {
                    Adjust::ApplyTables(work.data(), width, height, stride, levelsTable);
                    return true;
                },
                consume) ||
            !Measure(
                "exposure_scalar", width, height, iterations, prepare,
                [&]
                {
                    Adjust::ApplyTables(work.data(), width, height, stride, exposureTable);
                    return true;
                },
                consume) ||
            !Measure(
                "hue_saturation_scalar", width, height, iterations, prepare,
                [&]
                {
                    Adjust::ApplyCube(work.data(), width, height, stride, cube);
                    return true;
                },
                consume) ||
            !Measure(
                "uniform_noise_scalar", width, height, iterations, prepare,
                [&]
                {
                    Adjust::AddNoise(work.data(), width, height, stride, 12.0f, false, false, 0x12345678u);
                    return true;
                },
                consume))
        {
            return false;
        }
    }

    LevelsHistogram histogram{};
    if (!Measure(
            "histogram_scalar", width, height, iterations, [] {},
            [&]
            {
                histogram = Adjust::Histogram(source, nullptr);
                return true;
            },
            [&] { return HashHistogram(histogram); }))
    {
        return false;
    }

    LayerAdjustment levelsAdjustment;
    levelsAdjustment.kind = AdjustmentKind::Levels;
    levelsAdjustment.levels = levels;
    LayerAdjustment hsvAdjustment;
    hsvAdjustment.kind = AdjustmentKind::Hsv;
    hsvAdjustment.hsvSettings = Adjust::HsvSettings(23, 15, 4);
    FilterSettings noiseSettings;
    noiseSettings.amount = 12;
    noiseSettings.gaussian = false;
    noiseSettings.monochromatic = false;
    Raster wrapped;
    const auto validWrapped = [&]
    {
        return wrapped.HasStorage() && wrapped.Data() != nullptr && wrapped.ByteSize() == byteCount &&
               wrapped.Width() == width && wrapped.Height() == height && wrapped.Format() == RasterFormat::Bgra8888;
    };
    const auto wrappedChecksum = [&] { return HashBytes(wrapped.Data(), byteCount); };
    if (!Measure(
            "apply_adjustment_levels_output", width, height, iterations, [&] { wrapped = {}; },
            [&]
            {
                wrapped = Adjust::ApplyAdjustment(levelsAdjustment, source);
                return validWrapped();
            },
            wrappedChecksum) ||
        !Measure(
            "apply_adjustment_hsv_output", width, height, iterations, [&] { wrapped = {}; },
            [&]
            {
                wrapped = Adjust::ApplyAdjustment(hsvAdjustment, source);
                return validWrapped();
            },
            wrappedChecksum) ||
        !Measure(
            "run_filter_uniform_noise_output", width, height, iterations, [&] { wrapped = {}; },
            [&]
            {
                wrapped = Adjust::RunFilter(FilterKind::AddNoise, source, noiseSettings, 1.0, 0x12345678u);
                return validWrapped();
            },
            wrappedChecksum))
    {
        return false;
    }
    wrapped = {};

    Raster blurred;
    const int blurSamples = width == 8192 && height == 5464 ? std::min(iterations, 3) : iterations;
    if (!Measure(
            "gaussian_sigma10_scalar", width, height, blurSamples, [&] { blurred = {}; },
            [&]
            {
                blurred = ImageFilters::GaussianBlur(source, 10.0);
                return blurred.HasStorage() && blurred.Data() != nullptr && blurred.ByteSize() == byteCount &&
                       blurred.Width() == width && blurred.Height() == height;
            },
            [&] { return HashBytes(blurred.Data(), byteCount); }))
    {
        return false;
    }
    blurred = {};

    Raster made;
    if (!Measure(
            "raster_make_zero_first_touch", width, height, iterations, [&] { made = {}; },
            [&]
            {
                made = Raster::Make(width, height, RasterFormat::Bgra8888);
                return made.HasStorage() && made.Data() != nullptr && made.ByteSize() == byteCount;
            },
            [&] { return HashBytes(made.Data(), byteCount); }) ||
        !Measure(
            "raster_make_copy", width, height, iterations, [&] { made = {}; },
            [&]
            {
                made = Raster::Make(width, height, RasterFormat::Bgra8888, source.Data(), byteCount);
                return made.HasStorage() && made.Data() != nullptr && made.ByteSize() == byteCount;
            },
            [&] { return HashBytes(made.Data(), byteCount); }))
    {
        return false;
    }
    made = {};

    std::vector<std::uint8_t> raw;
    if (!Measure(
            "vector_zero_first_touch", width, height, iterations, [&] { std::vector<std::uint8_t>().swap(raw); },
            [&]
            {
                raw = std::vector<std::uint8_t>(byteCount);
                return raw.size() == byteCount;
            },
            [&] { return HashBytes(raw.data(), raw.size()); }) ||
        !Measure(
            "vector_copy", width, height, iterations, [&] { std::vector<std::uint8_t>().swap(raw); },
            [&]
            {
                raw = std::vector<std::uint8_t>(source.Data(), source.Data() + byteCount);
                return raw.size() == byteCount;
            },
            [&] { return HashBytes(raw.data(), raw.size()); }))
    {
        return false;
    }
    std::printf("KERNEL BENCH REPORT status=ok width=%d height=%d operations=13 arch=%s config=%s\n", width, height,
                kArch, kConfiguration);
    std::fflush(stdout);
    return true;
}

[[nodiscard]] double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    return count % 2 == 0 ? (samples[count / 2 - 1] + samples[count / 2]) / 2.0 : samples[count / 2];
}

template <typename Copy, typename Adopt>
[[nodiscard]] bool MeasureBuffers(const char* name, int width, int height, int iterations, Copy copy, Adopt adopt)
{
    std::vector<double> copyTimes;
    std::vector<double> adoptTimes;
    std::vector<double> savedTimes;
    std::vector<double> speedups;
    for (auto* samples : {&copyTimes, &adoptTimes, &savedTimes, &speedups})
    {
        samples->reserve(static_cast<std::size_t>(iterations));
    }
    std::uint64_t expected = 0;
    bool hasExpected = false;
    const auto timed = [&](auto invoke, double& milliseconds)
    {
        const auto begin = std::chrono::steady_clock::now();
        const Raster output = invoke();
        const auto end = std::chrono::steady_clock::now();
        milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();
        const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        if (!output.HasStorage() || output.Width() != width || output.Height() != height ||
            output.Format() != RasterFormat::Bgra8888 || output.ByteSize() != bytes || output.Data() == nullptr)
        {
            return false;
        }
        const std::uint64_t hash = HashBytes(output.Data(), bytes);
        if (hasExpected && hash != expected)
        {
            return false;
        }
        expected = hash;
        hasExpected = true;
        return true;
    };
    for (int run = -1; run < iterations; ++run)
    {
        double copyTime = 0;
        double adoptTime = 0;
        // Alternate order so warming the caches or a changing clock does not always favor the second variant.
        const bool ok = (run + 1) % 2 == 0 ? timed(copy, copyTime) && timed(adopt, adoptTime)
                                           : timed(adopt, adoptTime) && timed(copy, copyTime);
        if (!ok)
        {
            std::fprintf(stderr, "BUFFER BENCH ERROR name=%s width=%d height=%d run=%d output_mismatch\n", name, width,
                         height, run);
            return false;
        }
        if (run >= 0)
        {
            copyTimes.push_back(copyTime);
            adoptTimes.push_back(adoptTime);
            savedTimes.push_back(copyTime - adoptTime);
            speedups.push_back(adoptTime > 0 ? copyTime / adoptTime : 0);
        }
    }
    std::printf("BUFFER BENCH name=%s width=%d height=%d samples=%d warmup=1 copy_median_ms=%.3f "
                "adopt_median_ms=%.3f median_saved_ms=%.3f median_speedup=%.3f checksum=%016llx arch=%s config=%s\n",
                name, width, height, iterations, Median(copyTimes), Median(adoptTimes), Median(savedTimes),
                Median(speedups), static_cast<unsigned long long>(expected), kArch, kConfiguration);
    std::fflush(stdout);
    return true;
}

// A conservative copy reference: the old vector/result-copy strategy, using today's ReadRegion (which already
// skips the old redundant fill). This compares ownership strategies, not two independently compiled binaries.
[[nodiscard]] Raster CopyReference(FilterKind kind, const Raster& source, const LayerAdjustment& levels,
                                   const LayerAdjustment& hsv)
{
    std::vector<float> cube;
    if (kind == FilterKind::HueSaturation)
    {
        cube = Adjust::ColorCube(hsv.ResolvedHsv());
    }
    std::vector<std::uint8_t> pixels(source.ByteSize());
    source.ReadRegion(0, 0, source.Width(), source.Height(), pixels.data(), source.Stride(), 0);
    switch (kind)
    {
    case FilterKind::Levels:
        Adjust::ApplyTables(pixels.data(), source.Width(), source.Height(), source.Stride(),
                            Adjust::LevelsTables(levels.levels));
        break;
    case FilterKind::HueSaturation:
        Adjust::ApplyCube(pixels.data(), source.Width(), source.Height(), source.Stride(), cube);
        break;
    case FilterKind::AddNoise:
        Adjust::AddNoise(pixels.data(), source.Width(), source.Height(), source.Stride(), 12.0f, false, false,
                         0x12345678u);
        break;
    default:
        return {};
    }
    return Raster::Make(source.Width(), source.Height(), RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] bool RunBuffers(int width, int height, int iterations)
{
    if (width < 1 || height < 1 || width > 10000 || height > 10000 || iterations < 1 || iterations > 100 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 100000000ull)
    {
        std::fputs("BUFFER BENCH ERROR invalid_dimensions_or_iterations\n", stderr);
        return false;
    }
    const Raster source = MakeInput(width, height);
    if (!source.HasStorage() || source.Data() == nullptr)
    {
        return false;
    }
    LayerAdjustment levels;
    levels.kind = AdjustmentKind::Levels;
    Adjust::SetLevelsCurrent(levels.levels, LevelRange{18, 1.2, 238, 4, 250});
    LayerAdjustment hsv;
    hsv.kind = AdjustmentKind::Hsv;
    hsv.hsvSettings = Adjust::HsvSettings(23, 15, 4);
    FilterSettings noise;
    noise.amount = 12;
    noise.gaussian = false;
    noise.monochromatic = false;
    for (const auto& item :
         {std::pair{FilterKind::Levels, "levels"}, std::pair{FilterKind::HueSaturation, "hue_saturation"},
          std::pair{FilterKind::AddNoise, "uniform_noise"}})
    {
        if (!MeasureBuffers(
                item.second, width, height, iterations, [&] { return CopyReference(item.first, source, levels, hsv); },
                [&]
                {
                    return item.first == FilterKind::AddNoise
                               ? Adjust::RunFilter(item.first, source, noise, 1.0, 0x12345678u)
                               : Adjust::ApplyAdjustment(item.first == FilterKind::Levels ? levels : hsv, source);
                }))
        {
            return false;
        }
    }
    std::printf("BUFFER BENCH REPORT status=ok width=%d height=%d operations=3 arch=%s config=%s\n", width, height,
                kArch, kConfiguration);
    std::fflush(stdout);
    return true;
}
} // namespace

int RunKernelBenchmark(int width, int height, int iterations) noexcept
{
    // This developer command is a complete operation boundary: an allocation failure fails the benchmark.
    try
    {
        return Run(width, height, iterations) ? 0 : 1;
    }
    catch (const std::bad_alloc&)
    {
        std::fputs("KERNEL BENCH ERROR out_of_memory\n", stderr);
        return 1;
    }
}

int RunKernelBenchmark() noexcept
{
    return RunKernelBenchmark(3840, 2160, 10);
}

int RunBufferBenchmark(int width, int height, int iterations) noexcept
{
    try
    {
        return RunBuffers(width, height, iterations) ? 0 : 1;
    }
    catch (const std::bad_alloc&)
    {
        std::fputs("BUFFER BENCH ERROR out_of_memory\n", stderr);
        return 1;
    }
}

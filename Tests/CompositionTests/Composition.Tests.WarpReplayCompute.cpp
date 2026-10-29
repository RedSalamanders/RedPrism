#include "Composition.Tests.WarpReplayCompute.h"
#include <cstddef>

#include "BrushCompute.h"
#include "SharedPublicationMemory.h"
#include "WarpPreviewWork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
constexpr int kSide = 32;
constexpr BrushSettings kSettings{6, 1, 1, 0, 0, 1};
constexpr std::array<Vec2, 6> kPoints{{{8, 8}, {8, 8}, {24, 8}, {24, 24}, {8, 24}, {8, 8}}};
constexpr std::array<std::uint8_t, 4> kOriginal{8, 4, 12, 32};
constexpr std::array<std::array<std::uint8_t, 4>, 4> kSamples{
    {{}, {16, 8, 32, 64}, {32, 16, 64, 128}, {64, 32, 128, 255}}};
int gFailures = 0;
int gChecks = 0;
const char* gDriver = nullptr;

void Expect(bool condition, const char* name)
{
    ++gChecks;
    gFailures += condition ? 0 : 1;
    std::printf("%s warp-replay-compute.%s.%s\n", condition ? "PASS" : "FAIL", gDriver, name);
}

template <typename Work> void Joined(Work&& work)
{
    std::thread worker(std::forward<Work>(work));
    worker.join();
}

Raster FixtureRaster(bool sampled)
{
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto& pixel = sampled ? kSamples[static_cast<std::size_t>((x + y) % 4)] : kOriginal;
            std::copy(pixel.begin(), pixel.end(), bytes.begin() + (y * kSide + x) * 4);
        }
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

// Numeric compute reference only. This diagnostic intentionally uses the existing synchronous engine on
// its context owner; it does not qualify the production recorder, readback lifetime, admission or G1.
template <typename Coverage>
bool RenderReferenceRows(GpuBrushCoverage& gpu, const Coverage& coverage, std::vector<std::vector<std::uint32_t>>& rows,
                         bool final)
{
    auto tiles = coverage.tiles;
    std::vector<std::vector<std::uint8_t>> preview(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i)
    {
        preview[i].resize(static_cast<std::size_t>(tiles[i].width) * tiles[i].height);
        tiles[i].preview = preview[i].data(); // Never write the detached CPU draft from this context owner.
    }
    bool rendered = false;
    if constexpr (std::is_same_v<Coverage, BrushStroke::SampleCoverage>)
        rendered = gpu.Render(tiles, coverage.settled, coverage.tail, coverage.params);
    else
        rendered = final && gpu.Render(tiles, coverage.settled, {}, coverage.params);
    if (!rendered)
        return false;
    rows.resize(preview.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
        rows[i].assign(preview[i].begin(), preview[i].end());
    return true;
}

void RunFixture(D3D_DRIVER_TYPE driver, const char* label)
{
    gDriver = label;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    const auto created = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                           D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (FAILED(created) && driver == D3D_DRIVER_TYPE_HARDWARE)
    {
        std::printf("INFO warp-replay-compute.hardware.unavailable hr=%08lx\n", static_cast<unsigned long>(created));
        return;
    }
    GpuBrushCoverage gpu;
    const bool initialized = SUCCEEDED(created) && SUCCEEDED(gpu.Initialize(device.get(), immediate.get())) &&
                             gpu.Ready() && !gpu.UsesCpuPermanent();
    Expect(initialized, "actual-compute-engine-initializes");
    if (!initialized)
        return;
    gpu.BeginStroke();
    const auto owner = std::this_thread::get_id();
    const Raster sample = FixtureRaster(true);
    const Raster original = FixtureRaster(false);
    ImageLayer layer = ImageLayer::Blank(L"Compute replay", {kSide, kSide});
    layer.asset = RasterAsset{original, Raster::MakeThumbnail(original), layer.name};
    layer.transform.sampling = LayerSampling::Nearest;
    std::shared_ptr<const PreparedWarpPreview> preview;
    std::shared_ptr<BrushStroke> replay;
    Joined(
        [&]
        {
            const auto memory = SharedPublicationMemory::CreateForWork();
            auto warp = WarpStroke::MakePlacedForWork({sample, layer.transform, kSide, kSide}, SmearMode::Liquify,
                                                      kSettings, owner, {}, memory);
            if (warp)
                preview = PrepareWarpPreviewForWork(*warp, owner, {}, memory);
            replay = BrushStroke::MakeForWork(layer, false, kSettings, kSide, kSide);
            if (!replay || !replay->BeginWarpReplayForWork(preview, true))
                replay.reset();
        });
    Expect(preview && replay, "joined-worker-opens-private-compute-replay-from-distinct-literal-source");
    if (!replay)
        return;
    BrushStroke scalar(layer, false, kSettings, kSide, kSide, &CpuBrushCoverage::Shared());
    scalar.SetSample(sample, 0, 0, true);
    bool referenceGood = scalar.IsValid();
    for (const auto point : kPoints)
    {
        std::optional<BrushStroke::SampleCoverage> coverage;
        bool missingRefused = false;
        Joined(
            [&]
            {
                coverage = replay->PrepareWarpReplaySampleForWork(point);
                missingRefused = coverage && (!coverage->needsGpu || !replay->ComposeSampleCoverage(coverage.value()));
            });
        Expect(coverage && missingRefused, "sample-cannot-compose-before-required-compute-values");
        if (!coverage)
            break;
        std::vector<std::vector<std::uint32_t>> rows;
        const bool rendered = RenderReferenceRows(gpu, coverage.value(), rows, false);
        bool composed = false;
        Joined(
            [&]
            {
                bool installed = rendered;
                for (std::size_t i = 0; installed && i < coverage->tiles.size(); ++i)
                {
                    const auto key = coverage->tiles[i].key;
                    installed = !replay->InstallSampleCoverage(key, {}) &&
                                replay->InstallSampleCoverage(key, rows[i]) &&
                                !replay->InstallSampleCoverage(key, rows[i]);
                }
                composed = installed && replay->ComposeSampleCoverage(coverage.value());
            });
        Expect(rendered && composed, "exact-uint-rows-compose-on-worker-short-and-duplicate-delivery-refuse");
        referenceGood = scalar.Append(point) && referenceGood;
    }
    std::optional<BrushStroke::FinalCoverage> final;
    bool finalMissingRefused = false;
    Joined(
        [&]
        {
            final = replay->PrepareWarpReplayFinishForWork();
            finalMissingRefused =
                final && final->needsGpu && !replay->ComposeFinalCoverage(final.value()) && !replay->PaintSnapshot();
        });
    Expect(finalMissingRefused, "final-and-snapshot-refuse-before-final-compute-values");
    std::optional<BrushStroke::Commit> committed;
    if (final)
    {
        std::vector<std::vector<std::uint32_t>> rows;
        const bool rendered = RenderReferenceRows(gpu, final.value(), rows, true);
        Joined(
            [&]
            {
                bool installed = rendered;
                for (std::size_t i = 0; installed && i < final->tiles.size(); ++i)
                    installed = replay->InstallFinalCoverage(final->tiles[i].key, rows[i]);
                if (installed && replay->ComposeFinalCoverage(final.value()))
                    committed = replay->PaintSnapshot();
            });
    }
    referenceGood = scalar.Flush() && referenceGood;
    const auto reference = referenceGood ? scalar.PaintSnapshot() : std::nullopt;
    int worst = 256;
    std::array<std::uint8_t, kSide * kSide * 4> actual{};
    if (committed && reference && committed->bounds == reference->bounds &&
        committed->transform == reference->transform && committed->bounds == GridRect{0, 0, kSide, kSide})
    {
        std::array<std::uint8_t, kSide * kSide * 4> expected{};
        committed->image.ReadRegion(0, 0, kSide, kSide, actual.data(), kSide * 4, 0);
        reference->image.ReadRegion(0, 0, kSide, kSide, expected.data(), kSide * 4, 0);
        worst = 0;
        for (std::size_t i = 0; i < actual.size(); ++i)
            worst = (std::max)(worst, std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])));
    }
    if (worst > 2)
        std::printf("INFO warp-replay-compute.%s.maximum-byte-error=%d\n", gDriver, worst);
    Expect(worst <= 2, "curved-duplicate-flush-pixels-match-scalar-with-existing-two-byte-coverage-tolerance");
    const auto pixelAt = [&](int x, int y, const std::array<std::uint8_t, 4>& expected)
    {
        const auto offset = static_cast<std::size_t>((y * kSide + x) * 4);
        return std::equal(expected.begin(), expected.end(), actual.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    Expect(committed && pixelAt(8, 8, {}) && pixelAt(31, 31, kOriginal),
           "transparent-tip-clears-and-far-original-retains-independent-literal-bytes");
    gpu.EndStroke();
    Expect(gpu.TileCount() == 0, "reference-compute-density-retires-after-all-joined-readers");
    Joined(
        [&]
        {
            committed.reset();
            replay.reset();
            preview.reset();
        });
}
} // namespace

int RunWarpReplayComputeTests()
{
    gFailures = 0;
    gChecks = 0;
    RunFixture(D3D_DRIVER_TYPE_WARP, "warp");
    RunFixture(D3D_DRIVER_TYPE_HARDWARE, "hardware");
    std::printf("SUMMARY warp-replay-compute checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

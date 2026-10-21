#include "Composition.Tests.BrushEpoch.h"

#include "BrushCompute.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <dxgi1_2.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;
constexpr int kTileWidth = 4;
constexpr int kTileHeight = 4;
constexpr std::size_t kPixelCount = static_cast<std::size_t>(kTileWidth) * kTileHeight;
constexpr std::uint64_t kTilePayloadLowerBound = kPixelCount * 8 + 4 * 4096;
constexpr std::uint64_t kStagingPayloadLowerBound = kPixelCount * sizeof(std::uint32_t) + 4096;
constexpr std::uint64_t kSegmentPayloadLowerBound = 2 * 4096 + 64 * 4 * sizeof(float);
constexpr std::uint64_t kCpuDirectoryLowerBound = 64 * 1024;
constexpr std::uint64_t kStrokeKey = 0xE90C4A51u;
constexpr std::uint8_t kPreviewSentinel = 0xA5;

int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s brush-epoch.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectCase(bool condition, const char* scenario, const char* control) noexcept
{
    std::printf("%s brush-epoch.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
    gFailures += condition ? 0 : 1;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

struct BrushFixture final
{
    std::array<std::uint8_t, kPixelCount> gpuPreview{};
    std::array<std::uint8_t, kPixelCount> cpuPreview{};
    std::array<float, kPixelCount> cpuDensity{};
    std::array<BrushKernelTile, 1> gpuTiles{};
    std::array<BrushKernelTile, 1> cpuTiles{};
    std::array<BrushSegment, 1> dab{};
    BrushKernelParams params{};

    BrushFixture() noexcept
    {
        gpuTiles[0] = {kStrokeKey, 0, 0, kTileWidth, kTileHeight, nullptr, gpuPreview.data()};
        cpuTiles[0] = {kStrokeKey, 0, 0, kTileWidth, kTileHeight, cpuDensity.data(), cpuPreview.data()};
        dab[0] = {Vec2{2.0, 2.0}, Vec2{2.0, 2.0}};
        params.radius = 1000.0;
        params.hardness = 1.0;
        params.canvasWidth = kTileWidth;
        params.canvasHeight = kTileHeight;
        params.antialiasWidth = 1.0;
        params.spacing = 1.0;
    }
};

bool MatchesCpuWithinLegacyTolerance(const BrushFixture& fixture) noexcept
{
    bool positive = false;
    for (std::size_t i = 0; i < kPixelCount; ++i)
    {
        positive |= fixture.gpuPreview[i] != 0;
        const int difference =
            std::abs(static_cast<int>(fixture.gpuPreview[i]) - static_cast<int>(fixture.cpuPreview[i]));
        if (difference > 2)
            return false;
    }
    return positive;
}

using EpochCase = void (*)(GpuBrushCoverage&, GpuBrushCoverage::StrokeEpoch&, void*) noexcept;

// The worker owns the epoch object for its complete lifetime. Owner-side inspection and Take are bracketed
// between ready and reset events; after the explicit worker reset acknowledgement, the directory is unusable.
bool WithWorkerEpoch(Device& device, GpuBrushCoverage& engine, const char* scenario, EpochCase runCase, void* state)
{
    wil::unique_handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle reset{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle acknowledged{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle finish{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!ready || !reset || !acknowledged || !finish)
    {
        ExpectCase(false, scenario, "worker-events-created");
        return false;
    }

    std::atomic<GpuBrushCoverage::StrokeEpoch*> published{};
    std::atomic<DWORD> resetThread{};
    const DWORD ownerThread = GetCurrentThreadId();
    std::thread worker(
        [&]() noexcept
        {
            GpuBrushCoverage::StrokeEpoch epoch;
            published.store(&epoch, std::memory_order_release);
            SetEvent(ready.get());
            WaitForSingleObject(reset.get(), INFINITE);
            epoch.ResetForWork();
            resetThread.store(GetCurrentThreadId(), std::memory_order_release);
            SetEvent(acknowledged.get());
            WaitForSingleObject(finish.get(), INFINITE);
        });

    const bool readyObserved = WaitForSingleObject(ready.get(), kFailureTimeoutMs) == WAIT_OBJECT_0;
    auto* epoch = published.load(std::memory_order_acquire);
    ExpectCase(readyObserved && epoch != nullptr, scenario, "worker-constructed-epoch-published");
    if (readyObserved && epoch != nullptr)
        runCase(engine, *epoch, state);

    // The submitted density commands must finish before the worker releases
    // the detached resource graph. This event barrier never polls or recopies.
    wil::unique_handle gpuDone{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::com_ptr_nothrow<IDXGIDevice2> dxgi;
    const bool gpuAcknowledged = gpuDone && SUCCEEDED(device.device->QueryInterface(IID_PPV_ARGS(dxgi.put()))) &&
                                 SUCCEEDED(dxgi->EnqueueSetEvent(gpuDone.get())) &&
                                 WaitForSingleObject(gpuDone.get(), kFailureTimeoutMs) == WAIT_OBJECT_0;
    ExpectCase(gpuAcknowledged, scenario, "gpu-event-acknowledged-before-worker-retirement");
    if (!gpuAcknowledged)
    {
        std::fflush(stdout);
        std::_Exit(1);
    }

    if (!SetEvent(reset.get()))
        std::_Exit(1);
    const bool resetObserved = WaitForSingleObject(acknowledged.get(), kFailureTimeoutMs) == WAIT_OBJECT_0;
    if (!resetObserved)
    {
        std::printf("FAIL brush-epoch.%s.worker-reset-ack-timeout\n", scenario);
        std::fflush(stdout);
        std::_Exit(1); // The worker still owns the epoch and its resource references.
    }

    const DWORD workerThread = resetThread.load(std::memory_order_acquire);
    ExpectCase(workerThread != 0 && workerThread != ownerThread, scenario, "reset-ran-off-owner-after-ack");
    if (epoch != nullptr)
    {
        ExpectCase(epoch->TileCount() == 0 && epoch->CpuBytes() == 0 && epoch->GpuBytes() == 0 &&
                       !epoch->HasResources(),
                   scenario, "acknowledged-reset-releases-epoch-resources");
        const auto cpuBeforeRejectedTake = engine.StrokeCpuOwnershipBytes();
        const auto gpuBeforeRejectedTake = engine.StrokeGpuOwnershipBytes();
        ExpectCase(!engine.TakeStrokeEpoch(*epoch) && engine.StrokeCpuOwnershipBytes() == cpuBeforeRejectedTake &&
                       engine.StrokeGpuOwnershipBytes() == gpuBeforeRejectedTake,
                   scenario, "reset-directory-cannot-be-reused-or-disturb-engine");
    }

    if (!SetEvent(finish.get()))
        std::_Exit(1);
    worker.join();
    return readyObserved && epoch != nullptr;
}

struct CaptureState final
{
    BrushFixture* fixture = nullptr;
    std::uint64_t stagedGpuBytes = 0;
    std::uint64_t deferredGpuBytes = 0;
};

void TakeStagedAndRefuseSecondTake(GpuBrushCoverage& engine, GpuBrushCoverage::StrokeEpoch& epoch,
                                   void* opaque) noexcept
{
    auto& state = *static_cast<CaptureState*>(opaque);
    auto& fixture = *state.fixture;
    const auto ownerGpuBytes = engine.StrokeGpuOwnershipBytes();
    const auto ownerCpuBytes = engine.StrokeCpuOwnershipBytes();
    auto* const coverageBefore = engine.CoverageBuffer(kStrokeKey, kTileWidth, kTileHeight);
    ExpectCase(engine.CanTakeStrokeEpoch(epoch) && engine.TileCount() == 1 && epoch.TileCount() == 0 &&
                   !epoch.HasResources() && epoch.CpuBytes() == 0 && epoch.GpuBytes() == 0 &&
                   engine.StrokeCpuOwnershipBytes() == ownerCpuBytes &&
                   engine.StrokeGpuOwnershipBytes() == ownerGpuBytes &&
                   engine.CoverageBuffer(kStrokeKey, kTileWidth, kTileHeight) == coverageBefore,
               "staged", "preflight-preserves-original-density-and-empty-destination");
    const bool took = engine.TakeStrokeEpoch(epoch);
    ExpectCase(took, "staged", "populated-epoch-taken-on-owner");
    ExpectCase(took && epoch.TileCount() == 1 &&
                   epoch.CpuBytes() >= sizeof(GpuBrushCoverage::StrokeEpoch) + kCpuDirectoryLowerBound &&
                   epoch.GpuBytes() == ownerGpuBytes &&
                   epoch.GpuBytes() >= kTilePayloadLowerBound + kStagingPayloadLowerBound + kSegmentPayloadLowerBound &&
                   epoch.CpuBytes() >= ownerCpuBytes,
               "staged", "tile-count-and-literal-tile-segment-staging-charge-lower-bounds");
    ExpectCase(took && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() == 0 &&
                   engine.CoverageBuffer(kStrokeKey, kTileWidth, kTileHeight) == nullptr,
               "staged", "take-leaves-owner-engine-empty");
    if (!took)
        return;

    fixture.gpuPreview.fill(kPreviewSentinel);
    engine.BeginStroke();
    const bool emptyDispatch = engine.Render(fixture.gpuTiles, {}, {}, fixture.params);
    const bool literalZero = std::all_of(fixture.gpuPreview.begin(), fixture.gpuPreview.end(),
                                         [](std::uint8_t value) noexcept { return value == 0; });
    ExpectCase(emptyDispatch && literalZero, "staged", "new-stroke-empty-tile-is-literal-zero");

    // Drop this stroke's staging tile on the owner, retaining only the segment buffers; the following
    // deferred dispatch creates the same tile without a staging resource.
    engine.EndStroke();
    ExpectCase(engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() >= kSegmentPayloadLowerBound, "staged",
               "endstroke-keeps-only-segment-payload");
    engine.BeginStroke();
    const bool deferredRecorded = engine.RecordCoverage(fixture.gpuTiles, fixture.dab, {}, fixture.params);
    state.deferredGpuBytes = engine.StrokeGpuOwnershipBytes();
    ExpectCase(deferredRecorded && engine.TileCount() == 1 &&
                   state.deferredGpuBytes >= kTilePayloadLowerBound + kSegmentPayloadLowerBound,
               "staged", "deferred-record-creates-tile-without-staging-payload");
    ExpectCase(ownerGpuBytes >= state.deferredGpuBytes + kStagingPayloadLowerBound, "staged",
               "staged-charge-includes-literal-optional-staging-payload");
    const auto deferredCpuBytes = engine.StrokeCpuOwnershipBytes();
    const auto epochCpuBytes = epoch.CpuBytes();
    const auto epochGpuBytes = epoch.GpuBytes();
    ExpectCase(!engine.CanTakeStrokeEpoch(epoch) && engine.TileCount() == 1 && epoch.TileCount() == 1 &&
                   engine.StrokeCpuOwnershipBytes() == deferredCpuBytes &&
                   engine.StrokeGpuOwnershipBytes() == state.deferredGpuBytes && epoch.CpuBytes() == epochCpuBytes &&
                   epoch.GpuBytes() == epochGpuBytes,
               "staged", "occupied-preflight-refuses-with-both-graphs-unchanged");
    const bool secondTake = engine.TakeStrokeEpoch(epoch);
    ExpectCase(!secondTake && engine.StrokeCpuOwnershipBytes() == deferredCpuBytes &&
                   engine.StrokeGpuOwnershipBytes() == state.deferredGpuBytes && engine.TileCount() == 1 &&
                   engine.CoverageBuffer(kStrokeKey, kTileWidth, kTileHeight) != nullptr &&
                   epoch.CpuBytes() == epochCpuBytes && epoch.GpuBytes() == epochGpuBytes && epoch.TileCount() == 1,
               "staged", "populated-target-refuses-second-take-and-preserves-both-graphs");
    state.stagedGpuBytes = ownerGpuBytes;
}

void TakeDeferredNonStaging(GpuBrushCoverage& engine, GpuBrushCoverage::StrokeEpoch& epoch, void* opaque) noexcept
{
    auto& state = *static_cast<CaptureState*>(opaque);
    const auto ownerGpuBytes = engine.StrokeGpuOwnershipBytes();
    const auto ownerCpuBytes = engine.StrokeCpuOwnershipBytes();
    const bool took = engine.TakeStrokeEpoch(epoch);
    ExpectCase(took && epoch.TileCount() == 1 &&
                   epoch.CpuBytes() >= sizeof(GpuBrushCoverage::StrokeEpoch) + kCpuDirectoryLowerBound &&
                   epoch.GpuBytes() == ownerGpuBytes &&
                   epoch.GpuBytes() >= kTilePayloadLowerBound + kSegmentPayloadLowerBound &&
                   epoch.GpuBytes() + kStagingPayloadLowerBound <= state.stagedGpuBytes &&
                   epoch.CpuBytes() >= ownerCpuBytes,
               "deferred", "tile-count-and-literal-nonstaging-payload-charge");
    ExpectCase(took && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() == 0 &&
                   engine.CoverageBuffer(kStrokeKey, kTileWidth, kTileHeight) == nullptr,
               "deferred", "take-clears-owner-graph-after-deferred-record");
    state.deferredGpuBytes = epoch.GpuBytes();
}

void TakeSegmentOnly(GpuBrushCoverage& engine, GpuBrushCoverage::StrokeEpoch& epoch, void*) noexcept
{
    const auto ownerGpuBytes = engine.StrokeGpuOwnershipBytes();
    const auto ownerCpuBytes = engine.StrokeCpuOwnershipBytes();
    const bool took = engine.TakeStrokeEpoch(epoch);
    ExpectCase(took && epoch.TileCount() == 0 && epoch.HasResources() &&
                   epoch.CpuBytes() >= sizeof(GpuBrushCoverage::StrokeEpoch) + kCpuDirectoryLowerBound &&
                   epoch.CpuBytes() >= ownerCpuBytes && epoch.GpuBytes() == ownerGpuBytes &&
                   epoch.GpuBytes() >= kSegmentPayloadLowerBound,
               "segments-only", "segment-buffer-and-view-transferred-without-tile");
    ExpectCase(took && engine.StrokeGpuOwnershipBytes() == 0, "segments-only", "owner-segment-charge-cleared");
}

void TakeEmpty(GpuBrushCoverage& engine, GpuBrushCoverage::StrokeEpoch& epoch, void* opaque) noexcept
{
    const char* scenario = static_cast<const char*>(opaque);
    const auto ownerCpuBytes = engine.StrokeCpuOwnershipBytes();
    const bool took = engine.TakeStrokeEpoch(epoch);
    ExpectCase(took && epoch.TileCount() == 0 && !epoch.HasResources() && epoch.GpuBytes() == 0 &&
                   epoch.CpuBytes() >= sizeof(GpuBrushCoverage::StrokeEpoch) + kCpuDirectoryLowerBound &&
                   epoch.CpuBytes() >= ownerCpuBytes,
               scenario, "empty-epoch-directory-transfer-has-cpu-bound-and-zero-gpu-payload");
    ExpectCase(took && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() == 0, scenario,
               "empty-engine-remains-empty");
}

bool PrepareStagedTile(GpuBrushCoverage& engine, BrushFixture& fixture) noexcept
{
    fixture.gpuPreview.fill(kPreviewSentinel);
    fixture.cpuPreview.fill(0);
    fixture.cpuDensity.fill(0.0f);
    engine.BeginStroke();
    const bool rendered = engine.Render(fixture.gpuTiles, fixture.dab, {}, fixture.params);
    BrushKernel::RenderTile(fixture.cpuTiles[0], fixture.dab, {}, fixture.params);
    const bool matches = MatchesCpuWithinLegacyTolerance(fixture);
    Expect(rendered && matches, "setup.staged-real-tile-matches-independent-cpu-within-2-of-255");
    return rendered && matches;
}

bool PrepareSegmentOnly(GpuBrushCoverage& engine, BrushFixture& fixture) noexcept
{
    fixture.gpuPreview.fill(kPreviewSentinel);
    engine.BeginStroke();
    const bool rendered = engine.Render(fixture.gpuTiles, fixture.dab, {}, fixture.params);
    engine.EndStroke();
    Expect(rendered && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() >= kSegmentPayloadLowerBound,
           "setup.endstroke-leaves-segment-only-payload");
    return rendered && engine.TileCount() == 0;
}
} // namespace

int RunBrushEpochTests()
{
    gFailures = 0;
    Device device;
    const HRESULT deviceResult = device.Create();
    Expect(SUCCEEDED(deviceResult), "setup.warp-device");
    if (FAILED(deviceResult))
        return gFailures;

    GpuBrushCoverage engine;
    const HRESULT engineResult = engine.Initialize(device.device.get(), device.context.get());
    Expect(SUCCEEDED(engineResult), "setup.compute-engine");
    if (FAILED(engineResult))
        return gFailures;

    BrushFixture fixture;
    CaptureState state{&fixture};
    PrepareStagedTile(engine, fixture);
    WithWorkerEpoch(device, engine, "staged", TakeStagedAndRefuseSecondTake, &state);
    WithWorkerEpoch(device, engine, "deferred", TakeDeferredNonStaging, &state);

    PrepareSegmentOnly(engine, fixture);
    WithWorkerEpoch(device, engine, "segments-only", TakeSegmentOnly, nullptr);
    char emptyFirst[] = "empty-first";
    char emptyRepeat[] = "empty-repeat";
    WithWorkerEpoch(device, engine, "empty-first", TakeEmpty, emptyFirst);
    WithWorkerEpoch(device, engine, "empty-repeat", TakeEmpty, emptyRepeat);

    engine.Release();
    return gFailures;
}

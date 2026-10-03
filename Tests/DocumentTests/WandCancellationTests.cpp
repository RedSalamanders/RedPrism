#include "WandCancellationTests.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s wand-cancel.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

Raster SolidBgra(int width, int height, std::array<std::uint8_t, 4> pixel)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4u)
    {
        std::copy(pixel.begin(), pixel.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return Bgra(width, height, bytes);
}

Raster SolidGray(int width, int height, std::uint8_t value)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
    return Gray(width, height, bytes);
}

WandSettings Settings(int tolerance = 0, bool contiguous = true, WandSampleSize sample = WandSampleSize::Point)
{
    WandSettings settings;
    settings.tolerance = tolerance;
    settings.contiguous = contiguous;
    settings.sampleSize = sample;
    return settings;
}

struct CancellationProbe final
{
    CancelSource* source = nullptr;
    MagicWand::WorkStage targetStage{};
    std::size_t targetOffset = 0;
    int hits = 0;
};

void CancelAt(void* opaque, MagicWand::WorkStage stage, std::size_t offset) noexcept
{
    auto& probe = *static_cast<CancellationProbe*>(opaque);
    if (stage == probe.targetStage && offset == probe.targetOffset)
    {
        ++probe.hits;
        static_cast<void>(probe.source->Cancel());
    }
}

MagicWand::WorkObserver Observer(CancellationProbe& probe) noexcept
{
    return MagicWand::WorkObserver{&probe, &CancelAt};
}

bool SameLiteralMask(const std::vector<std::uint8_t>& actual, std::span<const std::uint8_t> expected, long count,
                     long expectedCount) noexcept
{
    return actual.size() == expected.size() && std::equal(expected.begin(), expected.end(), actual.begin()) &&
           count == expectedCount;
}

bool IsCancelledFailure(WandFailure failure) noexcept
{
    return failure == WandFailure::Cancelled;
}

bool HasLiteralLoop(const std::vector<SelectionLoop>& loops, const SelectionLoop& expected) noexcept
{
    return std::any_of(loops.begin(), loops.end(),
                       [&](const SelectionLoop& actual)
                       {
                           if (actual.size() != expected.size())
                           {
                               return false;
                           }
                           for (std::size_t start = 0; start < actual.size(); ++start)
                           {
                               bool matches = true;
                               for (std::size_t vertex = 0; vertex < expected.size(); ++vertex)
                               {
                                   matches = matches && actual[(start + vertex) % actual.size()] == expected[vertex];
                               }
                               if (matches)
                               {
                                   return true;
                               }
                           }
                           return false;
                       });
}

void CheckMaskCancellation(MagicWand::WorkStage stage, std::size_t offset, const Raster& image,
                           const char* checkpointName, const char* resultName)
{
    CancelSource source;
    CancellationProbe probe{&source, stage, offset, 0};
    long count = 123;
    const auto result = MagicWand::Mask(image, 0, 0, Settings(0, false), count, source.Token(), Observer(probe));
    Expect(probe.hits == 1 && source.Token().IsCancelled(), checkpointName);
    Expect(result.empty() && count == MagicWand::kCancelledMaskCount, resultName);
}

void CheckSelectCancellation(MagicWand::WorkStage stage, std::size_t offset, const Raster& image, WandSettings settings,
                             const char* checkpointName, const char* resultName)
{
    CancelSource source;
    CancellationProbe probe{&source, stage, offset, 0};
    WandFailure failure = WandFailure::None;
    const auto result = MagicWand::Select(image, Vec2{0.5, 0.5}, settings, &failure, source.Token(), Observer(probe));
    Expect(probe.hits == 1 && source.Token().IsCancelled(), checkpointName);
    Expect(!result.has_value() && IsCancelledFailure(failure), resultName);
}

void CheckTraceCancellation(MagicWand::WorkStage stage, std::size_t offset, const std::vector<std::uint8_t>& mask,
                            int width, int height, const char* checkpointName, const char* resultName)
{
    CancelSource source;
    CancellationProbe probe{&source, stage, offset, 0};
    WandFailure failure = WandFailure::None;
    const auto result = MagicWand::Trace(mask, width, height, &failure, source.Token(), Observer(probe));
    Expect(probe.hits == 1 && source.Token().IsCancelled(), checkpointName);
    Expect(!result.has_value() && IsCancelledFailure(failure), resultName);
}

void PreCancelledTokens()
{
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 30, 255, 0, 0, 30, 255, 0, 0, 30, 255, 0, 0, 30, 255};
    constexpr std::array<std::uint8_t, 4> maskPixels{0, 255, 127, 255};
    const Raster image = Bgra(2, 2, pixels);
    const Raster gray = Gray(2, 2, maskPixels);
    CancelSource local;
    static_cast<void>(local.Cancel());
    std::stop_source shutdown;
    static_cast<void>(shutdown.request_stop());
    const CancelToken shutdownOnly{{}, shutdown.get_token()};
    const CancelToken localAndShutdown{local.Token().LocalToken(), shutdown.get_token()};

    long count = 9;
    const auto mask = MagicWand::Mask(image, 0, 0, Settings(), count, local.Token());
    Expect(mask.empty() && count == MagicWand::kCancelledMaskCount, "pre-cancel.local-mask-is-empty-with-cancel-count");

    count = 9;
    const auto shutdownMask = MagicWand::Mask(image, 0, 0, Settings(), count, localAndShutdown);
    Expect(shutdownMask.empty() && count == MagicWand::kCancelledMaskCount,
           "pre-cancel.local-and-shutdown-mask-is-empty-with-cancel-count");

    count = 9;
    const auto shutdownOnlyMask = MagicWand::Mask(image, 0, 0, Settings(), count, shutdownOnly);
    Expect(shutdownOnlyMask.empty() && count == MagicWand::kCancelledMaskCount,
           "pre-cancel.shutdown-mask-is-empty-with-cancel-count");

    WandFailure failure = WandFailure::None;
    const auto selected = MagicWand::Select(image, Vec2{0.5, 0.5}, Settings(), &failure, local.Token());
    Expect(!selected && failure == WandFailure::Cancelled, "pre-cancel.select-reports-cancelled");

    failure = WandFailure::None;
    const auto traced =
        MagicWand::Trace(std::vector<std::uint8_t>{255, 255, 255, 255}, 2, 2, &failure, localAndShutdown);
    Expect(!traced && failure == WandFailure::Cancelled, "pre-cancel.trace-local-and-shutdown");

    failure = WandFailure::None;
    const auto dark = MaskTracing::DarkPixels(gray, &failure, local.Token());
    Expect(!dark && failure == WandFailure::Cancelled, "pre-cancel.mask-tracing-reports-cancelled");

    failure = WandFailure::None;
    const auto opaque = MaskTracing::OpaquePixels(image, &failure, shutdownOnly);
    Expect(!opaque && failure == WandFailure::Cancelled, "pre-cancel.opaque-tracing-shutdown");

    LayerTransform transform;
    transform.origin = Vec2{1, 1};
    transform.size = Vec2{2, 2};
    transform.sampling = LayerSampling::Nearest;
    Expect(!RenderLayerPixels(image, transform, 4, 4, local.Token()).HasStorage(), "pre-cancel.render-layer-is-empty");
}

struct OwnerStopGate final
{
    std::mutex mutex;
    std::condition_variable event;
    bool entered = false;
    bool released = false;
};

void EnterOwnerStopGate(void* opaque, MagicWand::WorkStage stage, std::size_t offset) noexcept
{
    if (stage != MagicWand::WorkStage::MatchScan || offset != 4096)
        return;
    auto& gate = *static_cast<OwnerStopGate*>(opaque);
    std::unique_lock lock(gate.mutex);
    gate.entered = true;
    gate.event.notify_all();
    gate.event.wait(lock, [&] { return gate.released; });
}

void OwnerCancelsReachedWorker()
{
    const Raster image = SolidBgra(128, 64, {17, 19, 23, 255});
    const void* const identity = image.Identity();
    for (const bool shutdown : {false, true})
    {
        CancelSource local;
        std::stop_source stop;
        OwnerStopGate gate;
        long count = 0;
        std::vector<std::uint8_t> output;
        const CancelToken token{local.Token().LocalToken(), stop.get_token()};
        std::jthread worker(
            [&]
            { output = MagicWand::Mask(image, 0, 0, Settings(0, false), count, token, {&gate, EnterOwnerStopGate}); });
        bool entered = false;
        {
            std::unique_lock lock(gate.mutex);
            entered = gate.event.wait_for(lock, std::chrono::seconds(5), [&] { return gate.entered; });
            if (shutdown)
                static_cast<void>(stop.request_stop());
            else
                static_cast<void>(local.Cancel());
            gate.released = true;
        }
        gate.event.notify_all();
        worker.join();
        Expect(entered, shutdown ? "worker.shutdown-reached-partial-match-before-owner-stop"
                                 : "worker.local-reached-partial-match-before-owner-stop");
        Expect(output.empty() && count == MagicWand::kCancelledMaskCount,
               shutdown ? "worker.shutdown-discards-partial-mask-after-acknowledged-return"
                        : "worker.local-discards-partial-mask-after-acknowledged-return");
        const CancelSource fresh;
        count = 0;
        output = MagicWand::Mask(image, 0, 0, Settings(0, false), count, fresh.Token());
        Expect(count == 8192 && output.size() == 8192 &&
                   std::all_of(output.begin(), output.end(), [](auto value) { return value == 255; }) &&
                   image.Identity() == identity,
               shutdown ? "worker.shutdown-fresh-token-literal-recovery-preserves-source"
                        : "worker.local-fresh-token-literal-recovery-preserves-source");
    }
}

void DeterministicCheckpointCancellation()
{
    const Raster smallSolid = SolidBgra(2, 2, {0, 0, 40, 255});
    CheckMaskCancellation(MagicWand::WorkStage::Source, 0, smallSolid, "checkpoint.mask-source-0-reached",
                          "checkpoint.mask-source-0-discards-and-sets-cancel-count");

    const Raster initializeGrid = SolidBgra(128, 64, {7, 9, 11, 255});
    CheckMaskCancellation(MagicWand::WorkStage::MaskInitialize, 4096, initializeGrid,
                          "checkpoint.mask-initialize-4096-reached",
                          "checkpoint.mask-initialize-4096-discards-and-sets-cancel-count");

    CheckSelectCancellation(MagicWand::WorkStage::Sample, 0, smallSolid, Settings(),
                            "checkpoint.select-sample-0-reached", "checkpoint.select-sample-0-reports-cancelled");

    const Raster noncontiguous = SolidBgra(128, 64, {17, 19, 23, 255});
    CheckSelectCancellation(MagicWand::WorkStage::MatchScan, 4096, noncontiguous, Settings(0, false),
                            "checkpoint.match-scan-4096-reached", "checkpoint.match-scan-4096-reports-cancelled");

    CheckSelectCancellation(MagicWand::WorkStage::FloodSeeds, 0, noncontiguous, Settings(),
                            "checkpoint.flood-seeds-0-reached", "checkpoint.flood-seeds-0-reports-cancelled");

    const Raster longSolid = SolidBgra(8192, 1, {31, 37, 41, 255});
    CheckSelectCancellation(MagicWand::WorkStage::FloodRun, 4096, longSolid, Settings(),
                            "checkpoint.flood-run-4096-reached", "checkpoint.flood-run-4096-reports-cancelled");

    CheckSelectCancellation(MagicWand::WorkStage::FloodNeighbors, 4096, noncontiguous, Settings(),
                            "checkpoint.flood-neighbors-4096-reached",
                            "checkpoint.flood-neighbors-4096-reports-cancelled");

    std::vector<std::uint8_t> solidMask(128u * 64u, 255);
    CheckTraceCancellation(MagicWand::WorkStage::TraceInitialize, 4096, solidMask, 128, 64,
                           "checkpoint.trace-initialize-4096-reached",
                           "checkpoint.trace-initialize-4096-reports-cancelled");

    std::vector<std::uint8_t> checker(128u * 64u);
    for (std::size_t index = 0; index < checker.size(); ++index)
    {
        checker[index] = (index % 2u) == 0u ? 255 : 0;
    }
    CheckTraceCancellation(MagicWand::WorkStage::TraceEdges, 4096, checker, 128, 64,
                           "checkpoint.trace-edges-4096-reached", "checkpoint.trace-edges-4096-reports-cancelled");

    CheckTraceCancellation(MagicWand::WorkStage::TraceStarts, 4096, solidMask, 128, 64,
                           "checkpoint.trace-starts-4096-reached", "checkpoint.trace-starts-4096-reports-cancelled");

    std::vector<std::uint8_t> walkMask(3072u * 2u, 255);
    CheckTraceCancellation(MagicWand::WorkStage::TraceWalk, 4096, walkMask, 3072, 2,
                           "checkpoint.trace-walk-4096-reached", "checkpoint.trace-walk-4096-reports-cancelled");

    CheckTraceCancellation(MagicWand::WorkStage::Complete, 0, solidMask, 128, 64, "checkpoint.trace-complete-0-reached",
                           "checkpoint.trace-complete-0-reports-cancelled");
}

void LiteralMaskGoldens()
{
    constexpr std::array<std::uint8_t, 16> channels{100, 100, 100, 255, 132, 100, 100, 255,
                                                    133, 100, 100, 255, 100, 100, 100, 222};
    constexpr std::array<std::uint8_t, 4> channelExpected{255, 255, 0, 0};
    long count = -1;
    const auto channelMask = MagicWand::Mask(Bgra(4, 1, channels), 0, 0, Settings(32, false), count);
    Expect(SameLiteralMask(channelMask, channelExpected, count, 2),
           "golden.mask-per-channel-tolerance-and-alpha-literal");

    constexpr std::array<std::uint8_t, 100> samplePixels{
        0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0,   255, 0,   0,
        0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 255, 255, 255, 255,
        0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0,   255, 0,   0,
        0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255, 0, 0,   0, 255};
    constexpr std::array<std::uint8_t, 25> averageExpected{255, 255, 255, 255, 255, 255, 255, 255, 255,
                                                           255, 255, 255, 0,   255, 255, 255, 255, 255,
                                                           255, 255, 255, 255, 255, 255, 255};
    count = -1;
    const auto averageMask =
        MagicWand::Mask(Bgra(5, 5, samplePixels), 2, 2, Settings(30, false, WandSampleSize::ThreeByThree), count);
    Expect(SameLiteralMask(averageMask, averageExpected, count, 24), "golden.mask-3x3-average-excludes-center-literal");

    std::array<std::uint8_t, 160> stripes{};
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 10; ++x)
        {
            const bool red = x < 3 || x >= 6;
            const std::size_t offset = static_cast<std::size_t>(y * 10 + x) * 4u;
            stripes[offset] = red ? 0 : 255;
            stripes[offset + 2u] = red ? 255 : 0;
            stripes[offset + 3u] = 255;
        }
    }
    constexpr std::array<std::uint8_t, 40> contiguousExpected{
        255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0,
        255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 40> noncontiguousExpected{
        255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255,
        255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255};
    const Raster stripeImage = Bgra(10, 4, stripes);
    long contiguousCount = -1;
    const auto contiguousMask = MagicWand::Mask(stripeImage, 1, 0, Settings(0, true), contiguousCount);
    Expect(SameLiteralMask(contiguousMask, contiguousExpected, contiguousCount, 12),
           "golden.mask-contiguous-components-literal");
    long noncontiguousCount = -1;
    const auto noncontiguousMask = MagicWand::Mask(stripeImage, 1, 0, Settings(0, false), noncontiguousCount);
    Expect(SameLiteralMask(noncontiguousMask, noncontiguousExpected, noncontiguousCount, 28),
           "golden.mask-noncontiguous-components-literal");
}

void TraceLiteralGoldens()
{
    constexpr std::array<std::uint8_t, 9> donut{255, 255, 255, 255, 0, 255, 255, 255, 255};
    const auto traced = MagicWand::Trace(std::vector<std::uint8_t>(donut.begin(), donut.end()), 3, 3);
    const SelectionLoop expectedOuter{Vec2{0, 0}, Vec2{3, 0}, Vec2{3, 3}, Vec2{0, 3}};
    const SelectionLoop expectedHole{Vec2{1, 1}, Vec2{1, 2}, Vec2{2, 2}, Vec2{2, 1}};
    Expect(traced && traced->size() == 2 && HasLiteralLoop(*traced, expectedOuter) &&
               HasLiteralLoop(*traced, expectedHole),
           "golden.trace-hole-literal-quad-vertices");

    constexpr std::array<std::uint8_t, 4> cornerTouches{255, 0, 0, 255};
    const auto corners = MagicWand::Trace(std::vector<std::uint8_t>(cornerTouches.begin(), cornerTouches.end()), 2, 2);
    const SelectionLoop expectedFirst{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};
    const SelectionLoop expectedLast{Vec2{1, 1}, Vec2{2, 1}, Vec2{2, 2}, Vec2{1, 2}};
    Expect(corners && corners->size() == 2 && HasLiteralLoop(*corners, expectedFirst) &&
               HasLiteralLoop(*corners, expectedLast),
           "golden.trace-corner-touch-literal-quad-vertices");
}

void InputAndRecovery()
{
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 40, 255, 0, 0, 40, 255, 0, 0, 40, 255, 0, 0, 40, 255};
    const Raster source = Bgra(2, 2, pixels);
    const void* const identity = source.Identity();
    CheckMaskCancellation(MagicWand::WorkStage::Source, 0, source, "input.source-cancel-checkpoint-reached",
                          "input.source-cancel-discards-mask");
    long recoveredCount = -1;
    CancelSource freshToken;
    const auto afterCancel = MagicWand::Mask(source, 0, 0, Settings(), recoveredCount, freshToken.Token());
    constexpr std::array<std::uint8_t, 4> expectedMask{255, 255, 255, 255};
    Expect(SameLiteralMask(afterCancel, expectedMask, recoveredCount, 4) && source.Identity() == identity &&
               source.Data() != nullptr && std::equal(pixels.begin(), pixels.end(), source.Data()),
           "input.fresh-active-token-recovers-with-literal-mask-and-source-stable");

    const Raster tile = Bgra(2, 2, pixels);
    const Raster sparse = Raster::MakeSparse(4, 4, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{1, 1, tile}}, 0);
    CancelSource sparseCancel;
    CancellationProbe probe{&sparseCancel, MagicWand::WorkStage::Source, 0, 0};
    long count = 4;
    const auto cancelledSparse =
        MagicWand::Mask(sparse, 1, 1, Settings(), count, sparseCancel.Token(), Observer(probe));
    Expect(probe.hits == 1 && cancelledSparse.empty() && count == MagicWand::kCancelledMaskCount &&
               !sparse.IsMaterialized(),
           "sparse.cancelled-wand-read-keeps-source-unmaterialized");

    CancelSource afterReadCancel;
    CancellationProbe afterRead{&afterReadCancel, MagicWand::WorkStage::Source, 1, 0};
    count = 0;
    const auto afterReadMask =
        MagicWand::Mask(sparse, 1, 1, Settings(), count, afterReadCancel.Token(), Observer(afterRead));
    Expect(afterRead.hits == 1 && afterReadMask.empty() && count == MagicWand::kCancelledMaskCount &&
               !sparse.IsMaterialized(),
           "sparse.cancel-after-private-read-discards-without-publishing-cache");

    long sparseCount = -1;
    const auto sparseMask = MagicWand::Mask(sparse, 1, 1, Settings(), sparseCount, CancelToken{});
    constexpr std::array<std::uint8_t, 16> sparseExpected{0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0};
    Expect(sparseMask.size() == sparseExpected.size() && sparseCount == 4 &&
               std::equal(sparseExpected.begin(), sparseExpected.end(), sparseMask.begin()) && !sparse.IsMaterialized(),
           "sparse.successful-wand-read-literal-keeps-source-unmaterialized");

    constexpr std::array<std::uint8_t, 4> tileGray{0, 0, 0, 0};
    const Raster grayTile = Gray(2, 2, tileGray);
    const Raster sparseGray =
        Raster::MakeSparse(4, 4, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 1, grayTile}}, 255);
    CancelSource traceCancel;
    WandFailure failure = WandFailure::None;
    CancellationProbe traceProbe{&traceCancel, MagicWand::WorkStage::TraceInitialize, 0, 0};
    const auto cancelledTrace =
        MaskTracing::DarkPixels(sparseGray, &failure, traceCancel.Token(), Observer(traceProbe));
    Expect(traceProbe.hits == 1 && !cancelledTrace && failure == WandFailure::Cancelled && !sparseGray.IsMaterialized(),
           "sparse.cancelled-mask-tracing-keeps-source-unmaterialized");

    failure = WandFailure::None;
    const auto sparseTrace = MaskTracing::DarkPixels(sparseGray, &failure);
    const std::vector<SelectionLoop> expectedDark{SelectionLoop{Vec2{1, 1}, Vec2{3, 1}, Vec2{3, 3}, Vec2{1, 3}}};
    Expect(sparseTrace && *sparseTrace == expectedDark && failure == WandFailure::None && !sparseGray.IsMaterialized(),
           "sparse.successful-mask-tracing-literal-keeps-source-unmaterialized");
}

void RenderLayerLiteralGoldens()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    const Raster image = Bgra(2, 2, pixels);
    LayerTransform transform;
    transform.origin = Vec2{1, 1};
    transform.size = Vec2{2, 2};
    transform.sampling = LayerSampling::Nearest;
    const Raster placed = RenderLayerPixels(image, transform, 4, 4);
    constexpr std::array<std::uint8_t, 64> expected{0, 0, 0, 0, 0,  0,  0,  0,   0,   0,   0,   0,   0, 0, 0, 0,
                                                    0, 0, 0, 0, 10, 20, 30, 255, 40,  50,  60,  255, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 70, 80, 90, 255, 100, 110, 120, 255, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 0,  0,  0,  0,   0,   0,   0,   0,   0, 0, 0, 0};
    Expect(placed.HasStorage() && placed.Width() == 4 && placed.Height() == 4 &&
               placed.Format() == RasterFormat::Bgra8888 && std::equal(expected.begin(), expected.end(), placed.Data()),
           "render.literal-transformed-source-pixel-offsets");

    CancelSource cancel;
    static_cast<void>(cancel.Cancel());
    const Raster blank = RenderLayerPixels(Raster{}, transform, 4, 4, cancel.Token());
    const Raster cancelled = RenderLayerPixels(image, transform, 4, 4, cancel.Token());
    Expect(!blank.HasStorage() && !cancelled.HasStorage(), "render.precancel-blank-and-valid-source-return-empty");
}
} // namespace

int RunWandCancellationTests()
{
    gAssertions = 0;
    gFailures = 0;
    PreCancelledTokens();
    OwnerCancelsReachedWorker();
    DeterministicCheckpointCancellation();
    LiteralMaskGoldens();
    TraceLiteralGoldens();
    InputAndRecovery();
    RenderLayerLiteralGoldens();
    std::printf("wand-cancel.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

#include "Document.Tests.WarpReplay.h"

#include "BrushStroke.h"
#include "SharedPublicationMemory.h"
#include "WarpPreviewWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
constexpr int kCanvasSide = 16;
constexpr BrushSettings kBrushSettings{6, 1, 1, 0, 0, 1};

int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s warp-replay.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

// Literal premultiplied sample; the original layer is deliberately different.
constexpr std::array<std::array<std::uint8_t, 4>, 16> kPremultipliedPixels{{
    {{0, 0, 0, 0}},
    {{16, 8, 32, 64}},
    {{24, 12, 48, 96}},
    {{32, 16, 64, 128}},
    {{40, 20, 80, 160}},
    {{48, 24, 96, 192}},
    {{56, 28, 112, 224}},
    {{64, 32, 128, 255}},
    {{8, 24, 40, 64}},
    {{12, 36, 60, 96}},
    {{16, 48, 80, 128}},
    {{20, 60, 100, 160}},
    {{24, 72, 120, 192}},
    {{28, 84, 140, 224}},
    {{32, 96, 160, 255}},
    {{4, 12, 20, 32}},
}};

Raster PatternRaster()
{
    std::array<std::uint8_t, static_cast<std::size_t>(kCanvasSide * kCanvasSide * 4)> bytes{};
    for (int y = 0; y < kCanvasSide; ++y)
    {
        for (int x = 0; x < kCanvasSide; ++x)
        {
            const auto& pixel = kPremultipliedPixels[static_cast<std::size_t>((x + y) % 16)];
            const std::size_t offset = static_cast<std::size_t>((y * kCanvasSide + x) * 4);
            for (std::size_t channel = 0; channel < pixel.size(); ++channel)
                bytes[offset + channel] = pixel[channel];
        }
    }
    return Raster::Make(kCanvasSide, kCanvasSide, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster OriginalLayerRaster()
{
    // Deliberately different from the sample: otherwise all coverage/spline errors are invisible.
    std::array<std::uint8_t, kCanvasSide * kCanvasSide * 4> bytes{};
    constexpr std::array<std::uint8_t, 4> pixel{8, 4, 12, 32};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = pixel[i % 4];
    return Raster::Make(kCanvasSide, kCanvasSide, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster FractionalSelection()
{
    constexpr std::array<std::uint8_t, 16> row{0, 32, 64, 96, 128, 160, 192, 224, 255, 224, 192, 160, 128, 96, 64, 32};
    std::array<std::uint8_t, static_cast<std::size_t>(kCanvasSide * kCanvasSide)> bytes{};
    for (int y = 0; y < kCanvasSide; ++y)
    {
        for (int x = 0; x < kCanvasSide; ++x)
            bytes[static_cast<std::size_t>(y * kCanvasSide + x)] = row[static_cast<std::size_t>((x + y * 3) % 16)];
    }
    return Raster::Make(kCanvasSide, kCanvasSide, RasterFormat::Gray8, bytes.data(), bytes.size());
}

ImageLayer ReplayLayer(const Raster& image)
{
    ImageLayer layer = ImageLayer::Blank(L"Warp replay", Vec2{kCanvasSide, kCanvasSide});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Warp replay"};
    layer.transform.origin = Vec2{0, 0};
    layer.transform.size = Vec2{kCanvasSide, kCanvasSide};
    layer.transform.sampling = LayerSampling::Nearest;
    return layer;
}

struct PreparedPreview final
{
    std::shared_ptr<const PreparedWarpPreview> preview;
    bool validNoDabSource = false;
};

PreparedPreview PreparePreviewOnJoinedWorker(const Raster& image, std::thread::id owner)
{
    PreparedPreview result;
    std::thread worker(
        [&result, image, owner]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            LayerTransform placement;
            placement.origin = Vec2{0, 0};
            placement.size = Vec2{kCanvasSide, kCanvasSide};
            placement.sampling = LayerSampling::Nearest;
            auto stroke = WarpStroke::MakePlacedForWork(WarpSourceView{image, placement, kCanvasSide, kCanvasSide},
                                                        SmearMode::Liquify, BrushSettings{6, 1, 0, 0, 0, 1}, owner,
                                                        CancelToken{std::stop_token{}, std::stop_token{}}, memory);
            if (!stroke || !stroke->IsValid() || !stroke->Points().empty() || !memory)
                return;
            result.preview = PrepareWarpPreviewForWork(
                *stroke, owner, CancelToken{std::stop_token{}, std::stop_token{}}, std::move(memory));
            result.validNoDabSource =
                result.preview && result.preview->source.image == image && result.preview->tiles.empty();
        });
    worker.join();
    return result;
}

struct Scenario final
{
    const char* name;
    std::span<const Vec2> points;
    bool selected;
    bool probeNonfinite;
};

struct ReplayResult final
{
    std::optional<BrushStroke::Commit> replay;
    std::optional<BrushStroke::Commit> scalar;
    bool replayCompleted = false;
    bool scalarCompleted = false;
    bool nonfiniteRefused = false;
};

bool SameSnapshot(const BrushStroke::Commit& left, const BrushStroke::Commit& right)
{
    if (left.bounds != right.bounds || left.transform != right.transform || !left.image.HasStorage() ||
        !right.image.HasStorage() || left.image.Width() != right.image.Width() ||
        left.image.Height() != right.image.Height())
        return false;

    const std::size_t byteCount =
        static_cast<std::size_t>(left.image.Width()) * static_cast<std::size_t>(left.image.Height()) * 4u;
    std::vector<std::uint8_t> leftBytes(byteCount);
    std::vector<std::uint8_t> rightBytes(byteCount);
    const int stride = left.image.Width() * 4;
    left.image.ReadRegion(0, 0, left.image.Width(), left.image.Height(), leftBytes.data(), stride, 0);
    right.image.ReadRegion(0, 0, right.image.Width(), right.image.Height(), rightBytes.data(), stride, 0);
    return leftBytes == rightBytes;
}

ReplayResult RunPairedReplayOnJoinedWorker(std::shared_ptr<const PreparedWarpPreview> preview, ImageLayer layer,
                                           Raster sampleImage, Raster selection, Scenario scenario)
{
    ReplayResult result;
    std::thread worker(
        [&result, preview = std::move(preview), layer = std::move(layer), sampleImage = std::move(sampleImage),
         selection = std::move(selection), scenario]
        {
            const CancelToken live{std::stop_token{}, std::stop_token{}};
            auto replay = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
            if (replay && replay->IsValid())
            {
                if (scenario.selected)
                    replay->SetSelectionClip(selection);
                bool complete = replay->BeginWarpReplayForWork(preview, false, live);
                for (std::size_t i = 0; complete && i < scenario.points.size(); ++i)
                {
                    if (scenario.probeNonfinite && i == 1)
                    {
                        const double nan = std::numeric_limits<double>::quiet_NaN();
                        result.nonfiniteRefused =
                            !replay->PrepareWarpReplaySampleForWork(Vec2{nan, 8}, live).has_value();
                    }
                    const auto sample = replay->PrepareWarpReplaySampleForWork(scenario.points[i], live);
                    complete = sample.has_value() && replay->ComposeSampleCoverage(sample.value());
                }
                if (complete)
                {
                    const auto final = replay->PrepareWarpReplayFinishForWork(live);
                    complete = final.has_value() && replay->ComposeFinalCoverage(final.value());
                }
                result.replayCompleted = complete;
                if (complete)
                    result.replay = replay->PaintSnapshot();
            }

            BrushStroke scalar(layer, false, kBrushSettings, kCanvasSide, kCanvasSide, &CpuBrushCoverage::Shared());
            if (scalar.IsValid())
            {
                if (scenario.selected)
                    scalar.SetSelectionClip(selection);
                scalar.SetSample(sampleImage, 0, 0, true); // Original literal fixture, never candidate preview data.
                bool complete = true;
                for (const Vec2 point : scenario.points)
                    complete = scalar.Append(point) && complete;
                complete = scalar.Flush() && complete;
                result.scalarCompleted = complete;
                if (complete)
                    result.scalar = scalar.PaintSnapshot();
            }
        });
    worker.join();
    return result;
}

struct RefusalResults final
{
    bool missingPreviewRefused = false;
    bool secondBeginRefused = false;
    bool uncomposedBlocksAdvanceAndFinish = false;
    bool cancelledPrefixHasNoSnapshot = false;
    std::shared_ptr<BrushStroke> ownerProbe;
};

RefusalResults ExerciseWorkerRefusals(const std::shared_ptr<const PreparedWarpPreview>& preview,
                                      const ImageLayer& layer)
{
    RefusalResults result;
    std::thread worker(
        [&result, preview, layer]
        {
            const CancelToken live{std::stop_token{}, std::stop_token{}};

            auto missing = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
            result.missingPreviewRefused =
                missing && missing->IsValid() && !missing->BeginWarpReplayForWork(nullptr, false, live);

            auto twice = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
            if (twice && twice->BeginWarpReplayForWork(preview, false, live))
                result.secondBeginRefused = !twice->BeginWarpReplayForWork(preview, false, live);

            auto gated = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
            if (gated && gated->BeginWarpReplayForWork(preview, false, live))
            {
                const auto pending = gated->PrepareWarpReplaySampleForWork(Vec2{5, 8}, live);
                const auto next = gated->PrepareWarpReplaySampleForWork(Vec2{8, 8}, live);
                const auto finish = gated->PrepareWarpReplayFinishForWork(live);
                result.uncomposedBlocksAdvanceAndFinish =
                    pending.has_value() && !next.has_value() && !finish.has_value();
            }

            auto cancelled = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
            if (cancelled && cancelled->BeginWarpReplayForWork(preview, false, live))
            {
                const auto sample = cancelled->PrepareWarpReplaySampleForWork(Vec2{5, 8}, live);
                const bool composed = sample && cancelled->ComposeSampleCoverage(sample.value());
                CancelSource stop;
                static_cast<void>(stop.Cancel());
                const auto refused = cancelled->PrepareWarpReplaySampleForWork(
                    Vec2{8, 8}, CancelToken{stop.Token().LocalToken(), std::stop_token{}});
                result.cancelledPrefixHasNoSnapshot =
                    composed && !refused.has_value() && !cancelled->PaintSnapshot().has_value();
            }

            // Transfer only after this thread joins; main then calls Begin as the preview owner.
            result.ownerProbe = BrushStroke::MakeForWork(layer, false, kBrushSettings, kCanvasSide, kCanvasSide);
        });
    worker.join();
    return result;
}

} // namespace

int RunWarpReplayTests()
{
    const Raster image = PatternRaster();
    const Raster selection = FractionalSelection();
    const ImageLayer layer = ReplayLayer(OriginalLayerRaster());
    const PreparedPreview prepared = PreparePreviewOnJoinedWorker(image, std::this_thread::get_id());
    Expect(image.HasStorage() && selection.HasStorage() && prepared.validNoDabSource,
           "worker-preview-is-immutable-original-16x16-source-with-no-dabs");

    constexpr std::array<Vec2, 6> curved{
        {Vec2{3, 3}, Vec2{12, 3}, Vec2{12, 12}, Vec2{3, 12}, Vec2{3, 3}, Vec2{12, 12}}};
    constexpr std::array<Vec2, 5> duplicates{{Vec2{4, 8}, Vec2{4, 8}, Vec2{10, 8}, Vec2{10, 8}, Vec2{6, 8}}};
    constexpr std::array<Vec2, 1> onePoint{{Vec2{8, 8}}};
    constexpr std::array<Vec2, 3> outlier{{Vec2{3, 8}, Vec2{6, 8}, Vec2{1e8, 8}}};
    const std::array<Scenario, 4> scenarios{{
        {"curved-selected-return-self-cross", curved, true, false},
        {"duplicate-points", duplicates, false, true},
        {"single-point", onePoint, false, false},
        {"finite-outlier-preserves-prefix", outlier, false, false},
    }};
    std::array<ReplayResult, 4> results{};
    if (prepared.preview)
    {
        for (std::size_t i = 0; i < scenarios.size(); ++i)
            results[i] = RunPairedReplayOnJoinedWorker(prepared.preview, layer, image, selection, scenarios[i]);
    }

    for (std::size_t i = 0; i < scenarios.size(); ++i)
    {
        const char* name = scenarios[i].name;
        std::array<char, 96> checkName{};
        std::snprintf(checkName.data(), checkName.size(), "%s-replay-completes", name);
        Expect(results[i].replayCompleted && results[i].replay.has_value(), checkName.data());
        std::snprintf(checkName.data(), checkName.size(), "%s-scalar-reference-completes", name);
        Expect(results[i].scalarCompleted && results[i].scalar.has_value(), checkName.data());
        std::snprintf(checkName.data(), checkName.size(), "%s-exact-snapshot-match", name);
        Expect(results[i].replay && results[i].scalar &&
                   SameSnapshot(results[i].replay.value(), results[i].scalar.value()),
               checkName.data());
    }
    const auto literalPixel = [](const std::optional<BrushStroke::Commit>& snapshot, int x, int y,
                                 const std::array<std::uint8_t, 4>& expected)
    {
        std::array<std::uint8_t, 4> actual{};
        if (!snapshot || x < snapshot->bounds.x || y < snapshot->bounds.y ||
            x >= snapshot->bounds.x + snapshot->bounds.width || y >= snapshot->bounds.y + snapshot->bounds.height)
            return false;
        snapshot->image.ReadRegion(x - snapshot->bounds.x, y - snapshot->bounds.y, 1, 1, actual.data(), 4, 0);
        return actual == expected;
    };
    // The full hard coverage at (3,3) mixes literal (56,28,112,224) through selection 128/255 with
    // original (8,4,12,32): rounded result (32,16,62,128). Selection zero at (0,0) keeps original bytes.
    Expect(literalPixel(results[0].replay, 3, 3, {32, 16, 62, 128}) &&
               literalPixel(results[0].replay, 0, 0, {8, 4, 12, 32}),
           "selected-full-coverage-mixes-independent-literal-and-zero-coverage-preserves-original");
    Expect(literalPixel(results[2].replay, 8, 8, {}) && literalPixel(results[2].replay, 0, 0, {8, 4, 12, 32}),
           "single-point-transparent-sample-clears-tip-interior-and-preserves-far-original");
    Expect(results[1].nonfiniteRefused, "nonfinite-point-refused-with-prefix-preserved");

    RefusalResults refusals;
    if (prepared.preview)
        refusals = ExerciseWorkerRefusals(prepared.preview, layer);
    Expect(refusals.missingPreviewRefused, "worker-refuses-missing-preview");
    Expect(refusals.secondBeginRefused, "worker-refuses-second-begin");
    Expect(refusals.uncomposedBlocksAdvanceAndFinish, "worker-requires-compose-before-next-sample-or-finish");
    Expect(refusals.cancelledPrefixHasNoSnapshot, "worker-cancellation-invalidates-private-prefix-snapshot");
    const bool ownerRefused = prepared.preview && refusals.ownerProbe &&
                              !refusals.ownerProbe->BeginWarpReplayForWork(
                                  prepared.preview, false, CancelToken{std::stop_token{}, std::stop_token{}});
    Expect(ownerRefused, "known-preview-owner-refuses-replay-mutation-after-worker-join");

    std::printf("SUMMARY warp-replay checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

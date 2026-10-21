#include "Document.Tests.WarpWorkerCancellation.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s warp-worker.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster Solid(int width, int height, std::array<std::uint8_t, 4> pixel)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < pixels.size(); offset += pixel.size())
        std::copy(pixel.begin(), pixel.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    return Bgra(width, height, pixels);
}

bool PixelsEqual(const Raster& actual, const Raster& expected) noexcept
{
    const std::size_t bytes =
        static_cast<std::size_t>(expected.Width()) * static_cast<std::size_t>(expected.Height()) * 4u;
    return actual.HasStorage() && expected.HasStorage() && actual.Width() == expected.Width() &&
           actual.Height() == expected.Height() && actual.Format() == RasterFormat::Bgra8888 &&
           expected.Format() == RasterFormat::Bgra8888 && actual.Data() && expected.Data() &&
           std::equal(actual.Data(), actual.Data() + bytes, expected.Data());
}

struct StopAtStage final
{
    std::stop_source* source = nullptr;
    WarpStroke::WorkStage target{};
    std::size_t hits = 0;
    std::size_t stopAt = 1;
};

void RequestStopAtStage(void* opaque, WarpStroke::WorkStage stage) noexcept
{
    auto& observer = *static_cast<StopAtStage*>(opaque);
    if (stage == observer.target)
    {
        ++observer.hits;
        if (observer.hits == observer.stopAt)
            static_cast<void>(observer.source->request_stop());
    }
}

WarpStroke::WorkObserver Observer(StopAtStage& state) noexcept
{
    return WarpStroke::WorkObserver{RequestStopAtStage, &state};
}

void CheckCancelledAt(WarpStroke::WorkStage stage, SmearMode mode, const char* name)
{
    // A 640x640 source reaches deterministic row checkpoints and leaves enough private work to discard.
    constexpr std::array<std::uint8_t, 4> literal{12, 24, 48, 96}; // premultiplied BGRA
    const Raster source = Solid(640, 640, literal);
    const void* const identity = source.Identity();
    std::stop_source stop;
    const std::size_t stopAt = stage == WarpStroke::WorkStage::Sample     ? 3u
                               : stage == WarpStroke::WorkStage::Complete ? 1u
                                                                          : 21u;
    StopAtStage observer{&stop, stage, 0, stopAt};
    WarpStroke stroke(source, mode, BrushSettings{40, 0.5, 0, 0, 0, 0.8}, CancelToken{{}, stop.get_token()});
    // Pick-up occurs on the first point; other stages run after an uncancelled opening. Row cancellation
    // happens at the center of the tip after preceding rows, not merely at the entry guard.
    bool accepted = false;
    if (stage == WarpStroke::WorkStage::PickUpRow)
        accepted = stroke.AppendForWork({200, 200}, CancelToken{{}, stop.get_token()}, Observer(observer));
    else
    {
        stroke.Append({200, 200});
        accepted = stroke.AppendForWork({224, 200}, CancelToken{{}, stop.get_token()}, Observer(observer));
    }
    Expect(!accepted && observer.hits == stopAt && stop.stop_requested(), name);
    Expect(!stroke.IsValid() && !stroke.Image().HasStorage() && stroke.TakeChangedTiles().empty(),
           "cancel.invalidates-private-stroke-and-hides-all-partial-pixels");
    Expect(source.Identity() == identity && PixelsEqual(source, Solid(640, 640, literal)),
           "cancel.original-source-owner-and-literal-pixels-remain-valid");
}

void CancellationCheckpoints()
{
    CheckCancelledAt(WarpStroke::WorkStage::Sample, SmearMode::Liquify, "checkpoint.sample-observed-and-stopped");
    CheckCancelledAt(WarpStroke::WorkStage::PickUpRow, SmearMode::Smudge, "checkpoint.pickup-row-observed-and-stopped");
    CheckCancelledAt(WarpStroke::WorkStage::SmudgeRow, SmearMode::Smudge, "checkpoint.smudge-row-observed-and-stopped");
    CheckCancelledAt(WarpStroke::WorkStage::ScratchRow, SmearMode::Liquify,
                     "checkpoint.scratch-row-observed-and-stopped");
    CheckCancelledAt(WarpStroke::WorkStage::PushRow, SmearMode::Liquify, "checkpoint.push-row-observed-and-stopped");
    CheckCancelledAt(WarpStroke::WorkStage::Complete, SmearMode::Smudge,
                     "checkpoint.complete-observed-and-stopped-before-publication");
}

void PreCancelledTokens()
{
    constexpr std::array<std::uint8_t, 4> pixel{8, 16, 32, 64};
    const Raster source = Solid(2, 2, pixel);
    for (const bool shutdown : {false, true})
    {
        std::stop_source local;
        std::stop_source workerShutdown;
        std::stop_source* selected = shutdown ? &workerShutdown : &local;
        static_cast<void>(selected->request_stop());
        const CancelToken token =
            shutdown ? CancelToken{{}, workerShutdown.get_token()} : CancelToken{local.get_token(), {}};
        WarpStroke stroke(source, SmearMode::Smudge, BrushSettings{}, token);
        Expect(!stroke.IsValid() && !stroke.Image().HasStorage() && stroke.Points().empty() &&
                   stroke.TakeChangedTiles().empty(),
               shutdown ? "precancel.shutdown-token-refuses-stroke" : "precancel.local-token-refuses-stroke");
    }
}

void LiteralConstantAndBlankInvariance()
{
    constexpr std::array<std::uint8_t, 4> transparent{0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 4> constant{32, 16, 8, 64}; // each color channel <= alpha
    for (const SmearMode mode : {SmearMode::Liquify, SmearMode::Smudge})
    {
        for (const auto& literal : {transparent, constant})
        {
            const Raster source = Solid(48, 32, literal);
            WarpStroke stroke(source, mode, BrushSettings{20, 0.5, 0, 0, 0, 0.8});
            stroke.Append(Vec2{12, 16});
            stroke.Append(Vec2{27, 16});
            Expect(stroke.IsValid() && PixelsEqual(stroke.Image(), source),
                   mode == SmearMode::Liquify ? "literal.liquify-blank-or-constant-preserved"
                                              : "literal.smudge-blank-or-constant-preserved");
        }
    }
}

void DuplicateAndSubspacingPointRules()
{
    const Raster source = Solid(128, 32, {16, 16, 16, 64});
    // Live formula: spacing = max(1, diameter*0.08) for Smudge, and steps = ceil(distance/spacing).
    // At diameter 40, spacing is 3.2. The initial point seeds pickup but is not in Points().
    WarpStroke smudge(source, SmearMode::Smudge, BrushSettings{40, 0.5, 0, 0, 0, 0.8});
    smudge.Append(Vec2{10, 10});
    smudge.Append(Vec2{10, 10});   // duplicate: distance 0 < 3.2, no dabs
    smudge.Append(Vec2{11.5, 10}); // subspacing: still measured from accepted x=10
    Expect(smudge.Points().empty(), "points.duplicate-and-subspacing-inputs-add-no-dabs");
    smudge.Append(Vec2{20, 10}); // ceil(10/3.2)=4, centers at x=12.5,15,17.5,20
    constexpr std::array<Vec2, 4> expectedSmudge{{{12.5, 10}, {15, 10}, {17.5, 10}, {20, 10}}};
    Expect(smudge.Points().size() == expectedSmudge.size() &&
               std::equal(expectedSmudge.begin(), expectedSmudge.end(), smudge.Points().begin()),
           "points.smudge-count-and-centers-follow-serial-spacing-formula");

    // Liquify formula: spacing = max(1, diameter*0.025), hence 1 pixel at diameter 40.
    WarpStroke liquify(source, SmearMode::Liquify, BrushSettings{40, 0.5, 0, 0, 0, 1});
    liquify.Append(Vec2{10, 10});
    liquify.Append(Vec2{10, 10});
    liquify.Append(Vec2{10.5, 10});
    Expect(liquify.Points().empty(), "points.liquify-duplicate-and-subspacing-are-ignored");
    liquify.Append(Vec2{13, 10}); // accepted endpoint remains x=10; ceil(3/1)=3
    constexpr std::array<Vec2, 3> expectedLiquify{{{11, 10}, {12, 10}, {13, 10}}};
    Expect(liquify.Points().size() == expectedLiquify.size() &&
               std::equal(expectedLiquify.begin(), expectedLiquify.end(), liquify.Points().begin()),
           "points.liquify-count-and-centers-follow-serial-spacing-formula");
}

void RefusalAndSafeCoordinates()
{
    constexpr std::array<std::uint8_t, 4> bytes{0, 0, 0, 255};
    const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8);
    WarpStroke unsupportedFormat(gray, SmearMode::Liquify, BrushSettings{});
    Expect(!unsupportedFormat.IsValid() && !unsupportedFormat.Image().HasStorage() &&
               unsupportedFormat.Points().empty() && unsupportedFormat.TakeChangedTiles().empty(),
           "refusal.gray-source-has-no-private-publication");

    const Raster source = Solid(2, 2, bytes);
    WarpStroke unsupportedMode(source, static_cast<SmearMode>(0xff), BrushSettings{});
    Expect(!unsupportedMode.IsValid() && !unsupportedMode.Image().HasStorage() && unsupportedMode.Points().empty() &&
               unsupportedMode.TakeChangedTiles().empty(),
           "refusal.unsupported-mode-has-no-private-publication");
    WarpStroke nonfiniteSettings(source, SmearMode::Liquify,
                                 BrushSettings{std::numeric_limits<double>::infinity(), 0.5, 0, 0, 0, 1});
    Expect(!nonfiniteSettings.IsValid() && !nonfiniteSettings.Image().HasStorage() &&
               nonfiniteSettings.Points().empty() && nonfiniteSettings.TakeChangedTiles().empty(),
           "refusal.nonfinite-diameter-has-no-private-publication");

    WarpStroke safe(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1});
    safe.Append(Vec2{std::numeric_limits<double>::quiet_NaN(), 0});
    safe.Append(Vec2{std::numeric_limits<double>::infinity(), 0});
    safe.Append(Vec2{-1, 0});
    safe.Append(Vec2{0, 0});
    safe.Append(Vec2{1, 1});
    const Raster result = safe.Image();
    Expect(safe.IsValid() && result.HasStorage() && result.Width() == 2 && result.Height() == 2 &&
               safe.Points().size() == 3,
           "coordinates.nonfinite-and-near-edge-inputs-remain-bounded-and-safe");

    WarpStroke invalidInput(source, SmearMode::Liquify, BrushSettings{});
    Expect(!invalidInput.AppendForWork({std::numeric_limits<double>::quiet_NaN(), 0}, {}) && !invalidInput.IsValid() &&
               !invalidInput.Image().HasStorage() && invalidInput.TakeChangedTiles().empty(),
           "coordinates.worker-nonfinite-input-invalidates-draft");
    WarpStroke distant(source, SmearMode::Liquify, BrushSettings{});
    Expect(!distant.AppendForWork({static_cast<double>((std::numeric_limits<int>::max)()), 0}, {}) &&
               !distant.IsValid() && !distant.Image().HasStorage() && distant.TakeChangedTiles().empty(),
           "coordinates.outside-safe-int-center-bound-refused-before-narrowing");
    WarpStroke badHardness(source, SmearMode::Smudge,
                           BrushSettings{40, std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 1});
    Expect(!badHardness.IsValid(), "refusal.nonfinite-hardness-refused-before-tip-arithmetic");
    WarpStroke badStrength(source, SmearMode::Liquify,
                           BrushSettings{40, 0.5, 0, 0, 0, std::numeric_limits<double>::infinity()});
    Expect(!badStrength.IsValid(), "refusal.nonfinite-strength-refused-before-displacement-arithmetic");
    WarpStroke oversized(source, SmearMode::Smudge, BrushSettings{2001, 0.5, 0, 0, 0, 1});
    Expect(!oversized.IsValid(), "refusal.oversized-tip-refused-before-integer-radius-arithmetic");
}

void InvalidPrivateWarpCannotCommitAsOrdinaryPaint()
{
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    const Raster original = Solid(4, 1, {32, 16, 8, 64});
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                auto& layer = document.layers.front();
                                layer.asset = RasterAsset{original, Raster::MakeThumbnail(original), layer.name};
                            });
    session.smearMode = SmearMode::Liquify;
    session.brushSettings = BrushSettings{8, 0.5, 1, 0, 0, 1};
    const auto generation = session.Generation();
    const auto revision = session.history.CurrentRevision();
    const auto undo = session.history.UndoCount();
    Expect(session.BeginWarp({0, 0}), "session.valid-warp-opening");
    session.ContinueWarp({1, 0});
    auto* draft = session.MutableWarp();
    if (!draft)
    {
        Expect(false, "session.private-draft-present-for-cancel");
        return;
    }
    std::stop_source stop;
    StopAtStage observer{&stop, WarpStroke::WorkStage::Complete, 0, 1};
    Expect(!draft->AppendForWork({2, 0}, CancelToken{{}, stop.get_token()}, Observer(observer)) && !draft->IsValid() &&
               !draft->Points().empty(),
           "session.cancelled-draft-retains-private-prefix-but-hides-image");
    session.FinishWarp();
    const auto* layer = session.ActiveLayer();
    Expect(!session.PendingWarp() && layer && layer->asset && layer->asset->image == original &&
               PixelsEqual(layer->asset->image, Solid(4, 1, {32, 16, 8, 64})),
           "session.invalid-finish-drops-without-turning-missing-sample-into-red-paint");
    Expect(session.Generation() == generation && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undo,
           "session.invalid-finish-preserves-generation-revision-and-history");
    Expect(!session.BeginWarp({std::numeric_limits<double>::quiet_NaN(), 0}) && !session.PendingWarp(),
           "session.nonfinite-opening-refuses-before-publishing-draft");
    Expect(!session.BeginWarp({static_cast<double>((std::numeric_limits<int>::max)()), 0}) && !session.PendingWarp(),
           "session.unsafe-center-opening-cannot-publish-invalid-draft");
}
} // namespace

int RunWarpWorkerCancellationTests()
{
    gAssertions = 0;
    gFailures = 0;
    CancellationCheckpoints();
    PreCancelledTokens();
    LiteralConstantAndBlankInvariance();
    DuplicateAndSubspacingPointRules();
    RefusalAndSafeCoordinates();
    InvalidPrivateWarpCannotCommitAsOrdinaryPaint();
    std::printf("SUMMARY warp-worker checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

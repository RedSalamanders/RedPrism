#include "Document.Tests.WarpSamplePlan.h"

#include "SharedPublicationMemory.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s warp-sample-plan.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Solid(int width, int height, const std::array<std::uint8_t, 4>& pixel)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t offset = 0; offset < bytes.size(); offset += pixel.size())
        std::copy(pixel.begin(), pixel.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

bool IsSolid(const Raster& raster, const std::array<std::uint8_t, 4>& expected) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || !raster.Data())
        return false;
    const auto* pixels = raster.Data();
    const auto bytes = raster.ByteSize();
    for (std::size_t offset = 0; offset < bytes; offset += expected.size())
        if (!std::equal(expected.begin(), expected.end(), pixels + offset))
            return false;
    return true;
}

struct FactoryResult final
{
    std::shared_ptr<WarpStroke> stroke;
    std::thread::id creator;
    std::thread::id owner;
    bool memoryCreated = false;
    bool factoryRanOffOwner = false;
};

std::shared_ptr<WarpStroke> MakeWorkerStroke(const Raster& source, SmearMode mode, const BrushSettings& settings,
                                             std::thread::id owner)
{
    FactoryResult result;
    result.owner = owner;
    std::thread worker(
        [&]
        {
            result.creator = std::this_thread::get_id();
            auto memory = SharedPublicationMemory::CreateForWork();
            result.memoryCreated = static_cast<bool>(memory);
            result.factoryRanOffOwner = result.creator != result.owner;
            result.stroke = WarpStroke::MakeForWork(source, mode, settings, owner, {}, std::move(memory));
        });
    worker.join();
    Expect(result.memoryCreated && result.factoryRanOffOwner && result.creator != result.owner,
           "factory.real-joined-worker-creates-private-draft-off-designated-owner");
    Expect(result.stroke && result.stroke->IsValid() && result.stroke->WorkOwner() == owner,
           "factory.worker-draft-records-owner-for-planning");
    return result.stroke;
}

bool AppendOnJoinedWorker(const std::shared_ptr<WarpStroke>& stroke, const WarpStroke::SampleMemoryPlan& plan,
                          CancelToken cancellation = {})
{
    bool accepted = false;
    std::thread worker([&] { accepted = stroke->AppendPlannedForWork(plan, cancellation); });
    worker.join();
    return accepted;
}

Raster ImageOnJoinedWorker(const std::shared_ptr<WarpStroke>& stroke)
{
    Raster image;
    std::thread worker([&] { image = stroke->ImageForWork({}); });
    worker.join();
    return image;
}

std::vector<RasterTile> ChangedTilesOnJoinedWorker(const std::shared_ptr<WarpStroke>& stroke)
{
    std::vector<RasterTile> tiles;
    std::thread worker([&] { tiles = stroke->TakeChangedTilesForWork({}); });
    worker.join();
    return tiles;
}

std::optional<WarpStroke::SampleMemoryPlan> PlanOnOwner(const std::shared_ptr<WarpStroke>& stroke, Vec2 point)
{
    // The caller is the designated planning owner; expected geometry below is literal and independent of output.
    return stroke->PlanSampleMemory(point);
}

void OwnerPlanningAndSerialCenters()
{
    constexpr std::array<std::uint8_t, 4> literal{32, 16, 8, 64};
    const Raster source = Solid(520, 520, literal);
    const auto owner = std::this_thread::get_id();
    auto stroke = MakeWorkerStroke(source, SmearMode::Smudge, BrushSettings{40, 0.5, 0, 0, 0, 0.8}, owner);
    if (!stroke)
        return;

    // Keep the complete radius-20 pickup inside the image. Edge pickup intentionally carries transparent
    // pixels and is covered by EdgePickupHasIndependentTransparentPixels below, not by constant invariance.
    const auto first = PlanOnOwner(stroke, {40, 40});
    Expect(first && first->sequence == 1 && first->maximumPoints == 0 && first->maximumTiles == 1 &&
               first->maximumChangedTiles == 1 && first->pointCapacity == 0 &&
               stroke->IsSamplePlanCurrentForOwner(first.value()),
           "plan.first-smudge-sequence-and-one-tile-capacity-are-literal");
    if (!first)
        return;
    Expect(first->persistentCpuBytes >= stroke->RetirementMemoryBytes() && first->transientCpuBytes >= 1024 * 1024,
           "plan.first-old-new-graph-admission-covers-observed-retirement-bound");
    const auto before = stroke->RetirementMemoryBytes();
    Expect(AppendOnJoinedWorker(stroke, first.value()), "append.first-plan-mutates-only-on-joined-worker");
    const auto after = stroke->RetirementMemoryBytes();
    Expect(stroke->Points().empty() && stroke->IsValid() && first->persistentCpuBytes >= after &&
               first->persistentCpuBytes <= (std::numeric_limits<std::uint64_t>::max)() - first->transientCpuBytes &&
               first->persistentCpuBytes + first->transientCpuBytes >= before + after,
           "bound.first-plan-covers-actual-draft-and-conservative-old-new-overlap");

    const auto duplicate = PlanOnOwner(stroke, {40, 40});
    Expect(duplicate && duplicate->sequence == 2 && duplicate->maximumPoints == 0,
           "plan.duplicate-still-gets-next-operation-sequence-with-no-dabs");
    if (!duplicate || !AppendOnJoinedWorker(stroke, duplicate.value()))
    {
        Expect(false, "append.duplicate-plan-accepted");
        return;
    }
    const auto subspacing = PlanOnOwner(stroke, {41.5, 40});
    Expect(subspacing && subspacing->sequence == 3 && subspacing->maximumPoints == 0,
           "plan.subspacing-point-keeps-literal-zero-dab-count");
    if (!subspacing || !AppendOnJoinedWorker(stroke, subspacing.value()))
    {
        Expect(false, "append.subspacing-plan-accepted");
        return;
    }
    const auto spacing = PlanOnOwner(stroke, {50, 40});
    // Diameter 40 Smudge spacing is 3.2; from the accepted center (40,40), ceil(10/3.2)=4.
    Expect(spacing && spacing->sequence == 4 && spacing->maximumPoints == 4 && spacing->pointCapacity == 4 &&
               spacing->maximumTiles == 1 && stroke->IsSamplePlanCurrentForOwner(spacing.value()),
           "plan.spacing-count-capacity-and-bounds-follow-literal-serial-geometry");
    if (!spacing || !AppendOnJoinedWorker(stroke, spacing.value()))
    {
        Expect(false, "append.spacing-plan-accepted");
        return;
    }
    constexpr std::array<Vec2, 4> expected{{{42.5, 40}, {45, 40}, {47.5, 40}, {50, 40}}};
    Expect(stroke->Points().size() == expected.size() &&
               std::equal(expected.begin(), expected.end(), stroke->Points().begin()),
           "points.serial-spacing-centers-match-independent-literal-sequence");
    Expect(!stroke->IsSamplePlanCurrentForOwner(spacing.value()), "plan.accepted-plan-is-stale-for-next-owner-capture");
    Expect(IsSolid(ImageOnJoinedWorker(stroke), literal),
           "pixels.constant-premultiplied-input-is-invariant-under-planned-smudge");

    bool wrongThreadPlanAvailable = true;
    std::thread wrongPlanner([&] { wrongThreadPlanAvailable = stroke->PlanSampleMemory({51, 40}).has_value(); });
    wrongPlanner.join();
    Expect(!wrongThreadPlanAvailable, "owner.plan-sampling-refuses-from-non-owner-thread");
    const auto ownerOnly = PlanOnOwner(stroke, {55, 40});
    Expect(ownerOnly && ownerOnly->maximumPoints == 6 && ownerOnly->pointCapacity == 8 &&
               stroke->IsSamplePlanCurrentForOwner(ownerOnly.value()) &&
               !stroke->AppendPlannedForWork(ownerOnly.value(), {}) && stroke->IsValid() &&
               stroke->Points().size() == expected.size(),
           "owner.mutation-refuses-on-planning-owner-with-draft-preserved");
}

void EdgePickupHasIndependentTransparentPixels()
{
    constexpr std::array<std::uint8_t, 4> literal{32, 16, 8, 64};
    const Raster source = Solid(4, 5, literal);
    auto stroke =
        MakeWorkerStroke(source, SmearMode::Smudge, BrushSettings{4, 0.98, 0, 0, 0, 0.8}, std::this_thread::get_id());
    if (!stroke)
        return;
    const auto seed = stroke->PlanSampleMemory({0, 2});
    Expect(seed && seed->maximumPoints == 0 && AppendOnJoinedWorker(stroke, seed.value()),
           "edge.transparent-pickup-opens-at-left-image-boundary");
    const auto next = stroke->PlanSampleMemory({1, 2});
    Expect(next && next->maximumPoints == 1 && AppendOnJoinedWorker(stroke, next.value()) &&
               stroke->Points() == std::vector<Vec2>{{1, 2}},
           "edge.one-literal-spacing-dab-accepted");
    const Raster image = ImageOnJoinedWorker(stroke);
    bool matches = image.HasStorage() && image.Width() == 4 && image.Height() == 5;
    // Radius 2, hardness .98: dx=-1, dy=-1/0/+1 lie in the full-weight inner tip. The pickup
    // at x=-1 is transparent; after one +1 dab only those three destination pixels become zero.
    // The radius boundary has zero weight. Every other contributing pickup was the literal source.
    for (int y = 0; y < 5 && matches; ++y)
        for (int x = 0; x < 4 && matches; ++x)
        {
            std::array<std::uint8_t, 4> actual{};
            image.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
            const auto expected = x == 0 && y >= 1 && y <= 3 ? std::array<std::uint8_t, 4>{} : literal;
            matches = actual == expected;
        }
    Expect(matches && IsSolid(source, literal),
           "edge.only-three-independent-transparent-pickup-pixels-change-source-is-immutable");
}

void GeometryBoundsAndTileCounts()
{
    constexpr std::array<std::uint8_t, 4> literal{12, 24, 48, 96};
    const Raster source = Solid(768, 768, literal);
    const auto owner = std::this_thread::get_id();
    auto stroke = MakeWorkerStroke(source, SmearMode::Liquify, BrushSettings{2, 0.5, 0, 0, 0, 1}, owner);
    if (!stroke)
        return;

    const auto a = PlanOnOwner(stroke, {255, 255});
    Expect(a && a->sequence == 1 && a->maximumPoints == 0 && a->maximumTiles == 0,
           "plan.first-liquify-point-does-not-read-or-allocate-a-tile");
    if (!a || !AppendOnJoinedWorker(stroke, a.value()))
    {
        Expect(false, "append.first-liquify-plan-accepted");
        return;
    }
    const auto b = PlanOnOwner(stroke, {257, 257});
    // Radius 1 plus a one-pixel movement and the safety margin 3 yield reach 5; the swept rectangle [250,262] crosses
    // 2x2 tiles.
    Expect(b && b->sequence == 2 && b->maximumPoints == 3 && b->maximumTiles == 4 && b->maximumChangedTiles == 4,
           "plan.diagonal-boundary-crossing-has-independent-four-tile-upper-bound");
    if (!b || !AppendOnJoinedWorker(stroke, b.value()))
    {
        Expect(false, "append.boundary-plan-accepted");
        return;
    }
    constexpr std::array<Vec2, 3> crossing{
        {{255.66666666666666, 255.66666666666666}, {256.33333333333331, 256.33333333333331}, {257, 257}}};
    Expect(stroke->Points().size() == crossing.size() &&
               std::equal(crossing.begin(), crossing.end(), stroke->Points().begin()),
           "points.boundary-crossing-dabs-use-literal-one-pixel-spacing-centers");
    const auto tiles = ChangedTilesOnJoinedWorker(stroke);
    Expect(tiles.size() == 4 && tiles[0].x == 0 && tiles[0].y == 0 && tiles[1].x == 256 && tiles[1].y == 0 &&
               tiles[2].x == 0 && tiles[2].y == 256 && tiles[3].x == 256 && tiles[3].y == 256,
           "tiles.boundary-crossing-publishes-literal-four-grid-tiles");

    const auto far = PlanOnOwner(stroke, {900, 900});
    // Literal segment distance sqrt(643^2 + 643^2) requires 910 one-pixel Liquify steps.
    Expect(far && far->sequence == 3 && far->maximumPoints == 913 && far->maximumTiles == 9,
           "plan.long-boundary-exit-has-literal-step-count-and-whole-document-tile-bound");
    if (!far || !AppendOnJoinedWorker(stroke, far.value()))
    {
        Expect(false, "append.outside-reaching-plan-accepted");
        return;
    }
    const auto afterFarTiles = ChangedTilesOnJoinedWorker(stroke);
    Expect(stroke->Points().size() == 913 && afterFarTiles.size() <= far->maximumChangedTiles && stroke->IsValid(),
           "points.long-exit-keeps-serial-centers-and-tile-publication-within-bound");
    const auto outside = PlanOnOwner(stroke, {901, 900});
    Expect(outside && outside->sequence == 4 && outside->maximumPoints == 914 && outside->maximumTiles == 9,
           "plan.outside-only-dab-retains-earlier-allocation-bound");
    if (!outside || !AppendOnJoinedWorker(stroke, outside.value()))
    {
        Expect(false, "append.outside-only-plan-accepted");
        return;
    }
    Expect(stroke->Points().size() == 914 && ChangedTilesOnJoinedWorker(stroke).empty() && stroke->IsValid(),
           "points.outside-only-dab-publishes-no-new-tile-data");
    const auto back = PlanOnOwner(stroke, {255, 255});
    Expect(back && back->sequence == 5 && back->maximumPoints == 1827 && back->maximumTiles == 9,
           "plan.return-across-boundaries-covers-existing-nine-tile-envelope");
    if (!back || !AppendOnJoinedWorker(stroke, back.value()))
        Expect(false, "append.return-plan-accepted");
    else
    {
        const auto changed = ChangedTilesOnJoinedWorker(stroke);
        Expect(stroke->IsValid() && changed.size() <= back->maximumChangedTiles,
               "tiles.returning-sample-changed-tile-count-fits-independent-plan-bound");
    }
}

void MaximumDiameterAndBlankInvariance()
{
    constexpr std::array<std::uint8_t, 4> literal{0, 0, 0, 0};
    const Raster blank = Raster::Make(3000, 3000, RasterFormat::Bgra8888);
    const auto owner = std::this_thread::get_id();
    auto stroke = MakeWorkerStroke(blank, SmearMode::Liquify, BrushSettings{2000, 0.5, 0, 0, 0, 1}, owner);
    if (!stroke)
        return;
    const auto seed = PlanOnOwner(stroke, {1500, 1500});
    Expect(seed && seed->sequence == 1 && seed->maximumTiles == 0 && AppendOnJoinedWorker(stroke, seed.value()),
           "plan.maximum-diameter-liquify-seed-does-not-allocate-before-a-dab");
    const auto plan = PlanOnOwner(stroke, {1550, 1500});
    // A 2000-pixel tip has a 1000-pixel radius; the literal swept interval crosses at least an 8x8 grid region.
    // The asserted upper bound is intentionally loose because plan padding may conservatively cover extra tiles.
    Expect(plan && plan->sequence == 2 && plan->maximumPoints == 1 && plan->maximumTiles >= 64 &&
               plan->maximumTiles <= 256 && plan->maximumChangedTiles == plan->maximumTiles,
           "plan.maximum-diameter-has-large-finite-conservative-tile-bound");
    if (!plan || !AppendOnJoinedWorker(stroke, plan.value()))
    {
        Expect(false, "append.maximum-diameter-plan-accepted");
        return;
    }
    Expect(stroke->IsValid() && IsSolid(ImageOnJoinedWorker(stroke), literal) &&
               plan->persistentCpuBytes >= stroke->RetirementMemoryBytes(),
           "pixels.blank-invariance-and-retirement-bound-hold-at-maximum-tip-size");
}

void RefusalPreservesDraftAndCancellationPublishesNothing()
{
    constexpr std::array<std::uint8_t, 4> literal{32, 16, 8, 64};
    const Raster source = Solid(128, 128, literal);
    const auto owner = std::this_thread::get_id();
    auto stroke = MakeWorkerStroke(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1}, owner);
    auto other = MakeWorkerStroke(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1}, owner);
    if (!stroke || !other)
        return;

    const auto plan = PlanOnOwner(stroke, {10, 10});
    const auto foreign = other->PlanSampleMemory({10, 10});
    const auto before = stroke->RetirementMemoryBytes();
    const bool pointsEmpty = stroke->Points().empty();
    const bool foreignRefused = foreign && !AppendOnJoinedWorker(stroke, foreign.value());
    auto edited = plan;
    if (edited)
        ++edited->persistentCpuBytes;
    const bool editedRefused = edited && !AppendOnJoinedWorker(stroke, edited.value());
    auto editedPoint = plan;
    if (editedPoint)
        editedPoint->point.x += 1.0; // Same zero-dab/capacity envelope must not authorize a different point.
    Expect(editedPoint && !AppendOnJoinedWorker(stroke, editedPoint.value()) && stroke->IsValid() &&
               stroke->Points().empty() && stroke->RetirementMemoryBytes() == before &&
               stroke->IsSamplePlanCurrentForOwner(plan.value()),
           "refusal.changed-point-with-equal-envelope-cannot-forge-an-accepted-plan");
    const bool acceptedFirst = plan && AppendOnJoinedWorker(stroke, plan.value());
    const auto second = PlanOnOwner(stroke, {11, 10});
    const bool acceptedSecond = second && AppendOnJoinedWorker(stroke, second.value());
    const auto afterAccepted = stroke->RetirementMemoryBytes();
    const bool staleRefused = plan && !AppendOnJoinedWorker(stroke, plan.value());
    constexpr std::array<Vec2, 1> acceptedPoints{{{11, 10}}};
    Expect(plan && foreign && foreignRefused && edited && editedRefused && acceptedFirst && second && acceptedSecond &&
               staleRefused && stroke->IsValid() && stroke->Points().size() == acceptedPoints.size() &&
               std::equal(acceptedPoints.begin(), acceptedPoints.end(), stroke->Points().begin()) &&
               stroke->RetirementMemoryBytes() == afterAccepted && afterAccepted >= before && pointsEmpty,
           "refusal.foreign-edited-and-stale-plans-preserve-valid-draft-after-two-accepted-steps");
    Expect(plan && second && foreign && !stroke->IsSamplePlanCurrentForOwner(foreign.value()) &&
               !stroke->IsSamplePlanCurrentForOwner(plan.value()) &&
               !stroke->IsSamplePlanCurrentForOwner(second.value()),
           "capture.owner-currentness-check-rejects-foreign-sequence-and-stale-plan");

    WarpStroke reference(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1});
    Expect(reference.IsValid() && !reference.PlanSampleMemory({10, 10}),
           "refusal.reference-constructor-has-no-worker-plan-provenance");
    bool workerCouldPlan = true;
    std::thread wrongPlanner([&] { workerCouldPlan = stroke->PlanSampleMemory({10, 10}).has_value(); });
    wrongPlanner.join();
    Expect(!workerCouldPlan, "refusal.worker-cannot-create-owner-plan");

    auto cancelledStroke = MakeWorkerStroke(source, SmearMode::Liquify, BrushSettings{8, 0.5, 0, 0, 0, 1}, owner);
    const auto cancelledPlan = cancelledStroke ? PlanOnOwner(cancelledStroke, {10, 10}) : std::nullopt;
    if (!cancelledPlan)
    {
        Expect(false, "cancel.fixture-plan-created");
        return;
    }
    std::stop_source stop;
    static_cast<void>(stop.request_stop());
    const bool cancelled =
        !AppendOnJoinedWorker(cancelledStroke, cancelledPlan.value(), CancelToken({}, stop.get_token()));
    const auto unpublishedTiles = ChangedTilesOnJoinedWorker(cancelledStroke);
    Expect(cancelled && !cancelledStroke->IsValid() && !cancelledStroke->Image().HasStorage() &&
               unpublishedTiles.empty() && cancelledStroke->Points().empty(),
           "cancel.cancelled-planned-mutation-invalidates-with-no-published-image-or-tiles");
}
} // namespace

int RunWarpSamplePlanTests()
{
    gAssertions = 0;
    gFailures = 0;
    OwnerPlanningAndSerialCenters();
    EdgePickupHasIndependentTransparentPixels();
    GeometryBoundsAndTileCounts();
    MaximumDiameterAndBlankInvariance();
    RefusalPreservesDraftAndCancellationPublishesNothing();
    std::printf("SUMMARY warp-sample-plan checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

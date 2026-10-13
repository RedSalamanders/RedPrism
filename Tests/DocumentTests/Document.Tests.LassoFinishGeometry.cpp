#include "Document.Tests.LassoFinishGeometry.h"

#include "BoundedLassoGesture.h"
#include "Executor.h"
#include "LassoFinishGeometry.h"
#include "LassoFinishWork.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;
void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s lasso-finish.%s\n", condition ? "PASS" : "FAIL", label);
}

struct Fixture final
{
    std::shared_ptr<SharedPublicationMemory> memory = SharedPublicationMemory::CreateForWork(1024 * 1024);
    std::optional<BoundedLassoGesture> gesture;
    explicit Fixture(std::uint32_t blocks = 8)
    {
        auto points = BoundedLassoPoints::CreateForWork(memory, blocks);
        if (points)
            gesture.emplace(std::move(points.value()));
    }
};

std::optional<BoundedLassoGesture> NewGesture(std::shared_ptr<SharedPublicationMemory> memory, std::uint32_t blocks)
{
    auto points = BoundedLassoPoints::CreateForWork(std::move(memory), blocks);
    if (!points)
        return std::nullopt;
    return BoundedLassoGesture(std::move(points.value()));
}
std::optional<BoundedLassoGesture::Display> Polygon(std::span<const Vec2> points,
                                                    SelectionMode mode = SelectionMode::New, bool antialiased = true,
                                                    std::uint64_t opening = 41, std::uint32_t blocks = 8)
{
    Fixture fixture(blocks);
    if (!fixture.gesture || points.empty() ||
        !fixture.gesture.value().BeginOwner(opening, LassoKind::Freehand, mode, antialiased, points.front()))
        return std::nullopt;
    for (const auto point : points.subspan(1))
        if (fixture.gesture.value().ExtendOwner(fixture.gesture.value().CurrentOwner(), point) !=
            BoundedLassoGesture::Outcome::Changed)
            return std::nullopt;
    return fixture.gesture.value().FinishOwner(fixture.gesture.value().CurrentOwner());
}

std::optional<BoundedLassoGesture::Display> Rectangle(LassoKind kind, Vec2 from, Vec2 to,
                                                      SelectionMode mode = SelectionMode::New, bool antialiased = true)
{
    Fixture fixture;
    if (!fixture.gesture || !fixture.gesture.value().BeginOwner(42, kind, mode, antialiased, from) ||
        fixture.gesture.value().MarqueeOwner(fixture.gesture.value().CurrentOwner(), to, false, false) !=
            BoundedLassoGesture::Outcome::Changed)
        return std::nullopt;
    return fixture.gesture.value().FinishOwner(fixture.gesture.value().CurrentOwner());
}

std::optional<DocumentSelection> BeforeRectangle(double x0, double y0, double x1, double y1, bool aa = true)
{
    SelectionLoop loop{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::vector<SelectionLoop> loops;
    loops.push_back(std::move(loop));
    auto adopted = SelectionLoops::AdoptForWork(std::move(loops));
    if (!adopted)
        return std::nullopt;
    return DocumentSelection{aa, std::move(adopted.value())};
}

std::optional<double> Area(const DocumentSelection& selection) noexcept
{
    double signedTwiceArea = 0.0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.size() < 3)
            continue;
        for (std::size_t index = 0; index < loop.size(); ++index)
        {
            const auto& a = loop[index];
            const auto& b = loop[(index + 1) % loop.size()];
            signedTwiceArea += a.x * b.y - b.x * a.y;
        }
    }
    const double area = std::abs(signedTwiceArea) * 0.5;
    return std::isfinite(area) ? std::optional(area) : std::nullopt;
}
bool Contains(const LassoFinishGeometry::Result& result, Vec2 point) noexcept
{
    return result.selection && SelectionGeometry::Contains(result.selection.value().loops.Get(), point);
}

BoundedSelectionBoolean::Limits SmallLimits() noexcept
{
    BoundedSelectionBoolean::Limits limits;
    limits.workspaceBytes = 1024 * 1024;
    limits.inputPoints = 4096;
    limits.inputLoops = 256;
    limits.outputPoints = 4096;
    limits.outputLoops = 256;
    limits.allocations = 4096;
    return limits;
}

std::optional<LassoFinishGeometry::Result> PrepareBounded(const BoundedLassoGesture::Display& terminal, int width,
                                                          int height, const std::optional<DocumentSelection>& before,
                                                          CancelToken cancellation = {},
                                                          const LassoFinishGeometry::Observer* observer = nullptr,
                                                          std::shared_ptr<const SharedPublicationMemory> memory = {})
{
    auto bounded = observer ? *observer : LassoFinishGeometry::Observer{};
    bounded.limits = SmallLimits();
    return LassoFinishGeometry::Prepare(terminal, width, height, before, cancellation, &bounded, std::move(memory));
}
void CheckLegacyReplaceAddSubtract()
{
    constexpr std::array<Vec2, 4> first{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto base = Polygon(first);
    auto newResult = base ? PrepareBounded(base.value(), 100, 100, std::nullopt, {}, nullptr,
                                           SharedPublicationMemory::CreateForWork(1024 * 1024))
                          : std::nullopt;
    Expect(base && newResult && newResult.value().disposition == LassoFinishGeometry::Disposition::Replace &&
               newResult.value().selection &&
               std::abs(Area(newResult.value().selection.value()).value_or(-1) - 1600.0) < 0.01 &&
               Contains(newResult.value(), {30, 30}) && !Contains(newResult.value(), {70, 70}),
           "legacy-new-square-area-and-literal-interior-outside");

    constexpr std::array<Vec2, 4> second{{{50, 50}, {90, 50}, {90, 90}, {50, 90}}};
    auto addShape = Polygon(second, SelectionMode::Add);
    auto before = BeforeRectangle(10, 10, 50, 50);
    auto add = addShape && before ? PrepareBounded(addShape.value(), 100, 100, before, {}, nullptr,
                                                   SharedPublicationMemory::CreateForWork(1024 * 1024))
                                  : std::nullopt;
    Expect(add && add.value().selection && std::abs(Area(add.value().selection.value()).value_or(-1) - 3200.0) < 0.01 &&
               Contains(add.value(), {30, 30}) && Contains(add.value(), {70, 70}),
           "legacy-add-disjoint-squares-area-and-both-interiors");

    constexpr std::array<Vec2, 4> cut{{{20, 20}, {40, 20}, {40, 40}, {20, 40}}};
    auto cutShape = Polygon(cut, SelectionMode::Subtract);
    auto subtract = cutShape && before ? PrepareBounded(cutShape.value(), 100, 100, before, {}, nullptr,
                                                        SharedPublicationMemory::CreateForWork(1024 * 1024))
                                       : std::nullopt;
    Expect(subtract && subtract.value().selection &&
               std::abs(Area(subtract.value().selection.value()).value_or(-1) - 1200.0) < 0.01 &&
               !Contains(subtract.value(), {30, 30}) && Contains(subtract.value(), {15, 15}),
           "legacy-subtract-square-area-and-retained-border-point");
}

void CheckAbsentEmptyClipAndCapturedAA()
{
    constexpr std::array<Vec2, 4> shape{{{0, 0}, {50, 0}, {50, 50}, {0, 50}}};
    auto subtract = Polygon(shape, SelectionMode::Subtract, true);
    if (subtract)
        subtract.value().antialiased = false;
    auto absent = subtract ? PrepareBounded(subtract.value(), 100, 100, std::nullopt, {}, nullptr,
                                            SharedPublicationMemory::CreateForWork(1024 * 1024))
                           : std::nullopt;
    Expect(absent && absent.value().disposition == LassoFinishGeometry::Disposition::Keep && !absent.value().selection,
           "subtract-from-absent-preserves-absence");

    DocumentSelection empty{true, SelectionLoops{}};
    auto presentEmpty = subtract ? PrepareBounded(subtract.value(), 100, 100, empty, {}, nullptr,
                                                  SharedPublicationMemory::CreateForWork(1024 * 1024))
                                 : std::nullopt;
    Expect(presentEmpty && presentEmpty.value().disposition == LassoFinishGeometry::Disposition::Replace &&
               presentEmpty.value().selection && presentEmpty.value().selection.value().loops.empty() &&
               !presentEmpty.value().selection.value().antialiased,
           "subtract-from-present-empty-stays-present-and-uses-finish-aa");

    constexpr std::array<Vec2, 4> outside{{{-50, -50}, {50, -50}, {50, 50}, {-50, 50}}};
    auto outsideShape = Polygon(outside, SelectionMode::New, false);
    auto clipped = outsideShape ? PrepareBounded(outsideShape.value(), 100, 100, std::nullopt, {}, nullptr,
                                                 SharedPublicationMemory::CreateForWork(1024 * 1024))
                                : std::nullopt;
    const auto bounds =
        clipped && clipped.value().selection ? clipped.value().selection.value().Bounds() : std::nullopt;
    Expect(clipped && clipped.value().selection && bounds && bounds.value().minX == 0 && bounds.value().minY == 0 &&
               bounds.value().maxX == 50 && bounds.value().maxY == 50 &&
               std::abs(Area(clipped.value().selection.value()).value_or(-1) - 2500.0) < 0.01 &&
               !clipped.value().selection.value().antialiased,
           "legacy-outside-canvas-clips-to-literal-fifty-square-and-captures-aa");
}

void CheckMarqueeAndEllipse()
{
    auto reverse = Rectangle(LassoKind::Rectangle, {60.4, 70.6}, {20.2, 30.3});
    auto reversedResult = reverse ? PrepareBounded(reverse.value(), 100, 100, std::nullopt, {}, nullptr,
                                                   SharedPublicationMemory::CreateForWork(1024 * 1024))
                                  : std::nullopt;
    const auto reverseBounds = reversedResult && reversedResult.value().selection
                                   ? reversedResult.value().selection.value().Bounds()
                                   : std::nullopt;
    Expect(reversedResult && reverseBounds && reverseBounds.value().minX == 20 && reverseBounds.value().minY == 30 &&
               reverseBounds.value().maxX == 60 && reverseBounds.value().maxY == 71 &&
               std::abs(Area(reversedResult.value().selection.value()).value_or(-1) - 1640.0) < 0.01,
           "legacy-reversed-marquee-rounds-whole-pixel-rectangle");

    auto ellipse = Rectangle(LassoKind::Ellipse, {10, 20}, {70, 60});
    auto oval = ellipse ? PrepareBounded(ellipse.value(), 100, 100, std::nullopt, {}, nullptr,
                                         SharedPublicationMemory::CreateForWork(1024 * 1024))
                        : std::nullopt;
    const double piArea = 3.14159265358979323846 * 30.0 * 20.0;
    Expect(oval && oval.value().selection && oval.value().kind == LassoKind::Ellipse &&
               std::abs(Area(oval.value().selection.value()).value_or(-1) - piArea) < 3.0 &&
               Contains(oval.value(), {40, 40}) && !Contains(oval.value(), {11, 21}),
           "legacy-ellipse-independent-pi-area-center-and-box-corner");
}

void CheckDegenerateAndKeyLimits()
{
    constexpr std::array<Vec2, 1> one{{{5, 5}}};
    auto click = Polygon(one);
    auto clear = click ? PrepareBounded(click.value(), 100, 100, std::nullopt, {}, nullptr,
                                        SharedPublicationMemory::CreateForWork(1024 * 1024))
                       : std::nullopt;
    Expect(clear && clear.value().disposition == LassoFinishGeometry::Disposition::Clear && !clear.value().selection,
           "legacy-new-click-clears-absent-selection");
    auto addClickShape = Polygon(one, SelectionMode::Add);
    auto keep = addClickShape ? PrepareBounded(addClickShape.value(), 100, 100, std::nullopt, {}, nullptr,
                                               SharedPublicationMemory::CreateForWork(1024 * 1024))
                              : std::nullopt;
    Expect(keep && keep.value().disposition == LassoFinishGeometry::Disposition::Keep && !keep.value().selection,
           "degenerate-add-keeps-absent-selection");

    constexpr std::array<Vec2, 4> square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    auto shape = Polygon(square);
    if (shape)
        shape.value().key.revision = 0;
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, SmallLimits()),
           "zero-revision-terminal-key-refused");
    shape = Polygon(square);
    if (shape)
        shape.value().key.opening = 0;
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, SmallLimits()),
           "zero-opening-terminal-key-refused");
    shape = Polygon(square);
    auto invalidCanvas = shape ? PrepareBounded(shape.value(), 0, 100, std::nullopt, {}, nullptr,
                                                SharedPublicationMemory::CreateForWork(1024 * 1024))
                               : std::nullopt;
    auto tooLargeCanvas = shape ? PrepareBounded(shape.value(), kMaxCanvasSide + 1, 100, std::nullopt, {}, nullptr,
                                                 SharedPublicationMemory::CreateForWork(1024 * 1024))
                                : std::nullopt;
    Expect(shape && !invalidCanvas && !tooLargeCanvas, "nonpositive-and-overlimit-canvas-refused");

    auto limits = SmallLimits();
    limits.inputPoints = 5;
    shape = Polygon(square);
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, limits),
           "input-point-cap-includes-four-point-reserve");
    limits = SmallLimits();
    limits.workspaceBytes = 0;
    shape = Polygon(square);
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, limits), "zero-workspace-limit-refused");
    limits = SmallLimits();
    limits.inputLoops = 1;
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, limits),
           "insufficient-input-loop-limit-refused");
    if (shape)
        shape.value().kind = static_cast<LassoKind>(255);
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, SmallLimits()),
           "invalid-terminal-kind-refused");
    shape = Polygon(square);
    if (shape)
        shape.value().mode = static_cast<SelectionMode>(255);
    Expect(shape && !LassoFinishGeometry::Plan(shape.value(), std::nullopt, SmallLimits()),
           "invalid-terminal-mode-refused");
}

struct CancelAt final
{
    CancelSource* source = nullptr;
    LassoFinishGeometry::Stage target{};
    std::uint32_t occurrence = 0;
    std::uint32_t seen = 0;
};
void CancelCheckpoint(void* context, LassoFinishGeometry::Stage stage) noexcept
{
    auto& state = *static_cast<CancelAt*>(context);
    if (stage == state.target && ++state.seen == state.occurrence)
        static_cast<void>(state.source->Cancel());
}

void CheckCancellationStages()
{
    constexpr std::array<LassoFinishGeometry::Stage, 7> stages{
        {LassoFinishGeometry::Stage::BeforeCopy, LassoFinishGeometry::Stage::ChunkCopied,
         LassoFinishGeometry::Stage::BeforeClip, LassoFinishGeometry::Stage::Clipped,
         LassoFinishGeometry::Stage::BeforeMode, LassoFinishGeometry::Stage::BeforeAdopt,
         LassoFinishGeometry::Stage::Prepared}};
    constexpr std::array<Vec2, 4> square{{{1, 1}, {30, 1}, {30, 30}, {1, 30}}};
    bool everyStageCancelled = true;
    for (const auto stage : stages)
    {
        auto shape = Polygon(square);
        CancelSource cancel;
        CancelAt state{&cancel, stage, 1, 0};
        auto limits = SmallLimits();
        LassoFinishGeometry::Observer observer{&state, &CancelCheckpoint, limits};
        auto result = shape ? PrepareBounded(shape.value(), 64, 64, std::nullopt, cancel.Token(), &observer,
                                             SharedPublicationMemory::CreateForWork(1024 * 1024))
                            : std::nullopt;
        everyStageCancelled = everyStageCancelled && !result && state.seen != 0;
    }
    Expect(everyStageCancelled, "cancellation-at-each-observable-stage-refuses-partial-result");

    std::vector<Vec2> many;
    many.reserve(130);
    for (int index = 0; index < 130; ++index)
        many.push_back({static_cast<double>(index % 50), static_cast<double>(index / 50)});
    auto shape = Polygon(many, SelectionMode::New, true, 77, 4);
    CancelSource cancel;
    CancelAt state{&cancel, LassoFinishGeometry::Stage::ChunkCopied, 2, 0};
    auto limits = SmallLimits();
    LassoFinishGeometry::Observer observer{&state, &CancelCheckpoint, limits};
    auto result = shape ? PrepareBounded(shape.value(), 64, 64, std::nullopt, cancel.Token(), &observer,
                                         SharedPublicationMemory::CreateForWork(1024 * 1024))
                        : std::nullopt;
    Expect(!result && state.seen == 2, "cancellation-inside-multiblock-copy-stops-on-second-chunk");
}

void CheckFinishOmitsPolygonCursor()
{
    auto memory = SharedPublicationMemory::CreateForWork(1024 * 1024);
    auto gesture = NewGesture(memory, 2);
    bool setup = gesture && gesture.value().BeginOwner(81, LassoKind::Polygonal, SelectionMode::New, true, {10, 10});
    if (setup)
    {
        static_cast<void>(gesture.value().ExtendOwner(gesture.value().CurrentOwner(), {50, 10}));
        static_cast<void>(gesture.value().ExtendOwner(gesture.value().CurrentOwner(), {50, 50}));
        static_cast<void>(gesture.value().CursorOwner(gesture.value().CurrentOwner(), Vec2{15, 30}));
    }
    const auto displayed = setup ? gesture.value().CaptureOwner() : BoundedLassoGesture::Display{};
    auto terminal = setup ? gesture.value().FinishOwner(gesture.value().CurrentOwner()) : std::nullopt;
    auto result = terminal ? PrepareBounded(terminal.value(), 64, 64, std::nullopt, {}, nullptr, memory) : std::nullopt;
    Expect(displayed.cursor && !terminal.value_or(BoundedLassoGesture::Display{}).cursor && result &&
               result.value().selection && std::abs(Area(result.value().selection.value()).value_or(-1) - 800.0) < 0.01,
           "polygon-rubberband-cursor-is-omitted-from-finished-outline");
}
void CheckActualMultiblockGeometry()
{
    std::vector<Vec2> perimeter;
    perimeter.reserve(132);
    for (int index = 0; index < 33; ++index)
        perimeter.push_back({10.0 + index * 1.25, 10.0});
    for (int index = 1; index <= 32; ++index)
        perimeter.push_back({50.0, 10.0 + index * 1.25});
    for (int index = 1; index <= 32; ++index)
        perimeter.push_back({50.0 - index * 1.25, 50.0});
    for (int index = 1; index <= 31; ++index)
        perimeter.push_back({10.0, 50.0 - index * 1.25});
    auto shape = Polygon(perimeter, SelectionMode::New, true, 88, 4);
    auto result = shape ? PrepareBounded(shape.value(), 64, 64, std::nullopt, {}, nullptr,
                                         SharedPublicationMemory::CreateForWork(1024 * 1024))
                        : std::nullopt;
    Expect(result && result.value().selection && shape.value().points.Count() == 128 &&
               std::abs(Area(result.value().selection.value()).value_or(-1) - 1600.0) < 0.01 &&
               Contains(result.value(), {30, 30}) && !Contains(result.value(), {5, 30}),
           "actual-multiblock-perimeter-prepares-independent-square-area");
}
void CheckMarqueePopAndEllipseTriangleCompatibility()
{
    Fixture rectangleFixture;
    bool setup =
        rectangleFixture.gesture &&
        rectangleFixture.gesture.value().BeginOwner(101, LassoKind::Rectangle, SelectionMode::New, true, {10, 10}) &&
        rectangleFixture.gesture.value().MarqueeOwner(rectangleFixture.gesture.value().CurrentOwner(), {60, 60}, false,
                                                      false) == BoundedLassoGesture::Outcome::Changed;
    const auto marqueeKey =
        rectangleFixture.gesture ? rectangleFixture.gesture.value().CurrentOwner() : BoundedLassoGesture::Key{};
    bool counts = setup && rectangleFixture.gesture.value().PointCountOwner() == 4;
    for (std::uint32_t remaining = 3; setup && remaining > 0; --remaining)
    {
        counts = counts &&
                 rectangleFixture.gesture.value().PopOwner(rectangleFixture.gesture.value().CurrentOwner()) ==
                     BoundedLassoGesture::Outcome::Changed &&
                 rectangleFixture.gesture.value().PointCountOwner() == remaining;
    }
    const bool emptyCancels =
        setup &&
        rectangleFixture.gesture.value().PopOwner(rectangleFixture.gesture.value().CurrentOwner()) ==
            BoundedLassoGesture::Outcome::Changed &&
        !rectangleFixture.gesture.value().ActiveOwner() && rectangleFixture.gesture.value().PointCountOwner() == 0;
    Expect(counts && emptyCancels &&
               rectangleFixture.gesture.value().PopOwner(marqueeKey) == BoundedLassoGesture::Outcome::Refused,
           "marquee-backspace-removes-four-corners-to-empty-terminal-cancel");

    Fixture ellipseFixture;
    setup = ellipseFixture.gesture &&
            ellipseFixture.gesture.value().BeginOwner(102, LassoKind::Ellipse, SelectionMode::New, true, {10, 10}) &&
            ellipseFixture.gesture.value().MarqueeOwner(ellipseFixture.gesture.value().CurrentOwner(), {60, 60}, false,
                                                        false) == BoundedLassoGesture::Outcome::Changed &&
            ellipseFixture.gesture.value().PopOwner(ellipseFixture.gesture.value().CurrentOwner()) ==
                BoundedLassoGesture::Outcome::Changed;
    auto triangle = setup ? ellipseFixture.gesture.value().FinishOwner(ellipseFixture.gesture.value().CurrentOwner())
                          : std::nullopt;
    auto prepared = triangle
                        ? PrepareBounded(triangle.value(), 100, 100, std::nullopt, {}, nullptr, ellipseFixture.memory)
                        : std::nullopt;
    Expect(prepared && prepared.value().kind == LassoKind::Ellipse && prepared.value().selection &&
               triangle.value().points.Count() == 3 &&
               std::abs(Area(prepared.value().selection.value()).value_or(-1) - 1250.0) < 0.01,
           "ellipse-with-one-corner-popped-finishes-legacy-three-point-triangle");
}

void CheckFirstOwnerCacheLifecycle()
{
    Fixture fixture;
    bool setup = fixture.gesture && fixture.gesture.value().FirstOwner() == std::nullopt &&
                 fixture.gesture.value().BeginOwner(103, LassoKind::Polygonal, SelectionMode::New, true, {3.25, 4.5});
    const auto opening = fixture.gesture ? fixture.gesture.value().CurrentOwner() : BoundedLassoGesture::Key{};
    const auto first = fixture.gesture ? fixture.gesture.value().FirstOwner() : std::nullopt;
    auto moved = setup ? std::optional<BoundedLassoGesture>(std::move(fixture.gesture.value())) : std::nullopt;
    const bool movedState = moved && moved.value().FirstOwner() == std::optional<Vec2>(Vec2{3.25, 4.5}) &&
                            fixture.gesture.value().FirstOwner() == std::nullopt;
    const auto movedKey = moved ? moved.value().CurrentOwner() : BoundedLassoGesture::Key{};
    if (moved)
        static_cast<void>(moved.value().ExtendOwner(movedKey, {20, 4.5}));
    const bool stableAfterAppend =
        moved && moved.value().FirstOwner() == first &&
        moved.value().ExtendOwner(opening, {30, 4.5}) == BoundedLassoGesture::Outcome::Refused &&
        moved.value().FirstOwner() == first;
    Expect(setup && first == std::optional<Vec2>(Vec2{3.25, 4.5}) && movedState && stableAfterAppend,
           "first-owner-cache-survives-move-and-stale-key-refusal");

    Fixture emptied;
    setup = emptied.gesture &&
            emptied.gesture.value().BeginOwner(104, LassoKind::Freehand, SelectionMode::New, true, {9, 8}) &&
            emptied.gesture.value().PopOwner(emptied.gesture.value().CurrentOwner()) ==
                BoundedLassoGesture::Outcome::Changed;
    Expect(setup && !emptied.gesture.value().FirstOwner() && !emptied.gesture.value().ActiveOwner(),
           "first-owner-is-empty-after-final-point-pop");
}
void CheckMemoryPlanArithmetic()
{
    constexpr std::array<Vec2, 4> square{{{0, 0}, {40, 0}, {40, 40}, {0, 40}}};
    auto shape = Polygon(square, SelectionMode::New, true, 91, 2);
    auto limits = SmallLimits();
    const auto plan = shape ? LassoFinishGeometry::Plan(shape.value(), std::nullopt, limits) : std::nullopt;
    bool arithmeticMatches = false;
    if (plan)
    {
        const std::uint64_t minimumOutput = limits.outputPoints * sizeof(Vec2) +
                                            limits.outputLoops * sizeof(SelectionLoop) +
                                            sizeof(std::vector<SelectionLoop>);
        const std::uint64_t independentPreparation = plan.value().traversalBytes + plan.value().outlineBytes +
                                                     plan.value().workspaceBytes + 2 * plan.value().outputBytes +
                                                     SharedPublicationMemory::OwnershipAllowance() +
                                                     sizeof(LassoFinishGeometry::Result) + 4096;
        arithmeticMatches = plan.value().retainedArenaBytes == shape.value().points.RetainedArenaBytes() &&
                            plan.value().traversalBytes == shape.value().points.TraversalScratchBytes() &&
                            plan.value().outputBytes >= minimumOutput &&
                            plan.value().preparationBytes == independentPreparation &&
                            plan.value().outlineBytes == 4 * sizeof(Vec2) + 1024 && plan.value().workspaceBytes != 0;
    }
    Expect(arithmeticMatches, "memory-plan-components-match-independent-envelope-sum");
}
std::optional<LassoFinishInput> CaptureInputForCurrentOpening(DocumentSession& session,
                                                              BoundedLassoGesture::Display display, bool finishAA)
{
    session.selectionAntialiased = finishAA;
    return session.PrepareLassoFinishInput(LassoFinishTerminal{std::move(display)});
}

std::optional<LassoFinishInput> CaptureSessionInput(DocumentSession& session, LassoKind kind, SelectionMode mode,
                                                    bool finishAA, std::span<const Vec2> points)
{
    session.BeginLasso(points.front(), mode);
    const auto opening = session.LassoOpeningForWork();
    session.CancelLasso();
    auto display = opening ? Polygon(points, mode, !finishAA, opening) : std::nullopt;
    if (!display)
        return std::nullopt;
    display.value().kind = kind;
    return CaptureInputForCurrentOpening(session, std::move(display.value()), finishAA);
}

std::optional<PreparedLassoFinish> PrepareCapturedInput(LassoFinishInput input, CancelToken cancellation = {},
                                                        const LassoFinishObserver* observer = nullptr)
{
    LassoFinishObserver bounded;
    bounded.geometry.limits = SmallLimits();
    return DocumentSession::PrepareLassoFinishPublication(std::move(input), cancellation, {},
                                                          observer ? observer : &bounded,
                                                          SharedPublicationMemory::CreateForWork(1024 * 1024));
}
std::optional<PreparedLassoFinish> PrepareSessionPublication(DocumentSession& session, LassoKind kind,
                                                             SelectionMode mode, bool finishAA,
                                                             std::span<const Vec2> points)
{
    auto input = CaptureSessionInput(session, kind, mode, finishAA, points);
    if (!input)
        return std::nullopt;
    LassoFinishObserver observer;
    observer.geometry.limits = SmallLimits();
    return DocumentSession::PrepareLassoFinishPublication(std::move(input.value()), {}, {}, &observer,
                                                          SharedPublicationMemory::CreateForWork(1024 * 1024));
}

bool AcquireAndApply(DocumentSession& session, PreparedLassoFinish& prepared)
{
    DocumentHistory::ResidencyPermit permit;
    auto validation = prepared.TryAcquireHistoryResidency(permit);
    if (validation == DocumentHistory::ResidencyValidation::Refresh && prepared.RefreshHistoryResidency())
        validation = prepared.TryAcquireHistoryResidency(permit);
    const bool applied = validation == DocumentHistory::ResidencyValidation::Ready &&
                         session.ApplyPreparedLassoFinishIfCurrent(prepared);
    permit.Reset();
    return applied;
}

void CheckPreparedPublicationHistoryAndAA()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset(); // CreateDocument records New Canvas; Reset establishes this fixture's saved baseline.
    Expect(session.Document() && !session.history.IsModified(), "new-document-starts-at-saved-history-marker");
    auto prepared = PrepareSessionPublication(session, LassoKind::Polygonal, SelectionMode::New, false, square);
    const auto* proposed = prepared ? prepared.value().SelectionForDisplayForWork() : nullptr;
    const bool currentAA = prepared && session.IsPreparedLassoFinishCurrent(prepared.value()) && proposed &&
                           !proposed->antialiased && proposed->loops.PointCount() == 4;
    const bool applied = prepared && AcquireAndApply(session, prepared.value());
    Expect(currentAA && applied && session.Document()->selection &&
               !session.Document()->selection.value().antialiased && session.history.UndoCount() == 1 &&
               session.history.UndoName() == L"Polygonal Lasso" && session.history.IsModified(),
           "finish-publication-uses-finish-aa-and-adds-one-named-history-entry");
    session.Undo();
    const bool undoSaved = !session.Document()->selection && !session.history.IsModified() && session.history.CanRedo();
    session.Redo();
    Expect(undoSaved && session.Document()->selection && session.history.IsModified() &&
               SelectionGeometry::Contains(session.Document()->selection.value().loops.Get(), {30, 30}),
           "finish-publication-undo-redo-restores-literal-selection-and-saved-marker");
}

void CheckNoOpRedoAndMovedInput()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto noOpFixture = std::make_unique<DocumentSession>();
    auto& noOp = *noOpFixture;
    noOp.CreateDocument(64, 64, true);
    noOp.history.Reset();
    noOp.SelectAll();
    noOp.Undo();
    const int undoCount = noOp.history.UndoCount();
    const std::wstring redoName = noOp.history.RedoName();
    auto input = CaptureSessionInput(noOp, LassoKind::Freehand, SelectionMode::Subtract, true, square);
    auto keep = input ? PrepareCapturedInput(std::move(input.value())) : std::nullopt;
    Expect(keep && !keep.value().ChangesSelection() && AcquireAndApply(noOp, keep.value()) &&
               !noOp.Document()->selection && noOp.history.CanRedo() && noOp.history.UndoCount() == undoCount &&
               noOp.history.RedoName() == redoName,
           "subtract-absent-noop-retains-redo-branch-and-history-count");

    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    auto movable = CaptureSessionInput(session, LassoKind::Freehand, SelectionMode::New, true, square);
    bool reusable = false;
    if (movable)
    {
        LassoFinishInput moved(std::move(movable.value()));
        auto reused = PrepareCapturedInput(std::move(movable.value()));
        auto valid = PrepareCapturedInput(std::move(moved));
        reusable = !reused && valid && session.history.UndoCount() == 0 && !session.Document()->selection;
    }
    Expect(reusable, "moved-from-finish-input-refuses-reuse-without-document-change");
}
void CheckFinishIntentSerialAndCancellation()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto newestFixture = std::make_unique<DocumentSession>();
    auto& newest = *newestFixture;
    newest.CreateDocument(64, 64, true);
    newest.history.Reset();
    newest.BeginLasso(square.front(), SelectionMode::New);
    const auto opening = newest.LassoOpeningForWork();
    newest.CancelLasso();
    auto firstDisplay = Polygon(square, SelectionMode::New, true, opening);
    auto secondDisplay = Polygon(square, SelectionMode::New, true, opening);
    auto firstInput =
        firstDisplay ? CaptureInputForCurrentOpening(newest, std::move(firstDisplay.value()), true) : std::nullopt;
    auto secondInput =
        secondDisplay ? CaptureInputForCurrentOpening(newest, std::move(secondDisplay.value()), true) : std::nullopt;
    auto first = firstInput ? PrepareCapturedInput(std::move(firstInput.value())) : std::nullopt;
    auto second = secondInput ? PrepareCapturedInput(std::move(secondInput.value())) : std::nullopt;
    Expect(first && second && !newest.IsPreparedLassoFinishCurrent(first.value()) &&
               newest.IsPreparedLassoFinishCurrent(second.value()) && !AcquireAndApply(newest, first.value()) &&
               AcquireAndApply(newest, second.value()) && newest.history.UndoCount() == 1,
           "same-opening-newest-terminal-intent-supersedes-older-publication");

    auto cancelledFixture = std::make_unique<DocumentSession>();
    auto& cancelled = *cancelledFixture;
    cancelled.CreateDocument(64, 64, true);
    cancelled.history.Reset();
    auto prepared = PrepareSessionPublication(cancelled, LassoKind::Freehand, SelectionMode::New, true, square);
    cancelled.CancelLasso();
    Expect(prepared && !cancelled.IsPreparedLassoFinishCurrent(prepared.value()) &&
               !AcquireAndApply(cancelled, prepared.value()) && cancelled.history.UndoCount() == 0 &&
               !cancelled.Document()->selection,
           "postcapture-cancel-refuses-finish-with-no-history-or-selection-change");

    auto stoppedFixture = std::make_unique<DocumentSession>();
    auto& stopped = *stoppedFixture;
    stopped.CreateDocument(64, 64, true);
    stopped.history.Reset();
    auto stoppedInput = CaptureSessionInput(stopped, LassoKind::Freehand, SelectionMode::New, true, square);
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    auto stoppedResult =
        stoppedInput ? PrepareCapturedInput(std::move(stoppedInput.value()), cancellation.Token()) : std::nullopt;
    Expect(stoppedInput && !stoppedResult && stopped.history.UndoCount() == 0 && !stopped.Document()->selection,
           "cancelled-worker-preparation-has-no-partial-document-publication");
}

void CheckPublicationMidCancellation()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto sessionFixture = std::make_unique<DocumentSession>();
    auto& session = *sessionFixture;
    session.CreateDocument(64, 64, true);
    session.history.Reset();
    auto input = CaptureSessionInput(session, LassoKind::Freehand, SelectionMode::New, true, square);
    CancelSource cancellation;
    CancelAt stopAtClip{&cancellation, LassoFinishGeometry::Stage::Clipped, 1, 0};
    LassoFinishObserver observer;
    observer.geometry = {&stopAtClip, &CancelCheckpoint, SmallLimits()};
    auto result =
        input ? PrepareCapturedInput(std::move(input.value()), cancellation.Token(), &observer) : std::nullopt;
    Expect(!result && stopAtClip.seen == 1 && session.history.UndoCount() == 0 && !session.Document()->selection,
           "mid-geometry-cancellation-leaves-document-and-history-unpublished");
}
void CheckFinishStaleSessionAndHistoryGates()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto reopenedFixture = std::make_unique<DocumentSession>();
    auto& reopened = *reopenedFixture;
    reopened.CreateDocument(64, 64, true);
    reopened.history.Reset();
    auto prepared = PrepareSessionPublication(reopened, LassoKind::Freehand, SelectionMode::New, true, square);
    reopened.BeginLasso({0, 0}, SelectionMode::New);
    reopened.CancelLasso();
    Expect(prepared && !reopened.IsPreparedLassoFinishCurrent(prepared.value()) &&
               !AcquireAndApply(reopened, prepared.value()) && reopened.history.UndoCount() == 0,
           "identical-outline-from-reopened-opening-is-stale");

    auto historyChangedFixture = std::make_unique<DocumentSession>();
    auto& historyChanged = *historyChangedFixture;
    historyChanged.CreateDocument(64, 64, true);
    historyChanged.history.Reset();
    auto oldHistory = PrepareSessionPublication(historyChanged, LassoKind::Freehand, SelectionMode::New, true, square);
    historyChanged.SelectAll();
    Expect(oldHistory && !historyChanged.IsPreparedLassoFinishCurrent(oldHistory.value()) &&
               !AcquireAndApply(historyChanged, oldHistory.value()) && historyChanged.history.UndoCount() == 1,
           "finish-captured-history-change-refuses-stale-publication");

    auto replacedFixture = std::make_unique<DocumentSession>();
    auto& replaced = *replacedFixture;
    replaced.CreateDocument(64, 64, true);
    replaced.history.Reset();
    auto oldDocument = PrepareSessionPublication(replaced, LassoKind::Freehand, SelectionMode::New, true, square);
    const auto generation = replaced.Generation();
    replaced.CreateDocument(64, 64, true);
    replaced.history.Reset();
    Expect(oldDocument && replaced.Generation() != generation &&
               !replaced.IsPreparedLassoFinishCurrent(oldDocument.value()) &&
               !AcquireAndApply(replaced, oldDocument.value()) && !replaced.Document()->selection,
           "same-size-new-document-generation-refuses-old-finish");

    auto sourceFixture = std::make_unique<DocumentSession>();
    auto& source = *sourceFixture;
    source.CreateDocument(64, 64, true);
    source.history.Reset();
    auto foreign = PrepareSessionPublication(source, LassoKind::Freehand, SelectionMode::New, true, square);
    auto otherFixture = std::make_unique<DocumentSession>();
    auto& other = *otherFixture;
    other.CreateDocument(64, 64, true);
    other.history.Reset();
    Expect(foreign && !other.IsPreparedLassoFinishCurrent(foreign.value()) &&
               !other.ApplyPreparedLassoFinishIfCurrent(foreign.value()) && !other.Document()->selection,
           "different-session-refuses-foreign-finish-authority");
}

void CheckActiveCropAndMaskAuthorityGates()
{
    constexpr std::array<Vec2, 4> square{{{10, 10}, {50, 10}, {50, 50}, {10, 50}}};
    auto activeFixture = std::make_unique<DocumentSession>();
    auto& active = *activeFixture;
    active.CreateDocument(64, 64, true);
    active.history.Reset();
    active.BeginLasso({0, 0}, SelectionMode::New);
    const auto opening = active.LassoOpeningForWork();
    auto display = Polygon(square, SelectionMode::New, true, opening);
    auto blocked =
        display ? active.PrepareLassoFinishInput(LassoFinishTerminal{std::move(display.value())}) : std::nullopt;
    active.CancelLasso();
    Expect(display && opening != 0 && !blocked && active.history.UndoCount() == 0,
           "finish-input-capture-refuses-while-legacy-lasso-is-active");

    auto cropFixture = std::make_unique<DocumentSession>();
    auto& crop = *cropFixture;
    crop.CreateDocument(64, 64, true);
    crop.history.Reset();
    auto cropPrepared = PrepareSessionPublication(crop, LassoKind::Freehand, SelectionMode::New, true, square);
    crop.SetCropFrame(CropRect{2, 3, 40, 41});
    Expect(cropPrepared && !crop.IsPreparedLassoFinishCurrent(cropPrepared.value()) &&
               !AcquireAndApply(crop, cropPrepared.value()) && crop.history.UndoCount() == 0,
           "crop-frame-change-invalidates-captured-finish-authority");

    auto maskFixture = std::make_unique<DocumentSession>();
    auto& mask = *maskFixture;
    mask.CreateDocument(64, 64, true);
    mask.history.Reset();
    const auto layer = mask.ActiveLayerID();
    mask.AddLayerMask();
    if (layer)
        mask.SelectLayerTarget(layer.value(), false);
    auto maskPrepared = PrepareSessionPublication(mask, LassoKind::Freehand, SelectionMode::New, true, square);
    if (layer)
        mask.SelectLayerTarget(layer.value(), true);
    const auto count = mask.history.UndoCount();
    Expect(layer && mask.IsMaskSelected() && maskPrepared && !mask.IsPreparedLassoFinishCurrent(maskPrepared.value()) &&
               !AcquireAndApply(mask, maskPrepared.value()) && mask.history.UndoCount() == count,
           "mask-target-change-invalidates-captured-finish-authority");
}
} // namespace

int RunLassoFinishGeometryTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckLegacyReplaceAddSubtract();
    CheckAbsentEmptyClipAndCapturedAA();
    CheckMarqueeAndEllipse();
    CheckDegenerateAndKeyLimits();
    CheckCancellationStages();
    CheckFinishOmitsPolygonCursor();
    CheckActualMultiblockGeometry();
    CheckMarqueePopAndEllipseTriangleCompatibility();
    CheckFirstOwnerCacheLifecycle();
    CheckMemoryPlanArithmetic();
    CheckPreparedPublicationHistoryAndAA();
    CheckNoOpRedoAndMovedInput();
    CheckFinishIntentSerialAndCancellation();
    CheckPublicationMidCancellation();
    CheckFinishStaleSessionAndHistoryGates();
    CheckActiveCropAndMaskAuthorityGates();
    std::printf("%s lasso-finish.summary assertions=%d failures=%d\n", gFailures ? "FAIL" : "PASS", gAssertions,
                gFailures);
    return gFailures ? 1 : 0;
}

#include "Document.Tests.EllipseFinishPlan.h"

#include "BoundedLassoGesture.h"
#include "LassoFinishGeometry.h"
#include "PixelBuffer.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kClipperScale = 128.0;
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s ellipse-finish-plan.%s\n", condition ? "PASS" : "FAIL", label);
}

struct PreparedEllipse final
{
    LassoFinishGeometry::MemoryPlan downPlan;
    LassoFinishGeometry::MemoryPlan terminalPlan;
    std::optional<LassoFinishGeometry::Result> result;
};

[[nodiscard]] bool AddBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (bytes > maximum - total)
        return false;
    total += bytes;
    return true;
}

[[nodiscard]] std::optional<PreparedEllipse> PrepareEllipse(
    Vec2 from, Vec2 to, int width, int height, bool square, bool finishAntialiased,
    const std::optional<DocumentSelection>& before = std::nullopt, CancelToken cancellation = {},
    const LassoFinishGeometry::Observer* observer = nullptr)
{
    const auto limits = LassoFinishGeometry::NewMarqueeLimits(LassoKind::Ellipse);
    const auto downPlan = LassoFinishGeometry::PlanNewMarquee(LassoKind::Ellipse);
    if (!limits || !downPlan)
        return std::nullopt;

    std::uint64_t admittedBytes = downPlan.value().retainedArenaBytes;
    if (!AddBytes(admittedBytes, downPlan.value().preparationBytes))
        return std::nullopt;
    const auto memory = SharedPublicationMemory::CreateForWork(admittedBytes);
    if (!memory)
        return std::nullopt;
    auto points = BoundedLassoPoints::CreateForWork(memory, 1);
    if (!points)
        return std::nullopt;
    BoundedLassoGesture gesture(std::move(points.value()));
    if (!gesture.BeginOwner(1, LassoKind::Ellipse, SelectionMode::New, finishAntialiased, from) ||
        gesture.MarqueeOwner(gesture.CurrentOwner(), to, square, false) == BoundedLassoGesture::Outcome::Refused)
        return std::nullopt;
    auto terminal = gesture.FinishOwner(gesture.CurrentOwner(), finishAntialiased);
    if (!terminal)
        return std::nullopt;
    const auto terminalPlan = LassoFinishGeometry::Plan(terminal.value(), before, limits.value());
    if (!terminalPlan)
        return std::nullopt;

    LassoFinishGeometry::Observer boundedObserver{};
    if (observer)
    {
        boundedObserver.context = observer->context;
        boundedObserver.checkpoint = observer->checkpoint;
    }
    boundedObserver.limits = limits.value();
    auto result =
        LassoFinishGeometry::Prepare(terminal.value(), width, height, before, cancellation, &boundedObserver, memory);
    return PreparedEllipse{downPlan.value(), terminalPlan.value(), std::move(result)};
}

[[nodiscard]] std::uint64_t ExpectedSegments(SelectionBounds box) noexcept
{
    const double a = box.Width() / 2.0;
    const double b = box.Height() / 2.0;
    const double perimeter = kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
    const double estimate = std::ceil(perimeter / 2.0);
    const double limited = std::isfinite(estimate) ? std::clamp(estimate, 32.0, 2048.0) : 2048.0;
    return (static_cast<std::uint64_t>(limited) + 3u) / 4u * 4u;
}

// Independent serial numeric reference: generate the sampled ellipse from its bounds, snap each
// coordinate to ClipperD precision2's 1/128 grid (nearest integer at scale 128), then measure its
// polygon area and clip that convex polygon against the canvas, snapping each independently
// computed edge intersection to the same grid. This models the documented numeric precision without
// calling production geometry or Clipper. The legacy oracle
// marqueeEllipseSelectsAnOvalInItsBoxAndShiftMakesACircle fixes box bounds, center/corner coverage,
// and equal circle dimensions; this reference adds bounded area/clip checks without calling production geometry.
[[nodiscard]] double QuantizeForClipper(double coordinate) noexcept
{
    return std::round(coordinate * kClipperScale) / kClipperScale;
}

[[nodiscard]] std::vector<Vec2> ReferenceEllipse(SelectionBounds box)
{
    const double a = box.Width() / 2.0;
    const double b = box.Height() / 2.0;
    const double centerX = box.minX + a;
    const double centerY = box.minY + b;
    const std::uint64_t count = ExpectedSegments(box);
    std::vector<Vec2> points;
    points.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index)
    {
        const double angle = 2.0 * kPi * static_cast<double>(index) / static_cast<double>(count);
        points.push_back(
            {QuantizeForClipper(centerX + a * std::cos(angle)), QuantizeForClipper(centerY + b * std::sin(angle))});
    }
    return points;
}

template <typename Inside, typename Intersect>
[[nodiscard]] std::vector<Vec2> ClipBoundary(const std::vector<Vec2>& input, Inside inside, Intersect intersect)
{
    std::vector<Vec2> output;
    if (input.empty())
        return output;
    Vec2 previous = input.back();
    bool previousInside = inside(previous);
    for (const Vec2 point : input)
    {
        const bool pointInside = inside(point);
        if (pointInside)
        {
            if (!previousInside)
                output.push_back(intersect(previous, point));
            output.push_back(point);
        }
        else if (previousInside)
            output.push_back(intersect(previous, point));
        previous = point;
        previousInside = pointInside;
    }
    return output;
}

[[nodiscard]] std::vector<Vec2> ClipReference(std::vector<Vec2> points, int width, int height)
{
    const auto vertical = [](Vec2 first, Vec2 second, double x)
    {
        const double ratio = (x - first.x) / (second.x - first.x);
        return Vec2{QuantizeForClipper(x), QuantizeForClipper(first.y + ratio * (second.y - first.y))};
    };
    const auto horizontal = [](Vec2 first, Vec2 second, double y)
    {
        const double ratio = (y - first.y) / (second.y - first.y);
        return Vec2{QuantizeForClipper(first.x + ratio * (second.x - first.x)), QuantizeForClipper(y)};
    };
    points = ClipBoundary(
        points, [](Vec2 point) { return point.x >= 0.0; },
        [&](Vec2 first, Vec2 second) { return vertical(first, second, 0.0); });
    points = ClipBoundary(
        points, [=](Vec2 point) { return point.x <= static_cast<double>(width); },
        [&](Vec2 first, Vec2 second) { return vertical(first, second, static_cast<double>(width)); });
    points = ClipBoundary(
        points, [](Vec2 point) { return point.y >= 0.0; },
        [&](Vec2 first, Vec2 second) { return horizontal(first, second, 0.0); });
    return ClipBoundary(
        points, [=](Vec2 point) { return point.y <= static_cast<double>(height); },
        [&](Vec2 first, Vec2 second) { return horizontal(first, second, static_cast<double>(height)); });
}

[[nodiscard]] double PolygonArea(const std::vector<Vec2>& points) noexcept
{
    if (points.size() < 3)
        return 0.0;
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const Vec2 first = points[index];
        const Vec2 second = points[(index + 1u) % points.size()];
        twiceArea += first.x * second.y - second.x * first.y;
    }
    return std::abs(twiceArea) * 0.5;
}

// Both the serial reference and production-area reducer sum the same grid-snapped vertices. Permit
// only a conservative IEEE-754 accumulation bound: each shoelace term uses two products and a
// subtraction, followed by at most n additions and the final half-scale. The bound scales with the
// actual coordinate magnitude and vertex count; it does not absorb geometric/quantization error.
[[nodiscard]] double AreaRoundoffBound(const std::vector<Vec2>& points) noexcept
{
    double maxCoordinate = 1.0;
    for (const Vec2 point : points)
        maxCoordinate = (std::max)(maxCoordinate, (std::max)(std::abs(point.x), std::abs(point.y)));
    const double operations = 32.0 * static_cast<double>(points.size()) + 64.0;
    return std::numeric_limits<double>::epsilon() * operations * maxCoordinate * maxCoordinate;
}

[[nodiscard]] bool AreaMatchesReference(double actual, const std::vector<Vec2>& expected) noexcept
{
    return std::abs(actual - PolygonArea(expected)) <= AreaRoundoffBound(expected);
}

[[nodiscard]] std::optional<double> SelectionArea(const DocumentSelection& selection) noexcept
{
    double twiceArea = 0.0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.size() < 3)
            continue;
        for (std::size_t index = 0; index < loop.size(); ++index)
        {
            const Vec2 first = loop[index];
            const Vec2 second = loop[(index + 1u) % loop.size()];
            twiceArea += first.x * second.y - second.x * first.y;
        }
    }
    const double area = std::abs(twiceArea) * 0.5;
    return std::isfinite(area) ? std::optional(area) : std::nullopt;
}

[[nodiscard]] std::optional<SelectionBounds> PolygonBounds(const std::vector<Vec2>& points) noexcept
{
    if (points.empty())
        return std::nullopt;
    SelectionBounds bounds{points.front().x, points.front().y, points.front().x, points.front().y};
    for (const Vec2 point : points)
    {
        bounds.minX = (std::min)(bounds.minX, point.x);
        bounds.minY = (std::min)(bounds.minY, point.y);
        bounds.maxX = (std::max)(bounds.maxX, point.x);
        bounds.maxY = (std::max)(bounds.maxY, point.y);
    }
    return bounds;
}

[[nodiscard]] std::optional<SelectionBounds> SelectionBoundsOf(const DocumentSelection& selection) noexcept
{
    const auto& loops = selection.loops.Get();
    if (loops.empty() || loops.front().empty())
        return std::nullopt;
    SelectionBounds bounds{loops.front().front().x, loops.front().front().y, loops.front().front().x,
                           loops.front().front().y};
    for (const auto& loop : loops)
        for (const Vec2 point : loop)
        {
            bounds.minX = (std::min)(bounds.minX, point.x);
            bounds.minY = (std::min)(bounds.minY, point.y);
            bounds.maxX = (std::max)(bounds.maxX, point.x);
            bounds.maxY = (std::max)(bounds.maxY, point.y);
        }
    return bounds;
}

[[nodiscard]] bool ContainsConvex(const std::vector<Vec2>& polygon, Vec2 point) noexcept
{
    if (polygon.size() < 3)
        return false;
    bool positive = false;
    bool negative = false;
    for (std::size_t index = 0; index < polygon.size(); ++index)
    {
        const Vec2 first = polygon[index];
        const Vec2 second = polygon[(index + 1u) % polygon.size()];
        const double cross = (second.x - first.x) * (point.y - first.y) - (second.y - first.y) * (point.x - first.x);
        positive = positive || cross > 1e-9;
        negative = negative || cross < -1e-9;
        if (positive && negative)
            return false;
    }
    return true;
}

[[nodiscard]] bool ContainsConvexSelection(const DocumentSelection& selection, Vec2 point) noexcept
{
    const auto& loops = selection.loops.Get();
    return loops.size() == 1 && ContainsConvex(loops.front(), point);
}

[[nodiscard]] bool SamePlan(const LassoFinishGeometry::MemoryPlan& first,
                            const LassoFinishGeometry::MemoryPlan& second) noexcept
{
    return first.retainedArenaBytes == second.retainedArenaBytes && first.traversalBytes == second.traversalBytes &&
           first.outlineBytes == second.outlineBytes && first.workspaceBytes == second.workspaceBytes &&
           first.outputBytes == second.outputBytes && first.preparationBytes == second.preparationBytes;
}

void CheckLimitsAndAdmissionPlan()
{
    const auto ellipseLimits = LassoFinishGeometry::NewMarqueeLimits(LassoKind::Ellipse);
    const auto ellipsePlan = LassoFinishGeometry::PlanNewMarquee(LassoKind::Ellipse);
    const auto rectangleLimits = LassoFinishGeometry::NewMarqueeLimits(LassoKind::Rectangle);
    const auto rectanglePlan = LassoFinishGeometry::PlanNewMarquee(LassoKind::Rectangle);
    Expect(ellipseLimits && ellipseLimits.value().workspaceBytes == 4u * 1024u * 1024u &&
               ellipseLimits.value().inputPoints == 2052 && ellipseLimits.value().inputLoops == 2 &&
               ellipseLimits.value().outputPoints == 4096 && ellipseLimits.value().outputLoops == 16,
           "ellipse-limits-match-fixed-workspace-and-geometry-ceilings");

    const std::uint64_t ellipseOutline = 2048u * sizeof(Vec2) + 1024u;
    const std::uint64_t ellipseOutput = 4096u * sizeof(Vec2) + 16u * sizeof(SelectionLoop) +
                                        SelectionLoops::WorkOwnershipAllowance(16) + sizeof(std::vector<SelectionLoop>);
    const std::uint64_t ellipsePreparation = ellipseOutline + PixelBuffer::RequiredBytes(4u * 1024u * 1024u) +
                                             2u * ellipseOutput + SharedPublicationMemory::OwnershipAllowance() +
                                             sizeof(LassoFinishGeometry::Result) + 4096u;
    Expect(ellipsePlan && ellipsePlan.value().retainedArenaBytes == BoundedLassoPoints::MemoryBytes(1) &&
               ellipsePlan.value().traversalBytes == 0 && ellipsePlan.value().outlineBytes == ellipseOutline &&
               ellipsePlan.value().workspaceBytes == PixelBuffer::RequiredBytes(4u * 1024u * 1024u) &&
               ellipsePlan.value().outputBytes == ellipseOutput &&
               ellipsePlan.value().preparationBytes == ellipsePreparation,
           "ellipse-down-plan-is-one-arena-no-traversal-and-exact-envelope");

    auto basicEllipse = PrepareEllipse({10, 20}, {70, 60}, 100, 100, false, true);
    Expect(basicEllipse && SamePlan(basicEllipse.value().downPlan, basicEllipse.value().terminalPlan),
           "ellipse-down-admission-plan-equals-four-corner-terminal-plan");
    Expect(rectangleLimits && rectangleLimits.value().workspaceBytes == 64u * 1024u &&
               rectangleLimits.value().inputPoints == 8 && rectangleLimits.value().inputLoops == 2 &&
               rectangleLimits.value().outputPoints == 16 && rectangleLimits.value().outputLoops == 4,
           "rectangle-limits-retain-existing-small-plan");
    Expect(rectanglePlan && rectanglePlan.value().retainedArenaBytes == BoundedLassoPoints::MemoryBytes(1) &&
               rectanglePlan.value().traversalBytes == 0 &&
               rectanglePlan.value().workspaceBytes == PixelBuffer::RequiredBytes(64u * 1024u),
           "rectangle-plan-retains-one-block-and-existing-workspace");
}

void CheckUnsupportedKindsRefuse()
{
    Expect(!LassoFinishGeometry::NewMarqueeLimits(LassoKind::Freehand) &&
               !LassoFinishGeometry::PlanNewMarquee(LassoKind::Freehand),
           "freehand-has-no-new-marquee-limits-or-plan");
    Expect(!LassoFinishGeometry::NewMarqueeLimits(LassoKind::Polygonal) &&
               !LassoFinishGeometry::PlanNewMarquee(LassoKind::Polygonal),
           "polygonal-has-no-new-marquee-limits-or-plan");
    constexpr auto invalidKind = static_cast<LassoKind>(255);
    Expect(!LassoFinishGeometry::NewMarqueeLimits(invalidKind) && !LassoFinishGeometry::PlanNewMarquee(invalidKind),
           "invalid-kind-has-no-new-marquee-limits-or-plan");
}

void CheckMinimumEllipseAndLegacyGolden()
{
    auto minimum = PrepareEllipse({4, 4}, {5, 5}, 16, 16, false, true);
    const auto minimumExpected = ReferenceEllipse({4, 4, 5, 5});
    const auto minimumShape = minimum && minimum.value().result && minimum.value().result.value().selection
                                  ? SelectionArea(minimum.value().result.value().selection.value())
                                  : std::nullopt;
    Expect(minimum && minimum.value().result && minimum.value().result.value().selection && minimumShape &&
               minimum.value().result.value().selection.value().loops.PointCount() == 32 &&
               AreaMatchesReference(minimumShape.value(), minimumExpected),
           "minimum-diameter-ellipse-uses-independent-32-vertex-area-reference");

    auto legacy = PrepareEllipse({10, 20}, {70, 60}, 100, 100, false, true);
    const auto legacyExpected = ReferenceEllipse({10, 20, 70, 60});
    const auto legacyBounds = legacy && legacy.value().result && legacy.value().result.value().selection
                                  ? SelectionBoundsOf(legacy.value().result.value().selection.value())
                                  : std::nullopt;
    const auto legacyArea = legacy && legacy.value().result && legacy.value().result.value().selection
                                ? SelectionArea(legacy.value().result.value().selection.value())
                                : std::nullopt;
    Expect(legacy && legacy.value().result && legacy.value().result.value().selection && legacyBounds && legacyArea &&
               std::abs(legacyBounds.value().minX - 10.0) < 0.5 && std::abs(legacyBounds.value().maxX - 70.0) < 0.5 &&
               std::abs(legacyBounds.value().minY - 20.0) < 0.5 && std::abs(legacyBounds.value().maxY - 60.0) < 0.5 &&
               AreaMatchesReference(legacyArea.value(), legacyExpected),
           "legacy-ellipse-marquee-bounds-and-independent-serial-area");
    // Port the actual legacy byte goldens as well as the independent polygon reference.
    const auto coverage =
        legacy && legacy.value().result && legacy.value().result.value().selection
            ? SelectionGeometry::Coverage(legacy.value().result.value().selection.value().loops.Get(), 100, 100, true)
            : Raster{};
    std::uint8_t centerByte = 0, cornerByte = 255;
    if (coverage.HasStorage())
    {
        coverage.ReadRegion(40, 40, 1, 1, &centerByte, 1, 0);
        coverage.ReadRegion(11, 21, 1, 1, &cornerByte, 1, 255);
    }
    Expect(coverage.HasStorage() && centerByte == 255 && cornerByte == 0 && legacy && legacy.value().result &&
               legacy.value().result.value().selection &&
               ContainsConvexSelection(legacy.value().result.value().selection.value(), {40, 40}) &&
               !ContainsConvexSelection(legacy.value().result.value().selection.value(), {11, 21}),
           "legacy-ellipse-center-covered-and-box-corner-clear");

    auto minimumCircle = PrepareEllipse({5, 5}, {6, 6}, 16, 16, true, true);
    const auto minimumCircleArea =
        minimumCircle && minimumCircle.value().result && minimumCircle.value().result.value().selection
            ? SelectionArea(minimumCircle.value().result.value().selection.value())
            : std::nullopt;
    Expect(minimumCircle && minimumCircle.value().result && minimumCircle.value().result.value().selection &&
               minimumCircle.value().result.value().selection.value().loops.PointCount() == 32 && minimumCircleArea &&
               AreaMatchesReference(minimumCircleArea.value(), ReferenceEllipse({5, 5, 6, 6})),
           "minimum-shift-circle-uses-independent-32-vertex-area-reference");

    auto circle = PrepareEllipse({5, 5}, {45, 25}, 64, 64, true, true);
    const auto circleBounds = circle && circle.value().result && circle.value().result.value().selection
                                  ? SelectionBoundsOf(circle.value().result.value().selection.value())
                                  : std::nullopt;
    Expect(circle && circle.value().result && circle.value().result.value().selection && circleBounds &&
               std::abs((circleBounds.value().maxX - circleBounds.value().minX) -
                        (circleBounds.value().maxY - circleBounds.value().minY)) < 0.5,
           "legacy-shift-marquee-ellipse-produces-circle-bounds");
}

void CheckReverseClipAndMaximumEllipse()
{
    auto reversed = PrepareEllipse({60, 70}, {20, 30}, 100, 100, false, false);
    const auto reverseExpected = ReferenceEllipse({20, 30, 60, 70});
    const auto reverseBounds = reversed && reversed.value().result && reversed.value().result.value().selection
                                   ? SelectionBoundsOf(reversed.value().result.value().selection.value())
                                   : std::nullopt;
    const auto reverseArea = reversed && reversed.value().result && reversed.value().result.value().selection
                                 ? SelectionArea(reversed.value().result.value().selection.value())
                                 : std::nullopt;
    Expect(reversed && reversed.value().result && reversed.value().result.value().selection && reverseBounds &&
               reverseArea && reverseBounds.value().minX == 20 && reverseBounds.value().minY == 30 &&
               reverseBounds.value().maxX == 60 && reverseBounds.value().maxY == 70 &&
               AreaMatchesReference(reverseArea.value(), reverseExpected) &&
               !reversed.value().result.value().selection.value().antialiased,
           "reverse-drag-retains-bounds-and-event-time-aa");

    auto clipped = PrepareEllipse({-10, 10}, {10, 30}, 64, 64, false, true);
    const auto clippedExpected = ClipReference(ReferenceEllipse({-10, 10, 10, 30}), 64, 64);
    const auto clippedBounds = clipped && clipped.value().result && clipped.value().result.value().selection
                                   ? SelectionBoundsOf(clipped.value().result.value().selection.value())
                                   : std::nullopt;
    const auto clippedArea = clipped && clipped.value().result && clipped.value().result.value().selection
                                 ? SelectionArea(clipped.value().result.value().selection.value())
                                 : std::nullopt;
    const auto clippedReferenceBounds = PolygonBounds(clippedExpected);
    Expect(clipped && clipped.value().result && clipped.value().result.value().selection && clippedBounds &&
               clippedArea && clippedReferenceBounds &&
               std::abs(clippedBounds.value().minX - clippedReferenceBounds.value().minX) < 0.01 &&
               std::abs(clippedBounds.value().minY - clippedReferenceBounds.value().minY) < 0.01 &&
               std::abs(clippedBounds.value().maxX - clippedReferenceBounds.value().maxX) < 0.01 &&
               std::abs(clippedBounds.value().maxY - clippedReferenceBounds.value().maxY) < 0.01 &&
               AreaMatchesReference(clippedArea.value(), clippedExpected),
           "negative-origin-ellipse-clips-to-independent-convex-reference");

    auto maximum = PrepareEllipse({1000, 1000}, {15000, 15000}, 16000, 16000, false, true);
    const auto maximumExpected = ReferenceEllipse({1000, 1000, 15000, 15000});
    const auto maximumArea = maximum && maximum.value().result && maximum.value().result.value().selection
                                 ? SelectionArea(maximum.value().result.value().selection.value())
                                 : std::nullopt;
    const auto maximumBounds = maximum && maximum.value().result && maximum.value().result.value().selection
                                   ? SelectionBoundsOf(maximum.value().result.value().selection.value())
                                   : std::nullopt;
    Expect(maximum && maximum.value().result && maximum.value().result.value().selection && maximumArea &&
               maximumBounds && maximumExpected.size() == 2048 &&
               maximum.value().result.value().selection.value().loops.PointCount() == 2048 &&
               AreaMatchesReference(maximumArea.value(), maximumExpected) &&
               std::abs(maximumBounds.value().minX - 1000.0) < 0.01 &&
               std::abs(maximumBounds.value().maxX - 15000.0) < 0.01 &&
               std::abs(maximumBounds.value().minY - 1000.0) < 0.01 &&
               std::abs(maximumBounds.value().maxY - 15000.0) < 0.01,
           "maximum-2048-vertex-ellipse-matches-independent-area-and-bounds");
}

void CheckNewModeStateAndCancellation()
{
    auto zeroArea = PrepareEllipse({20, 20}, {20, 20}, 64, 64, false, true);
    Expect(zeroArea && zeroArea.value().result &&
               zeroArea.value().result.value().disposition == LassoFinishGeometry::Disposition::Clear &&
               !zeroArea.value().result.value().selection,
           "zero-area-new-ellipse-clears-selection");

    auto outside = PrepareEllipse({-30, -30}, {-10, -10}, 64, 64, false, false);
    Expect(outside && outside.value().result &&
               outside.value().result.value().disposition == LassoFinishGeometry::Disposition::Replace &&
               outside.value().result.value().selection &&
               outside.value().result.value().selection.value().loops.empty() &&
               !outside.value().result.value().selection.value().antialiased,
           "outside-ellipse-new-publishes-present-empty-with-captured-aa");

    std::vector<SelectionLoop> priorLoops;
    priorLoops.reserve(17);
    for (int index = 0; index < 17; ++index)
    {
        const double offset = static_cast<double>(index * 2);
        priorLoops.push_back({{offset, 0}, {offset + 1, 0}, {offset + 1, 1}, {offset, 1}});
    }
    auto priorStorage = SelectionLoops::AdoptForWork(std::move(priorLoops));
    std::optional<DocumentSelection> largePrior;
    if (priorStorage)
        largePrior.emplace(DocumentSelection{false, std::move(priorStorage.value())});
    auto replacesPrior =
        largePrior ? PrepareEllipse({10, 10}, {30, 30}, 64, 64, false, true, largePrior.value()) : std::nullopt;
    Expect(largePrior && largePrior.value().loops.size() > 16 && replacesPrior && replacesPrior.value().result &&
               replacesPrior.value().result.value().selection &&
               replacesPrior.value().result.value().selection.value().loops.size() == 1 &&
               replacesPrior.value().result.value().selection.value().antialiased,
           "new-ellipse-replacement-ignores-prior-over-ellipse-clip-loop-ceiling");

    struct CancelState final
    {
        CancelSource* source = nullptr;
        std::uint32_t beforeClip = 0;
        std::uint32_t clipped = 0;
    };
    const auto cancelBeforeClip = [](void* context, LassoFinishGeometry::Stage stage) noexcept
    {
        auto& state = *static_cast<CancelState*>(context);
        if (stage == LassoFinishGeometry::Stage::BeforeClip)
        {
            ++state.beforeClip;
            static_cast<void>(state.source->Cancel());
        }
        if (stage == LassoFinishGeometry::Stage::Clipped)
            ++state.clipped;
    };
    CancelSource cancellation;
    CancelState cancelState{&cancellation};
    LassoFinishGeometry::Observer observer{&cancelState, cancelBeforeClip, {}};
    auto cancelled =
        PrepareEllipse({10, 10}, {40, 30}, 64, 64, false, true, std::nullopt, cancellation.Token(), &observer);
    Expect(cancelled && !cancelled.value().result && cancelState.beforeClip == 1 && cancelState.clipped == 0,
           "cancellation-at-before-clip-refuses-unpublished-result");
}
} // namespace

int RunEllipseFinishPlanTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckLimitsAndAdmissionPlan();
    CheckUnsupportedKindsRefuse();
    CheckMinimumEllipseAndLegacyGolden();
    CheckReverseClipAndMaximumEllipse();
    CheckNewModeStateAndCancellation();
    std::printf("SUMMARY ellipse-finish-plan checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

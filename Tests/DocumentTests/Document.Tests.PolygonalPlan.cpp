#include "Document.Tests.PolygonalPlan.h"

#include "BoundedLassoGesture.h"
#include "LassoFinishGeometry.h"
#include "PixelBuffer.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label, const char* caseName = nullptr) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    if (caseName)
        std::printf("%s polygonal-plan.%s.%s\n", condition ? "PASS" : "FAIL", label, caseName);
    else
        std::printf("%s polygonal-plan.%s\n", condition ? "PASS" : "FAIL", label);
}

struct LimitsCase final
{
    const char* name;
    SelectionMode mode;
    std::uint64_t sourcePoints;
    std::uint64_t sourceLoops;
    std::uint64_t inputPoints;
    std::uint64_t inputLoops;
    std::uint64_t outputPoints;
    std::uint64_t outputLoops;
    std::uint64_t workspaceBytes;
    std::uint64_t plannedWorkspaceBytes;
    std::uint64_t maximumOutputBytes;
};

[[nodiscard]] bool LimitsMatch(const BoundedSelectionBoolean::Limits& actual, const LimitsCase& expected) noexcept
{
    return actual.inputPoints == expected.inputPoints && actual.inputLoops == expected.inputLoops &&
           actual.outputPoints == expected.outputPoints && actual.outputLoops == expected.outputLoops &&
           actual.workspaceBytes == expected.workspaceBytes &&
           actual.allocations == (std::numeric_limits<std::size_t>::max)();
}

[[nodiscard]] bool PlanWithinIndependentBounds(const LassoFinishGeometry::MemoryPlan& plan,
                                               const LimitsCase& expected) noexcept
{
    constexpr std::uint64_t oneMiB = 1024ULL * 1024;
    constexpr std::uint64_t threeGiB = 3ULL * 1024 * 1024 * 1024;
    const auto outputFloor = expected.outputPoints * sizeof(Vec2) + expected.outputLoops * sizeof(SelectionLoop) +
                             sizeof(std::vector<SelectionLoop>);
    const auto completeFloor = plan.traversalBytes + plan.outlineBytes + plan.workspaceBytes + 2 * plan.outputBytes;
    return plan.retainedArenaBytes == BoundedLassoPoints::MemoryBytes(LassoFinishGeometry::kPolygonalArenaBlocks) &&
           plan.retainedArenaBytes > oneMiB && plan.retainedArenaBytes < 2 * oneMiB && plan.traversalBytes == 4092 &&
           plan.outlineBytes == 1'049'600 && plan.workspaceBytes == expected.plannedWorkspaceBytes &&
           plan.outputBytes >= outputFloor && plan.outputBytes < expected.maximumOutputBytes &&
           plan.preparationBytes >= completeFloor && plan.preparationBytes < threeGiB;
}

void CheckLiteralLimitsAndPlans()
{
    static_assert(LassoFinishGeometry::kPolygonalMaximumPoints == 65'536);
    static_assert(LassoFinishGeometry::kPolygonalArenaBlocks == 1023);
    static_assert(sizeof(Vec2) == 16);

    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t smallOutputCeiling = 32ULL * 1024 * 1024;
    constexpr std::uint64_t largeOutputCeiling = 1024ULL * 1024 * 1024;
    constexpr std::uint64_t newWorkspace = 32ULL * 1024 * 1024;
    constexpr std::uint64_t preservingWorkspace = 128ULL * 1024 * 1024;

    // Literal policy values are independently tabulated; this suite never derives Polygonal
    // expectations by calling the Freehand API.
    constexpr LimitsCase cases[]{
        {"new-empty-source", SelectionMode::New, 0, 0, 65'540, 2, 262'160, 65'552, newWorkspace, newWorkspace + 64,
         smallOutputCeiling},
        {"new-uint64-max-old-counts", SelectionMode::New, maximum, maximum, 65'540, 2, 262'160, 65'552, newWorkspace,
         newWorkspace + 64, smallOutputCeiling},
        {"add-empty-source", SelectionMode::Add, 0, 0, 262'160, 65'553, 524'336, 65'568, preservingWorkspace,
         preservingWorkspace + 64, smallOutputCeiling},
        {"add-four-points-one-loop", SelectionMode::Add, 4, 1, 262'164, 65'554, 524'368, 65'573, preservingWorkspace,
         preservingWorkspace + 64, smallOutputCeiling},
        {"add-eight-points-two-loops", SelectionMode::Add, 8, 2, 262'168, 65'555, 524'400, 65'578, preservingWorkspace,
         preservingWorkspace + 64, smallOutputCeiling},
        {"add-large-source-clamps-output", SelectionMode::Add, 8'000'000, 100, 8'262'160, 65'653, 8'000'000, 2'666'667,
         preservingWorkspace, preservingWorkspace + 64, largeOutputCeiling},
        {"subtract-empty-source", SelectionMode::Subtract, 0, 0, 262'160, 65'553, 524'336, 65'568, preservingWorkspace,
         preservingWorkspace + 64, smallOutputCeiling},
        {"subtract-four-points-one-loop", SelectionMode::Subtract, 4, 1, 262'164, 65'554, 524'368, 65'573,
         preservingWorkspace, preservingWorkspace + 64, smallOutputCeiling},
        {"subtract-eight-points-two-loops", SelectionMode::Subtract, 8, 2, 262'168, 65'555, 524'400, 65'578,
         preservingWorkspace, preservingWorkspace + 64, smallOutputCeiling},
        {"subtract-large-source-clamps-output", SelectionMode::Subtract, 8'000'000, 100, 8'262'160, 65'653, 8'000'000,
         2'666'667, preservingWorkspace, preservingWorkspace + 64, largeOutputCeiling},
    };

    for (const auto& test : cases)
    {
        const auto limits = LassoFinishGeometry::PolygonalLimits(test.mode, test.sourcePoints, test.sourceLoops);
        const auto plan = LassoFinishGeometry::PlanPolygonal(test.mode, test.sourcePoints, test.sourceLoops);
        Expect(limits && LimitsMatch(limits.value(), test), "literal-mode-source-resource-limits", test.name);
        Expect(plan && PlanWithinIndependentBounds(plan.value(), test), "full-arena-workspace-and-output-bounds",
               test.name);
    }
}

void CheckInvalidAndOverflowRefusal()
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    struct RefusalCase final
    {
        const char* name;
        SelectionMode mode;
        std::uint64_t sourcePoints;
        std::uint64_t sourceLoops;
    };
    constexpr RefusalCase cases[]{
        {"invalid-mode", static_cast<SelectionMode>(255), 0, 0},
        {"add-point-limit-plus-one", SelectionMode::Add, 8'000'001, 1},
        {"subtract-loop-limit", SelectionMode::Subtract, 0, 16'000'004},
        {"add-uint64-point-overflow", SelectionMode::Add, maximum, 0},
        {"subtract-uint64-loop-overflow", SelectionMode::Subtract, 0, maximum},
    };
    for (const auto& test : cases)
        Expect(!LassoFinishGeometry::PolygonalLimits(test.mode, test.sourcePoints, test.sourceLoops) &&
                   !LassoFinishGeometry::PlanPolygonal(test.mode, test.sourcePoints, test.sourceLoops),
               "limits-and-plan-refuse-invalid-or-overflowing-counts", test.name);
}

void CheckPolygonalDrawingAdmissionStillRefuses()
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::array modes{SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract};
    for (const auto mode : modes)
        Expect(!LassoFinishGeometry::PlanDrawing(LassoKind::Polygonal, mode, maximum, maximum),
               "drag-up-finish-plan-refuses-separate-polygonal-press-protocol",
               mode == SelectionMode::New   ? "new"
               : mode == SelectionMode::Add ? "add"
                                            : "subtract");
}

struct PreparedPolygon final
{
    std::shared_ptr<SharedPublicationMemory> memory;
    BoundedLassoGesture::Display terminal;
    std::optional<LassoFinishGeometry::Result> result;
};

[[nodiscard]] std::optional<PreparedPolygon> PreparePolygonal(std::span<const Vec2> vertices, SelectionMode mode,
                                                              std::optional<Vec2> cursor)
{
    if (vertices.empty())
        return std::nullopt;
    const auto limits = LassoFinishGeometry::PolygonalLimits(mode);
    const auto plan = LassoFinishGeometry::PlanPolygonal(mode);
    if (!limits || !plan ||
        plan.value().preparationBytes > (std::numeric_limits<std::uint64_t>::max)() - plan.value().retainedArenaBytes)
        return std::nullopt;
    auto memory =
        SharedPublicationMemory::CreateForWork(plan.value().retainedArenaBytes + plan.value().preparationBytes);
    if (!memory)
        return std::nullopt;
    auto points = BoundedLassoPoints::CreateForWork(memory, LassoFinishGeometry::kPolygonalArenaBlocks);
    if (!points)
        return std::nullopt;

    BoundedLassoGesture gesture(std::move(points.value()));
    if (!gesture.BeginOwner(71, LassoKind::Polygonal, mode, false, vertices.front()))
        return std::nullopt;
    auto key = gesture.CurrentOwner();
    for (const auto point : vertices.subspan(1))
    {
        if (gesture.ExtendOwner(key, point) != BoundedLassoGesture::Outcome::Changed)
            return std::nullopt;
        key = gesture.CurrentOwner();
    }
    if (cursor && gesture.CursorOwner(key, cursor) != BoundedLassoGesture::Outcome::Changed)
        return std::nullopt;
    auto terminal = gesture.FinishOwner(gesture.CurrentOwner());
    if (!terminal)
        return std::nullopt;

    const LassoFinishGeometry::Observer observer{nullptr, nullptr, limits.value()};
    auto result = LassoFinishGeometry::Prepare(terminal.value(), 12, 10, std::nullopt, {}, &observer, memory);
    return PreparedPolygon{std::move(memory), std::move(terminal.value()), std::move(result)};
}

[[nodiscard]] bool MatchesLiteralCoverage(const LassoFinishGeometry::Result& result,
                                          const std::array<std::string_view, 10>& rows)
{
    if (result.disposition != LassoFinishGeometry::Disposition::Replace || !result.selection ||
        result.selection.value().antialiased)
        return false;
    auto coverage = SelectionGeometry::Coverage(result.selection.value().loops.Get(), 12, 10, false);
    if (coverage.Width() != 12 || coverage.Height() != 10 || coverage.Format() != RasterFormat::Gray8)
        return false;
    for (std::size_t y = 0; y < rows.size(); ++y)
    {
        if (rows[y].size() != 12)
            return false;
        for (std::size_t x = 0; x < rows[y].size(); ++x)
        {
            const auto offset = y * coverage.Stride() + x;
            if (coverage.Data()[offset] != (rows[y][x] == '#' ? 255 : 0))
                return false;
        }
    }
    return true;
}

void CheckTriangleAndSelfIntersectionWorkerPreparation()
{
    constexpr std::array<Vec2, 3> triangle{{{1, 1}, {9, 1}, {1, 8}}};
    constexpr std::array<std::string_view, 10> trianglePixels{
        "............", ".#######....", ".######.....", ".#####......", ".####.......",
        ".###........", ".##.........", ".#..........", "............", "............"};
    const auto preparedTriangle = PreparePolygonal(std::span<const Vec2>(triangle), SelectionMode::New, Vec2{11, 9});
    Expect(preparedTriangle && preparedTriangle.value().result &&
               preparedTriangle.value().terminal.kind == LassoKind::Polygonal &&
               preparedTriangle.value().terminal.points.Count() == 3 && !preparedTriangle.value().terminal.cursor &&
               preparedTriangle.value().result.value().kind == LassoKind::Polygonal &&
               preparedTriangle.value().result.value().selection.has_value(),
           "three-point-polygonal-terminal-prepares-on-bounded-arena");
    Expect(preparedTriangle && preparedTriangle.value().result &&
               MatchesLiteralCoverage(preparedTriangle.value().result.value(), trianglePixels),
           "triangle-matches-28-independent-literal-pixel-centers");

    constexpr std::array<Vec2, 4> bowtie{{{1, 1}, {9, 8}, {1, 8}, {9, 1}}};
    constexpr std::array<std::string_view, 10> bowtiePixels{
        "............", "..######....", "...####.....", "....##......", "............",
        "....##......", "...####.....", "..######....", "............", "............"};
    const auto preparedBowtie = PreparePolygonal(std::span<const Vec2>(bowtie), SelectionMode::New, Vec2{11, 9});
    Expect(preparedBowtie && preparedBowtie.value().result && preparedBowtie.value().terminal.points.Count() == 4 &&
               preparedBowtie.value().result.value().kind == LassoKind::Polygonal &&
               preparedBowtie.value().result.value().selection.has_value(),
           "self-intersecting-polygonal-loop-prepares-without-partial-result");
    Expect(preparedBowtie && preparedBowtie.value().result &&
               MatchesLiteralCoverage(preparedBowtie.value().result.value(), bowtiePixels),
           "bowtie-matches-24-independent-literal-pixel-centers");
}

void CheckShortWorkerPreparation()
{
    constexpr std::array<Vec2, 2> shortPath{{{2, 2}, {7, 2}}};
    constexpr std::array modes{SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract};
    for (const auto mode : modes)
    {
        const auto prepared = PreparePolygonal(std::span<const Vec2>(shortPath), mode, Vec2{10, 9});
        const auto expected = mode == SelectionMode::New ? LassoFinishGeometry::Disposition::Clear
                                                         : LassoFinishGeometry::Disposition::Keep;
        Expect(prepared && prepared.value().result && prepared.value().terminal.points.Count() == 2 &&
                   !prepared.value().terminal.cursor && prepared.value().result.value().kind == LassoKind::Polygonal &&
                   prepared.value().result.value().disposition == expected &&
                   !prepared.value().result.value().selection.has_value(),
               "short-polygonal-worker-follows-mode-terminal-policy",
               mode == SelectionMode::New   ? "new"
               : mode == SelectionMode::Add ? "add"
                                            : "subtract");
    }
}
} // namespace

int RunPolygonalPlanTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckLiteralLimitsAndPlans();
    CheckInvalidAndOverflowRefusal();
    CheckPolygonalDrawingAdmissionStillRefuses();
    CheckTriangleAndSelfIntersectionWorkerPreparation();
    CheckShortWorkerPreparation();
    constexpr int expected = 35;
    std::printf("SUMMARY polygonal-plan assertions=%d failures=%d expected=%d\n", gAssertions, gFailures, expected);
    return gFailures == 0 && gAssertions == expected ? 0 : (gFailures ? gFailures : 1);
}

#include "Document.Tests.MarqueeModePlan.h"

#include "LassoFinishGeometry.h"

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label, const char* caseName = nullptr) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    if (caseName)
        std::printf("%s marquee-mode-plan.%s.%s\n", condition ? "PASS" : "FAIL", label, caseName);
    else
        std::printf("%s marquee-mode-plan.%s\n", condition ? "PASS" : "FAIL", label);
}

[[nodiscard]] bool SameLimits(const BoundedSelectionBoolean::Limits& first,
                              const BoundedSelectionBoolean::Limits& second) noexcept
{
    return first.workspaceBytes == second.workspaceBytes && first.allocations == second.allocations &&
           first.inputPoints == second.inputPoints && first.inputLoops == second.inputLoops &&
           first.outputPoints == second.outputPoints && first.outputLoops == second.outputLoops;
}

[[nodiscard]] bool SamePlan(const LassoFinishGeometry::MemoryPlan& first,
                            const LassoFinishGeometry::MemoryPlan& second) noexcept
{
    return first.retainedArenaBytes == second.retainedArenaBytes && first.traversalBytes == second.traversalBytes &&
           first.outlineBytes == second.outlineBytes && first.workspaceBytes == second.workspaceBytes &&
           first.outputBytes == second.outputBytes && first.preparationBytes == second.preparationBytes;
}

void CheckNewCompatibility()
{
    constexpr std::uint64_t arbitraryPoints = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t arbitraryLoops = arbitraryPoints - 1;
    const auto rectangleLimits =
        LassoFinishGeometry::MarqueeLimits(LassoKind::Rectangle, SelectionMode::New, arbitraryPoints, arbitraryLoops);
    const auto oldRectangleLimits = LassoFinishGeometry::NewMarqueeLimits(LassoKind::Rectangle);
    Expect(rectangleLimits && oldRectangleLimits && SameLimits(rectangleLimits.value(), oldRectangleLimits.value()),
           "rectangle-new-limits-exactly-preserve-legacy-fields");

    const auto rectanglePlan =
        LassoFinishGeometry::PlanMarquee(LassoKind::Rectangle, SelectionMode::New, arbitraryPoints, arbitraryLoops);
    const auto oldRectanglePlan = LassoFinishGeometry::PlanNewMarquee(LassoKind::Rectangle);
    Expect(rectanglePlan && oldRectanglePlan && SamePlan(rectanglePlan.value(), oldRectanglePlan.value()),
           "rectangle-new-plan-exactly-preserves-legacy-fields");

    const auto ellipseLimits =
        LassoFinishGeometry::MarqueeLimits(LassoKind::Ellipse, SelectionMode::New, arbitraryPoints, arbitraryLoops);
    const auto oldEllipseLimits = LassoFinishGeometry::NewMarqueeLimits(LassoKind::Ellipse);
    Expect(ellipseLimits && oldEllipseLimits && SameLimits(ellipseLimits.value(), oldEllipseLimits.value()),
           "ellipse-new-limits-exactly-preserve-legacy-fields");

    const auto ellipsePlan =
        LassoFinishGeometry::PlanMarquee(LassoKind::Ellipse, SelectionMode::New, arbitraryPoints, arbitraryLoops);
    const auto oldEllipsePlan = LassoFinishGeometry::PlanNewMarquee(LassoKind::Ellipse);
    Expect(ellipsePlan && oldEllipsePlan && SamePlan(ellipsePlan.value(), oldEllipsePlan.value()),
           "ellipse-new-plan-exactly-preserves-legacy-fields");
}

struct ExpectedPreservingLimits final
{
    const char* caseName;
    LassoKind kind;
    std::uint64_t sourcePoints;
    std::uint64_t sourceLoops;
    std::uint64_t inputPoints;
    std::uint64_t inputLoops;
    std::uint64_t outputPoints;
    std::uint64_t outputLoops;
    std::uint64_t workspaceBytes;
};

void CheckPreservingScalarCases()
{
    constexpr ExpectedPreservingLimits cases[]{
        {"zero-source-counts", LassoKind::Rectangle, 0, 0, 16, 5, 48, 20, 75'008},
        {"four-points-one-loop", LassoKind::Rectangle, 4, 1, 20, 6, 80, 25, 77'312},
        {"eight-points-two-loops", LassoKind::Rectangle, 8, 2, 24, 7, 112, 30, 79'616},
        {"zero-source-counts", LassoKind::Ellipse, 0, 0, 4096, 17, 8208, 32, 6'295'808},
        {"four-points-one-loop", LassoKind::Ellipse, 4, 1, 4100, 18, 8240, 37, 6'298'112},
        {"eight-points-two-loops", LassoKind::Ellipse, 8, 2, 4104, 19, 8272, 42, 6'300'416},
    };
    constexpr SelectionMode modes[]{SelectionMode::Add, SelectionMode::Subtract};
    for (const auto& expected : cases)
    {
        for (const SelectionMode mode : modes)
        {
            const auto limits =
                LassoFinishGeometry::MarqueeLimits(expected.kind, mode, expected.sourcePoints, expected.sourceLoops);
            const auto plan =
                LassoFinishGeometry::PlanMarquee(expected.kind, mode, expected.sourcePoints, expected.sourceLoops);
            const bool limitsMatch = limits && limits.value().inputPoints == expected.inputPoints &&
                                     limits.value().inputLoops == expected.inputLoops &&
                                     limits.value().outputPoints == expected.outputPoints &&
                                     limits.value().outputLoops == expected.outputLoops &&
                                     limits.value().workspaceBytes == expected.workspaceBytes;
            Expect(limitsMatch,
                   expected.kind == LassoKind::Rectangle
                       ? (mode == SelectionMode::Add ? "rectangle-add-tabulated-limits"
                                                     : "rectangle-subtract-tabulated-limits")
                       : (mode == SelectionMode::Add ? "ellipse-add-tabulated-limits"
                                                     : "ellipse-subtract-tabulated-limits"),
                   expected.caseName);
            constexpr std::uint64_t planCeiling = 16ULL * 1024ULL * 1024ULL;
            const bool completePlanUnderCeiling =
                plan && plan.value().retainedArenaBytes < planCeiling &&
                plan.value().traversalBytes < planCeiling - plan.value().retainedArenaBytes &&
                plan.value().preparationBytes <
                    planCeiling - plan.value().retainedArenaBytes - plan.value().traversalBytes;
            Expect(completePlanUnderCeiling,
                   expected.kind == LassoKind::Rectangle
                       ? (mode == SelectionMode::Add ? "rectangle-add-complete-plan-under-16mib"
                                                     : "rectangle-subtract-complete-plan-under-16mib")
                       : (mode == SelectionMode::Add ? "ellipse-add-complete-plan-under-16mib"
                                                     : "ellipse-subtract-complete-plan-under-16mib"),
                   expected.caseName);
        }
    }
}

void CheckLargeCountsClampToAbsoluteCaps()
{
    constexpr std::uint64_t largePoints = 8'000'000;
    constexpr std::uint64_t largeLoops = 100;
    constexpr std::uint64_t workspaceCap = 128ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t inputPointCap = 16'000'004;
    constexpr std::uint64_t outputPointCap = 8'000'000;
    constexpr std::uint64_t outputLoopCap = 2'666'667;
    constexpr LassoKind kinds[]{LassoKind::Rectangle, LassoKind::Ellipse};
    constexpr SelectionMode modes[]{SelectionMode::Add, SelectionMode::Subtract};
    for (const auto kind : kinds)
    {
        for (const auto mode : modes)
        {
            const auto limits = LassoFinishGeometry::MarqueeLimits(kind, mode, largePoints, largeLoops);
            const std::uint64_t expectedInputPoints = largePoints + (kind == LassoKind::Rectangle ? 16ULL : 4096ULL);
            const std::uint64_t expectedInputLoops = largeLoops + (kind == LassoKind::Rectangle ? 4ULL : 16ULL) + 1;
            Expect(limits && limits.value().workspaceBytes == workspaceCap &&
                       limits.value().inputPoints == expectedInputPoints &&
                       limits.value().inputPoints <= inputPointCap && limits.value().inputLoops == expectedInputLoops &&
                       limits.value().inputLoops <= inputPointCap && limits.value().outputPoints == outputPointCap &&
                       limits.value().outputLoops == outputLoopCap,
                   kind == LassoKind::Rectangle
                       ? (mode == SelectionMode::Add ? "rectangle-add-huge-counts-clamp-to-caps"
                                                     : "rectangle-subtract-huge-counts-clamp-to-caps")
                       : (mode == SelectionMode::Add ? "ellipse-add-huge-counts-clamp-to-caps"
                                                     : "ellipse-subtract-huge-counts-clamp-to-caps"));
        }
    }
}

void CheckUnsupportedAndInvalidInputs()
{
    constexpr LassoKind unsupportedKinds[]{LassoKind::Freehand, LassoKind::Polygonal, static_cast<LassoKind>(255)};
    constexpr SelectionMode supportedModes[]{SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract};
    for (const LassoKind kind : unsupportedKinds)
    {
        for (const SelectionMode mode : supportedModes)
        {
            const auto limits = LassoFinishGeometry::MarqueeLimits(kind, mode);
            const auto plan = LassoFinishGeometry::PlanMarquee(kind, mode);
            const char* label =
                kind == LassoKind::Freehand    ? (mode == SelectionMode::New   ? "freehand-new-refuses"
                                                  : mode == SelectionMode::Add ? "freehand-add-refuses"
                                                                               : "freehand-subtract-refuses")
                : kind == LassoKind::Polygonal ? (mode == SelectionMode::New   ? "polygonal-new-refuses"
                                                  : mode == SelectionMode::Add ? "polygonal-add-refuses"
                                                                               : "polygonal-subtract-refuses")
                                               : (mode == SelectionMode::New   ? "invalid-kind-new-refuses"
                                                  : mode == SelectionMode::Add ? "invalid-kind-add-refuses"
                                                                               : "invalid-kind-subtract-refuses");
            Expect(!limits && !plan, label);
        }
    }

    constexpr SelectionMode invalidMode = static_cast<SelectionMode>(255);
    Expect(!LassoFinishGeometry::MarqueeLimits(LassoKind::Rectangle, invalidMode) &&
               !LassoFinishGeometry::PlanMarquee(LassoKind::Rectangle, invalidMode),
           "invalid-rectangle-mode-refuses-limits-and-plan");
    Expect(!LassoFinishGeometry::MarqueeLimits(LassoKind::Ellipse, invalidMode) &&
               !LassoFinishGeometry::PlanMarquee(LassoKind::Ellipse, invalidMode),
           "invalid-ellipse-mode-refuses-limits-and-plan");
    Expect(!LassoFinishGeometry::MarqueeLimits(LassoKind::Rectangle, SelectionMode::Add,
                                               (std::numeric_limits<std::uint64_t>::max)(), 0) &&
               !LassoFinishGeometry::PlanMarquee(LassoKind::Rectangle, SelectionMode::Add,
                                                 (std::numeric_limits<std::uint64_t>::max)(), 0),
           "overflowing-preserving-point-count-refuses");
    Expect(!LassoFinishGeometry::MarqueeLimits(LassoKind::Rectangle, SelectionMode::Subtract, 0,
                                               (std::numeric_limits<std::uint64_t>::max)()) &&
               !LassoFinishGeometry::PlanMarquee(LassoKind::Rectangle, SelectionMode::Subtract, 0,
                                                 (std::numeric_limits<std::uint64_t>::max)()),
           "overflowing-preserving-loop-count-refuses");
}
} // namespace

int RunMarqueeModePlanTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckNewCompatibility();
    CheckPreservingScalarCases();
    CheckLargeCountsClampToAbsoluteCaps();
    CheckUnsupportedAndInvalidInputs();
    std::printf("SUMMARY marquee-mode-plan checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

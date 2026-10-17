#include "Document.Tests.RectangleFinishPlan.h"

#include "BoundedLassoGesture.h"
#include "LassoFinishGeometry.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace
{
int gChecks = 0, gFailures = 0;
void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    gFailures += condition ? 0 : 1;
    std::printf("%s rectangle-finish-plan.%s\n", condition ? "PASS" : "FAIL", name);
}
struct Probe final
{
    unsigned entered = 0, clipped = 0, adopted = 0;
    static void Observe(void* context, LassoFinishGeometry::Stage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(context);
        self.entered += stage == LassoFinishGeometry::Stage::BeforeCopy;
        self.clipped += stage == LassoFinishGeometry::Stage::Clipped;
        self.adopted += stage == LassoFinishGeometry::Stage::Prepared;
    }
};
std::optional<BoundedLassoGesture::Display> Rectangle(Vec2 first, Vec2 last, bool square = false, bool center = false,
                                                      SelectionMode mode = SelectionMode::New)
{
    auto memory = SharedPublicationMemory::CreateForWork(1024 * 1024);
    auto points = BoundedLassoPoints::CreateForWork(memory, 1);
    if (!points)
        return std::nullopt;
    BoundedLassoGesture writer(std::move(*points));
    if (!writer.BeginOwner(17, LassoKind::Rectangle, mode, false, first) ||
        writer.MarqueeOwner(writer.CurrentOwner(), last, square, center) == BoundedLassoGesture::Outcome::Refused)
        return std::nullopt;
    return writer.FinishOwner(writer.CurrentOwner(), true);
}
std::optional<LassoFinishGeometry::Result> Prepare(const BoundedLassoGesture::Display& terminal,
                                                   const std::optional<DocumentSelection>& before = std::nullopt,
                                                   Probe* probe = nullptr, CancelToken stopped = {}, int side = 100)
{
    LassoFinishGeometry::Observer observer;
    observer.limits = LassoFinishGeometry::NewRectangleLimits();
    observer.context = probe;
    observer.checkpoint = probe ? &Probe::Observe : nullptr;
    return LassoFinishGeometry::Prepare(terminal, side, side, before, stopped, &observer,
                                        SharedPublicationMemory::CreateForWork(1024 * 1024));
}
bool Box(const LassoFinishGeometry::Result& result, double x0, double y0, double x1, double y1) noexcept
{
    if (result.disposition != LassoFinishGeometry::Disposition::Replace || !result.selection)
        return false;
    const auto bounds = result.selection->Bounds();
    if (!bounds || bounds->minX != x0 || bounds->minY != y0 || bounds->maxX != x1 || bounds->maxY != y1 ||
        result.selection->loops.size() != 1 || result.selection->loops.PointCount() != 4)
        return false;
    const auto& loop = result.selection->loops.Get().front();
    double twiceArea = 0;
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        const auto& a = loop[i];
        const auto& b = loop[(i + 1) % loop.size()];
        twiceArea += a.x * b.y - b.x * a.y;
    }
    return std::abs(twiceArea) == 2 * (x1 - x0) * (y1 - y0);
}
void NumericCases()
{
    struct Case final
    {
        Vec2 first, last;
        bool square, center;
        double x0, y0, x1, y1;
        const char* name;
    };
    constexpr std::array cases{
        Case{{10, 20}, {60, 70}, false, false, 10, 20, 60, 70, "forward-literal-four-corners-area2500"},
        Case{{60.4, 70.6}, {20.2, 30.3}, false, false, 20, 30, 60, 71, "reverse-rounding-area1640"},
        Case{{20, 30}, {40, 35}, true, false, 20, 30, 40, 50, "shift-square-area400"},
        Case{{20, 30}, {40, 35}, false, true, 0, 25, 40, 35, "alt-center-area400"},
        Case{{50, 50}, {60, 55}, true, true, 40, 40, 60, 60, "shift-alt-centered-square-area400"},
        Case{{-20.5, -30.5}, {50, 60}, false, false, 0, 0, 50, 60, "negative-anchor-canvas-clip-area3000"},
        Case{{-1000000, -1000000},
             {1000000, 1000000},
             false,
             false,
             0,
             0,
             100,
             100,
             "large-finite-surrounds-canvas-area10000"},
        Case{{90, 80}, {120, 130}, false, false, 90, 80, 100, 100, "right-bottom-canvas-clip-area200"},
        Case{{90, 10}, {20, 80}, false, false, 20, 10, 90, 80, "reverse-x-area4900"},
        Case{{10, 90}, {80, 20}, false, false, 10, 20, 80, 90, "reverse-y-area4900"},
    };
    for (const auto& c : cases)
    {
        auto terminal = Rectangle(c.first, c.last, c.square, c.center);
        Probe probe;
        auto result = terminal ? Prepare(*terminal, std::nullopt, &probe) : std::nullopt;
        Expect(result && Box(*result, c.x0, c.y0, c.x1, c.y1) && result->selection->antialiased && probe.entered == 1 &&
                   probe.clipped == 1 && probe.adopted == 1,
               c.name);
    }
}
void PlanningAndSemanticCases()
{
    const auto limits = LassoFinishGeometry::NewRectangleLimits();
    const auto admitted = LassoFinishGeometry::PlanNewRectangle();
    Expect(admitted && limits.workspaceBytes == 65536 && limits.inputPoints == 8 && limits.inputLoops == 2 &&
               limits.outputPoints == 16 && limits.outputLoops == 4 && admitted->traversalBytes == 0 &&
               admitted->retainedArenaBytes == BoundedLassoPoints::MemoryBytes(1) &&
               admitted->workspaceBytes >= 65536 && admitted->preparationBytes < 100 * 1024,
           "down-time-geometry-envelope-below100KiB-excludes-source-history-host-display");
    auto terminal = Rectangle({10, 20}, {60, 70});
    const auto actual = terminal ? LassoFinishGeometry::Plan(*terminal, std::nullopt, limits) : std::nullopt;
    Expect(admitted && actual && actual->preparationBytes == admitted->preparationBytes &&
               actual->retainedArenaBytes == admitted->retainedArenaBytes && !actual->traversalBytes,
           "terminal-uses-original-fixed-rectangle-admission");
    std::vector<SelectionLoop> many;
    many.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
        const double x = i;
        many.push_back({{x, 0}, {x + 0.5, 0}, {x + 0.5, 1}, {x, 1}});
    }
    auto adopted = SelectionLoops::AdoptForWork(std::move(many));
    std::optional<DocumentSelection> before;
    if (adopted)
        before.emplace(false, std::move(*adopted));
    const auto largePlan = terminal ? LassoFinishGeometry::Plan(*terminal, before, limits) : std::nullopt;
    auto replaced = terminal ? Prepare(*terminal, before) : std::nullopt;
    Expect(before && before->loops.PointCount() == 400 && largePlan && admitted &&
               largePlan->preparationBytes == admitted->preparationBytes && replaced &&
               Box(*replaced, 10, 20, 60, 70) && before->loops.PointCount() == 400 && before->loops.size() == 100,
           "new-replaces-large-prior-without-clipping-or-mutating-prior-geometry");
    auto add = Rectangle({10, 20}, {60, 70}, false, false, SelectionMode::Add);
    auto subtract = Rectangle({10, 20}, {60, 70}, false, false, SelectionMode::Subtract);
    Expect(add && subtract && before && !LassoFinishGeometry::Plan(*add, before, limits) &&
               !LassoFinishGeometry::Plan(*subtract, before, limits),
           "small-envelope-refuses-large-prior-for-add-and-subtract");
    const std::optional<DocumentSelection> empty{DocumentSelection{false, SelectionLoops{}}};
    auto withEmpty = terminal ? Prepare(*terminal, empty) : std::nullopt;
    Expect(withEmpty && Box(*withEmpty, 10, 20, 60, 70) && empty->loops.empty() && !empty->antialiased,
           "new-present-empty-prior-replaces-without-changing-source");
    auto outside = Rectangle({110, 120}, {130, 140});
    auto clipped = outside ? Prepare(*outside) : std::nullopt;
    Expect(clipped && clipped->disposition == LassoFinishGeometry::Disposition::Replace && clipped->selection &&
               clipped->selection->loops.empty() && clipped->selection->antialiased,
           "nondegenerate-outside-is-present-empty-not-clear");
    auto click = Rectangle({20, 30}, {20, 30});
    auto cleared = click ? Prepare(*click, before) : std::nullopt;
    Expect(cleared && cleared->disposition == LassoFinishGeometry::Disposition::Clear && !cleared->selection,
           "degenerate-new-clears-old-selection");
    CancelSource cancellation;
    cancellation.Cancel();
    Probe stopped;
    auto cancelled = terminal ? Prepare(*terminal, before, &stopped, cancellation.Token()) : std::nullopt;
    Expect(!cancelled && stopped.entered == 1 && stopped.clipped == 0 && stopped.adopted == 0,
           "cancelled-first-checkpoint-does-not-clip-or-publish");
    Probe invalid;
    auto invalidCanvas = terminal ? Prepare(*terminal, before, &invalid, {}, 0) : std::nullopt;
    Expect(!invalidCanvas && invalid.entered == 0 && invalid.clipped == 0 && invalid.adopted == 0,
           "invalid-canvas-refuses-before-work");
}
} // namespace
int RunRectangleFinishPlanTests()
{
    gChecks = gFailures = 0;
    NumericCases();
    PlanningAndSemanticCases();
    std::printf("SUMMARY rectangle-finish-plan checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

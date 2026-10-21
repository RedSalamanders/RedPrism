#include "Document.Tests.WandBooleanGeometry.h"
#include "BoundedClipper/BoundedSelectionBoolean.h"
#include "Document.Tests.WandBooleanCompatibility.h"
#include "Document.Tests.WandBooleanLoad.h"
#include "Document.Tests.WandBooleanTestCases.h"
#include "Document.Tests.WandBooleanWorkspace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <span>
#include <stop_token>

namespace
{
using namespace WandBooleanTestCasesR1;
using Operation = BoundedSelectionBoolean::Operation;
using Loops = std::vector<SelectionLoop>;

Loops Rectangle(Rect rect)
{
    return {{{rect.x, rect.y},
             {rect.x + rect.width, rect.y},
             {rect.x + rect.width, rect.y + rect.height},
             {rect.x, rect.y + rect.height}}};
}

double Area(const Loops& loops)
{
    double twice = 0;
    for (const auto& loop : loops)
        for (std::size_t index = 0; index < loop.size(); ++index)
        {
            const auto& a = loop[index];
            const auto& b = loop[(index + 1) % loop.size()];
            twice += a.x * b.y - b.x * a.y;
        }
    return std::abs(twice) / 2;
}

// Independent NonZero membership at pixel centers, not an implementation boolean oracle.
bool Contains(const Loops& loops, int x, int y)
{
    const double px = x + 0.5, py = y + 0.5;
    int winding = 0;
    for (const auto& loop : loops)
        for (std::size_t index = 0; index < loop.size(); ++index)
        {
            const auto& a = loop[index];
            const auto& b = loop[(index + 1) % loop.size()];
            const double cross = (b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y);
            if (a.y <= py && b.y > py && cross > 0)
                ++winding;
            if (a.y > py && b.y <= py && cross < 0)
                --winding;
        }
    return winding != 0;
}

bool Check(const Loops& loops, double area, std::span<const PixelProbe> probes)
{
    if (Area(loops) != area)
        return false;
    for (const auto& probe : probes)
        if ((Contains(loops, probe.x, probe.y) ? 255 : 0) != probe.expectedCoverage)
            return false;
    return true;
}

bool Grid(const Loops& loops, int width, int height, std::span<const int> selected)
{
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (Contains(loops, x, y) != (std::find(selected.begin(), selected.end(), y * width + x) != selected.end()))
                return false;
    return true;
}

int failures = 0;
void Record(bool pass, const char* name)
{
    std::printf("%s private-wand.%s\n", pass ? "PASS" : "FAIL", name);
    failures += pass ? 0 : 1;
}

void Geometry(const Loops& subject, const Loops& clip, Operation operation, double area,
              std::span<const PixelProbe> probes, const char* name)
{
    auto result = BoundedSelectionBoolean::Run(subject, clip, operation, {});
    Record(result && Check(*result, area, probes), name);
}

template <class Test> bool Refuses(Test test)
{
    try
    {
        test();
    }
    catch (const std::bad_alloc&)
    {
        return true;
    }
    return false;
}

void CancelAtCheckpoint(void* context, std::uint64_t) noexcept
{
    (void)static_cast<std::stop_source*>(context)->request_stop();
}
} // namespace

int RunDocumentWandGeometryTests()
{
    failures = 0;
    Geometry(Rectangle(kSharedEdgeA), Rectangle(kSharedEdgeB), Operation::Union, kSharedEdgeUnionArea,
             kSharedEdgeProbes, "analytical.shared-edge-union");
    Geometry(Rectangle(kOverlapA), Rectangle(kOverlapB), Operation::Intersect, kOverlapArea, kIntersectionProbes,
             "analytical.overlap-intersection");
    Geometry(Rectangle(kOverlapA), Rectangle(kOverlapB), Operation::Union, kUnionArea, kOverlapUnionProbes,
             "analytical.overlap-union");
    Geometry(Rectangle(kOverlapA), Rectangle(kOverlapB), Operation::Subtract, kSubtractArea, kOverlapSubtractProbes,
             "analytical.overlap-subtract");
    auto added =
        BoundedSelectionBoolean::Run(Rectangle(kLegacyReplace), Rectangle(kLegacyAddCornerTouch), Operation::Union, {});
    Record(added && Check(*added, kLegacyAfterAddArea, kLegacyAddProbes), "legacy.selection-add-32-41");
    auto subtracted =
        added ? BoundedSelectionBoolean::Run(*added, Rectangle(kLegacySubtractContained), Operation::Subtract, {})
              : std::nullopt;
    Record(subtracted && Check(*subtracted, kLegacyAfterSubtractArea, kLegacySubtractProbes),
           "legacy.selection-subtract-32-41");

    const Loops ring = {{{0, 0}, {3, 0}, {3, 3}, {0, 3}}, {{1, 1}, {1, 2}, {2, 2}, {2, 1}}};
    auto hole = BoundedSelectionBoolean::Run(ring, Rectangle({-1, -1, 5, 5}), Operation::Intersect, {});
    Record(hole && Check(*hole, kWindingHoleArea, kWindingHoleProbes) && Grid(*hole, 3, 3, kWindingHoleSelectedIndices),
           "analytical.opposite-winding-hole");
    auto fractional = BoundedSelectionBoolean::Run(Rectangle(kAnalyticalFractionalRect), {}, Operation::Union, {});
    Record(fractional && Area(*fractional) == kAnalyticalFractionalArea &&
               Grid(*fractional, 4, 3, kAnalyticalFractionalSelectedIndices),
           "analytical.fractional-pixel-centers");

    auto rectangle = Rectangle(kOverlapA);
    BoundedSelectionBoolean::Limits limits;
    limits.workspaceBytes = 1;
    Record(Refuses([&] { (void)BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, {}, limits); }),
           "limits.scratch-exhaustion");
    limits = {};
    limits.outputPoints = 3;
    Record(Refuses([&] { (void)BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, {}, limits); }),
           "limits.output-points");
    limits = {};
    limits.outputLoops = 1;
    Record(Refuses(
               [&]
               {
                   (void)BoundedSelectionBoolean::Run(Rectangle(kLegacyReplace), Rectangle(kLegacyAddCornerTouch),
                                                      Operation::Union, {}, limits);
               }),
           "limits.output-loops");
    limits = {};
    limits.inputPoints = 3;
    Record(!BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, {}, limits), "limits.input-points");
    auto invalid = rectangle;
    invalid.front().front().x = std::numeric_limits<double>::quiet_NaN();
    Record(!BoundedSelectionBoolean::Run(invalid, {}, Operation::Union, {}), "input.nan-refusal");
    std::stop_source stopped;
    (void)stopped.request_stop();
    BoundedSelectionBoolean::Statistics statistics;
    Record(!BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, CancelToken{stopped.get_token()}, {},
                                         &statistics) &&
               statistics.allocations == 0,
           "cancellation.before-workspace");
    std::stop_source during;
    Record(Refuses(
               [&]
               {
                   (void)BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, CancelToken{during.get_token()},
                                                      {}, nullptr, CancelAtCheckpoint, &during);
               }) &&
               during.stop_requested(),
           "cancellation.engine-checkpoint");
    // A successful fresh scope following all refusals proves TLS/arena cleanup is reusable.
    auto after = BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, {});
    Record(after && Area(*after) == 100 && rectangle == Rectangle(kOverlapA), "ownership.reuse-and-input-preservation");
    unsigned refused = 0, succeeded = 0;
    bool capacitiesSafe = true;
    for (std::size_t capacity = 64; capacity <= 16384; capacity += 16)
    {
        limits = {};
        limits.workspaceBytes = capacity;
        try
        {
            auto result = BoundedSelectionBoolean::Run(rectangle, {}, Operation::Union, {}, limits, &statistics);
            ++succeeded;
            capacitiesSafe = capacitiesSafe && result && Area(*result) == 100 &&
                             statistics.workspaceCapacity == capacity && statistics.workspaceConsumed <= capacity;
        }
        catch (const std::bad_alloc&)
        {
            ++refused;
        }
        capacitiesSafe = capacitiesSafe && rectangle == Rectangle(kOverlapA);
    }
    Record(capacitiesSafe && refused > 0 && succeeded > 0 && refused + succeeded == 1021,
           "ownership.1021-capacity-refusal-and-success-sweep");
    return failures;
}

int RunDocumentWandProbeSmokeTests()
{
    const std::vector<SelectionLoop> subject = {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}};
    const std::vector<SelectionLoop> clip = {{{1, 0}, {3, 0}, {3, 2}, {1, 2}}};
    const struct Case
    {
        BoundedSelectionBoolean::Operation operation;
        double expected;
        const char* name;
    } cases[] = {{BoundedSelectionBoolean::Operation::Intersect, 2, "intersect"},
                 {BoundedSelectionBoolean::Operation::Union, 6, "union"},
                 {BoundedSelectionBoolean::Operation::Subtract, 2, "subtract"}};
    int smokeFailures = 0;
    try
    {
        for (const auto& item : cases)
        {
            BoundedSelectionBoolean::Statistics statistics;
            auto result = BoundedSelectionBoolean::Run(subject, clip, item.operation, {}, {}, &statistics);
            const bool pass = result && Area(*result) == item.expected && statistics.allocations &&
                              statistics.workspaceConsumed <= statistics.workspaceCapacity;
            std::printf("%s private-wand.%s area=%.1f scratch=%zu allocations=%zu\n", pass ? "PASS" : "FAIL", item.name,
                        result ? Area(*result) : 0.0, statistics.workspaceConsumed, statistics.allocations);
            smokeFailures += pass ? 0 : 1;
        }
    }
    catch (const std::bad_alloc&)
    {
        std::puts("FAIL private-wand.unexpected-refusal");
        ++smokeFailures;
    }
    return smokeFailures;
}

int RunDocumentWandBooleanTests()
{
    int totalFailures = 0;
    totalFailures += RunDocumentWandWorkspaceTests();
    totalFailures += RunDocumentWandGeometryTests();
    totalFailures += RunDocumentWandCompatibilityTests();
    totalFailures += RunDocumentWandLoadTests();
    totalFailures += RunDocumentWandProbeSmokeTests();
    std::printf("document-wand-arena.summary failures=%d\n", totalFailures);
    return totalFailures;
}

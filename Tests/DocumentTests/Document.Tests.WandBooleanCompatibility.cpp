#include "Document.Tests.WandBooleanCompatibility.h"
#include "BoundedClipper/BoundedSelectionBoolean.h"
#pragma warning(push)
// The unchanged upstream header retains an exception-disabled precision clamp after DoError throws.
#pragma warning(disable : 4702)
#include "clipper2/clipper.engine.h"
#pragma warning(pop)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <new>
#include <utility>
#include <vector>

namespace
{
using Loops = std::vector<SelectionLoop>;
using Point = std::pair<double, double>;
using LoopKey = std::vector<Point>;
using Key = std::vector<LoopKey>;

struct StopPoint
{
    std::stop_source source;
    std::uint64_t at;
    std::uint64_t observed = 0;
    std::chrono::steady_clock::time_point requested;
};
void StopAt(void* context, std::uint64_t work) noexcept
{
    auto& point = *static_cast<StopPoint*>(context);
    if (work == point.at)
    {
        point.observed = work;
        point.requested = std::chrono::steady_clock::now();
        (void)point.source.request_stop();
    }
}

Key Canonical(const Loops& loops)
{
    Key key;
    for (const auto& loop : loops)
    {
        LoopKey vertices;
        for (const auto& point : loop)
            vertices.emplace_back(point.x, point.y);
        LoopKey best = vertices;
        for (std::size_t first = 1; first < vertices.size(); ++first)
        {
            LoopKey rotated;
            for (std::size_t index = 0; index < vertices.size(); ++index)
                rotated.push_back(vertices[(first + index) % vertices.size()]);
            if (rotated < best)
                best = std::move(rotated);
        }
        key.push_back(std::move(best));
    }
    std::sort(key.begin(), key.end());
    return key;
}

Clipper2Lib::PathsD OriginalInput(const Loops& loops)
{
    Clipper2Lib::PathsD paths;
    for (const auto& loop : loops)
    {
        Clipper2Lib::PathD path;
        for (const auto& point : loop)
            path.emplace_back(point.x, point.y);
        paths.push_back(std::move(path));
    }
    return paths;
}

Loops Original(const Loops& subject, const Loops& clip, BoundedSelectionBoolean::Operation operation)
{
    Clipper2Lib::ClipperD engine(2);
    engine.AddSubject(OriginalInput(subject));
    engine.AddClip(OriginalInput(clip));
    const auto kind = operation == BoundedSelectionBoolean::Operation::Intersect ? Clipper2Lib::ClipType::Intersection
                      : operation == BoundedSelectionBoolean::Operation::Union   ? Clipper2Lib::ClipType::Union
                                                                                 : Clipper2Lib::ClipType::Difference;
    Clipper2Lib::PathsD paths;
    if (!engine.Execute(kind, Clipper2Lib::FillRule::NonZero, paths))
        return {};
    Loops result;
    for (const auto& path : paths)
    {
        SelectionLoop loop;
        for (const auto& point : path)
            loop.push_back({point.x, point.y});
        result.push_back(std::move(loop));
    }
    return result;
}
} // namespace

int RunDocumentWandCompatibilityTests()
{
    const struct Case
    {
        const char* name;
        Loops subject;
        Loops clip;
    } cases[] = {
        {"shared-edge", {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}, {{{2, 0}, {4, 0}, {4, 2}, {2, 2}}}},
        {"corner-touch", {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}, {{{2, 2}, {4, 2}, {4, 4}, {2, 4}}}},
        {"overlap", {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, {{{5, 5}, {15, 5}, {15, 15}, {5, 15}}}},
        {"hole",
         {{{0, 0}, {3, 0}, {3, 3}, {0, 3}}, {{1, 1}, {1, 2}, {2, 2}, {2, 1}}},
         {{{-1, -1}, {4, -1}, {4, 4}, {-1, 4}}}},
        {"nested",
         {{{0, 0}, {8, 0}, {8, 8}, {0, 8}}, {{1, 1}, {1, 7}, {7, 7}, {7, 1}}, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}},
         {{{3, 0}, {5, 0}, {5, 8}, {3, 8}}}},
        {"duplicates",
         {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}, {{0, 0}, {2, 0}, {2, 2}, {0, 2}}},
         {{{1, -1}, {3, -1}, {3, 3}, {1, 3}}}},
        {"self-intersection", {{{0, 0}, {4, 4}, {0, 4}, {4, 0}}}, {{{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}}},
        {"fractional",
         {{{0.3, 0.2}, {3.7, 0.2}, {3.7, 2.9}, {0.3, 2.9}}},
         {{{1.1, -1}, {2.7, -1}, {2.7, 4}, {1.1, 4}}}},
        {"translated",
         {{{-1000, -20}, {-995, -20}, {-995, -15}, {-1000, -15}}},
         {{{-999, -21}, {-994, -21}, {-994, -16}, {-999, -16}}}},
        {"empty", {}, {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}},
    };
    int failures = 0;
    std::size_t failurePrefixes = 0, cancellationPrefixes = 0;
    double maximumStopToReturnMs = 0;
    for (const auto& item : cases)
        for (const auto operation :
             {BoundedSelectionBoolean::Operation::Intersect, BoundedSelectionBoolean::Operation::Union,
              BoundedSelectionBoolean::Operation::Subtract})
        {
            BoundedSelectionBoolean::Statistics statistics;
            auto actual = BoundedSelectionBoolean::Run(item.subject, item.clip, operation, {}, {}, &statistics);
            const auto expected = Canonical(Original(item.subject, item.clip, operation));
            const bool pass = actual && Canonical(*actual) == expected;
            std::printf("%s private-wand.pinned-compatibility.%s.%u\n", pass ? "PASS" : "FAIL", item.name,
                        static_cast<unsigned>(operation));
            failures += pass ? 0 : 1;
            const auto savedSubject = item.subject, savedClip = item.clip;
            bool allFailed = true;
            for (std::size_t prefix = 0; prefix < statistics.allocations; ++prefix)
            {
                BoundedSelectionBoolean::Limits limits;
                limits.allocations = prefix;
                bool refused = false;
                try
                {
                    (void)BoundedSelectionBoolean::Run(item.subject, item.clip, operation, {}, limits);
                }
                catch (const std::bad_alloc&)
                {
                    refused = true;
                }
                allFailed = allFailed && refused && item.subject == savedSubject && item.clip == savedClip;
                ++failurePrefixes;
            }
            BoundedSelectionBoolean::Limits exact;
            exact.allocations = statistics.allocations;
            auto afterFaults = BoundedSelectionBoolean::Run(item.subject, item.clip, operation, {}, exact);
            allFailed = allFailed && afterFaults && Canonical(*afterFaults) == expected;
            std::printf("%s private-wand.allocation-prefixes.%s.%u count=%zu\n", allFailed ? "PASS" : "FAIL", item.name,
                        static_cast<unsigned>(operation), statistics.allocations);
            failures += allFailed ? 0 : 1;

            bool allCancelled = true;
            for (std::uint64_t checkpoint = 64; checkpoint <= statistics.workUnits; checkpoint += 64)
            {
                StopPoint point{{}, checkpoint};
                bool refused = false;
                try
                {
                    (void)BoundedSelectionBoolean::Run(item.subject, item.clip, operation,
                                                       CancelToken{point.source.get_token()}, {}, nullptr, StopAt,
                                                       &point);
                }
                catch (const std::bad_alloc&)
                {
                    refused = true;
                }
                const auto returned = std::chrono::steady_clock::now();
                allCancelled = allCancelled && refused && point.source.stop_requested() &&
                               point.observed == checkpoint && item.subject == savedSubject && item.clip == savedClip;
                if (point.observed)
                    maximumStopToReturnMs =
                        std::max(maximumStopToReturnMs,
                                 std::chrono::duration<double, std::milli>(returned - point.requested).count());
                ++cancellationPrefixes;
            }
            auto afterStops = BoundedSelectionBoolean::Run(item.subject, item.clip, operation, {});
            allCancelled = allCancelled && afterStops && Canonical(*afterStops) == expected;
            std::printf("%s private-wand.cancellation-prefixes.%s.%u count=%llu\n", allCancelled ? "PASS" : "FAIL",
                        item.name, static_cast<unsigned>(operation),
                        static_cast<unsigned long long>(statistics.workUnits / 64));
            failures += allCancelled ? 0 : 1;
        }
    std::printf("private-wand.prefix-summary allocations=%zu cancellations=%zu stop-to-return-max-ms=%.6f\n",
                failurePrefixes, cancellationPrefixes, maximumStopToReturnMs);
    return failures;
}

#include "Document.Tests.WandBooleanLoad.h"
#include "BoundedClipper/BoundedSelectionBoolean.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <new>
#include <utility>

namespace
{
using Loops = std::vector<SelectionLoop>;
using Operation = BoundedSelectionBoolean::Operation;
double Area(const Loops& loops)
{
    double total = 0;
    for (const auto& loop : loops)
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto& a = loop[i];
            const auto& b = loop[(i + 1) % loop.size()];
            total += a.x * b.y - b.x * a.y;
        }
    return std::abs(total) / 2;
}
SelectionLoop Rectangle(double x, double y, double width, double height)
{
    return {{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
}
struct StopPoint
{
    std::stop_source source;
    std::uint64_t at = 0, observed = 0;
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
int failures = 0;
void Record(bool success, const char* name)
{
    std::printf("%s private-wand.load.%s\n", success ? "PASS" : "FAIL", name);
    failures += success ? 0 : 1;
}
void Case(const Loops& subject, const Loops& clip, double expectedArea, std::size_t expectedLoops, const char* name)
{
    BoundedSelectionBoolean::Statistics statistics;
    const auto began = std::chrono::steady_clock::now();
    auto result = BoundedSelectionBoolean::Run(subject, clip, Operation::Intersect, {}, {}, &statistics);
    const auto finished = std::chrono::steady_clock::now();
    Record(result && Area(*result) == expectedArea && result->size() == expectedLoops &&
               statistics.workspaceConsumed <= statistics.workspaceCapacity,
           name);
    const auto savedSubject = subject, savedClip = clip;
    std::array<std::uint64_t, 6> stops = {64,
                                          4096,
                                          statistics.workUnits / 4,
                                          statistics.workUnits / 2,
                                          3 * (statistics.workUnits / 4),
                                          statistics.workUnits};
    bool allCancelled = true;
    double maximumStopToReturnMs = 0;
    unsigned exercised = 0;
    for (auto stop : stops)
    {
        stop = (stop / 64) * 64;
        if (!stop || stop > statistics.workUnits)
            continue;
        StopPoint point{{}, stop};
        bool refused = false;
        try
        {
            (void)BoundedSelectionBoolean::Run(subject, clip, Operation::Intersect,
                                               CancelToken{point.source.get_token()}, {}, nullptr, StopAt, &point);
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        const auto returned = std::chrono::steady_clock::now();
        allCancelled = allCancelled && refused && point.observed == stop && point.source.stop_requested() &&
                       subject == savedSubject && clip == savedClip;
        if (point.observed)
            maximumStopToReturnMs = std::max(
                maximumStopToReturnMs, std::chrono::duration<double, std::milli>(returned - point.requested).count());
        ++exercised;
    }
    Record(allCancelled && exercised == 6, "six-large-input-cancellation-boundaries");
    BoundedSelectionBoolean::Limits limited;
    limited.workspaceBytes = 512 * 1024;
    bool refused = false;
    try
    {
        (void)BoundedSelectionBoolean::Run(subject, clip, Operation::Intersect, {}, limited);
    }
    catch (const std::bad_alloc&)
    {
        refused = true;
    }
    Record(refused && subject == savedSubject && clip == savedClip, "large-input-fixed-scratch-refusal");
    // Following every abandoned graph, an independent tiny exact result must still succeed.
    auto fresh =
        BoundedSelectionBoolean::Run({Rectangle(0, 0, 2, 2)}, {Rectangle(1, 0, 2, 2)}, Operation::Intersect, {});
    Record(fresh && Area(*fresh) == 2, "fresh-scope-after-large-graph-retirement");
    std::printf("private-wand.load-diagnostic name=%s points=%llu loops=%llu scratch=%zu allocations=%zu "
                "work=%llu elapsed-ms=%.6f stop-to-return-max-ms=%.6f scope=private-loaded-host\n",
                name, static_cast<unsigned long long>(statistics.outputPoints),
                static_cast<unsigned long long>(statistics.outputLoops), statistics.workspaceConsumed,
                statistics.allocations, static_cast<unsigned long long>(statistics.workUnits),
                std::chrono::duration<double, std::milli>(finished - began).count(), maximumStopToReturnMs);
}
} // namespace
int RunDocumentWandLoadTests()
{
    failures = 0;
    constexpr std::size_t sidePoints = 65536;
    SelectionLoop perimeter;
    perimeter.reserve(4 * sidePoints);
    for (std::size_t i = 0; i < sidePoints; ++i)
        perimeter.push_back({static_cast<double>(i) / 64, 0});
    for (std::size_t i = 0; i < sidePoints; ++i)
        perimeter.push_back({1024, static_cast<double>(i) / 64});
    for (std::size_t i = 0; i < sidePoints; ++i)
        perimeter.push_back({1024 - static_cast<double>(i) / 64, 1024});
    for (std::size_t i = 0; i < sidePoints; ++i)
        perimeter.push_back({0, 1024 - static_cast<double>(i) / 64});
    Case({std::move(perimeter)}, {Rectangle(-1, -1, 1026, 1026)}, 1048576, 1, "262144-collinear-input-points");
    Loops vertical, horizontal;
    for (unsigned i = 0; i < 128; ++i)
    {
        vertical.push_back(Rectangle(2 * i, 0, 1, 256));
        horizontal.push_back(Rectangle(0, 2 * i, 256, 1));
    }
    Case(vertical, horizontal, 16384, 16384, "16384-disjoint-grid-intersections");
    return failures;
}

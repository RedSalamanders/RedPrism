#include "BoundedSelectionBoolean.h"
#include "clipper.engine.h"

#include <cmath>
#include <limits>
#include <utility>

namespace BoundedSelectionBoolean
{
namespace
{
using namespace RedPrismBoundedClipper;

bool ValidInput(const std::vector<SelectionLoop>& subject, const std::vector<SelectionLoop>& clip, const Limits& limits,
                const CancelToken& cancellation)
{
    std::uint64_t points = 0, loops = 0, work = 0;
    // ClipperD(precision2) computes scale128, not scale100. Keep that original conversion exactly.
    constexpr double scale = 128.0;
    constexpr double coordinateLimit = static_cast<double>(MAX_COORD) / scale;
    for (const auto* collection : {&subject, &clip})
        for (const auto& loop : *collection)
        {
            if ((work++ & (Workspace::Quantum - 1)) == 0 && cancellation.IsCancelled())
                return false;
            if (loops == limits.inputLoops || loop.size() > limits.inputPoints - points)
                return false;
            ++loops;
            points += loop.size();
            for (const auto& point : loop)
            {
                if ((work++ & (Workspace::Quantum - 1)) == 0 && cancellation.IsCancelled())
                    return false;
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) >= coordinateLimit ||
                    std::abs(point.y) >= coordinateLimit)
                    return false;
            }
        }
    return true;
}

PathsD InputPaths(const std::vector<SelectionLoop>& loops)
{
    PathsD paths;
    paths.reserve(loops.size());
    for (const auto& loop : loops)
    {
        Workspace::Pulse();
        if (loop.size() < 3)
            continue;
        PathD path;
        path.reserve(loop.size());
        for (const auto& point : loop)
        {
            Workspace::Pulse();
            path.emplace_back(point.x, point.y);
        }
        paths.push_back(std::move(path));
    }
    return paths;
}
} // namespace

std::optional<std::vector<SelectionLoop>> Run(const std::vector<SelectionLoop>& subject,
                                              const std::vector<SelectionLoop>& clip, Operation operation,
                                              CancelToken cancellation, Limits limits, Statistics* statistics,
                                              void (*checkpoint)(void*, std::uint64_t) noexcept, void* context)
{
    if (statistics)
        *statistics = {};
    if (operation > Operation::Subtract || !limits.workspaceBytes || !limits.outputPoints || !limits.outputLoops ||
        !limits.inputPoints || !limits.inputLoops || !ValidInput(subject, clip, limits, cancellation))
        return std::nullopt;
    Workspace workspace(limits.workspaceBytes, cancellation, checkpoint, context, limits.allocations);
    ClipperD engine(2);
    engine.AddSubject(InputPaths(subject));
    engine.AddClip(InputPaths(clip));
    const auto kind = operation == Operation::Intersect ? ClipType::Intersection
                      : operation == Operation::Union   ? ClipType::Union
                                                        : ClipType::Difference;
    PathsD paths;
    if (!engine.Execute(kind, FillRule::NonZero, paths))
        Workspace::Refuse();
    std::uint64_t outputPoints = 0, outputLoops = 0;
    for (const auto& path : paths)
    {
        Workspace::Pulse();
        if (path.size() < 3)
            continue;
        if (outputLoops == limits.outputLoops || path.size() > limits.outputPoints - outputPoints)
            Workspace::Refuse();
        ++outputLoops;
        outputPoints += path.size();
    }
    // No arena-owned path, allocator, node or iterator escapes. Ordinary output storage is separately
    // reserved, including its Debug proxies/control, conversion coexistence and subsequent retirement.
    std::vector<SelectionLoop> result;
    result.reserve(static_cast<std::size_t>(outputLoops));
    for (const auto& path : paths)
    {
        Workspace::Pulse();
        if (path.size() < 3)
            continue;
        SelectionLoop loop;
        loop.reserve(path.size());
        for (const auto& point : path)
        {
            Workspace::Pulse();
            loop.push_back({point.x, point.y});
        }
        result.push_back(std::move(loop));
    }
    if (cancellation.IsCancelled())
        Workspace::Refuse();
    if (statistics)
    {
        const auto measured = workspace.GetStatistics();
        *statistics = {measured.capacity,  measured.consumed, measured.allocations,
                       measured.workUnits, outputPoints,      outputLoops};
    }
    return result;
}
} // namespace BoundedSelectionBoolean

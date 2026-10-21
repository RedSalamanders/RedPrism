#include "BoundedSelectionOffset.h"
#include "clipper.offset.h"

#include <cmath>
#include <utility>

namespace BoundedSelectionOffset
{
namespace
{
using namespace RedPrismBoundedClipper;

bool ValidInput(const std::vector<SelectionLoop>& subject, const BoundedSelectionBoolean::Limits& limits,
                const CancelToken& cancellation)
{
    std::uint64_t points = 0, loops = 0, work = 0;
    constexpr double coordinateLimit = static_cast<double>(MAX_COORD) / 128.0;
    for (const auto& loop : subject)
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

std::optional<std::vector<SelectionLoop>> Run(const std::vector<SelectionLoop>& subject, int signedAmount,
                                              int canvasWidth, int canvasHeight, CancelToken cancellation,
                                              BoundedSelectionBoolean::Limits limits,
                                              BoundedSelectionBoolean::Statistics* statistics,
                                              void (*checkpoint)(void*, std::uint64_t) noexcept, void* context)
{
    if (statistics)
        *statistics = {};
    if (signedAmount == 0 || signedAmount < -DocumentSession::kSelectionModifyLimit ||
        signedAmount > DocumentSession::kSelectionModifyLimit || canvasWidth <= 0 || canvasHeight <= 0 ||
        canvasWidth > kMaxCanvasSide || canvasHeight > kMaxCanvasSide || !limits.workspaceBytes ||
        !limits.inputPoints || !limits.inputLoops || !limits.outputPoints || !limits.outputLoops ||
        !ValidInput(subject, limits, cancellation))
        return std::nullopt;
    Workspace workspace(limits.workspaceBytes, cancellation, checkpoint, context, limits.allocations,
                        Workspace::AllocationPolicy::ReuseFreed);
    PathsD normalized;
    {
        ClipperD engine(2);
        engine.AddSubject(InputPaths(subject));
        if (!engine.Execute(ClipType::Union, FillRule::NonZero, normalized))
            Workspace::Refuse();
    }
    int error = 0;
    // InflatePaths uses decimal scale100, distinct from ClipperD's precision2 binary scale128.
    PathsD paths;
    {
        ClipperOffset engine(2.0, 0.0);
        engine.AddPaths(ScalePaths<std::int64_t, double>(normalized, 100.0, error), JoinType::Round, EndType::Polygon);
        if (error)
            Workspace::Refuse();
        Paths64 inflated;
        engine.Execute(static_cast<double>(signedAmount) * 100.0, inflated);
        if (engine.ErrorCode())
            Workspace::Refuse();
        paths = ScalePaths<double, std::int64_t>(inflated, 1.0 / 100.0, error);
        if (error)
            Workspace::Refuse();
    }
    if (signedAmount > 0)
    {
        ClipperD engine(2);
        engine.AddSubject(paths);
        engine.AddClip(PathsD{PathD{{0.0, 0.0},
                                    {static_cast<double>(canvasWidth), 0.0},
                                    {static_cast<double>(canvasWidth), static_cast<double>(canvasHeight)},
                                    {0.0, static_cast<double>(canvasHeight)}}});
        PathsD clipped;
        if (!engine.Execute(ClipType::Intersection, FillRule::NonZero, clipped))
            Workspace::Refuse();
        paths = std::move(clipped);
    }
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
} // namespace BoundedSelectionOffset

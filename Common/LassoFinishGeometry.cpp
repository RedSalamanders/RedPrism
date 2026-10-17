#include "LassoFinishGeometry.h"

#include "PixelBuffer.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace LassoFinishGeometry
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
[[nodiscard]] bool Step(const Observer* observer, Stage stage, const CancelToken& cancellation)
{
    if (observer && observer->checkpoint)
        observer->checkpoint(observer->context, stage);
    return !cancellation.IsCancelled();
}
[[nodiscard]] bool ValidLimits(const BoundedSelectionBoolean::Limits& value) noexcept
{
    const BoundedSelectionBoolean::Limits maximum;
    return value.workspaceBytes && value.workspaceBytes <= maximum.workspaceBytes && value.inputPoints &&
           value.inputPoints <= maximum.inputPoints && value.inputLoops && value.inputLoops <= maximum.inputLoops &&
           value.outputPoints && value.outputPoints <= maximum.outputPoints && value.outputLoops &&
           value.outputLoops <= maximum.outputLoops;
}
struct ByteSum final
{
    std::uint64_t value = 0;
    [[nodiscard]] bool Add(std::uint64_t count, std::uint64_t size = 1) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (size && count > maximum / size)
            return false;
        const auto bytes = count * size;
        if (bytes > maximum - value)
            return false;
        value += bytes;
        return true;
    }
};
[[nodiscard]] std::optional<MemoryPlan> CalculatePlan(LassoKind kind, std::uint64_t pointCount,
                                                      std::uint64_t retainedArenaBytes, std::uint64_t traversalBytes,
                                                      BoundedSelectionBoolean::Limits limits) noexcept
{
    MemoryPlan result;
    result.retainedArenaBytes = retainedArenaBytes;
    result.traversalBytes = traversalBytes;
    ByteSum outline, output, complete;
    const auto points = kind == LassoKind::Ellipse ? std::uint64_t{2048} : pointCount;
    if (points > limits.inputPoints - (std::min)(limits.inputPoints, std::uint64_t{4}))
        return std::nullopt;
    const auto ownership = SelectionLoops::WorkOwnershipAllowance(limits.outputLoops);
    if (!ownership || !outline.Add(points, sizeof(Vec2)) || !outline.Add(1024) ||
        !output.Add(limits.outputPoints, sizeof(Vec2)) || !output.Add(limits.outputLoops, sizeof(SelectionLoop)) ||
        !output.Add(ownership) || !output.Add(sizeof(std::vector<SelectionLoop>)))
        return std::nullopt;
    result.outlineBytes = outline.value;
    result.workspaceBytes = PixelBuffer::RequiredBytes(limits.workspaceBytes);
    result.outputBytes = output.value;
    // One adapter workspace at a time; clipped output coexists with mode output and the original outline.
    // Recycled workspace capacity remains charged rather than assuming free returns process commit.
    if (!result.workspaceBytes || !complete.Add(result.traversalBytes) || !complete.Add(result.outlineBytes) ||
        !complete.Add(result.workspaceBytes) || !complete.Add(result.outputBytes, 2) ||
        !complete.Add(SharedPublicationMemory::OwnershipAllowance()) || !complete.Add(sizeof(Result) + 4096))
        return std::nullopt;
    result.preparationBytes = complete.value;
    return result;
}
[[nodiscard]] std::optional<SelectionLoop> Oval(SelectionBounds box, const CancelToken& cancellation)
{
    const double a = box.Width() / 2.0, b = box.Height() / 2.0;
    const double centerX = box.minX + a, centerY = box.minY + b;
    if (!(a > 0) || !(b > 0) || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(centerX) ||
        !std::isfinite(centerY))
        return std::nullopt;
    // Same Ramanujan expression and multiples of four as SelectionGeometry::Ellipse. Clamp before
    // integer conversion, including overflowed positive perimeter, to avoid undefined conversion.
    const double perimeter = kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
    const double estimate = std::ceil(perimeter / 2.0);
    const int segments =
        std::isfinite(estimate) ? (static_cast<int>(std::clamp(estimate, 32.0, 2048.0)) + 3) / 4 * 4 : 2048;
    SelectionLoop loop;
    loop.reserve(static_cast<std::size_t>(segments));
    for (int index = 0; index < segments; ++index)
    {
        if ((index & 63) == 0 && cancellation.IsCancelled())
            return std::nullopt;
        const double angle = 2.0 * kPi * static_cast<double>(index) / static_cast<double>(segments);
        loop.push_back({centerX + a * std::cos(angle), centerY + b * std::sin(angle)});
    }
    return loop;
}
} // namespace

BoundedSelectionBoolean::Limits NewRectangleLimits() noexcept
{
    BoundedSelectionBoolean::Limits limits;
    limits.workspaceBytes = 64 * 1024;
    limits.inputPoints = 8; // Four marquee corners plus the four canvas corners.
    limits.inputLoops = 2;
    limits.outputPoints = 16;
    limits.outputLoops = 4;
    return limits;
}

std::optional<MemoryPlan> PlanNewRectangle() noexcept
{
    // MarqueeOwner always resets its inline tail before adding four corners; no sealed prefix is traversed.
    return CalculatePlan(LassoKind::Rectangle, 4, BoundedLassoPoints::MemoryBytes(1), 0, NewRectangleLimits());
}

std::optional<BoundedSelectionBoolean::Limits> NewMarqueeLimits(LassoKind kind) noexcept
{
    if (kind == LassoKind::Rectangle)
        return NewRectangleLimits();
    if (kind != LassoKind::Ellipse)
        return std::nullopt;
    BoundedSelectionBoolean::Limits limits;
    limits.workspaceBytes = 4 * 1024 * 1024;
    limits.inputPoints = 2052; // At most 2048 oval vertices and four canvas corners.
    limits.inputLoops = 2;
    limits.outputPoints = 4096;
    limits.outputLoops = 16;
    return limits;
}
std::optional<MemoryPlan> PlanNewMarquee(LassoKind kind) noexcept
{
    const auto limits = NewMarqueeLimits(kind);
    if (!limits)
        return std::nullopt;
    return CalculatePlan(kind, 4, BoundedLassoPoints::MemoryBytes(1), 0, limits.value());
}

std::optional<MemoryPlan> Plan(const BoundedLassoGesture::Display& terminal,
                               const std::optional<DocumentSelection>& before,
                               BoundedSelectionBoolean::Limits limits) noexcept
{
    if (!terminal.key.opening || !terminal.key.revision || terminal.kind > LassoKind::Ellipse ||
        terminal.mode > SelectionMode::Subtract || !ValidLimits(limits) ||
        terminal.points.Count() > limits.inputPoints - (std::min)(limits.inputPoints, std::uint64_t{4}) ||
        limits.inputLoops < 2 ||
        (terminal.mode != SelectionMode::New && before &&
         (before->loops.PointCount() > limits.outputPoints || before->loops.size() >= limits.inputLoops)))
        return std::nullopt;
    return CalculatePlan(terminal.kind, terminal.points.Count(), terminal.points.RetainedArenaBytes(),
                         terminal.points.TraversalScratchBytes(), limits);
}

std::optional<Result> Prepare(const BoundedLassoGesture::Display& terminal, int width, int height,
                              const std::optional<DocumentSelection>& before, CancelToken cancellation,
                              const Observer* observer, std::shared_ptr<const SharedPublicationMemory> memory)
{
    const auto limits = observer ? observer->limits : BoundedSelectionBoolean::Limits{};
    if (width <= 0 || height <= 0 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        !Plan(terminal, before, limits) || !Step(observer, Stage::BeforeCopy, cancellation))
        return std::nullopt;
    Result result;
    result.kind = terminal.kind;
    const auto bounds = terminal.points.Bounds();
    if ((terminal.points.Count() < 3 && terminal.kind != LassoKind::Ellipse) || !bounds || !(bounds->Width() > 0) ||
        !(bounds->Height() > 0))
    {
        result.disposition = terminal.mode == SelectionMode::New ? Disposition::Clear : Disposition::Keep;
        return Step(observer, Stage::Prepared, cancellation) ? std::optional(std::move(result)) : std::nullopt;
    }
    if (terminal.mode == SelectionMode::Subtract && !before)
        return Step(observer, Stage::Prepared, cancellation) ? std::optional(std::move(result)) : std::nullopt;
    SelectionLoop loop;
    if (terminal.kind == LassoKind::Ellipse && terminal.points.Count() == 4)
    {
        auto oval = Oval(bounds.value(), cancellation);
        if (!oval)
            return std::nullopt;
        loop = std::move(oval.value());
    }
    else
    {
        loop.reserve(terminal.points.Count());
        if (!terminal.points.VisitForWork(
                [&](std::span<const Vec2> chunk)
                {
                    loop.insert(loop.end(), chunk.begin(), chunk.end());
                    return Step(observer, Stage::ChunkCopied, cancellation);
                },
                cancellation))
            return std::nullopt;
    }
    std::vector<SelectionLoop> outline;
    outline.reserve(1);
    outline.push_back(std::move(loop));
    const auto canvas = SelectionGeometry::Rect(0, 0, width, height);
    if (!Step(observer, Stage::BeforeClip, cancellation))
        return std::nullopt;
    auto clipped = BoundedSelectionBoolean::Run(outline, canvas, BoundedSelectionBoolean::Operation::Intersect,
                                                cancellation, limits);
    if (!clipped || !Step(observer, Stage::Clipped, cancellation))
        return std::nullopt;
    if (!Step(observer, Stage::BeforeMode, cancellation))
        return std::nullopt;
    std::optional<std::vector<SelectionLoop>> changed;
    if (terminal.mode == SelectionMode::New || !before)
        changed = std::move(clipped);
    else
        changed = BoundedSelectionBoolean::Run(before->loops.Get(), clipped.value(),
                                               terminal.mode == SelectionMode::Add
                                                   ? BoundedSelectionBoolean::Operation::Union
                                                   : BoundedSelectionBoolean::Operation::Subtract,
                                               cancellation, limits);
    if (!changed || !Step(observer, Stage::BeforeAdopt, cancellation))
        return std::nullopt;
    auto immutable = SelectionLoops::AdoptForWork(std::move(changed.value()), cancellation, std::move(memory));
    if (!immutable || !Step(observer, Stage::Prepared, cancellation))
        return std::nullopt;
    result.disposition = Disposition::Replace;
    result.selection = DocumentSelection{terminal.antialiased, std::move(immutable.value())};
    return result;
}
} // namespace LassoFinishGeometry

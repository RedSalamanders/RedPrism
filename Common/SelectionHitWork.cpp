#include "SelectionHitWork.h"

#include <algorithm>
#include <cmath>

std::optional<bool> ContainsSelectionForWork(const SelectionDisplayValue& display, Vec2 query, CancelToken cancellation,
                                             SelectionHitObserver observer) noexcept
{
    std::size_t work = 0;
    const auto checkpoint = [&](SelectionHitStage stage) noexcept
    {
        if (cancellation.IsCancelled())
            return false;
        if (observer.checkpoint)
            observer.checkpoint(observer.context, stage, work);
        return !cancellation.IsCancelled();
    };
    const auto step = [&](SelectionHitStage stage) noexcept
    {
        const bool ready = work % kSelectionHitCheckpointQuantum != 0 || checkpoint(stage);
        ++work;
        return ready;
    };
    const auto complete = [&](bool inside) noexcept -> std::optional<bool>
    { return checkpoint(SelectionHitStage::Complete) ? std::optional(inside) : std::nullopt; };
    if (!checkpoint(SelectionHitStage::Begin))
        return std::nullopt;
    if (!std::isfinite(display.translation.x) || !std::isfinite(display.translation.y))
        return std::nullopt;
    if (!display.selection || !std::isfinite(query.x) || !std::isfinite(query.y))
        return complete(false);

    int winding = 0;
    for (const auto& loop : display.selection->loops)
    {
        if (!step(SelectionHitStage::Loop))
            return std::nullopt;
        const auto count = loop.size();
        if (count < 3)
            continue;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!step(SelectionHitStage::Edge))
                return std::nullopt;
            const auto& first = loop[index];
            const auto& second = loop[(index + 1) % count];
            const Vec2 a{first.x + display.translation.x, first.y + display.translation.y};
            const Vec2 b{second.x + display.translation.x, second.y + display.translation.y};
            const double cross = (b.x - a.x) * (query.y - a.y) - (b.y - a.y) * (query.x - a.x);
            // Preserve the numeric reference's segment tolerance and nonzero winding rule.
            if (std::abs(cross) < 1e-9 && query.x >= std::min(a.x, b.x) - 1e-9 &&
                query.x <= std::max(a.x, b.x) + 1e-9 && query.y >= std::min(a.y, b.y) - 1e-9 &&
                query.y <= std::max(a.y, b.y) + 1e-9)
                return complete(true);
            if (a.y <= query.y)
            {
                if (b.y > query.y && cross > 0)
                    ++winding;
            }
            else if (b.y <= query.y && cross < 0)
                --winding;
        }
    }
    return complete(winding != 0);
}

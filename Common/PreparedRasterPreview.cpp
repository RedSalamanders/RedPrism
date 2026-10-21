#include "PreparedRasterPreview.h"

#include "SharedPublicationMemory.h"

#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "PublicationGraphAllocator.h"

#include <limits>
#include <utility>

namespace
{
bool Add(std::uint64_t& total, std::uint64_t count, std::uint64_t unit = 1) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if ((unit && count > maximum / unit) || count * unit > maximum - total)
        return false;
    total += count * unit;
    return true;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct OwnedRasterPreview final : DeferredReleaseNode
{
    PreparedRasterPreview value;
    OwnedRasterPreview() noexcept
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedRasterPreview> owned(static_cast<OwnedRasterPreview*>(node)); };
    }
};
#pragma warning(pop)

struct RetireRasterPreview final
{
    void operator()(OwnedRasterPreview* node) const noexcept
    {
        if (node && !TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<OwnedRasterPreview> owned(node);
        }
    }
};
} // namespace

std::uint64_t RasterPreviewMetadataMemoryBytes(int width, int height) noexcept
{
    if (width <= 0 || height <= 0 || width > BrushStroke::kMaxGridSide || height > BrushStroke::kMaxGridSide)
        return 0;
    const auto columns = (static_cast<std::uint64_t>(width) + 255) / 256;
    const auto rows = (static_cast<std::uint64_t>(height) + 255) / 256;
    // Full distinct tile directory, copied immutable windows and all three bounded graph controls. The fixed
    // allowance includes the accounting node, intrusive owners and failure-path construction overlap.
    auto bytes = std::uint64_t{1024 * 1024};
    return Add(bytes, columns * rows, 2 * sizeof(RasterTile)) ? bytes : 0;
}

std::shared_ptr<const PreparedRasterPreview> PrepareRasterPreviewForWork(
    const BrushStroke& stroke, std::thread::id ownerThread, CancelToken cancellation,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (ownerThread == std::thread::id{} || ownerThread == std::this_thread::get_id() || cancellation.IsCancelled() ||
        !stroke.IsValid() || stroke.Width() <= 0 || stroke.Height() <= 0 || !stroke.PaintTransform().IsValid())
        return {};
    const auto format = stroke.IsMask() ? RasterFormat::Gray8 : RasterFormat::Bgra8888;
    const auto& source = stroke.SourceImage();
    const auto& sourceRect = stroke.SourceRect();
    if (source.HasStorage() && (source.Format() != format || sourceRect.width <= 0 || sourceRect.height <= 0))
        return {};
    auto owned = std::make_unique<OwnedRasterPreview>();
    auto* output = &owned->value;
    output->memory = memory;
    output->target = stroke.LayerID();
    output->mask = stroke.IsMask();
    output->width = stroke.Width();
    output->height = stroke.Height();
    output->transform = stroke.PaintTransform();
    output->fill = stroke.FillValue();
    output->source = source;
    output->sourceRect = sourceRect;
    output->tiles.reserve(stroke.Tiles().size());
    auto held = static_cast<std::uint64_t>(sizeof(PreparedRasterPreview)) + 65536;
    if ((memory && !Add(held, SharedPublicationMemory::OwnershipAllowance())) ||
        !Add(held, output->tiles.capacity(), sizeof(RasterTile)) ||
        (source.HasStorage() && !Add(held, source.RetirementMemoryBytes())))
        return {};
    for (const auto& tile : stroke.Tiles())
    {
        if (cancellation.IsCancelled())
            return {};
        if (!tile.touched)
            continue;
        const auto& bounds = tile.rect;
        if (bounds.x < 0 || bounds.y < 0 || bounds.width <= 0 || bounds.height <= 0 ||
            static_cast<std::int64_t>(bounds.x) + bounds.width > output->width ||
            static_cast<std::int64_t>(bounds.y) + bounds.height > output->height || !tile.image.HasStorage() ||
            tile.image.Format() != format || tile.image.Width() != bounds.width ||
            tile.image.Height() != bounds.height || !Add(held, tile.image.RetirementMemoryBytes()))
            return {};
        output->tiles.push_back({bounds.x, bounds.y, tile.image});
    }
    if (cancellation.IsCancelled())
        return {};
    output->ownershipBytes = held;
    owned->bytes = held;
    auto shared = std::shared_ptr<OwnedRasterPreview>(owned.release(), RetireRasterPreview{},
                                                      PublicationGraphAllocator<OwnedRasterPreview>{memory});
    return std::shared_ptr<const PreparedRasterPreview>(shared, &shared->value);
}

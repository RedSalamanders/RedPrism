#include "TransformPixelsWork.h"

#include "RasterWork.h"
#include "ShapeEditWork.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace
{
bool Targets(const TransformEdit& edit, Guid id) noexcept
{
    if (!edit.group)
        return id == edit.layerID;
    return std::any_of(edit.group->originals.begin(), edit.group->originals.end(),
                       [&](const auto& entry) { return entry.first == id; });
}

std::optional<LayerTransform> Placement(const TransformEdit& edit, const ImageLayer& layer) noexcept
{
    if (!edit.group)
        return edit.draft;
    const auto original = std::find_if(edit.group->originals.begin(), edit.group->originals.end(),
                                       [&](const auto& entry) { return entry.first == layer.id; });
    return original == edit.group->originals.end()
               ? std::nullopt
               : std::optional(original->second.Following(edit.group->box, edit.draft));
}

bool Add(std::uint64_t& bytes, std::uint64_t amount) noexcept
{
    if (amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += amount;
    return true;
}

TransformPixelsStatus WarpStatus(DistortWorkStatus status) noexcept
{
    return status == DistortWorkStatus::Cancelled ? TransformPixelsStatus::Cancelled : TransformPixelsStatus::Memory;
}

bool SetMaskImage(LayerMask& mask, Raster image, CancelToken cancellation)
{
    if (mask.asset.image == image)
        return !cancellation.IsCancelled();
    auto thumbnail = ThumbnailRasterForWork(image, 96, cancellation);
    if (!thumbnail.HasStorage())
        return false;
    mask.asset.image = std::move(image);
    mask.asset.thumbnail = std::move(thumbnail);
    return !cancellation.IsCancelled();
}

TransformPixelsStatus Redraw(ImageLayer& layer, std::int64_t imageLeft, Executor& executor, CancelToken cancellation)
{
    const auto shape = LiveShape(layer);
    if (!shape || !layer.asset)
        return TransformPixelsStatus::Ready;
    const double w = (std::max)(1.0, std::round(layer.transform.size.x));
    const double h = (std::max)(1.0, std::round(layer.transform.size.y));
    // Preserve D7's stretched old image for sizes beyond the raster/document budget. Check before int conversion.
    if (w > kMaxCanvasSide || h > kMaxCanvasSide || w * h > DocumentSession::kMaxShapePixels || w * h > imageLeft)
        return TransformPixelsStatus::Ready;
    const auto width = static_cast<int>(w);
    const auto height = static_cast<int>(h);
    if (width == layer.asset->image.Width() && height == layer.asset->image.Height())
        return TransformPixelsStatus::Ready;
    const auto byte = [](double value)
    { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255)); };
    const auto color = 0xff000000u | (byte(shape->red) << 16) | (byte(shape->green) << 8) | byte(shape->blue);
    auto image = ShapeImageForWork(shape->kind, width, height, color, shape->cornerRadius, executor, cancellation);
    if (!image.HasStorage())
        return cancellation.IsCancelled() ? TransformPixelsStatus::Cancelled : TransformPixelsStatus::Memory;
    if (layer.mask && !layer.mask->placement)
        layer.mask->placement = MaskTransform(layer);
    layer.asset->image = image;
    layer.asset->thumbnail = {};
    layer.shapeImage = std::move(image);
    return TransformPixelsStatus::Ready;
}
} // namespace

std::uint64_t TransformPixelsMemoryBytes(const CanvasDocument& source, const TransformEdit& edit) noexcept
{
    if (edit.floating || (edit.mask && edit.group) || !edit.draft.IsValid() || source.layers.size() > kMaxLayerCount)
        return 0;
    if (edit.group && (!edit.group->box.IsValid() || edit.group->originals.empty() ||
                       edit.group->originals.size() > static_cast<std::size_t>(kMaxLayerCount)))
        return 0;
    std::uint64_t bytes = sizeof(TransformPixelsResult) + 4096;
    // Record/string/curve copies are charged by the caller's complete document/history declaration. Summing
    // complete per-target peaks also covers retained earlier results while the next target is prepared.
    for (const auto& layer : source.layers)
    {
        if (!Targets(edit, layer.id))
            continue;
        const auto moved = Placement(edit, layer);
        if (!moved || !moved->IsValid())
            continue;
        if (edit.corners)
        {
            const auto corners = edit.group ? DistortWarp::Carried(moved.value(), edit.draft, edit.corners.value())
                                            : edit.corners.value();
            if (!edit.mask && layer.asset)
            {
                const auto peak =
                    DistortWorkMemoryBytes({layer.asset->image, moved.value(), corners, false, 0, 0, true});
                if (peak && !Add(bytes, peak))
                    return 0;
            }
            if (layer.mask && (edit.mask || layer.mask->linked))
            {
                const bool placed = !edit.mask && layer.mask->placement.has_value();
                const auto placement =
                    placed ? layer.mask->placement->Following(layer.transform, moved.value()) : moved.value();
                const auto maskCorners = placed ? DistortWarp::Carried(placement, moved.value(), corners) : corners;
                const auto peak =
                    DistortWorkMemoryBytes({layer.mask->asset.image, placement, maskCorners, true,
                                            placed || edit.mask ? MaskEdgeBackground(layer.mask->asset.thumbnail) : 0});
                // Warp, crop overlap and private thumbnail source row/output each fit another complete mask peak.
                if (peak && (!Add(bytes, peak) || !Add(bytes, peak) || !Add(bytes, peak)))
                    return 0;
            }
            continue;
        }
        if (edit.mask || !LiveShape(layer))
            continue;
        const double w = (std::max)(1.0, std::round(moved->size.x));
        const double h = (std::max)(1.0, std::round(moved->size.y));
        if (w > kMaxCanvasSide || h > kMaxCanvasSide || w * h > DocumentSession::kMaxShapePixels)
            continue;
        const auto image = ShapeImageMemoryBytes(layer.shape->kind, static_cast<int>(w), static_cast<int>(h),
                                                 layer.shape->cornerRadius);
        if (!image || !Add(bytes, image))
            return 0;
    }
    return bytes;
}

TransformPixelsResult PrepareTransformPixels(const CanvasDocument& source, const TransformEdit& edit,
                                             Executor& executor, CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {TransformPixelsStatus::Cancelled};
    if (edit.floating)
        return {TransformPixelsStatus::Unsupported};
    if (!TransformPixelsMemoryBytes(source, edit) || ValidateDocument(source) != DocumentStatus::Ok)
        return {};
    TransformPixelsResult result;
    auto after = source;
    if (cancellation.IsCancelled())
        return {TransformPixelsStatus::Cancelled};
    std::int64_t imageLeft = kMaxLayerPixels;
    std::int64_t maskLeft = kMaxMaskPixels;
    for (const auto& layer : source.layers)
    {
        imageLeft -= layer.asset ? static_cast<std::int64_t>(layer.asset->image.PixelCount()) : 0;
        maskLeft -= layer.mask ? static_cast<std::int64_t>(layer.mask->asset.image.PixelCount()) : 0;
    }
    std::vector<std::size_t> targets;
    targets.reserve(edit.group ? edit.group->originals.size() : 1);
    if (edit.group)
    {
        // D7 is order-sensitive: preserve the exact original group order, rather than storage order.
        std::unordered_set<Guid> seen;
        seen.reserve(edit.group->originals.size());
        for (const auto& entry : edit.group->originals)
        {
            if (cancellation.IsCancelled())
                return {TransformPixelsStatus::Cancelled};
            if (!seen.insert(entry.first).second)
                return {};
            if (const auto index = IndexOf(after.layers, entry.first))
                targets.push_back(index.value());
        }
    }
    else if (const auto index = IndexOf(after.layers, edit.layerID))
        targets.push_back(index.value());
    for (const auto index : targets)
    {
        auto& layer = after.layers[index];
        if (cancellation.IsCancelled())
            return {TransformPixelsStatus::Cancelled};
        const auto moved = Placement(edit, layer);
        if (!moved || !moved->IsValid())
            continue;
        if (edit.mask)
        {
            if (!layer.mask)
                continue;
            LayerTransform maskPlacement = edit.draft;
            if (edit.corners)
            {
                auto warped = RunDistortWork({layer.mask->asset.image, edit.draft, edit.corners.value(), true,
                                              MaskEdgeBackground(layer.mask->asset.thumbnail)},
                                             executor, cancellation);
                if (warped.status == DistortWorkStatus::Invalid || warped.status == DistortWorkStatus::TooLarge)
                    continue;
                if (warped.status != DistortWorkStatus::Ready)
                    return {WarpStatus(warped.status)};
                if (static_cast<std::int64_t>(warped.image.PixelCount()) >
                    maskLeft + static_cast<std::int64_t>(layer.mask->asset.image.PixelCount()))
                {
                    result.budgetRefused = true;
                    continue;
                }
                if (!SetMaskImage(layer.mask.value(), std::move(warped.image), cancellation))
                    return {cancellation.IsCancelled() ? TransformPixelsStatus::Cancelled
                                                       : TransformPixelsStatus::Memory};
                maskPlacement = warped.placement;
            }
            layer.mask->placement =
                maskPlacement.SamePlacement(layer.transform) ? std::nullopt : std::optional(maskPlacement);
            continue;
        }
        if (!edit.corners)
        {
            if (layer.mask)
                layer.mask->placement = MaskPlacementMoving(layer.mask.value(), layer.transform, moved.value());
            const auto oldPixels = layer.asset ? static_cast<std::int64_t>(layer.asset->image.PixelCount()) : 0;
            layer.transform = moved.value();
            const auto redraw = Redraw(layer, imageLeft + oldPixels, executor, cancellation);
            if (redraw != TransformPixelsStatus::Ready)
                return {redraw};
            const auto newPixels = layer.asset ? static_cast<std::int64_t>(layer.asset->image.PixelCount()) : 0;
            imageLeft += oldPixels - newPixels;
            continue;
        }
        const auto oldPixels = layer.asset ? static_cast<std::int64_t>(layer.asset->image.PixelCount()) : 0;
        const auto oldMaskPixels = layer.mask ? static_cast<std::int64_t>(layer.mask->asset.image.PixelCount()) : 0;
        // Keep unprocessed members' old pixels reserved. Discounting every group member up front can let an
        // early replacement consume bytes that a later refusal still needs for its unchanged original.
        imageLeft += oldPixels;
        maskLeft += oldMaskPixels;
        const auto keep = [&]
        {
            imageLeft -= oldPixels;
            maskLeft -= oldMaskPixels;
        };
        if (!layer.asset)
        {
            keep();
            continue;
        }
        const auto corners =
            edit.group ? DistortWarp::Carried(moved.value(), edit.draft, edit.corners.value()) : edit.corners.value();
        auto warped =
            RunDistortWork({layer.asset->image, moved.value(), corners, false, 0, 0, true}, executor, cancellation);
        if (warped.status == DistortWorkStatus::Invalid || warped.status == DistortWorkStatus::TooLarge)
        {
            keep();
            continue;
        }
        if (warped.status != DistortWorkStatus::Ready)
            return {WarpStatus(warped.status)};
        auto mask = layer.mask;
        if (mask && mask->linked)
        {
            const bool placed = mask->placement.has_value();
            const auto placement = placed ? mask->placement->Following(layer.transform, moved.value()) : moved.value();
            const auto maskCorners = placed ? DistortWarp::Carried(placement, moved.value(), corners) : corners;
            auto warpedMask = RunDistortWork({mask->asset.image, placement, maskCorners, true,
                                              placed ? MaskEdgeBackground(mask->asset.thumbnail) : 0},
                                             executor, cancellation);
            if (warpedMask.status == DistortWorkStatus::Ready)
            {
                auto image = std::move(warpedMask.image);
                if (!placed && !(image == mask->asset.image))
                    image = CropRasterForWork(image,
                                              {warped.cropX, warped.cropY, warped.image.Width(), warped.image.Height()},
                                              0, cancellation);
                if (!image.HasStorage() || !SetMaskImage(mask.value(), std::move(image), cancellation))
                    return {cancellation.IsCancelled() ? TransformPixelsStatus::Cancelled
                                                       : TransformPixelsStatus::Memory};
                if (placed)
                    mask->placement = warpedMask.placement;
            }
            else if (warpedMask.status != DistortWorkStatus::Invalid &&
                     warpedMask.status != DistortWorkStatus::TooLarge)
                return {WarpStatus(warpedMask.status)};
        }
        else if (mask && !mask->placement)
            mask->placement = layer.transform;
        const auto pixels = static_cast<std::int64_t>(warped.image.PixelCount());
        const auto maskPixels = mask ? static_cast<std::int64_t>(mask->asset.image.PixelCount()) : 0;
        if (pixels > imageLeft || maskPixels > maskLeft)
        {
            result.budgetRefused = true;
            keep();
            continue;
        }
        imageLeft -= pixels;
        maskLeft -= maskPixels;
        layer.asset->image = std::move(warped.image);
        layer.asset->thumbnail = {};
        layer.transform = warped.placement;
        layer.mask = std::move(mask);
    }
    if (cancellation.IsCancelled())
        return {TransformPixelsStatus::Cancelled};
    if (ValidateDocument(after) != DocumentStatus::Ok)
        return {};
    result.historyName = edit.mask      ? edit.corners ? L"Distort Layer Mask" : L"Transform Layer Mask"
                         : edit.corners ? edit.group ? L"Distort Layers" : L"Distort"
                         : edit.group   ? L"Transform Layers"
                                        : L"Transform Layer";
    result.document = std::move(after);
    result.status = TransformPixelsStatus::Ready;
    return result;
}

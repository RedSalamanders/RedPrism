#include "FloatingPixelsWork.h"

#include "CopyPixelsWork.h"
#include "RasterEditWork.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool Add(std::uint64_t& bytes, std::uint64_t amount) noexcept
{
    if (amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += amount;
    return true;
}

std::int64_t ImageBudget(const CanvasDocument& document, Guid first, Guid second = {}) noexcept
{
    std::int64_t left = kMaxLayerPixels;
    for (const auto& layer : document.layers)
        if (layer.id != first && layer.id != second && layer.asset)
            left -= layer.asset->image.PixelCount();
    return left;
}

CopyPixelsInput CopyInput(const CanvasDocument& source, const ImageLayer& layer)
{
    return {layer.asset->image, layer.transform, source.selection, source.width, source.height};
}

RasterEditInput ClearInput(const CanvasDocument& source, const ImageLayer& layer)
{
    RasterEditInput input;
    input.layer = layer;
    input.selection = source.selection;
    input.canvasWidth = source.width;
    input.canvasHeight = source.height;
    input.pixelLimit = ImageBudget(source, layer.id);
    input.kind = RasterEditKind::Clear;
    return input;
}

const ImageLayer* OpeningLayer(const CanvasDocument& source, Guid active) noexcept
{
    const auto index = IndexOf(source.layers, active);
    const auto* layer = index ? &source.layers[index.value()] : nullptr;
    return source.selection && !source.selection->IsEmpty() && source.layers.size() < kMaxLayerCount && layer &&
                   !layer->isGroup && !layer->adjustment && layer->asset &&
                   layer->asset->image.Format() == RasterFormat::Bgra8888 && layer->transform.IsValid()
               ? layer
               : nullptr;
}

std::optional<GridRect> MappedRect(const Affine2& map, int width, int height) noexcept
{
    const std::array<Vec2, 4> corners{map.Apply({0, 0}), map.Apply({static_cast<double>(width), 0}),
                                      map.Apply({static_cast<double>(width), static_cast<double>(height)}),
                                      map.Apply({0, static_cast<double>(height)})};
    const auto bounds = PointBounds(corners);
    const auto x0 = std::floor(bounds.minX), x1 = std::ceil(bounds.maxX);
    const auto y0 = std::floor(bounds.minY), y1 = std::ceil(bounds.maxY);
    // Refuse remote/overflowing geometry before any int conversion or GridRect arithmetic. Such extents
    // could never satisfy the 30,000-side merge limit; no valid committed output is lost by this guard.
    constexpr double limit = 1'000'000'000;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || x0 < -limit ||
        y0 < -limit || x1 > limit || y1 > limit || x1 < x0 || y1 < y0)
        return std::nullopt;
    return GridRect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
}

bool ValidExtent(GridRect extent) noexcept
{
    return extent.width > 0 && extent.height > 0 && extent.width <= kMaxCanvasSide && extent.height <= kMaxCanvasSide &&
           static_cast<std::int64_t>(extent.width) * extent.height <= kMaxLayerPixels;
}

const ImageLayer* MergeLayer(const CanvasDocument& source, const TransformEdit& edit, bool floating) noexcept
{
    if (!edit.floating || edit.mask || edit.group || !edit.floating->before || !edit.draft.IsValid() ||
        (edit.corners && !DistortWarp::IsUsable(edit.corners.value())) || edit.floating->sourceID == edit.layerID ||
        edit.floating->pixelWidth < 1 || edit.floating->pixelHeight < 1)
        return nullptr;
    const auto index = IndexOf(source.layers, floating ? edit.layerID : edit.floating->sourceID);
    const auto* layer = index ? &source.layers[index.value()] : nullptr;
    return layer && layer->asset && layer->asset->image.Format() == RasterFormat::Bgra8888 && layer->transform.IsValid()
               ? layer
               : nullptr;
}

std::optional<GridRect> MergeExtent(const ImageLayer& source, LayerTransform placed, int width, int height) noexcept
{
    const auto sourceMap = PixelToDocument(source.transform, source.asset->image.Width(), source.asset->image.Height());
    const auto mapped = MappedRect(PixelToDocument(placed, width, height).Concat(sourceMap.Inverted()), width, height);
    return mapped
               ? std::optional(GridRect{0, 0, source.asset->image.Width(), source.asset->image.Height()}.Union(*mapped))
               : std::nullopt;
}

struct PixelView final
{
    const std::uint8_t* data;
    int width, height, stride;
};

bool Sample(PixelView pixels, double x, double y, std::array<double, 4>& out) noexcept
{
    if (!(x >= 0) || !(y >= 0) || !(x < pixels.width) || !(y < pixels.height))
        return false;
    const auto sx = std::clamp(x - 0.5, 0.0, static_cast<double>(pixels.width - 1));
    const auto sy = std::clamp(y - 0.5, 0.0, static_cast<double>(pixels.height - 1));
    const auto fx = std::floor(sx), fy = std::floor(sy);
    const auto tx = sx - fx, ty = sy - fy;
    const auto ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const auto at = [&](int px, int py, int channel) noexcept -> double
    {
        return pixels.data[static_cast<std::size_t>(std::clamp(py, 0, pixels.height - 1)) * pixels.stride +
                           static_cast<std::size_t>(std::clamp(px, 0, pixels.width - 1)) * 4 + channel];
    };
    for (int channel = 0; channel < 4; ++channel)
    {
        const auto top = at(ix, iy, channel) + (at(ix + 1, iy, channel) - at(ix, iy, channel)) * tx;
        const auto bottom = at(ix, iy + 1, channel) + (at(ix + 1, iy + 1, channel) - at(ix, iy + 1, channel)) * tx;
        out[static_cast<std::size_t>(channel)] = top + (bottom - top) * ty;
    }
    return true;
}

bool MoveOutline(DocumentSelection& selection, const TransformEdit& edit, CancelToken cancellation)
{
    const auto& floating = edit.floating.value();
    const auto originalInverse =
        PixelToDocument(floating.original, floating.pixelWidth, floating.pixelHeight).Inverted();
    const auto affine = originalInverse.Concat(PixelToDocument(edit.draft, floating.pixelWidth, floating.pixelHeight));
    const auto perspective =
        edit.corners ? DistortWarp::MakeHomography(edit.corners.value()) : DistortWarp::Homography{};
    std::vector<SelectionLoop> loops;
    loops.reserve(selection.loops.size());
    for (const auto& sourceLoop : selection.loops)
    {
        loops.emplace_back();
        auto& loop = loops.back();
        loop.reserve(sourceLoop.size());
        for (auto point : sourceLoop)
        {
            if (cancellation.IsCancelled())
                return false;
            if (!edit.corners)
                point = affine.Apply(point);
            else
            {
                auto pixel = originalInverse.Apply(point);
                auto u = pixel.x / floating.pixelWidth, v = pixel.y / floating.pixelHeight;
                if (edit.draft.flipX)
                    u = 1 - u;
                if (edit.draft.flipY)
                    v = 1 - v;
                point = perspective.Map({u, v});
            }
            loop.push_back(point);
        }
    }
    selection.loops = std::move(loops);
    return !cancellation.IsCancelled();
}
} // namespace

std::uint64_t FloatingOpeningPixelsMemoryBytes(const CanvasDocument& source, Guid active) noexcept
{
    const auto* layer = OpeningLayer(source, active);
    if (!layer)
        return 0;
    auto bytes = CopyPixelsMemoryBytes(CopyInput(source, *layer));
    const auto clear = RasterEditMemoryBytes(*layer, source.selection, source.width, source.height,
                                             ImageBudget(source, active), false);
    return bytes && clear && Add(bytes, clear) && Add(bytes, 8192) ? bytes : 0;
}

FloatingOpeningPixels PrepareFloatingOpeningPixels(const CanvasDocument& source, Guid active, Executor& executor,
                                                   CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    const auto* layer = OpeningLayer(source, active);
    if (!layer || !FloatingOpeningPixelsMemoryBytes(source, active) || ValidateDocument(source) != DocumentStatus::Ok)
        return {};
    auto copied = RunCopyPixels(CopyInput(source, *layer), cancellation);
    if (copied.status != CopyPixelsStatus::Ready || !copied.clipboard)
        return {cancellation.IsCancelled() || copied.status == CopyPixelsStatus::Cancelled
                    ? FloatingPixelsStatus::Cancelled
                : copied.status == CopyPixelsStatus::Invalid ? FloatingPixelsStatus::Invalid
                                                             : FloatingPixelsStatus::Memory};
    auto cleared = RunRasterEdit(ClearInput(source, *layer), cancellation);
    if (cleared.status != RasterEditStatus::Ready || !cleared.layer)
        return {cancellation.IsCancelled() || cleared.status == RasterEditStatus::Cancelled
                    ? FloatingPixelsStatus::Cancelled
                : cleared.status == RasterEditStatus::Unchanged ? FloatingPixelsStatus::Unchanged
                : cleared.status == RasterEditStatus::TooLarge  ? FloatingPixelsStatus::TooLarge
                : cleared.status == RasterEditStatus::Invalid   ? FloatingPixelsStatus::Invalid
                                                                : FloatingPixelsStatus::Memory};
    auto before = std::make_shared<CanvasDocument>(source);
    auto after = source;
    const auto index = IndexOf(after.layers, active);
    after.layers[index.value()] = std::move(cleared.layer.value());
    ImageLayer floating =
        ImageLayer::Blank(L"Floating Selection", {static_cast<double>(copied.clipboard->image.Width()),
                                                  static_cast<double>(copied.clipboard->image.Height())});
    floating.asset = RasterAsset{copied.clipboard->image, {}, L"Floating Selection"};
    floating.transform.origin = copied.clipboard->origin;
    floating.parentID = layer->parentID;
    floating.opacity = layer->opacity;
    floating.blendMode = layer->blendMode;
    TransformEdit edit;
    edit.layerID = floating.id;
    edit.draft = floating.transform;
    edit.persistent = true;
    edit.floating = FloatingTransform{active,
                                      std::move(before),
                                      active,
                                      floating.transform,
                                      copied.clipboard->image.Width(),
                                      copied.clipboard->image.Height()};
    after.layers.insert(after.layers.begin() + static_cast<std::ptrdiff_t>(index.value()) + 1, std::move(floating));
    if (ValidateDocument(after) != DocumentStatus::Ok)
        return {FloatingPixelsStatus::TooLarge};
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    return {FloatingPixelsStatus::Ready, std::move(after), std::move(edit)};
}

std::uint64_t FloatingMergePixelsMemoryBytes(const CanvasDocument& source, const TransformEdit& edit) noexcept
{
    const auto* layer = MergeLayer(source, edit, false);
    const auto* floating = MergeLayer(source, edit, true);
    if (!layer || !floating)
        return 0;
    std::uint64_t bytes = 8192;
    auto placed = edit.draft;
    auto width = floating->asset->image.Width(), height = floating->asset->image.Height();
    if (edit.corners)
    {
        const auto warp =
            DistortWorkMemoryBytes({floating->asset->image, placed, edit.corners.value(), false, 0, 0, true});
        if (!warp || !Add(bytes, warp))
            return 0;
        const auto bounds = PointBounds(edit.corners.value());
        placed = {};
        placed.origin = {std::floor(bounds.minX), std::floor(bounds.minY)};
        placed.size = {std::ceil(bounds.maxX) - placed.origin.x, std::ceil(bounds.maxY) - placed.origin.y};
        width = static_cast<int>(placed.size.x);
        height = static_cast<int>(placed.size.y);
    }
    const auto extent = MergeExtent(*layer, placed, width, height);
    if (!extent || !ValidExtent(extent.value()))
        return 0;
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(extent->width) * extent->height * 4);
    const auto sample = PixelBuffer::RequiredBytes(static_cast<std::size_t>(width) * height * 4);
    // Mask growth/source assembly/thumbnail all fit three extra BGRA output peaks, conservatively.
    return output && sample && Add(bytes, output * (layer->mask ? 4ull : 1ull)) && Add(bytes, sample) ? bytes : 0;
}

FloatingMergePixels PrepareFloatingMergePixels(const CanvasDocument& source, const TransformEdit& edit,
                                               Executor& executor, CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    const auto* layer = MergeLayer(source, edit, false);
    const auto* floating = MergeLayer(source, edit, true);
    if (!layer || !floating || ValidateDocument(source) != DocumentStatus::Ok)
        return {};
    if (!edit.corners && edit.draft == edit.floating->original)
        return {FloatingPixelsStatus::Unchanged, *edit.floating->before, edit.floating->beforeActive};
    if (!FloatingMergePixelsMemoryBytes(source, edit))
        return {FloatingPixelsStatus::TooLarge, {}, {}, true};
    Raster pixels = floating->asset->image;
    auto placed = edit.draft;
    if (edit.corners)
    {
        auto warp = RunDistortWork({pixels, placed, edit.corners.value(), false, 0, 0, true}, executor, cancellation);
        if (warp.status != DistortWorkStatus::Ready)
            return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Memory};
        pixels = std::move(warp.image);
        placed = warp.placement;
    }
    auto extent = MergeExtent(*layer, placed, pixels.Width(), pixels.Height());
    if (!extent || !ValidExtent(extent.value()))
        return {FloatingPixelsStatus::TooLarge, {}, {}, true};
    const auto oldWidth = layer->asset->image.Width(), oldHeight = layer->asset->image.Height();
    const GridRect original{0, 0, oldWidth, oldHeight};
    const bool maskGrows =
        layer->mask && !layer->mask->placement && extent != original && layer->mask->asset.image.PixelCount() > 1;
    const auto outputCount = static_cast<std::int64_t>(extent->width) * extent->height;
    std::int64_t maskLeft = kMaxMaskPixels;
    for (const auto& candidate : source.layers)
        if (candidate.id != layer->id && candidate.id != floating->id && candidate.mask)
            maskLeft -= candidate.mask->asset.image.PixelCount();
    if (outputCount > ImageBudget(source, layer->id, floating->id) || (maskGrows && outputCount > maskLeft))
        return {FloatingPixelsStatus::TooLarge, {}, {}, true};
    pixels = ContiguousRasterForWork(pixels, cancellation);
    if (!pixels.HasStorage())
        return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Memory};
    auto bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(outputCount) * 4);
    if (!bytes)
        return {FloatingPixelsStatus::Memory};
    if (!ReadRasterRegionForWork(layer->asset->image, extent.value(), {bytes.Data(), bytes.Size()},
                                 static_cast<std::size_t>(extent->width) * 4, 0, cancellation))
        return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Memory};
    const auto toDocument = PixelToDocument(layer->transform, oldWidth, oldHeight);
    const auto toFloating = PixelToDocument(placed, pixels.Width(), pixels.Height()).Inverted();
    const PixelView samplePixels{pixels.Data(), pixels.Width(), pixels.Height(), pixels.Stride()};
    auto* output = bytes.Data();
    const bool complete = ParallelFor(
        executor, TileGrid{static_cast<std::uint32_t>(extent->width), static_cast<std::uint32_t>(extent->height)},
        cancellation,
        [&](Tile tile, CancelToken token) noexcept
        {
            for (auto y = tile.y; y < tile.y + tile.height; ++y)
            {
                if (token.IsCancelled())
                    return false;
                for (auto x = tile.x; x < tile.x + tile.width; ++x)
                {
                    const auto document = toDocument.Apply(
                        {extent->x + static_cast<double>(x) + 0.5, extent->y + static_cast<double>(y) + 0.5});
                    const auto point = toFloating.Apply(document);
                    std::array<double, 4> sample{};
                    if (!Sample(samplePixels, point.x, point.y, sample) || sample[3] <= 0)
                        continue;
                    auto* out = output + (static_cast<std::size_t>(y) * extent->width + x) * 4;
                    const auto hold = 1 - sample[3] / 255;
                    for (int channel = 0; channel < 4; ++channel)
                        out[channel] = static_cast<std::uint8_t>(std::lround(
                            std::clamp(sample[static_cast<std::size_t>(channel)] + out[channel] * hold, 0.0, 255.0)));
                }
            }
            return true;
        });
    if (!complete || cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    auto image = Raster::Adopt(extent->width, extent->height, RasterFormat::Bgra8888, std::move(bytes));
    if (!image.HasStorage())
        return {FloatingPixelsStatus::Memory};
    auto mask = layer->mask;
    if (maskGrows)
    {
        auto grown = CarriedMaskForWork(mask->asset.image, oldWidth, oldHeight, -extent->x, -extent->y, extent->width,
                                        extent->height, 255, cancellation);
        auto thumb = ThumbnailRasterForWork(grown, 96, cancellation);
        if (!grown.HasStorage() || !thumb.HasStorage())
            return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Memory};
        mask->asset.image = std::move(grown);
        mask->asset.thumbnail = std::move(thumb);
    }
    auto selection = source.selection;
    if (selection && !MoveOutline(selection.value(), edit, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    auto after = source;
    const auto floatingIndex = IndexOf(after.layers, edit.layerID);
    after.layers.erase(after.layers.begin() + static_cast<std::ptrdiff_t>(floatingIndex.value()));
    auto& installed = after.layers[IndexOf(after.layers, edit.floating->sourceID).value()];
    auto merged = layer->transform;
    merged.size = {extent->width * merged.size.x / oldWidth, extent->height * merged.size.y / oldHeight};
    const auto center = toDocument.Apply({extent->x + extent->width / 2.0, extent->y + extent->height / 2.0});
    merged.origin = {center.x - merged.size.x / 2, center.y - merged.size.y / 2};
    if (!merged.IsValid())
        return {};
    installed.asset = RasterAsset{std::move(image), {}, layer->name};
    installed.transform = merged;
    installed.mask = std::move(mask);
    after.selection = std::move(selection);
    if (ValidateDocument(after) != DocumentStatus::Ok)
        return {};
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    return {FloatingPixelsStatus::Ready, std::move(after), edit.floating->sourceID};
}

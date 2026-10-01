#include "RasterEditWork.h"

#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

class PixelMoveOpening final
{
  public:
    PixelMoveOpening(BrushStrokeOpening stroke, DocumentSelection selection, Guid target) noexcept
        : stroke(std::move(stroke)), selection(std::move(selection)), target(target)
    {
    }
    [[nodiscard]] bool Matches(const RasterEditInput& input) const noexcept
    {
        return input.layer.id == target && input.selection && input.selection->antialiased == selection.antialiased &&
               input.selection->loops.Identity() == selection.loops.Identity();
    }
    const BrushStrokeOpening stroke;
    const DocumentSelection selection;
    const Guid target;
};

namespace
{
[[nodiscard]] bool AddBytes(std::uint64_t& bytes, std::uint64_t value) noexcept
{
    if (value > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += value;
    return true;
}

[[nodiscard]] bool AddProduct(std::uint64_t& bytes, std::uint64_t count, std::uint64_t size) noexcept
{
    return (size == 0 || count <= (std::numeric_limits<std::uint64_t>::max)() / size) && AddBytes(bytes, count * size);
}

[[nodiscard]] bool FrameValid(const Raster& image, RasterFormat format) noexcept
{
    return image.HasStorage() && image.Format() == format && image.Width() > 0 && image.Height() > 0 &&
           image.Width() <= kMaxCanvasSide && image.Height() <= kMaxCanvasSide && image.PixelCount() <= kMaxLayerPixels;
}

[[nodiscard]] bool InputGeometryValid(const ImageLayer& layer, int width, int height, std::int64_t pixelLimit,
                                      bool mask) noexcept
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide || pixelLimit < 1 ||
        pixelLimit > kMaxLayerPixels || !layer.transform.IsValid())
        return false;
    if (mask)
        return layer.mask && FrameValid(layer.mask->asset.image, RasterFormat::Gray8) &&
               (!layer.mask->placement || layer.mask->placement->IsValid());
    return !layer.isGroup && !layer.adjustment &&
           (!layer.asset || FrameValid(layer.asset->image, RasterFormat::Bgra8888));
}

// Fresh raster-edit patches lie on one 256-pixel grid. Splitting any old window only introduces those grid
// boundary coordinates plus its two original edges. This bounds pieces even for an old window larger than a
// tile; a 256-pixel window can span at most three intervals per axis, regardless of its offset from the grid.
[[nodiscard]] bool AddSparseWindows(std::uint64_t& bytes, const Raster& source) noexcept
{
    if (!source.IsSparse())
        return true;
    for (const RasterTile& tile : source.Tiles())
    {
        if (tile.Width() < 1 || tile.Height() < 1 || tile.Width() > kMaxCanvasSide || tile.Height() > kMaxCanvasSide)
            return false;
        const auto columns = (static_cast<std::uint64_t>(tile.Width()) + 255) / 256 + 2;
        const auto rows = (static_cast<std::uint64_t>(tile.Height()) + 255) / 256 + 2;
        // Result/vector growth, older headers, and one old-window's piece/next scratch coexist.
        if (!AddProduct(bytes, columns * rows, 8 * sizeof(RasterTile)))
            return false;
    }
    return true;
}

[[nodiscard]] RasterEditStatus InputStatus(const RasterEditInput& input, CancelToken cancellation) noexcept
{
    if (cancellation.IsCancelled())
        return RasterEditStatus::Cancelled;
    const auto colorValid = [](const std::array<double, 4>& color) noexcept
    {
        return std::all_of(color.begin(), color.end(),
                           [](double value) noexcept { return std::isfinite(value) && value >= 0 && value <= 1; });
    };
    if (!InputGeometryValid(input.layer, input.canvasWidth, input.canvasHeight, input.pixelLimit, input.mask) ||
        input.kind > RasterEditKind::PixelMove || !colorValid(input.color))
        return RasterEditStatus::Invalid;
    if (input.kind == RasterEditKind::PixelMove)
    {
        if (input.mask || !input.layer.asset || !input.selection || !std::isfinite(input.offset.x) ||
            !std::isfinite(input.offset.y))
            return RasterEditStatus::Invalid;
        if (std::round(input.offset.x) == 0 && std::round(input.offset.y) == 0)
            return RasterEditStatus::Unchanged;
        BrushStroke probe(input.layer, false, BrushSettings{}, input.canvasWidth, input.canvasHeight, nullptr);
        if (!probe.IsValid())
            return RasterEditStatus::Invalid;
        const auto inverse = probe.PixelToDocument().Inverted();
        const auto zero = inverse.Apply({0, 0});
        const auto shifted = inverse.Apply({std::round(input.offset.x), std::round(input.offset.y)});
        const double landingX = probe.SourceRect().x + shifted.x - zero.x;
        const double landingY = probe.SourceRect().y + shifted.y - zero.y;
        // MoveLifted converts the landing box to a signed grid rectangle. Bound it before floor/cast/addition;
        // an off-canvas move may still clear the hole, but cannot overflow the grid representation.
        for (const double coordinate : {landingX, landingY, landingX + probe.SourceRect().width + 1.0,
                                        landingY + probe.SourceRect().height + 1.0})
            if (!std::isfinite(coordinate) || coordinate < -BrushStroke::kMaxGridSide ||
                coordinate > BrushStroke::kMaxGridSide)
                return RasterEditStatus::Invalid;
        std::size_t checked = 0;
        for (const auto& loop : input.selection->loops)
            for (const auto& point : loop)
            {
                if ((checked++ & 255u) == 0 && cancellation.IsCancelled())
                    return RasterEditStatus::Cancelled;
                if (!std::isfinite(point.x + std::round(input.offset.x)) ||
                    !std::isfinite(point.y + std::round(input.offset.y)))
                    return RasterEditStatus::Invalid;
            }
    }
    if (input.kind == RasterEditKind::Gradient)
    {
        const double dx = input.end.x - input.start.x;
        const double dy = input.end.y - input.start.y;
        if (input.shape > GradientShape::Radial || !std::isfinite(input.start.x) || !std::isfinite(input.start.y) ||
            !std::isfinite(input.end.x) || !std::isfinite(input.end.y) || !std::isfinite(dx * dx + dy * dy) ||
            !std::isfinite(input.opacity) || input.opacity < 0 || input.opacity > 1 || !colorValid(input.endColor))
            return RasterEditStatus::Invalid;
        if (std::hypot(dx, dy) < 0.5)
            return RasterEditStatus::Unchanged;
        if (input.shape == GradientShape::Linear)
        {
            // Shaded points are clipped to the document canvas. Linear projection is affine, so its extrema
            // occur at these four corners; a finite line length alone does not prevent far-origin overflow.
            for (const double x : {0.0, static_cast<double>(input.canvasWidth)})
                for (const double y : {0.0, static_cast<double>(input.canvasHeight)})
                    if (!std::isfinite((x - input.start.x) * dx + (y - input.start.y) * dy))
                        return RasterEditStatus::Invalid;
        }
    }
    return (input.kind == RasterEditKind::Clear && !input.selection) || (input.selection && input.selection->IsEmpty())
               ? RasterEditStatus::Unchanged
               : RasterEditStatus::Ready;
}

[[nodiscard]] RasterEditStatus PaintForWork(const RasterEditInput& input, BrushStroke& edit, CancelToken cancellation)
{
    if (!edit.IsValid() || !edit.ConfigureWorkerRasterEdit(cancellation, input.progress, input.progressContext))
        return RasterEditStatus::Invalid;
    edit.SetPixelLimit(input.pixelLimit);
    const bool reuseOpening = input.kind == RasterEditKind::PixelMove && input.pixelMoveOpening;
    if (reuseOpening)
    {
        if (!input.pixelMoveOpening->Matches(input) || !edit.UseLiftedOpening(input.pixelMoveOpening->stroke))
            return RasterEditStatus::Invalid;
    }
    else if (input.selection)
    {
        Raster coverage = SelectionGeometry::Coverage(input.selection->loops, input.canvasWidth, input.canvasHeight,
                                                      input.selection->antialiased, cancellation);
        if (cancellation.IsCancelled())
            return RasterEditStatus::Cancelled;
        if (!coverage.HasStorage())
            return RasterEditStatus::Memory;
        edit.SetSelectionClip(std::move(coverage));
    }
    if (input.kind == RasterEditKind::PixelMove && !reuseOpening)
    {
        bool noPixels = false;
        if (!edit.LiftSelection(&noPixels))
            return cancellation.IsCancelled() ? RasterEditStatus::Cancelled
                   : noPixels                 ? RasterEditStatus::Unchanged
                                              : RasterEditStatus::Memory;
    }
    const bool rendered =
        input.kind == RasterEditKind::PixelMove
            ? edit.MoveLifted({std::round(input.offset.x), std::round(input.offset.y)}, input.duplicate)
        : input.kind == RasterEditKind::Gradient
            ? edit.FillGradient(input.shape, input.start, input.end, input.color, input.endColor, input.opacity)
        : input.kind == RasterEditKind::Invert               ? edit.Invert()
        : input.kind == RasterEditKind::Clear && !input.mask ? edit.ClearPixels()
                                                             : edit.Fill(input.color);
    if (cancellation.IsCancelled())
        return RasterEditStatus::Cancelled;
    if (!rendered)
        return edit.PixelLimitRefused() ? RasterEditStatus::TooLarge : RasterEditStatus::Memory;
    return edit.HasPaint() ? RasterEditStatus::Ready : RasterEditStatus::Unchanged;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct OwnedPreviewStroke final : DeferredReleaseNode
{
    std::unique_ptr<BrushStroke> stroke;
    OwnedPreviewStroke() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedPreviewStroke> owned(static_cast<OwnedPreviewStroke*>(node)); };
    }
};
#pragma warning(pop)

struct RetirePreviewStroke final
{
    OwnedPreviewStroke* owner;
    void operator()(BrushStroke*) const noexcept
    {
        if (!TryDeferRelease(*owner))
        {
            const std::unique_ptr<OwnedPreviewStroke> owned(owner);
        }
    }
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct OwnedPixelMoveOpening final : DeferredReleaseNode
{
    std::optional<PixelMoveOpening> value;
    OwnedPixelMoveOpening() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedPixelMoveOpening> owned(static_cast<OwnedPixelMoveOpening*>(node)); };
    }
};
#pragma warning(pop)

struct RetirePixelMoveOpening final
{
    void operator()(OwnedPixelMoveOpening* node) const noexcept
    {
        if (node && !TryDeferRelease(*node))
        {
            const std::unique_ptr<OwnedPixelMoveOpening> owned(node);
        }
    }
};

[[nodiscard]] std::shared_ptr<const PixelMoveOpening> OpeningForWork(const RasterEditInput& input,
                                                                     const BrushStroke& stroke)
{
    if (input.kind != RasterEditKind::PixelMove)
        return {};
    if (input.pixelMoveOpening)
        return input.pixelMoveOpening;
    auto captured = stroke.CaptureLiftedOpening();
    if (!captured || !input.selection)
        return {};
    auto bytes = captured->RetirementMemoryBytes();
    if (!AddBytes(bytes, input.selection->loops.StorageBytes()) ||
        !AddBytes(bytes, sizeof(OwnedPixelMoveOpening) + 256))
        return {};
    std::unique_ptr<OwnedPixelMoveOpening, RetirePixelMoveOpening> owned(new OwnedPixelMoveOpening);
    owned->bytes = (std::max)(bytes, kDeferredReleaseThreshold);
    owned->value.emplace(std::move(captured.value()), input.selection.value(), input.layer.id);
    auto shared = std::shared_ptr<OwnedPixelMoveOpening>(owned.release(), RetirePixelMoveOpening{});
    return std::shared_ptr<const PixelMoveOpening>(shared, &shared->value.value());
}
} // namespace

std::uint64_t PixelMoveOpeningMemoryBytes(const std::shared_ptr<const PixelMoveOpening>& opening) noexcept
{
    if (!opening)
        return 0;
    auto bytes = opening->stroke.RetirementMemoryBytes();
    if (!AddBytes(bytes, opening->selection.loops.StorageBytes()) ||
        !AddBytes(bytes, sizeof(OwnedPixelMoveOpening) + 256))
        return 0;
    return (std::max)(bytes, kDeferredReleaseThreshold);
}

std::uint64_t RasterEditMemoryBytes(const ImageLayer& layer, const std::optional<DocumentSelection>& selection,
                                    int canvasWidth, int canvasHeight, std::int64_t pixelLimit, bool mask) noexcept
{
    if (!InputGeometryValid(layer, canvasWidth, canvasHeight, pixelLimit, mask))
        return 0;
    // A geometry-only CPU probe owns no pixel/coverage allocations and never reaches a graphics device.
    BrushStroke probe(layer, mask, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    if (!probe.IsValid())
        return 0;
    std::uint64_t bytes = 1024 * 1024;
    if (!AddProduct(bytes, layer.name.size(), 8 * sizeof(wchar_t)) ||
        (layer.asset && !AddProduct(bytes, layer.asset->name.size(), 4 * sizeof(wchar_t))) ||
        (layer.mask && !AddProduct(bytes, layer.mask->asset.name.size(), 8 * sizeof(wchar_t))) ||
        !AddBytes(bytes, 8 * sizeof(ImageLayer) + 4 * sizeof(JobToken)))
        return 0;
    if (layer.adjustment)
        for (const auto& channel : layer.adjustment->curves.channels)
            if (channel.size() > 32 || !AddProduct(bytes, channel.size(), 8 * sizeof(CurvePoint)))
                return 0;

    const auto gridPixels = static_cast<std::uint64_t>(probe.Width()) * probe.Height();
    const auto outputPixels = (std::min)(gridPixels, static_cast<std::uint64_t>(pixelLimit));
    const auto channels = mask ? 1ull : 4ull;
    // Private bases and result tile images coexist; current tile/vector/copy scratch fits the fixed allowance.
    if (!AddProduct(bytes, outputPixels, 2 * channels))
        return 0;
    const auto columns = (static_cast<std::uint64_t>((std::min)(probe.Width(), kMaxCanvasSide)) + 255) / 256 + 1;
    const auto rows = (static_cast<std::uint64_t>((std::min)(probe.Height(), kMaxCanvasSide)) + 255) / 256 + 1;
    if (!AddProduct(bytes, columns * rows, 4 * sizeof(BrushStroke::Tile) + 8 * sizeof(RasterTile) + 256))
        return 0;
    const Raster& source = mask ? layer.mask->asset.image : layer.asset ? layer.asset->image : Raster{};
    if (!AddSparseWindows(bytes, source))
        return 0;

    if (selection)
    {
        const auto coverage = PixelBuffer::RequiredBytes(static_cast<std::size_t>(canvasWidth) * canvasHeight);
        if (!AddBytes(bytes, coverage))
            return 0;
        if (selection->antialiased)
        {
            if (!AddProduct(bytes, (static_cast<std::uint64_t>(canvasWidth) + 2) * canvasHeight, 2 * sizeof(float)))
                return 0;
        }
        else if (!AddProduct(bytes, selection->loops.PointCount(), 32))
            return 0;
    }
    // A stretched target mask and a grown image's carried nonuniform mask each need a private full grid, final
    // cropped bytes and a source row. Inputs stay shared. These conservative planes include transient copies.
    if (layer.mask && (mask || (!layer.mask->placement && layer.mask->asset.image.PixelCount() > 1)))
    {
        if (!AddProduct(bytes, outputPixels, 4) || !AddSparseWindows(bytes, layer.mask->asset.image))
            return 0;
    }
    return bytes;
}

std::uint64_t PixelMoveMemoryBytes(const ImageLayer& layer, const DocumentSelection& selection, int canvasWidth,
                                   int canvasHeight, std::int64_t pixelLimit) noexcept
{
    if (!layer.asset || selection.IsEmpty())
        return 0;
    auto bytes = RasterEditMemoryBytes(layer, selection, canvasWidth, canvasHeight, pixelLimit, false);
    if (!bytes || !AddProduct(bytes, layer.asset->image.PixelCount(), 8) ||
        !AddProduct(bytes, selection.loops.PointCount(), 2 * sizeof(Vec2)) ||
        !AddProduct(bytes, selection.loops.size(), 2 * sizeof(SelectionLoop)))
        return 0;
    // Tile keys for landing, hole, current/previous landing and merged uniqueness coexist with tile storage.
    BrushStroke probe(layer, false, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    const auto columns = (static_cast<std::uint64_t>((std::min)(probe.Width(), kMaxCanvasSide)) + 255) / 256 + 1;
    const auto rows = (static_cast<std::uint64_t>((std::min)(probe.Height(), kMaxCanvasSide)) + 255) / 256 + 1;
    return AddProduct(bytes, columns * rows, 8 * sizeof(std::uint64_t)) &&
                   AddBytes(bytes, sizeof(OwnedPixelMoveOpening) + 256)
               ? bytes
               : 0;
}

RasterEditResult RunRasterEdit(const RasterEditInput& input, CancelToken cancellation)
{
    const auto cancelled = [&]() noexcept { return RasterEditResult{RasterEditStatus::Cancelled}; };
    const auto status = InputStatus(input, cancellation);
    if (status != RasterEditStatus::Ready)
        return {status};
    BrushStroke edit(input.layer, input.mask, BrushSettings{}, input.canvasWidth, input.canvasHeight, nullptr);
    const auto rendered = PaintForWork(input, edit, cancellation);
    if (rendered != RasterEditStatus::Ready)
        return {rendered};
    const auto snapshot = edit.PaintSnapshot();
    if (cancellation.IsCancelled())
        return cancelled();
    if (!snapshot || !snapshot->image.HasStorage() || !snapshot->thumbnail.HasStorage())
        return {RasterEditStatus::Memory};

    ImageLayer prepared = input.layer;
    if (input.mask)
    {
        prepared.mask->asset.image = snapshot->image;
        prepared.mask->asset.thumbnail = snapshot->thumbnail;
    }
    else
    {
        if (prepared.mask && !prepared.mask->placement && snapshot->bounds != edit.SourceRect() &&
            prepared.mask->asset.image.PixelCount() > 1)
        {
            const auto& source = edit.SourceRect();
            Raster grown = CarriedMaskForWork(prepared.mask->asset.image, source.width, source.height,
                                              source.x - snapshot->bounds.x, source.y - snapshot->bounds.y,
                                              snapshot->bounds.width, snapshot->bounds.height, 255, cancellation);
            if (cancellation.IsCancelled())
                return cancelled();
            if (!grown.HasStorage())
                return {RasterEditStatus::Memory};
            Raster thumbnail = ThumbnailRasterForWork(grown, 96, cancellation);
            if (cancellation.IsCancelled())
                return cancelled();
            if (!thumbnail.HasStorage())
                return {RasterEditStatus::Memory};
            prepared.mask->asset.image = std::move(grown);
            prepared.mask->asset.thumbnail = std::move(thumbnail);
        }
        prepared.asset = RasterAsset{snapshot->image, snapshot->thumbnail, prepared.name};
        prepared.transform = snapshot->transform;
    }
    if (cancellation.IsCancelled())
        return cancelled();
    std::optional<DocumentSelection> moved;
    if (input.kind == RasterEditKind::PixelMove)
    {
        std::vector<SelectionLoop> loops;
        loops.reserve(input.selection->loops.size());
        for (const auto& original : input.selection->loops)
        {
            loops.emplace_back();
            auto& loop = loops.back();
            loop.reserve(original.size());
            for (std::size_t index = 0; index < original.size(); ++index)
            {
                if ((index & 255u) == 0 && cancellation.IsCancelled())
                    return cancelled();
                loop.push_back(
                    {original[index].x + std::round(input.offset.x), original[index].y + std::round(input.offset.y)});
            }
        }
        moved = DocumentSelection{input.selection->antialiased, std::move(loops)};
    }
    if (cancellation.IsCancelled())
        return cancelled();
    auto opening = OpeningForWork(input, edit);
    if (input.kind == RasterEditKind::PixelMove && !opening)
        return {RasterEditStatus::Memory};
    if (cancellation.IsCancelled())
        return cancelled();
    return {RasterEditStatus::Ready, std::move(prepared),
            BrushCommitInfo{snapshot->image, snapshot->bounds.x, snapshot->bounds.y, snapshot->bounds.width,
                            snapshot->bounds.height, input.mask},
            std::move(moved), std::move(opening)};
}

RasterEditPreviewResult RunRasterEditPreview(const RasterEditInput& input, CancelToken cancellation)
{
    const auto status = InputStatus(input, cancellation);
    if (status != RasterEditStatus::Ready)
        return {status};
    auto owner = std::make_unique<OwnedPreviewStroke>();
    owner->stroke = std::make_unique<BrushStroke>(input.layer, input.mask, BrushSettings{}, input.canvasWidth,
                                                  input.canvasHeight, nullptr);
    const auto rendered = PaintForWork(input, *owner->stroke, cancellation);
    if (rendered != RasterEditStatus::Ready)
        return {rendered};
    auto opening = OpeningForWork(input, *owner->stroke);
    if (input.kind == RasterEditKind::PixelMove && !opening)
        return {RasterEditStatus::Memory};
    owner->stroke->EndWorkerPreparation(); // no callback/context or job cancellation state escapes Run
    // The declared whole preparation peak also bounds the privately owned stroke/coverage/tile graph. Retained
    // source storages were not charged as new allocations, but may lose their last other owner after replacement.
    owner->bytes = input.kind == RasterEditKind::PixelMove
                       ? PixelMoveMemoryBytes(input.layer, input.selection.value(), input.canvasWidth,
                                              input.canvasHeight, input.pixelLimit)
                       : RasterEditMemoryBytes(input.layer, input.selection, input.canvasWidth, input.canvasHeight,
                                               input.pixelLimit, input.mask);
    const auto retainedSource = [&](const Raster& image)
    {
        if (image.HasStorage() &&
            (!image.RetirementMemoryBytes() || !AddBytes(owner->bytes, image.RetirementMemoryBytes())))
            owner->bytes = (std::numeric_limits<std::uint64_t>::max)();
    };
    retainedSource(owner->stroke->SourceImage());
    if (input.layer.asset)
    {
        retainedSource(input.layer.asset->image);
        retainedSource(input.layer.asset->thumbnail);
    }
    if (input.layer.mask)
    {
        retainedSource(input.layer.mask->asset.image);
        retainedSource(input.layer.mask->asset.thumbnail);
    }
    if (!AddBytes(owner->bytes, PixelMoveOpeningMemoryBytes(opening)))
        owner->bytes = (std::numeric_limits<std::uint64_t>::max)();
    if (cancellation.IsCancelled())
        return {RasterEditStatus::Cancelled};
    // shared_ptr invokes its deleter if allocating its control block fails. Transfer the unique node exactly
    // once before construction; evaluating a release and dereference in different arguments is unsafe.
    auto* transferred = owner.release();
    return {RasterEditStatus::Ready,
            std::shared_ptr<BrushStroke>(transferred->stroke.get(), RetirePreviewStroke{transferred}),
            std::move(opening)};
}

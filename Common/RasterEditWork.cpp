#include "RasterEditWork.h"
#include "PixelMovePreviewWork.h"

#include "SharedPublicationMemory.h"

#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "LocallyOwnedPublication.h"
#include "PreparedRasterPreview.h"
#include "PublicationGraphAllocator.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <array>
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

std::shared_ptr<const PreparedPixelMovePreview> PixelMovePreviewReader::PrepareDisplayForWork(
    std::shared_ptr<const PixelMoveOpening> opening, Vec2 offset, bool duplicate, std::thread::id ownerThread,
    std::shared_ptr<const SharedPublicationMemory> memory, CancelToken cancellation)
{
    if (ownerThread == std::thread::id{} || ownerThread == std::this_thread::get_id() || !opening || !memory ||
        cancellation.IsCancelled() || !std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        (std::round(offset.x) == 0 && std::round(offset.y) == 0))
        return {};
    PixelMovePreviewReader validation(opening, offset, duplicate, ownerThread);
    if (!validation.IsValidForWork())
        return {};
    const auto& stroke = opening->stroke;
    PixelMovePreviewLayout layout;
    layout.target = opening->target;
    layout.source = stroke._source;
    layout.sourceRect = stroke._sourceRect;
    layout.sourceTransform = stroke._sourceTransform;
    layout.paintTransform = stroke._paintTransform;
    layout.width = stroke._width;
    layout.height = stroke._height;
    layout.canvasWidth = stroke._canvasWidth;
    layout.canvasHeight = stroke._canvasHeight;
    layout.sourceOwnershipBytes = PixelMoveOpeningMemoryBytes(opening);
    if (!layout.sourceOwnershipBytes || layout.width < 1 || layout.height < 1 || layout.width > kMaxCanvasSide ||
        layout.height > kMaxCanvasSide)
        return {};
    constexpr auto bytes = sizeof(PreparedPixelMovePreview) + 1024;
    auto result = MakeLocallyOwnedPublicationObject<const PreparedPixelMovePreview>(
        memory, bytes, 0, PreparedPixelMovePreview::ConstructionTag{}, memory, std::move(opening), std::move(layout),
        offset, duplicate, ownerThread);
    return cancellation.IsCancelled() ? nullptr : std::move(result);
}

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
    const auto cached = source.GridReplacementWorkspaceBytes();
    return cached && AddBytes(bytes, cached.value());
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
        const auto plan =
            BrushStroke::PlanGrid(input.layer, false, BrushSettings{}, input.canvasWidth, input.canvasHeight);
        if (!plan)
            return RasterEditStatus::Invalid;
        const auto& grid = plan.value();
        const auto inverse = grid.pixelToDocument.Inverted();
        const auto zero = inverse.Apply({0, 0});
        const auto shifted = inverse.Apply({std::round(input.offset.x), std::round(input.offset.y)});
        const double landingX = grid.sourceRect.x + shifted.x - zero.x;
        const double landingY = grid.sourceRect.y + shifted.y - zero.y;
        // MoveLifted converts the landing box to a signed grid rectangle. Bound it before floor/cast/addition;
        // an off-canvas move may still clear the hole, but cannot overflow the grid representation.
        for (const double coordinate :
             {landingX, landingY, landingX + grid.sourceRect.width + 1.0, landingY + grid.sourceRect.height + 1.0})
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
    if (!edit.IsValid() ||
        !edit.ConfigureWorkerRasterEdit(cancellation, input.progress, input.progressContext, input.outputMemory))
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
    std::shared_ptr<const SharedPublicationMemory> memory;
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
        if (!TryDeferRelease(*owner, owner->memory ? DeferredReleasePolicy::AlwaysOnOwner
                                                   : DeferredReleasePolicy::SizeThreshold))
        {
            const std::unique_ptr<OwnedPreviewStroke> owned(owner);
        }
    }
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct OwnedPixelMoveOpening final : DeferredReleaseNode
{
    std::shared_ptr<const SharedPublicationMemory> memory;
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
        if (node && !TryDeferRelease(*node, node->memory ? DeferredReleasePolicy::AlwaysOnOwner
                                                         : DeferredReleasePolicy::SizeThreshold))
        {
            const std::unique_ptr<OwnedPixelMoveOpening> owned(node);
        }
    }
};

[[nodiscard]] std::shared_ptr<const PixelMoveOpening> OpeningForWork(
    const RasterEditInput& input, const BrushStroke& stroke, std::shared_ptr<const SharedPublicationMemory> memory = {})
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
    std::unique_ptr<OwnedPixelMoveOpening, RetirePixelMoveOpening> owned(
        std::make_unique<OwnedPixelMoveOpening>().release());
    owned->memory = memory;
    owned->bytes = (std::max)(bytes, kDeferredReleaseThreshold);
    owned->value.emplace(std::move(captured.value()), input.selection.value(), input.layer.id);
    if (memory)
    {
        auto shared = std::shared_ptr<OwnedPixelMoveOpening>(owned.release(), RetirePixelMoveOpening{},
                                                             PublicationGraphAllocator<OwnedPixelMoveOpening>{memory});
        return std::shared_ptr<const PixelMoveOpening>(shared, &shared->value.value());
    }
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

PixelMoveLiftResult PreparePixelMoveLiftForWork(const RasterEditInput& input, CancelToken cancellation,
                                                std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    if (input.kind != RasterEditKind::PixelMove || input.mask || !input.layer.asset || !input.selection ||
        input.selection->IsEmpty() || input.pixelMoveOpening ||
        !InputGeometryValid(input.layer, input.canvasWidth, input.canvasHeight, input.pixelLimit, false))
        return {PixelMoveLiftStatus::Invalid};
    std::size_t checked = 0;
    for (const auto& loop : input.selection->loops)
        for (const auto& point : loop)
        {
            if ((checked++ & 255u) == 0 && cancellation.IsCancelled())
                return {PixelMoveLiftStatus::Cancelled};
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return {PixelMoveLiftStatus::Invalid};
        }
    BrushStroke stroke(input.layer, false, BrushSettings{}, input.canvasWidth, input.canvasHeight, nullptr);
    if (!stroke.IsValid() || !stroke.ConfigureWorkerRasterEdit(cancellation, input.progress, input.progressContext))
        return {PixelMoveLiftStatus::Invalid};
    stroke.SetPixelLimit(input.pixelLimit);
    Raster coverage = SelectionGeometry::Coverage(input.selection->loops, input.canvasWidth, input.canvasHeight,
                                                  input.selection->antialiased, cancellation);
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    if (!coverage.HasStorage())
        return {PixelMoveLiftStatus::Memory};
    stroke.SetSelectionClip(std::move(coverage));
    bool noPixels = false;
    if (!stroke.LiftSelection(&noPixels))
        return {cancellation.IsCancelled()   ? PixelMoveLiftStatus::Cancelled
                : noPixels                   ? PixelMoveLiftStatus::NoPixels
                : stroke.PixelLimitRefused() ? PixelMoveLiftStatus::TooLarge
                                             : PixelMoveLiftStatus::Memory};
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    auto opening = OpeningForWork(input, stroke, std::move(memory));
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    if (!opening)
        return {PixelMoveLiftStatus::Memory};
    return {PixelMoveLiftStatus::Ready, std::move(opening)};
}

std::uint64_t RasterEditMemoryBytes(const ImageLayer& layer, const std::optional<DocumentSelection>& selection,
                                    int canvasWidth, int canvasHeight, std::int64_t pixelLimit, bool mask) noexcept
{
    if (!InputGeometryValid(layer, canvasWidth, canvasHeight, pixelLimit, mask))
        return 0;
    // Scalar planning creates no stroke, hash containers, source owner or engine lifetime before admission.
    const auto plan = BrushStroke::PlanGrid(layer, mask, BrushSettings{}, canvasWidth, canvasHeight);
    if (!plan)
        return 0;
    const auto& grid = plan.value();
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

    const auto gridPixels = static_cast<std::uint64_t>(grid.width) * grid.height;
    const auto outputPixels = (std::min)(gridPixels, static_cast<std::uint64_t>(pixelLimit));
    const auto channels = mask ? 1ull : 4ull;
    // Private bases and result tile images coexist; current tile/vector/copy scratch fits the fixed allowance.
    if (!AddProduct(bytes, outputPixels, 2 * channels))
        return 0;
    const auto columns = (static_cast<std::uint64_t>((std::min)(grid.width, kMaxCanvasSide)) + 255) / 256 + 1;
    const auto rows = (static_cast<std::uint64_t>((std::min)(grid.height, kMaxCanvasSide)) + 255) / 256 + 1;
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
    const auto plan = BrushStroke::PlanGrid(layer, false, BrushSettings{}, canvasWidth, canvasHeight);
    if (!plan)
        return 0;
    const auto& grid = plan.value();
    const auto columns = (static_cast<std::uint64_t>((std::min)(grid.width, kMaxCanvasSide)) + 255) / 256 + 1;
    const auto rows = (static_cast<std::uint64_t>((std::min)(grid.height, kMaxCanvasSide)) + 255) / 256 + 1;
    return AddProduct(bytes, columns * rows, 8 * sizeof(std::uint64_t)) &&
                   AddBytes(bytes, sizeof(OwnedPixelMoveOpening) + 256)
               ? bytes
               : 0;
}

RasterEditResult RunRasterEdit(const RasterEditInput& input, CancelToken cancellation)
{
    const auto cancelled = [&]() noexcept { return RasterEditResult{RasterEditStatus::Cancelled}; };
    if (input.outputMemory && input.kind != RasterEditKind::PixelMove)
        return {RasterEditStatus::Invalid};
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
    if (!snapshot || !snapshot->image.HasStorage() || (input.mask && !snapshot->thumbnail.HasStorage()))
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
            Raster grown =
                CarriedMaskForWork(prepared.mask->asset.image, source.width, source.height,
                                   source.x - snapshot->bounds.x, source.y - snapshot->bounds.y, snapshot->bounds.width,
                                   snapshot->bounds.height, 255, cancellation, input.outputMemory);
            if (cancellation.IsCancelled())
                return cancelled();
            if (!grown.HasStorage())
                return {RasterEditStatus::Memory};
            Raster thumbnail = ThumbnailRasterForWork(grown, 96, cancellation, input.outputMemory);
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
        if (input.outputMemory)
        {
            auto geometry = SelectionLoops::AdoptForWork(std::move(loops), cancellation, input.outputMemory);
            if (!geometry)
                return cancellation.IsCancelled() ? cancelled() : RasterEditResult{RasterEditStatus::Memory};
            moved = DocumentSelection{input.selection->antialiased, std::move(*geometry)};
        }
        else
            moved = DocumentSelection{input.selection->antialiased, std::move(loops)};
    }
    if (cancellation.IsCancelled())
        return cancelled();
    auto opening = OpeningForWork(input, edit, input.outputMemory);
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
    if (input.prepareDisplay &&
        (input.ownerThread == std::thread::id{} || input.ownerThread == std::this_thread::get_id()))
        return {RasterEditStatus::Invalid};
    const auto status = InputStatus(input, cancellation);
    if (status != RasterEditStatus::Ready)
        return {status};
    auto owner = std::make_unique<OwnedPreviewStroke>();
    auto memory = input.prepareDisplay ? SharedPublicationMemory::CreateForWork() : nullptr;
    owner->memory = memory;
    owner->stroke = std::make_unique<BrushStroke>(input.layer, input.mask, BrushSettings{}, input.canvasWidth,
                                                  input.canvasHeight, nullptr);
    const auto rendered = PaintForWork(input, *owner->stroke, cancellation);
    if (rendered != RasterEditStatus::Ready)
        return {rendered};
    auto opening = OpeningForWork(input, *owner->stroke, memory);
    if (input.kind == RasterEditKind::PixelMove && !opening)
        return {RasterEditStatus::Memory};
    owner->stroke->EndWorkerPreparation(); // no callback/context or job cancellation state escapes Run
    auto display = input.prepareDisplay
                       ? PrepareRasterPreviewForWork(*owner->stroke, input.ownerThread, cancellation, memory)
                       : nullptr;
    if (input.prepareDisplay && !display)
        return {cancellation.IsCancelled() ? RasterEditStatus::Cancelled : RasterEditStatus::Invalid};
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
    if (input.prepareDisplay)
        return {RasterEditStatus::Ready,
                std::shared_ptr<BrushStroke>(transferred->stroke.get(), RetirePreviewStroke{transferred},
                                             PublicationGraphAllocator<BrushStroke>{memory}),
                std::move(opening), std::move(memory), std::move(display)};
    return {RasterEditStatus::Ready,
            std::shared_ptr<BrushStroke>(transferred->stroke.get(), RetirePreviewStroke{transferred}),
            std::move(opening)};
}

PixelMovePreviewReader::PixelMovePreviewReader(std::shared_ptr<const PixelMoveOpening> opening, Vec2 offset,
                                               bool duplicate, std::thread::id ownerThread) noexcept
    : _opening(std::move(opening)), _owner(ownerThread), _duplicate(duplicate)
{
    if (!_opening || _owner == std::thread::id{} || !std::isfinite(offset.x) || !std::isfinite(offset.y))
        return;
    const auto& source = _opening->stroke;
    if (!source._source.HasStorage() || source._source.Format() != RasterFormat::Bgra8888 ||
        !source._coverage.HasStorage() || source._coverage.IsSparse() ||
        source._coverage.Format() != RasterFormat::Gray8 || !source._lifted.HasStorage() || source._lifted.IsSparse() ||
        source._lifted.Format() != RasterFormat::Bgra8888 || source._coverage.Width() != source._canvasWidth ||
        source._coverage.Height() != source._canvasHeight || source._lifted.Width() != source._liftedRect.width ||
        source._lifted.Height() != source._liftedRect.height || source._width < 1 || source._height < 1 ||
        source._retirementMemoryBytes == 0)
        return;
    _offset = {std::round(offset.x), std::round(offset.y)};
    const auto inverse = source._pixelToDocument.Inverted();
    const auto zero = inverse.Apply({0, 0});
    const auto shifted = inverse.Apply(_offset);
    _landing = {source._liftedRect.x + shifted.x - zero.x, source._liftedRect.y + shifted.y - zero.y};
    for (const double coordinate : {_landing.x, _landing.y, _landing.x + source._liftedRect.width + 1.0,
                                    _landing.y + source._liftedRect.height + 1.0})
        if (!std::isfinite(coordinate) || coordinate < -BrushStroke::kMaxGridSide ||
            coordinate > BrushStroke::kMaxGridSide)
            return;
    _whole = _landing.x == std::round(_landing.x) && _landing.y == std::round(_landing.y);
    _valid = true;
}

bool PixelMovePreviewReader::IsValidForWork() const noexcept
{
    return _valid && _opening && _owner != std::this_thread::get_id();
}

bool PixelMovePreviewReader::Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                                  CancelToken cancellation, PixelMovePreviewObserver observer) const noexcept
{
    if (!IsValidForWork() || cancellation.IsCancelled())
        return false;
    const auto& source = _opening->stroke;
    if (region.x < 0 || region.y < 0 || region.width < 1 || region.height < 1 || region.x > source._width ||
        region.y > source._height || region.width > source._width - region.x ||
        region.height > source._height - region.y)
        return false;
    const auto rowBytes = static_cast<std::size_t>(region.width) * 4;
    const auto lastRow = static_cast<std::size_t>(region.height - 1);
    if (stride < rowBytes || (lastRow && stride > ((std::numeric_limits<std::size_t>::max)() - rowBytes) / lastRow) ||
        output.size() < lastRow * stride + rowBytes)
        return false;
    const auto sourceX = static_cast<std::int64_t>(region.x) - source._sourceRect.x;
    const auto sourceY = static_cast<std::int64_t>(region.y) - source._sourceRect.y;
    if (sourceX < (std::numeric_limits<int>::min)() || sourceX > (std::numeric_limits<int>::max)() ||
        sourceY < (std::numeric_limits<int>::min)() || sourceY > (std::numeric_limits<int>::max)())
        return false;
    if (!ReadRasterRegionForWork(source._source,
                                 {static_cast<int>(sourceX), static_cast<int>(sourceY), region.width, region.height},
                                 output, stride, 0, cancellation))
        return false;
    // A zero-return frame must show the original. Reapplying an antialiased hole/landing at zero would darken it.
    if (_offset == Vec2{})
        return !cancellation.IsCancelled();
    const auto* coverage = source._coverage.Data();
    const auto* lifted = source._lifted.Data();
    if (!coverage || !lifted)
        return false;
    const int liftedWidth = source._lifted.Width(), liftedHeight = source._lifted.Height();
    const auto liftStride = static_cast<std::size_t>(source._lifted.Stride());
    const auto at = [&](int x, int y, std::size_t channel) noexcept -> double
    {
        x = std::clamp(x, 0, liftedWidth - 1);
        y = std::clamp(y, 0, liftedHeight - 1);
        return lifted[static_cast<std::size_t>(y) * liftStride + static_cast<std::size_t>(x) * 4 + channel];
    };
    for (int y = 0; y < region.height; ++y)
    {
        if (cancellation.IsCancelled())
            return false;
        auto* row = output.data() + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < region.width; ++x)
        {
            if ((x & 255) == 0 && cancellation.IsCancelled())
                return false;
            const int gx = region.x + x, gy = region.y + y;
            auto* pixel = row + static_cast<std::size_t>(x) * 4;
            if (!_duplicate)
            {
                const auto document = source._pixelToDocument.Apply({gx + 0.5, gy + 0.5});
                if (document.x >= 0 && document.y >= 0 && document.x < source._canvasWidth &&
                    document.y < source._canvasHeight)
                {
                    const auto cx = static_cast<std::size_t>(std::floor(document.x));
                    const auto cy = static_cast<std::size_t>(std::floor(document.y));
                    const double hold =
                        1.0 - coverage[cy * static_cast<std::size_t>(source._coverage.Stride()) + cx] / 255.0;
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        pixel[channel] = static_cast<std::uint8_t>(std::lround(pixel[channel] * hold));
                }
            }
            const double sx = gx + 0.5 - _landing.x, sy = gy + 0.5 - _landing.y;
            if (!(sx >= 0) || !(sy >= 0) || !(sx < liftedWidth) || !(sy < liftedHeight))
                continue;
            std::array<double, 4> sample{};
            if (_whole)
            {
                const auto ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
                for (std::size_t channel = 0; channel < 4; ++channel)
                    sample[channel] = at(ix, iy, channel);
            }
            else
            {
                const double fx = std::clamp(sx - 0.5, 0.0, static_cast<double>(liftedWidth - 1));
                const double fy = std::clamp(sy - 0.5, 0.0, static_cast<double>(liftedHeight - 1));
                const auto ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                const double tx = fx - ix, ty = fy - iy;
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    const double top = at(ix, iy, channel) + (at(ix + 1, iy, channel) - at(ix, iy, channel)) * tx;
                    const double bottom =
                        at(ix, iy + 1, channel) + (at(ix + 1, iy + 1, channel) - at(ix, iy + 1, channel)) * tx;
                    sample[channel] = top + (bottom - top) * ty;
                }
            }
            if (sample[3] <= 0)
                continue;
            const double hold = 1.0 - sample[3] / 255.0;
            for (std::size_t channel = 0; channel < 4; ++channel)
                pixel[channel] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(sample[channel] + pixel[channel] * hold, 0.0, 255.0)));
        }
        if (observer.progress)
            observer.progress(observer.context, static_cast<std::size_t>(y + 1),
                              static_cast<std::size_t>(region.height));
    }
    return !cancellation.IsCancelled();
}

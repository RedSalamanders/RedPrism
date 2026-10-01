#include "ShapeEditWork.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
constexpr double kPi = 3.14159265358979323846;

bool ValidShape(ShapeKind kind, int width, int height, double radius) noexcept
{
    return (kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse) && width >= 1 && height >= 1 &&
           width <= kMaxCanvasSide && height <= kMaxCanvasSide && std::isfinite(radius) &&
           static_cast<std::int64_t>(width) * height <= DocumentSession::kMaxShapePixels;
}

double Radius(int width, int height, double radius) noexcept
{
    return (std::min)({(std::max)(0.0, radius), width / 2.0, height / 2.0});
}

bool Checkpoint(ShapeWorkObserver observer, ShapeWorkStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

std::vector<SelectionLoop> RoundedShapeLoops(int width, int height, double radius, CancelToken cancellation)
{
    const int segments = (std::max)(4, static_cast<int>(std::ceil(radius * kPi / 4.0)));
    SelectionLoop loop;
    loop.reserve(4ull * (static_cast<std::size_t>(segments) + 1));
    const auto arc = [&](double cx, double cy, double from)
    {
        for (int i = 0; i <= segments; ++i)
        {
            if (cancellation.IsCancelled())
                return false;
            const double angle = from + (kPi / 2.0) * i / segments;
            loop.push_back(Vec2{cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
        }
        return true;
    };
    // Preserve the established clockwise edge order and exactly the same trigonometric operations.
    if (!arc(width - radius, radius, -kPi / 2.0) || !arc(width - radius, height - radius, 0.0) ||
        !arc(radius, height - radius, kPi / 2.0) || !arc(radius, radius, kPi))
        return {};
    std::vector<SelectionLoop> loops;
    loops.reserve(1);
    loops.push_back(std::move(loop));
    return loops;
}

bool AddBytes(std::uint64_t& bytes, std::uint64_t amount) noexcept
{
    if (!amount || amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += amount;
    return true;
}
} // namespace

ShapeEditStatus InspectShapeDraft(const ShapeDraft& draft, int& width, int& height) noexcept
{
    width = height = 0;
    if ((draft.kind != ShapeKind::Rectangle && draft.kind != ShapeKind::Ellipse) || !std::isfinite(draft.rect.minX) ||
        !std::isfinite(draft.rect.minY) || !std::isfinite(draft.rect.maxX) || !std::isfinite(draft.rect.maxY) ||
        !std::isfinite(draft.cornerRadius))
        return ShapeEditStatus::Invalid;
    const double w = draft.rect.maxX - draft.rect.minX;
    const double h = draft.rect.maxY - draft.rect.minY;
    if (!std::isfinite(w) || !std::isfinite(h))
        return ShapeEditStatus::Invalid;
    if (w < 1 || h < 1)
        return ShapeEditStatus::Unchanged;
    const double roundedW = std::round(w);
    const double roundedH = std::round(h);
    if (roundedW > kMaxCanvasSide || roundedH > kMaxCanvasSide ||
        roundedW * roundedH > DocumentSession::kMaxShapePixels)
        return ShapeEditStatus::TooLarge;
    width = static_cast<int>(roundedW);
    height = static_cast<int>(roundedH);
    return ShapeEditStatus::Ready;
}

std::uint64_t ShapeImageMemoryBytes(ShapeKind kind, int width, int height, double cornerRadius) noexcept
{
    if (!ValidShape(kind, width, height, cornerRadius))
        return 0;
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels * 4));
    const double radius = Radius(width, height, cornerRadius);
    if (kind == ShapeKind::Rectangle && radius == 0)
        return output + 4096; // Full integer box: coverage is exactly 255, with no geometry or Gray8 buffer.
    const auto coverage = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels));
    const auto cells = 2ull * sizeof(float) * (static_cast<std::uint64_t>(width) + 2) * height;
    const auto points = kind == ShapeKind::Ellipse
                            ? 4096ull // Ellipse's current return makes one extra copy of its at most 2048 points.
                            : 4ull * (static_cast<std::uint64_t>((std::max)(4.0, std::ceil(radius * kPi / 4.0))) + 1);
    return coverage + (std::max)(cells, output) + points * sizeof(Vec2) + 4096;
}

Raster ShapeImageForWork(ShapeKind kind, int width, int height, std::uint32_t foregroundArgb, double cornerRadius,
                         Executor& executor, CancelToken cancellation, ShapeWorkObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!ValidShape(kind, width, height, cornerRadius) || !Checkpoint(observer, ShapeWorkStage::Geometry, cancellation))
        return {};
    const double radius = Radius(width, height, cornerRadius);
    const bool solid = kind == ShapeKind::Rectangle && radius == 0;
    Raster coverage;
    if (!solid)
    {
        auto loops = kind == ShapeKind::Ellipse ? SelectionGeometry::Ellipse(0, 0, width, height)
                                                : RoundedShapeLoops(width, height, radius, cancellation);
        if (loops.empty() || !Checkpoint(observer, ShapeWorkStage::Coverage, cancellation))
            return {};
        coverage = SelectionGeometry::Coverage(loops, width, height, true, cancellation);
        if (!coverage.HasStorage())
            return {};
    }
    else if (!Checkpoint(observer, ShapeWorkStage::Coverage, cancellation))
        return {};
    if (!Checkpoint(observer, ShapeWorkStage::Pixels, cancellation))
        return {};
    const auto* mask = solid ? nullptr : coverage.Data();
    const std::array<double, 3> color{((foregroundArgb >> 16) & 255u) / 255.0, ((foregroundArgb >> 8) & 255u) / 255.0,
                                      (foregroundArgb & 255u) / 255.0};
    auto bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * height * 4);
    if (!bytes)
        return {};
    const bool complete = ParallelFor(
        executor, TileGrid{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}, cancellation,
        [&](Tile tile, CancelToken token) noexcept
        {
            for (auto y = tile.y; y < tile.y + tile.height; ++y)
            {
                if (token.IsCancelled())
                    return false;
                for (auto x = tile.x; x < tile.x + tile.width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y) * width + x;
                    const double c = solid ? 1.0 : mask[index] / 255.0;
                    auto* out = bytes.Data() + index * 4;
                    out[0] = static_cast<std::uint8_t>(std::lround(color[2] * 255.0 * c));
                    out[1] = static_cast<std::uint8_t>(std::lround(color[1] * 255.0 * c));
                    out[2] = static_cast<std::uint8_t>(std::lround(color[0] * 255.0 * c));
                    out[3] = static_cast<std::uint8_t>(std::lround(255.0 * c));
                }
            }
            return true;
        });
    return complete && !cancellation.IsCancelled()
               ? Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(bytes))
               : Raster{};
}

std::optional<ShapeEditInput> DocumentSession::PrepareShapeEditInput(const ShapeDraft& draft,
                                                                     std::uint32_t foregroundArgb) const
{
    if (!ShapeEditPreparationMemoryBytes(draft))
        return std::nullopt;
    return ShapeEditInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID, history.CaptureState(),
                          draft, foregroundArgb);
}

ShapeEditResult DocumentSession::PrepareShapeEditPublication(ShapeEditInput input, Executor& executor,
                                                             CancelToken cancellation, ShapeWorkObserver observer,
                                                             DocumentHistory::PrepareObserver historyObserver)
{
    if (!input._valid)
        return {};
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {ShapeEditStatus::Cancelled};
    int width = 0;
    int height = 0;
    const auto status = InspectShapeDraft(input._draft, width, height);
    if (status != ShapeEditStatus::Ready)
        return {status};
    std::int64_t used = 0;
    std::unordered_set<std::wstring> names;
    names.reserve(input._source.layers.size());
    std::optional<std::size_t> activeIndex;
    for (std::size_t i = 0; i < input._source.layers.size(); ++i)
    {
        if (cancellation.IsCancelled())
            return {ShapeEditStatus::Cancelled};
        const auto& layer = input._source.layers[i];
        names.insert(layer.name);
        if (layer.asset)
            used += layer.asset->image.PixelCount();
        if (input._active == layer.id)
            activeIndex = i;
    }
    if (input._source.layers.size() >= kMaxLayerCount ||
        static_cast<std::int64_t>(width) * height > kMaxLayerPixels - used)
        return {ShapeEditStatus::TooLarge};
    if (input._active && !activeIndex)
        return {};
    const auto image = ShapeImageForWork(input._draft.kind, width, height, input._foreground, input._draft.cornerRadius,
                                         executor, cancellation, observer);
    if (!image.HasStorage())
        return {cancellation.IsCancelled() ? ShapeEditStatus::Cancelled : ShapeEditStatus::Memory};
    const auto prefix = input._draft.kind == ShapeKind::Rectangle ? L"Rectangle " : L"Ellipse ";
    std::wstring name;
    for (std::size_t number = 1; number <= input._source.layers.size() + 1; ++number)
    {
        if (cancellation.IsCancelled())
            return {ShapeEditStatus::Cancelled};
        name = prefix + std::to_wstring(number);
        if (!names.contains(name))
            break;
    }
    auto added = ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
    added.asset = RasterAsset{image, {}, std::move(name)};
    added.transform.origin = Vec2{input._draft.rect.minX, input._draft.rect.minY};
    added.shape = LayerShapeStyle{input._draft.kind, ((input._foreground >> 16) & 255u) / 255.0,
                                  ((input._foreground >> 8) & 255u) / 255.0, (input._foreground & 255u) / 255.0,
                                  input._draft.cornerRadius};
    added.shapeImage = image;
    std::size_t insertion = input._source.layers.size();
    if (activeIndex)
    {
        const auto& active = input._source.layers[activeIndex.value()];
        added.parentID = active.isGroup ? input._active : active.parentID;
        insertion = activeIndex.value() + 1;
        if (active.isGroup)
        {
            const auto descendants = DescendantIds(input._source.layers, active.id);
            for (std::size_t i = 0; i < input._source.layers.size(); ++i)
            {
                if (cancellation.IsCancelled())
                    return {ShapeEditStatus::Cancelled};
                if (descendants.contains(input._source.layers[i].id))
                    insertion = (std::max)(insertion, i + 1);
            }
        }
    }
    const auto active = added.id;
    const auto parent = added.parentID;
    auto after = input._source;
    // Metadata construction and vector growth stay on this worker. Selection geometry shares its immutable root.
    after.layers.insert(after.layers.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(added));
    if (!Checkpoint(observer, ShapeWorkStage::Document, cancellation))
        return {ShapeEditStatus::Cancelled};
    ImageImportBatchResult imported;
    imported.status = ProjectIoStatus::Ok;
    imported.importedDocument = std::move(after);
    imported.importedActiveLayerID = active;
    imported.historyName = input._draft.kind == ShapeKind::Rectangle ? L"Rectangle" : L"Ellipse";
    auto publication =
        PrepareImageImportPublication(std::move(input._authority), std::move(input._source), input._active,
                                      std::move(input._history), std::move(imported), cancellation, historyObserver);
    if (!publication || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? ShapeEditStatus::Cancelled : ShapeEditStatus::Invalid};
    ShapeEditResult result;
    result.status = ShapeEditStatus::Ready;
    result.publication.emplace(PreparedShapeEdit(std::move(publication.value()), parent));
    return result;
}

bool DocumentSession::ApplyPreparedShapeEditIfCurrent(PreparedShapeEdit& prepared) noexcept
{
    if (_transformEdit || _gradient || _pixelMove || _shapeDraft || _brush || _warp || _colorEdit ||
        !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    if (prepared._parent)
        _collapsed.erase(prepared._parent.value());
    return true;
}

std::uint64_t DocumentSession::ShapeEditPreparationMemoryBytes(const ShapeDraft& draft) const noexcept
{
    int width = 0;
    int height = 0;
    if (!_document || _transformEdit || _gradient || _pixelMove || _brush || _warp || _colorEdit ||
        _document->layers.size() >= kMaxLayerCount ||
        InspectShapeDraft(draft, width, height) != ShapeEditStatus::Ready ||
        static_cast<std::int64_t>(width) * height > PixelBudgetLeft(false))
        return 0;
    auto bytes = ShapeImageMemoryBytes(draft.kind, width, height, draft.cornerRadius);
    const auto historyBytes = history.EditMemoryBytes(_document);
    return bytes && historyBytes && AddBytes(bytes, historyBytes) &&
                   AddBytes(bytes, 8 * (sizeof(ImageLayer) + 4096ull)) &&
                   AddBytes(bytes, (_document->layers.size() + 1) * (sizeof(Guid) * 8ull + 512)) &&
                   AddBytes(bytes, sizeof(ShapeEditInput) + sizeof(PreparedShapeEdit) + 4096)
               ? bytes
               : 0;
}

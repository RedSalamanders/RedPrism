#include "WarpOpeningWork.h"

#include "PixelBuffer.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
bool OptionsValid(const WarpOpeningOptions& options) noexcept
{
    constexpr auto centerLimit = static_cast<double>((std::numeric_limits<int>::max)()) / 2.0;
    return options.settings.IsValid() && (options.mode == SmearMode::Liquify || options.mode == SmearMode::Smudge) &&
           std::isfinite(options.point.x) && std::isfinite(options.point.y) &&
           std::abs(options.point.x) <= centerLimit && std::abs(options.point.y) <= centerLimit &&
           options.ownerThread != std::thread::id{};
}

bool Checkpoint(WarpOpeningStage stage, CancelToken cancellation, WarpOpeningObserver observer) noexcept
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

std::optional<WarpOpeningInput> DocumentSession::PrepareWarpOpeningInput(WarpOpeningOptions options) const noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    // This boundary settles no other gesture and executes no visibility, coverage or pixel traversal.
    if (!_document || !_activeLayerID || _maskSelected || _brush || _brushFinishingOpening || _brushUpdatingOpening ||
        _transformEdit || _gradient || _pixelMove || HasPendingWarp() || _colorEdit || _shapeDraft || _lassoDraft ||
        _selectionMoveOrigin || _warpOpening == UINT64_MAX || !OptionsValid(options) ||
        options.ownerThread != std::this_thread::get_id() || !WarpOpeningMemoryBytes(options))
        return std::nullopt;
    const auto* target = _document->layers.FindKey(_activeLayerID.value());
    if (!target || target->isGroup || target->adjustment || (_document->selection && _document->selection->IsEmpty()))
        return std::nullopt;
    return WarpOpeningInput(CaptureImageImportAuthority(), history.CaptureAuthority(), _document.value(), target->id,
                            std::move(options), _warpSource.image, _warpOpening);
}

WarpOpeningResult DocumentSession::PrepareWarpOpeningPublication(WarpOpeningInput input, CancelToken cancellation,
                                                                 WarpOpeningObserver observer,
                                                                 std::shared_ptr<const SharedPublicationMemory> memory)
{
    const auto failed = [&]() noexcept
    {
        return WarpOpeningResult{cancellation.IsCancelled() ? WarpOpeningStatus::Cancelled
                                                            : WarpOpeningStatus::Invalid};
    };
    if (!OptionsValid(input._options) || input._options.ownerThread == std::this_thread::get_id() || !memory ||
        memory->IsBound() || !Checkpoint(WarpOpeningStage::Source, cancellation, observer))
        return failed();
    const auto* target = input._source.layers.FindKey(input._target);
    if (!target || target->isGroup || target->adjustment || !EffectivelyVisible(input._source.layers, target->id) ||
        !::WarpOpeningMemoryBytes(input._source, *target, input._options))
        return failed();
    WarpSourceView view{target->asset ? target->asset->image : Raster{}, target->transform, input._source.width,
                        input._source.height};
    if (!Checkpoint(WarpOpeningStage::Draft, cancellation, observer))
        return failed();
    auto draft = WarpStroke::MakePlacedForWork(view, input._options.mode, input._options.settings,
                                               input._options.ownerThread, cancellation, memory);
    if (!draft || !Checkpoint(WarpOpeningStage::FirstSample, cancellation, observer) ||
        !draft->AppendForWork(input._options.point, cancellation) ||
        !Checkpoint(WarpOpeningStage::Complete, cancellation, observer))
        return failed();
    WarpOpeningResult result;
    result.status = WarpOpeningStatus::Ready;
    result.prepared.emplace(PreparedWarpOpening(std::move(input), std::move(view), std::move(draft)));
    return result;
}

bool DocumentSession::IsPreparedWarpOpeningCurrent(const PreparedWarpOpening& prepared) const noexcept
{
    return !prepared._applied && prepared._draft && prepared._draft->IsValid() && !HasPendingWarp() && !_brush &&
           !_brushFinishingOpening && !_brushUpdatingOpening && !_transformEdit && !_gradient && !_pixelMove &&
           !_colorEdit && !_shapeDraft && !_lassoDraft && !_selectionMoveOrigin && !_maskSelected &&
           prepared._input._options.ownerThread == std::this_thread::get_id() && _warpOpening != UINT64_MAX &&
           CanApplyImageImport(prepared._input._authority) && history.IsCurrent(prepared._input._history) &&
           _warpSource.image == prepared._input._retiredSource && _warpOpening == prepared._input._opening &&
           _document && _document->layers.Identity() == prepared._input._source.layers.Identity() &&
           _document->width == prepared._input._source.width && _document->height == prepared._input._source.height &&
           _document->resolution == prepared._input._source.resolution &&
           _document->selection.has_value() == prepared._input._source.selection.has_value() &&
           (!_document->selection ||
            (_document->selection->loops.Identity() == prepared._input._source.selection->loops.Identity() &&
             _document->selection->antialiased == prepared._input._source.selection->antialiased));
}

bool DocumentSession::ApplyPreparedWarpOpeningIfCurrent(PreparedWarpOpening& prepared) noexcept
{
    if (!IsPreparedWarpOpeningCurrent(prepared))
        return false;
    const auto* target = prepared._input._source.layers.FindKey(prepared._input._target);
    if (!target)
        return false;
    _warpSource = WarpSource{target->id, target->asset ? target->asset->image : Raster{}, target->transform};
    _warp = prepared._draft;
    ++_warpOpening;
    _lastBrushPoint = LastBrushPoint{prepared._input._options.point, target->id, false};
    prepared._applied = true;
    return true;
}

std::shared_ptr<WarpStroke> PreparedWarpOpening::TakeAppliedDraftForOwner() noexcept
{
    return _applied ? std::move(_draft) : std::shared_ptr<WarpStroke>{};
}

std::uint64_t WarpOpeningMemoryBytes(const CanvasDocument& source, const ImageLayer& target,
                                     const WarpOpeningOptions& options) noexcept
{
    if (!OptionsValid(options) || source.width < 1 || source.height < 1 || source.width > kMaxCanvasSide ||
        source.height > kMaxCanvasSide || target.isGroup || target.adjustment ||
        source.layers.FindKey(target.id) != &target || !target.transform.IsValid())
        return 0;
    const auto retained = source.RetirementMemoryBytes();
    if (!retained)
        return 0;
    auto bytes = retained;
    const auto add = [&](std::uint64_t amount) noexcept
    {
        if (amount > UINT64_MAX - bytes)
            return false;
        bytes += amount;
        return true;
    };
    const auto radius = static_cast<int>(std::ceil((std::max)(2.0, options.settings.diameter) / 2.0));
    const auto side = static_cast<std::uint64_t>(2 * radius + 1);
    // The fixed allowance covers every <=100MP/30000-side lazy tile header and flag, source-window buffers,
    // ownership/control nodes and captured scalar metadata. Opening allocates no rendered/full mutable canvas.
    if (static_cast<std::int64_t>(source.width) * source.height > kMaxLayerPixels || !add(1024 * 1024))
        return 0;
    if (options.mode == SmearMode::Smudge)
    {
        const auto cx = static_cast<int>(std::lround(options.point.x));
        const auto cy = static_cast<int>(std::lround(options.point.y));
        const int left = (std::max)(0, cx - radius), right = (std::min)(source.width - 1, cx + radius);
        const int top = (std::max)(0, cy - radius), bottom = (std::min)(source.height - 1, cy + radius);
        const auto tiles = right < left || bottom < top ? 0ULL
                                                        : static_cast<std::uint64_t>(right / 256 - left / 256 + 1) *
                                                              (bottom / 256 - top / 256 + 1);
        const auto tilePixels =
            static_cast<std::size_t>((std::min)(256, source.width)) * (std::min)(256, source.height);
        const auto tileBytes = PixelBuffer::RequiredBytes(tilePixels * 4);
        if (!tileBytes || !add(tiles * tileBytes) || !add(side * side * 4 * sizeof(float)))
            return 0;
    }
    if (target.asset && target.asset->image.HasStorage() &&
        (target.asset->image.Format() != RasterFormat::Bgra8888 || !target.asset->image.RetirementMemoryBytes()))
        return 0;
    return bytes;
}

std::uint64_t DocumentSession::WarpOpeningMemoryBytes(const WarpOpeningOptions& options) const noexcept
{
    if (!_document || !_activeLayerID || _maskSelected)
        return 0;
    const auto* target = _document->layers.FindKey(_activeLayerID.value());
    if (!target)
        return 0;
    auto bytes = ::WarpOpeningMemoryBytes(_document.value(), *target, options);
    if (!bytes)
        return 0;
    if (_warpSource.image.HasStorage())
    {
        const auto retired = _warpSource.image.RetirementMemoryBytes();
        if (!retired || retired > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return 0;
        bytes += retired;
    }
    return bytes;
}

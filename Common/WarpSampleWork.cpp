#include "WarpSampleWork.h"

#include <thread>
#include <type_traits>

bool WarpSampleInput::AppendForWork(CancelToken cancellation, WarpStroke::WorkObserver observer)
{
    if (!_valid || !_draft || _started || _draft->WorkOwner() == std::this_thread::get_id())
        return false;
    _started = true;
    _prepared = _draft->AppendPlannedForWork(_plan, cancellation, observer);
    return _prepared;
}

WarpStroke* WarpSampleInput::DraftForWork() noexcept
{
    return _valid && _prepared && _draft && _draft->WorkOwner() != std::this_thread::get_id() ? _draft.get() : nullptr;
}

std::optional<WarpSampleInput> DocumentSession::TakeWarpSampleInput(std::uint64_t opening,
                                                                    const WarpStroke::SampleMemoryPlan& plan) noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    if (!_document || !_warp || _warp.use_count() != 1 || _warpUpdatingOpening || _warpFinishingOpening ||
        opening != _warpOpening || !_activeLayerID || _maskSelected || _warpSource.layerID != _activeLayerID.value() ||
        _warp->WorkOwner() != std::this_thread::get_id() || !_warp->IsSamplePlanCurrentForOwner(plan) || _brush ||
        _brushUpdatingOpening || _brushFinishingOpening || _transformEdit || _gradient || _pixelMove || _colorEdit ||
        _shapeDraft || _lassoDraft || _selectionMoveOrigin)
        return std::nullopt;
    WarpSampleInput input(CaptureImageImportAuthority(), _document.value(), history.CaptureState(), std::move(_warp),
                          plan, _warpSource.layerID, opening);
    _warpUpdatingOpening = opening;
    _warpUpdatingSequence = plan.sequence;
    return input;
}

bool DocumentSession::IsWarpSampleCurrent(const WarpSampleInput& input) const noexcept
{
    // Caller has joined the original producer; no pixel, points, tile or source traversal runs here.
    return input._valid && input._prepared && input._draft && input._draft->IsValid() && !_warp && _document &&
           _warpUpdatingOpening == input._opening && _warpUpdatingSequence == input._plan.sequence &&
           _warpOpening == input._opening && !_warpFinishingOpening &&
           input._draft->WorkOwner() == std::this_thread::get_id() && !_maskSelected &&
           _activeLayerID == input._target && _warpSource.layerID == input._target && !_brush &&
           !_brushUpdatingOpening && !_brushFinishingOpening && !_transformEdit && !_gradient && !_pixelMove &&
           !_colorEdit && !_shapeDraft && !_lassoDraft && !_selectionMoveOrigin &&
           CanApplyImageImport(input._authority) && history.IsCurrent(input._history) &&
           _document->layers.Identity() == input._source.layers.Identity() && _document->width == input._source.width &&
           _document->height == input._source.height && _document->resolution == input._source.resolution &&
           _document->selection.has_value() == input._source.selection.has_value() &&
           (!_document->selection ||
            (_document->selection->antialiased == input._source.selection->antialiased &&
             _document->selection->loops.Identity() == input._source.selection->loops.Identity()));
}

bool DocumentSession::ApplyWarpSampleIfCurrent(WarpSampleInput& input) noexcept
{
    if (!IsWarpSampleCurrent(input))
        return false;
    _warp = std::move(input._draft);
    _warpUpdatingOpening.reset();
    _warpUpdatingSequence.reset();
    _lastBrushPoint = LastBrushPoint{input._plan.point, input._target, false};
    input._valid = false;
    return true;
}

bool DocumentSession::DiscardWarpSampleIfCurrent(std::uint64_t opening, std::uint64_t sequence) noexcept
{
    if (_warpUpdatingOpening != opening || _warpUpdatingSequence != sequence)
        return false;
    _warpUpdatingOpening.reset();
    _warpUpdatingSequence.reset();
    return true;
}

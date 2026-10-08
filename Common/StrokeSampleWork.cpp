#include "StrokeSampleWork.h"

#include <algorithm>
#include <thread>
#include <type_traits>

std::optional<StrokeSampleInput> DocumentSession::TakeStrokeSampleInput(std::uint64_t opening, Vec2 point,
                                                                        BrushCoverageEngine*& ownerEngine) noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    if (ownerEngine || !_document || !_brush || _brushUpdatingOpening || _brushFinishingOpening ||
        opening != _brushOpening || !_activeLayerID || _brush->LayerID() != _activeLayerID.value() ||
        _brush->IsMask() != _maskSelected || _transformEdit || _gradient || _pixelMove || _warp || _colorEdit)
        return std::nullopt;
    const auto plan = _brush->PlanSampleMemory(point);
    if (!plan)
        return std::nullopt;
    StrokeSampleInput input(CaptureImageImportAuthority(), _document.value(), history.CaptureState(), _brush, opening,
                            point);
    ownerEngine = _brush->DetachCoverageForSample(plan.value());
    if (!ownerEngine)
        return std::nullopt;
    _brush.reset(); // The private input retains the complete draft. No destructor or engine call runs here.
    _brushUpdatingOpening = opening;
    return input;
}

bool DocumentSession::IsStrokeSampleCurrent(const StrokeSampleInput& input) const noexcept
{
    return input._valid && input._stroke && !_brush && _document && _brushUpdatingOpening == input._opening &&
           !_brushFinishingOpening && !_transformEdit && !_gradient && !_pixelMove && !_warp && !_colorEdit &&
           !_shapeDraft && CanApplyImageImport(input._authority) && history.IsCurrent(input._history) &&
           _document->layers.Identity() == input._source.layers.Identity() && _document->width == input._source.width &&
           _document->height == input._source.height && _document->resolution == input._source.resolution &&
           _document->selection.has_value() == input._source.selection.has_value() &&
           (!_document->selection ||
            (_document->selection->antialiased == input._source.selection->antialiased &&
             _document->selection->loops.Identity() == input._source.selection->loops.Identity()));
}

bool DocumentSession::ApplyStrokeSampleIfCurrent(StrokeSampleInput& input, BrushCoverageEngine* ownerEngine) noexcept
{
    // Caller has joined the worker/recorder and every original map/copy user. No private pixel reads here.
    if (!IsStrokeSampleCurrent(input) || !input._stroke->AttachCoverageAfterSample(ownerEngine))
        return false;
    _brush = input._stroke;
    _brushUpdatingOpening.reset();
    _lastBrushPoint = LastBrushPoint{input._point, _brush->LayerID(), _brush->IsMask()};
    input._valid = false;
    // The joined input may retire on a worker after this owner publication. Return its mutable draft
    // reference now: later owner cancellation must remain the last engine-owning release, without waiting
    // for that input's immutable document/history wrappers to retire. The session already owns the draft.
    input._stroke.reset();
    return true;
}

bool DocumentSession::DiscardStrokeSampleIfCurrent(std::uint64_t opening) noexcept
{
    if (_brushUpdatingOpening != opening)
        return false;
    _brushUpdatingOpening.reset();
    return true;
}

std::int32_t BrushStroke::IncreaseOpeningMemoryForSample(JobScheduler& scheduler, const SampleMemoryPlan& plan) noexcept
{
    constexpr auto invalid = static_cast<std::int32_t>(0x80070057u);
    const auto current = PlanSampleMemory(plan.point);
    if (!current || current->sequence != plan.sequence || current->persistentCpuBytes != plan.persistentCpuBytes ||
        !_openingMemory || _coverageOwner != std::this_thread::get_id())
        return invalid;
    const auto baseline =
        _sampleMemoryBaseCpu ? _sampleMemoryBaseCpu : (std::max)(RetirementMemoryBytes(), _openingMemory.Bytes());
    const auto result = scheduler.IncreaseRetainedReservation(
        _openingMemory, (std::max)(plan.persistentCpuBytes, _openingMemory.Bytes()));
    if (result >= 0)
        _sampleMemoryBaseCpu = baseline;
    return result;
}

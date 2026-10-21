#include "WarpRetirementWork.h"

#include "WarpStroke.h"

#include <thread>

std::optional<WarpRetirementInput> DocumentSession::TakeWarpRetirementInput(
    std::uint64_t opening, std::optional<std::uint64_t> sampleSequence) noexcept
{
    // Original joined owner only. In-flight callbacks require both opening and sample sequence, so an
    // old input cannot clear a younger draft/update within this same gesture. No source/path/tile walk.
    if (!opening || opening != _warpOpening || _warpFinishingOpening ||
        _warpSource.ownerThread != std::this_thread::get_id() ||
        (_warpUpdatingOpening &&
         (_warpUpdatingOpening != opening || !sampleSequence || _warpUpdatingSequence != sampleSequence)) ||
        (_warp && _warp->WorkOwner() != std::this_thread::get_id()) || (!_warp && !_warpUpdatingOpening))
        return std::nullopt;
    WarpRetirementInput retired(std::move(_warp), std::move(_warpSource.image));
    // The finishing marker blocks useful sample apply after capture. For an updating input, joined
    // destruction acknowledges its exact sample sequence first, then this finishing opening. Finish
    // discard must refuse while that original update is pending; no callback may resurrect its draft.
    // Already-final captures and duplicate retirements refuse above. Zero-point/failed live drafts need
    // neither a final replay nor a new history entry.
    _warpFinishingOpening = opening;
    return retired;
}

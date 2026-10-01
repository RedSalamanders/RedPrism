#include "ColorDismissalWork.h"

#include <limits>
#include <type_traits>
#include <utility>

std::optional<ColorDismissalInput> DocumentSession::PrepareColorDismissalInput() const
{
    if (!_document || !_colorEdit || _adjustmentOriginal || _adjustmentEditingID ||
        _colorEditSequence == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    auto token = CaptureJobToken(std::nullopt, _colorEditSequence);
    if (!token)
        return std::nullopt;
    return ColorDismissalInput{std::move(token.value()), _colorEdit->openingSequence, _activeLayerID, _maskSelected,
                               _cropRect};
}

std::optional<PreparedColorDismissal> DocumentSession::PrepareColorDismissalPublication(
    ColorDismissalInput input, DocumentHistory::StateView history, CancelToken cancellation)
{
    if (cancellation.IsCancelled() || input.token.target || !input.token.documentSerial || !input.opening)
        return std::nullopt;
    return PreparedColorDismissal{std::move(input), std::move(history)};
}

bool DocumentSession::IsPreparedColorDismissalCurrent(const PreparedColorDismissal& prepared) const noexcept
{
    const auto& input = prepared._input;
    return !prepared._applied && _document && _colorEdit && !_adjustmentOriginal && !_adjustmentEditingID &&
           _colorEdit->openingSequence == input.opening && _colorEditSequence == input.token.sequence &&
           _activeLayerID == input.active && _maskSelected == input.maskSelected && _cropRect == input.crop &&
           CanApplyJobResult(input.token, _colorEditSequence) && history.IsCurrent(prepared._history);
}

bool DocumentSession::ApplyPreparedColorDismissalIfCurrent(PreparedColorDismissal& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<ColorEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedColorDismissalCurrent(prepared))
        return false;
    std::swap(_colorEdit, prepared._retiredPanel);
    std::swap(_displayCopy, prepared._retiredDisplay);
    ++_colorEditSequence;
    ++_editRevision;
    prepared._applied = true;
    return true; // pixels, active target, global template, generation and history are untouched
}

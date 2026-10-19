#pragma once

#include "CanvasRenderer.h"
#include "PixelMovePreviewWork.h"

// One shared pre-resource check for both the graph owner and its private composition. Read only cached
// metadata/identities; the immutable factory owns the actual row reader and exact lifted source.
[[nodiscard]] inline bool RecordedPixelPreviewMatchesForWork(const CanvasDocument& document,
                                                             const CanvasRenderer::StrokePreview& preview) noexcept
{
    if (!preview.pixelSource || preview.warpSource || preview.mask || !preview.pixelSource->IsValidForWork())
        return false;
    const auto& layout = preview.pixelSource->Layout();
    const auto* target = document.layers.FindKey(preview.layerID);
    return target && target->asset && !target->isGroup && !target->adjustment && layout.target == preview.layerID &&
           layout.canvasWidth == document.width && layout.canvasHeight == document.height &&
           target->asset->image == layout.source && preview.source == layout.source &&
           target->transform == layout.sourceTransform && preview.width == layout.width &&
           preview.height == layout.height && preview.transform == layout.paintTransform &&
           preview.sourceX == layout.sourceRect.x && preview.sourceY == layout.sourceRect.y &&
           preview.sourceWidth == 0 && preview.sourceHeight == 0 && preview.fill == 0;
}

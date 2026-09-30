#pragma once

#include "Document.h"
#include "PixelBuffer.h"
#include "ProjectSaveWork.h"

#include <cstddef>
#include <cstdint>
#include <limits>

// Whole-job reservation for the export snapshot and its full-canvas BGRA readback. The save allowance is
// intentionally conservative: it shares the snapshot/metadata/one-codec bound used by project saves, even though
// export does not build a project manifest. A zero result refuses invalid input or an unrepresentable peak before
// the caller allocates the readback buffer.
[[nodiscard]] inline std::uint64_t ExportReadbackMemoryBytes(const CanvasDocument& document,
                                                             std::size_t pathUnits) noexcept
{
    if (pathUnits == 0 || pathUnits > kMaxImageOutputPathUnits ||
        document.layers.size() > static_cast<std::size_t>(kMaxLayerCount) || document.width < 1 ||
        document.height < 1 || document.width > kMaxCanvasSide || document.height > kMaxCanvasSide)
        return 0;

    const auto width = static_cast<std::uint64_t>(document.width);
    const auto height = static_cast<std::uint64_t>(document.height);
    if (width > static_cast<std::uint64_t>(kMaxLayerPixels) / height)
        return 0;
    const std::uint64_t pixelCount = width * height;
    const std::uint64_t readbackSize = pixelCount * 4;
    if (readbackSize > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
        return 0;

    // Export has no nested asset names. Charge metadata at the project bound plus the extra resolved-path
    // copies, without inheriting that writer's narrower destination limit.
    const auto projectPathUnits = pathUnits > kMaxProjectOutputPathUnits ? kMaxProjectOutputPathUnits : pathUnits;
    const std::uint64_t saveAllowance = ProjectSaveMemoryBytes(document, projectPathUnits);
    const std::uint64_t additionalPathAllowance = static_cast<std::uint64_t>(pathUnits - projectPathUnits) * 4 *
                                                  sizeof(wchar_t) *
                                                  (8 + 2 * static_cast<std::uint64_t>(document.layers.size()));
    const std::uint64_t readbackAllowance = PixelBuffer::RequiredBytes(static_cast<std::size_t>(readbackSize));
    if (saveAllowance == 0 || readbackAllowance == 0 ||
        readbackAllowance > (std::numeric_limits<std::uint64_t>::max)() - saveAllowance ||
        additionalPathAllowance > (std::numeric_limits<std::uint64_t>::max)() - saveAllowance - readbackAllowance)
        return 0;
    return saveAllowance + readbackAllowance + additionalPathAllowance;
}

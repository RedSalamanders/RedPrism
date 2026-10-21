#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>
#include <memory>
#include <optional>

enum class CopyPixelsStatus : std::uint8_t
{
    Ready,
    Invalid,
    Memory,
    Cancelled,
};

enum class CopyPixelsStage : std::uint8_t
{
    Source,
    Coverage,
    Rows,
    Publication,
};

struct CopyPixelsObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, CopyPixelsStage) = nullptr;
};

// Captured immutable source and displayed placement. Mask sources are Gray8; composites must already be on
// the complete document grid. background is the eager mask edge tone for separately placed masks, else zero.
// This pixel primitive owns no session/history/private clipboard authority. Its caller supplies those barriers.
struct CopyPixelsInput final
{
    // Last to retire. Caller supplies its original admitted lifetime; no implicit inheritance or reservation.
    std::shared_ptr<const void> outputLifetime;
    Raster source;
    LayerTransform placement;
    std::optional<DocumentSelection> selection;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool mask = false;
    bool composite = false;
    std::uint8_t background = 0;
};

struct CopyPixelsResult final
{
    CopyPixelsStatus status = CopyPixelsStatus::Invalid;
    std::optional<PixelClipboard> clipboard;
};

// Additional private sampling/coverage/region output peak, including allocation/control records. Existing
// source/outline owners are shared. Complete host capture/history/codec/OS transfer/retirement is separate.
// Zero refuses invalid or overflowing input. No traversal/materialization/allocation occurs in this planner.
[[nodiscard]] std::uint64_t CopyPixelsMemoryBytes(const CopyPixelsInput& input) noexcept;
// Metadata-only composite preparation admission, before allocating or reading back the complete image.
// Covers cropped BGRA output, selection coverage and scan scratch; readback/metadata/encoding are separate.
[[nodiscard]] std::uint64_t CopyMergedPixelsMemoryBytes(int width, int height,
                                                        const std::optional<DocumentSelection>& selection) noexcept;
[[nodiscard]] CopyPixelsResult RunCopyPixels(const CopyPixelsInput& input, CancelToken cancellation = {},
                                             CopyPixelsObserver observer = {});

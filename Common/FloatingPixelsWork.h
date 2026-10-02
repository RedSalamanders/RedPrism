#pragma once

#include "DistortWork.h"

#include <cstdint>
#include <optional>

enum class FloatingPixelsStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled,
};

struct FloatingOpeningPixels final
{
    FloatingPixelsStatus status = FloatingPixelsStatus::Invalid;
    std::optional<CanvasDocument> document;
    std::optional<TransformEdit> edit;
};

struct FloatingMergePixels final
{
    FloatingPixelsStatus status = FloatingPixelsStatus::Invalid;
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    bool budgetRefused = false;
};

// Pure CPU preparation. Caller owns/admitted both source graphs and all document/history/outline metadata.
// These planners account additional pixel/coverage/warp/mask peaks only. No clipboard is read or written.
[[nodiscard]] std::uint64_t FloatingOpeningPixelsMemoryBytes(const CanvasDocument& source, Guid active) noexcept;
[[nodiscard]] FloatingOpeningPixels PrepareFloatingOpeningPixels(const CanvasDocument& source, Guid active,
                                                                 Executor& executor, CancelToken cancellation = {});
// Detached complete outline mapping. Failure/cancellation leaves the retained input loops unchanged.
[[nodiscard]] bool PrepareFloatingSelectionOutline(DocumentSelection& selection, const TransformEdit& edit,
                                                   CancelToken cancellation = {});
[[nodiscard]] std::uint64_t FloatingMergePixelsMemoryBytes(const CanvasDocument& source,
                                                           const TransformEdit& edit) noexcept;
// Unchanged returns the exact opening graph. Other failure/cancellation returns no after-document: the caller
// publishes the already prepared restoration bundle, never builds/copies a restoration after allocation failure.
[[nodiscard]] FloatingMergePixels PrepareFloatingMergePixels(const CanvasDocument& source, const TransformEdit& edit,
                                                             Executor& executor, CancelToken cancellation = {});

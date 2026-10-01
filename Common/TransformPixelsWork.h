#pragma once

#include "DistortWork.h"

#include <cstdint>
#include <optional>
#include <string>

enum class TransformPixelsStatus : std::uint8_t
{
    Ready,
    Invalid,
    Unsupported,
    Memory,
    Cancelled,
};

struct TransformPixelsResult final
{
    TransformPixelsStatus status = TransformPixelsStatus::Invalid;
    std::optional<CanvasDocument> document;
    std::wstring historyName;
    bool budgetRefused = false;
};

// The caller owns both immutable inputs for the entire call and reserves document/history metadata separately.
// Floating selection opening/merge has a different restoration contract and is deliberately refused here.
// All pixels, masks and live-shape redraws are built privately; failure returns no after-document.
[[nodiscard]] std::uint64_t TransformPixelsMemoryBytes(const CanvasDocument& source,
                                                       const TransformEdit& edit) noexcept;
[[nodiscard]] TransformPixelsResult PrepareTransformPixels(const CanvasDocument& source, const TransformEdit& edit,
                                                           Executor& executor, CancelToken cancellation = {});

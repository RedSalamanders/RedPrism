#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <optional>

// Worker-only exact containment. Inputs retain immutable geometry; the result carries no owner mutation.
// nullopt means cancelled or invalid translation. Empty geometry and a nonfinite query mean outside,
// matching the numeric Contains reference. Loops and edges both count toward cancellation checkpoints.
enum class SelectionHitStage : std::uint8_t
{
    Begin,
    Loop,
    Edge,
    Complete,
};
struct SelectionHitObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, SelectionHitStage, std::size_t) noexcept = nullptr;
};
inline constexpr std::size_t kSelectionHitCheckpointQuantum = 1024;

// Adds the display offset to each vertex, as Translated followed by Contains does. Subtracting the offset
// from the query would change floating-point boundary rounding at large coordinates.
[[nodiscard]] std::optional<bool> ContainsSelectionForWork(const SelectionDisplayValue& display, Vec2 query,
                                                           CancelToken cancellation = {},
                                                           SelectionHitObserver observer = {}) noexcept;

// Cached metadata only. This owns no geometry/history root. The caller adds its fixed job/payload
// metadata before admission; capture validates every field again under that original ticket.
struct SelectionCursorProbePlan final
{
    DocumentReadAuthority document;
    Vec2 translation{};
    SelectionBounds bounds;
    std::uint64_t retainedBytes = 0;
    [[nodiscard]] bool operator==(const SelectionCursorProbePlan&) const noexcept = default;
};

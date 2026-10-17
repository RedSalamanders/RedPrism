#pragma once

#include "SelectionOutlineGestureState.h"

#include <cstdint>
#include <optional>

// Owner-only cached description, before any source/history/outline root is retained. This describes
// the exact inside-selection New outline branch; other tools and display preparation are separate.
struct SelectionOutlineGestureMemory final
{
    SelectionOutlineGestureState::Admission source;
    SelectionOutlineGestureState::Limits limits;
    std::uint64_t sourceBytes = 0, historyBytes = 0, geometryBytes = 0;
};
[[nodiscard]] std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(
    const DocumentSession& session, bool allowNewRectangle = false) noexcept;

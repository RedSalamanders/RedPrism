#pragma once

#include "SelectionOutlineGestureState.h"

#include <cstdint>
#include <optional>

// Owner-only cached description, before any source/history/outline root is retained. This describes
// the selected inside-outline branch plus its explicit captured New marquee kind; display remains separate.
struct SelectionOutlineGestureMemory final
{
    SelectionOutlineGestureState::Admission source;
    SelectionOutlineGestureState::Limits limits;
    std::uint64_t sourceBytes = 0, historyBytes = 0, geometryBytes = 0;
    std::optional<LassoFinishGeometry::DrawingPlan> drawing;
};
[[nodiscard]] std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(
    const DocumentSession& session, bool allowNewRectangle = false) noexcept;
// Admit this exact captured kind before the original parent captures any source.
[[nodiscard]] std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(
    const DocumentSession& session, LassoKind newMarqueeKind) noexcept;
// Mode is captured at Down; its complete boolean output/workspace envelope precedes source capture.
[[nodiscard]] std::optional<SelectionOutlineGestureMemory> DescribeSelectionOutlineGestureMemory(
    const DocumentSession& session, LassoKind marqueeKind, SelectionMode mode) noexcept;

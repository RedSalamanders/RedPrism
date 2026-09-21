#pragma once

#include "Document.h"
#include "TransformEdit.h"

#include <cstdint>
#include <optional>
#include <vector>

// The Crop tool's model (C16, Specs/Document/Document_Tools.md "Canvas size, image size and crop"): ported from
// legacy Crop.swift. The frame is a document-space rectangle the session holds until Apply Crop resizes the canvas
// to it through ResizeCanvas with the frame's origin as the content offset.

namespace CropGeometry
{
// Whole pixels: the standardized rectangle's edges rounded, at least one pixel a side.
[[nodiscard]] CropRect Snapped(const CropRect& rect) noexcept;
// Finite, 1…30 000 pixels a side, an origin within ±1 000 000.
[[nodiscard]] bool Valid(const CropRect& rect) noexcept;
// A frame dragged from `start` to `end`, kept to `ratio` (width / height) when given — or, `symmetric` (Alt), grown
// out from `start` as its center.
[[nodiscard]] CropRect Create(Vec2 start, Vec2 end, std::optional<double> ratio, bool symmetric = false) noexcept;
} // namespace CropGeometry

struct CropDrag
{
    enum class Mode : std::uint8_t
    {
        Create,
        Move,
        Resize,
    };

    Vec2 start{};
    CropRect original{};
    Mode mode = Mode::Create;
    int handle = -1; // Resize: index into LayerTransform::kHandles

    // The frame for the pointer at `point`. `symmetric` (Alt held) keeps the frame's center fixed: the opposite edges
    // move with the dragged ones.
    [[nodiscard]] CropRect Updated(Vec2 point, std::optional<double> ratio, bool symmetric = false) const noexcept;
};

// Crop edges snap to nearby layer and canvas edges while dragging.
struct CropSnap
{
    std::vector<double> xs; // document x positions to snap to
    std::vector<double> ys;
    double tolerance = 0; // how close, in document pixels, an edge must come to snap

    // Moving the frame snaps its closest edges and keeps its size; creating or resizing snaps only the edges on the
    // side being dragged — mirrored about the center when `symmetric`. With a fixed ratio only moves snap, so the
    // ratio stays exact.
    [[nodiscard]] CropRect Apply(const CropRect& rect, const CropDrag& drag, Vec2 point, std::optional<double> ratio,
                                 bool symmetric = false) const noexcept;
};

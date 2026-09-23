#pragma once

#include "Document.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Layer transform editing (C8): drag geometry, snapping and free distortion in document space. The host maps
// pointer positions through CanvasViewport. Ported from legacy LayerTransform.swift and Distort.swift; see
// Specs/Document/Document_Tools.md.

struct TransformDrag
{
    enum class Mode : std::uint8_t
    {
        Move,
        Resize,
        Rotate,
        Distort,
    };

    Mode mode = Mode::Move;
    int handle = -1; // Resize / Distort: index into LayerTransform::kHandles
    LayerTransform original{};
    Vec2 start{};
    std::optional<std::array<Vec2, 4>> originalCorners;

    // Corners after dragging to `point`: a corner handle moves its corner, an edge handle both corners of that
    // edge, the body the whole shape. nullopt when the drag is not distorting.
    [[nodiscard]] std::optional<std::array<Vec2, 4>> Corners(Vec2 point, bool shift) const noexcept;
    // The transform for the pointer at `point` (document pixels). Shift constrains a move to one axis, snaps a
    // rotation to 15 degrees and inverts the ratio lock while resizing; option (Alt) resizes about the center.
    [[nodiscard]] LayerTransform Updated(Vec2 point, bool lockRatio, bool shift, bool option = false) const noexcept;
};

struct SnapTargets
{
    std::vector<double> xs;
    std::vector<double> ys;
};

namespace TransformSnap
{
// How close, in screen DIPs, a guide comes before it snaps.
inline constexpr double kDistanceDip = 10.0;

struct Result
{
    Vec2 offset{};
    std::optional<double> x;
    std::optional<double> y;
};

// The box (minX, minY, maxX, maxY) moved so whichever of its left, center or right lands nearest an `xs` target
// does, and the same vertically; each axis on its own and only within `tolerance` document pixels. The targets
// it landed on come back too, to draw a guide along.
[[nodiscard]] Result Offset(double minX, double minY, double maxX, double maxY, std::span<const double> xs,
                            std::span<const double> ys, double tolerance) noexcept;
} // namespace TransformSnap

namespace DistortWarp
{
// The perspective map of the unit square onto four corners (handle order: top-left, top-right, bottom-right,
// bottom-left) as a 3x3 matrix applied to (x, y, 1).
struct Homography
{
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    [[nodiscard]] Vec2 Map(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<Homography> Inverted() const noexcept;
};

// Four finite corners making a convex, non-degenerate shape; a twisted (bow-tie) or collapsed shape is refused.
[[nodiscard]] bool IsUsable(const std::array<Vec2, 4>& corners) noexcept;
[[nodiscard]] Homography MakeHomography(const std::array<Vec2, 4>& corners) noexcept;

struct Warped
{
    Raster image;
    LayerTransform transform;
};

struct WarpedTrimmed
{
    Raster image;
    LayerTransform transform;
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 0;
    int cropHeight = 0;
};

// `image`, shown through `transform`, resampled so its corners land on `corners`: the warped pixels over the
// shape's whole-pixel bounds and the axis-aligned transform for them. `limit` caps the longest side for previews
// (0 keeps full resolution). A uniform 1 x 1 mask passes through untouched.
[[nodiscard]] std::optional<Warped> Warp(const Raster& image, const LayerTransform& transform,
                                         const std::array<Vec2, 4>& corners, bool isMask, double limit = 0.0);
// A full-resolution warp cropped to its visible pixels, with the crop in the warp's pixels (for a mask to match).
[[nodiscard]] std::optional<WarpedTrimmed> WarpTrimmed(const Raster& image, const LayerTransform& transform,
                                                       const std::array<Vec2, 4>& corners);
// A mask warped like its layer, filled with `background` (0 hides, 1 reveals) outside the shape; `limit` caps the
// longest side for previews (0 keeps full resolution).
[[nodiscard]] std::optional<Warped> WarpMask(const Raster& mask, const LayerTransform& transform,
                                             const std::array<Vec2, 4>& corners, double background, double limit = 0.0);
// Where `placement`'s corners land when the perspective taking `transform`'s corners to `corners` is applied to
// it too: how a linked mask placed apart from its layer distorts with the layer.
[[nodiscard]] std::array<Vec2, 4> Carried(const LayerTransform& placement, const LayerTransform& transform,
                                          const std::array<Vec2, 4>& corners) noexcept;
} // namespace DistortWarp

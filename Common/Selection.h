#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <vector>

// Selection geometry (C9, Specs/Document/Document_Tools.md): boolean operations and offsets through Clipper2,
// coverage rasterization, the Magic Wand and mask-tracing kernels ported from legacy WandPixels.c and
// MaskTracing.swift, and the layer sampling the wand reads. The session API lives in Document.h.

namespace SelectionGeometry
{
[[nodiscard]] std::vector<SelectionLoop> Rect(double x, double y, double width, double height);
// An oval filling the box, as a polygon fine enough to read as a curve at any zoom.
[[nodiscard]] std::vector<SelectionLoop> Ellipse(double x, double y, double width, double height);
[[nodiscard]] std::optional<SelectionBounds> Bounds(const std::vector<SelectionLoop>& loops) noexcept;
// Boolean operations under the nonzero rule; results are normalized (outer loops and holes, no overlaps).
[[nodiscard]] std::vector<SelectionLoop> Union(const std::vector<SelectionLoop>& a,
                                               const std::vector<SelectionLoop>& b);
[[nodiscard]] std::vector<SelectionLoop> Subtract(const std::vector<SelectionLoop>& a,
                                                  const std::vector<SelectionLoop>& b);
[[nodiscard]] std::vector<SelectionLoop> Intersect(const std::vector<SelectionLoop>& a,
                                                   const std::vector<SelectionLoop>& b);
// Grown (delta > 0) or shrunk (delta < 0) by |delta| pixels with round corners (Photoshop's Expand / Contract).
[[nodiscard]] std::vector<SelectionLoop> Offset(const std::vector<SelectionLoop>& loops, double delta);
[[nodiscard]] std::vector<SelectionLoop> Translated(const std::vector<SelectionLoop>& loops, double dx, double dy);
[[nodiscard]] std::vector<SelectionLoop> Transformed(const std::vector<SelectionLoop>& loops, const Affine2& map);
// Nonzero winding containment; a point on the outline counts as inside.
[[nodiscard]] bool Contains(const std::vector<SelectionLoop>& loops, Vec2 point) noexcept;
// Gray8 coverage at document resolution (255 selected). Antialiased edges carry exact-area partial coverage;
// otherwise a pixel is selected when its center lies inside.
[[nodiscard]] Raster Coverage(const std::vector<SelectionLoop>& loops, int width, int height, bool antialiased);
} // namespace SelectionGeometry

// The box a drag from `anchor` to `point` spans, in whole pixels. `square` evens the sides; `fromCenter` grows the
// box around the anchor. Shared by the Marquee and (later) the Shape tool.
[[nodiscard]] SelectionBounds DragBox(Vec2 anchor, Vec2 point, bool square, bool fromCenter) noexcept;

namespace MagicWand
{
// 255 where a premultiplied BGRA pixel matches the color averaged over the sample square around (`x`, `y`);
// `count` receives the number selected, or -1 when memory runs out. Empty when the image has no storage.
[[nodiscard]] std::vector<std::uint8_t> Mask(const Raster& image, int x, int y, const WandSettings& settings,
                                             long& count);
// Outline of a mask's nonzero pixels along exact pixel edges: outer loops clockwise and holes counterclockwise in
// top-left coordinates, so the nonzero rule fills exactly those pixels. nullopt when empty or too detailed.
[[nodiscard]] std::optional<std::vector<SelectionLoop>> Trace(const std::vector<std::uint8_t>& mask, int width,
                                                              int height, WandFailure* failure = nullptr);
// The outline of the pixels matching the one at `point` (image pixel coordinates); nullopt when nothing matches
// or the point lies outside the image.
[[nodiscard]] std::optional<std::vector<SelectionLoop>> Select(const Raster& image, Vec2 point,
                                                               const WandSettings& settings,
                                                               WandFailure* failure = nullptr);
} // namespace MagicWand

namespace MaskTracing
{
// Outline of a Gray8 mask's pixels darker than 50 %, in the mask's pixel coordinates; nullopt when none, or when the
// outline is refused (`failure`: TooDetailed past 8 000 000 pixel edges, Memory when the pixels cannot be read or the
// outline allocated).
[[nodiscard]] std::optional<std::vector<SelectionLoop>> DarkPixels(const Raster& mask, WandFailure* failure = nullptr);
// Outline of a BGRA image's pixels at least 50 % opaque; nullopt when none or refused, as DarkPixels.
[[nodiscard]] std::optional<std::vector<SelectionLoop>> OpaquePixels(const Raster& image,
                                                                     WandFailure* failure = nullptr);
} // namespace MaskTracing

// The map from an image's pixel coordinates to the document for a layer placed by `transform` (legacy
// BrushRaster.pixelToDocument): pixel (0, 0) is the placed top-left corner, flips included.
[[nodiscard]] Affine2 PixelToDocument(const LayerTransform& transform, int width, int height) noexcept;

// `image` drawn at `transform` onto a transparent document-size BGRA canvas, sampled bilinearly (premultiplied,
// transparent beyond the pixels). What the wand reads for one layer. Empty when the image's pixels cannot be read.
[[nodiscard]] Raster RenderLayerPixels(const Raster& image, const LayerTransform& transform, int width, int height);

#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>

// Pixel filters the tools share (C11): a separable Gaussian blur and mask rendering at document size.
namespace ImageFilters
{
// `source` blurred by `sigma` pixels (a kernel of radius 3 sigma). Premultiplied BGRA treats pixels beyond the edge
// as transparent, so edges fade; Gray8 clamps to the edge, so a mask keeps its edge tone. Empty when the source is
// empty or memory runs out.
[[nodiscard]] Raster GaussianBlur(const Raster& source, double sigma, const CancelToken& cancellation = {});

// A mask's pixels placed by `transform` on a document-size Gray8 canvas filled with `background` (its edge tone):
// nearest when the transform's sampling is Nearest, else bilinear with the edges clamped inside the placed box
// (`RenderPlacedMask`, collapsing High to the same single bilinear tap, as `RenderLayerPixels` does). `background`
// beyond the placed box, or whole when the transform is invalid. Empty when the mask is empty, its pixels cannot be
// read or memory runs out.
[[nodiscard]] Raster RenderMaskPixels(const Raster& mask, const LayerTransform& transform, int width, int height,
                                      std::uint8_t background);
// Admitted worker sample: private sparse reads, cancellable rows, unchanged mask sampling/edge tone.
// Invalid dimensions, wrong format, cancellation or output over 100M pixels return empty.
[[nodiscard]] Raster RenderMaskPixelsForWork(const Raster& mask, const LayerTransform& transform, int width, int height,
                                             std::uint8_t background, const CancelToken& cancellation = {});
} // namespace ImageFilters

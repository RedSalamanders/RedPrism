#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>
#include <optional>

// Canvas Size, Image Size and the Crop tool's commit (C16, Specs/Document/Document_Tools.md "Canvas size, image
// size and crop"): ported from legacy CanvasSize.swift, CanvasResizer.swift and ImageResizer.swift.

enum class CanvasUnit : std::uint8_t
{
    Pixels,
    Percent,
    Inches,
    Centimeters,
};

// The Canvas Size sheet's numbers (legacy CanvasSizeDraft): pixels kept as doubles, shown in the chosen unit,
// relative to the current size or absolute, with the original ratio locked or free.
struct CanvasSizeDraft
{
    int originalWidth = 1;
    int originalHeight = 1;
    double resolution = 72;
    double width = 1;
    double height = 1;
    bool relative = false;
    bool locked = false;
    CanvasUnit unit = CanvasUnit::Pixels;

    CanvasSizeDraft() = default;
    CanvasSizeDraft(int width, int height, double resolution) noexcept;
    [[nodiscard]] bool Valid() const noexcept;
    // The number the field shows for an axis: the pixels (minus the original when relative) in the unit.
    [[nodiscard]] double Displayed(bool widthAxis) const noexcept;
    // A typed number: pixels from the unit (plus the original when relative); the locked ratio moves the other axis.
    void Set(double value, bool widthAxis) noexcept;
    [[nodiscard]] int RoundedWidth() const noexcept;
    [[nodiscard]] int RoundedHeight() const noexcept;
};

struct CanvasExtensionColor
{
    double red = 0;
    double green = 0;
    double blue = 0;
};

struct CanvasSizeOptions
{
    int width = 1;
    int height = 1;
    int anchor = 4; // row-major, top-left through bottom-right
    std::optional<CanvasExtensionColor> fill;
    std::optional<Vec2> contentOffset; // Crop supplies an explicit document-space translation
    // Where the old content lands: the explicit offset, or the anchor's share of the size change (floor puts the
    // extra pixel right/bottom when expanding and takes it from the left/top when shrinking around the center).
    [[nodiscard]] Vec2 Offset(int fromWidth, int fromHeight) const noexcept;
};

// The Image Size sheet's numbers (legacy ImageSizeSheet): pixels, resolution, unit, the locked ratio and resample.
struct ImageSizeDraft
{
    int originalWidth = 1;
    int originalHeight = 1;
    double width = 1;
    double height = 1;
    double resolution = 72;
    bool locked = true;
    bool resample = true;
    CanvasUnit unit = CanvasUnit::Pixels;
    LayerSampling sampling = LayerSampling::High;

    ImageSizeDraft() = default;
    ImageSizeDraft(int width, int height, double resolution) noexcept;
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] double Displayed(bool widthAxis) const noexcept;
    // Resampling: pixels from the unit, the locked ratio moving the other axis. Not resampling: the pixels stay and
    // the resolution follows the typed print size.
    void Set(double value, bool widthAxis) noexcept;
    // With resampling in inches or centimeters the pixels follow the resolution (the print size stays).
    void SetResolution(double value) noexcept;
    // Off: the pixels return to the original, the ratio locks and a pixel unit becomes inches.
    void SetResample(bool enabled) noexcept;
    [[nodiscard]] int RoundedWidth() const noexcept;
    [[nodiscard]] int RoundedHeight() const noexcept;
};

struct ImageSizeOptions
{
    int width = 1;
    int height = 1;
    double resolution = 72;
    LayerSampling sampling = LayerSampling::High;
};

enum class ResizeStatus : std::uint8_t
{
    Ok,
    Unchanged, // nothing to do: the same size and no offset
    TooLarge,
    Invalid,
    Memory,
    Cancelled, // sizing was cancelled; the result document is empty
};

// Additional whole-job allocation peak, including rounded pixel allocations, eager thumbnails, one private
// sparse-source assembly and document/history metadata. Zero means arithmetic cannot be represented. Early
// refusal reserves metadata; later refusal also reserves the allocated prefix. The resizer retains its status.
[[nodiscard]] std::uint64_t SizingMemoryBytes(const CanvasDocument& source, const CanvasSizeOptions& options) noexcept;
[[nodiscard]] std::uint64_t SizingMemoryBytes(const CanvasDocument& source, const ImageSizeOptions& options) noexcept;

// Every layer and placed mask translated by the anchor's offset onto a canvas of the new size, the content kept
// unscaled and unclipped; a colored extension (when growing) is a bottom layer `Canvas Extension` of the new size
// with the old canvas cut out, so the old transparency stays. The selection outline is dropped.
[[nodiscard]] ResizeStatus ResizeCanvas(const CanvasDocument& source, const CanvasSizeOptions& options,
                                        CanvasDocument& result, const CancelToken& cancellation = {});

// Every layer rasterized at the new document scale: its transformed pixels drawn upright into the box its corners
// span (a nonuniformly scaled rotated layer shears, which width/height/angle cannot hold), the transform reset to
// that box with the chosen sampling; masks on their own placement and uniform masks keep their pixels with the
// placement scaled, other masks rasterize the same way. The same size changes only the resolution.
[[nodiscard]] ResizeStatus ResizeImage(const CanvasDocument& source, const ImageSizeOptions& options,
                                       CanvasDocument& result, const CancelToken& cancellation = {});

// `image` drawn through `pixelToTarget` (image pixel → target pixel) into a transparent BGRA target of the given
// size: Nearest picks the nearest source pixel, Smooth samples bilinearly, High also averages a grid of samples per
// target pixel when the map shrinks the image. Pixels the source does not reach stay transparent. Empty when memory
// runs out, the source's pixels cannot be read, or cancellation is requested.
[[nodiscard]] Raster RenderPlacedImage(const Raster& image, const Affine2& pixelToTarget, int width, int height,
                                       LayerSampling sampling, const CancelToken& cancellation = {});
// The Gray8 counterpart for a mask: pixels the mask does not reach take `background`.
[[nodiscard]] Raster RenderPlacedMask(const Raster& mask, const Affine2& pixelToTarget, int width, int height,
                                      LayerSampling sampling, std::uint8_t background,
                                      const CancelToken& cancellation = {});

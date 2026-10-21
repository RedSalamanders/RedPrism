#pragma once

#include "BrushStroke.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Exclusive worker sampler for a transient displayed layer. No document-sized source copy, shared sparse
// assembly, GPU owner or session mutation. High uses the same single bilinear tap as RenderLayerPixels.
// Four reusable 64-square source windows bound the sparse-read scratch; contiguous sources borrow bytes.
class PlacedRasterReader final
{
  public:
    static constexpr int kCacheSide = 64;
    static constexpr std::size_t kCacheSlots = 4;
    PlacedRasterReader(Raster source, const LayerTransform& placement) noexcept;
    PlacedRasterReader(const PlacedRasterReader&) = delete;
    PlacedRasterReader& operator=(const PlacedRasterReader&) = delete;
    [[nodiscard]] bool Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                            CancelToken cancellation = {}) noexcept;
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;

  private:
    struct Window final
    {
        PixelBuffer pixels;
        int x = -1;
        int y = -1;
        std::uint64_t used = 0;
    };
    [[nodiscard]] const std::uint8_t* Pixel(int x, int y, CancelToken cancellation) noexcept;
    Raster _source;
    Affine2 _toPixel;
    std::array<Window, kCacheSlots> _windows;
    const std::uint8_t* _contiguous = nullptr;
    std::uint64_t _clock = 0;
    bool _valid = false;
    bool _nearest = false;
};

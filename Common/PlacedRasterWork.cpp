#include "PlacedRasterWork.h"

#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

PlacedRasterReader::PlacedRasterReader(Raster source, const LayerTransform& placement) noexcept
    : _source(std::move(source)), _nearest(placement.sampling == LayerSampling::Nearest)
{
    _valid = _source.HasStorage() && _source.Format() == RasterFormat::Bgra8888 && placement.IsValid();
    if (_valid)
        _toPixel = PixelToDocument(placement, _source.Width(), _source.Height()).Inverted();
}

const std::uint8_t* PlacedRasterReader::Pixel(int x, int y, CancelToken cancellation) noexcept
{
    x = std::clamp(x, 0, _source.Width() - 1);
    y = std::clamp(y, 0, _source.Height() - 1);
    if (_contiguous)
        return _contiguous + static_cast<std::size_t>(y) * _source.Stride() + static_cast<std::size_t>(x) * 4;
    const int originX = x / kCacheSide * kCacheSide;
    const int originY = y / kCacheSide * kCacheSide;
    Window* window = nullptr;
    for (auto& candidate : _windows)
        if (candidate.x == originX && candidate.y == originY)
        {
            window = &candidate;
            break;
        }
    if (!window)
    {
        window = &*std::min_element(_windows.begin(), _windows.end(),
                                    [](const Window& a, const Window& b) { return a.used < b.used; });
        if (!window->pixels)
            window->pixels = PixelBuffer::TryAllocate(kCacheSide * kCacheSide * 4);
        if (!window->pixels)
            return nullptr;
        // Coordinates are published only after the complete private read; cancellation cannot expose old keys
        // over partially replaced pixels. Outside-fill is irrelevant to clamped source sampling.
        window->x = window->y = -1;
        if (!ReadRasterRegionForWork(_source, {originX, originY, kCacheSide, kCacheSide},
                                     {window->pixels.Data(), window->pixels.Size()}, kCacheSide * 4, 0, cancellation))
            return nullptr;
        window->x = originX;
        window->y = originY;
    }
    if (_clock == UINT64_MAX)
    {
        for (auto& item : _windows)
            item.used = item.used ? 1 : 0;
        _clock = 1;
    }
    window->used = ++_clock;
    return window->pixels.Data() + static_cast<std::size_t>(y - originY) * kCacheSide * 4 +
           static_cast<std::size_t>(x - originX) * 4;
}

bool PlacedRasterReader::Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                              CancelToken cancellation) noexcept
{
    if (region.Empty() || region.width > kMaxCanvasSide || region.height > kMaxCanvasSide || region.x < 0 ||
        region.y < 0 || region.x > kMaxCanvasSide - region.width || region.y > kMaxCanvasSide - region.height ||
        cancellation.IsCancelled())
        return false;
    const auto rowBytes = static_cast<std::size_t>(region.width) * 4;
    if (stride < rowBytes || output.size() < rowBytes ||
        static_cast<std::size_t>(region.height - 1) > (output.size() - rowBytes) / stride)
        return false;
    // These conditions prohibit lazy materialization. A once-published sparse cache is immutable and its
    // owning Raster stays held for the entire borrow. The reader never calls Data on an unassembled sparse graph.
    if (_valid && !_contiguous && (!_source.IsSparse() || _source.IsMaterialized()))
        _contiguous = _source.Data();
    for (int row = 0; row < region.height; ++row)
    {
        if (cancellation.IsCancelled())
            return false;
        auto* destination = output.data() + static_cast<std::size_t>(row) * stride;
        std::memset(destination, 0, rowBytes);
        if (!_valid)
            continue;
        for (int column = 0; column < region.width; ++column)
        {
            if (column % 256 == 0 && cancellation.IsCancelled())
                return false;
            const auto point = _toPixel.Apply({region.x + column + 0.5, region.y + row + 0.5});
            if (!(point.x >= 0) || !(point.y >= 0) || !(point.x < _source.Width()) || !(point.y < _source.Height()))
                continue;
            auto* pixel = destination + static_cast<std::size_t>(column) * 4;
            if (_nearest)
            {
                const auto* sample =
                    Pixel(static_cast<int>(std::floor(point.x)), static_cast<int>(std::floor(point.y)), cancellation);
                if (!sample)
                    return false;
                std::memcpy(pixel, sample, 4);
                continue;
            }
            const double cx = std::clamp(point.x - 0.5, 0.0, static_cast<double>(_source.Width() - 1));
            const double cy = std::clamp(point.y - 0.5, 0.0, static_cast<double>(_source.Height() - 1));
            const double fx = std::floor(cx), fy = std::floor(cy);
            const double tx = cx - fx, ty = cy - fy;
            const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
            // Copy each sample before the next cache lookup: a subsequent source window can evict the previous
            // one, including when all four taps straddle a cache corner. Never keep four borrowed cache pointers.
            std::array<std::array<std::uint8_t, 4>, 4> samples{};
            const std::array<Vec2, 4> positions{{{static_cast<double>(ix), static_cast<double>(iy)},
                                                 {static_cast<double>(ix + 1), static_cast<double>(iy)},
                                                 {static_cast<double>(ix), static_cast<double>(iy + 1)},
                                                 {static_cast<double>(ix + 1), static_cast<double>(iy + 1)}}};
            for (std::size_t tap = 0; tap < samples.size(); ++tap)
            {
                const auto* value =
                    Pixel(static_cast<int>(positions[tap].x), static_cast<int>(positions[tap].y), cancellation);
                if (!value)
                    return false;
                std::memcpy(samples[tap].data(), value, 4);
            }
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const double top = samples[0][channel] + (samples[1][channel] - samples[0][channel]) * tx;
                const double bottom = samples[2][channel] + (samples[3][channel] - samples[2][channel]) * tx;
                pixel[channel] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(top + (bottom - top) * ty), 0L, 255L));
            }
        }
    }
    return !cancellation.IsCancelled();
}

std::uint64_t PlacedRasterReader::RetirementMemoryBytes() const noexcept
{
    auto bytes = sizeof(*this) + 1024ULL;
    const auto add = [&](std::uint64_t amount) noexcept
    {
        if (amount > UINT64_MAX - bytes)
            return false;
        bytes += amount;
        return true;
    };
    if (_source.HasStorage() && (!_source.RetirementMemoryBytes() || !add(_source.RetirementMemoryBytes())))
        return 0;
    for (const auto& window : _windows)
        if (!add(window.pixels.AllocationBytes()))
            return 0;
    return bytes;
}

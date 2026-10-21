#include "AdjustmentDisplayAdmission.h"

#include "Document.h"

#include <limits>

namespace AdjustmentDisplayAdmission
{
namespace
{
constexpr std::uint64_t kKiB = 1024;
constexpr std::uint64_t kMiB = 1024 * kKiB;
constexpr std::uint64_t kObjectAllowance = 64 * kKiB;
constexpr std::uint64_t kUploadBand = 1 * kMiB;
constexpr std::uint64_t kAdjustmentTableBytes = 256ull * 4 * sizeof(float);
constexpr std::uint64_t kSettingsAllowance = 4 * kKiB;
constexpr std::uint64_t kCurvePointAllowance = 2ull * 4 * 32 * sizeof(CurvePoint);
constexpr std::uint64_t kCubeBytes = 33ull * 33 * 33 * 4 * sizeof(float);
constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();

static_assert(sizeof(AdjustmentLookup) + sizeof(LayerAdjustment) <= kAdjustmentTableBytes + kSettingsAllowance);

[[nodiscard]] bool Add(std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}

[[nodiscard]] bool Multiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > kMaximum / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool ValidKind(AdjustmentKind kind) noexcept
{
    switch (kind)
    {
    case AdjustmentKind::Hsv:
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::Grain:
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t GpuLookupBytes(AdjustmentKind kind) noexcept
{
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        return kCubeBytes;
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    case AdjustmentKind::GradientMap:
        return kAdjustmentTableBytes;
    case AdjustmentKind::Grain:
        return 0;
    }
    return 0;
}

[[nodiscard]] bool MipMeasure(std::uint32_t width, std::uint32_t height, std::uint32_t& levels,
                              std::uint64_t& texels) noexcept
{
    levels = 0;
    texels = 0;
    for (;;)
    {
        std::uint64_t levelTexels = 0;
        if (!Multiply(width, height, levelTexels) || !Add(texels, levelTexels))
            return false;
        ++levels;
        if (width == 1 && height == 1)
            return levels <= 15;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
}
} // namespace

std::optional<Envelope> Estimate(const Descriptor& descriptor) noexcept
{
    if (!ValidKind(descriptor.kind))
        return std::nullopt;

    if (descriptor.liveAdjustment)
    {
        if (descriptor.width != 0 || descriptor.height != 0 || descriptor.selectionCoverage)
            return std::nullopt;
    }
    else if (descriptor.width == 0 || descriptor.height == 0 || descriptor.width > 16384 || descriptor.height > 16384)
    {
        return std::nullopt;
    }

    Envelope result{};
    std::uint64_t mipTexels = 0;
    std::uint32_t gpuRoundingObjects = 0;
    if (!descriptor.liveAdjustment)
    {
        if (!MipMeasure(descriptor.width, descriptor.height, result.mipLevels, mipTexels))
            return std::nullopt;

        std::uint64_t targetBytes = 0;
        if (!Multiply(mipTexels, 4, targetBytes) || !Add(result.logicalGpuBytes, targetBytes))
            return std::nullopt;
        ++result.textureCount;
        gpuRoundingObjects += 3; // Private BGRA texture, RTV, and shader-resource view.

        if (descriptor.selectionCoverage)
        {
            if (!Add(result.logicalGpuBytes, mipTexels))
                return std::nullopt;
            ++result.textureCount;
            gpuRoundingObjects += 2; // Gray8 texture and shader-resource view.
        }
    }

    const std::uint64_t gpuLookupBytes = GpuLookupBytes(descriptor.kind);
    if (!Add(result.logicalGpuBytes, gpuLookupBytes))
        return std::nullopt;
    if (gpuLookupBytes != 0)
    {
        ++result.textureCount;
        gpuRoundingObjects += 2; // Lookup texture and shader-resource view.
    }
    if (gpuRoundingObjects != 0)
        ++gpuRoundingObjects; // One bounded graph-container allowance.

    // AdjustmentLookup embeds its 4096-byte table for every kind, including Grain. Its copied LayerAdjustment and
    // vector objects fit the fixed settings allowance. Two maximum curve payloads cover original settings and the
    // retained lookup copy; the source/root owner remains a separate caller charge.
    std::uint64_t rowScratchBytes = 0;
    if (!Multiply(descriptor.liveAdjustment ? 0u : descriptor.width, 4 * 8, rowScratchBytes) ||
        !Add(result.logicalCpuBytes, kUploadBand) || !Add(result.logicalCpuBytes, rowScratchBytes) ||
        !Add(result.logicalCpuBytes, kAdjustmentTableBytes) || !Add(result.logicalCpuBytes, kSettingsAllowance) ||
        !Add(result.logicalCpuBytes, kCurvePointAllowance))
        return std::nullopt;
    if (descriptor.kind == AdjustmentKind::Hsv && !Add(result.logicalCpuBytes, kCubeBytes))
        return std::nullopt;

    result.additionalCpuBytes = result.logicalCpuBytes;
    if (!Add(result.additionalCpuBytes, kObjectAllowance))
        return std::nullopt;
    if (descriptor.software && !Add(result.additionalCpuBytes, result.logicalGpuBytes))
        return std::nullopt;

    result.additionalGpuBytes = result.logicalGpuBytes;
    std::uint64_t gpuRoundingBytes = 0;
    if (!Multiply(gpuRoundingObjects, kObjectAllowance, gpuRoundingBytes) ||
        !Add(result.additionalGpuBytes, gpuRoundingBytes))
        return std::nullopt;
    return result;
}
} // namespace AdjustmentDisplayAdmission

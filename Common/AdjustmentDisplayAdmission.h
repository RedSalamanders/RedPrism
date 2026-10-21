#pragma once

#include <cstdint>
#include <optional>

// Defined by Common/Document.h. Keep this public boundary free of document, Win32, and graphics headers.
enum class AdjustmentKind;

namespace AdjustmentDisplayAdmission
{
struct Descriptor final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool liveAdjustment = false;
    bool selectionCoverage = false;
    AdjustmentKind kind{};
    bool software = false;
};

struct Envelope final
{
    std::uint64_t logicalCpuBytes = 0;
    std::uint64_t logicalGpuBytes = 0;
    std::uint64_t additionalCpuBytes = 0;
    std::uint64_t additionalGpuBytes = 0;
    std::uint32_t mipLevels = 0;
    std::uint32_t textureCount = 0;
};

// Returns empty for invalid geometry/kind or checked-arithmetic failure. This API captures no owners and performs
// no allocation, document traversal, GPU work, or thread interaction.
[[nodiscard]] std::optional<Envelope> Estimate(const Descriptor& descriptor) noexcept;
} // namespace AdjustmentDisplayAdmission

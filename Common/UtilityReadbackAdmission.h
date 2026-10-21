#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>

// Pure constant-time estimate from the immutable root's producer-cached ownership bounds.
// This describes a serial utility source, not display, encoding or a GPU-completion acknowledgement.
namespace UtilityReadbackAdmission
{
// Non-owning arithmetic input. Ownership bounds come from the immediate immutable document descriptor;
// synthetic test descriptors qualify policy arithmetic only, never real resource admission or capture.
struct Descriptor final
{
    int width = 0;
    int height = 0;
    std::uint64_t layerCount = 0;
    std::uint64_t sourceBytes = 0;
    std::uint64_t rasterNodes = 0;
    std::uint32_t tileSide = 0;
    std::uint64_t additionalCpuBytes = 0;
    bool software = false;
};

struct Envelope final
{
    std::uint64_t sourceBytes = 0;
    std::uint64_t rasterNodes = 0;
    std::uint64_t cpuBytes = 0;
    std::uint64_t gpuBytes = 0;
    std::uint64_t logicalGpuBytes = 0;
    std::uint32_t tileSide = 0;
};

[[nodiscard]] std::optional<Envelope> Estimate(const Descriptor& descriptor) noexcept;

// additionalCpuBytes includes the consumer's output/preprocessing/encoding/history overlap, before capture.
// Caller separately reserves the real adapter budget and retains CPU/GPU charges through original uses.
[[nodiscard]] std::optional<Envelope> Estimate(const CanvasDocument& source, std::uint32_t tileSide,
                                               std::uint64_t additionalCpuBytes, bool software) noexcept;
} // namespace UtilityReadbackAdmission

#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Spot healing (C11, Specs/Document/Document_Tools.md): the legacy HealPixels.c kernel, ported number for number.
namespace SpotHealing
{
// Half-open bounds of nonzero bytes in a gray bitmap; all zero when empty.
struct Bounds
{
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    [[nodiscard]] bool Empty() const noexcept
    {
        return x1 <= x0 || y1 <= y0;
    }
};
[[nodiscard]] Bounds CoverageBounds(const std::uint8_t* gray, std::size_t width, std::size_t height, std::size_t stride,
                                    const CancelToken& cancellation = {}) noexcept;

// Worker checkpoints borrow no document or graphics owner. Cancellation leaves only private scratch/output;
// the caller must refuse publication whenever HealForWork returns false.
enum class WorkStage : std::uint8_t
{
    Role,
    RingRows,
    RingColumns,
    PatchSearch,
    Statistics,
    SolveCoarse,
    Solve,
    Output
};
struct WorkObserver final
{
    void (*checkpoint)(void*, WorkStage) noexcept = nullptr;
    void* context = nullptr;
};
// Worker only. Dimension, pitched extent and both owned spans are validated before allocation/pixel access.
// Allocation failure propagates to the scheduler runner; the compatibility Heal API keeps its existing policy.
[[nodiscard]] bool HealForWork(std::span<std::uint8_t> bgra, std::span<const std::uint8_t> coverage, std::size_t width,
                               std::size_t height, std::size_t stride, float opacity, SpotHealingMode mode,
                               std::uint32_t seed, const CancelToken& cancellation = {}, WorkObserver observer = {});

// Heals in place over premultiplied BGRA (4 bytes per pixel, `stride` bytes per row). `coverage` (width × height
// bytes, 0–255) marks what to heal. Copied texture is blended so it meets the surrounding tone exactly: the
// difference along the spot's edge is spread smoothly across it (a membrane fill). The result replaces the original
// by coverage × opacity. False when memory runs out.
[[nodiscard]] bool Heal(std::uint8_t* bgra, const std::uint8_t* coverage, std::size_t width, std::size_t height,
                        std::size_t stride, float opacity, SpotHealingMode mode, std::uint32_t seed);
} // namespace SpotHealing

#pragma once

#include "Executor.h"
#include "TransformEdit.h"

#include <cstdint>

enum class DistortWorkStatus : std::uint8_t
{
    Ready,
    Invalid,
    TooLarge,
    Memory,
    Cancelled,
};

enum class DistortWorkStage : std::uint8_t
{
    Source,
    Pixels,
    Trim,
};

struct DistortWorkObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, DistortWorkStage) = nullptr;
};

// Owned immutable source and event-time geometry. No session, display cache or GPU engine is borrowed.
struct DistortWorkInput final
{
    Raster image;
    LayerTransform placement;
    std::array<Vec2, 4> corners;
    bool mask = false;
    double background = 0;
    double previewLimit = 0; // zero means full resolution; positive caps the longest output side
    bool trim = false;       // color-only visible-alpha trim, preserving the complete transparent grid
};

struct DistortWorkResult final
{
    DistortWorkStatus status = DistortWorkStatus::Invalid;
    Raster image;
    LayerTransform placement;
    int cropX = 0;
    int cropY = 0;
};

// Zero refuses invalid geometry, format, limits or overflow. Includes private source assembly and the maximum
// untrimmed/trimmed output overlap; shared published owners are retained by input, not allocated twice.
[[nodiscard]] std::uint64_t DistortWorkMemoryBytes(const DistortWorkInput& input) noexcept;
[[nodiscard]] DistortWorkResult RunDistortWork(const DistortWorkInput& input, Executor& executor,
                                               CancelToken cancellation = {}, DistortWorkObserver observer = {});

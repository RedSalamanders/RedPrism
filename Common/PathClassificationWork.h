#pragma once

#include "Executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

inline constexpr std::size_t kMaxPathClassificationBatch = 256;
inline constexpr std::size_t kMaxPathClassificationLength = 32767;

enum class PathClassification : std::uint8_t
{
    Project,
    Image
};

enum class PathClassificationStatus : std::uint8_t
{
    Ok,
    InvalidInput,
    Cancelled
};

struct PathClassificationBatch final
{
    PathClassificationStatus status = PathClassificationStatus::InvalidInput;
    std::array<PathClassification, kMaxPathClassificationBatch> classifications{};
    std::size_t count = 0;
};

// The caller owns every view's backing string and keeps it alive and unchanged for the duration of this call.
// Inputs are copied only into a bounded, terminated stack buffer before any Win32 path API reads them.
[[nodiscard]] PathClassificationBatch ClassifyPathBatch(std::span<const std::wstring_view> paths,
                                                        const CancelToken& cancellation = {}) noexcept;

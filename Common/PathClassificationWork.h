#pragma once

#include "Executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    Cancelled,
    OutOfMemory
};

using PathClassificationScratch = std::array<wchar_t, kMaxPathClassificationLength + 1>;
// Optional bounded scratch factory for controlled admission/failure tests. The fixed buffer type prevents
// a successful source from supplying an undersized allocation. Empty/default uses the production allocator.
struct PathClassificationScratchSource final
{
    void* context = nullptr;
    std::unique_ptr<PathClassificationScratch> (*create)(void*) noexcept = nullptr;
};

struct PathClassificationBatch final
{
    PathClassificationStatus status = PathClassificationStatus::InvalidInput;
    std::array<PathClassification, kMaxPathClassificationBatch> classifications{};
    std::size_t count = 0;
};

// The caller owns every view's backing string and keeps it alive and unchanged for the duration of this call.
// Inputs are copied into one bounded, terminated worker-owned scratch buffer before Win32 reads them.
// Its complete allocation is covered by the admitted job's existing 96 KiB scratch allowance. Refusal returns
// OutOfMemory with no partial result. Each read/cancellation is acknowledged before reuse/release.
[[nodiscard]] PathClassificationBatch ClassifyPathBatch(std::span<const std::wstring_view> paths,
                                                        const CancelToken& cancellation = {},
                                                        PathClassificationScratchSource scratchSource = {}) noexcept;

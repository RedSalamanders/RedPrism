#pragma once

// Isolated worker engine; callers must admit both scratch and final output before capture.
#include "Document.h"
#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace BoundedSelectionBoolean
{
enum class Operation : std::uint8_t
{
    Intersect,
    Union,
    Subtract
};
struct Limits
{
    std::size_t workspaceBytes = 128 * 1024 * 1024ULL; // scratch ceiling; output uses a separate admitted envelope
    // Additional deterministic allocation-count ceiling. Refuses before allocating the next arena object.
    std::size_t allocations = (std::numeric_limits<std::size_t>::max)();
    std::uint64_t inputPoints = 16'000'004ULL;
    std::uint64_t inputLoops = inputPoints;
    std::uint64_t outputPoints = 8'000'000ULL;
    std::uint64_t outputLoops = outputPoints / 3 + 1;
};
struct Statistics
{
    std::size_t workspaceCapacity = 0;
    std::size_t workspaceConsumed = 0;
    std::size_t allocations = 0;
    std::uint64_t workUnits = 0;
    std::uint64_t outputPoints = 0;
    std::uint64_t outputLoops = 0;
};

// Worker-only. A null result is invalid input or cancellation before engine entry; the caller's original
// stopped token determines Cancelled. Engine memory/cap/cancellation refusal reaches the original
// job runner as bad_alloc. It owns the only allocation-failure translation and checks the stopped token.
// Result vectors are ordinary storage and need their separately admitted output envelope/lifetime.
[[nodiscard]] std::optional<std::vector<SelectionLoop>> Run(const std::vector<SelectionLoop>& subject,
                                                            const std::vector<SelectionLoop>& clip, Operation operation,
                                                            CancelToken cancellation, Limits limits = {},
                                                            Statistics* statistics = nullptr,
                                                            void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr,
                                                            void* context = nullptr);
} // namespace BoundedSelectionBoolean

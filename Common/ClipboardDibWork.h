#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>
#include <span>
#include <vector>

enum class ClipboardDibStatus
{
    Ok,
    Invalid,
    TooLarge,
    OutOfMemory,
    Cancelled,
};

inline constexpr std::uint64_t kClipboardDibEncodedByteLimit = 512ull * 1024ull * 1024ull;

[[nodiscard]] constexpr bool ClipboardDibEncodedSizeAllowed(std::uint64_t bytes) noexcept
{
    return bytes <= kClipboardDibEncodedByteLimit;
}

struct ClipboardDibMetadata final
{
    int width = 0;
    int height = 0;
    std::uint64_t pixels = 0;
};

[[nodiscard]] ClipboardDibStatus ProbeClipboardDib(std::span<const std::uint8_t> bytes, std::int64_t pixelBudget,
                                                   ClipboardDibMetadata& metadata,
                                                   CancelToken cancellation = {}) noexcept;
[[nodiscard]] ClipboardDibStatus DecodeClipboardDib(std::span<const std::uint8_t> bytes, std::int64_t pixelBudget,
                                                    Raster& image, CancelToken cancellation = {});
[[nodiscard]] ClipboardDibStatus EncodeClipboardDibV5(const Raster& image, std::vector<std::uint8_t>& bytes,
                                                      CancelToken cancellation = {});

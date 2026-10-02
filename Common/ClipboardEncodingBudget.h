#pragma once

#include <cstdint>

// Fixed object allowance, separate from payload allocations and the hash's 4096-byte object buffer.
inline constexpr std::uint64_t kClipboardEncodingFixedMetadataBytes = 1024;

// Conservative extra peak-byte estimate for the all-format clipboard encoder.
// WIC's private workspace is represented by a declared allowance, not a hard bound.
[[nodiscard]] std::uint64_t ClipboardEncodingBudget(int width, int height) noexcept;

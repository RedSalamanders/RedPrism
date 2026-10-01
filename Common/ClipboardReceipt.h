#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ClipboardReceipt
{

struct Key final
{
    std::array<std::uint8_t, 16> session{};
    std::uint64_t serial{};

    bool operator==(const Key&) const noexcept = default;
};

inline constexpr std::size_t kEncodedBytes = 40;

[[nodiscard]] bool Encode(const Key& key, std::array<std::uint8_t, kEncodedBytes>& output) noexcept;
[[nodiscard]] bool Decode(std::span<const std::uint8_t> input, Key& output) noexcept;

} // namespace ClipboardReceipt

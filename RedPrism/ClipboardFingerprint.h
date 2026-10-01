#pragma once

#include "ClipboardReceipt.h"
#include "Executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ClipboardFingerprint
{
struct Format final
{
    std::uint64_t bytes = 0;
    std::array<std::uint8_t, 32> sha256{};
    [[nodiscard]] bool operator==(const Format&) const noexcept = default;
};
struct Binding final
{
    ClipboardReceipt::Key key;
    Format png, dib;
};
enum class Stage : std::uint8_t
{
    Create,
    Band,
    Finish
};
struct Observer final
{
    void* context = nullptr;
    void (*reached)(void*, Stage, std::size_t) noexcept = nullptr;
    // Negative NTSTATUS injected before the named call, never a forged successful digest.
    std::int32_t (*fault)(void*, Stage) noexcept = nullptr;
};
// Worker only. Fixed 64 KiB bands and a borrowed CNG SHA256 pseudo-handle; WIL owns the private hash.
// Success is zero, cancellation is E_ABORT, other failures are exact HRESULTs. Failure clears output.
[[nodiscard]] std::int32_t Hash(std::span<const std::uint8_t> bytes, Format& output, CancelToken cancellation = {},
                                Observer observer = {}) noexcept;
// Metadata only, after owned bytes have been hashed on a worker. Every copied format must match and at least
// one must be present. Neither the receipt alone nor a newly sampled clipboard sequence authorizes origin.
[[nodiscard]] bool Matches(const Binding& expected, const ClipboardReceipt::Key& currentPrivate,
                           const std::optional<ClipboardReceipt::Key>& receipt, const Format& png,
                           const Format& dib) noexcept;
} // namespace ClipboardFingerprint

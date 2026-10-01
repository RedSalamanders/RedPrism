#include "ClipboardReceipt.h"

namespace ClipboardReceipt
{
namespace
{

constexpr std::array<std::uint8_t, 8> kMagic{'R', 'P', 'C', 'L', 'I', 'P', '0', '1'};
constexpr std::uint32_t kVersion = 1;

void StoreU32(std::uint8_t* destination, std::uint32_t value) noexcept
{
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8);
    destination[2] = static_cast<std::uint8_t>(value >> 16);
    destination[3] = static_cast<std::uint8_t>(value >> 24);
}

void StoreU64(std::uint8_t* destination, std::uint64_t value) noexcept
{
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8);
    destination[2] = static_cast<std::uint8_t>(value >> 16);
    destination[3] = static_cast<std::uint8_t>(value >> 24);
    destination[4] = static_cast<std::uint8_t>(value >> 32);
    destination[5] = static_cast<std::uint8_t>(value >> 40);
    destination[6] = static_cast<std::uint8_t>(value >> 48);
    destination[7] = static_cast<std::uint8_t>(value >> 56);
}

std::uint32_t LoadU32(const std::uint8_t* source) noexcept
{
    return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8) |
           (static_cast<std::uint32_t>(source[2]) << 16) | (static_cast<std::uint32_t>(source[3]) << 24);
}

std::uint64_t LoadU64(const std::uint8_t* source) noexcept
{
    return static_cast<std::uint64_t>(source[0]) | (static_cast<std::uint64_t>(source[1]) << 8) |
           (static_cast<std::uint64_t>(source[2]) << 16) | (static_cast<std::uint64_t>(source[3]) << 24) |
           (static_cast<std::uint64_t>(source[4]) << 32) | (static_cast<std::uint64_t>(source[5]) << 40) |
           (static_cast<std::uint64_t>(source[6]) << 48) | (static_cast<std::uint64_t>(source[7]) << 56);
}

bool IsNonzeroSession(const std::array<std::uint8_t, 16>& session) noexcept
{
    for (const std::uint8_t byte : session)
    {
        if (byte != 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool Encode(const Key& key, std::array<std::uint8_t, kEncodedBytes>& output) noexcept
{
    output.fill(0);
    if (!IsNonzeroSession(key.session) || key.serial == 0)
    {
        return false;
    }

    for (std::size_t index = 0; index < kMagic.size(); ++index)
    {
        output[index] = kMagic[index];
    }
    StoreU32(output.data() + 8, kVersion);
    StoreU32(output.data() + 12, 0);
    for (std::size_t index = 0; index < key.session.size(); ++index)
    {
        output[16 + index] = key.session[index];
    }
    StoreU64(output.data() + 32, key.serial);
    return true;
}

bool Decode(std::span<const std::uint8_t> input, Key& output) noexcept
{
    output = {};
    if (input.size() != kEncodedBytes || input.data() == nullptr)
    {
        return false;
    }

    for (std::size_t index = 0; index < kMagic.size(); ++index)
    {
        if (input[index] != kMagic[index])
        {
            return false;
        }
    }
    if (LoadU32(input.data() + 8) != kVersion || LoadU32(input.data() + 12) != 0)
    {
        return false;
    }

    Key decoded{};
    for (std::size_t index = 0; index < decoded.session.size(); ++index)
    {
        decoded.session[index] = input[16 + index];
    }
    decoded.serial = LoadU64(input.data() + 32);
    if (!IsNonzeroSession(decoded.session) || decoded.serial == 0)
    {
        return false;
    }

    output = decoded;
    return true;
}

} // namespace ClipboardReceipt

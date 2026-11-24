#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

struct FileOutputVersion final
{
    std::array<std::uint8_t, 16> fileId{};
    std::uint64_t size         = 0;
    std::int64_t lastWriteTime = 0;
    std::int64_t changeTime    = 0;
    std::uint64_t volume       = 0;

    bool operator==(const FileOutputVersion&) const noexcept = default;
};

struct FileOutputTarget final
{
    std::wstring path;
    // An absent version authorizes creation only.
    std::optional<FileOutputVersion> acceptedVersion;
};

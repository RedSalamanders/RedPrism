#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <windows.h>

struct AdapterOutputRecord final
{
    LUID adapterLuid{};
    HMONITOR monitor = nullptr;
    bool softwareAdapter = false;
};

[[nodiscard]] constexpr bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

// Adapter-of-output: first non-software record whose output owns `target`, or SIZE_MAX when nothing matches.
[[nodiscard]] constexpr size_t SelectAdapterRecordForMonitor(std::span<const AdapterOutputRecord> records,
                                                             HMONITOR target) noexcept
{
    if (!target)
    {
        return SIZE_MAX;
    }
    for (size_t index = 0; index < records.size(); ++index)
    {
        if (!records[index].softwareAdapter && records[index].monitor == target)
        {
            return index;
        }
    }
    return SIZE_MAX;
}

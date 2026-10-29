#pragma once

#include "Common/HistoryWork.h"

// Owner-only native fixture access. Restoring a clock may not predate any started operation; production
// constructors and NextOrdinal remain unchanged. This adds no member, allocation or production entrypoint.
struct HistoryWorkFixture final
{
    [[nodiscard]] static std::uint64_t NextValue(const HistoryWork& history) noexcept
    {
        return history._next;
    }
    [[nodiscard]] static bool SetNext(HistoryWork& history, std::uint64_t next) noexcept
    {
        if (history._active || next < history._lastStarted)
            return false;
        history._next = next;
        return true;
    }
};

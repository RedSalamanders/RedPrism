#pragma once

#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Owned harness gate. Absent in production; a worker posts the message after copying, while its rows remain lent.
struct UtilityWorkTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4b2;
    wil::unique_handle release;
    std::atomic<unsigned int> visits{0};
    JobResult result;
};

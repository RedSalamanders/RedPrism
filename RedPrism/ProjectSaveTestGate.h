#pragma once

#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct ProjectSaveTestGate final
{
    static constexpr unsigned int kMessage = WM_APP + 0x4B5;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool afterPublication = false;
    bool throwAllocationFailure = false;
    bool throwAdmissionFailure = false;
    JobResult result;
    std::atomic<unsigned int> visits{0};
};

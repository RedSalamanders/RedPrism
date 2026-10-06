#pragma once

#include "Jobs.h"

#include <atomic>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Owned event-gated harness state; absent from production. Publication occurs on the worker before its message.
struct ExportWorkTestGate final
{
    static constexpr UINT kMessage = WM_APP + 0x4b6;
    static constexpr UINT kReadbackMessage = WM_APP + 0x5f4;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> visits{0}, encodeAttempts{0};
    std::atomic<double> quality{0}, resolution{0};
    JobResult result;
    bool afterPublication = false;
    bool holdReadback = false; // hold the actual worker snapshot stage before any GPU tile
    bool throwAllocationFailure = false;
    bool throwAdmissionFailure = false;
};

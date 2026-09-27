#include "PipelineTrace.h"

#include <windows.h>

#include <TraceLoggingProvider.h>
#include <winmeta.h>

#pragma comment(lib, "advapi32.lib")

TRACELOGGING_DEFINE_PROVIDER(g_pipelineTraceProvider, "RedPrism.Pipeline",
                             (0x7b5d9c3e, 0x42f1, 0x4b76, 0x9a, 0x2d, 0x61, 0xe8, 0xc4, 0xf0, 0x37, 0xa5));

namespace
{
constexpr std::uint64_t kPipelineTraceFreeThresholdBytes = 1'048'576;
DWORD g_pipelineTraceOwnerThreadId = 0;
bool g_pipelineTraceRegistered = false;
} // namespace

std::int32_t InitializePipelineTrace() noexcept
{
    if (g_pipelineTraceRegistered)
    {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED));
    }

    const HRESULT status = TraceLoggingRegister(g_pipelineTraceProvider);
    if (FAILED(status))
    {
        g_pipelineTraceOwnerThreadId = 0;
        return static_cast<std::int32_t>(status);
    }

    g_pipelineTraceOwnerThreadId = GetCurrentThreadId();
    g_pipelineTraceRegistered = true;
    return static_cast<std::int32_t>(status);
}

void ClosePipelineTrace() noexcept
{
    if (!g_pipelineTraceRegistered)
    {
        return;
    }

    TraceLoggingUnregister(g_pipelineTraceProvider);
    g_pipelineTraceOwnerThreadId = 0;
    g_pipelineTraceRegistered = false;
}

void TracePipelineFree(std::uint64_t bytes) noexcept
{
    if (!g_pipelineTraceRegistered || bytes < kPipelineTraceFreeThresholdBytes ||
        GetCurrentThreadId() != g_pipelineTraceOwnerThreadId)
    {
        return;
    }

    TraceLoggingWrite(g_pipelineTraceProvider, "UiFree", TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                      TraceLoggingKeyword(0x1), TraceLoggingUInt64(bytes, "bytes"));
}

void TracePipelineMemory(const char* event, std::uint64_t requested, std::uint64_t reserved,
                         std::uint64_t releasing) noexcept
{
    if (!g_pipelineTraceRegistered || event == nullptr)
    {
        return;
    }

    TraceLoggingWrite(g_pipelineTraceProvider, "Memory", TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                      TraceLoggingKeyword(0x2), TraceLoggingString(event, "event"),
                      TraceLoggingUInt64(requested, "requested"), TraceLoggingUInt64(reserved, "reserved"),
                      TraceLoggingUInt64(releasing, "releasing"));
}

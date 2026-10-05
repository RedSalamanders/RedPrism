#include "PipelineTraceTests.h"

#include "../../Common/Document.h"
#include "../../Common/Jobs.h"
#include "../../Common/PerfRoot.h"
#include "../../Common/PipelineTrace.h"
#include "../../Common/UiTiming.h"

#include <windows.h>

#include <evntrace.h>
#include <tdh.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "tdh.lib")

namespace
{
constexpr GUID kPipelineTraceProviderGuid{0x7b5d9c3e, 0x42f1, 0x4b76, {0x9a, 0x2d, 0x61, 0xe8, 0xc4, 0xf0, 0x37, 0xa5}};
constexpr std::uint64_t kOneMiB = 1'048'576;
constexpr std::uint32_t kUiTimingSentinelDetail = 0xa100;
constexpr std::uint32_t kUiTimingSuppressedDetail = 0xb100;
constexpr std::uint32_t kUiTimingWorkerDetail = 0xc100;
constexpr std::uint32_t kUiResourceStageSentinelDetail = 0xd100;
constexpr std::uint32_t kUiResourceStageWorkerDetail = 0xe100;
constexpr std::uint32_t kUiResourceStageSuppressedDetail = 0xf100;

constexpr std::array<std::uint32_t, 6> kUiResourceStageExpectedIds{6, 7, 8, 9, 10, 11};

struct TimingSentinel final
{
    std::uint32_t detail;
    std::uint64_t durationUs;
};

constexpr std::array<TimingSentinel, 6> kUiTimingSentinels{{
    {kUiTimingSentinelDetail + 1, 91'001},
    {kUiTimingSentinelDetail + 2, 92'002},
    {0, 93'003},
    {kUiTimingSentinelDetail + 4, 94'004},
    {0, 95'005},
    {0, 96'006},
}};
constexpr std::array<TimingSentinel, 3> kUiTimingSuppressedSentinels{{
    {kUiTimingSuppressedDetail + 1, 4'000},
    {kUiTimingSuppressedDetail + 2, 4'000},
    {0, 4'000},
}};
constexpr std::array<TimingSentinel, 6> kUiTimingWorkerSentinels{{
    {kUiTimingWorkerDetail + 1, 101'001},
    {kUiTimingWorkerDetail + 2, 102'002},
    {0, 103'003},
    {kUiTimingWorkerDetail + 4, 104'004},
    {0, 105'005},
    {0, 106'006},
}};
constexpr std::array<TimingSentinel, 6> kUiResourceStageSentinels{{
    {kUiResourceStageSentinelDetail + 1, 201'001},
    {kUiResourceStageSentinelDetail + 2, 202'002},
    {kUiResourceStageSentinelDetail + 3, 203'003},
    {kUiResourceStageSentinelDetail + 4, 204'004},
    {kUiResourceStageSentinelDetail + 5, 205'005},
    {kUiResourceStageSentinelDetail + 6, 206'006},
}};
constexpr std::array<TimingSentinel, 6> kUiResourceStageWorkerSentinels{{
    {kUiResourceStageWorkerDetail + 1, 211'001},
    {kUiResourceStageWorkerDetail + 2, 212'002},
    {kUiResourceStageWorkerDetail + 3, 213'003},
    {kUiResourceStageWorkerDetail + 4, 214'004},
    {kUiResourceStageWorkerDetail + 5, 215'005},
    {kUiResourceStageWorkerDetail + 6, 216'006},
}};
constexpr TimingSentinel kUiResourceStageSuppressedSentinel{kUiResourceStageSuppressedDetail + 1, 4'000};
constexpr std::array<std::wstring_view, 6> kUiTimingEventNames{L"UiDispatch", L"UiMessage", L"UiDrain",
                                                               L"UiApply",    L"UiAction",  L"UiFrame"};

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s pipeline-trace.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

struct SessionProperties final
{
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t logFile[1024]{};
    wchar_t loggerName[1024]{};
};

struct EventCapture final
{
    DWORD uiThreadId = 0;
    std::uint32_t uiFreeEvents = 0;
    std::uint32_t workerFreeEvents = 0;
    std::uint32_t otherThreadFreeEvents = 0;
    std::uint32_t undecodedEvents = 0;
    std::uint32_t largeUiFrees = 0;
    std::uint64_t uiFreeBytes = 0;
    std::array<std::uint32_t, 6> uiTimingEvents{};
    std::array<std::uint32_t, 6> uiTimingOwnerEvents{};
    std::array<std::uint32_t, 6> uiTimingKeywordEvents{};
    std::array<std::uint32_t, 3> suppressedTimingEvents{};
    std::array<std::uint32_t, 6> workerTimingEvents{};
    std::array<std::uint32_t, 6> uiResourceStageEvents{};
    std::array<std::uint32_t, 6> uiResourceStageOwnerEvents{};
    std::array<std::uint32_t, 6> uiResourceStageKeywordEvents{};
    std::array<std::uint32_t, 6> workerResourceStageEvents{};
    std::uint32_t suppressedChromeSyncEvents = 0;
};

bool ReadTraceUInt32(PEVENT_RECORD record, const wchar_t* name, std::uint32_t& value) noexcept
{
    PROPERTY_DATA_DESCRIPTOR property{};
    property.PropertyName = reinterpret_cast<ULONGLONG>(name);
    property.ArrayIndex = ULONG_MAX;
    return TdhGetProperty(record, 0, nullptr, 1, &property, sizeof(value), reinterpret_cast<BYTE*>(&value)) ==
           ERROR_SUCCESS;
}

bool ReadTraceUInt64(PEVENT_RECORD record, const wchar_t* name, std::uint64_t& value) noexcept
{
    PROPERTY_DATA_DESCRIPTOR property{};
    property.PropertyName = reinterpret_cast<ULONGLONG>(name);
    property.ArrayIndex = ULONG_MAX;
    return TdhGetProperty(record, 0, nullptr, 1, &property, sizeof(value), reinterpret_cast<BYTE*>(&value)) ==
           ERROR_SUCCESS;
}

void WINAPI CaptureEvent(PEVENT_RECORD record) noexcept
{
    if (record == nullptr || record->UserContext == nullptr ||
        !IsEqualGUID(record->EventHeader.ProviderId, kPipelineTraceProviderGuid))
    {
        return;
    }

    auto& capture = *static_cast<EventCapture*>(record->UserContext);
    alignas(TRACE_EVENT_INFO) std::array<std::byte, 4096> metadata{};
    ULONG metadataBytes = static_cast<ULONG>(metadata.size());
    if (TdhGetEventInformation(record, 0, nullptr, reinterpret_cast<PTRACE_EVENT_INFO>(metadata.data()),
                               &metadataBytes) != ERROR_SUCCESS)
    {
        ++capture.undecodedEvents;
        return;
    }

    const auto* eventInfo = reinterpret_cast<const TRACE_EVENT_INFO*>(metadata.data());
    const ULONG nameOffset = eventInfo->EventNameOffset;
    if (nameOffset < sizeof(TRACE_EVENT_INFO) || nameOffset >= metadataBytes || metadataBytes > metadata.size() ||
        (metadataBytes - nameOffset) < sizeof(wchar_t))
    {
        ++capture.undecodedEvents;
        return;
    }

    const auto* eventName = reinterpret_cast<const wchar_t*>(metadata.data() + nameOffset);
    const std::size_t availableChars = (metadataBytes - nameOffset) / sizeof(wchar_t);
    std::size_t eventNameLength = 0;
    while (eventNameLength < availableChars && eventName[eventNameLength] != L'\0')
    {
        ++eventNameLength;
    }
    if (eventNameLength == availableChars)
    {
        return;
    }

    const std::wstring_view eventNameView{eventName, eventNameLength};
    if (eventNameView == L"UiResourceStage")
    {
        std::uint32_t stage = 0;
        std::uint32_t detail = 0;
        std::uint64_t durationUs = 0;
        if (!ReadTraceUInt32(record, L"stage", stage) || !ReadTraceUInt32(record, L"detail", detail) ||
            !ReadTraceUInt64(record, L"durationUs", durationUs))
        {
            ++capture.undecodedEvents;
            return;
        }

        if (stage < kUiResourceStageExpectedIds.front() || stage > kUiResourceStageExpectedIds.back())
        {
            return;
        }
        const std::size_t stageIndex = stage - kUiResourceStageExpectedIds.front();
        if (stage != kUiResourceStageExpectedIds[stageIndex])
        {
            return;
        }

        const TimingSentinel ownerSentinel = kUiResourceStageSentinels[stageIndex];
        if (detail == ownerSentinel.detail && durationUs == ownerSentinel.durationUs)
        {
            ++capture.uiResourceStageEvents[stageIndex];
            if (record->EventHeader.ThreadId == capture.uiThreadId)
            {
                ++capture.uiResourceStageOwnerEvents[stageIndex];
            }
            if (record->EventHeader.EventDescriptor.Keyword == 0x4)
            {
                ++capture.uiResourceStageKeywordEvents[stageIndex];
            }
        }

        const TimingSentinel workerSentinel = kUiResourceStageWorkerSentinels[stageIndex];
        if (detail == workerSentinel.detail && durationUs == workerSentinel.durationUs)
        {
            ++capture.workerResourceStageEvents[stageIndex];
        }
        if (stage == kUiResourceStageExpectedIds.back() && detail == kUiResourceStageSuppressedSentinel.detail &&
            durationUs == kUiResourceStageSuppressedSentinel.durationUs)
        {
            ++capture.suppressedChromeSyncEvents;
        }
        return;
    }

    if (eventNameView != L"UiFree")
    {
        std::size_t timingIndex = 0;
        while (timingIndex < kUiTimingEventNames.size() && eventNameView != kUiTimingEventNames[timingIndex])
        {
            ++timingIndex;
        }
        if (timingIndex == kUiTimingEventNames.size())
        {
            return;
        }

        std::uint32_t detail = 0;
        const bool hasDetail = timingIndex == 0 || timingIndex == 1 || timingIndex == 3;
        const wchar_t* const detailName = timingIndex == 3 ? L"jobClass" : L"msg";
        std::uint64_t durationUs = 0;
        if ((hasDetail && !ReadTraceUInt32(record, detailName, detail)) ||
            !ReadTraceUInt64(record, L"durationUs", durationUs))
        {
            ++capture.undecodedEvents;
            return;
        }

        const auto matches = [detail, durationUs, hasDetail](const TimingSentinel& sentinel) noexcept
        { return durationUs == sentinel.durationUs && (!hasDetail || detail == sentinel.detail); };
        if (timingIndex < kUiTimingSentinels.size() && matches(kUiTimingSentinels[timingIndex]))
        {
            ++capture.uiTimingEvents[timingIndex];
            if (record->EventHeader.EventDescriptor.Keyword == 0x4)
            {
                ++capture.uiTimingKeywordEvents[timingIndex];
            }
            if (record->EventHeader.ThreadId == capture.uiThreadId)
            {
                ++capture.uiTimingOwnerEvents[timingIndex];
            }
        }
        if (timingIndex < kUiTimingSuppressedSentinels.size() && matches(kUiTimingSuppressedSentinels[timingIndex]))
        {
            ++capture.suppressedTimingEvents[timingIndex];
        }
        if (timingIndex < kUiTimingWorkerSentinels.size() && matches(kUiTimingWorkerSentinels[timingIndex]))
        {
            ++capture.workerTimingEvents[timingIndex];
        }
        return;
    }

    if (record->EventHeader.ThreadId == capture.uiThreadId)
    {
        ++capture.uiFreeEvents;
        PROPERTY_DATA_DESCRIPTOR property{};
        property.PropertyName = reinterpret_cast<ULONGLONG>(L"bytes");
        property.ArrayIndex = ULONG_MAX;
        std::uint64_t bytes = 0;
        if (TdhGetProperty(record, 0, nullptr, 1, &property, sizeof(bytes), reinterpret_cast<BYTE*>(&bytes)) !=
            ERROR_SUCCESS)
        {
            ++capture.undecodedEvents;
        }
        else
        {
            capture.uiFreeBytes += bytes;
            capture.largeUiFrees += bytes >= 16 * kOneMiB ? 1 : 0;
        }
    }
    else if (record->EventHeader.ThreadId != 0)
    {
        ++capture.workerFreeEvents;
    }
    else
    {
        ++capture.otherThreadFreeEvents;
    }
}

bool BuildAbsoluteLogPath(std::wstring& path)
{
    std::wstring root;
    if (FAILED(static_cast<HRESULT>(ResolvePerfRoot(root))))
    {
        return false;
    }
    path = root + L"\\PipelineTrace-" + std::to_wstring(GetCurrentProcessId()) + L".etl";
    return true;
}

bool BuildSessionNames(SessionProperties& session, std::wstring& loggerName)
{
    loggerName = L"RedPrism.PipelineTrace-" + std::to_wstring(GetCurrentProcessId());
    if (loggerName.size() >= std::size(session.loggerName))
    {
        return false;
    }
    return wcscpy_s(session.loggerName, loggerName.c_str()) == 0;
}

bool StartCapture(SessionProperties& session, TRACEHANDLE& traceSession, const std::wstring& loggerName,
                  const std::wstring& logPath) noexcept
{
    if (wcscpy_s(session.logFile, logPath.c_str()) != 0)
    {
        return false;
    }
    session.properties.Wnode.BufferSize = static_cast<ULONG>(sizeof(SessionProperties));
    session.properties.Wnode.Guid = kPipelineTraceProviderGuid;
    session.properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    session.properties.BufferSize = 64;
    session.properties.MinimumBuffers = 2;
    session.properties.MaximumBuffers = 8;
    session.properties.LogFileMode =
        EVENT_TRACE_FILE_MODE_SEQUENTIAL | EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC;
    session.properties.LogFileNameOffset = static_cast<ULONG>(offsetof(SessionProperties, logFile));
    session.properties.LoggerNameOffset = static_cast<ULONG>(offsetof(SessionProperties, loggerName));

    const ULONG startStatus = StartTraceW(&traceSession, loggerName.c_str(), &session.properties);
    if (startStatus != ERROR_SUCCESS)
    {
        std::printf("INFO pipeline-trace.StartTraceW=%lu\n", startStatus);
        return false;
    }

    const ULONG enableStatus =
        EnableTraceEx2(traceSession, &kPipelineTraceProviderGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_VERBOSE, 0xffffffffffffffffULL, 0, 0, nullptr);
    if (enableStatus != ERROR_SUCCESS)
    {
        std::printf("INFO pipeline-trace.EnableTraceEx2=%lu\n", enableStatus);
        (void)ControlTraceW(traceSession, loggerName.c_str(), &session.properties, EVENT_TRACE_CONTROL_STOP);
        traceSession = 0;
        return false;
    }
    return true;
}
} // namespace

int RunPipelineTraceTests()
{
    g_failures = 0;
    std::wstring logPath;
    Expect(BuildAbsoluteLogPath(logPath), "build-etl-path");
    if (logPath.empty())
    {
        return g_failures;
    }

    const std::wstring suffixedLogPath = logPath + L"_" + std::to_wstring(GetCurrentProcessId());
    (void)DeleteFileW(logPath.c_str());
    (void)DeleteFileW(suffixedLogPath.c_str());
    auto deleteEtls = wil::scope_exit(
        [&]() noexcept
        {
            (void)DeleteFileW(logPath.c_str());
            (void)DeleteFileW(suffixedLogPath.c_str());
        });

    const std::int32_t initializeStatus = InitializePipelineTrace();
    Expect(SUCCEEDED(static_cast<HRESULT>(initializeStatus)), "initialize-provider");
    if (FAILED(static_cast<HRESULT>(initializeStatus)))
    {
        return g_failures;
    }
    auto closeProvider = wil::scope_exit([]() noexcept { ClosePipelineTrace(); });

    SessionProperties session{};
    std::wstring loggerName;
    Expect(BuildSessionNames(session, loggerName), "build-session-name");
    if (loggerName.empty())
    {
        return g_failures;
    }

    TRACEHANDLE traceSession = 0;
    if (!StartCapture(session, traceSession, loggerName, logPath))
    {
        Expect(false, "start-private-in-process-etw-session");
        return g_failures;
    }
    bool sessionRunning = true;
    auto stopSession = wil::scope_exit(
        [&]() noexcept
        {
            if (sessionRunning)
            {
                (void)ControlTraceW(traceSession, loggerName.c_str(), &session.properties, EVENT_TRACE_CONTROL_STOP);
            }
        });
    Expect(true, "start-private-in-process-etw-session");

    EventCapture capture{};
    capture.uiThreadId = GetCurrentThreadId();
    Expect(PipelineUiTimingEnabled(), "ui-timing-enabled-by-private-capture");

    TracePipelineUi(UiTimingKind::MessageDispatch, kUiTimingSentinels[0].durationUs, kUiTimingSentinels[0].detail);
    TracePipelineUi(UiTimingKind::WindowMessage, kUiTimingSentinels[1].durationUs, kUiTimingSentinels[1].detail);
    TracePipelineUi(UiTimingKind::CompletionDrain, kUiTimingSentinels[2].durationUs, 0);
    TracePipelineUi(UiTimingKind::ResultApply, kUiTimingSentinels[3].durationUs, kUiTimingSentinels[3].detail);
    TracePipelineUi(UiTimingKind::UiAction, kUiTimingSentinels[4].durationUs, 0);
    TracePipelineUi(UiTimingKind::Frame, kUiTimingSentinels[5].durationUs, 0);

    TracePipelineUi(UiTimingKind::TextureCreate, kUiResourceStageSentinels[0].durationUs,
                    kUiResourceStageSentinels[0].detail);
    TracePipelineUi(UiTimingKind::ShaderViewCreate, kUiResourceStageSentinels[1].durationUs,
                    kUiResourceStageSentinels[1].detail);
    TracePipelineUi(UiTimingKind::MipGeneration, kUiResourceStageSentinels[2].durationUs,
                    kUiResourceStageSentinels[2].detail);
    TracePipelineUi(UiTimingKind::RasterUpload, kUiResourceStageSentinels[3].durationUs,
                    kUiResourceStageSentinels[3].detail);
    TracePipelineUi(UiTimingKind::CompositionRecord, kUiResourceStageSentinels[4].durationUs,
                    kUiResourceStageSentinels[4].detail);
    TracePipelineUi(UiTimingKind::ChromeSync, kUiResourceStageSentinels[5].durationUs,
                    kUiResourceStageSentinels[5].detail);
    TracePipelineUi(UiTimingKind::ChromeSync, kUiResourceStageSuppressedSentinel.durationUs,
                    kUiResourceStageSuppressedSentinel.detail);

    TracePipelineUi(UiTimingKind::MessageDispatch, kUiTimingSuppressedSentinels[0].durationUs,
                    kUiTimingSuppressedSentinels[0].detail);
    TracePipelineUi(UiTimingKind::WindowMessage, kUiTimingSuppressedSentinels[1].durationUs,
                    kUiTimingSuppressedSentinels[1].detail);
    TracePipelineUi(UiTimingKind::CompletionDrain, kUiTimingSuppressedSentinels[2].durationUs, 0);

    std::atomic<bool> workerTimingEnabled{true};
    TracePipelineFree(kOneMiB - 1);
    TracePipelineFree(kOneMiB);
    std::thread worker(
        [&workerTimingEnabled]() noexcept
        {
            workerTimingEnabled.store(PipelineUiTimingEnabled(), std::memory_order_relaxed);
            TracePipelineFree(kOneMiB * 2);
            TracePipelineUi(UiTimingKind::MessageDispatch, kUiTimingWorkerSentinels[0].durationUs,
                            kUiTimingWorkerSentinels[0].detail);
            TracePipelineUi(UiTimingKind::WindowMessage, kUiTimingWorkerSentinels[1].durationUs,
                            kUiTimingWorkerSentinels[1].detail);
            TracePipelineUi(UiTimingKind::CompletionDrain, kUiTimingWorkerSentinels[2].durationUs, 0);
            TracePipelineUi(UiTimingKind::ResultApply, kUiTimingWorkerSentinels[3].durationUs,
                            kUiTimingWorkerSentinels[3].detail);
            TracePipelineUi(UiTimingKind::UiAction, kUiTimingWorkerSentinels[4].durationUs, 0);
            TracePipelineUi(UiTimingKind::Frame, kUiTimingWorkerSentinels[5].durationUs, 0);
            TracePipelineUi(UiTimingKind::TextureCreate, kUiResourceStageWorkerSentinels[0].durationUs,
                            kUiResourceStageWorkerSentinels[0].detail);
            TracePipelineUi(UiTimingKind::ShaderViewCreate, kUiResourceStageWorkerSentinels[1].durationUs,
                            kUiResourceStageWorkerSentinels[1].detail);
            TracePipelineUi(UiTimingKind::MipGeneration, kUiResourceStageWorkerSentinels[2].durationUs,
                            kUiResourceStageWorkerSentinels[2].detail);
            TracePipelineUi(UiTimingKind::RasterUpload, kUiResourceStageWorkerSentinels[3].durationUs,
                            kUiResourceStageWorkerSentinels[3].detail);
            TracePipelineUi(UiTimingKind::CompositionRecord, kUiResourceStageWorkerSentinels[4].durationUs,
                            kUiResourceStageWorkerSentinels[4].detail);
            TracePipelineUi(UiTimingKind::ChromeSync, kUiResourceStageWorkerSentinels[5].durationUs,
                            kUiResourceStageWorkerSentinels[5].detail);
        });
    worker.join();
    Expect(!workerTimingEnabled.load(std::memory_order_relaxed), "worker-ui-timing-disabled");

    // A real inline free is a second positive control: the production allocator must reach the provider.
    {
        auto small = PixelBuffer::TryAllocate(kOneMiB);
        Expect(static_cast<bool>(small), "real-inline-allocation");
    }
    {
        JobScheduler scheduler;
        Expect(scheduler.Initialize(2) == S_OK && scheduler.EnableDeferredRelease(), "real-release-queue");
        Expect(InitializePixelBufferRecycle(128 * kOneMiB), "real-recycle-initialize");
        auto closePool = wil::scope_exit([]() noexcept { ClosePixelBufferRecycle(); });
        auto direct = PixelBuffer::TryAllocate(16 * kOneMiB);
        auto adopted = PixelBuffer::TryAllocate(16 * kOneMiB);
        Expect(direct && adopted, "real-large-allocations");
        if (direct && adopted)
        {
            std::memset(direct.Data(), 0x37, direct.Size());
            std::memset(adopted.Data(), 0x75, adopted.Size());
            auto image = Raster::Adopt(2048, 2048, RasterFormat::Bgra8888, std::move(adopted));
            auto copied = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
            direct = {};
            image = {};
            copied = {};
            Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "real-large-releases-settled");
            Expect(PixelBufferRecycleBytes() == 2 * PixelBuffer::RequiredBytes(16 * kOneMiB),
                   "real-direct-and-adopted-buffers-returned-exactly-once");
        }
        scheduler.Close();
        // Include worker cache trimming in the trace; it must not generate an owner-thread free either.
        SetPixelBufferRecycleCap(0);
        std::thread trim([]() noexcept { TrimPixelBufferRecycle(); });
        trim.join();
    }

    SessionProperties stopped{};
    stopped.properties.Wnode.BufferSize = sizeof(stopped);
    stopped.properties.Wnode.Guid = kPipelineTraceProviderGuid;
    stopped.properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    stopped.properties.LogFileMode = session.properties.LogFileMode;
    stopped.properties.LoggerNameOffset = offsetof(SessionProperties, loggerName);
    stopped.properties.LogFileNameOffset = offsetof(SessionProperties, logFile);
    const ULONG stopStatus =
        ControlTraceW(traceSession, loggerName.c_str(), &stopped.properties, EVENT_TRACE_CONTROL_STOP);
    sessionRunning = stopStatus != ERROR_SUCCESS;
    if (stopStatus != ERROR_SUCCESS)
    {
        std::printf("INFO pipeline-trace.ControlTraceW=%lu guid=%08lx mode=%08lx\n", stopStatus,
                    session.properties.Wnode.Guid.Data1, session.properties.LogFileMode);
    }
    Expect(stopStatus == ERROR_SUCCESS, "stop-and-flush-session");
    if (stopStatus != ERROR_SUCCESS)
    {
        return g_failures;
    }

    std::array<wchar_t, MAX_PATH> actualLogPath{};
    DWORD pathLength =
        GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES ? static_cast<DWORD>(logPath.size()) : 0;
    if (pathLength != 0)
    {
        (void)wcscpy_s(actualLogPath.data(), actualLogPath.size(), logPath.c_str());
    }
    else if (GetFileAttributesW(suffixedLogPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        (void)wcscpy_s(actualLogPath.data(), actualLogPath.size(), suffixedLogPath.c_str());
        pathLength = static_cast<DWORD>(wcslen(actualLogPath.data()));
    }
    Expect(pathLength != 0, "etl-file-created");
    if (pathLength == 0)
    {
        return g_failures;
    }

    EventCapture* const callbackContext = &capture;
    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LogFileName = actualLogPath.data();
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = CaptureEvent;
    logfile.Context = callbackContext;
    TRACEHANDLE consumer = OpenTraceW(&logfile);
    Expect(consumer != INVALID_PROCESSTRACE_HANDLE, "open-etl-consumer");
    if (consumer == INVALID_PROCESSTRACE_HANDLE)
    {
        return g_failures;
    }
    auto closeConsumer = wil::scope_exit([consumer]() noexcept { (void)CloseTrace(consumer); });

    const ULONG processStatus = ProcessTrace(&consumer, 1, nullptr, nullptr);
    Expect(processStatus == ERROR_SUCCESS, "consume-real-etl");
    Expect(stopped.properties.EventsLost == 0 && stopped.properties.LogBuffersLost == 0, "no-lost-trace-events");
    Expect(capture.undecodedEvents == 0, "decode-tracelogging-metadata");
    Expect(capture.uiFreeEvents == 2, "two-ui-positive-controls-and-no-large-ui-free");
    Expect(capture.uiFreeBytes == 2 * kOneMiB && capture.largeUiFrees == 0,
           "decoded-free-sizes-exclude-any-large-ui-destruction");
    Expect(capture.workerFreeEvents == 0 && capture.otherThreadFreeEvents == 0, "worker-one-mib-free-not-emitted");
    Expect(capture.uiTimingEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "decode-all-six-ui-timing-events-and-properties");
    Expect(capture.uiTimingOwnerEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "ui-timing-events-run-on-owner-thread");
    Expect(capture.uiTimingKeywordEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "ui-timing-events-use-keyword-four");
    Expect(capture.suppressedTimingEvents == std::array<std::uint32_t, 3>{0, 0, 0},
           "dispatch-message-drain-at-or-below-four-ms-suppressed");
    Expect(capture.workerTimingEvents == std::array<std::uint32_t, 6>{0, 0, 0, 0, 0, 0},
           "worker-ui-timing-events-suppressed");
    Expect(capture.uiResourceStageEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "decode-all-six-resource-stage-events-and-properties-for-ids-6-through-11");
    Expect(capture.uiResourceStageOwnerEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "resource-stage-events-run-on-owner-thread");
    Expect(capture.uiResourceStageKeywordEvents == std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
           "resource-stage-events-use-keyword-four");
    Expect(capture.workerResourceStageEvents == std::array<std::uint32_t, 6>{0, 0, 0, 0, 0, 0},
           "worker-resource-stage-events-suppressed");
    Expect(capture.suppressedChromeSyncEvents == 0, "chrome-sync-at-four-ms-suppressed");
    return g_failures;
}

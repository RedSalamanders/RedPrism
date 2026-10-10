#include "../Support/FailureReports.h"
#include "AdapterSelection.h"
#include "AddressSanitizerProbe.h"
#include "BenchmarkMachineTests.h"
#include "CanvasViewport.h"
#include "CommandBindings.h"
#include "CommandLine.h"
#include "ContinuationAdjacentPreviewTests.h"
#include "ContinuationPendingViewTests.h"
#include "ContinuationQueueTests.h"
#include "ContinuationReceiptTests.h"
#include "ContinuationRequestTests.h"
#include "CrashDumps.h"
#include "DeferredReleaseTests.h"
#include "Document.h"
#include "EditorTool.h"
#include "ExecutorTests.h"
#include "FilterWorkBenchmark.h"
#include "FrameScheduler.h"
#include "HistoryWorkTests.h"
#include "Host.Tests.CompletedPreviewMemory.h"
#include "Host.Tests.ExecutionLaneStages.h"
#include "Host.Tests.GpuRecordingLane.h"
#include "Host.Tests.PathClassificationRead.h"
#include "Host.Tests.PathInputTransfer.h"
#include "Host.Tests.PathReadWaitCycleExhaustion.h"
#include "Host.Tests.PointerInputStreamPeek.h"
#include "Host.Tests.PreparedRasterPreviewLifetime.h"
#include "Host.Tests.PreservedContinuation.h"
#include "Host.Tests.PublicationCallback.h"
#include "Host.Tests.PublicationRetention.h"
#include "Host.Tests.ReservationGrowth.h"
#include "Host.Tests.RetainedGrowth.h"
#include "Host.Tests.RetainedSlotAdmission.h"
#include "Host.Tests.RetirementMemory.h"
#include "Host.Tests.UiFrameLatency.h"
#include "Host.Tests.WarpFactoryLifetime.h"
#include "Host.Tests.WarpPreviewLifetime.h"
#include "Host.Tests.WorkerStageRetirement.h"
#include "Host.Tests.WorkerWriteCompletion.h"
#include "HostPathReadTests.h"
#include "JobActivityTests.h"
#include "JobBenchmark.h"
#include "JobCloseHookTests.h"
#include "JobMemoryTests.h"
#include "JobRetirementTests.h"
#include "JobShutdownRetirementTests.h"
#include "JobStageTests.h"
#include "JobSuspendedRetirementTests.h"
#include "JobTests.h"
#include "JobUiPolicyTests.h"
#include "KernelCliTests.h"
#include "KernelDispatchTests.h"
#include "Localization.h"
#include "MemoryAdmissionTests.h"
#include "MemoryBenchmark.h"
#include "OpacityInputOrderTests.h"
#include "OwnerWorkMultiplexerTests.h"
#include "PerfRootTests.h"
#include "PipelineTraceTests.h"
#include "PixelRecycleTests.h"
#include "PointerInputStreamTests.h"
#include "PreviewMailboxTests.h"
#include "Resources.h"
#include "RetainedJobMemoryTests.h"
#include "RetryWaitCancellationTests.h"
#include "RetryWaitQueueTests.h"
#include "SamplerInputTests.h"
#include "SamplerIntentInputTests.h"
#include "Scope.Tests.JobExecutionObservation.h"
#include "Scope.Tests.KeyboardState.h"
#include "Scope.Tests.UiCancellationBody.h"
#include "Scope.Tests.UiCancellationCapture.h"
#include "Scope.Tests.UiCancellationCli.h"
#include "Scope.Tests.UiCancellationObservation.h"
#include "Scope.Tests.UiCancellationRefusal.h"
#include "SettingsStore.h"
#include "SharedRetainedJobMemoryTests.h"
#include "TransformOverlay.h"
#include "UiTimingHistogramTests.h"
#include "UiTimingTests.h"
#include "Version.h"
#include "VersionInfo.h"
#include "WorkerPoolTests.h"

#include <array>
#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <dwrite.h>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#pragma comment(lib, "dwrite.lib")

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", name);
    std::fflush(stdout);
    ++gFailures;
}

void TestAdapterSelection() noexcept
{
    constexpr LUID first{1, 0};
    constexpr LUID second{2, 0};
    const HMONITOR monitorA = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x100));
    const HMONITOR monitorB = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x200));
    const std::array<AdapterOutputRecord, 3> records{
        AdapterOutputRecord{first, monitorA, true},
        AdapterOutputRecord{second, monitorA, false},
        AdapterOutputRecord{first, monitorB, false},
    };

    Expect(SelectAdapterRecordForMonitor(records, nullptr) == SIZE_MAX, "adapter.null-monitor");
    Expect(SelectAdapterRecordForMonitor(records, monitorA) == 1, "adapter.skip-software");
    Expect(SelectAdapterRecordForMonitor(records, monitorB) == 2, "adapter.other-monitor");
    Expect(SelectAdapterRecordForMonitor(records, reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x300))) ==
               SIZE_MAX,
           "adapter.unknown-monitor");
    Expect(SameLuid(first, first) && !SameLuid(first, second), "adapter.luid");
}

void TestFrameScheduler() noexcept
{
    HostFrameState state{};
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.hidden-suspended");

    state.windowVisible = true;
    state.displayPoweredOn = true;
    state.rendererSuspended = false;
    state.frameInvalidated = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::Render, "frame.dirty");

    state.frameInvalidated = false;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.idle");

    state.rendererOccluded = true;
    state.occlusionStatusChanged = false;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.occluded-wait");

    state.occlusionStatusChanged = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::ProbeOcclusion, "frame.occluded-probe");

    state.rendererOccluded = false;
    state.occlusionStatusChanged = false;
    state.frameInvalidated = false;
    state.chromeAnimating = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::Render, "frame.chrome-animating");

    state.rendererSuspended = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.suspended");

    state.rendererSuspended = false;
    state.displayPoweredOn = false;
    state.frameInvalidated = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.display-off");

    struct CompletionPriorityCase final
    {
        HostFrameState state;
        HostFrameAction whenClear;
        const char* pendingName;
        const char* clearedName;
    };
    constexpr std::array completionPriorityCases{
        CompletionPriorityCase{{false, true, false, false, false, true},
                               HostFrameAction::WaitForMessage,
                               "frame.completions-hidden",
                               "frame.completions-hidden-cleared"},
        CompletionPriorityCase{{true, true, true, false, false, true},
                               HostFrameAction::WaitForMessage,
                               "frame.completions-suspended",
                               "frame.completions-suspended-cleared"},
        CompletionPriorityCase{{true, true, false, true, true, false},
                               HostFrameAction::ProbeOcclusion,
                               "frame.completions-occluded",
                               "frame.completions-occluded-cleared"},
        CompletionPriorityCase{{true, false, false, false, false, true},
                               HostFrameAction::WaitForMessage,
                               "frame.completions-display-off",
                               "frame.completions-display-off-cleared"},
        CompletionPriorityCase{{true, true, false, false, false, false},
                               HostFrameAction::WaitForMessage,
                               "frame.completions-idle",
                               "frame.completions-idle-cleared"},
        CompletionPriorityCase{{true, true, false, false, false, true},
                               HostFrameAction::Render,
                               "frame.completions-render",
                               "frame.completions-render-cleared"},
    };
    for (const CompletionPriorityCase& testCase : completionPriorityCases)
    {
        HostFrameState completionState = testCase.state;
        completionState.completionsPending = true;
        Expect(SelectHostFrameAction(completionState) == HostFrameAction::DrainCompletions, testCase.pendingName);

        completionState.completionsPending = false;
        Expect(SelectHostFrameAction(completionState) == testCase.whenClear, testCase.clearedName);
    }
}

void TestCommandLine() noexcept
{
    Expect(RedPrismIsHelpArgument(L"--help") && RedPrismIsHelpArgument(L"/?"), "cli.help-aliases");
    Expect(RedPrismFindSwitch(L"--warp") != nullptr, "cli.warp");
    Expect(RedPrismFindSwitch(L"--self-test") != nullptr, "cli.self-test");
    Expect(RedPrismFindSwitch(L"--screenshot") != nullptr, "cli.screenshot");
    Expect(RedPrismFindSwitch(L"--dpi") != nullptr, "cli.dpi");
    Expect(RedPrismFindSwitch(L"--demo") != nullptr, "cli.demo");
    Expect(RedPrismFindSwitch(L"--crash-probe-worker") != nullptr, "cli.crash-probe-worker");
    Expect(RedPrismFindSwitch(L"--no-such") == nullptr, "cli.unknown-switch");

    wchar_t scanMarker[] = L"--";
    wchar_t scanHelp[] = L"--help";
    wchar_t scanSelfTest[] = L"--self-test";
    wchar_t* literalSwitchArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), scanMarker, scanHelp, scanSelfTest};
    const RedPrismCommandLineScan literalSwitchScan = RedPrismScanCommandLine(literalSwitchArgs, 4);
    Expect(!literalSwitchScan.help && !literalSwitchScan.selfTest && !literalSwitchScan.harnessMode,
           "cli.preflight-switches-after-marker-are-positional");
    RedPrismCommandLine literalSwitchParsed{};
    const wchar_t* literalSwitchError = nullptr;
    Expect(RedPrismParseCommandLine(literalSwitchArgs, 4, literalSwitchParsed, &literalSwitchError) &&
               literalSwitchParsed.inputPathCount == 2 &&
               literalSwitchParsed.inputPaths[0] == std::wstring_view{scanHelp} &&
               literalSwitchParsed.inputPaths[1] == std::wstring_view{scanSelfTest},
           "cli.help-and-self-test-after-marker-remain-positional");

    wchar_t scanBench[] = L"--bench";
    wchar_t* benchHelpValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), scanBench, scanHelp};
    const RedPrismCommandLineScan benchHelpValueScan = RedPrismScanCommandLine(benchHelpValueArgs, 3);
    Expect(!benchHelpValueScan.help && !benchHelpValueScan.selfTest && benchHelpValueScan.harnessMode,
           "cli.preflight-switch-value-is-not-help");

    wchar_t scanDemo[] = L"--demo";
    wchar_t* demoSelfTestValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), scanDemo, scanSelfTest};
    const RedPrismCommandLineScan demoSelfTestValueScan = RedPrismScanCommandLine(demoSelfTestValueArgs, 3);
    Expect(!demoSelfTestValueScan.help && !demoSelfTestValueScan.selfTest && demoSelfTestValueScan.harnessMode,
           "cli.preflight-switch-value-is-not-self-test");

    wchar_t unknown[] = L"--no-such";
    wchar_t* args[] = {const_cast<wchar_t*>(L"RedPrism.exe"), unknown};
    Expect(std::wstring_view{RedPrismFindUnknownArgument(args, 2)} == L"--no-such", "cli.unknown-scan");

    wchar_t screenshot[] = L"--screenshot";
    wchar_t png[] = L"overlay.png";
    wchar_t* shotArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png};
    Expect(RedPrismFindUnknownArgument(shotArgs, 3) == nullptr, "cli.screenshot-value");

    RedPrismCommandLine parsed{};
    const wchar_t* error = nullptr;
    Expect(RedPrismParseCommandLine(shotArgs, 3, parsed, &error) && parsed.screenshotPath != nullptr &&
               parsed.screenshotPath == std::wstring_view{png},
           "cli.screenshot-parse");

    struct PreparedBenchCase final
    {
        const wchar_t* token;
        const char* name;
    };
    constexpr std::array preparedBenchCases{
        PreparedBenchCase{L"ui-prepared", "cli.prepared-bench-ui-prepared"},
        PreparedBenchCase{L"ui-prepared45", "cli.prepared-bench-ui-prepared45"},
        PreparedBenchCase{L"ui-prepared100", "cli.prepared-bench-ui-prepared100"},
        PreparedBenchCase{L"ui-prepared-smoke", "cli.prepared-bench-ui-prepared-smoke"},
    };
    wchar_t preparedBenchSwitch[] = L"--bench";
    wchar_t preparedWarpSwitch[] = L"--warp";
    for (const PreparedBenchCase& testCase : preparedBenchCases)
    {
        wchar_t* preparedArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), preparedBenchSwitch,
                                   const_cast<wchar_t*>(testCase.token)};
        parsed = {};
        const bool withoutWarp = RedPrismParseCommandLine(preparedArgs, 3, parsed, &error) && !parsed.warp &&
                                 parsed.bench && std::wstring_view{parsed.bench} == std::wstring_view{testCase.token} &&
                                 parsed.inputPathCount == 0;
        wchar_t* preparedWarpArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), preparedBenchSwitch,
                                       const_cast<wchar_t*>(testCase.token), preparedWarpSwitch};
        parsed = {};
        const bool withWarp = RedPrismParseCommandLine(preparedWarpArgs, 4, parsed, &error) && parsed.warp &&
                              parsed.bench && std::wstring_view{parsed.bench} == std::wstring_view{testCase.token} &&
                              parsed.inputPathCount == 0;
        Expect(withoutWarp && withWarp, testCase.name);
    }

    wchar_t malformedPreparedValue[] = L"ui-prepared999";
    wchar_t* malformedPreparedArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), preparedBenchSwitch,
                                        malformedPreparedValue};
    parsed = {};
    parsed.warp = true;
    parsed.bench = L"sentinel";
    parsed.inputPathCount = 1;
    parsed.inputPaths[0] = L"sentinel-path";
    Expect(!RedPrismParseCommandLine(malformedPreparedArgs, 3, parsed, &error) && error == malformedPreparedValue &&
               parsed.warp && parsed.bench == std::wstring_view{L"sentinel"} && parsed.inputPathCount == 1 &&
               parsed.inputPaths[0] == std::wstring_view{L"sentinel-path"},
           "cli.prepared-bench-malformed-atomic-rejection");

    wchar_t preparedInputPath[] = L"input.prism";
    wchar_t* preparedWithInputArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), preparedBenchSwitch,
                                        const_cast<wchar_t*>(L"ui-prepared"), preparedInputPath};
    Expect(!RedPrismParseCommandLine(preparedWithInputArgs, 4, parsed, &error) && error == preparedInputPath,
           "cli.prepared-bench-refuses-positional-input");

    wchar_t* missingScreenshotValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot};
    Expect(!RedPrismParseCommandLine(missingScreenshotValueArgs, 2, parsed, &error) && error == screenshot,
           "cli.screenshot-missing-value");

    wchar_t firstPath[] = L"C:\\art\\雪.png";
    wchar_t secondPath[] = L"D:\\素材\\é.png";
    wchar_t warpSwitch[] = L"--warp";
    wchar_t isaSwitch[] = L"--isa";
    wchar_t isaValue[] = L"scalar";
    wchar_t* positionalArgs[] = {
        const_cast<wchar_t*>(L"RedPrism.exe"), firstPath, warpSwitch, isaSwitch, isaValue, secondPath};
    parsed = {};
    Expect(RedPrismFindUnknownArgument(positionalArgs, 6) == nullptr &&
               RedPrismParseCommandLine(positionalArgs, 6, parsed, &error) && parsed.warp &&
               parsed.isa == KernelIsa::Scalar && parsed.inputPathCount == 2 &&
               parsed.inputPaths[0] == std::wstring_view{firstPath} &&
               parsed.inputPaths[1] == std::wstring_view{secondPath},
           "cli.positional-unicode-order-and-known-value");
    firstPath[0] = L'X';
    Expect(parsed.inputPaths[0].data() == firstPath && parsed.inputPaths[0][0] == L'X',
           "cli.positional-borrowed-lifetime");

    wchar_t endMarker[] = L"--";
    wchar_t dashPath[] = L"-literal-name.png";
    wchar_t* markedArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), warpSwitch, endMarker, dashPath};
    parsed = {};
    Expect(RedPrismFindUnknownArgument(markedArgs, 4) == nullptr &&
               RedPrismParseCommandLine(markedArgs, 4, parsed, &error) && parsed.warp && parsed.inputPathCount == 1 &&
               parsed.inputPaths[0] == std::wstring_view{dashPath},
           "cli.positional-end-marker");
    wchar_t* bareDashPath[] = {const_cast<wchar_t*>(L"RedPrism.exe"), dashPath};
    Expect(RedPrismFindUnknownArgument(bareDashPath, 2) == dashPath &&
               !RedPrismParseCommandLine(bareDashPath, 2, parsed, &error) && error == dashPath,
           "cli.positional-leading-dash-requires-marker");

    wchar_t emptyPath[] = L"";
    wchar_t* emptyPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), emptyPath};
    Expect(RedPrismFindUnknownArgument(emptyPathArgs, 2) == emptyPath &&
               !RedPrismParseCommandLine(emptyPathArgs, 2, parsed, &error) && error == emptyPath,
           "cli.positional-empty-rejected");

    wchar_t* nullPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), nullptr};
    Expect(RedPrismFindUnknownArgument(nullPathArgs, 2) != nullptr &&
               !RedPrismParseCommandLine(nullPathArgs, 2, parsed, &error) && error != nullptr,
           "cli.positional-null-rejected");

    std::array<wchar_t, RedPrismCommandLine::kMaximumPathLength + 2> pathBoundary{};
    pathBoundary.fill(L'a');
    pathBoundary[RedPrismCommandLine::kMaximumPathLength] = L'\0';
    wchar_t* maxPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), pathBoundary.data()};
    parsed = {};
    Expect(RedPrismParseCommandLine(maxPathArgs, 2, parsed, &error) && parsed.inputPathCount == 1 &&
               parsed.inputPaths[0].size() == RedPrismCommandLine::kMaximumPathLength,
           "cli.positional-maximum-length-accepted");
    pathBoundary[RedPrismCommandLine::kMaximumPathLength] = L'b';
    pathBoundary[RedPrismCommandLine::kMaximumPathLength + 1] = L'\0';
    Expect(RedPrismFindSwitch(pathBoundary.data()) == nullptr, "cli.overlong-switch-rejected-with-bounded-scan");
    parsed.warp = true;
    Expect(RedPrismFindUnknownArgument(maxPathArgs, 2) == pathBoundary.data() &&
               !RedPrismParseCommandLine(maxPathArgs, 2, parsed, &error) && error == pathBoundary.data() &&
               parsed.warp && parsed.inputPathCount == 1 &&
               parsed.inputPaths[0].size() == RedPrismCommandLine::kMaximumPathLength,
           "cli.positional-overlong-rejected-before-output-mutation");

    wchar_t* overlongScreenshotValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, pathBoundary.data()};
    Expect(RedPrismFindUnknownArgument(overlongScreenshotValueArgs, 3) == pathBoundary.data() &&
               !RedPrismParseCommandLine(overlongScreenshotValueArgs, 3, parsed, &error) &&
               error == pathBoundary.data() && parsed.warp,
           "cli.overlong-switch-value-rejected-before-view-construction");

    wchar_t* nullScreenshotValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, nullptr};
    Expect(RedPrismFindUnknownArgument(nullScreenshotValueArgs, 3) != nullptr &&
               !RedPrismParseCommandLine(nullScreenshotValueArgs, 3, parsed, &error) && error != nullptr && parsed.warp,
           "cli.null-switch-value-rejected");

    std::array<std::array<wchar_t, 2>, RedPrismCommandLine::kMaximumInputPathCount + 1> pathTokens{};
    std::array<wchar_t*, RedPrismCommandLine::kMaximumInputPathCount + 2> manyPathArgs{};
    manyPathArgs[0] = const_cast<wchar_t*>(L"RedPrism.exe");
    for (size_t index = 0; index < pathTokens.size(); ++index)
    {
        pathTokens[index][0] = L'p';
        manyPathArgs[index + 1] = pathTokens[index].data();
    }
    parsed = {};
    Expect(RedPrismParseCommandLine(manyPathArgs.data(),
                                    static_cast<int>(RedPrismCommandLine::kMaximumInputPathCount + 1), parsed,
                                    &error) &&
               parsed.inputPathCount == RedPrismCommandLine::kMaximumInputPathCount,
           "cli.positional-maximum-count-accepted");
    parsed.warp = true;
    Expect(RedPrismFindUnknownArgument(manyPathArgs.data(), static_cast<int>(manyPathArgs.size())) ==
                   pathTokens.back().data() &&
               !RedPrismParseCommandLine(manyPathArgs.data(), static_cast<int>(manyPathArgs.size()), parsed, &error) &&
               error == pathTokens.back().data() && parsed.warp &&
               parsed.inputPathCount == RedPrismCommandLine::kMaximumInputPathCount,
           "cli.positional-excess-count-rejected-before-output-mutation");

    wchar_t selfTestSwitch[] = L"--self-test";
    wchar_t* harnessPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), firstPath, selfTestSwitch};
    Expect(RedPrismFindUnknownArgument(harnessPathArgs, 3) == firstPath &&
               !RedPrismParseCommandLine(harnessPathArgs, 3, parsed, &error) && error == firstPath,
           "cli.positional-rejected-in-harness-mode");
    wchar_t* screenshotPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png, firstPath};
    Expect(RedPrismFindUnknownArgument(screenshotPathArgs, 4) == firstPath &&
               !RedPrismParseCommandLine(screenshotPathArgs, 4, parsed, &error) && error == firstPath,
           "cli.positional-rejected-in-screenshot-mode");
    wchar_t* benchPathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), const_cast<wchar_t*>(L"--bench"),
                                const_cast<wchar_t*>(L"brush"), firstPath};
    Expect(RedPrismFindUnknownArgument(benchPathArgs, 4) == firstPath &&
               !RedPrismParseCommandLine(benchPathArgs, 4, parsed, &error) && error == firstPath,
           "cli.positional-rejected-in-benchmark-mode");
    wchar_t* crashProbePathArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), const_cast<wchar_t*>(L"--crash-probe"),
                                     const_cast<wchar_t*>(L"C:\\dumps"), firstPath};
    Expect(RedPrismFindUnknownArgument(crashProbePathArgs, 4) == firstPath &&
               !RedPrismParseCommandLine(crashProbePathArgs, 4, parsed, &error) && error == firstPath,
           "cli.positional-rejected-in-crash-probe-mode");
    wchar_t* warpIsaPaths[] = {const_cast<wchar_t*>(L"RedPrism.exe"), firstPath, warpSwitch, isaSwitch, isaValue};
    parsed = {};
    Expect(RedPrismFindUnknownArgument(warpIsaPaths, 5) == nullptr &&
               RedPrismParseCommandLine(warpIsaPaths, 5, parsed, &error) && parsed.inputPathCount == 1,
           "cli.positional-allowed-with-warp-isa");

    wchar_t resetPath[] = L"reset.png";
    wchar_t* resetArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), resetPath};
    Expect(RedPrismParseCommandLine(resetArgs, 2, parsed, &error) && !parsed.warp && parsed.isa == KernelIsa::Auto &&
               parsed.inputPathCount == 1 && parsed.inputPaths[0] == std::wstring_view{resetPath},
           "cli.repeated-parse-resets-state");

    parsed.warp = true;
    parsed.screenshotPath = png;
    parsed.inputPaths[0] = std::wstring_view{firstPath};
    parsed.inputPathCount = 1;
    wchar_t* missingDemoValueArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), scanDemo};
    Expect(!RedPrismParseCommandLine(missingDemoValueArgs, 2, parsed, &error) && error == scanDemo && parsed.warp &&
               parsed.screenshotPath == png && parsed.inputPathCount == 1 &&
               parsed.inputPaths[0] == std::wstring_view{firstPath},
           "cli.failure-keeps-previous-output-atomic");

    wchar_t emptyScreenshotPath[] = L"";
    wchar_t* emptyScreenshotArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, emptyScreenshotPath};
    Expect(!RedPrismParseCommandLine(emptyScreenshotArgs, 3, parsed, &error) && error == screenshot,
           "cli.screenshot-empty-value-rejected");

    wchar_t dpiSwitch[] = L"--dpi";
    wchar_t dpiValue[] = L"144";
    wchar_t* dpiArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png, dpiSwitch, dpiValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(dpiArgs, 5, parsed, &error) && parsed.screenshotDpi == 144, "cli.dpi-parse");

    wchar_t badDpi[] = L"120";
    wchar_t* badArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), dpiSwitch, badDpi};
    parsed = {};
    Expect(!RedPrismParseCommandLine(badArgs, 3, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"120",
           "cli.dpi-rejects-other-values");

    wchar_t demoSwitch[] = L"--demo";
    wchar_t demoValue[] = L"transform";
    wchar_t* demoArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png, demoSwitch, demoValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(demoArgs, 5, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{demoValue},
           "cli.demo-parse");
    wchar_t badDemo[] = L"sketch";
    wchar_t* badDemoArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, badDemo};
    parsed = {};
    Expect(!RedPrismParseCommandLine(badDemoArgs, 3, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"sketch",
           "cli.demo-rejects-unknown");
    wchar_t probeSwitch[] = L"--crash-probe";
    wchar_t probeValue[] = L"C:\\dumps";
    wchar_t* probeArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), probeSwitch, probeValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(probeArgs, 3, parsed, &error) && parsed.crashProbeDirectory != nullptr &&
               parsed.crashProbeDirectory == std::wstring_view{probeValue} && !parsed.crashProbeWorker &&
               RedPrismFindUnknownArgument(probeArgs, 3) == nullptr,
           "cli.crash-probe-parse");
    wchar_t* probeNoValue[] = {const_cast<wchar_t*>(L"RedPrism.exe"), probeSwitch};
    parsed = {};
    Expect(!RedPrismParseCommandLine(probeNoValue, 2, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"--crash-probe",
           "cli.crash-probe-needs-a-directory");
    wchar_t probeWorkerSwitch[] = L"--crash-probe-worker";
    wchar_t* probeWorkerBeforeArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), probeWorkerSwitch, probeSwitch,
                                        probeValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(probeWorkerBeforeArgs, 4, parsed, &error) && parsed.crashProbeWorker &&
               parsed.crashProbeDirectory != nullptr && parsed.crashProbeDirectory == std::wstring_view{probeValue},
           "cli.crash-probe-worker-before-probe");
    wchar_t* probeWorkerAfterArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), probeSwitch, probeValue,
                                       probeWorkerSwitch};
    parsed = {};
    Expect(RedPrismParseCommandLine(probeWorkerAfterArgs, 4, parsed, &error) && parsed.crashProbeWorker &&
               parsed.crashProbeDirectory != nullptr && parsed.crashProbeDirectory == std::wstring_view{probeValue},
           "cli.crash-probe-worker-after-probe");
    wchar_t* probeWorkerNoProbe[] = {const_cast<wchar_t*>(L"RedPrism.exe"), probeWorkerSwitch};
    parsed = {};
    Expect(!RedPrismParseCommandLine(probeWorkerNoProbe, 2, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"--crash-probe-worker",
           "cli.crash-probe-worker-needs-probe");
    wchar_t selectionValue[] = L"selection";
    wchar_t* selectionArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, selectionValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(selectionArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{selectionValue},
           "cli.demo-selection");
    wchar_t brushValue[] = L"brush";
    wchar_t* brushArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, brushValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(brushArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{brushValue},
           "cli.demo-brush");
    wchar_t adjustValue[] = L"adjust";
    wchar_t* adjustArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, adjustValue};
    Expect(RedPrismParseCommandLine(adjustArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{adjustValue},
           "cli.demo-adjust");
    wchar_t retouchValue[] = L"retouch";
    wchar_t* retouchArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, retouchValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(retouchArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{retouchValue},
           "cli.demo-retouch");
    wchar_t cropValue[] = L"crop";
    wchar_t* cropArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, cropValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(cropArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{cropValue},
           "cli.demo-crop");
    wchar_t canvasSizeValue[] = L"canvassize";
    wchar_t* canvasSizeArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, canvasSizeValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(canvasSizeArgs, 3, parsed, &error) && parsed.demo != nullptr &&
               parsed.demo == std::wstring_view{canvasSizeValue},
           "cli.demo-canvassize");
    wchar_t benchSwitch[] = L"--bench";
    wchar_t* benchArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, brushValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(benchArgs, 3, parsed, &error) && parsed.bench != nullptr &&
               parsed.bench == std::wstring_view{brushValue},
           "cli.bench-brush");
    wchar_t* badBenchArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, badDemo};
    parsed = {};
    Expect(!RedPrismParseCommandLine(badBenchArgs, 3, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"sketch",
           "cli.bench-rejects-unknown");
    wchar_t kernelsValue[] = L"kernels";
    wchar_t* kernelsArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, kernelsValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(kernelsArgs, 3, parsed, &error) && parsed.bench != nullptr &&
               parsed.bench == std::wstring_view{kernelsValue},
           "cli.bench-kernels");
    wchar_t kernels45Value[] = L"kernels45";
    wchar_t* kernels45Args[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, kernels45Value};
    parsed = {};
    Expect(RedPrismParseCommandLine(kernels45Args, 3, parsed, &error) && parsed.bench != nullptr &&
               parsed.bench == std::wstring_view{kernels45Value},
           "cli.bench-kernels45");
    wchar_t kernelsSmokeValue[] = L"kernels-smoke";
    wchar_t* kernelsSmokeArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, kernelsSmokeValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(kernelsSmokeArgs, 3, parsed, &error) && parsed.bench != nullptr &&
               parsed.bench == std::wstring_view{kernelsSmokeValue},
           "cli.bench-kernels-smoke");
    for (const wchar_t* name : {L"buffers", L"buffers45"})
    {
        wchar_t* bufferArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch, const_cast<wchar_t*>(name)};
        parsed = {};
        Expect(RedPrismParseCommandLine(bufferArgs, 3, parsed, &error) && parsed.bench != nullptr &&
                   parsed.bench == std::wstring_view{name},
               "cli.bench-buffers");
    }
    wchar_t* missingBenchValue[] = {const_cast<wchar_t*>(L"RedPrism.exe"), benchSwitch};
    parsed = {};
    Expect(!RedPrismParseCommandLine(missingBenchValue, 2, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"--bench",
           "cli.bench-needs-value");

    const std::wstring help = RedPrismFormatCommandLineHelp();
    Expect(help.find(L"--warp") != std::wstring::npos && help.find(L"--self-test") != std::wstring::npos &&
               help.find(L"--screenshot") != std::wstring::npos && help.find(L"--dpi") != std::wstring::npos &&
               help.find(L"--crash-probe-worker") != std::wstring::npos,
           "cli.help-text");
}

// Separate runtime metadata queries retain their normal guarded switch entry points; do not duplicate four
// hoisted switch dispatches inside the container-heavy test. Every existing numeric/control expectation stays.
__declspec(noinline) wchar_t RuntimeEditorToolKey(EditorTool tool) noexcept
{
    return EditorToolKey(tool);
}
__declspec(noinline) wchar_t RuntimeEditorToolGlyph(EditorTool tool) noexcept
{
    return EditorToolGlyph(tool);
}
__declspec(noinline) UINT RuntimeEditorToolName(EditorTool tool) noexcept
{
    return EditorToolNameId(tool);
}
__declspec(noinline) EditorTool RuntimeEditorToolForCommand(UINT command) noexcept
{
    return CommandIds::ToolForSelectCommand(command);
}

void TestEditorTools() noexcept
{
    std::set<wchar_t> keys;
    std::set<wchar_t> glyphs;
    std::set<UINT> names;
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        const EditorTool tool = EditorToolAt(index);
        keys.insert(RuntimeEditorToolKey(tool));
        glyphs.insert(RuntimeEditorToolGlyph(tool));
        names.insert(RuntimeEditorToolName(tool));
        // A tool's key is the default chord of its select command, and that command selects the tool.
        Expect(PrimaryDefaultChord(CommandIds::SelectToolCommand(tool)) ==
                       Key(static_cast<std::uint16_t>(RuntimeEditorToolKey(tool))) &&
                   RuntimeEditorToolForCommand(CommandIds::SelectToolCommand(tool)) == tool,
               "tools.key-is-the-select-command-default");
        Expect(EditorToolIndex(tool) == index, "tools.index-round-trip");
    }
    Expect(keys.size() == kEditorToolCount, "tools.unique-keys");
    Expect(glyphs.size() == kEditorToolCount, "tools.unique-glyphs");
    Expect(names.size() == kEditorToolCount, "tools.unique-names");
    Expect(CommandIds::ToolForSelectCommand(CommandIds::kCommandSelectToolBase - 1) == EditorTool::Count &&
               CommandIds::ToolForSelectCommand(CommandIds::SelectToolCommand(EditorTool::Count)) == EditorTool::Count,
           "tools.select-command-range");
    Expect(EditorToolAt(999) == EditorTool::Move, "tools.index-clamp");
}

// Every rail glyph must exist in the installed Segoe Fluent Icons font; a missing glyph renders as a box.
void TestRailGlyphsExistInFluentFont() noexcept
{
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.put()));
    if (FAILED(result))
    {
        Expect(false, "glyphs.dwrite-factory");
        return;
    }
    wil::com_ptr_nothrow<IDWriteFontCollection> fonts;
    result = factory->GetSystemFontCollection(fonts.put(), FALSE);
    if (FAILED(result))
    {
        Expect(false, "glyphs.system-fonts");
        return;
    }
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    result = fonts->FindFamilyName(L"Segoe Fluent Icons", &familyIndex, &exists);
    if (FAILED(result) || !exists)
    {
        std::printf("SKIP glyphs.fluent-family (Segoe Fluent Icons is not installed on this host)\n");
        return;
    }
    wil::com_ptr_nothrow<IDWriteFontFamily> family;
    wil::com_ptr_nothrow<IDWriteFont> font;
    if (FAILED(fonts->GetFontFamily(familyIndex, family.put())) || FAILED(family->GetFont(0, font.put())))
    {
        Expect(false, "glyphs.fluent-font");
        return;
    }
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        BOOL has = FALSE;
        const wchar_t glyph = EditorToolGlyph(EditorToolAt(index));
        const bool ok = SUCCEEDED(font->HasCharacter(static_cast<UINT32>(glyph), &has)) && has;
        if (!ok)
        {
            std::printf("     missing glyph U+%04X for tool %zu\n", static_cast<unsigned>(glyph), index);
        }
        Expect(ok, "glyphs.fluent-has-tool-glyph");
    }
}

[[nodiscard]] bool PlaceholdersArePositional(std::wstring_view text) noexcept
{
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != L'{')
        {
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == L'{')
        {
            ++index;
            continue;
        }
        if (index + 1 >= text.size() || text[index + 1] < L'0' || text[index + 1] > L'9')
        {
            return false;
        }
    }
    return true;
}

// The STRINGTABLE in RedPrism.exe (next to this test) must define every id the chrome uses, with positional
// placeholders only. Loading the executable as a data file never runs its code.
void TestStringTable() noexcept
{
    wchar_t module[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0)
    {
        Expect(false, "strings.module-path");
        return;
    }
    std::wstring path(module);
    const std::size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1)) + L"RedPrism.exe";
    wil::unique_hmodule exe{
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    if (!exe)
    {
        Expect(false, "strings.load-redprism-exe");
        return;
    }
    constexpr std::array<UINT, 491> kIds{IDS_APP_TITLE,
                                         IDS_UNTITLED,
                                         IDS_DIRTY_MARK,
                                         IDS_STATUS_ZOOM,
                                         IDS_STATUS_SIZE,
                                         IDS_STATUS_TOOL,
                                         IDS_STATUS_NO_DOCUMENT,
                                         IDS_LAYERS_TITLE,
                                         IDS_LAYER_HIDDEN_SUFFIX,

                                         IDS_LAYER_OPACITY,

                                         IDS_PALETTE_FOREGROUND,

                                         IDS_PALETTE_BACKGROUND,

                                         IDS_PICKER_NEW,

                                         IDS_PICKER_CURRENT,

                                         IDS_PICKER_HEX,

                                         IDS_PICKER_RED,

                                         IDS_PICKER_GREEN,

                                         IDS_PICKER_BLUE,

                                         IDS_PICKER_OK,

                                         IDS_PICKER_CANCEL,
                                         IDS_MENU_FILE,
                                         IDS_MENU_EDIT,
                                         IDS_MENU_IMAGE,
                                         IDS_MENU_LAYER,
                                         IDS_MENU_SELECT,
                                         IDS_MENU_FILTER,
                                         IDS_MENU_VIEW,
                                         IDS_MENU_HELP,
                                         IDS_CMD_NEW,
                                         IDS_CMD_OPEN,
                                         IDS_CMD_SAVE,
                                         IDS_CMD_SAVE_AS,
                                         IDS_CMD_EXPORT_PNG,
                                         IDS_CMD_EXPORT_JPEG,
                                         IDS_CMD_UNDO,
                                         IDS_CMD_REDO,
                                         IDS_CMD_NEW_LAYER,
                                         IDS_CMD_NEW_FOLDER,
                                         IDS_CMD_DELETE_LAYER,
                                         IDS_CMD_TOGGLE_LAYER_VISIBILITY,
                                         IDS_CMD_MOVE_OUT_OF_GROUP,
                                         IDS_CMD_ZOOM_FIT,
                                         IDS_CMD_ZOOM_ACTUAL,
                                         IDS_CMD_ZOOM_IN,
                                         IDS_CMD_ZOOM_OUT,
                                         IDS_CMD_ABOUT,
                                         IDS_CMD_EXIT,
                                         IDS_CMD_UNDO_NAMED,
                                         IDS_CMD_REDO_NAMED,
                                         IDS_CMD_FREE_TRANSFORM,
                                         IDS_CMD_FLIP_LAYER_HORIZONTAL,
                                         IDS_CMD_FLIP_LAYER_VERTICAL,
                                         IDS_CMD_FLIP_CANVAS_HORIZONTAL,
                                         IDS_CMD_FLIP_CANVAS_VERTICAL,
                                         IDS_TOOL_MOVE,
                                         IDS_TOOL_MARQUEE,
                                         IDS_TOOL_LASSO,
                                         IDS_TOOL_WAND,
                                         IDS_TOOL_CROP,
                                         IDS_TOOL_BRUSH,
                                         IDS_TOOL_ERASER,
                                         IDS_TOOL_HEAL,
                                         IDS_TOOL_CLONE,
                                         IDS_TOOL_SMEAR,
                                         IDS_TOOL_GRADIENT,
                                         IDS_TOOL_SHAPE,
                                         IDS_TOOL_EYEDROPPER,
                                         IDS_TOOL_HAND,
                                         IDS_TOOL_ZOOM,
                                         IDS_TOOL_IDLE,
                                         IDS_TOOL_TOOLTIP,
                                         IDS_ERROR_OPEN_PROJECT,
                                         IDS_ERROR_IMPORT_IMAGE,
                                         IDS_ERROR_SAVE_PROJECT,
                                         IDS_ERROR_FLATTEN,
                                         IDS_ERROR_READBACK,
                                         IDS_ERROR_EXPORT_JPEG,
                                         IDS_ERROR_EXPORT_PNG,
                                         IDS_ERROR_START_GENERIC,
                                         IDS_ERROR_START_WINDOW,
                                         IDS_ERROR_START_GRAPHICS,
                                         IDS_ERROR_UNSUPPORTED_VERSION,
                                         IDS_ERROR_TOO_LARGE,
                                         IDS_ERROR_MISSING_IMAGE,
                                         IDS_FILTER_PROJECT,
                                         IDS_FILTER_IMAGES,
                                         IDS_FILTER_ALL,
                                         IDS_FILTER_EXPLODED,
                                         IDS_FILTER_JPEG,
                                         IDS_FILTER_PNG,
                                         IDS_DEFAULT_PROJECT_NAME,
                                         IDS_DEFAULT_EXPORT_JPEG,
                                         IDS_DEFAULT_EXPORT_PNG,
                                         IDS_ABOUT_TEXT,
                                         IDS_COMMAND_LINE_CAPTION,

                                         IDS_UNIT_PERCENT,
                                         IDS_UNIT_DEGREES,
                                         IDS_TRANSFORM_TITLE,
                                         IDS_TRANSFORM_MASK_TITLE,
                                         IDS_TRANSFORM_AUTO_SELECT,
                                         IDS_TRANSFORM_AUTO_SELECT_TIP,
                                         IDS_TRANSFORM_SHOW_CONTROLS,
                                         IDS_TRANSFORM_SHOW_CONTROLS_TIP,
                                         IDS_TRANSFORM_X,
                                         IDS_TRANSFORM_Y,
                                         IDS_TRANSFORM_WIDTH,
                                         IDS_TRANSFORM_HEIGHT,
                                         IDS_TRANSFORM_LOCK_RATIO,
                                         IDS_TRANSFORM_SCALE,
                                         IDS_TRANSFORM_SCALE_TIP,
                                         IDS_TRANSFORM_ANGLE,
                                         IDS_TRANSFORM_SAMPLING_NEAREST,
                                         IDS_TRANSFORM_SAMPLING_SMOOTH,
                                         IDS_TRANSFORM_SAMPLING_HIGH,
                                         IDS_TRANSFORM_FLIP_HORIZONTAL,
                                         IDS_TRANSFORM_FLIP_VERTICAL,
                                         IDS_TRANSFORM_APPLY,
                                         IDS_TRANSFORM_CANCEL,
                                         IDS_TRANSFORM_LOCK,
                                         IDS_CMD_SELECT_ALL,
                                         IDS_CMD_DESELECT,
                                         IDS_CMD_INVERSE,
                                         IDS_CMD_LOAD_LAYER_SELECTION,
                                         IDS_CMD_LOAD_MASK_SELECTION,
                                         IDS_ERROR_WAND_DETAILED,
                                         IDS_ERROR_LAYER_ROWS,
                                         IDS_CMD_RETRY_LAYER_LIST,
                                         IDS_ERROR_WAND_MEMORY,
                                         IDS_UNIT_PIXELS,
                                         IDS_SELECTION_RECTANGLE,
                                         IDS_SELECTION_ELLIPSE,
                                         IDS_SELECTION_FREEHAND,
                                         IDS_SELECTION_POLYGONAL,
                                         IDS_SELECTION_MODE_NEW,
                                         IDS_SELECTION_MODE_ADD,
                                         IDS_SELECTION_MODE_SUBTRACT,
                                         IDS_SELECTION_SHAPE_TIP,
                                         IDS_SELECTION_LASSO_TIP,
                                         IDS_SELECTION_MODE_TIP,
                                         IDS_SELECTION_ANTIALIAS,
                                         IDS_SELECTION_ANTIALIAS_TIP,
                                         IDS_SELECTION_EXPAND,
                                         IDS_SELECTION_CONTRACT,
                                         IDS_SELECTION_MODIFY_TIP,
                                         IDS_SELECTION_DESELECT,
                                         IDS_SELECTION_EMPTY,
                                         IDS_WAND_TOLERANCE,
                                         IDS_WAND_TOLERANCE_TIP,
                                         IDS_WAND_POINT_SAMPLE,
                                         IDS_WAND_THREE_BY_THREE,
                                         IDS_WAND_FIVE_BY_FIVE,
                                         IDS_WAND_SAMPLE_SIZE_TIP,
                                         IDS_WAND_THIS_LAYER,
                                         IDS_WAND_ALL_LAYERS,
                                         IDS_WAND_SAMPLE_TIP,
                                         IDS_WAND_CONTIGUOUS,
                                         IDS_WAND_CONTIGUOUS_TIP,
                                         IDS_ERROR_BRUSH_BUDGET,
                                         IDS_BRUSH_MODE_PAINT,
                                         IDS_BRUSH_MODE_ERASE,
                                         IDS_BRUSH_MODE_TIP,
                                         IDS_BRUSH_SIZE,
                                         IDS_BRUSH_SIZE_TIP,
                                         IDS_BRUSH_HARDNESS,
                                         IDS_BRUSH_HARDNESS_TIP,
                                         IDS_BRUSH_OPACITY,
                                         IDS_BRUSH_OPACITY_TIP,
                                         IDS_BRUSH_MASK_BLACK,
                                         IDS_BRUSH_MASK_WHITE,
                                         IDS_BRUSH_MASK_TIP,
                                         IDS_BRUSH_COLOR,
                                         IDS_BRUSH_MASK_NOTE,
                                         IDS_ERROR_CLONE_SOURCE,
                                         IDS_ERROR_SMEAR_MASK,
                                         IDS_ERROR_SHAPE_SIZE,
                                         IDS_HEAL_TYPE_TIP,
                                         IDS_HEAL_CONTENT_AWARE,
                                         IDS_HEAL_CREATE_TEXTURE,
                                         IDS_HEAL_PROXIMITY_MATCH,
                                         IDS_CLONE_ALIGNED,
                                         IDS_CLONE_ALIGNED_TIP,
                                         IDS_CLONE_THIS_LAYER,
                                         IDS_CLONE_ALL_LAYERS,
                                         IDS_CLONE_SAMPLE_TIP,
                                         IDS_CLONE_SOURCE_NOTE,
                                         IDS_SMEAR_LIQUIFY,
                                         IDS_SMEAR_BLUR,
                                         IDS_SMEAR_SMUDGE,
                                         IDS_SMEAR_MODE_TIP,
                                         IDS_BRUSH_STRENGTH,
                                         IDS_GRADIENT_LINEAR,
                                         IDS_GRADIENT_RADIAL,
                                         IDS_GRADIENT_SHAPE_TIP,
                                         IDS_GRADIENT_FG_BG,
                                         IDS_GRADIENT_FG_TRANSPARENT,
                                         IDS_GRADIENT_COLORS_TIP,
                                         IDS_GRADIENT_REVERSE,
                                         IDS_GRADIENT_OPACITY_TIP,
                                         IDS_SHAPE_RECTANGLE,
                                         IDS_SHAPE_ELLIPSE,
                                         IDS_SHAPE_KIND_TIP,
                                         IDS_SHAPE_RADIUS,
                                         IDS_SHAPE_RADIUS_TIP,
                                         IDS_SHAPE_FILL,
                                         IDS_SHAPE_FILL_TIP,
                                         IDS_CMD_SAMPLE_RING,
                                         IDS_CMD_CUT,
                                         IDS_CMD_COPY,
                                         IDS_CMD_COPY_MERGED,
                                         IDS_CMD_PASTE,
                                         IDS_CMD_FILL_FOREGROUND,
                                         IDS_CMD_FILL_BACKGROUND,
                                         IDS_CMD_CLEAR_PIXELS,
                                         IDS_CMD_INVERT,
                                         IDS_CMD_INVERT_MASK,
                                         IDS_CMD_LAYER_VIA_COPY,
                                         IDS_CMD_ADD_MASK,
                                         IDS_CMD_ADD_BLACK_MASK,
                                         IDS_CMD_DELETE_LAYER_MASK,
                                         IDS_CMD_TRANSFORM_SELECTION,
                                         IDS_CMD_CURVES,
                                         IDS_CMD_LEVELS,
                                         IDS_CMD_HUE_SATURATION,
                                         IDS_CMD_EXPOSURE,
                                         IDS_CMD_GRADIENT_MAP,
                                         IDS_CMD_GRAIN,
                                         IDS_CMD_GAUSSIAN_BLUR,
                                         IDS_CMD_MOTION_BLUR,
                                         IDS_CMD_ADD_NOISE,
                                         IDS_CMD_LENS_CORRECTION,
                                         IDS_CMD_NEW_ADJUSTMENT,
                                         IDS_CMD_ADJUST_LEVELS,
                                         IDS_CMD_ADJUST_CURVES,
                                         IDS_CMD_ADJUST_HSV,
                                         IDS_CMD_ADJUST_EXPOSURE,
                                         IDS_CMD_ADJUST_GRADIENT_MAP,
                                         IDS_CMD_ADJUST_GRAIN,
                                         IDS_CMD_EDIT_ADJUSTMENT,
                                         IDS_ADJUST_CHANNEL,
                                         IDS_ADJUST_RGB,
                                         IDS_ADJUST_RED,
                                         IDS_ADJUST_GREEN,
                                         IDS_ADJUST_BLUE,
                                         IDS_ADJUST_REMOVE_POINT,
                                         IDS_ADJUST_RESET,
                                         IDS_ADJUST_CURVE_HINT,
                                         IDS_ADJUST_CURVE_POINT,
                                         IDS_ADJUST_INPUT_BLACK,
                                         IDS_ADJUST_GAMMA,
                                         IDS_ADJUST_INPUT_WHITE,
                                         IDS_ADJUST_OUTPUT_BLACK,
                                         IDS_ADJUST_OUTPUT_WHITE,
                                         IDS_ADJUST_SAMPLE_BLACK,
                                         IDS_ADJUST_SAMPLE_GRAY,
                                         IDS_ADJUST_SAMPLE_WHITE,
                                         IDS_ADJUST_SAMPLE_TIP,
                                         IDS_ADJUST_AUTO_CONTRAST,
                                         IDS_ADJUST_AUTO_COLOR,
                                         IDS_ADJUST_AUTO_NEUTRAL,
                                         IDS_ADJUST_RANGE,
                                         IDS_ADJUST_MASTER,
                                         IDS_ADJUST_REDS,
                                         IDS_ADJUST_YELLOWS,
                                         IDS_ADJUST_GREENS,
                                         IDS_ADJUST_CYANS,
                                         IDS_ADJUST_BLUES,
                                         IDS_ADJUST_MAGENTAS,
                                         IDS_ADJUST_HUE,
                                         IDS_ADJUST_SATURATION,
                                         IDS_ADJUST_LIGHTNESS,
                                         IDS_ADJUST_HUE_SAMPLE,
                                         IDS_ADJUST_HUE_ADD,
                                         IDS_ADJUST_HUE_REMOVE,
                                         IDS_ADJUST_HUE_SAMPLE_TIP,
                                         IDS_ADJUST_TARGETED,
                                         IDS_ADJUST_TARGETED_TIP,
                                         IDS_ADJUST_INVERT_RANGE,
                                         IDS_ADJUST_COLORIZE,
                                         IDS_ADJUST_SHADOWS,
                                         IDS_ADJUST_HIGHLIGHTS,
                                         IDS_ADJUST_REVERSE,
                                         IDS_ADJUST_EXPOSURE,
                                         IDS_ADJUST_OFFSET,
                                         IDS_ADJUST_AMOUNT,
                                         IDS_ADJUST_SIZE,
                                         IDS_ADJUST_ROUGHNESS,
                                         IDS_ADJUST_RADIUS,
                                         IDS_ADJUST_ANGLE,
                                         IDS_ADJUST_DISTANCE,
                                         IDS_ADJUST_UNIFORM,
                                         IDS_ADJUST_GAUSSIAN,
                                         IDS_ADJUST_MONOCHROMATIC,
                                         IDS_ADJUST_DISTORTION,
                                         IDS_ADJUST_LENS_HINT,
                                         IDS_ADJUST_PREVIEW,
                                         IDS_ADJUST_LIMITED,
                                         IDS_ADJUST_OK,
                                         IDS_ADJUST_EDIT_TITLE,
                                         IDS_ERROR_FILTER,
                                         IDS_ERROR_PIXEL_EDIT,
                                         IDS_ERROR_LAYER_METADATA,
                                         IDS_ERROR_SELECTION_EDIT,
                                         IDS_ERROR_HISTORY_RESTORE,
                                         IDS_ERROR_HISTORY_RESIDENCY,
                                         IDS_CMD_CONTENT_AWARE_FILL,
                                         IDS_ADJUST_CONTENT_FILL_HINT,
                                         IDS_ADJUST_NO_SOURCE,
                                         IDS_ERROR_UNKNOWN_ARGUMENT,
                                         IDS_ERROR_KERNEL_ISA_UNSUPPORTED,
                                         IDS_ERROR_KERNEL_ISA_NOT_IMPLEMENTED,
                                         IDS_CRASH_PROMPT,
                                         IDS_A11Y_MENU_BAR,
                                         IDS_A11Y_PROJECT_TABS,
                                         IDS_A11Y_TOOLS,
                                         IDS_A11Y_LAYERS,
                                         IDS_A11Y_LAYERS_SPLITTER,
                                         IDS_A11Y_COLOR_PICKER,
                                         IDS_A11Y_GRADIENT_START,
                                         IDS_A11Y_GRADIENT_END,
                                         IDS_A11Y_NOISE_DISTRIBUTION,
                                         IDS_A11Y_CURVE_EDITOR,
                                         IDS_A11Y_LEVELS_INPUT,
                                         IDS_A11Y_LEVELS_OUTPUT,
                                         IDS_A11Y_HUE_SPECTRUM,
                                         IDS_A11Y_GRADIENT_RAMP,
                                         IDS_A11Y_SAMPLING,
                                         IDS_CMD_NEW_CANVAS,
                                         IDS_CMD_IMPORT_IMAGES,
                                         IDS_CMD_CLOSE_PROJECT,
                                         IDS_CMD_CANVAS_SIZE,
                                         IDS_CMD_IMAGE_SIZE,
                                         IDS_CMD_GROUP_LAYERS,
                                         IDS_CMD_RENAME_LAYER,
                                         IDS_CMD_MOVE_LAYER_UP,
                                         IDS_CMD_MOVE_LAYER_DOWN,
                                         IDS_CMD_CLIP_CREATE,
                                         IDS_CMD_CLIP_RELEASE,
                                         IDS_CMD_MERGE_DOWN,
                                         IDS_OPEN_IMAGE_TITLE,
                                         IDS_OPEN_IMAGES_TITLE,
                                         IDS_OPEN_IMAGE_TEXT,
                                         IDS_OPEN_IMAGE_ADD_LAYER,
                                         IDS_OPEN_IMAGE_NEW_PROJECT,
                                         IDS_CONFIRM_SAVE_TITLE,
                                         IDS_CONFIRM_SAVE_TEXT,
                                         IDS_CONFIRM_SAVE_SAVE,
                                         IDS_CONFIRM_SAVE_DISCARD,
                                         IDS_QUESTION_FALLBACK,
                                         IDS_SIZE_NEW_CANVAS_TITLE,
                                         IDS_SIZE_NEW_CANVAS_HINT,
                                         IDS_SIZE_WIDTH,
                                         IDS_SIZE_HEIGHT,
                                         IDS_SIZE_CREATE,
                                         IDS_SIZE_NEW_CANVAS_INVALID,
                                         IDS_SIZE_TRANSPARENT_NOTE,
                                         IDS_SIZE_OPEN_PROJECT,
                                         IDS_SIZE_IMPORT_IMAGE,
                                         IDS_SIZE_CANVAS_TITLE,
                                         IDS_SIZE_CURRENT,
                                         IDS_SIZE_BYTES,
                                         IDS_SIZE_UNITS,
                                         IDS_SIZE_UNIT_PIXELS,
                                         IDS_SIZE_UNIT_PERCENT,
                                         IDS_SIZE_UNIT_INCHES,
                                         IDS_SIZE_UNIT_CENTIMETERS,
                                         IDS_SIZE_RELATIVE,
                                         IDS_SIZE_LOCK_ORIGINAL,
                                         IDS_SIZE_NEW,
                                         IDS_SIZE_CANVAS_INVALID,
                                         IDS_SIZE_ANCHOR,
                                         IDS_SIZE_ANCHOR_HINT,
                                         IDS_SIZE_ANCHOR_TOP_LEFT,
                                         IDS_SIZE_ANCHOR_TOP_CENTER,
                                         IDS_SIZE_ANCHOR_TOP_RIGHT,
                                         IDS_SIZE_ANCHOR_MIDDLE_LEFT,
                                         IDS_SIZE_ANCHOR_CENTER,
                                         IDS_SIZE_ANCHOR_MIDDLE_RIGHT,
                                         IDS_SIZE_ANCHOR_BOTTOM_LEFT,
                                         IDS_SIZE_ANCHOR_BOTTOM_CENTER,
                                         IDS_SIZE_ANCHOR_BOTTOM_RIGHT,
                                         IDS_SIZE_EXTENSION,
                                         IDS_SIZE_EXT_TRANSPARENT,
                                         IDS_SIZE_EXT_FOREGROUND,
                                         IDS_SIZE_EXT_BACKGROUND,
                                         IDS_SIZE_EXT_BLACK,
                                         IDS_SIZE_EXT_WHITE,
                                         IDS_SIZE_EXT_CUSTOM,
                                         IDS_SIZE_EXTENSION_COLOR,
                                         IDS_SIZE_IMAGE_TITLE,
                                         IDS_SIZE_LOCK,
                                         IDS_SIZE_RESOLUTION,
                                         IDS_SIZE_PPI,
                                         IDS_SIZE_RESAMPLE,
                                         IDS_SIZE_SAMPLING,
                                         IDS_SIZE_RESAMPLE_NOTE,
                                         IDS_SIZE_NO_RESAMPLE_NOTE,
                                         IDS_SIZE_RESULT,
                                         IDS_SIZE_IMAGE_INVALID,
                                         IDS_SIZE_RESIZE,
                                         IDS_ERROR_RESIZE,
                                         IDS_ERROR_DOCUMENT_LIFECYCLE,
                                         IDS_SIZE_MEGABYTES,
                                         IDS_SIZE_KILOBYTES,
                                         IDS_CROP_RATIO,
                                         IDS_CROP_FREE,
                                         IDS_CROP_ORIGINAL,
                                         IDS_CROP_1_1,
                                         IDS_CROP_4_3,
                                         IDS_CROP_16_9,
                                         IDS_CROP_SIZE,
                                         IDS_CROP_APPLY,
                                         IDS_CROP_RATIO_TIP,
                                         IDS_RENAME_TITLE,
                                         IDS_RENAME_NAME,
                                         IDS_CMD_MERGE_GROUP,
                                         IDS_CMD_MERGE_LAYERS,
                                         IDS_CMD_KEYBOARD_SHORTCUTS,
                                         IDS_SHORTCUTS_TITLE,
                                         IDS_SHORTCUTS_SEARCH,
                                         IDS_CMD_UNAVAILABLE,
                                         IDS_PALETTE_SWAP,
                                         IDS_PALETTE_RESET,
                                         IDS_CMD_SWAP_PALETTE,
                                         IDS_CMD_RESET_PALETTE,
                                         IDS_MORE_TOOLS,
                                         IDS_CMD_TOOL_MOVE,
                                         IDS_CMD_TOOL_MARQUEE,
                                         IDS_CMD_TOOL_LASSO,
                                         IDS_CMD_TOOL_WAND,
                                         IDS_CMD_TOOL_CROP,
                                         IDS_CMD_TOOL_BRUSH,
                                         IDS_CMD_TOOL_ERASER,
                                         IDS_CMD_TOOL_HEAL,
                                         IDS_CMD_TOOL_CLONE,
                                         IDS_CMD_TOOL_SMEAR,
                                         IDS_CMD_TOOL_GRADIENT,
                                         IDS_CMD_TOOL_SHAPE,
                                         IDS_CMD_TOOL_EYEDROPPER,
                                         IDS_CMD_TOOL_HAND,
                                         IDS_CMD_TOOL_ZOOM,
                                         IDS_CMD_NUDGE_SELECTION_LEFT,
                                         IDS_CMD_NUDGE_SELECTION_RIGHT,
                                         IDS_CMD_NUDGE_SELECTION_UP,
                                         IDS_CMD_NUDGE_SELECTION_DOWN,
                                         IDS_CMD_NUDGE_LAYER_LEFT,
                                         IDS_CMD_NUDGE_LAYER_RIGHT,
                                         IDS_CMD_NUDGE_LAYER_UP,
                                         IDS_CMD_NUDGE_LAYER_DOWN,
                                         IDS_CMD_NUDGE_PIXELS_LEFT,
                                         IDS_CMD_NUDGE_PIXELS_RIGHT,
                                         IDS_CMD_NUDGE_PIXELS_UP,
                                         IDS_CMD_NUDGE_PIXELS_DOWN,
                                         IDS_CMD_BRUSH_SMALLER,
                                         IDS_CMD_BRUSH_LARGER,
                                         IDS_CMD_BRUSH_SOFTER,
                                         IDS_CMD_BRUSH_HARDER,
                                         IDS_CMD_BLEND_PREVIOUS,
                                         IDS_CMD_BLEND_NEXT,
                                         IDS_CMD_DELETE_KEY,
                                         IDS_CMD_TOGGLE_MARQUEE,
                                         IDS_CMD_TOGGLE_LASSO,
                                         IDS_CMD_TOGGLE_SHAPE,
                                         IDS_UNIT_INCHES,
                                         IDS_UNIT_CENTIMETERS,
                                         IDS_A11Y_PALETTE_NAME,
                                         IDS_STATUS_BUSY,
                                         IDS_STATUS_QUEUED_STALE,
                                         IDS_A11Y_JOB_PROGRESS,
                                         IDS_KEY_CTRL,
                                         IDS_KEY_ALT,
                                         IDS_KEY_SHIFT,
                                         IDS_KEY_JOIN,
                                         IDS_KEY_LIST,
                                         IDS_KEY_NUMBER_ROW_MINUS,
                                         IDS_KEY_NUMBER_ROW_PLUS,
                                         IDS_KEY_BRACKET_LEFT,
                                         IDS_KEY_BRACKET_RIGHT,
                                         IDS_KEY_FUNCTION,
                                         IDS_KEY_VIRTUAL,
                                         IDS_SHUTDOWN_UNSAVED,
                                         IDS_SHUTDOWN_SAVING,
                                         IDS_ERROR_CANVAS_DRAW,
                                         IDS_ERROR_SELECTION_DETAILED,
                                         IDS_CMD_TARGET_MASK,
                                         IDS_CMD_TARGET_PIXELS,
                                         IDS_CMD_UNLINK_MASK,
                                         IDS_CMD_LINK_MASK,
                                         IDS_CMD_DISABLE_MASK,
                                         IDS_CMD_ENABLE_MASK,
                                         IDS_LAYER_MASK_BADGE,
                                         IDS_LAYER_MASK_OFF_BADGE};
    for (const UINT id : kIds)
    {
        const std::wstring text = LoadStringResource(exe.get(), id);
        if (text.empty())
        {
            std::printf("     missing string %u\n", id);
        }
        Expect(!text.empty(), "strings.defined");
        Expect(PlaceholdersArePositional(text), "strings.positional-placeholders");
    }
    Expect(LoadStringResource(exe.get(), 99).empty(), "strings.unused-id-empty");
    Expect(FormatStringResource(exe.get(), IDS_STATUS_SIZE, 1280, 800) == L"1280 × 800", "strings.format-positional");
    Expect(FormatStringResource(exe.get(), IDS_TOOL_TOOLTIP, std::wstring(L"Move"), std::wstring(L"V")) == L"Move (V)",
           "strings.format-two-arguments");
    Expect(FormatStringResource(exe.get(), IDS_A11Y_PALETTE_NAME, std::wstring(L"Foreground"),
                                std::wstring(L"Background")) == L"Foreground, Background",
           "strings.format-palette-name");
    Expect(PlainCommandName(LoadStringResource(exe.get(), IDS_CMD_SAVE_AS)) == L"Save As" &&
               CaptionWithoutChord(LoadStringResource(exe.get(), IDS_CMD_SAVE_AS)) == L"Save &As..." &&
               PlainCommandName(L"Rock && Ro&ll… \tCtrl+R") == L"Rock & Roll" && PlainCommandName(L"E&xit") == L"Exit",
           "strings.plain-command-name");
    Expect(FormatStringResource(exe.get(), IDS_KEY_JOIN, LoadStringResource(exe.get(), IDS_KEY_CTRL),
                                std::wstring(L"S")) == L"Ctrl+S" &&
               FormatStringResource(exe.get(), IDS_KEY_LIST, std::wstring(L"["), std::wstring(L"]")) == L"[, ]",
           "strings.chord-joiners");
    // Key names a layout does not give come from the table, never from code (RF-1112).
    Expect(FormatStringResource(exe.get(), IDS_KEY_FUNCTION, 13u) == L"F13" &&
               FormatStringResource(exe.get(), IDS_KEY_VIRTUAL, 0xE7u) == L"Key E7" &&
               LoadStringResource(exe.get(), IDS_KEY_NUMBER_ROW_PLUS) == L"=" &&
               LoadStringResource(exe.get(), IDS_KEY_BRACKET_RIGHT) == L"]",
           "strings.key-name-fallbacks");
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        Expect(!LoadStringResource(exe.get(), EditorToolNameId(EditorToolAt(index))).empty(), "strings.tool-names");
    }
    // Every command's caption keeps its first default chord as the canonical accelerator column (the chrome shows
    // the effective chord with the layout's key names instead), and so do the captions that follow the target.
    for (const CommandCatalogEntry& entry : CommandCatalog())
    {
        const std::wstring caption = LoadStringResource(exe.get(), entry.captionId);
        const std::size_t tab = caption.find(L'\t');
        const std::wstring column = tab == std::wstring::npos ? std::wstring{} : caption.substr(tab + 1);
        const std::wstring expected = CanonicalChordText(PrimaryDefaultChord(entry.command));
        if (column != expected)
        {
            std::printf("     command %d column '%ls', default '%ls'\n", entry.command, column.c_str(),
                        expected.c_str());
        }
        Expect(column == expected, "strings.accelerator-column-is-the-default-chord");
    }
    struct CaptionVariant
    {
        UINT captionId;
        int command;
    };
    constexpr std::array<CaptionVariant, 11> kVariants{{
        {IDS_CMD_UNDO_NAMED, CommandIds::kCommandUndo},
        {IDS_CMD_REDO_NAMED, CommandIds::kCommandRedo},
        {IDS_CMD_INVERT_MASK, CommandIds::kCommandInvert},
        {IDS_CMD_TRANSFORM_SELECTION, CommandIds::kCommandFreeTransform},
        {IDS_CMD_DELETE_LAYER_MASK, CommandIds::kCommandDeleteLayer},
        {IDS_CMD_CLIP_RELEASE, CommandIds::kCommandClipToggle},
        {IDS_CMD_MERGE_GROUP, CommandIds::kCommandMergeDown},
        {IDS_CMD_MERGE_LAYERS, CommandIds::kCommandMergeDown},
        {IDS_CMD_TARGET_PIXELS, CommandIds::kCommandTargetLayerMask},
        {IDS_CMD_LINK_MASK, CommandIds::kCommandToggleMaskLink},
        {IDS_CMD_ENABLE_MASK, CommandIds::kCommandToggleLayerMask},
    }};
    for (const CaptionVariant& variant : kVariants)
    {
        // A command without a default chord (the mask commands) has no column in any of its captions.
        const std::wstring caption = LoadStringResource(exe.get(), variant.captionId);
        const std::size_t tab = caption.find(L'\t');
        const std::wstring column = tab == std::wstring::npos ? std::wstring{} : caption.substr(tab + 1);
        Expect(!caption.empty() && column == CanonicalChordText(PrimaryDefaultChord(variant.command)),
               "strings.variant-caption-column-is-the-default-chord");
    }
}

// ── Keyboard chords and the command catalog (RF-303, Specs/Core/Core_Settings.md "Command chords") ──

void TestKeyChords()
{
    std::uint16_t key = 0;
    bool named = true;
    for (const KeyChordNames::NamedKey& entry : KeyChordNames::kNamedKeys)
    {
        std::uint16_t parsed = 0;
        named = named && ParseStableKeyName(entry.name, parsed) && parsed == entry.virtualKey &&
                ParseStableKeyName(StableKeyName(entry.virtualKey), parsed) && parsed == entry.virtualKey;
    }
    Expect(named, "chords.named-keys-round-trip");
    Expect(ParseStableKeyName("delete", key) && key == VK_DELETE && ParseStableKeyName("DELETE", key) &&
               key == VK_DELETE && ParseStableKeyName("numpadplus", key) && key == VK_ADD &&
               ParseStableKeyName("f5", key) && key == VK_F5 && ParseStableKeyName("vk_e7", key) && key == 0xE7 &&
               ParseStableKeyName("q", key) && key == 'Q',
           "chords.vk-names-ignore-case");
    Expect(!ParseStableKeyName("F0", key) && !ParseStableKeyName("F25", key) && !ParseStableKeyName("VK_G1", key) &&
               !ParseStableKeyName("", key) && !ParseStableKeyName("Hyper", key) && !ParseStableKeyName("!", key),
           "chords.vk-names-reject");
    Expect(StableKeyName(VK_F24) == "F24" && StableKeyName('7') == "7" && StableKeyName(0xE7) == "VK_E7" &&
               StableKeyName(VK_RETURN) == "Enter",
           "chords.stable-names");
    Expect(ParseKeyPositionName("numberRowPlus") == KeyPosition::NumberRowPlus &&
               ParseKeyPositionName("BRACKETLEFT") == KeyPosition::BracketLeft &&
               ParseKeyPositionName("") == KeyPosition::None && ParseKeyPositionName("minus") == KeyPosition::None &&
               KeyPositionName(KeyPosition::BracketRight) == "bracketRight",
           "chords.position-names");
    Expect(KeyPositionFromScanCode(0x0C, false) == KeyPosition::NumberRowMinus &&
               KeyPositionFromScanCode(0x0D, false) == KeyPosition::NumberRowPlus &&
               KeyPositionFromScanCode(0x1A, false) == KeyPosition::BracketLeft &&
               KeyPositionFromScanCode(0x1B, false) == KeyPosition::BracketRight &&
               KeyPositionFromScanCode(0x1B, true) == KeyPosition::None &&
               KeyPositionFromScanCode(0x1C, false) == KeyPosition::None &&
               KeyPositionScanCode(KeyPosition::NumberRowPlus) == 0x0D,
           "chords.positions-from-scan-codes");
    Expect(CanonicalChordText(Ctrl(Shift(Key('S')))) == L"Ctrl+Shift+S" &&
               CanonicalChordText(Ctrl(At(KeyPosition::NumberRowPlus))) == L"Ctrl++" &&
               CanonicalChordText(Shift(At(KeyPosition::NumberRowPlus))) == L"Shift+=" &&
               CanonicalChordText(Alt(Key(VK_F4))) == L"Alt+F4" &&
               CanonicalChordText(Ctrl(Alt(Key('G')))) == L"Ctrl+Alt+G" &&
               CanonicalChordText(At(KeyPosition::BracketLeft)) == L"[" &&
               CanonicalChordText(Key(VK_ADD)) == L"NumpadPlus" && CanonicalChordText(KeyChord{}).empty(),
           "chords.canonical-text");
    Expect(IsModifierVirtualKey(VK_SHIFT) && IsModifierVirtualKey(VK_RMENU) && IsModifierVirtualKey(VK_LWIN) &&
               !IsModifierVirtualKey('A') && !IsModifierVirtualKey(VK_F10),
           "chords.modifier-keys");
    Expect(IsExtendedNavigationKey(VK_DELETE) && IsExtendedNavigationKey(VK_LEFT) && IsExtendedNavigationKey(VK_HOME) &&
               IsExtendedNavigationKey(VK_INSERT) && !IsExtendedNavigationKey(VK_ADD) &&
               !IsExtendedNavigationKey(VK_BACK) && !IsExtendedNavigationKey('A'),
           "chords.navigation-keys-are-extended");
}

void TestCommandCatalog()
{
    std::set<int> commands;
    std::set<std::wstring> textIds;
    std::set<unsigned> captions;
    bool roundTrip = true;
    for (const CommandCatalogEntry& entry : CommandCatalog())
    {
        commands.insert(entry.command);
        textIds.insert(entry.textId);
        captions.insert(entry.captionId);
        roundTrip = roundTrip && std::wstring_view{entry.textId}.starts_with(L"cmd/") &&
                    CommandForTextId(entry.textId) == entry.command && FindCommand(entry.command) == &entry;
    }
    const std::size_t count = CommandCatalog().size();
    Expect(commands.size() == count && textIds.size() == count && captions.size() == count,
           "commands.unique-ids-text-ids-and-captions");
    Expect(roundTrip && CommandForTextId(L"cmd/future/command") == 0 && FindCommand(99999) == nullptr,
           "commands.text-id-round-trip");
    bool known = true;
    bool reserved = true;
    for (const DefaultChord& chord : DefaultChords())
    {
        known = known && FindCommand(chord.command) != nullptr && !chord.chord.IsEmpty();
        reserved = reserved && (IsReservedChord(chord.chord) == (chord.kind == DefaultChordKind::System));
    }
    Expect(known, "commands.defaults-name-catalog-commands");
    Expect(reserved, "commands.only-system-defaults-are-window-manager-chords");
    // Two commands dispatch one default chord only under different conditions, the conditioned one first (M on the
    // Marquee, the arrows): a new default never lands on another command's chord (RF-1117).
    bool unambiguous = true;
    const std::span<const DefaultChord> defaultChords = DefaultChords();
    for (std::size_t first = 0; first < defaultChords.size(); ++first)
    {
        for (std::size_t second = first + 1; second < defaultChords.size(); ++second)
        {
            const DefaultChord& earlier = defaultChords[first];
            const DefaultChord& later = defaultChords[second];
            if (earlier.kind != DefaultChordKind::Dispatch || later.kind != DefaultChordKind::Dispatch ||
                earlier.command == later.command || !(earlier.chord == later.chord))
            {
                continue;
            }
            const CommandCatalogEntry* const earlierEntry = FindCommand(earlier.command);
            const CommandCatalogEntry* const laterEntry = FindCommand(later.command);
            unambiguous = unambiguous && earlierEntry != nullptr && laterEntry != nullptr &&
                          earlierEntry->condition != CommandCondition::None &&
                          earlierEntry->condition != laterEntry->condition;
        }
    }
    Expect(unambiguous, "commands.default-chords-unambiguous");
    Expect(PrimaryDefaultChord(CommandIds::kCommandRedo) == Ctrl(Shift(Key('Z'))) &&
               PrimaryDefaultChord(CommandIds::kCommandZoomIn) == Ctrl(At(KeyPosition::NumberRowPlus)) &&
               PrimaryDefaultChord(CommandIds::kCommandExportPng) == Ctrl(Shift(Key('E'))) &&
               PrimaryDefaultChord(CommandIds::kCommandImportImages).IsEmpty(),
           "commands.primary-defaults");
}

void TestCommandBindings()
{
    using C = CommandIds;
    const auto press = [](KeyChord chord)
    {
        KeyPress key;
        key.virtualKey = chord.virtualKey;
        key.position = chord.position;
        key.ctrl = chord.ctrl;
        key.alt = chord.alt;
        key.shift = chord.shift;
        return key;
    };
    const auto state = [](KeyFocus focus, EditorTool tool = EditorTool::Move, bool selection = false,
                          bool lasso = false) { return KeyState{focus, tool, selection, lasso}; };
    const KeyState canvas = state(KeyFocus::Canvas);
    const KeyState chrome = state(KeyFocus::Chrome);
    const KeyState text = state(KeyFocus::TextInput);
    const auto runs = [](const CommandBindings& table, const KeyPress& key, const KeyState& at)
    {
        const KeyMatch match = table.Match(key, at);
        return match.result == KeyMatchResult::Command ? match.command : 0;
    };
    const auto build = [](std::initializer_list<ShortcutBinding> replacements)
    {
        CommandBindings table;
        const std::vector<ShortcutBinding> list(replacements);
        table.Build(list);
        return table;
    };

    // Defaults, aliases included.
    const CommandBindings defaults;
    Expect(runs(defaults, press(Ctrl(Key('Z'))), chrome) == C::kCommandUndo &&
               runs(defaults, press(Ctrl(Shift(Key('Z')))), chrome) == C::kCommandRedo &&
               runs(defaults, press(Ctrl(Key('Y'))), chrome) == C::kCommandRedo &&
               runs(defaults, press(Ctrl(At(KeyPosition::NumberRowPlus))), canvas) == C::kCommandZoomIn &&
               runs(defaults, press(Ctrl(Shift(At(KeyPosition::NumberRowPlus)))), canvas) == C::kCommandZoomIn &&
               runs(defaults, press(Ctrl(Key(VK_ADD))), canvas) == C::kCommandZoomIn &&
               runs(defaults, press(Ctrl(Key(VK_SUBTRACT))), canvas) == C::kCommandZoomOut &&
               defaults.ChordFor(C::kCommandRedo) == Ctrl(Shift(Key('Z'))),
           "bindings.defaults-and-aliases");
    // A replacement takes every default chord of its command, aliases included.
    const CommandBindings redoOnK = build({{L"cmd/edit/redo", Key('K')}});
    const CommandBindings zoomOnK = build({{L"cmd/view/zoomIn", Ctrl(Key('K'))}});
    Expect(runs(redoOnK, press(Ctrl(Key('Y'))), chrome) == 0 &&
               runs(redoOnK, press(Ctrl(Shift(Key('Z')))), chrome) == 0 &&
               runs(redoOnK, press(Key('K')), canvas) == C::kCommandRedo &&
               redoOnK.ChordFor(C::kCommandRedo) == Key('K') &&
               runs(zoomOnK, press(Ctrl(At(KeyPosition::NumberRowPlus))), canvas) == 0 &&
               runs(zoomOnK, press(Ctrl(Shift(At(KeyPosition::NumberRowPlus)))), canvas) == 0 &&
               runs(zoomOnK, press(Ctrl(Key(VK_ADD))), canvas) == 0 &&
               runs(zoomOnK, press(Ctrl(Key('K'))), canvas) == C::kCommandZoomIn,
           "bindings.replacement-removes-every-default");
    // The later of two entries for one command wins; a later entry takes a chord from another command.
    const CommandBindings twice = build({{L"cmd/edit/swapPalette", Key('Q')}, {L"cmd/edit/swapPalette", Key('K')}});
    const CommandBindings taken = build({{L"cmd/edit/swapPalette", Key('Q')}, {L"cmd/edit/resetPalette", Key('Q')}});
    Expect(runs(twice, press(Key('Q')), canvas) == 0 &&
               runs(twice, press(Key('K')), canvas) == C::kCommandSwapPalette &&
               twice.ChordFor(C::kCommandSwapPalette) == Key('K') &&
               runs(taken, press(Key('Q')), canvas) == C::kCommandResetPalette &&
               !taken.ChordFor(C::kCommandSwapPalette).has_value() && runs(taken, press(Key('X')), canvas) == 0,
           "bindings.later-entry-wins");
    // A replacement's chord is taken from other commands' defaults.
    const CommandBindings swapOnB = build({{L"cmd/edit/swapPalette", Key('B')}});
    Expect(runs(swapOnB, press(Key('B')), canvas) == C::kCommandSwapPalette &&
               !swapOnB.ChordFor(C::SelectToolCommand(EditorTool::Brush)).has_value(),
           "bindings.replacement-shadows-defaults");
    // Delete Layer's Delete is a label while the Delete key's command dispatches Delete.
    const CommandBindings deleteOnQ = build({{L"cmd/edit/delete", Key('Q')}});
    const CommandBindings layerOnCtrlDelete = build({{L"cmd/layer/delete", Ctrl(Key(VK_DELETE))}});
    Expect(defaults.ChordFor(C::kCommandDeleteLayer) == Key(VK_DELETE) &&
               runs(defaults, press(Key(VK_DELETE)), canvas) == C::kCommandDeleteKey &&
               !deleteOnQ.ChordFor(C::kCommandDeleteLayer).has_value() &&
               runs(deleteOnQ, press(Key(VK_DELETE)), canvas) == 0 &&
               runs(deleteOnQ, press(Key('Q')), canvas) == C::kCommandDeleteKey &&
               runs(layerOnCtrlDelete, press(Key(VK_DELETE)), canvas) == C::kCommandDeleteKey &&
               runs(layerOnCtrlDelete, press(Ctrl(Key(VK_DELETE))), chrome) == C::kCommandDeleteLayer &&
               layerOnCtrlDelete.ChordFor(C::kCommandDeleteLayer) == Ctrl(Key(VK_DELETE)),
           "bindings.shared-label-follows-the-dispatching-command");
    // Decision D5: window-manager chords are never taken; an entry naming one, a modifier key or an unknown command
    // is ignored. Exit shows Alt+F4 and never dispatches it.
    const CommandBindings ignored = build({{L"cmd/file/save", Alt(Key(VK_F4))},
                                           {L"cmd/edit/swapPalette", Key(VK_F10)},
                                           {L"cmd/edit/resetPalette", Alt(Key(VK_SPACE))},
                                           {L"cmd/edit/undo", Key(VK_SHIFT)},
                                           {L"cmd/future/command", Key('Q')},
                                           {L"cmd/tool/brush", KeyChord{}}});
    const CommandBindings exitOnCtrlQ = build({{L"cmd/file/exit", Ctrl(Key('Q'))}});
    Expect(ignored.ChordFor(C::kCommandSave) == Ctrl(Key('S')) &&
               ignored.ChordFor(C::kCommandSwapPalette) == Key('X') &&
               ignored.ChordFor(C::kCommandResetPalette) == Key('D') &&
               ignored.ChordFor(C::kCommandUndo) == Ctrl(Key('Z')) &&
               ignored.ChordFor(C::SelectToolCommand(EditorTool::Brush)) == Key('B') &&
               runs(ignored, press(Key('Q')), canvas) == 0 && runs(defaults, press(Alt(Key(VK_F4))), chrome) == 0 &&
               defaults.ChordFor(C::kCommandExit) == Alt(Key(VK_F4)) &&
               runs(exitOnCtrlQ, press(Ctrl(Key('Q'))), chrome) == C::kCommandExit &&
               runs(exitOnCtrlQ, press(Alt(Key(VK_F4))), chrome) == 0,
           "bindings.window-manager-chords-and-unusable-entries");
    // Exact modifiers: a Ctrl chord does not run with Shift or Alt added.
    Expect(runs(defaults, press(Ctrl(Key('X'))), chrome) == C::kCommandCut &&
               runs(defaults, press(Ctrl(Shift(Key('X')))), chrome) == 0 &&
               runs(defaults, press(Ctrl(Alt(Key('X')))), chrome) == 0 &&
               runs(defaults, press(Ctrl(Alt(Key('E')))), chrome) == 0,
           "bindings.exact-modifiers");
    // AltGr (Ctrl+Alt) text is typing: Ctrl+Alt+G is the clip toggle only where the layout types nothing for it.
    KeyPress altGr = press(Ctrl(Alt(Key('G'))));
    const int clip = runs(defaults, altGr, chrome);
    altGr.typesCharacter = true;
    Expect(clip == C::kCommandClipToggle && runs(defaults, altGr, chrome) == 0, "bindings.altgr-text-is-not-a-chord");
    // The legacy File and Image key equivalents (RF-1117), beside the chords they differ from by a modifier: the
    // exports run from a text input too (the File menu is global), the size cards do not, and AltGr text is not the
    // chord (Polish AltGr+C types ć).
    KeyPress altGrC = press(Ctrl(Alt(Key('C'))));
    altGrC.typesCharacter = true;
    Expect(runs(defaults, press(Ctrl(Shift(Key('E')))), chrome) == C::kCommandExportPng &&
               runs(defaults, press(Ctrl(Alt(Shift(Key('S'))))), canvas) == C::kCommandExportJpeg &&
               runs(defaults, press(Ctrl(Alt(Key('C')))), canvas) == C::kCommandCanvasSize &&
               runs(defaults, press(Ctrl(Alt(Key('I')))), chrome) == C::kCommandImageSize &&
               runs(defaults, press(Ctrl(Shift(Key('E')))), text) == C::kCommandExportPng &&
               runs(defaults, press(Ctrl(Alt(Shift(Key('S'))))), text) == C::kCommandExportJpeg &&
               runs(defaults, press(Ctrl(Alt(Key('C')))), text) == 0 && runs(defaults, altGrC, canvas) == 0 &&
               runs(defaults, press(Ctrl(Key('E'))), chrome) == C::kCommandMergeDown &&
               runs(defaults, press(Ctrl(Shift(Key('S')))), chrome) == C::kCommandSaveAs &&
               runs(defaults, press(Ctrl(Key('C'))), chrome) == C::kCommandCopy &&
               runs(defaults, press(Ctrl(Shift(Key('I')))), chrome) == C::kCommandInverse &&
               defaults.ChordFor(C::kCommandCanvasSize) == Ctrl(Alt(Key('C'))),
           "bindings.legacy-file-and-image-chords");
    // A command that allows Shift takes its chord with Shift added (a nudge steps by ten, a tool key ignores it);
    // an exact Shift chord comes first.
    Expect(runs(defaults, press(Shift(Key('B'))), canvas) == C::SelectToolCommand(EditorTool::Brush) &&
               runs(defaults, press(Shift(Key(VK_LEFT))), canvas) == C::kCommandNudgeLayerLeft &&
               runs(defaults, press(Shift(Key('U'))), state(KeyFocus::Canvas, EditorTool::Brush)) ==
                   C::SelectToolCommand(EditorTool::Shape) &&
               runs(defaults, press(Shift(Key('U'))), state(KeyFocus::Canvas, EditorTool::Shape)) ==
                   C::kCommandToggleShapeKind &&
               runs(defaults, press(Shift(Key(VK_DELETE))), canvas) == C::kCommandContentAwareFill &&
               runs(defaults, press(Shift(Ctrl(Key('Z')))), chrome) == C::kCommandRedo,
           "bindings.shift-allowed-and-exact-first");
    // A match says whether Shift was added to its chord: a nudge steps by ten then, never when its own chord holds
    // Shift (a replacement on Shift+J steps by one).
    const CommandBindings nudgeOnShiftJ = build({{L"cmd/nudge/layerLeft", Shift(Key('J'))}});
    const KeyMatch shiftJ = nudgeOnShiftJ.Match(press(Shift(Key('J'))), canvas);
    Expect(defaults.Match(press(Shift(Key(VK_LEFT))), canvas).shiftAdded &&
               !defaults.Match(press(Key(VK_LEFT)), canvas).shiftAdded && shiftJ.command == C::kCommandNudgeLayerLeft &&
               !shiftJ.shiftAdded,
           "bindings.shift-added-steps-by-ten");
    // A replacement written by virtual key for a key the layout places on a position key (vk VK_BB, the US =) is
    // compared as that position (the host normalizes through the layout; a stand-in here): it takes the default of
    // that key, so Zoom In no longer claims Ctrl+= while the key runs Zoom Out.
    constexpr auto plusAsPosition = [](KeyChord chord) noexcept
    {
        if (chord.position == KeyPosition::None && chord.virtualKey == VK_OEM_PLUS)
        {
            chord.virtualKey = 0;
            chord.position = KeyPosition::NumberRowPlus;
        }
        return chord;
    };
    const std::vector<ShortcutBinding> zoomOutOnPlus{{L"cmd/view/zoomOut", Ctrl(Key(VK_OEM_PLUS))}};
    CommandBindings normalized;
    normalized.Build(zoomOutOnPlus, plusAsPosition);
    CommandBindings unnormalized;
    unnormalized.Build(zoomOutOnPlus);
    KeyPress plusPress = press(Ctrl(At(KeyPosition::NumberRowPlus)));
    plusPress.virtualKey = VK_OEM_PLUS;
    Expect(runs(normalized, plusPress, canvas) == C::kCommandZoomOut &&
               normalized.ChordFor(C::kCommandZoomOut) == Ctrl(At(KeyPosition::NumberRowPlus)) &&
               normalized.ChordFor(C::kCommandZoomIn) == Ctrl(Shift(At(KeyPosition::NumberRowPlus))) &&
               unnormalized.ChordFor(C::kCommandZoomIn) == Ctrl(At(KeyPosition::NumberRowPlus)),
           "bindings.virtual-key-replacement-takes-the-position-default");
    // Repeat: a held M switches the Marquee's shape once; a replacement keeps that rule.
    KeyPress held = press(Key('M'));
    held.repeat = true;
    const KeyState marquee = state(KeyFocus::Canvas, EditorTool::Marquee);
    const CommandBindings toggleOnQ = build({{L"cmd/tool/toggleMarquee", Key('Q')}});
    KeyPress heldQ = press(Key('Q'));
    heldQ.repeat = true;
    Expect(runs(defaults, press(Key('M')), marquee) == C::kCommandToggleMarqueeKind &&
               defaults.Match(held, marquee).result == KeyMatchResult::HeldKey &&
               runs(defaults, press(Key('M')), canvas) == C::SelectToolCommand(EditorTool::Marquee) &&
               runs(defaults, held, canvas) == C::SelectToolCommand(EditorTool::Marquee) &&
               runs(toggleOnQ, press(Key('Q')), marquee) == C::kCommandToggleMarqueeKind &&
               toggleOnQ.Match(heldQ, marquee).result == KeyMatchResult::HeldKey &&
               runs(toggleOnQ, press(Key('M')), marquee) == C::SelectToolCommand(EditorTool::Marquee),
           "bindings.repeat-policy");
    // Decision D4: a text input keeps its keys; only a global command's chord with Ctrl, Alt or a function key runs.
    const CommandBindings saveOnQ = build({{L"cmd/file/save", Key('Q')}});
    const CommandBindings saveOnF5 = build({{L"cmd/file/save", Key(VK_F5)}});
    Expect(runs(defaults, press(Ctrl(Key('Z'))), text) == 0 && runs(defaults, press(Ctrl(Key('A'))), text) == 0 &&
               runs(defaults, press(Ctrl(Key(VK_BACK))), text) == 0 &&
               runs(defaults, press(Ctrl(Key('S'))), text) == C::kCommandSave &&
               runs(defaults, press(Ctrl(Key('N'))), text) == C::kCommandNew &&
               runs(defaults, press(Key(VK_F2)), text) == 0 && runs(saveOnQ, press(Key('Q')), text) == 0 &&
               runs(saveOnQ, press(Key('Q')), chrome) == C::kCommandSave &&
               runs(saveOnF5, press(Key(VK_F5)), text) == C::kCommandSave,
           "bindings.text-input-keeps-its-keys");
    // Canvas commands need the canvas; App commands run from the chrome too.
    Expect(runs(defaults, press(Key('X')), chrome) == 0 &&
               runs(defaults, press(Key('X')), canvas) == C::kCommandSwapPalette &&
               runs(defaults, press(Key(VK_F2)), chrome) == C::kCommandRenameLayer &&
               runs(defaults, press(Ctrl(Key(VK_BACK))), chrome) == C::kCommandFillBackground,
           "bindings.scopes");
    // Conditions share chords: the arrows, Ctrl-arrows, brackets and Alt+Backspace.
    Expect(runs(defaults, press(Key(VK_LEFT)), state(KeyFocus::Canvas, EditorTool::Marquee, true)) ==
                   C::kCommandNudgeSelectionLeft &&
               runs(defaults, press(Key(VK_LEFT)), state(KeyFocus::Canvas, EditorTool::Marquee, false)) == 0 &&
               runs(defaults, press(Key(VK_LEFT)), state(KeyFocus::Canvas, EditorTool::Marquee, true, true)) == 0 &&
               runs(defaults, press(Key(VK_LEFT)), state(KeyFocus::Canvas, EditorTool::Brush, true)) == 0 &&
               runs(defaults, press(Ctrl(Key(VK_LEFT))), state(KeyFocus::Chrome, EditorTool::Brush, true)) ==
                   C::kCommandNudgePixelsLeft &&
               runs(defaults, press(Ctrl(Key(VK_LEFT))), state(KeyFocus::Canvas, EditorTool::Brush, true, true)) == 0 &&
               runs(defaults, press(At(KeyPosition::BracketLeft)), state(KeyFocus::Canvas, EditorTool::Eraser)) ==
                   C::kCommandBrushSmaller &&
               runs(defaults, press(At(KeyPosition::BracketLeft)), canvas) == 0 &&
               runs(defaults, press(Ctrl(At(KeyPosition::BracketLeft))), chrome) == C::kCommandMoveLayerDown &&
               runs(defaults, press(Alt(Key(VK_BACK))), canvas) == C::kCommandFillForeground &&
               runs(defaults, press(Alt(Key(VK_BACK))), state(KeyFocus::Canvas, EditorTool::Lasso, true, true)) == 0,
           "bindings.conditions");
    // A position chord follows the scan code, whatever virtual key the layout gives that key.
    KeyPress plusKey = press(Ctrl(At(KeyPosition::NumberRowPlus)));
    plusKey.virtualKey = VK_OEM_PLUS;
    KeyPress elsewhere = press(Ctrl(Key(VK_OEM_PLUS)));
    Expect(runs(defaults, plusKey, canvas) == C::kCommandZoomIn && runs(defaults, elsewhere, canvas) == 0,
           "bindings.position-keys-follow-the-scan-code");
}
} // namespace

// Ported from legacy TransformTests: hover regions and document mapping of the Move tool's box.
void TestTransformOverlayGeometry() noexcept
{
    const auto closeTo = [](Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 0.0001 && std::abs(a.y - b.y) < 0.0001; };
    CanvasViewport viewport;
    const Vec2 size{1000, 800};
    viewport.Resize(Vec2{1000, 800}, 1.0, size);
    viewport.SetZoom(1.0, viewport.Center(), size);
    LayerTransform transform;
    transform.origin = Vec2{100, 100};
    transform.size = Vec2{400, 300};
    transform.rotation = 37;
    const TransformOverlayGeometry geometry = TransformOverlayGeometry::FromTransform(transform, viewport, size);
    bool edges = true;
    constexpr std::array<std::array<int, 3>, 4> kEdges{{{0, 2, 1}, {2, 4, 3}, {4, 6, 5}, {6, 0, 7}}};
    for (const std::array<int, 3>& edge : kEdges)
    {
        const Vec2 a = geometry.handles[static_cast<size_t>(edge[0])];
        const Vec2 b = geometry.handles[static_cast<size_t>(edge[1])];
        const std::optional<TransformHit> hit = geometry.Hit(Vec2{a.x * 0.75 + b.x * 0.25, a.y * 0.75 + b.y * 0.25});
        edges = edges && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == edge[2];
    }
    Expect(edges, "overlay.rotated-edges-hit-their-handle");
    bool corners = true;
    for (const int index : {0, 2, 4, 6})
    {
        const std::optional<TransformHit> hit = geometry.Hit(geometry.handles[static_cast<size_t>(index)]);
        corners = corners && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == index;
    }
    Expect(corners, "overlay.corners-hit");
    const std::optional<TransformHit> rotate = geometry.Hit(geometry.rotationHandle);
    Expect(rotate.has_value() && rotate->mode == TransformDrag::Mode::Rotate, "overlay.rotation-knob-hit");
    Expect(!geometry.Hit(viewport.ViewFromDocument(transform.Center(), size)).has_value(), "overlay.center-misses");

    CanvasViewport zoomed;
    zoomed.Resize(Vec2{900, 600}, 2.0, size);
    zoomed.SetZoom(1.5, Vec2{300, 200}, size);
    zoomed.Translate(Vec2{57, -30});
    LayerTransform turned;
    turned.origin = Vec2{100, 200};
    turned.size = Vec2{100, 50};
    turned.rotation = 90;
    const TransformOverlayGeometry mapped = TransformOverlayGeometry::FromTransform(turned, zoomed, size);
    Expect(closeTo(zoomed.DocumentFromView(mapped.handles[4], size), turned.Point(Vec2{1, 1})),
           "overlay.document-mapping");
    Expect(turned.Contains(turned.Center()) && turned.Contains(Vec2{150, 265}) && !turned.Contains(Vec2{190, 225}),
           "overlay.rotated-contains");
    bool handles = true;
    for (int index = 0; index < 8; ++index)
    {
        const std::optional<TransformHit> hit = mapped.Hit(mapped.handles[static_cast<size_t>(index)]);
        handles = handles && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == index;
    }
    Expect(handles, "overlay.zoomed-handles-hit");
    Expect(mapped.Hit(mapped.rotationHandle).has_value() &&
               mapped.Hit(mapped.rotationHandle)->mode == TransformDrag::Mode::Rotate,
           "overlay.zoomed-rotation-hit");

    LayerTransform upright;
    upright.size = Vec2{100, 100};
    const TransformOverlayGeometry axisAligned = TransformOverlayGeometry::FromTransform(upright, viewport, size);
    Expect(axisAligned.ResizeCursorDirection(3) == 0 && axisAligned.ResizeCursorDirection(1) == 2 &&
               axisAligned.ResizeCursorDirection(4) == 1 && axisAligned.ResizeCursorDirection(2) == 3,
           "overlay.resize-cursor-directions");
    const std::array<Vec2, 4> shape{Vec2{10, 10}, Vec2{60, 10}, Vec2{30, 30}, Vec2{10, 30}};
    const TransformOverlayGeometry distorted = TransformOverlayGeometry::FromCorners(shape, viewport, size);
    Expect(!distorted.showsRotation && closeTo(distorted.handles[3], viewport.ViewFromDocument(Vec2{45, 20}, size)),
           "overlay.distortion-midpoints");
}

// ── Crash dumps (C14) ─────────────────────────────────────────

void TestCrashDumps()
{
    Expect(CrashDumps::DefaultDirectory().ends_with(L"\\RedPrism\\Crashes"), "crash.default-directory");
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error) /
                                       (L"RedPrism.HostTests.crash-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    bool created = true;
    for (int index = 10; index < 22; ++index)
    {
        const std::wstring name = L"RedPrism-20260101-0000" + std::to_wstring(index) + L"-1.dmp";
        created = SettingsStore::WriteFileAtomic((root / name).wstring(), "x") && created;
    }
    Expect(created && CrashDumps::PruneDumps(root.wstring(), 10) == 2 &&
               !std::filesystem::exists(root / L"RedPrism-20260101-000010-1.dmp", error) &&
               !std::filesystem::exists(root / L"RedPrism-20260101-000011-1.dmp", error) &&
               std::filesystem::exists(root / L"RedPrism-20260101-000012-1.dmp", error) &&
               std::filesystem::exists(root / L"RedPrism-20260101-000021-1.dmp", error) &&
               CrashDumps::PruneDumps(root.wstring(), 10) == 0,
           "crash.prune-keeps-newest");
    wchar_t path[CrashDumps::kPathCapacity] = {};
    const bool written = CrashDumps::WriteMiniDump(root.c_str(), nullptr, path, CrashDumps::kPathCapacity);
    const std::wstring dump = path;
    Expect(written && dump.starts_with(root.wstring() + L"\\RedPrism-") && dump.ends_with(L".dmp") &&
               std::filesystem::file_size(dump, error) > 0,
           "crash.dump-written");
    Expect(CrashDumps::RecordPending(root.c_str(), path) && CrashDumps::TakePending(root.wstring()) == dump &&
               !CrashDumps::TakePending(root.wstring()).has_value(),
           "crash.pending-round-trip");
    Expect(!CrashDumps::WriteMiniDump(L"", nullptr, path, CrashDumps::kPathCapacity) && path[0] == 0,
           "crash.dump-needs-a-directory");
    std::filesystem::remove_all(root, error);
}

// ── Localization (C14) ────────────────────────────────────────

void TestLocalization()
{
    const std::vector<std::wstring> system{L"fr-FR", L"fr-FR", L"it-IT"};
    const std::vector<std::wstring> candidates = Localization::SatelliteCandidates(L"C:\\App\\", L"de-AT", system);
    const std::vector<std::wstring> expected{
        L"C:\\App\\Languages\\de-AT\\RedPrism.resources.dll", L"C:\\App\\Languages\\de\\RedPrism.resources.dll",
        L"C:\\App\\Languages\\fr-FR\\RedPrism.resources.dll", L"C:\\App\\Languages\\fr\\RedPrism.resources.dll",
        L"C:\\App\\Languages\\it-IT\\RedPrism.resources.dll", L"C:\\App\\Languages\\it\\RedPrism.resources.dll"};
    Expect(candidates == expected, "locale.candidates-in-order");
    Expect(Localization::SatelliteCandidates(L"C:\\App", L"", {}).empty() &&
               Localization::SatelliteCandidates(L"C:\\App", L"fr", {L"fr"}).size() == 1,
           "locale.candidates-dedupe");
    // The embedded table is English: `language: "en"` keeps English over a satellite for the display language, and
    // an English display language ahead of another one keeps it too (a regional English table may still load).
    const std::vector<std::wstring> english{L"C:\\App\\Languages\\en\\RedPrism.resources.dll"};
    const std::vector<std::wstring> britishFirst{L"C:\\App\\Languages\\en-GB\\RedPrism.resources.dll",
                                                 L"C:\\App\\Languages\\en\\RedPrism.resources.dll"};
    Expect(Localization::SatelliteCandidates(L"C:\\App", L"en", {L"fr-FR"}) == english &&
               Localization::SatelliteCandidates(L"C:\\App", L"EN", {L"fr-FR"}).size() == 1 &&
               Localization::SatelliteCandidates(L"C:\\App", L"", {L"en-GB", L"fr-FR"}) == britishFirst &&
               Localization::SatelliteCandidates(L"C:\\App", L"de", {L"en-US", L"fr-FR"}).size() == 3,
           "locale.english-ends-the-candidates");
    Expect(Localization::LoadSatelliteModule({L"C:\\App\\Languages\\zz\\RedPrism.resources.dll"}) == nullptr,
           "locale.missing-satellite");
    Expect(!Localization::SystemUiLanguages().empty() && !Localization::ExecutableDirectory().empty(),
           "locale.system-languages");
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    const std::size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1)) + L"RedPrism.exe";
    const wil::unique_hmodule exe{
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    if (!exe)
    {
        Expect(false, "locale.load-redprism-exe");
        return;
    }
    // The satellite answers nullptr-module loads (HostTests.exe has no table of its own); an explicit module ignores
    // it; without a satellite the process's own (empty) table answers.
    SetStringResourceSatellite(exe.get());
    const std::wstring title = LoadStringResource(nullptr, IDS_APP_TITLE);
    SetStringResourceSatellite(GetModuleHandleW(nullptr));
    const std::wstring explicitModule = LoadStringResource(exe.get(), IDS_APP_TITLE);
    const std::wstring emptySatellite = LoadStringResource(nullptr, IDS_APP_TITLE);
    SetStringResourceSatellite(nullptr);
    Expect(!title.empty() && title == explicitModule && emptySatellite.empty() &&
               LoadStringResource(nullptr, IDS_APP_TITLE).empty(),
           "locale.satellite-override");
}

// Help › About reads the running module's own VS_VERSIONINFO (RF-704): the stamped build number, not the
// compile-time "0.1.0.0". RedPrism.exe carries the resource; HostTests.exe does not, so About's fallback shows.
void TestVersionInfo()
{
    Expect(RedPrism::FormatFileVersion(0, 1, 42, 0) == L"0.1.42.0" &&
               RedPrism::FormatFileVersion(65535, 0, 7, 3) == L"65535.0.7.3",
           "version.format");
    std::wstring formatted = L"untouched";
    Expect(!RedPrism::TryReadModuleFileVersion(nullptr, formatted) && formatted == L"untouched", "version.null-path");
    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    Expect(length > 0 && length < MAX_PATH && !RedPrism::TryReadModuleFileVersion(module, formatted) &&
               formatted == L"untouched" && RedPrism::FormatRunningModuleVersion(L"9.8.7.6") == L"9.8.7.6",
           "version.no-resource-falls-back");
    std::wstring exe(module, length);
    const std::size_t slash = exe.find_last_of(L"\\/");
    exe = (slash == std::wstring::npos ? std::wstring{} : exe.substr(0, slash + 1)) + L"RedPrism.exe";
    const bool read = RedPrism::TryReadModuleFileVersion(exe.c_str(), formatted);
    // Major and minor come from Common/Version.h; the build field is whatever ResourceCompile stamped; revision 0.
    const std::wstring prefix =
        std::to_wstring(REDPRISM_VERSION_MAJOR) + L"." + std::to_wstring(REDPRISM_VERSION_MINOR) + L".";
    std::size_t fields = 0;
    bool digits = !formatted.empty();
    for (const wchar_t character : formatted)
    {
        fields += character == L'.' ? 1u : 0u;
        digits = digits && (character == L'.' || (character >= L'0' && character <= L'9'));
    }
    if (!read || !formatted.starts_with(prefix) || !formatted.ends_with(L".0") || fields != 3 || !digits)
    {
        std::printf("     RedPrism.exe version '%ls' (read %d)\n", formatted.c_str(), read ? 1 : 0);
    }
    Expect(read && formatted.starts_with(prefix) && formatted.ends_with(L".0") && fields == 3 && digits,
           "version.redprism-exe");
}

int wmain(int argc, wchar_t** argv)
{
    // Before anything can fail a check: a CRT report ends the run with exit code 3 and no dialog (RF-702,
    // Tests/Support/FailureReports.h). The hidden switch below proves it.
    RedPrismTestFailureReports::RouteAwayFromDialogs();
    if (argc == 2 && std::wstring_view{argv[1]} == L"--failure-report-self-test")
    {
        return RedPrismTestFailureReports::RunSelfTest();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--asan-probe")
    {
        return RunAddressSanitizerProbe();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--bench-executor")
    {
        return RunWorkerPoolBenchmark();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--bench-jobs")
    {
        return RunJobBenchmark();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--bench-memory")
    {
        return RunMemoryBenchmark();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--bench-ui-timing")
    {
        return RunUiTimingBenchmark();
    }
    if (argc > 1 && std::wstring_view{argv[1]} == L"--bench-filter-cancel")
    {
        return RunFilterWorkBenchmark();
    }

    const bool stress = argc > 1 && std::wstring_view{argv[1]} == L"--stress";
    gFailures += RunExecutorValueTests();
    gFailures += RunKernelDispatchTests();
    gFailures += RunKernelCliTests();
    gFailures += RunBenchmarkMachineTests();
    gFailures += RunUiTimingHistogramTests();
    gFailures += RunUiTimingTests();
    gFailures += RunKeyboardStateScopeTests();
    gFailures += RunJobExecutionObservationTests();
    gFailures += RunUiCancellationCaptureTests();
    gFailures += static_cast<int>(!RunUiCancellationBodyTests());
    gFailures += RunUiCancellationObservationTransportTests();
    gFailures += RunUiCancellationCliTests();
    gFailures += static_cast<int>(!RunUiCancellationRefusalTests());
    gFailures += RunHostUiFrameLatencyTests();
    gFailures += RunWorkerPoolTests(stress);
    gFailures += RunJobTests(stress);
    gFailures += RunJobStageTests();
    gFailures += RunOwnerWorkMultiplexerTests();
    gFailures += RunHostPathClassificationReadTests();
    gFailures += RunPathInputTransferTests();
    gFailures += RunHostPathReadWaitCycleExhaustionTests();
    gFailures += RunRetryWaitQueueTests();
    gFailures += RunRetryWaitCancellationTests();
    gFailures += RunJobUiPolicyTests();
    gFailures += RunWorkerWriteCompletionChecks();
    gFailures += RunJobActivityTests();
    gFailures += RunMemoryAdmissionTests();
    gFailures += RunDeferredReleaseTests();
    gFailures += RunPerfRootTests();
    gFailures += RunPipelineTraceTests();
    gFailures += RunJobMemoryTests();
    gFailures += RunRetainedJobMemoryTests();
    gFailures += RunSharedRetainedJobMemoryTests();
    gFailures += RunCompletedPreviewMemoryTests();
    gFailures += RunRetirementMemoryTests();
    gFailures += RunPreparedRasterPreviewLifetimeTests();
    gFailures += RunWarpFactoryLifetimeTests();
    gFailures += RunWarpPreviewLifetimeTests();
    gFailures += RunJobShutdownRetirementTests();
    gFailures += RunJobCloseHookTests();
    gFailures += RunJobSuspendedRetirementTests();
    gFailures += RunHostWorkerStageRetirementTests();
    gFailures += RunGpuRecordingLaneTests();
    gFailures += RunExecutionLaneStagesTests();
    gFailures += RunReservationGrowthTests();
    gFailures += RunRetainedGrowthTests();
    gFailures += RunRetainedSlotAdmissionTests();
    gFailures += RunPreservedContinuationTests();
    gFailures += RunJobRetirementTests();
    gFailures += RunPixelRecycleTests();
    gFailures += RunPreviewMailboxTests();
    gFailures += RunContinuationQueueTests();
    gFailures += RunContinuationAdjacentPreviewTests();
    gFailures += RunContinuationPendingViewTests();
    gFailures += RunOpacityInputOrderTests();
    gFailures += RunPointerInputStreamTests();
    gFailures += RunHostPointerInputStreamPeekTests();
    gFailures += RunSamplerInputTests();
    gFailures += RunSamplerIntentInputTests();
    gFailures += RunContinuationReceiptTests();
    gFailures += RunContinuationRequestTests();
    gFailures += RunHostPathReadTests();
    gFailures += RunHistoryWorkHostTests();
    TestAdapterSelection();
    TestFrameScheduler();
    TestCommandLine();
    TestEditorTools();
    TestKeyChords();
    TestCommandCatalog();
    TestCommandBindings();
    TestTransformOverlayGeometry();
    TestRailGlyphsExistInFluentFont();
    TestStringTable();
    TestCrashDumps();
    TestLocalization();
    TestVersionInfo();
    gFailures += RunPublicationCallbackTests();
    gFailures += RunPublicationRetentionTests();
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All host tests passed.\n");
    return 0;
}

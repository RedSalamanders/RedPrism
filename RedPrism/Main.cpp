#include "Application.Tests.RowPublication.h"
#include "Application.h"
#include "CommandLine.h"
#include "CrashDumps.h"
#include "Kernels.h"
#include "Localization.h"
#include "Resources.h"
#include "SettingsStore.h"
#include "WorkerPool.h"
#include "resource.h"

#include <atomic>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <shellapi.h>
#include <span>
#include <string>
#include <string_view>
#include <windows.h>

#include <wil/resource.h>

namespace
{
#ifdef _DEBUG
constexpr bool kDebugBuild = true;
#else
constexpr bool kDebugBuild = false;
#endif

// A per-run folder under the system temp directory for harness-mode crash dumps (--self-test, --screenshot, a
// --demo, --bench): never the user's real %LocalAppData%\RedPrism\Crashes, so a crashing harness run cannot leave
// a pending.txt that trips the "crashed last time" prompt on the next ordinary launch (RF-708,
// Specs/Core/Core_CrashHandling.md). Falls back to the ordinary default directory if the temp path is unavailable.
[[nodiscard]] std::wstring HarnessCrashDirectory()
{
    wchar_t tempPath[MAX_PATH]{};
    const DWORD tempLength = GetTempPathW(static_cast<DWORD>(MAX_PATH), tempPath);
    if (tempLength == 0 || tempLength >= MAX_PATH)
    {
        return CrashDumps::DefaultDirectory();
    }
    return std::wstring{tempPath, tempLength} + L"RedPrism.Harness\\" + std::to_wstring(GetCurrentProcessId());
}

#if defined(_DEBUG)
// Installed only for harness runs. A Debug assertion or debug-heap error report has no one to click a dialog, and
// the CRT's own default handling does not always abort by itself on a report (RF-702,
// Specs/Testing/Testing_Validation.md): this hook writes the report to stderr itself and ends the process right
// away instead of letting an untrustworthy heap wander on into a later, unrelated failure.
int __cdecl HarnessCrtReportHook(int reportType, char* message, int* returnValue) noexcept
{
    if (reportType != _CRT_ERROR && reportType != _CRT_ASSERT)
    {
        return 0;
    }
    if (message)
    {
        std::fputs(message, stderr);
        std::fflush(stderr);
    }
    if (returnValue)
    {
        *returnValue = 0;
    }
    std::_Exit(3);
}
#endif

void EmitCommandLineText(const std::wstring& text, bool error, bool quiet) noexcept
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    bool attached = false;
    if (!output || output == INVALID_HANDLE_VALUE)
    {
        attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        output = attached ? GetStdHandle(STD_OUTPUT_HANDLE) : nullptr;
    }
    if (output && output != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(output, &mode))
        {
            const std::wstring console = L"\n" + text;
            DWORD written = 0;
            (void)WriteConsoleW(output, console.c_str(), static_cast<DWORD>(console.size()), &written, nullptr);
        }
        else
        {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                                  nullptr, nullptr);
            std::string utf8(static_cast<size_t>(bytes > 0 ? bytes : 0), '\0');
            if (bytes > 0)
            {
                (void)WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes,
                                          nullptr, nullptr);
                DWORD written = 0;
                (void)WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            }
        }
        if (attached)
        {
            FreeConsole();
        }
        return;
    }
    if (quiet)
    {
        OutputDebugStringW(text.c_str());
        return;
    }
    const std::wstring caption = LoadStringResource(nullptr, IDS_COMMAND_LINE_CAPTION);
    MessageBoxW(nullptr, text.c_str(), caption.c_str(), MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

int RunWorkerCrashProbe() noexcept
{
    struct Probe final
    {
        std::atomic<unsigned int> started{0};
        wil::unique_handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    } probe;
    if (!probe.ready)
    {
        return 1;
    }
    WorkerPool pool;
    const HRESULT started = pool.Initialize(
        2,
        [](void* context, WorkPriority, CancelToken) noexcept
        {
            auto& state = *static_cast<Probe*>(context);
            const unsigned int index = state.started.fetch_add(1);
            if (index == 1)
            {
                (void)SetEvent(state.ready.get());
            }
            if (WaitForSingleObject(state.ready.get(), 5000) != WAIT_OBJECT_0)
            {
                ExitProcess(1);
            }
            if (index == 0)
            {
                std::terminate(); // verifies the per-worker UCRT handler as well as competing faults
            }
            CrashDumps::RaiseProbeCrash();
        },
        &probe);
    if (FAILED(started) || !pool.Wake(WorkPriority::High) || !pool.Wake(WorkPriority::High))
    {
        return 1;
    }
    // No teardown: the child exists only to fault. A missing worker/fault fails without leaving a dump.
    (void)WaitForSingleObject(GetCurrentProcess(), 15000);
    ExitProcess(1);
}

int RunApplication(HINSTANCE instance, int showCommand) noexcept
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argumentCount = 0;
    wil::unique_hlocal_ptr<wchar_t*[]> arguments{CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
    if (!arguments)
    {
        return 1;
    }

    const RedPrismCommandLineScan commandLineScan = RedPrismScanCommandLine(arguments.get(), argumentCount);
    const bool selfTest = commandLineScan.selfTest;
    if (commandLineScan.help)
    {
        EmitCommandLineText(RedPrismFormatCommandLineHelp(), false, selfTest);
        return 0;
    }
    if (const wchar_t* unknown = RedPrismFindUnknownArgument(arguments.get(), argumentCount))
    {
        EmitCommandLineText(FormatStringResource(nullptr, IDS_ERROR_UNKNOWN_ARGUMENT, std::wstring_view{unknown}) +
                                L"\n",
                            true, selfTest);
        return 2;
    }

    RedPrismCommandLine parsed{};
    const wchar_t* errorToken = nullptr;
    if (!RedPrismParseCommandLine(arguments.get(), argumentCount, parsed, &errorToken))
    {
        EmitCommandLineText(FormatStringResource(nullptr, IDS_ERROR_UNKNOWN_ARGUMENT,
                                                 std::wstring_view{errorToken ? errorToken : L""}) +
                                L"\n",
                            true, selfTest);
        return 2;
    }

    // Settings, the language, and the user's real crash folder belong to ordinary runs: every harness switch
    // (self-test, a screenshot, a demo, a benchmark) keeps the defaults (dark chrome, English) and its own crash
    // dumps out of the user's directories (RF-708).
    const bool harness =
        parsed.selfTest || parsed.bench != nullptr || parsed.screenshotPath != nullptr || parsed.demo != nullptr;

    if (harness || parsed.crashProbeDirectory != nullptr)
    {
        // No one answers a dialog on a harness run: neither Windows Error Reporting's for a fast-fail that bypasses the
        // crash handler below, nor a critical-error box (an unreadable drive). The run ends instead, and the log keeps
        // its exit code (Specs/Testing/Testing_Validation.md).
        (void)SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }

    // Crash handling comes first so every later failure leaves a dump (Specs/Core/Core_CrashHandling.md); the
    // probe raises a fault through it and never reaches the window.
    const std::wstring crashDirectory = parsed.crashProbeDirectory != nullptr ? std::wstring{parsed.crashProbeDirectory}
                                        : harness                             ? HarnessCrashDirectory()
                                                                              : CrashDumps::DefaultDirectory();
    if (FAILED(CrashDumps::InstallCrashHandler(crashDirectory.c_str(), parsed.crashProbeWorker)))
    {
        return 1;
    }
    const KernelSelection kernelSelection = InitializeKernels(parsed.isa);
    if (kernelSelection != KernelSelection::Selected)
    {
        const UINT messageId = kernelSelection == KernelSelection::UnsupportedHardware
                                   ? IDS_ERROR_KERNEL_ISA_UNSUPPORTED
                                   : IDS_ERROR_KERNEL_ISA_NOT_IMPLEMENTED;
        const bool quiet = harness || parsed.crashProbeDirectory != nullptr;
        EmitCommandLineText(FormatStringResource(nullptr, messageId, KernelIsaName(parsed.isa)) + L"\n", true, quiet);
        return 2;
    }
    if (parsed.crashProbeDirectory != nullptr)
    {
        if (parsed.crashProbeWorker)
        {
            return RunWorkerCrashProbe();
        }
        CrashDumps::RaiseProbeCrash();
    }

#if defined(_DEBUG)
    if (harness)
    {
        // No one can click a dialog on a harness run, and the CRT's own default handling does not always abort by
        // itself on a report (RF-702): install the hook that ends the process the moment one happens.
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &HarnessCrtReportHook);
    }
#endif

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        return 1;
    }
    // Declared before Application: its explicit Shutdown and destruction precede CoUninitialize (RF-710).
    // Armed only when this call actually initialized COM
    // (RPC_E_CHANGED_MODE means something else already did, and only that owner may uninitialize it).
    wil::unique_couninitialize_call comScope{SUCCEEDED(com)};

    const bool forceWarp = parsed.warp || parsed.selfTest;
    if (parsed.selfTest)
    {
        (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::printf("BEGIN self-test.RunStandaloneSelectionOutlineGpuHostSelfTest\n");
        if (const int failed = Application::RunStandaloneSelectionOutlineGpuHostSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneLassoDraftDisplaySelfTest\n");
        if (const int failed = Application::RunStandaloneLassoDraftDisplaySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneSelectionPressGestureSelfTest\n");
        if (const int failed = Application::RunStandaloneSelectionPressGestureSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneRectanglePressSelfTest\n");
        if (const int failed = Application::RunStandaloneRectanglePressSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneEllipsePressSelfTest\n");
        if (const int failed = Application::RunStandaloneEllipsePressSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneClipboardContentionSelfTest\n");
        if (const int failed = Application::RunStandaloneClipboardContentionSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneMarqueeModesSelfTest\n");
        if (const int failed = Application::RunStandaloneMarqueeModesSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneSelectionCursorSelfTest\n");
        if (const int failed = Application::RunStandaloneSelectionCursorSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneSelectionPressShutdownSelfTest\n");
        if (const int failed = Application::RunStandaloneSelectionPressShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneLassoHostInputSelfTest\n");
        if (const int failed = Application::RunStandaloneLassoHostInputSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneSelectionOutlineParitySelfTest\n");
        const int parity = Application::RunStandaloneSelectionOutlineParitySelfTest(instance);
        if (parity != 0)
            return parity;
        std::printf("BEGIN self-test.RunStandaloneSelectionOutlineOwnerCapacitySelfTest\n");
        if (const int failed = Application::RunStandaloneSelectionOutlineOwnerCapacitySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneClipboardMergedShutdownSelfTest\n");
        if (const int failed = Application::RunStandaloneClipboardMergedShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneMergeReadbackSelfTest\n");
        if (const int failed = Application::RunStandaloneMergeReadbackSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneCanvasLifetimeShutdownSelfTest\n");
        if (const int failed = Application::RunStandaloneCanvasLifetimeShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneCloneReadbackShutdownSelfTest\n");
        if (const int failed = Application::RunStandaloneCloneReadbackShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneCloneLayerSourceShutdownSelfTest\n");
        if (const int failed = Application::RunStandaloneCloneLayerSourceShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsGroupSelectionSelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsGroupSelectionSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsBoundarySelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsBoundarySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsInteractionSelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsInteractionSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsRetrySelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsRetrySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsEditSelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsEditSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsIdentitySelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsIdentitySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsAccessibilitySelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsAccessibilitySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsCapacitySelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsCapacitySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePreparedLayerRowsNotificationSelfTest\n");
        if (const int failed = Application::RunStandalonePreparedLayerRowsNotificationSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneResponsiveStartupSelfTest\n");
        if (const int failed = Application::RunStandaloneResponsiveStartupSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneResponsiveSelectionStartupSelfTest\n");
        if (const int failed = Application::RunStandaloneResponsiveSelectionStartupSelfTest(instance); failed != 0)
            return failed;
        // This fixture owns a scheduler/TLS retirement sink and must precede the main Application owner.
        std::printf("BEGIN self-test.RunStandalonePanelWandFollowthroughSelfTest\n");
        if (const int failed = Application::RunStandalonePanelWandFollowthroughSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneScalarSelectionClosureSelfTest\n");
        if (const int failed = Application::RunStandaloneScalarSelectionClosureSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneHistoryResidencyStartupSelfTest\n");
        if (const int failed = Application::RunStandaloneHistoryResidencyStartupSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePathReadContinuationSelfTest\n");
        if (const int failed = Application::RunStandalonePathReadContinuationSelfTest(instance); failed != 0)
            return failed;
        if (const int failed = Application::RunStandaloneProcessingSelfTest(instance); failed != 0)
            return failed;
        if (const int failed = Application::RunStandaloneLeaseShutdownSelfTest(instance); failed != 0)
            return failed;
        if (const int failed = Application::RunStandaloneLiveShutdownSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePendingDisplaySelfTest\n");
        if (const int failed = Application::RunStandalonePendingDisplaySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePendingRasterDisplaySelfTest\n");
        if (const int failed = Application::RunStandalonePendingRasterDisplaySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandalonePendingDisplayTransportSelfTest\n");
        if (const int failed = Application::RunStandalonePendingDisplayTransportSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneCommittedDisplaySelfTest\n");
        if (const int failed = Application::RunStandaloneCommittedDisplaySelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneUiCancellationCanvasSelfTest\n");
        if (const int failed = Application::RunStandaloneUiCancellationCanvasSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunPreparedRowPublicationTests\n");
        if (const int failed = RunPreparedRowPublicationTests(); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeFinishSelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeFinishSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneWarpFinishSelfTest\n");
        if (const int failed = Application::RunStandaloneWarpFinishSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveHostSelfTest\n");
        if (const int failed = Application::RunWarpLiveHostSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveFaultSelfTest\n");
        if (const int failed = Application::RunWarpLiveFaultSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveInputSelfTest\n");
        if (const int failed = Application::RunWarpLiveInputSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveTargetsSelfTest\n");
        if (const int failed = Application::RunWarpLiveTargetsSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveAdmissionSelfTest\n");
        if (const int failed = Application::RunWarpLiveAdmissionSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunWarpLiveStaleSelfTest\n");
        if (const int failed = Application::RunWarpLiveStaleSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeSampleSelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeSampleSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeSampleFaultSelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeSampleFaultSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeSampleInputSelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeSampleInputSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeSampleAdmissionSelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeSampleAdmissionSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunStandaloneStrokeSampleParitySelfTest\n");
        if (const int failed = Application::RunStandaloneStrokeSampleParitySelfTest(instance); failed != 0)
            return failed;
        if (const int failed = Application::RunStandaloneShutdownSelfTest(instance); failed != 0)
            return failed;
        // These fixtures own processing and an HWND. Run before the main owner installs its TLS sink
        // and registers the shared window class; each fixture joins before the next owner starts.
        std::printf("BEGIN self-test.RunSelectionOutlineTimingSelfTest\n");
        if (const int failed = Application::RunSelectionOutlineTimingSelfTest(instance); failed != 0)
            return failed;
        std::printf("BEGIN self-test.RunOpacityClosureGuardSelfTest\n");
        if (const int failed = Application::RunOpacityClosureGuardSelfTest(instance); failed != 0)
            return failed;
    }
    const std::unique_ptr<Application> application{new (std::nothrow) Application(
        instance, forceWarp, parsed.screenshotPath, parsed.screenshotDpi, parsed.demo, parsed.bench)};
    if (!application)
    {
        return 1;
    }

    AppSettings settings;
    std::wstring settingsPath;
    wil::unique_hmodule satellite;
    const auto shutdown = wil::scope_exit(
        [&]() noexcept
        {
            application->Shutdown();
            SetStringResourceSatellite(nullptr);
        });
    if (harness)
    {
        settings.theme = ThemePreference::Dark;
    }
    else
    {
        settingsPath = SettingsStore::DefaultPath(kDebugBuild);
        if (!settingsPath.empty())
        {
            const SettingsLoadResult loaded = SettingsStore::LoadCold(settingsPath);
            settings = loaded.settings;
            if (loaded.status == SettingsLoadStatus::Invalid)
            {
                OutputDebugStringW(
                    L"The settings file was not a settings document: backed up and replaced by the template.\n");
            }
        }
        satellite.reset(Localization::LoadSatelliteModule(Localization::SatelliteCandidates(
            Localization::ExecutableDirectory(), settings.language, Localization::SystemUiLanguages())));
        SetStringResourceSatellite(satellite.get());
        // A crash last time: say where the report went (nothing is uploaded) and offer its folder.
        if (const std::optional<std::wstring> pending = CrashDumps::TakePending(crashDirectory); pending.has_value())
        {
            const std::wstring message = FormatStringResource(nullptr, IDS_CRASH_PROMPT, pending.value());
            const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
            if (MessageBoxW(nullptr, message.c_str(), caption.c_str(), MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                const std::wstring select = L"/select,\"" + pending.value() + L"\"";
                (void)ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
            }
        }
        (void)CrashDumps::PruneDumps(crashDirectory, CrashDumps::kKeepNewest);
    }
    application->UseSettings(std::move(settingsPath), settings);

    const HRESULT processing = application->InitializeProcessing();
    int exitCode = 1;
    if (SUCCEEDED(processing))
    {
        const std::span<const std::wstring_view> inputPaths{parsed.inputPaths.data(), parsed.inputPathCount};
        if (parsed.inputPathCount != 0 && !application->OpenCommandLinePaths(inputPaths))
        {
            OutputDebugStringW(L"Command-line paths could not be queued.\n");
        }
        else if (parsed.selfTest)
        {
            exitCode = application->RunSelfTest();
        }
        else if (parsed.bench != nullptr)
        {
            exitCode = application->RunBenchmark();
        }
        else
        {
            exitCode = application->Run(showCommand);
        }
    }
    // All modes, including early self-test/bench failures, join before UIA/device/satellite/STA teardown.
    application->Shutdown();
    if (exitCode != 0 && !parsed.selfTest && parsed.bench == nullptr && parsed.screenshotPath == nullptr)
    {
        UINT messageId = IDS_ERROR_START_GENERIC;
        switch (exitCode)
        {
        case 2:
            messageId = IDS_ERROR_START_WINDOW;
            break;
        case 5:
            messageId = IDS_ERROR_START_GRAPHICS;
            break;
        default:
            break;
        }
        const std::wstring message = LoadStringResource(nullptr, messageId);
        const std::wstring caption = LoadStringResource(nullptr, IDS_APP_TITLE);
        MessageBoxW(nullptr, message.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
    }
    return exitCode;
}
} // namespace

_Use_decl_annotations_ int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    return RunApplication(instance, showCommand);
}

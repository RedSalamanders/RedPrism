#include "Application.h"
#include "CommandLine.h"
#include "CrashDumps.h"
#include "Localization.h"
#include "Resources.h"
#include "SettingsStore.h"
#include "resource.h"

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <shellapi.h>
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

bool HasArgument(wchar_t* const* arguments, int argumentCount, std::wstring_view expected) noexcept
{
    for (int index = 1; index < argumentCount; ++index)
    {
        if (expected == arguments[index])
        {
            return true;
        }
    }
    return false;
}

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

int RunApplication(HINSTANCE instance, int showCommand) noexcept
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argumentCount = 0;
    wil::unique_hlocal_ptr<wchar_t*[]> arguments{CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
    if (!arguments)
    {
        return 1;
    }

    const bool selfTest = HasArgument(arguments.get(), argumentCount, RedPrismSwitchName(RedPrismSwitch::SelfTest));
    for (int index = 1; index < argumentCount; ++index)
    {
        if (RedPrismIsHelpArgument(arguments.get()[index]))
        {
            EmitCommandLineText(RedPrismFormatCommandLineHelp(), false, selfTest);
            return 0;
        }
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

    // Crash handling comes first so every later failure leaves a dump (Specs/Core/Core_CrashHandling.md); the
    // probe raises a fault through it and never reaches the window.
    const std::wstring crashDirectory = parsed.crashProbeDirectory != nullptr ? std::wstring{parsed.crashProbeDirectory}
                                        : harness                             ? HarnessCrashDirectory()
                                                                              : CrashDumps::DefaultDirectory();
    CrashDumps::InstallCrashHandler(crashDirectory.c_str());
    if (parsed.crashProbeDirectory != nullptr)
    {
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
    // Declared before Application: reverse destruction order then runs ~Application (UIA disconnect,
    // DestroyWindow) before CoUninitialize, not after (RF-710). Armed only when this call actually initialized COM
    // (RPC_E_CHANGED_MODE means something else already did, and only that owner may uninitialize it).
    wil::unique_couninitialize_call comScope{SUCCEEDED(com)};

    const bool forceWarp = parsed.warp || parsed.selfTest;
    const std::unique_ptr<Application> application{new (std::nothrow) Application(
        instance, forceWarp, parsed.screenshotPath, parsed.screenshotDpi, parsed.demo, parsed.bench)};
    if (!application)
    {
        return 1;
    }

    AppSettings settings;
    std::wstring settingsPath;
    wil::unique_hmodule satellite;
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

    const int exitCode = parsed.selfTest           ? application->RunSelfTest()
                         : parsed.bench != nullptr ? application->RunBenchmark()
                                                   : application->Run(showCommand);
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
    SetStringResourceSatellite(nullptr);
    return exitCode;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    return RunApplication(instance, showCommand);
}

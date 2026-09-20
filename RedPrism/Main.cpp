#include "Application.h"
#include "CommandLine.h"

#include <memory>
#include <new>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <windows.h>

namespace
{
struct LocalFreeDeleter
{
    void operator()(wchar_t** value) const noexcept
    {
        if (value)
        {
            LocalFree(value);
        }
    }
};

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
    MessageBoxW(nullptr, text.c_str(), L"RedPrism command line", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

int RunApplication(HINSTANCE instance, int showCommand) noexcept
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argumentCount = 0;
    std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments{CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
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
        std::wstring message = L"Unknown argument \"";
        message += unknown;
        message += L"\". Run RedPrism.exe --help for the command line.\n";
        EmitCommandLineText(message, true, selfTest);
        return 2;
    }

    RedPrismCommandLine parsed{};
    const wchar_t* errorToken = nullptr;
    if (!RedPrismParseCommandLine(arguments.get(), argumentCount, parsed, &errorToken))
    {
        std::wstring message = L"Unknown argument \"";
        message += errorToken ? errorToken : L"";
        message += L"\". Run RedPrism.exe --help for the command line.\n";
        EmitCommandLineText(message, true, selfTest);
        return 2;
    }

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        return 1;
    }
    const bool uninitializeCom = SUCCEEDED(com);

    const bool forceWarp = parsed.warp || parsed.selfTest;
    const std::unique_ptr<Application> application{
        new (std::nothrow) Application(instance, forceWarp, parsed.screenshotPath, parsed.screenshotDpi)};
    if (!application)
    {
        if (uninitializeCom)
        {
            CoUninitialize();
        }
        return 1;
    }

    const int exitCode = parsed.selfTest ? application->RunSelfTest() : application->Run(showCommand);
    if (exitCode != 0 && !parsed.selfTest && parsed.screenshotPath == nullptr)
    {
        const wchar_t* message = L"RedPrism could not start. See the debugger output.";
        switch (exitCode)
        {
        case 2:
            message = L"RedPrism could not create its window. See the debugger output.";
            break;
        case 5:
            message = L"RedPrism could not complete graphics initialization or rendering. See the debugger output.";
            break;
        default:
            break;
        }
        MessageBoxW(nullptr, message, L"RedPrism", MB_OK | MB_ICONERROR);
    }
    if (uninitializeCom)
    {
        CoUninitialize();
    }
    return exitCode;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    return RunApplication(instance, showCommand);
}

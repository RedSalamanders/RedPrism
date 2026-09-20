#include "AdapterSelection.h"
#include "AddressSanitizerProbe.h"
#include "CommandLine.h"
#include "FrameScheduler.h"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <windows.h>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
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
}

void TestCommandLine() noexcept
{
    Expect(RedPrismIsHelpArgument(L"--help") && RedPrismIsHelpArgument(L"/?"), "cli.help-aliases");
    Expect(RedPrismFindSwitch(L"--warp") != nullptr, "cli.warp");
    Expect(RedPrismFindSwitch(L"--self-test") != nullptr, "cli.self-test");
    Expect(RedPrismFindSwitch(L"--screenshot") != nullptr, "cli.screenshot");
    Expect(RedPrismFindSwitch(L"--no-such") == nullptr, "cli.unknown-switch");

    wchar_t unknown[] = L"--no-such";
    wchar_t* args[] = {const_cast<wchar_t*>(L"RedPrism.exe"), unknown};
    Expect(std::wstring_view{RedPrismFindUnknownArgument(args, 2)} == L"--no-such", "cli.unknown-scan");

    wchar_t screenshot[] = L"--screenshot";
    wchar_t png[] = L"overlay.png";
    wchar_t* shotArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png};
    Expect(RedPrismFindUnknownArgument(shotArgs, 3) == nullptr, "cli.screenshot-value");

    RedPrismCommandLine parsed{};
    const wchar_t* error = nullptr;
    Expect(RedPrismParseCommandLine(shotArgs, 3, parsed, &error) && parsed.screenshotPath == std::wstring_view{png},
           "cli.screenshot-parse");

    const std::wstring help = RedPrismFormatCommandLineHelp();
    Expect(help.find(L"--warp") != std::wstring::npos && help.find(L"--self-test") != std::wstring::npos &&
               help.find(L"--screenshot") != std::wstring::npos,
           "cli.help-text");
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && std::wstring_view{argv[1]} == L"--asan-probe")
    {
        return RunAddressSanitizerProbe();
    }

    TestAdapterSelection();
    TestFrameScheduler();
    TestCommandLine();
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All host tests passed.\n");
    return 0;
}

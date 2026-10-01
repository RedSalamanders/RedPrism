#pragma once

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <windows.h>

// A test process never waits on a dialog. In Debug and ASan Debug a failed runtime check (an STL range check, a CRT
// assertion, a debug-heap report) otherwise opens the CRT's modal Abort/Retry/Ignore box, and in every configuration an
// abort, a fast-fail or a crash opens Windows Error Reporting's dialog: an unattended run hangs on it until its bound
// ends it, and an attended one interrupts the person at the desktop. The report goes to stderr, which the suite log
// keeps, and the process ends with exit code 3, the dialog's Abort, with no dialog of either kind
// (Specs/Testing/Testing_Validation.md).
namespace RedPrismTestFailureReports
{
#if defined(_DEBUG)
// The CRT calls its report hooks of both character types for every report, whatever type the reporting call used.
inline int __cdecl ReportAndEnd(int reportType, wchar_t* message, int* returnValue) noexcept
{
    if (reportType == _CRT_WARN)
    {
        return FALSE; // A warning keeps the CRT's own handling: the debugger output.
    }
    std::fputws(message != nullptr ? message : L"(a CRT report without text)\n", stderr);
    std::fflush(stderr);
    if (returnValue != nullptr)
    {
        *returnValue = 0;
    }
    std::abort();
}
#endif

// Call first in wmain, before any test can fail a check.
inline void RouteAwayFromDialogs() noexcept
{
    // abort() writes no message box and calls no Windows Error Reporting: it ends the process with exit code 3.
    static_cast<void>(_set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
    static_cast<void>(SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX));
#if defined(_DEBUG)
    // Reports go to stderr even if the hook were somehow bypassed; the hook ends the run at the first one.
    static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE));
    static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
    static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE));
    static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
    static_cast<void>(_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &ReportAndEnd));
#endif
}

// The hidden --failure-report-self-test switch of every test executable, which
// Tests/BuildProcessTests/FailureReportTests.ps1 runs: a failed runtime check must end the run with its report and exit
// code 3, never wait on a dialog.
[[nodiscard]] inline int RunSelfTest() noexcept
{
#if defined(_DEBUG)
    _ASSERTE(!L"the failure-report self-test fails this check on purpose");
    std::fputs("The failed check returned instead of ending the run.\n", stderr);
    return 1;
#else
    std::fputs("A Release build has no runtime checks to report.\n", stdout);
    return 0;
#endif
}
} // namespace RedPrismTestFailureReports

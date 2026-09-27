#include "PerfRootTests.h"

#include "../../Common/PerfRoot.h"

#include <cstdio>
#include <string>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s perf-root.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}
} // namespace

int RunPerfRootTests()
{
    g_failures = 0;

    Expect(IsValidPerfRootPath(L"X:\\RedPrism.Perf"), "grammar-canonical");
    Expect(IsValidPerfRootPath(L"z:\\redprism.perf"), "grammar-case-insensitive");
    Expect(!IsValidPerfRootPath(L"\\\\server\\share\\RedPrism.Perf"), "grammar-rejects-unc");
    Expect(!IsValidPerfRootPath(L"C:\\temp\\RedPrism.Perf"), "grammar-rejects-nested-path");
    Expect(!IsValidPerfRootPath(L"C:\\RedPrism.Perf\\child"), "grammar-rejects-child");
    Expect(!IsValidPerfRootPath(L"C:/RedPrism.Perf"), "grammar-requires-canonical-separator");
    Expect(!IsValidPerfRootPath(L"1:\\RedPrism.Perf"), "grammar-requires-ascii-drive-letter");

    std::wstring first;
    const std::int32_t firstStatus = ResolvePerfRoot(first);
    Expect(firstStatus >= 0 && IsValidPerfRootPath(first), "resolve-owned-root");
    if (firstStatus >= 0)
    {
        std::wstring second;
        const std::int32_t secondStatus = ResolvePerfRoot(second);
        Expect(secondStatus >= 0 && second == first, "resolve-stable-marked-root");
    }

    return g_failures;
}

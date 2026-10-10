#include "Scope.Tests.UiCancellationCli.h"

#include "CommandLine.h"

#include <array>
#include <cstdio>
#include <string_view>

namespace
{
void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s ui-cancel-cli.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    failures += condition ? 0 : 1;
}

struct BenchCase final
{
    const wchar_t* value;
    const char* acceptedName;
    const char* warpAfterName;
    const char* warpBeforeName;
};

constexpr std::array<BenchCase, 8> kBenchCases{{
    {L"ui-cancel", "accepted.ui-cancel", "warp-after.ui-cancel", "warp-before.ui-cancel"},
    {L"ui-cancel45", "accepted.ui-cancel45", "warp-after.ui-cancel45", "warp-before.ui-cancel45"},
    {L"ui-cancel100", "accepted.ui-cancel100", "warp-after.ui-cancel100", "warp-before.ui-cancel100"},
    {L"ui-cancel-smoke", "accepted.ui-cancel-smoke", "warp-after.ui-cancel-smoke", "warp-before.ui-cancel-smoke"},
    {L"ui-cancel-prepared", "accepted.ui-cancel-prepared", "warp-after.ui-cancel-prepared",
     "warp-before.ui-cancel-prepared"},
    {L"ui-cancel-prepared45", "accepted.ui-cancel-prepared45", "warp-after.ui-cancel-prepared45",
     "warp-before.ui-cancel-prepared45"},
    {L"ui-cancel-prepared100", "accepted.ui-cancel-prepared100", "warp-after.ui-cancel-prepared100",
     "warp-before.ui-cancel-prepared100"},
    {L"ui-cancel-prepared-smoke", "accepted.ui-cancel-prepared-smoke", "warp-after.ui-cancel-prepared-smoke",
     "warp-before.ui-cancel-prepared-smoke"},
}};

void ExpectRejectedWithoutMutation(wchar_t* const* arguments, int argumentCount, const wchar_t* expectedToken,
                                   const char* name, int& failures) noexcept
{
    constexpr wchar_t kScreenshotSentinel[] = L"sentinel.png";
    constexpr wchar_t kDemoSentinel[] = L"sentinel-demo";
    constexpr wchar_t kBenchSentinel[] = L"sentinel-bench";
    constexpr wchar_t kProbeSentinel[] = L"sentinel-probe";
    constexpr wchar_t kInputSentinel[] = L"sentinel-input";

    RedPrismCommandLine parsed{};
    parsed.warp = true;
    parsed.selfTest = true;
    parsed.screenshotPath = kScreenshotSentinel;
    parsed.screenshotDpi = 144;
    parsed.demo = kDemoSentinel;
    parsed.bench = kBenchSentinel;
    parsed.crashProbeDirectory = kProbeSentinel;
    parsed.crashProbeWorker = true;
    parsed.isa = KernelIsa::Scalar;
    parsed.inputPaths[0] = kInputSentinel;
    parsed.inputPathCount = 1;

    const wchar_t* errorToken = nullptr;
    const bool rejected = !RedPrismParseCommandLine(arguments, argumentCount, parsed, &errorToken);
    const bool exactToken = errorToken == expectedToken;
    const bool unchanged = parsed.warp && parsed.selfTest && parsed.screenshotPath == kScreenshotSentinel &&
                           parsed.screenshotDpi == 144 && parsed.demo == kDemoSentinel &&
                           parsed.bench == kBenchSentinel && parsed.crashProbeDirectory == kProbeSentinel &&
                           parsed.crashProbeWorker && parsed.isa == KernelIsa::Scalar && parsed.inputPathCount == 1 &&
                           parsed.inputPaths[0] == std::wstring_view{kInputSentinel};
    Expect(rejected && exactToken && unchanged, name, failures);
}
} // namespace

int RunUiCancellationCliTests() noexcept
{
    int failures = 0;
    wchar_t application[] = L"RedPrism.exe";
    wchar_t benchSwitch[] = L"--bench";
    wchar_t warpSwitch[] = L"--warp";
    for (const BenchCase& test : kBenchCases)
    {
        wchar_t* value = const_cast<wchar_t*>(test.value);
        RedPrismCommandLine parsed{};
        const wchar_t* errorToken = nullptr;
        wchar_t* acceptedArguments[] = {application, benchSwitch, value};
        Expect(RedPrismParseCommandLine(acceptedArguments, 3, parsed, &errorToken) && errorToken == nullptr &&
                   parsed.bench && parsed.bench == std::wstring_view{test.value} && !parsed.warp,
               test.acceptedName, failures);

        wchar_t* warpAfterArguments[] = {application, benchSwitch, value, warpSwitch};
        parsed = {};
        Expect(RedPrismParseCommandLine(warpAfterArguments, 4, parsed, &errorToken) && errorToken == nullptr &&
                   parsed.bench && parsed.bench == std::wstring_view{test.value} && parsed.warp,
               test.warpAfterName, failures);

        wchar_t* warpBeforeArguments[] = {application, warpSwitch, benchSwitch, value};
        parsed = {};
        Expect(RedPrismParseCommandLine(warpBeforeArguments, 4, parsed, &errorToken) && errorToken == nullptr &&
                   parsed.bench && parsed.bench == std::wstring_view{test.value} && parsed.warp,
               test.warpBeforeName, failures);
    }

    wchar_t* missingValueArguments[] = {application, benchSwitch};
    ExpectRejectedWithoutMutation(missingValueArguments, 2, benchSwitch, "missing-value.atomic", failures);

    wchar_t malformedValue[] = L"ui-cancel-not-a-mode";
    wchar_t* malformedArguments[] = {application, benchSwitch, malformedValue};
    ExpectRejectedWithoutMutation(malformedArguments, 3, malformedValue, "malformed-value.atomic", failures);

    wchar_t nearMatchValue[] = L"ui-cancel-smoke-extra";
    wchar_t* nearMatchArguments[] = {application, benchSwitch, nearMatchValue};
    ExpectRejectedWithoutMutation(nearMatchArguments, 3, nearMatchValue, "near-match.atomic", failures);

    wchar_t inputPath[] = L"image.png";
    wchar_t* positionalArguments[] = {application, benchSwitch, const_cast<wchar_t*>(L"ui-cancel"), inputPath};
    Expect(RedPrismFindUnknownArgument(positionalArguments, 4) == inputPath, "positional-input.exact-offender",
           failures);
    ExpectRejectedWithoutMutation(positionalArguments, 4, inputPath, "positional-input.atomic", failures);

    wchar_t firstScreenshot[] = L"first.png";
    wchar_t secondScreenshot[] = L"second.png";
    wchar_t firstScreenshotSwitch[] = L"--screenshot";
    wchar_t secondScreenshotSwitch[] = L"--screenshot";
    wchar_t* duplicateScreenshotArguments[] = {application, firstScreenshotSwitch, firstScreenshot,
                                               secondScreenshotSwitch, secondScreenshot};
    ExpectRejectedWithoutMutation(duplicateScreenshotArguments, 5, secondScreenshotSwitch,
                                  "duplicate-screenshot.atomic", failures);

    wchar_t firstProbe[] = L"first-dump-dir";
    wchar_t secondProbe[] = L"second-dump-dir";
    wchar_t firstProbeSwitch[] = L"--crash-probe";
    wchar_t secondProbeSwitch[] = L"--crash-probe";
    wchar_t* duplicateProbeArguments[] = {application, firstProbeSwitch, firstProbe, secondProbeSwitch, secondProbe};
    ExpectRejectedWithoutMutation(duplicateProbeArguments, 5, secondProbeSwitch, "duplicate-crash-probe.atomic",
                                  failures);

    wchar_t scalarValue[] = L"scalar";
    wchar_t sse2Value[] = L"sse2";
    wchar_t firstIsaSwitch[] = L"--isa";
    wchar_t secondIsaSwitch[] = L"--isa";
    wchar_t* duplicateIsaArguments[] = {application, firstIsaSwitch, scalarValue, secondIsaSwitch, sse2Value};
    ExpectRejectedWithoutMutation(duplicateIsaArguments, 5, secondIsaSwitch, "duplicate-isa.atomic", failures);

    wchar_t workerSwitch[] = L"--crash-probe-worker";
    wchar_t* workerWithoutProbeArguments[] = {application, workerSwitch};
    ExpectRejectedWithoutMutation(workerWithoutProbeArguments, 2, RedPrismSwitchName(RedPrismSwitch::CrashProbeWorker),
                                  "crash-probe-worker-requires-probe.atomic", failures);

    const std::wstring help = RedPrismFormatCommandLineHelp();
    Expect(help.find(L"ui-cancel-prepared100") != std::wstring::npos &&
               help.find(L"dispatch actual Escape") != std::wstring::npos &&
               help.find(L"prepared display/rows") != std::wstring::npos,
           "help.modes-and-prepared-fixtures", failures);
    Expect(help.find(L"correctness only") != std::wstring::npos &&
               help.find(L"no performance assertions") != std::wstring::npos &&
               help.find(L"do not activate the normal route") != std::wstring::npos,
           "help.smoke-safety-description", failures);
    return failures;
}

#include "KernelCliTests.h"

#include "CommandLine.h"

#include <array>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] bool ParseArguments(std::initializer_list<std::wstring_view> arguments, RedPrismCommandLine& parsed,
                                  std::wstring& error)
{
    std::vector<std::wstring> storage{L"RedPrism.exe"};
    storage.reserve(arguments.size() + 1);
    for (const std::wstring_view argument : arguments)
    {
        storage.emplace_back(argument);
    }

    std::vector<wchar_t*> pointers;
    pointers.reserve(storage.size());
    for (std::wstring& argument : storage)
    {
        pointers.push_back(argument.data());
    }

    const wchar_t* errorToken = nullptr;
    const bool succeeded =
        RedPrismParseCommandLine(pointers.data(), static_cast<int>(pointers.size()), parsed, &errorToken);
    error = errorToken != nullptr ? errorToken : L"";
    return succeeded;
}
} // namespace

int RunKernelCliTests()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        std::printf("%s kernel-cli.%s\n", condition ? "PASS" : "FAIL", name);
        failures += condition ? 0 : 1;
    };

    RedPrismCommandLine parsed;
    std::wstring error;
    check(ParseArguments({}, parsed, error) && parsed.isa == KernelIsa::Auto, "default-isa-is-auto");

    constexpr std::array<std::pair<std::wstring_view, KernelIsa>, 5> kValidIsas{{
        {L"scalar", KernelIsa::Scalar},
        {L"sse2", KernelIsa::Sse2},
        {L"avx2", KernelIsa::Avx2},
        {L"avx512", KernelIsa::Avx512},
        {L"neon", KernelIsa::Neon},
    }};
    for (const auto& [name, expected] : kValidIsas)
    {
        const bool succeeded = ParseArguments({L"--isa", name}, parsed, error);
        check(succeeded && parsed.isa == expected && error.empty(), "valid-isa-value");
    }

    check(!ParseArguments({L"--isa"}, parsed, error) && error == L"--isa", "missing-value");
    check(!ParseArguments({L"--isa", L""}, parsed, error) && error == L"--isa", "empty-value");
    check(!ParseArguments({L"--isa", L"unknown"}, parsed, error) && error == L"unknown", "unknown-value");
    check(!ParseArguments({L"--isa", L"auto"}, parsed, error) && error == L"auto", "auto-is-not-an-override");
    check(!ParseArguments({L"--isa", L"SSE2"}, parsed, error) && error == L"SSE2", "value-is-case-sensitive");
    check(!ParseArguments({L"--isa=scalar"}, parsed, error) && error == L"--isa=scalar", "inline-value-rejected");
    check(!ParseArguments({L"--isa", L"scalar", L"--isa", L"sse2"}, parsed, error) && error == L"--isa",
          "duplicate-switch-rejected");

    const RedPrismCommandLineSwitch* isaSwitch = RedPrismFindSwitch(L"--isa");
    const std::wstring help = RedPrismFormatCommandLineHelp();
    check(isaSwitch != nullptr && isaSwitch->valueKind == RedPrismSwitchValue::Separate &&
              std::wstring_view{isaSwitch->value} == L"<scalar|sse2|avx2|avx512|neon>" &&
              std::wstring_view{isaSwitch->group} == L"Diagnostics" && help.find(L"--isa") != std::wstring::npos &&
              help.find(isaSwitch->value) != std::wstring::npos,
          "help-catalogue-includes-isa");
    return failures;
}

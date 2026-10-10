#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Kernels.h"

enum class RedPrismSwitch : uint8_t
{
    Help = 0,
    Warp,
    SelfTest,
    Screenshot,
    Dpi,
    Demo,
    Bench,
    CrashProbe,
    CrashProbeWorker,
    Isa,
    Count,
};

enum class RedPrismSwitchValue : uint8_t
{
    None = 0,
    Separate,
};

struct RedPrismCommandLineSwitch final
{
    RedPrismSwitch id;
    const wchar_t* name;
    const wchar_t* aliases;
    RedPrismSwitchValue valueKind;
    const wchar_t* value;
    const wchar_t* summary;
    const wchar_t* group;
};

struct RedPrismCommandLine final
{
    static constexpr size_t kMaximumInputPathCount = 256;
    static constexpr size_t kMaximumPathLength = 32767;

    bool warp = false;
    bool selfTest = false;
    const wchar_t* screenshotPath = nullptr;
    uint32_t screenshotDpi = 0;
    const wchar_t* demo = nullptr;
    const wchar_t* bench = nullptr;
    const wchar_t* crashProbeDirectory = nullptr;
    bool crashProbeWorker = false;
    KernelIsa isa = KernelIsa::Auto;
    std::array<std::wstring_view, kMaximumInputPathCount> inputPaths{};
    size_t inputPathCount = 0;
};

inline constexpr std::array<const wchar_t*, 4> kRedPrismHelpArguments{L"--help", L"-h", L"/?", L"-?"};

inline constexpr std::array<RedPrismCommandLineSwitch, static_cast<size_t>(RedPrismSwitch::Count)>
    kRedPrismCommandLineSwitches{
        RedPrismCommandLineSwitch{RedPrismSwitch::Help, L"--help", L"-h, /?, -?", RedPrismSwitchValue::None, nullptr,
                                  L"Show this text and exit.", L"General"},
        RedPrismCommandLineSwitch{RedPrismSwitch::Warp, L"--warp", nullptr, RedPrismSwitchValue::None, nullptr,
                                  L"Render on the Microsoft Basic Render Driver (WARP) instead of the GPU.",
                                  L"General"},
        RedPrismCommandLineSwitch{RedPrismSwitch::SelfTest, L"--self-test", nullptr, RedPrismSwitchValue::None, nullptr,
                                  L"Hidden startup validation: create a flip-model WARP swap chain, present chrome, "
                                  L"and exit 0 when the host works.",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::Screenshot, L"--screenshot", nullptr, RedPrismSwitchValue::Separate,
                                  L"<png>",
                                  L"Show the window without activating it, present chrome, capture it through "
                                  L"Windows.Graphics.Capture, and exit: 0 written, 8 failed.",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::Dpi, L"--dpi", nullptr, RedPrismSwitchValue::Separate, L"<96|144>",
                                  L"Override layout DPI for --screenshot (empty-document shell at 96 or 144).",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::Demo, L"--demo", nullptr, RedPrismSwitchValue::Separate,
                                  L"<transform|selection|brush|retouch|adjust|canvassize|crop>",
                                  L"Populate the new document for a screenshot: transform adds two painted layers and "
                                  L"shows the Move tool's box on the rotated one; selection adds an elliptical and a "
                                  L"rectangular selection with the Marquee tool; brush paints strokes on a backdrop; "
                                  L"retouch adds a radial gradient and two shapes; adjust opens the Levels panel on "
                                  L"a Levels adjustment layer over a painted backdrop; canvassize opens the Canvas "
                                  L"Size card over the transform layers; crop frames part of them with the Crop tool.",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{
            RedPrismSwitch::Bench, L"--bench", nullptr, RedPrismSwitchValue::Separate,
            L"<brush|kernels|kernels45|buffers|buffers45|kernels-smoke|ui|ui45|ui100|ui-smoke|ui-sampler|"
            L"ui-sampler45|ui-sampler100|ui-sampler-smoke|ui-prepared|ui-prepared45|ui-prepared100|"
            L"ui-prepared-smoke|layer-rows|layer-rows-smoke|ui-cancel|ui-cancel45|ui-cancel100|ui-cancel-smoke|ui-"
            L"cancel-prepared|ui-cancel-prepared45|ui-cancel-prepared100|ui-cancel-prepared-smoke>",
            L"Run the brush benchmark on the selected device, or the CPU kernel benchmark (kernels: 3840x2160, 10; "
            L"kernels45: 8192x5464, 10; kernels-smoke: 32x24, 2). The smoke mode is not a performance "
            L"measurement. buffers and buffers45 compare copy/adopt pairs at those same image sizes. UI benchmarks "
            L"exercise the application input loop (ui: 3840x2160 with two images, mask and adjustment; ui45: "
            L"8192x5464 with the same fixture; ui100: 10000x10000 with one image; each runs 10 samples after one "
            L"warmup). ui-smoke runs the same path at 64x48 for two samples after one warmup. The ui-sampler, "
            L"ui-sampler45 and ui-sampler100 variants report only sampler-accepted-prefix on their corresponding "
            L"UI fixture at 10 samples after one warmup; ui-sampler-smoke runs that row at 64x48 for two samples "
            L"after one warmup. ui-prepared, ui-prepared45, ui-prepared100 and ui-prepared-smoke use those "
            L"same UI fixtures with the experimental prepared display and rows, requiring current publications. "
            L"They do not enable that route for normal editing. layer-rows measures owner-thread publish/Prepare and "
            L"full offscreen UI Automation "
            L"(UIA) name reads with 10,000 rows and 1,024 UTF-16-unit names; layer-rows-smoke uses a small fixture. "
            L"These modes report measurements without asserting performance success."
            L"\n\nOpt-in ui-cancel modes dispatch actual Escape and measure cancellation feedback and body return with "
            L"original ownership. "
            L"ui-cancel, ui-cancel45, and ui-cancel100 use the default, 45 MP, and 100 MP correctness fixtures; "
            L"ui-cancel-smoke uses the small correctness fixture. The prepared variants use prepared display/rows, "
            L"not ordinary activation, with corresponding fixtures. Smoke variants are correctness only, make no "
            L"performance assertions, and do not activate the normal route.",
            L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::CrashProbe, L"--crash-probe", nullptr, RedPrismSwitchValue::Separate,
                                  L"<directory>",
                                  L"Hidden crash-handling validation: install the crash handler with <directory> for "
                                  L"its dumps, raise an access violation and exit through it; the dump and its "
                                  L"pending marker land there.",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::CrashProbeWorker, L"--crash-probe-worker", nullptr,
                                  RedPrismSwitchValue::None, nullptr,
                                  L"Fault two private-pool threads; requires --crash-probe <directory> and "
                                  L"may appear before or after it.",
                                  L"Diagnostics"},
        RedPrismCommandLineSwitch{RedPrismSwitch::Isa, L"--isa", nullptr, RedPrismSwitchValue::Separate,
                                  L"<scalar|sse2|avx2|avx512|neon>",
                                  L"Select the CPU kernel implementation. The requested tier must be supported by "
                                  L"this hardware and implemented in this build.",
                                  L"Diagnostics"}};

inline constexpr std::array<const wchar_t*, 2> kRedPrismCommandLineGroups{L"General", L"Diagnostics"};

[[nodiscard]] constexpr const RedPrismCommandLineSwitch& RedPrismSwitchInfo(RedPrismSwitch id) noexcept
{
    return kRedPrismCommandLineSwitches[static_cast<size_t>(id)];
}

[[nodiscard]] constexpr const wchar_t* RedPrismSwitchName(RedPrismSwitch id) noexcept
{
    return RedPrismSwitchInfo(id).name;
}

[[nodiscard]] inline bool RedPrismIsHelpArgument(std::wstring_view argument) noexcept
{
    for (const wchar_t* alias : kRedPrismHelpArguments)
    {
        if (argument == alias)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline size_t RedPrismBoundedWideLength(const wchar_t* text, size_t limit) noexcept
{
    if (!text)
    {
        return limit;
    }
    size_t length = 0;
    while (length < limit && text[length] != L'\0')
    {
        ++length;
    }
    return length;
}

[[nodiscard]] inline const RedPrismCommandLineSwitch* RedPrismFindSwitch(std::wstring_view argument) noexcept
{
    if (RedPrismIsHelpArgument(argument))
    {
        return &RedPrismSwitchInfo(RedPrismSwitch::Help);
    }
    for (const RedPrismCommandLineSwitch& entry : kRedPrismCommandLineSwitches)
    {
        if (argument == entry.name)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const RedPrismCommandLineSwitch* RedPrismFindSwitch(const wchar_t* argument) noexcept
{
    constexpr size_t kMaximumArgumentLength = RedPrismCommandLine::kMaximumPathLength;
    if (!argument)
    {
        return nullptr;
    }
    const size_t length = RedPrismBoundedWideLength(argument, kMaximumArgumentLength + 1);
    if (length > kMaximumArgumentLength)
    {
        return nullptr;
    }
    return RedPrismFindSwitch(std::wstring_view{argument, length});
}

[[nodiscard]] inline bool RedPrismIsHarnessArgument(RedPrismSwitch id) noexcept
{
    return id == RedPrismSwitch::SelfTest || id == RedPrismSwitch::Screenshot || id == RedPrismSwitch::Dpi ||
           id == RedPrismSwitch::Demo || id == RedPrismSwitch::Bench || id == RedPrismSwitch::CrashProbe ||
           id == RedPrismSwitch::CrashProbeWorker;
}

struct RedPrismCommandLineScan final
{
    bool help = false;
    bool selfTest = false;
    bool harnessMode = false;
};

[[nodiscard]] inline RedPrismCommandLineScan RedPrismScanCommandLine(wchar_t* const* arguments,
                                                                     int argumentCount) noexcept
{
    RedPrismCommandLineScan scan{};
    bool positionalOnly = false;
    if (!arguments || argumentCount < 0)
    {
        return scan;
    }
    for (int index = 1; index < argumentCount; ++index)
    {
        const wchar_t* const argument = arguments[index];
        if (!argument || RedPrismBoundedWideLength(argument, RedPrismCommandLine::kMaximumPathLength + 1) >
                             RedPrismCommandLine::kMaximumPathLength)
        {
            return scan;
        }
        const size_t length = RedPrismBoundedWideLength(argument, RedPrismCommandLine::kMaximumPathLength + 1);
        const std::wstring_view text{argument, length};
        if (!positionalOnly && text == L"--")
        {
            positionalOnly = true;
            continue;
        }
        if (positionalOnly)
        {
            continue;
        }
        const RedPrismCommandLineSwitch* const entry = RedPrismFindSwitch(text);
        if (!entry)
        {
            continue;
        }
        scan.help = scan.help || entry->id == RedPrismSwitch::Help;
        scan.selfTest = scan.selfTest || entry->id == RedPrismSwitch::SelfTest;
        scan.harnessMode = scan.harnessMode || RedPrismIsHarnessArgument(entry->id);
        if (entry->valueKind == RedPrismSwitchValue::Separate && index + 1 < argumentCount)
        {
            ++index;
        }
    }
    return scan;
}

[[nodiscard]] inline const wchar_t* RedPrismFindInvalidArgumentShape(wchar_t* const* arguments,
                                                                     int argumentCount) noexcept
{
    constexpr size_t kMaximumInputPathCount = RedPrismCommandLine::kMaximumInputPathCount;
    constexpr size_t kMaximumPathLength = RedPrismCommandLine::kMaximumPathLength;
    size_t pathCount = 0;
    const wchar_t* firstPath = nullptr;
    bool harnessMode = false;
    bool positionalOnly = false;
    if (!arguments || argumentCount < 0)
    {
        return L"";
    }
    for (int index = 1; index < argumentCount; ++index)
    {
        const wchar_t* const argument = arguments[index];
        if (!argument)
        {
            return L"";
        }
        const size_t argumentLength = RedPrismBoundedWideLength(argument, kMaximumPathLength + 1);
        if (argumentLength > kMaximumPathLength)
        {
            return argument;
        }
        const std::wstring_view argumentView{argument, argumentLength};
        if (!positionalOnly && argumentView == L"--")
        {
            positionalOnly = true;
            continue;
        }

        const RedPrismCommandLineSwitch* entry = positionalOnly ? nullptr : RedPrismFindSwitch(argumentView);
        if (entry)
        {
            harnessMode = harnessMode || RedPrismIsHarnessArgument(entry->id);
            if (entry->valueKind == RedPrismSwitchValue::Separate && index + 1 < argumentCount)
            {
                const wchar_t* const value = arguments[index + 1];
                if (!value)
                {
                    return L"";
                }
                if (RedPrismBoundedWideLength(value, kMaximumPathLength + 1) > kMaximumPathLength)
                {
                    return value;
                }
                ++index;
            }
            continue;
        }

        if (!positionalOnly && argument[0] == L'-')
        {
            return argument;
        }

        if (argumentLength == 0)
        {
            return argument;
        }
        if (!firstPath)
        {
            firstPath = argument;
        }
        if (++pathCount > kMaximumInputPathCount)
        {
            return argument;
        }
    }
    return harnessMode ? firstPath : nullptr;
}

[[nodiscard]] inline const wchar_t* RedPrismFindUnknownArgument(wchar_t* const* arguments, int argumentCount) noexcept
{
    return RedPrismFindInvalidArgumentShape(arguments, argumentCount);
}

[[nodiscard]] inline bool RedPrismParseCommandLine(wchar_t* const* arguments, int argumentCount,
                                                   RedPrismCommandLine& parsed, const wchar_t** errorToken) noexcept
{
    if (errorToken)
    {
        *errorToken = nullptr;
    }
    if (const wchar_t* invalidArgument = RedPrismFindInvalidArgumentShape(arguments, argumentCount))
    {
        if (errorToken)
        {
            *errorToken = invalidArgument;
        }
        return false;
    }

    RedPrismCommandLine candidate{};
    bool isaSpecified = false;
    bool positionalOnly = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        if (!positionalOnly && std::wstring_view{arguments[index]} == L"--")
        {
            positionalOnly = true;
            continue;
        }
        const RedPrismCommandLineSwitch* entry = positionalOnly ? nullptr : RedPrismFindSwitch(arguments[index]);
        if (!entry)
        {
            candidate.inputPaths[candidate.inputPathCount++] = std::wstring_view{arguments[index]};
            continue;
        }
        switch (entry->id)
        {
        case RedPrismSwitch::Help:
            break;
        case RedPrismSwitch::Warp:
            candidate.warp = true;
            break;
        case RedPrismSwitch::SelfTest:
            candidate.selfTest = true;
            break;
        case RedPrismSwitch::Screenshot:
            if (candidate.screenshotPath || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
            {
                if (errorToken)
                {
                    *errorToken = arguments[index];
                }
                return false;
            }
            candidate.screenshotPath = arguments[++index];
            break;
        case RedPrismSwitch::Demo:
            if (index + 1 >= argumentCount || (std::wstring_view{arguments[index + 1]} != L"transform" &&
                                               std::wstring_view{arguments[index + 1]} != L"selection" &&
                                               std::wstring_view{arguments[index + 1]} != L"brush" &&
                                               std::wstring_view{arguments[index + 1]} != L"retouch" &&
                                               std::wstring_view{arguments[index + 1]} != L"adjust" &&
                                               std::wstring_view{arguments[index + 1]} != L"canvassize" &&
                                               std::wstring_view{arguments[index + 1]} != L"crop"))
            {
                if (errorToken)
                {
                    *errorToken = index + 1 < argumentCount ? arguments[index + 1] : arguments[index];
                }
                return false;
            }
            candidate.demo = arguments[++index];
            break;
        case RedPrismSwitch::Bench:
            if (index + 1 >= argumentCount || (std::wstring_view{arguments[index + 1]} != L"brush" &&
                                               std::wstring_view{arguments[index + 1]} != L"kernels" &&
                                               std::wstring_view{arguments[index + 1]} != L"kernels45" &&
                                               std::wstring_view{arguments[index + 1]} != L"buffers" &&
                                               std::wstring_view{arguments[index + 1]} != L"buffers45" &&
                                               std::wstring_view{arguments[index + 1]} != L"kernels-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui45" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui100" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-sampler" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-sampler45" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-sampler100" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-sampler-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-prepared" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-prepared45" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-prepared100" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-prepared-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"layer-rows" &&
                                               std::wstring_view{arguments[index + 1]} != L"layer-rows-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel45" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel100" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel-smoke" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel-prepared" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel-prepared45" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel-prepared100" &&
                                               std::wstring_view{arguments[index + 1]} != L"ui-cancel-prepared-smoke"))
            {
                if (errorToken)
                {
                    *errorToken = index + 1 < argumentCount ? arguments[index + 1] : arguments[index];
                }
                return false;
            }
            candidate.bench = arguments[++index];
            break;
        case RedPrismSwitch::CrashProbe:
            if (candidate.crashProbeDirectory || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
            {
                if (errorToken)
                {
                    *errorToken = arguments[index];
                }
                return false;
            }
            candidate.crashProbeDirectory = arguments[++index];
            break;
        case RedPrismSwitch::CrashProbeWorker:
            candidate.crashProbeWorker = true;
            break;
        case RedPrismSwitch::Isa:
            if (isaSpecified || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
            {
                if (errorToken)
                {
                    *errorToken = arguments[index];
                }
                return false;
            }
            {
                const std::optional<KernelIsa> isa = ParseKernelIsa(arguments[index + 1]);
                if (!isa.has_value())
                {
                    if (errorToken)
                    {
                        *errorToken = arguments[++index];
                    }
                    return false;
                }
                candidate.isa = isa.value();
                isaSpecified = true;
                ++index;
            }
            break;
        case RedPrismSwitch::Dpi:
            if (index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
            {
                if (errorToken)
                {
                    *errorToken = arguments[index];
                }
                return false;
            }
            {
                const std::wstring_view text{arguments[++index]};
                if (text == L"96")
                {
                    candidate.screenshotDpi = 96;
                }
                else if (text == L"144")
                {
                    candidate.screenshotDpi = 144;
                }
                else
                {
                    if (errorToken)
                    {
                        *errorToken = arguments[index];
                    }
                    return false;
                }
            }
            break;
        case RedPrismSwitch::Count:
            break;
        }
    }
    if (candidate.crashProbeWorker && !candidate.crashProbeDirectory)
    {
        if (errorToken)
        {
            *errorToken = RedPrismSwitchName(RedPrismSwitch::CrashProbeWorker);
        }
        return false;
    }
    parsed = candidate;
    return true;
}

[[nodiscard]] inline std::wstring RedPrismFormatCommandLineHelp()
{
    std::wstring text;
    text += L"RedPrism — Windows raster composition\n\n";
    text += L"Usage: RedPrism.exe [--warp]\n";
    text += L"       RedPrism.exe --self-test [--warp]\n";
    text += L"       RedPrism.exe --screenshot <png> [--warp] [--dpi 96|144]\n";
    text += L"       RedPrism.exe [--isa scalar|sse2|avx2|avx512|neon]\n";
    text += L"       RedPrism.exe --crash-probe <directory>\n";
    text += L"       RedPrism.exe --crash-probe <directory> --crash-probe-worker\n";
    text += L"       RedPrism.exe --crash-probe-worker --crash-probe <directory>\n";
    text += L"       RedPrism.exe --help\n";
    text += L"\nWithout switches RedPrism opens a titled Direct3D 11 window with DxUi chrome.\n";
    for (const wchar_t* group : kRedPrismCommandLineGroups)
    {
        text += L"\n";
        text += group;
        text += L"\n";
        for (const RedPrismCommandLineSwitch& entry : kRedPrismCommandLineSwitches)
        {
            if (std::wstring_view{entry.group} != group)
            {
                continue;
            }
            std::wstring lead = L"  ";
            lead += entry.name;
            if (entry.value)
            {
                lead += L" ";
                lead += entry.value;
            }
            if (entry.aliases)
            {
                lead += L"  (";
                lead += entry.aliases;
                lead += L")";
            }
            constexpr size_t kColumn = 28;
            if (lead.size() < kColumn)
            {
                lead.append(kColumn - lead.size(), L' ');
            }
            else
            {
                lead += L"\n";
                lead.append(kColumn, L' ');
            }
            text += lead;
            text += entry.summary;
            text += L"\n";
        }
    }
    text += L"\nExit codes: 0 ok, 1 allocation, 2 command line or window, 5 graphics, 8 capture unsupported, 9 capture "
            L"failed.\n";
    return text;
}

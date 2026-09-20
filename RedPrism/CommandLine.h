#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

enum class RedPrismSwitch : uint8_t
{
    Help = 0,
    Warp,
    SelfTest,
    Screenshot,
    Dpi,
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
    bool warp = false;
    bool selfTest = false;
    const wchar_t* screenshotPath = nullptr;
    uint32_t screenshotDpi = 0;
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
    };

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

[[nodiscard]] inline const wchar_t* RedPrismFindUnknownArgument(wchar_t* const* arguments, int argumentCount) noexcept
{
    for (int index = 1; index < argumentCount; ++index)
    {
        const RedPrismCommandLineSwitch* entry = RedPrismFindSwitch(arguments[index]);
        if (!entry)
        {
            return arguments[index];
        }
        if (entry->valueKind == RedPrismSwitchValue::Separate && index + 1 < argumentCount)
        {
            ++index;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool RedPrismParseCommandLine(wchar_t* const* arguments, int argumentCount,
                                                   RedPrismCommandLine& parsed, const wchar_t** errorToken) noexcept
{
    parsed = {};
    if (errorToken)
    {
        *errorToken = nullptr;
    }
    for (int index = 1; index < argumentCount; ++index)
    {
        const RedPrismCommandLineSwitch* entry = RedPrismFindSwitch(arguments[index]);
        if (!entry)
        {
            if (errorToken)
            {
                *errorToken = arguments[index];
            }
            return false;
        }
        switch (entry->id)
        {
        case RedPrismSwitch::Help:
            break;
        case RedPrismSwitch::Warp:
            parsed.warp = true;
            break;
        case RedPrismSwitch::SelfTest:
            parsed.selfTest = true;
            break;
        case RedPrismSwitch::Screenshot:
            if (parsed.screenshotPath || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
            {
                if (errorToken)
                {
                    *errorToken = arguments[index];
                }
                return false;
            }
            parsed.screenshotPath = arguments[++index];
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
                    parsed.screenshotDpi = 96;
                }
                else if (text == L"144")
                {
                    parsed.screenshotDpi = 144;
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
    return true;
}

[[nodiscard]] inline std::wstring RedPrismFormatCommandLineHelp()
{
    std::wstring text;
    text += L"RedPrism — Windows raster composition\n\n";
    text += L"Usage: RedPrism.exe [--warp]\n";
    text += L"       RedPrism.exe --self-test [--warp]\n";
    text += L"       RedPrism.exe --screenshot <png> [--warp] [--dpi 96|144]\n";
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
    text += L"\nExit codes: 0 ok, 1 allocation, 2 command line or window, 5 graphics, 8 screenshot.\n";
    return text;
}

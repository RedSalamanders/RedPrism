#pragma once

// RF-704: Specs/Build/Build_Toolchain.md defines REDPRISM_VERSION_BUILD only for ResourceCompile (deliberately, so
// a new build number does not force a C++ recompile) -- the compile-time REDPRISM_VERSION_STRING baked into this
// translation unit's object code is therefore always "0.1.0.0". The running module's own VS_VERSIONINFO resource,
// stamped by ResourceCompile, does carry the real build number; this reads that back at run time instead.

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <winver.h>

#pragma comment(lib, "version.lib")

namespace RedPrism
{

// "major.minor.build.revision", the same shape as REDPRISM_VERSION_STRING.
[[nodiscard]] inline std::wstring FormatFileVersion(std::uint16_t major, std::uint16_t minor, std::uint16_t build,
                                                    std::uint16_t revision)
{
    return std::to_wstring(major) + L"." + std::to_wstring(minor) + L"." + std::to_wstring(build) + L"." +
           std::to_wstring(revision);
}

// Reads modulePath's VS_FIXEDFILEINFO and formats it into `formatted`. Returns false, leaving `formatted`
// untouched, when the module has no version resource or it fails to parse, so the caller can fall back to the
// compile-time string.
[[nodiscard]] inline bool TryReadModuleFileVersion(const wchar_t* modulePath, std::wstring& formatted)
{
    if (!modulePath)
    {
        return false;
    }
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(modulePath, &handle);
    if (size == 0)
    {
        return false;
    }
    std::vector<BYTE> block(size);
    // GetFileVersionInfoW's dwHandle parameter is reserved and must be zero. The handle output from
    // GetFileVersionInfoSizeW is not an input to this API.
    if (!GetFileVersionInfoW(modulePath, 0, size, block.data()))
    {
        return false;
    }
    void* value = nullptr;
    UINT valueSize = 0;
    if (!VerQueryValueW(block.data(), L"\\", &value, &valueSize) || !value || valueSize < sizeof(VS_FIXEDFILEINFO))
    {
        return false;
    }
    const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(value);
    if (fixed->dwSignature != 0xFEEF04BDu)
    {
        return false;
    }
    formatted = FormatFileVersion(HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                                  HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
    return true;
}

// Formats the running module's own file version for display, falling back to `fallbackVersion` (the compile-time
// REDPRISM_VERSION_STRING_W) when the resource cannot be read.
[[nodiscard]] inline std::wstring FormatRunningModuleVersion(const wchar_t* fallbackVersion)
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(MAX_PATH));
    std::wstring formatted;
    if (length > 0 && length < MAX_PATH && TryReadModuleFileVersion(modulePath, formatted))
    {
        return formatted;
    }
    return fallbackVersion ? std::wstring{fallbackVersion} : std::wstring{};
}

} // namespace RedPrism

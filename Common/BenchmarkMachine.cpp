#include "BenchmarkMachine.h"

#include <windows.h>

#include <objbase.h>
#include <powrprof.h>
#include <winternl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <math.h>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

#if defined(_M_X64)
#include <intrin.h>
#endif

namespace
{
constexpr std::size_t kMaximumTopologyBytes = 16u * 1024u * 1024u;

[[nodiscard]] std::optional<std::string> Utf8FromWide(const wchar_t* text, const int length)
{
    if (text == nullptr || length <= 0)
    {
        return std::nullopt;
    }

    const int byteCount =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0)
    {
        return std::nullopt;
    }

    std::string result(static_cast<std::size_t>(byteCount), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, result.data(), byteCount, nullptr, nullptr) !=
        byteCount)
    {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::string> ReadRegistryCpuBrand()
{
    wchar_t value[256]{};
    DWORD byteCount = static_cast<DWORD>(sizeof(value));
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                        L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, value, &byteCount);
    if (status != ERROR_SUCCESS || byteCount < sizeof(wchar_t) || byteCount > sizeof(value))
    {
        return std::nullopt;
    }

    const std::size_t chars = byteCount / sizeof(wchar_t);
    std::size_t length = 0;
    while (length < chars && value[length] != L'\0')
    {
        ++length;
    }
    while (length > 0 && (value[length - 1] == L' ' || value[length - 1] == L'\t'))
    {
        --length;
    }
    return Utf8FromWide(value, static_cast<int>(length));
}

[[nodiscard]] std::optional<std::string> ReadCpuidCpuBrand()
{
#if defined(_M_X64)
    int registers[4]{};
    __cpuid(registers, static_cast<int>(0x80000000u));
    if (static_cast<std::uint32_t>(registers[0]) < 0x80000004u)
    {
        return std::nullopt;
    }

    char brand[49]{};
    for (int leaf = 0; leaf < 3; ++leaf)
    {
        __cpuid(registers, static_cast<int>(0x80000002u + static_cast<std::uint32_t>(leaf)));
        const std::size_t offset = static_cast<std::size_t>(leaf) * 16;
        const auto* bytes = reinterpret_cast<const char*>(registers);
        for (std::size_t index = 0; index < 16; ++index)
        {
            brand[offset + index] = bytes[index];
        }
    }
    std::string result(brand);
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
    {
        result.pop_back();
    }
    if (result.empty())
    {
        return std::nullopt;
    }
    return result;
#else
    return std::nullopt;
#endif
}

[[nodiscard]] std::optional<std::string> ReadCpuBrand()
{
    auto brand = ReadRegistryCpuBrand();
    if (!brand.has_value() || brand.value().empty())
    {
        brand = ReadCpuidCpuBrand();
    }
    return brand;
}

struct ProcessorTopology final
{
    std::uint32_t physicalCores = 0;
    std::uint32_t logicalProcessors = 0;
    std::uint32_t l3Caches = 0;
    std::uint64_t l3Bytes = 0;
    bool sawProcessorCore = false;
    bool sawL3Cache = false;
};

[[nodiscard]] bool AddChecked(const std::uint64_t amount, std::uint64_t& total) noexcept
{
    if (amount > std::numeric_limits<std::uint64_t>::max() - total)
    {
        return false;
    }
    total += amount;
    return true;
}

[[nodiscard]] bool ReadProcessorTopology(ProcessorTopology& topology)
{
    DWORD requiredBytes = 0;
    (void)GetLogicalProcessorInformationEx(RelationAll, nullptr, &requiredBytes);
    if (requiredBytes == 0 || requiredBytes > kMaximumTopologyBytes)
    {
        return false;
    }

    std::vector<std::byte> buffer(requiredBytes);
    DWORD returnedBytes = requiredBytes;
    if (GetLogicalProcessorInformationEx(RelationAll,
                                         reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                                         &returnedBytes) == FALSE ||
        returnedBytes == 0 || returnedBytes > buffer.size())
    {
        return false;
    }

    ProcessorTopology result{};
    std::size_t offset = 0;
    const std::size_t processorHeaderBytes =
        offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) + offsetof(PROCESSOR_RELATIONSHIP, GroupMask);
    const std::size_t cacheGroupMasksOffset =
        offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Cache) + offsetof(CACHE_RELATIONSHIP, GroupMask);
    while (offset < returnedBytes)
    {
        const std::size_t remaining = static_cast<std::size_t>(returnedBytes) - offset;
        if (remaining < offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor))
        {
            return false;
        }

        const auto* record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        const std::size_t recordBytes = record->Size;
        if (recordBytes < offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size) + sizeof(DWORD) ||
            recordBytes > remaining)
        {
            return false;
        }

        if (record->Relationship == RelationProcessorCore)
        {
            if (recordBytes < processorHeaderBytes)
            {
                return false;
            }
            const std::size_t groupCount = record->Processor.GroupCount;
            if (groupCount == 0 || groupCount > (recordBytes - processorHeaderBytes) / sizeof(GROUP_AFFINITY) ||
                result.physicalCores == std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }

            std::uint32_t logicalCountForCore = 0;
            for (std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
            {
                const GROUP_AFFINITY& group = record->Processor.GroupMask[groupIndex];
                const auto count = static_cast<std::uint32_t>(std::popcount(static_cast<std::uint64_t>(group.Mask)));
                if (count > std::numeric_limits<std::uint32_t>::max() - logicalCountForCore)
                {
                    return false;
                }
                logicalCountForCore += count;
            }
            if (logicalCountForCore == 0 ||
                logicalCountForCore > std::numeric_limits<std::uint32_t>::max() - result.logicalProcessors)
            {
                return false;
            }
            ++result.physicalCores;
            result.logicalProcessors += logicalCountForCore;
            result.sawProcessorCore = true;
        }
        else if (record->Relationship == RelationCache)
        {
            if (recordBytes < cacheGroupMasksOffset)
            {
                return false;
            }
            const std::size_t groupCount = record->Cache.GroupCount;
            if (groupCount == 0 || groupCount > (recordBytes - cacheGroupMasksOffset) / sizeof(GROUP_AFFINITY))
            {
                return false;
            }
            if (record->Cache.Level == 3)
            {
                if (record->Cache.CacheSize == 0 || result.l3Caches == std::numeric_limits<std::uint32_t>::max() ||
                    !AddChecked(record->Cache.CacheSize, result.l3Bytes))
                {
                    return false;
                }
                ++result.l3Caches;
                result.sawL3Cache = true;
            }
        }
        offset += recordBytes;
    }

    if (offset != returnedBytes)
    {
        return false;
    }
    topology = result;
    return true;
}

[[nodiscard]] std::optional<std::string> ReadActivePowerSchemeGuid()
{
    GUID* returned = nullptr;
    const DWORD status = PowerGetActiveScheme(nullptr, &returned);
    wil::unique_hlocal_ptr<GUID> scheme{returned};
    if (status != ERROR_SUCCESS || scheme.get() == nullptr)
    {
        return std::nullopt;
    }

    wchar_t text[39]{};
    if (StringFromGUID2(*scheme.get(), text, static_cast<int>(std::size(text))) == 0)
    {
        return std::nullopt;
    }
    return Utf8FromWide(text, 38);
}

[[nodiscard]] BenchmarkMachineInfo::Fma3Status ReadFma3Status() noexcept
{
#if defined(_M_ARM64)
    return BenchmarkMachineInfo::Fma3Status::NotApplicable;
#elif defined(_M_X64)
    return _get_FMA3_enable() != 0 ? BenchmarkMachineInfo::Fma3Status::Enabled
                                   : BenchmarkMachineInfo::Fma3Status::Disabled;
#else
    return BenchmarkMachineInfo::Fma3Status::Unknown;
#endif
}

[[nodiscard]] std::string_view Fma3Text(const BenchmarkMachineInfo::Fma3Status status) noexcept
{
    using Status = BenchmarkMachineInfo::Fma3Status;
    switch (status)
    {
    case Status::Enabled:
        return "enabled";
    case Status::Disabled:
        return "disabled";
    case Status::NotApplicable:
        return "n/a";
    case Status::Unknown:
        return "unknown";
    }
    return "unknown";
}

void AppendQuoted(std::string& output, const std::string_view value)
{
    output.push_back('"');
    for (const char rawCharacter : value)
    {
        const auto character = static_cast<unsigned char>(rawCharacter);
        switch (character)
        {
        case '"':
            output.append("\\\"");
            break;
        case '\\':
            output.append("\\\\");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\r':
            output.append("\\r");
            break;
        case '\t':
            output.append("\\t");
            break;
        default:
            if (character < 0x20u || character == 0x7fu)
            {
                output.append(std::format("\\x{:02X}", static_cast<unsigned int>(character)));
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

template <typename T>
void AppendOptionalNumber(std::string& output, const std::string_view name, const std::optional<T>& value)
{
    output.append(name);
    output.push_back('=');
    if (value.has_value())
    {
        output.append(std::to_string(value.value()));
    }
    else
    {
        output.append("unknown");
    }
    output.push_back(' ');
}

void AppendOptionalText(std::string& output, const std::string_view name, const std::optional<std::string>& value)
{
    output.append(name);
    output.push_back('=');
    if (value.has_value())
    {
        AppendQuoted(output, value.value());
    }
    else
    {
        output.append("unknown");
    }
    output.push_back(' ');
}
} // namespace

BenchmarkMachineInfo ReadBenchmarkMachine()
{
    BenchmarkMachineInfo info{};
    info.cpuBrand = ReadCpuBrand();

    ProcessorTopology topology{};
    if (ReadProcessorTopology(topology))
    {
        if (topology.sawProcessorCore)
        {
            info.physicalCoreCount = topology.physicalCores;
            info.logicalProcessorCount = topology.logicalProcessors;
        }
        if (topology.sawL3Cache)
        {
            info.l3CacheCount = topology.l3Caches;
            info.l3TotalBytes = topology.l3Bytes;
        }
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = static_cast<DWORD>(sizeof(memory));
    if (GlobalMemoryStatusEx(&memory) != FALSE)
    {
        info.physicalMemoryBytes = memory.ullTotalPhys;
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = static_cast<ULONG>(sizeof(version));
    using ReadVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"); // borrowed; no library load in the benchmark
    const auto readVersion = ntdll ? std::bit_cast<ReadVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (readVersion && readVersion(&version) >= 0)
    {
        info.osMajorVersion = version.dwMajorVersion;
        info.osMinorVersion = version.dwMinorVersion;
        info.osBuildNumber = version.dwBuildNumber;
    }

    info.activePowerSchemeGuid = ReadActivePowerSchemeGuid();
#if defined(_M_ARM64)
    info.processArchitecture = "arm64";
#elif defined(_M_X64)
    info.processArchitecture = "x64";
#else
    info.processArchitecture = "unknown";
#endif

#if defined(__SANITIZE_ADDRESS__)
    info.configuration = "ASan Debug";
#elif defined(_DEBUG)
    info.configuration = "Debug";
#else
    info.configuration = "Release";
#endif

    info.fma3 = ReadFma3Status();
    return info;
}

std::string FormatBenchmarkMachineInfo(const BenchmarkMachineInfo& info)
{
    std::string output;
    output.reserve(384);
    AppendOptionalText(output, "cpu_brand", info.cpuBrand);
    AppendOptionalNumber(output, "physical_cores", info.physicalCoreCount);
    AppendOptionalNumber(output, "logical_processors", info.logicalProcessorCount);
    AppendOptionalNumber(output, "l3_cache_count", info.l3CacheCount);
    AppendOptionalNumber(output, "l3_total_bytes", info.l3TotalBytes);
    AppendOptionalNumber(output, "ccd_count", info.ccdCount);
    AppendOptionalNumber(output, "physical_memory_bytes", info.physicalMemoryBytes);
    AppendOptionalNumber(output, "os_major", info.osMajorVersion);
    AppendOptionalNumber(output, "os_minor", info.osMinorVersion);
    AppendOptionalNumber(output, "os_build", info.osBuildNumber);
    AppendOptionalText(output, "power_scheme_guid", info.activePowerSchemeGuid);
    AppendOptionalText(output, "adapter", info.adapterName);
    AppendOptionalText(output, "driver", info.driverVersion);
    AppendOptionalText(output, "adapter_luid", info.adapterLuid);
    AppendOptionalText(output, "application_version", info.applicationVersion);
    AppendOptionalText(output, "isa", info.isa);
    AppendOptionalNumber(output, "worker_count", info.workerCount);
    output.append("process_architecture=");
    AppendQuoted(output, info.processArchitecture);
    output.append(" configuration=");
    AppendQuoted(output, info.configuration);
    output.append(" fma3=");
    output.append(Fma3Text(info.fma3));
    return output;
}

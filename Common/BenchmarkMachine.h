#pragma once

#include <cstdint>
#include <optional>
#include <string>

// This snapshot is intended for explicit benchmark setup/reporting only. It does not belong in a frame or
// document-processing path. A missing optional value means Windows did not provide a trustworthy value.
struct BenchmarkMachineInfo final
{
    std::optional<std::string> cpuBrand;
    std::optional<std::uint32_t> physicalCoreCount;
    std::optional<std::uint32_t> logicalProcessorCount;
    std::optional<std::uint32_t> l3CacheCount;
    std::optional<std::uint64_t> l3TotalBytes;
    // Windows topology APIs do not identify CCDs. Do not infer CCD count from the L3 cache count.
    std::optional<std::uint32_t> ccdCount;
    std::optional<std::uint64_t> physicalMemoryBytes;
    std::optional<std::uint32_t> osMajorVersion;
    std::optional<std::uint32_t> osMinorVersion;
    std::optional<std::uint32_t> osBuildNumber;
    std::optional<std::string> activePowerSchemeGuid;
    std::string processArchitecture;
    std::string configuration;
    // Filled by the benchmark host from its actual initialized renderer, executable and scheduler.
    std::optional<std::string> adapterName;
    std::optional<std::string> driverVersion;
    std::optional<std::string> adapterLuid;
    std::optional<std::string> applicationVersion;
    std::optional<std::string> isa;
    std::optional<std::uint32_t> workerCount;
    enum class Fma3Status : std::uint8_t
    {
        Enabled,
        Disabled,
        Unknown,
        NotApplicable
    };
    // On x64 this is the CRT's current FMA3 math-library setting, not a CPU capability probe.
    Fma3Status fma3 = Fma3Status::Unknown;
};

[[nodiscard]] BenchmarkMachineInfo ReadBenchmarkMachine();

// Produces one line with quoted/escaped text fields. Missing values are rendered as "unknown".
[[nodiscard]] std::string FormatBenchmarkMachineInfo(const BenchmarkMachineInfo& info);

#include "BenchmarkMachineTests.h"

#include "../../Common/BenchmarkMachine.h"
#include "../../Common/Jobs.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <windows.h>

namespace
{
int g_failures = 0;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s bench-machine.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}
} // namespace

int RunBenchmarkMachineTests()
{
    g_failures = 0;
    BenchmarkMachineInfo fixture;
    fixture.cpuBrand = "test \"CPU\"\n\\path\t\x01";
    fixture.physicalCoreCount = 6;
    fixture.logicalProcessorCount = 12;
    fixture.l3CacheCount = 2;
    fixture.l3TotalBytes = 96 * 1024 * 1024;
    fixture.physicalMemoryBytes = 64ULL * 1024 * 1024 * 1024;
    fixture.osBuildNumber = 26100;
    fixture.applicationVersion = "1.2.3.4";
    fixture.workerCount = 4;
    fixture.fma3 = BenchmarkMachineInfo::Fma3Status::Disabled;
    const auto formatted = FormatBenchmarkMachineInfo(fixture);
    Expect(formatted.find("cpu_brand=\"test \\\"CPU\\\"\\n\\\\path\\t\\x01\"") == 0, "text-fields-escaped");
    Expect(formatted.find('\n') == std::string::npos && formatted.find('\r') == std::string::npos,
           "report-stays-on-one-line");
    Expect(formatted.find("physical_cores=6 logical_processors=12 l3_cache_count=2 l3_total_bytes=100663296") !=
               std::string::npos,
           "independent-topology-values");
    Expect(formatted.find("physical_memory_bytes=68719476736") != std::string::npos,
           "physical-bytes-not-truncated-to-32-bit");
    Expect(formatted.find("ccd_count=unknown") != std::string::npos, "cache-count-not-assumed-to-be-ccd-count");
    Expect(formatted.find("adapter=unknown driver=unknown adapter_luid=unknown") != std::string::npos,
           "missing-renderer-values-explicit");
    Expect(formatted.find("worker_count=4") != std::string::npos && formatted.ends_with("fma3=disabled"),
           "actual-pool-and-crt-policy-fields");

    const auto live = ReadBenchmarkMachine();
    Expect(live.cpuBrand.has_value() && !live.cpuBrand.value().empty(), "host-cpu-brand-readable");
    Expect(live.physicalCoreCount.has_value() && live.physicalCoreCount.value() > 0 &&
               live.logicalProcessorCount.has_value() &&
               live.logicalProcessorCount.value() >= live.physicalCoreCount.value(),
           "host-core-and-logical-counts");
    Expect(live.logicalProcessorCount == GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
           "host-logical-count-independent-api");
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    Expect(GlobalMemoryStatusEx(&memory) && live.physicalMemoryBytes == memory.ullTotalPhys,
           "host-physical-memory-independent-query");
    Expect(live.osMajorVersion == 10u && live.osBuildNumber.has_value() && live.osBuildNumber.value() >= 22000,
           "host-real-windows-build");
    Expect(!live.ccdCount.has_value(), "host-ccd-remains-unknown");
    Expect(live.activePowerSchemeGuid.has_value() && live.activePowerSchemeGuid.value().size() == 38,
           "host-active-power-scheme");
#if defined(_M_X64)
    Expect(live.processArchitecture == "x64" &&
               live.fma3 == (_get_FMA3_enable() != 0 ? BenchmarkMachineInfo::Fma3Status::Enabled
                                                     : BenchmarkMachineInfo::Fma3Status::Disabled),
           "host-architecture-and-current-crt-fma3");
#elif defined(_M_ARM64)
    Expect(live.processArchitecture == "arm64" && live.fma3 == BenchmarkMachineInfo::Fma3Status::NotApplicable,
           "host-architecture-and-fma3-not-applicable");
#endif
#if defined(__SANITIZE_ADDRESS__)
    Expect(live.configuration == "ASan Debug", "host-configuration-asan");
#elif defined(_DEBUG)
    Expect(live.configuration == "Debug", "host-configuration-debug");
#else
    Expect(live.configuration == "Release", "host-configuration-release");
#endif
    JobScheduler scheduler;
    Expect(scheduler.WorkerCount() == 0, "uninitialized-pool-count");
    Expect(scheduler.Initialize(2) == S_OK && scheduler.WorkerCount() == 2, "actual-configured-pool-count");
    scheduler.Close();
    Expect(scheduler.WorkerCount() == 0, "closed-pool-count");
    return g_failures;
}

#pragma once

#include <array>
#include <cstdint>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <windows.h>

namespace GpuCapabilities
{
struct FormatSupport final
{
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    HRESULT queryResult = E_PENDING;
    bool available = false;
    bool typedUavStore = false;
};

struct VideoMemoryInfo final
{
    HRESULT queryResult = E_PENDING;
    bool available = false;
    std::uint64_t budget = 0;
    std::uint64_t currentUsage = 0;
    std::uint64_t availableForReservation = 0;
    std::uint64_t currentReservation = 0;
};

// A value-only snapshot. Capability booleans are meaningful only when their corresponding query is available.
struct Report final
{
    HRESULT device5QueryResult = E_PENDING;
    bool device5Available = false;
    HRESULT context4QueryResult = E_PENDING;
    bool context4Available = false;
    HRESULT createFenceResult = E_PENDING;
    bool createFenceAvailable = false;

    HRESULT adapter4QueryResult = E_PENDING;
    bool adapter4Available = false;
    HRESULT adapterDescriptionResult = E_PENDING;
    bool adapterDescriptionAvailable = false;
    bool monitoredFencesSupported = false;
    HRESULT dxgiDevice2QueryResult = E_PENDING;
    bool dxgiDevice2Available = false;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t dedicatedSystemMemory = 0;
    std::uint64_t sharedSystemMemory = 0;

    HRESULT options2QueryResult = E_PENDING;
    bool options2Available = false;
    bool typedUavLoadAdditionalFormats = false;
    bool unifiedMemoryArchitecture = false;
    bool mapOnDefaultTextures = false;

    HRESULT threadingQueryResult = E_PENDING;
    bool threadingAvailable = false;
    bool driverConcurrentCreates = false;

    std::array<FormatSupport, 4> formats{};

    HRESULT minPrecisionQueryResult = E_PENDING;
    bool minPrecisionAvailable = false;
    std::uint32_t pixelShaderMinPrecision = 0;
    std::uint32_t allOtherShaderStagesMinPrecision = 0;

    VideoMemoryInfo localVideoMemory{};
    VideoMemoryInfo nonLocalVideoMemory{};
};

enum class CompletionRoute : std::uint8_t
{
    None,
    MonitoredFence,
    EnqueueSetEvent
};

// Selects a completion mechanism from an immutable value snapshot.
[[nodiscard]] CompletionRoute SelectCompletionRoute(const Report& report) noexcept;

// Borrows both input interfaces. It performs capability queries only and owns no persistent graphics resources.
HRESULT Probe(ID3D11Device* device, ID3D11DeviceContext* context, Report& report) noexcept;
} // namespace GpuCapabilities

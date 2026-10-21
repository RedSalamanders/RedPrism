#include "Composition.Tests.GpuCapability.h"

#include "GpuCapabilities.h"

#include <array>
#include <cstdio>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
struct RouteCase final
{
    bool device5;
    bool context4;
    bool createFence;
    bool adapterDescription;
    bool monitoredFences;
    bool dxgiDevice2;
    GpuCapabilities::CompletionRoute expected;
    const char* name;
};

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s gpu-capability.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++failures;
    }
}

[[nodiscard]] HRESULT SyntheticStatus(bool available) noexcept
{
    return available ? S_OK : E_NOINTERFACE;
}

[[nodiscard]] GpuCapabilities::Report MakeRouteReport(const RouteCase& test) noexcept
{
    GpuCapabilities::Report report{};
    report.device5Available = test.device5;
    report.device5QueryResult = SyntheticStatus(test.device5);
    report.context4Available = test.context4;
    report.context4QueryResult = SyntheticStatus(test.context4);
    report.createFenceAvailable = test.createFence;
    report.createFenceResult = SyntheticStatus(test.createFence);
    report.adapterDescriptionAvailable = test.adapterDescription;
    report.adapter4Available = test.adapterDescription;
    report.adapter4QueryResult = SyntheticStatus(test.adapterDescription);
    report.adapterDescriptionResult = SyntheticStatus(test.adapterDescription);
    report.monitoredFencesSupported = test.monitoredFences;
    report.dxgiDevice2Available = test.dxgiDevice2;
    report.dxgiDevice2QueryResult = SyntheticStatus(test.dxgiDevice2);
    return report;
}

HRESULT MakeWarp(wil::com_ptr_nothrow<ID3D11Device>& device,
                 wil::com_ptr_nothrow<ID3D11DeviceContext>& context) noexcept
{
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL usedLevel = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, device.put(), &usedLevel, context.put());
}

[[nodiscard]] bool IsResetReport(const GpuCapabilities::Report& report) noexcept
{
    return report.device5QueryResult == E_PENDING && !report.device5Available &&
           report.context4QueryResult == E_PENDING && !report.context4Available &&
           report.createFenceResult == E_PENDING && !report.createFenceAvailable &&
           report.dxgiDevice2QueryResult == E_PENDING && !report.dxgiDevice2Available &&
           report.formats[0].format == DXGI_FORMAT_R16G16B16A16_FLOAT && report.formats[0].queryResult == E_PENDING &&
           !report.formats[0].available;
}
} // namespace

int RunGpuCapabilityTests()
{
    int failures = 0;
    constexpr std::array<RouteCase, 9> routeCases = {{
        {true, true, true, true, true, false, GpuCapabilities::CompletionRoute::MonitoredFence,
         "route.monitored-fence"},
        {false, true, true, true, true, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.no-device5-fallback"},
        {true, false, true, true, true, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.no-context4-fallback"},
        {true, true, false, true, true, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.no-fence-fallback"},
        {true, true, true, false, true, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.no-description-fallback"},
        {true, true, true, true, false, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.unsupported-fallback"},
        {false, false, false, false, false, true, GpuCapabilities::CompletionRoute::EnqueueSetEvent,
         "route.event-only"},
        {false, false, false, false, false, false, GpuCapabilities::CompletionRoute::None, "route.none"},
        {true, true, true, true, true, false, GpuCapabilities::CompletionRoute::MonitoredFence,
         "route.fence-precedes-event"},
    }};

    for (const RouteCase& test : routeCases)
    {
        const GpuCapabilities::Report report = MakeRouteReport(test);
        Expect(GpuCapabilities::SelectCompletionRoute(report) == test.expected, test.name, failures);
    }

    // Availability alone cannot override a failed HRESULT in a synthetic or partially populated report.
    GpuCapabilities::Report failedQuery = MakeRouteReport(routeCases[0]);
    failedQuery.createFenceResult = E_FAIL;
    Expect(GpuCapabilities::SelectCompletionRoute(failedQuery) == GpuCapabilities::CompletionRoute::None,
           "route.failed-fence-hresult", failures);
    failedQuery = MakeRouteReport(routeCases[6]);
    failedQuery.dxgiDevice2QueryResult = E_NOINTERFACE;
    Expect(GpuCapabilities::SelectCompletionRoute(failedQuery) == GpuCapabilities::CompletionRoute::None,
           "route.failed-device2-hresult", failures);
    failedQuery = MakeRouteReport(routeCases[0]);
    failedQuery.adapter4Available = false;
    Expect(GpuCapabilities::SelectCompletionRoute(failedQuery) == GpuCapabilities::CompletionRoute::None,
           "route.no-adapter4-refuses-fence", failures);
    failedQuery.adapter4Available = true;
    failedQuery.adapter4QueryResult = E_NOINTERFACE;
    Expect(GpuCapabilities::SelectCompletionRoute(failedQuery) == GpuCapabilities::CompletionRoute::None,
           "route.failed-adapter4-hresult", failures);

    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediateContext;
    HRESULT result = MakeWarp(device, immediateContext);
    Expect(SUCCEEDED(result), "probe.warp-created", failures);
    if (FAILED(result))
    {
        return failures;
    }

    GpuCapabilities::Report report{};
    result = GpuCapabilities::Probe(device.get(), immediateContext.get(), report);
    Expect(SUCCEEDED(result), "probe.warp-immediate-context", failures);
    const auto route = GpuCapabilities::SelectCompletionRoute(report);
    std::printf("GPU CAPABILITIES WARP route=%s device5=%u context4=%u fence=%u monitored=%u dxgi2=%u\n",
                route == GpuCapabilities::CompletionRoute::MonitoredFence    ? "monitored-fence"
                : route == GpuCapabilities::CompletionRoute::EnqueueSetEvent ? "enqueue-event"
                                                                             : "none",
                static_cast<unsigned int>(report.device5Available), static_cast<unsigned int>(report.context4Available),
                static_cast<unsigned int>(report.createFenceAvailable),
                static_cast<unsigned int>(report.monitoredFencesSupported),
                static_cast<unsigned int>(report.dxgiDevice2Available));

    wil::com_ptr_nothrow<ID3D11DeviceContext> deferredContext;
    result = device->CreateDeferredContext(0, deferredContext.put());
    Expect(SUCCEEDED(result), "probe.deferred-context-created", failures);
    if (SUCCEEDED(result))
    {
        report.device5Available = true;
        report.dxgiDevice2Available = true;
        result = GpuCapabilities::Probe(device.get(), deferredContext.get(), report);
        Expect(result == E_INVALIDARG && IsResetReport(report), "probe.deferred-refused-before-report", failures);
    }

    GpuCapabilities::Report nullDeviceReport{};
    result = GpuCapabilities::Probe(nullptr, immediateContext.get(), nullDeviceReport);
    Expect(result == E_INVALIDARG && IsResetReport(nullDeviceReport), "probe.null-device", failures);
    GpuCapabilities::Report nullContextReport{};
    result = GpuCapabilities::Probe(device.get(), nullptr, nullContextReport);
    Expect(result == E_INVALIDARG && IsResetReport(nullContextReport), "probe.null-context", failures);

    wil::com_ptr_nothrow<ID3D11Device> otherDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> otherContext;
    result = MakeWarp(otherDevice, otherContext);
    Expect(SUCCEEDED(result), "probe.second-warp-created", failures);
    if (SUCCEEDED(result))
    {
        report.device5Available = true;
        report.dxgiDevice2Available = true;
        result = GpuCapabilities::Probe(device.get(), otherContext.get(), report);
        Expect(result == E_INVALIDARG && IsResetReport(report), "probe.mismatched-device-refused", failures);
    }

    report.device5Available = true;
    report.dxgiDevice2Available = true;
    result = GpuCapabilities::Probe(device.get(), immediateContext.get(), report);
    const bool resetThenRepopulated =
        SUCCEEDED(result) && report.device5QueryResult != E_PENDING && report.dxgiDevice2QueryResult != E_PENDING;
    Expect(resetThenRepopulated, "probe.repeated-report-reset", failures);
    return failures;
}

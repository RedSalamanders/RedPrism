#include "GpuCapabilities.h"

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace GpuCapabilities
{
namespace
{
[[nodiscard]] bool IsImmediateContextForDevice(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
{
    if (device == nullptr || context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
    {
        return false;
    }

    wil::com_ptr_nothrow<ID3D11Device> contextDevice;
    context->GetDevice(contextDevice.put());
    if (!contextDevice)
    {
        return false;
    }

    wil::com_ptr_nothrow<IUnknown> inputIdentity;
    wil::com_ptr_nothrow<IUnknown> contextIdentity;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(inputIdentity.put()))) ||
        FAILED(contextDevice->QueryInterface(IID_PPV_ARGS(contextIdentity.put()))))
    {
        return false;
    }

    return inputIdentity.get() == contextIdentity.get();
}

void QueryVideoMemory(IDXGIAdapter3* adapter, DXGI_MEMORY_SEGMENT_GROUP segment, VideoMemoryInfo& result) noexcept
{
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    const HRESULT queryResult = adapter->QueryVideoMemoryInfo(0, segment, &info);
    result = VideoMemoryInfoFromQuery(queryResult, info);
}
} // namespace

VideoMemoryInfo VideoMemoryInfoFromQuery(HRESULT queryResult, const DXGI_QUERY_VIDEO_MEMORY_INFO& info) noexcept
{
    VideoMemoryInfo result{};
    result.queryResult = queryResult;
    result.available = SUCCEEDED(queryResult);
    if (result.available)
    {
        result.budget = info.Budget;
        result.currentUsage = info.CurrentUsage;
        result.availableForReservation = info.AvailableForReservation;
        result.currentReservation = info.CurrentReservation;
    }
    return result;
}

CompletionRoute SelectCompletionRoute(const Report& report) noexcept
{
    const bool monitoredFenceReady =
        report.device5Available && SUCCEEDED(report.device5QueryResult) && report.context4Available &&
        SUCCEEDED(report.context4QueryResult) && report.createFenceAvailable && SUCCEEDED(report.createFenceResult) &&
        report.adapter4Available && SUCCEEDED(report.adapter4QueryResult) && report.adapterDescriptionAvailable &&
        SUCCEEDED(report.adapterDescriptionResult) && report.monitoredFencesSupported;
    if (monitoredFenceReady)
    {
        return CompletionRoute::MonitoredFence;
    }

    if (report.dxgiDevice2Available && SUCCEEDED(report.dxgiDevice2QueryResult))
    {
        return CompletionRoute::EnqueueSetEvent;
    }

    return CompletionRoute::None;
}

HRESULT Probe(ID3D11Device* device, ID3D11DeviceContext* context, Report& report) noexcept
{
    report = {};
    report.formats[0].format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    report.formats[1].format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    report.formats[2].format = DXGI_FORMAT_R32_FLOAT;
    report.formats[3].format = DXGI_FORMAT_R16_UINT;

    if (device == nullptr || context == nullptr)
    {
        return E_INVALIDARG;
    }

    if (!IsImmediateContextForDevice(device, context))
    {
        return E_INVALIDARG;
    }

    wil::com_ptr_nothrow<ID3D11Device5> device5;
    report.device5QueryResult = device->QueryInterface(IID_PPV_ARGS(device5.put()));
    report.device5Available = SUCCEEDED(report.device5QueryResult);
    if (report.device5Available)
    {
        wil::com_ptr_nothrow<ID3D11Fence> fence;
        report.createFenceResult = device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
        report.createFenceAvailable = SUCCEEDED(report.createFenceResult);
    }
    else
    {
        report.createFenceResult = report.device5QueryResult;
    }

    wil::com_ptr_nothrow<ID3D11DeviceContext4> context4;
    report.context4QueryResult = context->QueryInterface(IID_PPV_ARGS(context4.put()));
    report.context4Available = SUCCEEDED(report.context4QueryResult);

    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};
    report.options2QueryResult = device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2, sizeof(options2));
    report.options2Available = SUCCEEDED(report.options2QueryResult);
    if (report.options2Available)
    {
        report.typedUavLoadAdditionalFormats = options2.TypedUAVLoadAdditionalFormats != FALSE;
        report.unifiedMemoryArchitecture = options2.UnifiedMemoryArchitecture != FALSE;
        report.mapOnDefaultTextures = options2.MapOnDefaultTextures != FALSE;
    }

    D3D11_FEATURE_DATA_THREADING threading{};
    report.threadingQueryResult = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    report.threadingAvailable = SUCCEEDED(report.threadingQueryResult);
    if (report.threadingAvailable)
    {
        report.driverConcurrentCreates = threading.DriverConcurrentCreates != FALSE;
    }

    for (FormatSupport& format : report.formats)
    {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{};
        support.InFormat = format.format;
        format.queryResult = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support));
        format.available = SUCCEEDED(format.queryResult);
        if (format.available)
        {
            format.typedUavStore = (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
        }
    }

    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT minPrecision{};
    report.minPrecisionQueryResult =
        device->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &minPrecision, sizeof(minPrecision));
    report.minPrecisionAvailable = SUCCEEDED(report.minPrecisionQueryResult);
    if (report.minPrecisionAvailable)
    {
        report.pixelShaderMinPrecision = minPrecision.PixelShaderMinPrecision;
        report.allOtherShaderStagesMinPrecision = minPrecision.AllOtherShaderStagesMinPrecision;
    }

    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    HRESULT result = device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(result))
    {
        return result;
    }
    if (!adapter)
    {
        return E_NOINTERFACE;
    }

    wil::com_ptr_nothrow<IDXGIDevice2> dxgiDevice2;
    report.dxgiDevice2QueryResult = dxgiDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice2.put()));
    report.dxgiDevice2Available = SUCCEEDED(report.dxgiDevice2QueryResult);

    wil::com_ptr_nothrow<IDXGIAdapter4> adapter4;
    report.adapter4QueryResult = adapter->QueryInterface(IID_PPV_ARGS(adapter4.put()));
    report.adapter4Available = SUCCEEDED(report.adapter4QueryResult);
    if (!report.adapter4Available)
    {
        report.adapterDescriptionResult = report.adapter4QueryResult;
        report.localVideoMemory.queryResult = report.adapter4QueryResult;
        report.nonLocalVideoMemory.queryResult = report.adapter4QueryResult;
        return S_OK;
    }

    DXGI_ADAPTER_DESC3 description{};
    report.adapterDescriptionResult = adapter4->GetDesc3(&description);
    report.adapterDescriptionAvailable = SUCCEEDED(report.adapterDescriptionResult);
    if (report.adapterDescriptionAvailable)
    {
        report.monitoredFencesSupported = (description.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES) != 0;
        report.dedicatedVideoMemory = description.DedicatedVideoMemory;
        report.dedicatedSystemMemory = description.DedicatedSystemMemory;
        report.sharedSystemMemory = description.SharedSystemMemory;
    }

    QueryVideoMemory(adapter4.get(), DXGI_MEMORY_SEGMENT_GROUP_LOCAL, report.localVideoMemory);
    QueryVideoMemory(adapter4.get(), DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, report.nonLocalVideoMemory);
    return S_OK;
}

HRESULT QueryVideoMemorySnapshot(ID3D11Device* device, VideoMemorySnapshot& snapshot) noexcept
{
    snapshot = {};
    if (device == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT result = device->GetDeviceRemovedReason();
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    result = device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(result))
    {
        return result;
    }
    if (!adapter)
    {
        return E_NOINTERFACE;
    }

    wil::com_ptr_nothrow<IDXGIAdapter3> adapter3;
    result = adapter->QueryInterface(IID_PPV_ARGS(adapter3.put()));
    if (FAILED(result))
    {
        return result;
    }

    QueryVideoMemory(adapter3.get(), DXGI_MEMORY_SEGMENT_GROUP_LOCAL, snapshot.local);
    QueryVideoMemory(adapter3.get(), DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, snapshot.nonLocal);

    result = device->GetDeviceRemovedReason();
    if (FAILED(result))
    {
        snapshot = {};
        return result;
    }
    return S_OK;
}
} // namespace GpuCapabilities

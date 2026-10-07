#include "Composition.Tests.VideoMemorySnapshot.h"

#include "GpuCapabilities.h"

#include <cstdio>
#include <type_traits>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s video-memory-snapshot.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

void PrintUnavailable(const char* name, HRESULT result) noexcept
{
    std::printf("INFO video-memory-snapshot.%s unavailable hr=0x%08lx\n", name, static_cast<unsigned long>(result));
}

bool HasZeroFields(const GpuCapabilities::VideoMemoryInfo& info) noexcept
{
    return info.budget == 0 && info.currentUsage == 0 && info.availableForReservation == 0 &&
           info.currentReservation == 0;
}

bool IsReset(const GpuCapabilities::VideoMemorySnapshot& snapshot) noexcept
{
    return snapshot.local.queryResult == E_PENDING && !snapshot.local.available && HasZeroFields(snapshot.local) &&
           snapshot.nonLocal.queryResult == E_PENDING && !snapshot.nonLocal.available &&
           HasZeroFields(snapshot.nonLocal);
}

bool SameValue(const GpuCapabilities::VideoMemoryInfo& left, const GpuCapabilities::VideoMemoryInfo& right) noexcept
{
    return left.queryResult == right.queryResult && left.available == right.available && left.budget == right.budget &&
           left.currentUsage == right.currentUsage && left.availableForReservation == right.availableForReservation &&
           left.currentReservation == right.currentReservation;
}

void TestValueMapping(int& failures) noexcept
{
    DXGI_QUERY_VIDEO_MEMORY_INFO values{};
    values.Budget = 0x123456789ABCDEF0ULL;
    values.CurrentUsage = 0x23456789ABCDEF01ULL;
    values.AvailableForReservation = 0x3456789ABCDEF012ULL;
    values.CurrentReservation = 0x456789ABCDEF0123ULL;
    const auto success = GpuCapabilities::VideoMemoryInfoFromQuery(S_OK, values);
    Expect(success.queryResult == S_OK && success.available, "value.success-status", failures);
    Expect(success.budget == 0x123456789ABCDEF0ULL, "value.exact-budget", failures);
    Expect(success.currentUsage == 0x23456789ABCDEF01ULL, "value.exact-current-usage", failures);
    Expect(success.availableForReservation == 0x3456789ABCDEF012ULL, "value.exact-available-reservation", failures);
    Expect(success.currentReservation == 0x456789ABCDEF0123ULL, "value.exact-current-reservation", failures);

    DXGI_QUERY_VIDEO_MEMORY_INFO zeroBudget = values;
    zeroBudget.Budget = 0;
    const auto zero = GpuCapabilities::VideoMemoryInfoFromQuery(S_OK, zeroBudget);
    Expect(zero.queryResult == S_OK && zero.available && zero.budget == 0, "value.zero-budget-available", failures);
    Expect(zero.currentUsage == 0x23456789ABCDEF01ULL && zero.availableForReservation == 0x3456789ABCDEF012ULL &&
               zero.currentReservation == 0x456789ABCDEF0123ULL,
           "value.zero-budget-keeps-other-fields", failures);

    const auto nonzeroSuccess = GpuCapabilities::VideoMemoryInfoFromQuery(S_FALSE, values);
    Expect(nonzeroSuccess.queryResult == S_FALSE && nonzeroSuccess.available, "value.s-false-available", failures);
    Expect(nonzeroSuccess.budget == 0x123456789ABCDEF0ULL && nonzeroSuccess.currentUsage == 0x23456789ABCDEF01ULL &&
               nonzeroSuccess.availableForReservation == 0x3456789ABCDEF012ULL &&
               nonzeroSuccess.currentReservation == 0x456789ABCDEF0123ULL,
           "value.s-false-exact-fields", failures);

    // Every input field is poisoned. Failure must preserve only the HRESULT, regardless of written API output.
    DXGI_QUERY_VIDEO_MEMORY_INFO poison{};
    poison.Budget = 0xF123456789ABCDEFULL;
    poison.CurrentUsage = 0xE23456789ABCDEF0ULL;
    poison.AvailableForReservation = 0xD3456789ABCDEF01ULL;
    poison.CurrentReservation = 0xC456789ABCDEF012ULL;
    const auto failed = GpuCapabilities::VideoMemoryInfoFromQuery(DXGI_ERROR_UNSUPPORTED, poison);
    Expect(failed.queryResult == DXGI_ERROR_UNSUPPORTED, "value.failure-original-hresult", failures);
    Expect(!failed.available, "value.failure-unavailable", failures);
    Expect(HasZeroFields(failed), "value.failure-poison-zeroed", failures);
}

HRESULT MakeWarpDevice(wil::com_ptr_nothrow<ID3D11Device>& device) noexcept
{
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL usedLevel = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, device.put(), &usedLevel, nullptr);
}

HRESULT ReadAdapter3(ID3D11Device* device, wil::com_ptr_nothrow<IDXGIAdapter3>& adapter3) noexcept
{
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
    return adapter->QueryInterface(IID_PPV_ARGS(adapter3.put()));
}

void CheckActualSegment(const GpuCapabilities::VideoMemoryInfo& actual, HRESULT independentResult, bool local,
                        int& failures) noexcept
{
    Expect(actual.queryResult != E_PENDING && actual.available == SUCCEEDED(actual.queryResult),
           local ? "warp.local.internal-status" : "warp.nonlocal.internal-status", failures);
    Expect(actual.available == SUCCEEDED(independentResult),
           local ? "warp.local.independent-query-support" : "warp.nonlocal.independent-query-support", failures);
    if (FAILED(independentResult))
    {
        PrintUnavailable(local ? "warp.local.unsupported" : "warp.nonlocal.unsupported", independentResult);
        Expect(actual.queryResult == independentResult,
               local ? "warp.local.unsupported-original-hresult" : "warp.nonlocal.unsupported-original-hresult",
               failures);
    }
    if (FAILED(actual.queryResult))
    {
        Expect(HasZeroFields(actual),
               local ? "warp.local.unavailable-fields-zero" : "warp.nonlocal.unavailable-fields-zero", failures);
    }
    // Numeric fields are live observations at different instants. Only the pure mapping tests compare values.
}

void PoisonSnapshot(GpuCapabilities::VideoMemorySnapshot& snapshot) noexcept
{
    snapshot.local = {
        S_FALSE, true, 0xF123456789ABCDEFULL, 0xE23456789ABCDEF0ULL, 0xD3456789ABCDEF01ULL, 0xC456789ABCDEF012ULL};
    snapshot.nonLocal = {
        E_FAIL, true, 0xB56789ABCDEF0123ULL, 0xA6789ABCDEF01234ULL, 0x9789ABCDEF012345ULL, 0x889ABCDEF0123456ULL};
}
} // namespace

int RunVideoMemorySnapshotTests()
{
    static_assert(std::is_trivially_copyable_v<GpuCapabilities::VideoMemorySnapshot>);
    int failures = 0;
    TestValueMapping(failures);

    GpuCapabilities::VideoMemorySnapshot snapshot{};
    PoisonSnapshot(snapshot);
    const HRESULT nullResult = GpuCapabilities::QueryVideoMemorySnapshot(nullptr, snapshot);
    Expect(nullResult == E_INVALIDARG && IsReset(snapshot), "null-device-resets-all-fields", failures);

    wil::com_ptr_nothrow<ID3D11Device> device;
    const HRESULT deviceResult = MakeWarpDevice(device);
    Expect(SUCCEEDED(deviceResult) && device, "warp.device-created-without-context", failures);
    if (FAILED(deviceResult) || !device)
    {
        return failures;
    }

    wil::com_ptr_nothrow<IDXGIAdapter3> independentAdapter3;
    const HRESULT adapterResult = ReadAdapter3(device.get(), independentAdapter3);
    if (FAILED(adapterResult))
    {
        PrintUnavailable("warp.adapter3.unsupported", adapterResult);
        PoisonSnapshot(snapshot);
        const HRESULT result = GpuCapabilities::QueryVideoMemorySnapshot(device.get(), snapshot);
        Expect(result == adapterResult, "warp.adapter3.unavailable-original-hresult", failures);
        Expect(IsReset(snapshot), "warp.adapter3.unavailable-snapshot-reset", failures);
        return failures;
    }
    Expect(independentAdapter3.get() != nullptr, "warp.actual-device-adapter3-independently-acquired", failures);
    if (!independentAdapter3)
    {
        return failures;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO independentLocal{};
    DXGI_QUERY_VIDEO_MEMORY_INFO independentNonLocal{};
    const HRESULT localResult =
        independentAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &independentLocal);
    const HRESULT nonLocalResult =
        independentAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &independentNonLocal);
    PoisonSnapshot(snapshot);
    const HRESULT queryResult = GpuCapabilities::QueryVideoMemorySnapshot(device.get(), snapshot);
    if (FAILED(queryResult))
    {
        PrintUnavailable("warp.fresh-api.unexpected-failure", queryResult);
        Expect(false, "warp.fresh-api-unexpected-failure", failures);
        Expect(IsReset(snapshot), "warp.fresh-api-failure-reset", failures);
        return failures;
    }
    Expect(queryResult == S_OK, "warp.fresh-api-both-queries-attempted", failures);
    std::printf("INFO video-memory-snapshot.warp.local hr=0x%08lx budget=%llu usage=%llu "
                "available_reservation=%llu reservation=%llu\n",
                static_cast<unsigned long>(snapshot.local.queryResult),
                static_cast<unsigned long long>(snapshot.local.budget),
                static_cast<unsigned long long>(snapshot.local.currentUsage),
                static_cast<unsigned long long>(snapshot.local.availableForReservation),
                static_cast<unsigned long long>(snapshot.local.currentReservation));
    std::printf("INFO video-memory-snapshot.warp.nonlocal hr=0x%08lx budget=%llu usage=%llu "
                "available_reservation=%llu reservation=%llu\n",
                static_cast<unsigned long>(snapshot.nonLocal.queryResult),
                static_cast<unsigned long long>(snapshot.nonLocal.budget),
                static_cast<unsigned long long>(snapshot.nonLocal.currentUsage),
                static_cast<unsigned long long>(snapshot.nonLocal.availableForReservation),
                static_cast<unsigned long long>(snapshot.nonLocal.currentReservation));
    CheckActualSegment(snapshot.local, localResult, true, failures);
    CheckActualSegment(snapshot.nonLocal, nonLocalResult, false, failures);

    PoisonSnapshot(snapshot);
    const HRESULT refreshResult = GpuCapabilities::QueryVideoMemorySnapshot(device.get(), snapshot);
    if (FAILED(refreshResult))
    {
        PrintUnavailable("warp.refresh-api.unexpected-failure", refreshResult);
        Expect(false, "warp.refresh-api-unexpected-failure", failures);
        Expect(IsReset(snapshot), "warp.refresh-api-failure-reset", failures);
        return failures;
    }
    Expect(refreshResult == S_OK, "warp.refresh-api-both-queries-attempted", failures);
    Expect(snapshot.local.queryResult != E_PENDING && snapshot.local.available == SUCCEEDED(snapshot.local.queryResult),
           "warp.refresh-local-status", failures);
    Expect(snapshot.nonLocal.queryResult != E_PENDING &&
               snapshot.nonLocal.available == SUCCEEDED(snapshot.nonLocal.queryResult),
           "warp.refresh-nonlocal-status", failures);
    if (!snapshot.local.available)
    {
        PrintUnavailable("warp.refresh-local.unsupported", snapshot.local.queryResult);
        Expect(HasZeroFields(snapshot.local), "warp.refresh-local-unavailable-fields-zero", failures);
    }
    if (!snapshot.nonLocal.available)
    {
        PrintUnavailable("warp.refresh-nonlocal.unsupported", snapshot.nonLocal.queryResult);
        Expect(HasZeroFields(snapshot.nonLocal), "warp.refresh-nonlocal-unavailable-fields-zero", failures);
    }

    const GpuCapabilities::VideoMemorySnapshot retainedValue = snapshot;
    device.reset();
    independentAdapter3.reset();
    Expect(SameValue(retainedValue.local, snapshot.local) && SameValue(retainedValue.nonLocal, snapshot.nonLocal),
           "snapshot-values-outlive-device-and-adapter", failures);
    return failures;
}

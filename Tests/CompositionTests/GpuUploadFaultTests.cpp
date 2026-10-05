#include "GpuUploadFaultTests.h"

#include "Document.h"
#include "GpuUploadWork.h"
#include "OwnerWorkMultiplexer.h"
#include "RasterUploadWork.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
void Expect(bool condition, const char* label, unsigned int mode) noexcept
{
    std::printf("%s gpu-upload-fault.%u.%s\n", condition ? "PASS" : "FAIL", mode, label);
    gFailures += condition ? 0 : 1;
}
bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {8ULL << 30, 8ULL << 30};
    return true;
}
struct Receiver final : GpuUploadReceiver
{
    GpuUploadRows rows;
    unsigned int deliveries = 0;
    unsigned int completions = 0;
    HRESULT result = E_PENDING;
    JobTicket ticket;
    void WritableRowsReady(JobTicket selected, GpuUploadRows writable) noexcept override
    {
        ticket = selected;
        rows = writable;
        ++deliveries;
    }
    void UploadFinished(JobTicket selected, HRESULT error) noexcept override
    {
        ticket = selected;
        result = error;
        ++completions;
    }
};
template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate predicate) noexcept
{
    const auto started = GetTickCount64();
    while (!predicate())
    {
        (void)DrainCompletions(scheduler);
        if (predicate())
            return true;
        if (scheduler.HasUiWork())
            continue;
        const auto elapsed = GetTickCount64() - started;
        if (elapsed >= 10000)
            return false;
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(10000 - elapsed)) !=
            WAIT_OBJECT_0)
            return false;
    }
    return true;
}
// Numeric verification only, after acknowledgement. Production uploads never use this blocking read path.
bool TargetMatches(ID3D11Device& device, ID3D11DeviceContext& context, ID3D11Texture2D& target, bool copied) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    target.GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, staging.put())))
        return false;
    context.CopyResource(staging.get(), &target);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { context.Unmap(staging.get(), 0); });
    if (!mapped.pData || mapped.RowPitch < 4)
        return false;
    constexpr std::array<std::uint8_t, 4> literal{7, 17, 29, 41};
    for (unsigned int y = 0; y < 4; ++y)
        for (unsigned int x = 0; x < 4; ++x)
        {
            const auto expected =
                copied && x >= 1 && x <= 2 && y >= 1 && y <= 2 ? literal[(y - 1) * 2 + x - 1] : std::uint8_t{53};
            const auto* row =
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            if (row[x] != expected)
                return false;
        }
    return true;
}
bool FillOnWorker(GpuUploadQueue& queue, Receiver& receiver, const Raster& source) noexcept
{
    const DWORD owner = GetCurrentThreadId();
    std::atomic<bool> copied{false};
    std::atomic<bool> acknowledged{false};
    std::atomic<bool> offOwner{false};
    std::jthread worker(
        [&]() noexcept
        {
            offOwner.store(GetCurrentThreadId() != owner);
            copied.store(CopyRasterRowsForUpload(source, 0, 0, 2, 2, receiver.rows.data, receiver.rows.capacity,
                                                 receiver.rows.rowPitch) == RasterUploadWorkStatus::Ready);
            acknowledged.store(
                queue.PublishFilled(receiver.rows.lease, copied.load() ? GpuUploadFill::Ready : GpuUploadFill::Failed));
        });
    worker.join(); // Test-owned worker joins; never a production owner callback or responsiveness claim.
    return copied.load() && acknowledged.load() && offOwner.load();
}
} // namespace

int RunGpuUploadFaultTests()
{
    gFailures = 0;
    struct Case final
    {
        GpuUploadFailurePoint point;
        bool busy;
        bool lent;
        bool quarantined;
        HRESULT expected;
    };
    const std::array cases{
        Case{GpuUploadFailurePoint::StagingCreation, false, false, false, E_OUTOFMEMORY},
        Case{GpuUploadFailurePoint::Map, false, false, false, E_ACCESSDENIED},
        Case{GpuUploadFailurePoint::MappedPitch, false, false, false, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)},
        Case{GpuUploadFailurePoint::WaitRearm, false, true, false, E_ACCESSDENIED},
        Case{GpuUploadFailurePoint::WaitRearm, true, false, true, E_ACCESSDENIED},
        Case{GpuUploadFailurePoint::RecoverySignal, true, false, true, E_ACCESSDENIED},
        Case{GpuUploadFailurePoint::CopySignal, false, true, true, E_ACCESSDENIED}};
    for (unsigned int mode = 0; mode < cases.size(); ++mode)
    {
        const auto selected = cases[mode];
        JobScheduler scheduler;
        Expect(SUCCEEDED(scheduler.Initialize(1, {}, {StableMemory, nullptr})), "scheduler-ready", mode);
        if (!scheduler.WakeHandle())
            continue;
        OwnerWorkMultiplexer routes;
        const bool registered = scheduler.RegisterOwnerWork(routes);
        Expect(registered, "shared-owner-route-ready", mode);
        if (!registered)
        {
            scheduler.Close();
            continue;
        }
        GpuUploadQueue queue;
        wil::com_ptr_nothrow<ID3D11Device> device;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT created =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                              D3D11_SDK_VERSION, device.put(), nullptr, context.put());
        Expect(SUCCEEDED(created) && SUCCEEDED(queue.Initialize(scheduler, &routes)) &&
                   SUCCEEDED(queue.Bind(device.get(), context.get(), true)),
               "event-upload-route-ready", mode);
        if (FAILED(created) || queue.DeviceGeneration() == 0)
        {
            queue.StopNotifications();
            queue.CloseAfterWorkers();
            (void)scheduler.UnregisterOwnerWork(routes);
            scheduler.Close();
            continue;
        }
        Receiver first;
        Receiver second;
        // Receivers and target owners outlive every callback, including the explicit joined close below.
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                queue.StopNotifications();
                queue.CloseAfterWorkers();
                (void)scheduler.UnregisterOwnerWork(routes);
                scheduler.Close();
            });
        constexpr std::array<std::uint8_t, 4> literal{7, 17, 29, 41};
        const auto source = Raster::Make(2, 2, RasterFormat::Gray8, literal.data(), literal.size());
        std::array<std::uint8_t, 16> initial{};
        initial.fill(53);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 4;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{initial.data(), 4, 0};
        wil::com_ptr_nothrow<ID3D11Texture2D> target;
        Expect(SUCCEEDED(device->CreateTexture2D(&desc, &data, target.put())), "literal-target-ready", mode);
        if (!target)
            continue;
        queue.SetFailureForTest(
            selected.point, selected.point == GpuUploadFailurePoint::StagingCreation ? E_OUTOFMEMORY : E_ACCESSDENIED);
        if (selected.busy)
            queue.SetMapBusyForTest(1);
        const JobTicket ticket{0, 1};
        Expect(SUCCEEDED(queue.Enqueue(target.get(), 0, 1, 1, 2, 2, ticket, first)), "fault-request-admitted", mode);
        Expect(Until(scheduler, [&]() noexcept { return first.deliveries != 0 || first.completions != 0; }),
               "event-or-owner-delivery-settles", mode);
        Expect(first.deliveries == (selected.lent ? 1U : 0U), "only-valid-map-lends-rows", mode);
        if (selected.lent && first.deliveries != 0)
        {
            Expect(FillOnWorker(queue, first, source), "mapped-fill-and-acknowledgement-off-owner", mode);
            Expect(!queue.PublishFilled(first.rows.lease, GpuUploadFill::Ready), "duplicate-fill-refuses", mode);
        }
        Expect(Until(scheduler, [&]() noexcept { return first.completions == 1; }) &&
                   first.result == selected.expected && first.ticket == ticket && queue.FailureHitsForTest() == 1,
               "one-exact-local-failure-after-acknowledgement", mode);
        Expect(!queue.HasPending() && !queue.HasReady() && queue.InFlightCount() == 0 && queue.LentCount() == 0,
               "failed-request-has-no-spin-or-mapped-loan", mode);
        Expect(TargetMatches(*device.get(), *context.get(), *target.get(),
                             selected.point == GpuUploadFailurePoint::CopySignal),
               "precopy-failure-no-writes-postcopy-failure-still-unpublished", mode);
        const auto failedBytes = queue.StagingBytes();
        Expect((selected.quarantined ? failedBytes != 0 : true) && failedBytes <= GpuUploadQueue::kStagingCapBytes &&
                   (selected.point != GpuUploadFailurePoint::MappedPitch || failedBytes == 0),
               "invalid-pitch-drops-storage-signalling-failure-keeps-charge", mode);
        Expect(SUCCEEDED(queue.Enqueue(target.get(), 0, 1, 1, 2, 2, {0, 2}, second)) &&
                   Until(scheduler, [&]() noexcept { return second.deliveries == 1; }),
               "subsequent-request-gets-a-valid-fresh-loan", mode);
        if (second.deliveries != 1)
            continue;
        Expect(!queue.PublishFilled(first.rows.lease, GpuUploadFill::Ready) &&
                   (!selected.quarantined || second.rows.lease.slot != 0),
               "stale-loan-and-quarantined-slot-never-reused", mode);
        Expect(FillOnWorker(queue, second, source) &&
                   Until(scheduler, [&]() noexcept { return second.completions == 1; }) && second.result == S_OK &&
                   second.ticket == JobTicket{0, 2},
               "failure-is-local-next-upload-completes-once", mode);
        Expect(TargetMatches(*device.get(), *context.get(), *target.get(), true) && first.completions == 1 &&
                   second.completions == 1 && queue.FailureHitsForTest() == 1,
               "literal-next-pixels-and-unmodified-callback-counts", mode);
        queue.RetireDevice();
        Expect(queue.StagingBytes() == (selected.quarantined ? failedBytes : 0),
               "retirement-keeps-only-quarantined-charge", mode);
        queue.StopNotifications();
        queue.CloseAfterWorkers();
        Expect(queue.StagingBytes() == 0 && !queue.HasPending() && !queue.HasReady() &&
                   !queue.PublishFilled(second.rows.lease, GpuUploadFill::Ready),
               "joined-close-drops-charge-and-stale-authority", mode);
    }
    return gFailures;
}

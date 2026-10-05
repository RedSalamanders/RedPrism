#include "GpuUploadTransportTests.h"

#include "Document.h"
#include "GpuUploadWork.h"
#include "OwnerWorkMultiplexer.h"
#include "RasterUploadWork.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
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
    std::printf("%s gpu-upload-transport.%u.%s\n", condition ? "PASS" : "FAIL", mode, label);
    gFailures += condition ? 0 : 1;
}
bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {8ULL << 30, 8ULL << 30};
    return true;
}
struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};
struct Receiver final : GpuUploadReceiver
{
    GpuUploadRows rows;
    unsigned int deliveries = 0;
    unsigned int completions = 0;
    HRESULT result = E_PENDING;
    JobTicket ticket;
    void WritableRowsReady(JobTicket selected, GpuUploadRows writable) noexcept override
    {
        ++deliveries;
        rows = writable;
        ticket = selected;
    }
    void UploadFinished(JobTicket selected, HRESULT error) noexcept override
    {
        ++completions;
        ticket = selected;
        result = error;
    }
};

// Event-driven transport harness. These tickets label receiver ownership, not scheduler admission evidence.
template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate predicate) noexcept
{
    const auto start = GetTickCount64();
    while (!predicate())
    {
        (void)DrainCompletions(scheduler);
        if (predicate())
            return true;
        if (scheduler.HasUiWork())
            continue;
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= 10000)
            return false;
        const auto waited =
            WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(10000 - elapsed));
        if (waited != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

bool LiteralTarget(Device& device, ID3D11Texture2D* destination, bool uploaded, bool gray) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    destination->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> read;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, read.put())))
        return false;
    device.context->CopyResource(read.get(), destination);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    // Synchronous verification is confined to this numeric GPU test, after the queue's completion event.
    if (FAILED(device.context->Map(read.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto unmap = wil::scope_exit([&]() noexcept { device.context->Unmap(read.get(), 0); });
    if (mapped.pData == nullptr)
        return false;
    constexpr std::array<std::uint8_t, 16> bgra{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::array<std::uint8_t, 4> grayPixels{7, 17, 29, 41};
    const auto channels = gray ? 1U : 4U;
    for (unsigned int y = 0; y < 4; ++y)
        for (unsigned int x = 0; x < 4; ++x)
            for (unsigned int c = 0; c < channels; ++c)
            {
                const auto actual =
                    static_cast<const std::uint8_t*>(mapped.pData)[y * mapped.RowPitch + x * channels + c];
                const bool inside = uploaded && x >= 1 && x < 3 && y >= 1 && y < 3;
                const auto index = inside ? (y - 1) * 2 + x - 1 : 0;
                const auto expected = inside ? gray ? grayPixels[index] : bgra[index * 4 + c] : std::uint8_t{53};
                if (actual != expected)
                    return false;
            }
    return true;
}

void TestTransport(unsigned int mode)
{
    const bool gray = mode == 1;
    const bool forcedEvent = mode == 2;
    const bool cancelled = mode == 3;
    const bool failed = mode == 4;
    const bool retired = mode == 5;
    const unsigned int busy = mode == 6 ? 1U : mode == 7 ? 2U : 0U;
    Device device, replacement;
    Expect(SUCCEEDED(device.Create()), "WARP-device", mode);
    if (!device.device)
        return;
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    GpuUploadQueue queue, foreign;
    Receiver receiver;
    if (scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK || !scheduler.RegisterOwnerWork(routes) ||
        FAILED(queue.Initialize(scheduler, &routes)) ||
        FAILED(queue.Bind(device.device.get(), device.context.get(), forcedEvent)))
    {
        Expect(false, "initialize", mode);
        return;
    }
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            foreign.StopNotifications();
            queue.StopNotifications();
            scheduler.Close();
            foreign.CloseAfterWorkers();
            queue.CloseAfterWorkers();
        });
    Expect(SUCCEEDED(foreign.Initialize(scheduler, &routes)), "foreign-queue-initialized", mode);
    if (forcedEvent)
        Expect(queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent, "forced-real-event-route",
               mode);
    std::array<std::uint8_t, 64> blank{};
    blank.fill(53);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 4;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Format = gray ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    const D3D11_SUBRESOURCE_DATA initial{blank.data(), gray ? 4U : 16U, 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    if (FAILED(device.device->CreateTexture2D(&desc, &initial, target.put())))
    {
        Expect(false, "destination", mode);
        return;
    }
    const JobTicket ticket{17, 31};
    queue.SetMapBusyForTest(busy);
    Expect(SUCCEEDED(queue.Enqueue(target.get(), 0, 1, 1, 2, 2, ticket, receiver)), "admitted-cropped-chunk", mode);
    Expect(Until(scheduler, [&]() noexcept { return receiver.deliveries != 0 || receiver.completions != 0; }),
           "event-driven-map-or-local-refusal", mode);
    if (busy >= 2)
    {
        Expect(receiver.deliveries == 0 && receiver.completions == 1 &&
                   receiver.result == DXGI_ERROR_WAS_STILL_DRAWING && queue.MapAttemptsForTest() == 2 &&
                   LiteralTarget(device, target.get(), false, gray),
               "one-event-retry-persistent-busy-no-copy", mode);
        return;
    }
    if (receiver.deliveries == 0)
    {
        Expect(false, "mapped-rows", mode);
        return;
    }
    const auto rows = receiver.rows;
    Expect(rows.data && rows.width == 2 && rows.height == 2 && rows.rowPitch >= (gray ? 2U : 8U) &&
               rows.capacity >= rows.rowPitch * 2 && queue.LentCount() == 1 && queue.InFlightCount() == 1 &&
               !queue.HasReady() && queue.HasPending() && queue.StagingBytes() <= GpuUploadQueue::kStagingCapBytes,
           "lent-rows-count-pitch-and-no-ready-spin", mode);
    Expect(!foreign.PublishFilled(rows.lease, GpuUploadFill::Ready) &&
               !queue.PublishFilled({rows.lease.slot, rows.lease.generation + 1, rows.lease.queueIdentity},
                                    GpuUploadFill::Ready),
           "foreign-and-wrong-generation-refuse", mode);
    if (cancelled)
        queue.Cancel(ticket);
    if (retired)
    {
        const auto bytes = queue.StagingBytes();
        const auto generation = queue.DeviceGeneration();
        Expect(SUCCEEDED(replacement.Create()) &&
                   SUCCEEDED(queue.Bind(replacement.device.get(), replacement.context.get())) &&
                   queue.DeviceGeneration() == generation + 1 && queue.StagingBytes() == bytes &&
                   queue.LentCount() == 1,
               "retired-lent-context-and-pitch-held-through-rebind", mode);
    }
    constexpr std::array<std::uint8_t, 16> bgra{7, 11, 13, 255, 17, 19, 23, 255, 29, 31, 37, 255, 41, 43, 47, 255};
    constexpr std::array<std::uint8_t, 4> grayPixels{7, 17, 29, 41};
    const auto source = Raster::Make(2, 2, gray ? RasterFormat::Gray8 : RasterFormat::Bgra8888,
                                     gray ? grayPixels.data() : bgra.data(), gray ? grayPixels.size() : bgra.size());
    bool workerCopied = false, acknowledged = false;
    const auto ownerThread = GetCurrentThreadId();
    {
        std::jthread worker(
            [&]() noexcept
            {
                workerCopied = GetCurrentThreadId() != ownerThread &&
                               CopyRasterRowsForUpload(source, 0, 0, 2, 2, rows.data, rows.capacity, rows.rowPitch) ==
                                   RasterUploadWorkStatus::Ready;
                acknowledged = queue.PublishFilled(rows.lease, failed ? GpuUploadFill::Failed : GpuUploadFill::Ready);
            });
    }
    Expect(workerCopied && acknowledged && !queue.PublishFilled(rows.lease, GpuUploadFill::Ready),
           "worker-only-copy-single-acknowledgement", mode);
    Expect(Until(scheduler, [&]() noexcept { return receiver.completions != 0; }), "copy-completes-on-real-event",
           mode);
    const bool copied = !cancelled && !failed && !retired;
    Expect(receiver.deliveries == 1 && receiver.completions == 1 && receiver.ticket == ticket &&
               receiver.result == (failed   ? E_FAIL
                                   : copied ? S_OK
                                            : E_ABORT),
           "exact-original-ticket-and-one-final-callback", mode);
    Expect(LiteralTarget(device, target.get(), copied, gray), "literal-crop-padding-or-suppressed-copy", mode);
    Expect(!queue.HasPending() && !queue.HasReady() && queue.LentCount() == 0 && queue.InFlightCount() == 0 &&
               !queue.PublishFilled(rows.lease, GpuUploadFill::Ready),
           "all-loans-retired-stale-ack-refuses", mode);
    if (busy == 1)
        Expect(queue.MapAttemptsForTest() == 2, "one-busy-map-one-event-one-real-map", mode);
    if (retired)
        Expect(queue.StagingBytes() == 0, "retired-staging-released-after-worker-ack", mode);
}

void TestCapacityAndReuse()
{
    constexpr unsigned int mode = 8;
    Device device;
    if (FAILED(device.Create()))
    {
        Expect(false, "WARP-device", mode);
        return;
    }
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    std::array<Receiver, 8> receivers;
    Receiver reused;
    GpuUploadQueue queue;
    if (scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK || !scheduler.RegisterOwnerWork(routes) ||
        FAILED(queue.Initialize(scheduler, &routes)) || FAILED(queue.Bind(device.device.get(), device.context.get())))
    {
        Expect(false, "initialize", mode);
        return;
    }
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            queue.StopNotifications();
            scheduler.Close();
            queue.CloseAfterWorkers();
        });
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 4;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, target.put())))
    {
        Expect(false, "destination", mode);
        return;
    }
    bool admitted = true;
    for (std::size_t i = 0; i < receivers.size(); ++i)
        admitted = SUCCEEDED(queue.Enqueue(target.get(), 0, 0, 0, 2, 2, {i, 19}, receivers[i])) && admitted;
    Expect(admitted, "eight-records-admitted", mode);
    queue.Pump();
    Expect(queue.LentCount() == 1 && queue.InFlightCount() == 1 && receivers[0].deliveries == 1,
           "one-map-in-one-owner-pump", mode);
    queue.Pump();
    Expect(queue.LentCount() == 2 && queue.InFlightCount() == 2 && receivers[1].deliveries == 1 && !queue.HasReady() &&
               queue.HasPending() && receivers[2].deliveries == 0 && queue.StagingBytes() != 0 &&
               queue.StagingBytes() <= 16ULL * 1024 * 1024,
           "two-loans-block-pending-without-readiness-spin", mode);
    const auto oldLease = receivers[0].rows.lease;
    Expect(queue.PublishFilled(oldLease, GpuUploadFill::Cancelled), "first-loan-acknowledged", mode);
    queue.Pump();
    Expect(receivers[0].completions == 1 && receivers[0].result == E_ABORT && queue.InFlightCount() == 1 &&
               receivers[2].deliveries == 0,
           "acknowledgement-is-its-own-bounded-pump", mode);
    queue.Pump();
    Expect(receivers[2].deliveries == 1 && queue.InFlightCount() == 2, "released-capacity-admits-next-chunk", mode);
    for (std::size_t i = 0; i < receivers.size(); ++i)
    {
        queue.Cancel({i, 19});
        if (receivers[i].deliveries != 0 && receivers[i].completions == 0)
            Expect(queue.PublishFilled(receivers[i].rows.lease, GpuUploadFill::Cancelled), "held-loan-cancel-ack",
                   mode);
    }
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "all-cancelled-requests-settle-without-present",
           mode);
    bool exact = true;
    for (std::size_t i = 0; i < receivers.size(); ++i)
        exact = exact && receivers[i].completions == 1 && receivers[i].result == E_ABORT &&
                receivers[i].ticket == JobTicket{i, 19} && receivers[i].deliveries == (i < 3 ? 1U : 0U);
    Expect(exact && queue.LentCount() == 0 && queue.InFlightCount() == 0 && !queue.HasPending(),
           "all-eight-original-receivers-complete-exactly-once", mode);
    const auto bytes = queue.StagingBytes();
    Expect(SUCCEEDED(queue.Enqueue(target.get(), 0, 0, 0, 2, 2, {3, 51}, reused)), "new-generation-admitted", mode);
    queue.Pump();
    Expect(reused.deliveries == 1 && reused.rows.lease.slot == oldLease.slot &&
               reused.rows.lease.generation > oldLease.generation && queue.StagingBytes() == bytes &&
               !queue.PublishFilled(oldLease, GpuUploadFill::Ready),
           "reusable-class-keeps-new-lease-safe-from-old-ack", mode);
    Expect(queue.PublishFilled(reused.rows.lease, GpuUploadFill::Cancelled), "new-loan-acknowledged", mode);
    queue.Pump();
    Expect(reused.completions == 1 && reused.result == E_ABORT && !queue.HasPending(), "reuse-finishes-once", mode);
}

struct ShutdownState final : GpuUploadReceiver
{
    JobScheduler& scheduler;
    GpuUploadQueue& queue;
    GpuUploadRows rows;
    wil::unique_handle mapped{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> entered{false};
    bool offOwner = false, stopObserved = false, acknowledgement = false;
    unsigned int completed = 0;
    unsigned int deliveries = 0;
    DWORD ownerThread = GetCurrentThreadId();
    ShutdownState(JobScheduler& jobs, GpuUploadQueue& uploads) noexcept : scheduler(jobs), queue(uploads) {}
    void WritableRowsReady(JobTicket, GpuUploadRows writable) noexcept override
    {
        rows = writable;
        ++deliveries;
        (void)SetEvent(mapped.get());
    }
    void UploadFinished(JobTicket, HRESULT) noexcept override
    {
        ++completed;
    }
};

class ShutdownFillJob final : public Job
{
  public:
    explicit ShutdownFillJob(ShutdownState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        if (WaitForSingleObject(_state.mapped.get(), 10000) != WAIT_OBJECT_0)
            return {JobStatus::Failed, E_UNEXPECTED};
        _state.offOwner = GetCurrentThreadId() != _state.ownerThread;
        _state.entered.store(true, std::memory_order_release);
        _state.scheduler.NotifyOwnerWork();
        _state.stopObserved = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) == WAIT_OBJECT_0;
        if (_state.stopObserved)
        {
            // Still write-only and unpublished after stop: shutdown cannot unmap until this worker returns.
            _state.rows.data[0] = 59;
            _state.acknowledgement = _state.queue.PublishFilled(_state.rows.lease, GpuUploadFill::Cancelled);
        }
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        ++_state.completed;
    }

  private:
    ShutdownState& _state;
};

void TestShutdownWithHeldWorker()
{
    constexpr unsigned int mode = 9;
    Device device;
    if (FAILED(device.Create()))
    {
        Expect(false, "WARP-device", mode);
        return;
    }
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    GpuUploadQueue queue;
    ShutdownState state{scheduler, queue};
    if (!state.mapped || scheduler.Initialize(1, {}, {StableMemory, nullptr}) != S_OK ||
        !scheduler.RegisterOwnerWork(routes) || FAILED(queue.Initialize(scheduler, &routes)) ||
        FAILED(queue.Bind(device.device.get(), device.context.get())))
    {
        Expect(false, "initialize", mode);
        return;
    }
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            queue.StopNotifications();
            scheduler.Close();
            queue.CloseAfterWorkers();
        });
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 1;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, target.put())))
    {
        Expect(false, "destination", mode);
        return;
    }
    std::unique_ptr<Job> job = std::make_unique<ShutdownFillJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Commit, 1, 4096}, ticket) == S_OK, "actual-fill-worker-admitted", mode);
    Expect(SUCCEEDED(queue.Enqueue(target.get(), 0, 0, 0, 1, 1, ticket, state)), "actual-ticket-enqueues-rows", mode);
    Expect(Until(scheduler, [&]() noexcept { return state.entered.load(std::memory_order_acquire); }),
           "actual-worker-held-after-mapping", mode);
    Expect(state.deliveries == 1 && state.offOwner && queue.LentCount() == 1 && queue.StagingBytes() != 0 &&
               !queue.HasReady() && scheduler.ReservedMemoryBytes() == 4096,
           "mapped-context-and-reservation-held-before-stop", mode);
    const auto lease = state.rows.lease;
    queue.RetireDevice();
    queue.StopNotifications();
    scheduler.Close(); // Signals shutdown and joins the actual filling callback before owner Unmap.
    Expect(state.stopObserved && state.acknowledgement && state.completed == 0 && queue.LentCount() == 1,
           "shutdown-joins-filling-worker-before-unmap-and-drops-callbacks", mode);
    queue.CloseAfterWorkers();
    Expect(queue.LentCount() == 0 && queue.StagingBytes() == 0 && !queue.HasPending() && !routes.HasPending() &&
               !routes.HasReady() && scheduler.ReservedMemoryBytes() == 0 &&
               !queue.PublishFilled(lease, GpuUploadFill::Ready),
           "joined-close-detaches-and-invalidates-retired-loan", mode);
    queue.CloseAfterWorkers();
    scheduler.Close();
    Expect(state.completed == 0, "repeated-close-has-no-late-callback", mode);
}
} // namespace

int RunGpuUploadTransportTests()
{
    gFailures = 0;
    for (unsigned int mode = 0; mode < 8; ++mode)
        TestTransport(mode);
    TestCapacityAndReuse();
    TestShutdownWithHeldWorker();
    return gFailures;
}

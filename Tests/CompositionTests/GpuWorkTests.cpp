#include "GpuWorkTests.h"

#include "GpuWork.h"
#include "PixelBuffer.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
constexpr UINT kShutdownCopy = WM_APP + 0x4a3;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-work.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
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

wil::com_ptr_nothrow<ID3D11Texture2D> Texture(Device& device, unsigned int width, unsigned int height)
{
    // Independent coordinate formula, including alpha. Copied rectangles are compared with these literal rules,
    // not with a second call through the queue.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (unsigned int y = 0; y < height; ++y)
        for (unsigned int x = 0; x < width; ++x)
        {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i] = static_cast<std::uint8_t>((x + y * 3) % 127);
            pixels[i + 1] = static_cast<std::uint8_t>((x * 2 + y) % 127);
            pixels[i + 2] = static_cast<std::uint8_t>((x + y * 7) % 127);
            pixels[i + 3] = 160;
        }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    Expect(SUCCEEDED(device.device->CreateTexture2D(&desc, &data, texture.put())), "source-created");
    return texture;
}

struct CopyState final
{
    unsigned int callbacks = 0;
    unsigned int deliveries = 0;
    unsigned int runs = 0;
    JobStatus status = JobStatus::Failed;
    bool workerCopied = false;
    bool correctRows = false;
    bool releaseAccepted = false;
    bool resumeAccepted = false;
    bool cancelBeforeResume = false;
    Device* replacement = nullptr;
    HRESULT bindResult = E_PENDING;
    HRESULT error = E_PENDING;
    std::uint64_t retainedAfterBind = 0;
    std::uint64_t stagingAtDelivery = 0;
    bool shutdownCopy = false;
    bool copyReturnedAfterShutdown = false;
};

class CopyJob final : public Job, public GpuReadbackReceiver
{
  public:
    CopyJob(JobScheduler& scheduler, GpuWorkQueue& queue, ID3D11Texture2D* source, D3D11_BOX box,
            CopyState& state) noexcept
        : _scheduler(scheduler), _queue(queue), _source(source), _box(box), _state(state),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        ++_state.runs;
        if (!_submitted)
        {
            _pixels = PixelBuffer::TryAllocate(static_cast<std::size_t>(_box.right - _box.left) *
                                               (_box.bottom - _box.top) * 4);
            return _pixels ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
        }
        if (FAILED(_error))
            return {JobStatus::Failed, _error};
        _state.workerCopied = GetCurrentThreadId() != _ownerThread;
        _state.correctRows = true;
        for (unsigned int y = 0; y < _rows.height; ++y)
        {
            std::memcpy(_pixels.Data() + static_cast<std::size_t>(y) * _rows.width * 4,
                        _rows.data + static_cast<std::size_t>(y) * _rows.rowPitch,
                        static_cast<std::size_t>(_rows.width) * 4);
            for (unsigned int x = 0; x < _rows.width; ++x)
            {
                const auto i = (static_cast<std::size_t>(y) * _rows.width + x) * 4;
                const unsigned int sx = x + _box.left, sy = y + _box.top;
                _state.correctRows =
                    _state.correctRows && _pixels[i] == static_cast<std::uint8_t>((sx + sy * 3) % 127) &&
                    _pixels[i + 1] == static_cast<std::uint8_t>((sx * 2 + sy) % 127) &&
                    _pixels[i + 2] == static_cast<std::uint8_t>((sx + sy * 7) % 127) && _pixels[i + 3] == 160;
            }
        }
        if (_state.shutdownCopy)
        {
            if (!PostThreadMessageW(_ownerThread, kShutdownCopy, 0, 0) ||
                WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
            {
                std::printf("FAIL gpu-work.shutdown-copy-event-gate\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
            // Close has signalled cancellation, but has not yet joined this worker or unmapped its rows.
            _state.copyReturnedAfterShutdown = _rows.data[3] == 160;
        }
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_submitted)
            return false;
        _submitted = true;
        _error = _queue.EnqueueReadback(_source.get(), _box, ticket, *this);
        if (FAILED(_error))
            _state.resumeAccepted = _scheduler.Resume(ticket);
        return true;
    }
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        ++_state.deliveries;
        _rows = rows;
        _error = result;
        _state.stagingAtDelivery = _queue.StagingBytes();
        if (_state.replacement)
        {
            _state.bindResult = _queue.Bind(_state.replacement->device.get(), _state.replacement->context.get());
            _state.retainedAfterBind = _queue.StagingBytes();
        }
        if (_state.cancelBeforeResume)
            (void)_scheduler.Cancel(ticket);
        _state.resumeAccepted = _scheduler.Resume(ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.error = result.error;
        if (_rows.data)
        {
            if (_rows.deviceGeneration != _queue.DeviceGeneration() && result.status == JobStatus::Finished)
                result = {JobStatus::Cancelled};
            _state.releaseAccepted = _queue.ReleaseMapped(_rows.lease);
            Expect(!_queue.ReleaseMapped(_rows.lease), "duplicate-lease-release-refused");
        }
        _state.status = result.status;
    }

  private:
    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    wil::com_ptr_nothrow<ID3D11Texture2D> _source;
    D3D11_BOX _box;
    CopyState& _state;
    DWORD _ownerThread;
    PixelBuffer _pixels;
    GpuReadbackRows _rows;
    HRESULT _error = S_OK;
    bool _submitted = false;
};

void TestCopy(bool cancel, bool rebind, bool singlePixel)
{
    Device device, replacement;
    Expect(SUCCEEDED(device.Create()), "copy.device");
    if (!device.device)
        return;
    if (rebind)
        Expect(SUCCEEDED(replacement.Create()), "copy.replacement-device");
    JobScheduler scheduler;
    GpuWorkQueue queue;
    Expect(scheduler.Initialize(1) == S_OK && SUCCEEDED(queue.Initialize(scheduler)), "copy.initialize");
    Expect(SUCCEEDED(queue.Bind(device.device.get(), device.context.get())), "copy.bind");
    Expect(!scheduler.HasUiWork() && scheduler.IsIdle(), "copy.idle-registration-no-work");
    auto texture = Texture(device, 11, 7);
    const D3D11_BOX box = singlePixel ? D3D11_BOX{5, 3, 0, 6, 4, 1} : D3D11_BOX{2, 1, 0, 9, 6, 1};
    CopyState state;
    state.cancelBeforeResume = cancel;
    state.replacement = rebind ? &replacement : nullptr;
    std::unique_ptr<Job> job = std::make_unique<CopyJob>(scheduler, queue, texture.get(), box, state);
    JobTicket ticket;
    const auto bytes =
        PixelBuffer::RequiredBytes(static_cast<std::size_t>(box.right - box.left) * (box.bottom - box.top) * 4);
    Expect(scheduler.Submit(job, {JobClass::Commit, 1, bytes}, ticket) == S_OK, "copy.submit");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "copy.settle-without-window-or-present");
    Expect(state.deliveries == 1 && state.callbacks == 1 && state.resumeAccepted && state.releaseAccepted,
           "copy.one-delivery-final-apply-and-ack");
    if (cancel)
        Expect(state.runs == 1 && !state.workerCopied && state.status == JobStatus::Cancelled,
               "copy.cancel-after-map-skips-copy-and-unmaps");
    else
        Expect(state.runs == 2 && state.workerCopied && state.correctRows,
               "copy.worker-row-pitch-and-cropped-coordinates");
    if (rebind)
    {
        Expect(SUCCEEDED(state.bindResult) && state.retainedAfterBind == state.stagingAtDelivery &&
                   state.retainedAfterBind != 0 && queue.StagingBytes() == 0,
               "copy.retired-mapping-retained-until-worker-ack");
        Expect(state.status == JobStatus::Cancelled, "copy.retired-generation-not-published");
    }
    else if (!cancel)
        Expect(state.status == JobStatus::Finished, "copy.finished");
    if (singlePixel)
        Expect(state.stagingAtDelivery == 4, "copy.one-pixel-uses-four-texel-bytes");
    Expect(queue.LentCount() == 0 && queue.InFlightCount() == 0 && !queue.HasPending() &&
               scheduler.ReservedMemoryBytes() == 0,
           "copy.no-leaked-slot-or-reservation");
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
}

constexpr UINT kReleaseLeases = WM_APP + 0x4a2;
class CapacityReceiver final : public GpuReadbackReceiver
{
  public:
    GpuWorkQueue& queue;
    JobScheduler& scheduler;
    std::array<GpuReadbackLease, 4> leases{};
    unsigned int deliveries = 0;
    bool saturationWaited = false;
    bool fullPixelsCorrect = true;
    CapacityReceiver(GpuWorkQueue& work, JobScheduler& jobs) noexcept : queue(work), scheduler(jobs) {}
    void ReadbackReady(JobTicket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        Expect(result == S_OK && rows.data && queue.InFlightCount() <= GpuWorkQueue::kMaxInFlight,
               "cap.successful-bounded-completion");
        if (FAILED(result))
            return;
        fullPixelsCorrect = fullPixelsCorrect && rows.data[0] == 0 && rows.data[3] == 160;
        if (deliveries < leases.size())
            leases[deliveries] = rows.lease;
        else
            Expect(queue.ReleaseMapped(rows.lease), "cap.later-tile-ack");
        if (++deliveries == leases.size())
        {
            saturationWaited =
                queue.StagingBytes() == GpuWorkQueue::kStagingCapBytes && !queue.HasReady() && !scheduler.IsIdle();
            Expect(PostThreadMessageW(GetCurrentThreadId(), kReleaseLeases, 0, 0) != FALSE,
                   "cap.post-owned-ack-message");
        }
    }
    void Release() noexcept
    {
        for (auto lease : leases)
            Expect(queue.ReleaseMapped(lease), "cap.release-held-mapping");
    }
};

void DispatchLeaseMessage(void* context, void* nativeMessage) noexcept
{
    const auto& message = *static_cast<MSG*>(nativeMessage);
    if (message.message == kReleaseLeases)
        static_cast<CapacityReceiver*>(context)->Release();
    else
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void TestCapacity()
{
    Device device;
    Expect(SUCCEEDED(device.Create()), "cap.device");
    if (!device.device)
        return;
    JobScheduler scheduler;
    GpuWorkQueue queue;
    Expect(scheduler.Initialize(1) == S_OK && SUCCEEDED(queue.Initialize(scheduler)) &&
               SUCCEEDED(queue.Bind(device.device.get(), device.context.get())),
           "cap.initialize");
    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    auto texture = Texture(device, 2048, 2048);
    CapacityReceiver receiver{queue, scheduler};
    const D3D11_BOX box{0, 0, 0, 2048, 2048, 1};
    for (std::size_t i = 0; i < GpuWorkQueue::kCapacity; ++i)
        Expect(queue.EnqueueReadback(texture.get(), box, {i, 1}, receiver) == S_OK, "cap.admit-eight");
    Expect(queue.EnqueueReadback(texture.get(), box, {0, 2}, receiver) == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
           "cap.ninth-refused-without-callback");
    Expect(SettleJobs(scheduler, 10000, DispatchLeaseMessage, &receiver) == SettleResult::Settled,
           "cap.waits-for-owned-ack-and-completes");
    Expect(receiver.deliveries == GpuWorkQueue::kCapacity && receiver.saturationWaited && receiver.fullPixelsCorrect,
           "cap.staging-full-waits-without-poll-or-partial-result");
    Expect(queue.StagingBytes() <= GpuWorkQueue::kStagingCapBytes && queue.LentCount() == 0,
           "cap.budget-held-after-reuse");
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
}

struct ShutdownOwner final
{
    JobScheduler& scheduler;
    GpuWorkQueue& queue;
    unsigned int messages = 0;
    bool mappingLentAtShutdown = false;
};

void DispatchShutdown(void* context, void* nativeMessage) noexcept
{
    auto& owner = *static_cast<ShutdownOwner*>(context);
    const auto& message = *static_cast<MSG*>(nativeMessage);
    if (message.message != kShutdownCopy)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        return;
    }
    ++owner.messages;
    owner.mappingLentAtShutdown = owner.queue.LentCount() == 1;
    owner.queue.StopNotifications();
    owner.scheduler.Close();
    owner.queue.CloseAfterWorkers();
}

void TestShutdownWhileCopying()
{
    Device device;
    Expect(SUCCEEDED(device.Create()), "shutdown.device");
    if (!device.device)
        return;
    JobScheduler scheduler;
    GpuWorkQueue queue;
    Expect(scheduler.Initialize(1) == S_OK && SUCCEEDED(queue.Initialize(scheduler)) &&
               SUCCEEDED(queue.Bind(device.device.get(), device.context.get())),
           "shutdown.initialize");
    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    auto texture = Texture(device, 2, 2);
    CopyState state;
    state.shutdownCopy = true;
    std::unique_ptr<Job> job =
        std::make_unique<CopyJob>(scheduler, queue, texture.get(), D3D11_BOX{0, 0, 0, 2, 2, 1}, state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Commit, 1, PixelBuffer::RequiredBytes(16)}, ticket) == S_OK,
           "shutdown.submit");
    ShutdownOwner owner{scheduler, queue};
    Expect(SettleJobs(scheduler, 10000, DispatchShutdown, &owner) == SettleResult::Settled, "shutdown.settle");
    Expect(owner.messages == 1 && owner.mappingLentAtShutdown && state.copyReturnedAfterShutdown && state.correctRows &&
               state.callbacks == 0,
           "shutdown.joins-copy-before-unmap-and-drops-callback");
    Expect(queue.StagingBytes() == 0 && scheduler.ReservedMemoryBytes() == 0, "shutdown.releases-after-join");
}
} // namespace

int RunGpuWorkTests()
{
    gFailures = 0;
    TestCopy(false, false, false);
    TestCopy(false, false, true);
    TestCopy(true, false, false);
    TestCopy(false, true, false);
    TestCapacity();
    TestShutdownWhileCopying();
    return gFailures;
}

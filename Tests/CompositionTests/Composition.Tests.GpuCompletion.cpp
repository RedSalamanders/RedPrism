#include "Composition.Tests.GpuCompletion.h"

#include "GpuWork.h"
#include "WorkerPool.h"

#include <array>
#include <cstdint>
#include <cstdio>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-completion.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct WarpDevice final
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

class Rig final
{
  public:
    ~Rig()
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
    }

    bool Initialize(bool forceEvent = true, bool bindDevice = true) noexcept
    {
        HRESULT result = device.Create();
        Expect(SUCCEEDED(result), "setup.warp-device");
        if (FAILED(result))
            return false;

        result = static_cast<HRESULT>(scheduler.Initialize(1, {}, {StableMemory, nullptr}));
        Expect(SUCCEEDED(result), "setup.single-worker-scheduler");
        if (FAILED(result))
            return false;

        result = queue.Initialize(scheduler);
        Expect(SUCCEEDED(result), "setup.queue");
        if (FAILED(result))
            return false;

        if (bindDevice)
        {
            result = queue.Bind(device.device.get(), device.context.get(), forceEvent);
            Expect(SUCCEEDED(result), "setup.bind-warp-device");
            if (FAILED(result))
                return false;
        }
        if (bindDevice && forceEvent)
            Expect(queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent,
                   "setup.forced-enqueue-set-event-route");
        return true;
    }

    WarpDevice device;
    JobScheduler scheduler;
    GpuWorkQueue queue;
};

struct Delivery final
{
    JobTicket ticket{};
    std::uint64_t deviceGeneration = 0;
    HRESULT result = E_PENDING;
};

class Receiver final : public GpuCompletionReceiver
{
  public:
    void GpuCompletionReady(JobTicket ticket, std::uint64_t deviceGeneration, HRESULT result) noexcept override
    {
        if (count < deliveries.size())
            deliveries[count] = {ticket, deviceGeneration, result};
        ++count;
    }

    std::array<Delivery, GpuWorkQueue::kCapacity + 1> deliveries{};
    std::size_t count = 0;
};

void PumpUntil(Rig& rig, Receiver& receiver, std::size_t target) noexcept
{
    constexpr std::uint64_t timeoutMs = 10000;
    const std::uint64_t start = GetTickCount64();
    while (receiver.count < target)
    {
        (void)DrainCompletions(rig.scheduler);
        rig.queue.Pump();
        if (receiver.count >= target)
            return;
        const std::uint64_t elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs)
        {
            Expect(false, "wait.completion-event-timeout");
            return;
        }
        const HANDLE wake = static_cast<HANDLE>(rig.scheduler.WakeHandle());
        if (!wake || WaitForSingleObject(wake, static_cast<DWORD>(timeoutMs - elapsed)) != WAIT_OBJECT_0)
        {
            Expect(false, "wait.scheduler-event-wake");
            return;
        }
    }
}

wil::com_ptr_nothrow<ID3D11Buffer> MakeUavBuffer(WarpDevice& device, std::size_t count) noexcept
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(count * sizeof(std::uint32_t));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(std::uint32_t);
    wil::com_ptr_nothrow<ID3D11Buffer> buffer;
    if (FAILED(device.device->CreateBuffer(&desc, nullptr, buffer.put())))
        return {};
    return buffer;
}

wil::com_ptr_nothrow<ID3D11UnorderedAccessView> MakeBufferUav(WarpDevice& device, ID3D11Buffer* buffer,
                                                              UINT elements) noexcept
{
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.NumElements = elements;
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> view;
    if (FAILED(device.device->CreateUnorderedAccessView(buffer, &desc, view.put())))
        return {};
    return view;
}

wil::com_ptr_nothrow<ID3D11Buffer> QueueBufferProbe(WarpDevice& device, ID3D11Buffer* source) noexcept
{
    D3D11_BUFFER_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Buffer> staging;
    if (FAILED(device.device->CreateBuffer(&desc, nullptr, staging.put())))
        return {};
    device.context->CopyResource(staging.get(), source); // This copy precedes the barrier under test.
    return staging;
}

bool ReadBufferAfterAck(WarpDevice& device, ID3D11Buffer* staging,
                        const std::array<std::uint32_t, 4>& expected) noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device.context->Map(staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(result))
    {
        std::printf("INFO gpu-completion.buffer.nonblocking-probe-map=%08lX\n", static_cast<unsigned long>(result));
        return false; // A premature callback cannot be hidden by a blocking Map.
    }
    const auto* values = static_cast<const std::uint32_t*>(mapped.pData);
    bool matches = true;
    for (std::size_t i = 0; i < expected.size(); ++i)
        matches = matches && values[i] == expected[i];
    device.context->Unmap(staging, 0);
    return matches;
}

bool TestBufferCommandsCompleteBeforeAck() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize())
        return false;
    // A structured buffer clear repeats the first uint; it does not cycle the four clear channels.
    constexpr std::array<std::uint32_t, 4> expected{0x10203040u, 0x10203040u, 0x10203040u, 0x10203040u};
    auto buffer = MakeUavBuffer(rig.device, expected.size());
    auto uav = buffer ? MakeBufferUav(rig.device, buffer.get(), static_cast<UINT>(expected.size()))
                      : wil::com_ptr_nothrow<ID3D11UnorderedAccessView>{};
    Expect(bool(buffer) && bool(uav), "buffer.create-default-uav-resource");
    if (!buffer || !uav)
        return false;

    const UINT clear[4]{expected[0], expected[1], expected[2], expected[3]};
    rig.device.context->ClearUnorderedAccessViewUint(uav.get(), clear);
    auto probe = QueueBufferProbe(rig.device, buffer.get());
    Expect(bool(probe), "buffer.independent-copy-queued-before-barrier");
    if (!probe)
        return false;
    const GpuReadbackStatistics before = rig.queue.Statistics();
    const JobTicket ticket{3, 41};
    const std::uint64_t acceptedGeneration = rig.queue.DeviceGeneration();
    const HRESULT admitted = rig.queue.EnqueueCompletion(ticket, receiver);
    Expect(admitted == S_OK, "buffer.admitted-after-real-uav-clear");
    if (FAILED(admitted))
        return false;

    PumpUntil(rig, receiver, 1);
    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    const bool oneExactSuccess = receiver.count == 1 && receiver.deliveries[0].ticket == ticket &&
                                 receiver.deliveries[0].deviceGeneration == acceptedGeneration &&
                                 receiver.deliveries[0].result == S_OK;
    Expect(oneExactSuccess, "buffer.one-callback-exact-ticket-and-generation");
    Expect(receiver.count == 1 && ReadBufferAfterAck(rig.device, probe.get(), expected),
           "buffer.gpu-clear-visible-to-independent-read-only-after-ack");
    const GpuReadbackStatistics after = rig.queue.Statistics();
    Expect(after.copies == before.copies && after.maps == before.maps,
           "buffer.success-path-adds-no-queue-staging-copy-or-map");
    return true;
}

wil::com_ptr_nothrow<ID3D11Texture2D> MakeUavTexture(WarpDevice& device) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 2;
    desc.Height = 2;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, texture.put())))
        return {};
    return texture;
}

wil::com_ptr_nothrow<ID3D11Texture2D> QueueTextureProbe(WarpDevice& device, ID3D11Texture2D* source) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, staging.put())))
        return {};
    device.context->CopyResource(staging.get(), source);
    return staging;
}

bool ReadTextureAfterAck(WarpDevice& device, ID3D11Texture2D* staging,
                         const std::array<std::uint32_t, 4>& channels) noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device.context->Map(staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(result))
    {
        std::printf("INFO gpu-completion.texture.nonblocking-probe-map=%08lX\n", static_cast<unsigned long>(result));
        return false;
    }
    bool matches = true;
    for (unsigned int y = 0; y < 2; ++y)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData) +
                                                                 static_cast<std::size_t>(y) * mapped.RowPitch);
        for (unsigned int x = 0; x < 2; ++x)
            for (unsigned int channel = 0; channel < 4; ++channel)
                matches = matches && row[x * 4 + channel] == channels[channel];
    }
    device.context->Unmap(staging, 0);
    return matches;
}

bool TestTextureCommandsCompleteBeforeAck() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize())
        return false;
    auto texture = MakeUavTexture(rig.device);
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uav;
    if (texture)
        (void)rig.device.device->CreateUnorderedAccessView(texture.get(), nullptr, uav.put());
    Expect(bool(texture) && bool(uav), "texture.create-default-uav-resource");
    if (!texture || !uav)
        return false;

    constexpr std::array<std::uint32_t, 4> expected{0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u};
    const UINT clear[4]{expected[0], expected[1], expected[2], expected[3]};
    rig.device.context->ClearUnorderedAccessViewUint(uav.get(), clear);
    auto probe = QueueTextureProbe(rig.device, texture.get());
    Expect(bool(probe), "texture.independent-copy-queued-before-barrier");
    if (!probe)
        return false;
    const GpuReadbackStatistics before = rig.queue.Statistics();
    const JobTicket ticket{4, 42};
    const std::uint64_t acceptedGeneration = rig.queue.DeviceGeneration();
    const HRESULT admitted = rig.queue.EnqueueCompletion(ticket, receiver);
    Expect(admitted == S_OK, "texture.admitted-after-real-uav-clear");
    if (FAILED(admitted))
        return false;

    PumpUntil(rig, receiver, 1);
    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    Expect(receiver.count == 1 && receiver.deliveries[0].ticket == ticket &&
               receiver.deliveries[0].deviceGeneration == acceptedGeneration && receiver.deliveries[0].result == S_OK,
           "texture.one-callback-exact-ticket-and-generation");
    Expect(receiver.count == 1 && ReadTextureAfterAck(rig.device, probe.get(), expected),
           "texture-all-cleared-uint-channels-visible-after-ack");
    const GpuReadbackStatistics after = rig.queue.Statistics();
    Expect(after.copies == before.copies && after.maps == before.maps,
           "texture.success-path-adds-no-queue-staging-copy-or-map");
    return true;
}

bool TestQuotaRefusalHasNoCallback() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize())
        return false;
    const JobTicket ticket{5, 90};
    const HRESULT first = rig.queue.EnqueueCompletion(JobTicket{0, 1}, receiver);
    const HRESULT second = rig.queue.EnqueueCompletion(JobTicket{1, 2}, receiver);
    const HRESULT overflow = rig.queue.EnqueueCompletion(ticket, receiver);
    Expect(first == S_OK && second == S_OK && overflow == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) &&
               !rig.queue.HasCompletionSlot(),
           "quota.two-in-flight-admitted-third-refused-and-readiness-closes");
    Expect(receiver.count == 0, "quota.refused-request-has-no-callback");
    PumpUntil(rig, receiver, 2);
    Expect(receiver.count == 2 && rig.queue.HasCompletionSlot(),
           "quota.release-event-restores-slot-after-both-exactly-once-callbacks");
    return true;
}

bool TestRetirementPreservesRecordedGeneration() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize())
        return false;
    const JobTicket ticket{6, 91};
    const std::uint64_t acceptedGeneration = rig.queue.DeviceGeneration();
    Expect(rig.queue.EnqueueCompletion(ticket, receiver) == S_OK, "retire.record-old-generation-barrier-at-admission");
    rig.queue.RetireDevice();
    WarpDevice replacement;
    const HRESULT createResult = replacement.Create();
    Expect(SUCCEEDED(createResult), "retire.create-replacement-warp-device");
    if (FAILED(createResult))
        return false;
    const HRESULT bindResult = rig.queue.Bind(replacement.device.get(), replacement.context.get(), true);
    Expect(SUCCEEDED(bindResult), "retire.bind-new-device");
    if (FAILED(bindResult))
        return false;
    const std::uint64_t replacementGeneration = rig.queue.DeviceGeneration();
    Expect(replacementGeneration != acceptedGeneration && rig.queue.InFlightCount() == 1 && receiver.count == 0,
           "retire.new-device-bound-while-old-event-and-generation-remain-owned");
    PumpUntil(rig, receiver, 1);
    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    Expect(receiver.count == 1 && receiver.deliveries[0].ticket == ticket &&
               receiver.deliveries[0].deviceGeneration == acceptedGeneration &&
               receiver.deliveries[0].deviceGeneration != replacementGeneration &&
               receiver.deliveries[0].result == E_ABORT,
           "retire.old-event-acknowledges-once-as-abort-with-original-generation");
    return true;
}

bool TestPendingStateAndForcedFallback() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize(true))
        return false;
    const JobTicket ticket{7, 92};
    const HRESULT admitted = rig.queue.EnqueueCompletion(ticket, receiver);
    Expect(admitted == S_OK, "event.admit-on-forced-fallback");
    if (FAILED(admitted))
        return false;
    // HasReady may already be true if the real event won the race; that is valid. The in-flight slot
    // remains waiting and no callback runs until the owner pumps, regardless of notification timing.
    const bool waiting = rig.queue.HasPending() && rig.queue.InFlightCount() == 1 && receiver.count == 0;
    PumpUntil(rig, receiver, 1);
    Expect(receiver.count == 1 && receiver.deliveries[0].result == S_OK && receiver.deliveries[0].ticket == ticket,
           "event.actual-enqueue-set-event-wakes-one-success-delivery");
    Expect(waiting, "event-in-flight-ownership-retained-with-no-callback-before-pump");
    return true;
}

bool TestFailedAdmissionHasNoCallback() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize(true, false))
        return false;
    const HRESULT refused = rig.queue.EnqueueCompletion(JobTicket{2, 94}, receiver);
    Expect(FAILED(refused) && receiver.count == 0 && !rig.queue.HasPending(),
           "failure.unbound-recording-refusal-has-no-callback-or-queue-owner");
    return true;
}

bool TestShutdownDropsNotificationsAndWaits() noexcept
{
    Receiver receiver;
    Rig rig;
    if (!rig.Initialize())
        return false;
    Expect(rig.queue.EnqueueCompletion(JobTicket{1, 93}, receiver) == S_OK, "shutdown.admit-pending-barrier");
    rig.queue.StopNotifications();
    const std::size_t callbacksAtStop = receiver.count;
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::array<JobEventTicket, WaitPool::kCapacity> registrations{};
    std::size_t registered = 0;
    const auto noop = [](void*) noexcept {};
    if (event)
        for (; registered < registrations.size(); ++registered)
            if (FAILED(rig.scheduler.RegisterEventWait(event.get(), noop, nullptr, registrations[registered])))
                break;
    Expect(event && registered == registrations.size(), "shutdown.all-wait-pool-slots-reusable-after-stop");
    for (std::size_t i = 0; i < registered; ++i)
        (void)rig.scheduler.RemoveEventWait(registrations[i]);
    rig.scheduler.Close();
    rig.queue.CloseAfterWorkers();
    Expect(receiver.count == callbacksAtStop && receiver.count == 0, "shutdown.stop-and-close-do-not-deliver-callback");
    Expect(!rig.queue.HasPending() && !rig.queue.HasReady(), "shutdown.queue-has-no-pending-or-ready-work");
    return true;
}
} // namespace

int RunGpuCompletionBarrierTests()
{
    gFailures = 0;
    (void)TestBufferCommandsCompleteBeforeAck();
    (void)TestTextureCommandsCompleteBeforeAck();
    (void)TestQuotaRefusalHasNoCallback();
    (void)TestRetirementPreservesRecordedGeneration();
    (void)TestPendingStateAndForcedFallback();
    (void)TestFailedAdmissionHasNoCallback();
    (void)TestShutdownDropsNotificationsAndWaits();
    return gFailures;
}

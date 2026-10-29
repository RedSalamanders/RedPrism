#include "Composition.Tests.CapturedCompletion.h"

#include "GpuWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Event-driven retirement uses the actual captured stream; independent copies prove completed data visibility.
namespace
{
static_assert(45 < JobScheduler::kCapacity);

int gFailures = 0;
int gUnsupported = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s captured-completion.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ReportUnsupported(const char* name) noexcept
{
    std::printf("UNSUPPORTED captured-completion.%s\n", name);
    ++gUnsupported;
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

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Rig final
{
  public:
    ~Rig()
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
    }

    bool Initialize(bool bindDevice = true) noexcept
    {
        HRESULT result = device.Create();
        if (FAILED(result))
            return false;

        result = static_cast<HRESULT>(scheduler.Initialize(1, {}, {StableMemory, nullptr}));
        if (FAILED(result))
            return false;

        result = queue.Initialize(scheduler);
        if (FAILED(result))
            return false;

        if (bindDevice)
        {
            result = queue.Bind(device.device.get(), device.context.get());
            if (FAILED(result))
                return false;
            const auto route = queue.CompletionRoute();
            if (route != GpuCapabilities::CompletionRoute::MonitoredFence &&
                route != GpuCapabilities::CompletionRoute::EnqueueSetEvent)
                return false;
        }
        return true;
    }

    WarpDevice device;
    JobScheduler scheduler;
    GpuWorkQueue queue;
};
#pragma warning(pop)

struct Delivery final
{
    JobTicket ticket{};
    std::uint64_t generation = 0;
    HRESULT result = E_PENDING;
};

class Receiver final : public GpuCompletionReceiver
{
  public:
    void GpuCompletionReady(JobTicket ticket, std::uint64_t generation, HRESULT result) noexcept override
    {
        if (count < deliveries.size())
            deliveries[count] = {ticket, generation, result};
        ++count;
    }

    [[nodiscard]] const Delivery* Find(JobTicket ticket) const noexcept
    {
        for (std::size_t index = 0; index < count && index < deliveries.size(); ++index)
            if (deliveries[index].ticket == ticket)
                return &deliveries[index];
        return nullptr;
    }

    std::array<Delivery, GpuWorkQueue::kCapacity + 1> deliveries{};
    std::size_t count = 0;
};

bool PumpUntil(Rig& rig, Receiver& receiver, std::size_t target, const char* eventControl) noexcept
{
    // The harness owner thread drains scheduler work and pumps the queue. Worker waiters only signal WakeHandle;
    // they never call this receiver. Call sites declare Receiver before Rig so it outlives queue teardown.
    constexpr std::uint64_t timeoutMs = 10000;
    const std::uint64_t start = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(rig.scheduler);
        rig.queue.Pump();
        if (receiver.count >= target)
        {
            Expect(true, eventControl);
            return true;
        }
        const std::uint64_t elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs)
        {
            Expect(false, eventControl);
            return false;
        }
        const HANDLE wake = static_cast<HANDLE>(rig.scheduler.WakeHandle());
        if (!wake || WaitForSingleObject(wake, static_cast<DWORD>(timeoutMs - elapsed)) != WAIT_OBJECT_0)
        {
            Expect(false, eventControl);
            return false;
        }
    }
}

wil::com_ptr_nothrow<ID3D11Texture2D> MakeTexture(WarpDevice& device,
                                                  const std::array<std::uint32_t, 4>& pixels) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 2;
    desc.Height = 2;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = static_cast<UINT>(2u * sizeof(std::uint32_t));
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(device.device->CreateTexture2D(&desc, &initial, texture.put())))
        return {};
    return texture;
}

wil::com_ptr_nothrow<ID3D11Texture2D> MakeStaging(WarpDevice& device) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 2;
    desc.Height = 2;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (FAILED(device.device->CreateTexture2D(&desc, nullptr, texture.put())))
        return {};
    return texture;
}

bool ReadCopiedPixelsDoNotWait(WarpDevice& device, ID3D11Texture2D* staging,
                               const std::array<std::uint32_t, 4>& expected) noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device.context->Map(staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(result))
        return false;
    bool matches = true;
    for (unsigned int y = 0; y < 2; ++y)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData) +
                                                                 static_cast<std::size_t>(y) * mapped.RowPitch);
        for (unsigned int x = 0; x < 2; ++x)
        {
            const std::size_t pixel = static_cast<std::size_t>(y) * 2 + x;
            matches = matches && row[x] == expected[pixel];
        }
    }
    device.context->Unmap(staging, 0);
    return matches;
}

bool TestCaptureRefusalsAreNonMutating() noexcept
{
    Receiver localReceiver;
    Receiver foreignReceiver;
    Rig local;
    const bool localReady = local.Initialize(false);
    Expect(localReady, "setup.capture-local-unbound-rig-ready");
    if (!localReady)
        return false;

    GpuCompletionContext empty;
    Expect(empty.DeviceStatusOwner() == HRESULT_FROM_WIN32(ERROR_INVALID_STATE),
           "capture.empty-device-status-does-not-acknowledge-work");
    const HRESULT unbound = local.queue.CaptureCompletionContext(empty);
    Expect(FAILED(unbound) && !empty && empty.DeviceGeneration() == 0,
           "capture.unbound-refusal-leaves-default-context-empty");

    const HRESULT bindLocal = local.queue.Bind(local.device.device.get(), local.device.context.get());
    Expect(SUCCEEDED(bindLocal), "capture.bind-local-warp-device");
    if (FAILED(bindLocal))
        return false;
    const auto localRoute = local.queue.CompletionRoute();
    Expect(localRoute == GpuCapabilities::CompletionRoute::MonitoredFence ||
               localRoute == GpuCapabilities::CompletionRoute::EnqueueSetEvent,
           "capture.local-device-selects-supported-completion-route");
    if (localRoute != GpuCapabilities::CompletionRoute::MonitoredFence &&
        localRoute != GpuCapabilities::CompletionRoute::EnqueueSetEvent)
        return false;

    GpuCompletionContext captured;
    const HRESULT capture = local.queue.CaptureCompletionContext(captured);
    Expect(capture == S_OK && captured && captured.DeviceGeneration() == local.queue.DeviceGeneration(),
           "capture.bound-context-retains-original-generation");
    Expect(captured.DeviceStatusOwner() == S_OK, "capture.original-healthy-device-status-is-independent");
    if (FAILED(capture))
        return false;

    GpuCompletionContext occupied = captured;
    const std::uint64_t occupiedGeneration = occupied.DeviceGeneration();
    WarpDevice replacement;
    const HRESULT created = replacement.Create();
    Expect(SUCCEEDED(created), "capture.create-replacement-device-for-occupied-value-check");
    if (FAILED(created))
        return false;
    // Matching the captured owner/device/context is immutable metadata work, including after rebind.
    // A worker receives strong owners for retention only; it never queries their immediate context.
    const auto ownerThread = GetCurrentThreadId();
    std::array<bool, 9> binding{};
    std::thread verifier(
        [&]() noexcept
        {
            const auto foreignThread = GetCurrentThreadId();
            GpuCompletionContext invalid;
            binding = {
                captured.MatchesBindingForWork(ownerThread, local.device.device.get(), local.device.context.get()),
                !captured.MatchesBindingForWork(0, local.device.device.get(), local.device.context.get()),
                !captured.MatchesBindingForWork(foreignThread, local.device.device.get(), local.device.context.get()),
                !captured.MatchesBindingForWork(ownerThread, nullptr, local.device.context.get()),
                !captured.MatchesBindingForWork(ownerThread, local.device.device.get(), nullptr),
                !captured.MatchesBindingForWork(ownerThread, replacement.device.get(), local.device.context.get()),
                !captured.MatchesBindingForWork(ownerThread, local.device.device.get(), replacement.context.get()),
                !captured.MatchesBindingForWork(ownerThread, replacement.device.get(), replacement.context.get()),
                !invalid.MatchesBindingForWork(ownerThread, local.device.device.get(), local.device.context.get())};
        });
    verifier.join();
    constexpr std::array<const char*, 9> bindingNames{"binding.worker-matches-exact-owner-device-context",
                                                      "binding.worker-refuses-zero-owner",
                                                      "binding.worker-refuses-foreign-owner",
                                                      "binding.worker-refuses-null-device",
                                                      "binding.worker-refuses-null-context",
                                                      "binding.worker-refuses-foreign-device",
                                                      "binding.worker-refuses-foreign-context",
                                                      "binding.worker-refuses-foreign-pair",
                                                      "binding.worker-refuses-empty-capture"};
    for (std::size_t index = 0; index < binding.size(); ++index)
        Expect(binding[index], bindingNames[index]);
    const HRESULT rebound = local.queue.Bind(replacement.device.get(), replacement.context.get());
    Expect(SUCCEEDED(rebound) && local.queue.DeviceGeneration() != occupiedGeneration,
           "capture.rebind-advances-current-generation-before-occupied-refusal");
    if (FAILED(rebound))
        return false;
    Expect(captured.MatchesBindingForWork(ownerThread, local.device.device.get(), local.device.context.get()) &&
               !captured.MatchesBindingForWork(ownerThread, replacement.device.get(), replacement.context.get()),
           "binding.rebind-retains-original-identities-without-granting-current-authority");
    const HRESULT recapture = local.queue.CaptureCompletionContext(occupied);
    Expect(FAILED(recapture) && occupied && occupied.DeviceGeneration() == occupiedGeneration &&
               occupied.DeviceGeneration() != local.queue.DeviceGeneration() &&
               local.queue.HasRetirementCompletionSlot(occupied),
           "capture.occupied-destination-refused-with-value-unchanged");

    Rig foreign;
    const bool foreignReady = foreign.Initialize();
    Expect(foreignReady, "setup.capture-foreign-bound-rig-ready");
    if (!foreignReady)
        return false;
    const bool foreignTokenReady = !foreign.queue.HasRetirementCompletionSlot(captured);
    const HRESULT foreignAdmission =
        foreign.queue.EnqueueRetirementCompletion(captured, JobTicket{40, 1}, foreignReceiver);
    Expect(foreignTokenReady && FAILED(foreignAdmission) && foreignReceiver.count == 0,
           "capture.foreign-queue-token-refused-without-callback");
    Expect(localReceiver.count == 0, "capture.refusals-produce-no-local-callback");
    return true;
}

bool TestOriginalAndReplacementStreamsCompleteWithCopies() noexcept
{
    Receiver receiver;
    Rig rig;
    const bool ready = rig.Initialize();
    Expect(ready, "setup.rebind-rig-ready");
    if (!ready)
        return false;
    if (rig.queue.CompletionRoute() != GpuCapabilities::CompletionRoute::MonitoredFence)
        ReportUnsupported("rebind.monitored-fence-route-unavailable-counter-reset-check-not-exercised");

    GpuCompletionContext original;
    const HRESULT captured = rig.queue.CaptureCompletionContext(original);
    Expect(captured == S_OK && original, "rebind.capture-original-command-stream");
    if (FAILED(captured))
        return false;

    const std::uint64_t oldGeneration = original.DeviceGeneration();
    constexpr std::array<JobTicket, 3> warmupTickets{JobTicket{41, 11}, JobTicket{41, 12}, JobTicket{41, 13}};
    constexpr std::array<const char*, 3> warmupAdmitControls{"rebind.warmup-1-admitted-on-original-generation",
                                                             "rebind.warmup-2-admitted-on-original-generation",
                                                             "rebind.warmup-3-admitted-on-original-generation"};
    constexpr std::array<const char*, 3> warmupWaitControls{
        "rebind.warmup-1-event-delivered", "rebind.warmup-2-event-delivered", "rebind.warmup-3-event-delivered"};
    constexpr std::array<const char*, 3> warmupReceiptControls{"rebind.warmup-1-exact-ticket-old-generation-sok",
                                                               "rebind.warmup-2-exact-ticket-old-generation-sok",
                                                               "rebind.warmup-3-exact-ticket-old-generation-sok"};
    for (std::size_t index = 0; index < warmupTickets.size(); ++index)
    {
        const HRESULT admitted = rig.queue.EnqueueCompletion(warmupTickets[index], receiver);
        Expect(admitted == S_OK, warmupAdmitControls[index]);
        if (FAILED(admitted))
            return false;
        if (!PumpUntil(rig, receiver, index + 1, warmupWaitControls[index]))
            return false;
        const Delivery* delivery = receiver.Find(warmupTickets[index]);
        Expect(delivery && delivery->ticket == warmupTickets[index] && delivery->generation == oldGeneration &&
                   delivery->result == S_OK,
               warmupReceiptControls[index]);
        if (!delivery || delivery->generation != oldGeneration || delivery->result != S_OK)
            return false;
    }
    Expect(receiver.count == 3, "rebind.exactly-three-original-generation-warmups-acknowledged-before-rebind");

    constexpr std::array<std::uint32_t, 4> oldPixels{0x10203040u, 0x55667788u, 0x90ABCDEFu, 0xDEADBEEFu};
    auto oldSource = MakeTexture(rig.device, oldPixels);
    auto oldStaging = MakeStaging(rig.device);
    Expect(bool(oldSource) && bool(oldStaging), "rebind.create-original-copy-resources");
    if (!oldSource || !oldStaging)
        return false;

    // This is useful work on the original stream and must precede retirement. The later API call records only a
    // barrier.
    rig.device.context->CopyResource(oldStaging.get(), oldSource.get());
    rig.queue.RetireDevice();

    WarpDevice replacement;
    const HRESULT created = replacement.Create();
    Expect(SUCCEEDED(created), "rebind.create-replacement-warp-device");
    if (FAILED(created))
        return false;
    const HRESULT bound = rig.queue.Bind(replacement.device.get(), replacement.context.get());
    Expect(SUCCEEDED(bound) && rig.queue.DeviceGeneration() != oldGeneration,
           "rebind.bind-replacement-with-distinct-generation");
    if (FAILED(bound))
        return false;
    Expect(rig.queue.HasRetirementCompletionSlot(original), "rebind.original-token-retains-retirement-slot");

    const JobTicket oldTicket{41, 1};
    const HRESULT oldAdmitted = rig.queue.EnqueueRetirementCompletion(original, oldTicket, receiver);
    Expect(oldAdmitted == S_OK, "rebind.enqueue-old-stream-barrier-after-three-warmups-and-new-bind");
    if (FAILED(oldAdmitted))
        return false;

    constexpr std::array<std::uint32_t, 4> newPixels{0xA1020304u, 0xB5566778u, 0xC90ABCDEu, 0xDDEADBEEu};
    auto newSource = MakeTexture(replacement, newPixels);
    auto newStaging = MakeStaging(replacement);
    Expect(bool(newSource) && bool(newStaging), "rebind.create-replacement-copy-resources");
    if (!newSource || !newStaging)
        return false;
    replacement.context->CopyResource(newStaging.get(), newSource.get());

    const JobTicket currentTicket{41, 2};
    const std::uint64_t currentGeneration = rig.queue.DeviceGeneration();
    const HRESULT currentAdmitted = rig.queue.EnqueueCompletion(currentTicket, receiver);
    Expect(currentAdmitted == S_OK, "rebind.enqueue-current-stream-after-old-retirement-barrier");
    if (FAILED(currentAdmitted))
        return false;

    if (!PumpUntil(rig, receiver, 5, "rebind.both-streams-wake-through-scheduler-events"))
        return false;
    const Delivery* oldDelivery = receiver.Find(oldTicket);
    const Delivery* currentDelivery = receiver.Find(currentTicket);
    Expect(receiver.count == 5 && oldDelivery && oldDelivery->generation == oldGeneration &&
               oldDelivery->result == E_ABORT && currentDelivery && currentDelivery->generation == currentGeneration &&
               currentDelivery->result == S_OK,
           "rebind.one-old-abort-and-one-current-success-with-exact-generations");
    Expect(receiver.count == 5 && ReadCopiedPixelsDoNotWait(rig.device, oldStaging.get(), oldPixels),
           "rebind.original-copy-data-visible-by-independent-do-not-wait-map-after-retirement-event");
    Expect(receiver.count == 5 && ReadCopiedPixelsDoNotWait(replacement, newStaging.get(), newPixels),
           "rebind.replacement-copy-data-visible-by-independent-do-not-wait-map-after-current-event");

    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    Expect(receiver.count == 5, "rebind.no-lost-or-duplicate-delivery-after-drain");
    return true;
}

bool TestForcedEventCapturedStream() noexcept
{
    Receiver receiver;
    Rig rig;
    const bool ready = rig.Initialize(false);
    Expect(ready, "setup.forced-event-rig-ready");
    if (!ready)
        return false;

    const HRESULT bound = rig.queue.Bind(rig.device.device.get(), rig.device.context.get(), true);
    if (bound == E_NOINTERFACE)
    {
        ReportUnsupported("event.forced-enqueue-set-event-unavailable-on-warp");
        return true;
    }
    Expect(bound == S_OK && rig.queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent,
           "event.forced-enqueue-set-event-route-selected");
    if (bound != S_OK || rig.queue.CompletionRoute() != GpuCapabilities::CompletionRoute::EnqueueSetEvent)
        return false;

    GpuCompletionContext captured;
    const HRESULT capture = rig.queue.CaptureCompletionContext(captured);
    Expect(capture == S_OK && captured, "event.capture-strong-original-context");
    if (FAILED(capture))
        return false;

    constexpr std::array<std::uint32_t, 4> pixels{0x01020304u, 0x11223344u, 0x55667788u, 0x99AABBCCu};
    auto source = MakeTexture(rig.device, pixels);
    auto staging = MakeStaging(rig.device);
    Expect(bool(source) && bool(staging), "event.create-independent-copy-resources");
    if (!source || !staging)
        return false;

    // Capture precedes useful work. Both copies are recorded on this original context before any retirement.
    rig.device.context->CopyResource(staging.get(), source.get());
    const std::uint64_t originalGeneration = captured.DeviceGeneration();
    const JobTicket currentTicket{45, 1};
    const HRESULT currentAdmitted = rig.queue.EnqueueCompletion(currentTicket, receiver);
    Expect(currentAdmitted == S_OK, "event.enqueue-current-completion-after-copy");
    if (FAILED(currentAdmitted))
        return false;
    if (!PumpUntil(rig, receiver, 1, "event.current-enqueue-set-event-wakes-owner"))
        return false;
    const Delivery* currentDelivery = receiver.Find(currentTicket);
    Expect(receiver.count == 1 && currentDelivery && currentDelivery->ticket == currentTicket &&
               currentDelivery->generation == originalGeneration && currentDelivery->result == S_OK,
           "event.current-stream-one-exact-sok-delivery");
    Expect(receiver.count == 1 && ReadCopiedPixelsDoNotWait(rig.device, staging.get(), pixels),
           "event.current-copy-visible-after-real-enqueue-set-event");

    rig.device.context->CopyResource(staging.get(), source.get());
    rig.queue.RetireDevice();
    const JobTicket retiredTicket{45, 2};
    const HRESULT retiredAdmitted = rig.queue.EnqueueRetirementCompletion(captured, retiredTicket, receiver);
    Expect(retiredAdmitted == S_OK, "event.enqueue-captured-stream-retirement-barrier-while-unbound");
    if (FAILED(retiredAdmitted))
        return false;
    if (!PumpUntil(rig, receiver, 2, "event.retirement-enqueue-set-event-wakes-owner"))
        return false;
    const Delivery* retiredDelivery = receiver.Find(retiredTicket);
    Expect(receiver.count == 2 && retiredDelivery && retiredDelivery->ticket == retiredTicket &&
               retiredDelivery->generation == originalGeneration && retiredDelivery->result == E_ABORT,
           "event.retired-stream-one-exact-eabort-with-original-generation");
    Expect(receiver.count == 2 && ReadCopiedPixelsDoNotWait(rig.device, staging.get(), pixels),
           "event.pre-retirement-copy-visible-after-real-retirement-event");
    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    Expect(receiver.count == 2, "event.no-lost-or-duplicate-callbacks-after-drain");
    return true;
}

bool TestTwoInFlightRefusalAndReoffer() noexcept
{
    Receiver receiver;
    Rig rig;
    const bool ready = rig.Initialize();
    Expect(ready, "setup.capacity-rig-ready");
    if (!ready)
        return false;

    const JobTicket first{42, 1};
    const JobTicket second{42, 2};
    const JobTicket third{42, 3};
    const HRESULT firstResult = rig.queue.EnqueueCompletion(first, receiver);
    const HRESULT secondResult = rig.queue.EnqueueCompletion(second, receiver);
    const HRESULT refused = rig.queue.EnqueueCompletion(third, receiver);
    Expect(firstResult == S_OK && secondResult == S_OK && refused == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) &&
               receiver.count == 0,
           "capacity.two-admitted-third-refused-without-callback");
    if (FAILED(firstResult) || FAILED(secondResult))
        return false;

    if (!PumpUntil(rig, receiver, 2, "capacity.first-two-events-release-inflight-slots"))
        return false;
    const Delivery* firstDelivery = receiver.Find(first);
    const Delivery* secondDelivery = receiver.Find(second);
    Expect(receiver.count == 2 && firstDelivery && firstDelivery->result == S_OK && secondDelivery &&
               secondDelivery->result == S_OK && rig.queue.HasCompletionSlot(),
           "capacity.event-release-restores-slot-after-two-exact-successes");

    const HRESULT reoffered = rig.queue.EnqueueCompletion(third, receiver);
    Expect(reoffered == S_OK, "capacity.refused-third-can-be-reoffered-after-real-event-release");
    if (FAILED(reoffered))
        return false;
    if (!PumpUntil(rig, receiver, 3, "capacity.reoffered-third-wakes-after-real-event"))
        return false;
    const Delivery* thirdDelivery = receiver.Find(third);
    Expect(receiver.count == 3 && thirdDelivery && thirdDelivery->ticket == third && thirdDelivery->result == S_OK,
           "capacity.reoffered-third-delivers-once-with-success");
    (void)DrainCompletions(rig.scheduler);
    rig.queue.Pump();
    Expect(receiver.count == 3, "capacity.no-duplicate-callback-after-final-drain");
    return true;
}

bool TestUnboundRetirementAcknowledgesOriginalStream() noexcept
{
    Receiver receiver;
    Rig rig;
    const bool ready = rig.Initialize();
    Expect(ready, "setup.unbound-rig-ready");
    if (!ready)
        return false;
    GpuCompletionContext original;
    const HRESULT captured = rig.queue.CaptureCompletionContext(original);
    Expect(captured == S_OK && original, "unbound.capture-original-context-before-retire");
    if (FAILED(captured))
        return false;

    constexpr std::array<std::uint32_t, 4> pixels{0x01020304u, 0x11223344u, 0x55667788u, 0x99AABBCCu};
    auto source = MakeTexture(rig.device, pixels);
    auto staging = MakeStaging(rig.device);
    Expect(bool(source) && bool(staging), "unbound.create-original-copy-resources");
    if (!source || !staging)
        return false;
    rig.device.context->CopyResource(staging.get(), source.get());

    const std::uint64_t generation = original.DeviceGeneration();
    rig.queue.RetireDevice();
    Expect(rig.queue.DeviceGeneration() == generation &&
               rig.queue.CompletionRoute() == GpuCapabilities::CompletionRoute::None,
           "unbound.retire-leaves-queue-unbound-at-original-generation");
    Expect(rig.queue.HasRetirementCompletionSlot(original), "unbound.original-token-has-retirement-slot-without-bind");
    const JobTicket ticket{43, 1};
    const HRESULT admitted = rig.queue.EnqueueRetirementCompletion(original, ticket, receiver);
    Expect(admitted == S_OK, "unbound.enqueue-original-stream-barrier-without-rebind");
    if (FAILED(admitted))
        return false;

    if (!PumpUntil(rig, receiver, 1, "unbound.original-stream-event-wakes-without-current-device"))
        return false;
    const Delivery* delivery = receiver.Find(ticket);
    Expect(receiver.count == 1 && delivery && delivery->generation == generation && delivery->result == E_ABORT,
           "unbound.one-retired-abort-delivery-with-captured-generation");
    Expect(receiver.count == 1 && ReadCopiedPixelsDoNotWait(rig.device, staging.get(), pixels),
           "unbound.original-copy-data-visible-by-independent-do-not-wait-map");
    return true;
}

bool TestClosedQueueRejectsOldCaptureAfterReinitialize() noexcept
{
    Receiver receiver;
    Rig rig;
    const bool ready = rig.Initialize();
    Expect(ready, "setup.reinitialize-rig-ready");
    if (!ready)
        return false;
    GpuCompletionContext oldToken;
    const HRESULT captured = rig.queue.CaptureCompletionContext(oldToken);
    Expect(captured == S_OK && oldToken, "reinitialize.capture-token-before-close");
    if (FAILED(captured))
        return false;

    rig.queue.StopNotifications();
    rig.queue.CloseAfterWorkers();
    const HRESULT initialized = rig.queue.Initialize(rig.scheduler);
    Expect(SUCCEEDED(initialized), "reinitialize.same-queue-object-initializes-new-identity");
    if (FAILED(initialized))
        return false;
    WarpDevice replacement;
    const HRESULT created = replacement.Create();
    Expect(SUCCEEDED(created), "reinitialize.create-device-for-new-queue-identity");
    if (FAILED(created))
        return false;
    const HRESULT bound = rig.queue.Bind(replacement.device.get(), replacement.context.get());
    Expect(SUCCEEDED(bound), "reinitialize.bind-device-after-queue-reinitialize");
    if (FAILED(bound))
        return false;

    const bool staleReady = rig.queue.HasRetirementCompletionSlot(oldToken);
    const HRESULT staleAdmission = rig.queue.EnqueueRetirementCompletion(oldToken, JobTicket{44, 1}, receiver);
    Expect(!staleReady && FAILED(staleAdmission) && receiver.count == 0,
           "reinitialize.old-origin-token-refused-after-same-address-reuse");
    return true;
}
} // namespace

int RunCapturedCompletionContextTests()
{
    gFailures = 0;
    gUnsupported = 0;
    (void)TestCaptureRefusalsAreNonMutating();
    (void)TestOriginalAndReplacementStreamsCompleteWithCopies();
    (void)TestForcedEventCapturedStream();
    (void)TestTwoInFlightRefusalAndReoffer();
    (void)TestUnboundRetirementAcknowledgesOriginalStream();
    (void)TestClosedQueueRejectsOldCaptureAfterReinitialize();
    std::printf("SUMMARY captured-completion.failures=%d unsupported=%d\n", gFailures, gUnsupported);
    return gFailures;
}

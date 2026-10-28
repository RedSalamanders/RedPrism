#include "Composition.Tests.GpuBufferSlots.h"

#include "GpuWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-buffer-slots.%s\n", condition ? "PASS" : "FAIL", name);
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

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Rig final
{
  public:
    ~Rig()
    {
        if (!_closed)
            Close();
    }

    bool InitializeUnbound() noexcept
    {
        const HRESULT deviceResult = device.Create();
        Expect(SUCCEEDED(deviceResult), "setup.warp-device");
        if (FAILED(deviceResult))
            return false;

        const std::int32_t schedulerResult = scheduler.Initialize(1, {}, {StableMemory, nullptr});
        Expect(schedulerResult == S_OK, "setup.scheduler-initialize");
        if (schedulerResult != S_OK)
            return false;

        const HRESULT queueResult = queue.Initialize(scheduler);
        Expect(SUCCEEDED(queueResult), "setup.queue-initialize");
        return SUCCEEDED(queueResult);
    }

    bool Bind(Device& target) noexcept
    {
        const HRESULT result = queue.Bind(target.device.get(), target.context.get(), true);
        Expect(SUCCEEDED(result), "setup.bind-warp-device");
        return SUCCEEDED(result);
    }

    void Close() noexcept
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        _closed = true;
    }

    Device device;
    JobScheduler scheduler;
    GpuWorkQueue queue;

  private:
    bool _closed = false;
};
#pragma warning(pop)

constexpr unsigned int kWidth = 3;
constexpr unsigned int kHeight = 2;
constexpr std::size_t kValueCount = static_cast<std::size_t>(kWidth) * kHeight;
constexpr std::size_t kMaxDeliveries = GpuWorkQueue::kCapacity + 1;
constexpr std::uint32_t kSourceValues[kValueCount]{0u, 1u, 255u, 256u, 65535u, UINT32_MAX};

wil::com_ptr_nothrow<ID3D11Buffer> MakeBuffer(Device& device) noexcept
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(kValueCount * sizeof(std::uint32_t));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(std::uint32_t);
    const D3D11_SUBRESOURCE_DATA data{kSourceValues, 0, 0};
    wil::com_ptr_nothrow<ID3D11Buffer> buffer;
    if (FAILED(device.device->CreateBuffer(&desc, &data, buffer.put())))
        return {};
    return buffer;
}

struct LoanMetadata final
{
    GpuReadbackLease lease{};
    std::uint64_t deviceGeneration = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    std::size_t count = 0;
    HRESULT result = E_PENDING;
    bool hasData = false;
};

class LoanReceiver final : public GpuBufferReadbackReceiver
{
  public:
    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        ++deliveries;
        if (deliveryCount >= loans.size())
        {
            overflow = true;
            return;
        }
        auto& loan = loans[deliveryCount++];
        loan.lease = values.lease;
        loan.deviceGeneration = values.deviceGeneration;
        loan.width = values.width;
        loan.height = values.height;
        loan.count = values.count;
        loan.result = result;
        loan.hasData = values.data != nullptr;
        lastTicket = ticket;
    }

    std::array<LoanMetadata, kMaxDeliveries> loans{};
    std::size_t deliveryCount = 0;
    unsigned int deliveries = 0;
    JobTicket lastTicket{};
    bool overflow = false;
};

[[noreturn]] void FailWhileBorrowed(const char* reason) noexcept
{
    std::printf("FAIL gpu-buffer-slots.%s\n", reason);
    std::fflush(stdout);
    std::_Exit(1); // Keep the receiver, queue, and any admitted borrowers alive until process termination.
}

void WaitForDeliveries(Rig& rig, LoanReceiver& receiver, std::size_t target) noexcept
{
    constexpr std::uint64_t timeoutMs = 10000;
    const std::uint64_t started = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(rig.scheduler);
        rig.queue.Pump();
        if (receiver.deliveryCount >= target)
            return;

        const std::uint64_t elapsed = GetTickCount64() - started;
        if (elapsed >= timeoutMs)
            FailWhileBorrowed("timeout-with-admitted-borrower");
        const auto remaining = static_cast<DWORD>(timeoutMs - elapsed);
        const auto wake = static_cast<HANDLE>(rig.scheduler.WakeHandle());
        if (!wake)
            FailWhileBorrowed("missing-scheduler-event-with-admitted-borrower");
        const DWORD waited = WaitForSingleObject(wake, remaining);
        if (waited != WAIT_OBJECT_0)
            FailWhileBorrowed("event-wait-failed-with-admitted-borrower");
    }
}

bool QueueOne(GpuWorkQueue& queue, ID3D11Buffer* source, LoanReceiver& receiver, std::size_t sequence) noexcept
{
    const JobTicket ticket{sequence % JobScheduler::kCapacity, sequence + 1};
    return queue.EnqueueBufferReadback(source, kWidth, kHeight, ticket, receiver) == S_OK;
}

bool TestReadbackSlotStates()
{
    GpuWorkQueue uninitialized;
    Expect(!uninitialized.HasReadbackSlot(), "state.default-uninitialized-false");

    // Rig shuts down first on every return, joining queue waiters before this receiver is destroyed.
    LoanReceiver receiver;
    Rig rig;
    if (!rig.InitializeUnbound())
        return false;

    Expect(!rig.queue.HasReadbackSlot(), "state.initialized-unbound-false");
    if (!rig.Bind(rig.device))
        return false;
    Expect(rig.queue.HasReadbackSlot(), "state.bound-free-slot-true");

    auto source = MakeBuffer(rig.device);
    Expect(bool(source), "setup.structured-source-buffer");
    if (!source)
        return false;

    bool allAdmitted = true;
    for (std::size_t i = 0; i < GpuWorkQueue::kCapacity; ++i)
    {
        if (!QueueOne(rig.queue, source.get(), receiver, i))
        {
            allAdmitted = false;
            break;
        }
    }
    Expect(allAdmitted, "pending.admit-all-eight-slots");
    if (!allAdmitted)
        return false;
    Expect(!rig.queue.HasReadbackSlot(), "pending.full-eight-slots-false");
    Expect(rig.queue.EnqueueBufferReadback(source.get(), kWidth, kHeight, JobTicket{0, 99}, receiver) ==
               HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
           "pending.ninth-admission-refused-by-quota");

    rig.queue.Pump();
    Expect(rig.queue.InFlightCount() > 0 && !rig.queue.HasReadbackSlot(), "waiting.real-queue-pump-remains-full");
    WaitForDeliveries(rig, receiver, GpuWorkQueue::kCapacity);
    Expect(receiver.deliveries == GpuWorkQueue::kCapacity && receiver.deliveryCount == GpuWorkQueue::kCapacity &&
               !receiver.overflow,
           "lent.all-eight-actual-mapped-loans-delivered");

    bool metadataValid = receiver.deliveryCount == GpuWorkQueue::kCapacity;
    if (metadataValid)
    {
        for (std::size_t i = 0; i < GpuWorkQueue::kCapacity; ++i)
        {
            const auto& loan = receiver.loans[i];
            metadataValid = metadataValid && loan.result == S_OK && loan.hasData && loan.width == kWidth &&
                            loan.height == kHeight && loan.count == kValueCount &&
                            loan.lease.slot < GpuWorkQueue::kCapacity && loan.lease.generation != 0 &&
                            loan.deviceGeneration == rig.queue.DeviceGeneration();
        }
    }
    Expect(metadataValid, "lent.owner-retains-only-valid-view-and-lease-metadata");
    Expect(rig.queue.LentCount() == GpuWorkQueue::kCapacity && !rig.queue.HasReadbackSlot(),
           "lent.eight-mapped-loans-exhaust-readiness");
    if (!metadataValid)
        return false;

    const GpuReadbackLease acknowledged = receiver.loans[0].lease;
    const bool firstReleased = rig.queue.ReleaseMapped(acknowledged);
    Expect(firstReleased, "lease.acknowledge-exact-mapped-loan");
    Expect(!rig.queue.ReleaseMapped(acknowledged), "lease.duplicate-acknowledgement-rejected");
    Expect(rig.queue.HasReadbackSlot(), "lease.released-slot-restores-readiness");

    const std::uint64_t oldDeviceGeneration = rig.queue.DeviceGeneration();
    rig.queue.RetireDevice();
    Expect(!rig.queue.HasReadbackSlot(), "retired.unbound-device-false");
    bool oldLoansPreserved = rig.queue.LentCount() == GpuWorkQueue::kCapacity - 1;
    for (std::size_t i = 1; i < GpuWorkQueue::kCapacity; ++i)
        oldLoansPreserved = oldLoansPreserved && receiver.loans[i].deviceGeneration != 0 &&
                            receiver.loans[i].deviceGeneration == oldDeviceGeneration;
    Expect(oldLoansPreserved, "retired.remaining-old-lent-leases-stay-owned");

    Device replacement;
    const HRESULT replacementResult = replacement.Create();
    Expect(SUCCEEDED(replacementResult), "rebind.create-second-warp-device");
    if (FAILED(replacementResult))
        return false;
    if (!rig.Bind(replacement))
        return false;
    auto replacementSource = MakeBuffer(replacement);
    Expect(bool(replacementSource), "rebind.create-new-device-source");
    if (!replacementSource)
        return false;
    Expect(rig.queue.HasReadbackSlot(), "rebind.free-slot-true-with-old-loans-lent");
    const bool newRequestAdmitted = QueueOne(rig.queue, replacementSource.get(), receiver, GpuWorkQueue::kCapacity);
    Expect(newRequestAdmitted, "rebind.new-generation-request-admitted");
    if (!newRequestAdmitted)
        return false;
    WaitForDeliveries(rig, receiver, GpuWorkQueue::kCapacity + 1);
    const bool newLoanValid = receiver.deliveryCount == GpuWorkQueue::kCapacity + 1 &&
                              receiver.loans[GpuWorkQueue::kCapacity].result == S_OK &&
                              receiver.loans[GpuWorkQueue::kCapacity].hasData &&
                              receiver.loans[GpuWorkQueue::kCapacity].deviceGeneration == rig.queue.DeviceGeneration();
    Expect(newLoanValid, "rebind.new-device-loan-delivered-with-current-generation");
    if (!newLoanValid)
        return false;

    bool oldLoansAcknowledged = true;
    for (std::size_t i = 1; i < GpuWorkQueue::kCapacity; ++i)
        oldLoansAcknowledged = rig.queue.ReleaseMapped(receiver.loans[i].lease) && oldLoansAcknowledged;
    Expect(oldLoansAcknowledged && rig.queue.LentCount() == 1,
           "rebind.each-old-lease-acknowledged-against-its-original-slot-generation");
    Expect(rig.queue.ReleaseMapped(receiver.loans[GpuWorkQueue::kCapacity].lease) &&
               !rig.queue.ReleaseMapped(receiver.loans[GpuWorkQueue::kCapacity].lease),
           "rebind.new-lease-acknowledges-once");

    rig.queue.RetireDevice();
    Expect(!rig.queue.HasReadbackSlot() && rig.queue.LentCount() == 0 && rig.queue.InFlightCount() == 0 &&
               rig.queue.StagingBytes() == 0,
           "cleanup.retired-context-and-all-staging-owners-released");
    rig.queue.StopNotifications();
    Expect(!rig.queue.HasReadbackSlot(), "stopped.query-false");
    rig.Close();
    Expect(rig.queue.LentCount() == 0 && rig.queue.StagingBytes() == 0,
           "cleanup.workers-closed-and-no-loans-or-staging-remain");
    return true;
}
} // namespace

int RunGpuBufferSlotsTests()
{
    gFailures = 0;
    (void)TestReadbackSlotStates();
    return gFailures;
}

#include "Composition.Tests.UtilityCurrentContext.h"

#include "GpuWork.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace RedPrism::CompositionTests
{
namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s utility-current-context.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 4ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct WarpDevice final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Rig final
{
    ~Rig()
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
    }

    HRESULT Initialize(bool bind = true) noexcept
    {
        HRESULT result = device.Create();
        if (FAILED(result))
            return result;
        result = scheduler.Initialize(1, {}, {StableMemory, nullptr});
        if (FAILED(result))
            return result;
        result = queue.Initialize(scheduler);
        if (FAILED(result) || !bind)
            return result;
        return queue.Bind(device.device.get(), device.context.get());
    }

    WarpDevice device;
    JobScheduler scheduler;
    GpuWorkQueue queue;
};
#pragma warning(pop)

bool RefusalClearsOutputs(GpuWorkQueue& queue, const GpuCompletionContext& captured, ID3D11Device* sentinelDevice,
                          ID3D11DeviceContext* sentinelContext, const char* name) noexcept
{
    ID3D11Device* borrowedDevice = sentinelDevice;
    ID3D11DeviceContext* borrowedContext = sentinelContext;
    const HRESULT result = queue.BorrowCurrentContextOwner(captured, borrowedDevice, borrowedContext);
    const bool passed = FAILED(result) && borrowedDevice == nullptr && borrowedContext == nullptr;
    Expect(passed, name);
    return passed;
}

void TestCaptureAndRebindRefusals() noexcept
{
    Rig rig;
    HRESULT result = rig.device.Create();
    Expect(SUCCEEDED(result), "setup.actual-warp-device-created");
    if (FAILED(result))
        return;

    GpuCompletionContext empty;
    RefusalClearsOutputs(rig.queue, empty, rig.device.device.get(), rig.device.context.get(),
                         "uninitialized-queue-refuses-and-clears-both-outputs");

    result = rig.scheduler.Initialize(1, {}, {StableMemory, nullptr});
    if (SUCCEEDED(result))
        result = rig.queue.Initialize(rig.scheduler);
    if (SUCCEEDED(result))
        result = rig.queue.Bind(rig.device.device.get(), rig.device.context.get());
    Expect(SUCCEEDED(result), "setup.initialized-queue-bound-to-actual-warp-pointers");
    if (FAILED(result))
        return;

    RefusalClearsOutputs(rig.queue, empty, rig.device.device.get(), rig.device.context.get(),
                         "empty-capture-refuses-and-clears-both-outputs");

    GpuCompletionContext first;
    result = rig.queue.CaptureCompletionContext(first);
    Expect(result == S_OK && first, "capture.actual-original-queue-stream");
    if (FAILED(result))
        return;

    ID3D11Device* borrowedDevice = nullptr;
    ID3D11DeviceContext* borrowedContext = nullptr;
    result = rig.queue.BorrowCurrentContextOwner(first, borrowedDevice, borrowedContext);
    Expect(result == S_OK && borrowedDevice == rig.device.device.get() && borrowedContext == rig.device.context.get(),
           "borrow-returns-exact-actual-warp-device-and-immediate-context");
    if (FAILED(result))
        return;

    Rig foreign;
    const HRESULT foreignReady = foreign.Initialize();
    Expect(SUCCEEDED(foreignReady), "setup.independent-foreign-warp-queue");
    if (SUCCEEDED(foreignReady))
        RefusalClearsOutputs(foreign.queue, first, foreign.device.device.get(), foreign.device.context.get(),
                             "foreign-capture-refuses-and-clears-both-outputs");

    const std::uint64_t firstGeneration = first.DeviceGeneration();
    result = rig.queue.Bind(rig.device.device.get(), rig.device.context.get());
    Expect(SUCCEEDED(result) && rig.queue.DeviceGeneration() != firstGeneration,
           "same-device-and-context-rebind-advances-actual-queue-generation");
    if (FAILED(result))
        return;
    RefusalClearsOutputs(rig.queue, first, rig.device.device.get(), rig.device.context.get(),
                         "same-pointer-rebind-refuses-stale-capture");

    GpuCompletionContext samePointers;
    result = rig.queue.CaptureCompletionContext(samePointers);
    Expect(result == S_OK && samePointers.DeviceGeneration() == rig.queue.DeviceGeneration(),
           "recapture-after-same-pointer-rebind-uses-current-generation");
    if (FAILED(result))
        return;
    borrowedDevice = nullptr;
    borrowedContext = nullptr;
    result = rig.queue.BorrowCurrentContextOwner(samePointers, borrowedDevice, borrowedContext);
    Expect(result == S_OK && borrowedDevice == rig.device.device.get() && borrowedContext == rig.device.context.get(),
           "same-pointer-rebind-borrows-exact-current-objects");

    WarpDevice replacement;
    result = replacement.Create();
    Expect(SUCCEEDED(result), "setup.second-actual-warp-device-created");
    if (FAILED(result))
        return;
    const std::uint64_t samePointerGeneration = samePointers.DeviceGeneration();
    result = rig.queue.Bind(replacement.device.get(), replacement.context.get());
    Expect(SUCCEEDED(result) && rig.queue.DeviceGeneration() != samePointerGeneration,
           "changed-device-and-context-rebind-advances-generation");
    if (FAILED(result))
        return;
    RefusalClearsOutputs(rig.queue, samePointers, replacement.device.get(), replacement.context.get(),
                         "changed-pointer-rebind-refuses-old-capture-and-clears-outputs");

    GpuCompletionContext changedPointers;
    result = rig.queue.CaptureCompletionContext(changedPointers);
    Expect(result == S_OK, "capture-after-changed-device-and-context-rebind");
    if (FAILED(result))
        return;
    borrowedDevice = nullptr;
    borrowedContext = nullptr;
    result = rig.queue.BorrowCurrentContextOwner(changedPointers, borrowedDevice, borrowedContext);
    Expect(result == S_OK && borrowedDevice == replacement.device.get() && borrowedContext == replacement.context.get(),
           "changed-pointer-rebind-returns-exact-replacement-objects");

    struct WorkerAttempt final
    {
        GpuWorkQueue* queue = nullptr;
        const GpuCompletionContext* captured = nullptr;
        ID3D11Device* borrowedDevice = nullptr;
        ID3D11DeviceContext* borrowedContext = nullptr;
        HRESULT result = E_PENDING;
        DWORD thread = 0;
    } attempt{&rig.queue, &changedPointers, replacement.device.get(), replacement.context.get(), E_PENDING, 0};

    const DWORD ownerThread = GetCurrentThreadId();
    std::thread worker(
        [&attempt]() noexcept
        {
            attempt.thread = GetCurrentThreadId();
            attempt.result = attempt.queue->BorrowCurrentContextOwner(*attempt.captured, attempt.borrowedDevice,
                                                                      attempt.borrowedContext);
        });
    worker.join();
    Expect(attempt.thread != ownerThread && attempt.result == HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID) &&
               !attempt.borrowedDevice && !attempt.borrowedContext,
           "joined-off-owner-thread-refusal-clears-both-outputs");

    rig.queue.RetireDevice();
    RefusalClearsOutputs(rig.queue, changedPointers, replacement.device.get(), replacement.context.get(),
                         "retired-device-refuses-capture-and-clears-both-outputs");
}

void TestDestructAndReinitializeIdentity() noexcept
{
    WarpDevice device;
    HRESULT result = device.Create();
    Expect(SUCCEEDED(result), "identity.actual-warp-device-created");
    if (FAILED(result))
        return;

    JobScheduler firstScheduler;
    JobScheduler secondScheduler;
    std::optional<GpuWorkQueue> queue;
    GpuCompletionContext original;
    result = firstScheduler.Initialize(1, {}, {StableMemory, nullptr});
    if (SUCCEEDED(result))
    {
        queue.emplace();
        result = queue->Initialize(firstScheduler);
    }
    if (SUCCEEDED(result))
        result = queue->Bind(device.device.get(), device.context.get());
    if (SUCCEEDED(result))
        result = queue->CaptureCompletionContext(original);
    Expect(result == S_OK && original, "identity.capture-before-queue-destruction");
    if (FAILED(result))
    {
        if (queue)
        {
            queue->StopNotifications();
            firstScheduler.Close();
            queue->CloseAfterWorkers();
            queue.reset();
        }
        else
            firstScheduler.Close();
        return;
    }

    GpuWorkQueue* const originalQueueAddress = &queue.value();
    const std::uint64_t originalGeneration = original.DeviceGeneration();
    queue->StopNotifications();
    RefusalClearsOutputs(queue.value(), original, device.device.get(), device.context.get(),
                         "stopped-queue-refuses-original-capture-and-clears-outputs");
    firstScheduler.Close();
    queue->CloseAfterWorkers();
    RefusalClearsOutputs(queue.value(), original, device.device.get(), device.context.get(),
                         "closed-queue-refuses-original-capture-and-clears-outputs");
    queue.reset();

    queue.emplace();
    Expect(&queue.value() == originalQueueAddress, "queue-object-storage-address-is-reused-after-destruction");
    result = secondScheduler.Initialize(1, {}, {StableMemory, nullptr});
    if (SUCCEEDED(result))
        result = queue->Initialize(secondScheduler);
    if (SUCCEEDED(result))
        result = queue->Bind(device.device.get(), device.context.get());
    GpuCompletionContext replacement;
    if (SUCCEEDED(result))
        result = queue->CaptureCompletionContext(replacement);
    Expect(result == S_OK && replacement && replacement.DeviceGeneration() == originalGeneration,
           "reinitialized-queue-reuses-numeric-generation-with-fresh-queue-identity");
    if (SUCCEEDED(result))
    {
        RefusalClearsOutputs(queue.value(), original, device.device.get(), device.context.get(),
                             "new-queue-at-reused-address-refuses-old-origin-capture");
        ID3D11Device* borrowedDevice = nullptr;
        ID3D11DeviceContext* borrowedContext = nullptr;
        const HRESULT borrowed = queue->BorrowCurrentContextOwner(replacement, borrowedDevice, borrowedContext);
        Expect(borrowed == S_OK && borrowedDevice == device.device.get() && borrowedContext == device.context.get(),
               "reinitialized-queue-borrows-only-its-new-capture");
    }

    queue->StopNotifications();
    secondScheduler.Close();
    queue->CloseAfterWorkers();
    queue.reset();
}

struct DeferredReleaseObservation final
{
    JobTicket original{};
    bool capturedOnOwner = false;
    bool resumedSameTicket = false;
    bool captureHeldAtWorkerRetirement = false;
    bool captureReleasedOnWorker = false;
    DWORD ownerThread = 0;
    DWORD releaseThread = 0;
    bool completed = false;
    JobResult terminal{};
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class DeferredCaptureRelease final : public Job
{
  public:
    DeferredCaptureRelease(JobScheduler& scheduler, GpuWorkQueue& queue,
                           DeferredReleaseObservation& observation) noexcept
        : _scheduler(scheduler), _queue(queue), _observation(observation)
    {
    }

    JobResult Run(JobContext&) override
    {
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _observation.releaseThread = GetCurrentThreadId();
        _observation.captureHeldAtWorkerRetirement = bool(_captured);
        _captured = {};
        _observation.captureReleasedOnWorker = !_captured;
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (_observation.original == JobTicket{})
            _observation.original = ticket;
        else if (ticket != _observation.original)
            return false;

        if (!_observation.capturedOnOwner)
        {
            if (result.status != JobStatus::Finished)
                return false;
            ID3D11Device* borrowedDevice = nullptr;
            ID3D11DeviceContext* borrowedContext = nullptr;
            const HRESULT captured = _queue.CaptureCompletionContext(_captured);
            const HRESULT borrowed = SUCCEEDED(captured)
                                         ? _queue.BorrowCurrentContextOwner(_captured, borrowedDevice, borrowedContext)
                                         : captured;
            _observation.capturedOnOwner =
                captured == S_OK && borrowed == S_OK && borrowedDevice != nullptr && borrowedContext != nullptr;
            _observation.ownerThread = GetCurrentThreadId();
            if (!_observation.capturedOnOwner)
                return false;
            if (!_scheduler.Resume(ticket))
                std::terminate();
            _observation.resumedSameTicket = ticket == _observation.original;
            return true;
        }

        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        _observation.terminal = result;
        _observation.completed = true;
    }

  private:
    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    DeferredReleaseObservation& _observation;
    GpuCompletionContext _captured;
};
#pragma warning(pop)

void TestOriginalTicketWorkerRelease() noexcept
{
    Rig rig;
    const HRESULT ready = rig.Initialize();
    Expect(SUCCEEDED(ready), "worker-release.actual-warp-queue-ready");
    if (FAILED(ready))
        return;

    DeferredReleaseObservation observation;
    const DWORD ownerThread = GetCurrentThreadId();
    std::unique_ptr<Job> job = std::make_unique<DeferredCaptureRelease>(rig.scheduler, rig.queue, observation);
    JobTicket submitted{};
    const HRESULT admitted = rig.scheduler.Submit(
        job, {.kind = JobClass::Background, .memoryBytes = 4096, .deferredRetirement = true}, submitted);
    Expect(SUCCEEDED(admitted) && !job, "worker-release.scheduler-parent-admitted");
    if (FAILED(admitted))
        return;

    const std::uint64_t deadline = GetTickCount64() + 5000;
    while (!observation.completed)
    {
        (void)DrainCompletions(rig.scheduler);
        if (observation.completed)
            break;
        const std::uint64_t now = GetTickCount64();
        if (now >= deadline)
        {
            Expect(false, "worker-release.deferred-parent-completes-before-timeout");
            rig.queue.StopNotifications();
            rig.scheduler.Close(); // Join while the borrowed observation still exists.
            rig.queue.CloseAfterWorkers();
            return;
        }
        const auto remaining = static_cast<DWORD>(deadline - now);
        const HANDLE wake = static_cast<HANDLE>(rig.scheduler.WakeHandle());
        if (!wake || WaitForSingleObject(wake, remaining) != WAIT_OBJECT_0)
        {
            Expect(false, "worker-release.deferred-parent-wakes-owner-before-timeout");
            rig.queue.StopNotifications();
            rig.scheduler.Close(); // Join while the borrowed observation still exists.
            rig.queue.CloseAfterWorkers();
            return;
        }
    }

    Expect(observation.original == submitted && observation.resumedSameTicket,
           "worker-release-capture-retirement-stays-under-original-parent-ticket");
    Expect(observation.capturedOnOwner && observation.ownerThread == ownerThread &&
               observation.captureHeldAtWorkerRetirement && observation.captureReleasedOnWorker &&
               observation.releaseThread != ownerThread,
           "worker-release-strong-capture-drops-on-actual-scheduler-retirement-worker");
    Expect(observation.terminal.status == JobStatus::Finished && rig.scheduler.IsIdle(),
           "worker-release-parent-finishes-after-deferred-capture-retirement");
}
} // namespace

int RunUtilityCurrentContextTests()
{
    gFailures = 0;
    TestCaptureAndRebindRefusals();
    TestDestructAndReinitializeIdentity();
    TestOriginalTicketWorkerRelease();
    return gFailures == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests

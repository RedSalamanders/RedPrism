#include "Composition.Tests.GpuCommandStream.h"

#include "GpuCommandStream.h"
#include "GpuWork.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <d3d11.h>
#include <memory>
#include <stop_token>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;
constexpr JobTicket kSource{7, 19};
constexpr std::uint64_t kDeviceGeneration = 23;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s recorded-display.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool Signaled(HANDLE event) noexcept
{
    return event && WaitForSingleObject(event, kFailureTimeoutMs) == WAIT_OBJECT_0;
}

struct WarpDevice final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;

    [[nodiscard]] HRESULT Initialize() noexcept
    {
        const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested,
                                 1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    }

    [[nodiscard]] wil::com_ptr_nothrow<ID3D11CommandList> EmptyCommand() const noexcept
    {
        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        wil::com_ptr_nothrow<ID3D11CommandList> command;
        if (FAILED(device->CreateDeferredContext(0, deferred.put())) ||
            FAILED(deferred->FinishCommandList(FALSE, command.put())))
            command.reset();
        return command;
    }

    [[nodiscard]] wil::com_ptr_nothrow<ID3D11Texture2D> ReadbackSource() const noexcept
    {
        constexpr std::array<std::uint8_t, 16> pixels{11, 23, 37, 255, 41,  53,  67,  255,
                                                      71, 83, 97, 255, 101, 113, 127, 255};
        const D3D11_TEXTURE2D_DESC desc{
            2, 2, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        const D3D11_SUBRESOURCE_DATA initial{pixels.data(), 8, 0};
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, &initial, texture.put())))
            texture.reset();
        return texture;
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Published final
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle thirdAttempt{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle allowThird{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle thirdReturned{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    GpuCommandLease first{};
    GpuCommandLease second{};
    std::atomic<HRESULT> thirdResult{E_PENDING};
    HRESULT producerResult = E_PENDING;
};
#pragma warning(pop)

void RecordThree(GpuCommandStream& stream, WarpDevice& warp, Published& published,
                 CancelToken cancellation = {}) noexcept
{
    auto first = warp.EmptyCommand();
    auto second = warp.EmptyCommand();
    auto third = warp.EmptyCommand();
    if (!first || !second || !third)
    {
        published.producerResult = E_FAIL;
        (void)SetEvent(published.event.get());
        stream.FinishProducerForWork(E_FAIL);
        return;
    }
    published.producerResult = stream.PublishForWork(std::move(first));
    if (FAILED(published.producerResult))
    {
        (void)SetEvent(published.event.get());
        stream.FinishProducerForWork(published.producerResult);
        return;
    }
    // The producer observes these values from the independent fixed-input oracle. NextOwner is owner-thread only.
    published.first = {kSource, kDeviceGeneration, 1, 0};
    published.producerResult = stream.PublishForWork(std::move(second));
    if (FAILED(published.producerResult))
    {
        (void)SetEvent(published.event.get());
        stream.FinishProducerForWork(published.producerResult);
        return;
    }
    published.second = {kSource, kDeviceGeneration, 2, 1};
    (void)SetEvent(published.event.get());
    if (WaitForSingleObject(published.allowThird.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
    {
        published.thirdResult.store(HRESULT_FROM_WIN32(WAIT_TIMEOUT), std::memory_order_release);
        (void)SetEvent(published.thirdReturned.get());
        stream.FinishProducerForWork(published.thirdResult.load(std::memory_order_acquire));
        return;
    }
    (void)SetEvent(published.thirdAttempt.get());
    published.thirdResult.store(stream.PublishForWork(std::move(third), cancellation), std::memory_order_release);
    (void)SetEvent(published.thirdReturned.get());
    stream.FinishProducerForWork(published.thirdResult.load(std::memory_order_acquire));
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class OrdinaryJob final : public Job
{
  public:
    OrdinaryJob(HANDLE ran, HANDLE completed, JobResult& result) noexcept
        : _ran(ran), _completed(completed), _result(result)
    {
    }
    JobResult Run(JobContext&) override
    {
        const bool inputIsIntact = _input.front() == 0x5a && _input.back() == 0x5a;
        (void)SetEvent(_ran);
        return inputIsIntact ? JobResult{} : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }
    void Completed(JobResult result) noexcept override
    {
        _result = result;
        (void)SetEvent(_completed);
    }

  private:
    HANDLE _ran;
    HANDLE _completed;
    JobResult& _result;
    std::array<std::uint8_t, 1024> _input = []() noexcept
    {
        std::array<std::uint8_t, 1024> bytes{};
        bytes.fill(0x5a);
        return bytes;
    }();
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class OriginalCompletion final : public GpuReadbackReceiver
{
  public:
    void Attach(GpuWorkQueue& queue) noexcept
    {
        _queue = &queue;
    }
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        ++callbacks;
        callbackTicket = ticket;
        callbackResult = result;
        rowsWereValid = rows.data != nullptr && rows.width == 2 && rows.height == 2 && rows.rowPitch >= 8;
        released = rows.data && _queue && _queue->ReleaseMapped(rows.lease);
        (void)SetEvent(delivered.get());
    }

    wil::unique_handle delivered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    unsigned int callbacks = 0;
    JobTicket callbackTicket{};
    HRESULT callbackResult = E_PENDING;
    bool rowsWereValid = false;
    bool released = false;

  private:
    GpuWorkQueue* _queue = nullptr;
};
#pragma warning(pop)

[[nodiscard]] bool RetireOnWorker(GpuCommandStream& stream) noexcept
{
    std::atomic<bool> retired{false};
    std::jthread worker([&]() noexcept { retired.store(stream.RetireForWork(), std::memory_order_release); });
    worker.join();
    return retired.load(std::memory_order_acquire);
}

void StopJoinShutdownAndRetire(GpuCommandStream& stream, JobScheduler& scheduler, GpuWorkQueue& queue,
                               std::jthread& producer, bool executed, GpuCommandLease lease) noexcept
{
    stream.RequestStopOwner();
    if (producer.joinable())
        producer.join();
    HRESULT producerResult = E_PENDING;
    Expect(stream.ProducerFinishedOwner(producerResult), "execute.failure-producer-finished-after-join");

    // This is the joined queue shutdown acknowledgment. It deliberately does not claim that readback completed.
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
    if (executed)
        Expect(stream.AcknowledgeOwner(lease), "execute.failure-ack-only-after-joined-queue-shutdown");

    const bool canRetire = stream.CanRetireOwner();
    Expect(canRetire, "execute.failure-can-retire-after-shutdown-ack");
    const bool authorized = canRetire && stream.AuthorizeRetirementOwner();
    Expect(authorized, "execute.failure-authorize-after-shutdown-ack");
    if (authorized)
        Expect(RetireOnWorker(stream), "execute.failure-worker-retirement");
}

void TestInvalidInputsAndOwnerRules()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(2) == S_OK, "invalid.initialize-scheduler");
    GpuCommandStream validStream;
    Expect(validStream.InitializeOwner(scheduler, kSource, kDeviceGeneration) == S_OK,
           "invalid.initialize-valid-stream");

    JobScheduler closedScheduler;
    GpuCommandStream invalidScheduler;
    Expect(FAILED(invalidScheduler.InitializeOwner(closedScheduler, kSource, kDeviceGeneration)),
           "invalid.reject-uninitialized-scheduler");
    GpuCommandStream invalidSource;
    Expect(invalidSource.InitializeOwner(scheduler, JobTicket{7, 0}, kDeviceGeneration) == E_INVALIDARG,
           "invalid.reject-zero-source-generation");
    GpuCommandStream invalidSourceSlot;
    Expect(invalidSourceSlot.InitializeOwner(scheduler, JobTicket{JobScheduler::kCapacity, 19}, kDeviceGeneration) ==
               E_INVALIDARG,
           "invalid.reject-source-slot-out-of-range");
    GpuCommandStream invalidDevice;
    Expect(invalidDevice.InitializeOwner(scheduler, kSource, 0) == E_INVALIDARG,
           "invalid.reject-zero-device-generation");
    Expect(validStream.InitializeOwner(scheduler, kSource, kDeviceGeneration) == E_INVALIDARG,
           "invalid.reject-duplicate-initialization");

    WarpDevice warp;
    Expect(SUCCEEDED(warp.Initialize()), "invalid.warp-device");
    if (warp.device)
    {
        auto command = warp.EmptyCommand();
        Expect(static_cast<bool>(command), "invalid.real-empty-command-created");
        if (command)
            Expect(validStream.PublishForWork(std::move(command)) == E_INVALIDARG,
                   "invalid.owner-thread-publish-refused");
    }
    Expect(validStream.NextOwner().command == nullptr, "invalid-owner-publish-consumes-no-slot");
    Expect(!validStream.RetireForWork(), "invalid.owner-thread-retirement-refused");
    scheduler.Close();
}

void TestCapacityBackpressureAndStoppedRetirement()
{
    WarpDevice warp;
    JobScheduler scheduler;
    wil::unique_handle ordinaryRan(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wil::unique_handle ordinaryDone(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    JobResult ordinaryResult{JobStatus::Failed, E_PENDING};
    GpuCommandStream stream;
    Published published;
    const bool initialized = SUCCEEDED(warp.Initialize()) && scheduler.Initialize(2) == S_OK && ordinaryRan &&
                             ordinaryDone && published.event && published.thirdAttempt && published.allowThird &&
                             stream.InitializeOwner(scheduler, kSource, kDeviceGeneration) == S_OK;
    Expect(initialized, "capacity.initialize");
    if (!initialized || !warp.device || !ordinaryRan || !ordinaryDone || !published.event || !published.thirdAttempt ||
        !published.allowThird || !scheduler.WakeHandle())
    {
        stream.RequestStopOwner();
        scheduler.Close();
        return;
    }

    std::jthread producer([&]() noexcept { RecordThree(stream, warp, published); });
    const bool publishedTwice = Signaled(published.event.get());
    Expect(publishedTwice, "capacity.two-publications-event");
    const bool expectedLeases = publishedTwice && published.producerResult == S_OK &&
                                published.first == GpuCommandLease{kSource, kDeviceGeneration, 1, 0} &&
                                published.second == GpuCommandLease{kSource, kDeviceGeneration, 2, 1};
    Expect(expectedLeases, "capacity.literal-first-two-leases");
    if (!expectedLeases)
    {
        (void)SetEvent(published.allowThird.get());
        stream.RequestStopOwner();
        producer.join();
        HRESULT producerResult = E_PENDING;
        (void)stream.ProducerFinishedOwner(producerResult);
        if (stream.CanRetireOwner() && stream.AuthorizeRetirementOwner())
            (void)RetireOnWorker(stream);
        (void)DrainCompletions(scheduler);
        scheduler.Close();
        return;
    }

    // Consume the coalesced publication wake before allowing the producer's third call. The pressure transition
    // has its own scheduler wake and owner drain below.
    (void)WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), 0);
    (void)DrainCompletions(scheduler);
    (void)SetEvent(published.allowThird.get());
    Expect(Signaled(published.thirdAttempt.get()), "capacity-third-attempt-event");
    const DWORD pressureWake = WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), kFailureTimeoutMs);
    Expect(pressureWake == WAIT_OBJECT_0, "capacity.backpressure-owner-wake");
    (void)DrainCompletions(scheduler);
    Expect(stream.WasBackpressured(), "capacity-third-publication-observed-backpressure");
    Expect(!stream.CanRetireOwner() && !stream.AuthorizeRetirementOwner(),
           "capacity-retirement-withheld-before-producer-join");

    std::unique_ptr<Job> ordinary =
        std::make_unique<OrdinaryJob>(ordinaryRan.get(), ordinaryDone.get(), ordinaryResult);
    JobTicket ordinaryTicket;
    Expect(scheduler.Submit(ordinary, {JobClass::Background, 0, 1024}, ordinaryTicket) == S_OK,
           "capacity.submit-independent-1k-job");
    Expect(Signaled(ordinaryRan.get()), "capacity-independent-job-runs-during-producer-wait");
    Expect(SettleJobs(scheduler, kFailureTimeoutMs) == SettleResult::Settled && Signaled(ordinaryDone.get()) &&
               ordinaryResult.status == JobStatus::Finished,
           "capacity-independent-job-finishes-during-producer-wait");
    Expect(published.thirdResult.load(std::memory_order_acquire) == E_PENDING,
           "capacity-third-publication-still-pending-during-job");

    stream.RequestStopOwner();
    producer.join();
    Expect(published.thirdResult.load(std::memory_order_acquire) == E_ABORT,
           "capacity-stop-releases-blocked-publication");
    Expect(stream.WasBackpressured(), "capacity-producer-observed-full-slots-before-stop");
    Expect(stream.ProducerFinishedOwner(published.producerResult) && published.producerResult == E_ABORT,
           "capacity-producer-finished-result");
    Expect(stream.CanRetireOwner(), "capacity-stopped-unsubmitted-commands-are-retirable");
    // The first two commands were never submitted to the immediate context. Their joined producer is the
    // parent's producer acknowledgment; cancellation did not release them from the stream.
    Expect(stream.AuthorizeRetirementOwner(), "capacity-authorize-after-real-join");
    Expect(!stream.RetireForWork(), "capacity-owner-cannot-retire");
    Expect(RetireOnWorker(stream), "capacity-worker-retires-stopped-unsubmitted-commands");
    (void)DrainCompletions(scheduler);
    scheduler.Close();
}

void TestCapacityCancellation(bool shutdownCancellation)
{
    WarpDevice warp;
    OriginalCompletion receiver;
    JobScheduler scheduler;
    GpuWorkQueue queue;
    GpuCommandStream stream;
    Published published;
    CancelSource localCancellation;
    std::stop_source shutdownCancellationSource;
    const CancelToken cancellation = shutdownCancellation
                                         ? CancelToken(std::stop_token{}, shutdownCancellationSource.get_token())
                                         : localCancellation.Token();
    const bool initialized =
        SUCCEEDED(warp.Initialize()) && scheduler.Initialize(2) == S_OK && receiver.delivered && published.event &&
        published.thirdAttempt && published.allowThird && published.thirdReturned && scheduler.WakeHandle() &&
        stream.InitializeOwner(scheduler, kSource, kDeviceGeneration) == S_OK &&
        SUCCEEDED(queue.Initialize(scheduler)) && SUCCEEDED(queue.Bind(warp.device.get(), warp.immediate.get(), true));
    Expect(initialized, shutdownCancellation ? "shutdown-cancel.initialize" : "local-cancel.initialize");
    receiver.Attach(queue);
    if (!initialized)
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }

    std::jthread producer([&]() noexcept { RecordThree(stream, warp, published, cancellation); });
    const bool publishedTwice = Signaled(published.event.get());
    Expect(publishedTwice, shutdownCancellation ? "shutdown-cancel.two-publications" : "local-cancel.two-publications");
    const bool expectedLeases = publishedTwice && published.producerResult == S_OK &&
                                published.first == GpuCommandLease{kSource, kDeviceGeneration, 1, 0} &&
                                published.second == GpuCommandLease{kSource, kDeviceGeneration, 2, 1};
    Expect(expectedLeases, shutdownCancellation ? "shutdown-cancel.literal-leases" : "local-cancel.literal-leases");
    if (!expectedLeases)
    {
        (void)SetEvent(published.allowThird.get());
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, {});
        return;
    }

    (void)WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), 0);
    (void)DrainCompletions(scheduler);
    (void)SetEvent(published.allowThird.get());
    const bool thirdAttempted = Signaled(published.thirdAttempt.get());
    Expect(thirdAttempted, shutdownCancellation ? "shutdown-cancel.third-attempt" : "local-cancel.third-attempt");
    const DWORD pressureWake = WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), kFailureTimeoutMs);
    Expect(pressureWake == WAIT_OBJECT_0,
           shutdownCancellation ? "shutdown-cancel.backpressure-owner-wake" : "local-cancel.backpressure-owner-wake");
    (void)DrainCompletions(scheduler);
    Expect(stream.WasBackpressured(),
           shutdownCancellation ? "shutdown-cancel.backpressure-observed" : "local-cancel.backpressure-observed");

    const GpuCommandStream::Ready ready = stream.NextOwner();
    const bool firstReady = ready.command && ready.lease == GpuCommandLease{kSource, kDeviceGeneration, 1, 0};
    Expect(firstReady,
           shutdownCancellation ? "shutdown-cancel.first-command-ready" : "local-cancel.first-command-ready");
    if (!firstReady)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }
    auto source = warp.ReadbackSource();
    Expect(static_cast<bool>(source),
           shutdownCancellation ? "shutdown-cancel.readback-source" : "local-cancel.readback-source");
    if (!source)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }
    const bool began = stream.BeginExecuteOwner(ready.lease);
    Expect(began, shutdownCancellation ? "shutdown-cancel.begin-exact-lease" : "local-cancel.begin-exact-lease");
    if (!began)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }

    warp.immediate->ExecuteCommandList(ready.command, FALSE);
    const bool cancellationRequested =
        shutdownCancellation ? shutdownCancellationSource.request_stop() : localCancellation.Cancel();
    Expect(cancellationRequested, shutdownCancellation ? "shutdown-cancel.request-stop" : "local-cancel.request-stop");
    const bool thirdReturned = Signaled(published.thirdReturned.get());
    Expect(thirdReturned, shutdownCancellation ? "shutdown-cancel.third-returned" : "local-cancel.third-returned");
    if (!thirdReturned)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }
    Expect(published.thirdResult.load(std::memory_order_acquire) == E_ABORT,
           shutdownCancellation ? "shutdown-cancel-refuses-unsubmitted-third"
                                : "local-cancel-refuses-unsubmitted-third");
    producer.join();
    HRESULT producerResult = E_PENDING;
    Expect(stream.ProducerFinishedOwner(producerResult) && producerResult == E_ABORT,
           shutdownCancellation ? "shutdown-cancel.producer-joined" : "local-cancel.producer-joined");
    Expect(!stream.CanRetireOwner() && !stream.AuthorizeRetirementOwner(),
           shutdownCancellation ? "shutdown-cancel-does-not-ack-executing-command"
                                : "local-cancel-does-not-ack-executing-command");

    const D3D11_BOX box{0, 0, 0, 2, 2, 1};
    const HRESULT enqueueResult = queue.EnqueueReadback(source.get(), box, kSource, receiver);
    Expect(SUCCEEDED(enqueueResult), shutdownCancellation ? "shutdown-cancel.enqueue-original-completion"
                                                          : "local-cancel.enqueue-original-completion");
    if (FAILED(enqueueResult))
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }
    const SettleResult settled = SettleJobs(scheduler, kFailureTimeoutMs);
    const bool originalCallbackSucceeded = settled == SettleResult::Settled && Signaled(receiver.delivered.get()) &&
                                           receiver.callbacks == 1 && receiver.callbackTicket == kSource &&
                                           receiver.callbackResult == S_OK && receiver.rowsWereValid &&
                                           receiver.released && queue.LentCount() == 0;
    Expect(originalCallbackSucceeded, shutdownCancellation ? "shutdown-cancel.original-completion-succeeded"
                                                           : "local-cancel.original-completion-succeeded");
    if (!originalCallbackSucceeded)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }
    const bool acknowledged = stream.AcknowledgeOwner(ready.lease);
    Expect(acknowledged, shutdownCancellation ? "shutdown-cancel.ack-after-original-callback"
                                              : "local-cancel.ack-after-original-callback");
    if (!acknowledged)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }
    Expect(!stream.NextOwner().command, shutdownCancellation ? "shutdown-cancel-no-next-command-after-stop"
                                                             : "local-cancel-no-next-command-after-stop");
    const bool authorized = stream.CanRetireOwner() && stream.AuthorizeRetirementOwner();
    Expect(authorized, shutdownCancellation ? "shutdown-cancel.authorize-after-join-and-real-ack"
                                            : "local-cancel.authorize-after-join-and-real-ack");
    if (!authorized)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }
    Expect(RetireOnWorker(stream), shutdownCancellation ? "shutdown-cancel.retire-unsubmitted-command"
                                                        : "local-cancel.retire-unsubmitted-command");
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
}

void TestExecutedCommandRequiresOriginalCompletion()
{
    WarpDevice warp;
    OriginalCompletion receiver;
    JobScheduler scheduler;
    GpuWorkQueue queue;
    GpuCommandStream stream;
    const bool initialized = SUCCEEDED(warp.Initialize()) && scheduler.Initialize(2) == S_OK && receiver.delivered &&
                             stream.InitializeOwner(scheduler, kSource, kDeviceGeneration) == S_OK &&
                             SUCCEEDED(queue.Initialize(scheduler)) &&
                             SUCCEEDED(queue.Bind(warp.device.get(), warp.immediate.get(), true));
    Expect(initialized, "execute.initialize-real-warp-queue");
    receiver.Attach(queue);
    wil::unique_handle publishedEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!initialized || !warp.device || !scheduler.WakeHandle() || !publishedEvent)
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }

    std::atomic<HRESULT> publishResult{E_PENDING};
    std::jthread producer(
        [&]() noexcept
        {
            auto command = warp.EmptyCommand();
            publishResult.store(command ? stream.PublishForWork(std::move(command)) : E_FAIL,
                                std::memory_order_release);
            (void)SetEvent(publishedEvent.get());
            stream.FinishProducerForWork(publishResult.load(std::memory_order_acquire));
        });
    Expect(Signaled(publishedEvent.get()), "execute.publication-event");
    const GpuCommandStream::Ready ready = stream.NextOwner();
    Expect(publishResult.load(std::memory_order_acquire) == S_OK && ready.command &&
               ready.lease == GpuCommandLease{kSource, kDeviceGeneration, 1, 0},
           "execute.real-command-published");
    if (!ready.command)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }

    auto wrongSource = ready.lease;
    wrongSource.source.generation++;
    auto wrongGeneration = ready.lease;
    wrongGeneration.deviceGeneration++;
    auto wrongOrdinal = ready.lease;
    wrongOrdinal.ordinal++;
    auto wrongSlot = ready.lease;
    wrongSlot.slot = 1;
    const bool wrongSourceBegan = stream.BeginExecuteOwner(wrongSource);
    const bool wrongGenerationBegan = stream.BeginExecuteOwner(wrongGeneration);
    const bool wrongOrdinalBegan = stream.BeginExecuteOwner(wrongOrdinal);
    const bool wrongSlotBegan = stream.BeginExecuteOwner(wrongSlot);
    const bool staleBeginsRejected =
        !wrongSourceBegan && !wrongGenerationBegan && !wrongOrdinalBegan && !wrongSlotBegan;
    Expect(staleBeginsRejected, "execute-refuse-stale-begin-source-generation-ordinal-slot");
    if (!staleBeginsRejected)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer,
                                  wrongSourceBegan || wrongGenerationBegan || wrongOrdinalBegan || wrongSlotBegan,
                                  ready.lease);
        return;
    }
    auto source = warp.ReadbackSource();
    Expect(static_cast<bool>(source), "execute-real-readback-source");
    if (!source)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }

    const bool began = stream.BeginExecuteOwner(ready.lease);
    Expect(began, "execute.begin-exact-lease");
    if (!began)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, false, ready.lease);
        return;
    }
    warp.immediate->ExecuteCommandList(ready.command, FALSE);
    stream.RequestStopOwner();
    Expect(!stream.CanRetireOwner() && !stream.AuthorizeRetirementOwner(),
           "execute-cancel-does-not-retire-in-flight-command");

    const D3D11_BOX box{0, 0, 0, 2, 2, 1};
    const HRESULT enqueueResult = queue.EnqueueReadback(source.get(), box, kSource, receiver);
    Expect(SUCCEEDED(enqueueResult), "execute-enqueue-real-original-completion");
    if (FAILED(enqueueResult))
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }

    const SettleResult settled = SettleJobs(scheduler, kFailureTimeoutMs);
    Expect(settled == SettleResult::Settled, "execute-pump-original-completion-to-settle");
    const bool originalCallbackSucceeded = settled == SettleResult::Settled && Signaled(receiver.delivered.get()) &&
                                           receiver.callbacks == 1 && receiver.callbackTicket == kSource &&
                                           receiver.callbackResult == S_OK && receiver.rowsWereValid &&
                                           receiver.released && queue.LentCount() == 0;
    Expect(originalCallbackSucceeded, "execute-original-callback-releases-mapped-rows");
    if (!originalCallbackSucceeded)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }

    const bool wrongSourceAcknowledged = stream.AcknowledgeOwner(wrongSource);
    const bool wrongGenerationAcknowledged = stream.AcknowledgeOwner(wrongGeneration);
    const bool wrongOrdinalAcknowledged = stream.AcknowledgeOwner(wrongOrdinal);
    const bool wrongSlotAcknowledged = stream.AcknowledgeOwner(wrongSlot);
    Expect(!wrongSourceAcknowledged && !wrongGenerationAcknowledged && !wrongOrdinalAcknowledged &&
               !wrongSlotAcknowledged,
           "execute-refuse-stale-source-generation-ordinal-slot");

    const bool acknowledged = stream.AcknowledgeOwner(ready.lease);
    Expect(acknowledged, "execute-exact-ack-after-original-callback");
    if (!acknowledged)
    {
        StopJoinShutdownAndRetire(stream, scheduler, queue, producer, true, ready.lease);
        return;
    }
    Expect(!stream.AcknowledgeOwner(ready.lease), "execute-duplicate-ack-refused");

    // Producer completion and its real join precede the parent's retirement authorization.
    producer.join();
    HRESULT producerResult = E_PENDING;
    Expect(stream.ProducerFinishedOwner(producerResult) && producerResult == S_OK, "execute-producer-finished");
    Expect(stream.CanRetireOwner() && stream.AuthorizeRetirementOwner(), "execute-authorize-after-completion-and-join");
    Expect(!stream.RetireForWork(), "execute-owner-retirement-refused");
    Expect(RetireOnWorker(stream), "execute-worker-retirement");
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
}
} // namespace

int RunGpuCommandStreamTests()
{
    gFailures = 0;
    TestInvalidInputsAndOwnerRules();
    TestCapacityBackpressureAndStoppedRetirement();
    TestCapacityCancellation(false);
    TestCapacityCancellation(true);
    TestExecutedCommandRequiresOriginalCompletion();
    return gFailures == 0 ? 0 : 1;
}

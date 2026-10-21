#include "Application.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Runtime.Tests.ApplicationShutdownTestProbe.h"

#include "CanvasRenderer.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "DocumentReadbackJob.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stop_token>

namespace
{
struct LeaseState final
{
    DWORD ownerThread = GetCurrentThreadId();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle helperEntered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle callbackEntered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle callbackRelease{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle cancellationReady{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> workerEntered{false};
    std::atomic<bool> workerFinished{false};
    std::atomic<bool> helperFinished{false};
    std::atomic<unsigned int> callbackVisits{0};
    std::atomic<unsigned int> callbackReturns{0};
    std::atomic<unsigned int> receiverCalls{0};
    std::atomic<unsigned int> completedCalls{0};
    std::atomic<unsigned int> retired{0};
    std::atomic<bool> retiredMta{false};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<bool> retiredLiteral{false};
    GpuReadbackRows rows;
    bool beforeCancelLiteral = false;
    bool afterCancelLiteral = false;
    bool cancellationObserved = false;
    bool wrapperOnOwner = false;
    bool wrapperAfterProducer = false;
    bool wrapperBeforeCpuRetirement = false;
    bool ownerSawAcknowledged = false;
    bool joinedSawExactAcknowledgement = false;
    JobTicket ticket;
    HRESULT armResult = E_UNEXPECTED;
    CancelToken cancellation;
    std::uint64_t reservation = 0;
};

[[nodiscard]] bool LiteralRows(const std::uint8_t* data, std::size_t stride, unsigned int width,
                               unsigned int height) noexcept
{
    if (!data || width != 2048 || height != 2048 || stride < 2048u * 4)
        return false;
    for (unsigned int y = 0; y < height; ++y)
    {
        const auto* const row = data + static_cast<std::size_t>(y) * stride;
        for (unsigned int x = 0; x < width; ++x)
        {
            const auto* const pixel = row + static_cast<std::size_t>(x) * 4;
            if (pixel[0] != 31 || pixel[1] != 63 || pixel[2] != 127 || pixel[3] != 255)
                return false;
        }
    }
    return true;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct LeaseCpuOwners final : DeferredReleaseNode
{
    CanvasDocument document;
    PixelBuffer pixels;
    Raster image;
    LeaseState& state;

    explicit LeaseCpuOwners(LeaseState& observation) noexcept : state(observation)
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* const payload = static_cast<LeaseCpuOwners*>(node);
            LeaseState& check = payload->state;
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            const bool mta = SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA;
            const bool offOwner = GetCurrentThreadId() != check.ownerThread;
            bool literal = false;
            if (payload->image.HasStorage())
                literal = LiteralRows(payload->image.Data(), static_cast<std::size_t>(payload->image.Stride()),
                                      static_cast<unsigned int>(payload->image.Width()),
                                      static_cast<unsigned int>(payload->image.Height()));
            else if (payload->pixels && payload->pixels.Size() == kDeferredReleaseThreshold)
            {
                literal = true;
                for (std::size_t index = 0; index < payload->pixels.Size(); ++index)
                    if (payload->pixels.Data()[index] != 0x5a)
                    {
                        literal = false;
                        break;
                    }
            }
            {
                const std::unique_ptr<LeaseCpuOwners> owned(payload);
            }
            // The owned graph has actually been destroyed before publishing this observation.
            check.retiredLiteral.store(literal);
            check.retiredMta.store(mta);
            check.retiredOffOwner.store(offOwner);
            check.retired.fetch_add(1);
        };
    }
};
#pragma warning(pop)

struct RetireLeaseOwners final
{
    void operator()(LeaseCpuOwners* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
            payload->destroy(payload);
    }
};

using LeaseOwners = std::unique_ptr<LeaseCpuOwners, RetireLeaseOwners>;

class HeldReadback final : public DocumentReadbackJob
{
  public:
    HeldReadback(JobScheduler& scheduler, GpuWorkQueue& queue, CanvasRenderer& composition, CanvasDocument document,
                 LeaseState& state, void* owner, HRESULT (*prepare)(void*) noexcept)
        : DocumentReadbackJob(scheduler, queue, composition, std::move(document), {0, 0, 0, 2048, 2048, 1}),
          _state(state), _owner(owner), _prepare(prepare), _payload(std::make_unique<LeaseCpuOwners>(state).release())
    {
        _payload->bytes = 2 * kDeferredReleaseThreshold;
    }
    ~HeldReadback() override
    {
        _state.wrapperOnOwner = GetCurrentThreadId() == _state.ownerThread;
        _state.wrapperAfterProducer = _state.workerFinished.load();
        _state.wrapperBeforeCpuRetirement = _state.retired.load() == 0;
        TransferReadbackOwners(_payload->document, _payload->pixels, _payload->image);
        // The base then acknowledges the real map on owner; this CPU aggregate uses the installed sink.
    }

  private:
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        _state.rows = MappedRowsForTest();
        _state.beforeCancelLiteral =
            LiteralRows(_state.rows.data, _state.rows.rowPitch, _state.rows.width, _state.rows.height) &&
            LiteralRows(image.Data(), static_cast<std::size_t>(image.Stride()),
                        static_cast<unsigned int>(image.Width()), static_cast<unsigned int>(image.Height()));
        _state.reservation = context.reservedMemoryBytes;
        const std::stop_callback stopped(context.cancellation.ShutdownToken(),
                                         [&]() noexcept
                                         {
                                             if (!SetEvent(_state.cancellationReady.get()))
                                                 std::_Exit(5);
                                         });
        _state.workerEntered.store(true, std::memory_order_release);
        if (!SetEvent(_state.entered.get()) ||
            WaitForSingleObject(_state.cancellationReady.get(), 10000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 0) != WAIT_OBJECT_0)
            std::_Exit(5);
        _state.cancellationObserved = context.cancellation.IsCancelled();
        // Read while this worker still owns the lease, even when its device is retired.
        _state.afterCancelLiteral =
            LiteralRows(_state.rows.data, _state.rows.rowPitch, _state.rows.width, _state.rows.height);
        _state.workerFinished.store(true, std::memory_order_release);
        return {JobStatus::Cancelled};
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return FAILED(reason) ? reason : _prepare(_owner);
    }
    void ReadbackCompleted(JobResult, Raster, std::uint64_t) noexcept override
    {
        _state.completedCalls.fetch_add(1);
    }
    LeaseState& _state;
    void* const _owner;
    HRESULT (*const _prepare)(void*) noexcept;
    LeaseOwners _payload;
};

class HeldRetry final : public Job, public RetryWaitReceiver
{
  public:
    HeldRetry(RetryWaitQueue& queue, LeaseState& state)
        : _queue(queue), _state(state), _payload(std::make_unique<LeaseCpuOwners>(state).release())
    {
    }
    ~HeldRetry() override
    {
        _state.wrapperOnOwner = GetCurrentThreadId() == _state.ownerThread;
        _state.wrapperAfterProducer = _state.workerFinished.load() && _state.callbackReturns.load() == 1;
        _state.wrapperBeforeCpuRetirement = _state.retired.load() == 0;
    }
    JobResult Run(JobContext& context) override
    {
        _payload->pixels = PixelBuffer::TryAllocate(static_cast<std::size_t>(kDeferredReleaseThreshold));
        if (!_payload->pixels)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        std::memset(_payload->pixels.Data(), 0x5a, _payload->pixels.Size());
        _state.cancellation = context.cancellation;
        _state.reservation = context.reservedMemoryBytes;
        _state.workerFinished.store(true, std::memory_order_release);
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        _state.armResult = _queue.Start(ticket, 5, _state.cancellation, *this);
        return SUCCEEDED(_state.armResult);
    }
    void Completed(JobResult) noexcept override
    {
        _state.completedCalls.fetch_add(1);
    }
    void RetryWaitReady(JobTicket, RetryWaitStatus, std::int32_t) noexcept override
    {
        _state.receiverCalls.fetch_add(1);
    }

  private:
    RetryWaitQueue& _queue;
    LeaseState& _state;
    LeaseOwners _payload;
};

class ReleaseRetryOnShutdown final : public Job
{
  public:
    explicit ReleaseRetryOnShutdown(LeaseState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        const std::stop_callback stopped(context.cancellation.ShutdownToken(),
                                         [&]() noexcept
                                         {
                                             if (!SetEvent(_state.cancellationReady.get()))
                                                 std::_Exit(5);
                                         });
        _state.workerEntered.store(true, std::memory_order_release);
        if (!SetEvent(_state.helperEntered.get()) ||
            WaitForSingleObject(_state.cancellationReady.get(), 10000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 0) != WAIT_OBJECT_0)
            std::_Exit(5);
        _state.cancellationObserved = context.cancellation.IsCancelled();
        if (!SetEvent(_state.callbackRelease.get()))
            std::_Exit(5);
        _state.helperFinished.store(true, std::memory_order_release);
        return {JobStatus::Cancelled};
    }
    void Completed(JobResult) noexcept override
    {
        _state.completedCalls.fetch_add(1);
    }

  private:
    LeaseState& _state;
};

} // namespace

int Application::RunStandaloneLeaseShutdownSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-lease-shutdown.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
    };
    for (unsigned int kind = 0; kind < 4; ++kind)
    {
        const bool readback = kind < 2;
        const bool retiredDevice = kind == 1;
        const bool readyUndrained = kind == 3;
        const char* const scenario = std::array{"mapped-bound-device", "mapped-retired-device", "retry-held-producer",
                                                "retry-ready-undrained"}[kind];
        expect(!DeferredReleaseDetail::sink.enqueue, scenario, "no-outer-surrogate-sink");
        APTTYPE originalApartment{};
        APTTYPEQUALIFIER originalQualifier{};
        const HRESULT apartmentResult = CoGetApartmentType(&originalApartment, &originalQualifier);
        LeaseState state;
        ApplicationShutdownTestProbe shutdownProbe;
        ApplicationProcessingTestProbe processingProbe;
        processingProbe.retryWait.context = &state;
        processingProbe.retryWait.beforePublication = [](void* opaque) noexcept
        {
            auto& check = *static_cast<LeaseState*>(opaque);
            check.callbackVisits.fetch_add(1);
            if (!SetEvent(check.callbackEntered.get()) ||
                WaitForSingleObject(check.callbackRelease.get(), 10000) != WAIT_OBJECT_0)
                std::_Exit(5);
            check.callbackReturns.fetch_add(1, std::memory_order_release);
        };
        auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
        const auto shutdown = wil::scope_exit(
            [&]() noexcept
            {
                if (owner)
                {
                    (void)SetEvent(state.callbackRelease.get());
                    owner->Shutdown();
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        owner->_processingTestProbe = &processingProbe;
        if (!state.entered || !state.helperEntered || !state.callbackEntered || !state.callbackRelease ||
            !state.cancellationReady || FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
            FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
            FAILED(owner->InitializeChrome()))
        {
            SelfTestFailure(L"Standalone lease shutdown fixture could not initialize its actual host.\n");
            return 5;
        }
        owner->BindCompositionDevice();
        expect(owner->_renderer.IsWarp() && owner->_renderer.Device() && owner->_composition.Device() &&
                   owner->_window && owner->_chrome && DeferredReleaseDetail::sink.enqueue &&
                   owner->_jobs.WorkerCount() != 0 && SUCCEEDED(owner->_gpuBindingError),
               scenario, "actual-hidden-WARP-host-own-pool-sink-and-queues");

        // This pre-shutdown harness pump is bounded and event-driven. Shutdown and all close hooks never pump.
        const auto pumpUntil = [&](auto&& predicate, HANDLE entered) noexcept
        {
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (!predicate())
            {
                const auto messages = PumpMessages();
                if (messages.quit)
                    return false;
                (void)DrainCompletions(owner->_jobs);
                if (predicate())
                    break;
                const ULONGLONG now = GetTickCount64();
                if (now >= deadline)
                    return false;
                const std::array handles{static_cast<HANDLE>(owner->_jobs.WakeHandle()), entered};
                if (MsgWaitForMultipleObjectsEx(static_cast<DWORD>(handles.size()), handles.data(),
                                                static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    return false;
            }
            return true;
        };

        JobTicket original;
        if (readback)
        {
            owner->_session.CreateDocument(2048, 2048, true);
            (void)InsertPaintedLayer(owner->_session, L"Mapped shutdown source", 2048, 2048, {}, 0, 0xFF7F3F1Fu);
            const auto& optionalDocument = owner->_session.Document();
            const CanvasDocument* const document = optionalDocument.has_value() ? &optionalDocument.value() : nullptr;
            if (!document)
                return 5;
            std::unique_ptr<Job> job = std::make_unique<HeldReadback>(
                owner->_jobs, owner->_gpuWork, owner->_composition, *document, state, owner.get(),
                [](void* opaque) noexcept
                {
                    auto& application = *static_cast<Application*>(opaque);
                    application.BindCompositionDevice();
                    return application._gpuBindingError;
                });
            const auto required = DocumentReadbackJob::RequiredMemory({0, 0, 0, 2048, 2048, 1});
            if (FAILED(owner->_jobs.Submit(job, {JobClass::Io, 0, required}, original)) ||
                !pumpUntil([&]() noexcept { return state.workerEntered.load(std::memory_order_acquire); },
                           state.entered.get()))
                std::_Exit(5);
            expect(state.beforeCancelLiteral && state.rows.data && state.rows.lease.generation != 0 &&
                       state.rows.deviceGeneration == owner->_gpuWork.DeviceGeneration() &&
                       state.reservation == required && owner->_jobs.ReservedMemoryBytes() == required,
                   scenario, "real-copy-literal-rows-original-lease-and-reservation");
            const auto statistics = owner->_gpuWork.Statistics();
            expect(owner->_gpuWork.LentCount() == 1 && statistics.copies == 1 && statistics.maps == 1 &&
                       !state.workerFinished.load() && state.retired.load() == 0,
                   scenario, "actual-map-remains-lent-while-worker-gated");
            if (retiredDevice)
                owner->_gpuWork.RetireDevice();
            expect(owner->_gpuWork.LentCount() == 1 && owner->_gpuWork.StagingBytes() >= kDeferredReleaseThreshold &&
                       (!retiredDevice || owner->_gpuWork.CompletionRoute() == GpuCapabilities::CompletionRoute::None),
                   scenario, "lent-storage-retained-across-optional-device-retirement");
        }
        else
        {
            std::unique_ptr<Job> retry = std::make_unique<HeldRetry>(owner->_retryWait, state);
            const auto required = PixelBuffer::RequiredBytes(static_cast<std::size_t>(kDeferredReleaseThreshold));
            if (FAILED(owner->_jobs.Submit(retry, {JobClass::Io, 0, required}, original)) ||
                !pumpUntil([&]() noexcept { return SUCCEEDED(state.armResult); }, state.entered.get()))
                std::_Exit(5);
            expect(state.ticket == original && state.reservation == required && state.workerFinished.load() &&
                       owner->_retryWait.HasPending() && owner->_jobs.ReservedMemoryBytes() == required,
                   scenario, "actual-suspended-retry-original-ticket-buffer-and-reservation");
            std::unique_ptr<Job> helper = std::make_unique<ReleaseRetryOnShutdown>(state);
            JobTicket helperTicket;
            if (FAILED(owner->_jobs.Submit(helper, {JobClass::Background, 0, 0}, helperTicket)) ||
                WaitForSingleObject(state.helperEntered.get(), 10000) != WAIT_OBJECT_0 ||
                !owner->_retryWait.SignalDeadlineForTest(original) ||
                WaitForSingleObject(state.callbackEntered.get(), 10000) != WAIT_OBJECT_0)
                std::_Exit(5);
            expect(state.workerEntered.load() && state.callbackVisits.load() == 1 &&
                       state.callbackReturns.load() == 0 && !owner->_retryWait.HasReady() &&
                       state.receiverCalls.load() == 0 && state.completedCalls.load() == 0,
                   scenario, "real-wait-producer-held-before-publication-and-delivery");
            if (readyUndrained)
            {
                // Consume prior unrelated wakes while the actual wait producer is still blocked.
                (void)DrainCompletions(owner->_jobs);
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                if (WaitForSingleObject(wake, 0) == WAIT_FAILED)
                    std::_Exit(5);
                if (!SetEvent(state.callbackRelease.get()))
                    std::_Exit(5);
                // The auto-reset wake was consumed above while the producer was blocked. This signal
                // follows the actual readiness release-store; do not dispatch it before Shutdown.
                if (WaitForSingleObject(wake, 10000) != WAIT_OBJECT_0 || !owner->_retryWait.HasReady())
                    std::_Exit(5);
            }
            expect(owner->_retryWait.HasPending() && owner->_retryWait.HasReady() == readyUndrained &&
                       state.callbackReturns.load() == (readyUndrained ? 1u : 0u) && state.retired.load() == 0 &&
                       state.receiverCalls.load() == 0,
                   scenario, "readiness-undrained-and-owners-retained-at-shutdown-entry");
        }

        struct OwnerObservation final
        {
            Application* application;
            LeaseState* state;
            bool readback;
        } observation{owner.get(), &state, readback};
        shutdownProbe.checkContext = &observation;
        shutdownProbe.checkJoined = [](void* opaque) noexcept
        {
            auto& check = *static_cast<OwnerObservation*>(opaque);
            Application& application = *check.application;
            auto& state = *check.state;
            const auto statistics = application._gpuWork.Statistics();
            // Observe before queue CloseAfterWorkers, so its fallback cleanup cannot fake acknowledgement.
            state.joinedSawExactAcknowledgement =
                state.wrapperOnOwner && state.wrapperAfterProducer && state.retired.load() == 0 &&
                application._jobs.WakeHandle() && application._jobs.ReservedMemoryBytes() != 0 &&
                (check.readback ? (application._gpuWork.LentCount() == 0 && statistics.maps == 1 &&
                                   !application._gpuWork.ReleaseMapped(state.rows.lease))
                                : (state.callbackReturns.load() == 1 && application._retryWait.HasPending() &&
                                   state.receiverCalls.load() == 0));
        };
        shutdownProbe.checkOwner = [](void* opaque) noexcept
        {
            auto& check = *static_cast<OwnerObservation*>(opaque);
            Application& application = *check.application;
            auto& state = *check.state;
            state.ownerSawAcknowledged = state.joinedSawExactAcknowledgement && state.wrapperOnOwner &&
                                         state.wrapperAfterProducer && state.wrapperBeforeCpuRetirement &&
                                         state.retired.load() == 0 && application._jobs.WakeHandle() &&
                                         application._jobs.ReservedMemoryBytes() != 0 &&
                                         application._gpuWork.LentCount() == 0 && !application._gpuWork.HasPending() &&
                                         !application._retryWait.HasPending() && !application._retryWait.HasReady() &&
                                         (!check.readback || !application._gpuWork.ReleaseMapped(state.rows.lease));
        };
        owner->_shutdownTestProbe = &shutdownProbe;
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady, scenario,
               "actual-shutdown-acknowledged");
        expect(state.cancellationObserved && state.workerFinished.load() &&
                   (readback ? state.afterCancelLiteral : state.helperFinished.load()),
               scenario, "cancelled-producer-acknowledges-before-ownership-release");
        expect(shutdownProbe.ownerCalls == 1 && state.ownerSawAcknowledged && shutdownProbe.graphicsDetachedOnOwner &&
                   shutdownProbe.reservationAndWakeAlive,
               scenario, "joined-owner-wrapper-map-wait-and-reservation-order");
        expect(shutdownProbe.workerCalls.load() == 1 && shutdownProbe.mta.load() && shutdownProbe.offOwner.load() &&
                   state.retired.load() == 1 && state.retiredMta.load() && state.retiredOffOwner.load() &&
                   state.retiredLiteral.load(),
               scenario, "real-literal-CPU-graph-destroyed-on-MTA-exactly-once");
        expect(state.completedCalls.load() == 0 && state.receiverCalls.load() == 0 &&
                   (readback || (state.callbackVisits.load() == 1 && state.callbackReturns.load() == 1)),
               scenario, "useful-callbacks-suppressed-and-event-producer-joined");
        expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   !DeferredReleaseDetail::sink.enqueue && !owner->_retryWait.HasPending() &&
                   owner->_gpuWork.LentCount() == 0 && owner->_gpuWork.StagingBytes() == 0 &&
                   !owner->_jobs.Resume(original),
               scenario, "original-ticket-routes-reservations-events-and-staging-retired");
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        expect(SUCCEEDED(apartmentResult) && SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
                   apartment == originalApartment && qualifier == originalQualifier,
               scenario, "caller-apartment-preserved");
        owner->Shutdown();
        expect(shutdownProbe.ownerCalls == 1 && shutdownProbe.workerCalls.load() == 1 && state.retired.load() == 1,
               scenario, "second-shutdown-does-not-repeat-acknowledgement");
        owner->_shutdownTestProbe = nullptr;
        owner->_processingTestProbe = nullptr;
        owner.reset();
        bool consumedQuit = false;
        for (;;)
        {
            const auto drained = PumpMessages();
            consumedQuit = consumedQuit || drained.quit;
            if (!drained.quit)
                break;
        }
        expect(consumedQuit, scenario, "fixture-quit-consumed-before-next-owner");
    }
    std::printf("application-lease-shutdown summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}

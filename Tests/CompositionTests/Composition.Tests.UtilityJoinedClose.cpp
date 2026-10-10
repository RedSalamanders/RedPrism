#include "Composition.Tests.UtilityJoinedClose.h"

#include "DeferredRelease.h"
#include "OwnerWorkMultiplexer.h"
#include "PixelBuffer.h"
#include "SharedPublicationMemory.h"
#include "UtilityReadbackOwnerService.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace RedPrism::CompositionTests
{
namespace
{
constexpr std::uint64_t kCpuCharge = 1024 * 1024;
constexpr std::uint64_t kGpuCharge = 65536;
constexpr DWORD kTimeoutMs = 5000;
unsigned failures = 0;

void Expect(bool value, const char* name) noexcept
{
    std::printf("%s utility-joined-close.%s\n", value ? "PASS" : "FAIL", name);
    failures += value ? 0u : 1u;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
}

struct Observation final
{
    const DWORD ownerThread = GetCurrentThreadId();
    wil::unique_event_nothrow entered;
    wil::unique_event_nothrow release;
    JobTicket original;
    std::weak_ptr<SharedPublicationMemory> weak;
    bool attached = false;
    bool unboundChargeRefused = false;
    bool joined = false;
    bool boundBeforeTransfer = false;
    bool wrongOriginalRefused = false;
    bool transferred = false;
    bool detached = false;
    bool deferred = false;
    bool ownerHook = false;
    bool chargeHeldInOwnerHook = false;
    bool releaseRefusedInOwnerHook = false;
    bool closeRefusedInOwnerHook = false;
    bool workerHook = false;
    bool heldBytesIntact = false;  // Worker only until release acknowledgement/Close returns.
    bool heldChargeIntact = false; // Joined witness writes, then owner reads after thread::join.
    bool heldEventObserved = false;
    bool retiredOffOwner = false;
    bool retired = false;
    unsigned completed = 0;
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct Payload final : DeferredReleaseNode
{
    explicit Payload(Observation& value) noexcept : observation(value)
    {
        bytes = kCpuCharge;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* payload = static_cast<Payload*>(node);
            auto& observation = payload->observation;
            observation.retiredOffOwner = GetCurrentThreadId() != observation.ownerThread;
            observation.heldBytesIntact =
                payload->pixels.Size() == 65536 &&
                std::all_of(payload->pixels.Data(), payload->pixels.Data() + payload->pixels.Size(),
                            [](std::uint8_t value) noexcept { return value == 17; });
            if (!SetEvent(observation.entered.get()) ||
                WaitForSingleObject(observation.release.get(), kTimeoutMs) != WAIT_OBJECT_0)
                std::terminate(); // Failure bound, never a fabricated retirement acknowledgement.
            delete payload;
            observation.retired = true;
        };
    }
    Observation& observation;
    std::shared_ptr<SharedPublicationMemory> memory;
    PixelBuffer pixels;
};
#pragma warning(pop)

class Parent final : public Job, public UtilityReadbackOwner
{
  public:
    Parent(JobScheduler& jobs, UtilityReadbackOwnerService& service, Observation& observation,
           GpuResidencyReservation charge) noexcept
        : _jobs(jobs), _service(service), _observation(observation), _charge(std::move(charge))
    {
    }
    ~Parent() override
    {
        if (!_payload)
            return;
        if (!_observation.joined || _charge)
            std::terminate();
        _pending = false; // The scheduler joined every useful producer; only final payload retirement remains.
        _observation.detached = _service.DetachOwner(_observation.original, *this);
        if (!_observation.detached)
            std::terminate();
        _observation.deferred = TryDeferRelease(*_payload, DeferredReleasePolicy::AlwaysOnOwner);
        if (!_observation.deferred)
            std::terminate();
        _payload = nullptr;
    }
    JobResult Run(JobContext& context) override
    {
        if (context.reservedMemoryBytes != kCpuCharge)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto payload = std::make_unique<Payload>(_observation);
        payload->memory = SharedPublicationMemory::CreateForWork();
        payload->pixels = PixelBuffer::TryAllocate(65536);
        if (!payload->pixels)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        std::fill_n(payload->pixels.Data(), payload->pixels.Size(), static_cast<std::uint8_t>(17));
        _payload = payload.release();
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _observation.original = ticket;
        _observation.weak = _payload->memory;
        _pending = true;
        _observation.attached = _service.AttachOwner(ticket, *this);
        _observation.unboundChargeRefused =
            !_service.RetainJoinedChargeOwner(ticket, *this, _charge, *_payload->memory) &&
            _charge.Bytes() == kGpuCharge;
        return _observation.attached;
    }
    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket original) noexcept override
    {
        if (&jobs != &_jobs || original != _observation.original || !_payload)
            std::terminate();
        _observation.joined = true;
        if (FAILED(_payload->memory->BindForRetirement(jobs, original)))
            std::terminate();
        _observation.boundBeforeTransfer =
            _payload->memory->BoundTo(jobs, original) && _payload->memory->Bytes() == kCpuCharge;
        auto wrong = original;
        ++wrong.generation;
        _observation.wrongOriginalRefused =
            !_service.RetainJoinedChargeOwner(wrong, *this, _charge, *_payload->memory) &&
            _charge.Bytes() == kGpuCharge;
        _observation.transferred = _service.RetainJoinedChargeOwner(original, *this, _charge, *_payload->memory);
        if (!_observation.transferred || _charge)
            std::terminate();
    }
    void Completed(JobResult) noexcept override
    {
        ++_observation.completed;
    }
    bool HasUtilityOwnerWork() const noexcept override
    {
        return _pending;
    }
    bool IsUtilityOwnerReady() const noexcept override
    {
        return false;
    }
    void PumpUtilityOwner() noexcept override
    {
        std::terminate();
    }
    void StopUtilityOwner() noexcept override {}

  private:
    JobScheduler& _jobs;
    UtilityReadbackOwnerService& _service;
    Observation& _observation;
    GpuResidencyReservation _charge;
    Payload* _payload = nullptr;
    bool _pending = false;
};

bool WaitAttached(JobScheduler& jobs, Observation& observation) noexcept
{
    const auto deadline = GetTickCount64() + kTimeoutMs;
    while (!observation.attached)
    {
        (void)DrainCompletions(jobs);
        if (observation.attached)
            return true;
        const auto now = GetTickCount64();
        if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()),
                                                   static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            return false;
    }
    return true;
}
struct CloseState final
{
    UtilityReadbackOwnerService& service;
    GpuResidencyBudget& budget;
    Observation& observation;
};
} // namespace

int RunUtilityJoinedCloseTests()
{
    failures = 0;
    Observation observation;
    Expect(SUCCEEDED(observation.entered.create(wil::EventOptions::ManualReset)) &&
               SUCCEEDED(observation.release.create(wil::EventOptions::ManualReset)),
           "actual-retirement-events-created");
    if (!observation.entered || !observation.release)
        return 1;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    auto service = std::make_unique<UtilityReadbackOwnerService>();
    GpuResidencyBudget budget;
    Expect(SUCCEEDED(jobs.Initialize(1, {}, {Memory, nullptr})) && jobs.EnableDeferredRelease() &&
               jobs.RegisterOwnerWork(routes) && SUCCEEDED(service->InitializeOwner(jobs, routes)),
           "actual-scheduler-release-sink-and-service-initialized");
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    // This is an independent accounting policy fixture, not an adapter-budget/caller admission proof.
    auto charge = budget.ReserveOwner(kGpuCharge, {1, 1024ull * 1024 * 1024, 0}, admission);
    Expect(charge && admission == GpuResidencyAdmission::Ready, "whole-policy-charge-reserved-before-parent");
    std::unique_ptr<Job> parent = std::make_unique<Parent>(jobs, *service, observation, std::move(charge));
    JobOptions options;
    options.memoryBytes = kCpuCharge;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    JobTicket original;
    if (FAILED(jobs.Submit(parent, options, original)) || !WaitAttached(jobs, observation))
        std::terminate();
    Expect(observation.original == original && observation.unboundChargeRefused && service->HasPending(),
           "one-actual-original-ticket-suspended-with-whole-charges");
    service->StopOwner();
    Expect(!service->ReleaseJoinedChargesOwner() && !service->CloseOwner(),
           "live-scheduler-cannot-refund-or-close-service");
    std::thread witness(
        [&]() noexcept
        {
            observation.heldEventObserved = WaitForSingleObject(observation.entered.get(), kTimeoutMs) == WAIT_OBJECT_0;
            if (observation.heldEventObserved)
            {
                const auto memory = observation.weak.lock();
                observation.heldChargeIntact = memory && memory->IsBound() && memory->Bytes() == kCpuCharge &&
                                               memory->ResidentBytes() == kCpuCharge && observation.heldBytesIntact;
            }
            if (!SetEvent(observation.release.get()))
                std::terminate();
        });
    CloseState state{*service, budget, observation};
    jobs.Close({[](void* opaque) noexcept
                {
                    auto& state = *static_cast<CloseState*>(opaque);
                    auto& observation = state.observation;
                    observation.ownerHook = true;
                    observation.chargeHeldInOwnerHook = state.budget.ReservedOwner() == kGpuCharge &&
                                                        state.budget.ActiveOwner() == 1 && state.service.HasPending() &&
                                                        observation.joined && observation.detached &&
                                                        observation.deferred;
                    observation.releaseRefusedInOwnerHook = !state.service.ReleaseJoinedChargesOwner();
                    observation.closeRefusedInOwnerHook = !state.service.CloseOwner();
                },
                [](void* opaque) noexcept
                {
                    auto& observation = static_cast<CloseState*>(opaque)->observation;
                    observation.workerHook = GetCurrentThreadId() != observation.ownerThread;
                },
                &state});
    witness.join();
    Expect(observation.boundBeforeTransfer && observation.wrongOriginalRefused && observation.transferred,
           "original-joined-hook-binds-cpu-before-exact-gpu-transfer");
    Expect(observation.ownerHook && observation.chargeHeldInOwnerHook && observation.releaseRefusedInOwnerHook &&
               observation.closeRefusedInOwnerHook,
           "owner-close-hook-retains-gpu-charge-until-final-worker-return");
    Expect(observation.workerHook && observation.heldEventObserved && observation.heldChargeIntact &&
               observation.retiredOffOwner && observation.retired,
           "held-final-worker-destroys-literal-bytes-before-acknowledgement");
    Expect(observation.completed == 0 && !jobs.WakeHandle() && budget.ReservedOwner() == kGpuCharge,
           "joined-close-drops-useful-completion-and-keeps-owner-gpu-charge");
    Expect(observation.weak.expired(), "final-worker-destroys-last-readable-cpu-owner");
    observation.weak.reset(); // Retained accounting includes the actual weak control owner, too.
    Expect(service->ReleaseJoinedChargesOwner() && !service->HasPending() && budget.ReservedOwner() == 0 &&
               budget.ActiveOwner() == 0,
           "gpu-refund-only-after-actual-close-return-and-final-worker-ack");
    Expect(service->CloseOwner() && !routes.HasPending() && !routes.HasReady(),
           "stable-route-detached-after-all-original-owners");
    return failures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests

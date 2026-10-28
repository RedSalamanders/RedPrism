#include "Composition.Tests.UtilityOwnerService.h"

#include "OwnerWorkMultiplexer.h"
#include "UtilityReadbackOwnerService.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace
{
constexpr DWORD kWaitTimeoutMs = 5000;

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s utility-owner.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++g_failures;
}

void FailFast(const char* name, DWORD result) noexcept
{
    std::printf("FAIL utility-owner.%s wait=0x%08lx\n", name, result);
    std::fflush(stdout);
    std::_Exit(1);
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    // Routing assertions control memory notifications; a real OS notification is independent ready work.
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

class Receiver final : public UtilityReadbackOwner
{
  public:
    [[nodiscard]] bool HasUtilityOwnerWork() const noexcept override
    {
        return false;
    }

    [[nodiscard]] bool IsUtilityOwnerReady() const noexcept override
    {
        ++readinessQueries;
        return ready;
    }

    void PumpUtilityOwner() noexcept override
    {
        ++pumpCalls;
        if (order && orderCount && *orderCount < order->size())
            (*order)[(*orderCount)++] = index;
        detachDuringPumpRefused = service && original.generation && !service->DetachOwner(original, *this);
    }

    void StopUtilityOwner() noexcept override
    {
        ++stopCalls;
        if (!service || !original.generation)
            return;
        detachDuringStopRefused = !service->DetachOwner(original, *this);
        attachDuringStopRefused = !service->AttachOwner(original, *this);
    }

    void MarkReady(bool value) noexcept
    {
        ready = value;
    }

    UtilityReadbackOwnerService* service = nullptr;
    JobTicket original{};
    std::size_t index = 0;
    bool ready = false;
    mutable std::size_t readinessQueries = 0;
    std::size_t pumpCalls = 0;
    std::size_t stopCalls = 0;
    std::array<std::size_t, JobScheduler::kCapacity>* order = nullptr;
    std::size_t* orderCount = nullptr;
    bool detachDuringPumpRefused = false;
    bool detachDuringStopRefused = false;
    bool attachDuringStopRefused = false;
};

struct Entry final
{
    Receiver receiver;
    JobTicket ticket{};
    bool firstAttachAccepted = false;
    bool duplicateAttachRefused = false;
    bool secondGenerationAttachRefused = false;
    bool callbackStartupRefused = false;
    std::size_t completed = 0;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class AdmittedRoutingJob final : public Job
{
  public:
    AdmittedRoutingJob(Entry& entry, UtilityReadbackOwnerService& service, JobScheduler& scheduler) noexcept
        : _entry(entry), _service(service), _scheduler(scheduler)
    {
    }

    [[nodiscard]] JobResult Run(JobContext&) override
    {
        return {};
    }

    [[nodiscard]] bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _deferred)
            return false;

        _deferred = true;
        _entry.ticket = ticket;
        _entry.receiver.original = ticket;
        UtilityReadbackOwnerService callbackService;
        OwnerWorkMultiplexer callbackRoutes;
        _entry.callbackStartupRefused = FAILED(callbackService.InitializeOwner(_scheduler, callbackRoutes)) &&
                                        !callbackService.HasPending() && !callbackRoutes.HasPending();
        _entry.firstAttachAccepted = _service.AttachOwner(ticket, _entry.receiver);
        _entry.duplicateAttachRefused = !_service.AttachOwner(ticket, _entry.receiver);

        JobTicket otherGeneration = ticket;
        ++otherGeneration.generation;
        _entry.secondGenerationAttachRefused = !_service.AttachOwner(otherGeneration, _entry.receiver);
        return _entry.firstAttachAccepted;
    }

    void Completed(JobResult) noexcept override
    {
        ++_entry.completed;
    }

  private:
    Entry& _entry;
    UtilityReadbackOwnerService& _service;
    JobScheduler& _scheduler;
    bool _deferred = false;
};
#pragma warning(pop)

bool DrainUntilDeferred(JobScheduler& scheduler, const std::vector<Entry>& entries) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWaitTimeoutMs;
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        std::size_t deferred = 0;
        for (const auto& entry : entries)
            deferred += entry.firstAttachAccepted ? 1u : 0u;
        if (deferred == entries.size())
            return true;

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            return false;
        const auto remaining = static_cast<DWORD>(deadline - now);
        const DWORD waited = WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), remaining);
        if (waited == WAIT_FAILED || (waited == WAIT_TIMEOUT && GetTickCount64() >= deadline))
            return false;
    }
}

void TestCrossThreadRefusal(UtilityReadbackOwnerService& service, Entry& entry)
{
    bool attachRefused = false;
    bool detachRefused = false;
    bool readyRefused = false;
    std::thread caller(
        [&]() noexcept
        {
            attachRefused = !service.AttachOwner(entry.ticket, entry.receiver);
            detachRefused = !service.DetachOwner(entry.ticket, entry.receiver);
            readyRefused = !service.HasReady();
        });
    caller.join();
    Expect(attachRefused, "off-owner-attach-refused");
    Expect(detachRefused, "off-owner-detach-refused");
    Expect(readyRefused, "off-owner-readiness-refused");
}
} // namespace

int RunUtilityOwnerServiceRoutingTests()
{
    g_failures = 0;
    static_assert(JobScheduler::kCapacity == 128);
    const volatile std::size_t observedCapacity = JobScheduler::kCapacity;
    Expect(observedCapacity == 128, "literal-original-record-capacity-is-128");

    // The fixed-capacity fixture is heap-backed; only the small router handles live on this stack.
    auto entries = std::make_unique<std::vector<Entry>>(JobScheduler::kCapacity);
    auto service = std::make_unique<UtilityReadbackOwnerService>();
    OwnerWorkMultiplexer routes;
    JobScheduler scheduler;

    Expect(scheduler.Initialize(2, {}, {nullptr, StableMemory}) >= 0, "scheduler-initialized");
    Expect(scheduler.RegisterOwnerWork(routes), "scheduler-routes-registered");
    Expect(SUCCEEDED(service->InitializeOwner(scheduler, routes)), "service-initialized");

    Expect(!service->HasPending(), "empty-service-not-pending");
    Expect(!service->HasReady(), "empty-service-not-ready");
    Expect(scheduler.IsIdle(), "empty-scheduler-is-idle");
    // A notification is ready scheduler work even when this utility service owns no records.
    // Check both states explicitly without relying on the timing of live system-pressure callbacks.
    scheduler.NotifyMemoryChanged();
    Expect(!scheduler.IsIdle() && scheduler.HasUiWork(), "memory-event-makes-empty-scheduler-nonidle");
    Expect(!service->HasPending() && !service->HasReady(), "memory-event-keeps-empty-utility-quiescent");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.IsIdle() && !scheduler.HasUiWork(), "drained-memory-event-restores-empty-idle");
    bool emptyHadReadinessVisits = false;
    for (const auto& entry : *entries)
        emptyHadReadinessVisits = emptyHadReadinessVisits || entry.receiver.readinessQueries != 0;
    Expect(!emptyHadReadinessVisits, "empty-service-does-not-visit-receivers");
    routes.Pump();

    bool allAdmitted = true;
    for (std::size_t index = 0; index < entries->size(); ++index)
    {
        auto& entry = (*entries)[index];
        entry.receiver.service = service.get();
        entry.receiver.index = index;

        std::unique_ptr<Job> job = std::make_unique<AdmittedRoutingJob>(entry, *service, scheduler);
        JobTicket ticket{};
        const std::int32_t submitted = scheduler.Submit(job, {}, ticket);
        allAdmitted = allAdmitted && submitted >= 0 && !job;
        if (submitted < 0 || job)
            FailFast("original-job-admission-failed", static_cast<DWORD>(submitted));
        entry.ticket = ticket;
    }
    Expect(allAdmitted, "all-original-jobs-admitted");

    if (!DrainUntilDeferred(scheduler, *entries))
        FailFast("original-owner-callback-timeout", WAIT_TIMEOUT);

    bool everyActualSlotMatches = true;
    for (std::size_t index = 0; index < entries->size(); ++index)
    {
        const auto& entry = (*entries)[index];
        everyActualSlotMatches = everyActualSlotMatches && entry.ticket.slot == index && entry.ticket.generation != 0 &&
                                 entry.firstAttachAccepted && entry.duplicateAttachRefused &&
                                 entry.secondGenerationAttachRefused && entry.callbackStartupRefused;
    }
    Expect(everyActualSlotMatches, "actual-tickets-fill-slots-0-through-127");
    Expect((*entries)[63].ticket.slot == 63 && (*entries)[64].ticket.slot == 64 && (*entries)[127].ticket.slot == 127,
           "actual-ticket-word-boundary-slots-63-64-127");
    Expect(service->HasPending(), "borrowed-receivers-are-pending");
    Expect(!scheduler.IsIdle(), "original-jobs-stay-suspended-in-their-admissions");
    Expect(!routes.HasReady(), "attached-receivers-remain-dormant");
    routes.Pump();
    bool dormantReceiversUnpumped = true;
    for (const auto& entry : *entries)
        dormantReceiversUnpumped = dormantReceiversUnpumped && entry.receiver.pumpCalls == 0;
    Expect(dormantReceiversUnpumped, "dormant-receivers-are-not-pumped");

    JobTicket foreignGeneration = (*entries)[0].ticket;
    ++foreignGeneration.generation;
    Expect(!service->DetachOwner(foreignGeneration, (*entries)[0].receiver), "foreign-generation-detach-refused");
    Expect(!service->AttachOwner((*entries)[1].ticket, (*entries)[0].receiver),
           "duplicate-parent-on-another-ticket-refused");
    TestCrossThreadRefusal(*service, (*entries)[0]);

    std::array<std::size_t, JobScheduler::kCapacity> order{};
    std::size_t orderCount = 0;
    for (auto& entry : *entries)
    {
        entry.receiver.order = &order;
        entry.receiver.orderCount = &orderCount;
        entry.receiver.MarkReady(true);
    }
    scheduler.NotifyOwnerWork();

    bool exactlyOnePerPass = true;
    for (std::size_t pass = 0; pass < JobScheduler::kCapacity; ++pass)
    {
        const std::size_t before = orderCount;
        routes.Pump();
        exactlyOnePerPass = exactlyOnePerPass && orderCount == before + 1;
    }
    bool roundRobinOrder = orderCount == JobScheduler::kCapacity;
    for (std::size_t pass = 0; pass < orderCount; ++pass)
        roundRobinOrder = roundRobinOrder && order[pass] == pass;
    Expect(exactlyOnePerPass, "one-ready-receiver-per-owner-pump");
    Expect(roundRobinOrder, "round-robin-crosses-slots-63-64-127");

    bool everyPumpDetachRefused = true;
    for (const auto& entry : *entries)
        everyPumpDetachRefused =
            everyPumpDetachRefused && entry.receiver.pumpCalls == 1 && entry.receiver.detachDuringPumpRefused;
    Expect(everyPumpDetachRefused, "pump-callback-cannot-detach-receiver");

    for (auto& entry : *entries)
        entry.receiver.MarkReady(false);
    scheduler.NotifyOwnerWork();
    Expect(!routes.HasReady(), "readiness-cleared-before-stop");

    // Drop three actual word-boundary slots while the remaining original parents stay suspended.
    constexpr std::array<std::size_t, 3> removed{63, 64, 127};
    std::array<std::size_t, 3> removedQueries{};
    for (std::size_t i = 0; i < removed.size(); ++i)
    {
        auto& entry = (*entries)[removed[i]];
        Expect(service->DetachOwner(entry.ticket, entry.receiver), "actual-word-boundary-slot-detached");
        removedQueries[i] = entry.receiver.readinessQueries;
    }
    Expect(service->HasPending() && !service->HasReady(), "partially-detached-original-parents-stay-dormant");
    bool removedNotQueried = true;
    for (std::size_t i = 0; i < removed.size(); ++i)
        removedNotQueried = removedNotQueried && (*entries)[removed[i]].receiver.readinessQueries == removedQueries[i];
    Expect(removedNotQueried, "cleared-bitmap-slots-are-not-queried");
    // An unchanged original ticket outside its acknowledged callback is not new attachment authority.
    Expect(!service->AttachOwner((*entries)[63].ticket, (*entries)[63].receiver),
           "detached-original-cannot-reattach-outside-exact-acknowledged-callback");

    service->StopOwner();
    bool stopCallbacksExcluded = service->HasPending();
    for (std::size_t i = 0; i < entries->size(); ++i)
    {
        const auto& entry = (*entries)[i];
        const bool wasRemoved = i == 63 || i == 64 || i == 127;
        stopCallbacksExcluded = stopCallbacksExcluded &&
                                (wasRemoved ? entry.receiver.stopCalls == 0
                                            : entry.receiver.stopCalls == 1 && entry.receiver.detachDuringStopRefused &&
                                                  entry.receiver.attachDuringStopRefused);
    }
    Expect(stopCallbacksExcluded, "stop-callbacks-cannot-mutate-route");

    bool allDetached = true;
    for (std::size_t i = 0; i < entries->size(); ++i)
        if (i != 63 && i != 64 && i != 127)
        {
            auto& entry = (*entries)[i];
            allDetached = service->DetachOwner(entry.ticket, entry.receiver) && allDetached;
        }
    Expect(allDetached && !service->HasPending(), "all-borrowed-receivers-detached");

    bool allResumed = true;
    for (const auto& entry : *entries)
        allResumed = scheduler.Resume(entry.ticket) && allResumed;
    Expect(allResumed, "all-original-admissions-resumed");
    const SettleResult settled = SettleJobs(scheduler, kWaitTimeoutMs);
    Expect(settled == SettleResult::Settled, "all-original-jobs-joined-and-settled");
    bool allCompleted = true;
    for (const auto& entry : *entries)
        allCompleted = allCompleted && entry.completed == 1;
    Expect(allCompleted, "each-original-job-completed-once");

    Expect(service->CloseOwner(), "service-closed-after-join-and-detach");
    Expect(scheduler.UnregisterOwnerWork(routes), "scheduler-routes-unregistered");
    scheduler.Close();
    return g_failures == 0 ? 0 : 1;
}

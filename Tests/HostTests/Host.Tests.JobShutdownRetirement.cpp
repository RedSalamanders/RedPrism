#include "Host.Tests.JobShutdownRetirement.h"

#include "Common/DeferredRelease.h"
#include "Common/Jobs.h"
#include "Common/WorkerPool.h"

#include <array>
#include <atomic>
#include <combaseapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kMiB = 1024 * 1024;
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s shutdown-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

void Wait(HANDLE event) noexcept
{
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL shutdown-retirement.gate-timeout\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Probe final
{
    JobScheduler* scheduler = nullptr;
    DWORD owner = GetCurrentThreadId();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> runs{0}, ended{0}, callbacks{0}, wrappers{0}, payloads{0};
    std::atomic<DWORD> wrapperThread{0}, payloadThread{0};
    std::atomic<bool> joinedBeforeWrapper{false}, handlesAlive{false}, pixelsIntact{false}, mta{false};
    std::atomic<std::uint64_t> reservedAtWrapper{0};
};
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626)
struct Payload final : DeferredReleaseNode
{
    Probe& probe;
    std::vector<std::uint8_t> pixels;
    explicit Payload(Probe& value) : probe(value), pixels(static_cast<std::size_t>(kDeferredReleaseThreshold), 37)
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<Payload> owned(static_cast<Payload*>(node)); };
    }
    ~Payload()
    {
        probe.pixelsIntact.store(pixels.size() == kDeferredReleaseThreshold && pixels.front() == 37 &&
                                 pixels.back() == 37);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        probe.mta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA);
        probe.payloadThread.store(GetCurrentThreadId());
        probe.payloads.fetch_add(1);
    }
};
#pragma warning(pop)

#pragma warning(pop)

struct Retire final
{
    void operator()(Payload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<Payload> owned(payload);
        }
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class ProbeJob final : public Job
{
  public:
    ProbeJob(Probe& probe, bool waitForShutdown)
        : _probe(probe), _wait(waitForShutdown), _payload(std::make_unique<Payload>(probe).release())
    {
    }
    ~ProbeJob() override
    {
        _probe.wrapperThread.store(GetCurrentThreadId());
        _probe.joinedBeforeWrapper.store(_probe.runs.load() == _probe.ended.load());
        _probe.handlesAlive.store(_probe.scheduler->WakeHandle() != nullptr &&
                                  WaitForSingleObject(_probe.entered.get(), 0) != WAIT_FAILED);
        _probe.reservedAtWrapper.store(_probe.scheduler->ReservedMemoryBytes());
        _probe.wrappers.fetch_add(1);
    }
    JobResult Run(JobContext& context) override
    {
        _probe.runs.fetch_add(1);
        (void)SetEvent(_probe.entered.get());
        if (_wait)
            Wait(static_cast<HANDLE>(context.shutdownEvent));
        _probe.ended.fetch_add(1);
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        _probe.callbacks.fetch_add(1);
    }

  private:
    Probe& _probe;
    bool _wait;
    std::unique_ptr<Payload, Retire> _payload;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class ProbeAction final : public UiAction
{
  public:
    explicit ProbeAction(Probe& probe) : _probe(probe), _payload(std::make_unique<Payload>(probe).release()) {}
    ~ProbeAction() override
    {
        _probe.wrapperThread.store(GetCurrentThreadId());
        _probe.reservedAtWrapper.store(_probe.scheduler->ReservedMemoryBytes());
        _probe.wrappers.fetch_add(1);
    }
    void Run() noexcept override
    {
        _probe.callbacks.fetch_add(1);
    }

  private:
    Probe& _probe;
    std::unique_ptr<Payload, Retire> _payload;
};
#pragma warning(pop)

bool Sample(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {1024 * kMiB + 64 * kMiB, 1024 * kMiB + 64 * kMiB};
    return true;
}

void CheckPayload(const Probe& probe, const char* caseName) noexcept
{
    Expect(probe.payloads.load() == 1 && probe.payloadThread.load() != probe.owner && probe.mta.load() &&
               probe.pixelsIntact.load(),
           caseName);
}

void TestRunningQueuedAndMemoryWaiting()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(2, {}, {Sample, nullptr}) == S_OK && scheduler.EnableDeferredRelease(),
           "standalone-scheduler-owns-release-sink");
    Probe running, queued, waiting, action;
    for (Probe* probe : std::array{&running, &queued, &waiting, &action})
        probe->scheduler = &scheduler;
    std::unique_ptr<Job> first = std::make_unique<ProbeJob>(running, true);
    JobTicket firstTicket;
    Expect(scheduler.Submit(first, {JobClass::Commit, 101, 32 * kMiB}, firstTicket) == S_OK && !first,
           "running-job-submitted");
    Wait(running.entered.get());
    std::unique_ptr<Job> second = std::make_unique<ProbeJob>(queued, false);
    JobTicket secondTicket;
    Expect(scheduler.Submit(second, {JobClass::Commit, 101, 32 * kMiB}, secondTicket) == S_OK && !second,
           "same-document-successor-submitted");
    std::unique_ptr<Job> third = std::make_unique<ProbeJob>(waiting, false);
    JobTicket thirdTicket;
    Expect(scheduler.Submit(third, {JobClass::Commit, 202, 48 * kMiB}, thirdTicket) == S_OK && !third,
           "memory-waiter-submitted");
    std::unique_ptr<UiAction> ownedAction = std::make_unique<ProbeAction>(action);
    Expect(scheduler.PostUiAction(ownedAction) && !ownedAction, "owned-action-submitted");
    Expect(scheduler.ReservedMemoryBytes() == 32 * kMiB && queued.runs.load() == 0 && waiting.runs.load() == 0,
           "queued-and-waiting-never-enter-before-shutdown");
    scheduler.Close();
    Expect(running.runs.load() == 1 && running.ended.load() == 1 && queued.runs.load() == 0 && waiting.runs.load() == 0,
           "shutdown-joins-running-and-drops-unentered-work");
    for (const Probe* probe : std::array{&running, &queued, &waiting})
        Expect(probe->wrappers.load() == 1 && probe->wrapperThread.load() == probe->owner &&
                   probe->joinedBeforeWrapper.load() && probe->handlesAlive.load() &&
                   probe->reservedAtWrapper.load() == 32 * kMiB && probe->callbacks.load() == 0,
               "wrapper-drops-on-owner-after-join-with-handles-and-reservation");
    Expect(action.wrappers.load() == 1 && action.wrapperThread.load() == action.owner &&
               action.reservedAtWrapper.load() == 32 * kMiB && action.callbacks.load() == 0,
           "action-drops-on-owner-with-reservation-without-run");
    CheckPayload(running, "running-payload-retired-once-on-mta-worker");
    CheckPayload(queued, "queued-payload-retired-once-on-mta-worker");
    CheckPayload(waiting, "memory-waiting-payload-retired-once-on-mta-worker");
    CheckPayload(action, "owned-action-payload-retired-once-on-mta-worker");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle() &&
               scheduler.WorkerCount() == 0 && scheduler.WakeHandle() == nullptr,
           "close-acknowledges-all-retirement-before-handles-and-reservations-close");
    scheduler.Close();
    Expect(running.payloads.load() == 1 && queued.payloads.load() == 1 && waiting.payloads.load() == 1 &&
               action.payloads.load() == 1,
           "repeated-close-does-not-destroy-again");
}

void TestUndrainedCompletion()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {Sample, nullptr}) == S_OK && scheduler.EnableDeferredRelease(),
           "undrained-standalone-initialize");
    Probe probe;
    probe.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<ProbeJob>(probe, false);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Commit, 303, 32 * kMiB}, ticket) == S_OK && !job,
           "undrained-job-submitted");
    Wait(probe.entered.get());
    scheduler.Close();
    Expect(probe.runs.load() == 1 && probe.ended.load() == 1 && probe.callbacks.load() == 0 &&
               probe.wrappers.load() == 1 && probe.joinedBeforeWrapper.load(),
           "undrained-completion-joined-without-owner-callback");
    CheckPayload(probe, "undrained-payload-retired-once-on-mta-worker");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0,
           "undrained-retirement-acknowledged");
}
void TestStoppedIdlePoolHooks()
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct State final
    {
        DWORD owner = GetCurrentThreadId();
        unsigned int ownerCalls = 0;
        std::atomic<unsigned int> workerCalls{0};
        std::atomic<bool> ordered{false}, mta{false};
    }
#pragma warning(pop)
    state;
    WorkerPool pool;
    Expect(pool.Initialize(1) == S_OK, "idle-pool-initialize");
    pool.RequestStop();
    const WorkerPool::CloseWork work{
        [](void* context) noexcept
        {
            auto& value = *static_cast<State*>(context);
            if (GetCurrentThreadId() == value.owner)
                ++value.ownerCalls;
        },
        [](void* context) noexcept
        {
            auto& value = *static_cast<State*>(context);
            value.ordered.store(value.ownerCalls == 1 && GetCurrentThreadId() != value.owner);
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            value.mta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA);
            value.workerCalls.fetch_add(1);
        },
        &state};
    pool.Close(work);
    Expect(state.ownerCalls == 1 && state.workerCalls.load() == 1 && state.ordered.load() && state.mta.load(),
           "stopped-idle-pool-final-worker-not-cancelled-and-ordered-after-owner");
    pool.Close(work);
    Expect(state.ownerCalls == 1 && state.workerCalls.load() == 1, "repeated-pool-close-does-not-repeat-either-hook");
    Expect(pool.Initialize(1) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE), "closed-pool-cannot-reinitialize");
    WorkerPool partial;
    Expect(partial.Initialize(1025) == E_INVALIDARG, "invalid-pool-initialization-refused");
    unsigned int partialOwnerCalls = 0;
    partial.Close(
        {[](void* context) noexcept { ++*static_cast<unsigned int*>(context); }, nullptr, &partialOwnerCalls});
    partial.Close(
        {[](void* context) noexcept { ++*static_cast<unsigned int*>(context); }, nullptr, &partialOwnerCalls});
    Expect(partialOwnerCalls == 1, "uninitialized-close-owner-cleanup-is-idempotent");
}
} // namespace

int RunJobShutdownRetirementTests()
{
    g_failures = 0;
    TestRunningQueuedAndMemoryWaiting();
    TestUndrainedCompletion();
    TestStoppedIdlePoolHooks();
    return g_failures;
}

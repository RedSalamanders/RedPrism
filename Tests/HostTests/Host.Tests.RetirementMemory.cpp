#include "Host.Tests.RetirementMemory.h"

#include "DeferredRelease.h"
#include "Jobs.h"
#include "SharedPublicationMemory.h"

#include <wil/resource.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <thread>
#include <utility>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 3ull * 1024 * 1024;
constexpr std::uint64_t kObserverBytes = 4096;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + kAdmissionBytes + kObserverBytes;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr std::size_t kDeferredCapacity = 2;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* scenario, const char* fact) noexcept
{
    std::printf("%s retirement-memory.%s.%s\n", condition ? "PASS" : "FAIL", scenario, fact);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* scenario, const char* fact, std::int32_t result = 0) noexcept
{
    std::printf("FAIL retirement-memory.setup.%s.%s result=%ld\n", scenario, fact, static_cast<long>(result));
    std::fflush(stdout);
    std::_Exit(1);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

enum class Outcome
{
    Finished,
    Failed,
    Cancelled,
    Close
};

[[nodiscard]] const char* ScenarioName(Outcome outcome) noexcept
{
    switch (outcome)
    {
    case Outcome::Finished:
        return "finished";
    case Outcome::Failed:
        return "failed";
    case Outcome::Cancelled:
        return "cancelled";
    case Outcome::Close:
        return "close";
    }
    return "invalid";
}

struct DeferredQueue final
{
    std::array<DeferredReleaseNode*, kDeferredCapacity> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::size_t destroyed = 0;
    std::thread::id destroyThread{};

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& queue = *static_cast<DeferredQueue*>(context);
        if (queue.count == queue.nodes.size())
        {
            queue.overflow = true;
            return;
        }
        queue.nodes[queue.count++] = &node;
    }
};

struct DeferredSinkScope final
{
    DeferredQueue queue;
    bool installed = false;

    DeferredSinkScope() noexcept : installed(InstallDeferredReleaseSink(DeferredQueue::Enqueue, &queue)) {}
    ~DeferredSinkScope()
    {
        if (installed)
            (void)ClearDeferredReleaseSink(&queue);
    }
    DeferredSinkScope(const DeferredSinkScope&) = delete;
    DeferredSinkScope& operator=(const DeferredSinkScope&) = delete;
};

// DeferredReleaseNode deliberately requires 16-byte SLIST alignment, including trailing padding.
#pragma warning(push)
#pragma warning(disable : 4324)
struct DeferredMemoryGraph final
{
    DeferredReleaseNode node{};
    std::shared_ptr<SharedPublicationMemory> memory;
    bool destroyed = false;

    DeferredMemoryGraph() noexcept
    {
        node.bytes = SharedPublicationMemory::OwnershipAllowance();
        node.destroy = Destroy;
    }

    static void Destroy(DeferredReleaseNode* node) noexcept
    {
        auto& graph = *reinterpret_cast<DeferredMemoryGraph*>(node);
        graph.memory.reset();
        graph.destroyed = true;
    }
};
#pragma warning(pop)
static_assert(offsetof(DeferredMemoryGraph, node) == 0);

void DestroyQueuedOnJoinedWorker(DeferredQueue& queue)
{
    std::thread worker(
        [&queue]() noexcept
        {
            queue.destroyThread = std::this_thread::get_id();
            for (std::size_t index = 0; index < queue.count; ++index)
            {
                DeferredReleaseNode* const node = queue.nodes[index];
                if (node && node->destroy)
                {
                    node->destroy(node);
                    ++queue.destroyed;
                }
            }
        });
    worker.join();
    queue.nodes.fill(nullptr);
    queue.count = 0;
}

struct RetirementState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    Outcome outcome = Outcome::Finished;
    std::thread::id ownerThread{};
    std::thread::id workerThread{};
    wil::unique_handle readyEvent;
    wil::unique_handle releaseEvent;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<SharedPublicationMemory> refusalProbe;
    JobResult completion{JobStatus::Failed};
    std::int32_t bindResult = E_UNEXPECTED;
    std::int32_t occupiedResult = E_UNEXPECTED;
    std::int32_t wrongTicketResult = E_UNEXPECTED;
    std::int32_t publicationResult = E_UNEXPECTED;
    std::int32_t usefulBindResult = E_UNEXPECTED;
    std::uint64_t chargeBeforeBind = 0;
    std::uint64_t chargeAfterBind = 0;
    bool callbackRan = false;
    bool joinedHookRan = false;
    bool workerCreatedMemory = false;
    bool workerSawShutdown = false;
    bool callbackWasUiCallback = false;
    bool acknowledgedCallback = true;
    bool joinedWasUiCallback = true;
    bool joinedAcknowledgedCallback = true;
};

class RetirementJob final : public Job
{
  public:
    explicit RetirementJob(RetirementState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        _state.workerThread = std::this_thread::get_id();
        _state.memory = SharedPublicationMemory::CreateForWork();
        _state.refusalProbe = SharedPublicationMemory::CreateForWork();
        if (!_state.memory || !_state.refusalProbe)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _state.workerCreatedMemory = true;
        if (!SetEvent(_state.readyEvent.get()))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};

        const HANDLE waits[]{_state.releaseEvent.get(), static_cast<HANDLE>(context.shutdownEvent)};
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, kTimeoutMs);
        if (wait == WAIT_OBJECT_0)
        {
            if (_state.outcome == Outcome::Failed)
                return {JobStatus::Failed, E_FAIL};
            return {};
        }
        if (wait == WAIT_OBJECT_0 + 1)
        {
            _state.workerSawShutdown = true;
            return {JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        }
        return {JobStatus::Failed,
                wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }

    void Completed(JobResult result) noexcept override
    {
        _state.callbackRan = true;
        _state.completion = result;
        _state.callbackWasUiCallback = _state.scheduler->InUiCallback();
        _state.acknowledgedCallback = _state.scheduler->InAcknowledgedJobCallback(_state.ticket);

        JobTicket wrong = _state.ticket;
        ++wrong.generation;
        _state.wrongTicketResult = _state.refusalProbe->BindForRetirement(*_state.scheduler, wrong);
        // Probe useful authority while the original reservation is still present and the output is empty.
        // Testing only after transfer would also refuse a broken success guard because reserved == 0.
        _state.publicationResult = _state.refusalProbe->BindForPublication(*_state.scheduler, _state.ticket);
        if (result.status != JobStatus::Finished)
            _state.usefulBindResult = _state.refusalProbe->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        _state.chargeBeforeBind = _state.scheduler->ReservedMemoryBytes();
        _state.bindResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
        _state.chargeAfterBind = _state.scheduler->ReservedMemoryBytes();
        _state.occupiedResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _state.joinedHookRan = true;
        _state.joinedWasUiCallback = scheduler.InUiCallback();
        _state.joinedAcknowledgedCallback = scheduler.InAcknowledgedJobCallback(ticket);
        _state.publicationResult = _state.refusalProbe->BindForPublication(scheduler, ticket);
        _state.usefulBindResult = _state.refusalProbe->BindAfterCompletedWork(scheduler, ticket);
        _state.chargeBeforeBind = scheduler.ReservedMemoryBytes();
        _state.bindResult = _state.memory->BindForRetirement(scheduler, ticket);
        _state.chargeAfterBind = scheduler.ReservedMemoryBytes();
    }

  private:
    RetirementState& _state;
};

void StartScheduler(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("shared", "scheduler-initialize");
}

void SubmitGated(JobScheduler& scheduler, RetirementState& state, bool reserveSlot = true)
{
    state.scheduler = &scheduler;
    state.ownerThread = std::this_thread::get_id();
    state.readyEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    state.releaseEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!state.readyEvent || !state.releaseEvent)
        SetupFailure(ScenarioName(state.outcome), "create-events", HRESULT_FROM_WIN32(GetLastError()));

    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = reserveSlot;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure(ScenarioName(state.outcome), "submit-job");
    const DWORD ready = WaitForSingleObject(state.readyEvent.get(), kTimeoutMs);
    if (ready != WAIT_OBJECT_0)
        SetupFailure(ScenarioName(state.outcome), "worker-ready-gate",
                     ready == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
}

void QueueGraph(DeferredSinkScope& sink, RetirementState& state, DeferredMemoryGraph& graph,
                std::weak_ptr<SharedPublicationMemory>& weak) noexcept
{
    graph.memory = std::move(state.memory);
    weak = graph.memory;
    if (!TryDeferRelease(graph.node, DeferredReleasePolicy::AlwaysOnOwner) || sink.queue.overflow ||
        sink.queue.count != 1)
        SetupFailure(ScenarioName(state.outcome), "queue-whole-cpu-graph");
}

void TestTerminal(Outcome outcome, bool releaseWeakFirst, const char* scenario)
{
    DeferredSinkScope sink;
    Expect(sink.installed, scenario, "owner-deferred-sink-installed");
    if (!sink.installed)
        return;

    JobScheduler scheduler;
    StartScheduler(scheduler);
    RetirementState state;
    state.outcome = outcome;
    SubmitGated(scheduler, state);
    Expect(state.workerCreatedMemory && state.workerThread != state.ownerThread &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           scenario, "worker-created-owner-while-original-charge-held-at-event-gate");

    if (outcome == Outcome::Cancelled && !scheduler.Cancel(state.ticket))
        SetupFailure(scenario, "cancel-gated-ticket");
    if (!SetEvent(state.releaseEvent.get()))
        SetupFailure(scenario, "release-event-gate", HRESULT_FROM_WIN32(GetLastError()));
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure(scenario, "settle-terminal-callback");

    const JobStatus expected = outcome == Outcome::Failed      ? JobStatus::Failed
                               : outcome == Outcome::Cancelled ? JobStatus::Cancelled
                                                               : JobStatus::Finished;
    Expect(state.callbackRan && state.completion.status == expected &&
               (outcome != Outcome::Failed || state.completion.error == E_FAIL) && state.callbackWasUiCallback &&
               !state.acknowledgedCallback,
           scenario, "terminal-callback-is-not-useful-acknowledgement");
    Expect(state.bindResult == S_OK && state.memory->IsBound() && state.memory->BoundTo(scheduler, state.ticket),
           scenario, "exact-terminal-callback-binds-retirement-accounting");
    Expect(state.occupiedResult == E_INVALIDARG && state.wrongTicketResult == E_INVALIDARG, scenario,
           "occupied-output-and-wrong-ticket-refused");
    Expect(state.publicationResult != S_OK && (outcome == Outcome::Finished || state.usefulBindResult != S_OK),
           scenario, "retirement-binding-does-not-grant-publication-or-failed-useful-reuse");
    Expect(state.chargeBeforeBind == kAdmissionBytes && state.chargeAfterBind == kAdmissionBytes &&
               state.memory->Bytes() == kAdmissionBytes && state.memory->ResidentBytes() == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           scenario, "one-bind-transfers-whole-charge-without-changing-total");

    const std::int32_t outsideBind = state.refusalProbe->BindForRetirement(scheduler, state.ticket);
    Expect(outsideBind == E_INVALIDARG, scenario, "retirement-bind-outside-exact-callback-refused");

    DeferredMemoryGraph graph;
    std::weak_ptr<SharedPublicationMemory> weak;
    QueueGraph(sink, state, graph, weak);
    state.refusalProbe.reset();
    state.memory.reset();
    if (releaseWeakFirst)
    {
        weak.reset();
        Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes, scenario,
               "charge-remains-while-queued-graph-holds-final-strong-owner");
    }
    DestroyQueuedOnJoinedWorker(sink.queue);
    Expect(graph.destroyed && sink.queue.destroyed == 1 && sink.queue.destroyThread != state.ownerThread, scenario,
           "queued-cpu-graph-destroyed-on-joined-worker");
    if (!releaseWeakFirst)
    {
        Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes && weak.expired(), scenario,
               "charge-remains-after-graph-destruction-while-weak-control-block-lives");
        weak.reset();
    }
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, scenario, "charge-released-after-final-strong-and-weak");
    if (!ClearDeferredReleaseSink(&sink.queue))
        SetupFailure(scenario, "clear-deferred-sink");
    sink.installed = false;
    scheduler.Close();
}

struct MissingSlotState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> memory;
    std::int32_t bindResult = S_OK;
    bool noAcknowledgement = false;
};

class MissingSlotJob final : public Job
{
  public:
    explicit MissingSlotJob(MissingSlotState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.memory = SharedPublicationMemory::CreateForWork();
        return _state.memory ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    void Completed(JobResult result) noexcept override
    {
        _state.noAcknowledgement = !_state.scheduler->InAcknowledgedJobCallback(_state.ticket);
        if (result.status == JobStatus::Finished)
            _state.bindResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
    }

  private:
    MissingSlotState& _state;
};

void TestMissingDescriptorSlot()
{
    JobScheduler scheduler;
    StartScheduler(scheduler);
    MissingSlotState state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<MissingSlotJob>(state);
    JobOptions options{};
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = false;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job ||
        SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("missing-slot", "job-settlement");
    const auto expectedBindResult = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    Expect(state.bindResult == expectedBindResult && state.noAcknowledgement, "missing-slot",
           "retirement-refuses-descriptor-without-reserved-slot-and-no-useful-ack");
    Expect(scheduler.ReservedMemoryBytes() == 0 && state.memory && !state.memory->IsBound(), "missing-slot",
           "refusal-preserves-then-releases-original-reservation");
    state.memory.reset();
    scheduler.Close();
}

struct ObserverState final
{
    JobScheduler& scheduler;
    JobTicket ticket;
    SharedRetainedMemoryLease lease;
    std::int32_t result = E_PENDING;
};

class EndpointObserverJob final : public Job
{
  public:
    explicit EndpointObserverJob(ObserverState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status == JobStatus::Finished)
            _state.result = _state.scheduler.RetainMemory(_state.ticket, _state.lease);
    }

  private:
    ObserverState& _state;
};

void TestCloseWhileGated(bool releaseWeakFirst, const char* scenario)
{
    DeferredSinkScope sink;
    Expect(sink.installed, scenario, "owner-deferred-sink-installed");
    if (!sink.installed)
        return;

    JobScheduler scheduler;
    StartScheduler(scheduler);
    // A separately admitted real lease observes the shared endpoint after Close without retaining any
    // strong reference to the candidate memory owner. Its own bytes are included in every expectation.
    ObserverState observer{scheduler};
    std::unique_ptr<Job> observerJob = std::make_unique<EndpointObserverJob>(observer);
    JobOptions observerOptions{};
    observerOptions.memoryBytes = kObserverBytes;
    observerOptions.reserveRetainedMemory = true;
    if (scheduler.Submit(observerJob, observerOptions, observer.ticket) != S_OK || observerJob ||
        SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled || observer.result != S_OK)
        SetupFailure(scenario, "independent-endpoint-observer-admission");
    Expect(observer.lease.Bytes() == kObserverBytes && observer.lease.ResidentBytes() == kObserverBytes, scenario,
           "separate-original-lease-observes-endpoint-without-candidate-strong-reference");
    RetirementState state;
    state.outcome = Outcome::Close;
    SubmitGated(scheduler, state);
    Expect(WaitForSingleObject(state.releaseEvent.get(), 0) == WAIT_TIMEOUT, scenario,
           "worker-gate-is-not-manually-released-before-close");
    scheduler.Close(); // The worker's actual shutdown event releases WaitForMultipleObjects.

    Expect(state.workerSawShutdown && state.joinedHookRan && !state.callbackRan, scenario,
           "shutdown-event-releases-gated-worker-and-terminal-callback-is-dropped");
    Expect(state.bindResult == S_OK && state.chargeBeforeBind == kAdmissionBytes + kObserverBytes &&
               state.chargeAfterBind == kAdmissionBytes + kObserverBytes && state.memory && state.memory->IsBound() &&
               state.memory->Bytes() == kAdmissionBytes &&
               state.memory->ResidentBytes() == kAdmissionBytes + kObserverBytes,
           scenario, "joined-close-hook-retains-whole-original-charge");
    Expect(!state.joinedAcknowledgedCallback && state.joinedWasUiCallback && state.publicationResult != S_OK &&
               state.usefulBindResult != S_OK,
           scenario, "joined-retirement-is-accounting-only-without-useful-or-publication-authority");

    JobScheduler replacement;
    StartScheduler(replacement);
    Expect(!state.memory->BoundTo(replacement, state.ticket), scenario, "closed-endpoint-never-binds-to-new-scheduler");
    Expect(state.memory->ResidentBytes() == kAdmissionBytes + kObserverBytes, scenario,
           "foreign-endpoint-remains-resident-after-real-close-while-owner-is-strong");

    DeferredMemoryGraph graph;
    std::weak_ptr<SharedPublicationMemory> weak;
    QueueGraph(sink, state, graph, weak);
    state.memory.reset();
    state.refusalProbe.reset(); // The complete original charge also covers this refusing control owner.
    if (releaseWeakFirst)
    {
        weak.reset();
        Expect(observer.lease.ResidentBytes() == kAdmissionBytes + kObserverBytes, scenario,
               "closed-endpoint-keeps-whole-charge-after-weak-release-while-queued-strong-owner-lives");
    }
    DestroyQueuedOnJoinedWorker(sink.queue);
    Expect(graph.destroyed && (releaseWeakFirst || weak.expired()) && sink.queue.destroyThread != state.ownerThread,
           scenario, "queued-owner-destruction-finishes-on-joined-worker");
    if (!releaseWeakFirst)
    {
        Expect(observer.lease.ResidentBytes() == kAdmissionBytes + kObserverBytes, scenario,
               "actual-closed-endpoint-keeps-whole-charge-with-only-candidate-weak-control-owner");
        weak.reset();
    }
    Expect(observer.lease.ResidentBytes() == kObserverBytes, scenario,
           "last-candidate-weak-release-refunds-only-its-original-closed-endpoint-charge");
    observer.lease.Reset();
    replacement.Close();
    if (!ClearDeferredReleaseSink(&sink.queue))
        SetupFailure(scenario, "clear-deferred-sink");
    sink.installed = false;
}
} // namespace

int RunRetirementMemoryTests()
{
    gPassed = gFailed = 0;
    TestTerminal(Outcome::Finished, false, "finished.graph-first");
    TestTerminal(Outcome::Finished, true, "finished.weak-first");
    TestTerminal(Outcome::Failed, false, "failed.graph-first");
    TestTerminal(Outcome::Failed, true, "failed.weak-first");
    TestTerminal(Outcome::Cancelled, false, "cancelled.graph-first");
    TestTerminal(Outcome::Cancelled, true, "cancelled.weak-first");
    TestMissingDescriptorSlot();
    TestCloseWhileGated(false, "close.graph-first");
    TestCloseWhileGated(true, "close.weak-first");
    std::printf("SUMMARY retirement-memory passed=%u failed=%u controls=%u expected=90\n", gPassed, gFailed,
                gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 && gPassed == 90 ? 0 : 1;
}

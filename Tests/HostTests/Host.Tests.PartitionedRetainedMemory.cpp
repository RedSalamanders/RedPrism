#include "Document.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kWhole = 131072;
constexpr std::uint64_t kOutput = 32768;
int failures = 0;
std::uint64_t memoryQueries = 0; // MemorySource is invoked only by the scheduler owner.
void Expect(bool condition, const char* name)
{
    std::printf("%s retained-partition.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}
[[noreturn]] void SetupFailure(const char* name)
{
    std::printf("FAIL retained-partition.setup.%s\n", name);
    std::_Exit(1);
}
bool Sample(void*, MemorySnapshot& snapshot) noexcept
{
    ++memoryQueries;
    snapshot = {kMemoryAdmissionHeadroomBytes + 64 * 1024 * 1024, kMemoryAdmissionHeadroomBytes + 64 * 1024 * 1024};
    return true;
}
void Start(JobScheduler& scheduler)
{
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("start");
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("settle");
}
enum class Route
{
    Completed,
    Publication,
    Retirement,
    Close
};
struct State final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket;
    std::shared_ptr<SharedPublicationMemory> output;
    std::shared_ptr<SharedPublicationMemory> temporary;
    std::optional<SelectionLoops> geometry;
    std::uint64_t outputBytes = kOutput;
    std::int32_t result = E_UNEXPECTED;
    Route route = Route::Completed;
    JobResult workerResult;
    bool alias = false;
    bool wrongTicket = false;
    bool made = false;
    bool called = false;
    bool suspended = false;
    std::int32_t wrong = S_OK;
    std::uint64_t queriesBefore = 0;
    std::uint64_t queriesAfter = 0;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    SharedRetainedMemoryLease* uniqueOutput = nullptr;
    SharedRetainedMemoryLease* uniqueTemporary = nullptr;
    JobResult completed;
};
class PartitionJob final : public Job
{
  public:
    explicit PartitionJob(State& state) : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        _state.output = SharedPublicationMemory::CreateForWork();
        _state.temporary = SharedPublicationMemory::CreateForWork();
        std::vector<SelectionLoop> loops = {{{0, 0}, {2, 0}, {2, 2}, {0, 2}}};
        _state.geometry = SelectionLoops::AdoptForWork(std::move(loops), context.cancellation, _state.output);
        _state.made = _state.geometry.has_value();
        if (_state.entered)
        {
            (void)SetEvent(_state.entered);
            if (WaitForSingleObject(_state.release, 10000) != WAIT_OBJECT_0)
                SetupFailure("worker-release");
        }
        return _state.workerResult;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        if (_state.route != Route::Publication && _state.route != Route::Close)
            return false;
        _state.suspended = true;
        if (_state.route == Route::Publication)
        {
            _state.wrong = _state.output->BindPartitionedAfterCompletedWork(*_state.scheduler, ticket,
                                                                            _state.outputBytes, *_state.temporary);
            Bind(Route::Publication);
        }
        return true; // useful work is over; a real suspended original record stays until Close.
    }
    void Completed(JobResult result) noexcept override
    {
        _state.completed = result;
        if (_state.route != Route::Publication)
            Bind(_state.route);
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        if (!_state.output->IsBound())
            Bind(Route::Retirement);
    }

  private:
    void Bind(Route route) noexcept
    {
        auto ticket = _state.ticket;
        if (_state.wrongTicket)
            ++ticket.generation;
        auto& temporary = _state.alias ? *_state.output : *_state.temporary;
        _state.queriesBefore = memoryQueries;
        if (_state.uniqueOutput)
            _state.result = _state.scheduler->RetainPartitionedMemory(ticket, _state.outputBytes, *_state.uniqueOutput,
                                                                      *_state.uniqueTemporary);
        else
            _state.result = route == Route::Publication ? _state.output->BindPartitionedForPublication(
                                                              *_state.scheduler, ticket, _state.outputBytes, temporary)
                            : route == Route::Retirement
                                ? _state.output->BindPartitionedForRetirement(*_state.scheduler, ticket,
                                                                              _state.outputBytes, temporary)
                                : _state.output->BindPartitionedAfterCompletedWork(*_state.scheduler, ticket,
                                                                                   _state.outputBytes, temporary);
        _state.queriesAfter = memoryQueries;
        _state.called = true;
    }
    State& _state;
};
void Submit(JobScheduler& scheduler, State& state, bool preclaim = true, bool deferred = true)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<PartitionJob>(state);
    JobOptions options;
    options.memoryBytes = kWhole;
    options.reserveRetainedMemory = preclaim;
    options.deferredRetirement = deferred;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("submit");
}
void Clear(State& state, JobScheduler& scheduler)
{
    state.geometry.reset();
    state.output.reset();
    state.temporary.reset();
    Settle(scheduler);
}
void TestGeometryAndWeakOwners()
{
    JobScheduler scheduler;
    Start(scheduler);
    State state;
    Submit(scheduler, state);
    Settle(scheduler);
    Expect(state.result == S_OK && state.called && state.made, "completed-authenticates-original-ticket");
    Expect(state.output->Bytes() == kOutput && state.temporary->Bytes() == kWhole - kOutput,
           "two-immutable-parts-sum-to-original");
    Expect(scheduler.ReservedMemoryBytes() == kWhole && state.output->ResidentBytes() == kWhole,
           "transfer-never-refunds-original");
    Expect(state.output->BoundTo(scheduler, state.ticket) && state.temporary->BoundTo(scheduler, state.ticket),
           "both-parts-retain-exact-endpoint-and-ticket");
    Expect(state.queriesBefore == state.queriesAfter, "partition-queries-no-new-memory-snapshot");
    auto weakGeometry = state.geometry->WeakStorageForTest();
    auto copied = *state.geometry;
    auto weakOutput = std::weak_ptr<SharedPublicationMemory>(state.output);
    auto weakTemporary = std::weak_ptr<SharedPublicationMemory>(state.temporary);
    state.output.reset();
    std::thread releaseTemporary([&] { state.temporary.reset(); });
    releaseTemporary.join();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kWhole, "temporary-weak-control-keeps-entire-charge");
    weakTemporary.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kOutput && copied.PointCount() == 4 && copied[0][2].x == 2 &&
               copied[0][2].y == 2,
           "temporary-refund-leaves-exact-live-geometry-charge");
    state.geometry.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kOutput && copied.PointCount() == 4,
           "copied-history-reader-remains-readable-and-charged");
    copied = {};
    Settle(scheduler);
    Expect(weakGeometry.expired() && !weakOutput.expired(), "actual-worker-geometry-destruction-keeps-weak-accounting");
    weakOutput.reset();
    Expect(scheduler.ReservedMemoryBytes() == kOutput, "geometry-weak-control-survives-accounting-weak-drop");
    weakGeometry.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "last-geometry-weak-control-final-ack");
    scheduler.Close();
}
void TestIndependentAcknowledgements()
{
    for (bool temporaryFirst : {false, true})
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        Submit(scheduler, state);
        Settle(scheduler);
        state.geometry.reset();
        Settle(scheduler);
        std::thread first(
            [&]
            {
                if (temporaryFirst)
                    state.temporary.reset();
                else
                    state.output.reset();
            });
        first.join();
        Settle(scheduler);
        Expect(scheduler.ReservedMemoryBytes() == (temporaryFirst ? kOutput : kWhole - kOutput),
               temporaryFirst ? "temporary-first-exact-ack" : "output-first-exact-ack");
        std::thread second(
            [&]
            {
                if (temporaryFirst)
                    state.output.reset();
                else
                    state.temporary.reset();
            });
        second.join();
        Settle(scheduler);
        Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
               temporaryFirst ? "temporary-first-final-ack" : "output-first-final-ack");
        scheduler.Close();
    }
    JobScheduler scheduler;
    Start(scheduler);
    std::array<State, 32> states;
    for (auto& state : states)
    {
        Submit(scheduler, state);
        Settle(scheduler);
    }
    Expect(scheduler.ReservedMemoryBytes() == 32 * kWhole, "many-original-partitioned-slots-admitted");
    for (auto& state : states)
        state.geometry.reset();
    Settle(scheduler);
    std::thread a(
        [&]
        {
            for (auto& state : states)
                state.output.reset();
        });
    std::thread b(
        [&]
        {
            for (auto& state : states)
                state.temporary.reset();
        });
    a.join();
    b.join();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "simultaneous-two-word-acks-return-exact-aggregate");
    State next;
    Submit(scheduler, next);
    Settle(scheduler);
    Expect(next.result == S_OK && next.output->ResidentBytes() == kWhole, "slot-reuse-has-no-old-part-ack");
    Clear(next, scheduler);
    scheduler.Close();
}
void TestRefusals()
{
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        if (mode == 0)
            state.outputBytes = 0;
        if (mode == 1)
            state.outputBytes = kWhole;
        if (mode == 2)
            state.outputBytes = kWhole + 1;
        if (mode == 3)
            state.alias = true;
        if (mode == 4)
            state.wrongTicket = true;
        if (mode == 7)
            state.workerResult = {JobStatus::Failed, E_FAIL};
        Submit(scheduler, state, mode != 5, mode != 6);
        Settle(scheduler);
        Expect(state.result < 0 && !state.output->IsBound() && !state.temporary->IsBound() &&
                   scheduler.ReservedMemoryBytes() == 0,
               "invalid-transfer-preserves-both-empty-outputs");
        Clear(state, scheduler);
        scheduler.Close();
    }
    JobScheduler scheduler;
    Start(scheduler);
    State state;
    Submit(scheduler, state);
    Settle(scheduler);
    auto extra = SharedPublicationMemory::CreateForWork();
    Expect(state.output->BindPartitionedAfterCompletedWork(scheduler, state.ticket, kOutput, *extra) < 0 &&
               !extra->IsBound() && scheduler.ReservedMemoryBytes() == kWhole,
           "outside-and-repeat-transfer-refused");
    Clear(state, scheduler);
    extra.reset();
    scheduler.Close();
}
void TestPublicationFailureAndClose()
{
    for (auto route : {Route::Publication, Route::Retirement, Route::Close})
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        state.route = route;
        if (route == Route::Retirement)
            state.workerResult = {JobStatus::Failed, E_FAIL};
        Submit(scheduler, state);
        if (route == Route::Retirement)
            Settle(scheduler);
        else
        {
            const auto deadline = GetTickCount64() + 10000;
            while (!state.called && !(route == Route::Close && state.suspended))
            {
                (void)DrainCompletions(scheduler);
                if (state.called || (route == Route::Close && state.suspended))
                    break;
                const auto now = GetTickCount64();
                if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()),
                                                           static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
                    SetupFailure("suspended");
            }
        }
        // Close joins before issuing its exact retirement callback.
        scheduler.Close();
        Expect(state.result == S_OK && state.output->Bytes() == kOutput && state.temporary->Bytes() == kWhole - kOutput,
               "publication-failed-terminal-and-joined-close-retain-both-parts");
        if (route == Route::Publication)
            Expect(state.wrong < 0, "publication-refuses-completed-only-authority");
        Expect(state.output->ResidentBytes() == kWhole && !state.output->BoundTo(scheduler, state.ticket),
               "closed-scheduler-keeps-independent-endpoint-residency");
        auto diagnostic = state.output;
        state.geometry.reset();
        state.output.reset();
        std::thread release([&] { state.temporary.reset(); });
        release.join();
        Expect(diagnostic->ResidentBytes() == kOutput, "post-close-temporary-death-only-refunds-its-part");
        diagnostic.reset();
    }
}
void TestCancelledOriginalAndGrowth()
{
    {
        JobScheduler scheduler;
        Start(scheduler);
        wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!entered || !release)
            SetupFailure("events");
        State state;
        state.route = Route::Retirement;
        state.entered = entered.get();
        state.release = release.get();
        Submit(scheduler, state);
        if (WaitForSingleObject(entered.get(), 10000) != WAIT_OBJECT_0)
            SetupFailure("worker-entered");
        Expect(scheduler.Cancel(state.ticket), "original-running-cancel-accepted");
        (void)SetEvent(release.get());
        Settle(scheduler);
        Expect(state.completed.status == JobStatus::Cancelled && state.result == S_OK &&
                   state.output->Bytes() == kOutput && state.temporary->Bytes() == kWhole - kOutput,
               "actual-cancelled-original-permits-destruction-accounting-only");
        Clear(state, scheduler);
        Expect(scheduler.ReservedMemoryBytes() == 0, "cancelled-both-parts-last-owner-ack");
        scheduler.Close();
    }
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        SharedRetainedMemoryLease output, temporary;
        state.uniqueOutput = &output;
        state.uniqueTemporary = &temporary;
        Submit(scheduler, state);
        Settle(scheduler);
        const auto before = memoryQueries;
        Expect(state.result == S_OK && scheduler.IncreaseRetainedReservation(output, kOutput + 1) < 0 &&
                   scheduler.IncreaseRetainedReservation(temporary, kWhole - kOutput + 1) < 0 &&
                   output.Bytes() == kOutput && temporary.Bytes() == kWhole - kOutput && memoryQueries == before &&
                   scheduler.ReservedMemoryBytes() == kWhole,
               "partitioned-leases-refuse-growth-without-query-or-change");
        state.geometry.reset();
        state.output.reset();
        state.temporary.reset();
        Settle(scheduler);
        output.Reset();
        temporary.Reset();
        Settle(scheduler);
        Expect(scheduler.ReservedMemoryBytes() == 0, "unique-partition-growth-refusal-cleanup");
        scheduler.Close();
    }
}
} // namespace
int RunPartitionedRetainedMemoryTests()
{
    TestGeometryAndWeakOwners();
    TestIndependentAcknowledgements();
    TestRefusals();
    TestPublicationFailureAndClose();
    TestCancelledOriginalAndGrowth();
    std::printf("%s retained-partition.summary failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}

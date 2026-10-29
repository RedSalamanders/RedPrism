#include "Document.Tests.SelectionOutlineAllocation.h"

#include "Jobs.h"
#include "SelectionOutlineRasterWork.h"
#include "SharedPublicationMemory.h"

#include <objbase.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
using OutlineRaster = PreparedSelectionOutlineRaster;
using Grid = OutlineRaster::GridKey;

constexpr std::uint64_t kExtraHeadroom = 4ull * 1024 * 1024 * 1024;
constexpr Grid kGrid{16, 16, 1.0, 0.0, 0.0, 96.0f};

enum class JobKind
{
    Build,
    LimitOne,
    SealedOwner,
    OffOwnerRetire,
};

unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* fact) noexcept
{
    std::printf("%s document.selection-outline-allocation.%s\n", condition ? "PASS" : "FAIL", fact);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

struct MemoryProbe final
{
    MemorySnapshot snapshot{};
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    snapshot = static_cast<MemoryProbe*>(context)->snapshot;
    return true;
}

struct State final
{
    JobScheduler* scheduler = nullptr;
    OutlineRaster published;
    OutlineRaster copy;
    std::weak_ptr<const void> weakStorage;
    std::weak_ptr<const SharedPublicationMemory> weakOutputMemory;
    std::weak_ptr<const SharedPublicationMemory> weakSourceMemory;
    OutlineRaster::MemoryPlan plan{};
    JobTicket ticket{};
    std::uint64_t reservationBytes = 0;
    std::uint64_t outputPartitionBytes = 0;
    std::uint64_t outputLocalBytes = 0;
    std::uint64_t outputDeclaredBytes = 0;
    std::uint64_t outputLeaseBytes = 0;
    std::uint64_t sourceLeaseBytes = 0;
    std::uint64_t refusalLocalBefore = 0;
    std::uint64_t refusalLocalAfter = 0;
    bool refusalOwnerBound = false;
    std::uint64_t reservationAtCompletion = 0;
    const void* sourceIdentity = nullptr;
    const std::uint8_t* firstPlane = nullptr;
    std::size_t firstPlaneBytes = 0;
    std::thread::id ownerThread{};
    std::thread::id workerThread{};
    JobResult completed{};
    std::int32_t bindingResult = E_UNEXPECTED;
    std::int32_t buildResult = E_UNEXPECTED;
    std::int32_t refusalResult = E_UNEXPECTED;
    bool callbackSeen = false;
    bool workerPrepared = false;
    bool outputBoundToExactTicket = false;
    bool sourceBoundToExactTicket = false;
    bool offOwnerRasterReleased = false;
    std::uint64_t offOwnerLocalBefore = 0;
    std::uint64_t offOwnerLocalAfter = 0;
    std::weak_ptr<const void> offOwnerWeakStorage;
    std::weak_ptr<const SharedPublicationMemory> offOwnerWeakMemory;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SelectionOutlineAllocationJob final : public Job
{
  public:
    SelectionOutlineAllocationJob(State& state, JobKind kind,
                                  std::shared_ptr<const SharedPublicationMemory> suppliedOwner = {}) noexcept
        : _state(state), _kind(kind), _suppliedOwner(std::move(suppliedOwner))
    {
    }

    JobResult Run(JobContext& context) override
    {
        _state.workerThread = std::this_thread::get_id();
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment))
            return {JobStatus::Failed, apartment};
        const auto uninitialize = wil::scope_exit([]() noexcept { CoUninitialize(); });
        (void)uninitialize;

        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};

        if (_kind == JobKind::OffOwnerRetire)
        {
            auto sourceMemory = SharedPublicationMemory::CreateForWork();
            std::shared_ptr<const void> lifetime{sourceMemory, nullptr};
            std::vector<SelectionLoop> loops;
            loops.push_back({{2.0, 2.0}, {10.0, 2.0}, {10.0, 10.0}, {2.0, 10.0}});
            auto source = SelectionLoops::AdoptForWork(std::move(loops), context.cancellation, std::move(lifetime));
            if (!source)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            auto outputMemory = SharedPublicationMemory::CreateForWork(_state.plan.outputBytes);
            OutlineRaster workerRaster;
            const auto prepared = OutlineRaster::Prepare(std::move(source.value()), kGrid, outputMemory,
                                                         context.cancellation, {}, workerRaster);
            if (prepared != S_OK || !workerRaster.HasStorage())
                return {JobStatus::Failed, prepared};
            _state.offOwnerWeakStorage = workerRaster.WeakStorageForTest();
            _state.offOwnerWeakMemory = outputMemory;
            _state.offOwnerLocalBefore = outputMemory->LocalAllocationBytes();
            workerRaster = OutlineRaster{};
            _state.offOwnerLocalAfter = outputMemory->LocalAllocationBytes();
            _state.offOwnerRasterReleased = _state.offOwnerWeakStorage.expired() &&
                                            _state.offOwnerLocalBefore > _state.offOwnerLocalAfter &&
                                            _state.offOwnerLocalAfter > 0 &&
                                            _state.offOwnerLocalBefore - _state.offOwnerLocalAfter >= 9u * 16u * 16u;
            return {};
        }

        _sourceMemory = SharedPublicationMemory::CreateForWork();
        std::shared_ptr<const void> lifetime{_sourceMemory, nullptr};
        std::vector<SelectionLoop> loops;
        loops.push_back({{2.0, 2.0}, {10.0, 2.0}, {10.0, 10.0}, {2.0, 10.0}});
        auto source = SelectionLoops::AdoptForWork(std::move(loops), context.cancellation, std::move(lifetime));
        if (!source)
            return {JobStatus::Failed, E_OUTOFMEMORY};

        if (_kind == JobKind::Build)
        {
            _outputMemory = SharedPublicationMemory::CreateForWork(_state.plan.outputBytes);
            _state.sourceIdentity = source->Identity();
            _state.weakSourceMemory = _sourceMemory;
            _state.buildResult = OutlineRaster::Prepare(std::move(source.value()), kGrid, _outputMemory,
                                                        context.cancellation, {}, _state.published);
            _state.workerPrepared = _state.buildResult == S_OK && _state.published.HasStorage();
            if (!_state.workerPrepared)
                return {JobStatus::Failed, _state.buildResult};
            _state.firstPlane = _state.published.Plane(0).data();
            _state.firstPlaneBytes = _state.published.Plane(0).size();
            return {};
        }

        if (_kind == JobKind::LimitOne)
            _outputMemory = SharedPublicationMemory::CreateForWork(1);
        _attemptOwner = _kind == JobKind::LimitOne ? _outputMemory : _suppliedOwner;
        if (!_attemptOwner)
            return {JobStatus::Failed, E_INVALIDARG};
        _state.refusalLocalBefore = _attemptOwner->LocalAllocationBytes();
        _state.sourceIdentity = _state.published.SourceIdentity(); // caller-output witness; never a source for new data
        _state.firstPlane = _state.published.Plane(0).data();
        _state.firstPlaneBytes = _state.published.Plane(0).size();
        // Deliberately let std::bad_alloc escape Run. JobScheduler's actual worker runner must translate it.
        _state.refusalResult = OutlineRaster::Prepare(std::move(source.value()), kGrid, _attemptOwner,
                                                      context.cancellation, {}, _state.published);
        return {_state.refusalResult == S_OK ? JobStatus::Finished : JobStatus::Failed,
                _state.refusalResult == S_OK ? S_OK : _state.refusalResult};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_kind != JobKind::Build || result.status != JobStatus::Finished)
            return false;
        _state.ticket = ticket;
        _state.outputLocalBytes = _outputMemory->LocalAllocationBytes();
        _state.outputDeclaredBytes = _state.published.DeclaredOutputBytes();
        _state.outputPartitionBytes = _state.plan.outputBytes + SharedPublicationMemory::OwnershipAllowance();
        _state.bindingResult = _outputMemory->BindPartitionedForPublication(
            *_state.scheduler, ticket, _state.outputPartitionBytes, *_sourceMemory);
        if (_state.bindingResult == S_OK)
        {
            _state.outputLeaseBytes = _outputMemory->Bytes();
            _state.sourceLeaseBytes = _sourceMemory->Bytes();
            _state.outputBoundToExactTicket = _outputMemory->BoundTo(*_state.scheduler, ticket);
            _state.sourceBoundToExactTicket = _sourceMemory->BoundTo(*_state.scheduler, ticket);
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.completed = result;
        _state.callbackSeen = true;
        if (_kind == JobKind::Build)
        {
            _state.weakStorage = _state.published.WeakStorageForTest();
            _state.weakOutputMemory = _outputMemory;
            _state.reservationAtCompletion = _state.scheduler->ReservedMemoryBytes();
            _outputMemory.reset();
            _sourceMemory.reset();
        }
        else
        {
            if (_attemptOwner)
            {
                _state.refusalLocalAfter = _attemptOwner->LocalAllocationBytes();
                _state.refusalOwnerBound = _attemptOwner->IsBound();
            }
            _state.reservationAtCompletion = _state.scheduler->ReservedMemoryBytes();
            _outputMemory.reset();
            _attemptOwner.reset();
            _sourceMemory.reset();
        }
    }

  private:
    State& _state;
    const JobKind _kind;
    std::shared_ptr<const SharedPublicationMemory> _suppliedOwner;
    std::shared_ptr<SharedPublicationMemory> _outputMemory;
    std::shared_ptr<const SharedPublicationMemory> _attemptOwner;
    std::shared_ptr<SharedPublicationMemory> _sourceMemory;
};
#pragma warning(pop)

bool Settle(JobScheduler& scheduler, const char* fact)
{
    const auto result = SettleJobs(scheduler, 10'000);
    Expect(result == SettleResult::Settled, fact);
    return result == SettleResult::Settled;
}

bool SubmitAndSettle(JobScheduler& scheduler, State& state, JobKind kind,
                     std::shared_ptr<const SharedPublicationMemory> suppliedOwner = {})
{
    std::unique_ptr<Job> job = std::make_unique<SelectionOutlineAllocationJob>(state, kind, std::move(suppliedOwner));
    JobOptions options;
    options.kind = JobClass::Selection;
    options.memoryBytes = kind == JobKind::Build ? state.reservationBytes
                          : kind == JobKind::OffOwnerRetire
                              ? state.plan.totalBytes + SharedPublicationMemory::OwnershipAllowance() * 2
                              : 2ull * 1024 * 1024;
    options.reserveRetainedMemory = kind == JobKind::Build;
    options.deferredRetirement = kind == JobKind::Build;
    if (kind == JobKind::Build)
        Expect(options.reserveRetainedMemory && options.deferredRetirement,
               "publication-job-reserves-retained-slot-and-deferred-retirement");
    JobTicket ticket{};
    const auto submitted = scheduler.Submit(job, options, ticket);
    const char* admissionFact = kind == JobKind::Build            ? "success-job-admitted-to-real-scheduler"
                                : kind == JobKind::LimitOne       ? "limit-one-job-admitted"
                                : kind == JobKind::OffOwnerRetire ? "off-owner-retirement-job-admitted"
                                                                  : "sealed-owner-job-admitted";
    Expect(submitted == S_OK && !job, admissionFact);
    if (submitted != S_OK)
        return false;
    state.ticket = ticket;
    return Settle(scheduler, "scheduler-settles-by-event-with-timeout-only-as-deadlock-guard");
}

void TestScheduledAllocationAndFailureControls(JobScheduler& scheduler, State& state)
{
    state.ownerThread = std::this_thread::get_id();
    const auto planned = OutlineRaster::PlanMemory(kGrid, 1, 4, state.plan);
    Expect(planned == S_OK && state.plan.outputBytes > 0 && state.plan.totalBytes > state.plan.outputBytes,
           "fixed-16-square-grid-has-nonzero-output-and-temporary-plan");
    if (planned != S_OK)
        return;

    const std::uint64_t ownerAllowance = SharedPublicationMemory::OwnershipAllowance();
    const bool arithmeticFits =
        ownerAllowance <= (std::numeric_limits<std::uint64_t>::max)() / 2 &&
        state.plan.totalBytes <= (std::numeric_limits<std::uint64_t>::max)() - ownerAllowance * 2 &&
        state.plan.outputBytes <= (std::numeric_limits<std::uint64_t>::max)() - ownerAllowance;
    Expect(arithmeticFits, "fixed-test-plan-arithmetic-fits");
    if (!arithmeticFits)
        return;
    state.reservationBytes = state.plan.totalBytes + ownerAllowance * 2;
    state.outputPartitionBytes = state.plan.outputBytes + ownerAllowance;
    Expect(state.outputPartitionBytes > 0 && state.outputPartitionBytes < state.reservationBytes,
           "output-and-source-temporary-are-distinct-positive-partitions-of-one-original-ticket");
    if (state.outputPartitionBytes >= state.reservationBytes)
        return;

    if (!SubmitAndSettle(scheduler, state, JobKind::Build))
        return;
    Expect(state.callbackSeen && state.completed.status == JobStatus::Finished && state.buildResult == S_OK &&
               state.workerPrepared && state.workerThread != state.ownerThread,
           "all-source-and-nine-plane-preparation-ran-on-admitted-worker-and-completed-successfully");
    Expect(state.bindingResult == S_OK && state.outputBoundToExactTicket && state.sourceBoundToExactTicket &&
               state.outputLeaseBytes == state.outputPartitionBytes &&
               state.sourceLeaseBytes == state.reservationBytes - state.outputPartitionBytes &&
               state.reservationAtCompletion == state.reservationBytes,
           "successful-post-join-callback-binds-and-seals-both-original-ticket-partitions");
    bool planeShapes = state.published.HasStorage() && state.published.DeclaredOutputBytes() == state.plan.outputBytes;
    for (std::size_t plane = 0; plane < OutlineRaster::PlaneCount; ++plane)
        planeShapes = planeShapes && state.published.Plane(plane).size() == 16u * 16u;
    Expect(planeShapes, "published-nine-planes-each-own-exact-fixed-grid-buffer");
    Expect(state.outputLocalBytes > 0 && state.outputLocalBytes <= state.plan.outputBytes &&
               state.outputDeclaredBytes == state.plan.outputBytes,
           "actual-accounted-data-and-shared-control-allocation-is-positive-and-within-plan-output-upper-bound");
    Expect(state.published.SourceIdentity() == state.sourceIdentity && !state.weakSourceMemory.expired(),
           "published-immutable-result-retains-the-worker-created-older-source-owner");

    state.copy = state.published;
    Expect(state.copy.HasStorage() && state.copy.Plane(0).data() == state.published.Plane(0).data() &&
               state.copy.SourceIdentity() == state.sourceIdentity,
           "strong-copy-shares-immutable-buffer-and-source");

    State limitState;
    limitState.scheduler = &scheduler;
    limitState.published = state.published;
    const auto limitIdentity = limitState.published.SourceIdentity();
    const auto limitBytes = limitState.published.Plane(0).data();
    const auto limitSize = limitState.published.Plane(0).size();
    if (!SubmitAndSettle(scheduler, limitState, JobKind::LimitOne))
        return;
    Expect(limitState.completed.status == JobStatus::Failed && limitState.completed.error == E_OUTOFMEMORY,
           "local-limit-one-throws-through-real-worker-runner-as-eoutofmemory");
    Expect(limitState.refusalLocalBefore == 0 && limitState.refusalLocalAfter == 0 && !limitState.refusalOwnerBound,
           "limit-one-refusal-occurs-before-any-output-charge-and-terminal-failure-does-not-bind");
    Expect(limitState.published.SourceIdentity() == limitIdentity &&
               limitState.published.Plane(0).data() == limitBytes && limitState.published.Plane(0).size() == limitSize,
           "limit-one-refusal-leaves-prior-successful-output-unchanged");

    auto sealedOwner = state.weakOutputMemory.lock();
    Expect(static_cast<bool>(sealedOwner), "successful-publication-retains-output-accounting-owner");
    if (!sealedOwner)
        return;
    State sealedState;
    sealedState.scheduler = &scheduler;
    sealedState.published = state.published;
    const auto sealedIdentity = sealedState.published.SourceIdentity();
    const auto sealedBytes = sealedState.published.Plane(0).data();
    const auto sealedSize = sealedState.published.Plane(0).size();
    if (!SubmitAndSettle(scheduler, sealedState, JobKind::SealedOwner, sealedOwner))
        return;
    Expect(sealedState.completed.status == JobStatus::Failed && sealedState.completed.error == E_OUTOFMEMORY,
           "sealed-output-owner-refuses-late-local-allocation-through-real-worker-runner");
    Expect(sealedState.refusalLocalBefore == state.outputLocalBytes &&
               sealedState.refusalLocalAfter == sealedState.refusalLocalBefore && sealedState.refusalOwnerBound,
           "sealed-owner-rejection-does-not-grow-charge-or-rebind-under-failed-terminal-callback");
    Expect(sealedState.published.SourceIdentity() == sealedIdentity &&
               sealedState.published.Plane(0).data() == sealedBytes &&
               sealedState.published.Plane(0).size() == sealedSize,
           "sealed-owner-refusal-leaves-prior-successful-output-unchanged");
    sealedOwner.reset();

    limitState.published = OutlineRaster{};
    sealedState.published = OutlineRaster{};
    state.published = OutlineRaster{};
    Expect(state.copy.HasStorage() && !state.weakStorage.expired() && !state.weakOutputMemory.expired() &&
               scheduler.ReservedMemoryBytes() == state.reservationBytes,
           "strong-copy-and-full-original-charge-remain-before-last-reader-release");
    state.copy = OutlineRaster{};
    Expect(state.weakStorage.expired() && scheduler.ReservedMemoryBytes() == state.reservationBytes,
           "last-strong-raster-expires-storage-weak-pointer-while-full-charge-remains");
    if (!Settle(scheduler, "deferred-node-destruction-joins-before-source-weak-observation"))
        return;
    Expect(state.weakSourceMemory.expired() && scheduler.ReservedMemoryBytes() == state.reservationBytes,
           "joined-deferred-destruction-expires-source-object-while-weak-control-retains-full-charge");
    state.weakSourceMemory.reset();
    if (!Settle(scheduler, "source-weak-control-refund-acknowledges-without-waiting"))
        return;
    Expect(state.weakSourceMemory.expired() && scheduler.ReservedMemoryBytes() == state.outputPartitionBytes,
           "reset-source-weak-observer-refunds-source-partition-while-output-control-remains");
    state.weakStorage.reset();
    Expect(state.weakOutputMemory.expired() && scheduler.ReservedMemoryBytes() == state.outputPartitionBytes,
           "expired-output-owner-weak-observer-still-retains-exact-output-charge");
    state.weakOutputMemory.reset();
    if (!Settle(scheduler, "final-output-weak-control-refund-acknowledges-without-polling"))
        return;
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "reset-final-output-weak-owner-observer-consumes-original-reservation-refund");

    State offOwnerState;
    offOwnerState.scheduler = &scheduler;
    offOwnerState.ownerThread = std::this_thread::get_id();
    offOwnerState.plan = state.plan;
    if (!SubmitAndSettle(scheduler, offOwnerState, JobKind::OffOwnerRetire))
        return;
    Expect(offOwnerState.completed.status == JobStatus::Finished && offOwnerState.offOwnerRasterReleased &&
               offOwnerState.workerThread != offOwnerState.ownerThread && offOwnerState.offOwnerWeakStorage.expired() &&
               !offOwnerState.offOwnerWeakMemory.expired() && scheduler.ReservedMemoryBytes() == 0,
           "worker-created-raster-data-is-destroyed-off-owner-while-weak-control-survives");
    offOwnerState.offOwnerWeakStorage.reset();
    Expect(offOwnerState.offOwnerWeakMemory.expired() && scheduler.ReservedMemoryBytes() == 0,
           "off-owner-last-weak-storage-observer-releases-unbound-owner-control");
}
} // namespace

int RunSelectionOutlineAllocationTests()
{
    gPassed = 0;
    gFailed = 0;
    MemoryProbe probe{{kMemoryAdmissionHeadroomBytes + kExtraHeadroom, kMemoryAdmissionHeadroomBytes + kExtraHeadroom}};
    JobScheduler scheduler;
    const auto initialized = scheduler.Initialize(1, {}, {SampleMemory, &probe});
    Expect(initialized == S_OK, "one-worker-scheduler-initializes-with-deterministic-headroom");
    if (initialized == S_OK)
    {
        const bool deferredRelease = scheduler.EnableDeferredRelease();
        Expect(deferredRelease, "scheduler-enables-owner-deferred-publication-retirement");
        if (deferredRelease)
        {
            State state;
            state.scheduler = &scheduler;
            TestScheduledAllocationAndFailureControls(scheduler, state);
            state.copy = OutlineRaster{};
            state.published = OutlineRaster{};
            state.weakStorage.reset();
            state.weakOutputMemory.reset();
            state.weakSourceMemory.reset();
        }
        scheduler.Close();
    }
    std::printf("%s document.selection-outline-allocation.summary passed=%u failed=%u controls=%u\n",
                gFailed == 0 ? "PASS" : "FAIL", gPassed, gFailed, gPassed + gFailed);
    std::fflush(stdout);
    return static_cast<int>(gFailed);
}

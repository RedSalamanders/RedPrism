#include "Jobs.h"
#include "SharedRetainedMemoryState.h"

#include "DeferredRelease.h"
#include "PipelineTrace.h"
#include "PixelBuffer.h"
#include "UiTiming.h"
#include "WorkerPool.h"

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace
{
std::atomic<std::uint64_t> gRetainedMemoryOwners{1};

std::uint64_t NextRetainedMemoryOwner() noexcept
{
    auto current = gRetainedMemoryOwners.load(std::memory_order_relaxed);
    while (current != 0)
    {
        const auto next = current == (std::numeric_limits<std::uint64_t>::max)() ? 0 : current + 1;
        if (gRetainedMemoryOwners.compare_exchange_weak(current, next, std::memory_order_relaxed))
            return current;
    }
    return 0; // exhaustion permanently refuses transfers; no lifetime can alias an earlier owner
}

WorkPriority Priority(JobClass kind) noexcept
{
    switch (kind)
    {
    case JobClass::InteractivePreview:
    case JobClass::Refinement:
    case JobClass::Stroke:
    case JobClass::Selection:
        return WorkPriority::High;
    case JobClass::Commit:
    case JobClass::Io:
        return WorkPriority::Normal;
    case ::JobClass::Background:
    case ::JobClass::Count:
    default:
        return WorkPriority::Background;
    }
}

bool Serial(JobClass kind) noexcept
{
    return kind == JobClass::Stroke || kind == JobClass::Selection || kind == JobClass::Commit || kind == JobClass::Io;
}
} // namespace

// Windows interlocked SLIST requires this alignment on both its head and every intrusive entry.
#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct JobScheduler::Impl final
{
    enum class State
    {
        Empty,
        WaitingAdmission,
        Queued,
        Running,
        Completed,
        Suspended,
        Resuming
    };
    struct Record;
    struct alignas(MEMORY_ALLOCATION_ALIGNMENT) Node final
    {
        SLIST_ENTRY link{};
        Record* record = nullptr;
        bool final = false;
    };
    static_assert(offsetof(Node, link) == 0);
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct Record final
    {
        // State, sequence and slot reuse are protected by Impl::lock. The payload/options/cancel source are
        // immutable during each worker stage. Published stage ownership returns to the owner until Resume;
        // no worker can re-enter a job while its completion hook is executing.
        State state = State::Empty;
        std::uint64_t generation = 0;
        std::uint64_t sequence = 0;
        JobOptions options{};
        std::uint64_t reserved = 0;
        std::size_t retainedSlot = JobScheduler::kRetainedMemoryCapacity;
        bool continuingOutputTransferred = false;
        std::unique_ptr<Job> job;
        std::optional<CancelSource> cancel;
        JobResult result{};
        Impl* owner = nullptr;
        Node progress{{}, this, false};
        Node final{{}, this, true};
        std::atomic<unsigned int> percent{0};
        std::atomic<bool> progressQueued{false};
        std::atomic_flag reporting = ATOMIC_FLAG_INIT;
        // Only the thread holding reporting may read/write these. Other reporters skip, never spin.
        unsigned int publishedPercent = 0;
        std::uint64_t publishedAt = 0;
    };
#pragma warning(pop)

    Impl()
    {
        InitializeSListHead(&completions);
        InitializeSListHead(&releases);
    }

    struct RetainedMemory final
    {
        std::uint64_t generation = 0;
        std::uint64_t bytes = 0;
        bool shared = false;
        bool partitioned = false;
        std::array<std::uint64_t, 2> parts{};
        std::uint64_t reservedFor = 0; // exact original record sequence; owner only
    };
    const std::uint64_t retainedMemoryOwner = NextRetainedMemoryOwner();
    std::array<RetainedMemory, JobScheduler::kRetainedMemoryCapacity> retainedMemory{};
    std::shared_ptr<SharedRetainedMemoryLease::State> sharedMemory;
    DWORD ownerThread = GetCurrentThreadId();
    SRWLOCK lock = SRWLOCK_INIT;
    _Guarded_by_(lock) bool closing = false;
    _Guarded_by_(lock) std::array<Record, JobScheduler::kCapacity> records {};
    std::uint64_t nextSequence = 0;          // owner thread
    std::size_t liveCount = 0;               // owner thread, counts queued/running/undrained
    bool inCallback = false;                 // owner thread, guards drain and actions across nested message loops
    bool inJoinedRetirementCallback = false; // exact actual joined Close hook; reuses retainedMemoryCallback ticket
    std::optional<JobTicket> retainedMemoryCallback;    // exact final apply only, never progress/pump/action
    std::optional<JobTicket> acknowledgedJobCallback;   // owner-only original joined success hook
    std::optional<JobTicket> publicationMemoryCallback; // exact acknowledged successful DeferCompletion only
    bool closed = false;
    JobObserver observer;
    JobExecutionObserver executionObserver; // Immutable after initialization; borrowed through joined Close.
    MemorySource memorySource;
    MemoryBudget memory;
    bool releaseSinkInstalled = false;
    bool ownsRecycle = false;
    bool lowPressure = false;
    std::uint64_t recycleCap = 0;
    std::atomic<bool> trimPending{false};
    std::atomic<unsigned int> trimCount{0}; // queued plus executing trims
    std::atomic<std::uint64_t> releasingBytes{0};
    std::atomic<std::size_t> releasingCount{0};
    std::atomic<unsigned int> memoryRetirements{0}; // publication between final count decrement and readiness
    std::atomic<bool> memoryChanged{false};
    // A signal is one-shot. Low arms high and high arms low: never poll or rearm a signaled level in a loop.
    std::atomic<unsigned int> pressureSignal{0}; // 1 low, 2 high
    std::array<std::unique_ptr<UiAction>, JobScheduler::kUiActionCapacity> actions;
    std::size_t actionHead = 0;
    std::size_t actionCount = 0;
    UiAction* actionSource = nullptr; // borrowed, owner thread only; registration outlives every callback
    bool sourceReady = false;
    bool preferSource = true;
    OwnerWorkPump* ownerWork = nullptr; // borrowed, owner thread only
    std::atomic<bool> ownerNotified{false};
    bool resumesPending = false; // owner thread; promotes Resuming only outside a completion hook
    alignas(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER completions{};
    alignas(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER releases{};
    wil::unique_handle wake;
    wil::unique_handle shutdown;
    WorkerPool workers;
    std::unique_ptr<WorkerPool> recorders; // Created only by explicit owner startup, immutable until Close.
    WaitPool memoryWaits;
    bool waitsInitialized = false;
    std::array<std::uint64_t, WaitPool::kCapacity> eventGenerations{}; // external registrations, owner only
    wil::unique_handle lowMemory;
    wil::unique_handle highMemory;
    WaitPool::Ticket lowWait;
    WaitPool::Ticket highWait;

    static_assert(sizeof(DeferredReleaseNode::link) >= sizeof(SLIST_ENTRY));
    static_assert(alignof(DeferredReleaseNode) >= alignof(SLIST_ENTRY));

    void MemoryChanged() noexcept
    {
        memoryChanged.store(true, std::memory_order_release);
        (void)SetEvent(wake.get());
    }

    template <typename Integer> void FinishMemoryRetirement(std::atomic<Integer>& count) noexcept
    {
        // Keep idle false across publication. Admission observes completed release/trim counts before
        // consuming readiness, including the recycle-cap restore. One wake follows the entire publication.
        memoryRetirements.fetch_add(1, std::memory_order_relaxed);
        count.fetch_sub(1, std::memory_order_release);
        memoryChanged.store(true, std::memory_order_release);
        memoryRetirements.fetch_sub(1, std::memory_order_release);
        (void)SetEvent(wake.get());
    }

    static void LowMemory(void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        self.pressureSignal.store(1, std::memory_order_release);
        self.MemoryChanged();
    }

    static void HighMemory(void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        self.pressureSignal.store(2, std::memory_order_release);
        self.MemoryChanged();
    }

    static void EnqueueRelease(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        self.CheckOwner();
        // Count before publication; a worker can consume the node immediately. No allocation, job slot,
        // reservation or cancellation token is needed to return memory to the OS.
        self.releasingBytes.fetch_add(node.bytes, std::memory_order_relaxed);
        self.releasingCount.fetch_add(1, std::memory_order_release);
        InterlockedPushEntrySList(&self.releases, reinterpret_cast<SLIST_ENTRY*>(node.link));
        (void)self.workers.Wake(WorkPriority::Normal);
    }

    void DrainReleases() noexcept
    {
        while (auto* entry = InterlockedPopEntrySList(&releases))
        {
            auto* node = reinterpret_cast<DeferredReleaseNode*>(entry);
            const std::uint64_t bytes = node->bytes;
            node->destroy(node);
            releasingBytes.fetch_sub(bytes, std::memory_order_relaxed);
            FinishMemoryRetirement(releasingCount);
        }
    }

    void RequestTrim() noexcept
    {
        // Disable returns before dispatch. The worker may be freeing cached pages while the UI keeps pumping.
        SetPixelBufferRecycleCap(0);
        trimCount.fetch_add(1, std::memory_order_release);
        if (!trimPending.exchange(true, std::memory_order_acq_rel))
        {
            (void)workers.Wake(WorkPriority::Normal);
        }
        else
        {
            trimCount.fetch_sub(1, std::memory_order_release);
        }
    }

    void TrimRecycle() noexcept
    {
        // A newer request may run concurrently with this one. The pool detaches its fixed slot array under
        // its lock, so each allocation is destroyed once, outside that lock. Count includes detached pages.
        if (trimPending.exchange(false, std::memory_order_acq_rel))
        {
            TrimPixelBufferRecycle();
            FinishMemoryRetirement(trimCount);
        }
    }

    _Requires_lock_held_(lock) HRESULT
        ValidateRetainedGrowth(const SharedRetainedMemoryLease& retained, std::uint64_t totalBytes) const noexcept
    {
        if (closing || closed || workers.ShutdownToken().stop_requested())
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        if (!retained._state || retained._state != sharedMemory || retained._slot >= kRetainedMemoryCapacity ||
            !retained._generation || !retained._bytes)
            return E_INVALIDARG;
        const auto& slot = retainedMemory[retained._slot];
        if (slot.partitioned || !slot.shared || slot.generation != retained._generation ||
            slot.bytes != retained._bytes ||
            (sharedMemory->pending[retained._part].load(std::memory_order_acquire) &
             (std::uint64_t{1} << retained._slot)))
            return E_INVALIDARG;
        return totalBytes < slot.bytes ? E_INVALIDARG : S_OK;
    }

    _Requires_lock_held_(lock) HRESULT
        ValidateReservationGrowth(JobTicket ticket, std::uint64_t totalBytes) const noexcept
    {
        const auto& record = records[ticket.slot];
        if (closing || closed || record.state != State::Suspended || record.generation != ticket.generation ||
            !record.cancel)
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        if (record.cancel->Token().IsCancelled() || workers.ShutdownToken().stop_requested())
            return E_ABORT;
        if (record.options.continuingOutputLimit)
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE); // Complete future stages were admitted together.
        return totalBytes < record.reserved ? E_INVALIDARG : S_OK;
    }

    bool SampleMemory(MemorySnapshot& snapshot) const noexcept
    {
        if (memorySource.sample)
        {
            return memorySource.sample(memorySource.context, snapshot);
        }
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
        {
            return false;
        }
        snapshot = {status.ullAvailPhys, status.ullAvailPageFile};
        return true;
    }

    void CheckOwner() const noexcept
    {
        assert(ownerThread == GetCurrentThreadId());
    }

    void Notify(JobNoticeKind event, JobTicket ticket, JobClass kind, unsigned int percent = 0) noexcept
    {
        if (observer.notify)
        {
            observer.notify(observer.context, {event, ticket, kind, percent});
        }
    }

    void Publish(Node& node) noexcept
    {
        InterlockedPushEntrySList(&completions, &node.link);
        (void)SetEvent(wake.get());
    }

    static void Report(void* context, unsigned int percent) noexcept
    {
        auto& record = *static_cast<Record*>(context);
        if (percent > 100 || record.reporting.test_and_set(std::memory_order_acquire))
        {
            return;
        }
        const auto release = wil::scope_exit([&]() noexcept { record.reporting.clear(std::memory_order_release); });
        if (percent <= record.percent.load(std::memory_order_relaxed))
        {
            return;
        }
        record.percent.store(percent, std::memory_order_relaxed);
        const std::uint64_t now = GetTickCount64();
        if (percent > record.publishedPercent && (record.publishedPercent == 0 || now - record.publishedAt >= 100) &&
            !record.progressQueued.exchange(true, std::memory_order_acq_rel))
        {
            record.publishedPercent = percent;
            record.publishedAt = now;
            record.owner->Publish(record.progress);
        }
    }

    _Requires_lock_held_(lock) bool Blocked(const Record& candidate) const noexcept
    {
        if (!Serial(candidate.options.kind) || candidate.cancel->Token().IsCancelled())
        {
            return false;
        }
        for (const auto& other : records)
        {
            if (other.state != State::Empty && other.sequence < candidate.sequence &&
                other.options.kind == candidate.options.kind && other.options.serialKey == candidate.options.serialKey)
            {
                return true; // the predecessor keeps the key until its result has applied or dropped
            }
        }
        return false;
    }

    void AdmitWaiting() noexcept
    {
        CheckOwner();
        // Visit every waiting record once in input order. An impossible large request must not occupy a pool
        // callback or prevent an unrelated smaller request from running. No partial reservation is retained.
        std::uint64_t after = 0;
        for (;;)
        {
            Record* selected = nullptr;
            {
                WorkerResources::ExclusiveLock guard{lock};
                if (closing)
                {
                    return;
                }
                for (auto& record : records)
                {
                    if (record.state == State::WaitingAdmission && record.sequence > after &&
                        (!selected || record.sequence < selected->sequence) && !Blocked(record))
                    {
                        selected = &record;
                    }
                }
            }
            if (!selected)
            {
                break;
            }
            after = selected->sequence;
            const bool cancelled = selected->cancel->Token().IsCancelled();
            MemorySnapshot snapshot{};
            const bool sampled = cancelled || selected->options.memoryBytes == 0 || SampleMemory(snapshot);
            auto decision = cancelled ? MemoryAdmission::Refused
                            : sampled ? memory
                                            .Reserve(selected->options.memoryBytes, snapshot,
                                                     releasingBytes.load(std::memory_order_acquire) +
                                                         (trimCount.load(std::memory_order_acquire) != 0 ? 1 : 0))
                                            .admission
                                      : MemoryAdmission::Refused;
            if (decision == MemoryAdmission::Waiting && !selected->options.allowMemoryWaiting)
                decision = MemoryAdmission::Refused;
            if (!cancelled && sampled && selected->options.allowMemoryWaiting && decision != MemoryAdmission::Ready &&
                PixelBufferRecycleBytes() != 0)
            {
                RequestTrim();
                continue;
            }
            TracePipelineMemory(decision == MemoryAdmission::Ready     ? "admitted"
                                : decision == MemoryAdmission::Waiting ? "waiting"
                                                                       : "refused",
                                selected->options.memoryBytes, memory.Reserved(),
                                releasingBytes.load(std::memory_order_acquire));
            if (decision == MemoryAdmission::Waiting)
            {
                continue;
            }
            {
                WorkerResources::ExclusiveLock guard{lock};
                if (decision == MemoryAdmission::Ready)
                {
                    selected->reserved = selected->options.memoryBytes;
                    selected->state = State::Queued;
                }
                else
                {
                    selected->result =
                        cancelled ? JobResult{JobStatus::Cancelled} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
                    selected->state = State::Completed;
                }
            }
            if (decision != MemoryAdmission::Ready)
            {
                Publish(selected->final);
            }
        }
        WakeQueued();
    }

    bool DrainSharedMemoryAcknowledgements() noexcept
    {
        if (!sharedMemory)
            return false;
        const auto persistent = sharedMemory->persistentReleasedBytes.exchange(0, std::memory_order_acquire);
        bool changed = persistent != 0;
        if (persistent != 0)
        {
            const bool released = memory.Release(persistent);
            assert(released);
            (void)released;
        }
        for (std::size_t part = 0; part < 2; ++part)
        {
            auto mask = sharedMemory->pending[part].exchange(0, std::memory_order_acquire);
            changed = changed || mask != 0;
            while (mask != 0)
            {
                const auto index = static_cast<std::size_t>(std::countr_zero(mask));
                mask &= mask - 1;
                auto& cache = retainedMemory[index];
                const auto generation =
                    sharedMemory->acknowledged[part * kRetainedMemoryCapacity + index].load(std::memory_order_relaxed);
                const auto bytes = cache.parts[part];
                assert(cache.shared && bytes != 0 && cache.bytes >= bytes && cache.generation == generation);
                if (cache.shared && bytes != 0 && cache.bytes >= bytes && cache.generation == generation)
                {
                    const bool released = memory.Release(bytes);
                    assert(released);
                    (void)released;
                    cache.parts[part] = 0;
                    cache.bytes -= bytes;
                    if (!cache.bytes)
                        cache.shared = false;
                }
            }
        }
        return changed;
    }

    void ProcessMemoryChange() noexcept
    {
        const bool sharedChanged = DrainSharedMemoryAcknowledgements();
        if (!memoryChanged.exchange(false, std::memory_order_acq_rel) && !sharedChanged)
        {
            return;
        }
        const unsigned int pressure = pressureSignal.exchange(0, std::memory_order_acq_rel);
        if (pressure == 1)
        {
            lowPressure = true;
            RequestTrim();
            (void)memoryWaits.Rearm(highWait);
        }
        else if (pressure == 2)
        {
            lowPressure = false;
            (void)memoryWaits.Rearm(lowWait);
        }
        if (ownsRecycle && !lowPressure && trimCount.load(std::memory_order_acquire) == 0)
        {
            SetPixelBufferRecycleCap(recycleCap);
        }
        AdmitWaiting();
    }

    Record* Take(WorkPriority priority, JobExecutionLane lane, JobExecutionNotice* observation) noexcept
    {
        WorkerResources::ExclusiveLock guard{lock};
        if (closing)
        {
            return nullptr;
        }
        Record* selected = nullptr;
        for (auto& record : records)
        {
            if (record.state == State::Queued && record.options.executionLane == lane &&
                Priority(record.options.kind) == priority && (!selected || record.sequence < selected->sequence) &&
                !Blocked(record))
            {
                selected = &record;
            }
        }
        if (selected)
        {
            selected->state = State::Running;
            if (observation)
                *observation = {JobExecutionBoundary::BodyEntered,
                                {static_cast<std::size_t>(selected - records.data()), selected->generation},
                                selected->options.kind,
                                selected->options.executionLane,
                                std::nullopt};
        }
        return selected;
    }

    static void Dispatch(void* context, WorkPriority priority, CancelToken) noexcept
    {
        DispatchLane(context, priority, JobExecutionLane::Compute);
    }
    static void DispatchRecorder(void* context, WorkPriority priority, CancelToken) noexcept
    {
        DispatchLane(context, priority, JobExecutionLane::GpuRecording);
    }

    void ObserveExecution(const JobExecutionNotice& identity, JobExecutionBoundary boundary,
                          std::optional<JobResult> result) noexcept
    {
        if (executionObserver.notify)
            executionObserver.notify(executionObserver.context,
                                     {boundary, identity.ticket, identity.kind, identity.lane, result});
    }
    JobResult RunBody(Record& record, JobContext& execution, const JobExecutionNotice& identity)
    {
        if (!executionObserver.cancellationObserved)
            return record.job->Run(execution);
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4625 4626 5026 5027)
        struct FirstCheck final
        {
            Impl& scheduler;
            const JobExecutionNotice& identity;
            std::atomic<bool> reported{false};
        }
#pragma warning(pop)
        first{*this, identity};
        static_assert(std::atomic<bool>::is_always_lock_free);
        const CancellationObservation::Scope scope{{[](void* context) noexcept
                                                    {
                                                        auto& check = *static_cast<FirstCheck*>(context);
                                                        if (!check.reported.exchange(true, std::memory_order_relaxed))
                                                            check.scheduler.executionObserver.cancellationObserved(
                                                                check.scheduler.executionObserver.context,
                                                                {JobExecutionBoundary::CancellationObserved,
                                                                 check.identity.ticket, check.identity.kind,
                                                                 check.identity.lane, std::nullopt});
                                                    },
                                                    &first}};
        // The job and executor contract joins helpers before return/unwind; no retained token owns scope.
        return record.job->Run(execution);
    }
    static void DispatchLane(void* context, WorkPriority priority, JobExecutionLane lane) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        if (lane == JobExecutionLane::Compute && priority == WorkPriority::Normal)
        {
            self.DrainReleases();
            self.TrimRecycle();
        }
        // The optional immutable identity is captured under Take's existing lock. Observers run outside
        // that lock and never inspect the guarded slot array; normal execution captures no identity.
        JobExecutionNotice observation{};
        while (Record* record = self.Take(priority, lane,
                                          (self.executionObserver.notify || self.executionObserver.cancellationObserved)
                                              ? &observation
                                              : nullptr))
        {
            JobContext execution(self.workers, record->cancel->Token().WithShutdown(self.workers.ShutdownToken()),
                                 self.shutdown.get(), Report, record, record->reserved,
                                 record->options.deferredRetirement);
            JobResult result{JobStatus::Cancelled};
            if (!execution.cancellation.IsCancelled())
            {
                self.ObserveExecution(observation, JobExecutionBoundary::BodyEntered, std::nullopt);
                try
                {
                    result = self.RunBody(*record, execution, observation);
                }
                catch (const std::bad_alloc&)
                {
                    result = {JobStatus::Failed, E_OUTOFMEMORY};
                }
                self.ObserveExecution(observation, JobExecutionBoundary::BodyExited, result);
            }
            else
                self.ObserveExecution(observation, JobExecutionBoundary::BodySkipped, result);
            if (execution.cancellation.IsCancelled())
            {
                result = {JobStatus::Cancelled};
            }
            if (record->options.deferredRetirement)
                record->job->RetireWorkerStage(execution, result);
            {
                WorkerResources::ExclusiveLock guard{self.lock};
                record->result = result;
                record->state = State::Completed;
            }
            // No access to record after this call. A draining UI may immediately destroy/reuse the slot.
            self.Publish(record->final);
            if (lane == JobExecutionLane::Compute && priority == WorkPriority::Normal)
            {
                self.DrainReleases();
                self.TrimRecycle();
            }
        }
    }

    void WakeQueued() noexcept
    {
        std::array<std::array<bool, 3>, 2> pending{};
        {
            WorkerResources::ExclusiveLock guard{lock};
            if (closing)
            {
                return;
            }
            for (const auto& record : records)
            {
                if (record.state == State::Queued && !Blocked(record))
                {
                    pending[static_cast<std::size_t>(record.options.executionLane)]
                           [static_cast<std::size_t>(Priority(record.options.kind))] = true;
                }
            }
        }
        for (std::size_t lane = 0; lane < pending.size(); ++lane)
            for (std::size_t index = 0; index < pending[lane].size(); ++index)
                if (pending[lane][index])
                {
                    if (lane == 0)
                        (void)workers.Wake(static_cast<WorkPriority>(index));
                    else if (recorders)
                        (void)recorders->Wake(static_cast<WorkPriority>(index));
                }
    }

    bool ResumeOwner(JobTicket ticket, std::optional<JobExecutionLane> lane) noexcept
    {
        CheckOwner();
        {
            WorkerResources::ExclusiveLock guard{lock};
            auto& record = records[ticket.slot];
            if (closing || record.state != State::Suspended || record.generation != ticket.generation ||
                (lane && lane.value() == JobExecutionLane::GpuRecording && !recorders))
                return false;
            if (lane)
                record.options.executionLane = lane.value(); // Prior stage has actually acknowledged.
            record.state = State::Resuming;
        }
        resumesPending = true;
        (void)SetEvent(wake.get());
        return true;
    }

    void PromoteResumed() noexcept
    {
        if (!resumesPending)
        {
            return;
        }
        resumesPending = false;
        {
            WorkerResources::ExclusiveLock guard{lock};
            if (closing)
            {
                return;
            }
            for (auto& record : records)
            {
                if (record.state == State::Resuming)
                {
                    record.state = State::Queued;
                }
            }
        }
        WakeQueued();
    }
};
#pragma warning(pop)

#pragma warning(pop)

JobScheduler::JobScheduler() noexcept = default;
JobScheduler::~JobScheduler()
{
    Close();
}

std::int32_t JobScheduler::Initialize(unsigned int threads, JobObserver observer, MemorySource memory,
                                      JobExecutionObserver execution) noexcept
{
    if (_impl)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    try
    {
        auto candidate = std::make_unique<Impl>();
        candidate->sharedMemory = std::make_shared<SharedRetainedMemoryLease::State>();
        _impl = std::move(candidate);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    _impl->wake.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!_impl->wake)
    {
        const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    _impl->shutdown.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_impl->shutdown)
    {
        const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    if (!DuplicateHandle(GetCurrentProcess(), _impl->wake.get(), GetCurrentProcess(), _impl->sharedMemory->wake.put(),
                         0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    _impl->observer = observer;
    _impl->executionObserver = execution;
    _impl->memorySource = memory;
    HRESULT result = _impl->workers.Initialize(threads, Impl::Dispatch, _impl.get(), true);
    if (SUCCEEDED(result) && !memory.sample)
    {
        _impl->lowMemory.reset(CreateMemoryResourceNotification(LowMemoryResourceNotification));
        _impl->highMemory.reset(CreateMemoryResourceNotification(HighMemoryResourceNotification));
        if (!_impl->lowMemory || !_impl->highMemory)
        {
            result = HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            result = _impl->memoryWaits.Initialize();
            _impl->waitsInitialized = SUCCEEDED(result);
        }
        if (SUCCEEDED(result))
        {
            result = _impl->memoryWaits.Add(_impl->lowMemory.get(), Impl::LowMemory, _impl.get(), _impl->lowWait);
        }
        if (SUCCEEDED(result))
        {
            result = _impl->memoryWaits.Add(_impl->highMemory.get(), Impl::HighMemory, _impl.get(), _impl->highWait);
        }
    }
    if (FAILED(result))
    {
        Close();
    }
    return result;
}

std::int32_t JobScheduler::EnableGpuRecordingLane() noexcept
{
    if (!_impl)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.nextSequence != 0 || self.recorders ||
        self.workers.ShutdownToken().stop_requested())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    try
    {
        auto candidate = std::make_unique<WorkerPool>();
        const HRESULT result = candidate->Initialize(1, Impl::DispatchRecorder, &self, true);
        if (SUCCEEDED(result))
            self.recorders = std::move(candidate);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // Explicit optional startup boundary; caller must refuse recording or fail startup.
    }
}

bool JobScheduler::EnableDeferredRelease() noexcept
{
    if (!_impl || _impl->closed || _impl->workers.ShutdownToken().stop_requested())
    {
        return false;
    }
    _impl->CheckOwner();
    if (!_impl->releaseSinkInstalled)
    {
        _impl->releaseSinkInstalled = InstallDeferredReleaseSink(Impl::EnqueueRelease, _impl.get());
    }
    return _impl->releaseSinkInstalled;
}

bool JobScheduler::EnableMemoryRecycling() noexcept
{
    if (!_impl || _impl->closed || _impl->workers.ShutdownToken().stop_requested())
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.ownsRecycle)
    {
        return true;
    }
    MemorySnapshot snapshot{};
    if (!self.SampleMemory(snapshot))
    {
        return false;
    }
    const auto budget = EvaluateMemoryAdmission(0, snapshot.availablePhysicalBytes, snapshot.availableCommitBytes,
                                                self.memory.Reserved(), 0)
                            .availableAfterReservations;
    self.recycleCap = (std::min)(budget / 8, 512ull * 1024 * 1024);
    self.ownsRecycle = InitializePixelBufferRecycle(self.recycleCap);
    return self.ownsRecycle;
}

std::uint64_t JobScheduler::ReservedMemoryBytes() const noexcept
{
    if (!_impl)
    {
        return 0;
    }
    _impl->CheckOwner();
    return _impl->memory.Reserved();
}

std::uint64_t JobScheduler::ReleasingMemoryBytes() const noexcept
{
    return _impl ? _impl->releasingBytes.load(std::memory_order_acquire) : 0;
}

std::int32_t JobScheduler::RetainMemory(JobTicket ticket, RetainedMemoryTicket& retained) noexcept
{
    return RetainMemoryImpl(ticket, retained, false);
}

std::int32_t JobScheduler::RetainMemoryImpl(JobTicket ticket, RetainedMemoryTicket& retained, bool publication,
                                            bool retirement) noexcept
{
    if (!_impl || _impl->closed)
        return E_UNEXPECTED;
    auto& self = *_impl;
    self.CheckOwner();
    const auto& callback = publication ? self.publicationMemoryCallback : self.retainedMemoryCallback;
    if (!self.inCallback || !callback || callback.value() != ticket || retained.owner != 0 ||
        retained.generation != 0 || ticket.slot >= kCapacity || self.retainedMemoryOwner == 0)
        return E_INVALIDARG;
    WorkerResources::ExclusiveLock guard{self.lock};
    auto& record = self.records[ticket.slot];
    const auto expectedState = publication ? Impl::State::Suspended : Impl::State::Completed;
    if (record.generation != ticket.generation || record.state != expectedState || record.reserved == 0 ||
        (!retirement && (self.closing || record.result.status != JobStatus::Finished || !record.cancel ||
                         record.cancel->Token().IsCancelled() || self.workers.ShutdownToken().stop_requested())))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if ((publication || retirement) && (!record.options.deferredRetirement || !record.options.reserveRetainedMemory ||
                                        record.retainedSlot >= kRetainedMemoryCapacity))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (record.retainedSlot < kRetainedMemoryCapacity)
    {
        const auto index = record.retainedSlot;
        auto& cache = self.retainedMemory[index];
        if (cache.reservedFor != record.sequence || cache.bytes || !cache.generation)
            std::terminate();
        cache.reservedFor = 0;
        cache.bytes = std::exchange(record.reserved, 0);
        cache.shared = false;
        record.retainedSlot = kRetainedMemoryCapacity;
        retained = {index, cache.generation, self.retainedMemoryOwner};
        if (publication)
            self.publicationMemoryCallback.reset();
        return S_OK;
    }
    for (std::size_t index = 0; index < self.retainedMemory.size(); ++index)
    {
        auto& cache = self.retainedMemory[index];
        if (cache.bytes == 0 && cache.reservedFor == 0 &&
            cache.generation != (std::numeric_limits<std::uint64_t>::max)())
        {
            ++cache.generation;
            cache.bytes = std::exchange(record.reserved, 0);
            cache.shared = false;
            retained = {index, cache.generation, self.retainedMemoryOwner};
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

std::int32_t JobScheduler::RetainMemory(JobTicket ticket, SharedRetainedMemoryLease& retained) noexcept
{
    return RetainSharedMemoryImpl(ticket, retained, false);
}

std::int32_t JobScheduler::RetainPublicationMemory(JobTicket ticket, SharedRetainedMemoryLease& retained) noexcept
{
    return RetainSharedMemoryImpl(ticket, retained, true);
}

std::int32_t JobScheduler::RetainContinuingOutputMemory(JobTicket original, std::uint64_t outputBytes,
                                                        SharedRetainedMemoryLease& output) noexcept
{
    if (!_impl || _impl->closed)
        return E_UNEXPECTED;
    auto& self = *_impl;
    self.CheckOwner();
    if (!self.inCallback || !self.publicationMemoryCallback || self.publicationMemoryCallback.value() != original ||
        !self.acknowledgedJobCallback || self.acknowledgedJobCallback.value() != original || output || !outputBytes ||
        original.slot >= kCapacity || !self.retainedMemoryOwner)
        return E_INVALIDARG;
    WorkerResources::ExclusiveLock guard{self.lock};
    auto& record = self.records[original.slot];
    if (record.generation != original.generation || record.state != Impl::State::Suspended || self.closing ||
        record.result.status != JobStatus::Finished || !record.cancel || record.cancel->Token().IsCancelled() ||
        self.workers.ShutdownToken().stop_requested() || !record.options.deferredRetirement ||
        !record.options.reserveRetainedMemory || !record.options.continuingOutputLimit ||
        record.continuingOutputTransferred || outputBytes > record.options.continuingOutputLimit ||
        outputBytes >= record.reserved || record.retainedSlot >= kRetainedMemoryCapacity)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto& terminal = self.retainedMemory[record.retainedSlot];
    if (terminal.reservedFor != record.sequence || terminal.bytes || !terminal.generation)
        std::terminate();
    // The original memory budget already owns these bytes. Only their ownership changes. Persistent output
    // uses the existing endpoint's aggregate last-reader acknowledgement, so the terminal slot stays reserved.
    record.reserved -= outputBytes;
    record.continuingOutputTransferred = true;
    self.sharedMemory->residentBytes.fetch_add(outputBytes, std::memory_order_relaxed);
    output._state = self.sharedMemory;
    output._slot = kRetainedMemoryCapacity;
    output._part = 2;
    output._generation = original.generation;
    output._bytes = outputBytes;
    self.publicationMemoryCallback.reset(); // No second transfer in this same joined acknowledgement.
    return S_OK;
}

std::int32_t JobScheduler::RetainRetirementMemory(JobTicket ticket, SharedRetainedMemoryLease& retained) noexcept
{
    return RetainSharedMemoryImpl(ticket, retained, false, true);
}

bool JobScheduler::OwnsRetainedMemoryEndpoint(const SharedRetainedMemoryLease& retained) const noexcept
{
    if (!_impl)
        return false;
    _impl->CheckOwner();
    // Both endpoints are strongly held. Their addresses cannot alias while an older lease survives.
    return retained._state && _impl->sharedMemory == retained._state;
}

std::int32_t JobScheduler::RetainSharedMemoryImpl(JobTicket ticket, SharedRetainedMemoryLease& retained,
                                                  bool publication, bool retirement) noexcept
{
    if (retained)
        return E_INVALIDARG;
    RetainedMemoryTicket transfer{};
    const auto result = RetainMemoryImpl(ticket, transfer, publication, retirement);
    if (result != S_OK)
        return result;
    auto& self = *_impl;
    // A successful transfer owns one of the fixed slots. Fail fast before indexing if this bridge's
    // contract is ever broken; the explicit bound also keeps compiler analysis independent of callee inference.
    if (transfer.slot >= kRetainedMemoryCapacity)
        std::terminate();
    auto& cache = self.retainedMemory[transfer.slot];
    cache.shared = true;
    cache.partitioned = false;
    cache.parts = {cache.bytes, 0};
    self.sharedMemory->residentBytes.fetch_add(cache.bytes, std::memory_order_relaxed);
    retained._state = self.sharedMemory;
    retained._slot = transfer.slot;
    retained._part = 0;
    retained._generation = transfer.generation;
    retained._bytes = cache.bytes;
    return S_OK;
}

std::int32_t JobScheduler::RetainPartitionedMemory(JobTicket ticket, std::uint64_t outputBytes,
                                                   SharedRetainedMemoryLease& output,
                                                   SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPartitionedMemoryImpl(ticket, outputBytes, output, temporary, false, false);
}
std::int32_t JobScheduler::RetainPartitionedPublicationMemory(JobTicket ticket, std::uint64_t outputBytes,
                                                              SharedRetainedMemoryLease& output,
                                                              SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPartitionedMemoryImpl(ticket, outputBytes, output, temporary, true, false);
}
std::int32_t JobScheduler::RetainPartitionedRetirementMemory(JobTicket ticket, std::uint64_t outputBytes,
                                                             SharedRetainedMemoryLease& output,
                                                             SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPartitionedMemoryImpl(ticket, outputBytes, output, temporary, false, true);
}
std::int32_t JobScheduler::RetainPartitionedMemoryImpl(JobTicket ticket, std::uint64_t outputBytes,
                                                       SharedRetainedMemoryLease& output,
                                                       SharedRetainedMemoryLease& temporary, bool publication,
                                                       bool retirement) noexcept
{
    if (!_impl || _impl->closed)
        return E_UNEXPECTED;
    auto& self = *_impl;
    self.CheckOwner();
    if (&output == &temporary || output || temporary || !outputBytes || ticket.slot >= kCapacity)
        return E_INVALIDARG;
    {
        WorkerResources::ExclusiveLock guard{self.lock};
        const auto& record = self.records[ticket.slot];
        if (record.generation != ticket.generation || !record.options.deferredRetirement ||
            !record.options.reserveRetainedMemory || record.retainedSlot >= kRetainedMemoryCapacity ||
            outputBytes >= record.reserved)
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    // The existing bridge authenticates exact callback/status/cancellation and consumes the original
    // descriptor-reserved slot. Nothing after success can fail or allocate. Both leases are immutable.
    const auto result = RetainSharedMemoryImpl(ticket, output, publication, retirement);
    if (result != S_OK)
        return result;
    auto& cache = self.retainedMemory[output._slot];
    cache.partitioned = true;
    cache.parts = {outputBytes, cache.bytes - outputBytes};
    output._bytes = outputBytes;
    temporary._state = self.sharedMemory;
    temporary._slot = output._slot;
    temporary._part = 1;
    temporary._generation = output._generation;
    temporary._bytes = cache.parts[1];
    return S_OK;
}

std::int32_t JobScheduler::RetainPersistentOutputMemory(JobTicket original, std::uint64_t outputBytes,
                                                        SharedRetainedMemoryLease& output,
                                                        SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPersistentOutputMemoryImpl(original, outputBytes, output, temporary, false, false);
}
std::int32_t JobScheduler::RetainPersistentOutputPublicationMemory(JobTicket original, std::uint64_t outputBytes,
                                                                   SharedRetainedMemoryLease& output,
                                                                   SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPersistentOutputMemoryImpl(original, outputBytes, output, temporary, true, false);
}
std::int32_t JobScheduler::RetainPersistentOutputRetirementMemory(JobTicket original, std::uint64_t outputBytes,
                                                                  SharedRetainedMemoryLease& output,
                                                                  SharedRetainedMemoryLease& temporary) noexcept
{
    return RetainPersistentOutputMemoryImpl(original, outputBytes, output, temporary, false, true);
}
std::int32_t JobScheduler::RetainPersistentOutputMemoryImpl(JobTicket original, std::uint64_t outputBytes,
                                                            SharedRetainedMemoryLease& output,
                                                            SharedRetainedMemoryLease& temporary, bool publication,
                                                            bool retirement) noexcept
{
    // Reuse all existing admission/authority checks before consuming the preclaimed slot.
    const auto result = RetainPartitionedMemoryImpl(original, outputBytes, output, temporary, publication, retirement);
    if (result != S_OK)
        return result;
    auto& self = *_impl;
    auto& cache = self.retainedMemory[temporary._slot];
    // Both accounts already own this exact reservation. Conversion changes neither resident nor reserved
    // totals, and cannot allocate/fail. The cache slot now belongs solely to the temporary phase.
    cache.bytes = temporary._bytes;
    cache.parts = {temporary._bytes, 0};
    temporary._part = 0;
    output._slot = kRetainedMemoryCapacity;
    output._part = 2;
    return S_OK;
}

std::int32_t JobScheduler::IncreaseRetainedReservation(SharedRetainedMemoryLease& retained,
                                                       std::uint64_t totalBytes) noexcept
{
    if (!_impl)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    {
        WorkerResources::ExclusiveLock validationLock{self.lock};
        const auto valid = self.ValidateRetainedGrowth(retained, totalBytes);
        if (FAILED(valid) || totalBytes == retained._bytes)
            return valid;
    }
    MemorySnapshot snapshot;
    if (!self.SampleMemory(snapshot))
        return E_OUTOFMEMORY;
    // The test/OS sampler runs outside scheduler locks. It can reenter an owner action; revalidate exact
    // lease identity and lifetime before any accounting. The caller never grows while readers access it.
    WorkerResources::ExclusiveLock accountingLock{self.lock};
    const auto current = self.ValidateRetainedGrowth(retained, totalBytes);
    if (FAILED(current) || totalBytes == retained._bytes)
        return current;
    const auto delta = totalBytes - retained._bytes;
    const auto decision = self.memory.Reserve(delta, snapshot,
                                              self.releasingBytes.load(std::memory_order_acquire) +
                                                  (self.trimCount.load(std::memory_order_acquire) != 0 ? 1 : 0));
    if (decision.admission != MemoryAdmission::Ready)
        return E_OUTOFMEMORY;
    self.retainedMemory[retained._slot].bytes = totalBytes;
    self.retainedMemory[retained._slot].parts[0] = totalBytes;
    self.sharedMemory->residentBytes.fetch_add(delta, std::memory_order_relaxed);
    retained._bytes = totalBytes;
    return S_OK;
}

bool JobScheduler::ReleaseRetainedMemory(RetainedMemoryTicket ticket) noexcept
{
    if (!_impl || _impl->closed)
        return false;
    auto& self = *_impl;
    self.CheckOwner();
    if (ticket.owner == 0 || ticket.owner != self.retainedMemoryOwner || ticket.slot >= kRetainedMemoryCapacity)
        return false;
    auto& cache = self.retainedMemory[ticket.slot];
    if (cache.bytes == 0 || cache.shared || cache.generation != ticket.generation)
        return false;
    const bool released = self.memory.Release(cache.bytes);
    assert(released);
    if (!released)
        return false;
    cache.bytes = 0;
    NotifyMemoryChanged(); // one readiness transition; original admission waiters retry on the next outer drain
    return true;
}

void JobScheduler::NotifyMemoryChanged() noexcept
{
    if (_impl)
    {
        _impl->MemoryChanged();
    }
}

std::int32_t JobScheduler::Submit(std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket) noexcept
{
    ticket = {};
    if (!job || static_cast<unsigned int>(options.kind) >= static_cast<unsigned int>(JobClass::Count) ||
        static_cast<unsigned int>(options.executionLane) >= static_cast<unsigned int>(JobExecutionLane::Count) ||
        (options.continuingOutputLimit && (!options.deferredRetirement || !options.reserveRetainedMemory ||
                                           options.continuingOutputLimit >= options.memoryBytes)))
    {
        return E_INVALIDARG;
    }
    if (!_impl)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    auto& self = *_impl;
    self.CheckOwner();
    {
        WorkerResources::ExclusiveLock guard{self.lock};
        if (self.closing || self.nextSequence == (std::numeric_limits<std::uint64_t>::max)())
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        }
        if (options.executionLane == JobExecutionLane::GpuRecording && !self.recorders)
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        std::size_t slot = 0;
        for (; slot < self.records.size(); ++slot)
        {
            if (self.records[slot].state == Impl::State::Empty)
            {
                break;
            }
        }
        if (slot == self.records.size())
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        }
        std::size_t retainedSlot = kRetainedMemoryCapacity;
        if (options.reserveRetainedMemory)
        {
            if (!options.memoryBytes)
                return E_INVALIDARG;
            for (std::size_t index = 0; index < self.retainedMemory.size(); ++index)
            {
                const auto& cache = self.retainedMemory[index];
                if (!cache.bytes && !cache.reservedFor &&
                    cache.generation != (std::numeric_limits<std::uint64_t>::max)())
                {
                    retainedSlot = index;
                    break;
                }
            }
            if (retainedSlot == kRetainedMemoryCapacity)
                return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        }
        auto& record = self.records[slot];
        try
        {
            record.cancel.emplace();
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        record.owner = &self;
        record.options = options;
        record.reserved = 0;
        record.sequence = ++self.nextSequence;
        record.generation = record.sequence;
        record.retainedSlot = retainedSlot;
        record.continuingOutputTransferred = false;
        if (retainedSlot < kRetainedMemoryCapacity)
        {
            auto& cache = self.retainedMemory[retainedSlot];
            ++cache.generation;
            cache.reservedFor = record.sequence;
        }
        record.job = std::move(job);
        record.result = {};
        record.percent.store(0, std::memory_order_relaxed);
        record.progressQueued.store(false, std::memory_order_relaxed);
        record.publishedPercent = 0;
        record.publishedAt = 0;
        record.state = Impl::State::WaitingAdmission;
        ticket = {slot, record.generation};
        ++self.liveCount;
    }
    self.Notify(JobNoticeKind::Submitted, ticket, options.kind);
    self.AdmitWaiting();
    return S_OK;
}

bool JobScheduler::Cancel(JobTicket ticket) noexcept
{
    if (!_impl || ticket.slot >= kCapacity)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    std::optional<CancelSource> cancel;
    {
        WorkerResources::ExclusiveLock guard{self.lock};
        const auto& record = self.records[ticket.slot];
        if (self.closing || record.state == Impl::State::Empty || record.generation != ticket.generation)
        {
            return false;
        }
        cancel = record.cancel;
    }
    (void)cancel->Cancel();
    NotifyOwnerWork();
    self.AdmitWaiting();
    return true;
}

bool JobScheduler::Resume(JobTicket ticket) noexcept
{
    return _impl && ticket.slot < kCapacity && _impl->ResumeOwner(ticket, std::nullopt);
}

bool JobScheduler::Resume(JobTicket ticket, JobExecutionLane lane) noexcept
{
    return _impl && ticket.slot < kCapacity &&
           static_cast<unsigned int>(lane) < static_cast<unsigned int>(JobExecutionLane::Count) &&
           _impl->ResumeOwner(ticket, lane);
}

std::int32_t JobScheduler::IncreaseReservation(JobTicket ticket, std::uint64_t totalBytes) noexcept
{
    if (!_impl || ticket.slot >= kCapacity)
        return E_INVALIDARG;
    auto& self = *_impl;
    self.CheckOwner();
    {
        WorkerResources::ExclusiveLock validationLock{self.lock};
        const HRESULT status = self.ValidateReservationGrowth(ticket, totalBytes);
        if (FAILED(status) || totalBytes == self.records[ticket.slot].reserved)
            return status;
    }
    MemorySnapshot snapshot{};
    // The OS/test snapshot source is outside the scheduler lock, as in initial admission. Revalidate the
    // exact acknowledged stage before accounting; an intervening owner action cannot authorize new owners.
    if (!self.SampleMemory(snapshot))
        return E_OUTOFMEMORY;
    WorkerResources::ExclusiveLock accountingLock{self.lock};
    const HRESULT status = self.ValidateReservationGrowth(ticket, totalBytes);
    auto& record = self.records[ticket.slot];
    if (FAILED(status) || totalBytes == record.reserved)
        return status;
    const auto delta = totalBytes - record.reserved;
    const auto decision = self.memory.Reserve(delta, snapshot,
                                              self.releasingBytes.load(std::memory_order_acquire) +
                                                  (self.trimCount.load(std::memory_order_acquire) != 0 ? 1 : 0));
    if (decision.admission != MemoryAdmission::Ready)
        return E_OUTOFMEMORY;
    record.options.memoryBytes = totalBytes;
    record.reserved = totalBytes;
    return S_OK;
}

bool JobScheduler::RegisterOwnerWork(OwnerWorkPump& source) noexcept
{
    if (!_impl)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.workers.ShutdownToken().stop_requested() || self.ownerWork)
    {
        return false;
    }
    self.ownerWork = &source;
    self.ownerNotified.store(false, std::memory_order_relaxed);
    if (source.HasReady())
    {
        (void)SetEvent(self.wake.get());
    }
    return true;
}

bool JobScheduler::UnregisterOwnerWork(OwnerWorkPump& source) noexcept
{
    if (!_impl)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.ownerWork != &source || source.HasPending() || source.HasReady())
    {
        return false;
    }
    self.ownerWork = nullptr;
    self.ownerNotified.store(false, std::memory_order_relaxed);
    return true;
}

void JobScheduler::NotifyOwnerWork() noexcept
{
    if (_impl)
    {
        // The external producer lifetime excludes Close. Do not read owner-only registration/closing state.
        _impl->ownerNotified.store(true, std::memory_order_release);
        (void)SetEvent(_impl->wake.get());
    }
}

std::int32_t JobScheduler::RegisterEventWait(void* event, void (*callback)(void*) noexcept, void* context,
                                             JobEventTicket& ticket) noexcept
{
    ticket = {};
    if (!_impl)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.workers.ShutdownToken().stop_requested())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!self.waitsInitialized)
    {
        const HRESULT result = self.memoryWaits.Initialize();
        if (FAILED(result))
            return result;
        self.waitsInitialized = true;
    }
    WaitPool::Ticket wait;
    const HRESULT result = self.memoryWaits.Add(static_cast<HANDLE>(event), callback, context, wait);
    if (SUCCEEDED(result))
    {
        ticket = {wait.slot, wait.generation};
        self.eventGenerations[wait.slot] = wait.generation;
    }
    return result;
}

bool JobScheduler::RearmEventWait(JobEventTicket ticket, std::uint32_t timeoutMs) noexcept
{
    if (!_impl || _impl->closed || _impl->workers.ShutdownToken().stop_requested())
        return false;
    _impl->CheckOwner();
    if (ticket.slot >= _impl->eventGenerations.size() || ticket.generation == 0 ||
        _impl->eventGenerations[ticket.slot] != ticket.generation)
        return false;
    return _impl->memoryWaits.Rearm({ticket.slot, ticket.generation}, timeoutMs);
}

bool JobScheduler::RemoveEventWait(JobEventTicket ticket) noexcept
{
    if (!_impl)
        return false;
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed)
        return false;
    if (ticket.slot >= self.eventGenerations.size() || ticket.generation == 0 ||
        self.eventGenerations[ticket.slot] != ticket.generation)
        return false;
    self.eventGenerations[ticket.slot] = 0;
    self.memoryWaits.Remove({ticket.slot, ticket.generation});
    return true;
}

bool JobScheduler::PostUiAction(std::unique_ptr<UiAction>& action) noexcept
{
    if (!_impl || !action)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.closed || self.workers.ShutdownToken().stop_requested() || self.actionCount == kUiActionCapacity)
    {
        return false;
    }
    const std::size_t tail = (self.actionHead + self.actionCount) % kUiActionCapacity;
    self.actions[tail] = std::move(action);
    ++self.actionCount;
    (void)SetEvent(self.wake.get());
    return true;
}

bool JobScheduler::RegisterUiActionSource(UiAction& source) noexcept
{
    if (!_impl)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.workers.ShutdownToken().stop_requested() || self.actionSource)
    {
        return false;
    }
    self.actionSource = &source;
    self.sourceReady = false;
    self.preferSource = true;
    return true;
}

bool JobScheduler::RequestUiActionSource(UiAction& source) noexcept
{
    if (!_impl)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.closed || self.workers.ShutdownToken().stop_requested() || self.actionSource != &source)
    {
        return false;
    }
    if (!self.sourceReady)
    {
        self.sourceReady = true;
        (void)SetEvent(self.wake.get());
    }
    return true;
}

bool JobScheduler::UnregisterUiActionSource(UiAction& source) noexcept
{
    if (!_impl)
    {
        return false;
    }
    auto& self = *_impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || self.actionSource != &source)
    {
        return false;
    }
    self.actionSource = nullptr;
    self.sourceReady = false;
    return true;
}

void JobScheduler::RequestStop() noexcept
{
    if (!_impl)
    {
        return;
    }
    auto& self = *_impl;
    self.CheckOwner();
    {
        WorkerResources::ExclusiveLock guard{self.lock};
        self.closing = true;
    }
    if (self.shutdown)
    {
        (void)SetEvent(self.shutdown.get());
    }
    self.workers.RequestStop();
    if (self.recorders)
        self.recorders->RequestStop();
    self.sourceReady = false;
    self.resumesPending = false;
}

void JobScheduler::Close(CloseHooks hooks) noexcept
{
    if (!_impl)
    {
        return;
    }
    auto& self = *_impl;
    self.CheckOwner();
    assert(!self.inCallback);
    if (self.closed || self.inCallback)
    {
        return;
    }
    RequestStop();
    self.memoryWaits.Close();
    self.lowMemory.reset();
    self.highMemory.reset();
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct CloseState final
    {
        JobScheduler& owner;
        Impl& scheduler;
        CloseHooks hooks;
    }
#pragma warning(pop)
    closeState{*this, self, hooks};
    // Recorder callbacks borrow the same records and general executor. Join them before general-pool
    // teardown, job-wrapper destruction, owner cleanup and final worker graph retirement.
    if (self.recorders)
        self.recorders->Close();
    self.workers.Close({[](void* opaque) noexcept
                        {
                            auto& state = *static_cast<CloseState*>(opaque);
                            auto& stopped = state.scheduler;
                            stopped.CheckOwner();
                            (void)InterlockedFlushSList(&stopped.completions);
                            // Every compute/recorder callback joined. An explicit native read may remain
                            // owned through final-worker acknowledgement. Grant only terminal accounting,
                            // outside the scheduler lock and before resetting cancelled wrappers. Success-only
                            // capture/publication hooks remain unset and no useful callback is invoked.
                            stopped.inCallback = true;
                            for (std::size_t index = 0; index < stopped.records.size(); ++index)
                            {
                                Job* joined = nullptr;
                                JobTicket original{};
                                {
                                    WorkerResources::ExclusiveLock guard{stopped.lock};
                                    auto& record = stopped.records[index];
                                    if (record.job)
                                    {
                                        record.state = Impl::State::Completed;
                                        joined = record.job.get();
                                        original = {index, record.generation};
                                    }
                                }
                                if (!joined)
                                    continue;
                                stopped.retainedMemoryCallback = original;
                                stopped.inJoinedRetirementCallback = true;
                                joined->RetainJoinedRetirement(state.owner, original);
                                stopped.inJoinedRetirementCallback = false;
                                stopped.retainedMemoryCallback.reset();
                            }
                            stopped.inCallback = false;
                            std::array<std::unique_ptr<Job>, kCapacity> retired;
                            {
                                WorkerResources::ExclusiveLock guard{stopped.lock};
                                for (std::size_t index = 0; index < stopped.records.size(); ++index)
                                {
                                    auto& record = stopped.records[index];
                                    retired[index] = std::move(record.job);
                                    record.cancel.reset();
                                    record.state = Impl::State::Empty;
                                }
                            }
                            // Producer callbacks have joined. Owner-affine wrappers detach their CPU payloads through
                            // the still-installed sink, outside the scheduler lock and without applying completions.
                            for (auto& job : retired)
                                job.reset();
                            for (auto& action : stopped.actions)
                                action.reset();
                            if (state.hooks.owner)
                                state.hooks.owner(state.hooks.context);
                            if (stopped.releaseSinkInstalled)
                            {
                                (void)ClearDeferredReleaseSink(&stopped);
                                stopped.releaseSinkInstalled = false;
                            }
                        },
                        [](void* opaque) noexcept
                        {
                            auto& state = *static_cast<CloseState*>(opaque);
                            auto& stopped = state.scheduler;
                            if (state.hooks.worker)
                                state.hooks.worker(state.hooks.context);
                            stopped.DrainReleases();
                            stopped.TrimRecycle();
                            if (stopped.ownsRecycle)
                            {
                                ClosePixelBufferRecycle();
                                stopped.ownsRecycle = false;
                            }
                        },
                        &closeState});
    // Final CPU retirement is acknowledged while events, reservations and the pool apartment are alive.
    assert(self.releasingCount.load(std::memory_order_acquire) == 0);
    (void)self.memory.Release(self.memory.Reserved());
    for (auto& cache : self.retainedMemory)
    {
        cache.reservedFor = 0;
        cache.bytes = 0; // Close has joined producers and owner cleanup; every older ticket is now invalid.
    }
    self.sharedMemory.reset(); // Foreign leases keep their independent endpoint and owned wake alive.
    self.actionCount = 0;
    self.actionSource = nullptr;
    self.sourceReady = false;
    self.liveCount = 0;
    self.ownerWork = nullptr;
    self.ownerNotified.store(false, std::memory_order_relaxed);
    self.wake.reset();
    self.shutdown.reset();
    self.closed = true;
}

bool JobScheduler::IsIdle() const noexcept
{
    if (!_impl)
    {
        return true;
    }
    _impl->CheckOwner();
    return _impl->liveCount == 0 && _impl->actionCount == 0 && !_impl->sourceReady &&
           (!_impl->ownerWork || (!_impl->ownerWork->HasPending() && !_impl->ownerWork->HasReady())) &&
           _impl->releasingCount.load(std::memory_order_acquire) == 0 &&
           _impl->trimCount.load(std::memory_order_acquire) == 0 &&
           _impl->memoryRetirements.load(std::memory_order_acquire) == 0 && !HasUiWork();
}

unsigned int JobScheduler::WorkerCount() const noexcept
{
    if (!_impl)
    {
        return 0;
    }
    _impl->CheckOwner();
    return _impl->closed ? 0 : _impl->workers.Concurrency();
}

bool JobScheduler::HasUiWork() const noexcept
{
    if (!_impl || _impl->closed)
    {
        return false;
    }
    _impl->CheckOwner();
    return QueryDepthSList(&_impl->completions) != 0 || _impl->actionCount != 0 || _impl->sourceReady ||
           _impl->resumesPending ||
           (!_impl->workers.ShutdownToken().stop_requested() && _impl->ownerWork && _impl->ownerWork->HasReady()) ||
           _impl->memoryChanged.load(std::memory_order_acquire) ||
           (_impl->sharedMemory && _impl->sharedMemory->HasPending());
}

bool JobScheduler::InUiCallback() const noexcept
{
    if (!_impl)
    {
        return false;
    }
    _impl->CheckOwner();
    return _impl->inCallback;
}

bool JobScheduler::InJoinedRetirementCallback(JobTicket original) const noexcept
{
    if (!_impl || _impl->ownerThread != GetCurrentThreadId() || _impl->closed)
        return false;
    return _impl->inCallback && _impl->inJoinedRetirementCallback && _impl->retainedMemoryCallback == original &&
           !_impl->acknowledgedJobCallback && !_impl->publicationMemoryCallback;
}

bool JobScheduler::InAcknowledgedJobCallback(JobTicket original) const noexcept
{
    if (!_impl || _impl->closed)
        return false;
    _impl->CheckOwner();
    return _impl->inCallback && _impl->acknowledgedJobCallback == original;
}

JobScheduler::UiReadiness JobScheduler::UiReadinessForTest() const noexcept
{
    if (!_impl || _impl->closed)
        return {};
    _impl->CheckOwner();
    return {QueryDepthSList(&_impl->completions),
            static_cast<std::uint32_t>(_impl->actionCount),
            _impl->sourceReady,
            _impl->resumesPending,
            !_impl->workers.ShutdownToken().stop_requested() && _impl->ownerWork && _impl->ownerWork->HasReady(),
            _impl->memoryChanged.load(std::memory_order_acquire) ||
                (_impl->sharedMemory && _impl->sharedMemory->HasPending())};
}

void* JobScheduler::WakeHandle() const noexcept
{
    return _impl ? _impl->wake.get() : nullptr;
}

void JobContext::ReportProgress(unsigned int percent) const noexcept
{
    if (_report)
    {
        _report(_context, percent);
    }
}

MessagePumpResult PumpMessages(MessageDispatch dispatch, void* context) noexcept
{
    MessagePumpResult result;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            result.quit = true;
            result.exitCode = static_cast<int>(message.wParam);
            break;
        }
        if (dispatch)
        {
            const ScopedUiTiming timing{UiTimingKind::MessageDispatch, message.message};
            dispatch(context, &message);
        }
        else
        {
            const ScopedUiTiming timing{UiTimingKind::MessageDispatch, message.message};
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        ++result.dispatched;
    }
    return result;
}

std::size_t DrainCompletions(JobScheduler& scheduler, JobScheduler::Apply apply, void* context) noexcept
{
    if (!scheduler._impl)
    {
        return 0;
    }
    auto& self = *scheduler._impl;
    self.CheckOwner();
    if (self.inCallback || self.closed)
    {
        return 0;
    }
    const ScopedUiTiming timing{UiTimingKind::CompletionDrain};
    self.inCallback = true;
    const auto leave = wil::scope_exit([&]() noexcept { self.inCallback = false; });
    self.ProcessMemoryChange();
    self.PromoteResumed();
    // Consume before Pump so an event arriving during Pump wakes a subsequent outer pass. HasReady owns the
    // actual predicate; a notification without ready work never creates a polling loop.
    (void)self.ownerNotified.exchange(false, std::memory_order_acquire);
    if (!self.workers.ShutdownToken().stop_requested() && self.ownerWork && self.ownerWork->HasReady())
    {
        self.ownerWork->Pump();
    }
    auto* list = InterlockedFlushSList(&self.completions);
    SLIST_ENTRY* fifo = nullptr;
    while (list)
    {
        auto* next = list->Next;
        list->Next = fifo;
        fifo = list;
        list = next;
    }
    std::size_t completed = 0;
    while (fifo)
    {
        auto* node = reinterpret_cast<JobScheduler::Impl::Node*>(fifo);
        fifo = fifo->Next; // save before a progress node can be republished
        auto& record = *node->record;
        const JobTicket ticket{static_cast<std::size_t>(&record - self.records.data()), record.generation};
        if (!node->final)
        {
            const unsigned int percent = record.percent.load(std::memory_order_relaxed);
            record.progressQueued.store(false, std::memory_order_release);
            if (!record.cancel->Token().IsCancelled() && !self.workers.ShutdownToken().stop_requested())
            {
                record.job->Progress(percent);
                self.Notify(JobNoticeKind::Progress, ticket, record.options.kind, percent);
            }
            continue;
        }
        const UiTimingAttributionIdentity identity{ticket.slot, ticket.generation};
        const auto detail = static_cast<std::uint32_t>(record.options.kind);
        const ScopedUiTiming applyTiming{UiTimingKind::ResultApply, detail, identity};
        JobResult result = record.result;
        if (record.cancel->Token().IsCancelled() || self.workers.ShutdownToken().stop_requested())
        {
            result = {JobStatus::Cancelled};
        }
        if (record.options.deferredRetirement)
        {
            {
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::StateTransition, detail, identity};
                WorkerResources::ExclusiveLock guard{self.lock};
                record.state = JobScheduler::Impl::State::Suspended;
            }
            bool retirementDeferred = false;
            {
                if (result.status == JobStatus::Finished)
                    self.acknowledgedJobCallback = ticket;
                const auto clearAcknowledged =
                    wil::scope_exit([&]() noexcept { self.acknowledgedJobCallback.reset(); });
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::DeferRetirement, detail, identity};
                retirementDeferred = record.job->DeferRetirement(result, ticket);
            }
            if (retirementDeferred)
                continue;
            // Promotion is below this drain. A false acknowledgement hook cannot dispatch a resumed worker
            // before final apply/destruction, exactly as the successful-stage continuation contract requires.
            {
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::StateTransition, detail, identity};
                WorkerResources::ExclusiveLock guard{self.lock};
                record.state = JobScheduler::Impl::State::Completed;
            }
            if (record.cancel->Token().IsCancelled() || self.workers.ShutdownToken().stop_requested())
                result = {JobStatus::Cancelled};
        }
        if (result.status == JobStatus::Finished)
        {
            {
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::StateTransition, detail, identity};
                WorkerResources::ExclusiveLock guard{self.lock};
                record.state = JobScheduler::Impl::State::Suspended;
            }
            bool deferred = false;
            {
                self.acknowledgedJobCallback = ticket;
                const auto clearAcknowledged =
                    wil::scope_exit([&]() noexcept { self.acknowledgedJobCallback.reset(); });
                self.publicationMemoryCallback = ticket;
                const auto clearPublication =
                    wil::scope_exit([&]() noexcept { self.publicationMemoryCallback.reset(); });
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::DeferCompletion, detail, identity};
                deferred = record.job->DeferCompletion(result, ticket);
            }
            if (deferred)
                continue;
            // A hook returning false retires even if it requested Resume. Resuming is not runnable until
            // promotion below, so payload destruction cannot race a worker stage.
            {
                const ScopedUiTimingAttribution phaseTiming{
                    UiTimingKind::ResultApply, UiTimingAttributionPhase::StateTransition, detail, identity};
                WorkerResources::ExclusiveLock guard{self.lock};
                record.state = JobScheduler::Impl::State::Completed;
            }
            if (record.cancel->Token().IsCancelled() || self.workers.ShutdownToken().stop_requested())
                result = {JobStatus::Cancelled};
        }
        {
            const ScopedUiTimingAttribution phaseTiming{UiTimingKind::ResultApply,
                                                        UiTimingAttributionPhase::CompletedCallback, detail, identity};
            self.retainedMemoryCallback = ticket;
            const auto endTransfer = wil::scope_exit([&]() noexcept { self.retainedMemoryCallback.reset(); });
            if (apply)
            {
                apply(context, *record.job, result);
            }
            else
            {
                record.job->Completed(result);
            }
        }
        {
            const ScopedUiTimingAttribution phaseTiming{UiTimingKind::ResultApply,
                                                        UiTimingAttributionPhase::PayloadDestruction, detail, identity};
            record.job.reset();
        }
        {
            const ScopedUiTimingAttribution phaseTiming{
                UiTimingKind::ResultApply, UiTimingAttributionPhase::CancellationSourceDestruction, detail, identity};
            record.cancel.reset();
        }
        {
            const ScopedUiTimingAttribution phaseTiming{UiTimingKind::ResultApply,
                                                        UiTimingAttributionPhase::MemoryRefund, detail, identity};
            const bool released = self.memory.Release(record.reserved);
            assert(released);
            (void)released;
            record.reserved = 0;
            if (record.retainedSlot < JobScheduler::kRetainedMemoryCapacity)
            {
                auto& cache = self.retainedMemory[record.retainedSlot];
                if (cache.reservedFor != record.sequence || cache.bytes)
                    std::terminate();
                cache.reservedFor = 0;
                record.retainedSlot = JobScheduler::kRetainedMemoryCapacity;
            }
        }
        {
            const ScopedUiTimingAttribution phaseTiming{UiTimingKind::ResultApply,
                                                        UiTimingAttributionPhase::StateTransition, detail, identity};
            WorkerResources::ExclusiveLock guard{self.lock};
            record.state = JobScheduler::Impl::State::Empty;
        }
        --self.liveCount;
        ++completed;
        {
            const ScopedUiTimingAttribution phaseTiming{UiTimingKind::ResultApply,
                                                        UiTimingAttributionPhase::RetirementNotice, detail, identity};
            self.Notify(JobNoticeKind::Retired, ticket, record.options.kind);
        }
    }
    if (completed != 0)
    {
        self.AdmitWaiting();
    }
    self.PromoteResumed();
    return completed;
}

bool RunUiActions(JobScheduler& scheduler) noexcept
{
    if (!scheduler._impl)
    {
        return false;
    }
    auto& self = *scheduler._impl;
    self.CheckOwner();
    if (self.inCallback || self.closed || (self.actionCount == 0 && !self.sourceReady))
    {
        return false;
    }
    const ScopedUiTiming timing{UiTimingKind::UiAction};
    const bool runSource = self.sourceReady && (self.actionCount == 0 || self.preferSource);
    // Declared before owned: when engaged below, destruction includes the original leave guard and payload.
    std::optional<ScopedUiTimingAttribution> destruction;
    std::unique_ptr<UiAction> owned;
    UiAction* action = nullptr;
    if (runSource)
    {
        action = self.actionSource;
        self.sourceReady = false;
        self.preferSource = false;
    }
    else
    {
        owned = std::move(self.actions[self.actionHead]);
        action = owned.get();
        self.actionHead = (self.actionHead + 1) % JobScheduler::kUiActionCapacity;
        --self.actionCount;
        self.preferSource = true;
    }
    self.inCallback = true;
    const auto leave = wil::scope_exit([&]() noexcept { self.inCallback = false; });
    {
        const ScopedUiTimingAttribution attribution{UiTimingKind::UiAction, UiTimingAttributionPhase::ActionRun};
        action->Run();
    }
    destruction.emplace(UiTimingKind::UiAction, UiTimingAttributionPhase::ActionDestruction, 0,
                        UiTimingAttributionIdentity{}, std::source_location::current());
    return true;
}

SettleResult SettleJobs(JobScheduler& scheduler, std::uint32_t timeoutMs, MessageDispatch dispatch, void* context,
                        JobScheduler::Apply apply, void* applyContext) noexcept
{
    if (scheduler.InUiCallback())
    {
        return SettleResult::Failed;
    }
    const std::uint64_t start = GetTickCount64();
    for (;;)
    {
        const auto messages = PumpMessages(dispatch, context);
        if (messages.quit)
        {
            PostQuitMessage(messages.exitCode); // preserve the outer Run loop's exit
            return SettleResult::Quit;
        }
        (void)DrainCompletions(scheduler, apply, applyContext);
        (void)RunUiActions(scheduler);
        if (scheduler.IsIdle())
        {
            return SettleResult::Settled;
        }
        const std::uint64_t elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs)
        {
            return SettleResult::TimedOut;
        }
        if (scheduler.HasUiWork())
        {
            continue;
        }
        HANDLE wake = scheduler.WakeHandle();
        if (!wake)
        {
            return SettleResult::Failed;
        }
        const DWORD waited = MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(timeoutMs - elapsed), QS_ALLINPUT,
                                                         MWMO_INPUTAVAILABLE);
        if (waited == WAIT_TIMEOUT)
        {
            return SettleResult::TimedOut;
        }
        if (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1)
        {
            return SettleResult::Failed;
        }
    }
}

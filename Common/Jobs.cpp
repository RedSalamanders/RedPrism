#include "Jobs.h"

#include "WorkerPool.h"

#include <array>
#include <cassert>
#include <limits>
#include <new>
#include <optional>

namespace
{
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
struct JobScheduler::Impl final
{
    enum class State
    {
        Empty,
        Queued,
        Running,
        Completed
    };
    struct Record;
    struct alignas(MEMORY_ALLOCATION_ALIGNMENT) Node final
    {
        SLIST_ENTRY link{};
        Record* record = nullptr;
        bool final = false;
    };
    static_assert(offsetof(Node, link) == 0);
    struct Record final
    {
        // State, sequence and slot reuse are protected by Impl::lock. The payload/options/cancel source are
        // immutable from admission through final publication. Only the owner drains/destroys a published job.
        State state = State::Empty;
        std::uint64_t generation = 0;
        std::uint64_t sequence = 0;
        JobOptions options{};
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

    Impl()
    {
        InitializeSListHead(&completions);
    }

    DWORD ownerThread = GetCurrentThreadId();
    SRWLOCK lock = SRWLOCK_INIT;
    _Guarded_by_(lock) bool closing = false;
    _Guarded_by_(lock) std::array<Record, JobScheduler::kCapacity> records {};
    std::uint64_t nextSequence = 0; // owner thread
    std::size_t liveCount = 0;      // owner thread, counts queued/running/undrained
    bool inCallback = false;        // owner thread, guards drain and actions across nested message loops
    bool closed = false;
    std::array<std::unique_ptr<UiAction>, JobScheduler::kUiActionCapacity> actions;
    std::size_t actionHead = 0;
    std::size_t actionCount = 0;
    alignas(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER completions{};
    wil::unique_handle wake;
    wil::unique_handle shutdown;
    WorkerPool workers;

    void CheckOwner() const noexcept
    {
        assert(ownerThread == GetCurrentThreadId());
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

    Record* Take(WorkPriority priority) noexcept
    {
        WorkerResources::ExclusiveLock guard{lock};
        if (closing)
        {
            return nullptr;
        }
        Record* selected = nullptr;
        for (auto& record : records)
        {
            if (record.state == State::Queued && Priority(record.options.kind) == priority &&
                (!selected || record.sequence < selected->sequence) && !Blocked(record))
            {
                selected = &record;
            }
        }
        if (selected)
        {
            selected->state = State::Running;
        }
        return selected;
    }

    static void Dispatch(void* context, WorkPriority priority, CancelToken) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        while (Record* record = self.Take(priority))
        {
            JobContext execution{self.workers, record->cancel->Token().WithShutdown(self.workers.ShutdownToken()),
                                 self.shutdown.get(), Report, record};
            JobResult result{JobStatus::Cancelled};
            if (!execution.cancellation.IsCancelled())
            {
                try
                {
                    result = record->job->Run(execution);
                }
                catch (const std::bad_alloc&)
                {
                    result = {JobStatus::Failed, E_OUTOFMEMORY};
                }
            }
            if (execution.cancellation.IsCancelled())
            {
                result = {JobStatus::Cancelled};
            }
            {
                WorkerResources::ExclusiveLock guard{self.lock};
                record->result = result;
                record->state = State::Completed;
            }
            // No access to record after this call. A draining UI may immediately destroy/reuse the slot.
            self.Publish(record->final);
        }
    }

    void WakeQueued() noexcept
    {
        std::array<bool, 3> pending{};
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
                    pending[static_cast<std::size_t>(Priority(record.options.kind))] = true;
                }
            }
        }
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            if (pending[index])
            {
                (void)workers.Wake(static_cast<WorkPriority>(index));
            }
        }
    }
};
#pragma warning(pop)

JobScheduler::JobScheduler() noexcept = default;
JobScheduler::~JobScheduler()
{
    Close();
}

std::int32_t JobScheduler::Initialize(unsigned int threads) noexcept
{
    if (_impl)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    try
    {
        _impl = std::make_unique<Impl>();
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
    const HRESULT result = _impl->workers.Initialize(threads, Impl::Dispatch, _impl.get());
    if (FAILED(result))
    {
        Close();
    }
    return result;
}

std::int32_t JobScheduler::Submit(std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket) noexcept
{
    ticket = {};
    if (!job || static_cast<unsigned int>(options.kind) >= static_cast<unsigned int>(JobClass::Count))
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
        record.sequence = ++self.nextSequence;
        record.generation = record.sequence;
        record.job = std::move(job);
        record.result = {};
        record.percent.store(0, std::memory_order_relaxed);
        record.progressQueued.store(false, std::memory_order_relaxed);
        record.publishedPercent = 0;
        record.publishedAt = 0;
        record.state = Impl::State::Queued;
        ticket = {slot, record.generation};
        ++self.liveCount;
    }
    (void)self.workers.Wake(Priority(options.kind));
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
    self.WakeQueued();
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
}

void JobScheduler::Close() noexcept
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
    self.workers.Close();
    (void)InterlockedFlushSList(&self.completions);
    std::array<std::unique_ptr<Job>, kCapacity> retired;
    {
        WorkerResources::ExclusiveLock guard{self.lock};
        for (std::size_t index = 0; index < self.records.size(); ++index)
        {
            auto& record = self.records[index];
            retired[index] = std::move(record.job);
            record.cancel.reset();
            record.state = Impl::State::Empty;
        }
    }
    // No producer remains. Payload destruction deliberately occurs without the scheduler lock/UI callback.
    // Destroy before closing handles: a payload may own a subordinate cancellation-registration lifetime.
    for (auto& job : retired)
    {
        job.reset();
    }
    for (auto& action : self.actions)
    {
        action.reset();
    }
    self.actionCount = 0;
    self.liveCount = 0;
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
    return _impl->liveCount == 0 && _impl->actionCount == 0;
}

bool JobScheduler::HasUiWork() const noexcept
{
    if (!_impl)
    {
        return false;
    }
    _impl->CheckOwner();
    return QueryDepthSList(&_impl->completions) != 0 || _impl->actionCount != 0;
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
            dispatch(context, &message);
        }
        else
        {
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
    self.inCallback = true;
    const auto leave = wil::scope_exit([&]() noexcept { self.inCallback = false; });
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
        if (!node->final)
        {
            const unsigned int percent = record.percent.load(std::memory_order_relaxed);
            record.progressQueued.store(false, std::memory_order_release);
            if (!record.cancel->Token().IsCancelled() && !self.workers.ShutdownToken().stop_requested())
            {
                record.job->Progress(percent);
            }
            continue;
        }
        JobResult result = record.result;
        if (record.cancel->Token().IsCancelled() || self.workers.ShutdownToken().stop_requested())
        {
            result = {JobStatus::Cancelled};
        }
        if (apply)
        {
            apply(context, *record.job, result);
        }
        else
        {
            record.job->Completed(result);
        }
        record.job.reset();
        record.cancel.reset();
        {
            WorkerResources::ExclusiveLock guard{self.lock};
            record.state = JobScheduler::Impl::State::Empty;
        }
        --self.liveCount;
        ++completed;
    }
    self.WakeQueued();
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
    if (self.inCallback || self.closed || self.actionCount == 0)
    {
        return false;
    }
    auto action = std::move(self.actions[self.actionHead]);
    self.actionHead = (self.actionHead + 1) % JobScheduler::kUiActionCapacity;
    --self.actionCount;
    self.inCallback = true;
    const auto leave = wil::scope_exit([&]() noexcept { self.inCallback = false; });
    action->Run();
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

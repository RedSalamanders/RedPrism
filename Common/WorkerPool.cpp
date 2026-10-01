#include "WorkerPool.h"

#include "CrashDumps.h"

#include <algorithm>
#include <cassert>

#pragma comment(lib, "ole32.lib")

namespace
{
thread_local WorkerPool* g_currentPool = nullptr;
thread_local WorkPriority g_currentPriority = WorkPriority::Normal;
thread_local const wchar_t* g_threadName = nullptr;

// These are private pool threads. Restore system-managed QoS on callback exit; nested parallel calls inherit
// the surrounding callback's priority and do not alter the caller's apartment or QoS.
class CallbackScope final
{
  public:
    CallbackScope(WorkerPool* pool, WorkPriority priority, const wchar_t* name) noexcept
        : _previousPool(g_currentPool), _previousPriority(g_currentPriority)
    {
        CrashDumps::InstallWorkerTerminateHandler();
        g_currentPool = pool;
        g_currentPriority = priority;
        if (g_threadName != name)
        {
            (void)SetThreadDescription(GetCurrentThread(), name);
            g_threadName = name;
        }
        THREAD_POWER_THROTTLING_STATE state{
            THREAD_POWER_THROTTLING_CURRENT_VERSION, THREAD_POWER_THROTTLING_EXECUTION_SPEED,
            priority == WorkPriority::Background ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0u};
        (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
    }
    ~CallbackScope()
    {
        THREAD_POWER_THROTTLING_STATE state{THREAD_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
        (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
        g_currentPool = _previousPool;
        g_currentPriority = _previousPriority;
    }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

  private:
    WorkerPool* _previousPool;
    WorkPriority _previousPriority;
};

TP_CALLBACK_PRIORITY NativePriority(WorkPriority priority) noexcept
{
    switch (priority)
    {
    case WorkPriority::High:
        return TP_CALLBACK_PRIORITY_HIGH;
    case WorkPriority::Background:
        return TP_CALLBACK_PRIORITY_LOW;
    default:
        return TP_CALLBACK_PRIORITY_NORMAL;
    }
}

struct ParallelCall final
{
    WorkerPool* pool;
    void* context;
    Executor::Participant run;
    WorkPriority priority;
};

HRESULT InitializePool(unsigned int threads, WorkerResources::UniquePool& pool,
                       WorkerResources::UniqueCleanupGroup& cleanup, wil::unique_mta_usage_cookie& mta) noexcept
{
    const HRESULT com = CoIncrementMTAUsage(mta.put());
    if (FAILED(com))
    {
        return com;
    }
    pool.reset(CreateThreadpool(nullptr));
    if (!pool)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    SetThreadpoolThreadMaximum(pool.get(), threads);
    if (!SetThreadpoolThreadMinimum(pool.get(), threads))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    cleanup.reset(CreateThreadpoolCleanupGroup());
    return cleanup ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
} // namespace

WorkerPool::~WorkerPool()
{
    Close();
}

HRESULT WorkerPool::Initialize(unsigned int threads, Dispatch dispatch, void* context) noexcept
{
    if (_pool || _stop.stop_requested())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    if (threads == 0)
    {
        threads = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    }
    if (threads == 0 || threads > 1024)
    {
        return E_INVALIDARG;
    }
    _threadCount = threads;
    _dispatch = dispatch;
    _context = context;
    HRESULT result = InitializePool(threads, _pool, _cleanup, _mta);
    if (SUCCEEDED(result))
    {
        for (std::size_t index = 0; index < _lanes.size(); ++index)
        {
            auto& lane = _lanes[index];
            lane.owner = this;
            lane.priority = static_cast<WorkPriority>(index);
            TP_CALLBACK_ENVIRON environment;
            InitializeThreadpoolEnvironment(&environment);
            SetThreadpoolCallbackPool(&environment, _pool.get());
            SetThreadpoolCallbackCleanupGroup(&environment, _cleanup.get(), nullptr);
            SetThreadpoolCallbackPriority(&environment, NativePriority(lane.priority));
            lane.work = CreateThreadpoolWork(DispatchCallback, &lane, &environment);
            const DWORD error = lane.work ? ERROR_SUCCESS : GetLastError();
            DestroyThreadpoolEnvironment(&environment);
            if (!lane.work)
            {
                result = HRESULT_FROM_WIN32(error);
                break;
            }
        }
    }
    if (FAILED(result))
    {
        Close();
    }
    return result;
}

bool WorkerPool::Wake(WorkPriority priority) noexcept
{
    const auto index = static_cast<std::size_t>(priority);
    WorkerResources::ExclusiveLock lock{_lock};
    if (_closing || _stop.stop_requested() || !_pool || !_dispatch || index >= _lanes.size())
    {
        return false;
    }
    // Coalesce redundant wakeups. The scheduler callback drains its own bounded lane queue; the Windows
    // threadpool never becomes an unbounded second job queue.
    if (_pending[index] < _threadCount)
    {
        ++_pending[index];
        SubmitThreadpoolWork(_lanes[index].work);
    }
    return true;
}

void CALLBACK WorkerPool::DispatchCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept
{
    const auto& lane = *static_cast<Lane*>(context);
    auto& pool = *lane.owner;
    {
        WorkerResources::ExclusiveLock lock{pool._lock};
        --pool._pending[static_cast<std::size_t>(lane.priority)];
    }
    CallbackScope scope{&pool, lane.priority, L"RedPrism.Workers"};
    if (!pool._stop.stop_requested())
    {
        pool._dispatch(pool._context, lane.priority, CancelToken{pool._stop.get_token()});
    }
}

void WorkerPool::RequestStop() noexcept
{
    (void)_stop.request_stop();
}

void WorkerPool::Close() noexcept
{
    assert(g_currentPool != this);
    RequestStop();
    {
        WorkerResources::ExclusiveLock lock{_lock};
        _closing = true;
    }
    if (_cleanup)
    {
        // Dispatch work is owned by the cleanup group. Parallel helper work is owned by its calling frame,
        // not by this group: a group close must never race that frame's cancel/join/close.
        CloseThreadpoolCleanupGroupMembers(_cleanup.get(), TRUE, nullptr);
    }
    {
        WorkerResources::ExclusiveLock lock{_lock};
        while (_parallelCalls != 0)
        {
            (void)SleepConditionVariableSRW(&_parallelDone, &_lock, INFINITE, 0);
        }
        _pending.fill(0);
        for (auto& lane : _lanes)
        {
            lane.work = nullptr;
        }
        _cleanup.reset();
        _pool.reset();
    }
    _mta.reset();
}

void CALLBACK WorkerPool::ParallelCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept
{
    const auto& call = *static_cast<ParallelCall*>(context);
    CallbackScope scope{call.pool, call.priority, L"RedPrism.Workers"};
    call.run(call.context);
}

void WorkerPool::FinishParallel() noexcept
{
    WorkerResources::ExclusiveLock lock{_lock};
    --_parallelCalls;
    if (_parallelCalls == 0)
    {
        WakeAllConditionVariable(&_parallelDone);
    }
}

void WorkerPool::RunParticipants(void* context, Participant run, unsigned int participants) noexcept
{
    {
        WorkerResources::ExclusiveLock lock{_lock};
        if (_closing || !_pool || _stop.stop_requested())
        {
            return;
        }
        ++_parallelCalls;
    }
    const auto finished = wil::scope_exit([this] { FinishParallel(); });
    participants = (std::min)(participants, _threadCount);
    if (participants <= 1)
    {
        run(context);
        return;
    }
    ParallelCall call{this, context, run, g_currentPool == this ? g_currentPriority : WorkPriority::Normal};
    TP_CALLBACK_ENVIRON environment;
    InitializeThreadpoolEnvironment(&environment);
    SetThreadpoolCallbackPool(&environment, _pool.get());
    SetThreadpoolCallbackPriority(&environment, NativePriority(call.priority));
    wil::unique_threadpool_work helper{CreateThreadpoolWork(ParallelCallback, &call, &environment)};
    DestroyThreadpoolEnvironment(&environment);
    if (helper)
    {
        for (unsigned int index = 1; index < participants; ++index)
        {
            SubmitThreadpoolWork(helper.get());
        }
    }
    run(context); // caller participates even if helper allocation failed or every worker is occupied
    // WIL cancels unstarted callbacks, waits for running callbacks, then closes the work object. Nothing is
    // disassociated. Therefore neither call nor the caller's frame can be accessed after this join returns.
    helper.reset();
}

WaitPool::~WaitPool()
{
    Close();
}

_Use_decl_annotations_ HRESULT WaitPool::Initialize() noexcept
{
    if (_pool || _closing)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    const HRESULT result = InitializePool(1, _pool, _cleanup, _mta);
    if (FAILED(result))
    {
        Close();
    }
    return result;
}

_Use_decl_annotations_ bool WaitPool::Matches(Ticket ticket) const noexcept
{
    return ticket.slot < _slots.size() && _slots[ticket.slot].wait && !_slots[ticket.slot].retiring &&
           _slots[ticket.slot].generation == ticket.generation;
}

HRESULT WaitPool::Add(HANDLE event, Callback callback, void* context, Ticket& ticket) noexcept
{
    ticket = {};
    if (!event || event == INVALID_HANDLE_VALUE || !callback)
    {
        return E_INVALIDARG;
    }
    WorkerResources::ExclusiveLock lock{_lock};
    if (_closing || !_pool)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    for (std::size_t index = 0; index < _slots.size(); ++index)
    {
        auto& slot = _slots[index];
        if (slot.wait)
        {
            continue;
        }
        TP_CALLBACK_ENVIRON environment;
        InitializeThreadpoolEnvironment(&environment);
        SetThreadpoolCallbackPool(&environment, _pool.get());
        SetThreadpoolCallbackCleanupGroup(&environment, _cleanup.get(), nullptr);
        slot.owner = this;
        slot.event = event;
        slot.callback = callback;
        slot.context = context;
        slot.retiring = false;
        slot.wait = CreateThreadpoolWait(WaitCallback, &slot, &environment);
        const DWORD error = slot.wait ? ERROR_SUCCESS : GetLastError();
        DestroyThreadpoolEnvironment(&environment);
        if (!slot.wait)
        {
            return HRESULT_FROM_WIN32(error);
        }
        ticket = {index, ++slot.generation};
        SetThreadpoolWait(slot.wait, event, nullptr);
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

bool WaitPool::Rearm(Ticket ticket, std::uint32_t timeoutMs) noexcept
{
    WorkerResources::ExclusiveLock lock{_lock};
    if (_closing || !Matches(ticket))
    {
        return false;
    }
    auto& slot = _slots[ticket.slot];
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(timeoutMs) * 10'000;
    FILETIME deadline{due.LowPart, static_cast<DWORD>(due.HighPart)};
    SetThreadpoolWait(slot.wait, slot.event, timeoutMs ? &deadline : nullptr);
    return true;
}

void CALLBACK WaitPool::WaitCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) noexcept
{
    auto& slot = *static_cast<Slot*>(context);
    Callback callback = nullptr;
    void* target = nullptr;
    {
        WorkerResources::ExclusiveLock lock{slot.owner->_lock};
        if (!slot.retiring && !slot.owner->_closing)
        {
            callback = slot.callback;
            target = slot.context;
        }
    }
    CallbackScope scope{nullptr, WorkPriority::Normal, L"RedPrism.Waits"};
    if (callback)
    {
        callback(target);
    }
}

void WaitPool::Remove(Ticket ticket) noexcept
{
    PTP_WAIT wait = nullptr;
    {
        WorkerResources::ExclusiveLock lock{_lock};
        if (_closing || !Matches(ticket))
        {
            return;
        }
        auto& slot = _slots[ticket.slot];
        slot.retiring = true;
        wait = slot.wait;
        SetThreadpoolWait(wait, nullptr, nullptr);
    }
    // Removes this member from the cleanup group too. Must finish before its borrowed handle/context is freed.
    wil::unique_threadpool_wait removed{wait};
    removed.reset();
    WorkerResources::ExclusiveLock lock{_lock};
    auto& slot = _slots[ticket.slot];
    slot.wait = nullptr;
    slot.context = nullptr;
    slot.callback = nullptr;
    slot.event = nullptr;
}

void WaitPool::Close() noexcept
{
    {
        WorkerResources::ExclusiveLock lock{_lock};
        _closing = true;
        for (auto& slot : _slots)
        {
            if (slot.wait)
            {
                SetThreadpoolWait(slot.wait, nullptr, nullptr);
            }
        }
    }
    if (_cleanup)
    {
        CloseThreadpoolCleanupGroupMembers(_cleanup.get(), TRUE, nullptr);
    }
    WorkerResources::ExclusiveLock lock{_lock};
    for (auto& slot : _slots)
    {
        slot.wait = nullptr;
        slot.context = nullptr;
    }
    _cleanup.reset();
    _pool.reset();
    _mta.reset();
}

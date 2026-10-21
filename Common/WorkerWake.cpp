#include "WorkerWake.h"
#include "CrashDumps.h"

#include <array>
#include <cassert>

namespace WorkerResources
{
WorkerWake::~WorkerWake()
{
    Close();
}

HRESULT WorkerWake::Initialize(PTP_POOL pool, std::uint32_t capacity, Dispatch dispatch, void* context,
                               Observer observer) noexcept
{
    // Reinitialization is deliberately unsupported: no generation or callback can alias another endpoint.
    if (_ownerThread != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    _ownerThread = GetCurrentThreadId();
    if (!pool || !dispatch || capacity == 0 || capacity > OwnerWakeCounts::kMaximumCapacity)
        return E_INVALIDARG;
    _counts.emplace(capacity);
    _dispatch = dispatch;
    _context = context;
    _observer = observer;
    _event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!_event)
    {
        const DWORD error = GetLastError();
        Close();
        return HRESULT_FROM_WIN32(error);
    }
    TP_CALLBACK_ENVIRON environment;
    InitializeThreadpoolEnvironment(&environment);
    const auto destroyEnvironment = wil::scope_exit([&]() noexcept { DestroyThreadpoolEnvironment(&environment); });
    SetThreadpoolCallbackPool(&environment, pool);
    SetThreadpoolCallbackPriority(&environment, TP_CALLBACK_PRIORITY_HIGH);
    // Owned separately from the parent's dispatch cleanup group. Broker Close joins it first.
    _wait.reset(CreateThreadpoolWait(Callback, this, &environment));
    if (!_wait)
    {
        const DWORD error = GetLastError();
        Close();
        return HRESULT_FROM_WIN32(error);
    }
    SetThreadpoolWait(_wait.get(), _event.get(), nullptr);
    _open = true;
    return S_OK;
}

HRESULT WorkerWake::Request(WakeLane channel) noexcept
{
    if (_ownerThread != GetCurrentThreadId())
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    if (!_open)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!_counts.value().Request(channel))
        return E_INVALIDARG;
    // Always signal, including saturation. Real work has already been published by the caller's protocol;
    // relaxed counter operations do not publish it. A failure must fail the caller operation or tear down.
    if (!SetEvent(_event.get()))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

void WorkerWake::Observe(Phase phase) noexcept
{
    if (_observer.checkpoint)
        _observer.checkpoint(_observer.context, phase);
}

void CALLBACK WorkerWake::Callback(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) noexcept
{
    CrashDumps::InstallWorkerTerminateHandler();
    auto& self = *static_cast<WorkerWake*>(context);
    if (self._stopping.load(std::memory_order_acquire))
        return;
    self.Observe(Phase::Entered);
    std::array<std::uint32_t, OwnerWakeCounts::kChannelCount> taken{};
    for (std::uint32_t index = 0; index < taken.size(); ++index)
        taken[index] = self._counts.value().Take(static_cast<WakeLane>(index));
    self.Observe(Phase::CountsTaken);
    for (std::uint32_t index = 0; index < taken.size(); ++index)
        if (taken[index] != 0)
            self._dispatch(self._context, static_cast<WakeLane>(index), taken[index]);
    self.Observe(Phase::BeforeRearm);
    // Serialize rearm with Close's disarm. Close cannot disarm and then lose that decision to a late rearm.
    const auto lock = wil::AcquireSRWLockExclusive(&self._rearmLock);
    self.Observe(Phase::RearmLocked);
    if (!self._stopping.load(std::memory_order_acquire))
        SetThreadpoolWait(self._wait.get(), self._event.get(), nullptr);
}

void WorkerWake::Close() noexcept
{
    if (_ownerThread == 0)
        return;
    assert(_ownerThread == GetCurrentThreadId());
    _open = false;
    if (_wait)
    {
        {
            const auto lock = wil::AcquireSRWLockExclusive(&_rearmLock);
            _stopping.store(true, std::memory_order_release);
            SetThreadpoolWait(_wait.get(), nullptr, nullptr);
        }
        // Lifetime join only. Request and normal callbacks never wait for completion on the owner.
        WaitForThreadpoolWaitCallbacks(_wait.get(), FALSE);
        _wait.reset();
    }
    else
        _stopping.store(true, std::memory_order_release);
    _event.reset();
    _counts.reset();
    _dispatch = nullptr;
    _context = nullptr;
    _observer = {};
}
} // namespace WorkerResources

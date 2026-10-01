#pragma once

#include "Executor.h"

#include <array>
#include <combaseapi.h>
#include <windows.h>

#include <wil/resource.h>

namespace WorkerResources
{
using UniquePool = wil::unique_any<PTP_POOL, decltype(&CloseThreadpool), CloseThreadpool>;
using UniqueCleanupGroup =
    wil::unique_any<PTP_CLEANUP_GROUP, decltype(&CloseThreadpoolCleanupGroup), CloseThreadpoolCleanupGroup>;

// The pinned WIL guard has no concurrency SAL annotations. Describe its real acquire/release effects here,
// retaining WIL ownership. The analysis gate also compiles a deliberate unguarded access as a negative control.
class ExclusiveLock final
{
  public:
    _Acquires_exclusive_lock_(lock) _Post_same_lock_(_lock, lock) explicit ExclusiveLock(SRWLOCK& lock) noexcept
        : _lock(lock), _owner(wil::AcquireSRWLockExclusive(&lock))
    {
        _Analysis_assume_lock_acquired_(lock);
    }
    _Releases_exclusive_lock_(_lock) ~ExclusiveLock() noexcept
    {
        _owner.reset();
        _Analysis_assume_lock_released_(_lock);
    }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;

  private:
    SRWLOCK& _lock;
    wil::rwlock_release_exclusive_scope_exit _owner;
};
} // namespace WorkerResources

enum class WorkPriority : unsigned int
{
    High,
    Normal,
    Background,
    Count
};

// Owner-thread Initialize/Close; Wake, RequestStop and parallel execution are thread-safe. Close must not be
// called from a callback. Callbacks borrow the owner's context until Close returns and carry no queued payload.
class WorkerPool final : public Executor
{
  public:
    using Dispatch = void (*)(void*, WorkPriority, CancelToken) noexcept;
    WorkerPool() = default;
    ~WorkerPool() override;
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    [[nodiscard]] HRESULT Initialize(unsigned int threads = 0, Dispatch dispatch = nullptr,
                                     void* context = nullptr) noexcept;
    [[nodiscard]] bool Wake(WorkPriority priority) noexcept;
    void RequestStop() noexcept;
    void Close() noexcept;
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return _threadCount;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return _stop.get_token();
    }
    void RunParticipants(void* context, Participant run, unsigned int participants) noexcept override;

  private:
    struct Lane final
    {
        WorkerPool* owner = nullptr;
        WorkPriority priority = WorkPriority::Normal;
        PTP_WORK work = nullptr; // owned by _cleanup; not a queued payload
    };
    static void CALLBACK DispatchCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept;
    static void CALLBACK ParallelCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept;
    void FinishParallel() noexcept;

    SRWLOCK _lock = SRWLOCK_INIT;
    CONDITION_VARIABLE _parallelDone = CONDITION_VARIABLE_INIT;
    _Guarded_by_(_lock) bool _closing = false;
    _Guarded_by_(_lock) std::size_t _parallelCalls = 0;
    _Guarded_by_(_lock) std::array<unsigned int, 3> _pending {};
    // Initialized before publication; retained through all callbacks and external parallel calls.
    unsigned int _threadCount = 1;
    Dispatch _dispatch = nullptr;
    void* _context = nullptr;
    std::stop_source _stop;
    wil::unique_mta_usage_cookie _mta;
    WorkerResources::UniquePool _pool;
    WorkerResources::UniqueCleanupGroup _cleanup;
    std::array<Lane, 3> _lanes{};
};

// Initialize, Remove and Close are serialized on one owning thread. Add/Rearm may race Close safely.
// Dedicated one-thread pool. Registered handles and callback contexts are borrowed until Remove/Close returns.
// Callbacks only publish events; they must not block or call Remove/Close. One-shot waits rearm explicitly.
class WaitPool final
{
  public:
    using Callback = void (*)(void*) noexcept;
    static constexpr std::size_t kCapacity = 32;
    struct Ticket final
    {
        std::size_t slot = kCapacity;
        std::uint64_t generation = 0;
    };
    WaitPool() = default;
    ~WaitPool();
    WaitPool(const WaitPool&) = delete;
    WaitPool& operator=(const WaitPool&) = delete;
    _No_competing_thread_ [[nodiscard]] HRESULT Initialize() noexcept;
    [[nodiscard]] HRESULT Add(HANDLE event, Callback callback, void* context, Ticket& ticket) noexcept;
    [[nodiscard]] bool Rearm(Ticket ticket, std::uint32_t timeoutMs = 0) noexcept;
    // Owner-thread teardown only. The lifetime wait is not a UI operation.
    void Remove(Ticket ticket) noexcept;
    void Close() noexcept;

  private:
    struct Slot final
    {
        WaitPool* owner = nullptr;
        PTP_WAIT wait = nullptr; // owned by _cleanup
        HANDLE event = nullptr;
        Callback callback = nullptr;
        void* context = nullptr;
        std::uint64_t generation = 0;
        bool retiring = false;
    };
    static void CALLBACK WaitCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) noexcept;
    _Requires_lock_held_(_lock) [[nodiscard]] bool Matches(Ticket ticket) const noexcept;

    SRWLOCK _lock = SRWLOCK_INIT;
    _Guarded_by_(_lock) bool _closing = false;
    _Guarded_by_(_lock) std::array<Slot, kCapacity> _slots {};
    wil::unique_mta_usage_cookie _mta;
    WorkerResources::UniquePool _pool;
    WorkerResources::UniqueCleanupGroup _cleanup;
};

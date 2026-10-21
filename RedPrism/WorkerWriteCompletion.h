#pragma once

#include <atomic>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// One active file writer, shared by its original job and the owner. No document, device or callback is owned.
// The event acknowledges only the CPU write stage, so confirmed session end can wait without UI application.
class WorkerWriteCompletion final
{
  public:
    // Fixed per-record allowance for this wrapper, its shared control block and native event bookkeeping.
    // The scheduler also applies its process commit/physical headroom before admitting the complete graph.
    static constexpr std::uint64_t kReservationBytes = 4096;

    enum class Phase : unsigned int
    {
        Readback,
        Queued,
        Writing,
        Cancelled,
        Finished
    };

    explicit WorkerWriteCompletion(Phase initial) noexcept
        : _completed(CreateEventW(nullptr, TRUE, FALSE, nullptr)), _phase(initial)
    {
        if (!_completed)
        {
            const DWORD error = GetLastError();
            _creationError = error != 0 ? HRESULT_FROM_WIN32(error) : E_FAIL;
        }
    }

    [[nodiscard]] HRESULT CreationError() const noexcept
    {
        return _creationError;
    }
    [[nodiscard]] Phase Current() const noexcept
    {
        return _phase.load(std::memory_order_acquire);
    }
    [[nodiscard]] HANDLE Event() const noexcept
    {
        return _completed.get();
    }
    // The sole worker stage and owner cancellation compete at this boundary. Whichever wins defines whether
    // session end waits for a file writer or cancels readback before a file can be touched.
    [[nodiscard]] bool BeginWorker(Phase expected) noexcept
    {
        if (expected != Phase::Readback && expected != Phase::Queued)
            return false;
        return _phase.compare_exchange_strong(expected, Phase::Writing, std::memory_order_acq_rel);
    }
    [[nodiscard]] bool CancelReadback() noexcept
    {
        Phase expected = Phase::Readback;
        return _phase.compare_exchange_strong(expected, Phase::Cancelled, std::memory_order_acq_rel);
    }
    // Worker scope exit, including named allocation failure unwinding. The original result remains in its job;
    // this event confers no publication, saved-revision or owner-completion authority.
    void Acknowledge() noexcept
    {
        _phase.store(Phase::Finished, std::memory_order_release);
        (void)SetEvent(_completed.get());
    }

  private:
    wil::unique_handle _completed;
    std::atomic<Phase> _phase;
    HRESULT _creationError = S_OK;
};

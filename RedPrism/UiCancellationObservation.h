#pragma once

#include "UiCancellationBody.h"

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

// Opt-in benchmark transport. The stable object and event are created before scheduler initialization,
// outside timed work. Its borrowed observer and wait handle survive the scheduler's joined Close.
// Ordinary startup does not construct this object. No callback accesses Application or scheduler state.
class UiCancellationObservation final
{
    // Only the admission factory can construct this key; std::make_unique still owns the allocation.
    class Admission final
    {
        Admission() noexcept = default;
        friend class UiCancellationObservation;
    };

  public:
    explicit UiCancellationObservation(Admission) noexcept {}
    static constexpr std::size_t kCapacity = 65536;
    static constexpr std::size_t kOwnerBatch = 32;
    using Capture = UiCancellationCapture<kCapacity>;
    using Sample = Capture::Sample;
    using ReadResult = Capture::ReadResult;

    ~UiCancellationObservation() = default;
    UiCancellationObservation(const UiCancellationObservation&) = delete;
    UiCancellationObservation& operator=(const UiCancellationObservation&) = delete;

    [[nodiscard]] static HRESULT TryCreate(std::unique_ptr<UiCancellationObservation>& output) noexcept
    {
        if (output)
            return E_UNEXPECTED;
        static_assert(noexcept(UiCancellationObservation{Admission{}}));
        try
        {
            auto next = std::make_unique<UiCancellationObservation>(Admission{});
            const HRESULT result = next->_wake.create(wil::EventOptions::ManualReset);
            if (FAILED(result))
                return result;
            output = std::move(next);
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            // Owning opt-in construction boundary. The caller fails the whole benchmark before startup.
            return E_OUTOFMEMORY;
        }
    }

    [[nodiscard]] JobExecutionObserver Observer() noexcept
    {
        return {Notify, this, Notify};
    }

    [[nodiscard]] HANDLE WakeHandle() const noexcept
    {
        return _wake.get(); // Borrowed only; the outer message/frame waits do not close this handle.
    }

    [[nodiscard]] HRESULT Failure() const noexcept
    {
        const auto eventError = _eventError.load(std::memory_order_acquire);
        if (eventError != S_OK)
            return static_cast<HRESULT>(eventError);
        if (_capture.Overflowed())
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        return _clockFailed.load(std::memory_order_acquire) ? E_FAIL : S_OK;
    }

    // Owner only. Reset before reading, never afterwards: publication that races the drain must retain
    // its wake-up. Read does not advance the caller's cursor at an unpublished reservation hole.
    [[nodiscard]] HRESULT BeginOwnerDrain() noexcept
    {
        if (!::ResetEvent(_wake.get()))
            RememberEventFailure();
        return Failure();
    }

    [[nodiscard]] ReadResult Read(std::size_t cursor, Sample& output) const noexcept
    {
        return _capture.Read(cursor, output);
    }

    // After at most kOwnerBatch Published records, inspect the next slot once. Re-arm only for an
    // already published record. Pending waits for that producer's own publication signal; re-arming
    // on a hole would busy-loop on a later producer's completed record.
    [[nodiscard]] HRESULT EndOwnerBatch(std::size_t nextCursor) noexcept
    {
        Sample next;
        if (_capture.Read(nextCursor, next) == ReadResult::Published)
            Signal();
        return Failure();
    }

  private:
    static_assert(std::atomic<std::int32_t>::is_always_lock_free);

    static void Notify(void* context, JobExecutionNotice notice) noexcept
    {
        auto& self = *static_cast<UiCancellationObservation*>(context);
        LARGE_INTEGER counter{};
        const bool clockValid = QueryPerformanceCounter(&counter) != FALSE && counter.QuadPart >= 0;
        if (!clockValid)
            self._clockFailed.store(true, std::memory_order_release);
        (void)self._capture.Publish(notice, clockValid ? counter.QuadPart : -1);
        // Even overflow/clock refusal signals the owner. No diagnostic boundary grants retirement.
        self.Signal();
    }

    void RememberEventFailure() noexcept
    {
        const auto error = GetLastError();
        const auto result = static_cast<std::int32_t>(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
        std::int32_t expected = S_OK;
        (void)_eventError.compare_exchange_strong(expected, result, std::memory_order_release,
                                                  std::memory_order_relaxed);
    }

    void Signal() noexcept
    {
        if (!::SetEvent(_wake.get()))
            RememberEventFailure();
    }

    Capture _capture;
    wil::unique_event_nothrow _wake;
    std::atomic<std::int32_t> _eventError{S_OK};
    std::atomic<bool> _clockFailed{false};
};

#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

namespace
{

// TEST-ONLY state witness for one clipboard Paste's cross-stage native acquisition retry accounting.
// It observes metadata only: it never follows HistoryWorkStamp::owner and owns no resources.
class ClipboardRetryWitness final
{
  public:
    struct Diagnostics final
    {
        JobTicket firstTicket{};
        JobTicket lastTicket{};
        HistoryWorkStamp stamp{};
        std::uint64_t selectedOrdinal = 0;
        unsigned int lastAttempt = 0;
        unsigned int handoffCount = 0;
        bool initialized = false;
        bool fullCaptured = false;
    };

    // actualOpens and attempt represent Paste's cumulative Inspect/Copy acquisition number across
    // metadata-probe and full-read stages. A handoff is a new admitted full-read ticket while the
    // selected history stamp and ordinal remain exact.
    [[nodiscard]] bool Observe(JobTicket ticket, HistoryWorkStamp stamp, std::uint64_t selectedOrdinal,
                               unsigned int attempt, unsigned int actualOpens, std::uint32_t delay,
                               std::uint32_t expectedDelay, bool fullCaptured, bool allowProbeToReadHandoff) noexcept
    {
        if (ticket.slot >= 128 || ticket.generation == 0 || stamp.owner == nullptr || stamp.documentSerial == 0 ||
            stamp.ordinal == 0 || stamp.acquisition == 0 || selectedOrdinal == 0 || stamp.ordinal != selectedOrdinal ||
            attempt == 0 || attempt > 5 || actualOpens != attempt || expectedDelay == 0 || delay != expectedDelay)
        {
            return false;
        }

        if (!_state.initialized)
        {
            _state.firstTicket = ticket;
            _state.lastTicket = ticket;
            _state.stamp = stamp;
            _state.selectedOrdinal = selectedOrdinal;
            _state.lastAttempt = attempt;
            _state.handoffCount = 0;
            _state.initialized = true;
            _state.fullCaptured = fullCaptured;
            return true;
        }

        if (!(stamp == _state.stamp) || selectedOrdinal != _state.selectedOrdinal || attempt <= _state.lastAttempt ||
            (_state.fullCaptured && !fullCaptured))
        {
            return false;
        }

        if (!(ticket == _state.lastTicket))
        {
            // Exactly one probe-to-read record transition is permitted. The attempt gap proves at
            // least one successful Inspect acquisition elapsed between the two observed waits.
            if (_state.handoffCount != 0 || !allowProbeToReadHandoff || !fullCaptured || _state.fullCaptured ||
                attempt < _state.lastAttempt + 2)
            {
                return false;
            }
            ++_state.handoffCount;
            _state.lastTicket = ticket;
        }

        _state.lastAttempt = attempt;
        _state.fullCaptured = fullCaptured;
        return true;
    }

    [[nodiscard]] const Diagnostics& GetDiagnostics() const noexcept
    {
        return _state;
    }

    [[nodiscard]] unsigned int HandoffCount() const noexcept
    {
        return _state.handoffCount;
    }

    [[nodiscard]] unsigned int LastAttempt() const noexcept
    {
        return _state.lastAttempt;
    }

    [[nodiscard]] JobTicket LastTicket() const noexcept
    {
        return _state.lastTicket;
    }

  private:
    Diagnostics _state{};
};

struct ClipboardRetryWitnessTestFact final
{
    const char* name = "";
    bool passed = false;
};

struct ClipboardRetryWitnessTestResults final
{
    static constexpr std::size_t kFactCapacity = 20;
    ClipboardRetryWitnessTestFact facts[kFactCapacity]{};
    std::size_t factCount = 0;
    int failures = 0;
};

namespace ClipboardRetryWitnessTests
{
inline constexpr std::uint32_t kDelay = 23;

[[nodiscard]] inline JobTicket Ticket(std::size_t slot, std::uint64_t generation = 1) noexcept
{
    return JobTicket{slot, generation};
}

[[nodiscard]] inline HistoryWorkStamp Stamp(const void* owner, std::uint64_t documentSerial = 17,
                                            std::uint64_t ordinal = 41, std::uint64_t acquisition = 3) noexcept
{
    return HistoryWorkStamp{owner, documentSerial, ordinal, HistoryWorkKind::Import, acquisition};
}

[[nodiscard]] inline bool SameDiagnostics(const ClipboardRetryWitness::Diagnostics& left,
                                          const ClipboardRetryWitness::Diagnostics& right) noexcept
{
    return left.firstTicket == right.firstTicket && left.lastTicket == right.lastTicket && left.stamp == right.stamp &&
           left.selectedOrdinal == right.selectedOrdinal && left.lastAttempt == right.lastAttempt &&
           left.handoffCount == right.handoffCount && left.initialized == right.initialized &&
           left.fullCaptured == right.fullCaptured;
}

inline void Record(ClipboardRetryWitnessTestResults& results, const char* name, bool passed) noexcept
{
    if (results.factCount == ClipboardRetryWitnessTestResults::kFactCapacity)
    {
        ++results.failures;
        return;
    }
    results.facts[results.factCount++] = ClipboardRetryWitnessTestFact{name, passed};
    if (!passed)
    {
        ++results.failures;
    }
}

[[nodiscard]] inline bool RejectedWithoutMutation(ClipboardRetryWitness& witness, JobTicket ticket,
                                                  HistoryWorkStamp stamp, std::uint64_t ordinal, unsigned int attempt,
                                                  unsigned int actualOpens, std::uint32_t delay,
                                                  std::uint32_t expectedDelay, bool fullCaptured,
                                                  bool allowHandoff) noexcept
{
    const auto before = witness.GetDiagnostics();
    const bool rejected = !witness.Observe(ticket, stamp, ordinal, attempt, actualOpens, delay, expectedDelay,
                                           fullCaptured, allowHandoff);
    return rejected && SameDiagnostics(before, witness.GetDiagnostics());
}

// This runner is independent of Win32 and the application fixture. It returns fixed-storage facts;
// callers decide how to print each PASS/FAIL line. No test execution constitutes a native clipboard claim.
[[nodiscard]] inline ClipboardRetryWitnessTestResults Run() noexcept
{
    ClipboardRetryWitnessTestResults results{};
    int ownerToken = 0;
    const auto stamp = Stamp(&ownerToken);

    {
        ClipboardRetryWitness witness;
        const bool probe = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool read = witness.Observe(Ticket(5), stamp, 41, 3, 3, kDelay, kDelay, true, true);
        Record(results, "probe-attempt-1-to-full-read-attempt-3-handoff",
               probe && read && witness.HandoffCount() == 1 && witness.LastAttempt() == 3 &&
                   witness.LastTicket() == Ticket(5));
    }
    {
        ClipboardRetryWitness witness;
        const bool first = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool retry = witness.Observe(Ticket(2), stamp, 41, 2, 2, kDelay, kDelay, false, true);
        Record(results, "same-ticket-retry-increments-global-acquisition",
               first && retry && witness.HandoffCount() == 0 && witness.LastAttempt() == 2);
    }
    {
        ClipboardRetryWitness witness;
        const bool first = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool later = witness.Observe(Ticket(2), stamp, 41, 4, 4, kDelay, kDelay, false, true);
        Record(results, "same-ticket-acquisition-gaps-are-allowed", first && later && witness.LastAttempt() == 4);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "ticket-change-during-probe-refused-without-mutation",
               RejectedWithoutMutation(witness, Ticket(5), stamp, 41, 3, 3, kDelay, kDelay, false, true));
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "copy-or-exhaustion-cannot-authorize-handoff",
               RejectedWithoutMutation(witness, Ticket(5), stamp, 41, 3, 3, kDelay, kDelay, true, false));
    }
    {
        ClipboardRetryWitness witness;
        const bool first = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool handoff = witness.Observe(Ticket(5), stamp, 41, 3, 3, kDelay, kDelay, true, true);
        Record(results, "second-ticket-handoff-refused-without-mutation",
               first && handoff &&
                   RejectedWithoutMutation(witness, Ticket(7), stamp, 41, 5, 5, kDelay, kDelay, true, true));
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 2, 2, kDelay, kDelay, false, true);
        const bool duplicate =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 2, 2, kDelay, kDelay, false, true);
        const bool regression =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "duplicate-and-regressing-attempts-refused-unchanged", duplicate && regression);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool wrongOpens =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 2, 1, kDelay, kDelay, false, true);
        const bool wrongDelay =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 2, 2, kDelay + 1, kDelay, false, true);
        const bool zeroExpected = RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 2, 2, 0, 0, false, true);
        Record(results, "cumulative-open-count-and-positive-exact-delay-enforced",
               wrongOpens && wrongDelay && zeroExpected);
    }
    {
        ClipboardRetryWitness witness;
        const bool badSlot =
            RejectedWithoutMutation(witness, Ticket(128), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool badGeneration =
            RejectedWithoutMutation(witness, Ticket(2, 0), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool defaultStamp =
            RejectedWithoutMutation(witness, Ticket(2), HistoryWorkStamp{}, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "out-of-range-or-default-ticket-and-default-stamp-refused",
               badSlot && badGeneration && defaultStamp && !witness.GetDiagnostics().initialized);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const auto changedDoc = Stamp(&ownerToken, 18, 41, 3);
        const auto changedAcquisition = Stamp(&ownerToken, 17, 41, 4);
        const auto changedKind = HistoryWorkStamp{&ownerToken, 17, 41, HistoryWorkKind::Open, 3};
        const bool doc = RejectedWithoutMutation(witness, Ticket(2), changedDoc, 41, 2, 2, kDelay, kDelay, false, true);
        const bool acquisition =
            RejectedWithoutMutation(witness, Ticket(2), changedAcquisition, 41, 2, 2, kDelay, kDelay, false, true);
        const bool kind =
            RejectedWithoutMutation(witness, Ticket(2), changedKind, 41, 2, 2, kDelay, kDelay, false, true);
        Record(results, "changed-document-acquisition-or-kind-stamp-refused", doc && acquisition && kind);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "changed-selected-ordinal-refused-without-mutation",
               RejectedWithoutMutation(witness, Ticket(2), stamp, 42, 2, 2, kDelay, kDelay, false, true));
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const auto nullOwner = Stamp(nullptr);
        const auto zeroDocument = Stamp(&ownerToken, 0);
        const auto zeroAcquisition = Stamp(&ownerToken, 17, 41, 0);
        const bool owner =
            RejectedWithoutMutation(witness, Ticket(2), nullOwner, 41, 2, 2, kDelay, kDelay, false, true);
        const bool document =
            RejectedWithoutMutation(witness, Ticket(2), zeroDocument, 41, 2, 2, kDelay, kDelay, false, true);
        const bool acquisition =
            RejectedWithoutMutation(witness, Ticket(2), zeroAcquisition, 41, 2, 2, kDelay, kDelay, false, true);
        Record(results, "null-owner-and-zero-source-identities-refused", owner && document && acquisition);
    }
    {
        ClipboardRetryWitness witness;
        const bool ordinalZero =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 0, 1, 1, kDelay, kDelay, false, true);
        const bool stampOrdinalZero = RejectedWithoutMutation(witness, Ticket(2), Stamp(&ownerToken, 17, 0, 3), 0, 1, 1,
                                                              kDelay, kDelay, false, true);
        Record(results, "zero-selected-or-stamp-ordinal-refused", ordinalZero && stampOrdinalZero);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool full = witness.Observe(Ticket(2), stamp, 41, 2, 2, kDelay, kDelay, true, true);
        const bool revert = RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 3, 3, kDelay, kDelay, false, true);
        Record(results, "full-captured-state-is-monotonic", full && revert && witness.GetDiagnostics().fullCaptured);
    }
    {
        ClipboardRetryWitness witness;
        (void)witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "ticket-handoff-requires-one-successful-acquisition-gap",
               RejectedWithoutMutation(witness, Ticket(5), stamp, 41, 2, 2, kDelay, kDelay, true, true));
    }
    {
        ClipboardRetryWitness witness;
        const auto wrongOrdinalStamp = Stamp(&ownerToken, 17, 42, 3);
        Record(results, "stamp-ordinal-must-equal-selected-ordinal",
               RejectedWithoutMutation(witness, Ticket(2), wrongOrdinalStamp, 41, 1, 1, kDelay, kDelay, false, true));
    }
    {
        ClipboardRetryWitness witness;
        const bool attemptZero =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 0, 0, kDelay, kDelay, false, true);
        const bool attemptSix =
            RejectedWithoutMutation(witness, Ticket(2), stamp, 41, 6, 6, kDelay, kDelay, false, true);
        Record(results, "attempt-range-one-through-five-enforced", attemptZero && attemptSix);
    }
    {
        ClipboardRetryWitness witness;
        const bool first = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        const bool gap = witness.Observe(Ticket(2), stamp, 41, 3, 3, kDelay, kDelay, false, true);
        Record(results, "same-record-global-attempt-gap-is-valid", first && gap && witness.HandoffCount() == 0);
    }
    {
        ClipboardRetryWitness witness;
        int foreignOwner = 0;
        const bool first = witness.Observe(Ticket(2), stamp, 41, 1, 1, kDelay, kDelay, false, true);
        Record(results, "different-live-history-owner-refused",
               first && RejectedWithoutMutation(witness, Ticket(2), Stamp(&foreignOwner), 41, 2, 2, kDelay, kDelay,
                                                false, true));
    }
    return results;
}
} // namespace ClipboardRetryWitnessTests

[[nodiscard]] inline ClipboardRetryWitnessTestResults RunClipboardRetryWitnessTests() noexcept
{
    return ClipboardRetryWitnessTests::Run();
}

struct ClipboardHolder final
{
    wil::unique_handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<HWND> window{nullptr};
    std::atomic<bool> acquired{false};
    std::atomic<bool> closedCleanly{false};
    std::atomic<DWORD> error{ERROR_SUCCESS};
    std::atomic<unsigned> setupAttempts{0};
    std::atomic<DWORD> firstSetupError{ERROR_SUCCESS};
    void* deniedContext = nullptr;
    void (*afterFirstDenied)(void*) noexcept = nullptr;
    bool installDib = false;
};
DWORD WINAPI HoldClipboard(void* raw) noexcept
{
    auto& holder = *static_cast<ClipboardHolder*>(raw);
    const auto finish = wil::scope_exit(
        [&]() noexcept
        {
            holder.window.store(nullptr);
            (void)SetEvent(holder.ready.get());
            (void)SetEvent(holder.closed.get());
        });
    wil::unique_hwnd window(
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!window)
    {
        holder.error.store(GetLastError());
        return 1;
    }
    // Native setup shares the desktop clipboard with other applications. Retry only ACCESS_DENIED
    // through bounded one-shot kernel events; no sleep, busy loop or fabricated acquisition.
    constexpr std::array<DWORD, 5> setupDelays{20, 40, 80, 160, 320};
    wil::unique_handle deadline;
    for (unsigned attempt = 0;; ++attempt)
    {
        ++holder.setupAttempts;
        if (OpenClipboard(window.get()))
            break;
        const auto error = GetLastError();
        if (attempt == 0)
        {
            holder.firstSetupError.store(error);
            if (holder.afterFirstDenied)
                holder.afterFirstDenied(holder.deniedContext);
        }
        if (error != ERROR_ACCESS_DENIED || attempt == setupDelays.size())
        {
            holder.error.store(error);
            return 1;
        }
        if (!deadline)
        {
            deadline.reset(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE));
            if (!deadline)
            {
                holder.error.store(GetLastError());
                return 1;
            }
        }
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(setupDelays[attempt]) * 10000;
        if (!SetWaitableTimer(deadline.get(), &due, 0, nullptr, nullptr, FALSE))
        {
            holder.error.store(GetLastError());
            return 1;
        }
        const HANDLE events[]{deadline.get(), holder.stop.get(), holder.release.get()};
        if (WaitForMultipleObjects(3, events, FALSE, INFINITE) != WAIT_OBJECT_0)
        {
            holder.error.store(ERROR_CANCELLED);
            return 1;
        }
    }
    holder.window.store(window.get());
    {
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                if (CloseClipboard())
                    holder.closedCleanly.store(true);
                else
                    holder.error.store(GetLastError());
                (void)SetEvent(holder.closed.get());
            });
        if (holder.installDib)
        {
            // Literal 1x1 32-bit BI_RGB DIB: header40 followed by B17 G33 R65, opaque padding.
            constexpr std::array<std::uint8_t, 44> dib{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0,  0,  1,  0,  32,
                                                       0,  0, 0, 0, 0, 4, 0, 0, 0, 0, 0,  0,  0,  0,  0,
                                                       0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 33, 65, 255};
            wil::unique_hglobal block(GlobalAlloc(GMEM_MOVEABLE, dib.size()));
            if (!block)
            {
                holder.error.store(GetLastError());
                return 1;
            }
            {
                const wil::unique_hglobal_locked locked(block.get());
                if (!locked.get())
                {
                    holder.error.store(GetLastError());
                    return 1;
                }
                std::memcpy(locked.get(), dib.data(), dib.size());
            }
            if (!EmptyClipboard() || !SetClipboardData(CF_DIB, block.get()))
            {
                holder.error.store(GetLastError());
                return 1;
            }
            (void)block.release();
        }
        holder.acquired.store(true);
        if (!SetEvent(holder.ready.get()) || WaitForSingleObject(holder.release.get(), 15000) != WAIT_OBJECT_0)
        {
            holder.error.store(ERROR_TIMEOUT);
            return 1;
        }
    }
    if (!holder.closedCleanly.load())
        return 1;
    // Keep the immediate format owner alive and dispatch WM_DESTROYCLIPBOARD until joined cleanup.
    // The original producer never turns immediate bytes into a delayed-render fixture.
    const HANDLE stop = holder.stop.get();
    for (;;)
    {
        const auto wait = MsgWaitForMultipleObjectsEx(1, &stop, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0)
            return 0;
        if (wait != WAIT_OBJECT_0 + 1)
        {
            holder.error.store(GetLastError());
            return 1;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
int VerifyClipboardHolderSetup() noexcept
{
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s clipboard-holder-setup.%s\n", value ? "PASS" : "FAIL", label);
        return value;
    };
    const auto summary = wil::scope_exit(
        [&]() noexcept { std::printf("SUMMARY clipboard-holder-setup checks=%u failures=%u\n", checks, failures); });
    const auto run = [&](bool exhaust) noexcept
    {
        struct Gate final
        {
            wil::unique_handle denied{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            wil::unique_handle resume{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        } gate;
        ClipboardHolder blocker;
        ClipboardHolder holder;
        wil::unique_handle blockerThread;
        wil::unique_handle thread;
        auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                if (gate.resume)
                    (void)SetEvent(gate.resume.get());
                if (blocker.release)
                    (void)SetEvent(blocker.release.get());
                if (blocker.stop)
                    (void)SetEvent(blocker.stop.get());
                if (holder.release)
                    (void)SetEvent(holder.release.get());
                if (holder.stop)
                    (void)SetEvent(holder.stop.get());
                if (thread)
                    (void)WaitForSingleObject(thread.get(), INFINITE);
                if (blockerThread)
                    (void)WaitForSingleObject(blockerThread.get(), INFINITE);
            });
        if (!gate.denied || !gate.resume || !blocker.ready || !blocker.release || !blocker.closed || !blocker.stop ||
            !holder.ready || !holder.release || !holder.closed || !holder.stop)
            return false;
        blockerThread.reset(CreateThread(nullptr, 0, HoldClipboard, &blocker, 0, nullptr));
        if (!blockerThread || WaitForSingleObject(blocker.ready.get(), 10000) != WAIT_OBJECT_0 || !blocker.acquired ||
            blocker.error != ERROR_SUCCESS || GetOpenClipboardWindow() != blocker.window.load())
            return false;
        const auto sequence = GetClipboardSequenceNumber();
        holder.deniedContext = &gate;
        holder.afterFirstDenied = [](void* raw) noexcept
        {
            auto& state = *static_cast<Gate*>(raw);
            (void)SetEvent(state.denied.get());
            (void)WaitForSingleObject(state.resume.get(), INFINITE);
        };
        thread.reset(CreateThread(nullptr, 0, HoldClipboard, &holder, 0, nullptr));
        if (!thread || WaitForSingleObject(gate.denied.get(), 10000) != WAIT_OBJECT_0)
            return false;
        const bool firstDenied = holder.setupAttempts == 1 && holder.firstSetupError == ERROR_ACCESS_DENIED &&
                                 !holder.acquired && GetOpenClipboardWindow() == blocker.window.load() &&
                                 GetThreadId(thread.get()) != GetThreadId(blockerThread.get()) &&
                                 GetThreadId(thread.get()) != GetCurrentThreadId();
        if (!exhaust)
        {
            (void)SetEvent(blocker.release.get());
            if (WaitForSingleObject(blocker.closed.get(), 10000) != WAIT_OBJECT_0 || !blocker.closedCleanly)
                return false;
        }
        (void)SetEvent(gate.resume.get());
        if (WaitForSingleObject(holder.ready.get(), 10000) != WAIT_OBJECT_0)
            return false;
        const bool reached = exhaust ? !holder.acquired && holder.setupAttempts == 6 &&
                                           holder.error == ERROR_ACCESS_DENIED &&
                                           GetOpenClipboardWindow() == blocker.window.load()
                                     : holder.acquired && holder.setupAttempts == 2 && holder.error == ERROR_SUCCESS &&
                                           GetOpenClipboardWindow() == holder.window.load();
        const bool untouchedWhileHeld = !exhaust || GetClipboardSequenceNumber() == sequence;
        (void)SetEvent(blocker.release.get());
        (void)SetEvent(blocker.stop.get());
        (void)SetEvent(holder.release.get());
        (void)SetEvent(holder.stop.get());
        if (WaitForSingleObject(thread.get(), 10000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(blockerThread.get(), 10000) != WAIT_OBJECT_0)
            return false;
        DWORD result = STILL_ACTIVE;
        DWORD blockerResult = STILL_ACTIVE;
        const bool joined = GetExitCodeThread(thread.get(), &result) && result == (exhaust ? 1u : 0u) &&
                            GetExitCodeThread(blockerThread.get(), &blockerResult) && blockerResult == 0 &&
                            !holder.window.load() && holder.closedCleanly == !exhaust && !blocker.window.load() &&
                            blocker.closedCleanly && untouchedWhileHeld;
        return firstDenied && reached && joined;
    };
    // Real OS denial with a deterministic event gate; no elapsed-time threshold defines success.
    expect(run(false), "real-denial-release-retry-acquires-on-second-attempt-and-joins");
    expect(run(true), "six-real-denials-exhaust-preserve-independent-holder-and-join-without-data-change");
    return failures ? 5 : 0;
}
} // namespace

int Application::RunStandaloneClipboardContentionSelfTest(HINSTANCE instance) noexcept
{
    const auto witnessControls = RunClipboardRetryWitnessTests();
    for (std::size_t i = 0; i < witnessControls.factCount; ++i)
        std::printf("%s clipboard-retry-witness.%s\n", witnessControls.facts[i].passed ? "PASS" : "FAIL",
                    witnessControls.facts[i].name);
    if (witnessControls.factCount != 19 || witnessControls.failures)
        return 5;
    if (const int failed = VerifyClipboardHolderSetup(); failed != 0)
        return failed;
    unsigned checks = 0;
    unsigned failures = 0;
    unsigned reached = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s clipboard-real-contention.%s.%s\n", value ? "PASS" : "FAIL", scenario, label);
        return value;
    };
    const auto summary = wil::scope_exit(
        [&]() noexcept { std::printf("SUMMARY clipboard-real-contention checks=%u failures=%u\n", checks, failures); });
    struct Scenario final
    {
        const char* name;
        bool paste;
        bool release;
    };
    constexpr std::array scenarios{Scenario{"copy-release", false, true}, Scenario{"copy-exhaust", false, false},
                                   Scenario{"paste-release", true, true}, Scenario{"paste-exhaust", true, false}};
    for (const auto item : scenarios)
    {
        scenario = item.name;
        ClipboardHolder holder;
        ClipboardHolder secondHolder;
        holder.installDib = item.paste;
        struct Retry final
        {
            ClipboardHolder& holder;
            ClipboardHolder& secondHolder;
            wil::unique_handle secondThread;
            ClipboardWorkTestGate* gate = nullptr; // borrowed until both jobs and threads have joined
            bool forceHandoff = false;
            bool secondStarted = false;
            bool secondReady = false;
            bool secondHeld = false;
            std::atomic<bool> secondBusyObserved{false};
            std::uint64_t selectedOrdinal = 0;
            std::uint64_t selectedDocumentSerial = 0;
            Application* owner = nullptr; // accessed only by the owner-side armed callback
            bool releaseOnArm = false;
            unsigned armed = 0;
            unsigned heldWaits = 0, closedWaits = 0;
            ClipboardRetryWitness witness;
            bool paste = false;
            bool sameTicket = true;
            bool actualHolder = true;
            bool originalBarrier = true;
            bool signalSucceeded = true;
            std::atomic<bool> callbackFailed{false};
        } retry{holder, secondHolder};
        retry.forceHandoff = item.paste && item.release;
        retry.releaseOnArm = item.release;
        retry.paste = item.paste;
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.retryWait.context = &retry;
        probe.retryWait.armed = [](void* raw, JobTicket ticket, unsigned number, std::uint32_t delay) noexcept
        {
            auto& observed = *static_cast<Retry*>(raw);
            auto& app = *observed.owner;
            ++observed.armed;
            const auto* gate = app._clipboardWorkTestGate.get();
            const auto stamp = app._clipboardStamp.value_or(HistoryWorkStamp{});
            const auto ordinal = app._activeHostRequest ? app._activeHostRequest->ordinal : 0;
            const bool fullCaptured = gate && gate->captureReservationHeld && gate->reservationBytes &&
                                      app._jobs.ReservedMemoryBytes() >= gate->reservationBytes;
            observed.sameTicket =
                observed.sameTicket &&
                observed.witness.Observe(ticket, stamp, ordinal, number, gate ? gate->openAttempts.load() : 0, delay,
                                         RetryWaitQueue::DelayMs(number), fullCaptured,
                                         observed.paste && observed.releaseOnArm);
            observed.originalBarrier =
                observed.originalBarrier && app._historyWork.IsCurrent(stamp) && app._activeHostRequest &&
                stamp.documentSerial == app._activeHostRequest->documentSerial && ordinal == observed.selectedOrdinal &&
                stamp.documentSerial == observed.selectedDocumentSerial;
            // Every Busy retains its admitted phase's ticket and reservation. Paste may hand off
            // once from metadata inspection to a fully reserved read under the same selected request.
            // A second actual OS owner forces Busy after the successful inspection, without injection.
            const bool expectHeld = !observed.releaseOnArm || observed.armed == 1;
            const bool held = observed.holder.acquired.load() &&
                              GetOpenClipboardWindow() == observed.holder.window.load() &&
                              !observed.holder.closedCleanly.load();
            const bool closed = observed.holder.closedCleanly.load() &&
                                WaitForSingleObject(observed.holder.closed.get(), 0) == WAIT_OBJECT_0;
            observed.heldWaits += expectHeld && held ? 1u : 0u;
            observed.closedWaits += !expectHeld && closed ? 1u : 0u;
            observed.actualHolder = observed.actualHolder && (expectHeld ? held : closed);
            observed.originalBarrier = observed.originalBarrier && app._clipboardTicket == std::optional{ticket} &&
                                       app._historyWork.Busy() && app._activeHostRequest &&
                                       app._jobs.ReservedMemoryBytes() > 0 && app._retryWait.HasPending();
            if (observed.releaseOnArm && observed.armed == 1)
                observed.signalSucceeded = SetEvent(observed.holder.release.get()) != FALSE && observed.signalSucceeded;
            if (observed.forceHandoff && fullCaptured && !observed.secondBusyObserved.load())
            {
                observed.secondHeld = observed.secondReady && observed.secondHolder.acquired.load() &&
                                      !observed.secondHolder.closedCleanly.load() &&
                                      GetOpenClipboardWindow() == observed.secondHolder.window.load();
                observed.secondBusyObserved.store(true);
                observed.signalSucceeded =
                    SetEvent(observed.secondHolder.release.get()) != FALSE && observed.signalSucceeded;
            }
            // Exhaustion keeps the real holder locked and accelerates only the registered deadline event.
            // Release uses the genuine native backoff so the desktop clipboard can finish its handoff.
            if (!observed.releaseOnArm)
                observed.signalSucceeded = app._retryWait.SignalDeadlineForTest(ticket) && observed.signalSucceeded;
        };
        probe.retryWait.beforePublication = [](void* raw) noexcept
        {
            auto& observed = *static_cast<Retry*>(raw);
            // Wait-pool producer uses only immutable mode and holder events/atomics, never the UI pointer.
            if (observed.releaseOnArm && (WaitForSingleObject(observed.holder.closed.get(), 10000) != WAIT_OBJECT_0 ||
                                          !observed.holder.closedCleanly.load()))
                observed.callbackFailed.store(true);
            if (observed.secondBusyObserved.load() &&
                (WaitForSingleObject(observed.secondHolder.closed.get(), 10000) != WAIT_OBJECT_0 ||
                 !observed.secondHolder.closedCleanly.load()))
                observed.callbackFailed.store(true);
        };
        const auto dispatch = [](void* raw, void* nativeMessage) noexcept
        {
            auto& observed = *static_cast<Retry*>(raw);
            const auto& message = *static_cast<MSG*>(nativeMessage);
            if (observed.forceHandoff && !observed.secondStarted && !message.hwnd &&
                message.message == ClipboardWorkTestGate::kMessage)
            {
                // PasteInspected has closed the original native inspection before posting this event.
                // Keep that worker gated until the second independent native thread owns the clipboard.
                observed.secondStarted = true;
                observed.secondThread.reset(
                    CreateThread(nullptr, 0, HoldClipboard, &observed.secondHolder, 0, nullptr));
                const auto ready =
                    observed.secondThread ? WaitForSingleObject(observed.secondHolder.ready.get(), 10000) : WAIT_FAILED;
                observed.secondReady = ready == WAIT_OBJECT_0 && observed.secondHolder.acquired.load() &&
                                       observed.secondHolder.error.load() == ERROR_SUCCESS &&
                                       GetOpenClipboardWindow() == observed.secondHolder.window.load() &&
                                       observed.holder.closedCleanly.load();
                observed.signalSucceeded =
                    observed.gate && SetEvent(observed.gate->release.get()) != FALSE && observed.signalSucceeded;
                return;
            }
            Application::DispatchMessageThunk(observed.owner, nativeMessage);
        };
        auto owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        retry.owner = owner.get();
        wil::unique_handle thread;
        SystemClipboard::SavedContents saved;
        bool savedReady = false;
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                (void)SetEvent(holder.release.get());
                (void)SetEvent(holder.stop.get());
                (void)SetEvent(secondHolder.release.get());
                (void)SetEvent(secondHolder.stop.get());
                if ((thread && WaitForSingleObject(thread.get(), 10000) != WAIT_OBJECT_0) ||
                    (retry.secondThread && WaitForSingleObject(retry.secondThread.get(), 10000) != WAIT_OBJECT_0))
                    std::_Exit(5); // borrowed thread contexts must not die after a failed join
                if (owner)
                {
                    if (savedReady && owner->_window)
                        (void)SystemClipboard::RestoreContents(owner->_window.get(), saved);
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                }
            });
        if (!expect(owner && holder.ready && holder.release && holder.closed && holder.stop && secondHolder.ready &&
                        secondHolder.release && secondHolder.closed && secondHolder.stop,
                    "owned-window-thread-events-and-probe-context"))
            return 5;
        owner->_processingTestProbe = &probe;
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                        SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                        SUCCEEDED(owner->InitializeChrome()),
                    "real-hidden-owner-and-production-retry-route"))
            return 5;
        DocumentFixture fixture;
        fixture.width = 9;
        fixture.height = 7;
        constexpr std::array<std::uint8_t, 12> pixels{11, 22, 33, 255, 20, 40, 80, 255, 99, 100, 101, 255};
        auto layer = ImageLayer::Blank(L"Contention source", {3, 1});
        const auto source = Raster::Make(3, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        layer.asset = RasterAsset{source, {}, layer.name};
        const auto active = layer.id;
        fixture.layers.push_back(std::move(layer));
        fixture.selection = DocumentSelection{false, SelectionLoops{SelectionLoop{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}};
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_session.history.Reset();
        owner->DocumentChanged();
        savedReady = SystemClipboard::SaveContents(owner->_window.get(), saved);
        if (!expect(savedReady &&
                        SettleJobs(owner->_jobs, 10000, DispatchMessageThunk, owner.get()) == SettleResult::Settled,
                    "literal-source-and-saved-system-clipboard"))
            return 5;
        owner->_clipboardWorkTestGate = std::make_shared<ClipboardWorkTestGate>();
        const auto gate = owner->_clipboardWorkTestGate;
        retry.gate = gate.get();
        gate->phase =
            retry.forceHandoff ? ClipboardWorkPhase::PasteInspected : ClipboardWorkPhase::CopyPublicationSucceeded;
        if (!gate->release || (!retry.forceHandoff && !SetEvent(gate->release.get())))
            return 5;
        thread.reset(CreateThread(nullptr, 0, HoldClipboard, &holder, 0, nullptr));
        const auto ready = thread ? WaitForSingleObject(holder.ready.get(), 10000) : WAIT_FAILED;
        if (!holder.acquired.load())
        {
            DWORD blockerProcess = 0;
            const auto blockerWindow = GetOpenClipboardWindow();
            if (blockerWindow)
                (void)GetWindowThreadProcessId(blockerWindow, &blockerProcess);
            std::printf("DIAG clipboard-holder-setup attempts=%u error=%lu first-error=%lu blocker-pid=%lu ready=%lu\n",
                        holder.setupAttempts.load(), holder.error.load(), holder.firstSetupError.load(), blockerProcess,
                        ready);
        }
        if (!expect(thread && ready == WAIT_OBJECT_0 && holder.acquired.load() &&
                        holder.error.load() == ERROR_SUCCESS && GetOpenClipboardWindow() == holder.window.load(),
                    "distinct-native-thread-really-holds-open-clipboard"))
            return 5;
        owner->_clipboardSequence = 0; // external Paste cannot use a private-origin shortcut
        const auto serial = owner->_session.ClipboardSerial();
        const auto receipt =
            owner->QueueHostInput(HostCommandInput{item.paste ? ChromeHost::kCommandPaste : ChromeHost::kCommandCopy});
        if (!expect(receipt.Read().outcome == HostOutcome::Pending, "original-public-command-descriptor-pending"))
            return 5;
        if (const auto* queued = owner->_hostRequests.PendingAt(0))
        {
            retry.selectedOrdinal = queued->ordinal;
            retry.selectedDocumentSerial = queued->documentSerial;
        }
        if (!expect(SettleJobs(owner->_jobs, 15000, dispatch, &retry) == SettleResult::Settled,
                    "production-job-and-real-event-wait-acknowledged"))
            return 5;
        if (!((item.release ? retry.armed >= 1 && retry.armed <= 5 && retry.heldWaits == 1 &&
                                  retry.closedWaits + 1 == retry.armed
                            : retry.armed == 5 && retry.heldWaits == 5 && retry.closedWaits == 0) &&
              retry.sameTicket && retry.actualHolder && retry.originalBarrier && retry.signalSucceeded &&
              !retry.callbackFailed.load()))
            std::printf("DIAG clipboard-holder-retry scenario=%s armed=%u same=%u holder=%u barrier=%u signals=%u "
                        "callback=%u opens=%u handoffs=%u lastAttempt=%u\n",
                        scenario, retry.armed, retry.sameTicket ? 1u : 0u, retry.actualHolder ? 1u : 0u,
                        retry.originalBarrier ? 1u : 0u, retry.signalSucceeded ? 1u : 0u,
                        retry.callbackFailed.load() ? 1u : 0u, gate->openAttempts.load(), retry.witness.HandoffCount(),
                        retry.witness.LastAttempt());
        expect((item.release ? retry.armed >= 1 && retry.armed <= 5 && retry.heldWaits == 1 &&
                                   retry.closedWaits + 1 == retry.armed
                             : retry.armed == 5 && retry.heldWaits == 5 && retry.closedWaits == 0) &&
                   retry.sameTicket && retry.actualHolder && retry.originalBarrier && retry.signalSucceeded &&
                   !retry.callbackFailed.load(),
               "same-original-ticket-real-busy-and-retained-reservation-at-each-armed-wait");
        expect(gate->forcedBusyAttempts.load() == 0 && gate->openFailures.load() == 0 &&
                   gate->openAttempts.load() >= (item.release ? 2u : 6u) && gate->openAttempts.load() <= 6 &&
                   gate->emptyAttempts.load() == (!item.paste && item.release ? 1u : 0u) &&
                   gate->publications.load() == (!item.paste && item.release ? 1u : 0u),
               "zero-injected-failures-six-acquisition-cap-and-exact-native-publication-truth");
        bool literal = false;
        if (item.paste && item.release)
        {
            const auto* pasted = owner->_session.ActiveLayer();
            std::array<std::uint8_t, 4> pixel{};
            if (pasted && pasted->asset && pasted->asset.value().image.Width() == 1 &&
                pasted->asset.value().image.Height() == 1)
            {
                pasted->asset.value().image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
                literal = pixel == std::array<std::uint8_t, 4>{17, 33, 65, 255};
            }
        }
        else if (!item.paste)
        {
            const auto& copied = owner->_session.Clipboard();
            std::array<std::uint8_t, 4> pixel{};
            if (copied && copied.value().image.Width() == 1 && copied.value().image.Height() == 1)
            {
                copied.value().image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
                literal = copied.value().origin == Vec2{1, 0} &&
                          pixel == std::array<std::uint8_t, 4>{20, 40, 80, 255} &&
                          owner->_session.ClipboardSerial() == serial + 1;
            }
        }
        else
            literal = owner->_session.ActiveLayer()->asset.value().image == source &&
                      owner->_session.ClipboardSerial() == serial;
        expect(literal, "independent-native-dib-or-private-copy-pixels-preserved");
        expect(receipt.Read().outcome == (item.paste && !item.release ? HostOutcome::Failed : HostOutcome::Succeeded) &&
                   owner->_session.history.UndoCount() == (item.paste && item.release ? 1 : 0) &&
                   (!item.paste || !item.release || owner->_session.history.UndoName() == L"Paste") &&
                   (item.paste || owner->_clipboardWriteSucceeded == item.release),
               "exact-public-outcome-and-atomic-history-with-private-copy-surviving-mirror-exhaustion");
        expect(!owner->_clipboardTicket && !owner->_clipboardStamp && !owner->_historyWork.Busy() &&
                   !owner->_retryWait.HasPending() && !owner->_retryWait.HasReady() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                   gate->retiredOwnerNodes.load() == 0,
               "original-receivers-barriers-and-memory-retired");
        (void)SetEvent(holder.release.get());
        (void)SetEvent(holder.stop.get());
        DWORD threadExit = STILL_ACTIVE;
        expect(WaitForSingleObject(thread.get(), 10000) == WAIT_OBJECT_0 &&
                   GetExitCodeThread(thread.get(), &threadExit) && threadExit == 0 && holder.closedCleanly.load() &&
                   holder.error.load() == ERROR_SUCCESS && !holder.window.load(),
               "external-window-thread-and-open-scope-joined-cleanly");
        if (retry.forceHandoff)
        {
            const auto nativeFact = [&](bool value, const char* name) noexcept
            {
                failures += value ? 0u : 1u;
                std::printf("%s clipboard-real-contention.paste-read-handoff.%s\n", value ? "PASS" : "FAIL", name);
            };
            nativeFact(retry.secondStarted && retry.secondReady && retry.secondHeld &&
                           retry.secondBusyObserved.load() && retry.armed >= 2 && retry.witness.HandoffCount() == 1 &&
                           retry.witness.LastAttempt() >= 3 && gate->openAttempts.load() >= 4 &&
                           gate->visits.load() == 1,
                       "second-native-owner-forces-read-busy-after-successful-inspection");
            nativeFact(retry.sameTicket && retry.originalBarrier && retry.selectedOrdinal != 0 &&
                           retry.selectedDocumentSerial != 0 && gate->captureReservationHeld,
                       "new-read-ticket-preserves-selected-ordinal-history-and-full-reservation");
            (void)SetEvent(secondHolder.release.get());
            (void)SetEvent(secondHolder.stop.get());
            DWORD secondExit = STILL_ACTIVE;
            nativeFact(retry.secondThread && WaitForSingleObject(retry.secondThread.get(), 10000) == WAIT_OBJECT_0 &&
                           GetExitCodeThread(retry.secondThread.get(), &secondExit) && secondExit == 0 &&
                           secondHolder.closedCleanly.load() && secondHolder.error.load() == ERROR_SUCCESS &&
                           !secondHolder.window.load(),
                       "second-owned-window-thread-and-native-open-scope-joined");
        }
        const bool restored = SystemClipboard::RestoreContents(owner->_window.get(), saved);
        savedReady = !restored;
        owner->Shutdown();
        expect(restored && owner->_shutdownComplete && owner->_jobs.IsIdle() && !owner->_jobs.WakeHandle(),
               "saved-clipboard-restored-and-owner-joined");
        ++reached;
    }
    scenario = "close";
    expect(reached == scenarios.size() && checks == 52 && failures == 0, "all-four-native-contention-cases-reached");
    return failures ? 5 : 0;
}

#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

namespace
{
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
        holder.installDib = item.paste;
        struct Retry final
        {
            ClipboardHolder& holder;
            Application* owner = nullptr; // accessed only by the owner-side armed callback
            bool releaseOnArm = false;
            unsigned armed = 0;
            unsigned heldWaits = 0, closedWaits = 0;
            std::optional<JobTicket> ticket;
            bool sameTicket = true;
            bool actualHolder = true;
            bool originalBarrier = true;
            bool signalSucceeded = true;
            std::atomic<bool> callbackFailed{false};
        } retry{holder};
        retry.releaseOnArm = item.release;
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.retryWait.context = &retry;
        probe.retryWait.armed = [](void* raw, JobTicket ticket, unsigned number, std::uint32_t delay) noexcept
        {
            auto& observed = *static_cast<Retry*>(raw);
            auto& app = *observed.owner;
            ++observed.armed;
            if (!observed.ticket)
                observed.ticket = ticket;
            observed.sameTicket = observed.sameTicket && observed.ticket.value() == ticket &&
                                  number == observed.armed && delay == RetryWaitQueue::DelayMs(number);
            // Only the first release wait is caused by our held clipboard. Inspect and Copy share
            // six native acquisitions; an unrelated desktop owner may deny a later acquisition after
            // our holder has closed. Every later wait must still retain the same ticket and barriers.
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
                if (thread && WaitForSingleObject(thread.get(), 10000) != WAIT_OBJECT_0)
                    std::_Exit(5); // borrowed thread context must not die after a failed join
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
        if (!expect(owner && holder.ready && holder.release && holder.closed && holder.stop,
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
        gate->phase = ClipboardWorkPhase::CopyPublicationSucceeded;
        if (!gate->release || !SetEvent(gate->release.get()))
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
        if (!expect(SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled,
                    "production-job-and-real-event-wait-acknowledged"))
            return 5;
        if (!((item.release ? retry.armed >= 1 && retry.armed <= 5 && retry.heldWaits == 1 &&
                                  retry.closedWaits + 1 == retry.armed
                            : retry.armed == 5 && retry.heldWaits == 5 && retry.closedWaits == 0) &&
              retry.sameTicket && retry.actualHolder && retry.originalBarrier && retry.signalSucceeded &&
              !retry.callbackFailed.load()))
            std::printf("DIAG clipboard-holder-retry scenario=%s armed=%u same=%u holder=%u barrier=%u signals=%u "
                        "callback=%u opens=%u\n",
                        scenario, retry.armed, retry.sameTicket ? 1u : 0u, retry.actualHolder ? 1u : 0u,
                        retry.originalBarrier ? 1u : 0u, retry.signalSucceeded ? 1u : 0u,
                        retry.callbackFailed.load() ? 1u : 0u, gate->openAttempts.load());
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

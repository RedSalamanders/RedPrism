#include "Application.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CursorGate final
{
    wil::unique_event_nothrow reached;
    wil::unique_event_nothrow release;
    std::atomic<bool> scarce = false;
    std::atomic<unsigned> visits = 0;
    std::atomic<unsigned> retirements = 0;
    std::atomic<DWORD> worker = 0;
    std::atomic<DWORD> retireWorker = 0;
};
#pragma warning(pop)

DocumentSelection CursorOutline(bool hole)
{
    std::vector<SelectionLoop> loops{{{1, 1}, {7, 1}, {7, 7}, {1, 7}}};
    if (hole)
        loops.push_back({{3, 3}, {3, 5}, {5, 5}, {5, 3}});
    return {false, SelectionLoops{std::move(loops)}};
}
} // namespace

int Application::RunStandaloneSelectionCursorSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s selection-cursor-host.%s\n", value ? "PASS" : "FAIL", label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept { std::printf("SUMMARY selection-cursor-host checks=%u failures=%u\n", checks, failures); });
    // Exercise ordinary Run startup without opting in the prepared display/selection test engines.
    // The later held-worker cases exercise the same admitted cursor handler and joined lifetime.
    {
        struct Startup final
        {
            Application* owner = nullptr;
            bool visited = false;
            bool cursorOnly = false;
        } startup;
        ApplicationProcessingTestProbe ordinaryProbe;
        ordinaryProbe.workerCount = 2;
        ordinaryProbe.context = &startup;
        ordinaryProbe.afterRuntimeSources = [](void* raw) noexcept -> std::int32_t
        {
            auto& observed = *static_cast<Startup*>(raw);
            observed.visited = true;
            observed.cursorOnly =
                observed.owner->_asyncSelectionCursorEnabled && !observed.owner->_committedDisplayInitialized &&
                !observed.owner->_preparedLayerRowsEnabled && !observed.owner->_selectionOutlineInitialized;
            return E_ABORT; // Stop before showing a window or accepting ordinary user input.
        };
        auto ordinary = std::make_unique<Application>(instance, true, nullptr, 0);
        startup.owner = ordinary.get();
        ordinary->_quietUi = true;
        ordinary->_prompts = &QuietPrompts();
        ordinary->_processingTestProbe = &ordinaryProbe;
        const auto joined = wil::scope_exit(
            [&]() noexcept
            {
                ordinary->Shutdown();
                ordinary.reset();
                while (PumpMessages().quit)
                {
                }
            });
        if (!expect(SUCCEEDED(ordinary->InitializeProcessing()), "ordinary-cursor-startup-processing"))
            return 5;
        expect(ordinary->Run(SW_HIDE) == 5 && startup.visited && startup.cursorOnly,
               "ordinary-run-enables-independent-cursor-worker-before-input");
    }
    CursorGate gate;
    if (!expect(SUCCEEDED(gate.reached.create(wil::EventOptions::ManualReset)) &&
                    SUCCEEDED(gate.release.create(wil::EventOptions::ManualReset)),
                "fixture-events"))
        return 5;
    const auto ownerThread = GetCurrentThreadId();
    ApplicationProcessingTestProbe probe;
    probe.workerCount = 2;
    probe.memory = {[](void* raw, MemorySnapshot& value) noexcept
                    {
                        const auto available = static_cast<CursorGate*>(raw)->scarce.load()
                                                   ? 0ULL
                                                   : kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024;
                        value = {available, available};
                        return true;
                    },
                    &gate};
    auto owner = std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            gate.release.SetEvent();
            if (owner)
            {
                owner->Shutdown();
                owner.reset();
            }
            while (PumpMessages().quit)
            {
            }
        });
    if (!expect(bool(owner), "owner-allocated"))
        return 5;
    owner->_processingTestProbe = &probe;
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()),
                "actual-hidden-warp-chrome-two-worker-owner"))
        return 5;
    owner->_asyncSelectionCursorEnabled = true;
    if (!owner->_cursorRevision)
        ++owner->_cursorRevision;
    owner->_selectionCursorWorkerProbe = {&gate,
                                          [](void* raw, const JobContext& context) noexcept
                                          {
                                              auto& state = *static_cast<CursorGate*>(raw);
                                              state.worker = GetCurrentThreadId();
                                              ++state.visits;
                                              state.reached.SetEvent();
                                              const HANDLE events[]{state.release.get(),
                                                                    static_cast<HANDLE>(context.shutdownEvent)};
                                              (void)WaitForMultipleObjects(2, events, FALSE, INFINITE);
                                          },
                                          [](void* raw) noexcept
                                          {
                                              auto& state = *static_cast<CursorGate*>(raw);
                                              state.retireWorker = GetCurrentThreadId();
                                              ++state.retirements;
                                          }};
    const auto replace = [&](bool hole)
    {
        DocumentFixture fixture;
        fixture.width = fixture.height = 64;
        fixture.selection = CursorOutline(hole);
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), std::nullopt);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    const auto reached = [&]() noexcept
    {
        const auto deadline = GetTickCount64() + 10000;
        while (WaitForSingleObject(gate.reached.get(), 0) != WAIT_OBJECT_0)
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (WaitForSingleObject(gate.reached.get(), 0) == WAIT_OBJECT_0)
                return true;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            const HANDLE handles[]{gate.reached.get(), owner->_jobs.WakeHandle()};
            if (MsgWaitForMultipleObjectsEx(2, handles, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                return false;
        }
        return true;
    };
    const auto resetGate = [&]() noexcept
    {
        gate.reached.ResetEvent();
        gate.release.ResetEvent();
    };
    replace(true);
    resetGate();
    const auto preCaptureCount = owner->_selectionCursorCaptures;
    const auto preCaptureCallbacks = owner->_selectionCursorCompletedCallbacks;
    const auto preCaptureVisits = gate.visits.load();
    if (!expect(!owner->CursorOverSelection({2, 2}) && owner->_selectionCursorTicket.has_value(),
                "pre-capture-cancel-has-original-submitted-ticket"))
        return 5;
    const auto preCaptureTicket = owner->_selectionCursorTicket;
    // No completion drain has run: source capture is impossible even if the no-op producer returned.
    (void)SendMessageW(owner->_window.get(), WM_MOUSELEAVE, 0, 0);
    expect(owner->_selectionCursorTicket == preCaptureTicket && !owner->_selectionCursorRequests.Latest() &&
               owner->_selectionCursorCaptures == preCaptureCount,
           "pre-capture-leave-retains-original-without-source");
    expect(settle() && !owner->_selectionCursorTicket && !owner->_selectionCursorRequests.Busy() &&
               owner->_selectionCursorCaptures == preCaptureCount && gate.visits == preCaptureVisits &&
               owner->_selectionCursorCompletedCallbacks == preCaptureCallbacks + 1,
           "pre-capture-cancel-exact-ack-clears-owner-without-useful-stage");
    constexpr std::array queries{Vec2{2, 2}, Vec2{4, 4}, Vec2{1, 3}, Vec2{1, 1}};
    constexpr std::array results{true, false, true, true}; // Literal winding and on-segment expectations.
    constexpr std::array pendingLabels{"interior-pending", "hole-pending", "edge-pending", "corner-pending"};
    constexpr std::array reachedLabels{"interior-worker-reached", "hole-worker-reached", "edge-worker-reached",
                                       "corner-worker-reached"};
    constexpr std::array resultLabels{"interior-result-and-no-repeat-work", "hole-result-and-no-repeat-work",
                                      "edge-result-and-no-repeat-work", "corner-result-and-no-repeat-work"};
    for (std::size_t i = 0; i < queries.size(); ++i)
    {
        resetGate();
        const auto dispatches = owner->_selectionCursorDispatches;
        if (!expect(!owner->CursorOverSelection(queries[i]), pendingLabels[i]) || !expect(reached(), reachedLabels[i]))
            return 5;
        gate.release.SetEvent();
        const bool done = settle();
        const auto current = owner->_selectionCursorRequests.Latest();
        expect(done && current &&
                   owner->_selectionCursorRequests.Observe(current.value().key).state ==
                       SelectionCursorRequests::State::Ready &&
                   owner->CursorOverSelection(queries[i]) == results[i] &&
                   owner->CursorOverSelection(queries[i]) == results[i] &&
                   owner->_selectionCursorDispatches == dispatches + 1 && !owner->_selectionCursorTicket,
               resultLabels[i]);
    }
    expect(gate.worker.load() != ownerThread && gate.retireWorker.load() != ownerThread && gate.visits == 4 &&
               gate.retirements == 4 && owner->_selectionCursorCaptures == 4,
           "geometry-and-last-captured-reader-retire-off-owner");
    const auto beforeOutside = owner->_selectionCursorDispatches;
    expect(!owner->CursorOverSelection({9, 9}) && !owner->_selectionCursorRequests.Latest() &&
               owner->_selectionCursorDispatches == beforeOutside,
           "outside-bounds-does-not-submit");
    expect(!owner->CursorOverSelection({(std::numeric_limits<double>::quiet_NaN)(), 2}) &&
               !owner->_selectionCursorRequests.Latest() && owner->_selectionCursorDispatches == beforeOutside,
           "nonfinite-does-not-submit");

    resetGate();
    if (!expect(!owner->CursorOverSelection({2, 2}) && reached(), "latest-original-captured-and-held"))
        return 5;
    const auto original = owner->_selectionCursorTicket;
    const auto beforeLatest = owner->_selectionCursorDispatches;
    expect(!owner->CursorOverSelection({4, 4}) && !owner->CursorOverSelection({6, 6}) &&
               owner->_selectionCursorTicket == original && owner->_selectionCursorDispatches == beforeLatest &&
               owner->_selectionCursorRequests.NeedsCancellation(),
           "latest-replacement-keeps-original-until-ack");
    gate.release.SetEvent();
    expect(settle() && owner->_selectionCursorDispatches == beforeLatest + 1 && owner->CursorOverSelection({6, 6}) &&
               !owner->_selectionCursorTicket,
           "latest-only-query-publishes-after-original-ack");

    // Same bounds and query, changed geometry before any coalesced chrome epoch publication.
    replace(false);
    gate.release.SetEvent();
    expect(!owner->CursorOverSelection({4, 4}) && settle() && owner->CursorOverSelection({4, 4}),
           "generation-original-ready");
    const auto epoch = owner->_cursorRevision;
    replace(true);
    const auto beforeGeneration = owner->_selectionCursorDispatches;
    expect(!owner->CursorOverSelection({4, 4}) && owner->_cursorRevision == epoch && settle() &&
               !owner->CursorOverSelection({4, 4}) && owner->_selectionCursorDispatches == beforeGeneration + 1,
           "generation-invalidates-same-point-same-bounds-before-chrome-epoch");

    // The old source remains retained only by the useful worker while its document is replaced.
    resetGate();
    if (!expect(!owner->CursorOverSelection({2, 2}) && reached(), "stale-source-worker-held"))
        return 5;
    auto weakSource = owner->_session.Selection().value().loops.WeakStorageForTest();
    replace(false);
    expect(!weakSource.expired(), "stale-source-retained-through-active-worker");
    gate.release.SetEvent();
    expect(settle() && weakSource.expired() && !owner->_selectionCursorTicket &&
               !owner->_selectionCursorRequests.Begin(),
           "stale-completion-cannot-publish-or-retry-old-source");

    resetGate();
    if (!expect(!owner->CursorOverSelection({2, 2}) && reached(), "leave-worker-held"))
        return 5;
    const auto beforeLeave = owner->_selectionCursorDispatches;
    (void)SendMessageW(owner->_window.get(), WM_MOUSELEAVE, 0, 0);
    expect(!owner->_selectionCursorRequests.Latest() && owner->_selectionCursorTicket.has_value(),
           "actual-pointer-leave-invalidates-but-retains-ticket");
    gate.release.SetEvent();
    expect(settle() && !owner->_selectionCursorTicket && owner->_selectionCursorDispatches == beforeLeave,
           "leave-ack-does-not-resubmit");

    resetGate();
    if (!expect(!owner->CursorOverSelection({2, 2}) && reached(), "focus-worker-held"))
        return 5;
    (void)SendMessageW(owner->_window.get(), WM_KILLFOCUS, 0, 0);
    expect(!owner->_selectionCursorRequests.Latest() && owner->_selectionCursorTicket.has_value(),
           "actual-focus-loss-invalidates-original");
    gate.release.SetEvent();
    expect(settle() && !owner->_selectionCursorTicket, "focus-loss-worker-ack");

    DocumentFixture largeFixture;
    largeFixture.width = largeFixture.height = 64;
    SelectionLoop largeLoop;
    largeLoop.reserve(65540);
    for (unsigned i = 0; i < 65536; ++i)
        largeLoop.push_back({1.0 + 6.0 * i / 65535.0, 2});
    largeLoop.insert(largeLoop.end(), {{7, 2}, {7, 6}, {1, 6}, {1, 2}});
    largeFixture.selection = DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{std::move(largeLoop)}}};
    owner->_session.ReplaceDocument(std::move(largeFixture).Seal(), std::nullopt);
    const auto largeIdentity = owner->_session.Selection().value().loops.Identity();
    resetGate();
    const auto largeDispatches = owner->_selectionCursorDispatches;
    if (!expect(!owner->CursorOverSelection({2, 3}) && reached(), "large-65540-point-source-reaches-useful-worker"))
        return 5;
    const auto largeVisits = gate.visits.load();
    const auto largeTicket = owner->_selectionCursorTicket;
    bool allPending = true;
    for (unsigned i = 0; i < 20; ++i)
        allPending = !owner->CursorOverSelection({2.0 + i / 16.0, 3}) && allPending;
    expect(allPending && gate.visits == largeVisits && owner->_selectionCursorDispatches == largeDispatches + 1 &&
               owner->_selectionCursorTicket == largeTicket &&
               owner->_session.Selection().value().loops.Identity() == largeIdentity,
           "large-rapid-queries-retain-one-original-and-only-latest-scalar");
    gate.release.SetEvent();
    expect(settle() && owner->CursorOverSelection({2.0 + 19.0 / 16.0, 3}) &&
               owner->_selectionCursorDispatches == largeDispatches + 2 && gate.visits == largeVisits + 1,
           "large-latest-only-exact-result-after-original-retirement");
    expect(!owner->CursorOverSelection({3, 3}) && settle() && owner->CursorOverSelection({3, 3}),
           "deferred-source-original-point-ready");
    if (!expect(owner->_session.BeginDeferredSelectionMove(), "actual-deferred-outline-move-begins"))
        return 5;
    owner->_session.MoveSelection(5, -2);
    const auto translated = owner->_session.DescribeSelectionCursorProbe();
    expect(translated && translated.value().translation == Vec2{5, -2} &&
               owner->_session.Selection().value().loops.Identity() == largeIdentity &&
               !owner->CursorOverSelection({3, 3}),
           "deferred-old-point-refuses-without-materializing-original-root");
    expect(!owner->CursorOverSelection({8, 1}) && settle() && owner->CursorOverSelection({8, 1}) &&
               owner->_session.Selection().value().loops.Identity() == largeIdentity,
           "deferred-worker-probe-uses-exact-translated-vertices");
    owner->_session.CancelSelectionMove();
    replace(false);

    gate.scarce = true;
    const auto refusedDispatch = owner->_selectionCursorDispatches;
    const auto refusedCapture = owner->_selectionCursorCaptures;
    expect(!owner->CursorOverSelection({2, 2}) && settle() && !owner->_selectionCursorTicket &&
               !owner->_selectionCursorRequests.Busy() && owner->_selectionCursorCaptures == refusedCapture,
           "memory-refusal-acknowledges-original-without-capture");
    gate.scarce = false;
    expect(!owner->CursorOverSelection({2, 2}) && owner->_selectionCursorDispatches == refusedDispatch + 1 &&
               owner->_selectionCursorCaptures == refusedCapture,
           "same-refused-query-never-spins-when-memory-recovers");
    gate.release.SetEvent();
    expect(!owner->CursorOverSelection({2, 3}) && settle() && owner->CursorOverSelection({2, 3}),
           "new-query-can-retry-recovered-memory");

    owner->_selectionCursorRequests = SelectionCursorRequests{(std::numeric_limits<std::uint64_t>::max)()};
    const auto exhaustedDispatch = owner->_selectionCursorDispatches;
    expect(!owner->CursorOverSelection({2, 2}) && !owner->_selectionCursorRequests.Latest() &&
               owner->_selectionCursorDispatches == exhaustedDispatch,
           "exhausted-serial-refuses-without-wrap");
    owner->_selectionCursorRequests = SelectionCursorRequests{};
    resetGate();
    if (!expect(!owner->CursorOverSelection({2, 2}) && reached(), "joined-close-useful-worker-held"))
        return 5;
    auto joinedWeak = owner->_session.Selection().value().loops.WeakStorageForTest();
    replace(false);
    const auto callbacks = owner->_selectionCursorCompletedCallbacks;
    const auto retired = gate.retirements.load();
    expect(!joinedWeak.expired() && owner->_selectionCursorTicket.has_value(),
           "joined-close-holds-source-and-original-ticket");
    owner->Shutdown(); // shutdownEvent releases the real useful worker; no manual release/timeout acknowledgement.
    expect(joinedWeak.expired() && gate.retirements == retired + 1 && gate.retireWorker.load() != ownerThread &&
               owner->_selectionCursorRequests.Closed() && !owner->_selectionCursorRequests.Busy() &&
               !owner->_selectionCursorTicket && owner->_selectionCursorCompletedCallbacks == callbacks,
           "joined-close-retires-source-before-exact-ack-and-suppresses-callback");
    owner->Shutdown();
    expect(!owner->CursorOverSelection({2, 2}) && owner->_selectionCursorRequests.Closed(),
           "closed-owner-cannot-restart");
    return failures ? 5 : 0;
}

#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>

namespace
{
bool PressLiteral(const std::optional<DocumentSelection>& selection, bool moved, bool overflow = false) noexcept
{
    constexpr std::array<Vec2, 4> original{{{8, 8}, {40, 8}, {40, 40}, {8, 40}}};
    constexpr std::array<Vec2, 4> translated{{{16, 12}, {48, 12}, {48, 44}, {16, 44}}};
    if (!selection || selection->antialiased || selection->loops.size() != 1 || selection->loops[0].size() != 4)
        return false;
    constexpr std::array<Vec2, 4> overflowed{{{519, 8}, {551, 8}, {551, 40}, {519, 40}}};
    const auto& expected = overflow ? overflowed : moved ? translated : original;
    for (std::size_t i = 0; i < 4; ++i)
        if (selection->loops[0][i] != expected[i])
            return false;
    return true;
}
} // namespace
int Application::RunStandaloneSelectionPressGestureSelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0, failures = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s application-selection-press.%s.%s\n", value ? "PASS" : "FAIL", scenario, label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY application-selection-press checks=%u failures=%u\n", checks, failures); });
    bool refuseMemory = false;
    ApplicationProcessingTestProbe probe;
    probe.workerCount = 2;
    probe.memory = {[](void* raw, MemorySnapshot& out) noexcept
                    {
                        const auto available = *static_cast<bool*>(raw) ? 0ULL : 8ULL * 1024 * 1024 * 1024;
                        out = {available, available};
                        return true;
                    },
                    &refuseMemory};
    auto owner = std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner)
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            }
        });
    if (!expect(bool(owner), "real-owner-allocated"))
        return 5;
    owner->_processingTestProbe = &probe;
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                    SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                    SUCCEEDED(owner->InitializeChrome()),
                "real-hidden-warp-window-chrome-and-two-worker-owner"))
        return 5;
    if (!expect(owner->EnableCommittedDisplayForTest() && owner->EnableSelectionOutlineForTest(),
                "real-display-owners-enabled"))
        return 5;
    owner->_selectionPressTestEnabled = true;
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, 30000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    const auto until = [&](auto done) noexcept
    {
        const auto deadline = GetTickCount64() + 10000;
        while (!done())
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (done())
                return true;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            const HANDLE wake = owner->_jobs.WakeHandle();
            if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                return false;
        }
        return true;
    };
    constexpr std::array<const char*, 6> cases{"early-up",  "moved-up", "moved-lost",
                                               "cancelled", "refused",  "overflow"};
    for (unsigned mode = 0; mode < cases.size(); ++mode)
    {
        scenario = cases[mode];
        DocumentFixture fixture;
        fixture.width = 64;
        fixture.height = 64;
        fixture.layers.push_back(ImageLayer::Blank(L"Physical press", {64, 64}));
        fixture.selection = DocumentSelection{false, SelectionLoops{SelectionLoop{{8, 8}, {40, 8}, {40, 40}, {8, 40}}}};
        const auto active = fixture.layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_tool = EditorTool::Marquee;
        owner->_session.marqueeKind = LassoKind::Rectangle;
        owner->_session.selectionModeChoice = SelectionMode::New;
        owner->_session.selectionAntialiased = false;
        owner->_viewport.zoom = 1.0;
        owner->DocumentChanged();
        if (!expect(settle(), "fixture-and-first-display-settled"))
            return 5;
        owner->RenderFrame();
        const auto generation = owner->_session.Generation();
        const auto entries = owner->_session.history.UndoCount();
        const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
        {
            const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
            const auto hole = owner->_chrome->HolePixels();
            const int x = static_cast<int>(std::lround(hole.left + view.x)),
                      y = static_cast<int>(std::lround(hole.top + view.y));
            SendMessageW(owner->_window.get(), message, modifiers, MAKELPARAM(x, y));
        };
        refuseMemory = mode == 4;
        send(WM_LBUTTONDOWN, {24, 24}, MK_LBUTTON);
        if (!expect(owner->_selectionPressInput.Busy() &&
                        owner->_selectionPressReceipt.Read().outcome == HostOutcome::Pending &&
                        !owner->_session.IsMovingSelection(),
                    "wndproc-down-records-only-fixed-descriptor-before-opening"))
            return 5;
        if (mode == 1 || mode == 2 || mode == 3)
            send(WM_MOUSEMOVE, {32, 28}, MK_LBUTTON);
        if (mode == 5)
        {
            for (unsigned point = 1; point <= 511; ++point)
                send(WM_MOUSEMOVE, {24.0 + point, 24}, MK_LBUTTON);
            send(WM_MOUSEMOVE, {9999, 24}, MK_LBUTTON); // Refused point must not replace the accepted prefix.
            ReleaseCapture();
        }
        else if (mode == 2)
            ReleaseCapture();
        else if (mode == 3)
        {
            const bool captureBeforeWait = GetCapture() == owner->_window.get();
            const bool trackingBeforeWait = owner->_mouseTracked;
            if (!until(
                    [&]() noexcept
                    {
                        return owner->_selectionPressState && owner->_selectionPressState->waiting &&
                               owner->_selectionPressState->moves == 1;
                    }))
            {
                const auto* stopped = owner->_selectionPressState ? &owner->_selectionPressState.value() : nullptr;
                std::fprintf(
                    stderr,
                    "DIAG selection-press.cancelled wait: capture-before=%d tracking-before=%d capture-now=%d "
                    "tracking-now=%d busy=%d sampling=%d pending=%zu receipt=%d state=%d controller=%d "
                    "waiting=%d opened=%d closed=%d moves=%u terminal-lost=%d completed=%d aborted=%d "
                    "source-current=%d status=%d error=%08lx\n",
                    captureBeforeWait, trackingBeforeWait, GetCapture() == owner->_window.get(), owner->_mouseTracked,
                    owner->_selectionPressInput.Busy(), owner->_selectionPressInput.Sampling(),
                    static_cast<std::size_t>(owner->_selectionPressInput.PendingCount()),
                    static_cast<int>(owner->_selectionPressReceipt.Read().outcome), stopped != nullptr,
                    stopped && stopped->controller, stopped && stopped->waiting, stopped && stopped->opened,
                    stopped && stopped->closed, stopped ? stopped->moves : 0u, stopped && stopped->terminalLost,
                    stopped && stopped->completed, stopped && stopped->aborted, stopped && stopped->sourceCurrent,
                    stopped ? static_cast<int>(stopped->outcome.status) : -1,
                    stopped ? static_cast<unsigned long>(stopped->outcome.error) : 0ul);
                return 5;
            }
            owner->CancelHostContinuations(false);
            if (GetCapture() == owner->_window.get())
                ReleaseCapture();
        }
        else
            send(WM_LBUTTONUP, {32, 28}, 0);
        if (!expect(settle(), "physical-prefix-original-job-and-terminal-settle"))
            return 5;
        const auto result = owner->_selectionPressReceipt.Read();
        if (!expect(result.outcome == (mode == 3   ? HostOutcome::Cancelled
                                       : mode == 4 ? HostOutcome::Failed
                                                   : HostOutcome::Succeeded) &&
                        !owner->_selectionPressInput.Busy() && !owner->_session.IsMovingSelection() &&
                        !owner->_historyWork.Busy(),
                    "exact-receipt-and-history-barrier-acknowledged"))
            return 5;
        const bool geometry = mode == 0 ? !owner->_session.Selection()
                                        : PressLiteral(owner->_session.Selection(), mode == 1 || mode == 2, mode == 5);
        const auto mutations = mode == 4 ? 0u : mode == 3 ? 3u : mode == 5 ? 512u : 2u;
        if (!expect(geometry && owner->_session.Generation() == generation + mutations &&
                        owner->_session.history.UndoCount() == entries + (mode < 3 || mode == 5 ? 1 : 0),
                    "literal-geometry-generation-and-single-history-entry"))
            return 5;
        const auto& state = owner->_selectionPressState;
        if (!expect(state && !state->controller &&
                        (mode == 4 ? !state->opened && state->captures == 0
                                   : state->completed && state->sameTicket && state->captures == 1 &&
                                         state->hits == 1 && state->inputAcknowledged) &&
                        owner->_selectionPressMovePresentations ==
                            (mode == 1 || mode == 2 || mode == 3 || mode == 5 ? 1u : 0u) &&
                        (mode != 5 || (state->moves == 511 && state->overflowObserved && state->terminalLost)),
                    "real-host-uses-qualified-reusable-original-parent-engine"))
            return 5;
        refuseMemory = false;
    }
    scenario = "close";
    owner->Shutdown();
    if (!expect(owner->_jobs.IsIdle(), "joined-close-has-no-active-job"))
        return 5;
    owner.reset();
    while (PumpMessages().quit)
    {
    }
    if (!expect(checks == 40 && failures == 0, "all-physical-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}

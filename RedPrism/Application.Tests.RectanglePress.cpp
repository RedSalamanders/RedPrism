#include "Application.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.ApplicationProcessingTestProbe.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>
#include <span>

int Application::RunStandaloneRectanglePressSelfTest(HINSTANCE instance) noexcept
{
    return RunStandaloneMarqueePressSelfTest(instance, LassoKind::Rectangle);
}
int Application::RunStandaloneEllipsePressSelfTest(HINSTANCE instance) noexcept
{
    return RunStandaloneMarqueePressSelfTest(instance, LassoKind::Ellipse);
}
int Application::RunStandaloneMarqueePressSelfTest(HINSTANCE instance, LassoKind kind) noexcept
{
    KeyboardState::ScopedSynthetic keyboard; // Physical keys cannot change this standalone driver's input.
    std::array<BYTE, 256> nativeKeys{};
    if (!GetKeyboardState(nativeKeys.data()))
        return 5;
    auto restoreKeys = wil::scope_exit([&]() noexcept { (void)SetKeyboardState(nativeKeys.data()); });
    const bool ellipse = kind == LassoKind::Ellipse;
    const char* family = ellipse ? "application-ellipse-press" : "application-rectangle-press";
    unsigned checks = 0, failures = 0;
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s %s.%s.%s\n", value ? "PASS" : "FAIL", family, scenario, label);
        return value;
    };
    auto summary = wil::scope_exit([&]() noexcept
                                   { std::printf("SUMMARY %s checks=%u failures=%u\n", family, checks, failures); });
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
    constexpr std::array rectangleCases{"absent", "empty",     "outside",     "click-old",     "click-absent",
                                        "lost",   "cancelled", "refused",     "active-target", "overflow",
                                        "shift",  "alt",       "joined-close"};
    constexpr std::array ellipseCases{"absent", "empty",     "outside", "click-old",     "click-absent",
                                      "lost",   "cancelled", "refused", "active-target", "overflow",
                                      "shift",  "alt",       "inside",  "joined-close"};
    const auto cases =
        ellipse ? std::span<const char* const>{ellipseCases} : std::span<const char* const>{rectangleCases};
    unsigned reached = 0;
    for (unsigned mode = 0; mode < cases.size(); ++mode)
    {
        scenario = cases[mode];
        const bool original = mode >= 2 && mode != 4;
        const bool cancel = mode == 6, refused = mode == 7, target = mode == 8;
        const bool overflow = mode == 9, shift = mode == 10, alt = mode == 11, joined = mode == cases.size() - 1,
                   lost = mode == 5 || overflow;
        const bool click = mode == 3 || mode == 4;
        const bool inside = ellipse && mode == 12;
        DocumentFixture fixture;
        fixture.width = fixture.height = 64;
        fixture.layers.push_back(ImageLayer::Blank(L"Rectangle press", {64, 64}));
        fixture.layers.push_back(ImageLayer::Blank(L"Other target", {64, 64}));
        if (original)
            fixture.selection =
                DocumentSelection{false, SelectionLoops{SelectionLoop{{8, 8}, {40, 8}, {40, 40}, {8, 40}}}};
        else if (mode == 1)
            fixture.selection = DocumentSelection{false, SelectionLoops{}};
        const auto active = fixture.layers[0].id, other = fixture.layers[1].id;
        owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
        owner->_tool = EditorTool::Marquee;
        owner->_session.marqueeKind = kind;
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
            SendMessageW(owner->_window.get(), message, modifiers,
                         MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                    static_cast<int>(std::lround(hole.top + view.y))));
        };
        refuseMemory = refused;
        send(WM_LBUTTONDOWN, inside ? Vec2{24, 24} : Vec2{50, 50}, MK_LBUTTON);
        if (!expect(owner->_selectionPressInput.Busy() &&
                        owner->_selectionPressReceipt.Read().outcome == HostOutcome::Pending &&
                        !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection(),
                    inside ? "wndproc-inside-down-records-only-fixed-descriptor"
                           : "wndproc-outside-down-records-only-fixed-descriptor"))
            return 5;
        if (target)
            owner->_session.SelectLayer(other);
        if (!click && !refused && !target)
        {
            if (overflow)
            {
                for (unsigned i = 1; i <= 511; ++i)
                    send(WM_MOUSEMOVE, {50.0 + i, 50}, MK_LBUTTON);
                send(WM_MOUSEMOVE, {9999, 50}, MK_LBUTTON);
            }
            else
            {
                std::array<BYTE, 256> beforeKeys{};
                if (!KeyboardState::Read(beforeKeys))
                    return 5;
                if (shift || alt)
                {
                    auto heldKeys = beforeKeys;
                    heldKeys[shift ? VK_SHIFT : VK_MENU] = 0x80;
                    KeyboardState::Write(heldKeys);
                }
                send(WM_MOUSEMOVE,
                     inside         ? Vec2{32, 28}
                     : shift || alt ? Vec2{60, 55}
                                    : Vec2{60, 60},
                     static_cast<WPARAM>(MK_LBUTTON | (shift ? MK_SHIFT : 0)));
                if (shift || alt)
                    KeyboardState::Write(beforeKeys);
            }
        }
        bool joinedSourcePreserved = false;
        if (cancel || joined)
        {
            if (!until(
                    [&]() noexcept
                    {
                        return owner->_selectionPressState && owner->_selectionPressState.value().drawing &&
                               owner->_selectionPressState.value().waiting &&
                               owner->_selectionPressState.value().moves == 1;
                    }))
                return 5;
            if (joined)
            {
                joinedSourcePreserved =
                    owner->_session.Selection() &&
                    owner->_session.Selection().value().Bounds() == std::optional<SelectionBounds>{{8, 8, 40, 40}} &&
                    owner->_session.Generation() == generation && owner->_session.history.UndoCount() == entries;
                owner->Shutdown();
            }
            else
                owner->CancelHostContinuations(false);
            if (GetCapture() == owner->_window.get())
                ReleaseCapture();
        }
        else if (lost)
            ReleaseCapture();
        else
        {
            owner->_session.marqueeKind = ellipse ? LassoKind::Rectangle : LassoKind::Ellipse;
            owner->_session.selectionAntialiased = true;
            send(WM_LBUTTONUP, {63, 63}, 0); // Release point is not an invented Move.
        }
        if (!expect(joined ? owner->_shutdownStarted && owner->_jobs.IsIdle() : settle(),
                    "original-parent-or-exact-refusal-settles"))
            return 5;
        const auto outcome = owner->_selectionPressReceipt.Read().outcome;
        const auto wanted = cancel || joined || lost ? HostOutcome::Cancelled
                            : target                 ? HostOutcome::Stale
                            : refused                ? HostOutcome::Failed
                                                     : HostOutcome::Succeeded;
        if (!expect(outcome == wanted && !owner->_selectionPressInput.Busy() && !owner->_session.PendingLasso() &&
                        !owner->_session.IsMovingSelection() && !owner->_selectionPressStamp &&
                        !owner->_historyWork.Busy(),
                    "exact-receipt-prefix-and-history-stamp-acknowledged"))
            return 5;
        const bool changed = !(cancel || joined || lost || refused || target) && !(mode == 4);
        const auto& selection = owner->_session.Selection();
        const bool geometry =
            joined ? joinedSourcePreserved && !owner->_session.Document() && !owner->_session.Selection() &&
                         owner->_jobs.IsIdle()
            : cancel || lost || refused || target
                ? selection && !selection.value().antialiased &&
                      selection.value().Bounds() == std::optional<SelectionBounds>{{8, 8, 40, 40}}
            : inside ? selection && !selection.value().antialiased && selection.value().loops.PointCount() == 4 &&
                           selection.value().Bounds() == std::optional<SelectionBounds>{{16, 12, 48, 44}}
            : click  ? !selection
                     : selection && selection.value().antialiased &&
                           selection.value().loops.PointCount() == (ellipse ? 32u : 4u) &&
                           selection.value().Bounds() ==
                               std::optional<SelectionBounds>{alt ? SelectionBounds{50, 50, 60, 55}
                                                                  : SelectionBounds{50, 50, 60, 60}};
        if (!expect(geometry &&
                        (joined || (owner->_session.Generation() == generation + (inside    ? 2
                                                                                  : changed ? 1
                                                                                            : 0) &&
                                    owner->_session.history.UndoCount() == entries + (changed ? 1 : 0))) &&
                        (target ? owner->_session.ActiveLayerID() == other : true),
                    "literal-event-time-shape-aa-single-edit-source-or-joined-retirement"))
            return 5;
        const auto& state = owner->_selectionPressState;
        if (!expect(target ? !owner->_activeHostRequest
                           : state && !state.value().controller &&
                                 (refused ? !state.value().opened && !state.value().captures
                                          : state.value().drawing == !inside && state.value().sameTicket &&
                                                state.value().captures == 1 && state.value().hits == 1 &&
                                                state.value().inputAcknowledged &&
                                                (joined ? !state.value().completed && state.value().aborted
                                                        : state.value().completed)) &&
                                 (!overflow || (state.value().moves == 511 && state.value().overflowObserved &&
                                                state.value().terminalLost)),
                    "actual-message-route-uses-one-parent-and-preserves-joined-or-refused-lifetimes"))
            return 5;
        refuseMemory = false;
        ++reached;
    }
    scenario = "close";
    owner->Shutdown();
    if (!expect(owner->_jobs.IsIdle(), "joined-close-has-no-active-job"))
        return 5;
    owner.reset();
    while (PumpMessages().quit)
    {
    }
    if (!expect(reached == cases.size() && failures == 0,
                "all-outside-empty-click-loss-cancel-target-and-close-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}

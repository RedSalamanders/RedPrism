#include "Application.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "SelectionOutlineGestureMemory.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

namespace
{
// Independent nonzero winding, with probes deliberately away from grid boundaries.
bool Filled(const DocumentSelection& selection, Vec2 point) noexcept
{
    int winding = 0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.empty())
            continue;
        auto previous = loop.back();
        for (const auto next : loop)
        {
            const auto side =
                (next.x - previous.x) * (point.y - previous.y) - (point.x - previous.x) * (next.y - previous.y);
            if (previous.y <= point.y && next.y > point.y && side > 0)
                ++winding;
            else if (previous.y > point.y && next.y <= point.y && side < 0)
                --winding;
            previous = next;
        }
    }
    return winding != 0;
}
} // namespace

int Application::RunStandaloneMarqueeModesSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::ScopedSynthetic keyboard;
    std::array<BYTE, 256> nativeKeys{};
    if (!GetKeyboardState(nativeKeys.data()))
        return 5;
    auto restoreKeys = wil::scope_exit([&]() noexcept { (void)SetKeyboardState(nativeKeys.data()); });
    unsigned checks = 0;
    unsigned failures = 0;
    unsigned reached = 0;
    const char* family = "setup";
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s marquee-modes-host.%s.%s.%s\n", value ? "PASS" : "FAIL", family, scenario, label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept { std::printf("SUMMARY marquee-modes-host checks=%u failures=%u\n", checks, failures); });
    constexpr std::array cases{"header",        "physical-modifier", "shift-rearm", "large-prior", "absent",
                               "empty",         "no-layers",         "lost",        "cancelled",   "refused",
                               "active-target", "overflow",          "joined-close"};
    for (const auto kind : {LassoKind::Rectangle, LassoKind::Ellipse})
        for (const auto mode : {SelectionMode::Add, SelectionMode::Subtract})
        {
            family = kind == LassoKind::Rectangle
                         ? (mode == SelectionMode::Add ? "rectangle-add" : "rectangle-subtract")
                         : (mode == SelectionMode::Add ? "ellipse-add" : "ellipse-subtract");
            scenario = "setup";
            bool refuseMemory = false;
            ApplicationProcessingTestProbe probe;
            probe.workerCount = 2;
            probe.memory = {[](void* raw, MemorySnapshot& value) noexcept
                            {
                                const auto available = *static_cast<bool*>(raw)
                                                           ? 0ULL
                                                           : kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024;
                                value = {available, available};
                                return true;
                            },
                            &refuseMemory};
            auto owner =
                std::unique_ptr<Application>(new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr));
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
            if (!expect(bool(owner), "owner-allocated"))
                return 5;
            owner->_processingTestProbe = &probe;
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (!expect(SUCCEEDED(owner->InitializeProcessing()) && SUCCEEDED(owner->RegisterWindowClass()) &&
                            SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                            SUCCEEDED(owner->InitializeChrome()),
                        "real-hidden-warp-chrome-two-worker-owner"))
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
            for (const std::string_view name : cases)
            {
                scenario = name.data();
                const bool absent = name == "absent";
                const bool empty = name == "empty";
                const bool noLayers = name == "no-layers";
                const bool large = name == "large-prior";
                const bool physical = name == "physical-modifier" || name == "shift-rearm";
                const bool rearm = name == "shift-rearm";
                const bool lost = name == "lost";
                const bool cancel = name == "cancelled";
                const bool refused = name == "refused";
                const bool target = name == "active-target";
                const bool overflow = name == "overflow";
                const bool joined = name == "joined-close";
                DocumentFixture fixture;
                fixture.width = fixture.height = 64;
                if (!noLayers)
                {
                    fixture.layers.push_back(ImageLayer::Blank(L"Preserving marquee", {64, 64}));
                    fixture.layers.push_back(ImageLayer::Blank(L"Other target", {64, 64}));
                }
                if (empty)
                    fixture.selection = DocumentSelection{false, SelectionLoops{}};
                else if (!absent && !noLayers)
                {
                    SelectionLoop loop;
                    if (large)
                    {
                        loop.reserve(65540);
                        for (unsigned i = 0; i < 65536; ++i)
                            loop.push_back({1.0 + 6.0 * i / 65535.0, 2});
                        loop.insert(loop.end(), {{7, 2}, {7, 6}, {1, 6}, {1, 2}});
                    }
                    else
                        loop = {{1, 2}, {7, 2}, {7, 6}, {1, 6}};
                    fixture.selection =
                        DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{std::move(loop)}}};
                }
                const auto active = noLayers ? std::optional<Guid>{} : std::optional{fixture.layers[0].id};
                const auto other = noLayers ? std::optional<Guid>{} : std::optional{fixture.layers[1].id};
                owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
                owner->_session.history.Reset();
                owner->_tool = EditorTool::Marquee;
                owner->_session.marqueeKind = kind;
                owner->_session.selectionModeChoice = physical ? SelectionMode::New : mode;
                owner->_session.selectionAntialiased = false;
                owner->_viewport.zoom = 1.0;
                owner->DocumentChanged();
                if (!expect(settle(), "fixture-first-display-settled"))
                    return 5;
                owner->RenderFrame();
                const auto original = owner->_session.Selection();
                const auto generation = owner->_session.Generation();
                const auto revision = owner->_session.history.CurrentRevision();
                const auto admitted = DescribeSelectionOutlineGestureMemory(owner->_session, kind, mode);
                if (!expect(admitted &&
                                admitted.value().limits.totalBytes <
                                    (large ? 512ULL * 1024 * 1024 : 32ULL * 1024 * 1024) &&
                                (!large || (original && original.value().loops.PointCount() == 65540)),
                            "cached-complete-admission-fits-normal2gib-query"))
                    return 5;
                const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
                {
                    const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
                    const auto hole = owner->_chrome->HolePixels();
                    SendMessageW(owner->_window.get(), message, modifiers,
                                 MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                            static_cast<int>(std::lround(hole.top + view.y))));
                };
                std::array<BYTE, 256> keys{};
                const bool initialShift = physical && (mode == SelectionMode::Add || rearm);
                const bool initialAlt = physical && mode == SelectionMode::Subtract;
                keys[VK_SHIFT] = initialShift ? 0x80 : 0;
                keys[VK_MENU] = initialAlt ? 0x80 : 0;
                KeyboardState::Write(keys);
                refuseMemory = refused;
                send(WM_LBUTTONDOWN, {3, 3}, MK_LBUTTON | (initialShift ? MK_SHIFT : 0));
                const auto* context =
                    owner->_selectionPressInput.CapturedContext(owner->_selectionPressInput.Opening());
                if (!expect(owner->_selectionPressInput.Busy() &&
                                owner->_selectionPressReceipt.Read().outcome == HostOutcome::Pending && context &&
                                context->kind == kind && context->mode == mode && !owner->_session.PendingLasso() &&
                                !owner->_session.IsMovingSelection(),
                            "wndproc-down-captures-mode-and-kind-without-source-work"))
                    return 5;
                // Every later header setting differs; accepted Down must retain mode/kind.
                owner->_session.selectionModeChoice = SelectionMode::New;
                owner->_session.marqueeKind = kind == LassoKind::Rectangle ? LassoKind::Ellipse : LassoKind::Rectangle;
                if (target)
                    owner->_session.SelectLayer(other);
                if (!refused && !target)
                {
                    send(WM_MOUSEMOVE, {9, 8}, MK_LBUTTON | (initialShift ? MK_SHIFT : 0));
                    if (rearm)
                    {
                        keys[VK_SHIFT] = 0;
                        KeyboardState::Write(keys);
                        send(WM_MOUSEMOVE, {9, 8}, MK_LBUTTON);
                        keys[VK_SHIFT] = 0x80;
                        KeyboardState::Write(keys);
                        send(WM_MOUSEMOVE, {9, 8}, MK_LBUTTON | MK_SHIFT);
                    }
                    if (overflow)
                    {
                        for (unsigned i = 1; i <= 510; ++i)
                            send(WM_MOUSEMOVE, {9.0 + i, 8}, MK_LBUTTON);
                        send(WM_MOUSEMOVE, {9999, 8}, MK_LBUTTON);
                    }
                }
                keys = {};
                KeyboardState::Write(keys);
                bool joinedSource = false;
                if (cancel || joined)
                {
                    if (!expect(until(
                                    [&]() noexcept
                                    {
                                        return owner->_selectionPressState &&
                                               owner->_selectionPressState.value().drawing &&
                                               owner->_selectionPressState.value().waiting &&
                                               owner->_selectionPressState.value().moves == 1;
                                    }),
                                "original-drawing-parent-reaches-event-wait"))
                        return 5;
                    if (joined)
                    {
                        joinedSource = owner->_session.Selection() == original &&
                                       owner->_session.Generation() == generation && !owner->_session.history.CanUndo();
                        owner->Shutdown();
                    }
                    else
                        owner->CancelHostContinuations(false);
                    if (GetCapture() == owner->_window.get())
                        ReleaseCapture();
                }
                else if (lost || overflow)
                    ReleaseCapture();
                else
                {
                    owner->_session.selectionAntialiased = true;
                    send(WM_LBUTTONUP, {22, 24}, 0); // Up seals AA; its location is not an invented Move.
                    owner->_session.selectionAntialiased = false;
                }
                if (!expect(joined ? owner->_shutdownStarted && owner->_jobs.IsIdle() : settle(),
                            "exact-original-parent-or-refusal-settles"))
                    return 5;
                const auto wanted = lost || overflow || cancel || joined ? HostOutcome::Cancelled
                                    : target                             ? HostOutcome::Stale
                                    : refused                            ? HostOutcome::Failed
                                                                         : HostOutcome::Succeeded;
                if (!expect(owner->_selectionPressReceipt.Read().outcome == wanted &&
                                !owner->_selectionPressInput.Busy() && !owner->_session.PendingLasso() &&
                                !owner->_session.IsMovingSelection() && !owner->_selectionPressStamp &&
                                !owner->_historyWork.Busy(),
                            "exact-receipt-input-and-history-barrier-acknowledged"))
                    return 5;
                const bool unchanged = cancel || joined || lost || overflow || target || refused;
                const bool missingSubtract = mode == SelectionMode::Subtract && (absent || noLayers);
                const auto& selected = owner->_session.Selection();
                bool geometry = false;
                if (joined)
                    geometry = joinedSource && !owner->_session.Document() && !selected;
                else if (unchanged)
                    geometry = selected == original && owner->_session.Generation() == generation &&
                               owner->_session.history.CurrentRevision() == revision &&
                               !owner->_session.history.CanUndo();
                else if (missingSubtract)
                    geometry = !selected && owner->_session.Generation() == generation &&
                               owner->_session.history.CurrentRevision() == revision &&
                               !owner->_session.history.CanUndo();
                else
                {
                    const bool emptySubtract = mode == SelectionMode::Subtract && empty;
                    const bool hadOld = !absent && !empty && !noLayers;
                    const SelectionBounds bounds =
                        hadOld ? (mode == SelectionMode::Add ? SelectionBounds{1, 2, 9, rearm ? 9.0 : 8.0}
                                                             : SelectionBounds{1, 2, 7, 6})
                               : SelectionBounds{3, 3, 9, rearm ? 9.0 : 8.0};
                    geometry = selected && selected.value().antialiased &&
                               (emptySubtract ? selected.value().IsEmpty()
                                              : selected.value().Bounds() == std::optional(bounds)) &&
                               owner->_session.Generation() == generation + 1 &&
                               owner->_session.history.UndoCount() == 1 &&
                               owner->_session.history.UndoName() ==
                                   (kind == LassoKind::Rectangle ? L"Rectangular Marquee" : L"Elliptical Marquee");
                }
                if (!expect(geometry && (!target || owner->_session.ActiveLayerID() == other),
                            "literal-bounds-terminal-aa-noop-source-or-joined-retirement"))
                    return 5;
                if (!unchanged && !missingSubtract && !empty && !absent && !noLayers)
                {
                    if (!expect(selected && Filled(selected.value(), {2, 3}) &&
                                    Filled(selected.value(), {6, 4}) == (mode == SelectionMode::Add) &&
                                    Filled(selected.value(), {8, 5.5}) == (mode == SelectionMode::Add) &&
                                    !Filled(selected.value(), {8, 2}),
                                "independent-four-region-preserving-winding"))
                        return 5;
                }
                const auto& state = owner->_selectionPressState;
                if (!expect(target
                                ? !owner->_activeHostRequest
                                : state && !state.value().controller &&
                                      (refused ? !state.value().opened && !state.value().captures
                                               : state.value().drawing && state.value().sameTicket &&
                                                     state.value().captures == 1 && !state.value().hits &&
                                                     state.value().firstBudget == admitted.value().limits.totalBytes &&
                                                     state.value().inputAcknowledged &&
                                                     (joined ? !state.value().completed && state.value().aborted
                                                             : state.value().completed)) &&
                                      (!overflow || (state.value().moves == 511 && state.value().overflowObserved &&
                                                     state.value().terminalLost)),
                            "one-original-ticket-budget-without-containment-or-source-recapture"))
                    return 5;
                if (!unchanged && !missingSubtract)
                {
                    owner->_session.Undo();
                    const bool restored = owner->_session.Selection() == original && owner->_session.history.CanRedo();
                    owner->_session.Redo();
                    if (!expect(restored && owner->_session.Selection() &&
                                    owner->_session.Selection().value().antialiased &&
                                    owner->_session.history.UndoCount() == 1 && !owner->_session.history.CanRedo(),
                                "undo-restores-source-and-redo-restores-one-complete-result"))
                        return 5;
                }
                refuseMemory = false;
                ++reached;
            }
            scenario = "close";
            owner->Shutdown();
            if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                            !owner->_jobs.ReleasingMemoryBytes(),
                        "joined-close-returns-all-original-reservations"))
                return 5;
        }
    family = "close";
    scenario = "close";
    if (!expect(reached == 4 * cases.size() && !failures, "all-kind-mode-physical-header-lifecycle-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}

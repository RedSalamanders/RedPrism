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
#include <string_view>
#include <vector>

namespace
{
bool FreehandFilled(const DocumentSelection& selection, Vec2 point) noexcept
{
    int winding = 0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.empty())
            continue;
        auto previous = loop.back();
        for (const auto next : loop)
        {
            const auto cross =
                (next.x - previous.x) * (point.y - previous.y) - (point.x - previous.x) * (next.y - previous.y);
            if (previous.y <= point.y && next.y > point.y && cross > 0)
                ++winding;
            else if (previous.y > point.y && next.y <= point.y && cross < 0)
                --winding;
            previous = next;
        }
    }
    return winding != 0;
}
bool SameFreehandSelection(const std::optional<DocumentSelection>& actual,
                           const std::optional<DocumentSelection>& expected) noexcept
{
    if (actual.has_value() != expected.has_value())
        return false;
    if (!actual)
        return true;
    if (actual.value().antialiased != expected.value().antialiased ||
        actual.value().Bounds() != expected.value().Bounds())
        return false;
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 40; ++x)
            if (FreehandFilled(actual.value(), {x + .5, y + .5}) != FreehandFilled(expected.value(), {x + .5, y + .5}))
                return false;
    return true;
}
} // namespace

int Application::RunStandaloneFreehandPressSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::ScopedSynthetic keyboard;
    std::array<BYTE, 256> nativeKeys{};
    if (!GetKeyboardState(nativeKeys.data()))
        return 5;
    auto restoreKeys = wil::scope_exit([&]() noexcept { (void)SetKeyboardState(nativeKeys.data()); });
    unsigned checks = 0, failures = 0, reached = 0;
    const char* family = "setup";
    const char* scenario = "setup";
    const auto expect = [&](bool value, const char* label) noexcept
    {
        ++checks;
        failures += value ? 0u : 1u;
        std::printf("%s freehand-press-host.%s.%s.%s\n", value ? "PASS" : "FAIL", family, scenario, label);
        return value;
    };
    auto summary = wil::scope_exit(
        [&]() noexcept
        { std::printf("SUMMARY freehand-press-host checks=%u failures=%u expected=472\n", checks, failures); });
    constexpr std::array cases{
        "literal",           "captured-header", "physical-mode",   "absent",     "empty",
        "no-layers",         "multiblock",      "short",           "lost",       "cancelled",
        "refused",           "target",          "prefix-overflow", "inside-new", "live-rectangle-loss",
        "live-ellipse-loss", "joined-close"};
    for (const auto mode : {SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract})
    {
        family = mode == SelectionMode::New ? "new" : mode == SelectionMode::Add ? "add" : "subtract";
        scenario = "setup";
        bool scarce = false;
        ApplicationProcessingTestProbe probe;
        probe.workerCount = 2;
        probe.memory = {[](void* raw, MemorySnapshot& out) noexcept
                        {
                            const auto value = *static_cast<bool*>(raw)
                                                   ? 0ULL
                                                   : kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024;
                            out = {value, value};
                            return true;
                        },
                        &scarce};
        auto owner = std::make_unique<Application>(instance, true, L"", 0, nullptr, nullptr);
        auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
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
            const bool rectangleLoss = name == "live-rectangle-loss";
            const bool ellipseLoss = name == "live-ellipse-loss";
            const bool marqueeLoss = rectangleLoss || ellipseLoss;
            const auto capturedKind = rectangleLoss ? LassoKind::Rectangle
                                      : ellipseLoss ? LassoKind::Ellipse
                                                    : LassoKind::Freehand;
            const bool absent = name == "absent", empty = name == "empty", noLayers = name == "no-layers";
            const bool multi = name == "multiblock", shortPath = name == "short", lost = name == "lost" || marqueeLoss;
            const bool cancelled = name == "cancelled", refused = name == "refused", target = name == "target";
            const bool overflow = name == "prefix-overflow", joined = name == "joined-close";
            const bool insideMove = name == "inside-new" && mode == SelectionMode::New;
            const bool physical = name == "physical-mode";
            DocumentFixture fixture;
            fixture.width = fixture.height = 64;
            if (!noLayers)
            {
                for (const auto label : {L"Freehand target", L"Other target"})
                {
                    auto layer = ImageLayer::Blank(label, {64, 64});
                    layer.asset = RasterAsset{
                        Raster::MakeSparse(64, 64, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, layer.name};
                    fixture.layers.push_back(std::move(layer));
                }
            }
            if (empty)
                fixture.selection = DocumentSelection{false, SelectionLoops{}};
            else if (!absent && !noLayers)
                fixture.selection = DocumentSelection{
                    false, SelectionLoops{std::vector<SelectionLoop>{{{1, 2}, {7, 2}, {7, 6}, {1, 6}}}}};
            const auto active = noLayers ? std::optional<Guid>{} : std::optional{fixture.layers[0].id};
            const auto other = noLayers ? std::optional<Guid>{} : std::optional{fixture.layers[1].id};
            owner->_session.ReplaceDocument(std::move(fixture).Seal(), active);
            owner->_session.history.Reset();
            owner->_tool = marqueeLoss ? EditorTool::Marquee : EditorTool::Lasso;
            owner->_session.lassoKind = LassoKind::Freehand;
            if (marqueeLoss)
                owner->_session.marqueeKind = capturedKind;
            owner->_session.selectionModeChoice = physical ? SelectionMode::New : mode;
            owner->_session.selectionAntialiased = false;
            owner->_viewport.followsFit = false;
            owner->_viewport.zoom = 8;
            owner->DocumentChanged();
            if (!expect(settle(), "first-display-and-source-settled"))
                return 5;
            owner->RenderFrame();
            const auto hole = owner->_chrome->HolePixels();
            const auto origin = owner->_viewport.DocumentRectOrigin(owner->DocumentSize());
            owner->_viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
            owner->_viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
            const auto original = owner->_session.Selection();
            const auto generation = owner->_session.Generation();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto admitted = DescribeSelectionOutlineGestureMemory(owner->_session, capturedKind, mode);
            if (!expect(admitted && admitted.value().drawing &&
                            admitted.value().drawing.value().arenaBlocks == (marqueeLoss ? 1u : 1023u) &&
                            admitted.value().limits.totalBytes < 1536ULL * 1024 * 1024,
                        "cached-whole-gesture-admission-before-source-capture"))
                return 5;
            const auto send = [&](UINT message, Vec2 document, WPARAM modifiers) noexcept
            {
                const auto view = owner->_viewport.ViewFromDocument(document, owner->DocumentSize());
                SendMessageW(owner->_window.get(), message, modifiers,
                             MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                        static_cast<int>(std::lround(hole.top + view.y))));
            };
            const auto down = insideMove                   ? Vec2{2, 3}
                              : multi                      ? Vec2{1, 1}
                              : mode == SelectionMode::New ? Vec2{10, 7}
                                                           : Vec2{4, 3};
            auto reference = std::make_unique<DocumentSession>();
            reference->ReplaceDocument(owner->_session.Document().value(), active);
            reference->history.Reset();
            reference->lassoKind = LassoKind::Freehand;
            reference->selectionAntialiased = false;
            reference->BeginLasso(down, mode);
            std::array<BYTE, 256> keys{};
            keys[VK_SHIFT] = physical && mode == SelectionMode::Add ? 0x80 : 0;
            keys[VK_MENU] = physical && mode == SelectionMode::Subtract ? 0x80 : 0;
            KeyboardState::Write(keys);
            scarce = refused;
            send(WM_LBUTTONDOWN, down, MK_LBUTTON | (keys[VK_SHIFT] ? MK_SHIFT : 0));
            const auto* captured = owner->_selectionPressInput.CapturedContext(owner->_selectionPressInput.Opening());
            if (!expect(captured &&
                            captured->tool == (marqueeLoss ? HostSelectionPressInput::Tool::Marquee
                                                           : HostSelectionPressInput::Tool::Lasso) &&
                            captured->kind == capturedKind && captured->mode == mode &&
                            owner->_selectionPressReceipt.Read().outcome == HostOutcome::Pending &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection(),
                        "wndproc-down-is-only-captured-kind-mode-and-scalars"))
                return 5;
            if (name == "captured-header")
            {
                owner->_session.lassoKind = LassoKind::Polygonal;
                owner->_session.selectionModeChoice = SelectionMode::Subtract;
            }
            if (target)
                owner->_session.SelectLayer(other);
            const auto move = [&](Vec2 point) noexcept
            {
                send(WM_MOUSEMOVE, point, MK_LBUTTON);
                reference->ExtendLasso(point);
            };
            if (!refused && !target && !shortPath)
            {
                if (insideMove)
                    move({6, 5});
                else if (multi)
                {
                    for (unsigned i = 1; i <= 128; ++i)
                        move({1 + .25 * i, 1});
                    move({33, 5});
                    move({1, 5});
                }
                else if (mode == SelectionMode::New)
                {
                    move({4, 7});
                    move({4, 3});
                    move({10, 3});
                }
                else
                {
                    move({10, 3});
                    move({10, 7});
                    move({4, 7});
                }
                if (overflow)
                    for (unsigned i = 3; i <= 512; ++i)
                        move({10.0 + (i % 8), 7.0 + (i % 5)});
            }
            keys = {};
            KeyboardState::Write(keys);
            bool live = true;
            if (!refused && !target && !overflow)
                live = until([&]() noexcept
                             { return owner->_selectionPressState && owner->_selectionPressState.value().waiting; });
            const auto pointCount =
                owner->_session.PendingLasso() ? owner->_session.PendingLasso().value().PointCount() : 0;
            if (!expect(live && (refused || target || overflow || insideMove ||
                                 pointCount == (multi       ? 131u
                                                : shortPath ? 1u
                                                            : 4u)),
                        "bounded-replay-reaches-original-live-prefix-or-refusal"))
                return 5;
            const bool joinedSource =
                owner->_session.Selection() == original && owner->_session.Generation() == generation &&
                owner->_session.history.CurrentRevision() == revision && !owner->_session.history.CanUndo();
            if (joined)
                owner->Shutdown();
            else if (cancelled)
                owner->CancelHostContinuations(false);
            else if (lost || overflow)
            {
                if (GetCapture() == owner->_window.get())
                    ReleaseCapture();
            }
            else
            {
                owner->_session.selectionAntialiased = true;
                send(WM_LBUTTONUP, {47, 49}, 0);
                owner->_session.selectionAntialiased = false;
            }
            if (!joined && GetCapture() == owner->_window.get())
                ReleaseCapture();
            if (!expect(joined ? owner->_jobs.IsIdle() : settle(), "exact-original-terminal-or-refusal-settles"))
                return 5;
            const auto wanted = joined || cancelled || lost || overflow ? HostOutcome::Cancelled
                                : target                                ? HostOutcome::Stale
                                : refused                               ? HostOutcome::Failed
                                                                        : HostOutcome::Succeeded;
            if (!expect(owner->_selectionPressReceipt.Read().outcome == wanted && !owner->_selectionPressInput.Busy() &&
                            !owner->_session.PendingLasso() && !owner->_session.IsMovingSelection() &&
                            !owner->_selectionPressStamp && !owner->_historyWork.Busy(),
                        "exact-outcome-and-all-original-input-history-barriers-acknowledged"))
                return 5;
            const bool unchanged = joined || cancelled || lost || overflow || target || refused;
            bool geometry = false, changed = false;
            if (joined)
                geometry = joinedSource && !owner->_session.Document();
            else if (unchanged)
                geometry = owner->_session.Selection() == original && owner->_session.Generation() == generation &&
                           owner->_session.history.CurrentRevision() == revision && !owner->_session.history.CanUndo();
            else if (insideMove)
            {
                geometry =
                    owner->_session.Selection() &&
                    owner->_session.Selection().value().Bounds() == std::optional(SelectionBounds{5, 4, 11, 8}) &&
                    !owner->_session.Selection().value().antialiased && owner->_session.history.UndoCount() == 1;
                changed = true;
            }
            else
            {
                reference->selectionAntialiased = true;
                reference->FinishLasso();
                changed = reference->history.UndoCount() != 0;
                geometry = SameFreehandSelection(owner->_session.Selection(), reference->Selection()) &&
                           owner->_session.history.UndoCount() == reference->history.UndoCount() &&
                           owner->_session.Generation() == generation + (changed ? 1u : 0u);
            }
            if (!expect(geometry, "literal-or-legacy-numeric-selection-history-and-terminal-aa"))
                return 5;
            const auto& state = owner->_selectionPressState;
            if (!expect(target ? !owner->_activeHostRequest
                               : state && !state.value().controller &&
                                     (refused ? !state.value().captures && !state.value().opened
                                              : state.value().captures == 1 && state.value().sameTicket &&
                                                    state.value().inputAcknowledged &&
                                                    state.value().firstBudget == admitted.value().limits.totalBytes &&
                                                    state.value().maximumDrawingSamples <= 16 &&
                                                    (insideMove ? !state.value().drawing && state.value().hits == 1
                                                                : state.value().drawing)),
                        "one-original-ticket-capture-budget-and-max16-owner-quantum"))
                return 5;
            bool roundtrip = true;
            if (!unchanged && changed)
            {
                const auto final = owner->_session.Selection();
                owner->_session.Undo();
                roundtrip = owner->_session.Selection() == original && owner->_session.history.CanRedo();
                owner->_session.Redo();
                roundtrip = roundtrip && SameFreehandSelection(owner->_session.Selection(), final) &&
                            owner->_session.history.UndoCount() == 1 && !owner->_session.history.CanRedo();
            }
            if (!expect(roundtrip, "undo-redo-keeps-complete-original-and-terminal-roots"))
                return 5;
            scarce = false;
            ++reached;
        }
        scenario = "close";
        owner->Shutdown();
        if (!expect(owner->_jobs.IsIdle() && !owner->_jobs.ReservedMemoryBytes() &&
                        !owner->_jobs.ReleasingMemoryBytes(),
                    "joined-mode-close-refunds-all-owner-roots"))
            return 5;
    }
    family = scenario = "close";
    if (!expect(reached == 3 * cases.size() && !failures && checks == 471,
                "all51-physical-mode-lifecycle-cases-reached"))
        return 5;
    return failures ? 5 : 0;
}

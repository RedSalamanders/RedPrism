#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"

#include <cstdio>
#include <memory>
#include <new>

int Application::RunOpacityClosureGuardSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic syntheticKeyboard;
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 96, nullptr, nullptr)};
    if (!owner)
        return 5;
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            owner->Shutdown();
            owner.reset();
            while (PumpMessages().quit)
            {
            }
        });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    owner->_session.CreateDocument(4, 3, true);
    owner->_session.AddBlankLayer();
    owner->_session.history.Reset();
    owner->_session.history.MarkSaved();
    const auto target = owner->_session.ActiveLayerID().value();
    const auto other = owner->_session.Document()->layers.front().id;
    int failures = 0;
    const auto expect = [&](const char* name, bool result) noexcept
    {
        std::fprintf(stderr, "%s opacity-closure-guard.%s\n", result ? "PASS" : "FAIL", name);
        failures += result ? 0 : 1;
    };
    if (FAILED(owner->InitializeProcessing()) || !owner->EnableCommittedDisplayForTest() ||
        FAILED(owner->RegisterWindowClass()) || FAILED(owner->CreateMainWindow(false)) ||
        FAILED(owner->InitializeRenderer()) || FAILED(owner->InitializeChrome()))
        return 5;
    owner->DocumentChanged();
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    owner->RenderFrame();
    const auto hole = owner->_chrome->HolePixels();
    const auto point =
        MAKELPARAM(static_cast<int>(hole.left + hole.width / 2), static_cast<int>(hole.top + hole.height / 2));
    expect("actual-hidden-window-has-canvas-pointer-route",
           hole.width > 0 && hole.height > 0 && owner->_window && owner->_tool == EditorTool::Move);
    expect("absent-invocation-needs-no-worker", owner->CloseOpacityEdit());
    owner->_opacityEdit = OpacityEdit{target, 1.0, 1.0, 1, owner->_session.DocumentSerial()};
    expect("unchanged-invocation-closes-without-history",
           owner->CloseOpacityEdit() && !owner->_opacityEdit && owner->_session.history.UndoCount() == 0);
    // Independent fixture writes the acknowledged preview value only. It is not
    // a production owner writer and never creates the commit this guard forbids.
    if (!owner->_session.PreviewLayerOpacity(target, .25))
        return 5;
    owner->_opacityEdit = OpacityEdit{target, 1.0, .25, 2, owner->_session.DocumentSerial()};
    const auto layers = owner->_session.Document()->layers;
    const auto selection = owner->_session.Selection();
    const auto history = owner->_session.history.CaptureState();
    const auto generation = owner->_session.Generation();
    const auto revision = owner->_session.history.CurrentRevision();
    const auto tool = owner->_tool;
    const auto preserved = [&]() noexcept
    {
        return owner->_session.Document()->layers.Identity() == layers.Identity() &&
               owner->_session.Selection() == selection && owner->_session.ActiveLayerID() == target &&
               owner->_session.Generation() == generation && owner->_session.history.IsCurrent(history) &&
               owner->_session.history.CurrentRevision() == revision && !owner->_session.history.IsModified() &&
               owner->_opacityEdit && owner->_opacityEdit->start == 1.0 && owner->_opacityEdit->shown == .25 &&
               owner->_session.ActiveLayer()->opacity == .25 && owner->_tool == tool;
    };
    expect("unselected-changed-edit-refuses-with-all-owners-intact", !owner->CloseOpacityEdit() && preserved());
    owner->_hostExecuting = true;
    owner->_hostExecutionError = S_OK;
    expect("selected-changed-edit-refuses-unacknowledged-history",
           !owner->CloseOpacityEdit() && owner->_hostExecutionError == E_UNEXPECTED && preserved());
    const auto caller = [&](const char* name, auto&& operation) noexcept
    {
        owner->_hostExecutionError = S_OK;
        operation();
        expect(name, owner->_hostExecutionError == E_UNEXPECTED && preserved());
    };
    caller("layer-select-stops-before-target-mutation", [&]() noexcept { owner->OnLayerSelected(other); });
    caller("layer-target-stops-before-target-mutation", [&]() noexcept { owner->TargetLayer(other, false, false); });
    caller("layer-invoke-stops-before-target-or-card", [&]() noexcept { owner->OnLayerInvoked(other); });
    caller("layer-reorder-stops-before-record-write",
           [&]() noexcept { owner->OnLayerReordered(target, other, std::nullopt, DxUi::TreeDropPlace::Before); });
    caller("command-stops-before-dependent-edit",
           [&]() noexcept { owner->OnCommand(ChromeHost::kCommandClearPixels); });
    caller("tool-switch-stops-before-dependent-settlement",
           [&]() noexcept { owner->OnTool(tool == EditorTool::Crop ? EditorTool::Hand : EditorTool::Crop); });
    caller("write-stops-before-pending-canvas-settlement",
           [&]() noexcept { expect("write-reports-prerequisite-refusal", !owner->SettleForWrite()); });
    caller("interaction-stops-before-pending-canvas-settlement",
           [&]() noexcept { expect("interaction-reports-prerequisite-refusal", !owner->SettleInteractions(false)); });
    owner->_hostExecutionError = S_OK;
    owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, point);
    const bool refused = owner->_hostExecutionError == E_UNEXPECTED && !owner->PointerBusy() &&
                         std::holds_alternative<std::monostate>(owner->_drag) && preserved();
    owner->OnPointer(WM_LBUTTONUP, 0, point);
    expect("actual-canvas-mutation-press-refuses-before-drag", refused && preserved());
    owner->_hostExecuting = false;
    owner->_spacePan = true;
    owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, point);
    const bool panning = std::holds_alternative<PanDrag>(owner->_drag) && preserved();
    owner->OnPointer(WM_LBUTTONUP, 0, point);
    owner->_spacePan = false;
    expect("actual-read-only-pan-keeps-preview-and-history", panning && !owner->PointerBusy() && preserved());
    owner->_opacityEdit->documentSerial = owner->_session.DocumentSerial() + 1;
    expect("expired-session-invocation-drops-only-scalar-state",
           owner->CloseOpacityEdit() && !owner->_opacityEdit &&
               owner->_session.Document()->layers.Identity() == layers.Identity() &&
               owner->_session.history.IsCurrent(history) && owner->_session.Generation() == generation);
    return failures ? 5 : 0;
}

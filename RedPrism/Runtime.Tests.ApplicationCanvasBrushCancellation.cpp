#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunCanvasBrushCancellationSelfTest() noexcept
{
    int checks = 0;
    const auto originalViewport = _viewport;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-cancel.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, &settle]() noexcept
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            (void)CancelCanvasBrush();
            if (!settle())
                std::_Exit(5);
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
        });
    enum class Key
    {
        Escape,
        Undo,
        Redo
    };
    struct Scenario
    {
        const char* name;
        Key key;
        bool afterReplay;
    };
    constexpr std::array scenarios{
        Scenario{"held-escape", Key::Escape, false}, Scenario{"held-undo", Key::Undo, false},
        Scenario{"held-redo", Key::Redo, false},     Scenario{"live-escape", Key::Escape, true},
        Scenario{"live-undo", Key::Undo, true},      Scenario{"live-redo", Key::Redo, true}};
    struct State
    {
        Application& owner;
        Scenario scenario;
        bool visited = false;
        bool held = false;
        bool cancelledOnOwner = false;
    };
    const auto press = [](Application& owner, Key key) noexcept
    {
        return owner.HandleAccelerator(key == Key::Escape ? VK_ESCAPE : 'Z',
                                       key == Key::Escape ? 0 : MK_CONTROL | (key == Key::Redo ? MK_SHIFT : 0));
    };
    const auto clientAt = [this](Vec2 point) noexcept
    {
        const auto hole = _chrome->HolePixels();
        const auto view = _viewport.ViewFromDocument(point, DocumentSize());
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    // The bounded owner pump borrows only this harness's pure key driver, never worker/session storage.
    struct PumpState
    {
        State& state;
        decltype(press)& pressKey;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& inputs = *static_cast<PumpState*>(opaque);
        auto& state = inputs.state;
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && !owner._session.PendingBrush() &&
                     owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.PendingCount() == 1 &&
                     owner._session.history.UndoCount() == 0;
        gate->hold = false;
        if (!state.scenario.afterReplay)
        {
            const auto opening = owner._canvasBrushInput.Opening();
            const bool handled = inputs.pressKey(owner, state.scenario.key);
            state.cancelledOnOwner = handled && owner._canvasBrushCancelledOpening == opening &&
                                     owner._canvasBrushInput.Busy() && !owner._session.PendingBrush() &&
                                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        }
        (void)SetEvent(gate->release.get()); // No Up is sent: cancellation must retire without physical release.
    };
    for (const auto& scenario : scenarios)
    {
        if (!settle())
            return 5;
        _session.ClearDocument();
        _session.CreateDocument(32, 24, true);
        _session.AddBlankLayer();
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _tool = EditorTool::Brush;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushMode = BrushToolMode::Paint;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        _layerMetadataTestGate.reset();
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() || !_opacityEdit || _session.ActiveLayer()->opacity != .25)
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, clientAt({8, 8}));
        const auto receipt = _canvasBrushReceipt;
        if (!expect(scenario.name, "down-admits-without-paint-or-history",
                    _canvasBrushInput.Sampling() && _canvasBrushInput.PendingCount() == 1 && !_session.PendingBrush() &&
                        !_session.ActiveLayer()->asset && _session.history.UndoCount() == 0 &&
                        _chrome->CanvasFocused()))
            return 5;
        State state{*this, scenario};
        PumpState inputs{state, press};
        if (SettleJobs(_jobs, 15000, pump, &inputs) != SettleResult::Settled ||
            !expect(scenario.name, "gate-retains-opening-before-replay", state.visited && state.held))
            return 5;
        if (scenario.afterReplay)
        {
            const auto strokeOpening = _session.PendingBrushOpening();
            if (!expect(scenario.name, "replay-opens-one-owned-stroke-and-waits-on-input",
                        _canvasBrushInput.Sampling() && _session.PendingBrush() && strokeOpening &&
                            _canvasBrushSessionOpening == strokeOpening && _session.history.UndoCount() == 1 &&
                            _session.ActiveLayer()->opacity == .25 && !_opacityEdit && _hostInputBudget.Used() != 0))
                return 5;
            const bool handled = press(*this, scenario.key);
            state.cancelledOnOwner = handled && _session.PendingBrushOpening() == strokeOpening &&
                                     _canvasBrushCancelledOpening == _canvasBrushInput.Opening() &&
                                     receipt.Read().outcome == HostOutcome::Pending;
        }
        else if (!expect(scenario.name, "cancelled-held-input-does-not-open-a-stroke",
                         state.cancelledOnOwner && !_session.PendingBrush()))
            return 5;
        if (!expect(scenario.name, "key-records-cancellation-before-owner-ack", state.cancelledOnOwner) || !settle())
            return 5;
        if (!expect(scenario.name, "ack-cancels-only-stroke-and-retains-opacity-history",
                    receipt.Read().outcome == HostOutcome::Cancelled && receipt.Read().error == E_ABORT &&
                        !_canvasBrushInput.Busy() && !_session.PendingBrushOpening() &&
                        !_session.ActiveLayer()->asset && _session.history.UndoCount() == 1 &&
                        _session.history.UndoName() == L"Layer Opacity" && _session.ActiveLayer()->opacity == .25 &&
                        !_opacityEdit && !_canvasBrushCancelledOpening && !_canvasBrushSessionOpening))
            return 5;
        if (!expect(scenario.name, "ack-releases-input-and-worker-reservations",
                    !_activeHostRequest && _hostRequests.Size() == 0 && _hostInputBudget.Used() == 0 &&
                        MetadataAccountingSettledForTest() && _layerMetadataTestGate->retired.load() &&
                        _layerMetadataTestGate->retiredOffOwner.load()))
            return 5;
        OnPointer(WM_MOUSEMOVE, MK_LBUTTON, clientAt({20, 8}));
        OnPointer(WM_LBUTTONUP, 0, clientAt({20, 8}));
        if (!settle() ||
            !expect(scenario.name, "late-physical-release-cannot-paint-or-commit",
                    !_session.PendingBrush() && !_session.ActiveLayer()->asset && _session.history.UndoCount() == 1 &&
                        _session.ActiveLayer()->opacity == .25 && receipt.Read().outcome == HostOutcome::Cancelled))
            return 5;
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle() || !expect(scenario.name, "next-undo-restores-opacity-original",
                                 _session.history.UndoCount() == 0 && _session.ActiveLayer()->opacity == 1 &&
                                     !_session.ActiveLayer()->asset))
            return 5;
    }
    // Authority fault seam: replace the core owner without routing a second physical gesture through the host.
    // This proves late retirement/path data cannot affect another stroke, independently of future tool routing.
    for (int variant = 0; variant < 3; ++variant)
    {
        const char* name = variant == 0   ? "new-stroke-cancel"
                           : variant == 1 ? "new-stroke-path"
                                          : "new-document-cancel";
        if (!settle())
            return 5;
        _session.ClearDocument();
        _session.CreateDocument(32, 24, true);
        _session.AddBlankLayer();
        _session.history.Reset();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _layerMetadataTestGate.reset();
        _tool = EditorTool::Brush;
        _session.brushSettings = BrushSettings{3, 1, 1, 0, 0, 1};
        DocumentChanged();
        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() || !_opacityEdit)
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, clientAt({8, 8}));
        const auto receipt = _canvasBrushReceipt;
        State state{*this, Scenario{name, Key::Escape, true}};
        PumpState inputs{state, press};
        if (SettleJobs(_jobs, 15000, pump, &inputs) != SettleResult::Settled ||
            !expect(name, "held-down-retains-original-authority", state.visited && state.held))
            return 5;
        const auto oldOpening = _session.PendingBrushOpening();
        const auto oldDocument = _session.DocumentSerial();
        if (!expect(name, "replay-owns-original-core-opening",
                    oldOpening && _canvasBrushSessionOpening == oldOpening && _canvasBrushInput.Sampling() &&
                        _session.history.UndoCount() == 1))
            return 5;
        _session.CancelBrush();
        if (variant == 2)
        {
            _session.ClearDocument();
            _session.CreateDocument(32, 24, true);
            _session.AddBlankLayer();
            _session.history.Reset();
        }
        if (!_session.BeginBrush({24, 18}, BrushKind::Paint, nullptr, BrushSettings{3, 1, 0, 1, 0, 1}, false))
            return 5;
        const auto newOpening = _session.PendingBrushOpening();
        const auto* newStroke = _session.PendingBrush();
        const auto historyBefore = _session.history.UndoCount();
        if (!expect(name, "replacement-has-distinct-stroke-or-document-authority",
                    newOpening && newStroke && (newOpening != oldOpening || _session.DocumentSerial() != oldDocument)))
            return 5;
        if (variant == 1)
        {
            OnPointer(WM_MOUSEMOVE, MK_LBUTTON, clientAt({20, 8}));
            OnPointer(WM_LBUTTONUP, 0, clientAt({20, 8}));
        }
        else if (!press(*this, Key::Escape))
            return 5;
        if (!settle() ||
            !expect(name, "old-input-retires-once-without-owned-budget",
                    receipt.Read().outcome == (variant == 0 ? HostOutcome::Cancelled : HostOutcome::Stale) &&
                        receipt.Read().error == E_ABORT && !_canvasBrushInput.Busy() && !_activeHostRequest &&
                        _hostInputBudget.Used() == 0 && MetadataAccountingSettledForTest() &&
                        (variant != 2 || _jobs.ReservedMemoryBytes() == 0)))
            return 5;
        if (!expect(name, "old-retirement-preserves-new-core-stroke",
                    _session.PendingBrushOpening() == newOpening && _session.PendingBrush() == newStroke &&
                        _session.history.UndoCount() == historyBefore && !_session.ActiveLayer()->asset))
            return 5;
        _session.FinishBrush();
        _composition.EndStroke();
        _previewSource.reset();
        const auto* layer = _session.ActiveLayer();
        const auto pixelAt = [layer](int x, int y)
        {
            std::array<std::uint8_t, 4> pixel{};
            if (layer && layer->asset)
                layer->asset->image.ReadRegion(x - static_cast<int>(std::lround(layer->transform.origin.x)),
                                               y - static_cast<int>(std::lround(layer->transform.origin.y)), 1, 1,
                                               pixel.data(), 4, 0);
            return pixel;
        };
        if (!expect(name, "new-stroke-commits-only-its-literal-green-tip",
                    !_session.PendingBrushOpening() && _session.history.UndoCount() == historyBefore + 1 &&
                        pixelAt(24, 18) == std::array<std::uint8_t, 4>{0, 255, 0, 255} && pixelAt(20, 8)[3] == 0 &&
                        pixelAt(8, 8)[3] == 0))
            return 5;
        OnPointer(WM_LBUTTONUP, 0, clientAt({20, 8}));
        DocumentChanged();
    }
    std::fprintf(stderr, "PASS canvas-brush-cancel.summary checks=%d cases=9\n", checks);
    return 0;
}

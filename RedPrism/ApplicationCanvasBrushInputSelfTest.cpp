#include "Application.h"

#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunCanvasBrushOpacitySelfTest() noexcept
{
    const auto originalViewport = _viewport;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-opacity.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, &settle]
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            if (_canvasBrushInput.Sampling())
                LoseCanvasBrush();
            if (!settle())
                std::_Exit(5);
            _layerMetadataTestGate.reset();
            _opacityEdit.reset();
            _opacityRecorded.reset();
            _canvasBrushReceipt = {};
            _viewport = originalViewport;
            (void)SendMessageW(_window.get(), WM_ACTIVATEAPP, TRUE, 0);
        });

    enum class Kind
    {
        Release,
        View,
        Capture,
        CancelMode,
        Focus,
        Deactivate,
        Failure,
        Overflow,
    };
    struct State final
    {
        Application& owner;
        Kind kind;
        LPARAM oldEndClient;
        bool visited = false;
        bool held = false;
        bool responsive = false;
        bool terminalOwned = false;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        const auto* captured = owner._canvasBrushInput.CapturedContext(owner._canvasBrushInput.Opening());
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && owner._session.ActiveLayer()->opacity == .25 &&
                     owner._session.history.UndoCount() == 0 && owner._session.PendingBrush() == nullptr &&
                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && captured &&
                     captured->settings == BrushSettings{3, 1, 1, 0, 0, 1};
        gate->hold = false;
        const auto pointer = [&owner](UINT kind, WPARAM modifiers, Vec2 point)
        {
            const auto hole = owner._chrome->HolePixels();
            const auto view = owner._viewport.ViewFromDocument(point, owner.DocumentSize());
            owner.OnPointer(kind, modifiers,
                            MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                       static_cast<int>(std::lround(hole.top + view.y))));
        };
        // New header/palette values must not replace the values owned by the queued physical Down.
        owner._session.brushSettings = BrushSettings{9, 1, 0, 0, 1, 1};
        if (state.kind == Kind::View)
        {
            owner._viewport.SetZoom(2.0, owner._viewport.Center(), owner.DocumentSize());
            owner.SyncView();
            state.responsive =
                owner._viewport.zoom == 2 && owner._historyWork.Busy() && owner._session.PendingBrush() == nullptr;
            // Same physical client position means (18,10) after centered 2x zoom of this 32x24 fixture.
            owner.OnPointer(WM_MOUSEMOVE, MK_LBUTTON, state.oldEndClient);
            owner.OnPointer(WM_LBUTTONUP, 0, state.oldEndClient);
        }
        else if (state.kind == Kind::Overflow)
        {
            for (std::size_t i = 0; i < CanvasBrushInput::kCapacity; ++i)
                pointer(WM_MOUSEMOVE, MK_LBUTTON, Vec2{12, 8});
            state.terminalOwned = owner._canvasBrushInput.PendingCount() == 513 &&
                                  owner._canvasBrushInput.Overflowed() && !owner._canvasBrushInput.Sampling();
            pointer(WM_LBUTTONUP, 0, Vec2{20, 8}); // Refused late endpoint cannot extend the accepted path.
        }
        else
        {
            pointer(WM_MOUSEMOVE, MK_LBUTTON, Vec2{12, 8});
            if (state.kind == Kind::Capture || state.kind == Kind::CancelMode || state.kind == Kind::Focus ||
                state.kind == Kind::Deactivate)
            {
                const UINT loss = state.kind == Kind::Capture      ? WM_CAPTURECHANGED
                                  : state.kind == Kind::CancelMode ? WM_CANCELMODE
                                  : state.kind == Kind::Focus      ? WM_KILLFOCUS
                                                                   : WM_ACTIVATEAPP;
                (void)SendMessageW(owner._window.get(), loss, 0, 0);
                pointer(WM_MOUSEMOVE, MK_LBUTTON, Vec2{20, 8});
                pointer(WM_LBUTTONUP, 0, Vec2{20, 8});
            }
            else
                pointer(WM_LBUTTONUP, 0, Vec2{20, 8});
        }
        state.terminalOwned =
            state.terminalOwned || (owner._canvasBrushInput.PendingCount() == 3 && !owner._canvasBrushInput.Sampling());
        (void)SetEvent(gate->release.get());
    };
    constexpr std::array cases{std::pair{"release", Kind::Release}, std::pair{"view", Kind::View},
                               std::pair{"capture", Kind::Capture}, std::pair{"cancel-mode", Kind::CancelMode},
                               std::pair{"focus", Kind::Focus},     std::pair{"deactivate", Kind::Deactivate},
                               std::pair{"failure", Kind::Failure}, std::pair{"overflow", Kind::Overflow}};
    for (const auto& [name, kind] : cases)
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
        if (kind == Kind::Failure)
            _layerMetadataTestGate->result = {JobStatus::Failed, E_OUTOFMEMORY};
        const auto clientAt = [this](Vec2 point)
        {
            const auto hole = _chrome->HolePixels();
            const auto view = _viewport.ViewFromDocument(point, DocumentSize());
            return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                              static_cast<int>(std::lround(hole.top + view.y)));
        };
        const LPARAM down = clientAt(Vec2{8, 8});
        const LPARAM oldEnd = clientAt(Vec2{20, 8});
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, down);
        if (!expect(name, "physical-down-admits-without-paint-or-history",
                    _canvasBrushInput.PendingCount() == 1 &&
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush() &&
                        _session.history.UndoCount() == 0 && _session.ActiveLayer()->opacity == .25))
            return 5;
        State state{*this, kind, oldEnd};
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(name, "worker-held-before-press-replay", state.visited && state.held && state.terminalOwned))
            return 5;
        if (kind == Kind::Failure)
        {
            if (!expect(name, "failed-closure-preserves-edit-and-drops-unpainted-stream",
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Failed &&
                            _canvasBrushReceipt.Read().error == E_OUTOFMEMORY && _opacityEdit &&
                            _opacityEdit->start == 1 && _opacityEdit->shown == .25 &&
                            _session.history.UndoCount() == 0 && !_session.ActiveLayer()->asset &&
                            !_canvasBrushInput.Busy() && !_session.PendingBrush()))
                return 5;
            continue;
        }
        const auto pixelAt = [this](int x, int y)
        {
            std::array<std::uint8_t, 4> pixel{};
            const auto* layer = _session.ActiveLayer();
            if (layer && layer->asset)
                layer->asset->image.ReadRegion(x - static_cast<int>(std::lround(layer->transform.origin.x)),
                                               y - static_cast<int>(std::lround(layer->transform.origin.y)), 1, 1,
                                               pixel.data(), 4, 0);
            return pixel;
        };
        if (!expect(name, "terminal-acknowledges-one-brush-after-opacity",
                    _session.history.UndoCount() == 2 && _session.history.UndoName() == L"Brush Stroke" &&
                        _session.ActiveLayer()->opacity == .25 && !_opacityEdit && !_canvasBrushInput.Busy() &&
                        !_session.PendingBrush() && _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 &&
                        _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load()))
            return 5;
        if (!expect(name, "down-retains-red-and-small-event-time-tip",
                    pixelAt(8, 8) == std::array<std::uint8_t, 4>{0, 0, 255, 255} && pixelAt(8, 11)[3] == 0 &&
                        _session.brushSettings == BrushSettings{9, 1, 0, 0, 1, 1}))
            return 5;
        const bool limited = kind == Kind::Capture || kind == Kind::CancelMode || kind == Kind::Focus ||
                             kind == Kind::Deactivate || kind == Kind::Overflow;
        if (!expect(name, "endpoint-and-terminal-receipt-match-owned-input",
                    pixelAt(20, 8)[3] == (limited || kind == Kind::View ? 0 : 255) &&
                        (kind != Kind::View || (state.responsive && pixelAt(18, 10)[3] == 255)) &&
                        _canvasBrushReceipt.Read().outcome ==
                            (kind == Kind::Overflow ? HostOutcome::Failed : HostOutcome::Succeeded)))
            return 5;
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle() ||
            !expect(name, "first-undo-removes-brush-only",
                    _session.ActiveLayer()->opacity == .25 && !_session.ActiveLayer()->asset &&
                        _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Layer Opacity"))
            return 5;
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle() || !expect(name, "second-undo-restores-original-opacity",
                                 _session.ActiveLayer()->opacity == 1 && _session.history.UndoCount() == 0))
            return 5;
    }
    std::fprintf(stderr, "PASS canvas-brush-opacity.summary checks=%d cases=8\n", checks);
    return 0;
}

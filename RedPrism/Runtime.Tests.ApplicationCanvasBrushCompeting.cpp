#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunCanvasBrushCompetingSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s canvas-brush-competing.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        ++checks;
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
        RightDownUp,
        SecondLeftDown,
        CaptureAfterRefusal,
        RetiredOpening,
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Kind kind;
        LPARAM latePoint;
        std::uint64_t firstOpening = 0;
        bool visited = false;
        bool held = false;
        bool refusedWithoutReplacing = false;
        bool lostPrefixClosed = false;
        bool lateInputIgnored = false;
        bool terminalOwned = false;
        bool terminalRefusalKeepsPrefix = false;
        int historyBefore = 0;
        double shownOpacity = .25;
        BrushSettings settings{3, 1, 1, 0, 0, 1};
    };
#pragma warning(pop)

    static constexpr auto point = [](Application& owner, UINT message, WPARAM modifiers, Vec2 documentPoint) noexcept
    {
        const CanvasHole hole = owner._chrome->HolePixels();
        const Vec2 view = owner._viewport.ViewFromDocument(documentPoint, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
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
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        Application& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerMetadataTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }

        state.visited = true;
        const auto gate = owner._layerMetadataTestGate;
        const auto* captured = owner._canvasBrushInput.CapturedContext(state.firstOpening);
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && owner._session.ActiveLayer()->opacity == state.shownOpacity &&
                     owner._session.history.UndoCount() == state.historyBefore &&
                     owner._session.PendingBrush() == nullptr &&
                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && captured &&
                     captured->settings == state.settings;

        // The held opening owns the original Down, and this accepted move belongs to that same stream.
        if (state.kind != Kind::RetiredOpening || state.historyBefore == 0)
            point(owner, WM_MOUSEMOVE, MK_LBUTTON, Vec2{12, 8});
        if (state.kind == Kind::RightDownUp)
        {
            point(owner, WM_RBUTTONDOWN, MK_RBUTTON, Vec2{24, 18});
            point(owner, WM_RBUTTONUP, 0, Vec2{24, 18});
            state.refusedWithoutReplacing =
                owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Opening() == state.firstOpening &&
                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && GetCapture() == owner._window.get();
            point(owner, WM_MOUSEMOVE, MK_LBUTTON, Vec2{20, 8});
            point(owner, WM_LBUTTONUP, 0, Vec2{20, 8});
        }
        else if (state.kind == Kind::SecondLeftDown)
        {
            const std::uint64_t acceptedPrefixCount = owner._canvasBrushInput.PendingCount();
            const bool wasSampling = owner._canvasBrushInput.Sampling();
            point(owner, WM_LBUTTONDOWN, MK_LBUTTON, Vec2{24, 18});
            state.refusedWithoutReplacing = wasSampling && owner._canvasBrushInput.Opening() == state.firstOpening &&
                                            owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
            // A repeated left Down is ambiguous at the OS message layer. The explicit policy is to refuse it and
            // close the original accepted prefix with Lost at (12,8), while its acknowledgement is still gated.
            state.lostPrefixClosed = !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Busy() &&
                                     owner._canvasBrushInput.PendingCount() == acceptedPrefixCount + 1 &&
                                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
            const std::size_t terminalPrefixCount = owner._canvasBrushInput.PendingCount();
            // Later messages from the refused physical gesture cannot extend or replace that closed prefix.
            point(owner, WM_MOUSEMOVE, MK_LBUTTON, Vec2{20, 8});
            point(owner, WM_LBUTTONUP, 0, Vec2{20, 8});
            state.lateInputIgnored = !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Busy() &&
                                     owner._canvasBrushInput.PendingCount() == terminalPrefixCount &&
                                     owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        }
        else if (state.kind == Kind::CaptureAfterRefusal)
        {
            point(owner, WM_RBUTTONDOWN, MK_RBUTTON, Vec2{24, 18});
            point(owner, WM_RBUTTONUP, 0, Vec2{24, 18});
            state.refusedWithoutReplacing =
                owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Opening() == state.firstOpening &&
                owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending && GetCapture() == owner._window.get();
            (void)SendMessageW(owner._window.get(), WM_CAPTURECHANGED, 0, 0);
            // Late movement and release belong to the retired opening and cannot extend or commit it again.
            point(owner, WM_MOUSEMOVE, MK_LBUTTON, Vec2{20, 8});
            point(owner, WM_LBUTTONUP, 0, Vec2{20, 8});
        }
        else
        {
            const Vec2 end = state.historyBefore == 0 ? Vec2{20, 8} : Vec2{28, 18};
            point(owner, WM_MOUSEMOVE, MK_LBUTTON, end);
            point(owner, WM_LBUTTONUP, 0, end);
        }

        state.terminalOwned = !owner._canvasBrushInput.Sampling() && owner._canvasBrushInput.Busy();
        const auto terminalCount = owner._canvasBrushInput.PendingCount();
        point(owner, WM_LBUTTONDOWN, MK_LBUTTON, Vec2{16, 20});
        state.terminalRefusalKeepsPrefix = state.terminalOwned && !owner._canvasBrushInput.Sampling() &&
                                           owner._canvasBrushInput.Opening() == state.firstOpening &&
                                           owner._canvasBrushInput.PendingCount() == terminalCount &&
                                           owner._canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
        gate->hold = false;
        (void)SetEvent(gate->release.get());
    };

    constexpr std::array cases{std::pair{"right-down-up-refused", Kind::RightDownUp},
                               std::pair{"second-left-down-refused", Kind::SecondLeftDown},
                               std::pair{"capture-loss-after-refusal", Kind::CaptureAfterRefusal},
                               std::pair{"retired-new-opening", Kind::RetiredOpening}};
    std::uint64_t retiredOpening = 0;
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

        if (kind == Kind::RetiredOpening)
        {
            // The prior brush/opacity pair has retired. A fresh typed edit must receive a new opening and receipt.
            OnLayerOpacity(.25, ChromeEditPhase::Preview);
            if (!settle() || !_opacityEdit || _session.ActiveLayer()->opacity != .25)
                return 5;
            _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
            _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!_layerMetadataTestGate->release)
                return 5;
            const LPARAM firstDown = [&]
            {
                const CanvasHole hole = _chrome->HolePixels();
                const Vec2 view = _viewport.ViewFromDocument(Vec2{8, 8}, DocumentSize());
                return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                  static_cast<int>(std::lround(hole.top + view.y)));
            }();
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, firstDown);
            State first{*this, Kind::RetiredOpening, 0, _canvasBrushInput.Opening()};
            if (SettleJobs(_jobs, 15000, pump, &first) != SettleResult::Settled ||
                !expect(name, "prior-opening-settles-after-terminal-acknowledgement",
                        first.visited && first.held && first.terminalRefusalKeepsPrefix && settle() &&
                            !_canvasBrushInput.Busy()))
                return 5;
            retiredOpening = first.firstOpening;
            const auto firstReceipt = _canvasBrushReceipt;
            _opacityEdit.reset();
            _opacityRecorded.reset();
            if (!expect(name, "prior-opening-retired-with-one-brush-receipt",
                        _session.history.UndoCount() == 2 && _session.ActiveLayer()->asset &&
                            _canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                            _hostInputBudget.Used() == 0 && MetadataAccountingSettledForTest() &&
                            _session.PendingBrush() == nullptr))
                return 5;

            // Observe the first ticket before selecting the next gate; its history remains charged.
            _layerMetadataTestGate.reset();
            OnLayerOpacity(.5, ChromeEditPhase::Preview);
            if (!settle() || !_opacityEdit || _session.ActiveLayer()->opacity != .5)
                return 5;
            _session.brushSettings = BrushSettings{3, 1, 0, 0, 1, 1};
            _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
            _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!_layerMetadataTestGate->release)
                return 5;
            const CanvasHole hole = _chrome->HolePixels();
            const Vec2 view = _viewport.ViewFromDocument(Vec2{24, 18}, DocumentSize());
            const LPARAM secondDown = MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                                 static_cast<int>(std::lround(hole.top + view.y)));
            OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, secondDown);
            const std::uint64_t secondOpening = _canvasBrushInput.Opening();
            State second{*this, kind, 0, secondOpening};
            second.historyBefore = 2;
            second.shownOpacity = .5;
            second.settings = BrushSettings{3, 1, 0, 0, 1, 1};
            if (!expect(name, "new-pending-receipt-leaves-prior-acknowledgement",
                        secondOpening > retiredOpening && firstReceipt.Read().outcome == HostOutcome::Succeeded &&
                            _canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingBrush()))
                return 5;
            if (SettleJobs(_jobs, 15000, pump, &second) != SettleResult::Settled ||
                !expect(name, "fresh-typed-opacity-holds-distinct-opening",
                        second.visited && second.held && second.terminalRefusalKeepsPrefix && settle()))
                return 5;
            if (!expect(name, "new-opening-and-receipt-own-new-literal-stroke",
                        secondOpening > retiredOpening && firstReceipt.Read().outcome == HostOutcome::Succeeded &&
                            _canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                            _session.history.UndoCount() == 4 && _session.ActiveLayer()->opacity == .5 &&
                            pixelAt(8, 8) == std::array<std::uint8_t, 4>{0, 0, 255, 255} &&
                            pixelAt(24, 18) == std::array<std::uint8_t, 4>{255, 0, 0, 255} &&
                            pixelAt(28, 18) == std::array<std::uint8_t, 4>{255, 0, 0, 255} && pixelAt(16, 20)[3] == 0 &&
                            _hostInputBudget.Used() == 0 && MetadataAccountingSettledForTest() &&
                            _session.PendingBrush() == nullptr))
                return 5;
            continue;
        }

        OnLayerOpacity(.25, ChromeEditPhase::Preview);
        if (!settle() || !_opacityEdit || _session.ActiveLayer()->opacity != .25)
            return 5;
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_layerMetadataTestGate->release)
            return 5;
        const CanvasHole hole = _chrome->HolePixels();
        const Vec2 downView = _viewport.ViewFromDocument(Vec2{8, 8}, DocumentSize());
        const LPARAM down = MAKELPARAM(static_cast<int>(std::lround(hole.left + downView.x)),
                                       static_cast<int>(std::lround(hole.top + downView.y)));
        const Vec2 lateView = _viewport.ViewFromDocument(Vec2{20, 8}, DocumentSize());
        const LPARAM late = MAKELPARAM(static_cast<int>(std::lround(hole.left + lateView.x)),
                                       static_cast<int>(std::lround(hole.top + lateView.y)));
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, down);
        State state{*this, kind, late, _canvasBrushInput.Opening()};
        if (!expect(name, "left-down-opens-one-pending-receipt",
                    state.firstOpening != 0 && _canvasBrushInput.Sampling() &&
                        _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                        _session.history.UndoCount() == 0 && _session.PendingBrush() == nullptr))
            return 5;
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(name, "worker-held-and-competitor-does-not-replace-opening",
                    state.visited && state.held && state.refusedWithoutReplacing && state.terminalOwned))
            return 5;
        if (kind == Kind::SecondLeftDown &&
            !expect(name, "repeated-left-down-loses-accepted-prefix-and-late-events-cannot-extend",
                    state.lostPrefixClosed && state.lateInputIgnored))
            return 5;
        if (!expect(name, "terminal-owned-refuses-next-left-down-without-changing-prefix",
                    state.terminalRefusalKeepsPrefix))
            return 5;
        if (!settle())
            return 5;

        const bool lost = kind == Kind::CaptureAfterRefusal || kind == Kind::SecondLeftDown;
        const auto expectedOrigin = std::array<std::uint8_t, 4>{0, 0, 255, 255};
        if (!expect(name, "only-first-opening-path-and-terminal-are-committed",
                    _canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded && _session.history.UndoCount() == 2 &&
                        _session.history.UndoName() == L"Brush Stroke" && _session.ActiveLayer()->opacity == .25 &&
                        pixelAt(8, 8) == expectedOrigin && pixelAt(12, 8) == expectedOrigin &&
                        pixelAt(24, 18)[3] == 0 && (lost ? pixelAt(20, 8)[3] == 0 : pixelAt(20, 8) == expectedOrigin) &&
                        !_canvasBrushInput.Busy() && !_session.PendingBrush() && _hostInputBudget.Used() == 0 &&
                        MetadataAccountingSettledForTest()))
            return 5;

        if (lost)
        {
            // A second terminal cannot publish another history entry or alter the already committed prefix.
            OnPointer(WM_LBUTTONUP, 0, state.latePoint);
            if (!settle() || !expect(name, "late-release-cannot-extend-or-commit-twice",
                                     _session.history.UndoCount() == 2 && pixelAt(20, 8)[3] == 0 &&
                                         _canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded))
                return 5;
        }
    }

    std::fprintf(stderr, "PASS canvas-brush-competing.summary checks=%d cases=4\n", checks);
    return 0;
}

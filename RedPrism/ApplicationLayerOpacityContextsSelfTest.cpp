#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

int Application::RunLayerOpacityContextsSelfTest() noexcept
{
    enum class Context
    {
        Brush,
        Warp,
        Shape,
        Lasso,
        Nested
    };
    struct Case final
    {
        const char* name;
        Context context;
    };
    constexpr std::array cases{Case{"brush", Context::Brush}, Case{"warp", Context::Warp},
                               Case{"shape", Context::Shape}, Case{"lasso", Context::Lasso},
                               Case{"nested", Context::Nested}};

    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* check, bool condition) noexcept
    {
        std::fprintf(stderr, "%s opacity-context.%s.%s\n", condition ? "PASS" : "FAIL", scenario, check);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cancelLiveState = [this, &settle]() noexcept
    {
        if (_layerMetadataTestGate)
            (void)SetEvent(_layerMetadataTestGate->release.get());
        if (!settle())
            std::_Exit(5);
        if (_opacityEdit)
        {
            const auto id = _opacityEdit->layer;
            (void)QueueHostInput(HostLayerOpacityInput{id, _opacityEdit->start, ChromeEditPhase::Cancel});
            if (!settle())
                std::_Exit(5);
        }
        _session.CancelBrush();
        _session.CancelWarp();
        _session.CancelShape();
        _session.CancelLasso();
        while (_session.history.CaptureState().Depth() != 0)
            _session.EndEdit();
        _layerMetadataTestGate.reset();
        if (!settle())
            std::_Exit(5);
    };
    const auto cleanup = wil::scope_exit([&cancelLiveState] { cancelLiveState(); });
    const auto originalViewport = _viewport;
    const auto originalSmearMode = _session.smearMode;
    const auto restoreViewport = wil::scope_exit(
        [this, originalViewport, originalSmearMode]
        {
            _viewport = originalViewport;
            _session.smearMode = originalSmearMode;
        });

    const auto prepare = [this, &cancelLiveState]() noexcept
    {
        cancelLiveState();
        _session.ClearDocument();
        _session.CreateDocument(4, 3, true);
        _session.AddBlankLayer();
        // Document replacement preserves tool settings; this fixture needs a supported Warp mode.
        _session.smearMode = SmearMode::Liquify;
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->hold = false;
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        DocumentChanged();
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
        return _layerMetadataTestGate->release && _session.ActiveLayer() != nullptr;
    };
    const auto input = [this](Guid id, double value, ChromeEditPhase phase) noexcept
    { return QueueHostInput(HostLayerOpacityInput{id, value, phase}); };

    for (const Case& test : cases)
    {
        if (!prepare())
            return 5;
        const Guid active = _session.ActiveLayerID().value();
        if (test.context == Context::Brush && !_session.BeginBrush({0.5, 0.5}))
            return 5;
        if (test.context == Context::Warp && !_session.BeginWarp({0.5, 0.5}))
            return 5;
        if (test.context == Context::Shape)
            _session.BeginShape({0.5, 0.5});
        if (test.context == Context::Lasso)
            _session.BeginLasso({0.5, 0.5}, SelectionMode::New);
        if (test.context == Context::Nested)
            _session.BeginEdit(L"Literal outer opacity transaction");

        const auto brush = _session.PendingBrush();
        const auto warp = _session.PendingWarp();
        const auto shape = _session.PendingShape();
        const auto lasso = _session.PendingLasso();
        const auto depth = _session.history.CaptureState().Depth();
        const bool live = test.context == Context::Brush   ? brush != nullptr
                          : test.context == Context::Warp  ? warp != nullptr
                          : test.context == Context::Shape ? shape.has_value()
                          : test.context == Context::Lasso ? lasso.has_value()
                                                           : depth == 1;
        const auto before = _session.Document();
        const auto gate = _layerMetadataTestGate;
        auto expected = DocumentFixture::Copy(before.value());
        expected.layers.back().opacity = 0.375;

        const auto preview = input(active, 0.25, ChromeEditPhase::Preview);
        if (!settle() ||
            !expect(test.name, "preview-ack-and-original-opening",
                    live && preview.Read().outcome == HostOutcome::Succeeded && _opacityEdit &&
                        _opacityEdit->layer == active && _opacityEdit->start == 1.0 && _opacityEdit->shown == 0.25 &&
                        _session.ActiveLayer()->opacity == 0.25 && _session.history.CaptureState().Depth() == depth &&
                        _session.history.UndoCount() == 0 && gate->captures.load() == 1 &&
                        gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load() &&
                        _jobs.ReservedMemoryBytes() == 0))
            return 5;
        const auto opening = _opacityEdit->opening;
        const auto previewRevision = _session.history.CurrentRevision();
        gate->retired.store(false);
        gate->retiredOffOwner.store(false);
        gate->hold = true;
        const auto commit = input(active, 0.375, ChromeEditPhase::Commit);
        struct HeldCommit final
        {
            Application& owner;
            std::uint64_t opening;
            bool captured = false;
        } held{*this, opening};
        const auto pump = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<HeldCommit*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message == LayerMetadataTestGate::kMessage)
            {
                const auto gate = state.owner._layerMetadataTestGate;
                state.captured = gate && gate->captures.load() == 2 && gate->reservationHeld.load() &&
                                 state.owner._opacityEdit && state.owner._opacityEdit->opening == state.opening &&
                                 state.owner._opacityEdit->start == 1.0 && state.owner._opacityEdit->shown == 0.25 &&
                                 state.owner._session.ActiveLayer()->opacity == 0.25 &&
                                 state.owner._session.history.UndoCount() == 0 &&
                                 state.owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes;
                gate->hold = false;
                (void)SetEvent(gate->release.get());
                return;
            }
            DispatchMessageThunk(&state.owner, native);
        };
        if (SettleJobs(_jobs, 15000, pump, &held) != SettleResult::Settled)
            return 5;

        const auto& currentLasso = _session.PendingLasso();
        const bool sameLasso =
            currentLasso.has_value() == lasso.has_value() &&
            (!lasso ||
             (currentLasso.value().points == lasso.value().points &&
              currentLasso.value().cursor == lasso.value().cursor && currentLasso.value().mode == lasso.value().mode &&
              currentLasso.value().kind == lasso.value().kind && currentLasso.value().anchor == lasso.value().anchor));
        const bool sameGesture = _session.PendingBrush() == brush && _session.PendingWarp() == warp &&
                                 _session.PendingShape() == shape && sameLasso;
        if (!expect(test.name, "commit-preserves-gesture-and-opening",
                    held.captured && commit.Read().outcome == HostOutcome::Succeeded && !_opacityEdit && sameGesture &&
                        _session.ActiveLayerID() == std::optional(active) &&
                        _session.history.CaptureState().Depth() == depth && gate->captures.load() == 2 &&
                        gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load() &&
                        _jobs.ReservedMemoryBytes() == 0 && !_layerMetadataTicket && !_historyWork.Busy()))
            return 5;

        if (!expect(
                test.name, "literal-final-and-history",
                _session.Document() == std::optional<CanvasDocument>{std::move(expected).Seal()} &&
                    _session.ActiveLayer()->opacity == 0.375 &&
                    (test.context == Context::Nested
                         ? _session.history.UndoCount() == 0 && _session.history.CurrentRevision() == previewRevision
                         : _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Layer Opacity")))
            return 5;

        if (test.context == Context::Nested)
        {
            _session.EndEdit();
            if (!expect(test.name, "outer-transaction-has-one-original-to-final-entry",
                        _session.history.UndoCount() == 1 &&
                            _session.history.UndoName() == L"Literal outer opacity transaction"))
                return 5;
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle())
                return 5;
            const bool undid = _session.ActiveLayer()->opacity == 1.0 &&
                               _session.history.RedoName() == L"Literal outer opacity transaction";
            OnCommand(ChromeHost::kCommandRedo);
            if (!settle())
                return 5;
            if (!expect(test.name, "outer-undo-redo-uses-original-and-final-values",
                        undid && _session.ActiveLayer()->opacity == 0.375 &&
                            _session.history.UndoName() == L"Literal outer opacity transaction"))
                return 5;
        }
        else
        {
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle())
                return 5;
            // UI_ApplicationChrome Canvas interactions: brush/warp/shape consume the first Undo; a lasso
            // outline is dropped and history moves on that same call.
            const bool lassoMovesHistory = test.context == Context::Lasso;
            if (!expect(test.name, "first-undo-follows-gesture-history-policy",
                        !_session.PendingBrush() && !_session.PendingWarp() && !_session.PendingShape() &&
                            !_session.PendingLasso() &&
                            (lassoMovesHistory
                                 ? _session.ActiveLayer()->opacity == 1.0 && _session.history.UndoCount() == 0 &&
                                       _session.history.RedoName() == L"Layer Opacity"
                                 : _session.ActiveLayer()->opacity == 0.375 && _session.history.UndoCount() == 1 &&
                                       _session.history.UndoName() == L"Layer Opacity")))
                return 5;
            if (!lassoMovesHistory)
            {
                OnCommand(ChromeHost::kCommandUndo);
                if (!settle())
                    return 5;
            }
            const bool undid =
                _session.ActiveLayer()->opacity == 1.0 && _session.history.RedoName() == L"Layer Opacity";
            OnCommand(ChromeHost::kCommandRedo);
            if (!settle())
                return 5;
            if (!expect(test.name, "undo-redo-uses-original-and-final-values",
                        undid && _session.ActiveLayer()->opacity == 0.375 &&
                            _session.history.UndoName() == L"Layer Opacity"))
                return 5;
        }
        _session.CancelBrush();
        _session.CancelWarp();
        _session.CancelShape();
        _session.CancelLasso();
        _layerMetadataTestGate.reset();
    }

    for (const Case& test : cases)
    {
        if (test.context == Context::Nested)
            continue;
        if (!prepare())
            return 5;
        const auto reopen = [this, context = test.context]() noexcept
        {
            switch (context)
            {
            case Context::Brush:
                _session.CancelBrush();
                return _session.BeginBrush({0.5, 0.5});
            case Context::Warp:
                _session.CancelWarp();
                return _session.BeginWarp({0.5, 0.5});
            case Context::Shape:
                _session.CancelShape();
                _session.BeginShape({0.5, 0.5});
                return _session.PendingShape().has_value();
            case Context::Lasso:
                _session.CancelLasso();
                _session.BeginLasso({0.5, 0.5}, SelectionMode::New);
                return _session.PendingLasso().has_value();
            case Context::Nested:
                return false;
            }
            return false;
        };
        if (!reopen())
            return 5;
        const auto target = _session.ActiveLayerID().value();
        (void)input(target, .25, ChromeEditPhase::Preview);
        if (!settle())
            return 5;
        const auto opening = _opacityEdit->opening;
        const auto generation = _session.Generation();
        const auto revision = _session.history.CurrentRevision();
        const auto gate = _layerMetadataTestGate;
        gate->retired.store(false);
        gate->retiredOffOwner.store(false);
        gate->hold = true;
        const auto stale = input(target, .375, ChromeEditPhase::Commit);
        struct Reopened final
        {
            Application& owner;
            const decltype(reopen)& action;
            bool visited = false;
            bool changed = false;
        } state{*this, reopen};
        const auto pump = [](void* opaque, void* native) noexcept
        {
            auto& state = *static_cast<Reopened*>(opaque);
            auto& message = *static_cast<MSG*>(native);
            if (message.message != LayerMetadataTestGate::kMessage)
            {
                DispatchMessageThunk(&state.owner, native);
                return;
            }
            state.visited = true;
            state.changed = state.action();
            const auto gate = state.owner._layerMetadataTestGate;
            gate->hold = false;
            (void)SetEvent(gate->release.get());
        };
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(test.name, "reopened-gesture-refuses-held-terminal-without-pixel-generation-change",
                    state.visited && state.changed &&
                        stale.Read() == ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT} && _opacityEdit &&
                        _opacityEdit->opening == opening && _opacityEdit->start == 1.0 && _opacityEdit->shown == .25 &&
                        _session.ActiveLayer()->opacity == .25 && _session.Generation() == generation &&
                        _session.history.CurrentRevision() == revision && _session.history.UndoCount() == 0 &&
                        gate->captures.load() == 2 && gate->retired.load() && gate->retiredOffOwner.load() &&
                        _jobs.ReservedMemoryBytes() == 0))
            return 5;
        const auto brush = _session.PendingBrush();
        const auto warp = _session.PendingWarp();
        const auto shape = _session.PendingShape();
        const auto lasso = _session.PendingLasso();
        gate->retired.store(false);
        gate->retiredOffOwner.store(false);
        const auto retry = input(target, .5, ChromeEditPhase::Commit);
        if (!settle())
            return 5;
        const auto& acknowledgedLasso = _session.PendingLasso();
        const bool sameLasso = acknowledgedLasso.has_value() == lasso.has_value() &&
                               (!lasso || (acknowledgedLasso.value().points == lasso.value().points &&
                                           acknowledgedLasso.value().cursor == lasso.value().cursor &&
                                           acknowledgedLasso.value().mode == lasso.value().mode &&
                                           acknowledgedLasso.value().kind == lasso.value().kind &&
                                           acknowledgedLasso.value().anchor == lasso.value().anchor));
        if (!expect(test.name, "reopened-gesture-fresh-retry-keeps-original-to-final-history",
                    retry.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                        _session.ActiveLayer()->opacity == .5 && _session.history.UndoCount() == 1 &&
                        _session.history.UndoName() == L"Layer Opacity" && _session.PendingBrush() == brush &&
                        _session.PendingWarp() == warp && _session.PendingShape() == shape && sameLasso &&
                        gate->captures.load() == 3 && gate->reservationHeld.load() && gate->retired.load() &&
                        gate->retiredOffOwner.load() && _jobs.ReservedMemoryBytes() == 0))
            return 5;
    }

    if (!prepare())
        return 5;
    const Guid active = _session.ActiveLayerID().value();
    if (!_session.BeginBrush({0.5, 0.5}))
        return 5;
    const auto brush = _session.PendingBrush();
    const auto lasso = _session.PendingLasso();
    auto expected = DocumentFixture::Copy(_session.Document().value());
    const auto preview = input(active, 0.25, ChromeEditPhase::Preview);
    if (!settle() ||
        !expect("escape", "preview-worker-captured-under-reservation",
                preview.Read().outcome == HostOutcome::Succeeded && _opacityEdit && _opacityEdit->start == 1.0 &&
                    _opacityEdit->shown == 0.25 && _layerMetadataTestGate->captures.load() == 1 &&
                    _layerMetadataTestGate->reservationHeld.load() && _layerMetadataTestGate->retired.load() &&
                    _layerMetadataTestGate->retiredOffOwner.load() && _jobs.ReservedMemoryBytes() == 0))
        return 5;
    _layerMetadataTestGate->retired.store(false);
    _layerMetadataTestGate->retiredOffOwner.store(false);
    const auto escape = input(active, 1.0, ChromeEditPhase::Cancel);
    if (!settle() ||
        !expect("escape", "restores-original-without-history-or-gesture-loss",
                escape.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                    _session.ActiveLayer()->opacity == 1.0 &&
                    _session.Document() == std::optional<CanvasDocument>{std::move(expected).Seal()} &&
                    _session.history.UndoCount() == 0 && _session.PendingBrush() == brush &&
                    _session.PendingLasso().has_value() == lasso.has_value() &&
                    _layerMetadataTestGate->captures.load() == 2 && _layerMetadataTestGate->reservationHeld.load() &&
                    _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load() &&
                    _jobs.ReservedMemoryBytes() == 0))
        return 5;
    _session.CancelBrush();
    _session.CancelWarp();
    _session.CancelShape();
    _session.CancelLasso();
    _layerMetadataTestGate.reset();
    if (!expect("cleanup", "no-opacity-edit-gesture-transaction-or-reservation",
                !_opacityEdit && !_session.PendingBrush() && !_session.PendingWarp() && !_session.PendingShape() &&
                    !_session.PendingLasso() && _session.history.CaptureState().Depth() == 0 && !_layerMetadataTicket &&
                    !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0))
        return 5;

    std::fprintf(stderr, "PASS opacity-context.summary checks=%d contexts=%zu\n", checks, cases.size() + 5);
    return 0;
}

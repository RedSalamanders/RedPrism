#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

bool Application::OpenOpacityEditForTest(double value) noexcept
{
    // Fault harnesses change only the live invocation while another worker is held. They must not enqueue a
    // production input or alter the document generation when testing rejection of the original authority.
    const auto* layer = _session.ActiveLayer();
    if (!layer || layer->isGroup || layer->opacity != value || _opacityOpening == UINT64_MAX)
        return false;
    _opacityEdit = OpacityEdit{layer->id, value, value, ++_opacityOpening, _session.DocumentSerial()};
    return true;
}

int Application::RunLayerOpacityHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit([this, originalViewport] { _viewport = originalViewport; });
    int checks = 0;
    const auto expect = [&checks](const char* scenario, const char* name, bool condition) noexcept
    {
        std::fprintf(stderr, "%s opacity-host.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        checks += condition ? 1 : 0;
        return condition;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [this]
        {
            if (_layerMetadataTestGate)
                (void)SetEvent(_layerMetadataTestGate->release.get());
            _layerMetadataTestGate.reset();
            _forcedDiscard.reset();
        });
    const auto prepare = [this, &settle]()
    {
        if (!settle())
            return false;
        _session.ClearDocument();
        _session.CreateDocument(4, 3, true);
        _session.AddBlankLayer();
        _session.history.Reset();
        _session.history.MarkSaved();
        _opacityEdit.reset();
        _opacityRecorded.reset();
        _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
        _layerMetadataTestGate->hold = false;
        _layerMetadataTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        DocumentChanged();
        return _layerMetadataTestGate->release && _session.ActiveLayer();
    };
    const auto input = [this](double value, ChromeEditPhase phase)
    { return QueueHostInput(HostLayerOpacityInput{_session.ActiveLayerID().value(), value, phase}); };
    const auto drained = [this]() noexcept
    {
        return !_activeHostRequest && !_layerMetadataTicket && !_historyWork.Busy() && _hostRequests.Size() == 0 &&
               MetadataAccountingSettledForTest() && _hostInputBudget.Used() == 0 &&
               _layerMetadataTestGate->retired.load() && _layerMetadataTestGate->retiredOffOwner.load();
    };

    if (!prepare())
        return 5;
    const auto preview = input(.25, ChromeEditPhase::Preview);
    if (!expect("preview", "admission-preserves-opening",
                preview.Read().outcome == HostOutcome::Pending && _session.ActiveLayer()->opacity == 1.0 &&
                    !_opacityEdit && _session.history.UndoCount() == 0))
        return 5;
    if (!settle() || !expect("preview", "checked-worker-acknowledges-literal",
                             preview.Read().outcome == HostOutcome::Succeeded &&
                                 _session.ActiveLayer()->opacity == .25 && _opacityEdit && _opacityEdit->start == 1.0 &&
                                 _opacityEdit->shown == .25 && _session.history.UndoCount() == 0 && drained()))
        return 5;
    const auto opening = _opacityEdit->opening;
    const auto next = input(.375, ChromeEditPhase::Preview);
    if (!settle() ||
        !expect("preview", "second-tick-retains-original-model-opening",
                next.Read().outcome == HostOutcome::Succeeded && _opacityEdit && _opacityEdit->opening == opening &&
                    _opacityEdit->start == 1.0 && _opacityEdit->shown == .375 &&
                    _session.ActiveLayer()->opacity == .375 && _session.history.UndoCount() == 0))
        return 5;
    const auto commit = input(.5, ChromeEditPhase::Commit);
    if (!settle() || !expect("commit", "terminal-value-records-one-original-to-final-entry",
                             commit.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                                 _session.ActiveLayer()->opacity == .5 && _session.history.UndoCount() == 1 &&
                                 _session.history.UndoName() == L"Layer Opacity" && drained()))
        return 5;
    OnCommand(ChromeHost::kCommandUndo);
    if (!settle() || !expect("commit", "undo-restores-one",
                             _session.ActiveLayer()->opacity == 1.0 && _session.history.RedoName() == L"Layer Opacity"))
        return 5;
    OnCommand(ChromeHost::kCommandRedo);
    if (!settle() || !expect("commit", "redo-restores-terminal-half",
                             _session.ActiveLayer()->opacity == .5 && _session.history.UndoName() == L"Layer Opacity"))
        return 5;

    if (!prepare())
        return 5;
    (void)input(.25, ChromeEditPhase::Preview);
    if (!settle())
        return 5;
    const auto cancel = input(.375, ChromeEditPhase::Cancel);
    if (!settle() ||
        !expect("escape", "restores-original-without-history",
                cancel.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                    _session.ActiveLayer()->opacity == 1.0 && _session.history.UndoCount() == 0 && drained()))
        return 5;

    if (!prepare())
        return 5;
    const auto seed = input(.5, ChromeEditPhase::Commit);
    if (!settle() || seed.Read().outcome != HostOutcome::Succeeded)
        return 5;
    OnCommand(ChromeHost::kCommandUndo);
    if (!settle())
        return 5;
    const auto revision = _session.history.CurrentRevision();
    (void)input(.25, ChromeEditPhase::Preview);
    if (!settle())
        return 5;
    const auto returnToStart = input(1.0, ChromeEditPhase::Commit);
    if (!settle() ||
        !expect("return", "preserves-redo-and-revision",
                returnToStart.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                    _session.ActiveLayer()->opacity == 1.0 && _session.history.UndoCount() == 0 &&
                    _session.history.RedoName() == L"Layer Opacity" && _session.history.CurrentRevision() == revision))
        return 5;

    if (!prepare())
        return 5;
    const auto older = input(.25, ChromeEditPhase::Preview);
    const auto newest = input(.375, ChromeEditPhase::Preview);
    const auto terminal = input(.5, ChromeEditPhase::Commit);
    if (!expect("coalescence", "only-pending-tail-replaced",
                older.Read().outcome == HostOutcome::Cancelled && newest.Read().outcome == HostOutcome::Pending &&
                    terminal.Read().outcome == HostOutcome::Pending && _hostRequests.Size() == 2 &&
                    _session.ActiveLayer()->opacity == 1.0))
        return 5;
    if (!settle() ||
        !expect("coalescence", "newer-terminal-suppresses-preview-publication",
                newest.Read().outcome == HostOutcome::Cancelled && terminal.Read().outcome == HostOutcome::Succeeded &&
                    _session.ActiveLayer()->opacity == .5 && _session.history.UndoCount() == 1 && !_opacityEdit &&
                    drained()))
        return 5;

    enum class Held
    {
        Newer,
        Command,
        Target,
        View
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Held kind;
        Guid target;
        Guid other;
        ContinuationReceipt first;
        ContinuationReceipt younger;
        ContinuationReceipt terminal;
        bool visited = false;
        bool held = false;
        bool responsive = false;
    };
#pragma warning(pop)

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
        state.held = owner._layerMetadataTicket && owner._historyWork.Busy() && gate->captures.load() == 1 &&
                     gate->reservationHeld.load() && owner._session.ActiveLayer()->opacity == 1.0 &&
                     owner._session.history.UndoCount() == 0 && state.first.Read().outcome == HostOutcome::Pending;
        // The first worker has already observed hold and is waiting. Younger workers start after submission.
        gate->hold = false;
        switch (state.kind)
        {
        case Held::Newer:
            state.younger = owner.QueueHostInput(HostLayerOpacityInput{state.target, .375, ChromeEditPhase::Preview});
            state.terminal = owner.QueueHostInput(HostLayerOpacityInput{state.target, .5, ChromeEditPhase::Commit});
            break;
        case Held::Command:
            state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandToggleLayerVisibility});
            break;
        case Held::Target:
            state.younger = owner.QueueHostInput(HostLayerSelectInput{state.other});
            break;
        case Held::View:
        {
            const double zoom = owner._viewport.zoom;
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            state.responsive = owner._viewport.zoom != zoom && owner._historyWork.Busy() &&
                               owner._session.ActiveLayer()->opacity == 1.0;
            break;
        }
        }
        (void)SetEvent(gate->release.get());
    };
    constexpr std::array heldCases{std::pair{"superseded", Held::Newer}, std::pair{"command", Held::Command},
                                   std::pair{"target", Held::Target}, std::pair{"view", Held::View}};
    for (const auto& [name, kind] : heldCases)
    {
        if (!prepare())
            return 5;
        _layerMetadataTestGate->hold = true;
        if (kind == Held::View)
        {
            // A 4x3 fixture can otherwise fit at the maximum zoom before the held worker begins.
            _viewport.SetZoom(1.0, {}, DocumentSize());
            SyncView();
        }
        const auto target = _session.ActiveLayerID().value();
        const auto other = _session.Document()->layers.front().id;
        State state{*this, kind, target, other, input(.25, ChromeEditPhase::Preview)};
        if (SettleJobs(_jobs, 15000, pump, &state) != SettleResult::Settled ||
            !expect(name, "owner-pumps-held-admitted-worker", state.visited && state.held))
            return 5;
        if (kind == Held::Newer)
        {
            if (!expect(name, "old-preview-retires-before-new-terminal",
                        state.first.Read().outcome == HostOutcome::Cancelled &&
                            state.younger.Read().outcome == HostOutcome::Cancelled &&
                            state.terminal.Read().outcome == HostOutcome::Succeeded &&
                            _session.ActiveLayer()->opacity == .5 && _session.history.UndoCount() == 1 &&
                            !_opacityEdit && drained()))
                return 5;
        }
        else if (kind == Held::Command)
        {
            if (!expect(name, "closure-acknowledges-before-dependent-history",
                        state.first.Read().outcome == HostOutcome::Succeeded &&
                            state.younger.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                            _session.ActiveLayer()->opacity == .25 && !_session.ActiveLayer()->visible &&
                            _session.history.UndoCount() == 2 && _session.history.UndoName() == L"Hide Layer" &&
                            drained()))
                return 5;
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle() || !expect(name, "first-undo-keeps-opacity-entry",
                                     _session.ActiveLayer()->visible && _session.ActiveLayer()->opacity == .25 &&
                                         _session.history.UndoName() == L"Layer Opacity"))
                return 5;
            OnCommand(ChromeHost::kCommandUndo);
            if (!settle() ||
                !expect(name, "second-undo-restores-original-opacity", _session.ActiveLayer()->opacity == 1.0))
                return 5;
        }
        else if (kind == Held::Target)
        {
            const auto old = IndexOf(_session.Document()->layers.Read(), target);
            if (!expect(name, "closure-stays-on-original-target",
                        state.first.Read().outcome == HostOutcome::Succeeded &&
                            state.younger.Read().outcome == HostOutcome::Succeeded && old && !_opacityEdit &&
                            _session.ActiveLayerID() == std::optional(other) &&
                            _session.ActiveLayer()->opacity == 1.0 &&
                            _session.Document()->layers[old.value()].opacity == .25 &&
                            _session.history.UndoCount() == 1 && drained()))
                return 5;
        }
        else if (!expect(name, "view-keeps-edit-and-model-opening",
                         state.responsive && _opacityEdit && _opacityEdit->start == 1.0 && _opacityEdit->shown == .25 &&
                             _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0 && drained()))
            return 5;
    }

    if (!prepare())
        return 5;
    (void)input(.25, ChromeEditPhase::Preview);
    if (!settle())
        return 5;
    const auto oldTarget = _session.ActiveLayerID().value();
    const auto newTarget = _session.Document()->layers.front().id;
    const auto selection = QueueHostInput(HostLayerSelectInput{newTarget});
    const auto oldTargetPreview = QueueHostInput(HostLayerOpacityInput{oldTarget, .375, ChromeEditPhase::Preview});
    const auto oldTargetTerminal = QueueHostInput(HostLayerOpacityInput{oldTarget, .5, ChromeEditPhase::Commit});
    if (!settle() ||
        !expect("target-order", "queued-old-preview-is-observably-stale",
                selection.Read().outcome == HostOutcome::Succeeded &&
                    oldTargetPreview.Read() == ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT} &&
                    _session.ActiveLayerID() == std::optional(newTarget) && _session.ActiveLayer()->opacity == 1.0 &&
                    _session.Document()->layers.back().opacity == .25 && _session.history.UndoCount() == 1 &&
                    !_opacityEdit && drained()))
        return 5;
    if (!expect("target-order", "queued-old-terminal-is-observably-stale",
                oldTargetTerminal.Read() == ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT}))
        return 5;
    const auto lateEqual = QueueHostInput(HostLayerOpacityInput{oldTarget, .25, ChromeEditPhase::Commit});
    if (!settle() ||
        !expect("target-order", "equal-late-terminal-acknowledges-original-closure",
                lateEqual.Read().outcome == HostOutcome::Succeeded && _session.ActiveLayer()->opacity == 1.0 &&
                    _session.Document()->layers.back().opacity == .25 && _session.history.UndoCount() == 1))
        return 5;
    const auto currentTerminal = input(.5, ChromeEditPhase::Commit);
    if (!settle() ||
        !expect("target-order", "new-target-commit-remains-independent",
                currentTerminal.Read().outcome == HostOutcome::Succeeded && _session.ActiveLayer()->opacity == .5 &&
                    _session.Document()->layers.back().opacity == .25 && _session.history.UndoCount() == 2 &&
                    drained()))
        return 5;

    constexpr std::array faults{LayerMetadataPrepareStage::BeforeOpacityOpening,
                                LayerMetadataPrepareStage::AfterOpacityOpening};
    for (const auto stage : faults)
    {
        const auto* name =
            stage == LayerMetadataPrepareStage::BeforeOpacityOpening ? "opening-fault" : "sealed-opening-fault";
        if (!prepare())
            return 5;
        (void)input(.25, ChromeEditPhase::Preview);
        if (!settle())
            return 5;
        const auto original = _opacityEdit.value();
        _layerMetadataTestGate->throwMetadataStage = stage;
        const auto failed = input(.375, ChromeEditPhase::Commit);
        if (!settle() || !expect(name, "failure-keeps-acknowledged-edit-and-history",
                                 failed.Read() == ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} &&
                                     _opacityEdit && _opacityEdit->opening == original.opening &&
                                     _opacityEdit->start == 1.0 && _opacityEdit->shown == .25 &&
                                     _session.ActiveLayer()->opacity == .25 && _session.history.UndoCount() == 0 &&
                                     _layerMetadataTestGate->faultHits.load() == 1 && drained()))
            return 5;
        _layerMetadataTestGate->throwMetadataStage.reset();
        const auto retried = input(.5, ChromeEditPhase::Commit);
        if (!settle() ||
            !expect(name, "retry-retains-original-to-final-history",
                    retried.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                        _session.ActiveLayer()->opacity == .5 && _session.history.UndoCount() == 1 && drained()))
            return 5;
    }
    if (!prepare())
        return 5;
    _layerMetadataTestGate->throwCaptureFailure = true;
    const auto captureFailure = input(.25, ChromeEditPhase::Preview);
    if (!settle() || !expect("capture-fault", "failure-consumes-no-model-change",
                             captureFailure.Read() == ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} &&
                                 _session.ActiveLayer()->opacity == 1.0 && _session.history.UndoCount() == 0 &&
                                 _opacityEdit && _opacityEdit->start == 1.0 && drained()))
        return 5;
    _layerMetadataTestGate->throwCaptureFailure = false;
    const auto retry = input(.375, ChromeEditPhase::Preview);
    if (!settle() ||
        !expect("capture-fault", "retry-acknowledges-literal-preview",
                retry.Read().outcome == HostOutcome::Succeeded && _session.ActiveLayer()->opacity == .375 &&
                    _opacityEdit && _opacityEdit->shown == .375 && drained()))
        return 5;
    const auto finalCancel = input(1.0, ChromeEditPhase::Cancel);
    if (!settle() ||
        !expect("cleanup", "fixture-leaves-no-live-field-or-reservation",
                finalCancel.Read().outcome == HostOutcome::Succeeded && !_opacityEdit &&
                    _session.ActiveLayer()->opacity == 1.0 && _session.history.UndoCount() == 0 && drained()))
        return 5;
    std::fprintf(stderr, "PASS opacity-host.summary checks=%d\n", checks);
    return 0;
}

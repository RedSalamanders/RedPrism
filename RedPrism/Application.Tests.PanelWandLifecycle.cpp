#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "Runtime.Tests.WandLayerHostTestGate.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunPanelWandLifecycleSelfTest() noexcept
{
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    if (!_chrome || !_hostSourceRegistered || !settled() || !_session.Document() || _session.HasColorEdit())
        return 5;
    const auto original = std::make_unique<CanvasDocument>(_session.Document().value());
    const auto originalActive = _session.ActiveLayerID();
    const auto originalPath = _documentPath;
    const auto originalTool = _tool;
    const auto originalWand = _session.wandSettings;
    const auto originalAa = _session.selectionAntialiased;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_wandLayerHostTestGate)
                (void)SetEvent(_wandLayerHostTestGate->release.get());
            CancelHostContinuations();
            if (!settled())
                std::_Exit(5);
            _wandLayerHostTestGate.reset();
            CancelColorEditForTest();
            ReplaceDocument(*original, originalActive, originalPath);
            _tool = originalTool;
            _session.wandSettings = originalWand;
            _session.selectionAntialiased = originalAa;
            SyncChrome();
        });
    struct State final
    {
        Application& owner;
        int mode;
        std::shared_ptr<WandLayerHostTestGate> gate;
        ContinuationReceipt receipt;
        ContinuationReceipt panelCancel;
        std::uint64_t opening = 0;
        Guid revision;
        Raster source;
        bool visited = false, held = false, afterTerminalStillCharged = false;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != WandLayerHostTestGate::kMessage)
        {
            DispatchMessageThunk(&owner, native);
            return;
        }
        state.visited = true;
        const auto charged = owner._jobs.ReservedMemoryBytes();
        const auto originalTicket = owner._wandLayerHostTicket;
        state.held = owner._wandLayerHostTicket && owner._wandLayerHostStamp && owner._historyWork.Busy() &&
                     state.receipt.Read().outcome == HostOutcome::Pending && state.gate->captures.load() == 1 &&
                     state.gate->reservationBytes != 0 && charged >= state.gate->reservationBytes &&
                     owner.ColorControlsFrozen() && !owner._session.Selection() &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._session.ActiveLayer()->asset->image == state.source && owner._session.PendingColorEdit() &&
                     owner._session.PendingColorEdit()->openingSequence == state.opening;
        if (state.mode == 0)
        {
            (void)owner._historyWork.RequestCancel();
            if (owner._wandLayerHostTicket)
                (void)owner._jobs.Cancel(owner._wandLayerHostTicket.value());
        }
        else if (state.mode == 2)
        {
            owner._session.CancelColorEdit();
            owner._session.BeginColorEdit(FilterKind::Levels, true);
        }
        if (state.mode == 3)
        {
            owner.OnAdjustAction(AdjustAction::Cancel, 0);
            state.panelCancel = owner._pendingCancelReceipt;
        }
        // A real panel Cancel may retire its unrelated preview reservation. Bind this observation
        // to the still-held original Wand ticket and its complete declared charge, not the global sum.
        state.afterTerminalStillCharged = owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes &&
                                          state.receipt.Read().outcome == HostOutcome::Pending && originalTicket &&
                                          owner._wandLayerHostTicket == originalTicket;
        (void)SetEvent(state.gate->release.get());
    };
    const std::array names{"cancel", "worker-failure", "equal-panel-reopen", "panel-cancel"};
    int assertions = 0;
    const auto check = [&](bool value, bool composite, const char* mode, const char* fact) noexcept
    {
        ++assertions;
        std::printf("%s panel-wand-lifecycle.%s.%s.%s\n", value ? "PASS" : "FAIL", composite ? "composite" : "layer",
                    mode, fact);
        return value;
    };
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 0, 0, 255};
    const auto raster = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    for (const bool composite : {false, true})
        for (int mode = 0; mode < 4; ++mode)
        {
            CancelColorEditForTest();
            if (!settled())
                return 5;
            DocumentFixture fixture;
            fixture.id = Guid::Generate();
            fixture.width = 2;
            fixture.height = 2;
            auto layer = ImageLayer::Blank(L"Held panel Wand source", {2, 2});
            const auto active = layer.id;
            layer.asset = RasterAsset{raster, {}, layer.name};
            layer.transform.sampling = LayerSampling::Nearest;
            fixture.layers.push_back(std::move(layer));
            ReplaceDocument(std::move(fixture).Seal(), active, {});
            _session.history.Reset();
            _session.history.MarkSaved();
            _session.wandSettings = {};
            _session.wandSettings.tolerance = 0;
            _session.wandSettings.sampleSize = WandSampleSize::Point;
            _session.wandSettings.sampleAllLayers = composite;
            _session.selectionAntialiased = false;
            _tool = EditorTool::Wand;
            if (!settled() || !BeginColorEdit(ChromeHost::kCommandLevels) || !settled())
                return 5;
            auto gate = std::make_shared<WandLayerHostTestGate>();
            gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!gate->release)
                return 5;
            gate->result = mode == 1 ? JobResult{JobStatus::Failed, E_FAIL} : JobResult{};
            _wandLayerHostTestGate = gate;
            const auto opening = _session.PendingColorEdit()->openingSequence;
            RunMagicWand({.2, .2}, SelectionMode::New);
            const auto* entry = _hostRequests.Size() ? _hostRequests.PendingAt(_hostRequests.Size() - 1) : nullptr;
            if (!entry)
                return 5;
            auto state = std::make_unique<State>(State{
                *this, mode, gate, entry->input.Receipt(), {}, opening, _session.history.CurrentRevision(), raster});
            const auto result = SettleJobs(_jobs, 15000, pump, state.get());
            if (!check(result == SettleResult::Settled && state->visited && state->held, composite, names[mode],
                       "original-graph-held-before-terminal"))
                return 5;
            if (!check(state->afterTerminalStillCharged, composite, names[mode], "no-early-refund-or-receipt"))
                return 5;
            const auto outcome = mode == 0 || mode == 3 ? HostOutcome::Cancelled
                                 : mode == 1            ? HostOutcome::Failed
                                                        : HostOutcome::Stale;
            if (!check(state->receipt.Read().outcome == outcome && !_wandLayerHostTicket && !_historyWork.Busy() &&
                           !PanelWandBusyOwner() && gate->retired.load(),
                       composite, names[mode], "original-terminal-joins-and-retires"))
                return 5;
            if (!check(!_session.Selection() && _session.history.UndoCount() == 0 &&
                           _session.history.CurrentRevision() == state->revision &&
                           _session.ActiveLayer()->asset->image == raster &&
                           (mode == 3 ? !_session.PendingColorEdit() &&
                                            state->panelCancel.Read().outcome == HostOutcome::Succeeded
                                      : _session.PendingColorEdit() &&
                                            (mode == 2 ? _session.PendingColorEdit()->openingSequence != opening
                                                       : _session.PendingColorEdit()->openingSequence == opening)),
                       composite, names[mode], "source-history-and-live-panel-preserved"))
                return 5;
            _wandLayerHostTestGate.reset();
        }
    std::printf("PASS panel-wand-lifecycle (%d assertions)\n", assertions);
    return 0;
}

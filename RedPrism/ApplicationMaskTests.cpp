#include "Application.h"

#include "ChromeHost.h"
#include "MaskWorkTestGate.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunMaskWorkSelfTest() noexcept
{
    enum class Mode
    {
        Success,
        Cancel,
        LateCancel,
        Failure,
        Allocation,
        Admission,
        Capture,
        HistoryBefore,
        HistoryAfter,
        HistoryEntry,
        HistoryStack,
        HistoryTrim,
        StaleSource,
        StaleSelection,
        StaleHistory,
        Replace,
        Undo,
        LargeRelease
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> undo;
        Guid revision;
        Raster source;
        bool visited = false, held = false, cancelledRetained = false, viewChanged = false;
    };
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* fact, Mode mode, bool after, int kind) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s mask-host.kind-%d.after-%d.mode-%d.%s\n", condition ? "PASS" : "FAIL", kind, after,
                    static_cast<int>(mode), fact);
        std::fflush(stdout);
    };
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != MaskWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const auto* layer = owner._session.ActiveLayer();
        state.held = owner._activeHostRequest && owner._maskTicket && owner._maskStamp && owner._historyWork.Busy() &&
                     owner._hostCommitStamp == owner._maskStamp && owner._maskWorkTestGate->captureReservationHeld &&
                     owner._jobs.ReservedMemoryBytes() == owner._maskWorkTestGate->reservationBytes &&
                     state.receipt.Read().outcome == HostOutcome::Pending &&
                     (!state.undo || state.undo->Read().outcome == HostOutcome::Pending) &&
                     owner._session.history.CurrentRevision() == state.revision && layer && layer->asset &&
                     layer->asset->image == state.source && !layer->mask;
        const auto zoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewChanged = owner._viewport.zoom != zoom;
        if (state.mode == Mode::Cancel || state.mode == Mode::LargeRelease)
        {
            owner.CancelHostContinuations();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._maskTicket &&
                                      owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.mode == Mode::LateCancel)
        {
            (void)owner._historyWork.RequestCancel();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._maskTicket;
        }
        else if (state.mode == Mode::StaleSource)
            owner._session.EditForTest([](CanvasDocument& document) { document.layers.back().opacity = 0.5; });
        else if (state.mode == Mode::StaleSelection)
            owner._session.Deselect();
        else if (state.mode == Mode::StaleHistory)
            owner._session.history.Reset();
        else if (state.mode == Mode::Replace)
        {
            auto document = owner._session.Document().value();
            const auto active = owner._session.ActiveLayerID();
            owner._session.ReplaceDocument(std::move(document), active);
        }
        (void)SetEvent(owner._maskWorkTestGate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_maskWorkTestGate)
                (void)SetEvent(_maskWorkTestGate->release.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _maskWorkTestGate.reset();
        });
    for (const bool after : {false, true})
        for (int kind = 0; kind < 5; ++kind)
            for (const auto mode :
                 {Mode::Success, Mode::Cancel, Mode::LateCancel, Mode::Failure, Mode::Allocation, Mode::Admission,
                  Mode::Capture, Mode::HistoryBefore, Mode::HistoryAfter, Mode::HistoryEntry, Mode::HistoryStack,
                  Mode::HistoryTrim, Mode::StaleSource, Mode::StaleSelection, Mode::StaleHistory, Mode::Replace,
                  Mode::Undo, Mode::LargeRelease})
            {
                const bool historyFailure = mode >= Mode::HistoryBefore && mode <= Mode::HistoryTrim;
                if ((kind != 0 && mode != Mode::Success) || (after && (mode == Mode::Allocation || historyFailure)) ||
                    (mode == Mode::LargeRelease && !after))
                    continue;
                _session.CreateDocument(100, 80);
                const int side = mode == Mode::LargeRelease ? 2048 : 100;
                const auto target = InsertPaintedLayer(_session, L"Mask baseline", side,
                                                       mode == Mode::LargeRelease ? 2048 : 40, {0, 0}, 0, 0xFFFF0000);
                _session.SelectLayer(target);
                _session.EditForTest(
                    [&](CanvasDocument& document)
                    {
                        if (kind < 2 || mode == Mode::LargeRelease)
                            document.selection = DocumentSelection{true, SelectionGeometry::Rect(20, 10, 30, 20)};
                        else if (kind == 4)
                            document.selection = DocumentSelection{true, {}};
                    });
                _session.history.Reset();
                _session.history.MarkSaved();
                _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
                DocumentChanged();
                const auto source = _session.ActiveLayer()->asset->image;
                const auto revision = _session.history.CurrentRevision();
                const auto presents = _renderer.PresentCount();
                _maskWorkTestGate = std::make_shared<MaskWorkTestGate>();
                if (!_maskWorkTestGate->release)
                    return 5;
                _maskWorkTestGate->afterPreparation = after;
                _maskWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
                _maskWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
                _maskWorkTestGate->throwCaptureFailure = mode == Mode::Capture;
                if (historyFailure)
                {
                    constexpr std::array stages{
                        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                        DocumentHistory::PrepareStage::TrimIndex};
                    _maskWorkTestGate->historyFailure =
                        stages[static_cast<std::size_t>(mode) - static_cast<std::size_t>(Mode::HistoryBefore)];
                }
                if (mode == Mode::Failure)
                    _maskWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                const bool revealing = kind != 1 && kind != 3;
                State state{*this,
                            mode,
                            QueueHostInput(HostCommandInput{revealing ? ChromeHost::kCommandAddMask
                                                                      : ChromeHost::kCommandAddBlackMask}),
                            {},
                            revision,
                            source};
                if (mode == Mode::Undo)
                    state.undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
                    std::_Exit(5);
                const auto actual = state.receipt.Read();
                const auto expected = mode == Mode::Cancel || mode == Mode::LateCancel || mode == Mode::LargeRelease
                                          ? HostOutcome::Cancelled
                                      : mode >= Mode::StaleSource && mode <= Mode::Replace ? HostOutcome::Stale
                                      : mode >= Mode::Failure && mode <= Mode::HistoryTrim ? HostOutcome::Failed
                                                                                           : HostOutcome::Succeeded;
                expect(actual.outcome == expected, "outcome", mode, after, kind);
                expect(mode == Mode::Admission || mode == Mode::Capture
                           ? !state.visited
                           : state.visited && state.held && state.viewChanged,
                       "held-and-view-input", mode, after, kind);
                expect(!_maskTicket && !_maskStamp && !_historyWork.Busy() && !_hostCommitStamp &&
                           !_activeHostRequest && !_hostRequests.Size() && !_hostInputBudget.Used() &&
                           !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes() &&
                           _renderer.PresentCount() == presents,
                       "complete-retirement-no-present", mode, after, kind);
                if (mode == Mode::Cancel || mode == Mode::LateCancel || mode == Mode::LargeRelease)
                    expect(state.cancelledRetained && actual.error == E_ABORT, "cancel-acknowledgement", mode, after,
                           kind);
                if (mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::Capture || historyFailure)
                    expect(actual.error == E_OUTOFMEMORY, "allocation-error", mode, after, kind);
                if (historyFailure)
                    expect(_maskWorkTestGate->historyFaultHits == 1, "history-fault-visited", mode, after, kind);
                const auto* layer = _session.ActiveLayer();
                if (mode == Mode::Success)
                {
                    const bool selected = kind < 2 || kind == 4;
                    const bool ready = layer && layer->mask && layer->asset && layer->asset->image == source;
                    std::uint8_t interior = 42, exterior = 42;
                    if (ready)
                    {
                        const auto& mask = layer->mask->asset.image;
                        mask.ReadRegion(selected ? 30 : 0, selected ? 20 : 0, 1, 1, &interior, 1, 0);
                        mask.ReadRegion(selected ? 5 : 0, selected ? 5 : 0, 1, 1, &exterior, 1, 0);
                    }
                    expect(ready && interior == (kind == 0 || kind == 3 ? 0 : 255) &&
                               exterior == (revealing ? 255 : 0) && _session.IsMaskSelected() &&
                               !_session.Selection() && layer->mask->asset.thumbnail.HasStorage() &&
                               MaskEdgeBackground(layer->mask->asset.thumbnail) == (revealing ? 1.0 : 0.0) &&
                               _session.history.UndoCount() == 1 &&
                               _session.history.UndoName() == (selected    ? L"Add Mask from Selection"
                                                               : revealing ? L"Add Reveal-All Mask"
                                                                           : L"Add Hide-All Mask"),
                           "literal-polarity-eager-thumbnail-target-and-one-history-entry", mode, after, kind);
                }
                else if (mode == Mode::Undo)
                    expect(state.undo && state.undo->Read().outcome == HostOutcome::Succeeded && layer &&
                               !layer->mask && layer->asset->image == source && _session.Selection() &&
                               _session.history.UndoCount() == 0,
                           "younger-undo-restores-source-outline", mode, after, kind);
                else
                    expect(layer && !layer->mask &&
                               _session.history.UndoCount() == (mode == Mode::StaleSelection ? 1 : 0),
                           "no-mask-publication", mode, after, kind);
                if (mode != Mode::Admission && mode != Mode::Capture)
                    expect(_maskWorkTestGate->retired && _maskWorkTestGate->retiredOffOwner, "aggregate-off-owner",
                           mode, after, kind);
                _maskWorkTestGate.reset();
            }
    std::printf("mask-host summary: %u checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}

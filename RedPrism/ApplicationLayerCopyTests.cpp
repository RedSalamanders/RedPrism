#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerCopyWorkTestGate.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

int Application::RunLayerCopyWorkSelfTest() noexcept
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
        Guid revision, active;
        Raster source, clipboard;
        std::uint64_t clipboardSerial;
        bool visited = false, held = false, cancelledRetained = false, viewChanged = false;
    };
    unsigned checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* fact, Mode mode, bool after, int kind) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s layer-copy-host.kind-%d.after-%d.mode-%d.%s\n", condition ? "PASS" : "FAIL", kind, after,
                    static_cast<int>(mode), fact);
        std::fflush(stdout);
    };
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerCopyWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const auto* layer = owner._session.ActiveLayer();
        state.held =
            owner._activeHostRequest && owner._layerCopyTicket && owner._layerCopyStamp && owner._historyWork.Busy() &&
            owner._hostCommitStamp == owner._layerCopyStamp && owner._layerCopyWorkTestGate->captureReservationHeld &&
            owner._jobs.ReservedMemoryBytes() == owner._layerCopyWorkTestGate->reservationBytes &&
            state.receipt.Read().outcome == HostOutcome::Pending &&
            (!state.undo || state.undo->Read().outcome == HostOutcome::Pending) &&
            owner._session.history.CurrentRevision() == state.revision && layer && layer->id == state.active &&
            layer->asset && layer->asset->image == state.source && owner._session.Document()->layers.size() == 1;
        const auto zoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewChanged = owner._viewport.zoom != zoom;
        if (state.mode == Mode::Cancel || state.mode == Mode::LargeRelease)
        {
            owner.CancelHostContinuations();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._layerCopyTicket &&
                                      owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.mode == Mode::LateCancel)
        {
            (void)owner._historyWork.RequestCancel();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._layerCopyTicket;
        }
        else if (state.mode == Mode::StaleSource)
            EditLayerRecordsForTest(owner._session,
                                    [](DocumentFixture& document) { document.layers.back().opacity = 0.5; });
        else if (state.mode == Mode::StaleSelection)
            owner._session.Deselect();
        else if (state.mode == Mode::StaleHistory)
            owner._session.history.Reset();
        else if (state.mode == Mode::Replace)
        {
            auto document = owner._session.Document().value();
            const auto active = owner._session.ActiveLayerID();
            owner._session.ReplaceDocument(std::move(document), active);
            state.clipboard = owner._session.Clipboard() ? owner._session.Clipboard()->image : Raster{};
            state.clipboardSerial = owner._session.ClipboardSerial();
        }
        (void)SetEvent(owner._layerCopyWorkTestGate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_layerCopyWorkTestGate)
                (void)SetEvent(_layerCopyWorkTestGate->release.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _layerCopyWorkTestGate.reset();
        });
    for (const bool after : {false, true})
        for (int kind = 0; kind < 3; ++kind)
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
                const int side = mode == Mode::LargeRelease ? 2048 : 8;
                const auto target = InsertPaintedLayer(_session, L"Copy baseline", side,
                                                       mode == Mode::LargeRelease ? 2048 : 6, {0, 0}, 0, 0xFFFF0000);
                _session.SelectLayer(target);
                _session.EditForTest(
                    [](CanvasDocument& document)
                    { document.selection = DocumentSelection{true, SelectionGeometry::Rect(2, 1, 3, 2)}; });
                if (!_session.CopySelection() || !_session.Clipboard())
                    return 5;
                if (kind == 1)
                    _session.Deselect();
                if (kind == 2)
                {
                    EditLayerRecordsForTest(_session,
                                            [](DocumentFixture& document)
                                            {
                                                auto& layer = document.layers.back();
                                                const auto gray =
                                                    Raster::MakeSparse(8, 6, RasterFormat::Gray8, {}, 0, 0, {}, 85);
                                                layer.mask = LayerMask{};
                                                layer.mask->asset =
                                                    RasterAsset{gray, Raster::MakeThumbnail(gray), L"Literal gray"};
                                            });
                    _session.SelectLayerTarget(target, true);
                }
                _session.history.Reset();
                _session.history.MarkSaved();
                _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
                DocumentChanged();
                const auto source = _session.ActiveLayer()->asset->image;
                const auto revision = _session.history.CurrentRevision();
                const auto presents = _renderer.PresentCount();
                _layerCopyWorkTestGate = std::make_shared<LayerCopyWorkTestGate>();
                if (!_layerCopyWorkTestGate->release)
                    return 5;
                _layerCopyWorkTestGate->afterPreparation = after;
                _layerCopyWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
                _layerCopyWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
                _layerCopyWorkTestGate->throwCaptureFailure = mode == Mode::Capture;
                if (historyFailure)
                {
                    constexpr std::array stages{
                        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                        DocumentHistory::PrepareStage::TrimIndex};
                    _layerCopyWorkTestGate->historyFailure =
                        stages[static_cast<std::size_t>(mode) - static_cast<std::size_t>(Mode::HistoryBefore)];
                }
                if (mode == Mode::Failure)
                    _layerCopyWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                State state{*this,
                            mode,
                            QueueHostInput(HostCommandInput{ChromeHost::kCommandLayerViaCopy}),
                            {},
                            revision,
                            target,
                            source,
                            _session.Clipboard()->image,
                            _session.ClipboardSerial()};
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
                expect(!_layerCopyTicket && !_layerCopyStamp && !_historyWork.Busy() && !_hostCommitStamp &&
                           !_activeHostRequest && !_hostRequests.Size() && !_hostInputBudget.Used() &&
                           !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes() &&
                           _renderer.PresentCount() == presents,
                       "complete-retirement-no-present", mode, after, kind);
                expect(_session.ClipboardSerial() == state.clipboardSerial &&
                           (_session.Clipboard() ? _session.Clipboard()->image : Raster{}) == state.clipboard,
                       "private-clipboard-identity-and-serial-preserved", mode, after, kind);
                if (mode == Mode::Cancel || mode == Mode::LateCancel || mode == Mode::LargeRelease)
                    expect(state.cancelledRetained && actual.error == E_ABORT, "cancel-acknowledgement", mode, after,
                           kind);
                if (mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::Capture || historyFailure)
                    expect(actual.error == E_OUTOFMEMORY, "allocation-error", mode, after, kind);
                if (historyFailure)
                    expect(_layerCopyWorkTestGate->historyFaultHits == 1, "history-fault-visited", mode, after, kind);
                const auto* layer = _session.ActiveLayer();
                if (mode == Mode::Success)
                {
                    std::array<std::uint8_t, 4> pixel{};
                    if (layer && layer->asset)
                        layer->asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
                    const std::array<std::uint8_t, 4> literal = kind == 2 ? std::array<std::uint8_t, 4>{85, 85, 85, 255}
                                                                          : std::array<std::uint8_t, 4>{0, 0, 255, 255};
                    expect(layer && layer->id != target && layer->asset && _session.Document()->layers.size() == 2 &&
                               layer->asset->image.Width() == (kind == 1 ? 8 : 3) &&
                               layer->asset->image.Height() == (kind == 1 ? 6 : 2) && pixel == literal &&
                               layer->transform.origin == (kind == 1 ? Vec2{0, 0} : Vec2{2, 1}) &&
                               !_session.Selection() && !_session.IsMaskSelected() &&
                               _session.history.UndoCount() == 1 &&
                               _session.history.UndoName() == (kind == 1 ? L"Duplicate Layer" : L"Layer via Copy"),
                           "literal-grid-origin-target-and-one-history-entry", mode, after, kind);
                }
                else if (mode == Mode::Undo)
                    expect(state.undo && state.undo->Read().outcome == HostOutcome::Succeeded && layer &&
                               layer->id == target && layer->asset && layer->asset->image == source &&
                               _session.Document()->layers.size() == 1 && _session.Selection() &&
                               _session.history.UndoCount() == 0,
                           "younger-undo-restores-source-outline", mode, after, kind);
                else
                    expect(_session.Document()->layers.size() == 1 &&
                               _session.history.UndoCount() == (mode == Mode::StaleSelection ? 1 : 0),
                           "no-layer-publication", mode, after, kind);
                if (mode != Mode::Admission && mode != Mode::Capture)
                    expect(_layerCopyWorkTestGate->retired && _layerCopyWorkTestGate->retiredOffOwner,
                           "aggregate-off-owner", mode, after, kind);
                _layerCopyWorkTestGate.reset();
            }
    std::printf("layer-copy-host summary: %u checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : RunImageWrapperResidencySelfTest(1);
}

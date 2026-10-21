#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.ShapeWorkTestGate.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

int Application::RunShapeWorkSelfTest() noexcept
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
        bool visited = false;
        bool held = false;
        bool cancelledRetained = false;
        bool viewChanged = false;
        bool observeSelectedReceipt = false;
    };
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name, Mode mode, bool after, int kind) noexcept
    {
        ++checks;
        if (!condition)
            ++failures;
        std::printf("%s shape-host.kind-%d.after-%d.mode-%d.%s\n", condition ? "PASS" : "FAIL", kind, after,
                    static_cast<int>(mode), name);
        std::fflush(stdout);
    };
    const auto dispatch = [](void* value, void* native) noexcept
    {
        auto& state = *static_cast<State*>(value);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ShapeWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        if (state.observeSelectedReceipt && owner._activeHostRequest)
            state.receipt = owner._activeHostRequest->input.Receipt();
        const auto* layer = owner._session.ActiveLayer();
        state.held = owner._activeHostRequest && owner._shapeTicket && owner._shapeStamp && owner._historyWork.Busy() &&
                     owner._hostCommitStamp == owner._shapeStamp && owner._shapeWorkTestGate->captureReservationHeld &&
                     owner._jobs.ReservedMemoryBytes() == owner._shapeWorkTestGate->reservationBytes &&
                     state.receipt.Read().outcome == HostOutcome::Pending &&
                     (!state.undo || state.undo->Read().outcome == HostOutcome::Pending) &&
                     owner._session.history.CurrentRevision() == state.revision && layer && layer->asset &&
                     layer->asset->image == state.source && owner._shapeCommitPreview &&
                     owner._shapePreviewDocumentSerial == owner._session.DocumentSerial();
        const auto zoom = owner._viewport.zoom;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        state.viewChanged = owner._viewport.zoom != zoom;
        owner._session.SetForegroundColor(0xFF0000FF); // The owned release keeps its original opaque red color.
        if (state.mode == Mode::Cancel || state.mode == Mode::LargeRelease)
        {
            owner.CancelHostContinuations();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._shapeTicket &&
                                      owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() != 0;
        }
        else if (state.mode == Mode::LateCancel)
        {
            (void)owner._historyWork.RequestCancel();
            state.cancelledRetained = state.receipt.Read().outcome == HostOutcome::Pending && owner._shapeTicket;
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
            owner._session.ReplaceDocument(std::move(document), active); // Persisted IDs all remain identical.
        }
        (void)SetEvent(owner._shapeWorkTestGate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_shapeWorkTestGate)
                (void)SetEvent(_shapeWorkTestGate->release.get());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5); // Borrowed owners survive a failed join.
            _shapeWorkTestGate.reset();
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
                const auto target = InsertPaintedLayer(_session, L"Shape baseline", 4, 2, {0, 0}, 0, 0xFF123456);
                _session.SelectLayer(target);
                _session.SelectAll();
                _session.history.Reset();
                _session.history.MarkSaved();
                _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
                _session.SetForegroundColor(0x88FF0000); // Shapes deliberately ignore palette alpha.
                DocumentChanged();
                const auto layers = _session.Document()->layers.size();
                const auto count = _session.history.UndoCount();
                const auto source = _session.ActiveLayer()->asset->image;
                const auto revision = _session.history.CurrentRevision();
                const auto presents = _renderer.PresentCount();
                _shapeWorkTestGate = std::make_shared<ShapeWorkTestGate>();
                if (!_shapeWorkTestGate->release)
                    return 5;
                _shapeWorkTestGate->afterPreparation = after;
                _shapeWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
                _shapeWorkTestGate->throwAdmissionFailure = mode == Mode::Admission;
                _shapeWorkTestGate->throwCaptureFailure = mode == Mode::Capture;
                if (historyFailure)
                {
                    constexpr std::array stages{
                        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                        DocumentHistory::PrepareStage::TrimIndex};
                    _shapeWorkTestGate->historyFailure =
                        stages[static_cast<std::size_t>(mode) - static_cast<std::size_t>(Mode::HistoryBefore)];
                }
                if (mode == Mode::Failure)
                    _shapeWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                ShapeDraft draft{kind == 1 ? ShapeKind::Ellipse : ShapeKind::Rectangle,
                                 {10, 10},
                                 {10, 10, 40, 30},
                                 kind == 2 ? 8.0 : 0.0};
                if (mode == Mode::LargeRelease)
                    draft.rect = {0, 0, 2048, 2048};
                State state{*this, mode,     QueueHostInput(HostShapeFinishInput{draft, _session.ForegroundColor()}),
                            {},    revision, source};
                if (mode == Mode::Undo)
                    state.undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
                if (settled != SettleResult::Settled)
                    std::_Exit(5);
                const auto actual = state.receipt.Read();
                const auto expected = mode == Mode::Cancel || mode == Mode::LateCancel || mode == Mode::LargeRelease
                                          ? HostOutcome::Cancelled
                                      : mode >= Mode::StaleSource && mode <= Mode::Replace ? HostOutcome::Stale
                                      : mode >= Mode::Failure && mode <= Mode::HistoryTrim ? HostOutcome::Failed
                                                                                           : HostOutcome::Succeeded;
                const bool noGate = mode == Mode::Admission || mode == Mode::Capture;
                expect(actual.outcome == expected, "outcome", mode, after, kind);
                expect(noGate ? !state.visited : state.visited && state.held && state.viewChanged,
                       "held-and-view-input", mode, after, kind);
                expect(!_shapeTicket && !_shapeStamp && !_historyWork.Busy() && !_hostCommitStamp &&
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
                    expect(_shapeWorkTestGate->historyFaultHits == 1, "history-fault-visited", mode, after, kind);
                if (mode == Mode::Success)
                {
                    const auto* layer = _session.ActiveLayer();
                    const bool ready = layer && layer->asset;
                    std::array<std::uint8_t, 4> pixel{};
                    if (ready)
                        layer->asset->image.ReadRegion(15, 10, 1, 1, pixel.data(), 4, 0);
                    expect(ready && _session.Document()->layers.size() == layers + 1 &&
                               layer->name == (kind == 1 ? L"Ellipse 1" : L"Rectangle 1") &&
                               layer->transform.origin == Vec2{10, 10} && layer->transform.size == Vec2{30, 20} &&
                               pixel == std::array<std::uint8_t, 4>{0, 0, 255, 255} && LiveShape(*layer) &&
                               layer->shapeImage == layer->asset->image && _session.Selection() &&
                               _session.history.UndoCount() == count + 1 &&
                               _session.history.UndoName() == (kind == 1 ? L"Ellipse" : L"Rectangle"),
                           "captured-color-layer-selection-history", mode, after, kind);
                }
                else if (mode == Mode::Undo)
                    expect(state.undo && state.undo->Read().outcome == HostOutcome::Succeeded &&
                               _session.Document()->layers.size() == layers && _session.ActiveLayerID() == target &&
                               _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == count,
                           "younger-undo", mode, after, kind);
                else
                    expect(_session.Document()->layers.size() == layers &&
                               _session.history.UndoCount() == count + (mode == Mode::StaleSelection ? 1 : 0) &&
                               (mode != Mode::StaleSelection || _session.history.UndoName() == L"Deselect"),
                           "no-shape-publication", mode, after, kind);
                if (mode != Mode::Admission && mode != Mode::Capture)
                    expect(_shapeWorkTestGate->retired && _shapeWorkTestGate->retiredOffOwner, "aggregate-off-owner",
                           mode, after, kind);
                _shapeWorkTestGate.reset();
            }

    // Exercise the actual Win32 producers, including the different ordinary-leave and capture-loss contracts.
    for (int ending = 0; ending < 3; ++ending)
    {
        _session.CreateDocument(100, 80);
        const auto target = InsertPaintedLayer(_session, L"Pointer shape baseline", 4, 2, {0, 0}, 0, 0xFF123456);
        _session.SelectLayer(target);
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.shapeKind = ShapeKind::Rectangle;
        _session.shapeCornerRadius = 0;
        _session.SetForegroundColor(0x88FF0000);
        OnTool(EditorTool::Shape);
        _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
        DocumentChanged();
        const auto layers = _session.Document()->layers.size();
        const auto revision = _session.history.CurrentRevision();
        const auto source = _session.ActiveLayer()->asset->image;
        const auto pointer = [&](UINT message, WPARAM keys, Vec2 point) noexcept
        {
            const auto hole = _chrome->HolePixels();
            const auto view = _viewport.ViewFromDocument(point, DocumentSize());
            OnPointer(message, keys,
                      MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                 static_cast<int>(std::lround(hole.top + view.y))));
        };
        pointer(WM_LBUTTONDOWN, 0, {10, 10});
        pointer(WM_MOUSEMOVE, MK_LBUTTON, {40, 30});
        expect(_session.PendingShape().has_value(), "pointer-starts-live-draft", Mode::Success, false, ending);
        if (!_session.PendingShape())
            return 5;
        const auto owned = _session.PendingShape().value();
        _shapeWorkTestGate = std::make_shared<ShapeWorkTestGate>();
        if (!_shapeWorkTestGate->release)
            return 5;
        State state{*this, Mode::Success, {}, {}, revision, source};
        state.observeSelectedReceipt = true;
        if (ending == 0)
            pointer(WM_LBUTTONUP, 0, {40, 30});
        else if (ending == 1)
        {
            _releasingCapture = true;
            if (GetCapture() == _window.get())
                ReleaseCapture();
            _releasingCapture = false;
            OnPointer(WM_MOUSELEAVE, 0, 0);
        }
        else
        {
            // Invoke the real capture-loss message handler even when a hidden runner cannot own OS capture.
            (void)SendMessageW(_window.get(), WM_CAPTURECHANGED, 0, 0);
            if (GetCapture() == _window.get())
                ReleaseCapture();
        }
        expect(!_session.PendingShape(), "pointer-ends-live-draft", Mode::Success, false, ending);
        const bool commit = ending != 2;
        expect(commit ? _shapeCommitPreview && _shapeCommitPreview->rect == owned.rect && _hostRequests.Size() == 1
                      : !_shapeCommitPreview && !_hostRequests.Size() && !_shapeTicket,
               "pointer-owned-outline-or-loss-drop", Mode::Success, false, ending);
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        expect(commit ? state.visited && state.held && state.receipt.Read().outcome == HostOutcome::Succeeded
                      : !state.visited,
               "pointer-producer-worker-route", Mode::Success, false, ending);
        const auto* layer = _session.ActiveLayer();
        std::array<std::uint8_t, 4> pixel{};
        if (commit && layer && layer->asset)
            layer->asset->image.ReadRegion(15, 10, 1, 1, pixel.data(), 4, 0);
        expect(commit ? layer && layer->asset && _session.Document()->layers.size() == layers + 1 &&
                            pixel == std::array<std::uint8_t, 4>{0, 0, 255, 255} &&
                            layer->transform.origin == Vec2{owned.rect.minX, owned.rect.minY} &&
                            _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Rectangle"
                      : _session.Document()->layers.size() == layers && _session.history.CurrentRevision() == revision,
               "pointer-literal-publication-or-unchanged-loss", Mode::Success, false, ending);
        expect(!_shapeCommitPreview && !_shapeStamp && !_shapeTicket && !_historyWork.Busy() && !_activeHostRequest &&
                   !_hostRequests.Size() && !_hostInputBudget.Used() && !_jobs.ReservedMemoryBytes() &&
                   !_jobs.ReleasingMemoryBytes(),
               "pointer-final-ownership-released", Mode::Success, false, ending);
        _shapeWorkTestGate.reset();
    }
    std::printf("shape-host summary: %u checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : RunImageWrapperResidencySelfTest(2);
}

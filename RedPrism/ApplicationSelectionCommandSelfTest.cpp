#include "Application.h"

#include "ChromeHost.h"
#include "Selection.h"
#include "SelectionCommandTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
class OccupySelectionTestSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunSelectionCommandSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit([this, originalViewport] { _viewport = originalViewport; });
    enum class Mode
    {
        SelectAll,
        Deselect,
        NoOpRedo,
        NoOpDeselect,
        EmptyDocument,
        PreserveTargets,
        Nested,
        SavedDuringWorker,
        Ordered,
        ViewPump,
        Cancel,
        Failure,
        CaptureFailure,
        SelectionSnapshot,
        BeforeSnapshot,
        AfterSnapshot,
        Entry,
        StackGrowth,
        TrimIndex,
        Generation,
        Active,
        Mask,
        Crop,
        Epoch,
        Replacement,
        Lasso,
        ReopenedBrush,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger;
        std::optional<ContinuationReceipt> undo;
        std::optional<CanvasDocument> before;
        std::vector<Guid> layerIds;
        std::optional<Guid> active;
        std::optional<Guid> firstLayer;
        std::optional<CropRect> crop;
        Guid revision;
        Guid savedRevision;
        std::uint64_t documentSerial = 0;
        std::uint64_t generation = 0;
        int undoCount = 0;
        std::size_t redoCount = 0;
        bool visited = false, held = false, cancelPending = false, pumpSeen = false, pumpWasPending = false;
        bool mutationApplied = false, maskSelected = false, viewChangedWhilePending = false;
        double zoomBefore = 0;
        unsigned int pumpMessages = 0;
    };
    constexpr UINT kPumpMessage = WM_APP + 0x4c6;
    int passed = 0;
    const auto expect = [&passed](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL selection-host.%s.%s: %s\n", mode, check, detail);
            return false;
        }
        ++passed;
        std::fprintf(stderr, "PASS selection-host.%s.%s\n", mode, check);
        return true;
    };
    const auto prepare = [this]()
    {
        _session.ClearDocument();
        _session.CreateDocument(4, 3, true);
        _session.AddBlankLayer();
        _session.SetSelection(
            DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{1, 1}, Vec2{2, 1}, Vec2{2, 2}, Vec2{1, 2}}}},
            L"Fixture Selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionCommandTestGate::kMessage)
        {
            const auto gate = owner._selectionCommandTestGate;
            state.visited = true;
            state.held = owner._selectionCommandTicket.has_value() && owner._selectionCommandStamp.has_value() &&
                         owner._historyWork.Busy() &&
                         owner._jobs.ReservedMemoryBytes() >= owner._selectionCommandTestGate->reservationBytes &&
                         owner._session.Document() == state.before &&
                         owner._session.history.CurrentRevision() == state.revision &&
                         state.receipt.Read().outcome == HostOutcome::Pending &&
                         owner._selectionCommandTestGate->captures.load() == 1;
            if (state.mode == Mode::SavedDuringWorker)
                owner._session.history.MarkSaved(state.savedRevision);
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
                state.undo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                // Only the first job is gated. Its payload retains this gate until worker retirement.
                owner._selectionCommandTestGate.reset();
            }
            if (state.mode == Mode::ViewPump)
            {
                state.zoomBefore = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.viewChangedWhilePending = owner._viewport.zoom != state.zoomBefore &&
                                                state.receipt.Read().outcome == HostOutcome::Pending &&
                                                owner._historyWork.Busy();
                (void)PostMessageW(owner._window.get(), kPumpMessage, 0, 0);
                return;
            }
            if (state.mode == Mode::Cancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            }
            if (state.mode == Mode::Generation)
            {
                owner._session.EditForTest([](CanvasDocument& document) { document.resolution = 300; });
                state.mutationApplied = owner._session.Generation() != state.generation;
            }
            else if (state.mode == Mode::Active)
            {
                if (state.firstLayer)
                    owner._session.SelectLayer(state.firstLayer.value());
                state.mutationApplied = state.firstLayer != state.active &&
                                        owner._session.ActiveLayerID() == state.firstLayer &&
                                        owner._session.Generation() == state.generation;
            }
            else if (state.mode == Mode::Mask)
            {
                const auto id = owner._session.ActiveLayerID();
                if (id)
                    owner._session.SelectLayerTarget(id.value(), true);
                state.mutationApplied = !state.maskSelected && owner._session.IsMaskSelected() &&
                                        owner._session.Generation() == state.generation;
            }
            else if (state.mode == Mode::Crop)
            {
                owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
                state.mutationApplied =
                    owner._session.CropFrame() != state.crop && owner._session.Generation() == state.generation;
            }
            else if (state.mode == Mode::Epoch)
            {
                owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
                owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
                state.mutationApplied = owner._session.Generation() == state.generation &&
                                        owner._session.history.CurrentRevision() == state.revision;
            }
            else if (state.mode == Mode::Replacement)
            {
                owner._session.CreateDocument(4, 3, true);
                state.mutationApplied = owner._session.DocumentSerial() != state.documentSerial;
            }
            else if (state.mode == Mode::Lasso)
            {
                owner._session.BeginLasso({0, 0}, SelectionMode::New);
                state.mutationApplied = owner._session.PendingLasso().has_value();
            }
            else if (state.mode == Mode::ReopenedBrush)
                state.mutationApplied = owner._session.BeginBrush({1, 1}) && owner._session.PendingBrush();
            (void)SetEvent(gate->release.get());
            return;
        }
        if (message.hwnd && message.message == kPumpMessage)
        {
            ++state.pumpMessages;
            state.pumpSeen = true;
            state.pumpWasPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            (void)SetEvent(owner._selectionCommandTestGate->release.get());
        }
        owner.DispatchQueuedMessage(message);
    };
    const auto name = [](Mode mode) noexcept -> const char*
    {
        switch (mode)
        {
        case Mode::SelectAll:
            return "select-all";
        case Mode::Deselect:
            return "deselect";
        case Mode::NoOpRedo:
            return "noop-redo";
        case Mode::NoOpDeselect:
            return "noop-deselect";
        case Mode::EmptyDocument:
            return "empty-document";
        case Mode::PreserveTargets:
            return "preserve-targets";
        case Mode::Nested:
            return "nested";
        case Mode::SavedDuringWorker:
            return "saved-during-worker";
        case Mode::Ordered:
            return "ordered";
        case Mode::ViewPump:
            return "view-pump";
        case Mode::Cancel:
            return "cancel";
        case Mode::Failure:
            return "failure";
        case Mode::CaptureFailure:
            return "capture-failure";
        case Mode::SelectionSnapshot:
            return "stage-selection-snapshot";
        case Mode::BeforeSnapshot:
            return "stage-before-snapshot";
        case Mode::AfterSnapshot:
            return "stage-after-snapshot";
        case Mode::Entry:
            return "stage-entry";
        case Mode::StackGrowth:
            return "stage-stack-growth";
        case Mode::TrimIndex:
            return "stage-trim-index";
        case Mode::Generation:
            return "stale-generation";
        case Mode::Active:
            return "stale-active";
        case Mode::Mask:
            return "stale-mask";
        case Mode::Crop:
            return "stale-crop";
        case Mode::Epoch:
            return "stale-history-epoch";
        case Mode::Replacement:
            return "stale-replacement";
        case Mode::Lasso:
            return "stale-lasso";
        case Mode::ReopenedBrush:
            return "stale-reopened-brush";
        }
        return "unknown";
    };

    const auto run = [&](Mode mode) -> bool
    {
        const bool empty = mode == Mode::EmptyDocument;
        prepare();
        if (mode == Mode::Mask || mode == Mode::PreserveTargets)
        {
            _session.AddLayerMask(); // capture the mask before the stale-target mutation
            const auto id = _session.ActiveLayerID();
            if (id)
                _session.SelectLayerTarget(id.value(), mode == Mode::PreserveTargets);
            if (mode == Mode::Mask &&
                !expect(name(mode), "starts-on-image-with-mask",
                        _session.ActiveLayer() && _session.ActiveLayer()->mask && !_session.IsMaskSelected(),
                        "the fixture must start on image pixels before switching to its existing mask"))
                return false;
            if (mode == Mode::PreserveTargets)
            {
                _session.SetCropFrame(CropRect{0, 0, 2, 2});
            }
            _session.history.Reset();
            _session.history.MarkSaved();
            DocumentChanged();
        }
        if (mode == Mode::NoOpRedo)
        {
            _session.SelectAll();
            _session.Deselect();
            _session.Undo(); // current selection is full-canvas; Deselect remains redoable
        }
        if (mode == Mode::NoOpDeselect)
        {
            _session.Deselect();
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{1, 1}, Vec2{2, 1}, Vec2{2, 2}, Vec2{1, 2}}}},
                L"Redo Fixture");
            _session.Undo(); // null selection is current; the redo stack remains populated
        }
        if (mode == Mode::Nested)
            _session.history.Begin(L"Open fixture transaction", _session.Document(), _session.ActiveLayerID());
        if (empty)
        {
            _session.ClearDocument();
            DocumentChanged();
        }
        const auto document = _session.Document();
        const auto gate = std::make_shared<SelectionCommandTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release)
            return expect(name(mode), "gate-event", false, "CreateEventW failed");
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        if (mode == Mode::Failure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        switch (mode)
        {
        case Mode::SelectionSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::SelectionSnapshot;
            break;
        case Mode::BeforeSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::BeforeSnapshot;
            break;
        case Mode::AfterSnapshot:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::AfterSnapshot;
            break;
        case Mode::Entry:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::Entry;
            break;
        case Mode::StackGrowth:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::StackGrowth;
            break;
        case Mode::TrimIndex:
            gate->throwHistoryStage = DocumentHistory::PrepareStage::TrimIndex;
            break;
        default:
            break;
        }
        _selectionCommandTestGate = gate;
        State state{*this, mode,
                    QueueHostInput(HostCommandInput{mode == Mode::Deselect || mode == Mode::NoOpDeselect ||
                                                            mode == Mode::PreserveTargets
                                                        ? ChromeHost::kCommandDeselect
                                                        : ChromeHost::kCommandSelectAll})};
        state.before = document;
        state.active = _session.ActiveLayerID();
        if (document && !document->layers.empty())
            state.firstLayer = document->layers.front().id;
        state.crop = _session.CropFrame();
        state.maskSelected = _session.IsMaskSelected();
        state.revision = _session.history.CurrentRevision();
        state.savedRevision = state.revision;
        state.documentSerial = _session.DocumentSerial();
        state.generation = _session.Generation();
        state.undoCount = _session.history.UndoCount();
        state.redoCount = _session.history.CaptureState().FutureCount();
        if (document)
            for (const auto& layer : document->layers)
                state.layerIds.push_back(layer.id);
        _session.history.MarkSaved(state.savedRevision);
        if (mode == Mode::SavedDuringWorker)
            _session.history.MarkSaved(Guid::Generate()); // the held callback updates this distinct marker
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto result = state.receipt.Read();
        bool ok = true;
        ok &= expect(name(mode), "settled", settled == SettleResult::Settled,
                     "SettleJobs did not acknowledge all worker and queued host input");
        const bool fault = gate->throwHistoryStage.has_value();
        const bool captureFault = mode == Mode::CaptureFailure;
        const bool expectedWorker = !empty && !captureFault && !fault;
        ok &= expect(name(mode), "worker-reached", state.visited == expectedWorker,
                     "held worker message count did not match the scenario");
        if (expectedWorker)
        {
            ok &= expect(name(mode), "held-before-ack", state.held,
                         "worker did not retain its reservation and pending receipt at the owner gate");
            ok &= expect(
                name(mode), "capture-retirement",
                gate->reservationHeld.load() && gate->captures.load() == 1 && gate->retired.load() &&
                    gate->retiredOffOwner.load(),
                "owner capture was not performed once under reservation or payload retirement was not off-owner");
        }
        ok &= expect(name(mode), "reservation-released",
                     _jobs.ReservedMemoryBytes() == 0 && !_selectionCommandTicket && !_selectionCommandStamp &&
                         !_historyWork.Busy(),
                     "selection work retained its ticket, history stamp, or scheduler reservation");

        if (mode == Mode::SelectAll || mode == Mode::Deselect || mode == Mode::PreserveTargets)
        {
            const auto expected =
                mode == Mode::SelectAll
                    ? std::optional<DocumentSelection>(DocumentSelection{
                          true, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 3}, Vec2{0, 3}}}})
                    : std::optional<DocumentSelection>{};
            ok &=
                expect(name(mode), "receipt", result.outcome == HostOutcome::Succeeded, "host command did not succeed");
            ok &= expect(name(mode), "selection", _session.Selection() == expected,
                         "selection does not equal the literal full-canvas clockwise outline or nullopt");
            const bool layersMatch = document && _session.Document() && document->layers == _session.Document()->layers;
            ok &= expect(name(mode), "layers-and-target", layersMatch && _session.ActiveLayerID() == state.active,
                         "command changed the layer stack or active target");
            if (mode == Mode::PreserveTargets)
                ok &= expect(name(mode), "mask-and-crop-preserved",
                             _session.IsMaskSelected() == state.maskSelected && state.maskSelected &&
                                 _session.CropFrame() == state.crop,
                             "selection command changed the mask target or crop frame");
            ok &= expect(name(mode), "history-title",
                         _session.history.UndoName() == (mode == Mode::SelectAll ? L"Select All" : L"Deselect"),
                         "command history title is not exact");
        }
        else if (mode == Mode::NoOpRedo)
        {
            ok &= expect(name(mode), "receipt-and-selection",
                         result.outcome == HostOutcome::Succeeded && _session.Selection() == state.before->selection,
                         "repeat Select All did not acknowledge as a no-op");
            ok &=
                expect(name(mode), "redo-revision-generation",
                       _session.history.CanRedo() && _session.history.CaptureState().FutureCount() == state.redoCount &&
                           _session.history.CurrentRevision() == state.revision && state.redoCount > 0 &&
                           _session.Generation() == state.generation,
                       "no-op discarded redo or changed revision/generation");
        }
        else if (mode == Mode::NoOpDeselect)
        {
            ok &= expect(name(mode), "receipt-and-selection",
                         result.outcome == HostOutcome::Succeeded && !_session.Selection(),
                         "repeat Deselect did not acknowledge as a no-op");
            ok &= expect(name(mode), "redo-revision-generation",
                         _session.history.CanRedo() && state.redoCount > 0 &&
                             _session.history.CaptureState().FutureCount() == state.redoCount &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.Generation() == state.generation,
                         "no-op discarded redo or changed revision/generation");
        }
        else if (mode == Mode::SavedDuringWorker)
        {
            ok &= expect(name(mode), "saved-revision",
                         result.outcome == HostOutcome::Succeeded && _session.history.IsModified(),
                         "publication lost the saved-during-worker revision");
            _session.Undo();
            ok &= expect(name(mode), "undo-to-updated-saved-marker",
                         _session.history.CurrentRevision() == state.savedRevision && !_session.history.IsModified(),
                         "Undo to the revision saved during preparation did not return to the unmodified state");
        }
        else if (mode == Mode::Ordered)
            ok &= expect(
                name(mode), "younger-opposite-and-undo",
                result.outcome == HostOutcome::Succeeded && state.younger && state.undo &&
                    state.younger->Read().outcome == HostOutcome::Succeeded &&
                    state.undo->Read().outcome == HostOutcome::Succeeded &&
                    _session.Selection() ==
                        std::optional<DocumentSelection>(DocumentSelection{
                            true, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 3}, Vec2{0, 3}}}}) &&
                    _session.history.UndoName() == L"Select All",
                "younger Deselect and Undo did not execute in ordinal order with settled receipts");
        else if (mode == Mode::ViewPump)
            ok &= expect(
                name(mode), "view-message-pumped",
                result.outcome == HostOutcome::Succeeded && state.viewChangedWhilePending && state.pumpSeen &&
                    state.pumpMessages == 1 && state.pumpWasPending,
                "direct zoom did not bypass held history or posted window message was not pumped before release");
        else if (mode == Mode::Cancel)
            ok &=
                expect(name(mode), "cancel-acknowledgement",
                       state.cancelPending && result.outcome == HostOutcome::Cancelled &&
                           _session.Document() == state.before && _session.history.CurrentRevision() == state.revision,
                       "cancellation did not retain the input until acknowledgement or changed live state");
        else if (mode == Mode::Failure)
            ok &=
                expect(name(mode), "worker-failure",
                       result.outcome == HostOutcome::Failed && result.error == E_ACCESSDENIED &&
                           _session.Document() == state.before && _session.history.CurrentRevision() == state.revision,
                       "worker failure published a change or lost its exact error");
        else if (captureFault)
            ok &= expect(name(mode), "capture-bad-alloc",
                         result.outcome == HostOutcome::Failed && result.error == E_OUTOFMEMORY &&
                             gate->captures == 0 && _session.Document() == state.before,
                         "owner capture allocation fault was not reported without publishing");
        else if (fault)
            ok &= expect(name(mode), "history-prepare-fault",
                         result.outcome == HostOutcome::Failed && result.error == E_OUTOFMEMORY &&
                             gate->historyFaultHits == 1 && gate->captures == 1 &&
                             _session.Selection() == state.before->selection,
                         "history preparation fault was not reached once or published selection");
        else if (empty)
            ok &=
                expect(name(mode), "no-worker-effect",
                       !state.visited && result.outcome == HostOutcome::Succeeded &&
                           _session.Document() == state.before && _session.history.CurrentRevision() == state.revision,
                       "empty document unexpectedly reached worker publication");
        else if (mode == Mode::Nested)
        {
            const bool nestedDepthRetained = _session.history.CaptureState().Depth() == 1;
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            ok &= expect(
                name(mode), "outer-transaction-title",
                nestedDepthRetained && result.outcome == HostOutcome::Succeeded &&
                    _session.Selection() ==
                        std::optional<DocumentSelection>(DocumentSelection{
                            true, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 3}, Vec2{0, 3}}}}) &&
                    _session.history.UndoName() == L"Open fixture transaction" &&
                    _session.history.UndoCount() == state.undoCount + 1,
                "nested selection did not join the outer transaction with its exact title");
        }
        else
        {
            const bool stale = mode >= Mode::Generation && mode <= Mode::ReopenedBrush;
            ok &= expect(name(mode), "stale-authority",
                         stale && state.mutationApplied && result.outcome == HostOutcome::Stale &&
                             (mode == Mode::Replacement || _session.Selection() == state.before->selection),
                         "changed live authority was incorrectly published or not reported stale");
        }
        if (mode == Mode::Lasso)
            _session.CancelLasso();
        if (mode == Mode::ReopenedBrush)
            _session.CancelBrush();
        _selectionCommandTestGate.reset();
        return ok;
    };

    constexpr std::array modes{Mode::SelectAll,      Mode::Deselect,
                               Mode::NoOpRedo,       Mode::NoOpDeselect,
                               Mode::EmptyDocument,  Mode::PreserveTargets,
                               Mode::Nested,         Mode::SavedDuringWorker,
                               Mode::Ordered,        Mode::ViewPump,
                               Mode::Cancel,         Mode::Failure,
                               Mode::CaptureFailure, Mode::SelectionSnapshot,
                               Mode::BeforeSnapshot, Mode::AfterSnapshot,
                               Mode::Entry,          Mode::StackGrowth,
                               Mode::TrimIndex,      Mode::Generation,
                               Mode::Active,         Mode::Mask,
                               Mode::Crop,           Mode::Epoch,
                               Mode::Replacement,    Mode::Lasso,
                               Mode::ReopenedBrush};
    for (const auto mode : modes)
    {
        if (!run(mode))
            return 5;
    }
    // Completed jobs still occupy records until the owner drains them. Filling the actual record table
    // therefore reaches submission refusal deterministically without holding workers or using a fake HRESULT.
    prepare();
    const auto beforeAdmission = _session.Document();
    const auto admissionRevision = _session.history.CurrentRevision();
    for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupySelectionTestSlot>();
        JobTicket ticket;
        if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
            return 5;
    }
    const auto refused = QueueHostInput(HostCommandInput{ChromeHost::kCommandSelectAll});
    for (unsigned int pass = 0; pass < 4 && refused.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    const bool admissionRefused =
        expect("admission", "full-record-table-refuses",
               refused.Read().outcome == HostOutcome::Failed &&
                   refused.Read().error == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) && !_historyWork.Busy() &&
                   !_selectionCommandTicket && !_selectionCommandStamp && _session.Document() == beforeAdmission &&
                   _session.history.CurrentRevision() == admissionRevision,
               "real scheduler refusal lost history/input acknowledgement or changed state");
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || !admissionRefused)
        return 5;
    const auto recovery = QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
    if (!expect("admission", "fresh-request-recovers",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                    recovery.Read().outcome == HostOutcome::Succeeded && !_session.Selection() &&
                    !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0,
                "a fresh request failed after actual submission refusal"))
        return 5;
    std::fprintf(stderr, "PASS selection-host.summary.count checks=%d modes=%zu\n", passed, modes.size() + 1);
    return 0;
}

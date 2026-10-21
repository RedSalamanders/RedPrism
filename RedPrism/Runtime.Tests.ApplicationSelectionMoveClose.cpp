#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.SelectionMoveCloseTestGate.h"

#include <array>
#include <cstdio>
#include <memory>

namespace
{
class OccupySelectionMoveCloseSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunSelectionMoveCloseSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit([this, originalViewport] { _viewport = originalViewport; });
    enum class Mode
    {
        End,
        CancelOriginal,
        NoOpEnd,
        PendingCancel,
        WorkerFailure,
        CaptureFailure,
        OpeningSnapshotRetained,
        AfterSnapshot,
        Entry,
        StackGrowth,
        TrimIndex,
        StaleOpening,
        Generation,
        ActiveLayer,
        Replacement,
        SavedDuringWorker,
        Nested,
        Ordered,
        ViewPump,
        QueueProducer,
        PointerRelease,
        EscapeProducer,
        UndoProducer,
        RedoProducer,
        Crop,
        Mask,
        Epoch,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        std::optional<ContinuationReceipt> receipt;
        std::optional<ContinuationReceipt> youngerDeselect;
        std::optional<ContinuationReceipt> youngerUndo;
        std::optional<CanvasDocument> liveBefore;
        std::optional<DocumentSelection> origin;
        std::optional<Guid> activeBefore;
        std::optional<Guid> otherLayer;
        Guid revision;
        Guid savedRevision;
        std::uint64_t generation = 0;
        std::uint64_t documentSerial = 0;
        int undoCount = 0;
        std::size_t redoCount = 0;
        double zoomBefore = 0;
        unsigned int pumpMessages = 0;
        bool visited = false;
        bool held = false;
        bool cancelPending = false;
        bool mutationApplied = false;
        bool pumpSeen = false;
        bool pumpWasPending = false;
        bool pointerReleased = false;
        bool latePointerIgnored = false;
        bool keyProducerAccepted = false;
        bool duplicateProducerReused = false;
        std::optional<CropRect> cropAfterMutation;
        bool maskAfterMutation = false;
        bool queuedProducerDroppedDrag = false;
        bool movingAfterMutation = false;
        std::optional<CanvasDocument> documentAfterMutation;
        std::optional<DocumentSelection> selectionAfterMutation;
        std::optional<Guid> activeAfterMutation;
        std::optional<std::uint64_t> openingAfterMutation;
        std::uint64_t generationAfterMutation = 0;
        std::uint64_t documentSerialAfterMutation = 0;
        Guid revisionAfterMutation;
        int undoCountAfterMutation = 0;
        std::size_t redoCountAfterMutation = 0;
        DocumentHistory::StateView historyAfterMutation;
    };
    constexpr UINT kPumpMessage = WM_APP + 0x4ca;
    int passed = 0;
    int failed = 0;
    int modesRun = 0;
    const auto expect =
        [&passed, &failed](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        if (!condition)
        {
            ++failed;
            std::fprintf(stderr, "FAIL selection-move-host.%s.%s: %s\n", mode, check, detail);
            return false;
        }
        ++passed;
        std::fprintf(stderr, "PASS selection-move-host.%s.%s\n", mode, check);
        return true;
    };
    const auto rect = [](double left, double top, double right, double bottom)
    {
        return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{right, top},
                                                                     Vec2{right, bottom}, Vec2{left, bottom}}}};
    };
    const auto prepare = [this](bool twoLayers)
    {
        _drag = PointerDrag{};
        _session.ClearDocument();
        _session.CreateDocument(4, 4, true);
        _session.AddBlankLayer();
        std::optional<Guid> other;
        if (twoLayers)
        {
            _session.AddBlankLayer();
            other = _session.ActiveLayerID();
            const auto& document = _session.Document();
            if (document && !document->layers.empty())
                _session.SelectLayer(document->layers.front().id);
        }
        _session.SetSelection(
            DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{1, 1}, Vec2{2, 1}, Vec2{2, 2}, Vec2{1, 2}}}},
            L"Fixture Selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
        return other;
    };
    const auto name = [](Mode mode) noexcept -> const char*
    {
        switch (mode)
        {
        case Mode::End:
            return "end";
        case Mode::CancelOriginal:
            return "cancel-original";
        case Mode::NoOpEnd:
            return "noop-end";
        case Mode::PendingCancel:
            return "pending-cancel";
        case Mode::WorkerFailure:
            return "worker-failure";
        case Mode::CaptureFailure:
            return "capture-bad-alloc";
        case Mode::OpeningSnapshotRetained:
            return "opening-snapshot-retained";
        case Mode::AfterSnapshot:
            return "history-after-snapshot";
        case Mode::Entry:
            return "history-entry";
        case Mode::StackGrowth:
            return "history-stack-growth";
        case Mode::TrimIndex:
            return "history-trim-index";
        case Mode::StaleOpening:
            return "stale-equal-geometry-opening";
        case Mode::Generation:
            return "stale-generation";
        case Mode::ActiveLayer:
            return "stale-active-layer";
        case Mode::Replacement:
            return "stale-replacement";
        case Mode::SavedDuringWorker:
            return "saved-during-worker";
        case Mode::Nested:
            return "nested-outer-history";
        case Mode::Ordered:
            return "ordered-deselect-undo";
        case Mode::ViewPump:
            return "view-pump";
        case Mode::QueueProducer:
            return "queue-producer";
        case Mode::PointerRelease:
            return "pointer-release";
        case Mode::EscapeProducer:
            return "escape-producer";
        case Mode::UndoProducer:
            return "undo-producer";
        case Mode::RedoProducer:
            return "redo-producer";
        case Mode::Crop:
            return "stale-crop";
        case Mode::Mask:
            return "stale-mask";
        case Mode::Epoch:
            return "stale-history-epoch";
        }
        return "unknown";
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionMoveCloseTestGate::kMessage)
        {
            const auto gate = owner._selectionMoveCloseTestGate;
            state.visited = true;
            if (!state.receipt && owner._activeHostRequest)
                state.receipt = owner._activeHostRequest->input.Receipt();
            state.held = owner._selectionMoveCloseTicket.has_value() && owner._selectionMoveCloseStamp.has_value() &&
                         owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                         state.receipt && state.receipt->Read().outcome == HostOutcome::Pending &&
                         owner._session.Document() == state.liveBefore &&
                         owner._session.Selection() == state.liveBefore->selection &&
                         owner._session.history.CurrentRevision() == state.revision;
            const auto captureMutation = [&state, &owner]() noexcept
            {
                state.documentAfterMutation = owner._session.Document();
                state.selectionAfterMutation = owner._session.Selection();
                state.activeAfterMutation = owner._session.ActiveLayerID();
                state.openingAfterMutation = owner._session.SelectionMoveOpening();
                state.movingAfterMutation = owner._session.IsMovingSelection();
                state.generationAfterMutation = owner._session.Generation();
                state.documentSerialAfterMutation = owner._session.DocumentSerial();
                state.revisionAfterMutation = owner._session.history.CurrentRevision();
                state.undoCountAfterMutation = owner._session.history.UndoCount();
                state.redoCountAfterMutation = owner._session.history.CaptureState().FutureCount();
                state.historyAfterMutation = owner._session.history.CaptureState();
                state.cropAfterMutation = owner._session.CropFrame();
                state.maskAfterMutation = owner._session.IsMaskSelected();
            };
            if (state.mode == Mode::PointerRelease || state.mode == Mode::QueueProducer ||
                state.mode == Mode::EscapeProducer || state.mode == Mode::UndoProducer ||
                state.mode == Mode::RedoProducer)
            {
                (void)owner.SelectionPointerMove(owner._viewport.ViewFromDocument(Vec2{3, 3}, owner.DocumentSize()),
                                                 MK_LBUTTON);
                state.latePointerIgnored = std::holds_alternative<std::monostate>(owner._drag) &&
                                           owner._session.Document() == state.liveBefore &&
                                           owner._session.Generation() == state.generation;
            }
            if (state.mode == Mode::PendingCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt && state.receipt->Read().outcome == HostOutcome::Pending &&
                                      owner._historyWork.Busy() && owner._session.IsMovingSelection() &&
                                      owner._session.Selection() == state.liveBefore->selection;
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::Generation)
            {
                owner._session.EditForTest([](CanvasDocument& document) { document.resolution = 300; });
                state.mutationApplied = owner._session.Generation() != state.generation;
                captureMutation();
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::ActiveLayer)
            {
                if (state.otherLayer)
                    owner._session.SelectLayer(state.otherLayer.value());
                state.mutationApplied = state.otherLayer != state.activeBefore &&
                                        owner._session.ActiveLayerID() == state.otherLayer &&
                                        owner._session.Generation() == state.generation;
                captureMutation();
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::Replacement)
            {
                owner._session.CreateDocument(4, 4, true);
                state.mutationApplied = owner._session.DocumentSerial() != state.documentSerial;
                captureMutation();
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::StaleOpening)
            {
                const auto oldOpening = owner._session.SelectionMoveOpening();
                owner._session.CancelSelectionMove();
                const bool began = owner._session.BeginSelectionMove();
                if (began)
                    owner._session.MoveSelection(1, 1);
                state.mutationApplied = oldOpening && began && owner._session.SelectionMoveOpening() != oldOpening &&
                                        owner._session.Selection() == state.liveBefore->selection;
                captureMutation();
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::Crop || state.mode == Mode::Mask || state.mode == Mode::Epoch)
            {
                const auto beforeHistory = owner._session.history.CaptureState();
                if (state.mode == Mode::Crop)
                    owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
                else if (state.mode == Mode::Mask && owner._session.ActiveLayerID())
                    owner._session.SelectLayerTarget(owner._session.ActiveLayerID().value(), true);
                else if (state.mode == Mode::Epoch)
                {
                    owner._session.history.Begin(L"Epoch only", owner._session.Document(),
                                                 owner._session.ActiveLayerID());
                    owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
                }
                state.mutationApplied = owner._session.Generation() == state.generation &&
                                        (state.mode == Mode::Crop   ? owner._session.CropFrame().has_value()
                                         : state.mode == Mode::Mask ? owner._session.IsMaskSelected()
                                                                    : !owner._session.history.IsCurrent(beforeHistory));
                captureMutation();
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::SavedDuringWorker)
            {
                state.savedRevision = owner._session.history.CurrentRevision();
                owner._session.history.MarkSaved(state.savedRevision);
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::Nested)
            {
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::Ordered)
            {
                state.youngerDeselect = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
                state.youngerUndo = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                (void)SetEvent(gate->release.get());
            }
            else if (state.mode == Mode::ViewPump)
            {
                state.zoomBefore = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                const bool zoomChanged = owner._viewport.zoom != state.zoomBefore && state.receipt &&
                                         state.receipt->Read().outcome == HostOutcome::Pending &&
                                         owner._historyWork.Busy();
                if (!PostMessageW(owner._window.get(), kPumpMessage, 0, 0))
                    (void)SetEvent(gate->release.get());
                state.mutationApplied = zoomChanged;
            }
            else
            {
                (void)SetEvent(gate->release.get());
            }
            return;
        }
        if (message.hwnd && message.message == kPumpMessage)
        {
            ++state.pumpMessages;
            state.pumpSeen = true;
            state.pumpWasPending =
                state.receipt && state.receipt->Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            (void)SetEvent(owner._selectionMoveCloseTestGate->release.get());
        }
        owner.DispatchQueuedMessage(message);
    };

    const auto run = [&](Mode mode) -> bool
    {
        ++modesRun;
        const bool nested = mode == Mode::Nested;
        const bool activeScenario = mode == Mode::ActiveLayer;
        const auto otherLayer = prepare(activeScenario);
        if (mode == Mode::Mask)
        {
            _session.AddLayerMask();
            if (_session.ActiveLayerID())
                _session.SelectLayerTarget(_session.ActiveLayerID().value(), false);
            _session.history.Reset();
            _session.history.MarkSaved();
        }
        const bool cancelProducer =
            mode == Mode::EscapeProducer || mode == Mode::UndoProducer || mode == Mode::RedoProducer;
        if (mode == Mode::NoOpEnd || mode == Mode::CancelOriginal || cancelProducer)
        {
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}},
                L"Seed redo selection");
            _session.Undo();
        }
        if (nested)
            _session.history.Begin(L"Outer selection transaction", _session.Document(), _session.ActiveLayerID());
        const auto origin = _session.Selection();
        if (!_session.BeginSelectionMove())
            return expect(name(mode), "move-opened", false, "fixture did not open its outline move");
        if (mode != Mode::NoOpEnd)
            _session.MoveSelection(1, 1);
        const auto opening = _session.SelectionMoveOpening();
        if (!opening)
            return expect(name(mode), "opening", false, "successful fixture move has no opening");
        const auto gate = std::make_shared<SelectionMoveCloseTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release)
            return expect(name(mode), "gate-event", false, "CreateEventW failed");
        gate->throwCaptureFailure = mode == Mode::CaptureFailure;
        if (mode == Mode::WorkerFailure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        switch (mode)
        {
        case Mode::OpeningSnapshotRetained:
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
        _selectionMoveCloseTestGate = gate;
        State state{*this, mode};
        state.origin = origin;
        state.liveBefore = _session.Document();
        state.activeBefore = _session.ActiveLayerID();
        state.otherLayer = otherLayer;
        state.revision = _session.history.CurrentRevision();
        state.savedRevision = state.revision;
        state.generation = _session.Generation();
        state.documentSerial = _session.DocumentSerial();
        state.undoCount = _session.history.UndoCount();
        state.redoCount = _session.history.CaptureState().FutureCount();
        if (mode == Mode::PointerRelease)
        {
            _drag = OutlineDrag{Vec2{1, 1}, Vec2{1, 1}};
            SelectionRelease(Vec2{2, 2});
            state.pointerReleased = std::holds_alternative<std::monostate>(_drag);
        }
        else if (mode == Mode::QueueProducer)
        {
            _drag = OutlineDrag{Vec2{1, 1}, Vec2{1, 1}};
            state.receipt = QueueSelectionMoveClose(false);
            state.queuedProducerDroppedDrag = std::holds_alternative<std::monostate>(_drag);
            const auto queued = _hostRequests.Size();
            const auto duplicate = QueueSelectionMoveClose(false);
            state.duplicateProducerReused = queued == 1 && _hostRequests.Size() == queued &&
                                            duplicate.Read().outcome == HostOutcome::Pending &&
                                            state.receipt->Read().outcome == HostOutcome::Pending;
        }
        else if (cancelProducer)
        {
            _drag = OutlineDrag{Vec2{1, 1}, Vec2{1, 1}};
            if (mode == Mode::EscapeProducer)
                state.keyProducerAccepted = EndCanvasEdit(CanvasEdit::OutlineMove, EditEnd::Escape);
            else
            {
                OnCommand(mode == Mode::UndoProducer ? ChromeHost::kCommandUndo : ChromeHost::kCommandRedo);
                state.keyProducerAccepted = _hostRequests.Size() == 1;
            }
            state.receipt = _pendingSelectionMoveCloseReceipt;
        }
        else
        {
            state.receipt = QueueHostInput(HostSelectionMoveCloseInput{opening.value(), mode == Mode::CancelOriginal});
        }
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto receipt = state.receipt ? state.receipt->Read() : ContinuationReceipt::Result{};
        bool ok = true;
        ok &= expect(name(mode), "settled", settled == SettleResult::Settled,
                     "SettleJobs did not acknowledge the worker and owned host request within the failure timeout");
        ok &= expect(name(mode), "receipt-captured", state.receipt.has_value(),
                     "the actual owned host receipt was not observed");
        const bool stageFault = gate->throwHistoryStage.has_value() && mode != Mode::OpeningSnapshotRetained;
        const bool captureFault = mode == Mode::CaptureFailure;
        const bool expectedWorker = mode != Mode::CaptureFailure && !stageFault;
        ok &= expect(name(mode), "worker-gate-visits", gate->visits.load() == (expectedWorker ? 1u : 0u),
                     "worker gate visit count did not match capture/preparation outcome");
        if (expectedWorker)
        {
            ok &= expect(name(mode), "held-before-ack", state.visited && state.held,
                         "worker did not retain the real receipt, history work and reservation while held");
            ok &= expect(name(mode), "reservation-before-capture",
                         gate->reservationHeld.load() && gate->captures.load() == 1,
                         "owner capture did not occur once under its complete scheduler reservation");
            // This is the private prepared-payload aggregate probe, not proof for each raster allocation.
            ok &=
                expect(name(mode), "aggregate-retired-off-owner", gate->retired.load() && gate->retiredOffOwner.load(),
                       "prepared payload aggregate was not observed retiring away from the owner thread");
            // Gate counts guard this single path; they neither enumerate nor prove the last reference to each Raster.
        }
        if (mode == Mode::PointerRelease || mode == Mode::QueueProducer || cancelProducer)
            ok &= expect(name(mode), "late-pointer-frozen", state.latePointerIgnored,
                         "later pointer samples mutated the retained outline before closure acknowledgement");
        ok &= expect(name(mode), "released-work",
                     !_selectionMoveCloseTicket && !_selectionMoveCloseStamp && !_historyWork.Busy() &&
                         _jobs.ReservedMemoryBytes() == 0,
                     "selection-move closure retained its ticket, stamp, history work or scheduler reservation");

        if (mode == Mode::End || mode == Mode::QueueProducer || mode == Mode::PointerRelease ||
            mode == Mode::OpeningSnapshotRetained)
        {
            ok &= expect(name(mode), "receipt-and-geometry",
                         receipt.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                             _session.Selection() == std::optional(rect(2, 2, 3, 3)),
                         "End did not publish the exact moved rectangle and close the opening");
            ok &= expect(name(mode), "named-history",
                         _session.history.UndoName() == L"Move Selection" &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.CurrentRevision() != state.revision,
                         "End did not publish one named Move Selection history entry");
            ok &= expect(name(mode), "end-generation", _session.Generation() == state.generation + 1,
                         "End did not acknowledge exactly one generation before Undo/Redo");
            if (mode == Mode::OpeningSnapshotRetained)
                ok &= expect(name(mode), "opening-snapshot-not-recopied", gate->historyFaultHits.load() == 0,
                             "End recopied the immutable snapshot already owned by Begin");
            _session.Undo();
            // Literal Undo/Redo results are independent of the prepared publication internals.
            ok &=
                expect(name(mode), "undo-original", _session.Selection() == state.origin && _session.history.CanRedo(),
                       "Undo did not restore the original literal rectangle and make the move redoable");
            _session.Redo();
            ok &= expect(name(mode), "redo-moved", _session.Selection() == std::optional(rect(2, 2, 3, 3)),
                         "Redo did not restore the exact moved rectangle");
            if (mode == Mode::PointerRelease)
                ok &= expect(name(mode), "release-producer",
                             state.pointerReleased && _session.history.UndoName() == L"Move Selection",
                             "SelectionRelease did not drop OutlineDrag and route the moved outline to closure");
            if (mode == Mode::QueueProducer)
                ok &= expect(name(mode), "queue-producer-closure-only",
                             state.queuedProducerDroppedDrag && _session.history.UndoName() == L"Move Selection",
                             "QueueSelectionMoveClose did not freeze OutlineDrag and publish closure only");
            if (mode == Mode::QueueProducer)
                ok &= expect(name(mode), "duplicate-producer-reuses-pending-input", state.duplicateProducerReused,
                             "repeat closure admitted a second input for the same opening/intent");
        }
        else if (mode == Mode::CancelOriginal || cancelProducer)
        {
            ok &= expect(name(mode), "cancel-original-and-redo",
                         receipt.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                             _session.Selection() == state.origin && _session.history.UndoCount() == state.undoCount &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.history.CaptureState().FutureCount() == state.redoCount,
                         "Cancel did not restore the origin and preserve history revision and redo");
            ok &= expect(name(mode), "cancel-generation", _session.Generation() == state.generation + 2,
                         "Cancel did not acknowledge exactly the restore and closing generations");
            if (cancelProducer)
                ok &= expect(name(mode), "producer-consumes-first-cancel", state.keyProducerAccepted,
                             "Escape/first Undo/Redo failed to own cancellation of the live outline move");
        }
        else if (mode == Mode::NoOpEnd)
        {
            ok &= expect(
                name(mode), "noop-redo-preserved",
                receipt.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                    _session.Selection() == state.origin && _session.history.CurrentRevision() == state.revision &&
                    _session.history.UndoCount() == state.undoCount &&
                    _session.history.CaptureState().FutureCount() == state.redoCount && _session.history.CanRedo(),
                "unchanged End changed document/history or discarded the existing redo entry");
            ok &= expect(name(mode), "noop-generation", _session.Generation() == state.generation + 1,
                         "unchanged End did not acknowledge exactly one closing generation");
        }
        else if (mode == Mode::PendingCancel)
        {
            ok &= expect(name(mode), "cancel-awaits-worker",
                         state.cancelPending && receipt.outcome == HostOutcome::Cancelled &&
                             _session.IsMovingSelection() && _session.Selection() == state.liveBefore->selection &&
                             _session.history.CurrentRevision() == state.revision,
                         "pending cancellation did not remain pending until acknowledgement or changed the live draft");
        }
        else if (mode == Mode::WorkerFailure)
        {
            ok &= expect(name(mode), "worker-failure-preserves-live-state",
                         receipt.outcome == HostOutcome::Failed && receipt.error == E_ACCESSDENIED &&
                             _session.Document() == state.liveBefore && _session.IsMovingSelection() &&
                             _session.history.CurrentRevision() == state.revision,
                         "worker failure changed the moved draft/document/history or lost its exact error");
        }
        else if (captureFault)
        {
            ok &= expect(name(mode), "capture-bad-alloc-preserves-live-state",
                         receipt.outcome == HostOutcome::Failed && receipt.error == E_OUTOFMEMORY &&
                             gate->captures.load() == 0 && _session.Document() == state.liveBefore &&
                             _session.IsMovingSelection() && _session.history.CurrentRevision() == state.revision,
                         "capture allocation failure published or damaged the original move/history");
        }
        else if (stageFault)
        {
            ok &= expect(name(mode), "history-bad-alloc-preserves-live-state",
                         receipt.outcome == HostOutcome::Failed && receipt.error == E_OUTOFMEMORY &&
                             gate->historyFaultHits.load() == 1 && gate->captures.load() == 1 &&
                             _session.Document() == state.liveBefore && _session.IsMovingSelection() &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.history.CaptureState().FutureCount() == state.redoCount,
                         "history preparation allocation failure changed the live move, revision or redo");
        }
        else if (mode == Mode::StaleOpening || mode == Mode::Generation || mode == Mode::ActiveLayer ||
                 mode == Mode::Replacement || mode == Mode::Crop || mode == Mode::Mask || mode == Mode::Epoch)
        {
            const bool authorityChanged = state.mutationApplied;
            const bool liveMutationRetained =
                _session.Document() == state.documentAfterMutation &&
                _session.Selection() == state.selectionAfterMutation &&
                _session.ActiveLayerID() == state.activeAfterMutation &&
                _session.SelectionMoveOpening() == state.openingAfterMutation &&
                _session.IsMovingSelection() == state.movingAfterMutation &&
                _session.Generation() == state.generationAfterMutation &&
                _session.DocumentSerial() == state.documentSerialAfterMutation &&
                _session.history.CurrentRevision() == state.revisionAfterMutation &&
                _session.history.UndoCount() == state.undoCountAfterMutation &&
                _session.history.CaptureState().FutureCount() == state.redoCountAfterMutation &&
                _session.history.IsCurrent(state.historyAfterMutation) &&
                _session.CropFrame() == state.cropAfterMutation && _session.IsMaskSelected() == state.maskAfterMutation;
            ok &= expect(name(mode), "stale-does-not-publish",
                         authorityChanged && receipt.outcome == HostOutcome::Stale && liveMutationRetained,
                         "stale event authority published the prepared closure or lost the replacement draft");
        }
        else if (mode == Mode::SavedDuringWorker)
        {
            ok &= expect(name(mode), "saved-marker-lineage",
                         receipt.outcome == HostOutcome::Succeeded && _session.history.IsModified(),
                         "closure overwrote a saved marker recorded while the worker was held");
            _session.Undo();
            ok &= expect(name(mode), "undo-to-saved-marker",
                         _session.history.CurrentRevision() == state.savedRevision && !_session.history.IsModified(),
                         "Undo to the worker-time saved revision did not restore the clean marker");
        }
        else if (nested)
        {
            const bool depthRetained = _session.history.CaptureState().Depth() == 1;
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            ok &= expect(name(mode), "outer-transaction-kept",
                         receipt.outcome == HostOutcome::Succeeded && depthRetained &&
                             _session.history.UndoName() == L"Outer selection transaction" &&
                             _session.history.UndoCount() == state.undoCount + 1,
                         "closure did not join the named outer history transaction");
        }
        else if (mode == Mode::Ordered)
        {
            ok &= expect(name(mode), "younger-deselect-undo",
                         receipt.outcome == HostOutcome::Succeeded && state.youngerDeselect && state.youngerUndo &&
                             state.youngerDeselect->Read().outcome == HostOutcome::Succeeded &&
                             state.youngerUndo->Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                             _session.history.UndoName() == L"Move Selection",
                         "younger Deselect and Undo did not publish in ordinal order after closure");
        }
        else if (mode == Mode::ViewPump)
        {
            ok &= expect(name(mode), "actual-window-message-and-zoom",
                         receipt.outcome == HostOutcome::Succeeded && state.mutationApplied && state.pumpSeen &&
                             state.pumpMessages == 1 && state.pumpWasPending,
                         "actual posted window message or zoom command did not proceed while closure was held");
        }
        if (_session.IsMovingSelection())
            _session.CancelSelectionMove();
        _selectionMoveCloseTestGate.reset();
        return ok;
    };

    constexpr std::array modes{Mode::End,
                               Mode::CancelOriginal,
                               Mode::NoOpEnd,
                               Mode::PendingCancel,
                               Mode::WorkerFailure,
                               Mode::CaptureFailure,
                               Mode::OpeningSnapshotRetained,
                               Mode::AfterSnapshot,
                               Mode::Entry,
                               Mode::StackGrowth,
                               Mode::TrimIndex,
                               Mode::StaleOpening,
                               Mode::Generation,
                               Mode::ActiveLayer,
                               Mode::Replacement,
                               Mode::SavedDuringWorker,
                               Mode::Nested,
                               Mode::Ordered,
                               Mode::ViewPump,
                               Mode::QueueProducer,
                               Mode::PointerRelease,
                               Mode::EscapeProducer,
                               Mode::UndoProducer,
                               Mode::RedoProducer,
                               Mode::Crop,
                               Mode::Mask,
                               Mode::Epoch};
    for (const auto mode : modes)
        (void)run(mode);

    ++modesRun;
    (void)prepare(false);
    (void)_session.BeginSelectionMove();
    _session.MoveSelection(1, 1);
    const auto queuedOpening = _session.SelectionMoveOpening();
    const auto oldRequest = QueueHostInput(HostSelectionMoveCloseInput{queuedOpening.value(), false});
    _session.CancelSelectionMove();
    (void)_session.BeginSelectionMove();
    _session.MoveSelection(1, 1);
    const auto reopenedDocument = _session.Document();
    const auto reopenedHistory = _session.history.CaptureState();
    const auto reopenedGeneration = _session.Generation();
    const auto reopenedOpening = _session.SelectionMoveOpening();
    const auto queuedGate = std::make_shared<SelectionMoveCloseTestGate>();
    _selectionMoveCloseTestGate = queuedGate;
    (void)expect("queued-opening", "refused-before-capture",
                 SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                     oldRequest.Read().outcome == HostOutcome::Stale && reopenedOpening != queuedOpening &&
                     _session.SelectionMoveOpening() == reopenedOpening && _session.Document() == reopenedDocument &&
                     _session.Generation() == reopenedGeneration && _session.history.IsCurrent(reopenedHistory) &&
                     queuedGate->captures.load() == 0 && queuedGate->visits.load() == 0 && !_historyWork.Busy() &&
                     !_selectionMoveCloseTicket && !_selectionMoveCloseStamp && _jobs.ReservedMemoryBytes() == 0,
                 "a queued old opening captured or closed its equal-geometry replacement");
    _selectionMoveCloseTestGate.reset();
    _session.CancelSelectionMove();

    for (const bool allocationFault : {false, true})
    {
        ++modesRun;
        (void)prepare(false);
        (void)_session.BeginSelectionMove();
        _session.MoveSelection(1, 1);
        _drag = OutlineDrag{Vec2{1, 1}, Vec2{1, 1}};
        const auto before = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto generation = _session.Generation();
        std::optional<HostInputBudget::Reservation> budget;
        if (allocationFault)
            _hostAdmissionAllocationFailureForTest = true;
        else
            budget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
        const auto refusal = QueueSelectionMoveClose(false);
        _hostAdmissionAllocationFailureForTest = false;
        const bool statePreserved =
            refusal.Read().outcome == HostOutcome::Failed &&
            refusal.Read().error == (allocationFault ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
            (allocationFault || budget.has_value()) && std::holds_alternative<std::monostate>(_drag) &&
            _session.Document() == before && _session.Generation() == generation && _session.IsMovingSelection() &&
            _session.history.IsCurrent(history) && !_historyWork.Busy() && !_selectionMoveCloseTicket &&
            _jobs.ReservedMemoryBytes() == 0;
        (void)expect(allocationFault ? "input-allocation" : "input-budget", "refusal-preserves-frozen-draft",
                     statePreserved, "host input refusal mutated history or synchronously closed the move");
        budget.reset();
        const auto retry = QueueSelectionMoveClose(true);
        (void)expect(allocationFault ? "input-allocation" : "input-budget", "retry-restores-origin",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                         retry.Read().outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                         _session.Selection() == std::optional(rect(1, 1, 2, 2)) && _hostInputBudget.Used() == 0,
                     "fresh Cancel could not recover the retained draft after event input admission refusal");
    }

    // Fill the real scheduler record table so StartSelectionMoveClose observes admission refusal before capture.
    (void)prepare(false);
    (void)_session.BeginSelectionMove();
    _session.MoveSelection(1, 1);
    const auto admissionOpening = _session.SelectionMoveOpening();
    const auto admissionDocument = _session.Document();
    const auto admissionRevision = _session.history.CurrentRevision();
    bool slotsFilled = admissionOpening.has_value();
    for (std::size_t index = 0; slotsFilled && index < JobScheduler::kCapacity; ++index)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupySelectionMoveCloseSlot>();
        JobTicket ticket;
        slotsFilled = SUCCEEDED(_jobs.Submit(job, {JobClass::Background}, ticket));
    }
    ++modesRun;
    const auto refused = admissionOpening ? QueueHostInput(HostSelectionMoveCloseInput{admissionOpening.value(), false})
                                          : ContinuationReceipt::Failure(E_ABORT);
    for (unsigned int pass = 0; pass < 4 && refused.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    const auto admissionSettled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
    const auto refusedResult = refused.Read();
    (void)expect(
        "admission", "real-record-capacity",
        slotsFilled && admissionSettled == SettleResult::Settled && refusedResult.outcome == HostOutcome::Failed &&
            refusedResult.error == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) &&
            _session.Document() == admissionDocument && _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
            _session.IsMovingSelection() && _session.history.CurrentRevision() == admissionRevision &&
            !_historyWork.Busy() && !_selectionMoveCloseTicket && !_selectionMoveCloseStamp,
        "real scheduler capacity refusal captured or published the move, or failed to release admission state");
    if (_session.IsMovingSelection())
        _session.CancelSelectionMove();
    (void)prepare(false);
    (void)_session.BeginSelectionMove();
    _session.MoveSelection(1, 1);
    const auto recovery = QueueSelectionMoveClose(false);
    (void)expect("admission", "fresh-request-recovers",
                 SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled &&
                     recovery.Read().outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                     _session.Selection() == std::optional(rect(2, 2, 3, 3)) && !_historyWork.Busy() &&
                     _jobs.ReservedMemoryBytes() == 0,
                 "fresh outline closure failed after actual record-capacity refusal");
    std::fprintf(stderr, "SUMMARY selection-move-host assertions=%d failures=%d modes=%d\n", passed, failed, modesRun);
    return failed == 0 ? 0 : 5;
}

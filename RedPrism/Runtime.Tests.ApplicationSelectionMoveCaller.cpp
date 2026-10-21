#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.SelectionMoveCloseTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <variant>

int Application::RunSelectionMoveCallerSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit([this, originalViewport] { _viewport = originalViewport; });
    enum class Mode
    {
        ChangedTool,
        NestedTool,
        UndoAfterTool,
        SameTool,
        SelectedSameTool,
        SelectedTool,
        ToolCommand,
        Undo,
        Redo,
        DeselectAfterEnd,
        SelectAllAfterEnd,
        CaptureLoss,
        Write,
        Enter,
        FailedTool,
        FailedCommand,
        FailedCommandBoundary,
        ToolInputBudget,
        ToolInputAllocation,
        CommandInputBudget,
    };
    struct State final
    {
        Application& owner;
        const char* name;
        Mode mode;
        std::shared_ptr<SelectionMoveCloseTestGate> gate;
        std::optional<ContinuationReceipt> receipt;
        std::optional<ContinuationReceipt> youngerTool;
        std::optional<CanvasDocument> document;
        std::optional<DocumentSelection> selection;
        DocumentHistory::StateView history;
        std::optional<Guid> activeLayer;
        std::optional<std::uint64_t> opening;
        std::uint64_t generation = 0;
        std::uint64_t documentSerial = 0;
        int undoCount = 0;
        std::size_t redoCount = 0;
        Guid revision;
        EditorTool tool = EditorTool::Move;
        bool visited = false;
        bool held = false;
        bool openingCaptured = false;
        bool latePointerIgnored = false;
        bool youngerPending = false;
    };

    int passed = 0;
    int failed = 0;
    int modes = 0;
    const auto expect =
        [&passed, &failed](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        if (!condition)
        {
            ++failed;
            std::fprintf(stderr, "FAIL selection-move-caller.%s.%s: %s\n", mode, check, detail);
            return false;
        }
        ++passed;
        std::fprintf(stderr, "PASS selection-move-caller.%s.%s\n", mode, check);
        return true;
    };
    const auto rect = [](double left, double top, double right, double bottom, bool antialiased = false)
    {
        return DocumentSelection{antialiased, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{right, top},
                                                                           Vec2{right, bottom}, Vec2{left, bottom}}}};
    };
    const auto prepare = [this, &rect](bool seedRedo, bool nested = false)
    {
        _drag = PointerDrag{};
        _session.ClearDocument();
        _session.CreateDocument(4, 4, true);
        _session.AddBlankLayer();
        _session.SetSelection(rect(1, 1, 2, 2), L"Caller fixture selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        if (seedRedo)
        {
            _session.SetSelection(rect(0, 0, 1, 1), L"Seed redo");
            _session.Undo();
            _session.history.MarkSaved();
        }
        _tool = EditorTool::Move;
        _viewport.zoom = 1;
        _viewport.followsFit = false;
        if (nested)
            _session.history.Begin(L"Outer caller transaction", _session.Document(), _session.ActiveLayerID());
        if (!_session.BeginSelectionMove())
            return false;
        _session.MoveSelection(1, 1);
        _drag = OutlineDrag{Vec2{1, 1}, Vec2{1, 1}};
        return _session.SelectionMoveOpening().has_value();
    };
    const auto makeGate = [](bool failWorker)
    {
        auto gate = std::make_shared<SelectionMoveCloseTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (failWorker)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        return gate;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != SelectionMoveCloseTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }

        state.visited = true;
        if (!state.receipt && owner._activeHostRequest)
            state.receipt = owner._activeHostRequest->input.Receipt();
        if (owner._activeHostRequest)
        {
            const auto& input = owner._activeHostRequest->input.Input();
            if (const auto* tool = std::get_if<HostToolInput>(&input))
                state.openingCaptured = tool->selectionMoveOpening == state.opening;
            else if (const auto* command = std::get_if<HostCommandInput>(&input))
                state.openingCaptured = command->selectionMoveOpening == state.opening;
            else if (const auto* close = std::get_if<HostSelectionMoveCloseInput>(&input))
                state.openingCaptured = close->opening == state.opening;
        }
        if (!state.receipt && owner._pendingSelectionMoveCloseOpening == state.opening &&
            owner._pendingSelectionMoveCloseReceipt.Read().outcome == HostOutcome::Pending)
            state.receipt = owner._pendingSelectionMoveCloseReceipt;
        state.held =
            state.gate->release && owner._selectionMoveCloseTicket.has_value() &&
            owner._selectionMoveCloseStamp.has_value() && owner._historyWork.Busy() &&
            owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes && state.receipt &&
            state.receipt->Read().outcome == HostOutcome::Pending && owner._session.Document() == state.document &&
            owner._session.Selection() == state.selection && owner._session.SelectionMoveOpening() == state.opening &&
            owner._session.IsMovingSelection() && owner._session.Generation() == state.generation &&
            owner._session.DocumentSerial() == state.documentSerial &&
            owner._session.ActiveLayerID() == state.activeLayer && owner._session.history.IsCurrent(state.history) &&
            owner._session.history.UndoCount() == state.undoCount &&
            owner._session.history.CaptureState().FutureCount() == state.redoCount && owner._tool == state.tool;

        (void)owner.SelectionPointerMove(owner._viewport.ViewFromDocument(Vec2{3, 3}, owner.DocumentSize()),
                                         MK_LBUTTON);
        state.latePointerIgnored =
            owner._session.Document() == state.document && owner._session.Selection() == state.selection &&
            owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history);
        if (state.mode == Mode::DeselectAfterEnd || state.mode == Mode::UndoAfterTool)
        {
            state.youngerTool = state.mode == Mode::UndoAfterTool
                                    ? owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo, std::nullopt})
                                    : owner.QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
            state.youngerPending = state.youngerTool->Read().outcome == HostOutcome::Pending &&
                                   state.receipt->Read().outcome == HostOutcome::Pending && owner._tool == state.tool &&
                                   owner._session.history.IsCurrent(state.history);
        }
        (void)SetEvent(state.gate->release.get());
    };

    const auto run = [&](Mode mode, int boundaryCommand = 0, const char* boundaryName = nullptr) -> bool
    {
        ++modes;
        const bool seededRedo = mode == Mode::Undo || mode == Mode::Redo || mode == Mode::FailedCommand;
        if (!prepare(seededRedo, mode == Mode::NestedTool))
            return expect("fixture", "outline-opened", false, "could not open the literal outline move");

        State state{*this, "unknown", mode};
        switch (mode)
        {
        case Mode::ChangedTool:
            state.name = "on-tool-changed";
            break;
        case Mode::NestedTool:
            state.name = "nested-tool";
            break;
        case Mode::UndoAfterTool:
            state.name = "undo-after-tool";
            break;
        case Mode::SameTool:
            state.name = "on-tool-same";
            break;
        case Mode::SelectedSameTool:
            state.name = "selected-tool-same";
            break;
        case Mode::SelectedTool:
            state.name = "selected-tool-input";
            break;
        case Mode::ToolCommand:
            state.name = "tool-command";
            break;
        case Mode::Undo:
            state.name = "selected-undo";
            break;
        case Mode::Redo:
            state.name = "selected-redo";
            break;
        case Mode::DeselectAfterEnd:
            state.name = "deselect-after-end";
            break;
        case Mode::SelectAllAfterEnd:
            state.name = "select-all-after-end";
            break;
        case Mode::CaptureLoss:
            state.name = "capture-loss";
            break;
        case Mode::Write:
            state.name = "write-preserves";
            break;
        case Mode::Enter:
            state.name = "enter-preserves";
            break;
        case Mode::FailedTool:
            state.name = "failed-tool-retry";
            break;
        case Mode::FailedCommand:
            state.name = "failed-command-retry";
            break;
        case Mode::FailedCommandBoundary:
            state.name = boundaryName;
            break;
        case Mode::ToolInputBudget:
            state.name = "tool-input-budget";
            break;
        case Mode::ToolInputAllocation:
            state.name = "tool-input-allocation";
            break;
        case Mode::CommandInputBudget:
            state.name = "command-input-budget";
            break;
        }
        state.document = _session.Document();
        state.selection = _session.Selection();
        state.history = _session.history.CaptureState();
        state.activeLayer = _session.ActiveLayerID();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.documentSerial = _session.DocumentSerial();
        state.undoCount = _session.history.UndoCount();
        state.redoCount = _session.history.CaptureState().FutureCount();
        state.revision = _session.history.CurrentRevision();
        state.tool = _tool;

        if (mode == Mode::Write || mode == Mode::Enter)
        {
            const EditEnd end = mode == Mode::Write ? EditEnd::Write : EditEnd::Enter;
            const bool ended = EndCanvasEdit(CanvasEdit::OutlineMove, end);
            const bool preserved = !ended && _session.Document() == state.document &&
                                   _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                                   _session.SelectionMoveOpening() == state.opening && _session.IsMovingSelection() &&
                                   _session.history.IsCurrent(state.history) &&
                                   _session.history.UndoCount() == state.undoCount &&
                                   _session.history.CaptureState().FutureCount() == state.redoCount;
            bool ok = true;
            ok &= expect(state.name, "direct-end", !ended, "Write or Enter consumed the outline move");
            ok &= expect(state.name, "live-outline-and-history", preserved,
                         "Write or Enter changed the live moved outline or its history");
            return ok;
        }

        if (mode == Mode::SameTool || mode == Mode::SelectedSameTool)
        {
            if (mode == Mode::SameTool)
                OnTool(EditorTool::Move);
            else
            {
                const auto receipt = QueueHostInput(HostToolInput{EditorTool::Move, std::nullopt});
                const auto settled = SettleJobs(
                    _jobs, 15000, [](void* opaque, void* native) noexcept
                    { static_cast<Application*>(opaque)->DispatchQueuedMessage(*static_cast<MSG*>(native)); }, this);
                (void)expect(state.name, "selected-acknowledged",
                             settled == SettleResult::Settled && receipt.Read().outcome == HostOutcome::Succeeded,
                             "the already active tool input was not acknowledged");
            }
            const bool unchanged =
                _tool == EditorTool::Move && _session.IsMovingSelection() &&
                _session.SelectionMoveOpening() == state.opening && _session.Selection() == state.selection &&
                _session.history.IsCurrent(state.history) && !_historyWork.Busy() && !_selectionMoveCloseTicket;
            return expect(state.name, "no-close", unchanged,
                          "selecting the already active tool closed or changed the outline move");
        }

        const bool failureRoute =
            mode == Mode::FailedTool || mode == Mode::FailedCommand || mode == Mode::FailedCommandBoundary;
        state.gate = makeGate(failureRoute);
        if (!state.gate->release)
            return expect(state.name, "gate-event", false, "CreateEventW failed");
        _selectionMoveCloseTestGate = state.gate;
        if (mode == Mode::ToolInputBudget || mode == Mode::ToolInputAllocation || mode == Mode::CommandInputBudget)
        {
            std::optional<HostInputBudget::Reservation> budget;
            _hostAdmissionAllocationFailureForTest = mode == Mode::ToolInputAllocation;
            if (mode != Mode::ToolInputAllocation)
                budget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
            const auto refused = mode == Mode::CommandInputBudget
                                     ? QueueHostInput(HostCommandInput{ChromeHost::kCommandSelectAll, std::nullopt})
                                     : QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
            budget.reset();
            _hostAdmissionAllocationFailureForTest = false;
            const auto error =
                mode == Mode::ToolInputAllocation ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
            (void)expect(state.name, "refuses-before-capture",
                         refused.Read().outcome == HostOutcome::Failed &&
                             refused.Read().error == static_cast<std::int32_t>(error) &&
                             state.gate->captures.load() == 0 && state.gate->visits.load() == 0 &&
                             !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0,
                         "refused caller acquired a job, capture or history barrier");
            (void)SelectionPointerMove(_viewport.ViewFromDocument(Vec2{3, 3}, DocumentSize()), MK_LBUTTON);
            (void)expect(state.name, "stops-samples-retains-exact-state",
                         _session.Document() == state.document && _session.Selection() == state.selection &&
                             _session.SelectionMoveOpening() == state.opening &&
                             _session.history.IsCurrent(state.history) && _session.Generation() == state.generation &&
                             _tool == state.tool && !std::holds_alternative<OutlineDrag>(_drag),
                         "refused dependent caller changed the move or kept pointer sampling");
        }
        switch (mode)
        {
        case Mode::ChangedTool:
        case Mode::NestedTool:
        case Mode::UndoAfterTool:
        case Mode::FailedTool:
            OnTool(EditorTool::Hand);
            break;
        case Mode::SelectedTool:
        case Mode::ToolInputBudget:
        case Mode::ToolInputAllocation:
            state.receipt = QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
            break;
        case Mode::ToolCommand:
            OnCommand(ChromeHost::SelectToolCommand(EditorTool::Hand));
            break;
        case Mode::Undo:
        case Mode::FailedCommand:
            state.receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo, std::nullopt});
            break;
        case Mode::Redo:
            state.receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo, std::nullopt});
            break;
        case Mode::DeselectAfterEnd:
            state.receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect, std::nullopt});
            break;
        case Mode::SelectAllAfterEnd:
        case Mode::CommandInputBudget:
            OnCommand(ChromeHost::kCommandSelectAll);
            break;
        case Mode::CaptureLoss:
            EndPointerDrag(PointerEnd::Lost);
            break;
        case Mode::FailedCommandBoundary:
            OnCommand(boundaryCommand);
            break;
        case Mode::SameTool:
        case Mode::SelectedSameTool:
        case Mode::Write:
        case Mode::Enter:
            break;
        }

        auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto firstResult = state.receipt ? state.receipt->Read() : ContinuationReceipt::Result{};
        bool ok = true;
        ok &= expect(state.name, "settled", settled == SettleResult::Settled,
                     "SettleJobs did not acknowledge the held close and selected input");
        ok &= expect(state.name, "held-real-gate", state.visited && state.held,
                     "the real worker gate did not hold the pending receipt, reservation, exact document and history");
        if (mode != Mode::CaptureLoss)
            ok &= expect(state.name, "event-time-opening", state.openingCaptured,
                         "the selected host input did not retain its event-time outline opening");
        ok &= expect(state.name, "late-pointer-ignored", state.latePointerIgnored,
                     "a later pointer sample changed the move while closure was held");

        if (failureRoute)
        {
            ok &= expect(state.name, "worker-error-retains-input",
                         firstResult.outcome == HostOutcome::Failed && firstResult.error == E_ACCESSDENIED &&
                             _session.IsMovingSelection() && _session.Selection() == state.selection &&
                             _session.SelectionMoveOpening() == state.opening &&
                             _session.history.IsCurrent(state.history) && _tool == EditorTool::Move &&
                             _session.history.UndoCount() == state.undoCount &&
                             _session.history.CaptureState().FutureCount() == state.redoCount,
                         "failed close applied the selected tool or command, or changed the live move/history");
            if (mode == Mode::FailedCommandBoundary)
            {
                ok &= expect(state.name, "failed-dependent-never-runs",
                             !_historyWork.Busy() && !_selectionMoveCloseTicket && _jobs.ReservedMemoryBytes() == 0 &&
                                 !_session.HasColorEdit() && !_session.PendingTransformEdit(),
                             "a refused closure retained work or executed its dependent command");
                _selectionMoveCloseTestGate.reset();
                return ok;
            }
            state.gate = makeGate(false);
            if (!state.gate->release)
            {
                (void)expect(state.name, "retry-gate-event", false, "CreateEventW failed on retry");
                _selectionMoveCloseTestGate.reset();
                return false;
            }
            _selectionMoveCloseTestGate = state.gate;
            if (mode == Mode::FailedTool)
                state.receipt = QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
            else
                state.receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo, std::nullopt});
            state.visited = false;
            state.held = false;
            state.latePointerIgnored = false;
            settled = SettleJobs(_jobs, 15000, pump, &state);
            const auto retryResult = state.receipt->Read();
            ok &= expect(state.name, "retry-settled", settled == SettleResult::Settled && state.visited && state.held,
                         "fresh selected input did not pass through a second held close");
            if (mode == Mode::FailedTool)
                ok &= expect(state.name, "retry-applies-tool",
                             retryResult.outcome == HostOutcome::Succeeded && _tool == EditorTool::Hand &&
                                 !_session.IsMovingSelection() &&
                                 _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                                 _session.history.UndoName() == L"Move Selection",
                             "retry did not close the move before applying the selected tool");
            else
                ok &= expect(state.name, "retry-consumes-undo",
                             retryResult.outcome == HostOutcome::Succeeded && _tool == EditorTool::Move &&
                                 !_session.IsMovingSelection() &&
                                 _session.Selection() == std::optional(rect(1, 1, 2, 2)) &&
                                 _session.history.UndoCount() == state.undoCount &&
                                 _session.history.CaptureState().FutureCount() == state.redoCount,
                             "retry did not cancel the first active outline edit before Undo could restore history");
        }
        else if (mode == Mode::Undo || mode == Mode::Redo)
        {
            ok &= expect(state.name, "first-edit-cancels-only",
                         firstResult.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                             _session.Selection() == std::optional(rect(1, 1, 2, 2)) &&
                             _session.history.UndoCount() == state.undoCount &&
                             _session.history.CaptureState().FutureCount() == state.redoCount &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.history.CaptureState().Depth() == 0,
                         "Undo or Redo restored history instead of consuming the first active outline move");
        }
        else if (mode == Mode::NestedTool)
        {
            ok &= expect(state.name, "keeps-outer-depth",
                         firstResult.outcome == HostOutcome::Succeeded && _tool == EditorTool::Hand &&
                             !_session.IsMovingSelection() && _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                             _session.history.CaptureState().Depth() == 1 &&
                             _session.history.UndoCount() == state.undoCount,
                         "tool closure ended or renamed the enclosing transaction");
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            ok &= expect(state.name, "outer-title-and-undo",
                         _session.history.UndoName() == L"Outer caller transaction" &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.CaptureState().Depth() == 0,
                         "closing the enclosing transaction did not record its independent title");
            _session.Undo();
            ok &= expect(state.name, "outer-undo-restores-origin",
                         _session.Selection() == std::optional(rect(1, 1, 2, 2)),
                         "outer Undo did not restore the literal opening outline");
        }
        else if (mode == Mode::UndoAfterTool)
        {
            ok &= expect(state.name, "younger-undo-waits", state.youngerPending, "younger Undo passed the held End");
            ok &= expect(state.name, "undo-after-completed-end",
                         firstResult.outcome == HostOutcome::Succeeded && state.youngerTool &&
                             state.youngerTool->Read().outcome == HostOutcome::Succeeded && _tool == EditorTool::Hand &&
                             !_session.IsMovingSelection() && _session.Selection() == std::optional(rect(1, 1, 2, 2)) &&
                             _session.history.UndoCount() == state.undoCount &&
                             _session.history.RedoName() == L"Move Selection",
                         "younger Undo was consumed as gesture cancellation instead of restoring the completed End");
        }
        else if (mode == Mode::DeselectAfterEnd)
        {
            ok &= expect(state.name, "younger-input-waits", state.youngerPending,
                         "younger tool input was applied or acknowledged before outline closure");
            ok &= expect(state.name, "move-then-deselect",
                         firstResult.outcome == HostOutcome::Succeeded && state.youngerTool &&
                             state.youngerTool->Read().outcome == HostOutcome::Succeeded &&
                             !_session.IsMovingSelection() && !_session.Selection() && _tool == EditorTool::Hand &&
                             _session.history.UndoCount() == state.undoCount + 2 &&
                             _session.history.UndoName() == L"Deselect",
                         "Deselect did not publish after Move Selection, ahead of the younger tool input");
        }
        else if (mode == Mode::SelectAllAfterEnd || mode == Mode::CommandInputBudget)
        {
            ok &= expect(state.name, "move-then-select-all",
                         firstResult.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                             _session.Selection() == std::optional(rect(0, 0, 4, 4, true)) &&
                             _session.history.UndoCount() == state.undoCount + 2 &&
                             _session.history.UndoName() == L"Select All",
                         "Select All did not publish after the exact moved outline");
            _session.Undo();
            ok &= expect(state.name, "undo-select-all-retains-move",
                         _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                             _session.history.UndoName() == L"Move Selection",
                         "Undo restored the wrong selection before Select All");
            _session.Undo();
            ok &= expect(state.name, "undo-move-restores-origin",
                         _session.Selection() == std::optional(rect(1, 1, 2, 2)) &&
                             _session.history.UndoCount() == state.undoCount,
                         "the older move entry did not retain the literal origin");
        }
        else
        {
            const bool toolRoute = mode == Mode::ChangedTool || mode == Mode::SelectedTool ||
                                   mode == Mode::ToolCommand || mode == Mode::ToolInputBudget ||
                                   mode == Mode::ToolInputAllocation;
            const bool closed = firstResult.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                                _session.Selection() == std::optional(rect(2, 2, 3, 3)) &&
                                _session.history.UndoCount() == state.undoCount + 1 &&
                                _session.history.UndoName() == L"Move Selection";
            ok &= expect(state.name, "move-entry", closed,
                         "successful caller did not publish the exact moved rectangle as one Move Selection entry");
            if (toolRoute)
                ok &= expect(state.name, "tool-after-move",
                             _tool == EditorTool::Hand && firstResult.outcome == HostOutcome::Succeeded,
                             "selected tool was applied before or without successful move closure");
            if (mode == Mode::CaptureLoss)
                ok &= expect(state.name, "lost-capture-ends-move", closed,
                             "capture loss did not close the live OutlineDrag at its moved rectangle");
        }
        _selectionMoveCloseTestGate.reset();
        return ok;
    };

    (void)run(Mode::ChangedTool);
    (void)run(Mode::NestedTool);
    (void)run(Mode::UndoAfterTool);
    (void)run(Mode::SameTool);
    (void)run(Mode::SelectedSameTool);
    (void)run(Mode::SelectedTool);
    (void)run(Mode::ToolCommand);
    (void)run(Mode::Undo);
    (void)run(Mode::Redo);
    (void)run(Mode::DeselectAfterEnd);
    (void)run(Mode::SelectAllAfterEnd);
    (void)run(Mode::CaptureLoss);
    (void)run(Mode::Write);
    (void)run(Mode::Enter);
    (void)run(Mode::FailedTool);
    (void)run(Mode::FailedCommand);
    (void)run(Mode::ToolInputBudget);
    (void)run(Mode::ToolInputAllocation);
    (void)run(Mode::CommandInputBudget);
    struct CommandCase final
    {
        int command;
        const char* name;
    };
    // Independent production End-before-action routes: a failed closure must reach no dependent action.
    constexpr std::array commands{
        CommandCase{ChromeHost::kCommandCut, "cut-refused"},
        CommandCase{ChromeHost::kCommandPaste, "paste-refused"},
        CommandCase{ChromeHost::kCommandLayerViaCopy, "layer-copy-refused"},
        CommandCase{ChromeHost::kCommandCurves, "curves-refused"},
        CommandCase{ChromeHost::kCommandLevels, "levels-refused"},
        CommandCase{ChromeHost::kCommandHueSaturation, "hsv-refused"},
        CommandCase{ChromeHost::kCommandExposure, "exposure-refused"},
        CommandCase{ChromeHost::kCommandGradientMap, "gradient-map-refused"},
        CommandCase{ChromeHost::kCommandGrain, "grain-refused"},
        CommandCase{ChromeHost::kCommandGaussianBlur, "gaussian-refused"},
        CommandCase{ChromeHost::kCommandMotionBlur, "motion-refused"},
        CommandCase{ChromeHost::kCommandAddNoise, "noise-refused"},
        CommandCase{ChromeHost::kCommandLensCorrection, "lens-refused"},
        CommandCase{ChromeHost::kCommandContentAwareFill, "heal-refused"},
        CommandCase{ChromeHost::kCommandNewLevelsAdjustment, "new-levels-refused"},
        CommandCase{ChromeHost::kCommandNewCurvesAdjustment, "new-curves-refused"},
        CommandCase{ChromeHost::kCommandNewHueSaturationAdjustment, "new-hsv-refused"},
        CommandCase{ChromeHost::kCommandNewExposureAdjustment, "new-exposure-refused"},
        CommandCase{ChromeHost::kCommandNewGradientMapAdjustment, "new-gradient-map-refused"},
        CommandCase{ChromeHost::kCommandNewGrainAdjustment, "new-grain-refused"},
        CommandCase{ChromeHost::kCommandAddMask, "mask-refused"},
        CommandCase{ChromeHost::kCommandAddBlackMask, "black-mask-refused"},
        CommandCase{ChromeHost::kCommandSelectAll, "select-all-refused"},
        CommandCase{ChromeHost::kCommandDeselect, "deselect-refused"},
        CommandCase{ChromeHost::kCommandInverse, "inverse-refused"},
        CommandCase{ChromeHost::kCommandLoadLayerSelection, "load-layer-refused"},
        CommandCase{ChromeHost::kCommandLoadMaskSelection, "load-mask-refused"},
    };
    for (const auto& command : commands)
        (void)run(Mode::FailedCommandBoundary, command.command, command.name);

    for (const auto command : {0, ChromeHost::kCommandDeselect, ChromeHost::kCommandUndo})
    {
        ++modes;
        const auto name = command == 0                              ? "stale-tool-opening"
                          : command == ChromeHost::kCommandDeselect ? "stale-deselect-opening"
                                                                    : "stale-undo-opening";
        if (!prepare(false))
        {
            (void)expect(name, "fixture", false, "outline fixture failed");
            continue;
        }
        const auto oldOpening = _session.SelectionMoveOpening();
        const auto receipt = command == 0 ? QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt})
                                          : QueueHostInput(HostCommandInput{command, std::nullopt});
        _session.CancelSelectionMove();
        const bool reopened = _session.BeginSelectionMove();
        _session.MoveSelection(1, 1);
        const auto document = _session.Document();
        const auto selection = _session.Selection();
        const auto history = _session.history.CaptureState();
        const auto generation = _session.Generation();
        const auto opening = _session.SelectionMoveOpening();
        const auto gate = makeGate(false);
        _selectionMoveCloseTestGate = gate;
        const auto settled = SettleJobs(
            _jobs, 15000, [](void* opaque, void* native) noexcept
            { static_cast<Application*>(opaque)->DispatchQueuedMessage(*static_cast<MSG*>(native)); }, this);
        (void)expect(name, "equal-geometry-new-opening",
                     reopened && opening && opening != oldOpening && selection == std::optional(rect(2, 2, 3, 3)),
                     "fixture did not reopen identical geometry with different authority");
        (void)expect(name, "refuses-before-capture",
                     settled == SettleResult::Settled && receipt.Read().outcome == HostOutcome::Stale &&
                         gate->captures.load() == 0 && gate->visits.load() == 0,
                     "stale input captured or closed the replacement outline");
        (void)expect(name, "exact-replacement-preserved",
                     _session.Document() == document && _session.Selection() == selection &&
                         _session.SelectionMoveOpening() == opening && _session.Generation() == generation &&
                         _session.history.IsCurrent(history) && _tool == EditorTool::Move && !_historyWork.Busy() &&
                         _jobs.ReservedMemoryBytes() == 0,
                     "stale dependent input changed the new outline, tool or history");
        _selectionMoveCloseTestGate.reset();
    }

    _selectionMoveCloseTestGate.reset();
    _drag = PointerDrag{};
    if (_session.IsMovingSelection())
        _session.CancelSelectionMove();
    std::fprintf(stderr, "SUMMARY selection-move-caller assertions=%d failures=%d modes=%d\n", passed, failed, modes);
    return failed == 0 ? 0 : 5;
}

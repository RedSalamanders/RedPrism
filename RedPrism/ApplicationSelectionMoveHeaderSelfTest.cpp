#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "SelectionCommandTestGate.h"
#include "SelectionMoveCloseTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace
{
constexpr Vec2 kSelectionMoveHeaderStart{1.2, 1.2};
constexpr std::array<std::uint8_t, 16> kSelectionMoveHeaderPixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                                  70, 80, 90, 255, 100, 110, 120, 255};

DocumentSelection SelectionMoveHeaderRectangle(double left, double top, double right, double bottom)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom},
                                                                 Vec2{left, bottom}}}};
}
} // namespace

// Production-route controls for the three selection-header actions that close an eligible idle outline move.
int Application::RunSelectionMoveHeaderSelfTest() noexcept
{
    struct State final
    {
        Application& owner;
        const char* name = "unknown";
        SelectionAction action = SelectionAction::Deselect;
        double value = 0.0;
        std::shared_ptr<SelectionMoveCloseTestGate> closeGate;
        std::shared_ptr<SelectionCommandTestGate> commandGate;
        std::optional<ContinuationReceipt> receipt;
        std::optional<ContinuationReceipt> younger;
        std::optional<CanvasDocument> document;
        std::optional<DocumentSelection> selection;
        DocumentHistory::StateView history;
        std::optional<std::uint64_t> opening;
        std::optional<std::uint64_t> ordinal;
        std::uint64_t generation = 0;
        int undoCount = 0;
        std::size_t redoCount = 0;
        LassoKind marqueeKind = LassoKind::Rectangle;
        LassoKind lassoKind = LassoKind::Freehand;
        bool moved = false;
        bool nested = false;
        bool commandFailure = false;
        bool commandCancellation = false;
        bool closeFailure = false;
        bool closeCancellation = false;
        bool unchangedKind = false;
        bool closeVisited = false;
        bool closeHeld = false;
        bool commandVisited = false;
        bool commandHeld = false;
        bool eventInputExact = false;
        bool busyInputRefused = false;
        bool latePointerIgnored = false;
        bool youngerPending = false;
        bool cancellationPending = false;
        bool reservationObserved = false;
        bool staleReplacementSurvived = false;
    };

    const auto originalViewport = _viewport;
    const auto originalTool = _tool;
    const auto originalMarqueeKind = _session.marqueeKind;
    const auto originalLassoKind = _session.lassoKind;
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalTool, originalMarqueeKind, originalLassoKind]() noexcept
        {
            _viewport = originalViewport;
            _tool = originalTool;
            _session.marqueeKind = originalMarqueeKind;
            _session.lassoKind = originalLassoKind;
            _selectionMoveCloseTestGate.reset();
            _selectionCommandTestGate.reset();
            _hostAdmissionAllocationFailureForTest = false;
            _drag = PointerDrag{};
            if (_session.IsMovingSelection())
                _session.CancelSelectionMove();
        });

    int assertions = 0;
    int failures = 0;
    int modes = 0;
    const auto expect =
        [&assertions, &failures](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        ++assertions;
        std::fprintf(stderr, "%s selection-move-header.%s.%s%s%s\n", condition ? "PASS" : "FAIL", mode, check,
                     condition ? "" : ": ", condition ? "" : detail);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto rectangle = [](double left, double top, double right, double bottom)
    { return SelectionMoveHeaderRectangle(left, top, right, bottom); };
    const auto prepare = [this, &rectangle](bool moved, bool nested, bool seedRedo = false) -> bool
    {
        _drag = PointerDrag{};
        _session.ClearDocument();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 4;
        ImageLayer layer = ImageLayer::Blank(L"Selection header source", Vec2{2, 2});
        const Guid activeId = layer.id;
        layer.asset = RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, kSelectionMoveHeaderPixels.data(),
                                               kSelectionMoveHeaderPixels.size()),
                                  {},
                                  layer.name};
        layer.transform.origin = Vec2{1, 1};
        layer.transform.size = Vec2{2, 2};
        layer.transform.sampling = LayerSampling::Nearest;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document).Seal(), activeId);
        _session.SetSelection(rectangle(1, 1, 2, 2), L"Selection header opening");
        _session.history.Reset();
        _session.history.MarkSaved();
        if (seedRedo)
        {
            _session.SetSelection(rectangle(2, 1, 3, 2), L"Header redo source");
            _session.Undo();
        }
        if (nested)
            _session.history.Begin(L"Outer selection header transaction", _session.Document(),
                                   _session.ActiveLayerID());
        _tool = EditorTool::Marquee;
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
        if (!_session.BeginSelectionMove())
            return false;
        _drag = OutlineDrag{kSelectionMoveHeaderStart, Vec2{}};
        if (moved)
        {
            _session.MoveSelection(1.0, 0.0);
            _drag = OutlineDrag{kSelectionMoveHeaderStart, Vec2{1.0, 0.0}};
        }
        return _session.SelectionMoveOpening().has_value();
    };
    const auto makeCloseGate = [](JobResult result = {})
    {
        auto gate = std::make_shared<SelectionMoveCloseTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = true;
        gate->result = result;
        return gate;
    };
    const auto makeCommandGate = [](JobResult result = {})
    {
        auto gate = std::make_shared<SelectionCommandTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = true;
        gate->result = result;
        return gate;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionMoveCloseTestGate::kMessage)
        {
            state.closeVisited = true;
            if (owner._activeHostRequest)
            {
                auto& request = owner._activeHostRequest.value();
                state.receipt = request.input.Receipt();
                state.ordinal = request.ordinal;
                if (const auto* header = std::get_if<HostSelectionMoveHeaderInput>(&request.input.Input()))
                    state.eventInputExact = state.opening == std::optional(header->opening) &&
                                            header->action == state.action && header->value == state.value;
            }
            const auto expectedSelection = state.selection;
            state.closeHeld =
                state.closeGate && state.closeGate->release && owner._activeHostRequest && state.receipt &&
                state.receipt->Read().outcome == HostOutcome::Pending && owner._selectionMoveCloseTicket &&
                owner._selectionMoveCloseStamp && owner._historyWork.Busy() &&
                owner._session.Document() == state.document && owner._session.Selection() == expectedSelection &&
                owner._session.IsMovingSelection() && owner._session.SelectionMoveOpening() == state.opening &&
                owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history) &&
                owner._session.marqueeKind == state.marqueeKind && owner._session.lassoKind == state.lassoKind &&
                std::holds_alternative<std::monostate>(owner._drag);
            state.reservationObserved = state.closeGate && state.closeGate->reservationBytes != 0 &&
                                        owner._jobs.ReservedMemoryBytes() >= state.closeGate->reservationBytes;
            (void)owner.SelectionPointerMove(owner._viewport.ViewFromDocument(Vec2{3, 3}, owner.DocumentSize()),
                                             MK_LBUTTON);
            state.latePointerIgnored =
                owner._session.Document() == state.document && owner._session.Selection() == expectedSelection &&
                owner._session.SelectionMoveOpening() == state.opening &&
                owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history);

            // This second header event reaches the preserved busy refusal while the first typed request is owned.
            const auto busyAction =
                state.action == SelectionAction::Deselect ? SelectionAction::SetMarqueeKind : state.action;
            const auto busyValue = state.action == SelectionAction::Deselect
                                       ? (owner._session.marqueeKind == LassoKind::Rectangle ? 1.0 : 0.0)
                                       : 1.0 - state.value;
            const auto marqueeBeforeBusyInput = owner._session.marqueeKind;
            const auto lassoBeforeBusyInput = owner._session.lassoKind;
            owner.OnSelectionAction(busyAction, busyValue);
            state.busyInputRefused = owner._session.marqueeKind == marqueeBeforeBusyInput &&
                                     owner._session.lassoKind == lassoBeforeBusyInput && state.receipt &&
                                     state.receipt->Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            if (!state.closeFailure && !state.closeCancellation && !state.commandFailure && !state.commandCancellation)
            {
                state.younger = owner.QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
                state.youngerPending = state.younger->Read().outcome == HostOutcome::Pending && state.receipt &&
                                       state.receipt->Read().outcome == HostOutcome::Pending;
            }
            if (state.closeCancellation)
            {
                owner.CancelHostContinuations();
                state.cancellationPending = state.receipt && state.receipt->Read().outcome == HostOutcome::Pending &&
                                            owner._activeHostRequest && owner._historyWork.Busy() &&
                                            owner._selectionMoveCloseTicket.has_value();
            }
            if (state.closeGate && state.closeGate->release)
                (void)SetEvent(state.closeGate->release.get());
            return;
        }
        if (message.message == SelectionCommandTestGate::kMessage)
        {
            state.commandVisited = true;
            const auto* header =
                owner._activeHostRequest
                    ? std::get_if<HostSelectionMoveHeaderInput>(&owner._activeHostRequest.value().input.Input())
                    : nullptr;
            state.commandHeld =
                owner._hostStage == Application::HostStage::SelectionHeaderFollowup && header && state.ordinal &&
                owner._activeHostRequest.value().ordinal == state.ordinal.value() && state.receipt &&
                state.receipt->Read().outcome == HostOutcome::Pending && owner._selectionCommandTicket &&
                owner._selectionCommandStamp && owner._historyWork.Busy() && !owner._session.IsMovingSelection() &&
                owner._session.Selection() == state.selection &&
                owner._session.history.UndoCount() == state.undoCount + (state.moved && !state.nested ? 1 : 0);
            state.reservationObserved =
                state.reservationObserved || (state.commandGate && state.commandGate->reservationBytes != 0 &&
                                              owner._jobs.ReservedMemoryBytes() >= state.commandGate->reservationBytes);
            if (state.commandCancellation)
            {
                owner.CancelHostContinuations();
                state.cancellationPending = state.receipt && state.receipt->Read().outcome == HostOutcome::Pending &&
                                            owner._activeHostRequest && owner._historyWork.Busy() &&
                                            owner._selectionCommandTicket.has_value();
            }
            if (state.commandGate && state.commandGate->release)
                (void)SetEvent(state.commandGate->release.get());
            return;
        }
        owner.DispatchQueuedMessage(message);
    };

    const auto runHeader = [&](const char* name, SelectionAction action, double value, bool moved, bool nested = false,
                               bool unchangedKind = false) -> bool
    {
        ++modes;
        const bool seedRedo = !moved && action == SelectionAction::SetMarqueeKind && value == 0.0;
        if (!prepare(moved, nested, seedRedo))
            return expect(name, "fixture-opened", false, "could not prepare the literal 4x4 outline move");
        State state{*this};
        state.name = name;
        state.action = action;
        state.value = value;
        state.moved = moved;
        state.nested = nested;
        state.unchangedKind = unchangedKind;
        state.document = _session.Document();
        state.selection = moved ? std::optional(rectangle(2, 1, 3, 2)) : std::optional(rectangle(1, 1, 2, 2));
        state.history = _session.history.CaptureState();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.undoCount = _session.history.UndoCount();
        state.redoCount = state.history.FutureCount();
        _session.marqueeKind = unchangedKind                                               ? LassoKind::Rectangle
                               : action == SelectionAction::SetMarqueeKind && value >= 0.5 ? LassoKind::Rectangle
                                                                                           : LassoKind::Ellipse;
        _session.lassoKind = unchangedKind                                             ? LassoKind::Freehand
                             : action == SelectionAction::SetLassoKind && value >= 0.5 ? LassoKind::Freehand
                                                                                       : LassoKind::Polygonal;
        state.marqueeKind = _session.marqueeKind;
        state.lassoKind = _session.lassoKind;
        state.closeGate = makeCloseGate();
        _selectionMoveCloseTestGate = state.closeGate;
        if (action == SelectionAction::Deselect)
        {
            state.commandGate = makeCommandGate();
            _selectionCommandTestGate = state.commandGate;
        }

        OnSelectionAction(action, value);
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto result = state.receipt ? state.receipt->Read() : ContinuationReceipt::Result{};
        bool ok = expect(name, "settled-to-ack", settled == SettleResult::Settled,
                         "SettleJobs did not acknowledge the End, dependent command, and younger input");
        if (unchangedKind)
            ok &= expect(name, "setting-identical-before-input",
                         action == SelectionAction::SetMarqueeKind && value == 0.0 &&
                             state.marqueeKind == LassoKind::Rectangle,
                         "the control did not begin with the requested Rectangle setting already selected");
        ok &= expect(name, "actual-header-input", state.closeVisited && state.eventInputExact,
                     "OnSelectionAction did not admit the exact typed action, value, and opening");
        ok &= expect(name, "held-end-preserves-live-state",
                     state.closeHeld && state.latePointerIgnored && state.reservationObserved,
                     "held End changed the live outline/settings, sampled a stopped pointer, or lost its reservation");
        ok &= expect(name, "busy-header-keeps-original-refusal", state.busyInputRefused,
                     "a younger header callback mutated state or escaped the pending-request Busy gate");

        const bool deselect = action == SelectionAction::Deselect;
        const bool kindAction = action == SelectionAction::SetMarqueeKind || action == SelectionAction::SetLassoKind;
        const bool expectedMoveEntry = moved && !nested;
        if (deselect)
        {
            ok &= expect(name, "second-worker-reached-and-ordered",
                         state.commandVisited && state.commandHeld && state.youngerPending && state.younger &&
                             state.younger->Read().outcome == HostOutcome::Succeeded &&
                             result.outcome == HostOutcome::Succeeded && _tool == EditorTool::Hand,
                         "Deselect did not use its acknowledged second worker before the younger tool");
            if (nested)
            {
                ok &= expect(name, "nested-depth-after-header",
                             result.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                                 !_session.Selection() && _session.history.CaptureState().Depth() == 1 &&
                                 _session.history.UndoCount() == state.undoCount,
                             "header End/Deselect changed the outer transaction depth or published early");
                _session.history.End(_session.Document(), _session.ActiveLayerID());
                ok &= expect(name, "nested-title-and-single-undo",
                             _session.history.UndoName() == L"Outer selection header transaction" &&
                                 _session.history.UndoCount() == state.undoCount + 1 &&
                                 _session.history.CaptureState().Depth() == 0,
                             "outer transaction title/depth was not retained through header End and Deselect");
                _session.Undo();
                ok &= expect(name, "nested-undo-literal-origin",
                             _session.Selection() == std::optional(rectangle(1, 1, 2, 2)),
                             "Undo of the outer transaction did not restore the literal opening rectangle");
            }
            else if (moved)
            {
                ok &= expect(name, "move-then-deselect-two-entries",
                             result.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                                 !_session.Selection() && _session.history.UndoCount() == state.undoCount + 2 &&
                                 _session.history.UndoName() == L"Deselect",
                             "moved Deselect did not publish exactly Move Selection then Deselect");
                _session.Undo();
                ok &= expect(name, "undo-one-restores-moved-rectangle",
                             _session.Selection() == std::optional(rectangle(2, 1, 3, 2)) &&
                                 _session.history.UndoName() == L"Move Selection",
                             "first Undo after Deselect did not restore the literal moved rectangle");
                _session.Undo();
                ok &= expect(name, "undo-two-restores-opening-rectangle",
                             _session.Selection() == std::optional(rectangle(1, 1, 2, 2)) &&
                                 _session.history.UndoCount() == state.undoCount,
                             "second Undo did not restore the literal original rectangle");
            }
            else
            {
                ok &= expect(name, "zero-offset-deselect-one-entry",
                             result.outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                                 !_session.Selection() && _session.history.UndoCount() == state.undoCount + 1 &&
                                 _session.history.UndoName() == L"Deselect",
                             "zero-offset Deselect created a move entry or failed to record Deselect");
                _session.Undo();
                ok &= expect(name, "zero-offset-undo-restores-opening",
                             _session.Selection() == std::optional(rectangle(1, 1, 2, 2)),
                             "Undo of zero-offset Deselect did not restore the literal opening rectangle");
            }
        }
        else if (kindAction)
        {
            const auto expectedKind = action == SelectionAction::SetMarqueeKind
                                          ? (value >= 0.5 ? LassoKind::Ellipse : LassoKind::Rectangle)
                                          : (value >= 0.5 ? LassoKind::Polygonal : LassoKind::Freehand);
            const bool exactKind = action == SelectionAction::SetMarqueeKind ? _session.marqueeKind == expectedKind
                                                                             : _session.lassoKind == expectedKind;
            ok &= expect(name, "kind-value-applied-after-end",
                         result.outcome == HostOutcome::Succeeded && exactKind && !_session.IsMovingSelection() &&
                             _session.Selection() == state.selection && state.youngerPending && state.younger &&
                             state.younger->Read().outcome == HostOutcome::Succeeded && _tool == EditorTool::Hand,
                         "the event-time kind value did not apply after End and before the younger tool");
            ok &= expect(name, "kind-history-count",
                         _session.history.UndoCount() == state.undoCount + (expectedMoveEntry ? 1 : 0) &&
                             (!expectedMoveEntry || _session.history.UndoName() == L"Move Selection"),
                         "kind change added a history entry or failed to retain the single moved-outline entry");
            if (seedRedo)
            {
                ok &= expect(name, "no-op-kind-retains-redo",
                             state.redoCount == 1 && _session.history.CaptureState().FutureCount() == state.redoCount,
                             "zero-offset header End discarded an existing redo entry");
                _session.Redo();
                ok &= expect(name, "retained-redo-restores-literal-selection",
                             _session.Selection() == std::optional(rectangle(2, 1, 3, 2)) &&
                                 _session.history.UndoName() == L"Header redo source" &&
                                 _session.marqueeKind == expectedKind,
                             "retained Redo failed to restore its selection or changed the header kind");
                _session.Undo();
            }
            if (moved && !nested)
            {
                _session.Undo();
                ok &= expect(name, "kind-move-undo-literal-origin",
                             _session.Selection() == std::optional(rectangle(1, 1, 2, 2)) &&
                                 (action == SelectionAction::SetMarqueeKind ? _session.marqueeKind == expectedKind
                                                                            : _session.lassoKind == expectedKind),
                             "Undo did not restore the original rectangle while keeping the requested kind");
            }
        }
        ok &= expect(name, "released-owners-and-reservation",
                     !_historyWork.Busy() && !_activeHostRequest && !_selectionMoveCloseTicket &&
                         !_selectionMoveCloseStamp && !_selectionCommandTicket && !_selectionCommandStamp &&
                         _jobs.ReservedMemoryBytes() == 0,
                     "an acknowledged header request retained a worker owner, ticket, stamp, or reservation");
        _selectionMoveCloseTestGate.reset();
        _selectionCommandTestGate.reset();
        return ok;
    };

    const auto runFailureOrCancellation = [&](const char* name, bool moved, bool closeStage, bool cancellation) -> bool
    {
        ++modes;
        if (!prepare(moved, false))
            return expect(name, "fixture-opened", false, "could not prepare the literal 4x4 outline move");
        State state{*this};
        state.name = name;
        state.action = SelectionAction::Deselect;
        state.value = 0.0;
        state.moved = moved;
        state.commandFailure = !closeStage && !cancellation;
        state.commandCancellation = !closeStage && cancellation;
        state.closeFailure = closeStage && !cancellation;
        state.closeCancellation = closeStage && cancellation;
        state.document = _session.Document();
        state.selection = moved ? std::optional(rectangle(2, 1, 3, 2)) : std::optional(rectangle(1, 1, 2, 2));
        state.history = _session.history.CaptureState();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.undoCount = _session.history.UndoCount();
        state.redoCount = state.history.FutureCount();
        _session.marqueeKind = LassoKind::Ellipse;
        _session.lassoKind = LassoKind::Polygonal;
        state.marqueeKind = _session.marqueeKind;
        state.lassoKind = _session.lassoKind;
        if (closeStage)
        {
            state.closeGate =
                makeCloseGate(state.closeFailure ? JobResult{JobStatus::Failed, E_ACCESSDENIED} : JobResult{});
            _selectionMoveCloseTestGate = state.closeGate;
        }
        else
        {
            state.closeGate = makeCloseGate();
            state.commandGate =
                makeCommandGate(state.commandFailure ? JobResult{JobStatus::Failed, E_ACCESSDENIED} : JobResult{});
            _selectionMoveCloseTestGate = state.closeGate;
            _selectionCommandTestGate = state.commandGate;
        }

        OnSelectionAction(SelectionAction::Deselect, 0.0);
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto result = state.receipt ? state.receipt->Read() : ContinuationReceipt::Result{};
        bool ok = expect(name, "settled-through-real-worker-ack",
                         settled == SettleResult::Settled && state.receipt.has_value(),
                         "the real close/selection-command worker did not settle with its retained receipt");
        ok &= expect(
            name, closeStage ? "close-gate-held-reserved" : "command-gate-held-reserved",
            (closeStage ? state.closeVisited && state.closeHeld
                        : state.closeVisited && state.closeHeld && state.commandVisited && state.commandHeld) &&
                state.reservationObserved && (!cancellation || state.cancellationPending),
            "the real gated worker did not retain its reservation, history barrier, and input through cancel/fail");
        if (closeStage)
        {
            const auto expectedOutcome = cancellation ? HostOutcome::Cancelled : HostOutcome::Failed;
            const auto expectedError = cancellation ? E_ABORT : E_ACCESSDENIED;
            ok &= expect(
                name, "close-failure-keeps-live-outline",
                result.outcome == expectedOutcome && result.error == static_cast<std::int32_t>(expectedError) &&
                    _session.IsMovingSelection() && _session.Selection() == state.selection &&
                    _session.SelectionMoveOpening() == state.opening && _session.Generation() == state.generation &&
                    _session.history.IsCurrent(state.history) && _session.history.UndoCount() == state.undoCount &&
                    _session.marqueeKind == state.marqueeKind && _session.lassoKind == state.lassoKind &&
                    !_historyWork.Busy() && !_activeHostRequest && !_selectionMoveCloseTicket &&
                    std::holds_alternative<std::monostate>(_drag),
                "failed/cancelled End ran Deselect or changed the live selection/history/settings");
        }
        else
        {
            const auto expectedOutcome = cancellation ? HostOutcome::Cancelled : HostOutcome::Failed;
            const auto expectedError = cancellation ? E_ABORT : E_ACCESSDENIED;
            ok &=
                expect(name, "followup-failure-keeps-ended-move",
                       result.outcome == expectedOutcome && result.error == static_cast<std::int32_t>(expectedError) &&
                           !_session.IsMovingSelection() && !_session.SelectionMoveOpening() &&
                           _session.Selection() == state.selection &&
                           _session.history.UndoCount() == state.undoCount + (moved ? 1 : 0) &&
                           (!moved || _session.history.UndoName() == L"Move Selection") && !_historyWork.Busy() &&
                           !_activeHostRequest && !_selectionCommandTicket && !_selectionCommandStamp &&
                           _jobs.ReservedMemoryBytes() == 0,
                       "the second worker rewound the committed End or published a partial Deselect");
            if (moved)
            {
                _session.Undo();
                ok &=
                    expect(name, "failed-followup-undo-restores-origin",
                           _session.Selection() == std::optional(rectangle(1, 1, 2, 2)) &&
                               _session.history.UndoCount() == state.undoCount,
                           "Undo after failed/cancelled moved Deselect did not restore the literal opening rectangle");
            }
            else
            {
                ok &= expect(name, "zero-offset-failure-has-no-move-entry",
                             _session.history.UndoCount() == state.undoCount &&
                                 _session.Selection() == std::optional(rectangle(1, 1, 2, 2)),
                             "zero-offset End created a history entry when the second worker failed");
            }
        }
        _selectionMoveCloseTestGate.reset();
        _selectionCommandTestGate.reset();
        return ok;
    };

    const auto runAdmissionRetry = [&](const char* name, bool allocationFailure) -> bool
    {
        ++modes;
        if (!prepare(false, false))
            return expect(name, "fixture-opened", false, "could not prepare the zero-offset outline move");
        State state{*this};
        state.name = name;
        state.action = SelectionAction::SetMarqueeKind;
        state.value = 0.0;
        state.selection = rectangle(1, 1, 2, 2);
        state.document = _session.Document();
        state.history = _session.history.CaptureState();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.undoCount = _session.history.UndoCount();
        _session.marqueeKind = LassoKind::Ellipse;
        _drag = OutlineDrag{kSelectionMoveHeaderStart, Vec2{}};
        std::optional<HostInputBudget::Reservation> budget;
        if (allocationFailure)
            _hostAdmissionAllocationFailureForTest = true;
        else
            budget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
        OnSelectionAction(SelectionAction::SetMarqueeKind, 0.0);
        _hostAdmissionAllocationFailureForTest = false;
        const bool refusedWithoutMutation =
            _session.IsMovingSelection() && _session.Selection() == state.selection &&
            _session.SelectionMoveOpening() == state.opening && _session.Generation() == state.generation &&
            _session.history.IsCurrent(state.history) && _session.marqueeKind == LassoKind::Ellipse &&
            std::holds_alternative<std::monostate>(_drag) && !_historyWork.Busy() && !_activeHostRequest &&
            _hostRequests.Size() == 0 && _jobs.ReservedMemoryBytes() == 0 && (allocationFailure || budget.has_value());
        bool ok = expect(name, "refusal-preserves-outline-and-drops-pointer", refusedWithoutMutation,
                         "header admission refusal changed settings/history or retained OutlineDrag samples");
        budget.reset();
        state.closeGate = makeCloseGate();
        state.marqueeKind = _session.marqueeKind;
        state.lassoKind = _session.lassoKind;
        _selectionMoveCloseTestGate = state.closeGate;
        const auto beforeRetry = _session.marqueeKind;
        OnSelectionAction(SelectionAction::SetMarqueeKind, 0.0);
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        ok &= expect(name, "fresh-header-retry-applies",
                     settled == SettleResult::Settled && state.receipt &&
                         state.receipt->Read().outcome == HostOutcome::Succeeded && !_session.IsMovingSelection() &&
                         _session.marqueeKind == LassoKind::Rectangle && beforeRetry == LassoKind::Ellipse &&
                         _hostInputBudget.Used() == 0 && _jobs.ReservedMemoryBytes() == 0,
                     "a fresh OnSelectionAction could not retry after budget/allocation refusal");
        _selectionMoveCloseTestGate.reset();
        return ok;
    };

    // Zero-offset and moved cases, both scalar values for each kind action, and both Deselect geometries.
    (void)runHeader("marquee-zero-rectangle", SelectionAction::SetMarqueeKind, 0.0, false);
    (void)runHeader("marquee-zero-ellipse", SelectionAction::SetMarqueeKind, 1.0, false);
    (void)runHeader("marquee-moved-rectangle", SelectionAction::SetMarqueeKind, 0.0, true);
    (void)runHeader("marquee-moved-ellipse", SelectionAction::SetMarqueeKind, 1.0, true);
    (void)runHeader("marquee-moved-identical-kind-end", SelectionAction::SetMarqueeKind, 0.0, true, false, true);
    (void)runHeader("lasso-zero-freehand", SelectionAction::SetLassoKind, 0.0, false);
    (void)runHeader("lasso-zero-polygonal", SelectionAction::SetLassoKind, 1.0, false);
    (void)runHeader("lasso-moved-freehand", SelectionAction::SetLassoKind, 0.0, true);
    (void)runHeader("lasso-moved-polygonal", SelectionAction::SetLassoKind, 1.0, true);
    (void)runHeader("deselect-zero", SelectionAction::Deselect, 0.0, false);
    (void)runHeader("deselect-moved", SelectionAction::Deselect, 0.0, true);
    (void)runHeader("deselect-nested-transaction", SelectionAction::Deselect, 0.0, true, true);
    (void)runFailureOrCancellation("deselect-moved-command-failure", true, false, false);
    (void)runFailureOrCancellation("deselect-zero-command-failure", false, false, false);
    (void)runFailureOrCancellation("deselect-moved-command-cancellation", true, false, true);
    (void)runFailureOrCancellation("header-close-worker-failure", true, true, false);
    (void)runFailureOrCancellation("header-close-cancellation", true, true, true);
    (void)runAdmissionRetry("admission-budget-retry", false);
    (void)runAdmissionRetry("admission-allocation-retry", true);

    // An equal-geometry replacement has a new opening token. The already-admitted actual header callback must fail
    // before capture and leave that replacement untouched.
    ++modes;
    if (prepare(true, false))
    {
        const auto oldOpening = _session.SelectionMoveOpening();
        const auto original = _session.Selection();
        const auto kindBefore = _session.marqueeKind;
        _selectionMoveCloseTestGate = makeCloseGate();
        _selectionMoveCloseTestGate->hold = false; // A broken stale guard must fail, rather than leave a worker held.
        OnSelectionAction(SelectionAction::SetMarqueeKind, 1.0);
        const bool admitted = _hostRequests.Size() == 1 && _hostInputBudget.Used() != 0;
        const auto* queued = _hostRequests.Front();
        const auto staleReceipt = queued ? std::optional(queued->input.Receipt()) : std::nullopt;
        _session.CancelSelectionMove();
        _drag = PointerDrag{};
        const bool replacementOpened = _session.BeginSelectionMove();
        if (replacementOpened)
            _session.MoveSelection(1.0, 0.0);
        const auto newOpening = _session.SelectionMoveOpening();
        State state{*this};
        state.name = "equal-geometry-replaced-opening";
        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        state.staleReplacementSurvived =
            replacementOpened && oldOpening && newOpening && oldOpening != newOpening &&
            original == _session.Selection() && _session.IsMovingSelection() &&
            _session.SelectionMoveOpening() == newOpening && _session.marqueeKind == kindBefore &&
            _selectionMoveCloseTestGate->captures == 0 && _selectionMoveCloseTestGate->visits == 0 &&
            !_historyWork.Busy() && !_activeHostRequest && _hostRequests.Size() == 0;
        bool ok = expect(state.name, "actual-callback-admitted-before-replacement",
                         admitted && oldOpening.has_value() && settled == SettleResult::Settled,
                         "the test did not admit OnSelectionAction and settle its queued stale request");
        ok &= expect(state.name, "new-opening-untouched-before-capture", state.staleReplacementSurvived,
                     "equal geometry let an old header request capture or close the replacement opening");
        ok &= expect(state.name, "owned-receipt-is-stale",
                     staleReceipt && staleReceipt->Read().outcome == HostOutcome::Stale &&
                         staleReceipt->Read().error == E_ABORT && _hostInputBudget.Used() == 0,
                     "the old accepted header input did not publish Stale and release its input reservation");
        _selectionMoveCloseTestGate.reset();
    }
    else
    {
        (void)expect("equal-geometry-replaced-opening", "fixture-opened", false,
                     "could not prepare the original zero-offset outline move");
    }

    // With a coexisting color edit, the request is ineligible for this typed route and follows the established
    // synchronous header behavior. Keep this as a boundary probe; it does not claim coverage for the panel edit.
    ++modes;
    if (prepare(false, false))
    {
        State state{*this};
        state.name = "coexisting-edit-boundary";
        _session.BeginColorEdit(FilterKind::Levels, true);
        const bool panelOpened = _session.HasColorEdit(FilterKind::Levels);
        (void)expect(state.name, "reached-live-panel", panelOpened && _session.HasColorEdit(FilterKind::Levels),
                     "the boundary did not reach an actual coexisting Levels panel");
        const auto selectionBefore = _session.Selection();
        const auto hostSizeBefore = _hostRequests.Size();
        const auto historyBefore = _session.history.UndoCount();
        _selectionMoveCloseTestGate = makeCloseGate();
        _selectionMoveCloseTestGate->hold = false;
        const auto refused = QueueSelectionMoveHeader(SelectionAction::SetMarqueeKind, 1.0);
        (void)expect(state.name, "typed-route-refuses-before-capture",
                     refused.Read().outcome == HostOutcome::Failed && refused.Read().error == E_ABORT &&
                         _session.IsMovingSelection() && _selectionMoveCloseTestGate->captures == 0 &&
                         _hostRequests.Size() == hostSizeBefore,
                     "a settled header producer closed or captured a coexisting panel's outline");
        OnSelectionAction(SelectionAction::SetMarqueeKind, 1.0);
        const bool stayedOffTypedRoute = _session.marqueeKind == LassoKind::Ellipse && !_session.IsMovingSelection() &&
                                         _session.HasColorEdit(FilterKind::Levels) &&
                                         _session.Selection() == selectionBefore &&
                                         _hostRequests.Size() == hostSizeBefore && !_historyWork.Busy() &&
                                         _session.history.UndoCount() == historyBefore;
        (void)expect(state.name, "coexisting-edit-keeps-reference-route", stayedOffTypedRoute,
                     "an ineligible coexisting edit entered the new End/header continuation route");
        CancelColorEdit();
        (void)expect(state.name, "cleanup-settles",
                     SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled,
                     "the coexisting fixture left a selected worker behind");
        _selectionMoveCloseTestGate.reset();
    }
    else
    {
        (void)expect("coexisting-edit-boundary", "fixture-opened", false,
                     "could not prepare the coexisting-edit boundary fixture");
    }

    std::fprintf(stderr, "SUMMARY selection-move-header assertions=%d failures=%d modes=%d\n", assertions, failures,
                 modes);
    return failures == 0 ? 0 : 5;
}

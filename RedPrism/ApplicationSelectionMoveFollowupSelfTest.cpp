#include "Application.h"

#include "ChromeHost.h"
#include "SelectionCommandTestGate.h"
#include "SelectionMoveCloseTestGate.h"
#include "WandLayerHostTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace
{
constexpr Vec2 kClick{1.2, 1.2};
constexpr std::array<std::uint8_t, 16> kPixels{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};

DocumentSelection FollowupRectangle(double left, double top, double right, double bottom, bool aa = false)
{
    return DocumentSelection{
        aa, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}}}};
}

WandSettings FollowupSettings()
{
    WandSettings settings;
    settings.tolerance = 0;
    settings.sampleSize = WandSampleSize::Point;
    settings.contiguous = true;
    settings.sampleAllLayers = false;
    return settings;
}
} // namespace

// Production-route controls for the SelectionRelease zero-offset continuation. The host input carries only the
// immutable opening token and optional event-time New-wand request; workers are held by the existing production test
// gates.
int Application::RunSelectionMoveFollowupSelfTest() noexcept
{
    enum class Mode
    {
        DeselectOrderedTool,
        WandOrderedUndo,
        EventTimeWandCapture,
        WandNoOpPreservesRedo,
        NestedHistory,
        CloseWorkerFailure,
        CloseCaptureFailure,
        CloseCancellation,
        FollowupWorkerFailure,
        FollowupCancellation,
        AdmissionBudgetRetry,
        AdmissionAllocationRetry,
        StaleEqualGeometryOpening,
        LatePointerIgnored,
        ViewAndWindowPump,
        CoexistingPanelReference,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        const char* name = "unknown";
        std::shared_ptr<SelectionMoveCloseTestGate> closeGate;
        std::shared_ptr<SelectionCommandTestGate> commandGate;
        std::shared_ptr<WandLayerHostTestGate> wandGate;
        std::optional<ContinuationReceipt> receipt;
        std::optional<ContinuationReceipt> youngerOne;
        std::optional<ContinuationReceipt> youngerTwo;
        std::optional<CanvasDocument> document;
        std::optional<DocumentSelection> selection;
        DocumentHistory::StateView history;
        std::optional<std::uint64_t> opening;
        std::optional<std::uint64_t> hostOrdinal;
        std::uint64_t generation = 0;
        int undoCount = 0;
        std::size_t futureCount = 0;
        EditorTool tool = EditorTool::Move;
        bool closeVisited = false;
        bool closeHeld = false;
        bool followupVisited = false;
        bool followupHeld = false;
        bool eventInputExact = false;
        bool latePointerIgnored = false;
        bool youngerPending = false;
        bool cancellationHeld = false;
        bool windowPosted = false;
        bool windowSeenPending = false;
        bool zoomChangedPending = false;
    };

    const auto originalViewport = _viewport;
    const auto originalSettings = _session.wandSettings;
    const auto originalAntialiased = _session.selectionAntialiased;
    const auto originalTool = _tool;
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalSettings, originalAntialiased, originalTool]() noexcept
        {
            _viewport = originalViewport;
            _session.wandSettings = originalSettings;
            _session.selectionAntialiased = originalAntialiased;
            _tool = originalTool;
            _selectionMoveCloseTestGate.reset();
            _selectionCommandTestGate.reset();
            _wandLayerHostTestGate.reset();
            _drag = PointerDrag{};
            if (_session.IsMovingSelection())
                _session.CancelSelectionMove();
        });

    int assertions = 0;
    int failures = 0;
    int modes = 0;
    constexpr UINT kWindowMessage = WM_APP + 0x4dd;
    const auto expect =
        [&assertions, &failures](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        ++assertions;
        std::fprintf(stderr, "%s selection-move-followup.%s.%s%s%s\n", condition ? "PASS" : "FAIL", mode, check,
                     condition ? "" : ": ", condition ? "" : detail);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto prepare = [this](bool seedRedo, bool nested, bool widerOpening) -> bool
    {
        _drag = PointerDrag{};
        _session.ClearDocument();
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 4;
        ImageLayer layer = ImageLayer::Blank(L"Follow-up source", Vec2{2, 2});
        const Guid activeId = layer.id;
        layer.asset =
            RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, kPixels.data(), kPixels.size()), {}, layer.name};
        layer.transform.origin = Vec2{1, 1};
        layer.transform.size = Vec2{2, 2};
        layer.transform.sampling = LayerSampling::Nearest;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document), activeId);
        _session.wandSettings = FollowupSettings();
        _session.selectionAntialiased = false;
        _session.SetSelection(widerOpening ? FollowupRectangle(0, 0, 3, 3) : FollowupRectangle(1, 1, 2, 2),
                              L"Follow-up opening selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        if (seedRedo)
        {
            _session.SetSelection(FollowupRectangle(0, 0, 1, 1), L"Seed follow-up redo");
            _session.Undo();
            _session.history.MarkSaved();
        }
        if (nested)
            _session.history.Begin(L"Outer follow-up transaction", _session.Document(), _session.ActiveLayerID());
        _tool = EditorTool::Marquee;
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
        if (!_session.BeginSelectionMove())
            return false;
        _drag = OutlineDrag{kClick, Vec2{}}; // a click: the live selection geometry has zero offset
        return _session.SelectionMoveOpening().has_value();
    };
    const auto makeCloseGate = [](bool hold, JobResult result = {})
    {
        auto gate = std::make_shared<SelectionMoveCloseTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = hold;
        gate->result = result;
        return gate;
    };
    const auto makeCommandGate = [](bool hold, JobResult result = {})
    {
        auto gate = std::make_shared<SelectionCommandTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = hold;
        gate->result = result;
        return gate;
    };
    const auto makeWandGate = [](bool hold, JobResult result = {})
    {
        auto gate = std::make_shared<WandLayerHostTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->hold = hold;
        gate->result = result;
        return gate;
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == kWindowMessage)
        {
            state.windowSeenPending = message.hwnd == owner._window.get() && state.receipt &&
                                      state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                                      owner._historyWork.Busy() && owner._session.Selection() == state.selection;
            owner.DispatchQueuedMessage(message);
            if (state.commandGate && state.commandGate->release)
                (void)SetEvent(state.commandGate->release.get());
            return;
        }
        if (message.message == SelectionMoveCloseTestGate::kMessage)
        {
            state.closeVisited = true;
            if (owner._activeHostRequest)
            {
                auto& request = owner._activeHostRequest.value();
                state.receipt = request.input.Receipt();
                state.hostOrdinal = request.ordinal;
                if (const auto* click = std::get_if<HostSelectionMoveClickInput>(&request.input.Input()))
                {
                    state.eventInputExact = click->opening == state.opening;
                    if (state.tool == EditorTool::Wand)
                    {
                        state.eventInputExact =
                            state.eventInputExact && click->wand.has_value() &&
                            click->wand.value().point.x == kClick.x && click->wand.value().point.y == kClick.y &&
                            click->wand.value().settings.tolerance == 0 &&
                            click->wand.value().settings.sampleSize == WandSampleSize::Point &&
                            click->wand.value().settings.contiguous && !click->wand.value().settings.sampleAllLayers &&
                            !click->wand.value().antialiased;
                    }
                    else
                    {
                        state.eventInputExact = state.eventInputExact && !click->wand;
                    }
                }
            }
            state.closeHeld =
                state.closeGate && owner._selectionMoveCloseTicket.has_value() &&
                owner._selectionMoveCloseStamp.has_value() && owner._historyWork.Busy() &&
                owner._session.Document() == state.document && owner._session.Selection() == state.selection &&
                owner._session.SelectionMoveOpening() == state.opening && owner._session.IsMovingSelection() &&
                owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history) &&
                state.receipt && state.receipt->Read().outcome == ContinuationOutcome::Pending;
            (void)owner.SelectionPointerMove(owner._viewport.ViewFromDocument(Vec2{3, 3}, owner.DocumentSize()),
                                             MK_LBUTTON);
            state.latePointerIgnored =
                owner._session.Document() == state.document && owner._session.Selection() == state.selection &&
                owner._session.SelectionMoveOpening() == state.opening &&
                owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history);
            if (state.mode == Mode::DeselectOrderedTool)
            {
                state.youngerOne = owner.QueueHostInput(HostToolInput{EditorTool::Hand, std::nullopt});
                state.youngerPending = state.youngerOne->Read().outcome == ContinuationOutcome::Pending &&
                                       state.receipt->Read().outcome == ContinuationOutcome::Pending;
            }
            else if (state.mode == Mode::CloseCancellation)
            {
                owner.CancelHostContinuations();
                state.cancellationHeld = state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                                         owner._historyWork.Busy() && owner._activeHostRequest &&
                                         owner._tool == state.tool && owner._session.history.IsCurrent(state.history);
            }
            else if (state.mode == Mode::WandOrderedUndo)
            {
                state.youngerOne = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo, std::nullopt});
                state.youngerPending = state.youngerOne->Read().outcome == ContinuationOutcome::Pending &&
                                       state.receipt->Read().outcome == ContinuationOutcome::Pending;
            }
            if (state.closeGate && state.closeGate->release)
                (void)SetEvent(state.closeGate->release.get());
            return;
        }
        if (message.message == SelectionCommandTestGate::kMessage)
        {
            state.followupVisited = true;
            const bool hasEntry = owner._activeHostRequest.has_value();
            state.followupHeld = owner._hostStage == Application::HostStage::SelectionMoveFollowup && hasEntry &&
                                 state.hostOrdinal &&
                                 owner._activeHostRequest.value().ordinal == state.hostOrdinal.value() &&
                                 owner._selectionCommandTicket.has_value() &&
                                 owner._selectionCommandStamp.has_value() && owner._historyWork.Busy() &&
                                 state.receipt && state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                                 owner._session.Selection() == state.selection && !owner._session.IsMovingSelection();
            if (state.mode == Mode::ViewAndWindowPump)
            {
                const auto before = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.zoomChangedPending = owner._viewport.zoom != before && state.receipt &&
                                           state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                                           owner._historyWork.Busy();
                state.windowPosted = PostMessageW(owner._window.get(), kWindowMessage, 0, 0) != FALSE;
                if (state.windowPosted)
                    return; // The actual queued window message releases the worker after the view command.
            }
            if (state.commandGate && state.commandGate->release)
                (void)SetEvent(state.commandGate->release.get());
            return;
        }
        if (message.message == WandLayerHostTestGate::kMessage)
        {
            state.followupVisited = true;
            const bool hasEntry = owner._activeHostRequest.has_value();
            state.followupHeld = owner._hostStage == Application::HostStage::SelectionMoveFollowup && hasEntry &&
                                 state.hostOrdinal &&
                                 owner._activeHostRequest.value().ordinal == state.hostOrdinal.value() &&
                                 owner._wandLayerHostTicket.has_value() && owner._wandLayerHostStamp.has_value() &&
                                 owner._historyWork.Busy() && state.receipt &&
                                 state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                                 owner._session.Selection() == state.selection && !owner._session.IsMovingSelection();
            if (state.mode == Mode::FollowupCancellation)
            {
                owner.CancelHostContinuations();
                state.cancellationHeld =
                    state.receipt && state.receipt->Read().outcome == ContinuationOutcome::Pending &&
                    owner._historyWork.Busy() && owner._activeHostRequest && !owner._session.IsMovingSelection();
            }
            if (state.wandGate && state.wandGate->release)
                (void)SetEvent(state.wandGate->release.get());
            return;
        }
        owner.DispatchQueuedMessage(message);
    };

    const auto run = [&](Mode mode) -> bool
    {
        ++modes;
        const bool seedRedo = mode == Mode::WandNoOpPreservesRedo;
        const bool nested = mode == Mode::NestedHistory;
        const bool widerOpening = mode == Mode::WandOrderedUndo || mode == Mode::EventTimeWandCapture ||
                                  mode == Mode::FollowupWorkerFailure || mode == Mode::FollowupCancellation;
        if (!prepare(seedRedo, nested, widerOpening))
            return expect("fixture", "opened-zero-offset-outline", false,
                          "could not prepare the literal click fixture");

        State state{*this, mode};
        switch (mode)
        {
        case Mode::DeselectOrderedTool:
            state.name = "deselect-before-younger-tool";
            break;
        case Mode::WandOrderedUndo:
            state.name = "wand-before-younger-undo";
            _tool = EditorTool::Wand;
            break;
        case Mode::EventTimeWandCapture:
            state.name = "event-time-wand-values";
            _tool = EditorTool::Wand;
            break;
        case Mode::WandNoOpPreservesRedo:
            state.name = "wand-no-op-redo";
            _tool = EditorTool::Wand;
            break;
        case Mode::NestedHistory:
            state.name = "nested-history";
            break;
        case Mode::CloseWorkerFailure:
            state.name = "close-worker-failure";
            break;
        case Mode::CloseCaptureFailure:
            state.name = "close-capture-failure";
            break;
        case Mode::CloseCancellation:
            state.name = "close-cancellation";
            break;
        case Mode::FollowupWorkerFailure:
            state.name = "followup-worker-failure";
            _tool = EditorTool::Wand;
            break;
        case Mode::FollowupCancellation:
            state.name = "followup-cancellation";
            _tool = EditorTool::Wand;
            break;
        case Mode::AdmissionBudgetRetry:
            state.name = "admission-budget-retry";
            break;
        case Mode::AdmissionAllocationRetry:
            state.name = "admission-allocation-retry";
            break;
        case Mode::StaleEqualGeometryOpening:
            state.name = "stale-equal-geometry-opening";
            break;
        case Mode::LatePointerIgnored:
            state.name = "late-pointer-ignored";
            break;
        case Mode::ViewAndWindowPump:
            state.name = "view-window-pump";
            break;
        case Mode::CoexistingPanelReference:
            state.name = "coexisting-panel-reference";
            break;
        }
        state.document = _session.Document();
        state.selection = _session.Selection();
        state.history = _session.history.CaptureState();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.undoCount = _session.history.UndoCount();
        state.futureCount = state.history.FutureCount();
        state.tool = _tool;

        const bool wantsWand = mode == Mode::WandOrderedUndo || mode == Mode::EventTimeWandCapture ||
                               mode == Mode::WandNoOpPreservesRedo || mode == Mode::FollowupWorkerFailure ||
                               mode == Mode::FollowupCancellation;
        if (mode == Mode::CoexistingPanelReference)
        {
            _session.BeginColorEdit(FilterKind::Levels, true);
            bool ok = expect(state.name, "panel-reached", _session.HasColorEdit(FilterKind::Levels),
                             "fixture did not reach a live coexisting Levels panel");
            state.closeGate = makeCloseGate(false);
            _selectionMoveCloseTestGate = state.closeGate;
            const auto refused = QueueSelectionMoveClick(std::nullopt);
            ok &= expect(state.name, "typed-route-refuses-before-close",
                         refused.Read().outcome == ContinuationOutcome::Failed && refused.Read().error == E_ABORT &&
                             _session.IsMovingSelection() && _session.SelectionMoveOpening() == state.opening &&
                             _session.Selection() == state.selection && state.closeGate->captures.load() == 0 &&
                             state.closeGate->visits.load() == 0 && !_historyWork.Busy(),
                         "settled-only follow-up captured or closed the coexisting edit");
            _drag = OutlineDrag{kClick, Vec2{}};
            SelectionRelease(_viewport.ViewFromDocument(kClick, DocumentSize()));
            ok &= expect(state.name, "actual-release-preserves-reference",
                         !_session.IsMovingSelection() && !_session.Selection() &&
                             _session.HasColorEdit(FilterKind::Levels) &&
                             _session.PendingColorEdit()->selection == state.selection &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.UndoName() == L"Deselect" && state.closeGate->captures.load() == 0,
                         "release changed the panel or dropped its original Deselect behavior");
            _session.CancelColorEdit();
            SyncChrome();
            ok &= expect(state.name, "cleanup-settles",
                         SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled,
                         "the coexisting panel's preview cleanup did not acknowledge");
            _selectionMoveCloseTestGate.reset();
            return ok;
        }
        if (mode == Mode::AdmissionBudgetRetry || mode == Mode::AdmissionAllocationRetry)
        {
            state.closeGate = makeCloseGate(false);
            _selectionMoveCloseTestGate = state.closeGate;
            std::optional<HostInputBudget::Reservation> fullBudget;
            _hostAdmissionAllocationFailureForTest = mode == Mode::AdmissionAllocationRetry;
            if (mode == Mode::AdmissionBudgetRetry)
                fullBudget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
            SelectionRelease(_viewport.ViewFromDocument(kClick, DocumentSize()));
            _hostAdmissionAllocationFailureForTest = false;
            fullBudget.reset();
            const bool refusedBeforeCapture =
                !std::holds_alternative<OutlineDrag>(_drag) && state.closeGate->captures.load() == 0 &&
                state.closeGate->visits.load() == 0 && !_historyWork.Busy() && _session.IsMovingSelection() &&
                _session.SelectionMoveOpening() == state.opening && _session.Selection() == state.selection &&
                _session.Generation() == state.generation && _session.history.IsCurrent(state.history);
            bool ok = expect(state.name, "drops-drag-before-refusal", refusedBeforeCapture,
                             "admission refusal captured work, kept the outline drag, or acquired the history barrier");
            _selectionMoveCloseTestGate.reset();
            const auto retry = QueueSelectionMoveClick(std::nullopt);
            const auto settled = SettleJobs(
                _jobs, 15000, [](void* opaque, void* native) noexcept
                { static_cast<Application*>(opaque)->DispatchQueuedMessage(*static_cast<MSG*>(native)); }, this);
            ok &= expect(state.name, "fresh-retry-closes",
                         settled == SettleResult::Settled && retry.Read().outcome == ContinuationOutcome::Succeeded &&
                             !_session.IsMovingSelection() && !_session.Selection() &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.UndoName() == L"Deselect",
                         "fresh admitted click did not close the same live opening and then Deselect");
            _session.Undo();
            ok &= expect(state.name, "retry-undo-restores-opening",
                         _session.Selection() == state.selection && _session.history.UndoCount() == state.undoCount,
                         "Undo after successful retry did not restore the literal opening outline");
            return ok;
        }
        if (mode == Mode::StaleEqualGeometryOpening)
        {
            const auto staleOpening = state.opening.value();
            const auto staleReceipt = QueueSelectionMoveClick(std::nullopt);
            _session.CancelSelectionMove();
            const bool reopened = _session.BeginSelectionMove();
            const auto newOpening = _session.SelectionMoveOpening();
            _drag = OutlineDrag{kClick, Vec2{}};
            const auto gate = makeCloseGate(false);
            _selectionMoveCloseTestGate = gate;
            const auto settled = SettleJobs(
                _jobs, 15000, [](void* opaque, void* native) noexcept
                { static_cast<Application*>(opaque)->DispatchQueuedMessage(*static_cast<MSG*>(native)); }, this);
            const bool ok = expect(
                state.name, "equal-geometry-reopen-before-capture",
                reopened && newOpening && newOpening != staleOpening &&
                    staleReceipt.Read().outcome == ContinuationOutcome::Stale && gate->captures.load() == 0 &&
                    gate->visits.load() == 0 && settled == SettleResult::Settled && _session.IsMovingSelection() &&
                    _session.Selection() == state.selection && _session.SelectionMoveOpening() == newOpening,
                "stale event captured or affected an identical-geometry replacement opening");
            _selectionMoveCloseTestGate.reset();
            return ok;
        }

        const bool closeFailure = mode == Mode::CloseWorkerFailure || mode == Mode::CloseCancellation;
        state.closeGate = makeCloseGate(
            true, mode == Mode::CloseWorkerFailure ? JobResult{JobStatus::Failed, E_ACCESSDENIED} : JobResult{});
        if (!state.closeGate->release)
            return expect(state.name, "close-gate-event", false, "CreateEventW failed");
        if (mode == Mode::CloseCaptureFailure)
            state.closeGate->throwCaptureFailure = true;
        if (mode == Mode::FollowupWorkerFailure)
            state.wandGate = makeWandGate(true, JobResult{JobStatus::Failed, E_ACCESSDENIED});
        else if (mode == Mode::FollowupCancellation)
            state.wandGate = makeWandGate(true);
        else if (wantsWand)
            state.wandGate = makeWandGate(true);
        else
            state.commandGate = makeCommandGate(true);
        if ((state.wandGate && !state.wandGate->release) || (state.commandGate && !state.commandGate->release))
            return expect(state.name, "followup-gate-event", false, "CreateEventW failed");
        _selectionMoveCloseTestGate = state.closeGate;
        _selectionCommandTestGate = state.commandGate;
        _wandLayerHostTestGate = state.wandGate;

        if (mode == Mode::EventTimeWandCapture)
        {
            _session.wandSettings = FollowupSettings();
            _session.selectionAntialiased = false;
        }
        if (mode == Mode::CloseCaptureFailure)
        {
            _drag = PointerDrag{};
            state.receipt = QueueSelectionMoveClick(std::nullopt);
        }
        else
        {
            SelectionRelease(_viewport.ViewFromDocument(mode == Mode::EventTimeWandCapture ? Vec2{2.2, 2.2} : kClick,
                                                        DocumentSize()));
        }
        if (mode == Mode::EventTimeWandCapture)
        {
            _session.wandSettings.tolerance = 255;
            _session.wandSettings.sampleSize = WandSampleSize::FiveByFive;
            _session.wandSettings.contiguous = false;
            _session.wandSettings.sampleAllLayers = true;
            _session.selectionAntialiased = true;
        }
        if (mode == Mode::LatePointerIgnored)
            (void)SelectionPointerMove(_viewport.ViewFromDocument(Vec2{3, 3}, DocumentSize()), MK_LBUTTON);

        const auto settled = SettleJobs(_jobs, 15000, pump, &state);
        const auto result = state.receipt ? state.receipt->Read() : ContinuationReceipt::Result{};
        bool ok = expect(state.name, "settled-through-acknowledgements", settled == SettleResult::Settled,
                         "SettleJobs did not drive close, follow-up, and queued writers to acknowledgement");
        ok &= expect(state.name, "retained-opening-input",
                     state.receipt && ((state.closeVisited && state.eventInputExact) ||
                                       (mode == Mode::CloseCaptureFailure && !state.closeVisited)),
                     "the click route did not retain the exact opening and event-time follow-up input");
        if (mode != Mode::CloseCaptureFailure)
            ok &= expect(state.name, "held-close-worker", state.closeHeld,
                         "close worker did not hold its receipt while the moved selection stayed unchanged");
        if (!closeFailure && mode != Mode::CloseCaptureFailure)
            ok &= expect(state.name, "held-followup-same-ordinal", state.followupVisited && state.followupHeld,
                         "follow-up was not held at SelectionMoveFollowup under the close request ordinal");
        if (state.closeVisited)
            ok &= expect(state.name, "late-sample-ignored", state.latePointerIgnored,
                         "a post-release pointer sample changed the frozen selection or history");

        if (closeFailure || mode == Mode::CloseCaptureFailure)
        {
            const auto expectedOutcome = mode == Mode::CloseWorkerFailure  ? ContinuationOutcome::Failed
                                         : mode == Mode::CloseCancellation ? ContinuationOutcome::Cancelled
                                                                           : ContinuationOutcome::Failed;
            const auto expectedError = mode == Mode::CloseWorkerFailure    ? E_ACCESSDENIED
                                       : mode == Mode::CloseCaptureFailure ? E_OUTOFMEMORY
                                                                           : E_ABORT;
            ok &= expect(state.name, "failure-stops-before-followup",
                         result.outcome == expectedOutcome && result.error == expectedError && !state.followupVisited &&
                             _session.IsMovingSelection() && _session.Selection() == state.selection &&
                             _session.SelectionMoveOpening() == state.opening &&
                             _session.Generation() == state.generation && _session.history.IsCurrent(state.history) &&
                             _session.history.UndoCount() == state.undoCount &&
                             _session.history.CaptureState().FutureCount() == state.futureCount &&
                             !std::holds_alternative<OutlineDrag>(_drag),
                         "failed close ran a follow-up or changed the live selection/history; release must still drop "
                         "OutlineDrag");
            if (mode == Mode::CloseCancellation)
                ok &= expect(state.name, "owner-cancel-retains-until-ack",
                             state.cancellationHeld && _tool == state.tool && !_historyWork.Busy() &&
                                 !_activeHostRequest && !_selectionMoveCloseTicket,
                             "owner cancellation released its active input/barrier before acknowledgement");
        }
        else if (mode == Mode::FollowupWorkerFailure || mode == Mode::FollowupCancellation)
        {
            const auto expectedOutcome =
                mode == Mode::FollowupWorkerFailure ? ContinuationOutcome::Failed : ContinuationOutcome::Cancelled;
            ok &= expect(state.name, "closed-end-keeps-original-selection",
                         result.outcome == expectedOutcome && !_session.IsMovingSelection() &&
                             !_session.SelectionMoveOpening() && _session.Selection() == state.selection &&
                             _session.Generation() == state.generation + 1 &&
                             _session.history.UndoCount() == state.undoCount &&
                             _session.history.CaptureState().FutureCount() == state.futureCount,
                         "failed follow-up rewound End or published partial geometry/history");
            if (mode == Mode::FollowupCancellation)
                ok &= expect(state.name, "owner-cancel-retains-followup-until-ack",
                             state.cancellationHeld && result.error == E_ABORT && !_historyWork.Busy() &&
                                 !_activeHostRequest && !_wandLayerHostTicket,
                             "follow-up cancellation released its active input/barrier before acknowledgement");
        }
        else if (mode == Mode::DeselectOrderedTool)
        {
            ok &= expect(state.name, "deselect-before-tool",
                         result.outcome == ContinuationOutcome::Succeeded && !_session.IsMovingSelection() &&
                             !_session.Selection() && _tool == EditorTool::Hand &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.UndoName() == L"Deselect" && state.youngerPending && state.youngerOne &&
                             state.youngerOne->Read().outcome == ContinuationOutcome::Succeeded,
                         "the younger tool ran before Deselect or Deselect did not own exactly one entry");
            _session.Undo();
            ok &= expect(state.name, "undo-deselect-restores-opening",
                         _session.Selection() == state.selection && _session.history.UndoCount() == state.undoCount,
                         "undo of the Deselect follow-up did not restore the literal opening selection");
        }
        else if (mode == Mode::WandOrderedUndo)
        {
            ok &= expect(state.name, "wand-before-younger-undo",
                         result.outcome == ContinuationOutcome::Succeeded && !_session.IsMovingSelection() &&
                             _session.Selection() == state.selection &&
                             _session.history.UndoCount() == state.undoCount && state.youngerPending &&
                             state.youngerOne && state.youngerOne->Read().outcome == ContinuationOutcome::Succeeded &&
                             _session.history.RedoName() == L"Magic Wand",
                         "Undo overtook the New wand or did not restore the literal opening selection");
        }
        else if (mode == Mode::EventTimeWandCapture)
        {
            ok &= expect(state.name, "changed-wand-records-one-entry",
                         result.outcome == ContinuationOutcome::Succeeded &&
                             _session.Selection() == std::optional(FollowupRectangle(1, 1, 2, 2)) &&
                             _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.UndoName() == L"Magic Wand" &&
                             _session.Generation() == state.generation + 2,
                         "the captured New wand did not publish its literal result as one Magic Wand entry");
        }
        else if (mode == Mode::WandNoOpPreservesRedo)
        {
            const auto current = _session.history.CaptureState();
            ok &=
                expect(state.name, "no-op-preserves-redo-generation",
                       result.outcome == ContinuationOutcome::Succeeded && _session.Selection() == state.selection &&
                           _session.Generation() == state.generation + 1 && current.Depth() == 0 &&
                           current.PastCount() == state.history.PastCount() &&
                           current.FutureCount() == state.futureCount && current.Revision() == state.history.Revision(),
                       "a geometrically identical New wand consumed redo or changed history state");
        }
        else if (mode == Mode::NestedHistory)
        {
            ok &= expect(state.name, "nested-title-depth",
                         result.outcome == ContinuationOutcome::Succeeded && !_session.IsMovingSelection() &&
                             !_session.Selection() && _session.history.CaptureState().Depth() == 1 &&
                             _session.history.UndoCount() == state.undoCount,
                         "the follow-up escaped the caller's open transaction or replaced its title/depth");
            _session.history.End(_session.Document(), _session.ActiveLayerID());
            ok &= expect(state.name, "outer-title-published",
                         _session.history.UndoName() == L"Outer follow-up transaction",
                         "the outer history transaction lost its original title");
            _session.Undo();
            ok &= expect(state.name, "undo-restores-literal-opening-selection",
                         _session.Selection() == state.selection && _session.history.UndoCount() == state.undoCount,
                         "undo of the enclosing transaction did not restore the exact opening selection");
        }
        else if (mode == Mode::LatePointerIgnored)
        {
            ok &= expect(state.name, "release-remains-zero-offset",
                         result.outcome == ContinuationOutcome::Succeeded && !_session.IsMovingSelection() &&
                             !_session.Selection() && _session.history.UndoCount() == state.undoCount + 1 &&
                             _session.history.UndoName() == L"Deselect",
                         "late pointer input changed the zero-offset follow-up or created an extra move entry");
            _session.Undo();
            ok &= expect(state.name, "late-sample-undo-restores-opening",
                         _session.Selection() == state.selection && _session.history.UndoCount() == state.undoCount,
                         "late sampling changed the original outline retained by Deselect history");
        }
        else if (mode == Mode::ViewAndWindowPump)
        {
            ok &= expect(state.name, "view-and-real-window-message-before-worker-release",
                         state.windowPosted && state.windowSeenPending && state.zoomChangedPending &&
                             result.outcome == ContinuationOutcome::Succeeded && !_session.Selection() &&
                             _session.history.UndoName() == L"Deselect",
                         "the view command or actual queued window message did not run while follow-up held");
        }

        _selectionMoveCloseTestGate.reset();
        _selectionCommandTestGate.reset();
        _wandLayerHostTestGate.reset();
        return ok;
    };

    (void)run(Mode::DeselectOrderedTool);
    (void)run(Mode::WandOrderedUndo);
    (void)run(Mode::EventTimeWandCapture);
    (void)run(Mode::WandNoOpPreservesRedo);
    (void)run(Mode::NestedHistory);
    (void)run(Mode::CloseWorkerFailure);
    (void)run(Mode::CloseCaptureFailure);
    (void)run(Mode::CloseCancellation);
    (void)run(Mode::FollowupWorkerFailure);
    (void)run(Mode::FollowupCancellation);
    (void)run(Mode::AdmissionBudgetRetry);
    (void)run(Mode::AdmissionAllocationRetry);
    (void)run(Mode::StaleEqualGeometryOpening);
    (void)run(Mode::LatePointerIgnored);
    (void)run(Mode::ViewAndWindowPump);
    (void)run(Mode::CoexistingPanelReference);

    _drag = PointerDrag{};
    if (_session.IsMovingSelection())
        _session.CancelSelectionMove();
    std::fprintf(stderr, "SUMMARY selection-move-followup assertions=%d failures=%d modes=%d\n", assertions, failures,
                 modes);
    return failures == 0 ? 0 : 5;
}

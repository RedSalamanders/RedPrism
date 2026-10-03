#include "Application.h"

#include "ChromeHost.h"
#include "Prompts.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

int Application::RunSelectionLifecycleOutlineLiteralSelfTest() noexcept
{
    const CanvasViewport originalViewport = _viewport;
    const EditorTool originalTool = _tool;
    const std::wstring originalPath = _documentPath;
    const std::optional<Prompts::Answer> originalAnswer = _forcedDiscardAnswer;
    const bool originalEndSessionPending = _endSessionPending;
    const bool originalCloseConfirmed = _closeConfirmed;
    const bool originalSessionDeferred = _endSessionPromptDeferred;
    const std::uint64_t originalEndSessionGeneration = _endSessionGeneration;

    struct Pump final
    {
        Application* owner;
        unsigned int closePosts = 0;
    };
    const auto dispatch = [](void* context, void* nativeMessage) noexcept
    {
        auto& pump = *static_cast<Pump*>(context);
        auto& message = *static_cast<MSG*>(nativeMessage);
        if (message.message == WM_CLOSE && message.hwnd == pump.owner->_window.get())
        {
            ++pump.closePosts;
            return; // SettleJobs already removed this message; never let it destroy the parent harness.
        }
        MSG postedClose{};
        while (pump.owner->_window &&
               PeekMessageW(&postedClose, pump.owner->_window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE)
        {
            ++pump.closePosts;
        }
        pump.owner->DispatchQueuedMessage(message);
    };
    Pump pump{this};
    const auto settle = [&]() noexcept { return SettleJobs(_jobs, 15000, dispatch, &pump) == SettleResult::Settled; };
    const auto drainClose = [&]() noexcept
    {
        MSG message{};
        while (_window && PeekMessageW(&message, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE)
            ++pump.closePosts;
    };
    const auto cleanup = wil::scope_exit(
        [this, originalViewport, originalTool, &originalPath, originalAnswer, originalEndSessionPending,
         originalCloseConfirmed, originalEndSessionGeneration, originalSessionDeferred, &settle, &drainClose]() noexcept
        {
            _forcedDiscardAnswer = originalAnswer;
            CancelHostContinuations();
            if (!settle())
            {
                SelfTestFailure(L"Selection lifecycle outline cleanup did not acknowledge its worker.\n");
                std::_Exit(5);
            }
            drainClose();
            if (_session.IsMovingSelection())
            {
                DropDrag<OutlineDrag>();
                _session.CancelSelectionMove();
            }
            _endSessionPending = originalEndSessionPending;
            _closeConfirmed = originalCloseConfirmed;
            _endSessionGeneration = originalEndSessionGeneration;
            _endSessionPromptDeferred = originalSessionDeferred;
            if (_window)
                (void)ShutdownBlockReasonDestroy(_window.get());
            CancelSizeCard();
            if (!settle())
            {
                SelfTestFailure(L"Selection lifecycle outline final cleanup did not settle.\n");
                std::_Exit(5);
            }
            drainClose();
            _viewport = originalViewport;
            _tool = originalTool;
            _documentPath = originalPath;
            SyncChrome();
        });

    struct Mode final
    {
        const char* route;
        Prompts::Answer answer;
    };
    constexpr std::array<Mode, 6> modes{{
        {"new.discard", Prompts::Answer::Second},
        {"new.cancel", Prompts::Answer::Cancel},
        {"close-window.discard", Prompts::Answer::Second},
        {"close-window.cancel", Prompts::Answer::Cancel},
        {"end-session.discard", Prompts::Answer::Second},
        {"end-session.cancel", Prompts::Answer::Cancel},
    }};

    int assertions = 0;
    int failures = 0;
    const auto expect = [&assertions, &failures](const char* route, const char* fact, bool condition) noexcept
    {
        ++assertions;
        std::fprintf(stderr, "%s selection-lifecycle-outline-literal.%s.%s\n", condition ? "PASS" : "FAIL", route,
                     fact);
        std::fflush(stderr);
        failures += condition ? 0 : 1;
    };
    const auto selectionMatches = [this](double left, double top, double right, double bottom) noexcept
    {
        const std::optional<DocumentSelection> selection = _session.Selection();
        if (!selection || selection.value().antialiased || selection.value().loops.size() != 1 ||
            selection.value().loops[0].size() != 4)
            return false;
        const SelectionLoop& loop = selection.value().loops[0];
        return loop[0].x == left && loop[0].y == top && loop[1].x == right && loop[1].y == top && loop[2].x == right &&
               loop[2].y == bottom && loop[3].x == left && loop[3].y == bottom;
    };

    for (const Mode& mode : modes)
    {
        CancelHostContinuations();
        CancelSizeCard();
        if (!settle())
        {
            expect(mode.route, "preceding-work-settled", false);
            break;
        }
        drainClose();
        pump.closePosts = 0;
        _forcedDiscardAnswer = mode.answer;
        _endSessionPending = false;
        _closeConfirmed = false;
        _tool = EditorTool::Marquee;
        _documentPath = L"Z:\\selection-lifecycle-outline-literal\\source.prism";
        _session.ClearDocument();
        _session.CreateDocument(40, 40, false);
        _session.AddBlankLayer();
        _session.SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{10, 10}, Vec2{30, 10},
                                                                                    Vec2{30, 30}, Vec2{10, 30}}}},
                              L"Selection lifecycle outline source");
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetOpacity(0.5);
        DocumentChanged();
        const bool fixture = _session.Document() && _session.Document()->width == 40 &&
                             _session.Document()->height == 40 && _session.Document()->layers.size() == 1 &&
                             _session.ActiveLayerID().has_value() && _session.history.IsModified() &&
                             _session.history.UndoCount() == 1 && selectionMatches(10, 10, 30, 30);
        expect(mode.route, "dirty-40x40-one-layer-opacity-half-fixture", fixture);
        if (!fixture)
            break;

        const bool opened = _session.BeginSelectionMove();
        _session.MoveSelection(5, 7);
        _drag = OutlineDrag{Vec2{20, 20}, Vec2{5, 7}};
        const auto opening = _session.SelectionMoveOpening();
        const bool live = opened && _session.IsMovingSelection() && opening.has_value() &&
                          selectionMatches(15, 17, 35, 37) && std::holds_alternative<OutlineDrag>(_drag) &&
                          std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                          std::get<OutlineDrag>(_drag).offset == Vec2{5, 7};
        expect(mode.route, "moved-outline-5-7-with-local-drag", live);
        if (!live)
            break;

        const std::vector<ImageLayer> sourceLayers = _session.Document()->layers;
        const Guid sourceActive = _session.ActiveLayerID().value();
        const auto sourceHistory = _session.history.CaptureState();
        const Guid sourceRevision = _session.history.CurrentRevision();
        const std::uint64_t sourceGeneration = _session.Generation();
        const auto sourceOpening = _session.SelectionMoveOpening();
        const std::wstring sourcePath = _documentPath;
        const int undoCount = _session.history.UndoCount();
        const bool canRedo = _session.history.CanRedo();
        const std::wstring undoName = _session.history.UndoName();
        const bool modified = _session.history.IsModified();

        if (std::string_view(mode.route).starts_with("new."))
        {
            NewDocument();
            expect(mode.route, "real-new-document-admitted-before-publication",
                   _hostRequests.Size() != 0 && _session.IsMovingSelection() &&
                       _session.history.IsCurrent(sourceHistory));
            const bool settled = settle();
            drainClose();
            expect(mode.route, "real-new-document-producer-settled",
                   settled && !_activeHostRequest && _hostRequests.Size() == 0);
            if (!settled)
                break;
            if (mode.answer == Prompts::Answer::Cancel)
            {
                expect(mode.route, "cancel-preserves-document-layers-outline-drag-and-opening",
                       _session.Document() && _session.Document()->width == 40 && _session.Document()->height == 40 &&
                           _session.Document()->layers == sourceLayers && _session.ActiveLayerID() == sourceActive &&
                           _session.IsMovingSelection() && _session.SelectionMoveOpening() == sourceOpening &&
                           selectionMatches(15, 17, 35, 37) && std::holds_alternative<OutlineDrag>(_drag) &&
                           std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                           std::get<OutlineDrag>(_drag).offset == Vec2{5, 7});
                expect(mode.route, "cancel-preserves-history-generation-and-path",
                       _session.history.IsCurrent(sourceHistory) &&
                           _session.history.CurrentRevision() == sourceRevision &&
                           _session.Generation() == sourceGeneration && _session.history.UndoCount() == undoCount &&
                           _session.history.CanRedo() == canRedo && _session.history.UndoName() == undoName &&
                           _session.history.IsModified() == modified && _documentPath == sourcePath);
            }
            else
            {
                const auto& document = _session.Document();
                const bool newCanvas = document && document->width == 1280 && document->height == 800 &&
                                       document->layers.size() == 1 && document->layers.front().name == L"Layer 1" &&
                                       !document->layers.front().asset && !_session.IsMovingSelection() &&
                                       !_session.Selection() && _session.history.UndoCount() == 1 &&
                                       _session.history.UndoName() == L"New Canvas" && !_session.history.IsModified() &&
                                       _documentPath.empty();
                expect(mode.route, "discard-creates-saved-default-new-canvas", newCanvas);
                const ContinuationReceipt undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                const bool undoSettled = settle();
                drainClose();
                expect(mode.route, "undo-restores-absent-before-document",
                       undoSettled && undo.Read().outcome == HostOutcome::Succeeded && !_session.Document() &&
                           !_session.ActiveLayerID() && _session.history.CanRedo() &&
                           _session.history.RedoName() == L"New Canvas");
                const ContinuationReceipt redo = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
                const bool redoSettled = settle();
                drainClose();
                expect(mode.route, "redo-restores-new-default-canvas",
                       redoSettled && redo.Read().outcome == HostOutcome::Succeeded && _session.Document() &&
                           _session.Document()->width == 1280 && _session.Document()->height == 800 &&
                           _session.Document()->layers.size() == 1 &&
                           _session.Document()->layers.front().name == L"Layer 1" &&
                           _session.history.UndoName() == L"New Canvas" && !_session.history.IsModified());
            }
        }
        else
        {
            if (std::string_view(mode.route).starts_with("end-session."))
            {
                // The real query posts its generation-tagged prompt. The forced flag only permits this
                // quiet harness query; the three-way answer remains independently fixed by the fixture.
                const auto previousForcedDiscard = _forcedDiscard;
                const auto restoreForcedDiscard = wil::scope_exit([this, previousForcedDiscard]() noexcept
                                                                  { _forcedDiscard = previousForcedDiscard; });
                _forcedDiscard = false;
                const auto previousGeneration = _endSessionGeneration;
                const LRESULT query = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
                expect(mode.route, "real-query-refuses-and-posts-current-generation",
                       query == FALSE && _endSessionPending && _endSessionGeneration == previousGeneration + 1 &&
                           _session.IsMovingSelection() && _session.history.IsCurrent(sourceHistory));
            }
            else
            {
                // WM_CLOSE's quiet shortcut would destroy the harness. Exercise the ordinary producer
                // for this one synchronous dispatch, then restore quiet before pumping the forced prompt.
                const bool canDispatch = _window && _runtimeFailure == S_OK && !_closeConfirmed && _messageDepth == 0 &&
                                         !CloseMustWait() && !_jobs.InUiCallback();
                expect(mode.route, "real-window-close-safe-producer-preconditions", canDispatch);
                if (!canDispatch)
                    break;
                const bool previousQuiet = _quietUi;
                const auto restoreQuiet =
                    wil::scope_exit([this, previousQuiet]() noexcept { _quietUi = previousQuiet; });
                _quietUi = false;
                (void)SendMessageW(_window.get(), WM_CLOSE, 0, 0);
                expect(mode.route, "real-window-close-admitted-before-publication",
                       _window && _hostRequests.Size() != 0 && _session.IsMovingSelection() &&
                           _session.history.IsCurrent(sourceHistory));
            }
            const bool settled = settle();
            drainClose();
            expect(mode.route, "real-lifecycle-producer-settled",
                   settled && !_activeHostRequest && _hostRequests.Size() == 0 && _window);
            if (mode.answer == Prompts::Answer::Cancel)
            {
                expect(mode.route, "cancel-preserves-document-layers-live-outline-and-local-drag",
                       _session.Document() && _session.Document()->width == 40 && _session.Document()->height == 40 &&
                           _session.Document()->layers == sourceLayers && _session.ActiveLayerID() == sourceActive &&
                           _session.IsMovingSelection() && _session.SelectionMoveOpening() == sourceOpening &&
                           selectionMatches(15, 17, 35, 37) && std::holds_alternative<OutlineDrag>(_drag) &&
                           std::get<OutlineDrag>(_drag).start == Vec2{20, 20} &&
                           std::get<OutlineDrag>(_drag).offset == Vec2{5, 7});
                expect(mode.route, "cancel-preserves-history-generation-and-path",
                       _session.history.IsCurrent(sourceHistory) &&
                           _session.history.CurrentRevision() == sourceRevision &&
                           _session.Generation() == sourceGeneration && _session.history.UndoCount() == undoCount &&
                           _session.history.CanRedo() == canRedo && _session.history.UndoName() == undoName &&
                           _session.history.IsModified() == modified && _documentPath == sourcePath &&
                           pump.closePosts == 0 && !_closeConfirmed);
            }
            else
            {
                expect(mode.route, "outline-cancel-acknowledged-before-window-close",
                       _session.Document() && _session.Document()->width == 40 && _session.Document()->height == 40 &&
                           _session.Document()->layers == sourceLayers && _session.ActiveLayerID() == sourceActive &&
                           !_session.IsMovingSelection() && !_session.SelectionMoveOpening().has_value() &&
                           selectionMatches(10, 10, 30, 30) && std::holds_alternative<std::monostate>(_drag));
                expect(mode.route, "one-callback-safe-close-post-confirmed", pump.closePosts == 1 && _closeConfirmed);
            }
            if (std::string_view(mode.route).starts_with("end-session."))
                expect(mode.route, "end-session-pending-cleared", !_endSessionPending);
            else
                expect(mode.route, "close-window-keeps-end-session-idle", !_endSessionPending);
        }
    }

    CancelHostContinuations();
    if (!settle())
    {
        SelfTestFailure(L"Selection lifecycle outline final receipt did not settle.\n");
        std::_Exit(5);
    }
    drainClose();
    std::fprintf(stderr, "SUMMARY selection-lifecycle-outline-literal assertions=%d failures=%d modes=%zu\n",
                 assertions, failures, modes.size());
    std::fflush(stderr);
    return failures == 0 ? 0 : 5;
}

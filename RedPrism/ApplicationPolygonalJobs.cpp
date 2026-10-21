#include "Application.h"
#include "ChromeHost.h"
#include "resource.h"

#include <exception>

bool Application::PolygonalInputOwner(std::uint64_t opening) const noexcept
{
    const auto* source = _lassoInput.CapturedContext(opening);
    return _polygonalDrawingTestEnabled && source && source->kind == LassoKind::Polygonal;
}
HRESULT Application::StartPolygonalDrawing(std::uint64_t opening) noexcept
{
    const auto* source = _lassoInput.CapturedContext(opening);
    if (!PolygonalInputOwner(opening) || !_activeHostRequest || !source ||
        source->ordinal != _activeHostRequest->ordinal || !LassoInputCurrentOwner(opening) ||
        (_polygonalDrawing && _polygonalDrawing->controller))
        return E_ABORT;
    const auto plan = DescribePolygonalDrawing(_session, source->mode);
    if (!plan)
        return E_OUTOFMEMORY; // Eligible refusal has no synchronous hit/geometry fallback.
    _polygonalStopForHost = false;
    _polygonalDrawing.emplace(_jobs, _session, _lassoInput, opening, plan.value(), _polygonalDrawingAntialiased,
                              _polygonalTerminalOrdinal);
    auto& state = _polygonalDrawing.value();
    state.observer = this;
    state.notify = [](void* owner, PolygonalDrawingState::Event event) noexcept
    { static_cast<Application*>(owner)->PolygonalDrawingEvent(event); };
    state.hostObserver = this;
    state.mayReplay = [](void* owner, std::uint64_t ordinal) noexcept
    { return static_cast<Application*>(owner)->PolygonalMayReplay(ordinal); };
    state.mayFinish = [](void* owner, std::uint64_t ordinal, std::uint64_t intent, bool cancelled) noexcept
    { return static_cast<Application*>(owner)->PolygonalMayFinish(ordinal, intent, cancelled); };
    state.pressResolved = [](void* owner, std::uint64_t ordinal, std::uint64_t intent, bool closes) noexcept
    { static_cast<Application*>(owner)->PolygonalPressResolved(ordinal, intent, closes); };
    const auto result = PolygonalDrawingJob::Submit(state);
    if (SUCCEEDED(result))
        _hostStage = HostStage::PolygonalOpening; // Creator receipt waits only for acknowledged opening.
    return result;
}
bool Application::PolygonalMayReplay(std::uint64_t ordinal) const noexcept
{
    if (_shutdownStarted || !ordinal || !_polygonalDrawing || !_polygonalDrawing->controller)
        return false;
    const auto* front = _hostRequests.Front();
    if ((front && front->ordinal < ordinal) || (_canvasLifetime && _canvasLifetime->entry.ordinal < ordinal))
        return false;
    // A write reads committed document state. Own-key corners do not mutate it or its history.
    // Other selected older requests must settle/cancel this writer before younger input can replay.
    return !_activeHostRequest || _activeHostRequest->ordinal > ordinal ||
           std::holds_alternative<HostWriteInput>(_activeHostRequest->input.Input());
}
bool Application::PolygonalMayFinish(std::uint64_t ordinal, std::uint64_t intent, bool cancelled) const noexcept
{
    if (_shutdownStarted || !_polygonalDrawing)
        return false;
    if (cancelled && !ordinal)
        return true; // Fixed-ring overflow owns Lost; it cannot publish partial selection/history.
    const auto* canvas =
        _activeHostRequest ? std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input()) : nullptr;
    return _polygonalStamp && canvas && canvas->kind == HostCanvasInputKind::LassoFinish &&
           canvas->opening == _polygonalDrawing->opening && canvas->lassoClosingIntent == intent &&
           ordinal == (intent ? _polygonalDrawing->closingOrdinal : _polygonalTerminalOrdinal);
}
void Application::PolygonalPressResolved(std::uint64_t ordinal, std::uint64_t intent, bool closes) noexcept
{
    if (!_polygonalDrawing || !_polygonalDrawing->controller || !ordinal || !intent || _lassoReplayedClosingIntent)
        std::terminate(); // Ordinal yielding permits at most one unresolved selected provisional request.
    _lassoReplayedClosingIntent = intent;
    _lassoReplayedCloseChosen = closes;
    if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
HRESULT Application::StartPolygonalTerminal(std::uint64_t opening) noexcept
{
    const auto* canvas =
        _activeHostRequest ? std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input()) : nullptr;
    if (!_polygonalDrawing || _polygonalDrawing->opening != opening || !canvas ||
        canvas->kind != HostCanvasInputKind::LassoFinish || canvas->opening != opening ||
        !_polygonalDrawing->controller)
        return E_ABORT;
    _hostStage = HostStage::PolygonalTerminal;
    if (canvas->lassoClosingIntent)
    {
        if (!_lassoReplayedClosingIntent)
            return S_OK; // Older replay has not resolved this physical proximity/double-click yet.
        if (_lassoReplayedClosingIntent != canvas->lassoClosingIntent)
            return E_ABORT;
        if (!_lassoReplayedCloseChosen)
        {
            _lassoReplayedClosingIntent = 0;
            _lassoReplayedCloseChosen = false;
            FinishHostContinuation({HostOutcome::Succeeded, S_OK});
            return S_OK; // Only this provisional receipt completes; original drawing continues.
        }
    }
    if (!_polygonalStamp)
    {
        _polygonalStamp =
            _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
        if (!_polygonalStamp)
            return E_UNEXPECTED;
        _hostCommitStamp = _polygonalStamp;
        _hostAfterPrerequisite = HostStage::Complete;
        _lassoFinishQueued = true;
        _lassoFinishReceipt = _activeHostRequest->input.Receipt();
    }
    PumpPolygonalDrawing();
    return S_OK;
}
void Application::PumpPolygonalDrawing() noexcept
{
    if (!_polygonalDrawing || !_polygonalDrawing->controller || !_polygonalDrawing->waiting ||
        !_lassoInput.PendingCount())
        return;
    const auto next = _lassoInput.Peek(_polygonalDrawing->opening);
    if (!next)
        return;
    if (_polygonalDrawing->waitReason == PolygonalDrawingState::WaitReason::Ordinal &&
        !PolygonalMayReplay(next->sample.ordinal))
        return;
    if (_polygonalDrawing->waitReason == PolygonalDrawingState::WaitReason::Terminal &&
        !PolygonalMayFinish(_polygonalDrawing->closingOrdinal ? _polygonalDrawing->closingOrdinal
                                                              : _polygonalTerminalOrdinal,
                            _polygonalDrawing->closingIntent, next->action == HostLassoInput::Action::Lost))
        return;
    (void)_polygonalDrawing->controller->NotifyInput();
}
HRESULT Application::StartPolygonalHostCancel() noexcept
{
    if (!_polygonalDrawing || !_polygonalDrawing->controller)
        return S_FALSE;
    if (!_activeHostRequest || _polygonalStamp)
        return E_UNEXPECTED;
    _polygonalStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_polygonalStamp)
        return E_UNEXPECTED;
    _polygonalStopForHost = true;
    _hostCommitStamp = _polygonalStamp;
    _hostAfterPrerequisite = HostStage::Execute;
    CancelLassoInputOwner(_polygonalDrawing->opening, HostOutcome::Cancelled);
    return S_OK;
}
void Application::PolygonalDrawingEvent(PolygonalDrawingState::Event event) noexcept
{
    if (!_polygonalDrawing)
        return;
    const auto opening = _polygonalDrawing->opening;
    if (event == PolygonalDrawingState::Event::Opened)
    {
        _lassoAcquiredOpening = opening;
        _lassoSessionOpening = _session.LassoOpeningForWork();
        const auto* canvas =
            _activeHostRequest ? std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input()) : nullptr;
        if (canvas && canvas->kind == HostCanvasInputKind::LassoOpening && canvas->opening == opening)
            FinishHostContinuation({HostOutcome::Succeeded, S_OK});
    }
    else if (event == PolygonalDrawingState::Event::Terminal)
    {
        const auto result = _polygonalDrawing->outcome;
        const auto outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                             : result.status == JobStatus::Failed
                                 ? (result.error == E_ABORT ? HostOutcome::Stale : HostOutcome::Failed)
                                 : HostOutcome::Succeeded;
        if (_polygonalStamp)
        {
            const auto stamp = _polygonalStamp.value();
            (void)_historyWork.Finish(stamp);
            _polygonalStamp.reset();
            const bool stopped = _polygonalStopForHost && result.status == JobStatus::Cancelled;
            ResumeHostHistory(stamp, stopped ? HostOutcome::Succeeded : outcome, stopped ? S_OK : result.error);
        }
        else if (_hostStage == HostStage::PolygonalOpening || _hostStage == HostStage::PolygonalTerminal)
            FinishHostContinuation({outcome, result.error});
        _polygonalStopForHost = false;
        _lassoAcquiredOpening = 0;
        _lassoSessionOpening = 0;
        _lassoFinishQueued = false;
        _lassoReplayedClosingIntent = 0;
        _lassoReplayedCloseChosen = false;
        DropDrag<DraftDrag>();
    }
    if (!_shutdownStarted)
        SelectionChanged();
}

bool Application::PolygonalHostInputSupported(const HostRequestInput& input) const noexcept
{
    if (std::holds_alternative<HostWriteInput>(input) || std::holds_alternative<HostToolInput>(input) ||
        std::holds_alternative<HostNewInput>(input) || std::holds_alternative<HostCloseProjectInput>(input) ||
        std::holds_alternative<HostCloseWindowInput>(input) || std::holds_alternative<HostEndSessionInput>(input))
        return true;
    if (const auto* canvas = std::get_if<HostCanvasBrushInput>(&input))
        return canvas->kind == HostCanvasInputKind::LassoOpening || canvas->kind == HostCanvasInputKind::LassoFinish;
    const auto* command = std::get_if<HostCommandInput>(&input);
    return command &&
           (command->command == ChromeHost::kCommandZoomFit || command->command == ChromeHost::kCommandZoomActual ||
            command->command == ChromeHost::kCommandZoomIn || command->command == ChromeHost::kCommandZoomOut ||
            command->command == ChromeHost::kCommandAbout || command->command == ChromeHost::kCommandSampleRing ||
            command->command == ChromeHost::kCommandKeyboardShortcuts || command->command == ChromeHost::kCommandCopy ||
            command->command == ChromeHost::kCommandCopyMerged || command->command == ChromeHost::kCommandUndo ||
            command->command == ChromeHost::kCommandRedo || command->command == ChromeHost::kCommandDeselect ||
            ChromeHost::ToolForSelectCommand(command->command) != EditorTool::Count);
}
bool Application::PolygonalHostInputStops(const HostRequestInput& input) const noexcept
{
    if (std::holds_alternative<HostToolInput>(input) || std::holds_alternative<HostNewInput>(input) ||
        std::holds_alternative<HostCloseProjectInput>(input) || std::holds_alternative<HostCloseWindowInput>(input) ||
        std::holds_alternative<HostEndSessionInput>(input))
        return true;
    const auto* command = std::get_if<HostCommandInput>(&input);
    return command && (command->command == ChromeHost::kCommandUndo || command->command == ChromeHost::kCommandRedo ||
                       command->command == ChromeHost::kCommandDeselect ||
                       ChromeHost::ToolForSelectCommand(command->command) != EditorTool::Count);
}

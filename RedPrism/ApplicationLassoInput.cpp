#include "Application.h"
#include "HostLassoInput.h"
#include "resource.h"

#include <cmath>
#include <limits>

bool Application::LassoInputCurrentOwner(std::uint64_t opening) const noexcept
{
    const auto* captured = _lassoInput.CapturedContext(opening);
    return captured && _session.CaptureDocumentReadAuthority() == std::optional(captured->document) &&
           _session.ActiveLayerID() == captured->active && _session.CropFrame() == captured->crop &&
           _session.IsMaskSelected() == captured->mask;
}
void Application::CancelLassoInputOwner(std::uint64_t opening, HostOutcome outcome) noexcept
{
    if (!_lassoInput.CapturedContext(opening))
        return;
    if (PolygonalInputOwner(opening) && _polygonalDrawing && _polygonalDrawing->controller &&
        _polygonalDrawing->opening == opening)
    {
        _lassoCancelledOpening = opening;
        if (_lassoInput.Sampling())
        {
            // Seal at the physical cancel boundary. Never Drop/CancelLasso while original work can use its writer.
            _polygonalTerminalOrdinal = _historyWork.NextOrdinal().value_or(0);
            (void)_lassoInput.Finish(opening, _session.selectionAntialiased, true);
        }
        (void)_jobs.Cancel(_polygonalDrawing->original);
        if (_polygonalDrawing->waiting)
            (void)_jobs.Resume(_polygonalDrawing->original);
        return; // Original completion/join owns input and prepared-writer acknowledgement.
    }
    if (outcome == HostOutcome::Cancelled)
        _lassoCancelledOpening = opening;
    if (_lassoOpeningTicket)
        (void)_jobs.Cancel(_lassoOpeningTicket.value());
    if (_lassoFinishTicket)
        (void)_jobs.Cancel(_lassoFinishTicket.value());
    if (!_session.PendingLasso() ||
        (_lassoAcquiredOpening == opening && _session.LassoOpeningForWork() == _lassoSessionOpening))
        _session.CancelLasso();
    (void)_lassoInput.Drop(opening);
    _lassoAcquiredOpening = 0;
    _lassoSessionOpening = 0;
    _lassoFinishQueued = false;
    _lassoReplayedClosingIntent = 0;
    _lassoReplayedCloseChosen = false;
    DropDrag<DraftDrag>();
    if (outcome == HostOutcome::Failed && !_shutdownStarted)
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
}
bool Application::QueueLassoOpeningOwner(Vec2 pixel, SelectionMode mode, bool marquee) noexcept
{
    if (!_lassoDraftDisplayEnabled)
        return false;
    if (_lassoInput.Busy() || _lassoOpeningTicket || _lassoFinishTicket ||
        !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()) || !std::isfinite(pixel.x) ||
        !std::isfinite(pixel.y))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_BUSY)));
        return true; // Eligible refusal has no reference fallback.
    }
    if (_lassoInputOpening == (std::numeric_limits<std::uint64_t>::max)())
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)));
        return true;
    }
    const auto authority = _session.CaptureDocumentReadAuthority();
    if (!authority)
        return true;
    HostLassoInput::Context captured;
    captured.document = authority.value();
    captured.active = _session.ActiveLayerID();
    captured.crop = _session.CropFrame();
    captured.mask = _session.IsMaskSelected();
    captured.kind = marquee ? _session.marqueeKind : _session.lassoKind;
    captured.mode = mode;
    captured.viewEpoch = _lassoDraftViewEpoch ? _lassoDraftViewEpoch : 1;
    const auto opening = _lassoInputOpening + 1;
    _lassoOpeningReceipt = QueueHostInput(HostCanvasBrushInput{opening, HostCanvasInputKind::LassoOpening});
    if (_lassoOpeningReceipt.Read().outcome != HostOutcome::Pending)
    {
        ReportHostAdmission(_lassoOpeningReceipt);
        return true;
    }
    // Queue admission never dispatches the owner. Read this exact new fixed descriptor's real shared ordinal;
    // no source root, input array or geometry is captured by the request.
    for (std::size_t index = 0; index < _hostRequests.Size(); ++index)
    {
        const auto* entry = _hostRequests.PendingAt(index);
        const auto* canvas = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
        if (canvas && canvas->opening == opening && canvas->kind == HostCanvasInputKind::LassoOpening)
        {
            captured.ordinal = entry->ordinal;
            break;
        }
    }
    if (!_lassoInput.Begin(opening, captured, pixel))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_INVALIDARG));
        return true; // The queued descriptor observes no matching stream and fails before capture.
    }
    _lassoInputOpening = opening;
    if (_polygonalDrawingTestEnabled && captured.kind == LassoKind::Polygonal)
    {
        _polygonalDrawingAntialiased = _session.selectionAntialiased;
        _polygonalTerminalOrdinal = 0;
    }
    _lassoInputFirstPoint = pixel;
    _lassoAcquiredOpening = 0;
    _lassoSessionOpening = 0;
    _lassoFinishQueued = false;
    _lassoReplayedClosingIntent = 0;
    _lassoReplayedCloseChosen = false;
    return true;
}
std::uint64_t Application::NextLassoSampleOrdinalOwner() noexcept
{
    if (!_lassoInput.Sampling())
        return 0; // Late input cannot consume a counter or cancel the already selected physical terminal.
    const auto ordinal = _historyWork.NextOrdinal();
    if (!ordinal)
    {
        CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
        SelectionChanged();
        return 0; // Exact exhaustion drops the stream before a producer can accept this sample.
    }
    return ordinal.value();
}
void Application::RecordLassoInputOwner(HostLassoInput::Admission admission) noexcept
{
    if (admission == HostLassoInput::Admission::Closed)
        return; // An older selected terminal cannot accept a late hover/release sample.
    if (admission != HostLassoInput::Admission::Recorded)
    {
        if (_lassoInput.Busy())
            CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
        SelectionChanged();
        return;
    }
    if (!_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
void Application::RecordLassoPolygonPressOwner(Vec2 point, bool nearFirst, bool doubleClick, bool antialiased) noexcept
{
    if (!_lassoInput.Sampling())
        return; // Preserve an accepted terminal before either close-intent or shared-ordinal exhaustion.
    const auto opening = _lassoInput.Opening();
    if (!nearFirst && !doubleClick)
    {
        RecordLassoInputOwner(
            _lassoInput.PolygonPress(opening, point, false, false, antialiased, 0, NextLassoSampleOrdinalOwner()));
        return;
    }
    if (_lassoClosingIntent == (std::numeric_limits<std::uint64_t>::max)())
    {
        CancelLassoInputOwner(opening, HostOutcome::Failed);
        SelectionChanged();
        return;
    }
    const auto intent = _lassoClosingIntent + 1;
    const auto recorded = _lassoInput.PolygonPress(opening, point, nearFirst, doubleClick, antialiased, intent,
                                                   NextLassoSampleOrdinalOwner());
    RecordLassoInputOwner(recorded);
    if (recorded != HostLassoInput::Admission::Recorded)
        return;
    _lassoClosingIntent = intent;
    // Reserve the actual shared ordinal now. Replay may acknowledge a non-closing click without history,
    // but a chosen close cannot be overtaken by a physically younger command/save/tool input.
    const auto receipt = QueueHostInput(HostCanvasBrushInput{opening, HostCanvasInputKind::LassoFinish, intent});
    if (receipt.Read().outcome != HostOutcome::Pending)
    {
        CancelLassoInputOwner(opening, HostOutcome::Failed);
        SelectionChanged();
    }
}
bool Application::QueueLassoFinishOwner(bool antialiased) noexcept
{
    if (!_lassoInput.Busy())
        return false;
    if (PolygonalInputOwner(_lassoInput.Opening()))
    {
        if (!_lassoInput.Sampling())
            return true; // Preserve an older accepted conditional/explicit terminal and its AA.
        const auto ordinal = _historyWork.NextOrdinal();
        if (!ordinal)
        {
            CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
            return true;
        }
        const bool recorded =
            _polygonalDrawing && _polygonalDrawing->controller
                ? _polygonalDrawing->controller->Finish(antialiased, ordinal.value())
                : _lassoInput.Finish(_lassoInput.Opening(), antialiased) == HostLassoInput::Admission::Recorded;
        if (!recorded)
        {
            CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
            return true;
        }
        _polygonalTerminalOrdinal = ordinal.value();
        _lassoFinishReceipt =
            QueueHostInput(HostCanvasBrushInput{_lassoInput.Opening(), HostCanvasInputKind::LassoFinish});
        _lassoFinishQueued = _lassoFinishReceipt.Read().outcome == HostOutcome::Pending;
        if (!_lassoFinishQueued)
            CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
        return true;
    }
    const auto result = _lassoInput.Finish(_lassoInput.Opening(), antialiased);
    if (result == HostLassoInput::Admission::Closed)
        return true; // The original selected terminal already owns its ordinal and physical AA.
    if (result != HostLassoInput::Admission::Recorded)
    {
        CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
        SelectionChanged();
        return true;
    }
    _lassoFinishReceipt = QueueHostInput(HostCanvasBrushInput{_lassoInput.Opening(), HostCanvasInputKind::LassoFinish});
    _lassoFinishQueued = _lassoFinishReceipt.Read().outcome == HostOutcome::Pending;
    if (!_lassoFinishQueued)
    {
        CancelLassoInputOwner(_lassoInput.Opening(), HostOutcome::Failed);
        SelectionChanged();
    }
    return true;
}
bool Application::ReplayLassoInputOwner() noexcept
{
    if (_polygonalDrawing && _polygonalDrawing->controller && PolygonalInputOwner(_polygonalDrawing->opening))
        return false; // The original parent exclusively consumes this prefix.
    if (!_lassoInput.Busy() || _lassoAcquiredOpening != _lassoInput.Opening() || _lassoOpeningTicket ||
        _lassoFinishTicket || _lassoReplayedClosingIntent)
        return false;
    const auto opening = _lassoInput.Opening();
    const auto rearm = [&]() noexcept
    {
        if (!_shutdownStarted && (_lassoInput.Peek(opening) || _hostRequests.Size() != 0 || _canvasLifetime) &&
            !_jobs.RequestUiActionSource(_continuationAction))
            std::terminate();
    };
    if (!LassoInputCurrentOwner(opening))
    {
        CancelLassoInputOwner(opening, HostOutcome::Stale);
        SelectionChanged();
        rearm();
        return true;
    }
    const auto event = _lassoInput.Peek(opening);
    if (!event)
        return false; // An idle live gesture disarms until physical input; no polling wake-up.
    const auto* front = _hostRequests.Front();
    // Samples and commands use one nonwrapping event clock. A physically older queued request or canvas
    // lifetime wins before a later point/cursor/Backspace; fixed ring order still preserves older samples.
    // Zero is reserved for explicitly constructed component fixtures, never a physical Application producer.
    if (event->action == HostLassoInput::Action::Move && event->sample.ordinal != 0 &&
        ((front && front->ordinal < event->sample.ordinal) ||
         (_canvasLifetime && _canvasLifetime->entry.ordinal < event->sample.ordinal)))
        return false;
    if (event->action == HostLassoInput::Action::Lost)
    {
        const bool overflow = _lassoInput.Overflowed();
        CancelLassoInputOwner(opening, overflow ? HostOutcome::Failed : HostOutcome::Cancelled);
        SelectionChanged();
        rearm();
        return true;
    }
    if (event->action == HostLassoInput::Action::Up)
        return false; // Select its already accepted event-time descriptor after all older samples replay.
    const auto taken = _lassoInput.Take(opening);
    bool applied = taken && taken->action == HostLassoInput::Action::Down; // Opening already owns this first point.
    if (taken && taken->action == HostLassoInput::Action::Move)
    {
        const auto& sample = taken->sample;
        switch (sample.operation)
        {
        case HostLassoInput::Operation::Point:
            applied = _session.ExtendBoundedLasso(sample.point);
            break;
        case HostLassoInput::Operation::Cursor:
            applied =
                _session.MoveBoundedLassoCursor(sample.cursorPresent ? std::optional(sample.point) : std::nullopt);
            break;
        case HostLassoInput::Operation::Marquee:
            applied = _session.DragBoundedMarquee(sample.point, sample.square, sample.fromCenter);
            break;
        case HostLassoInput::Operation::Backspace:
            applied = _session.RemoveLastBoundedLassoPoint();
            break;
        case HostLassoInput::Operation::PolygonPress:
        {
            const bool close = sample.doubleClick || (sample.nearFirst && _session.PendingLasso() &&
                                                      _session.PendingLasso()->PointCount() >= 3);
            applied =
                close ? _lassoInput.SelectReplayedPolygonClose(opening) : _session.ExtendBoundedLasso(sample.point);
            if (applied && sample.closingIntent)
            {
                _lassoReplayedClosingIntent = sample.closingIntent;
                _lassoReplayedCloseChosen = close;
            }
            break;
        }
        case HostLassoInput::Operation::Finish:
            applied = false;
            break;
        }
    }
    if (!applied)
        CancelLassoInputOwner(opening, HostOutcome::Failed);
    else if (!_session.PendingLasso())
        CancelLassoInputOwner(opening, HostOutcome::Cancelled); // Backspace of the last corner ends that exact draft.
    SelectionChanged();
    rearm();     // Draining the last sample still selects an accepted command/lifetime; empty gestures stay idle.
    return true; // One bounded accepted operation per existing source dispatch.
}

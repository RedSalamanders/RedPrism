#include "Application.h"

#include "ChromeHost.h"

#include <cstdint>
#include <optional>
#include <variant>

bool Application::CanDeferCanvasBrush() const noexcept
{
    // First caller slice: ordinary Brush/Eraser behind opacity only. Other tools and preceding mutations retain
    // their existing admission route until their event-time settings and closure policies are migrated.
    if ((_tool != EditorTool::Brush && _tool != EditorTool::Eraser) || _canvasBrushInput.Busy() || PointerBusy() ||
        !_session.CanPaint() || !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return false;
    bool opacityPending = _opacityEdit.has_value();
    if (_activeHostRequest)
    {
        if (!std::holds_alternative<HostLayerOpacityInput>(_activeHostRequest->input.Input()))
            return false;
        opacityPending = true;
    }
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* pending = _hostRequests.PendingAt(i);
        if (!pending || !std::holds_alternative<HostLayerOpacityInput>(pending->input.Input()))
            return false;
        opacityPending = true;
    }
    return opacityPending;
}

bool Application::QueueCanvasBrush(const DxUi::PointerEvent& event, Vec2 view) noexcept
{
    if (!CanDeferCanvasBrush())
        return false;
    if (_canvasBrushOpening == UINT64_MAX)
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)));
        return true;
    }
    const CanvasBrushContext captured{
        _session.DocumentSerial(), _session.ActiveLayerID().value(), _tool, BrushKindFor(), _session.brushSettings,
        _session.IsMaskSelected(), _session.maskPaintWhite};
    const CanvasBrushSample down{_viewport.DocumentFromView(view, DocumentSize()), event.modifiers};
    const auto opening = ++_canvasBrushOpening;
    if (!_canvasBrushInput.Begin(opening, captured, down))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_UNEXPECTED));
        return true;
    }
    _canvasBrushLastSample = down;
    _canvasBrushReplayed = false;
    _canvasBrushCancelledOpening = 0;
    _canvasBrushSessionOpening.reset();
    _canvasBrushReceipt = QueueHostInput(HostCanvasBrushInput{opening});
    if (_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
        (void)_canvasBrushInput.Drop(opening);
    ReportHostAdmission(_canvasBrushReceipt);
    return true;
}

void Application::RecordCanvasBrush(const DxUi::PointerEvent& event, Vec2 view) noexcept
{
    const CanvasBrushSample sample{_viewport.DocumentFromView(view, DocumentSize()), event.modifiers};
    const auto opening = _canvasBrushInput.Opening();
    const auto admission = event.action == DxUi::PointerAction::Up ? _canvasBrushInput.End(opening, sample, false)
                                                                   : _canvasBrushInput.Move(opening, sample);
    if (admission == CanvasBrushInput::Admission::Recorded)
        _canvasBrushLastSample = sample;
    else if (admission == CanvasBrushInput::Admission::Overflow)
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
    if (admission == CanvasBrushInput::Admission::Recorded || admission == CanvasBrushInput::Admission::Overflow)
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

void Application::LoseCanvasBrush() noexcept
{
    if (_canvasBrushInput.End(_canvasBrushInput.Opening(), _canvasBrushLastSample, true) ==
        CanvasBrushInput::Admission::Recorded)
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

bool Application::CancelCanvasBrush() noexcept
{
    if (!_canvasBrushInput.Busy() || _canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
        return false;
    // Cancellation owns the same admitted ordinal. Do not mutate a replayed transaction in a key callback,
    // drop its context before the selected owner retires, or wait for a physical Up that may never arrive.
    _canvasBrushCancelledOpening = _canvasBrushInput.Opening();
    (void)_jobs.RequestUiActionSource(_continuationAction);
    return true;
}

void Application::SealCanvasBrushForBoundary() noexcept
{
    if (!_canvasBrushInput.Sampling())
        return;
    // The dependent input has been admitted. Finish only this accepted prefix; later physical events cannot
    // extend it or open a replacement stroke while its original ordinal still owns acknowledgement.
    LoseCanvasBrush();
    if (_window && GetCapture() == _window.get())
    {
        _releasingCapture = true;
        ReleaseCapture();
        _releasingCapture = false;
    }
}

void Application::RetireCanvasBrush(HostOutcome outcome, std::uint64_t opening) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    if (!captured)
        return;
    if (!_shutdownStarted && outcome != HostOutcome::Succeeded && _canvasBrushReplayed && _canvasBrushSessionOpening &&
        captured->documentSerial == _session.DocumentSerial() &&
        _session.PendingBrushOpening() == _canvasBrushSessionOpening)
    {
        _session.CancelBrush();
        _composition.EndStroke();
        _previewSource.reset();
        DocumentChanged();
    }
    if (!_canvasBrushInput.Acknowledge(opening))
        (void)_canvasBrushInput.Drop(opening);
    _canvasBrushReplayed = false;
    _canvasBrushSessionOpening.reset();
    if (_canvasBrushCancelledOpening == opening)
        _canvasBrushCancelledOpening = 0;
}

void Application::ReplayCanvasBrush() noexcept
{
    const auto* input =
        _activeHostRequest ? std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input()) : nullptr;
    const auto* captured = input ? _canvasBrushInput.CapturedContext(input->opening) : nullptr;
    if (input && captured && _canvasBrushCancelledOpening == input->opening)
    {
        FinishHostContinuation({HostOutcome::Cancelled, E_ABORT});
        return;
    }
    if (!captured || captured->documentSerial != _session.DocumentSerial() ||
        _session.ActiveLayerID() != captured->target || _session.IsMaskSelected() != captured->mask ||
        _tool != captured->tool || _opacityEdit)
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (_canvasBrushReplayed &&
        (!_canvasBrushSessionOpening || _session.PendingBrushOpening() != _canvasBrushSessionOpening))
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return; // An accepted old path must never append to a replacement core stroke.
    }
    const auto event = _canvasBrushInput.Take(input->opening);
    if (!event)
        return; // A live physical gesture is idle until its producer records another event.
    switch (event->action)
    {
    case CanvasBrushInput::Action::Down:
        if (PointerBusy())
        {
            FinishHostContinuation({HostOutcome::Stale, E_ABORT});
            return;
        }
        BeginBrushStrokeAt(event->sample.point, event->sample.modifiers, captured->kind, captured);
        _canvasBrushSessionOpening = _session.PendingBrushOpening();
        _canvasBrushReplayed = _canvasBrushSessionOpening.has_value();
        if (!_canvasBrushReplayed)
        {
            FinishHostContinuation({HostOutcome::Failed, E_FAIL});
            return;
        }
        break;
    case CanvasBrushInput::Action::Move:
        ContinueBrushStrokeAt(event->sample.point);
        if (!_session.PendingBrush())
        {
            FinishHostContinuation({HostOutcome::Failed, E_FAIL});
            return;
        }
        break;
    case CanvasBrushInput::Action::Up:
        ContinueBrushStrokeAt(event->sample.point);
        if (!_session.PendingBrush())
        {
            FinishHostContinuation({HostOutcome::Failed, E_FAIL});
            return;
        }
        FinishBrushStroke();
        break;
    case CanvasBrushInput::Action::Lost:
        EndPointerDrag(PointerEnd::Lost); // Brush retains only its accepted path, exactly as capture loss does.
        break;
    }
    if (_canvasBrushInput.TerminalConsumed())
    {
        FinishHostContinuation({_canvasBrushInput.Overflowed() ? HostOutcome::Failed : HostOutcome::Succeeded,
                                _canvasBrushInput.Overflowed() ? HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) : S_OK});
        return;
    }
    if (_canvasBrushInput.PendingCount() != 0)
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"

#include <cstdint>
#include <optional>
#include <variant>

EditorTool Application::CapturedCanvasTool() const noexcept
{
    EditorTool tool = _tool;
    if (_activeHostRequest)
        if (const auto* selected = std::get_if<HostToolInput>(&_activeHostRequest->input.Input()))
            tool = selected->tool;
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
        if (const auto* pending = _hostRequests.PendingAt(i))
            if (const auto* selected = std::get_if<HostToolInput>(&pending->input.Input()))
                tool = selected->tool;
    return tool;
}

bool Application::CanDeferCanvasBrush() const noexcept
{
    // A delayed canvas press may follow opacity or read-only sampling. Every other preceding mutation keeps
    // its own source/closure policy. The fixed context owns settings and an exact older color dependency.
    const auto tool = CapturedCanvasTool();
    const bool brush = tool == EditorTool::Brush || tool == EditorTool::Eraser;
    const bool colorTool = tool == EditorTool::Gradient || tool == EditorTool::Shape;
    if ((!brush && !colorTool) || SamplesColor(tool) || _canvasBrushInput.Busy() || PointerBusy() ||
        (tool != EditorTool::Shape && !_session.CanPaint()) || !_session.ActiveLayerID() ||
        !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return false;
    bool precedingInput = _opacityEdit.has_value();
    bool samplerPending = false;
    const auto accepts = [&samplerPending](const HostRequest& request) noexcept
    {
        if (std::holds_alternative<HostLayerOpacityInput>(request.Input()))
            return true;
        if (samplerPending && std::holds_alternative<HostToolInput>(request.Input()))
            return true;
        const auto* canvas = std::get_if<HostCanvasBrushInput>(&request.Input());
        if (canvas && canvas->kind == HostCanvasInputKind::Sampler)
        {
            samplerPending = true;
            return true;
        }
        return false;
    };
    if (_activeHostRequest)
    {
        if (!accepts(_activeHostRequest->input))
            return false;
        precedingInput = true;
    }
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* pending = _hostRequests.PendingAt(i);
        if (!pending || !accepts(pending->input))
            return false;
        precedingInput = true;
    }
    return precedingInput && (brush || samplerPending);
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
    CanvasBrushContext captured{
        _session.DocumentSerial(), _session.ActiveLayerID().value(), CapturedCanvasTool(),
        CapturedCanvasTool() == EditorTool::Eraser ? BrushKind::Erase : BrushKind::Paint, _session.brushSettings,
        _session.IsMaskSelected(), _session.maskPaintWhite};
    if (!captured.mask && captured.tool != EditorTool::Eraser)
        captured.sampledColor = CaptureSamplerColorDependency();
    captured.gradient = _session.gradientSettings;
    captured.foreground = _session.ForegroundColor();
    captured.background = _session.BackgroundColor();
    captured.shape = _session.shapeKind;
    captured.cornerRadius = _session.shapeCornerRadius;
    const CanvasBrushSample down{_viewport.DocumentFromView(view, DocumentSize()), event.modifiers,
                                 KeyboardState::Held(VK_MENU)};
    const auto opening = ++_canvasBrushOpening;
    if (!_canvasBrushInput.Begin(opening, captured, down))
    {
        if (captured.sampledColor)
            ReleaseSamplerColorDependency(captured.sampledColor.value());
        ReportHostAdmission(ContinuationReceipt::Failure(E_UNEXPECTED));
        return true;
    }
    _canvasBrushLastSample = down;
    _canvasBrushReplayed = false;
    _canvasBrushCancelledOpening = 0;
    _canvasBrushSessionOpening.reset();
    _canvasBrushReceipt = QueueHostInput(HostCanvasBrushInput{opening});
    if (_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
    {
        if (captured.sampledColor)
            ReleaseSamplerColorDependency(captured.sampledColor.value());
        (void)_canvasBrushInput.Drop(opening);
    }
    ReportHostAdmission(_canvasBrushReceipt);
    return true;
}

void Application::RecordCanvasBrush(const DxUi::PointerEvent& event, Vec2 view) noexcept
{
    const CanvasBrushSample sample{_viewport.DocumentFromView(view, DocumentSize()), event.modifiers,
                                   KeyboardState::Held(VK_MENU)};
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

void Application::LoseCanvasBrush(bool keepShape) noexcept
{
    auto terminal = _canvasBrushLastSample;
    terminal.keepShapeOnLoss = keepShape;
    if (_canvasBrushInput.End(_canvasBrushInput.Opening(), terminal, true) ==
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
    if (captured->sampledColor)
        ReleaseSamplerColorDependency(captured->sampledColor.value());
    if (!_shutdownStarted && outcome != HostOutcome::Succeeded && _canvasBrushReplayed &&
        captured->documentSerial == _session.DocumentSerial())
    {
        if (captured->tool == EditorTool::Gradient && _session.PendingGradient() &&
            std::optional(_session.PendingGradient()->opening) == _canvasBrushSessionOpening)
            CancelGradient();
        else if (captured->tool == EditorTool::Shape && _session.PendingShapeOpening() == _canvasBrushSessionOpening)
            _session.CancelShape();
    }
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
    const auto currentOpening = captured->tool == EditorTool::Gradient
                                    ? (_session.PendingGradient() ? std::optional(_session.PendingGradient()->opening)
                                                                  : std::nullopt)
                                : captured->tool == EditorTool::Shape ? _session.PendingShapeOpening()
                                                                     : _session.PendingBrushOpening();
    if (_canvasBrushReplayed && (!_canvasBrushSessionOpening || currentOpening != _canvasBrushSessionOpening))
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return; // An accepted old path must never append to a replacement core stroke.
    }
    const auto event = _canvasBrushInput.Take(input->opening);
    if (!event)
        return; // A live physical gesture is idle until its producer records another event.
    if (captured->tool == EditorTool::Gradient || captured->tool == EditorTool::Shape)
    {
        if (event->action == CanvasBrushInput::Action::Down)
        {
            if (PointerBusy())
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            _canvasResolvedForeground = captured->sampledColor ? ResolveSamplerColor(captured->sampledColor.value())
                                                              : captured->foreground;
            if (captured->tool == EditorTool::Gradient)
            {
                if (!_session.BeginDeferredGradient(event->sample.point,
                    {captured->gradient, _canvasResolvedForeground, captured->background, captured->maskWhite}))
                {
                    FinishHostContinuation({HostOutcome::Failed, E_FAIL});
                    return;
                }
                _canvasBrushSessionOpening = _session.PendingGradient()->opening;
            }
            else
            {
                _session.BeginShape(event->sample.point, captured->shape, captured->cornerRadius);
                _canvasBrushSessionOpening = _session.PendingShapeOpening();
            }
            _canvasBrushReplayed = _canvasBrushSessionOpening.has_value();
            if (!_canvasBrushReplayed)
            {
                FinishHostContinuation({HostOutcome::Failed, E_FAIL});
                return;
            }
        }
        else if (event->action != CanvasBrushInput::Action::Lost)
        {
            if (captured->tool == EditorTool::Gradient)
            {
                Vec2 point = event->sample.point;
                if ((event->sample.modifiers & MK_SHIFT) != 0)
                    point = SnappedGradientPoint(point, _session.PendingGradient()->start);
                _session.MoveGradient(std::nullopt, point);
            }
            else
                _session.DragShape(event->sample.point, (event->sample.modifiers & MK_SHIFT) != 0, event->sample.alt);
        }
        if (captured->tool == EditorTool::Gradient)
        {
            if (_canvasBrushInput.TerminalConsumed())
                _session.EndGradientDrag();
            RefreshGradientJobs();
            SyncStrokePreview();
        }
        else if (_canvasBrushInput.TerminalConsumed())
        {
            const auto draft = _session.PendingShape();
            _session.CancelShape();
            _canvasBrushSessionOpening.reset();
            if (draft && (event->action != CanvasBrushInput::Action::Lost || event->sample.keepShapeOnLoss))
            {
                const HRESULT error = StartShape(draft.value(), _canvasResolvedForeground);
                if (FAILED(error))
                {
                    FinishHostContinuation({HostOutcome::Failed, error});
                    return;
                }
                if (error == S_OK)
                {
                    _hostStage = HostStage::Complete; // The original ordinal stays held through worker apply/drop.
                    return;
                }
            }
        }
        SyncToolOverlays();
        _frameInvalidated = true;
        if (_canvasBrushInput.TerminalConsumed())
            FinishHostContinuation({_canvasBrushInput.Overflowed() ? HostOutcome::Failed : HostOutcome::Succeeded,
                _canvasBrushInput.Overflowed() ? HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) : S_OK});
        else if (_canvasBrushInput.PendingCount() != 0)
            (void)_jobs.RequestUiActionSource(_continuationAction);
        return;
    }
    switch (event->action)
    {
    case CanvasBrushInput::Action::Down:
        if (PointerBusy())
        {
            FinishHostContinuation({HostOutcome::Stale, E_ABORT});
            return;
        }
        {
            auto resolved = *captured;
            if (resolved.sampledColor)
            {
                const auto argb = ResolveSamplerColor(resolved.sampledColor.value());
                resolved.settings.red = static_cast<double>((argb >> 16) & 0xFFu) / 255.0;
                resolved.settings.green = static_cast<double>((argb >> 8) & 0xFFu) / 255.0;
                resolved.settings.blue = static_cast<double>(argb & 0xFFu) / 255.0;
            }
            BeginBrushStrokeAt(event->sample.point, event->sample.modifiers, captured->kind, &resolved);
        }
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

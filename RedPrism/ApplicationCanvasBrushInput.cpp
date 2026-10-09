#include "Application.h"

#include "ChromeHost.h"
#include "CloneReadbackTestGate.h"
#include "KeyboardState.h"

#include <cstdint>
#include <exception>
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
    const bool brush = tool == EditorTool::Brush || tool == EditorTool::Eraser ||
                       (tool == EditorTool::Heal && !_session.IsMaskSelected()) ||
                       (tool == EditorTool::Smear && (_session.smearMode == SmearMode::Blur ||
                                                      (_asyncWarpLiveEnabled && !_session.IsMaskSelected())));
    const bool clone = tool == EditorTool::Clone && !KeyboardState::Held(VK_MENU) && !_session.IsMaskSelected() &&
                       _session.cloneSource;
    const bool colorTool = tool == EditorTool::Gradient || tool == EditorTool::Shape;
    if ((!brush && !colorTool && !clone) || SamplesColor(tool) || _canvasBrushInput.Busy() || PointerBusy() ||
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
    return clone || brush || (precedingInput && samplerPending);
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
    CanvasBrushContext captured{_session.DocumentSerial(), _session.ActiveLayerID().value(), CapturedCanvasTool(),
                                BrushKind::Paint,          _session.brushSettings,           _session.IsMaskSelected(),
                                _session.maskPaintWhite};
    captured.kind = captured.tool == EditorTool::Eraser  ? BrushKind::Erase
                    : captured.tool == EditorTool::Heal  ? BrushKind::Heal
                    : captured.tool == EditorTool::Smear ? BrushKind::Blur
                    : captured.tool == EditorTool::Brush && _session.brushMode == BrushToolMode::Erase
                        ? BrushKind::Erase
                        : BrushKind::Paint;
    captured.healing = _session.spotHealingMode;
    captured.smear = _session.smearMode;
    if (!captured.mask && captured.kind == BrushKind::Paint && captured.tool != EditorTool::Clone)
        captured.sampledColor = CaptureSamplerColorDependency();
    captured.gradient = _session.gradientSettings;
    captured.foreground = _session.ForegroundColor();
    captured.background = _session.BackgroundColor();
    captured.shape = _session.shapeKind;
    captured.cornerRadius = _session.shapeCornerRadius;
    const CanvasBrushSample down{_viewport.DocumentFromView(view, DocumentSize()), event.modifiers,
                                 KeyboardState::Held(VK_MENU)};
    if (captured.tool == EditorTool::Clone)
    {
        captured.kind = BrushKind::Clone;
        captured.cloneAllLayers = _session.cloneSettings.sampleAllLayers;
        const auto start = (event.modifiers & MK_SHIFT) != 0 ? _session.ShiftLineStart() : std::nullopt;
        captured.cloneOffset = _session.CloneStrokeOffset(start.value_or(down.point));
        if (!captured.cloneOffset)
        {
            ReportHostAdmission(ContinuationReceipt::Failure(E_ABORT));
            return true;
        }
    }
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
    if (_canvasBrushInput.End(_canvasBrushInput.Opening(), terminal, true) == CanvasBrushInput::Admission::Recorded)
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

bool Application::CanvasBrushSealedStrokePending() const noexcept
{
    const auto opening = _canvasBrushInput.Opening();
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    return captured && captured->documentSerial == _session.DocumentSerial() && EditorToolIsBrush(captured->tool) &&
           !_canvasBrushInput.Sampling() && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
           _canvasBrushCancelledOpening != opening;
}

bool Application::CancelCanvasBrush(bool physicalOnly) noexcept
{
    if ((physicalOnly && CanvasBrushSealedStrokePending()) || !_canvasBrushInput.Busy() ||
        _canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
        return false;
    // Cancellation owns the same admitted ordinal. Do not mutate a replayed transaction in a key callback,
    // drop its context before the selected owner retires, or wait for a physical Up that may never arrive.
    _canvasBrushCancelledOpening = _canvasBrushInput.Opening();
    if (_strokeFinishCurrent)
    {
        // Withdraw future preview frames immediately. The original parent retains the graph/resources;
        // producer, command and last-frame acknowledgements still precede detachment and retirement.
        _acknowledgedStrokePreview = nullptr;
        InvalidateFrame();
    }
    const auto* captured = _canvasBrushInput.CapturedContext(_canvasBrushCancelledOpening);
    if (captured && _utilityTicket)
    {
        (void)_historyWork.RequestCancel();
        (void)_jobs.Cancel(_utilityTicket.value());
    }
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
    if (!_shutdownStarted && outcome != HostOutcome::Succeeded && _canvasBrushSessionOpening &&
        captured->documentSerial == _session.DocumentSerial() &&
        _session.PendingBrushOpening() == _canvasBrushSessionOpening)
    {
        _session.CancelBrush();
        _composition.EndStroke();
        _previewSource.reset();
        DocumentChanged();
    }
    if (captured->tool == EditorTool::Smear && captured->smear != SmearMode::Blur && _asyncWarpLiveEnabled &&
        _canvasBrushSessionOpening && !_shutdownStarted && _session.PendingWarpOpening() == _canvasBrushSessionOpening)
        std::terminate(); // receipt delivery must follow original joined draft/cache retirement
    if (!_canvasBrushInput.Acknowledge(opening))
        (void)_canvasBrushInput.Drop(opening);
    _canvasBrushReplayed = false;
    _canvasBrushSessionOpening.reset();
    if (!_shutdownStarted)
        _canvasCloneSample.reset(); // Shutdown retains this charge until the final worker retires the stroke.
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
    const bool warp =
        _asyncWarpLiveEnabled && captured->tool == EditorTool::Smear && captured->smear != SmearMode::Blur;
    const auto currentOpening =
        captured->tool == EditorTool::Gradient
            ? (_session.PendingGradient() ? std::optional(_session.PendingGradient()->opening) : std::nullopt)
        : captured->tool == EditorTool::Shape ? _session.PendingShapeOpening()
                                              : (warp ? _session.PendingWarpOpening() : _session.PendingBrushOpening());
    if (_canvasBrushReplayed && (!_canvasBrushSessionOpening || currentOpening != _canvasBrushSessionOpening))
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return; // An accepted old path must never append to a replacement core stroke.
    }
    if (warp && !_canvasBrushReplayed && !_canvasBrushSessionOpening)
    {
        if (!_asyncWarpLiveEnabled)
        {
            FinishHostContinuation({HostOutcome::Failed, E_UNEXPECTED});
            return;
        }
        const auto result = StartWarpLive(input->opening);
        if (FAILED(result))
            FinishHostContinuation({HostOutcome::Failed, result});
        return; // the original parent owns Down, opening, every preview and final publication
    }
    if (captured->kind == BrushKind::Clone && !_canvasBrushReplayed && !_canvasCloneSample)
    {
        const HRESULT result =
            captured->cloneAllLayers ? StartCloneReadback(input->opening) : StartCloneLayerSource(input->opening);
        if (FAILED(result))
            FinishHostContinuation({HostOutcome::Failed, result});
        return; // The Down event stays in the original FIFO until its complete source acknowledges.
    }
    if (!_canvasBrushReplayed && !_canvasBrushSessionOpening && captured->tool != EditorTool::Gradient &&
        captured->tool != EditorTool::Shape)
    {
        const HRESULT result = StartStrokeOpening(input->opening);
        if (FAILED(result))
            FinishHostContinuation({HostOutcome::Failed, result});
        return; // Opening owns the complete Down/Move/Up prefix until current preparation acknowledges.
    }
    if (_asyncStrokeSamplesEnabled && !warp && !_canvasBrushReplayed && _canvasBrushSessionOpening &&
        EditorToolIsBrush(captured->tool))
    {
        const auto result = StartStrokeSamples(input->opening);
        if (FAILED(result))
            FinishHostContinuation({HostOutcome::Failed, result});
        return; // Same physical ordinal; original processing parent consumes Down/Move/Up and finalizes.
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
            _canvasResolvedForeground =
                captured->sampledColor ? ResolveSamplerColor(captured->sampledColor.value()) : captured->foreground;
            if (captured->tool == EditorTool::Gradient)
            {
                if (!_session.BeginDeferredGradient(event->sample.point, {captured->gradient, _canvasResolvedForeground,
                                                                          captured->background, captured->maskWhite}))
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
        InvalidateFrame();
        if (_canvasBrushInput.TerminalConsumed())
            FinishHostContinuation(
                {_canvasBrushInput.Overflowed() ? HostOutcome::Failed : HostOutcome::Succeeded,
                 _canvasBrushInput.Overflowed() ? HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) : S_OK});
        else if (_canvasBrushInput.PendingCount() != 0)
            (void)_jobs.RequestUiActionSource(_continuationAction);
        return;
    }
    switch (event->action)
    {
    case CanvasBrushInput::Action::Down:
        if (!_canvasBrushSessionOpening || _session.PendingBrushOpening() != _canvasBrushSessionOpening)
        {
            FinishHostContinuation({HostOutcome::Stale, E_ABORT});
            return;
        }
        BeginPreparedBrushStrokeAt(event->sample.point, event->sample.modifiers);
        if (captured->kind == BrushKind::Clone)
            SyncToolOverlays(); // The captured Clone offset moves its source crosshair with this stroke.
        _canvasBrushSessionOpening = _session.PendingBrushOpening();
        _canvasBrushReplayed = _canvasBrushSessionOpening.has_value();
        if (!_canvasBrushReplayed)
        {
            FinishHostContinuation({HostOutcome::Failed, E_FAIL});
            return;
        }
        if (captured->kind == BrushKind::Clone && _cloneReadbackTestGate && _cloneReadbackTestGate->observeAdoption)
            (void)PostThreadMessageW(GetCurrentThreadId(), CloneReadbackTestGate::kMessage, 3, 0);
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
        if (_asyncStrokeFinishEnabled)
        {
            const HRESULT started = StartStrokeFinish(input->opening);
            if (FAILED(started))
                FinishHostContinuation({HostOutcome::Failed, started});
            return; // Original ordinal stays held through final worker/GPU/display/row retirement.
        }
        FinishBrushStroke();
        break;
    case CanvasBrushInput::Action::Lost:
        if (_asyncStrokeFinishEnabled)
        {
            const HRESULT started = StartStrokeFinish(input->opening);
            if (FAILED(started))
                FinishHostContinuation({HostOutcome::Failed, started});
            return;
        }
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
bool Application::RouteDeferredCanvasPan(const DxUi::PointerEvent& event, UINT message) noexcept
{
    const bool starts = !_canvasPanLast && message == WM_LBUTTONDOWN && _spacePan && _canvasBrushInput.Busy() &&
                        !_canvasBrushInput.Sampling() && _canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                        !(_canvasLifetime && _canvasLifetime->presenting) &&
                        std::holds_alternative<std::monostate>(_drag);
    if (!_canvasPanLast && !starts)
        return false;
    if (event.action == DxUi::PointerAction::Wheel)
        return false;
    const auto hole = _chrome->HolePixels();
    const Vec2 view{event.xPixels - hole.left, event.yPixels - hole.top};
    if (starts)
    {
        if (view.x < 0 || view.y < 0 || view.x >= hole.width || view.y >= hole.height)
            return false;
        _canvasPanLast = view;
        InvalidateFrame();
        return true;
    }
    if (event.action == DxUi::PointerAction::Move)
    {
        const Vec2 delta{view.x - _canvasPanLast->x, view.y - _canvasPanLast->y};
        _canvasPanLast = view;
        _viewport.Translate(delta);
        SyncView();
    }
    else if (message == WM_LBUTTONUP || event.action == DxUi::PointerAction::Leave)
        _canvasPanLast.reset();
    else if (event.action == DxUi::PointerAction::Down)
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
    return true; // This physical view gesture never appends samples to the older, sealed paint FIFO.
}

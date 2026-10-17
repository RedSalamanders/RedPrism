#include "Application.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "SelectionOutlineGestureJob.h"
#include "SelectionOutlineGestureMemory.h"
#include "resource.h"

#include <limits>
#include <memory>
#include <new>

bool Application::QueueSelectionPressOwner(Vec2 pixel, UINT modifiers) noexcept
{
    const auto kind = _session.marqueeKind;
    const auto mode = _session.SelectionModeFor((modifiers & MK_SHIFT) != 0, KeyboardState::Held(VK_MENU));
    if (!_selectionPressTestEnabled || _tool != EditorTool::Marquee ||
        (kind != LassoKind::Rectangle && kind != LassoKind::Ellipse) ||
        (mode != SelectionMode::New && mode != SelectionMode::Add && mode != SelectionMode::Subtract) ||
        (modifiers & MK_CONTROL) != 0 || KeyboardState::Held(VK_CONTROL))
        return false;
    if (_selectionPressInput.Busy() || (_selectionPressState && _selectionPressState->controller))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_BUSY)));
        return true;
    }
    const auto authority = _session.CaptureDocumentReadAuthority();
    if (!authority || _selectionPressOpening == (std::numeric_limits<std::uint64_t>::max)())
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)));
        return true;
    }
    const auto opening = _selectionPressOpening + 1;
    const auto receipt = QueueHostInput(HostCanvasBrushInput{opening, HostCanvasInputKind::SelectionPress});
    if (receipt.Read().outcome != HostOutcome::Pending)
    {
        ReportHostAdmission(receipt);
        return true;
    }
    HostSelectionPressInput::Context context;
    context.document = *authority;
    context.tool = HostSelectionPressInput::Tool::Marquee;
    context.kind = kind;
    context.mode = mode;
    // These are scalar event-time settings. Mutable source geometry/history is captured after admission.
    context.active = _session.ActiveLayerID();
    context.crop = _session.CropFrame();
    context.mask = _session.IsMaskSelected();
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* entry = _hostRequests.PendingAt(i);
        const auto* canvas = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
        if (canvas && canvas->kind == HostCanvasInputKind::SelectionPress && canvas->opening == opening)
        {
            context.ordinal = entry->ordinal;
            break;
        }
    }
    if (!_selectionPressInput.Begin(
            opening, context, pixel,
            {(modifiers & MK_SHIFT) != 0, (modifiers & MK_CONTROL) != 0, KeyboardState::Held(VK_MENU)},
            _session.selectionAntialiased))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_INVALIDARG));
        return true; // The exact queued descriptor fails without source capture.
    }
    _selectionPressOpening = opening;
    _selectionPressReceipt = receipt;
    return true;
}

bool Application::RouteSelectionPressOwner(const DxUi::PointerEvent& event, UINT message) noexcept
{
    if (!_selectionPressInput.Busy())
        return false;
    if (_selectionPressReceipt.Read().outcome != HostOutcome::Pending &&
        (!_selectionPressState || !_selectionPressState->controller))
    {
        (void)_selectionPressInput.Drop(_selectionPressInput.Opening());
        return false;
    }
    if (event.action == DxUi::PointerAction::Wheel)
        return false; // Subsequent physical samples are converted with their own current view.
    if (event.action == DxUi::PointerAction::Down || event.action == DxUi::PointerAction::Leave)
    {
        if (message == WM_LBUTTONDOWN || event.action == DxUi::PointerAction::Leave)
            LoseSelectionPressOwner();
        return true;
    }
    if (!_selectionPressInput.Sampling())
        return true;
    if (event.action != DxUi::PointerAction::Move && message != WM_LBUTTONUP)
        return true; // Right release cannot become the accepted left path's terminal.
    const auto ordinal = _historyWork.NextOrdinal();
    if (!ordinal)
    {
        LoseSelectionPressOwner(); // Reserved terminal reuses the last accepted ordinal, never wraps.
        return true;
    }
    const auto opening = _selectionPressInput.Opening();
    const auto hole = _chrome->HolePixels();
    const Vec2 view{event.xPixels - hole.left, event.yPixels - hole.top};
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    const auto admission =
        message == WM_LBUTTONUP
            ? _selectionPressInput.Finish(opening, _session.selectionAntialiased, *ordinal)
            : _selectionPressInput.Move(opening, pixel,
                                        {(event.modifiers & MK_SHIFT) != 0, (event.modifiers & MK_CONTROL) != 0,
                                         KeyboardState::Held(VK_MENU)},
                                        _session.selectionAntialiased, *ordinal);
    if (admission == HostSelectionPressInput::Admission::Invalid)
        (void)_selectionPressInput.SealLost(opening);
    if (!_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
    return true;
}
void Application::LoseSelectionPressOwner() noexcept
{
    if (!_selectionPressInput.Sampling())
        return;
    if (_selectionPressInput.SealLost(_selectionPressInput.Opening()) == HostSelectionPressInput::Admission::Recorded &&
        !_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
void Application::PumpSelectionPressOwner() noexcept
{
    if (_selectionPressState && _selectionPressState->controller && _selectionPressState->waiting &&
        _selectionPressInput.PendingCount())
        (void)_selectionPressState->controller->NotifyInput();
}
void Application::SelectionPressEventOwner(SelectionOutlineGestureState::Event event) noexcept
{
    if (event == SelectionOutlineGestureState::Event::Moved)
        ++_selectionPressMovePresentations;
    if (event == SelectionOutlineGestureState::Event::Terminal && _selectionPressState && _selectionPressStamp)
    {
        const auto stamp = *_selectionPressStamp;
        const auto result = _selectionPressState->outcome;
        const auto outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                             : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                   : HostOutcome::Succeeded;
        (void)_historyWork.Finish(stamp);
        _selectionPressStamp.reset();
        ResumeHostHistory(stamp, outcome, result.error);
    }
    if (!_shutdownStarted)
        SelectionChanged();
}
HRESULT Application::StartSelectionPressGesture(std::uint64_t opening) noexcept
{
    const auto* arrival = _selectionPressInput.CapturedContext(opening);
    const auto authority = _session.CaptureDocumentReadAuthority();
    if (!_selectionPressTestEnabled || !_activeHostRequest || !arrival || !authority ||
        arrival->document.documentSerial != authority->documentSerial ||
        arrival->document.sessionIdentity != authority->sessionIdentity ||
        arrival->ordinal != _activeHostRequest->ordinal || arrival->active != _session.ActiveLayerID() ||
        arrival->crop != _session.CropFrame() || arrival->mask != _session.IsMaskSelected() ||
        !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()) ||
        (_selectionPressState && _selectionPressState->controller))
        return E_ABORT;
    const auto memory = DescribeSelectionOutlineGestureMemory(_session, arrival->kind, arrival->mode);
    if (!memory)
        return E_OUTOFMEMORY;
    _selectionPressStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_selectionPressStamp)
        return E_UNEXPECTED;
    const auto limits = memory->limits;
    _selectionPressMovePresentations = 0;
    _selectionPressState.emplace(_jobs, _session, _selectionPressInput, opening, limits);
    _selectionPressState->admissionSource = memory->source;
    _selectionPressState->allowNewRectangle = arrival->kind == LassoKind::Rectangle;
    _selectionPressState->allowNewEllipse = arrival->kind == LassoKind::Ellipse;
    _selectionPressState->allowPreservingMarquee = arrival->mode != SelectionMode::New;
    _selectionPressState->requireCapturedTarget = true;
    _selectionPressState->observer = this;
    _selectionPressState->notify = [](void* raw, SelectionOutlineGestureState::Event event) noexcept
    { static_cast<Application*>(raw)->SelectionPressEventOwner(event); };
    HRESULT result;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*_selectionPressState);
        result = _jobs.Submit(job,
                              {.kind = JobClass::Selection,
                               .serialKey = _session.DocumentSerial(),
                               .memoryBytes = limits.totalBytes,
                               .deferredRetirement = true,
                               .allowMemoryWaiting = false,
                               .reserveRetainedMemory = true,
                               .continuingOutputLimit = limits.continuingOutputBytes},
                              _selectionPressState->original);
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        _selectionPressState->controller = nullptr;
        (void)_historyWork.Finish(*_selectionPressStamp);
        _hostFinishedStamp = _selectionPressStamp;
        _selectionPressStamp.reset();
        return result;
    }
    _hostCommitStamp = _selectionPressStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

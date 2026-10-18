#include "Application.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "SelectionOutlineGestureJob.h"
#include "SelectionOutlineGestureMemory.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>

HostPolygonalInputJournal::Sample Application::PolygonalPhysicalSample(const DxUi::PointerEvent& event, Vec2 view,
                                                                       std::uint64_t ordinal,
                                                                       bool doubleClick) const noexcept
{
    HostPolygonalInputJournal::Sample sample;
    sample.point = _viewport.DocumentFromView(view, DocumentSize());
    sample.modifiers = {(event.modifiers & MK_SHIFT) != 0, (event.modifiers & MK_CONTROL) != 0,
                        KeyboardState::Held(VK_MENU)};
    sample.ordinal = ordinal;
    sample.viewScale = _viewport.ViewScale();
    sample.antialiased = _session.selectionAntialiased;
    sample.doubleClick = doubleClick;
    sample.leftDown = (event.modifiers & MK_LBUTTON) != 0;
    sample.viewPoint = view;
    sample.documentOrigin = _viewport.DocumentRectOrigin(DocumentSize());
    sample.dipScale = (std::max)(1.0, _viewport.backingScale);
    return sample;
}

std::optional<std::uint64_t> Application::QueuePolygonalRecordOwner(HostCanvasInputKind kind,
                                                                    std::uint64_t record) noexcept
{
    const auto receipt = QueueHostInput(HostCanvasBrushInput{_polygonalPressFamily, kind, record});
    _polygonalPressLastReceipt = receipt;
    if (receipt.Read().outcome != HostOutcome::Pending)
    {
        ReportHostAdmission(receipt);
        return std::nullopt;
    }
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* entry = _hostRequests.PendingAt(i);
        const auto* input = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
        if (input && input->kind == kind && input->opening == _polygonalPressFamily &&
            input->lassoClosingIntent == record)
            return entry->ordinal;
    }
    std::terminate(); // Queue admission never dispatches inline; the exact immutable descriptor must exist.
}

bool Application::QueuePolygonalPressOwner(const DxUi::PointerEvent& event, UINT message, Vec2 view,
                                           POINT client) noexcept
{
    const bool existing = _polygonalPressInput.Busy();
    if (!_polygonalPressTestEnabled || message != WM_LBUTTONDOWN ||
        (!existing && (_tool != EditorTool::Lasso || _session.lassoKind != LassoKind::Polygonal)))
        return false;
    if (!existing)
    {
        if (_polygonalPressLastReceipt.Read().outcome == HostOutcome::Pending ||
            (_polygonalPressState && _polygonalPressState->controller))
        {
            ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_BUSY)));
            return true;
        }
        _polygonalPressFailed = false;
    }
    if (existing && _polygonalPressInput.Closed())
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_BUSY)));
        return true;
    }
    if (_polygonalPressFailed || (event.modifiers & MK_CONTROL) != 0 || KeyboardState::Held(VK_CONTROL))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)));
        return true; // Eligible unsupported lift has no synchronous containment fallback.
    }
    if (!_session.Document())
    {
        _polygonalPressLastReceipt = ContinuationReceipt::Failure(E_ABORT);
        LosePolygonalPressOwner();
        return true; // An empty canvas has no input source; do not misreport it as counter overflow.
    }
    const auto authority = _session.CaptureDocumentReadAuthority();
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!authority || _polygonalPressRecord == maximum || (!existing && _polygonalPressFamily == maximum))
    {
        _polygonalPressLastReceipt = ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
        ReportHostAdmission(_polygonalPressLastReceipt);
        LosePolygonalPressOwner();
        return true;
    }
    if (!existing)
        ++_polygonalPressFamily;
    const auto ordinal = QueuePolygonalRecordOwner(HostCanvasInputKind::PolygonalPress, ++_polygonalPressRecord);
    if (!ordinal)
    {
        LosePolygonalPressOwner();
        return true;
    }
    const auto now = GetTickCount64();
    const bool doubleClick = now - _lastPressTick <= GetDoubleClickTime() &&
                             std::abs(client.x - _lastPressPoint.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                             std::abs(client.y - _lastPressPoint.y) <= GetSystemMetrics(SM_CYDOUBLECLK);
    _lastPressTick = now;
    _lastPressPoint = client;
    auto sample = PolygonalPhysicalSample(event, view, *ordinal, doubleClick);
    sample.leftDown = true;
    HostPolygonalInputJournal::Context context;
    context.document = *authority;
    context.ordinal = *ordinal;
    context.tool = HostSelectionPressInput::Tool::Lasso;
    context.kind = LassoKind::Polygonal;
    context.mode = _session.SelectionModeFor(sample.modifiers.shift, sample.modifiers.alt);
    context.active = _session.ActiveLayerID();
    context.crop = _session.CropFrame();
    context.mask = _session.IsMaskSelected();
    const auto admission = existing ? _polygonalPressInput.Down(_polygonalPressFamily, context, sample)
                                    : (_polygonalPressInput.Begin(_polygonalPressFamily, context, sample)
                                           ? HostPolygonalInputJournal::Admission::Recorded
                                           : HostPolygonalInputJournal::Admission::Invalid);
    if (admission != HostPolygonalInputJournal::Admission::Recorded)
    {
        LosePolygonalPressOwner();
        return true; // Its accepted descriptor refuses without model capture if the raw record could not be stored.
    }
    _polygonalPressLeftDown = true;
    _polygonalPressLast = sample;
    if (!_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
    return true;
}

bool Application::RoutePolygonalPressOwner(const DxUi::PointerEvent& event, UINT message, POINT client) noexcept
{
    const bool busy = _polygonalPressInput.Busy();
    if (_shutdownStarted || !_chrome ||
        (!busy &&
         (!_polygonalPressTestEnabled || _tool != EditorTool::Lasso || _session.lassoKind != LassoKind::Polygonal)))
        return false;
    if (event.action == DxUi::PointerAction::Wheel)
        return false;
    if (!_polygonalPressLeftDown && (_spacePan || _canvasPanLast || std::holds_alternative<PanDrag>(_drag)))
        return false; // Temporary view panning belongs to its existing owner; the Polygonal draft stays parked.
    const auto hole = _chrome->HolePixels();
    const Vec2 view{event.xPixels - hole.left, event.yPixels - hole.top};
    const bool inside = view.x >= 0 && view.y >= 0 && view.x < hole.width && view.y < hole.height;
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
        return inside ||
               (busy && _polygonalPressLeftDown); // Right selection input never reaches synchronous lasso edits.
    if (!busy)
        return false;
    if (event.action == DxUi::PointerAction::Down)
        return message == WM_LBUTTONDOWN && inside ? QueuePolygonalPressOwner(event, message, view, client)
                                                   : _polygonalPressLeftDown;
    if (event.action != DxUi::PointerAction::Move && event.action != DxUi::PointerAction::Leave &&
        message != WM_LBUTTONUP)
        return false;
    const auto ordinal = _historyWork.NextOrdinal();
    if (!ordinal)
    {
        LosePolygonalPressOwner();
        return true;
    }
    auto sample = PolygonalPhysicalSample(event, view, *ordinal);
    const auto action = message == WM_LBUTTONUP ? HostPolygonalInputJournal::Action::Up
                        : event.action == DxUi::PointerAction::Leave || (!inside && !_polygonalPressLeftDown)
                            ? HostPolygonalInputJournal::Action::Leave
                            : HostPolygonalInputJournal::Action::Move;
    if (message == WM_LBUTTONUP)
    {
        sample.leftDown = false;
        _polygonalPressLeftDown = false;
    }
    const auto admission = _polygonalPressInput.Record(_polygonalPressFamily, action, sample);
    if (admission == HostPolygonalInputJournal::Admission::Recorded)
        _polygonalPressLast = sample;
    else if (admission != HostPolygonalInputJournal::Admission::Overflow &&
             admission != HostPolygonalInputJournal::Admission::Closed)
        LosePolygonalPressOwner();
    if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
    return inside || _polygonalPressLeftDown || message == WM_LBUTTONUP;
}

bool Application::QueuePolygonalKeyOwner(HostPolygonalInputJournal::Action action) noexcept
{
    if (!_polygonalPressInput.Busy())
        return false;
    if (_shutdownStarted)
        return true;
    if (_polygonalPressInput.Closed())
    {
        ReportHostAdmission(ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_BUSY)));
        return true;
    }
    if (action != HostPolygonalInputJournal::Action::Enter && action != HostPolygonalInputJournal::Action::Escape &&
        action != HostPolygonalInputJournal::Action::Backspace)
        return false;
    if (_polygonalPressRecord == (std::numeric_limits<std::uint64_t>::max)())
    {
        LosePolygonalPressOwner();
        return true;
    }
    const auto ordinal = QueuePolygonalRecordOwner(HostCanvasInputKind::PolygonalKey, ++_polygonalPressRecord);
    if (!ordinal)
    {
        LosePolygonalPressOwner();
        return true;
    }
    auto sample = _polygonalPressLast;
    sample.ordinal = *ordinal;
    sample.antialiased = _session.selectionAntialiased;
    const auto admission = _polygonalPressInput.Record(_polygonalPressFamily, action, sample);
    if (admission != HostPolygonalInputJournal::Admission::Recorded)
        LosePolygonalPressOwner();
    else if (action == HostPolygonalInputJournal::Action::Escape)
        (void)_polygonalPressInput.CloseInput(_polygonalPressFamily);
    if (!_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
    return true;
}

void Application::LosePolygonalPressOwner() noexcept
{
    if (!_polygonalPressInput.Busy())
        return;
    _polygonalPressLeftDown = false;
    if (_polygonalPressInput.Closed())
        return;
    (void)_polygonalPressInput.SealLost(_polygonalPressFamily);
    if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
namespace
{
const HostCanvasBrushInput* PolygonalCanvasInput(const HostRequestInput& input, std::uint64_t family) noexcept
{
    const auto* canvas = std::get_if<HostCanvasBrushInput>(&input);
    return canvas && canvas->opening == family &&
                   (canvas->kind == HostCanvasInputKind::PolygonalPress ||
                    canvas->kind == HostCanvasInputKind::PolygonalKey)
               ? canvas
               : nullptr;
}
} // namespace

HRESULT Application::PreparePolygonalRecordOwner(std::uint64_t ordinal) noexcept
{
    unsigned consumed = 0;
    while (const auto next = _polygonalPressInput.Peek(_polygonalPressFamily))
    {
        if (next->sample.ordinal >= ordinal)
            return next->sample.ordinal == ordinal ? S_OK : E_ABORT;
        if (next->action == HostPolygonalInputJournal::Action::Down)
            return E_ABORT; // An older Down cannot be skipped under a younger descriptor.
        (void)_polygonalPressInput.Take(_polygonalPressFamily); // Orphan hover/Up after the prior chosen terminal.
        if (++consumed == 16)
        {
            _hostStage = HostStage::PolygonalConditional;
            if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
                std::terminate();
            return S_FALSE; // Known bounded draining; no source capture/admission has occurred.
        }
    }
    return E_ABORT;
}

HRESULT Application::StartPolygonalPressOwner(std::uint64_t family) noexcept
{
    if (_shutdownStarted || !_polygonalPressTestEnabled || !_activeHostRequest ||
        !PolygonalCanvasInput(_activeHostRequest->input.Input(), family) || family != _polygonalPressInput.Stream())
        return E_ABORT;
    if (_polygonalPressFailed)
        return _polygonalPressFailure.error;
    if (_polygonalPressState && _polygonalPressState->controller)
        return StartPolygonalConditionalOwner(family);
    const auto prepared = PreparePolygonalRecordOwner(_activeHostRequest->ordinal);
    if (prepared != S_OK)
        return prepared;
    const auto first = _polygonalPressInput.Peek(family);
    if (!first || first->action != HostPolygonalInputJournal::Action::Down || !first->down ||
        first->sample.ordinal != _activeHostRequest->ordinal || first->down->ordinal != first->sample.ordinal)
        return E_ABORT;
    const auto& context = *first->down;
    if (context.document.documentSerial != _session.DocumentSerial() || context.active != _session.ActiveLayerID() ||
        context.crop != _session.CropFrame() || context.mask != _session.IsMaskSelected() ||
        !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return E_ABORT;
    const auto admitted = DescribeSelectionOutlineGestureMemory(_session, LassoKind::Polygonal, context.mode);
    if (!admitted)
        return E_OUTOFMEMORY;
    auto predecessor = _polygonalPressState ? _polygonalPressState->TakeSuccessor()
                                            : std::optional<SelectionOutlineGestureTransition>{};
    _polygonalPressStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_polygonalPressStamp)
        return E_UNEXPECTED;
    _polygonalPressDownOrdinal = context.ordinal;
    _polygonalPressState.emplace(_jobs, _session, _polygonalPressInput, family, admitted->limits);
    auto& state = *_polygonalPressState;
    state.admissionSource = admitted->source;
    state.drawingPlan = admitted->drawing;
    state.allowPreservingMarquee = context.mode != SelectionMode::New;
    state.requireCapturedTarget = true;
    if (predecessor)
        state.predecessor.emplace(std::move(*predecessor));
    state.observer = state.hostObserver = this;
    state.notify = [](void* raw, SelectionOutlineGestureState::Event event) noexcept
    { static_cast<Application*>(raw)->PolygonalPressEventOwner(event); };
    state.mayReplay = [](void* raw, std::uint64_t ordinal) noexcept
    { return static_cast<Application*>(raw)->PolygonalPressMayReplay(ordinal); };
    state.mayFinish = [](void* raw, std::uint64_t ordinal, bool cancelled) noexcept
    { return static_cast<Application*>(raw)->PolygonalPressMayFinish(ordinal, cancelled); };
    state.admitTerminal = [](void* raw, std::uint64_t ordinal, bool cancelled) noexcept
    { return static_cast<Application*>(raw)->AdmitPolygonalPressTerminal(ordinal, cancelled); };
    state.inputProgress = [](void* raw, std::uint64_t) noexcept
    {
        auto& owner = *static_cast<Application*>(raw);
        if (!owner._shutdownStarted && !owner._jobs.RequestUiActionSource(owner._continuationAction))
            std::terminate();
    };
    HRESULT result;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(state);
        result = _jobs.Submit(job,
                              {.kind = JobClass::Selection,
                               .serialKey = _session.DocumentSerial(),
                               .memoryBytes = admitted->limits.totalBytes,
                               .deferredRetirement = true,
                               .allowMemoryWaiting = false,
                               .reserveRetainedMemory = true,
                               .continuingOutputLimit = admitted->limits.continuingOutputBytes},
                              state.original);
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        state.controller = nullptr;
        (void)_historyWork.Finish(*_polygonalPressStamp);
        _polygonalPressStamp.reset();
        return result;
    }
    ++_polygonalPressParents;
    _hostCommitStamp = _polygonalPressStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

bool Application::PolygonalPressMayReplay(std::uint64_t ordinal) const noexcept
{
    if (_shutdownStarted || !_polygonalPressState || !_polygonalPressState->controller || !ordinal)
        return false;
    const auto* front = _hostRequests.Front();
    if ((front && front->ordinal < ordinal) || (_canvasLifetime && _canvasLifetime->entry.ordinal < ordinal))
        return false;
    if (!_activeHostRequest)
    {
        const auto next = _polygonalPressInput.Peek(_polygonalPressFamily);
        return next && next->sample.ordinal == ordinal && next->action != HostPolygonalInputJournal::Action::Down &&
               next->action != HostPolygonalInputJournal::Action::Enter &&
               next->action != HostPolygonalInputJournal::Action::Escape &&
               next->action != HostPolygonalInputJournal::Action::Backspace;
    }
    if (PolygonalCanvasInput(_activeHostRequest->input.Input(), _polygonalPressFamily))
        return ordinal <= _activeHostRequest->ordinal;
    return std::holds_alternative<HostWriteInput>(_activeHostRequest->input.Input()) ||
           _activeHostRequest->ordinal > ordinal;
}

bool Application::PolygonalPressMayFinish(std::uint64_t ordinal, bool cancelled) const noexcept
{
    if (_shutdownStarted || !_polygonalPressState || !_polygonalPressState->controller)
        return false;
    if (!_polygonalPressState->drawing)
        return _polygonalPressStamp && _historyWork.IsCurrent(*_polygonalPressStamp);
    const auto next = _polygonalPressInput.Peek(_polygonalPressFamily);
    if (cancelled && next && next->action == HostPolygonalInputJournal::Action::Lost && next->emergency)
        return true;
    return _activeHostRequest && _activeHostRequest->ordinal == ordinal &&
           PolygonalCanvasInput(_activeHostRequest->input.Input(), _polygonalPressFamily) &&
           (_polygonalPressStamp ? _historyWork.IsCurrent(*_polygonalPressStamp) : !_historyWork.Busy());
}

bool Application::AdmitPolygonalPressTerminal(std::uint64_t ordinal, bool cancelled) noexcept
{
    if (!PolygonalPressMayFinish(ordinal, cancelled))
        return false;
    if (_polygonalPressStamp)
        return _historyWork.IsCurrent(*_polygonalPressStamp);
    const auto next = _polygonalPressInput.Peek(_polygonalPressFamily);
    if (cancelled && next && next->action == HostPolygonalInputJournal::Action::Lost && next->emergency)
        return true; // Reserved loss cancels metadata; it cannot publish selection/history.
    _polygonalPressStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Selection);
    if (!_polygonalPressStamp)
        return false;
    _hostCommitStamp = _polygonalPressStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return true;
}

HRESULT Application::StartPolygonalConditionalOwner(std::uint64_t family) noexcept
{
    if (!_activeHostRequest || !PolygonalCanvasInput(_activeHostRequest->input.Input(), family) ||
        family != _polygonalPressInput.Stream())
        return E_ABORT;
    _hostStage = HostStage::PolygonalConditional;
    if (_polygonalPressFailed)
    {
        FinishHostContinuation(_polygonalPressFailure);
        return S_OK;
    }
    if (!_polygonalPressState)
        return E_ABORT;
    const auto ordinal = _activeHostRequest->ordinal;
    const auto* input = PolygonalCanvasInput(_activeHostRequest->input.Input(), family);
    if (!_polygonalPressState->controller)
    {
        if (input->kind == HostCanvasInputKind::PolygonalPress)
            return StartPolygonalPressOwner(family);
        if (ordinal > _polygonalPressState->lastConsumedOrdinal)
        {
            const auto prepared = PreparePolygonalRecordOwner(ordinal);
            if (prepared != S_OK)
                return prepared;
        }
        const auto next = _polygonalPressInput.Peek(family);
        if (next && next->sample.ordinal == ordinal && next->action != HostPolygonalInputJournal::Action::Down)
            (void)_polygonalPressInput.Take(family); // Exact younger terminal acknowledges the prior chosen close.
        else if (ordinal > _polygonalPressState->lastConsumedOrdinal)
            return E_ABORT;
        const auto result = _polygonalPressState->outcome;
        const auto outcome = result.status == JobStatus::Failed ? HostOutcome::Failed
                             : result.status == JobStatus::Cancelled && ordinal != _polygonalPressState->terminalOrdinal
                                 ? HostOutcome::Cancelled
                                 : HostOutcome::Succeeded;
        FinishHostContinuation({outcome, result.error});
        return S_OK;
    }
    if (_polygonalPressState->lastConsumedOrdinal >= ordinal && !_polygonalPressState->terminalOrdinal)
    {
        FinishHostContinuation({HostOutcome::Succeeded, S_OK}); // Only this corner or partial Backspace receipt.
        return S_OK;
    }
    if (_polygonalPressState->waiting)
        (void)_polygonalPressState->controller->NotifyInput();
    return S_OK;
}

HRESULT Application::StopPolygonalPressForHost() noexcept
{
    if (!_polygonalPressState || !_polygonalPressState->controller)
        return S_FALSE;
    if (!_activeHostRequest || _polygonalPressStamp)
        return E_UNEXPECTED;
    _polygonalPressStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_polygonalPressStamp)
        return E_UNEXPECTED;
    _polygonalPressStopping = true;
    _hostCommitStamp = _polygonalPressStamp;
    _hostAfterPrerequisite = HostStage::Execute;
    LosePolygonalPressOwner();
    (void)_jobs.Cancel(_polygonalPressState->original);
    if (_polygonalPressState->waiting)
        (void)_jobs.Resume(_polygonalPressState->original);
    return S_OK;
}

void Application::PolygonalPressEventOwner(SelectionOutlineGestureState::Event event) noexcept
{
    if (!_polygonalPressState)
        return;
    auto& state = *_polygonalPressState;
    if (event == SelectionOutlineGestureState::Event::Opened && state.drawing && _polygonalPressStamp)
    {
        const auto stamp = *_polygonalPressStamp;
        (void)_historyWork.Finish(stamp);
        _polygonalPressStamp.reset();
        ResumeHostHistory(stamp, HostOutcome::Succeeded, S_OK);
        if (_activeHostRequest && _activeHostRequest->ordinal == _polygonalPressDownOrdinal &&
            PolygonalCanvasInput(_activeHostRequest->input.Input(), _polygonalPressFamily))
            FinishHostContinuation({HostOutcome::Succeeded, S_OK});
    }
    else if (event == SelectionOutlineGestureState::Event::Terminal)
    {
        const auto result = state.outcome;
        auto outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                       : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                             : HostOutcome::Succeeded;
        const auto* input = _activeHostRequest
                                ? PolygonalCanvasInput(_activeHostRequest->input.Input(), _polygonalPressFamily)
                                : nullptr;
        if (input && input->kind == HostCanvasInputKind::PolygonalKey &&
            _activeHostRequest->ordinal == state.terminalOrdinal && result.status == JobStatus::Cancelled)
            outcome = HostOutcome::Succeeded; // The selected Escape succeeded by joining the cancelled writer.
        if (result.status == JobStatus::Failed)
        {
            _polygonalPressFailed = true;
            _polygonalPressFailure = {outcome, result.error};
            LosePolygonalPressOwner();
        }
        if (_polygonalPressStopping && result.status == JobStatus::Cancelled)
            outcome = HostOutcome::Succeeded;
        if (_polygonalPressStamp)
        {
            const auto stamp = *_polygonalPressStamp;
            (void)_historyWork.Finish(stamp);
            _polygonalPressStamp.reset();
            ResumeHostHistory(stamp, outcome,
                              _polygonalPressStopping && outcome == HostOutcome::Succeeded ? S_OK : result.error);
        }
        _polygonalPressStopping = false;
        if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
            std::terminate();
    }
    if (!_shutdownStarted)
        SelectionChanged();
}

void Application::PumpPolygonalPressOwner() noexcept
{
    if (_polygonalPressState && _polygonalPressState->controller && _polygonalPressState->waiting &&
        _polygonalPressInput.PendingCount())
        (void)_polygonalPressState->controller->NotifyInput();
    RetirePolygonalPressOwner();
}

void Application::RetirePolygonalPressOwner() noexcept
{
    if (!_polygonalPressInput.Busy() || (_polygonalPressState && _polygonalPressState->controller))
        return;
    const auto matches = [&](const HostRequestInput& input) noexcept
    { return PolygonalCanvasInput(input, _polygonalPressFamily) != nullptr; };
    if (_activeHostRequest && matches(_activeHostRequest->input.Input()))
        return;
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
        if (const auto* entry = _hostRequests.PendingAt(i); entry && matches(entry->input.Input()))
            return;
    unsigned drained = 0;
    while (const auto next = _polygonalPressInput.Peek(_polygonalPressFamily))
    {
        if (!_polygonalPressFailed && next->action == HostPolygonalInputJournal::Action::Down)
        {
            // No active/queued matching descriptor exists: TakeReady/Clear already retired its receipt.
            // Refuse and drain this stale scalar family instead of retaining an orphan forever.
            _polygonalPressFailed = true;
            _polygonalPressFailure = {HostOutcome::Stale, E_ABORT};
            LosePolygonalPressOwner();
        }
        (void)_polygonalPressInput.Take(_polygonalPressFamily);
        if (++drained == 16 && _polygonalPressInput.PendingCount())
        {
            if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
                std::terminate();
            return;
        }
    }
    if (!_polygonalPressInput.Closed())
        (void)_polygonalPressInput.CloseInput(_polygonalPressFamily);
    if (_polygonalPressState)
        (void)_polygonalPressState->TakeSuccessor(); // Retire unused scalar credit before ending its family.
    // Keep only the completed scalar diagnostics until the next parent; no source/history owners live here.
    if (!_polygonalPressInput.Finish(_polygonalPressFamily))
        std::terminate();
    _polygonalPressLast = {};
    _polygonalPressLeftDown = _polygonalPressFailed = false;
}

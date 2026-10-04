#include "Application.h"

#include <optional>
#include <utility>
#include <variant>

bool Application::CanvasLifetimeInput(const HostRequestInput& input) noexcept
{
    return std::holds_alternative<HostCloseProjectInput>(input) || std::holds_alternative<HostNewInput>(input) ||
           std::holds_alternative<HostCloseWindowInput>(input) || std::holds_alternative<HostEndSessionInput>(input);
}

bool Application::CanvasLifetimeCurrent() const noexcept
{
    if (!_canvasLifetime || _shutdownStarted || _shutdownDeferred ||
        _canvasLifetime->entry.documentSerial != _session.DocumentSerial() ||
        _canvasLifetime->brushOpening != _canvasBrushOpening)
        return false;
    if (const auto* ending = std::get_if<HostEndSessionInput>(&_canvasLifetime->entry.input.Input());
        ending && (!_endSessionPending || ending->generation != _endSessionGeneration))
        return false;
    if (_canvasBrushInput.Busy())
    {
        return _canvasBrushInput.Opening() == _canvasLifetime->brushOpening &&
               (!_canvasBrushReplayed ||
                (_canvasBrushSessionOpening && _session.PendingBrushOpening() == _canvasBrushSessionOpening));
    }
    return !_session.PendingBrushOpening(); // A replacement core opening cannot inherit the old decision.
}

void Application::FinishCanvasLifetimePreflight(ContinuationReceipt::Result result) noexcept
{
    if (!_canvasLifetime)
        return;
    if (_canvasLifetime->presenting)
    {
        _canvasLifetime->interrupted = result;
        return; // Output selection borrows this owner until its modal stack unwinds.
    }
    if (const auto* ending = std::get_if<HostEndSessionInput>(&_canvasLifetime->entry.input.Input());
        ending && ending->generation == _endSessionGeneration)
    {
        _endSessionPending = false;
        _endSessionPromptDeferred = false;
        if (_window)
            (void)ShutdownBlockReasonDestroy(_window.get());
    }
    (void)_canvasLifetime->entry.input.Finish(result.outcome, result.error);
    _canvasLifetime.reset();
    if (!_shutdownStarted && (_activeHostRequest || _hostRequests.Size() != 0))
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

void Application::RunCanvasLifetimePreflight() noexcept
{
    if (_canvasLifetime || !_canvasBrushInput.Busy() || _canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
        return;
    // Inspect fixed queue slots only. Never carry this borrowed pointer into either modal presenter.
    std::optional<std::uint64_t> ordinal;
    const auto serial = _session.DocumentSerial();
    for (std::size_t index = 0; index < _hostRequests.Size(); ++index)
    {
        const auto* pending = _hostRequests.PendingAt(index);
        if (pending && pending->documentSerial == serial && CanvasLifetimeInput(pending->input.Input()))
        {
            ordinal = pending->ordinal;
            break;
        }
    }
    if (!ordinal)
        return;
    auto detached = _hostRequests.DetachPending(serial, ordinal.value());
    if (!detached)
        return;
    _canvasLifetime.emplace(
        CanvasLifetimePreflight{std::move(detached.value()), _canvasBrushInput.Opening(), _canvasBrushReceipt});
    if (!CanvasLifetimeCurrent())
    {
        FinishCanvasLifetimePreflight({HostOutcome::Stale, E_ABORT});
        return;
    }
    _canvasLifetime->presenting = true;
    const auto answer = AskDiscardChanges();
    HRESULT output = S_OK;
    if (CanvasLifetimeCurrent() && _canvasLifetime->interrupted.outcome == HostOutcome::Pending &&
        answer == Prompts::Answer::First)
    {
        // This explicit owner is independent of the older selected opacity/brush request.
        output = SelectHostOutput(_canvasLifetime->entry.input, HostWriteFormat::Project, false);
    }
    _canvasLifetime->presenting = false;
    if (!CanvasLifetimeCurrent())
    {
        FinishCanvasLifetimePreflight({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (_canvasLifetime->interrupted.outcome != HostOutcome::Pending)
    {
        FinishCanvasLifetimePreflight(_canvasLifetime->interrupted);
        return;
    }
    if (answer == Prompts::Answer::Cancel || FAILED(output))
    {
        const auto result = answer == Prompts::Answer::Cancel ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : output;
        FinishCanvasLifetimePreflight(
            {result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? HostOutcome::Cancelled : HostOutcome::Failed, result});
        if (_canvasBrushInput.Sampling() && _window)
            (void)SetCapture(_window.get());
        return;
    }
    _canvasLifetime->save = answer == Prompts::Answer::First;
    SealCanvasBrushForBoundary();
    if (!_canvasLifetime->save)
        (void)CancelCanvasBrush();
    (void)_jobs.RequestUiActionSource(_continuationAction);
}

#include "Application.h"

#include "ChromeHost.h"
#include "resource.h"

#include <exception>

bool Application::HueInputCurrentOwner(std::uint64_t opening) const noexcept
{
    const auto* captured = _hueInput.CapturedContext(opening);
    const auto& panel = _session.PendingColorEdit();
    if (!captured || !panel || panel->kind != FilterKind::HueSaturation ||
        panel->openingSequence != captured->panelOpening || _session.ColorEditSequence() != _huePanelSequence ||
        !_session.IsDocumentReadCurrent(captured->document) || ColorCancelPending())
        return false;
    return captured->purpose == HostHueInput::Purpose::RangeSample ? panel->hueSample == std::optional(captured->mode)
                                                                   : panel->targeting && !panel->settings.hsv.colorize;
}
void Application::DropHueInputOwner(std::uint64_t opening) noexcept
{
    const auto* captured = _hueInput.CapturedContext(opening);
    if (!captured)
        return;
    const auto& panel = _session.PendingColorEdit();
    if (_hueAcquiredOpening == opening && panel && panel->openingSequence == captured->panelOpening)
        _session.EndHueTargeting();
    CancelHueReadbackOwner();
    (void)_hueInput.Drop(opening);
    _hueAcquiredOpening = 0;
    _hueCloseAfterPrefix = false;
    _huePanelSequence = 0;
    if (const auto* drag = std::get_if<HueTargetDrag>(&_drag); drag && drag->opening == std::optional(opening))
        _drag = PointerDrag{};
}
void Application::CompleteHueReadbackOwner(std::uint64_t opening, HistoryWorkStamp stamp, JobResult result,
                                           std::optional<double> hue) noexcept
{
    const bool current = !_shutdownStarted && _hueReadbackStamp == stamp && _historyWork.IsCurrent(stamp) &&
                         HueInputCurrentOwner(opening);
    const bool cancelled = result.status == JobStatus::Cancelled || _historyWork.CancelRequested(stamp);
    auto outcome = _historyWork.CancelRequested(stamp)  ? HostOutcome::Cancelled
                   : !current                           ? HostOutcome::Stale
                   : cancelled                          ? HostOutcome::Cancelled
                   : result.status == JobStatus::Failed ? HostOutcome::Failed
                                                        : HostOutcome::Succeeded;
    _hueReadbackTicket.reset(); // This exact completion is already joined; dropping slots must not recancel it.
    const auto* captured = _hueInput.CapturedContext(opening);
    if (outcome == HostOutcome::Succeeded && captured)
    {
        if (captured->purpose == HostHueInput::Purpose::RangeSample)
        {
            _session.SampleHueRangeValue(hue);
            _huePanelSequence = _session.ColorEditSequence();
            (void)_hueInput.Drop(opening); // One press has no pending physical drag to replay.
        }
        else if (_session.BeginHueTargetingValue(hue))
        {
            _huePanelSequence = _session.ColorEditSequence();
            _hueAcquiredOpening = opening;
            const auto down = _hueInput.Take(opening);
            if (!down || down->action != HostHueInput::Action::Down)
            {
                outcome = HostOutcome::Failed;
                DropHueInputOwner(opening);
            }
        }
        else
        {
            (void)_hueInput.Drop(opening); // Grey/transparent samples leave the panel unchanged.
            if (const auto* drag = std::get_if<HueTargetDrag>(&_drag); drag && drag->opening == std::optional(opening))
                _drag = PointerDrag{};
        }
        DocumentChanged();
    }
    else
    {
        DropHueInputOwner(opening);
    }
    _hueReadbackStamp.reset();
    (void)_historyWork.Finish(stamp);
    const auto error = outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale ? E_ABORT
                       : outcome == HostOutcome::Failed ? (FAILED(result.error) ? result.error : E_UNEXPECTED)
                                                        : S_OK;
    ResumeHostHistory(stamp, outcome, error);
    // This same continuation owns subsequent input, including idle-to-live physical moves. No new source/owner.
    if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
bool Application::ReplayHueInputOwner() noexcept
{
    if (!_hueInput.Busy() || _hueReadbackTicket || _hueAcquiredOpening != _hueInput.Opening())
        return false;
    const auto opening = _hueInput.Opening();
    const auto rearm = [&]() noexcept
    {
        if (!_shutdownStarted &&
            (_hueInput.Peek(opening) || _hueCloseAfterPrefix || _hostRequests.Size() || _canvasLifetime) &&
            !_jobs.RequestUiActionSource(_continuationAction))
            std::terminate();
    };
    if (!HueInputCurrentOwner(opening))
    {
        DropHueInputOwner(opening);
        rearm();
        return true;
    }
    const auto event = _hueInput.Peek(opening);
    if (!event)
    {
        if (!_hueCloseAfterPrefix)
            return false;           // A live idle gesture has no timer, poll or dummy worker.
        DropHueInputOwner(opening); // Every accepted sample joined; no invented/wrapped event ordinal.
        rearm();
        return true;
    }
    const auto* front = _hostRequests.Front();
    if ((front && front->ordinal < event->sample.ordinal) ||
        (_canvasLifetime && _canvasLifetime->entry.ordinal < event->sample.ordinal))
        return false;
    const auto accepted = _hueInput.Take(opening);
    if (!accepted)
        return false;
    if (accepted->action == HostHueInput::Action::Move)
    {
        _session.DragHueTargeting(accepted->sample.viewDelta, accepted->sample.adjustsHue);
        _huePanelSequence = _session.ColorEditSequence();
        DocumentChanged();
    }
    else if (accepted->action == HostHueInput::Action::Up || accepted->action == HostHueInput::Action::Lost)
    {
        // Legacy release/loss ends the drag and never applies a new release coordinate or modifier.
        _session.EndHueTargeting();
        (void)_hueInput.Acknowledge(opening);
        _hueAcquiredOpening = 0;
        _huePanelSequence = 0;
    }
    else
    {
        DropHueInputOwner(opening);
    }
    rearm();
    return true;
}

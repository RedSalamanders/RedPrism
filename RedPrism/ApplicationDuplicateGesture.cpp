#include "Application.h"

#include <limits>
#include <utility>

// The opening owns source settlement and duplication at one ordinal. Pointer metadata can coalesce while
// that source is held; release owns a separate event-time ordinal, never one invented by worker completion.
bool Application::StartDuplicateGesture(const TransformDrag& drag, Vec2 pixel, UINT modifiers, bool alt) noexcept
{
    const auto& pending = _session.PendingTransformEdit();
    const auto* layer = _session.ActiveLayer();
    if (_duplicateGesture || _transformRestoreOwner || !pending || pending->floating || pending->group ||
        _session.TransformsAsGroup() || !layer || layer->isGroup || !layer->asset ||
        _duplicateGestureSerial == (std::numeric_limits<std::uint64_t>::max)())
        return false; // Alt on a group or a non-image target keeps the ordinary move behavior.
    const auto serial = ++_duplicateGestureSerial;
    _duplicateGesture.emplace(DuplicateGesture{serial, _session.DocumentSerial(), pending->layerID,
                                               _session.TransformDraftRevision(), pending->opening, drag, pixel,
                                               modifiers, alt});
    const auto receipt =
        QueueHostInput(HostDuplicateBeginInput{pending->layerID, _session.TransformDraftRevision(), serial});
    ReportHostAdmission(receipt);
    if (receipt.Read().outcome != HostOutcome::Pending)
    {
        _duplicateGesture.reset();
        return false;
    }
    return true;
}

bool Application::FinishDuplicateGesture(bool cancel) noexcept
{
    if (!_duplicateGesture)
        return false;
    const auto serial = _duplicateGesture->serial;
    _duplicateGesture->pointerDown = false;
    if (_duplicateGesture->finishQueued && (!cancel || _duplicateGesture->finishCancel))
        return true;
    _duplicateGesture->finishQueued = true;
    _duplicateGesture->finishCancel = cancel;
    if (cancel)
        CancelHostContinuations();
    const auto receipt = QueueHostInput(HostDuplicateFinishInput{serial, cancel});
    ReportHostAdmission(receipt);
    if (receipt.Read().outcome != HostOutcome::Pending && _duplicateGesture && _duplicateGesture->serial == serial)
    {
        _duplicateGesture->abandoned = true;
        if (_transformWorkTicket)
        {
            (void)_historyWork.RequestCancel();
            (void)_jobs.Cancel(_transformWorkTicket.value());
        }
        else if (!_historyWork.Busy() && !_activeHostRequest && _hostRequests.Size() == 0)
        {
            if (RestoreTransformOpening())
                DocumentChanged();
            _duplicateGesture.reset();
        }
    }
    return true;
}

void Application::PublishDuplicateGesture() noexcept
{
    if (!_duplicateGesture || !_session.PendingTransformEdit() ||
        _duplicateGesture->documentSerial != _session.DocumentSerial())
        return;
    auto& gesture = _duplicateGesture.value();
    (void)_session.DeferTransform();
    gesture.copy = _session.PendingTransformEdit()->layerID;
    gesture.copyOpening = _session.PendingTransformEdit()->opening;
    // Source distortion is already baked by the older prerequisite; the fresh duplicate's pixel grid is the
    // drag reference. The original press and its latest document-space displacement remain unchanged.
    gesture.drag.original = _session.PendingTransformEdit()->draft;
    gesture.drag.originalCorners.reset();
    if (gesture.pointerDown && std::holds_alternative<MoveToolDrag>(_drag))
        _drag = MoveToolDrag{gesture.drag, false};
    ApplyTransformDragGeometry(gesture.drag, gesture.latest, gesture.modifiers, gesture.alt);
}

void Application::CompleteDuplicateGestureInput(ContinuationOutcome outcome) noexcept
{
    if (!_duplicateGesture || !_activeHostRequest)
        return;
    const auto& input = _activeHostRequest->input.Input();
    const auto* begin = std::get_if<HostDuplicateBeginInput>(&input);
    const auto* finish = std::get_if<HostDuplicateFinishInput>(&input);
    bool retire = _duplicateGesture->documentSerial != _session.DocumentSerial();
    if (begin && begin->gesture == _duplicateGesture->serial && outcome != HostOutcome::Succeeded)
        retire |= !_duplicateGesture->finishQueued;
    if (finish && finish->gesture == _duplicateGesture->serial)
        retire = true;
    if (_duplicateGesture->copy && !_session.PendingTransformEdit())
        retire = true; // A younger tool/history/write request already settled this exact opening.
    if (_duplicateGesture->abandoned && !_transformWorkTicket && !_historyWork.Busy())
    {
        if (RestoreTransformOpening())
            DocumentChanged();
        retire = true;
    }
    if (retire)
    {
        DropDrag<MoveToolDrag>();
        _duplicateGesture.reset();
    }
}

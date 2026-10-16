#include "Application.h"

#include "ChromeHost.h"
#include "resource.h"

#include <exception>

#include <cmath>
#include <limits>

bool Application::QueueHuePressOwner(Vec2 pixel, double viewX, HostHueInput::Purpose purpose,
                                     HueSampleMode mode) noexcept
{
    if (!_hueReadbackEnabled)
        return false;
    if (purpose > HostHueInput::Purpose::Targeting || mode > HueSampleMode::Remove)
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_INVALIDARG));
        return true; // Refuse invalid context before any descriptor, ordinal or producer opening is accepted.
    }
    const auto authority = _session.CaptureDocumentReadAuthority();
    const auto& panel = _session.PendingColorEdit();
    if (_shutdownStarted || !_hostSourceRegistered || !authority || !panel ||
        panel->kind != FilterKind::HueSaturation || _hueInput.Busy() || _hueReadbackTicket ||
        _hueInputOpening == (std::numeric_limits<std::uint64_t>::max)() || !std::isfinite(viewX) ||
        !std::isfinite(pixel.x) || !std::isfinite(pixel.y) || pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= authority->canvasWidth || pixel.y >= authority->canvasHeight)
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_ABORT));
        return true; // An enabled refusal performs no synchronous composite/readback fallback.
    }
    HostHueInput::Context context;
    context.document = authority.value();
    context.panelOpening = panel->openingSequence;
    context.documentPoint = pixel;
    context.startViewX = viewX;
    context.purpose = purpose;
    context.mode = mode;
    const auto opening = _hueInputOpening + 1;
    const auto receipt = QueueHostInput(HostCanvasBrushInput{opening, HostCanvasInputKind::HueSample});
    if (receipt.Read().outcome != HostOutcome::Pending)
    {
        ReportHostAdmission(receipt);
        return true;
    }
    // Every accepted descriptor consumes its opening even if producer binding later refuses. A later
    // valid press must never make that older descriptor match a new producer with equal scalar identity.
    _hueInputOpening = opening;
    for (std::size_t index = 0; index < _hostRequests.Size(); ++index)
    {
        const auto* entry = _hostRequests.PendingAt(index);
        const auto* input = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
        if (input && input->kind == HostCanvasInputKind::HueSample && input->opening == opening)
        {
            context.ordinal = entry->ordinal;
            break;
        }
    }
    if (!_hueInput.Begin(opening, context))
    {
        ReportHostAdmission(ContinuationReceipt::Failure(E_INVALIDARG));
        return true; // Its queued descriptor has no matching opening and refuses before source capture.
    }
    _huePanelSequence = _session.ColorEditSequence();
    _hueAcquiredOpening = 0;
    _hueCloseAfterPrefix = false;
    if (purpose == HostHueInput::Purpose::Targeting)
        _drag = HueTargetDrag{viewX, opening};
    SyncAdjustPanel();
    InvalidateFrame();
    return true;
}
void Application::RecordHueMoveOwner(std::uint64_t opening, double viewX, bool adjustsHue) noexcept
{
    if (!_hueInput.Sampling() || _hueInput.Opening() != opening)
        return;
    if (_hueCloseAfterPrefix)
        return;
    const auto ordinal = _historyWork.NextOrdinal();
    if (!ordinal)
    {
        // No younger writer can acquire an ordinal. Retain the accepted FIFO; its existing owner
        // route drains it before ending this exact drag, without inventing a wrapped terminal event.
        _hueCloseAfterPrefix = true;
        if (!_jobs.RequestUiActionSource(_continuationAction))
            std::terminate();
        return;
    }
    const auto admission = _hueInput.Move(opening, viewX, adjustsHue, ordinal.value());
    if (admission == HostHueInput::Admission::Invalid)
        return; // A nonfinite physical move changes neither the accepted prefix nor its last value.
    if (admission == HostHueInput::Admission::Overflow)
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
    if ((admission == HostHueInput::Admission::Recorded || admission == HostHueInput::Admission::Overflow) &&
        !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
void Application::SealHueInputOwner(std::uint64_t opening, bool lost) noexcept
{
    if (!_hueInput.Sampling() || _hueInput.Opening() != opening)
        return;
    const auto ordinal = _hueCloseAfterPrefix ? std::nullopt : _historyWork.NextOrdinal();
    if (!ordinal)
    {
        _hueCloseAfterPrefix = true;
        if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
            std::terminate();
        return;
    }
    const auto admission = _hueInput.Finish(opening, ordinal.value(), lost);
    if (admission == HostHueInput::Admission::Invalid)
        DropHueInputOwner(opening);
    if (!_shutdownStarted && !_jobs.RequestUiActionSource(_continuationAction))
        std::terminate();
}
void Application::SealHueForBoundaryOwner(std::uint64_t ordinal) noexcept
{
    if (!_hueInput.Sampling())
        return;
    const auto* context = _hueInput.CapturedContext(_hueInput.Opening());
    if (!context || context->purpose != HostHueInput::Purpose::Targeting)
        return;
    // The already accepted command is a physical barrier. Its same shared ordinal closes the older
    // gesture before that command; no invented later event may move an older drag behind the writer.
    const auto admission = _hueInput.Finish(_hueInput.Opening(), ordinal, true);
    if (admission == HostHueInput::Admission::Invalid)
        DropHueInputOwner(_hueInput.Opening());
    DropDrag<HueTargetDrag>();
}

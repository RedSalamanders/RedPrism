#include "Application.h"

#include "ChromeHost.h"
#include "resource.h"

#include <cmath>
#include <limits>

bool Application::CanQueueSampler(bool paletteInput) const noexcept
{
    if (_shutdownStarted || !_hostSourceRegistered || !_samplerState || !_session.Document())
        return false;
    if (_historyWork.Busy() && (!_samplerHostStamp || !_historyWork.IsCurrent(_samplerHostStamp.value())))
        return false;
    const auto accepts = [paletteInput](const HostRequest& request) noexcept
    {
        const auto* canvas = std::get_if<HostCanvasBrushInput>(&request.Input());
        return canvas && (canvas->kind == HostCanvasInputKind::Sampler || paletteInput);
    };
    if (_activeHostRequest)
    {
        const auto* canvas = std::get_if<HostCanvasBrushInput>(&_activeHostRequest->input.Input());
        if (!canvas || canvas->kind != HostCanvasInputKind::Sampler)
            return false; // A replayed writer owns its distinct palette/settings policy.
    }
    for (std::size_t i = 0; i < _hostRequests.Size(); ++i)
    {
        const auto* pending = _hostRequests.PendingAt(i);
        if (!pending || !accepts(pending->input))
            return false;
    }
    return true;
}

bool Application::CanChangePaletteDuringSampling() const noexcept
{
    if (!CanQueueSampler(true) || _opacityEdit || _gradientCommitTicket)
        return false;
    for (const auto& opening : _samplerState->openings)
        if (opening.opening != 0 && opening.receipt.Read().outcome == HostOutcome::Pending)
            return true;
    return false;
}

SamplerOpening* Application::FindSamplerOpening(std::uint64_t opening) noexcept
{
    if (_samplerState && opening != 0)
        for (auto& candidate : _samplerState->openings)
            if (candidate.opening == opening)
                return &candidate;
    return nullptr;
}

const SamplerOpening* Application::FindSamplerOpening(std::uint64_t opening) const noexcept
{
    if (_samplerState && opening != 0)
        for (const auto& candidate : _samplerState->openings)
            if (candidate.opening == opening)
                return &candidate;
    return nullptr;
}

void Application::SweepSamplerOpenings() noexcept
{
    if (!_samplerState)
        return;
    for (auto& opening : _samplerState->openings)
    {
        if (opening.opening == 0 || opening.receipt.Read().outcome == HostOutcome::Pending || opening.jobAlive)
            continue;
        RetireSamplerOpening(opening.opening);
        if (opening.consumers != 0)
            continue;
        opening.opening = 0;
        opening.picker.reset();
        opening.latestAcceptedIntent.reset();
        opening.resolvedIntent.reset();
        opening.ticket.reset();
        opening.receipt = {};
    }
}

std::optional<std::uint64_t> Application::BeginSamplerOpening(Vec2 pixel) noexcept
{
    if (!_samplerState || _shutdownStarted || !_hostSourceRegistered ||
        _samplerState->nextOpening == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    const auto source = _session.CaptureDocumentReadAuthority();
    if (!source)
        return std::nullopt;
    const auto picker = _chrome ? _chrome->CapturePickerIntentAuthority() : std::nullopt;
    if (_chrome && _chrome->ColorPickerOpen() && !picker)
        return std::nullopt;
    SweepSamplerOpenings();
    SamplerOpening* available = nullptr;
    for (auto& opening : _samplerState->openings)
        if (opening.opening == 0)
        {
            available = &opening;
            break;
        }
    if (!available)
        return std::nullopt;
    const auto opening = ++_samplerState->nextOpening;
    if (!available->points.Begin(opening, source->canvasWidth, source->canvasHeight))
        return std::nullopt;
    available->opening = opening;
    available->source = source.value();
    available->picker =
        picker ? std::optional(SamplerPickerBinding{picker->host, picker->opening, picker->target}) : std::nullopt;
    available->originalArgb = picker ? _chrome->PickerColor() : _session.ForegroundColor();
    available->resolvedArgb = available->originalArgb;
    available->latestAcceptedIntent.reset();
    available->resolvedIntent.reset();
    available->ticket.reset();
    available->consumers = 0;
    available->jobAlive = false;
    available->terminal = false;
    available->overflow = false;
    available->receipt = QueueHostInput(HostSamplerInput{opening, HostCanvasInputKind::Sampler});
    if (available->receipt.Read().outcome != HostOutcome::Pending)
    {
        RetireSamplerOpening(opening);
        SweepSamplerOpenings();
        return std::nullopt;
    }
    RecordSamplerPoint(opening, pixel);
    return opening;
}

void Application::RecordSamplerPoint(std::uint64_t openingId, Vec2 pixel) noexcept
{
    auto* opening = FindSamplerOpening(openingId);
    if (!opening || opening->terminal || opening->receipt.Read().outcome != HostOutcome::Pending)
        return;
    if (!_session.IsDocumentReadCurrent(opening->source))
    {
        SealSamplerOpening(openingId);
        return;
    }
    // Nonfinite/outside coordinates only move/hide the ring. Never cast an unbounded double to an integer.
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= opening->source.canvasWidth || pixel.y >= opening->source.canvasHeight)
        return;
    SamplerPointIntent intent;
    intent.paletteIntent = _session.CapturePaletteIntentAuthority().revision;
    intent.baselineArgb = _session.ForegroundColor();
    if (opening->picker)
    {
        const auto current = _chrome ? _chrome->CapturePickerIntentAuthority() : std::nullopt;
        const auto& target = opening->picker.value();
        if (!current || current->host != target.host || current->opening != target.opening ||
            current->target != target.target)
        {
            SealSamplerOpening(openingId);
            return; // This physical gesture must never adopt a replacement picker opening.
        }
        intent.pickerIntent = current->revision;
        intent.baselineArgb = _chrome->PickerColor();
    }
    else if (intent.paletteIntent == 0)
    {
        SealSamplerOpening(openingId);
        return;
    }
    const auto admitted = opening->points.Accept(openingId, static_cast<std::int32_t>(std::floor(pixel.x)),
                                                 static_cast<std::int32_t>(std::floor(pixel.y)), intent);
    if (admitted == SamplerIntentInput::Admission::Recorded || admitted == SamplerIntentInput::Admission::Coalesced)
    {
        opening->latestAcceptedIntent = intent;
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
    else if (admitted == SamplerIntentInput::Admission::Overflow)
    {
        opening->overflow = true; // Base FIFO seals its exact accepted prefix; the rejected point is not accepted.
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::SealSamplerOpening(std::uint64_t openingId) noexcept
{
    if (auto* opening = FindSamplerOpening(openingId))
    {
        (void)opening->points.Seal(openingId);
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::RetireSamplerOpening(std::uint64_t openingId) noexcept
{
    if (auto* opening = FindSamplerOpening(openingId))
    {
        (void)opening->points.Cancel(openingId);
        if (const auto active = opening->points.ActivePoint(openingId))
            (void)opening->points.Acknowledge(active->point.stamp, std::nullopt);
        opening->terminal = true;
        opening->ticket.reset();
    }
    if (const auto* sampling = std::get_if<ColorSampling>(&_drag); sampling && sampling->opening == openingId)
    {
        _drag = PointerDrag{}; // An asynchronous drag holds scalar ring metadata only.
        if (_window && GetCapture() == _window.get())
        {
            _releasingCapture = true;
            (void)ReleaseCapture();
            _releasingCapture = false;
        }
        _layoutDirty = true;
        InvalidateFrame();
    }
}

void Application::SealSamplerForBoundary() noexcept
{
    bool changed = false;
    if (_samplerState)
        for (auto& opening : _samplerState->openings)
            if (opening.opening != 0 && opening.receipt.Read().outcome == HostOutcome::Pending)
                changed = opening.points.Seal(opening.opening) || changed;
    if (const auto* sampling = std::get_if<ColorSampling>(&_drag); sampling && sampling->opening)
    {
        _drag = PointerDrag{};
        if (_window && GetCapture() == _window.get())
        {
            _releasingCapture = true;
            (void)ReleaseCapture();
            _releasingCapture = false;
        }
        _layoutDirty = true;
        InvalidateFrame();
    }
    if (changed)
        (void)_jobs.RequestUiActionSource(_continuationAction);
}

void Application::ReplaySamplerOpening(std::uint64_t openingId) noexcept
{
    auto* opening = FindSamplerOpening(openingId);
    if (_samplerHostTicket)
    {
        AdvanceSamplerReadback();
        return; // The selected input survives stale/cancel until the original job acknowledges.
    }
    if (!opening || !_session.IsDocumentReadCurrent(opening->source))
    {
        RetireSamplerOpening(openingId);
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (opening->points.IsFinished(openingId))
    {
        RetireSamplerOpening(openingId);
        FinishHostContinuation({opening->overflow ? HostOutcome::Failed : HostOutcome::Succeeded,
                                opening->overflow ? HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) : S_OK});
        return;
    }
    _hostStage = HostStage::Sampler;
    if (opening->points.HasNoWork(openingId))
        return; // Wait for actual in-bounds input or a terminal; no job/capture is needed yet.
    const HRESULT error = StartSamplerReadback(openingId);
    if (FAILED(error))
    {
        RetireSamplerOpening(openingId);
        FinishHostContinuation({HostOutcome::Failed, error});
    }
}

std::optional<SamplerColorDependency> Application::CaptureSamplerColorDependency() noexcept
{
    if (!_samplerState)
        return std::nullopt;
    const auto intent = _session.CapturePaletteIntentAuthority();
    SamplerOpening* newest = nullptr;
    for (auto& opening : _samplerState->openings)
        if (opening.opening != 0 && !opening.picker && !opening.terminal &&
            opening.receipt.Read().outcome == HostOutcome::Pending && opening.latestAcceptedIntent &&
            opening.source.sessionIdentity == intent.session &&
            opening.source.documentSerial == _session.DocumentSerial() &&
            opening.latestAcceptedIntent->paletteIntent == intent.revision &&
            (!newest || newest->opening < opening.opening))
            newest = &opening;
    if (!newest || newest->consumers == (std::numeric_limits<std::uint32_t>::max)())
        return std::nullopt;
    ++newest->consumers; // Scalar lease before captured-input admission; every refused/retired consumer releases it.
    return SamplerColorDependency{newest->opening, intent, _session.ForegroundColor()};
}

std::uint32_t Application::ResolveSamplerColor(const SamplerColorDependency& dependency) const noexcept
{
    const auto* opening = FindSamplerOpening(dependency.opening);
    return opening && opening->source.sessionIdentity == dependency.intent.session &&
                   opening->source.documentSerial == _session.DocumentSerial() && opening->resolvedIntent &&
                   opening->resolvedIntent->paletteIntent == dependency.intent.revision
               ? opening->resolvedArgb
               : dependency.fallbackArgb;
}

void Application::ReleaseSamplerColorDependency(const SamplerColorDependency& dependency) noexcept
{
    if (auto* opening = FindSamplerOpening(dependency.opening); opening && opening->consumers != 0)
        --opening->consumers;
    SweepSamplerOpenings();
}

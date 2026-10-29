#include "Application.h"

#include "ChromeHost.h"
#include "RecordedDocumentReadbackJob.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct SamplerReadbackPayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> memory; // Original complete charge dies after pixel/document owners.
    CanvasDocument document;
    PixelBuffer pixels;
    Raster image;
    Raster completedImage; // The base transfer destinations above remain empty through completion.

    SamplerReadbackPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<SamplerReadbackPayload> owned(static_cast<SamplerReadbackPayload*>(node)); };
    }
};
#pragma warning(pop)

#pragma warning(pop)
struct RetireSamplerPayload final
{
    void operator()(SamplerReadbackPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
    }
};
bool AddSamplerBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (bytes > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += bytes;
    return true;
}
bool SameSamplerIntent(const SamplerPointIntent& left, const SamplerPointIntent& right) noexcept
{
    return left.paletteIntent == right.paletteIntent && left.pickerIntent == right.pickerIntent;
}
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Application::SamplerHostJob final : public RecordedDocumentReadbackJob
{
  public:
    SamplerHostJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, SamplerIntentPoint point,
                   D3D11_BOX region, UtilityReadbackAdmission::Envelope envelope,
                   GpuResidencyReservation reservation) noexcept
        : RecordedDocumentReadbackJob(owner._jobs, owner._gpuWork, owner._recordedCommands, owner._utilityReadback,
                                      owner._graphGpuBudget, envelope, std::move(reservation), region,
                                      owner._renderer.IsWarp()),
          _owner(owner), _stamp(stamp), _opening(opening), _point(point), _retirement(envelope.cpuBytes)
    {
    }
    ~SamplerHostJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->document, _payload->pixels, _payload->image, _payload->memory);
        if (_owner._samplerHostJob == this)
            _owner._samplerHostJob = nullptr;
        if (auto* opening = _owner.FindSamplerOpening(_opening))
            opening->jobAlive = false;
        _owner.SweepSamplerOpenings(); // Fixed scalar storage only; no document/pixel owner is held here.
    }

    // Owner-only, called after the preceding worker/GPU lease has acknowledged. Idle input causes no resume.
    void Advance() noexcept
    {
        if (!_waiting)
            return;
        auto* opening = _owner.FindSamplerOpening(_opening);
        if (!Current() || !opening || _owner._historyWork.CancelRequested(_stamp))
        {
            (void)_owner._jobs.Cancel(_waiting.value());
            if (ResumeDetachedReadback(_waiting.value()))
                _waiting.reset();
            return;
        }
        if (const auto next = opening->points.SelectNext(_opening))
        {
            _point = next.value();
            if (ContinuePixelReadback(_waiting.value(), static_cast<unsigned int>(_point.point.x),
                                      static_cast<unsigned int>(_point.point.y)))
            {
                _waiting.reset();
                return;
            }
            // A failed resume changed no base request. Finish that exact suspended record with a local error.
            _terminal = true;
            _terminalError = E_UNEXPECTED;
        }
        else if (opening->points.IsFinished(_opening))
        {
            _terminal = true;
        }
        else
        {
            return; // The next real input or terminal owns the wake; no timer, polling or dummy worker.
        }
        if (ResumeDetachedReadback(_waiting.value()))
            _waiting.reset();
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* opening = _owner.FindSamplerOpening(_opening);
        return !_owner._shutdownStarted && opening && _owner._samplerHostStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.IsDocumentReadCurrent(opening->source);
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        document = _owner._session.Document().value(); // Original admitted committed root, once.
        if (_owner._samplerSourceCapturesForTest)
            ++*_owner._samplerSourceCapturesForTest;
        return S_OK;
    }
    JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext&) override
    {
        if (!_payload)
        {
            std::unique_ptr<SamplerReadbackPayload, RetireSamplerPayload> payload(
                std::make_unique<SamplerReadbackPayload>().release());
            payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload = std::move(payload); // Worker allocation under the complete original admission.
        }
        return {};
    }
    bool KeepReadbackGraphForPixelContinuation() const noexcept override
    {
        return true;
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (_terminal)
            return FAILED(_terminalError) ? JobResult{JobStatus::Failed, _terminalError} : JobResult{};
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!image.HasStorage() || image.Width() != 1 || image.Height() != 1 ||
            image.Format() != RasterFormat::Bgra8888)
            return {JobStatus::Failed, E_UNEXPECTED};
        _opaque = DocumentSession::SampleCompositeColor(image, {0, 0}); // Unchanged straight-sRGB formula.
        return {};
    }
    bool DeferDetachedCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_terminal)
            return false;
        auto* opening = _owner.FindSamplerOpening(_opening);
        if (!Current() || !opening || _owner._historyWork.CancelRequested(_stamp))
        {
            (void)_owner._jobs.Cancel(ticket);
            (void)ResumeDetachedReadback(ticket);
            return true;
        }
        const auto active = opening->points.ActivePoint(_opening);
        if (!active || active->point.stamp.opening != _point.point.stamp.opening ||
            active->point.stamp.sequence != _point.point.stamp.sequence)
        {
            _terminal = true;
            _terminalError = E_UNEXPECTED;
            (void)ResumeDetachedReadback(ticket);
            return true;
        }
        _owner.PublishSamplerPoint(_opening, _point, _opaque);
        if (!opening->points.Acknowledge(_point.point.stamp, _opaque))
        {
            _terminal = true;
            _terminalError = E_UNEXPECTED;
            (void)ResumeDetachedReadback(ticket);
            return true;
        }
        if (_owner._samplerAcknowledgedForTest)
            (void)SetEvent(_owner._samplerAcknowledgedForTest);
        if (opening->points.IsFinished(_opening))
            return false;
        _waiting = ticket;
        Advance();
        return true;
    }
    void ResetReadback() noexcept override
    {
        _opaque.reset(); // Only the worker retires previous output; a point change does not reset restart allowance.
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        if (_payload)
            _payload->completedImage = std::move(image);
        const bool current = Current();
        HostOutcome outcome = !current                                ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed    ? HostOutcome::Failed
                                                                      : HostOutcome::Succeeded;
        if (_owner._historyWork.CancelRequested(_stamp))
            outcome = HostOutcome::Cancelled;
        const auto* opening = _owner.FindSamplerOpening(_opening);
        const bool overflow = opening && opening->overflow && outcome == HostOutcome::Succeeded;
        if (overflow)
            outcome = HostOutcome::Failed;
        const auto error = outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale ? E_ABORT
                           : overflow ? HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)
                                      : result.error;
        if (outcome == HostOutcome::Failed && !overflow && !_owner._shutdownStarted)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_READBACK);
        _owner.RetireSamplerOpening(_opening);
        _owner._samplerHostTicket.reset();
        _owner._samplerHostStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    SamplerIntentPoint _point; // Replaced only by owner after worker acknowledgement, before an exact resume.
    const std::uint64_t _retirement;
    std::optional<JobTicket> _waiting;
    std::optional<std::uint32_t> _opaque; // Worker result, read only after that worker has joined.
    bool _terminal = false;
    HRESULT _terminalError = S_OK;
    std::unique_ptr<SamplerReadbackPayload, RetireSamplerPayload> _payload;
};
#pragma warning(pop)

HRESULT Application::StartSamplerReadback(std::uint64_t openingId) noexcept
{
    auto* opening = FindSamplerOpening(openingId);
    if (!opening || _samplerHostTicket || !_session.IsDocumentReadCurrent(opening->source))
        return E_ABORT;
    const auto point = opening->points.SelectNext(openingId);
    if (!point)
        return S_FALSE;
    const auto x = static_cast<unsigned int>(point->point.x);
    const auto y = static_cast<unsigned int>(point->point.y);
    const D3D11_BOX region{x, y, 0, x + 1, y + 1, 1};
    // Complete immutable-source/private-graph/scratch admission precedes capture. The sampler's
    // additional bound includes its full control lifetime and reusable 1x1 output.
    auto memory = RecordedDocumentReadbackJob::RequiredMemory(region);
    const auto metadata = sizeof(SamplerHostJob) + sizeof(SamplerReadbackPayload) + sizeof(SamplerState) +
                          SharedPublicationMemory::OwnershipAllowance() + 8192ull;
    if (!memory || !AddSamplerBytes(memory, metadata))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    UtilityReadbackAdmission::Envelope envelope;
    GpuResidencyReservation reservation;
    RETURN_IF_FAILED(PrepareRecordedUtilityAdmission(_session.Document().value(), memory, envelope, reservation));
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _samplerHostStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_samplerHostStamp)
        return E_UNEXPECTED;
    HRESULT error = E_OUTOFMEMORY;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<SamplerHostJob>(
            *this, _samplerHostStamp.value(), openingId, point.value(), region, envelope, std::move(reservation));
        auto* borrowed = static_cast<SamplerHostJob*>(job.get());
        JobTicket ticket;
        error = static_cast<HRESULT>(_jobs.Submit(job,
                                                  {.kind = JobClass::Selection,
                                                   .serialKey = _session.DocumentSerial(),
                                                   .memoryBytes = envelope.cpuBytes,
                                                   .deferredRetirement = true,
                                                   .allowMemoryWaiting = true,
                                                   .reserveRetainedMemory = true},
                                                  ticket));
        if (SUCCEEDED(error))
        {
            opening->jobAlive = true;
            opening->ticket = ticket;
            _samplerHostJob = borrowed;
            _samplerHostTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor-only allocation refusal; the source root/pixels have not been captured.
    }
    if (FAILED(error))
    {
        (void)_historyWork.Finish(_samplerHostStamp.value());
        _samplerHostStamp.reset();
        return error;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _samplerHostStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

void Application::AdvanceSamplerReadback() noexcept
{
    if (_samplerHostJob)
        _samplerHostJob->Advance();
}

void Application::CancelSamplerReadback() noexcept
{
    if (_samplerHostTicket)
    {
        (void)_jobs.Cancel(_samplerHostTicket.value());
        AdvanceSamplerReadback(); // An idle repeated-pixel record needs one explicit resume to acknowledge cancel.
    }
}

void Application::PublishSamplerPoint(std::uint64_t openingId, const SamplerIntentPoint& point,
                                      std::optional<std::uint32_t> opaqueArgb) noexcept
{
    auto* opening = FindSamplerOpening(openingId);
    if (!opening)
        return;
    if (!opening->resolvedIntent || !SameSamplerIntent(opening->resolvedIntent.value(), point.intent))
    {
        opening->resolvedArgb = point.intent.baselineArgb | 0xFF000000u;
        const auto& prefix = _samplerState->palettePrefix;
        if (!opening->picker && prefix && prefix->session == opening->source.sessionIdentity &&
            prefix->documentSerial == opening->source.documentSerial && prefix->intent == point.intent.paletteIntent)
            opening->resolvedArgb = prefix->argb;
    }
    if (opaqueArgb)
        opening->resolvedArgb = opaqueArgb.value();
    opening->resolvedIntent = point.intent;
    if (!opening->picker)
        _samplerState->palettePrefix =
            SamplerState::PalettePrefix{opening->source.sessionIdentity, opening->source.documentSerial,
                                        point.intent.paletteIntent, opening->resolvedArgb};
    if (!opaqueArgb)
        return; // A transparent point carries the accepted prefix but performs no palette/picker write.
    if (opening->picker && _chrome)
    {
        const auto& target = opening->picker.value();
        const ChromeHost::PickerIntentAuthority intent{target.host, target.opening, point.intent.pickerIntent,
                                                       target.target};
        if (_chrome->IsPickerIntentCurrent(intent))
        {
            auto publication = _chrome->CapturePickerSampleAuthority();
            if (publication && _chrome->ApplySampledPickerColor(publication.value(), opaqueArgb.value()))
            {
                // Worker-mode Preview preserves its root/generation; direct reference Preview may mutate
                // parameters. Only this exact own callback renews authority over the admitted original root.
                const auto current = _session.CaptureDocumentReadAuthority();
                if (current && current->sessionIdentity == opening->source.sessionIdentity &&
                    current->documentSerial == opening->source.documentSerial &&
                    current->restoreSerial == opening->source.restoreSerial &&
                    current->canvasWidth == opening->source.canvasWidth &&
                    current->canvasHeight == opening->source.canvasHeight)
                    opening->source = current.value();
            }
        }
    }
    else if (_session.PendingBrush() == nullptr)
    {
        const DocumentSession::PaletteIntentAuthority intent{opening->source.sessionIdentity,
                                                             point.intent.paletteIntent};
        if (_session.IsPaletteIntentCurrent(intent))
        {
            auto publication = _session.CapturePaletteSampleAuthority();
            (void)_session.ApplySampledForegroundColor(publication, opaqueArgb.value());
        }
    }
    _layoutDirty = true;
    InvalidateFrame();
}

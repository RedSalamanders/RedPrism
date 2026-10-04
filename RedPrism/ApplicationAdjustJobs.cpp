#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "ColorDisplayPublication.h"
#include "ColorEditHistory.h"
#include "ColorWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

JobResult PassColorWorkTestGate(const std::shared_ptr<ColorWorkTestGate>& gate, ColorWorkTestKind kind,
                                JobContext& context) noexcept
{
    if (!gate || gate->kind != kind)
    {
        return {};
    }
    gate->visits.fetch_add(1);
    (void)SetEvent(gate->entered.get());
    const HANDLE handles[]{gate->release.get(), context.shutdownEvent};
    const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
    if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
    {
        return {JobStatus::Cancelled};
    }
    return waited == WAIT_OBJECT_0 ? gate->releasedResult
                                   : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
}

namespace
{
bool AddColorBytes(std::uint64_t& total, std::uint64_t count, std::uint64_t size = 1) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if ((size && count > maximum / size) || count * size > maximum - total)
        return false;
    total += count * size;
    return true;
}

std::optional<std::uint64_t> ColorCaptureMetadata(const ColorEdit& edit) noexcept
{
    std::uint64_t bytes = 65536;
    for (const auto& channel : edit.settings.curves.channels)
        if (!AddColorBytes(bytes, channel.size(), 6 * sizeof(CurvePoint)))
            return std::nullopt;
    if (edit.sourceToken && edit.sourceToken->target)
    {
        const auto& target = edit.sourceToken->target.value();
        if (target.mask && !AddColorBytes(bytes, target.mask->asset.name.size() + 1, 6 * sizeof(wchar_t)))
            return std::nullopt;
        if (target.adjustment)
            for (const auto& channel : target.adjustment->curves.channels)
                if (!AddColorBytes(bytes, channel.size(), 6 * sizeof(CurvePoint)))
                    return std::nullopt;
    }
    return bytes;
}

std::optional<ColorDisplayPreviewMemory> ColorLookupMemory(const DocumentSession& session, bool histogram) noexcept
{
    const auto& pending = session.PendingColorEdit();
    const auto* layer = session.ActiveLayer();
    if (!pending || !layer)
        return std::nullopt;
    const auto& edit = pending.value();
    const auto render = histogram ? ColorEditHistogramMemoryBytes(edit) : ColorEditPreviewMemoryBytes(edit, true, true);
    const auto metadata = ColorCaptureMetadata(edit);
    const auto target = RecordOwnershipPolicy<ImageLayer>::Measure(*layer);
    const auto held = ColorEditHeldMemoryBytes(edit);
    if (!render || !metadata || !target || !held)
        return std::nullopt;
    ColorDisplayPreviewMemory memory{render.value(), 0};
    if (!AddColorBytes(memory.additionalBytes, metadata.value()) ||
        !AddColorBytes(memory.additionalBytes, target->metadataBytes, 6) ||
        !AddColorBytes(memory.retirementBytes, target->ownershipBytes, 4) ||
        !AddColorBytes(memory.retirementBytes, held, 4) ||
        !AddColorBytes(memory.retirementBytes, memory.additionalBytes, 2))
        return std::nullopt;
    return memory;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct ColorPreviewPayload final : DeferredReleaseNode
{
    std::optional<ColorDisplayPreviewInput> input;
    std::optional<PreparedColorDisplayPreview> fallback;
    std::optional<ColorDisplayPreviewResult> prepared;
    std::optional<ColorEditJobInput> lookupInput;
    std::optional<ColorEditPreviewResult> lookupFallback;
    std::optional<ColorEditPreviewResult> lookupResult;
    std::shared_ptr<ColorWorkTestGate> gate;
    DWORD ownerThread = 0;
    ColorPreviewPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<ColorPreviewPayload> owned(static_cast<ColorPreviewPayload*>(value)); };
    }
    ~ColorPreviewPayload()
    {
        if (gate && gate->kind == ColorWorkTestKind::Preview)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)

struct RetireColorPreviewPayload final
{
    void operator()(ColorPreviewPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value))
        {
            const std::unique_ptr<ColorPreviewPayload> owned(value);
        }
    }
};
} // namespace

class Application::ColorPreviewJob final : public Job
{
  public:
    ColorPreviewJob(Application& owner, ColorPreviewRequest request, PreviewStamp stamp, std::uint64_t retirement)
        : _owner(owner), _request(request), _stamp(stamp), _retirement(retirement), _gate(owner._colorWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Admission selects this same record before owner capture and resume.
        if (!_gate || !_gate->afterPreparation)
        {
            const auto result = PassColorWorkTestGate(_gate, ColorWorkTestKind::Preview, context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Preview && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        if (_request.lookupOnly)
        {
            _payload->lookupResult.emplace(
                RunColorEditPreviewJob(_payload->lookupInput.value(), context.executor, context.cancellation));
            _payload->lookupInput.reset();
        }
        else
        {
            _payload->prepared.emplace();
            DocumentSession::PrepareColorDisplayPreviewPublicationInto(std::move(_payload->input.value()),
                                                                       _payload->prepared.value(), context.executor,
                                                                       context.cancellation, {});
            _payload->input.reset();
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Preview)
            (void)SetEvent(_gate->computed.get());
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassColorWorkTestGate(_gate, ColorWorkTestKind::Preview, context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    bool DeferCompletion(JobResult completion, JobTicket ticket) noexcept override
    {
        if (_payload || completion.status != JobStatus::Finished || !Current(true))
            return false;
        try
        {
            _payload.reset(std::make_unique<ColorPreviewPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate && _gate->kind == ColorWorkTestKind::Preview)
            {
                _gate->captureReservationHeld = _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            if (_request.lookupOnly)
            {
                auto input = _owner._session.PrepareColorEditPreview();
                if (!input)
                {
                    _captureError = E_ABORT;
                    return false;
                }
                _owner.ConfigureColorLookupPreview(input.value());
                _payload->lookupFallback.emplace();
                _payload->lookupFallback->token = input->token;
                _payload->lookupFallback->edit = input->edit;
                _payload->lookupInput.emplace(std::move(input.value()));
            }
            else
            {
                _owner._session.PrepareColorDisplayPreviewInputInto(_payload->input, _request.lookupPreview);
                if (!_payload->input)
                {
                    _captureError = E_ABORT;
                    return false;
                }
                // Preallocate a move-only failure publication while the complete reservation is held. The
                // worker may unwind its input after bad_alloc; completion never reconstructs settings/owners.
                DocumentSession::PrepareFailedColorDisplayPreviewPublicationInto(
                    std::move(_payload->input.value()), _payload->fallback, ColorEditJobStatus::Failed);
                _payload->input.reset(); // All owners moved into the preallocated failure publication.
                _owner._session.PrepareColorDisplayPreviewInputInto(_payload->input, _request.lookupPreview);
                if (!_payload->input)
                {
                    _captureError = E_ABORT;
                    return false;
                }
            }
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY;
        }
        return false;
    }
    void Completed(JobResult completion) noexcept override
    {
        _owner._colorCompleting = true;
        const auto endCompletion = wil::scope_exit([&]() noexcept { _owner._colorCompleting = false; });
        const bool current = Current(false);
        bool applied = false;
        bool changed = false;
        HRESULT error = FAILED(_captureError) ? _captureError : completion.error;
        if (current && completion.status != JobStatus::Cancelled && _payload)
        {
            if (_request.lookupOnly)
            {
                auto* result = completion.status == JobStatus::Finished && SUCCEEDED(error) && _payload->lookupResult
                                   ? &_payload->lookupResult.value()
                                   : (_payload->lookupFallback ? &_payload->lookupFallback.value() : nullptr);
                if (result)
                {
                    applied = _owner._session.ApplyColorEditPreview(*result);
                    changed = applied || result->token.sequence == _owner._session.ColorEditSequence();
                    if (result->status == ColorEditJobStatus::Failed &&
                        result->token.sequence == _owner._session.ColorEditSequence())
                        error = FAILED(error) ? error : E_FAIL;
                }
            }
            else
            {
                auto* publication = completion.status == JobStatus::Finished && SUCCEEDED(error) &&
                                            _payload->prepared && _payload->prepared->publication
                                        ? &_payload->prepared->publication.value()
                                        : (_payload->fallback ? &_payload->fallback.value() : nullptr);
                if (publication)
                {
                    applied = _owner._session.ApplyPreparedColorDisplayPreviewIfCurrent(*publication);
                    changed = publication->IsConsumed();
                    if (publication->Status() == ColorEditJobStatus::Failed &&
                        _request.sequence == _owner._session.ColorEditSequence())
                        error = FAILED(error) ? error : E_FAIL;
                }
            }
        }
        _owner._colorPreviewTicket.reset(); // Retained across close/off/reopen until this exact acknowledgement.
        (void)_owner._colorPreviews.Finish(_stamp, applied);
        if (current && completion.status != JobStatus::Cancelled && FAILED(error) && error != E_ABORT)
            _owner.QueueColorFailure();
        if (changed)
            _owner.DocumentChanged();
        if (!_owner._shutdownStarted)
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
    }

  private:
    bool Current(bool capturing) const noexcept
    {
        const auto& edit = _owner._session.PendingColorEdit();
        return !_owner._shutdownStarted && !_owner.ColorCommitBusy() && edit && edit->deferred &&
               _owner._session.DocumentSerial() == _request.documentSerial &&
               edit->openingSequence == _request.opening && _owner._colorPreviews.CanPublish(_stamp) &&
               _owner._session.AdjustmentEditingID().has_value() == _request.lookupOnly &&
               (_request.lookupOnly || _owner._session.Generation() == _request.generation) &&
               (!capturing || _owner._session.ColorEditSequence() == _request.sequence);
    }
    Application& _owner; // Owner callbacks only; Run uses its admitted immutable payload and harness gate.
    const ColorPreviewRequest _request;
    const PreviewStamp _stamp;
    const std::uint64_t _retirement;
    const std::shared_ptr<ColorWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<ColorPreviewPayload, RetireColorPreviewPayload> _payload;
    HRESULT _captureError = S_OK;
};

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ColorHistogramPayload final : DeferredReleaseNode
{
    std::optional<ColorEditJobInput> input;
    std::optional<ColorEditHistogramResult> result;
    std::shared_ptr<ColorWorkTestGate> gate;
    DWORD ownerThread = 0;
    ColorHistogramPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ColorHistogramPayload> owned(static_cast<ColorHistogramPayload*>(node)); };
    }
    ~ColorHistogramPayload()
    {
        if (gate && gate->kind == ColorWorkTestKind::Histogram)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireColorHistogramPayload final
{
    void operator()(ColorHistogramPayload* node) const noexcept
    {
        if (node && !TryDeferRelease(*node))
        {
            const std::unique_ptr<ColorHistogramPayload> owned(node);
        }
    }
};
} // namespace

class Application::ColorHistogramJob final : public Job
{
  public:
    ColorHistogramJob(Application& owner, std::uint64_t serial, std::uint64_t opening, std::uint64_t retirement)
        : _owner(owner), _serial(serial), _opening(opening), _retirement(retirement), _gate(owner._colorWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {};
        if (!_gate || !_gate->afterPreparation)
        {
            const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Histogram, context);
            if (gate.status != JobStatus::Finished)
                return gate;
        }
        if (_gate && _gate->kind == ColorWorkTestKind::Histogram && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        _payload->result.emplace(
            RunColorEditHistogramJob(_payload->input.value(), context.executor, context.cancellation));
        _payload->input.reset();
        if (_gate && _gate->kind == ColorWorkTestKind::Histogram)
            (void)SetEvent(_gate->computed.get());
        if (_gate && _gate->afterPreparation)
        {
            const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Histogram, context);
            if (gate.status != JobStatus::Finished)
                return gate;
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current())
            return false;
        try
        {
            _payload.reset(std::make_unique<ColorHistogramPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate && _gate->kind == ColorWorkTestKind::Histogram)
            {
                _gate->captureReservationHeld = _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            auto input = _owner._session.PrepareColorEditPreview();
            if (!input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY;
        }
        return false;
    }
    void Completed(JobResult completion) noexcept override
    {
        HRESULT error = FAILED(_captureError) ? _captureError : completion.error;
        if (Current() && completion.status != JobStatus::Cancelled)
        {
            if (completion.status == JobStatus::Finished && SUCCEEDED(error) && _payload && _payload->result)
            {
                auto& result = _payload->result.value();
                if (_owner._session.ApplyColorEditHistogram(result))
                {
                    _owner.SyncChrome();
                    _owner._frameInvalidated = true;
                }
                else if (result.status == ColorEditJobStatus::Failed)
                    error = E_FAIL;
            }
            if (FAILED(error) && error != E_ABORT)
                _owner.QueueColorFailure();
        }
        _owner._colorHistogramTicket.reset();
        if (!_owner._shutdownStarted)
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
    }

  private:
    bool Current() const noexcept
    {
        const auto& edit = _owner._session.PendingColorEdit();
        return !_owner._shutdownStarted && !_owner.ColorCommitBusy() && edit && edit->deferred &&
               _owner._session.DocumentSerial() == _serial && edit->openingSequence == _opening;
    }
    Application& _owner; // Only owner callbacks read it; Run owns immutable admitted inputs.
    const std::uint64_t _serial;
    const std::uint64_t _opening;
    const std::uint64_t _retirement;
    const std::shared_ptr<ColorWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<ColorHistogramPayload, RetireColorHistogramPayload> _payload;
    HRESULT _captureError = S_OK;
};
class Application::ColorFailureAction final : public UiAction
{
  public:
    explicit ColorFailureAction(Application& owner) noexcept
        : _owner(owner), _serial(owner._colorDocumentSerial), _opening(owner._colorOpening)
    {
    }
    void Run() noexcept override
    {
        const auto& edit = _owner._session.PendingColorEdit();
        if (!_owner._shutdownStarted && _owner._session.DocumentSerial() == _serial && edit &&
            edit->openingSequence == _opening)
        {
            _owner.ShowError(IDS_ERROR_FILTER);
        }
    }

  private:
    Application& _owner;
    std::uint64_t _serial;
    std::uint64_t _opening;
};

void Application::QueueColorFailure() noexcept
{
    std::unique_ptr<UiAction> action{new (std::nothrow) ColorFailureAction(*this)};
    if (!action || !_jobs.PostUiAction(action))
    {
        // The bounded action queue may be full. The existing posted notice is also delivered outside the
        // completion callback; it does not open a modal or change focus from this call stack.
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_FILTER);
    }
}

void Application::ConfigureColorLookupPreview(ColorEditJobInput& input) const
{
    input.lookupPreview = FilterIsImageAdjustment(input.edit.kind);
    if (const auto id = _session.AdjustmentEditingID(); id)
    {
        const ImageLayer* layer = _session.ActiveLayer();
        if (layer && layer->id == *id && layer->adjustment)
        {
            input.liveAdjustment = layer->adjustment;
        }
    }
}

void Application::SyncColorLookupPreview() noexcept
{
    const auto& edit = _session.PendingColorEdit();
    if (edit && _session.AdjustmentEditingID() && _session.AdjustmentOriginal())
    {
        _composition.SetAdjustmentPreview(CanvasRenderer::AdjustmentPreview{
            edit->layerID, {}, {}, edit->preview ? edit->previewLookup : nullptr, _session.AdjustmentOriginal(), 1.0});
    }
    else if (edit && _session.ColorLookupPreviewReady() && !edit->previewLookupOnly)
    {
        _composition.SetAdjustmentPreview(CanvasRenderer::AdjustmentPreview{
            edit->layerID, edit->previewSource, edit->previewCoverage, edit->previewLookup, {}, edit->previewScale});
    }
    else
    {
        _composition.SetAdjustmentPreview(std::nullopt);
    }
}

std::int32_t Application::SubmitColorPreview(PreviewMailbox<ColorPreviewRequest>::Launch launch) noexcept
{
    const auto& edit = _session.PendingColorEdit();
    const auto request = launch.input;
    if (!edit || !edit->deferred || request.documentSerial != _session.DocumentSerial() ||
        request.opening != edit->openingSequence || request.sequence != _session.ColorEditSequence() ||
        request.lookupOnly != _session.AdjustmentEditingID().has_value() ||
        (!request.lookupOnly && request.generation != _session.Generation()))
        return E_ABORT;
    auto memory = request.lookupOnly ? ColorLookupMemory(_session, false)
                                     : _session.ColorDisplayPreviewMemoryBytes(request.lookupPreview);
    if (!memory ||
        !AddColorBytes(memory->additionalBytes, sizeof(ColorPreviewJob) + sizeof(ColorPreviewPayload) + 4096) ||
        !AddColorBytes(memory->retirementBytes, sizeof(ColorPreviewPayload) + 4096))
        return E_INVALIDARG;
    try
    {
        std::unique_ptr<Job> job =
            std::make_unique<ColorPreviewJob>(*this, request, launch.stamp, memory->retirementBytes);
        JobTicket ticket;
        const auto status =
            _jobs.Submit(job, {JobClass::InteractivePreview, request.documentSerial, memory->additionalBytes}, ticket);
        if (status >= 0)
        {
            _colorPreviewTicket = ticket;
            if (_colorWorkTestGate && _colorWorkTestGate->kind == ColorWorkTestKind::Preview)
                _colorWorkTestGate->reservationBytes = memory->additionalBytes;
        }
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
std::int32_t Application::SubmitColorHistogram() noexcept
{
    const auto& edit = _session.PendingColorEdit();
    auto memory = ColorLookupMemory(_session, true);
    if (!edit || !memory ||
        !AddColorBytes(memory->additionalBytes, sizeof(ColorHistogramJob) + sizeof(ColorHistogramPayload) + 4096) ||
        !AddColorBytes(memory->retirementBytes, sizeof(ColorHistogramPayload) + 4096))
        return E_INVALIDARG;
    try
    {
        const auto serial = _session.DocumentSerial();
        std::unique_ptr<Job> job =
            std::make_unique<ColorHistogramJob>(*this, serial, edit->openingSequence, memory->retirementBytes);
        JobTicket ticket;
        const auto status = _jobs.Submit(job, {JobClass::Background, serial, memory->additionalBytes}, ticket);
        if (status >= 0)
        {
            _colorHistogramTicket = ticket;
            if (_colorWorkTestGate && _colorWorkTestGate->kind == ColorWorkTestKind::Histogram)
                _colorWorkTestGate->reservationBytes = memory->additionalBytes;
        }
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
std::int32_t Application::OfferColorPreview() noexcept
{
    const auto& edit = _session.PendingColorEdit();
    if (!edit || !edit->deferred)
        return E_INVALIDARG;
    const bool live = _session.AdjustmentEditingID().has_value();
    ColorPreviewRequest request{_session.DocumentSerial(),
                                edit->openingSequence,
                                _session.ColorEditSequence(),
                                live ? 0 : _session.Generation(),
                                live,
                                FilterIsImageAdjustment(edit->kind)};
    return _colorPreviews.Offer(request).accepted ? S_OK : E_INVALIDARG;
}

void Application::LaunchColorPreview() noexcept
{
    if (_shutdownStarted || _colorCompleting || _colorPreviewTicket || ColorCommitBusy() || _historyWork.Busy() ||
        _activeHostRequest || _hostRequests.Size() || _canvasLifetime)
        return;
    if (auto launch = _colorPreviews.TakeNext(); launch)
    {
        const auto stamp = launch->stamp;
        const auto status = SubmitColorPreview(std::move(launch.value()));
        if (status < 0)
        {
            (void)_colorPreviews.Finish(stamp, false);
            if (status != E_ABORT)
                QueueColorFailure();
        }
    }
}

void Application::CancelColorJobs() noexcept
{
    CancelColorCommit();
    (void)_colorPreviews.Close();
    _colorMailboxOpen = false;
    if (_colorPreviewTicket)
        (void)_jobs.Cancel(_colorPreviewTicket.value());
    if (_colorHistogramTicket)
        (void)_jobs.Cancel(_colorHistogramTicket.value());
    // These exact records acknowledge before a reopened panel can capture a second owner graph.
    _colorOpening = 0;
    _colorSeenSequence = 0;
    _colorSeenGeneration = 0;
    _colorHistogramAttempted = false;
}

void Application::RefreshColorJobs() noexcept
{
    if (_colorCompleting)
        return;
    if (ColorCommitBusy())
    {
        if (_colorCommitStamp->documentSerial != _session.DocumentSerial())
            CancelColorCommit();
        return;
    }
    const auto& edit = _session.PendingColorEdit();
    if (_shutdownStarted || !edit || !edit->deferred)
    {
        if (_colorOpening)
            CancelColorJobs();
        return;
    }
    if (_colorDocumentSerial != _session.DocumentSerial() || _colorOpening != edit->openingSequence)
    {
        CancelColorJobs();
        _colorDocumentSerial = _session.DocumentSerial();
        _colorOpening = edit->openingSequence;
    }
    if (!_colorHistogramAttempted && !_colorHistogramTicket && !_historyWork.Busy() && !_activeHostRequest &&
        !_hostRequests.Size() && !_canvasLifetime && edit->kind == FilterKind::Levels && !edit->histogramReady)
    {
        _colorHistogramAttempted = true;
        if (SubmitColorHistogram() < 0)
            QueueColorFailure();
    }
    const auto sequence = _session.ColorEditSequence();
    const auto generation = _session.AdjustmentEditingID() ? 0 : _session.Generation();
    if (sequence == _colorSeenSequence && generation == _colorSeenGeneration)
    {
        LaunchColorPreview();
        return;
    }
    if (_colorMailboxOpen && generation != _colorSeenGeneration)
    {
        (void)_colorPreviews.Close();
        _colorMailboxOpen = false;
        if (_colorPreviewTicket)
            (void)_jobs.Cancel(_colorPreviewTicket.value());
    }
    _colorSeenSequence = sequence;
    _colorSeenGeneration = generation;
    const bool pixels = _session.AdjustmentEditingID().has_value() ||
                        (edit->preview && !ColorEditChangesNothing(edit->kind, edit->settings) &&
                         Adjust::BlurMargin(edit->kind, edit->settings) <= edit->grownMargin);
    if (!pixels)
    {
        (void)_colorPreviews.Close();
        _colorMailboxOpen = false;
        if (_colorPreviewTicket)
            (void)_jobs.Cancel(_colorPreviewTicket.value());
        return;
    }
    if (!_colorMailboxOpen)
        _colorMailboxOpen = _colorPreviews.Begin();
    if (!_colorMailboxOpen || OfferColorPreview() < 0)
    {
        QueueColorFailure();
        return;
    }
    LaunchColorPreview();
}
int Application::RunColorJobsSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    CancelColorEditForTest();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        return fail(L"Color job fixture could not settle preceding work.\n");
    }
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Color jobs", Vec2{4, 1});
    const Guid active = layer.id;
    layer.asset = RasterAsset{source, source, layer.name};
    document.layers.push_back(std::move(layer));
    ReplaceDocument(std::move(document).Seal(), active, {});

    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
            {
                (void)SetEvent(_colorWorkTestGate->release.get());
            }
            CancelColorEditForTest();
            _colorWorkTestGate.reset();
            if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            {
                SelfTestFailure(L"Color jobs did not settle at the harness failure boundary.\n");
                std::_Exit(5);
            }
        });
    const auto newGate = [&](bool histogram)
    {
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = histogram ? ColorWorkTestKind::Histogram : ColorWorkTestKind::Preview;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    if (!newGate(true) || !BeginColorEdit(ChromeHost::kCommandLevels) ||
        !WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The production Levels histogram did not reach its worker gate.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    _viewport.followsFit = false;
    _viewport.zoom = 1;
    constexpr UINT gateMessage = WM_APP + 0x7D0;
    struct PumpEvidence final
    {
        Application* owner;
        bool beforeHistogram = false;
        bool zoomed = false;
    } evidence{this};
    const auto dispatch = [](void* context, void* nativeMessage) noexcept
    {
        auto& seen = *static_cast<PumpEvidence*>(context);
        auto& message = *static_cast<MSG*>(nativeMessage);
        Application& app = *seen.owner;
        if (!message.hwnd && message.message == gateMessage)
        {
            seen.beforeHistogram = app._session.HasColorEdit() && !app._session.PendingColorEdit()->histogramReady;
            app.OnCommand(ChromeHost::kCommandZoomIn);
            seen.zoomed = app._viewport.zoom > 1;
            (void)SetEvent(app._colorWorkTestGate->release.get());
        }
        else
        {
            app.DispatchQueuedMessage(message);
        }
    };
    if (!PostThreadMessageW(GetCurrentThreadId(), gateMessage, 0, 0) ||
        SettleJobs(_jobs, 10000, dispatch, &evidence) != SettleResult::Settled || !evidence.beforeHistogram ||
        !evidence.zoomed || !_session.PendingColorEdit()->histogramReady ||
        _session.PendingColorEdit()->histogram[0][64] != 1 ||
        _session.PendingColorEdit()->settings.levels.ranges[0].gamma != 2)
    {
        return fail(L"Messages/view input or the opening histogram failed while real color work was gated.\n");
    }
    CancelColorEditForTest();

    if (!newGate(false) || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The coalesced preview fixture could not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The production preview did not reach its worker gate.\n");
    }
    for (int tick = 0; tick < 12; ++tick)
    {
        OnAdjustAction(AdjustAction::SetInputGamma, 1.1 + tick * 0.1);
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 3);
    if (_colorWorkTestGate->visits.load() != 1 || !_colorPreviews.HasPending())
    {
        return fail(L"Settings ticks started overlapping preview work instead of keeping the latest pending input.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        _colorWorkTestGate->visits.load() != 2 || _colorPreviews.HasPending() ||
        !_session.PendingColorEdit()->previewLookup || _session.PendingColorEdit()->previewRaster.HasStorage() ||
        _session.PendingColorEdit()->previewSequence != _session.ColorEditSequence() ||
        _session.PendingColorEdit()->settings.levels.ranges[0].gamma != 3)
    {
        return fail(L"The preview mailbox did not finish with exactly the latest request.\n");
    }
    CancelColorEditForTest();

    if (!newGate(false) || !BeginColorEdit(ChromeHost::kCommandLevels))
    {
        return fail(L"The late completion fixture could not open.\n");
    }
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The late completion fixture did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The old preview did not finish its pixels before the Cancel/reopen race.\n");
    }
    CancelColorEditForTest(); // Its successful pixels exist, but no completion has been drained yet.
    if (!BeginColorEdit(ChromeHost::kCommandLevels) ||
        SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        _session.PendingColorEdit()->previewRaster.HasStorage() || _session.PendingColorEdit()->previewLookup ||
        !_session.PendingColorEdit()->histogramReady || _session.ActiveLayer()->asset->image != source)
    {
        return fail(L"A completed old preview crossed the cancelled panel lifetime.\n");
    }
    CancelColorEditForTest();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        return fail(L"The live adjustment fixture could not retire earlier work.\n");
    }
    _session.AddAdjustment(AdjustmentKind::Levels);
    const auto adjustmentId = _session.AdjustmentEditingID();
    LayerAdjustment original;
    original.kind = AdjustmentKind::Levels;
    original.levels.ranges[0].gamma = 1.25;
    if (!adjustmentId || !_session.UpdateAdjustment(*adjustmentId, original) ||
        !_session.BeginAdjustmentEditing(*adjustmentId, source, true) || !newGate(false))
    {
        return fail(L"The live adjustment lookup fixture did not open.\n");
    }
    DocumentChanged();
    if (!WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this))
    {
        return fail(L"The live adjustment did not build its lookup on a worker.\n");
    }
    const auto matchesAdjustment = [&](const LayerAdjustment& adjustment)
    {
        const CanvasDocument* display = _session.DisplayDocument();
        std::vector<std::uint8_t> shown;
        UINT width = 0;
        UINT height = 0;
        const Raster expected = Adjust::ApplyAdjustment(adjustment, source);
        if (!display || FAILED(_composition.Compose(*display)) || FAILED(_composition.Readback(shown, width, height)) ||
            width != 4 || height != 1 || !expected.HasStorage() || shown.size() != 16)
            return false;
        const auto* bytes = expected.Data();
        for (std::size_t index = 0; index < shown.size(); ++index)
        {
            if (std::abs(static_cast<int>(shown[index]) - bytes[index]) > (index % 4 == 3 ? 0 : 1))
                return false;
        }
        return true;
    };
    OnAdjustAction(AdjustAction::SetInputGamma, 3);
    if (!matchesAdjustment(original) || !_colorPreviews.HasPending())
    {
        return fail(L"Pending live adjustment work changed the canvas before its immutable lookup completed.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled ||
        !_session.PendingColorEdit()->previewLookup || !_session.PendingColorEdit()->previewLookupOnly ||
        _session.PendingColorEdit()->previewRaster.HasStorage() ||
        _session.PendingColorEdit()->previewSequence != _session.ColorEditSequence() ||
        _session.PendingColorEdit()->previewLookup->adjustment.levels.ranges[0].gamma != 3 ||
        !matchesAdjustment(_session.PendingColorEdit()->previewLookup->adjustment))
    {
        return fail(L"The live adjustment mailbox did not publish the latest lookup and matching GPU pixels.\n");
    }
    std::printf("PASS color-jobs.live-worker-lookup-and-latest-gpu-pixels\n");
    if (!newGate(false))
        return fail(L"The live preview-off gate could not be created.\n");
    _session.UpdateColorEdit(_session.PendingColorEdit()->settings, false);
    DocumentChanged();
    if (!WaitForColorWorkTestEvent(_jobs, _colorWorkTestGate->entered.get(), DispatchMessageThunk, this) ||
        !matchesAdjustment(original))
    {
        return fail(L"Live preview off waited for a worker before restoring the opening adjustment.\n");
    }
    CancelColorEditForTest();
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled || _session.HasColorEdit() ||
        !matchesAdjustment(original))
    {
        return fail(L"A late lookup crossed live adjustment Cancel.\n");
    }
    std::printf("PASS color-jobs.live-preview-off-and-cancel-in-flight\n");
    CancelColorEditForTest();
    auto previousDocument = _session.Document();
    if (const int display = RunColorDisplayHostSelfTest(); display != 0)
        return display;
    _session.ReplaceDocument(std::move(previousDocument.value()), active);
    _session.SelectLayer(active); // leave the image target expected by the following host fixtures
    DocumentChanged();
    return 0;
}

#include "Application.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "Runtime.Tests.GradientWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct GradientPayload final : DeferredReleaseNode
{
    RasterEditInput input;
    std::optional<JobToken> token;
    RasterEditResult committed;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView historyState;
    std::optional<PreparedRasterEdit> publication;
    RasterEditPreviewResult preview;
    std::shared_ptr<GradientWorkTestGate> gate;
    DWORD ownerThread = 0;
    std::uint64_t preparationBytes = 0;
    std::uint64_t retirementBytes = 0;
    GradientPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold; // capture/preparation OOM still retires a potentially large graph
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<GradientPayload> owned(static_cast<GradientPayload*>(node)); };
    }
    ~GradientPayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
    void AccountRetainedGraph()
    {
        // The complete preparation peak bounds private stroke/output storage. Source owners are shared at
        // admission, but can become the last owners after document replacement. Count occurrences conservatively
        // here; do not allocate a deduplication set merely to decide whether destruction belongs on the UI.
        std::uint64_t retained = preparationBytes;
        const auto add = [&](std::uint64_t value) noexcept
        {
            retained = value > (std::numeric_limits<std::uint64_t>::max)() - retained
                           ? (std::numeric_limits<std::uint64_t>::max)()
                           : retained + value;
        };
        const auto raster = [&](const Raster& image)
        {
            image.ForEachStorage(
                [&](const Raster& storage)
                {
                    add(PixelBuffer::RequiredBytes(storage.ByteSize()) + 256ull +
                        storage.Tiles().capacity() * sizeof(RasterTile));
                });
        };
        if (input.layer.asset)
        {
            raster(input.layer.asset->image);
            raster(input.layer.asset->thumbnail);
        }
        if (input.layer.mask)
        {
            raster(input.layer.mask->asset.image);
            raster(input.layer.mask->asset.thumbnail);
        }
        raster(input.layer.shapeImage);
        if (input.selection)
            add(input.selection->loops.StorageBytes());
        bytes = retirementBytes ? (std::max)({retained, retirementBytes, kDeferredReleaseThreshold}) : retained;
    }
};
#pragma warning(pop)

struct RetireGradientPayload final
{
    void operator()(GradientPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<GradientPayload> owned(payload);
        }
    }
};
} // namespace

// One admission/capture protocol serves both kinds. Requests are primitive event-time values; source handles
// are captured only under the full held reservation. Run never dereferences the Application or mailboxes.
class Application::GradientJob final : public Job
{
  public:
    GradientJob(Application& owner, GradientRequest request, std::uint64_t serial, std::uint64_t generation,
                std::int64_t pixelLimit, std::uint64_t preparationBytes, std::optional<PreviewStamp> preview,
                std::optional<HistoryWorkStamp> commit, std::uint64_t retirementBytes)
        : _owner(owner), _request(request), _serial(serial), _generation(generation), _pixelLimit(pixelLimit),
          _preparationBytes(preparationBytes), _retirementBytes(retirementBytes), _preview(preview), _commit(commit),
          _gate(owner._gradientWorkTestGate &&
                        owner._gradientWorkTestGate->kind ==
                            (commit ? GradientWorkTestKind::Commit : GradientWorkTestKind::Preview)
                    ? owner._gradientWorkTestGate
                    : nullptr),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // owner capture follows complete admission, then resumes this same scheduler record
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        _payload->input.progressContext = &context;
        _payload->input.progress = [](void* value, std::size_t completed, std::size_t total) noexcept
        {
            static_cast<JobContext*>(value)->ReportProgress(total ? static_cast<unsigned int>(completed * 100 / total)
                                                                  : 100);
        };
        const auto clearBorrow = wil::scope_exit(
            [&]() noexcept
            {
                _payload->input.progress = nullptr;
                _payload->input.progressContext = nullptr;
            });
        if (_commit)
        {
            _payload->committed = RunRasterEdit(_payload->input, context.cancellation);
            if (_payload->committed.status == RasterEditStatus::Ready && _payload->token)
            {
                _payload->publication = PrepareRasterEditPublication(
                    RasterEditKind::Gradient, _request.mask, _payload->source, _request.target, *_payload->token,
                    std::move(_payload->historyState), std::move(_payload->committed),
                    _request.mask ? L"Gradient Mask" : L"Gradient", context.cancellation);
                if (!_payload->publication)
                    _payload->committed.status =
                        context.cancellation.IsCancelled() ? RasterEditStatus::Cancelled : RasterEditStatus::Invalid;
            }
        }
        else
            _payload->preview = RunRasterEditPreview(_payload->input, context.cancellation);
        _payload->AccountRetainedGraph();
        if (_gate && _gate->status)
        {
            if (_commit)
                _payload->committed.status = _gate->status.value();
            else
                _payload->preview.status = _gate->status.value();
        }
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            (_commit && _owner._historyWork.CancelRequested(_commit.value())))
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            const auto* layer = _owner._session.ActiveLayer();
            if (!layer || layer->id != _request.target || _owner._session.IsMaskSelected() != _request.mask)
            {
                _captureError = E_ABORT;
                return false;
            }
            auto owned = std::make_unique<GradientPayload>();
            _payload.reset(owned.release());
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            _payload->preparationBytes = _preparationBytes;
            _payload->retirementBytes = _retirementBytes;
            _payload->bytes = (std::max)(_retirementBytes, kDeferredReleaseThreshold);
            auto& input = _payload->input;
            input.layer = *layer;
            input.selection = _owner._session.Selection();
            input.canvasWidth = _owner._session.Document()->width;
            input.canvasHeight = _owner._session.Document()->height;
            input.pixelLimit = _pixelLimit;
            input.mask = _request.mask;
            input.kind = RasterEditKind::Gradient;
            input.start = _request.start;
            input.end = _request.end;
            input.shape = _request.shape;
            input.color = _request.color;
            input.endColor = _request.endColor;
            input.opacity = _request.opacity;
            // Acquisition and gradient settings are separate clocks. A continued selected ordinal has a fresh
            // acquisition; a preview has a mailbox sequence. Neither is compared with the metadata sequence.
            _payload->token =
                _owner._session.CaptureJobToken(_request.target, _commit ? _commit->acquisition : _preview->sequence);
            if (_commit)
            {
                _payload->source = _owner._session.Document();
                _payload->historyState = _owner._session.history.CaptureState();
            }
            if (_payload->token && _owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // explicit noexcept capture boundary fails the entire operation
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        _owner._gradientCompleting = true;
        const auto clearCompleting = wil::scope_exit([&]() noexcept { _owner._gradientCompleting = false; });
        const bool cancelled =
            result.status == JobStatus::Cancelled || (_commit && _owner._historyWork.CancelRequested(_commit.value()));
        const bool current = Current() && _payload && _payload->token &&
                             _owner._session.CanApplyJobResult(_payload->token.value(), _payload->token->sequence);
        HostOutcome outcome = cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError)
                                  ? (_captureError == E_ABORT ? HostOutcome::Stale : HostOutcome::Failed)
                              : !current ? HostOutcome::Stale
                                         : HostOutcome::Succeeded;
        HRESULT error = FAILED(_captureError) ? _captureError : result.error;
        bool produced = false;
        if (outcome == HostOutcome::Succeeded)
        {
            const auto status = _commit ? _payload->committed.status : _payload->preview.status;
            switch (status)
            {
            case RasterEditStatus::Ready:
                if (_commit)
                {
                    const auto* pending = _owner._session.PendingGradient();
                    const bool adopt = pending && pending->raster && pending->publishedSequence == _request.sequence &&
                                       pending->publishedGeneration == _generation &&
                                       _owner._previewSource.lock() == pending->raster &&
                                       _owner._composition.StrokeLayerID() == std::optional<Guid>(_request.target);
                    produced = _payload->publication &&
                               _owner._session.ApplyPreparedGradientIfCurrent(*_payload->publication, _request.opening,
                                                                              _request.sequence);
                    if (produced && adopt)
                        _owner.EndStrokePreview(_payload->publication->PreviewCommit());
                }
                else
                    produced = _owner._session.PublishGradientPreview(std::move(_payload->preview.stroke),
                                                                      _payload->token.value(), _request.opening,
                                                                      _request.sequence);
                if (!produced)
                    outcome = HostOutcome::Stale;
                break;
            case RasterEditStatus::Unchanged:
                if (_commit)
                    _owner._session.CancelGradient(); // valid exact acquisition, but no pixels/history to apply
                break;
            case RasterEditStatus::Cancelled:
                outcome = HostOutcome::Cancelled;
                break;
            default:
                outcome = HostOutcome::Failed;
                error = status == RasterEditStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                        : status == RasterEditStatus::Memory ? E_OUTOFMEMORY
                                                             : E_INVALIDARG;
                break;
            }
        }
        if (outcome == HostOutcome::Failed && !_owner._shutdownStarted && Current())
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (_commit)
        {
            _owner._gradientCommitTicket.reset();
            _owner._gradientCommitStamp.reset();
            (void)_owner._historyWork.Finish(_commit.value());
            if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
                error = E_ABORT;
            _owner.ResumeHostHistory(_commit.value(), outcome, error);
            _owner.DocumentChanged();
        }
        else
        {
            _owner._gradientPreviewTicket.reset(); // acknowledgement, including an invalidated old epoch
            (void)_owner._gradientPreviews.Finish(_preview.value(), produced);
            _owner.SyncStrokePreview();
            _owner.SyncChrome(); // observes a new opening/tick and launches the latest pending request
            _owner._frameInvalidated = true;
        }
        if (_owner._gradientMailboxOpen && _owner._gradientPreviews.HasPending())
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
    }

  private:
    bool Current() const noexcept
    {
        const auto* pending = _owner._session.PendingGradient();
        if (_owner._shutdownStarted || _owner._session.DocumentSerial() != _serial ||
            _owner._session.Generation() != _generation || !pending || !pending->deferred || !pending->HasLine() ||
            pending->opening != _request.opening || pending->target != _request.target ||
            pending->mask != _request.mask)
            return false;
        if (!_commit)
            return _preview && _owner._gradientPreviews.CanPublish(_preview.value());
        return pending->sequence == _request.sequence && _owner._gradientCommitStamp == _commit &&
               _owner._historyWork.IsCurrent(_commit.value()) && _owner._activeHostRequest &&
               _owner._activeHostRequest->ordinal == _commit->ordinal &&
               _owner._activeHostRequest->documentSerial == _serial;
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, GradientWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner;
    const GradientRequest _request;
    const std::uint64_t _serial;
    const std::uint64_t _generation;
    const std::int64_t _pixelLimit;
    const std::uint64_t _preparationBytes;
    const std::uint64_t _retirementBytes;
    const std::optional<PreviewStamp> _preview;
    const std::optional<HistoryWorkStamp> _commit;
    const std::shared_ptr<GradientWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<GradientPayload, RetireGradientPayload> _payload;
    HRESULT _captureError = S_OK;
};

Application::GradientRequest Application::CaptureGradientRequest() const noexcept
{
    const auto* pending = _session.PendingGradient();
    const auto colors = _session.GradientColors();
    return GradientRequest{pending->target,
                           pending->opening,
                           pending->sequence,
                           pending->mask,
                           pending->start,
                           pending->end,
                           _session.gradientSettings.shape,
                           colors[0],
                           colors[1],
                           _session.gradientSettings.opacity};
}

void Application::CancelGradientJobs() noexcept
{
    (void)_gradientPreviews.Close();
    _gradientMailboxOpen = false;
    _gradientSeenSequence = 0;
    if (_gradientPreviewTicket)
        (void)_jobs.Cancel(_gradientPreviewTicket.value());
    // Keep the ticket until acknowledgement; a reopened epoch cannot launch a second interactive worker.
}

void Application::RefreshGradientJobs() noexcept
{
    const auto* pending = _session.PendingGradient();
    if (_shutdownStarted || _gradientCommitTicket || !pending || !pending->deferred || !pending->HasLine())
    {
        if (_gradientMailboxOpen)
            CancelGradientJobs();
        return;
    }
    if (_gradientMailboxOpen &&
        (_gradientPreviewSerial != _session.DocumentSerial() || _gradientPreviewOpening != pending->opening))
        CancelGradientJobs();
    if (!_gradientMailboxOpen)
    {
        if (!_gradientPreviews.Begin())
            return;
        _gradientMailboxOpen = true;
        _gradientPreviewSerial = _session.DocumentSerial();
        _gradientPreviewOpening = pending->opening;
    }
    if (_gradientSeenSequence != pending->sequence)
    {
        auto request = CaptureGradientRequest();
        if (_gradientPreviews.Offer(request).accepted)
            _gradientSeenSequence = pending->sequence;
    }
    LaunchGradientPreview();
}

void Application::LaunchGradientPreview() noexcept
{
    if (_shutdownStarted || _gradientCompleting || _gradientPreviewTicket || _gradientCommitTicket)
        return;
    if (auto launch = _gradientPreviews.TakeNext())
    {
        const auto result = SubmitGradientJob(launch->input, launch->stamp, std::nullopt);
        if (FAILED(result))
        {
            (void)_gradientPreviews.Finish(launch->stamp, false);
            ++_errorsShown;
            _lastErrorId =
                result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
        }
    }
}

HRESULT Application::SubmitGradientJob(GradientRequest request, std::optional<PreviewStamp> preview,
                                       std::optional<HistoryWorkStamp> commit) noexcept
{
    const auto* layer = _session.ActiveLayer();
    if (!layer || layer->id != request.target || !_session.Document())
        return E_ABORT;
    const auto limit = _session.RasterEditPixelLimit(*layer, request.mask);
    const auto preparation = RasterEditMemoryBytes(*layer, _session.Selection(), _session.Document()->width,
                                                   _session.Document()->height, limit, request.mask);
    const auto history = commit ? _session.history.EditMemoryBytes(_session.Document()) : 0;
    if (preparation == 0 || (commit && history == 0) ||
        history > (std::numeric_limits<std::uint64_t>::max)() - sizeof(GradientJob) - sizeof(GradientPayload) ||
        preparation >
            (std::numeric_limits<std::uint64_t>::max)() - history - sizeof(GradientJob) - sizeof(GradientPayload))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto memory = preparation + history + sizeof(GradientJob) + sizeof(GradientPayload);
    const auto held = commit ? _session.RasterEditRetirementMemoryBytes() : 0;
    if ((commit && !held) || memory > ((std::numeric_limits<std::uint64_t>::max)() - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto retirement = commit ? held + memory * 2 : 0;
    try
    {
        if (_gradientWorkTestGate &&
            _gradientWorkTestGate->kind == (commit ? GradientWorkTestKind::Commit : GradientWorkTestKind::Preview) &&
            _gradientWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<GradientJob>(*this, request, _session.DocumentSerial(), _session.Generation(), limit,
                                          preparation, preview, commit, retirement);
        JobTicket ticket;
        const auto result = static_cast<HRESULT>(_jobs.Submit(
            job, {commit ? JobClass::Commit : JobClass::InteractivePreview, _session.DocumentSerial(), memory},
            ticket));
        if (SUCCEEDED(result))
        {
            if (commit)
                _gradientCommitTicket = ticket;
            else
                _gradientPreviewTicket = ticket;
            if (_gradientWorkTestGate &&
                _gradientWorkTestGate->kind == (commit ? GradientWorkTestKind::Commit : GradientWorkTestKind::Preview))
                _gradientWorkTestGate->reservationBytes = memory;
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // no job admitted; caller fails this complete request
    }
}

HRESULT Application::StartGradientCommit(std::uint64_t ordinal) noexcept
{
    const auto* pending = _session.PendingGradient();
    if (!pending)
        return S_FALSE;
    if (!pending->deferred)
    {
        CommitGradient(); // explicit synchronous session fixtures retain their existing oracle path
        return S_FALSE;
    }
    if (!pending->HasLine())
    {
        CancelGradient();
        return S_FALSE;
    }
    if (_gradientCommitTicket || !_activeHostRequest || _activeHostRequest->ordinal != ordinal)
        return E_UNEXPECTED;
    const auto request = CaptureGradientRequest();
    _gradientCommitStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_gradientCommitStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _gradientCommitStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_gradientCommitStamp)
        return E_UNEXPECTED;
    const auto result = SubmitGradientJob(request, std::nullopt, _gradientCommitStamp);
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_gradientCommitStamp.value());
        _hostFinishedStamp = _gradientCommitStamp;
        _gradientCommitStamp.reset();
        return result;
    }
    _hostCommitStamp = _gradientCommitStamp;
    CancelGradientJobs();
    DropDrag<GradientDrag>();
    return S_OK;
}

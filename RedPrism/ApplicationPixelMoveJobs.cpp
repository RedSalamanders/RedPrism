#include "Application.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "PixelMoveWorkTestGate.h"
#include "PreparedRasterPreview.h"
#include "SharedPublicationMemory.h"
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
struct PixelMovePayload final : DeferredReleaseNode
{
    std::shared_ptr<const SharedPublicationMemory> memory; // All captured/output owners retire first.
    bool preparedDisplay = false;
    bool workerTargetCapture = false;
    RasterEditInput input;
    std::optional<JobToken> token;
    RasterEditResult committed;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView historyState;
    std::optional<PreparedRasterEdit> publication;
    RasterEditPreviewResult preview;
    std::shared_ptr<PixelMoveWorkTestGate> gate;
    DWORD ownerThread = 0;
    std::uint64_t preparationBytes = 0;
    std::uint64_t retirementBytes = 0;
    PixelMovePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold; // capture/preparation OOM still retires a potentially large graph
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<PixelMovePayload> owned(static_cast<PixelMovePayload*>(node)); };
    }
    ~PixelMovePayload()
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
            if (image.HasStorage())
                add(image.RetirementMemoryBytes() ? image.RetirementMemoryBytes()
                                                  : (std::numeric_limits<std::uint64_t>::max)());
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
        add(PixelMoveOpeningMemoryBytes(input.pixelMoveOpening));
        bytes = retirementBytes ? (std::max)({retained, retirementBytes, kDeferredReleaseThreshold}) : retained;
        if (gate)
            gate->payloadReleaseRequired.store(bytes >= kDeferredReleaseThreshold);
    }
};
#pragma warning(pop)

struct RetirePixelMovePayload final
{
    void operator()(PixelMovePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload, payload->preparedDisplay ? DeferredReleasePolicy::AlwaysOnOwner
                                                                           : DeferredReleasePolicy::SizeThreshold))
        {
            const std::unique_ptr<PixelMovePayload> owned(payload);
        }
    }
};
} // namespace

// One admission/capture protocol serves both kinds. Requests are primitive event-time values; source handles
// are captured only under the full held reservation. Run never dereferences the Application or mailboxes.
class Application::PixelMoveJob final : public Job
{
  public:
    PixelMoveJob(Application& owner, PixelMoveRequest request, std::uint64_t serial, std::uint64_t generation,
                 std::int64_t pixelLimit, std::uint64_t preparationBytes, std::optional<PreviewStamp> preview,
                 std::optional<HistoryWorkStamp> commit, std::uint64_t retirementBytes, bool prepareDisplay)
        : _owner(owner), _request(request), _serial(serial), _generation(generation), _pixelLimit(pixelLimit),
          _preparationBytes(preparationBytes), _retirementBytes(retirementBytes), _preview(preview), _commit(commit),
          _gate(owner._pixelMoveWorkTestGate &&
                        owner._pixelMoveWorkTestGate->kind ==
                            (commit ? PixelMoveWorkTestKind::Commit : PixelMoveWorkTestKind::Preview)
                    ? owner._pixelMoveWorkTestGate
                    : nullptr),
          _ownerThread(GetCurrentThreadId()), _prepareDisplay(prepareDisplay),
          _ownerIdentity(std::this_thread::get_id())
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
        if (_payload->workerTargetCapture)
        {
            // Commits and prepared previews already hold the complete document phase envelope.
            // Resolve and copy all potentially large layer/mask metadata on this worker only.
            if (!_payload->source || !_payload->token)
                return {JobStatus::Failed, E_UNEXPECTED};
            const auto* layer = _payload->source->layers.FindKey(_request.target);
            if (!layer || !layer->asset)
                return {JobStatus::Failed, E_ABORT};
            _payload->input.layer = *layer;
            _payload->token->target = TargetIdentity{layer->id,   layer->asset->image, layer->transform,
                                                     layer->mask, layer->isGroup,      layer->adjustment};
            if (!_commit)
                _payload->source.reset(); // Drop the extra whole-document owner on the worker after target capture.
            _payload->workerTargetCapture = false;
            if (_gate)
            {
                const bool onOwner = GetCurrentThreadId() == _ownerThread;
                _gate->targetCapturesOnOwner.fetch_add(onOwner ? 1u : 0u);
                _gate->targetCapturesOnWorker.fetch_add(onOwner ? 0u : 1u);
                _gate->capturedImageNameUnits.store(_payload->input.layer.asset->name.size());
                _gate->capturedMaskNameUnits.store(
                    _payload->input.layer.mask ? _payload->input.layer.mask->asset.name.size() : 0);
            }
        }
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
            if (_gate && _payload->committed.status == RasterEditStatus::Ready && _payload->committed.pixelMoveOpening)
                (_payload->input.pixelMoveOpening ? _gate->openingReuses : _gate->openingPreparations).fetch_add(1);
            if (_payload->committed.status == RasterEditStatus::Ready && _payload->token)
            {
                _payload->publication = PrepareRasterEditPublication(
                    RasterEditKind::PixelMove, false, _payload->source, _request.target, *_payload->token,
                    std::move(_payload->historyState), std::move(_payload->committed),
                    _request.duplicate ? L"Duplicate Pixels" : L"Move Pixels", context.cancellation);
                if (!_payload->publication)
                    _payload->committed.status =
                        context.cancellation.IsCancelled() ? RasterEditStatus::Cancelled : RasterEditStatus::Invalid;
            }
        }
        else
        {
            _payload->preview = RunRasterEditPreview(_payload->input, context.cancellation);
            _payload->memory = _payload->preview.memory;
            if (_gate && _payload->preview.status == RasterEditStatus::Ready && _payload->preview.pixelMoveOpening)
                (_payload->input.pixelMoveOpening ? _gate->openingReuses : _gate->openingPreparations).fetch_add(1);
        }
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
            if (!layer || layer->id != _request.target || _owner._session.IsMaskSelected())
            {
                _captureError = E_ABORT;
                return false;
            }
            auto owned = std::make_unique<PixelMovePayload>();
            _payload.reset(owned.release());
            _payload->preparedDisplay = _prepareDisplay;
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            _payload->preparationBytes = _preparationBytes;
            _payload->retirementBytes = _retirementBytes;
            _payload->bytes = (std::max)(_retirementBytes, kDeferredReleaseThreshold);
            auto& input = _payload->input;
            input.prepareDisplay = _prepareDisplay;
            input.ownerThread = _ownerIdentity;
            _original = ticket;
            _payload->workerTargetCapture = _commit.has_value() || _prepareDisplay;
            if (_payload->workerTargetCapture)
            {
                // This immutable root is included in the pre-capture commit/prepared-preview bound.
                _payload->source = _owner._session.Document();
                if (_gate)
                    _gate->rootOnlyCapturedOnOwner = GetCurrentThreadId() == _ownerThread;
            }
            else
            {
                // The unprepared-preview fixture path has no complete document-source envelope yet.
                input.layer = *layer;
                if (_gate)
                    _gate->targetCapturesOnOwner.fetch_add(1);
            }
            input.selection = _owner._session.Selection();
            input.canvasWidth = _owner._session.Document()->width;
            input.canvasHeight = _owner._session.Document()->height;
            input.pixelLimit = _pixelLimit;
            input.mask = false;
            input.kind = RasterEditKind::PixelMove;
            input.offset = _request.offset;
            input.duplicate = _request.duplicate;
            if (_owner._pixelMoveOpeningSerial == _serial && _owner._pixelMoveOpeningId == _request.opening &&
                _owner._pixelMoveOpeningGeneration == _generation)
                input.pixelMoveOpening = _owner._pixelMoveOpening;
            // Acquisition and pixelMove settings are separate clocks. A continued selected ordinal has a fresh
            // acquisition; a preview has a mailbox sequence. Neither is compared with the metadata sequence.
            const auto target = _payload->workerTargetCapture ? std::optional<Guid>{} : std::optional(_request.target);
            _payload->token =
                _owner._session.CaptureJobToken(target, _commit ? _commit->acquisition : _preview->sequence);
            if (_commit)
            {
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
        _owner._pixelMoveCompleting = true;
        const auto clearCompleting = wil::scope_exit([&]() noexcept { _owner._pixelMoveCompleting = false; });
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
                if (_prepareDisplay)
                {
                    if (!_payload->preview.memory || !_payload->preview.display)
                        error = E_UNEXPECTED;
                    else
                        error = static_cast<HRESULT>(
                            _payload->preview.memory->BindAfterCompletedWork(_owner._jobs, _original));
                    if (FAILED(error))
                    {
                        outcome = HostOutcome::Failed;
                        break;
                    }
                }
                if (_commit)
                {
                    const auto* pending = _owner._session.PendingPixelMove();
                    const bool adopt = pending && pending->raster && pending->publishedSequence == _request.sequence &&
                                       pending->publishedGeneration == _generation &&
                                       _owner._previewSource.lock() == pending->raster &&
                                       _owner._composition.StrokeLayerID() == std::optional<Guid>(_request.target);
                    produced = _payload->publication &&
                               _owner._session.ApplyPreparedPixelMoveIfCurrent(*_payload->publication, _request.opening,
                                                                               _request.sequence);
                    if (produced && adopt)
                        _owner.EndStrokePreview(_payload->publication->PreviewCommit());
                }
                else
                {
                    // Active older ticks may publish before the pending newest tick, as required by the mailbox.
                    // The cache belongs to the exact opening/source, independently of its frame settings sequence.
                    produced = _owner._session.PublishPixelMovePreview(
                        std::move(_payload->preview.stroke), _payload->token.value(), _request.opening,
                        _request.sequence, std::move(_payload->preview.display));
                    if (produced && _payload->preview.pixelMoveOpening)
                    {
                        _owner._pixelMoveOpening = _payload->preview.pixelMoveOpening;
                        _owner._pixelMoveOpeningSerial = _serial;
                        _owner._pixelMoveOpeningId = _request.opening;
                        _owner._pixelMoveOpeningGeneration = _generation;
                    }
                }
                if (!produced)
                    outcome = HostOutcome::Stale;
                break;
            case RasterEditStatus::Unchanged:
                if (_commit)
                    _owner._session.CancelPixelMove(); // valid exact acquisition, but no pixels/history to apply
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
            if (outcome != HostOutcome::Succeeded)
            {
                const auto* pending = _owner._session.PendingPixelMove();
                if (pending && pending->deferred && pending->opening == _request.opening)
                    _owner._session.CancelPixelMove(); // terminal failure drops this gesture, never a newer opening
            }
            _owner._pixelMoveCommitTicket.reset();
            _owner._pixelMoveCommitStamp.reset();
            _owner._pixelMoveOpening.reset(); // the admitted payload still holds it through worker retirement
            _owner._pixelMoveOpeningSerial = 0;
            _owner._pixelMoveOpeningId = 0;
            _owner._pixelMoveOpeningGeneration = 0;
            (void)_owner._historyWork.Finish(_commit.value());
            if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
                error = E_ABORT;
            _owner.ResumeHostHistory(_commit.value(), outcome, error);
            _owner.SyncStrokePreview();
            _owner.DocumentChanged();
        }
        else
        {
            _owner._pixelMovePreviewTicket.reset(); // acknowledgement, including an invalidated old epoch
            (void)_owner._pixelMovePreviews.Finish(_preview.value(), produced);
            _owner.SyncStrokePreview();
            _owner.SyncChrome(); // observes a new opening/tick and launches the latest pending request
            _owner.InvalidateFrame();
        }
        if (_owner._pixelMoveMailboxOpen && _owner._pixelMovePreviews.HasPending())
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
    }

  private:
    bool Current() const noexcept
    {
        const auto* pending = _owner._session.PendingPixelMove();
        if (_owner._shutdownStarted || _owner._session.DocumentSerial() != _serial ||
            _owner._session.Generation() != _generation || !pending || !pending->deferred || !pending->HasOffset() ||
            pending->openingGeneration != _generation || pending->opening != _request.opening ||
            pending->target != _request.target || _owner._session.IsMaskSelected())
            return false;
        if (!_commit)
            return _preview && _owner._pixelMovePreviews.CanPublish(_preview.value());
        return pending->sequence == _request.sequence && _owner._pixelMoveCommitStamp == _commit &&
               _owner._historyWork.IsCurrent(_commit.value()) && _owner._activeHostRequest &&
               _owner._activeHostRequest->ordinal == _commit->ordinal &&
               _owner._activeHostRequest->documentSerial == _serial;
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, PixelMoveWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner;
    const PixelMoveRequest _request;
    const std::uint64_t _serial;
    const std::uint64_t _generation;
    const std::int64_t _pixelLimit;
    const std::uint64_t _preparationBytes;
    const std::uint64_t _retirementBytes;
    const std::optional<PreviewStamp> _preview;
    const std::optional<HistoryWorkStamp> _commit;
    const std::shared_ptr<PixelMoveWorkTestGate> _gate;
    const DWORD _ownerThread;
    const bool _prepareDisplay;
    const std::thread::id _ownerIdentity;
    JobTicket _original;
    std::unique_ptr<PixelMovePayload, RetirePixelMovePayload> _payload;
    HRESULT _captureError = S_OK;
};

Application::PixelMoveRequest Application::CapturePixelMoveRequest() const noexcept
{
    const auto* pending = _session.PendingPixelMove();
    return PixelMoveRequest{pending->target, pending->opening, pending->sequence, pending->offset, pending->duplicate};
}

void Application::CancelPixelMoveJobs(bool keepOpening) noexcept
{
    (void)_pixelMovePreviews.Close();
    _pixelMoveMailboxOpen = false;
    _pixelMoveSeenSequence = 0;
    if (!keepOpening)
    {
        _pixelMoveOpening.reset(); // complete immutable node retires through the release lane
        _pixelMoveOpeningSerial = 0;
        _pixelMoveOpeningId = 0;
        _pixelMoveOpeningGeneration = 0;
    }
    if (_pixelMovePreviewTicket)
        (void)_jobs.Cancel(_pixelMovePreviewTicket.value());
    // Keep the ticket until acknowledgement; a reopened epoch cannot launch a second interactive worker.
}

void Application::RefreshPixelMoveJobs() noexcept
{
    const auto* pending = _session.PendingPixelMove();
    if (pending && pending->proceduralOpening)
        return; // The original physical parent owns all CPU work; the separately admitted frame consumer reads its
                // lift.
    if (!_pixelMoveCommitTicket && pending && pending->deferred && pending->openingGeneration != _session.Generation())
    {
        _session.CancelPixelMove();
        CancelPixelMoveJobs();
        return; // never acquire a new source for a gesture whose original image/selection was invalidated
    }
    if (_shutdownStarted || _pixelMoveCommitTicket || !pending || !pending->deferred || !pending->HasOffset())
    {
        if (_pixelMoveMailboxOpen)
            CancelPixelMoveJobs(_pixelMoveCommitTicket.has_value());
        return;
    }
    if (_pixelMoveMailboxOpen &&
        (_pixelMovePreviewSerial != _session.DocumentSerial() || _pixelMovePreviewOpening != pending->opening))
        CancelPixelMoveJobs();
    if (!_pixelMoveMailboxOpen)
    {
        if (!_pixelMovePreviews.Begin())
            return;
        _pixelMoveMailboxOpen = true;
        _pixelMovePreviewSerial = _session.DocumentSerial();
        _pixelMovePreviewOpening = pending->opening;
    }
    if (_pixelMoveSeenSequence != pending->sequence)
    {
        auto request = CapturePixelMoveRequest();
        if (_pixelMovePreviews.Offer(request).accepted)
            _pixelMoveSeenSequence = pending->sequence;
    }
    LaunchPixelMovePreview();
}

void Application::LaunchPixelMovePreview() noexcept
{
    if (_shutdownStarted || _pixelMoveCompleting || _pixelMovePreviewTicket || _pixelMoveCommitTicket)
        return;
    if (auto launch = _pixelMovePreviews.TakeNext())
    {
        const auto result = SubmitPixelMoveJob(launch->input, launch->stamp, std::nullopt);
        if (FAILED(result))
        {
            (void)_pixelMovePreviews.Finish(launch->stamp, false);
            ++_errorsShown;
            _lastErrorId =
                result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
        }
    }
}

HRESULT Application::SubmitPixelMoveJob(PixelMoveRequest request, std::optional<PreviewStamp> preview,
                                        std::optional<HistoryWorkStamp> commit) noexcept
{
    const auto* layer = _session.ActiveLayer();
    if (!layer || layer->id != request.target || !_session.Document() || !_session.Selection())
        return E_ABORT;
    const auto limit = _session.RasterEditPixelLimit(*layer, false);
    const auto preparation = PixelMoveMemoryBytes(*layer, _session.Selection().value(), _session.Document()->width,
                                                  _session.Document()->height, limit);
    const bool prepareDisplay = !commit && _pendingDisplayInitialized;
    std::uint64_t previewOwners = 0;
    if (prepareDisplay)
    {
        const auto grid = BrushStroke::PlanGrid(*layer, false, BrushSettings{}, _session.Document()->width,
                                                _session.Document()->height);
        const auto metadata = grid ? RasterPreviewMetadataMemoryBytes(grid->width, grid->height) : 0;
        const auto source = _session.Document()->RetirementMemoryBytes();
        if (!metadata || !source || source > (std::numeric_limits<std::uint64_t>::max)() - metadata)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        previewOwners = source + metadata;
        const auto opening = PixelMoveOpeningMemoryBytes(_pixelMoveOpening);
        if (opening > (std::numeric_limits<std::uint64_t>::max)() - previewOwners)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        previewOwners += opening;
    }
    const auto history = commit ? _session.history.EditMemoryBytes(_session.Document()) : 0;
    if (preparation == 0 || (commit && history == 0) ||
        history > (std::numeric_limits<std::uint64_t>::max)() - sizeof(PixelMoveJob) - sizeof(PixelMovePayload) ||
        preparation >
            (std::numeric_limits<std::uint64_t>::max)() - history - sizeof(PixelMoveJob) - sizeof(PixelMovePayload))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto baseMemory = preparation + history + sizeof(PixelMoveJob) + sizeof(PixelMovePayload);
    if (previewOwners > (std::numeric_limits<std::uint64_t>::max)() - baseMemory)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto memory = baseMemory + previewOwners;
    auto held = commit ? _session.RasterEditRetirementMemoryBytes() : 1;
    const auto addHeld = [&](std::uint64_t more) noexcept
    {
        if (more > (std::numeric_limits<std::uint64_t>::max)() - held)
            return false;
        held += more;
        return true;
    };
    const auto addRaster = [&](const Raster& raster) noexcept
    { return !raster.HasStorage() || (raster.RetirementMemoryBytes() && addHeld(raster.RetirementMemoryBytes())); };
    if (!held || !addHeld(PixelMoveOpeningMemoryBytes(_pixelMoveOpening)) ||
        (!commit &&
         ((!addHeld(_session.Selection()->loops.StorageBytes())) ||
          (layer->asset && (!addRaster(layer->asset->image) || !addRaster(layer->asset->thumbnail))) ||
          (layer->mask && (!addRaster(layer->mask->asset.image) || !addRaster(layer->mask->asset.thumbnail))) ||
          !addRaster(layer->shapeImage))) ||
        memory > ((std::numeric_limits<std::uint64_t>::max)() - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto retirement = held + memory * 2;
    try
    {
        if (_pixelMoveWorkTestGate &&
            _pixelMoveWorkTestGate->kind == (commit ? PixelMoveWorkTestKind::Commit : PixelMoveWorkTestKind::Preview) &&
            _pixelMoveWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<PixelMoveJob>(*this, request, _session.DocumentSerial(), _session.Generation(), limit,
                                           preparation, preview, commit, retirement, prepareDisplay);
        JobTicket ticket;
        JobOptions options{commit ? JobClass::Commit : JobClass::InteractivePreview, _session.DocumentSerial(), memory};
        options.reserveRetainedMemory = prepareDisplay;
        options.allowMemoryWaiting = !prepareDisplay; // A replacement cannot wait on its own opening/cache.
        const auto result = static_cast<HRESULT>(_jobs.Submit(job, options, ticket));
        if (SUCCEEDED(result))
        {
            if (commit)
                _pixelMoveCommitTicket = ticket;
            else
                _pixelMovePreviewTicket = ticket;
            if (_pixelMoveWorkTestGate && _pixelMoveWorkTestGate->kind ==
                                              (commit ? PixelMoveWorkTestKind::Commit : PixelMoveWorkTestKind::Preview))
                _pixelMoveWorkTestGate->reservationBytes = memory;
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // no job admitted; caller fails this complete request
    }
}

HRESULT Application::StartPixelMoveCommit(std::uint64_t ordinal) noexcept
{
    const auto* pending = _session.PendingPixelMove();
    if (!pending)
        return S_FALSE;
    if (!pending->deferred)
    {
        FinishPixelDrag(); // explicit synchronous session fixtures retain their existing oracle path
        return S_FALSE;
    }
    if (!pending->HasOffset())
    {
        CancelPixelDrag();
        return S_FALSE;
    }
    if (_pixelMoveCommitTicket || !_activeHostRequest || _activeHostRequest->ordinal != ordinal)
        return E_UNEXPECTED;
    const auto request = CapturePixelMoveRequest();
    _pixelMoveCommitStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_pixelMoveCommitStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _pixelMoveCommitStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_pixelMoveCommitStamp)
        return E_UNEXPECTED;
    const auto result = SubmitPixelMoveJob(request, std::nullopt, _pixelMoveCommitStamp);
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_pixelMoveCommitStamp.value());
        _hostFinishedStamp = _pixelMoveCommitStamp;
        _pixelMoveCommitStamp.reset();
        return result;
    }
    _hostCommitStamp = _pixelMoveCommitStamp;
    CancelPixelMoveJobs(true); // acknowledged immutable opening remains available to admitted commit capture
    DropDrag<PixelDrag>();
    return S_OK;
}

void Application::ExecutePixelNudge(Vec2 offset) noexcept
{
    if (!_activeHostRequest)
    {
        _hostExecutionError = E_UNEXPECTED;
        return;
    }
    if (_session.PendingPixelMove())
        CancelPixelDrag();
    if (!_session.BeginDeferredPixelMove())
        return; // eligibility refusal is the legacy no-op, with no history or pixel work
    _session.MovePixels(offset.x, offset.y);
    _hostAfterPrerequisite = HostStage::Complete; // never re-execute this press after worker acknowledgement
    _hostExecutionError = StartPixelMoveCommit(_activeHostRequest->ordinal);
    if (FAILED(_hostExecutionError))
    {
        ++_errorsShown;
        _lastErrorId = _hostExecutionError == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                                                                       : IDS_ERROR_PIXEL_EDIT;
        _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
        CancelPixelDrag();
    }
}

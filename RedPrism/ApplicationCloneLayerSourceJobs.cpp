#include "Application.h"

#include "Runtime.Tests.CloneReadbackTestGate.h"
#include "Selection.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

// Uses the same accepted pointer stream and retained-source graph as All Layers. Only source production differs:
// the active layer's existing placement sampler runs on a worker, without composition or GPU readback.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class Application::CloneLayerSourceJob final : public Job
{
  public:
    CloneLayerSourceJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening,
                        DocumentReadAuthority authority, std::uint64_t bytes) noexcept
        : _owner(owner), _stamp(stamp), _opening(opening), _authority(authority), _bytes(bytes),
          _gate(owner._cloneReadbackTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Whole admission precedes capture of any document/source owner.
        _payload->composite = RenderLayerPixels(_payload->image, _payload->sourceTransform, _authority.canvasWidth,
                                                _authority.canvasHeight, context.cancellation);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->composite.HasStorage())
            return {JobStatus::Failed, E_OUTOFMEMORY};
        if (_gate && _gate->holdAfterCopy)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, CloneReadbackTestGate::kMessage, 1, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, waits, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            return waited == WAIT_OBJECT_0 ? _gate->result
                                           : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp) ||
            _payload)
            return false;
        try
        {
            if (_gate && _gate->throwCaptureFailure)
                throw std::bad_alloc{};
            _payload.reset(std::make_unique<CloneReadbackState>().release());
            _payload->bytes = (std::max)(_bytes, kDeferredReleaseThreshold);
            _payload->opening = _opening;
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            _payload->document = _owner._session.Document().value();
            const ImageLayer* layer = _owner._session.ActiveLayer();
            if (!layer || layer->isGroup)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->image = layer->asset ? layer->asset->image : Raster{};
            _payload->sourceTransform = _owner._session.DisplayedTransform(*layer);
            if (_gate)
                _gate->captures.fetch_add(1);
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // Admitted noexcept capture fails the entire accepted stroke.
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        HostOutcome outcome = !Current() || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp) ||
                                      _owner._canvasBrushCancelledOpening == _opening
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        HRESULT error = outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled ? E_ABORT
                        : FAILED(_captureError)                                            ? _captureError
                                                                                           : result.error;
        if (outcome == HostOutcome::Succeeded)
        {
            error = _payload && _payload->composite.HasStorage() && _owner._utilityTicket
                        ? _owner._jobs.RetainMemory(_owner._utilityTicket.value(), _payload->lease)
                        : E_UNEXPECTED;
            if (FAILED(error))
                outcome = HostOutcome::Failed;
            else
            {
                _owner._canvasCloneSample = std::move(_payload);
                if (_gate && _gate->observeAdoption)
                    (void)PostThreadMessageW(_ownerThread, CloneReadbackTestGate::kMessage, 2, 0);
            }
        }
        _owner._utilityTicket.reset();
        _owner._utilityStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_opening);
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               captured && captured->kind == BrushKind::Clone && !captured->cloneAllLayers &&
               _owner._session.IsDocumentReadCurrent(_authority) &&
               _owner._session.ActiveLayerID() == captured->target && !_owner._session.IsMaskSelected();
    }

    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const DocumentReadAuthority _authority;
    const std::uint64_t _bytes;
    const std::shared_ptr<CloneReadbackTestGate> _gate;
    const DWORD _ownerThread;
    HRESULT _captureError = S_OK;
    std::unique_ptr<CloneReadbackState, CloneReadbackState::Retire> _payload;
};
#pragma warning(pop)

HRESULT Application::StartCloneLayerSource(std::uint64_t opening) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    const auto& document = _session.Document();
    const auto authority = _session.CaptureDocumentReadAuthority();
    const auto* layer = _session.ActiveLayer();
    if (!captured || captured->kind != BrushKind::Clone || captured->cloneAllLayers || !captured->cloneOffset ||
        !document || !authority || !layer || layer->isGroup || _utilityTicket || _canvasCloneSample ||
        !_activeHostRequest)
        return E_UNEXPECTED;
    const auto source = document->RetirementMemoryBytes();
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(document->width) * document->height * 4u);
    const auto contiguousSource =
        layer->asset ? PixelBuffer::RequiredBytes(static_cast<std::size_t>(layer->asset->image.Stride()) *
                                                  layer->asset->image.Height())
                     : 0;
    const auto metadata = sizeof(CloneLayerSourceJob) + sizeof(CloneReadbackState) + 8192ULL;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!source || !output || source > maximum - output || contiguousSource > maximum - source - output ||
        metadata > maximum - source - output - contiguousSource)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto bytes = source + output + contiguousSource + metadata;
    const auto ordinal = _activeHostRequest->ordinal;
    auto stamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Selection);
    if (!stamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        stamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!stamp)
        return E_UNEXPECTED;
    _utilityStamp = stamp;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        std::unique_ptr<Job> job =
            std::make_unique<CloneLayerSourceJob>(*this, stamp.value(), opening, authority.value(), bytes);
        JobTicket ticket;
        result = _jobs.Submit(job, {JobClass::Stroke, _session.DocumentSerial(), bytes}, ticket);
        if (SUCCEEDED(result))
            _utilityTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Refused descriptor construction precedes source capture, pixels, alignment and history.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(stamp.value());
        _hostFinishedStamp = stamp;
        _utilityStamp.reset();
        return result;
    }
    _hostCommitStamp = stamp;
    _hostAfterPrerequisite = HostStage::CanvasBrush;
    return S_OK;
}

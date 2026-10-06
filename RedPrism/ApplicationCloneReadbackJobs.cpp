#include "Application.h"

#include "CloneReadbackTestGate.h"
#include "DocumentReadbackJob.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

CloneReadbackState::~CloneReadbackState()
{
    if (gate)
    {
        gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
        gate->retired.store(true, std::memory_order_release);
    }
}

class Application::CloneReadbackJob final : public DocumentReadbackJob
{
  public:
    CloneReadbackJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, DocumentReadAuthority authority,
                     D3D11_BOX region, std::uint64_t bytes) noexcept
        : DocumentReadbackJob(owner._jobs, owner._gpuWork, owner._composition, region), _owner(owner), _stamp(stamp),
          _opening(opening), _authority(authority), _bytes(bytes), _gate(owner._cloneReadbackTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    ~CloneReadbackJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->document, _payload->pixels, _payload->image);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_opening);
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               captured && captured->kind == BrushKind::Clone && _owner._session.IsDocumentReadCurrent(_authority) &&
               _owner._session.ActiveLayerID() == captured->target && !_owner._session.IsMaskSelected();
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        try
        {
            if (_gate && _gate->throwCaptureFailure)
                throw std::bad_alloc{};
            _payload.reset(std::make_unique<CloneReadbackState>().release());
            _payload->bytes = (std::max)(_bytes, kDeferredReleaseThreshold);
            _payload->opening = _opening;
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            document = _owner._session.Document().value();
            if (_gate)
                _gate->captures.fetch_add(1);
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY; // Admitted construction boundary; no stroke or history is published.
        }
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        if (_gate)
        {
            _gate->preparations.fetch_add(1);
            if (FAILED(reason))
                _gate->recoveries.fetch_add(1);
        }
        const HRESULT result = _owner.PrepareUtilityDevice(reason);
        if (SUCCEEDED(result) && reason == S_OK && _gate && _gate->recoveries.load() < _gate->rebindsBeforeCopy)
        {
            if (!PostThreadMessageW(_ownerThread, CloneReadbackTestGate::kMessage, 0, 0))
                return HRESULT_FROM_WIN32(GetLastError());
        }
        return result;
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 ||
            image.Width() != _authority.canvasWidth || image.Height() != _authority.canvasHeight)
            return {JobStatus::Failed, E_UNEXPECTED};
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
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!Current())
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp) || _owner._canvasBrushCancelledOpening == _opening)
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        if (_payload)
            _payload->composite = std::move(image);
        if (outcome == HostOutcome::Succeeded)
        {
            error = _payload ? _owner._jobs.RetainMemory(_owner._utilityTicket.value(), _payload->lease) : E_UNEXPECTED;
            if (FAILED(error))
                outcome = HostOutcome::Failed;
            else
            {
                TransferReadbackOwners(_payload->document, _payload->pixels, _payload->image);
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

    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const DocumentReadAuthority _authority;
    const std::uint64_t _bytes;
    const std::shared_ptr<CloneReadbackTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<CloneReadbackState, CloneReadbackState::Retire> _payload;
};

HRESULT Application::StartCloneReadback(std::uint64_t opening) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    const auto& document = _session.Document();
    const auto authority = _session.CaptureDocumentReadAuthority();
    if (!captured || captured->kind != BrushKind::Clone || !captured->cloneOffset || !document || !authority ||
        _utilityTicket || _canvasCloneSample || !_activeHostRequest)
        return E_UNEXPECTED;
    const D3D11_BOX region{
        0, 0, 0, static_cast<unsigned int>(document->width), static_cast<unsigned int>(document->height), 1};
    auto bytes = DocumentReadbackJob::RequiredMemory(region);
    const auto source = document->RetirementMemoryBytes();
    const auto metadata = sizeof(CloneReadbackJob) + sizeof(CloneReadbackState) + 8192ULL;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!bytes || !source || source > maximum - bytes || metadata > maximum - bytes - source)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    bytes += source + metadata;
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
            std::make_unique<CloneReadbackJob>(*this, stamp.value(), opening, authority.value(), region, bytes);
        JobTicket ticket;
        result = _jobs.Submit(job, {JobClass::Stroke, _session.DocumentSerial(), bytes}, ticket);
        if (SUCCEEDED(result))
            _utilityTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor admission fails before source capture or GPU recording; preserve the accepted receipt.
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

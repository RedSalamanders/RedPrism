#include "Application.h"

#include "DocumentReadbackJob.h"
#include "WandLayerHostTestGate.h"
#include "WandLayerSelectionWork.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct WandCompositePayload final : DeferredReleaseNode
{
    std::optional<WandLayerSelectionInput> input;
    std::optional<PreparedWandLayerSelection> publication;
    CanvasDocument readbackDocument;
    PixelBuffer readbackPixels;
    Raster readbackImage;
    Raster completedImage; // Separate from the empty transfer destination used by the base destructor.
    WandFailure failure = WandFailure::None;
    std::shared_ptr<WandLayerHostTestGate> gate;
    DWORD ownerThread = 0;
    WandCompositePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<WandCompositePayload> owned(static_cast<WandCompositePayload*>(node)); };
    }
    ~WandCompositePayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireWandCompositePayload final
{
    void operator()(WandCompositePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<WandCompositePayload> owned(payload);
        }
    }
};
bool AddWandBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (bytes > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += bytes;
    return true;
}
} // namespace

class Application::WandCompositeHostJob final : public DocumentReadbackJob
{
  public:
    WandCompositeHostJob(Application& owner, HistoryWorkStamp stamp, HostWandCompositeNewInput request,
                         D3D11_BOX region, std::uint64_t retirement) noexcept
        : DocumentReadbackJob(owner._jobs, owner._gpuWork, owner._composition, region), _owner(owner), _stamp(stamp),
          _request(request), _retirement(retirement), _authority(owner._session.CaptureHistoryRestoreAuthority()),
          _gate(owner._wandLayerHostTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    ~WandCompositeHostJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->readbackDocument, _payload->readbackPixels, _payload->readbackImage);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._wandLayerHostStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        try
        {
            _payload.reset(new WandCompositePayload);
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareWandCompositeSelectionInput(_request.point, _request.settings,
                                                                                 _request.antialiased);
            const auto* source = _payload->input ? _payload->input->ReadbackDocument() : nullptr;
            if (!source)
                return E_ABORT;
            document = *source;
            if (_gate)
                _gate->captures.fetch_add(1);
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY; // Complete admitted construction boundary; live history remains unchanged.
        }
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        if (_gate)
        {
            _gate->readbackPrepares.fetch_add(1);
            if (FAILED(reason))
                _gate->readbackRecoveries.fetch_add(1);
        }
        const HRESULT ready = _owner.PrepareUtilityDevice(reason);
        // Test-only owner message: RequestTile enqueues the real readback after this callback. The shared
        // message pump dispatches the queued message before the next GPU pump, without timers or polling.
        if (SUCCEEDED(ready) && reason == S_OK && _gate &&
            _gate->readbackRecoveries.load() < _gate->requestedReadbackRebinds &&
            !PostThreadMessageW(_ownerThread, WandLayerHostTestGate::kReadbackMessage, 0,
                                reinterpret_cast<LPARAM>(_gate.get())))
            return HRESULT_FROM_WIN32(GetLastError());
        return ready;
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true; // Map/Unmap and utility release precede selection/history preparation.
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (!_payload || !_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<WandLayerHostTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        _payload->publication = DocumentSession::PrepareWandCompositeSelectionPublication(
            std::move(_payload->input.value()), image, &_payload->failure, context.cancellation, observer);
        if (context.cancellation.IsCancelled() || _payload->failure == WandFailure::Cancelled)
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, _payload->failure == WandFailure::TooDetailed
                                           ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                           : E_OUTOFMEMORY};
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, WandLayerHostTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            return waited == WAIT_OBJECT_0 ? _gate->result
                                           : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return {};
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        if (_payload)
            _payload->completedImage = std::move(image);
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!Current())
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedWandLayerSelectionIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage,
                                        _payload && _payload->failure == WandFailure::TooDetailed
                                            ? IDS_ERROR_WAND_DETAILED
                                            : IDS_ERROR_WAND_MEMORY);
        _owner._wandLayerHostTicket.reset();
        _owner._wandLayerHostStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const HostWandCompositeNewInput _request;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<WandLayerHostTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<WandCompositePayload, RetireWandCompositePayload> _payload;
};

HRESULT Application::StartWandCompositeSelection(const HostWandCompositeNewInput& input) noexcept
{
    if (_wandLayerHostTicket || !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto& document = _session.Document();
    if (!document || !std::isfinite(input.point.x) || !std::isfinite(input.point.y))
        return S_OK;
    if (!input.settings.sampleAllLayers || input.settings.sampleSize > WandSampleSize::FiveByFive)
        return E_INVALIDARG;
    if (input.point.x < 0 || input.point.y < 0 || input.point.x >= document->width || input.point.y >= document->height)
        return S_OK;
    const auto plan = _session.PlanWandCompositeSelectionMemory(input.point, input.settings);
    const D3D11_BOX region{0, 0, 0, static_cast<UINT>(document->width), static_cast<UINT>(document->height), 1};
    const auto readback = DocumentReadbackJob::RequiredMemory(region);
    if (!plan || !readback)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t memory = 0;
    if (EvaluateWandLayerSelectionAdmission(*plan, SelectionMode::New, document->selection.has_value(), memory) !=
        WandLayerSelectionAdmission::Ready)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    auto retirement = plan->retirementBytes;
    const auto metadata = sizeof(WandCompositeHostJob) + sizeof(WandCompositePayload) + 8192ull;
    // Charge readback storage in addition to the conservative selection phases, including the retained GPU
    // source snapshot and complete CPU aggregate. Admission never assumes a live document keeps owners alive.
    const auto sourceBytes = _session.history.RetirementMemoryBytes(document);
    if (!sourceBytes || !AddWandBytes(memory, readback) || !AddWandBytes(memory, metadata) ||
        !AddWandBytes(memory, sourceBytes) || !AddWandBytes(retirement, readback) ||
        !AddWandBytes(retirement, metadata) || !AddWandBytes(retirement, sourceBytes))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _wandLayerHostStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _wandLayerHostStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        if (_wandLayerHostTestGate)
            _wandLayerHostTestGate->reservationBytes = memory;
        std::unique_ptr<Job> job =
            std::make_unique<WandCompositeHostJob>(*this, _wandLayerHostStamp.value(), input, region, retirement);
        JobTicket ticket;
        result =
            static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Selection, _session.DocumentSerial(), memory}, ticket));
        if (SUCCEEDED(result))
            _wandLayerHostTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor-only refusal before immutable source capture, pixel allocation or GPU recording.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_wandLayerHostStamp.value());
        _hostFinishedStamp = _wandLayerHostStamp;
        _wandLayerHostStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _wandLayerHostStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

#include "Application.h"
#include "ChromeHost.h"

#include "RecordedDocumentReadbackJob.h"
#include "SharedPublicationMemory.h"
#include "WandLayerHostTestGate.h"
#include "WandLayerSelectionWork.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <exception>
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
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<SharedPublicationMemory> output;
    std::optional<WandLayerSelectionInput> input;
    std::optional<PreparedWandLayerSelection> publication;
    PreparedSelectionOutline outline;
    std::uint64_t outlineBytes = 0;
    std::vector<CanvasOverlay::OutlineLoop> retiredReferenceOutline;
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
        if (payload && !TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
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

class Application::WandCompositeHostJob final : public RecordedDocumentReadbackJob
{
  public:
    WandCompositeHostJob(Application& owner, HistoryWorkStamp stamp, HostWandCompositeNewInput request,
                         D3D11_BOX region, UtilityReadbackAdmission::Envelope envelope,
                         GpuResidencyReservation reservation, std::uint64_t outlineLimit) noexcept
        : RecordedDocumentReadbackJob(owner._jobs, owner._gpuWork, owner._recordedCommands, owner._utilityReadback,
                                      owner._graphGpuBudget, envelope, std::move(reservation), region,
                                      owner._renderer.IsWarp()),
          _owner(owner), _stamp(stamp), _request(request), _retirement(envelope.cpuBytes), _outlineLimit(outlineLimit),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._wandLayerHostTestGate),
          _outputGate(owner._wandOutputLifetimeTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    ~WandCompositeHostJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->readbackDocument, _payload->readbackPixels, _payload->readbackImage,
                                   _payload->memory);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._wandLayerHostStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    JobResult PrepareReadbackOpening(JobContext&) override
    {
        auto payload = std::make_unique<WandCompositePayload>();
        payload->output = SharedPublicationMemory::CreateForWork();
        payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
        payload->gate = _gate;
        payload->ownerThread = _ownerThread;
        _payload.reset(payload.release());
        return {};
    }
    HRESULT BindReadbackMemoryForRetirement(JobTicket original, SharedPublicationMemory& temporary) noexcept override
    {
        _originalOutputTicket = original;
        const auto output = _payload && _payload->publication
                                ? _payload->publication->OutputAllocationBytes() + _payload->outlineBytes
                                : 0;
        return output ? _payload->output->BindPersistentOutputForRetirement(_owner._jobs, original, output, temporary)
                      : temporary.BindForRetirement(_owner._jobs, original);
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp))
            return E_ABORT;
        try
        {
            if (!_payload)
                return E_UNEXPECTED;
            if (_gate)
            {
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareWandCompositeSelectionInput(_request.point, _request.settings,
                                                                                 _request.antialiased, _request.mode);
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
        if (SUCCEEDED(ready) && _gate && _gate->readbackRecoveries.load() < _gate->requestedReadbackRebinds &&
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
            std::move(_payload->input.value()), image, &_payload->failure, context.cancellation, observer, nullptr,
            _payload->output);
        if (context.cancellation.IsCancelled() || _payload->failure == WandFailure::Cancelled)
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, _payload->failure == WandFailure::TooDetailed
                                           ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                           : E_OUTOFMEMORY};
        if (_payload->publication->ChangesSelection())
        {
            const auto* selected = _payload->publication->SelectionForDisplayForWork();
            SelectionLoops source = selected ? selected->loops : SelectionLoops{};
            const auto declared = PreparedSelectionOutline::OutputMemory(source);
            if (!declared || declared.value() > _outlineLimit)
                return {JobStatus::Failed, E_OUTOFMEMORY}; // Refuse before allocating outside original admission.
            auto outline = PreparedSelectionOutline::Prepare(std::move(source), _payload->output, context.cancellation);
            if (!outline)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            if (_gate)
            {
                _gate->outlinePreparedOffOwner.store(GetCurrentThreadId() != _ownerThread);
                const auto& points = outline->Points();
                _gate->preparedPointBuffer.store(!points.empty() && !points[0].empty() ? points[0].data() : nullptr);
            }
            _payload->outlineBytes = outline->DeclaredOutputBytes();
            _payload->outline = std::move(outline.value());
        }
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
        if (outcome == HostOutcome::Succeeded && _payload && _payload->outline.HasStorage())
            (void)_owner.AdoptPreparedSelectionOutlineOwner(std::move(_payload->outline),
                                                            _payload->retiredReferenceOutline);
        if (_outputGate && outcome == HostOutcome::Succeeded && _payload && _payload->publication &&
            _payload->publication->OutputAllocationBytes() != 0)
        {
            const auto bytes = _payload->output->Bytes();
            _outputGate->allOutputsBoundToOriginal =
                _outputGate->allOutputsBoundToOriginal &&
                _payload->output->BoundTo(_owner._jobs, _originalOutputTicket) &&
                bytes == _payload->publication->OutputAllocationBytes() + _payload->outlineBytes;
            _outputGate->publishedOutputBytes += bytes;
            ++_outputGate->publishedOutputs;
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
    const std::uint64_t _outlineLimit;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<WandLayerHostTestGate> _gate;
    const std::shared_ptr<WandLayerHostTestGate> _outputGate;
    const DWORD _ownerThread;
    JobTicket _originalOutputTicket;
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
    const auto plan = _session.PlanWandCompositeSelectionMemory(input.point, input.settings, input.mode);
    const D3D11_BOX region{0, 0, 0, static_cast<UINT>(document->width), static_cast<UINT>(document->height), 1};
    const auto readback = RecordedDocumentReadbackJob::RequiredMemory(region);
    if (!plan || !readback)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t memory = 0;
    if (EvaluateWandLayerSelectionAdmission(plan.value(), input.mode, document->selection.has_value(), memory) !=
        WandLayerSelectionAdmission::Ready)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto metadata = sizeof(WandCompositeHostJob) + sizeof(WandCompositePayload) +
                          2 * SharedPublicationMemory::OwnershipAllowance() + 8192ull;
    // Charge readback storage in addition to the conservative selection phases, including the retained GPU
    // source snapshot and complete CPU aggregate. Admission never assumes a live document keeps owners alive.
    const auto sourceBytes = _session.history.RetirementMemoryBytes(document);
    if (!sourceBytes || !AddWandBytes(memory, readback) || !AddWandBytes(memory, metadata) ||
        !AddWandBytes(memory, sourceBytes))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto outline = PreparedSelectionOutline::AdmissionMemory(plan.value());
    const auto legacy = _chrome ? _chrome->LegacySelectionOutlineMemoryOwner() : std::optional<std::uint64_t>{0};
    if (!outline || !legacy || !AddWandBytes(memory, outline.value()) || !AddWandBytes(memory, legacy.value()))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    UtilityReadbackAdmission::Envelope envelope;
    GpuResidencyReservation reservation;
    RETURN_IF_FAILED(PrepareRecordedUtilityAdmission(document.value(), memory, envelope, reservation));
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
            _wandLayerHostTestGate->reservationBytes = envelope.cpuBytes;
        std::unique_ptr<Job> job = std::make_unique<WandCompositeHostJob>(
            *this, _wandLayerHostStamp.value(), input, region, envelope, std::move(reservation), outline.value());
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job,
                                                   {.kind = JobClass::Selection,
                                                    .serialKey = _session.DocumentSerial(),
                                                    .memoryBytes = envelope.cpuBytes,
                                                    .deferredRetirement = true,
                                                    .allowMemoryWaiting = true,
                                                    .reserveRetainedMemory = true},
                                                   ticket));
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

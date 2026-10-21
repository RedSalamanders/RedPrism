#include "Application.h"

#include "DocumentDraft.h"

#include "PublicationGraphAllocator.h"
#include "RasterWork.h"
#include "RecordedDocumentReadbackJob.h"
#include "Runtime.Tests.UtilityWorkTestGate.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
void RecordMergeRetirement(const std::shared_ptr<UtilityWorkTestGate>& gate, DWORD ownerThread) noexcept
{
    if (gate)
    {
        gate->retiredNodes.fetch_add(1);
        if (GetCurrentThreadId() == ownerThread)
            gate->retiredOwnerNodes.fetch_add(1);
    }
}
} // namespace

#pragma warning(push)
#pragma warning(disable : 4324)
struct Application::MergeProbePayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> memory; // Last after all captured source/plan owners.
    std::optional<CanvasDocument> source;
    std::optional<Guid> active;
    std::optional<JobToken> token;
    std::unordered_set<Guid> selected;
    std::optional<DocumentSession::MergePlan> plan;
    CanvasDocument flat;
    std::shared_ptr<UtilityWorkTestGate> gate;
    DWORD ownerThread = 0;
    MergeProbePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<MergeProbePayload> owned(static_cast<MergeProbePayload*>(node)); };
    }
    ~MergeProbePayload()
    {
        RecordMergeRetirement(gate, ownerThread);
    }
};
#pragma warning(pop)

class Application::MergePlanProbeJob final : public Job
{
  public:
    MergePlanProbeJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _generation(owner._session.Generation()), _retirement(retirement),
          _gate(owner._utilityWorkTestGate), _ownerThread(GetCurrentThreadId()), _active(owner._session.ActiveLayerID())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            _memory = SharedPublicationMemory::CreateForWork();
            auto allocated = std::make_unique<MergeProbePayload>();
            allocated->memory = _memory;
            allocated->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            allocated->gate = _gate;
            allocated->ownerThread = _ownerThread;
            const auto retire = [](MergeProbePayload* payload) noexcept
            {
                if (!payload)
                    return;
                if (GetCurrentThreadId() == payload->ownerThread)
                {
                    if (!TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
                        std::terminate();
                }
                else
                {
                    const std::unique_ptr<MergeProbePayload> owned(payload);
                }
            };
            _payload = std::shared_ptr<MergeProbePayload>{allocated.release(), retire,
                                                          PublicationGraphAllocator<MergeProbePayload>{_memory}};
            return {}; // Original admitted Compute allocates before the owner captures source/selection.
        }
        if (_gate && _gate->throwProbeAllocationFailure)
            throw std::bad_alloc{};
        _payload->plan = DocumentSession::PlanMergeForWork(_payload->source, _payload->active, _payload->selected,
                                                           context.cancellation);
        if (_payload->plan)
        {
            auto& flat = _payload->flat;
            flat.id = _payload->source->id;
            flat.width = _payload->source->width;
            flat.height = _payload->source->height;
            flat.resolution = _payload->source->resolution;
            auto draft = CanvasDocumentDraft::Rebuild(flat, _payload->source->layers.size(), context.cancellation);
            if (!draft)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_INVALIDARG};
            for (const auto& layer : _payload->source->layers)
            {
                if (context.cancellation.IsCancelled())
                    return {JobStatus::Cancelled};
                if (!_payload->plan->removed.contains(layer.id))
                    continue;
                auto copy = layer;
                if (copy.parentID && !_payload->plan->removed.contains(copy.parentID.value()))
                    copy.parentID.reset();
                if (copy.maskSourceID && !_payload->plan->removed.contains(copy.maskSourceID.value()))
                    copy.maskSourceID.reset();
                if (!draft.value().Append(std::move(copy)))
                    return {JobStatus::Failed, E_INVALIDARG};
            }
            auto sealed = draft.value().Seal(context.cancellation);
            if (!sealed)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_INVALIDARG};
            flat = std::move(sealed.value());
        }
        if (_gate)
            _gate->probePreparations.fetch_add(1);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_gate && _gate->holdProbe)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, UtilityWorkTestGate::kMessage, 0, 0))
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
    bool DeferRetirement(JobResult, JobTicket original) noexcept override
    {
        _ticket = original;
        return false;
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept override
    {
        if (&scheduler != &_owner._jobs)
            std::terminate();
        if (_memory && !_memory->IsBound() && FAILED(_memory->BindForRetirement(scheduler, original)))
            std::terminate();
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_captured || !_payload || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (_gate)
            {
                _gate->probeReservationHeld = _gate->probeReservationBytes != 0 &&
                                              _owner._jobs.ReservedMemoryBytes() >= _gate->probeReservationBytes;
                if (_gate->throwProbeCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->token = _owner._session.CaptureJobToken(std::nullopt, _stamp.acquisition);
            _payload->source = _owner._session.Document();
            _payload->active = _active;
            _payload->selected = _owner._session.SelectedLayerIDs();
            _captured = true;
            if (_payload->token && _owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY;
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (_memory && !_memory->IsBound() && FAILED(_memory->BindForRetirement(_owner._jobs, _ticket)))
            std::terminate();
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!Current() || (_payload && _payload->token &&
                           !_owner._session.CanApplyJobResult(_payload->token.value(), _stamp.acquisition)))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (_captureError && outcome == HostOutcome::Succeeded)
        {
            outcome = HostOutcome::Failed;
            error = _captureError;
        }
        if (outcome == HostOutcome::Succeeded && _payload && _payload->plan)
        {
            const auto submitted = _owner.StartPreparedMergeReadback(_stamp, _generation, _payload);
            if (SUCCEEDED(submitted))
                return; // replacement ticket owns the same history ordinal through final apply/drop
            outcome = HostOutcome::Failed;
            error = submitted;
        }
        _owner.FinishMergeWork(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.ActiveLayerID() == _active;
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation, _retirement;
    const std::shared_ptr<UtilityWorkTestGate> _gate;
    const DWORD _ownerThread;
    const std::optional<Guid> _active;
    std::shared_ptr<SharedPublicationMemory> _memory;
    std::shared_ptr<MergeProbePayload> _payload;
    JobTicket _ticket;
    bool _captured = false;
    std::int32_t _captureError = 0;
};

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct MergePayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> memory;
    DocumentHistory::StateView history;
    std::optional<DocumentSession::PreparedMergePublication> publication;
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    CanvasDocument readbackDocument;
    PixelBuffer readbackPixels;
    Raster readbackImage;
    Raster completedImage; // Terminal output is separate from the mandatory empty transfer destination.
    std::shared_ptr<UtilityWorkTestGate> gate;
    DWORD ownerThread = 0;
    MergePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<MergePayload> owned(static_cast<MergePayload*>(node)); };
    }
    ~MergePayload()
    {
        RecordMergeRetirement(gate, ownerThread);
    }
};
#pragma warning(pop)
struct RetireMergePayload final
{
    void operator()(MergePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
    }
};
} // namespace

class Application::MergeReadbackJob final : public RecordedDocumentReadbackJob
{
  public:
    MergeReadbackJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t generation, D3D11_BOX region,
                     UtilityReadbackAdmission::Envelope envelope, GpuResidencyReservation reservation,
                     std::shared_ptr<MergeProbePayload> probe) noexcept
        : RecordedDocumentReadbackJob(owner._jobs, owner._gpuWork, owner._recordedCommands, owner._utilityReadback,
                                      owner._graphGpuBudget, envelope, std::move(reservation), region,
                                      owner._renderer.IsWarp()),
          _owner(owner), _stamp(stamp), _generation(generation), _retirement(envelope.cpuBytes),
          _gate(owner._utilityWorkTestGate), _ownerThread(GetCurrentThreadId()),
          _active(owner._session.ActiveLayerID()), _probe(std::move(probe))
    {
    }
    ~MergeReadbackJob() override
    {
        if (_payload)
            TransferReadbackOwners(_payload->readbackDocument, _payload->readbackPixels, _payload->readbackImage,
                                   _payload->memory);
        // The base acknowledges mappings/releases utility targets on the owner. CPU owners retire separately.
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.ActiveLayerID() == _active;
    }
    JobResult PrepareReadbackOpening(JobContext&) override
    {
        _payload.reset(std::make_unique<MergePayload>().release());
        _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
        _payload->gate = _gate;
        _payload->ownerThread = _ownerThread;
        return {};
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        if (!Current() || !_payload || _owner._historyWork.CancelRequested(_stamp) || !_probe->token ||
            !_owner._session.CanApplyJobResult(_probe->token.value(), _stamp.acquisition))
            return E_ABORT;
        try
        {
            if (_gate && _gate->throwCaptureFailure)
                throw std::bad_alloc{};
            _payload->history = _owner._session.history.CaptureState();
            std::swap(document, _probe->flat); // worker-built subset; acknowledgement precedes this ownership move
            if (_gate)
            {
                _gate->captured.store(true);
                _gate->readbackCaptures.fetch_add(1);
            }
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY; // complete construction boundary; live document/history are unchanged
        }
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true; // copied pixels are authoritative after the final map acknowledgement
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (!_payload)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            if (static_cast<UtilityWorkTestGate*>(opaque)->throwHistoryStage == stage)
                                throw std::bad_alloc{};
                        },
                        _gate.get()};
        _payload->publication = DocumentSession::PrepareMergePublication(
            _probe->source, _probe->active, _probe->token.value(), std::move(_payload->history), _probe->plan.value(),
            image, _payload->status, context.cancellation, observer, ReadbackOutputLifetime());
        image = {}; // full readback pixels are no longer needed and release on this worker
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, _payload->status == DocumentSession::MergePublicationStatus::TooLarge
                                           ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                           : E_FAIL};
        if (_gate && !_gate->holdProbe && _gate->visits.fetch_add(1) == 0)
        {
            if (!PostThreadMessageW(_ownerThread, UtilityWorkTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
            return _gate->result;
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
        if (!Current() || !_owner._session.CanApplyJobResult(_probe->token.value(), _stamp.acquisition))
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
                  !_owner._session.ApplyPreparedMergeIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _owner.FinishMergeWork(_stamp, outcome, error,
                               _payload && _payload->status == DocumentSession::MergePublicationStatus::TooLarge);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation;
    const std::uint64_t _retirement;
    const std::shared_ptr<UtilityWorkTestGate> _gate;
    const DWORD _ownerThread;
    const std::optional<Guid> _active;
    const std::shared_ptr<MergeProbePayload> _probe;
    std::unique_ptr<MergePayload, RetireMergePayload> _payload;
};

HRESULT Application::StartPreparedMergeReadback(HistoryWorkStamp stamp, std::uint64_t generation,
                                                std::shared_ptr<MergeProbePayload> source) noexcept
{
    if (!source || !source->source || !source->plan || _utilityStamp != stamp)
        return E_UNEXPECTED;
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const D3D11_BOX region{
        0, 0, 0, static_cast<UINT>(_session.Document()->width), static_cast<UINT>(_session.Document()->height), 1};
    const auto readback = RecordedDocumentReadbackJob::RequiredMemory(region);
    const auto history = _session.history.EditMemoryBytes(_session.Document());
    auto held = _session.MergeRetirementMemoryBytes();
    std::uint64_t memory =
        sizeof(MergeReadbackJob) + sizeof(MergePayload) + SharedPublicationMemory::OwnershipAllowance() + 64 * 1024;
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if ((size && count > maximum / size) || count * size > maximum - memory)
            return false;
        memory += count * size;
        return true;
    };
    // Full readback/crop/snapshot, detached history and source/plan copies coexist. Kept conservative until H2.
    if (readback && history && held && add(3, readback) && add(1, history) &&
        add(_session.Document()->layers.size(), 4 * sizeof(Guid) + 1024) &&
        held <= (std::numeric_limits<std::uint64_t>::max)() / 2 &&
        memory <= ((std::numeric_limits<std::uint64_t>::max)() - held * 2) / 2)
    {
        held = held * 2 + memory * 2;
        try
        {
            UtilityReadbackAdmission::Envelope envelope;
            GpuResidencyReservation reservation;
            result = PrepareRecordedUtilityAdmission(source->flat, held, envelope, reservation);
            if (SUCCEEDED(result))
            {
                std::unique_ptr<Job> job = std::make_unique<MergeReadbackJob>(
                    *this, stamp, generation, region, envelope, std::move(reservation), std::move(source));
                JobTicket ticket;
                if (_utilityWorkTestGate)
                    _utilityWorkTestGate->readbackReservationBytes = envelope.cpuBytes;
                // The predecessor's captured allocation remains resident until this phase retires.
                // Waiting on that owned dependency cannot make space: refuse once on insufficient admission.
                result = static_cast<HRESULT>(_jobs.Submit(job,
                                                           {.kind = JobClass::Commit,
                                                            .serialKey = stamp.documentSerial,
                                                            .memoryBytes = envelope.cpuBytes,
                                                            .deferredRetirement = true,
                                                            .allowMemoryWaiting = false,
                                                            .reserveRetainedMemory = true},
                                                           ticket));
                if (SUCCEEDED(result))
                    _utilityTicket = ticket;
            }
        }
        catch (const std::bad_alloc&)
        {
            result = E_OUTOFMEMORY;
        }
    }
    return result;
}

void Application::FinishMergeWork(HistoryWorkStamp stamp, HostOutcome outcome, std::int32_t error,
                                  bool budgetRefusal) noexcept
{
    if (outcome == HostOutcome::Failed)
    {
        if (budgetRefusal)
            ReportBudgetRefusal();
        else
            _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_ERROR_FLATTEN);
    }
    _utilityTicket.reset();
    _utilityStamp.reset();
    (void)_historyWork.Finish(stamp);
    DocumentChanged();
    ResumeHostHistory(stamp, outcome, error);
}

HRESULT Application::StartMergeReadback() noexcept
{
    if (_utilityTicket || !_session.Document())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _utilityStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_utilityStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _utilityStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_utilityStamp)
        return E_UNEXPECTED;
    auto memory = _session.history.EditMemoryBytes(_session.Document());
    auto held = _session.MergeRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > maximum / size) || count * size > maximum - memory)
            return false;
        memory += count * size;
        return true;
    };
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    // Metadata-only planning. Pixel/readback/history publication allocation starts only when a plan exists.
    if (memory && held &&
        add(1, sizeof(MergePlanProbeJob) + sizeof(MergeProbePayload) + SharedPublicationMemory::OwnershipAllowance() +
                   64 * 1024) &&
        add(_session.Document()->layers.size(), 1024) && add(_session.SelectedLayerIDs().size(), 1024) &&
        memory <= maximum / 2 && memory * 2 <= maximum - held)
    {
        held += memory * 2;
        try
        {
            if (_utilityWorkTestGate)
                _utilityWorkTestGate->probeReservationBytes = held;
            std::unique_ptr<Job> job = std::make_unique<MergePlanProbeJob>(*this, _utilityStamp.value(), held);
            JobTicket ticket;
            result = static_cast<HRESULT>(_jobs.Submit(job,
                                                       {.kind = JobClass::Commit,
                                                        .serialKey = _session.DocumentSerial(),
                                                        .memoryBytes = held,
                                                        .deferredRetirement = true,
                                                        .allowMemoryWaiting = true,
                                                        .reserveRetainedMemory = true},
                                                       ticket));
            if (SUCCEEDED(result))
                _utilityTicket = ticket;
        }
        catch (const std::bad_alloc&)
        {
            result = E_OUTOFMEMORY;
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_utilityStamp.value());
        _hostFinishedStamp = _utilityStamp;
        _utilityStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _utilityStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

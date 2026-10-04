#include "Application.h"

#include "LayerMetadataTestGate.h"
#include "LayerMetadataWork.h"
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
struct MetadataPayload final : DeferredReleaseNode
{
    std::optional<LayerMetadataInput> input;
    std::optional<PreparedLayerMetadata> publication;
    std::shared_ptr<LayerMetadataTestGate> gate;
    DWORD ownerThread = 0;
    MetadataPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<MetadataPayload> owned(static_cast<MetadataPayload*>(node)); };
    }
    ~MetadataPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireMetadataPayload final
{
    void operator()(MetadataPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<MetadataPayload> owned(payload);
        }
    }
};
} // namespace

class Application::LayerMetadataJob final : public Job
{
  public:
    LayerMetadataJob(Application& owner, HistoryWorkStamp stamp, LayerMetadataKind kind, Guid target,
                     std::wstring_view name, double opacity, LayerBlendMode blendMode,
                     std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _kind(kind), _target(target), _name(name), _opacity(opacity),
          _blendMode(blendMode), _retirement(retirement), _authority(owner._session.CaptureHistoryRestoreAuthority()),
          _gate(owner._layerMetadataTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // successful admission precedes owner capture on the resumable boundary
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver historyObserver;
        LayerMetadataPrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            historyObserver = {[](void* opaque, DocumentHistory::PrepareStage stage)
                               {
                                   auto& gate = *static_cast<LayerMetadataTestGate*>(opaque);
                                   if (gate.throwHistoryStage == stage)
                                   {
                                       gate.faultHits.fetch_add(1);
                                       throw std::bad_alloc{};
                                   }
                               },
                               _gate.get()};
        if (_gate && _gate->throwMetadataStage)
            observer = {[](void* opaque, LayerMetadataPrepareStage stage)
                        {
                            auto& gate = *static_cast<LayerMetadataTestGate*>(opaque);
                            if (gate.throwMetadataStage == stage)
                            {
                                gate.faultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        auto publication = PrepareLayerMetadataPublication(std::move(_payload->input.value()), context.cancellation,
                                                           historyObserver, observer);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!publication)
            return {JobStatus::Failed, E_FAIL};
        _payload->publication.emplace(std::move(publication.value()));
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, LayerMetadataTestGate::kMessage, 0, 0))
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
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _payload || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            _payload.reset(std::make_unique<MetadataPayload>().release());
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
            auto input = _owner._session.PrepareLayerMetadataInput(_kind, _target, _name, _opacity, _blendMode);
            // No further access to the borrowed request text, on either thread, after this owner capture.
            _name = {};
            if (!input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
            if (_gate)
                _gate->captures.fetch_add(1);
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
    void Completed(JobResult result) noexcept override
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
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedLayerMetadataIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_LAYER_METADATA);
        _owner._layerMetadataTicket.reset();
        _owner._layerMetadataStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.DocumentChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._layerMetadataStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const LayerMetadataKind _kind;
    const Guid _target;
    // Rename's selected HostRequest owns this storage through acknowledgement. This view is consulted only
    // by DeferCompletion on the owner; workers never read it. Scalar metadata jobs keep this empty.
    std::wstring_view _name;
    const double _opacity;
    const LayerBlendMode _blendMode;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<LayerMetadataTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<MetadataPayload, RetireMetadataPayload> _payload;
    std::int32_t _captureError = 0;
};

HRESULT Application::StartLayerMetadata(LayerMetadataKind kind, Guid target, std::wstring_view name, double opacity,
                                        LayerBlendMode blendMode) noexcept
{
    if (_layerMetadataTicket || kind > LayerMetadataKind::SetBlendMode ||
        (kind == LayerMetadataKind::Rename && !_activeHostRequest))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (kind == LayerMetadataKind::Rename)
    {
        const auto* rename = std::get_if<HostLayerRenameInput>(&_activeHostRequest.value().input.Input());
        if (!rename || rename->id != target || !rename->name || rename->name.value().data() != name.data() ||
            rename->name.value().size() != name.size())
            return E_INVALIDARG;
    }
    if (!_session.Document())
        return S_OK;
    if (!_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()) ||
        _session.history.CaptureState().Depth() != 0)
    {
        // This slice admits only settled documents. Preserve the reference commands' live-gesture and nested
        // transaction behavior until their sealed worker authority is implemented; never reject a valid edit
        // merely because a brush, warp, shape, lasso or outline gesture is still open. The host already orders
        // this command after any admitted predecessor. These reference constructors still block root activation.
        if (_historyWork.Busy())
            return E_ABORT;
        switch (kind)
        {
        case LayerMetadataKind::Rename:
            _session.RenameLayer(target, name);
            break;
        case LayerMetadataKind::ToggleVisibility:
            _session.ToggleVisibility(target);
            break;
        case LayerMetadataKind::SetOpacity:
            _session.SetOpacity(opacity);
            break;
        case LayerMetadataKind::SetBlendMode:
            _session.SetBlendMode(blendMode);
            break;
        }
        DocumentChanged();
        return S_OK;
    }
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _layerMetadataStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_layerMetadataStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _layerMetadataStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_layerMetadataStamp)
        return E_UNEXPECTED;
    auto memory = _session.LayerMetadataMemoryBytes(kind, kind == LayerMetadataKind::Rename ? name.size() : 0);
    auto retirement = _session.HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if (memory && retirement && sizeof(LayerMetadataJob) + sizeof(MetadataPayload) <= maximum - memory)
    {
        memory += sizeof(LayerMetadataJob) + sizeof(MetadataPayload);
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_layerMetadataTestGate)
                    _layerMetadataTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job = std::make_unique<LayerMetadataJob>(
                    *this, _layerMetadataStamp.value(), kind, target, name, opacity, blendMode, retirement);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _layerMetadataTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_layerMetadataStamp.value());
        _hostFinishedStamp = _layerMetadataStamp;
        _layerMetadataStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _layerMetadataStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

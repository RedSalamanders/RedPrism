#include "Application.h"

#include "LayerMetadataTestGate.h"
#include "LayerMetadataWork.h"
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
                     std::wstring_view name, double opacity, LayerBlendMode blendMode, std::uint64_t retirement,
                     OpacityMetadataIntent intent, std::optional<double> opening,
                     std::optional<OpacityMetadataContext> opacityContext) noexcept
        : _owner(owner), _stamp(stamp), _kind(kind), _target(target), _name(name), _opacity(opacity),
          _blendMode(blendMode), _retirement(retirement), _intent(intent), _opening(opening),
          _opacityContext(opacityContext), _authority(owner._session.CaptureLayerMetadataAuthority(true)),
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
        if (_refreshResidency)
        {
            if (_gate && _gate->throwResidencyRefresh)
                throw std::bad_alloc{};
            const bool refreshed =
                _payload->publication && _payload->publication->RefreshHistoryResidency(context.cancellation);
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                   : refreshed                        ? JobResult{}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
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
        if (_gate && _gate->lateResidencySource.HasStorage())
        {
            // Harness-only actual worker cache publication AFTER the private history sample, before owner apply.
            // Production leaves the gate absent; this models a concurrent consumer without UI pixel work.
            if (!_gate->lateResidencySource.Data())
                return {JobStatus::Failed, E_FAIL};
            _gate->residentGrowths.fetch_add(1);
        }
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
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_payload)
        {
            if (!_payload->publication)
                return false;
            const auto decision = _payload->publication->TryAcquireHistoryResidency(_residencyPermit);
            if (decision == DocumentHistory::ResidencyValidation::Ready)
                return false; // same owner drain immediately calls Completed; no message pumping or callbacks
            if (decision == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            _refreshResidency = true;
            if (_gate)
            {
                _gate->residencyRefreshes.fetch_add(1);
                _gate->refreshReservationHeld.store(_owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes &&
                                                    _gate->reservationBytes != 0);
            }
            if (_owner._jobs.Resume(ticket))
            {
                if (_gate && _gate->cancelResidencyRefresh)
                    (void)_owner._jobs.Cancel(ticket); // harness-only: exact retained ticket, before resumed stage
                return true;
            }
            _captureError = E_UNEXPECTED;
            return false;
        }
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
            auto input =
                _intent == OpacityMetadataIntent::Preview
                    ? _owner._session.PrepareLayerOpacityPreviewInput(_target, _opacity)
                : _intent == OpacityMetadataIntent::TypedCommit
                    ? _owner._session.PrepareTypedLayerOpacityInput(_target, _opening.value(), _opacity)
                    : _owner._session.PreparePreservedLayerMetadataInput(_kind, _target, _name, _opacity, _blendMode);
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
        const auto releasePermit = wil::scope_exit([this]() noexcept { _residencyPermit.Reset(); });
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
        else if (outcome == HostOutcome::Succeeded && _opacityContext &&
                 _opacityContext->action == OpacityAcknowledgement::Preview &&
                 _owner.NewerOpacityInputPending(_target, _opacityContext->order))
        {
            outcome = HostOutcome::Cancelled; // superseded input never publishes or overwrites newer field text
            error = E_ABORT;
        }
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedLayerMetadataIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        // No callback/chrome/display/source read retains this gate, including stale/failed/cancelled results.
        _residencyPermit.Reset();
        if (outcome == HostOutcome::Succeeded && _opacityContext)
            _owner.AcknowledgeOpacityMetadata(_target, _opacity, _opacityContext.value());
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
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyLayerMetadataAuthority(_authority) &&
               (!_opacityContext || (_owner._opacityEdit && _owner._opacityEdit->opening == _opacityContext->opening &&
                                     _owner._opacityEdit->layer == _target &&
                                     _owner._opacityEdit->documentSerial == _owner._session.DocumentSerial()));
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
    const OpacityMetadataIntent _intent;
    // Admission validates this original value. Capture happens only on the owner after reservation, then
    // the worker receives a sealed input; no worker follows the host's live typed-edit state.
    const std::optional<double> _opening;
    const std::optional<OpacityMetadataContext> _opacityContext;
    const LayerMetadataAuthority _authority;
    const std::shared_ptr<LayerMetadataTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<MetadataPayload, RetireMetadataPayload> _payload;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _refreshResidency = false; // scheduler stage fences protect the same-record worker/owner handoff
    std::int32_t _captureError = 0;
};

HRESULT Application::StartLayerMetadata(LayerMetadataKind kind, Guid target, std::wstring_view name, double opacity,
                                        LayerBlendMode blendMode, OpacityMetadataIntent intent,
                                        std::optional<double> opening,
                                        std::optional<OpacityMetadataContext> opacityContext) noexcept
{
    if (intent > OpacityMetadataIntent::TypedCommit ||
        (intent != OpacityMetadataIntent::Recorded && kind != LayerMetadataKind::SetOpacity) ||
        (intent == OpacityMetadataIntent::TypedCommit &&
         (!opening || !std::isfinite(opening.value()) || opening.value() < 0.0 || opening.value() > 1.0)) ||
        (intent != OpacityMetadataIntent::TypedCommit && opening))
        return E_INVALIDARG;
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
    if (!_session.CanApplyLayerMetadataAuthority(_session.CaptureLayerMetadataAuthority(true)))
        return E_ABORT;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _layerMetadataStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_layerMetadataStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _layerMetadataStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_layerMetadataStamp)
        return E_UNEXPECTED;
    auto memory = _session.PreservedLayerMetadataMemoryBytes(kind, kind == LayerMetadataKind::Rename ? name.size() : 0);
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
                std::unique_ptr<Job> job =
                    std::make_unique<LayerMetadataJob>(*this, _layerMetadataStamp.value(), kind, target, name, opacity,
                                                       blendMode, retirement, intent, opening, opacityContext);
                JobTicket ticket;
                result = static_cast<HRESULT>(_jobs.Submit(
                    job,
                    {intent == OpacityMetadataIntent::Preview ? JobClass::InteractivePreview : JobClass::Commit,
                     _session.DocumentSerial(), memory},
                    ticket));
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

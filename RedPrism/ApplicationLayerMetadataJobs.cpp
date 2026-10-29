#include "Application.h"

#include "AccountedPublicationObject.h"

#include "LayerMetadataWork.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct MetadataPayload final : DeferredReleaseNode
{
    // The node/control allocator also retains temporary through actual destruction. These fields keep
    // the original owners readable throughout worker, owner apply/drop and joined shutdown.
    std::shared_ptr<SharedPublicationMemory> temporary;
    std::shared_ptr<SharedPublicationMemory> output;
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

#pragma warning(pop)
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class Application::LayerMetadataJob final : public Job
{
  public:
    LayerMetadataJob(Application& owner, HistoryWorkStamp stamp, LayerMetadataKind kind, Guid target,
                     std::wstring_view name, double opacity, LayerBlendMode blendMode, std::uint64_t retirement,
                     std::uint64_t admission, OpacityMetadataIntent intent, std::optional<double> opening,
                     std::optional<OpacityMetadataContext> opacityContext) noexcept
        : _owner(owner), _stamp(stamp), _kind(kind), _target(target), _name(name), _opacity(opacity),
          _blendMode(blendMode), _retirement(retirement), _admission(admission), _intent(intent), _opening(opening),
          _opacityContext(opacityContext),
          _authority(owner._session.CaptureLayerMetadataAuthority(!IsLayerFlipMetadata(kind))),
          _gate(owner._layerMetadataTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            const auto fixed = 2 * SharedPublicationMemory::OwnershipAllowance() +
                               AccountedPublicationObjectAllowance<MetadataPayload>();
            if (_admission <= fixed)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            auto temporary = SharedPublicationMemory::CreateForWork();
            // Every selected new output allocation refuses before it can exceed this original envelope.
            // Useful work remains covered by the complete descriptor, including borrowed source overlap.
            auto output = SharedPublicationMemory::CreateForWork(_admission - fixed);
            auto payload = MakeAccountedPublicationObject<MetadataPayload>(
                temporary, (std::max)(_retirement, kDeferredReleaseThreshold), 0);
            payload->temporary = std::move(temporary);
            payload->output = std::move(output);
            payload->gate = _gate;
            payload->ownerThread = _ownerThread;
            _payload = std::move(payload);
            if (_gate)
            {
                _gate->accountingOutput = _payload->output;
                _gate->accountingTemporary = _payload->temporary;
                _gate->originalAccountingBound = false;
                _gate->accountingObserved = false;
            }
            return {}; // Original admission covers this worker preparation before owner capture.
        }
        if (!_captureAttempted)
            return {JobStatus::Failed, E_UNEXPECTED};
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
        auto publication = DocumentSession::PrepareLayerMetadataPublication(
            std::move(_payload->input.value()), context.cancellation, historyObserver, observer, _payload->output);
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
    bool DeferRetirement(JobResult, JobTicket original) noexcept override
    {
        _ticket = original;
        return false;
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket original) noexcept override
    {
        _ticket = original;
        BindOriginalAccounting(original);
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (!_payload)
            return false;
        if (_captureAttempted)
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
            _captureAttempted = true;
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
                : IsLayerFlipMetadata(_kind)
                    ? _owner._session.PrepareLayerMetadataInput(_kind, _target)
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
        // Useful producers have joined; no newly allocated root can escape before its original ticket binds.
        BindOriginalAccounting(_ticket);
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
    void BindOriginalAccounting(JobTicket original) noexcept
    {
        if (!_payload || _payload->temporary->IsBound())
            return;
        if (!_payload->output->SealLocalAllocations())
            std::terminate(); // No useful producer may allocate after the original graph is partitioned.
        const auto local = _payload->output->LocalAllocationBytes();
        const auto allowance = SharedPublicationMemory::OwnershipAllowance();
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (local > maximum - allowance)
            std::terminate();
        const auto output = local ? local + allowance : 0;
        if (_admission <= allowance || output > _admission - allowance)
            std::terminate(); // A producer outside the complete admitted envelope must never refund live data.
        const auto result = output ? _payload->output->BindPersistentOutputForRetirement(_owner._jobs, original, output,
                                                                                         *_payload->temporary)
                                   : _payload->temporary->BindForRetirement(_owner._jobs, original);
        if (result < 0)
            std::terminate(); // Preserve the original reservation through every actual graph/control lifetime.
        if (_gate)
        {
            _gate->accountingTicket = original;
            _gate->accountingOutputBytes = output;
            _gate->originalAccountingBound =
                _payload->temporary->BoundTo(_owner._jobs, original) &&
                _payload->temporary->Bytes() == _admission - output &&
                (!output || (_payload->output->BoundTo(_owner._jobs, original) && _payload->output->Bytes() == output));
        }
    }
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
    const std::uint64_t _admission;
    const OpacityMetadataIntent _intent;
    // Admission validates this original value. Capture happens only on the owner after reservation, then
    // the worker receives a sealed input; no worker follows the host's live typed-edit state.
    const std::optional<double> _opening;
    const std::optional<OpacityMetadataContext> _opacityContext;
    const LayerMetadataAuthority _authority;
    const std::shared_ptr<LayerMetadataTestGate> _gate;
    const DWORD _ownerThread;
    std::shared_ptr<MetadataPayload> _payload;
    JobTicket _ticket;
    bool _captureAttempted = false;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _refreshResidency = false; // scheduler stage fences protect the same-record worker/owner handoff
    std::int32_t _captureError = 0;
};
#pragma warning(pop)

bool Application::MetadataAccountingSettledForTest() noexcept
{
    // A completed job retires its temporary/input phase. Its live document/history/weak graph still
    // owns the output charge. Zero total reservations is therefore not a successful-retirement oracle.
    const auto gate = _layerMetadataTestGate;
    if (_jobs.InUiCallback() || !_jobs.IsIdle())
        return false;
    if (!gate || !gate->originalAccountingBound)
        return _jobs.ReservedMemoryBytes() == 0;
    if (gate->accountingObserved)
        return true;
    if (!gate->accountingTemporary.expired())
        return false;
    auto output = gate->accountingOutput.lock();
    const auto bytes = gate->accountingOutputBytes;
    if (output && bytes &&
        (!output->BoundTo(_jobs, gate->accountingTicket) || output->Bytes() != bytes ||
         bytes < SharedPublicationMemory::OwnershipAllowance() ||
         output->LocalAllocationBytes() > bytes - SharedPublicationMemory::OwnershipAllowance()))
        return false;
    if (output && !bytes && (output->IsBound() || output->LocalAllocationBytes() != 0))
        return false;
    gate->accountingTemporary.reset();
    gate->accountingOutput.reset();
    (void)DrainCompletions(_jobs); // Consume only actual destruction acknowledgements; no useful work remains.
    if (!_jobs.IsIdle() ||
        (output && bytes &&
         (output->ResidentBytes() < bytes || output->ResidentBytes() != _jobs.ReservedMemoryBytes())))
        return false;
    output.reset();
    (void)DrainCompletions(_jobs);
    gate->accountingObserved = true;
    return _jobs.IsIdle();
}

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
    if (_layerMetadataTicket || kind > LayerMetadataKind::FlipVertical ||
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
    if (!_session.CanApplyLayerMetadataAuthority(_session.CaptureLayerMetadataAuthority(!IsLayerFlipMetadata(kind))))
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
    auto memory =
        IsLayerFlipMetadata(kind)
            ? _session.LayerMetadataMemoryBytes(kind)
            : _session.PreservedLayerMetadataMemoryBytes(kind, kind == LayerMetadataKind::Rename ? name.size() : 0);
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
                                                       blendMode, retirement, memory, intent, opening, opacityContext);
                JobTicket ticket;
                JobOptions options{intent == OpacityMetadataIntent::Preview ? JobClass::InteractivePreview
                                                                            : JobClass::Commit,
                                   _session.DocumentSerial(), memory};
                options.deferredRetirement = true;
                options.reserveRetainedMemory = true;
                options.allowMemoryWaiting = false; // Replacement cannot wait forever on its retained predecessor.
                result = static_cast<HRESULT>(_jobs.Submit(job, options, ticket));
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

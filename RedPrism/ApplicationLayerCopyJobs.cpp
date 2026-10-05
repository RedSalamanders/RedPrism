#include "Application.h"

#include "LayerCopyWorkTestGate.h"
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
struct LayerCopyPayload final : DeferredReleaseNode
{
    std::optional<LayerCopyInput> input;
    std::optional<LayerCopyResult> prepared;
    std::shared_ptr<LayerCopyWorkTestGate> gate;
    DWORD ownerThread = 0;
    LayerCopyPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<LayerCopyPayload> owned(static_cast<LayerCopyPayload*>(value)); };
    }
    ~LayerCopyPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireLayerCopyPayload final
{
    void operator()(LayerCopyPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<LayerCopyPayload> owned(payload);
        }
    }
};
} // namespace

class Application::LayerCopyJob final : public Job
{
  public:
    LayerCopyJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t retirement)
        : _owner(owner), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()),
          _retirement(retirement), _gate(owner._layerCopyWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_refreshResidency)
        {
            if (_gate && _gate->throwResidencyRefresh)
                throw std::bad_alloc{};
            const bool refreshed = _payload && _payload->prepared && _payload->prepared->publication &&
                                   _payload->prepared->publication->RefreshHistoryResidency(context.cancellation);
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                   : refreshed                        ? JobResult{}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
        if (!_payload)
            return {}; // Whole admission precedes the owner snapshot and every pixel/history allocation.
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        const DocumentHistory::PrepareObserver observer{[](void* value, DocumentHistory::PrepareStage stage)
                                                        {
                                                            auto* gate = static_cast<LayerCopyWorkTestGate*>(value);
                                                            if (gate && gate->historyFailure == stage)
                                                            {
                                                                gate->historyFaultHits.fetch_add(1);
                                                                throw std::bad_alloc{};
                                                            }
                                                        },
                                                        _gate.get()};
        if (_gate)
            _gate->preparations.fetch_add(1);
        _payload->prepared.emplace(DocumentSession::PrepareLayerCopyPublication(
            std::move(_payload->input.value()), context.executor, context.cancellation, {},
            _gate ? observer : DocumentHistory::PrepareObserver{}));
        _payload->input.reset(); // Any remaining copied source and history owners retire on this worker.
        if (_gate && _gate->lateResidencySource.HasStorage() && _payload->prepared->publication)
        {
            if (!_gate->lateResidencySource.Data())
                return {JobStatus::Failed, E_FAIL};
            _gate->residentGrowths.fetch_add(1);
        }
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() || _payload->prepared->status == LayerCopyStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_payload)
        {
            if (!_payload->prepared || !_payload->prepared->publication)
                return false;
            const auto decision = _payload->prepared->publication->TryAcquireHistoryResidency(_residencyPermit);
            if (decision == DocumentHistory::ResidencyValidation::Ready)
                return false;
            if (decision == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            _refreshResidency = true;
            if (_gate)
            {
                _gate->residencyRefreshes.fetch_add(1);
                _gate->refreshTicketMatches.store(_gate->capturedTicket == ticket);
                _gate->refreshReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
            }
            if (_owner._jobs.Resume(ticket))
            {
                if (_gate && _gate->cancelResidencyRefresh)
                    (void)_owner._jobs.Cancel(ticket);
                return true;
            }
            _captureError = E_UNEXPECTED;
            return false;
        }
        try
        {
            if (_gate)
            {
                _gate->captures.fetch_add(1);
                _gate->capturedTicket = ticket;
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() == _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload.reset(std::make_unique<LayerCopyPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            auto input = _owner._session.PrepareLayerCopyInput();
            if (!input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // Explicit noexcept snapshot boundary fails this entire operation.
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        const auto releasePermit = wil::scope_exit([this]() noexcept { _residencyPermit.Reset(); });
        HostOutcome outcome = !Current() || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        HRESULT error = FAILED(_captureError) ? _captureError : result.error;
        bool changed = false;
        if (outcome == HostOutcome::Succeeded)
        {
            const auto status = _payload && _payload->prepared ? _payload->prepared->status : LayerCopyStatus::Invalid;
            if (status == LayerCopyStatus::Ready && _payload->prepared->publication)
            {
                changed = _owner._session.ApplyPreparedLayerCopyIfCurrent(_payload->prepared->publication.value());
                if (!changed)
                    outcome = HostOutcome::Stale;
            }
            else if (status != LayerCopyStatus::Unchanged)
            {
                outcome = HostOutcome::Failed;
                error = status == LayerCopyStatus::Memory     ? E_OUTOFMEMORY
                        : status == LayerCopyStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                              : E_INVALIDARG;
            }
        }
        _residencyPermit.Reset(); // Release before notices, chrome, callbacks, or another owner pump.
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled)
            error = E_ABORT;
        _owner._layerCopyTicket.reset();
        _owner._layerCopyStamp.reset();
        if (changed)
            _owner.DocumentChanged();
        else
        {
            _owner.SyncChrome();
            _owner._frameInvalidated = true;
        }
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._layerCopyStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.CanApplyImageImport(_authority);
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, LayerCopyWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // Only owner callbacks use Application; Run sees detached state and the immutable gate.
    const HistoryWorkStamp _stamp;
    const ImageImportAuthority _authority;
    const std::uint64_t _retirement;
    const std::shared_ptr<LayerCopyWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<LayerCopyPayload, RetireLayerCopyPayload> _payload;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _refreshResidency = false;
    HRESULT _captureError = S_OK;
};

HRESULT Application::StartLayerCopy() noexcept
{
    if (!_activeHostRequest || _layerCopyTicket)
        return E_UNEXPECTED;
    const auto* layer = _session.ActiveLayer();
    if (!_session.Document() || !layer || layer->isGroup || (_session.Selection() && _session.Selection()->IsEmpty()) ||
        (_session.Selection() && (_session.IsMaskSelected() ? !layer->mask : !layer->asset)))
        return S_FALSE;
    const auto prepared = _session.LayerCopyPreparationMemoryBytes();
    const auto held = _session.ImageImportRetirementMemoryBytes();
    const auto refresh = _session.history.EditMemoryBytes(_session.Document());
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr auto metadata = sizeof(LayerCopyJob) + sizeof(LayerCopyPayload);
    if (!prepared || !held || !refresh || prepared > maximum - refresh || prepared + refresh > maximum - metadata ||
        prepared + refresh + metadata > (maximum - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto whole = prepared + refresh + metadata; // Original preparation and one private refresh coexist.
    const auto ordinal = _activeHostRequest->ordinal;
    _layerCopyStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_layerCopyStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _layerCopyStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_layerCopyStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        if (_layerCopyWorkTestGate && _layerCopyWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job = std::make_unique<LayerCopyJob>(*this, _layerCopyStamp.value(), held + whole * 2);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), whole}, ticket));
        if (SUCCEEDED(result))
        {
            _layerCopyTicket = ticket;
            if (_layerCopyWorkTestGate)
                _layerCopyWorkTestGate->reservationBytes = whole;
        }
    }
    catch (const std::bad_alloc&)
    {
        // No snapshot was taken; fail the complete request admission.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_layerCopyStamp.value());
        _hostFinishedStamp = _layerCopyStamp;
        _layerCopyStamp.reset();
        return result;
    }
    _hostCommitStamp = _layerCopyStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}
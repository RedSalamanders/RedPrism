#include "Application.h"

#include "Runtime.Tests.MaskWorkTestGate.h"
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
struct MaskPayload final : DeferredReleaseNode
{
    std::optional<MaskEditInput> input;
    std::optional<MaskEditResult> prepared;
    std::shared_ptr<MaskWorkTestGate> gate;
    DWORD ownerThread = 0;
    MaskPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<MaskPayload> owned(static_cast<MaskPayload*>(value)); };
    }
    ~MaskPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireMaskPayload final
{
    void operator()(MaskPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<MaskPayload> owned(payload);
        }
    }
};
} // namespace

class Application::MaskJob final : public Job
{
  public:
    MaskJob(Application& owner, HistoryWorkStamp stamp, bool revealing, std::uint64_t retirement)
        : _owner(owner), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()), _revealing(revealing),
          _retirement(retirement), _gate(owner._maskWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
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
                                                            auto* gate = static_cast<MaskWorkTestGate*>(value);
                                                            if (gate && gate->historyFailure == stage)
                                                            {
                                                                gate->historyFaultHits.fetch_add(1);
                                                                throw std::bad_alloc{};
                                                            }
                                                        },
                                                        _gate.get()};
        _payload->prepared.emplace(DocumentSession::PrepareMaskEditPublication(
            std::move(_payload->input.value()), context.executor, context.cancellation, {},
            _gate ? observer : DocumentHistory::PrepareObserver{}));
        _payload->input.reset(); // Any remaining copied source and history owners retire on this worker.
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() || _payload->prepared->status == MaskEditStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() == _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload.reset(std::make_unique<MaskPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            auto input = _owner._session.PrepareMaskEditInput(_revealing);
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
        HostOutcome outcome = !Current() || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        HRESULT error = FAILED(_captureError) ? _captureError : result.error;
        bool changed = false;
        if (outcome == HostOutcome::Succeeded)
        {
            const auto status = _payload && _payload->prepared ? _payload->prepared->status : MaskEditStatus::Invalid;
            if (status == MaskEditStatus::Ready && _payload->prepared->publication)
            {
                changed = _owner._session.ApplyPreparedMaskEditIfCurrent(_payload->prepared->publication.value());
                if (!changed)
                    outcome = HostOutcome::Stale;
            }
            else if (status != MaskEditStatus::Unchanged)
            {
                outcome = HostOutcome::Failed;
                error = status == MaskEditStatus::Memory     ? E_OUTOFMEMORY
                        : status == MaskEditStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                             : E_INVALIDARG;
            }
        }
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled)
            error = E_ABORT;
        _owner._maskTicket.reset();
        _owner._maskStamp.reset();
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
        return !_owner._shutdownStarted && _owner._maskStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.CanApplyImageImport(_authority);
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, MaskWorkTestGate::kMessage, 0, 0))
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
    const bool _revealing;
    const std::uint64_t _retirement;
    const std::shared_ptr<MaskWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<MaskPayload, RetireMaskPayload> _payload;
    HRESULT _captureError = S_OK;
};

HRESULT Application::StartMask(bool revealing) noexcept
{
    if (!_activeHostRequest || _maskTicket)
        return E_UNEXPECTED;
    const auto* layer = _session.ActiveLayer();
    if (!_session.Document() || !layer || layer->mask)
        return S_FALSE;
    const auto prepared = _session.MaskEditPreparationMemoryBytes();
    const auto held = _session.ImageImportRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr auto metadata = sizeof(MaskJob) + sizeof(MaskPayload);
    if (!prepared || !held || prepared > maximum - metadata || prepared + metadata > (maximum - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto whole = prepared + metadata;
    const auto ordinal = _activeHostRequest->ordinal;
    _maskStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_maskStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _maskStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_maskStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        if (_maskWorkTestGate && _maskWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job = std::make_unique<MaskJob>(*this, _maskStamp.value(), revealing, held + whole * 2);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), whole}, ticket));
        if (SUCCEEDED(result))
        {
            _maskTicket = ticket;
            if (_maskWorkTestGate)
                _maskWorkTestGate->reservationBytes = whole;
        }
    }
    catch (const std::bad_alloc&)
    {
        // No snapshot was taken; fail the complete request admission.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_maskStamp.value());
        _hostFinishedStamp = _maskStamp;
        _maskStamp.reset();
        return result;
    }
    _hostCommitStamp = _maskStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}
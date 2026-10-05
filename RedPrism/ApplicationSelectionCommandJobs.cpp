#include "Application.h"

#include "SelectionCommandTestGate.h"
#include "SelectionCommandWork.h"
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
struct SelectionPayload final : DeferredReleaseNode
{
    std::optional<SelectionCommandInput> input;
    std::optional<PreparedSelectionCommand> publication;
    std::shared_ptr<SelectionCommandTestGate> gate;
    DWORD ownerThread = 0;
    SelectionPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<SelectionPayload> owned(static_cast<SelectionPayload*>(node)); };
    }
    ~SelectionPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireSelectionPayload final
{
    void operator()(SelectionPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<SelectionPayload> owned(payload);
        }
    }
};
} // namespace

class Application::SelectionCommandJob final : public Job
{
  public:
    SelectionCommandJob(Application& owner, HistoryWorkStamp stamp, SelectionCommandKind kind,
                        std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _kind(kind), _retirement(retirement),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._selectionCommandTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // full scheduler admission precedes owner capture on the resumable boundary
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
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<SelectionCommandTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        _payload->publication = DocumentSession::PrepareSelectionCommandPublication(std::move(_payload->input.value()),
                                                                                    context.cancellation, observer);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_FAIL};
        if (_gate && _gate->lateResidencySource.HasStorage())
        {
            if (!_gate->lateResidencySource.Data())
                return {JobStatus::Failed, E_FAIL};
            _gate->residentGrowths.fetch_add(1);
        }
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, SelectionCommandTestGate::kMessage, 0, 0))
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
            _payload.reset(std::make_unique<SelectionPayload>().release());
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
            _payload->input = _owner._session.PrepareSelectionCommandInput(_kind);
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
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
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedSelectionCommandIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _residencyPermit.Reset(); // No callback/chrome/coverage read retains the residency gate.
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
        _owner._selectionCommandTicket.reset();
        _owner._selectionCommandStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._selectionCommandStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const SelectionCommandKind _kind;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<SelectionCommandTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<SelectionPayload, RetireSelectionPayload> _payload;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _refreshResidency = false;
    std::int32_t _captureError = 0;
};

HRESULT Application::StartSelectionCommand(SelectionCommandKind kind) noexcept
{
    if (_selectionCommandTicket || kind > SelectionCommandKind::Deselect)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!_session.Document())
        return S_OK;
    if (!_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return E_ABORT;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _selectionCommandStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_selectionCommandStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _selectionCommandStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_selectionCommandStamp)
        return E_UNEXPECTED;
    auto memory = _session.SelectionCommandMemoryBytes(kind);
    auto retirement = _session.HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if (memory && retirement && sizeof(SelectionCommandJob) + sizeof(SelectionPayload) <= maximum - memory)
    {
        memory += sizeof(SelectionCommandJob) + sizeof(SelectionPayload);
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_selectionCommandTestGate)
                    _selectionCommandTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job =
                    std::make_unique<SelectionCommandJob>(*this, _selectionCommandStamp.value(), kind, retirement);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Selection, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _selectionCommandTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_selectionCommandStamp.value());
        _hostFinishedStamp = _selectionCommandStamp;
        _selectionCommandStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _selectionCommandStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

#include "Application.h"

#include "SelectionNudgeTestGate.h"
#include "SelectionNudgeWork.h"
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
struct SelectionNudgePayload final : DeferredReleaseNode
{
    std::optional<SelectionNudgeInput> input;
    std::optional<PreparedSelectionNudge> publication;
    std::shared_ptr<SelectionNudgeTestGate> gate;
    DWORD ownerThread = 0;
    SelectionNudgePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<SelectionNudgePayload> owned(static_cast<SelectionNudgePayload*>(node)); };
    }
    ~SelectionNudgePayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireSelectionNudgePayload final
{
    void operator()(SelectionNudgePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<SelectionNudgePayload> owned(payload);
        }
    }
};
} // namespace

class Application::SelectionNudgeJob final : public Job
{
  public:
    SelectionNudgeJob(Application& owner, HistoryWorkStamp stamp, int dx, int dy, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _dx(dx), _dy(dy), _retirement(retirement),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._selectionNudgeTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // full scheduler admission precedes owner capture on the resumable boundary
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<SelectionNudgeTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        _payload->publication = DocumentSession::PrepareSelectionNudgePublication(std::move(_payload->input.value()),
                                                                                  context.cancellation, observer);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_FAIL};
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, SelectionNudgeTestGate::kMessage, 0, 0))
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
            _payload.reset(std::make_unique<SelectionNudgePayload>().release());
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
            _payload->input = _owner._session.PrepareSelectionNudgeInput(_dx, _dy);
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
                  !_owner._session.ApplyPreparedSelectionNudgeIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
        _owner._selectionNudgeTicket.reset();
        _owner._selectionNudgeStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._selectionNudgeStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const int _dx;
    const int _dy;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<SelectionNudgeTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<SelectionNudgePayload, RetireSelectionNudgePayload> _payload;
    std::int32_t _captureError = 0;
};

HRESULT Application::StartSelectionNudge(int dx, int dy) noexcept
{
    if (_selectionNudgeTicket)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!_session.Document() || !_session.Selection() || _session.Selection().value().IsEmpty() ||
        _session.IsMovingSelection())
        return S_OK;
    if (!_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return E_ABORT;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _selectionNudgeStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_selectionNudgeStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _selectionNudgeStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_selectionNudgeStamp)
        return E_UNEXPECTED;
    auto memory = _session.SelectionNudgeMemoryBytes();
    auto retirement = _session.HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if (memory && retirement && sizeof(SelectionNudgeJob) + sizeof(SelectionNudgePayload) <= maximum - memory)
    {
        memory += sizeof(SelectionNudgeJob) + sizeof(SelectionNudgePayload);
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_selectionNudgeTestGate)
                    _selectionNudgeTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job =
                    std::make_unique<SelectionNudgeJob>(*this, _selectionNudgeStamp.value(), dx, dy, retirement);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Selection, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _selectionNudgeTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_selectionNudgeStamp.value());
        _hostFinishedStamp = _selectionNudgeStamp;
        _selectionNudgeStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _selectionNudgeStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

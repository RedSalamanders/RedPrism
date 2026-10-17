#include "Application.h"

#include "SelectionMoveCloseTestGate.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"
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
struct SelectionMoveClosePayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> geometryMemory; // outlives every contained geometry owner
    std::optional<SelectionMoveClosureInput> input;
    std::optional<PreparedSelectionMoveClosure> publication;
    std::shared_ptr<SelectionMoveCloseTestGate> gate;
    DWORD ownerThread = 0;
    SelectionMoveClosePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<SelectionMoveClosePayload> owned(static_cast<SelectionMoveClosePayload*>(node)); };
    }
    ~SelectionMoveClosePayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireSelectionMoveClosePayload final
{
    void operator()(SelectionMoveClosePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<SelectionMoveClosePayload> owned(payload);
        }
    }
};
} // namespace

class Application::SelectionMoveCloseJob final : public Job
{
  public:
    SelectionMoveCloseJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, bool cancel,
                          std::uint64_t retirement, std::uint64_t declaredMemory, bool scalar) noexcept
        : _owner(owner), _stamp(stamp), _opening(opening), _cancel(cancel), _retirement(retirement),
          _declaredMemory(declaredMemory), _scalar(scalar), _generation(owner._session.Generation()),
          _active(owner._session.ActiveLayerID()), _crop(owner._session.CropFrame()),
          _maskSelected(owner._session.IsMaskSelected()), _gate(owner._selectionMoveCloseTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            if (_scalar)
            {
                auto payload = std::make_unique<SelectionMoveClosePayload>();
                payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
                payload->geometryMemory = SharedPublicationMemory::CreateForWork(_declaredMemory);
                payload->gate = _gate;
                payload->ownerThread = _ownerThread;
                if (_gate)
                {
                    _gate->geometryMemory = payload->geometryMemory;
                    _gate->payloadAllocatedOffOwner.store(GetCurrentThreadId() != _ownerThread);
                }
                _payload.reset(payload.release());
            }
            return {}; // full admission and selected worker allocation precede owner capture
        }
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<SelectionMoveCloseTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        _payload->publication = DocumentSession::PrepareSelectionMoveClosurePublication(
            std::move(_payload->input.value()), context.cancellation, observer, nullptr, _payload->geometryMemory);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_FAIL};
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, SelectionMoveCloseTestGate::kMessage, 0, 0))
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
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        BindRetirement();
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _captured || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (!_payload)
            {
                _payload.reset(std::make_unique<SelectionMoveClosePayload>().release());
                _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
                _payload->gate = _gate;
                _payload->ownerThread = _ownerThread;
            }
            if (_gate)
            {
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareSelectionMoveClosureInput(_cancel, _opening);
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
            if (_gate)
                _gate->captures.fetch_add(1);
            _captured = true;
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
        else if (outcome == HostOutcome::Succeeded)
        {
            const auto bound = !_scalar ? S_OK
                               : _payload && _payload->geometryMemory
                                   ? _payload->geometryMemory->BindAfterCompletedWork(_owner._jobs, _ticket)
                                   : E_UNEXPECTED;
            if (FAILED(bound))
            {
                outcome = HostOutcome::Failed;
                error = bound;
            }
            else if (!_payload || !_payload->publication ||
                     !_owner._session.ApplyPreparedSelectionMoveClosureIfCurrent(_payload->publication.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
        }
        BindRetirement();
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
        _owner._selectionMoveCloseTicket.reset();
        _owner._selectionMoveCloseStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* size = _owner._activeHostRequest
                               ? std::get_if<HostSizeApplyInput>(&_owner._activeHostRequest->input.Input())
                               : nullptr;
        const bool sizeCurrent = !size || !size->selectionMoveOpening ||
                                 (size->invocation == _owner._sizeInvocation && _owner.SizeCardOpen() &&
                                  _owner.CanQueueSelectionSizeOutline());
        const bool ownsLifecycleOutline = _owner._activeHostRequest && _owner.SelectionLifecycleRequestOpening(
                                                                           _owner._activeHostRequest->input.Input());
        const bool lifecycleCurrent = !ownsLifecycleOutline || _owner.SelectionLifecycleOutlineCurrent();
        return !_owner._shutdownStarted && _owner._selectionMoveCloseStamp == _stamp && sizeCurrent &&
               lifecycleCurrent && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.SelectionMoveOpening() == std::optional(_opening) &&
               _owner._session.Generation() == _generation && _owner._session.ActiveLayerID() == _active &&
               _owner._session.CropFrame() == _crop && _owner._session.IsMaskSelected() == _maskSelected;
    }
    void BindRetirement() noexcept
    {
        if (_payload && _payload->geometryMemory && !_payload->geometryMemory->IsBound() &&
            FAILED(_payload->geometryMemory->BindForRetirement(_owner._jobs, _ticket)))
            std::terminate(); // The descriptor reserved this exact original retirement slot.
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const bool _cancel;
    const std::uint64_t _retirement;
    const std::uint64_t _declaredMemory;
    const bool _scalar;
    const std::uint64_t _generation;
    const std::optional<Guid> _active;
    const std::optional<CropRect> _crop;
    const bool _maskSelected;
    const std::shared_ptr<SelectionMoveCloseTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<SelectionMoveClosePayload, RetireSelectionMoveClosePayload> _payload;
    std::int32_t _captureError = 0;
    JobTicket _ticket;
    bool _captured = false;
};

HRESULT Application::StartSelectionMoveClose(std::uint64_t opening, bool cancel) noexcept
{
    if (_selectionMoveCloseTicket || opening == 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (_session.SelectionMoveOpening() != std::optional(opening))
        return E_ABORT;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _selectionMoveCloseStamp =
        _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_selectionMoveCloseStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _selectionMoveCloseStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_selectionMoveCloseStamp)
        return E_UNEXPECTED;
    auto memory = _session.SelectionMoveClosureMemoryBytes();
    auto retirement = _session.SelectionMoveClosureRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const bool scalar = _session.IsDeferredSelectionMove();
    const auto extra = sizeof(SelectionMoveCloseJob) + sizeof(SelectionMoveClosePayload) +
                       (scalar ? SharedPublicationMemory::OwnershipAllowance() : 0);
    if (memory && retirement && extra <= maximum - memory)
    {
        memory += extra;
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_selectionMoveCloseTestGate)
                    _selectionMoveCloseTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job = std::make_unique<SelectionMoveCloseJob>(
                    *this, _selectionMoveCloseStamp.value(), opening, cancel, retirement, memory, scalar);
                JobTicket ticket;
                result = static_cast<HRESULT>(_jobs.Submit(job,
                                                           {.kind = JobClass::Selection,
                                                            .serialKey = _session.DocumentSerial(),
                                                            .memoryBytes = memory,
                                                            .deferredRetirement = scalar,
                                                            .allowMemoryWaiting = !scalar,
                                                            .reserveRetainedMemory = scalar},
                                                           ticket));
                if (SUCCEEDED(result))
                    _selectionMoveCloseTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_selectionMoveCloseStamp.value());
        _hostFinishedStamp = _selectionMoveCloseStamp;
        _selectionMoveCloseStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _selectionMoveCloseStamp;
    }
    return S_OK;
}

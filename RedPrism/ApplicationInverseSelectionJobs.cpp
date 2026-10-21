#include "Application.h"
#include "ChromeHost.h"
#include "InverseSelectionWork.h"
#include "Runtime.Tests.SelectionCommandTestGate.h"
#include "SelectionOutlineWork.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct InverseSelectionPayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> temporary;
    std::shared_ptr<SharedPublicationMemory> output;
    std::optional<InverseSelectionInput> input;
    std::optional<PreparedInverseSelection> publication;
    PreparedSelectionOutline outline;
    std::uint64_t outlineBytes = 0;
    std::vector<CanvasOverlay::OutlineLoop> retiredReferenceOutline;
    std::shared_ptr<SelectionCommandTestGate> gate;
    DWORD ownerThread = 0;
    InverseSelectionPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<InverseSelectionPayload> owned(static_cast<InverseSelectionPayload*>(node)); };
    }
    ~InverseSelectionPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireInverseSelectionPayload final
{
    void operator()(InverseSelectionPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<InverseSelectionPayload> owned(payload);
        }
    }
};
bool AddInverseHostBytes(std::uint64_t& bytes, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += extra;
    return true;
}
} // namespace

class Application::InverseSelectionJob final : public Job
{
  public:
    InverseSelectionJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t retirement,
                        std::uint64_t outlineLimit) noexcept
        : _owner(owner), _stamp(stamp), _retirement(retirement), _outlineLimit(outlineLimit),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._inverseSelectionTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            auto payload = std::make_unique<InverseSelectionPayload>();
            payload->temporary = SharedPublicationMemory::CreateForWork();
            payload->output = SharedPublicationMemory::CreateForWork();
            payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            payload->gate = _gate;
            payload->ownerThread = _ownerThread;
            _payload.reset(payload.release());
            return {}; // Complete admission and worker allocation precede immutable owner capture.
        }
        if (_refreshResidency)
        {
            if (_gate)
            {
                if (_gate->throwResidencyRefresh)
                    throw std::bad_alloc{};
            }
            if (!_payload->publication || !_payload->publication->RefreshHistoryResidency(context.cancellation))
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            _refreshResidency = false;
            return {};
        }
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver observer;
        if (_gate)
            observer = {[](void* raw, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<SelectionCommandTestGate*>(raw);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        if (_gate && _gate->observeInverseOutputLifetime)
        {
            BoundedSelectionBoolean::Statistics statistics;
            InverseSelectionObserver diagnostic;
            diagnostic.statistics = &statistics;
            const auto report = wil::scope_exit(
                [&]() noexcept
                {
                    std::fprintf(stderr,
                                 "INFO inverse-engine capacity=%zu consumed=%zu allocations=%zu work=%llu points=%llu "
                                 "loops=%llu\n",
                                 statistics.workspaceCapacity, statistics.workspaceConsumed, statistics.allocations,
                                 static_cast<unsigned long long>(statistics.workUnits),
                                 static_cast<unsigned long long>(statistics.outputPoints),
                                 static_cast<unsigned long long>(statistics.outputLoops));
                });
            _payload->publication = DocumentSession::PrepareInverseSelectionPublication(
                std::move(*_payload->input), context.cancellation, observer, _payload->output, &diagnostic);
        }
        else
        {
            _payload->publication = DocumentSession::PrepareInverseSelectionPublication(
                std::move(*_payload->input), context.cancellation, observer, _payload->output);
        }
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        if (_payload->publication->ChangesSelection())
        {
            const auto* selected = _payload->publication->SelectionForDisplayForWork();
            SelectionLoops source = selected ? selected->loops : SelectionLoops{};
            const auto declared = PreparedSelectionOutline::OutputMemory(source);
            if (!declared || *declared > _outlineLimit)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            auto outline = PreparedSelectionOutline::Prepare(std::move(source), _payload->output, context.cancellation);
            if (!outline)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            _payload->outlineBytes = outline->DeclaredOutputBytes();
            _payload->outline = std::move(*outline);
            if (_gate && _gate->observeInverseOutputLifetime)
            {
                _gate->inverseOutlineOwner = _payload->outline.WeakStorageForTest();
                _gate->inverseMemoryOwner = _payload->output;
                const auto& points = _payload->outline.Points();
                _gate->inversePointBuffer.store(points.empty() ? nullptr : points.front().data());
                _gate->inverseOutlinePreparedOffOwner.store(GetCurrentThreadId() != _ownerThread);
            }
        }
        if (_gate && _gate->lateResidencySource.HasStorage())
        {
            if (!_gate->lateResidencySource.Data())
                return {JobStatus::Failed, E_OUTOFMEMORY};
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
    bool DeferRetirement(JobResult, JobTicket original) noexcept override
    {
        _ticket = original;
        return false;
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !_payload || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_captureAttempted)
        {
            if (!_payload->publication)
                return false;
            const auto residency = _payload->publication->TryAcquireHistoryResidency(_residencyPermit);
            if (residency == DocumentHistory::ResidencyValidation::Ready)
                return false;
            if (residency == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            // Both Refresh and Busy resume the original worker, as the settled selection command does.
            {
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
            }
            return false;
        }
        _captureAttempted = true;
        try
        {
            if (_gate)
            {
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareInverseSelectionInput();
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
    void RetainJoinedRetirement(JobScheduler&, JobTicket original) noexcept override
    {
        _ticket = original;
        BindRetirement(original);
    }
    void Completed(JobResult result) noexcept override
    {
        const auto releasePermit = wil::scope_exit([this]() noexcept { _residencyPermit.Reset(); });
        BindRetirement(_ticket);
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
                  !_owner._session.ApplyPreparedInverseSelectionIfCurrent(*_payload->publication)))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _residencyPermit.Reset();
        if (outcome == HostOutcome::Succeeded && _payload && _payload->outline.HasStorage())
            (void)_owner.AdoptPreparedSelectionOutlineOwner(std::move(_payload->outline),
                                                            _payload->retiredReferenceOutline);
        if (_gate && _gate->observeInverseOutputLifetime && outcome == HostOutcome::Succeeded && _payload &&
            _payload->publication && _payload->publication->OutputAllocationBytes() != 0)
        {
            const auto bytes = _payload->output->Bytes();
            _gate->inverseOutputBoundToOriginal =
                _gate->inverseOutputBoundToOriginal && _payload->output->BoundTo(_owner._jobs, _ticket) &&
                bytes == _payload->publication->OutputAllocationBytes() + _payload->outlineBytes;
            _gate->inversePublishedBytes += bytes;
            ++_gate->inversePublishedOutputs;
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_SELECTION_EDIT);
        _owner._inverseSelectionTicket.reset();
        _owner._inverseSelectionStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    void BindRetirement(JobTicket original) noexcept
    {
        if (!_payload || _payload->temporary->IsBound())
            return;
        std::uint64_t output = _payload->publication ? _payload->publication->OutputAllocationBytes() : 0;
        if (!AddInverseHostBytes(output, _payload->outlineBytes))
            std::terminate();
        const auto result = output ? _payload->output->BindPersistentOutputForRetirement(_owner._jobs, original, output,
                                                                                         *_payload->temporary)
                                   : _payload->temporary->BindForRetirement(_owner._jobs, original);
        if (result < 0)
            std::terminate(); // Never refund a graph still readable by a worker/display/history owner.
    }
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._inverseSelectionStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _retirement, _outlineLimit;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<SelectionCommandTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<InverseSelectionPayload, RetireInverseSelectionPayload> _payload;
    DocumentHistory::ResidencyPermit _residencyPermit;
    JobTicket _ticket;
    std::int32_t _captureError = 0;
    bool _captureAttempted = false, _refreshResidency = false;
};

HRESULT Application::StartInverseSelection() noexcept
{
    if (_inverseSelectionTicket || !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!_session.Document() || !_session.Document()->selection)
        return S_OK; // Legacy absent-selection no-op retains history, redo and generation.
    const auto plan = _session.PlanInverseSelectionMemory();
    if (!plan)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const BoundedSelectionBoolean::Limits limits;
    const auto outline = PreparedSelectionOutline::OutputMemory(limits.outputLoops, limits.outputPoints);
    const auto legacy = _chrome ? _chrome->LegacySelectionOutlineMemoryOwner() : std::optional<std::uint64_t>{0};
    if (!outline || !legacy)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t additional = sizeof(InverseSelectionJob) + sizeof(InverseSelectionPayload);
    if (!AddInverseHostBytes(additional, 2 * SharedPublicationMemory::OwnershipAllowance()) ||
        !AddInverseHostBytes(additional, *outline) || !AddInverseHostBytes(additional, *legacy))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t memory = plan->reservationBytes, retirement = plan->retirementBytes;
    if (!AddInverseHostBytes(memory, additional) || !AddInverseHostBytes(retirement, additional))
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _inverseSelectionStamp = _historyWork.Begin(_session.DocumentSerial(), *ordinal, HistoryWorkKind::Selection);
    if (!_inverseSelectionStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == *ordinal)
        _inverseSelectionStamp =
            _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_inverseSelectionStamp)
        return E_UNEXPECTED;
    HRESULT result;
    try
    {
        if (_inverseSelectionTestGate)
            _inverseSelectionTestGate->reservationBytes = memory;
        std::unique_ptr<Job> job =
            std::make_unique<InverseSelectionJob>(*this, *_inverseSelectionStamp, retirement, *outline);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job,
                                                   {.kind = JobClass::Selection,
                                                    .serialKey = _session.DocumentSerial(),
                                                    .memoryBytes = memory,
                                                    .deferredRetirement = true,
                                                    .reserveRetainedMemory = true},
                                                   ticket));
        if (SUCCEEDED(result))
            _inverseSelectionTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(*_inverseSelectionStamp);
        _hostFinishedStamp = _inverseSelectionStamp;
        _inverseSelectionStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _inverseSelectionStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

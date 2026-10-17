#include "Application.h"
#include "ApplicationProcessingTestProbe.h"
#include "HostLassoInput.h"
#include "LassoOpeningWork.h"
#include "LassoWorkTestGate.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
struct LassoOpeningPayload final : DeferredReleaseNode
{
    std::shared_ptr<LassoWorkTestGate> gate;
    std::uint32_t ownerThread = 0;
    ~LassoOpeningPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true, std::memory_order_release);
        }
    }

    std::shared_ptr<SharedPublicationMemory> memory;
    std::optional<LassoOpeningInput> input;
    std::optional<PreparedLassoOpening> prepared;
    explicit LassoOpeningPayload(std::uint64_t declared) noexcept
    {
        bytes = (std::max)(declared, kDeferredReleaseThreshold);
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<LassoOpeningPayload> owned(static_cast<LassoOpeningPayload*>(node)); };
    }
};
struct RetireLassoOpeningPayload final
{
    void operator()(LassoOpeningPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<LassoOpeningPayload> owned(value);
        }
    }
};
} // namespace

class Application::LassoOpeningJob final : public Job
{
  public:
    LassoOpeningJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, std::uint32_t blocks,
                    std::uint64_t declared) noexcept
        : _owner(owner), _stamp(stamp), _opening(opening), _blocks(blocks), _declared(declared),
          _gate(owner._processingTestProbe ? owner._processingTestProbe->lassoWork : nullptr),
          _ownerThread(_gate ? GetCurrentThreadId() : 0)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            auto payload = std::make_unique<LassoOpeningPayload>(_declared);
            payload->memory = SharedPublicationMemory::CreateForWork(PreparedLassoOpening::MemoryBytes(_blocks));
            if (!payload->memory)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            payload->gate = _gate;
            payload->ownerThread = _ownerThread;
            _payload.reset(payload.release());
            if (_gate)
            {
                _gate->openingMemory = _payload->memory;
                return _gate->Reach(LassoWorkTestGate::Stage::OpeningAllocated, _ownerThread, context);
            }
            return {}; // Original complete admission precedes the owner capture.
        }
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto prepared = DocumentSession::PrepareLassoOpening(std::move(_payload->input.value()), _blocks,
                                                             context.cancellation, _payload->memory);
        _payload->input.reset();
        if (prepared)
            _payload->prepared.emplace(std::move(prepared.value()));
        if (_gate && _payload->prepared)
            return _gate->Reach(LassoWorkTestGate::Stage::OpeningPrepared, _ownerThread, context);
        return _payload->prepared                   ? JobResult{}
               : context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                    : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _captured || !Current() || !_payload)
            return false;
        const auto* captured = _owner._lassoInput.CapturedContext(_opening);
        const auto first = _owner._lassoInput.Peek(_opening);
        if (!captured || !first || first->action != HostLassoInput::Action::Down ||
            captured->kind != (captured->kind == LassoKind::Rectangle || captured->kind == LassoKind::Ellipse
                                   ? _owner._session.marqueeKind
                                   : _owner._session.lassoKind))
        {
            _captureError = E_ABORT;
            return false;
        }
        const bool marquee = captured->kind == LassoKind::Rectangle || captured->kind == LassoKind::Ellipse;
        auto input = _owner._session.PrepareLassoOpeningInput(first->sample.point, captured->mode, marquee);
        if (!input)
        {
            _captureError = E_ABORT;
            return false;
        }
        _payload->input.emplace(std::move(input.value()));
        if (_gate)
            _gate->openingCaptures.fetch_add(1);
        _captured = true;
        if (!_owner._jobs.Resume(ticket))
            std::terminate();
        return true;
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        BindRetirement();
    }
    void Completed(JobResult result) noexcept override
    {
        auto outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                       : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                             : HostOutcome::Succeeded;
        HRESULT error = result.error;
        if (_owner._lassoCancelledOpening == _opening || _owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (!Current())
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (FAILED(_captureError))
        {
            outcome = _captureError == E_ABORT ? HostOutcome::Stale : HostOutcome::Failed;
            error = _captureError;
        }
        if (outcome == HostOutcome::Succeeded)
        {
            const auto bound = _payload && _payload->memory
                                   ? _payload->memory->BindAfterCompletedWork(_owner._jobs, _ticket)
                                   : E_UNEXPECTED;
            if (FAILED(bound))
            {
                outcome = HostOutcome::Failed;
                error = bound;
            }
            else if (!_payload->prepared ||
                     !_owner._session.ApplyPreparedLassoOpeningIfCurrent(_payload->prepared.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
            else
            {
                _owner._lassoAcquiredOpening = _opening;
                _owner._lassoSessionOpening = _owner._session.LassoOpeningForWork();
            }
        }
        BindRetirement();
        _owner._lassoOpeningTicket.reset();
        _owner._lassoOpeningStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        if (outcome != HostOutcome::Succeeded)
            _owner.CancelLassoInputOwner(_opening, outcome);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._lassoOpeningStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && !_owner._historyWork.CancelRequested(_stamp) &&
               _owner.LassoInputCurrentOwner(_opening);
    }
    void BindRetirement() noexcept
    {
        if (_payload && _payload->memory && !_payload->memory->IsBound() &&
            FAILED(_payload->memory->BindForRetirement(_owner._jobs, _ticket)))
            std::terminate();
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const std::uint32_t _blocks;
    const std::uint64_t _declared;
    std::unique_ptr<LassoOpeningPayload, RetireLassoOpeningPayload> _payload;
    const std::shared_ptr<LassoWorkTestGate> _gate;
    const std::uint32_t _ownerThread;
    JobTicket _ticket;
    HRESULT _captureError = S_OK;
    bool _captured = false;
};

HRESULT Application::StartLassoOpening(std::uint64_t opening) noexcept
{
    if (PolygonalInputOwner(opening))
        return StartPolygonalDrawing(opening);
    if (!_activeHostRequest || _lassoOpeningTicket || !LassoInputCurrentOwner(opening) ||
        !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return E_ABORT;
    constexpr std::uint32_t blocks = 131071;
    const auto arena = PreparedLassoOpening::MemoryBytes(blocks);
    const std::uint64_t extra = sizeof(LassoOpeningJob) + sizeof(LassoOpeningPayload) + sizeof(HostLassoInput) +
                                SharedPublicationMemory::OwnershipAllowance() + 8192;
    if (!arena || arena > (std::numeric_limits<std::uint64_t>::max)() - extra)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    _lassoOpeningStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_lassoOpeningStamp)
        return E_UNEXPECTED;
    HRESULT result;
    try
    {
        std::unique_ptr<Job> job =
            std::make_unique<LassoOpeningJob>(*this, *_lassoOpeningStamp, opening, blocks, arena + extra);
        JobTicket ticket;
        result = _jobs.Submit(job,
                              {.kind = JobClass::Selection,
                               .serialKey = _session.DocumentSerial(),
                               .memoryBytes = arena + extra,
                               .deferredRetirement = true,
                               .allowMemoryWaiting = false,
                               .reserveRetainedMemory = true},
                              ticket);
        if (SUCCEEDED(result))
            _lassoOpeningTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(*_lassoOpeningStamp);
        _hostFinishedStamp = _lassoOpeningStamp;
        _lassoOpeningStamp.reset();
        return result;
    }
    _hostCommitStamp = _lassoOpeningStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

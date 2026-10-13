#include "Application.h"
#include "ApplicationProcessingTestProbe.h"
#include "ChromeHost.h"
#include "HostLassoInput.h"
#include "LassoFinishWork.h"
#include "LassoWorkTestGate.h"
#include "SelectionOutlineWork.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
struct LassoFinishPayload final : DeferredReleaseNode
{
    std::shared_ptr<LassoWorkTestGate> gate;
    std::uint32_t ownerThread = 0;
    ~LassoFinishPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true, std::memory_order_release);
        }
    }

    std::shared_ptr<SharedPublicationMemory> temporary, output;
    std::optional<LassoFinishInput> input;
    std::optional<PreparedLassoFinish> publication;
    PreparedSelectionOutline outline;
    std::vector<CanvasOverlay::OutlineLoop> retiredReferenceOutline;
    std::uint64_t outlineBytes = 0;
    explicit LassoFinishPayload(std::uint64_t declared) noexcept
    {
        bytes = (std::max)(declared, kDeferredReleaseThreshold);
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<LassoFinishPayload> owned(static_cast<LassoFinishPayload*>(node)); };
    }
};
struct RetireLassoFinishPayload final
{
    void operator()(LassoFinishPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<LassoFinishPayload> owned(value);
        }
    }
};
bool AddLassoFinishBytes(std::uint64_t& value, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - value)
        return false;
    value += extra;
    return true;
}
} // namespace
class Application::LassoFinishJob final : public Job
{
  public:
    LassoFinishJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, std::uint64_t declared,
                   std::uint64_t outlineLimit) noexcept
        : _owner(owner), _stamp(stamp), _opening(opening), _declared(declared), _outlineLimit(outlineLimit),
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
            auto value = std::make_unique<LassoFinishPayload>(_declared);
            value->temporary = SharedPublicationMemory::CreateForWork();
            value->output = SharedPublicationMemory::CreateForWork();
            if (!value->temporary || !value->output)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            value->gate = _gate;
            value->ownerThread = _ownerThread;
            _payload.reset(value.release());
            if (_gate)
            {
                _gate->finishMemory = _payload->temporary;
                _gate->finishOutputMemory = _payload->output;
                return _gate->Reach(LassoWorkTestGate::Stage::FinishAllocated, _ownerThread, context);
            }
            return {}; // Complete original admission precedes closure and immutable input capture.
        }
        if (_refreshResidency)
        {
            if (!_payload->publication || !_payload->publication->RefreshHistoryResidency(context.cancellation))
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            _refreshResidency = false;
            return {};
        }
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto publication = DocumentSession::PrepareLassoFinishPublication(
            std::move(_payload->input.value()), context.cancellation, {}, nullptr, _payload->output);
        _payload->input.reset();
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!publication)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _payload->publication.emplace(std::move(publication.value()));
        if (_payload->publication->ChangesSelection())
        {
            const auto* selection = _payload->publication->SelectionForDisplayForWork();
            SelectionLoops source = selection ? selection->loops : SelectionLoops{};
            const auto output = PreparedSelectionOutline::OutputMemory(source);
            if (!output || *output > _outlineLimit)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            auto outline = PreparedSelectionOutline::Prepare(std::move(source), _payload->output, context.cancellation);
            if (!outline)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
            _payload->outlineBytes = outline->DeclaredOutputBytes();
            _payload->outline = std::move(outline.value());
        }
        if (_gate)
            return _gate->Reach(LassoWorkTestGate::Stage::FinishPrepared, _ownerThread, context);
        return {};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || !_payload)
            return false;
        if (_captured)
        {
            if (!_payload->publication)
                return false;
            const auto status = _payload->publication->TryAcquireHistoryResidency(_permit);
            if (status == DocumentHistory::ResidencyValidation::Ready)
                return false;
            if (status == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            _refreshResidency = true;
        }
        else
        {
            const auto event = _owner._lassoInput.Peek(_opening);
            if (!event || event->action != HostLassoInput::Action::Up)
            {
                _captureError = E_ABORT;
                return false;
            }
            auto terminal = _owner._session.CloseBoundedLassoForWork(event->sample.antialiased);
            if (!terminal)
            {
                _captureError = E_ABORT;
                return false;
            }
            auto input = _owner._session.PrepareClosedBoundedLassoFinishInput(std::move(terminal.value()));
            if (!input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
            if (_gate)
                _gate->finishCaptures.fetch_add(1);
            _captured = true;
            (void)_owner._lassoInput.Take(_opening);
            _owner.SelectionChanged(); // Immediately excludes the closed draft from current-only borrow.
        }
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
        const auto release = wil::scope_exit([this]() noexcept { _permit.Reset(); });
        BindRetirement();
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
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedLassoFinishIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _permit.Reset();
        if (outcome == HostOutcome::Succeeded && _payload && _payload->outline.HasStorage())
            (void)_owner.AdoptPreparedSelectionOutlineOwner(std::move(_payload->outline),
                                                            _payload->retiredReferenceOutline);
        _owner._lassoFinishTicket.reset();
        _owner._lassoFinishStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.CancelLassoInputOwner(_opening, outcome);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._lassoFinishStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && !_owner._historyWork.CancelRequested(_stamp) &&
               _owner.LassoInputCurrentOwner(_opening);
    }
    void BindRetirement() noexcept
    {
        if (!_payload || !_payload->temporary || _payload->temporary->IsBound())
            return;
        auto output = _payload->publication ? _payload->publication->OutputAllocationBytes() : 0;
        if (!AddLassoFinishBytes(output, _payload->outlineBytes))
            std::terminate();
        const auto result = output ? _payload->output->BindPersistentOutputForRetirement(_owner._jobs, _ticket, output,
                                                                                         *_payload->temporary)
                                   : _payload->temporary->BindForRetirement(_owner._jobs, _ticket);
        if (FAILED(result))
            std::terminate();
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening, _declared, _outlineLimit;
    std::unique_ptr<LassoFinishPayload, RetireLassoFinishPayload> _payload;
    DocumentHistory::ResidencyPermit _permit;
    const std::shared_ptr<LassoWorkTestGate> _gate;
    const std::uint32_t _ownerThread;
    JobTicket _ticket;
    HRESULT _captureError = S_OK;
    bool _captured = false, _refreshResidency = false;
};
HRESULT Application::StartLassoFinish(std::uint64_t opening) noexcept
{
    if (!_activeHostRequest || _lassoFinishTicket || !LassoInputCurrentOwner(opening))
        return E_ABORT;
    const auto terminal = _lassoInput.Peek(opening);
    const auto snapshot = _session.CaptureBoundedLassoDisplayForWork();
    if (!terminal || terminal->action != HostLassoInput::Action::Up || !snapshot || !_session.Document())
        return E_ABORT;
    const auto plan = LassoFinishGeometry::Plan(snapshot->display, _session.Document()->selection);
    const BoundedSelectionBoolean::Limits limits;
    const auto outline = PreparedSelectionOutline::OutputMemory(limits.outputLoops, limits.outputPoints);
    const auto legacy = _chrome ? _chrome->LegacySelectionOutlineMemoryOwner() : std::optional<std::uint64_t>{0};
    const auto history = _session.history.EditMemoryBytes(_session.Document());
    const auto old = _session.HistoryRestoreRetirementMemoryBytes();
    if (!plan || !outline || !legacy || !history || !old)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    std::uint64_t memory = sizeof(LassoFinishJob) + sizeof(LassoFinishPayload) + sizeof(HostLassoInput) +
                           2 * SharedPublicationMemory::OwnershipAllowance() + 16 * 4096;
    // Original arena remains independently charged. Full source/history, refresh overlap, scratch, output,
    // prepared outline, old reference outline, and worker retirement are admitted before draft closure.
    for (const auto bytes : {plan->preparationBytes, *outline, *legacy, history, history, old})
        if (!AddLassoFinishBytes(memory, bytes))
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    _lassoFinishStamp =
        _historyWork.Begin(_session.DocumentSerial(), _activeHostRequest->ordinal, HistoryWorkKind::Selection);
    if (!_lassoFinishStamp)
        return E_UNEXPECTED;
    HRESULT result;
    try
    {
        std::unique_ptr<Job> job =
            std::make_unique<LassoFinishJob>(*this, *_lassoFinishStamp, opening, memory, *outline);
        JobTicket ticket;
        result = _jobs.Submit(job,
                              {.kind = JobClass::Selection,
                               .serialKey = _session.DocumentSerial(),
                               .memoryBytes = memory,
                               .deferredRetirement = true,
                               .allowMemoryWaiting = false,
                               .reserveRetainedMemory = true},
                              ticket);
        if (SUCCEEDED(result))
            _lassoFinishTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(*_lassoFinishStamp);
        _hostFinishedStamp = _lassoFinishStamp;
        _lassoFinishStamp.reset();
        return result;
    }
    _hostCommitStamp = _lassoFinishStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

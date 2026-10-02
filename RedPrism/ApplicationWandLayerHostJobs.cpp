#include "Application.h"

#include "WandLayerHostTestGate.h"
#include "WandLayerSelectionWork.h"
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
struct WandLayerHostPayload final : DeferredReleaseNode
{
    std::optional<WandLayerSelectionInput> input;
    std::optional<PreparedWandLayerSelection> publication;
    WandFailure failure = WandFailure::None;
    std::shared_ptr<WandLayerHostTestGate> gate;
    DWORD ownerThread = 0;
    WandLayerHostPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<WandLayerHostPayload> owned(static_cast<WandLayerHostPayload*>(node)); };
    }
    ~WandLayerHostPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireWandLayerHostPayload final
{
    void operator()(WandLayerHostPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<WandLayerHostPayload> owned(payload);
        }
    }
};
} // namespace

class Application::WandLayerHostJob final : public Job
{
  public:
    WandLayerHostJob(Application& owner, HistoryWorkStamp stamp, HostWandLayerNewInput request,
                     std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _request(request), _retirement(retirement),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._wandLayerHostTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Admission of the complete graph precedes owner capture and resume.
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<WandLayerHostTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        _payload->publication = DocumentSession::PrepareWandLayerSelectionPublication(
            std::move(_payload->input.value()), &_payload->failure, context.cancellation, observer);
        if (context.cancellation.IsCancelled() || _payload->failure == WandFailure::Cancelled)
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, _payload->failure == WandFailure::TooDetailed
                                           ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                                           : E_OUTOFMEMORY};
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, WandLayerHostTestGate::kMessage, 0, 0))
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
            _payload.reset(std::make_unique<WandLayerHostPayload>().release());
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
            _payload->input =
                _owner._session.PrepareWandLayerSelectionInput(_request.point, _request.settings, _request.antialiased);
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
                  !_owner._session.ApplyPreparedWandLayerSelectionIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage,
                                        _payload && _payload->failure == WandFailure::TooDetailed
                                            ? IDS_ERROR_WAND_DETAILED
                                            : IDS_ERROR_WAND_MEMORY);
        _owner._wandLayerHostTicket.reset();
        _owner._wandLayerHostStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.SelectionChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._wandLayerHostStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const HostWandLayerNewInput _request;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<WandLayerHostTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<WandLayerHostPayload, RetireWandLayerHostPayload> _payload;
    std::int32_t _captureError = 0;
};

HRESULT Application::StartWandLayerSelection(const HostWandLayerNewInput& input) noexcept
{
    if (_wandLayerHostTicket || !_session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority()))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto& document = _session.Document();
    if (!document || !std::isfinite(input.point.x) || !std::isfinite(input.point.y))
        return S_OK;
    if (input.settings.sampleAllLayers || input.settings.sampleSize > WandSampleSize::FiveByFive)
        return E_INVALIDARG;
    if (input.point.x < 0 || input.point.y < 0 || input.point.x >= document->width || input.point.y >= document->height)
        return S_OK; // The direct reference ignores outside clicks; a valid in-canvas no-match deselects.
    const auto plan = _session.PlanWandLayerSelectionMemory(input.point, input.settings);
    if (!plan)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    constexpr std::uint64_t metadata = sizeof(WandLayerHostJob) + sizeof(WandLayerHostPayload);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (metadata > maximum - plan->reservationBytes || metadata > maximum - plan->retirementBytes)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto memory = plan->reservationBytes + metadata;
    const auto retirement = plan->retirementBytes + metadata;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _wandLayerHostStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _wandLayerHostStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!_wandLayerHostStamp)
        return E_UNEXPECTED;
    HRESULT result;
    try
    {
        if (_wandLayerHostTestGate)
            _wandLayerHostTestGate->reservationBytes = memory;
        std::unique_ptr<Job> job =
            std::make_unique<WandLayerHostJob>(*this, _wandLayerHostStamp.value(), input, retirement);
        JobTicket ticket;
        result =
            static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Selection, _session.DocumentSerial(), memory}, ticket));
        if (SUCCEEDED(result))
            _wandLayerHostTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_wandLayerHostStamp.value());
        _hostFinishedStamp = _wandLayerHostStamp;
        _wandLayerHostStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _wandLayerHostStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

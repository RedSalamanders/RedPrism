#include "Application.h"

#include "StrokeOpeningTestGate.h"
#include "resource.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct OpeningPayload final : DeferredReleaseNode
{
    std::optional<StrokeOpeningInput> input;
    std::optional<StrokeOpeningResult> result;
    std::shared_ptr<StrokeOpeningTestGate> gate;
    std::uint32_t ownerThread = 0;
    OpeningPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OpeningPayload> owned(static_cast<OpeningPayload*>(node)); };
    }
    ~OpeningPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true, std::memory_order_release);
        }
    }
};
#pragma warning(pop)
struct RetireOpeningPayload final
{
    void operator()(OpeningPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<OpeningPayload> owned(payload);
        }
    }
};
} // namespace

class Application::StrokeOpeningJob final : public Job
{
  public:
    StrokeOpeningJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t opening, std::uint64_t bytes) noexcept
        : _owner(owner), _stamp(stamp), _opening(opening), _bytes(bytes), _gate(owner._strokeOpeningTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_cleanup)
            return {}; // No useful work: RetireWorkerStage also runs if cancellation skips this stage.
        if (!_payload)
        {
            // Original complete admission precedes even the worker allocation of the capture owner.
            _payload.reset(std::make_unique<OpeningPayload>().release());
            _payload->bytes = (std::max)(_bytes, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            return {};
        }
        if (!_payload->input)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_gate)
        {
            _gate->prepares.fetch_add(1);
            _gate->offowner.fetch_add(GetCurrentThreadId() != _ownerThread ? 1u : 0u);
        }
        _payload->result.emplace(DocumentSession::PrepareStrokeOpeningPublication(std::move(_payload->input.value()),
                                                                                  context.cancellation, {}));
        _payload->input.reset(); // Worker only: all captured source owners retire off the dispatcher.
        _prepared = true;
        if (context.cancellation.IsCancelled() || _payload->result->status == StrokeOpeningStatus::Cancelled)
            return {JobStatus::Cancelled};
        if (_payload->result->status != StrokeOpeningStatus::Ready || !_payload->result->prepared)
            return {JobStatus::Failed, _payload->result->status == StrokeOpeningStatus::TooLarge
                                           ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                           : E_INVALIDARG};
        if (_gate && _gate->holdAfterPreparation)
        {
            if (!PostThreadMessageW(_ownerThread, StrokeOpeningTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, waits, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
            if (_gate->failureResult)
                return _gate->failureResult.value();
        }
        return {};
    }

    ~StrokeOpeningJob() override
    {
        // Close joins all producers before destroying job wrappers on the owner. Empty applied openings have
        // not replayed any sample; detach that graphics borrow before the shutdown final worker retires CPU.
        DetachAppliedOpening();
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        if (!_cleanup)
            return;
        _payload.reset();
        _retiredStroke.reset();
        _cleanupDone = true; // Scheduler publication is the producer/owner acknowledgement.
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (!_terminal &&
            (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp) ||
             FAILED(_captureError) || _owner._canvasBrushCancelledOpening == _opening))
        {
            _terminal = true;
            _terminalResult = result;
        }
        if (!_terminal)
            return false;
        if (_liveStroke)
        {
            DetachAppliedOpening();
            _cleanupDone = false;
        }
        if (_cleanupDone || (!_payload && !_retiredStroke))
            return false;
        _cleanup = true;
        if (_owner._jobs.Resume(ticket))
            return true;
        std::terminate(); // Original current suspended record must acknowledge its detached CPU owners.
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_terminal || _cleanup)
            return false;
        if (!Current() || _owner._historyWork.CancelRequested(_stamp) || !_payload ||
            _owner._canvasBrushCancelledOpening == _opening)
            return false;
        if (_prepared)
        {
            auto* prepared =
                _payload->result && _payload->result->prepared ? &_payload->result->prepared.value() : nullptr;
            if (!prepared || !_owner._session.IsPreparedStrokeOpeningCurrent(*prepared))
                _captureError = E_ABORT;
            else
            {
                _owner.BindCompositionDevice();
                if (!_owner._session.ApplyPreparedStrokeOpeningIfCurrent(*prepared))
                    _captureError = E_ABORT;
                else
                {
                    _liveStroke = prepared->TakeAppliedDraftForOwner();
                    _owner._canvasBrushSessionOpening = _owner._session.PendingBrushOpening();
                }
            }
            _terminal = FAILED(_captureError);
            _cleanup = true;
            if (_owner._jobs.Resume(ticket))
                return true;
            std::terminate();
        }
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_opening);
        StrokeOpeningOptions options;
        options.settings = captured->settings;
        options.kind = captured->kind;
        options.healing = captured->healing;
        options.maskWhite = captured->maskWhite;
        if (captured->sampledColor)
        {
            const auto color = _owner.ResolveSamplerColor(captured->sampledColor.value());
            options.settings.red = static_cast<double>((color >> 16) & 255u) / 255;
            options.settings.green = static_cast<double>((color >> 8) & 255u) / 255;
            options.settings.blue = static_cast<double>(color & 255u) / 255;
        }
        auto input = _owner._session.PrepareStrokeOpeningInput(std::move(options));
        if (!input)
            _captureError = E_ABORT;
        else
        {
            _payload->input.emplace(std::move(input.value()));
            if (_gate)
                _gate->captures.fetch_add(1);
        }
        _terminal = FAILED(_captureError);
        _cleanup = _terminal;
        if (_owner._jobs.Resume(ticket))
            return true;
        std::terminate();
    }

    void Completed(JobResult result) noexcept override
    {
        HostOutcome outcome = !Current() || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp) ||
                                      _owner._canvasBrushCancelledOpening == _opening
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        HRESULT error = outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled ? E_ABORT
                        : FAILED(_captureError)                                            ? _captureError
                                                                                           : result.error;
        if (_terminalResult.status != JobStatus::Finished && outcome == HostOutcome::Succeeded)
        {
            outcome = _terminalResult.status == JobStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
            error = _terminalResult.error;
        }
        if (outcome == HostOutcome::Succeeded)
        {
            if (!_liveStroke || !_cleanupDone || !_owner._utilityTicket ||
                _owner._session.PendingBrushOpening() != _owner._canvasBrushSessionOpening)
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
            else
            {
                SharedRetainedMemoryLease lease;
                error = _owner._jobs.RetainMemory(_owner._utilityTicket.value(), lease);
                if (FAILED(error) || !_liveStroke->RetainOpeningMemory(std::move(lease)))
                    std::terminate(); // Preclaimed slot, current empty private draft and original callback guarantee
                                      // transfer.
                _liveStroke.reset();  // Session owns the complete draft and its original retained charge now.
            }
        }
        _owner._utilityTicket.reset();
        _owner._utilityStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    void DetachAppliedOpening() noexcept
    {
        if (!_liveStroke)
            return;
        auto* engine = _liveStroke->DetachCoverageForRetirement();
        if (_owner._session.PendingBrushOpening() == _owner._canvasBrushSessionOpening &&
            _owner._session.PendingBrush() == _liveStroke.get())
        {
            if (engine)
                engine->EndStroke(); // Exact empty opening; a replacement's engine must never be ended.
            _owner._session.CancelBrush();
        }
        _retiredStroke = std::move(_liveStroke);
    }
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_opening);
        return !_owner._shutdownStarted && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               captured && captured->documentSerial == _owner._session.DocumentSerial() &&
               captured->target == _owner._session.ActiveLayerID() &&
               captured->mask == _owner._session.IsMaskSelected() && captured->tool == _owner._tool &&
               (!_liveStroke || (_owner._session.PendingBrush() == _liveStroke.get() &&
                                 _owner._session.PendingBrushOpening() == _owner._canvasBrushSessionOpening));
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _opening;
    const std::uint64_t _bytes;
    const std::shared_ptr<StrokeOpeningTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<OpeningPayload, RetireOpeningPayload> _payload;
    HRESULT _captureError = S_OK;
    bool _prepared = false;
    bool _cleanup = false;
    bool _cleanupDone = false;
    bool _terminal = false;
    JobResult _terminalResult;
    std::shared_ptr<BrushStroke> _liveStroke;    // Owner-only empty graphics borrow, never destroyed on worker.
    std::shared_ptr<BrushStroke> _retiredStroke; // Detached whole CPU owner, original worker cleanup only.
};

HRESULT Application::StartStrokeOpening(std::uint64_t opening) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    const auto& document = _session.Document();
    const auto* layer = _session.ActiveLayer();
    if (!captured || !document || !layer || _utilityTicket || !_activeHostRequest ||
        captured->kind == BrushKind::Clone || _session.PendingBrush() || _canvasBrushSessionOpening ||
        captured->documentSerial != _session.DocumentSerial() || captured->target != layer->id ||
        captured->mask != _session.IsMaskSelected())
        return E_UNEXPECTED;
    const auto memory = StrokeOpeningMemoryBytes(*document, *layer, captured->mask,
                                                 _session.RasterEditPixelLimit(*layer, captured->mask),
                                                 captured->settings, captured->kind);
    const auto fixed = sizeof(StrokeOpeningJob) + sizeof(OpeningPayload) + 8192ULL;
    if (!memory || memory > (std::numeric_limits<std::uint64_t>::max)() - fixed)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto bytes = memory + fixed;
    const auto ordinal = _activeHostRequest->ordinal;
    auto stamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Selection);
    if (!stamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        stamp = _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!stamp)
        return E_UNEXPECTED;
    _utilityStamp = stamp;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        std::unique_ptr<Job> job = std::make_unique<StrokeOpeningJob>(*this, *stamp, opening, bytes);
        JobTicket ticket;
        result = _jobs.Submit(job, {JobClass::Stroke, _session.DocumentSerial(), bytes, true, false, true}, ticket);
        if (SUCCEEDED(result))
            _utilityTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor admission boundary. No source capture, coverage, pixels, alignment or history changed.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(*stamp);
        _hostFinishedStamp = stamp;
        _utilityStamp.reset();
        return result;
    }
    _hostCommitStamp = stamp;
    _hostAfterPrerequisite = HostStage::CanvasBrush;
    return S_OK;
}

#include "Application.h"

#include "ChromeHost.h"
#include "HistoryResidencyTestGate.h"
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
struct ResidencyPayload final : DeferredReleaseNode
{
    DocumentHistory::StateView history;
    std::optional<CanvasDocument> document;
    std::optional<DocumentHistory::Publication> publication;
    std::shared_ptr<HistoryResidencyTestGate> gate;
    DWORD ownerThread = 0;
    ResidencyPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ResidencyPayload> owned(static_cast<ResidencyPayload*>(node)); };
    }
    ~ResidencyPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireResidencyPayload final
{
    void operator()(ResidencyPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ResidencyPayload> owned(payload);
        }
    }
};
} // namespace

bool Application::HistoryResidencySource::Start() noexcept
{
    if (_enabled || _attached || !_owner._ownerWork.Attach(*this))
        return false;
    _attached = true;
    if (!_subscription.Start(
            [](void* context) noexcept
            {
                auto& source = *static_cast<HistoryResidencySource*>(context);
                source._ready.store(true, std::memory_order_release);
                source._owner._jobs.NotifyOwnerWork();
            },
            this))
    {
        (void)_owner._ownerWork.Detach(*this);
        _attached = false;
        return false;
    }
    _enabled = true;
    _ready.store(true, std::memory_order_release); // Check existing reference-written or stale-index history too.
    _owner._jobs.NotifyOwnerWork();
    return true;
}

void Application::HistoryResidencySource::StopNotifications() noexcept
{
    _subscription.Stop(); // Join notices BEFORE RequestStop/Close destroys the scheduler's wake/context.
    _enabled = false;
    _ready.store(false, std::memory_order_release);
    _deferred = false;
}

void Application::HistoryResidencySource::CloseAfterWorkers() noexcept
{
    StopNotifications();
    if (_attached)
    {
        if (!_owner._ownerWork.Detach(*this))
            std::terminate(); // Teardown is outside pumps, and notices/pending readiness have joined.
        _attached = false;
    }
}

void Application::HistoryResidencySource::RetryDeferred() noexcept
{
    if (_enabled && _deferred)
    {
        _deferred = false;
        _ready.store(true, std::memory_order_release);
        _owner._jobs.NotifyOwnerWork();
    }
}

bool Application::HistoryResidencySource::HasPending() const noexcept
{
    // A dormant notice behind an open edit is not a poll/wait producer. Its exact finisher rearms it. Active
    // maintenance already owns a scheduler record; only an unconsumed readiness notification needs a pump.
    return HasReady();
}

bool Application::HistoryResidencySource::HasReady() const noexcept
{
    return _enabled && _ready.load(std::memory_order_acquire);
}

void Application::HistoryResidencySource::Pump() noexcept
{
    if (!_enabled || !_ready.exchange(false, std::memory_order_acq_rel) || _owner._shutdownStarted)
        return;
    if (!_owner._session.history.NeedsResidencyMaintenance())
        return;
    // Maintenance is an internal history writer. It must not get a younger ordinal ahead of accepted inputs,
    // invalidate a running writer's captured view, or trim an open transaction while a producer owns it.
    if (!_owner.CanStartHistoryResidencyMaintenance())
    {
        _deferred = true;
        return;
    }
    const HRESULT result = _owner.StartHistoryResidencyMaintenance();
    if (FAILED(result))
    {
        // A failed admission is visible once. No self-rearm, idle retry or busy loop; a later growth/finisher
        // may request a new attempt. No document/history has been captured or changed by failed admission.
        _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_HISTORY_RESIDENCY);
    }
}

class Application::HistoryResidencyJob final : public Job
{
  public:
    HistoryResidencyJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _generation(owner._session.Generation()), _retirement(retirement),
          _gate(owner._historyResidencyTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Admission precedes the resumable owner capture.
        if (_refresh)
        {
            if (_gate)
                _gate->refreshes.fetch_add(1);
            const bool refreshed = _payload->publication &&
                                   DocumentHistory::RefreshPreparedResidency(_payload->publication.value(),
                                                                             _payload->document, context.cancellation);
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                   : refreshed                        ? JobResult{}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
        if (_gate)
        {
            _gate->preparations.fetch_add(1);
            if (_gate->throwPrepareFailure)
                throw std::bad_alloc{};
        }
        _payload->publication = DocumentHistory::PrepareResidencyMaintenance(std::move(_payload->history),
                                                                             _payload->document, context.cancellation);
        if (_payload->publication && _gate && _gate->hold)
        {
            if (!PostThreadMessageW(_ownerThread, HistoryResidencyTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, waits, FALSE, 15000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
               : _payload->publication            ? JobResult{}
                                                  : JobResult{JobStatus::Failed, E_FAIL};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_payload)
        {
            if (!_payload->publication)
                return false;
            const auto decision = _permit.TryAcquire(_payload->publication.value());
            if (decision == DocumentHistory::ResidencyValidation::Ready)
                return false; // Completed follows immediately on this owner drain, before any callback/pump.
            if (decision == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            _refresh = true;
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
            return false;
        }
        try
        {
            _payload.reset(std::make_unique<ResidencyPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->captures.fetch_add(1);
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->history = _owner._session.history.CaptureState();
            _payload->document = std::as_const(_owner._session).Document();
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
        const auto releasePermit = wil::scope_exit([this]() noexcept { _permit.Reset(); });
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
        else if (outcome == HostOutcome::Succeeded && (!_payload || !_payload->publication ||
                                                       !_owner._session.history.Publish(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _permit.Reset();
        if (_gate)
        {
            _gate->published.store(outcome == HostOutcome::Succeeded);
            _gate->cancelled.store(outcome == HostOutcome::Cancelled);
            _gate->failed.store(outcome == HostOutcome::Failed);
            _gate->stale.store(outcome == HostOutcome::Stale);
        }
        _owner._historyResidencyTicket.reset();
        _owner._historyResidencyStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_HISTORY_RESIDENCY);
        if (outcome == HostOutcome::Succeeded)
        {
            // Only history membership changed. Preserve document/generation/user revision/saved marker and
            // settled canvas composition. Refresh the existing history chrome; never synthesize a user edit.
            _owner.SyncChrome();
            _owner.InvalidateFrame();
        }
        _owner.ResumeHostHistory(_stamp, outcome, error);
        _owner._historyResidencySource.RetryDeferred();
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._historyResidencyStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation;
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation;
    const std::uint64_t _retirement;
    const std::shared_ptr<HistoryResidencyTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<ResidencyPayload, RetireResidencyPayload> _payload;
    DocumentHistory::ResidencyPermit _permit;
    bool _refresh = false;
    std::int32_t _captureError = 0;
};

bool Application::CanStartHistoryResidencyMaintenance() const noexcept
{
    // The settled session authority also excludes live brush/warp/shape/lasso/color/transform/move contexts.
    // Captured but unreplayed input owns an older ordinal independently of those session fields.
    return !_shutdownStarted && !_historyResidencyTicket && !_historyWork.Busy() && !_activeHostRequest &&
           _hostRequests.Size() == 0 && !_canvasLifetime && !_canvasBrushInput.Busy() && !_opacityEdit &&
           !_duplicateGesture && std::holds_alternative<std::monostate>(_drag) &&
           _session.history.CaptureState().Depth() == 0 &&
           _session.CanApplyHistoryRestore(_session.CaptureHistoryRestoreAuthority());
}

HRESULT Application::StartHistoryResidencyMaintenance() noexcept
{
    if (!CanStartHistoryResidencyMaintenance())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (!_session.history.NeedsResidencyMaintenance())
        return S_OK;
    const auto ordinal = _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _historyResidencyStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_historyResidencyStamp)
        return E_UNEXPECTED;
    auto memory = _session.history.EditMemoryBytes(std::as_const(_session).Document());
    auto retirement = _session.HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    // Original private index plus one refresh candidate may coexist. Retain the same reservation until its
    // publication/drop aggregate retires; capture and all graph construction follow successful admission.
    constexpr auto overhead = sizeof(HistoryResidencyJob) + sizeof(ResidencyPayload);
    if (memory && retirement && memory <= (maximum - overhead) / 2)
    {
        memory = memory * 2 + overhead;
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_historyResidencyTestGate)
                    _historyResidencyTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job =
                    std::make_unique<HistoryResidencyJob>(*this, _historyResidencyStamp.value(), retirement);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _historyResidencyTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_historyResidencyStamp.value());
        _historyResidencyStamp.reset();
    }
    return result;
}

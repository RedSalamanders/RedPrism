#include "Application.h"

#include "ColorDismissalWork.h"
#include "Runtime.Tests.ColorWorkTestGate.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ColorDismissalPayload final : DeferredReleaseNode
{
    std::optional<ColorDismissalInput> input;
    DocumentHistory::StateView history;
    std::optional<PreparedColorDismissal> publication;
    std::shared_ptr<ColorWorkTestGate> gate;
    DWORD ownerThread = 0;
    ColorDismissalPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ColorDismissalPayload> owned(static_cast<ColorDismissalPayload*>(node)); };
    }
    ~ColorDismissalPayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireColorDismissal final
{
    void operator()(ColorDismissalPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ColorDismissalPayload> owned(payload);
        }
    }
};
} // namespace

class Application::ColorDismissalJob final : public Job
{
  public:
    ColorDismissalJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _retirement(retirement),
          _opening(owner._session.PendingColorEdit()->openingSequence), _sequence(owner._session.ColorEditSequence()),
          _generation(owner._session.Generation()), _gate(owner._colorWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {};
        if (const auto gate = PassColorWorkTestGate(_gate, ColorWorkTestKind::Commit, context);
            gate.status != JobStatus::Finished)
            return gate;
        if (_gate && _gate->kind == ColorWorkTestKind::Commit && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        _payload->publication = DocumentSession::PrepareColorDismissalPublication(
            std::move(_payload->input.value()), std::move(_payload->history), context.cancellation);
        if (_gate && _gate->kind == ColorWorkTestKind::Commit)
            (void)SetEvent(_gate->computed.get());
        return _payload->publication                ? JobResult{}
               : context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                    : JobResult{JobStatus::Failed, E_FAIL};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            auto owned = std::make_unique<ColorDismissalPayload>();
            owned->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            owned->gate = _gate;
            owned->ownerThread = _ownerThread;
            _payload.reset(owned.release());
            if (_gate)
            {
                _gate->captureReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->input = _owner._session.PrepareColorDismissalInput();
            _payload->history = _owner._session.history.CaptureState();
            if (!_payload->input)
            {
                _captureError = E_ABORT;
                return false;
            }
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
        _owner.CompleteColorDismissal(_stamp, Current(), result,
                                      _payload && _payload->publication ? &_payload->publication.value() : nullptr,
                                      _captureError);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto& panel = _owner._session.PendingColorEdit();
        return !_owner._shutdownStarted && _owner._colorCommitStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.ColorEditSequence() == _sequence &&
               !_owner._session.AdjustmentOriginal() && !_owner._session.AdjustmentEditingID() && panel &&
               panel->openingSequence == _opening;
    }
    Application& _owner; // owner stages only; Run never accesses this live owner
    const HistoryWorkStamp _stamp;
    const std::uint64_t _retirement, _opening, _sequence, _generation;
    const std::shared_ptr<ColorWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::int32_t _captureError = 0;
    std::unique_ptr<ColorDismissalPayload, RetireColorDismissal> _payload;
};

Application::HostOutcome Application::StartColorCancellation(std::uint64_t ordinal) noexcept
{
    if (ColorCommitBusy())
        return _colorCommitStamp->ordinal == ordinal ? HostOutcome::Pending : HostOutcome::Failed;
    if (!_session.HasColorEdit())
    {
        _colorCommitError = 0;
        return HostOutcome::Succeeded;
    }
    _colorCommitStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_colorCommitStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _colorCommitStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_colorCommitStamp)
    {
        _colorCommitError = E_UNEXPECTED;
        _colorCommitOutcome = HostOutcome::Failed;
        return HostOutcome::Failed;
    }
    _colorCommitError = 0;
    DropDrag<HueTargetDrag>();
    if (_session.AdjustmentEditingID())
        return StartAdjustmentClosure(ordinal, false);
    std::int32_t status = E_INVALIDARG;
    try
    {
        constexpr std::uint64_t memory = sizeof(ColorDismissalJob) + sizeof(ColorDismissalPayload) +
                                         4 * sizeof(ColorDismissalInput) + 4 * sizeof(PreparedColorDismissal) + 1024;
        const auto held = _session.ImageImportRetirementMemoryBytes();
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        // No final document/history is constructed for destructive dismissal; its retained graph is resident.
        // The full session retirement allowance covers captured history roots and swapped panel/display owners.
        if (held && held <= maximum - 2 * memory)
        {
            if (_colorWorkTestGate)
                _colorWorkTestGate->reservationBytes = memory;
            std::unique_ptr<Job> job =
                std::make_unique<ColorDismissalJob>(*this, _colorCommitStamp.value(), held + 2 * memory);
            JobTicket ticket;
            status = _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket);
            if (status >= 0)
                _colorCommitTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        status = E_OUTOFMEMORY;
    }
    if (status < 0)
    {
        (void)_historyWork.Finish(_colorCommitStamp.value());
        _colorCommitStamp.reset();
        _colorCommitOutcome = HostOutcome::Failed;
        _colorCommitError = status;
        return HostOutcome::Failed;
    }
    _colorCommitOutcome = HostOutcome::Pending;
    (void)_colorPreviews.Close();
    _colorMailboxOpen = false;
    if (_colorPreviewTicket)
    {
        (void)_jobs.Cancel(_colorPreviewTicket.value());
        _colorPreviewTicket.reset();
    }
    DocumentChanged();
    return HostOutcome::Pending;
}

void Application::CompleteColorDismissal(HistoryWorkStamp stamp, bool current, JobResult completion,
                                         PreparedColorDismissal* publication, std::int32_t captureError) noexcept
{
    if (_colorCommitStamp != stamp || !_historyWork.IsCurrent(stamp))
        return;
    const auto outcome =
        !current || captureError == E_ABORT                                                ? HostOutcome::Stale
        : _historyWork.CancelRequested(stamp) || completion.status == JobStatus::Cancelled ? HostOutcome::Cancelled
        : captureError < 0 || completion.status == JobStatus::Failed || !publication       ? HostOutcome::Failed
        : _session.ApplyPreparedColorDismissalIfCurrent(*publication)                      ? HostOutcome::Succeeded
                                                                                           : HostOutcome::Stale;
    (void)_historyWork.Finish(stamp);
    _colorCommitStamp.reset();
    _colorCommitTicket.reset();
    _colorCommitOutcome = outcome;
    _colorCommitError = outcome == HostOutcome::Failed      ? (captureError < 0       ? captureError
                                                               : completion.error < 0 ? completion.error
                                                                                      : E_FAIL)
                        : outcome == HostOutcome::Succeeded ? 0
                                                            : E_ABORT;
    ResumeHostHistory(stamp, outcome, _colorCommitError);
    DocumentChanged();
    if (outcome == HostOutcome::Failed)
        QueueColorFailure();
}

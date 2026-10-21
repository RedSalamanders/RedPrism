#include "Application.h"

#include "DuplicateOpeningWork.h"
#include "FloatingMergeWork.h"
#include "FloatingOpeningWork.h"
#include "Runtime.Tests.TransformWorkTestGate.h"
#include "TransformCancellationWork.h"
#include "TransformCommitWork.h"
#include "resource.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
template <typename T> struct RetireTransformOwner final
{
    void operator()(T* value) const noexcept
    {
        if (value && !TryDeferRelease(*value))
        {
            const std::unique_ptr<T> owned(value);
        }
    }
};

HRESULT StatusError(FloatingPixelsStatus status) noexcept
{
    return status == FloatingPixelsStatus::Memory     ? E_OUTOFMEMORY
           : status == FloatingPixelsStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                      : E_INVALIDARG;
}
HRESULT StatusError(TransformPixelsStatus status) noexcept
{
    return status == TransformPixelsStatus::Memory        ? E_OUTOFMEMORY
           : status == TransformPixelsStatus::Unsupported ? E_NOTIMPL
                                                          : E_INVALIDARG;
}
} // namespace

#pragma warning(push)
#pragma warning(disable : 4324)
struct Application::TransformRestoreOwner final : DeferredReleaseNode
{
    std::shared_ptr<PreparedFloatingRestore> restore;
    std::shared_ptr<PreparedDuplicateRestore> duplicateRestore;
    std::shared_ptr<TransformWorkTestGate> gate;
    DWORD ownerThread = 0;
    TransformRestoreOwner() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<TransformRestoreOwner> owned(static_cast<TransformRestoreOwner*>(value)); };
    }
    ~TransformRestoreOwner()
    {
        if (gate)
        {
            gate->restoreRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->restoreRetired.store(true);
        }
    }
};
#pragma warning(pop)

// The selected ordinal owns the job through its admission, capture, worker work, completion and retirement.
// A successful floating opening retains a separate preallocated restoration node until its edit settles.
class Application::TransformJob final : public Job
{
#pragma warning(push)
#pragma warning(disable : 4324)
    struct Payload final : DeferredReleaseNode
    {
        std::optional<FloatingOpeningInput> openingInput;
        std::optional<DuplicateOpeningInput> duplicateInput;
        std::optional<TransformCommitInput> commitInput;
        std::optional<FloatingMergeInput> mergeInput;
        std::optional<TransformCancellationInput> cancelInput;
        std::optional<FloatingOpeningResult> opening;
        std::optional<DuplicateOpeningResult> duplicated;
        std::optional<TransformCommitResult> committed;
        std::optional<FloatingMergeResult> merged;
        std::optional<PreparedTransformCancellation> cancelled;
        std::shared_ptr<TransformRestoreOwner> openingOwner;
        std::shared_ptr<TransformRestoreOwner> restoreOwner;
        std::shared_ptr<TransformWorkTestGate> gate;
        DWORD ownerThread = 0;
        Payload() noexcept
        {
            bytes = kDeferredReleaseThreshold;
            destroy = [](DeferredReleaseNode* value) noexcept
            { const std::unique_ptr<Payload> owned(static_cast<Payload*>(value)); };
        }
        ~Payload()
        {
            if (gate)
            {
                gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
                gate->retired.store(true);
            }
        }
    };
#pragma warning(pop)

  public:
    [[nodiscard]] static constexpr std::uint64_t MetadataBytes() noexcept
    {
        return sizeof(TransformJob) + sizeof(Payload) + sizeof(TransformRestoreOwner) + 8192;
    }

    TransformJob(Application& owner, TransformWorkKind kind, HistoryWorkStamp stamp, std::uint64_t retirement)
        : _owner(owner), _kind(kind), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()),
          _revision(owner._session.TransformDraftRevision()), _retirement(retirement),
          _gate(owner._transformWorkTestGate &&
                        static_cast<TransformWorkKind>(owner._transformWorkTestGate->kind) == kind
                    ? owner._transformWorkTestGate
                    : nullptr),
          _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Every capture/allocation follows the complete reservation.
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        const DocumentHistory::PrepareObserver observer{[](void* value, DocumentHistory::PrepareStage stage)
                                                        {
                                                            auto* gate = static_cast<TransformWorkTestGate*>(value);
                                                            if (gate && gate->historyFailure == stage)
                                                            {
                                                                gate->historyFaultHits.fetch_add(1);
                                                                throw std::bad_alloc{};
                                                            }
                                                        },
                                                        _gate.get()};
        if (_kind == TransformWorkKind::OpenFloating)
        {
            _payload->opening.emplace(DocumentSession::PrepareFloatingOpeningPublication(
                std::move(_payload->openingInput.value()), context.executor, context.cancellation, observer, {}));
            _payload->openingInput.reset();
            if (_payload->opening->publication)
            {
                std::unique_ptr<TransformRestoreOwner, RetireTransformOwner<TransformRestoreOwner>> node(
                    std::make_unique<TransformRestoreOwner>().release());
                node->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
                node->restore = _payload->opening->publication->Restoration();
                node->gate = _gate;
                node->ownerThread = _ownerThread;
                _payload->openingOwner = std::shared_ptr<TransformRestoreOwner>(std::move(node));
            }
        }
        else if (_kind == TransformWorkKind::OpenDuplicate)
        {
            _payload->duplicated.emplace(DocumentSession::PrepareDuplicateOpeningPublication(
                std::move(_payload->duplicateInput.value()), context.executor, context.cancellation, observer, {}));
            _payload->duplicateInput.reset();
            if (_payload->duplicated->publication)
            {
                std::unique_ptr<TransformRestoreOwner, RetireTransformOwner<TransformRestoreOwner>> node(
                    std::make_unique<TransformRestoreOwner>().release());
                node->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
                node->duplicateRestore = _payload->duplicated->publication->Restoration();
                node->gate = _gate;
                node->ownerThread = _ownerThread;
                _payload->openingOwner = std::shared_ptr<TransformRestoreOwner>(std::move(node));
            }
        }
        else if (_kind == TransformWorkKind::Cancel)
        {
            auto cancelled = DocumentSession::PrepareTransformCancellationPublication(
                std::move(_payload->cancelInput.value()), context.cancellation, observer);
            if (cancelled)
                _payload->cancelled.emplace(std::move(cancelled.value()));
            _payload->cancelInput.reset();
        }
        else if (_payload->mergeInput)
        {
            _payload->merged.emplace(DocumentSession::PrepareFloatingMergePublication(
                std::move(_payload->mergeInput.value()), context.executor, context.cancellation, observer, {}));
            _payload->mergeInput.reset();
        }
        else
        {
            _payload->committed.emplace(DocumentSession::PrepareTransformCommitPublication(
                std::move(_payload->commitInput.value()), context.executor, context.cancellation, observer));
            _payload->commitInput.reset();
        }
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() == _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload.reset(std::make_unique<Payload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_kind == TransformWorkKind::OpenFloating)
            {
                auto input = _owner._session.PrepareFloatingOpeningInput();
                if (input)
                    _payload->openingInput.emplace(std::move(input.value()));
            }
            else if (_kind == TransformWorkKind::OpenDuplicate)
            {
                auto input = _owner._session.PrepareDuplicateOpeningInput();
                if (input)
                    _payload->duplicateInput.emplace(std::move(input.value()));
            }
            else if (_kind == TransformWorkKind::Cancel)
            {
                auto input = _owner._session.PrepareTransformCancellationInput();
                if (input)
                    _payload->cancelInput.emplace(std::move(input.value()));
            }
            else if (_owner._session.PendingTransformEdit() && _owner._session.PendingTransformEdit()->floating)
            {
                _payload->restoreOwner = _owner._transformRestoreOwner;
                if (_payload->restoreOwner)
                {
                    auto input = _owner._session.PrepareFloatingMergeInput(_payload->restoreOwner->restore);
                    if (input)
                        _payload->mergeInput.emplace(std::move(input.value()));
                }
            }
            else
            {
                _payload->restoreOwner = _owner._transformRestoreOwner;
                auto input = _owner._session.PrepareTransformCommitInput();
                if (input)
                    _payload->commitInput.emplace(std::move(input.value()));
            }
            if (!_payload->openingInput && !_payload->duplicateInput && !_payload->cancelInput &&
                !_payload->mergeInput && !_payload->commitInput)
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
        const bool current = Current();
        HostOutcome outcome = !current || _captureError == E_ABORT ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        HRESULT error = FAILED(_captureError) ? _captureError : result.error;
        bool changed = false;
        bool refusalReportedByModel = false;
        if (outcome == HostOutcome::Succeeded)
        {
            if (_kind == TransformWorkKind::OpenFloating)
            {
                const auto status =
                    _payload && _payload->opening ? _payload->opening->status : FloatingPixelsStatus::Invalid;
                if (status == FloatingPixelsStatus::Ready && _payload->opening->publication && _payload->openingOwner)
                {
                    changed =
                        _owner._session.ApplyPreparedFloatingOpeningIfCurrent(_payload->opening->publication.value());
                    if (changed)
                    {
                        (void)_owner._session.DeferTransform();
                        std::swap(_owner._transformRestoreOwner, _payload->openingOwner);
                    }
                    else
                        outcome = HostOutcome::Stale;
                }
                else
                {
                    outcome = status == FloatingPixelsStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
                    error = StatusError(status);
                }
            }
            else if (_kind == TransformWorkKind::OpenDuplicate)
            {
                const auto status =
                    _payload && _payload->duplicated ? _payload->duplicated->status : DuplicateOpeningStatus::Invalid;
                if (status == DuplicateOpeningStatus::Ready && _payload->duplicated->publication &&
                    _payload->openingOwner)
                {
                    changed = _owner._session.ApplyPreparedDuplicateOpeningIfCurrent(
                        _payload->duplicated->publication.value());
                    if (changed)
                    {
                        std::swap(_owner._transformRestoreOwner, _payload->openingOwner);
                        _owner.PublishDuplicateGesture();
                    }
                    else
                        outcome = HostOutcome::Stale;
                }
                else
                {
                    outcome =
                        status == DuplicateOpeningStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
                    error = status == DuplicateOpeningStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                                       : E_INVALIDARG;
                }
            }
            else if (_kind == TransformWorkKind::Cancel)
            {
                changed = _payload && _payload->cancelled &&
                          _owner._session.ApplyPreparedTransformCancellationIfCurrent(_payload->cancelled.value());
                if (!changed)
                    outcome = HostOutcome::Stale;
            }
            else if (_payload && _payload->merged)
            {
                const auto status = _payload->merged->status;
                changed = _payload->merged->publication &&
                          _owner._session.ApplyPreparedFloatingMergeIfCurrent(_payload->merged->publication.value());
                if (!changed)
                    outcome = HostOutcome::Stale;
                else
                {
                    _owner._transformRestoreOwner
                        .reset(); // Payload retains the complete owner until off-owner retirement.
                    if (status != FloatingPixelsStatus::Ready && status != FloatingPixelsStatus::Unchanged)
                    {
                        outcome =
                            status == FloatingPixelsStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
                        error = StatusError(status);
                        refusalReportedByModel = status == FloatingPixelsStatus::TooLarge;
                    }
                }
            }
            else
            {
                const auto status =
                    _payload && _payload->committed ? _payload->committed->status : TransformPixelsStatus::Invalid;
                if (status == TransformPixelsStatus::Ready && _payload->committed->publication)
                {
                    changed =
                        _owner._session.ApplyPreparedTransformCommitIfCurrent(_payload->committed->publication.value());
                    if (!changed)
                        outcome = HostOutcome::Stale;
                    else if (_payload->restoreOwner)
                        _owner.RetireTransformRestore();
                }
                else
                {
                    outcome = status == TransformPixelsStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
                    error = StatusError(status);
                }
            }
        }
        // A reached worker/capture allocation failure or acknowledged cancellation can always restore the exact
        // floating opening without allocation. A newer draft or replacement fails Current and remains untouched.
        if (current && outcome != HostOutcome::Succeeded && _kind == TransformWorkKind::Commit &&
            _owner.RestoreTransformOpening())
        {
            changed = true;
        }
        if (outcome == HostOutcome::Failed && !refusalReportedByModel && !_owner._shutdownStarted)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        _owner._transformWorkTicket.reset();
        _owner._transformWorkStamp.reset();
        if (changed)
            _owner.DocumentChanged();
        else
        {
            _owner.SyncChrome();
            _owner._frameInvalidated = true;
        }
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome,
                                 outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled ? E_ABORT : error);
    }

  private:
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, TransformWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted &&
               (_kind != TransformWorkKind::OpenDuplicate || !_owner._duplicateGesture ||
                !_owner._duplicateGesture->abandoned) &&
               _owner._transformWorkStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.TransformDraftRevision() == _revision && _owner._session.CanApplyImageImport(_authority);
    }
    Application& _owner;
    const TransformWorkKind _kind;
    const HistoryWorkStamp _stamp;
    const ImageImportAuthority _authority;
    const std::uint64_t _revision;
    const std::uint64_t _retirement;
    const std::shared_ptr<TransformWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<Payload, RetireTransformOwner<Payload>> _payload;
    HRESULT _captureError = S_OK;
};

void Application::RetireTransformRestore() noexcept
{
    _transformRestoreOwner.reset(); // Custom CPU-node deleter schedules destruction on the release lane.
}

bool Application::RestoreTransformOpening() noexcept
{
    if (!_transformRestoreOwner)
        return false;
    const bool changed =
        _transformRestoreOwner->restore
            ? _session.ApplyPreparedFloatingRestoreIfCurrent(*_transformRestoreOwner->restore)
            : _transformRestoreOwner->duplicateRestore &&
                  _session.ApplyPreparedDuplicateRestoreIfCurrent(*_transformRestoreOwner->duplicateRestore);
    if (changed)
        RetireTransformRestore();
    return changed;
}

void Application::ExecuteLayerNudge(Vec2 offset) noexcept
{
    const bool editing = _session.PendingTransformEdit().has_value();
    if (!editing)
        (void)_session.BeginDeferredTransform(false);
    if (!_session.PendingTransformEdit())
        return;                              // Nontransformable targets preserve their existing no-op command behavior.
    _session.NudgeLayer(offset.x, offset.y); // An existing draft changes metadata only; it cannot commit inline.
    if (!editing)
    {
        _hostExecutionError = StartTransformWork(TransformWorkKind::Commit);
        if (FAILED(_hostExecutionError))
        {
            ++_errorsShown;
            _lastErrorId = _hostExecutionError == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
                                                                                           : IDS_ERROR_PIXEL_EDIT;
            _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
        }
    }
    DocumentChanged();
}

HRESULT Application::StartTransformWork(TransformWorkKind kind) noexcept
{
    if (!_activeHostRequest || _transformWorkTicket || _historyWork.Busy())
        return E_UNEXPECTED;
    CancelTransformPreviewJobs();
    const auto& pending = _session.PendingTransformEdit();
    const bool opening = kind == TransformWorkKind::OpenFloating || kind == TransformWorkKind::OpenDuplicate;
    if (!_session.Document() || (!opening && !pending) ||
        (kind == TransformWorkKind::OpenFloating && !_session.CanTransformSelection()))
        return S_FALSE;
    if (!opening && pending->floating && (!_transformRestoreOwner || !_transformRestoreOwner->restore))
        return E_ABORT;
    if (kind == TransformWorkKind::Cancel &&
        (pending->floating || (_transformRestoreOwner && _transformRestoreOwner->duplicateRestore)))
    {
        // Cancellation of an exposed floating layer uses its already prepared restoration after preceding
        // history work acknowledged. No new graph, reservation, document copy or history allocation is needed.
        if (!RestoreTransformOpening())
            return E_ABORT;
        DocumentChanged();
        return S_FALSE;
    }
    const auto prepared = kind == TransformWorkKind::OpenFloating    ? _session.FloatingOpeningPreparationMemoryBytes()
                          : kind == TransformWorkKind::OpenDuplicate ? _session.DuplicateOpeningPreparationMemoryBytes()
                          : kind == TransformWorkKind::Cancel ? _session.TransformCancellationPreparationMemoryBytes()
                          : pending->floating && _transformRestoreOwner && _transformRestoreOwner->restore
                              ? _session.FloatingMergePreparationMemoryBytes(*_transformRestoreOwner->restore)
                              : _session.TransformCommitPreparationMemoryBytes();
    const auto held = _session.ImageImportRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t metadata = TransformJob::MetadataBytes();
    if (!prepared || !held || prepared > maximum - metadata || prepared + metadata > (maximum - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto whole = prepared + metadata;
    const auto ordinal = _activeHostRequest->ordinal;
    _transformWorkStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_transformWorkStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _transformWorkStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_transformWorkStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        if (_transformWorkTestGate && static_cast<TransformWorkKind>(_transformWorkTestGate->kind) == kind &&
            _transformWorkTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<TransformJob>(*this, kind, _transformWorkStamp.value(), held + whole * 2);
        JobTicket ticket;
        result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), whole}, ticket));
        if (SUCCEEDED(result))
        {
            _transformWorkTicket = ticket;
            if (_transformWorkTestGate && static_cast<TransformWorkKind>(_transformWorkTestGate->kind) == kind)
                _transformWorkTestGate->reservationBytes = whole;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_transformWorkStamp.value());
        _hostFinishedStamp = _transformWorkStamp;
        _transformWorkStamp.reset();
        if (kind == TransformWorkKind::Commit && RestoreTransformOpening())
        {
            DocumentChanged();
        }
        return result;
    }
    _hostCommitStamp = _transformWorkStamp;
    if (_hostStage == HostStage::Execute)
        _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

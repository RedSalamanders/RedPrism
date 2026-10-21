#include "Application.h"

#include "Runtime.Tests.TransformPreviewTestGate.h"
#include "TransformPreviewWork.h"
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
struct TransformPreviewPayload final : DeferredReleaseNode
{
    std::optional<TransformPreviewInput> input;
    std::optional<TransformPreviewResult> prepared;
    std::shared_ptr<TransformPreviewTestGate> gate;
    DWORD ownerThread = 0;
    TransformPreviewPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<TransformPreviewPayload> owned(static_cast<TransformPreviewPayload*>(value)); };
    }
    ~TransformPreviewPayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)

struct RetireTransformPreviewPayload final
{
    void operator()(TransformPreviewPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value))
        {
            const std::unique_ptr<TransformPreviewPayload> owned(value);
        }
    }
};
} // namespace

class Application::TransformPreviewJob final : public Job
{
  public:
    TransformPreviewJob(Application& owner, TransformPreviewRequest request, PreviewStamp stamp,
                        std::uint64_t retirement)
        : _owner(owner), _request(request), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()),
          _retirement(retirement), _gate(owner._transformPreviewTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // The selected preview's complete reservation precedes capture and all graph allocations.
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        _payload->prepared.emplace(DocumentSession::PrepareTransformPreviewPublication(
            std::move(_payload->input.value()), context.executor, context.cancellation, {}));
        _payload->input.reset();
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() || _payload->prepared->status == DistortWorkStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current())
            return false;
        try
        {
            _payload.reset(std::make_unique<TransformPreviewPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->captureReservationHeld = _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            auto input = _owner._session.PrepareTransformPreviewInput();
            if (!input)
            {
                _captureError = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
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
        bool produced = false;
        const bool current = Current();
        HRESULT error = FAILED(_captureError) ? _captureError : result.error;
        if (current && result.status == JobStatus::Finished && SUCCEEDED(error) && _payload && _payload->prepared)
        {
            const auto status = _payload->prepared->status;
            if (status == DistortWorkStatus::Ready && _payload->prepared->publication)
                produced =
                    _owner._session.ApplyPreparedTransformPreviewIfCurrent(_payload->prepared->publication.value());
            else if (status != DistortWorkStatus::Cancelled)
                error = status == DistortWorkStatus::Memory ? E_OUTOFMEMORY : E_INVALIDARG;
        }
        if (current && FAILED(error) && error != E_ABORT && result.status != JobStatus::Cancelled)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId = IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        _owner._transformPreviewTicket.reset();
        (void)_owner._transformPreviews.Finish(_stamp, produced);
        if (produced)
        {
            _owner._composition.InvalidateDocument();
            _owner.InvalidateFrame();
        }
        // Re-arm only after acknowledgement. The next source dispatch follows record/reservation retirement;
        // accepted history inputs take priority and never wait inside a completion callback.
        if (_owner._transformPreviewMailboxOpen && _owner._transformPreviews.HasPending())
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
    }

  private:
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, TransformPreviewTestGate::kMessage, 0, 0))
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
        const auto& pending = _owner._session.PendingTransformEdit();
        return !_owner._shutdownStarted && pending && pending->deferred && pending->opening == _request.opening &&
               _owner._session.TransformDraftRevision() == _request.revision &&
               _owner._session.Generation() == _request.generation && _owner._transformPreviews.CanPublish(_stamp) &&
               !_owner._transformWorkTicket && _owner._session.CanApplyImageImport(_authority);
    }
    Application& _owner;
    const TransformPreviewRequest _request;
    const PreviewStamp _stamp;
    const ImageImportAuthority _authority;
    const std::uint64_t _retirement;
    const std::shared_ptr<TransformPreviewTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<TransformPreviewPayload, RetireTransformPreviewPayload> _payload;
    HRESULT _captureError = S_OK;
};

void Application::CancelTransformPreviewJobs() noexcept
{
    (void)_transformPreviews.Close();
    _transformPreviewMailboxOpen = false;
    _transformPreviewSeenRevision = 0;
    if (_transformPreviewTicket)
        (void)_jobs.Cancel(_transformPreviewTicket.value());
    // Retain the ticket until the old epoch acknowledges; reopening cannot create a second active graph.
}

void Application::RefreshTransformPreviewJobs() noexcept
{
    const auto& pending = _session.PendingTransformEdit();
    if (_shutdownStarted || _transformWorkTicket || !pending || !pending->deferred)
    {
        if (_transformPreviewMailboxOpen)
            CancelTransformPreviewJobs();
        return;
    }
    if (_transformPreviewMailboxOpen &&
        (_transformPreviewSerial != _session.DocumentSerial() || _transformPreviewOpening != pending->opening ||
         _transformPreviewGeneration != _session.Generation()))
        CancelTransformPreviewJobs();
    if (!_transformPreviewMailboxOpen)
    {
        if (!_transformPreviews.Begin())
            return;
        _transformPreviewMailboxOpen = true;
        _transformPreviewSerial = _session.DocumentSerial();
        _transformPreviewOpening = pending->opening;
        _transformPreviewGeneration = _session.Generation();
    }
    const auto revision = _session.TransformDraftRevision();
    if (_transformPreviewSeenRevision != revision)
    {
        TransformPreviewRequest request{pending->opening, revision, _session.Generation()};
        if (_transformPreviews.Offer(request).accepted)
            _transformPreviewSeenRevision = revision;
    }
    LaunchTransformPreview();
}

void Application::LaunchTransformPreview() noexcept
{
    if (_shutdownStarted || _transformPreviewTicket || _transformWorkTicket || _historyWork.Busy() ||
        _activeHostRequest || _hostRequests.Size())
        return;
    if (auto launch = _transformPreviews.TakeNext())
    {
        const auto result = SubmitTransformPreview(launch->input, launch->stamp);
        if (FAILED(result))
        {
            (void)_transformPreviews.Finish(launch->stamp, false);
            if (result != E_ABORT)
            {
                ++_errorsShown;
                _lastErrorId =
                    result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
                _prompts->PostNotice(_window.get(), kNoticeMessage, _lastErrorId);
            }
        }
    }
}

HRESULT Application::SubmitTransformPreview(TransformPreviewRequest request, PreviewStamp stamp) noexcept
{
    const auto& pending = _session.PendingTransformEdit();
    if (!pending || !pending->deferred || pending->opening != request.opening ||
        _session.TransformDraftRevision() != request.revision || _session.Generation() != request.generation)
        return E_ABORT;
    const auto prepared = _session.TransformPreviewPreparationMemoryBytes();
    const auto held = _session.ImageImportRetirementMemoryBytes();
    constexpr auto metadata = sizeof(TransformPreviewJob) + sizeof(TransformPreviewPayload) + 4096;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!prepared || !held || prepared > maximum - metadata || prepared + metadata > (maximum - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto whole = prepared + metadata;
    try
    {
        if (_transformPreviewTestGate && _transformPreviewTestGate->throwAdmissionFailure)
            throw std::bad_alloc{};
        std::unique_ptr<Job> job = std::make_unique<TransformPreviewJob>(*this, request, stamp, held + whole * 2);
        JobTicket ticket;
        const auto result = static_cast<HRESULT>(
            _jobs.Submit(job, {JobClass::InteractivePreview, _session.DocumentSerial(), whole}, ticket));
        if (SUCCEEDED(result))
        {
            _transformPreviewTicket = ticket;
            if (_transformPreviewTestGate)
                _transformPreviewTestGate->reservationBytes = whole;
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

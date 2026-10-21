#include "Application.h"

#include "ChromeHost.h"
#include "ImageImportHistory.h"
#include "Runtime.Tests.DocumentLifecycleTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct LifecyclePayload final : DeferredReleaseNode
{
    DocumentHistory::StateView history;
    std::optional<PreparedImageImport> publication;
    std::wstring retiredPath;
    std::shared_ptr<DocumentLifecycleTestGate> gate;
    DWORD ownerThread = 0;
    LifecyclePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<LifecyclePayload> owned(static_cast<LifecyclePayload*>(value)); };
    }
    ~LifecyclePayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.fetch_add(1);
        }
    }
};
#pragma warning(pop)
struct RetireLifecyclePayload final
{
    void operator()(LifecyclePayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value))
        {
            const std::unique_ptr<LifecyclePayload> owned(value);
        }
    }
};
} // namespace

std::optional<std::array<int, 2>> Application::NewCanvasDimensions(const HostSizeDraft& draft) noexcept
{
    if (!std::isfinite(draft.newWidth) || !std::isfinite(draft.newHeight))
        return std::nullopt;
    const double width = std::round(draft.newWidth);
    const double height = std::round(draft.newHeight);
    if (width < 1 || width > kMaxCanvasSide || height < 1 || height > kMaxCanvasSide)
        return std::nullopt;
    return std::array<int, 2>{static_cast<int>(width), static_cast<int>(height)};
}

class Application::DocumentLifecycleJob final : public Job
{
  public:
    DocumentLifecycleJob(Application& owner, HistoryWorkStamp stamp, std::optional<std::array<int, 2>> dimensions,
                         std::optional<std::uint64_t> invocation, std::uint64_t retirement)
        : _owner(owner), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()),
          _dimensions(dimensions), _invocation(invocation), _retirement(retirement),
          _gate(owner._documentLifecycleTestGate), _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {}; // Complete admission precedes source/history capture on an owner completion pass.
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwPreparationFailure)
            throw std::bad_alloc{};
        std::optional<CanvasDocument> replacement;
        std::optional<Guid> active;
        if (_dimensions)
        {
            CanvasDocument document;
            document.id = Guid::Generate();
            document.width = (*_dimensions)[0];
            document.height = (*_dimensions)[1];
            document.layers.push_back(ImageLayer::Blank(
                L"Layer 1", Vec2{static_cast<double>(document.width), static_cast<double>(document.height)}));
            active = document.layers.front().id;
            replacement.emplace(std::move(document));
        }
        const DocumentHistory::PrepareObserver observer{[](void* opaque, DocumentHistory::PrepareStage stage)
                                                        {
                                                            auto& gate =
                                                                *static_cast<DocumentLifecycleTestGate*>(opaque);
                                                            if (gate.historyFailure == stage)
                                                            {
                                                                gate.historyFaultHits.fetch_add(1);
                                                                throw std::bad_alloc{};
                                                            }
                                                        },
                                                        _gate.get()};
        _payload->publication = DocumentSession::PrepareDocumentLifecyclePublication(
            _authority, std::move(_payload->history), std::move(replacement), active, _dimensions ? L"New Canvas" : L"",
            context.cancellation, _gate ? observer : DocumentHistory::PrepareObserver{});
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        return _payload->publication ? JobResult{} : JobResult{JobStatus::Failed, E_INVALIDARG};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_payload || result.status != JobStatus::Finished || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            _payload.reset(std::make_unique<LifecyclePayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->captureReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->history = _owner._session.history.CaptureState();
            if (_owner._jobs.Resume(ticket))
                return true;
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY; // The entire accepted operation fails without live mutation.
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        HostOutcome outcome = !Current() ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        std::int32_t error = FAILED(_captureError) ? _captureError : result.error;
        bool replaced = false;
        if (outcome == HostOutcome::Succeeded)
        {
            if (!_payload || !_payload->publication ||
                !_owner._session.ApplyPreparedImageImportIfCurrent(_payload->publication.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
            else if (_payload->publication->ReplacesDocument())
            {
                std::swap(_owner._documentPath, _payload->retiredPath);
                _owner.FinishPreparedDocumentReplacement();
                _owner._viewport.followsFit = true;
                _owner.DocumentChanged();
                // Rebind only the selected input whose validated publication installed this new lifetime.
                _owner._activeHostRequest->documentSerial = _owner._session.DocumentSerial();
                replaced = true;
            }
        }
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId = IDS_ERROR_DOCUMENT_LIFECYCLE;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
            error = E_ABORT;
        _owner._documentLifecycleTicket.reset();
        _owner._documentLifecycleStamp.reset();
        if (replaced)
            (void)_owner._historyWork.FinishReplacement(_stamp, _owner._session.DocumentSerial());
        else
            (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        const auto* size = _owner._activeHostRequest
                               ? std::get_if<HostSizeApplyInput>(&_owner._activeHostRequest->input.Input())
                               : nullptr;
        const bool outlineCurrent =
            !size || size->draft.kind != SizeCardKind::NewCanvas || !size->selectionMoveOpening ||
            (_owner.CanQueueSelectionSizeOutline() && !_owner._session.IsMovingSelection() && _owner.SizeCardOpen());
        return !_owner._shutdownStarted && _owner._documentLifecycleStamp == _stamp && outlineCurrent &&
               _owner._hostCommitStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               (!_invocation || _owner._sizeInvocation == _invocation.value()) &&
               _owner._session.CanApplyImageImport(_authority);
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, DocumentLifecycleTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // Owner callbacks only. Run reads immutable values and the admitted private payload.
    const HistoryWorkStamp _stamp;
    const ImageImportAuthority _authority;
    const std::optional<std::array<int, 2>> _dimensions;
    const std::optional<std::uint64_t> _invocation;
    const std::uint64_t _retirement;
    const std::shared_ptr<DocumentLifecycleTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<LifecyclePayload, RetireLifecyclePayload> _payload;
    HRESULT _captureError = S_OK;
};

HRESULT Application::StartDocumentLifecycle(std::optional<std::array<int, 2>> dimensions,
                                            std::optional<std::uint64_t> invocation) noexcept
{
    if (_documentLifecycleTicket || !_activeHostRequest ||
        (dimensions && ((*dimensions)[0] < 1 || (*dimensions)[0] > kMaxCanvasSide || (*dimensions)[1] < 1 ||
                        (*dimensions)[1] > kMaxCanvasSide)))
        return E_INVALIDARG;
    const auto ordinal = _activeHostRequest->ordinal;
    _documentLifecycleStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_documentLifecycleStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _documentLifecycleStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_documentLifecycleStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        auto memory = _session.history.EditMemoryBytes(std::nullopt);
        auto retirement = _session.ImageImportRetirementMemoryBytes();
        const auto add = [](std::uint64_t& value, std::uint64_t more) noexcept
        {
            if (more > (std::numeric_limits<std::uint64_t>::max)() - value)
                return false;
            value += more;
            return true;
        };
        // Lifecycle captures only constant-sized authority/history roots. The fixed allowance covers the
        // one blank layer, selected-id/retirement holders and replacement snapshot metadata (no pixel buffers).
        if (memory && retirement &&
            _documentPath.capacity() < (std::numeric_limits<std::uint64_t>::max)() / sizeof(wchar_t) &&
            add(retirement, (_documentPath.capacity() + 1) * sizeof(wchar_t)) &&
            add(memory, 128 * 1024 + sizeof(DocumentLifecycleJob) + sizeof(LifecyclePayload)) &&
            add(retirement, _jobs.ReservedMemoryBytes()) && memory <= (std::numeric_limits<std::uint64_t>::max)() / 4 &&
            add(retirement, memory * 4))
        {
            if (_documentLifecycleTestGate)
            {
                _documentLifecycleTestGate->reservationBytes = memory;
                if (_documentLifecycleTestGate->throwAdmissionFailure)
                    throw std::bad_alloc{};
            }
            std::unique_ptr<Job> job = std::make_unique<DocumentLifecycleJob>(*this, _documentLifecycleStamp.value(),
                                                                              dimensions, invocation, retirement);
            JobTicket ticket;
            result =
                static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
            if (SUCCEEDED(result))
                _documentLifecycleTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // Admission/construction failure refuses the entire operation.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_documentLifecycleStamp.value());
        _hostFinishedStamp = _documentLifecycleStamp;
        _documentLifecycleStamp.reset();
        return result;
    }
    _hostCommitStamp = _documentLifecycleStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

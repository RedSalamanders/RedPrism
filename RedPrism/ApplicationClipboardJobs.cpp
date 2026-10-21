#include "Application.h"
#include "ChromeHost.h"
#include "Runtime.Tests.ClipboardWorkTestGate.h"

#include "ClipboardCommandWork.h"
#include "ClipboardEncodingBudget.h"
#include "ClipboardPasteWork.h"
#include "ClipboardTransfer.h"
#include "CopyPixelsWork.h"
#include "ImageCodecs.h"
#include "ImageImportHistory.h"
#include "RecordedDocumentReadbackJob.h"
#include "SharedPublicationMemory.h"
#include "resource.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
void TraceClipboardOwnerForTest(const char* boundary) noexcept
{
    const DWORD saved = GetLastError();
    DWORD process = 0;
    const HWND owner = GetClipboardOwner();
    const DWORD thread = owner ? GetWindowThreadProcessId(owner, &process) : 0;
    std::printf("DIAG clipboard-owner boundary=%s sequence=%lu owner=%p pid=%lu thread=%lu self_pid=%lu\n", boundary,
                static_cast<unsigned long>(GetClipboardSequenceNumber()), static_cast<void*>(owner),
                static_cast<unsigned long>(process), static_cast<unsigned long>(thread),
                static_cast<unsigned long>(GetCurrentProcessId()));
    SetLastError(saved);
}
JobResult PassClipboardGate(const std::shared_ptr<ClipboardWorkTestGate>& gate, DWORD ownerThread,
                            ClipboardWorkPhase phase, JobContext& context)
{
    if (!gate || gate->phase != phase || gate->visits.fetch_add(1) != 0)
        return {};
    if (!PostThreadMessageW(ownerThread, ClipboardWorkTestGate::kMessage, 0, 0))
        return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
    const HANDLE handles[]{gate->release.get(), context.shutdownEvent};
    const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
    if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
        return {JobStatus::Cancelled};
    if (waited != WAIT_OBJECT_0)
        return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    if (gate->throwWorkerFailure)
        throw std::bad_alloc{};
    return gate->result;
}
ClipboardTransfer::Observer ClipboardObserver(const std::shared_ptr<ClipboardWorkTestGate>& gate) noexcept
{
    if (!gate)
        return {};
    return {gate.get(),
            [](void* raw, ClipboardTransfer::Stage stage) noexcept
            {
                auto& state = *static_cast<ClipboardWorkTestGate*>(raw);
                if (stage == ClipboardTransfer::Stage::Opened)
                    state.openedScopes.fetch_add(1);
                if (stage == ClipboardTransfer::Stage::Emptied)
                {
                    state.openAttemptsAtEmpty.store(state.openAttempts.load());
                    state.emptyAttempts.fetch_add(1);
                }
                if (stage == ClipboardTransfer::Stage::ReceiptPlaced)
                    state.receiptPlacements.fetch_add(1);
                if (stage == ClipboardTransfer::Stage::PngPlaced)
                    state.pngPlacements.fetch_add(1);
                if (stage == ClipboardTransfer::Stage::DibPlaced)
                    state.dibPlacements.fetch_add(1);
                if (stage == ClipboardTransfer::Stage::Closed)
                    state.publications.fetch_add(1);
                if (stage == ClipboardTransfer::Stage::DibInspectRead ||
                    stage == ClipboardTransfer::Stage::DibV5InspectRead)
                    state.workerDibRead.store(true);
                if (stage == ClipboardTransfer::Stage::Inspected)
                    state.workerDibRead.store(false);
                if (state.publicationStage != stage || state.publicationVisits.fetch_add(1) != 0)
                    return;
                const HANDLE shutdown = state.workerShutdownEvent.load();
                if (!shutdown ||
                    !PostThreadMessageW(state.publicationOwnerThread, ClipboardWorkTestGate::kPublicationMessage, 0, 0))
                {
                    state.publicationGateFailed.store(true);
                    return;
                }
                const HANDLE waits[]{state.release.get(), shutdown};
                if (WaitForMultipleObjects(2, waits, FALSE, 10000) != WAIT_OBJECT_0)
                    state.publicationGateFailed.store(true);
            },
            [](void* raw, ClipboardTransfer::Fault fault) noexcept -> std::uint32_t
            {
                auto& state = *static_cast<ClipboardWorkTestGate*>(raw);
                if (fault == ClipboardTransfer::Fault::Open)
                {
                    state.openAttempts.fetch_add(1);
                    auto remaining = state.openFailures.load();
                    while (remaining && !state.openFailures.compare_exchange_weak(remaining, remaining - 1))
                    {
                    }
                    if (remaining)
                    {
                        state.forcedBusyAttempts.fetch_add(1);
                        return ERROR_BUSY;
                    }
                }
                return state.fault == fault ? state.nativeError : 0;
            },
            [](void*, ClipboardTransfer::NativeCall call, std::uint32_t format, std::uint32_t error) noexcept
            {
                std::printf("DIAG clipboard-host.native call=%u format=%lu error=%lu thread=%lu sequence=%lu\n",
                            static_cast<unsigned>(call), static_cast<unsigned long>(format),
                            static_cast<unsigned long>(error), GetCurrentThreadId(), GetClipboardSequenceNumber());
            }};
}
HRESULT ClipboardStatusError(ClipboardTransfer::Status status) noexcept
{
    switch (status)
    {
    case ClipboardTransfer::Status::Ok:
        return S_OK;
    case ClipboardTransfer::Status::Busy:
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    case ClipboardTransfer::Status::Cancelled:
    case ClipboardTransfer::Status::Stale:
        return E_ABORT;
    case ClipboardTransfer::Status::TooLarge:
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    case ClipboardTransfer::Status::OutOfMemory:
        return E_OUTOFMEMORY;
    default:
        return E_FAIL;
    }
}
bool AddClipboardBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (!bytes || bytes > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += bytes;
    return true;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct ClipboardCopyPayload final : DeferredReleaseNode
{
    // Final transfer destination remains empty until the original parent actually terminates/joins.
    std::shared_ptr<SharedPublicationMemory> memory;
    // During useful detached stages, this opaque proxy is backed by the original still-reserved job.
    std::shared_ptr<const void> outputLifetime;
    std::shared_ptr<ClipboardWorkTestGate> gate;
    DWORD ownerThread = 0;
    std::optional<ClipboardCommandInput> input;
    std::optional<PreparedClipboardCommand> publication;
    std::optional<PixelClipboard> clipboard;
    ClipboardTransfer::PreparedImage transfer;
    ClipboardTransfer::Publication offered;
    std::optional<ClipboardFingerprint::Binding> binding;
    ClipboardReceipt::Key key;
    std::uint32_t sourceSequence = 0;
    double resolution = 0;
    CanvasDocument readbackDocument;
    PixelBuffer readbackPixels;
    Raster readbackImage;
    Raster completedImage; // Terminal output cannot populate the original ownership-transfer destination.
    std::optional<JobToken> mergedAuthority;
    ClipboardCopyPayload(std::uint64_t retirement) noexcept
    {
        bytes = (std::max)(retirement, kDeferredReleaseThreshold);
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ClipboardCopyPayload> owned(static_cast<ClipboardCopyPayload*>(node)); };
    }
    ~ClipboardCopyPayload()
    {
        if (gate)
        {
            gate->retiredNodes.fetch_add(1);
            if (GetCurrentThreadId() == ownerThread)
                gate->retiredOwnerNodes.fetch_add(1);
        }
    }
};
#pragma warning(pop)

struct RetireClipboardCopy final
{
    void operator()(ClipboardCopyPayload* payload) const noexcept
    {
        if (!payload)
            return;
        if (payload->outputLifetime.use_count() != 0)
        {
            if (!TryDeferRelease(*payload, DeferredReleasePolicy::AlwaysOnOwner))
                std::terminate();
        }
        else if (!TryDeferRelease(*payload))
        {
            const std::unique_ptr<ClipboardCopyPayload> owned(payload);
        }
    }
};
} // namespace

class Application::ClipboardCopyJob final : public Job, public RetryWaitReceiver
{
  public:
    ClipboardCopyJob(Application& owner, HistoryWorkStamp stamp, ClipboardCommandKind kind, bool mirrorOnly,
                     std::uint64_t retirement, void* resumeContext = nullptr,
                     bool (*resumeDetached)(void*, JobTicket) noexcept = nullptr) noexcept
        : _owner(owner), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()), _kind(kind),
          _mirrorOnly(mirrorOnly), _retirement(retirement), _gate(owner._clipboardWorkTestGate),
          _ownerThread(GetCurrentThreadId()), _resumeContext(resumeContext), _resumeDetached(resumeDetached)
    {
    }
    JobResult PrepareMergedOpening(const std::shared_ptr<const void>& lifetime)
    {
        if (_kind != ClipboardCommandKind::CopyMerged || !lifetime.use_count() || _payload)
            return {JobStatus::Failed, E_INVALIDARG};
        _payload.reset(std::make_unique<ClipboardCopyPayload>(_retirement).release());
        _payload->outputLifetime = lifetime;
        _payload->gate = _gate;
        _payload->ownerThread = _ownerThread;
        return {};
    }

    // The enclosing Copy Merged readback owns the same admitted record, original snapshot and reservation.
    HRESULT CaptureMergedDocument(CanvasDocument& document) noexcept
    {
        if (!Current() || !_payload || _owner._historyWork.CancelRequested(_stamp))
            return _error = E_ABORT;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->mergedAuthority = _owner._session.CaptureJobToken();
            if (!_payload->mergedAuthority)
                return _error = E_ABORT;
            _payload->clipboard = _owner._session.Clipboard();
            _payload->resolution = _owner._session.Document()->resolution;
            document = _owner._session.Document().value();
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return _error = E_OUTOFMEMORY; // admitted capture failed without private or OS publication
        }
    }
    HRESULT CaptureMergedPixels(const Raster& image) noexcept
    {
        if (!Current() || _owner._historyWork.CancelRequested(_stamp) || !_payload || !_payload->mergedAuthority ||
            !_owner._session.CanApplyJobResult(_payload->mergedAuthority.value(), _payload->mergedAuthority->sequence))
            return _error = E_ABORT;
        try
        {
            auto input = _owner._session.PrepareClipboardCommandInput(
                ClipboardCommandKind::CopyMerged, image, _payload->mergedAuthority, _payload->outputLifetime);
            if (!input)
                return _error = E_ABORT;
            _payload->input.emplace(std::move(input.value()));
            _phase = Phase::PreparePrivate;
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return _error = E_OUTOFMEMORY;
        }
    }
    ClipboardCopyPayload* ReadbackReleaseAggregate() noexcept
    {
        return _payload.get();
    }
    JobResult PrepareMergedSnapshot(JobContext& context)
    {
        return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::MergedSnapshot, context);
    }

    JobResult Run(JobContext& context) override
    {
        _cancellation = context.cancellation;
        if (_gate)
            _gate->workerShutdownEvent.store(context.shutdownEvent);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (FAILED(_error))
            return {JobStatus::Failed, _error};
        switch (_phase)
        {
        case Phase::Capture:
            return {}; // owner capture only after the whole reservation has been acquired
        case Phase::PreparePrivate:
            if (const auto result = PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyCaptured, context);
                result.status != JobStatus::Finished)
                return result;
            // This sequence belongs to the copied source, before encoding and every retry. Never sample it in
            // owner completion after a worker's temporary clipboard owner window has been destroyed.
            _payload->sourceSequence = GetClipboardSequenceNumber();
            if (_payload->input)
            {
                auto publication = DocumentSession::PrepareClipboardCommandPublication(
                    std::move(_payload->input.value()), context.cancellation);
                if (publication)
                    _payload->publication.emplace(std::move(publication.value()));
                _payload->input.reset(); // CPU owners retire here, not in owner capture/application
            }
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyPrepared, context);
        case Phase::Encode:
            if (const auto result =
                    PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyPrivateApplied, context);
                result.status != JobStatus::Finished)
                return result;
            if (!_payload->clipboard)
                return {}; // a no-paint Cut without a prior private copy has no mirror
            _transferStatus = ClipboardTransfer::Prepare(_payload->clipboard->image, _payload->resolution,
                                                         _payload->sourceSequence, _payload->transfer,
                                                         context.cancellation, ClipboardObserver(_gate), _payload->key);
            if (_transferStatus == ClipboardTransfer::Status::Ok)
                _payload->binding = _payload->transfer.ContentBinding();
            if (_transferStatus == ClipboardTransfer::Status::Cancelled)
                return {JobStatus::Cancelled};
            return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyEncoded, context);
        case Phase::Publish:
            ++_attempts;
            _payload->offered =
                ClipboardTransfer::Publish(_payload->transfer, context.cancellation, ClipboardObserver(_gate));
            if (_payload->offered.status == ClipboardTransfer::Status::Cancelled)
                return {JobStatus::Cancelled};
            // CopyPublished also gates fault tests after a failed attempt. A successful-publication gate
            // must not release on a Busy attempt, before the original ticket's bounded retry has published.
            if (_payload->offered.status == ClipboardTransfer::Status::Ok && _gate &&
                _gate->phase == ClipboardWorkPhase::CopyPublicationSucceeded)
                return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyPublicationSucceeded, context);
            return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::CopyPublished, context);
        case Phase::Done:
            return {};
        }
        return {JobStatus::Failed, E_UNEXPECTED};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            switch (_phase)
            {
            case Phase::Capture:
                if (_gate)
                {
                    _gate->captureReservationHeld =
                        _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                    if (_gate->throwCaptureFailure)
                        throw std::bad_alloc{};
                }
                _payload.reset(new ClipboardCopyPayload(_retirement));
                _payload->gate = _gate;
                _payload->ownerThread = _ownerThread;
                _payload->clipboard = _owner._session.Clipboard();
                _payload->resolution = _owner._session.Document()->resolution;
                if (!_mirrorOnly)
                {
                    auto input = _owner._session.PrepareClipboardCommandInput(_kind);
                    if (!input)
                    {
                        _error = E_ABORT;
                        return false;
                    }
                    _payload->input.emplace(std::move(input.value()));
                }
                _phase = Phase::PreparePrivate;
                return Resume(ticket);
            case Phase::PreparePrivate:
                if (_payload->publication)
                {
                    if (!_owner._session.ApplyPreparedClipboardCommandIfCurrent(_payload->publication.value()))
                    {
                        _error = E_ABORT;
                        return false;
                    }
                    _payload->clipboard = _payload->publication->Clipboard();
                    _privateApplied = true;
                    if (_payload->publication->CutsPixels())
                        _owner.DocumentChanged();
                    else
                    {
                        _owner.SyncChrome();
                        _owner.InvalidateFrame();
                    }
                }
                else if (_kind != ClipboardCommandKind::Cut)
                {
                    _error = E_INVALIDARG;
                    return false;
                }
                // Cut's legacy no-paint path mirrors the previous private copy without replacing it. OS
                // failures/cancellation after private application cannot undo that completed Copy/Cut.
                _privateOperationFinished = true;
                _authority = _owner._session.CaptureImageImportAuthority();
                _payload->key = {_owner._session.ClipboardSessionIdentity().bytes, _owner._session.ClipboardSerial()};
                _owner._clipboardSequence = _payload->sourceSequence;
                _owner._clipboardWriteSucceeded = false;
                if (_privateApplied)
                    _owner._clipboardBinding.reset();
                if (!_payload->clipboard)
                    return false;
                _phase = Phase::Encode;
                return Resume(ticket);
            case Phase::Encode:
                if (_transferStatus != ClipboardTransfer::Status::Ok || !_payload->binding)
                    return false;
                _owner._clipboardBinding = _payload->binding;
                _phase = Phase::Publish;
                return Resume(ticket);
            case Phase::Publish:
                if (_payload->offered.status != ClipboardTransfer::Status::Busy || _payload->offered.emptied ||
                    _attempts >= 6)
                    return false;
                _error = static_cast<HRESULT>(_owner._retryWait.Start(ticket, _attempts, _cancellation, *this));
                return SUCCEEDED(_error);
            case Phase::Done:
                return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            _error = E_OUTOFMEMORY; // explicit noexcept admission/capture failure; no partial core application
        }
        return false;
    }

    void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        if (status != RetryWaitStatus::Ready)
        {
            _error = status == RetryWaitStatus::Cancelled ? E_ABORT : static_cast<HRESULT>(error);
            _phase = Phase::Done;
        }
        (void)Resume(ticket); // the original reservation and suspended record survive acknowledgement
    }

    void Completed(JobResult result) noexcept override
    {
        // Record only truth captured inside the held clipboard scope. Late window teardown can change its
        // sequence, and origin preference then requires the owned fingerprint/receipt match.
        if (_privateOperationFinished && _payload &&
            _payload->key.session == _owner._session.ClipboardSessionIdentity().bytes &&
            _payload->key.serial == _owner._session.ClipboardSerial())
        {
            _owner._clipboardWriteSucceeded = _payload->offered.status == ClipboardTransfer::Status::Ok;
            if (_payload->offered.emptied)
                _owner._clipboardSequence = _payload->offered.sequence;
        }
        const bool current = Current();
        const auto outcome = _privateOperationFinished ? HostOutcome::Succeeded
                             : !current                ? HostOutcome::Stale
                             : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                 ? HostOutcome::Cancelled
                             : _error == E_ABORT || result.error == E_ABORT         ? HostOutcome::Stale
                             : result.status == JobStatus::Failed || FAILED(_error) ? HostOutcome::Failed
                                                                                    : HostOutcome::Succeeded;
        const auto error = outcome == HostOutcome::Succeeded                                    ? S_OK
                           : outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled ? E_ABORT
                           : FAILED(_error) ? _error
                                            : static_cast<HRESULT>(result.error);
        _owner._clipboardTicket.reset();
        _owner._clipboardStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    enum class Phase : std::uint8_t
    {
        Capture,
        PreparePrivate,
        Encode,
        Publish,
        Done
    };
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._clipboardStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.CanApplyImageImport(_authority);
    }
    bool Resume(JobTicket ticket) noexcept
    {
        if (_resumeDetached ? _resumeDetached(_resumeContext, ticket) : _owner._jobs.Resume(ticket))
            return true;
        _error = E_UNEXPECTED;
        return false;
    }
    Application& _owner; // touched only in owner callbacks; Run uses its detached payload
    const HistoryWorkStamp _stamp;
    ImageImportAuthority _authority;
    const ClipboardCommandKind _kind;
    const bool _mirrorOnly;
    const std::uint64_t _retirement;
    const std::shared_ptr<ClipboardWorkTestGate> _gate;
    const DWORD _ownerThread;
    void* const _resumeContext;
    bool (*const _resumeDetached)(void*, JobTicket) noexcept;
    std::unique_ptr<ClipboardCopyPayload, RetireClipboardCopy> _payload;
    CancelToken _cancellation;
    ClipboardTransfer::Status _transferStatus = ClipboardTransfer::Status::Invalid;
    Phase _phase = Phase::Capture;
    HRESULT _error = S_OK;
    unsigned int _attempts = 0;
    bool _privateApplied = false, _privateOperationFinished = false;
};

class Application::ClipboardMergedJob final : public RecordedDocumentReadbackJob
{
  public:
    ClipboardMergedJob(Application& owner, HistoryWorkStamp stamp, D3D11_BOX region,
                       UtilityReadbackAdmission::Envelope envelope, GpuResidencyReservation reservation) noexcept
        : RecordedDocumentReadbackJob(owner._jobs, owner._gpuWork, owner._recordedCommands, owner._utilityReadback,
                                      owner._graphGpuBudget, envelope, std::move(reservation), region,
                                      owner._renderer.IsWarp()),
          _owner(owner), _copy(owner, stamp, ClipboardCommandKind::CopyMerged, false, envelope.cpuBytes, this,
                               [](void* opaque, JobTicket ticket) noexcept
                               { return static_cast<ClipboardMergedJob*>(opaque)->ResumeDetachedReadback(ticket); })
    {
    }
    ~ClipboardMergedJob() override
    {
        if (auto* payload = _copy.ReadbackReleaseAggregate())
            TransferReadbackOwners(payload->readbackDocument, payload->readbackPixels, payload->readbackImage,
                                   payload->memory);
    }

  private:
    JobResult PrepareReadbackOpening(JobContext&) override
    {
        return _copy.PrepareMergedOpening(ReadbackOutputLifetime());
    }
    HRESULT CaptureReadbackDocument(CanvasDocument& document) noexcept override
    {
        return _copy.CaptureMergedDocument(document);
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext& context) override
    {
        return _copy.PrepareMergedSnapshot(context);
    }
    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    HRESULT CaptureDetachedReadback(Raster& image) noexcept override
    {
        if (auto* payload = _copy.ReadbackReleaseAggregate(); payload && payload->gate)
        {
            ++payload->gate->detachedCaptures;
            payload->gate->utilityReleasedBeforeEncoding = _owner._gpuWork.LentCount() == 0 &&
                                                           _owner._composition.UtilitySurfaceBytes() == 0 &&
                                                           ReadbackGraphicsRetiredOwner();
        }
        return _copy.CaptureMergedPixels(image);
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        auto result = _copy.Run(context);
        image = {}; // detached input/publication retains needed pixels; last references retire here
        return result;
    }
    bool DeferDetachedCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        return _copy.DeferCompletion(result, ticket);
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        if (image.HasStorage())
        {
            if (auto* payload = _copy.ReadbackReleaseAggregate())
                std::swap(image, payload->completedImage);
        }
        _copy.Completed(result); // acknowledge private/OS truth and the original selected ordinal once
    }
    Application& _owner;    // device callback only
    ClipboardCopyJob _copy; // same scheduler record, reservation and acknowledged retry receiver
};

HRESULT Application::StartClipboardMerged() noexcept
{
    if (!_activeHostRequest || _clipboardTicket)
        return E_UNEXPECTED;
    if (!_session.CanCopyMerged() || _session.HasColorEdit() || _session.PendingGradient() ||
        _session.PendingTransformEdit())
        return S_FALSE;
    const auto& document = _session.Document().value();
    const auto region = _session.SelectionCopyRegion();
    if (!region)
        return S_FALSE;
    const D3D11_BOX full{0, 0, 0, static_cast<UINT>(document.width), static_cast<UINT>(document.height), 1};
    auto memory = sizeof(ClipboardMergedJob) + sizeof(ClipboardCopyPayload) + sizeof(ClipboardCommandInput) +
                  sizeof(PreparedClipboardCommand) + SharedPublicationMemory::OwnershipAllowance() + 64 * 1024ull;
    auto held = _session.HistoryRestoreRetirementMemoryBytes();
    const auto copy = CopyMergedPixelsMemoryBytes(document.width, document.height, document.selection);
    const auto encoding = ClipboardEncodingBudget(region->width, region->height);
    const auto readback = RecordedDocumentReadbackJob::RequiredMemory(full);
    const auto metadata = _session.history.EditMemoryBytes(_session.Document());
    // Close preserves a previous private copy, potentially larger than this document. Capture/publication
    // retain it independently of document/history; its complete cached bound must precede that capture.
    if (const auto& previous = _session.Clipboard(); previous)
    {
        const auto previousBytes = previous->image.RetirementMemoryBytes();
        if (!previousBytes || !AddClipboardBytes(memory, previousBytes) ||
            !AddClipboardBytes(memory, sizeof(PixelClipboard)))
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }
    if (!held || !copy || !encoding || !readback || !metadata || !AddClipboardBytes(memory, copy) ||
        !AddClipboardBytes(memory, encoding) || !AddClipboardBytes(memory, readback) ||
        !AddClipboardBytes(memory, metadata) ||
        (_session.Clipboard() && !AddClipboardBytes(held, _session.Clipboard()->image.RetirementMemoryBytes())) ||
        memory > ((std::numeric_limits<std::uint64_t>::max)() - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    held += memory * 2;
    UtilityReadbackAdmission::Envelope envelope;
    GpuResidencyReservation reservation;
    RETURN_IF_FAILED(PrepareRecordedUtilityAdmission(document, held, envelope, reservation));
    const auto ordinal = _activeHostRequest->ordinal;
    _clipboardStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_clipboardStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _clipboardStamp =
            _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_clipboardStamp)
        return E_UNEXPECTED;
    HRESULT error = E_OUTOFMEMORY;
    try
    {
        if (_clipboardWorkTestGate)
        {
            _clipboardWorkTestGate->reservationBytes = envelope.cpuBytes;
            if (_clipboardWorkTestGate->throwAdmissionFailure)
                throw std::bad_alloc{};
        }
        std::unique_ptr<Job> job =
            std::make_unique<ClipboardMergedJob>(*this, *_clipboardStamp, full, envelope, std::move(reservation));
        JobTicket ticket;
        error = static_cast<HRESULT>(_jobs.Submit(job,
                                                  {.kind = JobClass::Commit,
                                                   .serialKey = _session.DocumentSerial(),
                                                   .memoryBytes = envelope.cpuBytes,
                                                   .deferredRetirement = true,
                                                   .allowMemoryWaiting = true,
                                                   .reserveRetainedMemory = true},
                                                  ticket));
        if (SUCCEEDED(error))
            _clipboardTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor-only refusal; CPU/GPU/private/OS allocation and publication have not begun.
    }
    if (FAILED(error))
    {
        (void)_historyWork.Finish(*_clipboardStamp);
        _hostFinishedStamp = _clipboardStamp;
        _clipboardStamp.reset();
        return error;
    }
    _hostCommitStamp = _clipboardStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

class Application::ClipboardPrefillJob final : public Job, public RetryWaitReceiver
{
  public:
    explicit ClipboardPrefillJob(Application& owner) noexcept
        : _owner(owner), _invocation(owner._sizeInvocation), _documentSerial(owner._session.DocumentSerial()),
          _session(owner._session.ClipboardSessionIdentity()), _gate(owner._clipboardWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        _cancellation = context.cancellation;
        if (_gate)
            _gate->workerShutdownEvent.store(context.shutdownEvent);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (FAILED(_error))
            return {JobStatus::Failed, _error};
        ++_attempts;
        _status =
            ClipboardTransfer::Inspect(kMaxLayerPixels, _inspection, context.cancellation, ClipboardObserver(_gate));
        if (_status == ClipboardTransfer::Status::Cancelled)
            return {JobStatus::Cancelled};
        // A retry has no settled metadata yet. Keep the test boundary at the successful inspection or final refusal.
        if (_status == ClipboardTransfer::Status::Busy && _attempts < 6)
            return {};
        return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PrefillInspected, context);
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _status != ClipboardTransfer::Status::Busy ||
            _attempts >= 6)
            return false;
        _error = static_cast<HRESULT>(_owner._retryWait.Start(ticket, _attempts, _cancellation, *this));
        return SUCCEEDED(_error);
    }
    void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        if (status != RetryWaitStatus::Ready)
            _error = status == RetryWaitStatus::Cancelled ? E_ABORT : static_cast<HRESULT>(error);
        if (!_owner._jobs.Resume(ticket))
            _error = E_UNEXPECTED;
    }
    void Completed(JobResult result) noexcept override
    {
        const bool publish = Current() && result.status == JobStatus::Finished && SUCCEEDED(_error) &&
                             _status == ClipboardTransfer::Status::Ok && _inspection;
        if (_owner._clipboardPrefillInvocation == _invocation)
            _owner._clipboardPrefillTicket.reset();
        if (publish)
        {
            const auto& info = _inspection.Png().pixels ? _inspection.Png() : _inspection.Dib();
            _owner._sizeDialog.newWidth = info.width;
            _owner._sizeDialog.newHeight = info.height;
            _owner.SyncSizeCard();
            _owner.InvalidateFrame();
        }
        // Optional suggestion failure is local. It neither overwrites the draft nor publishes a notice/history.
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._clipboardPrefillTicket &&
               _owner._clipboardPrefillInvocation == _invocation && _owner._sizeInvocation == _invocation &&
               _owner._sizeDialog.kind == SizeCardKind::NewCanvas && !_owner._newCanvasEdited &&
               _owner._session.DocumentSerial() == _documentSerial &&
               _owner._session.ClipboardSessionIdentity() == _session;
    }
    Application& _owner; // fixed metadata on the worker; owner validation/application only
    const std::uint64_t _invocation, _documentSerial;
    const Guid _session;
    const std::shared_ptr<ClipboardWorkTestGate> _gate;
    const DWORD _ownerThread;
    ClipboardTransfer::Inspection _inspection;
    CancelToken _cancellation;
    ClipboardTransfer::Status _status = ClipboardTransfer::Status::Invalid;
    HRESULT _error = S_OK;
    unsigned int _attempts = 0;
};

void Application::CancelClipboardPrefill() noexcept
{
    if (_clipboardPrefillTicket)
        (void)_jobs.Cancel(_clipboardPrefillTicket.value());
}

void Application::StartClipboardPrefill() noexcept
{
    constexpr std::uint64_t metadataBytes = 2ull * 1024 * 1024 + sizeof(ClipboardPrefillJob);
    try
    {
        std::unique_ptr<Job> job = std::make_unique<ClipboardPrefillJob>(*this);
        JobTicket ticket;
        if (SUCCEEDED(static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Io, 0, metadataBytes}, ticket))))
        {
            _clipboardPrefillInvocation = _sizeInvocation;
            _clipboardPrefillTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        // Optional metadata admission failed as a whole; the immediately opened draft keeps its dimensions.
    }
}

HRESULT Application::StartClipboardCopy(bool cut) noexcept
{
    if (!_activeHostRequest || _clipboardTicket)
        return E_UNEXPECTED;
    if (!_session.Document() || !_session.CanCopyPixels() || (cut && !_session.Selection()))
        return S_FALSE;
    const auto kind = cut ? ClipboardCommandKind::Cut : ClipboardCommandKind::Copy;
    const auto core = _session.ClipboardCommandMemoryBytes(kind);
    const auto held = _session.ClipboardCommandRetirementMemoryBytes(kind);
    const bool mirrorOnly = cut && !_session.CanPaint();
    std::uint64_t encoding = 0;
    if (const auto region = _session.SelectionCopyRegion())
        encoding = ClipboardEncodingBudget(region->width, region->height);
    if (cut && _session.Clipboard())
        encoding = (std::max)(encoding, ClipboardEncodingBudget(_session.Clipboard()->image.Width(),
                                                                _session.Clipboard()->image.Height()));
    // The no-paint Cut still captures only its previous clipboard after admission. It never records an edit.
    auto memory = sizeof(ClipboardCopyJob) + sizeof(ClipboardCopyPayload) + 4096ull;
    auto retirement = mirrorOnly && _session.Clipboard() ? _session.Clipboard()->image.RetirementMemoryBytes() : held;
    if ((!mirrorOnly && (!core || !AddClipboardBytes(memory, core))) ||
        (encoding && !AddClipboardBytes(memory, encoding)) || (!mirrorOnly && (!encoding || !retirement)) ||
        memory > ((std::numeric_limits<std::uint64_t>::max)() - retirement) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    retirement += memory * 2;
    const auto ordinal = _activeHostRequest->ordinal;
    _clipboardStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_clipboardStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _clipboardStamp =
            _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_clipboardStamp)
        return E_UNEXPECTED;
    HRESULT error = E_OUTOFMEMORY;
    try
    {
        if (_clipboardWorkTestGate)
        {
            _clipboardWorkTestGate->reservationBytes = memory;
            if (_clipboardWorkTestGate->throwAdmissionFailure)
                throw std::bad_alloc{};
        }
        std::unique_ptr<Job> job =
            std::make_unique<ClipboardCopyJob>(*this, *_clipboardStamp, kind, mirrorOnly, retirement);
        JobTicket ticket;
        error = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
        if (SUCCEEDED(error))
            _clipboardTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Complete admission refusal: no captured document/pixels, private application or OS mutation exists.
    }
    if (FAILED(error))
    {
        (void)_historyWork.Finish(*_clipboardStamp);
        _hostFinishedStamp = _clipboardStamp;
        _clipboardStamp.reset();
        return error;
    }
    _hostCommitStamp = _clipboardStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ClipboardPastePayload final : DeferredReleaseNode
{
    std::shared_ptr<ClipboardWorkTestGate> gate;
    DWORD ownerThread = 0;
    std::optional<ClipboardPasteInput> input;
    std::optional<PreparedClipboardPaste> publication;
    ClipboardTransfer::ImageCopy imageBytes;
    Raster decoded;
    explicit ClipboardPastePayload(std::uint64_t retirement) noexcept
    {
        bytes = (std::max)(retirement, kDeferredReleaseThreshold);
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ClipboardPastePayload> owned(static_cast<ClipboardPastePayload*>(node)); };
    }
    ~ClipboardPastePayload()
    {
        if (gate)
        {
            gate->retiredNodes.fetch_add(1);
            if (GetCurrentThreadId() == ownerThread)
                gate->retiredOwnerNodes.fetch_add(1);
        }
    }
};
#pragma warning(pop)
struct RetireClipboardPaste final
{
    void operator()(ClipboardPastePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ClipboardPastePayload> owned(payload);
        }
    }
};
} // namespace

class Application::ClipboardPasteJob final : public Job, public RetryWaitReceiver
{
  public:
    ClipboardPasteJob(Application& owner, HistoryWorkStamp stamp, ImageImportAuthority authority,
                      ClipboardTransfer::Inspection inspection, bool privatePreference, ClipboardReceipt::Key key,
                      std::optional<ClipboardFingerprint::Binding> binding, unsigned int attempts,
                      std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _authority(std::move(authority)), _inspection(std::move(inspection)), _key(key),
          _binding(std::move(binding)), _captureSerial(owner._session.ClipboardSerial()), _retirement(retirement),
          _gate(owner._clipboardWorkTestGate), _ownerThread(GetCurrentThreadId()), _privateOrigin(privatePreference),
          _attempts(attempts)
    {
    }

    JobResult Run(JobContext& context) override
    {
        _cancellation = context.cancellation;
        if (_gate)
            _gate->workerShutdownEvent.store(context.shutdownEvent);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (FAILED(_error))
            return {JobStatus::Failed, _error};
        if (!_payload)
            return {}; // admitted owner capture precedes encoded/decoded/history allocations
        if (const auto result = PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteCaptured, context);
            result.status != JobStatus::Finished)
            return result;
        if (!_privateOrigin)
        {
            ++_attempts;
            _status = ClipboardTransfer::Copy(_inspection, _payload->imageBytes, context.cancellation,
                                              ClipboardObserver(_gate));
            if (_status == ClipboardTransfer::Status::Busy)
                return {}; // suspend the same input/ticket/reservation for the remaining acquisition attempts
            if (_status != ClipboardTransfer::Status::Ok)
                return _status == ClipboardTransfer::Status::Cancelled
                           ? JobResult{JobStatus::Cancelled}
                           : JobResult{JobStatus::Failed, ClipboardStatusError(_status)};
            if (_binding)
            {
                _status = ClipboardTransfer::HasPrivateOrigin(_payload->imageBytes, _binding.value(), _key,
                                                              _privateOrigin, context.cancellation);
                if (_status != ClipboardTransfer::Status::Ok)
                    return _status == ClipboardTransfer::Status::Cancelled
                               ? JobResult{JobStatus::Cancelled}
                               : JobResult{JobStatus::Failed, ClipboardStatusError(_status)};
            }
            if (!_privateOrigin)
            {
                _status = ClipboardTransfer::Decode(_payload->imageBytes, _payload->decoded, context.cancellation);
                if (_status != ClipboardTransfer::Status::Ok)
                    return _status == ClipboardTransfer::Status::Cancelled
                               ? JobResult{JobStatus::Cancelled}
                               : JobResult{JobStatus::Failed, ClipboardStatusError(_status)};
            }
        }
        if (const auto result = PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteCopied, context);
            result.status != JobStatus::Finished)
            return result;
        auto publication = DocumentSession::PrepareClipboardPastePublication(
            std::move(_payload->input.value()), _privateOrigin, _payload->decoded, context.cancellation);
        _payload->input.reset(); // worker-only retirement of input graph not retained by the publication
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!publication)
            return {JobStatus::Failed, E_INVALIDARG};
        _payload->publication.emplace(std::move(publication.value()));
        _prepared = true;
        return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PastePrepared, context);
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_payload)
        {
            if (_prepared || _status != ClipboardTransfer::Status::Busy || _attempts >= 6)
                return false;
            _error = static_cast<HRESULT>(_owner._retryWait.Start(ticket, _attempts, _cancellation, *this));
            return SUCCEEDED(_error);
        }
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld =
                    _gate->reservationBytes != 0 && _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            // Seal clipboard dimensions/retirement owners against the metadata used for this admission.
            if (_owner._session.ClipboardSerial() != _captureSerial ||
                (_privateOrigin &&
                 (_key.serial != _captureSerial || _key.session != _owner._session.ClipboardSessionIdentity().bytes)))
            {
                _error = E_ABORT;
                return false;
            }
            _payload.reset(new ClipboardPastePayload(_retirement));
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            auto input = _owner._session.PrepareClipboardPasteInput();
            if (!input)
            {
                _error = E_ABORT;
                return false;
            }
            _payload->input.emplace(std::move(input.value()));
            if (_owner._jobs.Resume(ticket))
                return true;
            _error = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _error = E_OUTOFMEMORY;
        }
        return false;
    }

    void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        if (status != RetryWaitStatus::Ready)
            _error = status == RetryWaitStatus::Cancelled ? E_ABORT : static_cast<HRESULT>(error);
        if (!_owner._jobs.Resume(ticket))
            _error = E_UNEXPECTED;
    }

    void Completed(JobResult result) noexcept override
    {
        const bool current = Current();
        HostOutcome outcome = !current ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : _error == E_ABORT || _status == ClipboardTransfer::Status::Stale ? HostOutcome::Stale
                              : result.status == JobStatus::Failed || FAILED(_error)             ? HostOutcome::Failed
                              : _status == ClipboardTransfer::Status::Busy ? HostOutcome::Failed
                                                                           : HostOutcome::Succeeded;
        HRESULT error = FAILED(_error) ? _error : static_cast<HRESULT>(result.error);
        if (outcome == HostOutcome::Failed && _status == ClipboardTransfer::Status::Busy && !FAILED(error))
            error = HRESULT_FROM_WIN32(ERROR_BUSY);
        if (outcome == HostOutcome::Succeeded)
        {
            if (!_payload || !_payload->publication ||
                !_owner._session.ApplyPreparedClipboardPasteIfCurrent(_payload->publication.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
            else
                _owner.DocumentChanged();
        }
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled)
            error = E_ABORT;
        _owner._clipboardTicket.reset();
        _owner._clipboardStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._clipboardStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.CanApplyImageImport(_authority) &&
               (!_privateOrigin || _owner._session.ClipboardSerial() == _key.serial);
    }
    Application& _owner; // owner callbacks only; worker consumes an admitted immutable snapshot/owned bytes
    const HistoryWorkStamp _stamp;
    const ImageImportAuthority _authority;
    ClipboardTransfer::Inspection _inspection;
    const ClipboardReceipt::Key _key;
    const std::optional<ClipboardFingerprint::Binding> _binding;
    const std::uint64_t _captureSerial, _retirement;
    const std::shared_ptr<ClipboardWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<ClipboardPastePayload, RetireClipboardPaste> _payload;
    CancelToken _cancellation;
    HRESULT _error = S_OK;
    ClipboardTransfer::Status _status = ClipboardTransfer::Status::Invalid;
    bool _privateOrigin = false, _prepared = false;
    unsigned int _attempts = 0;
};

class Application::ClipboardPasteProbeJob final : public Job, public RetryWaitReceiver
{
  public:
    ClipboardPasteProbeJob(Application& owner, HistoryWorkStamp stamp, std::int64_t pixelBudget) noexcept
        : _owner(owner), _stamp(stamp), _authority(owner._session.CaptureImageImportAuthority()),
          _key{owner._session.ClipboardSessionIdentity().bytes, owner._session.ClipboardSerial()},
          _binding(owner._clipboardBinding), _preferredSequence(owner._clipboardSequence), _pixelBudget(pixelBudget),
          _hasPrivate(owner._session.Clipboard().has_value()), _gate(owner._clipboardWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        _cancellation = context.cancellation;
        if (_gate)
            _gate->workerShutdownEvent.store(context.shutdownEvent);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (FAILED(_error))
            return {JobStatus::Failed, _error};
        if (const auto result = PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteProbe, context);
            result.status != JobStatus::Finished)
            return result;
        // Preserve the current private preference on a pre-Empty mirror failure. This worker samples the
        // source before any transfer, never a later owner completion sequence.
        if (_gate && _gate->phase == ClipboardWorkPhase::PasteProbe)
            TraceClipboardOwnerForTest("before-inspect");
        const auto sampledSequence = _gate && _gate->forceUnavailableProbeSequence ? 0u : GetClipboardSequenceNumber();
        if (_gate && _gate->phase == ClipboardWorkPhase::PasteProbe)
            std::printf(
                "DIAG clipboard-probe before attempt=%u private=%d preferred=%lu sampled=%lu forced=%u opens=%u\n",
                _attempts, _hasPrivate, static_cast<unsigned long>(_preferredSequence),
                static_cast<unsigned long>(sampledSequence), _gate->forcedBusyAttempts.load(),
                _gate->openAttempts.load());
        // Zero is an unavailable sequence query, and also our "inspect the OS" sentinel. It cannot
        // prove that private pixels match the OS clipboard. Continue through ordinary Inspect/retry.
        if (_hasPrivate && _preferredSequence != 0 && sampledSequence == _preferredSequence)
        {
            _privatePreference = true;
            _status = ClipboardTransfer::Status::Ok;
            return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteInspected, context);
        }
        ++_attempts;
        _status = ClipboardTransfer::Inspect(_pixelBudget, _inspection, context.cancellation, ClipboardObserver(_gate),
                                             &_availability);
        if (_gate && _gate->phase == ClipboardWorkPhase::PasteProbe)
            TraceClipboardOwnerForTest("after-inspect");
        if (_status == ClipboardTransfer::Status::Cancelled)
            return {JobStatus::Cancelled};
        if (_gate && _gate->phase == ClipboardWorkPhase::PasteProbe)
            std::printf("DIAG clipboard-probe inspected attempt=%u status=%u observed=%d image=%d sequence=%lu "
                        "forced=%u opens=%u\n",
                        _attempts, static_cast<unsigned>(_status), _availability.observed,
                        _availability.hasImageFormats, static_cast<unsigned long>(_availability.sequence),
                        _gate->forcedBusyAttempts.load(), _gate->openAttempts.load());
        if (_availability.observed && !_availability.hasImageFormats)
        {
            _privatePreference = _hasPrivate; // legacy no-image fall-through; malformed images do not fall through
            _empty = !_hasPrivate;
            return PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteInspected, context);
        }
        if (_status == ClipboardTransfer::Status::Busy)
            return {}; // no owned inspection exists yet; retry before the inspected boundary
        return _status == ClipboardTransfer::Status::Ok
                   ? PassClipboardGate(_gate, _ownerThread, ClipboardWorkPhase::PasteInspected, context)
                   : JobResult{JobStatus::Failed, ClipboardStatusError(_status)};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _status != ClipboardTransfer::Status::Busy || _attempts >= 6 ||
            !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        _error = static_cast<HRESULT>(_owner._retryWait.Start(ticket, _attempts, _cancellation, *this));
        return SUCCEEDED(_error);
    }
    void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        if (status != RetryWaitStatus::Ready)
            _error = status == RetryWaitStatus::Cancelled ? E_ABORT : static_cast<HRESULT>(error);
        if (!_owner._jobs.Resume(ticket))
            _error = E_UNEXPECTED;
    }
    void Completed(JobResult result) noexcept override
    {
        const bool current = Current();
        HostOutcome outcome = !current ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_error) ? HostOutcome::Failed
                                                                                     : HostOutcome::Succeeded;
        HRESULT error = FAILED(_error) ? _error : static_cast<HRESULT>(result.error);
        _owner._clipboardTicket.reset();
        if (outcome == HostOutcome::Succeeded && _status == ClipboardTransfer::Status::Busy)
        {
            outcome = HostOutcome::Failed;
            error = HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        if (outcome == HostOutcome::Succeeded && !_empty)
        {
            error = _owner.SubmitClipboardPaste(_stamp, _authority, std::move(_inspection), _privatePreference, _key,
                                                _binding, _attempts);
            if (SUCCEEDED(error))
                return; // same selected ordinal/barrier; the probe reservation retires before the main record runs
            outcome = error == E_ABORT ? HostOutcome::Stale : HostOutcome::Failed;
        }
        if (outcome == HostOutcome::Failed)
        {
            ++_owner._errorsShown;
            _owner._lastErrorId =
                error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE : IDS_ERROR_PIXEL_EDIT;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
        }
        if (outcome == HostOutcome::Stale || outcome == HostOutcome::Cancelled)
            error = E_ABORT;
        _owner._clipboardStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._clipboardStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.CanApplyImageImport(_authority) &&
               (!_privatePreference || _owner._session.ClipboardSerial() == _key.serial);
    }
    Application& _owner; // worker reads only fixed metadata and the private adapter's owned inspection
    const HistoryWorkStamp _stamp;
    const ImageImportAuthority _authority;
    const ClipboardReceipt::Key _key;
    const std::optional<ClipboardFingerprint::Binding> _binding;
    const std::uint32_t _preferredSequence;
    const std::int64_t _pixelBudget;
    const bool _hasPrivate;
    const std::shared_ptr<ClipboardWorkTestGate> _gate;
    const DWORD _ownerThread;
    ClipboardTransfer::Inspection _inspection;
    ClipboardTransfer::Availability _availability;
    CancelToken _cancellation;
    ClipboardTransfer::Status _status = ClipboardTransfer::Status::Invalid;
    HRESULT _error = S_OK;
    unsigned int _attempts = 0;
    bool _privatePreference = false, _empty = false;
};

HRESULT Application::SubmitClipboardPaste(HistoryWorkStamp stamp, ImageImportAuthority authority,
                                          ClipboardTransfer::Inspection inspection, bool privatePreference,
                                          ClipboardReceipt::Key key,
                                          std::optional<ClipboardFingerprint::Binding> binding,
                                          unsigned int attempts) noexcept
{
    if (!_activeHostRequest || _clipboardTicket || !_historyWork.IsCurrent(stamp) ||
        !_session.CanApplyImageImport(authority))
        return E_ABORT;
    if (!privatePreference && attempts >= 6)
        return HRESULT_FROM_WIN32(ERROR_BUSY); // six total inspection/copy acquisition attempts
    std::uint64_t pixels = (std::max)(inspection.Png().pixels, inspection.Dib().pixels);
    if (privatePreference)
    {
        if (!_session.Clipboard() || key.serial != _session.ClipboardSerial() ||
            key.session != _session.ClipboardSessionIdentity().bytes)
            return E_ABORT;
        pixels = _session.Clipboard()->image.PixelCount();
    }
    else if (key.serial != _session.ClipboardSerial() || key.session != _session.ClipboardSessionIdentity().bytes)
    {
        // A newer direct private copy makes the old receipt foreign. Continue its owned byte decode instead
        // of binding that old receipt to the new private pixels/origin or holding an unadmitted old raster.
        binding.reset();
        key = {_session.ClipboardSessionIdentity().bytes, _session.ClipboardSerial()};
    }
    if (!pixels || pixels > kMaxLayerPixels)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    auto memory = _session.ClipboardPastePreparationMemoryBytes(static_cast<std::int64_t>(pixels));
    const auto held = _session.ClipboardPasteRetirementMemoryBytes();
    if (!memory || !held ||
        !AddClipboardBytes(memory, sizeof(ClipboardPasteJob) + sizeof(ClipboardPastePayload) + 16ull * 1024 * 1024) ||
        (!privatePreference && (!inspection || !AddClipboardBytes(memory, inspection.CopyAndPixelsBytes()))) ||
        memory > ((std::numeric_limits<std::uint64_t>::max)() - held) / 2)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    try
    {
        if (_clipboardWorkTestGate)
        {
            _clipboardWorkTestGate->reservationBytes = memory;
            if (_clipboardWorkTestGate->throwMainAdmissionFailure)
                throw std::bad_alloc{};
        }
        std::unique_ptr<Job> job = std::make_unique<ClipboardPasteJob>(*this, stamp, std::move(authority),
                                                                       std::move(inspection), privatePreference, key,
                                                                       std::move(binding), attempts, held + memory * 2);
        JobTicket ticket;
        const auto error =
            static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
        if (SUCCEEDED(error))
            _clipboardTicket = ticket;
        return error;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // complete main-graph admission refusal; no private clipboard/document copy occurred
    }
}

HRESULT Application::StartClipboardPaste() noexcept
{
    if (!_activeHostRequest || _clipboardTicket)
        return E_UNEXPECTED;
    if (!_session.Document())
        return S_FALSE;
    const auto pixels = _session.ClipboardPastePixelLimit();
    if (pixels <= 0)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    const auto ordinal = _activeHostRequest->ordinal;
    _clipboardStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_clipboardStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _clipboardStamp =
            _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_clipboardStamp)
        return E_UNEXPECTED;
    HRESULT error = E_OUTOFMEMORY;
    try
    {
        if (_clipboardWorkTestGate)
        {
            _clipboardWorkTestGate->probeReservationBytes = 2ull * 1024 * 1024 + sizeof(ClipboardPasteProbeJob);
            if (_clipboardWorkTestGate->throwAdmissionFailure)
                throw std::bad_alloc{};
        }
        std::unique_ptr<Job> job = std::make_unique<ClipboardPasteProbeJob>(*this, *_clipboardStamp, pixels);
        JobTicket ticket;
        error = static_cast<HRESULT>(_jobs.Submit(
            job, {JobClass::Commit, _session.DocumentSerial(), 2ull * 1024 * 1024 + sizeof(ClipboardPasteProbeJob)},
            ticket));
        if (SUCCEEDED(error))
            _clipboardTicket = ticket;
    }
    catch (const std::bad_alloc&)
    {
        // Cheap fixed-metadata admission failed. No borrowed OS pointer or private pixel owner was captured.
    }
    if (FAILED(error))
    {
        (void)_historyWork.Finish(*_clipboardStamp);
        _hostFinishedStamp = _clipboardStamp;
        _clipboardStamp.reset();
        return error;
    }
    _hostCommitStamp = _clipboardStamp;
    _hostAfterPrerequisite = HostStage::Complete;
    return S_OK;
}

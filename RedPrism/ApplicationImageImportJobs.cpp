#include "Application.h"

#include "ChromeHost.h"
#include "ImageImportHistory.h"
#include "ImageImportTestGate.h"
#include "ImageImportWork.h"
#include "ProjectOpenTestGate.h"
#include "Resources.h"
#include "resource.h"

#include "../Tests/Support/DocumentFixture.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ImageImportPayload final : DeferredReleaseNode
{
    std::vector<std::wstring> paths;
    std::optional<std::vector<ImageImportFilePlan>> files;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    ImageImportProbeResult probed;
    std::optional<PreparedImageImport> publication;
    std::wstring retiredPath;
    std::shared_ptr<ImageImportTestGate> gate;
    DWORD ownerThread = 0;
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    bool failed = false, tooLarge = false;
    ImageImportPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ImageImportPayload> owned(static_cast<ImageImportPayload*>(node)); };
    }
    ~ImageImportPayload()
    {
        if (gate && files)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireImageImportPayload final
{
    void operator()(ImageImportPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ImageImportPayload> owned(payload);
        }
    }
};
} // namespace

class Application::ImageImportJob final : public Job
{
  public:
    ImageImportJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t generation,
                   std::vector<std::wstring> paths, std::optional<std::vector<ImageImportFilePlan>> files,
                   std::optional<Guid> active, bool newProject, std::optional<Vec2> center, int maximumAddedPixels,
                   std::uint64_t retirement)
        : _owner(owner), _stamp(stamp), _generation(generation), _active(active), _newProject(newProject),
          _center(center), _maximumAddedPixels(maximumAddedPixels), _gate(owner._imageImportTestGate),
          _ownerThread(GetCurrentThreadId())
    {
        _authority = owner._session.CaptureImageImportAuthority();
        _payload.reset(std::make_unique<ImageImportPayload>().release());
        // Install the whole bound before capture can fail after a partial owner copy.
        _payload->bytes = (std::max)(retirement, kDeferredReleaseThreshold);
        _payload->paths = std::move(paths);
        _payload->files = std::move(files);
        _payload->gate = _gate;
        _payload->ownerThread = _ownerThread;
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_refreshResidency)
        {
            if (_gate && _gate->throwResidencyRefresh)
                throw std::bad_alloc{};
            const bool refreshed =
                _payload->publication && _payload->publication->RefreshHistoryResidency(context.cancellation);
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                   : refreshed                        ? JobResult{}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
        if (_payload->files && !_snapshotReady)
            return {}; // admitted record reaches its owner capture hook before any document copy
        const auto phase = _payload->files ? ImageImportTestPhase::Prepare : ImageImportTestPhase::Probe;
        if (_gate && _gate->phase == phase && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->phase == phase && !_gate->afterPreparation && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        std::array<std::wstring_view, kImageImportMaxPaths> views{};
        for (std::size_t index = 0; index < _payload->paths.size(); ++index)
            views[index] = _payload->paths[index];
        const auto paths = std::span<const std::wstring_view>(views.data(), _payload->paths.size());
        if (_payload->files)
        {
            if (_gate)
                _gate->preparations.fetch_add(1);
            ImageImportBatchInput input{_payload->source, _active, paths, _newProject, _center};
            input.files = _payload->files.value();
            input.maximumAddedPixels = _maximumAddedPixels;
            struct Progress final
            {
                JobContext& context;
                std::size_t count;
            } progress{context, paths.size()};
            input.progressContext = &progress;
            input.progress = [](void* value, std::size_t attempted, ProjectIoStatus) noexcept
            {
                auto& progressValue = *static_cast<Progress*>(value);
                progressValue.context.ReportProgress(static_cast<unsigned int>(attempted * 100 / progressValue.count));
            };
            auto prepared = RunImageImportBatch(input, context.cancellation);
            _payload->status = prepared.status;
            _payload->failed = prepared.failed;
            _payload->tooLarge = prepared.tooLarge;
            if (context.cancellation.IsCancelled() || prepared.status == ProjectIoStatus::Cancelled)
                return {JobStatus::Cancelled};
            DocumentHistory::PrepareObserver observer;
            if (_gate && _gate->throwHistoryStage)
            {
                observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                            {
                                auto& gate = *static_cast<ImageImportTestGate*>(opaque);
                                if (gate.throwHistoryStage == stage)
                                {
                                    gate.historyFaultHits.fetch_add(1);
                                    throw std::bad_alloc{};
                                }
                            },
                            _gate.get()};
            }
            _payload->publication = DocumentSession::PrepareImageImportPublication(
                _authority, std::move(_payload->source), _active, std::move(_payload->history), std::move(prepared),
                context.cancellation, observer);
            if (!_payload->publication)
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, E_UNEXPECTED};
            if (_gate && _gate->lateResidencySource.HasStorage())
            {
                if (!_gate->lateResidencySource.Data())
                    return {JobStatus::Failed, E_FAIL};
                _gate->residentGrowths.fetch_add(1);
            }
        }
        else
            _payload->probed = ProbeImageImportBatch(paths, context.cancellation);
        if (_gate && _gate->phase == phase && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->phase == phase && _gate->afterPreparation && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        return context.cancellation.IsCancelled() ||
                       (_payload->files ? _payload->status : _payload->probed.status) == ProjectIoStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !_payload->files || !Current() ||
            _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_snapshotReady)
        {
            if (!_payload->publication)
                return false;
            const auto decision = _payload->publication->TryAcquireHistoryResidency(_residencyPermit);
            if (decision == DocumentHistory::ResidencyValidation::Ready)
                return false;
            if (decision == DocumentHistory::ResidencyValidation::Invalid)
            {
                _snapshotError = E_UNEXPECTED;
                return false;
            }
            _refreshResidency = true;
            if (_gate)
            {
                _gate->residencyRefreshes.fetch_add(1);
                _gate->refreshTicketMatches.store(_gate->capturedTicket == ticket);
                _gate->refreshReservationHeld.store(_gate->preparationReservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >=
                                                        _gate->preparationReservationBytes);
            }
            if (_owner._jobs.Resume(ticket))
            {
                if (_gate && _gate->cancelResidencyRefresh)
                    (void)_owner._jobs.Cancel(ticket);
                return true;
            }
            _snapshotError = E_UNEXPECTED;
            return false;
        }
        try
        {
            if (_gate)
            {
                _gate->captures.fetch_add(1);
                _gate->capturedTicket = ticket;
                _gate->captureReservationHeld =
                    _owner._jobs.ReservedMemoryBytes() != 0 &&
                    _owner._jobs.ReservedMemoryBytes() == _gate->preparationReservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            // The bound includes old session/history owners even when these images create a replacement canvas.
            _payload->source = _owner._session.Document();
            _payload->history = _owner._session.history.CaptureState();
            _snapshotReady = true;
            if (_owner._jobs.Resume(ticket))
                return true;
            _snapshotError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _snapshotError = E_OUTOFMEMORY; // explicit noexcept capture/admission boundary fails the whole operation
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        const auto releasePermit = wil::scope_exit([this]() noexcept { _residencyPermit.Reset(); });
        const bool current = Current();
        HostOutcome outcome = !current ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_snapshotError) ? HostOutcome::Failed
                                                                                             : HostOutcome::Succeeded;
        std::int32_t error = FAILED(_snapshotError) ? _snapshotError : result.error;
        bool replaced = false;
        _owner._imageImportTicket.reset();
        if (outcome == HostOutcome::Succeeded && !_payload->files)
        {
            error = _payload->probed.status == ProjectIoStatus::Ok
                        ? _owner.SubmitImageImport(_stamp, _generation, std::move(_payload->paths),
                                                   std::move(_payload->probed.files), _newProject, _center)
                        : E_FAIL;
            if (SUCCEEDED(error))
                return; // selected input/barrier survives; Io FIFO waits for probe record retirement
            outcome = HostOutcome::Failed;
        }
        if (outcome == HostOutcome::Succeeded && _payload->files)
        {
            const bool changes = _payload->publication && _payload->publication.value().ChangesDocument();
            replaced = _payload->publication && _payload->publication.value().ReplacesDocument();
            if (!_payload->publication ||
                !_owner._session.ApplyPreparedImageImportIfCurrent(_payload->publication.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
                replaced = false;
            }
            _residencyPermit.Reset(); // No borrowed lease survives chrome, replacement, or another owner pump.
            if (outcome == HostOutcome::Succeeded && changes)
            {
                if (replaced)
                {
                    std::swap(_owner._documentPath, _payload->retiredPath);
                    _owner.FinishPreparedDocumentReplacement();
                    _owner._activeHostRequest->documentSerial = _owner._session.DocumentSerial();
                }
                _owner._viewport.followsFit = true;
                _owner.DocumentChanged();
            }
            if (outcome == HostOutcome::Succeeded && (_payload->failed || _payload->status != ProjectIoStatus::Ok))
            {
                outcome = HostOutcome::Failed;
                error = _payload->tooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) : E_FAIL;
            }
        }
        _residencyPermit.Reset();
        if (outcome == HostOutcome::Failed)
        {
            if (!FAILED(error))
                error = E_FAIL;
            _owner._hostBatchFailed = true;
            if (!FAILED(_owner._hostBatchError))
                _owner._hostBatchError = error;
            ++_owner._errorsShown;
            _owner._lastErrorId = _payload->tooLarge ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId);
            outcome = HostOutcome::Succeeded; // FileComplete reports the first batch error; no later images remain
            error = S_OK;
        }
        if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
            error = E_ABORT;
        _owner._imageImportStamp.reset();
        if (replaced)
            (void)_owner._historyWork.FinishReplacement(_stamp, _owner._session.DocumentSerial());
        else
            (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._imageImportStamp == _stamp && _owner._hostCommitStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._activeHostRequest &&
               _owner._activeHostRequest->ordinal == _stamp.ordinal &&
               _owner._activeHostRequest->documentSerial == _stamp.documentSerial &&
               _owner._session.DocumentSerial() == _stamp.documentSerial &&
               _owner._session.Generation() == _generation && _owner._session.CanApplyImageImport(_authority);
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        if (!ResetEvent(_gate->release.get()))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, ImageImportTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // worker never dereferences Application, session, HWND or GPU
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation;
    ImageImportAuthority _authority;
    std::unique_ptr<ImageImportPayload, RetireImageImportPayload> _payload;
    const std::optional<Guid> _active;
    const bool _newProject;
    const std::optional<Vec2> _center;
    const int _maximumAddedPixels;
    const std::shared_ptr<ImageImportTestGate> _gate;
    const DWORD _ownerThread;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _snapshotReady = false, _refreshResidency = false;
    HRESULT _snapshotError = S_OK;
};

HRESULT Application::SubmitImageImport(HistoryWorkStamp stamp, std::uint64_t generation,
                                       std::vector<std::wstring> paths,
                                       std::optional<std::vector<ImageImportFilePlan>> files, bool newProject,
                                       std::optional<Vec2> center) noexcept
{
    try
    {
        std::array<std::wstring_view, kImageImportMaxPaths> views{};
        if (paths.empty() || paths.size() > views.size())
            return E_INVALIDARG;
        for (std::size_t index = 0; index < paths.size(); ++index)
            views[index] = paths[index];
        const auto pathViews = std::span<const std::wstring_view>(views.data(), paths.size());
        const bool createsCanvas = newProject || !_session.Document();
        const bool prepares = files.has_value();
        int pixelLimit = 0;
        auto memory = files ? ImageImportMemoryBytes(pathViews, files.value(),
                                                     createsCanvas ? nullptr : &_session.Document().value(), pixelLimit)
                            : ImageImportMetadataMemoryBytes(pathViews);
        if (memory == 0)
            return E_INVALIDARG;
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        auto retirement = kDeferredReleaseThreshold;
        if (prepares)
        {
            const auto history = _session.history.EditMemoryBytes(_session.Document());
            retirement = _session.ImageImportRetirementMemoryBytes();
            if (!history || !retirement || history > maximum / 2 || history * 2 > maximum - memory)
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            memory += history * 2; // Original private history and one same-ticket refresh candidate coexist.
            // Derived previews already in flight remain allowed; later mutating inputs wait behind this ordinal.
            const auto previews = _jobs.ReservedMemoryBytes();
            if (previews > maximum - retirement)
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            retirement += previews;
        }
        if (sizeof(ImageImportJob) + sizeof(ImageImportPayload) > maximum - memory)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        memory += sizeof(ImageImportJob) + sizeof(ImageImportPayload);
        if (memory > maximum / 4 || memory * 4 > maximum - retirement)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        retirement += memory * 4;
        if (_imageImportTestGate && _imageImportTestGate->throwAdmissionFailure &&
            _imageImportTestGate->phase == (files ? ImageImportTestPhase::Prepare : ImageImportTestPhase::Probe))
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<ImageImportJob>(*this, stamp, generation, std::move(paths), std::move(files),
                                             _session.ActiveLayerID(), createsCanvas, center, pixelLimit, retirement);
        JobTicket ticket;
        const auto result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Io, 0, memory}, ticket));
        if (SUCCEEDED(result))
        {
            _imageImportTicket = ticket;
            if (_imageImportTestGate && prepares)
                _imageImportTestGate->preparationReservationBytes = memory;
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // owning graph admission fails before work or partial publication
    }
}

HRESULT Application::StartImageImport(std::span<const std::wstring_view> paths, bool newProject,
                                      std::optional<Vec2> center) noexcept
{
    if (!_activeHostRequest || _imageImportTicket || paths.empty() || paths.size() > kImageImportMaxPaths)
        return E_UNEXPECTED;
    const auto ordinal = _activeHostRequest->ordinal;
    _imageImportStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_imageImportStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _imageImportStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_imageImportStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        std::vector<std::wstring> owned;
        owned.reserve(paths.size());
        const auto resolvedStorage = std::make_unique<std::array<wchar_t, 32768>>();
        auto& resolved = *resolvedStorage;
        for (const auto path : paths)
        {
            const std::wstring terminated(path);
            const DWORD units =
                GetFullPathNameW(terminated.c_str(), static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
            if (units == 0 || units >= resolved.size() || units > kImageImportMaxPathUnits)
            {
                result =
                    units == 0 ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
                break;
            }
            owned.emplace_back(resolved.data(), units); // lexical absolute path owned before the worker is accepted
        }
        if (owned.size() == paths.size())
            result = SubmitImageImport(_imageImportStamp.value(), _session.Generation(), std::move(owned), {},
                                       newProject, center);
    }
    catch (const std::bad_alloc&)
    {
        // Complete selected input admission refusal; no worker or prepared document exists.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_imageImportStamp.value());
        _hostFinishedStamp = _imageImportStamp;
        _imageImportStamp.reset();
        return result;
    }
    _hostCommitStamp = _imageImportStamp;
    _hostAfterPrerequisite = HostStage::FileComplete;
    return S_OK;
}

int Application::RunImageImportSelfTest() noexcept
{
    enum class Target
    {
        Existing,
        Grouped,
        New,
        Empty
    };
    enum class Mode
    {
        Success,
        Ordered,
        Cancel,
        Failure,
        Allocation,
        Admission,
        Stale,
        Generation,
        Device,
        CaptureAllocation,
        HistoryReset,
        HistoryBefore,
        HistoryAfter,
        HistoryEntry,
        HistoryStack,
        HistoryTrim,
        Epoch,
        ActiveOnly,
        MaskOnly,
        CropOnly
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<CanvasDocument> source;
        std::optional<Guid> active;
        Guid revision;
        std::uint64_t serial;
        bool visited = false;
        bool held = false;
        bool cancelPending = false;
        HRESULT deviceRecovered = E_UNEXPECTED;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ImageImportTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._imageImportStamp && owner._imageImportTicket && owner._activeHostRequest &&
                     owner._historyWork.Busy() && state.receipt.Read().outcome == HostOutcome::Pending &&
                     owner._session.Document() == state.source && owner._session.DocumentSerial() == state.serial &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._jobs.ReservedMemoryBytes() != 0;
        if (owner._imageImportTestGate->phase == ImageImportTestPhase::Prepare)
            state.held = state.held &&
                         owner._jobs.ReservedMemoryBytes() == owner._imageImportTestGate->preparationReservationBytes;
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        if (state.mode == Mode::Cancel)
        {
            owner.CancelHostContinuations();
            state.cancelPending = owner._historyWork.Busy() && owner._activeHostRequest &&
                                  state.receipt.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale)
        {
            CanvasDocument replacement = state.source.value();
            replacement.width = 3;
            replacement.height = 2;
            owner.ReplaceDocument(std::move(replacement), state.active, L"import-replacement.prism");
        }
        else if (state.mode == Mode::Generation)
            owner._session.RenameLayer(state.active.value(), L"Import external generation");
        else if (state.mode == Mode::Epoch)
        {
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
        }
        else if (state.mode == Mode::ActiveOnly)
            owner._session.SelectLayer(owner._session.Document().value().layers.front().id);
        else if (state.mode == Mode::MaskOnly)
            owner._session.SelectLayerTarget(state.active.value(), false);
        else if (state.mode == Mode::CropOnly)
            owner._session.SetCropFrame(CropRect{0, 0, 2, 1});
        else if (state.mode == Mode::Device)
        {
            state.deviceRecovered = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
        }
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._imageImportTestGate->release.get());
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 20000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto previousDiscard = _forcedDiscardAnswer;
    const auto previousImageChoice = _forcedImageOpen;
    const auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto leaf = L"RedPrism.ImageImportHost." + Guid::Generate().ToString();
    const auto root = parent / leaf;
    std::error_code fileError;
    if (!root.is_absolute() || root.parent_path() != parent || !std::filesystem::create_directory(root, fileError) ||
        fileError)
        return 5;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_imageImportTestGate)
                (void)SetEvent(_imageImportTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _imageImportTestGate.reset();
            _forcedDiscardAnswer = previousDiscard;
            _forcedImageOpen = previousImageChoice;
            std::error_code error;
            const auto actual = std::filesystem::weakly_canonical(root, error);
            if (!error && actual.is_absolute() && actual == root && actual.parent_path() == parent &&
                actual.filename() == leaf)
                std::filesystem::remove_all(actual, error);
        });
    const auto first = (root / L"import-a.png").wstring();
    const auto second = (root / L"import-b.png").wstring();
    const std::array<std::uint8_t, 8> bytes{191, 191, 191, 255, 23, 45, 67, 255};
    const auto image = Raster::Make(2, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (EncodePngFile(image, 72, first.c_str()) != ImageCodecStatus::Ok ||
        EncodePngFile(image, 72, second.c_str()) != ImageCodecStatus::Ok)
        return 5;
    for (const auto target : {Target::Existing, Target::Grouped, Target::New, Target::Empty})
        for (const auto phase : {ImageImportTestPhase::Probe, ImageImportTestPhase::Prepare})
            for (const bool after : {false, true})
                for (const auto mode :
                     {Mode::Success,      Mode::Ordered,           Mode::Cancel,       Mode::Failure,
                      Mode::Allocation,   Mode::Admission,         Mode::Stale,        Mode::Generation,
                      Mode::Device,       Mode::CaptureAllocation, Mode::HistoryReset, Mode::HistoryBefore,
                      Mode::HistoryAfter, Mode::HistoryEntry,      Mode::HistoryStack, Mode::HistoryTrim,
                      Mode::Epoch,        Mode::ActiveOnly,        Mode::MaskOnly,     Mode::CropOnly})
                {
                    const bool historyFailure = mode >= Mode::HistoryReset && mode <= Mode::HistoryTrim;
                    const bool scopeStale = mode >= Mode::Epoch && mode <= Mode::CropOnly;
                    if (((historyFailure || scopeStale) && ((target != Target::Existing && target != Target::New) ||
                                                            phase != ImageImportTestPhase::Prepare || !after)) ||
                        (mode == Mode::HistoryReset && target != Target::New) ||
                        (target == Target::Empty && (mode == Mode::Stale || mode == Mode::Generation)) ||
                        (mode == Mode::CaptureAllocation &&
                         (target != Target::Existing || phase != ImageImportTestPhase::Prepare || after)) ||
                        (mode == Mode::Device &&
                         (target != Target::Existing || phase != ImageImportTestPhase::Prepare || !after)))
                        continue;
                    if (!settle())
                        return 5;
                    _session.ClearDocument();
                    _session.CreateDocument(8, 4);
                    (void)InsertPaintedLayer(_session, L"Before import", 8, 4, {}, 0, 0xFF404040);
                    if (mode == Mode::ActiveOnly)
                        _session.AddBlankLayer();
                    if (mode == Mode::MaskOnly)
                    {
                        const auto selected = _session.ActiveLayerID().value();
                        EditLayerRecordsForTest(_session,
                                                [&](DocumentFixture& fixture)
                                                {
                                                    for (auto& layer : fixture.layers)
                                                        if (layer.id == selected)
                                                        {
                                                            const std::uint8_t byte = 173;
                                                            const Raster mask =
                                                                Raster::Make(1, 1, RasterFormat::Gray8, &byte, 1);
                                                            LayerMask placed;
                                                            placed.asset =
                                                                RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"};
                                                            layer.mask = std::move(placed);
                                                        }
                                                });
                        _session.SelectLayerTarget(selected, true);
                    }
                    if (historyFailure || scopeStale)
                    {
                        _session.SetOpacity(0.5);
                        _session.Undo(); // Both stacks exist before refusal; the literal source opacity is 1.
                    }
                    if (mode == Mode::CropOnly)
                        _session.SetCropFrame(CropRect{1, 0, 1, 1});
                    if (target == Target::Grouped)
                        EditLayerRecordsForTest(_session,
                                                [](DocumentFixture& fixture)
                                                {
                                                    ImageLayer group = ImageLayer::Blank(L"Import folder", Vec2{8, 4});
                                                    group.isGroup = true;
                                                    fixture.layers.back().parentID = group.id;
                                                    fixture.layers.insert(fixture.layers.begin(), std::move(group));
                                                });
                    _session.history.MarkSaved();
                    _documentPath = L"before-import.prism";
                    if (target == Target::Empty)
                        _session.ClearDocument();
                    DocumentChanged();
                    const auto source = _session.Document();
                    const auto active = _session.ActiveLayerID();
                    const auto revision = _session.history.CurrentRevision();
                    const auto undoCount = _session.history.UndoCount();
                    const auto redoName = _session.history.RedoName();
                    const auto serial = _session.DocumentSerial();
                    const auto errors = _errorsShown;
                    const auto presents = _renderer.PresentCount();
                    _forcedDiscardAnswer = Prompts::Answer::Second;
                    _imageImportTestGate = std::make_shared<ImageImportTestGate>();
                    if (!_imageImportTestGate->release)
                        return 5;
                    _imageImportTestGate->phase = phase;
                    _imageImportTestGate->afterPreparation = after;
                    _imageImportTestGate->throwAllocationFailure = mode == Mode::Allocation;
                    _imageImportTestGate->throwAdmissionFailure = mode == Mode::Admission;
                    _imageImportTestGate->throwCaptureFailure = mode == Mode::CaptureAllocation;
                    if (historyFailure)
                    {
                        constexpr std::array stages{DocumentHistory::PrepareStage::ReplacementReset,
                                                    DocumentHistory::PrepareStage::BeforeSnapshot,
                                                    DocumentHistory::PrepareStage::AfterSnapshot,
                                                    DocumentHistory::PrepareStage::Entry,
                                                    DocumentHistory::PrepareStage::StackGrowth,
                                                    DocumentHistory::PrepareStage::TrimIndex};
                        _imageImportTestGate->throwHistoryStage =
                            stages[static_cast<unsigned int>(mode) - static_cast<unsigned int>(Mode::HistoryReset)];
                    }
                    if (mode == Mode::Failure)
                        _imageImportTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                    std::wstring callerPath = first;
                    const std::array<std::wstring_view, 2> paths{callerPath, second};
                    const bool creates = target == Target::New || target == Target::Empty;
                    const auto receipt = QueueHostPaths(
                        paths, creates ? HostPathAction::ImagesAsProject : HostPathAction::ImagesAsLayers,
                        Vec2{12, 14});
                    callerPath = L"changed-after-import-admission.png";
                    std::optional<ContinuationReceipt> younger;
                    if (mode == Mode::Ordered)
                        younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                    State state{*this, mode, receipt, source, active, revision, serial};
                    const auto settled = SettleJobs(_jobs, 25000, dispatch, &state);
                    const auto actual = receipt.Read();
                    const auto expected =
                        mode == Mode::Stale || mode == Mode::Generation || scopeStale ? HostOutcome::Stale
                        : mode == Mode::Cancel                                        ? HostOutcome::Cancelled
                        : mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::Admission ||
                                mode == Mode::CaptureAllocation || historyFailure
                            ? HostOutcome::Failed
                            : HostOutcome::Succeeded;
                    const bool failed = expected == HostOutcome::Failed;
                    bool correct = settled == SettleResult::Settled && actual.outcome == expected &&
                                   (mode == Mode::Admission || mode == Mode::CaptureAllocation || historyFailure
                                        ? !state.visited
                                        : state.visited && state.held) &&
                                   !_imageImportStamp && !_imageImportTicket && !_activeHostRequest &&
                                   !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 &&
                                   _hostInputBudget.Used() == 0 && _renderer.PresentCount() == presents &&
                                   _errorsShown == errors + static_cast<int>(failed);
                    if (expected == HostOutcome::Succeeded)
                    {
                        if (mode == Mode::Ordered && !creates)
                            correct = correct && _session.Document() == source &&
                                      _session.history.UndoCount() == undoCount && _session.history.CanRedo() &&
                                      _session.history.RedoName() == L"Import Images";
                        else
                        {
                            const auto* imported = _session.ActiveLayer();
                            correct = correct && imported && imported->asset && imported->asset->image.Width() == 2 &&
                                      imported->asset->image.Height() == 1 && imported->asset->image.Data()[0] == 191 &&
                                      imported->asset->image.Data()[4] == 23 &&
                                      _session.Document()->layers.size() == (creates ? 2 : source->layers.size() + 2) &&
                                      _session.history.UndoCount() == (creates ? 1 : undoCount + 1) &&
                                      _session.history.IsModified() &&
                                      // legacy EditorSession.swift:656-657 floors centered placement.
                                      imported->transform.origin == (creates ? Vec2{0, 0} : Vec2{11, 13});
                            if (target == Target::Grouped)
                                correct = correct && imported->parentID == source->layers.front().id;
                            if (creates)
                            {
                                correct = correct && _session.DocumentSerial() != serial && _documentPath.empty() &&
                                          _session.Document()->width == 2 && _session.Document()->height == 1;
                                _session.Undo();
                                correct = correct && _session.Document()->layers.size() == 1 &&
                                          !_session.history.IsModified() &&
                                          _session.ActiveLayer()->asset->image.Data()[0] == 191;
                            }
                            else
                                correct = correct && _session.DocumentSerial() == serial &&
                                          _documentPath == L"before-import.prism";
                        }
                    }
                    else if (mode == Mode::Stale)
                        correct = correct && _session.Document()->id == source->id && _session.Document()->width == 3 &&
                                  _session.Document()->height == 2 && _documentPath == L"import-replacement.prism";
                    else if (mode == Mode::Generation)
                        correct = correct && _session.DocumentSerial() == serial &&
                                  _session.ActiveLayer()->name == L"Import external generation" &&
                                  _session.ActiveLayer()->asset->image.Data()[0] == 64;
                    else
                        correct = correct && _session.Document() == source &&
                                  _session.history.CurrentRevision() == revision &&
                                  _documentPath == L"before-import.prism";
                    if (historyFailure || scopeStale)
                        correct = correct && _session.history.UndoCount() == undoCount &&
                                  _session.history.RedoName() == redoName;
                    if (mode == Mode::ActiveOnly)
                        correct = correct && _session.ActiveLayerID() == source.value().layers.front().id;
                    if (mode == Mode::MaskOnly)
                        correct = correct && !_session.IsMaskSelected();
                    if (mode == Mode::CropOnly)
                        correct = correct && _session.CropFrame() == std::optional(CropRect{0, 0, 2, 1});
                    if (historyFailure)
                        correct = correct && _imageImportTestGate->historyFaultHits.load() == 1 &&
                                  !_session.history.IsModified();
                    if (failed)
                        correct = correct &&
                                  actual.error == static_cast<std::int32_t>(mode == Mode::Failure ? E_ACCESSDENIED
                                                                                                  : E_OUTOFMEMORY) &&
                                  _lastErrorId == IDS_ERROR_IMPORT_IMAGE;
                    if (mode == Mode::Cancel)
                        correct = correct && state.cancelPending;
                    if (mode == Mode::Device)
                        correct = correct && SUCCEEDED(state.deviceRecovered);
                    if (phase == ImageImportTestPhase::Prepare && mode != Mode::Admission)
                        correct = correct && _imageImportTestGate->captureReservationHeld &&
                                  _imageImportTestGate->retired.load() && _imageImportTestGate->retiredOffOwner.load();
                    if (expected == HostOutcome::Cancelled || expected == HostOutcome::Stale)
                        correct = correct && actual.error == static_cast<std::int32_t>(E_ABORT);
                    if (younger)
                        correct = correct && younger.value().Read().outcome ==
                                                 (creates ? HostOutcome::Stale : HostOutcome::Succeeded);
                    std::fprintf(stderr, "%s image-import-host.target=%u phase=%u after=%u case=%u\n",
                                 correct ? "PASS" : "FAIL", static_cast<unsigned int>(target),
                                 static_cast<unsigned int>(phase), after ? 1U : 0U, static_cast<unsigned int>(mode));
                    if (!correct)
                    {
                        std::fprintf(stderr,
                                     "image-import-host.diagnostic settled=%u outcome=%u expected=%u error=%08x "
                                     "held=%u visits=%u device=%08x capture=%u\n",
                                     static_cast<unsigned int>(settled), static_cast<unsigned int>(actual.outcome),
                                     static_cast<unsigned int>(expected), static_cast<unsigned int>(actual.error),
                                     state.held ? 1U : 0U, _imageImportTestGate->visits.load(),
                                     static_cast<unsigned int>(state.deviceRecovered),
                                     _imageImportTestGate->captureReservationHeld ? 1U : 0U);
                        return 5;
                    }
                }
    // Actual selected input route: nested import retains the outer entry; Images as Project discards live
    // previews/floating transactions only after the complete baseline and additional-image entry exist.
    for (const unsigned int context : {0U, 1U, 2U})
        for (const bool fail : {false, true})
        {
            if (!settle())
                return 5;
            _session.ClearDocument(); // Fixture isolation: failed replacement keeps drafts in the prior case.
            _session.CreateDocument(8, 4);
            (void)InsertPaintedLayer(_session, L"Live import source", 8, 4, {}, 0, 0xFF404040);
            _session.history.MarkSaved();
            _documentPath = L"live-import.prism";
            if (context == 0)
                _session.BeginEdit(L"Outer host import");
            else if (context == 1)
                _session.BeginColorEdit(FilterKind::Exposure, true);
            else
            {
                _session.SelectAll();
                _session.BeginSelectionTransform();
            }
            DocumentChanged();
            if (!settle())
                return 5;
            const auto source = _session.Document();
            const auto active = _session.ActiveLayerID();
            const auto revision = _session.history.CurrentRevision();
            const auto depth = _session.history.CaptureState().Depth();
            const auto undo = _session.history.UndoCount();
            const auto serial = _session.DocumentSerial();
            _forcedDiscardAnswer = Prompts::Answer::Second;
            _imageImportTestGate = std::make_shared<ImageImportTestGate>();
            if (!_imageImportTestGate->release)
                return 5;
            _imageImportTestGate->phase = ImageImportTestPhase::Prepare;
            _imageImportTestGate->afterPreparation = true;
            _imageImportTestGate->throwAllocationFailure = fail;
            const std::array<std::wstring_view, 2> paths{first, second};
            const auto receipt =
                QueueHostPaths(paths, context == 0 ? HostPathAction::ImagesAsLayers : HostPathAction::ImagesAsProject);
            State state{*this, fail ? Mode::Allocation : Mode::Success, receipt, source, active, revision, serial};
            const auto settled = SettleJobs(_jobs, 25000, dispatch, &state);
            bool correct = settled == SettleResult::Settled && state.visited && state.held &&
                           receipt.Read().outcome == (fail ? HostOutcome::Failed : HostOutcome::Succeeded) &&
                           !_imageImportTicket && !_imageImportStamp && !_activeHostRequest && !_historyWork.Busy() &&
                           _jobs.ReservedMemoryBytes() == 0 && _imageImportTestGate->retired.load() &&
                           _imageImportTestGate->retiredOffOwner.load();
            if (fail)
                correct = correct && _session.Document() == source && _session.ActiveLayerID() == active &&
                          _session.DocumentSerial() == serial && _session.history.CurrentRevision() == revision &&
                          _session.history.CaptureState().Depth() == depth && _session.history.UndoCount() == undo &&
                          _documentPath == L"live-import.prism" &&
                          (context == 0   ? depth == 1
                           : context == 1 ? _session.HasColorEdit()
                                          : _session.PendingTransformEdit() &&
                                                _session.PendingTransformEdit()->floating.has_value());
            else if (context == 0)
            {
                correct = correct && _session.history.CurrentRevision() == revision &&
                          _session.history.UndoCount() == undo && _session.history.CaptureState().Depth() == 1 &&
                          _session.DocumentSerial() == serial && _documentPath == L"live-import.prism";
                _session.EndEdit();
                correct = correct && _session.history.UndoCount() == undo + 1 &&
                          _session.history.UndoName() == L"Outer host import";
                _session.Undo();
                correct = correct && _session.Document() == source && _session.ActiveLayerID() == active;
                _session.Redo();
                correct = correct && _session.Document()->layers.size() == source->layers.size() + 2 &&
                          _session.ActiveLayer()->asset->image.Data()[0] == 191;
            }
            else
            {
                correct = correct && !_session.HasColorEdit() && !_session.PendingTransformEdit() &&
                          _session.history.CaptureState().Depth() == 0 && _session.history.UndoCount() == 1 &&
                          _session.DocumentSerial() != serial && _documentPath.empty() &&
                          _session.Document()->layers.size() == 2 &&
                          _session.ActiveLayer()->asset->image.Data()[0] == 191;
                _session.Undo();
                correct = correct && !_session.history.IsModified() && _session.Document()->layers.size() == 1 &&
                          _session.ActiveLayer()->asset->image.Data()[4] == 23;
            }
            std::fprintf(stderr, "%s image-import-host.live-context=%u fail=%u\n", correct ? "PASS" : "FAIL", context,
                         fail ? 1U : 0U);
            if (!correct)
                return 5;
        }
    enum class BatchMode
    {
        MissingFirst,
        MissingLast,
        CachedMissing,
        PixelGrowth,
        EncodedGrowth,
        MixedSuccess,
        MixedFailure,
        AllMissing
    };
    struct BatchState final
    {
        Application& owner;
        BatchMode mode;
        ContinuationReceipt receipt;
        std::wstring path;
        Raster grown;
        bool visited = false;
        bool held = false;
        bool rewritten = true;
    };
    const auto batchDispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<BatchState*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ImageImportTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._imageImportStamp && owner._imageImportTicket && owner._historyWork.Busy() &&
                     owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
                     owner._jobs.ReservedMemoryBytes() != 0;
        if (state.mode == BatchMode::CachedMissing || state.mode == BatchMode::PixelGrowth)
            state.rewritten = EncodePngFile(state.grown, 72, state.path.c_str()) == ImageCodecStatus::Ok;
        else if (state.mode == BatchMode::EncodedGrowth)
        {
            std::vector<std::uint8_t> encoded;
            state.rewritten = EncodePng(state.grown, 72, encoded) == ImageCodecStatus::Ok;
            encoded.resize(8192, 0);
            state.rewritten =
                state.rewritten && WriteFileBytesToDisk(state.path.c_str(), encoded.data(), encoded.size());
        }
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._imageImportTestGate->release.get());
    };
    DocumentFixture projectFixture;
    projectFixture.id = Guid::Generate();
    projectFixture.width = 8;
    projectFixture.height = 4;
    auto projectLayer = ImageLayer::Blank(L"Mixed baseline", Vec2{2, 1});
    const auto projectActive = projectLayer.id;
    projectLayer.asset = RasterAsset{image, image, projectLayer.name};
    projectFixture.layers.push_back(std::move(projectLayer));
    const CanvasDocument projectDocument = std::move(projectFixture).Seal();
    const auto projectPath = (root / L"mixed.prism").wstring();
    const auto failedProjectPath = (root / L"missing-project.prism").wstring();
    if (SaveProject(projectPath.c_str(), projectDocument, projectActive, true) != ProjectIoStatus::Ok)
        return 5;
    for (const auto mode :
         {BatchMode::MissingFirst, BatchMode::MissingLast, BatchMode::CachedMissing, BatchMode::PixelGrowth,
          BatchMode::EncodedGrowth, BatchMode::MixedSuccess, BatchMode::MixedFailure, BatchMode::AllMissing})
    {
        if (!settle())
            return 5;
        const auto changing =
            (root / (L"changing-" + std::to_wstring(static_cast<unsigned int>(mode)) + L".png")).wstring();
        const auto missing = (root / L"always-missing.png").wstring();
        if (mode == BatchMode::PixelGrowth || mode == BatchMode::EncodedGrowth)
        {
            std::vector<std::uint8_t> encoded;
            if (EncodePng(image, 72, encoded) != ImageCodecStatus::Ok)
                return 5;
            encoded.resize(4096, 0); // growth of pixel dimensions can remain within this independent encoded cap
            if (!WriteFileBytesToDisk(changing.c_str(), encoded.data(), encoded.size()))
                return 5;
        }
        _session.CreateDocument(8, 4);
        (void)InsertPaintedLayer(_session, L"Before batch", 8, 4, {}, 0, 0xFF404040);
        _session.history.MarkSaved();
        _documentPath = L"before-import-batch.prism";
        const auto source = _session.Document();
        const auto serial = _session.DocumentSerial();
        const auto revision = _session.history.CurrentRevision();
        const auto errors = _errorsShown;
        const auto presents = _renderer.PresentCount();
        _forcedDiscardAnswer = Prompts::Answer::Second;
        _forcedImageOpen = ImageOpenChoice::AddLayer;
        _imageImportTestGate = std::make_shared<ImageImportTestGate>();
        _imageImportTestGate->phase =
            mode == BatchMode::MixedFailure ? ImageImportTestPhase::Prepare : ImageImportTestPhase::Probe;
        _imageImportTestGate->afterPreparation = true;
        if (mode == BatchMode::MixedFailure)
        {
            _imageImportTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
            _projectOpenTestGate = std::make_shared<ProjectOpenTestGate>();
            _projectOpenTestGate->throwAdmissionFailure = true;
            _projectOpenTestGate->admissionFailurePath = failedProjectPath;
        }
        const bool mixed = mode == BatchMode::MixedSuccess || mode == BatchMode::MixedFailure;
        const std::array<std::wstring_view, 3> paths{
            mixed ? (mode == BatchMode::MixedSuccess ? projectPath : failedProjectPath)
            : mode == BatchMode::MissingFirst || mode == BatchMode::AllMissing ? missing
            : mode == BatchMode::CachedMissing || mode == BatchMode::PixelGrowth || mode == BatchMode::EncodedGrowth
                ? changing
                : first,
            mode == BatchMode::MissingLast || mode == BatchMode::AllMissing ? missing : second, first};
        const auto receipt =
            QueueHostPaths(std::span(paths.data(), mixed ? 3 : 2),
                           mixed ? HostPathAction::OpenPaths : HostPathAction::ImagesAsProject, Vec2{100, 200});
        const auto grown = mode == BatchMode::PixelGrowth ? SolidRaster(3, 1, 0xFFBFBFBF) : image;
        BatchState state{*this, mode, receipt, changing, grown};
        const auto settled = SettleJobs(_jobs, 25000, batchDispatch, &state);
        const auto actual = receipt.Read();
        const bool success = mode == BatchMode::MixedSuccess;
        const bool growth = mode == BatchMode::PixelGrowth || mode == BatchMode::EncodedGrowth;
        bool correct = settled == SettleResult::Settled && state.visited && state.held && state.rewritten &&
                       actual.outcome == (success ? HostOutcome::Succeeded : HostOutcome::Failed) &&
                       !_imageImportStamp && !_imageImportTicket && !_activeHostRequest && !_historyWork.Busy() &&
                       _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 &&
                       _renderer.PresentCount() == presents &&
                       _errorsShown == errors + (success                           ? 0
                                                 : mode == BatchMode::MixedFailure ? 2
                                                                                   : 1);
        if (mode == BatchMode::MixedFailure || mode == BatchMode::AllMissing)
            correct = correct && _session.Document() == source && _session.DocumentSerial() == serial &&
                      _session.history.CurrentRevision() == revision && _documentPath == L"before-import-batch.prism";
        else if (success)
            correct = correct && _session.Document()->id == projectDocument.id &&
                      _session.Document()->layers.size() == 3 && _documentPath == projectPath &&
                      _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Import Images" &&
                      _session.ActiveLayer()->transform.origin ==
                          Vec2{3, 1}; // legacy floored center; project batch discards drop point
        else
            correct = correct && _session.DocumentSerial() != serial && _session.Document()->layers.size() == 1 &&
                      _session.Document()->width == 2 && _session.Document()->height == 1 && _documentPath.empty() &&
                      !_session.history.CanUndo() && !_session.history.IsModified() &&
                      _session.ActiveLayer()->asset->image.Data()[0] == 191 &&
                      _session.ActiveLayer()->transform.origin == Vec2{0, 0};
        if (!success)
            correct = correct &&
                      actual.error == static_cast<std::int32_t>(mode == BatchMode::MixedFailure ? E_OUTOFMEMORY
                                                                : growth ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                                         : E_FAIL) &&
                      _lastErrorId == static_cast<UINT>(growth ? IDS_ERROR_TOO_LARGE : IDS_ERROR_IMPORT_IMAGE);
        _projectOpenTestGate.reset();
        std::fprintf(stderr, "%s image-import-host.batch=%u\n", correct ? "PASS" : "FAIL",
                     static_cast<unsigned int>(mode));
        if (!correct)
        {
            std::fprintf(
                stderr,
                "image-import-host.batch-diagnostic settled=%u outcome=%u error=%08x held=%u visited=%u errors=%d\n",
                static_cast<unsigned int>(settled), static_cast<unsigned int>(actual.outcome),
                static_cast<unsigned int>(actual.error), state.held ? 1U : 0U, state.visited ? 1U : 0U,
                _errorsShown - errors);
            return 5;
        }
    }
    const std::array<std::wstring_view, 2> residencyPaths{first, second};
    return RunImageImportResidencySelfTest(residencyPaths);
}

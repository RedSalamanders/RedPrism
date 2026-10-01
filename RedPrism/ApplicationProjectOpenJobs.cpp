#include "Application.h"

#include "ChromeHost.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ImageImportHistory.h"
#include "ProjectLoadWork.h"
#include "ProjectOpenTestGate.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct ProjectOpenPayload final : DeferredReleaseNode
{
    std::wstring path;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    ProjectLoadProbeResult probed;
    ProjectLoadResult loaded;
    std::optional<PreparedImageImport> publication;
    std::shared_ptr<ProjectOpenTestGate> gate;
    DWORD ownerThread = 0;
    bool decode = false;
    ProjectOpenPayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<ProjectOpenPayload> owned(static_cast<ProjectOpenPayload*>(node)); };
    }
    ~ProjectOpenPayload()
    {
        if (gate && decode)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireProjectOpenPayload final
{
    void operator()(ProjectOpenPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<ProjectOpenPayload> owned(payload);
        }
    }
};
} // namespace

class Application::ProjectOpenJob final : public Job
{
  public:
    ProjectOpenJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t generation, std::wstring path,
                   std::optional<ProjectLoadLimits> limits, std::uint64_t retirement)
        : _owner(owner), _stamp(stamp), _generation(generation), _limits(limits), _gate(owner._projectOpenTestGate),
          _ownerThread(GetCurrentThreadId())
    {
        _authority = owner._session.CaptureImageImportAuthority();
        _payload.reset(std::make_unique<ProjectOpenPayload>().release());
        _payload->bytes = (std::max)(retirement, kDeferredReleaseThreshold);
        _payload->path = std::move(path);
        _payload->gate = _gate;
        _payload->ownerThread = _ownerThread;
        _payload->decode = _limits.has_value();
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_limits && !_snapshotReady)
            return {};
        const auto phase = _limits ? ProjectOpenTestPhase::Decode : ProjectOpenTestPhase::Probe;
        if (_gate && _gate->phase == phase && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->phase == phase && !_gate->afterPreparation && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (_limits)
        {
            _payload->loaded = LoadProject(_payload->path.c_str(), context.cancellation, {}, _limits.value());
            _status = _payload->loaded.status;
            _version = _payload->loaded.formatVersion;
            if (_status == ProjectIoStatus::Ok && !context.cancellation.IsCancelled())
            {
                DocumentHistory::PrepareObserver observer;
                if (_gate && _gate->throwHistoryStage)
                    observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                                {
                                    auto& gate = *static_cast<ProjectOpenTestGate*>(opaque);
                                    if (gate.throwHistoryStage == stage)
                                    {
                                        gate.historyFaultHits.fetch_add(1);
                                        throw std::bad_alloc{};
                                    }
                                },
                                _gate.get()};
                _payload->publication = DocumentSession::PrepareDocumentReplacementPublication(
                    _authority, std::move(_payload->source), std::move(_payload->history),
                    std::move(_payload->loaded.document), _payload->loaded.activeLayerID, context.cancellation,
                    observer);
                if (!_payload->publication)
                    return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                              : JobResult{JobStatus::Failed, E_UNEXPECTED};
            }
        }
        else
        {
            _payload->probed = ProbeProjectLoad(_payload->path.c_str(), context.cancellation);
            _status = _payload->probed.status;
            _version = _payload->probed.formatVersion;
        }
        if (_gate && _gate->phase == phase && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->phase == phase && _gate->afterPreparation && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        return context.cancellation.IsCancelled() || _status == ProjectIoStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (!_limits || _snapshotReady || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        try
        {
            if (_gate)
            {
                _gate->captureReservationHeld = _gate->decodeReservationBytes != 0 &&
                                                _owner._jobs.ReservedMemoryBytes() == _gate->decodeReservationBytes;
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->source = _owner._session.Document();
            _payload->history = _owner._session.history.CaptureState();
            _snapshotReady = true;
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
        HostOutcome outcome = !current ? HostOutcome::Stale
                              : result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                                  ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed || FAILED(_captureError) ? HostOutcome::Failed
                                                                                            : HostOutcome::Succeeded;
        std::int32_t error = FAILED(_captureError) ? _captureError : result.error;
        bool replaced = false;
        _owner._projectOpenTicket.reset();
        if (outcome == HostOutcome::Succeeded && _status == ProjectIoStatus::Ok && !_limits)
        {
            // All manifest/header owners died on the probe worker. This small immutable plan becomes the
            // next graph's enforced ceilings. FIFO retirement prevents decode starting before this callback
            // returns and its separate metadata reservation retires.
            error = _owner.SubmitProjectOpen(_stamp, _generation, std::move(_payload->path), _payload->probed.limits);
            if (SUCCEEDED(error))
                return; // retain the selected input/history stamp through decode and owner apply/drop
            outcome = HostOutcome::Failed;
        }
        if (outcome == HostOutcome::Succeeded && _status == ProjectIoStatus::Ok && _limits)
        {
            if (!_payload->publication ||
                !_owner._session.ApplyPreparedImageImportIfCurrent(_payload->publication.value()))
            {
                outcome = HostOutcome::Stale;
                error = E_ABORT;
            }
            else
            {
                std::swap(_owner._documentPath, _payload->path);
                _owner.FinishPreparedDocumentReplacement();
                _owner._viewport.followsFit = true;
                _owner.DocumentChanged();
                // Only this validated replacement may rebind its selected batch. A subsequent external replacement
                // is still stale at the next source dispatch, including reused persisted document/layer IDs.
                _owner._activeHostRequest->documentSerial = _owner._session.DocumentSerial();
                replaced = true;
            }
        }
        else if (outcome == HostOutcome::Succeeded)
        {
            outcome = HostOutcome::Failed;
            error = _status == ProjectIoStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) : E_FAIL;
        }
        if (outcome == HostOutcome::Failed)
        {
            if (!FAILED(error))
                error = _status == ProjectIoStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) : E_FAIL;
            _owner._hostBatchFailed = true;
            if (!FAILED(_owner._hostBatchError))
                _owner._hostBatchError = error;
            ++_owner._errorsShown;
            _owner._lastErrorId = LoadErrorStringId(_status);
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, _owner._lastErrorId, _version);
            // Ordinary project-file failures preserve the document and continue later project paths. The
            // batch's final receipt remains Failed. Cancellation/staleness never authorizes later files.
            outcome = HostOutcome::Succeeded;
            error = S_OK;
        }
        if (outcome == HostOutcome::Cancelled || outcome == HostOutcome::Stale)
            error = E_ABORT;
        _owner._projectOpenStamp.reset();
        if (replaced)
            (void)_owner._historyWork.FinishReplacement(_stamp, _owner._session.DocumentSerial());
        else
            (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._projectOpenStamp == _stamp && _owner._hostCommitStamp == _stamp &&
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
        if (!PostThreadMessageW(_ownerThread, ProjectOpenTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // owner callbacks only; both workers read only owned immutable input
    const HistoryWorkStamp _stamp;
    const std::uint64_t _generation;
    ImageImportAuthority _authority;
    std::unique_ptr<ProjectOpenPayload, RetireProjectOpenPayload> _payload;
    const std::optional<ProjectLoadLimits> _limits;
    const std::shared_ptr<ProjectOpenTestGate> _gate;
    const DWORD _ownerThread;
    bool _snapshotReady = false;
    HRESULT _captureError = S_OK;
    ProjectIoStatus _status = ProjectIoStatus::Invalid;
    int _version = 0;
};

HRESULT Application::SubmitProjectOpen(HistoryWorkStamp stamp, std::uint64_t generation, std::wstring path,
                                       std::optional<ProjectLoadLimits> limits) noexcept
{
    try
    {
        auto memory = limits ? ProjectLoadMemoryBytes(limits.value(), path.size())
                             : ProjectLoadMetadataMemoryBytes(ProjectLoadLimits{}, path.size());
        if (memory == 0)
            return E_INVALIDARG;
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        auto retirement = kDeferredReleaseThreshold;
        if (limits)
        {
            const auto history = _session.history.EditMemoryBytes(_session.Document());
            retirement = _session.ImageImportRetirementMemoryBytes();
            if (!history || !retirement || history > maximum - memory)
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            memory += history;
            // Existing admitted preview graphs may publish derived buffers while IO runs. Mutating inputs are
            // held behind this history ordinal; charge all already admitted graph peaks in the retirement bound.
            const auto previews = _jobs.ReservedMemoryBytes();
            if (previews > maximum - retirement)
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            retirement += previews;
        }
        if (sizeof(ProjectOpenJob) + sizeof(ProjectOpenPayload) > maximum - memory)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        memory += sizeof(ProjectOpenJob) + sizeof(ProjectOpenPayload);
        if (memory > maximum / 4 || memory * 4 > maximum - retirement)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        retirement += memory * 4;
        if (_projectOpenTestGate && _projectOpenTestGate->throwAdmissionFailure &&
            (_projectOpenTestGate->admissionFailurePath.empty() ||
             _projectOpenTestGate->admissionFailurePath == path) &&
            _projectOpenTestGate->phase == (limits ? ProjectOpenTestPhase::Decode : ProjectOpenTestPhase::Probe))
            throw std::bad_alloc{};
        std::unique_ptr<Job> job =
            std::make_unique<ProjectOpenJob>(*this, stamp, generation, std::move(path), limits, retirement);
        JobTicket ticket;
        const auto result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Io, 0, memory}, ticket));
        if (SUCCEEDED(result))
        {
            _projectOpenTicket = ticket;
            if (_projectOpenTestGate && limits)
                _projectOpenTestGate->decodeReservationBytes = memory;
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // complete owning construction/admission refusal; no partial IO graph accepted
    }
}

HRESULT Application::StartProjectOpen(const std::wstring& path) noexcept
{
    if (!_activeHostRequest || _projectOpenTicket ||
        !std::holds_alternative<HostPathsInput>(_activeHostRequest->input.Input()))
        return E_UNEXPECTED;
    std::array<wchar_t, 32768> resolved{};
    const DWORD units = GetFullPathNameW(path.c_str(), static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (units == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    if (units >= resolved.size() || units > HostInputBudget::kMaxPathUnits)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    const auto ordinal = _activeHostRequest->ordinal;
    _projectOpenStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_projectOpenStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _projectOpenStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_projectOpenStamp)
        return E_UNEXPECTED;
    HRESULT result = E_OUTOFMEMORY;
    try
    {
        result = SubmitProjectOpen(_projectOpenStamp.value(), _session.Generation(),
                                   std::wstring(resolved.data(), units), {});
    }
    catch (const std::bad_alloc&)
    {
        // Absolute-path ownership is part of admission; fail this entire graph before any worker starts.
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_projectOpenStamp.value());
        _hostFinishedStamp = _projectOpenStamp;
        _projectOpenStamp.reset();
        return result;
    }
    _hostCommitStamp = _projectOpenStamp;
    _hostAfterPrerequisite = HostStage::FileComplete;
    return S_OK;
}
int Application::RunProjectOpenSelfTest() noexcept
{
    enum class Mode
    {
        Success,
        Ordered,
        Cancel,
        Failure,
        Allocation,
        Stale,
        Generation,
        Admission,
        EmptySession,
        CaptureAllocation,
        HistoryReset,
        HistorySnapshot,
        Epoch,
        ActiveOnly,
        MaskOnly,
        CropOnly,
        LiveColor,
        LiveColorFailure,
        LiveAdjustment,
        LiveAdjustmentFailure,
        LiveFloating,
        LiveFloatingFailure,
        LiveBrush,
        LiveBrushFailure,
        LiveGradient,
        LiveGradientFailure,
        LiveLasso,
        LiveLassoFailure,
        LiveWarp,
        LiveWarpFailure,
        LiveShape,
        LiveShapeFailure,
        LiveOutline,
        LiveOutlineFailure
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::uint64_t serial;
        Guid revision;
        CanvasDocument source;
        std::optional<Guid> active;
        bool visited = false;
        bool held = false;
        bool cancellationPending = false;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ProjectOpenTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held =
            owner._projectOpenStamp && owner._projectOpenTicket && owner._historyWork.Busy() &&
            owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
            owner._jobs.ReservedMemoryBytes() != 0 && owner._session.DocumentSerial() == state.serial &&
            (state.mode == Mode::EmptySession ? !owner._session.Document()
                                              : owner._session.Document() == std::optional(state.source) &&
                                                    owner._session.history.CurrentRevision() == state.revision);
        owner.OnCommand(ChromeHost::kCommandZoomActual); // view input stays available while the worker waits
        if (state.mode == Mode::Cancel)
        {
            owner.CancelHostContinuations();
            state.cancellationPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                        owner._projectOpenTicket && owner._activeHostRequest;
        }
        else if (state.mode == Mode::Stale)
        {
            auto replacement = state.source; // deliberately reuse every persisted document/layer id
            replacement.width = 3;
            replacement.height = 2;
            owner.ReplaceDocument(std::move(replacement), state.active, L"replacement.prism");
        }
        else if (state.mode == Mode::Generation)
            owner._session.RenameLayer(state.active.value(), L"External generation change");
        else if (state.mode == Mode::Epoch)
        {
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
        }
        else if (state.mode == Mode::ActiveOnly)
            owner._session.SelectLayer(state.source.layers.front().id);
        else if (state.mode == Mode::MaskOnly)
            owner._session.SelectLayerTarget(state.active.value(), false);
        else if (state.mode == Mode::CropOnly)
            owner._session.SetCropFrame(CropRect{0, 0, 2, 1});
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        if (owner._projectOpenTestGate->phase == ProjectOpenTestPhase::Decode)
            state.held =
                state.held && owner._jobs.ReservedMemoryBytes() == owner._projectOpenTestGate->decodeReservationBytes;
        (void)SetEvent(owner._projectOpenTestGate->release.get());
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 20000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto priorDiscard = _forcedDiscardAnswer;
    const auto* priorPrompts = _prompts;
    const auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto leaf = L"RedPrism.ProjectOpenHost." + Guid::Generate().ToString();
    const auto root = parent / leaf;
    std::error_code fileError;
    if (!root.is_absolute() || root.parent_path() != parent || !std::filesystem::create_directory(root, fileError) ||
        fileError)
        return 5;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_projectOpenTestGate)
                (void)SetEvent(_projectOpenTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            _projectOpenTestGate.reset();
            _forcedDiscardAnswer = priorDiscard;
            _prompts = priorPrompts;
            std::error_code error;
            const auto actual = std::filesystem::weakly_canonical(root, error);
            if (!error && actual.is_absolute() && actual == root && actual.parent_path() == parent &&
                actual.filename().wstring() == leaf)
                std::filesystem::remove_all(actual, error);
        });
    const std::array<std::uint8_t, 8> fileBytes{191, 191, 191, 255, 23, 45, 67, 255};
    const auto fileImage = Raster::Make(2, 1, RasterFormat::Bgra8888, fileBytes.data(), fileBytes.size());
    CanvasDocument fileDocument;
    fileDocument.id = Guid::Generate();
    fileDocument.width = 2;
    fileDocument.height = 1;
    ImageLayer fileLayer = ImageLayer::Blank(L"Loaded literal", Vec2{2, 1});
    const auto fileId = fileLayer.id;
    fileLayer.asset = RasterAsset{fileImage, fileImage, fileLayer.name};
    fileDocument.layers.push_back(std::move(fileLayer));
    const auto project = (root / L"source.prism").wstring();
    if (SaveProject(project.c_str(), fileDocument, fileId, true) != ProjectIoStatus::Ok)
        return 5;

    for (const auto phase : {ProjectOpenTestPhase::Probe, ProjectOpenTestPhase::Decode})
    {
        for (const bool after : {false, true})
        {
            for (const auto mode : {Mode::Success,        Mode::Ordered,
                                    Mode::Cancel,         Mode::Failure,
                                    Mode::Allocation,     Mode::Stale,
                                    Mode::Generation,     Mode::Admission,
                                    Mode::EmptySession,   Mode::CaptureAllocation,
                                    Mode::HistoryReset,   Mode::HistorySnapshot,
                                    Mode::Epoch,          Mode::ActiveOnly,
                                    Mode::MaskOnly,       Mode::CropOnly,
                                    Mode::LiveColor,      Mode::LiveColorFailure,
                                    Mode::LiveAdjustment, Mode::LiveAdjustmentFailure,
                                    Mode::LiveFloating,   Mode::LiveFloatingFailure,
                                    Mode::LiveBrush,      Mode::LiveBrushFailure,
                                    Mode::LiveGradient,   Mode::LiveGradientFailure,
                                    Mode::LiveLasso,      Mode::LiveLassoFailure,
                                    Mode::LiveWarp,       Mode::LiveWarpFailure,
                                    Mode::LiveShape,      Mode::LiveShapeFailure,
                                    Mode::LiveOutline,    Mode::LiveOutlineFailure})
            {
                const bool historyFailure = mode == Mode::HistoryReset || mode == Mode::HistorySnapshot;
                const bool scopeStale = mode >= Mode::Epoch && mode <= Mode::CropOnly;
                const bool live = mode >= Mode::LiveColor;
                const auto liveKind =
                    (static_cast<unsigned int>(mode) - static_cast<unsigned int>(Mode::LiveColor)) / 2;
                const bool liveFailure =
                    live && (static_cast<unsigned int>(mode) - static_cast<unsigned int>(Mode::LiveColor)) % 2 != 0;
                if (mode >= Mode::CaptureAllocation && (phase != ProjectOpenTestPhase::Decode || !after))
                    continue;
                if (!settle())
                    return 5;
                _session.ClearDocument(); // A preceding refusal deliberately left its live panel/transaction intact.
                _session.CreateDocument(8, 4);
                (void)InsertPaintedLayer(_session, L"Before open", 8, 4, {}, 0, 0xFF404040);
                if (mode == Mode::ActiveOnly)
                    _session.AddBlankLayer();
                if (mode == Mode::MaskOnly)
                {
                    const auto selected = _session.ActiveLayerID().value();
                    _session.EditForTest(
                        [&](CanvasDocument& document)
                        {
                            for (auto& layer : document.layers)
                                if (layer.id == selected)
                                {
                                    const std::uint8_t byte = 173;
                                    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, &byte, 1);
                                    LayerMask placed;
                                    placed.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"};
                                    layer.mask = std::move(placed);
                                }
                        });
                    _session.SelectLayerTarget(selected, true);
                }
                if (historyFailure || scopeStale || mode == Mode::CaptureAllocation)
                {
                    _session.SetOpacity(0.5);
                    _session.Undo();
                }
                if (mode == Mode::CropOnly)
                    _session.SetCropFrame(CropRect{1, 0, 1, 1});
                _session.history.MarkSaved();
                _documentPath = L"before-open.prism";
                const auto liveOpen = [&]() noexcept
                {
                    switch (liveKind)
                    {
                    case 0:
                        return _session.HasColorEdit();
                    case 1:
                        return _session.HasColorEdit() && _session.AdjustmentEditingID().has_value();
                    case 2:
                        return _session.PendingTransformEdit() && _session.PendingTransformEdit()->floating.has_value();
                    case 3:
                        return _session.PendingBrush() != nullptr;
                    case 4:
                        return _session.PendingGradient() != nullptr;
                    case 5:
                        return _session.PendingLasso().has_value();
                    case 6:
                        return _session.PendingWarp() != nullptr;
                    case 7:
                        return _session.PendingShape().has_value();
                    case 8:
                        return _session.IsMovingSelection();
                    default:
                        return false;
                    }
                };
                if (live)
                {
                    switch (liveKind)
                    {
                    case 0:
                        _session.BeginColorEdit(FilterKind::Exposure, true);
                        break;
                    case 1:
                    {
                        const auto image = _session.ActiveLayer()->asset->image;
                        _session.AddAdjustment(AdjustmentKind::Exposure);
                        if (!_session.BeginAdjustmentEditing(_session.ActiveLayerID().value(), image, true))
                            return 5;
                        break;
                    }
                    case 2:
                        _session.SelectAll();
                        _session.BeginSelectionTransform();
                        break;
                    case 3:
                        if (!_session.BeginBrush({2, 2}))
                            return 5;
                        break;
                    case 4:
                        if (!_session.BeginGradient({0, 0}))
                            return 5;
                        _session.MoveGradient(Vec2{0, 0}, Vec2{4, 0});
                        break;
                    case 5:
                        _session.BeginLasso({0, 0}, SelectionMode::New);
                        _session.ExtendLasso({4, 2});
                        break;
                    case 6:
                        if (!_session.BeginWarp({2, 2}))
                            return 5;
                        break;
                    case 7:
                        _session.BeginShape({0, 0});
                        _session.DragShape({4, 2}, false, false);
                        break;
                    case 8:
                        _session.SelectAll();
                        if (!_session.BeginSelectionMove())
                            return 5;
                        break;
                    }
                    if (!liveOpen())
                        return 5;
                }
                const auto source = _session.Document().value();
                const auto active = _session.ActiveLayerID();
                if (mode == Mode::EmptySession)
                    _session.ClearDocument();
                DocumentChanged();
                if (live && !settle())
                    return 5;
                const auto revision = _session.history.CurrentRevision();
                const auto undoCount = _session.history.UndoCount();
                const auto redoName = _session.history.RedoName();
                const auto serial = _session.DocumentSerial();
                const auto errors = _errorsShown;
                const auto presents = _renderer.PresentCount();
                _forcedDiscardAnswer = Prompts::Answer::Second;
                _projectOpenTestGate = std::make_shared<ProjectOpenTestGate>();
                if (!_projectOpenTestGate->release)
                    return 5;
                _projectOpenTestGate->phase = phase;
                _projectOpenTestGate->afterPreparation = after;
                _projectOpenTestGate->throwAllocationFailure = mode == Mode::Allocation;
                _projectOpenTestGate->throwAdmissionFailure = mode == Mode::Admission;
                _projectOpenTestGate->throwCaptureFailure = mode == Mode::CaptureAllocation;
                if (liveFailure)
                    _projectOpenTestGate->throwHistoryStage = DocumentHistory::PrepareStage::ReplacementSnapshot;
                if (historyFailure)
                    _projectOpenTestGate->throwHistoryStage = mode == Mode::HistoryReset
                                                                  ? DocumentHistory::PrepareStage::ReplacementReset
                                                                  : DocumentHistory::PrepareStage::ReplacementSnapshot;
                if (mode == Mode::Failure)
                    _projectOpenTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                std::wstring callerPath = project;
                const std::array<std::wstring_view, 1> paths{callerPath};
                const auto receipt = QueueHostPaths(paths, HostPathAction::OpenPaths);
                callerPath = L"changed-after-admission.prism";
                std::optional<ContinuationReceipt> younger;
                if (mode == Mode::Ordered)
                    younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
                State state{*this, mode, receipt, serial, revision, source, active};
                const auto settled = SettleJobs(_jobs, 25000, dispatch, &state);
                const auto actual = receipt.Read();
                const auto expected = mode == Mode::Stale || mode == Mode::Generation || scopeStale ? HostOutcome::Stale
                                      : mode == Mode::Cancel ? HostOutcome::Cancelled
                                      : mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::Admission ||
                                              mode == Mode::CaptureAllocation || historyFailure || liveFailure
                                          ? HostOutcome::Failed
                                          : HostOutcome::Succeeded;
                bool correct =
                    settled == SettleResult::Settled && actual.outcome == expected &&
                    (mode == Mode::Admission || mode == Mode::CaptureAllocation || historyFailure ? !state.visited
                     : liveFailure ? !state.visited
                                   : state.visited && state.held) &&
                    !_projectOpenStamp && !_projectOpenTicket && !_activeHostRequest && !_historyWork.Busy() &&
                    _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 &&
                    _renderer.PresentCount() == presents;
                if (expected == HostOutcome::Succeeded)
                    correct = correct && _session.Document().value().width == 2 &&
                              _session.Document().value().height == 1 && _session.ActiveLayerID() == fileId &&
                              _documentPath == project && !_session.history.IsModified() &&
                              _session.history.UndoCount() == 0 &&
                              _session.ActiveLayer()->asset.value().image.Data()[0] == 191 &&
                              _session.ActiveLayer()->asset.value().image.Data()[4] == 23;
                else if (mode == Mode::Stale)
                    correct = correct && _session.Document().value().width == 3 &&
                              _session.Document().value().height == 2 && _documentPath == L"replacement.prism" &&
                              _session.Document().value().id == source.id;
                else if (mode == Mode::Generation)
                    correct = correct && _session.DocumentSerial() == serial &&
                              _session.ActiveLayer()->name == L"External generation change" &&
                              _session.ActiveLayer()->asset.value().image.Data()[0] == 64;
                else
                    correct = correct && _session.Document() == std::optional(source) &&
                              _session.history.CurrentRevision() == revision && _documentPath == L"before-open.prism";
                if (historyFailure || scopeStale || mode == Mode::CaptureAllocation)
                    correct = correct && _session.history.UndoCount() == undoCount &&
                              _session.history.RedoName() == redoName && !_session.history.IsModified();
                if (mode == Mode::ActiveOnly)
                    correct = correct && _session.ActiveLayerID() == source.layers.front().id;
                if (mode == Mode::MaskOnly)
                    correct = correct && !_session.IsMaskSelected();
                if (mode == Mode::CropOnly)
                    correct = correct && _session.CropFrame() == std::optional(CropRect{0, 0, 2, 1});
                if (historyFailure)
                    correct = correct && _projectOpenTestGate->historyFaultHits.load() == 1;
                if (live)
                    correct = correct && (liveFailure ? liveOpen() : !liveOpen()) &&
                              (liveFailure ? _session.DocumentSerial() == serial : _session.DocumentSerial() != serial);
                if (liveFailure)
                    correct = correct && _projectOpenTestGate->historyFaultHits.load() == 1 &&
                              _session.history.UndoCount() == undoCount && _session.history.RedoName() == redoName;
                if (phase == ProjectOpenTestPhase::Decode && mode != Mode::Admission)
                    correct = correct && _projectOpenTestGate->captureReservationHeld &&
                              _projectOpenTestGate->retired.load() && _projectOpenTestGate->retiredOffOwner.load();
                if (mode == Mode::Cancel)
                    correct = correct && state.cancellationPending;
                if (expected == HostOutcome::Cancelled || expected == HostOutcome::Stale)
                    correct = correct && actual.error == E_ABORT;
                if (younger)
                    correct = correct && younger.value().Read().outcome == HostOutcome::Stale;
                const bool failed = expected == HostOutcome::Failed;
                correct = correct && _errorsShown == errors + static_cast<int>(failed);
                if (failed)
                    correct = correct && _lastErrorId == IDS_ERROR_OPEN_PROJECT &&
                              actual.error == (mode == Mode::Failure ? E_ACCESSDENIED : E_OUTOFMEMORY);
                std::fprintf(stderr, "%s project-open-host.phase=%u after=%u case=%u\n", correct ? "PASS" : "FAIL",
                             static_cast<unsigned int>(phase), after ? 1U : 0U, static_cast<unsigned int>(mode));
                if (!correct)
                    return 5;
            }
        }
    }
    enum class BatchMode
    {
        Success,
        MissingFirst,
        WorkerFailure,
        DecodeAdmission,
        Unsupported,
        UnsupportedZero,
        Growth,
        CancelSecond,
        ReplaceSecond
    };
    struct BatchState final
    {
        Application& owner;
        BatchMode mode;
        ContinuationReceipt receipt;
        std::wstring firstPath;
        CanvasDocument larger;
        Guid largerActive;
        unsigned int visits = 0;
        bool held = true;
        bool cancelPending = false;
        bool noticeReceived = false;
        int noticeVersion = -1;
        bool formattedNotice = false;
    };
    const auto batchDispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<BatchState*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == kNoticeMessage &&
            (state.mode == BatchMode::Unsupported || state.mode == BatchMode::UnsupportedZero))
        {
            if (message.wParam != IDS_ERROR_UNSUPPORTED_VERSION)
                return; // the later queued-stale notice must not overwrite this file's captured version
            state.noticeReceived = message.wParam == IDS_ERROR_UNSUPPORTED_VERSION;
            state.noticeVersion = static_cast<int>(message.lParam);
            const auto text = FormatPostedNotice(static_cast<UINT>(message.wParam), state.noticeVersion);
            bool hasVersion = false;
            bool hasPlaceholder = false;
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                hasVersion =
                    hasVersion || (state.mode == BatchMode::UnsupportedZero
                                       ? text[index] == L'0'
                                       : index + 1 < text.size() && text[index] == L'9' && text[index + 1] == L'9');
                hasPlaceholder = hasPlaceholder || (index + 2 < text.size() && text[index] == L'{' &&
                                                    text[index + 1] == L'0' && text[index + 2] == L'}');
            }
            state.formattedNotice = hasVersion && !hasPlaceholder;
            return; // exercise the real post and shared formatter without displaying a test notice
        }
        if (message.message != ProjectOpenTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        ++state.visits;
        state.held = state.held && owner._projectOpenTicket && owner._projectOpenStamp && owner._historyWork.Busy() &&
                     owner._activeHostRequest && state.receipt.Read().outcome == HostOutcome::Pending &&
                     owner._jobs.ReservedMemoryBytes() != 0;
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        if (state.mode == BatchMode::DecodeAdmission)
            state.held = state.held && owner._documentPath == L"before-batch.prism" &&
                         owner._session.Document().value().width == 8 &&
                         owner._jobs.ReservedMemoryBytes() == owner._projectOpenTestGate->decodeReservationBytes;
        owner._projectOpenTestGate->result = state.mode == BatchMode::WorkerFailure && state.visits == 1
                                                 ? JobResult{JobStatus::Failed, E_ACCESSDENIED}
                                                 : JobResult{};
        if (state.mode == BatchMode::Growth && state.visits == 1)
            state.held = state.held && SaveProject(state.firstPath.c_str(), state.larger, state.largerActive, true) ==
                                           ProjectIoStatus::Ok;
        if (state.visits == 2 && state.mode == BatchMode::CancelSecond)
        {
            owner.CancelHostContinuations();
            state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._activeHostRequest &&
                                  owner._projectOpenTicket;
        }
        if (state.visits == 2 && state.mode == BatchMode::ReplaceSecond)
        {
            auto replacement = owner._session.Document().value();
            const auto replacementActive = owner._session.ActiveLayerID();
            replacement.width = 4;
            owner.ReplaceDocument(std::move(replacement), replacementActive, L"external.prism");
        }
        (void)SetEvent(owner._projectOpenTestGate->release.get());
    };
    auto secondDocument = fileDocument;
    secondDocument.width = 3;
    const auto secondPath = (root / L"second.prism").wstring();
    if (SaveProject(secondPath.c_str(), secondDocument, fileId, true) != ProjectIoStatus::Ok)
        return 5;
    auto larger = fileDocument;
    larger.width = 5;
    auto& largerLayer = larger.layers.front();
    largerLayer.asset = RasterAsset{SolidRaster(5, 1, 0xFF575757), fileImage, largerLayer.name};
    largerLayer.transform.size = {5, 1};
    const auto firstPath = (root / L"batch-first.prism").wstring();
    const auto missing = (root / L"missing.prism").wstring();
    const auto unsupported = (root / L"unsupported.prism").wstring();
    if (SaveProject(unsupported.c_str(), fileDocument, fileId, false) != ProjectIoStatus::Ok)
        return 5;
    // An independent small manifest keeps a visible unsupported version, including a real directory route.
    const auto manifest = std::filesystem::path(unsupported) / L"manifest.json";
    const auto writeVersion = [&](int version) noexcept
    {
        const auto invalidVersion =
            "{\"format\":\"com.redprism.project\",\"version\":" + std::to_string(version) + "}";
        wil::unique_handle output{
            CreateFileW(manifest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        DWORD written = 0;
        return output &&
               WriteFile(output.get(), invalidVersion.data(), static_cast<DWORD>(invalidVersion.size()), &written,
                         nullptr) &&
               written == invalidVersion.size();
    };
    for (const auto mode : {BatchMode::Success, BatchMode::MissingFirst, BatchMode::WorkerFailure,
                            BatchMode::DecodeAdmission, BatchMode::Unsupported, BatchMode::UnsupportedZero,
                            BatchMode::Growth, BatchMode::CancelSecond, BatchMode::ReplaceSecond})
    {
        if (!settle() || SaveProject(firstPath.c_str(), fileDocument, fileId, true) != ProjectIoStatus::Ok)
            return 5;
        _session.CreateDocument(8, 4);
        (void)InsertPaintedLayer(_session, L"Batch original", 8, 4, {}, 0, 0xFF404040);
        _session.history.MarkSaved();
        _documentPath = L"before-batch.prism";
        _forcedDiscardAnswer = Prompts::Answer::Second;
        const bool versionNotice = mode == BatchMode::Unsupported || mode == BatchMode::UnsupportedZero;
        _prompts = versionNotice ? &RealPrompts() : priorPrompts;
        if (versionNotice && !writeVersion(mode == BatchMode::UnsupportedZero ? 0 : 99))
            return 5;
        DocumentChanged();
        const auto errors = _errorsShown;
        const auto presents = _renderer.PresentCount();
        _projectOpenTestGate = std::make_shared<ProjectOpenTestGate>();
        if (!_projectOpenTestGate->release)
            return 5;
        _projectOpenTestGate->phase = ProjectOpenTestPhase::Probe;
        _projectOpenTestGate->afterPreparation = true;
        if (mode == BatchMode::DecodeAdmission)
        {
            _projectOpenTestGate->phase = ProjectOpenTestPhase::Decode;
            _projectOpenTestGate->throwAdmissionFailure = true;
            _projectOpenTestGate->admissionFailurePath = firstPath;
        }
        const std::array<std::wstring_view, 2> paths{mode == BatchMode::MissingFirst ? std::wstring_view(missing)
                                                     : versionNotice                 ? std::wstring_view(unsupported)
                                                                                     : std::wstring_view(firstPath),
                                                     secondPath};
        const auto receipt = QueueHostPaths(paths, HostPathAction::OpenPaths);
        const auto younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        BatchState state{*this, mode, receipt, firstPath, larger, fileId};
        const auto settled = SettleJobs(_jobs, 25000, batchDispatch, &state);
        const auto messages = PumpMessages(batchDispatch, &state);
        const auto actual = receipt.Read();
        const bool failed = mode == BatchMode::MissingFirst || mode == BatchMode::WorkerFailure ||
                            mode == BatchMode::DecodeAdmission || versionNotice || mode == BatchMode::Growth;
        const auto expected = mode == BatchMode::CancelSecond    ? HostOutcome::Cancelled
                              : mode == BatchMode::ReplaceSecond ? HostOutcome::Stale
                              : failed                           ? HostOutcome::Failed
                                                                 : HostOutcome::Succeeded;
        bool correct =
            settled == SettleResult::Settled && !messages.quit && actual.outcome == expected &&
            state.visits == (mode == BatchMode::DecodeAdmission ? 1U : 2U) && state.held && !_projectOpenStamp &&
            !_projectOpenTicket && !_activeHostRequest && !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 &&
            _hostInputBudget.Used() == 0 && _renderer.PresentCount() == presents &&
            younger.Read().outcome == (mode == BatchMode::CancelSecond ? HostOutcome::Cancelled : HostOutcome::Stale) &&
            _errorsShown == errors + static_cast<int>(failed) && _session.ActiveLayerID() == fileId &&
            !_session.history.IsModified() && _session.history.UndoCount() == 0 &&
            _session.ActiveLayer()->asset.value().image.Data()[0] == 191;
        if (mode == BatchMode::CancelSecond)
            correct =
                correct && state.cancelPending && _documentPath == firstPath && _session.Document().value().width == 2;
        else if (mode == BatchMode::ReplaceSecond)
            correct = correct && _documentPath == L"external.prism" && _session.Document().value().width == 4;
        else
            correct = correct && _documentPath == secondPath && _session.Document().value().width == 3;
        if (versionNotice)
            correct = correct && state.noticeReceived && state.formattedNotice &&
                      state.noticeVersion == (mode == BatchMode::UnsupportedZero ? 0 : 99);
        if (expected == HostOutcome::Cancelled || expected == HostOutcome::Stale)
            correct = correct && actual.error == E_ABORT;
        if (failed)
            correct = correct &&
                      actual.error == (mode == BatchMode::WorkerFailure     ? E_ACCESSDENIED
                                       : mode == BatchMode::DecodeAdmission ? E_OUTOFMEMORY
                                       : mode == BatchMode::Growth          ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                                                            : E_FAIL) &&
                      _lastErrorId == static_cast<UINT>(versionNotice               ? IDS_ERROR_UNSUPPORTED_VERSION
                                                        : mode == BatchMode::Growth ? IDS_ERROR_TOO_LARGE
                                                                                    : IDS_ERROR_OPEN_PROJECT);
        std::fprintf(stderr, "%s project-open-host.batch=%u\n", correct ? "PASS" : "FAIL",
                     static_cast<unsigned int>(mode));
        if (!correct)
        {
            std::fprintf(stderr,
                         "project-open-host.batch-diagnostic settled=%u outcome=%u expected=%u error=%08x "
                         "visits=%u held=%u notice=%u version=%d formatted=%u errors=%d delta=%d\n",
                         static_cast<unsigned int>(settled), static_cast<unsigned int>(actual.outcome),
                         static_cast<unsigned int>(expected), static_cast<unsigned int>(actual.error), state.visits,
                         state.held ? 1U : 0U, state.noticeReceived ? 1U : 0U, state.noticeVersion,
                         state.formattedNotice ? 1U : 0U, _errorsShown, _errorsShown - errors);
            return 5;
        }
    }
    return 0;
}

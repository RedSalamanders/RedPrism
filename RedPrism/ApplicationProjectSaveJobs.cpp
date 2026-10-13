#include "Application.h"

#include "ChromeHost.h"
#include "LayerMetadataTestGate.h"
#include "ProjectSaveTestGate.h"
#include "ProjectSaveWork.h"
#include "ProjectStore.h"
#include "WorkerWriteCompletion.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

class Application::ProjectSaveJob final : public Job
{
  public:
    ProjectSaveJob(Application& owner, HistoryWorkStamp stamp, CanvasDocument document, std::optional<Guid> active,
                   Guid revision, std::wstring path, std::shared_ptr<WorkerWriteCompletion> write)
        : _owner(owner), _stamp(stamp), _document(std::move(document)), _active(active), _revision(revision),
          _path(std::move(path)), _write(std::move(write)), _gate(owner._projectSaveTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        const auto acknowledged = wil::scope_exit([this]() noexcept { _write->Acknowledge(); });
        if (!_write->BeginWorker(WorkerWriteCompletion::Phase::Queued))
            return {JobStatus::Cancelled};
        for (const auto& layer : _document.layers)
            _assets +=
                static_cast<unsigned int>(layer.asset.has_value()) + static_cast<unsigned int>(layer.mask.has_value());
        if (_gate && !_gate->traceOnly && !_gate->afterPublication)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        const auto attributes = GetFileAttributesW(_path.c_str());
        const bool folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool zip = !folder && PathLooksLikeZipProject(_path.c_str());
        struct Progress final
        {
            ProjectSaveJob& job;
            JobContext& context;
            wil::unique_hfile recoveryReader;
        } progress{*this, context, {}};
        const ProjectIoObserver observer{[](void* state, ProjectIoStage stage, unsigned int value) noexcept
                                         {
                                             auto& stateValue = *static_cast<Progress*>(state);
                                             auto& job = stateValue.job;
                                             if (job._gate)
                                             {
                                                 auto& trace = *job._gate;
                                                 if (trace.observationCount < trace.observations.size())
                                                     trace.observations[trace.observationCount++] = {stage, value};
                                                 else
                                                     trace.traceTruncated = true;
                                             }
                                             if (stage == ProjectIoStage::Published)
                                             {
                                                 // Independent external truth survives a generic Cancelled/Failed
                                                 // scheduler outcome.
                                                 job._published = true;
                                                 stateValue.context.ReportProgress(100);
                                             }
                                             else if (stage == ProjectIoStage::AssetEncoded)
                                                 stateValue.context.ReportProgress(
                                                     job._assets == 0 ? 80 : value * 80 / job._assets);
                                             else if (stage == ProjectIoStage::ArchiveWriting)
                                                 stateValue.context.ReportProgress(80 + value / 5);
                                             else if (stage == ProjectIoStage::Staged)
                                                 stateValue.context.ReportProgress(99);
                                         },
                                         &progress};
        ProjectSaveFaultPolicy faultPolicy;
        if (_gate && (_gate->faultStageForTest != ProjectSaveFaultStage::Count || _gate->holdRecoveryReaderForTest ||
                      _gate->createAssetCollisionForTest))
        {
            faultPolicy = {
                [](void* raw, ProjectSaveFaultStage stage) noexcept -> std::uint32_t
                {
                    auto& state = *static_cast<Progress*>(raw);
                    auto& trace = *state.job._gate;
                    const auto index = static_cast<std::size_t>(stage);
                    if (index >= trace.faultVisits.size())
                    {
                        trace.faultTraceTruncated = true;
                        return ERROR_ARITHMETIC_OVERFLOW;
                    }
                    ++trace.faultVisits[index];
                    if (trace.createAssetCollisionForTest && stage == ProjectSaveFaultStage::ManifestClose)
                    {
                        // Only the exclusive fixture's one owned staging directory is eligible. Keep the
                        // noexcept operation seam allocation-free; all request strings were prepared by the owner.
                        WIN32_FIND_DATAW found{};
                        wil::unique_hfind search{FindFirstFileW(trace.assetStagingPatternForTest.c_str(), &found)};
                        std::array<wchar_t, MAX_PATH> leaf{};
                        bool endedNormally = false;
                        if (search)
                        {
                            do
                            {
                                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                    (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                                {
                                    ++trace.assetStagingMatchesForTest;
                                    if (trace.assetStagingMatchesForTest == 1)
                                        std::wmemcpy(leaf.data(), found.cFileName, std::wcslen(found.cFileName) + 1);
                                    else
                                        break;
                                }
                            } while (FindNextFileW(search.get(), &found));
                            endedNormally =
                                trace.assetStagingMatchesForTest == 1 && GetLastError() == ERROR_NO_MORE_FILES;
                        }
                        std::array<wchar_t, 1024> collision{};
                        constexpr std::wstring_view images{L"\\images\\"};
                        const auto leafLength = std::wcslen(leaf.data());
                        const bool boundedRequest = trace.assetStagingParentForTest.size() < collision.size() &&
                                                    trace.assetLeafForTest.size() < collision.size();
                        const auto length = boundedRequest ? trace.assetStagingParentForTest.size() + leafLength +
                                                                 images.size() + trace.assetLeafForTest.size()
                                                           : collision.size();
                        if (endedNormally && length < collision.size())
                        {
                            auto* next = collision.data();
                            std::wmemcpy(next, trace.assetStagingParentForTest.data(),
                                         trace.assetStagingParentForTest.size());
                            next += trace.assetStagingParentForTest.size();
                            std::wmemcpy(next, leaf.data(), leafLength);
                            next += leafLength;
                            std::wmemcpy(next, images.data(), images.size());
                            next += images.size();
                            std::wmemcpy(next, trace.assetLeafForTest.data(), trace.assetLeafForTest.size());
                            trace.assetCollisionCreatedForTest = CreateDirectoryW(collision.data(), nullptr) != FALSE;
                            trace.assetCollisionErrorForTest =
                                trace.assetCollisionCreatedForTest ? ERROR_SUCCESS : GetLastError();
                        }
                        else
                            trace.assetCollisionErrorForTest = ERROR_INVALID_DATA;
                        // Return the ordinary healthy operation policy. The real asset encoder must refuse
                        // replacing this directory, and own both its file temp and parent-tree cleanup.
                    }
                    if (trace.holdAtFaultForTest && stage == trace.faultStageForTest &&
                        trace.faultVisits[index] == trace.faultAtVisitForTest)
                    {
                        // The real writer remains owned. Test failure arrives only after the native timeout returns.
                        trace.heldFaultResultForTest = state.job.PassGate(state.context);
                        trace.heldFaultCancelledForTest = state.context.cancellation.IsCancelled();
                    }
                    if (trace.holdRecoveryReaderForTest && stage == ProjectSaveFaultStage::StagingToDestinationRename)
                    {
                        // Rotation has succeeded. No DELETE sharing: native rollback must actually fail.
                        state.recoveryReader.reset(CreateFileW(trace.recoveryManifestForTest.c_str(), GENERIC_READ,
                                                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                               FILE_ATTRIBUTE_NORMAL, nullptr));
                        trace.recoveryReaderOpenedForTest = static_cast<bool>(state.recoveryReader);
                        trace.recoveryReaderErrorForTest = state.recoveryReader ? ERROR_SUCCESS : GetLastError();
                        const wil::unique_handle deleteAccess{
                            CreateFileW(trace.recoveryManifestForTest.c_str(), DELETE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
                        trace.recoveryDeleteAccessErrorWhileHeldForTest = deleteAccess ? ERROR_SUCCESS : GetLastError();
                        trace.recoveryDeleteAccessDeniedWhileHeldForTest =
                            !deleteAccess && trace.recoveryDeleteAccessErrorWhileHeldForTest == ERROR_SHARING_VIOLATION;
                    }
                    return (stage == trace.faultStageForTest &&
                            trace.faultVisits[index] == trace.faultAtVisitForTest) ||
                                   stage == trace.precedingFaultStageForTest
                               ? trace.faultErrorForTest
                               : 0;
                },
                &progress};
        }
        _status = SaveProject(_path.c_str(), _document, _active, zip, context.cancellation, observer, faultPolicy);
        if (_gate && _gate->holdRecoveryReaderForTest)
        {
            progress.recoveryReader.reset(); // Retire the actual reader before the original acknowledgement.
            _gate->recoveryReaderRetiredForTest = true;
        }
        if (_gate)
            _gate->saveStatus = _status;
        if (_gate && !_gate->traceOnly && _gate->afterPublication)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return _status == ProjectIoStatus::Cancelled ? JobResult{JobStatus::Cancelled} : JobResult{};
    }

    void Completed(JobResult result) noexcept override
    {
        const bool current = _owner._projectSaveStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
                             _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _stamp.ordinal &&
                             _owner._session.DocumentSerial() == _stamp.documentSerial;
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (_published && current)
        {
            _owner._documentPath = std::move(_path);
            _owner._session.history.MarkSaved(_revision);
            outcome = result.status == JobStatus::Cancelled || _owner._historyWork.CancelRequested(_stamp)
                          ? HostOutcome::Cancelled
                          : HostOutcome::Succeeded;
            error = outcome == HostOutcome::Cancelled ? E_ABORT : S_OK;
            _owner.SyncChrome();
            _owner.InvalidateFrame(); // save changes chrome, never document composition
        }
        else if (!current)
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (outcome == HostOutcome::Succeeded && (_status != ProjectIoStatus::Ok || !_published))
        {
            outcome = _status == ProjectIoStatus::Cancelled ? HostOutcome::Cancelled : HostOutcome::Failed;
            error = _status == ProjectIoStatus::TooLarge ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                    : outcome == HostOutcome::Cancelled  ? E_ABORT
                                                         : E_FAIL;
        }
        if (outcome == HostOutcome::Failed)
        {
            const auto stringId = error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
                                      ? IDS_ERROR_TOO_LARGE
                                      : IDS_ERROR_SAVE_PROJECT;
            ++_owner._errorsShown;
            _owner._lastErrorId = stringId;
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, stringId);
        }
        _owner._projectSaveWrite.reset();
        _owner.ClearEndSessionWriteBlock();
        _owner._projectSaveTicket.reset();
        _owner._projectSaveStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, ProjectSaveTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // owner completion only
    const HistoryWorkStamp _stamp;
    const CanvasDocument _document;
    const std::optional<Guid> _active;
    const Guid _revision;
    std::wstring _path;
    const std::shared_ptr<WorkerWriteCompletion> _write;
    const std::shared_ptr<ProjectSaveTestGate> _gate;
    const DWORD _ownerThread;
    unsigned int _assets = 0;
    ProjectIoStatus _status = ProjectIoStatus::Encode;
    bool _published = false; // worker-only writes, owner reads only after final acknowledged completion
};

HRESULT Application::StartProjectSave() noexcept
{
    if (!_activeHostRequest || !_session.Document() || _session.HasColorEdit() || _projectSaveTicket)
        return E_UNEXPECTED;
    const auto path = _activeHostRequest->input.SelectedOutputPath();
    if (!path)
        return E_UNEXPECTED;
    // Lexical absolute-path preparation performs no filesystem classification. Charge the resolved path,
    // including the process directory for relative arguments, before any per-source libzip path is created.
    std::unique_ptr<std::array<wchar_t, 32768>> resolvedStorage;
    try
    {
        resolvedStorage = std::make_unique<std::array<wchar_t, 32768>>();
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // Complete lexical-path admission refusal before history or worker acceptance.
    }
    auto& resolved = *resolvedStorage;
    const DWORD units = GetFullPathNameW(path->data(), static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (units == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    if (units >= resolved.size() || units > kMaxProjectOutputPathUnits)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    const auto ordinal = _activeHostRequest->ordinal;
    _projectSaveStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Commit);
    if (!_projectSaveStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        _projectSaveStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_projectSaveStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        const auto memory = ProjectSaveMemoryBytes(_session.Document().value(), units);
        constexpr auto bookkeeping = sizeof(ProjectSaveJob) + WorkerWriteCompletion::kReservationBytes;
        if (memory != 0 && memory <= (std::numeric_limits<std::uint64_t>::max)() - bookkeeping)
        {
            if (_projectSaveTestGate && _projectSaveTestGate->throwAdmissionFailure)
                throw std::bad_alloc{};
            auto write = std::make_shared<WorkerWriteCompletion>(WorkerWriteCompletion::Phase::Queued);
            result = write->CreationError();
            if (SUCCEEDED(result))
            {
                CanvasDocument snapshot = _session.Document().value();
                snapshot.selection.reset(); // selection is not a project manifest field
                std::unique_ptr<Job> job = std::make_unique<ProjectSaveJob>(
                    *this, _projectSaveStamp.value(), std::move(snapshot), _session.ActiveLayerID(),
                    _session.history.CurrentRevision(), std::wstring(resolved.data(), units), write);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Io, _session.DocumentSerial(), memory + bookkeeping}, ticket));
                if (SUCCEEDED(result))
                {
                    _projectSaveWrite = std::move(write);
                    _projectSaveTicket = ticket;
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // complete owning admission failure; no writer was accepted
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_projectSaveStamp.value());
        _hostFinishedStamp = _projectSaveStamp;
        _projectSaveStamp.reset();
        return result;
    }
    _hostCommitStamp = _projectSaveStamp;
    _hostWriteInFlight = true;
    return S_OK;
}

int Application::RunProjectSaveSelfTest() noexcept
{
    enum class Mode
    {
        Save,
        Ordered,
        CancelBefore,
        CancelPublished,
        CancelClosePublished,
        Failure,
        Allocation,
        Stale,
        EditedPublished,
        PendingTransform,
        PendingOpacity,
        Admission,
        PathBudget
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
        std::wstring output;
        bool visited = false, held = false, cancelPending = false;
        std::optional<Guid> laterRevision;
        std::optional<Guid> settledRevision;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != ProjectSaveTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const bool pendingEdit = state.mode == Mode::PendingTransform || state.mode == Mode::PendingOpacity;
        if (pendingEdit)
            state.settledRevision = owner._session.history.CurrentRevision();
        state.held = owner._projectSaveTicket && owner._projectSaveStamp && owner._historyWork.Busy() &&
                     owner._hostWriteInFlight && owner._activeHostRequest &&
                     state.receipt.Read().outcome == HostOutcome::Pending && owner._jobs.ReservedMemoryBytes() != 0 &&
                     (pendingEdit ? state.settledRevision != state.revision && !owner._session.PendingTransformEdit() &&
                                        !owner._opacityEdit
                                  : owner._session.history.CurrentRevision() == state.revision) &&
                     owner._session.history.IsModified();
        const bool afterPublication = owner._projectSaveTestGate->afterPublication;
        state.held =
            state.held && ((GetFileAttributesW(state.output.c_str()) != INVALID_FILE_ATTRIBUTES) == afterPublication);
        owner.OnCommand(ChromeHost::kCommandZoomActual);
        state.held =
            state.held && owner._session.history.CurrentRevision() == state.settledRevision.value_or(state.revision);
        if (state.mode == Mode::CancelBefore || state.mode == Mode::CancelPublished ||
            state.mode == Mode::CancelClosePublished)
        {
            owner.CancelHostContinuations();
            state.cancelPending = owner._activeHostRequest && owner._projectSaveTicket &&
                                  state.receipt.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale)
        {
            owner._session.CreateDocument(3, 2); // deliberately bypass production ordering to test lineage refusal
            state.laterRevision = owner._session.history.CurrentRevision();
        }
        else if (state.mode == Mode::EditedPublished)
        {
            owner._session.InvertPixels(); // test only: save must mark the captured revision, never this later edit
            state.laterRevision = owner._session.history.CurrentRevision();
        }
        (void)SetEvent(owner._projectSaveTestGate->release.get());
    };
    const auto temporaryStorage = std::make_unique<std::array<wchar_t, 32768>>();
    auto& temporary = *temporaryStorage;
    const DWORD units = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    if (units == 0 || units >= temporary.size())
        return 5;
    const std::filesystem::path root =
        std::filesystem::path(temporary.data()) / (L"RedPrism.ProjectSaveHost." + Guid::Generate().ToString());
    if (!CreateDirectoryW(root.c_str(), nullptr))
        return 5;
    const auto oldForcedAnswer = _forcedDiscardAnswer;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_projectSaveTestGate)
                (void)SetEvent(_projectSaveTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5); // no borrowed test state survives a failed join
            _projectSaveTestGate.reset();
            _layerMetadataTestGate.reset();
            _forcedDiscardAnswer = oldForcedAnswer;
            std::error_code error;
            const auto target = std::filesystem::weakly_canonical(root, error);
            const auto base = std::filesystem::weakly_canonical(std::filesystem::path(temporary.data()), error);
            if (!error && target.parent_path() == base)
                std::filesystem::remove_all(target, error);
        });
    for (const auto mode :
         {Mode::Save, Mode::Ordered, Mode::CancelBefore, Mode::CancelPublished, Mode::CancelClosePublished,
          Mode::Failure, Mode::Allocation, Mode::Stale, Mode::EditedPublished, Mode::PendingTransform,
          Mode::PendingOpacity, Mode::Admission, Mode::PathBudget})
    {
        // CreateDocument records the previous document in Undo. Explicitly close this independent fixture
        // first: its metadata output remains correctly charged while document/history/display readers exist.
        // Join actual deferred destruction before admitting the next fixture; never relax its zero baseline.
        _layerMetadataTestGate.reset();
        _session.ClearDocument();
        DocumentChanged();
        const auto priorRetired = SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
        if (priorRetired != SettleResult::Settled || _session.Document() || _session.history.UndoCount() != 0 ||
            _session.history.CanRedo() || _jobs.ReservedMemoryBytes() != 0)
        {
            std::fprintf(stderr,
                         "DIAGNOSTIC project-save-host.prior-fixture case=%u settled=%u document=%u "
                         "undo=%llu redo=%llu reserved=%llu\n",
                         static_cast<unsigned int>(mode), priorRetired == SettleResult::Settled ? 1u : 0u,
                         _session.Document() ? 1u : 0u, static_cast<unsigned long long>(_session.history.UndoCount()),
                         _session.history.CanRedo() ? 1ull : 0ull,
                         static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()));
            std::fflush(stderr);
            return 5;
        }
        _session.CreateDocument(2, 1);
        const auto id = InsertPaintedLayer(_session, L"Save snapshot", 2, 1, {}, 0, 0xFF404040);
        _session.history.MarkSaved();
        _session.InvertPixels(); // independent captured literal is 191/255 in every color channel
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        if (mode == Mode::PendingTransform)
        {
            _session.BeginTransform();
            auto draft = _session.ActiveLayer()->transform;
            draft.origin.x = 1;
            _session.PreviewTransform(draft);
        }
        else if (mode == Mode::PendingOpacity)
        {
            // Save closes this edit and retains its document/history output. Observe the current original
            // metadata ticket without holding the worker or adding a source/control owner.
            _layerMetadataTestGate = std::make_shared<LayerMetadataTestGate>();
            _layerMetadataTestGate->hold = false;
            OnLayerOpacity(0.5, ChromeEditPhase::Preview);
        }
        const auto errors = _errorsShown;
        _projectSaveTestGate = std::make_shared<ProjectSaveTestGate>();
        if (!_projectSaveTestGate->release || id == Guid{})
            return 5;
        _projectSaveTestGate->afterPublication =
            mode == Mode::CancelPublished || mode == Mode::CancelClosePublished || mode == Mode::EditedPublished;
        if (mode == Mode::Failure)
            _projectSaveTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _projectSaveTestGate->throwAllocationFailure = mode == Mode::Allocation;
        _projectSaveTestGate->throwAdmissionFailure = mode == Mode::Admission;
        const std::wstring output =
            mode == Mode::PathBudget ? L"C:\\" + std::wstring(32635, L'a')
                                     : (root / (std::to_wstring(static_cast<unsigned int>(mode)) + L".prism")).wstring();
        const bool close = mode == Mode::CancelClosePublished;
        _documentPath = close ? output : L"before-save.prism";
        _forcedDiscardAnswer = Prompts::Answer::First;
        DocumentChanged();
        std::wstring callerPath = output;
        const auto receipt = close ? QueueHostInput(HostCloseProjectInput{})
                                   : QueueHostInput(HostWriteInput{HostWriteFormat::Project, true, callerPath});
        callerPath = L"changed-after-admission.prism";
        std::optional<ContinuationReceipt> younger;
        if (mode == Mode::Ordered)
            younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        State state{*this, mode, receipt, revision, output};
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto actual = receipt.Read();
        const bool cancelled = mode == Mode::CancelBefore || mode == Mode::CancelPublished || close;
        const auto expected =
            mode == Mode::Stale ? HostOutcome::Stale
            : cancelled         ? HostOutcome::Cancelled
            : mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::PathBudget
                ? HostOutcome::Failed
                : HostOutcome::Succeeded;
        const bool accountingSettled = mode == Mode::PendingOpacity
                                           ? _layerMetadataTestGate->originalAccountingBound &&
                                                 _layerMetadataTestGate->accountingOutputBytes > 0 &&
                                                 MetadataAccountingSettledForTest()
                                           : _jobs.ReservedMemoryBytes() == 0;
        bool correct = settled == SettleResult::Settled &&
                       (mode == Mode::Admission || mode == Mode::PathBudget
                            ? !state.visited && _projectSaveTestGate->visits.load() == 0
                            : state.visited && state.held) &&
                       actual.outcome == expected && !_activeHostRequest && !_hostWriteInFlight &&
                       !_projectSaveTicket && !_projectSaveStamp && !_historyWork.Busy() && accountingSettled &&
                       _hostInputBudget.Used() == 0 && _session.Document().has_value();
        if (cancelled)
            correct = correct && state.cancelPending;
        const bool published = mode != Mode::CancelBefore && mode != Mode::Failure && mode != Mode::Allocation &&
                               mode != Mode::Admission && mode != Mode::PathBudget;
        if (published)
        {
            const auto loaded = LoadProject(output.c_str());
            correct = correct && loaded.status == ProjectIoStatus::Ok && loaded.document.width == 2 &&
                      loaded.document.height == 1 && loaded.document.layers.size() == 1 && loaded.activeLayerID == id &&
                      loaded.document.layers.front().asset &&
                      loaded.document.layers.front().asset->image.Data()[0] == 191 &&
                      loaded.document.layers.front().asset->image.Data()[1] == 191 &&
                      loaded.document.layers.front().asset->image.Data()[2] == 191 &&
                      loaded.document.layers.front().asset->image.Data()[3] == 255;
        }
        else
            correct = correct && GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES &&
                      _documentPath == L"before-save.prism" && _session.history.CurrentRevision() == revision &&
                      _session.history.IsModified();
        if (mode == Mode::Stale)
            correct = correct && _documentPath == L"before-save.prism" && _session.Document()->width == 3 &&
                      _session.Document()->height == 2 && _session.history.CurrentRevision() == state.laterRevision;
        else if (mode == Mode::EditedPublished || mode == Mode::Ordered)
            correct = correct && _documentPath == output && _session.history.IsModified() &&
                      _session.history.UndoCount() == historyCount + 1 &&
                      _session.Document()->layers.front().asset->image.Data()[0] == 64;
        else if (published)
            correct = correct && _documentPath == output && !_session.history.IsModified() &&
                      _session.history.CurrentRevision() == state.settledRevision.value_or(revision) &&
                      _session.history.UndoCount() == historyCount + (state.settledRevision ? 1 : 0);
        if (mode == Mode::PendingTransform || mode == Mode::PendingOpacity)
        {
            const auto loaded = LoadProject(output.c_str());
            const auto* layer = loaded.document.layers.empty() ? nullptr : &loaded.document.layers.front();
            correct = correct && loaded.status == ProjectIoStatus::Ok && layer &&
                      (mode == Mode::PendingTransform ? layer->transform.origin.x == 1 : layer->opacity == 0.5);
        }
        if (younger)
            correct = correct && younger->Read().outcome == HostOutcome::Succeeded;
        if (mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::Admission || mode == Mode::PathBudget)
            correct = correct && _errorsShown == errors + 1 && _lastErrorId == IDS_ERROR_SAVE_PROJECT &&
                      actual.error == static_cast<std::int32_t>(mode == Mode::Failure ? E_ACCESSDENIED
                                                                : mode == Mode::PathBudget
                                                                    ? HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE)
                                                                    : E_OUTOFMEMORY);
        else
            correct = correct && _errorsShown == errors;
        if (!correct)
        {
            const auto metadata = _layerMetadataTestGate;
            std::fprintf(
                stderr,
                "DIAGNOSTIC project-save-host.case=%u settled=%u visited=%u visits=%u held=%u outcome=%u "
                "expected=%u error=%08X active=%u write=%u ticket=%u stamp=%u historyBusy=%u accounting=%u "
                "reserved=%llu releasing=%llu idle=%u input=%llu document=%u published=%u exists=%u "
                "pathBefore=%u revisionSame=%u modified=%u errors=%u originalErrors=%u errorId=%u "
                "metaBound=%u metaObserved=%u metaOutput=%llu metaTemporaryExpired=%u metaOutputExpired=%u "
                "preparedRows=%u committedDisplay=%u pendingDisplay=%u\n",
                static_cast<unsigned int>(mode), settled == SettleResult::Settled ? 1u : 0u, state.visited ? 1u : 0u,
                _projectSaveTestGate->visits.load(), state.held ? 1u : 0u, static_cast<unsigned int>(actual.outcome),
                static_cast<unsigned int>(expected), static_cast<unsigned int>(actual.error),
                _activeHostRequest ? 1u : 0u, _hostWriteInFlight ? 1u : 0u, _projectSaveTicket ? 1u : 0u,
                _projectSaveStamp ? 1u : 0u, _historyWork.Busy() ? 1u : 0u, accountingSettled ? 1u : 0u,
                static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()), _jobs.IsIdle() ? 1u : 0u,
                static_cast<unsigned long long>(_hostInputBudget.Used()), _session.Document() ? 1u : 0u,
                published ? 1u : 0u, GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES ? 1u : 0u,
                _documentPath == L"before-save.prism" ? 1u : 0u,
                _session.history.CurrentRevision() == revision ? 1u : 0u, _session.history.IsModified() ? 1u : 0u,
                static_cast<unsigned int>(_errorsShown), static_cast<unsigned int>(errors),
                static_cast<unsigned int>(_lastErrorId), metadata && metadata->originalAccountingBound ? 1u : 0u,
                metadata && metadata->accountingObserved ? 1u : 0u,
                metadata ? static_cast<unsigned long long>(metadata->accountingOutputBytes) : 0ull,
                !metadata || metadata->accountingTemporary.expired() ? 1u : 0u,
                !metadata || metadata->accountingOutput.expired() ? 1u : 0u, _preparedLayerRowsEnabled ? 1u : 0u,
                _committedDisplayInitialized ? 1u : 0u, _pendingDisplayInitialized ? 1u : 0u);
            std::fflush(stderr);
        }
        std::fprintf(stderr, "%s project-save-host.case=%u\n", correct ? "PASS" : "FAIL",
                     static_cast<unsigned int>(mode));
        if (!correct)
            return 5;
    }
    return 0;
}

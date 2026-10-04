#include "Application.h"

#include "ChromeHost.h"
#include "FileWorkTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <utility>
#include <vector>

class Application::PathClassificationJob final : public Job
{
  public:
    PathClassificationJob(Application& owner, std::uint64_t ordinal, std::uint64_t serial,
                          const std::vector<std::wstring>& paths)
        : _owner(owner), _ordinal(ordinal), _serial(serial), _paths(paths), _gate(owner._fileWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        std::array<std::wstring_view, kMaxPathClassificationBatch> views{};
        std::array<std::size_t, kMaxPathClassificationBatch> indices{};
        std::size_t count = 0;
        for (std::size_t index = 0; index < _paths.size(); ++index)
        {
            // Host batches historically skip empty paths; the worker helper itself rejects an empty entry.
            if (!_paths[index].empty())
            {
                views[count] = _paths[index];
                indices[count++] = index;
            }
        }
        const auto classified =
            ClassifyPathBatch(std::span<const std::wstring_view>(views.data(), count), context.cancellation);
        _result.status = classified.status;
        if (classified.status == PathClassificationStatus::Ok)
        {
            _result.classifications.fill(PathClassification::Image);
            for (std::size_t index = 0; index < count; ++index)
                _result.classifications[indices[index]] = classified.classifications[index];
            _result.count = _paths.size();
        }
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (context.cancellation.IsCancelled() || _result.status == PathClassificationStatus::Cancelled)
            return {JobStatus::Cancelled};
        if (_result.status == PathClassificationStatus::OutOfMemory)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        const bool current = _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _ordinal &&
                             _owner._activeHostRequest->documentSerial == _serial &&
                             _owner._session.DocumentSerial() == _serial;
        _owner._pathClassificationTicket.reset();
        const auto outcome = !current ? HostOutcome::Stale
                             : _owner._pathClassificationCancelled || result.status == JobStatus::Cancelled
                                 ? HostOutcome::Cancelled
                             : result.status == JobStatus::Failed || _result.status != PathClassificationStatus::Ok
                                 ? HostOutcome::Failed
                                 : HostOutcome::Succeeded;
        if (outcome == HostOutcome::Succeeded)
        {
            _owner._pathClassifications = _result;
            (void)_owner._jobs.RequestUiActionSource(_owner._continuationAction);
        }
        else
        {
            if (outcome == HostOutcome::Failed)
                _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_OPEN_PROJECT);
            _owner.FinishHostContinuation({outcome, outcome == HostOutcome::Failed
                                                        ? (result.error != 0 ? result.error : E_INVALIDARG)
                                                        : E_ABORT});
        }
    }

  private:
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, FileWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner; // owner callback only
    const std::uint64_t _ordinal;
    const std::uint64_t _serial;
    const std::vector<std::wstring> _paths;
    const std::shared_ptr<FileWorkTestGate> _gate;
    const DWORD _ownerThread;
    PathClassificationBatch _result;
};

HRESULT Application::StartPathClassification(const HostPathsInput& input) noexcept
{
    if (!_activeHostRequest || _pathClassificationTicket || input.paths.size() > kMaxPathClassificationBatch)
        return E_UNEXPECTED;
    _pathClassificationCancelled = false;
    _pathClassifications = {};
    if (input.action != HostPathAction::OpenPaths)
    {
        // Explicit image import does not inspect the filesystem to reinterpret the caller's chosen action.
        _pathClassifications.status = PathClassificationStatus::Ok;
        _pathClassifications.count = input.paths.size();
        _pathClassifications.classifications.fill(PathClassification::Image);
        return S_OK;
    }
    try
    {
        // The job owns another bounded path snapshot, because shutdown can retire the selected host request
        // before workers join. Charge that copy; the event payload retains its separate host-input reservation.
        // Includes the terminated 64 KiB path plus bounded view/index/result arrays and callback frames.
        std::uint64_t memory = sizeof(PathClassificationJob) + 96 * 1024;
        for (const auto& path : input.paths)
            memory += sizeof(std::wstring) + (static_cast<std::uint64_t>(path.size()) + 32) * sizeof(wchar_t) + 64;
        std::unique_ptr<Job> job = std::make_unique<PathClassificationJob>(
            *this, _activeHostRequest->ordinal, _activeHostRequest->documentSerial, input.paths);
        JobTicket ticket;
        const auto result = static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Io, 0, memory}, ticket));
        if (SUCCEEDED(result))
            _pathClassificationTicket = ticket;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // complete owning construction/admission failure, no filesystem work accepted
    }
}

bool Application::HostPathIsProject(std::size_t index) const noexcept
{
    return _pathClassifications.status == PathClassificationStatus::Ok && index < _pathClassifications.count &&
           _pathClassifications.classifications[index] == PathClassification::Project;
}

int Application::RunPathClassificationSelfTest() noexcept
{
    enum class Mode
    {
        Normal,
        Empty,
        CancelBefore,
        CancelAfter,
        Failure,
        Allocation,
        Replace
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
        bool visited = false;
        bool held = false;
        bool cancellationPending = false;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != FileWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._pathClassificationTicket && owner._activeHostRequest && !owner._historyWork.Busy() &&
                     state.receipt.Read().outcome == HostOutcome::Pending && owner._jobs.ReservedMemoryBytes() != 0 &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._session.Document()->width == 8 && owner._session.Document()->height == 4;
        if (state.mode == Mode::CancelBefore)
            owner.CancelHostContinuations();
        else if (state.mode == Mode::CancelAfter)
            owner._pathClassificationCancelled = true; // even a late Finished result must not run a file action
        else if (state.mode == Mode::Replace)
            owner._session.CreateDocument(3, 2);
        if (state.mode == Mode::CancelBefore || state.mode == Mode::CancelAfter)
            state.cancellationPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                        owner._activeHostRequest.has_value() &&
                                        owner._pathClassificationTicket.has_value();
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._fileWorkTestGate->release.get());
    };
    const auto forcedImage = _forcedImageOpen;
    const auto restore = wil::scope_exit(
        [&]
        {
            _forcedImageOpen = forcedImage;
            _fileWorkTestGate.reset();
        });
    _forcedImageOpen = ImageOpenChoice::Cancel;
    for (const auto mode : {Mode::Normal, Mode::Empty, Mode::CancelBefore, Mode::CancelAfter, Mode::Failure,
                            Mode::Allocation, Mode::Replace})
    {
        _session.CreateDocument(8, 4);
        (void)InsertPaintedLayer(_session, L"Classification source", 8, 4, {}, 0, 0xFF404040);
        _session.history.MarkSaved();
        DocumentChanged();
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        const auto presents = _renderer.PresentCount();
        const auto errors = _errorsShown;
        _fileWorkTestGate = std::make_shared<FileWorkTestGate>();
        if (!_fileWorkTestGate->release)
            return 5;
        _fileWorkTestGate->afterPreparation = mode != Mode::CancelBefore && mode != Mode::Allocation;
        _fileWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        if (mode == Mode::Failure)
            _fileWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        std::wstring callerPath = mode == Mode::Empty ? L"" : L"RedPrism.classification.harness.missing.png";
        const std::array<std::wstring_view, 2> paths{L"", callerPath};
        const auto receipt = QueueHostPaths(paths, HostPathAction::OpenPaths);
        callerPath = L"this-later-caller-value.prism";
        const bool younger = mode == Mode::Normal || mode == Mode::Empty;
        std::optional<ContinuationReceipt> invert;
        if (younger)
            invert = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        State state{*this, mode, receipt, revision};
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto actual = receipt.Read();
        const auto expected = mode == Mode::Replace                               ? HostOutcome::Stale
                              : mode == Mode::Failure || mode == Mode::Allocation ? HostOutcome::Failed
                              : mode == Mode::Empty                               ? HostOutcome::Succeeded
                                                                                  : HostOutcome::Cancelled;
        bool correct = settled == SettleResult::Settled && state.visited && state.held && actual.outcome == expected &&
                       !_pathClassificationTicket && !_activeHostRequest && !_historyWork.Busy() &&
                       _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0 &&
                       _renderer.PresentCount() == presents && _errorsShown == errors;
        if (mode == Mode::CancelBefore || mode == Mode::CancelAfter)
            correct = correct && state.cancellationPending;
        if (mode == Mode::Failure)
            correct = correct && actual.error == E_ACCESSDENIED;
        if (mode == Mode::Allocation)
            correct = correct && actual.error == E_OUTOFMEMORY;
        if (mode == Mode::Replace)
            correct = correct && _session.Document()->width == 3 && _session.Document()->height == 2;
        else if (younger)
            correct = correct && invert->Read().outcome == HostOutcome::Succeeded &&
                      _session.history.UndoCount() == historyCount + 1 && _session.history.UndoName() == L"Invert";
        else
            correct = correct && _session.Document()->width == 8 && _session.Document()->height == 4 &&
                      _session.history.CurrentRevision() == revision;
        std::fprintf(stderr, "%s path-classification-host.case=%u\n", correct ? "PASS" : "FAIL",
                     static_cast<unsigned int>(mode));
        if (!correct)
            return 5;
    }
    return 0;
}

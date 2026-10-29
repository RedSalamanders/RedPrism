#include "Application.h"

#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "Runtime.Tests.FileWorkTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class Application::PathClassificationJob final : public Job, public PathReadWaitReceiver
{
    enum class Phase
    {
        Allocate,
        CapturePaths,
        Classify,
        NeedIssue,
        Issue,
        Observe,
        NeedRearm,
        BatchComplete,
        Retire,
        Retired
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
    struct Payload final : DeferredReleaseNode
    {
        std::vector<std::wstring> paths;
        PathClassificationRead read;
        Payload(std::uint64_t allowance, void* event, PathClassificationReadSource source)
        {
            bytes = allowance;
            destroy = [](DeferredReleaseNode* node) noexcept { delete static_cast<Payload*>(node); };
            read.Initialize(event, source);
        }
    };
#pragma warning(pop)

    struct PayloadDelete final
    {
        void operator()(Payload* value) const noexcept
        {
            if (value && !TryDeferRelease(*value, DeferredReleasePolicy::AlwaysOnOwner))
                delete value;
        }
    };

  public:
    PathClassificationJob(Application& owner, std::uint64_t ordinal, std::uint64_t serial, std::uint64_t memory,
                          void* event)
        : _owner(owner), _ordinal(ordinal), _serial(serial), _memory(memory), _event(event),
          _source(owner._pathReadSource), _gate(owner._fileWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

    [[nodiscard]] static constexpr std::uint64_t ScratchAndMetadataBytes() noexcept
    {
        return sizeof(PathClassificationJob) + sizeof(Payload) + 96 * 1024;
    }
    JobResult Run(JobContext& context) override
    {
        _cancellation = context.cancellation;
        switch (_phase)
        {
        case Phase::Allocate:
        {
            if (_gate && !_gate->afterPreparation)
            {
                const auto gated = PassGate(context);
                if (gated.status != JobStatus::Finished)
                    return gated;
            }
            if (_gate && _gate->throwAllocationFailure)
                throw std::bad_alloc{};
            auto payload = std::make_unique<Payload>(_memory, _event, _source);
            _payload.reset(payload.release()); // original worker allocates all private scratch
            _result.classifications.fill(PathClassification::Image);
            _phase = Phase::CapturePaths;
            return {};
        }
        case Phase::Classify:
            return ContinuePathsWorker(context);
        case Phase::Issue:
        {
            const auto issued = _payload->read.Issue(context.cancellation);
            if (issued == PathClassificationReadStep::InvalidInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            _phase = Phase::Observe;
            return {}; // original event cycle must acknowledge immediate, pending and failed native issue
        }
        case Phase::Observe:
        {
            const auto acknowledged = _payload->read.Acknowledge(context.cancellation);
            if (acknowledged == PathClassificationReadStep::Pending)
            {
                _phase = Phase::NeedRearm;
                return {};
            }
            if (acknowledged == PathClassificationReadStep::Cancelled)
                return {JobStatus::Cancelled};
            _result.classifications[_index++] = _payload->read.Classification();
            _phase = Phase::Classify;
            return ContinuePathsWorker(context);
        }
        case Phase::Retire:
            _payload.reset();
            _phase = Phase::Retired;
            return _terminalResult;
        case ::Application::PathClassificationJob::Phase::BatchComplete:
        case ::Application::PathClassificationJob::Phase::CapturePaths:
        case ::Application::PathClassificationJob::Phase::NeedIssue:
        case ::Application::PathClassificationJob::Phase::NeedRearm:
        case ::Application::PathClassificationJob::Phase::Retired:
        default:
            return {JobStatus::Failed, E_UNEXPECTED};
        }
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        if (result.status != JobStatus::Finished && !_terminal)
        {
            _terminal = true;
            _terminalResult = result;
        }
        if (!_terminal || !_payload)
            return;
        if (_readCycle && _payload->read.ReadyToIssue())
            _payload->read.RetireUnissuedCycleWorker();
        if (_payload->read.HasPendingRead())
        {
            const auto acknowledged = _payload->read.Acknowledge(context.cancellation, true);
            if (acknowledged == PathClassificationReadStep::Pending)
            {
                _phase = _readCycle ? Phase::Observe : Phase::NeedRearm;
                return;
            }
        }
        if (_readCycle)
            return; // actual read can be complete before the original event callback publishes
        _payload.reset();
        _phase = Phase::Retired;
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        if (result.status != JobStatus::Finished && !_terminal)
        {
            _terminal = true;
            _terminalResult = result;
        }
        if (_owner._pathClassificationCancelled)
        {
            if (!_terminal)
            {
                _terminal = true;
                _terminalResult = {JobStatus::Cancelled};
            }
            (void)_owner._jobs.Cancel(ticket);
        }
        if (_readCycle)
        {
            if (!_owner._pathReadWait.AwaitRead(ticket))
                std::terminate();
            return true;
        }
        if (_payload && _payload->read.HasPendingRead())
        {
            // Only a consumed premature event reaches here. Worker reset/rechecked before returning;
            // rearm without ResetEvent preserves an actual completion arriving after that recheck.
            const auto prepared = _owner._pathReadWait.PrepareRead(ticket, _cancellation, *this, false);
            if (FAILED(prepared))
                std::terminate(); // no valid lifetime may discard a driver-owned operation
            _readCycle = true;
            if (!_owner._pathReadWait.AwaitRead(ticket))
                std::terminate();
            return true;
        }
        if (_terminal && _payload)
        {
            _phase = Phase::Retire;
            ResumeOriginal(ticket);
            return true;
        }
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_phase == Phase::Retired)
            return false;
        if (!Current() || _owner._pathClassificationCancelled)
        {
            _stale = !Current();
            _terminal = true;
            _terminalResult = {_stale ? JobStatus::Failed : JobStatus::Cancelled, E_ABORT};
            _phase = Phase::Retire;
            ResumeOriginal(ticket);
            return true;
        }
        if (_phase == Phase::CapturePaths)
        {
            if (!_owner._activeHostRequest->input.TryTakePaths(_payload->paths))
            {
                _terminal = true;
                _terminalResult = {JobStatus::Failed, E_UNEXPECTED};
                _phase = Phase::Retire;
            }
            else
                _phase = Phase::Classify;
            ResumeOriginal(ticket);
            return true;
        }
        if (_phase == Phase::NeedIssue)
        {
            const auto prepared = _owner._pathReadWait.PrepareRead(ticket, _cancellation, *this, true);
            if (FAILED(prepared))
            {
                _terminal = true;
                _terminalResult = {JobStatus::Failed, prepared};
                _phase = Phase::Retire;
            }
            else
            {
                _readCycle = true;
                _phase = Phase::Issue;
            }
            ResumeOriginal(ticket);
            return true;
        }
        if (_phase == Phase::BatchComplete)
        {
            if (!_owner._activeHostRequest->input.TryRestorePaths(_payload->paths))
            {
                _terminal = true;
                _terminalResult = {JobStatus::Failed, E_UNEXPECTED};
            }
            _phase = Phase::Retire;
            ResumeOriginal(ticket);
            return true;
        }
        std::terminate();
    }
    void PathReadReady(JobTicket ticket, bool cancellationOnly) noexcept override
    {
        assert(ticket == _ticket && _readCycle);
        if (!cancellationOnly)
            _readCycle = false;
        _phase = Phase::Observe;
        ResumeOriginal(ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        assert(!_payload && !_readCycle);
        if (_owner._pathClassificationTicket == std::optional(_ticket))
            _owner._pathClassificationTicket.reset();
        const auto outcome = _stale || !Current() ? HostOutcome::Stale
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
    bool Current() const noexcept
    {
        return _owner._activeHostRequest && _owner._activeHostRequest->ordinal == _ordinal &&
               _owner._activeHostRequest->documentSerial == _serial && _owner._session.DocumentSerial() == _serial;
    }
    void ResumeOriginal(JobTicket ticket) noexcept
    {
        if (!_owner._jobs.Resume(ticket))
            std::terminate();
    }
    JobResult ContinuePathsWorker(JobContext& context)
    {
        while (_index < _payload->paths.size())
        {
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            const auto& path = _payload->paths[_index];
            if (path.empty())
            {
                ++_index;
                continue;
            }
            const auto step = _payload->read.Prepare(path, context.cancellation);
            if (step == PathClassificationReadStep::Cancelled)
                return {JobStatus::Cancelled};
            if (step == PathClassificationReadStep::InvalidInput)
                return {JobStatus::Failed, E_INVALIDARG};
            if (step == PathClassificationReadStep::ReadyToIssue)
            {
                _phase = Phase::NeedIssue;
                return {};
            }
            _result.classifications[_index++] = _payload->read.Classification();
        }
        _result.status = PathClassificationStatus::Ok;
        _result.count = _payload->paths.size();
        _phase = Phase::BatchComplete;
        if (_gate && _gate->afterPreparation)
            return PassGate(context);
        return {};
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, FileWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000); // controlled test hold only
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        return waited == WAIT_OBJECT_0 ? _gate->result
                                       : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    Application& _owner;
    const std::uint64_t _ordinal, _serial, _memory;
    void* const _event;
    const PathClassificationReadSource _source;
    const std::shared_ptr<FileWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<Payload, PayloadDelete> _payload;
    CancelToken _cancellation;
    JobTicket _ticket;
    PathClassificationBatch _result;
    JobResult _terminalResult;
    std::size_t _index = 0;
    Phase _phase = Phase::Allocate;
    bool _readCycle = false, _terminal = false, _stale = false;
};
#pragma warning(pop)

HRESULT Application::StartPathClassification(const HostPathsInput& input) noexcept
{
    if (!_activeHostRequest || _pathClassificationTicket || input.paths.size() > kMaxPathClassificationBatch)
        return E_UNEXPECTED;
    _pathClassificationCancelled = false;
    _pathClassifications = {};
    if (input.action != HostPathAction::OpenPaths)
    {
        _pathClassifications.status = PathClassificationStatus::Ok;
        _pathClassifications.count = input.paths.size();
        _pathClassifications.classifications.fill(PathClassification::Image);
        return S_OK;
    }
    // Every production path factory admits the root before copying. Refuse an unbudgeted fabricated path
    // request rather than transfer an unknown-capacity root under a guessed 1 MiB allowance.
    const auto pathBytes = _activeHostRequest->input.ReservedInputBytes();
    if (!pathBytes || pathBytes > HostInputBudget::kStorageLimit)
        return E_INVALIDARG;
    void* const event = _pathReadWait.EventForWorker();
    if (!event)
        return E_UNEXPECTED;
    try
    {
        const std::uint64_t memory = PathClassificationJob::ScratchAndMetadataBytes() + pathBytes;
        std::unique_ptr<Job> job = std::make_unique<PathClassificationJob>(
            *this, _activeHostRequest->ordinal, _activeHostRequest->documentSerial, memory, event);
        JobOptions options{JobClass::Io, 0, memory};
        options.deferredRetirement = true;
        JobTicket ticket;
        const auto submitted = static_cast<HRESULT>(_jobs.Submit(job, options, ticket));
        if (SUCCEEDED(submitted))
            _pathClassificationTicket = ticket;
        return submitted;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY; // owning wrapper/admission boundary; no IO or input ownership accepted
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
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
#pragma warning(pop)

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

#include "Document.Tests.OwnedMetadataJobsR4.h"

#include "Document.h"
#include "Jobs.h"
#include "LayerMetadataWork.h"
#include "SharedPublicationMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::size_t kLiveEdits = 110;
constexpr std::uint64_t kLongRunAssertions = 15 * kLiveEdits + 9;
constexpr std::uint64_t kExpectedAssertions = kLongRunAssertions + 3 + 5 + 4 + 11 + 5 + 5;
constexpr std::uint64_t kSixteenGiB = 16ull * 1024ull * 1024ull * 1024ull;
constexpr Guid kDocumentId{{0x75, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kTargetId{{0x75, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kMissingTargetId{{0x75, 0, 0, 3, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 3}};
constexpr std::array<std::uint8_t, 16> kPixels{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 4> kMaskPixels{25, 75, 125, 200};

int gFailures = 0;
std::uint64_t gAssertions = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s document.owned-metadata-jobs.%s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

[[noreturn]] void SetupFailure(const char* label, std::int64_t error = ERROR_SUCCESS)
{
    std::printf("FAIL document.owned-metadata-jobs.setup.%s error=%lld\n", label, static_cast<long long>(error));
    std::fflush(stdout);
    std::_Exit(10);
}

void CheckWait(DWORD result, const char* label)
{
    if (result == WAIT_OBJECT_0)
        return;
    SetupFailure(label, result == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
}

void Signal(HANDLE event, const char* label)
{
    if (!SetEvent(event))
        SetupFailure(label, GetLastError());
}

struct MemoryProbe final
{
    MemorySnapshot snapshot{};
};

bool SampleMemory(void* opaque, MemorySnapshot& snapshot) noexcept
{
    snapshot = static_cast<MemoryProbe*>(opaque)->snapshot;
    return true;
}

void StartScheduler(JobScheduler& scheduler, MemoryProbe& probe)
{
    const std::int32_t initialized = scheduler.Initialize(2, {}, {SampleMemory, &probe});
    if (initialized != S_OK)
        SetupFailure("scheduler-initialize", static_cast<DWORD>(initialized));
    if (!scheduler.EnableDeferredRelease())
        SetupFailure("deferred-release-enable", ERROR_INVALID_STATE);
}

void Settle(JobScheduler& scheduler, const char* label)
{
    const SettleResult settled = SettleJobs(scheduler, 10000);
    if (settled != SettleResult::Settled)
        SetupFailure(label, static_cast<DWORD>(settled));
}

void UseHighHeadroom(MemoryProbe& probe) noexcept
{
    probe.snapshot = {kMemoryAdmissionHeadroomBytes + kSixteenGiB, kMemoryAdmissionHeadroomBytes + kSixteenGiB};
}

CanvasDocument MakeDocument(double opacity)
{
    const Raster imageBase = Raster::Make(2, 2, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
    const Raster maskBase = Raster::Make(2, 2, RasterFormat::Gray8, kMaskPixels.data(), kMaskPixels.size());
    const Raster image = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, imageBase, 0, 0, {}, 0);
    const Raster maskImage = Raster::MakeSparse(2, 2, RasterFormat::Gray8, maskBase, 0, 0, {}, 0);
    ImageLayer target = ImageLayer::Blank(L"Original", Vec2{2, 2});
    target.id = kTargetId;
    target.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Pixels"};
    target.mask = LayerMask{RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"Mask"}};
    target.opacity = opacity;
    target.blendMode = LayerBlendMode::Multiply;
    DocumentFixture fixture;
    fixture.id = kDocumentId;
    fixture.width = 2;
    fixture.height = 2;
    fixture.layers = {target};
    return std::move(fixture).Seal();
}
DocumentSession MakeSession(double opacity = 0.5)
{
    DocumentSession session;
    session.ReplaceDocument(MakeDocument(opacity), kTargetId);
    return session;
}

const ImageLayer* Target(const DocumentSession& session) noexcept
{
    if (!session.Document().has_value())
        return nullptr;
    for (const ImageLayer& layer : session.Document().value().layers)
        if (layer.id == kTargetId)
            return &layer;
    return nullptr;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct JobState final
{
    JobTicket ticket{};
    JobResult result{};
    std::optional<DocumentHistory::StateView> oldState;
    std::optional<DocumentHistory::StateView> publishedState;
    std::weak_ptr<SharedPublicationMemory> output;
    std::weak_ptr<SharedPublicationMemory> temporary;
    std::uint64_t descriptorBytes = 0;
    std::uint64_t outputBytes = 0;
    std::uint64_t temporaryBytes = 0;
    std::uint32_t captures = 0;
    std::uint32_t workerStages = 0;
    std::uint32_t terminalBindings = 0;
    bool completionSeen = false;
    bool resumedSameTicket = false;
    bool outputBoundToOriginal = false;
    bool temporaryBoundToOriginal = false;
    bool outputFitsDescriptor = false;
    bool applied = false;
    bool boundBeforeApply = false;
    bool joinedSawPreparedGraph = false;
    bool joinedOwnerLiveDuringCallback = false;
    bool producersJoined = false;
    bool ownerCaptureComplete = false;
    bool ownerContinuationRequested = false;
    JobTicket retirementTicket{};
    std::uint32_t retirementCallbacks = 0;
    bool retirementTicketRecorded = false;
    bool retirementSawUnboundOwners = false;
    bool boundInCompleted = false;
    bool boundInJoinedClose = false;
    bool captureRefusalRequested = false;
    bool captureRefused = false;
    bool heldPublicationAfterClose = false;
    bool gateBeforePrepare = false;
    bool gateBeforeCapture = false;
    std::shared_ptr<SharedPublicationMemory> heldTemporary;
    std::optional<PreparedLayerMetadata> heldPublication;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class OwnedMetadataJob final : public Job
{
  public:
    OwnedMetadataJob(DocumentSession& session, JobScheduler& scheduler, JobState& state, double opacity,
                     HANDLE entered = nullptr, HANDLE release = nullptr,
                     LayerMetadataKind kind = LayerMetadataKind::SetOpacity)
        : _session(session), _scheduler(scheduler), _state(state), _opacity(opacity), _entered(entered),
          _release(release), _kind(kind)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        ++_state.workerStages;
        if (!_started)
        {
            _temporary = SharedPublicationMemory::CreateForWork();
            _state.temporary = _temporary;
            _started = true;
            _state.producersJoined = true;
            _output = SharedPublicationMemory::CreateForWork();
            _state.output = _output;
            if (_state.gateBeforeCapture && _entered != nullptr)
            {
                Signal(_entered, "pre-capture-worker-entered-event");
                HANDLE handles[]{_release, static_cast<HANDLE>(context.shutdownEvent)};
                const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
                _state.producersJoined = true;
                if (waited == WAIT_OBJECT_0 + 1 || context.cancellation.IsCancelled())
                    return {JobStatus::Cancelled};
                if (waited != WAIT_OBJECT_0)
                    SetupFailure("pre-capture-release-gate", waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
            }
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
        }

        if (!_captured || !_input.has_value())
        {
            _state.producersJoined = true;
            return {JobStatus::Failed, E_UNEXPECTED};
        }

        if (_state.gateBeforePrepare && _entered != nullptr)
        {
            Signal(_entered, "partial-output-worker-entered-event");
            HANDLE handles[]{_release, static_cast<HANDLE>(context.shutdownEvent)};
            const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            _state.producersJoined = true;
            if (waited == WAIT_OBJECT_0 + 1 || context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                SetupFailure("partial-output-release-gate", waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
        }
        auto prepared = DocumentSession::PrepareLayerMetadataPublication(std::move(_input.value()),
                                                                         context.cancellation, {}, {}, _output);
        _input.reset();
        if (!prepared)
        {
            _state.producersJoined = true;
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
        _publication.emplace(std::move(prepared.value()));
        if (_entered != nullptr)
        {
            Signal(_entered, "worker-entered-event");
            HANDLE handles[]{_release, static_cast<HANDLE>(context.shutdownEvent)};
            const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            _state.producersJoined = true;
            if (waited == WAIT_OBJECT_0 + 1 || context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                SetupFailure("worker-release-gate", waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
        }
        _state.producersJoined = true;
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (!(_state.ticket == ticket))
            SetupFailure("completion-ticket-is-not-original", E_UNEXPECTED);
        if (result.status != JobStatus::Finished || _captured)
            return false;
        try
        {
            _state.oldState.emplace(_session.history.CaptureState());
            const Guid& requestedTarget = _state.captureRefusalRequested ? kMissingTargetId : kTargetId;
            auto input = IsLayerFlipMetadata(_kind)
                             ? _session.PrepareLayerMetadataInput(_kind, requestedTarget)
                             : _session.PreparePreservedLayerMetadataInput(_kind, requestedTarget, {}, _opacity);
            if (!input)
            {
                if (!_state.captureRefusalRequested)
                    return false;
                _state.captureRefused = true;
                return false; // No captured input: deliver the first joined completion directly.
            }
            _input.emplace(std::move(input.value()));
            _captured = true;
            _state.captures = 1;
            _state.ownerCaptureComplete = _scheduler.Resume(ticket);
            _state.resumedSameTicket = _state.ownerCaptureComplete && _state.ticket == ticket;
            _state.ownerContinuationRequested = _state.ownerCaptureComplete;
            return _state.ownerCaptureComplete;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        if (!(_state.ticket == ticket))
            SetupFailure("retirement-callback-ticket-is-not-original", E_UNEXPECTED);
        _state.retirementTicket = ticket;
        ++_state.retirementCallbacks;
        _state.retirementTicketRecorded = true;
        _state.retirementSawUnboundOwners = _state.terminalBindings == 0;
        return false;
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        _state.joinedSawPreparedGraph = _publication.has_value();
        if (&scheduler != &_scheduler || (!_output && !_temporary))
            return;
        _state.producersJoined = true;
        BindForRetirement(ticket);
        _state.boundInJoinedClose = _state.terminalBindings == 1;
        if (_publication)
        {
            _state.heldPublication.emplace(std::move(_publication.value()));
            _publication.reset();
            _state.heldPublicationAfterClose = _state.heldPublication.has_value();
        }
        _state.heldTemporary = _temporary;
        _state.joinedOwnerLiveDuringCallback = _state.outputBoundToOriginal || _state.temporaryBoundToOriginal;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.completionSeen = true;
        if (_started && _state.terminalBindings == 0)
        {
            if (!_state.retirementTicketRecorded)
                SetupFailure("completed-without-retirement-ticket", E_UNEXPECTED);
            if (!_state.producersJoined)
                SetupFailure("completed-before-useful-producers-joined", E_UNEXPECTED);
            BindForRetirement(_state.retirementTicket);
            _state.boundInCompleted = _state.terminalBindings == 1;
        }
        if (result.status != JobStatus::Finished || !_publication.has_value())
            return;
        _state.boundBeforeApply = _state.boundInCompleted && _state.terminalBindings == 1 && _state.producersJoined &&
                                  (_state.outputBoundToOriginal || _state.temporaryBoundToOriginal);
        _state.applied = _state.boundBeforeApply && _session.ApplyPreparedLayerMetadataIfCurrent(_publication.value());
    }

  private:
    void BindForRetirement(JobTicket ticket) noexcept
    {
        if (_state.terminalBindings != 0)
            return;
        if (!_temporary)
            SetupFailure("retirement-temporary-owner-missing", E_UNEXPECTED);
        const std::uint64_t localBytes = _output ? _output->LocalAllocationBytes() : 0;
        const std::uint64_t allowance = SharedPublicationMemory::OwnershipAllowance();
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (localBytes > maximum - allowance)
            SetupFailure("retirement-output-byte-overflow", E_OUTOFMEMORY);
        _state.outputBytes = localBytes == 0 ? 0 : localBytes + allowance;
        _state.outputFitsDescriptor =
            _state.outputBytes == 0 || (_state.outputBytes > 0 && _state.outputBytes < _state.descriptorBytes);
        if (!_state.outputFitsDescriptor)
            SetupFailure("retirement-output-exceeds-original-descriptor", E_INVALIDARG);

        std::int32_t bound = E_UNEXPECTED;
        if (_state.outputBytes == 0)
        {
            bound = _temporary->BindForRetirement(_scheduler, ticket);
            _state.temporaryBytes = _state.descriptorBytes;
        }
        else
        {
            bound = _output->BindPersistentOutputForRetirement(_scheduler, ticket, _state.outputBytes, *_temporary);
            _state.temporaryBytes = _state.descriptorBytes - _state.outputBytes;
        }
        if (bound != S_OK)
            SetupFailure("retirement-bind-original-ticket", static_cast<DWORD>(bound));

        _state.outputBoundToOriginal = _state.outputBytes != 0 && _output && _output->BoundTo(_scheduler, ticket) &&
                                       _output->Bytes() == _state.outputBytes;
        _state.temporaryBoundToOriginal =
            _temporary->BoundTo(_scheduler, ticket) && _temporary->Bytes() == _state.temporaryBytes;
        if (!_state.temporaryBoundToOriginal || (_state.outputBytes != 0 && !_state.outputBoundToOriginal) ||
            (_state.outputBytes == 0 && _state.outputBoundToOriginal))
            SetupFailure("retirement-bind-provenance", E_UNEXPECTED);
        _state.terminalBindings = 1;
    }

    DocumentSession& _session;
    JobScheduler& _scheduler;
    JobState& _state;
    const double _opacity;
    HANDLE _entered = nullptr;
    HANDLE _release = nullptr;
    std::shared_ptr<SharedPublicationMemory> _output;
    std::shared_ptr<SharedPublicationMemory> _temporary;
    std::optional<LayerMetadataInput> _input;
    std::optional<PreparedLayerMetadata> _publication;
    bool _started = false;
    bool _captured = false;
    const LayerMetadataKind _kind;
};
#pragma warning(pop)

std::uint64_t DescriptorBytes(const DocumentSession& session, LayerMetadataKind kind = LayerMetadataKind::SetOpacity)
{
    const std::uint64_t estimate = IsLayerFlipMetadata(kind) ? session.LayerMetadataMemoryBytes(kind)
                                                             : session.PreservedLayerMetadataMemoryBytes(kind);
    const std::uint64_t jobBytes = sizeof(OwnedMetadataJob) + 2 * sizeof(SharedPublicationMemory);
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (estimate == 0 || estimate > maximum - jobBytes)
        return 0;
    return estimate + jobBytes;
}

std::int32_t SubmitMetadata(JobScheduler& scheduler, DocumentSession& session, JobState& state, double opacity,
                            HANDLE entered = nullptr, HANDLE release = nullptr,
                            LayerMetadataKind kind = LayerMetadataKind::SetOpacity)
{
    state.descriptorBytes = DescriptorBytes(session, kind);
    if (state.descriptorBytes == 0)
        return E_INVALIDARG;
    std::unique_ptr<Job> job =
        std::make_unique<OwnedMetadataJob>(session, scheduler, state, opacity, entered, release, kind);
    JobOptions options;
    options.kind = JobClass::Commit;
    options.serialKey = 1;
    options.memoryBytes = state.descriptorBytes;
    options.reserveRetainedMemory = true;
    options.deferredRetirement = true;
    options.allowMemoryWaiting = false;
    return scheduler.Submit(job, options, state.ticket);
}
void WaitForOwnerContinuation(JobScheduler& scheduler, JobState& state, const char* label)
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    if (wake == nullptr)
        SetupFailure("scheduler-wake-handle", ERROR_INVALID_HANDLE);
    for (;;)
    {
        const MessagePumpResult messages = PumpMessages();
        if (messages.quit)
            SetupFailure("owner-continuation-message-quit", static_cast<DWORD>(messages.exitCode));
        (void)DrainCompletions(scheduler);
        if (state.ownerContinuationRequested)
            return;
        if (scheduler.IsIdle())
            SetupFailure("owner-continuation-idle-before-resume",
                         ERROR_NOT_READY); // No owner continuation can arrive from an idle scheduler.
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            SetupFailure(label, ERROR_TIMEOUT);
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        const DWORD waited = WaitForSingleObject(wake, remaining);
        if (waited != WAIT_OBJECT_0)
            SetupFailure(label, waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
    }
}

struct ObserverState final
{
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> lease;
    bool bound = false;
    bool completed = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class ObserverJob final : public Job
{
  public:
    ObserverJob(JobScheduler& scheduler, ObserverState& state) : _scheduler(scheduler), _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.lease = SharedPublicationMemory::CreateForWork();
        return {};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status != JobStatus::Finished || !_state.lease)
            SetupFailure("observer-job-terminal-state", E_UNEXPECTED);
        const std::int32_t bound = _state.lease->BindForRetirement(_scheduler, _state.ticket);
        if (bound != S_OK)
            SetupFailure("observer-job-retained-lease-bind", static_cast<DWORD>(bound));
        if (!_state.lease->BoundTo(_scheduler, _state.ticket) || _state.lease->Bytes() != 8192)
            SetupFailure("observer-job-retained-lease-provenance", E_UNEXPECTED);
        _state.bound = true;
        _state.completed = true;
    }

  private:
    JobScheduler& _scheduler;
    ObserverState& _state;
};
#pragma warning(pop)

void StartObserverLease(JobScheduler& scheduler, ObserverState& state)
{
    std::unique_ptr<Job> job = std::make_unique<ObserverJob>(scheduler, state);
    JobOptions options;
    options.kind = JobClass::Background;
    options.memoryBytes = 8192;
    options.reserveRetainedMemory = true;
    options.deferredRetirement = true;
    options.allowMemoryWaiting = false;
    const std::int32_t submitted = scheduler.Submit(job, options, state.ticket);
    if (submitted != S_OK || job)
        SetupFailure("observer-job-submit", static_cast<DWORD>(submitted));
    Settle(scheduler, "observer-job-settle");
}
void TestOneHundredTenOwnedEdits()
{
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    const CanvasDocument original = session.Document().value();
    const Raster& originalImage = original.layers[0].asset.value().image;
    const Raster& originalMask = original.layers[0].mask.value().asset.image;
    const void* const imageIdentity = originalImage.Identity();
    const void* const maskIdentity = originalMask.Identity();
    auto ownedStates = std::make_unique<std::array<JobState, kLiveEdits>>();
    auto& states = *ownedStates;
    std::uint64_t retainedOutputBytes = 0;

    Expect(!originalImage.IsMaterialized() && !originalMask.IsMaterialized(),
           "sparse-image-and-mask-fixture-start-unmaterialized");
    for (std::size_t index = 0; index < kLiveEdits; ++index)
    {
        const double requested = 0.5 + static_cast<double>(index + 1) / 222.0;
        const std::int32_t submitted = SubmitMetadata(scheduler, session, states[index], requested);
        Expect(states[index].descriptorBytes > 0, "each-edit-descriptor-precomputed-before-capture");
        Expect(submitted == S_OK, "each-edit-real-scheduler-submit-accepted");
        if (submitted != S_OK)
            SetupFailure("long-run-submit", static_cast<DWORD>(submitted));
        WaitForOwnerContinuation(scheduler, states[index], "long-run-owner-capture-deadline");
        Settle(scheduler, "long-run-settle");
        // Each publication has an independent retained reader, including the final output. The
        // pre-edit view remains separate for stale-authority checks and cannot retain its successor.
        states[index].publishedState.emplace(session.history.CaptureState());

        const int expectedUndo = (std::min)(static_cast<int>(index + 1), session.history.EntryLimit());
        Expect(states[index].captures == 1 && states[index].oldState.has_value(),
               "each-edit-owner-captures-one-real-session-state-and-input");
        Expect(states[index].ownerCaptureComplete && states[index].resumedSameTicket,
               "each-edit-owner-resumes-the-same-original-ticket-after-capture");
        Expect(states[index].workerStages == 2, "each-edit-runs-first-allocation-stage-and-resumed-producer-stage");
        Expect(states[index].terminalBindings == 1 && states[index].retirementCallbacks == 2 &&
                   states[index].retirementSawUnboundOwners && states[index].boundInCompleted &&
                   states[index].outputBoundToOriginal && states[index].temporaryBoundToOriginal,
               "each-edit-binds-output-and-temporary-to-original-ticket-once");
        Expect(states[index].boundBeforeApply, "each-edit-binds-retirement-before-document-session-apply");
        Expect(states[index].applied && states[index].completionSeen &&
                   states[index].result.status == JobStatus::Finished,
               "each-edit-completes-and-applies-real-prepared-metadata");
        Expect(states[index].oldState.has_value() && !session.history.IsCurrent(states[index].oldState.value()),
               "each-edit-captured-history-view-becomes-stale-after-publication");
        auto output = states[index].output.lock();
        Expect(output && output->BoundTo(scheduler, states[index].ticket) &&
                   output->Bytes() == states[index].outputBytes && states[index].outputBytes > 0 &&
                   states[index].outputBytes < states[index].descriptorBytes &&
                   states[index].outputBytes ==
                       output->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance(),
               "each-edit-output-bytes-equal-local-allocations-plus-ownership-allowance");
        Expect(states[index].temporaryBoundToOriginal &&
                   states[index].temporaryBytes == states[index].descriptorBytes - states[index].outputBytes,
               "each-edit-temporary-partition-retains-exact-descriptor-remainder");
        output.reset();

        const double expectedOpacity = 0.5 + static_cast<double>(index + 1) / 222.0;
        const ImageLayer* layer = Target(session);
        Expect(layer && layer->opacity == expectedOpacity,
               "each-edit-opacity-matches-independent-literal-step-formula");
        Expect(layer && layer->asset.value().image.Identity() == imageIdentity &&
                   layer->mask.value().asset.image.Identity() == maskIdentity &&
                   !layer->asset.value().image.IsMaterialized() && !layer->mask.value().asset.image.IsMaterialized(),
               "each-edit-preserves-lazy-image-and-mask-identities-without-materialization");
        Expect(session.history.UndoCount() == expectedUndo, "each-edit-history-count-obeys-existing-entry-limit");
        retainedOutputBytes += states[index].outputBytes;
        const bool temporaryExpired = states[index].temporary.expired();
        states[index].temporary.reset();
        Settle(scheduler, "temporary-weak-control-release");
        Expect(temporaryExpired && scheduler.ReservedMemoryBytes() == retainedOutputBytes,
               "each-edit-temporary-weak-reset-drains-to-output-only-charge");
    }
    auto redoView = session.history.CaptureState();
    const double latest = 0.5 + static_cast<double>(kLiveEdits) / 222.0;
    const double previous = 0.5 + static_cast<double>(kLiveEdits - 1) / 222.0;
    session.Undo();
    Expect(Target(session) && Target(session)->opacity == previous &&
               session.history.UndoCount() == session.history.EntryLimit() - 1 &&
               session.history.CaptureState().FutureCount() == 1,
           "capped-history-undo-restores-previous-opacity-and-one-redo");
    session.Redo();
    Expect(Target(session) && Target(session)->opacity == latest &&
               session.history.UndoCount() == session.history.EntryLimit() &&
               session.history.CaptureState().FutureCount() == 0,
           "capped-history-redo-restores-latest-opacity-and-full-undo-cap");
    Expect(redoView.Revision() == session.history.CurrentRevision() && !session.history.IsCurrent(redoView),
           "undo-redo-restores-revision-but-advances-history-epoch");
    bool distinctTickets = true;
    for (std::size_t left = 0; left < states.size(); ++left)
        for (std::size_t right = left + 1; right < states.size(); ++right)
            distinctTickets = distinctTickets && !(states[left].ticket == states[right].ticket);
    Expect(distinctTickets, "110-edits-use-110-distinct-original-ticket-generations");
    Expect(scheduler.ReservedMemoryBytes() == retainedOutputBytes,
           "all-output-charges-remain-after-each-temporary-partition-drains");

    redoView = {};
    session.ReplaceDocument(MakeDocument(0.25), kTargetId);
    Settle(scheduler, "captured-history-view-retention-settle");
    bool allOutputsAlive = true;
    for (const JobState& state : states)
        allOutputsAlive = allOutputsAlive && !state.output.expired();
    Expect(allOutputsAlive && scheduler.ReservedMemoryBytes() == retainedOutputBytes,
           "110-captured-old-state-handles-retain-every-output-across-document-replacement");
    for (JobState& state : states)
    {
        state.oldState.reset();
        state.publishedState.reset();
    }
    Settle(scheduler, "captured-history-release-settle");
    bool allOutputObjectsExpired = true;
    for (const JobState& state : states)
        allOutputObjectsExpired = allOutputObjectsExpired && state.output.expired();
    Expect(allOutputObjectsExpired && scheduler.ReservedMemoryBytes() == retainedOutputBytes,
           "expired-output-objects-still-charge-while-weak-control-readers-remain");
    for (JobState& state : states)
        state.output.reset();
    Settle(scheduler, "last-output-weak-control-release");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "resetting-final-weak-readers-returns-all-output-charge-to-zero");
    scheduler.Close();
}
void TestCaptureRefusalBeforeUsefulPublication()
{
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    const auto revision = session.history.CurrentRevision();
    const double originalOpacity = Target(session) ? Target(session)->opacity : 0.0;
    JobState state;
    state.captureRefusalRequested = true;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9);
    if (submitted != S_OK)
        SetupFailure("capture-refusal-submit", static_cast<DWORD>(submitted));
    Settle(scheduler, "capture-refusal-terminal-settle");
    Expect(state.captureRefused && state.captures == 0 && state.workerStages == 1 &&
               !state.ownerContinuationRequested && state.retirementCallbacks == 1 && state.retirementTicketRecorded &&
               state.retirementSawUnboundOwners && state.boundInCompleted,
           "real-session-input-refusal-reaches-terminal-completed-binding-after-defer-completion");
    Expect(state.completionSeen && state.result.status == JobStatus::Finished && state.captureRefused &&
               !state.applied && state.terminalBindings == 1 && state.outputBytes == 0 &&
               !state.outputBoundToOriginal && state.temporaryBoundToOriginal &&
               state.temporaryBytes == state.descriptorBytes,
           "capture-refusal-terminal-callback-binds-full-original-admission-to-temporary");
    const bool weakControlsExpired = state.output.expired() && state.temporary.expired();
    Expect(weakControlsExpired && scheduler.ReservedMemoryBytes() == state.descriptorBytes,
           "capture-refusal-expired-weak-controls-retain-full-ticket-charge");
    Expect(session.history.CurrentRevision() == revision && Target(session) &&
               Target(session)->opacity == originalOpacity && session.history.UndoCount() == 0,
           "capture-refusal-before-publication-preserves-document-and-history");
    state.output.reset();
    state.temporary.reset();
    Settle(scheduler, "capture-refusal-weak-control-release");
    Expect(scheduler.ReservedMemoryBytes() == 0, "capture-refusal-weak-control-release-returns-ticket-charge-to-zero");
    scheduler.Close();
}
void TestCancellationBeforeOwnerCapture()
{
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    const auto revision = session.history.CurrentRevision();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !release)
        SetupFailure("pre-capture-cancel-events", GetLastError());
    JobState state;
    state.gateBeforeCapture = true;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9, entered.get(), release.get());
    if (submitted != S_OK)
        SetupFailure("pre-capture-cancel-submit", static_cast<DWORD>(submitted));
    CheckWait(WaitForSingleObject(entered.get(), 10000), "pre-capture-cancel-worker-entered");
    Expect(state.captures == 0 && state.workerStages == 1 && !state.ownerContinuationRequested &&
               !state.output.expired() && !state.temporary.expired(),
           "pre-capture-cancel-gate-holds-first-worker-after-owners-before-capture");
    const bool accepted = scheduler.Cancel(state.ticket);
    Signal(release.get(), "pre-capture-cancel-release-event");
    Settle(scheduler, "pre-capture-cancel-terminal-settle");
    Expect(accepted && state.completionSeen && state.result.status == JobStatus::Cancelled && state.captures == 0 &&
               state.terminalBindings == 1 && state.boundInCompleted && !state.applied && state.outputBytes == 0 &&
               state.temporaryBytes == state.descriptorBytes && !state.outputBoundToOriginal &&
               state.temporaryBoundToOriginal,
           "pre-capture-cancel-terminal-callback-binds-full-admission-without-publication");
    Expect(session.history.CurrentRevision() == revision && Target(session) && Target(session)->opacity == 0.5 &&
               session.history.UndoCount() == 0,
           "pre-capture-cancel-preserves-document-revision-and-history");
    const bool weakControlsExpired = state.output.expired() && state.temporary.expired();
    Expect(weakControlsExpired && scheduler.ReservedMemoryBytes() == state.descriptorBytes,
           "pre-capture-cancel-expired-weak-controls-retain-full-ticket-charge");
    state.output.reset();
    state.temporary.reset();
    Settle(scheduler, "pre-capture-cancel-weak-control-release");
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "pre-capture-cancel-weak-control-release-returns-ticket-charge-to-zero");
    scheduler.Close();
}
void TestBelowHeadroomFailsBeforeWorkerOrCapture()
{
    MemoryProbe probe;
    probe.snapshot = {kMemoryAdmissionHeadroomBytes - 1, kMemoryAdmissionHeadroomBytes - 1};
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    const auto revision = session.history.CurrentRevision();
    const auto document = session.Document().value();
    JobState state;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9);
    Expect(submitted == S_OK, "below-headroom-submit-accepts-descriptor-before-worker-admission");
    Settle(scheduler, "below-headroom-terminal-callback");
    Expect(state.completionSeen && state.result.status == JobStatus::Failed && state.result.error == E_OUTOFMEMORY &&
               state.workerStages == 0 && state.captures == 0 && state.output.expired() && state.temporary.expired(),
           "below-headroom-worker-admission-fails-before-run-capture-or-output");
    Expect(session.Document().value() == document && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == 0 && scheduler.ReservedMemoryBytes() == 0,
           "below-headroom-failure-preserves-document-revision-history-and-budget");
    scheduler.Close();
}
void TestEventGatedCancelPreservesRedoAndRevision()
{
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    JobState seed;
    const std::int32_t seedSubmitted = SubmitMetadata(scheduler, session, seed, 0.75);
    if (seedSubmitted != S_OK)
        SetupFailure("cancel-redo-seed-submit", static_cast<DWORD>(seedSubmitted));
    WaitForOwnerContinuation(scheduler, seed, "cancel-redo-seed-capture");
    Settle(scheduler, "cancel-redo-seed-settle");
    const bool seedTemporaryExpired = seed.temporary.expired();
    seed.temporary.reset();
    Settle(scheduler, "cancel-seed-temporary-weak-release");
    session.Undo();
    const auto revision = session.history.CurrentRevision();
    const auto redoCount = session.history.CaptureState().FutureCount();
    const std::uint64_t baselineCharge = scheduler.ReservedMemoryBytes();
    Expect(seedTemporaryExpired && baselineCharge > 0 && !seed.output.expired(),
           "seed-redo-output-remains-charged-after-temporary-weak-reader-reset");

    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !release)
        SetupFailure("cancel-events", GetLastError());
    JobState cancelled;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, cancelled, 0.9, entered.get(), release.get());
    if (submitted != S_OK)
        SetupFailure("cancel-job-submit", static_cast<DWORD>(submitted));
    WaitForOwnerContinuation(scheduler, cancelled, "cancel-owner-capture-deadline");
    CheckWait(WaitForSingleObject(entered.get(), 10000), "cancel-worker-entered");
    const bool accepted = scheduler.Cancel(cancelled.ticket);
    Signal(release.get(), "cancel-release-event");
    Settle(scheduler, "cancel-settle");
    Expect(accepted && cancelled.completionSeen && cancelled.result.status == JobStatus::Cancelled &&
               !cancelled.applied,
           "event-gated-cancel-reaches-terminal-callback-without-apply");
    Expect(session.history.CurrentRevision() == revision && session.history.CaptureState().FutureCount() == redoCount &&
               session.history.UndoCount() == 0 && Target(session) && Target(session)->opacity == 0.5,
           "event-gated-cancel-preserves-redo-revision-history-and-visible-opacity");
    const bool cancelledTempExpired = cancelled.temporary.expired();
    cancelled.oldState.reset();
    cancelled.output.reset();
    cancelled.temporary.reset();
    Settle(scheduler, "cancelled-owner-weak-release");
    Expect(cancelledTempExpired && cancelled.terminalBindings == 1 && cancelled.outputBoundToOriginal &&
               cancelled.temporaryBoundToOriginal && cancelled.boundInCompleted &&
               scheduler.ReservedMemoryBytes() == baselineCharge,
           "cancelled-ticket-releases-its-partitions-back-to-seeded-redo-baseline");
    session.ReplaceDocument(MakeDocument(0.5), kTargetId);
    seed.oldState.reset();
    seed.output.reset();
    Settle(scheduler, "cancel-seed-history-and-weak-release");
    Expect(scheduler.ReservedMemoryBytes() == 0, "dropping-seed-redo-source-and-captured-weak-views-returns-zero");
    scheduler.Close();
}
void TestStaleAfterMovingToAnotherDocument()
{
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    auto session = MakeSession();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !release)
        SetupFailure("stale-events", GetLastError());
    JobState state;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9, entered.get(), release.get());
    if (submitted != S_OK)
        SetupFailure("stale-job-submit", static_cast<DWORD>(submitted));
    WaitForOwnerContinuation(scheduler, state, "stale-owner-capture-deadline");
    CheckWait(WaitForSingleObject(entered.get(), 10000), "stale-worker-entered");
    session.ReplaceDocument(MakeDocument(0.25), kTargetId);
    const CanvasDocument current = session.Document().value();
    Signal(release.get(), "stale-release-event");
    Settle(scheduler, "stale-settle");
    Expect(state.completionSeen && state.result.status == JobStatus::Finished && !state.applied,
           "prepared-result-for-prior-document-is-rejected-as-stale");
    Expect(session.Document().value() == current && Target(session) && Target(session)->opacity == 0.25 &&
               session.history.UndoCount() == 0,
           "stale-completion-preserves-current-replacement-document");
    Expect(state.terminalBindings == 1 && state.outputBoundToOriginal && state.temporaryBoundToOriginal &&
               state.boundBeforeApply && state.boundInCompleted,
           "stale-prepared-graph-binds-original-ticket-before-owner-rejection");
    state.oldState.reset();
    state.output.reset();
    state.temporary.reset();
    Settle(scheduler, "stale-graph-and-weak-release");
    Expect(scheduler.ReservedMemoryBytes() == 0, "stale-output-and-temporary-weak-owners-release-after-old-state-drop");
    scheduler.Close();
}
void TestJoinedCloseRetainsPreparedGraphUntilBinding(LayerMetadataKind kind = LayerMetadataKind::SetOpacity)
{
    const auto expect = [kind](bool condition, const char* label)
    {
        const std::string prefix = kind == LayerMetadataKind::FlipHorizontal ? "flip-horizontal-close-"
                                   : kind == LayerMetadataKind::FlipVertical ? "flip-vertical-close-"
                                                                             : "";
        Expect(condition, (prefix + label).c_str());
    };
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    ObserverState observer;
    StartObserverLease(scheduler, observer);
    expect(observer.bound && observer.completed && observer.lease && observer.lease->Bytes() == 8192 &&
               observer.lease->ResidentBytes() == 8192,
           "real-observer-job-retains-8192-byte-live-endpoint-before-close");

    auto session = MakeSession();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !release)
        SetupFailure("joined-close-events", GetLastError());
    JobState state;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9, entered.get(), release.get(), kind);
    if (submitted != S_OK)
        SetupFailure("joined-close-submit", static_cast<DWORD>(submitted));
    WaitForOwnerContinuation(scheduler, state, "joined-close-owner-capture-deadline");
    CheckWait(WaitForSingleObject(entered.get(), 10000), "joined-close-prepared-worker-entered");
    expect(state.captures == 1 && state.workerStages == 2 && state.ownerCaptureComplete,
           "joined-close-case-reaches-real-captured-prepared-worker-stage");
    scheduler.Close();
    expect(state.joinedSawPreparedGraph && state.heldPublicationAfterClose && state.terminalBindings == 1 &&
               state.outputBoundToOriginal && state.temporaryBoundToOriginal && state.joinedOwnerLiveDuringCallback &&
               state.boundInJoinedClose,
           "joined-close-binds-original-ticket-and-moves-prepared-graph-to-held-state");
    expect(observer.lease->ResidentBytes() == 8192 + state.descriptorBytes,
           "observer-lease-proves-closed-endpoint-retains-observer-plus-original-admission");
    expect(!state.completionSeen && !state.applied,
           "joined-close-drops-owner-completion-and-never-applies-prepared-metadata");

    state.heldPublication.reset();
    state.heldTemporary.reset();
    state.oldState.reset();
    expect(state.output.expired() && state.temporary.expired() &&
               observer.lease->ResidentBytes() == 8192 + state.descriptorBytes,
           "expired-joined-graph-weak-controls-keep-original-endpoint-charge");
    state.output.reset();
    state.temporary.reset();
    expect(observer.lease->ResidentBytes() == 8192,
           "last-joined-graph-weak-release-leaves-observer-only-endpoint-charge");
    observer.lease.reset();
}
void TestJoinedCloseWithZeroLocalOutput(LayerMetadataKind kind = LayerMetadataKind::SetOpacity)
{
    const auto expect = [kind](bool condition, const char* label)
    {
        const std::string prefix = kind == LayerMetadataKind::FlipHorizontal ? "flip-horizontal-close-"
                                   : kind == LayerMetadataKind::FlipVertical ? "flip-vertical-close-"
                                                                             : "";
        Expect(condition, (prefix + label).c_str());
    };
    MemoryProbe probe;
    UseHighHeadroom(probe);
    JobScheduler scheduler;
    StartScheduler(scheduler, probe);
    ObserverState observer;
    StartObserverLease(scheduler, observer);
    auto session = MakeSession();
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!entered || !release)
        SetupFailure("partial-close-events", GetLastError());
    JobState state;
    state.gateBeforePrepare = true;
    const std::int32_t submitted = SubmitMetadata(scheduler, session, state, 0.9, entered.get(), release.get(), kind);
    if (submitted != S_OK)
        SetupFailure("partial-close-submit", static_cast<DWORD>(submitted));
    WaitForOwnerContinuation(scheduler, state, "partial-close-owner-capture-deadline");
    CheckWait(WaitForSingleObject(entered.get(), 10000), "partial-close-worker-entered");
    scheduler.Close();
    expect(!state.heldPublicationAfterClose && state.terminalBindings == 1 && state.outputBytes == 0 &&
               !state.outputBoundToOriginal && state.temporaryBoundToOriginal &&
               state.temporaryBytes == state.descriptorBytes && state.boundInJoinedClose,
           "joined-close-zero-local-output-keeps-full-original-admission-in-temporary");
    expect(observer.lease->ResidentBytes() == 8192 + state.descriptorBytes,
           "partial-joined-close-is-retained-by-live-observer-endpoint");
    state.heldTemporary.reset();
    state.oldState.reset();
    expect(state.output.expired() && state.temporary.expired() &&
               observer.lease->ResidentBytes() == 8192 + state.descriptorBytes,
           "partial-graph-weak-controls-retain-charge-after-strong-owner-release");
    state.output.reset();
    state.temporary.reset();
    expect(observer.lease->ResidentBytes() == 8192, "partial-graph-weak-release-leaves-observer-only-charge");
    observer.lease.reset();
}
} // namespace

int RunOwnedMetadataJobsR4Tests()
{
    gFailures = 0;
    gAssertions = 0;
    TestOneHundredTenOwnedEdits();
    TestCaptureRefusalBeforeUsefulPublication();
    TestCancellationBeforeOwnerCapture();
    TestBelowHeadroomFailsBeforeWorkerOrCapture();
    TestEventGatedCancelPreservesRedoAndRevision();
    TestStaleAfterMovingToAnotherDocument();
    TestJoinedCloseRetainsPreparedGraphUntilBinding();
    TestJoinedCloseWithZeroLocalOutput();
    if (gAssertions != kExpectedAssertions)
        SetupFailure("authored-assertion-roster-mismatch", static_cast<DWORD>(gAssertions));
    std::printf("%s document.owned-metadata-jobs-r4.summary assertions=%llu failures=%d\n",
                gFailures == 0 ? "PASS" : "FAIL", static_cast<unsigned long long>(gAssertions), gFailures);
    const auto beforeFlips = gAssertions;
    for (const auto kind : {LayerMetadataKind::FlipHorizontal, LayerMetadataKind::FlipVertical})
    {
        TestJoinedCloseRetainsPreparedGraphUntilBinding(kind);
        TestJoinedCloseWithZeroLocalOutput(kind);
    }
    if (gAssertions - beforeFlips != 22)
        SetupFailure("flip-close-authored-assertion-roster-mismatch", static_cast<DWORD>(gAssertions - beforeFlips));
    std::printf("%s document.owned-metadata-flip-close.summary assertions=22 failures=%d\n",
                gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}

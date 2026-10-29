#include "Composition.Tests.OriginalDisplayPublication.h"

#include "BrushStroke.h"
#include "DocumentDraft.h"
#include "OriginalDisplayPublication.h"
#include "PixelMoveClosureWork.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
constexpr std::uint64_t kBytes = 512ull * 1024 * 1024;
int failures = 0;
[[noreturn]] void BoundaryFailure(int line) noexcept
{
    std::printf("FAIL original-display-pixel.fixture-boundary line=%d thread=%lu\n", line, GetCurrentThreadId());
    std::fflush(stdout);
    std::terminate();
}

enum class Scenario
{
    Move,
    Duplicate,
    CpuRefused,
    GpuRefused,
    CancelAfterRecord,
    WrongRowEpoch
};

void Expect(const char* name, bool condition) noexcept
{
    std::printf("%s original-display-pixel.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    failures += condition ? 0 : 1;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {8ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024};
    return true;
}

DocumentSelection Rectangle(double x)
{
    return {false, SelectionLoops{{Vec2{x, 0}, Vec2{x + 1, 0}, Vec2{x + 1, 1}, Vec2{x, 1}}}};
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class Client final : public CommittedDisplayClient
{
  public:
    DocumentSession session;
    GpuWorkQueue& queue;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    bool software;
    RecordedDisplayGraph* graph = nullptr;
    std::shared_ptr<const PreparedLayerRows> rows;
    unsigned publications = 0;
    bool finalCallbackSawRows = false, rowMetadataReady = false;

    Client(GpuWorkQueue& q, ID3D11Device* d, ID3D11DeviceContext* c, bool s) noexcept
        : queue(q), device(d), context(c), software(s)
    {
    }
    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& value) noexcept override
    {
        const auto authority = session.CaptureDocumentReadAuthority();
        if (!authority)
            return false;
        value = {{authority.value(), queue.DeviceGeneration()}, &session.Document().value(), device, context, software};
        return true;
    }
    void CommittedDisplayPublishedOwner(RecordedDisplayGraph* value) noexcept override
    {
        graph = value;
        if (value)
        {
            ++publications;
            if (publications == 2)
                finalCallbackSawRows = rowMetadataReady && rows &&
                                       rows->Source().layers.Identity() == session.Document()->layers.Identity();
        }
    }
    void CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT error) noexcept override
    {
        std::printf("FAIL original-display-pixel.controller-error=0x%08x\n", static_cast<unsigned>(error));
        ++failures;
    }
    bool Initialize()
    {
        CanvasDocument source;
        source.width = 3;
        source.height = 1;
        source.selection = Rectangle(0);
        constexpr std::array<std::uint8_t, 12> pixels{0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0};
        auto draft = CanvasDocumentDraft::Rebuild(source, 1);
        auto layer = ImageLayer::Blank(L"Original pixel publication", {3, 1});
        const auto id = layer.id;
        layer.transform.sampling = LayerSampling::Nearest;
        layer.asset =
            RasterAsset{Raster::Make(3, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
        if (!draft || !layer.asset->image.HasStorage() || !draft->Append(std::move(layer)))
            return false;
        auto sealed = draft->Seal();
        if (!sealed)
            return false;
        session.ReplaceDocument(std::move(sealed.value()), id);
        session.SetSelection(Rectangle(2), L"Fixture redo");
        session.Undo();
        return session.Document() && session.Document()->selection == std::optional{Rectangle(0)} &&
               session.history.CanRedo();
    }
};
#pragma warning(pop)

template <typename Predicate> bool Until(JobScheduler& jobs, OwnerWorkMultiplexer& routes, Predicate predicate) noexcept
{
    const auto start = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(jobs);
        if (predicate())
            return true;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const auto elapsed = GetTickCount64() - start;
        if (elapsed >= 10000 || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()),
                                                    static_cast<DWORD>(10000 - elapsed)) != WAIT_OBJECT_0)
            return false;
    }
}

class Parent;
struct PixelPump final : OwnerWorkPump
{
    Parent* parent = nullptr;
    bool HasPending() const noexcept override
    {
        return parent != nullptr;
    }
    bool HasReady() const noexcept override;
    void Pump() noexcept override;
};

struct Observation final
{
    JobTicket original;
    bool finished = false, changed = false, transportRetired = false, graphRetired = false, destroyed = false;
    JobResult outcome;
    unsigned stages = 0;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class Parent final : public Job
{
    enum class Stage
    {
        Allocate,
        Open,
        Prepare,
        Record,
        AwaitTransport,
        RetireTransport,
        Refresh,
        Publish,
        RetireGraph,
        Destroy
    };
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    CommittedDisplayController& display;
    Client& client;
    struct PixelPump& pump;
    Observation& observation;
    const Scenario scenario;
    const std::uint32_t ownerThread = GetCurrentThreadId();
    const std::uint64_t beforeGeneration = client.session.Generation();
    const Guid beforeRevision = client.session.history.CurrentRevision();
    const std::uint64_t documentSerial = client.session.DocumentSerial();
    const void* beforeLayers = client.session.Document()->layers.Identity();
    Stage stage = Stage::Allocate;
    std::unique_ptr<OriginalDisplayPublication> publication;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<SharedPublicationMemory> retirementMemory;
    std::optional<PixelMoveOpeningInput> openingInput;
    std::optional<PreparedPixelMoveOpening> opening;
    std::optional<PixelMoveClosureInput> closeInput;
    std::optional<PreparedPixelMoveClosure> closure;
    DocumentHistory::ResidencyPermit residency;
    JobResult terminal;

    bool Unchanged() const noexcept
    {
        return client.session.Generation() == beforeGeneration &&
               client.session.history.CurrentRevision() == beforeRevision &&
               client.session.Document()->layers.Identity() == beforeLayers && client.session.history.CanRedo() &&
               client.publications == 1 && !client.rows;
    }
    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        stage = next;
        if (!jobs.Resume(observation.original, lane))
            BoundaryFailure(__LINE__);
    }
    bool BeginCleanup() noexcept
    {
        residency.Reset();
        if (!observation.changed && opening &&
            client.session.IsPreparedDeferredPixelMoveGestureCurrent(opening.value()))
            if (!client.session.AbortPreparedDeferredPixelMoveIfCurrent(opening.value()))
                BoundaryFailure(__LINE__);
        // The original record retains its whole reservation through cleanup. The small accounting
        // alias transfers only in Completed, after every useful worker and graph destructor joined.
        if (!publication->DetachForRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireGraph);
        return true;
    }

  public:
    Parent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, CommittedDisplayController& d, Client& cl,
           struct PixelPump& p, Observation& o, Scenario s) noexcept
        : jobs(j), queue(q), commands(c), display(d), client(cl), pump(p), observation(o), scenario(s)
    {
    }

    JobResult Run(JobContext& context) override
    {
        ++observation.stages;
        if (GetCurrentThreadId() == ownerThread ||
            (stage != Stage::RetireGraph && stage != Stage::Destroy && context.reservedMemoryBytes != kBytes))
            return {JobStatus::Failed, E_UNEXPECTED};
        if (stage == Stage::Allocate)
        {
            const OriginalDisplayPublication::Bounds bounds{scenario == Scenario::CpuRefused ? 1 : 384ull * 1024 * 1024,
                                                            scenario == Scenario::GpuRefused ? 1
                                                                                             : 256ull * 1024 * 1024};
            publication = std::make_unique<OriginalDisplayPublication>(ownerThread, bounds);
            memory = SharedPublicationMemory::CreateForWork(kBytes);
        }
        else if (stage == Stage::Open)
        {
            auto result =
                DocumentSession::PrepareDeferredPixelMoveOpening(std::move(openingInput.value()), context.cancellation);
            openingInput.reset();
            if (!result.prepared || result.status != PixelMoveLiftStatus::Ready)
                return {JobStatus::Failed, E_FAIL};
            opening.emplace(std::move(result.prepared.value()));
        }
        else if (stage == Stage::Prepare)
        {
            auto result = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(closeInput.value()),
                                                                           context.cancellation, memory);
            closeInput.reset();
            if (!result || !result->FinalDocument())
                return {JobStatus::Failed, E_FAIL};
            closure.emplace(std::move(result.value()));
            OriginalDisplayPublication::RowInput rows;
            rows.documentSerial = documentSerial + (scenario == Scenario::WrongRowEpoch ? 1u : 0u); // Captured scalar.
            rows.firstItemId = PreparedLayerRows::FirstItemIdentity;
            rows.hiddenPattern = L"{0} (hidden)";
            return publication->PrepareForWork(*closure->FinalDocument(), std::move(rows), context.cancellation);
        }
        else if (stage == Stage::Record)
            return publication->RecordForWork(context.cancellation);
        else if (stage == Stage::Refresh)
            return closure->RefreshHistoryResidency(context.cancellation) ? JobResult{}
                                                                          : JobResult{JobStatus::Failed, E_ABORT};
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (stage == Stage::Record)
            publication->FinishRecordingForWork(result);
        else if (stage == Stage::RetireTransport)
        {
            if (!publication->RetireTransportForWork())
                BoundaryFailure(__LINE__);
            observation.transportRetired = GetCurrentThreadId() != ownerThread;
        }
        else if (stage == Stage::RetireGraph)
        {
            if (!publication->RetireGraphForWork())
                BoundaryFailure(__LINE__);
            opening.reset();
            closure.reset();
            openingInput.reset();
            closeInput.reset();
            memory.reset();
            observation.graphRetired = GetCurrentThreadId() != ownerThread;
        }
        else if (stage == Stage::Destroy)
        {
            publication.reset();
            observation.destroyed = GetCurrentThreadId() != ownerThread;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != observation.original)
            BoundaryFailure(__LINE__);
        if (stage == Stage::Record)
        {
            if (!publication->RecordingJoinedOwner())
                BoundaryFailure(__LINE__);
            if (result.status != JobStatus::Finished)
                terminal = result;
            if (scenario == Scenario::CancelAfterRecord)
            {
                terminal = {JobStatus::Cancelled};
                publication->StopOwner();
                if (!jobs.Cancel(ticket))
                    BoundaryFailure(__LINE__);
                Expect("cancellation-keeps-original-memory-before-transport-retirement",
                       Unchanged() && jobs.ReservedMemoryBytes() >= kBytes && !memory->IsBound());
            }
            stage = Stage::AwaitTransport;
            jobs.NotifyOwnerWork();
            return true;
        }
        if (stage == Stage::RetireGraph)
        {
            if (!publication->FinishRetirementOwner())
                BoundaryFailure(__LINE__);
            Resume(Stage::Destroy);
            return true;
        }
        if (stage == Stage::Destroy)
            return false;
        if (result.status != JobStatus::Finished)
        {
            terminal = result;
            Expect(scenario == Scenario::CancelAfterRecord
                       ? "cancelled-recording-preserves-committed-bytes-history-redo-rows-and-graph"
                       : "refused-final-preparation-preserves-committed-bytes-history-redo-rows-and-graph",
                   Unchanged());
            if (scenario == Scenario::WrongRowEpoch)
                Expect("wrong-row-document-epoch-refuses-before-recording-or-row-publication",
                       result.error == E_INVALIDARG && !publication->HasRecorderOwner() && Unchanged());
            return BeginCleanup();
        }
        if (stage == Stage::RetireTransport)
            stage = Stage::Publish;
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (stage == Stage::Allocate)
        {
            CommittedDisplayDescriptor descriptor;
            if (!client.DescribeCommittedDisplayOwner(descriptor) ||
                FAILED(publication->CaptureOwner(jobs, ticket, queue, commands, display, descriptor)))
                BoundaryFailure(__LINE__);
            auto input = client.session.PrepareDeferredPixelMoveOpeningInput(scenario == Scenario::Duplicate);
            if (!input)
                BoundaryFailure(__LINE__);
            retirementMemory = memory;
            openingInput.emplace(std::move(input.value()));
            Expect("original-admission-captures-source-only-in-its-joined-callback", Unchanged());
            Resume(Stage::Open);
            return true;
        }
        if (stage == Stage::Open)
        {
            if (!client.session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(opening.value()) ||
                !client.session.MovePreparedDeferredPixelMoveIfCurrent(opening.value(), 1, 0))
                BoundaryFailure(__LINE__);
            auto input = client.session.PrepareDeferredPixelMoveClosureInput(opening.value());
            if (!input)
                BoundaryFailure(__LINE__);
            closeInput.emplace(std::move(input.value()));
            Expect("pending-opening-and-offset-preserve-committed-source-and-redo", Unchanged());
            Resume(Stage::Prepare);
            return true;
        }
        if (stage == Stage::Prepare)
        {
            Expect("complete-final-document-and-rows-prepare-before-any-CPU-publication", Unchanged());
            const auto admitted = publication->AdmitRecordingOwner();
            if (FAILED(admitted))
                BoundaryFailure(__LINE__); // The two selected bound failures occur before recording admission.
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        if (stage == Stage::Publish || stage == Stage::Refresh)
        {
            Expect("real-recording-and-transport-retire-before-CPU-history-rows-publication",
                   Unchanged() && observation.transportRetired);
            const auto decision = closure->TryAcquireHistoryResidency(residency);
            if (decision == DocumentHistory::ResidencyValidation::Refresh)
            {
                residency.Reset();
                Resume(Stage::Refresh);
                return true;
            }
            if (decision != DocumentHistory::ResidencyValidation::Ready ||
                FAILED(memory->BindForPublication(jobs, ticket)) ||
                FAILED(publication->PreparePublicationOwner(memory, *closure->FinalDocument())))
                BoundaryFailure(__LINE__);
            Expect("exact-original-bound-memory-and-ready-private-GPU-do-not-publish-CPU-early",
                   Unchanged() && memory->BoundTo(jobs, ticket));
            if (!client.session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(closure.value()) ||
                !publication->AdoptOwner(
                    client.rows,
                    [](void* raw) noexcept
                    {
                        auto& value = *static_cast<Client*>(raw);
                        value.rowMetadataReady = value.rows && value.rows->Source().layers.Identity() ==
                                                                   value.session.Document()->layers.Identity();
                    },
                    &client))
                BoundaryFailure(__LINE__);
            Expect("owner-row-metadata-is-installed-before-graph-callback",
                   client.rowMetadataReady && client.finalCallbackSawRows);
            residency.Reset();
            observation.changed = true;
            Expect("one-checked-CPU-history-swap-adopts-prepared-rows-before-graph-callback",
                   client.session.Generation() == beforeGeneration + 1 && !client.session.history.CanRedo() &&
                       client.session.history.CanUndo() && client.publications == 2 && client.finalCallbackSawRows &&
                       client.rows && client.rows->HasMemoryLease() && publication->AdoptedOwner());
            return BeginCleanup();
        }
        return false;
    }
    bool Ready() const noexcept
    {
        return stage == Stage::AwaitTransport && publication->TransportReadyOwner();
    }
    void Advance() noexcept
    {
        if (!Ready() || !publication->AuthorizeTransportRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireTransport);
    }
    void Completed(JobResult result) noexcept override
    {
        if (stage != Stage::Destroy || publication)
            BoundaryFailure(__LINE__);
        if (!retirementMemory ||
            (!retirementMemory->IsBound() && FAILED(retirementMemory->BindForRetirement(jobs, observation.original))))
            BoundaryFailure(__LINE__);
        observation.outcome = terminal.status == JobStatus::Finished ? result : terminal;
        observation.finished = true;
        pump.parent = nullptr;
    }
};
#pragma warning(pop)

bool PixelPump::HasReady() const noexcept
{
    return parent && parent->Ready();
}
void PixelPump::Pump() noexcept
{
    if (parent)
        parent->Advance();
}

enum class ClosePoint
{
    Allocated,
    Captured,
    Prepared,
    RecorderAdmitted,
    ProducerJoined,
    TransportRetired,
    PrivatePublished,
    AllocateWorker,
    PrepareWorker,
    RecordWorker,
    RetireTransportWorker
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct ClosePayload final
{
    std::unique_ptr<OriginalDisplayPublication> publication;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::optional<CanvasDocument> input, final;
    bool captured = false;
    std::uint64_t documentSerial = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CloseState final
{
    std::unique_ptr<ClosePayload> retired;
    GpuResidencyReservation gpu;
    std::atomic<bool> ready{false}, heldProducer{false}, shutdownObserved{false};
    bool originalJoined = false, wrapperDestroyed = false;
    bool workerRetired = false, chargeHeldOnWorker = false;
    std::uint32_t owner = GetCurrentThreadId();

    void RetireForWork() noexcept
    {
        if (GetCurrentThreadId() == owner || !originalJoined || !wrapperDestroyed || !retired)
            BoundaryFailure(__LINE__);
        chargeHeldOnWorker = retired->memory && retired->memory->IsBound() && retired->memory->Bytes() == kBytes;
        if (retired->captured && !retired->publication->RetireJoinedShutdownForWork())
            BoundaryFailure(__LINE__);
        retired.reset();
        workerRetired = true;
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class CloseParent final : public Job, public OwnerWorkPump
{
    enum class Stage
    {
        Allocate,
        Prepare,
        Record,
        AwaitTransport,
        RetireTransport,
        Publish
    };
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    CommittedDisplayController& display;
    Client& client;
    CloseState& state;
    const ClosePoint point;
    OwnerWorkMultiplexer& routes;
    Stage stage = Stage::Allocate;
    JobTicket ticket;
    std::unique_ptr<ClosePayload> payload;

    bool Hold(ClosePoint boundary) noexcept
    {
        if (boundary != point)
            return false;
        state.ready = true;
        return true;
    }
    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        stage = next;
        if (!jobs.Resume(ticket, lane))
            BoundaryFailure(__LINE__);
    }

  public:
    CloseParent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, CommittedDisplayController& d, Client& cl,
                CloseState& s, ClosePoint p, OwnerWorkMultiplexer& mux) noexcept
        : jobs(j), queue(q), commands(c), display(d), client(cl), state(s), point(p), routes(mux)
    {
    }
    ~CloseParent() override
    {
        if (payload)
            BoundaryFailure(__LINE__);
        state.wrapperDestroyed = true;
    }
    JobResult Run(JobContext& context) override
    {
        if (GetCurrentThreadId() == state.owner || context.reservedMemoryBytes != kBytes)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (stage == Stage::Allocate)
        {
            payload = std::make_unique<ClosePayload>();
            payload->publication = std::make_unique<OriginalDisplayPublication>(
                state.owner, OriginalDisplayPublication::Bounds{384ull * 1024 * 1024, 256ull * 1024 * 1024});
            payload->memory = SharedPublicationMemory::CreateForWork(kBytes);
        }
        else if (stage == Stage::Prepare)
        {
            auto draft = CanvasDocumentDraft::Rebuild(*payload->input, 1);
            auto layer = (*payload->input).layers[0];
            constexpr std::array<std::uint8_t, 12> pixels{0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0};
            layer.asset->image = Raster::Make(3, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
            if (!draft || !layer.asset->image.HasStorage() || !draft->Append(std::move(layer)))
                return {JobStatus::Failed, E_FAIL};
            payload->final = draft->Seal();
            if (!payload->final)
                return {JobStatus::Failed, E_FAIL};
            OriginalDisplayPublication::RowInput rows;
            rows.documentSerial = payload->documentSerial; // Exact original owner-captured row epoch.
            rows.firstItemId = PreparedLayerRows::FirstItemIdentity;
            rows.hiddenPattern = L"{0} (hidden)";
            return payload->publication->PrepareForWork(*payload->final, std::move(rows), context.cancellation);
        }
        else if (stage == Stage::Record)
            return payload->publication->RecordForWork(context.cancellation);
        return {};
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        const bool hold = (stage == Stage::Allocate && point == ClosePoint::AllocateWorker) ||
                          (stage == Stage::Prepare && point == ClosePoint::PrepareWorker) ||
                          (stage == Stage::Record && point == ClosePoint::RecordWorker) ||
                          (stage == Stage::RetireTransport && point == ClosePoint::RetireTransportWorker);
        if (hold)
        {
            state.heldProducer.store(true, std::memory_order_release);
            state.ready.store(true, std::memory_order_release);
            jobs.NotifyOwnerWork();
            // Deterministic test gate: Close signals this actual scheduler event, then joins this producer.
            // No owner wait, forged completion, Sleep or performance assertion.
            if (WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
                BoundaryFailure(__LINE__);
            state.shutdownObserved.store(true, std::memory_order_release);
        }
        if (stage == Stage::Record)
            payload->publication->FinishRecordingForWork(result);
        else if (stage == Stage::RetireTransport && !payload->publication->RetireTransportForWork())
            BoundaryFailure(__LINE__);
    }
    bool DeferRetirement(JobResult result, JobTicket original) noexcept override
    {
        if (result.status != JobStatus::Finished || (ticket.generation && ticket != original))
            BoundaryFailure(__LINE__);
        if (stage == Stage::Record)
        {
            if (!payload->publication->RecordingJoinedOwner())
                BoundaryFailure(__LINE__);
            if (!Hold(ClosePoint::ProducerJoined))
            {
                stage = Stage::AwaitTransport;
                jobs.NotifyOwnerWork();
            }
            return true;
        }
        if (stage == Stage::RetireTransport)
        {
            if (!Hold(ClosePoint::TransportRetired))
                Resume(Stage::Publish);
            return true;
        }
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket original) noexcept override
    {
        ticket = original;
        if (stage == Stage::Allocate)
        {
            if (Hold(ClosePoint::Allocated))
                return true;
            CommittedDisplayDescriptor descriptor;
            if (!client.DescribeCommittedDisplayOwner(descriptor) ||
                FAILED(payload->publication->CaptureOwner(jobs, ticket, queue, commands, display, descriptor)))
                BoundaryFailure(__LINE__);
            payload->captured = true;
            payload->documentSerial = descriptor.key.document.documentSerial;
            if (Hold(ClosePoint::Captured))
                return true;
            payload->input = client.session.Document();
            Resume(Stage::Prepare);
            return true;
        }
        if (stage == Stage::Prepare)
        {
            if (Hold(ClosePoint::Prepared))
                return true;
            if (FAILED(payload->publication->AdmitRecordingOwner()))
                BoundaryFailure(__LINE__);
            if (!Hold(ClosePoint::RecorderAdmitted))
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        if (stage == Stage::Publish)
        {
            if (FAILED(payload->memory->BindForPublication(jobs, ticket)) ||
                FAILED(payload->publication->PreparePublicationOwner(payload->memory, *payload->final)) ||
                !Hold(ClosePoint::PrivatePublished))
                BoundaryFailure(__LINE__);
            return true;
        }
        BoundaryFailure(__LINE__);
    }
    bool HasPending() const noexcept override
    {
        return !state.originalJoined;
    }
    bool HasReady() const noexcept override
    {
        return !state.ready && stage == Stage::AwaitTransport && payload && payload->publication->TransportReadyOwner();
    }
    void Pump() noexcept override
    {
        if (!HasReady() || !payload->publication->AuthorizeTransportRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireTransport);
    }
    bool RefusesUsefulAuthorityOwner() noexcept
    {
        CommittedDisplayDescriptor descriptor;
        std::shared_ptr<const PreparedLayerRows> rows;
        return payload && client.DescribeCommittedDisplayOwner(descriptor) &&
               FAILED(payload->publication->CaptureOwner(jobs, ticket, queue, commands, display, descriptor)) &&
               FAILED(payload->publication->PreparePublicationOwner(payload->memory, *client.session.Document())) &&
               !payload->publication->AdoptOwner(rows) && !rows;
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept override
    {
        if (&scheduler != &jobs || !jobs.InJoinedRetirementCallback(original) || !payload || state.retired ||
            !state.ready || (ticket.generation && ticket != original))
            BoundaryFailure(__LINE__);
        if (!payload->memory->IsBound() && FAILED(payload->memory->BindForRetirement(jobs, original)))
            BoundaryFailure(__LINE__);
        if (payload->captured && !payload->publication->JoinedShutdownOwner(state.gpu))
            BoundaryFailure(__LINE__);
        state.retired = std::move(payload);
        state.originalJoined = true;
        // Actual scheduler Close joined all producers before invoking this exact hook. Only now may
        // the borrowing route detach, before its owner-affine job wrapper is destroyed.
        if (!routes.Detach(*this))
            BoundaryFailure(__LINE__);
    }
    void Completed(JobResult) noexcept override
    {
        BoundaryFailure(__LINE__);
    }
};
#pragma warning(pop)

bool RunClose(D3D_DRIVER_TYPE driver, ClosePoint point)
{
    std::printf("INFO original-display-close.driver=%s prefix=%u\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", static_cast<unsigned>(point));
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr auto level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put())))
        return false;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    CommittedDisplayController display;
    auto ownedClient = std::make_unique<Client>(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    auto& client = *ownedClient;
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.EnableDeferredRelease() || !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), context.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        FAILED(display.InitializeOwner(jobs, queue, commands, routes, client, nullptr, false, false)) ||
        !client.Initialize())
        BoundaryFailure(__LINE__);
    display.RequestOwner();
    if (!Until(jobs, routes, [&]() noexcept { return client.graph != nullptr; }))
        BoundaryFailure(__LINE__);
    const auto generation = client.session.Generation();
    const auto revision = client.session.history.CurrentRevision();
    const auto root = client.session.Document()->layers.Identity();
    CloseState state;
    auto implementation = std::make_unique<CloseParent>(jobs, queue, commands, display, client, state, point, routes);
    auto* pump = implementation.get();
    if (!routes.Attach(*pump))
        BoundaryFailure(__LINE__);
    std::unique_ptr<Job> job = std::move(implementation);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.memoryBytes = kBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    JobTicket ticket;
    if (FAILED(jobs.Submit(job, options, ticket)) ||
        !Until(jobs, routes, [&]() noexcept { return state.ready.load(std::memory_order_acquire); }))
        BoundaryFailure(__LINE__);
    Expect("close-prefix-holds-original-whole-charge-with-no-CPU-history-rows-publication",
           jobs.ReservedMemoryBytes() >= kBytes && client.session.Generation() == generation &&
               client.session.history.CurrentRevision() == revision &&
               client.session.Document()->layers.Identity() == root && client.session.history.CanRedo() &&
               client.publications == 1 && !client.rows);
    Expect("close-prefix-useful-capture-and-publication-refuse-outside-exact-successful-callback",
           pump->RefusesUsefulAuthorityOwner());
    // Keep the real borrowed pump registered until RetainJoinedRetirement acknowledges producer join.
    commands.StopOwner();
    display.StopOwner();
    queue.StopNotifications();
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct Closing
    {
        CloseState& state;
        CommittedDisplayController& display;
        GpuWorkQueue& queue;
    }
#pragma warning(pop)
    closing{state, display, queue};
    jobs.Close({[](void* raw) noexcept
                {
                    auto& value = *static_cast<Closing*>(raw);
                    if (!value.state.originalJoined || !value.state.wrapperDestroyed)
                        BoundaryFailure(__LINE__);
                    value.display.JoinedShutdownOwner();
                    value.queue.CloseAfterWorkers();
                },
                [](void* raw) noexcept
                {
                    auto& value = *static_cast<Closing*>(raw);
                    value.state.RetireForWork();
                    value.display.RetireShutdownForWork();
                },
                &closing});
    Expect("close-prefix-joins-exact-original-before-worker-only-payload-retirement",
           state.originalJoined && state.wrapperDestroyed && state.workerRetired && state.chargeHeldOnWorker &&
               !state.retired);
    const bool heldPrefix = point == ClosePoint::AllocateWorker || point == ClosePoint::PrepareWorker ||
                            point == ClosePoint::RecordWorker || point == ClosePoint::RetireTransportWorker;
    Expect("close-prefix-held-producer-joins-after-actual-scheduler-shutdown",
           state.heldProducer.load() == heldPrefix && state.shutdownObserved.load() == heldPrefix);
    Expect("close-prefix-never-applies-document-history-or-rows",
           client.session.Generation() == generation && client.session.history.CurrentRevision() == revision &&
               client.session.Document()->layers.Identity() == root && client.session.history.CanRedo() &&
               !client.rows);
    state.gpu.ResetOwner();
    const bool displayClosed = display.CloseAfterWorkersOwner();
    const bool commandsClosed = commands.CloseOwner();
    const auto reserved = jobs.ReservedMemoryBytes(), releasing = jobs.ReleasingMemoryBytes();
    if (!displayClosed || !commandsClosed || reserved || releasing)
        std::printf("DIAG close-prefix displayClosed=%u commandsClosed=%u reserved=%llu releasing=%llu sources=%zu\n",
                    displayClosed ? 1u : 0u, commandsClosed ? 1u : 0u, static_cast<unsigned long long>(reserved),
                    static_cast<unsigned long long>(releasing), routes.SourceCountForTest());
    Expect("close-prefix-refunds-only-after-final-worker-and-detaches-command-routes",
           displayClosed && commandsClosed && reserved == 0 && releasing == 0);
    return true;
}

bool Run(D3D_DRIVER_TYPE driver, Scenario scenario)
{
    std::printf("INFO original-display-pixel.driver=%s scenario=%u\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", static_cast<unsigned>(scenario));
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr auto level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put())))
        return false;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    CommittedDisplayController display;
    struct PixelPump pump;
    auto ownedClient = std::make_unique<Client>(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    auto& client = *ownedClient;
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.EnableDeferredRelease() || !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), context.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)) || !routes.Attach(pump) ||
        !client.Initialize())
        BoundaryFailure(__LINE__);
    display.RequestOwner();
    if (!Until(jobs, routes, [&]() noexcept { return client.graph != nullptr; }))
        BoundaryFailure(__LINE__);
    Observation observation;
    auto implementation = std::make_unique<Parent>(jobs, queue, commands, display, client, pump, observation, scenario);
    pump.parent = implementation.get();
    std::unique_ptr<Job> job = std::move(implementation);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 23;
    options.memoryBytes = kBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    if (FAILED(jobs.Submit(job, options, observation.original)) ||
        !Until(jobs, routes, [&]() noexcept { return observation.finished && !display.HasPending() && jobs.IsIdle(); }))
        BoundaryFailure(__LINE__);
    const bool success = scenario == Scenario::Move || scenario == Scenario::Duplicate;
    Expect("terminal-status-and-worker-retirement-match-selected-boundary",
           observation.graphRetired && observation.destroyed && observation.changed == success &&
               observation.outcome.status == (success                                   ? JobStatus::Finished
                                              : scenario == Scenario::CancelAfterRecord ? JobStatus::Cancelled
                                                                                        : JobStatus::Failed) &&
               (success || scenario == Scenario::CancelAfterRecord ||
                (scenario == Scenario::WrongRowEpoch ? observation.outcome.error == E_INVALIDARG
                                                     : observation.outcome.error == E_OUTOFMEMORY)));
    if (success)
    {
        std::vector<std::uint8_t> pixels;
        unsigned width = 0, height = 0;
        const auto status = client.graph->CompositionForTestOwner()->Readback(pixels, width, height);
        const auto expected = scenario == Scenario::Duplicate
                                  ? std::vector<std::uint8_t>{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0}
                                  : std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0};
        Expect("real-final-GPU-pixels-match-independent-move-or-duplicate-literal",
               SUCCEEDED(status) && width == 3 && height == 1 && pixels == expected);
        Expect("row-reader-keeps-original-whole-charge-after-parent-retirement",
               jobs.ReservedMemoryBytes() == kBytes && client.rows);
        client.session.Undo();
        std::array<std::uint8_t, 12> restored{};
        const auto* restoredLayer = client.session.ActiveLayer();
        if (restoredLayer && restoredLayer->asset)
            restoredLayer->asset->image.ReadRegion(0, 0, 3, 1, restored.data(), 12, 0);
        const std::array<std::uint8_t, 12> original{0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0};
        Expect("one-Undo-restores-blue-neighbour-and-redo",
               restored == original && client.session.history.CanRedo() &&
                   client.session.Document()->selection == std::optional{Rectangle(0)});
        client.session.Redo();
        Expect("one-Redo-restores-moved-selection",
               client.session.Document()->selection == std::optional{Rectangle(1)});
    }
    client.session.ClearDocument();
    display.RequestOwner();
    client.rows.reset();
    if (!Until(jobs, routes,
               [&]() noexcept { return !display.HasPending() && jobs.IsIdle() && jobs.ReservedMemoryBytes() == 0; }))
        BoundaryFailure(__LINE__);
    Expect("real-last-use-and-worker-retirement-refund-every-original-charge",
           jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    if (!routes.Detach(pump))
        BoundaryFailure(__LINE__);
    display.StopOwner();
    queue.StopNotifications();
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct Closing
    {
        CommittedDisplayController& display;
        GpuWorkQueue& queue;
    }
#pragma warning(pop)
    closing{display, queue};
    jobs.Close({[](void* p) noexcept
                {
                    auto& c = *static_cast<Closing*>(p);
                    c.display.JoinedShutdownOwner();
                    c.queue.CloseAfterWorkers();
                },
                [](void* p) noexcept { static_cast<Closing*>(p)->display.RetireShutdownForWork(); }, &closing});
    Expect("joined-close-detaches-routes-and-leaves-no-charge",
           display.CloseAfterWorkersOwner() && jobs.ReservedMemoryBytes() == 0);
    return true;
}
} // namespace

int RunOriginalDisplayPixelPublicationTests()
{
    for (const auto driver : {D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE})
        for (const auto point : {ClosePoint::Allocated, ClosePoint::Captured, ClosePoint::Prepared,
                                 ClosePoint::RecorderAdmitted, ClosePoint::ProducerJoined, ClosePoint::TransportRetired,
                                 ClosePoint::PrivatePublished, ClosePoint::AllocateWorker, ClosePoint::PrepareWorker,
                                 ClosePoint::RecordWorker, ClosePoint::RetireTransportWorker})
            if (!RunClose(driver, point))
                ++failures;

    for (const auto driver : {D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE})
        for (const auto scenario : {Scenario::Move, Scenario::Duplicate, Scenario::CpuRefused, Scenario::GpuRefused,
                                    Scenario::CancelAfterRecord, Scenario::WrongRowEpoch})
            if (!Run(driver, scenario))
                ++failures;
    return failures == 0 ? 0 : 1;
}

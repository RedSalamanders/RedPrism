#include "Composition.Tests.PublicationParent.h"

#include "CommittedDisplayController.h"
#include "DocumentDraft.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// Independent transport/publication state-machine probe. EditForTest models the single checked
// document swap only; this establishes neither application history/row activation nor G1 latency.
namespace
{
unsigned int failures = 0;
constexpr std::uint64_t kParentBytes = 192ull * 1024 * 1024;

void Expect(const char* name, bool condition) noexcept
{
    std::printf("%s publication-parent.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!condition)
        ++failures;
}

bool Memory(void*, MemorySnapshot& output) noexcept
{
    output = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
}

class Client final : public CommittedDisplayClient
{
  public:
    DocumentSession session;
    GpuWorkQueue& queue;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    bool software;
    RecordedDisplayGraph* graph = nullptr;
    unsigned int publications = 0;
    unsigned int errors = 0;

    Client(GpuWorkQueue& q, ID3D11Device* d, ID3D11DeviceContext* c, bool s) noexcept
        : queue(q), device(d), context(c), software(s)
    {
    }

    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& output) noexcept override
    {
        const auto authority = session.CaptureDocumentReadAuthority();
        if (!authority)
            return false;
        output = {{*authority, queue.DeviceGeneration()}, &*session.Document(), device, context, software};
        return true;
    }
    void CommittedDisplayPublishedOwner(RecordedDisplayGraph* value) noexcept override
    {
        graph = value;
        if (value)
            ++publications;
    }
    void CommittedDisplayFailedOwner(CommittedDisplayKey, HRESULT error) noexcept override
    {
        ++errors;
        std::printf("INFO publication-parent.controller-error=0x%08x\n", static_cast<unsigned int>(error));
    }
    bool Fixture()
    {
        // Even premultiplied channels have an exact 0.5-opacity result {32,48,64,127}.
        // Alpha 254 deliberately avoids the 127.5 UNORM rounding boundary: WARP and hardware
        // chose different legal adjacent bytes for alpha 255 in the preserved r2/r3 experiments.
        const std::array<std::uint8_t, 16> bytes{64, 96, 128, 254, 64, 96, 128, 254,
                                                 64, 96, 128, 254, 64, 96, 128, 254};
        CanvasDocument document;
        document.width = document.height = 2;
        auto draft = CanvasDocumentDraft::Rebuild(document, 1);
        auto layer = ImageLayer::Blank(L"Publication fixture", {2, 2});
        const auto id = layer.id;
        auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        layer.transform.sampling = LayerSampling::Nearest;
        if (!draft || !image.HasStorage())
            return false;
        layer.asset = RasterAsset{std::move(image), {}, L"Publication fixture"};
        if (!draft->Append(std::move(layer)))
            return false;
        auto sealed = draft->Seal();
        if (!sealed)
            return false;
        session.ReplaceDocument(std::move(*sealed), id);
        return true;
    }
};

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

struct Observation final
{
    bool finished = false;
    bool usefulWorkersOffOwner = true;
    bool streamRetiredOffOwner = false;
    bool payloadRetiredOffOwner = false;
    bool followerStarted = false;
    bool followerAfterFinal = false;
    bool followerFinished = false;
    unsigned int runs = 0;
    unsigned int acknowledgements = 0;
    JobTicket original;
    JobResult terminal;
    std::uint64_t chargedAtPublication = 0;
};

class Parent;
class ParentPump final : public OwnerWorkPump
{
  public:
    Parent* parent = nullptr;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
};

class Parent final : public Job, public GpuCommandSourceReceiver
{
    enum class Stage
    {
        Allocate,
        Prepare,
        Record,
        AwaitCommands,
        RetireStream,
        Publish,
        Cleanup
    };
    struct Payload final
    {
        CanvasDocument source;
        RasterResidencyCpuAdmission admission;
        std::unique_ptr<PreparedCommittedDisplay> prepared;
        std::shared_ptr<SharedPublicationMemory> memory;
        wil::com_ptr_nothrow<ID3D11Device> device;
        wil::com_ptr_nothrow<ID3D11DeviceContext> context;
        GpuCompletionContext captured;
        GpuCompletionContext detachedCommandOwner;
        GpuCommandStream stream;
    };
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    CommittedDisplayController& display;
    Client& client;
    ParentPump& pump;
    Observation& observation;
    const std::uint32_t ownerThread;
    std::unique_ptr<Payload> payload;
    CommittedDisplayPublicationSlot slot;
    CommittedDisplaySeedLease seed;
    GpuResidencyReservation gpu;
    GpuCommandSourceLease commandSource;
    Stage stage = Stage::Allocate;
    bool recordingJoined = false;
    bool streamInitialized = false;
    bool producerFinished = false;
    HRESULT terminal = S_OK;
    bool published = false;

    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        stage = next;
        if (!jobs.Resume(observation.original, lane))
            std::terminate();
    }

  public:
    Parent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, CommittedDisplayController& d, Client& cl,
           ParentPump& p, Observation& o) noexcept
        : jobs(j), queue(q), commands(c), display(d), client(cl), pump(p), observation(o),
          ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        observation.usefulWorkersOffOwner &= GetCurrentThreadId() != ownerThread;
        ++observation.runs;
        if (stage == Stage::Allocate)
            payload = std::make_unique<Payload>();
        else if (stage == Stage::Prepare)
        {
            auto draft = CanvasDocumentDraft::Clone(payload->source, 1, context.cancellation);
            if (!draft)
                return {JobStatus::Failed, E_INVALIDARG};
            draft->EditLayers()[0].opacity = 0.5;
            auto sealed = draft->Seal(context.cancellation);
            if (!sealed)
                return {JobStatus::Failed, E_INVALIDARG};
            payload->source = std::move(*sealed);
            payload->admission = RasterResidencyCpuAdmission::Estimate(
                payload->source, {16384, queueGeneration, 1024 * 1024, 128 * 1024});
            payload->memory = SharedPublicationMemory::CreateForWork();
            payload->prepared = PreparedCommittedDisplay::PrepareForWork(
                ownerThread, payload->source, payload->admission, kParentBytes, payload->device.get(),
                payload->context.get(), std::move(payload->captured), context.cancellation);
            if (!payload->prepared)
                return {JobStatus::Failed, E_OUTOFMEMORY};
        }
        else if (stage == Stage::Record)
        {
            const HRESULT status =
                payload->prepared->RecordForWork(payload->stream, context.cancellation, seed.GraphForWork());
            return FAILED(status) ? JobResult{JobStatus::Failed, status} : JobResult{};
        }
        return {};
    }
    std::uint64_t queueGeneration = 0; // Owner captured before the first resumed preparation.

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (stage == Stage::Record && !producerFinished)
        {
            payload->stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK
                                                  : result.status == JobStatus::Failed ? result.error
                                                                                       : E_ABORT);
            producerFinished = true;
        }
        if (stage == Stage::RetireStream)
        {
            if (!payload->stream.RetireForWork())
                std::terminate();
            observation.streamRetiredOffOwner = GetCurrentThreadId() != ownerThread;
        }
        if (stage == Stage::Cleanup)
        {
            // Adoption consumed the graph, so this destructor cannot hide partial GPU retirement.
            if (!published || !payload || !payload->prepared)
                std::terminate();
            payload.reset();
            observation.payloadRetiredOffOwner = GetCurrentThreadId() != ownerThread;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != observation.original)
            std::terminate();
        ++observation.acknowledgements;
        if (result.status != JobStatus::Finished || FAILED(terminal))
            std::terminate(); // Success probe must not accidentally qualify failure cleanup.
        switch (stage)
        {
        case Stage::Allocate:
        {
            CommittedDisplayDescriptor descriptor;
            auto wrongGeneration = ticket;
            ++wrongGeneration.generation;
            auto wrongSlot = ticket;
            wrongSlot.slot = (ticket.slot + 1) % JobScheduler::kCapacity;
            CommittedDisplayPublicationSlot foreignClaim;
            Expect("successful-joined-callback-authorizes-only-its-exact-original-ticket",
                   jobs.InAcknowledgedJobCallback(ticket) && !jobs.InAcknowledgedJobCallback(wrongGeneration) &&
                       !jobs.InAcknowledgedJobCallback(wrongSlot) &&
                       !display.ClaimPublicationSlotOwner(wrongGeneration, foreignClaim) && !foreignClaim);
            if (!client.DescribeCommittedDisplayOwner(descriptor) || !display.ClaimPublicationSlotOwner(ticket, slot) ||
                !display.AcquirePublishedSeedOwner(descriptor.key, seed))
                std::terminate();
            Expect("original-owner-preclaims-fixed-slot-and-exact-cached-seed", bool(slot) && bool(seed));
            CommittedDisplayPublicationSlot second;
            const auto charged = jobs.ReservedMemoryBytes();
            Expect("second-publication-slot-refuses-without-changing-charge",
                   !display.ClaimPublicationSlotOwner(ticket, second) && !second &&
                       jobs.ReservedMemoryBytes() == charged);
            payload->source = *descriptor.document;
            payload->device = descriptor.device;
            payload->context = descriptor.immediate;
            queueGeneration = descriptor.key.deviceGeneration;
            if (FAILED(queue.CaptureCompletionContext(payload->captured)))
                std::terminate();
            Resume(Stage::Prepare);
            return true;
        }
        case Stage::Prepare:
        {
            const auto* manifest = payload->prepared->ManifestAfterJoin();
            if (!manifest || FAILED(display.ReservePublicationGpuOwner(slot, payload->device.get(), client.software,
                                                                       manifest->RequiredGpuBytes(), gpu)))
                std::terminate();
            Expect("complete-CPU-and-fresh-actual-GPU-admission-precede-recording",
                   payload->admission.IsValid() && gpu.Bytes() >= manifest->RequiredGpuBytes() &&
                       !payload->memory->IsBound() && display.PublicationSlotCurrentOwner(slot));
            if (FAILED(payload->stream.InitializeOwner(jobs, ticket, queueGeneration)) ||
                FAILED(commands.AttachOwner(payload->stream, ticket, *this, commandSource)))
                std::terminate();
            streamInitialized = true;
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        case Stage::Record:
            recordingJoined = true;
            if (!display.ReleasePublishedSeedOwner(seed))
                std::terminate();
            stage = Stage::AwaitCommands;
            jobs.NotifyOwnerWork();
            return true;
        case Stage::RetireStream:
            stage = Stage::Publish;
            return false; // Exact successful DeferCompletion gets the publication-transfer authority.
        case Stage::Cleanup:
            return false;
        default:
            std::terminate();
        }
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (stage != Stage::Publish)
            return false;
        if (ticket != observation.original || !display.PublicationSlotCurrentOwner(slot) ||
            FAILED(payload->memory->BindForPublication(jobs, ticket)) ||
            !payload->prepared->BindMemoryOwner(payload->memory) || FAILED(payload->prepared->PublishOwner()))
            std::terminate();
        const auto cpuBefore = jobs.ReservedMemoryBytes();
        const auto gpuBefore = gpu.Bytes();
        Expect("same-original-ticket-binds-whole-publication-after-real-recording-and-transport-retirement",
               payload->memory->Bytes() == kParentBytes && recordingJoined && observation.streamRetiredOffOwner &&
                   !seed && payload->stream.CanRetireOwner());
        Expect("old-document-generation-refuses-adoption-with-all-owners-preserved",
               !display.AdoptPublicationOwner(slot, *payload->prepared, gpu) && bool(slot) &&
                   gpu.Bytes() == gpuBefore && jobs.ReservedMemoryBytes() == cpuBefore &&
                   payload->prepared->ManifestAfterJoin());
        Expect("bound-accounting-provenance-is-the-exact-original-scheduler-and-ticket",
               payload->memory->BoundTo(jobs, ticket) &&
                   !payload->memory->BoundTo(jobs, JobTicket{ticket.slot, ticket.generation + 1}));
        Expect("preflight-accepts-only-the-exact-final-layer-root-before-document-mutation",
               display.CanAdoptPublicationOwner(slot, *payload->prepared, gpu, payload->source) &&
                   !display.CanAdoptPublicationOwner(slot, *payload->prepared, gpu, *client.session.Document()));
        GpuResidencyBudget foreignLedger;
        GpuResidencyAdmission foreignDecision;
        auto foreignReservation =
            foreignLedger.ReserveOwner(gpu.Bytes(), {queueGeneration, 1024ull * 1024 * 1024, 0}, foreignDecision);
        Expect("foreign-ledger-reservation-refuses-before-document-or-history-mutation",
               foreignReservation &&
                   !display.CanAdoptPublicationOwner(slot, *payload->prepared, foreignReservation, payload->source) &&
                   jobs.ReservedMemoryBytes() == cpuBefore && gpu.Bytes() == gpuBefore && bool(slot));
        foreignReservation.ResetOwner();
        // Fixture-only surrogate for the eventual prepared history/document publication. No nested callback.
        client.session.EditForTest([&](CanvasDocument& document) noexcept { std::swap(document, payload->source); });
        if (!display.AdoptPublicationOwner(slot, *payload->prepared, gpu))
            std::terminate();
        published = true;
        observation.chargedAtPublication = jobs.ReservedMemoryBytes();
        Expect("one-generation-document-swap-adopts-exact-layer-root-and-original-device",
               !slot && !gpu && client.graph && client.graph->IsPublishedOnOwner(client.device, client.context) &&
                   !payload->prepared->ManifestAfterJoin() && observation.chargedAtPublication >= kParentBytes);
        Resume(Stage::Cleanup);
        return true; // Same FIFO record survives publication through payload/command/context retirement.
    }
    void Completed(JobResult result) noexcept override
    {
        if (stage != Stage::Cleanup || payload || slot || seed || gpu || !published)
            std::terminate();
        observation.terminal = result;
        Expect("final-Completed-has-no-successful-acknowledgement-capture-authority",
               !jobs.InAcknowledgedJobCallback(observation.original));
        observation.finished = true;
        pump.parent = nullptr;
    }
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT status) noexcept override
    {
        if (ticket != observation.original || command.source != observation.original)
            std::terminate();
        if (FAILED(status))
            terminal = status;
        jobs.NotifyOwnerWork();
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT status) noexcept override
    {
        if (ticket != observation.original)
            std::terminate();
        terminal = status;
        jobs.NotifyOwnerWork();
    }
    bool Waiting() const noexcept
    {
        return stage == Stage::AwaitCommands;
    }
    bool Ready() const noexcept
    {
        return Waiting() && recordingJoined && streamInitialized && payload->stream.CanRetireOwner();
    }
    void Pump() noexcept
    {
        if (!Ready())
            return;
        if (FAILED(terminal) || !commands.DetachOwner(commandSource, payload->detachedCommandOwner) ||
            !payload->stream.AuthorizeRetirementOwner())
            std::terminate();
        commandSource = {};
        Resume(Stage::RetireStream);
    }
};

bool ParentPump::HasPending() const noexcept
{
    return parent != nullptr;
}
bool ParentPump::HasReady() const noexcept
{
    return parent && parent->Ready();
}
void ParentPump::Pump() noexcept
{
    if (parent)
        parent->Pump();
}

class Follower final : public Job
{
    Observation& observation;

  public:
    explicit Follower(Observation& value) noexcept : observation(value) {}
    JobResult Run(JobContext&) override
    {
        observation.followerStarted = true;
        observation.followerAfterFinal = observation.finished;
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        observation.followerFinished = result.status == JobStatus::Finished;
    }
};

bool Run(D3D_DRIVER_TYPE driver)
{
    std::printf("INFO publication-parent.driver=%s\n", driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware");
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
    ParentPump pump;
    Client client(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.EnableDeferredRelease() || !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), context.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)) || !routes.Attach(pump) ||
        !client.Fixture())
        std::terminate();
    display.RequestOwner();
    if (!Until(jobs, routes, [&]() noexcept { return client.graph || client.errors; }) || !client.graph)
        std::terminate();
    Expect("initial-real-cache-is-published-and-charged",
           client.publications == 1 && jobs.ReservedMemoryBytes() != 0 && !display.HasPending());
    Observation observation;
    auto implementation = std::make_unique<Parent>(jobs, queue, commands, display, client, pump, observation);
    pump.parent = implementation.get();
    std::unique_ptr<Job> parent = std::move(implementation);
    JobOptions options;
    options.kind = JobClass::Stroke;
    options.serialKey = 17;
    options.memoryBytes = kParentBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    if (FAILED(jobs.Submit(parent, options, observation.original)))
        std::terminate();
    Expect("outside-job-callback-has-no-original-capture-authority",
           !jobs.InAcknowledgedJobCallback(observation.original));
    JobTicket follower;
    std::unique_ptr<Job> next = std::make_unique<Follower>(observation);
    auto nextOptions = options;
    nextOptions.memoryBytes = 1024;
    nextOptions.deferredRetirement = false;
    nextOptions.reserveRetainedMemory = false;
    if (FAILED(jobs.Submit(next, nextOptions, follower)))
        std::terminate();
    Expect("same-key-follower-queued-behind-original-publication-record", !observation.followerStarted);
    if (!Until(jobs, routes, [&]() noexcept
               { return observation.finished && observation.followerFinished && !display.HasPending(); }))
        std::terminate();
    Expect("all-useful-stages-and-final-payload-retirement-run-on-real-workers",
           observation.usefulWorkersOffOwner && observation.streamRetiredOffOwner &&
               observation.payloadRetiredOffOwner && observation.runs == 5 && observation.acknowledgements == 5);
    Expect("same-key-FIFO-unblocks-only-after-original-final-callback",
           observation.followerStarted && observation.followerAfterFinal && observation.followerFinished &&
               observation.terminal.status == JobStatus::Finished);
    Expect("published-root-retains-exact-whole-parent-charge-after-original-wrapper-retirement",
           jobs.ReservedMemoryBytes() == kParentBytes && jobs.ReleasingMemoryBytes() == 0 && client.publications == 2 &&
               client.errors == 0 && client.graph);
    std::vector<std::uint8_t> pixels;
    unsigned int width = 0, height = 0;
    const auto readback = client.graph->CompositionForTestOwner()->Readback(pixels, width, height);
    std::printf("INFO publication-parent.readback hr=0x%08x width=%u height=%u bytes=",
                static_cast<unsigned int>(readback), width, height);
    for (const auto value : pixels)
        std::printf("%u,", static_cast<unsigned int>(value));
    std::printf("\n");
    Expect("literal-premultiplied-half-opacity-pixels-match-independent-reference",
           SUCCEEDED(readback) && width == 2 && height == 2 &&
               pixels == std::vector<std::uint8_t>{32, 48, 64, 127, 32, 48, 64, 127, 32, 48, 64, 127, 32, 48, 64, 127});
    client.session.ClearDocument();
    display.RequestOwner();
    Expect("clear-excludes-published-pixels-before-original-retirement", !client.graph);
    Expect(
        "actual-last-use-barrier-and-worker-root-destruction-refund-whole-parent-charge",
        Until(
            jobs, routes, [&]() noexcept
            { return !display.HasPending() && jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0; }));
    Expect("finished-parent-route-detaches-without-pending-work", routes.Detach(pump));
    display.StopOwner();
    queue.StopNotifications();
    struct Closing
    {
        CommittedDisplayController& display;
        GpuWorkQueue& queue;
    } closing{display, queue};
    jobs.Close({[](void* value) noexcept
                {
                    auto& state = *static_cast<Closing*>(value);
                    state.display.JoinedShutdownOwner();
                    state.queue.CloseAfterWorkers();
                },
                [](void* value) noexcept { static_cast<Closing*>(value)->display.RetireShutdownForWork(); }, &closing});
    Expect("actual-joined-close-returns-all-charges-and-controller-routes",
           display.CloseAfterWorkersOwner() && jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    return failures == 0;
}
} // namespace

int RunPublicationParentTests()
{
    const bool warp = Run(D3D_DRIVER_TYPE_WARP);
    const bool hardware = Run(D3D_DRIVER_TYPE_HARDWARE);
    return warp && hardware && failures == 0 ? 0 : 1;
}

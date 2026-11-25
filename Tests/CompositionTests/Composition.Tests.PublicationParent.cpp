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
unsigned int failures                = 0;
constexpr std::uint64_t kParentBytes = 192ull * 1024 * 1024;

void Expect(const char* name, bool condition) noexcept
{
    std::printf("%s publication-parent.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (! condition)
        ++failures;
}

bool Memory(void*, MemorySnapshot& output) noexcept
{
    output = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
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
    unsigned int publications   = 0;
    unsigned int errors         = 0;

    Client(GpuWorkQueue& q, ID3D11Device* d, ID3D11DeviceContext* c, bool s) noexcept : queue(q), device(d), context(c), software(s)
    {
    }

    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& output) noexcept override
    {
        const auto authority = session.CaptureDocumentReadAuthority();
        if (! authority)
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
        const std::array<std::uint8_t, 16> bytes{64, 96, 128, 254, 64, 96, 128, 254, 64, 96, 128, 254, 64, 96, 128, 254};
        CanvasDocument document;
        document.width = document.height = 2;
        auto draft                       = CanvasDocumentDraft::Rebuild(document, 1);
        auto layer                       = ImageLayer::Blank(L"Publication fixture", {2, 2});
        const auto id                    = layer.id;
        auto image                       = Raster::Make(2, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        layer.transform.sampling         = LayerSampling::Nearest;
        if (! draft || ! image.HasStorage())
            return false;
        layer.asset = RasterAsset{std::move(image), {}, L"Publication fixture"};
        if (! draft->Append(std::move(layer)))
            return false;
        auto sealed = draft->Seal();
        if (! sealed)
            return false;
        session.ReplaceDocument(std::move(*sealed), id);
        return true;
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
        if (elapsed >= 10000 || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(10000 - elapsed)) != WAIT_OBJECT_0)
            return false;
    }
}

struct Observation final
{
    bool finished                 = false;
    bool usefulWorkersOffOwner    = true;
    bool streamRetiredOffOwner    = false;
    bool payloadRetiredOffOwner   = false;
    bool followerStarted          = false;
    bool followerAfterFinal       = false;
    bool followerFinished         = false;
    unsigned int runs             = 0;
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

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
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
#pragma warning(pop)

    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    CommittedDisplayController& _display;
    Client& _client;
    ParentPump& _pump;
    Observation& _observation;
    const std::uint32_t _ownerThread;
    std::unique_ptr<Payload> _payload;
    CommittedDisplayPublicationSlot _slot;
    CommittedDisplaySeedLease _seed;
    GpuResidencyReservation _gpu;
    GpuCommandSourceLease _commandSource;
    Stage _stage            = Stage::Allocate;
    bool _recordingJoined   = false;
    bool _streamInitialized = false;
    bool _producerFinished  = false;
    HRESULT _terminal       = S_OK;
    bool _published         = false;

    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = next;
        if (! _jobs.Resume(_observation.original, lane))
            std::terminate();
    }

public:
    Parent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, CommittedDisplayController& d, Client& cl, ParentPump& p, Observation& o) noexcept
        : _jobs(j),
          _queue(q),
          _commands(c),
          _display(d),
          _client(cl),
          _pump(p),
          _observation(o),
          _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        _observation.usefulWorkersOffOwner &= GetCurrentThreadId() != _ownerThread;
        ++_observation.runs;
        if (_stage == Stage::Allocate)
            _payload = std::make_unique<Payload>();
        else if (_stage == Stage::Prepare)
        {
            auto draft = CanvasDocumentDraft::Clone(_payload->source, 1, context.cancellation);
            if (! draft)
                return {JobStatus::Failed, E_INVALIDARG};
            draft->EditLayers()[0].opacity = 0.5;
            auto sealed                    = draft->Seal(context.cancellation);
            if (! sealed)
                return {JobStatus::Failed, E_INVALIDARG};
            _payload->source    = std::move(*sealed);
            _payload->admission = RasterResidencyCpuAdmission::Estimate(_payload->source, {16384, queueGeneration, 1024 * 1024, 128 * 1024});
            _payload->memory    = SharedPublicationMemory::CreateForWork();
            _payload->prepared  = PreparedCommittedDisplay::PrepareForWork(_ownerThread,
                                                                           _payload->source,
                                                                           _payload->admission,
                                                                           kParentBytes,
                                                                           _payload->device.get(),
                                                                           _payload->context.get(),
                                                                           std::move(_payload->captured),
                                                                           context.cancellation);
            if (! _payload->prepared)
                return {JobStatus::Failed, E_OUTOFMEMORY};
        }
        else if (_stage == Stage::Record)
        {
            const HRESULT status = _payload->prepared->RecordForWork(_payload->stream, context.cancellation, _seed.GraphForWork());
            return FAILED(status) ? JobResult{JobStatus::Failed, status} : JobResult{};
        }
        return {};
    }
    std::uint64_t queueGeneration = 0; // Owner captured before the first resumed preparation.

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_stage == Stage::Record && ! _producerFinished)
        {
            _payload->stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK : result.status == JobStatus::Failed ? result.error : E_ABORT);
            _producerFinished = true;
        }
        if (_stage == Stage::RetireStream)
        {
            if (! _payload->stream.RetireForWork())
                std::terminate();
            _observation.streamRetiredOffOwner = GetCurrentThreadId() != _ownerThread;
        }
        if (_stage == Stage::Cleanup)
        {
            // Adoption consumed the graph, so this destructor cannot hide partial GPU retirement.
            if (! _published || ! _payload || ! _payload->prepared)
                std::terminate();
            _payload.reset();
            _observation.payloadRetiredOffOwner = GetCurrentThreadId() != _ownerThread;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != _observation.original)
            std::terminate();
        ++_observation.acknowledgements;
        if (result.status != JobStatus::Finished || FAILED(_terminal))
            std::terminate(); // Success probe must not accidentally qualify failure cleanup.
        switch (_stage)
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
                       _jobs.InAcknowledgedJobCallback(ticket) && ! _jobs.InAcknowledgedJobCallback(wrongGeneration) &&
                           ! _jobs.InAcknowledgedJobCallback(wrongSlot) && ! _display.ClaimPublicationSlotOwner(wrongGeneration, foreignClaim) &&
                           ! foreignClaim);
                if (! _client.DescribeCommittedDisplayOwner(descriptor) || ! _display.ClaimPublicationSlotOwner(ticket, _slot) ||
                    ! _display.AcquirePublishedSeedOwner(descriptor.key, _seed))
                    std::terminate();
                Expect("original-owner-preclaims-fixed-slot-and-exact-cached-seed", bool(_slot) && bool(_seed));
                CommittedDisplayPublicationSlot second;
                const auto charged = _jobs.ReservedMemoryBytes();
                Expect("second-publication-slot-refuses-without-changing-charge",
                       ! _display.ClaimPublicationSlotOwner(ticket, second) && ! second && _jobs.ReservedMemoryBytes() == charged);
                _payload->source  = *descriptor.document;
                _payload->device  = descriptor.device;
                _payload->context = descriptor.immediate;
                queueGeneration   = descriptor.key.deviceGeneration;
                if (FAILED(_queue.CaptureCompletionContext(_payload->captured)))
                    std::terminate();
                Resume(Stage::Prepare);
                return true;
            }
            case Stage::Prepare:
            {
                const auto* manifest = _payload->prepared->ManifestAfterJoin();
                if (! manifest ||
                    FAILED(_display.ReservePublicationGpuOwner(_slot, _payload->device.get(), _client.software, manifest->RequiredGpuBytes(), _gpu)))
                    std::terminate();
                Expect("complete-CPU-and-fresh-actual-GPU-admission-precede-recording",
                       _payload->admission.IsValid() && _gpu.Bytes() >= manifest->RequiredGpuBytes() && ! _payload->memory->IsBound() &&
                           _display.PublicationSlotCurrentOwner(_slot));
                if (FAILED(_payload->stream.InitializeOwner(_jobs, ticket, queueGeneration)) ||
                    FAILED(_commands.AttachOwner(_payload->stream, ticket, *this, _commandSource)))
                    std::terminate();
                _streamInitialized = true;
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
                return true;
            }
            case Stage::Record:
                _recordingJoined = true;
                if (! _display.ReleasePublishedSeedOwner(_seed))
                    std::terminate();
                _stage = Stage::AwaitCommands;
                _jobs.NotifyOwnerWork();
                return true;
            case Stage::RetireStream: _stage = Stage::Publish; return false; // Exact successful DeferCompletion gets the publication-transfer authority.
            case Stage::Cleanup: return false;
            case ::Parent::Stage::AwaitCommands:
            case ::Parent::Stage::Publish:
            default: std::terminate();
        }
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_stage != Stage::Publish)
            return false;
        if (ticket != _observation.original || ! _display.PublicationSlotCurrentOwner(_slot) || FAILED(_payload->memory->BindForPublication(_jobs, ticket)) ||
            ! _payload->prepared->BindMemoryOwner(_payload->memory) || FAILED(_payload->prepared->PublishOwner()))
            std::terminate();
        const auto cpuBefore = _jobs.ReservedMemoryBytes();
        const auto gpuBefore = _gpu.Bytes();
        Expect("same-original-ticket-binds-whole-publication-after-real-recording-and-transport-retirement",
               _payload->memory->Bytes() == kParentBytes && _recordingJoined && _observation.streamRetiredOffOwner && ! _seed &&
                   _payload->stream.CanRetireOwner());
        Expect("old-document-generation-refuses-adoption-with-all-owners-preserved",
               ! _display.AdoptPublicationOwner(_slot, *_payload->prepared, _gpu) && bool(_slot) && _gpu.Bytes() == gpuBefore &&
                   _jobs.ReservedMemoryBytes() == cpuBefore && _payload->prepared->ManifestAfterJoin());
        Expect("bound-accounting-provenance-is-the-exact-original-scheduler-and-ticket",
               _payload->memory->BoundTo(_jobs, ticket) && ! _payload->memory->BoundTo(_jobs, JobTicket{ticket.slot, ticket.generation + 1}));
        Expect("preflight-accepts-only-the-exact-final-layer-root-before-document-mutation",
               _display.CanAdoptPublicationOwner(_slot, *_payload->prepared, _gpu, _payload->source) &&
                   ! _display.CanAdoptPublicationOwner(_slot, *_payload->prepared, _gpu, *_client.session.Document()));
        GpuResidencyBudget foreignLedger;
        GpuResidencyAdmission foreignDecision;
        auto foreignReservation = foreignLedger.ReserveOwner(_gpu.Bytes(), {queueGeneration, 1024ull * 1024 * 1024, 0}, foreignDecision);
        Expect("foreign-ledger-reservation-refuses-before-document-or-history-mutation",
               foreignReservation && ! _display.CanAdoptPublicationOwner(_slot, *_payload->prepared, foreignReservation, _payload->source) &&
                   _jobs.ReservedMemoryBytes() == cpuBefore && _gpu.Bytes() == gpuBefore && bool(_slot));
        foreignReservation.ResetOwner();
        // Fixture-only surrogate for the eventual prepared history/document publication. No nested callback.
        _client.session.EditForTest([&](CanvasDocument& document) noexcept { std::swap(document, _payload->source); });
        if (! _display.AdoptPublicationOwner(_slot, *_payload->prepared, _gpu))
            std::terminate();
        _published                        = true;
        _observation.chargedAtPublication = _jobs.ReservedMemoryBytes();
        Expect("one-generation-document-swap-adopts-exact-layer-root-and-original-device",
               ! _slot && ! _gpu && _client.graph && _client.graph->IsPublishedOnOwner(_client.device, _client.context) &&
                   ! _payload->prepared->ManifestAfterJoin() && _observation.chargedAtPublication >= kParentBytes);
        Resume(Stage::Cleanup);
        return true; // Same FIFO record survives publication through payload/command/context retirement.
    }
    void Completed(JobResult result) noexcept override
    {
        if (_stage != Stage::Cleanup || _payload || _slot || _seed || _gpu || ! _published)
            std::terminate();
        _observation.terminal = result;
        Expect("final-Completed-has-no-successful-acknowledgement-capture-authority", ! _jobs.InAcknowledgedJobCallback(_observation.original));
        _observation.finished = true;
        _pump.parent          = nullptr;
    }
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT status) noexcept override
    {
        if (ticket != _observation.original || command.source != _observation.original)
            std::terminate();
        if (FAILED(status))
            _terminal = status;
        _jobs.NotifyOwnerWork();
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT status) noexcept override
    {
        if (ticket != _observation.original)
            std::terminate();
        _terminal = status;
        _jobs.NotifyOwnerWork();
    }
    bool Waiting() const noexcept
    {
        return _stage == Stage::AwaitCommands;
    }
    bool Ready() const noexcept
    {
        return Waiting() && _recordingJoined && _streamInitialized && _payload->stream.CanRetireOwner();
    }
    void Pump() noexcept
    {
        if (! Ready())
            return;
        if (FAILED(_terminal) || ! _commands.DetachOwner(_commandSource, _payload->detachedCommandOwner) || ! _payload->stream.AuthorizeRetirementOwner())
            std::terminate();
        _commandSource = {};
        Resume(Stage::RetireStream);
    }
};
#pragma warning(pop)

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

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class Follower final : public Job
{
    Observation& _observation;

public:
    explicit Follower(Observation& value) noexcept : _observation(value)
    {
    }
    JobResult Run(JobContext&) override
    {
        _observation.followerStarted    = true;
        _observation.followerAfterFinal = _observation.finished;
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _observation.followerFinished = result.status == JobStatus::Finished;
    }
};
#pragma warning(pop)

bool Run(D3D_DRIVER_TYPE driver)
{
    std::printf("INFO publication-parent.driver=%s\n", driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware");
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr auto level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(
            D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put())))
        return false;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    CommittedDisplayController display;
    ParentPump pump;
    Client client(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) || ! jobs.EnableDeferredRelease() ||
        ! jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), context.get())) ||
        FAILED(commands.InitializeOwner(jobs, queue, routes)) || FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)) ||
        ! routes.Attach(pump) || ! client.Fixture())
        std::terminate();
    display.RequestOwner();
    if (! Until(jobs, routes, [&]() noexcept { return client.graph || client.errors; }) || ! client.graph)
        std::terminate();
    Expect("initial-real-cache-is-published-and-charged", client.publications == 1 && jobs.ReservedMemoryBytes() != 0 && ! display.HasPending());
    Observation observation;
    auto implementation         = std::make_unique<Parent>(jobs, queue, commands, display, client, pump, observation);
    pump.parent                 = implementation.get();
    std::unique_ptr<Job> parent = std::move(implementation);
    JobOptions options;
    options.kind                  = JobClass::Stroke;
    options.serialKey             = 17;
    options.memoryBytes           = kParentBytes;
    options.deferredRetirement    = true;
    options.reserveRetainedMemory = true;
    if (FAILED(jobs.Submit(parent, options, observation.original)))
        std::terminate();
    Expect("outside-job-callback-has-no-original-capture-authority", ! jobs.InAcknowledgedJobCallback(observation.original));
    JobTicket follower;
    std::unique_ptr<Job> next         = std::make_unique<Follower>(observation);
    auto nextOptions                  = options;
    nextOptions.memoryBytes           = 1024;
    nextOptions.deferredRetirement    = false;
    nextOptions.reserveRetainedMemory = false;
    if (FAILED(jobs.Submit(next, nextOptions, follower)))
        std::terminate();
    Expect("same-key-follower-queued-behind-original-publication-record", ! observation.followerStarted);
    if (! Until(jobs, routes, [&]() noexcept { return observation.finished && observation.followerFinished && ! display.HasPending() && jobs.IsIdle(); }))
        std::terminate();
    Expect("original-and-prior-cache-retirement-acknowledgements-join-before-charge-observation", jobs.IsIdle());
    Expect("all-useful-stages-and-final-payload-retirement-run-on-real-workers",
           observation.usefulWorkersOffOwner && observation.streamRetiredOffOwner && observation.payloadRetiredOffOwner && observation.runs == 5 &&
               observation.acknowledgements == 5);
    Expect("same-key-FIFO-unblocks-only-after-original-final-callback",
           observation.followerStarted && observation.followerAfterFinal && observation.followerFinished && observation.terminal.status == JobStatus::Finished);
    Expect("published-root-retains-exact-whole-parent-charge-after-original-wrapper-retirement",
           jobs.ReservedMemoryBytes() == kParentBytes && jobs.ReleasingMemoryBytes() == 0 && client.publications == 2 && client.errors == 0 && client.graph);
    std::vector<std::uint8_t> pixels;
    unsigned int width = 0, height = 0;
    const auto readback = client.graph->CompositionForTestOwner()->Readback(pixels, width, height);
    std::printf("INFO publication-parent.readback hr=0x%08x width=%u height=%u bytes=", static_cast<unsigned int>(readback), width, height);
    for (const auto value : pixels)
        std::printf("%u,", static_cast<unsigned int>(value));
    std::printf("\n");
    Expect("literal-premultiplied-half-opacity-pixels-match-independent-reference",
           SUCCEEDED(readback) && width == 2 && height == 2 &&
               pixels == std::vector<std::uint8_t>{32, 48, 64, 127, 32, 48, 64, 127, 32, 48, 64, 127, 32, 48, 64, 127});
    client.session.ClearDocument();
    display.RequestOwner();
    Expect("clear-excludes-published-pixels-before-original-retirement", ! client.graph);
    Expect("actual-last-use-barrier-and-worker-root-destruction-refund-whole-parent-charge",
           Until(jobs, routes, [&]() noexcept { return ! display.HasPending() && jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0; }));
    Expect("finished-parent-route-detaches-without-pending-work", routes.Detach(pump));
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
    jobs.Close({[](void* value) noexcept
    {
        auto& state = *static_cast<Closing*>(value);
        state.display.JoinedShutdownOwner();
        state.queue.CloseAfterWorkers();
    },
                [](void* value) noexcept { static_cast<Closing*>(value)->display.RetireShutdownForWork(); },
                &closing});
    Expect("actual-joined-close-returns-all-charges-and-controller-routes",
           display.CloseAfterWorkersOwner() && jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    return failures == 0;
}
} // namespace

int RunPublicationParentTests()
{
    const bool warp     = Run(D3D_DRIVER_TYPE_WARP);
    const bool hardware = Run(D3D_DRIVER_TYPE_HARDWARE);
    return warp && hardware && failures == 0 ? 0 : 1;
}

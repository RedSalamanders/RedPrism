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
int failures                   = 0;
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
    unsigned publications     = 0;
    bool finalCallbackSawRows = false, rowMetadataReady = false;

    Client(GpuWorkQueue& q, ID3D11Device* d, ID3D11DeviceContext* c, bool s) noexcept : queue(q), device(d), context(c), software(s)
    {
    }
    bool DescribeCommittedDisplayOwner(CommittedDisplayDescriptor& value) noexcept override
    {
        const auto authority = session.CaptureDocumentReadAuthority();
        if (! authority)
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
                finalCallbackSawRows = rowMetadataReady && rows && rows->Source().layers.Identity() == session.Document()->layers.Identity();
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
        source.width     = 3;
        source.height    = 1;
        source.selection = Rectangle(0);
        constexpr std::array<std::uint8_t, 12> pixels{0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0};
        auto draft               = CanvasDocumentDraft::Rebuild(source, 1);
        auto layer               = ImageLayer::Blank(L"Original pixel publication", {3, 1});
        const auto id            = layer.id;
        layer.transform.sampling = LayerSampling::Nearest;
        layer.asset              = RasterAsset{Raster::Make(3, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
        if (! draft || ! layer.asset->image.HasStorage() || ! draft->Append(std::move(layer)))
            return false;
        auto sealed = draft->Seal();
        if (! sealed)
            return false;
        session.ReplaceDocument(std::move(sealed.value()), id);
        session.SetSelection(Rectangle(2), L"Fixture redo");
        session.Undo();
        return session.Document() && session.Document()->selection == std::optional{Rectangle(0)} && session.history.CanRedo();
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
    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    CommittedDisplayController& _display;
    Client& _client;
    struct PixelPump& _pump;
    Observation& _observation;
    const Scenario _scenario;
    const std::uint32_t _ownerThread      = GetCurrentThreadId();
    const std::uint64_t _beforeGeneration = _client.session.Generation();
    const Guid _beforeRevision            = _client.session.history.CurrentRevision();
    const std::uint64_t _documentSerial   = _client.session.DocumentSerial();
    const void* _beforeLayers             = _client.session.Document()->layers.Identity();
    Stage _stage                          = Stage::Allocate;
    std::unique_ptr<OriginalDisplayPublication> _publication;
    std::shared_ptr<SharedPublicationMemory> _memory;
    std::shared_ptr<SharedPublicationMemory> _retirementMemory;
    std::optional<PixelMoveOpeningInput> _openingInput;
    std::optional<PreparedPixelMoveOpening> _opening;
    std::optional<PixelMoveClosureInput> _closeInput;
    std::optional<PreparedPixelMoveClosure> _closure;
    DocumentHistory::ResidencyPermit _residency;
    JobResult _terminal;

    bool Unchanged() const noexcept
    {
        return _client.session.Generation() == _beforeGeneration && _client.session.history.CurrentRevision() == _beforeRevision &&
               _client.session.Document()->layers.Identity() == _beforeLayers && _client.session.history.CanRedo() && _client.publications == 1 &&
               ! _client.rows;
    }
    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = next;
        if (! _jobs.Resume(_observation.original, lane))
            BoundaryFailure(__LINE__);
    }
    bool BeginCleanup() noexcept
    {
        _residency.Reset();
        if (! _observation.changed && _opening && _client.session.IsPreparedDeferredPixelMoveGestureCurrent(_opening.value()))
            if (! _client.session.AbortPreparedDeferredPixelMoveIfCurrent(_opening.value()))
                BoundaryFailure(__LINE__);
        // The original record retains its whole reservation through cleanup. The small accounting
        // alias transfers only in Completed, after every useful worker and graph destructor joined.
        if (! _publication->DetachForRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireGraph);
        return true;
    }

public:
    Parent(JobScheduler& j,
           GpuWorkQueue& q,
           GpuCommandPump& c,
           CommittedDisplayController& d,
           Client& cl,
           struct PixelPump& p,
           Observation& o,
           Scenario s) noexcept
        : _jobs(j),
          _queue(q),
          _commands(c),
          _display(d),
          _client(cl),
          _pump(p),
          _observation(o),
          _scenario(s)
    {
    }

    JobResult Run(JobContext& context) override
    {
        ++_observation.stages;
        if (GetCurrentThreadId() == _ownerThread || (_stage != Stage::RetireGraph && _stage != Stage::Destroy && context.reservedMemoryBytes != kBytes))
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_stage == Stage::Allocate)
        {
            const OriginalDisplayPublication::Bounds bounds{_scenario == Scenario::CpuRefused ? 1 : 384ull * 1024 * 1024,
                                                            _scenario == Scenario::GpuRefused ? 1 : 256ull * 1024 * 1024};
            _publication = std::make_unique<OriginalDisplayPublication>(_ownerThread, bounds);
            _memory      = SharedPublicationMemory::CreateForWork(kBytes);
        }
        else if (_stage == Stage::Open)
        {
            auto result = DocumentSession::PrepareDeferredPixelMoveOpening(std::move(_openingInput.value()), context.cancellation);
            _openingInput.reset();
            if (! result.prepared || result.status != PixelMoveLiftStatus::Ready)
                return {JobStatus::Failed, E_FAIL};
            _opening.emplace(std::move(result.prepared.value()));
        }
        else if (_stage == Stage::Prepare)
        {
            auto result = DocumentSession::PrepareDeferredPixelMoveClosure(std::move(_closeInput.value()), context.cancellation, _memory);
            _closeInput.reset();
            if (! result || ! result->FinalDocument())
                return {JobStatus::Failed, E_FAIL};
            _closure.emplace(std::move(result.value()));
            OriginalDisplayPublication::RowInput rows;
            rows.documentSerial = _documentSerial + (_scenario == Scenario::WrongRowEpoch ? 1u : 0u); // Captured scalar.
            rows.firstItemId    = PreparedLayerRows::FirstItemIdentity;
            rows.hiddenPattern  = L"{0} (hidden)";
            return _publication->PrepareForWork(*_closure->FinalDocument(), std::move(rows), context.cancellation);
        }
        else if (_stage == Stage::Record)
            return _publication->RecordForWork(context.cancellation);
        else if (_stage == Stage::Refresh)
            return _closure->RefreshHistoryResidency(context.cancellation) ? JobResult{} : JobResult{JobStatus::Failed, E_ABORT};
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_stage == Stage::Record)
            _publication->FinishRecordingForWork(result);
        else if (_stage == Stage::RetireTransport)
        {
            if (! _publication->RetireTransportForWork())
                BoundaryFailure(__LINE__);
            _observation.transportRetired = GetCurrentThreadId() != _ownerThread;
        }
        else if (_stage == Stage::RetireGraph)
        {
            if (! _publication->RetireGraphForWork())
                BoundaryFailure(__LINE__);
            _opening.reset();
            _closure.reset();
            _openingInput.reset();
            _closeInput.reset();
            _memory.reset();
            _observation.graphRetired = GetCurrentThreadId() != _ownerThread;
        }
        else if (_stage == Stage::Destroy)
        {
            _publication.reset();
            _observation.destroyed = GetCurrentThreadId() != _ownerThread;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != _observation.original)
            BoundaryFailure(__LINE__);
        if (_stage == Stage::Record)
        {
            if (! _publication->RecordingJoinedOwner())
                BoundaryFailure(__LINE__);
            if (result.status != JobStatus::Finished)
                _terminal = result;
            if (_scenario == Scenario::CancelAfterRecord)
            {
                _terminal = {JobStatus::Cancelled};
                _publication->StopOwner();
                if (! _jobs.Cancel(ticket))
                    BoundaryFailure(__LINE__);
                Expect("cancellation-keeps-original-memory-before-transport-retirement",
                       Unchanged() && _jobs.ReservedMemoryBytes() >= kBytes && ! _memory->IsBound());
            }
            _stage = Stage::AwaitTransport;
            _jobs.NotifyOwnerWork();
            return true;
        }
        if (_stage == Stage::RetireGraph)
        {
            if (! _publication->FinishRetirementOwner())
                BoundaryFailure(__LINE__);
            Resume(Stage::Destroy);
            return true;
        }
        if (_stage == Stage::Destroy)
            return false;
        if (result.status != JobStatus::Finished)
        {
            _terminal = result;
            Expect(_scenario == Scenario::CancelAfterRecord ? "cancelled-recording-preserves-committed-bytes-history-redo-rows-and-graph"
                                                            : "refused-final-preparation-preserves-committed-bytes-history-redo-rows-and-graph",
                   Unchanged());
            if (_scenario == Scenario::WrongRowEpoch)
                Expect("wrong-row-document-epoch-refuses-before-recording-or-row-publication",
                       result.error == E_INVALIDARG && ! _publication->HasRecorderOwner() && Unchanged());
            return BeginCleanup();
        }
        if (_stage == Stage::RetireTransport)
            _stage = Stage::Publish;
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_stage == Stage::Allocate)
        {
            CommittedDisplayDescriptor descriptor;
            if (! _client.DescribeCommittedDisplayOwner(descriptor) ||
                FAILED(_publication->CaptureOwner(_jobs, ticket, _queue, _commands, _display, descriptor)))
                BoundaryFailure(__LINE__);
            auto input = _client.session.PrepareDeferredPixelMoveOpeningInput(_scenario == Scenario::Duplicate);
            if (! input)
                BoundaryFailure(__LINE__);
            _retirementMemory = _memory;
            _openingInput.emplace(std::move(input.value()));
            Expect("original-admission-captures-source-only-in-its-joined-callback", Unchanged());
            Resume(Stage::Open);
            return true;
        }
        if (_stage == Stage::Open)
        {
            if (! _client.session.ApplyPreparedDeferredPixelMoveOpeningIfCurrent(_opening.value()) ||
                ! _client.session.MovePreparedDeferredPixelMoveIfCurrent(_opening.value(), 1, 0))
                BoundaryFailure(__LINE__);
            auto input = _client.session.PrepareDeferredPixelMoveClosureInput(_opening.value());
            if (! input)
                BoundaryFailure(__LINE__);
            _closeInput.emplace(std::move(input.value()));
            Expect("pending-opening-and-offset-preserve-committed-source-and-redo", Unchanged());
            Resume(Stage::Prepare);
            return true;
        }
        if (_stage == Stage::Prepare)
        {
            Expect("complete-final-document-and-rows-prepare-before-any-CPU-publication", Unchanged());
            const auto admitted = _publication->AdmitRecordingOwner();
            if (FAILED(admitted))
                BoundaryFailure(__LINE__); // The two selected bound failures occur before recording admission.
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        if (_stage == Stage::Publish || _stage == Stage::Refresh)
        {
            Expect("real-recording-and-transport-retire-before-CPU-history-rows-publication", Unchanged() && _observation.transportRetired);
            const auto decision = _closure->TryAcquireHistoryResidency(_residency);
            if (decision == DocumentHistory::ResidencyValidation::Refresh)
            {
                _residency.Reset();
                Resume(Stage::Refresh);
                return true;
            }
            if (decision != DocumentHistory::ResidencyValidation::Ready || FAILED(_memory->BindForPublication(_jobs, ticket)) ||
                FAILED(_publication->PreparePublicationOwner(_memory, *_closure->FinalDocument())))
                BoundaryFailure(__LINE__);
            Expect("exact-original-bound-memory-and-ready-private-GPU-do-not-publish-CPU-early", Unchanged() && _memory->BoundTo(_jobs, ticket));
            if (! _client.session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(_closure.value()) || ! _publication->AdoptOwner(_client.rows,
                                                                                                                                 [](void* raw) noexcept
            {
                auto& value            = *static_cast<Client*>(raw);
                value.rowMetadataReady = value.rows && value.rows->Source().layers.Identity() == value.session.Document()->layers.Identity();
            },
                                                                                                                                 &_client))
                BoundaryFailure(__LINE__);
            Expect("owner-row-metadata-is-installed-before-graph-callback", _client.rowMetadataReady && _client.finalCallbackSawRows);
            _residency.Reset();
            _observation.changed = true;
            Expect("one-checked-CPU-history-swap-adopts-prepared-rows-before-graph-callback",
                   _client.session.Generation() == _beforeGeneration + 1 && ! _client.session.history.CanRedo() && _client.session.history.CanUndo() &&
                       _client.publications == 2 && _client.finalCallbackSawRows && _client.rows && _client.rows->HasMemoryLease() &&
                       _publication->AdoptedOwner());
            return BeginCleanup();
        }
        return false;
    }
    bool Ready() const noexcept
    {
        return _stage == Stage::AwaitTransport && _publication->TransportReadyOwner();
    }
    void Advance() noexcept
    {
        if (! Ready() || ! _publication->AuthorizeTransportRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireTransport);
    }
    void Completed(JobResult result) noexcept override
    {
        if (_stage != Stage::Destroy || _publication)
            BoundaryFailure(__LINE__);
        if (! _retirementMemory || (! _retirementMemory->IsBound() && FAILED(_retirementMemory->BindForRetirement(_jobs, _observation.original))))
            BoundaryFailure(__LINE__);
        _observation.outcome  = _terminal.status == JobStatus::Finished ? result : _terminal;
        _observation.finished = true;
        _pump.parent          = nullptr;
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
    bool captured                = false;
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
        if (GetCurrentThreadId() == owner || ! originalJoined || ! wrapperDestroyed || ! retired)
            BoundaryFailure(__LINE__);
        chargeHeldOnWorker = retired->memory && retired->memory->IsBound() && retired->memory->Bytes() == kBytes;
        if (retired->captured && ! retired->publication->RetireJoinedShutdownForWork())
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
    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    CommittedDisplayController& _display;
    Client& _client;
    CloseState& _state;
    const ClosePoint _point;
    OwnerWorkMultiplexer& _routes;
    Stage _stage = Stage::Allocate;
    JobTicket _ticket;
    std::unique_ptr<ClosePayload> _payload;

    bool Hold(ClosePoint boundary) noexcept
    {
        if (boundary != _point)
            return false;
        _state.ready = true;
        return true;
    }
    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = next;
        if (! _jobs.Resume(_ticket, lane))
            BoundaryFailure(__LINE__);
    }

public:
    CloseParent(JobScheduler& j,
                GpuWorkQueue& q,
                GpuCommandPump& c,
                CommittedDisplayController& d,
                Client& cl,
                CloseState& s,
                ClosePoint p,
                OwnerWorkMultiplexer& mux) noexcept
        : _jobs(j),
          _queue(q),
          _commands(c),
          _display(d),
          _client(cl),
          _state(s),
          _point(p),
          _routes(mux)
    {
    }
    ~CloseParent() override
    {
        if (_payload)
            BoundaryFailure(__LINE__);
        _state.wrapperDestroyed = true;
    }
    JobResult Run(JobContext& context) override
    {
        if (GetCurrentThreadId() == _state.owner || context.reservedMemoryBytes != kBytes)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_stage == Stage::Allocate)
        {
            _payload = std::make_unique<ClosePayload>();
            _payload->publication =
                std::make_unique<OriginalDisplayPublication>(_state.owner, OriginalDisplayPublication::Bounds{384ull * 1024 * 1024, 256ull * 1024 * 1024});
            _payload->memory = SharedPublicationMemory::CreateForWork(kBytes);
        }
        else if (_stage == Stage::Prepare)
        {
            auto draft = CanvasDocumentDraft::Rebuild(*_payload->input, 1);
            auto layer = (*_payload->input).layers[0];
            constexpr std::array<std::uint8_t, 12> pixels{0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0};
            layer.asset->image = Raster::Make(3, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
            if (! draft || ! layer.asset->image.HasStorage() || ! draft->Append(std::move(layer)))
                return {JobStatus::Failed, E_FAIL};
            _payload->final = draft->Seal();
            if (! _payload->final)
                return {JobStatus::Failed, E_FAIL};
            OriginalDisplayPublication::RowInput rows;
            rows.documentSerial = _payload->documentSerial; // Exact original owner-captured row epoch.
            rows.firstItemId    = PreparedLayerRows::FirstItemIdentity;
            rows.hiddenPattern  = L"{0} (hidden)";
            return _payload->publication->PrepareForWork(*_payload->final, std::move(rows), context.cancellation);
        }
        else if (_stage == Stage::Record)
            return _payload->publication->RecordForWork(context.cancellation);
        return {};
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        const bool hold = (_stage == Stage::Allocate && _point == ClosePoint::AllocateWorker) ||
                          (_stage == Stage::Prepare && _point == ClosePoint::PrepareWorker) ||
                          (_stage == Stage::Record && _point == ClosePoint::RecordWorker) ||
                          (_stage == Stage::RetireTransport && _point == ClosePoint::RetireTransportWorker);
        if (hold)
        {
            _state.heldProducer.store(true, std::memory_order_release);
            _state.ready.store(true, std::memory_order_release);
            _jobs.NotifyOwnerWork();
            // Deterministic test gate: Close signals this actual scheduler event, then joins this producer.
            // No owner wait, forged completion, Sleep or performance assertion.
            if (WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
                BoundaryFailure(__LINE__);
            _state.shutdownObserved.store(true, std::memory_order_release);
        }
        if (_stage == Stage::Record)
            _payload->publication->FinishRecordingForWork(result);
        else if (_stage == Stage::RetireTransport && ! _payload->publication->RetireTransportForWork())
            BoundaryFailure(__LINE__);
    }
    bool DeferRetirement(JobResult result, JobTicket original) noexcept override
    {
        if (result.status != JobStatus::Finished || (_ticket.generation && _ticket != original))
            BoundaryFailure(__LINE__);
        if (_stage == Stage::Record)
        {
            if (! _payload->publication->RecordingJoinedOwner())
                BoundaryFailure(__LINE__);
            if (! Hold(ClosePoint::ProducerJoined))
            {
                _stage = Stage::AwaitTransport;
                _jobs.NotifyOwnerWork();
            }
            return true;
        }
        if (_stage == Stage::RetireTransport)
        {
            if (! Hold(ClosePoint::TransportRetired))
                Resume(Stage::Publish);
            return true;
        }
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket original) noexcept override
    {
        _ticket = original;
        if (_stage == Stage::Allocate)
        {
            if (Hold(ClosePoint::Allocated))
                return true;
            CommittedDisplayDescriptor descriptor;
            if (! _client.DescribeCommittedDisplayOwner(descriptor) ||
                FAILED(_payload->publication->CaptureOwner(_jobs, _ticket, _queue, _commands, _display, descriptor)))
                BoundaryFailure(__LINE__);
            _payload->captured       = true;
            _payload->documentSerial = descriptor.key.document.documentSerial;
            if (Hold(ClosePoint::Captured))
                return true;
            _payload->input = _client.session.Document();
            Resume(Stage::Prepare);
            return true;
        }
        if (_stage == Stage::Prepare)
        {
            if (Hold(ClosePoint::Prepared))
                return true;
            if (FAILED(_payload->publication->AdmitRecordingOwner()))
                BoundaryFailure(__LINE__);
            if (! Hold(ClosePoint::RecorderAdmitted))
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        if (_stage == Stage::Publish)
        {
            if (FAILED(_payload->memory->BindForPublication(_jobs, _ticket)) ||
                FAILED(_payload->publication->PreparePublicationOwner(_payload->memory, *_payload->final)) || ! Hold(ClosePoint::PrivatePublished))
                BoundaryFailure(__LINE__);
            return true;
        }
        BoundaryFailure(__LINE__);
    }
    bool HasPending() const noexcept override
    {
        return ! _state.originalJoined;
    }
    bool HasReady() const noexcept override
    {
        return ! _state.ready && _stage == Stage::AwaitTransport && _payload && _payload->publication->TransportReadyOwner();
    }
    void Pump() noexcept override
    {
        if (! HasReady() || ! _payload->publication->AuthorizeTransportRetirementOwner())
            BoundaryFailure(__LINE__);
        Resume(Stage::RetireTransport);
    }
    bool RefusesUsefulAuthorityOwner() noexcept
    {
        CommittedDisplayDescriptor descriptor;
        std::shared_ptr<const PreparedLayerRows> rows;
        return _payload && _client.DescribeCommittedDisplayOwner(descriptor) &&
               FAILED(_payload->publication->CaptureOwner(_jobs, _ticket, _queue, _commands, _display, descriptor)) &&
               FAILED(_payload->publication->PreparePublicationOwner(_payload->memory, *_client.session.Document())) &&
               ! _payload->publication->AdoptOwner(rows) && ! rows;
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept override
    {
        if (&scheduler != &_jobs || ! _jobs.InJoinedRetirementCallback(original) || ! _payload || _state.retired || ! _state.ready ||
            (_ticket.generation && _ticket != original))
            BoundaryFailure(__LINE__);
        if (! _payload->memory->IsBound() && FAILED(_payload->memory->BindForRetirement(_jobs, original)))
            BoundaryFailure(__LINE__);
        if (_payload->captured && ! _payload->publication->JoinedShutdownOwner(_state.gpu))
            BoundaryFailure(__LINE__);
        _state.retired        = std::move(_payload);
        _state.originalJoined = true;
        // Actual scheduler Close joined all producers before invoking this exact hook. Only now may
        // the borrowing route detach, before its owner-affine job wrapper is destroyed.
        if (! _routes.Detach(*this))
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
    std::printf("INFO original-display-close.driver=%s prefix=%u\n", driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", static_cast<unsigned>(point));
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
    auto ownedClient = std::make_unique<Client>(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    auto& client     = *ownedClient;
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) || ! jobs.EnableDeferredRelease() ||
        ! jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), context.get())) ||
        FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        FAILED(display.InitializeOwner(jobs, queue, commands, routes, client, nullptr, false, false)) || ! client.Initialize())
        BoundaryFailure(__LINE__);
    display.RequestOwner();
    if (! Until(jobs, routes, [&]() noexcept { return client.graph != nullptr; }))
        BoundaryFailure(__LINE__);
    const auto generation = client.session.Generation();
    const auto revision   = client.session.history.CurrentRevision();
    const auto root       = client.session.Document()->layers.Identity();
    CloseState state;
    auto implementation = std::make_unique<CloseParent>(jobs, queue, commands, display, client, state, point, routes);
    auto* pump          = implementation.get();
    if (! routes.Attach(*pump))
        BoundaryFailure(__LINE__);
    std::unique_ptr<Job> job = std::move(implementation);
    JobOptions options;
    options.kind                  = JobClass::Selection;
    options.memoryBytes           = kBytes;
    options.deferredRetirement    = true;
    options.reserveRetainedMemory = true;
    JobTicket ticket;
    if (FAILED(jobs.Submit(job, options, ticket)) || ! Until(jobs, routes, [&]() noexcept { return state.ready.load(std::memory_order_acquire); }))
        BoundaryFailure(__LINE__);
    Expect("close-prefix-holds-original-whole-charge-with-no-CPU-history-rows-publication",
           jobs.ReservedMemoryBytes() >= kBytes && client.session.Generation() == generation && client.session.history.CurrentRevision() == revision &&
               client.session.Document()->layers.Identity() == root && client.session.history.CanRedo() && client.publications == 1 && ! client.rows);
    Expect("close-prefix-useful-capture-and-publication-refuse-outside-exact-successful-callback", pump->RefusesUsefulAuthorityOwner());
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
        if (! value.state.originalJoined || ! value.state.wrapperDestroyed)
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
           state.originalJoined && state.wrapperDestroyed && state.workerRetired && state.chargeHeldOnWorker && ! state.retired);
    const bool heldPrefix = point == ClosePoint::AllocateWorker || point == ClosePoint::PrepareWorker || point == ClosePoint::RecordWorker ||
                            point == ClosePoint::RetireTransportWorker;
    Expect("close-prefix-held-producer-joins-after-actual-scheduler-shutdown",
           state.heldProducer.load() == heldPrefix && state.shutdownObserved.load() == heldPrefix);
    Expect("close-prefix-never-applies-document-history-or-rows",
           client.session.Generation() == generation && client.session.history.CurrentRevision() == revision &&
               client.session.Document()->layers.Identity() == root && client.session.history.CanRedo() && ! client.rows);
    state.gpu.ResetOwner();
    const bool displayClosed  = display.CloseAfterWorkersOwner();
    const bool commandsClosed = commands.CloseOwner();
    const auto reserved = jobs.ReservedMemoryBytes(), releasing = jobs.ReleasingMemoryBytes();
    if (! displayClosed || ! commandsClosed || reserved || releasing)
        std::printf("DIAG close-prefix displayClosed=%u commandsClosed=%u reserved=%llu releasing=%llu sources=%zu\n",
                    displayClosed ? 1u : 0u,
                    commandsClosed ? 1u : 0u,
                    static_cast<unsigned long long>(reserved),
                    static_cast<unsigned long long>(releasing),
                    routes.SourceCountForTest());
    Expect("close-prefix-refunds-only-after-final-worker-and-detaches-command-routes", displayClosed && commandsClosed && reserved == 0 && releasing == 0);
    return true;
}

bool Run(D3D_DRIVER_TYPE driver, Scenario scenario)
{
    std::printf("INFO original-display-pixel.driver=%s scenario=%u\n", driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", static_cast<unsigned>(scenario));
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
    struct PixelPump pump;
    auto ownedClient = std::make_unique<Client>(queue, device.get(), context.get(), driver == D3D_DRIVER_TYPE_WARP);
    auto& client     = *ownedClient;
    if (FAILED(jobs.Initialize(2, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) || ! jobs.EnableDeferredRelease() ||
        ! jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), context.get())) ||
        FAILED(commands.InitializeOwner(jobs, queue, routes)) || FAILED(display.InitializeOwner(jobs, queue, commands, routes, client)) ||
        ! routes.Attach(pump) || ! client.Initialize())
        BoundaryFailure(__LINE__);
    display.RequestOwner();
    if (! Until(jobs, routes, [&]() noexcept { return client.graph != nullptr; }))
        BoundaryFailure(__LINE__);
    Observation observation;
    auto implementation      = std::make_unique<Parent>(jobs, queue, commands, display, client, pump, observation, scenario);
    pump.parent              = implementation.get();
    std::unique_ptr<Job> job = std::move(implementation);
    JobOptions options;
    options.kind                  = JobClass::Selection;
    options.serialKey             = 23;
    options.memoryBytes           = kBytes;
    options.deferredRetirement    = true;
    options.reserveRetainedMemory = true;
    if (FAILED(jobs.Submit(job, options, observation.original)) ||
        ! Until(jobs, routes, [&]() noexcept { return observation.finished && ! display.HasPending() && jobs.IsIdle(); }))
        BoundaryFailure(__LINE__);
    const bool success = scenario == Scenario::Move || scenario == Scenario::Duplicate;
    Expect("terminal-status-and-worker-retirement-match-selected-boundary",
           observation.graphRetired && observation.destroyed && observation.changed == success &&
               observation.outcome.status == (success                                   ? JobStatus::Finished
                                              : scenario == Scenario::CancelAfterRecord ? JobStatus::Cancelled
                                                                                        : JobStatus::Failed) &&
               (success || scenario == Scenario::CancelAfterRecord ||
                (scenario == Scenario::WrongRowEpoch ? observation.outcome.error == E_INVALIDARG : observation.outcome.error == E_OUTOFMEMORY)));
    if (success)
    {
        std::vector<std::uint8_t> pixels;
        unsigned width = 0, height = 0;
        const auto status   = client.graph->CompositionForTestOwner()->Readback(pixels, width, height);
        const auto expected = scenario == Scenario::Duplicate ? std::vector<std::uint8_t>{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0}
                                                              : std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0};
        Expect("real-final-GPU-pixels-match-independent-move-or-duplicate-literal", SUCCEEDED(status) && width == 3 && height == 1 && pixels == expected);
        Expect("row-reader-keeps-original-whole-charge-after-parent-retirement", jobs.ReservedMemoryBytes() == kBytes && client.rows);
        client.session.Undo();
        std::array<std::uint8_t, 12> restored{};
        const auto* restoredLayer = client.session.ActiveLayer();
        if (restoredLayer && restoredLayer->asset)
            restoredLayer->asset->image.ReadRegion(0, 0, 3, 1, restored.data(), 12, 0);
        const std::array<std::uint8_t, 12> original{0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0};
        Expect("one-Undo-restores-blue-neighbour-and-redo",
               restored == original && client.session.history.CanRedo() && client.session.Document()->selection == std::optional{Rectangle(0)});
        client.session.Redo();
        Expect("one-Redo-restores-moved-selection", client.session.Document()->selection == std::optional{Rectangle(1)});
    }
    client.session.ClearDocument();
    display.RequestOwner();
    client.rows.reset();
    if (! Until(jobs, routes, [&]() noexcept { return ! display.HasPending() && jobs.IsIdle() && jobs.ReservedMemoryBytes() == 0; }))
        BoundaryFailure(__LINE__);
    Expect("real-last-use-and-worker-retirement-refund-every-original-charge", jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0);
    if (! routes.Detach(pump))
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
                [](void* p) noexcept { static_cast<Closing*>(p)->display.RetireShutdownForWork(); },
                &closing});
    Expect("joined-close-detaches-routes-and-leaves-no-charge", display.CloseAfterWorkersOwner() && jobs.ReservedMemoryBytes() == 0);
    return true;
}
} // namespace

int RunOriginalDisplayPixelPublicationTests()
{
    for (const auto driver : {D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE})
        for (const auto point : {ClosePoint::Allocated,
                                 ClosePoint::Captured,
                                 ClosePoint::Prepared,
                                 ClosePoint::RecorderAdmitted,
                                 ClosePoint::ProducerJoined,
                                 ClosePoint::TransportRetired,
                                 ClosePoint::PrivatePublished,
                                 ClosePoint::AllocateWorker,
                                 ClosePoint::PrepareWorker,
                                 ClosePoint::RecordWorker,
                                 ClosePoint::RetireTransportWorker})
            if (! RunClose(driver, point))
                ++failures;

    for (const auto driver : {D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE})
        for (const auto scenario :
             {Scenario::Move, Scenario::Duplicate, Scenario::CpuRefused, Scenario::GpuRefused, Scenario::CancelAfterRecord, Scenario::WrongRowEpoch})
            if (! Run(driver, scenario))
                ++failures;
    return failures == 0 ? 0 : 1;
}

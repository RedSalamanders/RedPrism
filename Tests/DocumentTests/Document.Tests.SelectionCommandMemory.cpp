#include "Document.Tests.SelectionCommandMemory.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "Jobs.h"
#include "Selection.h"
#include "SelectionCommandWork.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <windows.h>

namespace
{
constexpr std::uint64_t kHeadroomBytes = 16ull * 1024ull * 1024ull * 1024ull;
constexpr std::array<Vec2, 4> kSquare{{{0, 0}, {8, 0}, {8, 8}, {0, 8}}};
unsigned int gAssertions = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s selection-command-memory.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0u : 1u;
}

bool HasSquare(const std::optional<DocumentSelection>& selection) noexcept
{
    if (!selection || !selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& points = selection->loops.Get()[0];
    if (points.size() != kSquare.size())
        return false;
    for (std::size_t index = 0; index < kSquare.size(); ++index)
    {
        if (points[index] != kSquare[index])
            return false;
    }
    return true;
}

bool HasSmallSquare(const std::optional<DocumentSelection>& selection) noexcept
{
    constexpr std::array<Vec2, 4> expected{{{3, 3}, {4, 3}, {4, 4}, {3, 4}}};
    if (!selection || selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& points = selection->loops.Get()[0];
    if (points.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (points[index] != expected[index])
            return false;
    }
    return true;
}

struct Fixture final
{
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    bool ready = false;

    explicit Fixture(bool initiallySelected)
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 8;
        if (initiallySelected)
            document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 8, 8)};
        auto layer = ImageLayer::Blank(L"Selection command memory", Vec2{8, 8});
        const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
        layer.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size()), {}, layer.name};
        const Guid active = layer.id;
        document.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(document).Seal(), active);
        session->SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{{1, 1}, {2, 1}, {2, 2}, {1, 2}}}},
                              L"Seed redo");
        session->Undo();
        ready = session->Document().has_value() && session->history.CanRedo() &&
                session->history.RedoName() == L"Seed redo" &&
                (initiallySelected ? HasSquare(session->Selection()) : !session->Selection());
    }
};

struct MemoryProbe final
{
    MemorySnapshot snapshot{kMemoryAdmissionHeadroomBytes + kHeadroomBytes,
                            kMemoryAdmissionHeadroomBytes + kHeadroomBytes};
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    snapshot = static_cast<MemoryProbe*>(context)->snapshot;
    return true;
}

bool StartScheduler(JobScheduler& scheduler, MemoryProbe& probe) noexcept
{
    return scheduler.Initialize(1, {}, {SampleMemory, &probe}) == S_OK && scheduler.EnableDeferredRelease();
}

bool Settle(JobScheduler& scheduler) noexcept
{
    return SettleJobs(scheduler, 10000) == SettleResult::Settled;
}

enum class BindMode : std::uint8_t
{
    Exact,
    Unbound,
    Underbound,
    StaleAfterBind,
};

struct CommandState final
{
    JobTicket ticket{};
    SelectionCommandKind kind = SelectionCommandKind::Deselect;
    BindMode binding = BindMode::Exact;
    JobResult result{};
    std::weak_ptr<const SharedPublicationMemory> memory;
    std::weak_ptr<const void> geometry;
    std::uint64_t reservationBytes = 0;
    std::uint64_t outputBytes = 0;
    std::uint64_t boundBytes = 0;
    std::uint64_t localMetadataBytes = 0;
    std::uint64_t geometryStorageBytes = 0;
    std::uint64_t geometryOwnershipAllowance = 0;
    std::uint64_t publicationAllowance = 0;
    std::uint64_t staleGeneration = 0;
    Guid staleRevision{};
    std::uint32_t workerStages = 0;
    bool captured = false;
    bool resumed = false;
    bool boundToOriginal = false;
    bool completed = false;
    bool applied = false;
    bool staleAuthorityRejected = false;
};

class SelectionCommandJob final : public Job
{
  public:
    SelectionCommandJob(DocumentSession& session, JobScheduler& scheduler, CommandState& state) noexcept
        : _session(session), _scheduler(scheduler), _state(state)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        ++_state.workerStages;
        if (!_started)
        {
            _memory = SharedPublicationMemory::CreateForWork();
            _temporary = SharedPublicationMemory::CreateForWork();
            _state.memory = _memory;
            _started = true;
            return {};
        }
        if (!_input)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto prepared = DocumentSession::PrepareSelectionCommandPublication(std::move(_input.value()),
                                                                            context.cancellation, {}, _memory);
        _input.reset();
        if (!prepared)
            return {context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Failed,
                    context.cancellation.IsCancelled() ? 0 : E_FAIL};
        _prepared.emplace(std::move(prepared.value()));
        _state.outputBytes = _prepared->OutputMemoryBytes();
        _state.localMetadataBytes = _memory->LocalAllocationBytes();
        _state.publicationAllowance = SharedPublicationMemory::OwnershipAllowance();
        if (_prepared->ChangesSelection() != (_state.outputBytes != 0))
            return {JobStatus::Failed, E_OUTOFMEMORY};
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (!(ticket == _state.ticket) || result.status != JobStatus::Finished)
            return false;
        if (!_state.captured)
        {
            try
            {
                auto input = _session.PrepareSelectionCommandInput(_state.kind);
                if (!input)
                    return false;
                _input.emplace(std::move(input.value()));
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            _state.captured = true;
            _state.resumed = _scheduler.Resume(ticket) && ticket == _state.ticket;
            return _state.resumed;
        }
        if (!_prepared || !_memory || !_temporary || _state.outputBytes == 0)
            return false;
        if (_state.binding == BindMode::Unbound)
            return false;
        const std::uint64_t capacity =
            _state.binding == BindMode::Underbound ? _state.outputBytes - 1 : _state.outputBytes;
        if (capacity < SharedPublicationMemory::OwnershipAllowance() || capacity >= _state.reservationBytes)
            return false;
        if (_memory->BindPersistentOutputForPublication(_scheduler, ticket, capacity, *_temporary) != S_OK)
            return false;
        _state.boundBytes = capacity;
        _state.boundToOriginal =
            _memory->BoundTo(_scheduler, ticket) && _memory->IsBound() && _memory->Bytes() == capacity;
        if (_state.binding == BindMode::StaleAfterBind)
        {
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{SelectionLoop{{3, 3}, {4, 3}, {4, 4}, {3, 4}}}},
                L"Stale command interleave");
            _state.staleGeneration = _session.Generation();
            _state.staleRevision = _session.history.CurrentRevision();
        }
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.completed = true;
        if (result.status == JobStatus::Finished && _prepared)
        {
            _state.staleAuthorityRejected = _state.binding != BindMode::StaleAfterBind ||
                                            !_session.IsPreparedSelectionCommandCurrent(_prepared.value());
            _state.applied = _session.ApplyPreparedSelectionCommandIfCurrent(_prepared.value());
            if (_state.applied && _session.Selection())
            {
                _state.geometryStorageBytes = _session.Selection().value().loops.StorageBytes();
                _state.geometryOwnershipAllowance =
                    SelectionLoops::WorkOwnershipAllowance(_session.Selection()->loops.size());
                _state.geometry = _session.Selection().value().loops.WeakStorageForTest();
            }
        }
    }

  private:
    DocumentSession& _session;
    JobScheduler& _scheduler;
    CommandState& _state;
    std::shared_ptr<SharedPublicationMemory> _memory;
    std::shared_ptr<SharedPublicationMemory> _temporary;
    std::optional<SelectionCommandInput> _input;
    std::optional<PreparedSelectionCommand> _prepared;
    bool _started = false;
};

std::uint64_t Submit(JobScheduler& scheduler, DocumentSession& session, CommandState& state)
{
    state.reservationBytes = session.SelectionCommandMemoryBytes(state.kind);
    if (state.reservationBytes == 0)
        return 0;
    std::unique_ptr<Job> job = std::make_unique<SelectionCommandJob>(session, scheduler, state);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 1;
    options.memoryBytes = state.reservationBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    const std::int32_t submitted = scheduler.Submit(job, options, state.ticket);
    return submitted == S_OK && !job ? state.reservationBytes : 0;
}

void TestSelectedCommand(SelectionCommandKind kind)
{
    MemoryProbe probe;
    JobScheduler scheduler;
    const bool schedulerReady = StartScheduler(scheduler, probe);
    Expect(schedulerReady, "scheduler-initializes");
    if (!schedulerReady)
    {
        scheduler.Close();
        return;
    }
    auto fixture = std::make_unique<Fixture>(kind == SelectionCommandKind::Deselect);
    Expect(fixture->ready, "fixture-has-literal-state-and-redo");
    if (!fixture->ready)
    {
        scheduler.Close();
        return;
    }
    const auto before = fixture->session->history.CaptureState();
    const auto revision = before.Revision();
    const int depth = before.Depth();
    const auto generation = fixture->session->Generation();
    const SelectionLoops literalRect{SelectionGeometry::Rect(0, 0, 8, 8)};
    const std::uint64_t literalGeometryBytes = literalRect.StorageBytes();
    CommandState state;
    state.kind = kind;
    const auto reservation = Submit(scheduler, *fixture->session, state);
    Expect(reservation != 0 && reservation == state.reservationBytes, "submits-original-retained-ticket");
    const bool settled = reservation && Settle(scheduler);
    Expect(settled && state.completed && state.result.status == JobStatus::Finished && state.workerStages == 2 &&
               state.captured && state.resumed && state.boundToOriginal && state.applied && state.outputBytes > 0 &&
               state.boundBytes == state.outputBytes && state.outputBytes < state.reservationBytes,
           kind == SelectionCommandKind::SelectAll ? "select-all-binds-exact-output-before-publication"
                                                   : "deselect-binds-exact-output-before-publication");
    if (settled && state.applied)
    {
        const auto after = fixture->session->history.CaptureState();
        const bool changed = kind == SelectionCommandKind::SelectAll ? HasSquare(fixture->session->Selection())
                                                                     : !fixture->session->Selection();
        const bool geometryBound =
            kind == SelectionCommandKind::SelectAll
                ? literalGeometryBytes > 0 && state.geometryStorageBytes == literalGeometryBytes &&
                      state.geometryOwnershipAllowance == SelectionLoops::WorkOwnershipAllowance(1)
                : state.geometryStorageBytes == 0 && state.geometryOwnershipAllowance == 0;
        const auto completeGeometryBytes = state.geometryStorageBytes + state.geometryOwnershipAllowance;
        const bool independentlyBound =
            state.localMetadataBytes <= (std::numeric_limits<std::uint64_t>::max)() - state.publicationAllowance &&
            state.localMetadataBytes + state.publicationAllowance <=
                (std::numeric_limits<std::uint64_t>::max)() - completeGeometryBytes &&
            state.localMetadataBytes + state.publicationAllowance + completeGeometryBytes == state.outputBytes;
        Expect(changed && geometryBound && independentlyBound && !fixture->session->history.IsCurrent(before) &&
                   after.Revision() != revision && after.Revision() == fixture->session->history.CurrentRevision() &&
                   after.Depth() == depth && after.PastCount() == before.PastCount() + 1 && after.FutureCount() == 0 &&
                   fixture->session->Generation() != generation,
               kind == SelectionCommandKind::SelectAll ? "select-all-publishes-aa-literal-full-canvas-history"
                                                       : "deselect-publishes-empty-history-entry");
        const bool undo = fixture->session->history.CanUndo();
        fixture->session->Undo();
        const bool undoRestored = kind == SelectionCommandKind::SelectAll ? !fixture->session->Selection()
                                                                          : HasSquare(fixture->session->Selection());
        const bool redo = fixture->session->history.CanRedo();
        fixture->session->Redo();
        const bool redoRestored = kind == SelectionCommandKind::SelectAll ? HasSquare(fixture->session->Selection())
                                                                          : !fixture->session->Selection();
        Expect(undo && undoRestored && redo && redoRestored, "command-undo-redo-restores-literal-selection");
    }
    if (kind == SelectionCommandKind::SelectAll && settled && state.applied)
    {
        Expect(!state.geometry.expired() && scheduler.ReservedMemoryBytes() >= state.outputBytes,
               "select-all-geometry-weak-control-retains-output-charge");
        fixture->session.reset();
        const bool retired = Settle(scheduler);
        Expect(retired && state.geometry.expired() && !state.memory.expired() &&
                   scheduler.ReservedMemoryBytes() >= state.outputBytes,
               "expired-geometry-weak-control-retains-output-charge-after-session-drop");
        // Drop the diagnostic memory weak observer first: the geometry control is then the actual last
        // owner of that publication control. An expired weak memory observer would itself retain its lease.
        state.memory.reset();
        state.geometry.reset();
        const bool refunded = Settle(scheduler);
        Expect(refunded && state.memory.expired() && scheduler.ReservedMemoryBytes() == 0,
               "last-geometry-weak-control-release-refunds-output-charge");
    }
    scheduler.Close();
}

void TestRefusal(BindMode mode)
{
    MemoryProbe probe;
    JobScheduler scheduler;
    const bool schedulerReady = StartScheduler(scheduler, probe);
    Expect(schedulerReady, "refusal-scheduler-initializes");
    if (!schedulerReady)
    {
        scheduler.Close();
        return;
    }
    auto fixture = std::make_unique<Fixture>(false);
    const auto before = fixture->session->history.CaptureState();
    const auto generation = fixture->session->Generation();
    CommandState state;
    state.kind = SelectionCommandKind::SelectAll;
    state.binding = mode;
    const auto reservation = Submit(scheduler, *fixture->session, state);
    const bool settled = reservation && Settle(scheduler);
    if (mode == BindMode::StaleAfterBind)
    {
        Expect(settled && state.completed && state.result.status == JobStatus::Finished && !state.applied &&
                   state.boundToOriginal && state.boundBytes == state.outputBytes && state.staleAuthorityRejected &&
                   HasSmallSquare(fixture->session->Selection()) &&
                   fixture->session->Generation() == state.staleGeneration &&
                   fixture->session->history.CurrentRevision() == state.staleRevision,
               "stale-supplied-memory-output-refuses-after-exact-bind-without-overwrite");
    }
    else
    {
        Expect(settled && state.completed && state.result.status == JobStatus::Finished && !state.applied &&
                   !fixture->session->Selection() && fixture->session->history.IsCurrent(before) &&
                   fixture->session->Generation() == generation,
               mode == BindMode::Unbound ? "unbound-output-refuses-before-mutation"
                                         : "underbound-complete-output-refuses-before-mutation");
    }
    fixture.reset();
    state.geometry.reset();
    state.memory.reset();
    const bool refunded = Settle(scheduler);
    Expect(refunded && scheduler.ReservedMemoryBytes() == 0, "refusal-output-retires-and-refunds-original-charge");
    scheduler.Close();
}

void TestNoOpCancellationAndStale()
{
    auto empty = std::make_unique<Fixture>(false);
    auto full = std::make_unique<Fixture>(true);
    const std::array<std::pair<Fixture*, SelectionCommandKind>, 2> noOps{
        {{empty.get(), SelectionCommandKind::Deselect}, {full.get(), SelectionCommandKind::SelectAll}}};
    for (const auto& [fixture, kind] : noOps)
    {
        for (const bool supplyMemory : {false, true})
        {
            const auto before = fixture->session->history.CaptureState();
            const auto generation = fixture->session->Generation();
            std::shared_ptr<SharedPublicationMemory> memory =
                supplyMemory ? SharedPublicationMemory::CreateForWork() : nullptr;
            auto input = fixture->session->PrepareSelectionCommandInput(kind);
            auto prepared =
                input ? DocumentSession::PrepareSelectionCommandPublication(std::move(input.value()), {}, {}, memory)
                      : std::optional<PreparedSelectionCommand>{};
            const bool memoryUncharged = !supplyMemory || (memory && !memory->IsBound() && memory->Bytes() == 0 &&
                                                           memory->LocalAllocationBytes() == 0);
            Expect(prepared && !prepared->ChangesSelection() && prepared->OutputMemoryBytes() == 0 &&
                       fixture->session->ApplyPreparedSelectionCommandIfCurrent(prepared.value()) &&
                       fixture->session->history.IsCurrent(before) && fixture->session->history.CanRedo() &&
                       fixture->session->Generation() == generation && memoryUncharged,
                   kind == SelectionCommandKind::SelectAll
                       ? (supplyMemory ? "noop-select-all-supplied-memory-zero-unaccounted-preserves-redo"
                                       : "noop-select-all-zero-output-preserves-redo-generation")
                       : (supplyMemory ? "noop-deselect-supplied-memory-zero-unaccounted-preserves-redo"
                                       : "noop-deselect-zero-output-preserves-redo-generation"));
        }
    }
    auto input = empty->session->PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
    CancelSource cancelled;
    cancelled.Cancel();
    const auto beforeCancelled = empty->session->history.CaptureState();
    const auto generationBeforeCancelled = empty->session->Generation();
    auto cancelledMemory = SharedPublicationMemory::CreateForWork();
    auto cancelledResult = input ? DocumentSession::PrepareSelectionCommandPublication(
                                       std::move(input.value()), cancelled.Token(), {}, cancelledMemory)
                                 : std::optional<PreparedSelectionCommand>{};
    Expect(!cancelledResult && !empty->session->Selection() && empty->session->history.IsCurrent(beforeCancelled) &&
               empty->session->history.CanRedo() && empty->session->Generation() == generationBeforeCancelled &&
               cancelledMemory && !cancelledMemory->IsBound() && cancelledMemory->Bytes() == 0 &&
               cancelledMemory->LocalAllocationBytes() == 0,
           "pre-cancelled-supplied-memory-command-refuses-without-mutation-or-charge");

    input = empty->session->PrepareSelectionCommandInput(SelectionCommandKind::SelectAll);
    auto staleMemory = SharedPublicationMemory::CreateForWork();
    auto stale =
        input ? DocumentSession::PrepareSelectionCommandPublication(std::move(input.value()), {}, {}, staleMemory)
              : std::optional<PreparedSelectionCommand>{};
    empty->session->SetSelection(
        DocumentSelection{false, SelectionLoops{SelectionLoop{{3, 3}, {4, 3}, {4, 4}, {3, 4}}}}, L"Make command stale");
    Expect(stale && stale->OutputMemoryBytes() > 0 &&
               !empty->session->IsPreparedSelectionCommandCurrent(stale.value()) &&
               !empty->session->ApplyPreparedSelectionCommandIfCurrent(stale.value()) &&
               HasSmallSquare(empty->session->Selection()) && staleMemory && !staleMemory->IsBound(),
           "source-stale-supplied-memory-command-refuses-without-overwrite");
}
} // namespace

int RunDocumentSelectionCommandMemoryTests()
{
    TestSelectedCommand(SelectionCommandKind::SelectAll);
    TestSelectedCommand(SelectionCommandKind::Deselect);
    TestRefusal(BindMode::Unbound);
    TestRefusal(BindMode::Underbound);
    TestRefusal(BindMode::StaleAfterBind);
    TestNoOpCancellationAndStale();
    std::printf("selection-command-memory: %u assertions, %u failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

#include "Document.Tests.LassoGestureAuthority.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "DocumentDraft.h"
#include "Executor.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "LassoFinishWork.h"
#include "LassoOpeningWork.h"
#include "LocallyOwnedPublication.h"
#include "MemoryAdmission.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <windows.h>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4ULL * 1024 * 1024;
constexpr std::uint32_t kBlocks = 1;
constexpr std::uint32_t kTimeoutMs = 10'000;
constexpr unsigned kExpectedChecks = 45;
unsigned gChecks = 0;
unsigned gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s lasso-gesture-authority.%s\n", condition ? "PASS" : "FAIL", label);
}

bool SampleMemory(void*, MemorySnapshot& out) noexcept
{
    constexpr auto available = kMemoryAdmissionHeadroomBytes + kAdmissionBytes + 64ULL * 1024 * 1024;
    out = {available, available};
    return true;
}

bool Settle(JobScheduler& scheduler)
{
    return SettleJobs(scheduler, kTimeoutMs) == SettleResult::Settled;
}

struct Stamp final
{
    explicit Stamp(const DocumentSession& session) noexcept
        : generation(session.Generation()), serial(session.DocumentSerial()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undo(session.history.UndoCount()),
          redo(session.history.CanRedo())
    {
    }
    bool Matches(const DocumentSession& session) const noexcept
    {
        return session.Generation() == generation && session.DocumentSerial() == serial &&
               session.history.IsCurrent(history) && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undo && session.history.CanRedo() == redo;
    }
    std::uint64_t generation;
    std::uint64_t serial;
    DocumentHistory::StateView history;
    Guid revision;
    int undo;
    bool redo;
};

struct OpeningState final
{
    JobScheduler* scheduler = nullptr;
    DocumentSession* session = nullptr;
    JobTicket ticket;
    std::thread::id owner;
    std::optional<LassoOpeningInput> input;
    std::optional<PreparedLassoOpening> prepared;
    std::shared_ptr<SharedPublicationMemory> memory;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::uint64_t reservedBefore = 0;
    std::int32_t bindResult = E_UNEXPECTED;
    unsigned runs = 0;
    bool retainOwnGesture = true;
    bool noCaptureBeforeDelivery = false;
    bool capturedOnOwner = false;
    bool workerOnOriginalSlot = true;
    bool resumedOriginal = false;
    bool completedOnOwner = false;
    bool unboundRefused = false;
    bool boundOriginal = false;
};

class OpeningJob final : public Job
{
  public:
    explicit OpeningJob(OpeningState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        auto& s = _state;
        ++s.runs;
        s.workerOnOriginalSlot = s.workerOnOriginalSlot && std::this_thread::get_id() != s.owner &&
                                 context.reservedMemoryBytes == kAdmissionBytes && context.deferredRetirement;
        if (!_preparing)
            return {};
        if (!s.input || !s.workerOnOriginalSlot)
            return {JobStatus::Failed, E_ABORT};
        s.memory = SharedPublicationMemory::CreateForWork(PreparedLassoOpening::MemoryBytes(kBlocks));
        // The false branch intentionally omits the new argument, exercising the default API.
        auto prepared = s.retainOwnGesture ? DocumentSession::PrepareLassoOpening(std::move(s.input.value()), kBlocks,
                                                                                  context.cancellation, s.memory, true)
                                           : DocumentSession::PrepareLassoOpening(std::move(s.input.value()), kBlocks,
                                                                                  context.cancellation, s.memory);
        if (!prepared)
            return {JobStatus::Failed, E_ABORT};
        s.prepared.emplace(std::move(prepared.value()));
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        if (_preparing || result.status != JobStatus::Finished || result.error != S_OK || ticket != s.ticket ||
            !s.workerOnOriginalSlot || !s.scheduler->InUiCallback() ||
            s.scheduler->ReservedMemoryBytes() != s.reservedBefore + kAdmissionBytes)
            return false;
        s.capturedOnOwner = std::this_thread::get_id() == s.owner;
        auto input = s.session->PrepareLassoOpeningInput({8.5, -2.5}, SelectionMode::New,
                                                         LassoOpeningSettings{LassoKind::Rectangle, false});
        if (input)
            s.input.emplace(std::move(input.value()));
        _preparing = true;
        s.resumedOriginal = s.scheduler->Resume(ticket);
        return s.resumedOriginal;
    }

    void Completed(JobResult result) noexcept override
    {
        auto& s = _state;
        s.terminal = result;
        s.completedOnOwner = s.scheduler->InUiCallback() && std::this_thread::get_id() == s.owner;
        if (!s.memory)
            return;
        if (result.status == JobStatus::Finished && result.error == S_OK && s.prepared)
        {
            const Stamp before(*s.session);
            const auto opening = s.session->LassoOpeningForWork();
            s.unboundRefused = !s.session->ApplyPreparedLassoOpeningIfCurrent(s.prepared.value()) &&
                               !s.session->PendingLasso() && before.Matches(*s.session) &&
                               s.session->LassoOpeningForWork() == opening;
            s.bindResult = s.memory->BindAfterCompletedWork(*s.scheduler, s.ticket);
        }
        else
            s.bindResult = s.memory->BindForRetirement(*s.scheduler, s.ticket);
        s.boundOriginal = s.memory->BoundTo(*s.scheduler, s.ticket) && s.memory->Bytes() == kAdmissionBytes;
        if (s.bindResult != S_OK || !s.boundOriginal)
        {
            if (!s.memory->IsBound())
                (void)s.memory->BindForRetirement(*s.scheduler, s.ticket);
            s.prepared.reset();
        }
        // This original job performs no useful stage or Resume after whole-reservation binding.
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if (_state.memory && !_state.memory->IsBound())
            (void)_state.memory->BindForRetirement(scheduler, ticket);
    }

  private:
    OpeningState& _state;
    bool _preparing = false;
};

struct Fixture final
{
    explicit Fixture(JobScheduler& jobs) : scheduler(jobs)
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 32;
        document.height = 32;
        auto layer = ImageLayer::Blank(L"Gesture authority", {2, 2});
        constexpr std::array<std::uint8_t, 16> pixels{0,   0, 255, 255, 0,   255, 0,   255,
                                                      255, 0, 0,   255, 255, 255, 255, 255};
        layer.asset =
            RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
        const auto active = layer.id;
        document.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(document).Seal(), active);
        session.history.Reset();
        session.SelectAll();
        session.Undo(); // Opening/drag/abort must preserve this real Redo branch.
    }
    ~Fixture()
    {
        if (!Settle(scheduler))
            scheduler.Close(); // Join every job that borrows state before destroying state/session.
        session.CancelLasso();
        finishInput.reset();
        state.input.reset();
        state.prepared.reset();
        state.memory.reset();
        if (!Settle(scheduler))
            scheduler.Close();
    }
    JobScheduler& scheduler;
    DocumentSession session;
    OpeningState state;
    std::optional<LassoFinishInput> finishInput;
};

bool Submit(Fixture& fixture)
{
    auto& s = fixture.state;
    auto& scheduler = fixture.scheduler;
    if (!Settle(scheduler))
        return false;
    s.scheduler = &scheduler;
    s.session = &fixture.session;
    s.owner = std::this_thread::get_id();
    s.reservedBefore = scheduler.ReservedMemoryBytes();
    JobOptions options{};
    options.kind = JobClass::Selection;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    std::unique_ptr<Job> job = std::make_unique<OpeningJob>(s);
    if (scheduler.Submit(job, options, s.ticket) != S_OK || job)
        return false;
    s.noCaptureBeforeDelivery = !s.input && !s.prepared && !fixture.session.PendingLasso();
    return Settle(scheduler);
}

bool LiteralRedoBranch(const DocumentSession& session) noexcept
{
    const auto history = session.history.CaptureState();
    // Reset -> Select All -> Undo is independently one future entry, with no open transaction.
    return history.PastCount() == 0 && history.FutureCount() == 1 && history.Depth() == 0 &&
           session.history.UndoCount() == 0 && session.history.CanRedo() && !session.history.IsModified() &&
           !session.Selection();
}

bool Ready(const Fixture& fixture) noexcept
{
    const auto& s = fixture.state;
    return s.noCaptureBeforeDelivery && s.capturedOnOwner && s.input && s.resumedOriginal && s.workerOnOriginalSlot &&
           s.runs == 2 && s.completedOnOwner && s.prepared && s.bindResult == S_OK && s.boundOriginal &&
           s.terminal.status == JobStatus::Finished && s.terminal.error == S_OK && LiteralRedoBranch(fixture.session);
}

bool Apply(Fixture& f) noexcept
{
    return f.state.prepared && f.session.ApplyPreparedLassoOpeningIfCurrent(f.state.prepared.value());
}

bool Current(Fixture& f) noexcept
{
    return f.state.prepared && f.session.IsPreparedLassoGestureCurrent(f.state.prepared.value());
}

bool Drag(Fixture& f, Vec2 point, bool square = false, bool center = false) noexcept
{
    return f.state.prepared && f.session.DragPreparedMarqueeIfCurrent(f.state.prepared.value(), point, square, center);
}

bool Abort(Fixture& f) noexcept
{
    return f.state.prepared && f.session.AbortPreparedLassoGestureIfCurrent(f.state.prepared.value());
}

std::optional<LassoFinishTerminal> Close(Fixture& f, bool aa) noexcept
{
    return f.state.prepared ? f.session.ClosePreparedLassoGestureForWork(f.state.prepared.value(), aa) : std::nullopt;
}

bool SameDisplay(const BoundedLassoGesture::Display& a, const BoundedLassoGesture::Display& b) noexcept
{
    return a.key == b.key && a.kind == b.kind && a.mode == b.mode && a.antialiased == b.antialiased &&
           a.anchor == b.anchor && a.cursor == b.cursor && a.points.SameCapture(b.points);
}

bool PreservesDraft(DocumentSession& session, const std::optional<LassoFinishTerminal>& captured,
                    const LassoDraft* address) noexcept
{
    const auto now = session.CaptureBoundedLassoDisplayForWork();
    return captured && now && session.PendingLasso() && &session.PendingLasso().value() == address &&
           SameDisplay(captured.value().display, now.value().display);
}

bool LiteralDisplay(const BoundedLassoGesture::Display& display, SelectionBounds bounds, std::uint64_t revision,
                    std::uint32_t count, bool aa = false) noexcept
{
    return display.key == BoundedLassoGesture::Key{1, revision} && display.kind == LassoKind::Rectangle &&
           display.mode == SelectionMode::New && display.antialiased == aa &&
           display.anchor == std::optional(Vec2{9, -3}) && !display.cursor && display.points.Count() == count &&
           display.points.Bounds() == std::optional(bounds);
}

bool LiteralDraft(DocumentSession& session, SelectionBounds bounds, std::uint64_t revision,
                  std::uint32_t count = 4) noexcept
{
    const auto captured = session.CaptureBoundedLassoDisplayForWork();
    const auto& draft = session.PendingLasso();
    return captured && draft && draft.value().IsBounded() && draft.value().kind == LassoKind::Rectangle &&
           draft.value().mode == SelectionMode::New && draft.value().anchor == std::optional(Vec2{9, -3}) &&
           draft.value().FirstPoint() == std::optional(Vec2{bounds.minX, bounds.minY}) &&
           draft.value().PointCount() == count && draft.value().points.empty() &&
           draft.value().points.capacity() == 0 && LiteralDisplay(captured.value().display, bounds, revision, count);
}

bool CaptureFinish(Fixture& f, LassoFinishTerminal terminal)
{
    auto input = f.session.PrepareClosedBoundedLassoFinishInput(std::move(terminal));
    if (input)
        f.finishInput.emplace(std::move(input.value()));
    return input.has_value();
}

void CheckOwnedGeometry(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const Stamp before(f->session);
    const auto restore = f->session.CaptureHistoryRestoreAuthority();
    const bool settled = Submit(*f);
    Expect(settled && f->state.noCaptureBeforeDelivery && f->state.capturedOnOwner && f->state.resumedOriginal,
           "protocol.capture-after-original-admission");
    Expect(Ready(*f) && f->state.workerOnOriginalSlot && f->state.completedOnOwner && f->state.memory &&
               f->state.memory->Bytes() == kAdmissionBytes,
           "protocol.worker-prepares-and-completed-binds-original-ticket");
    Expect(Ready(*f) && f->state.unboundRefused && before.Matches(f->session),
           "protocol.unbound-refuses-before-draft-mutation");
    const bool applied = Ready(*f) && Apply(*f);
    Expect(applied && LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1),
           "owned.rounded-anchor-and-first-point-are-9-minus3");
    Expect(applied && Current(*f) && !f->session.CanApplyHistoryRestore(restore),
           "owned.current-while-general-history-restore-still-refuses-lasso");
    const bool rectangle = Drag(*f, {13.5, 2.5});
    Expect(applied && rectangle && Current(*f) && LiteralDraft(f->session, {9, -3, 14, 3}, 2),
           "owned.rectangle-literal-bounds-9-minus3-14-3");
    const bool square = Drag(*f, {4.5, -0.5}, true);
    Expect(square && Current(*f) && LiteralDraft(f->session, {5, -3, 9, 1}, 3),
           "owned.shift-negative-square-literal-bounds-5-minus3-9-1");
    const bool center = Drag(*f, {12.5, 1.5}, false, true);
    Expect(center && Current(*f) && LiteralDraft(f->session, {5, -8, 13, 2}, 4),
           "owned.alt-center-literal-bounds-5-minus8-13-2");
    const bool centeredSquare = Drag(*f, {6.5, 0.5}, true, true);
    Expect(centeredSquare && Current(*f) && LiteralDraft(f->session, {5, -7, 13, 1}, 5),
           "owned.shift-alt-square-literal-bounds-5-minus7-13-1");
    const auto unchanged = f->session.CaptureBoundedLassoDisplayForWork();
    const auto* address = f->session.PendingLasso() ? &f->session.PendingLasso().value() : nullptr;
    const bool noOp = Drag(*f, {6.5, 0.5}, true, true);
    Expect(noOp && Current(*f) && PreservesDraft(f->session, unchanged, address) &&
               LiteralDraft(f->session, {5, -7, 13, 1}, 5),
           "owned.same-sample-preserves-exact-owned-key");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    const bool badX = Drag(*f, {nan, 2});
    const bool badY = Drag(*f, {2, infinity});
    const bool badNegative = Drag(*f, {-infinity, 2}, true, true);
    Expect(!badX && !badY && !badNegative && Current(*f) && PreservesDraft(f->session, unchanged, address),
           "owned.nonfinite-samples-refuse-and-preserve-owned-key");
    Expect(before.Matches(f->session) && LiteralRedoBranch(f->session),
           "owned.all-own-drags-preserve-history-generation-and-redo");
    const bool aborted = Abort(*f);
    Expect(aborted && !f->session.PendingLasso() && before.Matches(f->session) && LiteralRedoBranch(f->session),
           "owned.abort-clears-only-draft-without-history-generation-or-redo-change");
    const bool secondAbort = Abort(*f);
    const bool afterAbortDrag = Drag(*f, {20, 20});
    const auto afterAbortClose = Close(*f, true);
    Expect(aborted && !Current(*f) && !secondAbort && !afterAbortDrag && !afterAbortClose &&
               !f->session.PendingLasso() && before.Matches(f->session),
           "owned.abort-is-one-shot-and-rejects-later-drag-and-close");
}

void CheckForeignWriter(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Current(*f);
    const Stamp before(f->session);
    const bool foreign = f->session.DragBoundedMarquee({17.5, 4.5}, false, false);
    const auto captured = f->session.CaptureBoundedLassoDisplayForWork();
    const auto* address = f->session.PendingLasso() ? &f->session.PendingLasso().value() : nullptr;
    Expect(opened && foreign && !Current(*f) && LiteralDraft(f->session, {9, -3, 18, 5}, 2) &&
               before.Matches(f->session),
           "foreign-writer.direct-drag-changes-key-without-source-history-change");
    const bool dragged = Drag(*f, {25, 25}, true, true);
    Expect(opened && foreign && !dragged && PreservesDraft(f->session, captured, address) && before.Matches(f->session),
           "foreign-writer.own-drag-refuses-and-preserves-exact-foreign-draft");
    const bool aborted = Abort(*f);
    Expect(opened && foreign && !aborted && PreservesDraft(f->session, captured, address) && before.Matches(f->session),
           "foreign-writer.own-abort-refuses-and-preserves-exact-foreign-draft");
    const auto closed = Close(*f, true);
    Expect(opened && foreign && !closed && PreservesDraft(f->session, captured, address) && before.Matches(f->session),
           "foreign-writer.own-close-refuses-and-preserves-exact-foreign-draft");
}

void CheckForeignEditors(JobScheduler& scheduler)
{
    for (const bool history : {true, false})
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Current(*f);
        const Stamp before(f->session);
        if (history)
            f->session.history.Begin(L"Foreign Begin", f->session.Document(), f->session.ActiveLayerID());
        else
            f->session.BeginTransform(true);
        const Stamp foreign(f->session);
        const auto captured = f->session.CaptureBoundedLassoDisplayForWork();
        const auto* address = f->session.PendingLasso() ? &f->session.PendingLasso().value() : nullptr;
        const auto& editor = f->session.PendingTransformEdit();
        const auto* editorAddress = editor ? &editor.value() : nullptr;
        const auto editorDraft = editor ? std::optional(editor.value().draft) : std::optional<LayerTransform>{};
        const bool foreignExists = history ? (!f->session.history.IsCurrent(before.history) &&
                                              foreign.history.Depth() == 1 && foreign.history.FutureCount() == 1)
                                           : (editorAddress && editorDraft && before.Matches(f->session));
        const auto preserves = [&]() noexcept
        {
            const auto& currentEditor = f->session.PendingTransformEdit();
            return foreign.Matches(f->session) && PreservesDraft(f->session, captured, address) &&
                   (history ? !currentEditor
                            : (currentEditor && &currentEditor.value() == editorAddress &&
                               currentEditor.value().draft == editorDraft.value()));
        };
        Expect(opened && foreignExists && f->session.Generation() == before.generation &&
                   f->session.history.CurrentRevision() == before.revision && !Current(*f),
               history ? "foreign-history.begin-keeps-generation-revision-but-revokes-own-authority"
                       : "foreign-transform.begin-keeps-generation-history-and-exact-editor");
        const bool dragged = Drag(*f, {22, 23});
        Expect(opened && foreignExists && !dragged && preserves(),
               history ? "foreign-history.own-drag-refuses-with-foreign-pending-preserved"
                       : "foreign-transform.own-drag-refuses-with-exact-editor-and-draft-preserved");
        const bool aborted = Abort(*f);
        Expect(opened && foreignExists && !aborted && preserves(),
               history ? "foreign-history.own-abort-refuses-with-foreign-pending-preserved"
                       : "foreign-transform.own-abort-refuses-with-exact-editor-and-draft-preserved");
        const auto closed = Close(*f, false);
        Expect(opened && foreignExists && !closed && preserves(),
               history ? "foreign-history.own-close-refuses-with-foreign-pending-preserved"
                       : "foreign-transform.own-close-refuses-with-exact-editor-and-draft-preserved");
    }
}

void CheckForeignSessions(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    auto other = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Current(*f);
    const Stamp original(f->session);
    const Stamp foreign(other->session);
    const bool wrongDrag = f->state.prepared && other->session.DragPreparedMarqueeIfCurrent(f->state.prepared.value(),
                                                                                            {20, 20}, false, false);
    Expect(opened && !other->session.IsPreparedLassoGestureCurrent(f->state.prepared.value()) && !wrongDrag &&
               original.Matches(f->session) && foreign.Matches(other->session) && !other->session.PendingLasso() &&
               Current(*f) && LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1),
           "session.foreign-drag-refuses-and-preserves-both-sessions");
    const bool wrongAbort =
        f->state.prepared && other->session.AbortPreparedLassoGestureIfCurrent(f->state.prepared.value());
    Expect(opened && !wrongAbort && original.Matches(f->session) && foreign.Matches(other->session) &&
               !other->session.PendingLasso() && Current(*f) && LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1),
           "session.foreign-abort-refuses-and-preserves-both-sessions");
    const auto wrongClose = f->state.prepared
                                ? other->session.ClosePreparedLassoGestureForWork(f->state.prepared.value(), false)
                                : std::nullopt;
    Expect(opened && !wrongClose && original.Matches(f->session) && foreign.Matches(other->session) &&
               !other->session.PendingLasso() && Current(*f) && LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1),
           "session.foreign-close-refuses-and-preserves-both-sessions");
    const auto serial = f->session.DocumentSerial();
    f->session.CreateDocument(24, 16, true);
    const Stamp replacement(f->session);
    const bool staleDrag = Drag(*f, {20, 20});
    const bool staleAbort = Abort(*f);
    const auto staleClose = Close(*f, false);
    Expect(opened && f->session.DocumentSerial() != serial && !Current(*f) && !staleDrag && !staleAbort &&
               !staleClose && replacement.Matches(f->session) && !f->session.PendingLasso() && f->session.Document() &&
               f->session.Document()->width == 24 && f->session.Document()->height == 16,
           "session.replaced-document-refuses-all-own-operations-without-fresh-mutation");
}

void CheckClose(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const Stamp before(f->session);
        const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Drag(*f, {13.5, 2.5});
        f->session.selectionAntialiased = true;
        const auto terminal = Close(*f, false);
        Expect(opened && terminal && !f->session.PendingLasso() && f->session.selectionAntialiased &&
                   LiteralDisplay(terminal.value().display, {9, -3, 14, 3}, 2, 4, false) && before.Matches(f->session),
               "close.captured-aa-false-overrides-live-true-with-literal-terminal");
        const bool dragged = Drag(*f, {20, 20});
        const bool aborted = Abort(*f);
        const auto secondClose = Close(*f, true);
        Expect(opened && terminal && !Current(*f) && !dragged && !aborted && !secondClose &&
                   !f->session.PendingLasso() && before.Matches(f->session) && LiteralRedoBranch(f->session),
               "close.consumes-own-authority-without-history-generation-or-redo-change");
        bool corruptRefused = false;
        bool firstCapture = false;
        bool duplicateRefused = false;
        if (terminal)
        {
            auto corrupt = terminal.value();
            corrupt.display.antialiased = true;
            corruptRefused = !f->session.PrepareClosedBoundedLassoFinishInput(std::move(corrupt)) &&
                             !f->session.PendingLasso() && before.Matches(f->session) && LiteralRedoBranch(f->session);
            firstCapture = CaptureFinish(*f, terminal.value());
            duplicateRefused = !f->session.PrepareClosedBoundedLassoFinishInput(terminal.value());
        }
        Expect(opened && terminal && corruptRefused && before.Matches(f->session),
               "close.foreign-terminal-aa-refuses-before-good-terminal-capture");
        Expect(opened && terminal && firstCapture && f->finishInput && duplicateRefused && before.Matches(f->session) &&
                   LiteralRedoBranch(f->session),
               "close.exact-matching-terminal-captures-once");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const Stamp before(f->session);
        const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Drag(*f, {13.5, 2.5});
        f->session.selectionAntialiased = false;
        const auto terminal = Close(*f, true);
        const auto secondClose = Close(*f, false);
        Expect(opened && terminal && !f->session.selectionAntialiased &&
                   LiteralDisplay(terminal.value().display, {9, -3, 14, 3}, 2, 4, true) && !secondClose &&
                   !Current(*f) && !f->session.PendingLasso() && before.Matches(f->session),
               "close.captured-aa-true-overrides-live-false-and-is-one-shot");
    }
}

void CheckDefaultMode(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    f->state.retainOwnGesture = false;
    const Stamp before(f->session);
    const auto restore = f->session.CaptureHistoryRestoreAuthority();
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f);
    const bool dragged = Drag(*f, {22, 23});
    const bool aborted = Abort(*f);
    const auto closed = Close(*f, false);
    Expect(opened && !Current(*f) && !dragged && !aborted && !closed &&
               LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1) && before.Matches(f->session),
           "default.omitted-opt-in-refuses-all-own-apis-with-draft-preserved");
    const bool legacyDrag = f->session.DragBoundedMarquee({13.5, 2.5}, false, false);
    Expect(opened && legacyDrag && LiteralDraft(f->session, {9, -3, 14, 3}, 2) && before.Matches(f->session),
           "default.legacy-bounded-marquee-still-draws-exact-rectangle");
    const bool restoreRefused = !f->session.CanApplyHistoryRestore(restore);
    const auto legacyClose = f->session.CloseBoundedLassoForWork(false);
    Expect(opened && legacyDrag && restoreRefused && legacyClose &&
               LiteralDisplay(legacyClose.value().display, {9, -3, 14, 3}, 2, 4, false) && !f->session.PendingLasso() &&
               before.Matches(f->session),
           "default.general-history-restore-refuses-live-lasso-and-legacy-close-still-works");
}

void CheckRetention(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f);
    const auto envelope =
        BoundedLassoPoints::MemoryBytes(kBlocks) + LocallyOwnedPublicationAllowance<BoundedLassoGesture>();
    const auto localBytes = f->state.memory ? f->state.memory->LocalAllocationBytes() : 0;
    std::weak_ptr<SharedPublicationMemory> weak = f->state.memory;
    const bool aborted = Abort(*f);
    f->state.memory.reset();
    const auto retained = [&]() noexcept
    {
        const auto witness = weak.lock(); // Diagnostic lock dies before any later release/refund predicate.
        return witness && witness->Bytes() == kAdmissionBytes && witness->ResidentBytes() == kAdmissionBytes &&
               witness->LocalAllocationBytes() == localBytes;
    };
    Expect(opened && aborted && !f->session.PendingLasso() && f->state.prepared && envelope > 0 &&
               PreparedLassoOpening::MemoryBytes(kBlocks) == envelope &&
               envelope + SharedPublicationMemory::OwnershipAllowance() < kAdmissionBytes && localBytes > 0 &&
               localBytes <= envelope && Settle(scheduler) && scheduler.IsIdle() &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes && retained(),
           "memory.aborted-old-writer-and-arena-stay-charged-through-prepared-owner");
    f->session.CreateDocument(16, 8, true);
    f->session.history.Reset();
    const auto freshHistory = f->session.history.CaptureState();
    Expect(opened && aborted && f->session.Document() && f->session.Document()->width == 16 &&
               f->session.Document()->height == 8 && freshHistory.PastCount() == 0 && freshHistory.FutureCount() == 0 &&
               freshHistory.Depth() == 0 && !f->session.PendingLasso() && Settle(scheduler) &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes && retained(),
           "memory.replaced-session-and-dropped-history-do-not-refund-old-prepared-owner");
    // There is no public weak-writer observer. Settled local-allocation and original-lease diagnostics
    // observe retention without exposing the private writer or adding a test-only production API.
    f->state.prepared.reset();
    Expect(opened && aborted && Settle(scheduler) && weak.expired() &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "memory.last-strong-prepared-release-leaves-only-charged-weak-control");
    weak.reset();
    Expect(Settle(scheduler) && scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0 &&
               scheduler.ReleasingMemoryBytes() == 0,
           "memory.last-weak-release-refunds-exact-original-ticket");
}

void CheckHistoryBeforeAcquisition(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool prepared = Submit(*f) && Ready(*f);
    const Stamp before(f->session);
    f->session.history.Begin(L"Foreign Begin before acquisition", f->session.Document(), f->session.ActiveLayerID());
    const Stamp foreign(f->session);
    const bool openingCurrent =
        f->state.prepared && f->session.IsPreparedLassoOpeningCurrent(f->state.prepared.value());
    const bool applied = Apply(*f);
    Expect(prepared && !f->session.history.IsCurrent(before.history) && f->session.Generation() == before.generation &&
               f->session.history.CurrentRevision() == before.revision && foreign.history.Depth() == 1 &&
               foreign.history.FutureCount() == 1 && !openingCurrent && !applied && !Current(*f) &&
               !f->session.PendingLasso() && foreign.Matches(f->session),
           "history.foreign-begin-before-acquisition-keeps-fixed-s8-epoch-refusal");
}
} // namespace

int RunLassoGestureAuthorityTests()
{
    gChecks = 0;
    gFailures = 0;
    auto scheduler = std::make_unique<JobScheduler>();
    const bool started =
        scheduler->Initialize(1, {}, {SampleMemory, nullptr}) == S_OK && scheduler->EnableDeferredRelease();
    Expect(started, "scheduler.one-worker-original-reservations-and-deferred-retirement");
    if (started)
    {
        CheckOwnedGeometry(*scheduler);
        CheckForeignWriter(*scheduler);
        CheckForeignEditors(*scheduler);
        CheckForeignSessions(*scheduler);
        CheckClose(*scheduler);
        CheckDefaultMode(*scheduler);
        CheckRetention(*scheduler);
        CheckHistoryBeforeAcquisition(*scheduler);
        const bool settled = Settle(*scheduler);
        Expect(gChecks == kExpectedChecks - 1 && settled && scheduler->IsIdle() &&
                   scheduler->ReservedMemoryBytes() == 0 && scheduler->ReleasingMemoryBytes() == 0,
               "scheduler.all-original-ticket-charges-and-records-drain");
    }
    scheduler->Close();
    std::printf("SUMMARY lasso-gesture-authority checks=%u failures=%u\n", gChecks, gFailures);
    return gChecks == kExpectedChecks && gFailures == 0 ? 0 : static_cast<int>(gFailures ? gFailures : 1);
}

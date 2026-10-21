#include "Document.Tests.LassoGestureAuthority.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "DocumentDraft.h"
#include "Executor.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "LassoFinishWork.h"
#include "LassoIntent.h"
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
constexpr unsigned kIntentCleanupChecks = 30;
const char* gFamily = "lasso-gesture-authority";
unsigned gChecks = 0;
unsigned gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s %s.%s\n", condition ? "PASS" : "FAIL", gFamily, label);
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
    std::optional<LassoIntent> intent;
    std::optional<LassoFinishTerminal> closed;
    std::optional<LassoFinishInput> finishInput;
    std::optional<PreparedLassoOpening> prepared;
    std::shared_ptr<SharedPublicationMemory> memory;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::uint64_t reservedBefore = 0;
    std::int32_t bindResult = E_UNEXPECTED;
    unsigned runs = 0;
    LassoKind kind = LassoKind::Rectangle;
    bool retainOwnGesture = true;
    bool cancelPreparation = false;
    bool failAfterInputMove = false;
    bool finishWork = false;
    bool referenceFinish = false;
    bool workerInputMoved = false;
    bool preparationRefused = false;
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
        if (!s.workerOnOriginalSlot || (s.finishWork ? !s.finishInput : !s.input))
            return {JobStatus::Failed, E_ABORT};
        s.memory = SharedPublicationMemory::CreateForWork(PreparedLassoOpening::MemoryBytes(kBlocks));
        CancelSource cancelled;
        if (s.cancelPreparation)
            (void)cancelled.Cancel();
        const auto cancellation = s.cancelPreparation ? cancelled.Token() : context.cancellation;
        if (s.finishWork)
        {
            auto moved = std::move(s.finishInput.value());
            s.workerInputMoved = true;
            if (s.failAfterInputMove)
                return {JobStatus::Failed, E_FAIL};
            // Cleanup cases stop here: no finished document/history publication is exposed.
            auto prepared =
                DocumentSession::PrepareLassoFinishPublication(std::move(moved), cancellation, {}, nullptr, s.memory);
            s.preparationRefused = !prepared;
            return {s.cancelPreparation ? JobStatus::Cancelled : JobStatus::Failed, prepared ? E_UNEXPECTED : E_ABORT};
        }
        auto moved = std::move(s.input.value());
        s.workerInputMoved = true;
        if (s.failAfterInputMove)
            return {JobStatus::Failed, E_FAIL};
        // The false branch intentionally omits the new argument, exercising the default API.
        auto prepared =
            s.retainOwnGesture
                ? DocumentSession::PrepareLassoOpening(std::move(moved), kBlocks, cancellation, s.memory, true)
                : DocumentSession::PrepareLassoOpening(std::move(moved), kBlocks, cancellation, s.memory);
        s.preparationRefused = !prepared;
        if (!prepared)
            return {s.cancelPreparation ? JobStatus::Cancelled : JobStatus::Failed, E_ABORT};
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
        if (s.finishWork)
        {
            if (s.closed)
            {
                auto input = s.referenceFinish ? s.session->PrepareLassoFinishInput(s.closed.value())
                                               : s.session->PrepareClosedBoundedLassoFinishInput(s.closed.value());
                if (input)
                {
                    s.finishInput.emplace(std::move(input.value()));
                    s.intent = s.session->CaptureLassoIntent(s.finishInput.value());
                }
            }
        }
        else
        {
            auto input = s.session->PrepareLassoOpeningInput({8.5, -2.5}, SelectionMode::New,
                                                             LassoOpeningSettings{s.kind, false});
            if (input)
            {
                s.input.emplace(std::move(input.value()));
                s.intent = s.session->CaptureLassoIntent(s.input.value());
            }
        }
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
        const auto release = [](OpeningState& value) noexcept
        {
            value.input.reset();
            value.finishInput.reset();
            value.closed.reset();
            value.intent.reset();
            value.prepared.reset();
            value.memory.reset();
        };
        release(state);
        release(newer);
        release(finish);
        release(later);
        if (!Settle(scheduler))
            scheduler.Close();
    }
    JobScheduler& scheduler;
    DocumentSession session;
    OpeningState state;
    OpeningState newer;
    OpeningState finish;
    OpeningState later;
    std::optional<LassoFinishInput> finishInput;
};

bool Submit(Fixture& fixture, OpeningState* selected = nullptr)
{
    auto& s = selected ? *selected : fixture.state;
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
    s.noCaptureBeforeDelivery =
        !s.input && !s.finishInput && !s.intent && !s.prepared && !fixture.session.PendingLasso();
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
    static_assert(envelope > 0, "memory.original-envelope-positive");
    Expect(opened && aborted && !f->session.PendingLasso() && f->state.prepared &&
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
bool PreparedOpeningCurrent(Fixture& f, OpeningState& state) noexcept
{
    return state.prepared && f.session.IsPreparedLassoOpeningCurrent(state.prepared.value());
}

bool DiscardIntent(Fixture& f, OpeningState& state) noexcept
{
    return state.intent && f.session.DiscardLassoIntentIfCurrent(state.intent.value());
}

bool DiscardClosed(Fixture& f, OpeningState& state) noexcept
{
    return state.prepared && f.session.DiscardPreparedClosedLassoIfCurrent(state.prepared.value());
}

bool StoppedOriginal(const OpeningState& state, bool cancelled) noexcept
{
    return state.noCaptureBeforeDelivery && state.capturedOnOwner && state.resumedOriginal &&
           state.workerOnOriginalSlot && state.runs == 2 && state.workerInputMoved && state.completedOnOwner &&
           state.intent && !state.prepared && state.bindResult == S_OK && state.boundOriginal && state.memory &&
           state.memory->Bytes() == kAdmissionBytes &&
           state.terminal.status == (cancelled ? JobStatus::Cancelled : JobStatus::Failed) &&
           state.terminal.error == static_cast<std::int32_t>(cancelled ? E_ABORT : E_FAIL);
}

bool StopFinish(Fixture& f, OpeningState& state, const LassoFinishTerminal& terminal, bool reference = false)
{
    state.finishWork = true;
    state.referenceFinish = reference;
    state.cancelPreparation = true;
    state.closed.emplace(terminal);
    return Submit(f, &state) && StoppedOriginal(state, true) && state.preparationRefused;
}

void CheckStoppedOpeningIntents(JobScheduler& scheduler)
{
    for (const bool cancel : {true, false})
    {
        auto f = std::make_unique<Fixture>(scheduler);
        f->state.cancelPreparation = cancel;
        f->state.failAfterInputMove = !cancel;
        const Stamp before(f->session);
        const bool submitted = Submit(*f);
        const bool movedCannotCapture = f->state.input && !f->session.CaptureLassoIntent(f->state.input.value());
        const auto copy = f->state.intent;
        Expect(submitted && StoppedOriginal(f->state, cancel) && movedCannotCapture &&
                   (!cancel || f->state.preparationRefused) && !f->session.PendingLasso() && before.Matches(f->session),
               cancel ? "opening.cancelled-worker-move-retains-original-scalar-claim"
                      : "opening.failed-worker-move-retains-original-scalar-claim");
        const bool discarded = DiscardIntent(*f, f->state);
        const bool repeated = copy && f->session.DiscardLassoIntentIfCurrent(copy.value());
        Expect(submitted && discarded && !repeated && !DiscardIntent(*f, f->state) && !f->session.PendingLasso() &&
                   f->session.LassoOpeningForWork() == 0 && before.Matches(f->session) && LiteralRedoBranch(f->session),
               cancel ? "opening.cancelled-claim-copy-clears-once-without-document-history-change"
                      : "opening.failed-claim-copy-clears-once-without-document-history-change");
    }
}

void CheckAcquiredAndSupersededIntents(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool opened = Submit(*f) && Ready(*f) && f->state.intent && Apply(*f);
        const Stamp before(f->session);
        const bool discarded = DiscardIntent(*f, f->state);
        Expect(opened && !discarded && Current(*f) && LiteralDraft(f->session, {9, -3, 9, -3}, 1, 1) &&
                   before.Matches(f->session),
               "opening.after-acquire-old-claim-refuses-with-exact-live-draft");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool first = Submit(*f) && Ready(*f) && f->state.intent;
        const bool newer = Submit(*f, &f->newer) && f->newer.intent && PreparedOpeningCurrent(*f, f->newer);
        const Stamp before(f->session);
        const bool oldDiscard = DiscardIntent(*f, f->state);
        Expect(first && newer && !oldDiscard && PreparedOpeningCurrent(*f, f->newer) &&
                   !PreparedOpeningCurrent(*f, f->state) && before.Matches(f->session) && !f->session.PendingLasso(),
               "opening.old-claim-cannot-clear-newer-opening-pending");
        const auto copy = f->newer.intent;
        const bool newDiscard = DiscardIntent(*f, f->newer);
        const bool repeated = copy && f->session.DiscardLassoIntentIfCurrent(copy.value());
        const bool applied =
            f->newer.prepared && f->session.ApplyPreparedLassoOpeningIfCurrent(f->newer.prepared.value());
        Expect(first && newer && newDiscard && !repeated && !applied && !PreparedOpeningCurrent(*f, f->newer) &&
                   before.Matches(f->session) && LiteralRedoBranch(f->session) && !f->session.PendingLasso(),
               "opening.newer-claim-clears-only-itself-and-prevents-late-acquire");
    }
}

void CheckFinishIntentKinds(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Drag(*f, {13.5, 2.5});
    const auto terminal = Close(*f, false);
    const Stamp before(f->session);
    const bool firstFinish = terminal && StopFinish(*f, f->finish, terminal.value());
    const bool movedCannotCapture =
        f->finish.finishInput && !f->session.CaptureLassoIntent(f->finish.finishInput.value());
    Expect(opened && firstFinish && movedCannotCapture && f->state.intent && f->finish.intent &&
               LiteralDisplay(terminal.value().display, {9, -3, 14, 3}, 2, 4) && before.Matches(f->session),
           "finish.cancelled-worker-move-retains-exact-finish-claim");
    // Fresh fixture: opening intent 1 and finish intent 1 have different opaque kinds.
    const bool wrongKind = DiscardIntent(*f, f->state);
    const bool finishStillCurrent = DiscardIntent(*f, f->finish);
    Expect(opened && firstFinish && !wrongKind && finishStillCurrent && !DiscardIntent(*f, f->finish) &&
               before.Matches(f->session) && !f->session.PendingLasso(),
           "finish.old-opening-kind-cannot-clear-first-finish-claim");
    // Reference recaptures create successive finish intents over the same immutable terminal.
    const bool secondFinish = terminal && StopFinish(*f, f->newer, terminal.value(), true);
    const bool thirdFinish = terminal && StopFinish(*f, f->later, terminal.value(), true);
    const bool stale = DiscardIntent(*f, f->newer);
    const auto copy = f->later.intent;
    const bool latest = DiscardIntent(*f, f->later);
    const bool repeated = copy && f->session.DiscardLassoIntentIfCurrent(copy.value());
    Expect(secondFinish && thirdFinish && !stale && latest && !repeated && before.Matches(f->session) &&
               LiteralRedoBranch(f->session) && !f->session.PendingLasso(),
           "finish.older-same-kind-claim-cannot-clear-newer-finish");
}

void CheckParallelIntentKinds(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Drag(*f, {13.5, 2.5});
    const auto terminal = Close(*f, false);
    const bool finish = terminal && StopFinish(*f, f->finish, terminal.value());
    const bool newer = Submit(*f, &f->newer) && f->newer.intent && PreparedOpeningCurrent(*f, f->newer);
    const Stamp before(f->session);
    const auto copy = f->finish.intent;
    const bool finishDiscarded = DiscardIntent(*f, f->finish);
    Expect(opened && finish && newer && finishDiscarded && PreparedOpeningCurrent(*f, f->newer) &&
               before.Matches(f->session) && !f->session.PendingLasso(),
           "kinds.finish-cleanup-preserves-simultaneous-new-opening-pending");
    const bool repeated = copy && f->session.DiscardLassoIntentIfCurrent(copy.value());
    const bool openingDiscarded = DiscardIntent(*f, f->newer);
    Expect(opened && finish && newer && !repeated && openingDiscarded && !PreparedOpeningCurrent(*f, f->newer) &&
               before.Matches(f->session) && LiteralRedoBranch(f->session) && !f->session.PendingLasso(),
           "kinds.copied-finish-claim-never-clears-the-other-kind");
}

void CheckIntentSessions(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto other = std::make_unique<Fixture>(scheduler);
        const bool first = Submit(*f) && Ready(*f) && f->state.intent;
        const bool second = Submit(*other) && Ready(*other) && other->state.intent;
        const Stamp source(f->session);
        const Stamp foreign(other->session);
        const bool wrong = f->state.intent && other->session.DiscardLassoIntentIfCurrent(f->state.intent.value());
        Expect(first && second && !wrong && PreparedOpeningCurrent(*f, f->state) &&
                   PreparedOpeningCurrent(*other, other->state) && source.Matches(f->session) &&
                   foreign.Matches(other->session),
               "session.foreign-guid-refuses-with-both-pending-openings-preserved");
        const bool own = DiscardIntent(*f, f->state);
        const bool foreignOwn = DiscardIntent(*other, other->state);
        Expect(first && second && own && foreignOwn && !PreparedOpeningCurrent(*f, f->state) &&
                   !PreparedOpeningCurrent(*other, other->state) && source.Matches(f->session) &&
                   foreign.Matches(other->session),
               "session.each-session-can-still-discard-only-its-own-claim");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool first = Submit(*f) && Ready(*f) && f->state.intent;
        const auto serial = f->session.DocumentSerial();
        f->session.CreateDocument(24, 16, true);
        const Stamp replacement(f->session);
        const bool newer = Submit(*f, &f->newer) && f->newer.intent && PreparedOpeningCurrent(*f, f->newer);
        const bool old = DiscardIntent(*f, f->state);
        const bool stillNew = PreparedOpeningCurrent(*f, f->newer);
        const bool own = DiscardIntent(*f, f->newer);
        Expect(first && newer && f->session.DocumentSerial() != serial && !old && stillNew && own &&
                   replacement.Matches(f->session) && f->session.Document() && f->session.Document()->width == 24 &&
                   f->session.Document()->height == 16 && !f->session.PendingLasso(),
               "session.replaced-document-old-claim-cannot-clear-fresh-pending");
    }
}

void CheckIntentForeignEditors(JobScheduler& scheduler)
{
    for (const bool history : {true, false})
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool prepared = Submit(*f) && Ready(*f) && f->state.intent;
        const Stamp before(f->session);
        if (history)
            f->session.history.Begin(L"Foreign intent cleanup Begin", f->session.Document(),
                                     f->session.ActiveLayerID());
        else
            f->session.BeginTransform(true);
        const Stamp foreign(f->session);
        const auto& editor = f->session.PendingTransformEdit();
        const auto* address = editor ? &editor.value() : nullptr;
        const auto draft = editor ? std::optional(editor.value().draft) : std::optional<LayerTransform>{};
        const bool present = history ? (!f->session.history.IsCurrent(before.history) && foreign.history.Depth() == 1 &&
                                        foreign.history.FutureCount() == 1)
                                     : (address && draft && before.Matches(f->session));
        Expect(prepared && present && f->session.Generation() == before.generation &&
                   f->session.history.CurrentRevision() == before.revision && !f->session.PendingLasso(),
               history ? "foreign-history.begin-keeps-generation-revision-before-intent-cleanup"
                       : "foreign-transform.exact-editor-exists-before-intent-cleanup");
        const auto copy = f->state.intent;
        const bool cleaned = DiscardIntent(*f, f->state);
        const bool repeated = copy && f->session.DiscardLassoIntentIfCurrent(copy.value());
        const bool applied = Apply(*f);
        const auto& after = f->session.PendingTransformEdit();
        const bool editorPreserved =
            history ? !after : (after && &after.value() == address && after.value().draft == draft.value());
        Expect(prepared && present && cleaned && !repeated && !applied && editorPreserved &&
                   foreign.Matches(f->session) && f->session.LassoOpeningForWork() == 0 && !f->session.PendingLasso(),
               history ? "foreign-history.exact-owned-intent-clears-with-foreign-pending-preserved"
                       : "foreign-transform.exact-owned-intent-clears-with-exact-editor-preserved");
    }
}

void CheckClosedIntentRetirement(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    auto other = std::make_unique<Fixture>(scheduler);
    const bool opened = Submit(*f) && Ready(*f) && Apply(*f) && Drag(*f, {13.5, 2.5});
    auto terminal = Close(*f, false);
    const Stamp before(f->session);
    f->session.history.Begin(L"Foreign Begin after Close", f->session.Document(), f->session.ActiveLayerID());
    const Stamp foreign(f->session);
    const Stamp otherBefore(other->session);
    const bool captureRefused = terminal && !f->session.PrepareClosedBoundedLassoFinishInput(terminal.value());
    Expect(opened && terminal && captureRefused && LiteralDisplay(terminal.value().display, {9, -3, 14, 3}, 2, 4) &&
               f->session.Generation() == before.generation &&
               f->session.history.CurrentRevision() == before.revision && foreign.history.Depth() == 1 &&
               foreign.history.FutureCount() == 1 && foreign.Matches(f->session),
           "closed.foreign-history-refuses-terminal-capture-with-literal-source-preserved");
    const bool wrongSession =
        f->state.prepared && other->session.DiscardPreparedClosedLassoIfCurrent(f->state.prepared.value());
    const bool cleaned = DiscardClosed(*f, f->state);
    const bool repeated = DiscardClosed(*f, f->state);
    const bool afterCleanupCapture =
        terminal && f->session.PrepareClosedBoundedLassoFinishInput(terminal.value()).has_value();
    Expect(opened && captureRefused && !wrongSession && cleaned && !repeated && !afterCleanupCapture &&
               foreign.Matches(f->session) && otherBefore.Matches(other->session) &&
               f->session.LassoOpeningForWork() == 1 && !f->session.PendingLasso(),
           "closed.exact-owner-cleans-once-without-touching-foreign-history-or-session");
    std::weak_ptr<SharedPublicationMemory> weak = f->state.memory;
    const auto localBytes = f->state.memory ? f->state.memory->LocalAllocationBytes() : 0;
    terminal.reset(); // A diagnostic snapshot must not mask the prepared owner's retirement.
    f->state.memory.reset();
    const auto retained = [&]() noexcept
    {
        const auto witness = weak.lock();
        return witness && witness->Bytes() == kAdmissionBytes && witness->ResidentBytes() == kAdmissionBytes &&
               witness->LocalAllocationBytes() == localBytes;
    };
    Expect(opened && cleaned && f->state.prepared && localBytes > 0 && Settle(scheduler) &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes && retained() && foreign.Matches(f->session),
           "closed.prepared-retirement-keeps-exact-original-charge-after-all-display-readers-drop");
    f->state.prepared.reset();
    Expect(opened && cleaned && Settle(scheduler) && weak.expired() &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes && foreign.Matches(f->session),
           "closed.last-strong-owner-leaves-only-charged-weak-control");
    weak.reset();
    Expect(Settle(scheduler) && scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 &&
               foreign.Matches(f->session),
           "closed.last-weak-release-refunds-exact-original-charge");
}

void CheckNewerClosedAndDefault(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool first = Submit(*f) && Ready(*f) && Apply(*f);
        const auto terminal = Close(*f, false);
        const bool newer = Submit(*f, &f->newer) && f->newer.intent && PreparedOpeningCurrent(*f, f->newer);
        const Stamp before(f->session);
        const bool cleaned = DiscardClosed(*f, f->state);
        const bool pendingPreserved = PreparedOpeningCurrent(*f, f->newer);
        const bool acquired =
            f->newer.prepared && f->session.ApplyPreparedLassoOpeningIfCurrent(f->newer.prepared.value());
        const auto display = f->session.CaptureBoundedLassoDisplayForWork();
        Expect(first && terminal && newer && cleaned && pendingPreserved && acquired && display &&
                   display.value().display.key == BoundedLassoGesture::Key{2, 1} &&
                   display.value().display.points.Bounds() == std::optional(SelectionBounds{9, -3, 9, -3}) &&
                   before.Matches(f->session) && !DiscardClosed(*f, f->state),
               "closed.owned-metadata-cleanup-preserves-newer-pending-opening");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool first = Submit(*f) && Ready(*f) && Apply(*f);
        const auto oldTerminal = Close(*f, false);
        const bool newPrepared = Submit(*f, &f->newer) && PreparedOpeningCurrent(*f, f->newer);
        const bool acquired =
            f->newer.prepared && f->session.ApplyPreparedLassoOpeningIfCurrent(f->newer.prepared.value());
        const Stamp before(f->session);
        const bool discarded = DiscardClosed(*f, f->state);
        const auto display = f->session.CaptureBoundedLassoDisplayForWork();
        Expect(first && oldTerminal && newPrepared && acquired && !discarded && display &&
                   display.value().display.key == BoundedLassoGesture::Key{2, 1} &&
                   display.value().display.points.Bounds() == std::optional(SelectionBounds{9, -3, 9, -3}) &&
                   f->session.IsPreparedLassoGestureCurrent(f->newer.prepared.value()) && before.Matches(f->session),
               "closed.old-owner-cannot-clean-newer-live-opening-two");
        const bool dragged = f->newer.prepared && f->session.DragPreparedMarqueeIfCurrent(f->newer.prepared.value(),
                                                                                          {13.5, 2.5}, false, false);
        const auto terminal = f->newer.prepared
                                  ? f->session.ClosePreparedLassoGestureForWork(f->newer.prepared.value(), false)
                                  : std::nullopt;
        const bool oldCleanup = DiscardClosed(*f, f->state);
        const bool captured = terminal && StopFinish(*f, f->finish, terminal.value());
        const bool duplicate =
            terminal && f->session.PrepareClosedBoundedLassoFinishInput(terminal.value()).has_value();
        Expect(first && acquired && dragged && terminal && !oldCleanup && captured && !duplicate &&
                   terminal.value().display.key == BoundedLassoGesture::Key{2, 2} &&
                   terminal.value().display.points.Bounds() == std::optional(SelectionBounds{9, -3, 14, 3}) &&
                   DiscardIntent(*f, f->finish) && before.Matches(f->session),
               "closed.old-owner-cannot-overwrite-newer-terminal-or-its-one-shot-capture");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        f->state.retainOwnGesture = false;
        const bool first = Submit(*f) && Ready(*f) && Apply(*f);
        const bool dragged = f->session.DragBoundedMarquee({13.5, 2.5}, false, false);
        const auto terminal = f->session.CloseBoundedLassoForWork(false);
        const Stamp before(f->session);
        const bool cleaned = DiscardClosed(*f, f->state);
        const bool captured = terminal && StopFinish(*f, f->finish, terminal.value());
        Expect(first && dragged && terminal && !cleaned && captured &&
                   LiteralDisplay(terminal.value().display, {9, -3, 14, 3}, 2, 4) && DiscardIntent(*f, f->finish) &&
                   before.Matches(f->session),
               "closed.default-non-opt-in-refuses-cleanup-and-preserves-exact-terminal");
    }
}

bool OpenPointDraft(Fixture& fixture, LassoKind kind = LassoKind::Polygonal)
{
    fixture.state.kind = kind;
    return Submit(fixture) && Ready(fixture) && Apply(fixture) && Current(fixture);
}
void CheckPolygonPointMetadata(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const Stamp before(f->session);
    const bool opened = OpenPointDraft(*f);
    Expect(opened, "points.original-admitted-own-polygon-writer-opens");
    if (!opened)
        return;
    auto& prepared = f->state.prepared.value();
    const auto initial = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(initial && initial.value().display.kind == LassoKind::Polygonal &&
               initial.value().display.points.Count() == 1 && !initial.value().display.anchor &&
               initial.value().display.key.revision == 1 &&
               initial.value().display.points.Bounds() == std::optional(SelectionBounds{8.5, -2.5, 8.5, -2.5}),
           "points.literal-first-corner-stays-unrounded-eightpointfive-minus2point5");
    const auto closePoint = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {8.6, -2.5});
    const auto unchanged = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(closePoint && !closePoint.value() && initial && unchanged &&
               SameDisplay(initial.value().display, unchanged.value().display) && Current(*f),
           "points.sub-quarter-distance-is-noop-without-revoking-own-key");
    f->session.lassoKind = LassoKind::Freehand;
    const auto cursor = f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{22, 19});
    const auto cursorDisplay = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(cursor && cursor.value() && cursorDisplay && cursorDisplay.value().display.kind == LassoKind::Polygonal &&
               cursorDisplay.value().display.cursor == std::optional(Vec2{22, 19}) &&
               cursorDisplay.value().display.points.Count() == 1 && cursorDisplay.value().display.key.revision == 2 &&
               Current(*f),
           "cursor.accepted-kind-survives-header-change-and-cursor-adds-no-vertex");
    const auto repeated = f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{22, 19});
    const auto repeatedDisplay = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(repeated && !repeated.value() && cursorDisplay && repeatedDisplay &&
               SameDisplay(cursorDisplay.value().display, repeatedDisplay.value().display),
           "cursor.identical-hover-preserves-revision-and-snapshot");
    const auto invalid =
        f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{(std::numeric_limits<double>::quiet_NaN)(), 19});
    const auto invalidDisplay = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(!invalid && cursorDisplay && invalidDisplay &&
               SameDisplay(cursorDisplay.value().display, invalidDisplay.value().display) && Current(*f),
           "cursor.nonfinite-refusal-preserves-exact-writer-and-old-cursor");
    const auto second = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {12.5, -2.5});
    Expect(second && second.value() && f->session.PendingLasso().value().PointCount() == 2 && Current(*f),
           "points.append-renews-only-own-writer-key");
    const auto boundary = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {12.75, -2.5});
    Expect(boundary && boundary.value() && f->session.PendingLasso().value().PointCount() == 3,
           "points.exact-quarter-distance-is-an-accepted-corner");
    const auto removed = f->session.RemovePreparedPolygonPointIfCurrent(prepared);
    const auto held = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(removed == std::optional{PreparedLassoRemoval::Continues} && held &&
               held.value().display.points.Count() == 2 && held.value().display.key.revision == 5 && Current(*f),
           "pop.nonfinal-corner-removal-renews-current-key");
    const auto cleared = f->session.MovePreparedPolygonCursorIfCurrent(prepared, std::nullopt);
    const auto clearedDisplay = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(cleared && cleared.value() && clearedDisplay && !clearedDisplay.value().display.cursor &&
               clearedDisplay.value().display.points.Count() == 2 && clearedDisplay.value().display.key.revision == 6,
           "cursor.leave-clears-only-rubberband-not-accepted-corners");
    const auto terminal = Close(*f, true);
    Expect(terminal && terminal.value().display.kind == LassoKind::Polygonal && terminal.value().display.antialiased &&
               !terminal.value().display.cursor && terminal.value().display.points.Count() == 2 &&
               terminal.value().display.key.revision == 6 && !Current(*f) && !f->session.PendingLasso(),
           "close.original-two-corners-and-terminal-aa-exclude-hover");
    Expect(!f->session.ExtendPreparedLassoPointIfCurrent(prepared, {16, 8}) &&
               !f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{16, 8}) &&
               !f->session.RemovePreparedPolygonPointIfCurrent(prepared) && !Abort(*f),
           "close.every-late-operation-refuses-consumed-own-authority");
    Expect(before.Matches(f->session) && LiteralRedoBranch(f->session),
           "metadata.no-selection-history-generation-or-redo-publication");
    Expect(held && held.value().display.cursor == std::optional(Vec2{22, 19}) &&
               held.value().display.points.Bounds() == std::optional(SelectionBounds{8.5, -2.5, 12.5, -2.5}),
           "retention.earlier-corner-cursor-snapshot-stays-immutable");
}
void CheckPolygonLastCorner(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const Stamp before(f->session);
    const bool opened = OpenPointDraft(*f);
    Expect(opened, "last-corner.original-writer-opens");
    if (!opened)
        return;
    auto& prepared = f->state.prepared.value();
    const auto held = f->session.CaptureBoundedLassoDisplayForWork();
    const auto result = f->session.RemovePreparedPolygonPointIfCurrent(prepared);
    Expect(result == std::optional{PreparedLassoRemoval::Ended} && !f->session.PendingLasso() && !Current(*f),
           "last-corner.matched-pop-ends-writer-and-draft-in-one-transition");
    Expect(before.Matches(f->session) && LiteralRedoBranch(f->session),
           "last-corner.preserve-original-selection-history-generation-and-redo");
    Expect(!f->session.RemovePreparedPolygonPointIfCurrent(prepared) &&
               !f->session.ExtendPreparedLassoPointIfCurrent(prepared, {10, 10}) &&
               !f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{10, 10}) && !Abort(*f),
           "last-corner.ended-writer-cannot-be-edited-or-rolled-back-again");
    Expect(held && held.value().display.points.Count() == 1 && held.value().display.key.revision == 1 &&
               scheduler.ReservedMemoryBytes() >= kAdmissionBytes,
           "last-corner.held-snapshot-and-control-keep-original-admission");
}
void CheckPolygonForeign(JobScheduler& scheduler, bool history)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = OpenPointDraft(*f);
    Expect(opened, history ? "foreign-history.original-writer-opens" : "foreign-writer.original-writer-opens");
    if (!opened)
        return;
    if (history)
        f->session.history.Begin(L"Foreign polygon edit", f->session.Document(), f->session.ActiveLayerID());
    else
        f->session.ExtendLasso({16, 8});
    const auto held = f->session.CaptureBoundedLassoDisplayForWork();
    const auto* address = &f->session.PendingLasso().value();
    const Stamp foreign(f->session);
    auto& prepared = f->state.prepared.value();
    Expect(!Current(*f) && !f->session.ExtendPreparedLassoPointIfCurrent(prepared, {17, 8}) &&
               !f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{17, 8}) &&
               !f->session.RemovePreparedPolygonPointIfCurrent(prepared) && !Abort(*f),
           history ? "foreign-history.all-metadata-and-abort-operations-refuse"
                   : "foreign-writer.all-metadata-and-abort-operations-refuse");
    Expect(PreservesDraft(f->session, held, address) && foreign.Matches(f->session),
           history ? "foreign-history.exact-foreign-draft-and-history-survive"
                   : "foreign-writer.exact-foreign-draft-and-history-survive");
}
void CheckPolygonWrongKind(JobScheduler& scheduler, LassoKind kind)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const bool opened = OpenPointDraft(*f, kind);
    const char* openLabel = kind == LassoKind::Freehand ? "wrong-kind.freehand-opens" : "wrong-kind.rectangle-opens";
    Expect(opened, openLabel);
    if (!opened)
        return;
    auto& prepared = f->state.prepared.value();
    const auto held = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(!f->session.MovePreparedPolygonCursorIfCurrent(prepared, Vec2{16, 8}) &&
               !f->session.RemovePreparedPolygonPointIfCurrent(prepared) && Current(*f) && held &&
               PreservesDraft(f->session, held, &f->session.PendingLasso().value()),
           kind == LassoKind::Freehand ? "wrong-kind.freehand-rejects-polygon-cursor-and-pop"
                                       : "wrong-kind.rectangle-rejects-polygon-cursor-and-pop");
    if (kind == LassoKind::Rectangle)
        Expect(!f->session.ExtendPreparedLassoPointIfCurrent(prepared, {16, 8}) && Current(*f),
               "wrong-kind.marquee-rejects-point-append-without-revoking-authority");
    else
    {
        const auto appended = f->session.ExtendPreparedFreehandIfCurrent(prepared, {16, 8});
        Expect(appended && appended.value() && Current(*f) && f->session.PendingLasso().value().PointCount() == 2,
               "wrong-kind.existing-freehand-wrapper-keeps-its-accepted-point-behavior");
    }
}
void CheckPolygonCapacity(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    const Stamp before(f->session);
    const bool opened = OpenPointDraft(*f);
    Expect(opened, "capacity.original-one-block-writer-opens");
    if (!opened)
        return;
    auto& prepared = f->state.prepared.value();
    bool accepted = true;
    for (unsigned index = 1; index <= 127; ++index)
    {
        const auto appended = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {8.5 + .25 * index, -2.5});
        accepted = accepted && appended && appended.value();
    }
    const auto held = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(accepted && held && held.value().display.points.Count() == 128 && Current(*f),
           "capacity.one-sealed-block-plus-inline-tail-holds128-corners");
    const auto refused = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {48, -2.5});
    const auto after = f->session.CaptureBoundedLassoDisplayForWork();
    Expect(!refused && held && after && SameDisplay(held.value().display, after.value().display) && Current(*f),
           "capacity.exhaustion-refuses-without-dropping-accepted-points-or-own-key");
    const auto removed = f->session.RemovePreparedPolygonPointIfCurrent(prepared);
    const auto refill = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {99, -2.5});
    Expect(removed == std::optional{PreparedLassoRemoval::Continues} && refill && refill.value() &&
               f->session.PendingLasso().value().PointCount() == 128 && Current(*f),
           "capacity.backspace-refills-inline-tail-without-recycling-sealed-node");
    const auto secondRefusal = f->session.ExtendPreparedLassoPointIfCurrent(prepared, {100, -2.5});
    Expect(!secondRefusal && held && held.value().display.points.Count() == 128 &&
               held.value().display.points.Bounds() == std::optional(SelectionBounds{8.5, -2.5, 40.25, -2.5}) &&
               Current(*f) && before.Matches(f->session),
           "capacity.refill-keeps-earlier-snapshot-and-never-admits-extra-node");
    Expect(Abort(*f) && !f->session.PendingLasso() && before.Matches(f->session),
           "capacity.exhausted-original-can-still-cancel-without-selection-publication");
}
} // namespace

int RunLassoGestureAuthorityTests()
{
    gFamily = "lasso-gesture-authority";
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

int RunLassoIntentCleanupTests()
{
    gFamily = "lasso-intent-cleanup";
    gChecks = 0;
    gFailures = 0;
    auto scheduler = std::make_unique<JobScheduler>();
    const bool started =
        scheduler->Initialize(1, {}, {SampleMemory, nullptr}) == S_OK && scheduler->EnableDeferredRelease();
    Expect(started, "scheduler.original-ticket-worker-and-retirement-initialized");
    if (started)
    {
        CheckStoppedOpeningIntents(*scheduler);
        CheckAcquiredAndSupersededIntents(*scheduler);
        CheckFinishIntentKinds(*scheduler);
        CheckParallelIntentKinds(*scheduler);
        CheckIntentSessions(*scheduler);
        CheckIntentForeignEditors(*scheduler);
        CheckClosedIntentRetirement(*scheduler);
        CheckNewerClosedAndDefault(*scheduler);
        const bool settled = Settle(*scheduler);
        Expect(gChecks == kIntentCleanupChecks - 1 && settled && scheduler->IsIdle() &&
                   scheduler->ReservedMemoryBytes() == 0 && scheduler->ReleasingMemoryBytes() == 0,
               "scheduler.all-original-ticket-charges-and-records-drain");
    }
    scheduler->Close();
    std::printf("SUMMARY lasso-intent-cleanup checks=%u failures=%u\n", gChecks, gFailures);
    return gChecks == kIntentCleanupChecks && gFailures == 0 ? 0 : static_cast<int>(gFailures ? gFailures : 1);
}

int RunPreparedPolygonAuthorityTests()
{
    gFamily = "prepared-polygon-authority";
    gChecks = gFailures = 0;
    auto scheduler = std::make_unique<JobScheduler>();
    const bool started =
        scheduler->Initialize(1, {}, {SampleMemory, nullptr}) == S_OK && scheduler->EnableDeferredRelease();
    Expect(started, "scheduler.original-worker-and-retirement-ready");
    if (started)
    {
        CheckPolygonPointMetadata(*scheduler);
        CheckPolygonLastCorner(*scheduler);
        CheckPolygonForeign(*scheduler, false);
        CheckPolygonForeign(*scheduler, true);
        CheckPolygonWrongKind(*scheduler, LassoKind::Freehand);
        CheckPolygonWrongKind(*scheduler, LassoKind::Rectangle);
        CheckPolygonCapacity(*scheduler);
        const bool settled = Settle(*scheduler);
        Expect(settled && scheduler->IsIdle() && !scheduler->ReservedMemoryBytes() &&
                   !scheduler->ReleasingMemoryBytes(),
               "scheduler.all-exact-original-owners-and-records-refund");
    }
    scheduler->Close();
    std::printf("SUMMARY prepared-polygon-authority checks=%u failures=%u expected=39\n", gChecks, gFailures);
    return gChecks == 39 && !gFailures ? 0 : static_cast<int>(gFailures ? gFailures : 1);
}

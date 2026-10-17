#include "Document.Tests.LassoOpeningSettings.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "DocumentDraft.h"
#include "Executor.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "LassoOpeningWork.h"
#include "LocallyOwnedPublication.h"
#include "MemoryAdmission.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
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
unsigned gChecks = 0;
unsigned gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s lasso-opening-settings.%s\n", condition ? "PASS" : "FAIL", label);
}

bool SampleMemory(void*, MemorySnapshot& out) noexcept
{
    constexpr auto available = kMemoryAdmissionHeadroomBytes + kAdmissionBytes + 64ULL * 1024 * 1024;
    out = {available, available};
    return true;
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
    Vec2 point{8.5, -2.5};
    SelectionMode mode = SelectionMode::New;
    LassoOpeningSettings settings{LassoKind::Rectangle, false};
    bool capturedSettings = true;
    bool marquee = true;
    bool probeInvalid = false;
    bool probeInputMove = false;
    bool cancelWork = false;
    std::uint64_t reservedBefore = 0;
    std::thread::id owner;
    std::optional<LassoOpeningInput> input;
    std::optional<PreparedLassoOpening> prepared;
    std::optional<PreparedLassoOpening> movedPrepared;
    std::shared_ptr<SharedPublicationMemory> memory;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::int32_t bindResult = E_UNEXPECTED;
    unsigned runs = 0;
    bool noCaptureBeforeDelivery = false;
    bool captureOnOwner = false;
    bool captured = false;
    bool originalReservationOnWorker = true;
    bool resumedOriginal = false;
    bool completedOnOwner = false;
    bool unboundRefused = false;
    bool boundOriginal = false;
    bool invalidKindRefused = false;
    bool invalidModeRefused = false;
    bool nonfiniteRefused = false;
    bool invalidPreservedScalars = false;
    bool movedInputRefused = false;
};

class OpeningJob final : public Job
{
  public:
    explicit OpeningJob(OpeningState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        auto& s = _state;
        ++s.runs;
        s.originalReservationOnWorker = s.originalReservationOnWorker &&
                                        context.reservedMemoryBytes == kAdmissionBytes && context.deferredRetirement &&
                                        std::this_thread::get_id() != s.owner;
        if (!_preparing)
            return {};
        if (!s.input || !s.originalReservationOnWorker)
            return {JobStatus::Failed, E_ABORT};
        s.memory = SharedPublicationMemory::CreateForWork(PreparedLassoOpening::MemoryBytes(kBlocks));
        CancelSource cancelled;
        if (s.cancelWork)
            (void)cancelled.Cancel();
        const auto token = s.cancelWork ? cancelled.Token() : context.cancellation;
        auto prepared = [&]() -> std::optional<PreparedLassoOpening>
        {
            if (!s.probeInputMove)
                return DocumentSession::PrepareLassoOpening(std::move(s.input.value()), kBlocks, token, s.memory);
            auto moved = std::move(s.input.value());
            auto refused = DocumentSession::PrepareLassoOpening(std::move(s.input.value()), kBlocks, token, s.memory);
            s.movedInputRefused = !refused;
            return DocumentSession::PrepareLassoOpening(std::move(moved), kBlocks, token, s.memory);
        }();
        if (!prepared)
            return {s.cancelWork ? JobStatus::Cancelled : JobStatus::Failed, E_ABORT};
        s.prepared.emplace(std::move(prepared.value()));
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        if (_preparing || result.status != JobStatus::Finished || result.error != S_OK || ticket != s.ticket ||
            !s.originalReservationOnWorker || !s.scheduler->InUiCallback() ||
            s.scheduler->ReservedMemoryBytes() != s.reservedBefore + kAdmissionBytes)
            return false;
        s.captureOnOwner = std::this_thread::get_id() == s.owner;
        auto input = s.capturedSettings ? s.session->PrepareLassoOpeningInput(s.point, s.mode, s.settings)
                                        : s.session->PrepareLassoOpeningInput(s.point, s.mode, s.marquee);
        s.captured = input.has_value();
        if (input)
            s.input.emplace(std::move(input.value()));
        if (s.probeInvalid && s.captured)
        {
            const Stamp before(*s.session);
            const auto opening = s.session->LassoOpeningForWork();
            s.invalidKindRefused = !s.session->PrepareLassoOpeningInput(
                {1, 2}, SelectionMode::New, LassoOpeningSettings{static_cast<LassoKind>(255), false});
            s.invalidModeRefused = !s.session->PrepareLassoOpeningInput(
                {1, 2}, static_cast<SelectionMode>(255), LassoOpeningSettings{LassoKind::Rectangle, false});
            const auto nan = std::numeric_limits<double>::quiet_NaN();
            const auto infinity = std::numeric_limits<double>::infinity();
            const auto badX = s.session->PrepareLassoOpeningInput({nan, 2}, SelectionMode::New,
                                                                  LassoOpeningSettings{LassoKind::Rectangle, false});
            const auto badY = s.session->PrepareLassoOpeningInput({1, infinity}, SelectionMode::New,
                                                                  LassoOpeningSettings{LassoKind::Ellipse, true});
            const auto negativeInfinity = s.session->PrepareLassoOpeningInput(
                {-infinity, 2}, SelectionMode::New, LassoOpeningSettings{LassoKind::Freehand, false});
            s.nonfiniteRefused = !badX && !badY && !negativeInfinity;
            s.invalidPreservedScalars =
                before.Matches(*s.session) && !s.session->PendingLasso() && s.session->LassoOpeningForWork() == opening;
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
        // No Resume or useful worker stage follows this final whole-reservation binding.
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
        auto layer = ImageLayer::Blank(L"Opening settings", {2, 2});
        constexpr std::array<std::uint8_t, 16> pixels{0,   0, 255, 255, 0,   255, 0,   255,
                                                      255, 0, 0,   255, 255, 255, 255, 255};
        layer.asset =
            RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
        const auto active = layer.id;
        document.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(document).Seal(), active);
        session.history.Reset();
        session.selectionAntialiased = true;
        session.SelectAll();
        session.Undo(); // A real Redo branch makes opening-only history preservation observable.
    }
    ~Fixture()
    {
        if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
            scheduler.Close(); // Join callbacks before any state/session/payload can be destroyed.
        session.CancelLasso();
        primary.movedPrepared.reset();
        primary.prepared.reset();
        primary.memory.reset();
        secondary.movedPrepared.reset();
        secondary.prepared.reset();
        secondary.memory.reset();
    }
    JobScheduler& scheduler;
    DocumentSession session;
    OpeningState primary;
    OpeningState secondary;
};

bool Submit(Fixture& fixture, OpeningState& state)
{
    auto& scheduler = fixture.scheduler;
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        return false;
    state.scheduler = &scheduler;
    state.session = &fixture.session;
    state.owner = std::this_thread::get_id();
    state.reservedBefore = scheduler.ReservedMemoryBytes();
    std::unique_ptr<Job> job = std::make_unique<OpeningJob>(state);
    JobOptions options{};
    options.kind = JobClass::Selection;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.allowMemoryWaiting = false;
    options.reserveRetainedMemory = true;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        return false;
    state.noCaptureBeforeDelivery = !state.captured && !state.input && !fixture.session.PendingLasso();
    return SettleJobs(scheduler, kTimeoutMs) == SettleResult::Settled;
}

bool Ready(const OpeningState& state) noexcept
{
    return state.noCaptureBeforeDelivery && state.captureOnOwner && state.captured && state.resumedOriginal &&
           state.runs == 2 && state.originalReservationOnWorker && state.completedOnOwner &&
           state.terminal.status == JobStatus::Finished && state.terminal.error == S_OK && state.prepared &&
           state.bindResult == S_OK && state.boundOriginal;
}

bool Current(Fixture& fixture, OpeningState& state) noexcept
{
    return state.prepared && fixture.session.IsPreparedLassoOpeningCurrent(state.prepared.value());
}

bool Apply(Fixture& fixture, OpeningState& state) noexcept
{
    return state.prepared && fixture.session.ApplyPreparedLassoOpeningIfCurrent(state.prepared.value());
}

bool LiteralDisplay(DocumentSession& session, LassoKind kind, SelectionMode mode, bool aa, Vec2 first, bool hasAnchor,
                    std::uint64_t opening = 1) noexcept
{
    const auto& draft = session.PendingLasso();
    const auto display = session.CaptureBoundedLassoDisplayForWork();
    if (!draft || !display)
        return false;
    const auto& value = display.value().display;
    return draft.value().IsBounded() && draft.value().kind == kind && draft.value().mode == mode &&
           draft.value().PointCount() == 1 && draft.value().FirstPoint() == std::optional(first) &&
           draft.value().anchor == (hasAnchor ? std::optional(first) : std::nullopt) && value.kind == kind &&
           value.mode == mode && value.antialiased == aa && value.points.Count() == 1 &&
           value.points.Bounds() == std::optional(SelectionBounds{first.x, first.y, first.x, first.y}) &&
           value.anchor == (hasAnchor ? std::optional(first) : std::nullopt) && !value.cursor &&
           value.key.opening == opening && value.key.revision == 1;
}

bool FullRectangle(const DocumentSession& session) noexcept
{
    const auto& selection = session.Selection();
    constexpr std::array<Vec2, 4> expected{{{0, 0}, {32, 0}, {32, 32}, {0, 32}}};
    if (!selection || !selection.value().antialiased || selection.value().loops.size() != 1 ||
        selection.value().loops[0].size() != 4)
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (selection.value().loops[0][i] != expected[i])
            return false;
    return true;
}

void CheckCapturedRectangle(JobScheduler& scheduler)
{
    auto fixture = std::make_unique<Fixture>(scheduler);
    auto& session = fixture->session;
    auto& state = fixture->primary;
    state.mode = SelectionMode::Subtract;
    state.settings = {LassoKind::Rectangle, false}; // Accepted Down settings, before header changes.
    session.marqueeKind = LassoKind::Ellipse;
    session.selectionAntialiased = true;
    const Stamp before(session);
    const bool settled = Submit(*fixture, state);
    Expect(settled && state.noCaptureBeforeDelivery && state.captureOnOwner && state.captured && state.resumedOriginal,
           "captured.rectangle.capture-after-original-admission");
    Expect(Ready(state) && state.originalReservationOnWorker && state.boundOriginal,
           "captured.rectangle.worker-prepares-off-owner-and-binds-original-ticket");
    Expect(Ready(state) && state.unboundRefused, "captured.rectangle.unbound-result-refuses-without-mutation");
    session.marqueeKind = LassoKind::Freehand;
    session.lassoKind = LassoKind::Polygonal;
    session.selectionAntialiased = false;
    const bool currentAfterAaChange = Ready(state) && Current(*fixture, state);
    session.selectionAntialiased = true; // A-B-A after capture still cannot replace captured false.
    Expect(currentAfterAaChange && Current(*fixture, state),
           "captured.rectangle.before-and-after-header-changes-remain-current");
    const bool applied = Ready(state) && Apply(*fixture, state);
    const auto& draft = session.PendingLasso();
    Expect(applied && draft && draft.value().anchor == std::optional(Vec2{9, -3}) &&
               draft.value().FirstPoint() == std::optional(Vec2{9, -3}),
           "captured.rectangle.rounded-anchor-and-first-point-are-9-minus3");
    Expect(applied && LiteralDisplay(session, LassoKind::Rectangle, SelectionMode::Subtract, false, {9, -3}, true),
           "captured.rectangle.display-retains-kind-mode-and-aa-false");
    Expect(applied && before.Matches(session) && session.history.CanRedo() && !session.Selection(),
           "captured.rectangle.opening-preserves-document-history-and-generation");
    const auto envelope = BoundedLassoPoints::MemoryBytes(1) + LocallyOwnedPublicationAllowance<BoundedLassoGesture>();
    Expect(applied && draft && draft.value().points.empty() && draft.value().points.capacity() == 0 && state.memory &&
               state.memory->LocalAllocationBytes() > 0 && state.memory->LocalAllocationBytes() <= envelope &&
               state.memory->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance() < kAdmissionBytes &&
               draft.value().RetirementMemoryBytes() == envelope,
           "captured.rectangle.no-owner-point-vector-or-unbounded-metadata");
}

void CheckOtherCapturedKinds(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.settings = {LassoKind::Polygonal, true};
        s.mode = SelectionMode::Add;
        s.point = {8.5, -2.5};
        f->session.lassoKind = LassoKind::Freehand;
        f->session.selectionAntialiased = false;
        const Stamp before(f->session);
        const bool ready = Submit(*f, s) && Ready(s);
        f->session.lassoKind = LassoKind::Rectangle;
        Expect(ready && Current(*f, s), "captured.polygonal.original-ticket-prepares-after-header-change");
        Expect(ready && Apply(*f, s) &&
                   LiteralDisplay(f->session, LassoKind::Polygonal, SelectionMode::Add, true, {8.5, -2.5}, false) &&
                   before.Matches(f->session),
               "captured.polygonal.keeps-fractional-point-and-aa-true");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.settings = {LassoKind::Ellipse, false};
        s.point = {-8.5, 2.5};
        f->session.marqueeKind = LassoKind::Rectangle;
        const bool ready = Submit(*f, s) && Ready(s);
        f->session.marqueeKind = LassoKind::Polygonal;
        Expect(ready && Current(*f, s), "captured.ellipse.original-ticket-ignores-live-marquee-kind");
        Expect(ready && Apply(*f, s) &&
                   LiteralDisplay(f->session, LassoKind::Ellipse, SelectionMode::New, false, {-9, 3}, true),
               "captured.ellipse.rounds-both-signs-and-keeps-aa-false");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.settings = {LassoKind::Freehand, false};
        s.point = {-8.5, 2.5};
        f->session.lassoKind = LassoKind::Polygonal;
        const bool ready = Submit(*f, s) && Ready(s);
        f->session.lassoKind = LassoKind::Ellipse;
        Expect(ready && Current(*f, s), "captured.freehand.original-ticket-ignores-live-lasso-kind");
        Expect(ready && Apply(*f, s) &&
                   LiteralDisplay(f->session, LassoKind::Freehand, SelectionMode::New, false, {-8.5, 2.5}, false),
               "captured.freehand.keeps-fractional-point-and-aa-false");
    }
}

void CheckLiveOverload(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.capturedSettings = false;
        f->session.marqueeKind = LassoKind::Ellipse;
        f->session.selectionAntialiased = true;
        const bool ready = Submit(*f, s) && Ready(s);
        Expect(ready && Current(*f, s), "live.marquee.bool-overload-captures-current-kind");
        f->session.selectionAntialiased = false;
        Expect(ready && Apply(*f, s) &&
                   LiteralDisplay(f->session, LassoKind::Ellipse, SelectionMode::New, false, {9, -3}, true),
               "live.marquee.aa-is-read-at-apply");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.capturedSettings = false;
        s.marquee = false;
        f->session.lassoKind = LassoKind::Polygonal;
        f->session.selectionAntialiased = false;
        const bool ready = Submit(*f, s) && Ready(s);
        Expect(ready && Current(*f, s), "live.lasso.bool-overload-captures-current-kind");
        f->session.selectionAntialiased = true;
        Expect(ready && Apply(*f, s) &&
                   LiteralDisplay(f->session, LassoKind::Polygonal, SelectionMode::New, true, {8.5, -2.5}, false),
               "live.lasso.aa-is-read-at-apply");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.capturedSettings = false;
        s.marquee = false;
        f->session.lassoKind = LassoKind::Freehand;
        const bool ready = Submit(*f, s) && Ready(s) && Current(*f, s);
        const Stamp before(f->session);
        f->session.lassoKind = LassoKind::Polygonal;
        Expect(ready && !Current(*f, s) && !Apply(*f, s) && before.Matches(f->session) && !f->session.PendingLasso(),
               "live.lasso.kind-mismatch-refuses-without-mutation");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto& s = f->primary;
        s.capturedSettings = false;
        f->session.marqueeKind = LassoKind::Rectangle;
        const bool ready = Submit(*f, s) && Ready(s) && Current(*f, s);
        const Stamp before(f->session);
        f->session.marqueeKind = LassoKind::Ellipse;
        Expect(ready && !Current(*f, s) && !Apply(*f, s) && before.Matches(f->session) && !f->session.PendingLasso(),
               "live.marquee.kind-mismatch-refuses-without-mutation");
    }
}

void CheckInvalidInputs(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    auto& s = f->primary;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const bool seeded = f->session.SetLassoOpeningCountersForTest(0, maximum - 2);
    const Stamp before(f->session);
    s.probeInvalid = true;
    const bool ready = seeded && Submit(*f, s) && Ready(s);
    Expect(ready && s.invalidKindRefused, "invalid.kind-refused-before-intent-rotation");
    Expect(ready && s.invalidModeRefused, "invalid.mode-refused-before-intent-rotation");
    Expect(ready && s.nonfiniteRefused, "invalid.nan-and-infinite-coordinates-refused");
    const bool applied = ready && Current(*f, s) && Apply(*f, s);
    Expect(applied && LiteralDisplay(f->session, LassoKind::Rectangle, SelectionMode::New, false, {9, -3}, true),
           "invalid.original-valid-input-still-current-and-applies");
    Expect(applied && s.invalidPreservedScalars && before.Matches(f->session) && f->session.LassoOpeningForWork() == 1,
           "invalid.refusal-and-opening-preserve-history-and-use-one-opening");
    f->session.CancelLasso();
    auto& last = f->secondary;
    const bool lastReady = Submit(*f, last) && Ready(last);
    Expect(applied && lastReady && Current(*f, last) && Apply(*f, last) &&
               LiteralDisplay(f->session, LassoKind::Rectangle, SelectionMode::New, false, {9, -3}, true, 2),
           "invalid.rejected-attempts-preserve-last-valid-intent-slot");
}

void CheckAuthority(JobScheduler& scheduler)
{
    {
        auto f = std::make_unique<Fixture>(scheduler);
        auto other = std::make_unique<Fixture>(scheduler);
        const bool ready = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        const Stamp sourceBefore(f->session);
        const Stamp otherBefore(other->session);
        Expect(ready && !other->session.ApplyPreparedLassoOpeningIfCurrent(f->primary.prepared.value()) &&
                   !other->session.PendingLasso() && otherBefore.Matches(other->session) &&
                   sourceBefore.Matches(f->session) && Current(*f, f->primary),
               "authority.foreign-session-refuses-and-source-stays-current");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool ready = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        const auto oldSerial = f->session.DocumentSerial();
        f->session.CreateDocument(32, 32, true);
        const Stamp fresh(f->session);
        Expect(ready && f->session.DocumentSerial() != oldSerial && !Current(*f, f->primary) &&
                   !Apply(*f, f->primary) && fresh.Matches(f->session) && !f->session.PendingLasso() &&
                   !f->session.Selection(),
               "authority.replaced-document-refuses-old-opening-without-touching-new-document");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool ready = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        const Stamp before(f->session);
        f->session.BeginTransform(true);
        const auto& edit = f->session.PendingTransformEdit();
        const auto* address = edit ? &edit.value() : nullptr;
        const auto draft = edit ? std::optional(edit.value().draft) : std::optional<LayerTransform>{};
        Expect(ready && address && draft && before.Matches(f->session),
               "authority.foreign-transform-created-without-generation-or-history-change");
        Expect(ready && address && !Current(*f, f->primary) && !Apply(*f, f->primary) &&
                   f->session.PendingTransformEdit() && &f->session.PendingTransformEdit().value() == address &&
                   f->session.PendingTransformEdit().value().draft == draft.value() && before.Matches(f->session) &&
                   !f->session.PendingLasso(),
               "authority.foreign-transform-refuses-and-preserves-exact-editor");
        const bool settled = Submit(*f, f->secondary);
        Expect(address && settled && f->secondary.captureOnOwner && !f->secondary.captured && !f->secondary.prepared &&
                   f->secondary.terminal.status == JobStatus::Failed && f->secondary.terminal.error == E_ABORT &&
                   f->session.PendingTransformEdit() && &f->session.PendingTransformEdit().value() == address &&
                   before.Matches(f->session),
               "authority.foreign-transform-refuses-new-capture-before-mutation");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool ready = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        const Stamp before(f->session);
        f->session.CancelLasso();
        Expect(ready && !Current(*f, f->primary) && !Apply(*f, f->primary) && before.Matches(f->session) &&
                   !f->session.PendingLasso() && f->session.LassoOpeningForWork() == 0,
               "authority.cancelled-intent-refuses-without-resurrection");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        f->primary.cancelWork = true;
        const Stamp before(f->session);
        const bool settled = Submit(*f, f->primary);
        const auto& state = f->primary;
        Expect(settled && state.captured && state.captureOnOwner && state.resumedOriginal &&
                   state.originalReservationOnWorker && state.terminal.status == JobStatus::Cancelled &&
                   state.terminal.error == E_ABORT && state.bindResult == S_OK && state.boundOriginal &&
                   !state.prepared && !f->session.PendingLasso() && before.Matches(f->session),
               "authority.pre-cancelled-worker-produces-no-opening");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool ready = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        f->session.SelectAll();
        const Stamp selected(f->session);
        Expect(ready && !Current(*f, f->primary) && !Apply(*f, f->primary) && selected.Matches(f->session) &&
                   FullRectangle(f->session) && !f->session.PendingLasso(),
               "authority.selection-change-refuses-and-preserves-new-literal-rectangle");
    }
    {
        auto f = std::make_unique<Fixture>(scheduler);
        const bool older = Submit(*f, f->primary) && Ready(f->primary) && Current(*f, f->primary);
        auto& newer = f->secondary;
        newer.settings = {LassoKind::Polygonal, true};
        newer.mode = SelectionMode::Add;
        newer.point = {-4.5, 7.5};
        const bool newest = Submit(*f, newer) && Ready(newer) && Current(*f, newer);
        Expect(older && newest && !Current(*f, f->primary) && newer.reservedBefore == kAdmissionBytes,
               "authority.newer-valid-capture-supersedes-older-input");
        Expect(older && newest && !Apply(*f, f->primary) && Apply(*f, newer) && !Apply(*f, f->primary) &&
                   LiteralDisplay(f->session, LassoKind::Polygonal, SelectionMode::Add, true, {-4.5, 7.5}, false) &&
                   f->primary.memory && f->primary.memory->Bytes() == kAdmissionBytes && newer.memory &&
                   newer.memory->Bytes() == kAdmissionBytes,
               "authority.newer-ticket-applies-and-old-ticket-stays-refused");
    }
}

// A foreign Begin changes the history epoch without changing the document generation/revision.
// The worker-prepared opening must retain its captured history authority across owner acquisition.
void CheckChangedHistory(JobScheduler& scheduler)
{
    for (const bool captured : {true, false})
    {
        auto fixture = std::make_unique<Fixture>(scheduler);
        auto& state = fixture->primary;
        auto& session = fixture->session;
        state.capturedSettings = captured;
        const auto generation = session.Generation();
        const auto revision = session.history.CurrentRevision();
        const bool ready = Submit(*fixture, state) && Ready(state) && Current(*fixture, state);
        const auto previous = session.history.CaptureAuthority();
        session.history.Begin(L"Foreign Begin", session.Document(), session.ActiveLayerID());
        const auto foreign = session.history.CaptureState();
        Expect(ready && session.Generation() == generation && session.history.CurrentRevision() == revision &&
                   !session.history.IsCurrent(previous) && foreign.Depth() == 1 && foreign.FutureCount() == 1,
               captured ? "history.captured.foreign-begin-keeps-generation-and-revision"
                        : "history.live.foreign-begin-keeps-generation-and-revision");
        const bool current = Current(*fixture, state);
        const bool applied = Apply(*fixture, state); // Always attempt publication, even if currentness is wrong.
        Expect(ready && !current && !applied && !session.PendingLasso() && session.history.IsCurrent(foreign) &&
                   session.Generation() == generation && session.history.CurrentRevision() == revision &&
                   session.history.CaptureState().Depth() == 1 && session.history.CaptureState().FutureCount() == 1,
               captured ? "history.captured.rejects-opening-and-preserves-foreign-pending"
                        : "history.live.rejects-opening-and-preserves-foreign-pending");
    }
}

void CheckMoves(JobScheduler& scheduler)
{
    auto f = std::make_unique<Fixture>(scheduler);
    auto& s = f->primary;
    s.probeInputMove = true;
    const Stamp before(f->session);
    const bool ready = Submit(*f, s) && Ready(s);
    Expect(ready && s.movedInputRefused && Current(*f, s), "moves.moved-from-input-refuses-worker-construction");
    if (s.prepared)
        s.movedPrepared.emplace(std::move(s.prepared.value()));
    const bool movedApplied = ready && !Current(*f, s) && !Apply(*f, s) && s.movedPrepared &&
                              f->session.ApplyPreparedLassoOpeningIfCurrent(s.movedPrepared.value());
    Expect(movedApplied && before.Matches(f->session) &&
               LiteralDisplay(f->session, LassoKind::Rectangle, SelectionMode::New, false, {9, -3}, true),
           "moves.moved-prepared-source-refuses-and-destination-applies");
    Expect(movedApplied && !f->session.ApplyPreparedLassoOpeningIfCurrent(s.movedPrepared.value()) &&
               !f->session.IsPreparedLassoOpeningCurrent(s.movedPrepared.value()) && before.Matches(f->session) &&
               LiteralDisplay(f->session, LassoKind::Rectangle, SelectionMode::New, false, {9, -3}, true),
           "moves.applied-result-is-one-shot-with-stable-display-key");
}
} // namespace

int RunLassoOpeningSettingsTests()
{
    gChecks = 0;
    gFailures = 0;
    auto scheduler = std::make_unique<JobScheduler>();
    const bool started =
        scheduler->Initialize(1, {}, {SampleMemory, nullptr}) == S_OK && scheduler->EnableDeferredRelease();
    Expect(started, "scheduler.one-worker-original-admission");
    const auto envelope = BoundedLassoPoints::MemoryBytes(1) + LocallyOwnedPublicationAllowance<BoundedLassoGesture>();
    Expect(envelope > 0 && PreparedLassoOpening::MemoryBytes(1) == envelope &&
               envelope + SharedPublicationMemory::OwnershipAllowance() < kAdmissionBytes,
           "memory.one-block-metadata-envelope");
    if (started)
    {
        CheckCapturedRectangle(*scheduler);
        CheckOtherCapturedKinds(*scheduler);
        CheckLiveOverload(*scheduler);
        CheckInvalidInputs(*scheduler);
        CheckAuthority(*scheduler);
        CheckMoves(*scheduler);
        CheckChangedHistory(*scheduler);
        const bool settled = SettleJobs(*scheduler, kTimeoutMs) == SettleResult::Settled;
        Expect(gChecks == 45 && settled && scheduler->IsIdle() && scheduler->ReservedMemoryBytes() == 0 &&
                   scheduler->ReleasingMemoryBytes() == 0,
               "scheduler.all-original-ticket-charges-and-records-drain");
    }
    scheduler->Close();
    std::printf("SUMMARY lasso-opening-settings checks=%u failures=%u\n", gChecks, gFailures);
    return gChecks == 46 && gFailures == 0 ? 0 : static_cast<int>(gFailures ? gFailures : 1);
}

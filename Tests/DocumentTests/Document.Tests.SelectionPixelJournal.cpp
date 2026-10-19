#include "Document.Tests.SelectionPixelJournal.h"

#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
unsigned failures = 0;
const char* scenario = "setup";
using Pixels = std::array<std::uint8_t, 5 * 3 * 4>;
using Kind = SelectionOutlineGestureTransition::ResultKind;
using Action = HostPolygonalInputJournal::Action;
using Admission = HostPolygonalInputJournal::Admission;

void Expect(bool condition, const char* fact)
{
    failures += condition ? 0u : 1u;
    std::printf("%s selection-pixel-journal.%s.%s\n", condition ? "PASS" : "FAIL", scenario, fact);
}
[[noreturn]] void Broken(const char* fact)
{
    std::printf("FAIL selection-pixel-journal.setup.%s\n", fact);
    std::_Exit(5);
}
Pixels Source()
{
    Pixels pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        pixels[offset] = 255;
        pixels[offset + 3] = 255;
    }
    constexpr std::size_t red = (1 * 5 + 1) * 4;
    pixels[red] = 0;
    pixels[red + 2] = 255;
    return pixels;
}
Pixels Moved(bool duplicate)
{
    auto pixels = Source();
    if (!duplicate)
        std::fill_n(pixels.begin() + (1 * 5 + 1) * 4, 4, std::uint8_t{});
    constexpr std::size_t landing = (2 * 5 + 3) * 4;
    pixels[landing] = 0;
    pixels[landing + 2] = 255;
    return pixels;
}
bool Matches(const DocumentSession& session, const Pixels& expected)
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset || layer->asset->image.Width() != 5 || layer->asset->image.Height() != 3)
        return false;
    Pixels actual{};
    layer->asset->image.ReadRegion(0, 0, 5, 3, actual.data(), 20, 0);
    return actual == expected;
}
struct Memory
{
    unsigned queries = 0;
    static bool Read(void* raw, MemorySnapshot& snapshot) noexcept
    {
        ++static_cast<Memory*>(raw)->queries;
        constexpr auto bytes = kMemoryAdmissionHeadroomBytes + 8ull * 1024 * 1024 * 1024;
        snapshot = {bytes, bytes};
        return true;
    }
};
struct Fixture
{
    Memory memory;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostPolygonalInputJournal> journal = std::make_unique<HostPolygonalInputJournal>();
    std::unique_ptr<SelectionOutlineGestureState> state;
    HostSelectionPressInput::Context arrival;
    explicit Fixture(bool duplicate = false, SelectionMode mode = SelectionMode::New)
    {
        DocumentFixture document;
        document.width = 5;
        document.height = 3;
        document.selection = DocumentSelection{false, {{{1, 1}, {2, 1}, {2, 2}, {1, 2}}}};
        auto layer = ImageLayer::Blank(L"Journal literal", {5, 3});
        const auto bytes = Source();
        layer.asset =
            RasterAsset{Raster::Make(5, 3, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
        const auto active = layer.id;
        document.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(document).Seal(), active);
        // A zero or cancelled gesture must preserve an actual existing redo, not only an empty history.
        session->SetSelection(DocumentSelection{false, {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}}, L"Redo seed");
        session->Undo();
        if (!session->history.CanRedo() || jobs.Initialize(2, {}, {Memory::Read, &memory}) != 0 ||
            !jobs.EnableDeferredRelease())
            Broken("source-redo-and-two-worker-scheduler");
        arrival.document = session->CaptureDocumentReadAuthority().value();
        arrival.ordinal = 1;
        arrival.tool = HostSelectionPressInput::Tool::Lasso;
        arrival.kind = LassoKind::Polygonal;
        arrival.mode = mode;
        arrival.active = active;
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        auto down = Sample({1.5, 1.5}, 1);
        down.modifiers = {true, true, duplicate};
        if (!journal->Begin(81, arrival, down))
            Broken("literal-original-down");
    }
    ~Fixture()
    {
        jobs.Close(); // State/session/journal outlive actual joined retirement.
    }
    static HostPolygonalInputJournal::Sample Sample(Vec2 point, std::uint64_t ordinal)
    {
        HostPolygonalInputJournal::Sample result;
        result.point = result.viewPoint = point;
        result.ordinal = ordinal;
        result.antialiased = false;
        result.leftDown = true;
        return result;
    }
    void Record(Action action, Vec2 point, std::uint64_t ordinal, bool laterModifiers = false)
    {
        auto sample = Sample(point, ordinal);
        sample.modifiers = {laterModifiers, !laterModifiers, laterModifiers};
        sample.leftDown = action != Action::Up;
        if (journal->Record(81, action, sample) != Admission::Recorded)
            Broken("accepted-journal-record");
    }
    void NextDown(Vec2 point, std::uint64_t ordinal, bool pixels)
    {
        auto context = arrival; // The physical family still carries its original document generation.
        context.ordinal = ordinal;
        context.mode = SelectionMode::New;
        auto sample = Sample(point, ordinal);
        sample.modifiers = {false, pixels, false};
        if (journal->Down(81, context, sample) != Admission::Recorded)
            Broken("accepted-younger-down");
    }
    void Submit(bool pixels = true, std::optional<SelectionOutlineGestureTransition> predecessor = {},
                HostPolygonalInputJournal* alternate = nullptr, std::uint64_t stream = 81, bool outlineEnvelope = false)
    {
        if (state && state->controller)
            Broken("replaced-live-parent");
        auto& input = alternate ? *alternate : *journal;
        const auto next = input.Peek(stream);
        if (!next || !next->down)
            Broken("next-parent-must-borrow-original-down");
        const auto mode = next->down->mode;
        const auto admitted = pixels && !outlineEnvelope
                                  ? DescribeSelectionPixelGestureMemory(*session, LassoKind::Polygonal, mode)
                                  : DescribeSelectionOutlineGestureMemory(*session, LassoKind::Polygonal, mode);
        if (!admitted || !admitted->drawing)
            Broken("cached-complete-admission");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, input, stream, admitted->limits);
        state->admissionSource = admitted->source;
        state->drawingPlan = admitted->drawing;
        state->allowPreservingMarquee = mode != SelectionMode::New;
        state->allowInitialPixelMove = pixels;
        state->requireCapturedTarget = true;
        if (predecessor)
            state->predecessor.emplace(std::move(predecessor.value()));
        std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
        if (jobs.Submit(job,
                        {.kind = JobClass::Selection,
                         .serialKey = session->DocumentSerial(),
                         .memoryBytes = admitted->limits.totalBytes,
                         .deferredRetirement = true,
                         .allowMemoryWaiting = false,
                         .reserveRetainedMemory = true,
                         .continuingOutputLimit = admitted->limits.continuingOutputBytes},
                        state->original) != 0)
            Broken("original-submit");
        if (SettleJobs(jobs, 15000) != SettleResult::Settled)
            Broken("event-driven-settlement");
    }
};
void Chained(bool duplicate, bool secondPixels, SelectionMode firstMode)
{
    Fixture fixture(duplicate, firstMode);
    fixture.Record(Action::Move, {3.5, 2.5}, 2, true);
    fixture.Record(Action::Up, {999, 999}, 3);
    fixture.NextDown({3.5, 2.5}, 4, secondPixels);
    fixture.Record(Action::Move, secondPixels ? Vec2{1.5, 1.5} : Vec2{4.5, 2.5}, 5, secondPixels);
    fixture.Record(Action::Up, {999, 999}, 6);
    const auto queries = fixture.memory.queries;
    fixture.Submit();
    const auto& first = *fixture.state;
    Expect(first.completed && first.closed && first.pixelMove && first.outcome.status == JobStatus::Finished &&
               first.captures == 1 && first.hits == 1 && first.moves == 1 && first.sameTicket &&
               first.inputAcknowledged,
           "original-Ctrl-parent-completes-one-hit-lift-move-under-same-admission");
    Expect(fixture.memory.queries == queries + 1 && Matches(*fixture.session, Moved(duplicate)) &&
               fixture.session->history.UndoCount() == 1 && !fixture.session->history.CanRedo(),
           "independent-literal-first-pixels-and-one-history-entry");
    const auto next = fixture.journal->Peek(81);
    Expect(next && next->action == Action::Down && next->sample.ordinal == 4 && fixture.journal->PendingCount() == 3,
           "first-Up-preserves-every-accepted-younger-record");
    auto transition = fixture.state->TakeSuccessor();
    Expect(transition && transition->Kind() == Kind::PixelGesture && !fixture.state->TakeSuccessor(),
           "acknowledged-pixel-successor-is-typed-and-taken-once");
    fixture.Submit(secondPixels, std::move(transition));
    Expect(fixture.state->completed && fixture.state->closed && fixture.state->outcome.status == JobStatus::Finished &&
               fixture.state->pixelMove == secondPixels && fixture.state->captures == 1 && fixture.state->hits == 1 &&
               fixture.state->inputAcknowledged && fixture.journal->PendingCount() == 0,
           "next-original-consumes-exact-pixel-lineage-before-reading-current-source");
    auto expected = Moved(duplicate);
    if (secondPixels)
    {
        expected[(1 * 5 + 1) * 4] = 0;
        expected[(1 * 5 + 1) * 4 + 2] = 255;
        expected[(1 * 5 + 1) * 4 + 3] = 255;
        std::fill_n(expected.begin() + (2 * 5 + 3) * 4, 4, std::uint8_t{});
    }
    Expect(Matches(*fixture.session, expected) && fixture.session->history.UndoCount() == 2,
           "second-operation-has-independent-literal-pixels-and-second-history-entry");
    auto secondTransition = fixture.state->TakeSuccessor();
    Expect(secondTransition && secondTransition->Kind() == (secondPixels ? Kind::PixelGesture : Kind::SelectionOnly),
           "next-published-result-retains-its-own-transition-kind");
}
void Cancelled(Action terminal, bool newerDown = false)
{
    Fixture fixture;
    const auto generation = fixture.session->Generation();
    const auto revision = fixture.session->history.CurrentRevision();
    fixture.Record(Action::Move, {3.5, 2.5}, 2);
    if (newerDown)
        fixture.NextDown({1.5, 1.5}, 3, true);
    else
        fixture.Record(terminal, {999, 999}, 3);
    fixture.Submit();
    Expect(fixture.state->completed && fixture.state->aborted && !fixture.state->closed &&
               fixture.state->outcome.status == JobStatus::Cancelled && fixture.state->inputAcknowledged,
           "pixel-loss-cancels-original-with-real-input-acknowledgement");
    Expect(Matches(*fixture.session, Source()) && fixture.session->Generation() == generation &&
               fixture.session->history.CurrentRevision() == revision && fixture.session->history.CanRedo() &&
               !fixture.session->PendingPixelMove() && !fixture.state->TakeSuccessor(),
           "cancel-preserves-pixels-generation-redo-and-issues-no-publication-authority");
    const auto next = fixture.journal->Peek(81);
    Expect(newerDown ? next && next->action == Action::Down && next->sample.ordinal == 3 : !next,
           "loss-never-consumes-a-younger-original-Down");
}
void ZeroAndKeys()
{
    {
        Fixture zero;
        const auto generation = zero.session->Generation();
        const auto revision = zero.session->history.CurrentRevision();
        zero.Record(Action::Up, {999, 999}, 2);
        zero.Submit();
        auto transition = zero.state->TakeSuccessor();
        Expect(zero.state->closed && zero.state->pixelMove && zero.state->outcome.status == JobStatus::Finished &&
                   transition && transition->Kind() == Kind::PixelGesture && Matches(*zero.session, Source()) &&
                   zero.session->Generation() == generation && zero.session->history.CurrentRevision() == revision &&
                   zero.session->history.CanRedo(),
               "zero-offset-closes-typed-pixel-gesture-without-changing-source-history-or-redo");
    }
    Fixture keys;
    for (std::uint64_t ordinal = 2; ordinal <= 33; ++ordinal)
        keys.Record(ordinal % 2 ? Action::Enter : Action::Backspace, {1.5, 1.5}, ordinal);
    keys.Record(Action::Move, {3.5, 2.5}, 34, true);
    keys.Record(Action::Up, {999, 999}, 35);
    keys.Submit();
    Expect(keys.state->closed && keys.state->pixelMove && keys.state->moves == 1 && keys.state->collectAcks >= 2 &&
               keys.state->lastConsumedOrdinal == 35 && keys.journal->PendingCount() == 0 &&
               Matches(*keys.session, Moved(false)) && keys.session->history.UndoCount() == 1,
           "ignored-keys-count-toward-bounded-batches-without-editing-draft-or-reinterpreting-pixels");
}
void RefuseLineage(bool wrongFamily)
{
    std::unique_ptr<HostPolygonalInputJournal> alternate; // Outlives every borrowing state.
    Fixture fixture;
    fixture.Record(Action::Move, {3.5, 2.5}, 2);
    fixture.Record(Action::Up, {999, 999}, 3);
    fixture.NextDown({3.5, 2.5}, 4, true);
    fixture.Record(Action::Up, {999, 999}, 5);
    fixture.Submit();
    auto transition = fixture.state->TakeSuccessor();
    if (!transition)
        Broken("first-pixel-receipt");
    if (wrongFamily)
    {
        alternate = std::make_unique<HostPolygonalInputJournal>();
        auto context = fixture.arrival;
        context.document = fixture.session->CaptureDocumentReadAuthority().value();
        context.ordinal = 4;
        auto sample = Fixture::Sample({3.5, 2.5}, 4);
        sample.modifiers.control = true;
        if (!alternate->Begin(82, context, sample))
            Broken("independent-family");
    }
    else
        fixture.session->EditForTest([](CanvasDocument&) {});
    const auto revision = fixture.session->history.CurrentRevision();
    fixture.Submit(true, std::move(transition), alternate.get(), wrongFamily ? 82 : 81);
    Expect(fixture.state->completed && fixture.state->outcome.status == JobStatus::Failed &&
               fixture.state->captures == 0 && fixture.state->hits == 0 && !fixture.state->opened,
           "foreign-or-wrong-family-pixel-receipt-refuses-before-owning-source-or-hit");
    Expect(Matches(*fixture.session, Moved(false)) && fixture.session->history.CurrentRevision() == revision &&
               fixture.session->history.UndoCount() == 1 && !fixture.state->TakeSuccessor(),
           "lineage-refusal-preserves-committed-pixels-history-and-younger-raw-prefix");
}
void OverflowAndAdmission()
{
    {
        Fixture overflow;
        const auto generation = overflow.session->Generation();
        for (std::uint64_t ordinal = 2; ordinal <= HostPolygonalInputJournal::kCapacity; ++ordinal)
            overflow.Record(Action::Move, {static_cast<double>(ordinal), 1.5}, ordinal);
        Expect(overflow.journal->Record(81, Action::Move, Fixture::Sample({999, 999}, 513)) == Admission::Overflow,
               "full-raw-journal-reserves-original-loss-instead-of-replacing-accepted-prefix");
        overflow.Submit();
        Expect(overflow.state->outcome.status == JobStatus::Cancelled && overflow.state->moves == 511 &&
                   overflow.state->overflowObserved && overflow.state->terminalLost &&
                   overflow.state->inputAcknowledged && overflow.session->Generation() == generation &&
                   overflow.session->history.CanRedo() && Matches(*overflow.session, Source()),
               "overflow-drains-accepted-prefix-in-batches-and-cancels-without-pixel-history-output");
    }
    Fixture refused;
    refused.Record(Action::Up, {999, 999}, 2);
    refused.Submit(true, {}, nullptr, 81, true);
    Expect(refused.state->outcome.status == JobStatus::Failed && refused.state->captures == 0 &&
               refused.state->hits == 0 && refused.session->history.CanRedo() && Matches(*refused.session, Source()),
           "an-outline-only-admission-never-authorizes-a-journal-pixel-lift");
}
} // namespace

int RunSelectionPixelJournalTests()
{
    failures = 0;
    scenario = "move-then-outline";
    Chained(false, false, SelectionMode::New);
    scenario = "duplicate-then-pixel";
    Chained(true, true, SelectionMode::Add);
    scenario = "ctrl-before-subtract";
    Chained(false, true, SelectionMode::Subtract);
    scenario = "escape";
    Cancelled(Action::Escape);
    scenario = "lost";
    Cancelled(Action::Lost);
    scenario = "leave";
    Cancelled(Action::Leave);
    scenario = "missing-up";
    Cancelled(Action::Lost, true);
    scenario = "zero-and-keys";
    ZeroAndKeys();
    scenario = "foreign-generation";
    RefuseLineage(false);
    scenario = "wrong-family";
    RefuseLineage(true);
    scenario = "overflow-and-admission";
    OverflowAndAdmission();
    std::printf("SUMMARY selection-pixel-journal failures=%u\n", failures);
    return static_cast<int>(failures);
}

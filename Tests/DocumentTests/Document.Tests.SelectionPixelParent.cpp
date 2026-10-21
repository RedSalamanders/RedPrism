#include "Document.Tests.SelectionPixelParent.h"

#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace
{
unsigned failures = 0;
const char* scenario = "setup";
void Expect(bool value, const char* fact)
{
    failures += value ? 0u : 1u;
    std::printf("%s selection-pixel-parent.%s.%s\n", value ? "PASS" : "FAIL", scenario, fact);
}
[[noreturn]] void Broken(const char* message)
{
    std::printf("FAIL selection-pixel-parent.setup.%s\n", message);
    std::_Exit(5);
}
void Settle(JobScheduler& jobs)
{
    if (SettleJobs(jobs, 15000) != SettleResult::Settled)
        Broken("original-does-not-settle");
}
constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255}, blue{255, 0, 0, 255}, transparent{};
DocumentSelection Rect(double x, double y, double w, double h)
{
    return {false, SelectionLoops{{Vec2{x, y}, Vec2{x + w, y}, Vec2{x + w, y + h}, Vec2{x, y + h}}}};
}
bool Pixel(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}
struct Memory
{
    unsigned queries = 0;
    static bool Read(void* raw, MemorySnapshot& output) noexcept
    {
        ++static_cast<Memory*>(raw)->queries;
        constexpr auto available = kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        output = {available, available};
        return true;
    }
};
struct WorkProbe
{
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<unsigned> ownerVisits{0}, opening{0}, closure{0};
    static void Work(void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
    {
        auto& probe = *static_cast<WorkProbe*>(raw);
        if (std::this_thread::get_id() == probe.owner)
            probe.ownerVisits.fetch_add(1);
        if (stage == SelectionOutlineGestureState::WorkStage::PixelOpeningPrepared)
            probe.opening.fetch_add(1);
        if (stage == SelectionOutlineGestureState::WorkStage::PixelClosurePrepared)
            probe.closure.fetch_add(1);
    }
};
struct Fixture
{
    Memory memory;
    WorkProbe probe;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostSelectionPressInput> prefix = std::make_unique<HostSelectionPressInput>();
    std::unique_ptr<SelectionOutlineGestureState> state;
    std::optional<SelectionOutlineGestureMemory> admitted;
    explicit Fixture(SelectionMode mode, bool alt = false, bool shift = false, Vec2 point = {15, 15}, bool mask = false,
                     bool pixelEnvelope = true, bool pixelEnabled = true)
    {
        DocumentFixture doc;
        doc.id = Guid::Generate();
        doc.width = 100;
        doc.height = 40;
        doc.selection = Rect(10, 10, 10, 10);
        auto layer = ImageLayer::Blank(L"Ctrl literal", {100, 40});
        std::vector<std::uint8_t> bytes(100 * 40 * 4);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 100; ++x)
                std::copy((x < 50 ? red : blue).begin(), (x < 50 ? red : blue).end(),
                          bytes.begin() + (y * 100 + x) * 4);
        const auto active = layer.id;
        layer.asset =
            RasterAsset{Raster::Make(100, 40, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
        doc.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(doc).Seal(), active);
        if (mask)
            session->AddLayerMask(true);
        session->SetSelection(Rect(30, 10, 10, 10), L"Fixture Selection");
        session->Undo();
        if (!session->Selection() || session->Selection()->Bounds() != Rect(10, 10, 10, 10).Bounds())
            Broken("literal-selection");
        if (mask)
            session->SelectLayerTarget(active, true);
        if (jobs.Initialize(2, {}, {Memory::Read, &memory}) != 0 || !jobs.EnableDeferredRelease())
            Broken("scheduler");
        admitted = pixelEnvelope ? DescribeSelectionPixelGestureMemory(*session, LassoKind::Rectangle, mode)
                                 : DescribeSelectionOutlineGestureMemory(*session, LassoKind::Rectangle, mode);
        if (!admitted)
            Broken("complete-original-admission");
        HostSelectionPressInput::Context arrival;
        arrival.document = admitted->source.document;
        arrival.ordinal = 1;
        arrival.active = session->ActiveLayerID();
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        arrival.tool = HostSelectionPressInput::Tool::Marquee;
        arrival.kind = LassoKind::Rectangle;
        arrival.mode = mode;
        if (!prefix->Begin(81, arrival, point, {shift, true, alt}, false))
            Broken("accepted-down");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, *prefix, 81, admitted->limits);
        state->admissionSource = admitted->source;
        state->drawingPlan = admitted->drawing;
        state->allowPreservingMarquee = mode != SelectionMode::New;
        state->requireCapturedTarget = true;
        state->allowInitialPixelMove = pixelEnabled;
        state->workObserver = &probe;
        state->workCheckpoint = WorkProbe::Work;
    }
    ~Fixture()
    {
        jobs.Close();
    }
    void Move(Vec2 point, std::uint64_t ordinal, bool shift = true)
    {
        // Deliberately different later modifiers: Shift must not constrain pixels and Alt must not reselect duplicate.
        if (prefix->Move(81, point, {shift, false, false}, false, ordinal) !=
            HostSelectionPressInput::Admission::Recorded)
            Broken("accepted-move");
    }
    void End(bool lost = false, std::uint64_t ordinal = 4)
    {
        if (prefix->Finish(81, false, ordinal, lost) != HostSelectionPressInput::Admission::Recorded)
            Broken("accepted-terminal");
    }
    void Submit()
    {
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
            Broken("submit-original");
    }
    void Release()
    {
        Settle(jobs);
        state.reset();
        prefix.reset();
        session.reset();
        Settle(jobs);
        Expect(jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0,
               "joined-last-readers-refund-complete-original");
    }
};
void Inside(const char* name, bool duplicate, bool shift, bool zero = false, bool lost = false)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(duplicate ? SelectionMode::Subtract
                                       : shift   ? SelectionMode::Add
                                                 : SelectionMode::New,
                                       duplicate, shift);
    const auto image = f->session->ActiveLayer()->asset->image;
    const auto old = f->session->Document().value();
    const auto history = f->session->history.CaptureState();
    const auto generation = f->session->Generation();
    const auto revision = f->session->history.CurrentRevision();
    const auto redo = f->session->history.RedoName();
    const auto undo = f->session->history.UndoCount();
    f->Move({75.4, 20.4}, 2);
    if (zero)
        f->Move({15, 15}, 3);
    f->End(lost);
    f->Submit();
    Settle(f->jobs);
    auto& s = *f->state;
    Expect(s.completed && s.sameTicket && s.inputAcknowledged && s.captures == 1 && s.hits == 1 && s.pixelMove &&
               !s.drawing && !s.clicked && f->memory.queries == 1 && f->probe.ownerVisits.load() == 0 &&
               f->probe.opening.load() == 1 && s.firstBudget == f->admitted->limits.totalBytes,
           "one-admitted-original-worker-hit-lift-and-captured-Ctrl-precedence");
    Expect(!f->session->PendingPixelMove() && !f->session->PendingLasso() && !f->session->IsMovingSelection(),
           "terminal-clears-only-selected-pixel-gesture");
    if (lost)
    {
        Expect(s.outcome.status == JobStatus::Cancelled && s.aborted && !s.closed && s.closurePrepares == 0 &&
                   f->session->Document().value() == old && f->session->history.IsCurrent(history) &&
                   f->session->Generation() == generation && f->session->history.RedoName() == redo,
               "capture-loss-cancels-without-pixel-history-or-outline-fallback");
    }
    else if (zero)
    {
        Expect(s.outcome.status == JobStatus::Finished && s.closed && s.closurePrepares == 1 &&
                   f->session->Document().value() == old && f->session->history.IsCurrent(history) &&
                   f->session->history.CurrentRevision() == revision && f->session->history.RedoName() == redo &&
                   f->session->Generation() == generation,
               "return-to-zero-finishes-without-deselect-history-revision-or-redo-change");
    }
    else
    {
        Expect(s.outcome.status == JobStatus::Finished && s.closed && s.closurePrepares == 1 &&
                   f->probe.closure.load() == 1 && f->session->history.UndoCount() == undo + 1 &&
                   f->session->history.UndoName() == (duplicate ? L"Duplicate Pixels" : L"Move Pixels") &&
                   !f->session->history.CanRedo() && f->session->Generation() == generation + 1,
               "same-original-worker-final-and-one-named-undo");
        const auto& result = f->session->ActiveLayer()->asset->image;
        Expect(f->session->Selection()->Bounds() == Rect(70, 15, 10, 10).Bounds() &&
                   Pixel(result, 10, 10, duplicate ? red : transparent) &&
                   Pixel(result, 19, 19, duplicate ? red : transparent) && Pixel(result, 70, 15, red) &&
                   Pixel(result, 79, 24, red) && Pixel(result, 60, 10, blue) && Pixel(image, 10, 10, red) &&
                   Pixel(image, 70, 15, blue),
               "literal-cut-or-duplicate-60-by-5-move-ignores-later-modifiers-and-keeps-original");
        f->session->Undo();
        Expect(f->session->Selection()->Bounds() == Rect(10, 10, 10, 10).Bounds() &&
                   Pixel(f->session->ActiveLayer()->asset->image, 10, 10, red) &&
                   Pixel(f->session->ActiveLayer()->asset->image, 70, 15, blue),
               "undo-restores-literal-source");
    }
    f->Release();
}
void Outside(SelectionMode mode, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(mode, false, false, Vec2{40, 1});
    const auto source = f->session->ActiveLayer()->asset->image;
    f->Move({60, 15}, 2, false);
    f->End();
    f->Submit();
    Settle(f->jobs);
    const auto& s = *f->state;
    const auto bounds = mode == SelectionMode::New   ? Rect(40, 1, 20, 14).Bounds()
                        : mode == SelectionMode::Add ? Rect(10, 1, 50, 19).Bounds()
                                                     : Rect(10, 10, 10, 10).Bounds();
    Expect(s.completed && s.outcome.status == JobStatus::Finished && s.drawing && s.closed && !s.pixelMove &&
               s.sameTicket && s.hits == 1 && f->probe.opening.load() == 0 &&
               f->session->Selection()->Bounds() == bounds &&
               f->session->Selection()->loops.size() == (mode == SelectionMode::Add ? 2u : 1u) &&
               f->session->ActiveLayer()->asset->image == source,
           "Ctrl-outside-selects-exact-captured-drawing-mode-without-any-lift");
    f->Release();
}
void Refusal(bool mask, bool envelope, bool enabled, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(SelectionMode::New, false, false, Vec2{15, 15}, mask, envelope, enabled);
    const auto before = f->session->Document().value();
    const auto history = f->session->history.CaptureState();
    f->Move({75, 20}, 2);
    f->End();
    f->Submit();
    Settle(f->jobs);
    const auto& s = *f->state;
    Expect(s.completed && s.outcome.status == JobStatus::Failed && !s.opened && !s.drawing && !s.pixelMove &&
               s.inputAcknowledged && !f->session->PendingPixelMove() && !f->session->PendingLasso() &&
               f->session->Document().value() == before && f->session->history.IsCurrent(history),
           "refused-selected-input-has-no-outline-drawing-pixel-or-history-fallback");
    if (!envelope || !enabled)
        Expect(s.captures == 0 && s.hits == 0, "unsupported-or-smaller-envelope-refuses-before-owning-source-or-hit");
    else
        Expect(s.hits == 1 && s.captures == 1, "ineligible-inside-target-is-refused-only-after-admitted-worker-hit");
    f->Release();
}
// Actual original-ticket accounting, distinct from the unbound CPU storage-owner tests.
void BoundOutputSurvivor(unsigned kind, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(SelectionMode::New);
    f->Move({75, 20}, 2);
    f->End();
    f->Submit();
    Settle(f->jobs);
    const auto& image = f->session->ActiveLayer()->asset->image;
    std::weak_ptr<const void> weak;
    if (kind == 0)
        weak = image.WeakStorageForTest();
    else if (kind == 1 && !image.Tiles().empty())
        weak = image.Tiles().front().pixels.WeakStorageForTest();
    else if (kind == 2 && f->session->Selection())
        weak = f->session->Selection()->loops.WeakStorageForTest();
    auto survivor = weak.lock();
    Expect(f->state->outcome.status == JobStatus::Finished && f->state->closed && survivor && f->state->sameTicket &&
               f->state->original.generation != 0 && f->memory.queries == 1,
           "successful-original-publishes-independent-storage-survivor");
    const auto originalOutputCharge = f->admitted->limits.finalOutputBytes;
    f->state.reset();
    f->prefix.reset();
    f->session.reset();
    Settle(f->jobs);
    Expect(f->jobs.ReservedMemoryBytes() == originalOutputCharge && !weak.expired() &&
               f->jobs.ReleasingMemoryBytes() == 0,
           "last-independent-strong-storage-keeps-exact-original-output-charge");
    survivor.reset();
    Settle(f->jobs);
    Expect(weak.expired() && f->jobs.ReservedMemoryBytes() == originalOutputCharge &&
               f->jobs.ReleasingMemoryBytes() == 0,
           "expired-storage-weak-control-keeps-exact-original-output-charge");
    weak.reset();
    Settle(f->jobs);
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "last-weak-control-and-release-acknowledgement-refund-original");
}
} // namespace
int RunSelectionPixelParentTests()
{
    failures = 0;
    Inside("move", false, false);
    Inside("duplicate", true, false);
    Inside("Ctrl-Shift-inside", false, true);
    Inside("return-zero", false, false, true);
    Inside("capture-loss", false, false, false, true);
    Outside(SelectionMode::New, "outside-new");
    Outside(SelectionMode::Add, "outside-add");
    Outside(SelectionMode::Subtract, "outside-subtract");
    Refusal(true, true, true, "mask-inside");
    Refusal(false, false, true, "outline-only-envelope");
    Refusal(false, true, false, "explicit-route-disabled");
    BoundOutputSurvivor(0, "bound-root-survivor");
    BoundOutputSurvivor(1, "bound-tile-survivor");
    BoundOutputSurvivor(2, "bound-selection-survivor");
    return static_cast<int>(failures);
}

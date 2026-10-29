#include "Document.Tests.FreehandPlan.h"

#include "BoundedLassoGesture.h"
#include "Jobs.h"
#include "LassoFinishGeometry.h"
#include "PixelBuffer.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label, const char* caseName = nullptr) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    if (caseName)
        std::printf("%s freehand-plan.%s.%s\n", condition ? "PASS" : "FAIL", label, caseName);
    else
        std::printf("%s freehand-plan.%s\n", condition ? "PASS" : "FAIL", label);
}

bool SameLimits(const BoundedSelectionBoolean::Limits& a, const BoundedSelectionBoolean::Limits& b) noexcept
{
    return a.workspaceBytes == b.workspaceBytes && a.allocations == b.allocations && a.inputPoints == b.inputPoints &&
           a.inputLoops == b.inputLoops && a.outputPoints == b.outputPoints && a.outputLoops == b.outputLoops;
}

bool SamePlan(const LassoFinishGeometry::MemoryPlan& a, const LassoFinishGeometry::MemoryPlan& b) noexcept
{
    return a.retainedArenaBytes == b.retainedArenaBytes && a.traversalBytes == b.traversalBytes &&
           a.outlineBytes == b.outlineBytes && a.workspaceBytes == b.workspaceBytes && a.outputBytes == b.outputBytes &&
           a.preparationBytes == b.preparationBytes;
}

void CheckLiteralLimits()
{
    static_assert(LassoFinishGeometry::kFreehandMaximumPoints == 65'536);
    static_assert(LassoFinishGeometry::kFreehandArenaBlocks == 1023);
    struct Case final
    {
        const char* name;
        SelectionMode mode;
        std::uint64_t sourcePoints, sourceLoops, inputPoints, inputLoops, outputPoints, outputLoops;
        std::size_t workspace;
    };
    // Independently tabulated policy values, not values obtained from the implementation under test.
    // Preserving modes reach the 128 MiB scratch cap even with an absent source. These are resource
    // ceilings enforced by the engine, not a promise that every 65,536-point polygon must succeed.
    constexpr std::array cases{
        Case{"new", SelectionMode::New, 0, 0, 65'540, 2, 262'160, 65'552, 33'554'432},
        Case{"add-absent", SelectionMode::Add, 0, 0, 262'160, 65'553, 524'336, 65'568, 134'217'728},
        Case{"subtract-absent", SelectionMode::Subtract, 0, 0, 262'160, 65'553, 524'336, 65'568, 134'217'728},
        Case{"add-four-one", SelectionMode::Add, 4, 1, 262'164, 65'554, 524'368, 65'573, 134'217'728},
        Case{"subtract-four-one", SelectionMode::Subtract, 4, 1, 262'164, 65'554, 524'368, 65'573, 134'217'728},
        Case{"add-eight-two", SelectionMode::Add, 8, 2, 262'168, 65'555, 524'400, 65'578, 134'217'728},
        Case{"subtract-eight-two", SelectionMode::Subtract, 8, 2, 262'168, 65'555, 524'400, 65'578, 134'217'728},
        Case{"add-max-points", SelectionMode::Add, 8'000'000, 100, 8'262'160, 65'653, 8'000'000, 2'666'667,
             134'217'728},
        Case{"subtract-max-points", SelectionMode::Subtract, 8'000'000, 100, 8'262'160, 65'653, 8'000'000, 2'666'667,
             134'217'728},
        Case{"add-max-loops", SelectionMode::Add, 0, 15'934'451, 262'160, 16'000'004, 524'336, 2'666'667, 134'217'728},
        Case{"subtract-max-loops", SelectionMode::Subtract, 0, 15'934'451, 262'160, 16'000'004, 524'336, 2'666'667,
             134'217'728},
    };
    for (const auto& test : cases)
    {
        const auto limits = LassoFinishGeometry::FreehandLimits(test.mode, test.sourcePoints, test.sourceLoops);
        const auto plan = LassoFinishGeometry::PlanFreehand(test.mode, test.sourcePoints, test.sourceLoops);
        Expect(limits && limits->inputPoints == test.inputPoints && limits->inputLoops == test.inputLoops &&
                   limits->outputPoints == test.outputPoints && limits->outputLoops == test.outputLoops &&
                   limits->workspaceBytes == test.workspace &&
                   limits->allocations == (std::numeric_limits<std::size_t>::max)(),
               "literal-resource-ceilings", test.name);
        Expect(plan && plan->traversalBytes == 4092 && plan->outlineBytes == 1'049'600 &&
                   plan->workspaceBytes == PixelBuffer::RequiredBytes(test.workspace) &&
                   plan->retainedArenaBytes == BoundedLassoPoints::MemoryBytes(1023) && plan->retainedArenaBytes != 0 &&
                   plan->outputBytes != 0 && plan->preparationBytes > plan->workspaceBytes,
               "finite-down-plan-covers-full-arena-and-outline", test.name);
    }
}

void CheckSourceIndependenceAndRefusal()
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto baseLimits = LassoFinishGeometry::FreehandLimits(SelectionMode::New);
    const auto basePlan = LassoFinishGeometry::PlanFreehand(SelectionMode::New);
    const auto ignoredLimits = LassoFinishGeometry::FreehandLimits(SelectionMode::New, maximum, maximum);
    const auto ignoredPlan = LassoFinishGeometry::PlanFreehand(SelectionMode::New, maximum, maximum);
    Expect(baseLimits && ignoredLimits && SameLimits(baseLimits.value(), ignoredLimits.value()),
           "new-ignores-old-source-counts-even-at-uint64-max");
    Expect(basePlan && ignoredPlan && SamePlan(basePlan.value(), ignoredPlan.value()),
           "new-old-source-does-not-inflate-geometry-admission");

    struct Case final
    {
        const char* name;
        SelectionMode mode;
        std::uint64_t points, loops;
    };
    constexpr std::array cases{
        Case{"add-point-cap-plus-one", SelectionMode::Add, 8'000'001, 1},
        Case{"subtract-point-cap-plus-one", SelectionMode::Subtract, 8'000'001, 1},
        Case{"add-loop-sum-cap-plus-one", SelectionMode::Add, 0, 15'934'452},
        Case{"subtract-loop-sum-cap-plus-one", SelectionMode::Subtract, 0, 15'934'452},
        Case{"add-uint64-point-overflow", SelectionMode::Add, maximum, 0},
        Case{"subtract-uint64-point-overflow", SelectionMode::Subtract, maximum, 0},
        Case{"add-uint64-loop-overflow", SelectionMode::Add, 0, maximum},
        Case{"subtract-uint64-loop-overflow", SelectionMode::Subtract, 0, maximum},
        Case{"invalid-mode", static_cast<SelectionMode>(255), 0, 0},
        Case{"invalid-mode-and-max-counts", static_cast<SelectionMode>(255), maximum, maximum},
    };
    for (const auto& test : cases)
        Expect(!LassoFinishGeometry::FreehandLimits(test.mode, test.points, test.loops) &&
                   !LassoFinishGeometry::PlanFreehand(test.mode, test.points, test.loops),
               "refuses-without-plan", test.name);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct Fixture final
{
    std::optional<BoundedSelectionBoolean::Limits> limits;
    std::optional<LassoFinishGeometry::MemoryPlan> admitted;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::optional<BoundedLassoGesture> gesture;
    explicit Fixture(SelectionMode mode, std::uint64_t sourcePoints = 0, std::uint64_t sourceLoops = 0)
        : limits(LassoFinishGeometry::FreehandLimits(mode, sourcePoints, sourceLoops)),
          admitted(LassoFinishGeometry::PlanFreehand(mode, sourcePoints, sourceLoops))
    {
        if (!limits || !admitted)
            return;
        // The exact supplied plan funds the existing accounting path. No guessed fallback allowance.
        memory = SharedPublicationMemory::CreateForWork(admitted->retainedArenaBytes + admitted->preparationBytes);
        if (!memory)
            return;
        auto points = BoundedLassoPoints::CreateForWork(memory, LassoFinishGeometry::kFreehandArenaBlocks);
        if (points)
            gesture.emplace(std::move(points.value()));
    }
};
#pragma warning(pop)

std::optional<BoundedLassoGesture::Display> Terminal(Fixture& fixture, std::span<const Vec2> points, SelectionMode mode)
{
    if (!fixture.gesture || points.empty() ||
        !fixture.gesture->BeginOwner(41, LassoKind::Freehand, mode, true, points.front()))
        return std::nullopt;
    for (const auto point : points.subspan(1))
        if (fixture.gesture->ExtendOwner(fixture.gesture->CurrentOwner(), point) !=
            BoundedLassoGesture::Outcome::Changed)
            return std::nullopt;
    return fixture.gesture->FinishOwner(fixture.gesture->CurrentOwner(), false);
}

std::optional<DocumentSelection> BeforeRectangle()
{
    std::vector<SelectionLoop> loops{{{1, 1}, {7, 1}, {7, 5}, {1, 5}}};
    auto adopted = SelectionLoops::AdoptForWork(std::move(loops));
    if (!adopted)
        return std::nullopt;
    return DocumentSelection{true, std::move(adopted.value())};
}

bool BeforeUnchanged(const std::optional<DocumentSelection>& before) noexcept
{
    constexpr std::array<Vec2, 4> points{{{1, 1}, {7, 1}, {7, 5}, {1, 5}}};
    if (!before || !before->antialiased || before->loops.size() != 1 || before->loops.PointCount() != 4)
        return false;
    const auto& loop = before->loops.Get().front();
    for (std::size_t index = 0; index < points.size(); ++index)
        if (loop[index] != points[index])
            return false;
    return true;
}

double Area(const DocumentSelection& selection) noexcept
{
    double twiceArea = 0;
    for (const auto& loop : selection.loops.Get())
        for (std::size_t index = 0; index < loop.size(); ++index)
        {
            const auto& a = loop[index];
            const auto& b = loop[(index + 1) % loop.size()];
            twiceArea += a.x * b.y - b.x * a.y;
        }
    return std::abs(twiceArea) / 2;
}

bool LiteralCoverage(const DocumentSelection& selection, SelectionMode mode)
{
    const auto raster = SelectionGeometry::Coverage(selection.loops.Get(), 12, 8, false);
    if (raster.Width() != 12 || raster.Height() != 8 || raster.Format() != RasterFormat::Gray8)
        return false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool old = x >= 1 && x < 7 && y >= 1 && y < 5;
            const bool drawn = x >= 4 && x < 10 && y >= 2 && y < 6;
            const bool selected = mode == SelectionMode::New   ? drawn
                                  : mode == SelectionMode::Add ? old || drawn
                                                               : old && !drawn;
            const auto offset = static_cast<std::size_t>(y) * raster.Stride() + static_cast<std::size_t>(x);
            if (raster.Data()[offset] != (selected ? 255 : 0))
                return false;
        }
    return true;
}

void CheckLiteralModeGeometry()
{
    constexpr std::array<Vec2, 4> draw{{{4, 2}, {10, 2}, {10, 6}, {4, 6}}};
    struct Case final
    {
        SelectionMode mode;
        double area;
        const char* name;
    };
    // Two 6x4 rectangles overlap over 3x3: replacement=24, union=39, difference=15.
    constexpr std::array cases{Case{SelectionMode::New, 24, "new"}, Case{SelectionMode::Add, 39, "add"},
                               Case{SelectionMode::Subtract, 15, "subtract"}};
    for (const auto& test : cases)
    {
        Fixture fixture(test.mode, 4, 1);
        const auto terminal = Terminal(fixture, draw, test.mode);
        auto before = BeforeRectangle();
        LassoFinishGeometry::Observer observer;
        if (fixture.limits)
            observer.limits = fixture.limits.value();
        const auto result =
            terminal && before && fixture.memory
                ? LassoFinishGeometry::Prepare(terminal.value(), 12, 8, before, {}, &observer, fixture.memory)
                : std::nullopt;
        Expect(result && result->disposition == LassoFinishGeometry::Disposition::Replace && result->selection &&
                   result->kind == LassoKind::Freehand && !result->selection->antialiased &&
                   Area(result->selection.value()) == test.area,
               "literal-loop-area-and-terminal-aa", test.name);
        Expect(result && result->selection && LiteralCoverage(result->selection.value(), test.mode),
               "all-96-pixel-centers-match-independent-rectangle-predicate", test.name);
        Expect(BeforeUnchanged(before), "old-immutable-selection-retains-four-literal-vertices", test.name);
    }
}

Vec2 SerialPoint(std::uint32_t index) noexcept
{
    return {static_cast<double>(index), static_cast<double>(index % 7)};
}

bool SerialSnapshot(const BoundedLassoPoints::Snapshot& snapshot, std::uint32_t expected)
{
    std::uint32_t index = 0;
    const bool visited = snapshot.VisitForWork(
        [&](std::span<const Vec2> chunk)
        {
            for (const auto point : chunk)
            {
                if (index >= expected || point != SerialPoint(index))
                    return false;
                ++index;
            }
            return true;
        });
    return visited && snapshot.Count() == expected && index == expected;
}

struct Probe final
{
    unsigned copied = 0, beforeClip = 0, prepared = 0;
    static void Observe(void* context, LassoFinishGeometry::Stage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(context);
        self.copied += stage == LassoFinishGeometry::Stage::ChunkCopied;
        self.beforeClip += stage == LassoFinishGeometry::Stage::BeforeClip;
        self.prepared += stage == LassoFinishGeometry::Stage::Prepared;
    }
};

void CheckMultiblockTerminalPlan()
{
    Fixture fixture(SelectionMode::New);
    std::array<Vec2, 130> path{};
    for (std::size_t index = 0; index < 128; ++index)
        path[index] = {static_cast<double>(index), 1};
    path[128] = {127, 5};
    path[129] = {0, 5};
    const auto terminal = Terminal(fixture, path, SelectionMode::New);
    const auto plan = terminal && fixture.limits
                          ? LassoFinishGeometry::Plan(terminal.value(), std::nullopt, fixture.limits.value())
                          : std::nullopt;
    Expect(terminal && terminal->points.Count() == 130 && terminal->points.TraversalScratchBytes() == 8 &&
               !fixture.gesture->ActiveOwner() && fixture.gesture->PointCountOwner() == 0,
           "terminal-retains-130-points-two-sealed-blocks-and-two-point-tail");
    Expect(plan && fixture.admitted && plan->outlineBytes == 3104 && plan->traversalBytes == 8 &&
               plan->retainedArenaBytes == terminal->points.RetainedArenaBytes() &&
               fixture.memory->LocalAllocationBytes() != 0 &&
               plan->retainedArenaBytes >= fixture.memory->LocalAllocationBytes() &&
               plan->retainedArenaBytes == fixture.admitted->retainedArenaBytes &&
               plan->preparationBytes <= fixture.admitted->preparationBytes &&
               plan->outputBytes == fixture.admitted->outputBytes &&
               plan->workspaceBytes == fixture.admitted->workspaceBytes,
           "down-plan-covers-actual-terminal-count-traversal-and-charged-arena");
    Probe probe;
    LassoFinishGeometry::Observer observer;
    observer.context = &probe;
    observer.checkpoint = &Probe::Observe;
    if (fixture.limits)
        observer.limits = fixture.limits.value();
    const auto result =
        terminal && fixture.memory
            ? LassoFinishGeometry::Prepare(terminal.value(), 256, 8, std::nullopt, {}, &observer, fixture.memory)
            : std::nullopt;
    Expect(result && result->selection && Area(result->selection.value()) == 508 && probe.copied == 3 &&
               probe.beforeClip == 1 && probe.prepared == 1,
           "long-collinear-path-copies-three-chunks-and-prepares-literal-area508");
}

void CheckMaximumPointRefusal()
{
    Fixture fixture(SelectionMode::New);
    const bool began = fixture.gesture &&
                       fixture.gesture->BeginOwner(52, LassoKind::Freehand, SelectionMode::New, false, SerialPoint(0));
    Expect(began, "complete-gesture-arena-created-with-original-plan");
    if (!began)
        return;
    const auto charge = fixture.memory->LocalAllocationBytes();
    Expect(fixture.memory->SealLocalAllocations(), "arena-ledger-sealed-before-capacity-exercise");
    bool accepted = true;
    BoundedLassoPoints::Snapshot old;
    for (std::uint32_t index = 1; index < 65'536; ++index)
    {
        if (fixture.gesture->ExtendOwner(fixture.gesture->CurrentOwner(), SerialPoint(index)) !=
            BoundedLassoGesture::Outcome::Changed)
        {
            accepted = false;
            break;
        }
        if (index == 129)
            old = fixture.gesture->CaptureOwner().points;
    }
    const auto full = fixture.gesture->CaptureOwner();
    Expect(accepted && full.points.Count() == 65'536 && full.points.TraversalScratchBytes() == 4092 &&
               SerialSnapshot(full.points, 65'536),
           "exactly-65536-literal-vertices-fit-1023-sealed-blocks-and-inline-tail");
    Expect(SerialSnapshot(old, 130), "older-130-point-snapshot-survives-complete-gesture-growth");
    const auto key = fixture.gesture->CurrentOwner();
    Expect(fixture.gesture->ExtendOwner(key, SerialPoint(65'536)) == BoundedLassoGesture::Outcome::Refused &&
               fixture.gesture->CurrentOwner() == key && fixture.gesture->PointCountOwner() == 65'536 &&
               fixture.memory->LocalAllocationBytes() == charge &&
               full.points.SameCapture(fixture.gesture->CaptureOwner().points) && SerialSnapshot(old, 130) &&
               SerialSnapshot(full.points, 65'536),
           "65537th-refusal-preserves-key-points-old-snapshot-and-original-charge");
    Expect(fixture.gesture->CancelOwner(key) && !fixture.gesture->ActiveOwner() &&
               fixture.gesture->PointCountOwner() == 0 && !fixture.gesture->FinishOwner(key) &&
               fixture.gesture->ExtendOwner(key, {0, 0}) == BoundedLassoGesture::Outcome::Refused,
           "overflow-policy-cancel-prevents-terminal-or-late-append");
    Expect(SerialSnapshot(old, 130) && SerialSnapshot(full.points, 65'536) &&
               fixture.memory->LocalAllocationBytes() == charge,
           "cancel-keeps-old-snapshots-stable-and-original-arena-charged");
}

struct CancelAt final
{
    CancelSource* source;
    LassoFinishGeometry::Stage target;
    unsigned occurrence, seen = 0;
    static void Observe(void* context, LassoFinishGeometry::Stage stage) noexcept
    {
        auto& self = *static_cast<CancelAt*>(context);
        if (stage == self.target && ++self.seen == self.occurrence)
            static_cast<void>(self.source->Cancel());
    }
};

void CheckCancelledPreparation()
{
    constexpr std::array<Vec2, 4> draw{{{4, 2}, {10, 2}, {10, 6}, {4, 6}}};
    constexpr std::array stages{LassoFinishGeometry::Stage::BeforeCopy, LassoFinishGeometry::Stage::ChunkCopied,
                                LassoFinishGeometry::Stage::BeforeClip, LassoFinishGeometry::Stage::Clipped,
                                LassoFinishGeometry::Stage::BeforeMode, LassoFinishGeometry::Stage::BeforeAdopt,
                                LassoFinishGeometry::Stage::Prepared};
    Fixture fixture(SelectionMode::Subtract, 4, 1);
    const auto terminal = Terminal(fixture, draw, SelectionMode::Subtract);
    auto before = BeforeRectangle();
    for (const auto stage : stages)
    {
        CancelSource cancellation;
        CancelAt probe{&cancellation, stage, 1};
        if (!fixture.limits)
            return; // Missing admission also fails the suite's exact expected assertion count.
        LassoFinishGeometry::Observer observer{&probe, &CancelAt::Observe, fixture.limits.value()};
        const auto result = terminal && before && fixture.memory
                                ? LassoFinishGeometry::Prepare(terminal.value(), 12, 8, before, cancellation.Token(),
                                                               &observer, fixture.memory)
                                : std::nullopt;
        const auto stageName = static_cast<unsigned>(stage);
        std::printf("CONTROL freehand-plan.cancel-stage=%u reached=%u\n", stageName, probe.seen);
        Expect(probe.seen == 1 && cancellation.Token().IsCancelled(), "each-injected-cancellation-stage-is-reached");
        Expect(!result && BeforeUnchanged(before) && terminal && terminal->points.Count() == 4,
               "cancelled-geometry-preparation-publishes-nothing-and-keeps-source");
    }

    Fixture multiblock(SelectionMode::New);
    std::array<Vec2, 130> path{};
    for (std::uint32_t index = 0; index < 130; ++index)
        path[index] = SerialPoint(index);
    const auto many = Terminal(multiblock, path, SelectionMode::New);
    CancelSource cancellation;
    CancelAt probe{&cancellation, LassoFinishGeometry::Stage::ChunkCopied, 2};
    if (!multiblock.limits)
        return; // Never substitute an unplanned default envelope.
    LassoFinishGeometry::Observer observer{&probe, &CancelAt::Observe, multiblock.limits.value()};
    const auto result = many && multiblock.memory
                            ? LassoFinishGeometry::Prepare(many.value(), 256, 8, std::nullopt, cancellation.Token(),
                                                           &observer, multiblock.memory)
                            : std::nullopt;
    Expect(probe.seen == 2 && cancellation.Token().IsCancelled(),
           "multiblock-cancellation-reaches-second-sealed-chunk");
    Expect(!result && many && SerialSnapshot(many->points, 130),
           "mid-copy-cancellation-keeps-complete-original-snapshot-without-result");
}

struct CeilingOutcome final
{
    bool completed = false;
    bool produced = false;
    JobResult result{JobStatus::Failed, E_UNEXPECTED};
};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class CeilingJob final : public Job
{
  public:
    CeilingJob(BoundedLassoGesture::Display terminal, BoundedSelectionBoolean::Limits limits,
               std::shared_ptr<SharedPublicationMemory> memory, Probe& probe, CeilingOutcome& outcome)
        : _terminal(std::move(terminal)), _observer{&probe, &Probe::Observe, limits}, _memory(std::move(memory)),
          _outcome(outcome)
    {
    }
    JobResult Run(JobContext& context) override
    {
        _result =
            LassoFinishGeometry::Prepare(_terminal, 12, 8, std::nullopt, context.cancellation, &_observer, _memory);
        return _result ? JobResult{} : JobResult{JobStatus::Failed, E_INVALIDARG};
    }
    void Completed(JobResult result) noexcept override
    {
        _outcome.result = result;
        _outcome.produced = _result.has_value();
        _outcome.completed = true;
    }

  private:
    const BoundedLassoGesture::Display _terminal;
    const LassoFinishGeometry::Observer _observer;
    const std::shared_ptr<SharedPublicationMemory> _memory;
    CeilingOutcome& _outcome;
    std::optional<LassoFinishGeometry::Result> _result;
};
#pragma warning(pop)

void CheckEnforcedWorkerCeilings()
{
    constexpr std::array<Vec2, 4> draw{{{4, 2}, {10, 2}, {10, 6}, {4, 6}}};
    Fixture fixture(SelectionMode::New);
    const auto terminal = Terminal(fixture, draw, SelectionMode::New);
    constexpr std::array names{"output-three-vertices", "workspace-one-byte", "zero-engine-allocations"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (!fixture.limits)
        {
            Expect(false, "worker-ceiling-fixture-has-plan", names[index]);
            continue;
        }
        auto reduced = fixture.limits.value();
        if (index == 0)
            reduced.outputPoints = 3; // A nondegenerate rectangle needs four output vertices.
        else if (index == 1)
            reduced.workspaceBytes = 1;
        else
            reduced.allocations = 0;
        Probe probe;
        CeilingOutcome outcome;
        JobScheduler jobs;
        const auto sample = [](void*, MemorySnapshot& value) noexcept
        {
            value = {kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024,
                     kMemoryAdmissionHeadroomBytes + 2ULL * 1024 * 1024 * 1024};
            return true;
        };
        const bool initialized = jobs.Initialize(1, {}, {sample, nullptr}) == S_OK;
        JobTicket ticket;
        bool admitted = false;
        if (initialized && terminal && fixture.memory && fixture.admitted)
        {
            std::unique_ptr<Job> work =
                std::make_unique<CeilingJob>(terminal.value(), reduced, fixture.memory, probe, outcome);
            admitted = jobs.Submit(work,
                                   {.kind = JobClass::Selection,
                                    .memoryBytes = fixture.admitted->retainedArenaBytes +
                                                   fixture.admitted->preparationBytes + sizeof(CeilingJob) + 4096,
                                    .allowMemoryWaiting = false},
                                   ticket) == S_OK;
        }
        Expect(terminal && LassoFinishGeometry::Plan(terminal.value(), std::nullopt, reduced).has_value() &&
                   initialized && admitted,
               "reduced-ceiling-is-a-valid-plan-before-engine-refusal", names[index]);
        const bool settled = admitted && SettleJobs(jobs, 10000) == SettleResult::Settled;
        jobs.Close();
        Expect(settled && outcome.completed && outcome.result.status == JobStatus::Failed &&
                   outcome.result.error == E_OUTOFMEMORY && !outcome.produced && jobs.ReservedMemoryBytes() == 0 &&
                   probe.beforeClip == 1 && probe.prepared == 0 && terminal && terminal->points.Count() == 4,
               "enforced-engine-ceiling-refuses-instead-of-returning-partial-output", names[index]);
    }
}
} // namespace

int RunFreehandPlanTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckLiteralLimits();
    CheckSourceIndependenceAndRefusal();
    CheckLiteralModeGeometry();
    CheckMultiblockTerminalPlan();
    CheckMaximumPointRefusal();
    CheckCancelledPreparation();
    CheckEnforcedWorkerCeilings();
    // Source count: 22 literal-plan + 12 source/refusal + 9 geometry + 3 multiblock +
    // 7 capacity/snapshot + 16 cancellation + 6 engine-enforcement = 75 checks.
    constexpr int expected = 75;
    std::printf("SUMMARY freehand-plan assertions=%d failures=%d expected=%d\n", gAssertions, gFailures, expected);
    return gFailures == 0 && gAssertions == expected ? 0 : (gFailures ? gFailures : 1);
}

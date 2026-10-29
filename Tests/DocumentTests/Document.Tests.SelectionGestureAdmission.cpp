#include "Document.Tests.SelectionGestureAdmission.h"
#include "RedPrism/SelectionOutlineGestureJob.h"
#include "RedPrism/SelectionOutlineGestureMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "TransformEdit.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <windows.h>

namespace
{
unsigned checks = 0, failures = 0;
const char* current = "setup";
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s selection-gesture-admission.%s.%s\n", value ? "PASS" : "FAIL", current, label);
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL selection-gesture-admission.setup.%s\n", label);
    std::_Exit(1);
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("event-settlement");
}
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Memory final
{
    std::atomic<std::uint64_t> queries{0};
    bool scarce = false;
    static bool Sample(void* raw, MemorySnapshot& value) noexcept
    {
        auto& self = *static_cast<Memory*>(raw);
        ++self.queries;
        const auto available = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        value = {available, available};
        return true;
    }
};
#pragma warning(pop)

void Install(DocumentSession& session, bool large = false)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = large ? 8192 : 8;
    document.height = large ? 5464 : 8;
    SelectionLoop points;
    if (large)
    {
        points.reserve(65540);
        for (unsigned i = 0; i < 65536; ++i)
            points.push_back({1.0 + 6.0 * i / 65535.0, 2});
        points.insert(points.end(), {{7, 2}, {7, 6}, {1, 6}, {1, 2}});
    }
    else
        points = {{1, 2}, {7, 2}, {7, 6}, {1, 6}};
    document.selection = DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{std::move(points)}}};
    auto layer = ImageLayer::Blank(L"Admission reference", {8, 8});
    layer.asset =
        RasterAsset{Raster::MakeSparse(document.width, document.height, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0),
                    {},
                    layer.name};
    const auto id = layer.id;
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document).Seal(), id);
}
void Eligibility()
{
    current = "eligibility";
    auto session = std::make_unique<DocumentSession>();
    Expect(!DescribeSelectionOutlineGestureMemory(*session), "no-document-refuses");
    Install(*session);
    const auto generation = session->Generation();
    const auto revision = session->history.CurrentRevision();
    const auto root = session->Selection()->loops.Identity();
    const auto description = DescribeSelectionOutlineGestureMemory(*session);
    Expect(description && description->limits.continuingOutputBytes > 0 && description->limits.finalOutputBytes > 0 &&
               description->limits.totalBytes >
                   description->limits.continuingOutputBytes + description->limits.finalOutputBytes,
           "valid-source-reserves-opening-final-and-temporary-phases");
    Expect(description && session->IsDocumentReadCurrent(description->source.document) &&
               session->Generation() == generation && session->history.CurrentRevision() == revision &&
               session->Selection()->loops.Identity() == root,
           "cached-description-preserves-document-geometry-and-history");
    session->BeginTransform(true);
    Expect(session->PendingTransformEdit() && !DescribeSelectionOutlineGestureMemory(*session),
           "foreign-pending-transform-refuses-without-settlement");
    session = std::make_unique<DocumentSession>();
    Install(*session);
    session->SetSelection(std::nullopt, L"No selection");
    Expect(!DescribeSelectionOutlineGestureMemory(*session), "absent-selection-refuses");
    session->SetSelection(DocumentSelection{false, SelectionLoops{}}, L"Empty selection");
    Expect(!DescribeSelectionOutlineGestureMemory(*session), "explicit-empty-selection-refuses");
    session = std::make_unique<DocumentSession>();
    Install(*session, true);
    const auto large = DescribeSelectionOutlineGestureMemory(*session);
    Expect(large && description && large->geometryBytes > description->geometryBytes &&
               large->sourceBytes > description->sourceBytes && large->limits.totalBytes > 8ull * 1024 * 1024,
           "long-outline-and45mp-source-exceed-fixed-fixture-reservation");
    Expect(large && !session->Document()->layers[0].asset->image.IsMaterialized(),
           "large-sparse-source-is-never-materialized-by-description");
}
enum class Mode
{
    Move,
    Click,
    Large,
    Stale,
    Scarce
};
void Run(Mode mode, const char* name)
{
    current = name;
    auto session = std::make_unique<DocumentSession>();
    Install(*session, mode == Mode::Large);
    const auto description = DescribeSelectionOutlineGestureMemory(*session);
    if (!description)
        SetupFailure("complete-description");
    const auto generation = session->Generation();
    const auto revision = session->history.CurrentRevision();
    auto prefix = std::make_unique<HostSelectionPressInput>();
    Memory memory;
    memory.scarce = mode == Mode::Scarce;
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("scheduler");
    HostSelectionPressInput::Context arrival;
    arrival.document = description->source.document;
    arrival.ordinal = 1;
    arrival.tool = HostSelectionPressInput::Tool::Marquee;
    arrival.kind = LassoKind::Rectangle;
    if (!prefix->Begin(19, arrival, {2, 3}, {}, false))
        SetupFailure("down");
    if (mode != Mode::Click && prefix->Move(19, {4, 6}, {}, false, 2) != HostSelectionPressInput::Admission::Recorded)
        SetupFailure("move");
    if (prefix->Finish(19, false, 3) != HostSelectionPressInput::Admission::Recorded)
        SetupFailure("up");
    auto state = std::make_unique<SelectionOutlineGestureState>(scheduler, *session, *prefix, 19, description->limits);
    state->admissionSource = description->source;
    std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
    const JobOptions options{.kind = JobClass::Selection,
                             .serialKey = session->DocumentSerial(),
                             .memoryBytes = description->limits.totalBytes,
                             .deferredRetirement = true,
                             .allowMemoryWaiting = false,
                             .reserveRetainedMemory = true,
                             .continuingOutputLimit = description->limits.continuingOutputBytes};
    const auto admitted = scheduler.Submit(job, options, state->original);
    if (mode == Mode::Scarce)
    {
        if (admitted != S_OK || job)
            SetupFailure("scarce-descriptor-submission");
        Settle(scheduler);
        Expect(state->completed && state->outcome.status == JobStatus::Failed &&
                   state->outcome.error == E_OUTOFMEMORY && state->captures == 0 && state->runs == 0 &&
                   !state->opened && state->inputAcknowledged && !prefix->Busy(),
               "memory-refusal-acknowledges-original-descriptor-before-run-or-source-capture");
        Expect(session->Generation() == generation && session->history.CurrentRevision() == revision &&
                   session->Selection()->Bounds() == std::optional<SelectionBounds>{{1, 2, 7, 6}},
               "memory-refusal-preserves-selection-generation-and-history");
    }
    else
    {
        if (admitted != S_OK)
            SetupFailure("original-admission");
        // No owner completion has run yet, even if Allocate already joined. An external edit therefore
        // tests the admission-to-capture interval without a sleep or worker scheduling assumption.
        if (mode == Mode::Stale)
            session->SetSelection(DocumentSelection{true, SelectionLoops{{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}},
                                  L"External selection");
        Settle(scheduler);
        if (mode == Mode::Stale)
        {
            Expect(state->completed && state->outcome.status == JobStatus::Failed && state->outcome.error == E_ABORT &&
                       state->captures == 0 && !state->opened && !state->aborted,
                   "source-change-after-admission-refuses-before-root-capture");
            Expect(session->Generation() == generation + 1 && session->history.UndoName() == L"External selection" &&
                       session->Selection()->antialiased &&
                       session->Selection()->Bounds() == std::optional<SelectionBounds>{{0, 0, 2, 2}},
                   "stale-refusal-preserves-foreign-selection-and-history");
        }
        else
        {
            Expect(state->completed && state->outcome.status == JobStatus::Finished && state->closed &&
                       state->sameTicket && state->captures == 1 && state->hits == 1 && state->inputAcknowledged &&
                       !prefix->Busy(),
                   "cache-derived-original-admission-completes-exact-parent");
            const auto& selection = session->Selection();
            const bool geometry = mode == Mode::Click
                                      ? !selection
                                      : selection && !selection->antialiased &&
                                            selection->Bounds() == std::optional<SelectionBounds>{{3, 5, 9, 9}} &&
                                            selection->loops.PointCount() == (mode == Mode::Large ? 65540u : 4u) &&
                                            selection->loops[0][0] == Vec2{3, 5};
            Expect(geometry && session->Generation() == generation + 2 &&
                       session->history.UndoName() == (mode == Mode::Click ? L"Deselect" : L"Move Selection") &&
                       session->history.CaptureState().Depth() == 0,
                   "independent-literal-geometry-aa-generation-and-history");
            Expect(state->firstBudget == description->limits.totalBytes &&
                       state->openingBytes <= description->limits.continuingOutputBytes &&
                       state->remainingBudget == description->limits.totalBytes - state->openingBytes &&
                       memory.queries == 1,
                   "opening-transfer-and-useful-remainder-use-one-original-admission");
        }
    }
    // State borrows the session; release it only after its original job and release callbacks joined.
    state.reset();
    session.reset();
    Settle(scheduler);
    Expect(scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0,
           "last-readers-refund-complete-original-reservation");
    scheduler.Close();
}
} // namespace
int RunDocumentSelectionGestureAdmissionTests()
{
    Eligibility();
    Run(Mode::Move, "move");
    Run(Mode::Click, "click");
    Run(Mode::Large, "large");
    Run(Mode::Stale, "stale");
    Run(Mode::Scarce, "scarce");
    std::printf("SUMMARY selection-gesture-admission checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

#include "Document.Tests.SelectionCursorProbe.h"
#include "../Support/DocumentFixture.h"
#include "SelectionHitWork.h"

#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

namespace
{
unsigned checks = 0;
unsigned failures = 0;
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0u : 1u;
    std::printf("%s selection-cursor-probe.%s\n", value ? "PASS" : "FAIL", label);
}
std::unique_ptr<DocumentSession> Make(std::optional<DocumentSelection> selection)
{
    auto session = std::make_unique<DocumentSession>();
    DocumentFixture fixture;
    fixture.width = fixture.height = 64;
    fixture.selection = std::move(selection);
    fixture.layers.push_back(ImageLayer::Blank(L"Cursor source", {64, 64}));
    const auto active = fixture.layers.front().id;
    session->ReplaceDocument(std::move(fixture).Seal(), active);
    return session;
}
DocumentSelection Rectangle()
{
    return {false, SelectionLoops{SelectionLoop{{1, 2}, {7, 2}, {7, 6}, {1, 6}}}};
}
void EmptyAndEligibility()
{
    auto none = std::make_unique<DocumentSession>();
    Expect(!none->DescribeSelectionCursorProbe(), "no-document-has-no-plan");
    auto absent = Make(std::nullopt);
    Expect(!absent->DescribeSelectionCursorProbe(), "absent-selection-has-no-plan");
    auto empty = Make(DocumentSelection{false, SelectionLoops{}});
    Expect(!empty->DescribeSelectionCursorProbe(), "explicit-empty-has-no-plan");
    auto flat = Make(DocumentSelection{false, SelectionLoops{SelectionLoop{{1, 2}, {1, 3}, {1, 6}}}});
    Expect(!flat->DescribeSelectionCursorProbe(), "collapsed-area-has-no-plan");
    auto session = Make(Rectangle());
    EditLayerRecordsForTest(*session,
                            [](DocumentFixture& fixture)
                            {
                                fixture.layers.front().asset =
                                    RasterAsset{Raster::Make(64, 64, RasterFormat::Bgra8888), {}, L"Cursor transform"};
                            });
    session->BeginTransform();
    Expect(session->PendingTransformEdit().has_value(), "foreign-transform-fixture-is-open");
    Expect(!session->DescribeSelectionCursorProbe() && !session->CanMoveSelection({2, 3}),
           "transform-eligibility-matches-numeric-reference");
    session->CancelTransform();
    session->BeginLasso({3, 3}, SelectionMode::New);
    Expect(session->PendingLasso().has_value(), "foreign-lasso-fixture-is-open");
    Expect(!session->DescribeSelectionCursorProbe() && !session->CanMoveSelection({2, 3}),
           "lasso-eligibility-matches-numeric-reference");
    session->CancelLasso();
    Expect(session->DescribeSelectionCursorProbe().has_value(), "closing-foreign-gesture-restores-eligibility");
}
void MatchedAndTampered()
{
    auto session = Make(Rectangle());
    const auto plan = session->DescribeSelectionCursorProbe();
    Expect(plan && plan.value().bounds == SelectionBounds{1, 2, 7, 6} && plan.value().translation == Vec2{},
           "cached-literal-rectangle-plan");
    Expect(plan && plan.value().retainedBytes > session->Selection().value().loops.StorageBytes() &&
               plan.value().retainedBytes < 128 * 1024,
           "small-source-storage-and-control-envelope");
    const auto captured = session->CaptureSelectionCursorProbe(plan.value());
    Expect(captured && captured.value().selection == session->Selection() &&
               captured.value().selection.value().loops.Identity() == session->Selection().value().loops.Identity(),
           "matched-post-admission-capture-shares-exact-source-root");
    Expect(captured && ContainsSelectionForWork(captured.value(), {2, 3}) == std::optional{true},
           "literal-interior-is-selected");
    Expect(captured && ContainsSelectionForWork(captured.value(), {8, 2}) == std::optional{false},
           "literal-outside-is-unselected");
    Expect(captured && ContainsSelectionForWork(captured.value(), {1, 4}) == std::optional{true},
           "literal-edge-is-selected");
    auto wrong = plan.value();
    --wrong.retainedBytes;
    Expect(!session->CaptureSelectionCursorProbe(wrong), "underbound-memory-plan-refuses-capture");
    wrong = plan.value();
    wrong.bounds.maxX += .5;
    Expect(!session->CaptureSelectionCursorProbe(wrong), "foreign-bounds-refuse-capture");
    wrong = plan.value();
    wrong.translation.x = 1;
    Expect(!session->CaptureSelectionCursorProbe(wrong), "foreign-translation-refuses-capture");
    wrong = plan.value();
    ++wrong.document.generation;
    Expect(!session->CaptureSelectionCursorProbe(wrong), "foreign-generation-refuses-capture");
    const auto document = session->Document().value();
    const auto active = session->ActiveLayerID();
    session->ReplaceDocument(document, active);
    Expect(!session->CaptureSelectionCursorProbe(plan.value()), "same-persisted-ids-reopen-refuses-old-plan");
    auto foreign = Make(Rectangle());
    Expect(!foreign->CaptureSelectionCursorProbe(plan.value()), "other-session-refuses-old-plan");
    const auto current = session->DescribeSelectionCursorProbe();
    session->Deselect();
    Expect(!session->CaptureSelectionCursorProbe(current.value()), "changed-source-refuses-capture");
    Expect(captured && ContainsSelectionForWork(captured.value(), {2, 3}) == std::optional{true},
           "captured-immutable-root-survives-source-replacement-and-deselection");
}
void DeferredAndLarge()
{
    auto session = Make(Rectangle());
    const auto identity = session->Selection().value().loops.Identity();
    Expect(session->BeginDeferredSelectionMove(), "deferred-move-fixture-begins");
    session->MoveSelection(5, -2);
    const auto plan = session->DescribeSelectionCursorProbe();
    Expect(plan && plan.value().translation == Vec2{5, -2} && plan.value().bounds == SelectionBounds{6, 0, 12, 4},
           "deferred-move-description-is-translated-scalar-bounds");
    const auto captured = session->CaptureSelectionCursorProbe(plan.value());
    Expect(captured && captured.value().translation == Vec2{5, -2} &&
               captured.value().selection.value().loops.Identity() == identity &&
               captured.value().selection.value().Bounds() == std::optional{SelectionBounds{1, 2, 7, 6}},
           "deferred-capture-keeps-original-root-without-translated-vector");
    Expect(captured && ContainsSelectionForWork(captured.value(), {7, 1}) == std::optional{true} &&
               ContainsSelectionForWork(captured.value(), {2, 3}) == std::optional{false},
           "deferred-literal-new-and-old-interior-hits");
    session->CancelSelectionMove();
    auto largeCoordinate = Make(Rectangle());
    Expect(largeCoordinate->BeginDeferredSelectionMove(), "large-coordinate-move-begins");
    largeCoordinate->MoveSelection(1e15, 1e15);
    const auto hugePlan = largeCoordinate->DescribeSelectionCursorProbe();
    const auto huge = largeCoordinate->CaptureSelectionCursorProbe(hugePlan.value());
    Expect(huge && ContainsSelectionForWork(huge.value(), {1e15 + 2, 1e15 + 3}) == std::optional{true} &&
               ContainsSelectionForWork(huge.value(), {1e15 + 8, 1e15 + 3}) == std::optional{false},
           "large-coordinate-capture-retains-per-vertex-translation-reference");
    largeCoordinate->MoveSelection(1e18, 1e18);
    Expect(!largeCoordinate->DescribeSelectionCursorProbe() && !largeCoordinate->CanMoveSelection({1e18, 1e18}),
           "finite-rounding-collapse-is-empty-like-reference");
    largeCoordinate->CancelSelectionMove();
    SelectionLoop loop;
    loop.reserve(65540);
    for (unsigned i = 0; i < 65536; ++i)
        loop.push_back({1.0 + 6.0 * i / 65535.0, 2});
    loop.insert(loop.end(), {{7, 2}, {7, 6}, {1, 6}, {1, 2}});
    auto large = Make(DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{std::move(loop)}}});
    const auto original = large->Selection();
    const auto largePlan = large->DescribeSelectionCursorProbe();
    const auto largeCapture = large->CaptureSelectionCursorProbe(largePlan.value());
    Expect(largePlan && largePlan.value().retainedBytes > 65540 * sizeof(Vec2) &&
               largePlan.value().retainedBytes < 2 * 1024 * 1024,
           "large-collinear-source-has-cached-declared-envelope");
    Expect(largeCapture && largeCapture.value().selection.value().loops.PointCount() == 65540 &&
               largeCapture.value().selection.value().loops.Identity() == original.value().loops.Identity(),
           "large-post-admission-capture-shares-all-original-vertices");
    Expect(largeCapture && ContainsSelectionForWork(largeCapture.value(), {2, 3}) == std::optional{true} &&
               ContainsSelectionForWork(largeCapture.value(), {8, 3}) == std::optional{false},
           "large-collinear-literal-interior-and-outside");
}
static_assert(std::is_trivially_copyable_v<SelectionCursorProbePlan>);
static_assert(std::is_trivially_destructible_v<SelectionCursorProbePlan>);
static_assert(std::is_nothrow_copy_constructible_v<SelectionDisplayValue>);
} // namespace
int RunSelectionCursorProbeTests()
{
    EmptyAndEligibility();
    MatchedAndTampered();
    DeferredAndLarge();
    std::printf("SUMMARY selection-cursor-probe checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

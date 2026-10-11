#include "Document.Tests.WandBoundedPlan.h"

#include "../Support/DocumentFixture.h"
#include "Selection.h"
#include "SharedPublicationMemory.h"
#include "WandLayerSelectionWork.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
int failures = 0;
void Expect(bool condition, const char* name)
{
    std::printf("%s wand-bounded-plan.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}
DocumentSession EmptySource()
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 4;
    auto layer = ImageLayer::Blank(L"Wand empty source", {4, 4});
    const auto active = layer.id;
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}
double Area(const DocumentSelection& selection)
{
    double twice = 0;
    for (const auto& loop : selection.loops)
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto& a = loop[i];
            const auto& b = loop[(i + 1) % loop.size()];
            twice += a.x * b.y - b.x * a.y;
        }
    return std::abs(twice / 2);
}
void Admission()
{
    auto session = EmptySource();
    WandSettings settings;
    const auto newPlan = session.PlanWandLayerSelectionMemory({1, 1}, settings);
    const auto addPlan = session.PlanWandLayerSelectionMemory({1, 1}, settings, SelectionMode::Add);
    const auto subtract = session.PlanWandLayerSelectionMemory({1, 1}, settings, SelectionMode::Subtract);
    if (!newPlan || !addPlan || !subtract)
        std::_Exit(1);
    std::uint64_t required = 99;
    Expect(EvaluateWandLayerSelectionAdmission(*newPlan, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::UnknownClipperWorkspace &&
               required == 0,
           "new-only-plan-still-refuses-boolean-before-capture");
    Expect(newPlan->booleanWorkspaceBytes == 0 && subtract->booleanWorkspaceBytes == 0,
           "new-and-subtract-without-selection-require-no-engine");
    Expect(EvaluateWandLayerSelectionAdmission(*addPlan, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::Ready &&
               required == addPlan->reservationBytes,
           "mode-aware-add-admits-complete-reservation");
    Expect(addPlan->booleanWorkspaceBytes == 134217792,
           "arena-envelope-includes-128mib-plus-64-byte-allocation-record");
    // Independent capacity reference: 8M double points, ceil(8M/3) loop headers, and the cached
    // geometry/control allowance. This is a capacity bound; a small click never allocates this output.
    constexpr std::uint64_t points = 8000000, loops = 2666667;
    const auto output = points * sizeof(Vec2) + loops * sizeof(SelectionLoop) + sizeof(std::vector<SelectionLoop>) +
                        SelectionLoops::WorkOwnershipAllowance(loops);
    Expect(addPlan->booleanOutputBytes == output, "result-envelope-covers-all-points-loop-capacities-and-controls");
    Expect(addPlan->booleanPhaseBytes == addPlan->tracePhaseBytes + 134217792 + 2 * output &&
               addPlan->preparationBytes >= addPlan->captureHistoryBytes + addPlan->booleanPhaseBytes,
           "trace-arena-and-two-result-owners-coexist-before-admission");
    auto missing = *addPlan;
    missing.booleanWorkspaceBytes = 0;
    Expect(EvaluateWandLayerSelectionAdmission(missing, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::UnknownClipperWorkspace &&
               required == 0,
           "missing-arena-refuses-before-capture");
    auto shortArena = *addPlan;
    --shortArena.booleanWorkspaceBytes;
    Expect(EvaluateWandLayerSelectionAdmission(shortArena, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::IncompletePlan &&
               required == 0,
           "short-arena-cannot-authorize-the-engine");
    auto shortOutput = *addPlan;
    --shortOutput.booleanOutputBytes;
    Expect(EvaluateWandLayerSelectionAdmission(shortOutput, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::IncompletePlan &&
               required == 0,
           "short-result-cannot-authorize-conversion");
    auto overflow = *addPlan;
    overflow.booleanOutputBytes = (std::numeric_limits<std::uint64_t>::max)();
    Expect(EvaluateWandLayerSelectionAdmission(overflow, SelectionMode::Add, false, required) ==
                   WandLayerSelectionAdmission::Overflow &&
               required == 0,
           "conversion-coexistence-overflow-refuses");
    Expect(!session.PlanWandLayerSelectionMemory({1, 1}, settings, static_cast<SelectionMode>(255)),
           "invalid-mode-refuses-planning");
    session.SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 2, 4)}, L"Seed");
    const auto withSelection = session.PlanWandLayerSelectionMemory({1, 1}, settings, SelectionMode::Subtract);
    Expect(withSelection &&
               EvaluateWandLayerSelectionAdmission(*withSelection, SelectionMode::Subtract, true, required) ==
                   WandLayerSelectionAdmission::Ready &&
               withSelection->booleanWorkspaceBytes == 134217792,
           "subtract-with-selection-admits-bounded-engine");
}
void ChargedSelection()
{
    auto session = EmptySource();
    const DocumentSelection seed{false, SelectionGeometry::Rect(0, 0, 2, 4)};
    session.SetSelection(seed, L"Seed");
    session.history.MarkSaved();
    auto memory = SharedPublicationMemory::CreateForWork();
    std::weak_ptr<const SharedPublicationMemory> weak = memory;
    WandSettings settings;
    const auto plan = session.PlanWandLayerSelectionMemory({1, 1}, settings, SelectionMode::Add);
    auto input = session.PrepareWandLayerSelectionInput({1, 1}, settings, true, SelectionMode::Add);
    if (!plan || !input)
        std::_Exit(1);
    auto prepared =
        DocumentSession::PrepareWandLayerSelectionPublication(std::move(*input), nullptr, {}, {}, nullptr, memory);
    Expect(prepared && prepared->ChangesSelection() && prepared->OutputAllocationBytes() > 0 &&
               prepared->OutputAllocationBytes() < plan->reservationBytes,
           "worker-seals-smaller-live-output-envelope");
    Expect(prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(*prepared) && session.Selection() &&
               session.Selection()->antialiased && Area(*session.Selection()) == 16 && session.history.IsModified(),
           "charged-add-publishes-literal-canvas-union-and-aa");
    prepared.reset();
    memory.reset();
    Expect(!weak.expired(), "live-selection-and-history-retain-output-owner");
    session.Undo();
    Expect(session.Selection() == seed && !session.history.IsModified(), "undo-restores-exact-seed-and-saved-revision");
    session.Redo();
    Expect(session.Selection() && session.Selection()->antialiased && Area(*session.Selection()) == 16,
           "redo-retains-literal-union");
    session.ClearDocument();
    Expect(weak.expired(), "closing-document-and-history-releases-last-output-owner");

    auto subtract = EmptySource();
    subtract.SetSelection(seed, L"Seed");
    auto subMemory = SharedPublicationMemory::CreateForWork();
    auto subInput = subtract.PrepareWandLayerSelectionInput({1, 1}, settings, false, SelectionMode::Subtract);
    if (!subInput)
        std::_Exit(1);
    auto sub = DocumentSession::PrepareWandLayerSelectionPublication(std::move(*subInput), nullptr, {}, {}, nullptr,
                                                                     subMemory);
    Expect(sub && sub->OutputAllocationBytes() != 0 && subtract.ApplyPreparedWandLayerSelectionIfCurrent(*sub) &&
               subtract.Selection() && subtract.Selection()->loops.empty(),
           "charged-subtract-publishes-empty-exact-difference");
    subtract.Undo();
    Expect(subtract.Selection() == seed, "subtract-undo-restores-owned-seed");
}
} // namespace

int RunWandBoundedPlanTests()
{
    failures = 0;
    Admission();
    ChargedSelection();
    return failures;
}

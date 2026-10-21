#include "Document.Tests.SelectionOffsetCoreNumeric.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "Selection.h"
#include "SelectionOffsetWork.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s selection-offset-core.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSession Session100()
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 100;
    document.height = 100;
    document.layers.push_back(ImageLayer::Blank(L"Selection offset fixture", Vec2{100, 100}));
    const Guid active = document.layers.front().id;
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased)
{
    return DocumentSelection{antialiased, SelectionGeometry::Rect(x, y, width, height)};
}

std::optional<PreparedSelectionOffset> Prepare(DocumentSession& session, int amount)
{
    auto input = session.PrepareSelectionOffsetInput(amount);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareSelectionOffsetPublication(std::move(*input));
}

bool BoundsAre(const DocumentSelection& selection, double minX, double minY, double maxX, double maxY) noexcept
{
    const auto bounds = selection.Bounds();
    return bounds && *bounds == SelectionBounds{minX, minY, maxX, maxY};
}

bool CoverageIs(const DocumentSelection& selection, int x, int y, std::uint8_t expected)
{
    const Raster coverage = SelectionGeometry::Coverage(selection.loops.Get(), 100, 100, selection.antialiased);
    const auto* pixels = coverage.Data();
    return pixels && pixels[y * 100 + x] == expected;
}

void ExpandContractUndoRedoNumericAndHistory()
{
    DocumentSession session = Session100();
    session.SetSelection(Rect(40, 40, 20, 20, true), L"Seed selection");

    auto expanded = Prepare(session, 5);
    const bool expandedApplied = expanded && session.ApplyPreparedSelectionOffsetIfCurrent(*expanded);
    const auto expandedSelection = session.Selection();
    Expect(expandedApplied && expandedSelection && BoundsAre(*expandedSelection, 35, 35, 65, 65),
           "expand-five-bounds-35-to-65");
    Expect(expandedSelection && CoverageIs(*expandedSelection, 37, 50, 255) &&
               CoverageIs(*expandedSelection, 33, 50, 0),
           "expand-five-coverage-37-in-33-out");
    Expect(expandedSelection && expandedSelection->antialiased, "expand-five-preserves-antialiasing");
    Expect(session.history.UndoName() == L"Expand Selection", "expand-five-history-title");

    auto contracted = Prepare(session, -8);
    const bool contractedApplied = contracted && session.ApplyPreparedSelectionOffsetIfCurrent(*contracted);
    const auto contractedSelection = session.Selection();
    Expect(contractedApplied && contractedSelection && BoundsAre(*contractedSelection, 43, 43, 57, 57),
           "contract-eight-bounds-43-to-57");
    Expect(session.history.UndoName() == L"Contract Selection", "contract-eight-history-title");

    session.Undo();
    const auto afterFirstUndo = session.Selection();
    Expect(afterFirstUndo && BoundsAre(*afterFirstUndo, 35, 35, 65, 65), "undo-contract-restores-expanded-bounds");
    session.Undo();
    const auto afterSecondUndo = session.Selection();
    Expect(afterSecondUndo && BoundsAre(*afterSecondUndo, 40, 40, 60, 60), "second-undo-restores-seed-bounds");
    session.Redo();
    const auto afterFirstRedo = session.Selection();
    Expect(afterFirstRedo && BoundsAre(*afterFirstRedo, 35, 35, 65, 65), "redo-expand-restores-expanded-bounds");
    session.Redo();
    const auto afterSecondRedo = session.Selection();
    Expect(afterSecondRedo && BoundsAre(*afterSecondRedo, 43, 43, 57, 57), "second-redo-restores-contracted-bounds");
}

void CanvasEdgeContractAndExplicitEmpty()
{
    DocumentSession session = Session100();
    session.SetSelection(Rect(0, 0, 100, 100, true), L"Select All");
    DocumentSession reference = Session100();
    reference.SetSelection(Rect(0, 0, 100, 100, true), L"Select All");
    const auto originalRevision = session.history.CurrentRevision();
    const auto referenceRevision = reference.history.CurrentRevision();
    reference.ExpandSelection(10);
    auto expanded = Prepare(session, 10);
    const bool noOpApplied = expanded && session.ApplyPreparedSelectionOffsetIfCurrent(*expanded);
    const auto afterExpand = session.Selection();
    Expect(noOpApplied && afterExpand && reference.Selection() && *afterExpand == *reference.Selection() &&
               BoundsAre(*afterExpand, 0, 0, 100, 100) &&
               session.history.UndoCount() == reference.history.UndoCount() &&
               session.history.UndoName() == reference.history.UndoName() &&
               (session.history.CurrentRevision() != originalRevision) ==
                   (reference.history.CurrentRevision() != referenceRevision),
           "select-all-expand-ten-matches-original-geometry-and-history");

    auto contracted = Prepare(session, -10);
    const bool contractedApplied = contracted && session.ApplyPreparedSelectionOffsetIfCurrent(*contracted);
    const auto inset = session.Selection();
    Expect(contractedApplied && inset && BoundsAre(*inset, 10, 10, 90, 90), "select-all-contract-ten-insets-edges");
    Expect(inset && CoverageIs(*inset, 5, 50, 0) && CoverageIs(*inset, 50, 50, 255),
           "contract-ten-edge-clear-center-covered");

    auto emptied = Prepare(session, -45);
    const bool emptiedApplied = emptied && session.ApplyPreparedSelectionOffsetIfCurrent(*emptied);
    const auto emptySelection = session.Selection();
    Expect(emptiedApplied && emptySelection && emptySelection->loops.empty(),
           "contract-forty-five-publishes-explicit-empty");
    Expect(emptySelection && emptySelection->antialiased, "contract-forty-five-retains-antialiasing");
    Expect(session.history.UndoName() == L"Contract Selection", "contract-forty-five-history-title");
}
} // namespace

int RunSelectionOffsetCoreNumericTests()
{
    gFailures = 0;
    ExpandContractUndoRedoNumericAndHistory();
    CanvasEdgeContractAndExplicitEmpty();
    return gFailures;
}

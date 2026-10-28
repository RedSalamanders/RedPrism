// Isolated host success/golden draft for Application::RunSelectionOffsetHostSelfTest().
#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Selection.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace
{
DocumentSelection OffsetHostRect(double x, double y, double width, double height, bool antialiased = true)
{
    return DocumentSelection{antialiased, SelectionGeometry::Rect(x, y, width, height)};
}

bool HasBounds(const std::optional<DocumentSelection>& selection, double minX, double minY, double maxX, double maxY)
{
    if (!selection)
        return false;
    const auto bounds = selection->Bounds();
    return bounds && *bounds == SelectionBounds{minX, minY, maxX, maxY};
}
} // namespace

int Application::RunSelectionOffsetHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const int originalExpand = _selectionExpandAmount;
    const int originalContract = _selectionContractAmount;
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restoreState = wil::scope_exit(
        [this, originalViewport, originalExpand, originalContract, originalHistoryBudget]() noexcept
        {
            _viewport = originalViewport;
            _selectionExpandAmount = originalExpand;
            _selectionContractAmount = originalContract;
            _session.ClearDocument();
            _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
            DocumentChanged();
        });
    int failures = 0;
    const auto expect = [&failures](const char* name, bool condition, const char* detail) noexcept
    {
        std::fprintf(stderr, "%s selection-offset-host.%s%s%s\n", condition ? "PASS" : "FAIL", name,
                     condition ? "" : ": ", condition ? "" : detail);
        failures += condition ? 0 : 1;
    };
    const auto prepare = [this](int width, int height)
    {
        _session.ClearDocument();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = width;
        document.height = height;
        ImageLayer layer = ImageLayer::Blank(L"Selection offset host fixture",
                                             Vec2{static_cast<double>(width), static_cast<double>(height)});
        const Guid activeLayer = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document).Seal(), activeLayer);
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
    };
    const auto dispatch = [this](int command) { return QueueHostInput(HostCommandInput{command}); };
    const auto dispatchOffset = [this](int amount) { return QueueHostInput(HostSelectionOffsetInput{amount}); };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };

    // Both absent and explicit-empty selections are host-success no-ops, including redo preservation.
    prepare(100, 100);
    _session.SetSelection(OffsetHostRect(0, 0, 10, 10), L"Redo seed");
    _session.Undo();
    const auto absentDocument = _session.Document();
    const Guid absentRevision = _session.history.CurrentRevision();
    const auto absentGeneration = _session.Generation();
    const auto absentFuture = _session.history.CaptureState().FutureCount();
    const bool absentBaselineSettled = settle();
    const auto absentUndoCount = _session.history.UndoCount();
    const auto absentHistory = _session.history.CaptureState();
    const auto absentUndoName = _session.history.UndoName();
    const auto absentRedoName = _session.history.RedoName();
    const auto absentMemory = _jobs.ReservedMemoryBytes();
    const auto absentReceipt = dispatchOffset(5);
    const bool absentSettled = settle();
    expect("absent.no-op-retains-redo-and-admits-no-work",
           absentBaselineSettled && absentSettled && absentReceipt.Read().outcome == HostOutcome::Succeeded &&
               !_session.Selection() && _session.Document() == absentDocument &&
               _session.history.CurrentRevision() == absentRevision && _session.Generation() == absentGeneration &&
               _session.history.CaptureState().FutureCount() == absentFuture && absentFuture == 1 &&
               _session.history.IsCurrent(absentHistory) && _session.history.UndoCount() == absentUndoCount &&
               _session.history.UndoName() == absentUndoName && _session.history.RedoName() == absentRedoName &&
               !_historyWork.Busy() && !_selectionOffsetTicket && !_selectionOffsetStamp &&
               _jobs.ReservedMemoryBytes() == absentMemory,
           "absent selection changed document/history/redo state or admitted worker work");
    _session.SetSelection(DocumentSelection{true, {}}, L"Explicit empty");
    _session.SetSelection(OffsetHostRect(40, 40, 20, 20), L"Empty redo seed");
    _session.Undo();
    const auto emptyDocument = _session.Document();
    const Guid emptyRevision = _session.history.CurrentRevision();
    const auto emptyGeneration = _session.Generation();
    const bool emptyBaselineSettled = settle();
    const auto emptyHistory = _session.history.CaptureState();
    const auto emptyUndoCount = _session.history.UndoCount();
    const auto emptyUndoName = _session.history.UndoName();
    const auto emptyRedoName = _session.history.RedoName();
    const auto emptyMemory = _jobs.ReservedMemoryBytes();
    const auto emptyReceipt = dispatchOffset(5);
    const bool emptySettled = settle();
    expect("empty.explicit-empty-is-successful-no-op",
           emptyBaselineSettled && emptySettled && emptyReceipt.Read().outcome == HostOutcome::Succeeded &&
               _session.Selection() == std::optional<DocumentSelection>{DocumentSelection{true, {}}} &&
               _session.Document() == emptyDocument && _session.history.CurrentRevision() == emptyRevision &&
               _session.Generation() == emptyGeneration && _session.history.IsCurrent(emptyHistory) &&
               _session.history.CaptureState().FutureCount() == 1 && _session.history.UndoCount() == emptyUndoCount &&
               _session.history.UndoName() == emptyUndoName && _session.history.RedoName() == emptyRedoName &&
               !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == emptyMemory && !_selectionOffsetTicket &&
               !_selectionOffsetStamp,
           "explicit empty selection changed or admitted offset work");

    prepare(100, 100);
    _session.SetSelection(OffsetHostRect(40, 40, 20, 20), L"Marquee");
    _selectionExpandAmount = 5;
    const auto expandReceipt = dispatchOffset(5);
    const bool expandSettled = settle();
    const auto expanded = _session.Selection();
    const Raster expandedCoverage =
        expanded ? SelectionGeometry::Coverage(expanded->loops.Get(), 100, 100, expanded->antialiased) : Raster{};
    const auto* expandedPixels = expandedCoverage.Data();
    expect("expand.receipt-and-title",
           expandSettled && expandReceipt.Read().outcome == HostOutcome::Succeeded &&
               _session.history.UndoName() == L"Expand Selection",
           "queued Expand did not complete successfully with its history title");
    expect("expand.literal-bounds", HasBounds(expanded, 35, 35, 65, 65),
           "100x100 rectangle 40,40,20,20 expanded by 5 did not have bounds 35,35,65,65");
    expect("expand.literal-coverage-and-antialias",
           expanded && expanded->antialiased && expandedPixels && expandedPixels[50 * 100 + 37] == 255 &&
               expandedPixels[50 * 100 + 33] == 0,
           "expanded coverage at (37,50)/(33,50) or antialias flag differed from fixed expectation");

    _selectionContractAmount = 8;
    const auto contractReceipt = dispatchOffset(-8);
    const bool contractSettled = settle();
    const auto contracted = _session.Selection();
    expect("contract.receipt-title-and-bounds",
           contractSettled && contractReceipt.Read().outcome == HostOutcome::Succeeded &&
               _session.history.UndoName() == L"Contract Selection" && HasBounds(contracted, 43, 43, 57, 57),
           "Contract 8 did not complete with literal bounds 43,43,57,57 and title");

    const auto undoOne = dispatch(ChromeHost::kCommandUndo);
    const bool undoOneSettled = settle();
    expect("history.undo-contract-restores-expanded",
           undoOneSettled && undoOne.Read().outcome == HostOutcome::Succeeded &&
               HasBounds(_session.Selection(), 35, 35, 65, 65) && _session.history.RedoName() == L"Contract Selection",
           "queued Undo did not restore the expanded geometry and Contract redo title");
    const auto undoTwo = dispatch(ChromeHost::kCommandUndo);
    const bool undoTwoSettled = settle();
    expect("history.undo-expand-restores-original",
           undoTwoSettled && undoTwo.Read().outcome == HostOutcome::Succeeded &&
               HasBounds(_session.Selection(), 40, 40, 60, 60) && _session.history.RedoName() == L"Expand Selection",
           "second queued Undo did not restore original bounds and Expand redo title");
    const auto redoOne = dispatch(ChromeHost::kCommandRedo);
    const bool redoOneSettled = settle();
    expect("history.redo-expand-restores-expanded",
           redoOneSettled && redoOne.Read().outcome == HostOutcome::Succeeded &&
               HasBounds(_session.Selection(), 35, 35, 65, 65) && _session.history.UndoName() == L"Expand Selection",
           "queued Redo did not restore expanded geometry and title");
    const auto redoTwo = dispatch(ChromeHost::kCommandRedo);
    const bool redoTwoSettled = settle();
    expect("history.redo-contract-restores-contracted",
           redoTwoSettled && redoTwo.Read().outcome == HostOutcome::Succeeded &&
               HasBounds(_session.Selection(), 43, 43, 57, 57) && _session.history.UndoName() == L"Contract Selection",
           "second queued Redo did not restore contracted geometry and title");

    // Compare clipped full-canvas publication with the unchanged synchronous session operation.
    prepare(100, 100);
    _session.SetSelection(OffsetHostRect(0, 0, 100, 100), L"Select All");
    const auto reference = std::make_unique<DocumentSession>();
    DocumentFixture referenceDocument;
    referenceDocument.id = Guid::Generate();
    referenceDocument.width = 100;
    referenceDocument.height = 100;
    ImageLayer referenceLayer = ImageLayer::Blank(L"Selection offset host fixture", Vec2{100.0, 100.0});
    const Guid referenceActive = referenceLayer.id;
    referenceDocument.layers.push_back(std::move(referenceLayer));
    reference->ReplaceDocument(std::move(referenceDocument).Seal(), referenceActive);
    reference->history.Reset();
    reference->SetSelection(OffsetHostRect(0, 0, 100, 100), L"Select All");
    reference->ExpandSelection(10);
    const auto referenceSelection = reference->Selection();
    const auto referenceUndoName = reference->history.UndoName();
    const auto referenceUndoCount = reference->history.UndoCount();
    _selectionExpandAmount = 10;
    const auto fullReceipt = dispatchOffset(10);
    const bool fullSettled = settle();
    expect("full-canvas.expand-matches-session-reference",
           fullSettled && fullReceipt.Read().outcome == HostOutcome::Succeeded &&
               _session.Selection() == referenceSelection && _session.history.UndoName() == referenceUndoName &&
               _session.history.UndoCount() == referenceUndoCount,
           "host full-canvas Expand 10 differed from DocumentSession::ExpandSelection(10) reference");
    expect("full-canvas.literal-bounds", HasBounds(_session.Selection(), 0, 0, 100, 100),
           "full-canvas Expand 10 did not retain literal clipped bounds 0,0,100,100");

    // Continue the literal legacy sequence from full canvas: Contract10, then Contract45.
    _selectionContractAmount = 10;
    const auto edgeReceipt = dispatchOffset(-10);
    const bool edgeSettled = settle();
    const auto edgeSelection = _session.Selection();
    const Raster edgeCoverage =
        edgeSelection ? SelectionGeometry::Coverage(edgeSelection->loops.Get(), 100, 100, edgeSelection->antialiased)
                      : Raster{};
    const auto* edgePixels = edgeCoverage.Data();
    expect("contract.edge-coverage-literal",
           edgeSettled && edgeReceipt.Read().outcome == HostOutcome::Succeeded && edgePixels &&
               edgePixels[50 * 100 + 5] == 0 && edgePixels[50 * 100 + 50] == 255 &&
               HasBounds(edgeSelection, 10, 10, 90, 90),
           "Contract 10 edge/center coverage did not match literal 0/255 values");
    _selectionContractAmount = 45;
    const auto emptyContractReceipt = dispatchOffset(-45);
    const bool emptyContractSettled = settle();
    const auto explicitResult = _session.Selection();
    expect("contract.past-center-publishes-explicit-empty",
           emptyContractSettled && emptyContractReceipt.Read().outcome == HostOutcome::Succeeded && explicitResult &&
               explicitResult->IsEmpty() && _session.history.UndoName() == L"Contract Selection",
           "Contract 45 did not publish an explicit empty selection with the literal title");

    // Event-time value is captured by the real callback route before a later UI setting change.
    prepare(100, 100);
    _session.SetSelection(OffsetHostRect(40, 40, 20, 20), L"Marquee");
    _selectionExpandAmount = 5;
    OnSelectionAction(SelectionAction::Expand, 0.0);
    _selectionExpandAmount = 50;
    const bool eventSettled = settle();
    expect("callback.event-time-expand-amount",
           eventSettled && HasBounds(_session.Selection(), 35, 35, 65, 65) &&
               _session.history.UndoName() == L"Expand Selection",
           "callback used the changed value instead of the Expand amount present at event time");
    prepare(100, 100);
    _session.SetSelection(OffsetHostRect(40, 40, 20, 20), L"Marquee");
    _selectionContractAmount = 5;
    OnSelectionAction(SelectionAction::Contract, 0.0);
    _selectionContractAmount = 50;
    const bool contractEventSettled = settle();
    expect("callback.event-time-contract-amount",
           contractEventSettled && HasBounds(_session.Selection(), 45, 45, 55, 55) &&
               _session.history.UndoName() == L"Contract Selection",
           "callback used the changed value instead of the Contract amount present at event time");

    return failures;
}

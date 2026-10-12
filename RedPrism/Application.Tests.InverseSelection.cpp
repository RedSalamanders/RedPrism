// Draft fragment for Application::RunInverseSelectionHostSelfTest().
// Integration is pending: Main must wire ChromeHost::kCommandInverse to StartInverseSelection
// through the ordered HostCommandInput/OnCommand path and register this host self-test.
#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"

#include "ChromeHost.h"
#include "Selection.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace
{
DocumentSelection InverseHostRect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionGeometry::Rect(x, y, width, height)};
}
} // namespace

int Application::RunInverseSelectionHostSelfTest() noexcept
{
    const auto originalViewport = _viewport;
    const auto restoreViewport = wil::scope_exit([this, originalViewport] { _viewport = originalViewport; });
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restoreHistoryBudget =
        wil::scope_exit([this, originalHistoryBudget]() noexcept
                        { _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget}; });
    int failures = 0;
    const auto expect = [&failures](const char* assertion, bool condition, const char* detail) noexcept
    {
        std::fprintf(stderr, "%s inverse-selection-host.%s%s%s\n", condition ? "PASS" : "FAIL", assertion,
                     condition ? "" : ": ", condition ? "" : detail);
        failures += condition ? 0 : 1;
        return condition;
    };
    const auto prepare = [this](int width, int height)
    {
        _session.ClearDocument();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = width;
        document.height = height;
        ImageLayer layer =
            ImageLayer::Blank(L"Inverse host fixture", Vec2{static_cast<double>(width), static_cast<double>(height)});
        const Guid activeLayer = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document).Seal(), activeLayer);
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
    };
    const auto dispatch = [this](int command)
    {
        // This is the production ordered host command path used by the existing selection and wand host tests.
        return QueueHostInput(HostCommandInput{command});
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };

    // An absent selection is the specified legacy no-op, with a future history entry to detect redo loss.
    prepare(100, 100);
    _session.SetSelection(InverseHostRect(0, 0, 50, 50), L"Redo seed");
    _session.Undo();
    const auto absentDocument = _session.Document();
    const Guid absentRevision = _session.history.CurrentRevision();
    const auto absentGeneration = _session.Generation();
    const int absentUndoCount = _session.history.UndoCount();
    const auto absentFutureCount = _session.history.CaptureState().FutureCount();
    const std::wstring absentUndoName = _session.history.UndoName();
    const std::wstring absentRedoName = _session.history.RedoName();
    const bool beforeAbsentSettled = settle();
    const auto absentMemoryBaseline = _jobs.ReservedMemoryBytes();
    const auto absentReceipt = dispatch(ChromeHost::kCommandInverse);
    const bool absentSettled = settle();
    (void)expect("absent.no-op-receipt", absentSettled && absentReceipt.Read().outcome == HostOutcome::Succeeded,
                 "inverse on an absent selection did not acknowledge successfully");
    (void)expect("absent.no-op-preserves-redo-and-history",
                 beforeAbsentSettled && absentSettled && !_session.Selection() &&
                     _session.Document() == absentDocument && _session.history.CurrentRevision() == absentRevision &&
                     _session.Generation() == absentGeneration && _session.history.UndoCount() == absentUndoCount &&
                     _session.history.CaptureState().FutureCount() == absentFutureCount &&
                     _session.history.UndoName() == absentUndoName && _session.history.RedoName() == absentRedoName &&
                     absentFutureCount == 1 && !_inverseSelectionTicket && !_inverseSelectionStamp &&
                     !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == absentMemoryBaseline,
                 "no-op changed the document/history/redo state or admitted inverse work");

    // Explicit empty has meaning distinct from an absent selection: its inverse covers the whole canvas.
    prepare(4, 3);
    const DocumentSelection explicitEmpty{true, {}};
    _session.SetSelection(explicitEmpty, L"Explicit empty");
    const auto emptyReceipt = dispatch(ChromeHost::kCommandInverse);
    const bool emptySettled = settle();
    const auto fullSelection = _session.Selection();
    const Raster fullCoverage =
        fullSelection ? SelectionGeometry::Coverage(fullSelection->loops.Get(), 4, 3, fullSelection->antialiased)
                      : Raster{};
    bool allCanvasPixelsCovered = fullCoverage.Data() != nullptr;
    for (int pixel = 0; pixel < 12 && allCanvasPixelsCovered; ++pixel)
        allCanvasPixelsCovered = fullCoverage.Data()[pixel] == 255;
    (void)expect("empty.explicit-empty-to-full",
                 emptySettled && emptyReceipt.Read().outcome == HostOutcome::Succeeded && fullSelection &&
                     fullSelection->antialiased && fullSelection->loops.size() == 1 &&
                     fullSelection->loops.PointCount() == 4 && allCanvasPixelsCovered &&
                     _session.history.UndoName() == L"Inverse",
                 "explicit empty did not become the literal full-canvas selection with the Inverse title");

    // A full-canvas selection complements to an explicit empty outline, never nullopt.
    prepare(4, 3);
    _session.SetSelection(InverseHostRect(0, 0, 4, 3), L"Full canvas");
    const auto fullReceipt = dispatch(ChromeHost::kCommandInverse);
    const bool fullSettled = settle();
    const auto explicitEmptyResult = _session.Selection();
    (void)expect("full.full-to-explicit-empty",
                 fullSettled && fullReceipt.Read().outcome == HostOutcome::Succeeded && explicitEmptyResult &&
                     !explicitEmptyResult->antialiased && explicitEmptyResult->loops.empty() &&
                     _session.history.UndoName() == L"Inverse",
                 "full-canvas inverse did not publish explicit empty geometry with the Inverse title");

    // Preserve the existing 100x100 / 50x50 legacy scenario and its literal coverage samples.
    prepare(100, 100);
    _session.SetSelection(InverseHostRect(0, 0, 100, 100), L"Select All");
    const DocumentSelection legacyPartial = InverseHostRect(0, 0, 50, 50);
    _session.SetSelection(legacyPartial, L"Lasso");
    const auto legacyReceipt = dispatch(ChromeHost::kCommandInverse);
    const bool legacySettled = settle();
    const auto inverseSelection = _session.Selection();
    const Raster inverseCoverage = inverseSelection ? SelectionGeometry::Coverage(inverseSelection->loops.Get(), 100,
                                                                                  100, inverseSelection->antialiased)
                                                    : Raster{};
    const auto* coverage = inverseCoverage.Data();
    (void)expect("legacy.inverse-receipt-and-title",
                 legacySettled && legacyReceipt.Read().outcome == HostOutcome::Succeeded && inverseSelection &&
                     _session.history.UndoName() == L"Inverse",
                 "ordered inverse command did not complete or record the literal history title");
    (void)expect("legacy.literal-coverage", coverage && coverage[25 * 100 + 25] == 0 && coverage[75 * 100 + 75] == 255,
                 "legacy 100x100 inverse coverage samples differ from the unchanged numeric expectation");

    const auto undoReceipt = dispatch(ChromeHost::kCommandUndo);
    const bool undoSettled = settle();
    (void)expect("legacy.undo-restores-partial-selection",
                 undoSettled && undoReceipt.Read().outcome == HostOutcome::Succeeded &&
                     _session.Selection() == std::optional<DocumentSelection>{legacyPartial},
                 "actual ordered Undo did not restore the original 50x50 selection");
    (void)expect("legacy.undo-title", undoSettled && _session.history.RedoName() == L"Inverse",
                 "Undo did not expose Inverse as the redo title");

    const auto redoReceipt = dispatch(ChromeHost::kCommandRedo);
    const bool redoSettled = settle();
    const auto redoneSelection = _session.Selection();
    const Raster redoneCoverage = redoneSelection ? SelectionGeometry::Coverage(redoneSelection->loops.Get(), 100, 100,
                                                                                redoneSelection->antialiased)
                                                  : Raster{};
    const auto* redonePixels = redoneCoverage.Data();
    (void)expect("legacy.redo-restores-inverse",
                 redoSettled && redoReceipt.Read().outcome == HostOutcome::Succeeded && redoneSelection &&
                     redonePixels && redonePixels[25 * 100 + 25] == 0 && redonePixels[75 * 100 + 75] == 255 &&
                     _session.history.UndoName() == L"Inverse",
                 "actual ordered Redo did not restore the same literal inverse coverage and title");

    return failures == 0 ? 0 : 5;
}

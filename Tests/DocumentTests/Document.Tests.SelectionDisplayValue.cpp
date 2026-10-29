#include "Document.Tests.SelectionDisplayValue.h"

#include "Document.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s selectiondisplayvalue.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSelection Square()
{
    return DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}};
}

bool HasSquareAt(const DocumentSelection& selection, double x, double y) noexcept
{
    if (selection.antialiased || selection.loops.Get().size() != 1 || selection.loops.Get().front().size() != 4)
        return false;
    const SelectionLoop& points = selection.loops.Get().front();
    return points[0] == Vec2{x, y} && points[1] == Vec2{x + 1, y} && points[2] == Vec2{x + 1, y + 1} &&
           points[3] == Vec2{x, y + 1};
}

bool HasSquare(const DocumentSelection& selection) noexcept
{
    return HasSquareAt(selection, 1, 0);
}

bool Initialize(DocumentSession& session, bool withSelection = true)
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    const Raster image = Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    if (!image.HasStorage())
        return false;
    session.CreateDocument(4, 1, true);
    if (!session.Document() || session.Document().value().layers.empty())
        return false;
    const Raster thumbnail = Raster::MakeThumbnail(image);
    if (!thumbnail.HasStorage())
        return false;
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{image, thumbnail, layer.name};
                                if (withSelection)
                                    document.selection = Square();
                            });
    return session.ActiveLayer() && session.ActiveLayer()->asset &&
           session.Document().value().selection.has_value() == withSelection;
}

bool HasSelection(const SelectionDisplayValue& value, const void* identity, Vec2 translation) noexcept
{
    return value.selection.has_value() && value.selection.value().loops.Identity() == identity &&
           HasSquare(value.selection.value()) && value.translation == translation;
}

void NoDocumentAndNoSelectionStayAbsent()
{
    DocumentSession empty;
    const SelectionDisplayValue noDocument = empty.DisplayedSelectionValue();
    Expect(!noDocument.selection.has_value(), "no-document-is-nullopt");
    Expect(noDocument.translation == Vec2{0, 0}, "no-document-translation-is-zero");

    DocumentSession noSelection;
    Expect(Initialize(noSelection, false), "no-selection.fixture");
    if (!noSelection.Document())
        return;
    const SelectionDisplayValue absent = noSelection.DisplayedSelectionValue();
    Expect(!absent.selection.has_value(), "no-selection-remains-nullopt");
    Expect(absent.translation == Vec2{0, 0}, "no-selection-translation-is-zero");
}

void ExplicitEmptySelectionStaysPresent()
{
    DocumentSession session;
    Expect(Initialize(session, false), "empty.fixture");
    if (!session.Document())
        return;
    session.EditForTest([](CanvasDocument& document) { document.selection = DocumentSelection{}; });

    const SelectionDisplayValue displayed = session.DisplayedSelectionValue();
    Expect(displayed.selection.has_value(), "explicit-empty-remains-present");
    Expect(displayed.selection.has_value() && displayed.selection.value().IsEmpty(), "explicit-empty-has-no-area");
    Expect(displayed.translation == Vec2{0, 0}, "explicit-empty-translation-is-zero");
}

void DirectSelectionTransformKeepsItsNumericReference()
{
    DocumentSession session;
    Expect(Initialize(session), "transform.fixture");
    if (!session.Document())
        return;
    session.BeginSelectionTransform();
    Expect(session.PendingTransformEdit().has_value(), "direct-transform-opens-floating-reference");
    if (!session.PendingTransformEdit())
        return;
    LayerTransform draft = session.PendingTransformEdit()->draft;
    draft.origin.x += 1;
    session.PreviewTransform(draft);
    const SelectionDisplayValue displayed = session.DisplayedSelectionValue();
    Expect(displayed.selection.has_value() && HasSquareAt(displayed.selection.value(), 2, 0),
           "direct-transform-keeps-displayed-outline-geometry");
    Expect(displayed.translation == Vec2{0, 0}, "direct-transform-uses-zero-separate-translation");
}

void DeferredTransformTakesDisplayPrecedenceOverPixelMove()
{
    DocumentSession session;
    Expect(Initialize(session), "transform-precedence.fixture");
    if (!session.Document() || !session.Document().value().selection.has_value())
        return;

    const void* const sourceIdentity = session.Document().value().selection->loops.Identity();
    Expect(session.BeginPixelMove(), "transform-precedence.pixel-move-begins");
    if (!session.PendingPixelMove())
        return;
    session.MovePixels(2, 0);
    Expect(session.BeginDeferredTransform(), "transform-precedence.deferred-transform-begins");
    if (!session.PendingTransformEdit() || !session.PendingTransformEdit()->deferred)
        return;

    const SelectionDisplayValue displayed = session.DisplayedSelectionValue();
    Expect(HasSelection(displayed, sourceIdentity, Vec2{0, 0}),
           "deferred-transform-display-suppresses-pixel-move-translation");
}

void DeferredMoveSharesOriginAndReportsTranslation()
{
    DocumentSession session;
    Expect(Initialize(session), "move.fixture");
    if (!session.Document() || !session.Document().value().selection.has_value())
        return;

    const void* const sourceIdentity = session.Document().value().selection->loops.Identity();
    const std::uint64_t generation = session.Generation();
    const int historyCount = session.history.UndoCount();
    Expect(session.BeginDeferredPixelMove(), "deferred-move-begins");
    if (!session.PendingPixelMove())
        return;

    const SelectionDisplayValue zero = session.DisplayedSelectionValue();
    Expect(HasSelection(zero, sourceIdentity, Vec2{0, 0}), "zero-offset-keeps-source-geometry-and-identity");

    session.MovePixels(1.4, 0.4);
    const SelectionDisplayValue positive = session.DisplayedSelectionValue();
    Expect(HasSelection(positive, sourceIdentity, Vec2{1, 0}), "positive-offset-is-separate-from-geometry");
    Expect(session.PendingPixelMove()->origin.loops.Identity() == sourceIdentity,
           "positive-offset-retains-pixel-move-origin-storage");

    session.MovePixels(-1.6, -2.5);
    const SelectionDisplayValue negative = session.DisplayedSelectionValue();
    Expect(HasSelection(negative, sourceIdentity, Vec2{-2, -3}), "negative-offset-rounds-away-from-zero");
    Expect(session.PendingPixelMove()->origin.loops.Identity() == sourceIdentity,
           "negative-offset-retains-pixel-move-origin-storage");

    session.MovePixels(std::numeric_limits<double>::quiet_NaN(), 20.0);
    session.MovePixels(30.0, std::numeric_limits<double>::infinity());
    const SelectionDisplayValue afterInvalid = session.DisplayedSelectionValue();
    Expect(HasSelection(afterInvalid, sourceIdentity, Vec2{-2, -3}), "nonfinite-moves-keep-last-display-value");

    const std::optional<DocumentSelection> legacy = session.DisplayedSelection();
    Expect(legacy.has_value() && HasSquareAt(legacy.value(), -1, -3) &&
               legacy.value().loops.Identity() != sourceIdentity,
           "legacy-numeric-api-still-returns-materialized-translated-outline");
    Expect(session.Document().value().selection->loops.Identity() == sourceIdentity &&
               HasSquare(session.Document().value().selection.value()),
           "document-selection-remains-the-original-square");
    Expect(session.Generation() == generation && session.history.UndoCount() == historyCount,
           "view-offset-changes-do-not-mutate-generation-or-history");

    const SelectionDisplayValue retained = session.DisplayedSelectionValue();
    session.CancelPixelMove();
    session.CreateDocument(1, 1, true);
    Expect(!session.PendingPixelMove() && HasSelection(retained, sourceIdentity, Vec2{-2, -3}),
           "captured-view-retains-origin-after-cancel-and-session-replacement");
}

void DeferredMoveRetainsItsSourceAfterDocumentSelectionReplacement()
{
    DocumentSession session;
    Expect(Initialize(session) && session.BeginDeferredPixelMove(), "source-replacement.fixture-and-opening");
    if (!session.PendingPixelMove() || !session.Document() || !session.Document().value().selection.has_value())
        return;

    const void* const sourceIdentity = session.PendingPixelMove()->origin.loops.Identity();
    session.MovePixels(1, 0);
    session.EditForTest(
        [](CanvasDocument& document)
        {
            document.selection =
                DocumentSelection{false, SelectionLoops{{Vec2{2, 0}, Vec2{3, 0}, Vec2{3, 1}, Vec2{2, 1}}}};
        });

    const SelectionDisplayValue displayed = session.DisplayedSelectionValue();
    const bool documentSourceReplaced = session.Document().value().selection.has_value() &&
                                        session.Document().value().selection->loops.Identity() != sourceIdentity &&
                                        HasSquareAt(session.Document().value().selection.value(), 2, 0);
    Expect(documentSourceReplaced, "source-replacement.document-selection-is-new-square");
    Expect(HasSelection(displayed, sourceIdentity, Vec2{1, 0}),
           "source-replacement-pending-move-keeps-original-source-and-offset");
}

void DeferredMoveRoundsHalfStepsAwayFromZero()
{
    DocumentSession session;
    Expect(Initialize(session) && session.BeginDeferredPixelMove(), "half-step.fixture-and-opening");
    if (!session.PendingPixelMove())
        return;

    const void* const sourceIdentity = session.PendingPixelMove()->origin.loops.Identity();
    session.MovePixels(0.5, -0.5);
    const SelectionDisplayValue positiveHalf = session.DisplayedSelectionValue();
    Expect(HasSelection(positiveHalf, sourceIdentity, Vec2{1, -1}), "half-step-positive-rounds-away-from-zero");

    session.MovePixels(-0.5, 0.5);
    const SelectionDisplayValue negativeHalf = session.DisplayedSelectionValue();
    Expect(HasSelection(negativeHalf, sourceIdentity, Vec2{-1, 1}), "half-step-negative-rounds-away-from-zero");
}

void CapturedValueKeepsStorageBeyondAllOtherOwners()
{
    SelectionDisplayValue retained;
    const void* identity = nullptr;
    {
        DocumentSession session;
        Expect(Initialize(session) && session.BeginDeferredPixelMove(), "last-owner.fixture-and-opening");
        if (!session.PendingPixelMove())
            return;
        session.MovePixels(7, -4);
        retained = session.DisplayedSelectionValue();
        identity = session.Document().value().selection->loops.Identity();
        session.CancelPixelMove();
        session.CreateDocument(1, 1, true);
    }
    // Session, document, opening and all temporary display values have been destroyed.
    Expect(HasSelection(retained, identity, Vec2{7, -4}), "last-owner.captured-view-alone-retains-literal-source");
}
} // namespace

int RunSelectionDisplayValueTests()
{
    gFailures = 0;
    NoDocumentAndNoSelectionStayAbsent();
    ExplicitEmptySelectionStaysPresent();
    DirectSelectionTransformKeepsItsNumericReference();
    DeferredTransformTakesDisplayPrecedenceOverPixelMove();
    DeferredMoveSharesOriginAndReportsTranslation();
    DeferredMoveRetainsItsSourceAfterDocumentSelectionReplacement();
    DeferredMoveRoundsHalfStepsAwayFromZero();
    CapturedValueKeepsStorageBeyondAllOtherOwners();
    return gFailures;
}

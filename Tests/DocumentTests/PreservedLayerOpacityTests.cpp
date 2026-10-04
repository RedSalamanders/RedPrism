#include "PreservedLayerOpacityTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "LayerMetadataWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s preserved-opacity.%s\n", condition ? "PASS" : "FAIL", name);
    ++gChecks;
    gFailures += condition ? 0 : 1;
}

constexpr Guid kDocumentId{{0x76, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kTargetId{{0x76, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kSiblingId{{0x76, 0, 0, 3, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 3}};

CanvasDocument MakeDocument()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 4> maskPixels{25, 75, 125, 200};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster maskRaster = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    ImageLayer target = ImageLayer::Blank(L"Opacity target", Vec2{2, 2});
    target.id = kTargetId;
    target.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), L"Opacity pixels"};
    target.mask = LayerMask{RasterAsset{maskRaster, Raster::MakeThumbnail(maskRaster), L"Opacity mask"}};
    target.opacity = 1.0;
    ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
    sibling.id = kSiblingId;
    sibling.visible = false;
    DocumentFixture fixture;
    fixture.id = kDocumentId;
    fixture.width = 2;
    fixture.height = 2;
    fixture.layers = {target, sibling};
    fixture.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}}}};
    return std::move(fixture).Seal();
}

DocumentSession MakeSession()
{
    DocumentSession session;
    session.ReplaceDocument(MakeDocument(), kTargetId);
    return session;
}

enum class Gesture : std::uint8_t
{
    Brush,
    Warp,
    Shape,
    Lasso,
};

bool BeginGesture(DocumentSession& session, Gesture gesture)
{
    switch (gesture)
    {
    case Gesture::Brush:
        return session.BeginBrush(Vec2{0.5, 0.5});
    case Gesture::Warp:
        return session.BeginWarp(Vec2{0.5, 0.5});
    case Gesture::Shape:
        session.BeginShape(Vec2{0, 0});
        session.DragShape(Vec2{1, 1}, false, false);
        return session.PendingShape().has_value();
    case Gesture::Lasso:
        session.BeginLasso(Vec2{0, 0}, SelectionMode::New);
        session.ExtendLasso(Vec2{1, 0});
        return session.PendingLasso().has_value();
    }
    return false;
}

void MoveGesture(DocumentSession& session, Gesture gesture)
{
    switch (gesture)
    {
    case Gesture::Brush:
        session.ContinueBrush(Vec2{1.25, 1.25});
        break;
    case Gesture::Warp:
        session.ContinueWarp(Vec2{1.25, 1.25});
        break;
    case Gesture::Shape:
        session.DragShape(Vec2{1.5, 1.5}, false, false);
        break;
    case Gesture::Lasso:
        session.ExtendLasso(Vec2{1, 1});
        break;
    }
}

void FinishGesture(DocumentSession& session, Gesture gesture)
{
    switch (gesture)
    {
    case Gesture::Brush:
        session.FinishBrush();
        break;
    case Gesture::Warp:
        session.FinishWarp();
        break;
    case Gesture::Shape:
        session.FinishShape();
        break;
    case Gesture::Lasso:
        session.FinishLasso();
        break;
    }
}

void CancelGesture(DocumentSession& session, Gesture gesture) noexcept
{
    switch (gesture)
    {
    case Gesture::Brush:
        session.CancelBrush();
        break;
    case Gesture::Warp:
        session.CancelWarp();
        break;
    case Gesture::Shape:
        session.CancelShape();
        break;
    case Gesture::Lasso:
        session.CancelLasso();
        break;
    }
}

bool GestureActive(const DocumentSession& session, Gesture gesture) noexcept
{
    switch (gesture)
    {
    case Gesture::Brush:
        return session.PendingBrush() != nullptr;
    case Gesture::Warp:
        return session.PendingWarp() != nullptr;
    case Gesture::Shape:
        return session.PendingShape().has_value();
    case Gesture::Lasso:
        return session.PendingLasso().has_value();
    }
    return false;
}

std::optional<PreparedLayerMetadata> PreparePreview(DocumentSession& session, double value)
{
    auto input = session.PrepareLayerOpacityPreviewInput(kTargetId, value);
    if (!input)
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()));
}

std::optional<PreparedLayerMetadata> PrepareTyped(DocumentSession& session, double opening, double value)
{
    auto input = session.PrepareTypedLayerOpacityInput(kTargetId, opening, value);
    if (!input)
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()));
}

void PreservedGesturePreviewCycle(Gesture gesture, const char* typedName, const char* previewName,
                                  const char* shownName, const char* restoreName, const char* authorityName)
{
    DocumentSession session = MakeSession();
    const bool began = BeginGesture(session, gesture);
    auto typed = began ? PrepareTyped(session, 1.0, 0.375) : std::nullopt;
    if (began)
        MoveGesture(session, gesture);
    Expect(began && typed && session.IsPreparedLayerMetadataCurrent(typed.value()) && GestureActive(session, gesture),
           typedName);

    auto preview = PreparePreview(session, 0.25);
    Expect(preview && session.Document()->layers[0].opacity == 1.0 && session.history.UndoCount() == 0, previewName);
    const bool shown = preview && session.ApplyPreparedLayerMetadataIfCurrent(preview.value());
    Expect(shown && session.Document()->layers[0].opacity == 0.25 && session.history.UndoCount() == 0 &&
               GestureActive(session, gesture),
           shownName);

    auto restore = PreparePreview(session, 1.0);
    const bool restored = restore && session.ApplyPreparedLayerMetadataIfCurrent(restore.value());
    Expect(restored && session.Document()->layers[0].opacity == 1.0 && session.history.UndoCount() == 0 &&
               GestureActive(session, gesture),
           restoreName);
    auto refreshedTyped = PrepareTyped(session, 1.0, 0.375);
    const bool retainedOpeningAuthority = refreshedTyped &&
                                          session.IsPreparedLayerMetadataCurrent(refreshedTyped.value()) &&
                                          GestureActive(session, gesture);
    CancelGesture(session, gesture);
    Expect(retainedOpeningAuthority && !GestureActive(session, gesture) &&
               session.Document()->layers[0].opacity == 1.0 && session.history.UndoCount() == 0,
           authorityName);
}

void TypedCommitUndoRedo()
{
    DocumentSession session = MakeSession();
    const CanvasDocument opening = session.Document().value();
    auto finalFixture = DocumentFixture::Copy(opening);
    finalFixture.layers[0].opacity = 0.375;
    const CanvasDocument finalDocument = std::move(finalFixture).Seal();
    auto typed = PrepareTyped(session, 1.0, 0.375);
    Expect(typed && session.Document().value() == opening && session.history.UndoCount() == 0,
           "typed-prepare-keeps-literal-opacity-opening");
    const bool committed = typed && session.ApplyPreparedLayerMetadataIfCurrent(typed.value());
    Expect(committed && session.Document().value() == finalDocument && session.history.UndoCount() == 1 &&
               session.history.UndoName() == L"Layer Opacity",
           "typed-commit-records-one-layer-opacity-entry");
    session.Undo();
    Expect(session.Document().value() == opening && session.history.RedoName() == L"Layer Opacity",
           "typed-undo-restores-literal-opacity-one");
    session.Redo();
    Expect(session.Document().value() == finalDocument && session.history.UndoName() == L"Layer Opacity",
           "typed-redo-restores-literal-opacity-zero-point-three-seven-five");
}

void FinishReopenRejectsPreparedTyped(Gesture gesture, const char* preparedName, const char* finishedName,
                                      const char* rejectedName)
{
    DocumentSession session = MakeSession();
    const bool began = BeginGesture(session, gesture);
    auto typed = began ? PrepareTyped(session, 1.0, 0.375) : std::nullopt;
    Expect(began && typed.has_value(), preparedName);
    if (!began || !typed)
    {
        CancelGesture(session, gesture);
        return;
    }
    FinishGesture(session, gesture);
    const bool ended = !GestureActive(session, gesture);
    const bool reopened = BeginGesture(session, gesture);
    Expect(ended && reopened && GestureActive(session, gesture), finishedName);
    Expect(reopened && !session.IsPreparedLayerMetadataCurrent(typed.value()) &&
               !session.ApplyPreparedLayerMetadataIfCurrent(typed.value()) &&
               session.Document()->layers[0].opacity == 1.0,
           rejectedName);
    CancelGesture(session, gesture);
}

void NestedTransaction(int depth, const wchar_t* outerTitle, const wchar_t* innerTitle, const char* previewRestoreName,
                       const char* closureName, const char* undoName, const char* redoName)
{
    DocumentSession session = MakeSession();
    const CanvasDocument opening = session.Document().value();
    auto finalFixture = DocumentFixture::Copy(opening);
    finalFixture.layers[0].opacity = 0.375;
    const CanvasDocument finalDocument = std::move(finalFixture).Seal();
    session.BeginEdit(outerTitle);
    if (depth == 2)
        session.BeginEdit(innerTitle);

    auto preview = PreparePreview(session, 0.25);
    const bool shown = preview && session.ApplyPreparedLayerMetadataIfCurrent(preview.value());
    auto restore = PreparePreview(session, 1.0);
    const bool restored = restore && session.ApplyPreparedLayerMetadataIfCurrent(restore.value());
    Expect(shown && restored && session.Document().value() == opening &&
               session.history.CaptureState().Depth() == depth && session.history.UndoCount() == 0,
           previewRestoreName);

    auto typed = PrepareTyped(session, 1.0, 0.375);
    const bool applied = typed && session.ApplyPreparedLayerMetadataIfCurrent(typed.value());
    const bool typedFinalStayedNested = applied && session.Document().value() == finalDocument &&
                                        session.history.CaptureState().Depth() == depth &&
                                        session.history.UndoCount() == 0;
    if (depth == 2)
        session.EndEdit();
    session.EndEdit();
    Expect(typedFinalStayedNested && session.history.CaptureState().Depth() == 0 && session.history.UndoCount() == 1 &&
               session.history.UndoName() == outerTitle && session.Document().value() == finalDocument,
           closureName);
    session.Undo();
    Expect(session.Document().value() == opening && session.history.RedoName() == outerTitle, undoName);
    session.Redo();
    Expect(session.Document().value() == finalDocument && session.history.UndoName() == outerTitle, redoName);
}

void ReturnToOpeningPreservesRedo()
{
    DocumentSession session = MakeSession();
    auto seed = PrepareTyped(session, 1.0, 0.5);
    const bool seeded = seed && session.ApplyPreparedLayerMetadataIfCurrent(seed.value());
    session.Undo();
    Expect(seeded && session.Document()->layers[0].opacity == 1.0 && session.history.RedoName() == L"Layer Opacity",
           "redo-fixture-restores-literal-opacity-one");
    auto preview = PreparePreview(session, 0.25);
    const bool shown = preview && session.ApplyPreparedLayerMetadataIfCurrent(preview.value());
    auto restore = PrepareTyped(session, 1.0, 1.0);
    const bool restored = restore && session.ApplyPreparedLayerMetadataIfCurrent(restore.value());
    Expect(shown && restored && session.Document()->layers[0].opacity == 1.0 && session.history.UndoCount() == 0 &&
               session.history.CaptureState().FutureCount() == 1 && session.history.RedoName() == L"Layer Opacity",
           "return-to-start-restores-opacity-and-preserves-redo-entry");
    session.Redo();
    Expect(session.Document()->layers[0].opacity == 0.5 && session.history.UndoName() == L"Layer Opacity",
           "preserved-redo-replays-literal-seed-opacity");
}
} // namespace

int RunPreservedLayerOpacityTests()
{
    gFailures = 0;
    gChecks = 0;
    PreservedGesturePreviewCycle(Gesture::Brush, "cpu-brush-captures-typed-opening",
                                 "cpu-brush-preview-prepares-from-open-gesture",
                                 "cpu-brush-preview-stays-outside-history", "cpu-brush-restore-keeps-opening-live",
                                 "cpu-brush-restore-retains-original-gesture-authority");
    PreservedGesturePreviewCycle(Gesture::Warp, "warp-captures-typed-opening",
                                 "warp-preview-prepares-from-open-gesture", "warp-preview-stays-outside-history",
                                 "warp-restore-keeps-opening-live", "warp-restore-retains-original-gesture-authority");
    PreservedGesturePreviewCycle(Gesture::Shape, "shape-captures-typed-opening",
                                 "shape-preview-prepares-from-open-gesture", "shape-preview-stays-outside-history",
                                 "shape-restore-keeps-opening-live",
                                 "shape-restore-retains-original-gesture-authority");
    PreservedGesturePreviewCycle(Gesture::Lasso, "lasso-captures-typed-opening",
                                 "lasso-preview-prepares-from-open-gesture", "lasso-preview-stays-outside-history",
                                 "lasso-restore-keeps-opening-live",
                                 "lasso-restore-retains-original-gesture-authority");

    TypedCommitUndoRedo();
    FinishReopenRejectsPreparedTyped(Gesture::Brush, "brush-typed-result-captured",
                                     "brush-end-and-reopen-create-new-opening",
                                     "brush-reopen-rejects-old-typed-result-before-apply");
    FinishReopenRejectsPreparedTyped(Gesture::Warp, "warp-typed-result-captured",
                                     "warp-end-and-reopen-create-new-opening",
                                     "warp-reopen-rejects-old-typed-result-before-apply");
    FinishReopenRejectsPreparedTyped(Gesture::Shape, "shape-typed-result-captured",
                                     "shape-end-and-reopen-create-new-opening",
                                     "shape-reopen-rejects-old-typed-result-before-apply");
    FinishReopenRejectsPreparedTyped(Gesture::Lasso, "lasso-typed-result-captured",
                                     "lasso-end-and-reopen-create-new-opening",
                                     "lasso-reopen-rejects-old-typed-result-before-apply");

    NestedTransaction(1, L"Outer opacity transaction", L"Unused inner opacity transaction",
                      "depth-one-preview-and-restoration-create-no-entry",
                      "depth-one-closure-records-original-outer-title", "depth-one-undo-restores-literal-opacity-one",
                      "depth-one-redo-restores-literal-opacity-final");
    NestedTransaction(2, L"Outer opacity transaction", L"Inner opacity transaction",
                      "depth-two-preview-and-restoration-create-no-entry",
                      "depth-two-closure-records-original-outer-title", "depth-two-undo-restores-literal-opacity-one",
                      "depth-two-redo-restores-literal-opacity-final");
    ReturnToOpeningPreservesRedo();
    std::printf("ASSERTIONS preserved-opacity.count=%d\n", gChecks);
    return gFailures;
}

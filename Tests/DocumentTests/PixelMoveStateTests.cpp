#include "PixelMoveStateTests.h"

#include "../Support/DocumentFixture.h"
#include "BrushStroke.h"
#include "Document.h"
#include "RasterEditWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;

void Expect(bool condition, const char* label, int& failures)
{
    ++gAssertions;
    std::printf("%s pixelmovestate.%s\n", condition ? "PASS" : "FAIL", label);
    failures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourcePixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                     70, 80, 90, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 16> kMovedPixels{10, 20, 30, 255, 0, 0, 0, 0, 40, 50, 60, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 16> kDuplicatedPixels{10, 20, 30, 255, 40, 50, 60, 255,
                                                         70, 80, 90, 255, 40, 50, 60, 255};

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

bool PixelsAre(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != 4 ||
        raster.Height() != 1 || expected.size() != kSourcePixels.size())
        return false;
    std::array<std::uint8_t, 16> actual{};
    raster.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    return std::equal(actual.begin(), actual.end(), expected.begin());
}

bool Initialize(DocumentSession& session, bool withSelection = true)
{
    const Raster pixels = Raster::Make(4, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
    if (!pixels.HasStorage())
        return false;
    session.CreateDocument(4, 1, true);
    if (!session.Document() || session.Document()->layers.empty())
        return false;
    const Raster thumbnail = Raster::MakeThumbnail(pixels);
    if (!thumbnail.HasStorage())
        return false;
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{pixels, thumbnail, layer.name};
                                if (withSelection)
                                    document.selection = Rect(1, 0, 1, 1);
                            });
    return session.ActiveLayer() && session.ActiveLayer()->asset &&
           session.Document()->selection.has_value() == withSelection;
}

std::optional<JobToken> Token(DocumentSession& session, std::uint64_t acquisition = 73)
{
    return session.CaptureJobToken(session.ActiveLayerID(), acquisition);
}

std::optional<RasterEditInput> PendingInput(const DocumentSession& session)
{
    const PixelMove* const move = session.PendingPixelMove();
    const ImageLayer* const layer = session.ActiveLayer();
    if (!move || !layer || !session.Document() || !session.Document()->selection)
        return std::nullopt;
    RasterEditInput input;
    input.layer = *layer;
    input.selection = session.Document()->selection;
    input.canvasWidth = session.Document()->width;
    input.canvasHeight = session.Document()->height;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = move->offset;
    input.duplicate = move->duplicate;
    return input;
}

RasterEditPreviewResult Preview(const DocumentSession& session)
{
    const auto input = PendingInput(session);
    return input ? RunRasterEditPreview(input.value()) : RasterEditPreviewResult{};
}

RasterEditResult Prepare(const DocumentSession& session)
{
    const auto input = PendingInput(session);
    return input ? RunRasterEdit(input.value()) : RasterEditResult{};
}

bool CandidateRetained(const ImageLayer& prepared, const Raster& retained, const DocumentSelection& selection,
                       const DocumentSelection& expectedSelection)
{
    return prepared.asset && prepared.asset->image == retained && selection == expectedSelection;
}

void DeferredBeginAndRoundedSequence(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "begin.fixture", failures);
    if (!session.Document() || !session.ActiveLayer() || !session.ActiveLayer()->asset)
        return;
    const CanvasDocument before = session.Document().value();
    const Raster source = session.ActiveLayer()->asset->image;
    const std::uint64_t generation = session.Generation();
    const int undoCount = session.history.UndoCount();
    Expect(session.BeginDeferredPixelMove(), "begin.deferred-accepted", failures);
    const PixelMove* move = session.PendingPixelMove();
    Expect(move && move->deferred && move->raster == nullptr && move->sequence == 1 && move->opening != 0 &&
               move->offset == Vec2{0, 0} && move->target == session.ActiveLayerID().value(),
           "begin.metadata-only-opening", failures);
    Expect(session.Document().value() == before && session.ActiveLayer()->asset->image == source &&
               session.Generation() == generation && session.history.UndoCount() == undoCount,
           "begin.no-raster-selection-generation-or-history-mutation", failures);
    if (!move)
        return;
    const std::uint64_t sequence = move->sequence;
    session.MovePixels(0.49, -0.49);
    Expect(session.PendingPixelMove()->offset == Vec2{0, 0} && session.PendingPixelMove()->sequence == sequence &&
               !session.PendingPixelMove()->raster,
           "offset.subhalf-rounds-zero-without-new-sequence", failures);
    session.MovePixels(0.5, -0.5);
    const std::uint64_t onePixelSequence = session.PendingPixelMove()->sequence;
    Expect(session.PendingPixelMove()->offset == Vec2{1, -1} && onePixelSequence == sequence + 1,
           "offset.half-rounds-away-from-zero", failures);
    session.MovePixels(1.49, -1.49);
    Expect(session.PendingPixelMove()->offset == Vec2{1, -1} &&
               session.PendingPixelMove()->sequence == onePixelSequence,
           "offset.same-rounded-offset-keeps-sequence", failures);
    session.MovePixels(-0.5, 0.5);
    Expect(session.PendingPixelMove()->offset == Vec2{-1, 1} &&
               session.PendingPixelMove()->sequence == onePixelSequence + 1,
           "offset.changed-rounded-value-advances-sequence", failures);
    session.CancelPixelMove();

    DocumentSession noSelection;
    Expect(Initialize(noSelection, false) && !noSelection.BeginDeferredPixelMove(), "begin.no-selection-is-refused",
           failures);
}

void PreviewAcceptanceAndRejections(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session) && session.BeginDeferredPixelMove(), "preview.fixture", failures);
    session.MovePixels(1, 0);
    const auto token = Token(session);
    RasterEditPreviewResult offered = Preview(session);
    const std::uint64_t opening = session.PendingPixelMove()->opening;
    const std::uint64_t sequence = session.PendingPixelMove()->sequence;
    Expect(token && offered.stroke &&
               session.PublishPixelMovePreview(std::move(offered.stroke), token.value(), opening, sequence) &&
               session.PendingPixelMove()->publishedSequence == sequence && session.PendingPixelMove()->raster,
           "preview.current-opening-sequence-publishes", failures);

    session.MovePixels(2, 0);
    const std::uint64_t currentSequence = session.PendingPixelMove()->sequence;
    const auto rejectPreview =
        [&](const char* label, std::uint64_t offeredOpening, std::uint64_t offeredSequence, JobToken offeredToken)
    {
        RasterEditPreviewResult fresh = Preview(session);
        std::shared_ptr<BrushStroke> candidate = std::move(fresh.stroke);
        const BrushStroke* const identity = candidate.get();
        const bool accepted =
            session.PublishPixelMovePreview(std::move(candidate), offeredToken, offeredOpening, offeredSequence);
        Expect(!accepted && candidate.get() == identity, label, failures);
    };
    if (token)
    {
        rejectPreview("preview.wrong-opening-retains-shared-candidate", opening + 9, currentSequence, token.value());
        rejectPreview("preview.zero-sequence-retains-shared-candidate", opening, 0, token.value());
        rejectPreview("preview.duplicate-sequence-retains-shared-candidate", opening, sequence, token.value());
        rejectPreview("preview.future-sequence-retains-shared-candidate", opening, currentSequence + 1, token.value());
        JobToken wrongTarget = token.value();
        wrongTarget.target->layerId = Guid::Generate();
        rejectPreview("preview.wrong-target-retains-shared-candidate", opening, currentSequence, wrongTarget);
        JobToken wrongGeneration = token.value();
        ++wrongGeneration.generation;
        rejectPreview("preview.wrong-generation-retains-shared-candidate", opening, currentSequence, wrongGeneration);
    }

    DocumentSession staleGeneration;
    Expect(Initialize(staleGeneration) && staleGeneration.BeginDeferredPixelMove(), "preview.generation-fixture",
           failures);
    staleGeneration.MovePixels(1, 0);
    const auto generationToken = Token(staleGeneration);
    auto generationPreview = Preview(staleGeneration);
    std::shared_ptr<BrushStroke> generationCandidate = std::move(generationPreview.stroke);
    const BrushStroke* const generationIdentity = generationCandidate.get();
    const auto generationOpening = staleGeneration.PendingPixelMove()->opening;
    const auto generationSequence = staleGeneration.PendingPixelMove()->sequence;
    staleGeneration.EditForTest([](CanvasDocument&) {});
    Expect(generationToken &&
               !staleGeneration.PublishPixelMovePreview(std::move(generationCandidate), generationToken.value(),
                                                        generationOpening, generationSequence) &&
               generationCandidate.get() == generationIdentity,
           "preview.stale-source-generation-rejected-and-retained", failures);
    const auto refreshedToken = Token(staleGeneration);
    Expect(refreshedToken && generationCandidate &&
               !staleGeneration.PublishPixelMovePreview(std::move(generationCandidate), refreshedToken.value(),
                                                        generationOpening, generationSequence) &&
               generationCandidate.get() == generationIdentity,
           "preview.new-acquisition-cannot-retarget-invalidated-opening", failures);

    DocumentSession zero;
    Expect(Initialize(zero) && zero.BeginDeferredPixelMove(), "preview.zero-offset-fixture", failures);
    const auto zeroToken = Token(zero);
    zero.MovePixels(1, 0);
    RasterEditPreviewResult zeroPreview = Preview(zero);
    std::shared_ptr<BrushStroke> zeroCandidate = std::move(zeroPreview.stroke);
    const BrushStroke* const zeroIdentity = zeroCandidate.get();
    const auto zeroOpening = zero.PendingPixelMove()->opening;
    zero.MovePixels(0, 0);
    Expect(zeroToken && zeroCandidate &&
               !zero.PublishPixelMovePreview(std::move(zeroCandidate), zeroToken.value(), zeroOpening,
                                             zero.PendingPixelMove()->sequence) &&
               zeroCandidate.get() == zeroIdentity,
           "preview.zero-offset-rejected-with-shared-candidate-owned", failures);

    DocumentSession cancelled;
    Expect(Initialize(cancelled) && cancelled.BeginDeferredPixelMove(), "preview.cancel-fixture", failures);
    cancelled.MovePixels(1, 0);
    const auto cancelledToken = Token(cancelled);
    auto cancelledPreview = Preview(cancelled);
    std::shared_ptr<BrushStroke> cancelledCandidate = std::move(cancelledPreview.stroke);
    const BrushStroke* const cancelledIdentity = cancelledCandidate.get();
    const auto cancelledOpening = cancelled.PendingPixelMove()->opening;
    const auto cancelledSequence = cancelled.PendingPixelMove()->sequence;
    cancelled.CancelPixelMove();
    Expect(cancelledToken &&
               !cancelled.PublishPixelMovePreview(std::move(cancelledCandidate), cancelledToken.value(),
                                                  cancelledOpening, cancelledSequence) &&
               cancelledCandidate.get() == cancelledIdentity,
           "preview.cancelled-opening-rejected-and-owned", failures);

    DocumentSession reopened;
    Expect(Initialize(reopened) && reopened.BeginDeferredPixelMove(), "preview.reopen-fixture", failures);
    reopened.MovePixels(1, 0);
    const auto reopenedToken = Token(reopened);
    auto oldPreview = Preview(reopened);
    std::shared_ptr<BrushStroke> oldCandidate = std::move(oldPreview.stroke);
    const BrushStroke* const oldIdentity = oldCandidate.get();
    const auto oldOpening = reopened.PendingPixelMove()->opening;
    const auto oldSequence = reopened.PendingPixelMove()->sequence;
    reopened.CancelPixelMove();
    Expect(reopened.BeginDeferredPixelMove(), "preview.reopen-new-metadata-opening", failures);
    reopened.MovePixels(1, 0);
    const auto newOpening = reopened.PendingPixelMove()->opening;
    Expect(reopenedToken && newOpening != oldOpening &&
               !reopened.PublishPixelMovePreview(std::move(oldCandidate), reopenedToken.value(), oldOpening,
                                                 oldSequence) &&
               oldCandidate.get() == oldIdentity,
           "preview.reopened-opening-rejects-old-candidate", failures);

    DocumentSession selectionChanged;
    Expect(Initialize(selectionChanged) && selectionChanged.BeginDeferredPixelMove(), "preview.selection-fixture",
           failures);
    selectionChanged.MovePixels(1, 0);
    const auto selectionToken = Token(selectionChanged);
    auto selectionPreview = Preview(selectionChanged);
    std::shared_ptr<BrushStroke> selectionCandidate = std::move(selectionPreview.stroke);
    const BrushStroke* const selectionIdentity = selectionCandidate.get();
    const auto selectionOpening = selectionChanged.PendingPixelMove()->opening;
    const auto selectionSequence = selectionChanged.PendingPixelMove()->sequence;
    selectionChanged.EditForTest([](CanvasDocument& document) { document.selection = Rect(0, 0, 1, 1); });
    Expect(selectionToken &&
               !selectionChanged.PublishPixelMovePreview(std::move(selectionCandidate), selectionToken.value(),
                                                         selectionOpening, selectionSequence) &&
               selectionCandidate.get() == selectionIdentity,
           "preview.changed-selection-stales-token-and-retains-candidate", failures);
}

void PreparedApplyAndUndo(int& failures)
{
    DocumentSession session;
    Expect(Initialize(session), "apply.fixture", failures);
    const CanvasDocument original = session.Document().value();
    const int undoCount = session.history.UndoCount();
    Expect(session.BeginDeferredPixelMove(), "apply.deferred-begin", failures);
    session.MovePixels(1, 0);
    const auto token = Token(session, 91);
    const auto opening = session.PendingPixelMove()->opening;
    const auto sequence = session.PendingPixelMove()->sequence;
    RasterEditResult result = Prepare(session);
    const DocumentSelection expectedSelection = Rect(2, 0, 1, 1);
    Expect(token && result.status == RasterEditStatus::Ready && result.layer && result.selection &&
               *result.selection == expectedSelection && PixelsAre(result.layer->asset->image, kMovedPixels),
           "apply.prepared-literal-pixels-and-independent-translated-outline", failures);
    if (!token || !result.layer || !result.selection)
        return;
    ImageLayer prepared = std::move(result.layer.value());
    DocumentSelection moved = std::move(result.selection.value());
    Expect(session.ApplyPixelMoveIfCurrent(std::move(prepared), std::move(moved), token.value(), opening, sequence) &&
               !session.PendingPixelMove() && session.history.UndoCount() == undoCount + 1 &&
               session.history.UndoName() == L"Move Pixels" && session.Document()->selection == expectedSelection &&
               PixelsAre(session.ActiveLayer()->asset->image, kMovedPixels),
           "apply.exact-current-token-sequence-commits-one-named-move", failures);
    session.Undo();
    Expect(session.Document() && session.Document().value() == original && session.history.UndoCount() == undoCount &&
               PixelsAre(session.ActiveLayer()->asset->image, kSourcePixels),
           "apply.one-undo-restores-source-and-original-selection", failures);
}

void PreparedApplyRefusalsRetainOwnership(int& failures)
{
    const auto refusal = [&](const char* label, int kind)
    {
        DocumentSession session;
        if (!Initialize(session) || !session.BeginDeferredPixelMove())
        {
            Expect(false, "apply-refusal.fixture", failures);
            return;
        }
        session.MovePixels(1, 0);
        const auto token = Token(session, 101);
        const std::uint64_t opening = session.PendingPixelMove()->opening;
        const std::uint64_t sequence = session.PendingPixelMove()->sequence;
        RasterEditResult result = Prepare(session);
        if (!token || result.status != RasterEditStatus::Ready || !result.layer || !result.selection)
        {
            Expect(false, "apply-refusal.prepared-candidate-ready", failures);
            return;
        }
        ImageLayer prepared = std::move(result.layer.value());
        DocumentSelection selection = std::move(result.selection.value());
        if (kind == 0)
            prepared.id = Guid::Generate();
        else if (kind == 1)
            selection = Rect(0, 0, 1, 1);
        else if (kind == 2)
            session.EditForTest([](CanvasDocument&) {});
        else if (kind == 3)
            session.MovePixels(2, 0); // candidate belongs to the preceding metadata sequence
        else if (kind == 4)
        {
            session.CancelPixelMove();
            (void)session.BeginDeferredPixelMove();
            session.MovePixels(1, 0);
        }
        const CanvasDocument before = session.Document().value();
        const int history = session.history.UndoCount();
        const Raster retained = prepared.asset->image;
        const ImageLayer expectedLayer = prepared;
        const DocumentSelection expectedOutline = selection;
        const bool accepted = session.ApplyPixelMoveIfCurrent(std::move(prepared), std::move(selection), token.value(),
                                                              opening, sequence);
        Expect(!accepted && prepared.id == expectedLayer.id && prepared.asset &&
                   CandidateRetained(prepared, retained, selection, expectedOutline) && session.Document() &&
                   session.Document().value() == before && session.history.UndoCount() == history,
               label, failures);
    };
    refusal("apply-refusal.wrong-prepared-identity-retains-layer-and-selection", 0);
    refusal("apply-refusal.wrong-selection-bounds-retains-layer-and-selection", 1);
    refusal("apply-refusal.stale-generation-retains-layer-and-selection", 2);
    refusal("apply-refusal.stale-offset-sequence-retains-layer-and-selection", 3);
    refusal("apply-refusal.reopened-token-retains-layer-and-selection", 4);
}

void DeferredFinishDuplicateRedoAndLegacy(int& failures)
{
    {
        DocumentSession transparent;
        Expect(Initialize(transparent), "transparent.fixture", failures);
        EditLayerRecordsForTest(transparent,
                                [](DocumentFixture& document)
                                {
                                    auto& layer = document.layers.front();
                                    const Raster zero =
                                        Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
                                    layer.asset = RasterAsset{zero, Raster::MakeThumbnail(zero), layer.name};
                                });
        Expect(transparent.BeginDeferredPixelMove(), "transparent.deferred-open", failures);
        transparent.MovePixels(1, 0);
        const auto token = Token(transparent);
        const auto opening = transparent.PendingPixelMove()->opening;
        const auto sequence = transparent.PendingPixelMove()->sequence;
        auto result = Prepare(transparent);
        constexpr std::array<std::uint8_t, 16> zeroBytes{};
        Expect(token && result.status == RasterEditStatus::Ready && result.layer && result.selection &&
                   PixelsAre(result.layer->asset->image, zeroBytes) &&
                   transparent.ApplyPixelMoveIfCurrent(std::move(result.layer.value()),
                                                       std::move(result.selection.value()), token.value(), opening,
                                                       sequence) &&
                   transparent.Document()->selection == Rect(2, 0, 1, 1) &&
                   transparent.history.UndoName() == L"Move Pixels",
               "transparent.source-grid-move-retains-outline-history-semantics", failures);
    }
    {
        DocumentSession deferred;
        Expect(Initialize(deferred) && deferred.BeginDeferredPixelMove(), "finish.deferred-fixture", failures);
        deferred.MovePixels(1, 0);
        const CanvasDocument before = deferred.Document().value();
        const int history = deferred.history.UndoCount();
        deferred.FinishPixelMove();
        Expect(deferred.PendingPixelMove() && deferred.Document().value() == before &&
                   deferred.history.UndoCount() == history &&
                   PixelsAre(deferred.ActiveLayer()->asset->image, kSourcePixels),
               "finish.deferred-finish-does-not-inline-commit", failures);
    }
    {
        DocumentSession duplicate;
        Expect(Initialize(duplicate) && duplicate.BeginDeferredPixelMove(true), "duplicate.fixture", failures);
        duplicate.MovePixels(2, 0);
        const auto token = Token(duplicate);
        const auto opening = duplicate.PendingPixelMove()->opening;
        const auto sequence = duplicate.PendingPixelMove()->sequence;
        auto result = Prepare(duplicate);
        Expect(token && result.layer && result.selection &&
                   duplicate.ApplyPixelMoveIfCurrent(std::move(result.layer.value()),
                                                     std::move(result.selection.value()), token.value(), opening,
                                                     sequence),
               "duplicate.deferred-prepared-application", failures);
        Expect(!duplicate.PendingPixelMove() && duplicate.history.UndoName() == L"Duplicate Pixels" &&
                   PixelsAre(duplicate.ActiveLayer()->asset->image, kDuplicatedPixels) &&
                   duplicate.Document()->selection == Rect(3, 0, 1, 1),
               "duplicate.retains-original-and-lands-copy-with-translated-outline", failures);
    }
    {
        DocumentSession redo;
        Expect(Initialize(redo), "redo.fixture", failures);
        redo.SetSelection(Rect(0, 0, 1, 1), L"Selection A");
        redo.SetSelection(Rect(1, 0, 1, 1), L"Selection B");
        redo.Undo();
        const int history = redo.history.UndoCount();
        Expect(redo.history.CanRedo() && redo.BeginDeferredPixelMove(), "redo.ready-before-zero-move", failures);
        redo.MovePixels(0, 0);
        redo.FinishPixelMove();
        Expect(redo.history.CanRedo() && redo.history.UndoCount() == history && redo.PendingPixelMove() &&
                   redo.Document()->selection == Rect(0, 0, 1, 1),
               "redo.zero-offset-deferred-finish-preserves-redo-and-document", failures);
    }
    {
        DocumentSession legacy;
        Expect(Initialize(legacy) && legacy.BeginPixelMove(), "legacy.direct-api-fixture", failures);
        legacy.MovePixels(1, 0);
        legacy.FinishPixelMove();
        Expect(!legacy.PendingPixelMove() && legacy.history.UndoName() == L"Move Pixels" &&
                   PixelsAre(legacy.ActiveLayer()->asset->image, kMovedPixels) &&
                   legacy.Document()->selection == Rect(2, 0, 1, 1),
               "legacy.direct-api-still-commits-inline", failures);
    }
}

void RunAll(int& failures)
{
    DeferredBeginAndRoundedSequence(failures);
    PreviewAcceptanceAndRejections(failures);
    PreparedApplyAndUndo(failures);
    PreparedApplyRefusalsRetainOwnership(failures);
    DeferredFinishDuplicateRedoAndLegacy(failures);
}
} // namespace

int RunPixelMoveStateTests()
{
    int failures = 0;
    gAssertions = 0;
    RunAll(failures);
    std::printf("pixelmovestate.summary assertions=%d failures=%d\n", gAssertions, failures);
    return failures;
}

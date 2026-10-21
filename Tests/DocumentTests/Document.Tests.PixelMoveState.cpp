#include "Document.Tests.PixelMoveState.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "BrushStroke.h"
#include "Document.h"
#include "RasterEditWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

constexpr int kLegacyCanvasWidth = 100;
constexpr int kLegacyCanvasHeight = 40;

constexpr std::array<std::uint8_t, kLegacyCanvasWidth * kLegacyCanvasHeight * 4> MakeLegacyTwoColorPixels()
{
    std::array<std::uint8_t, kLegacyCanvasWidth * kLegacyCanvasHeight * 4> pixels{};
    for (int y = 0; y < kLegacyCanvasHeight; ++y)
    {
        for (int x = 0; x < kLegacyCanvasWidth; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kLegacyCanvasWidth) +
                                        static_cast<std::size_t>(x)) *
                                       4u;
            pixels[offset] = x < 50 ? 0 : 255;     // B
            pixels[offset + 1] = 0;                // G
            pixels[offset + 2] = x < 50 ? 255 : 0; // R
            pixels[offset + 3] = 255;              // A
        }
    }
    return pixels;
}

constexpr auto kLegacyTwoColorPixels = MakeLegacyTwoColorPixels();
constexpr std::array<std::uint8_t, 4> kLegacyRedBgra{0, 0, 255, 255};
constexpr std::array<std::uint8_t, 4> kLegacyBlueBgra{255, 0, 0, 255};
constexpr std::array<std::uint8_t, 4> kLegacyHoleBgra{0, 0, 0, 0};

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

bool PixelIs(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool InitializeLegacyTwoColor(DocumentSession& session)
{
    const Raster pixels = Raster::Make(kLegacyCanvasWidth, kLegacyCanvasHeight, RasterFormat::Bgra8888,
                                       kLegacyTwoColorPixels.data(), kLegacyTwoColorPixels.size());
    if (!pixels.HasStorage())
        return false;
    session.CreateDocument(kLegacyCanvasWidth, kLegacyCanvasHeight, true);
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
                                document.selection = Rect(10, 10, 10, 10);
                            });
    return session.ActiveLayer() && session.ActiveLayer()->asset &&
           session.Document()->selection == Rect(10, 10, 10, 10);
}

bool SelectionBoundsAre(const std::optional<DocumentSelection>& selection, const SelectionBounds& expected)
{
    return selection && selection->Bounds() == expected;
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

void Legacy100x40MoveNumbersAndOneUndo(int& failures)
{
    DocumentSession session;
    Expect(InitializeLegacyTwoColor(session), "legacy100x40-move.fixture-red-left-blue-right", failures);
    if (!session.Document() || !session.Document()->selection)
        return;
    const int history = session.history.UndoCount();
    const bool began = session.BeginDeferredPixelMove();
    Expect(began, "legacy100x40-move.deferred-begin", failures);
    if (!began)
        return;
    session.MovePixels(30.4, 0);
    const PixelMove* first = session.PendingPixelMove();
    Expect(first && first->offset == Vec2{30, 0}, "legacy100x40-move.first-offset-rounds-from-original", failures);
    session.MovePixels(60.2, 5);
    const PixelMove* move = session.PendingPixelMove();
    Expect(move && move->offset == Vec2{60, 5}, "legacy100x40-move.second-offset-replaces-first", failures);
    if (!move)
        return;

    const std::uint64_t opening = move->opening;
    const std::uint64_t sequence = move->sequence;
    const auto token = Token(session, 173);
    auto capturedInput = PendingInput(session);
    Expect(token && capturedInput, "legacy100x40-move.pending-input-and-token", failures);
    if (!token || !capturedInput)
        return;
    RasterEditInput input = std::move(capturedInput.value());
    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    const auto snapshot = preview.stroke ? preview.stroke->PaintSnapshot() : std::nullopt;
    Expect(preview.status == RasterEditStatus::Ready && snapshot && PixelIs(snapshot->image, 15, 15, kLegacyHoleBgra) &&
               PixelIs(snapshot->image, 75, 20, kLegacyRedBgra) && PixelIs(snapshot->image, 85, 20, kLegacyBlueBgra) &&
               PixelIs(snapshot->image, 5, 5, kLegacyRedBgra),
           "legacy100x40-move.preview-literal-hole-landing-and-untouched-colors", failures);
    const bool published = token && preview.pixelMoveOpening &&
                           session.PublishPixelMovePreview(std::move(preview.stroke), token.value(), opening, sequence);
    Expect(published && SelectionBoundsAre(session.DisplayedSelection(), SelectionBounds{70, 15, 80, 25}),
           "legacy100x40-move.displayed-outline-at-final-preview-offset", failures);
    input.pixelMoveOpening = preview.pixelMoveOpening;
    RasterEditResult prepared = RunRasterEdit(input);
    Expect(prepared.status == RasterEditStatus::Ready && prepared.layer && prepared.layer->asset &&
               prepared.selection && PixelIs(prepared.layer->asset->image, 15, 15, kLegacyHoleBgra) &&
               PixelIs(prepared.layer->asset->image, 75, 20, kLegacyRedBgra) &&
               PixelIs(prepared.layer->asset->image, 85, 20, kLegacyBlueBgra) &&
               PixelIs(prepared.layer->asset->image, 5, 5, kLegacyRedBgra) &&
               SelectionBoundsAre(prepared.selection, SelectionBounds{70, 15, 80, 25}),
           "legacy100x40-move.cpu-commit-literal-pixels-and-outline", failures);
    if (!token || !prepared.layer || !prepared.selection)
        return;
    const bool applied = session.ApplyPixelMoveIfCurrent(
        std::move(prepared.layer.value()), std::move(prepared.selection.value()), token.value(), opening, sequence);
    Expect(applied && session.history.UndoCount() == history + 1 && session.history.UndoName() == L"Move Pixels" &&
               SelectionBoundsAre(session.Document()->selection, SelectionBounds{70, 15, 80, 25}),
           "legacy100x40-move.one-move-pixels-history-entry-and-final-selection", failures);
    if (!applied)
        return;
    session.Undo();
    const ImageLayer* restored = session.ActiveLayer();
    Expect(session.Document() && restored && restored->asset && session.history.UndoCount() == history &&
               SelectionBoundsAre(session.Document()->selection, SelectionBounds{10, 10, 20, 20}) &&
               PixelIs(restored->asset->image, 15, 15, kLegacyRedBgra) &&
               PixelIs(restored->asset->image, 75, 20, kLegacyBlueBgra),
           "legacy100x40-move.single-undo-restores-source-destination-and-outline", failures);
}

void Legacy100x40DuplicatePreservesSourceAndUndoesTogether(int& failures)
{
    DocumentSession session;
    Expect(InitializeLegacyTwoColor(session) && session.BeginDeferredPixelMove(true),
           "legacy100x40-duplicate.fixture-and-deferred-begin", failures);
    if (!session.PendingPixelMove())
        return;
    const int history = session.history.UndoCount();
    session.MovePixels(60, 5);
    const PixelMove* move = session.PendingPixelMove();
    if (!move)
        return;
    const std::uint64_t opening = move->opening;
    const std::uint64_t sequence = move->sequence;
    const auto token = Token(session, 179);
    auto capturedInput = PendingInput(session);
    Expect(token && capturedInput, "legacy100x40-duplicate.pending-input-and-token", failures);
    if (!token || !capturedInput)
        return;
    RasterEditInput input = std::move(capturedInput.value());
    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    const auto snapshot = preview.stroke ? preview.stroke->PaintSnapshot() : std::nullopt;
    Expect(preview.status == RasterEditStatus::Ready && snapshot && PixelIs(snapshot->image, 15, 15, kLegacyRedBgra) &&
               PixelIs(snapshot->image, 75, 20, kLegacyRedBgra) && PixelIs(snapshot->image, 85, 20, kLegacyBlueBgra),
           "legacy100x40-duplicate.preview-preserves-source-and-lands-red-copy", failures);
    input.pixelMoveOpening = preview.pixelMoveOpening;
    RasterEditResult prepared = RunRasterEdit(input);
    Expect(prepared.status == RasterEditStatus::Ready && prepared.layer && prepared.layer->asset &&
               PixelIs(prepared.layer->asset->image, 15, 15, kLegacyRedBgra) &&
               PixelIs(prepared.layer->asset->image, 75, 20, kLegacyRedBgra) &&
               PixelIs(prepared.layer->asset->image, 85, 20, kLegacyBlueBgra),
           "legacy100x40-duplicate.commit-preserves-source-and-destination-blue", failures);
    if (!token || !prepared.layer || !prepared.selection)
        return;
    const bool applied = session.ApplyPixelMoveIfCurrent(
        std::move(prepared.layer.value()), std::move(prepared.selection.value()), token.value(), opening, sequence);
    Expect(applied && session.history.UndoCount() == history + 1 && session.history.UndoName() == L"Duplicate Pixels",
           "legacy100x40-duplicate.one-duplicate-pixels-history-entry", failures);
    if (!applied)
        return;
    Expect(session.Document() && SelectionBoundsAre(session.Document()->selection, SelectionBounds{70, 15, 80, 25}),
           "legacy100x40-duplicate.final-selection-translates-with-pixels", failures);
    session.Undo();
    const ImageLayer* restored = session.ActiveLayer();
    Expect(session.Document() && restored && restored->asset && session.history.UndoCount() == history &&
               PixelIs(restored->asset->image, 15, 15, kLegacyRedBgra) &&
               PixelIs(restored->asset->image, 75, 20, kLegacyBlueBgra) &&
               SelectionBoundsAre(session.Document()->selection, SelectionBounds{10, 10, 20, 20}),
           "legacy100x40-duplicate.single-undo-restores-source-destination-and-selection", failures);
}

void Legacy100x40OutlineTracksPreviewAndDeferredApply(int& failures)
{
    DocumentSession session;
    Expect(InitializeLegacyTwoColor(session) && session.BeginDeferredPixelMove(),
           "legacy100x40-outline.fixture-and-deferred-begin", failures);
    session.MovePixels(30, 0);
    const PixelMove* move = session.PendingPixelMove();
    const auto token = Token(session, 181);
    Expect(move && token, "legacy100x40-outline.pending-move-and-token", failures);
    if (!move || !token)
        return;
    const std::uint64_t opening = move->opening;
    const std::uint64_t sequence = move->sequence;
    auto capturedInput = PendingInput(session);
    Expect(token && capturedInput, "legacy100x40-outline.pending-input-and-token", failures);
    if (!token || !capturedInput)
        return;
    RasterEditInput input = std::move(capturedInput.value());
    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    const auto snapshot = preview.stroke ? preview.stroke->PaintSnapshot() : std::nullopt;
    const bool published = preview.status == RasterEditStatus::Ready && snapshot &&
                           session.PublishPixelMovePreview(std::move(preview.stroke), token.value(), opening, sequence);
    Expect(published && SelectionBoundsAre(session.DisplayedSelection(), SelectionBounds{40, 10, 50, 20}) &&
               session.Document()->selection == Rect(10, 10, 10, 10),
           "legacy100x40-outline.preview-displays-translated-outline-before-commit", failures);
    input.pixelMoveOpening = preview.pixelMoveOpening;
    RasterEditResult prepared = RunRasterEdit(input);
    Expect(prepared.status == RasterEditStatus::Ready && prepared.selection &&
               SelectionBoundsAre(session.DisplayedSelection(), SelectionBounds{40, 10, 50, 20}) &&
               SelectionBoundsAre(prepared.selection, SelectionBounds{40, 10, 50, 20}),
           "legacy100x40-outline.outline-stays-translated-while-cpu-result-awaits-owner-apply", failures);
    if (!prepared.layer || !prepared.selection)
        return;
    Expect(session.ApplyPixelMoveIfCurrent(std::move(prepared.layer.value()), std::move(prepared.selection.value()),
                                           token.value(), opening, sequence) &&
               !session.PendingPixelMove() &&
               SelectionBoundsAre(session.Document()->selection, SelectionBounds{40, 10, 50, 20}),
           "legacy100x40-outline.deferred-apply-installs-translated-selection", failures);
}

void RunAll(int& failures)
{
    DeferredBeginAndRoundedSequence(failures);
    PreviewAcceptanceAndRejections(failures);
    PreparedApplyAndUndo(failures);
    PreparedApplyRefusalsRetainOwnership(failures);
    DeferredFinishDuplicateRedoAndLegacy(failures);
    Legacy100x40MoveNumbersAndOneUndo(failures);
    Legacy100x40DuplicatePreservesSourceAndUndoesTogether(failures);
    Legacy100x40OutlineTracksPreviewAndDeferredApply(failures);
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

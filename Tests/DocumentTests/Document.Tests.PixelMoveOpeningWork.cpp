#include "Document.Tests.PixelMoveOpeningWork.h"

#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "Document.h"
#include "RasterEditWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace PixelMoveOpeningWorkTests
{
namespace
{
void Expect(int& failures, bool condition, const char* name)
{
    std::printf("%s pixelmoveopening.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer Image(const Raster& pixels, int width, int height)
{
    ImageLayer layer =
        ImageLayer::Blank(L"Pixel move opening", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), L"Pixel move opening"};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height},
                                                          Vec2{x, y + height}}}};
}

RasterEditInput Input(ImageLayer layer, int canvasWidth, int canvasHeight, DocumentSelection selection)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{1, 0};
    return input;
}

bool PixelsEqual(const Raster& raster, int width, int height, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != width ||
        raster.Height() != height ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, width, height, actual.data(), width * 4, 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

bool CommitIs(const RasterEditResult& result, std::span<const std::uint8_t> expected)
{
    return result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
           PixelsEqual(result.layer->asset->image, 4, 1, expected);
}

bool PreviewIs(const RasterEditPreviewResult& result, std::span<const std::uint8_t> expected)
{
    if (result.status != RasterEditStatus::Ready || !result.stroke)
        return false;
    const std::optional<BrushStroke::Commit> snapshot = result.stroke->PaintSnapshot();
    return snapshot && PixelsEqual(snapshot->image, 4, 1, expected);
}

constexpr std::array<std::uint8_t, 16> kFourPixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                   70, 80, 90, 255, 100, 110, 120, 255};

void ReusedOpeningKeepsLiteralMoveResults(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "reuse.fixture-source-created");
        return;
    }
    DocumentSession session;
    session.CreateDocument(4, 1, true);
    const Raster thumbnail = Raster::MakeThumbnail(source);
    if (!thumbnail.HasStorage() || !session.Document() || session.Document()->layers.empty())
    {
        Expect(failures, false, "reuse.fixture-session-created");
        return;
    }
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{source, thumbnail, layer.name};
                                document.selection = Rect(1, 0, 1, 1);
                            });
    if (!session.ActiveLayer() || !session.ActiveLayer()->asset || !session.Document()->selection)
    {
        Expect(failures, false, "reuse.fixture-session-state-installed");
        return;
    }
    const DocumentSelection originalSelection = session.Document()->selection.value();
    const int undoCount = session.history.UndoCount();
    const bool canRedo = session.history.CanRedo();
    const std::wstring redoName = session.history.RedoName();
    const Guid revision = session.history.CurrentRevision();
    RasterEditInput firstInput = Input(*session.ActiveLayer(), 4, 1, originalSelection);
    firstInput.offset = Vec2{2, 0};
    constexpr std::array<std::uint8_t, 16> expected{10, 20, 30, 255, 0, 0, 0, 0, 70, 80, 90, 255, 40, 50, 60, 255};
    const RasterEditPreviewResult first = RunRasterEditPreview(firstInput);
    const auto opening = first.pixelMoveOpening;
    RasterEditInput reusedPreviewInput = firstInput;
    reusedPreviewInput.pixelMoveOpening = opening;
    const RasterEditPreviewResult reusedPreview = RunRasterEditPreview(reusedPreviewInput);
    RasterEditInput reusedCommitInput = firstInput;
    reusedCommitInput.pixelMoveOpening = opening;
    const RasterEditResult reusedCommit = RunRasterEdit(reusedCommitInput);

    Expect(failures, first.status == RasterEditStatus::Ready && opening && PreviewIs(first, expected),
           "reuse.initial-preview-has-literal-cut-hole-and-landing-pixels");
    Expect(failures, PreviewIs(reusedPreview, expected) && CommitIs(reusedCommit, expected),
           "reuse-cached-opening-retains-literal-cut-hole-and-landing-pixels");
    Expect(failures, reusedPreview.pixelMoveOpening == opening && reusedCommit.pixelMoveOpening == opening,
           "reuse-preview-and-commit-return-the-same-opening-owner");
    Expect(failures, reusedPreview.stroke && first.stroke && reusedPreview.stroke.get() != first.stroke.get(),
           "reuse-preview-allocates-fresh-mutable-stroke-state");
    Expect(failures,
           PixelsEqual(source, 4, 1, kFourPixels) && firstInput.layer.asset &&
               PixelsEqual(firstInput.layer.asset->image, 4, 1, kFourPixels) && session.ActiveLayer() &&
               session.ActiveLayer()->asset && PixelsEqual(session.ActiveLayer()->asset->image, 4, 1, kFourPixels),
           "reuse-worker-calls-leave-shared-captured-and-session-source-pixels-unchanged");
    Expect(failures,
           session.history.UndoCount() == undoCount && session.history.CanRedo() == canRedo &&
               session.history.RedoName() == redoName && session.history.CurrentRevision() == revision &&
               session.Document()->selection && session.Document()->selection.value() == originalSelection,
           "reuse-worker-preview-and-candidate-commit-do-not-change-session-history-or-selection");
}

void ReusedOpeningKeepsPartialAlphaDuplicate(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{17, 33, 65, 128, 90, 80, 70, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "duplicate.fixture-source-created");
        return;
    }
    RasterEditInput input = Input(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1));
    input.offset = Vec2{1, 0};
    input.duplicate = true;
    constexpr std::array<std::uint8_t, 8> expected{17, 33, 65, 128, 62, 73, 100, 255};
    const RasterEditPreviewResult first = RunRasterEditPreview(input);
    input.pixelMoveOpening = first.pixelMoveOpening;
    const RasterEditPreviewResult reusedPreview = RunRasterEditPreview(input);
    const RasterEditResult reusedCommit = RunRasterEdit(input);
    const auto literalDuplicate = [&](const RasterEditPreviewResult& result)
    {
        if (result.status != RasterEditStatus::Ready || !result.stroke)
            return false;
        const auto snapshot = result.stroke->PaintSnapshot();
        return snapshot && PixelsEqual(snapshot->image, 2, 1, expected);
    };
    const bool commitLiteral = reusedCommit.status == RasterEditStatus::Ready && reusedCommit.layer &&
                               reusedCommit.layer->asset &&
                               PixelsEqual(reusedCommit.layer->asset->image, 2, 1, expected);
    Expect(failures, literalDuplicate(first) && literalDuplicate(reusedPreview) && commitLiteral,
           "duplicate-cached-opening-preserves-literal-partial-alpha-source-over-bytes");
    Expect(failures,
           first.pixelMoveOpening && reusedPreview.pixelMoveOpening == first.pixelMoveOpening &&
               reusedCommit.pixelMoveOpening == first.pixelMoveOpening &&
               first.stroke.get() != reusedPreview.stroke.get(),
           "duplicate-opening-identity-shared-while-preview-state-is-fresh");
    Expect(failures, PixelsEqual(source, 2, 1, sourceBytes), "duplicate-preview-and-commit-do-not-mutate-source");
}

void RefusesMismatchedCapturedInputs(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "refusal.fixture-source-created");
        return;
    }
    RasterEditInput seed = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1));
    seed.offset = Vec2{1, 0};
    const auto preview = RunRasterEditPreview(seed);
    if (preview.status != RasterEditStatus::Ready || !preview.pixelMoveOpening)
    {
        Expect(failures, false, "refusal.fixture-opening-created");
        return;
    }
    const auto refused = [&](RasterEditInput candidate)
    {
        candidate.pixelMoveOpening = preview.pixelMoveOpening;
        const auto previewResult = RunRasterEditPreview(candidate);
        const auto commitResult = RunRasterEdit(candidate);
        return previewResult.status == RasterEditStatus::Invalid && !previewResult.stroke &&
               !previewResult.pixelMoveOpening && commitResult.status == RasterEditStatus::Invalid &&
               !commitResult.layer && !commitResult.pixelMoveOpening;
    };

    RasterEditInput selectionIdentity = seed;
    selectionIdentity.selection = Rect(1, 0, 1, 1); // Equal geometry and AA, independently owned loops.
    RasterEditInput antialias = seed;
    antialias.selection->antialiased = !antialias.selection->antialiased;
    RasterEditInput target = seed;
    target.layer.id = Guid::Generate();
    constexpr std::array<std::uint8_t, 16> otherBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    RasterEditInput sourcePixels = seed;
    sourcePixels.layer = Image(Bgra(4, 1, otherBytes), 4, 1);
    sourcePixels.layer.id = seed.layer.id;
    RasterEditInput sourceGrid = seed;
    const std::array<std::uint8_t, 12> gridBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255};
    sourceGrid.layer.asset = RasterAsset{Bgra(3, 1, gridBytes), {}, L"Changed grid"};
    RasterEditInput transform = seed;
    transform.layer.transform.origin.x += 1;
    RasterEditInput canvas = seed;
    canvas.canvasWidth += 1;

    Expect(failures, refused(selectionIdentity) && refused(antialias),
           "refusal-selection-identity-and-antialias-must-match-opening");
    Expect(failures, refused(target) && refused(sourcePixels),
           "refusal-target-identity-and-source-raster-must-match-opening");
    Expect(failures, refused(sourceGrid) && refused(transform) && refused(canvas),
           "refusal-source-grid-transform-and-canvas-must-match-opening");
    Expect(failures,
           selectionIdentity.selection && seed.selection &&
               selectionIdentity.selection->loops == seed.selection->loops &&
               selectionIdentity.selection->loops.Identity() != seed.selection->loops.Identity(),
           "refusal-equal-selection-geometry-has-distinct-captured-identity");
}

struct ReleaseCollector final
{
    std::array<DeferredReleaseNode*, 8> nodes{};
    std::size_t count = 0;
};

void Enqueue(void* context, DeferredReleaseNode& node) noexcept
{
    auto& collector = *static_cast<ReleaseCollector*>(context);
    if (collector.count < collector.nodes.size())
        collector.nodes[collector.count++] = &node;
}

void CachedMemoryAndDeferredRelease(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "memory.fixture-source-created");
        return;
    }
    RasterEditInput input = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1));
    input.offset = Vec2{1, 0};
    RasterEditPreviewResult preview = RunRasterEditPreview(input);
    auto opening = std::move(preview.pixelMoveOpening);
    const std::uint64_t cachedBytes = PixelMoveOpeningMemoryBytes(opening);
    bool stable = cachedBytes != 0;
    for (int attempt = 0; attempt < 128; ++attempt)
        stable = stable && PixelMoveOpeningMemoryBytes(opening) == cachedBytes;
    Expect(failures, preview.status == RasterEditStatus::Ready && opening && stable,
           "memory-opening-byte-bound-is-cached-and-stable");
    Expect(failures, PixelMoveOpeningMemoryBytes({}) == 0, "memory-null-opening-has-zero-bound");
    if (!opening)
        return;
    preview.stroke.reset();

    ReleaseCollector collector;
    const bool installed = InstallDeferredReleaseSink(Enqueue, &collector);
    Expect(failures, installed, "release.owner-thread-sink-installed");
    if (!installed)
        return;
    const auto weakOpening = std::weak_ptr<const PixelMoveOpening>(opening);
    input.pixelMoveOpening.reset();
    auto lastOwner = std::move(opening);
    const bool onlyOneKnownReference = lastOwner.use_count() == 1;
    lastOwner.reset();
    const bool queued = collector.count == 1 && collector.nodes[0] && collector.nodes[0]->bytes == cachedBytes &&
                        collector.nodes[0]->destroy;
    Expect(failures, onlyOneKnownReference && queued && weakOpening.expired(),
           "release-last-opening-owner-queues-one-node-with-cached-bound");
    if (collector.count > 0 && collector.nodes[0] && collector.nodes[0]->destroy)
        collector.nodes[0]->destroy(collector.nodes[0]);
    Expect(failures, ClearDeferredReleaseSink(&collector), "release.owner-thread-sink-cleared");
}

struct CancelProgress final
{
    CancelSource* cancellation = nullptr;
    std::size_t completed = 0;
    std::size_t total = 0;
};

void CancelAfterFirstRow(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& progress = *static_cast<CancelProgress*>(context);
    if (completed == 1 && total == 4)
    {
        progress.completed = completed;
        progress.total = total;
        (void)progress.cancellation->Cancel();
    }
}

void CancellationAndNonmutation(int& failures)
{
    std::array<std::uint8_t, 64> bytes{};
    for (std::size_t y = 0; y < 4; ++y)
        std::copy(kFourPixels.begin(), kFourPixels.end(), bytes.begin() + static_cast<std::ptrdiff_t>(y * 16));
    const Raster source = Bgra(4, 4, bytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "cancel.fixture-source-created");
        return;
    }
    RasterEditInput input = Input(Image(source, 4, 4), 4, 4, Rect(0, 0, 4, 4));
    CancelSource cancellation;
    (void)cancellation.Cancel();
    const RasterEditPreviewResult preCancelled = RunRasterEditPreview(input, cancellation.Token());
    const RasterEditResult cancelledCommit = RunRasterEdit(input, cancellation.Token());

    CancelSource duringCancellation;
    CancelProgress progress{&duringCancellation};
    input.progress = CancelAfterFirstRow;
    input.progressContext = &progress;
    const RasterEditPreviewResult midCancelled = RunRasterEditPreview(input, duringCancellation.Token());
    Expect(failures,
           preCancelled.status == RasterEditStatus::Cancelled && !preCancelled.stroke &&
               !preCancelled.pixelMoveOpening && cancelledCommit.status == RasterEditStatus::Cancelled &&
               !cancelledCommit.layer && !cancelledCommit.pixelMoveOpening,
           "cancel-cancelled-input-publishes-no-opening-preview-or-commit");
    Expect(failures,
           midCancelled.status == RasterEditStatus::Cancelled && !midCancelled.stroke &&
               !midCancelled.pixelMoveOpening && progress.completed == 1 && progress.total == 4 &&
               duringCancellation.Token().IsCancelled(),
           "cancel-mid-lift-cancellation-publishes-no-opening");
    Expect(failures,
           PixelsEqual(source, 4, 4, bytes) && input.layer.asset && PixelsEqual(input.layer.asset->image, 4, 4, bytes),
           "cancel-preview-and-commit-preserve-source-raster-bytes");
}
} // namespace

void Run(int& failures)
{
    ReusedOpeningKeepsLiteralMoveResults(failures);
    ReusedOpeningKeepsPartialAlphaDuplicate(failures);
    RefusesMismatchedCapturedInputs(failures);
    CachedMemoryAndDeferredRelease(failures);
    CancellationAndNonmutation(failures);
}
} // namespace PixelMoveOpeningWorkTests

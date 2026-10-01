#include "GradientResourceTests.h"

#include "BrushStroke.h"
#include "DeferredRelease.h"
#include "Document.h"
#include "RasterEditWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gradientresource.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

struct ReleaseCollector final
{
    std::array<DeferredReleaseNode*, 2> nodes{};
    std::size_t count = 0;
};

void Enqueue(void* context, DeferredReleaseNode& node) noexcept
{
    auto& collector = *static_cast<ReleaseCollector*>(context);
    if (collector.count < collector.nodes.size())
    {
        collector.nodes[collector.count++] = &node;
    }
}

struct ProgressContext final
{
    std::size_t calls = 0;
    std::size_t completed = 0;
    std::size_t total = 0;
};

void Progress(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& progress = *static_cast<ProgressContext*>(context);
    ++progress.calls;
    progress.completed = completed;
    progress.total = total;
}

RasterEditInput GradientInput(ImageLayer layer, int width, int height,
                              std::optional<DocumentSelection> selection = std::nullopt)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = width;
    input.canvasHeight = height;
    input.pixelLimit = static_cast<std::int64_t>(width) * height;
    input.kind = RasterEditKind::Gradient;
    input.color = {0.9, 0.1, 0.2, 1.0};
    input.endColor = {0.1, 0.2, 0.9, 1.0};
    input.start = {0, 0};
    input.end = {static_cast<double>(width), static_cast<double>(height)};
    input.shape = GradientShape::Linear;
    input.opacity = 1.0;
    return input;
}

ImageLayer Layer(int width, int height, const Raster& image = {})
{
    ImageLayer layer =
        ImageLayer::Blank(L"Gradient resource fixture", Vec2{static_cast<double>(width), static_cast<double>(height)});
    if (image.HasStorage())
    {
        layer.asset = RasterAsset{image, {}, L"Gradient resource fixture"};
    }
    return layer;
}

DocumentSelection FullAntialiasedSelection(int width, int height)
{
    return DocumentSelection{true, SelectionLoops{{Vec2{0, 0}, Vec2{static_cast<double>(width), 0},
                                                   Vec2{static_cast<double>(width), static_cast<double>(height)},
                                                   Vec2{0, static_cast<double>(height)}}}};
}

void MemoryFloors()
{
    constexpr int side = 2048;
    constexpr std::uint64_t pixels = static_cast<std::uint64_t>(side) * side;
    const ImageLayer empty = Layer(side, side);
    const auto antialiased = RasterEditMemoryBytes(empty, FullAntialiasedSelection(side, side), side, side,
                                                   static_cast<std::int64_t>(pixels), false);
    Expect(antialiased >= pixels * 16, "memory-floor.antialiased-selection-has-independent-coverage-floor");

    const Raster sparseMask = Raster::MakeSparse(side, side, RasterFormat::Gray8, {}, 0, 0, {}, 255);
    ImageLayer masked = empty;
    if (!sparseMask.HasStorage())
    {
        Expect(false, "memory-floor.sparse-mask-fixture-created");
        return;
    }
    masked.mask = LayerMask{RasterAsset{sparseMask, {}, L"Mask floor"}};
    const auto outputAndMask =
        RasterEditMemoryBytes(masked, std::nullopt, side, side, static_cast<std::int64_t>(pixels), false);
    Expect(outputAndMask >= pixels * 12, "memory-floor.private-image-output-and-mask-floor");

    const ImageLayer tiny = Layer(1, 1);
    const auto metadata = RasterEditMemoryBytes(tiny, std::nullopt, 1, 1, 1, false);
    Expect(metadata >= 1024u * 1024u, "memory-floor.fixed-metadata-and-job-allowance");
    Expect(!sparseMask.IsMaterialized(), "memory-floor.mask-estimate-keeps-sparse-source-unmaterialized");
}

void LargePreviewRetirement()
{
    constexpr int side = 2048;
    constexpr std::uint64_t pixels = static_cast<std::uint64_t>(side) * side;
    Raster source = Raster::Make(side, side, RasterFormat::Bgra8888);
    if (!source.HasStorage())
    {
        Expect(false, "large-preview.source-fixture-created");
        return;
    }
    const void* const sourceIdentity = source.Identity();
    ImageLayer layer = Layer(side, side, source);
    RasterEditInput input = GradientInput(std::move(layer), side, side, FullAntialiasedSelection(side, side));
    ProgressContext progress;
    input.progress = Progress;
    input.progressContext = &progress;

    ReleaseCollector collector;
    const bool installed = InstallDeferredReleaseSink(Enqueue, &collector);
    Expect(installed, "large-preview.release-sink-installed");
    if (!installed)
    {
        return;
    }

    RasterEditPreviewResult result = RunRasterEditPreview(input);
    Expect(result.status == RasterEditStatus::Ready && result.stroke != nullptr,
           "large-preview.gradient-preview-published");
    if (result.status != RasterEditStatus::Ready || !result.stroke)
    {
        static_cast<void>(ClearDeferredReleaseSink(&collector));
        return;
    }

    std::shared_ptr<BrushStroke> copy = result.stroke;
    BrushStroke* const retainedStroke = copy.get();
    const bool sourceAndTilesRetained = copy->SourceImage().Identity() == sourceIdentity &&
                                        copy->Tiles().size() == 64 && copy->Tiles()[0].image.HasStorage() &&
                                        copy->Tiles()[63].image.HasStorage();
    const std::size_t progressCallsAtPublication = progress.calls;
    Expect(progressCallsAtPublication != 0, "large-preview.progress-reported-before-publication");
    input.layer.asset.reset();
    source = {};
    result.stroke.reset();
    Expect(collector.count == 0, "large-preview.first-shared-owner-does-not-retire");
    const bool stillOwnedByCopy = copy->SourceImage().Identity() == sourceIdentity && copy->Tiles().size() == 64 &&
                                  copy->Tiles()[0].image.HasStorage();
    Expect(sourceAndTilesRetained && stillOwnedByCopy,
           "large-preview.source-and-private-tile-images-live-through-shared-copy");

    progress.calls = (std::numeric_limits<std::size_t>::max)();
    progress.completed = (std::numeric_limits<std::size_t>::max)();
    progress.total = (std::numeric_limits<std::size_t>::max)();
    copy.reset();
    Expect(collector.count == 1 && collector.nodes[0] != nullptr,
           "large-preview.last-shared-owner-enqueues-one-preview-node");
    bool previewNodeDestroyed = false;
    if (collector.count == 1 && collector.nodes[0] != nullptr)
    {
        DeferredReleaseNode* const previewNode = collector.nodes[0];
        Expect(retainedStroke->SourceImage().Identity() == sourceIdentity && retainedStroke->Tiles().size() == 64 &&
                   retainedStroke->Tiles()[0].image.HasStorage() && retainedStroke->Tiles()[63].image.HasStorage(),
               "large-preview.queued-node-retains-source-and-private-tiles-until-destroy");
        Expect(previewNode->bytes >= pixels * 16 && previewNode->destroy != nullptr,
               "large-preview.queued-bytes-cover-private-output-and-selected-coverage-floor");
        previewNode->destroy(previewNode);
        previewNodeDestroyed = true;
        Expect(collector.count == 2 && collector.nodes[1] != nullptr &&
                   collector.nodes[1]->bytes >= kDeferredReleaseThreshold,
               "large-preview.source-storage-retires-only-when-preview-node-is-destroyed");
        Expect(progress.calls == (std::numeric_limits<std::size_t>::max)() &&
                   progress.completed == (std::numeric_limits<std::size_t>::max)() &&
                   progress.total == (std::numeric_limits<std::size_t>::max)(),
               "large-preview.progress-context-is-not-borrowed-after-publication");
    }
    Expect(ClearDeferredReleaseSink(&collector), "large-preview.release-sink-cleared");
    if (!previewNodeDestroyed && collector.count > 0 && collector.nodes[0] != nullptr &&
        collector.nodes[0]->destroy != nullptr)
    {
        collector.nodes[0]->destroy(collector.nodes[0]);
    }
    if (collector.count > 1 && collector.nodes[1] != nullptr && collector.nodes[1]->destroy != nullptr)
    {
        collector.nodes[1]->destroy(collector.nodes[1]);
    }
}

void SmallPreviewAndSparseSource()
{
    constexpr int side = 16;
    const Raster base = Raster::Make(side, side, RasterFormat::Bgra8888);
    if (!base.HasStorage())
    {
        Expect(false, "small-preview.base-fixture-created");
        return;
    }
    const Raster sparse = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    if (!sparse.HasStorage())
    {
        Expect(false, "small-preview.sparse-fixture-created");
        return;
    }
    RasterEditInput input = GradientInput(Layer(side, side, sparse), side, side);
    ProgressContext progress;
    input.progress = Progress;
    input.progressContext = &progress;
    ReleaseCollector collector;
    const bool installed = InstallDeferredReleaseSink(Enqueue, &collector);
    Expect(installed, "small-preview.release-sink-installed");
    if (!installed)
    {
        return;
    }

    RasterEditPreviewResult result = RunRasterEditPreview(input);
    const bool published = result.status == RasterEditStatus::Ready && result.stroke != nullptr;
    Expect(published, "small-preview.gradient-preview-published");
    Expect(!sparse.IsMaterialized() && (!published || !result.stroke->SourceImage().IsMaterialized()),
           "small-preview.sparse-source-remains-unmaterialized");
    const auto reported = progress.calls;
    if (published)
    {
        // The private candidate has not been installed anywhere. A subsequent exclusive owner operation must
        // not call the worker's former progress borrow; merely destroying it would not exercise that callback.
        Expect(result.stroke->FillGradient(GradientShape::Linear, {0, 0}, {16, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, 1) &&
                   progress.calls == reported && reported != 0,
               "small-preview-exclusive-owner-paint-does-not-call-retired-worker-progress");
    }
    result.stroke.reset();
    Expect(published && collector.count == 0, "small-preview.below-threshold-release-is-inline");
    Expect(ClearDeferredReleaseSink(&collector), "small-preview.release-sink-cleared");
}
} // namespace

int RunGradientResourceTests()
{
    gFailures = 0;
    MemoryFloors();
    LargePreviewRetirement();
    SmallPreviewAndSparseSource();
    return gFailures;
}

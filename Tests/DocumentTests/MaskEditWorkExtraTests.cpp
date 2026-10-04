#include "MaskEditWorkExtraTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "MaskEditWork.h"
#include "Selection.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s mask-work-extra.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster RedImage(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 0;
        pixels[i + 1] = 0;
        pixels[i + 2] = 255;
        pixels[i + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession SessionWithLayer(int width, int height, bool withAsset, LayerTransform transform = {})
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    ImageLayer layer = ImageLayer::Blank(L"Mask work fixture", transform.size);
    layer.transform = transform;
    if (withAsset)
    {
        Raster image = RedImage(width, height);
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    }
    const Guid active = layer.id;
    document.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.history.Reset();
    return session;
}

std::uint8_t GrayAt(const Raster& raster, int x, int y) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return 0;
    return raster
        .Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(raster.Stride()) + static_cast<std::size_t>(x)];
}

const ImageLayer* ActiveLayer(const DocumentSession& session) noexcept
{
    const auto& document = session.Document();
    if (!document || !session.ActiveLayerID())
        return nullptr;
    for (const ImageLayer& layer : document->layers)
        if (layer.id == session.ActiveLayerID().value())
            return &layer;
    return nullptr;
}

void AntialiasedHalfCoverageAndEdgeTone()
{
    for (const bool revealing : {true, false})
    {
        LayerTransform transform;
        transform.size = {2, 1};
        DocumentSession session = SessionWithLayer(2, 1, true, transform);
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0.5, 0, 1, 1)}; });
        session.history.Reset();
        auto input = session.PrepareMaskEditInput(revealing);
        Expect(input.has_value(), revealing ? "half.reveal-input" : "half.hide-input");
        if (!input)
            continue;
        InlineExecutor executor;
        MaskEditResult result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
        Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(),
               revealing ? "half.reveal-ready" : "half.hide-ready");
        if (!result.publication)
            continue;
        PreparedMaskEdit publication = std::move(result.publication.value());
        Expect(session.ApplyPreparedMaskEditIfCurrent(publication),
               revealing ? "half.reveal-applies" : "half.hide-applies");
        const ImageLayer* layer = ActiveLayer(session);
        Expect(layer && layer->mask.has_value(), revealing ? "half.reveal-mask" : "half.hide-mask");
        if (!layer || !layer->mask)
            continue;
        const Raster& mask = layer->mask->asset.image;
        const std::uint8_t expected = revealing ? 127 : 128;
        Expect(mask.Width() == 2 && mask.Height() == 1 && GrayAt(mask, 0, 0) == expected &&
                   GrayAt(mask, 1, 0) == expected,
               revealing ? "half.reveal-exact-bytes-127-127" : "half.hide-exact-bytes-128-128");
        Expect(MaskEdgeBackground(layer->mask->asset.thumbnail) == (revealing ? 0.0 : 1.0),
               revealing ? "half.reveal-edge-tone-zero-below-127-point-five" : "half.hide-edge-tone-one-at-128");
    }
    const double exactArea = 0.5;
    const int revealingByte = 255 - static_cast<int>(std::lround(exactArea * 255.0));
    const int hidingByte = static_cast<int>(std::lround((1.0 - exactArea) * 255.0));
    Expect(revealingByte == 127 && hidingByte == 128, "half.independent-lround-127-point-5-is-128");
}

void BlankLayerSelectionUsesTransformGrid()
{
    LayerTransform transform;
    transform.size = {4, 3};
    DocumentSession session = SessionWithLayer(0, 0, false, transform);
    session.EditForTest([](CanvasDocument& document)
                        { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 1, 2, 1)}; });
    session.history.Reset();
    auto input = session.PrepareMaskEditInput(true);
    Expect(input.has_value(), "blank.input");
    if (!input)
        return;
    InlineExecutor executor;
    MaskEditResult result = DocumentSession::PrepareMaskEditPublication(std::move(input.value()), executor, {}, {});
    Expect(result.status == MaskEditStatus::Ready && result.publication.has_value(), "blank.ready");
    if (!result.publication)
        return;
    PreparedMaskEdit publication = std::move(result.publication.value());
    Expect(session.ApplyPreparedMaskEditIfCurrent(publication), "blank.applies");
    const ImageLayer* layer = ActiveLayer(session);
    Expect(layer && layer->mask.has_value(), "blank.mask-exists");
    if (!layer || !layer->mask)
        return;
    const Raster& mask = layer->mask->asset.image;
    Expect(mask.Width() == 4 && mask.Height() == 3 && GrayAt(mask, 1, 1) == 0 && GrayAt(mask, 0, 0) == 255,
           "blank.transform-sized-selection-interior-zero-exterior-255");
}

void InvalidNoAssetTransformsRefuseWithoutMutation()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double dimension : {nan, infinity, 30001.0})
    {
        LayerTransform transform;
        transform.size = {1, 1};
        DocumentSession session = SessionWithLayer(0, 0, false, transform);
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    document.layers.front().transform.size.x = dimension;
                                    document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 1, 1)};
                                });
        session.history.Reset();
        const ImageLayer* layerBefore = ActiveLayer(session);
        const auto activeBefore = session.ActiveLayerID();
        const std::uint64_t generationBefore = session.Generation();
        const std::uint64_t documentSerialBefore = session.DocumentSerial();
        const double widthBefore = layerBefore ? layerBefore->transform.size.x : 0.0;
        const void* imageIdentityBefore =
            layerBefore && layerBefore->asset ? layerBefore->asset->image.Identity() : nullptr;
        const auto historyBefore = session.history.CaptureState();
        const auto revisionBefore = session.history.CurrentRevision();
        Expect(session.MaskEditPreparationMemoryBytes() == 0, "invalid.memory-admission-refused");
        const auto input = session.PrepareMaskEditInput(true);
        Expect(!input, "invalid.input-refused");
        const ImageLayer* layerAfter = ActiveLayer(session);
        const bool sameInvalidWidth = layerAfter && std::bit_cast<std::uint64_t>(layerAfter->transform.size.x) ==
                                                        std::bit_cast<std::uint64_t>(widthBefore);
        const void* imageIdentityAfter =
            layerAfter && layerAfter->asset ? layerAfter->asset->image.Identity() : nullptr;
        Expect(layerAfter && sameInvalidWidth && activeBefore == session.ActiveLayerID() &&
                   generationBefore == session.Generation() && documentSerialBefore == session.DocumentSerial() &&
                   imageIdentityBefore == imageIdentityAfter && session.history.CurrentRevision() == revisionBefore &&
                   session.history.CaptureState().Depth() == historyBefore.Depth() &&
                   session.history.CaptureState().PastCount() == historyBefore.PastCount() &&
                   session.history.UndoCount() == 0,
               "invalid.document-and-history-unchanged");
    }
}
} // namespace

int RunMaskEditWorkExtraTests()
{
    gFailures = 0;
    gAssertions = 0;
    AntialiasedHalfCoverageAndEdgeTone();
    BlankLayerSelectionUsesTransformGrid();
    InvalidNoAssetTransformsRefuseWithoutMutation();
    std::printf("%d mask-work-extra assertion(s), %d failure(s).\n", gAssertions, gFailures);
    return gFailures;
}

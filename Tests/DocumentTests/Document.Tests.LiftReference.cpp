#include "Document.Tests.LiftReference.h"

#include "BrushStroke.h"
#include "Document.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace PixelLiftReference
{
namespace
{
using Pixel = std::array<std::uint8_t, 4>; // Premultiplied BGRA.

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++failures;
    }
}

Raster MakeRaster(int width, int height, const std::vector<Pixel>& pixels)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pixels.size() * 4u);
    for (const Pixel& pixel : pixels)
    {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster MakeGray(int width, int height, const std::vector<std::uint8_t>& pixels)
{
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

Guid AddLayer(DocumentSession& session, int width, int height, Vec2 origin, Vec2 size, const std::vector<Pixel>& pixels)
{
    const Raster image = MakeRaster(width, height, pixels);
    ImageLayer layer = ImageLayer::Blank(L"Lift reference", size);
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Lift reference"};
    layer.transform.origin = origin;
    const Guid id = layer.id;
    session.EditForTest([&](CanvasDocument& document) { document.layers.push_back(layer); });
    session.SelectLayer(id);
    return id;
}

void Select(DocumentSession& session, double x, double y, double width, double height)
{
    session.selectionAntialiased = false;
    session.ApplySelection(SelectionGeometry::Rect(x, y, width, height), SelectionMode::New, L"Reference Select");
}

Pixel LayerPixel(const DocumentSession& session, const Guid& id, int x, int y)
{
    Pixel pixel{};
    if (!session.Document().has_value())
    {
        return pixel;
    }
    const ImageLayer* layer = nullptr;
    for (const ImageLayer& candidate : session.Document()->layers)
    {
        if (candidate.id == id)
        {
            layer = &candidate;
            break;
        }
    }
    if (layer == nullptr || !layer->asset.has_value())
    {
        return pixel;
    }
    const Raster& image = layer->asset->image;
    const Vec2 source = PixelToDocument(layer->transform, image.Width(), image.Height())
                            .Inverted()
                            .Apply(Vec2{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5});
    if (source.x < 0.0 || source.y < 0.0 || source.x >= image.Width() || source.y >= image.Height())
    {
        return pixel;
    }
    image.ReadRegion(static_cast<int>(std::floor(source.x)), static_cast<int>(std::floor(source.y)), 1, 1, pixel.data(),
                     4, 0);
    return pixel;
}

Pixel SnapshotPixel(const BrushStroke::Commit& snapshot, int x, int y)
{
    Pixel pixel{};
    if (!snapshot.image.HasStorage() || snapshot.image.Format() != RasterFormat::Bgra8888)
    {
        return pixel;
    }
    const Vec2 source = PixelToDocument(snapshot.transform, snapshot.image.Width(), snapshot.image.Height())
                            .Inverted()
                            .Apply(Vec2{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5});
    if (source.x < 0.0 || source.y < 0.0 || source.x >= snapshot.image.Width() || source.y >= snapshot.image.Height())
    {
        return pixel;
    }
    snapshot.image.ReadRegion(static_cast<int>(std::floor(source.x)), static_cast<int>(std::floor(source.y)), 1, 1,
                              pixel.data(), 4, 0);
    return pixel;
}

void TestTranslatedLayerMove(int& failures)
{
    // Independent coordinate fixture: native-size pixels begin at document (5,3), selection and move are in document
    // space.
    constexpr Pixel red{0, 0, 255, 255};
    constexpr Pixel blue{255, 0, 0, 255};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(12, 8, true);
    const Guid id = AddLayer(session, 3, 2, Vec2{5, 3}, Vec2{3, 2}, {red, blue, clear, clear, red, blue});
    Select(session, 6, 3, 1, 1);
    const bool began = session.BeginPixelMove();
    session.MovePixels(1, 1);
    const std::optional<DocumentSelection> displayed = session.DisplayedSelection();
    const std::optional<SelectionBounds> displayedBounds = displayed.has_value() ? displayed->Bounds() : std::nullopt;
    const bool outlineMoved = displayedBounds.has_value() && displayedBounds.value() == SelectionBounds{7, 4, 8, 5};
    session.FinishPixelMove();
    Expect(began && LayerPixel(session, id, 6, 3) == clear && LayerPixel(session, id, 7, 4) == blue &&
               LayerPixel(session, id, 8, 4) == clear && session.history.UndoName() == L"Move Pixels" && outlineMoved,
           "pixel-lift.translated-layer-pixel-and-outline-coordinate", failures);
}

void TestStretchedLayerIntegerDocumentOffset(int& failures)
{
    // The 2x layer grid makes a two-document-pixel move exactly one source pixel; source values are literal BGRA.
    constexpr Pixel first{10, 20, 30, 255};
    constexpr Pixel selected{40, 50, 60, 255};
    constexpr Pixel destination{70, 80, 90, 255};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(16, 8, true);
    const Guid id = AddLayer(session, 4, 2, Vec2{5, 3}, Vec2{8, 4},
                             {first, selected, clear, destination, clear, clear, clear, clear});
    Select(session, 7, 3, 2, 2);
    const bool began = session.BeginPixelMove();
    session.MovePixels(2, 0);
    session.FinishPixelMove();
    Expect(began && LayerPixel(session, id, 7, 3) == clear && LayerPixel(session, id, 9, 3) == selected &&
               LayerPixel(session, id, 11, 3) == destination,
           "pixel-lift.stretched-layer-integer-document-offset", failures);
}

void TestRepeatedMoveDoesNotAccumulate(int& failures)
{
    // Move through an overlapping destination, then farther; every preview must be rebuilt from the original tile.
    constexpr Pixel red{0, 0, 255, 255};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(8, 3, true);
    const Guid id = AddLayer(session, 5, 1, Vec2{0, 1}, Vec2{5, 1}, {clear, red, clear, clear, clear});
    Select(session, 1, 1, 1, 1);
    const bool began = session.BeginPixelMove();
    session.MovePixels(1, 0);
    Pixel atIntermediate{};
    bool hasPreview = false;
    const PixelMove* pending = session.PendingPixelMove();
    if (pending != nullptr && pending->raster != nullptr)
    {
        const std::optional<BrushStroke::Commit> snapshot = pending->raster->PaintSnapshot();
        if (snapshot.has_value())
        {
            atIntermediate = SnapshotPixel(snapshot.value(), 2, 1);
            hasPreview = true;
        }
    }
    session.MovePixels(2, 0);
    session.FinishPixelMove();
    Expect(began && hasPreview && atIntermediate == red && LayerPixel(session, id, 1, 1) == clear &&
               LayerPixel(session, id, 2, 1) == clear && LayerPixel(session, id, 3, 1) == red,
           "pixel-lift.consecutive-offsets-rebuild-from-source", failures);
}

void TestDuplicateRetainsTranslucentSource(int& failures)
{
    // Premultiplied partial-alpha color checks exact source retention and an exact duplicate, including alpha.
    constexpr Pixel translucent{20, 40, 60, 128};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(6, 3, true);
    const Guid id = AddLayer(session, 4, 1, Vec2{0, 1}, Vec2{4, 1}, {clear, translucent, clear, clear});
    Select(session, 1, 1, 1, 1);
    const bool began = session.BeginPixelMove(true);
    session.MovePixels(2, 0);
    session.FinishPixelMove();
    Expect(began && LayerPixel(session, id, 1, 1) == translucent && LayerPixel(session, id, 3, 1) == translucent &&
               session.history.UndoName() == L"Duplicate Pixels",
           "pixel-lift.duplicate-keeps-translucent-source", failures);
}

void TestTransparentLiftedPixel(int& failures)
{
    // A transparent selected source contributes no hidden RGB at the destination; the adjacent translucent sample
    // remains exact.
    constexpr Pixel translucent{12, 24, 36, 96};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(7, 3, true);
    const Guid id = AddLayer(session, 4, 1, Vec2{0, 1}, Vec2{4, 1}, {clear, translucent, clear, clear});
    Select(session, 1, 1, 2, 1);
    const bool began = session.BeginPixelMove();
    session.MovePixels(2, 0);
    session.FinishPixelMove();
    Expect(began && LayerPixel(session, id, 1, 1) == clear && LayerPixel(session, id, 2, 1) == clear &&
               LayerPixel(session, id, 3, 1) == translucent && LayerPixel(session, id, 4, 1) == clear,
           "pixel-lift.transparent-and-partial-alpha-edges", failures);
}

void TestClippedClipboardCropAndOrigin(int& failures)
{
    // A selection crossing the canvas edge clips the copied document rectangle and keeps its clipped origin.
    constexpr Pixel red{0, 0, 255, 255};
    constexpr Pixel green{0, 255, 0, 255};
    constexpr Pixel blue{255, 0, 0, 255};
    constexpr Pixel white{255, 255, 255, 255};
    DocumentSession session;
    session.CreateDocument(4, 3, true);
    AddLayer(session, 4, 3, Vec2{0, 0}, Vec2{4, 3},
             {red, green, blue, white, blue, white, red, green, green, red, white, blue});
    Select(session, -1, 1, 3, 2);
    const bool copied = session.CopySelection();
    const std::optional<PixelClipboard>& clip = session.Clipboard();
    const std::optional<Guid>& active = session.ActiveLayerID();
    Expect(copied && clip.has_value() && clip->origin == Vec2{0, 1} && clip->image.Width() == 2 &&
               clip->image.Height() == 2 && active.has_value() && LayerPixel(session, active.value(), 0, 1) == blue &&
               clip->image.Data() != nullptr &&
               std::array<std::uint8_t, 4>{clip->image.Data()[0], clip->image.Data()[1], clip->image.Data()[2],
                                           clip->image.Data()[3]} == blue,
           "pixel-lift.clipboard-crop-has-clipped-document-origin", failures);
}

void TestPlacedMaskCopyUsesEdgeTone(int& failures)
{
    // Three of four mask-edge pixels are white: the placed mask is shown as opaque white gray beyond its raster.
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(7, 5, true);
    const Guid id = AddLayer(session, 7, 5, Vec2{0, 0}, Vec2{7, 5}, std::vector<Pixel>(35, clear));
    const Raster mask = MakeGray(2, 2, {255, 255, 255, 0});
    LayerTransform placement;
    placement.origin = Vec2{1, 1};
    placement.size = Vec2{2, 2};
    bool maskInstalled = false;
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            for (ImageLayer& layer : document.layers)
            {
                if (layer.id == id && mask.HasStorage())
                {
                    layer.mask = LayerMask{RasterAsset{mask, mask, L"Mask"}, true, placement, false};
                    maskInstalled = true;
                    break;
                }
            }
        });
    session.SelectLayerTarget(id, true);
    Select(session, 4, 1, 2, 2);
    const bool copied = session.CopySelection();
    const std::optional<PixelClipboard>& clip = session.Clipboard();
    bool whiteCrop = maskInstalled && copied && clip.has_value() && clip->origin == Vec2{4, 1} &&
                     clip->image.Width() == 2 && clip->image.Height() == 2 && clip->image.Data() != nullptr;
    if (whiteCrop)
    {
        for (int i = 0; i < 4; ++i)
        {
            const std::size_t offset = static_cast<std::size_t>(i) * 4u;
            whiteCrop = whiteCrop && clip->image.Data()[offset] == 255 && clip->image.Data()[offset + 1] == 255 &&
                        clip->image.Data()[offset + 2] == 255 && clip->image.Data()[offset + 3] == 255;
        }
    }
    Expect(whiteCrop, "pixel-lift.placed-gray8-mask-copy-uses-displayed-edge-tone", failures);
}

void TestCancelLeavesPixelsAndUndoHistoryAlone(int& failures)
{
    // Cancel drops the live lift; the following Undo still removes only the earlier selection edit.
    constexpr Pixel red{0, 0, 255, 255};
    constexpr Pixel clear{0, 0, 0, 0};
    DocumentSession session;
    session.CreateDocument(6, 3, true);
    const Guid id = AddLayer(session, 4, 1, Vec2{0, 1}, Vec2{4, 1}, {clear, red, clear, clear});
    Select(session, 1, 1, 1, 1);
    const int before = session.history.UndoCount();
    const bool began = session.BeginPixelMove();
    session.MovePixels(2, 0);
    session.CancelPixelMove();
    const bool canceled = began && session.PendingPixelMove() == nullptr && session.history.UndoCount() == before &&
                          LayerPixel(session, id, 1, 1) == red && LayerPixel(session, id, 3, 1) == clear;
    session.Undo();
    Expect(canceled && session.history.UndoCount() == before - 1 && !session.Selection().has_value() &&
               LayerPixel(session, id, 1, 1) == red && LayerPixel(session, id, 3, 1) == clear,
           "pixel-lift.cancel-preserves-pixels-and-prior-undo", failures);
}
} // namespace

void Run(int& failures)
{
    TestTranslatedLayerMove(failures);
    TestStretchedLayerIntegerDocumentOffset(failures);
    TestRepeatedMoveDoesNotAccumulate(failures);
    TestDuplicateRetainsTranslucentSource(failures);
    TestTransparentLiftedPixel(failures);
    TestClippedClipboardCropAndOrigin(failures);
    TestPlacedMaskCopyUsesEdgeTone(failures);
    TestCancelLeavesPixelsAndUndoHistoryAlone(failures);
}
} // namespace PixelLiftReference

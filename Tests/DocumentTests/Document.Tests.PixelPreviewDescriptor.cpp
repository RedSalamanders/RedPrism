#include "Document.Tests.PixelPreviewDescriptor.h"

#include "BrushStroke.h"
#include "Document.h"
#include "PixelMovePreviewWork.h"
#include "RasterEditWork.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace PixelPreviewDescriptorTests
{
namespace
{
void Expect(bool condition, const char* name, int& failures)
{
    std::printf("%s pixel-preview-descriptor.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

constexpr int kCanvasWidth = 100;
constexpr int kCanvasHeight = 40;
constexpr int kRowWidth = 10;
constexpr int kRowHeight = 10;
constexpr std::size_t kBgraBytesPerPixel = 4;
constexpr std::array<std::uint8_t, 4> kRed{0, 0, 255, 255};
constexpr std::array<std::uint8_t, 4> kBlue{255, 0, 0, 255};

struct Fixture final
{
    RasterEditInput input;
    std::shared_ptr<const PixelMoveOpening> opening;
    std::shared_ptr<const SharedPublicationMemory> memory;
    std::shared_ptr<const PreparedPixelMovePreview> moved;
    std::shared_ptr<const PreparedPixelMovePreview> duplicate;
    bool validOnWorker = false;
    Raster originalSource;
};

DocumentSelection Selection(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

bool BuildRedBlueInput(RasterEditInput& input)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kCanvasWidth) * kCanvasHeight * kBgraBytesPerPixel);
    for (int y = 0; y < kCanvasHeight; ++y)
    {
        for (int x = 0; x < kCanvasWidth; ++x)
        {
            const auto& color = x < 50 ? kRed : kBlue;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kCanvasWidth + static_cast<std::size_t>(x)) * kBgraBytesPerPixel;
            std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
    const Raster source =
        Raster::Make(kCanvasWidth, kCanvasHeight, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    if (!source.HasStorage())
        return false;
    ImageLayer layer = ImageLayer::Blank(L"Pixel preview descriptor", Vec2{kCanvasWidth, kCanvasHeight});
    const Raster thumbnail = Raster::MakeThumbnail(source);
    if (!thumbnail.HasStorage())
        return false;
    layer.asset = RasterAsset{source, thumbnail, layer.name};

    input.layer = std::move(layer);
    input.selection = Selection(10, 10, 10, 10);
    input.canvasWidth = kCanvasWidth;
    input.canvasHeight = kCanvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{60, 5};
    return true;
}

bool PrepareFixtureOnWorker(std::thread::id owner, Fixture& fixture)
{
    bool ready = false;
    std::thread worker(
        [&]
        {
            if (!BuildRedBlueInput(fixture.input) || !fixture.input.layer.asset)
                return;
            const auto lift = PreparePixelMoveLiftForWork(fixture.input);
            if (lift.status != PixelMoveLiftStatus::Ready || !lift.opening)
                return;
            fixture.opening = lift.opening;
            fixture.originalSource = fixture.input.layer.asset->image;
            fixture.memory = SharedPublicationMemory::CreateForWork();
            if (!fixture.memory)
                return;
            fixture.moved = PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, fixture.input.offset, false,
                                                                          owner, fixture.memory);
            fixture.duplicate = PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, fixture.input.offset,
                                                                              true, owner, fixture.memory);
            ready = fixture.moved && fixture.duplicate;
            fixture.validOnWorker = ready && fixture.moved->IsValidForWork() && fixture.duplicate->IsValidForWork();
        });
    worker.join();
    return ready;
}

bool ReadPixel(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), static_cast<int>(kBgraBytesPerPixel), 0);
    return actual == expected;
}

bool ReadRowsOnWorker(const PreparedPixelMovePreview& preview, GridRect region,
                      const std::array<std::uint8_t, kRowWidth * kRowHeight * kBgraBytesPerPixel>& expected)
{
    constexpr std::size_t stride = kRowWidth * kBgraBytesPerPixel;
    constexpr std::size_t padding = 13;
    std::array<std::uint8_t, stride * kRowHeight + padding> output{};
    output.fill(0xA9);
    bool accepted = false;
    std::thread worker([&] { accepted = preview.Read(region, output, stride); });
    worker.join();
    return accepted && std::equal(expected.begin(), expected.end(), output.begin()) &&
           std::all_of(output.begin() + static_cast<std::ptrdiff_t>(stride * kRowHeight), output.end(),
                       [](std::uint8_t value) { return value == 0xA9; });
}

void InvalidFactoryInputs(Fixture& fixture, std::thread::id owner, int& failures)
{
    const Vec2 offset{60, 5};
    Expect(!PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, offset, false, owner, fixture.memory),
           "factory-refuses-owner-thread", failures);
    std::array<bool, 7> refused{};
    bool positive = false;
    std::thread worker(
        [&]
        {
            positive = static_cast<bool>(
                PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, offset, false, owner, fixture.memory));
            refused[0] =
                !PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, offset, false, {}, fixture.memory);
            refused[1] = !PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, offset, false, owner, {});
            refused[2] =
                !PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, Vec2{}, false, owner, fixture.memory);
            refused[3] = !PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, Vec2{0.4, -0.4}, false, owner,
                                                                        fixture.memory);
            refused[4] = !PixelMovePreviewReader::PrepareDisplayForWork(
                fixture.opening, Vec2{(std::numeric_limits<double>::infinity)(), 5}, false, owner, fixture.memory);
            CancelSource cancellation;
            (void)cancellation.Cancel();
            refused[5] = !PixelMovePreviewReader::PrepareDisplayForWork(fixture.opening, offset, false, owner,
                                                                        fixture.memory, cancellation.Token());
            refused[6] = !PixelMovePreviewReader::PrepareDisplayForWork({}, offset, false, owner, fixture.memory);
        });
    worker.join();
    Expect(positive, "refusal-worker-has-a-successful-otherwise-identical-positive-control", failures);
    constexpr std::array names{"factory-refuses-unknown-owner",    "factory-refuses-missing-memory",
                               "factory-refuses-zero-offset",      "factory-refuses-rounded-zero-offset",
                               "factory-refuses-nonfinite-offset", "factory-refuses-precancelled-work",
                               "factory-refuses-missing-opening"};
    for (std::size_t i = 0; i < refused.size(); ++i)
        Expect(refused[i], names[i], failures);
}

void LayoutAndReads(Fixture& fixture, std::thread::id owner, int& failures)
{
    const auto& layout = fixture.moved->Layout();
    const auto& source = fixture.input.layer.asset->image;
    const GridRect expectedSourceRect{0, 0, kCanvasWidth, kCanvasHeight};
    const bool exactLayout =
        layout.target == fixture.input.layer.id && layout.source == fixture.originalSource &&
        layout.sourceRect == expectedSourceRect && layout.sourceTransform == fixture.input.layer.transform &&
        layout.paintTransform == fixture.input.layer.transform && layout.width == kCanvasWidth &&
        layout.height == kCanvasHeight && layout.canvasWidth == kCanvasWidth && layout.canvasHeight == kCanvasHeight &&
        layout.sourceOwnershipBytes > 0 && layout.sourceOwnershipBytes == PixelMoveOpeningMemoryBytes(fixture.opening);
    Expect(exactLayout, "layout-target-source-rect-transforms-grid-canvas-and-cached-ownership", failures);
    Expect(fixture.validOnWorker && fixture.moved->Memory() && fixture.moved->Memory()->LocalAllocationBytes() > 0,
           "worker-validity-and-accounted-metadata-allocation", failures);

    constexpr std::size_t rowStride = kRowWidth * kBgraBytesPerPixel;
    std::array<std::uint8_t, rowStride * kRowHeight> ownerOutput{};
    ownerOutput.fill(0xD3);
    Expect(!fixture.moved->IsValidForWork() &&
               !fixture.moved->Read({70, 15, kRowWidth, kRowHeight}, ownerOutput, rowStride) &&
               std::all_of(ownerOutput.begin(), ownerOutput.end(), [](std::uint8_t b) { return b == 0xD3; }),
           "owner-read-refused-with-sentinel-bytes-unchanged", failures);

    std::array<std::uint8_t, kRowWidth * kRowHeight * kBgraBytesPerPixel> expectedHole{};
    std::array<std::uint8_t, kRowWidth * kRowHeight * kBgraBytesPerPixel> expectedRed{};
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(kRowWidth * kRowHeight); ++pixel)
        std::copy(kRed.begin(), kRed.end(),
                  expectedRed.begin() + static_cast<std::ptrdiff_t>(pixel * kBgraBytesPerPixel));
    Expect(ReadRowsOnWorker(*fixture.moved, {10, 10, kRowWidth, kRowHeight}, expectedHole),
           "move-read-10-by-10-original-selection-is-transparent", failures);
    Expect(ReadRowsOnWorker(*fixture.moved, {70, 15, kRowWidth, kRowHeight}, expectedRed),
           "move-read-10-by-10-red-at-independent-landing", failures);
    Expect(ReadRowsOnWorker(*fixture.duplicate, {10, 10, kRowWidth, kRowHeight}, expectedRed),
           "duplicate-read-keeps-original-10-by-10-red", failures);
    Expect(ReadPixel(source, 10, 10, kRed) && ReadPixel(source, 60, 15, kBlue) &&
               fixture.input.layer.asset->image == fixture.originalSource,
           "original-source-red-and-outside-blue-stay-untouched", failures);
    (void)owner;
}

void SparseSourceRead(int& failures)
{
    constexpr std::array<std::uint8_t, 4> baseBytes{7, 11, 13, 255};
    const Raster base = Raster::Make(1, 1, RasterFormat::Bgra8888, baseBytes.data(), baseBytes.size());
    const Raster sparse = Raster::MakeSparse(512, 256, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    if (!base.HasStorage() || !sparse.HasStorage())
    {
        Expect(false, "sparse-fixture-storage-created", failures);
        return;
    }

    RasterEditInput input;
    input.layer = ImageLayer::Blank(L"Sparse pixel preview", Vec2{512, 256});
    input.layer.asset = RasterAsset{sparse, {}, input.layer.name};
    input.selection = Selection(0, 0, 1, 1);
    input.canvasWidth = 512;
    input.canvasHeight = 256;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{1, 0};
    const std::thread::id owner = std::this_thread::get_id();
    std::shared_ptr<const PreparedPixelMovePreview> descriptor;
    std::thread worker(
        [&]
        {
            const auto opening = PreparePixelMoveLiftForWork(input);
            if (opening.status != PixelMoveLiftStatus::Ready || !opening.opening)
                return;
            const auto memory = SharedPublicationMemory::CreateForWork();
            if (!memory)
                return;
            descriptor =
                PixelMovePreviewReader::PrepareDisplayForWork(opening.opening, input.offset, false, owner, memory);
        });
    worker.join();
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && descriptor &&
               descriptor->Layout().source.Identity() == sparse.Identity(),
           "sparse-source-descriptor-retains-source-without-assembly", failures);
    if (!descriptor)
        return;

    constexpr std::array<std::uint8_t, 16> expected{0, 0, 0, 0, 7, 11, 13, 255, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<std::uint8_t, expected.size()> output{};
    bool read = false;
    std::thread readWorker([&] { read = descriptor->Read({0, 0, 4, 1}, output, 16); });
    readWorker.join();
    Expect(read && output == expected && sparse.IsSparse() && !sparse.IsMaterialized(),
           "sparse-source-lift-and-read-use-private-regions-without-shared-assembly", failures);
}
} // namespace

void Run(int& failures)
{
    const std::thread::id owner = std::this_thread::get_id();
    Fixture fixture;
    Expect(PrepareFixtureOnWorker(owner, fixture), "red-blue-opening-and-descriptors-prepared-on-native-worker",
           failures);
    if (fixture.moved && fixture.duplicate && fixture.opening && fixture.memory && fixture.input.layer.asset)
    {
        InvalidFactoryInputs(fixture, owner, failures);
        LayoutAndReads(fixture, owner, failures);
    }
    else
    {
        Expect(false, "descriptor-tests-have-valid-worker-fixture", failures);
    }
    SparseSourceRead(failures);
}
} // namespace PixelPreviewDescriptorTests

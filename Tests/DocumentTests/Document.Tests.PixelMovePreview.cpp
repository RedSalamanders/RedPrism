#include "Document.Tests.PixelMovePreview.h"

#include "Document.h"
#include "PixelMovePreviewWork.h"
#include "RasterEditWork.h"
#include "RasterWork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace PixelMovePreviewTests
{
namespace
{
void Expect(bool condition, const char* label, int& failures)
{
    std::printf("%s pixelmove-preview.%s\n", condition ? "PASS" : "FAIL", label);
    failures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSource{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 16> kMoved{10, 20, 30, 255, 0, 0, 0, 0, 40, 50, 60, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 16> kDuplicated{10, 20, 30, 255, 40,  50,  60,  255,
                                                   40, 50, 60, 255, 100, 110, 120, 255};

Raster MakeBgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer MakeImage(const Raster& image)
{
    ImageLayer layer = ImageLayer::Blank(L"Pixel move preview",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    const Raster thumbnail = Raster::MakeThumbnail(image);
    if (thumbnail.HasStorage())
        layer.asset = RasterAsset{image, thumbnail, layer.name};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height},
                                                          Vec2{x, y + height}}}};
}

RasterEditInput MakeInput(const Raster& image, int canvasWidth, int canvasHeight, DocumentSelection selection)
{
    RasterEditInput input;
    input.layer = MakeImage(image);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    return input;
}

bool RasterBytesEqual(const Raster& image, std::span<const std::uint8_t> expected)
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(image.Width()) * static_cast<std::size_t>(image.Height()) * 4u)
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    image.ReadRegion(0, 0, image.Width(), image.Height(), actual.data(), static_cast<std::size_t>(image.Width()) * 4u,
                     0);
    return std::equal(actual.begin(), actual.end(), expected.begin());
}

struct ReadResult final
{
    bool read = false;
    bool validForWork = false;
    std::vector<std::uint8_t> bytes;
};

ReadResult ReadOnWorker(const PixelMovePreviewReader& reader, GridRect region, std::size_t stride,
                        std::size_t bufferSize)
{
    ReadResult result;
    result.bytes.assign(bufferSize, 0xCD);
    std::thread worker(
        [&]
        {
            result.validForWork = reader.IsValidForWork();
            result.read = reader.Read(region, result.bytes, stride);
        });
    worker.join();
    return result;
}

void LiteralMoveAndDuplicate(int& failures, const Raster& source)
{
    RasterEditInput input = MakeInput(source, 4, 1, Rect(1, 0, 1, 1));
    Expect(input.layer.asset.has_value(), "literal-fixture-image-created", failures);
    if (!input.layer.asset)
        return;
    auto prepared = PreparePixelMoveLiftForWork(input);
    Expect(prepared.status == PixelMoveLiftStatus::Ready && prepared.opening, "literal-opening-prepared", failures);
    if (prepared.status != PixelMoveLiftStatus::Ready || !prepared.opening)
        return;
    input.offset = Vec2{1, 0};
    auto opening = std::move(prepared.opening);
    std::weak_ptr<const PixelMoveOpening> weak = opening;
    PixelMovePreviewReader movedReader(opening, input.offset, false, std::this_thread::get_id());
    PixelMovePreviewReader duplicateReader(opening, input.offset, true, std::this_thread::get_id());
    std::vector<std::uint8_t> ownerOutput(16, 0xA3);
    Expect(!movedReader.IsValidForWork() && !movedReader.Read({0, 0, 4, 1}, ownerOutput, 16) &&
               std::all_of(ownerOutput.begin(), ownerOutput.end(), [](auto b) { return b == 0xA3; }),
           "owner-thread-read-refused-without-touching-output", failures);
    opening.reset();
    Expect(!prepared.opening && weak.use_count() == 2, "only-two-readers-retain-opening-after-all-caller-owners-reset",
           failures);
    const auto movedRead = ReadOnWorker(movedReader, {0, 0, 4, 1}, 16, 16);
    Expect(movedRead.validForWork && movedRead.read &&
               std::equal(movedRead.bytes.begin(), movedRead.bytes.end(), kMoved.begin()),
           "actual-worker-move-matches-four-pixel-literal", failures);
    const auto duplicateRead = ReadOnWorker(duplicateReader, {0, 0, 4, 1}, 16, 16);
    Expect(duplicateRead.validForWork && duplicateRead.read &&
               std::equal(duplicateRead.bytes.begin(), duplicateRead.bytes.end(), kDuplicated.begin()),
           "actual-worker-duplicate-matches-four-pixel-literal", failures);
    Expect(RasterBytesEqual(source, kSource), "literal-source-bytes-unchanged", failures);
}

void StrideAndInvalidInputs(int& failures, const std::shared_ptr<const PixelMoveOpening>& opening)
{
    PixelMovePreviewReader reader(opening, Vec2{1, 0}, false, std::this_thread::get_id());
    constexpr std::size_t stride = 12;
    constexpr std::size_t size = stride + 4;
    std::vector<std::uint8_t> padded(size, 0xA7);
    bool read = false;
    std::thread worker([&] { read = reader.Read({1, 0, 2, 1}, padded, stride); });
    worker.join();
    constexpr std::array<std::uint8_t, 8> subregionExpected{0, 0, 0, 0, 40, 50, 60, 255};
    Expect(read && std::equal(subregionExpected.begin(), subregionExpected.end(), padded.begin()) &&
               std::all_of(padded.begin() + 8, padded.end(), [](std::uint8_t b) { return b == 0xA7; }),
           "subregion-padded-stride-sentinels-preserved", failures);

    const auto rejectedUnchanged = [&](GridRect region, std::size_t badStride, std::size_t capacity, const char* label)
    {
        std::vector<std::uint8_t> output(capacity, 0x5C);
        bool accepted = true;
        std::thread attempt([&] { accepted = reader.Read(region, output, badStride); });
        attempt.join();
        Expect(!accepted && std::all_of(output.begin(), output.end(), [](std::uint8_t b) { return b == 0x5C; }), label,
               failures);
    };
    rejectedUnchanged({0, 0, 4, 1}, 15, 16, "short-stride-refused-with-output-unchanged");
    rejectedUnchanged({0, 0, 4, 1}, 16, 15, "short-span-refused-with-output-unchanged");
    rejectedUnchanged({-1, 0, 1, 1}, 4, 4, "negative-region-refused-with-output-unchanged");
    rejectedUnchanged({0, 0, 5, 1}, 20, 20, "off-grid-region-refused-with-output-unchanged");

    PixelMovePreviewReader unknown(opening, Vec2{1, 0}, false, std::thread::id{});
    Expect(!unknown.IsValidForWork(), "unknown-owner-refused", failures);
    PixelMovePreviewReader nonfinite(opening, Vec2{(std::numeric_limits<double>::infinity)(), 0}, false,
                                     std::this_thread::get_id());
    Expect(!nonfinite.IsValidForWork(), "nonfinite-offset-refused", failures);
}

struct CancellationProgress final
{
    CancelSource cancellation;
    std::size_t completed = 0;
    std::size_t total = 0;
};

void CancelAtRowFive(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& progress = *static_cast<CancellationProgress*>(context);
    progress.completed = completed;
    progress.total = total;
    if (completed == 5)
        (void)progress.cancellation.Cancel();
}

void CancellationAndZeroOffset(int& failures)
{
    constexpr int width = 8, height = 8;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 31;
        pixels[i + 1] = 63;
        pixels[i + 2] = 95;
        pixels[i + 3] = static_cast<std::uint8_t>((i / 4) % 5 == 0 ? 127 : 255);
    }
    const Raster source = MakeBgra(width, height, pixels);
    RasterEditInput input = MakeInput(source, width, height, Rect(2, 2, 3, 3, true));
    const auto prepared = PreparePixelMoveLiftForWork(input);
    Expect(source.HasStorage() && input.layer.asset && prepared.status == PixelMoveLiftStatus::Ready &&
               prepared.opening,
           "antialiased-zero-offset-fixture-prepared", failures);
    if (!source.HasStorage() || !input.layer.asset || !prepared.opening)
        return;
    PixelMovePreviewReader zero(prepared.opening, Vec2{}, false, std::this_thread::get_id());
    std::vector<std::uint8_t> actual(pixels.size(), 0);
    bool zeroRead = false;
    std::thread zeroWorker([&] { zeroRead = zero.Read({0, 0, width, height}, actual, width * 4u); });
    zeroWorker.join();
    Expect(zeroRead && actual == pixels, "zero-offset-antialiased-read-preserves-original-bytes", failures);

    CancelSource alreadyCancelled;
    (void)alreadyCancelled.Cancel();
    std::vector<std::uint8_t> untouched(pixels.size(), 0x6D);
    bool precancelledRead = true;
    std::thread cancelledWorker(
        [&] { precancelledRead = zero.Read({0, 0, width, height}, untouched, width * 4u, alreadyCancelled.Token()); });
    cancelledWorker.join();
    Expect(!precancelledRead &&
               std::all_of(untouched.begin(), untouched.end(), [](std::uint8_t b) { return b == 0x6D; }),
           "precancelled-read-refused-before-output-write", failures);

    PixelMovePreviewReader moving(prepared.opening, Vec2{1, 0}, false, std::this_thread::get_id());
    CancellationProgress progress;
    std::vector<std::uint8_t> partial(pixels.size(), 0xB2);
    bool completed = true;
    std::thread observingWorker(
        [&]
        {
            completed = moving.Read({0, 0, width, height}, partial, width * 4u, progress.cancellation.Token(),
                                    PixelMovePreviewObserver{&progress, CancelAtRowFive});
        });
    observingWorker.join();
    Expect(!completed && progress.completed == 5 && progress.total == height, "observer-cancels-after-actual-fifth-row",
           failures);
}

void SparseSourceStaysSparse(int& failures)
{
    constexpr std::array<std::uint8_t, 4> baseBytes{7, 11, 13, 255};
    const Raster base = MakeBgra(1, 1, baseBytes);
    const Raster sparse = Raster::MakeSparse(512, 256, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    RasterEditInput input = MakeInput(sparse, 512, 256, Rect(0, 0, 1, 1));
    const auto prepared = PreparePixelMoveLiftForWork(input);
    Expect(base.HasStorage() && sparse.HasStorage() && sparse.IsSparse() && !sparse.IsMaterialized() &&
               input.layer.asset && prepared.status == PixelMoveLiftStatus::Ready && prepared.opening,
           "sparse-source-opening-prepared-without-materialization", failures);
    if (!prepared.opening)
        return;
    PixelMovePreviewReader reader(prepared.opening, Vec2{1, 0}, false, std::this_thread::get_id());
    std::vector<std::uint8_t> region(16, 0xFF);
    bool read = false;
    std::thread worker([&] { read = reader.Read({0, 0, 4, 1}, region, 16); });
    worker.join();
    constexpr std::array<std::uint8_t, 16> expected{0, 0, 0, 0, 7, 11, 13, 255, 0, 0, 0, 0, 0, 0, 0, 0};
    Expect(read && std::equal(region.begin(), region.end(), expected.begin()) && sparse.IsSparse() &&
               !sparse.IsMaterialized(),
           "sparse-region-literal-hole-and-landing-without-source-materialization", failures);
}

void MultipleRowsAndPadding(int& failures)
{
    std::vector<std::uint8_t> pixels;
    for (unsigned row = 0; row < 3; ++row)
        pixels.insert(pixels.end(), kSource.begin(), kSource.end());
    auto input = MakeInput(MakeBgra(4, 3, pixels), 4, 3, Rect(1, 0, 1, 3));
    const auto opening = PreparePixelMoveLiftForWork(input);
    Expect(opening.status == PixelMoveLiftStatus::Ready && opening.opening, "multirow-fixture-ready", failures);
    if (!opening.opening)
        return;
    PixelMovePreviewReader reader(opening.opening, {1, 0}, false, std::this_thread::get_id());
    const auto read = ReadOnWorker(reader, {1, 1, 2, 2}, 16, 32);
    constexpr std::array<std::uint8_t, 8> expected{0, 0, 0, 0, 40, 50, 60, 255};
    bool exact = read.validForWork && read.read;
    for (unsigned row = 0; row < 2 && exact; ++row)
    {
        exact = std::equal(expected.begin(), expected.end(), read.bytes.begin() + row * 16);
        for (unsigned column = 8; column < 16; ++column)
            exact = exact && read.bytes[row * 16 + column] == 0xCD;
    }
    Expect(exact, "two-offset-rows-have-literal-pixels-and-untouched-interrow-and-trailing-padding", failures);
    const auto overflow = ReadOnWorker(reader, {0, 0, 1, 2}, (std::numeric_limits<std::size_t>::max)(), 16);
    Expect(!overflow.read &&
               std::all_of(overflow.bytes.begin(), overflow.bytes.end(), [](auto value) { return value == 0xCD; }),
           "stride-overflow-refuses-before-any-output-byte", failures);
}

void TransformedTrustedReference(int& failures)
{
    std::vector<std::uint8_t> source(4 * 4 * 4);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            const auto at = (y * 4 + x) * 4;
            source[at] = static_cast<std::uint8_t>(10 + x * 13);
            source[at + 1] = static_cast<std::uint8_t>(20 + y * 11);
            source[at + 2] = static_cast<std::uint8_t>(30 + x * 7 + y * 5);
            source[at + 3] = 127;
        }
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        auto input = MakeInput(MakeBgra(4, 4, source), 32, 32, Rect(4.25, 4.25, 7.5, 7.5, true));
        input.layer.transform.origin = {4, 4};
        input.layer.transform.size = {8, 8};
        input.layer.transform.rotation = scenario == 0 ? 0 : scenario == 1 ? 15 : -30;
        input.layer.transform.flipX = scenario == 1;
        input.layer.transform.flipY = scenario == 2;
        input.offset = scenario == 0 ? Vec2{1, 1} : scenario == 1 ? Vec2{3, -2} : Vec2{-1, 1};
        input.duplicate = scenario == 2;
        const auto plan = BrushStroke::PlanGrid(input.layer, false, {}, 32, 32);
        auto opening = PreparePixelMoveLiftForWork(input);
        const bool ready = plan && plan->width > 0 && plan->height > 0 && plan->width < 128 && plan->height < 128 &&
                           opening.status == PixelMoveLiftStatus::Ready && opening.opening;
        Expect(ready, "transformed-reference-opening-ready", failures);
        if (!ready)
            continue;
        input.pixelMoveOpening = opening.opening;
        const auto reference = RunRasterEdit(input);
        const auto stride = static_cast<std::size_t>(plan->width) * 4;
        std::vector<std::uint8_t> expected(stride * static_cast<std::size_t>(plan->height));
        const bool referenceReady = reference.status == RasterEditStatus::Ready && reference.previewCommit &&
                                    ReadRasterRegionForWork(reference.previewCommit->image,
                                                            {-reference.previewCommit->x, -reference.previewCommit->y,
                                                             plan->width, plan->height},
                                                            expected, stride, 0);
        PixelMovePreviewReader reader(opening.opening, input.offset, input.duplicate, std::this_thread::get_id());
        const auto actual = ReadOnWorker(reader, {0, 0, plan->width, plan->height}, stride, expected.size());
        if (!referenceReady || !actual.read || actual.bytes != expected)
        {
            const auto different = std::mismatch(actual.bytes.begin(), actual.bytes.end(), expected.begin());
            std::printf("INFO pixelmove-preview.reference scenario=%u grid=%d,%d reference=%u read=%u "
                        "first-different-byte=%zu\n",
                        scenario, plan->width, plan->height, static_cast<unsigned int>(referenceReady),
                        static_cast<unsigned int>(actual.read),
                        static_cast<std::size_t>(different.first - actual.bytes.begin()));
        }
        Expect(referenceReady && actual.validForWork && actual.read && actual.bytes == expected,
               "scaled-rotated-flipped-antialiased-preview-matches-trusted-cpu-frame", failures);
        Expect(RasterBytesEqual(input.layer.asset->image, source),
               "transformed-reference-keeps-independent-source-bytes", failures);
    }
}
} // namespace

void Run(int& failures)
{
    const Raster source = MakeBgra(4, 1, kSource);
    Expect(source.HasStorage() && RasterBytesEqual(source, kSource), "active-root-four-pixel-fixture-created",
           failures);
    if (!source.HasStorage())
        return;
    LiteralMoveAndDuplicate(failures, source);

    RasterEditInput input = MakeInput(source, 4, 1, Rect(1, 0, 1, 1));
    const auto prepared = PreparePixelMoveLiftForWork(input);
    Expect(prepared.status == PixelMoveLiftStatus::Ready && prepared.opening, "shared-opening-fixture-prepared",
           failures);
    if (prepared.opening)
        StrideAndInvalidInputs(failures, prepared.opening);
    else
        Expect(false, "stride-and-refusal-fixture-available", failures);
    CancellationAndZeroOffset(failures);
    SparseSourceStaysSparse(failures);
    MultipleRowsAndPadding(failures);
    TransformedTrustedReference(failures);
}
} // namespace PixelMovePreviewTests

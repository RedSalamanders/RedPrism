#include "Document.Tests.WarpPreviewReader.h"

#include "SharedPublicationMemory.h"
#include "WarpPreviewWork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s warp-preview-reader.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kRamp{0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150};
constexpr std::array<std::uint8_t, 4> kSmudgePixel{32, 16, 8, 64};
constexpr BrushSettings kLiquifySettings{2, 1, 0, 0, 0, 1};
constexpr BrushSettings kSmudgeSettings{4, 0.98, 0, 0, 0, 0.8};

LayerTransform Placement(double x, double y, double width, double height, LayerSampling sampling)
{
    LayerTransform result;
    result.origin = {x, y};
    result.size = {width, height};
    result.sampling = sampling;
    return result;
}

Raster RampImage()
{
    return Raster::Make(2, 2, RasterFormat::Bgra8888, kRamp.data(), kRamp.size());
}

Raster UniformImage(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t offset = 0; offset < pixels.size(); offset += kSmudgePixel.size())
        std::copy(kSmudgePixel.begin(), kSmudgePixel.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

struct Prepared final
{
    std::shared_ptr<const PreparedWarpPreview> preview;
    bool factoryValid = false;
    bool noDabs = false;
};

Prepared PrepareOnJoinedWorker(WarpSourceView source, std::thread::id owner, SmearMode mode,
                               const BrushSettings& settings)
{
    Prepared result;
    std::thread worker(
        [&result, source = std::move(source), owner, mode, settings]() mutable
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            auto stroke =
                WarpStroke::MakePlacedForWork(std::move(source), mode, settings, owner, CancelToken{}, memory);
            result.factoryValid = stroke && stroke->IsValid();
            result.noDabs = stroke && stroke->Points().empty();
            if (stroke && memory)
                result.preview = PrepareWarpPreviewForWork(*stroke, owner, CancelToken{}, std::move(memory));
        });
    worker.join();
    return result;
}

void FactoryAndPlacedReads()
{
    const auto owner = std::this_thread::get_id();
    const auto source = RampImage();
    Expect(source.HasStorage(), "literal-ramp-source-created");

    const auto identity =
        PrepareOnJoinedWorker(WarpSourceView{source, Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2}, owner,
                              SmearMode::Liquify, kLiquifySettings);
    Expect(identity.factoryValid && identity.noDabs && identity.preview && identity.preview->source.image == source &&
               identity.preview->tiles.empty(),
           "joined-worker-factory-retains-placed-source-with-no-dab-tiles");

    bool identityBytes = false;
    bool ownerRefused = false;
    if (identity.preview)
    {
        std::thread readerWorker(
            [&]
            {
                WarpPreviewReader reader(identity.preview);
                constexpr std::size_t stride = 11;
                std::array<std::uint8_t, stride * 2> bytes{};
                bytes.fill(0xCD);
                const bool read = reader.Read({0, 0, 2, 2}, bytes, stride);
                identityBytes = read && std::equal(kRamp.begin(), kRamp.begin() + 8, bytes.begin()) &&
                                std::equal(kRamp.begin() + 8, kRamp.end(), bytes.begin() + stride) &&
                                bytes[8] == 0xCD && bytes[9] == 0xCD && bytes[10] == 0xCD &&
                                bytes[stride + 8] == 0xCD && bytes[stride + 9] == 0xCD && bytes[stride + 10] == 0xCD;
            });
        readerWorker.join();

        WarpPreviewReader ownerReader(identity.preview);
        std::array<std::uint8_t, 8> untouched{};
        untouched.fill(0xA5);
        ownerRefused = !ownerReader.Read({0, 0, 2, 1}, untouched, 8) &&
                       std::all_of(untouched.begin(), untouched.end(), [](std::uint8_t v) { return v == 0xA5; });
    }
    Expect(identityBytes, "identity-read-copies-literal-bgra-rows-and-preserves-padding-canaries");
    Expect(ownerRefused, "reader-refuses-known-owner-before-writing");

    const auto translated =
        PrepareOnJoinedWorker(WarpSourceView{source, Placement(1, 0, 2, 2, LayerSampling::Nearest), 3, 2}, owner,
                              SmearMode::Liquify, kLiquifySettings);
    bool translatedBytes = false;
    if (translated.preview)
    {
        std::thread readerWorker(
            [&]
            {
                WarpPreviewReader reader(translated.preview);
                std::array<std::uint8_t, 24> bytes{};
                translatedBytes =
                    reader.Read({0, 0, 3, 2}, bytes, 12) &&
                    std::all_of(bytes.begin(), bytes.begin() + 4, [](std::uint8_t v) { return v == 0; }) &&
                    std::equal(kRamp.begin(), kRamp.begin() + 8, bytes.begin() + 4) &&
                    std::all_of(bytes.begin() + 12, bytes.begin() + 16, [](std::uint8_t v) { return v == 0; }) &&
                    std::equal(kRamp.begin() + 8, kRamp.end(), bytes.begin() + 16);
            });
        readerWorker.join();
    }
    Expect(translatedBytes, "nearest-translation-reads-source-at-shifted-document-pixels");

    const auto smooth =
        PrepareOnJoinedWorker(WarpSourceView{source, Placement(0, 0, 4, 4, LayerSampling::Smooth), 4, 4}, owner,
                              SmearMode::Liquify, kLiquifySettings);
    bool smoothBytes = false;
    if (smooth.preview)
    {
        std::thread readerWorker(
            [&]
            {
                WarpPreviewReader reader(smooth.preview);
                std::array<std::uint8_t, 4> pixel{};
                smoothBytes = reader.Read({1, 1, 1, 1}, pixel, pixel.size()) &&
                              pixel == std::array<std::uint8_t, 4>{30, 40, 50, 60};
            });
        readerWorker.join();
    }
    Expect(smoothBytes, "smooth-placement-rounds-the-literal-quarter-offset-sample");
}

void SamplingAndRefusals()
{
    const auto owner = std::this_thread::get_id();
    const auto prepared =
        PrepareOnJoinedWorker(WarpSourceView{RampImage(), Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2}, owner,
                              SmearMode::Liquify, kLiquifySettings);
    bool samples = false;
    bool refusedUnchanged = false;
    bool invalidReadsUnchanged = false;
    bool preCancelledUnchanged = false;
    if (prepared.preview)
    {
        std::thread readerWorker(
            [&]
            {
                WarpPreviewReader reader(prepared.preview);
                std::array<double, 4> quarter{};
                std::array<double, 4> half{};
                samples = reader.Sample({0.75, 1.0}, quarter) && reader.Sample({1.0, 1.0}, half) &&
                          quarter == std::array<double, 4>{50, 60, 70, 80} &&
                          half == std::array<double, 4>{60, 70, 80, 90};

                std::array<double, 4> output{7, 8, 9, 10};
                const bool outside = !reader.Sample({2.0, 1.0}, output);
                const bool nonfinite = !reader.Sample({(std::numeric_limits<double>::quiet_NaN)(), 1.0}, output);
                refusedUnchanged = outside && nonfinite && output == std::array<double, 4>{7, 8, 9, 10};

                std::array<std::uint8_t, 16> destination{};
                destination.fill(0xB6);
                const bool badRegion = !reader.Read({-1, 0, 1, 1}, destination, 4);
                const bool shortStride = !reader.Read({0, 0, 2, 1}, destination, 7);
                const bool shortSpan = !reader.Read({0, 0, 2, 2}, std::span<std::uint8_t>(destination).first(11), 8);
                invalidReadsUnchanged =
                    badRegion && shortStride && shortSpan &&
                    std::all_of(destination.begin(), destination.end(), [](std::uint8_t v) { return v == 0xB6; });

                std::stop_source cancellation;
                static_cast<void>(cancellation.request_stop());
                destination.fill(0xC7);
                preCancelledUnchanged =
                    !reader.Read({0, 0, 2, 2}, destination, 8, CancelToken(cancellation.get_token(), {})) &&
                    std::all_of(destination.begin(), destination.end(), [](std::uint8_t v) { return v == 0xC7; });
            });
        readerWorker.join();
    }
    Expect(samples, "sample-returns-unrounded-literal-quarter-and-four-equal-tap-half-values");
    Expect(refusedUnchanged, "outside-and-nonfinite-samples-refuse-with-output-unchanged");
    Expect(invalidReadsUnchanged, "invalid-region-short-stride-and-short-span-refuse-before-writing");
    Expect(preCancelledUnchanged, "pre-cancelled-read-refuses-before-writing");

    bool unknownOwnerRefused = false;
    bool preCancelledFactoryRefused = false;
    std::thread factoryWorker(
        [&]
        {
            auto unknownMemory = SharedPublicationMemory::CreateForWork();
            auto unknown = WarpStroke::MakePlacedForWork(
                WarpSourceView{RampImage(), Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2}, SmearMode::Liquify,
                kLiquifySettings, std::thread::id{}, CancelToken{}, unknownMemory);
            unknownOwnerRefused = !unknown;

            std::stop_source cancellation;
            static_cast<void>(cancellation.request_stop());
            auto cancelledMemory = SharedPublicationMemory::CreateForWork();
            auto cancelled = WarpStroke::MakePlacedForWork(
                WarpSourceView{RampImage(), Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2}, SmearMode::Liquify,
                kLiquifySettings, owner, CancelToken(cancellation.get_token(), {}), cancelledMemory);
            preCancelledFactoryRefused = !cancelled;
        });
    factoryWorker.join();
    Expect(unknownOwnerRefused, "factory-refuses-unknown-owner-thread");
    Expect(preCancelledFactoryRefused, "factory-refuses-pre-cancelled-construction");
}

void SmudgeSnapshots()
{
    const auto owner = std::this_thread::get_id();
    const auto source = UniformImage(4, 5);
    std::array<std::uint8_t, 4 * 5 * 4> original{};
    std::memcpy(original.data(), source.Data(), original.size());
    bool accepted = false;
    bool snapshots = false;
    bool sourceUnchanged = false;

    std::thread worker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            auto stroke = WarpStroke::MakePlacedForWork(
                WarpSourceView{source, Placement(0, 0, 4, 5, LayerSampling::Nearest), 4, 5}, SmearMode::Smudge,
                kSmudgeSettings, owner, CancelToken{}, memory);
            if (!stroke || !memory || !stroke->AppendForWork({0, 2}, {}) || !stroke->AppendForWork({1, 2}, {}))
                return;
            auto first = PrepareWarpPreviewForWork(*stroke, owner, CancelToken{}, memory);
            if (!first || first->tiles.size() != 1)
                return;
            if (!stroke->AppendForWork({2, 2}, {}))
                return;
            auto second = PrepareWarpPreviewForWork(*stroke, owner, CancelToken{}, std::move(memory));
            if (!second || second->tiles.size() != 1)
                return;

            WarpPreviewReader firstReader(first);
            WarpPreviewReader secondReader(second);
            std::array<std::uint8_t, 4 * 5 * 4> firstPixels{};
            std::array<std::uint8_t, 4 * 5 * 4> secondPixels{};
            if (!firstReader.Read({0, 0, 4, 5}, firstPixels, 16) || !secondReader.Read({0, 0, 4, 5}, secondPixels, 16))
                return;

            accepted = stroke->IsValid() && stroke->Points().size() == 2;
            snapshots = true;
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 4; ++x)
                {
                    const std::size_t offset = (static_cast<std::size_t>(y) * 4 + x) * 4;
                    const bool firstTransparent = x == 0 && y >= 1 && y <= 3;
                    const std::array<std::uint8_t, 4> firstExpected =
                        firstTransparent ? std::array<std::uint8_t, 4>{} : kSmudgePixel;
                    snapshots = snapshots && std::equal(firstExpected.begin(), firstExpected.end(),
                                                        firstPixels.begin() + static_cast<std::ptrdiff_t>(offset));
                    const bool secondTransparent = x <= 1 && y >= 1 && y <= 3;
                    const std::array<std::uint8_t, 4> secondExpected =
                        secondTransparent ? std::array<std::uint8_t, 4>{} : kSmudgePixel;
                    snapshots = snapshots && std::equal(secondExpected.begin(), secondExpected.end(),
                                                        secondPixels.begin() + static_cast<std::ptrdiff_t>(offset));
                }
            sourceUnchanged = source.HasStorage() && source.IsMaterialized() &&
                              std::memcmp(source.Data(), original.data(), original.size()) == 0;
        });
    worker.join();
    Expect(accepted, "smudge-seed-and-append-accepted-on-joined-worker");
    Expect(snapshots, "complete-snapshots-preserve-prior-pixels-and-include-later-dab-as-full-image");
    Expect(sourceUnchanged, "smudge-preview-never-mutates-retained-original-source");
}

void PreviewFactoryRefusals()
{
    const auto owner = std::this_thread::get_id();
    std::shared_ptr<WarpStroke> draft;
    std::shared_ptr<SharedPublicationMemory> memory;
    std::thread::id producer;
    bool unknown = false, foreign = false, missingMemory = false, cancelled = false;
    bool draftPreserved = false, unpreparedRefused = false;
    std::thread worker(
        [&]
        {
            producer = std::this_thread::get_id();
            memory = SharedPublicationMemory::CreateForWork();
            draft = WarpStroke::MakePlacedForWork({RampImage(), Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2},
                                                  SmearMode::Liquify, kLiquifySettings, owner, {}, memory);
            if (!draft)
                return;
            unknown = !PrepareWarpPreviewForWork(*draft, {}, {}, memory);
            foreign = !PrepareWarpPreviewForWork(*draft, std::this_thread::get_id(), {}, memory);
            missingMemory = !PrepareWarpPreviewForWork(*draft, owner, {}, {});
            std::stop_source stop;
            static_cast<void>(stop.request_stop());
            cancelled = !PrepareWarpPreviewForWork(*draft, owner, CancelToken(stop.get_token(), {}), memory);
            draftPreserved = draft->IsValid() && draft->Points().empty() && draft->AllTouchedTilesForWork({}).empty();
            auto incomplete = std::make_shared<PreparedWarpPreview>();
            incomplete->memory = memory;
            incomplete->ownerThread = owner;
            incomplete->source = {RampImage(), Placement(0, 0, 2, 2, LayerSampling::Nearest), 2, 2};
            WarpPreviewReader reader(incomplete);
            std::array<std::uint8_t, 4> pixel{1, 2, 3, 4};
            unpreparedRefused =
                !reader.Read({0, 0, 1, 1}, pixel, 4) && pixel == std::array<std::uint8_t, 4>{1, 2, 3, 4};
        });
    worker.join();
    Expect(draft != nullptr, "preview-refusal-controls-use-valid-worker-draft");
    Expect(unknown, "preview-factory-refuses-unknown-owner");
    Expect(foreign, "preview-factory-refuses-current-worker-as-owner");
    Expect(missingMemory, "preview-factory-refuses-missing-original-accounting-owner");
    Expect(cancelled && draftPreserved, "pre-cancelled-preview-refuses-without-invalidating-unchanged-draft");
    Expect(unpreparedRefused, "public-metadata-cannot-forge-complete-reader-authority");
    Expect(draft && !PrepareWarpPreviewForWork(*draft, owner, {}, memory),
           "preview-factory-refuses-known-owner-before-snapshot-work");
    Expect(draft && !PrepareWarpPreviewForWork(*draft, producer, {}, memory),
           "preview-factory-refuses-foreign-original-owner");
    Expect(!WarpPreviewMetadataMemoryBytes(0, 2) && !WarpPreviewMetadataMemoryBytes(2, -1) &&
               !WarpPreviewMetadataMemoryBytes(kMaxCanvasSide + 1, 1) && !WarpPreviewMetadataMemoryBytes(10001, 10000),
           "metadata-bound-refuses-empty-side-limit-and-over-100mp-geometries");
}

void CrossingTileDirectory()
{
    // Four literal constant quadrants, split on the 256-square tile boundaries. A hard radius-2
    // Smudge carries x=254/255 into x=255/256. Those two columns share the left quadrant values,
    // including below the horizontal tile boundary. This reference never reads a computed result.
    constexpr std::array<std::uint8_t, 4> topLeft{0, 0, 64, 255}, topRight{0, 64, 0, 255};
    constexpr std::array<std::uint8_t, 4> bottomLeft{64, 0, 0, 255}, bottomRight{32, 32, 32, 255};
    constexpr int side = 300;
    std::vector<std::uint8_t> bytes(side * side * 4);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            const auto& pixel = y < 256 ? (x < 256 ? topLeft : topRight) : (x < 256 ? bottomLeft : bottomRight);
            std::copy(pixel.begin(), pixel.end(), bytes.begin() + (y * side + x) * 4);
        }
    const auto source = Raster::Make(side, side, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    const auto owner = std::this_thread::get_id();
    bool extents = false, crossed = false, sample = false, unchanged = false, versions = false;
    std::thread worker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            auto draft =
                WarpStroke::MakePlacedForWork({source, Placement(0, 0, side, side, LayerSampling::Nearest), side, side},
                                              SmearMode::Smudge, kSmudgeSettings, owner, {}, memory);
            if (!draft || !draft->AppendForWork({255, 255}, {}) || !draft->AppendForWork({256, 255}, {}))
                return;
            const auto first = PrepareWarpPreviewForWork(*draft, owner, {}, memory);
            if (!first || first->tiles.size() != 4)
                return;
            constexpr std::array<GridRect, 4> expected{
                {{0, 0, 256, 256}, {256, 0, 44, 256}, {0, 256, 256, 44}, {256, 256, 44, 44}}};
            extents = true;
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                const auto& tile = first->tiles[i];
                extents = extents && GridRect{tile.x, tile.y, tile.Width(), tile.Height()} == expected[i];
            }
            WarpPreviewReader reader(first);
            constexpr std::array<std::uint8_t, 16> expectedCross{0,  0, 64, 255, 0,  0, 64, 255,
                                                                 64, 0, 0,  255, 64, 0, 0,  255};
            std::array<std::uint8_t, 16> cross{};
            crossed = reader.Read({255, 255, 2, 2}, cross, 8) && cross == expectedCross;
            std::array<double, 4> value{};
            sample = reader.Sample({256, 256}, value) && value == std::array<double, 4>{32, 0, 32, 255};
            std::array<std::uint8_t, 4> far{};
            unchanged = reader.Read({299, 299, 1, 1}, far, 4) && far == bottomRight;
            if (!draft->AppendForWork({257, 255}, {}))
                return;
            const auto second = PrepareWarpPreviewForWork(*draft, owner, {}, memory);
            cross.fill(0);
            versions = second && second->tiles.size() == 4 && reader.Read({255, 255, 2, 2}, cross, 8) &&
                       cross == expectedCross;
            // Later edits replace every touched immutable tile version, including the thin edge tiles.
            if (second)
                for (std::size_t i = 0; i < first->tiles.size(); ++i)
                    versions = versions && first->tiles[i].pixels != second->tiles[i].pixels;
        });
    worker.join();
    Expect(extents, "directory-covers-four-row-major-clipped-crossing-tiles");
    Expect(crossed, "rectangle-read-crosses-both-tile-boundaries-with-literal-pixels");
    Expect(sample, "unrounded-sample-combines-literal-taps-from-four-tile-entries");
    Expect(unchanged, "far-pixels-preserve-the-literal-original-quadrant");
    Expect(versions, "later-dab-keeps-old-preview-immutable-and-publishes-complete-new-versions");
}

void LargeSparsePreview()
{
    constexpr int side = 10000;
    const auto owner = std::this_thread::get_id();
    const auto sparse = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    bool boundedMetadata = false;
    bool completeSparseRead = false;
    std::thread worker(
        [&]
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            auto stroke = WarpStroke::MakePlacedForWork(
                WarpSourceView{sparse, Placement(0, 0, side, side, LayerSampling::Nearest), side, side},
                SmearMode::Liquify, kLiquifySettings, owner, CancelToken{}, memory);
            if (!stroke || !memory || !stroke->AppendForWork({5000, 5000}, {}) ||
                !stroke->AppendForWork({5001, 5000}, {}))
                return;
            auto preview = PrepareWarpPreviewForWork(*stroke, owner, CancelToken{}, std::move(memory));
            if (!preview)
                return;
            WarpPreviewReader reader(preview);
            std::array<std::uint8_t, 4> untouched{};
            std::array<std::uint8_t, 4> touched{};
            const bool farRead = reader.Read({9999, 9999, 1, 1}, untouched, 4);
            const bool touchRead = reader.Read({5000, 5000, 1, 1}, touched, 4);
            boundedMetadata = WarpPreviewMetadataMemoryBytes(side, side) > 0 && preview->tiles.size() == 1 &&
                              preview->tiles[0].x == 4864 && preview->tiles[0].y == 4864 &&
                              preview->tiles[0].pixels.Width() == 256 && preview->tiles[0].pixels.Height() == 256;
            completeSparseRead =
                farRead && touchRead &&
                std::all_of(untouched.begin(), untouched.end(), [](std::uint8_t v) { return v == 0; }) &&
                std::all_of(touched.begin(), touched.end(), [](std::uint8_t v) { return v == 0; }) &&
                sparse.IsSparse() && !sparse.IsMaterialized();
        });
    worker.join();
    Expect(sparse.HasStorage() && sparse.IsSparse() && !sparse.IsMaterialized(),
           "100mp-empty-sparse-source-created-without-full-image");
    Expect(boundedMetadata, "100mp-preview-holds-only-metadata-and-one-complete-touched-tile");
    Expect(completeSparseRead, "100mp-reader-reads-far-and-touched-transparent-pixels-without-materializing-source");
}
} // namespace

int RunWarpPreviewReaderTests()
{
    gFailures = 0;
    gChecks = 0;
    FactoryAndPlacedReads();
    SamplingAndRefusals();
    SmudgeSnapshots();
    PreviewFactoryRefusals();
    CrossingTileDirectory();
    LargeSparsePreview();
    std::printf("SUMMARY warp-preview-reader checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

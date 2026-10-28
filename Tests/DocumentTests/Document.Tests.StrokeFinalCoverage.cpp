#include "Document.Tests.StrokeFinalCoverage.h"

#include "BrushStroke.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s strokefinal.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster SolidSource()
{
    constexpr int side = 32;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(side * side * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 50;
        pixels[i + 1] = 60;
        pixels[i + 2] = 70;
        pixels[i + 3] = 255;
    }
    return Raster::Make(side, side, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster CenterCoverage64()
{
    constexpr int side = 32;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(side * side), 255);
    coverage[static_cast<std::size_t>(16 * side + 16)] = 64;
    return Raster::Make(side, side, RasterFormat::Gray8, coverage.data(), coverage.size());
}

ImageLayer SourceLayer(const Raster& source)
{
    ImageLayer layer = ImageLayer::Blank(L"Final coverage fixture", Vec2{32, 32});
    layer.asset = RasterAsset{source, Raster::MakeThumbnail(source), layer.name};
    return layer;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Fixture final
{
    Raster source = SolidSource();
    ImageLayer layer = SourceLayer(source);
    BrushSettings settings{2000, 1, 1, 0, 0, 1};
    BrushStroke stroke{layer, false, settings, 32, 32, &CpuBrushCoverage::Shared()};

    [[nodiscard]] bool AppendFullCanvasPath()
    {
        return stroke.Append(Vec2{0, 16}) && stroke.Append(Vec2{16, 16}) && stroke.Append(Vec2{32, 16});
    }
};
#pragma warning(pop)

struct WorkerResult final
{
    std::optional<BrushStroke::FinalCoverage> final;
    std::optional<BrushStroke::Commit> snapshot;
    bool composed = false;
    bool cancelledSnapshotRefused = false;
    bool cancelledComposeRefused = false;
};

std::optional<BrushStroke::FinalCoverage> PrepareOnWorker(BrushStroke& stroke)
{
    std::optional<BrushStroke::FinalCoverage> result;
    std::thread worker([&] { result = stroke.PrepareFinalCoverage(); });
    worker.join();
    return result;
}

WorkerResult PrepareComposeOnWorker(BrushStroke& stroke, const BrushStroke::FinalCoverage& final)
{
    WorkerResult result;
    std::thread worker(
        [&]
        {
            result.composed = stroke.ComposeFinalCoverage(final);
            result.snapshot = stroke.PaintSnapshot();
        });
    worker.join();
    return result;
}

bool PixelEquals(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected) noexcept
{
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool AllPixelsEqual(const Raster& raster, const std::array<std::uint8_t, 4>& expected,
                    bool partialSelection = false) noexcept
{
    std::array<std::uint8_t, 32 * 32 * 4> pixels{};
    raster.ReadRegion(0, 0, 32, 32, pixels.data(), 32 * 4, 0);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            const std::array<std::uint8_t, 4> pixelExpected =
                partialSelection && x == 16 && y == 16 ? std::array<std::uint8_t, 4>{37, 45, 116, 255} : expected;
            for (std::size_t channel = 0; channel < pixelExpected.size(); ++channel)
            {
                const auto index = static_cast<std::size_t>((y * 32 + x) * 4) + channel;
                if (pixels[index] != pixelExpected[channel])
                {
                    std::printf("DIAG strokefinal pixel=(%d,%d) channel=%zu actual=%u expected=%u\n", x, y, channel,
                                static_cast<unsigned int>(pixels[index]),
                                static_cast<unsigned int>(pixelExpected[channel]));
                    return false;
                }
            }
        }
    return true;
}

bool SourcePixelIsOriginal(const Raster& raster) noexcept
{
    return AllPixelsEqual(raster, {50, 60, 70, 255});
}

void DetachPrepareAndFullCoverage()
{
    Fixture fixture;
    const bool appended = fixture.AppendFullCanvasPath();
    BrushCoverageEngine* const detached = fixture.stroke.DetachCoverageForFinish();
    Expect(appended, "full.path-three-points-appended");
    Expect(detached == &CpuBrushCoverage::Shared(), "detach.returns-borrowed-cpu-engine");
    Expect(!fixture.stroke.HasCoverageEngineForTest(), "detach-removes-engine-from-stroke");
    Expect(fixture.stroke.DetachCoverageForFinish() == nullptr, "detach.second-transfer-refused");
    Expect(!fixture.stroke.Append(Vec2{8, 8}), "detach.append-refused");
    Expect(!fixture.stroke.Flush(), "detach.flush-refused");
    Expect(!fixture.stroke.PaintSnapshot().has_value(), "prepare.snapshot-refused-before-final-compose");

    std::optional<BrushStroke::FinalCoverage> final = PrepareOnWorker(fixture.stroke);
    Expect(final.has_value(), "prepare.cpu-final-coverage-created-on-worker");
    if (!final)
        return;
    Expect(!final.value().needsGpu && !final.value().tiles.empty(), "prepare.cpu-path-has-final-tiles-without-gpu");

    std::vector<std::uint32_t> coverageValues(
        static_cast<std::size_t>(final.value().tiles.front().width * final.value().tiles.front().height), 255);
    bool cpuInstallRefused = false;
    std::thread installWorker(
        [&]
        { cpuInstallRefused = !fixture.stroke.InstallFinalCoverage(final.value().tiles.front().key, coverageValues); });
    installWorker.join();
    Expect(cpuInstallRefused, "install.cpu-coverage-rejects-gpu-install");

    WorkerResult worker = PrepareComposeOnWorker(fixture.stroke, final.value());
    Expect(worker.composed, "compose.accepts-prepared-cpu-coverage-on-worker");
    Expect(worker.snapshot.has_value(), "compose-enables-paint-snapshot");
    if (!worker.snapshot)
        return;

    const Raster& committed = worker.snapshot.value().image;
    Expect(committed.IsSparse(), "commit-remains-sparse");
    Expect(!committed.IsMaterialized(), "commit-is-unmaterialized-before-read");
    Expect(PixelEquals(committed, 16, 16, {0, 0, 255, 255}), "analytic.full-coverage-literal-bgra");
    Expect(AllPixelsEqual(committed, {0, 0, 255, 255}), "analytic.full-tile-literal-bgra");
    bool repeatedComposeRefused = false;
    std::thread repeatedWorker([&] { repeatedComposeRefused = !fixture.stroke.ComposeFinalCoverage(final.value()); });
    repeatedWorker.join();
    Expect(repeatedComposeRefused, "compose.second-completion-refused");
    Expect(!committed.IsMaterialized(), "commit-read-does-not-materialize-raster");
    Expect(SourcePixelIsOriginal(fixture.source), "analytic.original-source-remains-literal-bgra");
}

void SelectedPartialCoverage()
{
    Fixture fixture;
    const Raster selection = CenterCoverage64();
    fixture.stroke.SetSelectionClip(selection);
    const bool appended = fixture.AppendFullCanvasPath();
    Expect(selection.IsMaterialized() && !selection.IsSparse(), "partial.selection-is-plain-contiguous-raster");
    Expect(appended, "partial.path-three-points-appended");
    BrushCoverageEngine* const engine = fixture.stroke.DetachCoverageForFinish();
    Expect(engine == &CpuBrushCoverage::Shared(), "partial.detach-succeeds");
    std::optional<BrushStroke::FinalCoverage> final = PrepareOnWorker(fixture.stroke);
    Expect(final.has_value() && final.value().tiles.size() == 1 && !final.value().needsGpu,
           "partial.cpu-final-preparation");
    if (!final)
        return;

    const WorkerResult worker = PrepareComposeOnWorker(fixture.stroke, final.value());
    Expect(worker.composed && worker.snapshot.has_value(), "partial.final-compose-and-snapshot");
    if (worker.snapshot)
    {
        Expect(PixelEquals(worker.snapshot.value().image, 16, 16, {37, 45, 116, 255}),
               "analytic.selection-coverage-64-literal-bgra");
        Expect(AllPixelsEqual(worker.snapshot.value().image, {0, 0, 255, 255}, true),
               "analytic.selection-full-tile-literal-bgra");
        Expect(worker.snapshot.value().image.IsSparse() && !worker.snapshot.value().image.IsMaterialized(),
               "partial.commit-remains-sparse-and-unmaterialized");
    }
}

void PreparationIsExclusive()
{
    Fixture fixture;
    Expect(fixture.AppendFullCanvasPath(), "exclusive.path-appended");
    Expect(fixture.stroke.DetachCoverageForFinish() != nullptr, "exclusive.detach-succeeds");
    std::optional<BrushStroke::FinalCoverage> first = PrepareOnWorker(fixture.stroke);
    Expect(first.has_value(), "exclusive.first-prepare-succeeds");
    if (!first)
        return;

    std::optional<BrushStroke::FinalCoverage> second;
    std::thread worker([&] { second = fixture.stroke.PrepareFinalCoverage(); });
    worker.join();
    Expect(!second.has_value(), "exclusive.second-prepare-refused");
}

void InvalidFinalListsAreRejected()
{
    Fixture fixture;
    Expect(fixture.AppendFullCanvasPath(), "metadata.path-appended");
    Expect(fixture.stroke.DetachCoverageForFinish() != nullptr, "metadata.detach-succeeds");
    std::optional<BrushStroke::FinalCoverage> final = PrepareOnWorker(fixture.stroke);
    Expect(final.has_value() && final.value().tiles.size() == 1, "metadata.one-tile-final-prepared");
    if (!final || final.value().tiles.empty())
        return;

    std::array<bool, 6> refused{};
    bool invalidSnapshotRefused = false;
    WorkerResult worker;
    std::thread composeWorker(
        [&]
        {
            auto duplicate = final.value();
            duplicate.tiles.push_back(duplicate.tiles.front());
            refused[0] = !fixture.stroke.ComposeFinalCoverage(duplicate);

            auto foreign = final.value();
            ++foreign.tiles.front().key;
            refused[1] = !fixture.stroke.ComposeFinalCoverage(foreign);

            auto missing = final.value();
            missing.tiles.clear();
            refused[2] = !fixture.stroke.ComposeFinalCoverage(missing);

            auto wrongX = final.value();
            ++wrongX.tiles.front().x;
            refused[3] = !fixture.stroke.ComposeFinalCoverage(wrongX);

            auto wrongWidth = final.value();
            ++wrongWidth.tiles.front().width;
            refused[4] = !fixture.stroke.ComposeFinalCoverage(wrongWidth);

            auto wrongPreview = final.value();
            std::array<std::uint8_t, 1> foreignPreview{};
            wrongPreview.tiles.front().preview = foreignPreview.data();
            refused[5] = !fixture.stroke.ComposeFinalCoverage(wrongPreview);
            invalidSnapshotRefused = !fixture.stroke.PaintSnapshot().has_value();
            worker.composed = fixture.stroke.ComposeFinalCoverage(final.value());
            worker.snapshot = fixture.stroke.PaintSnapshot();
        });
    composeWorker.join();
    Expect(refused[0], "metadata.duplicate-final-entry-refused");
    Expect(refused[1], "metadata.foreign-final-key-refused");
    Expect(refused[2], "metadata.missing-final-entry-refused");
    Expect(refused[3], "metadata.wrong-origin-refused");
    Expect(refused[4], "metadata.wrong-dimensions-refused");
    Expect(refused[5], "metadata.foreign-preview-buffer-refused");
    Expect(invalidSnapshotRefused, "metadata.invalid-lists-expose-no-snapshot");
    Expect(worker.composed && worker.snapshot.has_value(), "metadata.valid-list-still-composes-after-rejections");
}

void CancellationNeverPublishesSnapshot()
{
    Fixture before;
    Expect(before.AppendFullCanvasPath(), "cancel-before.path-appended");
    Expect(before.stroke.DetachCoverageForFinish() != nullptr, "cancel-before.detach-succeeds");
    CancelSource beforeCancellation;
    static_cast<void>(beforeCancellation.Cancel());
    std::optional<BrushStroke::FinalCoverage> cancelledBefore;
    bool snapshotBeforeRefused = false;
    std::thread beforeWorker(
        [&]
        {
            cancelledBefore = before.stroke.PrepareFinalCoverage(beforeCancellation.Token());
            snapshotBeforeRefused = !before.stroke.PaintSnapshot().has_value();
        });
    beforeWorker.join();
    Expect(!cancelledBefore.has_value(), "cancel-before.prepare-refused");
    Expect(snapshotBeforeRefused, "cancel-before.snapshot-unavailable");

    Fixture after;
    after.stroke.SetSelectionClip(CenterCoverage64());
    Expect(after.AppendFullCanvasPath(), "cancel-after.path-appended");
    Expect(after.stroke.DetachCoverageForFinish() != nullptr, "cancel-after.detach-succeeds");
    CancelSource afterCancellation;
    WorkerResult result;
    std::thread afterWorker(
        [&]
        {
            result.final = after.stroke.PrepareFinalCoverage(afterCancellation.Token());
            if (result.final)
            {
                static_cast<void>(afterCancellation.Cancel());
                result.cancelledComposeRefused = !after.stroke.ComposeFinalCoverage(result.final.value());
                result.cancelledSnapshotRefused = !after.stroke.PaintSnapshot().has_value();
            }
        });
    afterWorker.join();
    Expect(result.final.has_value(), "cancel-after.prepare-completes-before-cancel");
    Expect(result.cancelledComposeRefused, "cancel-after.compose-refused");
    Expect(result.cancelledSnapshotRefused, "cancel-after.snapshot-unavailable");
    Expect(SourcePixelIsOriginal(before.source), "cancel-before.original-full-tile-unchanged");
    Expect(SourcePixelIsOriginal(after.source), "cancel-after.original-full-tile-unchanged");
}
// P1 r3: private worker healing handoff controls.
// Independent constant-surface reference for Document_Tools.md's retouch contract:
// C = premultiplied BGRA (128,128,128,255). A copied patch has zero tone difference;
// a texture fill has constant membrane mean C and zero detail variance. Thus
// out = C + (C - C) * (coverage / 255 * opacity) = C, exactly, in all three modes.
// The legacy SpotHealingTests.healsTheBlemishUnderTheBrushAndNothingElse and its
// existing DocumentTests blemish pixels/ranges remain unchanged.
constexpr std::array<std::uint8_t, 4> kHealingGray{128, 128, 128, 255};
constexpr std::array<std::array<int, 2>, 6> kHealingOutside{{
    {11, 16},
    {21, 16},
    {16, 11},
    {16, 21},
    {0, 0},
    {31, 31},
}};

struct HealingModeCase final
{
    SpotHealingMode mode;
    const char* name;
};
constexpr std::array<HealingModeCase, 3> kHealingModes{{
    {SpotHealingMode::ContentAware, "content-aware"},
    {SpotHealingMode::CreateTexture, "create-texture"},
    {SpotHealingMode::ProximityMatch, "proximity-match"},
}};

void ExpectHealing(bool condition, const char* mode, const char* name) noexcept
{
    std::printf("%s strokefinal.heal.%s.%s\n", condition ? "PASS" : "FAIL", mode, name);
    gFailures += condition ? 0 : 1;
}

Raster HealingGraySource()
{
    std::array<std::uint8_t, 32 * 32 * 4> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += kHealingGray.size())
        for (std::size_t channel = 0; channel < kHealingGray.size(); ++channel)
            pixels[i + channel] = kHealingGray[channel];
    return Raster::Make(32, 32, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct HealingFixture final
{
    Raster source = HealingGraySource();
    ImageLayer layer = SourceLayer(source);
    BrushSettings settings{6, 1, 0, 0, 0, 1};
    BrushStroke stroke;

    explicit HealingFixture(SpotHealingMode mode, BrushKind kind = BrushKind::Heal)
        : stroke(layer, false, settings, 32, 32, &CpuBrushCoverage::Shared(), kind, mode)
    {
    }

    [[nodiscard]] bool AppendCenterDab()
    {
        return stroke.Append(Vec2{16, 16}) && stroke.Append(Vec2{16.5, 16});
    }
};
#pragma warning(pop)

bool HealingCoverageEquals(const BrushStroke& stroke, int x, int y, std::uint8_t expected) noexcept
{
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        if (tile.rect.Intersect(GridRect{x, y, 1, 1}).Empty())
            continue;
        const auto index = static_cast<std::size_t>(y - tile.rect.y) * static_cast<std::size_t>(tile.rect.width) +
                           static_cast<std::size_t>(x - tile.rect.x);
        return index < tile.coverage.size() && tile.coverage[index] == expected;
    }
    return false;
}

bool HealingOutsideUncovered(const BrushStroke& stroke) noexcept
{
    for (const auto& point : kHealingOutside)
        if (!HealingCoverageEquals(stroke, point[0], point[1], 0))
            return false;
    return true;
}

bool HealingOutsideIsGray(const Raster& image) noexcept
{
    for (const auto& point : kHealingOutside)
        if (!PixelEquals(image, point[0], point[1], kHealingGray))
            return false;
    return true;
}

bool SameHealingFixturePixels(const Raster& before, const Raster& after) noexcept
{
    if (before.Width() != 32 || before.Height() != 32 || after.Width() != 32 || after.Height() != 32 ||
        before.Format() != RasterFormat::Bgra8888 || after.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, 32 * 32 * 4> beforePixels{};
    std::array<std::uint8_t, 32 * 32 * 4> afterPixels{};
    before.ReadRegion(0, 0, 32, 32, beforePixels.data(), 32 * 4, 0);
    after.ReadRegion(0, 0, 32, 32, afterPixels.data(), 32 * 4, 0);
    return beforePixels == afterPixels;
}

void DetachedHealingRestoresUniformGray()
{
    for (const auto& mode : kHealingModes)
    {
        HealingFixture fixture(mode.mode);
        ExpectHealing(fixture.AppendCenterDab(), mode.name, "path-appended");
        // Detach is owner-only; every subsequent finish operation is private worker work.
        BrushCoverageEngine* const engine = fixture.stroke.DetachCoverageForFinish();
        ExpectHealing(engine == &CpuBrushCoverage::Shared(), mode.name, "detach-returns-cpu-engine");
        ExpectHealing(!fixture.stroke.HasCoverageEngineForTest(), mode.name, "detached-draft-has-no-engine");

        bool preparedCpu = false;
        bool composed = false;
        bool centerCovered = false;
        bool outsideUncovered = false;
        bool healed = false;
        std::optional<BrushStroke::Commit> wash;
        std::optional<BrushStroke::Commit> snapshot;
        std::thread worker(
            [&]
            {
                const auto final = fixture.stroke.PrepareFinalCoverage();
                if (!final.has_value())
                    return;
                preparedCpu = !final.value().needsGpu && !final.value().tiles.empty();
                composed = fixture.stroke.ComposeFinalCoverage(final.value());
                if (!composed)
                    return;
                centerCovered = HealingCoverageEquals(fixture.stroke, 16, 16, 255);
                outsideUncovered = HealingOutsideUncovered(fixture.stroke);
                wash = fixture.stroke.PaintSnapshot();
                healed = fixture.stroke.HealForFinish(0x1234U);
                snapshot = fixture.stroke.PaintSnapshot();
            });
        worker.join();

        ExpectHealing(preparedCpu, mode.name, "nonempty-cpu-final-prepared");
        ExpectHealing(composed, mode.name, "final-composed-on-worker");
        ExpectHealing(centerCovered, mode.name, "center-coverage-255");
        ExpectHealing(outsideUncovered, mode.name, "outside-neighborhood-coverage-zero");
        ExpectHealing(wash.has_value() && !PixelEquals(wash.value().image, 16, 16, kHealingGray), mode.name,
                      "pre-heal-wash-differs-from-gray128");
        ExpectHealing(wash.has_value() && HealingOutsideIsGray(wash.value().image), mode.name,
                      "pre-heal-outside-neighborhood-stays-gray128");
        ExpectHealing(healed, mode.name, "heal-for-finish-completes-on-worker");
        ExpectHealing(snapshot.has_value(), mode.name, "healed-snapshot-available");
        ExpectHealing(snapshot.has_value() && snapshot.value().image.Width() == 32 &&
                          snapshot.value().image.Height() == 32 && AllPixelsEqual(snapshot.value().image, kHealingGray),
                      mode.name, "analytic-whole-image-exact-gray128");
        ExpectHealing(AllPixelsEqual(fixture.source, kHealingGray), mode.name, "original-whole-image-unchanged");
    }
}

void PaintHealingRefusalPreservesSnapshot()
{
    HealingFixture fixture(SpotHealingMode::ContentAware, BrushKind::Paint);
    Expect(fixture.AppendCenterDab(), "heal.paint.path-appended");
    Expect(fixture.stroke.DetachCoverageForFinish() == &CpuBrushCoverage::Shared(), "heal.paint.detach-succeeds");
    bool preparedCpu = false;
    bool composed = false;
    bool refused = false;
    std::optional<BrushStroke::Commit> before;
    std::optional<BrushStroke::Commit> after;
    std::thread worker(
        [&]
        {
            const auto final = fixture.stroke.PrepareFinalCoverage();
            if (!final.has_value())
                return;
            preparedCpu = !final.value().needsGpu && !final.value().tiles.empty();
            composed = fixture.stroke.ComposeFinalCoverage(final.value());
            if (!composed)
                return;
            before = fixture.stroke.PaintSnapshot();
            refused = !fixture.stroke.HealForFinish(0x1234U);
            after = fixture.stroke.PaintSnapshot();
        });
    worker.join();

    Expect(preparedCpu, "heal.paint.nonempty-cpu-final-prepared");
    Expect(composed, "heal.paint.final-composed-on-worker");
    Expect(before.has_value(), "heal.paint.snapshot-valid-before-heal-refusal");
    Expect(before.has_value() && PixelEquals(before.value().image, 16, 16, {0, 0, 0, 255}),
           "heal.paint.analytic-painted-center-is-opaque-black");
    Expect(before.has_value() && HealingOutsideIsGray(before.value().image),
           "heal.paint.outside-neighborhood-stays-gray128");
    Expect(refused, "heal.paint.heal-for-finish-refused");
    Expect(after.has_value(), "heal.paint.snapshot-valid-after-heal-refusal");
    Expect(before.has_value() && after.has_value() && before.value().bounds == after.value().bounds &&
               SameHealingFixturePixels(before.value().image, after.value().image),
           "heal.paint.refusal-preserves-bounds-and-all-snapshot-pixels");
    Expect(AllPixelsEqual(fixture.source, kHealingGray), "heal.paint.original-whole-image-unchanged");
}

void CancelledHealingRefusesSnapshot()
{
    for (const auto& mode : kHealingModes)
    {
        HealingFixture fixture(mode.mode);
        ExpectHealing(fixture.AppendCenterDab(), mode.name, "cancel.path-appended");
        ExpectHealing(fixture.stroke.DetachCoverageForFinish() == &CpuBrushCoverage::Shared(), mode.name,
                      "cancel.detach-succeeds");
        CancelSource cancellation;
        bool preparedCpu = false;
        bool composed = false;
        bool washValid = false;
        bool washDiffers = false;
        bool cancellationRequested = false;
        bool healingRefused = false;
        bool snapshotRefused = false;
        std::thread worker(
            [&]
            {
                const auto final = fixture.stroke.PrepareFinalCoverage(cancellation.Token());
                if (!final.has_value())
                    return;
                preparedCpu = !final.value().needsGpu && !final.value().tiles.empty();
                composed = fixture.stroke.ComposeFinalCoverage(final.value());
                if (!composed)
                    return;
                // Observe the legitimate pre-cancel wash privately; return no snapshot from this cancelled draft.
                const auto wash = fixture.stroke.PaintSnapshot();
                washValid = wash.has_value();
                washDiffers = wash.has_value() && !PixelEquals(wash.value().image, 16, 16, kHealingGray);
                cancellationRequested = cancellation.Cancel();
                healingRefused = !fixture.stroke.HealForFinish(0x1234U);
                snapshotRefused = !fixture.stroke.PaintSnapshot().has_value();
            });
        worker.join();

        ExpectHealing(preparedCpu, mode.name, "cancel.nonempty-cpu-final-prepared");
        ExpectHealing(composed, mode.name, "cancel.final-composed-before-cancel");
        ExpectHealing(washValid, mode.name, "cancel.snapshot-valid-before-cancel");
        ExpectHealing(washDiffers, mode.name, "cancel.pre-cancel-wash-differs-from-gray128");
        ExpectHealing(cancellationRequested, mode.name, "cancel.requested-after-compose");
        ExpectHealing(healingRefused, mode.name, "cancel.heal-for-finish-refused");
        ExpectHealing(snapshotRefused, mode.name, "cancel.snapshot-unavailable");
        ExpectHealing(AllPixelsEqual(fixture.source, kHealingGray), mode.name, "cancel.original-whole-image-unchanged");
    }
}
// End P1 r3 additions.
} // namespace

int RunStrokeFinalCoverageTests()
{
    gFailures = 0;
    DetachPrepareAndFullCoverage();
    SelectedPartialCoverage();
    PreparationIsExclusive();
    InvalidFinalListsAreRejected();
    CancellationNeverPublishesSnapshot();
    DetachedHealingRestoresUniformGray();
    PaintHealingRefusalPreservesSnapshot();
    CancelledHealingRefusesSnapshot();
    return gFailures;
}

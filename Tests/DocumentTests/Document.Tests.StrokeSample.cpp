#include "Document.Tests.StrokeSample.h"

#include "BrushStroke.h"
#include "Document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s stroke-sample.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster PatternBgra(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            const auto alpha = static_cast<std::uint8_t>(96 + ((x * 11 + y * 7) % 160));
            pixels[i] = static_cast<std::uint8_t>((x * 3 + y * 5) % (alpha + 1));
            pixels[i + 1] = static_cast<std::uint8_t>((x * 7 + y * 2) % (alpha + 1));
            pixels[i + 2] = static_cast<std::uint8_t>((x * 2 + y * 9) % (alpha + 1));
            pixels[i + 3] = alpha;
        }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster PatternGray(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((x * 13 + y * 17) & 255);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

Raster SelectionCoverage()
{
    constexpr int width = 520;
    constexpr int height = 300;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height, 0);
    for (int y = 30; y < 270; ++y)
        for (int x = 40; x < 480; ++x)
            pixels[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint8_t>((x + 3 * y) % 7 == 0 ? 91 : 255);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

ImageLayer ImageFixture(int width, int height, bool withPixels)
{
    ImageLayer layer =
        ImageLayer::Blank(L"sample fixture", Vec2{static_cast<double>(width), static_cast<double>(height)});
    if (withPixels)
    {
        const Raster source = PatternBgra(width, height);
        layer.asset = RasterAsset{source, {}, layer.name};
    }
    return layer;
}

ImageLayer MaskFixture(int width, int height)
{
    ImageLayer layer = ImageFixture(width, height, true);
    const Raster mask = PatternGray(width, height);
    layer.mask = LayerMask{RasterAsset{mask, {}, L"sample mask"}, true, std::nullopt, false};
    return layer;
}

[[nodiscard]] bool SameRaster(const Raster& a, const Raster& b)
{
    if (a.Width() != b.Width() || a.Height() != b.Height() || a.Format() != b.Format() ||
        a.HasStorage() != b.HasStorage())
        return false;
    if (!a.HasStorage())
        return true;
    const auto rowBytes = static_cast<std::size_t>(a.Width()) * a.Channels();
    std::vector<std::uint8_t> rowA(rowBytes);
    std::vector<std::uint8_t> rowB(rowBytes);
    for (int y = 0; y < a.Height(); ++y)
    {
        a.ReadRegion(0, y, a.Width(), 1, rowA.data(), static_cast<int>(rowBytes), 0);
        b.ReadRegion(0, y, b.Width(), 1, rowB.data(), static_cast<int>(rowBytes), 0);
        if (rowA != rowB)
            return false;
    }
    return true;
}

[[nodiscard]] bool SameTileState(const BrushStroke& a, const BrushStroke& b)
{
    if (a.Width() != b.Width() || a.Height() != b.Height() || a.Tiles().size() != b.Tiles().size() ||
        a.PaintTransform() != b.PaintTransform())
        return false;
    for (std::size_t i = 0; i < a.Tiles().size(); ++i)
    {
        const BrushStroke::Tile& left = a.Tiles()[i];
        const BrushStroke::Tile& right = b.Tiles()[i];
        if (left.key != right.key || left.rect != right.rect || left.coverage != right.coverage ||
            left.permanent != right.permanent || left.touched != right.touched || !SameRaster(left.image, right.image))
            return false;
    }
    return true;
}

[[nodiscard]] bool SameCommit(const std::optional<BrushStroke::Commit>& a, const std::optional<BrushStroke::Commit>& b)
{
    return a.has_value() == b.has_value() &&
           (!a.has_value() || (a->bounds == b->bounds && a->transform == b->transform &&
                               SameRaster(a->image, b->image) && SameRaster(a->thumbnail, b->thumbnail)));
}

struct SampleCase final
{
    const char* name;
    int width;
    int height;
    bool mask;
    BrushKind kind;
    BrushSettings settings;
    bool selection;
    std::array<Vec2, 8> points;
    std::size_t count;
};

constexpr std::array<SampleCase, 5> kCases{{
    {"hard-crossing",
     520,
     300,
     false,
     BrushKind::Paint,
     BrushSettings{32, 1, 0.2, 0.8, 0.35, 1},
     false,
     {Vec2{180, 96}, Vec2{250, 96}, Vec2{320, 96}, Vec2{250, 96}, Vec2{250, 165}, Vec2{250, 165}},
     6},
    {"soft-opacity-selection",
     520,
     300,
     false,
     BrushKind::Paint,
     BrushSettings{44, 0.28, 0.9, 0.1, 0.4, 0.37},
     true,
     {Vec2{72, 72}, Vec2{172, 126}, Vec2{270, 64}, Vec2{360, 154}, Vec2{444, 91}},
     5},
    {"image-erase",
     320,
     180,
     false,
     BrushKind::Erase,
     BrushSettings{27, 0.72, 0.1, 0.4, 0.8, 0.63},
     false,
     {Vec2{45, 88}, Vec2{112, 42}, Vec2{185, 117}, Vec2{258, 54}, Vec2{300, 120}},
     5},
    {"mask-paint",
     320,
     180,
     true,
     BrushKind::Paint,
     BrushSettings{23, 1, 0.7, 0.25, 0.1, 0.51},
     true,
     {Vec2{24, 24}, Vec2{87, 58}, Vec2{164, 32}, Vec2{234, 91}, Vec2{291, 130}},
     5},
    {"soft-curved-boundary",
     520,
     300,
     false,
     BrushKind::Paint,
     BrushSettings{51, 0, 0.15, 0.5, 1, 0.8},
     false,
     {Vec2{202, 45}, Vec2{260, 121}, Vec2{310, 225}, Vec2{369, 166}, Vec2{432, 72}},
     5},
}};

[[nodiscard]] bool RunOneSample(BrushStroke& sampled, Vec2 point, const char* caseName, std::size_t ordinal,
                                std::uint64_t& previousSequence, bool expectEmpty = false)
{
    static_cast<void>(sampled.TakeChanged()); // Begin a per-sample changed-tile observation window on the owner.
    const auto plan = sampled.PlanSampleMemory(point);
    if (!plan.has_value())
    {
        std::printf("FAIL stroke-sample.%s.%zu.plan\n", caseName, ordinal);
        ++gFailures;
        return false;
    }
    if (plan->sequence != previousSequence + 1)
    {
        std::printf("FAIL stroke-sample.%s.%zu.sequence-not-next\n", caseName, ordinal);
        ++gFailures;
        return false;
    }
    const std::uint64_t beforeBytes = sampled.RetirementMemoryBytes();
    BrushCoverageEngine* const engine = sampled.DetachCoverageForSample(plan.value());
    if (engine == nullptr || sampled.HasCoverageEngineForTest())
    {
        std::printf("FAIL stroke-sample.%s.%zu.owner-detach\n", caseName, ordinal);
        ++gFailures;
        return false;
    }
    const bool detachedCallsRefused = !sampled.Append(point) && !sampled.Flush() &&
                                      !sampled.PaintSnapshot().has_value() &&
                                      sampled.DetachCoverageForFinish() == nullptr;

    std::optional<BrushStroke::SampleCoverage> sample;
    bool prepared = false;
    bool composed = false;
    bool cpuInstallRefused = false;
    std::thread worker(
        [&]
        {
            sample = sampled.PrepareSampleCoverage();
            prepared = sample.has_value();
            cpuInstallRefused = sample.has_value() && sample->tiles.empty();
            if (sample.has_value() && !sample->needsGpu)
            {
                if (!sample->tiles.empty())
                {
                    const auto& tile = sample->tiles.front();
                    std::vector<std::uint32_t> values(static_cast<std::size_t>(tile.width) * tile.height, 255u);
                    cpuInstallRefused = !sampled.InstallSampleCoverage(tile.key, values);
                }
                composed = sampled.ComposeSampleCoverage(sample.value());
            }
        });
    worker.join(); // `sample` and its borrowed tile spans stay alive through the complete worker interval.

    const bool reattached = sampled.AttachCoverageAfterSample(engine);
    const std::vector<std::size_t> changed = sampled.TakeChanged();
    const auto afterBytes = sampled.RetirementMemoryBytes();
    const bool planBounded = beforeBytes != 0 && plan->persistentCpuBytes >= beforeBytes && afterBytes != 0 &&
                             plan->persistentCpuBytes >= afterBytes && plan->maximumTiles >= sampled.Tiles().size() &&
                             changed.size() <= plan->maximumChangedTiles;
    const bool emptyMatches = !expectEmpty || (sample.has_value() && sample->settled.empty() && sample->tail.empty() &&
                                               sample->tiles.empty() && changed.empty());
    const bool result = prepared && sample.has_value() && sample->sequence == plan->sequence && !sample->needsGpu &&
                        composed && cpuInstallRefused && detachedCallsRefused && reattached && emptyMatches &&
                        sampled.HasCoverageEngineForTest() && planBounded;
    if (!result)
    {
        std::printf("FAIL stroke-sample.%s.%zu.prepare-compose-reattach-bound\n", caseName, ordinal);
        ++gFailures;
    }
    return result;
}

void ScalarAppendEquivalence()
{
    for (const SampleCase& test : kCases)
    {
        ImageLayer layer =
            test.mask ? MaskFixture(test.width, test.height) : ImageFixture(test.width, test.height, true);
        ImageLayer scalarLayer = layer;
        BrushStroke scalar(layer, test.mask, test.settings, test.width, test.height, &CpuBrushCoverage::Shared(),
                           test.kind);
        BrushStroke sampled(scalarLayer, test.mask, test.settings, test.width, test.height, &CpuBrushCoverage::Shared(),
                            test.kind);
        if (test.selection)
        {
            const Raster selection = SelectionCoverage();
            scalar.SetSelectionClip(selection);
            sampled.SetSelectionClip(selection);
        }
        bool accepted = scalar.IsValid() && sampled.IsValid();
        std::size_t completed = 0;
        std::uint64_t previousSequence = 0;
        for (std::size_t i = 0; i < test.count && accepted; ++i)
        {
            accepted = scalar.Append(test.points[i]);
            if (!accepted)
                break;
            if (i != 0 && test.points[i] == test.points[i - 1])
            {
                accepted = RunOneSample(sampled, test.points[i], test.name, i, previousSequence, true);
                if (accepted)
                    ++previousSequence;
                ++completed;
                continue;
            }
            accepted = RunOneSample(sampled, test.points[i], test.name, i, previousSequence);
            if (accepted)
                ++previousSequence;
            ++completed;
            if (accepted)
                accepted = SameTileState(scalar, sampled);
        }
        const auto scalarSnapshot = scalar.PaintSnapshot();
        const auto sampleSnapshot = sampled.PaintSnapshot();
        Expect(accepted && completed == test.count, test.name);
        Expect(accepted && SameTileState(scalar, sampled), "scalar.tiles-placement-coverage-and-image-bytes-match");
        Expect(accepted && SameCommit(scalarSnapshot, sampleSnapshot),
               "scalar.commit-bytes-bounds-and-transform-match");
    }
}

class SyntheticGpuEngine final : public BrushCoverageEngine
{
  public:
    bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment>, std::span<const BrushSegment>,
                const BrushKernelParams&) noexcept override
    {
        ++renderCalls;
        for (const BrushKernelTile& tile : tiles)
            if (tile.preview != nullptr)
                std::fill_n(tile.preview, static_cast<std::size_t>(tile.width) * tile.height, std::uint8_t{0});
        return true;
    }
    void BeginStroke() noexcept override
    {
        ++beginCalls;
    }
    void EndStroke() noexcept override
    {
        ++endCalls;
    }
    bool UsesCpuPermanent() const noexcept override
    {
        ++usesCpuPermanentCalls;
        return false;
    }

    std::size_t beginCalls = 0;
    std::size_t endCalls = 0;
    std::size_t renderCalls = 0;
    mutable std::size_t usesCpuPermanentCalls = 0;
};

void SyntheticGpuCoverageProtocol()
{
    ImageLayer layer = ImageFixture(32, 32, true);
    SyntheticGpuEngine engine;
    BrushStroke stroke(layer, false, BrushSettings{8, 1, 0, 0, 1, 1}, 32, 32, &engine);
    const bool initial = stroke.IsValid() && stroke.Append(Vec2{16, 16});
    const std::size_t beginsBefore = engine.beginCalls;
    const std::size_t endsBefore = engine.endCalls;
    const std::size_t rendersBefore = engine.renderCalls;
    const std::size_t usesCpuPermanentBefore = engine.usesCpuPermanentCalls;
    const auto plan = stroke.PlanSampleMemory(Vec2{20, 16});
    BrushCoverageEngine* const detached = plan ? stroke.DetachCoverageForSample(plan.value()) : nullptr;
    Expect(initial && plan.has_value() && detached == &engine, "gpu.detach-returns-original-engine");
    if (!plan || detached != &engine)
        return;

    std::optional<BrushStroke::SampleCoverage> sample;
    bool prepared = false;
    std::array<bool, 5> rejected{};
    bool composed = false;
    std::vector<std::vector<std::uint32_t>> values;
    std::thread worker(
        [&]
        {
            sample = stroke.PrepareSampleCoverage();
            if (!sample || !sample->needsGpu || sample->tiles.empty())
                return;
            const auto firstKey = sample->tiles.front().key;
            const std::array<std::uint32_t, 1> wrongSize{7};
            rejected[2] = !stroke.InstallSampleCoverage(firstKey, wrongSize);
            const std::array<std::uint32_t, 1> wrongKey{7};
            rejected[3] = !stroke.InstallSampleCoverage(firstKey + 0x100000u, wrongKey);
            values.reserve(sample->tiles.size());
            for (const BrushKernelTile& tile : sample->tiles)
            {
                values.emplace_back(static_cast<std::size_t>(tile.width) * tile.height, 300u);
                auto& coverage = values.back();
                coverage.front() = 17u;
                if (coverage.size() > 1)
                    coverage[1] = 255u;
            }
            for (std::size_t i = 0; i < sample->tiles.size(); ++i)
            {
                const bool installed = stroke.InstallSampleCoverage(sample->tiles[i].key, values[i]);
                if (!installed)
                    return;
                if (i == 0)
                    rejected[4] = !stroke.InstallSampleCoverage(sample->tiles[i].key, values[i]);
            }
            // Validate sample identity only after all required keys have been acknowledged.
            auto stale = sample.value();
            ++stale.sequence;
            rejected[0] = !stroke.ComposeSampleCoverage(stale);
            auto pointerTamper = sample.value();
            std::array<std::uint8_t, 4> foreign{};
            pointerTamper.tiles.front().preview = foreign.data();
            rejected[1] = !stroke.ComposeSampleCoverage(pointerTamper);
            composed = stroke.ComposeSampleCoverage(sample.value());
            prepared = true;
        });
    worker.join();
    bool wrongOwnerRefused = false;
    std::thread wrongOwner([&] { wrongOwnerRefused = !stroke.AttachCoverageAfterSample(detached); });
    wrongOwner.join();
    const bool noEngineCallsOnWrongOwner = engine.beginCalls == beginsBefore && engine.endCalls == endsBefore &&
                                           engine.renderCalls == rendersBefore &&
                                           engine.usesCpuPermanentCalls == usesCpuPermanentBefore;
    SyntheticGpuEngine impostor;
    const bool foreignEngineRefused = !stroke.AttachCoverageAfterSample(&impostor);
    const bool attached = stroke.AttachCoverageAfterSample(detached);
    const bool stableEngine = engine.beginCalls == beginsBefore && engine.endCalls == endsBefore &&
                              engine.renderCalls == rendersBefore &&
                              engine.usesCpuPermanentCalls == usesCpuPermanentBefore + 1;
    bool clamped = sample.has_value() && !sample->tiles.empty();
    if (clamped)
    {
        const BrushKernelTile& first = sample->tiles.front();
        const auto* preview = first.preview;
        clamped = preview != nullptr && preview[0] == 17 && (first.width * first.height == 1 || preview[1] == 255);
        if (first.width * first.height > 2)
            clamped = clamped && preview[2] == 255;
    }
    Expect(prepared && sample.has_value() && sample->needsGpu && composed, "gpu.exact-key-install-and-compose");
    Expect(std::all_of(rejected.begin(), rejected.end(), [](bool value) { return value; }),
           "gpu.stale-pointer-wrong-size-key-and-duplicate-refused");
    Expect(attached && stroke.HasCoverageEngineForTest() && stableEngine,
           "gpu.reattach-original-without-begin-end-or-render-reset");
    Expect(foreignEngineRefused, "gpu.foreign-engine-reattach-refused");
    Expect(wrongOwnerRefused && noEngineCallsOnWrongOwner,
           "gpu.wrong-owner-reattach-refused-without-calling-original-engine");
    Expect(clamped, "gpu.uint-coverage-clamps-to-255");
}

void EmptyIncompleteStaleAndCancellation()
{
    {
        ImageLayer layer = ImageFixture(48, 48, true);
        BrushStroke stroke(layer, false, BrushSettings{10, 0.6, 0.4, 0.2, 0.8, 0.5}, 48, 48,
                           &CpuBrushCoverage::Shared());
        Expect(stroke.Append(Vec2{12, 12}), "stale.setup-first-sample");
        const auto stale = stroke.PlanSampleMemory(Vec2{24, 12});
        Expect(stale.has_value(), "stale.plan-captured");
        BrushCoverageEngine* engine = stale ? stroke.DetachCoverageForSample(stale.value()) : nullptr;
        std::optional<BrushStroke::SampleCoverage> first;
        bool prepared = false;
        bool composed = false;
        std::thread worker(
            [&]
            {
                first = stroke.PrepareSampleCoverage();
                prepared = first.has_value();
                composed = first && !first->needsGpu && stroke.ComposeSampleCoverage(first.value());
            });
        worker.join();
        const bool attached = stroke.AttachCoverageAfterSample(engine);
        const auto newer = stroke.PlanSampleMemory(Vec2{36, 18});
        const bool staleRefused =
            stale && stroke.DetachCoverageForSample(stale.value()) == nullptr && stroke.HasCoverageEngineForTest();
        Expect(prepared && composed && attached && newer.has_value() && staleRefused,
               "stale.old-sequence-plan-refused-after-newer-sample");
    }
    {
        ImageLayer layer = ImageFixture(520, 300, true);
        SyntheticGpuEngine engine;
        BrushStroke stroke(layer, false, BrushSettings{18, 1, 0, 0, 1, 1}, 520, 300, &engine);
        Expect(stroke.Append(Vec2{248, 150}), "incomplete.setup-first-sample");
        const auto plan = stroke.PlanSampleMemory(Vec2{280, 150});
        BrushCoverageEngine* detached = plan ? stroke.DetachCoverageForSample(plan.value()) : nullptr;
        std::optional<BrushStroke::SampleCoverage> sample;
        bool incompleteRefused = false;
        bool completed = false;
        std::thread worker(
            [&]
            {
                sample = stroke.PrepareSampleCoverage();
                if (!sample || !sample->needsGpu || sample->tiles.size() < 2)
                    return;
                incompleteRefused = !stroke.ComposeSampleCoverage(sample.value());
                for (const BrushKernelTile& tile : sample->tiles)
                {
                    std::vector<std::uint32_t> values(static_cast<std::size_t>(tile.width) * tile.height, 64u);
                    if (!stroke.InstallSampleCoverage(tile.key, values))
                        return;
                }
                completed = stroke.ComposeSampleCoverage(sample.value());
            });
        worker.join();
        const bool attached = stroke.AttachCoverageAfterSample(detached);
        Expect(incompleteRefused && completed && attached,
               "incomplete.gpu-compose-refuses-prefix-then-completes-exact-set");
    }
    {
        ImageLayer layer = ImageFixture(520, 300, true);
        SyntheticGpuEngine engine;
        BrushStroke stroke(layer, false, BrushSettings{18, 1, 0, 0, 1, 1}, 520, 300, &engine);
        Expect(stroke.Append(Vec2{248, 150}), "cancel-prefix.setup-first-sample");
        const auto plan = stroke.PlanSampleMemory(Vec2{280, 150});
        BrushCoverageEngine* detached = plan ? stroke.DetachCoverageForSample(plan.value()) : nullptr;
        CancelSource cancel;
        const CancelToken cancellation = cancel.Token();
        std::optional<BrushStroke::SampleCoverage> sample;
        bool prefixInstalled = false;
        bool composeRefused = false;
        bool snapshotRefused = false;
        std::thread worker(
            [&]
            {
                sample = stroke.PrepareSampleCoverage(cancellation);
                if (!sample || !sample->needsGpu || sample->tiles.size() < 2)
                    return;
                const auto& tile = sample->tiles.front();
                std::vector<std::uint32_t> values(static_cast<std::size_t>(tile.width) * tile.height, 91u);
                prefixInstalled = stroke.InstallSampleCoverage(tile.key, values);
                static_cast<void>(cancel.Cancel());
                composeRefused = !stroke.ComposeSampleCoverage(sample.value());
                snapshotRefused = !stroke.PaintSnapshot().has_value();
            });
        worker.join();
        const bool attachRefused = !stroke.AttachCoverageAfterSample(detached);
        if (detached != nullptr)
            detached->EndStroke();
        Expect(sample.has_value() && prefixInstalled && composeRefused && snapshotRefused && attachRefused,
               "cancel-prefix.partial-uint-install-cannot-compose-attach-or-snapshot");
    }
    {
        ImageLayer layer = ImageFixture(64, 64, true);
        BrushStroke stroke(layer, false, BrushSettings{10, 0.5, 0, 0, 0, 1}, 64, 64, &CpuBrushCoverage::Shared());
        Expect(stroke.Append(Vec2{16, 16}), "cancel-before.setup-first-sample");
        const auto plan = stroke.PlanSampleMemory(Vec2{30, 18});
        BrushCoverageEngine* engine = plan ? stroke.DetachCoverageForSample(plan.value()) : nullptr;
        CancelSource cancel;
        static_cast<void>(cancel.Cancel());
        std::optional<BrushStroke::SampleCoverage> sample;
        bool snapshotRefused = false;
        std::thread worker(
            [&]
            {
                sample = stroke.PrepareSampleCoverage(cancel.Token());
                snapshotRefused = !stroke.PaintSnapshot().has_value();
            });
        worker.join();
        const bool attachRefused = !stroke.AttachCoverageAfterSample(engine);
        if (engine != nullptr)
            engine->EndStroke(); // Owner discards the original engine after joined failed work.
        Expect(!sample.has_value() && snapshotRefused && attachRefused,
               "cancel-before.prepare-and-publication-refused-without-prefix");
    }
    {
        ImageLayer layer = ImageFixture(64, 64, true);
        BrushStroke stroke(layer, false, BrushSettings{10, 0.5, 0, 0, 0, 1}, 64, 64, &CpuBrushCoverage::Shared());
        Expect(stroke.Append(Vec2{16, 16}), "cancel-after.setup-first-sample");
        const auto plan = stroke.PlanSampleMemory(Vec2{30, 18});
        BrushCoverageEngine* engine = plan ? stroke.DetachCoverageForSample(plan.value()) : nullptr;
        CancelSource cancel;
        std::optional<BrushStroke::SampleCoverage> sample;
        bool composeRefused = false;
        bool snapshotRefused = false;
        std::thread worker(
            [&]
            {
                sample = stroke.PrepareSampleCoverage(cancel.Token());
                if (sample)
                    static_cast<void>(cancel.Cancel());
                composeRefused = !sample || !stroke.ComposeSampleCoverage(sample.value());
                snapshotRefused = !stroke.PaintSnapshot().has_value();
            });
        worker.join();
        const bool attachRefused = !stroke.AttachCoverageAfterSample(engine);
        if (engine != nullptr)
            engine->EndStroke();
        Expect(sample.has_value() && composeRefused && snapshotRefused && attachRefused,
               "cancel-after.partial-sample-compose-attach-and-snapshot-refused");
    }
}

void SamplePlannerRefusals()
{
    {
        ImageLayer layer = ImageFixture(80, 60, true);
        BrushStroke stroke(layer, false, BrushSettings{12, 0.5, 0, 0, 0, 1}, 80, 60, &CpuBrushCoverage::Shared());
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        const bool refused = !stroke.PlanSampleMemory(Vec2{nan, 20}).has_value() &&
                             !stroke.PlanSampleMemory(Vec2{20, infinity}).has_value();
        Expect(stroke.IsValid() && refused && stroke.Tiles().empty(),
               "planner.invalid-nonfinite-points-refused-without-tile-allocation");
    }
    {
        ImageLayer layer = ImageFixture(520, 300, true);
        BrushStroke stroke(layer, false, BrushSettings{18, 0.5, 0, 0, 0, 1}, 520, 300, &CpuBrushCoverage::Shared());
        const auto supported = stroke.PlanSampleMemory(Vec2{260, 150});
        stroke.SetPixelLimit(1);
        const auto overLimit = stroke.PlanSampleMemory(Vec2{260, 150});
        Expect(supported.has_value() && !overLimit.has_value() && stroke.Tiles().empty(),
               "planner.projected-pixel-limit-refused-before-transfer-or-allocation");
    }
}
} // namespace

int RunDocumentStrokeSampleTests()
{
    gFailures = 0;
    ScalarAppendEquivalence();
    SyntheticGpuCoverageProtocol();
    EmptyIncompleteStaleAndCancellation();
    SamplePlannerRefusals();
    return gFailures;
}

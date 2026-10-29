#include "Document.Tests.StrokeOpening.h"

#include "Executor.h"
#include "Selection.h"
#include "StrokeOpeningWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s stroke-opening.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 8;
constexpr std::array<std::uint8_t, 4> kSourcePixel{20, 40, 60, 255};

Raster LiteralImage()
{
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)) * 4u;
            bytes[offset] = static_cast<std::uint8_t>(20 + x);
            bytes[offset + 1] = static_cast<std::uint8_t>(40 + y);
            bytes[offset + 2] = static_cast<std::uint8_t>(60 + x + y);
            bytes[offset + 3] = 255;
        }
    }
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster UniformImage(std::array<std::uint8_t, 4> pixel)
{
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); i += pixel.size())
        for (std::size_t channel = 0; channel < pixel.size(); ++channel)
            bytes[i + channel] = pixel[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster UniformMask(std::uint8_t value)
{
    std::array<std::uint8_t, kSide * kSide> bytes{};
    bytes.fill(value);
    return Raster::Make(kSide, kSide, RasterFormat::Gray8, bytes.data(), bytes.size());
}

CanvasDocument FixtureDocument(const Raster& image, const Raster& mask = {})
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Stroke opening fixture", Vec2{kSide, kSide});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    if (mask.HasStorage())
    {
        LayerMask layerMask;
        layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Literal mask"};
        layer.mask = std::move(layerMask);
    }
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    DocumentSession session;
    Guid active;

    explicit Fixture(const Raster& image, const Raster& mask = {})
    {
        CanvasDocument document = FixtureDocument(image, mask);
        active = document.layers.front().id;
        session.ReplaceDocument(std::move(document), active);
        session.SetBrushEngine(&CpuBrushCoverage::Shared());
    }
};

StrokeOpeningOptions PaintOptions(BrushSettings settings = BrushSettings{2000, 1, 1, 0, 0, 1})
{
    StrokeOpeningOptions options;
    options.settings = settings;
    options.kind = BrushKind::Paint;
    return options;
}

struct WorkerResult final
{
    std::optional<StrokeOpeningResult> result;
    bool joined = false;

    WorkerResult() = default;
    WorkerResult(const WorkerResult&) = delete;
    WorkerResult& operator=(const WorkerResult&) = delete;
    WorkerResult(WorkerResult&&) noexcept = default;
};

WorkerResult PrepareDetached(StrokeOpeningInput input, CancelToken cancellation = {},
                             StrokeOpeningObserver observer = {})
{
    WorkerResult work;
    std::thread worker(
        [&work, input = std::move(input), cancellation, observer]() mutable
        {
            work.result.emplace(
                DocumentSession::PrepareStrokeOpeningPublication(std::move(input), cancellation, observer));
        });
    worker.join();
    work.joined = true;
    return work;
}

bool ReadBgra(const Raster& raster, int x, int y, std::array<std::uint8_t, 4>& pixel) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    raster.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return true;
}

bool ReadGray(const Raster& raster, int x, int y, std::uint8_t& pixel) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    raster.ReadRegion(x, y, 1, 1, &pixel, 1, 0);
    return true;
}

bool PaintAndFinish(DocumentSession& session)
{
    if (session.PendingBrush() == nullptr)
        return false;
    session.ContinueBrush(Vec2{3, 3});
    session.FinishBrush();
    return session.PendingBrush() == nullptr;
}

bool PixelsEqual(const Raster& raster, const std::array<std::uint8_t, 4>& expected) noexcept
{
    std::array<std::uint8_t, 4> actual{};
    return ReadBgra(raster, 3, 3, actual) && actual == expected;
}

bool PrepareApply(Fixture& fixture, StrokeOpeningOptions options)
{
    auto input = fixture.session.PrepareStrokeOpeningInput(std::move(options));
    if (!input)
        return false;
    WorkerResult work = PrepareDetached(std::move(input.value()));
    return work.joined && work.result && work.result->status == StrokeOpeningStatus::Ready && work.result->prepared &&
           fixture.session.IsPreparedStrokeOpeningCurrent(work.result->prepared.value()) &&
           fixture.session.ApplyPreparedStrokeOpeningIfCurrent(work.result->prepared.value());
}

void TypeAndAuthorityStates()
{
    static_assert(!std::is_copy_constructible_v<StrokeOpeningInput>);
    static_assert(std::is_nothrow_move_constructible_v<StrokeOpeningInput>);
    static_assert(!std::is_copy_constructible_v<PreparedStrokeOpening>);
    static_assert(std::is_nothrow_move_constructible_v<PreparedStrokeOpening>);

    const Raster source = UniformImage(kSourcePixel);
    Fixture fixture(source);
    const auto revision = fixture.session.history.CurrentRevision();
    const auto selectionBefore = fixture.session.Selection();
    const CanvasDocument* documentBefore = fixture.session.Document() ? &fixture.session.Document().value() : nullptr;
    const ImageLayer* layerBefore = fixture.session.ActiveLayer();
    const int widthBefore = documentBefore ? documentBefore->width : 0;
    const int heightBefore = documentBefore ? documentBefore->height : 0;
    const auto activeBefore = fixture.session.ActiveLayerID();
    auto input = fixture.session.PrepareStrokeOpeningInput(PaintOptions());
    auto secondInput = fixture.session.PrepareStrokeOpeningInput(PaintOptions());
    Expect(input.has_value() && secondInput.has_value(), "capture.accepts-valid-current-owner");
    Expect(documentBefore && layerBefore && layerBefore->asset && fixture.session.PendingBrush() == nullptr &&
               fixture.session.history.CurrentRevision() == revision &&
               fixture.session.Selection() == selectionBefore &&
               layerBefore->asset->image.Identity() == source.Identity() && fixture.session.Document() &&
               fixture.session.Document()->width == widthBefore && fixture.session.Document()->height == heightBefore &&
               fixture.session.ActiveLayerID() == activeBefore,
           "capture-preserves-source-header-selection-and-history");
    if (!input || !secondInput)
        return;

    WorkerResult work = PrepareDetached(std::move(input.value()));
    WorkerResult secondWork = PrepareDetached(std::move(secondInput.value()));
    Expect(work.joined && work.result && work.result->status == StrokeOpeningStatus::Ready && work.result->prepared,
           "worker.returns-prepared-typed-state-after-join");
    if (!work.result || !work.result->prepared)
        return;
    PreparedStrokeOpening& prepared = work.result->prepared.value();
    Expect(fixture.session.IsPreparedStrokeOpeningCurrent(prepared), "owner.recognizes-current-publication");
    Expect(fixture.session.ApplyPreparedStrokeOpeningIfCurrent(prepared) && fixture.session.PendingBrush() != nullptr &&
               fixture.session.PendingBrushOpening().has_value() &&
               fixture.session.PendingBrush()->RetirementMemoryBytes() != 0,
           "owner.apply-opens-one-live-stroke-without-a-sample");
    Expect(fixture.session.PendingBrush() &&
               fixture.session.PendingBrush()->ExpectedFinishCoverageEngine() == &CpuBrushCoverage::Shared(),
           "owner.worker-built-live-draft-exposes-exact-finish-engine");
    Expect(secondWork.result && secondWork.result->prepared &&
               !fixture.session.IsPreparedStrokeOpeningCurrent(secondWork.result->prepared.value()) &&
               !fixture.session.ApplyPreparedStrokeOpeningIfCurrent(secondWork.result->prepared.value()),
           "owner.second-prepared-opening-cannot-replace-active-stroke");
    Expect(!fixture.session.IsPreparedStrokeOpeningCurrent(prepared) &&
               !fixture.session.ApplyPreparedStrokeOpeningIfCurrent(prepared),
           "owner.duplicate-apply-is-refused");
    const ImageLayer* afterApply = fixture.session.ActiveLayer();
    Expect(afterApply && afterApply->asset && afterApply->asset->image.Identity() == source.Identity() &&
               fixture.session.history.CurrentRevision() == revision,
           "apply-does-not-commit-before-a-pointer-sample");
    fixture.session.CancelBrush();
}

void CapturedOptionsAndEagerMaskSelection()
{
    {
        const Raster source = UniformImage(kSourcePixel);
        Fixture fixture(source);
        const BrushSettings captured{2000, 1, 1, 0, 0, 1};
        Expect(PrepareApply(fixture, PaintOptions(captured)), "captured-options.prepared-and-applied");
        fixture.session.brushSettings = BrushSettings{1, 1, 0, 1, 0, 0.01};
        const bool finished = PaintAndFinish(fixture.session);
        const ImageLayer* layer = fixture.session.ActiveLayer();
        Expect(finished && layer && layer->asset && PixelsEqual(layer->asset->image, {0, 0, 255, 255}),
               "captured-options.later-session-settings-do-not-replace-press-settings");
        Expect(fixture.session.ShiftLineStart() == std::optional(Vec2{3, 3}),
               "captured-options.first-worker-opened-sample-retains-shift-line-endpoint");
    }
    {
        const Raster source = UniformImage(kSourcePixel);
        const Raster mask = UniformMask(128);
        Fixture fixture(source, mask);
        fixture.session.SelectLayerTarget(fixture.active, true);
        fixture.session.SetSelection(
            DocumentSelection{false, SelectionLoops{{Vec2{2, 2}, Vec2{6, 2}, Vec2{6, 6}, Vec2{2, 6}}}},
            L"Literal opening clip");
        StrokeOpeningOptions options = PaintOptions(BrushSettings{2000, 1, 0.8, 0.4, 0.2, 1});
        options.maskWhite = false;
        Expect(PrepareApply(fixture, std::move(options)), "mask.capture-selection-and-apply");
        const bool finished = PaintAndFinish(fixture.session);
        const ImageLayer* layer = fixture.session.ActiveLayer();
        std::uint8_t inside = 0;
        std::uint8_t outside = 0;
        const bool sampled = layer && layer->mask && ReadGray(layer->mask->asset.image, 3, 3, inside) &&
                             ReadGray(layer->mask->asset.image, 0, 0, outside);
        Expect(finished && sampled && inside == 0 && outside == 128,
               "mask.eager-selection-coverage-clips-black-paint-to-selected-box");
        const bool endpoint = fixture.session.ShiftLineStart() == std::optional(Vec2{3, 3});
        fixture.session.SelectLayerTarget(fixture.active, false);
        Expect(endpoint && !fixture.session.ShiftLineStart(),
               "mask.worker-opened-endpoint-remains-bound-to-mask-target");
    }
}

void CloneCapturedSourceAndRefusals()
{
    const Raster source = LiteralImage();
    {
        Fixture fixture(source);
        StrokeOpeningOptions options = PaintOptions(BrushSettings{2000, 1, 0, 0, 0, 1});
        options.kind = BrushKind::Clone;
        options.cloneOffset = Vec2{2, 1};
        options.cloneSample = source;
        auto input = fixture.session.PrepareStrokeOpeningInput(std::move(options));
        Expect(input.has_value(), "clone.capture-fixed-offset-and-source");
        if (!input)
            return;
        WorkerResult work = PrepareDetached(std::move(input.value()));
        Expect(work.joined && work.result && work.result->status == StrokeOpeningStatus::Ready && work.result->prepared,
               "clone.detached-preparation-ready");
        if (!work.result || !work.result->prepared)
            return;
        fixture.session.cloneSource = Vec2{0, 0};
        fixture.session.cloneSettings.aligned = false;
        const bool applied = fixture.session.ApplyPreparedStrokeOpeningIfCurrent(work.result->prepared.value());
        Expect(applied, "clone.owner-applies-captured-input");
        if (!applied)
            return;
        fixture.session.ContinueBrush(Vec2{3, 3});
        fixture.session.FinishBrush();
        std::array<std::uint8_t, 4> pixel{};
        const ImageLayer* layer = fixture.session.ActiveLayer();
        const bool sampled = layer && layer->asset && ReadBgra(layer->asset->image, 3, 3, pixel);
        Expect(sampled && pixel == std::array<std::uint8_t, 4>{25, 44, 69, 255},
               "clone.event-captured-offset-plus-two-one-samples-source-five-four");
    }
    {
        Fixture fixture(source, UniformMask(128));
        fixture.session.SelectLayerTarget(fixture.active, true);
        StrokeOpeningOptions options = PaintOptions();
        options.kind = BrushKind::Clone;
        options.cloneOffset = Vec2{0, 0};
        options.cloneSample = source;
        const auto revision = fixture.session.history.CurrentRevision();
        const ImageLayer* before = fixture.session.ActiveLayer();
        const auto maskIdentity = before && before->mask ? before->mask->asset.image.Identity() : nullptr;
        const auto input = fixture.session.PrepareStrokeOpeningInput(std::move(options));
        const ImageLayer* after = fixture.session.ActiveLayer();
        std::uint8_t maskPixel = 0;
        const bool unchanged = !input && fixture.session.PendingBrush() == nullptr &&
                               fixture.session.history.CurrentRevision() == revision && after && after->mask &&
                               after->mask->asset.image.Identity() == maskIdentity &&
                               ReadGray(after->mask->asset.image, 3, 3, maskPixel) && maskPixel == 128;
        Expect(unchanged, "clone.mask-target-refusal-preserves-mask-history-and-owner-state");
    }
    constexpr std::array<const char*, 5> invalidNames{"nan-offset", "missing-offset", "missing-sample", "wrong-format",
                                                      "wrong-dimensions"};
    for (std::size_t invalid = 0; invalid < invalidNames.size(); ++invalid)
    {
        Fixture fixture(source);
        StrokeOpeningOptions options = PaintOptions();
        options.kind = BrushKind::Clone;
        options.cloneOffset = Vec2{0, 0};
        options.cloneSample = source;
        if (invalid == 0)
            options.cloneOffset = Vec2{std::numeric_limits<double>::quiet_NaN(), 0};
        else if (invalid == 1)
            options.cloneOffset.reset();
        else if (invalid == 2)
            options.cloneSample = {};
        else if (invalid == 3)
            options.cloneSample = Raster::Make(kSide, kSide, RasterFormat::Gray8);
        else
            options.cloneSample = Raster::Make(kSide - 1, kSide, RasterFormat::Bgra8888);
        char name[64]{};
        const auto formatted =
            std::format_to_n(name, sizeof(name) - 1, "clone.invalid-{}.refused", invalidNames[invalid]);
        *formatted.out = static_cast<char>(0);
        Expect(!fixture.session.PrepareStrokeOpeningInput(std::move(options)), name);
    }
}

void BlurFiniteEdgeLiteralReferences()
{
    {
        constexpr std::array<std::uint8_t, 4> sourcePixel{17, 91, 203, 255};
        // Independent finite-edge reference: sigma 4, radius 12, transparent outside the 8x8 source.
        // At (3,3), separable alpha coverage is
        // (sum(exp(-i*i/32), i=-3..4) / sum(exp(-i*i/32), i=-12..12))^2
        // = 0.46422698403749746; rounded premultiplied BGRA is [8,42,94,118].
        // The finite-edge blurred sample is [8,42,94,118]. Premultiplied source-over of that sample
        // onto [17,91,203,255] independently rounds back to [17,91,203,255], rather than replacing alpha.
        constexpr std::array<std::uint8_t, 4> expected{17, 91, 203, 255};
        Fixture fixture(UniformImage(sourcePixel));
        StrokeOpeningOptions options = PaintOptions(BrushSettings{40, 1, 0, 0, 0, 1});
        options.kind = BrushKind::Blur;
        const bool applied = PrepareApply(fixture, std::move(options));
        Expect(applied, "blur.finite-edge-image-opening-ready");
        const bool finished = PaintAndFinish(fixture.session);
        const ImageLayer* layer = fixture.session.ActiveLayer();
        const bool sampled = layer && layer->asset && PixelsEqual(layer->asset->image, expected);
        Expect(finished && sampled, "blur.finite-edge-bgra-literal-at-three-three");
    }
    {
        std::array<std::uint8_t, kSide * kSide * 4> impulse{};
        const std::size_t center = static_cast<std::size_t>(3 * kSide + 3) * 4;
        impulse[center + 2] = 255;
        impulse[center + 3] = 255;
        Fixture fixture(Raster::Make(kSide, kSide, RasterFormat::Bgra8888, impulse.data(), impulse.size()));
        auto options = PaintOptions(BrushSettings{40, 1, 0, 0, 0, 1});
        options.kind = BrushKind::Blur;
        const bool applied = PrepareApply(fixture, std::move(options));
        Expect(applied, "blur.impulse-image-opening-ready");
        const bool finished = PaintAndFinish(fixture.session);
        const auto* layer = fixture.session.ActiveLayer();
        std::array<std::uint8_t, 4> actual{};
        const bool sampled = layer && layer->asset && ReadBgra(layer->asset->image, 4, 3, actual);
        // Independent Gaussian taps: 255*exp(-1/32)/(sum(exp(-i*i/32), i=-12..12)^2)
        // rounds to 2 in red and alpha. The original pixel at (4,3) is transparent.
        constexpr std::array<std::uint8_t, 4> expected{0, 0, 2, 2};
        if (!sampled || actual != expected)
            std::printf("INFO stroke-opening.blur.impulse actual=[%u,%u,%u,%u] expected=[0,0,2,2]\n",
                        static_cast<unsigned int>(actual[0]), static_cast<unsigned int>(actual[1]),
                        static_cast<unsigned int>(actual[2]), static_cast<unsigned int>(actual[3]));
        Expect(applied && finished && sampled && actual == expected,
               "blur.impulse-neighbor-matches-independent-gaussian-and-source-over-literal");
    }
    {
        constexpr std::uint8_t expected = 173;
        Fixture fixture(UniformImage(kSourcePixel), UniformMask(expected));
        fixture.session.SelectLayerTarget(fixture.active, true);
        StrokeOpeningOptions options = PaintOptions(BrushSettings{40, 1, 0, 0, 0, 1});
        options.kind = BrushKind::Blur;
        Expect(PrepareApply(fixture, std::move(options)), "blur.mask-opening-ready");
        const bool finished = PaintAndFinish(fixture.session);
        const ImageLayer* layer = fixture.session.ActiveLayer();
        std::uint8_t actual = 0;
        const bool sampled = layer && layer->mask && ReadGray(layer->mask->asset.image, 3, 3, actual);
        Expect(finished && sampled && actual == expected, "blur.mask-clamps-finite-edge-and-preserves-literal-gray173");
    }
}

void HealModesAndMaskRefusal()
{
    constexpr std::array<SpotHealingMode, 3> modes{SpotHealingMode::ContentAware, SpotHealingMode::CreateTexture,
                                                   SpotHealingMode::ProximityMatch};
    constexpr std::array<const char*, 3> names{"content-aware", "create-texture", "proximity-match"};
    for (std::size_t i = 0; i < modes.size(); ++i)
    {
        Fixture fixture(LiteralImage());
        StrokeOpeningOptions options = PaintOptions();
        options.kind = BrushKind::Heal;
        options.healing = modes[i];
        const auto revision = fixture.session.history.CurrentRevision();
        const bool applied = PrepareApply(fixture, std::move(options));
        const bool opened =
            applied && fixture.session.PendingBrush() != nullptr && fixture.session.PendingBrushOpening().has_value();
        char openedName[64]{};
        const auto openedFormat = std::format_to_n(openedName, sizeof(openedName) - 1, "heal.{}.mode-opens", names[i]);
        *openedFormat.out = static_cast<char>(0);
        Expect(opened, openedName);
        const bool hadPendingStroke = fixture.session.PendingBrush() != nullptr;
        fixture.session.CancelBrush();
        const ImageLayer* layer = fixture.session.ActiveLayer();
        std::array<std::uint8_t, 4> pixel{};
        const bool unchanged = hadPendingStroke && fixture.session.PendingBrush() == nullptr && layer && layer->asset &&
                               ReadBgra(layer->asset->image, 3, 3, pixel) &&
                               pixel == std::array<std::uint8_t, 4>{23, 43, 66, 255} &&
                               fixture.session.history.CurrentRevision() == revision;
        char cancelName[80]{};
        const auto cancelFormat = std::format_to_n(cancelName, sizeof(cancelName) - 1,
                                                   "heal.{}.cancel-preserves-source-and-history", names[i]);
        *cancelFormat.out = static_cast<char>(0);
        Expect(unchanged, cancelName);
    }
    Fixture maskFixture(LiteralImage(), UniformMask(128));
    maskFixture.session.SelectLayerTarget(maskFixture.active, true);
    StrokeOpeningOptions maskHeal = PaintOptions();
    maskHeal.kind = BrushKind::Heal;
    const auto maskRevision = maskFixture.session.history.CurrentRevision();
    const ImageLayer* before = maskFixture.session.ActiveLayer();
    const auto maskIdentity = before && before->mask ? before->mask->asset.image.Identity() : nullptr;
    const auto maskInput = maskFixture.session.PrepareStrokeOpeningInput(std::move(maskHeal));
    const ImageLayer* after = maskFixture.session.ActiveLayer();
    std::uint8_t maskPixel = 0;
    Expect(!maskInput && maskFixture.session.PendingBrush() == nullptr &&
               maskFixture.session.history.CurrentRevision() == maskRevision && after && after->mask &&
               after->mask->asset.image.Identity() == maskIdentity &&
               ReadGray(after->mask->asset.image, 3, 3, maskPixel) && maskPixel == 128,
           "heal.mask-target-refusal-preserves-mask-history-and-owner-state");
}

void StalePublicationPreservesNewerOwnerState()
{
    enum class Mutation : std::uint8_t
    {
        Source,
        Edit,
        Selection,
        History,
    };
    constexpr std::array<Mutation, 4> mutations{Mutation::Source, Mutation::Edit, Mutation::Selection,
                                                Mutation::History};
    constexpr std::array<const char*, 4> labels{"source", "edit", "selection", "history"};
    for (std::size_t i = 0; i < mutations.size(); ++i)
    {
        const Raster source = UniformImage(kSourcePixel);
        Fixture fixture(source);
        auto input = fixture.session.PrepareStrokeOpeningInput(PaintOptions());
        if (!input)
        {
            Expect(false, "stale.capture-valid-input");
            continue;
        }
        WorkerResult work = PrepareDetached(std::move(input.value()));
        if (!work.result || !work.result->prepared)
        {
            Expect(false, "stale.worker-prepared-input");
            continue;
        }
        bool mutationApplied = false;
        switch (mutations[i])
        {
        case Mutation::Source:
        {
            CanvasDocument replacement = FixtureDocument(UniformImage({9, 8, 7, 255}));
            const Guid replacementActive = replacement.layers.front().id;
            fixture.session.ReplaceDocument(std::move(replacement), replacementActive);
            const ImageLayer* replacementLayer = fixture.session.ActiveLayer();
            std::array<std::uint8_t, 4> replacementPixel{};
            mutationApplied = replacementLayer && replacementLayer->asset &&
                              ReadBgra(replacementLayer->asset->image, 3, 3, replacementPixel) &&
                              replacementPixel == std::array<std::uint8_t, 4>{9, 8, 7, 255};
            break;
        }
        case Mutation::Edit:
        {
            const auto beforeEditRevision = fixture.session.history.CurrentRevision();
            fixture.session.brushSettings = BrushSettings{2000, 1, 0.8, 0.1, 0.2, 1};
            if (fixture.session.BeginBrush(Vec2{0, 0}))
            {
                fixture.session.ContinueBrush(Vec2{3, 3});
                fixture.session.FinishBrush();
                const ImageLayer* edited = fixture.session.ActiveLayer();
                std::array<std::uint8_t, 4> editedPixel{};
                mutationApplied = fixture.session.history.CurrentRevision() != beforeEditRevision && edited &&
                                  edited->asset && ReadBgra(edited->asset->image, 3, 3, editedPixel) &&
                                  editedPixel != kSourcePixel;
            }
            break;
        }
        case Mutation::Selection:
            fixture.session.SetSelection(
                DocumentSelection{false, SelectionLoops{{Vec2{1, 1}, Vec2{5, 1}, Vec2{5, 5}, Vec2{1, 5}}}},
                L"Newer selection");
            mutationApplied = fixture.session.Selection().has_value() &&
                              fixture.session.Selection()->loops.Get().size() == 1 &&
                              fixture.session.Selection()->loops.Get().front().size() == 4;
            break;
        case Mutation::History:
        {
            const auto beforeHistory = fixture.session.history.CaptureAuthority();
            fixture.session.AddBlankLayer();
            const bool added =
                !fixture.session.history.IsCurrent(beforeHistory) && fixture.session.history.UndoCount() == 1;
            fixture.session.Undo();
            // Undo deliberately restores the previous revision ID; its authority epoch must still change.
            mutationApplied = added && !fixture.session.history.IsCurrent(beforeHistory) &&
                              fixture.session.history.UndoCount() == 0 &&
                              fixture.session.history.CaptureState().FutureCount() == 1;
            break;
        }
        }
        const auto currentRevision = fixture.session.history.CurrentRevision();
        const int currentUndoCount = fixture.session.history.UndoCount();
        const CanvasDocument* currentDocument =
            fixture.session.Document() ? &fixture.session.Document().value() : nullptr;
        const auto currentDocumentIdentity = currentDocument ? currentDocument->layers.Identity() : nullptr;
        const bool stale =
            mutationApplied && !fixture.session.IsPreparedStrokeOpeningCurrent(work.result->prepared.value());
        const bool refused =
            mutationApplied && !fixture.session.ApplyPreparedStrokeOpeningIfCurrent(work.result->prepared.value());
        const bool preserved = fixture.session.PendingBrush() == nullptr &&
                               fixture.session.history.CurrentRevision() == currentRevision &&
                               fixture.session.history.UndoCount() == currentUndoCount && fixture.session.Document() &&
                               fixture.session.Document()->layers.Identity() == currentDocumentIdentity;
        char name[96]{};
        const auto formatted = std::format_to_n(
            name, sizeof(name) - 1, "stale.{}.change-refuses-and-preserves-newer-owner-transaction", labels[i]);
        *formatted.out = static_cast<char>(0);
        Expect(mutationApplied && stale && refused && preserved, name);
    }
}

struct CancelAtStage final
{
    CancelSource* source = nullptr;
    StrokeOpeningStage target = StrokeOpeningStage::Draft;
    bool observed = false;
};

void CancelCheckpoint(void* context, StrokeOpeningStage stage)
{
    auto& state = *static_cast<CancelAtStage*>(context);
    if (stage == state.target)
    {
        state.observed = true;
        static_cast<void>(state.source->Cancel());
    }
}

void CancellationStates()
{
    {
        Fixture fixture(UniformImage(kSourcePixel));
        auto input = fixture.session.PrepareStrokeOpeningInput(PaintOptions());
        CancelSource source;
        const bool cancelled = source.Cancel();
        std::optional<WorkerResult> work;
        if (input)
            work.emplace(PrepareDetached(std::move(input.value()), source.Token()));
        else
            Expect(false, "cancel.pre-cancelled-input-capture-succeeds");
        Expect(cancelled && work && work->joined && work->result &&
                   work->result->status == StrokeOpeningStatus::Cancelled && !work->result->prepared,
               "cancel.pre-cancelled-input-returns-cancelled-state");
        Expect(fixture.session.PendingBrush() == nullptr, "cancel.pre-cancel-does-not-open-owner-stroke");
    }
    constexpr std::array<StrokeOpeningStage, 4> stages{StrokeOpeningStage::Draft, StrokeOpeningStage::Selection,
                                                       StrokeOpeningStage::BlurSample, StrokeOpeningStage::Complete};
    constexpr std::array<const char*, 4> labels{"draft", "selection", "blur-sample", "complete"};
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        Fixture fixture(UniformImage(kSourcePixel));
        auto input = fixture.session.PrepareStrokeOpeningInput(PaintOptions());
        CancelSource source;
        CancelAtStage state{&source, stages[i], false};
        StrokeOpeningObserver observer{&CancelCheckpoint, &state};
        std::optional<WorkerResult> work;
        if (input)
            work.emplace(PrepareDetached(std::move(input.value()), source.Token(), observer));
        char name[96]{};
        const auto formatted =
            std::format_to_n(name, sizeof(name) - 1, "cancel.{}.stage-cancels-publication", labels[i]);
        *formatted.out = static_cast<char>(0);
        Expect(input.has_value() && state.observed && work && work->joined && work->result &&
                   work->result->status == StrokeOpeningStatus::Cancelled && !work->result->prepared &&
                   fixture.session.PendingBrush() == nullptr,
               name);
    }
}

void NonImageMaskTargets()
{
    constexpr std::array<const char*, 2> names{"folder", "adjustment"};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto document = DocumentFixture::Copy(FixtureDocument(UniformImage(kSourcePixel), UniformMask(128)));
        auto& layer = document.layers.front();
        layer.asset.reset();
        if (i == 0)
            layer.isGroup = true;
        else
            layer.adjustment = LayerAdjustment{};
        const Guid target = layer.id;
        Fixture fixture(UniformImage(kSourcePixel));
        fixture.session.ReplaceDocument(std::move(document).Seal(), target);
        fixture.session.SelectLayerTarget(target, true);
        char openingName[72]{};
        const auto openingFormat =
            std::format_to_n(openingName, sizeof(openingName) - 1, "mask.{}.opening-remains-supported", names[i]);
        *openingFormat.out = static_cast<char>(0);
        const bool applied = PrepareApply(fixture, PaintOptions());
        Expect(applied, openingName);
        const bool finished = PaintAndFinish(fixture.session);
        const auto* edited = fixture.session.ActiveLayer();
        std::uint8_t actual = 255;
        char pixelName[80]{};
        const auto pixelFormat =
            std::format_to_n(pixelName, sizeof(pixelName) - 1, "mask.{}.worker-opened-stroke-commits-black", names[i]);
        *pixelFormat.out = static_cast<char>(0);
        Expect(applied && finished && edited && edited->mask && ReadGray(edited->mask->asset.image, 3, 3, actual) &&
                   actual == 0,
               pixelName);
    }
    auto document = DocumentFixture::Copy(FixtureDocument(UniformImage(kSourcePixel), UniformMask(128)));
    document.layers.front().mask->enabled = false;
    const Guid target = document.layers.front().id;
    DocumentSession session;
    session.ReplaceDocument(std::move(document).Seal(), target);
    session.SelectLayerTarget(target, true);
    Expect(!session.PrepareStrokeOpeningInput(PaintOptions()) && !session.PendingBrush(),
           "mask.disabled-target-refuses-without-opening");
}

class CoverageLifetime final : public BrushCoverageEngine
{
  public:
    unsigned int begins = 0;
    unsigned int ends = 0;
    [[nodiscard]] bool Render(std::span<const BrushKernelTile>, std::span<const BrushSegment>,
                              std::span<const BrushSegment>, const BrushKernelParams&) noexcept override
    {
        return false;
    }
    void BeginStroke() noexcept override
    {
        ++begins;
    }
    void EndStroke() noexcept override
    {
        ++ends;
    }
};

struct DeferredDraft final
{
    DeferredReleaseNode* node = nullptr;
    unsigned int calls = 0;
    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& draft = *static_cast<DeferredDraft*>(context);
        ++draft.calls;
        draft.node = &node;
    }
};

void CompleteDraftRetirement()
{
    Fixture fixture(UniformImage(kSourcePixel));
    const auto* layer = fixture.session.ActiveLayer();
    std::shared_ptr<BrushStroke> stroke;
    std::thread producer(
        [&]
        {
            if (layer)
                stroke = BrushStroke::MakeForWork(*layer, false, PaintOptions().settings, kSide, kSide);
        });
    producer.join();
    Expect(stroke && stroke->IsValid(), "retirement.worker-constructs-private-draft");
    if (!stroke)
        return;
    CoverageLifetime engine;
    Expect(stroke->AttachCoverageForOpening(&engine) && engine.begins == 1 && engine.ends == 0,
           "retirement.owner-attaches-empty-graphics-borrow-once");
    auto* detached = stroke->DetachCoverageForRetirement();
    if (detached)
        detached->EndStroke();
    Expect(detached == &engine && !stroke->HasCoverageEngineForTest() && engine.ends == 1,
           "retirement.owner-detaches-and-ends-borrow-before-cpu-transfer");
    const auto bound = stroke->RetirementMemoryBytes();
    const std::weak_ptr<BrushStroke> weak = stroke;
    DeferredDraft draft;
    const bool installed = InstallDeferredReleaseSink(&DeferredDraft::Enqueue, &draft);
    stroke.reset();
    const bool cleared = installed && ClearDeferredReleaseSink(&draft);
    Expect(installed && cleared && weak.expired() && draft.calls == 1 && draft.node && draft.node->bytes >= bound &&
               draft.node->destroy,
           "retirement.small-whole-graph-defers-one-declared-owner-node");
    std::thread::id destroyed;
    if (draft.node)
    {
        std::thread consumer(
            [&]
            {
                destroyed = std::this_thread::get_id();
                draft.node->destroy(draft.node);
            });
        consumer.join();
        draft.node = nullptr;
    }
    Expect(destroyed != std::thread::id{} && destroyed != std::this_thread::get_id() && engine.ends == 1,
           "retirement.worker-destroys-graph-without-reentering-engine");
}

} // namespace

int RunStrokeOpeningTests()
{
    gFailures = 0;
    TypeAndAuthorityStates();
    CapturedOptionsAndEagerMaskSelection();
    CloneCapturedSourceAndRefusals();
    BlurFiniteEdgeLiteralReferences();
    HealModesAndMaskRefusal();
    StalePublicationPreservesNewerOwnerState();
    CancellationStates();
    NonImageMaskTargets();
    CompleteDraftRetirement();
    return gFailures;
}

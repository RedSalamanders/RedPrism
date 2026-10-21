#include "Document.Tests.HueSampleScalar.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Adjust.h"
#include "Document.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s huesamplescalar.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

std::unique_ptr<DocumentSession> MakeHueSession(const std::uint8_t* pixels, int width, int height)
{
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    const Raster image = Raster::Make(width, height, RasterFormat::Bgra8888, pixels, byteCount);
    if (!image.HasStorage())
        return {};
    auto session = std::make_unique<DocumentSession>();
    session->CreateDocument(width, height, true);
    if (!session->Document() || session->Document().value().layers.empty())
        return {};
    const Raster thumbnail = Raster::MakeThumbnail(image);
    if (!thumbnail.HasStorage())
        return {};
    EditLayerRecordsForTest(*session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{image, thumbnail, layer.name};
                            });
    if (session->ActiveLayer() == nullptr || !session->ActiveLayer()->asset.has_value())
        return {};
    return session;
}

bool SameTargetDrag(const std::optional<HueTargetDrag>& left, const std::optional<HueTargetDrag>& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    return !left.has_value() || (left.value().range == right.value().range && left.value().hue == right.value().hue &&
                                 left.value().saturation == right.value().saturation);
}

struct HueStateSnapshot final
{
    explicit HueStateSnapshot(const DocumentSession& session)
        : panel(session.PendingColorEdit().value()), globalSettings(session.filterSettings),
          document(session.Document().value()), historyCount(session.history.UndoCount())
    {
    }

    ColorEdit panel;
    FilterSettings globalSettings;
    CanvasDocument document;
    int historyCount;
};

std::unique_ptr<HueStateSnapshot> CaptureHueState(const DocumentSession& session)
{
    return std::make_unique<HueStateSnapshot>(session);
}

bool SameHuePanelState(const DocumentSession& session, const HueStateSnapshot& before)
{
    const std::optional<ColorEdit>& current = session.PendingColorEdit();
    return current.has_value() && current.value().kind == before.panel.kind &&
           current.value().settings == before.panel.settings && current.value().hueSample == before.panel.hueSample &&
           current.value().targeting == before.panel.targeting && current.value().preview == before.panel.preview &&
           current.value().previewSequence == before.panel.previewSequence &&
           current.value().previewInvalidatedSequence == before.panel.previewInvalidatedSequence &&
           current.value().original.Identity() == before.panel.original.Identity() &&
           current.value().previewRaster.Identity() == before.panel.previewRaster.Identity() &&
           SameTargetDrag(current.value().targetDrag, before.panel.targetDrag) &&
           session.filterSettings == before.globalSettings && session.Document().has_value() &&
           session.Document().value() == before.document && session.history.UndoCount() == before.historyCount;
}
void SampleModesMatchLegacyRanges()
{
    constexpr std::array<std::uint8_t, 8> redBlue{0, 0, 255, 255, 255, 0, 0, 255};
    auto session = MakeHueSession(redBlue.data(), 2, 1);
    Expect(session != nullptr, "fixture.red-blue");
    if (!session)
        return;

    session->BeginColorEdit(FilterKind::HueSaturation);
    const HueSaturationSettings initial = Adjust::HsvSettings(10, 0, 0, false, ColorRange::Greens);
    session->UpdateHueSaturation(initial, false);

    session->SetHueSampleMode(HueSampleMode::Replace);
    session->SampleHueRangeValue(0.0);
    const HueBand centeredOnRed = Adjust::BandOf(session->PendingColorEdit().value().settings.hsv);
    Expect(Adjust::BandWeight(centeredOnRed, 0) == 1 && Adjust::BandWeight(centeredOnRed, 120) == 0,
           "legacy.replace-centers-red");

    session->SetHueSampleMode(HueSampleMode::Add);
    session->SampleHueRangeValue(240.0);
    const HueBand includingBlue = Adjust::BandOf(session->PendingColorEdit().value().settings.hsv);
    Expect(Adjust::BandWeight(includingBlue, 240) == 1 && Adjust::BandWeight(includingBlue, 0) == 1,
           "legacy.add-includes-blue-and-keeps-red");

    session->SetHueSampleMode(HueSampleMode::Remove);
    session->SampleHueRangeValue(240.0);
    const HueBand excludingBlue = Adjust::BandOf(session->PendingColorEdit().value().settings.hsv);
    Expect(Adjust::BandWeight(excludingBlue, 240) == 0 && Adjust::BandWeight(excludingBlue, 0) == 1,
           "legacy.remove-excludes-blue-keeps-red");
    session->CancelColorEdit();
}

void InvalidScalarValuesArePureRefusals()
{
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    auto session = MakeHueSession(red.data(), 1, 1);
    Expect(session != nullptr, "fixture.invalid-values");
    if (!session)
        return;
    session->BeginColorEdit(FilterKind::HueSaturation);
    HueSaturationSettings settings;
    settings.range = ColorRange::Blues;
    settings.adjustments[static_cast<std::size_t>(ColorRange::Blues)] = RangeAdjustment{12, -7, 3};
    session->UpdateHueSaturation(settings, false);
    session->SetHueSampleMode(HueSampleMode::Replace);

    const std::array<std::optional<double>, 6> invalid{360.0,
                                                       -0.001,
                                                       std::numeric_limits<double>::quiet_NaN(),
                                                       std::numeric_limits<double>::infinity(),
                                                       -std::numeric_limits<double>::infinity(),
                                                       std::nullopt};
    constexpr std::array<const char*, 6> sampleLabels{
        "sample.360",    "sample.negative", "sample.nan", "sample.positive-infinity", "sample.negative-infinity",
        "sample.nullopt"};
    constexpr std::array<const char*, 6> targetRefusalLabels{"target.360.refuses",
                                                             "target.negative.refuses",
                                                             "target.nan.refuses",
                                                             "target.positive-infinity.refuses",
                                                             "target.negative-infinity.refuses",
                                                             "target.nullopt.refuses"};
    constexpr std::array<const char*, 6> targetStateLabels{"target.360.keeps-state",
                                                           "target.negative.keeps-state",
                                                           "target.nan.keeps-state",
                                                           "target.positive-infinity.keeps-state",
                                                           "target.negative-infinity.keeps-state",
                                                           "target.nullopt.keeps-state"};
    for (std::size_t index = 0; index < invalid.size(); ++index)
    {
        const std::unique_ptr<HueStateSnapshot> before = CaptureHueState(*session);
        session->SampleHueRangeValue(invalid[index]);
        Expect(SameHuePanelState(*session, *before), sampleLabels[index]);
    }

    session->SetHueTargeting(true);
    Expect(session->BeginHueTargetingValue(240.0), "targeting.establishes-prior-drag");
    const std::unique_ptr<HueStateSnapshot> withDrag = CaptureHueState(*session);
    for (std::size_t index = 0; index < invalid.size(); ++index)
    {
        Expect(!session->BeginHueTargetingValue(invalid[index]), targetRefusalLabels[index]);
        Expect(SameHuePanelState(*session, *withDrag), targetStateLabels[index]);
    }
    session->CancelColorEdit();
}

void ScalarGuardsKeepPanelState()
{
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    auto master = MakeHueSession(red.data(), 1, 1);
    Expect(master != nullptr, "fixture.guards");
    if (!master)
        return;
    master->BeginColorEdit(FilterKind::HueSaturation);
    master->SetHueSampleMode(HueSampleMode::Replace);
    const std::unique_ptr<HueStateSnapshot> masterBefore = CaptureHueState(*master);
    master->SampleHueRangeValue(240.0);
    Expect(SameHuePanelState(*master, *masterBefore), "sample.master-range-refuses");
    HueSaturationSettings colorized;
    colorized.colorize = true;
    colorized.range = ColorRange::Blues;
    master->UpdateHueSaturation(colorized, false);
    const std::unique_ptr<HueStateSnapshot> colorizeBefore = CaptureHueState(*master);
    master->SampleHueRangeValue(240.0);
    Expect(SameHuePanelState(*master, *colorizeBefore), "sample.colorize-refuses");
    master->SetHueTargeting(true);
    Expect(!master->BeginHueTargetingValue(240.0), "target.colorize-refuses");
    master->CancelColorEdit();

    auto disarmed = MakeHueSession(red.data(), 1, 1);
    Expect(disarmed != nullptr, "fixture.disarmed");
    if (!disarmed)
        return;
    disarmed->BeginColorEdit(FilterKind::HueSaturation);
    const std::unique_ptr<HueStateSnapshot> disarmedBefore = CaptureHueState(*disarmed);
    disarmed->SampleHueRangeValue(240.0);
    Expect(SameHuePanelState(*disarmed, *disarmedBefore), "sample.disarmed-refuses");
    Expect(!disarmed->BeginHueTargetingValue(240.0), "target.disarmed-refuses");
    Expect(SameHuePanelState(*disarmed, *disarmedBefore), "target.disarmed-keeps-panel");
    disarmed->CancelColorEdit();

    auto wrongKind = MakeHueSession(red.data(), 1, 1);
    Expect(wrongKind != nullptr, "fixture.wrong-kind");
    if (!wrongKind)
        return;
    wrongKind->BeginColorEdit(FilterKind::Levels);
    const std::unique_ptr<HueStateSnapshot> levelsBefore = CaptureHueState(*wrongKind);
    wrongKind->SampleHueRangeValue(240.0);
    Expect(!wrongKind->BeginHueTargetingValue(240.0), "wrong-kind.target-refuses");
    Expect(SameHuePanelState(*wrongKind, *levelsBefore), "wrong-kind.panel-unchanged");
    wrongKind->CancelColorEdit();

    auto closed = MakeHueSession(red.data(), 1, 1);
    Expect(closed != nullptr, "fixture.closed");
    if (!closed)
        return;
    const int closedHistory = closed->history.UndoCount();
    closed->SampleHueRangeValue(240.0);
    Expect(!closed->BeginHueTargetingValue(240.0) && !closed->HasColorEdit() &&
               closed->history.UndoCount() == closedHistory,
           "closed-panel-refuses");
}

void RasterWrappersMatchLiteralColorReferences()
{
    constexpr std::array<std::uint8_t, 8> redBlue{0, 0, 255, 255, 255, 0, 0, 255};
    auto scalar = MakeHueSession(redBlue.data(), 2, 1);
    auto raster = MakeHueSession(redBlue.data(), 2, 1);
    Expect(scalar != nullptr && raster != nullptr, "fixture.wrapper-equivalence");
    if (!scalar || !raster)
        return;
    scalar->BeginColorEdit(FilterKind::HueSaturation);
    raster->BeginColorEdit(FilterKind::HueSaturation);
    const HueSaturationSettings initial = Adjust::HsvSettings(10, 0, 0, false, ColorRange::Greens);
    scalar->UpdateHueSaturation(initial, false);
    raster->UpdateHueSaturation(initial, false);
    scalar->SetHueSampleMode(HueSampleMode::Replace);
    raster->SetHueSampleMode(HueSampleMode::Replace);
    scalar->SampleHueRangeValue(240.0); // Independent literal for the legacy blue half.
    raster->SampleHueRange(raster->ActiveLayer()->asset->image, Vec2{1.5, 0.5});
    const HueBand scalarBlue = Adjust::BandOf(scalar->PendingColorEdit().value().settings.hsv);
    const HueBand rasterBlue = Adjust::BandOf(raster->PendingColorEdit().value().settings.hsv);
    Expect(Adjust::BandWeight(scalarBlue, 240) == 1 && Adjust::BandWeight(scalarBlue, 0) == 0,
           "wrapper-reference.literal-blue-replacement");
    Expect(rasterBlue == scalarBlue, "wrapper.sample-blue-matches-scalar-literal");
    scalar->SampleHueRangeValue(0.0);
    raster->SampleHueRange(raster->ActiveLayer()->asset->image, Vec2{0.5, 0.5});
    const HueBand scalarRed = Adjust::BandOf(scalar->PendingColorEdit().value().settings.hsv);
    const HueBand rasterRed = Adjust::BandOf(raster->PendingColorEdit().value().settings.hsv);
    Expect(Adjust::BandWeight(scalarRed, 0) == 1 && Adjust::BandWeight(scalarRed, 120) == 0,
           "wrapper-reference.literal-red-replacement");
    Expect(rasterRed == scalarRed, "wrapper.sample-red-matches-scalar-literal");
    scalar->CancelColorEdit();
    raster->CancelColorEdit();

    auto scalarTarget = MakeHueSession(redBlue.data(), 2, 1);
    auto rasterTarget = MakeHueSession(redBlue.data(), 2, 1);
    Expect(scalarTarget != nullptr && rasterTarget != nullptr, "fixture.target-wrapper");
    if (!scalarTarget || !rasterTarget)
        return;
    scalarTarget->BeginColorEdit(FilterKind::HueSaturation);
    rasterTarget->BeginColorEdit(FilterKind::HueSaturation);
    scalarTarget->SetHueTargeting(true);
    rasterTarget->SetHueTargeting(true);
    const bool scalarBegan = scalarTarget->BeginHueTargetingValue(240.0);
    const bool rasterBegan = rasterTarget->BeginHueTargeting(rasterTarget->ActiveLayer()->asset->image, Vec2{1.5, 0.5});
    Expect(scalarBegan && rasterBegan &&
               scalarTarget->PendingColorEdit().value().settings.hsv.range == ColorRange::Blues,
           "legacy.targeting-blue-range");
    scalarTarget->DragHueTargeting(60, false);
    rasterTarget->DragHueTargeting(60, false);
    Expect(scalarTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 30 &&
               rasterTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 30,
           "legacy.targeting-plus-60-is-30-saturation");
    scalarTarget->DragHueTargeting(20, false);
    rasterTarget->DragHueTargeting(20, false);
    Expect(scalarTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 10 &&
               rasterTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .saturation == 10,
           "legacy.targeting-drag-is-measured-from-start");
    scalarTarget->DragHueTargeting(-40, true);
    rasterTarget->DragHueTargeting(-40, true);
    Expect(scalarTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .hue == -20 &&
               rasterTarget->PendingColorEdit()
                       .value()
                       .settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                       .hue == -20,
           "legacy.targeting-minus-40-is-minus-20-hue");
    scalarTarget->CancelColorEdit();
    rasterTarget->CancelColorEdit();
}

void RasterSamplingRefusalsDoNotChangeThePanel()
{
    constexpr std::array<std::uint8_t, 20> pixels{
        0,   0,   255, 255, // Opaque red.
        0,   0,   0,   0,   // Transparent.
        128, 128, 128, 255, // Neutral gray.
        128, 128, 130, 255, // RGB(130,128,128), below the legacy 2% saturation threshold.
        0,   0,   128, 128  // Half-alpha premultiplied red.
    };
    auto session = MakeHueSession(pixels.data(), 5, 1);
    Expect(session != nullptr, "fixture.raster-refusals");
    if (!session)
        return;
    const Raster& raster = session->ActiveLayer()->asset->image;
    Expect(DocumentSession::SampledHue(raster, Vec2{0.5, 0.5}) == std::optional<double>{0.0},
           "legacy.sample-red-is-zero");
    Expect(!DocumentSession::SampledHue(raster, Vec2{1.5, 0.5}).has_value(), "legacy.sample-transparent-is-null");
    Expect(!DocumentSession::SampledHue(raster, Vec2{2.5, 0.5}).has_value(), "legacy.sample-gray-is-null");
    Expect(!DocumentSession::SampledHue(raster, Vec2{3.5, 0.5}).has_value(), "legacy.sample-near-gray-is-null");
    Expect(DocumentSession::SampledHue(raster, Vec2{4.5, 0.5}) == std::optional<double>{0.0},
           "legacy.sample-premultiplied-half-alpha-unpremultiplies");
    Expect(!DocumentSession::SampledHue(raster, Vec2{-0.01, 0.5}).has_value() &&
               !DocumentSession::SampledHue(raster, Vec2{5.0, 0.5}).has_value() &&
               !DocumentSession::SampledHue(raster, Vec2{std::numeric_limits<double>::quiet_NaN(), 0.5}).has_value(),
           "legacy.sample-outside-and-nonfinite-are-null");

    session->BeginColorEdit(FilterKind::HueSaturation);
    HueSaturationSettings settings;
    settings.range = ColorRange::Reds;
    session->UpdateHueSaturation(settings, false);
    session->SetHueSampleMode(HueSampleMode::Replace);
    const std::unique_ptr<HueStateSnapshot> before = CaptureHueState(*session);
    session->SampleHueRange(raster, Vec2{1.5, 0.5});
    session->SampleHueRange(raster, Vec2{2.5, 0.5});
    session->SampleHueRange(raster, Vec2{3.5, 0.5});
    session->SampleHueRange(raster, Vec2{-0.01, 0.5});
    session->SampleHueRange(raster, Vec2{5.0, 0.5});
    Expect(SameHuePanelState(*session, *before), "wrapper-refusals-preserve-panel-and-history");
    session->CancelColorEdit();
}

} // namespace

int RunHueSampleScalarTests()
{
    gFailures = 0;
    SampleModesMatchLegacyRanges();
    InvalidScalarValuesArePureRefusals();
    ScalarGuardsKeepPanelState();
    RasterWrappersMatchLiteralColorReferences();
    RasterSamplingRefusalsDoNotChangeThePanel();
    return gFailures;
}

#include "Document.Tests.AdjustmentNested.h"

#include "AdjustmentClosureWork.h"
#include "AdjustmentOpeningWork.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s adjustment-nested.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourceBytes{0,   0,   0,   255, 32,  64,  96,  255,
                                                    128, 160, 192, 255, 255, 224, 192, 255};
constexpr wchar_t kOuterName[] = L"Outer batch: preserve this name";

Raster MakeSource()
{
    return Raster::Make(2, 2, RasterFormat::Bgra8888, kSourceBytes.data(), kSourceBytes.size());
}

bool HasLiteralSource(const Raster& raster)
{
    if (!raster.HasStorage() || raster.Width() != 2 || raster.Height() != 2 ||
        raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kSourceBytes.size()> bytes{};
    raster.ReadRegion(0, 0, 2, 2, bytes.data(), 8, 0);
    return bytes == kSourceBytes;
}

LayerAdjustment OriginalAdjustment()
{
    LayerAdjustment result;
    result.kind = AdjustmentKind::Levels;
    result.levels.channel = LevelsChannel::Red;
    result.levels.ranges[0] = LevelRange{5, 1.2, 245, 3, 250};
    return result;
}

FilterSettings LiteralGlobalTemplate()
{
    FilterSettings result;
    result.radius = 7.25;
    result.angle = -18;
    result.distance = 83;
    result.amount = 33;
    result.gaussian = true;
    result.monochromatic = true;
    result.distortion = -12.5;
    result.exposure = ExposureSettings{-0.5, 0.125, 1.7};
    result.gradientMap = GradientMapSettings{{0.1, 0.2, 0.3}, {0.8, 0.7, 0.6}, true};
    result.grain = GrainSettings{12, 1.25, 66, 0x24681357u};
    return result;
}

FilterSettings LiteralEditedSettings()
{
    FilterSettings result;
    result.levels.channel = LevelsChannel::Blue;
    result.levels.ranges[2] = LevelRange{11, 1.7, 238, 6, 247};
    return result;
}

LayerAdjustment LiteralEditedAdjustment()
{
    LayerAdjustment result;
    result.kind = AdjustmentKind::Levels;
    result.levels.channel = LevelsChannel::Blue;
    result.levels.ranges[2] = LevelRange{11, 1.7, 238, 6, 247};
    return result;
}

struct Fixture final
{
    Fixture() : original(OriginalAdjustment()), templateSettings(LiteralGlobalTemplate()), source(MakeSource())
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 2;
        document.height = 2;

        ImageLayer pixels = ImageLayer::Blank(L"Source layer", Vec2{2, 2});
        sourceID = pixels.id;
        pixels.asset = RasterAsset{source, Raster::MakeThumbnail(source), pixels.name};
        ImageLayer adjustment = ImageLayer::Blank(L"Adjustment layer", Vec2{2, 2});
        adjustmentID = adjustment.id;
        adjustment.adjustment = original;
        document.layers.push_back(std::move(pixels));
        document.layers.push_back(std::move(adjustment));

        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        session.filterSettings = templateSettings;

        session.BeginEdit(L"Redo seed entry");
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& current) { current.layers[0].name = L"Redo seed after"; });
        session.EndEdit();
        session.Undo();
        outerBefore = session.Document();
        session.SetAdjustmentEditingID(adjustmentID);
    }

    LayerAdjustment original;
    FilterSettings templateSettings;
    Raster source;
    Guid sourceID;
    Guid adjustmentID;
    DocumentSession session;
    std::optional<CanvasDocument> outerBefore;
};

std::optional<PreparedAdjustmentOpening> PrepareOpening(Fixture& fixture)
{
    auto input = fixture.session.PrepareAdjustmentOpeningInput();
    if (!input)
        return std::nullopt;
    const auto source = fixture.session.Document();
    return DocumentSession::PrepareAdjustmentOpeningPublication(std::move(input.value()), source, fixture.source,
                                                                fixture.session.history.CaptureState());
}

std::optional<PreparedAdjustmentClosure> PrepareClosure(Fixture& fixture, bool commit)
{
    auto input = fixture.session.PrepareAdjustmentClosureInput(commit);
    if (!input)
        return std::nullopt;
    const auto source = fixture.session.Document();
    return DocumentSession::PrepareAdjustmentClosurePublication(std::move(input.value()), source,
                                                                fixture.session.history.CaptureState());
}

bool HasAdjustment(const std::optional<CanvasDocument>& document, const Guid& id, const LayerAdjustment& expected)
{
    if (!document)
        return false;
    for (const ImageLayer& layer : document->layers)
        if (layer.id == id)
            return layer.adjustment == std::optional(expected);
    return false;
}

void NamedOuterCommit(bool preview)
{
    Fixture fixture;
    fixture.session.BeginEdit(kOuterName);
    fixture.session.EditForTest([](CanvasDocument& document) { document.resolution = 144.0; });
    const auto beforeOpening = fixture.session.Document();
    const std::uint64_t generationBeforeOpen = fixture.session.Generation();
    const Guid revisionBeforeOpen = fixture.session.history.CurrentRevision();
    const auto preOpenHistory = fixture.session.history.CaptureState();
    const std::string prefix = preview ? "commit.preview-on" : "commit.preview-off";

    Expect(beforeOpening && beforeOpening != fixture.outerBefore && preOpenHistory.Depth() == 1 &&
               preOpenHistory.PastCount() == 0 && preOpenHistory.FutureCount() == 1 &&
               preOpenHistory.RedoName() == L"Redo seed entry",
           (prefix + ".outer-depth-name-and-seeded-redo").c_str());
    Expect(fixture.session.PrepareAdjustmentOpeningInput().has_value(), (prefix + ".opening-input-captured").c_str());
    auto opening = PrepareOpening(fixture);
    Expect(opening.has_value(), (prefix + ".opening-publication-prepared").c_str());
    if (!opening)
        return;
    Expect(fixture.session.IsPreparedAdjustmentOpeningCurrent(opening.value()) &&
               fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(opening.value()),
           (prefix + ".opening-applied").c_str());
    auto nested = fixture.session.history.CaptureState();
    Expect(nested.Depth() == 2 && nested.PastCount() == 0 && nested.FutureCount() == 1 &&
               nested.RedoName() == L"Redo seed entry" && fixture.session.Generation() == generationBeforeOpen &&
               fixture.session.history.CurrentRevision() == revisionBeforeOpen,
           (prefix + ".opening-nests-without-entry-or-generation-change").c_str());
    Expect(fixture.session.Document() == beforeOpening && fixture.session.filterSettings == fixture.templateSettings &&
               fixture.session.AdjustmentOriginal() == std::optional(fixture.original) &&
               fixture.session.PendingColorEdit() && fixture.session.PendingColorEdit()->deferred &&
               HasLiteralSource(fixture.session.PendingColorEdit()->original),
           (prefix + ".literal-opening-state-source-and-template").c_str());

    const FilterSettings edited = LiteralEditedSettings();
    fixture.session.UpdateColorEdit(edited, preview);
    Expect(fixture.session.PendingColorEdit() && fixture.session.PendingColorEdit()->preview == preview &&
               fixture.session.PendingColorEdit()->settings == edited,
           (prefix + ".settings-and-preview-state").c_str());

    const std::uint64_t generationBeforeClosure = fixture.session.Generation();
    const auto tokenBeforeClosure = fixture.session.CaptureJobToken();
    Expect(tokenBeforeClosure && fixture.session.CanApplyJobResult(tokenBeforeClosure.value(), 0),
           (prefix + ".whole-document-token-current-before-closure").c_str());
    auto closure = PrepareClosure(fixture, true);
    Expect(closure.has_value() && closure->Commits(), (prefix + ".ok-prepared").c_str());
    if (!closure)
        return;
    const LayerAdjustment expectedAdjustment = LiteralEditedAdjustment();
    Expect(HasAdjustment(closure->AfterDocument(), fixture.adjustmentID, expectedAdjustment) &&
               closure->AfterDocument()->layers[0].asset &&
               HasLiteralSource(closure->AfterDocument()->layers[0].asset->image),
           (prefix + ".ok-uses-independent-literal-parameters-and-source-bytes").c_str());
    Expect(fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(closure.value()), (prefix + ".ok-applied").c_str());
    const auto closedHistory = fixture.session.history.CaptureState();
    Expect(closedHistory.Depth() == 1 && closedHistory.PastCount() == 0 && closedHistory.FutureCount() == 1 &&
               closedHistory.RedoName() == L"Redo seed entry" && fixture.session.HasColorEdit() == false &&
               !fixture.session.AdjustmentOriginal() && fixture.session.Generation() == generationBeforeClosure + 1u &&
               tokenBeforeClosure && !fixture.session.CanApplyJobResult(tokenBeforeClosure.value(), 0),
           (prefix + ".closure-returns-to-outer-depth-without-inner-entry").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings &&
               HasAdjustment(fixture.session.Document(), fixture.adjustmentID, expectedAdjustment) &&
               fixture.session.Document()->layers[0].asset &&
               HasLiteralSource(fixture.session.Document()->layers[0].asset->image),
           (prefix + ".closed-live-document-literal-and-template").c_str());

    DocumentFixture expectedAfter = DocumentFixture::Copy(fixture.outerBefore.value());
    expectedAfter.resolution = 144.0;
    expectedAfter.layers[1].adjustment = expectedAdjustment;
    const CanvasDocument expectedDocument = std::move(expectedAfter).Seal();
    fixture.session.EndEdit();
    const auto completed = fixture.session.history.CaptureState();
    Expect(fixture.session.Document() == std::optional(expectedDocument) && completed.Depth() == 0 &&
               completed.PastCount() == 1 && completed.FutureCount() == 0 &&
               fixture.session.history.UndoName() == kOuterName,
           (prefix + ".outer-end-records-one-named-combined-entry").c_str());
    fixture.session.Undo();
    Expect(fixture.session.Document() == fixture.outerBefore && fixture.session.history.RedoName() == kOuterName,
           (prefix + ".undo-restores-literal-outer-before-document").c_str());
    fixture.session.Redo();
    Expect(fixture.session.Document() == std::optional(expectedDocument) &&
               HasAdjustment(fixture.session.Document(), fixture.adjustmentID, expectedAdjustment),
           (prefix + ".redo-restores-literal-combined-after-document").c_str());
}

void CancelNoopRetainsRedo(bool preview)
{
    Fixture fixture;
    fixture.session.BeginEdit(kOuterName);
    const std::string prefix = preview ? "cancel.preview-on" : "cancel.preview-off";
    auto opening = PrepareOpening(fixture);
    Expect(opening.has_value(), (prefix + ".opening-prepared").c_str());
    if (!opening)
        return;
    Expect(fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(opening.value()),
           (prefix + ".opening-applied").c_str());
    fixture.session.UpdateColorEdit(LiteralEditedSettings(), preview);
    Expect(fixture.session.PendingColorEdit() && fixture.session.PendingColorEdit()->preview == preview,
           (prefix + ".preview-setting").c_str());
    const std::uint64_t generationBeforeClosure = fixture.session.Generation();
    const auto tokenBeforeClosure = fixture.session.CaptureJobToken();
    Expect(tokenBeforeClosure && fixture.session.CanApplyJobResult(tokenBeforeClosure.value(), 0),
           (prefix + ".whole-document-token-current-before-closure").c_str());
    auto closure = PrepareClosure(fixture, false);
    Expect(closure.has_value() && !closure->Commits(), (prefix + ".cancel-prepared").c_str());
    if (!closure)
        return;
    Expect(closure->AfterDocument() == fixture.outerBefore &&
               HasAdjustment(closure->AfterDocument(), fixture.adjustmentID, fixture.original),
           (prefix + ".cancel-returns-literal-original-document").c_str());
    Expect(fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(closure.value()),
           (prefix + ".cancel-applied").c_str());
    const auto closed = fixture.session.history.CaptureState();
    Expect(fixture.session.Document() == fixture.outerBefore && closed.Depth() == 1 && closed.PastCount() == 0 &&
               closed.FutureCount() == 1 && closed.RedoName() == L"Redo seed entry" &&
               fixture.session.Generation() == generationBeforeClosure + 1u && tokenBeforeClosure &&
               !fixture.session.CanApplyJobResult(tokenBeforeClosure.value(), 0) && !fixture.session.HasColorEdit() &&
               !fixture.session.AdjustmentOriginal(),
           (prefix + ".cancel-returns-outer-depth-and-preserves-redo").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings && fixture.session.Document()->layers[0].asset &&
               HasLiteralSource(fixture.session.Document()->layers[0].asset->image),
           (prefix + ".cancel-preserves-global-template-and-source-bytes").c_str());
    fixture.session.EndEdit();
    const auto ended = fixture.session.history.CaptureState();
    Expect(ended.Depth() == 0 && ended.PastCount() == 0 && ended.FutureCount() == 1 &&
               ended.RedoName() == L"Redo seed entry" && fixture.session.Document() == fixture.outerBefore,
           (prefix + ".unchanged-outer-end-is-noop-and-retains-redo").c_str());
}

void Refusals()
{
    {
        Fixture fixture;
        fixture.session.BeginColorEdit(FilterKind::Levels, true);
        Expect(fixture.session.HasColorEdit(), "refuse.live-color-panel-fixture");
        Expect(!fixture.session.PrepareAdjustmentOpeningInput(), "refuse.live-color-panel");
    }
    {
        Fixture fixture;
        fixture.session.BeginEdit(kOuterName);
        auto input = fixture.session.PrepareAdjustmentOpeningInput();
        Expect(input.has_value(), "refuse.cancel-before-start-input");
        if (input)
        {
            CancelSource source;
            (void)source.Cancel();
            const auto prepared = DocumentSession::PrepareAdjustmentOpeningPublication(
                std::move(input.value()), fixture.session.Document(), fixture.source,
                fixture.session.history.CaptureState(), source.Token());
            Expect(!prepared, "refuse.cancel-before-start");
        }
    }
    {
        Fixture fixture;
        fixture.session.BeginEdit(kOuterName);
        auto input = fixture.session.PrepareAdjustmentOpeningInput();
        Expect(input.has_value(), "refuse.sequence-overflow-input");
        if (input)
        {
            input->token.sequence = (std::numeric_limits<std::uint64_t>::max)();
            const auto prepared = DocumentSession::PrepareAdjustmentOpeningPublication(
                std::move(input.value()), fixture.session.Document(), fixture.source,
                fixture.session.history.CaptureState());
            Expect(!prepared, "refuse.pending-sequence-overflow");
        }
    }
    // No public DocumentSession operation creates _adjustmentOriginal without its matching _colorEdit.
    // That otherwise-invalid internal combination is deliberately not manufactured by exposing private state.
}
} // namespace

int RunAdjustmentNestedTests()
{
    gFailures = 0;
    gAssertions = 0;
    NamedOuterCommit(false);
    NamedOuterCommit(true);
    CancelNoopRetainsRedo(false);
    CancelNoopRetainsRedo(true);
    Refusals();
    std::printf("SUMMARY adjustment-nested assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

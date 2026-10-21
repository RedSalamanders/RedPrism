#include "Document.Tests.ColorDismissal.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "AdjustmentClosureWork.h"
#include "ColorDismissalWork.h"
#include "ColorEditWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const std::string& group, const char* fact)
{
    ++gAssertions;
    std::printf("%s color-dismissal.%s.%s\n", condition ? "PASS" : "FAIL", group.c_str(), fact);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourceBytes{0,   0,   0,   255, 64,  64,  64,  255,
                                                    128, 128, 128, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> kMaskBytes{255, 255, 255, 255};
constexpr wchar_t kPriorName[] = L"Existing named entry";
constexpr wchar_t kRedoName[] = L"Retain this redo entry";
constexpr wchar_t kOuterName[] = L"Outer dismissal fixture: retained name";

bool LiteralPixels(const Raster& raster)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != 4 ||
        raster.Height() != 1)
        return false;
    std::array<std::uint8_t, kSourceBytes.size()> actual{};
    raster.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    return actual == kSourceBytes;
}

FilterSettings LiteralTemplate()
{
    FilterSettings settings;
    settings.radius = 0.75;
    settings.angle = -18;
    settings.distance = 83;
    settings.amount = 33;
    settings.gaussian = true;
    settings.monochromatic = true;
    settings.distortion = -12.5;
    settings.exposure = ExposureSettings{-0.5, 0.125, 1.7};
    settings.gradientMap = GradientMapSettings{{0.1, 0.2, 0.3}, {0.8, 0.7, 0.6}, true};
    settings.grain = GrainSettings{12, 1.25, 66, 0x24681357u};
    return settings;
}

FilterSettings PanelSettings(FilterKind kind)
{
    FilterSettings settings;
    if (kind == FilterKind::Levels)
        settings.levels.ranges[0].gamma = 1.7;
    else if (kind == FilterKind::GaussianBlur)
        settings.radius = 0.75;
    return settings;
}

LayerAdjustment LiteralAdjustment()
{
    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Levels;
    adjustment.levels.channel = LevelsChannel::Red;
    adjustment.levels.ranges[0] = LevelRange{5, 1.2, 245, 3, 250};
    return adjustment;
}

struct Fixture final
{
    Fixture()
        : source(Raster::Make(4, 1, RasterFormat::Bgra8888, kSourceBytes.data(), kSourceBytes.size())),
          templateSettings(LiteralTemplate())
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        document.selection = DocumentSelection{false, {SelectionLoop{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}};
        const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8, kMaskBytes.data(), kMaskBytes.size());
        ImageLayer primary = ImageLayer::Blank(L"Literal source layer", Vec2{4, 1});
        primaryID = primary.id;
        primary.asset = RasterAsset{source, Raster::MakeThumbnail(source), primary.name};
        primary.mask = LayerMask{RasterAsset{mask, mask, L"Literal full mask"}, true, std::nullopt, true};
        ImageLayer other = ImageLayer::Blank(L"Other source layer", Vec2{4, 1});
        otherID = other.id;
        other.asset = RasterAsset{source, source, other.name};
        ImageLayer adjustment = ImageLayer::Blank(L"Literal adjustment layer", Vec2{4, 1});
        adjustmentID = adjustment.id;
        adjustment.adjustment = LiteralAdjustment();
        document.layers.push_back(std::move(primary));
        document.layers.push_back(std::move(other));
        document.layers.push_back(std::move(adjustment));
        session.ReplaceDocument(std::move(document).Seal(), primaryID);
        session.filterSettings = templateSettings;
        session.BeginEdit(kPriorName);
        session.EditForTest([](CanvasDocument& current) { current.resolution = 144; });
        session.EndEdit();
        session.BeginEdit(kRedoName);
        EditLayerRecordsForTest(session,
                                [](DocumentFixture& current) { current.layers.front().name = L"Redo branch after"; });
        session.EndEdit();
        session.Undo();
        session.history.MarkSaved();
        session.SetCropFrame(CropRect{0, 0, 4, 1});
        baseline = session.Document();
    }

    Raster source;
    FilterSettings templateSettings;
    Guid primaryID;
    Guid otherID;
    Guid adjustmentID;
    DocumentSession session;
    std::optional<CanvasDocument> baseline;
};

bool TokensEqual(const std::optional<JobToken>& left, const std::optional<JobToken>& right)
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    const auto& a = left.value();
    const auto& b = right.value();
    if (a.sessionIdentity != b.sessionIdentity || a.documentSerial != b.documentSerial ||
        a.generation != b.generation || a.restoreSerial != b.restoreSerial || a.selection != b.selection ||
        a.canvasWidth != b.canvasWidth || a.canvasHeight != b.canvasHeight || a.sequence != b.sequence ||
        a.target.has_value() != b.target.has_value())
        return false;
    if (!a.target)
        return true;
    const auto& x = a.target.value();
    const auto& y = b.target.value();
    return x.layerId == y.layerId && x.image == y.image && x.transform == y.transform && x.mask == y.mask &&
           x.isGroup == y.isGroup && x.adjustment == y.adjustment;
}

bool PanelsEqual(const std::optional<ColorEdit>& left, const std::optional<ColorEdit>& right)
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    const auto& a = left.value();
    const auto& b = right.value();
    const bool dragEqual =
        a.targetDrag.has_value() == b.targetDrag.has_value() &&
        (!a.targetDrag || (a.targetDrag->range == b.targetDrag->range && a.targetDrag->hue == b.targetDrag->hue &&
                           a.targetDrag->saturation == b.targetDrag->saturation));
    return a.kind == b.kind && a.layerID == b.layerID && a.original == b.original && a.transform == b.transform &&
           a.selection == b.selection && a.settings == b.settings && TokensEqual(a.sourceToken, b.sourceToken) &&
           a.deferred == b.deferred && a.openingSequence == b.openingSequence &&
           a.previewSequence == b.previewSequence && a.previewInvalidatedSequence == b.previewInvalidatedSequence &&
           a.commitFailed == b.commitFailed && a.preview == b.preview && a.seed == b.seed &&
           a.grownImage == b.grownImage && a.grownGrid == b.grownGrid && a.grownTransform == b.grownTransform &&
           a.grownMargin == b.grownMargin && a.grownOffsetX == b.grownOffsetX && a.grownOffsetY == b.grownOffsetY &&
           a.previewSource == b.previewSource && a.previewScale == b.previewScale &&
           a.previewRaster == b.previewRaster && a.previewCoverage == b.previewCoverage &&
           a.previewLookup == b.previewLookup && a.previewLookupOnly == b.previewLookupOnly &&
           a.previewTransform == b.previewTransform && a.previewFailed == b.previewFailed &&
           a.histogram == b.histogram && a.histogramReady == b.histogramReady && a.levelsSample == b.levelsSample &&
           a.hueSample == b.hueSample && a.targeting == b.targeting && dragEqual;
}

struct State final
{
    std::optional<CanvasDocument> document;
    std::optional<CanvasDocument> displayed;
    std::optional<ColorEdit> panel;
    std::optional<Guid> active;
    std::optional<Guid> editing;
    std::optional<LayerAdjustment> adjustmentOriginal;
    bool mask = false;
    std::optional<CropRect> crop;
    FilterSettings templateSettings;
    std::uint64_t generation = 0;
    std::uint64_t serial = 0;
    std::uint64_t sequence = 0;
    Guid revision;
    bool modified = false;
    DocumentHistory::StateView history;
    std::wstring undoName;
    std::wstring redoName;
};

State Capture(const DocumentSession& session)
{
    State state;
    state.document = session.Document();
    if (const CanvasDocument* displayed = session.DisplayDocument())
        state.displayed = *displayed;
    state.panel = session.PendingColorEdit();
    state.active = session.ActiveLayerID();
    state.editing = session.AdjustmentEditingID();
    state.adjustmentOriginal = session.AdjustmentOriginal();
    state.mask = session.IsMaskSelected();
    state.crop = session.CropFrame();
    state.templateSettings = session.filterSettings;
    state.generation = session.Generation();
    state.serial = session.DocumentSerial();
    state.sequence = session.ColorEditSequence();
    state.revision = session.history.CurrentRevision();
    state.modified = session.history.IsModified();
    state.history = session.history.CaptureState();
    state.undoName = session.history.UndoName();
    state.redoName = session.history.RedoName();
    return state;
}

bool StableOwnerMatches(const DocumentSession& session, const State& expected)
{
    const auto history = session.history.CaptureState();
    return session.Document() == expected.document && session.ActiveLayerID() == expected.active &&
           session.AdjustmentEditingID() == expected.editing &&
           session.AdjustmentOriginal() == expected.adjustmentOriginal && session.IsMaskSelected() == expected.mask &&
           session.CropFrame() == expected.crop && session.filterSettings == expected.templateSettings &&
           session.Generation() == expected.generation && session.DocumentSerial() == expected.serial &&
           session.history.CurrentRevision() == expected.revision &&
           session.history.IsModified() == expected.modified && session.history.IsCurrent(expected.history) &&
           history.Depth() == expected.history.Depth() && history.PastCount() == expected.history.PastCount() &&
           history.FutureCount() == expected.history.FutureCount() && session.history.UndoName() == expected.undoName &&
           session.history.RedoName() == expected.redoName;
}

bool AllOwnerMatches(const DocumentSession& session, const State& expected)
{
    const auto after = Capture(session);
    return StableOwnerMatches(session, expected) && after.displayed == expected.displayed &&
           PanelsEqual(session.PendingColorEdit(), expected.panel) && session.ColorEditSequence() == expected.sequence;
}

bool OpenRealPreview(Fixture& fixture, FilterKind kind, const std::string& group)
{
    auto& session = fixture.session;
    session.BeginColorEdit(kind, true);
    Expect(session.HasColorEdit(kind), group, "panel-opened");
    if (!session.HasColorEdit(kind))
        return false;
    session.UpdateColorEdit(PanelSettings(kind), true);
    auto input = session.PrepareColorEditPreview();
    Expect(input.has_value(), group, "public-preview-input-captured");
    if (!input)
        return false;
    input->lookupPreview = kind == FilterKind::Levels;
    InlineExecutor executor;
    auto preview = RunColorEditPreviewJob(input.value(), executor);
    Expect(preview.status == ColorEditJobStatus::Ready, group, "real-preview-graph-ready");
    const bool applied = session.ApplyColorEditPreview(preview);
    Expect(applied, group, "real-preview-published");
    if (!applied)
        return false;
    const auto& panel = session.PendingColorEdit().value();
    Expect(panel.original == fixture.source && LiteralPixels(panel.original) && panel.previewSource.HasStorage(), group,
           "panel-holds-literal-original-and-prepared-source");
    Expect(kind == FilterKind::Levels ? (panel.previewLookup && !panel.previewRaster.HasStorage())
                                      : (!panel.previewLookup && panel.previewRaster.HasStorage()),
           group, "expected-public-lookup-or-raster-owner-held");
    const CanvasDocument* displayed = session.DisplayDocument();
    Expect(displayed && displayed != &session.Document().value() && displayed->layers.front().asset &&
               displayed->layers.front().asset->image ==
                   (panel.previewLookup ? panel.previewSource : panel.previewRaster),
           group, "public-display-copy-holds-preview-raster");
    return true;
}

std::optional<PreparedColorDismissal> Prepare(DocumentSession& session)
{
    auto input = session.PrepareColorDismissalInput();
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareColorDismissalPublication(std::move(input.value()), session.history.CaptureState());
}

void DismissOneKind(FilterKind kind, const char* name)
{
    Fixture fixture;
    const std::string group = name;
    Expect(fixture.session.Document() == fixture.baseline && fixture.session.history.UndoCount() == 1 &&
               fixture.session.history.CaptureState().FutureCount() == 1 &&
               fixture.session.history.UndoName() == kPriorName && fixture.session.history.RedoName() == kRedoName,
           group, "independent-literal-document-and-named-history-fixture");
    if (!OpenRealPreview(fixture, kind, group))
        return;
    Expect(fixture.session.Document() == fixture.baseline && fixture.session.filterSettings == fixture.templateSettings,
           group, "preview-keeps-live-document-and-global-template");
    const State expected = Capture(fixture.session);
    auto input = fixture.session.PrepareColorDismissalInput();
    Expect(input.has_value(), group, "dismissal-input-captured");
    if (!input)
        return;
    Expect(input->opening == expected.panel->openingSequence && input->token.sequence == expected.sequence &&
               !input->token.target && input->active == expected.active && input->maskSelected == expected.mask &&
               input->crop == expected.crop,
           group, "whole-document-opening-authority");
    Expect(AllOwnerMatches(fixture.session, expected), group, "input-capture-keeps-owner-state");
    auto candidate = DocumentSession::PrepareColorDismissalPublication(std::move(input.value()), expected.history);
    Expect(candidate.has_value(), group, "publication-prepared");
    Expect(AllOwnerMatches(fixture.session, expected), group, "preparation-keeps-all-owner-state");
    if (!candidate)
        return;
    Expect(fixture.session.IsPreparedColorDismissalCurrent(candidate.value()), group, "current-before-apply");
    Expect(fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()), group, "dismissal-applied");
    Expect(StableOwnerMatches(fixture.session, expected), group,
           "dismissal-keeps-document-target-crop-history-generation-and-template");
    Expect(!fixture.session.HasColorEdit() && fixture.session.ColorEditSequence() == expected.sequence + 1, group,
           "panel-closed-and-sequence-advanced-once");
    Expect(fixture.session.Document()->layers.front().asset &&
               fixture.session.Document()->layers.front().asset->image == fixture.source &&
               LiteralPixels(fixture.session.Document()->layers.front().asset->image),
           group, "literal-source-bytes-and-identity-unchanged");
    Expect(fixture.session.DisplayDocument() == &fixture.session.Document().value(), group,
           "preview-display-removed-and-live-document-displayed");
    const State closed = Capture(fixture.session);
    Expect(!fixture.session.IsPreparedColorDismissalCurrent(candidate.value()) &&
               !fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()),
           group, "repeated-publication-refused");
    Expect(AllOwnerMatches(fixture.session, closed), group, "repeated-refusal-keeps-closed-state");
}

void LookupRetirement()
{
    Fixture fixture;
    const std::string group = "lookup-retirement";
    if (!OpenRealPreview(fixture, FilterKind::Levels, group))
        return;
    std::weak_ptr<const AdjustmentLookup> held = fixture.session.PendingColorEdit()->previewLookup;
    Expect(!held.expired() && held.use_count() == 1, group, "lookup-has-only-live-panel-owner");
    auto candidate = Prepare(fixture.session);
    Expect(candidate.has_value(), group, "dismissal-prepared");
    if (!candidate)
        return;
    Expect(fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()), group, "dismissal-applied");
    Expect(!fixture.session.HasColorEdit() && !held.expired() && held.use_count() == 1, group,
           "lookup-stays-in-retirement-owner-after-live-panel-closes");
    Expect(fixture.session.DisplayDocument() == &fixture.session.Document().value() && LiteralPixels(fixture.source),
           group, "live-display-and-literal-source-remain-usable");
    candidate.reset();
    Expect(held.expired(), group, "lookup-last-owner-released-with-private-candidate");
}

void MovedAndForeign()
{
    {
        Fixture fixture;
        const std::string group = "moved";
        if (!OpenRealPreview(fixture, FilterKind::Levels, group))
            return;
        auto candidate = Prepare(fixture.session);
        Expect(candidate.has_value(), group, "prepared");
        if (!candidate)
            return;
        PreparedColorDismissal moved(std::move(candidate.value()));
        const State expected = Capture(fixture.session);
        Expect(!fixture.session.IsPreparedColorDismissalCurrent(candidate.value()) &&
                   !fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()),
               group, "moved-from-publication-refused");
        Expect(AllOwnerMatches(fixture.session, expected), group, "moved-from-refusal-keeps-owner");
        Expect(fixture.session.IsPreparedColorDismissalCurrent(moved) &&
                   fixture.session.ApplyPreparedColorDismissalIfCurrent(moved),
               group, "moved-to-publication-retains-authority");
    }
    {
        Fixture owner;
        Fixture foreign;
        const std::string group = "foreign";
        if (!OpenRealPreview(owner, FilterKind::Levels, group + ".owner") ||
            !OpenRealPreview(foreign, FilterKind::Levels, group + ".other"))
            return;
        auto candidate = Prepare(owner.session);
        Expect(candidate.has_value(), group, "owner-publication-prepared");
        if (!candidate)
            return;
        const State expected = Capture(foreign.session);
        Expect(!foreign.session.IsPreparedColorDismissalCurrent(candidate.value()) &&
                   !foreign.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()),
               group, "foreign-publication-refused");
        Expect(AllOwnerMatches(foreign.session, expected), group, "foreign-refusal-keeps-current-panel-and-state");
        Expect(owner.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()), group,
               "foreign-refusal-does-not-consume-owner-authority");
    }
}

enum class StaleChange
{
    Settings,
    Generation,
    Replacement,
    Active,
    Mask,
    Crop,
    HistoryEpoch,
};

void StaleOne(StaleChange change, const char* name)
{
    Fixture fixture;
    const std::string group = std::string("stale.") + name;
    if (!OpenRealPreview(fixture, FilterKind::Levels, group))
        return;
    auto candidate = Prepare(fixture.session);
    Expect(candidate.has_value(), group, "old-publication-prepared");
    if (!candidate)
        return;
    const State opening = Capture(fixture.session);
    Expect(fixture.session.IsPreparedColorDismissalCurrent(candidate.value()), group,
           "publication-current-before-invalidation");
    switch (change)
    {
    case StaleChange::Settings:
    {
        auto settings = PanelSettings(FilterKind::Levels);
        settings.levels.ranges[0].gamma = 2.5;
        fixture.session.UpdateColorEdit(settings, false);
        break;
    }
    case StaleChange::Generation:
        fixture.session.EditForTest([](CanvasDocument& document) { document.resolution = 222; });
        break;
    case StaleChange::Replacement:
    {
        const auto replacement = fixture.session.Document().value();
        fixture.session.ReplaceDocument(replacement, fixture.primaryID);
        if (!OpenRealPreview(fixture, FilterKind::Levels, group + ".new-opening"))
            return;
        break;
    }
    case StaleChange::Active:
        fixture.session.SelectLayer(fixture.otherID);
        break;
    case StaleChange::Mask:
        fixture.session.SelectLayerTarget(fixture.primaryID, true);
        break;
    case StaleChange::Crop:
        fixture.session.SetCropFrame(CropRect{1, 0, 2, 1});
        break;
    case StaleChange::HistoryEpoch:
        fixture.session.history.Begin(L"Epoch-only nested fixture", fixture.session.Document(),
                                      fixture.session.ActiveLayerID());
        fixture.session.history.End(fixture.session.Document(), fixture.session.ActiveLayerID());
        break;
    }
    const State expected = Capture(fixture.session);
    bool invalidationReached = false;
    switch (change)
    {
    case StaleChange::Settings:
        invalidationReached = expected.sequence == opening.sequence + 1 && expected.panel &&
                              expected.panel->settings.levels.ranges[0].gamma == 2.5 &&
                              expected.generation == opening.generation &&
                              fixture.session.history.IsCurrent(opening.history);
        break;
    case StaleChange::Generation:
        invalidationReached = expected.generation == opening.generation + 1 && expected.document &&
                              expected.document->resolution == 222 && expected.serial == opening.serial &&
                              expected.sequence == opening.sequence &&
                              fixture.session.history.IsCurrent(opening.history);
        break;
    case StaleChange::Replacement:
        invalidationReached = expected.serial != opening.serial && expected.panel && opening.panel &&
                              expected.panel->openingSequence != opening.panel->openingSequence;
        break;
    case StaleChange::Active:
        invalidationReached = expected.active == fixture.otherID && expected.active != opening.active &&
                              expected.generation == opening.generation && expected.sequence == opening.sequence;
        break;
    case StaleChange::Mask:
        invalidationReached = expected.mask && !opening.mask && expected.active == opening.active &&
                              expected.generation == opening.generation && expected.sequence == opening.sequence;
        break;
    case StaleChange::Crop:
        invalidationReached = expected.crop == std::optional(CropRect{1, 0, 2, 1}) && expected.crop != opening.crop &&
                              expected.generation == opening.generation && expected.sequence == opening.sequence;
        break;
    case StaleChange::HistoryEpoch:
        invalidationReached = !fixture.session.history.IsCurrent(opening.history) &&
                              expected.generation == opening.generation && expected.sequence == opening.sequence &&
                              expected.revision == opening.revision &&
                              expected.history.Depth() == opening.history.Depth() &&
                              expected.history.PastCount() == opening.history.PastCount() &&
                              expected.history.FutureCount() == opening.history.FutureCount();
        break;
    }
    Expect(invalidationReached, group, "specified-authority-invalidation-reached");
    Expect(fixture.session.HasColorEdit(), group, "current-panel-present-after-invalidation");
    Expect(!fixture.session.IsPreparedColorDismissalCurrent(candidate.value()), group, "stale-authority-refused");
    Expect(!fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()), group,
           "stale-publication-refused");
    Expect(AllOwnerMatches(fixture.session, expected), group,
           "refusal-retains-current-document-panel-display-history-and-settings");
}

void EarlyCancellationAndAdjustmentRefusal()
{
    {
        Fixture fixture;
        const std::string group = "cancel-before-preparation";
        if (!OpenRealPreview(fixture, FilterKind::GaussianBlur, group))
            return;
        const State expected = Capture(fixture.session);
        auto input = fixture.session.PrepareColorDismissalInput();
        Expect(input.has_value(), group, "input-captured");
        if (!input)
            return;
        CancelSource cancellation;
        Expect(cancellation.Cancel(), group, "cancellation-already-set");
        const auto candidate = DocumentSession::PrepareColorDismissalPublication(
            std::move(input.value()), fixture.session.history.CaptureState(), cancellation.Token());
        Expect(!candidate, group, "cancelled-preparation-yields-none");
        Expect(AllOwnerMatches(fixture.session, expected), group, "cancelled-preparation-keeps-owner-state");
    }
    {
        Fixture fixture;
        const std::string group = "adjustment-refusal";
        fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
        Expect(fixture.session.BeginAdjustmentEditing(fixture.adjustmentID, fixture.source, true), group,
               "reference-adjustment-panel-opened");
        const State expected = Capture(fixture.session);
        Expect(expected.panel && expected.adjustmentOriginal == std::optional(LiteralAdjustment()) &&
                   expected.history.Depth() == 1,
               group, "literal-original-and-open-metadata-transaction");
        Expect(!fixture.session.PrepareColorDismissalInput(), group, "adjustment-panel-refuses-destructive-dismissal");
        Expect(AllOwnerMatches(fixture.session, expected), group, "refusal-keeps-adjustment-panel-and-transaction");
        auto input = fixture.session.PrepareAdjustmentClosureInput(false);
        Expect(input.has_value(), group, "proper-metadata-cancel-input-captured");
        if (!input)
            return;
        auto closure = DocumentSession::PrepareAdjustmentClosurePublication(
            std::move(input.value()), fixture.session.Document(), fixture.session.history.CaptureState());
        Expect(closure.has_value(), group, "proper-metadata-end-prepared");
        if (!closure)
            return;
        Expect(fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(closure.value()), group,
               "proper-metadata-end-applied");
        Expect(!fixture.session.HasColorEdit() && fixture.session.history.CaptureState().Depth() == 0 &&
                   fixture.session.Generation() == expected.generation + 1 &&
                   fixture.session.Document() == expected.document && fixture.session.history.RedoName() == kRedoName &&
                   fixture.session.filterSettings == fixture.templateSettings,
               group, "metadata-end-closes-transaction-and-advances-generation-once");
    }
    {
        Fixture fixture;
        Expect(!fixture.session.PrepareColorDismissalInput(), "no-panel", "input-refused");
        DocumentSession empty;
        Expect(!empty.PrepareColorDismissalInput(), "no-document", "input-refused");
    }
}

void NestedOuter(bool changed)
{
    Fixture fixture;
    const std::string group = changed ? "outer.changed" : "outer.unchanged";
    const auto before = fixture.session.Document();
    fixture.session.BeginEdit(kOuterName);
    if (changed)
        fixture.session.EditForTest([](CanvasDocument& document) { document.resolution = 222; });
    if (!OpenRealPreview(fixture, FilterKind::Levels, group))
        return;
    const State expected = Capture(fixture.session);
    Expect(expected.history.Depth() == 1 && expected.history.PastCount() == 1 && expected.history.FutureCount() == 1,
           group, "outer-depth-and-independent-stack-counts");
    auto candidate = Prepare(fixture.session);
    Expect(candidate.has_value(), group, "dismissal-prepared");
    Expect(AllOwnerMatches(fixture.session, expected), group, "prepare-keeps-nested-owner-state");
    if (!candidate)
        return;
    Expect(fixture.session.ApplyPreparedColorDismissalIfCurrent(candidate.value()), group, "dismissal-applied");
    Expect(StableOwnerMatches(fixture.session, expected) && fixture.session.history.CaptureState().Depth() == 1, group,
           "dismissal-preserves-exact-outer-history-epoch-depth-and-generation");
    CanvasDocument after = before.value();
    if (changed)
        after.resolution = 222;
    fixture.session.EndEdit();
    const auto ended = fixture.session.history.CaptureState();
    Expect(ended.Depth() == 0 && fixture.session.Document() == std::optional(after), group,
           "later-outer-end-keeps-literal-document");
    if (changed)
    {
        Expect(ended.PastCount() == 2 && ended.FutureCount() == 0 && fixture.session.history.UndoName() == kOuterName,
               group, "later-outer-end-records-exactly-one-outer-named-entry");
        fixture.session.Undo();
        Expect(fixture.session.Document() == before && fixture.session.history.RedoName() == kOuterName, group,
               "outer-entry-undo-restores-literal-before");
        fixture.session.Redo();
        Expect(fixture.session.Document() == std::optional(after), group, "outer-entry-redo-restores-literal-after");
    }
    else
    {
        Expect(ended.PastCount() == 1 && ended.FutureCount() == 1 && fixture.session.history.UndoName() == kPriorName &&
                   fixture.session.history.RedoName() == kRedoName,
               group, "unchanged-outer-end-keeps-prior-name-and-redo");
    }
}
} // namespace

int RunColorDismissalTests()
{
    gAssertions = 0;
    gFailures = 0;
    DismissOneKind(FilterKind::Levels, "levels");
    DismissOneKind(FilterKind::GaussianBlur, "gaussian");
    DismissOneKind(FilterKind::ContentAwareFill, "fill");
    LookupRetirement();
    MovedAndForeign();
    StaleOne(StaleChange::Settings, "settings-sequence");
    StaleOne(StaleChange::Generation, "document-generation");
    StaleOne(StaleChange::Replacement, "replaced-session");
    StaleOne(StaleChange::Active, "active-layer");
    StaleOne(StaleChange::Mask, "mask-target");
    StaleOne(StaleChange::Crop, "crop-frame");
    StaleOne(StaleChange::HistoryEpoch, "history-epoch");
    EarlyCancellationAndAdjustmentRefusal();
    NestedOuter(true);
    NestedOuter(false);
    std::printf("SUMMARY color-dismissal assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

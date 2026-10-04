#include "AdjustmentClosureTests.h"

#include "../Support/DocumentFixture.h"
#include "AdjustmentClosureWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
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
    std::printf("%s adjustment-closure.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourcePixels{0,   0,   0,   255, 32,  64,  96,  255,
                                                     128, 160, 192, 255, 255, 224, 192, 255};
constexpr std::array<AdjustmentKind, 6> kKinds{AdjustmentKind::Hsv,         AdjustmentKind::Levels,
                                               AdjustmentKind::Curves,      AdjustmentKind::Exposure,
                                               AdjustmentKind::GradientMap, AdjustmentKind::Grain};
constexpr std::array<const char*, 6> kTestNames{"hsv", "levels", "curves", "exposure", "gradient-map", "grain"};
constexpr std::array<const wchar_t*, 6> kNames{L"Hue/Saturation", L"Levels",       L"Curves",
                                               L"Exposure",       L"Gradient Map", L"Grain"};

Raster SourceRaster()
{
    return Raster::Make(2, 2, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
}

bool SourceBytesMatch(const Raster& raster)
{
    if (!raster.HasStorage() || raster.Width() != 2 || raster.Height() != 2 ||
        raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kSourcePixels.size()> actual{};
    raster.ReadRegion(0, 0, 2, 2, actual.data(), 8, 0);
    return actual == kSourcePixels;
}

LayerAdjustment OriginalAdjustment(AdjustmentKind kind)
{
    LayerAdjustment result;
    result.kind = kind;
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        result.hsvSettings = HueSaturationSettings{};
        result.hsvSettings->range = ColorRange::Reds;
        result.hsvSettings->adjustments[static_cast<std::size_t>(ColorRange::Reds)] = {12, -8, 4};
        break;
    case AdjustmentKind::Levels:
        result.levels.channel = LevelsChannel::Red;
        result.levels.ranges[0] = LevelRange{5, 1.2, 245, 3, 250};
        break;
    case AdjustmentKind::Curves:
        result.curves.channel = LevelsChannel::Green;
        result.curves.channels[1] = {{0, 0}, {80, 96}, {255, 255}};
        break;
    case AdjustmentKind::Exposure:
        result.exposureSettings = ExposureSettings{0.25, 0.05, 1.2};
        break;
    case AdjustmentKind::GradientMap:
        result.gradientMapSettings = GradientMapSettings{{0.08, 0.16, 0.24}, {0.92, 0.84, 0.76}, false};
        break;
    case AdjustmentKind::Grain:
        result.grainSettings = GrainSettings{18, 2.0, 42, 0x13572468u};
        break;
    }
    return result;
}

FilterSettings OpeningSettings(AdjustmentKind kind, const LayerAdjustment& original)
{
    FilterSettings settings;
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        settings.hsv = original.hsvSettings.value();
        break;
    case AdjustmentKind::Levels:
        settings.levels = original.levels;
        break;
    case AdjustmentKind::Curves:
        settings.curves = original.curves;
        break;
    case AdjustmentKind::Exposure:
        settings.exposure = original.exposureSettings.value();
        break;
    case AdjustmentKind::GradientMap:
        settings.gradientMap = original.gradientMapSettings.value();
        break;
    case AdjustmentKind::Grain:
        settings.grain = original.grainSettings.value();
        break;
    }
    return settings;
}

FilterSettings ChangedSettings(AdjustmentKind kind, const FilterSettings& opening)
{
    FilterSettings settings = opening;
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        settings.hsv.range = ColorRange::Blues;
        settings.hsv.colorize = true;
        settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)] = {31, 18, -7};
        break;
    case AdjustmentKind::Levels:
        settings.levels.channel = LevelsChannel::Blue;
        settings.levels.ranges[2] = LevelRange{11, 1.7, 238, 6, 247};
        break;
    case AdjustmentKind::Curves:
        settings.curves.channel = LevelsChannel::Blue;
        settings.curves.channels[2] = {{0, 0}, {64, 53}, {190, 218}, {255, 255}};
        break;
    case AdjustmentKind::Exposure:
        settings.exposure = ExposureSettings{1.25, -0.125, 1.4};
        break;
    case AdjustmentKind::GradientMap:
        settings.gradientMap = GradientMapSettings{{0.12, 0.23, 0.34}, {0.81, 0.72, 0.63}, true};
        break;
    case AdjustmentKind::Grain:
        settings.grain = GrainSettings{42, 2.5, 37, 0x13572468u};
        break;
    }
    return settings;
}

// This is a literal metadata oracle. It does not call AdjustmentWithSettings or any pixel calculation.
LayerAdjustment ExpectedAdjustment(AdjustmentKind kind, LayerAdjustment original, const FilterSettings& settings)
{
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        original.hsvSettings =
            HueSaturationSettings{ColorRange::Blues, true, false, settings.hsv.adjustments, settings.hsv.bands};
        break;
    case AdjustmentKind::Levels:
        original.levels = LevelsSettings{settings.levels.channel, settings.levels.ranges};
        break;
    case AdjustmentKind::Curves:
        original.curves = settings.curves;
        break;
    case AdjustmentKind::Exposure:
        original.exposureSettings = ExposureSettings{1.25, -0.125, 1.4};
        break;
    case AdjustmentKind::GradientMap:
        original.gradientMapSettings = GradientMapSettings{{0.12, 0.23, 0.34}, {0.81, 0.72, 0.63}, true};
        break;
    case AdjustmentKind::Grain:
        original.grainSettings = GrainSettings{42, 2.5, 37, 0x13572468u};
        break;
    }
    return original;
}

FilterSettings TemplateSettings()
{
    FilterSettings settings;
    settings.radius = 7.25;
    settings.angle = -18;
    settings.amount = 33;
    settings.gaussian = true;
    settings.monochromatic = true;
    return settings;
}

struct Fixture final
{
    explicit Fixture(AdjustmentKind adjustmentKind)
        : kind(adjustmentKind), original(OriginalAdjustment(adjustmentKind)), opening(OpeningSettings(kind, original)),
          changed(ChangedSettings(kind, opening)), expected(ExpectedAdjustment(kind, original, changed)),
          templateSettings(TemplateSettings()), source(SourceRaster())
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 2;
        document.height = 2;
        ImageLayer adjustment = ImageLayer::Blank(L"Adjustment fixture", Vec2{2, 2});
        adjustment.adjustment = original;
        adjustmentID = adjustment.id;
        ImageLayer pixels = ImageLayer::Blank(L"Source fixture", Vec2{2, 2});
        baseID = pixels.id;
        pixels.asset = RasterAsset{source, Raster::MakeThumbnail(source), pixels.name};
        Raster maskPixels = Raster::Make(1, 1, RasterFormat::Gray8, kMaskPixel.data(), kMaskPixel.size());
        adjustment.mask = LayerMask{RasterAsset{maskPixels, maskPixels, L"Mask fixture"}, true, std::nullopt, true};
        document.layers.push_back(std::move(adjustment));
        document.layers.push_back(std::move(pixels));
        session.ReplaceDocument(std::move(document).Seal(), adjustmentID);
        session.filterSettings = templateSettings;

        session.BeginEdit(L"Seed redo");
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& current) { current.layers[1].name = L"Changed source fixture"; });
        session.EndEdit();
        session.Undo();
        openingDocument = session.Document();
        session.SetAdjustmentEditingID(adjustmentID);
        opened = session.BeginAdjustmentEditing(adjustmentID, source, true);
    }

    static constexpr std::array<std::uint8_t, 1> kMaskPixel{255};
    AdjustmentKind kind;
    Guid adjustmentID;
    Guid baseID;
    LayerAdjustment original;
    FilterSettings opening;
    FilterSettings changed;
    LayerAdjustment expected;
    FilterSettings templateSettings;
    Raster source;
    DocumentSession session;
    std::optional<CanvasDocument> openingDocument;
    bool opened = false;
};

struct State final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    std::optional<Guid> editing;
    std::optional<LayerAdjustment> original;
    std::optional<FilterSettings> pendingSettings;
    std::optional<bool> preview;
    std::optional<std::uint64_t> opening;
    bool maskSelected = false;
    bool hasColorEdit = false;
    std::optional<CropRect> crop;
    std::uint64_t generation = 0;
    std::uint64_t documentSerial = 0;
    std::uint64_t colorEditSequence = 0;
    FilterSettings filterSettings;
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
    state.active = session.ActiveLayerID();
    state.editing = session.AdjustmentEditingID();
    state.original = session.AdjustmentOriginal();
    state.hasColorEdit = session.HasColorEdit();
    if (session.PendingColorEdit())
    {
        state.pendingSettings = session.PendingColorEdit()->settings;
        state.preview = session.PendingColorEdit()->preview;
        state.opening = session.PendingColorEdit()->openingSequence;
    }
    state.maskSelected = session.IsMaskSelected();
    state.crop = session.CropFrame();
    state.generation = session.Generation();
    state.documentSerial = session.DocumentSerial();
    state.colorEditSequence = session.ColorEditSequence();
    state.filterSettings = session.filterSettings;
    state.revision = session.history.CurrentRevision();
    state.modified = session.history.IsModified();
    state.history = session.history.CaptureState();
    state.undoName = session.history.UndoName();
    state.redoName = session.history.RedoName();
    return state;
}

bool StateMatches(const DocumentSession& session, const State& state)
{
    const auto& pending = session.PendingColorEdit();
    return session.Document() == state.document && session.ActiveLayerID() == state.active &&
           session.AdjustmentEditingID() == state.editing && session.AdjustmentOriginal() == state.original &&
           session.HasColorEdit() == state.hasColorEdit && pending.has_value() == state.pendingSettings.has_value() &&
           (!pending || (pending->settings == state.pendingSettings && pending->preview == state.preview &&
                         pending->openingSequence == state.opening)) &&
           session.IsMaskSelected() == state.maskSelected && session.CropFrame() == state.crop &&
           session.Generation() == state.generation && session.DocumentSerial() == state.documentSerial &&
           session.ColorEditSequence() == state.colorEditSequence && session.filterSettings == state.filterSettings &&
           session.history.CurrentRevision() == state.revision && session.history.IsModified() == state.modified &&
           session.history.IsCurrent(state.history) &&
           session.history.UndoCount() == static_cast<int>(state.history.PastCount()) &&
           session.history.CaptureState().FutureCount() == state.history.FutureCount() &&
           session.history.CaptureState().Depth() == state.history.Depth() &&
           session.history.UndoName() == state.undoName && session.history.RedoName() == state.redoName;
}

std::optional<PreparedAdjustmentClosure> Prepare(Fixture& fixture, bool commit,
                                                 DocumentHistory::PrepareObserver observer = {},
                                                 CancelToken cancellation = {})
{
    auto input = fixture.session.PrepareAdjustmentClosureInput(commit);
    if (!input)
        return std::nullopt;
    const auto source = fixture.session.Document();
    return DocumentSession::PrepareAdjustmentClosurePublication(
        std::move(input.value()), source, fixture.session.history.CaptureState(), cancellation, observer);
}

bool TargetAdjustment(const std::optional<CanvasDocument>& document, const Guid& id,
                      const std::optional<LayerAdjustment>& expected)
{
    if (!document)
        return false;
    for (const ImageLayer& layer : document->layers)
        if (layer.id == id)
            return layer.adjustment == expected;
    return false;
}

void CheckFixture(Fixture& fixture, const char* caseName)
{
    const std::string suffix = std::string(caseName) + ".fixture";
    Expect(fixture.opened, (suffix + ".opened").c_str());
    Expect(fixture.session.history.CaptureState().FutureCount() == 1, (suffix + ".seeded-redo").c_str());
    Expect(fixture.session.HasColorEdit() && fixture.session.AdjustmentEditingID() == fixture.adjustmentID,
           (suffix + ".adjustment-open").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings, (suffix + ".template-settings-initial").c_str());
}

void CommitOneKind(AdjustmentKind kind, const char* name)
{
    Fixture fixture(kind);
    CheckFixture(fixture, name);
    fixture.session.UpdateColorEdit(fixture.changed, false);
    Expect(fixture.session.PendingColorEdit() && !fixture.session.PendingColorEdit()->preview,
           (std::string(name) + ".commit-preview-off").c_str());
    Expect(fixture.session.Document() == fixture.openingDocument,
           (std::string(name) + ".commit-preview-off-restores-live-metadata").c_str());
    auto input = fixture.session.PrepareAdjustmentClosureInput(true);
    Expect(input.has_value(), (std::string(name) + ".commit-input").c_str());
    if (!input)
        return;
    const auto source = fixture.session.Document();
    auto candidate = DocumentSession::PrepareAdjustmentClosurePublication(std::move(input.value()), source,
                                                                          fixture.session.history.CaptureState());
    Expect(candidate.has_value(), (std::string(name) + ".commit-prepared").c_str());
    if (!candidate)
        return;
    Expect(TargetAdjustment(candidate->AfterDocument(), fixture.adjustmentID, fixture.expected),
           (std::string(name) + ".commit-independent-literal-metadata").c_str());
    Expect(candidate->AfterDocument()->width == 2 && candidate->AfterDocument()->height == 2 &&
               candidate->AfterDocument()->layers.size() == 2 && candidate->AfterDocument()->layers[1].asset &&
               candidate->AfterDocument()->layers[1].asset->image == fixture.source,
           (std::string(name) + ".commit-retains-canvas-layer-order-and-source-owner").c_str());
    Expect(fixture.session.IsPreparedAdjustmentClosureCurrent(candidate.value()),
           (std::string(name) + ".commit-current-before-apply").c_str());
    const std::uint64_t generationBeforeApply = fixture.session.Generation();
    const bool applied = fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate.value());
    Expect(applied, (std::string(name) + ".commit-applied").c_str());
    Expect(fixture.session.Generation() == generationBeforeApply + 1,
           (std::string(name) + ".commit-preview-off-parameter-change-advances-generation-once").c_str());
    Expect(!fixture.session.HasColorEdit() && !fixture.session.AdjustmentEditingID() &&
               !fixture.session.AdjustmentOriginal(),
           (std::string(name) + ".commit-closes-panel").c_str());
    Expect(TargetAdjustment(fixture.session.Document(), fixture.adjustmentID, fixture.expected),
           (std::string(name) + ".commit-persists-literal-adjustment-record").c_str());
    Expect(fixture.session.history.UndoCount() == 1 && fixture.session.history.CaptureState().FutureCount() == 0 &&
               fixture.session.history.UndoName() ==
                   std::wstring(L"Edit ") + kNames[static_cast<std::size_t>(kind)] + L" Adjustment",
           (std::string(name) + ".commit-one-named-history-entry").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (std::string(name) + ".commit-template-settings-unchanged").c_str());
    Expect(SourceBytesMatch(fixture.session.Document()->layers[1].asset->image),
           (std::string(name) + ".commit-source-pixels-unchanged").c_str());
    fixture.session.Undo();
    Expect(fixture.session.Document() == fixture.openingDocument && fixture.session.history.CanRedo(),
           (std::string(name) + ".commit-undo-restores-before-document").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (std::string(name) + ".commit-undo-template-settings-unchanged").c_str());
    fixture.session.Redo();
    Expect(TargetAdjustment(fixture.session.Document(), fixture.adjustmentID, fixture.expected),
           (std::string(name) + ".commit-redo-restores-literal-record").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (std::string(name) + ".commit-redo-template-settings-unchanged").c_str());
}

void CancelOneKind(AdjustmentKind kind, const char* name, bool preview)
{
    Fixture fixture(kind);
    CheckFixture(fixture, name);
    fixture.session.UpdateColorEdit(fixture.changed, preview);
    Expect(fixture.session.PendingColorEdit() && fixture.session.PendingColorEdit()->preview == preview,
           (std::string(name) + (preview ? ".cancel-preview-on" : ".cancel-preview-off")).c_str());
    if (preview)
        Expect(TargetAdjustment(fixture.session.Document(), fixture.adjustmentID, fixture.expected),
               (std::string(name) + ".cancel-preview-on-live-literal").c_str());
    else
        Expect(fixture.session.Document() == fixture.openingDocument,
               (std::string(name) + ".cancel-preview-off-original-live-document").c_str());
    auto input = fixture.session.PrepareAdjustmentClosureInput(false);
    Expect(input.has_value(), (std::string(name) + ".cancel-input").c_str());
    if (!input)
        return;
    const auto source = fixture.session.Document();
    auto candidate = DocumentSession::PrepareAdjustmentClosurePublication(std::move(input.value()), source,
                                                                          fixture.session.history.CaptureState());
    Expect(candidate.has_value() && !candidate->Commits(), (std::string(name) + ".cancel-prepared-as-cancel").c_str());
    if (!candidate)
        return;
    Expect(candidate->AfterDocument() == fixture.openingDocument &&
               TargetAdjustment(candidate->AfterDocument(), fixture.adjustmentID, fixture.original),
           (std::string(name) + ".cancel-prepared-original-metadata").c_str());
    const std::uint64_t generationBeforeApply = fixture.session.Generation();
    const bool applied = fixture.session.IsPreparedAdjustmentClosureCurrent(candidate.value()) &&
                         fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate.value());
    Expect(applied, (std::string(name) + ".cancel-applied").c_str());
    Expect(fixture.session.Generation() == generationBeforeApply + 1,
           (std::string(name) + ".cancel-acknowledged-end-advances-generation-once").c_str());
    Expect(fixture.session.Document() == fixture.openingDocument && !fixture.session.HasColorEdit() &&
               !fixture.session.AdjustmentEditingID(),
           (std::string(name) + ".cancel-restores-document-and-closes-panel").c_str());
    Expect(fixture.session.history.UndoCount() == 0 && fixture.session.history.CaptureState().FutureCount() == 1 &&
               fixture.session.history.CanRedo(),
           (std::string(name) + ".cancel-no-entry-preserves-redo").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (std::string(name) + ".cancel-template-settings-unchanged").c_str());
    Expect(SourceBytesMatch(fixture.session.Document()->layers[1].asset->image),
           (std::string(name) + ".cancel-source-pixels-unchanged").c_str());
}

void NoOpCommitOneKind(AdjustmentKind kind, const char* name)
{
    Fixture fixture(kind);
    CheckFixture(fixture, name);
    const State before = Capture(fixture.session);
    fixture.session.UpdateColorEdit(fixture.opening, false);
    const std::uint64_t generationBeforeApply = fixture.session.Generation();
    const auto oldWholeToken = fixture.session.CaptureJobToken();
    auto candidate = Prepare(fixture, true);
    Expect(candidate.has_value(), (std::string(name) + ".no-op-prepared").c_str());
    if (!candidate)
        return;
    Expect(candidate->AfterDocument() == fixture.openingDocument,
           (std::string(name) + ".no-op-document-equal").c_str());
    const bool applied = fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate.value());
    Expect(applied && !fixture.session.HasColorEdit(), (std::string(name) + ".no-op-closes").c_str());
    Expect(fixture.session.Generation() == generationBeforeApply + 1,
           (std::string(name) + ".no-op-end-advances-generation").c_str());
    Expect(oldWholeToken && !fixture.session.CanApplyJobResult(oldWholeToken.value(), 0),
           (std::string(name) + ".no-op-end-expires-old-whole-token").c_str());
    Expect(fixture.session.history.CurrentRevision() == before.revision && !fixture.session.history.IsModified() &&
               fixture.session.history.UndoCount() == 0 && fixture.session.history.CaptureState().FutureCount() == 1 &&
               fixture.session.history.CanRedo(),
           (std::string(name) + ".no-op-history-revision-and-redo-preserved").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (std::string(name) + ".no-op-template-settings-unchanged").c_str());
}

enum class FailureMode
{
    Cancel,
    ThrowAllocation,
};

struct CheckpointAction final
{
    DocumentHistory::PrepareStage target{};
    FailureMode mode = FailureMode::Cancel;
    CancelSource cancellation;
    bool reached = false;

    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& action = *static_cast<CheckpointAction*>(context);
        if (stage != action.target)
            return;
        action.reached = true;
        if (action.mode == FailureMode::Cancel)
            (void)action.cancellation.Cancel();
        else
            throw std::bad_alloc{};
    }
};

void FailureAtCheckpoint(DocumentHistory::PrepareStage stage, const char* name, FailureMode mode)
{
    Fixture fixture(AdjustmentKind::Levels);
    CheckFixture(fixture, name);
    fixture.session.UpdateColorEdit(fixture.changed, false);
    auto input = fixture.session.PrepareAdjustmentClosureInput(true);
    Expect(input.has_value(), (std::string(name) + ".failure-input").c_str());
    if (!input)
        return;
    const auto source = fixture.session.Document();
    const auto history = fixture.session.history.CaptureState();
    const State before = Capture(fixture.session);
    CheckpointAction action{stage, mode};
    DocumentHistory::PrepareObserver observer{&CheckpointAction::Checkpoint, &action};
    std::optional<PreparedAdjustmentClosure> candidate;
    bool allocationPropagated = false;
    try
    {
        candidate = DocumentSession::PrepareAdjustmentClosurePublication(
            std::move(input.value()), source, history,
            mode == FailureMode::Cancel ? action.cancellation.Token() : CancelToken{}, observer);
    }
    catch (const std::bad_alloc&)
    {
        allocationPropagated = true;
    }
    Expect(action.reached, (std::string(name) + ".failure-checkpoint-reached").c_str());
    if (mode == FailureMode::Cancel)
    {
        Expect(!candidate && !allocationPropagated,
               (std::string(name) + ".failure-cancel-returns-no-publication").c_str());
    }
    else
    {
        Expect(!candidate && allocationPropagated,
               (std::string(name) + ".failure-bad-alloc-propagates-to-typed-catch").c_str());
    }
    Expect(StateMatches(fixture.session, before), (std::string(name) + ".failure-session-state-unchanged").c_str());
    auto recovery = Prepare(fixture, true);
    Expect(recovery.has_value() && fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(recovery.value()),
           (std::string(name) + ".failure-next-attempt-succeeds").c_str());
}

void ExpectStaleRefusal(Fixture& fixture, PreparedAdjustmentClosure& candidate, const char* name)
{
    const State afterInvalidation = Capture(fixture.session);
    Expect(!fixture.session.IsPreparedAdjustmentClosureCurrent(candidate),
           (std::string(name) + ".stale-current-refused").c_str());
    Expect(!fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate),
           (std::string(name) + ".stale-apply-refused").c_str());
    Expect(StateMatches(fixture.session, afterInvalidation),
           (std::string(name) + ".stale-refusal-preserves-invalidated-state").c_str());
}

std::optional<PreparedAdjustmentClosure> PreparedCommit(Fixture& fixture)
{
    fixture.session.UpdateColorEdit(fixture.changed, false);
    return Prepare(fixture, true);
}

void StaleCandidates()
{
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto input = fixture.session.PrepareAdjustmentClosureInput(true);
        Expect(input.has_value(), "stale-token.input");
        if (input)
        {
            ++input->token.generation;
            const auto source = fixture.session.Document();
            auto candidate = DocumentSession::PrepareAdjustmentClosurePublication(
                std::move(input.value()), source, fixture.session.history.CaptureState());
            Expect(candidate.has_value(), "stale-token.candidate-prepared");
            if (candidate)
                ExpectStaleRefusal(fixture, candidate.value(), "stale-token");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-generation.candidate");
        if (candidate)
        {
            EditLayerRecordsForTest(fixture.session, [&](DocumentFixture& document)
                                    { document.layers[1].name = L"Changed after prepare"; });
            ExpectStaleRefusal(fixture, candidate.value(), "stale-generation");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-document-serial.candidate");
        if (candidate)
        {
            const CanvasDocument replacement = fixture.session.Document().value();
            const std::optional<Guid> active = fixture.session.ActiveLayerID();
            fixture.session.ReplaceDocument(replacement, active);
            ExpectStaleRefusal(fixture, candidate.value(), "stale-document-serial");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-opening.candidate");
        if (candidate)
        {
            (void)fixture.session.FinishAdjustmentEditing(false);
            fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
            const bool opened = fixture.session.BeginAdjustmentEditing(fixture.adjustmentID, fixture.source, true);
            Expect(opened, "stale-opening.reopened-same-layer");
            ExpectStaleRefusal(fixture, candidate.value(), "stale-opening");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-active.candidate");
        if (candidate)
        {
            fixture.session.SelectLayer(fixture.baseID);
            Expect(fixture.session.ActiveLayerID() == fixture.baseID, "stale-active.sibling-selected");
            ExpectStaleRefusal(fixture, candidate.value(), "stale-active");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-mask.candidate");
        if (candidate)
        {
            fixture.session.SelectLayerTarget(fixture.adjustmentID, true);
            Expect(fixture.session.IsMaskSelected(), "stale-mask.mask-selected");
            ExpectStaleRefusal(fixture, candidate.value(), "stale-mask");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-crop.candidate");
        if (candidate)
        {
            fixture.session.SetCropFrame(CropRect{0, 0, 1, 2});
            Expect(fixture.session.CropFrame() == CropRect{0, 0, 1, 2}, "stale-crop.frame-changed");
            ExpectStaleRefusal(fixture, candidate.value(), "stale-crop");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "stale-history-epoch.candidate");
        if (candidate)
        {
            const auto revision = fixture.session.history.CurrentRevision();
            const int depth = fixture.session.history.CaptureState().Depth();
            const auto document = fixture.session.Document();
            const auto active = fixture.session.ActiveLayerID();
            fixture.session.history.Begin(L"Nested same-state", document, active);
            fixture.session.history.End(document, active);
            Expect(fixture.session.history.CurrentRevision() == revision &&
                       fixture.session.history.CaptureState().Depth() == depth &&
                       fixture.session.Document() == document,
                   "stale-history-epoch.nested-no-op-preserves-document-revision-depth");
            ExpectStaleRefusal(fixture, candidate.value(), "stale-history-epoch");
        }
    }
}

void RunKind(AdjustmentKind kind, const char* name)
{
    CommitOneKind(kind, name);
    CancelOneKind(kind, name, false);
    CancelOneKind(kind, name, true);
    NoOpCommitOneKind(kind, name);
}

void SettingsAuthorityAndLaterSavedRevision()
{
    {
        Fixture fixture(AdjustmentKind::Levels);
        fixture.session.UpdateColorEdit(fixture.changed, false);
        auto input = fixture.session.PrepareAdjustmentClosureInput(true);
        Expect(input.has_value(), "same-sequence-settings.input");
        if (input)
        {
            input->settings.levels.ranges[0].gamma += 0.125;
            const State before = Capture(fixture.session);
            auto candidate = DocumentSession::PrepareAdjustmentClosurePublication(
                std::move(input.value()), fixture.session.Document(), fixture.session.history.CaptureState());
            Expect(candidate.has_value(), "same-sequence-settings.candidate");
            Expect(candidate && !fixture.session.IsPreparedAdjustmentClosureCurrent(candidate.value()) &&
                       !fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate.value()),
                   "same-sequence-settings.mismatched-parameters-refused");
            Expect(StateMatches(fixture.session, before), "same-sequence-settings.refusal-keeps-live-state");
        }
    }
    {
        Fixture fixture(AdjustmentKind::Levels);
        auto candidate = PreparedCommit(fixture);
        Expect(candidate.has_value(), "later-saved.candidate");
        if (candidate)
        {
            // A written revision need no longer be reachable in retained history. Publish may not overwrite
            // that later save marker with the clean baseline captured by the worker.
            const Guid laterSaved = Guid::Generate();
            fixture.session.history.MarkSaved(laterSaved);
            Expect(fixture.session.history.IsModified() &&
                       fixture.session.IsPreparedAdjustmentClosureCurrent(candidate.value()),
                   "later-saved.save-marker-independent-of-edit-authority");
            Expect(fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(candidate.value()), "later-saved.applied");
            Expect(TargetAdjustment(fixture.session.Document(), fixture.adjustmentID, fixture.expected) &&
                       fixture.session.history.IsModified() &&
                       fixture.session.filterSettings == fixture.templateSettings,
                   "later-saved.literal-parameters-and-later-save-marker-kept");
            fixture.session.Undo();
            Expect(fixture.session.Document() == fixture.openingDocument && fixture.session.history.IsModified(),
                   "later-saved.undo-still-modified-against-later-written-revision");
        }
    }
}
} // namespace

int RunAdjustmentClosureTests()
{
    gFailures = 0;
    gAssertions = 0;
    for (std::size_t index = 0; index < kKinds.size(); ++index)
        RunKind(kKinds[index], kTestNames[index]);

    constexpr std::array stages{DocumentHistory::PrepareStage::AdjustmentSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    constexpr std::array stageNames{"adjustment-snapshot", "after-snapshot", "entry", "stack-growth", "trim-index"};
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        const std::string cancelName = std::string("cancel-") + stageNames[index];
        FailureAtCheckpoint(stages[index], cancelName.c_str(), FailureMode::Cancel);
        const std::string allocationName = std::string("oom-") + stageNames[index];
        FailureAtCheckpoint(stages[index], allocationName.c_str(), FailureMode::ThrowAllocation);
    }
    StaleCandidates();
    SettingsAuthorityAndLaterSavedRevision();
    std::printf("SUMMARY adjustment-closure assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

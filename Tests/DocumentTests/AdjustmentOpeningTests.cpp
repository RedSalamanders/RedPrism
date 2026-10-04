#include "AdjustmentOpeningTests.h"

#include "../Support/DocumentFixture.h"
#include "AdjustmentClosureWork.h"
#include "AdjustmentOpeningWork.h"
#include "Executor.h"

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
    std::printf("%s adjustment-opening.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kSourcePixels{0,   0,   0,   255, 32,  64,  96,  255,
                                                     128, 160, 192, 255, 255, 224, 192, 255};
constexpr std::array<AdjustmentKind, 6> kKinds{AdjustmentKind::Hsv,         AdjustmentKind::Levels,
                                               AdjustmentKind::Curves,      AdjustmentKind::Exposure,
                                               AdjustmentKind::GradientMap, AdjustmentKind::Grain};
constexpr std::array<const char*, 6> kNames{"hsv", "levels", "curves", "exposure", "gradient-map", "grain"};
constexpr std::array<FilterKind, 6> kFilterKinds{FilterKind::HueSaturation, FilterKind::Levels,      FilterKind::Curves,
                                                 FilterKind::Exposure,      FilterKind::GradientMap, FilterKind::Grain};

Raster SourceRaster()
{
    return Raster::Make(2, 2, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
}

bool SourceBytesMatch(const Raster& raster)
{
    if (!raster.HasStorage() || raster.Width() != 2 || raster.Height() != 2 ||
        raster.Format() != RasterFormat::Bgra8888)
        return false;
    std::array<std::uint8_t, kSourcePixels.size()> bytes{};
    raster.ReadRegion(0, 0, 2, 2, bytes.data(), 8, 0);
    return bytes == kSourcePixels;
}

LayerAdjustment LiteralAdjustment(AdjustmentKind kind)
{
    LayerAdjustment value;
    value.kind = kind;
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        value.hsvSettings = HueSaturationSettings{};
        value.hsvSettings->range = ColorRange::Reds;
        value.hsvSettings->adjustments[static_cast<std::size_t>(ColorRange::Reds)] = {12, -8, 4};
        break;
    case AdjustmentKind::Levels:
        value.levels.channel = LevelsChannel::Red;
        value.levels.ranges[0] = LevelRange{5, 1.2, 245, 3, 250};
        break;
    case AdjustmentKind::Curves:
        value.curves.channel = LevelsChannel::Green;
        value.curves.channels[1] = {{0, 0}, {80, 96}, {255, 255}};
        break;
    case AdjustmentKind::Exposure:
        value.exposureSettings = ExposureSettings{0.25, 0.05, 1.2};
        break;
    case AdjustmentKind::GradientMap:
        value.gradientMapSettings = GradientMapSettings{{0.08, 0.16, 0.24}, {0.92, 0.84, 0.76}, false};
        break;
    case AdjustmentKind::Grain:
        value.grainSettings = GrainSettings{18, 2.0, 42, 0x13572468u};
        break;
    }
    return value;
}

FilterSettings LiteralPanelSettings(AdjustmentKind kind)
{
    FilterSettings settings;
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        settings.hsv.range = ColorRange::Reds;
        settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Reds)] = {12, -8, 4};
        break;
    case AdjustmentKind::Levels:
        settings.levels.channel = LevelsChannel::Red;
        settings.levels.ranges[0] = LevelRange{5, 1.2, 245, 3, 250};
        break;
    case AdjustmentKind::Curves:
        settings.curves.channel = LevelsChannel::Green;
        settings.curves.channels[1] = {{0, 0}, {80, 96}, {255, 255}};
        break;
    case AdjustmentKind::Exposure:
        settings.exposure = ExposureSettings{0.25, 0.05, 1.2};
        break;
    case AdjustmentKind::GradientMap:
        settings.gradientMap = GradientMapSettings{{0.08, 0.16, 0.24}, {0.92, 0.84, 0.76}, false};
        break;
    case AdjustmentKind::Grain:
        settings.grain = GrainSettings{18, 2.0, 42, 0x13572468u};
        break;
    }
    return settings;
}

FilterSettings LiteralGlobalTemplate()
{
    FilterSettings settings;
    settings.radius = 7.25;
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

struct Fixture final
{
    explicit Fixture(AdjustmentKind kind)
        : kind(kind), original(LiteralAdjustment(kind)), templateSettings(LiteralGlobalTemplate()),
          source(SourceRaster())
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 2;
        document.height = 2;
        ImageLayer adjustment = ImageLayer::Blank(L"Adjustment fixture", Vec2{2, 2});
        adjustment.adjustment = original;
        adjustmentID = adjustment.id;
        adjustment.mask = LayerMask{RasterAsset{MaskRaster(), MaskRaster(), L"Mask fixture"}, true, std::nullopt, true};
        ImageLayer pixels = ImageLayer::Blank(L"Source fixture", Vec2{2, 2});
        baseID = pixels.id;
        pixels.asset = RasterAsset{source, Raster::MakeThumbnail(source), pixels.name};
        document.layers.push_back(std::move(adjustment));
        document.layers.push_back(std::move(pixels));
        session.ReplaceDocument(std::move(document).Seal(), adjustmentID);
        session.filterSettings = templateSettings;

        session.BeginEdit(L"Seed redo");
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& current) { current.layers[1].name = L"Changed source fixture"; });
        session.EndEdit();
        session.Undo();
        baseline = session.Document();
        session.SetAdjustmentEditingID(adjustmentID);
        referencePanelOpened = session.BeginAdjustmentEditing(adjustmentID, source, true);
        referencePanelClosed = session.FinishAdjustmentEditing(false);
        session.SetAdjustmentEditingID(adjustmentID);
    }

    static Raster MaskRaster()
    {
        constexpr std::array<std::uint8_t, 1> pixel{255};
        return Raster::Make(1, 1, RasterFormat::Gray8, pixel.data(), pixel.size());
    }

    AdjustmentKind kind;
    Guid adjustmentID;
    Guid baseID;
    LayerAdjustment original;
    FilterSettings templateSettings;
    Raster source;
    DocumentSession session;
    std::optional<CanvasDocument> baseline;
    bool referencePanelOpened = false;
    bool referencePanelClosed = false;
};

struct LiveState final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    std::optional<Guid> editing;
    std::optional<LayerAdjustment> original;
    bool hasPanel = false;
    bool mask = false;
    std::optional<CropRect> crop;
    FilterSettings globalSettings;
    std::uint64_t generation = 0;
    std::uint64_t serial = 0;
    std::uint64_t sequence = 0;
    Guid revision;
    bool modified = false;
    DocumentHistory::StateView history;
};

LiveState Capture(const DocumentSession& session)
{
    LiveState state;
    state.document = session.Document();
    state.active = session.ActiveLayerID();
    state.editing = session.AdjustmentEditingID();
    state.original = session.AdjustmentOriginal();
    state.hasPanel = session.HasColorEdit();
    state.mask = session.IsMaskSelected();
    state.crop = session.CropFrame();
    state.globalSettings = session.filterSettings;
    state.generation = session.Generation();
    state.serial = session.DocumentSerial();
    state.sequence = session.ColorEditSequence();
    state.revision = session.history.CurrentRevision();
    state.modified = session.history.IsModified();
    state.history = session.history.CaptureState();
    return state;
}

bool SameLiveState(const DocumentSession& session, const LiveState& before)
{
    const auto after = session.history.CaptureState();
    return session.Document() == before.document && session.ActiveLayerID() == before.active &&
           session.AdjustmentEditingID() == before.editing && session.AdjustmentOriginal() == before.original &&
           session.HasColorEdit() == before.hasPanel && session.IsMaskSelected() == before.mask &&
           session.CropFrame() == before.crop && session.filterSettings == before.globalSettings &&
           session.Generation() == before.generation && session.DocumentSerial() == before.serial &&
           session.ColorEditSequence() == before.sequence && session.history.CurrentRevision() == before.revision &&
           session.history.IsModified() == before.modified && session.history.IsCurrent(before.history) &&
           after.Depth() == before.history.Depth() && after.FutureCount() == before.history.FutureCount() &&
           after.PastCount() == before.history.PastCount();
}

std::optional<PreparedAdjustmentOpening> Prepare(Fixture& fixture, Raster below,
                                                 DocumentHistory::PrepareObserver observer = {},
                                                 CancelToken cancellation = {})
{
    auto input = fixture.session.PrepareAdjustmentOpeningInput();
    if (!input)
        return std::nullopt;
    const auto source = fixture.session.Document();
    const auto history = fixture.session.history.CaptureState();
    return DocumentSession::PrepareAdjustmentOpeningPublication(std::move(input.value()), source, std::move(below),
                                                                history, cancellation, observer);
}

std::optional<PreparedAdjustmentClosure> PrepareCancel(Fixture& fixture)
{
    auto input = fixture.session.PrepareAdjustmentClosureInput(false);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareAdjustmentClosurePublication(std::move(input.value()), fixture.session.Document(),
                                                                fixture.session.history.CaptureState());
}

void OpenAndCancelOneKind(AdjustmentKind kind, std::size_t index)
{
    Fixture fixture(kind);
    const std::string name = kNames[index];
    Expect(fixture.referencePanelOpened && fixture.referencePanelClosed && !fixture.session.HasColorEdit() &&
               !fixture.session.AdjustmentOriginal(),
           (name + ".reference-panel-closed").c_str());
    Expect(fixture.baseline == fixture.session.Document() && fixture.session.history.CaptureState().Depth() == 0 &&
               fixture.session.history.CaptureState().FutureCount() == 1,
           (name + ".literal-baseline-and-seeded-redo").c_str());
    Expect(fixture.session.AdjustmentEditingID() == fixture.adjustmentID &&
               fixture.session.filterSettings == fixture.templateSettings,
           (name + ".pending-target-and-global-template").c_str());

    const LiveState before = Capture(fixture.session);
    const auto input = fixture.session.PrepareAdjustmentOpeningInput();
    Expect(input.has_value(), (name + ".input-captured-under-admission").c_str());
    if (!input)
        return;
    Expect(!input->token.target && input->token.sequence == before.sequence &&
               input->documentID == fixture.baseline->id && input->layerID == fixture.adjustmentID &&
               input->active == fixture.session.ActiveLayerID(),
           (name + ".whole-document-input-authority").c_str());
    auto candidate = DocumentSession::PrepareAdjustmentOpeningPublication(
        std::move(AdjustmentOpeningInput{input.value()}), fixture.session.Document(), fixture.source,
        fixture.session.history.CaptureState());
    Expect(candidate.has_value(), (name + ".publication-prepared").c_str());
    Expect(SameLiveState(fixture.session, before), (name + ".prepare-does-not-change-live-state").c_str());
    if (!candidate)
        return;

    const ColorEdit& panel = candidate->Panel();
    Expect(panel.kind == kFilterKinds[index] && panel.layerID == fixture.adjustmentID && panel.deferred,
           (name + ".panel-kind-target-and-deferred").c_str());
    Expect(panel.settings == LiteralPanelSettings(kind), (name + ".panel-settings-independent-literal").c_str());
    Expect(SourceBytesMatch(panel.original) && panel.original == fixture.source,
           (name + ".literal-source-bytes-and-owner").c_str());
    Expect(panel.transform.origin == Vec2{0, 0} && panel.transform.size == Vec2{2, 2} &&
               panel.seed == (kind == AdjustmentKind::Grain ? 0x13572468u : 0u),
           (name + ".document-placement-and-kind-seed").c_str());
    Expect(panel.openingSequence == before.sequence + 1, (name + ".opening-sequence-next").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings,
           (name + ".global-filter-template-unchanged-after-prepare").c_str());
    Expect(fixture.session.IsPreparedAdjustmentOpeningCurrent(candidate.value()),
           (name + ".current-before-apply").c_str());

    const Guid revision = fixture.session.history.CurrentRevision();
    const std::uint64_t generation = fixture.session.Generation();
    const std::uint64_t sequence = fixture.session.ColorEditSequence();
    const bool applied = fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value());
    Expect(applied, (name + ".opening-applied").c_str());
    Expect(fixture.session.Document() == fixture.baseline && fixture.session.Generation() == generation &&
               fixture.session.history.CurrentRevision() == revision,
           (name + ".apply-preserves-document-generation-and-revision").c_str());
    Expect(fixture.session.HasColorEdit() && fixture.session.PendingColorEdit() &&
               fixture.session.PendingColorEdit()->deferred &&
               fixture.session.PendingColorEdit()->openingSequence == sequence + 1 &&
               fixture.session.AdjustmentOriginal() == std::optional(fixture.original),
           (name + ".applied-panel-and-opening-baseline").c_str());
    Expect(fixture.session.history.CaptureState().Depth() == 1 &&
               fixture.session.history.CaptureState().FutureCount() == 1 &&
               fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.RedoName() == L"Seed redo",
           (name + ".named-depth-one-transaction-retains-redo").c_str());
    Expect(fixture.session.filterSettings == fixture.templateSettings &&
               SourceBytesMatch(fixture.session.Document()->layers[1].asset->image),
           (name + ".apply-keeps-template-and-source-bytes").c_str());
    Expect(!fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value()),
           (name + ".second-apply-refused").c_str());

    auto cancel = PrepareCancel(fixture);
    Expect(cancel.has_value() && !cancel->Commits(), (name + ".prepared-cancel-created").c_str());
    if (cancel)
    {
        Expect(fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(cancel.value()),
               (name + ".prepared-cancel-applied").c_str());
        Expect(fixture.session.Document() == fixture.baseline && !fixture.session.HasColorEdit() &&
                   fixture.session.history.CaptureState().Depth() == 0 &&
                   fixture.session.history.CaptureState().FutureCount() == 1 &&
                   fixture.session.history.RedoName() == L"Seed redo" &&
                   fixture.session.history.CurrentRevision() == revision,
               (name + ".prepared-cancel-keeps-baseline-and-redo").c_str());
    }
}

enum class FailureMode
{
    Cancel,
    Allocation,
};

struct CheckpointAction final
{
    DocumentHistory::PrepareStage stage;
    FailureMode mode;
    CancelSource cancellation;
    bool reached = false;

    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& action = *static_cast<CheckpointAction*>(context);
        if (stage != action.stage)
            return;
        action.reached = true;
        if (action.mode == FailureMode::Cancel)
            (void)action.cancellation.Cancel();
        else
            throw std::bad_alloc{};
    }
};

void FailureAtOpeningCheckpoint(DocumentHistory::PrepareStage stage, const char* checkpoint, FailureMode mode)
{
    Fixture fixture(AdjustmentKind::Levels);
    const std::string name = std::string(mode == FailureMode::Cancel ? "cancel." : "oom.") + checkpoint;
    const LiveState before = Capture(fixture.session);
    CheckpointAction action{stage, mode};
    DocumentHistory::PrepareObserver observer{&CheckpointAction::Checkpoint, &action};
    std::optional<PreparedAdjustmentOpening> candidate;
    bool allocationCaught = false;
    try
    {
        candidate = Prepare(fixture, fixture.source, observer,
                            mode == FailureMode::Cancel ? action.cancellation.Token() : CancelToken{});
    }
    catch (const std::bad_alloc&)
    {
        allocationCaught = true;
    }
    Expect(action.reached, (name + ".checkpoint-reached").c_str());
    Expect(mode == FailureMode::Cancel ? (!candidate && !allocationCaught) : (!candidate && allocationCaught),
           (name + (mode == FailureMode::Cancel ? ".cancel-refuses-candidate" : ".bad-alloc-propagates")).c_str());
    Expect(SameLiveState(fixture.session, before), (name + ".no-live-state-change").c_str());
    auto retry = Prepare(fixture, fixture.source);
    Expect(retry.has_value() && fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(retry.value()),
           (name + ".next-valid-attempt-succeeds").c_str());
}

void InvalidInputs()
{
    {
        Fixture fixture(AdjustmentKind::Levels);
        auto input = fixture.session.PrepareAdjustmentOpeningInput();
        Expect(input.has_value(), "invalid.target.input");
        if (input)
        {
            input->layerID = Guid::Generate();
            auto result = DocumentSession::PrepareAdjustmentOpeningPublication(
                std::move(input.value()), fixture.session.Document(), fixture.source,
                fixture.session.history.CaptureState());
            Expect(!result, "invalid.missing-target-refused");
        }
    }
    {
        Fixture fixture(AdjustmentKind::Levels);
        Expect(!Prepare(fixture, Raster{}), "invalid.missing-below-refused");
        auto wrongSize = Raster::Make(1, 2, RasterFormat::Bgra8888, kSourcePixels.data(), 8);
        Expect(!Prepare(fixture, std::move(wrongSize)), "invalid.below-dimensions-refused");
        constexpr std::array<std::uint8_t, 4> grayBytes{0, 64, 128, 255};
        auto gray = Raster::Make(2, 2, RasterFormat::Gray8, grayBytes.data(), grayBytes.size());
        Expect(!Prepare(fixture, std::move(gray)), "invalid.gray-below-refused");
    }
}

void ExpectStale(Fixture& fixture, PreparedAdjustmentOpening& candidate, const char* name)
{
    const LiveState afterChange = Capture(fixture.session);
    Expect(!fixture.session.IsPreparedAdjustmentOpeningCurrent(candidate),
           (std::string(name) + ".stale-current-refused").c_str());
    Expect(!fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate),
           (std::string(name) + ".stale-apply-refused").c_str());
    Expect(SameLiveState(fixture.session, afterChange),
           (std::string(name) + ".refusal-preserves-current-state").c_str());
}

void StaleAuthority()
{
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto input = fixture.session.PrepareAdjustmentOpeningInput();
        Expect(input.has_value(), "stale.token.input");
        if (input)
        {
            ++input->token.generation;
            auto candidate = DocumentSession::PrepareAdjustmentOpeningPublication(
                std::move(input.value()), fixture.session.Document(), fixture.source,
                fixture.session.history.CaptureState());
            Expect(candidate.has_value(), "stale.token.candidate-built");
            if (candidate)
                ExpectStale(fixture, candidate.value(), "stale.token-generation");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.generation.candidate");
        if (candidate)
        {
            fixture.session.EditForTest([](CanvasDocument& document) { document.resolution = 144; });
            ExpectStale(fixture, candidate.value(), "stale.generation");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.opening-id.candidate");
        if (candidate)
        {
            fixture.session.SetAdjustmentEditingID(fixture.baseID);
            ExpectStale(fixture, candidate.value(), "stale.opening-id");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.active.candidate");
        if (candidate)
        {
            fixture.session.SelectLayer(fixture.baseID);
            ExpectStale(fixture, candidate.value(), "stale.active-layer");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.mask.candidate");
        if (candidate)
        {
            fixture.session.SelectLayerTarget(fixture.adjustmentID, true);
            ExpectStale(fixture, candidate.value(), "stale.mask-target");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.crop.candidate");
        if (candidate)
        {
            fixture.session.SetCropFrame(CropRect{0, 0, 1, 2});
            ExpectStale(fixture, candidate.value(), "stale.crop-frame");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "stale.history-epoch.candidate");
        if (candidate)
        {
            const Guid revision = fixture.session.history.CurrentRevision();
            const int depth = fixture.session.history.CaptureState().Depth();
            fixture.session.history.Begin(L"Nested no-op", fixture.session.Document(), fixture.session.ActiveLayerID());
            fixture.session.history.End(fixture.session.Document(), fixture.session.ActiveLayerID());
            Expect(fixture.session.history.CurrentRevision() == revision &&
                       fixture.session.history.CaptureState().Depth() == depth,
                   "stale.history-epoch.fixture-preserves-revision-depth");
            ExpectStale(fixture, candidate.value(), "stale.history-epoch");
        }
    }
    {
        auto sourceOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& source = *sourceOwner;
        auto foreignOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& foreign = *foreignOwner;
        auto candidate = Prepare(source, source.source);
        Expect(candidate.has_value(), "foreign.candidate");
        if (candidate)
        {
            const LiveState before = Capture(foreign.session);
            Expect(!foreign.session.IsPreparedAdjustmentOpeningCurrent(candidate.value()) &&
                       !foreign.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value()),
                   "foreign.candidate-refused");
            Expect(SameLiveState(foreign.session, before), "foreign.refusal-keeps-session");
        }
    }
    {
        auto fixtureOwner = std::make_unique<Fixture>(AdjustmentKind::Levels);
        auto& fixture = *fixtureOwner;
        auto candidate = Prepare(fixture, fixture.source);
        Expect(candidate.has_value(), "moved.candidate");
        if (candidate)
        {
            PreparedAdjustmentOpening moved(std::move(candidate.value()));
            Expect(!fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value()),
                   "moved.from-candidate-refused");
            Expect(fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(moved), "moved.to-candidate-applies");
        }
    }
}

void LaterSavedRevisionAndAbsentSession()
{
    Fixture fixture(AdjustmentKind::Levels);
    auto candidate = Prepare(fixture, fixture.source);
    Expect(candidate.has_value(), "later-saved.candidate");
    if (candidate)
    {
        fixture.session.history.MarkSaved(Guid::Generate());
        Expect(fixture.session.history.IsModified() &&
                   fixture.session.IsPreparedAdjustmentOpeningCurrent(candidate.value()),
               "later-saved.save-marker-independent-of-opening-authority");
        Expect(fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value()), "later-saved.applied");
        Expect(fixture.session.history.IsModified() && fixture.session.Document() == fixture.baseline &&
                   fixture.session.history.CaptureState().Depth() == 1,
               "later-saved.opening-keeps-later-save-marker-and-baseline");
        auto cancel = PrepareCancel(fixture);
        Expect(cancel && fixture.session.ApplyPreparedAdjustmentClosureIfCurrent(cancel.value()) &&
                   fixture.session.history.IsModified() && fixture.session.Document() == fixture.baseline,
               "later-saved.prepared-cancel-keeps-later-save-marker");
    }
    fixture.session.SetAdjustmentEditingID(std::nullopt);
    Expect(!fixture.session.PrepareAdjustmentOpeningInput(), "absent.pending-id-refused");
    DocumentSession empty;
    empty.SetAdjustmentEditingID(Guid::Generate());
    Expect(!empty.PrepareAdjustmentOpeningInput(), "absent.document-refused");
}
} // namespace

int RunAdjustmentOpeningTests()
{
    gFailures = 0;
    gAssertions = 0;
    for (std::size_t index = 0; index < kKinds.size(); ++index)
        OpenAndCancelOneKind(kKinds[index], index);
    InvalidInputs();
    constexpr std::array stages{DocumentHistory::PrepareStage::AdjustmentSnapshot,
                                DocumentHistory::PrepareStage::PendingSnapshot,
                                DocumentHistory::PrepareStage::PendingRecord};
    constexpr std::array names{"adjustment-snapshot", "pending-snapshot", "pending-record"};
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        FailureAtOpeningCheckpoint(stages[index], names[index], FailureMode::Cancel);
        FailureAtOpeningCheckpoint(stages[index], names[index], FailureMode::Allocation);
    }
    StaleAuthority();
    LaterSavedRevisionAndAbsentSession();
    std::printf("SUMMARY adjustment-opening assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

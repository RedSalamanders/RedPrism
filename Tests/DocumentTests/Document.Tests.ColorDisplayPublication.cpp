#include "Document.Tests.ColorDisplayPublication.h"

#include "ColorDisplayPublication.h"
#include "ColorEditHistory.h"
#include "Document.h"
#include "Selection.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s colordisplaypublication.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster GrayRamp()
{
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    return Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

FilterSettings GammaLevels()
{
    FilterSettings settings;
    settings.levels.ranges[0].gamma = 2;
    return settings;
}

FilterSettings ClippedLevels()
{
    FilterSettings settings;
    settings.levels.ranges[0].black = 64;
    settings.levels.ranges[0].white = 128;
    return settings;
}

void InitializeSession(DocumentSession& session, const Raster& image = GrayRamp())
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = image.Width();
    fixture.height = image.Height();
    fixture.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, image.Width(), image.Height())};
    ImageLayer layer = ImageLayer::Blank(L"Publication target",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    const Guid active = layer.id;
    const Raster thumbnail = Raster::MakeThumbnail(image);
    layer.asset = RasterAsset{image, thumbnail, L"Semantic asset name"};
    layer.shapeImage = image;
    fixture.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(fixture).Seal(), active);
}

DocumentSession Session(const Raster& image = GrayRamp())
{
    DocumentSession session;
    InitializeSession(session, image);
    return session;
}

void OpenDeferredLevels(DocumentSession& session, const FilterSettings& settings = GammaLevels())
{
    session.SetColorDisplayJobsEnabled(true);
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(settings, true);
}

ColorDisplayPreviewResult Render(DocumentSession& session, bool lookup = false, CancelToken cancellation = {},
                                 ColorDisplayObserver observer = {})
{
    auto input = session.PrepareColorDisplayPreviewInput(lookup);
    if (!input)
    {
        return {};
    }
    InlineExecutor executor;
    return DocumentSession::PrepareColorDisplayPreviewPublication(std::move(input.value()), executor, cancellation,
                                                                  observer);
}

std::unique_ptr<ColorDisplayPreviewResult> RenderOwned(DocumentSession& session, bool lookup = false,
                                                       CancelToken cancellation = {},
                                                       ColorDisplayObserver observer = {})
{
    return std::make_unique<ColorDisplayPreviewResult>(Render(session, lookup, cancellation, observer));
}

std::unique_ptr<ColorDisplayPreviewResult> PreparePublicationOwned(ColorDisplayPreviewInput&& input,
                                                                   InlineExecutor& executor,
                                                                   CancelToken cancellation = {},
                                                                   ColorDisplayObserver observer = {})
{
    return std::make_unique<ColorDisplayPreviewResult>(
        DocumentSession::PrepareColorDisplayPreviewPublication(std::move(input), executor, cancellation, observer));
}

const ImageLayer* Layer(const CanvasDocument* document, std::size_t index = 0)
{
    return document != nullptr && index < document->layers.size() ? &document->layers[index] : nullptr;
}

std::uint8_t Pixel(const Raster& raster, std::size_t byte)
{
    const std::uint8_t* pixels = raster.Data();
    return pixels == nullptr ? std::uint8_t{0} : pixels[byte];
}

Raster SmallSparseRamp()
{
    constexpr std::array<std::uint8_t, 4> pixel{48, 64, 80, 255};
    const Raster base = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    return Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
}

DocumentSession SessionWithMaskName(const Raster& image, const std::wstring& maskName)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = image.Width();
    fixture.height = image.Height();
    fixture.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, image.Width(), image.Height())};
    ImageLayer layer = ImageLayer::Blank(L"Mask name target",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    const Guid active = layer.id;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Target"};
    const Raster mask = Raster::Make(image.Width(), image.Height(), RasterFormat::Gray8);
    layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), maskName}, true, std::nullopt, false};
    fixture.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), active);
    session.SetColorDisplayJobsEnabled(true);
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    return session;
}

bool PreviewMemoryOverloadsAgree(const ColorEditJobInput& input)
{
    const auto owned = ColorEditPreviewMemoryBytes(input);
    const auto borrowed =
        ColorEditPreviewMemoryBytes(input.edit, input.lookupPreview, input.liveAdjustment.has_value());
    return owned.has_value() && borrowed.has_value() && owned == borrowed;
}

void ScalarHistoryAuthorityAndSaveMarkerCases()
{
    auto historyOwner = std::make_unique<DocumentHistory>();
    auto& history = *historyOwner;
    const DocumentHistory::Authority empty;
    const auto captured = history.CaptureAuthority();
    const auto copied = captured;
    auto otherOwner = std::make_unique<DocumentHistory>();
    auto& other = *otherOwner;
    Expect(!history.IsCurrent(empty), "authority.default-is-invalid");
    Expect(history.IsCurrent(captured) && history.IsCurrent(copied), "authority.copy-keeps-exact-owner");
    Expect(!other.IsCurrent(copied), "authority.copy-rejects-different-owner");

    auto sessionOwner = std::make_unique<DocumentSession>(Session());
    auto& session = *sessionOwner;
    OpenDeferredLevels(session);
    const auto before = session.Document();
    const auto generation = session.Generation();
    const Guid revision = session.history.CurrentRevision();
    const auto authority = session.history.CaptureAuthority();
    auto resultOwner = RenderOwned(session);
    auto& result = *resultOwner;
    Expect(result.publication.has_value(), "history-epoch.publication-captured");
    if (!result.publication)
        return;
    auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
    auto& publication = *publicationOwner;
    session.history.Begin(L"No-op history epoch", session.Document(), session.ActiveLayerID());
    session.history.End(session.Document(), session.ActiveLayerID());
    const bool refused = !session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && !publication.IsConsumed();
    Expect(session.Document() == before && session.Generation() == generation &&
               session.history.CurrentRevision() == revision && !session.history.IsCurrent(authority) && refused,
           "history-epoch-only-change-keeps-source-fixed-and-refuses-publication");

    auto savedOwner = std::make_unique<DocumentSession>(Session());
    auto& saved = *savedOwner;
    OpenDeferredLevels(saved);
    const auto savedAuthority = saved.history.CaptureAuthority();
    auto savedResultOwner = RenderOwned(saved);
    auto& savedResult = *savedResultOwner;
    Expect(savedResult.publication.has_value(), "saved-marker.publication-captured");
    if (!savedResult.publication)
        return;
    auto savedPublicationOwner =
        std::make_unique<PreparedColorDisplayPreview>(std::move(savedResult.publication.value()));
    auto& savedPublication = *savedPublicationOwner;
    saved.history.MarkSaved();
    Expect(saved.history.IsCurrent(savedAuthority) &&
               saved.ApplyPreparedColorDisplayPreviewIfCurrent(savedPublication) && savedPublication.IsConsumed(),
           "saved-marker-does-not-invalidate-current-publication");
}

void MovedPublicationTransfersOwnership()
{
    auto sessionOwner = std::make_unique<DocumentSession>();
    auto& session = *sessionOwner;
    InitializeSession(session);
    OpenDeferredLevels(session);
    auto resultOwner = RenderOwned(session);
    auto& result = *resultOwner;
    bool movedFromRefused = false;
    bool movedToApplied = false;
    if (result.publication)
    {
        auto source = std::move(result.publication.value());
        auto movedOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(source));
        auto& moved = *movedOwner;
        movedFromRefused = source.IsConsumed() && !session.ApplyPreparedColorDisplayPreviewIfCurrent(source);
        movedToApplied = session.ApplyPreparedColorDisplayPreviewIfCurrent(moved);
    }
    Expect(result.publication.has_value() && movedFromRefused && movedToApplied,
           "move.transferred-publication-consumes-source-and-applies-destination");
}

void CrossSessionPublicationRemainsReusableByProducer()
{
    auto producerOwner = std::make_unique<DocumentSession>();
    auto& producer = *producerOwner;
    InitializeSession(producer);
    OpenDeferredLevels(producer);
    auto resultOwner = RenderOwned(producer);
    auto& result = *resultOwner;
    Expect(result.publication.has_value(), "copy.publication-captured");
    if (!result.publication)
        return;
    auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
    auto& publication = *publicationOwner;
    auto copiedSessionOwner = std::make_unique<DocumentSession>(producer);
    auto& copiedSession = *copiedSessionOwner;
    const bool copyRefused =
        !copiedSession.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && !publication.IsConsumed();
    auto foreignOwner = std::make_unique<DocumentSession>();
    auto& foreign = *foreignOwner;
    InitializeSession(foreign);
    const bool foreignRefused =
        !foreign.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && !publication.IsConsumed();
    const bool producerApplied =
        producer.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && publication.IsConsumed();
    Expect(copyRefused && foreignRefused && producerApplied, "session-refusals-preserve-publication-for-producer");
}

void MovedAndCrossSessionPublicationsRemainReusableOnlyByProducer()
{
    MovedPublicationTransfersOwnership();
    CrossSessionPublicationRemainsReusableByProducer();
}

void DisabledBorrowedDisplayPlanner()
{
    {
        DocumentSession disabled = Session();
        OpenDeferredLevels(disabled);
        disabled.SetColorDisplayJobsEnabled(false);
        Expect(!disabled.ColorDisplayPreviewMemoryBytes(false), "planner.disabled-route-refuses");
    }
}

void MissingDocumentBorrowedDisplayPlanner()
{
    {
        DocumentSession missing;
        missing.SetColorDisplayJobsEnabled(true);
        Expect(!missing.ColorDisplayPreviewMemoryBytes(false), "planner.missing-document-refuses");
    }
}

void LiveAdjustmentBorrowedDisplayPlanner()
{
    {
        DocumentSession live = Session();
        live.SetColorDisplayJobsEnabled(true);
        live.AddAdjustment(AdjustmentKind::Levels);
        const auto adjustmentID = live.ActiveLayerID();
        const bool editing = adjustmentID.has_value() && live.BeginAdjustmentEditing(*adjustmentID, GrayRamp(), true);
        Expect(editing && !live.ColorDisplayPreviewMemoryBytes(false), "planner.live-adjustment-refuses");
    }
}

void MaskNameAddsBorrowedDisplayMetadata()
{
    const Raster sparse = SmallSparseRamp();
    DocumentSession shortMask = SessionWithMaskName(sparse, L"Mask");
    DocumentSession longMask = SessionWithMaskName(sparse, std::wstring(128 * 1024 + 1, L'M'));
    const auto shortMaskPlan = shortMask.ColorDisplayPreviewMemoryBytes(false);
    const auto longMaskPlan = longMask.ColorDisplayPreviewMemoryBytes(false);
    Expect(shortMaskPlan && longMaskPlan && longMaskPlan->additionalBytes > shortMaskPlan->additionalBytes &&
               !shortMask.ActiveLayer()->asset->image.IsMaterialized() &&
               !longMask.ActiveLayer()->asset->image.IsMaterialized(),
           "planner.large-mask-name-adds-metadata-charge-without-materializing-pixels");
}

void ValidBorrowedDisplayPlannerAndColorPreviewOverloads()
{
    DocumentSession session = Session(SmallSparseRamp());
    OpenDeferredLevels(session);
    const auto source = session.Document();
    const auto authority = session.history.CaptureAuthority();
    const auto generation = session.Generation();
    const bool materialized = session.ActiveLayer()->asset->image.IsMaterialized();
    const auto plan = session.ColorDisplayPreviewMemoryBytes(false);
    auto levelsInput = session.PrepareColorEditPreview();
    Expect(plan.has_value() && session.Document() == source && session.Generation() == generation &&
               session.history.IsCurrent(authority) &&
               session.ActiveLayer()->asset->image.IsMaterialized() == materialized && levelsInput.has_value() &&
               PreviewMemoryOverloadsAgree(levelsInput.value()),
           "planner.valid-plan-preserves-source-and-level-overloads-agree");
    if (!levelsInput)
        return;
    levelsInput->lookupPreview = true;
    const bool lookupOverloadsAgree = PreviewMemoryOverloadsAgree(levelsInput.value());

    DocumentSession filter = Session();
    filter.BeginColorEdit(FilterKind::GaussianBlur, true);
    FilterSettings blur;
    blur.radius = 1.0;
    filter.UpdateColorEdit(blur, true);
    auto filterInput = filter.PrepareColorEditPreview();
    const bool filterOverloadsAgree = filterInput.has_value() && PreviewMemoryOverloadsAgree(filterInput.value());

    ColorEditJobInput liveInput = levelsInput.value();
    liveInput.liveAdjustment.emplace();
    Expect(lookupOverloadsAgree && filterOverloadsAgree && PreviewMemoryOverloadsAgree(liveInput),
           "planner.lookup-filter-and-live-input-overloads-agree");

    MaskNameAddsBorrowedDisplayMetadata();

    auto shownOwner = RenderOwned(session);
    auto& shown = *shownOwner;
    const bool shownApplied = shown.status == ColorEditJobStatus::Ready && shown.publication.has_value() &&
                              session.ApplyPreparedColorDisplayPreviewIfCurrent(shown.publication.value());
    const auto heldPlan = session.ColorDisplayPreviewMemoryBytes(false);
    Expect(shownApplied && plan && heldPlan && heldPlan->retirementBytes > plan->retirementBytes,
           "planner.published-display-increases-retirement-charge");
}

void BorrowedDisplayPlannerAndColorPreviewOverloads()
{
    DisabledBorrowedDisplayPlanner();
    MissingDocumentBorrowedDisplayPlanner();
    LiveAdjustmentBorrowedDisplayPlanner();
    ValidBorrowedDisplayPlannerAndColorPreviewOverloads();
}

void FailedPublicationFactoryHandlesLatestResult()
{
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        auto initialOwner = RenderOwned(session);
        auto& initial = *initialOwner;
        Expect(initial.status == ColorEditJobStatus::Ready && initial.publication.has_value() &&
                   session.ApplyPreparedColorDisplayPreviewIfCurrent(initial.publication.value()),
               "failure-factory.latest-baseline-preview-is-shown");
        auto input = session.PrepareColorDisplayPreviewInput(false);
        Expect(input.has_value(), "failure-factory.latest-input-captured");
        if (!input)
            return;
        auto failed = DocumentSession::PrepareFailedColorDisplayPreviewPublication(std::move(input.value()),
                                                                                   ColorEditJobStatus::Failed);
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(failed) && failed.IsConsumed() &&
                   session.DisplayDocument() == &session.Document().value(),
               "failure-factory.latest-failure-consumes-false-and-selects-committed");
    }
}

void FailedPublicationFactoryHandlesStaleResult()
{
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        auto oldInput = session.PrepareColorDisplayPreviewInput(false);
        Expect(oldInput.has_value(), "failure-factory.intermediate-input-captured");
        if (!oldInput)
            return;
        auto staleFailure = DocumentSession::PrepareFailedColorDisplayPreviewPublication(std::move(oldInput.value()),
                                                                                         ColorEditJobStatus::NoSource);
        session.UpdateColorEdit(ClippedLevels(), true);
        auto latestOwner = RenderOwned(session);
        auto& latest = *latestOwner;
        Expect(latest.status == ColorEditJobStatus::Ready && latest.publication.has_value() &&
                   session.ApplyPreparedColorDisplayPreviewIfCurrent(latest.publication.value()),
               "failure-factory.current-preview-is-published");
        const CanvasDocument* currentDisplay = session.DisplayDocument();
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(staleFailure) && !staleFailure.IsConsumed() &&
                   session.DisplayDocument() == currentDisplay,
               "failure-factory.stale-intermediate-failure-preserves-current-display");
    }
}

void FailedPublicationFactoryHandlesLatestAndStaleResults()
{
    FailedPublicationFactoryHandlesLatestResult();
    FailedPublicationFactoryHandlesStaleResult();
}

void MovedFromInputCannotCreateApplicableFailurePublication()
{
    DocumentSession session = Session();
    OpenDeferredLevels(session);
    auto input = session.PrepareColorDisplayPreviewInput(false);
    Expect(input.has_value(), "invalid-input.capture-is-valid");
    if (!input)
        return;
    InlineExecutor executor;
    auto validResultOwner = PreparePublicationOwned(std::move(input.value()), executor);
    auto& validResult = *validResultOwner;
    Expect(validResult.status == ColorEditJobStatus::Ready && validResult.publication.has_value(),
           "invalid-input.first-move-prepares-valid-publication");
    if (!validResult.publication)
        return;
    auto validPublicationOwner =
        std::make_unique<PreparedColorDisplayPreview>(std::move(validResult.publication.value()));
    auto& validPublication = *validPublicationOwner;
    auto invalid = DocumentSession::PrepareFailedColorDisplayPreviewPublication(std::move(input.value()),
                                                                                ColorEditJobStatus::Failed);
    Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(invalid) && !invalid.IsConsumed() &&
               session.ApplyPreparedColorDisplayPreviewIfCurrent(validPublication) && validPublication.IsConsumed(),
           "invalid-input.failure-publication-refuses-without-consuming-valid-result");
}

void DeferredLevelsPublishesWholeDisplayWithoutDocumentOrHistoryEdit()
{
    static_assert(!std::is_copy_constructible_v<ColorDisplayPreviewInput>);
    static_assert(!std::is_copy_constructible_v<PreparedColorDisplayPreview>);

    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    Expect(!session.PrepareColorDisplayPreviewInput(false).has_value(), "opt-in.disabled-by-default");
    session.UpdateColorEdit(GammaLevels(), true);
    const auto directInput = session.PrepareColorEditPreview();
    Expect(directInput.has_value(), "direct-api.input-remains-available-before-opt-in");
    if (!directInput)
        return;
    const auto direct = [&]
    {
        InlineExecutor executor;
        return RunColorEditPreviewJob(directInput.value(), executor);
    }();
    Expect(session.ApplyColorEditPreview(direct) &&
               session.PendingColorEdit()->previewSequence == direct.token.sequence,
           "direct-api.preview-still-applies");

    session.SetColorDisplayJobsEnabled(true);
    auto capture = session.PrepareColorDisplayPreviewInput(false);
    Expect(capture.has_value(), "capture.opt-in-enables-complete-route");
    if (!capture)
        return;
    const CanvasDocument committed = session.Document().value();
    const Guid revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    const std::size_t redoCount = session.history.CaptureState().FutureCount();
    auto resultOwner = [&]
    {
        InlineExecutor executor;
        return PreparePublicationOwned(std::move(capture.value()), executor);
    }();
    auto& result = *resultOwner;
    Expect(result.status == ColorEditJobStatus::Ready && result.publication.has_value(),
           "worker.prepares-display-record");
    if (!result.publication)
        return;
    auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
    auto& publication = *publicationOwner;
    Expect(session.Document().value() == committed && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undoCount && session.history.CaptureState().FutureCount() == redoCount &&
               !session.history.IsModified(),
           "before-apply.committed-document-and-history-unchanged");
    Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && publication.IsConsumed(),
           "apply.current-result-is-shown-and-consumed");
    Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication), "apply.repeat-is-no-op");

    const CanvasDocument* shown = session.DisplayDocument();
    const ImageLayer* shownLayer = Layer(shown);
    Expect(shown != nullptr && shown->id == committed.id && shown->width == committed.width &&
               shown->height == committed.height && shown->resolution == committed.resolution &&
               shownLayer != nullptr && shownLayer->id == committed.layers[0].id &&
               shownLayer->name == committed.layers[0].name && shownLayer->transform == committed.layers[0].transform &&
               shownLayer->asset.has_value(),
           "display.complete-document-and-target-metadata-retained");
    Expect(shownLayer != nullptr && shownLayer->asset.has_value() &&
               shownLayer->asset->name == L"Semantic asset name" &&
               shownLayer->asset->thumbnail.Identity() == committed.layers[0].asset->thumbnail.Identity() &&
               shownLayer->shapeImage.Identity() == committed.layers[0].shapeImage.Identity() &&
               &shown->layers[0] != &committed.layers[0],
           "display.explicitly-preserves-fields-omitted-from-layer-equality");
    Expect(shown != nullptr && shown->selection.has_value() && committed.selection.has_value() &&
               shown->selection->loops.Identity() == committed.selection->loops.Identity(),
           "display.selection-root-is-shared");
    Expect(shownLayer != nullptr && shownLayer->asset.has_value() &&
               shownLayer->asset->image != committed.layers[0].asset->image && shownLayer->asset->image.Width() == 4 &&
               shownLayer->asset->image.Height() == 1,
           "display.target-image-is-rendered-preview");
    // Independent legacy Levels gamma expectation also recorded by ColorEditWorkTests: 0, 64, 128, 255 ->
    // 0, 128, 181, 255 (one-byte tolerance for the two interior values).
    Expect(shownLayer != nullptr && shownLayer->asset.has_value() && Pixel(shownLayer->asset->image, 0) == 0 &&
               std::abs(static_cast<int>(Pixel(shownLayer->asset->image, 4)) - 128) <= 1 &&
               std::abs(static_cast<int>(Pixel(shownLayer->asset->image, 8)) - 181) <= 1 &&
               Pixel(shownLayer->asset->image, 12) == 255,
           "display.levels-pixels-match-independent-legacy-golden");
    Expect(session.Document().value() == committed && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undoCount && session.history.CaptureState().FutureCount() == redoCount,
           "after-apply.preview-does-not-edit-committed-document-or-history");
    Expect(session.PendingColorEdit()->settings == GammaLevels() && session.PendingColorEdit()->previewSequence != 0 &&
               session.PendingColorEdit()->previewRaster.HasStorage(),
           "panel-retains-rendered-preview-metadata");
}

void LookupAndMaskPublicationKeepTheirSourceSemantics()
{
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session, true);
        auto& result = *resultOwner;
        Expect(result.status == ColorEditJobStatus::Ready && result.publication.has_value(),
               "lookup.worker-prepares-result");
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication), "lookup.publication-applies");
        const ImageLayer* displayed = Layer(session.DisplayDocument());
        const ColorEdit& panel = session.PendingColorEdit().value();
        Expect(panel.previewLookup != nullptr && panel.previewLookup->adjustment.kind == AdjustmentKind::Levels &&
                   panel.previewSource == panel.original && !panel.previewRaster.HasStorage(),
               "lookup.panel-retains-source-and-lookup-instead-of-rendered-raster");
        Expect(displayed != nullptr && displayed->asset.has_value() && displayed->asset->image == panel.original &&
                   session.DisplayDocument()->id == session.Document()->id,
               "lookup.complete-preview-document-uses-original-layer-raster");
    }
    {
        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = 4;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Masked target", Vec2{4, 1});
        const Guid active = layer.id;
        const Raster image = GrayRamp();
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Masked target"};
        const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8);
        layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"}, true, std::nullopt, false};
        const LayerTransform originalPlacement = layer.transform;
        fixture.layers.push_back(std::move(layer));
        DocumentSession session;
        session.ReplaceDocument(std::move(fixture).Seal(), active);
        session.SetColorDisplayJobsEnabled(true);
        session.BeginColorEdit(FilterKind::GaussianBlur, true);
        FilterSettings blur;
        blur.radius = 1.5;
        session.UpdateColorEdit(blur, true);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        Expect(result.status == ColorEditJobStatus::Ready && result.publication.has_value(),
               "mask.worker-prepares-grown-preview");
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication), "mask.grown-display-applies");
        const ImageLayer* displayed = Layer(session.DisplayDocument());
        Expect(displayed != nullptr && displayed->transform != originalPlacement && displayed->mask.has_value() &&
                   displayed->mask->placement == std::optional<LayerTransform>{originalPlacement},
               "mask.unplaced-mask-is-pinned-to-its-original-layer-location");
        Expect(session.Document()->layers[0].mask->placement == std::nullopt &&
                   session.Document()->layers[0].transform == originalPlacement,
               "mask-display-does-not-mutate-committed-mask-or-transform");
    }
}

void IntermediateAndOutOfOrderResultsRespectCurrentPanelAndDisplay()
{
    auto sessionOwner = std::make_unique<DocumentSession>();
    auto& session = *sessionOwner;
    InitializeSession(session);
    OpenDeferredLevels(session);
    auto olderInput = session.PrepareColorDisplayPreviewInput(false);
    Expect(olderInput.has_value(), "ordering.older-input-captured");
    if (!olderInput)
        return;
    InlineExecutor executor;
    auto olderOwner = PreparePublicationOwned(std::move(olderInput.value()), executor);
    auto& older = *olderOwner;
    Expect(older.publication.has_value(), "ordering.older-result-completed");
    if (!older.publication)
        return;
    auto staleInput = session.PrepareColorDisplayPreviewInput(false);
    Expect(staleInput.has_value(), "ordering.second-old-request-captured");
    if (!staleInput)
        return;
    auto staleResultOwner = PreparePublicationOwned(std::move(staleInput.value()), executor);
    auto& staleResult = *staleResultOwner;
    Expect(staleResult.publication.has_value(), "ordering.second-old-result-completed");
    if (!staleResult.publication)
        return;

    session.UpdateColorEdit(ClippedLevels(), true);
    auto intermediateOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(older.publication.value()));
    auto& intermediate = *intermediateOwner;
    Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(intermediate), "ordering.intermediate-result-is-shown");
    const ImageLayer* intermediateLayer = Layer(session.DisplayDocument());
    Expect(session.PendingColorEdit()->settings == ClippedLevels() && intermediateLayer != nullptr &&
               intermediateLayer->asset.has_value() && Pixel(intermediateLayer->asset->image, 4) == 128 &&
               Pixel(intermediateLayer->asset->image, 8) == 181,
           "ordering.intermediate-display-does-not-roll-back-current-controls");

    auto newerInput = session.PrepareColorDisplayPreviewInput(false);
    Expect(newerInput.has_value() && session.PendingColorEdit()->settings == ClippedLevels(),
           "ordering.latest-settings-remain-live-during-worker");
    if (!newerInput)
        return;
    auto newerOwner = PreparePublicationOwned(std::move(newerInput.value()), executor);
    auto& newer = *newerOwner;
    Expect(newer.publication.has_value(), "ordering.newer-result-completed");
    if (!newer.publication)
        return;
    auto latestOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(newer.publication.value()));
    auto& latest = *latestOwner;
    Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(latest), "ordering.newer-result-applies");
    const CanvasDocument* latestDisplay = session.DisplayDocument();
    const ImageLayer* latestLayer = Layer(latestDisplay);
    Expect(session.PendingColorEdit()->settings == ClippedLevels() && latestLayer != nullptr &&
               latestLayer->asset.has_value() && Pixel(latestLayer->asset->image, 4) == 0 &&
               Pixel(latestLayer->asset->image, 8) == 255,
           "ordering.latest-display-matches-latest-settings");

    auto staleOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(staleResult.publication.value()));
    auto& stale = *staleOwner;
    Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(stale), "ordering.older-completed-result-is-rejected");
    const ImageLayer* afterStale = Layer(session.DisplayDocument());
    Expect(session.PendingColorEdit()->settings == ClippedLevels() && afterStale != nullptr &&
               afterStale->asset.has_value() && Pixel(afterStale->asset->image, 4) == 0 &&
               Pixel(afterStale->asset->image, 8) == 255 && session.DisplayDocument() == latestDisplay,
           "ordering.stale-result-preserves-newer-display-and-controls");
}

void SourceHistoryReplacementAndForeignAuthoritiesRejectPublication()
{
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers[0].opacity = 0.5; });
        const CanvasDocument changed = session.Document().value();
        const auto revision = session.history.CurrentRevision();
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) &&
                   session.Document().value() == changed && session.history.CurrentRevision() == revision,
               "stale-source-metadata-change-refuses-whole-display");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        const Guid target = session.ActiveLayer()->id;
        session.RenameLayer(target, L"Renamed during worker");
        const CanvasDocument changed = session.Document().value();
        const int undoCount = session.history.UndoCount();
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) &&
                   session.Document().value() == changed && session.history.UndoCount() == undoCount &&
                   session.Document()->layers[0].name == L"Renamed during worker",
               "stale-history-refuses-without-overwriting-current-source");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        const CanvasDocument replacement = session.Document().value();
        session.ReplaceDocument(replacement, session.ActiveLayer()->id);
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication),
               "replacement-same-content-new-document-epoch-rejects-old-display");
    }
    {
        auto producerOwner = std::make_unique<DocumentSession>();
        auto& producer = *producerOwner;
        InitializeSession(producer);
        auto foreignOwner = std::make_unique<DocumentSession>();
        auto& foreign = *foreignOwner;
        InitializeSession(foreign);
        OpenDeferredLevels(producer);
        OpenDeferredLevels(foreign);
        auto resultOwner = RenderOwned(producer);
        auto& result = *resultOwner;
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        const auto before = foreign.Document().value();
        Expect(!foreign.ApplyPreparedColorDisplayPreviewIfCurrent(publication) &&
                   foreign.Document().value() == before && foreign.history.UndoCount() == 0,
               "foreign-session-rejects-result-without-changing-its-state");
    }
}

void ExistingLayerScopedCommitStillAppliesAfterUnrelatedMetadataChange()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), false);
    const auto input = session.PrepareColorEditCommit();
    Expect(input.has_value(), "layer-commit.input-captured");
    if (!input)
        return;
    InlineExecutor executor;
    auto result = RunColorEditJob(input.value(), executor);
    const Guid target = session.ActiveLayer()->id;
    session.RenameLayer(target, L"Metadata changed during commit");
    const int beforeUndo = session.history.UndoCount();
    Expect(result.status == ColorEditJobStatus::Ready && session.ApplyColorEditResult(result),
           "layer-commit.target-scoped-result-remains-applicable");
    Expect(session.Document()->layers[0].name == L"Metadata changed during commit" &&
               session.history.UndoCount() == beforeUndo + 1,
           "layer-commit-preserves-unrelated-metadata-and-records-one-edit");
}

void PreviewOffCloseAndReopenInvalidateCompletedWork()
{
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        Expect(result.publication.has_value(), "off.result-prepared");
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        session.UpdateColorEdit(GammaLevels(), false);
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) &&
                   session.DisplayDocument() == &session.Document().value(),
               "off-in-flight-result-refuses-and-selects-committed-document");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        session.UpdateColorEdit(FilterSettings{}, true);
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) &&
                   session.DisplayDocument() == &session.Document().value(),
               "identity-in-flight-result-refuses-and-selects-committed-document");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        auto& session = *sessionOwner;
        InitializeSession(session);
        OpenDeferredLevels(session);
        auto resultOwner = RenderOwned(session);
        auto& result = *resultOwner;
        Expect(result.publication.has_value(), "reopen.result-prepared");
        if (!result.publication)
            return;
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
        auto& publication = *publicationOwner;
        session.CancelColorEdit();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(publication) && session.HasColorEdit() &&
                   session.DisplayDocument() == &session.Document().value(),
               "close-reopen-rejects-old-result-and-selects-committed-source");
    }
}

struct ObserverState final
{
    ColorDisplayStage stopAt = ColorDisplayStage::Publication;
    CancelSource* cancellation = nullptr;
    bool throwAllocation = false;
    bool reached = false;
};

void Observe(void* context, ColorDisplayStage stage)
{
    auto& state = *static_cast<ObserverState*>(context);
    if (stage != state.stopAt)
        return;
    state.reached = true;
    if (state.cancellation != nullptr)
        (void)state.cancellation->Cancel();
    if (state.throwAllocation)
        throw std::bad_alloc{};
}

void CancellationAndAllocationFailureLeaveRetryableState()
{
    auto sessionOwner = std::make_unique<DocumentSession>();
    auto& session = *sessionOwner;
    InitializeSession(session);
    OpenDeferredLevels(session);
    const CanvasDocument before = session.Document().value();
    const Guid revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    {
        auto input = session.PrepareColorDisplayPreviewInput(false);
        if (!input)
            return;
        CancelSource cancellation;
        ObserverState state;
        state.cancellation = &cancellation;
        InlineExecutor executor;
        auto cancelledOwner = PreparePublicationOwned(std::move(input.value()), executor, cancellation.Token(),
                                                      ColorDisplayObserver{&state, Observe});
        const auto& cancelled = *cancelledOwner;
        Expect(state.reached && cancelled.status == ColorEditJobStatus::Cancelled && !cancelled.publication,
               "fault.observer-cancellation-discards-private-display");
        Expect(session.Document().value() == before && session.history.CurrentRevision() == revision &&
                   session.history.UndoCount() == undoCount,
               "fault.cancellation-leaves-document-and-history-unchanged");
    }
    {
        auto input = session.PrepareColorDisplayPreviewInput(false);
        if (!input)
            return;
        ObserverState state;
        state.throwAllocation = true;
        InlineExecutor executor;
        bool caught = false;
        try
        {
            (void)PreparePublicationOwned(std::move(input.value()), executor, {},
                                          ColorDisplayObserver{&state, Observe});
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        Expect(state.reached && caught, "fault.real-observer-bad-alloc-propagates-to-worker-boundary");
        Expect(session.Document().value() == before && session.history.CurrentRevision() == revision &&
                   session.history.UndoCount() == undoCount,
               "fault.bad-alloc-leaves-document-and-history-unchanged");
    }
    auto retryOwner = RenderOwned(session);
    auto& retry = *retryOwner;
    Expect(retry.status == ColorEditJobStatus::Ready && retry.publication.has_value(),
           "fault.same-panel-retry-succeeds");
    if (retry.publication)
    {
        auto publicationOwner = std::make_unique<PreparedColorDisplayPreview>(std::move(retry.publication.value()));
        auto& publication = *publicationOwner;
        Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication), "fault.retry-publishes-current-result");
    }
}

void MovedInputAndSelectionRootBindingAreCovered()
{
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        auto input = session.PrepareColorDisplayPreviewInput(false);
        if (!input)
            return;
        ColorDisplayPreviewInput moved(std::move(input.value()));
        InlineExecutor executor;
        auto resultOwner = PreparePublicationOwned(std::move(moved), executor);
        auto& result = *resultOwner;
        Expect(result.status == ColorEditJobStatus::Ready && result.publication.has_value(),
               "move.transferred-input-renders");
        if (result.publication)
        {
            auto publicationOwner =
                std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
            auto& publication = *publicationOwner;
            Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication), "move.transferred-input-publishes");
        }
        auto spentOwner = PreparePublicationOwned(std::move(input.value()), executor);
        const auto& spent = *spentOwner;
        Expect(spent.status == ColorEditJobStatus::Failed && !spent.publication,
               "move.moved-from-input-cannot-start-second-publication");
    }
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        const auto priorLoops = session.Document()->selection->loops.Identity();
        EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 4, 1)}; });
        Expect(session.Document()->selection->loops.Identity() != priorLoops,
               "selection.capture-source-has-independent-equal-root");
        auto input = session.PrepareColorDisplayPreviewInput(false);
        if (!input)
            return;
        InlineExecutor executor;
        auto resultOwner = PreparePublicationOwned(std::move(input.value()), executor);
        auto& result = *resultOwner;
        Expect(result.status == ColorEditJobStatus::Ready && result.publication.has_value(),
               "selection.worker-binds-equal-independent-root-to-captured-source");
        if (result.publication)
        {
            auto publicationOwner =
                std::make_unique<PreparedColorDisplayPreview>(std::move(result.publication.value()));
            auto& publication = *publicationOwner;
            Expect(session.ApplyPreparedColorDisplayPreviewIfCurrent(publication),
                   "selection.equal-independent-root-still-publishes");
        }
    }
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 3, 1)}; });
        auto input = session.PrepareColorDisplayPreviewInput(false);
        if (!input)
            return;
        InlineExecutor executor;
        auto resultOwner = PreparePublicationOwned(std::move(input.value()), executor);
        auto& result = *resultOwner;
        Expect(result.status == ColorEditJobStatus::Failed && !result.publication &&
                   session.Document()->selection->loops.size() == 1,
               "selection.changed-geometry-refuses-display-preparation");
    }
}

void InvalidatedPreviewOwnersStayHeldUntilPreparedTransfer()
{
    {
        DocumentSession session = Session();
        OpenDeferredLevels(session);
        auto firstOwner = RenderOwned(session, true);
        auto& first = *firstOwner;
        const bool applied =
            first.publication && session.ApplyPreparedColorDisplayPreviewIfCurrent(first.publication.value());
        Expect(applied && session.ColorLookupPreviewReady(), "owner-hold.lookup-initial-publication-is-visible");
        if (!applied)
            return;
        const auto sourceIdentity = session.PendingColorEdit()->previewSource.Identity();
        const auto coverageIdentity = session.PendingColorEdit()->previewCoverage.Identity();
        std::weak_ptr<const AdjustmentLookup> precedingLookup = session.PendingColorEdit()->previewLookup;
        first.publication.reset();
        session.UpdateColorEdit(GammaLevels(), false);
        Expect(!precedingLookup.expired() && session.PendingColorEdit()->previewSource.Identity() == sourceIdentity &&
                   session.PendingColorEdit()->previewCoverage.Identity() == coverageIdentity &&
                   !session.ColorLookupPreviewReady() && session.PendingColorEdit()->previewSequence == 0 &&
                   session.DisplayDocument() == &session.Document().value(),
               "owner-hold.off-retains-invisible-cache-owners");
        session.UpdateColorEdit(FilterSettings{}, true);
        Expect(!precedingLookup.expired() && !session.ColorLookupPreviewReady() &&
                   session.DisplayDocument() == &session.Document().value(),
               "owner-hold.identity-retains-invisible-lookup");
        session.UpdateColorEdit(ClippedLevels(), true);
        auto nextOwner = RenderOwned(session, true);
        auto& next = *nextOwner;
        const bool replaced =
            next.publication && session.ApplyPreparedColorDisplayPreviewIfCurrent(next.publication.value());
        Expect(replaced && session.ColorLookupPreviewReady() && !precedingLookup.expired(),
               "owner-hold.replacement-transfers-old-lookup-to-prepared-owner");
        next.publication.reset();
        Expect(precedingLookup.expired(), "owner-hold.old-lookup-releases-with-prepared-aggregate");
    }
    {
        DocumentSession session = Session();
        session.SetColorDisplayJobsEnabled(true);
        session.BeginColorEdit(FilterKind::GaussianBlur, true);
        auto firstOwner = RenderOwned(session);
        auto& first = *firstOwner;
        const bool applied =
            first.publication && session.ApplyPreparedColorDisplayPreviewIfCurrent(first.publication.value());
        Expect(applied && session.PendingColorEdit()->grownImage.HasStorage() &&
                   !session.PendingColorEdit()->retiredGrownImage.HasStorage(),
               "owner-hold.blur-initial-grown-cache-is-installed");
        if (!applied)
            return;
        const auto oldGrown = session.PendingColorEdit()->grownImage.Identity();
        const auto oldSource = session.PendingColorEdit()->previewSource.Identity();
        first.publication.reset();
        FilterSettings larger;
        larger.radius = 2;
        session.UpdateColorEdit(larger, true);
        Expect(!session.PendingColorEdit()->grownImage.HasStorage() &&
                   session.PendingColorEdit()->retiredGrownImage.Identity() == oldGrown &&
                   session.PendingColorEdit()->previewSource.Identity() == oldSource &&
                   session.DisplayDocument() == &session.Document().value(),
               "owner-hold.growth-moves-old-grown-owner-to-fixed-slot");
        larger.radius = 3;
        session.UpdateColorEdit(larger, true);
        Expect(!session.PendingColorEdit()->grownImage.HasStorage() &&
                   session.PendingColorEdit()->retiredGrownImage.Identity() == oldGrown &&
                   session.PendingColorEdit()->previewSource.Identity() == oldSource,
               "owner-hold.further-growth-retains-one-old-grown-owner");
        ColorEdit withoutHeld = session.PendingColorEdit().value();
        withoutHeld.retiredGrownImage = {};
        Expect(ColorEditHeldMemoryBytes(session.PendingColorEdit().value()) > ColorEditHeldMemoryBytes(withoutHeld),
               "owner-hold.retirement-planner-includes-invalidated-grown-owner");
        auto nextOwner = RenderOwned(session);
        auto& next = *nextOwner;
        const bool replaced =
            next.publication && session.ApplyPreparedColorDisplayPreviewIfCurrent(next.publication.value());
        Expect(replaced && session.PendingColorEdit()->grownImage.HasStorage() &&
                   session.PendingColorEdit()->grownImage.Identity() != oldGrown &&
                   !session.PendingColorEdit()->retiredGrownImage.HasStorage() &&
                   session.DisplayDocument() != &session.Document().value(),
               "owner-hold.matching-publication-drains-old-grown-slot");
    }
}

} // namespace

int RunColorDisplayPublicationTests()
{
    gFailures = 0;
    {
        auto sessionOwner = std::make_unique<DocumentSession>(Session());
        auto& session = *sessionOwner;
        OpenDeferredLevels(session);
        const auto openingIdentity = session.Selection()->loops.Identity();
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 4, 1)}; });
        Expect(session.Selection()->loops.Identity() != openingIdentity,
               "failure-factory.equal-outline-has-distinct-root");
        auto input = session.PrepareColorDisplayPreviewInput(false);
        Expect(input.has_value(), "failure-factory.distinct-selection-input-captured");
        if (input)
        {
            auto failed = DocumentSession::PrepareFailedColorDisplayPreviewPublication(std::move(input.value()),
                                                                                       ColorEditJobStatus::Failed);
            const auto before = session.Document();
            Expect(!session.ApplyPreparedColorDisplayPreviewIfCurrent(failed) && !failed.IsConsumed() &&
                       session.Document() == before && !session.PendingColorEdit()->previewSequence,
                   "failure-factory.distinct-selection-refuses-owner-geometry-comparison");
        }
    }
    InvalidatedPreviewOwnersStayHeldUntilPreparedTransfer();
    ScalarHistoryAuthorityAndSaveMarkerCases();
    MovedAndCrossSessionPublicationsRemainReusableOnlyByProducer();
    BorrowedDisplayPlannerAndColorPreviewOverloads();
    FailedPublicationFactoryHandlesLatestAndStaleResults();
    MovedFromInputCannotCreateApplicableFailurePublication();
    DeferredLevelsPublishesWholeDisplayWithoutDocumentOrHistoryEdit();
    LookupAndMaskPublicationKeepTheirSourceSemantics();
    IntermediateAndOutOfOrderResultsRespectCurrentPanelAndDisplay();
    SourceHistoryReplacementAndForeignAuthoritiesRejectPublication();
    ExistingLayerScopedCommitStillAppliesAfterUnrelatedMetadataChange();
    PreviewOffCloseAndReopenInvalidateCompletedWork();
    CancellationAndAllocationFailureLeaveRetryableState();
    MovedInputAndSelectionRootBindingAreCovered();
    return gFailures;
}

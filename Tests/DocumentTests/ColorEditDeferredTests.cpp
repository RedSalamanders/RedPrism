#include "ColorEditDeferredTests.h"

#include "Adjust.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "Selection.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s coloreditdeferred.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster FourGrayPixels()
{
    constexpr std::array<std::uint8_t, 16> pixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                  128, 128, 128, 255, 255, 255, 255, 255};
    return Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession Session(const Raster& image = FourGrayPixels())
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = image.Width();
    document.height = image.Height();
    ImageLayer layer = ImageLayer::Blank(L"Deferred fixture",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    const Guid active = layer.id;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Deferred fixture"};
    document.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(document), active);
    return session;
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

ColorEditPreviewResult Preview(const ColorEditJobInput& input, CancelToken cancellation = {})
{
    InlineExecutor executor;
    return RunColorEditPreviewJob(input, executor, cancellation);
}

ColorEditHistogramResult Histogram(const ColorEditJobInput& input, CancelToken cancellation = {})
{
    InlineExecutor executor;
    return RunColorEditHistogramJob(input, executor, cancellation);
}

ColorEditJobResult Commit(const ColorEditJobInput& input, CancelToken cancellation = {})
{
    InlineExecutor executor;
    return RunColorEditJob(input, executor, cancellation);
}

void DeferredBeginAndUpdateOnlyCaptureHandles()
{
    const Raster source = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, FourGrayPixels(), 0, 0, {}, 0);
    DocumentSession session = Session(source);
    const int historyBefore = session.history.UndoCount();
    const std::uint64_t sequenceBefore = session.ColorEditSequence();
    session.BeginColorEdit(FilterKind::Levels, true);
    const auto& opening = session.PendingColorEdit();
    const bool beganDeferred = opening.has_value() && opening->deferred && opening->original == source &&
                               opening->sourceToken.has_value() && !opening->histogramReady &&
                               !opening->previewRaster.HasStorage() && !opening->previewSource.HasStorage();
    Expect(beganDeferred && !source.IsMaterialized() && session.history.UndoCount() == historyBefore &&
               session.ColorEditSequence() > sequenceBefore,
           "begin.captures-opening-without-pixel-work-or-history");
    if (!beganDeferred)
    {
        return;
    }

    const std::uint64_t openingSequence = opening->openingSequence;
    const JobToken openingToken = opening->sourceToken.value();
    session.UpdateColorEdit(GammaLevels(), true);
    const auto& updated = session.PendingColorEdit();
    Expect(updated.has_value() && updated->openingSequence == openingSequence && updated->sourceToken.has_value() &&
               updated->sourceToken->sessionIdentity == openingToken.sessionIdentity &&
               updated->sourceToken->documentSerial == openingToken.documentSerial &&
               updated->sourceToken->generation == openingToken.generation &&
               updated->sourceToken->restoreSerial == openingToken.restoreSerial &&
               updated->sourceToken->sequence == openingToken.sequence && updated->settings == GammaLevels() &&
               !updated->previewRaster.HasStorage() && !updated->histogramReady && !source.IsMaterialized() &&
               session.history.UndoCount() == historyBefore,
           "update.captures-settings-without-resetting-opening-or-running-work");
}

void HistogramUsesOpeningPixelsAndSurvivesTicks()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    const auto first = session.PrepareColorEditPreview();
    Expect(first.has_value(), "histogram.opening-input-prepared");
    if (!first)
    {
        return;
    }
    const auto measured = Histogram(first.value());
    session.UpdateColorEdit(GammaLevels(), true);
    const bool applied = session.ApplyColorEditHistogram(measured);
    const auto& pending = session.PendingColorEdit();
    const bool literalBins = measured.status == ColorEditJobStatus::Ready && measured.histogram[0][0] == 1.0 &&
                             measured.histogram[0][64] == 1.0 && measured.histogram[0][128] == 1.0 &&
                             measured.histogram[0][255] == 1.0 && measured.histogram[1][64] == 1.0 &&
                             measured.histogram[2][128] == 1.0 && measured.histogram[3][255] == 1.0;
    Expect(literalBins && applied && pending.has_value() && pending->histogramReady &&
               pending->settings == GammaLevels() && pending->histogram == measured.histogram,
           "histogram.opening-epoch-result-applies-after-newer-settings-tick");
}

void HistogramCannotCrossPanelOrDocumentEpoch()
{
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "histogram-cancel.input-prepared");
        if (!input)
        {
            return;
        }
        CancelSource cancellation;
        (void)cancellation.Cancel();
        const auto cancelled = Histogram(input.value(), cancellation.Token());
        Expect(cancelled.status == ColorEditJobStatus::Cancelled && !session.ApplyColorEditHistogram(cancelled) &&
                   !session.PendingColorEdit()->histogramReady,
               "histogram-pre-cancelled-work-does-not-apply");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "histogram-reopen.input-prepared");
        if (!input)
        {
            return;
        }
        const auto measured = Histogram(input.value());
        session.CancelColorEdit();
        session.BeginColorEdit(FilterKind::Levels, true);
        Expect(!session.ApplyColorEditHistogram(measured) && session.HasColorEdit(FilterKind::Levels) &&
                   !session.PendingColorEdit()->histogramReady,
               "histogram-reopen.old-opening-cannot-populate-new-panel");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "histogram-document.input-prepared");
        if (!input)
        {
            return;
        }
        const auto measured = Histogram(input.value());
        const CanvasDocument replacement = session.Document().value();
        session.ReplaceDocument(replacement, session.ActiveLayer()->id);
        session.BeginColorEdit(FilterKind::Levels, true);
        Expect(!session.ApplyColorEditHistogram(measured) && session.HasColorEdit(FilterKind::Levels) &&
                   !session.PendingColorEdit()->histogramReady,
               "histogram-document-replacement-rejects-old-opening");
    }
}

void OlderPreviewDisplaysWithoutReplacingSettingsOrBeingReused()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    const auto oldInput = session.PrepareColorEditPreview();
    Expect(oldInput.has_value(), "late-preview.old-input-prepared");
    if (!oldInput)
    {
        return;
    }
    const auto oldPreview = Preview(oldInput.value());
    session.UpdateColorEdit(ClippedLevels(), true);
    const bool displayed = session.ApplyColorEditPreview(oldPreview);
    const auto& pending = session.PendingColorEdit();
    Expect(displayed && pending.has_value() && pending->settings == ClippedLevels() &&
               pending->previewSequence == oldInput->token.sequence && pending->previewRaster.HasStorage(),
           "late-preview.displays-older-frame-with-current-settings");

    const auto commitInput = session.PrepareColorEditCommit();
    Expect(commitInput.has_value(), "late-preview.current-commit-input-prepared");
    if (!commitInput)
    {
        return;
    }
    const auto result = Commit(commitInput.value());
    const auto* pixels = result.image.Data();
    Expect(result.status == ColorEditJobStatus::Ready && !result.reusedPreview && pixels != nullptr && pixels[0] == 0 &&
               pixels[4] == 0 && pixels[8] == 255 && pixels[12] == 255,
           "late-preview.older-frame-is-never-reused-by-newer-commit");
}

void PreviewPublicationHonorsInvalidationAndEpoch()
{
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "preview-off-on.input-prepared");
        if (!input)
        {
            return;
        }
        const auto result = Preview(input.value());
        session.UpdateColorEdit(GammaLevels(), false);
        session.UpdateColorEdit(GammaLevels(), true);
        Expect(!session.ApplyColorEditPreview(result) && !session.PendingColorEdit()->previewRaster.HasStorage() &&
                   session.PendingColorEdit()->previewInvalidatedSequence >= input->token.sequence,
               "preview-off-on-invalidates-inflight-publication");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "preview-cancel.input-prepared");
        if (!input)
        {
            return;
        }
        CancelSource cancellation;
        (void)cancellation.Cancel();
        const auto result = Preview(input.value(), cancellation.Token());
        Expect(result.status == ColorEditJobStatus::Cancelled && !session.ApplyColorEditPreview(result) &&
                   !session.PendingColorEdit()->previewRaster.HasStorage(),
               "preview-pre-cancelled-work-does-not-publish");
        session.CancelColorEdit();
        Expect(!session.ApplyColorEditPreview(Preview(input.value())) && !session.HasColorEdit(),
               "preview-cancelled-panel-rejects-late-publication");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        const auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "preview-current-mismatch.input-prepared");
        if (!input)
        {
            return;
        }
        auto result = Preview(input.value());
        result.edit.settings = ClippedLevels();
        Expect(!session.ApplyColorEditPreview(result) && !session.PendingColorEdit()->previewRaster.HasStorage(),
               "preview-current-tick-rejects-mismatched-settings");
    }
}

void CurrentPreviewCanBeReusedOnlyForItsExactRequest()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    const auto firstInput = session.PrepareColorEditPreview();
    Expect(firstInput.has_value(), "reuse-current.first-input-prepared");
    if (!firstInput)
    {
        return;
    }
    const auto firstPreview = Preview(firstInput.value());
    Expect(firstPreview.status == ColorEditJobStatus::Ready && session.ApplyColorEditPreview(firstPreview),
           "reuse-current.first-preview-published");
    const auto exactCommit = session.PrepareColorEditCommit();
    Expect(exactCommit.has_value(), "reuse-current.exact-commit-prepared");
    if (!exactCommit)
    {
        return;
    }
    const auto reused = Commit(exactCommit.value());
    Expect(reused.status == ColorEditJobStatus::Ready && reused.reusedPreview,
           "reuse-current.exact-current-preview-reused");

    session.UpdateColorEdit(ClippedLevels(), true);
    const auto newerInput = session.PrepareColorEditCommit();
    Expect(newerInput.has_value(), "reuse-current.newer-commit-prepared");
    if (!newerInput)
    {
        return;
    }
    const auto recomputed = Commit(newerInput.value());
    Expect(recomputed.status == ColorEditJobStatus::Ready && !recomputed.reusedPreview,
           "reuse-current-different-settings-require-new-render");
}

void GrownGridWorkerPreparationAndCacheCompatibility()
{
    constexpr std::array<std::uint8_t, 64> pixels{
        40,  40,  40,  255, 80,  80,  80,  255, 120, 120, 120, 255, 160, 160, 160, 255, 40,  40,  40,  255, 80,  80,
        80,  255, 120, 120, 120, 255, 160, 160, 160, 255, 40,  40,  40,  255, 80,  80,  80,  255, 120, 120, 120, 255,
        160, 160, 160, 255, 40,  40,  40,  255, 80,  80,  80,  255, 120, 120, 120, 255, 160, 160, 160, 255};
    const Raster source = Raster::Make(4, 4, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    DocumentSession session = Session(source);
    session.BeginColorEdit(FilterKind::GaussianBlur, true);
    const auto opening = session.PendingColorEdit();
    Expect(opening.has_value() && opening->grownGrid.has_value() && !opening->grownImage.HasStorage(),
           "grown-grid.opening-keeps-only-planned-extent");
    if (!opening || !opening->grownGrid)
    {
        return;
    }
    const auto firstInput = session.PrepareColorEditPreview();
    Expect(firstInput.has_value(), "grown-grid.first-worker-input-prepared");
    if (!firstInput)
    {
        return;
    }
    const auto first = Preview(firstInput.value());
    Expect(first.status == ColorEditJobStatus::Ready && first.edit.grownImage.HasStorage() &&
               first.edit.grownImage.Width() == first.edit.grownGrid->width &&
               first.edit.grownImage.Height() == first.edit.grownGrid->height && session.ApplyColorEditPreview(first),
           "grown-grid.worker-materializes-the-planned-source");
    const Raster oldPreviewSource = first.edit.previewSource;
    const ColorEditGrid oldGrid = first.edit.grownGrid.value();

    FilterSettings widerBlur = session.PendingColorEdit()->settings;
    widerBlur.radius = 2.0;
    session.UpdateColorEdit(widerBlur, true);
    const auto secondInput = session.PrepareColorEditPreview();
    Expect(secondInput.has_value() && secondInput->edit.grownGrid.has_value() &&
               secondInput->edit.grownGrid != oldGrid && !secondInput->edit.previewSource.HasStorage(),
           "grown-grid-new-extent-drops-old-preview-source");
    if (!secondInput)
    {
        return;
    }
    ColorEditJobInput injectedStale = secondInput.value();
    injectedStale.edit.previewSource = oldPreviewSource;
    injectedStale.edit.previewTransform = injectedStale.edit.Placement();
    const auto second = Preview(injectedStale);
    Expect(second.status == ColorEditJobStatus::Ready && second.edit.previewSource.HasStorage() &&
               second.edit.previewSource.Width() == second.edit.SourceWidth() &&
               second.edit.previewSource.Height() == second.edit.SourceHeight() &&
               second.edit.previewSource != oldPreviewSource,
           "grown-grid-worker-rejects-cache-from-another-grid");
}

void MemoryPlansIncludePaddingDownscaleAndCoverage()
{
    ColorEditJobInput input;
    input.edit.kind = FilterKind::GaussianBlur;
    input.edit.settings.radius = 1;
    input.edit.original = Raster::Make(4, 2, RasterFormat::Bgra8888);
    input.edit.grownGrid = ColorEditGrid{-1, -1, 6, 4};
    const auto unpreparedGrid = ColorEditJobMemoryBytes(input);
    input.edit.grownImage = Raster::Make(6, 4, RasterFormat::Bgra8888);
    const auto preparedGrid = ColorEditJobMemoryBytes(input);
    constexpr std::uint64_t grownPixels = 6 * 4;
    Expect(unpreparedGrid && preparedGrid && *unpreparedGrid > *preparedGrid &&
               *unpreparedGrid - *preparedGrid == PixelBuffer::RequiredBytes(grownPixels * 4),
           "memory.commit-includes-private-grown-grid-padding");

    ColorEditJobInput withinLimit;
    withinLimit.edit.kind = FilterKind::Levels;
    withinLimit.edit.settings = GammaLevels();
    withinLimit.edit.preview = true;
    withinLimit.edit.original = Raster::MakeSparse(4000, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    const auto direct = ColorEditPreviewMemoryBytes(withinLimit);
    withinLimit.edit.original = Raster::MakeSparse(5000, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    const auto downscaled = ColorEditPreviewMemoryBytes(withinLimit);
    Expect(direct && downscaled && *downscaled > *direct && *downscaled < 2 * 1024 * 1024,
           "memory.preview-accounts-for-bounded-downscale-and-private-read");

    ColorEditJobInput unselected;
    unselected.edit.kind = FilterKind::Levels;
    unselected.edit.original = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    const auto noCoverage = ColorEditHistogramMemoryBytes(unselected);
    unselected.edit.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 1)};
    const auto withCoverage = ColorEditHistogramMemoryBytes(unselected);
    Expect(noCoverage && withCoverage && *withCoverage > *noCoverage && *withCoverage < 256 * 1024,
           "memory.histogram-accounts-for-bounded-selection-coverage");
}

void CancelAndIdentityNeverPublishOrCommit()
{
    {
        DocumentSession session = Session();
        const int historyBefore = session.history.UndoCount();
        const Raster original = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::Levels, true);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "identity.input-prepared");
        if (!input)
        {
            return;
        }
        const auto result = Commit(input.value());
        Expect(result.status == ColorEditJobStatus::Unchanged && !session.ApplyColorEditResult(result) &&
                   !session.HasColorEdit() && session.history.UndoCount() == historyBefore &&
                   session.ActiveLayer()->asset->image == original,
               "identity-closes-without-document-or-history-publication");
    }
    {
        DocumentSession session = Session();
        const int historyBefore = session.history.UndoCount();
        const Raster original = session.ActiveLayer()->asset->image;
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "commit-cancel.input-prepared");
        if (!input)
        {
            return;
        }
        CancelSource cancellation;
        (void)cancellation.Cancel();
        const auto result = Commit(input.value(), cancellation.Token());
        Expect(result.status == ColorEditJobStatus::Cancelled && !session.ApplyColorEditResult(result) &&
                   session.HasColorEdit() && session.ActiveLayer()->asset->image == original &&
                   session.history.UndoCount() == historyBefore,
               "pre-cancelled-commit-does-not-apply");
    }
}

void OpeningTargetChecksAndAdjustmentHistogram()
{
    for (const bool selectionChanged : {false, true})
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        const auto input = session.PrepareColorEditPreview();
        if (!input)
        {
            Expect(false, "target.input-prepared");
            return;
        }
        const auto histogram = Histogram(*input);
        const auto preview = Preview(*input);
        session.EditForTest(
            [&](CanvasDocument& document)
            {
                if (selectionChanged)
                {
                    document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 1)};
                }
                else
                {
                    document.layers[0].transform.origin.x += 1;
                }
            });
        Expect(
            !session.ApplyColorEditHistogram(histogram) && !session.ApplyColorEditPreview(preview) &&
                !session.PendingColorEdit()->histogramReady && !session.PendingColorEdit()->previewRaster.HasStorage(),
            selectionChanged ? "target.new-selection-rejects-opening-work" : "target.moved-layer-rejects-opening-work");
    }
    DocumentSession session = Session();
    const Raster below = session.ActiveLayer()->asset->image;
    session.AddAdjustment(AdjustmentKind::Levels);
    const auto id = session.AdjustmentEditingID();
    if (!id || !session.BeginAdjustmentEditing(*id, below, true))
    {
        Expect(false, "adjustment.panel-opened");
        return;
    }
    const auto input = session.PrepareColorEditPreview();
    if (!input)
    {
        Expect(false, "adjustment.input-prepared");
        return;
    }
    const auto histogram = Histogram(*input);
    session.UpdateColorEdit(GammaLevels(), true);
    Expect(session.ApplyColorEditHistogram(histogram) && session.PendingColorEdit()->histogramReady &&
               session.PendingColorEdit()->histogram[0][64] == 1 &&
               session.PendingColorEdit()->settings == GammaLevels() && !session.PrepareColorEditCommit(),
           "adjustment.own-parameter-tick-preserves-opening-histogram-without-destructive-commit");
    session.CancelColorEdit();
    session.SetAdjustmentEditingID(id);
    Expect(session.BeginAdjustmentEditing(*id, below, true) && !session.ApplyColorEditHistogram(histogram) &&
               !session.PendingColorEdit()->histogramReady,
           "adjustment.reopen-rejects-old-histogram");
}

void LookupPreviewBuildsImmutableInputsAndKeepsCpuCommitExact()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    auto input = session.PrepareColorEditPreview();
    Expect(input.has_value(), "lookup.input-prepared");
    if (!input)
    {
        return;
    }

    input->lookupPreview = true;
    const auto histogram = Histogram(input.value());
    const auto result = Preview(input.value());
    const auto* lookupPixels = result.edit.previewSource.Data();
    Expect(result.status == ColorEditJobStatus::Ready && result.edit.previewLookup &&
               result.edit.previewLookup->adjustment.kind == AdjustmentKind::Levels && lookupPixels != nullptr &&
               result.edit.previewSequence == input->token.sequence &&
               result.edit.previewSource == input->edit.original && !result.edit.previewRaster.HasStorage() &&
               !result.edit.previewCoverage.HasStorage(),
           "lookup.worker-returns-source-and-immutable-levels-lookup-only");
    Expect(histogram.status == ColorEditJobStatus::Ready && histogram.histogram[0][64] == 1.0 &&
               histogram.original == input->edit.original,
           "lookup.histogram-remains-based-on-opening-pixels");
    Expect(session.ApplyColorEditPreview(result) && session.PendingColorEdit()->previewLookup &&
               session.PendingColorEdit()->previewSequence == result.token.sequence &&
               !session.PendingColorEdit()->previewRaster.HasStorage(),
           "lookup.lookup-only-result-publishes-without-raster");
    Expect(session.ApplyColorEditHistogram(histogram) && session.PendingColorEdit()->histogramReady &&
               session.PendingColorEdit()->settings == GammaLevels() &&
               session.PendingColorEdit()->histogram == histogram.histogram,
           "lookup.histogram-applies-independently-of-lookup-preview");

    const auto commitInput = session.PrepareColorEditCommit();
    Expect(commitInput.has_value(), "lookup.cpu-commit-input-prepared");
    if (!commitInput)
    {
        return;
    }
    const auto committed = Commit(commitInput.value());
    const auto* committedPixels = committed.image.Data();
    Expect(committed.status == ColorEditJobStatus::Ready && !committed.reusedPreview && committedPixels != nullptr &&
               committedPixels[0] == 0 && std::abs(static_cast<int>(committedPixels[4]) - 128) <= 1 &&
               std::abs(static_cast<int>(committedPixels[8]) - 181) <= 1 && committedPixels[7] == 255 &&
               committedPixels[11] == 255,
           "lookup.preview-is-never-reused-as-cpu-commit-result");
}

void LookupPreviewReusesCompatibleSelectionCoverage()
{
    DocumentSession session = Session();
    session.EditForTest([](CanvasDocument& document)
                        { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 1)}; });
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(GammaLevels(), true);
    auto firstInput = session.PrepareColorEditPreview();
    Expect(firstInput.has_value(), "lookup-coverage.first-input-prepared");
    if (!firstInput)
    {
        return;
    }
    firstInput->lookupPreview = true;
    const auto first = Preview(firstInput.value());
    const Raster source = first.edit.previewSource;
    const Raster coverage = first.edit.previewCoverage;
    Expect(first.status == ColorEditJobStatus::Ready && coverage.HasStorage() &&
               coverage.Format() == RasterFormat::Gray8 && coverage.Width() == source.Width() &&
               coverage.Height() == source.Height() && session.ApplyColorEditPreview(first),
           "lookup-coverage.first-selection-coverage-published");

    FilterSettings changedSettings = GammaLevels();
    changedSettings.levels.ranges[0].gamma = 1.5;
    session.UpdateColorEdit(changedSettings, true);
    auto secondInput = session.PrepareColorEditPreview();
    Expect(secondInput.has_value(), "lookup-coverage.second-input-prepared");
    if (!secondInput)
    {
        return;
    }
    secondInput->lookupPreview = true;
    const auto second = Preview(secondInput.value());
    Expect(second.status == ColorEditJobStatus::Ready && second.edit.previewSource == source &&
               second.edit.previewCoverage == coverage && second.edit.previewLookup &&
               !second.edit.previewRaster.HasStorage(),
           "lookup-coverage.same-source-reuses-coverage-across-settings");
}

void LookupPreviewInvalidationAndPreCancellation()
{
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "lookup-invalidation.input-prepared");
        if (!input)
        {
            return;
        }
        input->lookupPreview = true;
        CancelSource cancellation;
        (void)cancellation.Cancel();
        const auto cancelled = Preview(input.value(), cancellation.Token());
        Expect(cancelled.status == ColorEditJobStatus::Cancelled && !cancelled.edit.previewLookup &&
                   !session.ApplyColorEditPreview(cancelled),
               "lookup.pre-cancelled-worker-does-not-publish");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "lookup-preview-off.input-prepared");
        if (!input)
        {
            return;
        }
        input->lookupPreview = true;
        const auto result = Preview(input.value());
        session.UpdateColorEdit(GammaLevels(), false);
        Expect(!session.ApplyColorEditPreview(result) && !session.PendingColorEdit()->previewLookup &&
                   !session.PendingColorEdit()->previewRaster.HasStorage(),
               "lookup-preview-off-rejects-inflight-result");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "lookup-reopen.input-prepared");
        if (!input)
        {
            return;
        }
        input->lookupPreview = true;
        const auto result = Preview(input.value());
        session.CancelColorEdit();
        session.BeginColorEdit(FilterKind::Levels, true);
        Expect(!session.ApplyColorEditPreview(result) && session.HasColorEdit(FilterKind::Levels) &&
                   !session.PendingColorEdit()->previewLookup,
               "lookup-reopen-rejects-old-panel-result");
    }
    {
        DocumentSession session = Session();
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 1)}; });
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(GammaLevels(), true);
        auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "lookup-selection-change.input-prepared");
        if (!input)
        {
            return;
        }
        input->lookupPreview = true;
        const auto result = Preview(input.value());
        session.EditForTest([](CanvasDocument& document)
                            { document.selection = DocumentSelection{true, SelectionGeometry::Rect(1, 0, 3, 1)}; });
        Expect(!session.ApplyColorEditPreview(result) && !session.PendingColorEdit()->previewLookup,
               "lookup-selection-change-rejects-old-target-token");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels, true);
        auto input = session.PrepareColorEditPreview();
        Expect(input.has_value(), "lookup-identity.input-prepared");
        if (!input)
        {
            return;
        }
        input->lookupPreview = true;
        const auto identity = Preview(input.value());
        Expect(identity.status == ColorEditJobStatus::Unchanged && !identity.edit.previewLookup &&
                   !identity.edit.previewRaster.HasStorage(),
               "lookup-identity-does-not-build-preview");
    }
}

void LiveAdjustmentLookupDisplaysIntermediateWithoutRollingControlsBack()
{
    DocumentSession session = Session();
    const Raster below = session.ActiveLayer()->asset->image;
    session.AddAdjustment(AdjustmentKind::Levels);
    const auto id = session.AdjustmentEditingID();
    if (!id || !session.BeginAdjustmentEditing(id.value(), below, true))
    {
        Expect(false, "live-lookup.panel-opened");
        return;
    }
    session.UpdateColorEdit(GammaLevels(), true);
    auto input = session.PrepareColorEditPreview();
    Expect(input.has_value(), "live-lookup.input-prepared");
    if (!input)
    {
        return;
    }
    LayerAdjustment gamma = session.AdjustmentOriginal().value();
    gamma = Adjust::AdjustmentWithSettings(std::move(gamma), GammaLevels());
    input->liveAdjustment = gamma;
    const auto intermediate = Preview(input.value());
    FilterSettings newerSettings = ClippedLevels();
    session.UpdateColorEdit(newerSettings, true);
    Expect(intermediate.status == ColorEditJobStatus::Ready && intermediate.edit.previewLookup &&
               intermediate.edit.previewLookupOnly && !intermediate.edit.previewRaster.HasStorage() &&
               session.ApplyColorEditPreview(intermediate) && session.PendingColorEdit()->settings == newerSettings &&
               session.PendingColorEdit()->previewLookup == intermediate.edit.previewLookup &&
               !session.PendingColorEdit()->previewRaster.HasStorage(),
           "live-lookup-old-result-applies-without-reverting-current-controls");

    session.CancelColorEdit();
    session.SetAdjustmentEditingID(id);
    Expect(session.BeginAdjustmentEditing(id.value(), below, true), "live-lookup.reopen-succeeds");
    session.UpdateColorEdit(GammaLevels(), true);
    auto staleInput = session.PrepareColorEditPreview();
    if (!staleInput)
    {
        Expect(false, "live-lookup.cancel-input-prepared");
        return;
    }
    staleInput->liveAdjustment = gamma;
    const auto stale = Preview(staleInput.value());
    session.CancelColorEdit();
    session.SetAdjustmentEditingID(id);
    Expect(session.BeginAdjustmentEditing(id.value(), below, true) && !session.ApplyColorEditPreview(stale) &&
               !session.PendingColorEdit()->previewLookup,
           "live-lookup-cancel-rejects-late-result-after-reopen");
}
} // namespace

int RunColorEditDeferredTests()
{
    gFailures = 0;
    DeferredBeginAndUpdateOnlyCaptureHandles();
    HistogramUsesOpeningPixelsAndSurvivesTicks();
    HistogramCannotCrossPanelOrDocumentEpoch();
    OlderPreviewDisplaysWithoutReplacingSettingsOrBeingReused();
    PreviewPublicationHonorsInvalidationAndEpoch();
    CurrentPreviewCanBeReusedOnlyForItsExactRequest();
    GrownGridWorkerPreparationAndCacheCompatibility();
    MemoryPlansIncludePaddingDownscaleAndCoverage();
    CancelAndIdentityNeverPublishOrCommit();
    OpeningTargetChecksAndAdjustmentHistogram();
    LookupPreviewBuildsImmutableInputsAndKeepsCpuCommitExact();
    LookupPreviewReusesCompatibleSelectionCoverage();
    LookupPreviewInvalidationAndPreCancellation();
    LiveAdjustmentLookupDisplaysIntermediateWithoutRollingControlsBack();
    return gFailures;
}

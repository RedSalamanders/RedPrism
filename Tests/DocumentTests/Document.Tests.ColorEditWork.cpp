#include "Document.Tests.ColorEditWork.h"

#include "Adjust.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "Selection.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s coloreditwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Ramp()
{
    // Grayscale BGRA keeps the legacy Levels numeric fixture identical in every color channel.
    constexpr std::array<std::uint8_t, 16> pixels{64,  64,  64,  255, 128, 128, 128, 255,
                                                  255, 255, 255, 255, 0,   0,   0,   255};
    return Raster::Make(4, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

DocumentSession Session(const Raster& image = Ramp())
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = image.Width();
    document.height = image.Height();
    ImageLayer layer =
        ImageLayer::Blank(L"Ramp", Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    const Guid active = layer.id;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Ramp"};
    document.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

FilterSettings ClippedLevels()
{
    FilterSettings settings;
    settings.levels.ranges[0].black = 64;
    settings.levels.ranges[0].white = 128;
    return settings;
}

FilterSettings GammaLevels()
{
    FilterSettings settings;
    settings.levels.ranges[0].gamma = 2;
    return settings;
}

std::optional<ColorEditJobInput> Prepare(DocumentSession& session, const FilterSettings& settings)
{
    session.UpdateColorEdit(settings, true);
    return session.PrepareColorEditCommit();
}

ColorEditJobResult Run(const ColorEditJobInput& input, CancelToken cancellation = {})
{
    InlineExecutor executor;
    return RunColorEditJob(input, executor, cancellation);
}

std::vector<std::uint8_t> Bytes(const Raster& image)
{
    if (!image.HasStorage() || image.Data() == nullptr)
    {
        return {};
    }
    const auto* first = image.Data();
    return {first, first + static_cast<std::size_t>(image.Stride()) * static_cast<std::size_t>(image.Height())};
}

void WorkerLevelsGolden()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels);
    const auto clippedInput = Prepare(session, ClippedLevels());
    Expect(clippedInput.has_value(), "levels.clipping-input-prepared");
    if (!clippedInput.has_value())
    {
        return;
    }
    ColorEditJobInput clippedWorker = clippedInput.value();
    clippedWorker.edit.previewRaster = {}; // Exercise computation, separately from the preview reuse test.
    const ColorEditJobResult clipped = Run(clippedWorker);
    const std::vector<std::uint8_t> clippedBytes = Bytes(clipped.image);
    Expect(clipped.status == ColorEditJobStatus::Ready && clippedBytes.size() == 16 && clippedBytes[0] == 0 &&
               clippedBytes[4] == 255 && clippedBytes[8] == 255 && clippedBytes[12] == 0 && clippedBytes[3] == 255 &&
               clippedBytes[7] == 255 && clippedBytes[11] == 255 && clippedBytes[15] == 255,
           "levels.legacy-input-clipping-and-alpha");

    DocumentSession gammaSession = Session();
    gammaSession.BeginColorEdit(FilterKind::Levels);
    const auto gammaInput = Prepare(gammaSession, GammaLevels());
    Expect(gammaInput.has_value(), "levels.gamma-input-prepared");
    if (!gammaInput.has_value())
    {
        return;
    }
    ColorEditJobInput gammaWorker = gammaInput.value();
    gammaWorker.edit.previewRaster = {};
    const ColorEditJobResult gamma = Run(gammaWorker);
    const std::vector<std::uint8_t> gammaBytes = Bytes(gamma.image);
    Expect(gamma.status == ColorEditJobStatus::Ready && gammaBytes.size() == 16 &&
               std::abs(static_cast<int>(gammaBytes[0]) - 128) <= 1 &&
               std::abs(static_cast<int>(gammaBytes[4]) - 181) <= 1 && gammaBytes[3] == 255 && gammaBytes[7] == 255 &&
               gammaBytes[11] == 255 && gammaBytes[15] == 255,
           "levels.legacy-gamma-and-alpha");
}

void PreviewOffUsesFullSizeGolden()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels);
    session.UpdateColorEdit(GammaLevels(), false);
    const auto input = session.PrepareColorEditCommit();
    Expect(input.has_value(), "preview-off.input-prepared");
    if (!input.has_value())
    {
        return;
    }
    const ColorEditJobResult result = Run(input.value());
    const std::vector<std::uint8_t> bytes = Bytes(result.image);
    Expect(result.status == ColorEditJobStatus::Ready && !result.reusedPreview && bytes.size() == 16 &&
               std::abs(static_cast<int>(bytes[0]) - 128) <= 1 && std::abs(static_cast<int>(bytes[4]) - 181) <= 1 &&
               bytes[3] == 255 && bytes[7] == 255 && bytes[11] == 255 && bytes[15] == 255,
           "preview-off.matches-legacy-gamma-golden-without-reuse");
}

template <typename Mutate> void StaleSourceChangedBeforePrepare(const char* name, Mutate&& mutate)
{
    DocumentSession session = Session();
    const int historyBefore = session.history.UndoCount();
    session.BeginColorEdit(FilterKind::Levels);
    mutate(session);
    const CanvasDocument changedSource = session.Document().value();
    const auto input = Prepare(session, GammaLevels());
    Expect(input.has_value(), "stale-before-prepare.input-prepared");
    if (!input.has_value())
    {
        return;
    }
    const ColorEditJobResult result = Run(input.value());
    const bool applied = session.ApplyColorEditResult(result);
    Expect(result.status == ColorEditJobStatus::Ready && !applied && !session.HasColorEdit() &&
               session.Document().value() == changedSource && session.history.UndoCount() == historyBefore,
           name);
}

void OpeningTokenRejectsSourceChangesBeforePrepare()
{
    StaleSourceChangedBeforePrepare("opening-token.layer-move-before-prepare-rejects-and-closes-old-panel",
                                    [](DocumentSession& session)
                                    {
                                        EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                                                { document.layers[0].transform.origin.x += 1.0; });
                                    });
    StaleSourceChangedBeforePrepare(
        "opening-token.mask-change-before-prepare-rejects-and-closes-old-panel",
        [](DocumentSession& session)
        {
            constexpr std::array<std::uint8_t, 1> maskPixel{0};
            const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskPixel.data(), maskPixel.size());
            EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                    { document.layers[0].mask = LayerMask{RasterAsset{mask, mask, L"Mask"}, true}; });
        });
    StaleSourceChangedBeforePrepare(
        "opening-token.selection-change-before-prepare-rejects-and-closes-old-panel",
        [](DocumentSession& session)
        {
            session.EditForTest([](CanvasDocument& document)
                                { document.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, 2, 1)}; });
        });
    StaleSourceChangedBeforePrepare("opening-token.canvas-size-change-before-prepare-rejects-and-closes-old-panel",
                                    [](DocumentSession& session)
                                    { session.EditForTest([](CanvasDocument& document) { ++document.width; }); });
}

void CrossSessionResultCannotApplyToMatchingPanel()
{
    DocumentSession source = Session();
    DocumentSession other = source; // Same document/layer ids and owning pixel handles, different session identity.
    source.BeginColorEdit(FilterKind::Levels);
    other.BeginColorEdit(FilterKind::Levels);
    const auto input = Prepare(source, GammaLevels());
    const auto otherInput = Prepare(other, GammaLevels());
    Expect(input.has_value() && otherInput.has_value(), "cross-session.both-identical-panels-prepared");
    if (!input.has_value() || !otherInput.has_value())
    {
        return;
    }
    Expect(input->token.sessionIdentity != otherInput->token.sessionIdentity &&
               input->token.sequence == otherInput->token.sequence &&
               input->token.target->layerId == otherInput->token.target->layerId &&
               input->token.target->image == otherInput->token.target->image,
           "cross-session.only-owner-identity-distinguishes-inputs");
    const ColorEditJobResult result = Run(input.value());
    const Raster otherOriginal = other.ActiveLayer()->asset->image;
    const int otherHistory = other.history.UndoCount();
    const FilterSettings otherSettings = other.filterSettings;
    Expect(!other.ApplyColorEditResult(result) && other.HasColorEdit(FilterKind::Levels) &&
               other.ActiveLayer()->asset->image == otherOriginal && other.history.UndoCount() == otherHistory &&
               other.filterSettings == otherSettings,
           "cross-session.result-cannot-change-identically-prepared-panel");
}

ImageLayer SparseBudgetLayer(int width, int height, const wchar_t* name)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{100, 100});
    layer.asset = RasterAsset{Raster::MakeSparse(width, height, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0),
                              Raster::Make(1, 1, RasterFormat::Bgra8888), name};
    layer.visible = false;
    return layer;
}

void ChangedOtherLayerBudgetRefusesGrownBlurAtomically()
{
    DocumentSession session = Session();
    constexpr int kNearBudgetWidth = 10000;
    constexpr int kNearBudgetHeight = 9999;
    constexpr int kAdjustmentWidth = 4;
    constexpr int kInitialAdjustmentHeight = 2250;
    constexpr int kGrownAdjustmentHeight = 2499; // Adds 996 sparse pixels after Prepare.
    constexpr std::array<std::uint8_t, 4> maskPixels{255, 255, 255, 255};
    const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
    EditLayerRecordsForTest(
        session,
        [&](DocumentFixture& document)
        {
            document.layers[0].mask = LayerMask{RasterAsset{mask, mask, L"Target mask"}, true};
            document.layers.push_back(SparseBudgetLayer(kNearBudgetWidth, kNearBudgetHeight, L"Large sparse"));
            document.layers.push_back(
                SparseBudgetLayer(kAdjustmentWidth, kInitialAdjustmentHeight, L"Budget adjustment"));
        });
    session.filterSettings.radius = 2.0;
    session.BeginColorEdit(FilterKind::GaussianBlur);
    Expect(session.HasColorEdit(FilterKind::GaussianBlur), "budget-change.panel-opened-with-valid-initial-budget");
    if (!session.HasColorEdit(FilterKind::GaussianBlur))
    {
        return;
    }
    FilterSettings blur = session.PendingColorEdit()->settings;
    blur.radius = 1.0;
    const auto input = Prepare(session, blur);
    Expect(input.has_value(), "budget-change.input-prepared-with-room-for-growth");
    if (!input.has_value())
    {
        return;
    }
    const ColorEditJobResult result = Run(input.value());
    Expect(result.status == ColorEditJobStatus::Ready && result.mask.has_value() && result.image.PixelCount() > 4,
           "budget-change.worker-produced-grown-blur-and-carried-mask");
    if (result.status != ColorEditJobStatus::Ready || !result.mask.has_value() || result.image.PixelCount() <= 4)
    {
        return;
    }
    EditLayerRecordsForTest(session,
                            [](DocumentFixture& document)
                            {
                                document.layers.back().asset.value().image =
                                    Raster::MakeSparse(kAdjustmentWidth, kGrownAdjustmentHeight, RasterFormat::Bgra8888,
                                                       Raster{}, 0, 0, {}, 0);
                            });
    const CanvasDocument beforeApply = session.Document().value();
    const Raster originalImage = session.ActiveLayer()->asset->image;
    const auto originalMask = session.ActiveLayer()->mask;
    const int historyBefore = session.history.UndoCount();
    const FilterSettings settingsBefore = session.filterSettings;
    Expect(!session.ApplyColorEditResult(result) && session.HasColorEdit(FilterKind::GaussianBlur) &&
               session.Document().value() == beforeApply && session.ActiveLayer()->asset->image == originalImage &&
               session.ActiveLayer()->mask == originalMask && session.history.UndoCount() == historyBefore &&
               session.filterSettings == settingsBefore,
           "budget-change.apply-refusal-is-atomic-and-keeps-panel-open");
}

void RefusedBlurGrowthCannotCommitSmallerGrid()
{
    DocumentSession session = Session();
    EditLayerRecordsForTest(session,
                            [](DocumentFixture& document)
                            {
                                document.layers.push_back(SparseBudgetLayer(10000, 9999, L"Large sparse"));
                                document.layers.push_back(SparseBudgetLayer(4, 2250, L"Budget adjustment"));
                            });
    session.filterSettings.radius = 2.0;
    session.BeginColorEdit(FilterKind::GaussianBlur);
    Expect(session.HasColorEdit(), "grow-refusal.panel-opened");
    if (!session.HasColorEdit())
    {
        return;
    }
    const CanvasDocument before = session.Document().value();
    const int historyBefore = session.history.UndoCount();
    const FilterSettings settingsBefore = session.filterSettings;
    FilterSettings larger = session.PendingColorEdit()->settings;
    larger.radius = 250;
    session.UpdateColorEdit(larger, true);
    Expect(session.TakeBudgetRefusal(), "grow-refusal.larger-grid-rejected");
    session.CommitColorEdit();
    Expect(session.HasColorEdit() && session.PendingColorEdit()->commitFailed && session.Document().value() == before &&
               session.history.UndoCount() == historyBefore && session.filterSettings == settingsBefore,
           "grow-refusal.commit-keeps-panel-and-preserves-document-history-settings");
}

void CommitUndoAndPreviewReuse()
{
    DocumentSession session = Session();
    const Raster original = session.ActiveLayer()->asset->image;
    const int historyBefore = session.history.UndoCount();
    session.BeginColorEdit(FilterKind::Levels);
    const auto input = Prepare(session, GammaLevels());
    Expect(input.has_value(), "commit.input-prepared");
    if (!input.has_value())
    {
        return;
    }
    const ColorEditJobResult result = Run(input.value());
    Expect(result.status == ColorEditJobStatus::Ready && result.reusedPreview, "commit.full-size-preview-reused");
    const bool applied = session.ApplyColorEditResult(result);
    Expect(applied && !session.HasColorEdit() && session.history.UndoCount() == historyBefore + 1 &&
               session.history.UndoName() == L"Levels",
           "commit.one-history-entry");
    const Raster committed = session.ActiveLayer()->asset->image;
    Expect(committed.HasStorage() && !(committed == original), "commit.installs-worker-image");
    session.Undo();
    Expect(session.ActiveLayer()->asset->image == original &&
               Bytes(session.ActiveLayer()->asset->image) == Bytes(original) &&
               session.history.UndoCount() == historyBefore,
           "commit.undo-restores-exact-original");
}

void StaleLayerAndMaskCloseCurrentEdit()
{
    {
        DocumentSession session = Session();
        const int historyBefore = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "stale-move.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        EditLayerRecordsForTest(session,
                                [](DocumentFixture& document) { document.layers.back().transform.origin.x += 1.0; });
        Expect(!session.ApplyColorEditResult(result) && !session.HasColorEdit() &&
                   session.history.UndoCount() == historyBefore && session.ActiveLayer()->transform.origin.x == 1.0,
               "stale-move.rejects-and-closes-matching-panel");
    }
    {
        DocumentSession session = Session();
        const int historyBefore = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "stale-mask.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        constexpr std::array<std::uint8_t, 1> maskPixel{0};
        const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskPixel.data(), maskPixel.size());
        EditLayerRecordsForTest(
            session, [&](DocumentFixture& document)
            { document.layers.back().mask = LayerMask{RasterAsset{mask, mask, L"Mask"}, true, std::nullopt, true}; });
        Expect(!session.ApplyColorEditResult(result) && !session.HasColorEdit() &&
                   session.history.UndoCount() == historyBefore && session.ActiveLayer()->mask.has_value(),
               "stale-mask-rejects-and-closes-matching-panel");
    }
}

void NewerSettingsRemainOpen()
{
    DocumentSession session = Session();
    session.BeginColorEdit(FilterKind::Levels);
    const auto input = Prepare(session, GammaLevels());
    Expect(input.has_value(), "settings.input-prepared");
    if (!input.has_value())
    {
        return;
    }
    const ColorEditJobResult oldResult = Run(input.value());
    FilterSettings newer = ClippedLevels();
    session.UpdateColorEdit(newer, true);
    const int historyBefore = session.history.UndoCount();
    const Raster original = session.ActiveLayer()->asset->image;
    Expect(!session.ApplyColorEditResult(oldResult) && session.HasColorEdit(FilterKind::Levels) &&
               session.PendingColorEdit()->settings == newer && session.history.UndoCount() == historyBefore &&
               session.ActiveLayer()->asset->image == original,
           "settings.old-result-rejected-newer-panel-preserved");
}

void ReopenAndDocumentEpochsRejectOldResults()
{
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "reopen.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult oldResult = Run(input.value());
        session.CancelColorEdit();
        session.BeginColorEdit(FilterKind::Levels);
        session.UpdateColorEdit(GammaLevels(), true);
        const Raster original = session.ActiveLayer()->asset->image;
        const int historyBefore = session.history.UndoCount();
        Expect(!session.ApplyColorEditResult(oldResult) && session.HasColorEdit(FilterKind::Levels) &&
                   session.PendingColorEdit()->settings.levels == GammaLevels().levels &&
                   session.ActiveLayer()->asset->image == original && session.history.UndoCount() == historyBefore,
               "reopen.old-result-cannot-apply-to-new-panel");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "selection.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        session.ApplySelection(SelectionGeometry::Rect(0, 0, 2, 1), SelectionMode::New, L"Select");
        Expect(!session.ApplyColorEditResult(result) && !session.HasColorEdit(),
               "selection-change-invalidates-prepared-result");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "document.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        const CanvasDocument reopened = session.Document().value();
        const Guid active = session.ActiveLayer()->id;
        session.ReplaceDocument(reopened, active); // Same document id and identical raster handles.
        session.BeginColorEdit(FilterKind::Levels);
        session.UpdateColorEdit(GammaLevels(), true);
        Expect(!session.ApplyColorEditResult(result) && session.HasColorEdit(FilterKind::Levels),
               "same-id-reopen-rejects-old-result-and-keeps-new-panel");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "undo-restore.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        const CanvasDocument beforeEdit = session.Document().value();
        session.BeginEdit(L"Fixture");
        session.EditForTest([](CanvasDocument& document) { document.resolution += 1.0; });
        session.EndEdit();
        session.CancelColorEdit();
        session.Undo();
        Expect(!session.ApplyColorEditResult(result) && !session.HasColorEdit() && session.Document() == beforeEdit &&
                   session.ActiveLayer()->asset->image == input->edit.original,
               "undo-restore-invalidates-old-result-even-when-pixels-return");
    }
}

void NoOpCancelledAndFailures()
{
    {
        DocumentSession session = Session();
        const Raster original = session.ActiveLayer()->asset->image;
        const int historyBefore = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "noop.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const ColorEditJobResult result = Run(input.value());
        Expect(result.status == ColorEditJobStatus::Unchanged && !session.ApplyColorEditResult(result) &&
                   !session.HasColorEdit() && session.history.UndoCount() == historyBefore &&
                   session.ActiveLayer()->asset->image == original,
               "noop-identity-closes-without-history");
    }
    {
        DocumentSession session = Session();
        const Raster original = session.ActiveLayer()->asset->image;
        const int historyBefore = session.history.UndoCount();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "cancel.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        CancelSource cancellation;
        cancellation.Cancel();
        const ColorEditJobResult cancelled = Run(input.value(), cancellation.Token());
        Expect(cancelled.status == ColorEditJobStatus::Cancelled && !session.ApplyColorEditResult(cancelled) &&
                   session.HasColorEdit(FilterKind::Levels) && session.ActiveLayer()->asset->image == original &&
                   session.history.UndoCount() == historyBefore,
               "cancel-pre-cancel-preserves-panel-image-history");
    }
    {
        DocumentSession session = Session();
        const Raster original = session.ActiveLayer()->asset->image;
        const int historyBefore = session.history.UndoCount();
        FilterSettings previous = session.filterSettings;
        previous.exposure.exposure = 1.25;
        session.filterSettings = previous;
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "failed.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        ColorEditJobResult failed = Run(input.value());
        failed.status = ColorEditJobStatus::Failed;
        Expect(failed.image.HasStorage() && !session.ApplyColorEditResult(failed) &&
                   session.HasColorEdit(FilterKind::Levels) && session.ActiveLayer()->asset->image == original &&
                   session.history.UndoCount() == historyBefore && session.filterSettings == previous,
               "failed-ready-copy-with-failed-status-preserves-state");
    }
}

void NoSourceAndSettingsApplyOnlyOnSuccess()
{
    {
        constexpr std::array<std::uint8_t, 16> transparent{};
        const Raster blank = Raster::Make(4, 1, RasterFormat::Bgra8888, transparent.data(), transparent.size());
        DocumentSession session = Session(blank);
        session.ApplySelection(SelectionGeometry::Rect(0, 0, 4, 1), SelectionMode::New, L"Select");
        session.BeginColorEdit(FilterKind::ContentAwareFill);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "nosource.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        auto missingCoverage = input.value();
        missingCoverage.edit.selection.reset();
        missingCoverage.edit.previewRaster = {};
        const auto failed = Run(missingCoverage);
        Expect(failed.status == ColorEditJobStatus::Failed && !session.ApplyColorEditResult(failed) &&
                   session.HasColorEdit() && session.PendingColorEdit()->commitFailed,
               "nosource-invalid-worker-input-is-failure-not-missing-donors");
        const ColorEditJobResult noSource = Run(input.value());
        const bool applied = session.ApplyColorEditResult(noSource);
        Expect(noSource.status == ColorEditJobStatus::NoSource && !applied &&
                   session.HasColorEdit(FilterKind::ContentAwareFill) && session.PendingColorEdit()->previewFailed &&
                   !session.PendingColorEdit()->commitFailed,
               "nosource-fill-keeps-panel-open-and-failed-state");
    }
    {
        DocumentSession session = Session();
        FilterSettings prior = session.filterSettings;
        prior.exposure.exposure = -0.75;
        session.filterSettings = prior;
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "settings-success.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const FilterSettings committedSettings = input->edit.settings;
        const ColorEditJobResult result = Run(input.value());
        const bool applied = session.ApplyColorEditResult(result);
        Expect(applied && session.filterSettings == committedSettings && !session.HasColorEdit(),
               "settings-updated-only-after-successful-apply");
    }
}

void CarriedMaskAndCompatibleMetadata()
{
    {
        DocumentSession session = Session();
        constexpr std::array<std::uint8_t, 4> hidden{};
        const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8, hidden.data(), hidden.size());
        EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                { document.layers[0].mask = LayerMask{RasterAsset{mask, mask, L"Mask"}, false}; });
        session.BeginColorEdit(FilterKind::GaussianBlur);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "mask.grown-input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const auto result = Run(input.value());
        const bool ready = result.status == ColorEditJobStatus::Ready && result.mask.has_value();
        Expect(ready, "mask.worker-prepares-carried-mask");
        if (!ready)
        {
            return;
        }
        const auto maskBytes = Bytes(result.mask->asset.image);
        Expect(result.mask->asset.image.Width() == result.image.Width() &&
                   result.mask->asset.image.Height() == result.image.Height() && result.image.Width() > 4 &&
                   result.image.Height() > 1,
               "mask.carried-grid-matches-grown-and-trimmed-result");
        Expect(!maskBytes.empty() &&
                   std::all_of(maskBytes.begin(), maskBytes.end(), [](auto value) { return value == 0; }),
               "mask.hidden-edge-remains-hidden-through-growth");
        Expect(result.mask->enabled && result.mask->asset.thumbnail.HasStorage() &&
                   MaskEdgeBackground(result.mask->asset.thumbnail) == 0.0,
               "mask.semantic-thumbnail-is-eager-with-worker-result");
        Expect(session.ActiveLayer()->mask->asset.image == mask && !session.ActiveLayer()->mask->enabled,
               "mask.worker-has-not-mutated-session");
        Expect(session.ApplyColorEditResult(result) &&
                   session.ActiveLayer()->mask->asset.image == result.mask->asset.image,
               "mask.installed-with-pixels-in-one-apply");
    }
    {
        DocumentSession session = Session();
        session.BeginColorEdit(FilterKind::Levels);
        const auto input = Prepare(session, GammaLevels());
        Expect(input.has_value(), "metadata.input-prepared");
        if (!input.has_value())
        {
            return;
        }
        const auto result = Run(input.value());
        EditLayerRecordsForTest(session,
                                [](DocumentFixture& document)
                                {
                                    document.layers[0].name = L"Renamed";
                                    document.resolution = 300;
                                });
        Expect(session.ApplyColorEditResult(result) && session.ActiveLayer()->name == L"Renamed" &&
                   session.ActiveLayer()->asset->name == L"Renamed" && session.Document()->resolution == 300,
               "metadata.compatible-edits-survive-targeted-apply");
    }
}

void MemoryPlanAndSelectionCount()
{
    SelectionLoops points{{Vec2{0, 0}, Vec2{4, 0}, Vec2{4, 4}, Vec2{0, 4}}, {Vec2{1, 1}, Vec2{2, 2}}};
    const SelectionLoops shared = points;
    SelectionLoops moved = std::move(points);
    Expect(shared.PointCount() == 6 && moved.PointCount() == 6 && points.PointCount() == 0 &&
               shared.Identity() == moved.Identity(),
           "memory.selection-point-count-follows-shared-and-moved-geometry");

    constexpr int side = 1024;
    const Raster source = Raster::Make(side, side, RasterFormat::Bgra8888);
    ColorEditJobInput input;
    input.edit.original = source;
    input.edit.preview = false;
    input.edit.kind = FilterKind::GaussianBlur;
    input.edit.settings.radius = 250;
    const auto dense = ColorEditJobMemoryBytes(input);
    constexpr std::uint64_t pixels = side * side;
    Expect(dense && *dense >= pixels * 20 + 1501 * sizeof(double) && *dense < pixels * 21,
           "memory.gaussian-reserves-output-float-intermediate-and-maximum-taps");
    input.edit.original = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, source, 0, 0, {}, 0);
    const auto sparse = ColorEditJobMemoryBytes(input);
    Expect(dense && sparse && *sparse - *dense == PixelBuffer::RequiredBytes(pixels * 4) &&
               !input.edit.original.IsMaterialized(),
           "memory.only-unassembled-source-adds-private-copy");
    input.edit.selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, side, side)};
    const auto selected = ColorEditJobMemoryBytes(input);
    Expect(selected && sparse && *selected - *sparse == PixelBuffer::RequiredBytes(pixels),
           "memory.coverage-survives-the-gaussian-pass");
    input.edit.kind = FilterKind::ContentAwareFill;
    const auto fill = ColorEditJobMemoryBytes(input);
    Expect(fill && *fill >= pixels * 21 && *fill < pixels * 22,
           "memory.fill-reserves-seven-scratch-arrays-output-and-coverage");

    input.edit.kind = FilterKind::Levels;
    input.edit.settings = GammaLevels();
    input.edit.original = source;
    input.edit.selection.reset();
    const auto levels = ColorEditJobMemoryBytes(input);
    input.edit.preview = true;
    input.edit.previewScale = 1;
    input.edit.previewSource = source;
    input.edit.previewTransform = input.edit.Placement();
    input.edit.previewRaster = source;
    const auto reused = ColorEditJobMemoryBytes(input);
    Expect(levels && reused && *levels >= pixels * 4 && *reused < 256 * 1024,
           "memory.exact-preview-reuse-does-not-reserve-resident-pixels-twice");
    input.edit.previewRaster = Ramp(); // Wrong grid cannot count as an exact reusable preview.
    Expect(ColorEditJobMemoryBytes(input) == levels, "memory.mismatched-preview-grid-reserves-real-filter-work");
    input.edit.original = {};
    Expect(!ColorEditJobMemoryBytes(input), "memory.empty-input-has-no-admissible-graph");
}
} // namespace

int RunColorEditWorkTests()
{
    gFailures = 0;
    WorkerLevelsGolden();
    PreviewOffUsesFullSizeGolden();
    OpeningTokenRejectsSourceChangesBeforePrepare();
    CrossSessionResultCannotApplyToMatchingPanel();
    ChangedOtherLayerBudgetRefusesGrownBlurAtomically();
    RefusedBlurGrowthCannotCommitSmallerGrid();
    CommitUndoAndPreviewReuse();
    StaleLayerAndMaskCloseCurrentEdit();
    NewerSettingsRemainOpen();
    ReopenAndDocumentEpochsRejectOldResults();
    NoOpCancelledAndFailures();
    NoSourceAndSettingsApplyOnlyOnSuccess();
    CarriedMaskAndCompatibleMetadata();
    MemoryPlanAndSelectionCount();
    return gFailures;
}

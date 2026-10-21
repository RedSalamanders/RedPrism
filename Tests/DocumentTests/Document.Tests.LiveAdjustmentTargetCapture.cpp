#include "Document.Tests.LiveAdjustmentTargetCapture.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Adjust.h"
#include "AdjustmentOpeningWork.h"
#include "ColorEditHistory.h"
#include "ColorEditWork.h"
#include "Document.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

namespace LiveAdjustmentTargetCaptureTests
{
namespace
{
constexpr std::array<std::uint8_t, 16> kPixels{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> kMaskPixels{0, 64, 192, 255};
constexpr std::size_t kLongNameUnits = 1u << 20;

void Expect(bool condition, const char* name, int& failures)
{
    std::printf("%s live-adjustment-target-capture.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

struct Fixture final
{
    Fixture()
        : pixels(Raster::Make(4, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size())),
          maskPixels(Raster::Make(4, 1, RasterFormat::Gray8, kMaskPixels.data(), kMaskPixels.size())), below(pixels)
    {
        DocumentFixture draft;
        draft.id = Guid::Generate();
        draft.width = 4;
        draft.height = 1;
        ImageLayer adjustment = ImageLayer::Blank(L"Live adjustment", Vec2{4, 1});
        adjustmentID = adjustment.id;
        LayerAdjustment value;
        value.kind = AdjustmentKind::Levels;
        value.levels.ranges[0] = LevelRange{0, 2.0, 255, 0, 255};
        adjustment.adjustment = value;
        const LayerTransform placement{{1.0, 0.0}, {2.0, 1.0}, 0.0, false, false, LayerSampling::High};
        adjustment.mask =
            LayerMask{RasterAsset{maskPixels, Raster::MakeThumbnail(maskPixels), std::wstring(kLongNameUnits, L'N')},
                      false, placement, true};
        draft.layers.push_back(std::move(adjustment));

        ImageLayer source = ImageLayer::Blank(L"Underlying pixels", Vec2{4, 1});
        sourceID = source.id;
        source.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), L"Source asset"};
        draft.layers.push_back(std::move(source));
        document = std::move(draft).Seal();
        status = ValidateDocument(document);
        if (status == DocumentStatus::Ok)
            session.ReplaceDocument(document, adjustmentID);
    }

    Raster pixels;
    Raster maskPixels;
    Raster below;
    Guid adjustmentID;
    Guid sourceID;
    CanvasDocument document;
    DocumentStatus status = DocumentStatus::Invalid;
    DocumentSession session;
};

std::optional<PreparedAdjustmentOpening> PrepareOnWorker(AdjustmentOpeningInput input,
                                                         std::optional<CanvasDocument> source, Raster below,
                                                         DocumentHistory::StateView history,
                                                         std::thread::id& actualThread, CancelToken cancellation = {})
{
    std::optional<PreparedAdjustmentOpening> prepared;
    std::thread worker(
        [captured = std::move(input), document = std::move(source), image = std::move(below),
         capturedHistory = std::move(history), cancellation, &actualThread, &prepared]() mutable
        {
            actualThread = std::this_thread::get_id();
            auto result = DocumentSession::PrepareAdjustmentOpeningPublication(
                std::move(captured), document, std::move(image), std::move(capturedHistory), cancellation);
            if (result)
                prepared.emplace(std::move(result.value()));
        });
    worker.join();
    return prepared;
}

bool CapturedTargetIsBounded(const ColorEditJobInput& input, const RecordRoot<ImageLayer>& root, const Guid& id)
{
    const auto* original = root.FindKey(id);
    const auto* rooted = input.edit.sourceLayers.FindKey(id);
    const LayerMask* descriptorMask =
        input.token.target && input.token.target->mask ? &*input.token.target->mask : nullptr;
    return original && rooted && original->mask && rooted->mask && input.token.target && descriptorMask &&
           input.token.target->layerId == original->id &&
           input.token.target->image == (original->asset ? original->asset->image : Raster{}) &&
           input.token.target->transform == original->transform && input.token.target->isGroup == original->isGroup &&
           input.token.target->adjustment == original->adjustment &&
           input.edit.sourceLayers.Identity() == root.Identity() && rooted->mask->asset.name.size() == kLongNameUnits &&
           original->mask->asset.name == rooted->mask->asset.name && descriptorMask->asset.name.empty() &&
           descriptorMask->asset.image == original->mask->asset.image &&
           descriptorMask->asset.thumbnail == original->mask->asset.thumbnail &&
           descriptorMask->enabled == original->mask->enabled &&
           descriptorMask->placement == original->mask->placement && descriptorMask->linked == original->mask->linked;
}

void DirectDeferredOpening(int& failures)
{
    Fixture fixture;
    Expect(fixture.status == DocumentStatus::Ok && fixture.session.Document().has_value(),
           "direct-fixture-validates-with-one-megawchar-mask-name", failures);
    if (fixture.status != DocumentStatus::Ok || !fixture.session.Document())
        return;

    fixture.session.SetColorDisplayJobsEnabled(true);
    fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
    Expect(fixture.session.BeginAdjustmentEditing(fixture.adjustmentID, fixture.below, true),
           "direct-deferred-adjustment-opens", failures);
    const auto openingRoot = fixture.session.Document()->layers;
    const auto original = fixture.session.AdjustmentOriginal();
    const auto& pendingValue = fixture.session.PendingColorEdit();
    const auto* pending = pendingValue ? &pendingValue.value() : nullptr;
    const auto first = fixture.session.PrepareColorEditPreview();
    Expect(first && CapturedTargetIsBounded(first.value(), openingRoot, fixture.adjustmentID),
           "direct-opening-retains-root-and-small-semantic-descriptor", failures);
    if (!first)
        return;
    Expect(pending && pending->sourceLayers.Identity() == openingRoot.Identity() && pending->sourceToken &&
               pending->sourceToken->target &&
               ColorEditSourceMask(*pending) == &openingRoot.FindKey(fixture.adjustmentID)->mask.value(),
           "direct-opening-mask-source-is-original-full-record", failures);
    const auto* originalMask = &*openingRoot.FindKey(fixture.adjustmentID)->mask;
    Expect(originalMask && originalMask->asset.name.size() == kLongNameUnits && !originalMask->enabled &&
               originalMask->linked && originalMask->placement && originalMask->placement->origin.x == 1.0 &&
               originalMask->placement->size.x == 2.0,
           "direct-opening-source-keeps-original-mask-name-flags-and-placement", failures);

    auto liveInput = first.value();
    liveInput.liveAdjustment = original;
    ColorEditPreviewResult preview;
    std::thread worker(
        [input = std::move(liveInput), &preview]() mutable
        {
            InlineExecutor executor;
            preview = RunColorEditPreviewJob(input, executor);
        });
    worker.join();
    const auto revision = fixture.session.history.CurrentRevision();
    Expect(preview.status == ColorEditJobStatus::Ready && preview.edit.previewLookupOnly &&
               preview.edit.previewLookup && fixture.session.ApplyColorEditPreview(preview),
           "direct-worker-lookup-is-ready-and-owner-accepts-it", failures);
    Expect(fixture.session.Document()->layers.Identity() == openingRoot.Identity() &&
               fixture.session.Document()->layers.FindKey(fixture.adjustmentID)->adjustment == original &&
               fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.CaptureState().Depth() == 1 && fixture.session.history.UndoCount() == 0,
           "direct-lookup-keeps-adjustment-pixels-and-opening-history-pending", failures);

    bool capturesRemainBounded = true;
    for (int tick = 0; tick != 3; ++tick)
    {
        FilterSettings settings = fixture.session.PendingColorEdit()->settings;
        settings.levels.ranges[0].gamma = 1.25 + tick * 0.25;
        fixture.session.UpdateColorEdit(settings, true);
        const auto captured = fixture.session.PrepareColorEditPreview();
        if (!captured)
        {
            capturesRemainBounded = false;
            continue;
        }
        auto live = captured.value();
        live.liveAdjustment = original;
        capturesRemainBounded = capturesRemainBounded &&
                                CapturedTargetIsBounded(live, openingRoot, fixture.adjustmentID) &&
                                live.liveAdjustment == original;
        ColorEditPreviewResult tickPreview;
        std::thread tickWorker(
            [job = std::move(live), &tickPreview]() mutable
            {
                InlineExecutor executor;
                tickPreview = RunColorEditPreviewJob(job, executor);
            });
        tickWorker.join();
        capturesRemainBounded = capturesRemainBounded && tickPreview.status == ColorEditJobStatus::Ready &&
                                tickPreview.edit.previewLookupOnly &&
                                fixture.session.ApplyColorEditPreview(tickPreview);
    }
    std::array<std::uint8_t, 16> observed{};
    fixture.pixels.ReadRegion(0, 0, 4, 1, observed.data(), 16, 0);
    Expect(observed == kPixels, "live-lookup-preserves-independent-literal-source-bytes", failures);
    Expect(capturesRemainBounded, "repeated-direct-preview-ticks-copy-no-mask-name", failures);
}

void PreparedWorkerOpening(int& failures)
{
    Fixture fixture;
    Expect(fixture.status == DocumentStatus::Ok && fixture.session.Document().has_value(), "prepared-fixture-validates",
           failures);
    if (fixture.status != DocumentStatus::Ok || !fixture.session.Document())
        return;
    fixture.session.SetColorDisplayJobsEnabled(true);
    fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
    const auto input = fixture.session.PrepareAdjustmentOpeningInput();
    Expect(input.has_value(), "prepared-opening-input-captured", failures);
    if (!input)
        return;
    Expect(!input->token.target, "prepared-opening-input-keeps-whole-document-authority", failures);
    std::thread::id actualThread;
    auto candidate = PrepareOnWorker(AdjustmentOpeningInput{input.value()}, fixture.session.Document(), fixture.below,
                                     fixture.session.history.CaptureState(), actualThread);
    Expect(actualThread != std::this_thread::get_id(), "prepared-opening-runs-on-an-actual-worker", failures);
    Expect(candidate.has_value(), "prepared-opening-worker-candidate-created", failures);
    if (!candidate)
        return;
    const auto originalRoot = fixture.session.Document()->layers;
    Expect(fixture.session.ApplyPreparedAdjustmentOpeningIfCurrent(candidate.value()),
           "prepared-opening-owner-apply-succeeds", failures);
    const auto& panelValue = fixture.session.PendingColorEdit();
    const auto* panel = panelValue ? &panelValue.value() : nullptr;
    const auto* rooted = panel ? panel->sourceLayers.FindKey(fixture.adjustmentID) : nullptr;
    Expect(panel && rooted && panel->sourceLayers.Identity() == originalRoot.Identity() && rooted->mask &&
               rooted->mask->asset.name.size() == kLongNameUnits && panel->sourceToken && panel->sourceToken->target &&
               panel->sourceToken->target->mask && panel->sourceToken->target->mask->asset.name.empty() &&
               ColorEditSourceMask(*panel) == &originalRoot.FindKey(fixture.adjustmentID)->mask.value(),
           "prepared-opening-holds-root-and-name-free-mask-authority", failures);

    bool capturesRemainBounded = true;
    for (int tick = 0; tick != 3; ++tick)
    {
        FilterSettings settings = fixture.session.PendingColorEdit()->settings;
        settings.levels.ranges[0].gamma = 1.5 + tick * 0.25;
        fixture.session.UpdateColorEdit(settings, true);
        auto captured = fixture.session.PrepareColorEditPreview();
        if (!captured)
        {
            capturesRemainBounded = false;
            continue;
        }
        captured->liveAdjustment = fixture.session.AdjustmentOriginal();
        capturesRemainBounded = capturesRemainBounded &&
                                CapturedTargetIsBounded(captured.value(), originalRoot, fixture.adjustmentID) &&
                                captured->liveAdjustment == fixture.session.AdjustmentOriginal();
        ColorEditPreviewResult preview;
        std::thread worker(
            [job = std::move(captured.value()), &preview]() mutable
            {
                InlineExecutor executor;
                preview = RunColorEditPreviewJob(job, executor);
            });
        worker.join();
        capturesRemainBounded = capturesRemainBounded && preview.status == ColorEditJobStatus::Ready &&
                                preview.edit.previewLookupOnly && preview.edit.previewLookup &&
                                fixture.session.ApplyColorEditPreview(preview);
    }
    Expect(capturesRemainBounded, "prepared-opening-repeated-worker-lookups-accepted", failures);
    Expect(fixture.session.Document()->layers.Identity() == originalRoot.Identity() &&
               fixture.session.Document()->layers.FindKey(fixture.adjustmentID)->adjustment ==
                   fixture.session.AdjustmentOriginal() &&
               fixture.session.history.CaptureState().Depth() == 1 && fixture.session.history.UndoCount() == 0 &&
               fixture.session.PendingColorEdit().has_value(),
           "prepared-lookups-leave-source-pixels-and-history-pending", failures);
}

void StaleSessionReplacement(int& failures)
{
    Fixture fixture;
    fixture.session.SetColorDisplayJobsEnabled(true);
    fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
    Expect(fixture.session.BeginAdjustmentEditing(fixture.adjustmentID, fixture.below, true),
           "stale-session-fixture-opens", failures);
    auto input = fixture.session.PrepareColorEditPreview();
    Expect(input.has_value(), "stale-session-preview-captured", failures);
    if (!input)
        return;
    input->liveAdjustment = fixture.session.AdjustmentOriginal();
    fixture.session.ReplaceDocument(fixture.document, fixture.adjustmentID);
    ColorEditPreviewResult result;
    std::thread worker(
        [job = std::move(input.value()), &result]() mutable
        {
            InlineExecutor executor;
            result = RunColorEditPreviewJob(job, executor);
        });
    worker.join();
    Expect(result.status == ColorEditJobStatus::Ready && !fixture.session.ApplyColorEditPreview(result) &&
               fixture.session.Document()->layers.Identity() == fixture.document.layers.Identity() &&
               !fixture.session.HasColorEdit(),
           "ready-result-from-replaced-session-is-refused", failures);
}

void InvalidAuthorityAndCancellation(int& failures)
{
    {
        Fixture fixture;
        fixture.session.SetColorDisplayJobsEnabled(true);
        fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
        Expect(fixture.session.BeginAdjustmentEditing(fixture.adjustmentID, fixture.below, true),
               "corrupt-fixture-opens", failures);
        auto input = fixture.session.PrepareColorEditPreview();
        if (input && input->token.target && input->token.target->mask)
        {
            input->token.target->mask->linked = !input->token.target->mask->linked;
            ColorEditPreviewResult preview;
            std::thread worker(
                [job = std::move(input.value()), &preview]() mutable
                {
                    InlineExecutor executor;
                    preview = RunColorEditPreviewJob(job, executor);
                });
            worker.join();
            Expect(preview.status == ColorEditJobStatus::Failed, "worker-rejects-corrupt-mask-descriptor", failures);
        }
        else
            Expect(false, "corrupt-descriptor-input-captured", failures);
    }
    {
        Fixture fixture;
        fixture.session.SetAdjustmentEditingID(fixture.adjustmentID);
        auto input = fixture.session.PrepareAdjustmentOpeningInput();
        CancelSource cancellation;
        (void)cancellation.Cancel();
        std::thread::id actualThread;
        const auto candidate =
            input ? PrepareOnWorker(std::move(input.value()), fixture.session.Document(), fixture.below,
                                    fixture.session.history.CaptureState(), actualThread, cancellation.Token())
                  : std::nullopt;
        Expect(actualThread != std::thread::id{} && actualThread != std::this_thread::get_id(),
               "pre-cancelled-opening-acknowledges-on-worker", failures);
        Expect(!candidate && fixture.session.Document()->layers.Identity() == fixture.document.layers.Identity() &&
                   !fixture.session.HasColorEdit() && fixture.session.history.CaptureState().Depth() == 0,
               "pre-cancelled-opening-does-not-publish", failures);
    }
}
} // namespace

void Run(int& failures)
{
    DirectDeferredOpening(failures);
    PreparedWorkerOpening(failures);
    StaleSessionReplacement(failures);
    InvalidAuthorityAndCancellation(failures);
}
} // namespace LiveAdjustmentTargetCaptureTests

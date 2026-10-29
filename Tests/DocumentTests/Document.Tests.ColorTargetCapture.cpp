#include "Document.Tests.ColorTargetCapture.h"

#include "Common/ColorEditHistory.h"
#include "Common/ColorEditWork.h"
#include "Common/Document.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

namespace ColorTargetCaptureTests
{
namespace
{
constexpr std::array<std::uint8_t, 16> kSource{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
constexpr std::array<std::uint8_t, 16> kGamma2{0,   0,   0,   255, 128, 128, 128, 255,
                                               181, 181, 181, 255, 255, 255, 255, 255};

void Expect(bool condition, const char* name, int& failures)
{
    std::printf("%s color-target-capture.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

FilterSettings Gamma2()
{
    FilterSettings settings;
    settings.levels.ranges[0].gamma = 2.0;
    return settings;
}

DocumentSession MakeSession(std::size_t maskNameUnits = 18)
{
    const Raster image = Raster::Make(4, 1, RasterFormat::Bgra8888, kSource.data(), kSource.size());
    const Raster maskImage = Raster::Make(4, 1, RasterFormat::Gray8);
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 4;
    fixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Color target", Vec2{4.0, 1.0});
    const Guid active = layer.id;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Layer asset name"};
    std::wstring maskName(maskNameUnits, L'M');
    layer.mask = LayerMask{RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), std::move(maskName)}, false,
                           LayerTransform{{0.0, 0.0}, {4.0, 1.0}, 0.0, false, false, LayerSampling::High}, false};
    fixture.layers.push_back(std::move(layer));
    DocumentSession session;
    auto document = std::move(fixture).Seal();
    if (ValidateDocument(document) != DocumentStatus::Ok)
        return session;
    session.ReplaceDocument(std::move(document), active);
    return session;
}

bool PixelsEqual(const Raster& image, const std::array<std::uint8_t, 16>& expected)
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || image.Width() != 4 || image.Height() != 1)
        return false;
    std::array<std::uint8_t, 16> actual{};
    image.ReadRegion(0, 0, 4, 1, actual.data(), static_cast<int>(actual.size()), 0);
    return actual == expected;
}

void CapturesSmallDescriptorAndFullImmutableRoot(int& failures)
{
    DocumentSession session = MakeSession(1u << 20);
    Expect(session.Document().has_value(), "large-name-fixture-passes-product-validation", failures);
    if (!session.Document())
        return;
    const auto originalRoot = session.Document()->layers;
    const ImageLayer* original = originalRoot.FindKey(session.ActiveLayerID().value());
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(Gamma2(), true);
    const auto input = session.PrepareColorEditCommit();
    const auto* capturedMask =
        input && input->token.target && input->token.target->mask ? &*input->token.target->mask : nullptr;
    const ImageLayer* rooted = input ? input->edit.sourceLayers.FindKey(input->edit.layerID) : nullptr;
    Expect(input && original && rooted && capturedMask &&
               input->edit.sourceLayers.Identity() == originalRoot.Identity() &&
               input->edit.sourceLayers.Identity() == session.PendingColorEdit()->sourceLayers.Identity(),
           "open-and-job-share-original-record-root", failures);
    Expect(original && rooted && capturedMask && original->mask && rooted->mask &&
               original->mask->asset.name.size() == (1u << 20) &&
               rooted->mask->asset.name == original->mask->asset.name && capturedMask->asset.name.empty() &&
               capturedMask->enabled == original->mask->enabled &&
               capturedMask->placement == original->mask->placement && capturedMask->linked == original->mask->linked &&
               capturedMask->asset.image == original->mask->asset.image &&
               capturedMask->asset.thumbnail == original->mask->asset.thumbnail,
           "descriptor-omits-name-but-root-retains-full-mask-semantics", failures);
    Expect(original && original->asset && original->asset->name == L"Layer asset name" &&
               original->name == L"Color target" && session.history.UndoCount() == 0,
           "capture-keeps-bounded-layer-and-asset-metadata-without-history-edit", failures);
}

void WorkerJobsAndPreparedApply(int& failures)
{
    DocumentSession session = MakeSession(1024);
    Expect(session.Document().has_value(), "worker-fixture-passes-product-validation", failures);
    if (!session.Document())
        return;
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(Gamma2(), true);
    const auto input = session.PrepareColorEditCommit();
    Expect(input.has_value(), "worker-input-captured-on-owner", failures);
    if (!input)
        return;
    const auto before = session.Document();
    const auto revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    ColorEditPreviewResult preview;
    ColorEditHistogramResult histogram;
    ColorEditJobResult result;
    std::thread worker(
        [job = input.value(), &preview, &histogram, &result]() mutable
        {
            InlineExecutor executor;
            preview = RunColorEditPreviewJob(job, executor);
            histogram = RunColorEditHistogramJob(job, executor);
            result = RunColorEditJob(job, executor);
        });
    worker.join();
    Expect(preview.status == ColorEditJobStatus::Ready && histogram.status == ColorEditJobStatus::Ready &&
               result.status == ColorEditJobStatus::Ready,
           "native-worker-runs-preview-histogram-and-commit-from-captured-input", failures);
    Expect(session.Document() == before && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == undoCount && session.PendingColorEdit().has_value(),
           "worker-completion-does-not-touch-session-or-history", failures);
    Expect(result.mask && result.mask->asset.name.size() == 1024 && result.mask->asset.name[0] == L'M' &&
               result.mask->enabled == false && result.mask->linked == false && result.mask->placement.has_value(),
           "commit-materializes-original-mask-name-and-metadata", failures);
    Expect(PixelsEqual(result.image, kGamma2), "commit-matches-independent-literal-gamma2-pixels", failures);

    std::optional<PreparedColorEdit> prepared;
    std::thread publication(
        [source = session.Document(), active = session.ActiveLayerID(), generation = session.Generation(),
         history = session.history.CaptureState(), &result, &prepared]() mutable
        { prepared = PrepareColorEditPublication(source, active, generation, std::move(history), result); });
    publication.join();
    const bool applied = prepared && session.IsPreparedColorEditCurrent(prepared.value()) &&
                         session.ApplyPreparedColorEditResult(prepared.value());
    Expect(applied && session.history.UndoCount() == 1 && session.history.UndoName() == L"Levels" &&
               session.ActiveLayer() && session.ActiveLayer()->mask &&
               session.ActiveLayer()->mask->asset.name.size() == 1024 &&
               PixelsEqual(session.ActiveLayer()->asset->image, kGamma2),
           "prepared-history-apply-publishes-mask-and-one-undo-entry", failures);
    if (applied)
    {
        session.Undo();
        Expect(session.ActiveLayer() && PixelsEqual(session.ActiveLayer()->asset->image, kSource) &&
                   session.ActiveLayer()->mask && session.ActiveLayer()->mask->asset.name.size() == 1024,
               "undo-restores-literal-source-and-full-mask-name", failures);
    }
}

void CorruptDescriptorsAreRejected(int& failures)
{
    DocumentSession session = MakeSession(64);
    Expect(session.Document().has_value(), "corruption-fixture-passes-product-validation", failures);
    if (!session.Document())
        return;
    session.BeginColorEdit(FilterKind::Levels, true);
    session.UpdateColorEdit(Gamma2(), false);
    const auto input = session.PrepareColorEditCommit();
    Expect(input.has_value(), "corruption-input-captured", failures);
    if (!input)
        return;
    for (int corruption = 0; corruption != 3; ++corruption)
    {
        auto changed = input.value();
        auto& target = changed.token.target.value();
        if (corruption == 0)
            target.image = Raster::Make(1, 1, RasterFormat::Bgra8888);
        else if (corruption == 1)
            target.transform.origin.x += 0.25;
        else
            target.mask->enabled = !target.mask->enabled;
        ColorEditPreviewResult preview;
        ColorEditHistogramResult histogram;
        ColorEditJobResult result;
        std::thread worker(
            [job = std::move(changed), &preview, &histogram, &result]() mutable
            {
                InlineExecutor executor;
                preview = RunColorEditPreviewJob(job, executor);
                histogram = RunColorEditHistogramJob(job, executor);
                result = RunColorEditJob(job, executor);
            });
        worker.join();
        Expect(preview.status == ColorEditJobStatus::Failed && histogram.status == ColorEditJobStatus::Failed &&
                   result.status == ColorEditJobStatus::Failed && !result.image.HasStorage(),
               corruption == 0   ? "corrupt-image-rejected-by-all-workers"
               : corruption == 1 ? "corrupt-transform-rejected-by-all-workers"
                                 : "corrupt-mask-flags-rejected-by-all-workers",
               failures);
    }
}

void CancellationLifetimeAndMemoryPlans(int& failures)
{
    {
        DocumentSession session = MakeSession(128);
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(Gamma2(), false);
        const auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "cancellation-fixture-and-input-valid", failures);
        if (!input)
            return;
        const auto before = session.Document();
        const int undo = session.history.UndoCount();
        CancelSource cancel;
        (void)cancel.Cancel();
        ColorEditJobResult result;
        std::thread worker(
            [job = *input, token = cancel.Token(), &result]() mutable
            {
                InlineExecutor executor;
                result = RunColorEditJob(job, executor, token);
            });
        worker.join();
        Expect(result.status == ColorEditJobStatus::Cancelled && session.Document() == before &&
                   session.history.UndoCount() == undo && session.PendingColorEdit().has_value(),
               "cancelled-commit-leaves-original-document-and-history", failures);
    }
    {
        DocumentSession session = MakeSession(256);
        session.BeginColorEdit(FilterKind::Levels, true);
        session.UpdateColorEdit(Gamma2(), false);
        auto input = session.PrepareColorEditCommit();
        Expect(input.has_value(), "replacement-fixture-and-input-valid", failures);
        if (!input)
            return;
        const auto rootIdentity = input->edit.sourceLayers.Identity();
        DocumentFixture replacement;
        replacement.id = Guid::Generate();
        replacement.width = 1;
        replacement.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Replacement", Vec2{1.0, 1.0});
        const Guid active = layer.id;
        const std::array<std::uint8_t, 4> pixel{1, 2, 3, 255};
        const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Replacement"};
        replacement.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(replacement).Seal(), active);
        ColorEditJobResult late;
        std::thread worker(
            [job = std::move(input.value()), &late]() mutable
            {
                InlineExecutor executor;
                late = RunColorEditJob(job, executor);
            });
        worker.join();
        Expect(rootIdentity && !input->edit.sourceLayers.Identity() && late.status == ColorEditJobStatus::Ready &&
                   late.mask && late.mask->asset.name.size() == 256 && late.mask->asset.name[0] == L'M' &&
                   !session.ApplyColorEditResult(late),
               "moved-worker-closure-retains-source-across-replacement-but-result-is-stale", failures);
    }
    {
        DocumentSession shortSession = MakeSession(8);
        DocumentSession longSession = MakeSession(4096);
        shortSession.BeginColorEdit(FilterKind::Levels, true);
        longSession.BeginColorEdit(FilterKind::Levels, true);
        shortSession.UpdateColorEdit(Gamma2(), false);
        longSession.UpdateColorEdit(Gamma2(), false);
        const auto shortInput = shortSession.PrepareColorEditCommit();
        const auto longInput = longSession.PrepareColorEditCommit();
        Expect(shortInput && longInput, "memory-plan-fixtures-and-inputs-valid", failures);
        if (!shortInput || !longInput)
            return;
        const auto shortPlan = shortInput ? ColorEditJobMemoryBytes(shortInput.value()) : std::nullopt;
        const auto longPlan = longInput ? ColorEditJobMemoryBytes(longInput.value()) : std::nullopt;
        Expect(shortPlan && longPlan && *longPlan > *shortPlan, "commit-plan-grows-with-carried-mask-name", failures);
        Expect(ColorEditHeldMemoryBytes(longInput->edit) > ColorEditHeldMemoryBytes(shortInput->edit),
               "retirement-plan-includes-complete-retained-source-records", failures);
        Expect(shortSession.PendingColorEdit()->sourceLayers.Identity() == shortInput->edit.sourceLayers.Identity() &&
                   longSession.PendingColorEdit()->sourceLayers.Identity() == longInput->edit.sourceLayers.Identity(),
               "held-opening-roots-remain-owned-through-job-capture", failures);
    }
}
} // namespace

void Run(int& failures)
{
    CapturesSmallDescriptorAndFullImmutableRoot(failures);
    WorkerJobsAndPreparedApply(failures);
    CorruptDescriptorsAreRejected(failures);
    CancellationLifetimeAndMemoryPlans(failures);
}
} // namespace ColorTargetCaptureTests

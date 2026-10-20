#include "Document.Tests.SizeFinalSource.h"

#include "../Support/DocumentFixture.h"
#include "CanvasSize.h"
#include "Document.h"
#include "DocumentSizeHistory.h"
#include "Executor.h"
#include "RasterWork.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>

namespace
{
constexpr std::array<std::uint8_t, 8> kSourcePixels{0, 0, 255, 255, 255, 0, 0, 255};
constexpr std::array<std::uint8_t, 32> kScaledPixels{0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                                                     0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255};

void Expect(bool condition, const char* fact, int& failures) noexcept
{
    std::printf("%s size-final-source.%s\n", condition ? "PASS" : "FAIL", fact);
    failures += condition ? 0 : 1;
}

bool PixelsEqual(const Raster& raster, std::span<const std::uint8_t> expected, int width = 4, int height = 2)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != width ||
        raster.Height() != height)
        return false;
    const Raster contiguous = ContiguousRasterForWork(raster);
    if (!contiguous.HasStorage() ||
        static_cast<std::size_t>(contiguous.Stride()) * static_cast<std::size_t>(contiguous.Height()) !=
            expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (contiguous.Data()[index] != expected[index])
            return false;
    return true;
}

bool MaskPixelsEqual(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || raster.Width() != 4 || raster.Height() != 2)
        return false;
    const Raster contiguous = ContiguousRasterForWork(raster);
    if (!contiguous.HasStorage() ||
        static_cast<std::size_t>(contiguous.Stride()) * static_cast<std::size_t>(contiguous.Height()) !=
            expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (contiguous.Data()[index] != expected[index])
            return false;
    return true;
}

bool InstallLiteralSource(DocumentSession& session)
{
    session.CreateDocument(2, 1, true);
    if (!session.Document() || !session.ActiveLayerID())
        return false;
    const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
    if (!image.HasStorage())
        return false;
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
                            });
    session.history.Reset();
    return session.Document().has_value() && session.Document()->layers.size() == 1;
}

std::optional<PreparedDocumentSize> PrepareResized(DocumentSession& session, const CanvasDocument& source,
                                                   std::shared_ptr<const SharedPublicationMemory> memory = {},
                                                   CancelToken cancellation = {})
{
    const auto token = session.CaptureJobToken();
    const auto active = session.ActiveLayerID();
    const auto history = session.history.CaptureState();
    const auto sourceSnapshot = session.Document();
    if (!token || !sourceSnapshot || *sourceSnapshot != source)
        return std::nullopt;
    std::optional<PreparedDocumentSize> prepared;
    std::thread worker(
        [&]
        {
            ImageSizeOptions options;
            options.width = 4;
            options.height = 2;
            options.sampling = LayerSampling::Nearest;
            CanvasDocument result;
            if (ResizeImage(source, options, result, cancellation, memory) == ResizeStatus::Ok)
                prepared = PrepareDocumentSizePublication(sourceSnapshot, active, *token, history, std::move(result),
                                                          L"Image Size", cancellation, {}, std::move(memory));
        });
    worker.join();
    return prepared;
}

std::shared_ptr<SharedPublicationMemory> NewMemoryOnWorker()
{
    std::shared_ptr<SharedPublicationMemory> memory;
    std::thread worker([&memory] { memory = SharedPublicationMemory::CreateForWork(); });
    worker.join();
    return memory;
}

void FinalSourceAndHistory(int& failures)
{
    DocumentSession session;
    const bool installed = InstallLiteralSource(session);
    Expect(installed, "fixture.installs-literal-2x1-source", failures);
    if (!installed)
        return;

    const CanvasDocument original = *session.Document();
    auto prepared = PrepareResized(session, original);
    Expect(prepared.has_value() && session.Document() == std::optional<CanvasDocument>{original} &&
               prepared->FinalDocument() && prepared->FinalDocument()->width == 4 &&
               prepared->FinalDocument()->height == 2,
           "prepared.exposes-final-and-keeps-original-before-apply", failures);
    if (!prepared)
        return;

    PreparedDocumentSize moved(std::move(*prepared));
    Expect(prepared->FinalDocument() == nullptr && !session.IsPreparedDocumentSizeCurrent(*prepared) &&
               session.IsPreparedDocumentSizeCurrent(moved),
           "prepared.move-invalidates-source-and-preserves-destination", failures);
    DocumentHistory::ResidencyPermit permit;
    auto residency = moved.TryAcquireHistoryResidency(permit);
    if (residency == DocumentHistory::ResidencyValidation::Refresh)
    {
        permit.Reset();
        bool refreshed = false;
        std::thread worker([&] { refreshed = moved.RefreshHistoryResidency(); });
        worker.join();
        if (refreshed)
            residency = moved.TryAcquireHistoryResidency(permit);
    }
    const bool residencyReady = residency == DocumentHistory::ResidencyValidation::Ready;
    Expect(residencyReady, "prepared.history-residency-refreshes-before-owner-publication", failures);
    Expect(residencyReady && session.ApplyPreparedDocumentSizeIfCurrent(moved) && moved.FinalDocument() == nullptr &&
               PixelsEqual(session.Document()->layers.front().asset->image, kScaledPixels),
           "apply.invalidates-final-accessor-and-publishes-literal-nearest-pixels", failures);
    permit.Reset();
    DocumentHistory::ResidencyPermit afterApply;
    Expect(moved.TryAcquireHistoryResidency(afterApply) == DocumentHistory::ResidencyValidation::Invalid &&
               !session.IsPreparedDocumentSizeCurrent(moved) && !session.ApplyPreparedDocumentSizeIfCurrent(moved) &&
               session.history.UndoCount() == 1,
           "apply.cannot-republish-retired-source-or-create-second-history-entry", failures);
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{original} &&
               PixelsEqual(session.Document()->layers.front().asset->image, kSourcePixels, 2, 1) &&
               session.history.UndoCount() == 0 && session.history.CanRedo(),
           "history.undo-restores-original-literal-source", failures);
    session.Redo();
    Expect(session.Document() && session.history.UndoCount() == 1 && !session.history.CanRedo() &&
               PixelsEqual(session.Document()->layers.front().asset->image, kScaledPixels),
           "history.redo-restores-independent-scaled-pixel-golden", failures);
}

void StaleAndCancelledRemainUnpublished(int& failures)
{
    DocumentSession stale;
    if (!InstallLiteralSource(stale))
    {
        Expect(false, "stale.fixture-installed", failures);
        return;
    }
    auto stalePrepared = PrepareResized(stale, *stale.Document());
    if (!stalePrepared)
    {
        Expect(false, "stale.prepared", failures);
        return;
    }
    stale.SetOpacity(0.5);
    const CanvasDocument changed = *stale.Document();
    Expect(!stale.IsPreparedDocumentSizeCurrent(*stalePrepared) &&
               !stale.ApplyPreparedDocumentSizeIfCurrent(*stalePrepared) &&
               stale.Document() == std::optional<CanvasDocument>{changed},
           "stale.source-refuses-checked-apply-without-changing-session", failures);

    DocumentSession cancelled;
    if (!InstallLiteralSource(cancelled))
    {
        Expect(false, "cancel.fixture-installed", failures);
        return;
    }
    const CanvasDocument original = *cancelled.Document();
    auto prepared = PrepareResized(cancelled, original);
    CancelSource stop;
    (void)stop.Cancel();
    bool refreshRefused = false;
    if (prepared)
    {
        std::thread worker([&] { refreshRefused = !prepared->RefreshHistoryResidency(stop.Token()); });
        worker.join();
    }
    Expect(refreshRefused && prepared->FinalDocument() &&
               cancelled.Document() == std::optional<CanvasDocument>{original} && cancelled.history.UndoCount() == 0,
           "cancelled.refresh-leaves-prepared-final-document-and-session-unchanged", failures);
    const auto token = cancelled.CaptureJobToken();
    const auto active = cancelled.ActiveLayerID();
    const auto sourceSnapshot = cancelled.Document();
    const auto history = cancelled.history.CaptureState();
    std::optional<PreparedDocumentSize> refused;
    std::thread prepareWorker(
        [&]
        {
            if (token)
                refused = PrepareDocumentSizePublication(sourceSnapshot, active, *token, history, original,
                                                         L"Image Size", stop.Token());
        });
    prepareWorker.join();
    Expect(!refused && cancelled.Document() == std::optional<CanvasDocument>{original} &&
               cancelled.history.UndoCount() == 0,
           "cancelled.prepare-does-not-publish-or-change-history", failures);
}

void PublicationOwnerLifetime(int& failures)
{
    {
        auto owner = NewMemoryOnWorker();
        std::weak_ptr<const SharedPublicationMemory> weak{owner};
        std::shared_ptr<const SharedPublicationMemory> supplied = owner;
        const Raster source = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
        Raster output;
        std::thread rasterWorker(
            [&]
            { output = RenderPlacedImage(source, Affine2::Scale(2, 2), 4, 2, LayerSampling::Nearest, {}, supplied); });
        rasterWorker.join();
        owner.reset();
        supplied.reset();
        const bool retainedAndCorrect = !weak.expired() && PixelsEqual(output, kScaledPixels);
        output = {};
        Expect(retainedAndCorrect && weak.expired(), "owner.raster-retains-then-releases-memory", failures);
    }
    {
        auto owner = NewMemoryOnWorker();
        std::weak_ptr<const SharedPublicationMemory> weak{owner};
        std::shared_ptr<const SharedPublicationMemory> supplied = owner;
        constexpr std::array<std::uint8_t, 2> sourcePixels{32, 224};
        constexpr std::array<std::uint8_t, 8> expected{32, 32, 224, 224, 32, 32, 224, 224};
        const Raster source = Raster::Make(2, 1, RasterFormat::Gray8, sourcePixels.data(), sourcePixels.size());
        Raster output;
        std::thread maskWorker(
            [&]
            {
                output = RenderPlacedMask(source, Affine2::Scale(2, 2), 4, 2, LayerSampling::Nearest, 0, {}, supplied);
            });
        maskWorker.join();
        owner.reset();
        supplied.reset();
        const bool retainedAndCorrect = !weak.expired() && MaskPixelsEqual(output, expected);
        output = {};
        Expect(retainedAndCorrect && weak.expired(), "owner.mask-raster-retains-then-releases-memory", failures);
    }
    {
        auto owner = NewMemoryOnWorker();
        std::weak_ptr<const SharedPublicationMemory> weak{owner};
        std::shared_ptr<const SharedPublicationMemory> supplied = owner;
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Source", Vec2{2, 1});
        const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
        constexpr std::array<std::uint8_t, 2> maskPixels{32, 224};
        const Raster mask = Raster::Make(2, 1, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
        LayerMask layerMask;
        layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"};
        layer.mask = std::move(layerMask);
        fixture.layers.push_back(std::move(layer));
        const CanvasDocument source = std::move(fixture).Seal();
        ImageSizeOptions options;
        options.width = 192;
        options.height = 96;
        options.sampling = LayerSampling::Nearest;
        CanvasDocument resized;
        bool resizedOk = false;
        std::thread resizeWorker(
            [&] { resizedOk = ResizeImage(source, options, resized, {}, supplied) == ResizeStatus::Ok; });
        resizeWorker.join();
        Raster thumbnail;
        bool separatelyAllocated = false;
        if (resizedOk && resized.layers.front().mask)
        {
            const auto& asset = resized.layers.front().mask->asset;
            thumbnail = asset.thumbnail;
            separatelyAllocated = thumbnail.Identity() != asset.image.Identity();
        }
        owner.reset();
        supplied.reset();
        resized = {};
        const auto* thumbPixels = thumbnail.HasStorage() ? thumbnail.Data() : nullptr;
        const bool retainedByThumbnail =
            separatelyAllocated && thumbnail.Width() == 96 && thumbnail.Height() == 48 &&
            thumbnail.Format() == RasterFormat::Gray8 && thumbPixels && thumbPixels[0] == 32 &&
            thumbPixels[static_cast<std::size_t>(thumbnail.Stride()) * 47 + 95] == 224 && !weak.expired();
        thumbnail = {};
        Expect(resizedOk && retainedByThumbnail && weak.expired(),
               "owner.eager-mask-thumbnail-retains-then-releases-memory", failures);
    }
    {
        auto owner = NewMemoryOnWorker();
        std::weak_ptr<const SharedPublicationMemory> weak{owner};
        std::shared_ptr<const SharedPublicationMemory> supplied = owner;
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Source", Vec2{2, 1});
        const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size());
        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
        fixture.layers.push_back(std::move(layer));
        const CanvasDocument source = std::move(fixture).Seal();
        CanvasSizeOptions options;
        options.width = 3;
        options.height = 1;
        CanvasDocument resized;
        bool resizedOk = false;
        std::thread resizeWorker(
            [&] { resizedOk = ResizeCanvas(source, options, resized, {}, supplied) == ResizeStatus::Ok; });
        resizeWorker.join();
        auto root = resized.layers;
        owner.reset();
        supplied.reset();
        resized = {};
        const bool retainedByRoot = !weak.expired() && root.size() == 1;
        root = {};
        Expect(resizedOk && retainedByRoot && weak.expired(), "owner.sealed-layer-root-retains-then-releases-memory",
               failures);
    }
    {
        auto owner = NewMemoryOnWorker();
        std::weak_ptr<const SharedPublicationMemory> weak{owner};
        std::shared_ptr<const SharedPublicationMemory> supplied = owner;
        DocumentSession session;
        if (!InstallLiteralSource(session))
        {
            Expect(false, "owner.history-fixture-installed", failures);
            return;
        }
        const CanvasDocument original = *session.Document();
        const auto token = session.CaptureJobToken();
        const auto active = session.ActiveLayerID();
        const auto sourceSnapshot = session.Document();
        const auto history = session.history.CaptureState();
        CanvasDocument result;
        ImageSizeOptions options;
        options.width = 4;
        options.height = 2;
        options.sampling = LayerSampling::Nearest;
        bool resizedOk = false;
        std::thread resizeWorker([&] { resizedOk = ResizeImage(original, options, result) == ResizeStatus::Ok; });
        resizeWorker.join();
        std::optional<PreparedDocumentSize> prepared;
        std::thread worker(
            [&]
            {
                if (resizedOk && token)
                    prepared = PrepareDocumentSizePublication(sourceSnapshot, active, *token, history,
                                                              std::move(result), L"Image Size", {}, {}, supplied);
            });
        worker.join();
        owner.reset();
        supplied.reset();
        Expect(prepared.has_value() && !weak.expired(), "owner.prepared-history-retains-supplied-memory", failures);
        DocumentHistory::ResidencyPermit permit;
        const bool applied =
            prepared && prepared->TryAcquireHistoryResidency(permit) == DocumentHistory::ResidencyValidation::Ready &&
            session.ApplyPreparedDocumentSizeIfCurrent(*prepared);
        permit.Reset();
        prepared.reset();
        Expect(applied && !weak.expired() && session.history.UndoCount() == 1,
               "owner.live-history-keeps-memory-after-worker-publication-retires", failures);
        std::optional<DocumentHistory::StateView> historyReader{session.history.CaptureState()};
        session.history.Reset();
        const bool readerRetained = !weak.expired();
        historyReader.reset();
        Expect(readerRetained && weak.expired(), "owner.last-immutable-history-reader-releases-provided-owner",
               failures);
    }
}
} // namespace

namespace SizeFinalSourceTests
{
void Run(int& failures)
{
    FinalSourceAndHistory(failures);
    StaleAndCancelledRemainUnpublished(failures);
    PublicationOwnerLifetime(failures);
}
} // namespace SizeFinalSourceTests

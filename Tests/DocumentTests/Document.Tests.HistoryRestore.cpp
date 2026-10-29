#include "Document.Tests.HistoryRestore.h"

#include "HistoryRestoreWork.h"
#include "RasterWork.h"
#include "Selection.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-restore.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool PixelEquals(const Raster& raster, std::span<const std::uint8_t> expected)
{
    const auto bytesPerPixel = raster.Format() == RasterFormat::Gray8 ? 1u : 4u;
    if (!raster.HasStorage() || raster.Width() != 1 || raster.Height() != 1 || expected.size() != bytesPerPixel)
        return false;
    const Raster contiguous = ContiguousRasterForWork(raster);
    return contiguous.HasStorage() && contiguous.Stride() == static_cast<int>(bytesPerPixel) &&
           std::equal(expected.begin(), expected.end(), contiguous.Data());
}

bool InstallFixture(DocumentSession& session, bool withMask = false, bool secondLayer = false)
{
    session.CreateDocument(1, 1, true);
    if (!session.Document() || !session.ActiveLayerID())
        return false;
    if (secondLayer)
        session.AddBlankLayer();
    constexpr std::array<std::uint8_t, 4> pixel{17, 34, 51, 255};
    constexpr std::array<std::uint8_t, 1> maskPixel{173};
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskPixel.data(), maskPixel.size());
    if (!image.HasStorage() || (withMask && !mask.HasStorage()))
        return false;
    const Guid layerID = *session.ActiveLayerID();
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                for (ImageLayer& layer : document.layers)
                                {
                                    if (layer.id == layerID)
                                    {
                                        layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
                                        if (withMask)
                                        {
                                            LayerMask layerMask;
                                            layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"mask"};
                                            layer.mask = std::move(layerMask);
                                        }
                                        break;
                                    }
                                }
                            });
    session.history.Reset();
    return session.Document()->layers.size() == (secondLayer ? 2u : 1u);
}

std::optional<PreparedHistoryRestore> Prepare(DocumentSession& session, bool redo = false,
                                              CancelToken cancellation = {},
                                              DocumentHistory::PrepareObserver observer = {})
{
    return PrepareHistoryRestorePublication(session.CaptureHistoryRestoreAuthority(), session.history.CaptureState(),
                                            redo, cancellation, observer);
}

void SeedOpacityHistory(DocumentSession& session)
{
    session.SetOpacity(0.5);
}

struct ThrowAtRestoreStage final
{
    DocumentHistory::PrepareStage stage;
    unsigned int hits = 0;

    static void Checkpoint(void* context, DocumentHistory::PrepareStage current)
    {
        auto& state = *static_cast<ThrowAtRestoreStage*>(context);
        if (state.stage == current)
        {
            ++state.hits;
            throw std::bad_alloc{};
        }
    }
};

struct CancelAtRestoreStage final
{
    DocumentHistory::PrepareStage stage;
    CancelSource cancellation;
    unsigned int hits = 0;

    static void Checkpoint(void* context, DocumentHistory::PrepareStage current)
    {
        auto& state = *static_cast<CancelAtRestoreStage*>(context);
        if (state.stage == current)
        {
            ++state.hits;
            (void)state.cancellation.Cancel();
        }
    }
};

void UndoRedoPublishesLiteralDocumentAndMetadata()
{
    constexpr std::array<std::uint8_t, 4> expectedPixel{17, 34, 51, 255};
    constexpr std::array<std::uint8_t, 1> expectedMask{173};
    DocumentSession session;
    const bool fixture = InstallFixture(session, true);
    Expect(fixture, "publish.fixture");
    if (!fixture)
        return;
    const Guid active = *session.ActiveLayerID();
    session.SelectLayerTarget(active, true);
    const Guid beforeRevision = session.history.CurrentRevision();
    SeedOpacityHistory(session);
    const Guid editedRevision = session.history.CurrentRevision();
    const auto edited = session.Document();
    const int entries = session.history.UndoCount();
    auto undo = Prepare(session);
    Expect(undo.has_value() && session.Document() == edited && session.history.CurrentRevision() == editedRevision &&
               session.history.UndoCount() == entries && session.history.UndoName() == L"Layer Opacity",
           "publish.undo-prepare-is-detached");
    if (!undo)
        return;
    Expect(session.ApplyPreparedHistoryRestoreIfCurrent(*undo) && session.Document()->layers.front().mask.has_value(),
           "publish.undo-applies");
    const auto& restored = session.Document()->layers.front();
    Expect(session.Document()->width == 1 && session.Document()->height == 1 && restored.name == L"Layer 1" &&
               restored.opacity == 1.0 && PixelEquals(restored.asset->image, expectedPixel) && restored.mask &&
               PixelEquals(restored.mask->asset.image, expectedMask) && session.ActiveLayerID() == active &&
               session.IsMaskSelected() && session.history.CurrentRevision() == beforeRevision &&
               session.history.UndoCount() == 0 && session.history.CanRedo() &&
               session.history.RedoName() == L"Layer Opacity",
           "publish.undo-literal-pixels-active-revision-and-stack-name");
    auto redo = Prepare(session, true);
    Expect(redo.has_value() && session.history.CurrentRevision() == beforeRevision && session.history.CanRedo(),
           "publish.redo-prepare-is-detached");
    if (!redo)
        return;
    Expect(session.ApplyPreparedHistoryRestoreIfCurrent(*redo), "publish.redo-applies");
    Expect(session.Document()->layers.front().opacity == 0.5 &&
               PixelEquals(session.Document()->layers.front().asset->image, expectedPixel) &&
               session.ActiveLayerID() == active && session.IsMaskSelected() &&
               session.history.CurrentRevision() == editedRevision && session.history.UndoName() == L"Layer Opacity" &&
               !session.history.CanRedo(),
           "publish.redo-literal-pixels-active-revision-and-stack-name");
    Expect(!session.ApplyPreparedHistoryRestoreIfCurrent(*redo) && session.history.CurrentRevision() == editedRevision,
           "publish.candidate-single-use");
}

void SavedRevisionChangesAfterPreparation()
{
    DocumentSession session;
    const bool fixture = InstallFixture(session);
    Expect(fixture, "saved-revision.fixture");
    if (!fixture)
        return;

    session.history.MarkSaved();
    const Guid savedBeforeEdit = session.history.CurrentRevision();
    Expect(!session.history.IsModified(), "saved-revision.fixture-starts-clean");
    SeedOpacityHistory(session);
    const Guid savedEdit = session.history.CurrentRevision();
    Expect(session.history.IsModified(), "saved-revision.edit-is-modified");

    auto undo = Prepare(session);
    Expect(undo.has_value(), "saved-revision.undo-prepared");
    if (!undo)
        return;
    session.history.MarkSaved(savedEdit);
    Expect(!session.history.IsModified(), "saved-revision.mark-saved-after-prepare");
    Expect(session.ApplyPreparedHistoryRestoreIfCurrent(undo.value()) &&
               session.history.CurrentRevision() == savedBeforeEdit && session.history.IsModified(),
           "saved-revision.undo-compares-against-post-prepare-save");

    auto redo = Prepare(session, true);
    Expect(redo.has_value(), "saved-revision.redo-prepared");
    if (!redo)
        return;
    Expect(session.ApplyPreparedHistoryRestoreIfCurrent(redo.value()) &&
               session.history.CurrentRevision() == savedEdit && !session.history.IsModified(),
           "saved-revision.redo-returns-to-post-prepare-save");
}

void NoEntryMoveAndDefaultAuthority()
{
    DocumentSession session;
    auto defaultAuthority =
        PrepareHistoryRestorePublication(HistoryRestoreAuthority{}, session.history.CaptureState(), false);
    Expect(!defaultAuthority.has_value(), "no-entry.default-authority-refused");

    auto candidate = Prepare(session);
    Expect(candidate.has_value(), "no-entry.valid-authority-prepares-noop");
    if (!candidate)
        return;
    PreparedHistoryRestore moved(std::move(candidate.value()));
    const Guid revision = session.history.CurrentRevision();
    Expect(!session.ApplyPreparedHistoryRestoreIfCurrent(candidate.value()) &&
               session.history.CurrentRevision() == revision && session.ApplyPreparedHistoryRestoreIfCurrent(moved) &&
               session.history.CurrentRevision() == revision && !session.history.CanUndo() &&
               !session.history.CanRedo(),
           "no-entry.moved-from-refused-and-moved-noop-succeeds");
}

void PreparedRestoreResidencyAndNoopSingleUseGuards()
{
    using Validation = DocumentHistory::ResidencyValidation;
    constexpr std::array<std::uint8_t, 4> historicalPixel{41, 73, 109, 255};
    constexpr std::array<std::uint8_t, 4> livePixel{3, 5, 7, 255};
    const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, historicalPixel.data(), historicalPixel.size());
    const Raster historicalSparse =
        Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 0);
    const Raster liveImage = Raster::Make(1, 1, RasterFormat::Bgra8888, livePixel.data(), livePixel.size());
    const DocumentSelection historicalSelection{false, SelectionGeometry::Rect(0, 0, 1, 1)};
    DocumentSession session(4 + historicalSelection.loops.StorageBytes());
    session.CreateDocument(2, 1, true);
    if (!session.Document() || !session.ActiveLayerID() || !leaf.HasStorage() || !historicalSparse.HasStorage() ||
        !liveImage.HasStorage())
    {
        Expect(false, "residency.fixture");
        return;
    }

    const Guid active = session.ActiveLayerID().value();
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            auto fixture = DocumentFixture::Copy(document);
            fixture.layers.front().asset = RasterAsset{historicalSparse, Raster{}, L"Historical sparse"};
            fixture.selection = historicalSelection;
            document = std::move(fixture).Seal();
        });
    session.history.Reset();
    const Guid originalRevision = session.history.CurrentRevision();
    session.history.Begin(L"Replace with live image", session.Document(), session.ActiveLayerID());
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            auto fixture = DocumentFixture::Copy(document);
            fixture.width = 1;
            fixture.height = 1;
            fixture.layers.front().asset = RasterAsset{liveImage, liveImage, L"Live image"};
            fixture.selection.reset();
            document = std::move(fixture).Seal();
        });
    session.history.End(session.Document(), session.ActiveLayerID());
    const Guid liveRevision = session.history.CurrentRevision();
    const auto liveDocument = session.Document();
    auto undo = Prepare(session);
    Expect(undo.has_value() && session.history.CurrentRevision() == liveRevision &&
               session.Document() == liveDocument && session.history.UndoName() == L"Replace with live image",
           "residency.undo-prepare-preserves-live-revision");
    if (!undo)
        return;

    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const std::uint8_t* assembled = historicalSparse.Data();
    const bool cacheGrew = assembled && historicalSparse.ByteSize() == 8 && assembled[0] == historicalPixel[0] &&
                           assembled[1] == historicalPixel[1] && assembled[2] == historicalPixel[2] &&
                           assembled[3] == historicalPixel[3] && assembled[4] == 0 && assembled[5] == 0 &&
                           assembled[6] == 0 && assembled[7] == 0 && Raster::ResidencyEpoch() == epoch + 1;
    DocumentHistory::ResidencyPermit permit;
    const auto initialTry = undo.value().TryAcquireHistoryResidency(permit);
    const auto beforeCancelledRefresh = session.history.CaptureState();
    CancelSource cancellation;
    (void)cancellation.Cancel();
    const bool cancelled = !undo.value().RefreshHistoryResidency(cancellation.Token());
    const auto afterCancelledRefresh = session.history.CaptureState();
    const bool cancelledPreserves = cancelled &&
                                    undo.value().TryAcquireHistoryResidency(permit) == Validation::Refresh &&
                                    session.Document() == liveDocument && session.ActiveLayerID() == active &&
                                    session.history.CurrentRevision() == liveRevision &&
                                    afterCancelledRefresh.PastCount() == beforeCancelledRefresh.PastCount() &&
                                    afterCancelledRefresh.FutureCount() == beforeCancelledRefresh.FutureCount() &&
                                    afterCancelledRefresh.Revision() == beforeCancelledRefresh.Revision() &&
                                    afterCancelledRefresh.UndoName() == beforeCancelledRefresh.UndoName();
    Expect(cacheGrew, "residency.undo-sparse-cache-grows-after-prepare");
    Expect(initialTry == Validation::Refresh, "residency.undo-owner-try-requests-refresh");
    Expect(cancelledPreserves, "residency.undo-cancelled-refresh-preserves-session-history-and-revision");

    const bool refreshed = undo.value().RefreshHistoryResidency();
    const auto ready = undo.value().TryAcquireHistoryResidency(permit);
    const bool applied =
        refreshed && ready == Validation::Ready && session.ApplyPreparedHistoryRestoreIfCurrent(undo.value());
    bool restoredLiteral = false;
    if (applied)
    {
        const auto& restored = session.Document().value();
        const Raster contiguous = ContiguousRasterForWork(restored.layers.front().asset.value().image);
        restoredLiteral = restored.width == 2 && restored.height == 1 && contiguous.HasStorage() &&
                          contiguous.Stride() == 8 && contiguous.ByteSize() == 8 &&
                          std::equal(historicalPixel.begin(), historicalPixel.end(), contiguous.Data()) &&
                          contiguous.Data()[4] == 0 && contiguous.Data()[5] == 0 && contiguous.Data()[6] == 0 &&
                          contiguous.Data()[7] == 0 && restored.selection == std::optional{historicalSelection} &&
                          session.ActiveLayerID() == active && session.history.CurrentRevision() == originalRevision &&
                          session.history.CanRedo() && session.history.RedoName() == L"Replace with live image" &&
                          !session.history.CanUndo() && session.history.RetainedBytes(session.Document()) == 4;
    }
    const bool appliedRefusal = applied && undo.value().TryAcquireHistoryResidency(permit) == Validation::Invalid &&
                                !undo.value().RefreshHistoryResidency();
    Expect(applied && restoredLiteral,
           "residency.undo-refreshed-permit-applies-original-pixels-selection-revision-and-live-exclusion");
    Expect(appliedRefusal, "residency.undo-applied-output-refuses-residency-reuse");
    permit.Reset();
    if (!applied)
        return;

    auto redo = Prepare(session, true);
    DocumentHistory::ResidencyPermit redoPermit;
    const auto redoReady = redo ? redo->TryAcquireHistoryResidency(redoPermit) : Validation::Invalid;
    const bool redoApplied = redo.has_value() && redoReady == Validation::Ready &&
                             session.ApplyPreparedHistoryRestoreIfCurrent(redo.value());
    const bool redoLiteral = redoApplied && session.Document().value().width == 1 &&
                             session.Document().value().height == 1 &&
                             PixelEquals(session.Document().value().layers.front().asset.value().image, livePixel) &&
                             !session.Document().value().selection.has_value() && session.ActiveLayerID() == active &&
                             session.history.CurrentRevision() == liveRevision && !session.history.CanRedo();
    Expect(redoApplied && redoLiteral, "residency.redo-ready-permit-restores-live-pixels-selection-and-revision");

    DocumentSession empty;
    auto noOp = Prepare(empty);
    DocumentHistory::ResidencyPermit noOpPermit;
    const auto noOpReady = noOp ? noOp->TryAcquireHistoryResidency(noOpPermit) : Validation::Invalid;
    Expect(noOp.has_value() && noOpReady == Validation::Ready && !empty.history.CanUndo() && !empty.history.CanRedo(),
           "residency.noop-empty-stack-output-is-ready");
    if (!noOp)
        return;
    PreparedHistoryRestore movedNoOp(std::move(noOp.value()));
    const bool movedSourceRefusal = noOp.value().TryAcquireHistoryResidency(noOpPermit) == Validation::Invalid &&
                                    !noOp.value().RefreshHistoryResidency();
    const Guid emptyRevision = empty.history.CurrentRevision();
    const bool noOpApplied = empty.ApplyPreparedHistoryRestoreIfCurrent(movedNoOp) &&
                             empty.history.CurrentRevision() == emptyRevision && !empty.Document() &&
                             !empty.history.CanUndo() && !empty.history.CanRedo();
    const bool noOpAppliedRefusal = movedNoOp.TryAcquireHistoryResidency(noOpPermit) == Validation::Invalid &&
                                    !movedNoOp.RefreshHistoryResidency() &&
                                    !empty.ApplyPreparedHistoryRestoreIfCurrent(movedNoOp);
    Expect(movedSourceRefusal, "residency.noop-moved-source-refuses-residency-operations");
    Expect(noOpApplied && noOpAppliedRefusal,
           "residency.noop-applied-output-refuses-residency-operations-and-republication");
}

void OpenTransactionRefusesUndoAndRedoPreparation()
{
    DocumentSession session;
    const bool fixture = InstallFixture(session);
    Expect(fixture, "open-transaction.fixture");
    if (!fixture)
        return;
    SeedOpacityHistory(session);
    session.SetOpacity(0.25);
    auto undo = Prepare(session);
    if (!undo || !session.ApplyPreparedHistoryRestoreIfCurrent(undo.value()))
    {
        Expect(false, "open-transaction.seed-redo");
        return;
    }
    session.history.Begin(L"Open Transaction", session.Document(), session.ActiveLayerID());
    const auto before = session.history.CaptureState();
    const auto document = session.Document();
    Expect(before.Depth() == 1 && before.PastCount() == 1 && before.FutureCount() == 1 &&
               before.UndoName() == L"Layer Opacity" && before.RedoName() == L"Layer Opacity",
           "open-transaction.fixture-has-both-stacks");

    auto blockedUndo = Prepare(session);
    Expect(!blockedUndo.has_value(), "open-transaction.undo-preparation-refused");
    auto blockedRedo = Prepare(session, true);
    Expect(!blockedRedo.has_value(), "open-transaction.redo-preparation-refused");

    const auto after = session.history.CaptureState();
    Expect(session.Document() == document && session.Document().value().layers.front().opacity == 0.5 &&
               session.history.CurrentRevision() == before.Revision() && after.Depth() == before.Depth() &&
               after.PastCount() == before.PastCount() && after.FutureCount() == before.FutureCount() &&
               after.UndoName() == before.UndoName() && after.RedoName() == before.RedoName(),
           "open-transaction.refusals-preserve-document-revision-stacks-and-depth");
    session.history.End(session.Document(), session.ActiveLayerID());
}

void LassoInvalidatesPreparedRestoreWithoutGenerationChange()
{
    DocumentSession session;
    const bool fixture = InstallFixture(session);
    Expect(fixture, "lasso-authority.fixture");
    if (!fixture)
        return;
    SeedOpacityHistory(session);
    auto candidate = Prepare(session);
    Expect(candidate.has_value(), "lasso-authority.restore-prepared");
    if (!candidate)
        return;

    const std::uint64_t generation = session.Generation();
    const Guid revision = session.history.CurrentRevision();
    const auto document = session.Document();
    constexpr Vec2 start{0.25, 0.5};
    session.BeginLasso(start, SelectionMode::Add);
    const auto& pending = session.PendingLasso();
    Expect(session.Generation() == generation && pending.has_value() && pending.value().points.size() == 1 &&
               pending.value().points.front() == start && pending.value().mode == SelectionMode::Add,
           "lasso-authority.begin-does-not-change-generation-and-keeps-lasso");
    Expect(!session.ApplyPreparedHistoryRestoreIfCurrent(candidate.value()) && session.Document() == document &&
               session.history.CurrentRevision() == revision && session.PendingLasso().has_value() &&
               session.PendingLasso().value().points.front() == start &&
               session.PendingLasso().value().mode == SelectionMode::Add,
           "lasso-authority.stale-candidate-refused-and-lasso-intact");
    session.CancelLasso();
    Expect(!session.PendingLasso().has_value(), "lasso-authority.cancel-cleans-up");
}

void ActiveChangeDropsMaskAndCropRules()
{
    DocumentSession changedActive;
    const bool fixture = InstallFixture(changedActive, true, true);
    Expect(fixture, "target.fixture");
    if (!fixture)
        return;
    const Guid sourceActive = *changedActive.ActiveLayerID();
    changedActive.SelectLayerTarget(sourceActive, true);
    SeedOpacityHistory(changedActive);
    const Guid other = changedActive.Document()->layers.front().id == sourceActive
                           ? changedActive.Document()->layers.back().id
                           : changedActive.Document()->layers.front().id;
    changedActive.SelectLayerTarget(other, false);
    auto undo = Prepare(changedActive);
    Expect(undo.has_value() && changedActive.ApplyPreparedHistoryRestoreIfCurrent(*undo) &&
               changedActive.ActiveLayerID() == sourceActive && !changedActive.IsMaskSelected(),
           "target.changed-active-restores-snapshot-and-drops-mask-target");

    DocumentSession keepMask;
    const bool maskFixture = InstallFixture(keepMask, true);
    Expect(maskFixture, "target.mask-fixture");
    if (!maskFixture)
        return;
    const Guid maskedID = *keepMask.ActiveLayerID();
    keepMask.SelectLayerTarget(maskedID, true);
    SeedOpacityHistory(keepMask);
    auto maskUndo = Prepare(keepMask);
    Expect(maskUndo.has_value() && keepMask.ApplyPreparedHistoryRestoreIfCurrent(*maskUndo) &&
               keepMask.ActiveLayerID() == maskedID && keepMask.IsMaskSelected(),
           "target.same-active-with-mask-keeps-mask-target");

    DocumentSession crop;
    const bool cropFixture = InstallFixture(crop);
    Expect(cropFixture, "crop.fixture");
    if (!cropFixture)
        return;
    SeedOpacityHistory(crop);
    const CropRect pending{0, 0, 1, 1};
    crop.SetCropFrame(pending);
    auto cropUndo = Prepare(crop);
    Expect(cropUndo.has_value() && crop.CropFrame() == pending &&
               crop.ApplyPreparedHistoryRestoreIfCurrent(*cropUndo) && !crop.CropFrame().has_value(),
           "crop.actual-restore-clears-frame");

    DocumentSession noEntry;
    const bool noEntryFixture = InstallFixture(noEntry);
    Expect(noEntryFixture, "crop.no-entry-fixture");
    if (!noEntryFixture)
        return;
    SeedOpacityHistory(noEntry);
    auto firstUndo = Prepare(noEntry);
    Expect(firstUndo.has_value() && noEntry.ApplyPreparedHistoryRestoreIfCurrent(*firstUndo), "crop.seed-redo");
    const Guid redoRevision = noEntry.history.CurrentRevision();
    const std::wstring redoName = noEntry.history.RedoName();
    noEntry.SetCropFrame(pending);
    auto emptyUndo = Prepare(noEntry);
    Expect(emptyUndo.has_value() && noEntry.ApplyPreparedHistoryRestoreIfCurrent(*emptyUndo) &&
               noEntry.CropFrame() == pending && noEntry.history.CurrentRevision() == redoRevision &&
               noEntry.history.CanRedo() && noEntry.history.RedoName() == redoName,
           "crop.no-entry-preserves-frame-revision-and-redo");
}

void NewCanvasUndoRedoAndRestoreMemoryEstimate()
{
    DocumentSession session;
    const Guid emptyRevision = session.history.CurrentRevision();
    session.CreateDocument(1, 1, true);
    const Guid active = *session.ActiveLayerID();
    const Guid newCanvasRevision = session.history.CurrentRevision();
    Expect(session.history.UndoName() == L"New Canvas", "new-canvas.entry-name");
    const std::uint64_t editBytes = session.history.EditMemoryBytes(session.Document());
    const std::uint64_t restoreBytes = session.history.RestoreMemoryBytes(session.Document(), false);
    Expect(restoreBytes == editBytes, "memory.undo-to-empty-has-no-selected-document-metadata");
    auto undo = Prepare(session);
    Expect(undo.has_value() && session.ApplyPreparedHistoryRestoreIfCurrent(*undo) && !session.Document() &&
               !session.ActiveLayerID() && session.history.CurrentRevision() == emptyRevision &&
               session.history.CanRedo() && session.history.RedoName() == L"New Canvas",
           "new-canvas.undo-restores-empty-session");
    const std::uint64_t emptyLiveEditBytes = session.history.EditMemoryBytes(session.Document());
    const std::uint64_t redoRestoreBytes = session.history.RestoreMemoryBytes(session.Document(), true);
    constexpr std::uint64_t minimumCopiedRecord = sizeof(ImageLayer) + 8 * sizeof(wchar_t);
    Expect(redoRestoreBytes >= emptyLiveEditBytes + minimumCopiedRecord,
           "memory.redo-estimate-includes-selected-layer-metadata");
    auto redo = Prepare(session, true);
    Expect(redo.has_value() && session.ApplyPreparedHistoryRestoreIfCurrent(*redo) && session.Document() &&
               session.Document()->width == 1 && session.Document()->height == 1 &&
               session.Document()->layers.size() == 1 && !session.Document()->layers.front().asset &&
               session.ActiveLayerID() == active && session.history.CurrentRevision() == newCanvasRevision &&
               session.history.UndoName() == L"New Canvas" && !session.history.CanRedo(),
           "new-canvas.redo-restores-document-and-active-layer");
}

void StaleAuthorityAndOwnershipControls()
{
    const auto stale = [](const char* name, auto invalidate)
    {
        DocumentSession session;
        if (!InstallFixture(session, true, true))
        {
            Expect(false, "stale.fixture");
            return;
        }
        if (std::string_view{name} == "stale.mask-selection")
            session.SelectLayerTarget(*session.ActiveLayerID(), true);
        SeedOpacityHistory(session);
        auto candidate = Prepare(session);
        if (!candidate)
        {
            Expect(false, "stale.prepare");
            return;
        }
        const Guid revision = session.history.CurrentRevision();
        invalidate(session);
        const auto document = session.Document();
        const auto active = session.ActiveLayerID();
        const auto mask = session.IsMaskSelected();
        const auto crop = session.CropFrame();
        const auto entries = session.history.UndoCount();
        const auto redo = session.history.RedoName();
        Expect(!session.ApplyPreparedHistoryRestoreIfCurrent(*candidate) &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == entries &&
                   session.history.RedoName() == redo && session.Document() == document &&
                   session.ActiveLayerID() == active && session.IsMaskSelected() == mask && session.CropFrame() == crop,
               name);
    };

    stale("stale.epoch",
          [](DocumentSession& session)
          {
              session.history.Begin(L"No Change", session.Document(), session.ActiveLayerID());
              session.history.End(session.Document(), session.ActiveLayerID());
          });
    stale("stale.generation", [](DocumentSession& session)
          { session.EditForTest([](CanvasDocument& document) { document.resolution = 300; }); });
    stale("stale.active-layer",
          [](DocumentSession& session)
          {
              const Guid active = *session.ActiveLayerID();
              const Guid other = session.Document()->layers.front().id == active
                                     ? session.Document()->layers.back().id
                                     : session.Document()->layers.front().id;
              session.SelectLayerTarget(other, false);
          });
    stale("stale.mask-selection",
          [](DocumentSession& session) { session.SelectLayerTarget(*session.ActiveLayerID(), false); });
    stale("stale.crop-only", [](DocumentSession& session) { session.SetCropFrame(CropRect{0, 0, 1, 1}); });

    DocumentSession owner;
    DocumentSession foreign;
    const bool fixtures = InstallFixture(owner) && InstallFixture(foreign);
    Expect(fixtures, "ownership.fixtures");
    if (!fixtures)
        return;
    SeedOpacityHistory(owner);
    auto foreignCandidate = Prepare(owner);
    const Guid foreignRevision = foreign.history.CurrentRevision();
    Expect(foreignCandidate.has_value() && !foreign.ApplyPreparedHistoryRestoreIfCurrent(*foreignCandidate) &&
               foreign.history.CurrentRevision() == foreignRevision && owner.history.CanUndo(),
           "ownership.foreign-session-refused");

    auto movedFrom = Prepare(owner);
    if (!movedFrom)
    {
        Expect(false, "ownership.move-prepare");
        return;
    }
    PreparedHistoryRestore moved(std::move(movedFrom.value()));
    Expect(!owner.ApplyPreparedHistoryRestoreIfCurrent(movedFrom.value()) && owner.history.CanUndo() &&
               owner.ApplyPreparedHistoryRestoreIfCurrent(moved),
           "ownership.moved-from-refused-and-moved-candidate-applies");
}

void PreparationFailuresLeaveLiveStateUntouched()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::RestoreSnapshot,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (const bool redo : {false, true})
        for (const auto stage : stages)
        {
            DocumentSession session;
            if (!InstallFixture(session))
            {
                Expect(false, "failure.fixture");
                return;
            }
            SeedOpacityHistory(session);
            session.SetOpacity(0.25);
            auto seed = Prepare(session);
            if (!seed || !session.ApplyPreparedHistoryRestoreIfCurrent(seed.value()))
            {
                Expect(false, "failure.seed-redo");
                return;
            }
            const auto document = session.Document();
            const Guid revision = session.history.CurrentRevision();
            ThrowAtRestoreStage state{stage};
            const DocumentHistory::PrepareObserver observer{ThrowAtRestoreStage::Checkpoint, &state};
            bool threw = false;
            try
            {
                static_cast<void>(Prepare(session, redo, {}, observer));
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            Expect(threw && state.hits == 1 && session.Document() == document &&
                       session.history.CurrentRevision() == revision && session.history.UndoCount() == 1 &&
                       session.history.CanRedo() && session.history.RedoName() == L"Layer Opacity" &&
                       session.history.UndoName() == L"Layer Opacity",
                   "failure.bad-alloc-preserves-live-document-revision-and-undo");
        }

    for (const bool redo : {false, true})
        for (const auto stage : stages)
        {
            DocumentSession session;
            if (!InstallFixture(session))
            {
                Expect(false, "cancel.fixture");
                return;
            }
            SeedOpacityHistory(session);
            session.SetOpacity(0.25);
            auto seed = Prepare(session);
            if (!seed || !session.ApplyPreparedHistoryRestoreIfCurrent(seed.value()))
            {
                Expect(false, "cancel.seed-redo");
                return;
            }
            const auto document = session.Document();
            const Guid revision = session.history.CurrentRevision();
            CancelAtRestoreStage state{stage};
            const DocumentHistory::PrepareObserver observer{CancelAtRestoreStage::Checkpoint, &state};
            const auto prepared = Prepare(session, redo, state.cancellation.Token(), observer);
            Expect(!prepared && state.hits == 1 && session.Document() == document &&
                       session.history.CurrentRevision() == revision && session.history.UndoCount() == 1 &&
                       session.history.CanRedo() && session.history.RedoName() == L"Layer Opacity" &&
                       session.history.UndoName() == L"Layer Opacity",
                   "cancel.checkpoint-preserves-live-document-revision-and-undo");
        }
}
} // namespace

int RunHistoryRestoreTests()
{
    gFailures = 0;
    UndoRedoPublishesLiteralDocumentAndMetadata();
    SavedRevisionChangesAfterPreparation();
    NoEntryMoveAndDefaultAuthority();
    PreparedRestoreResidencyAndNoopSingleUseGuards();
    OpenTransactionRefusesUndoAndRedoPreparation();
    LassoInvalidatesPreparedRestoreWithoutGenerationChange();
    ActiveChangeDropsMaskAndCropRules();
    NewCanvasUndoRedoAndRestoreMemoryEstimate();
    StaleAuthorityAndOwnershipControls();
    PreparationFailuresLeaveLiveStateUntouched();
    return gFailures;
}

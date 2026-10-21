#include "Document.Tests.DocumentSizeHistory.h"

#include "CanvasSize.h"
#include "Document.h"
#include "DocumentSizeHistory.h"
#include "RasterWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s document-size-history.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(std::span<const std::uint8_t> bytes)
{
    return Raster::Make(1, 1, RasterFormat::Gray8, bytes.data(), bytes.size());
}

bool BytesEqual(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() < 1 ||
        raster.Height() < 1 ||
        expected.size() != static_cast<std::size_t>(raster.Width()) * static_cast<std::size_t>(raster.Height()) * 4u)
        return false;
    const Raster contiguous = ContiguousRasterForWork(raster);
    if (!contiguous.HasStorage() ||
        static_cast<std::size_t>(contiguous.Stride()) * contiguous.Height() != expected.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (contiguous.Data()[i] != expected[i])
            return false;
    return true;
}

DocumentSelection SelectionFixture()
{
    DocumentSelection selection;
    selection.antialiased = false;
    selection.loops =
        SelectionLoops{std::vector<SelectionLoop>{SelectionLoop{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};
    return selection;
}

bool InstallCanvasFixture(DocumentSession& session)
{
    session.CreateDocument(2, 2, true);
    if (!session.Document() || !session.ActiveLayerID())
        return false;
    constexpr std::array<std::uint8_t, 16> source{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    constexpr std::array<std::uint8_t, 1> gray{173};
    const Raster image = Bgra(2, 2, source);
    const Raster mask = Gray(gray);
    if (!image.HasStorage() || !mask.HasStorage())
        return false;
    const Guid id = *session.ActiveLayerID();
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            ImageLayer& layer = document.layers.front();
            layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
            LayerMask layerMask;
            layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"mask"};
            layerMask.placement = LayerTransform{Vec2{0, 0}, Vec2{2, 2}};
            layer.mask = std::move(layerMask);
            document.selection = SelectionFixture();
        });
    session.SelectLayerTarget(id, true);
    session.SetCropFrame(CropRect{0, 0, 2, 2});
    if (session.Document()->layers.size() != 1 || !session.Document()->layers.front().mask)
        return false;
    session.history.Reset();
    return true;
}

bool InstallImageFixture(DocumentSession& session)
{
    session.CreateDocument(2, 1, true);
    if (!session.Document() || !session.ActiveLayerID())
        return false;
    constexpr std::array<std::uint8_t, 8> source{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 1> gray{173};
    const Raster image = Bgra(2, 1, source);
    const Raster mask = Gray(gray);
    if (!image.HasStorage() || !mask.HasStorage())
        return false;
    const Guid id = *session.ActiveLayerID();
    session.EditForTest(
        [&](CanvasDocument& document)
        {
            ImageLayer& layer = document.layers.front();
            layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
            LayerMask layerMask;
            layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"mask"};
            layer.mask = std::move(layerMask);
            document.selection = SelectionFixture();
        });
    session.SelectLayerTarget(id, true);
    if (session.Document()->layers.size() != 1 || !session.Document()->layers.front().mask)
        return false;
    session.history.Reset();
    return true;
}

std::optional<PreparedDocumentSize> Prepare(DocumentSession& session, CanvasDocument result, const wchar_t* name,
                                            CancelToken cancellation = {},
                                            DocumentHistory::PrepareObserver observer = {})
{
    const auto token = session.CaptureJobToken();
    if (!token)
        return std::nullopt;
    return PrepareDocumentSizePublication(session.Document(), session.ActiveLayerID(), *token,
                                          session.history.CaptureState(), std::move(result), name, cancellation,
                                          observer);
}

std::optional<PreparedDocumentSize> PrepareCanvas(DocumentSession& session, CanvasDocument& resized)
{
    CanvasSizeOptions options;
    options.width = 4;
    options.height = 3;
    options.anchor = 8;
    options.fill = CanvasExtensionColor{1.0, 0.5, 0.0};
    if (ResizeCanvas(*session.Document(), options, resized) != ResizeStatus::Ok)
        return std::nullopt;
    return Prepare(session, resized, L"Canvas Size");
}

std::optional<PreparedDocumentSize> PrepareImage(DocumentSession& session, CanvasDocument& resized)
{
    ImageSizeOptions options;
    options.width = 4;
    options.height = 2;
    options.resolution = 300;
    options.sampling = LayerSampling::Nearest;
    if (ResizeImage(*session.Document(), options, resized) != ResizeStatus::Ok)
        return std::nullopt;
    return Prepare(session, resized, L"Image Size");
}

void CanvasLiteralApplyUndoRedoAndRetirement()
{
    constexpr std::array<std::uint8_t, 48> expectedExtension{
        0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255,
        0, 0,   0,   0,   0, 0,   0,   0,   0, 128, 255, 255, 0, 128, 255, 255, 0, 0,   0,   0,   0, 0,   0,   0};
    constexpr std::array<std::uint8_t, 1> expectedMask{173};
    DocumentSession session;
    const bool installed = InstallCanvasFixture(session);
    Expect(installed, "canvas.fixture-installed");
    if (!installed || !session.Document() || !session.ActiveLayerID())
        return;
    const CanvasDocument before = *session.Document();
    const Guid active = *session.ActiveLayerID();
    const auto selection = session.Selection();
    const auto crop = session.CropFrame();
    const int undoBefore = session.history.UndoCount();
    CanvasDocument result;
    auto prepared = PrepareCanvas(session, result);
    Expect(prepared.has_value() && *session.Document() == before && session.Selection() == selection &&
               session.history.UndoCount() == undoBefore,
           "canvas.prepare-does-not-publish");
    if (!prepared)
        return;
    const std::uint64_t retirement = session.DocumentSizeRetirementMemoryBytes();
    Expect(retirement > 0, "canvas.retirement-reservation-nonzero");
    const bool applied = session.ApplyPreparedDocumentSizeIfCurrent(*prepared);
    const CanvasDocument after = *session.Document();
    const ImageLayer* extension = after.layers.size() == 2 ? &after.layers.front() : nullptr;
    const ImageLayer* oldLayer = after.layers.size() == 2 ? &after.layers.back() : nullptr;
    Expect(applied && after.width == 4 && after.height == 3 && extension && extension->name == L"Canvas Extension" &&
               BytesEqual(extension->asset->image, expectedExtension) && oldLayer && oldLayer->id == active &&
               oldLayer->transform.origin == Vec2{2, 1} && oldLayer->mask && oldLayer->mask->placement &&
               oldLayer->mask->placement->origin == Vec2{2, 1} &&
               std::span<const std::uint8_t>{oldLayer->mask->asset.image.Data(), 1}[0] == expectedMask[0] &&
               !after.selection && session.ActiveLayerID() == active && session.IsMaskSelected() &&
               session.CropFrame() == crop && session.history.UndoCount() == undoBefore + 1 &&
               session.history.UndoName() == L"Canvas Size",
           "canvas.apply-preserves-size-golden-active-mask-target-and-crop");
    Expect(!session.ApplyPreparedDocumentSizeIfCurrent(*prepared) && session.history.UndoCount() == undoBefore + 1,
           "canvas.candidate-single-use");
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{before} && session.Selection() == selection &&
               session.ActiveLayerID() == active && session.IsMaskSelected() && session.history.CanRedo(),
           "canvas.undo-restores-selection-mask-and-source");
    session.Redo();
    Expect(session.Document() == std::optional<CanvasDocument>{after} && !session.Selection() &&
               session.ActiveLayerID() == active && session.history.UndoName() == L"Canvas Size",
           "canvas.redo-restores-result");
}

void ImageNearestAndResolutionOnlyUndoRedo()
{
    constexpr std::array<std::uint8_t, 32> expectedImage{10,  20,  30, 255, 10,  20,  30, 255, 40,  50, 60,
                                                         255, 40,  50, 60,  255, 10,  20, 30,  255, 10, 20,
                                                         30,  255, 40, 50,  60,  255, 40, 50,  60,  255};
    constexpr std::array<std::uint8_t, 1> expectedMask{173};
    DocumentSession session;
    const bool installed = InstallImageFixture(session);
    Expect(installed, "image.fixture-installed");
    if (!installed || !session.Document() || !session.ActiveLayerID())
        return;
    const CanvasDocument before = *session.Document();
    const Guid active = *session.ActiveLayerID();
    CanvasDocument result;
    auto prepared = PrepareImage(session, result);
    Expect(prepared.has_value() && *session.Document() == before, "image.prepare-does-not-publish");
    if (!prepared)
        return;
    const bool applied = session.ApplyPreparedDocumentSizeIfCurrent(*prepared);
    const CanvasDocument after = *session.Document();
    const ImageLayer* layer = after.layers.empty() ? nullptr : &after.layers.front();
    Expect(applied && after.width == 4 && after.height == 2 && after.resolution == 300 && layer && layer->asset &&
               BytesEqual(layer->asset->image, expectedImage) && layer->mask &&
               layer->mask->asset.image == before.layers.front().mask->asset.image &&
               std::span<const std::uint8_t>{layer->mask->asset.image.Data(), 1}[0] == expectedMask[0] &&
               session.ActiveLayerID() == active && session.IsMaskSelected() && !after.selection &&
               session.history.UndoName() == L"Image Size",
           "image.nearest-literal-mask-and-selection");
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{before} && session.Selection() == before.selection &&
               session.ActiveLayerID() == active && session.history.CanRedo(),
           "image.undo-restores-mask-selection-and-source");
    session.Redo();
    Expect(session.Document() == std::optional<CanvasDocument>{after} && !session.Selection() &&
               session.ActiveLayerID() == active,
           "image.redo-restores-nearest-result");

    DocumentSession resolution;
    resolution.CreateDocument(2, 1);
    if (!resolution.Document())
    {
        Expect(false, "resolution.fixture-document-created");
        return;
    }
    resolution.AddBlankLayer();
    if (!resolution.ActiveLayerID())
    {
        Expect(false, "resolution.fixture-layer-created");
        return;
    }
    resolution.SetSelection(SelectionFixture(), L"Select");
    resolution.history.Reset();
    const CanvasDocument resolutionBefore = *resolution.Document();
    ImageSizeOptions options;
    options.width = 2;
    options.height = 1;
    options.resolution = 300;
    CanvasDocument resolutionResult;
    Expect(ResizeImage(resolutionBefore, options, resolutionResult) == ResizeStatus::Ok &&
               resolutionResult.layers.front().transform == resolutionBefore.layers.front().transform &&
               resolutionResult.resolution == 300 && resolutionResult.selection == resolutionBefore.selection,
           "resolution-only-existing-result-and-selection");
    const int resolutionUndo = resolution.history.UndoCount();
    auto resolutionPrepared = Prepare(resolution, resolutionResult, L"Image Size");
    Expect(resolutionPrepared.has_value() && resolution.ApplyPreparedDocumentSizeIfCurrent(*resolutionPrepared) &&
               resolution.history.UndoCount() == resolutionUndo + 1 && resolution.history.UndoName() == L"Image Size",
           "resolution-only-publishes-one-entry");
    resolution.Undo();
    Expect(resolution.Document()->resolution == 72 && resolution.Selection() == resolutionBefore.selection,
           "resolution-only-undo");
    resolution.Redo();
    Expect(resolution.Document()->resolution == 300 && resolution.Selection() == resolutionBefore.selection,
           "resolution-only-redo");
}

struct ThrowAtHistoryStage final
{
    DocumentHistory::PrepareStage stage;
    static void Checkpoint(void* context, DocumentHistory::PrepareStage current)
    {
        if (static_cast<ThrowAtHistoryStage*>(context)->stage == current)
            throw std::bad_alloc{};
    }
};

std::optional<PreparedDocumentSize> PrepareObserved(DocumentSession& session, CanvasDocument result,
                                                    DocumentHistory::PrepareObserver observer)
{
    const auto token = session.CaptureJobToken();
    if (!token)
        return std::nullopt;
    return PrepareDocumentSizePublication(session.Document(), session.ActiveLayerID(), *token,
                                          session.history.CaptureState(), std::move(result), L"Image Size", {},
                                          observer);
}

void ObserverAllocationFailuresPreserveSeededRedo()
{
    constexpr std::array<DocumentHistory::PrepareStage, 5> stages{
        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
        DocumentHistory::PrepareStage::TrimIndex};
    for (const DocumentHistory::PrepareStage stage : stages)
    {
        DocumentSession session;
        if (!InstallImageFixture(session))
        {
            Expect(false, "observer.fixture-installed");
            return;
        }
        Expect(true, "observer.fixture-installed");
        session.SetOpacity(0.5);
        session.Undo();
        const CanvasDocument source = *session.Document();
        const auto token = session.CaptureJobToken();
        ImageSizeOptions options;
        options.width = 4;
        options.height = 2;
        options.resolution = 300;
        options.sampling = LayerSampling::Nearest;
        CanvasDocument result;
        const bool resized = ResizeImage(source, options, result) == ResizeStatus::Ok;
        ThrowAtHistoryStage throwing{stage};
        DocumentHistory::PrepareObserver observer{&ThrowAtHistoryStage::Checkpoint, &throwing};
        bool badAlloc = false;
        try
        {
            if (resized && token)
                (void)PrepareDocumentSizePublication(session.Document(), session.ActiveLayerID(), *token,
                                                     session.history.CaptureState(), std::move(result), L"Image Size",
                                                     {}, observer);
        }
        catch (const std::bad_alloc&)
        {
            badAlloc = true;
        }
        Expect(badAlloc && session.Document() == std::optional<CanvasDocument>{source} && session.history.CanRedo() &&
                   session.history.RedoName() == L"Layer Opacity" && session.history.UndoCount() == 0 && token &&
                   session.CanApplyJobResult(*token, token->sequence),
               "observer-bad-alloc-preserves-document-token-and-seeded-redo");
    }
}

void StaleTokenHistoryEpochForeignMovedAndSingleUse()
{
    // A document mutation invalidates the whole-document token.
    {
        DocumentSession session;
        if (!InstallCanvasFixture(session))
        {
            Expect(false, "stale-token.fixture-installed");
            return;
        }
        Expect(true, "stale-token.fixture-installed");
        CanvasDocument result;
        auto prepared = PrepareCanvas(session, result);
        session.SetOpacity(0.5);
        const CanvasDocument changed = *session.Document();
        const int count = session.history.UndoCount();
        Expect(prepared && !session.ApplyPreparedDocumentSizeIfCurrent(*prepared) &&
                   session.Document() == std::optional<CanvasDocument>{changed} &&
                   session.history.UndoCount() == count && session.history.UndoName() == L"Layer Opacity",
               "stale-token-refuses-without-mutation");
    }

    // Begin/End changes only history epoch here; the captured document token remains current.
    {
        DocumentSession session;
        if (!InstallImageFixture(session))
        {
            Expect(false, "epoch.fixture-installed");
            return;
        }
        Expect(true, "epoch.fixture-installed");
        CanvasDocument result;
        auto prepared = PrepareImage(session, result);
        const CanvasDocument source = *session.Document();
        const auto token = session.CaptureJobToken();
        session.history.Begin(L"Epoch-only", session.Document(), session.ActiveLayerID());
        session.history.End(session.Document(), session.ActiveLayerID());
        Expect(token && session.CanApplyJobResult(*token, token->sequence), "epoch.token-still-current");
        Expect(prepared && !session.ApplyPreparedDocumentSizeIfCurrent(*prepared) &&
                   session.Document() == std::optional<CanvasDocument>{source} && session.history.UndoCount() == 0 &&
                   session.history.CaptureState().Depth() == 0,
               "epoch-only-begin-end-refuses-prepared-size");
    }

    // Foreign session refusal retains the move-only candidate; moved-from and repeated apply are refused.
    {
        DocumentSession owner;
        DocumentSession foreign;
        if (!InstallCanvasFixture(owner) || !InstallCanvasFixture(foreign))
        {
            Expect(false, "ownership.fixtures-installed");
            return;
        }
        Expect(true, "ownership.fixtures-installed");
        CanvasDocument result;
        auto prepared = PrepareCanvas(owner, result);
        if (!prepared)
        {
            Expect(false, "ownership.candidate-prepared");
            return;
        }
        PreparedDocumentSize moved(std::move(*prepared));
        const CanvasDocument foreignBefore = *foreign.Document();
        const bool foreignRefused =
            !foreign.ApplyPreparedDocumentSizeIfCurrent(moved) && *foreign.Document() == foreignBefore;
        const bool movedFromRefused = !owner.ApplyPreparedDocumentSizeIfCurrent(*prepared);
        const bool ownerApplied = owner.ApplyPreparedDocumentSizeIfCurrent(moved);
        const int afterApply = owner.history.UndoCount();
        const bool reusedRefused =
            !owner.ApplyPreparedDocumentSizeIfCurrent(moved) && owner.history.UndoCount() == afterApply;
        Expect(foreignRefused && movedFromRefused && ownerApplied && reusedRefused,
               "ownership-foreign-moved-from-and-single-use");
    }
}

void NestedTransactionAndNoOpRedo()
{
    DocumentSession nested;
    nested.CreateDocument(2, 1);
    if (!nested.Document())
    {
        Expect(false, "nested.fixture-document-created");
        return;
    }
    nested.AddBlankLayer();
    if (!nested.ActiveLayerID())
    {
        Expect(false, "nested.fixture-layer-created");
        return;
    }
    nested.history.Reset();
    const CanvasDocument original = *nested.Document();
    const int before = nested.history.UndoCount();
    nested.BeginEdit(L"Outer");
    nested.EditForTest([](CanvasDocument& document) { document.resolution = 96; });
    const CanvasDocument nestedSource = *nested.Document();
    ImageSizeOptions options;
    options.width = 4;
    options.height = 2;
    options.resolution = 144;
    CanvasDocument sized;
    const bool resizeOk = ResizeImage(nestedSource, options, sized) == ResizeStatus::Ok;
    const auto token = nested.CaptureJobToken();
    auto prepared = resizeOk && token
                        ? PrepareDocumentSizePublication(nested.Document(), nested.ActiveLayerID(), *token,
                                                         nested.history.CaptureState(), std::move(sized), L"Image Size")
                        : std::nullopt;
    const bool appliedInside = prepared && nested.ApplyPreparedDocumentSizeIfCurrent(*prepared);
    nested.EndEdit();
    Expect(appliedInside && nested.history.CaptureState().Depth() == 0 && nested.history.UndoCount() == before + 1 &&
               nested.history.UndoName() == L"Outer" && nested.Document()->width == 4 &&
               nested.Document()->resolution == 144,
           "nested-size-joins-outer-transaction");
    nested.Undo();
    Expect(*nested.Document() == original && nested.history.CanRedo(), "nested-undo-restores-opening-state");
    nested.Redo();
    Expect(nested.Document()->width == 4 && nested.Document()->resolution == 144 &&
               nested.history.UndoName() == L"Outer",
           "nested-redo-restores-sized-state");

    DocumentSession noOp;
    const bool installed = InstallImageFixture(noOp);
    Expect(installed, "noop.fixture-installed");
    if (!installed || !noOp.Document())
        return;
    noOp.SetOpacity(0.5);
    noOp.Undo();
    const CanvasDocument unchanged = *noOp.Document();
    const Guid revision = noOp.history.CurrentRevision();
    const int undoCount = noOp.history.UndoCount();
    const std::wstring redoName = noOp.history.RedoName();
    CanvasSizeOptions same;
    same.width = unchanged.width;
    same.height = unchanged.height;
    CanvasDocument sameResult;
    const bool unchangedStatus = ResizeCanvas(unchanged, same, sameResult) == ResizeStatus::Unchanged;
    auto noOpPrepared = unchangedStatus ? Prepare(noOp, std::move(sameResult), L"Canvas Size") : std::nullopt;
    const bool appliedNoOp = noOpPrepared && noOp.ApplyPreparedDocumentSizeIfCurrent(*noOpPrepared);
    Expect(unchangedStatus && appliedNoOp && *noOp.Document() == unchanged &&
               noOp.history.CurrentRevision() == revision && noOp.history.UndoCount() == undoCount &&
               noOp.history.CanRedo() && noOp.history.RedoName() == redoName,
           "noop-size-preserves-revision-and-seeded-redo");
    noOp.Redo();
    Expect(noOp.history.UndoName() == L"Layer Opacity", "noop-preserved-redo-still-restores-edit");
}

void ActiveLayerChangeRefusesPreparedHistory()
{
    DocumentSession session;
    if (!InstallImageFixture(session) || !session.ActiveLayerID())
    {
        Expect(false, "active.fixture");
        return;
    }
    const Guid original = session.ActiveLayerID().value();
    session.AddBlankLayer();
    if (!session.ActiveLayerID())
    {
        Expect(false, "active.second-layer");
        return;
    }
    const Guid other = session.ActiveLayerID().value();
    session.SelectLayer(original);
    session.history.Reset();
    const auto source = session.Document();
    const auto revision = session.history.CurrentRevision();
    const auto token = session.CaptureJobToken();
    CanvasDocument result;
    auto prepared = PrepareImage(session, result);
    session.SelectLayer(other);
    Expect(prepared && token && session.CanApplyJobResult(token.value(), token->sequence),
           "active-selection-change-does-not-change-document-token");
    Expect(prepared && !session.ApplyPreparedDocumentSizeIfCurrent(prepared.value()) && session.Document() == source &&
               session.ActiveLayerID() == other && session.history.CurrentRevision() == revision &&
               session.history.UndoCount() == 0,
           "active-selection-change-refuses-stale-history-without-publication");
}

void CancellablePreparationLeavesNoPartialResult()
{
    DocumentSession session;
    const bool installed = InstallImageFixture(session);
    Expect(installed, "cancel.fixture-installed");
    if (!installed || !session.Document())
        return;
    const CanvasDocument source = *session.Document();
    CancelSource cancelled;
    static_cast<void>(cancelled.Cancel());
    CanvasDocument canvasResult;
    CanvasSizeOptions canvas;
    canvas.width = 4;
    canvas.height = 3;
    canvas.fill = CanvasExtensionColor{1.0, 0.5, 0.0};
    const ResizeStatus canvasStatus = ResizeCanvas(source, canvas, canvasResult, cancelled.Token());
    CanvasDocument imageResult;
    ImageSizeOptions image;
    image.width = 4;
    image.height = 2;
    image.resolution = 300;
    const ResizeStatus imageStatus = ResizeImage(source, image, imageResult, cancelled.Token());
    Expect(canvasStatus == ResizeStatus::Cancelled && canvasResult.layers.empty() &&
               imageStatus == ResizeStatus::Cancelled && imageResult.layers.empty() && *session.Document() == source &&
               session.history.UndoCount() == 0,
           "cancelled-resize-publishes-no-partial-document-or-history");
}

struct CancelAtHistoryStage final
{
    DocumentHistory::PrepareStage stage;
    CancelSource cancellation;
    unsigned int hits = 0;
    static void Checkpoint(void* context, DocumentHistory::PrepareStage current)
    {
        auto& state = *static_cast<CancelAtHistoryStage*>(context);
        if (state.stage == current)
        {
            ++state.hits;
            (void)state.cancellation.Cancel();
        }
    }
};

void HistoryCancellationPreservesSourceAndRedo()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : stages)
    {
        DocumentSession session;
        if (!InstallImageFixture(session))
        {
            Expect(false, "history-cancel.fixture");
            return;
        }
        session.SetOpacity(0.5);
        session.Undo();
        const auto source = session.Document();
        const auto revision = session.history.CurrentRevision();
        const auto token = session.CaptureJobToken();
        ImageSizeOptions options;
        options.width = 4;
        options.height = 2;
        CanvasDocument result;
        if (!token || ResizeImage(source.value(), options, result) != ResizeStatus::Ok)
        {
            Expect(false, "history-cancel.resize");
            return;
        }
        CancelAtHistoryStage state{stage};
        const DocumentHistory::PrepareObserver observer{CancelAtHistoryStage::Checkpoint, &state};
        auto prepared = PrepareDocumentSizePublication(source, session.ActiveLayerID(), token.value(),
                                                       session.history.CaptureState(), std::move(result), L"Image Size",
                                                       state.cancellation.Token(), observer);
        Expect(!prepared && state.hits == 1 && session.Document() == source &&
                   session.history.CurrentRevision() == revision && session.history.UndoCount() == 0 &&
                   session.history.RedoName() == L"Layer Opacity" && session.CanApplyJobResult(token.value(), 0),
               "history-cancel.preserves-source-revision-redo-and-token");
    }
}

void RetirementIncludesAndTransfersOutlineDraft()
{
    DocumentSession session;
    if (!InstallImageFixture(session))
    {
        Expect(false, "draft-retirement.fixture");
        return;
    }
    const auto withoutDraft = session.DocumentSizeRetirementMemoryBytes();
    session.BeginLasso(Vec2{0, 0}, SelectionMode::New);
    for (int point = 1; point < 4096; ++point)
        session.ExtendLasso(Vec2{static_cast<double>(point), 0});
    if (!session.PendingLasso())
    {
        Expect(false, "draft-retirement.outline");
        return;
    }
    const auto retainedPoints = session.PendingLasso()->points.capacity() * sizeof(Vec2);
    const auto withDraft = session.DocumentSizeRetirementMemoryBytes();
    Expect(withDraft >= withoutDraft + retainedPoints, "draft-retirement.capacity-is-in-the-bound");
    CanvasDocument result;
    auto prepared = PrepareImage(session, result);
    Expect(prepared && session.PendingLasso() && session.ApplyPreparedDocumentSizeIfCurrent(prepared.value()) &&
               !session.PendingLasso(),
           "draft-retirement.apply-transfers-outline-owner");
}
} // namespace

int RunDocumentSizeHistoryTests()
{
    gFailures = 0;
    CanvasLiteralApplyUndoRedoAndRetirement();
    ImageNearestAndResolutionOnlyUndoRedo();
    ObserverAllocationFailuresPreserveSeededRedo();
    StaleTokenHistoryEpochForeignMovedAndSingleUse();
    NestedTransactionAndNoOpRedo();
    ActiveLayerChangeRefusesPreparedHistory();
    CancellablePreparationLeavesNoPartialResult();
    HistoryCancellationPreservesSourceAndRedo();
    RetirementIncludesAndTransfersOutlineDraft();
    return gFailures;
}

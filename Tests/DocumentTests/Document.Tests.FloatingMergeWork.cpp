#include "Document.Tests.FloatingMergeWork.h"

#include "FloatingMergeWork.h"
#include "Selection.h"

#include <array>
#include <cstdio>
#include <new>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool value, const char* name)
{
    ++gAssertions;
    std::printf("%s floating-merge-work.%s\n", value ? "PASS" : "FAIL", name);
    gFailures += value ? 0 : 1;
}

struct Fixture final
{
    DocumentSession session;
    CanvasDocument before;
    Guid sourceID;
    std::optional<PixelClipboard> clipboard;
    std::uint64_t clipboardSerial = 0;
    int priorUndoCount = 0;
    std::shared_ptr<PreparedFloatingRestore> restore;
};

bool Open(Fixture& fixture, InlineExecutor& executor, bool mask = false, bool nested = false)
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 4;
    constexpr std::array<std::uint8_t, 8> sourcePixels{0, 0, 255, 255, 255, 0, 0, 255};
    auto source = ImageLayer::Blank(L"Source", {2, 1});
    source.asset = RasterAsset{
        Raster::Make(2, 1, RasterFormat::Bgra8888, sourcePixels.data(), sourcePixels.size()), {}, L"Source"};
    source.transform.sampling = LayerSampling::Nearest;
    if (mask)
    {
        constexpr std::array<std::uint8_t, 2> bytes{0, 127};
        source.mask =
            LayerMask{RasterAsset{Raster::Make(2, 1, RasterFormat::Gray8, bytes.data(), bytes.size()), {}, L"mask"},
                      true, std::nullopt, true};
    }
    fixture.sourceID = source.id;
    document.layers.push_back(source);
    document.selection = DocumentSelection{false, SelectionGeometry::Rect(0, 0, 1, 1)};
    fixture.session.ReplaceDocument(std::move(document), source.id);
    if (!fixture.session.CopySelection())
        return false;

    fixture.before = fixture.session.Document().value();
    fixture.clipboard = fixture.session.Clipboard();
    fixture.clipboardSerial = fixture.session.ClipboardSerial();
    fixture.priorUndoCount = fixture.session.history.UndoCount();
    if (nested)
        fixture.session.BeginEdit(L"Outer floating owner");
    auto input = fixture.session.PrepareFloatingOpeningInput();
    if (!input)
        return false;
    auto opening = DocumentSession::PrepareFloatingOpeningPublication(std::move(input.value()), executor, {}, {}, {});
    if (opening.status != FloatingPixelsStatus::Ready || !opening.publication || !opening.publication->Restoration() ||
        !fixture.session.ApplyPreparedFloatingOpeningIfCurrent(opening.publication.value()))
        return false;
    fixture.restore = opening.publication->Restoration();
    return static_cast<bool>(fixture.restore);
}

void Preview(Fixture& fixture, double x)
{
    auto draft = fixture.session.PendingTransformEdit()->draft;
    draft.origin = {x, 0};
    fixture.session.PreviewTransform(draft);
}

FloatingMergeResult Prepare(Fixture& fixture, InlineExecutor& executor, CancelToken cancellation = {},
                            DocumentHistory::PrepareObserver historyObserver = {},
                            FloatingMergeObserver mergeObserver = {})
{
    auto input = fixture.session.PrepareFloatingMergeInput(fixture.restore);
    if (!input)
        return {};
    return DocumentSession::PrepareFloatingMergePublication(std::move(input.value()), executor, cancellation,
                                                            historyObserver, mergeObserver);
}

bool ExactPixels(const Raster& image, const std::array<std::uint8_t, 8>& expected)
{
    if (image.Width() != 2 || image.Height() != 1)
        return false;
    std::array<std::uint8_t, 8> actual{};
    image.ReadRegion(0, 0, 2, 1, actual.data(), 8, 0);
    return actual == expected;
}

bool ClipboardUnchanged(const Fixture& fixture)
{
    return fixture.session.Clipboard() && fixture.clipboard &&
           fixture.session.Clipboard()->image == fixture.clipboard->image &&
           fixture.session.Clipboard()->origin == fixture.clipboard->origin &&
           fixture.session.ClipboardSerial() == fixture.clipboardSerial;
}

void PublishedPixelsAndHistory()
{
    Fixture fixture;
    InlineExecutor executor;
    Expect(Open(fixture, executor), "tiny-opening-fixture-ready");
    if (!fixture.restore)
        return;
    const auto opening = fixture.session.Document().value();
    const auto openingHistory = fixture.session.history.CaptureState();
    Preview(fixture, 1);
    Expect(fixture.session.FloatingMergePreparationMemoryBytes(*fixture.restore) > 0,
           "current-floating-draft-declares-whole-preparation");
    auto result = Prepare(fixture, executor);
    Expect(result.status == FloatingPixelsStatus::Ready && result.publication, "translated-red-merge-is-prepared");
    Expect(fixture.session.Document().value() == opening && fixture.session.history.IsCurrent(openingHistory) &&
               fixture.session.PendingTransformEdit().has_value(),
           "worker-cannot-publish-live-state");
    Expect(ClipboardUnchanged(fixture), "worker-keeps-private-clipboard");
    if (!result.publication)
        return;

    Expect(fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
           "sealed-merge-publishes-on-owner");
    Expect(!fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
           "sealed-merge-is-single-use");
    const auto after = fixture.session.Document().value();
    constexpr std::array<std::uint8_t, 8> movedRed{0, 0, 0, 0, 0, 0, 255, 255};
    Expect(after.layers.size() == 1 && after.layers.front().id == fixture.sourceID &&
               fixture.session.ActiveLayerID() == fixture.sourceID && fixture.session.SelectedLayerIDs().size() == 1 &&
               fixture.session.SelectedLayerIDs().contains(fixture.sourceID),
           "merge-keeps-only-original-source-active");
    Expect(ExactPixels(after.layers.front().asset->image, movedRed), "red-pixel-moves-over-blue-exactly");
    Expect(after.selection ==
               std::optional<DocumentSelection>{DocumentSelection{false, SelectionGeometry::Rect(1, 0, 1, 1)}},
           "outline-moves-by-one-document-pixel");
    Expect(ClipboardUnchanged(fixture), "publication-keeps-clipboard-bytes-and-authority");
    Expect(!fixture.session.PendingTransformEdit() && fixture.session.history.CaptureState().Depth() == 0 &&
               fixture.session.history.UndoCount() == fixture.priorUndoCount + 1 &&
               fixture.session.history.UndoName() == L"Transform Selection",
           "one-outer-transform-selection-history-entry");

    fixture.session.Undo();
    Expect(fixture.session.Document().value() == fixture.before &&
               fixture.session.ActiveLayerID() == fixture.sourceID && ClipboardUnchanged(fixture),
           "undo-restores-exact-opening-document-and-clipboard");
    fixture.session.Redo();
    Expect(fixture.session.Document().value() == after && fixture.session.ActiveLayerID() == fixture.sourceID &&
               ClipboardUnchanged(fixture),
           "redo-restores-exact-merged-document-and-clipboard");
}

void RefusalResolvesToOpening()
{
    InlineExecutor executor;
    for (const auto kind : {0, 1, 2})
    {
        Fixture fixture;
        const bool opened = Open(fixture, executor);
        Expect(opened, "resolution-fixture-ready");
        if (!opened)
            continue;
        if (kind == 0)
            Preview(fixture, 0);
        else if (kind == 1)
            Preview(fixture, 1);
        else
            Preview(fixture, 1'000'000);
        const auto opening = fixture.session.Document().value();
        const auto history = fixture.session.history.CaptureState();
        auto result = Prepare(fixture, executor);
        const auto expectedStatus = kind == 0   ? FloatingPixelsStatus::Unchanged
                                    : kind == 1 ? FloatingPixelsStatus::Ready
                                                : FloatingPixelsStatus::TooLarge;
        Expect(result.status == expectedStatus && result.publication, kind == 0   ? "unchanged-returns-resolution"
                                                                      : kind == 1 ? "successful-control-ready"
                                                                                  : "oversized-returns-resolution");
        if (!result.publication)
            continue;
        if (kind == 1)
            continue;
        Expect(fixture.session.Document().value() == opening && fixture.session.history.IsCurrent(history),
               "refused-worker-leaves-opening-sealed");
        Expect(fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
               "refusal-atomically-restores-opening");
        Expect(fixture.session.Document().value() == fixture.before && !fixture.session.PendingTransformEdit() &&
                   fixture.session.history.CaptureState().Depth() == 0 &&
                   fixture.session.history.UndoCount() == fixture.priorUndoCount && ClipboardUnchanged(fixture),
               "resolution-restores-source-selection-outline-and-clipboard-without-entry");
        Expect(!fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
               "resolution-cannot-apply-twice");
    }

    Fixture cancelled;
    const bool opened = Open(cancelled, executor);
    Expect(opened, "cancel-resolution-fixture-ready");
    if (opened)
    {
        Preview(cancelled, 1);
        CancelSource cancel;
        struct Stop final
        {
            CancelSource* cancel;
            bool reached = false;
        } stop{&cancel};
        const auto checkpoint = [](void* raw, FloatingMergeStage stage)
        {
            if (stage == FloatingMergeStage::Pixels)
            {
                auto& state = *static_cast<Stop*>(raw);
                state.reached = true;
                (void)state.cancel->Cancel();
            }
        };
        auto result = Prepare(cancelled, executor, cancel.Token(), {}, {&stop, checkpoint});
        Expect(stop.reached && result.status == FloatingPixelsStatus::Cancelled && result.publication,
               "cancelled-pixels-return-resolution-capsule");
        if (result.publication)
        {
            Expect(cancelled.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()) &&
                       cancelled.session.Document().value() == cancelled.before &&
                       cancelled.session.history.UndoCount() == cancelled.priorUndoCount,
                   "cancel-resolution-restores-without-history");
            Expect(!cancelled.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
                   "cancel-resolution-is-single-use");
        }
    }
}

struct MergeStop final
{
    FloatingMergeStage mergeStage = FloatingMergeStage::Pixels;
    DocumentHistory::PrepareStage historyStage = DocumentHistory::PrepareStage::AfterSnapshot;
    CancelSource* cancel = nullptr;
    bool history = false;
    bool throwing = false;
    bool reached = false;
};

void StopMerge(void* raw, FloatingMergeStage stage)
{
    auto& state = *static_cast<MergeStop*>(raw);
    if (!state.history && stage == state.mergeStage)
    {
        state.reached = true;
        if (state.throwing)
            throw std::bad_alloc{};
        (void)state.cancel->Cancel();
    }
}

void StopHistory(void* raw, DocumentHistory::PrepareStage stage)
{
    auto& state = *static_cast<MergeStop*>(raw);
    if (state.history && stage == state.historyStage)
    {
        state.reached = true;
        if (state.throwing)
            throw std::bad_alloc{};
        (void)state.cancel->Cancel();
    }
}

void WorkerFailureKeepsEscapeReady()
{
    InlineExecutor executor;
    for (const auto stage : {FloatingMergeStage::Pixels, FloatingMergeStage::History, FloatingMergeStage::Publication})
        for (const bool throwing : {false, true})
        {
            Fixture fixture;
            const bool opened = Open(fixture, executor);
            Expect(opened, "worker-checkpoint-fixture-ready");
            if (!opened)
                continue;
            Preview(fixture, 1);
            const auto opening = fixture.session.Document().value();
            CancelSource cancel;
            MergeStop stop{stage, {}, &cancel, false, throwing};
            bool failedAsRequested = false;
            std::optional<FloatingMergeResult> result;
            try
            {
                result.emplace(Prepare(fixture, executor, cancel.Token(), {}, {&stop, StopMerge}));
            }
            catch (const std::bad_alloc&)
            {
                failedAsRequested = throwing;
            }
            if (!throwing)
                failedAsRequested = result && result->status == FloatingPixelsStatus::Cancelled && result->publication;
            Expect(stop.reached && failedAsRequested,
                   throwing ? "merge-allocation-fault-propagates" : "merge-cancellation-seals-resolution");
            Expect(fixture.session.Document().value() == opening && fixture.session.PendingTransformEdit() &&
                       fixture.session.history.CaptureState().Depth() == 1,
                   "worker-failure-retains-live-opening");
            Expect(fixture.session.ApplyPreparedFloatingRestoreIfCurrent(*fixture.restore) &&
                       fixture.session.Document().value() == fixture.before &&
                       fixture.session.history.UndoCount() == fixture.priorUndoCount,
                   "prebuilt-escape-restoration-survives-worker-failure");
        }

    constexpr std::array historyStages{DocumentHistory::PrepareStage::AfterSnapshot,
                                       DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                                       DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : historyStages)
        for (const bool throwing : {false, true})
        {
            Fixture fixture;
            const bool opened = Open(fixture, executor);
            Expect(opened, "history-checkpoint-fixture-ready");
            if (!opened)
                continue;
            Preview(fixture, 1);
            const auto opening = fixture.session.Document().value();
            const auto history = fixture.session.history.CaptureState();
            CancelSource cancel;
            MergeStop stop{{}, stage, &cancel, true, throwing};
            bool failedAsRequested = false;
            std::optional<FloatingMergeResult> result;
            try
            {
                result.emplace(Prepare(fixture, executor, cancel.Token(), {StopHistory, &stop}));
            }
            catch (const std::bad_alloc&)
            {
                failedAsRequested = throwing;
            }
            if (!throwing)
                failedAsRequested = result && result->status == FloatingPixelsStatus::Cancelled && result->publication;
            Expect(stop.reached && failedAsRequested,
                   throwing ? "history-allocation-fault-propagates" : "history-cancellation-seals-resolution");
            Expect(fixture.session.Document().value() == opening && fixture.session.history.IsCurrent(history) &&
                       fixture.session.PendingTransformEdit(),
                   "history-failure-keeps-opening-and-unpublished-history");
            Expect(fixture.session.ApplyPreparedFloatingRestoreIfCurrent(*fixture.restore) &&
                       fixture.session.Document().value() == fixture.before,
                   "history-failure-leaves-prebuilt-restoration-viable");
        }
}

void StaleCapsulesProtectNewerState()
{
    InlineExecutor executor;
    for (const bool ready : {false, true})
        for (int mutation = 0; mutation < 4; ++mutation)
        {
            Fixture fixture;
            const bool opened = Open(fixture, executor);
            Expect(opened, "stale-capsule-fixture-ready");
            if (!opened)
                continue;
            Preview(fixture, 1);
            CancelSource cancel;
            struct Refuse final
            {
                CancelSource* cancel;
            } refuse{&cancel};
            const auto refuseAtPixels = [](void* raw, FloatingMergeStage stage)
            {
                if (stage == FloatingMergeStage::Pixels)
                    (void)static_cast<Refuse*>(raw)->cancel->Cancel();
            };
            auto result = ready ? Prepare(fixture, executor)
                                : Prepare(fixture, executor, cancel.Token(), {}, {&refuse, refuseAtPixels});
            Expect(result.publication.has_value() && (ready ? result.status == FloatingPixelsStatus::Ready
                                                            : result.status == FloatingPixelsStatus::Cancelled),
                   "stale-capsule-captured");
            if (!result.publication)
                continue;

            if (mutation == 0)
            {
                Preview(fixture, 2);
            }
            else if (mutation == 1)
            {
                fixture.session.EditForTest([](CanvasDocument& document) { document.layers.front().opacity = 0.5; });
            }
            else if (mutation == 2)
            {
                fixture.session.history.Reset();
            }
            else
            {
                auto sameIdentity = fixture.session.Document().value();
                fixture.session.ReplaceDocument(std::move(sameIdentity), fixture.session.ActiveLayerID());
            }
            const auto newerDocument = fixture.session.Document().value();
            const auto newerDraft = fixture.session.PendingTransformEdit();
            Expect(!fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
                   "stale-ready-or-refusal-publication-refuses");
            Expect(fixture.session.Document().value() == newerDocument &&
                       fixture.session.PendingTransformEdit().has_value() == newerDraft.has_value() &&
                       (!newerDraft || (fixture.session.PendingTransformEdit()->layerID == newerDraft->layerID &&
                                        fixture.session.PendingTransformEdit()->draft == newerDraft->draft)),
                   "stale-capsule-never-discards-newer-document-or-draft");
        }

    Fixture geometryOnly;
    const bool opened = Open(geometryOnly, executor);
    Expect(opened, "geometry-restore-fixture-ready");
    if (opened)
    {
        auto newer = geometryOnly.session.PendingTransformEdit()->draft;
        newer.origin = {3, 0};
        geometryOnly.session.PreviewTransform(newer);
        Expect(geometryOnly.session.ApplyPreparedFloatingRestoreIfCurrent(*geometryOnly.restore) &&
                   geometryOnly.session.Document().value() == geometryOnly.before &&
                   !geometryOnly.session.PendingTransformEdit(),
               "explicit-escape-still-restores-after-geometry-preview");
    }
}

void MovedInputLosesAuthority()
{
    Fixture fixture;
    InlineExecutor executor;
    const bool opened = Open(fixture, executor);
    Expect(opened, "move-input-fixture-ready");
    if (!opened)
        return;
    Preview(fixture, 1);
    auto input = fixture.session.PrepareFloatingMergeInput(fixture.restore);
    Expect(input.has_value(), "merge-input-captured");
    if (!input)
        return;
    auto owner = std::move(input.value());
    auto movedFrom = DocumentSession::PrepareFloatingMergePublication(std::move(input.value()), executor, {}, {}, {});
    auto movedTo = DocumentSession::PrepareFloatingMergePublication(std::move(owner), executor, {}, {}, {});
    Expect(movedFrom.status == FloatingPixelsStatus::Invalid && !movedFrom.publication &&
               movedTo.status == FloatingPixelsStatus::Ready && movedTo.publication,
           "moved-input-cannot-be-reused-owner-remains-valid");
}

void MaskGrowsWithSealedPublication()
{
    Fixture fixture;
    InlineExecutor executor;
    const auto opened = Open(fixture, executor, true);
    Expect(opened, "carried-mask-fixture-ready");
    if (!opened)
        return;
    Preview(fixture, -1);
    auto result = Prepare(fixture, executor);
    Expect(result.status == FloatingPixelsStatus::Ready && result.publication,
           "carried-mask-complete-publication-ready");
    if (!result.publication)
        return;
    Expect(fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()),
           "carried-mask-publishes-with-merged-pixels");
    const auto* layer = fixture.session.ActiveLayer();
    std::array<std::uint8_t, 3> mask{};
    if (layer && layer->mask)
        layer->mask->asset.image.ReadRegion(0, 0, 3, 1, mask.data(), 3, 0);
    Expect(layer && layer->mask && layer->mask->asset.image.Width() == 3 && layer->mask->asset.image.Height() == 1 &&
               mask == std::array<std::uint8_t, 3>{255, 0, 127} && layer->mask->asset.thumbnail.HasStorage() &&
               !layer->mask->placement && layer->transform.origin == Vec2{-1, 0},
           "carried-mask-keeps-old-values-and-reveals-new-area-with-eager-thumbnail");
    Expect(fixture.session.history.UndoCount() == fixture.priorUndoCount + 1 &&
               fixture.session.history.CaptureState().Depth() == 0,
           "carried-mask-is-part-of-one-outer-entry");
    fixture.session.Undo();
    Expect(fixture.session.Document().value() == fixture.before,
           "carried-mask-undo-restores-exact-image-mask-and-outline");
}
void NestedMergeClosesOnlyChild()
{
    Fixture fixture;
    InlineExecutor executor;
    const auto opened = Open(fixture, executor, false, true);
    Expect(opened, "nested-merge-fixture-ready");
    if (!opened)
        return;
    const auto revision = fixture.session.history.CurrentRevision();
    Preview(fixture, 1);
    auto result = Prepare(fixture, executor);
    Expect(result.status == FloatingPixelsStatus::Ready && result.publication, "nested-merge-prepares-at-depth-two");
    if (!result.publication)
        return;
    Expect(fixture.session.ApplyPreparedFloatingMergeIfCurrent(result.publication.value()) &&
               fixture.session.history.CaptureState().Depth() == 1 &&
               fixture.session.history.UndoCount() == fixture.priorUndoCount &&
               fixture.session.history.CurrentRevision() == revision && !fixture.session.PendingTransformEdit(),
           "nested-merge-publishes-pixels-with-parent-history-still-open");
    fixture.session.EndEdit();
    Expect(fixture.session.history.CaptureState().Depth() == 0 &&
               fixture.session.history.UndoCount() == fixture.priorUndoCount + 1 &&
               fixture.session.history.UndoName() == L"Outer floating owner",
           "nested-merge-parent-close-keeps-original-parent-name");
    fixture.session.Undo();
    Expect(fixture.session.Document().value() == fixture.before,
           "nested-merge-parent-undo-restores-exact-original-source");
}
} // namespace

int RunFloatingMergeWorkTests()
{
    gFailures = gAssertions = 0;
    PublishedPixelsAndHistory();
    RefusalResolvesToOpening();
    WorkerFailureKeepsEscapeReady();
    StaleCapsulesProtectNewerState();
    MovedInputLosesAuthority();
    MaskGrowsWithSealedPublication();
    NestedMergeClosesOnlyChild();
    std::printf("FLOATING MERGE WORK TESTS checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

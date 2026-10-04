#include "ImportHistoryTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>
#include <optional>
#include <utility>

namespace internal
{
namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s import-history.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

CanvasDocument PixelDocument(const wchar_t* name, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 1;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    layer.asset = RasterAsset{image, image, name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

CanvasDocument WithSecondImage(const CanvasDocument& baseline, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture result = DocumentFixture::Copy(baseline);
    ImageLayer layer = ImageLayer::Blank(L"Imported image", Vec2{1, 1});
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    layer.asset = RasterAsset{image, image, layer.name};
    result.layers.push_back(std::move(layer));
    return std::move(result).Seal();
}

CanvasDocument WithPixel(const CanvasDocument& source, std::array<std::uint8_t, 4> bgra)
{
    DocumentFixture result = DocumentFixture::Copy(source);
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    result.layers.front().asset.value().image = image;
    result.layers.front().asset.value().thumbnail = image;
    return std::move(result).Seal();
}

bool HasPixel(const CanvasDocument& document, std::size_t layerIndex, std::array<std::uint8_t, 4> expected) noexcept
{
    if (layerIndex >= document.layers.size() || !document.layers[layerIndex].asset)
        return false;
    const Raster& image = document.layers[layerIndex].asset.value().image;
    const std::uint8_t* pixels = image.Data();
    return image.Width() == 1 && image.Height() == 1 && pixels != nullptr &&
           std::equal(expected.begin(), expected.end(), pixels);
}

struct OldHistory final
{
    DocumentHistory history;
    CanvasDocument first = PixelDocument(L"Old first", {0x11, 0x22, 0x33, 0xff});
    CanvasDocument second = WithPixel(first, {0x44, 0x55, 0x66, 0xff});
    CanvasDocument third = WithPixel(second, {0x77, 0x88, 0x99, 0xff});
    std::optional<Guid> active{first.layers.front().id};
    std::optional<CanvasDocument> live{first};

    void SeedBothStacks()
    {
        history.Begin(L"Old edit one", live, active);
        live = second;
        history.End(live, active);
        history.Begin(L"Old edit two", live, active);
        live = third;
        history.End(live, active);
        const auto undone = history.Undo();
        if (undone.has_value())
        {
            live = undone.value().document;
            active = undone.value().activeLayerID;
        }
        history.MarkSaved();
    }
};

enum class FailureMode
{
    BadAlloc,
    Cancellation,
};

struct CheckpointFailure final
{
    DocumentHistory::PrepareStage target{};
    FailureMode mode{};
    CancelSource* cancellation = nullptr;
    int targetHits = 0;
};

void FailAtCheckpoint(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& failure = *static_cast<CheckpointFailure*>(opaque);
    if (stage != failure.target)
        return;
    ++failure.targetHits;
    if (failure.mode == FailureMode::BadAlloc)
        throw std::bad_alloc{};
    static_cast<void>(failure.cancellation->Cancel());
}

const char* StageName(DocumentHistory::PrepareStage stage) noexcept
{
    using Stage = DocumentHistory::PrepareStage;
    switch (stage)
    {
    case Stage::BeforeSnapshot:
        return "before-snapshot";
    case Stage::AfterSnapshot:
        return "after-snapshot";
    case Stage::Entry:
        return "entry";
    case Stage::StackGrowth:
        return "stack-growth";
    case Stage::TrimIndex:
        return "trim-index";
    case Stage::ReplacementReset:
        return "replacement-reset";
    case Stage::PendingSnapshot:
        return "pending-snapshot";
    case Stage::PendingRecord:
        return "pending-record";
    case Stage::RestoreSnapshot:
        return "restore-snapshot";
    }
    return "invalid-stage";
}

const char* FailureName(FailureMode mode) noexcept
{
    return mode == FailureMode::BadAlloc ? "bad-alloc" : "cancellation";
}

void RunFailureCase(DocumentHistory::PrepareStage stage, FailureMode mode)
{
    OldHistory fixture;
    fixture.SeedBothStacks();
    const DocumentHistory::StateView stateBefore = fixture.history.CaptureState();
    const Guid revisionBefore = fixture.history.CurrentRevision();
    const bool modifiedBefore = fixture.history.IsModified();
    const std::optional<CanvasDocument> liveBefore = fixture.live;
    const std::size_t undoCountBefore = stateBefore.PastCount();
    const std::size_t redoCountBefore = stateBefore.FutureCount();
    const std::wstring undoNameBefore = stateBefore.UndoName();
    const std::wstring redoNameBefore = stateBefore.RedoName();

    const CanvasDocument baseline = PixelDocument(L"Replacement baseline", {0x12, 0x34, 0x56, 0xff});
    const CanvasDocument after = WithSecondImage(baseline, {0x78, 0x9a, 0xbc, 0xff});
    CancelSource cancellation;
    CheckpointFailure failure{stage, mode, &cancellation};
    const DocumentHistory::PrepareObserver observer{&FailAtCheckpoint, &failure};
    std::optional<DocumentHistory::ReplacementPublication> publication;
    bool caughtBadAlloc = false;
    try
    {
        publication = DocumentHistory::PrepareReplacement(fixture.history.CaptureState(), baseline,
                                                          baseline.layers.front().id, after, after.layers.back().id,
                                                          L"Import Image", cancellation.Token(), observer);
    }
    catch (const std::bad_alloc&)
    {
        caughtBadAlloc = true;
    }

    const auto stateAfter = fixture.history.CaptureState();
    const bool refusedAsRequested = mode == FailureMode::BadAlloc ? caughtBadAlloc && !publication.has_value()
                                                                  : !caughtBadAlloc && !publication.has_value() &&
                                                                        cancellation.Token().IsCancelled();
    const bool retainedHistory =
        fixture.history.CurrentRevision() == revisionBefore && fixture.history.IsModified() == modifiedBefore &&
        fixture.history.UndoCount() == static_cast<int>(undoCountBefore) && fixture.history.CanUndo() &&
        fixture.history.CanRedo() && fixture.history.UndoName() == undoNameBefore &&
        fixture.history.RedoName() == redoNameBefore && stateAfter.Revision() == stateBefore.Revision() &&
        stateAfter.Depth() == stateBefore.Depth() && stateAfter.PastCount() == undoCountBefore &&
        stateAfter.FutureCount() == redoCountBefore && stateAfter.UndoName() == undoNameBefore &&
        stateAfter.RedoName() == redoNameBefore;
    const bool retainedLive = fixture.live == liveBefore && fixture.live.has_value() &&
                              HasPixel(fixture.live.value(), 0, {0x44, 0x55, 0x66, 0xff});
    const char* modeName = FailureName(mode);
    std::printf("%s import-history.failure.%s.%s-requested\n", refusedAsRequested ? "PASS" : "FAIL", StageName(stage),
                modeName);
    if (!refusedAsRequested)
        ++gFailures;
    std::printf("%s import-history.failure.%s.%s-live-state-retained\n", retainedHistory ? "PASS" : "FAIL",
                StageName(stage), modeName);
    if (!retainedHistory)
        ++gFailures;
    std::printf("%s import-history.failure.%s.%s-source-retained\n", retainedLive ? "PASS" : "FAIL", StageName(stage),
                modeName);
    if (!retainedLive)
        ++gFailures;
    const bool stageReachedOnce = failure.targetHits == 1;
    std::printf("%s import-history.failure.%s.%s-stage-reached-once\n", stageReachedOnce ? "PASS" : "FAIL",
                StageName(stage), modeName);
    if (!stageReachedOnce)
        ++gFailures;
}

template <std::size_t N> void RunFailureStage(const std::array<DocumentHistory::PrepareStage, N>& stages)
{
    for (const auto stage : stages)
    {
        RunFailureCase(stage, FailureMode::BadAlloc);
        RunFailureCase(stage, FailureMode::Cancellation);
    }
}
} // namespace

int RunImportHistoryTests()
{
    gFailures = 0;

    {
        OldHistory old;
        old.SeedBothStacks();
        const CanvasDocument baseline = PixelDocument(L"Single image", {0x21, 0x43, 0x65, 0xff});
        auto publication =
            DocumentHistory::PrepareReplacement(old.history.CaptureState(), baseline, baseline.layers.front().id,
                                                baseline, baseline.layers.front().id, L"Import Image");
        Guid baselineRevision{};
        bool replaced = false;
        if (publication.has_value())
        {
            baselineRevision = publication.value().BaselineRevision();
            replaced = old.history.CanPublishReplacement(publication.value()) &&
                       old.history.PublishReplacement(publication.value());
        }
        Expect(replaced && baselineRevision != Guid{} && old.history.CurrentRevision() == baselineRevision &&
                   !old.history.IsModified() && !old.history.CanUndo() && !old.history.CanRedo() &&
                   old.history.UndoCount() == 0 && old.history.CaptureState().PastCount() == 0 &&
                   old.history.CaptureState().FutureCount() == 0 && old.history.CaptureState().Depth() == 0 &&
                   HasPixel(baseline, 0, {0x21, 0x43, 0x65, 0xff}),
               "single-image-replacement-resets-old-undo-redo-to-clean-baseline");
    }

    {
        DocumentHistory history;
        const CanvasDocument baseline = PixelDocument(L"Baseline", {0x10, 0x20, 0x30, 0xff});
        const CanvasDocument after = WithSecondImage(baseline, {0x40, 0x50, 0x60, 0xff});
        const Guid baselineLayer = baseline.layers.front().id;
        const Guid importedLayer = after.layers.back().id;
        auto publication = DocumentHistory::PrepareReplacement(history.CaptureState(), baseline, baselineLayer, after,
                                                               importedLayer, L"Import Image");
        Guid baselineRevision{};
        bool published = false;
        if (publication.has_value())
        {
            baselineRevision = publication.value().BaselineRevision();
            published =
                history.CanPublishReplacement(publication.value()) && history.PublishReplacement(publication.value());
        }
        const Guid finalRevision = history.CurrentRevision();
        Expect(published && baselineRevision != Guid{} && finalRevision != baselineRevision && history.IsModified() &&
                   history.UndoCount() == 1 && history.CanUndo() && !history.CanRedo() &&
                   history.UndoName() == L"Import Image" && history.CaptureState().Depth() == 0,
               "baseline-plus-image-publishes-one-named-dirty-entry");

        auto undo = DocumentHistory::PrepareRestore(history.CaptureState(), false);
        const bool undoPublished = undo.has_value() && history.Publish(undo.value());
        const auto restoredBaseline = undo.has_value() && undoPublished ? undo.value().TakeRestoreSnapshot()
                                                                        : std::optional<DocumentHistory::Snapshot>{};
        Expect(undoPublished && restoredBaseline.has_value() && restoredBaseline.value().document.has_value() &&
                   restoredBaseline.value().activeLayerID == baselineLayer &&
                   restoredBaseline.value().revision == baselineRevision &&
                   history.CurrentRevision() == baselineRevision && !history.IsModified() && !history.CanUndo() &&
                   history.CanRedo() &&
                   HasPixel(restoredBaseline.value().document.value(), 0, {0x10, 0x20, 0x30, 0xff}) &&
                   restoredBaseline.value().document.value().layers.size() == 1,
               "undo-restores-literal-baseline-pixels-active-layer-and-clean-revision");

        auto redo = DocumentHistory::PrepareRestore(history.CaptureState(), true);
        const bool redoPublished = redo.has_value() && history.Publish(redo.value());
        const auto restoredFinal = redo.has_value() && redoPublished ? redo.value().TakeRestoreSnapshot()
                                                                     : std::optional<DocumentHistory::Snapshot>{};
        Expect(redoPublished && restoredFinal.has_value() && restoredFinal.value().document.has_value() &&
                   restoredFinal.value().activeLayerID == importedLayer &&
                   restoredFinal.value().revision == finalRevision && history.CurrentRevision() == finalRevision &&
                   history.IsModified() && history.CanUndo() && !history.CanRedo() &&
                   restoredFinal.value().document.value().layers.size() == 2 &&
                   HasPixel(restoredFinal.value().document.value(), 0, {0x10, 0x20, 0x30, 0xff}) &&
                   HasPixel(restoredFinal.value().document.value(), 1, {0x40, 0x50, 0x60, 0xff}),
               "redo-restores-final-literal-pixels-active-layer-and-dirty-revision");
    }

    {
        OldHistory old;
        old.SeedBothStacks();
        const CanvasDocument baseline = PixelDocument(L"Stale baseline", {0x13, 0x24, 0x35, 0xff});
        const CanvasDocument after = WithSecondImage(baseline, {0x46, 0x57, 0x68, 0xff});
        auto publication =
            DocumentHistory::PrepareReplacement(old.history.CaptureState(), baseline, baseline.layers.front().id, after,
                                                after.layers.back().id, L"Import Image");
        const Guid revision = old.history.CurrentRevision();
        const auto stateBefore = old.history.CaptureState();
        old.history.Begin(L"No-op epoch change", old.live, old.active);
        old.history.End(old.live, old.active);
        const auto stateAfterEpochChange = old.history.CaptureState();
        const bool exactEpochOnly = stateAfterEpochChange.Revision() == stateBefore.Revision() &&
                                    old.history.CurrentRevision() == revision &&
                                    stateAfterEpochChange.PastCount() == stateBefore.PastCount() &&
                                    stateAfterEpochChange.FutureCount() == stateBefore.FutureCount() &&
                                    stateAfterEpochChange.Depth() == stateBefore.Depth() && !old.history.IsModified();
        const bool refused = publication.has_value() && !old.history.CanPublishReplacement(publication.value()) &&
                             !old.history.PublishReplacement(publication.value());
        const auto stateAfterRefusal = old.history.CaptureState();
        Expect(exactEpochOnly && refused && old.history.CurrentRevision() == revision && !old.history.IsModified() &&
                   stateAfterRefusal.PastCount() == stateBefore.PastCount() &&
                   stateAfterRefusal.FutureCount() == stateBefore.FutureCount() &&
                   stateAfterRefusal.Depth() == stateBefore.Depth() &&
                   stateAfterRefusal.UndoName() == stateBefore.UndoName() &&
                   stateAfterRefusal.RedoName() == stateBefore.RedoName() && old.history.CanUndo() &&
                   old.history.CanRedo(),
               "epoch-only-stale-replacement-refusal-retains-both-stacks-revision-and-saved-state");
    }

    {
        DocumentHistory owner;
        DocumentHistory foreign;
        const CanvasDocument baseline = PixelDocument(L"Ownership baseline", {0x18, 0x29, 0x3a, 0xff});
        const CanvasDocument after = WithSecondImage(baseline, {0x4b, 0x5c, 0x6d, 0xff});
        auto publication = DocumentHistory::PrepareReplacement(
            owner.CaptureState(), baseline, baseline.layers.front().id, after, after.layers.back().id, L"Import Image");
        bool foreignRefused = false;
        bool movedFromRefused = false;
        bool movedPublishedOnce = false;
        if (publication.has_value())
        {
            foreignRefused =
                !foreign.CanPublishReplacement(publication.value()) && !foreign.PublishReplacement(publication.value());
            auto moved = std::move(publication.value());
            movedFromRefused =
                !owner.CanPublishReplacement(publication.value()) && !owner.PublishReplacement(publication.value());
            movedPublishedOnce = owner.CanPublishReplacement(moved) && owner.PublishReplacement(moved) &&
                                 !owner.CanPublishReplacement(moved) && !owner.PublishReplacement(moved) &&
                                 owner.UndoCount() == 1;
        }
        Expect(foreignRefused && movedFromRefused && movedPublishedOnce,
               "foreign-moved-from-and-single-use-replacement-publications");
    }

    {
        DocumentHistory history;
        const CanvasDocument baseline = PixelDocument(L"Pending baseline", {0x1a, 0x2b, 0x3c, 0xff});
        const CanvasDocument after = WithSecondImage(baseline, {0x4d, 0x5e, 0x6f, 0xff});
        const CanvasDocument oldDocument = PixelDocument(L"Pending old document", {0x70, 0x80, 0x90, 0xff});
        const std::optional<CanvasDocument> oldLive{oldDocument};
        const std::optional<Guid> oldActive{oldDocument.layers.front().id};
        history.Begin(L"Old pending transaction", oldLive, oldActive);
        auto publication =
            DocumentHistory::PrepareReplacement(history.CaptureState(), baseline, baseline.layers.front().id, after,
                                                after.layers.back().id, L"Import Image");
        const bool published = publication.has_value() && history.CanPublishReplacement(publication.value()) &&
                               history.PublishReplacement(publication.value());
        const auto state = history.CaptureState();
        Expect(published && state.Depth() == 0 && state.PastCount() == 1 && state.FutureCount() == 0 &&
                   history.UndoCount() == 1 && history.UndoName() == L"Import Image" &&
                   history.CurrentRevision() != Guid{} && history.IsModified() && history.CanUndo() &&
                   !history.CanRedo(),
               "replacement-clears-pending-old-transaction-and-starts-import-entry-at-depth-zero");
    }

    using Stage = DocumentHistory::PrepareStage;
    RunFailureStage(std::array{Stage::ReplacementReset, Stage::BeforeSnapshot, Stage::AfterSnapshot, Stage::Entry,
                               Stage::StackGrowth, Stage::TrimIndex});
    return gFailures;
}
} // namespace internal

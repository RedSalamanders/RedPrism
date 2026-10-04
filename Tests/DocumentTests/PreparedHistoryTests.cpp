#include "PreparedHistoryTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"

#include <array>
#include <cstdio>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-publication.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

std::optional<DocumentHistory::Publication> PrepareWidthEdit(DocumentHistory& history, const CanvasDocument& before,
                                                             const CanvasDocument& after,
                                                             const std::optional<Guid>& activeLayer,
                                                             const wchar_t* name = L"Resize")
{
    return DocumentHistory::PrepareEdit(history.CaptureState(), before, activeLayer, after, activeLayer, name);
}

bool PublishWidthEdit(DocumentHistory& history, const CanvasDocument& before, const CanvasDocument& after,
                      const std::optional<Guid>& activeLayer, const wchar_t* name = L"Resize")
{
    auto publication = PrepareWidthEdit(history, before, after, activeLayer, name);
    return publication.has_value() && history.CanPublish(publication.value()) && history.Publish(publication.value());
}
} // namespace

int RunPreparedHistoryTests()
{
    gFailures = 0;

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer = Guid::Generate();
        const Guid initialRevision = history.CurrentRevision();
        auto publication = PrepareWidthEdit(history, before, after, activeLayer, L"Prepared Resize");
        Expect(publication.has_value() && publication->ChangesRevision() &&
                   history.CurrentRevision() == initialRevision && history.UndoCount() == 0 && !history.CanUndo(),
               "edit-preparation-does-not-publish-live-history");
        if (publication.has_value())
        {
            Expect(history.CanPublish(publication.value()) && history.Publish(publication.value()) &&
                       history.CurrentRevision() != initialRevision && history.CanUndo() &&
                       history.UndoName() == L"Prepared Resize",
                   "edit-publishes-prepared-revision-and-entry");
        }
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer;
        auto publication = PrepareWidthEdit(history, before, after, activeLayer);
        const Guid savedRevision = history.CurrentRevision();
        history.MarkSaved();
        const bool savedWithoutInvalidatingCandidate =
            publication.has_value() && history.CanPublish(publication.value()) && !history.IsModified();
        const bool published = publication.has_value() && history.Publish(publication.value());
        Expect(savedWithoutInvalidatingCandidate && published && history.IsModified() &&
                   history.CurrentRevision() != savedRevision,
               "mark-saved-after-preparation-survives-publication");
    }

    {
        DocumentHistory history;
        CanvasDocument document;
        document.width = 1;
        CanvasDocument edited = document;
        edited.width = 2;
        const std::optional<Guid> beforeActive = Guid::Generate();
        const std::optional<Guid> afterActive = Guid::Generate();
        auto initialEdit = DocumentHistory::PrepareEdit(history.CaptureState(), document, beforeActive, edited,
                                                        afterActive, L"Undo Redo");
        Expect(initialEdit && history.Publish(*initialEdit), "restore-setup-edit-published");

        const Guid editedRevision = history.CurrentRevision();
        auto undo = DocumentHistory::PrepareRestore(history.CaptureState(), false);
        Expect(undo.has_value() && undo->RestoreSnapshot().has_value() &&
                   undo->RestoreSnapshot()->document.has_value() && undo->RestoreSnapshot()->document->width == 1 &&
                   undo->RestoreSnapshot()->activeLayerID == beforeActive &&
                   undo->RestoreSnapshot()->revision != editedRevision && history.CurrentRevision() == editedRevision &&
                   history.CanUndo() && !history.CanRedo(),
               "undo-prepares-independent-before-snapshot");
        if (undo.has_value())
        {
            const bool undoPublished = history.Publish(undo.value());
            auto restored = undo->TakeRestoreSnapshot();
            Expect(undoPublished && restored.has_value() && restored->document.has_value() &&
                       restored->document->width == 1 && restored->activeLayerID == beforeActive &&
                       restored->revision == history.CurrentRevision() && !history.CanUndo() && history.CanRedo() &&
                       !undo->TakeRestoreSnapshot().has_value(),
                   "undo-payload-taken-once-after-publish");

            auto redo = DocumentHistory::PrepareRestore(history.CaptureState(), true);
            Expect(redo.has_value() && redo->RestoreSnapshot().has_value() &&
                       redo->RestoreSnapshot()->document.has_value() && redo->RestoreSnapshot()->document->width == 2 &&
                       redo->RestoreSnapshot()->activeLayerID == afterActive && history.CanRedo(),
                   "redo-prepares-independent-after-snapshot");
            if (redo.has_value())
            {
                const bool redoPublished = history.Publish(redo.value());
                const auto redone = redo->TakeRestoreSnapshot();
                Expect(redoPublished && redone.has_value() && redone->document.has_value() &&
                           redone->document->width == 2 && redone->activeLayerID == afterActive &&
                           redone->revision == history.CurrentRevision() && history.CanUndo() && !history.CanRedo(),
                       "redo-publishes-and-transfers-after-snapshot");
            }
        }
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer = Guid::Generate();
        const Guid initialRevision = history.CurrentRevision();
        auto begin = DocumentHistory::PrepareBegin(history.CaptureState(), before, activeLayer, L"Nested Edit");
        const bool firstBegin = begin.has_value() && history.Publish(begin.value());
        const bool firstBeginKeptRevision =
            history.CurrentRevision() == initialRevision && history.CaptureState().Depth() == 1;
        auto nestedBegin = DocumentHistory::PrepareBegin(history.CaptureState(), after, std::nullopt, L"Ignored Name");
        const bool secondBegin = nestedBegin.has_value() && history.Publish(nestedBegin.value());
        auto nestedEnd = DocumentHistory::PrepareEnd(history.CaptureState(), after, std::nullopt);
        const bool firstEnd = nestedEnd.has_value() && history.Publish(nestedEnd.value());
        const bool stillNested = history.CaptureState().Depth() == 1 && history.UndoCount() == 0;
        auto finalEnd = DocumentHistory::PrepareEnd(history.CaptureState(), after, std::nullopt);
        const bool secondEnd = finalEnd.has_value() && history.Publish(finalEnd.value());
        Expect(firstBegin && firstBeginKeptRevision && secondBegin && firstEnd && stillNested && secondEnd &&
                   history.CaptureState().Depth() == 0 && history.CurrentRevision() != initialRevision &&
                   history.UndoCount() == 1 && history.UndoName() == L"Nested Edit" && !history.CanRedo(),
               "nested-begin-end-publishes-one-opening-transaction");
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer;
        Expect(PublishWidthEdit(history, before, after, activeLayer, L"Redo Setup"), "noop-redo-setup-edit");
        auto undo = DocumentHistory::PrepareRestore(history.CaptureState(), false);
        const bool undone = undo.has_value() && history.Publish(undo.value());
        if (undo.has_value() && undone)
        {
            static_cast<void>(undo->TakeRestoreSnapshot());
        }
        const Guid savedRevision = history.CurrentRevision();
        auto begin = DocumentHistory::PrepareBegin(history.CaptureState(), before, activeLayer, L"No-op");
        const bool began = begin.has_value() && history.Publish(begin.value());
        auto end = DocumentHistory::PrepareEnd(history.CaptureState(), before, activeLayer);
        const bool ended = end.has_value() && history.Publish(end.value());
        Expect(undone && began && ended && history.CurrentRevision() == savedRevision && history.UndoCount() == 0 &&
                   history.CanRedo() && history.RedoName() == L"Redo Setup",
               "no-op-prepared-transaction-preserves-redo-and-revision");
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer;
        auto publication = PrepareWidthEdit(history, before, after, activeLayer);
        history.Begin(L"Invalidate", before, activeLayer);
        Expect(publication.has_value() && !history.CanPublish(publication.value()) &&
                   !history.Publish(publication.value()) && history.UndoCount() == 0 &&
                   history.CaptureState().Depth() == 1 && publication->ChangesRevision(),
               "stale-epoch-refuses-publication-and-retains-candidate");
    }

    {
        DocumentHistory original;
        DocumentHistory other;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer;
        auto publication = PrepareWidthEdit(original, before, after, activeLayer);
        Expect(publication.has_value() && !other.CanPublish(publication.value()) &&
                   !other.Publish(publication.value()) && original.UndoCount() == 0 && other.UndoCount() == 0 &&
                   publication->ChangesRevision(),
               "foreign-owner-refuses-publication-without-losing-candidate");
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer;
        CancelSource cancelled;
        const CancelToken token = cancelled.Token();
        static_cast<void>(cancelled.Cancel());
        const auto edit = DocumentHistory::PrepareEdit(history.CaptureState(), before, activeLayer, after, activeLayer,
                                                       L"Cancelled", token);
        const auto restore = DocumentHistory::PrepareRestore(history.CaptureState(), false, token);
        const auto begin =
            DocumentHistory::PrepareBegin(history.CaptureState(), before, activeLayer, L"Cancelled", token);
        history.Begin(L"For End", before, activeLayer);
        const auto end = DocumentHistory::PrepareEnd(history.CaptureState(), before, activeLayer, token);
        Expect(!edit.has_value() && !restore.has_value() && !begin.has_value() && !end.has_value() &&
                   history.CaptureState().Depth() == 1 && history.UndoCount() == 0,
               "cancelled-preparations-refuse-without-publishing");
    }

    {
        DocumentHistory owner;
        DocumentHistory foreign;
        CanvasDocument before;
        before.width = 1;
        CanvasDocument after = before;
        after.width = 2;
        const std::optional<Guid> activeLayer = Guid::Generate();
        Expect(PublishWidthEdit(owner, before, after, activeLayer, L"Restore Candidate"), "refused-restore-setup-edit");
        auto publication = DocumentHistory::PrepareRestore(owner.CaptureState(), false);
        const bool refused = publication.has_value() && !foreign.Publish(publication.value());
        const auto inaccessibleBeforePublish =
            publication.has_value() ? publication->TakeRestoreSnapshot() : std::optional<DocumentHistory::Snapshot>{};
        const bool stillOwned = publication.has_value() && publication->RestoreSnapshot().has_value();
        const bool stillValid = publication.has_value() && owner.CanPublish(publication.value());
        const bool published = stillValid && owner.Publish(publication.value());
        const auto transferred =
            publication.has_value() ? publication->TakeRestoreSnapshot() : std::optional<DocumentHistory::Snapshot>{};
        Expect(refused && !inaccessibleBeforePublish.has_value() && stillOwned && published &&
                   transferred.has_value() && transferred->document.has_value() && transferred->document->width == 1 &&
                   !publication->TakeRestoreSnapshot().has_value() && owner.CanRedo() && !owner.CanUndo(),
               "refused-publication-keeps-restore-payload-until-successful-publish");
    }

    {
        DocumentHistory history;
        CanvasDocument before;
        CanvasDocument after = before;
        after.width = 2;
        auto view = history.CaptureState();
        auto transferredView = std::move(view);
        auto refused = DocumentHistory::PrepareEdit(std::move(view), before, {}, after, {}, L"Moved view");
        auto candidate = DocumentHistory::PrepareEdit(std::move(transferredView), before, {}, after, {}, L"Moved edit");
        Expect(!refused && candidate && history.CanPublish(*candidate), "moved-view-has-no-publication-authority");
        if (candidate)
        {
            auto transferred = std::move(*candidate);
            const bool refusedOriginal = !history.CanPublish(*candidate) && !history.Publish(*candidate);
            Expect(refusedOriginal && history.Publish(transferred) && history.UndoCount() == 1 &&
                       history.UndoName() == L"Moved edit" && !history.Publish(transferred),
                   "moved-publication-authority-transfers-once");
        }
    }
    {
        DocumentHistory history(1, 4);
        const std::array<std::uint8_t, 4> beforeBytes{17, 34, 51, 255};
        const std::array<std::uint8_t, 8> afterBytes{68, 85, 102, 255, 119, 136, 153, 255};
        DocumentFixture beforeFixture;
        beforeFixture.width = beforeFixture.height = 1;
        auto layer = ImageLayer::Blank(L"Trim restore", {1, 1});
        layer.asset = RasterAsset{
            Raster::Make(1, 1, RasterFormat::Bgra8888, beforeBytes.data(), beforeBytes.size()), {}, layer.name};
        const auto active = layer.id;
        beforeFixture.layers.push_back(std::move(layer));
        const CanvasDocument before = std::move(beforeFixture).Seal();
        DocumentFixture afterFixture = DocumentFixture::Copy(before);
        afterFixture.width = 2;
        afterFixture.layers.front().transform.size = {2, 1};
        afterFixture.layers.front().asset.value().image =
            Raster::Make(2, 1, RasterFormat::Bgra8888, afterBytes.data(), afterBytes.size());
        const CanvasDocument after = std::move(afterFixture).Seal();
        const auto openingRevision = history.CurrentRevision();
        Expect(PublishWidthEdit(history, before, after, active, L"Trim restore") && history.CanUndo(),
               "trim-restore-setup-retains-before-storage");
        auto undo = DocumentHistory::PrepareRestore(history.CaptureState(), false);
        Expect(undo && undo->RestoreSnapshot() && history.CanUndo() && !history.CanRedo(),
               "trim-restore-preparation-keeps-live-stacks");
        const bool published = undo && history.Publish(*undo);
        auto restored = undo ? undo->TakeRestoreSnapshot() : std::optional<DocumentHistory::Snapshot>{};
        std::array<std::uint8_t, 4> pixel{};
        if (restored && restored->document)
            restored->document->layers.front().asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
        Expect(published && restored && restored->document && restored->document->width == 1 &&
                   restored->activeLayerID == active && restored->revision == openingRevision &&
                   history.CurrentRevision() == openingRevision && !history.CanUndo() && !history.CanRedo() &&
                   pixel == beforeBytes,
               "trim-discards-destination-entry-with-independent-restore-and-literal-pixel");
    }
    return gFailures;
}

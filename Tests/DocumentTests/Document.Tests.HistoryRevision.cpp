#include "Document.Tests.HistoryRevision.h"

#include "Document.h"

#include <cstdio>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS history-revision.%s\n", name);
    }
    else
    {
        std::printf("FAIL history-revision.%s\n", name);
        ++gFailures;
    }
}

void Edit(DocumentHistory& history, std::optional<CanvasDocument>& document, std::optional<Guid>& activeLayer,
          int width)
{
    history.Begin(L"Resize", document, activeLayer);
    document.value().width = width;
    history.End(document, activeLayer);
}

std::optional<DocumentHistory::Snapshot> Undo(DocumentHistory& history, std::optional<CanvasDocument>& document,
                                              std::optional<Guid>& activeLayer)
{
    std::optional<DocumentHistory::Snapshot> snapshot = history.Undo();
    if (snapshot.has_value())
    {
        document = snapshot->document;
        activeLayer = snapshot->activeLayerID;
    }
    return snapshot;
}

std::optional<DocumentHistory::Snapshot> Redo(DocumentHistory& history, std::optional<CanvasDocument>& document,
                                              std::optional<Guid>& activeLayer)
{
    std::optional<DocumentHistory::Snapshot> snapshot = history.Redo();
    if (snapshot.has_value())
    {
        document = snapshot->document;
        activeLayer = snapshot->activeLayerID;
    }
    return snapshot;
}
} // namespace

int RunHistoryRevisionTests()
{
    gFailures = 0;

    {
        DocumentHistory history;
        Expect(!history.IsModified(), "current-snapshot-starts-clean");
        history.MarkSaved();
        Expect(!history.IsModified(), "mark-current-snapshot-clean");

        std::optional<CanvasDocument> document = CanvasDocument{};
        std::optional<Guid> activeLayer;
        const Guid savedRevision = history.CurrentRevision();
        Edit(history, document, activeLayer, 2);
        history.MarkSaved(savedRevision);
        Expect(history.IsModified(), "delayed-save-keeps-later-edit-dirty");
        const std::optional<DocumentHistory::Snapshot> undone = Undo(history, document, activeLayer);
        Expect(undone.has_value() && !history.IsModified() && document->width == 1, "undo-to-captured-revision-clean");
        if (undone.has_value())
        {
            const std::optional<DocumentHistory::Snapshot> redone = Redo(history, document, activeLayer);
            Expect(redone.has_value() && history.IsModified(), "redo-away-from-captured-revision-dirty");
        }
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        std::optional<Guid> activeLayer;
        Edit(history, document, activeLayer, 2);
        history.MarkSaved();
        const std::optional<DocumentHistory::Snapshot> atA = Undo(history, document, activeLayer);
        Expect(atA.has_value() && history.IsModified() && document->width == 1,
               "undo-from-saved-later-revision-is-dirty");
        if (atA.has_value())
        {
            history.MarkSaved(history.CurrentRevision());
            Expect(!history.IsModified(), "save-undo-restored-older-revision-clean");
        }
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        std::optional<Guid> activeLayer;
        Edit(history, document, activeLayer, 2);
        const Guid branchRevision = history.CurrentRevision();
        const std::optional<DocumentHistory::Snapshot> beforeB = Undo(history, document, activeLayer);
        Expect(beforeB.has_value() && document->width == 1, "branch-setup-undo");
        if (beforeB.has_value())
        {
            Edit(history, document, activeLayer, 3);
            Expect(!history.CanRedo() && history.IsModified(), "branch-discards-redo-and-stays-dirty");
            history.MarkSaved(branchRevision);
            Expect(history.IsModified(), "removed-branch-save-revision-stays-dirty");
        }
    }

    {
        DocumentHistory history(1, 0);
        std::optional<CanvasDocument> document = CanvasDocument{};
        std::optional<Guid> activeLayer;
        Edit(history, document, activeLayer, 2);
        Edit(history, document, activeLayer, 3);
        const std::optional<DocumentHistory::Snapshot> atB = Undo(history, document, activeLayer);
        Expect(atB.has_value() && document->width == 2, "trim-setup-captures-middle-revision");
        if (atB.has_value())
        {
            const Guid savedRevision = history.CurrentRevision();
            const std::optional<DocumentHistory::Snapshot> atC = Redo(history, document, activeLayer);
            Expect(atC.has_value(), "trim-setup-redo");
            if (atC.has_value())
            {
                Edit(history, document, activeLayer, 4);
                history.MarkSaved(savedRevision);
                Expect(history.UndoCount() == 1 && history.IsModified(), "trim-drops-saved-revision");
                const std::optional<DocumentHistory::Snapshot> undone = Undo(history, document, activeLayer);
                Expect(undone.has_value() && history.IsModified() && !history.CanUndo(),
                       "trimmed-saved-revision-is-unreachable-and-dirty");
            }
        }
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        std::optional<Guid> activeLayer;
        Edit(history, document, activeLayer, 2);
        const Guid priorRevision = history.CurrentRevision();
        history.Reset();
        Expect(history.CurrentRevision() != priorRevision && !history.IsModified() && !history.CanUndo() &&
                   !history.CanRedo(),
               "reset-clears-and-saves-new-revision");
    }

    return gFailures;
}

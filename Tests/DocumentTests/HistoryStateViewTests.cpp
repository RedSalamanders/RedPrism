#include "HistoryStateViewTests.h"

#include "Document.h"

#include <cstdio>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-state-view.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++gFailures;
    }
}

void EditWidth(DocumentHistory& history, std::optional<CanvasDocument>& document, int width,
               const wchar_t* name = L"Canvas Size")
{
    const std::optional<Guid> activeLayer;
    history.Begin(name, document, activeLayer);
    document->width = width;
    history.End(document, activeLayer);
}

void ApplySnapshot(std::optional<CanvasDocument>& document, const std::optional<DocumentHistory::Snapshot>& snapshot)
{
    if (snapshot.has_value())
    {
        document = snapshot->document;
    }
}
} // namespace

int RunHistoryStateViewTests()
{
    gFailures = 0;

    {
        DocumentHistory history;
        const DocumentHistory::StateView view = history.CaptureState();
        Expect(history.IsCurrent(view), "empty-capture-is-current");
        Expect(view.Revision() == history.CurrentRevision() && view.Depth() == 0 && view.PastCount() == 0 &&
                   view.FutureCount() == 0 && view.UndoName().empty() && view.RedoName().empty(),
               "empty-capture-values");
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        const Guid beforeRevision = history.CurrentRevision();
        EditWidth(history, document, 2);
        const DocumentHistory::StateView view = history.CaptureState();
        Expect(history.IsCurrent(view) && view.Revision() != beforeRevision && view.Depth() == 0 &&
                   view.PastCount() == 1 && view.FutureCount() == 0 && view.UndoName() == L"Canvas Size" &&
                   view.RedoName().empty(),
               "literal-metadata-edit-records-named-entry");
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        EditWidth(history, document, 2, L"First Edit");
        const DocumentHistory::StateView view = history.CaptureState();
        const Guid capturedRevision = view.Revision();

        ApplySnapshot(document, history.Undo());
        Expect(!history.IsCurrent(view) && view.Revision() == capturedRevision && view.PastCount() == 1 &&
                   view.FutureCount() == 0 && view.UndoName() == L"First Edit" && view.RedoName().empty(),
               "view-survives-undo");

        ApplySnapshot(document, history.Redo());
        Expect(!history.IsCurrent(view) && view.Revision() == capturedRevision && view.PastCount() == 1 &&
                   view.FutureCount() == 0 && view.UndoName() == L"First Edit" && view.RedoName().empty(),
               "view-survives-redo");

        history.Reset();
        Expect(!history.IsCurrent(view) && view.Revision() == capturedRevision && view.PastCount() == 1 &&
                   view.FutureCount() == 0 && view.UndoName() == L"First Edit" && view.RedoName().empty(),
               "view-survives-reset");
    }

    {
        DocumentHistory history(1, 0);
        std::optional<CanvasDocument> document = CanvasDocument{};
        EditWidth(history, document, 2, L"Trimmed Entry");
        const DocumentHistory::StateView view = history.CaptureState();
        const Guid capturedRevision = view.Revision();
        EditWidth(history, document, 3, L"Retained Entry");
        Expect(!history.IsCurrent(view) && history.UndoCount() == 1 && view.Revision() == capturedRevision &&
                   view.PastCount() == 1 && view.FutureCount() == 0 && view.UndoName() == L"Trimmed Entry" &&
                   view.RedoName().empty(),
               "view-retains-trimmed-history-metadata");
    }

    {
        DocumentHistory history;
        const std::optional<CanvasDocument> document = CanvasDocument{};
        const std::optional<Guid> activeLayer;

        const DocumentHistory::StateView beforeBegin = history.CaptureState();
        history.Begin(L"No-op", document, activeLayer);
        Expect(!history.IsCurrent(beforeBegin), "begin-invalidates-view");

        const DocumentHistory::StateView afterBegin = history.CaptureState();
        history.Begin(L"Nested Name Is Ignored", document, activeLayer);
        Expect(!history.IsCurrent(afterBegin), "nested-begin-invalidates-view");

        const DocumentHistory::StateView afterNestedBegin = history.CaptureState();
        history.End(document, activeLayer);
        Expect(!history.IsCurrent(afterNestedBegin), "nested-end-invalidates-view");

        const DocumentHistory::StateView beforeNoOpEnd = history.CaptureState();
        history.End(document, activeLayer);
        Expect(!history.IsCurrent(beforeNoOpEnd) && history.UndoCount() == 0 &&
                   history.CurrentRevision() == beforeNoOpEnd.Revision(),
               "no-op-end-invalidates-view");
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        const Guid initialRevision = history.CurrentRevision();
        const DocumentHistory::StateView initial = history.CaptureState();
        history.MarkSaved(initialRevision);
        Expect(history.IsCurrent(initial) && !history.IsModified(), "mark-current-saved-keeps-view-current");

        EditWidth(history, document, 2, L"Saved Revision Probe");
        const DocumentHistory::StateView edited = history.CaptureState();
        history.MarkSaved(edited.Revision());
        Expect(history.IsCurrent(edited) && !history.IsModified(),
               "mark-current-revision-keeps-view-current-and-clean");

        history.MarkSaved(initialRevision);
        Expect(history.IsCurrent(edited) && history.IsModified(), "mark-old-revision-keeps-view-current-and-dirty");
        ApplySnapshot(document, history.Undo());
        Expect(!history.IsModified(), "undo-to-old-saved-revision-is-clean");
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        EditWidth(history, document, 2, L"Redo Entry");
        ApplySnapshot(document, history.Undo());
        const DocumentHistory::StateView beforeNoOp = history.CaptureState();
        const Guid revision = history.CurrentRevision();
        const std::wstring redoName = history.RedoName();
        history.Begin(L"No-op Branch", document, std::nullopt);
        history.End(document, std::nullopt);
        Expect(!history.IsCurrent(beforeNoOp) && history.CurrentRevision() == revision && history.CanRedo() &&
                   history.RedoName() == redoName && beforeNoOp.FutureCount() == 1,
               "no-op-end-preserves-live-redo-and-revision");
    }

    {
        DocumentHistory history;
        std::optional<CanvasDocument> document = CanvasDocument{};
        EditWidth(history, document, 2, L"Owned By Original");
        const DocumentHistory::StateView originalView = history.CaptureState();
        const DocumentHistory copiedHistory = history;
        Expect(history.IsCurrent(originalView) && !copiedHistory.IsCurrent(originalView),
               "copied-history-has-distinct-owner");
    }

    return gFailures;
}

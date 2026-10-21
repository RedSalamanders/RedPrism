#pragma once

#include "Document.h"

#include <optional>
#include <unordered_set>
#include <utility>

// Session authority also exists with no live document (Redo after Undo of New Canvas).
// Capture is owner-only and constant-sized; workers never consult the session through it.
class HistoryRestoreAuthority final
{
  public:
    HistoryRestoreAuthority() = default;

  private:
    friend class DocumentSession;
    friend std::optional<PreparedHistoryRestore> PrepareHistoryRestorePublication(HistoryRestoreAuthority,
                                                                                  DocumentHistory::StateView, bool,
                                                                                  CancelToken,
                                                                                  DocumentHistory::PrepareObserver);
    Guid _session;
    std::uint64_t _documentSerial = 0, _generation = 0, _restoreSerial = 0;
    std::optional<Guid> _documentID, _active;
    std::optional<CropRect> _crop;
    bool _maskSelected = false;
    bool _valid = false;
};

class PreparedHistoryRestore final
{
  public:
    PreparedHistoryRestore(const PreparedHistoryRestore&) = delete;
    PreparedHistoryRestore& operator=(const PreparedHistoryRestore&) = delete;
    PreparedHistoryRestore(PreparedHistoryRestore&& source) noexcept;
    PreparedHistoryRestore& operator=(PreparedHistoryRestore&& source) noexcept;
    // Owner try and worker refresh retain the exact prepared Undo/Redo snapshot and its original revision.
    // They do not recreate restore intent or consult the live session. Hosts reserve the refresh overlap first.
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});

  private:
    friend class DocumentSession;
    friend std::optional<PreparedHistoryRestore> PrepareHistoryRestorePublication(HistoryRestoreAuthority,
                                                                                  DocumentHistory::StateView, bool,
                                                                                  CancelToken,
                                                                                  DocumentHistory::PrepareObserver);
    PreparedHistoryRestore(HistoryRestoreAuthority authority, DocumentHistory::StateView view)
        : _authority(std::move(authority)), _view(std::move(view))
    {
    }
    HistoryRestoreAuthority _authority;
    DocumentHistory::StateView _view;
    std::optional<DocumentHistory::Publication> _history;
    std::unordered_set<Guid> _selected;
    std::optional<DocumentHistory::Snapshot> _retired;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _changesDocumentSerial = false, _maskSelected = false, _applied = false;
};

[[nodiscard]] std::optional<PreparedHistoryRestore> PrepareHistoryRestorePublication(
    HistoryRestoreAuthority authority, DocumentHistory::StateView view, bool redo, CancelToken cancellation = {},
    DocumentHistory::PrepareObserver observer = {});

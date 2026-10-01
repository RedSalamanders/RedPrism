#pragma once

#include "CanvasSize.h"

#include <optional>
#include <utility>

// Worker-bound complete document and history publication. Replaced document/draft owners stay here through
// worker retirement; no caller can replace the result or its prepared history independently.
class PreparedDocumentSize final
{
  public:
    PreparedDocumentSize(const PreparedDocumentSize&) = delete;
    PreparedDocumentSize& operator=(const PreparedDocumentSize&) = delete;
    PreparedDocumentSize(PreparedDocumentSize&&) noexcept = default;
    PreparedDocumentSize& operator=(PreparedDocumentSize&&) noexcept = default;

  private:
    friend class DocumentSession;
    friend std::optional<PreparedDocumentSize> PrepareDocumentSizePublication(const std::optional<CanvasDocument>&,
                                                                              const std::optional<Guid>&, JobToken,
                                                                              DocumentHistory::StateView,
                                                                              CanvasDocument, std::wstring, CancelToken,
                                                                              DocumentHistory::PrepareObserver);
    PreparedDocumentSize(JobToken token, std::optional<Guid> active, CanvasDocument document,
                         DocumentHistory::Publication history)
        : _token(std::move(token)), _active(std::move(active)), _document(std::move(document)),
          _history(std::move(history))
    {
    }
    JobToken _token;
    std::optional<Guid> _active;
    std::optional<CanvasDocument> _document;
    DocumentHistory::Publication _history;
    std::optional<LassoDraft> _retiredLasso;
    std::optional<DocumentSelection> _retiredSelectionMove;
};

[[nodiscard]] std::optional<PreparedDocumentSize> PrepareDocumentSizePublication(
    const std::optional<CanvasDocument>& source, const std::optional<Guid>& active, JobToken token,
    DocumentHistory::StateView history, CanvasDocument result, std::wstring name, CancelToken cancellation = {},
    DocumentHistory::PrepareObserver observer = {});

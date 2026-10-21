#pragma once

#include "CanvasSize.h"

#include <memory>
#include <optional>
#include <utility>

// Worker-bound complete document and history publication. Replaced document/draft owners stay here through
// worker retirement; no caller can replace the result or its prepared history independently.
class PreparedDocumentSize final
{
  public:
    PreparedDocumentSize(const PreparedDocumentSize&) = delete;
    PreparedDocumentSize& operator=(const PreparedDocumentSize&) = delete;
    PreparedDocumentSize(PreparedDocumentSize&& source) noexcept
        : _memory(std::move(source._memory)), _token(std::move(source._token)), _active(std::move(source._active)),
          _document(std::move(source._document)), _history(std::move(source._history)),
          _retiredLasso(std::move(source._retiredLasso)),
          _retiredSelectionMove(std::move(source._retiredSelectionMove)), _valid(std::exchange(source._valid, false)),
          _applied(source._applied)
    {
    }
    PreparedDocumentSize& operator=(PreparedDocumentSize&& source) noexcept
    {
        if (this != &source)
        {
            // Retire replaced fields while their previous accounting owner is still retained.
            _token = std::move(source._token);
            _active = std::move(source._active);
            _document = std::move(source._document);
            _history = std::move(source._history);
            _retiredLasso = std::move(source._retiredLasso);
            _retiredSelectionMove = std::move(source._retiredSelectionMove);
            _memory = std::move(source._memory);
            _valid = std::exchange(source._valid, false);
            _applied = source._applied;
        }
        return *this;
    }
    // Stable immutable borrow before checked publication only; never the swapped retired predecessor.
    // Moves invalidate the source. Neither this source nor a history permit authorizes document mutation.
    [[nodiscard]] const CanvasDocument* FinalDocument() const noexcept
    {
        return _valid && !_applied && _document ? &*_document : nullptr;
    }
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});

  private:
    friend class DocumentSession;
    friend std::optional<PreparedDocumentSize> PrepareDocumentSizePublication(
        const std::optional<CanvasDocument>&, const std::optional<Guid>&, JobToken, DocumentHistory::StateView,
        CanvasDocument, std::wstring, CancelToken, DocumentHistory::PrepareObserver,
        std::shared_ptr<const SharedPublicationMemory>);
    PreparedDocumentSize(JobToken token, std::optional<Guid> active, CanvasDocument document,
                         DocumentHistory::Publication history, std::shared_ptr<const SharedPublicationMemory> memory)
        : _memory(std::move(memory)), _token(std::move(token)), _active(std::move(active)),
          _document(std::move(document)), _history(std::move(history))
    {
    }
    std::shared_ptr<const SharedPublicationMemory> _memory; // Dies after every input/output/retired graph.
    JobToken _token;
    std::optional<Guid> _active;
    std::optional<CanvasDocument> _document;
    DocumentHistory::Publication _history;
    std::optional<LassoDraft> _retiredLasso;
    std::optional<DocumentSelection> _retiredSelectionMove;
    bool _valid = true, _applied = false;
};

[[nodiscard]] std::optional<PreparedDocumentSize> PrepareDocumentSizePublication(
    const std::optional<CanvasDocument>& source, const std::optional<Guid>& active, JobToken token,
    DocumentHistory::StateView history, CanvasDocument result, std::wstring name, CancelToken cancellation = {},
    DocumentHistory::PrepareObserver observer = {}, std::shared_ptr<const SharedPublicationMemory> memory = {});

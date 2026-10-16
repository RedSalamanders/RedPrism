#pragma once

#include "HistoryRestoreWork.h"

#include <cstdint>
#include <optional>
#include <utility>

// Capture only after complete admission. All retained roots are immutable and copy in constant space.
class SelectionMoveOpeningInput final
{
  public:
    SelectionMoveOpeningInput(const SelectionMoveOpeningInput&) = delete;
    SelectionMoveOpeningInput& operator=(const SelectionMoveOpeningInput&) = delete;
    SelectionMoveOpeningInput(SelectionMoveOpeningInput&& source) noexcept
        : _authority(std::move(source._authority)), _token(std::move(source._token)),
          _history(std::move(source._history)), _document(std::move(source._document)), _active(source._active),
          _opening(source._opening), _valid(std::exchange(source._valid, false))
    {
    }
    SelectionMoveOpeningInput& operator=(SelectionMoveOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedSelectionMoveOpening;
    SelectionMoveOpeningInput(HistoryRestoreAuthority authority, JobToken token, DocumentHistory::StateView history,
                              CanvasDocument document, std::optional<Guid> active, std::uint64_t opening) noexcept
        : _authority(std::move(authority)), _token(std::move(token)), _history(std::move(history)),
          _document(std::move(document)), _active(active), _opening(opening)
    {
    }
    HistoryRestoreAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    std::uint64_t _opening = 0;
    bool _valid = true;
};
class PreparedSelectionMoveOpening final
{
  public:
    PreparedSelectionMoveOpening(const PreparedSelectionMoveOpening&) = delete;
    PreparedSelectionMoveOpening& operator=(const PreparedSelectionMoveOpening&) = delete;
    PreparedSelectionMoveOpening(PreparedSelectionMoveOpening&&) noexcept = default;
    PreparedSelectionMoveOpening& operator=(PreparedSelectionMoveOpening&&) = delete;

  private:
    friend class DocumentSession;
    explicit PreparedSelectionMoveOpening(SelectionMoveOpeningInput input) noexcept : _input(std::move(input)) {}
    SelectionMoveOpeningInput _input;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<DocumentSelection> _origin;
    bool _applied = false;
};

#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <utility>

// Capture after admission. The exact original outline is retained, never expanded on the owner.
class SelectionMoveClosureInput final
{
  public:
    SelectionMoveClosureInput(const SelectionMoveClosureInput&) = delete;
    SelectionMoveClosureInput& operator=(const SelectionMoveClosureInput&) = delete;
    SelectionMoveClosureInput(SelectionMoveClosureInput&& source) noexcept
        : _token(std::move(source._token)), _history(std::move(source._history)),
          _document(std::move(source._document)), _origin(std::move(source._origin)), _active(source._active),
          _crop(source._crop), _opening(source._opening), _maskSelected(source._maskSelected), _cancel(source._cancel),
          _valid(std::exchange(source._valid, false))
    {
    }
    SelectionMoveClosureInput& operator=(SelectionMoveClosureInput&& source) noexcept
    {
        if (this != &source)
        {
            _token = std::move(source._token);
            _history = std::move(source._history);
            _document = std::move(source._document);
            _origin = std::move(source._origin);
            _active = source._active;
            _crop = source._crop;
            _opening = source._opening;
            _maskSelected = source._maskSelected;
            _cancel = source._cancel;
            _valid = std::exchange(source._valid, false);
        }
        return *this;
    }

  private:
    friend class DocumentSession;
    friend class PreparedSelectionMoveClosure;
    SelectionMoveClosureInput(JobToken token, DocumentHistory::StateView history, CanvasDocument document,
                              DocumentSelection origin, std::optional<Guid> active, std::optional<CropRect> crop,
                              std::uint64_t opening, bool maskSelected, bool cancel) noexcept
        : _token(std::move(token)), _history(std::move(history)), _document(std::move(document)),
          _origin(std::move(origin)), _active(active), _crop(crop), _opening(opening), _maskSelected(maskSelected),
          _cancel(cancel)
    {
    }
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    DocumentSelection _origin;
    std::optional<Guid> _active;
    std::optional<CropRect> _crop;
    std::uint64_t _opening = 0;
    bool _maskSelected = false, _cancel = false, _valid = true;
};

class PreparedSelectionMoveClosure final
{
  public:
    PreparedSelectionMoveClosure(const PreparedSelectionMoveClosure&) = delete;
    PreparedSelectionMoveClosure& operator=(const PreparedSelectionMoveClosure&) = delete;
    PreparedSelectionMoveClosure(PreparedSelectionMoveClosure&&) noexcept = default;
    PreparedSelectionMoveClosure& operator=(PreparedSelectionMoveClosure&&) noexcept = default;

  private:
    friend class DocumentSession;
    explicit PreparedSelectionMoveClosure(SelectionMoveClosureInput input) noexcept : _input(std::move(input)) {}
    SelectionMoveClosureInput _input;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<DocumentSelection> _retiredOrigin;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};

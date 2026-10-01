#pragma once

#include "HistoryRestoreWork.h"

#include <cstdint>
#include <optional>
#include <utility>

enum class SelectionCommandKind : std::uint8_t
{
    SelectAll,
    Deselect,
};

// Sealed owner capture after admission. It owns the exact before-document/history and opaque settled-session
// authority. Workers cannot substitute a different snapshot or a separately prepared history publication.
class SelectionCommandInput final
{
  public:
    SelectionCommandInput(const SelectionCommandInput&) = delete;
    SelectionCommandInput& operator=(const SelectionCommandInput&) = delete;
    SelectionCommandInput(SelectionCommandInput&& source) noexcept
        : _kind(source._kind), _authority(std::move(source._authority)), _token(std::move(source._token)),
          _history(std::move(source._history)), _document(std::move(source._document)),
          _active(std::move(source._active)), _valid(std::exchange(source._valid, false))
    {
    }
    SelectionCommandInput& operator=(SelectionCommandInput&& source) noexcept
    {
        if (this != &source)
        {
            _kind = source._kind;
            _authority = std::move(source._authority);
            _token = std::move(source._token);
            _history = std::move(source._history);
            _document = std::move(source._document);
            _active = std::move(source._active);
            _valid = std::exchange(source._valid, false);
        }
        return *this;
    }

  private:
    friend class DocumentSession;
    friend class PreparedSelectionCommand;
    SelectionCommandInput(SelectionCommandKind kind, HistoryRestoreAuthority authority, JobToken token,
                          DocumentHistory::StateView history, CanvasDocument document,
                          std::optional<Guid> active) noexcept
        : _kind(kind), _authority(std::move(authority)), _token(std::move(token)), _history(std::move(history)),
          _document(std::move(document)), _active(std::move(active))
    {
    }
    SelectionCommandKind _kind;
    HistoryRestoreAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    bool _valid = true;
};

class PreparedSelectionCommand final
{
  public:
    PreparedSelectionCommand(const PreparedSelectionCommand&) = delete;
    PreparedSelectionCommand& operator=(const PreparedSelectionCommand&) = delete;
    PreparedSelectionCommand(PreparedSelectionCommand&&) noexcept = default;
    PreparedSelectionCommand& operator=(PreparedSelectionCommand&&) noexcept = default;
    [[nodiscard]] bool ChangesSelection() const noexcept
    {
        return _history.has_value();
    }

  private:
    friend class DocumentSession;
    explicit PreparedSelectionCommand(SelectionCommandInput input) noexcept : _input(std::move(input)) {}
    SelectionCommandInput _input;
    std::optional<CanvasDocument> _document;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};

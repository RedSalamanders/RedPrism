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

// Selection commands preserve a live paint/warp/shape gesture. This authority keeps only weak control-block
// identities, never a readable or owned mutable gesture. It is separate from settled history-restore authority.
class SelectionCommandAuthority final
{
  private:
    friend class DocumentSession;
    SelectionCommandAuthority() = default;
    Guid _session;
    std::uint64_t _documentSerial = 0, _generation = 0, _restoreSerial = 0, _shapeOpening = 0;
    std::optional<Guid> _documentID, _active;
    std::optional<CropRect> _crop;
    std::weak_ptr<BrushStroke> _brush;
    std::weak_ptr<WarpStroke> _warp;
    bool _shape = false, _maskSelected = false, _valid = false;
};

// Sealed owner capture after admission. It owns the exact before-document/history and opaque settled-session
// or preserved-gesture authority. Workers cannot substitute a different snapshot or history publication.
class SelectionCommandInput final
{
  public:
    SelectionCommandInput(const SelectionCommandInput&) = delete;
    SelectionCommandInput& operator=(const SelectionCommandInput&) = delete;
    SelectionCommandInput(SelectionCommandInput&& source) noexcept
        : _kind(source._kind), _authority(std::move(source._authority)),
          _preservedGesture(std::move(source._preservedGesture)), _token(std::move(source._token)),
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
            _preservedGesture = std::move(source._preservedGesture);
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
    std::optional<SelectionCommandAuthority> _preservedGesture;
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

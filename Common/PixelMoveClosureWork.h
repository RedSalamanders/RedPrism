#pragma once

#include "PixelMoveOpeningWork.h"

#include <memory>
#include <optional>
#include <thread>
#include <utility>

// Terminal capture under the original gesture's complete admission. Only immutable source handles and
// bounded authority/scalars cross; full target metadata and final pixels/history are worker work.
class PixelMoveClosureInput final
{
  public:
    PixelMoveClosureInput(const PixelMoveClosureInput&) = delete;
    PixelMoveClosureInput& operator=(const PixelMoveClosureInput&) = delete;
    PixelMoveClosureInput& operator=(PixelMoveClosureInput&&) = delete;
    PixelMoveClosureInput(PixelMoveClosureInput&& other) noexcept
        : _read(other._read), _editor(other._editor), _authority(other._authority), _history(std::move(other._history)),
          _document(std::move(other._document)), _token(std::move(other._token)), _lift(std::move(other._lift)),
          _active(other._active), _offset(other._offset), _opening(other._opening), _sequence(other._sequence),
          _pixelLimit(other._pixelLimit), _duplicate(other._duplicate), _owner(other._owner),
          _valid(std::exchange(other._valid, false))
    {
    }

  private:
    friend class DocumentSession;
    friend class PreparedPixelMoveClosure;
    PixelMoveClosureInput(DocumentReadAuthority read, HistoryRestoreAuthority editor,
                          DocumentHistory::Authority authority, DocumentHistory::StateView history,
                          CanvasDocument document, JobToken token, std::shared_ptr<const PixelMoveOpening> lift,
                          Guid active, Vec2 offset, std::uint64_t opening, std::uint64_t sequence,
                          std::int64_t pixelLimit, bool duplicate) noexcept
        : _read(read), _editor(editor), _authority(authority), _history(std::move(history)),
          _document(std::move(document)), _token(std::move(token)), _lift(std::move(lift)), _active(active),
          _offset(offset), _opening(opening), _sequence(sequence), _pixelLimit(pixelLimit), _duplicate(duplicate),
          _owner(std::this_thread::get_id())
    {
    }
    DocumentReadAuthority _read;
    HistoryRestoreAuthority _editor;
    DocumentHistory::Authority _authority;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    JobToken _token;
    std::shared_ptr<const PixelMoveOpening> _lift;
    Guid _active;
    Vec2 _offset;
    std::uint64_t _opening, _sequence;
    std::int64_t _pixelLimit;
    bool _duplicate;
    std::thread::id _owner;
    bool _valid = true;
};

class PreparedPixelMoveClosure final
{
  public:
    PreparedPixelMoveClosure(const PreparedPixelMoveClosure&) = delete;
    PreparedPixelMoveClosure& operator=(const PreparedPixelMoveClosure&) = delete;
    PreparedPixelMoveClosure(PreparedPixelMoveClosure&&) noexcept = default;
    PreparedPixelMoveClosure& operator=(PreparedPixelMoveClosure&&) = delete;
    [[nodiscard]] bool ChangesDocument() const noexcept
    {
        return _history.has_value();
    }
    // Immutable lifetime-bound final source for worker rows/residency/recording preparation.
    // Borrow only while this closure is stable; this grants no history or document apply authority.
    // Apply swaps _after to the retired predecessor, which must never be reported as the final image.
    [[nodiscard]] const CanvasDocument* FinalDocument() const noexcept
    {
        return !_applied && _input._valid && _after && _history ? &*_after : nullptr;
    }
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});

  private:
    friend class DocumentSession;
    explicit PreparedPixelMoveClosure(PixelMoveClosureInput input) noexcept : _input(std::move(input)) {}
    // Destruction order: every input/output/retired graph releases before the accounting owner.
    std::shared_ptr<const SharedPublicationMemory> _memory;
    PixelMoveClosureInput _input;
    std::optional<CanvasDocument> _after;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<PixelMove> _retiredPixelMove;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false, _residencyAttempted = false;
};

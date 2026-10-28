#pragma once

#include "HistoryRestoreWork.h"
#include "RasterEditWork.h"

#include <memory>
#include <optional>
#include <thread>
#include <utility>

// Owner captures only immutable roots and scalar authority under complete original admission.
// No layer/mask/name copy, geometry traversal, coverage or history mutation occurs at capture.
class PixelMoveOpeningInput final
{
  public:
    PixelMoveOpeningInput(const PixelMoveOpeningInput&) = delete;
    PixelMoveOpeningInput& operator=(const PixelMoveOpeningInput&) = delete;
    PixelMoveOpeningInput& operator=(PixelMoveOpeningInput&&) = delete;
    PixelMoveOpeningInput(PixelMoveOpeningInput&& source) noexcept
        : _editor(source._editor), _read(source._read), _history(source._history),
          _document(std::move(source._document)), _token(std::move(source._token)), _active(source._active),
          _opening(source._opening), _pixelLimit(source._pixelLimit), _duplicate(source._duplicate),
          _owner(source._owner), _valid(std::exchange(source._valid, false))
    {
    }

  private:
    friend class DocumentSession;
    friend class PreparedPixelMoveOpening;
    PixelMoveOpeningInput(HistoryRestoreAuthority editor, DocumentReadAuthority read,
                          DocumentHistory::Authority history, CanvasDocument document, JobToken token, Guid active,
                          std::uint64_t opening, std::int64_t pixelLimit, bool duplicate) noexcept
        : _editor(editor), _read(read), _history(history), _document(std::move(document)), _token(std::move(token)),
          _active(active), _opening(opening), _pixelLimit(pixelLimit), _duplicate(duplicate),
          _owner(std::this_thread::get_id())
    {
    }
    HistoryRestoreAuthority _editor;
    DocumentReadAuthority _read;
    DocumentHistory::Authority _history;
    std::optional<CanvasDocument> _document;
    JobToken _token;
    Guid _active;
    std::uint64_t _opening;
    std::int64_t _pixelLimit;
    bool _duplicate;
    std::thread::id _owner;
    bool _valid = true;
};

// The CPU opening, full target token and pending session record are constructed together on a worker.
// The original parent retains this complete graph through cancellation and frame-safe retirement.
// Applying swaps only the pending record; it opens no history transaction and moves no document pixels.
class PreparedPixelMoveOpening final
{
  public:
    PreparedPixelMoveOpening(const PreparedPixelMoveOpening&) = delete;
    PreparedPixelMoveOpening& operator=(const PreparedPixelMoveOpening&) = delete;
    PreparedPixelMoveOpening(PreparedPixelMoveOpening&&) noexcept = default;
    PreparedPixelMoveOpening& operator=(PreparedPixelMoveOpening&&) = delete;
    [[nodiscard]] const JobToken& Token() const noexcept
    {
        return _input._token;
    }
    [[nodiscard]] const std::shared_ptr<const PixelMoveOpening>& Lift() const noexcept
    {
        return _lift;
    }
    [[nodiscard]] std::uint64_t Opening() const noexcept
    {
        return _input._opening;
    }

  private:
    friend class DocumentSession;
    explicit PreparedPixelMoveOpening(PixelMoveOpeningInput input) noexcept : _input(std::move(input)) {}
    PixelMoveOpeningInput _input;
    std::shared_ptr<const SharedPublicationMemory> _memory;
    std::shared_ptr<const PixelMoveOpening> _lift;
    std::optional<PixelMove> _pending;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct PreparedPixelMoveOpeningResult final
{
    PixelMoveLiftStatus status = PixelMoveLiftStatus::Invalid;
    std::optional<PreparedPixelMoveOpening> prepared;
};
#pragma warning(pop)

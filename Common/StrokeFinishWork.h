#pragma once

#include "BrushStroke.h"
#include "ImageImportHistory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

class WarpFinishInput;
class WarpStroke;

enum class StrokeGestureKind : std::uint8_t
{
    Brush,
    Warp
};

enum class StrokeFinishStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled
};

enum class StrokeFinishStage : std::uint8_t
{
    Heal,
    Snapshot,
    Mask,
    Document,
    History
};

struct StrokeFinishObserver final
{
    void (*checkpoint)(void*, StrokeFinishStage) = nullptr;
    void* context = nullptr;
};

// Capture only after the original job's complete peak and retirement reservation is admitted. The owner
// transfers one exclusive CPU draft, never a live DocumentSession or graphics engine, to that same job.
// The returned engine borrow stays on the owner until all final GPU copy sources have been retained.
class StrokeFinishInput final
{
  public:
    StrokeFinishInput(const StrokeFinishInput&) = delete;
    StrokeFinishInput& operator=(const StrokeFinishInput&) = delete;
    StrokeFinishInput(StrokeFinishInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)), _active(input._active),
          _history(std::move(input._history)), _stroke(std::move(input._stroke)), _opening(input._opening),
          _healSeed(input._healSeed), _kind(input._kind), _warp(std::move(input._warp)),
          _valid(std::exchange(input._valid, false))
    {
    }
    StrokeFinishInput& operator=(StrokeFinishInput&&) = delete;

    [[nodiscard]] StrokeGestureKind GestureKind() const noexcept
    {
        return _kind;
    }
    // Exclusive worker borrow. FinalCoverage's buffer pointers remain borrowed from this owned draft.
    [[nodiscard]] BrushStroke& DraftForWork() noexcept
    {
        return *_stroke;
    }

  private:
    friend class DocumentSession;
    friend class PreparedStrokeFinish;
    friend class WarpFinishInput;
    StrokeFinishInput(ImageImportAuthority authority, CanvasDocument source, std::optional<Guid> active,
                      DocumentHistory::StateView history, std::shared_ptr<BrushStroke> stroke, std::uint64_t opening,
                      std::uint32_t healSeed) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _history(std::move(history)),
          _stroke(std::move(stroke)), _opening(opening), _healSeed(healSeed)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    std::optional<Guid> _active;
    DocumentHistory::StateView _history;
    std::shared_ptr<BrushStroke> _stroke;
    std::uint64_t _opening;
    std::uint32_t _healSeed;
    StrokeGestureKind _kind = StrokeGestureKind::Brush;
    std::shared_ptr<WarpStroke> _warp; // complete accepted path/source remains charged through final retirement
    bool _valid = true;
};

// Only the worker factory can bind a final document, exact opening and history publication. Successful owner
// publication swaps complete retired document/display/history/preview owners into this same charged aggregate.
class PreparedStrokeFinish final
{
  public:
    PreparedStrokeFinish(const PreparedStrokeFinish&) = delete;
    PreparedStrokeFinish& operator=(const PreparedStrokeFinish&) = delete;
    PreparedStrokeFinish(PreparedStrokeFinish&&) noexcept = default;
    PreparedStrokeFinish& operator=(PreparedStrokeFinish&&) = delete;
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});
    // Lifetime-bound immutable preparation views; these grant no publication authority. Null after apply/move.
    [[nodiscard]] const CanvasDocument* FinalDocument() const noexcept
    {
        return !_applied && _input._valid && _document && _commit ? &*_document : nullptr;
    }
    [[nodiscard]] const BrushCommitInfo* CommitInfo() const noexcept
    {
        return !_applied && _input._valid && _document && _commit ? &*_commit : nullptr;
    }

  private:
    friend class DocumentSession;
    PreparedStrokeFinish(StrokeFinishInput input, CanvasDocument document, DocumentHistory::Publication history,
                         BrushCommitInfo commit) noexcept
        : _input(std::move(input)), _document(std::move(document)), _history(std::move(history)), _commit(commit)
    {
    }
    StrokeFinishInput _input;
    std::optional<CanvasDocument> _document;
    DocumentHistory::Publication _history;
    std::optional<CanvasDocument> _retiredDisplay;
    Raster _retiredWarpSource; // swapped from session cache; dies inside the original charged worker aggregate
    std::optional<BrushCommitInfo> _commit;
    bool _applied = false;
};

struct StrokeFinishResult final
{
    StrokeFinishStatus status = StrokeFinishStatus::Invalid;
    std::optional<PreparedStrokeFinish> publication;
};

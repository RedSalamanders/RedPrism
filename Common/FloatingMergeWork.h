#pragma once

#include "FloatingOpeningWork.h"

#include <memory>
#include <optional>
#include <utility>

enum class FloatingMergeStage : std::uint8_t
{
    Pixels,
    History,
    Publication
};
struct FloatingMergeObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, FloatingMergeStage) = nullptr;
};

class FloatingMergeInput final
{
  public:
    FloatingMergeInput(const FloatingMergeInput&) = delete;
    FloatingMergeInput& operator=(const FloatingMergeInput&) = delete;
    FloatingMergeInput(FloatingMergeInput&& source) noexcept
        : _authority(std::move(source._authority)), _source(std::move(source._source)),
          _history(std::move(source._history)), _edit(std::move(source._edit)), _restore(std::move(source._restore)),
          _revision(source._revision), _valid(std::exchange(source._valid, false))
    {
    }
    FloatingMergeInput& operator=(FloatingMergeInput&&) = delete;

  private:
    friend class DocumentSession;
    FloatingMergeInput(ImageImportAuthority authority, CanvasDocument source, DocumentHistory::StateView history,
                       TransformEdit edit, std::shared_ptr<PreparedFloatingRestore> restore,
                       std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _edit(std::move(edit)), _restore(std::move(restore)), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    DocumentHistory::StateView _history;
    TransformEdit _edit;
    std::shared_ptr<PreparedFloatingRestore> _restore;
    std::uint64_t _revision;
    bool _valid = true;
};

// A refused/cancelled merge also returns a sealed resolution. It can restore only the captured draft;
// a newer draft is never discarded by an older worker failure. Explicit Escape uses the separate prebuilt
// restoration, after acknowledgement. All replaced CPU owners remain in this object for worker retirement.
class PreparedFloatingMerge final
{
  public:
    PreparedFloatingMerge(const PreparedFloatingMerge&) = delete;
    PreparedFloatingMerge& operator=(const PreparedFloatingMerge&) = delete;
    PreparedFloatingMerge(PreparedFloatingMerge&&) noexcept = default;
    PreparedFloatingMerge& operator=(PreparedFloatingMerge&&) = delete;

  private:
    friend class DocumentSession;
    PreparedFloatingMerge(ImageImportAuthority authority, CanvasDocument source, DocumentHistory::StateView history,
                          TransformEdit edit, std::shared_ptr<PreparedFloatingRestore> restore,
                          std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _edit(std::move(edit)), _restore(std::move(restore)), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    std::optional<CanvasDocument> _source;
    DocumentHistory::StateView _history;
    TransformEdit _edit;
    std::shared_ptr<PreparedFloatingRestore> _restore;
    std::optional<PreparedImageImport> _publication;
    std::optional<TransformEdit> _retiredEdit;
    SnapGuides _retiredGuides;
    std::uint64_t _revision;
    bool _budgetRefused = false, _consumed = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct FloatingMergeResult final
{
    FloatingPixelsStatus status = FloatingPixelsStatus::Invalid;
    std::optional<PreparedFloatingMerge> publication;
};
#pragma warning(pop)

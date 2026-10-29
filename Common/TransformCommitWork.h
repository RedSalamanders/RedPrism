#pragma once

#include "ImageImportHistory.h"
#include "TransformPixelsWork.h"

#include <optional>
#include <utility>

class TransformCommitInput final
{
  public:
    TransformCommitInput(const TransformCommitInput&) = delete;
    TransformCommitInput& operator=(const TransformCommitInput&) = delete;
    TransformCommitInput(TransformCommitInput&& source) noexcept
        : _authority(std::move(source._authority)), _source(std::move(source._source)), _active(source._active),
          _history(std::move(source._history)), _edit(std::move(source._edit)), _selected(std::move(source._selected)),
          _duplicate(source._duplicate), _revision(source._revision), _valid(std::exchange(source._valid, false))
    {
    }
    TransformCommitInput& operator=(TransformCommitInput&&) = delete;

  private:
    friend class DocumentSession;
    TransformCommitInput(ImageImportAuthority authority, CanvasDocument source, std::optional<Guid> active,
                         DocumentHistory::StateView history, TransformEdit edit, std::unordered_set<Guid> selected,
                         std::optional<std::pair<Guid, Guid>> duplicate, std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _history(std::move(history)),
          _edit(std::move(edit)), _selected(std::move(selected)), _duplicate(duplicate), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    std::optional<Guid> _active;
    DocumentHistory::StateView _history;
    TransformEdit _edit;
    std::unordered_set<Guid> _selected;
    std::optional<std::pair<Guid, Guid>> _duplicate;
    std::uint64_t _revision = 0;
    bool _valid = true;
};

class PreparedTransformCommit final
{
  public:
    PreparedTransformCommit(const PreparedTransformCommit&) = delete;
    PreparedTransformCommit& operator=(const PreparedTransformCommit&) = delete;
    PreparedTransformCommit(PreparedTransformCommit&&) noexcept = default;
    PreparedTransformCommit& operator=(PreparedTransformCommit&&) = delete;

  private:
    friend class DocumentSession;
    PreparedTransformCommit(PreparedImageImport publication, std::optional<std::pair<Guid, Guid>> duplicate,
                            std::uint64_t revision, bool refused) noexcept
        : _publication(std::move(publication)), _duplicate(duplicate), _revision(revision), _refused(refused)
    {
    }
    PreparedImageImport _publication;
    std::optional<std::pair<Guid, Guid>> _duplicate;
    std::optional<TransformEdit> _retiredEdit;
    std::optional<std::pair<Guid, Guid>> _retiredDuplicate;
    SnapGuides _retiredGuides;
    std::uint64_t _revision;
    bool _refused;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct TransformCommitResult final
{
    TransformPixelsStatus status = TransformPixelsStatus::Invalid;
    std::optional<PreparedTransformCommit> publication;
};
#pragma warning(pop)

#pragma once

#include "ImageImportHistory.h"

#include <memory>
#include <optional>
#include <utility>

enum class DuplicateOpeningStatus : std::uint8_t
{
    Ready,
    Invalid,
    TooLarge,
    Cancelled
};
enum class DuplicateOpeningStage : std::uint8_t
{
    Layer,
    Document,
    History,
    Restoration,
    Publication
};
struct DuplicateOpeningObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, DuplicateOpeningStage) = nullptr;
};

class DuplicateOpeningInput final
{
  public:
    DuplicateOpeningInput(const DuplicateOpeningInput&) = delete;
    DuplicateOpeningInput& operator=(const DuplicateOpeningInput&) = delete;
    DuplicateOpeningInput(DuplicateOpeningInput&& other) noexcept
        : _authority(std::move(other._authority)), _source(std::move(other._source)), _active(other._active),
          _history(std::move(other._history)), _selected(std::move(other._selected)), _revision(other._revision),
          _valid(std::exchange(other._valid, false))
    {
    }
    DuplicateOpeningInput& operator=(DuplicateOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    DuplicateOpeningInput(ImageImportAuthority authority, CanvasDocument source, Guid active,
                          DocumentHistory::StateView history, std::unordered_set<Guid> selected,
                          std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _history(std::move(history)),
          _selected(std::move(selected)), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    Guid _active;
    DocumentHistory::StateView _history;
    std::unordered_set<Guid> _selected;
    std::uint64_t _revision;
    bool _valid = true;
};

// Complete rollback is prepared before exposing the duplicate. Draft-only changes can use it without
// recopying the document or constructing history after an allocation/cancellation failure.
class PreparedDuplicateRestore final
{
  public:
    PreparedDuplicateRestore(const PreparedDuplicateRestore&) = delete;
    PreparedDuplicateRestore& operator=(const PreparedDuplicateRestore&) = delete;
    PreparedDuplicateRestore(PreparedDuplicateRestore&&) noexcept = default;
    PreparedDuplicateRestore& operator=(PreparedDuplicateRestore&&) = delete;

  private:
    friend class DocumentSession;
    PreparedDuplicateRestore(PreparedImageImport publication, std::pair<Guid, Guid> duplicate) noexcept
        : _publication(std::move(publication)), _duplicate(duplicate)
    {
    }
    PreparedImageImport _publication;
    std::pair<Guid, Guid> _duplicate;
    std::optional<TransformEdit> _retiredEdit;
    std::optional<std::pair<Guid, Guid>> _retiredDuplicate;
    SnapGuides _retiredGuides;
};

class PreparedDuplicateOpening final
{
  public:
    PreparedDuplicateOpening(const PreparedDuplicateOpening&) = delete;
    PreparedDuplicateOpening& operator=(const PreparedDuplicateOpening&) = delete;
    PreparedDuplicateOpening(PreparedDuplicateOpening&&) noexcept = default;
    PreparedDuplicateOpening& operator=(PreparedDuplicateOpening&&) = delete;
    [[nodiscard]] const std::shared_ptr<PreparedDuplicateRestore>& Restoration() const noexcept
    {
        return _restore;
    }

  private:
    friend class DocumentSession;
    PreparedDuplicateOpening(PreparedImageImport publication, TransformEdit edit, std::pair<Guid, Guid> duplicate,
                             std::shared_ptr<PreparedDuplicateRestore> restore, std::uint64_t revision) noexcept
        : _publication(std::move(publication)), _edit(std::move(edit)), _duplicate(duplicate),
          _restore(std::move(restore)), _revision(revision)
    {
    }
    PreparedImageImport _publication;
    std::optional<TransformEdit> _edit;
    std::optional<std::pair<Guid, Guid>> _duplicate;
    std::shared_ptr<PreparedDuplicateRestore> _restore;
    SnapGuides _retiredGuides;
    std::uint64_t _revision;
};
struct DuplicateOpeningResult final
{
    DuplicateOpeningStatus status = DuplicateOpeningStatus::Invalid;
    std::optional<PreparedDuplicateOpening> publication;
};

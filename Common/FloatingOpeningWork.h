#pragma once

#include "FloatingPixelsWork.h"
#include "ImageImportHistory.h"

#include <memory>
#include <optional>
#include <utility>

enum class FloatingOpeningStage : std::uint8_t
{
    Pixels,
    History,
    RestorationDocument,
    RestorationOwner,
    Publication
};
struct FloatingOpeningObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, FloatingOpeningStage) = nullptr;
};

class FloatingOpeningInput final
{
  public:
    FloatingOpeningInput(const FloatingOpeningInput&) = delete;
    FloatingOpeningInput& operator=(const FloatingOpeningInput&) = delete;
    FloatingOpeningInput(FloatingOpeningInput&& source) noexcept
        : _authority(std::move(source._authority)), _source(std::move(source._source)), _active(source._active),
          _history(std::move(source._history)), _selected(std::move(source._selected)), _revision(source._revision),
          _valid(std::exchange(source._valid, false))
    {
    }
    FloatingOpeningInput& operator=(FloatingOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    FloatingOpeningInput(ImageImportAuthority authority, CanvasDocument source, Guid active,
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

// Built before the floating layer becomes visible. The host retains this CPU owner for the whole edit and
// does not attempt restoration until any merge worker acknowledges cancellation/failure. No failure-path
// allocation or document copy is needed at owner publication; exact predecessor roots guard the history.
class PreparedFloatingRestore final
{
  public:
    PreparedFloatingRestore(const PreparedFloatingRestore&) = delete;
    PreparedFloatingRestore& operator=(const PreparedFloatingRestore&) = delete;
    PreparedFloatingRestore(PreparedFloatingRestore&&) noexcept = default;
    PreparedFloatingRestore& operator=(PreparedFloatingRestore&&) = delete;

  private:
    friend class DocumentSession;
    PreparedFloatingRestore(PreparedImageImport publication, Guid floating,
                            std::shared_ptr<CanvasDocument> before) noexcept
        : _publication(std::move(publication)), _floating(floating), _before(std::move(before))
    {
    }
    PreparedImageImport _publication;
    Guid _floating;
    std::shared_ptr<CanvasDocument> _before;
    std::optional<TransformEdit> _retiredEdit;
    SnapGuides _retiredGuides;
};

class PreparedFloatingOpening final
{
  public:
    PreparedFloatingOpening(const PreparedFloatingOpening&) = delete;
    PreparedFloatingOpening& operator=(const PreparedFloatingOpening&) = delete;
    PreparedFloatingOpening(PreparedFloatingOpening&&) noexcept = default;
    PreparedFloatingOpening& operator=(PreparedFloatingOpening&&) = delete;
    [[nodiscard]] const std::shared_ptr<PreparedFloatingRestore>& Restoration() const noexcept
    {
        return _restore;
    }

  private:
    friend class DocumentSession;
    PreparedFloatingOpening(PreparedImageImport publication, TransformEdit edit,
                            std::shared_ptr<PreparedFloatingRestore> restore, std::uint64_t revision) noexcept
        : _publication(std::move(publication)), _edit(std::move(edit)), _restore(std::move(restore)),
          _revision(revision)
    {
    }
    PreparedImageImport _publication;
    std::optional<TransformEdit> _edit;
    std::shared_ptr<PreparedFloatingRestore> _restore;
    SnapGuides _retiredGuides;
    std::uint64_t _revision;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct FloatingOpeningResult final
{
    FloatingPixelsStatus status = FloatingPixelsStatus::Invalid;
    std::optional<PreparedFloatingOpening> publication;
};
#pragma warning(pop)

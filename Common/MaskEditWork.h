#pragma once

#include "ImageImportHistory.h"

#include <optional>
#include <unordered_set>
#include <utility>

enum class MaskEditStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled
};

enum class MaskWorkStage : std::uint8_t
{
    Coverage,
    Pixels,
    Thumbnail,
    Document
};

struct MaskWorkObserver final
{
    void (*checkpoint)(void*, MaskWorkStage) = nullptr;
    void* context = nullptr;
};

// Source, selected targets and history are sealed together after complete admission. The worker borrows no session.
class MaskEditInput final
{
  public:
    MaskEditInput(const MaskEditInput&) = delete;
    MaskEditInput& operator=(const MaskEditInput&) = delete;
    MaskEditInput(MaskEditInput&& other) noexcept
        : _authority(std::move(other._authority)), _source(std::move(other._source)), _active(other._active),
          _selected(std::move(other._selected)), _history(std::move(other._history)), _revealing(other._revealing),
          _valid(std::exchange(other._valid, false))
    {
    }
    MaskEditInput& operator=(MaskEditInput&&) = delete;

  private:
    friend class DocumentSession;
    MaskEditInput(ImageImportAuthority authority, CanvasDocument source, Guid active, std::unordered_set<Guid> selected,
                  DocumentHistory::StateView history, bool revealing) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _selected(std::move(selected)),
          _history(std::move(history)), _revealing(revealing)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    Guid _active;
    std::unordered_set<Guid> _selected;
    DocumentHistory::StateView _history;
    bool _revealing;
    bool _valid = true;
};

class PreparedMaskEdit final
{
  public:
    PreparedMaskEdit(const PreparedMaskEdit&) = delete;
    PreparedMaskEdit& operator=(const PreparedMaskEdit&) = delete;
    PreparedMaskEdit(PreparedMaskEdit&&) noexcept = default;
    PreparedMaskEdit& operator=(PreparedMaskEdit&&) = delete;

  private:
    friend class DocumentSession;
    explicit PreparedMaskEdit(PreparedImageImport publication) noexcept : _publication(std::move(publication)) {}
    PreparedImageImport _publication;
};

struct MaskEditResult final
{
    MaskEditStatus status = MaskEditStatus::Invalid;
    std::optional<PreparedMaskEdit> publication;
};

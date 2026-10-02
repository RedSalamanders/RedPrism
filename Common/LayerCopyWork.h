#pragma once

#include "ImageImportHistory.h"

#include <optional>
#include <utility>

enum class LayerCopyStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled
};

enum class LayerCopyStage : std::uint8_t
{
    Pixels,
    Layer,
    Document
};

struct LayerCopyObserver final
{
    void (*checkpoint)(void*, LayerCopyStage) = nullptr;
    void* context = nullptr;
};

// Complete source and history captured only after admission. Private clipboard state is neither captured nor written.
class LayerCopyInput final
{
  public:
    LayerCopyInput(const LayerCopyInput&) = delete;
    LayerCopyInput& operator=(const LayerCopyInput&) = delete;
    LayerCopyInput(LayerCopyInput&& other) noexcept
        : _authority(std::move(other._authority)), _source(std::move(other._source)), _active(other._active),
          _placement(other._placement), _history(std::move(other._history)), _mask(other._mask),
          _valid(std::exchange(other._valid, false))
    {
    }
    LayerCopyInput& operator=(LayerCopyInput&&) = delete;

  private:
    friend class DocumentSession;
    LayerCopyInput(ImageImportAuthority authority, CanvasDocument source, Guid active, LayerTransform placement,
                   DocumentHistory::StateView history, bool mask) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _placement(placement),
          _history(std::move(history)), _mask(mask)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    Guid _active;
    LayerTransform _placement;
    DocumentHistory::StateView _history;
    bool _mask;
    bool _valid = true;
};

class PreparedLayerCopy final
{
  public:
    PreparedLayerCopy(const PreparedLayerCopy&) = delete;
    PreparedLayerCopy& operator=(const PreparedLayerCopy&) = delete;
    PreparedLayerCopy(PreparedLayerCopy&&) noexcept = default;
    PreparedLayerCopy& operator=(PreparedLayerCopy&&) = delete;

  private:
    friend class DocumentSession;
    PreparedLayerCopy(PreparedImageImport publication, std::optional<Guid> expandParent) noexcept
        : _publication(std::move(publication)), _expandParent(expandParent)
    {
    }
    PreparedImageImport _publication;
    std::optional<Guid> _expandParent;
};

struct LayerCopyResult final
{
    LayerCopyStatus status = LayerCopyStatus::Invalid;
    std::optional<PreparedLayerCopy> publication;
};

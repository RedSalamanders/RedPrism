#pragma once

#include "ImageImportHistory.h"

#include <optional>
#include <utility>

class TransformCancellationInput final
{
  public:
    TransformCancellationInput(const TransformCancellationInput&) = delete;
    TransformCancellationInput& operator=(const TransformCancellationInput&) = delete;
    TransformCancellationInput(TransformCancellationInput&& source) noexcept
        : _authority(std::move(source._authority)), _source(std::move(source._source)),
          _history(std::move(source._history)), _duplicate(source._duplicate), _layer(source._layer),
          _revision(source._revision), _valid(std::exchange(source._valid, false))
    {
    }
    TransformCancellationInput& operator=(TransformCancellationInput&&) = delete;

  private:
    friend class DocumentSession;
    TransformCancellationInput(ImageImportAuthority authority, std::optional<CanvasDocument> source,
                               DocumentHistory::StateView history, std::optional<std::pair<Guid, Guid>> duplicate,
                               Guid layer, std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _duplicate(duplicate), _layer(layer), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    std::optional<CanvasDocument> _source;
    DocumentHistory::StateView _history;
    std::optional<std::pair<Guid, Guid>> _duplicate;
    Guid _layer;
    std::uint64_t _revision;
    bool _valid = true;
};

class PreparedTransformCancellation final
{
  public:
    PreparedTransformCancellation(const PreparedTransformCancellation&) = delete;
    PreparedTransformCancellation& operator=(const PreparedTransformCancellation&) = delete;
    PreparedTransformCancellation(PreparedTransformCancellation&&) noexcept = default;
    PreparedTransformCancellation& operator=(PreparedTransformCancellation&&) = delete;

  private:
    friend class DocumentSession;
    PreparedTransformCancellation(ImageImportAuthority authority, DocumentHistory::StateView history,
                                  std::optional<std::pair<Guid, Guid>> duplicate, Guid layer,
                                  std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _history(std::move(history)), _duplicate(duplicate), _layer(layer),
          _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    DocumentHistory::StateView _history;
    std::optional<std::pair<Guid, Guid>> _duplicate;
    Guid _layer;
    std::uint64_t _revision;
    std::optional<PreparedImageImport> _publication;
    std::optional<TransformEdit> _retiredEdit;
    std::optional<std::pair<Guid, Guid>> _retiredDuplicate;
    std::optional<CanvasDocument> _retiredDisplay;
    SnapGuides _retiredGuides;
    bool _consumed = false;
};

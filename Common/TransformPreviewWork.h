#pragma once

#include "DistortWork.h"
#include "ImageImportHistory.h"

#include <optional>
#include <utility>

enum class TransformPreviewStage : std::uint8_t
{
    Snapshot,
    Layer,
    Publication,
};

struct TransformPreviewObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, TransformPreviewStage) = nullptr;
};

struct TransformDisplayResult final
{
    DistortWorkStatus status = DistortWorkStatus::Invalid;
    std::optional<CanvasDocument> document;
};

// Complete private display document. Distortion keeps its transparent bounds and caps both color and masks
// at 1024 pixels on the longest side; affine previews only change placements and share original pixels.
[[nodiscard]] std::uint64_t TransformDisplayMemoryBytes(const CanvasDocument& source,
                                                        const TransformEdit& edit) noexcept;
[[nodiscard]] TransformDisplayResult PrepareTransformDisplay(const CanvasDocument& source, const TransformEdit& edit,
                                                             Executor& executor, CancelToken cancellation = {},
                                                             TransformPreviewObserver observer = {});

class TransformPreviewInput final
{
  public:
    TransformPreviewInput(const TransformPreviewInput&) = delete;
    TransformPreviewInput& operator=(const TransformPreviewInput&) = delete;
    TransformPreviewInput(TransformPreviewInput&& other) noexcept
        : _authority(std::move(other._authority)), _history(std::move(other._history)),
          _source(std::move(other._source)), _edit(std::move(other._edit)), _revision(other._revision),
          _valid(std::exchange(other._valid, false))
    {
    }
    TransformPreviewInput& operator=(TransformPreviewInput&&) = delete;

  private:
    friend class DocumentSession;
    TransformPreviewInput(ImageImportAuthority authority, DocumentHistory::StateView history, CanvasDocument source,
                          TransformEdit edit, std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _history(std::move(history)), _source(std::move(source)),
          _edit(std::move(edit)), _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    DocumentHistory::StateView _history;
    CanvasDocument _source;
    TransformEdit _edit;
    std::uint64_t _revision;
    bool _valid = true;
};

class PreparedTransformPreview final
{
  public:
    PreparedTransformPreview(const PreparedTransformPreview&) = delete;
    PreparedTransformPreview& operator=(const PreparedTransformPreview&) = delete;
    PreparedTransformPreview(PreparedTransformPreview&&) noexcept = default;
    PreparedTransformPreview& operator=(PreparedTransformPreview&&) = delete;

  private:
    friend class DocumentSession;
    PreparedTransformPreview(ImageImportAuthority authority, DocumentHistory::StateView history,
                             std::optional<CanvasDocument> display, std::uint64_t revision) noexcept
        : _authority(std::move(authority)), _history(std::move(history)), _display(std::move(display)),
          _revision(revision)
    {
    }
    ImageImportAuthority _authority;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _display;
    std::uint64_t _revision;
    bool _consumed = false;
};

struct TransformPreviewResult final
{
    DistortWorkStatus status = DistortWorkStatus::Invalid;
    std::optional<PreparedTransformPreview> publication;
};

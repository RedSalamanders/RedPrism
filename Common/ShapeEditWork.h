#pragma once

#include "ImageImportHistory.h"

#include <cstdint>
#include <optional>
#include <utility>

enum class ShapeEditStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled
};

enum class ShapeWorkStage : std::uint8_t
{
    Geometry,
    Coverage,
    Pixels,
    Document
};

struct ShapeWorkObserver final
{
    void (*checkpoint)(void*, ShapeWorkStage) = nullptr;
    void* context = nullptr;
};

// Metadata-only, checked before conversion to integer dimensions. No document or pixel ownership.
[[nodiscard]] ShapeEditStatus InspectShapeDraft(const ShapeDraft& draft, int& width, int& height) noexcept;
[[nodiscard]] std::uint64_t ShapeImageMemoryBytes(ShapeKind kind, int width, int height, double cornerRadius) noexcept;
// Exact existing coverage/rounding; every published pixel is initialized. Curved coverage is serial in its
// established float accumulation order; independent color rows use the supplied worker executor.
[[nodiscard]] Raster ShapeImageForWork(ShapeKind kind, int width, int height, std::uint32_t foregroundArgb,
                                       double cornerRadius, Executor& executor, CancelToken cancellation = {},
                                       ShapeWorkObserver observer = {});

class ShapeEditInput final
{
  public:
    ShapeEditInput(const ShapeEditInput&) = delete;
    ShapeEditInput& operator=(const ShapeEditInput&) = delete;
    ShapeEditInput(ShapeEditInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)), _active(input._active),
          _history(std::move(input._history)), _draft(input._draft), _foreground(input._foreground),
          _valid(std::exchange(input._valid, false))
    {
    }
    ShapeEditInput& operator=(ShapeEditInput&&) = delete;

  private:
    friend class DocumentSession;
    ShapeEditInput(ImageImportAuthority authority, CanvasDocument source, std::optional<Guid> active,
                   DocumentHistory::StateView history, ShapeDraft draft, std::uint32_t foreground) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _history(std::move(history)),
          _draft(draft), _foreground(foreground)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    std::optional<Guid> _active;
    DocumentHistory::StateView _history;
    ShapeDraft _draft;
    std::uint32_t _foreground;
    bool _valid = true;
};

class PreparedShapeEdit final
{
  public:
    PreparedShapeEdit(const PreparedShapeEdit&) = delete;
    PreparedShapeEdit& operator=(const PreparedShapeEdit&) = delete;
    PreparedShapeEdit(PreparedShapeEdit&&) noexcept = default;
    PreparedShapeEdit& operator=(PreparedShapeEdit&&) = delete;

  private:
    friend class DocumentSession;
    PreparedShapeEdit(PreparedImageImport publication, std::optional<Guid> parent) noexcept
        : _publication(std::move(publication)), _parent(parent)
    {
    }
    PreparedImageImport _publication;
    std::optional<Guid> _parent;
};

struct ShapeEditResult final
{
    ShapeEditStatus status = ShapeEditStatus::Invalid;
    std::optional<PreparedShapeEdit> publication;
};

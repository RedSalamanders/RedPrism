#pragma once

#include "BrushStroke.h"
#include "RasterEditWork.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>

struct PixelMovePreviewObserver final
{
    void* context = nullptr;
    void (*progress)(void*, std::size_t completedRows, std::size_t totalRows) noexcept = nullptr;
};

class PreparedPixelMovePreview;

// Worker-only procedural view of the exact immutable lift. The owning opening keeps source, coverage,
// lifted pixels and original accounting alive; mutable stroke/session/device objects are never borrowed.
// Reads write only caller-admitted rows. They do not allocate tile images, snapshots or whole previews.
class PixelMovePreviewReader final
{
  public:
    PixelMovePreviewReader(std::shared_ptr<const PixelMoveOpening> opening, Vec2 offset, bool duplicate,
                           std::thread::id ownerThread) noexcept;
    PixelMovePreviewReader(const PixelMovePreviewReader&) = delete;
    PixelMovePreviewReader& operator=(const PixelMovePreviewReader&) = delete;
    [[nodiscard]] bool IsValidForWork() const noexcept;
    // Region is on the captured brush grid. Padding stays unchanged; cancellation may leave partial private
    // bytes, which the consumer must drop. No shared sparse cache is assembled or awaited.
    [[nodiscard]] bool Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                            CancelToken cancellation = {}, PixelMovePreviewObserver observer = {}) const noexcept;
    // Worker factory only; the caller already owns complete source/metadata and later graphics admission.
    // Zero offsets intentionally have no pending preview. The original lift is retained, never rebuilt.
    [[nodiscard]] static std::shared_ptr<const PreparedPixelMovePreview> PrepareDisplayForWork(
        std::shared_ptr<const PixelMoveOpening> opening, Vec2 offset, bool duplicate, std::thread::id ownerThread,
        std::shared_ptr<const SharedPublicationMemory> memory, CancelToken cancellation = {});

  private:
    std::shared_ptr<const PixelMoveOpening> _opening;
    std::thread::id _owner;
    Vec2 _offset{}, _landing{};
    bool _duplicate = false;
    bool _whole = false;
    bool _valid = false;
};

struct PixelMovePreviewLayout final
{
    Guid target;
    Raster source;
    GridRect sourceRect;
    LayerTransform sourceTransform, paintTransform;
    int width = 0, height = 0, canvasWidth = 0, canvasHeight = 0;
    std::uint64_t sourceOwnershipBytes = 0;
};

// Immutable factory certificate for a complete procedural brush-grid preview. It owns neither a mutable
// stroke/session nor a GPU resource. A recorder separately admits graphics and original-use retirement.
class PreparedPixelMovePreview final
{
    struct ConstructionTag final
    {
    };
    friend class PixelMovePreviewReader;

  public:
    PreparedPixelMovePreview(ConstructionTag, std::shared_ptr<const SharedPublicationMemory> memory,
                             std::shared_ptr<const PixelMoveOpening> opening, PixelMovePreviewLayout layout,
                             Vec2 offset, bool duplicate, std::thread::id ownerThread) noexcept
        : _memory(std::move(memory)), _reader(std::move(opening), offset, duplicate, ownerThread),
          _layout(std::move(layout))
    {
    }
    PreparedPixelMovePreview(const PreparedPixelMovePreview&) = delete;
    PreparedPixelMovePreview& operator=(const PreparedPixelMovePreview&) = delete;
    PreparedPixelMovePreview(PreparedPixelMovePreview&&) = delete;
    PreparedPixelMovePreview& operator=(PreparedPixelMovePreview&&) = delete;
    [[nodiscard]] const PixelMovePreviewLayout& Layout() const noexcept
    {
        return _layout;
    }
    [[nodiscard]] bool IsValidForWork() const noexcept
    {
        return _reader.IsValidForWork();
    }
    [[nodiscard]] bool Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                            CancelToken cancellation = {}) const noexcept
    {
        return _reader.Read(region, output, stride, cancellation);
    }
    [[nodiscard]] const std::shared_ptr<const SharedPublicationMemory>& Memory() const noexcept
    {
        return _memory;
    }

  private:
    // Layout's copied source dies before the reader's original lift/accounting; metadata memory dies last.
    std::shared_ptr<const SharedPublicationMemory> _memory;
    PixelMovePreviewReader _reader;
    PixelMovePreviewLayout _layout;
};

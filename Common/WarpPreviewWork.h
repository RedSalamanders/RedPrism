#pragma once

#include "PlacedRasterWork.h"
#include "WarpStroke.h"

#include <array>
#include <memory>
#include <span>
#include <thread>
#include <vector>

// Immutable document-grid bytes represented by the placed original and complete touched replacements.
// Holds no mutable stroke, session, UI or GPU owner. This value grants no document/publication authority.
struct PreparedWarpPreview final
{
    std::shared_ptr<const SharedPublicationMemory> memory; // dies after every retained CPU owner
    std::thread::id ownerThread;
    WarpSourceView source;
    std::vector<RasterTile> tiles; // complete row-major 256-square grid, clipped at document edges
    std::uint64_t ownershipBytes = 0;

  private:
    friend class WarpPreviewReader;
    friend std::shared_ptr<const PreparedWarpPreview> PrepareWarpPreviewForWork(
        WarpStroke&, std::thread::id, CancelToken, std::shared_ptr<const SharedPublicationMemory>);
    std::vector<std::int32_t> _indices; // -1 means unchanged source; producer-built, fixed grid, no reader hash
    int _columns = 0;
    bool _complete = false; // only the complete worker factory can open the reader
};

// Additional metadata/container allowance only; original parent separately admits captured document/history,
// draft pixels, old/new tile publication, recorder transport and all CPU/GPU/frame retirement overlap.
[[nodiscard]] std::uint64_t WarpPreviewMetadataMemoryBytes(int width, int height) noexcept;
[[nodiscard]] std::shared_ptr<const PreparedWarpPreview> PrepareWarpPreviewForWork(
    WarpStroke& stroke, std::thread::id ownerThread, CancelToken cancellation,
    std::shared_ptr<const SharedPublicationMemory> memory);

// Exclusive worker reader of an immutable complete preview. The source cache is bounded; no full render,
// Raster::Data on an unmaterialized sparse source, source mutation, or retained mutable producer borrow.
class WarpPreviewReader final
{
  public:
    explicit WarpPreviewReader(std::shared_ptr<const PreparedWarpPreview> preview) noexcept;
    WarpPreviewReader(const WarpPreviewReader&) = delete;
    WarpPreviewReader& operator=(const WarpPreviewReader&) = delete;
    [[nodiscard]] bool IsValidForWork() const noexcept
    {
        return CanRead({});
    }
    [[nodiscard]] bool Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                            CancelToken cancellation = {}) noexcept;
    // Same unrounded bilinear document-grid sample as BrushStroke's SampleAt, after placed-source byte
    // rounding. False outside the document; valid transparent document pixels remain successful samples.
    [[nodiscard]] bool Sample(Vec2 point, std::array<double, 4>& output, CancelToken cancellation = {}) noexcept;
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;

  private:
    [[nodiscard]] bool CanRead(CancelToken cancellation) const noexcept;
    std::shared_ptr<const PreparedWarpPreview> _preview;
    PlacedRasterReader _placed;
};

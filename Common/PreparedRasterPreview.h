#pragma once

#include "BrushStroke.h"
#include "Jobs.h"

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class SharedPublicationMemory;

// Worker-built immutable image/mask preview metadata. It holds no brush, engine, session or GPU owner.
// Caller admits complete source/output/container overlap before building; consumers separately admit graphics.
// Publication and replacement require exact tool opening, source authority and producer acknowledgement.
struct PreparedRasterPreview final
{
    // Bound by the original successful callback before publication. Members below retire before this owner.
    std::shared_ptr<const SharedPublicationMemory> memory;
    Guid target;
    bool mask = false;
    int width = 0;
    int height = 0;
    LayerTransform transform;
    std::uint8_t fill = 0;
    Raster source;
    GridRect sourceRect;
    std::vector<RasterTile> tiles; // All touched images, not a destructive TakeChanged delta.
    std::uint64_t ownershipBytes = 0;
};

// Additional construction/container/control-block allowance; source/output pixels belong to the complete
// raster-edit admission. Width/height describe the actual brush grid, including grown inverse placement.
// Use this before retaining source owners; zero refuses invalid geometry or arithmetic overflow.
[[nodiscard]] std::uint64_t RasterPreviewMetadataMemoryBytes(int width, int height) noexcept;

// Exclusively borrowed completed CPU raster edit. Refuse the known owner before traversing any tile.
// No PaintSnapshot/materialization, no consumption/mutation of the stroke's changed list, no document clone.
// Allocation failure belongs to the normal worker runner. Refusal/cancellation returns an empty handle.
[[nodiscard]] std::shared_ptr<const PreparedRasterPreview> PrepareRasterPreviewForWork(
    const BrushStroke& stroke, std::thread::id ownerThread, CancelToken cancellation = {},
    std::shared_ptr<const SharedPublicationMemory> memory = {});

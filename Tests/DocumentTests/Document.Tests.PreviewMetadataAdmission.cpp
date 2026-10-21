#include "Document.Tests.PreviewMetadataAdmission.h"

#include "PreparedRasterPreview.h"

#include <cstdint>
#include <cstdio>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s previewmetadata.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}
} // namespace

int RunPreviewMetadataAdmissionTests()
{
    constexpr std::uint64_t kTinyAndOneCellBytes = 1'048'656;
    constexpr std::uint64_t kTwoCellsBytes = 1'048'736;
    constexpr std::uint64_t kFourCellsBytes = 1'048'896;
    constexpr std::uint64_t kMaximumGridBytes = 1'220'703'126'048'576;

    static_assert(sizeof(RasterTile) == 40);

    gFailures = 0;
    Expect(RasterPreviewMetadataMemoryBytes(1, 1) == kTinyAndOneCellBytes, "tiny-grid-one-cell-envelope");
    Expect(RasterPreviewMetadataMemoryBytes(256, 256) == kTinyAndOneCellBytes, "exact-tile-boundary-remains-one-cell");
    Expect(RasterPreviewMetadataMemoryBytes(257, 256) == kTwoCellsBytes, "crossing-width-adds-one-cell");
    Expect(RasterPreviewMetadataMemoryBytes(256, 257) == kTwoCellsBytes, "crossing-height-adds-one-cell");
    Expect(RasterPreviewMetadataMemoryBytes(257, 257) == kFourCellsBytes, "crossing-both-axes-adds-four-cells");
    Expect(RasterPreviewMetadataMemoryBytes(BrushStroke::kMaxGridSide, BrushStroke::kMaxGridSide) == kMaximumGridBytes,
           "maximum-legal-grid-stays-within-uint64-envelope");
    Expect(RasterPreviewMetadataMemoryBytes(0, 1) == 0 && RasterPreviewMetadataMemoryBytes(1, 0) == 0 &&
               RasterPreviewMetadataMemoryBytes(-1, 1) == 0 && RasterPreviewMetadataMemoryBytes(1, -1) == 0,
           "zero-and-negative-sides-refused");
    Expect(RasterPreviewMetadataMemoryBytes(BrushStroke::kMaxGridSide + 1, 1) == 0 &&
               RasterPreviewMetadataMemoryBytes(1, BrushStroke::kMaxGridSide + 1) == 0,
           "sides-outside-legal-grid-refused");
    return gFailures;
}

#pragma once

#include "Document.h"

// Dispatcher-only description of an already prepared pending source. These are borrowed fields, not a cache
// or admission. Revalidate the complete authority/key immediately before capturing all owners under admission.
// The original producer remains distinct from the later graphics user and retains its own retirement charge.
struct PendingDisplaySource final
{
    enum class Kind : std::uint8_t
    {
        Transform,
        Color,
        Adjustment,
        Gradient,
        PixelMove
    };
    DocumentReadAuthority authority;
    Kind kind = Kind::Transform;
    std::uint64_t opening = 0;
    std::uint64_t publication = 0;
    const CanvasDocument* document = nullptr;
    const void* sourceIdentity = nullptr; // Only a comparison while the eventual graph holds the source root.
    Guid target;
    const Raster* adjustmentSource = nullptr;
    const Raster* adjustmentCoverage = nullptr;
    const std::shared_ptr<const AdjustmentLookup>* lookup = nullptr;
    const LayerAdjustment* original = nullptr;
    double previewScale = 1;
    const std::shared_ptr<const PreparedRasterPreview>* rasterPreview = nullptr;
    const std::shared_ptr<const PixelMoveOpening>* pixelOpening = nullptr;
    const std::shared_ptr<const SharedPublicationMemory>* pixelMemory = nullptr;
    Vec2 pixelOffset{};
    bool pixelDuplicate = false;
    int pixelWidth = 0, pixelHeight = 0;
    std::uint64_t pixelOwnershipBytes = 0;
};

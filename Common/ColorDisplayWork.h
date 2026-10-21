#pragma once

#include "Document.h"
#include "Executor.h"

#include <cstdint>
#include <optional>

// The raster has already been rendered. This primitive prepares display metadata only; it owns no session,
// history, window or GPU object. The host must retain the source and admit the complete job before capture.
struct ColorDisplayLayer final
{
    Guid layerId;
    Raster image;
    LayerTransform transform;
};

struct ColorDisplayMemory final
{
    std::uint64_t additionalBytes = 0;
    std::uint64_t retirementBytes = 0;
    std::uint64_t retirementRasterNodes = 0;
    [[nodiscard]] bool operator==(const ColorDisplayMemory&) const noexcept = default;
};

// Checked envelope arithmetic, exposed independently of a published record root. A bound is an estimate,
// never source authority. Occurrence overcount is intentional; this is not retained-history accounting.
[[nodiscard]] std::optional<ColorDisplayMemory> ColorDisplayMemoryFromBounds(
    const RecordOwnershipBounds& records, std::uint64_t recordCount, std::uint64_t previewBytes,
    std::uint64_t previewNodes, std::uint64_t selectionBytes = 0, std::uint64_t selectionLoops = 0) noexcept;

// Constant-time cached planning. Does not scan records, copy strings or assemble sparse raster bytes.
// This partial declaration excludes rendering, panel settings, old display owners and host publication.
[[nodiscard]] std::optional<ColorDisplayMemory> ColorDisplayMemoryBytes(const CanvasDocument& source,
                                                                        const ColorDisplayLayer& preview) noexcept;

enum class ColorDisplayStage : std::uint8_t
{
    Validation,
    Draft,
    Layer,
    Seal,
    Publication,
};

struct ColorDisplayObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, ColorDisplayStage) = nullptr;
};

enum class ColorDisplayStatus : std::uint8_t
{
    Invalid,
    Cancelled,
    Ready,
};

struct ColorDisplayResult final
{
    ColorDisplayStatus status = ColorDisplayStatus::Invalid;
    std::optional<CanvasDocument> document;
};

// Worker-only construction. Shares immutable pixel/selection sources, changes exactly one image/transform,
// and preserves an implicit mask's original placement when the preview grid moves. No pixel work occurs.
// bad_alloc reaches the ordinary scheduler runner; there is no partial publication on cancellation/failure.
[[nodiscard]] ColorDisplayResult PrepareColorDisplay(const CanvasDocument& source, const ColorDisplayLayer& preview,
                                                     CancelToken cancellation = {}, ColorDisplayObserver observer = {});

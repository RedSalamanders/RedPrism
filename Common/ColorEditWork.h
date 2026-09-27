#pragma once

#include "Document.h"
#include "Executor.h"

// Immutable worker inputs. Every raster and selection handle owns its source; no session, window or graphics
// owner may be reached from RunColorEditJob. The session prepares and applies on its owning thread.
struct ColorEditJobInput final
{
    JobToken token;
    ColorEdit edit;
};

enum class ColorEditJobStatus
{
    Ready,
    Unchanged,
    NoSource,
    Failed,
    Cancelled
};

struct ColorEditJobResult final
{
    JobToken token;
    FilterKind kind = FilterKind::GaussianBlur;
    FilterSettings settings;
    ColorEditJobStatus status = ColorEditJobStatus::Failed;
    Raster image;
    Raster thumbnail;
    LayerTransform transform;
    std::optional<LayerMask> mask;
    bool reusedPreview = false;
};

[[nodiscard]] bool ColorEditChangesNothing(FilterKind kind, const FilterSettings& settings) noexcept;
[[nodiscard]] Raster RenderColorEditRaster(const ColorEdit& edit, const Raster& source, const LayerTransform& placement,
                                           double scale, bool* noSource = nullptr,
                                           const CancelToken& cancellation = {});

// Additional live allocation peak of the complete commit graph, before it is submitted. Includes selection
// coverage, private sparse reads, kernel scratch, blending, trimming, carried masks, thumbnails and metadata.
// Existing immutable inputs are already resident. nullopt refuses an invalid/overflowing request.
[[nodiscard]] std::optional<std::uint64_t> ColorEditJobMemoryBytes(const ColorEditJobInput& input) noexcept;

// Run may throw bad_alloc; the job runner (or the direct inline wrapper) translates it to a failed operation.
// Apply never invokes a pixel kernel. Inner loops observe cancellation; the host supplies memory admission.
[[nodiscard]] ColorEditJobResult RunColorEditJob(const ColorEditJobInput& input, Executor& executor,
                                                 CancelToken cancellation = {});

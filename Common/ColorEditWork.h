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
                                           double scale, bool* noSource = nullptr);

// Run may throw bad_alloc; the job runner (or the direct inline wrapper) translates it to a failed operation.
// Apply never invokes a pixel kernel. Kernel-level cancellation and parallelism are separate P1.1b work.
[[nodiscard]] ColorEditJobResult RunColorEditJob(const ColorEditJobInput& input, Executor& executor,
                                                 CancelToken cancellation = {});

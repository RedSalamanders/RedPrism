#pragma once

#include "Document.h"
#include "Executor.h"

// Immutable worker inputs. Every raster and selection handle owns its source; no session, window or graphics
// owner may be reached from RunColorEditJob. The session prepares and applies on its owning thread.
struct ColorEditJobInput final
{
    JobToken token;
    ColorEdit edit;
    bool lookupPreview = false;
    std::optional<LayerAdjustment> liveAdjustment;
};

// Scalar/shared-handle opening descriptor. Full semantic mask/name data stays in sourceLayers and is
// materialized by the worker before a result can carry it to history. No asset string is copied here.
[[nodiscard]] TargetIdentity DescribeColorEditTarget(const ImageLayer& layer);
[[nodiscard]] const LayerMask* ColorEditSourceMask(const ColorEdit& edit) noexcept;

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

struct ColorEditPreviewResult final
{
    JobToken token;
    ColorEdit edit; // Prepared handles and rendered sequence; apply never replaces the panel's current settings.
    ColorEditJobStatus status = ColorEditJobStatus::Failed;
};

struct ColorEditHistogramResult final
{
    JobToken token;
    std::uint64_t openingSequence = 0;
    Raster original;
    LevelsHistogram histogram{};
    ColorEditJobStatus status = ColorEditJobStatus::Failed;
};

[[nodiscard]] int ColorEditPreviewLimit(FilterKind kind) noexcept;
[[nodiscard]] std::optional<std::uint64_t> ColorEditPreviewMemoryBytes(const ColorEditJobInput& input) noexcept;
// Borrowed planning before any settings/target capture. The legacy input overload delegates here.
[[nodiscard]] std::optional<std::uint64_t> ColorEditPreviewMemoryBytes(const ColorEdit& edit, bool lookupPreview,
                                                                       bool liveAdjustment = false) noexcept;
[[nodiscard]] std::optional<std::uint64_t> ColorEditHistogramMemoryBytes(const ColorEditJobInput& input) noexcept;
[[nodiscard]] std::optional<std::uint64_t> ColorEditHistogramMemoryBytes(const ColorEdit& edit) noexcept;
[[nodiscard]] ColorEditPreviewResult RunColorEditPreviewJob(const ColorEditJobInput& input, Executor& executor,
                                                            CancelToken cancellation = {});
[[nodiscard]] ColorEditHistogramResult RunColorEditHistogramJob(const ColorEditJobInput& input, Executor& executor,
                                                                CancelToken cancellation = {});

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

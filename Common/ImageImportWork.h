#pragma once

#include "Executor.h"
#include "ProjectStore.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Keep these standard-only bounds aligned with HostInputBudget in
// RedPrism/ContinuationRequests.h. The host still performs the full retained-capacity admission.
inline constexpr std::size_t kImageImportMaxPaths = 256;
inline constexpr std::size_t kImageImportMaxPathUnits = 32767;

struct ImageImportBatchInput final
{
    // A value snapshot copied into a private working document by RunImageImportBatch. Rasters remain shared handles.
    std::optional<CanvasDocument> document;
    std::optional<Guid> activeLayerID;
    // Views must remain valid for the duration of RunImageImportBatch. Each path is counted and may not contain NUL.
    std::span<const std::wstring_view> paths;
    bool newProject = false;
    std::optional<Vec2> center;
    void (*progress)(void*, std::size_t, ProjectIoStatus) noexcept = nullptr;
    void* progressContext = nullptr;
    // Borrowed only for this invocation; forwarded to every decode. The callback must not throw, wait or touch UI.
    ProjectIoObserver observer{};
};

struct ImageImportBatchResult final
{
    ProjectIoStatus status = ProjectIoStatus::Invalid;
    bool failed = false;
    bool tooLarge = false;
    std::size_t attemptedPaths = 0;

    // When a new canvas is requested, this is the first successfully imported image and its active layer. The host
    // applies it as the document-replacement/saved-baseline boundary before considering additional imports.
    std::optional<CanvasDocument> createdDocument;
    std::optional<Guid> createdActiveLayerID;

    // Existing-document imports and additional imports into a new canvas share this result. It is present only if
    // at least one import beyond the creation baseline succeeded (or an existing document changed).
    std::optional<CanvasDocument> importedDocument;
    std::optional<Guid> importedActiveLayerID;
    std::wstring historyName;
};

// CPU and codec work only. This helper never reaches DocumentSession, Application, UI or a GPU owner.
// Allocation failures propagate to the enclosing job runner's explicit bad_alloc boundary.
[[nodiscard]] ImageImportBatchResult RunImageImportBatch(const ImageImportBatchInput& input,
                                                         CancelToken cancellation = {});

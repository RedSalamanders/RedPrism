#pragma once

#include "BoundedClipper/BoundedSelectionBoolean.h"
#include "BoundedLassoGesture.h"

#include <cstdint>
#include <memory>
#include <optional>

// Terminal antialiased is captured from the session at Finish, preserving ApplySelection behavior.
// The opening metadata value alone is not that authority.
// Worker geometry only: this envelope excludes document/history, host input and prepared display.
// The caller admits those separately before capturing a terminal snapshot. No session authority is
// manufactured here and no result mutates a document. Original arena accounting is independently retained.
struct LassoFinishTerminal final
{
    BoundedLassoGesture::Display display;
};

namespace LassoFinishGeometry
{
enum class Disposition : std::uint8_t
{
    Keep,
    Clear,
    Replace
};
enum class Stage : std::uint8_t
{
    BeforeCopy,
    ChunkCopied,
    BeforeClip,
    Clipped,
    BeforeMode,
    BeforeAdopt,
    Prepared
};
struct Observer final
{
    void* context = nullptr;
    void (*checkpoint)(void*, Stage) noexcept = nullptr;
    BoundedSelectionBoolean::Limits limits;
};
struct MemoryPlan final
{
    std::uint64_t retainedArenaBytes = 0;
    std::uint64_t traversalBytes = 0;
    std::uint64_t outlineBytes = 0;
    std::uint64_t workspaceBytes = 0;
    std::uint64_t outputBytes = 0;
    std::uint64_t preparationBytes = 0;
};
struct Result final
{
    Disposition disposition = Disposition::Keep;
    // Replace may carry a present empty selection, which is distinct from Clear (null selection).
    std::optional<DocumentSelection> selection;
    LassoKind kind = LassoKind::Freehand;
};
// Rectangle/New has at most four input corners. These fixed ceilings apply to its worker clip as well
// as its complete Down-time admission. Source selection is retained/accounted separately, never a clip input.
[[nodiscard]] BoundedSelectionBoolean::Limits NewRectangleLimits() noexcept;
[[nodiscard]] std::optional<MemoryPlan> PlanNewRectangle() noexcept;
// Kind-specific New marquee ceilings. Unsupported lasso kinds refuse; they never inherit default limits.
[[nodiscard]] std::optional<BoundedSelectionBoolean::Limits> NewMarqueeLimits(LassoKind kind) noexcept;
[[nodiscard]] std::optional<MemoryPlan> PlanNewMarquee(LassoKind kind) noexcept;
// Preserving resource ceilings use cached source counts only; no geometry traversal or capture.
// Every allocation/output ceiling is enforced by the worker. Complex geometry may refuse within the
// admitted ceiling; this plan never promises that arbitrary input geometry must fit. New is unchanged.
[[nodiscard]] std::optional<BoundedSelectionBoolean::Limits> MarqueeLimits(LassoKind kind, SelectionMode mode,
                                                                           std::uint64_t sourcePoints = 0,
                                                                           std::uint64_t sourceLoops = 0) noexcept;
[[nodiscard]] std::optional<MemoryPlan> PlanMarquee(LassoKind kind, SelectionMode mode, std::uint64_t sourcePoints = 0,
                                                    std::uint64_t sourceLoops = 0) noexcept;
// Physical Freehand has a finite complete-gesture arena independent of the pending input ring.
// The arena holds 1023 sealed 64-point blocks plus one inline tail: exactly 65,536 vertices.
// These are enforced capacity ceilings; complex paths may exhaust worker scratch/output and fail
// the original gesture. No partial result or larger replacement admission is permitted.
inline constexpr std::uint32_t kFreehandMaximumPoints = 65'536;
inline constexpr std::uint32_t kFreehandArenaBlocks = 1023;
[[nodiscard]] std::optional<BoundedSelectionBoolean::Limits> FreehandLimits(SelectionMode mode,
                                                                            std::uint64_t sourcePoints = 0,
                                                                            std::uint64_t sourceLoops = 0) noexcept;
[[nodiscard]] std::optional<MemoryPlan> PlanFreehand(SelectionMode mode, std::uint64_t sourcePoints = 0,
                                                     std::uint64_t sourceLoops = 0) noexcept;
// Complete immutable Down-time drawing descriptor. Polygonal remains a separate multi-press protocol.
struct DrawingPlan final
{
    LassoKind kind;
    SelectionMode mode;
    std::uint32_t arenaBlocks;
    std::uint32_t maximumPoints;
    BoundedSelectionBoolean::Limits geometry;
    MemoryPlan finish;
};
[[nodiscard]] std::optional<DrawingPlan> PlanDrawing(LassoKind kind, SelectionMode mode, std::uint64_t sourcePoints = 0,
                                                     std::uint64_t sourceLoops = 0) noexcept;
[[nodiscard]] std::optional<MemoryPlan> Plan(const BoundedLassoGesture::Display& terminal,
                                             const std::optional<DocumentSelection>& before,
                                             BoundedSelectionBoolean::Limits limits = {}) noexcept;
// Allocations/engine refusal reach the scheduler's original bad_alloc boundary. A null result is
// invalid input or stopped work; never publish a partial shape or use a synchronous fallback.
[[nodiscard]] std::optional<Result> Prepare(const BoundedLassoGesture::Display& terminal, int width, int height,
                                            const std::optional<DocumentSelection>& before,
                                            CancelToken cancellation = {}, const Observer* observer = nullptr,
                                            std::shared_ptr<const SharedPublicationMemory> memory = {});
} // namespace LassoFinishGeometry

struct LassoFinishObserver final
{
    LassoFinishGeometry::Observer geometry;
};

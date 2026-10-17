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

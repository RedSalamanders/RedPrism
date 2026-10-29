#pragma once

#include "ImageImportHistory.h"
#include "WarpStroke.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

// Event-time metadata only. Caller admits the full graph before capturing this immutable source.
struct WarpOpeningOptions final
{
    BrushSettings settings;
    SmearMode mode = SmearMode::Liquify;
    Vec2 point;
    std::thread::id ownerThread;
};

class WarpOpeningInput final
{
  public:
    WarpOpeningInput(const WarpOpeningInput&) = delete;
    WarpOpeningInput& operator=(const WarpOpeningInput&) = delete;
    WarpOpeningInput(WarpOpeningInput&&) noexcept = default;
    WarpOpeningInput& operator=(WarpOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedWarpOpening;
    WarpOpeningInput(ImageImportAuthority authority, DocumentHistory::Authority history, CanvasDocument source,
                     Guid target, WarpOpeningOptions options, Raster retiredSource, std::uint64_t opening) noexcept
        : _authority(std::move(authority)), _history(history), _source(std::move(source)), _target(target),
          _options(std::move(options)), _retiredSource(std::move(retiredSource)), _opening(opening)
    {
    }
    ImageImportAuthority _authority;
    DocumentHistory::Authority _history;
    CanvasDocument _source;
    Guid _target;
    WarpOpeningOptions _options;
    Raster _retiredSource; // previous WarpSource survives owner replacement until worker retirement
    std::uint64_t _opening = 0;
};

class PreparedWarpOpening final
{
  public:
    PreparedWarpOpening(const PreparedWarpOpening&) = delete;
    PreparedWarpOpening& operator=(const PreparedWarpOpening&) = delete;
    PreparedWarpOpening(PreparedWarpOpening&&) noexcept = default;
    PreparedWarpOpening& operator=(PreparedWarpOpening&&) = delete;
    // Original placed source, not a baked document-sized image. Before any dab the committed layer itself is
    // the display. Subsequent complete touched tiles overlay this source under the original admission.
    [[nodiscard]] const WarpSourceView& SourceViewForJoinedOwner() const noexcept
    {
        return _sourceView;
    }
    // Exclusive original admitted worker only; no concurrent owner/sample/preview readers.
    [[nodiscard]] WarpStroke* DraftForWork() const noexcept
    {
        return _applied ? nullptr : _draft.get();
    }
    // Borrow only while useful producers have joined, and release on the original retirement path.
    [[nodiscard]] std::shared_ptr<WarpStroke> TakeAppliedDraftForOwner() noexcept;

  private:
    friend class DocumentSession;
    PreparedWarpOpening(WarpOpeningInput input, WarpSourceView source, std::shared_ptr<WarpStroke> draft) noexcept
        : _input(std::move(input)), _sourceView(std::move(source)), _draft(std::move(draft))
    {
    }
    WarpOpeningInput _input;
    WarpSourceView _sourceView;
    std::shared_ptr<WarpStroke> _draft;
    bool _applied = false;
};

enum class WarpOpeningStatus : std::uint8_t
{
    Ready,
    Invalid,
    Cancelled,
    Memory,
};

enum class WarpOpeningStage : std::uint8_t
{
    Source,
    Draft,
    FirstSample,
    Complete,
};

struct WarpOpeningObserver final
{
    void (*checkpoint)(void*, WarpOpeningStage) = nullptr;
    void* context = nullptr;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct WarpOpeningResult final
{
    WarpOpeningStatus status = WarpOpeningStatus::Invalid;
    std::optional<PreparedWarpOpening> prepared;
};
#pragma warning(pop)

// Opening only: cached captured graph, fixed source windows, lazy mutable tile directory, first tip,
// directories and control bookkeeping. Future serial samples/snapshots/final history/display require their
// complete overlapping phase admission before allocation. Overflow/unavailable graphs refuse with zero.
[[nodiscard]] std::uint64_t WarpOpeningMemoryBytes(const CanvasDocument& source, const ImageLayer& target,
                                                   const WarpOpeningOptions& options) noexcept;

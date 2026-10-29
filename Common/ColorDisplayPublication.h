#pragma once

#include "ColorDisplayWork.h"
#include "ColorEditWork.h"
#include "ImageImportHistory.h"

#include <optional>
#include <utility>

// Complete CPU graph, excluding the host's fixed job/payload records. Resident immutable pixels are shared,
// not allocated again. Retirement counts reachable occurrences conservatively, including the prior display
// and preview caches, and stays separate from the reservation for additional allocations. No history ownership.
struct ColorDisplayPreviewMemory final
{
    std::uint64_t additionalBytes = 0;
    std::uint64_t retirementBytes = 0;
};

// Capture only after the complete host reservation is held. Settings are fixed; the full document is a shared
// owner. History authority is scalar: display preparation neither reads nor owns any history stack. A
// layer-scoped commit token remains distinct from complete display-source authority.
class ColorDisplayPreviewInput final
{
  public:
    class ConstructionKey final
    {
        friend class DocumentSession;
        ConstructionKey() = default;
    };
    ColorDisplayPreviewInput(ConstructionKey, ImageImportAuthority authority, DocumentHistory::Authority history,
                             const CanvasDocument& source, ColorEditJobInput&& job) noexcept
        : _authority(std::move(authority)), _history(std::move(history)), _source(source), _job(std::move(job))
    {
    }
    ColorDisplayPreviewInput(const ColorDisplayPreviewInput&) = delete;
    ColorDisplayPreviewInput& operator=(const ColorDisplayPreviewInput&) = delete;
    ColorDisplayPreviewInput(ColorDisplayPreviewInput&& other) noexcept
        : _authority(std::move(other._authority)), _history(std::move(other._history)),
          _source(std::move(other._source)), _job(std::move(other._job)), _valid(std::exchange(other._valid, false))
    {
    }
    ColorDisplayPreviewInput& operator=(ColorDisplayPreviewInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedColorDisplayPreview;
    ImageImportAuthority _authority;
    DocumentHistory::Authority _history;
    CanvasDocument _source;
    ColorEditJobInput _job;
    bool _valid = true;
};

class PreparedColorDisplayPreview final
{
  public:
    class ConstructionKey final
    {
        friend class DocumentSession;
        ConstructionKey() = default;
    };
    PreparedColorDisplayPreview(ConstructionKey, ColorDisplayPreviewInput&& input, ColorEditPreviewResult&& result,
                                std::optional<CanvasDocument>&& display) noexcept
        : _input(std::move(input)), _result(std::move(result)), _display(std::move(display))
    {
    }
    PreparedColorDisplayPreview(ConstructionKey, ColorDisplayPreviewInput&& input, ColorEditJobStatus status) noexcept
        : _input(std::move(input)),
          _result{std::move(_input._job.token), std::move(_input._job.edit),
                  status == ColorEditJobStatus::NoSource ? status : ColorEditJobStatus::Failed}
    {
    }
    PreparedColorDisplayPreview(const PreparedColorDisplayPreview&) = delete;
    PreparedColorDisplayPreview& operator=(const PreparedColorDisplayPreview&) = delete;
    PreparedColorDisplayPreview(PreparedColorDisplayPreview&& other) noexcept
        : _input(std::move(other._input)), _result(std::move(other._result)), _display(std::move(other._display)),
          _retired(std::move(other._retired)), _consumed(std::exchange(other._consumed, true))
    {
    }
    PreparedColorDisplayPreview& operator=(PreparedColorDisplayPreview&&) = delete;
    [[nodiscard]] bool IsConsumed() const noexcept
    {
        return _consumed;
    }
    [[nodiscard]] ColorEditJobStatus Status() const noexcept
    {
        return _result.status;
    }

  private:
    friend class DocumentSession;
    struct RetiredPreview final
    {
        Raster raster, source, coverage, grown, pendingGrown;
        std::shared_ptr<const AdjustmentLookup> lookup;
    };
    ColorDisplayPreviewInput _input;
    ColorEditPreviewResult _result;
    std::optional<CanvasDocument> _display; // Swaps with the complete old display at publication.
    RetiredPreview _retired;
    bool _consumed = false;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct ColorDisplayPreviewResult final
{
    ColorEditJobStatus status = ColorEditJobStatus::Failed;
    std::optional<PreparedColorDisplayPreview> publication;
};
#pragma warning(pop)

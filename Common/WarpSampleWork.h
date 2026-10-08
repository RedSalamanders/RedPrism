#pragma once

#include "ImageImportHistory.h"
#include "WarpStroke.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// Capture only after the original parent has admitted the complete sample/preview overlap. The session
// keeps scalar gesture authority while this input exclusively owns its mutable CPU draft. All useful
// producers and preview recording users must join before owner publication or worker retirement.
class WarpSampleInput final
{
  public:
    WarpSampleInput(const WarpSampleInput&) = delete;
    WarpSampleInput& operator=(const WarpSampleInput&) = delete;
    WarpSampleInput(WarpSampleInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)),
          _history(std::move(input._history)), _draft(std::move(input._draft)), _plan(std::move(input._plan)),
          _target(input._target), _opening(input._opening), _prepared(input._prepared), _started(input._started),
          _valid(std::exchange(input._valid, false))
    {
    }
    WarpSampleInput& operator=(WarpSampleInput&&) = delete;
    [[nodiscard]] bool AppendForWork(CancelToken cancellation, WarpStroke::WorkObserver observer = {});
    // Exclusive worker only, after the accepted append. Metadata/snapshot preparation stays on this worker.
    [[nodiscard]] WarpStroke* DraftForWork() noexcept;
    [[nodiscard]] const CanvasDocument& CapturedDocumentForWork() const noexcept
    {
        return _source;
    }

  private:
    friend class DocumentSession;
    WarpSampleInput(ImageImportAuthority authority, CanvasDocument source, DocumentHistory::StateView history,
                    std::shared_ptr<WarpStroke> draft, WarpStroke::SampleMemoryPlan plan, Guid target,
                    std::uint64_t opening) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _draft(std::move(draft)), _plan(std::move(plan)), _target(target), _opening(opening)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    DocumentHistory::StateView _history;
    std::shared_ptr<WarpStroke> _draft;
    WarpStroke::SampleMemoryPlan _plan;
    Guid _target;
    std::uint64_t _opening = 0;
    bool _prepared = false;
    bool _started = false;
    bool _valid = true;
};

#pragma once

#include "BrushStroke.h"
#include "ImageImportHistory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// Captured after admission, and accessed exclusively by the admitted worker until its actual join.
// This input owns no GPU/context pointer and retains the immutable source and history authority.
class StrokeSampleInput final
{
  public:
    StrokeSampleInput(const StrokeSampleInput&) = delete;
    StrokeSampleInput& operator=(const StrokeSampleInput&) = delete;
    StrokeSampleInput(StrokeSampleInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)),
          _history(std::move(input._history)), _stroke(std::move(input._stroke)), _opening(input._opening),
          _point(input._point), _valid(std::exchange(input._valid, false))
    {
    }
    StrokeSampleInput& operator=(StrokeSampleInput&&) = delete;
    // Borrow only before successful ApplyStrokeSampleIfCurrent or after capture without publication.
    // A successful apply consumes the draft reference; remaining immutable wrappers retire on a worker.
    [[nodiscard]] BrushStroke& DraftForWork() noexcept
    {
        return *_stroke;
    }
    [[nodiscard]] const BrushStroke& JoinedDraft() const noexcept
    {
        return *_stroke;
    }

  private:
    friend class DocumentSession;
    StrokeSampleInput(ImageImportAuthority authority, CanvasDocument source, DocumentHistory::StateView history,
                      std::shared_ptr<BrushStroke> stroke, std::uint64_t opening, Vec2 point) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _stroke(std::move(stroke)), _opening(opening), _point(point)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    DocumentHistory::StateView _history;
    std::shared_ptr<BrushStroke> _stroke;
    std::uint64_t _opening;
    Vec2 _point;
    bool _valid = true;
};

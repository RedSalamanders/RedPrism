#pragma once

#include "StrokeFinishWork.h"
#include "WarpStroke.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>

// Preflight before capture. CPU includes old mutable source/path/history, complete replay, snapshot/cache,
// sparse output and linked-mask overlap. Host adds command transport, staging and display/row admission.
struct WarpFinishMemoryPlan final
{
    std::uint64_t cpuBytes = 0;
    std::uint64_t gpuBytes = 0;
    std::uint64_t maximumReplayTiles = 0;
};

// Exclusive accepted Warp path/source capture after the original parent's complete admission.
// Only scalar Warp finishing authority remains in the session. No graphics/context crosses this boundary.
class WarpFinishInput final
{
  public:
    WarpFinishInput(const WarpFinishInput&) = delete;
    WarpFinishInput& operator=(const WarpFinishInput&) = delete;
    WarpFinishInput(WarpFinishInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)),
          _history(std::move(input._history)), _warp(std::move(input._warp)), _replay(std::move(input._replay)),
          _settings(input._settings), _target(input._target), _opening(input._opening), _pixelLimit(input._pixelLimit),
          _owner(input._owner), _nextPoint(input._nextPoint), _samplePending(input._samplePending),
          _finishStarted(input._finishStarted), _finished(input._finished), _started(input._started),
          _valid(std::exchange(input._valid, false))
    {
    }
    WarpFinishInput& operator=(WarpFinishInput&&) = delete;
    // Creates one private replay under original accounting. Caller supplies exact uint coverage in compute mode.
    [[nodiscard]] const BrushStroke* PrepareReplayForWork(bool computeCoverage, CancelToken cancellation,
                                                          std::shared_ptr<const SharedPublicationMemory> memory);
    [[nodiscard]] std::span<const Vec2> PointsForWork() const noexcept;
    // Only this captured path supplies point values and advances completion authority.
    [[nodiscard]] std::optional<BrushStroke::SampleCoverage> PrepareNextSampleForWork(CancelToken cancellation);
    [[nodiscard]] bool InstallSampleCoverageForWork(std::uint64_t key, std::span<const std::uint32_t> values);
    [[nodiscard]] bool ComposeSampleForWork(const BrushStroke::SampleCoverage& sample);
    [[nodiscard]] std::optional<BrushStroke::FinalCoverage> PrepareFinishForWork(CancelToken cancellation);
    [[nodiscard]] bool InstallFinalCoverageForWork(std::uint64_t key, std::span<const std::uint32_t> values);
    [[nodiscard]] bool ComposeFinishForWork(const BrushStroke::FinalCoverage& final);
    // Final history work consumes only a completed replay; moved/unprepared/cancelled inputs refuse.
    [[nodiscard]] std::optional<StrokeFinishInput> TakeReplayPublicationForWork() noexcept;

  private:
    friend class DocumentSession;
    WarpFinishInput(ImageImportAuthority authority, CanvasDocument source, DocumentHistory::StateView history,
                    std::shared_ptr<WarpStroke> warp, BrushSettings settings, Guid target, std::uint64_t opening,
                    std::int64_t pixelLimit, std::thread::id owner) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _history(std::move(history)),
          _warp(std::move(warp)), _settings(settings), _target(target), _opening(opening), _pixelLimit(pixelLimit),
          _owner(owner)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    DocumentHistory::StateView _history;
    std::shared_ptr<WarpStroke> _warp;
    std::shared_ptr<BrushStroke> _replay;
    BrushSettings _settings;
    Guid _target;
    std::uint64_t _opening = 0;
    std::int64_t _pixelLimit = 0;
    std::thread::id _owner;
    [[nodiscard]] bool CanWork() const noexcept
    {
        return _valid && _replay && _warp && _owner != std::this_thread::get_id();
    }
    std::size_t _nextPoint = 0;
    bool _samplePending = false;
    bool _finishStarted = false;
    bool _finished = false;
    bool _started = false;
    bool _valid = true;
};

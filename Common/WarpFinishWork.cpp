#include "WarpFinishWork.h"

#include "Selection.h"
#include "SharedPublicationMemory.h"
#include "StrokeOpeningWork.h"
#include "WarpPreviewWork.h"

#include <algorithm>
#include <type_traits>

std::optional<WarpFinishMemoryPlan> DocumentSession::PlanWarpFinishMemory(bool computeCoverage) const noexcept
{
    if (!_document || !_warp || _warp.use_count() != 1 || !_warp->IsValid() || _warp->Points().empty() ||
        _warp->WorkOwner() != std::this_thread::get_id() || _warpUpdatingOpening || _warpFinishingOpening ||
        !_activeLayerID || _maskSelected)
        return std::nullopt;
    const auto* target = _document->layers.FindKey(_activeLayerID.value());
    if (!target || target->id != _warpSource.layerID || target->isGroup || target->adjustment ||
        (target->asset ? target->asset->image : Raster{}) != _warpSource.image ||
        target->transform != _warpSource.transform)
        return std::nullopt;
    auto settings = brushSettings;
    settings.diameter = (std::min)(2000.0, _warp->Diameter() + 4.0);
    settings.hardness = settings.opacity = 1;
    const auto pixelLimit = RasterEditPixelLimit(*target, false);
    const auto grid = BrushStroke::PlanGrid(*target, false, settings, _document->width, _document->height);
    const auto replay = StrokeOpeningMemoryBytes(*_document, *target, false, pixelLimit, settings, BrushKind::Paint);
    const auto historyBytes = RasterEditRetirementMemoryBytes();
    const auto warpBytes = _warp->RetirementMemoryBytes();
    const auto snapshot = _warp->SnapshotMemoryBytesForOwner();
    if (!grid || !replay || !historyBytes || !warpBytes || !snapshot)
        return std::nullopt;
    WarpFinishMemoryPlan plan;
    const auto add = [](std::uint64_t& total, std::uint64_t count, std::uint64_t unit = 1) noexcept
    {
        if ((unit && count > UINT64_MAX / unit) || count * unit > UINT64_MAX - total)
            return false;
        total += count * unit;
        return true;
    };
    const auto columns = (static_cast<std::uint64_t>(grid->width) + 255) / 256;
    const auto rows = (static_cast<std::uint64_t>(grid->height) + 255) / 256;
    // Same supported rectangle envelope as StrokeOpeningMemoryBytes, including unaligned crop edges.
    const auto envelope = (static_cast<std::uint64_t>(pixelLimit) + 65535) / 65536 +
                          4ULL * ((static_cast<std::uint64_t>(kMaxCanvasSide) + 255) / 256) + 4;
    plan.maximumReplayTiles = (std::min)(columns * rows, envelope);
    const auto width = static_cast<std::uint64_t>((std::min)(grid->width, kMaxCanvasSide));
    const auto height = static_cast<std::uint64_t>((std::min)(grid->height, kMaxCanvasSide));
    const auto pixels = (std::min)(width * height, static_cast<std::uint64_t>(pixelLimit));
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels * 4));
    const auto mask = target->mask && !target->mask->placement && target->mask->asset.image.PixelCount() > 1
                          ? PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels))
                          : 0;
    // Old history remains readable while a new private topology/index/stack is built. Declare another cached
    // complete history bound; output and new tile metadata are charged separately, with no owner traversal.
    if (!output || !add(plan.cpuBytes, 1, replay) || !add(plan.cpuBytes, 2, historyBytes) ||
        !add(plan.cpuBytes, 1, warpBytes) || !add(plan.cpuBytes, 1, snapshot) || !add(plan.cpuBytes, 1, output) ||
        !add(plan.cpuBytes, 1, 1024 * 1024) ||
        !add(plan.cpuBytes, plan.maximumReplayTiles, 8192 + 40 * sizeof(RasterTile)) ||
        (target->mask && !target->mask->placement && target->mask->asset.image.PixelCount() > 1 &&
         (!mask || !add(plan.cpuBytes, std::uint64_t{1}, mask) ||
          !add(plan.cpuBytes, std::uint64_t{1}, static_cast<std::uint64_t>(target->mask->asset.image.Stride())) ||
          !add(plan.cpuBytes, width, sizeof(int)) || !add(plan.cpuBytes, 2, 96 * 96 + 4096))))
        return std::nullopt;
    // Private density and uint output survive every replay phase. Tail replacement never resets the epoch.
    if (computeCoverage && (!add(plan.gpuBytes, plan.maximumReplayTiles, 65536 * 8 + 4 * 4096) ||
                            !add(plan.gpuBytes, 2048, 4 * sizeof(float)) || !add(plan.gpuBytes, 1, 4 * 4096)))
        return std::nullopt;
    return plan;
}

std::optional<WarpFinishInput> DocumentSession::TakeWarpFinishInput(std::uint64_t opening) noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    if (!_document || !_warp || _warp.use_count() != 1 || !_warp->IsValid() || _warp->Points().empty() ||
        _warpUpdatingOpening || _warpFinishingOpening || opening != _warpOpening || !_activeLayerID || _maskSelected ||
        _warpSource.layerID != _activeLayerID.value() || _warp->WorkOwner() != std::this_thread::get_id() || _brush ||
        _brushUpdatingOpening || _brushFinishingOpening || _transformEdit || _gradient || _pixelMove || _colorEdit ||
        _shapeDraft || _lassoDraft || _selectionMoveOrigin)
        return std::nullopt;
    const auto* target = _document->layers.FindKey(_warpSource.layerID);
    if (!target || target->isGroup || target->adjustment ||
        (target->asset ? target->asset->image : Raster{}) != _warpSource.image ||
        target->transform != _warpSource.transform)
        return std::nullopt;
    const auto limit = RasterEditPixelLimit(*target, false);
    if (limit < 1)
        return std::nullopt;
    WarpFinishInput input(CaptureImageImportAuthority(), _document.value(), history.CaptureState(), std::move(_warp),
                          brushSettings, target->id, opening, limit, std::this_thread::get_id());
    _warpFinishingOpening = opening;
    return input;
}

bool DocumentSession::DiscardWarpFinishIfCurrent(std::uint64_t opening) noexcept
{
    if (_warpFinishingOpening != opening || _warpUpdatingOpening)
        return false;
    _warpSource.image = {}; // detached input still retains this exact source through original worker retirement
    _warpFinishingOpening.reset();
    return true;
}

const BrushStroke* WarpFinishInput::PrepareReplayForWork(bool computeCoverage, CancelToken cancellation,
                                                         std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (!_valid || _started || !_warp || _owner == std::thread::id{} || _owner == std::this_thread::get_id() ||
        !memory || memory->IsBound() || cancellation.IsCancelled())
        return nullptr;
    _started = true; // Refused/failed useful preparation cannot be retried over a private prefix.
    const auto* target = _source.layers.FindKey(_target);
    if (!target || cancellation.IsCancelled())
        return nullptr;
    auto preview = PrepareWarpPreviewForWork(*_warp, _owner, cancellation, std::move(memory));
    if (!preview)
        return nullptr;
    auto settings = _settings;
    settings.diameter = (std::min)(2000.0, _warp->Diameter() + 4.0);
    settings.hardness = 1.0;
    settings.opacity = 1.0;
    auto replay = BrushStroke::MakeForWork(*target, false, settings, _source.width, _source.height);
    if (!replay || !replay->IsValid())
        return nullptr;
    replay->SetPixelLimit(_pixelLimit);
    if (_source.selection)
    {
        const auto& selection = _source.selection.value();
        auto coverage = SelectionGeometry::Coverage(selection.loops, _source.width, _source.height,
                                                    selection.antialiased, cancellation);
        if (!coverage.HasStorage() || cancellation.IsCancelled())
            return nullptr;
        replay->SetSelectionClip(std::move(coverage));
    }
    replay->SetEditName(_warp->Mode() == SmearMode::Liquify ? L"Liquify" : L"Smudge");
    if (!replay->BeginWarpReplayForWork(std::move(preview), computeCoverage, cancellation))
        return nullptr;
    _replay = std::move(replay);
    return _replay.get();
}

std::span<const Vec2> WarpFinishInput::PointsForWork() const noexcept
{
    return _valid && _replay && _warp && _owner != std::this_thread::get_id() ? std::span<const Vec2>(_warp->Points())
                                                                              : std::span<const Vec2>{};
}

std::optional<StrokeFinishInput> WarpFinishInput::TakeReplayPublicationForWork() noexcept
{
    if (!CanWork() || !_finished || _samplePending || _nextPoint != _warp->Points().size() || !_replay->IsValid() ||
        !_replay->IsWarpReplayCompleteForWork())
        return std::nullopt;
    StrokeFinishInput input(std::move(_authority), std::move(_source), _target, std::move(_history), std::move(_replay),
                            _opening, 0);
    input._kind = StrokeGestureKind::Warp;
    input._warp = std::move(_warp);
    _valid = false;
    return input;
}

std::optional<BrushStroke::SampleCoverage> WarpFinishInput::PrepareNextSampleForWork(CancelToken cancellation)
{
    if (!CanWork() || _samplePending || _finishStarted || _nextPoint >= _warp->Points().size())
        return std::nullopt;
    _samplePending = true;
    return _replay->PrepareWarpReplaySampleForWork(_warp->Points()[_nextPoint], cancellation);
}

bool WarpFinishInput::InstallSampleCoverageForWork(std::uint64_t key, std::span<const std::uint32_t> values)
{
    return CanWork() && _samplePending && !_finishStarted && _replay->InstallSampleCoverage(key, values);
}

bool WarpFinishInput::ComposeSampleForWork(const BrushStroke::SampleCoverage& sample)
{
    if (!CanWork() || !_samplePending || _finishStarted || !_replay->ComposeSampleCoverage(sample))
        return false;
    _samplePending = false;
    ++_nextPoint;
    return true;
}

std::optional<BrushStroke::FinalCoverage> WarpFinishInput::PrepareFinishForWork(CancelToken cancellation)
{
    if (!CanWork() || _samplePending || _finishStarted || _nextPoint != _warp->Points().size())
        return std::nullopt;
    _finishStarted = true;
    return _replay->PrepareWarpReplayFinishForWork(cancellation);
}

bool WarpFinishInput::InstallFinalCoverageForWork(std::uint64_t key, std::span<const std::uint32_t> values)
{
    return CanWork() && _finishStarted && !_finished && _replay->InstallFinalCoverage(key, values);
}

bool WarpFinishInput::ComposeFinishForWork(const BrushStroke::FinalCoverage& final)
{
    if (!CanWork() || !_finishStarted || _finished || !_replay->ComposeFinalCoverage(final))
        return false;
    _finished = true;
    return true;
}

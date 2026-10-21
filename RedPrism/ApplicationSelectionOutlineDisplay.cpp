#include "Application.h"
#include "ChromeHost.h"

#include <cmath>
#include <limits>

bool Application::EnableSelectionOutlineForTest(bool allowHidden) noexcept
{
    if (_shutdownStarted || _jobs.InUiCallback() || _selectionOutlineInitialized || !_rendererReady || !_chrome ||
        !_gpuWorkInitialized || _overlaySelection)
        return false;
    // Selection is independently acquirable; the cold queue must not depend on another display route.
    BindCompositionDevice();
    if (FAILED(_gpuBindingError) || !_gpuWork.DeviceGeneration())
        return false;
    // Acquire before any legacy outline is installed; no unadmitted old points/path need owner retirement.
    if (FAILED(_selectionOutlineDisplay.InitializeOwner(_jobs, _gpuWork, _ownerWork, _graphGpuBudget, *this)))
        return false;
    _selectionOutlineInitialized = true;
    _selectionOutlineAllowHiddenForTest = allowHidden;
    _chrome->SetSelectionPaintExternal(true);
    const auto shown = _session.DisplayedSelectionValue();
    SyncPreparedSelectionOutlineOwner(&shown);
    ReleaseSurfacesWhileUnseen();
    return true;
}

void Application::SyncPreparedSelectionOutlineOwner(const SelectionDisplayValue* shown) noexcept
{
    const auto current = shown ? shown->selection : _overlaySelection;
    const Vec2 translation = shown ? shown->translation : _overlaySelectionTranslation;
    const bool hasDocument = std::as_const(_session).Document().has_value();
    _marchingAnts = hasDocument && current && !current->IsEmpty();
    const CanvasHole hole = _chrome->HolePixels();
    const std::array<float, 4> holeKey{hole.left, hole.top, hole.width, hole.height};
    PreparedSelectionOutlineRaster::GridKey grid{};
    bool valid = _marchingAnts && std::isfinite(hole.left) && std::isfinite(hole.top) && std::isfinite(hole.width) &&
                 std::isfinite(hole.height) && hole.left >= 0.0f && hole.top >= 0.0f && hole.width > 0.0f &&
                 hole.height > 0.0f && static_cast<double>(hole.left) + hole.width <= _renderer.Width() &&
                 static_cast<double>(hole.top) + hole.height <= _renderer.Height();
    Vec2 anchor{};
    if (valid)
    {
        const double left = std::floor(static_cast<double>(hole.left));
        const double top = std::floor(static_cast<double>(hole.top));
        const double right = std::ceil(static_cast<double>(hole.left) + hole.width);
        const double bottom = std::ceil(static_cast<double>(hole.top) + hole.height);
        const auto bounds = current->Bounds();
        if (!bounds || right - left > PreparedSelectionOutlineRaster::MaximumSide ||
            bottom - top > PreparedSelectionOutlineRaster::MaximumSide)
            valid = false;
        else
        {
            anchor = {bounds->minX, bounds->minY};
            const Vec2 origin = _viewport.DocumentRectOrigin(DocumentSize());
            grid.width = static_cast<std::uint32_t>(right - left);
            grid.height = static_cast<std::uint32_t>(bottom - top);
            grid.documentScalePixel = _viewport.ViewScale();
            grid.originPixelX = hole.left - left + origin.x + (anchor.x + translation.x) * grid.documentScalePixel;
            grid.originPixelY = hole.top - top + origin.y + (anchor.y + translation.y) * grid.documentScalePixel;
            grid.dpi = ChromeDpi();
            grid.clipped = true;
            grid.clipLeft = hole.left - left;
            grid.clipTop = hole.top - top;
            grid.clipRight = static_cast<double>(hole.left) + hole.width - left;
            grid.clipBottom = static_cast<double>(hole.top) + hole.height - top;
            PreparedSelectionOutlineRaster::MemoryPlan plan;
            valid = SUCCEEDED(PreparedSelectionOutlineRaster::PlanMemory(grid, current->loops.size(),
                                                                         current->loops.PointCount(), plan));
        }
    }
    const auto documentAuthority = _session.CaptureDocumentReadAuthority();
    const bool changed =
        documentAuthority != _selectionOutlineDocument || valid != _selectionOutlineViewValid ||
        grid != _selectionOutlineGrid || holeKey != _selectionOutlineHole ||
        current.has_value() != _overlaySelection.has_value() ||
        (current && (!_overlaySelection || current->loops.Identity() != _overlaySelection->loops.Identity()));
    // Existing immutable source controls defer a final owner-thread release. The controller still retains every
    // originally captured source until its original worker and context retire; this cache never owns raw points.
    _overlaySelection = hasDocument ? current : std::nullopt;
    _overlaySelectionTranslation = translation;
    _overlaySelectionAnchor = anchor;
    _selectionOutlineDocument = documentAuthority;
    _selectionOutlineGrid = grid;
    _selectionOutlineHole = holeKey;
    _selectionOutlineViewValid = valid && !_selectionOutlineEpochExhausted;
    if (changed)
    {
        if (_selectionOutlineViewEpoch == (std::numeric_limits<std::uint64_t>::max)())
        {
            _selectionOutlineEpochExhausted = true;
            _selectionOutlineViewValid = false; // Refuse new display authority; no stale frame may escape.
        }
        else
            ++_selectionOutlineViewEpoch;
        _selectionOutlineDisplay.RequestOwner();
    }
    SyncLassoOverlay(); // Draft conversion is the remaining separate S5 draft path, unchanged here.
}

bool Application::DescribeSelectionOutlineOwner(SelectionOutlineDisplayDescriptor& descriptor) noexcept
{
    descriptor = {};
    if (_shutdownStarted || !_selectionOutlineInitialized || !_selectionOutlineViewValid || !_rendererReady ||
        !_gpuWorkInitialized || FAILED(_gpuBindingError) || !_overlaySelection)
        return false;
    const auto authority = _session.CaptureDocumentReadAuthority();
    if (!authority || !_renderer.Device() || !_renderer.Context())
        return false;
    // Session authority detects a document mutation even before SelectionChanged synchronizes the view cache.
    descriptor.key = {authority.value(), _overlaySelection->loops.Identity(), _selectionOutlineGrid,
                      _gpuWork.DeviceGeneration(), _selectionOutlineViewEpoch};
    descriptor.source = &_overlaySelection->loops;
    descriptor.device = _renderer.Device();
    descriptor.immediate = _renderer.Context();
    descriptor.software = _renderer.IsWarp();
    return true;
}

void Application::SelectionOutlineChangedOwner() noexcept
{
    _selectionOutlineFailure = S_OK;
    InvalidateFrame();
}
void Application::SelectionOutlineFailedOwner(SelectionOutlineDisplayKey, HRESULT failure) noexcept
{
    _selectionOutlineFailure = failure;
    InvalidateFrame();
}

Renderer::SelectionOutlineFrame Application::BorrowSelectionOutlineFrameOwner(float left, float top, float width,
                                                                              float height) noexcept
{
    Renderer::SelectionOutlineFrame result{};
    if (!_selectionOutlineInitialized || std::array<float, 4>{left, top, width, height} != _selectionOutlineHole)
        return result;
    const float phase = _chrome->SelectionAntPhase();
    const double step = std::isfinite(phase) ? std::floor(static_cast<double>(phase)) : 0.0;
    const long long whole = static_cast<long long>(std::fmod(step, 8.0));
    const unsigned int wrapped = static_cast<unsigned int>((whole + 8) % 8);
    const auto views = _selectionOutlineDisplay.BorrowFrameOwner(wrapped);
    if (!views)
        return result;
    result.composition = &_composition;
    result.solid = views.solid;
    result.dashes = views.dashes;
    result.width = views.width;
    result.height = views.height;
    result.pixelLeft = std::floor(left);
    result.pixelTop = std::floor(top);
    result.clipped = _selectionOutlineGrid.clipped;
    result.clip = {
        static_cast<float>(_selectionOutlineGrid.clipLeft), static_cast<float>(_selectionOutlineGrid.clipTop),
        static_cast<float>(_selectionOutlineGrid.clipRight), static_cast<float>(_selectionOutlineGrid.clipBottom)};
    return result;
}

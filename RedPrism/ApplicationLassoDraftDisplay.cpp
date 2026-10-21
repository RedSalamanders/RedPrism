#include "Application.h"
#include "ChromeHost.h"
#include "LassoFinishGeometry.h"
#include <cmath>
#include <limits>
bool Application::EnableLassoDraftDisplayForTest() noexcept
{
    if (_shutdownStarted || _jobs.InUiCallback() || !_selectionOutlineInitialized || _lassoDraftDisplayEnabled ||
        _session.PendingLasso())
        return false;
    if (FAILED(_selectionOutlineDisplay.EnableLassoDraftOwner(*this)))
        return false;
    _lassoDraftDisplayEnabled = true;
    // Acquire cold: there is no legacy draft vector/path requiring unadmitted owner retirement.
    _chrome->SetLassoDraft(std::nullopt);
    SyncPreparedLassoDraftOwner();
    return true;
}
void Application::SyncPreparedLassoDraftOwner() noexcept
{
    auto terminal = _session.CaptureBoundedLassoDisplayForWork();
    const auto hole = _chrome->HolePixels();
    const std::array<float, 4> holeKey{hole.left, hole.top, hole.width, hole.height};
    PreparedLassoDraftRaster::GridKey grid{};
    bool valid = terminal &&
                 (terminal->display.kind == LassoKind::Polygonal || terminal->display.points.Count() >= 2) &&
                 std::isfinite(hole.left) && std::isfinite(hole.top) && std::isfinite(hole.width) &&
                 std::isfinite(hole.height) && hole.left >= 0 && hole.top >= 0 && hole.width > 0 && hole.height > 0 &&
                 static_cast<double>(hole.left) + hole.width <= _renderer.Width() &&
                 static_cast<double>(hole.top) + hole.height <= _renderer.Height();
    if (valid)
    {
        const auto bounds = terminal->display.points.Bounds();
        const double left = std::floor(hole.left), top = std::floor(hole.top);
        const double right = std::ceil(static_cast<double>(hole.left) + hole.width);
        const double bottom = std::ceil(static_cast<double>(hole.top) + hole.height);
        if (!bounds || right - left > PreparedSelectionOutlineRaster::MaximumSide ||
            bottom - top > PreparedSelectionOutlineRaster::MaximumSide)
            valid = false;
        else
        {
            const Vec2 origin = _viewport.DocumentRectOrigin(DocumentSize());
            grid.width = static_cast<std::uint32_t>(right - left);
            grid.height = static_cast<std::uint32_t>(bottom - top);
            grid.documentScalePixel = _viewport.ViewScale();
            grid.originPixelX = hole.left - left + origin.x + bounds->minX * grid.documentScalePixel;
            grid.originPixelY = hole.top - top + origin.y + bounds->minY * grid.documentScalePixel;
            grid.dpi = ChromeDpi();
            grid.clipped = true;
            grid.clipLeft = hole.left - left;
            grid.clipTop = hole.top - top;
            grid.clipRight = static_cast<double>(hole.left) + hole.width - left;
            grid.clipBottom = static_cast<double>(hole.top) + hole.height - top;
            PreparedLassoDraftRaster::MemoryPlan plan;
            valid = SUCCEEDED(PreparedLassoDraftRaster::PlanMemory(terminal->display, grid, plan));
        }
    }
    const auto authority = _session.CaptureDocumentReadAuthority();
    const bool sameSource = terminal && _overlayLassoDraft && terminal->display.key == _overlayLassoDraft->key &&
                            terminal->display.kind == _overlayLassoDraft->kind &&
                            terminal->display.mode == _overlayLassoDraft->mode &&
                            terminal->display.antialiased == _overlayLassoDraft->antialiased &&
                            terminal->display.cursor == _overlayLassoDraft->cursor &&
                            terminal->display.anchor == _overlayLassoDraft->anchor &&
                            terminal->display.points.SameCapture(_overlayLassoDraft->points);
    const bool changed = authority != _lassoDraftDocument || valid != _lassoDraftViewValid || grid != _lassoDraftGrid ||
                         holeKey != _lassoDraftHole || terminal.has_value() != _overlayLassoDraft.has_value() ||
                         (terminal && !sameSource);
    // One originally accounted arena plus bounded inline-tail/metadata copies. No vector, path, mask,
    // resource, full-source traversal or decoder call occurs here. Display captures again after admission.
    _overlayLassoDraft = terminal ? std::optional(std::move(terminal->display)) : std::nullopt;
    _lassoDraftDocument = authority;
    _lassoDraftGrid = grid;
    _lassoDraftHole = holeKey;
    _lassoDraftViewValid = valid && !_lassoDraftEpochExhausted;
    if (changed)
    {
        if (_lassoDraftViewEpoch == (std::numeric_limits<std::uint64_t>::max)())
        {
            _lassoDraftEpochExhausted = true;
            _lassoDraftViewValid = false;
        }
        else
            ++_lassoDraftViewEpoch;
        _selectionOutlineDisplay.RequestLassoDraftOwner();
    }
}
bool Application::DescribeLassoDraftOwner(LassoDraftDisplayDescriptor& descriptor) noexcept
{
    descriptor = {};
    if (_shutdownStarted || !_lassoDraftDisplayEnabled || !_lassoDraftViewValid || !_rendererReady ||
        !_gpuWorkInitialized || FAILED(_gpuBindingError) || !_overlayLassoDraft)
        return false;
    const auto authority = _session.CaptureDocumentReadAuthority();
    const auto current = _session.CaptureBoundedLassoDisplayForWork();
    if (authority != _lassoDraftDocument || !authority || !current || current->display.key != _overlayLassoDraft->key ||
        !current->display.points.SameCapture(_overlayLassoDraft->points) ||
        current->display.kind != _overlayLassoDraft->kind || current->display.mode != _overlayLassoDraft->mode ||
        current->display.antialiased != _overlayLassoDraft->antialiased ||
        current->display.cursor != _overlayLassoDraft->cursor || current->display.anchor != _overlayLassoDraft->anchor)
        return false;
    const auto bounds = current->display.points.Bounds();
    const auto hole = _chrome->HolePixels();
    if (!bounds || std::array<float, 4>{hole.left, hole.top, hole.width, hole.height} != _lassoDraftHole)
        return false;
    const auto origin = _viewport.DocumentRectOrigin(DocumentSize());
    const auto scale = _viewport.ViewScale();
    if (scale != _lassoDraftGrid.documentScalePixel || ChromeDpi() != _lassoDraftGrid.dpi ||
        hole.left - std::floor(hole.left) + origin.x + bounds->minX * scale != _lassoDraftGrid.originPixelX ||
        hole.top - std::floor(hole.top) + origin.y + bounds->minY * scale != _lassoDraftGrid.originPixelY)
        return false;
    auto& key = descriptor.key;
    key.document = authority.value();
    key.draft = current->display.key;
    key.arenaIdentity = current->display.points.ArenaIdentity();
    key.pointCount = current->display.points.Count();
    key.kind = current->display.kind;
    key.mode = current->display.mode;
    key.antialiased = current->display.antialiased;
    key.cursor = current->display.cursor;
    key.anchor = current->display.anchor;
    key.grid = _lassoDraftGrid;
    key.deviceGeneration = _gpuWork.DeviceGeneration();
    key.viewEpoch = _lassoDraftViewEpoch;
    descriptor.source = &_overlayLassoDraft.value();
    descriptor.device = _renderer.Device();
    descriptor.immediate = _renderer.Context();
    descriptor.software = _renderer.IsWarp();
    return true;
}
void Application::LassoDraftChangedOwner() noexcept
{
    _lassoDraftDisplayFailure = S_OK;
    InvalidateFrame();
}
void Application::LassoDraftFailedOwner(LassoDraftDisplayKey, HRESULT error) noexcept
{
    _lassoDraftDisplayFailure = error;
    InvalidateFrame();
}
Renderer::LassoDraftFrame Application::BorrowLassoDraftFrameOwner(float left, float top, float width,
                                                                  float height) noexcept
{
    Renderer::LassoDraftFrame frame{};
    if (!_lassoDraftDisplayEnabled || std::array<float, 4>{left, top, width, height} != _lassoDraftHole)
        return frame;
    const auto view = _selectionOutlineDisplay.BorrowLassoDraftFrameOwner();
    if (!view)
        return frame;
    frame.composition = &_composition;
    frame.color = view.color;
    frame.width = view.width;
    frame.height = view.height;
    frame.pixelLeft = std::floor(left);
    frame.pixelTop = std::floor(top);
    frame.clipped = _lassoDraftGrid.clipped;
    frame.clip = {static_cast<float>(_lassoDraftGrid.clipLeft), static_cast<float>(_lassoDraftGrid.clipTop),
                  static_cast<float>(_lassoDraftGrid.clipRight), static_cast<float>(_lassoDraftGrid.clipBottom)};
    return frame;
}

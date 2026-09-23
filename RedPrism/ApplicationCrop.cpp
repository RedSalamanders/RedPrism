#include "Application.h"

#include "ChromeHost.h"
#include "Crop.h"
#include "Resources.h"
#include "TransformEdit.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

// The Crop tool (C16, Specs/UI/UI_ApplicationChrome.md "Crop tool", Specs/Document/Document_Tools.md "Canvas size,
// image size and crop"): the frame the session holds is dragged as a whole, by a handle or drawn afresh, snaps to
// the canvas and layer edges, and Apply Crop resizes the canvas to it as one Crop entry.

namespace
{
// Self-test diagnostics go to the debugger and, when the parent redirected it, to stderr (as Application.cpp does).
void ReportCropSelfTestFailure(const std::wstring& message) noexcept
{
    OutputDebugStringW(message.c_str());
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error == nullptr || error == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), utf8.data(), size, nullptr,
                            nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

[[nodiscard]] bool Contains(const CropRect& rect, Vec2 point) noexcept
{
    return point.x >= rect.MinX() && point.x <= rect.MaxX() && point.y >= rect.MinY() && point.y <= rect.MaxY();
}
} // namespace

void Application::BeginCropDrag(Vec2 view) noexcept
{
    const std::optional<CropRect> frame = _session.VisibleCropRect();
    if (!frame.has_value())
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    CropDrag drag;
    drag.start = pixel;
    drag.original = frame.value();
    drag.mode = CropDrag::Mode::Create;
    // A handle resizes, the inside moves, the outside starts a new frame (a click alone leaves the frame as it is).
    if (_overlayGeometry.has_value())
    {
        const std::optional<TransformHit> hit = _overlayGeometry->Hit(view);
        if (hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle >= 0)
        {
            drag.mode = CropDrag::Mode::Resize;
            drag.handle = hit->handle;
        }
    }
    if (drag.mode == CropDrag::Mode::Create && Contains(frame.value(), pixel))
    {
        drag.mode = CropDrag::Mode::Move;
    }
    _cropDrag = drag;
    SyncChrome();
    _frameInvalidated = true;
}

void Application::ContinueCropDrag(Vec2 view) noexcept
{
    if (!_cropDrag.has_value() || !_session.Document().has_value())
    {
        return;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    const bool symmetric = GetKeyState(VK_MENU) < 0;
    const std::optional<double> ratio = _session.CropRatio();
    CropRect rect = _cropDrag->Updated(pixel, ratio, symmetric);
    const SnapTargets targets = _session.CropSnapTargets();
    const CropSnap snap{targets.xs, targets.ys, _viewport.DipToDocument(TransformSnap::kDistanceDip)};
    rect = snap.Apply(rect, _cropDrag.value(), pixel, ratio, symmetric);
    if (CropGeometry::Valid(rect))
    {
        _session.SetCropFrame(rect);
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::EndCropDrag() noexcept
{
    _cropDrag.reset();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::ApplyCrop() noexcept
{
    _cropDrag.reset();
    if (_session.CommitCrop())
    {
        _viewport.followsFit = true;
        DocumentChanged();
        return;
    }
    SyncChrome();
    _frameInvalidated = true;
}

void Application::CancelCropFrame() noexcept
{
    _cropDrag.reset();
    _session.CancelCrop();
    SyncChrome();
    _frameInvalidated = true;
}

void Application::OnCropAction(CropAction action, double value) noexcept
{
    if (!std::isfinite(value))
    {
        return;
    }
    switch (action)
    {
    case CropAction::SetRatio:
        _session.cropRatioChoice = std::clamp(static_cast<int>(value), 0, 4);
        _session.ChangeCropRatio();
        SyncChrome();
        _frameInvalidated = true;
        break;
    case CropAction::Cancel:
        CancelCropFrame();
        break;
    case CropAction::Apply:
        ApplyCrop();
        break;
    default:
        break;
    }
}

// The Crop header and frame follow the session: while the tool is active the visible frame is the transform box
// (no rotation knob) and everything outside it dims.
void Application::SyncCropChrome() noexcept
{
    if (!_chrome)
    {
        return;
    }
    const bool hasDocument = _session.Document().has_value();
    const bool active = _tool == EditorTool::Crop && hasDocument;
    const std::optional<CropRect> frame = active ? _session.VisibleCropRect() : std::nullopt;
    CropInspectorState state;
    state.enabled = hasDocument;
    state.ratio = _session.cropRatioChoice;
    state.hasFrame = frame.has_value();
    if (frame.has_value())
    {
        state.width = static_cast<int>(std::lround(frame->width));
        state.height = static_cast<int>(std::lround(frame->height));
    }
    _chrome->SetCropInspector(state);
    std::optional<TransformOverlayGeometry> geometry;
    std::optional<D2D1_RECT_F> shade;
    if (frame.has_value())
    {
        const Vec2 documentSize = DocumentSize();
        LayerTransform box;
        box.origin = Vec2{frame->x, frame->y};
        box.size = Vec2{frame->width, frame->height};
        geometry = TransformOverlayGeometry::FromTransform(box, _viewport, documentSize);
        geometry->showsRotation = false;
        const Vec2 topLeft = _viewport.ViewFromDocument(Vec2{frame->MinX(), frame->MinY()}, documentSize);
        const Vec2 bottomRight = _viewport.ViewFromDocument(Vec2{frame->MaxX(), frame->MaxY()}, documentSize);
        shade = D2D1::RectF(static_cast<float>(topLeft.x), static_cast<float>(topLeft.y),
                            static_cast<float>(bottomRight.x), static_cast<float>(bottomRight.y));
    }
    if (active)
    {
        _overlayGeometry = geometry;
        _chrome->SetTransformOverlay(geometry, {}, {});
    }
    _chrome->SetCropShade(shade);
}

LPCWSTR Application::CropCursor(Vec2 view) const noexcept
{
    std::optional<TransformHit> hit;
    if (_cropDrag.has_value())
    {
        if (_cropDrag->mode == CropDrag::Mode::Move)
        {
            return IDC_SIZEALL;
        }
        if (_cropDrag->mode == CropDrag::Mode::Resize)
        {
            hit = TransformHit{TransformDrag::Mode::Resize, _cropDrag->handle};
        }
    }
    else if (_overlayGeometry.has_value())
    {
        hit = _overlayGeometry->Hit(view);
    }
    if (hit.has_value() && hit->mode == TransformDrag::Mode::Resize)
    {
        const int direction = _overlayGeometry.has_value() ? _overlayGeometry->ResizeCursorDirection(hit->handle) : 0;
        return direction == 0 ? IDC_SIZEWE : direction == 1 ? IDC_SIZENWSE : direction == 2 ? IDC_SIZENS : IDC_SIZENESW;
    }
    if (!_cropDrag.has_value())
    {
        const std::optional<CropRect> frame = _session.VisibleCropRect();
        if (frame.has_value() && Contains(frame.value(), _viewport.DocumentFromView(view, DocumentSize())))
        {
            return IDC_SIZEALL;
        }
    }
    return IDC_CROSS;
}

int Application::RunCropSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        ReportCropSelfTestFailure(L"The crop self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto document = [&]() -> const CanvasDocument* { return _session.DisplayDocument(); };
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    OnTool(EditorTool::Crop);
    SyncChrome();
    if (_tool != EditorTool::Crop || _chrome->Tool() != EditorTool::Crop ||
        _session.CropFrame() != CropRect{0, 0, 1280, 800} || !_overlayGeometry.has_value())
    {
        ReportCropSelfTestFailure(L"Choosing the Crop tool did not frame the whole canvas.\n");
        return 5;
    }
    // Dragging the bottom-right handle to the document's (640, 400) resizes the frame through the pointer path.
    const CanvasHole hole = _chrome->HolePixels();
    const auto pointerAt = [&](Vec2 view) noexcept
    {
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    const Vec2 handle = _overlayGeometry->handles[4];
    const Vec2 target = _viewport.ViewFromDocument(Vec2{640, 400}, DocumentSize());
    OnPointer(WM_MOUSEMOVE, 0, pointerAt(handle));
    OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, pointerAt(handle));
    OnPointer(WM_MOUSEMOVE, MK_LBUTTON, pointerAt(Vec2{(handle.x + target.x) / 2, (handle.y + target.y) / 2}));
    OnPointer(WM_MOUSEMOVE, MK_LBUTTON, pointerAt(target));
    OnPointer(WM_LBUTTONUP, 0, pointerAt(target));
    if (_session.CropFrame() != CropRect{0, 0, 640, 400} || _cropDrag.has_value())
    {
        const std::optional<CropRect> frame = _session.CropFrame();
        ReportCropSelfTestFailure(std::format(L"Dragging the crop frame's corner did not resize it to 640 × 400 "
                                              L"({}, {}, {} × {}).\n",
                                              frame ? frame->x : -1.0, frame ? frame->y : -1.0,
                                              frame ? frame->width : -1.0, frame ? frame->height : -1.0));
        return 5;
    }
    // Enter crops: the canvas takes the frame, the layer keeps its pixels and moves with the frame's origin.
    _chrome->SetCanvasFocused(true);
    if (!HandleAccelerator(VK_RETURN, 0) || document() == nullptr || document()->width != 640 ||
        document()->height != 400 || _session.history.UndoName() != L"Crop" || _session.CropFrame().has_value() ||
        _session.VisibleCropRect() != CropRect{0, 0, 640, 400})
    {
        ReportCropSelfTestFailure(std::format(L"Enter did not crop the canvas to the frame ({} × {}, undo '{}').\n",
                                              document() ? document()->width : 0, document() ? document()->height : 0,
                                              _session.history.UndoName()));
        return 5;
    }
    // A frame dragged inside the canvas moves as a whole; Escape drops it and the whole canvas frames again.
    SyncChrome();
    const Vec2 inside = _viewport.ViewFromDocument(Vec2{320, 200}, DocumentSize());
    const Vec2 moved = _viewport.ViewFromDocument(Vec2{420, 250}, DocumentSize());
    OnPointer(WM_MOUSEMOVE, 0, pointerAt(inside));
    OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, pointerAt(inside));
    OnPointer(WM_MOUSEMOVE, MK_LBUTTON, pointerAt(moved));
    OnPointer(WM_LBUTTONUP, 0, pointerAt(moved));
    const bool movedFrame = _session.CropFrame() == CropRect{100, 50, 640, 400};
    _chrome->SetCanvasFocused(true);
    if (!movedFrame || !HandleAccelerator(VK_ESCAPE, 0) || _session.CropFrame().has_value() ||
        _session.VisibleCropRect() != CropRect{0, 0, 640, 400} || document()->width != 640)
    {
        const std::optional<CropRect> frame = _session.CropFrame();
        ReportCropSelfTestFailure(std::format(L"Moving the frame and Escape did not behave (moved {}, frame {} {} "
                                              L"{} × {}).\n",
                                              movedFrame, frame ? frame->x : -1.0, frame ? frame->y : -1.0,
                                              frame ? frame->width : -1.0, frame ? frame->height : -1.0));
        return 5;
    }
    // The header's ratio re-shapes the frame about its center; Cancel drops it; Undo restores the canvas.
    OnCropAction(CropAction::SetRatio, 2.0);
    const bool squared = _session.CropFrame() == CropRect{0, -120, 640, 640};
    OnCropAction(CropAction::Cancel, 0.0);
    OnCropAction(CropAction::SetRatio, 0.0);
    _session.Undo();
    DocumentChanged();
    OnTool(EditorTool::Move);
    if (!squared || document() == nullptr || document()->width != 1280 || document()->height != 800 ||
        _session.CropFrame().has_value())
    {
        ReportCropSelfTestFailure(L"The crop ratio, Cancel, Undo or leaving the tool did not behave.\n");
        return 5;
    }
    return 0;
}

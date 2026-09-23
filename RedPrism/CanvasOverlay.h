#pragma once

#include "Document.h"

#include <DxUi/DxUi.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Paints the Move tool's transform box, handles, rotation knob and snap guides, the selection outline (marching
// ants) and a lasso draft over the canvas hole. It is never hit: pointer input in the hole belongs to the
// application, which owns the drags (Application::OnPointer).
class CanvasOverlay final : public DxUi::Control
{
  public:
    struct Geometry
    {
        std::array<D2D1_POINT_2F, 8> handles{};
        D2D1_POINT_2F rotation{};
        bool showsRotation = true;
    };

    using OutlineLoop = std::vector<D2D1_POINT_2F>;

    // A lasso outline being drawn: freehand or polygonal corners (the polygonal one with a rubber-band cursor),
    // or the Marquee's box as four corners.
    struct Draft
    {
        LassoKind kind = LassoKind::Freehand;
        std::vector<D2D1_POINT_2F> points;
        std::optional<D2D1_POINT_2F> cursor;
    };

    // Points and guide positions are DIPs relative to the overlay's own top-left corner.
    void SetGeometry(std::optional<Geometry> geometry) noexcept;
    void SetGuides(std::vector<float> xs, std::vector<float> ys);
    void SetSelectionOutline(std::vector<OutlineLoop> loops);
    // Dash offset in DIP (whole steps 0-7 of the 8 DIP cycle). The outline geometry stays; each step's stroke style
    // is made once and kept.
    void SetAntPhase(float phase) noexcept;
    [[nodiscard]] float AntPhase() const noexcept
    {
        return _antPhase;
    }
    void SetDraft(std::optional<Draft> draft);
    // The brush tip over the canvas: a circle of the tip's diameter at the pointer (DIP relative to the overlay).
    struct BrushCursor
    {
        D2D1_POINT_2F center{};
        float radius = 0;
        float hardness = 1;
    };
    void SetBrushCursor(std::optional<BrushCursor> cursor) noexcept;
    // A pending gradient's line in DIP, with a handle at each end.
    void SetGradientLine(std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> line) noexcept;
    // The shape being dragged, filled with the color it will get and outlined so it reads on any background.
    struct ShapeDraftOverlay
    {
        bool ellipse = false;
        D2D1_RECT_F rect{};
        float cornerRadius = 0;
        uint32_t argb = 0xFF000000u;
    };
    void SetShapeDraft(std::optional<ShapeDraftOverlay> draft) noexcept;
    // The crop frame (DIPs relative to the overlay): everything outside it dims.
    void SetCropShade(std::optional<D2D1_RECT_F> frame) noexcept;
    // The eyedropper's comparison ring: the new color above, the color from before the drag below.
    struct SampleRing
    {
        D2D1_POINT_2F center{};
        uint32_t originalArgb = 0xFF000000u;
        uint32_t sampledArgb = 0xFF000000u;
    };
    void SetSampleRing(std::optional<SampleRing> ring) noexcept;
    // Clone Stamp's source crosshair.
    void SetCloneSource(std::optional<D2D1_POINT_2F> point) noexcept;
    [[nodiscard]] std::optional<D2D1_POINT_2F> CloneSource() const noexcept
    {
        return _cloneSource;
    }
    [[nodiscard]] bool HasGeometry() const noexcept
    {
        return _geometry.has_value();
    }
    [[nodiscard]] bool HasSelectionOutline() const noexcept
    {
        return !_outline.empty();
    }
    [[nodiscard]] bool HasDraft() const noexcept
    {
        return _draft.has_value();
    }

    void Paint(DxUi::ControlHost& host) const override;
    [[nodiscard]] D2D1_RECT_F GetHitBounds() const noexcept override;
    // The crop frame clipped to the overlay's bounds, or nothing when the bounds are empty or inverted (a window
    // shorter than the chrome around the hole) or the frame misses them (RF-413).
    [[nodiscard]] static std::optional<D2D1_RECT_F> ShadeFrame(const D2D1_RECT_F& bounds,
                                                               const D2D1_RECT_F& frame) noexcept;

  private:
    void PaintSelection(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const;
    void PaintDraft(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const;
    void PaintBrushCursor(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const;
    void PaintTools(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const;
    void PaintTransform(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const;
    [[nodiscard]] bool EnsureSelectionResources(ID2D1DeviceContext* dc) const noexcept;
    // The whole dash step (0-7) of the phase.
    [[nodiscard]] std::size_t AntStep() const noexcept
    {
        const double step = std::isfinite(_antPhase) ? std::floor(static_cast<double>(_antPhase)) : 0.0;
        const long long whole = static_cast<long long>(std::fmod(step, 8.0));
        return static_cast<std::size_t>((whole + 8) % 8);
    }

    std::optional<Geometry> _geometry;
    std::vector<float> _guideXs;
    std::vector<float> _guideYs;
    std::vector<OutlineLoop> _outline;
    std::optional<Draft> _draft;
    std::optional<BrushCursor> _brushCursor;
    std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> _gradientLine;
    std::optional<ShapeDraftOverlay> _shapeDraft;
    std::optional<D2D1_RECT_F> _cropShade;
    std::optional<SampleRing> _sampleRing;
    std::optional<D2D1_POINT_2F> _cloneSource;
    // Device-independent Direct2D resources, rebuilt when the outline or the factory changes.
    mutable wil::com_ptr_nothrow<ID2D1Factory> _factory;
    mutable wil::com_ptr_nothrow<ID2D1PathGeometry> _outlineGeometry;
    // One dash style per step of the 8 DIP cycle, made on first use (RF-408).
    mutable std::array<wil::com_ptr_nothrow<ID2D1StrokeStyle>, 8> _antStyles;
    mutable bool _outlineDirty = false;
    float _antPhase = 0.0f;
};

#pragma once

#include <DxUi/DxUi.h>

#include <array>
#include <optional>
#include <vector>

// Paints the Move tool's transform box, handles, rotation knob and snap guides over the canvas hole. It is never
// hit: pointer input in the hole belongs to the application, which owns the drag (Application::OnPointer).
class CanvasOverlay final : public DxUi::Control
{
  public:
    struct Geometry
    {
        std::array<D2D1_POINT_2F, 8> handles{};
        D2D1_POINT_2F rotation{};
        bool showsRotation = true;
    };

    // Points and guide positions are DIPs relative to the overlay's own top-left corner.
    void SetGeometry(std::optional<Geometry> geometry) noexcept;
    void SetGuides(std::vector<float> xs, std::vector<float> ys);
    [[nodiscard]] bool HasGeometry() const noexcept
    {
        return _geometry.has_value();
    }

    void Paint(DxUi::ControlHost& host) const override;
    [[nodiscard]] D2D1_RECT_F GetHitBounds() const noexcept override;

  private:
    std::optional<Geometry> _geometry;
    std::vector<float> _guideXs;
    std::vector<float> _guideYs;
};

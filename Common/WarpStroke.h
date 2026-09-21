#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <vector>

// A Smudge or Liquify stroke in progress (C11, Specs/Document/Document_Tools.md), the legacy WarpStroke. It works on
// the active layer as the canvas shows it, at document size, changing it dab by dab; the host shows that working
// copy in place of the layer. When the stroke ends the session paints the result into the layer's own pixels along
// the stroke's path (`DocumentSession::FinishWarp`).
class WarpStroke
{
  public:
    static constexpr int kTileSize = 256;

    // `image` is the layer rendered at document size (premultiplied BGRA); `settings` give the tip (diameter at
    // least 2, hardness at most 0.98) and the strength (the opacity, 0.01–1).
    WarpStroke(const Raster& image, SmearMode mode, const BrushSettings& settings);

    [[nodiscard]] bool IsValid() const noexcept
    {
        return _valid;
    }
    // Continues the stroke to `point` (document pixels), dabbing along the way.
    void Append(Vec2 point);
    // Every dab's center, for painting the result into the layer.
    [[nodiscard]] const std::vector<Vec2>& Points() const noexcept
    {
        return _points;
    }
    // The working copy as a raster (document size); a copy of the pixels.
    [[nodiscard]] Raster Image() const;
    // Tiles of the working copy (256² of the document grid) whose pixels changed since the last call, for the live
    // preview.
    [[nodiscard]] std::vector<RasterTile> TakeChangedTiles();
    [[nodiscard]] SmearMode Mode() const noexcept
    {
        return _mode;
    }
    [[nodiscard]] double Diameter() const noexcept
    {
        return _diameter;
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _width;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _height;
    }

  private:
    [[nodiscard]] int Radius() const noexcept;
    // How much a dab moves pixels at a distance `u` (0 center, 1 rim) from its center.
    [[nodiscard]] float Weight(float u) const noexcept;
    void PickUp(Vec2 center);
    void Smudge(Vec2 center);
    void Push(Vec2 from, Vec2 to);
    void MarkDirty(int x0, int y0, int x1, int y1) noexcept;

    SmearMode _mode = SmearMode::Liquify;
    double _diameter = 2;
    double _hardness = 0;
    double _strength = 1;
    int _width = 0;
    int _height = 0;
    int _columns = 0;
    std::vector<std::uint8_t> _pixels;
    std::vector<Vec2> _points;
    std::optional<Vec2> _last;
    std::vector<float> _carried; // Smudge: the color the brush carries, a (2r+1)² BGRA square
    std::vector<float> _scratch;
    std::vector<std::uint8_t> _dirty; // one flag per tile
    bool _valid = false;
};

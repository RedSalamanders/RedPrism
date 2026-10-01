#pragma once
#include "Document.h"
#include <cstdint>
#include <optional>
#include <utility>

struct AdjustmentOpeningInput final
{
    JobToken token;
    Guid documentID;
    Guid layerID;
    std::optional<Guid> active;
    bool maskSelected = false;
    std::optional<CropRect> crop;
};

class PreparedAdjustmentOpening final
{
  public:
    PreparedAdjustmentOpening(const PreparedAdjustmentOpening&) = delete;
    PreparedAdjustmentOpening& operator=(const PreparedAdjustmentOpening&) = delete;
    PreparedAdjustmentOpening(PreparedAdjustmentOpening&&) noexcept = default;
    PreparedAdjustmentOpening& operator=(PreparedAdjustmentOpening&&) noexcept = default;
    [[nodiscard]] const ColorEdit& Panel() const noexcept
    {
        return _panel.value();
    }

  private:
    friend class DocumentSession;
    PreparedAdjustmentOpening(AdjustmentOpeningInput input, ColorEdit panel, LayerAdjustment original,
                              DocumentHistory::Publication history)
        : _input(std::move(input)), _panel(std::move(panel)), _original(std::move(original)),
          _history(std::move(history))
    {
    }
    AdjustmentOpeningInput _input;
    std::optional<ColorEdit> _panel;
    std::optional<LayerAdjustment> _original;
    DocumentHistory::Publication _history;
    std::optional<CanvasDocument> _retiredDisplay;
};
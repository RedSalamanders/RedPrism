#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <utility>

// Owner capture occurs only after complete admission. Worker reads own every settings/selection/raster handle.
// A whole-document token has no target: its generation guards the complete final metadata publication.
struct AdjustmentClosureInput final
{
    JobToken token;
    Guid documentID;
    Guid layerID;
    std::uint64_t opening = 0;
    std::optional<Guid> active;
    bool maskSelected = false;
    std::optional<CropRect> crop;
    LayerAdjustment original;
    FilterSettings settings;
    bool commit = true;
};

class PreparedAdjustmentClosure final
{
  public:
    PreparedAdjustmentClosure(const PreparedAdjustmentClosure&) = delete;
    PreparedAdjustmentClosure& operator=(const PreparedAdjustmentClosure&) = delete;
    PreparedAdjustmentClosure(PreparedAdjustmentClosure&&) noexcept = default;
    PreparedAdjustmentClosure& operator=(PreparedAdjustmentClosure&&) noexcept = default;
    [[nodiscard]] const std::optional<CanvasDocument>& AfterDocument() const noexcept
    {
        return _document;
    }
    [[nodiscard]] bool Commits() const noexcept
    {
        return _input.commit;
    }

  private:
    friend class DocumentSession;
    PreparedAdjustmentClosure(AdjustmentClosureInput input, std::optional<CanvasDocument> document,
                              DocumentHistory::Publication history)
        : _input(std::move(input)), _document(std::move(document)), _history(std::move(history))
    {
    }
    AdjustmentClosureInput _input;
    std::optional<CanvasDocument> _document;
    DocumentHistory::Publication _history;
    std::optional<ColorEdit> _retiredPanel;
    std::optional<LayerAdjustment> _retiredOriginal;
    std::optional<CanvasDocument> _retiredDisplay;
};

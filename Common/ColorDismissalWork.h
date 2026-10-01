#pragma once

#include "Document.h"

#include <cstdint>
#include <optional>
#include <utility>

// Destructive-panel dismissal has no history End or pixel restoration. Its descriptor owns lifetime authority;
// applying the completed candidate transfers the old panel/display graph to the admitted worker-release owner.
struct ColorDismissalInput final
{
    JobToken token;
    std::uint64_t opening = 0;
    std::optional<Guid> active;
    bool maskSelected = false;
    std::optional<CropRect> crop;
};

class PreparedColorDismissal final
{
  public:
    PreparedColorDismissal(const PreparedColorDismissal&) = delete;
    PreparedColorDismissal& operator=(const PreparedColorDismissal&) = delete;
    PreparedColorDismissal(PreparedColorDismissal&&) noexcept = default;
    PreparedColorDismissal& operator=(PreparedColorDismissal&&) noexcept = default;

  private:
    friend class DocumentSession;
    PreparedColorDismissal(ColorDismissalInput input, DocumentHistory::StateView history)
        : _input(std::move(input)), _history(std::move(history))
    {
    }
    ColorDismissalInput _input;
    DocumentHistory::StateView _history;
    std::optional<ColorEdit> _retiredPanel;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};

#pragma once

#include "ColorEditWork.h"

#include <optional>
#include <utility>

// Only worker preparation may bind a complete history state to these exact color inputs and output.
// The owner cannot replace either half. Retired layer, settings and panel owners remain in the same aggregate.
class PreparedColorEdit final
{
  public:
    PreparedColorEdit(const PreparedColorEdit&) = delete;
    PreparedColorEdit& operator=(const PreparedColorEdit&) = delete;
    PreparedColorEdit(PreparedColorEdit&&) noexcept = default;
    PreparedColorEdit& operator=(PreparedColorEdit&&) noexcept = default;

  private:
    friend class DocumentSession;
    friend std::optional<PreparedColorEdit> PrepareColorEditPublication(const std::optional<CanvasDocument>&,
                                                                        const std::optional<Guid>&, std::uint64_t,
                                                                        DocumentHistory::StateView,
                                                                        const ColorEditJobResult&, CancelToken,
                                                                        DocumentHistory::PrepareObserver);

    PreparedColorEdit(std::uint64_t generation, ColorEditJobResult result, ImageLayer layer,
                      DocumentHistory::Publication history)
        : _generation(generation), _result(std::move(result)), _layer(std::move(layer)), _history(std::move(history))
    {
    }
    std::uint64_t _generation = 0;
    ColorEditJobResult _result;
    ImageLayer _layer;
    DocumentHistory::Publication _history;
    std::optional<ColorEdit> _retiredPanel;
};

[[nodiscard]] std::optional<PreparedColorEdit> PrepareColorEditPublication(
    const std::optional<CanvasDocument>& source, const std::optional<Guid>& active, std::uint64_t generation,
    DocumentHistory::StateView history, const ColorEditJobResult& result, CancelToken cancellation = {},
    DocumentHistory::PrepareObserver observer = {});

// Borrowed immutable metadata only. Neither admission nor release estimation copies settings or sparse pixels.
[[nodiscard]] std::optional<std::uint64_t> ColorEditCommitMemoryBytes(const ColorEdit& edit,
                                                                      std::uint64_t sequence) noexcept;
[[nodiscard]] std::uint64_t ColorEditHeldMemoryBytes(const ColorEdit& edit) noexcept;

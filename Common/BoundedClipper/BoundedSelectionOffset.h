#pragma once

#include "BoundedSelectionBoolean.h"

namespace BoundedSelectionOffset
{
// Worker-only command engine. Positive amount expands then clips; negative amount contracts.
// All scratch phases share one admitted arena. Ordinary output has a separate caller-owned envelope.
[[nodiscard]] std::optional<std::vector<SelectionLoop>> Run(const std::vector<SelectionLoop>& subject, int signedAmount,
                                                            int canvasWidth, int canvasHeight,
                                                            CancelToken cancellation = {},
                                                            BoundedSelectionBoolean::Limits limits = {},
                                                            BoundedSelectionBoolean::Statistics* statistics = nullptr,
                                                            void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr,
                                                            void* context = nullptr);
} // namespace BoundedSelectionOffset

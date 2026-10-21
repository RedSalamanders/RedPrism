#pragma once

#include "Document.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// Whole original CPU draft/cache capture for worker destruction only. This conveys no useful-work,
// GPU-completion, history, sample or publication authority. The parent admits and joins every user first.
class WarpRetirementInput final
{
  public:
    WarpRetirementInput(const WarpRetirementInput&) = delete;
    WarpRetirementInput& operator=(const WarpRetirementInput&) = delete;
    WarpRetirementInput(WarpRetirementInput&&) noexcept = default;
    WarpRetirementInput& operator=(WarpRetirementInput&&) = delete;

  private:
    friend class DocumentSession;
    WarpRetirementInput(std::shared_ptr<WarpStroke> draft, Raster source) noexcept
        : _draft(std::move(draft)), _source(std::move(source))
    {
    }
    std::shared_ptr<WarpStroke> _draft;
    Raster _source;
};

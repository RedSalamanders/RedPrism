#pragma once

#include "BoundedClipper/BoundedSelectionOffset.h"
#include "WandLayerSelectionWork.h"

#include <cstdint>
#include <utility>

// Sealed command-specific capture for the settled Expand/Contract operations.
class SelectionOffsetInput final
{
  public:
    SelectionOffsetInput(const SelectionOffsetInput&) = delete;
    SelectionOffsetInput& operator=(const SelectionOffsetInput&) = delete;
    SelectionOffsetInput(SelectionOffsetInput&&) noexcept = default;
    SelectionOffsetInput& operator=(SelectionOffsetInput&&) noexcept = default;

  private:
    friend class DocumentSession;
    explicit SelectionOffsetInput(WandLayerSelectionInput input, int amount) noexcept
        : _input(std::move(input)), _amount(amount)
    {
    }
    WandLayerSelectionInput _input;
    int _amount = 0;
};

class PreparedSelectionOffset final
{
  public:
    PreparedSelectionOffset(const PreparedSelectionOffset&) = delete;
    PreparedSelectionOffset& operator=(const PreparedSelectionOffset&) = delete;
    PreparedSelectionOffset(PreparedSelectionOffset&&) noexcept = default;
    PreparedSelectionOffset& operator=(PreparedSelectionOffset&&) noexcept = default;
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool ChangesSelection() const noexcept
    {
        return _publication.ChangesSelection();
    }
    [[nodiscard]] std::uint64_t OutputAllocationBytes() const noexcept
    {
        return _publication.OutputAllocationBytes();
    }
    [[nodiscard]] const DocumentSelection* SelectionForDisplayForWork() const noexcept
    {
        return _publication.SelectionForDisplayForWork();
    }

  private:
    friend class DocumentSession;
    explicit PreparedSelectionOffset(PreparedWandLayerSelection result) noexcept : _publication(std::move(result)) {}
    PreparedWandLayerSelection _publication;
};

struct SelectionOffsetMemoryPlan final
{
    std::uint64_t captureHistoryBytes = 0;
    std::uint64_t workspaceBytes = 0;
    std::uint64_t geometryOutputBytes = 0;
    std::uint64_t preparationBytes = 0;
    std::uint64_t retirementBytes = 0;
    std::uint64_t reservationBytes = 0;
    bool hasSelection = false;
};

// Test ceilings may only reduce the production ceilings. They cannot bypass the original admission.
struct SelectionOffsetObserver final
{
    BoundedSelectionBoolean::Limits limits;
    BoundedSelectionBoolean::Statistics* statistics = nullptr;
    void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    void* context = nullptr;
};

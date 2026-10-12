#pragma once

#include "BoundedClipper/BoundedSelectionBoolean.h"
#include "WandLayerSelectionWork.h"

#include <cstdint>
#include <utility>

// Sealed command-specific capture for the settled Inverse operation.
class InverseSelectionInput final
{
  public:
    InverseSelectionInput(const InverseSelectionInput&) = delete;
    InverseSelectionInput& operator=(const InverseSelectionInput&) = delete;
    InverseSelectionInput(InverseSelectionInput&&) noexcept = default;
    InverseSelectionInput& operator=(InverseSelectionInput&&) noexcept = default;

  private:
    friend class DocumentSession;
    explicit InverseSelectionInput(WandLayerSelectionInput input) noexcept : _input(std::move(input)) {}
    WandLayerSelectionInput _input;
};

class PreparedInverseSelection final
{
  public:
    PreparedInverseSelection(const PreparedInverseSelection&) = delete;
    PreparedInverseSelection& operator=(const PreparedInverseSelection&) = delete;
    PreparedInverseSelection(PreparedInverseSelection&&) noexcept = default;
    PreparedInverseSelection& operator=(PreparedInverseSelection&&) noexcept = default;
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
    explicit PreparedInverseSelection(PreparedWandLayerSelection result) noexcept : _publication(std::move(result)) {}
    PreparedWandLayerSelection _publication;
};

struct InverseSelectionMemoryPlan final
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
struct InverseSelectionObserver final
{
    BoundedSelectionBoolean::Limits limits;
    BoundedSelectionBoolean::Statistics* statistics = nullptr;
    void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr;
    void* context = nullptr;
};

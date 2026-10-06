#pragma once

#include <DxUi/PreparedTreeAccessibility.h>

#include "Jobs.h"
#include "LayerRowsWork.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4324)
class PreparedLayerRows final : public DxUi::IPreparedTreeAccessibilityRows, public DeferredReleaseNode
{
  public:
    // Include this fixed wrapper/control-block allowance in the original admission before worker Create.
    [[nodiscard]] static std::uint64_t OwnershipAllowance() noexcept;
    [[nodiscard]] static std::shared_ptr<PreparedLayerRows> Create(LayerRowsWork::Handle rows,
                                                                   std::uint64_t retirementBound);

    PreparedLayerRows(const PreparedLayerRows&) = delete;
    PreparedLayerRows& operator=(const PreparedLayerRows&) = delete;
    PreparedLayerRows(PreparedLayerRows&&) = delete;
    PreparedLayerRows& operator=(PreparedLayerRows&&) = delete;

    [[nodiscard]] std::span<const HierarchyEntry> Rows() const noexcept;
    [[nodiscard]] std::span<const std::wstring> DisplayText() const noexcept;
    [[nodiscard]] const CanvasDocument& Source() const noexcept;
    [[nodiscard]] std::optional<std::size_t> FindVisibleRow(const Guid& id) const noexcept;

    [[nodiscard]] std::size_t GetCount() const noexcept override;
    [[nodiscard]] std::optional<DxUi::TreeAccessibilityItemView> GetItem(
        std::size_t visibleIndex) const noexcept override;
    [[nodiscard]] std::optional<DxUi::TreeAccessibilityItemView> FindItem(std::uint64_t itemId) const noexcept override;

    // Call from the successful owner completion, before exposing this wrapper to any reader.
    [[nodiscard]] std::int32_t BindMemory(JobScheduler& scheduler, JobTicket ticket) noexcept;
    // Seals the memory transfer before publication. A wrapper without its retained admission cannot be published.
    [[nodiscard]] bool SealForPublication() noexcept;

    [[nodiscard]] bool HasMemoryLease() const noexcept;
    [[nodiscard]] std::uint64_t MemoryBytes() const noexcept;
    [[nodiscard]] std::uint64_t ResidentBytes() const noexcept;

  private:
    PreparedLayerRows(LayerRowsWork::Handle rows, std::uint64_t retirementBound) noexcept;
    ~PreparedLayerRows() override = default;

    static void Release(PreparedLayerRows* rows) noexcept;
    static void DestroyNode(DeferredReleaseNode* node) noexcept;

    LayerRowsWork::Handle _rows;
    SharedRetainedMemoryLease _lease;
    bool _sealedForPublication = false;
};
#pragma warning(pop)

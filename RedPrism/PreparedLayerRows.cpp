#include "PreparedLayerRows.h"

#include <new>
#include <utility>

namespace
{
constexpr std::size_t kControlBlockLimit = 4096;

// The shared_ptr rebinds this allocator to its private control-block type. Refuse before allocation if a
// compiler/CRT change exceeds the declared envelope; never rely on a guessed private STL structure size.
template <class T> struct RowOwnerAllocator
{
    using value_type = T;
    RowOwnerAllocator() noexcept = default;
    template <class U> RowOwnerAllocator(const RowOwnerAllocator<U>&) noexcept {}
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > kControlBlockLimit / sizeof(T))
            throw std::bad_alloc{};
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* pointer, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(pointer, count);
    }
    template <class U> [[nodiscard]] bool operator==(const RowOwnerAllocator<U>&) const noexcept
    {
        return true;
    }
};
} // namespace

PreparedLayerRows::PreparedLayerRows(LayerRowsWork::Handle rows, std::uint64_t retirementBound) noexcept
    : _rows(std::move(rows))
{
    bytes = retirementBound;
    destroy = &PreparedLayerRows::DestroyNode;
}

std::uint64_t PreparedLayerRows::OwnershipAllowance() noexcept
{
    return sizeof(PreparedLayerRows) + kControlBlockLimit + 4096; // Include allocator/ownership metadata allowance.
}

std::shared_ptr<PreparedLayerRows> PreparedLayerRows::Create(LayerRowsWork::Handle rows, std::uint64_t retirementBound)
{
    if (!rows || retirementBound < rows->bytes || retirementBound - rows->bytes < OwnershipAllowance())
        return {};

    auto* prepared = new PreparedLayerRows(std::move(rows), retirementBound);
    // The shared_ptr constructor invokes Release(prepared) if allocating its control block throws.
    return std::shared_ptr<PreparedLayerRows>(prepared, &PreparedLayerRows::Release,
                                              RowOwnerAllocator<PreparedLayerRows>{});
}

std::span<const HierarchyEntry> PreparedLayerRows::Rows() const noexcept
{
    return _rows->Rows();
}

std::span<const std::wstring> PreparedLayerRows::DisplayText() const noexcept
{
    return _rows->DisplayText();
}

const CanvasDocument& PreparedLayerRows::Source() const noexcept
{
    return _rows->Source();
}

std::optional<std::size_t> PreparedLayerRows::FindVisibleRow(const Guid& id) const noexcept
{
    return _rows->FindVisibleRow(id);
}

std::size_t PreparedLayerRows::GetCount() const noexcept
{
    return Rows().size();
}

std::optional<DxUi::TreeAccessibilityItemView> PreparedLayerRows::GetItem(std::size_t visibleIndex) const noexcept
{
    const auto rows = Rows();
    if (visibleIndex >= rows.size())
        return std::nullopt;

    const HierarchyEntry& row = rows[visibleIndex];
    const bool isGroup = row.isGroup;
    return DxUi::TreeAccessibilityItemView{visibleIndex,
                                           static_cast<std::uint64_t>(visibleIndex) + 1u,
                                           DisplayText()[visibleIndex],
                                           static_cast<std::size_t>(row.depth),
                                           isGroup,
                                           isGroup && !row.collapsed};
}

std::optional<DxUi::TreeAccessibilityItemView> PreparedLayerRows::FindItem(std::uint64_t itemId) const noexcept
{
    if (itemId == 0 || itemId > static_cast<std::uint64_t>(GetCount()))
        return std::nullopt;
    return GetItem(static_cast<std::size_t>(itemId - 1u));
}

std::int32_t PreparedLayerRows::BindMemory(JobScheduler& scheduler, JobTicket ticket) noexcept
{
    if (_sealedForPublication || _lease)
        return static_cast<std::int32_t>(0x80070057u); // E_INVALIDARG
    return scheduler.RetainMemory(ticket, _lease);
}

bool PreparedLayerRows::SealForPublication() noexcept
{
    if (_sealedForPublication || !_lease)
        return false;
    _sealedForPublication = true;
    return true;
}

bool PreparedLayerRows::HasMemoryLease() const noexcept
{
    return static_cast<bool>(_lease);
}

std::uint64_t PreparedLayerRows::MemoryBytes() const noexcept
{
    return _lease.Bytes();
}

std::uint64_t PreparedLayerRows::ResidentBytes() const noexcept
{
    return _lease.ResidentBytes();
}

void PreparedLayerRows::Release(PreparedLayerRows* rows) noexcept
{
    if (rows && !TryDeferRelease(*rows, DeferredReleasePolicy::AlwaysOnOwner))
        rows->destroy(rows);
}

void PreparedLayerRows::DestroyNode(DeferredReleaseNode* node) noexcept
{
    auto* rows = static_cast<PreparedLayerRows*>(node);
    SharedRetainedMemoryLease lease = std::move(rows->_lease);
    delete rows;
    lease.Reset();
}

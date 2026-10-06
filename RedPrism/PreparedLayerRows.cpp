#include "PreparedLayerRows.h"

#include <algorithm>
#include <exception>
#include <limits>
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

std::uint64_t PreparedLayerRows::OwnershipAllowance(std::size_t layerCount) noexcept
{
    if (layerCount > static_cast<std::size_t>(kMaxLayerCount))
        return 0;
    // Exact arrays, with the visible arrays bounded by the complete layer count. No hash table or vector slack.
    return sizeof(PreparedLayerRows) + kControlBlockLimit + 4096 +
           layerCount * (sizeof(LayerIdentity) + sizeof(std::uint64_t) + sizeof(VisibleIdentity));
}

std::shared_ptr<PreparedLayerRows> PreparedLayerRows::Create(LayerRowsWork::Handle rows, std::uint64_t retirementBound,
                                                             std::uint64_t documentSerial, std::uint64_t firstItemId,
                                                             const PreparedLayerRows* previous)
{
    if (!rows)
        return {};
    const auto layerCount = rows->Source().layers.size();
    const auto allowance = OwnershipAllowance(layerCount);
    if (allowance == 0 || rows->Rows().size() > layerCount || documentSerial == 0 || firstItemId < FirstItemIdentity ||
        layerCount > (std::numeric_limits<std::uint64_t>::max)() - firstItemId || retirementBound < rows->bytes ||
        retirementBound - rows->bytes < allowance)
        return {};

    auto* prepared = new PreparedLayerRows(std::move(rows), retirementBound);
    // The shared_ptr constructor invokes Release(prepared) if allocating its control block throws.
    auto result = std::shared_ptr<PreparedLayerRows>(prepared, &PreparedLayerRows::Release,
                                                     RowOwnerAllocator<PreparedLayerRows>{});
    result->PrepareIdentities(documentSerial, firstItemId, previous);
    return result;
}

void PreparedLayerRows::PrepareIdentities(std::uint64_t documentSerial, std::uint64_t firstItemId,
                                          const PreparedLayerRows* previous)
{
    _documentSerial = documentSerial;
    _layerCount = Source().layers.size();
    if (_layerCount != 0)
        _layerIdentities = std::make_unique<LayerIdentity[]>(_layerCount);
    // Retain hidden descendants as well: a collapse/reopen never reassigns their item identity.
    for (std::size_t index = 0; index < _layerCount; ++index)
    {
        const auto& id = Source().layers[index].id;
        const auto existing = previous && previous->_documentSerial == documentSerial ? previous->FindLayerItem(id) : 0;
        _layerIdentities[index] = {id, existing != 0 ? existing : firstItemId + index};
    }
    if (_layerCount > 1)
        std::sort(_layerIdentities.get(), _layerIdentities.get() + _layerCount,
                  [](const auto& left, const auto& right) noexcept { return left.id < right.id; });
    const auto visibleCount = GetCount();
    if (visibleCount != 0)
    {
        _visibleItemIds = std::make_unique<std::uint64_t[]>(visibleCount);
        _visibleLookup = std::make_unique<VisibleIdentity[]>(visibleCount);
    }
    for (std::size_t index = 0; index < visibleCount; ++index)
    {
        const auto itemId = FindLayerItem(Rows()[index].id);
        if (itemId == 0)
            std::terminate(); // The prepared hierarchy must refer to the admitted sealed document.
        _visibleItemIds[index] = itemId;
        _visibleLookup[index] = {itemId, index};
    }
    if (visibleCount > 1)
        std::sort(_visibleLookup.get(), _visibleLookup.get() + visibleCount,
                  [](const auto& left, const auto& right) noexcept { return left.itemId < right.itemId; });
}

std::uint64_t PreparedLayerRows::FindLayerItem(const Guid& id) const noexcept
{
    std::size_t lower = 0, upper = _layerCount;
    while (lower < upper)
    {
        const auto middle = lower + (upper - lower) / 2;
        const auto& candidate = _layerIdentities[middle];
        if (candidate.id == id)
            return candidate.itemId;
        if (candidate.id < id)
            lower = middle + 1;
        else
            upper = middle;
    }
    return 0;
}

std::uint64_t PreparedLayerRows::ItemIdAt(std::size_t visibleIndex) const noexcept
{
    return visibleIndex < GetCount() ? _visibleItemIds[visibleIndex] : 0;
}

std::optional<std::size_t> PreparedLayerRows::FindVisibleItem(std::uint64_t itemId) const noexcept
{
    std::size_t lower = 0, upper = GetCount();
    while (lower < upper)
    {
        const auto middle = lower + (upper - lower) / 2;
        const auto& candidate = _visibleLookup[middle];
        if (candidate.itemId == itemId)
            return candidate.visibleIndex;
        if (candidate.itemId < itemId)
            lower = middle + 1;
        else
            upper = middle;
    }
    return std::nullopt;
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
    return DxUi::TreeAccessibilityItemView{
        visibleIndex, ItemIdAt(visibleIndex),   DisplayText()[visibleIndex], static_cast<std::size_t>(row.depth),
        isGroup,      isGroup && !row.collapsed};
}

std::optional<DxUi::TreeAccessibilityItemView> PreparedLayerRows::FindItem(std::uint64_t itemId) const noexcept
{
    const auto index = FindVisibleItem(itemId);
    return index ? GetItem(index.value()) : std::nullopt;
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

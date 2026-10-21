#include "LayerRowsWork.h"

#include <array>
#include <functional>
#include <limits>
#include <utility>

namespace
{
constexpr auto kEmpty = (std::numeric_limits<std::size_t>::max)();
constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::uint64_t kAllocationAllowance = 64 * 1024;
constexpr std::size_t kMaximumPatternUnits = 1024;

bool Add(std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}

bool PatternValid(std::wstring_view pattern) noexcept
{
    if (pattern.size() > kMaximumPatternUnits)
        return false;
    unsigned int arguments = 0;
    for (std::size_t index = 0; index < pattern.size(); ++index)
    {
        const auto unit = pattern[index];
        if (unit != L'{' && unit != L'}')
            continue;
        if (index + 1 < pattern.size() && pattern[index + 1] == unit)
        {
            ++index; // std::format's escaped literal brace
            continue;
        }
        if (unit == L'{' && pattern.substr(index, 3) == L"{0}")
        {
            ++arguments;
            index += 2;
            continue;
        }
        return false;
    }
    return arguments == 1;
}

void AppendHiddenText(std::wstring& text, std::wstring_view pattern, std::wstring_view name)
{
    for (std::size_t index = 0; index < pattern.size(); ++index)
    {
        const auto unit = pattern[index];
        if ((unit == L'{' || unit == L'}') && index + 1 < pattern.size() && pattern[index + 1] == unit)
        {
            text.push_back(unit);
            ++index;
        }
        else if (unit == L'{') // admission accepts exactly one {0} and no other unescaped brace
        {
            text.append(name);
            index += 2;
        }
        else
            text.push_back(unit);
    }
}

struct Frame final
{
    std::size_t next = kEmpty;
    int depth = 0;
    bool visible = true;
};
} // namespace

LayerRowsAdmission LayerRowsAdmission::Estimate(const CanvasDocument& source, std::size_t collapsedCount,
                                                std::wstring_view hiddenPattern) noexcept
{
    const auto count = source.layers.size();
    const auto bounds = source.layers.OwnershipBounds();
    const auto sourceBytes = source.RetirementMemoryBytes();
    if (count > kMaxLayerCount || collapsedCount > kMaxLayerCount || !PatternValid(hiddenPattern) || !sourceBytes ||
        !bounds)
        return {};
    std::size_t slots = 1;
    while (slots < count * 2)
        slots *= 2;
    // Input collapse owners, output names/labels, lookup indices and transient child/visibility work. The sealed
    // metadata bound includes source name capacity; its extra two copies cover rows and formatted labels.
    std::uint64_t metadata =
        sizeof(LayerRowsWork) + 12 * kAllocationAllowance + static_cast<std::uint64_t>(collapsedCount) * sizeof(Guid) +
        static_cast<std::uint64_t>(count) * (sizeof(HierarchyEntry) + sizeof(std::wstring) + 5 * sizeof(std::size_t) +
                                             1 + (hiddenPattern.size() + 40) * sizeof(wchar_t)) +
        static_cast<std::uint64_t>(slots) * sizeof(std::size_t);
    const auto names = bounds.value().metadataBytes;
    if (!Add(metadata, names) || !Add(metadata, names))
        return {};
    std::uint64_t bytes = sourceBytes;
    if (!Add(bytes, metadata))
        return {};
    LayerRowsAdmission admission;
    admission._documentId = source.id;
    admission._layers = source.layers.Identity();
    admission._count = count;
    admission._collapsedCount = collapsedCount;
    admission._patternUnits = hiddenPattern.size();
    admission._slots = slots;
    admission._sourceBytes = sourceBytes;
    admission._metadataBytes = metadata;
    admission._bytes = bytes;
    return admission;
}

LayerRowsWork::Handle LayerRowsWork::Prepare(CanvasDocument source, std::vector<Guid> collapsed,
                                             std::wstring hiddenPattern, const LayerRowsAdmission& admission,
                                             CancelToken cancellation, LayerRowsObserver observer)
{
    if (!admission.IsValid() || cancellation.IsCancelled() || source.id != admission._documentId ||
        source.layers.Identity() != admission._layers || source.layers.size() != admission._count ||
        source.RetirementMemoryBytes() != admission._sourceBytes || collapsed.size() != admission._collapsedCount ||
        hiddenPattern.size() != admission._patternUnits || !PatternValid(hiddenPattern))
        return {};
    auto constructed = std::make_unique<LayerRowsWork>(ConstructionTag{});
    Handle work(constructed.release());
    work->destroy = [](DeferredReleaseNode* node) noexcept { delete static_cast<LayerRowsWork*>(node); };
    work->bytes = admission._bytes;
    work->_source = std::move(source);
    work->_collapsed = std::move(collapsed);
    work->_hiddenPattern = std::move(hiddenPattern);
    work->_rows.reserve(admission._count);
    work->_displayText.reserve(admission._count);
    work->_idTable.assign(admission._slots, kEmpty);
    work->_rowByLayer.assign(admission._count, kEmpty);
    work->_lookupOrder.resize(admission._count);
    std::vector<std::size_t> lookupScratch(admission._count);
    std::vector<std::size_t> heads(admission._count, kEmpty);
    std::vector<std::size_t> next(admission._count, kEmpty);
    std::vector<std::uint8_t> isCollapsed(admission._count, 0);
    std::uint64_t allocated = sizeof(LayerRowsWork) + 12 * kAllocationAllowance;
    const auto charge = [&](std::uint64_t count, std::uint64_t element) noexcept -> bool
    { return !element || (count <= kMaximum / element && Add(allocated, count * element)); };
    if (!charge(work->_collapsed.capacity(), sizeof(Guid)) ||
        !charge(work->_hiddenPattern.capacity(), sizeof(wchar_t)) ||
        !charge(work->_rows.capacity(), sizeof(HierarchyEntry)) ||
        !charge(work->_displayText.capacity(), sizeof(std::wstring)) ||
        !charge(work->_idTable.capacity(), sizeof(std::size_t)) ||
        !charge(work->_rowByLayer.capacity(), sizeof(std::size_t)) ||
        !charge(work->_lookupOrder.capacity(), sizeof(std::size_t)) ||
        !charge(lookupScratch.capacity(), sizeof(std::size_t)) || !charge(heads.capacity(), sizeof(std::size_t)) ||
        !charge(next.capacity(), sizeof(std::size_t)) || !charge(isCollapsed.capacity(), sizeof(std::uint8_t)) ||
        allocated > admission._metadataBytes)
        return {};
    for (std::size_t index = 0; index < admission._count; ++index)
    {
        if (cancellation.IsCancelled())
            return {};
        const auto& id = work->_source.layers[index].id;
        auto slot = std::hash<Guid>{}(id) & (work->_idTable.size() - 1);
        while (work->_idTable[slot] != kEmpty)
        {
            if (cancellation.IsCancelled() || work->_source.layers[work->_idTable[slot]].id == id)
                return {}; // malformed duplicate IDs cannot alias row authority
            slot = (slot + 1) & (work->_idTable.size() - 1);
        }
        work->_idTable[slot] = index;
    }
    // Cancellable bottom-up merge prepares a sorted owner lookup. No recursion or comparison callback can
    // leave a partial order published; adversarial Guid hashes affect worker work only, never owner lookup.
    for (std::size_t index = 0; index < admission._count; ++index)
        work->_lookupOrder[index] = index;
    for (std::size_t width = 1; width < admission._count; width *= 2)
    {
        for (std::size_t first = 0; first < admission._count; first += 2 * width)
        {
            const auto middle = (std::min)(first + width, admission._count);
            const auto end = (std::min)(first + 2 * width, admission._count);
            auto left = first;
            auto right = middle;
            for (auto output = first; output < end; ++output)
            {
                if (cancellation.IsCancelled())
                    return {};
                const bool takeLeft =
                    right == end || (left < middle && work->_source.layers[work->_lookupOrder[left]].id.bytes <
                                                          work->_source.layers[work->_lookupOrder[right]].id.bytes);
                lookupScratch[output] = work->_lookupOrder[takeLeft ? left++ : right++];
            }
        }
        work->_lookupOrder.swap(lookupScratch);
    }
    const auto findLayer = [&](const Guid& id) noexcept -> std::size_t
    {
        auto slot = std::hash<Guid>{}(id) & (work->_idTable.size() - 1);
        while (work->_idTable[slot] != kEmpty)
        {
            if (cancellation.IsCancelled())
                return kEmpty;
            const auto index = work->_idTable[slot];
            if (work->_source.layers[index].id == id)
                return index;
            slot = (slot + 1) & (work->_idTable.size() - 1);
        }
        return kEmpty;
    };
    for (const auto& id : work->_collapsed)
    {
        if (cancellation.IsCancelled())
            return {};
        const auto index = findLayer(id);
        if (index != kEmpty)
            isCollapsed[index] = 1;
    }
    std::size_t root = kEmpty;
    for (std::size_t index = 0; index < admission._count; ++index)
    {
        if (cancellation.IsCancelled())
            return {};
        const auto& layer = work->_source.layers[index];
        if (layer.parentID)
        {
            const auto parent = findLayer(layer.parentID.value());
            if (parent != kEmpty)
            {
                next[index] = heads[parent];
                heads[parent] = index;
            }
        }
        else
        {
            next[index] = root;
            root = index;
        }
    }
    std::array<Frame, kMaxGroupDepth + 1> stack{};
    std::size_t frames = 1;
    stack[0].next = root;
    while (frames != 0)
    {
        if (cancellation.IsCancelled())
            return {};
        auto& frame = stack[frames - 1];
        if (frame.next == kEmpty)
        {
            --frames;
            continue;
        }
        const auto index = frame.next;
        frame.next = next[index];
        const auto& layer = work->_source.layers[index];
        const bool visible = frame.visible && layer.visible;
        const bool collapsedGroup = layer.isGroup && isCollapsed[index] != 0;
        const auto rowIndex = work->_rows.size();
        work->_rows.push_back({layer.id, layer.name, frame.depth, visible, layer.isGroup, collapsedGroup,
                               layer.mask.has_value(), !layer.mask || layer.mask->enabled});
        const auto name = layer.name.empty() ? layer.id.ToString() : layer.name;
        auto& text = work->_displayText.emplace_back();
        if (visible)
            text = name;
        else
        {
            text.reserve(name.size() + work->_hiddenPattern.size());
            AppendHiddenText(text, work->_hiddenPattern, name);
        }
        // name is one transient per-row copy, already covered by the sealed-name and fallback allowances.
        if (!charge(work->_rows.back().name.capacity(), sizeof(wchar_t)) || !charge(text.capacity(), sizeof(wchar_t)))
            return {};
        std::uint64_t withName = allocated;
        if (!Add(withName, static_cast<std::uint64_t>(name.capacity()) * sizeof(wchar_t)) ||
            withName > admission._metadataBytes)
            return {};
        work->_rowByLayer[index] = rowIndex;
        if (observer.checkpoint)
            observer.checkpoint(observer.context, work->_rows.size());
        if (cancellation.IsCancelled())
            return {};
        if (layer.isGroup && !collapsedGroup && frame.depth < kMaxGroupDepth && heads[index] != kEmpty)
        {
            const int depth = frame.depth + 1;
            stack[frames++] = {heads[index], depth, visible};
        }
    }
    return work;
}

std::optional<std::size_t> LayerRowsWork::FindVisibleRow(const Guid& id) const noexcept
{
    std::size_t first = 0;
    std::size_t end = _lookupOrder.size();
    while (first < end)
    {
        const auto middle = first + (end - first) / 2;
        const auto layer = _lookupOrder[middle];
        const auto& candidate = _source.layers[layer].id;
        if (candidate == id)
        {
            const auto row = _rowByLayer[layer];
            return row == kEmpty ? std::nullopt : std::optional<std::size_t>(row);
        }
        if (candidate.bytes < id.bytes)
            first = middle + 1;
        else
            end = middle;
    }
    return std::nullopt;
}

void LayerRowsWork::Release::operator()(LayerRowsWork* rows) const noexcept
{
    if (rows && !TryDeferRelease(*rows, DeferredReleasePolicy::AlwaysOnOwner))
        delete rows;
}

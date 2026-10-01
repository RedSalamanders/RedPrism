#include "Document.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

DocumentHistory::DocumentHistory(int entryLimit, std::uint64_t retainedByteLimit) noexcept
    : _entryLimit(std::max(0, entryLimit)), _retainedByteLimit(retainedByteLimit)
{
    _savedRevision = _revision;
}

void DocumentHistory::Begin(std::wstring name, const std::optional<CanvasDocument>& document,
                            const std::optional<Guid>& selection)
{
    if (_depth == 0)
    {
        _pending = Snapshot{document, selection, _revision};
        _pendingName = std::move(name);
    }
    ++_depth;
}

void DocumentHistory::End(const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection)
{
    if (_depth <= 0)
    {
        return;
    }
    --_depth;
    if (_depth != 0 || !_pending.has_value())
    {
        return;
    }
    Snapshot before = std::move(_pending.value());
    _pending.reset();
    if (before.document == document)
    {
        return;
    }
    _revision = Guid::Generate();
    _past.push_back(Entry{std::move(_pendingName), std::move(before), Snapshot{document, selection, _revision}});
    _future.clear();
    Trim(document);
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::Undo()
{
    if (!CanUndo())
    {
        return std::nullopt;
    }
    Entry entry = std::move(_past.back());
    _past.pop_back();
    // Trim may erase the destination entry. Keep the one required restore copy independent of both stacks.
    Snapshot restored = entry.before;
    _future.push_back(std::move(entry));
    _revision = restored.revision;
    Trim(restored.document);
    return restored;
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::Redo()
{
    if (!CanRedo())
    {
        return std::nullopt;
    }
    Entry entry = std::move(_future.back());
    _future.pop_back();
    Snapshot restored = entry.after;
    _past.push_back(std::move(entry));
    _revision = restored.revision;
    Trim(restored.document);
    return restored;
}

void DocumentHistory::MarkSaved() noexcept
{
    MarkSaved(_revision);
}

void DocumentHistory::MarkSaved(const Guid& revision) noexcept
{
    _savedRevision = revision;
}

void DocumentHistory::Reset()
{
    _past.clear();
    _future.clear();
    _pending.reset();
    _depth = 0;
    _revision = Guid::Generate();
    _savedRevision = _revision;
}

const std::wstring& DocumentHistory::UndoName() const noexcept
{
    static const std::wstring empty;
    return _past.empty() ? empty : _past.back().name;
}

const std::wstring& DocumentHistory::RedoName() const noexcept
{
    static const std::wstring empty;
    return _future.empty() ? empty : _future.back().name;
}

namespace
{
// A painted layer shares its base and untouched tiles with earlier snapshots, and a snapshot shares unedited
// layers with the document around it: each live storage counts once, the first time `seen` meets it. Also how
// RetainedBytes seeds `seen` with the current document's own storages (with a throwaway `bytes`), so they are
// never counted as retained by history. Selection identities are held by the same snapshots and charged once
// from their construction-time capacity cache; no point traversal or borrowed identity survives this call.
void AddSnapshotBytes(const std::optional<CanvasDocument>& document, std::unordered_set<const void*>& seen,
                      std::size_t& bytes)
{
    if (!document.has_value())
    {
        return;
    }
    const auto add = [&](const Raster& raster)
    {
        raster.ForEachStorage(
            [&](const Raster& storage)
            {
                const void* identity = storage.Identity();
                if (identity != nullptr && seen.insert(identity).second)
                {
                    bytes += storage.ByteSize();
                }
            });
    };
    for (const ImageLayer& layer : document.value().layers)
    {
        if (layer.asset.has_value())
        {
            add(layer.asset.value().image);
            add(layer.asset.value().thumbnail);
        }
        if (layer.mask.has_value())
        {
            add(layer.mask.value().asset.image);
            add(layer.mask.value().asset.thumbnail);
        }
    }
    if (document.value().selection.has_value())
    {
        const SelectionLoops& loops = document.value().selection.value().loops;
        const void* identity = loops.Identity();
        if (identity != nullptr && seen.insert(identity).second)
        {
            bytes += static_cast<std::size_t>(loops.StorageBytes());
        }
    }
}
} // namespace

std::uint64_t DocumentHistory::EditMemoryBytes(const std::optional<CanvasDocument>& current) const noexcept
{
    std::uint64_t bytes = 64 * 1024;
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size != 0 && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    const auto references = [&](this const auto& self, const Raster& raster, unsigned int depth) noexcept -> bool
    {
        if (!raster.HasStorage())
            return true;
        if (depth > 128 || !add(1, 128))
            return false;
        // Counting occurrences bounds the deduplicating trim set's entries, buckets and rehash overlap without
        // allocating an admission set. Every held identity may repeat; conservative overcount is intentional.
        if (!self(raster.Base(), depth + 1))
            return false;
        for (const auto& tile : raster.Tiles())
            if (!self(tile.pixels, depth + 1))
                return false;
        return true;
    };
    const auto snapshot = [&](const std::optional<CanvasDocument>& document, bool copies) noexcept
    {
        if (!document)
            return true;
        for (const auto& layer : document->layers)
        {
            if (!references(layer.shapeImage, 0) ||
                (layer.asset && (!references(layer.asset->image, 0) || !references(layer.asset->thumbnail, 0))) ||
                (layer.mask &&
                 (!references(layer.mask->asset.image, 0) || !references(layer.mask->asset.thumbnail, 0))))
                return false;
            if (!copies)
                continue;
            if (!add(8, sizeof(ImageLayer) + 256) || !add(layer.name.size() + 1, 8 * sizeof(wchar_t)) ||
                (layer.asset && !add(layer.asset->name.size() + 1, 8 * sizeof(wchar_t))) ||
                (layer.mask && !add(layer.mask->asset.name.size() + 1, 8 * sizeof(wchar_t))))
                return false;
            if (layer.adjustment)
                for (const auto& channel : layer.adjustment->curves.channels)
                    if (!add(channel.size(), 8 * sizeof(CurvePoint)))
                        return false;
        }
        return !document->selection || add(1, 128); // retained geometry is shared, only its set entry is new
    };
    if (!snapshot(current, true) || (_pending && !snapshot(_pending->document, false)) ||
        !add(_past.size() + 1, 4 * sizeof(Entry)) || !add(_past.size() + _future.size() + 1, 2 * sizeof(const Entry*)))
        return 0;
    for (const auto& entry : _past)
        if (!snapshot(entry.before.document, false) || !snapshot(entry.after.document, false))
            return 0;
    for (const auto& entry : _future)
        if (!snapshot(entry.before.document, false) || !snapshot(entry.after.document, false))
            return 0;
    return bytes;
}

std::size_t DocumentHistory::RetainedBytes(const std::optional<CanvasDocument>& current) const
{
    std::unordered_set<const void*> seen;
    std::size_t discard = 0;
    AddSnapshotBytes(current, seen, discard);
    std::size_t bytes = 0;
    for (const Entry& entry : _past)
    {
        AddSnapshotBytes(entry.before.document, seen, bytes);
        AddSnapshotBytes(entry.after.document, seen, bytes);
    }
    for (const Entry& entry : _future)
    {
        AddSnapshotBytes(entry.before.document, seen, bytes);
        AddSnapshotBytes(entry.after.document, seen, bytes);
    }
    return bytes;
}

void DocumentHistory::Trim(const std::optional<CanvasDocument>& current)
{
    // The oldest-first removal order Undo/Redo already see: every _past entry (oldest to newest), then every
    // _future entry (oldest to newest).
    std::vector<const Entry*> removalOrder;
    removalOrder.reserve(_past.size() + _future.size());
    for (const Entry& entry : _past)
    {
        removalOrder.push_back(&entry);
    }
    for (const Entry& entry : _future)
    {
        removalOrder.push_back(&entry);
    }
    // Keeping more entries never frees storage or drops a count, so the newest run that fits the budget is found
    // in one backward pass over removalOrder (each entry visited, and RetainedBytes'd, once) instead of
    // recomputing the whole document's retained bytes after every single removal.
    std::unordered_set<const void*> seen;
    std::size_t discard = 0;
    AddSnapshotBytes(current, seen, discard);
    std::size_t bytes = 0;
    std::size_t keep = 0;
    const std::size_t entryLimit = static_cast<std::size_t>(_entryLimit);
    const std::uint64_t byteLimit = _retainedByteLimit;
    for (std::size_t index = removalOrder.size(); index > 0; --index)
    {
        if (keep + 1 > entryLimit)
        {
            break;
        }
        const Entry& candidate = *removalOrder[index - 1];
        std::size_t trialBytes = bytes;
        AddSnapshotBytes(candidate.before.document, seen, trialBytes);
        AddSnapshotBytes(candidate.after.document, seen, trialBytes);
        if (trialBytes > byteLimit)
        {
            break;
        }
        bytes = trialBytes;
        ++keep;
    }
    std::size_t drop = removalOrder.size() - keep;
    const std::size_t dropFromPast = std::min(drop, _past.size());
    _past.erase(_past.begin(), _past.begin() + static_cast<std::ptrdiff_t>(dropFromPast));
    drop -= dropFromPast;
    _future.erase(_future.begin(), _future.begin() + static_cast<std::ptrdiff_t>(drop));
}

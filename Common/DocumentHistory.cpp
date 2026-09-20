#include "DocumentHistory.h"

#include <algorithm>
#include <unordered_set>

DocumentHistory::DocumentHistory(int entryLimit, int retainedByteLimit) noexcept
    : _entryLimit(std::max(0, entryLimit)), _retainedByteLimit(std::max(0, retainedByteLimit))
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
    Snapshot before = _pending.value();
    _pending.reset();
    if (before.document == document)
    {
        return;
    }
    _revision = Guid::Generate();
    _past.push_back(Entry{_pendingName, std::move(before), Snapshot{document, selection, _revision}});
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
    _future.push_back(entry);
    _revision = entry.before.revision;
    Trim(entry.before.document);
    return entry.before;
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::Redo()
{
    if (!CanRedo())
    {
        return std::nullopt;
    }
    Entry entry = std::move(_future.back());
    _future.pop_back();
    _past.push_back(entry);
    _revision = entry.after.revision;
    Trim(entry.after.document);
    return entry.after;
}

void DocumentHistory::MarkSaved() noexcept
{
    _savedRevision = _revision;
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
void CollectLiveIdentities(const std::optional<CanvasDocument>& current, std::unordered_set<const void*>& seen)
{
    if (!current.has_value())
    {
        return;
    }
    for (const ImageLayer& layer : current.value().layers)
    {
        if (layer.asset.has_value())
        {
            seen.insert(layer.asset.value().image.Identity());
            seen.insert(layer.asset.value().thumbnail.Identity());
        }
        if (layer.mask.has_value())
        {
            seen.insert(layer.mask.value().asset.image.Identity());
            seen.insert(layer.mask.value().asset.thumbnail.Identity());
        }
    }
}

void AddSnapshotBytes(const std::optional<CanvasDocument>& document, std::unordered_set<const void*>& seen,
                      std::size_t& bytes)
{
    if (!document.has_value())
    {
        return;
    }
    const auto add = [&](const Raster& raster)
    {
        const void* identity = raster.Identity();
        if (identity == nullptr)
        {
            return;
        }
        if (seen.insert(identity).second)
        {
            bytes += raster.ByteSize();
        }
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
}
} // namespace

std::size_t DocumentHistory::RetainedBytes(const std::optional<CanvasDocument>& current) const
{
    std::unordered_set<const void*> seen;
    CollectLiveIdentities(current, seen);
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
    while (_past.size() + _future.size() > static_cast<std::size_t>(_entryLimit) ||
           RetainedBytes(current) > static_cast<std::size_t>(_retainedByteLimit))
    {
        if (!_past.empty())
        {
            _past.erase(_past.begin());
        }
        else if (!_future.empty())
        {
            _future.erase(_future.begin());
        }
        else
        {
            break;
        }
    }
}

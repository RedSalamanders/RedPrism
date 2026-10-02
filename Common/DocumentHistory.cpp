#include "Document.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

DocumentHistory::DocumentHistory(int entryLimit, std::uint64_t retainedByteLimit) noexcept
    : _entryLimit(std::max(0, entryLimit)), _retainedByteLimit(retainedByteLimit)
{
    _savedRevision = _revision;
}

DocumentHistory::StateView DocumentHistory::CaptureState() const noexcept
{
    StateView view;
    view.past = _past;
    view.future = _future;
    view.pending = _pending;
    view.owner = this;
    view.revision = _revision;
    view.epoch = _stateEpoch;
    view.depth = _depth;
    view.entryLimit = _entryLimit;
    view.byteLimit = _retainedByteLimit;
    return view;
}

bool DocumentHistory::IsCurrent(const StateView& view) const noexcept
{
    return view.owner == this && view.epoch == _stateEpoch && view.revision == _revision &&
           (!view.requirePredecessor || (view.precedingPast.Identity() == _past.Identity() &&
                                         view.precedingFuture.Identity() == _future.Identity() &&
                                         view.precedingPending == _pending && view.precedingDepth == _depth));
}

const std::wstring& DocumentHistory::StateView::UndoName() const noexcept
{
    static const std::wstring empty;
    return past.empty() ? empty : past.back()->name;
}

const std::wstring& DocumentHistory::StateView::RedoName() const noexcept
{
    static const std::wstring empty;
    return future.empty() ? empty : future.back()->name;
}

namespace
{
[[nodiscard]] bool HistoryPrepareCheckpoint(DocumentHistory::PrepareObserver observer,
                                            DocumentHistory::PrepareStage stage, CancelToken cancellation)
{
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

[[nodiscard]] std::optional<bool> HistoryDocumentsEqual(const std::optional<CanvasDocument>& left,
                                                        const std::optional<CanvasDocument>& right,
                                                        CancelToken cancellation) noexcept
{
    if (cancellation.IsCancelled())
        return std::nullopt;
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    if (left->id != right->id || left->width != right->width || left->height != right->height ||
        left->resolution != right->resolution || left->layers.size() != right->layers.size() ||
        left->selection.has_value() != right->selection.has_value())
        return false;
    for (std::size_t index = 0; index < left->layers.size(); ++index)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        if (left->layers[index] != right->layers[index])
            return false;
    }
    if (left->selection)
    {
        const auto& a = left->selection.value();
        const auto& b = right->selection.value();
        if (a.antialiased != b.antialiased)
            return false;
        if (a.loops.Identity() != b.loops.Identity())
        {
            if (a.loops.size() != b.loops.size())
                return false;
            auto first = a.loops.begin();
            auto second = b.loops.begin();
            for (; first != a.loops.end(); ++first, ++second)
            {
                if (first->size() != second->size())
                    return false;
                for (std::size_t point = 0; point < first->size(); ++point)
                {
                    if ((point & 255u) == 0 && cancellation.IsCancelled())
                        return std::nullopt;
                    if ((*first)[point] != (*second)[point])
                        return false;
                }
            }
        }
    }
    return cancellation.IsCancelled() ? std::nullopt : std::optional<bool>{true};
}

// Value snapshots remain until H2. Their record/string allocations happen on a worker, with a cancellation
// checkpoint before each bounded layer record. Selection geometry and all pixel storage stay immutable/shared.
[[nodiscard]] bool CopyHistoryDocument(const std::optional<CanvasDocument>& source,
                                       std::optional<CanvasDocument>& destination, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (!source)
        return true;
    CanvasDocument copy;
    copy.id = source->id;
    copy.width = source->width;
    copy.height = source->height;
    copy.resolution = source->resolution;
    copy.selection = source->selection;
    copy.layers.reserve(source->layers.size());
    for (const auto& layer : source->layers)
    {
        if (cancellation.IsCancelled())
            return false;
        copy.layers.push_back(layer);
    }
    destination = std::move(copy);
    return !cancellation.IsCancelled();
}
} // namespace

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareEdit(
    StateView view, const std::optional<CanvasDocument>& before, const std::optional<Guid>& beforeActive,
    const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive, std::wstring name,
    CancelToken cancellation, PrepareObserver observer)
{
    if (!view.owner || cancellation.IsCancelled() || view.depth < 0 || (view.depth != 0 && !view.pending))
        return std::nullopt;
    Publication result;
    result.expectedEpoch = view.epoch;
    result.expectedRevision = view.revision;
    result.state = std::move(view);
    // Begin/End within an existing transaction does not close it or replace its opening snapshot/name.
    if (result.state.depth != 0)
        return result;
    const auto same = HistoryDocumentsEqual(before, after, cancellation);
    if (!same)
        return std::nullopt;
    if (same.value())
        return result;
    Snapshot opening{{}, beforeActive, result.expectedRevision};
    Snapshot finished{{}, afterActive, Guid::Generate()};
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::BeforeSnapshot, cancellation) ||
        !CopyHistoryDocument(before, opening.document, cancellation))
        return std::nullopt;
    auto openingOwner = std::make_shared<const Snapshot>(std::move(opening));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::AfterSnapshot, cancellation) ||
        !CopyHistoryDocument(after, finished.document, cancellation))
        return std::nullopt;
    auto finishedOwner = std::make_shared<const Snapshot>(std::move(finished));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::Entry, cancellation))
        return std::nullopt;
    auto entry = std::make_shared<const Entry>(std::move(name), std::move(openingOwner), std::move(finishedOwner));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StackGrowth, cancellation))
        return std::nullopt;
    result.state.past.push_back(entry);
    result.state.future.clear();
    result.state.revision = entry->after->revision;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, after, cancellation))
        return std::nullopt;
    return result;
}

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareRestore(StateView view, bool redo,
                                                                            CancelToken cancellation,
                                                                            PrepareObserver observer)
{
    if (!view.owner || view.depth != 0 || cancellation.IsCancelled() ||
        (redo ? view.future.empty() : view.past.empty()))
        return std::nullopt;
    Publication result;
    result.expectedEpoch = view.epoch;
    result.expectedRevision = view.revision;
    result.state = std::move(view);
    auto& source = redo ? result.state.future : result.state.past;
    auto& destination = redo ? result.state.past : result.state.future;
    const auto entry = source.back();
    const auto& snapshot = *(redo ? entry->after : entry->before);
    Snapshot restore{{}, snapshot.activeLayerID, snapshot.revision};
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::RestoreSnapshot, cancellation) ||
        !CopyHistoryDocument(snapshot.document, restore.document, cancellation))
        return std::nullopt;
    result.restore = std::move(restore); // independent owner survives even if trim discards both entry handles
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StackGrowth, cancellation))
        return std::nullopt;
    destination.push_back(entry);
    source.pop_back();
    result.state.revision = snapshot.revision;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, result.restore->document, cancellation))
        return std::nullopt;
    return result;
}

std::optional<DocumentHistory::ReplacementPublication> DocumentHistory::PrepareReplacement(
    StateView view, const std::optional<CanvasDocument>& baseline, const std::optional<Guid>& baselineActive,
    const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive, std::wstring name,
    CancelToken cancellation, PrepareObserver observer)
{
    if (!view.owner || view.depth < 0 || (view.depth != 0 && !view.pending) ||
        !HistoryPrepareCheckpoint(observer, PrepareStage::ReplacementReset, cancellation))
        return std::nullopt;
    const Guid expectedRevision = view.revision;
    const std::uint64_t expectedEpoch = view.epoch;
    const Guid baselineRevision = Guid::Generate();
    view.past.clear();
    view.future.clear();
    view.pending.reset();
    view.depth = 0;
    view.revision = baselineRevision;
    auto prepared = PrepareEdit(std::move(view), baseline, baselineActive, after, afterActive, std::move(name),
                                cancellation, observer);
    if (!prepared || cancellation.IsCancelled())
        return std::nullopt;
    // PrepareEdit recorded its opening against the new saved baseline; only publication validates the old lineage.
    prepared->expectedRevision = expectedRevision;
    prepared->expectedEpoch = expectedEpoch;
    return ReplacementPublication(std::move(prepared.value()), baselineRevision);
}

bool DocumentHistory::CanPublishReplacement(const ReplacementPublication& publication) const noexcept
{
    return CanPublish(publication.history);
}

std::optional<DocumentHistory::ReplacementPublication> DocumentHistory::PrepareSavedReplacement(
    StateView view, const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive,
    std::wstring name, CancelToken cancellation, PrepareObserver observer)
{
    auto result = PrepareReplacement(std::move(view), std::nullopt, std::nullopt, after, afterActive, std::move(name),
                                     cancellation, observer);
    if (result)
        result->savedRevision = result->history.state.revision;
    return result;
}

bool DocumentHistory::PublishReplacement(ReplacementPublication& publication) noexcept
{
    if (!Publish(publication.history))
        return false;
    // Unlike an edit/restore, validated document replacement deliberately installs a new saved lineage.
    _savedRevision = publication.savedRevision;
    return true;
}

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareBegin(StateView view,
                                                                          const std::optional<CanvasDocument>& before,
                                                                          const std::optional<Guid>& active,
                                                                          std::wstring name, CancelToken cancellation,
                                                                          PrepareObserver observer)
{
    if (!view.owner || view.depth < 0 || view.depth == (std::numeric_limits<int>::max)() ||
        cancellation.IsCancelled() || (view.depth != 0 && !view.pending))
        return std::nullopt;
    Publication result;
    result.expectedEpoch = view.epoch;
    result.expectedRevision = view.revision;
    result.state = std::move(view);
    if (result.state.depth == 0)
    {
        Snapshot opening{{}, active, result.expectedRevision};
        if (!HistoryPrepareCheckpoint(observer, PrepareStage::PendingSnapshot, cancellation) ||
            !CopyHistoryDocument(before, opening.document, cancellation))
            return std::nullopt;
        auto openingOwner = std::make_shared<const Snapshot>(std::move(opening));
        if (!HistoryPrepareCheckpoint(observer, PrepareStage::PendingRecord, cancellation))
            return std::nullopt;
        result.state.pending = std::make_shared<const Pending>(std::move(name), std::move(openingOwner));
    }
    ++result.state.depth;
    if (cancellation.IsCancelled())
        return std::nullopt;
    return result;
}

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareEnd(StateView view,
                                                                        const std::optional<CanvasDocument>& after,
                                                                        const std::optional<Guid>& active,
                                                                        CancelToken cancellation,
                                                                        PrepareObserver observer)
{
    if (!view.owner || view.depth <= 0 || !view.pending || cancellation.IsCancelled())
        return std::nullopt;
    Publication result;
    result.expectedEpoch = view.epoch;
    result.expectedRevision = view.revision;
    result.state = std::move(view);
    --result.state.depth;
    if (result.state.depth != 0)
        return result;
    const auto pending = std::move(result.state.pending);
    const auto same = HistoryDocumentsEqual(pending->before->document, after, cancellation);
    if (!same)
        return std::nullopt;
    if (same.value())
        return result;
    Snapshot finished{{}, active, Guid::Generate()};
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::AfterSnapshot, cancellation) ||
        !CopyHistoryDocument(after, finished.document, cancellation))
        return std::nullopt;
    auto finishedOwner = std::make_shared<const Snapshot>(std::move(finished));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::Entry, cancellation))
        return std::nullopt;
    const auto entry = std::make_shared<const Entry>(pending->name, pending->before, std::move(finishedOwner));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StackGrowth, cancellation))
        return std::nullopt;
    result.state.past.push_back(entry);
    result.state.future.clear();
    result.state.revision = entry->after->revision;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, after, cancellation))
        return std::nullopt;
    return result;
}

bool DocumentHistory::CanPublish(const Publication& publication) const noexcept
{
    return !publication.published && publication.state.owner == this && publication.expectedEpoch == _stateEpoch &&
           publication.expectedRevision == _revision &&
           (!publication.state.requirePredecessor ||
            (publication.state.precedingPast.Identity() == _past.Identity() &&
             publication.state.precedingFuture.Identity() == _future.Identity() &&
             publication.state.precedingPending == _pending && publication.state.precedingDepth == _depth));
}

std::optional<DocumentHistory::StateView> DocumentHistory::FollowingPreparedState(
    const Publication& publication) noexcept
{
    if (publication.published || !publication.state.owner || publication.state.epoch != publication.expectedEpoch ||
        publication.expectedEpoch == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    auto state = publication.state;
    state.epoch = publication.expectedEpoch + 1;
    state.requirePredecessor = true;
    state.precedingPast = publication.state.past;
    state.precedingFuture = publication.state.future;
    state.precedingPending = publication.state.pending;
    state.precedingDepth = publication.state.depth;
    return state;
}

bool DocumentHistory::Publish(Publication& publication) noexcept
{
    if (!CanPublish(publication))
        return false;
    _past.swap(publication.state.past);
    _future.swap(publication.state.future);
    _pending.swap(publication.state.pending);
    std::swap(_depth, publication.state.depth);
    std::swap(_revision, publication.state.revision);
    ++_stateEpoch;
    publication.published = true;
    // Saved revision belongs to the owner and can have changed while this publication was prepared.
    return true;
}

void DocumentHistory::Begin(std::wstring name, const std::optional<CanvasDocument>& document,
                            const std::optional<Guid>& selection)
{
    if (_depth == 0)
    {
        _pending = std::make_shared<const Pending>(
            std::move(name), std::make_shared<const Snapshot>(Snapshot{document, selection, _revision}));
    }
    ++_depth;
    ++_stateEpoch;
}

void DocumentHistory::End(const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection)
{
    if (_depth <= 0)
    {
        return;
    }
    --_depth;
    ++_stateEpoch;
    if (_depth != 0 || !_pending)
    {
        return;
    }
    const auto pending = std::move(_pending);
    _pending.reset();
    if (pending->before->document == document)
    {
        return;
    }
    const Guid revision = Guid::Generate();
    _past.push_back(std::make_shared<const Entry>(
        pending->name, pending->before, std::make_shared<const Snapshot>(Snapshot{document, selection, revision})));
    _revision = revision;
    _future.clear();
    Trim(document);
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::Undo()
{
    if (!CanUndo())
    {
        return std::nullopt;
    }
    const EntryHandle entry = _past.back();
    // Trim may erase the destination entry. Keep the one required restore copy independent of both stacks.
    Snapshot restored = *entry->before;
    _future.push_back(entry);
    _past.pop_back();
    _revision = restored.revision;
    ++_stateEpoch;
    Trim(restored.document);
    return restored;
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::Redo()
{
    if (!CanRedo())
    {
        return std::nullopt;
    }
    const EntryHandle entry = _future.back();
    Snapshot restored = *entry->after;
    _past.push_back(entry);
    _future.pop_back();
    _revision = restored.revision;
    ++_stateEpoch;
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
    ++_stateEpoch;
}

const std::wstring& DocumentHistory::UndoName() const noexcept
{
    static const std::wstring empty;
    return _past.empty() ? empty : _past.back()->name;
}

const std::wstring& DocumentHistory::RedoName() const noexcept
{
    static const std::wstring empty;
    return _future.empty() ? empty : _future.back()->name;
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
    const auto references = [&](const Raster& raster) noexcept
    {
        if (!raster.HasStorage())
            return true;
        // Counting occurrences bounds the deduplicating trim set's entries, buckets and rehash overlap without
        // allocating an admission set. Cached node counts avoid expanding repeatedly shared sparse DAGs on the
        // owner. Every held identity may repeat; conservative overcount is intentional until H3's exact index.
        const auto nodes = raster.RetirementNodeCount();
        return nodes && add(nodes, 128);
    };
    const auto snapshot = [&](const std::optional<CanvasDocument>& document, bool copies) noexcept
    {
        if (!document)
            return true;
        for (const auto& layer : document->layers)
        {
            if (!references(layer.shapeImage) ||
                (layer.asset && (!references(layer.asset->image) || !references(layer.asset->thumbnail))) ||
                (layer.mask && (!references(layer.mask->asset.image) || !references(layer.mask->asset.thumbnail))))
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
    if (!snapshot(current, true) || (_pending && !snapshot(_pending->before->document, false)) ||
        !add(_past.size() + 1, 4 * sizeof(Entry)) || !add(4, sizeof(HistoryEntries<EntryHandle>::Container) + 64) ||
        !add(_past.size() + _future.size() + 1, 8 * sizeof(EntryHandle)))
        return 0;
    for (const auto& entry : _past)
        if (!snapshot(entry->before->document, false) || !snapshot(entry->after->document, false))
            return 0;
    for (const auto& entry : _future)
        if (!snapshot(entry->before->document, false) || !snapshot(entry->after->document, false))
            return 0;
    return bytes;
}

std::uint64_t DocumentHistory::RestoreMemoryBytes(const std::optional<CanvasDocument>& current,
                                                  bool redo) const noexcept
{
    auto bytes = EditMemoryBytes(current);
    if (!bytes)
        return 0;
    const auto& stack = redo ? _future : _past;
    if (stack.empty())
        return bytes;
    const auto& restore = redo ? stack.back()->after->document : stack.back()->before->document;
    if (!restore)
        return bytes;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > maximum / size) || count * size > maximum - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    // The selected restore snapshot can contain far more metadata than the live document. Charge its copied
    // records/strings/curves separately; immutable pixels and selection geometry remain shared.
    for (const auto& layer : restore->layers)
    {
        if (!add(8, sizeof(ImageLayer) + 256) || !add(layer.name.size() + 1, 8 * sizeof(wchar_t)) ||
            (layer.asset && !add(layer.asset->name.size() + 1, 8 * sizeof(wchar_t))) ||
            (layer.mask && !add(layer.mask->asset.name.size() + 1, 8 * sizeof(wchar_t))))
            return 0;
        if (layer.adjustment)
            for (const auto& channel : layer.adjustment->curves.channels)
                if (!add(channel.size(), 8 * sizeof(CurvePoint)))
                    return 0;
    }
    return bytes;
}

std::uint64_t DocumentHistory::RetirementMemoryBytes(const std::optional<CanvasDocument>& current,
                                                     const Raster& additional) const noexcept
{
    std::uint64_t bytes = 64 * 1024; // root/control records and rounding not separately exposed by their owners
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    const auto raster = [&](const Raster& image) noexcept
    { return !image.HasStorage() || (image.RetirementMemoryBytes() && add(1, image.RetirementMemoryBytes())); };
    const auto document = [&](const std::optional<CanvasDocument>& value) noexcept
    {
        if (!value)
            return true;
        if (!add(1, sizeof(CanvasDocument)) || !add(value->layers.capacity(), sizeof(ImageLayer)) ||
            (value->selection && !add(1, value->selection->loops.StorageBytes())))
            return false;
        for (const auto& layer : value->layers)
        {
            if (!add(layer.name.capacity() + 1, sizeof(wchar_t)) || !raster(layer.shapeImage))
                return false;
            if (layer.asset && (!add(layer.asset->name.capacity() + 1, sizeof(wchar_t)) ||
                                !raster(layer.asset->image) || !raster(layer.asset->thumbnail)))
                return false;
            if (layer.mask && (!add(layer.mask->asset.name.capacity() + 1, sizeof(wchar_t)) ||
                               !raster(layer.mask->asset.image) || !raster(layer.mask->asset.thumbnail)))
                return false;
            if (layer.adjustment)
                for (const auto& channel : layer.adjustment->curves.channels)
                    if (!add(channel.capacity(), sizeof(CurvePoint)))
                        return false;
        }
        return true;
    };
    const auto snapshot = [&](const std::shared_ptr<const Snapshot>& value) noexcept
    { return !value || (add(1, sizeof(Snapshot) + 64) && document(value->document)); };
    const auto entry = [&](const EntryHandle& value) noexcept
    {
        return add(1, sizeof(Entry) + 64) && add(value->name.capacity() + 1, sizeof(wchar_t)) &&
               snapshot(value->before) && snapshot(value->after);
    };
    if (!document(current) || !raster(additional) || !add(2, sizeof(HistoryEntries<EntryHandle>::Container) + 64) ||
        !add(_past.capacity() + _future.capacity(), sizeof(EntryHandle)) ||
        (_pending && (!add(1, sizeof(Pending) + 64) || !add(_pending->name.capacity() + 1, sizeof(wchar_t)) ||
                      !snapshot(_pending->before))))
        return 0;
    for (const auto& value : _past)
        if (!entry(value))
            return 0;
    for (const auto& value : _future)
        if (!entry(value))
            return 0;
    return bytes;
}

std::size_t DocumentHistory::RetainedBytes(const std::optional<CanvasDocument>& current) const
{
    std::unordered_set<const void*> seen;
    std::size_t discard = 0;
    AddSnapshotBytes(current, seen, discard);
    std::size_t bytes = 0;
    for (const EntryHandle& entry : _past)
    {
        AddSnapshotBytes(entry->before->document, seen, bytes);
        AddSnapshotBytes(entry->after->document, seen, bytes);
    }
    for (const EntryHandle& entry : _future)
    {
        AddSnapshotBytes(entry->before->document, seen, bytes);
        AddSnapshotBytes(entry->after->document, seen, bytes);
    }
    return bytes;
}

void DocumentHistory::Trim(const std::optional<CanvasDocument>& current)
{
    // The oldest-first removal order Undo/Redo already see: every _past entry (oldest to newest), then every
    // _future entry (oldest to newest).
    std::vector<const Entry*> removalOrder;
    removalOrder.reserve(_past.size() + _future.size());
    for (const EntryHandle& entry : _past)
    {
        removalOrder.push_back(entry.get());
    }
    for (const EntryHandle& entry : _future)
    {
        removalOrder.push_back(entry.get());
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
        AddSnapshotBytes(candidate.before->document, seen, trialBytes);
        AddSnapshotBytes(candidate.after->document, seen, trialBytes);
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

bool DocumentHistory::TrimPrepared(StateView& state, const std::optional<CanvasDocument>& current,
                                   CancelToken cancellation)
{
    std::unordered_set<const void*> seen;
    const auto snapshotBytes = [&](const std::optional<CanvasDocument>& document, std::size_t& bytes) -> bool
    {
        if (!document)
            return !cancellation.IsCancelled();
        const auto storageBytes = [&](const Raster& raster) -> bool
        {
            if (cancellation.IsCancelled())
                return false;
            if (raster.HasStorage() && seen.insert(raster.Identity()).second)
            {
                const auto amount = raster.ByteSize();
                if (amount > (std::numeric_limits<std::size_t>::max)() - bytes)
                    return false;
                bytes += amount;
            }
            return true;
        };
        // Match today's ForEachStorage charge (raster, contiguous base, replacement windows). H3 owns the
        // complete cached storage index and nested-sparse closure; this slice does not invent different trim math.
        const auto rasterBytes = [&](const Raster& raster) -> bool
        {
            if (!storageBytes(raster) || !storageBytes(raster.Base()))
                return false;
            for (const auto& tile : raster.Tiles())
                if (!storageBytes(tile.pixels))
                    return false;
            return true;
        };
        for (const auto& layer : document->layers)
        {
            if (cancellation.IsCancelled() ||
                (layer.asset && (!rasterBytes(layer.asset->image) || !rasterBytes(layer.asset->thumbnail))) ||
                (layer.mask && (!rasterBytes(layer.mask->asset.image) || !rasterBytes(layer.mask->asset.thumbnail))))
                return false;
        }
        if (document->selection)
        {
            const auto& loops = document->selection->loops;
            if (loops.Identity() && seen.insert(loops.Identity()).second)
            {
                const auto amount = loops.StorageBytes();
                if (amount > (std::numeric_limits<std::size_t>::max)() - bytes)
                    return false;
                bytes += static_cast<std::size_t>(amount);
            }
        }
        return !cancellation.IsCancelled();
    };
    std::vector<const Entry*> removalOrder;
    removalOrder.reserve(state.past.size() + state.future.size());
    for (const auto& entry : state.past)
        removalOrder.push_back(entry.get());
    for (const auto& entry : state.future)
        removalOrder.push_back(entry.get());
    std::size_t discard = 0;
    if (!snapshotBytes(current, discard))
        return false;
    std::size_t retained = 0;
    std::size_t keep = 0;
    for (std::size_t index = removalOrder.size(); index > 0; --index)
    {
        if (cancellation.IsCancelled())
            return false;
        if (keep + 1 > static_cast<std::size_t>(state.entryLimit))
            break;
        const auto& entry = *removalOrder[index - 1];
        auto trial = retained;
        if (!snapshotBytes(entry.before->document, trial) || !snapshotBytes(entry.after->document, trial))
            return false;
        if (trial > state.byteLimit)
            break;
        retained = trial;
        ++keep;
    }
    auto drop = removalOrder.size() - keep;
    const auto dropPast = (std::min)(drop, state.past.size());
    state.past.erase(state.past.begin(), state.past.begin() + static_cast<std::ptrdiff_t>(dropPast));
    drop -= dropPast;
    state.future.erase(state.future.begin(), state.future.begin() + static_cast<std::ptrdiff_t>(drop));
    return !cancellation.IsCancelled();
}

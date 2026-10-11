#include "Document.h"
#include "PublicationObject.h"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::shared_ptr<const HistoryStorageTopology> HistoryStorageTopology::Prepare(
    const std::optional<CanvasDocument>& document, CancelToken cancellation,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (cancellation.IsCancelled())
        return {};
    auto result =
        MakePublicationObject<HistoryStorageTopology>(std::move(memory), kDeferredReleaseThreshold, ConstructionTag{});
    result->_hasDocument = document.has_value();
    std::unordered_set<const void*> seen;
    std::vector<Raster> pending;
    const auto queue = [&](const Raster& raster)
    {
        if (raster.HasStorage() && seen.insert(raster.Identity()).second)
            pending.push_back(raster);
    };
    if (document)
    {
        result->_layers = document->layers;
        if (document->selection)
            result->_selection = document->selection->loops;
        for (const auto& layer : document->layers)
        {
            if (cancellation.IsCancelled())
                return {};
            queue(layer.shapeImage);
            if (layer.asset)
            {
                queue(layer.asset->image);
                queue(layer.asset->thumbnail);
            }
            if (layer.mask)
            {
                queue(layer.mask->asset.image);
                queue(layer.mask->asset.thumbnail);
            }
        }
    }
    // Every key is backed by a pending or completed Raster owner. Iterative closure visits each storage once,
    // including sparse tiles of sparse tiles, without recursion or materializing any pixel allocation.
    while (!pending.empty())
    {
        if (cancellation.IsCancelled())
            return {};
        Raster raster = std::move(pending.back());
        pending.pop_back();
        queue(raster.Base());
        for (const auto& tile : raster.Tiles())
        {
            if (cancellation.IsCancelled())
                return {};
            queue(tile.pixels);
        }
        result->_rasters.push_back(std::move(raster));
    }
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (result->_rasters.capacity() <= (maximum - sizeof(HistoryStorageTopology) - 64) / sizeof(Raster))
    {
        result->_metadataBytes = sizeof(HistoryStorageTopology) + 64 + result->_rasters.capacity() * sizeof(Raster);
        const auto source = document ? document->RetirementMemoryBytes() : 0;
        if ((!document || source) && source <= maximum - result->_metadataBytes)
            result->_retirementBytes = source + result->_metadataBytes;
    }
    return cancellation.IsCancelled() ? nullptr : result;
}

bool HistoryStorageTopology::Matches(const std::optional<CanvasDocument>& document) const noexcept
{
    return _hasDocument == document.has_value() &&
           (!document ||
            (_layers.Identity() == document->layers.Identity() &&
             _selection.Identity() == (document->selection ? document->selection->loops.Identity() : nullptr)));
}

std::shared_ptr<const HistoryStorageTopology> DocumentHistory::PrepareTopology(
    const std::optional<CanvasDocument>& document, const StateView& state, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return {};
    const std::array candidates{state.pending ? state.pending->before : nullptr,
                                state.past.empty() ? nullptr : state.past.back()->before,
                                state.past.empty() ? nullptr : state.past.back()->after,
                                state.future.empty() ? nullptr : state.future.back()->before,
                                state.future.empty() ? nullptr : state.future.back()->after};
    for (const auto& candidate : candidates)
        if (candidate && candidate->storageTopology && candidate->storageTopology->Matches(document))
            return candidate->storageTopology;
    return HistoryStorageTopology::Prepare(document, cancellation, state.publicationMemory);
}

std::shared_ptr<const DocumentHistory::Snapshot> DocumentHistory::SealSnapshot(Snapshot snapshot,
                                                                               const StateView& state,
                                                                               CancelToken cancellation)
{
    snapshot.storageTopology = PrepareTopology(snapshot.document, state, cancellation);
    if (!snapshot.storageTopology || cancellation.IsCancelled())
        return {};
    return MakePublicationObject<const Snapshot>(state.publicationMemory, kDeferredReleaseThreshold,
                                                 std::move(snapshot));
}

DocumentHistory::DocumentHistory(int entryLimit, std::uint64_t retainedByteLimit) noexcept
    : _entryLimit(std::max(0, entryLimit)), _retainedByteLimit(retainedByteLimit)
{
    _savedRevision = _revision;
}

DocumentHistory::Publication::~Publication() noexcept
{
    if (residencyPermit)
        residencyPermit->Reset();
}

DocumentHistory::Publication::Publication(Publication&& source) noexcept
{
    if (source.residencyPermit)
        source.residencyPermit->Reset();
    state = std::move(source.state);
    restore = std::move(source.restore);
    source.restore.reset();
    expectedRevision = source.expectedRevision;
    expectedEpoch = source.expectedEpoch;
    published = std::exchange(source.published, true);
    sampledResidency = std::exchange(source.sampledResidency, false);
    requiresResidencyPermit = std::exchange(source.requiresResidencyPermit, false);
}

DocumentHistory::Publication& DocumentHistory::Publication::operator=(Publication&& source) noexcept
{
    if (this != &source)
    {
        if (residencyPermit)
            residencyPermit->Reset();
        if (source.residencyPermit)
            source.residencyPermit->Reset();
        state = std::move(source.state);
        restore = std::move(source.restore);
        source.restore.reset();
        expectedRevision = source.expectedRevision;
        expectedEpoch = source.expectedEpoch;
        published = std::exchange(source.published, true);
        sampledResidency = std::exchange(source.sampledResidency, false);
        requiresResidencyPermit = std::exchange(source.requiresResidencyPermit, false);
    }
    return *this;
}

DocumentHistory::StateView DocumentHistory::CaptureState() const noexcept
{
    StateView view;
    view.past = _past;
    view.future = _future;
    view.pending = _pending;
    view.storageIndex = _storageIndex;
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

DocumentHistory::Authority DocumentHistory::CaptureAuthority() const noexcept
{
    Authority result;
    result._owner = this;
    result._epoch = _stateEpoch;
    result._revision = _revision;
    return result;
}

bool DocumentHistory::IsCurrent(const Authority& authority) const noexcept
{
    return authority._owner == this && authority._epoch == _stateEpoch && authority._revision == _revision;
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
    if (left->layers.Identity() != right->layers.Identity())
    {
        for (std::size_t index = 0; index < left->layers.size(); ++index)
        {
            if (cancellation.IsCancelled())
                return std::nullopt;
            if (left->layers[index] != right->layers[index])
                return false;
        }
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

// A document snapshot shares immutable records and selection. Entry construction/accounting remains worker
// work; this capture does not copy layer records or strings, including when its destination already exists.
[[nodiscard]] bool CopyHistoryDocument(const std::optional<CanvasDocument>& source,
                                       std::optional<CanvasDocument>& destination, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (!source)
        return true;
    destination = source;
    return !cancellation.IsCancelled();
}
} // namespace

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareEdit(
    StateView view, const std::optional<CanvasDocument>& before, const std::optional<Guid>& beforeActive,
    const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive, std::wstring name,
    CancelToken cancellation, PrepareObserver observer, std::shared_ptr<const SharedPublicationMemory> memory)
{
    view.publicationMemory = std::move(memory);
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
    auto openingOwner = SealSnapshot(std::move(opening), result.state, cancellation);
    if (!openingOwner)
        return std::nullopt;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::AfterSnapshot, cancellation) ||
        !CopyHistoryDocument(after, finished.document, cancellation))
        return std::nullopt;
    auto finishedOwner = SealSnapshot(std::move(finished), result.state, cancellation);
    if (!finishedOwner)
        return std::nullopt;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::Entry, cancellation))
        return std::nullopt;
    auto entry = MakePublicationObject<const Entry>(result.state.publicationMemory, kDeferredReleaseThreshold,
                                                    std::move(name), std::move(openingOwner), std::move(finishedOwner));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StackGrowth, cancellation))
        return std::nullopt;
    result.state.past.push_back(entry, result.state.publicationMemory);
    result.state.future.clear();
    result.state.revision = entry->after->revision;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, after, cancellation, observer))
        return std::nullopt;
    result.sampledResidency = true;
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
    restore.storageTopology = snapshot.storageTopology;
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
        !TrimPrepared(result.state, result.restore->document, cancellation, observer))
        return std::nullopt;
    result.sampledResidency = true;
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
    view.storageIndex.reset();
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
        auto openingOwner = SealSnapshot(std::move(opening), result.state, cancellation);
        if (!openingOwner)
            return std::nullopt;
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
    auto finishedOwner = SealSnapshot(std::move(finished), result.state, cancellation);
    if (!finishedOwner)
        return std::nullopt;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::Entry, cancellation))
        return std::nullopt;
    const auto entry = std::make_shared<const Entry>(pending->name, pending->before, std::move(finishedOwner));
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StackGrowth, cancellation))
        return std::nullopt;
    result.state.past.push_back(entry, result.state.publicationMemory);
    result.state.future.clear();
    result.state.revision = entry->after->revision;
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, after, cancellation, observer))
        return std::nullopt;
    result.sampledResidency = true;
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
    if (!CanPublish(publication) || (publication.requiresResidencyPermit && !publication.residencyPermit))
        return false;
    _past.swap(publication.state.past);
    _future.swap(publication.state.future);
    _pending.swap(publication.state.pending);
    _storageIndex.swap(publication.state.storageIndex);
    std::swap(_depth, publication.state.depth);
    std::swap(_revision, publication.state.revision);
    ++_stateEpoch;
    publication.published = true;
    if (publication.residencyPermit)
        publication.residencyPermit->Reset();
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
    _storageIndex.reset(); // Reference mutation has no detached index update; the next preparation rebuilds it.
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
    _storageIndex.reset();
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
    _storageIndex.reset();
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
    _storageIndex.reset();
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
// Keys never outlive the immutable topology that owns them. Charges are sampled on the worker/reference
// lane at use time; topology publication does not cache a resident total or authorize late-cache races.
bool AddTopologyBytes(const HistoryStorageTopology& topology, std::unordered_set<const void*>& seen, std::size_t& bytes,
                      CancelToken cancellation = {})
{
    const auto add = [&](const void* identity, std::uint64_t amount)
    {
        if (!identity || !seen.insert(identity).second)
            return true;
        if (amount > (std::numeric_limits<std::size_t>::max)() - bytes)
            return false;
        bytes += static_cast<std::size_t>(amount);
        return true;
    };
    for (const auto& raster : topology.RasterOwners())
        if (cancellation.IsCancelled() || !add(raster.Identity(), raster.ByteSize()))
            return false;
    const auto& selection = topology.SelectionOwner();
    return !cancellation.IsCancelled() && add(selection.Identity(), selection.StorageBytes());
}

std::shared_ptr<const HistoryStorageTopology> SnapshotTopology(const DocumentHistory::Snapshot& snapshot,
                                                               CancelToken cancellation = {})
{
    if (snapshot.storageTopology && snapshot.storageTopology->Matches(snapshot.document))
        return snapshot.storageTopology;
    return HistoryStorageTopology::Prepare(snapshot.document, cancellation);
}

void AddDocumentBytes(const std::optional<CanvasDocument>& document, std::unordered_set<const void*>& seen,
                      std::size_t& bytes)
{
    if (!document)
        return;
    // Reference callers keep one document scan, without constructing a topology object or retained owner
    // vector. Reuse their existing dedup set for iterative nested closure; no recursion or pixel assembly.
    std::vector<Raster> pending;
    const auto queue = [&](const Raster& raster)
    {
        if (raster.HasStorage() && seen.insert(raster.Identity()).second)
            pending.push_back(raster);
    };
    for (const auto& layer : document->layers)
    {
        queue(layer.shapeImage);
        if (layer.asset)
        {
            queue(layer.asset->image);
            queue(layer.asset->thumbnail);
        }
        if (layer.mask)
        {
            queue(layer.mask->asset.image);
            queue(layer.mask->asset.thumbnail);
        }
    }
    while (!pending.empty())
    {
        Raster raster = std::move(pending.back());
        pending.pop_back();
        const auto amount = raster.ByteSize();
        if (amount > (std::numeric_limits<std::size_t>::max)() - bytes)
        {
            bytes = (std::numeric_limits<std::size_t>::max)();
            return;
        }
        bytes += amount;
        queue(raster.Base());
        for (const auto& tile : raster.Tiles())
            queue(tile.pixels);
    }
    if (document->selection)
    {
        const auto& loops = document->selection->loops;
        if (loops.Identity() && seen.insert(loops.Identity()).second)
        {
            const auto amount = loops.StorageBytes();
            bytes = amount > (std::numeric_limits<std::size_t>::max)() - bytes
                        ? (std::numeric_limits<std::size_t>::max)()
                        : bytes + static_cast<std::size_t>(amount);
        }
    }
}

void AddSnapshotBytes(const DocumentHistory::Snapshot& snapshot, std::unordered_set<const void*>& seen,
                      std::size_t& bytes)
{
    if (snapshot.storageTopology && snapshot.storageTopology->Matches(snapshot.document))
    {
        if (!AddTopologyBytes(*snapshot.storageTopology, seen, bytes))
            bytes = (std::numeric_limits<std::size_t>::max)();
        return;
    }
    // Compatibility/reference callers do not construct an index on the owner. Their single scan uses
    // complete iterative closure; migrating these synchronous callers remains H3/native activation work.
    AddDocumentBytes(snapshot.document, seen, bytes);
}

} // namespace

namespace
{
class HistoryBoundSum final
{
  public:
    explicit HistoryBoundSum(std::uint64_t header) noexcept : bounds{header, header, 0} {}
    [[nodiscard]] bool Add(std::uint64_t& total, std::uint64_t amount) noexcept
    {
        if (amount > (std::numeric_limits<std::uint64_t>::max)() - total)
            return false;
        total += amount;
        return true;
    }
    [[nodiscard]] bool Child(const std::optional<RecordOwnershipBounds>& child) noexcept
    {
        return child && child->ownershipBytes >= child->metadataBytes &&
               Add(bounds.metadataBytes, child->metadataBytes) && Add(bounds.ownershipBytes, child->ownershipBytes) &&
               Add(bounds.rasterNodes, child->rasterNodes);
    }
    [[nodiscard]] bool Text(const std::wstring& text) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (text.capacity() > (maximum - 64) / sizeof(wchar_t) - 1)
            return false;
        const auto bytes = (text.capacity() + 1) * sizeof(wchar_t) + 64;
        return Add(bounds.metadataBytes, bytes) && Add(bounds.ownershipBytes, bytes);
    }
    RecordOwnershipBounds bounds;
};
} // namespace

#pragma warning(push)
#pragma warning(disable : 4324) // intrusive worker retirement requires 16-byte alignment
struct DocumentHistory::StorageIndex final : DeferredReleaseNode
{
    struct RasterRecord final
    {
        Raster owner;
        std::uint64_t references = 0;
        std::uint64_t charge = 0;
        bool live = false;
    };
    struct SelectionRecord final
    {
        SelectionLoops owner;
        std::uint64_t references = 0;
        std::uint64_t charge = 0;
        bool live = false;
    };
    struct EntryRecord final
    {
        EntryHandle owner;
        std::shared_ptr<const HistoryStorageTopology> before;
        std::shared_ptr<const HistoryStorageTopology> after;
    };
    struct Retire final
    {
        void operator()(StorageIndex* index) const noexcept
        {
            index->DeferredReleaseNode::bytes = index->ownershipBytes;
            index->destroy = [](DeferredReleaseNode* node) noexcept
            { std::unique_ptr<StorageIndex> owned{static_cast<StorageIndex*>(node)}; };
            if (!TryDeferRelease(*index, DeferredReleasePolicy::AlwaysOnOwner))
                index->destroy(index);
        }
    };

    std::shared_ptr<const SharedPublicationMemory> publicationMemory; // Destroy maps/owners before accounting.
    std::unordered_map<const void*, RasterRecord> rasters;
    std::unordered_map<const void*, SelectionRecord> selections;
    std::unordered_map<const Entry*, EntryRecord> entries;
    std::shared_ptr<const HistoryStorageTopology> current;
    std::uint64_t metadataBytes = 0;
    std::uint64_t ownershipBytes = 0;
    std::uint64_t retainedBytes = 0;
    std::uint64_t residencyEpoch = 0;

    [[nodiscard]] static std::unique_ptr<StorageIndex> Update(
        const std::shared_ptr<const StorageIndex>& previous, std::span<const EntryHandle> desired,
        std::shared_ptr<const HistoryStorageTopology> currentTopology, CancelToken cancellation,
        PrepareObserver observer)
    {
        if (!currentTopology || !HistoryPrepareCheckpoint(observer, PrepareStage::StorageIndexClone, cancellation))
            return {};
        // Only the producer clones these maps. Published indexes have no mutable reader API and every key
        // owns its entry/raster/selection. Moving an entry between Undo and Redo does not change membership.
        // A partial producer never escapes this worker preparation or uses a copied retirement bound.
        // Install shared ownership/the deferred deleter only after final capacities have been sealed.
        auto result = previous ? std::make_unique<StorageIndex>(*previous) : std::make_unique<StorageIndex>();
        std::unordered_set<const Entry*> wanted;
        wanted.reserve(desired.size());
        for (const auto& entry : desired)
        {
            if (cancellation.IsCancelled() || !entry || !wanted.insert(entry.get()).second)
                return {};
        }
        for (auto it = result->entries.begin(); it != result->entries.end();)
        {
            if (cancellation.IsCancelled())
                return {};
            if (wanted.contains(it->first))
                ++it;
            else
            {
                if (!result->ChangeTopology(*it->second.before, false, cancellation) ||
                    !result->ChangeTopology(*it->second.after, false, cancellation))
                    return {};
                it = result->entries.erase(it);
                if (!HistoryPrepareCheckpoint(observer, PrepareStage::StorageIndexMembership, cancellation))
                    return {};
            }
        }
        for (const auto& entry : desired)
        {
            if (cancellation.IsCancelled())
                return {};
            if (result->entries.contains(entry.get()))
                continue;
            auto before = SnapshotTopology(*entry->before, cancellation);
            auto after = SnapshotTopology(*entry->after, cancellation);
            if (!before || !after || !result->ChangeTopology(*before, true, cancellation) ||
                !result->ChangeTopology(*after, true, cancellation))
                return {};
            result->entries.emplace(entry.get(), EntryRecord{entry, std::move(before), std::move(after)});
            if (!HistoryPrepareCheckpoint(observer, PrepareStage::StorageIndexMembership, cancellation))
                return {};
        }
        if (!result->SetCurrent(std::move(currentTopology), cancellation) || !result->SampleCharges(cancellation))
            return {};
        return result;
    }

    [[nodiscard]] bool ChangeTopology(const HistoryStorageTopology& topology, bool add, CancelToken cancellation)
    {
        const auto change = [this, add](auto& records, const auto& owner) -> bool
        {
            const auto identity = owner.Identity();
            if (!identity)
                return true;
            auto it = records.find(identity);
            if (add)
            {
                if (it == records.end())
                    it = records
                             .try_emplace(
                                 identity,
                                 typename std::remove_reference_t<decltype(records)>::mapped_type{owner, 0, 0, false})
                             .first;
                if (it->second.references == (std::numeric_limits<std::uint64_t>::max)())
                    return false;
                ++it->second.references;
            }
            else
            {
                if (it == records.end() || !it->second.references)
                    return false;
                --it->second.references;
                if (!it->second.references && !it->second.live)
                {
                    if (it->second.charge > retainedBytes)
                        return false;
                    retainedBytes -= it->second.charge;
                    records.erase(it);
                }
            }
            return true;
        };
        for (const auto& raster : topology.RasterOwners())
            if (cancellation.IsCancelled() || !change(rasters, raster))
                return false;
        return !cancellation.IsCancelled() && change(selections, topology.SelectionOwner());
    }

    [[nodiscard]] bool SetCurrent(std::shared_ptr<const HistoryStorageTopology> topology, CancelToken cancellation)
    {
        if (current == topology)
            return !cancellation.IsCancelled();
        const auto mark = [](auto& records, const auto& owner, bool live)
        {
            const auto identity = owner.Identity();
            if (!identity)
                return;
            auto it = records.find(identity);
            if (live && it == records.end())
                it = records
                         .try_emplace(
                             identity,
                             typename std::remove_reference_t<decltype(records)>::mapped_type{owner, 0, 0, false})
                         .first;
            if (it != records.end())
            {
                it->second.live = live;
                if (!live && !it->second.references)
                    records.erase(it);
            }
        };
        if (current)
        {
            for (const auto& raster : current->RasterOwners())
            {
                if (cancellation.IsCancelled())
                    return false;
                mark(rasters, raster, false);
            }
            mark(selections, current->SelectionOwner(), false);
        }
        current = std::move(topology);
        for (const auto& raster : current->RasterOwners())
        {
            if (cancellation.IsCancelled())
                return false;
            mark(rasters, raster, true);
        }
        mark(selections, current->SelectionOwner(), true);
        return !cancellation.IsCancelled();
    }

    [[nodiscard]] bool SampleCharges(CancelToken cancellation)
    {
        if (cancellation.IsCancelled())
            return false;
        auto residency = Raster::AcquireResidencyLease(cancellation);
        if (!residency.Held())
            return false;
        retainedBytes = 0;
        const auto sample = [&](auto& records, const auto& bytes) -> bool
        {
            for (auto& [identity, record] : records)
            {
                (void)identity;
                if (cancellation.IsCancelled())
                    return false;
                record.charge = record.live ? 0 : bytes(record.owner);
                if (record.charge > (std::numeric_limits<std::uint64_t>::max)() - retainedBytes)
                    return false;
                retainedBytes += record.charge;
            }
            return true;
        };
        if (!sample(rasters, [](const Raster& raster) { return raster.ByteSize(); }) ||
            !sample(selections, [](const SelectionLoops& selection) { return selection.StorageBytes(); }))
            return false;
        residencyEpoch = Raster::ResidencyEpoch();
        return !cancellation.IsCancelled();
    }

    [[nodiscard]] bool RemoveEntry(const Entry* entry, CancelToken cancellation)
    {
        const auto it = entries.find(entry);
        if (it == entries.end() || !ChangeTopology(*it->second.before, false, cancellation) ||
            !ChangeTopology(*it->second.after, false, cancellation))
            return false;
        entries.erase(it);
        return true;
    }

    [[nodiscard]] bool CacheBounds(CancelToken cancellation)
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        metadataBytes = sizeof(StorageIndex) + 64;
        ownershipBytes = 0;
        const auto add = [](std::uint64_t& total, std::uint64_t count, std::uint64_t size) noexcept
        {
            constexpr auto limit = (std::numeric_limits<std::uint64_t>::max)();
            if ((size && count > limit / size) || count * size > limit - total)
                return false;
            total += count * size;
            return true;
        };
        const auto map = [&](const auto& records) -> bool
        {
            using Map = std::remove_cvref_t<decltype(records)>;
            return add(metadataBytes, records.bucket_count(), sizeof(void*)) &&
                   add(metadataBytes, records.size(), sizeof(typename Map::value_type) + 64) &&
                   add(metadataBytes, 1, 64);
        };
        if (!map(rasters) || !map(selections) || !map(entries))
            return false;
        std::unordered_set<const void*> roots;
        std::unordered_set<const HistoryStorageTopology*> topologies;
        const auto topology = [&](const std::shared_ptr<const HistoryStorageTopology>& value) -> bool
        {
            if (!value || cancellation.IsCancelled())
                return false;
            if (!topologies.insert(value.get()).second)
                return true;
            if (!add(metadataBytes, 1, value->MetadataBytes()))
                return false;
            const auto& records = value->RecordOwner();
            if (!records.Identity() || !roots.insert(records.Identity()).second)
                return true;
            const auto bounds = records.OwnershipBounds();
            return bounds && add(metadataBytes, 1, bounds->metadataBytes);
        };
        if (!topology(current))
            return false;
        for (const auto& [identity, entry] : entries)
        {
            (void)identity;
            if (cancellation.IsCancelled() || !topology(entry.before) || !topology(entry.after) ||
                entry.owner->name.capacity() > (maximum - 64) / sizeof(wchar_t) - 1 ||
                !add(metadataBytes, 1, sizeof(Entry) + 64 + 2 * (sizeof(Snapshot) + 64)) ||
                !add(metadataBytes, entry.owner->name.capacity() + 1, sizeof(wchar_t)) || !add(metadataBytes, 1, 64))
                return false;
        }
        for (const auto& [identity, selection] : selections)
        {
            (void)identity;
            if (cancellation.IsCancelled() || !add(metadataBytes, 1, 64) ||
                !add(metadataBytes, selection.owner.size(), 64))
                return false;
        }
        ownershipBytes = metadataBytes;
        for (const auto& [identity, raster] : rasters)
        {
            (void)identity;
            const auto storageBytes = raster.owner.StorageRetirementMemoryBytes();
            if (cancellation.IsCancelled() || !storageBytes || !add(ownershipBytes, 1, storageBytes))
                return false;
        }
        for (const auto& [identity, selection] : selections)
        {
            (void)identity;
            if (cancellation.IsCancelled() || !add(ownershipBytes, 1, selection.owner.StorageBytes()))
                return false;
        }
        return !cancellation.IsCancelled();
    }

    [[nodiscard]] std::size_t CurrentRetainedBytes() const noexcept
    {
        std::size_t total = 0;
        const auto add = [&](std::uint64_t amount) noexcept
        {
            if (amount > (std::numeric_limits<std::size_t>::max)() - total)
                return false;
            total += static_cast<std::size_t>(amount);
            return true;
        };
        for (const auto& [identity, raster] : rasters)
        {
            (void)identity;
            if (!raster.live && !add(raster.owner.ByteSize()))
                return (std::numeric_limits<std::size_t>::max)();
        }
        for (const auto& [identity, selection] : selections)
        {
            (void)identity;
            if (!selection.live && !add(selection.owner.StorageBytes()))
                return (std::numeric_limits<std::size_t>::max)();
        }
        return total;
    }
};
#pragma warning(pop)

std::optional<RecordOwnershipBounds> DocumentHistory::Snapshot::OwnershipBounds() const noexcept
{
    HistoryBoundSum sum(sizeof(Snapshot) + 64);
    if (document)
    {
        const auto sourceBytes = document->RetirementMemoryBytes();
        const auto layers = document->layers.OwnershipBounds();
        if (!sourceBytes || (!layers && document->layers.Identity()))
            return std::nullopt;
        if ((layers && (!sum.Add(sum.bounds.metadataBytes, layers->metadataBytes) ||
                        !sum.Add(sum.bounds.rasterNodes, layers->rasterNodes))) ||
            !sum.Add(sum.bounds.ownershipBytes, sourceBytes) ||
            (document->selection && document->selection->loops.Identity() && !sum.Add(sum.bounds.rasterNodes, 1)))
            return std::nullopt;
    }
    if (storageTopology)
    {
        if (!storageTopology->Matches(document) || !storageTopology->RetirementMemoryBytes() ||
            !sum.Add(sum.bounds.metadataBytes, storageTopology->MetadataBytes()) ||
            !sum.Add(sum.bounds.ownershipBytes, storageTopology->MetadataBytes()))
            return std::nullopt;
    }
    return sum.bounds;
}

std::optional<RecordOwnershipBounds> DocumentHistory::Entry::OwnershipBounds() const noexcept
{
    HistoryBoundSum sum(sizeof(Entry) + 64);
    if (!sum.Text(name) || !before || !after || !sum.Child(before->OwnershipBounds()) ||
        !sum.Child(after->OwnershipBounds()))
        return std::nullopt;
    return sum.bounds;
}

std::optional<RecordOwnershipBounds> DocumentHistory::Pending::OwnershipBounds() const noexcept
{
    HistoryBoundSum sum(sizeof(Pending) + 64);
    if (!sum.Text(name) || !before || !sum.Child(before->OwnershipBounds()))
        return std::nullopt;
    return sum.bounds;
}

std::uint64_t DocumentHistory::EditMemoryBytes(const std::optional<CanvasDocument>& current) const noexcept
{
    std::uint64_t bytes = 64 * 1024;
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    // Detached map cloning, changed memberships and bounded producer dedup/scratch. The index's cached
    // metadata includes its retained bucket/node capacities; reading this bound does not visit its owners.
    if (_storageIndex)
    {
        if (!_storageIndex->metadataBytes || !add(4, _storageIndex->metadataBytes) ||
            !add(_storageIndex->rasters.size(), 128) || !add(_storageIndex->selections.size(), 128))
            return 0;
    }
    else
    {
        const auto past = _past.OwnershipBounds();
        const auto future = _future.OwnershipBounds();
        if (!past || !future || !add(past->rasterNodes, 128) || !add(future->rasterNodes, 128))
            return 0;
    }
    if (_pending)
    {
        const auto pending = _pending->OwnershipBounds();
        if (!pending || !add(pending->rasterNodes, 128))
            return 0;
    }
    if (current)
    {
        const auto layers = current->layers.OwnershipBounds();
        if ((!layers && current->layers.Identity()) ||
            (layers && (!add(layers->rasterNodes, 128) || !add(8, layers->metadataBytes))) ||
            !add(current->layers.size(), 8 * 256) || (current->selection && !add(1, 128)))
            return 0;
    }
    // Buffer growth and trim bookkeeping retain the same conservative per-entry allowance. Metadata and
    // complete raster-node occurrence counts now come from immutable producer caches, never layer traversal.
    if (!add(_past.size() + 1, 4 * sizeof(Entry)) || !add(4, sizeof(HistoryEntries<EntryHandle>::Container) + 64) ||
        !add(_past.size() + _future.size() + 1, 8 * sizeof(EntryHandle)))
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
    // The selected snapshot may contain far more metadata than the live document. Its sealed root retains
    // complete names/curve/capacity bounds, including unedited record ownership, without a per-layer walk.
    const auto layers = restore->layers.OwnershipBounds();
    if ((!layers && restore->layers.Identity()) || (layers && !add(8, layers->metadataBytes)) ||
        !add(restore->layers.size(), 8 * 256))
        return 0;
    return bytes;
}

std::uint64_t DocumentHistory::RetirementMemoryBytes(const std::optional<CanvasDocument>& current,
                                                     const Raster& additional) const noexcept
{
    std::uint64_t bytes = 64 * 1024;
    const auto add = [&bytes](std::uint64_t amount) noexcept
    {
        if (amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += amount;
        return true;
    };
    if (_storageIndex)
    {
        const auto past = _past.StorageMetadataBytes();
        const auto future = _future.StorageMetadataBytes();
        if (!past || !future || !_storageIndex->ownershipBytes || !add(*past) || !add(*future) ||
            !add(_storageIndex->ownershipBytes))
            return 0;
    }
    else
    {
        const auto past = _past.OwnershipBounds();
        const auto future = _future.OwnershipBounds();
        if (!past || !future || !add(past->ownershipBytes) || !add(future->ownershipBytes))
            return 0;
    }
    if (current && (!_storageIndex || !_storageIndex->current || !_storageIndex->current->Matches(current)))
    {
        const auto source = current->RetirementMemoryBytes();
        if (!source || !add(source))
            return 0;
    }
    if (_pending)
    {
        const auto pending = _pending->OwnershipBounds();
        if (!pending || !add(pending->ownershipBytes))
            return 0;
    }
    if (additional.HasStorage())
    {
        const auto source = additional.RetirementMemoryBytes();
        if (!source || !add(source))
            return 0;
    }
    return bytes;
}

std::size_t DocumentHistory::RetainedBytes(const std::optional<CanvasDocument>& current) const
{
    if (_storageIndex && _storageIndex->current && _storageIndex->current->Matches(current))
        return _storageIndex->CurrentRetainedBytes();
    std::unordered_set<const void*> seen;
    std::size_t discard = 0;
    AddDocumentBytes(current, seen, discard);
    std::size_t bytes = 0;
    for (const EntryHandle& entry : _past)
    {
        AddSnapshotBytes(*entry->before, seen, bytes);
        AddSnapshotBytes(*entry->after, seen, bytes);
    }
    for (const EntryHandle& entry : _future)
    {
        AddSnapshotBytes(*entry->before, seen, bytes);
        AddSnapshotBytes(*entry->after, seen, bytes);
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
    AddDocumentBytes(current, seen, discard);
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
        AddSnapshotBytes(*candidate.before, seen, trialBytes);
        AddSnapshotBytes(*candidate.after, seen, trialBytes);
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
                                   CancelToken cancellation, PrepareObserver observer)
{
    const auto currentTopology = PrepareTopology(current, state, cancellation);
    if (!currentTopology)
        return false;
    std::vector<EntryHandle> removalOrder;
    removalOrder.reserve(state.past.size() + state.future.size());
    for (const auto& entry : state.past)
        removalOrder.push_back(entry);
    for (const auto& entry : state.future)
        removalOrder.push_back(entry);
    auto index = StorageIndex::Update(state.storageIndex, removalOrder, currentTopology, cancellation, observer);
    if (!index)
        return false;
    std::size_t drop = 0;
    while (removalOrder.size() - drop > static_cast<std::size_t>(state.entryLimit) ||
           index->retainedBytes > state.byteLimit)
    {
        if (cancellation.IsCancelled() || drop == removalOrder.size() ||
            !index->RemoveEntry(removalOrder[drop].get(), cancellation))
            return false;
        ++drop;
    }
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::StorageIndexBounds, cancellation) ||
        !index->CacheBounds(cancellation))
        return false;
    const auto dropPast = (std::min)(drop, state.past.size());
    state.past.erase(state.past.begin(), state.past.begin() + static_cast<std::ptrdiff_t>(dropPast),
                     state.publicationMemory);
    drop -= dropPast;
    state.future.erase(state.future.begin(), state.future.begin() + static_cast<std::ptrdiff_t>(drop),
                       state.publicationMemory);
    index->publicationMemory = state.publicationMemory;
    auto* raw = index.release();
    state.storageIndex = std::shared_ptr<const StorageIndex>{
        raw, StorageIndex::Retire{}, PublicationGraphAllocator<StorageIndex>{state.publicationMemory}};
    return !cancellation.IsCancelled();
}

DocumentHistory::ResidencyValidation DocumentHistory::ResidencyPermit::TryAcquire(Publication& publication) noexcept
{
    if (_publication || _lease.Held() || publication.published || !publication.state.owner)
        return ResidencyValidation::Invalid;
    if (!publication.sampledResidency)
        return ResidencyValidation::Ready; // Begin/nested/no-op does not publish a new resident trim decision
    if (!publication.state.storageIndex)
        return ResidencyValidation::Invalid;
    publication.requiresResidencyPermit = true;
    if (!_lease.TryAcquire())
        return ResidencyValidation::Busy;
    if (publication.state.storageIndex->residencyEpoch != Raster::ResidencyEpoch())
    {
        _lease.Reset();
        return ResidencyValidation::Refresh;
    }
    _publication = &publication;
    publication.residencyPermit = this;
    return ResidencyValidation::Ready;
}

void DocumentHistory::ResidencyPermit::Reset() noexcept
{
    if (_publication && _publication->residencyPermit == this)
        _publication->residencyPermit = nullptr;
    _publication = nullptr;
    _lease.Reset();
}

DocumentHistory::ResidencyValidation DocumentHistory::ResidencyPermit::TryAcquire(
    ReplacementPublication& publication) noexcept
{
    return TryAcquire(publication.history);
}

bool DocumentHistory::RefreshPreparedResidency(ReplacementPublication& publication,
                                               const std::optional<CanvasDocument>& current, CancelToken cancellation,
                                               PrepareObserver observer)
{
    return RefreshPreparedResidency(publication.history, current, cancellation, observer);
}

bool DocumentHistory::RefreshPreparedResidency(Publication& publication, const std::optional<CanvasDocument>& current,
                                               CancelToken cancellation, PrepareObserver observer)
{
    if (publication.published || !publication.state.owner || publication.residencyPermit || cancellation.IsCancelled())
        return false;
    if (!publication.sampledResidency)
        return true;
    if (!publication.state.storageIndex || !publication.state.storageIndex->current ||
        !publication.state.storageIndex->current->Matches(current))
        return false;
    // Contention alone does not invalidate the resident witness. Wait on the worker with its stop token,
    // then avoid cloning/rebuilding the index if no cache grew. A later growth is still caught by owner try.
    auto residency = Raster::AcquireResidencyLease(cancellation);
    if (!residency.Held())
        return false;
    if (publication.state.storageIndex->residencyEpoch == Raster::ResidencyEpoch())
        return !cancellation.IsCancelled();
    residency.Reset();
    auto candidate = publication.state;
    if (!TrimPrepared(candidate, current, cancellation, observer) || cancellation.IsCancelled())
        return false;
    publication.state = std::move(candidate);
    return true;
}

bool DocumentHistory::NeedsResidencyMaintenance() const noexcept
{
    return (!_past.empty() || !_future.empty()) &&
           (!_storageIndex || _storageIndex->residencyEpoch != Raster::ResidencyEpoch());
}

std::optional<DocumentHistory::Publication> DocumentHistory::PrepareResidencyMaintenance(
    StateView view, const std::optional<CanvasDocument>& current, CancelToken cancellation, PrepareObserver observer)
{
    if (!view.owner || cancellation.IsCancelled())
        return std::nullopt;
    Publication result;
    result.expectedEpoch = view.epoch;
    result.expectedRevision = view.revision;
    result.state = std::move(view);
    if (!HistoryPrepareCheckpoint(observer, PrepareStage::TrimIndex, cancellation) ||
        !TrimPrepared(result.state, current, cancellation, observer))
        return std::nullopt;
    result.sampledResidency = true;
    return result;
}

#include "Document.Tests.HistoryPublicationLifetime.h"

#include "DeferredRelease.h"
#include "Document.h"
#include "PublicationObject.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{
void Expect(bool condition, const char* label, int& failures)
{
    std::cout << (condition ? "PASS " : "FAIL ") << "history-publication-lifetime." << label << '\n';
    failures += condition ? 0 : 1;
}

struct FixedSink final
{
    std::array<DeferredReleaseNode*, 32> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::thread::id drainThread{};
    std::uint32_t destroyed = 0;

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& sink = *static_cast<FixedSink*>(context);
        if (sink.count == sink.nodes.size())
        {
            sink.overflow = true;
            return;
        }
        sink.nodes[sink.count++] = &node;
    }

    void DrainOnWorker() noexcept
    {
        std::thread worker(
            [this]() noexcept
            {
                drainThread = std::this_thread::get_id();
                for (std::size_t i = 0; i < count; ++i)
                {
                    auto* const node = nodes[i];
                    if (node && node->destroy)
                    {
                        node->destroy(node);
                        ++destroyed;
                    }
                    nodes[i] = nullptr;
                }
            });
        worker.join();
        count = 0;
    }
};

struct ValueTrace final
{
    std::atomic<std::uint32_t> destructions{0};
    std::thread::id destructionThread{};
};

struct Probe final
{
    Probe(ValueTrace* traceValue, int firstValue, int secondValue) noexcept
        : trace(traceValue), first(firstValue), second(secondValue)
    {
    }
    ~Probe() noexcept
    {
        trace->destructionThread = std::this_thread::get_id();
        trace->destructions.fetch_add(1, std::memory_order_release);
    }
    ValueTrace* trace;
    int first;
    int second;
};

struct ThrowingProbe final
{
    explicit ThrowingProbe(int* constructedValue, bool failConstruction)
    {
        ++*constructedValue;
        if (failConstruction)
            throw std::bad_alloc{};
    }
    ~ThrowingProbe() noexcept = default;
};

std::shared_ptr<const SharedPublicationMemory> NewMemory()
{
    return SharedPublicationMemory::CreateForWork();
}

void RunPublicationObjectControls(int& failures, FixedSink& sink)
{
    auto memory = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakMemory{memory};
    ValueTrace trace;
    auto first = MakePublicationObject<Probe>(memory, 73, &trace, 19, 41);
    std::weak_ptr<Probe> weakValue{first};
    auto second = first;
    first.reset();
    memory.reset();
    Expect(second && second->first == 19 && second->second == 41 && !weakValue.expired(),
           "helper.forwards-literal-constructor-args", failures);
    Expect(sink.count == 0 && trace.destructions.load(std::memory_order_acquire) == 0,
           "helper.copy-keeps-last-strong-alive", failures);

    second.reset();
    Expect(sink.count == 1 && !sink.overflow && weakValue.expired(), "helper.last-strong-defers-one-node-and-value",
           failures);
    Expect(!weakMemory.expired(), "helper.control-allocator-retains-memory-before-destroy", failures);
    sink.DrainOnWorker();
    Expect(sink.destroyed == 1 && sink.drainThread != std::this_thread::get_id() &&
               trace.destructions.load(std::memory_order_acquire) == 1 && trace.destructionThread == sink.drainThread,
           "helper.node-value-destroyed-on-real-worker", failures);
    Expect(!weakMemory.expired(), "helper.last-weak-keeps-memory-after-node-destroy", failures);
    // The empty weak handle still owns the control block until reset; use_count is the observable lifetime edge.
    weakValue.reset();
    Expect(weakMemory.expired(), "helper.last-weak-release-drops-memory", failures);

    ValueTrace referenceTrace;
    auto reference = MakePublicationObject<Probe>({}, 0, &referenceTrace, 7, 8);
    Expect(reference->first == 7 && reference->second == 8 && sink.count == 0, "helper.null-memory-reference-value",
           failures);
    reference.reset();
    Expect(sink.count == 0 && referenceTrace.destructions.load(std::memory_order_acquire) == 1 &&
               referenceTrace.destructionThread == std::this_thread::get_id(),
           "helper.null-memory-reference-destroys-inline", failures);

    auto aliasOwner = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakAliasOwner{aliasOwner};
    std::shared_ptr<const SharedPublicationMemory> nullAlias{aliasOwner, nullptr};
    aliasOwner.reset();
    const bool aliasIsNull = !nullAlias && nullAlias.use_count() == 1;
    ValueTrace aliasTrace;
    auto aliased = MakePublicationObject<Probe>(nullAlias, 91, &aliasTrace, 3, 5);
    std::weak_ptr<Probe> aliasWeak{aliased};
    nullAlias.reset();
    Expect(aliasIsNull && !weakAliasOwner.expired() && aliased->first == 3,
           "helper.null-valued-owner-alias-retains-life", failures);
    aliased.reset();
    Expect(sink.count == 1 && !sink.overflow && !weakAliasOwner.expired(), "helper.alias-retirement-queued", failures);
    sink.DrainOnWorker();
    Expect(sink.destroyed == 2 && aliasTrace.destructionThread == sink.drainThread && !weakAliasOwner.expired(),
           "helper.alias-control-survives-worker-node-destroy", failures);
    aliasWeak.reset();
    Expect(weakAliasOwner.expired(), "helper.null-alias-first-last-weak-releases-memory", failures);
    // A second instance checks the weak control's final release independently.
    auto aliasOwner2 = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakAliasOwner2{aliasOwner2};
    std::shared_ptr<const SharedPublicationMemory> nullAlias2{aliasOwner2, nullptr};
    auto aliasValue = MakePublicationObject<Probe>(nullAlias2, 92, &aliasTrace, 6, 9);
    aliasWeak = aliasValue;
    aliasOwner2.reset();
    nullAlias2.reset();
    aliasValue.reset();
    sink.DrainOnWorker();
    Expect(!weakAliasOwner2.expired() && aliasWeak.expired(), "helper.alias-weak-value-retains-memory", failures);
    aliasWeak.reset();
    Expect(weakAliasOwner2.expired(), "helper.alias-last-weak-releases-memory", failures);
    aliasWeak.reset();
    Expect(aliasWeak.expired() && weakAliasOwner.expired() && !sink.overflow, "helper.alias-control-clean", failures);

    auto throwingMemory = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakThrowingMemory{throwingMemory};
    int constructorCalls = 0;
    bool propagated = false;
    try
    {
        static_cast<void>(MakePublicationObject<ThrowingProbe>(throwingMemory, 0, &constructorCalls, true));
    }
    catch (const std::bad_alloc&)
    {
        propagated = true;
    }
    throwingMemory.reset();
    Expect(propagated && constructorCalls == 1, "helper.constructor-bad-alloc-propagates", failures);
    Expect(weakThrowingMemory.expired() && sink.count == 0, "helper.constructor-failure-has-no-partial-node-leak",
           failures);
}

std::vector<SelectionLoop> Rectangle(double left, double top, double right, double bottom)
{
    return {{Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}}};
}

CanvasDocument DocumentWithSelection(const Guid& id, std::optional<DocumentSelection> selection)
{
    CanvasDocument document;
    document.id = id;
    document.width = 12;
    document.height = 10;
    document.resolution = 72;
    document.selection = std::move(selection);
    return document;
}

DocumentSelection Selection(std::vector<SelectionLoop> loops)
{
    return DocumentSelection{false, SelectionLoops{std::move(loops)}};
}

bool HasRectangle(const std::optional<CanvasDocument>& document, double left, double top, double right, double bottom)
{
    return document && document->selection && document->selection->loops.Get() == Rectangle(left, top, right, bottom);
}

void RunHistoryPublicationControls(int& failures)
{
    const Guid documentID = Guid::Generate();
    const Guid activeID = Guid::Generate();
    const auto original = DocumentWithSelection(documentID, Selection(Rectangle(1, 1, 4, 4)));
    const auto changed = DocumentWithSelection(documentID, Selection(Rectangle(2, 1, 6, 5)));
    const auto deselected = DocumentWithSelection(documentID, std::nullopt);
    DocumentHistory history(8);
    const Guid savedBaseline = history.CurrentRevision();
    history.MarkSaved(savedBaseline);

    auto memory1 = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakMemory1{memory1};
    auto first = DocumentHistory::PrepareEdit(history.CaptureState(), original, activeID, changed, activeID,
                                              L"Wand geometry", {}, {}, memory1);
    Expect(first.has_value() && history.CanPublish(first.value()), "history.prepare-edit-authorized", failures);
    const bool firstPublished = first && history.Publish(first.value());
    const Guid geometryRevision = history.CurrentRevision();
    first.reset();
    memory1.reset();
    Expect(firstPublished && history.UndoCount() == 1 && history.UndoName() == L"Wand geometry" &&
               history.IsModified() && !weakMemory1.expired(),
           "history.publish-retains-memory-after-caller-and-publication-drop", failures);

    history.MarkSaved(geometryRevision);
    auto memory2 = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakMemory2{memory2};
    auto second = DocumentHistory::PrepareEdit(history.CaptureState(), changed, activeID, deselected, activeID,
                                               L"Deselect", {}, {}, memory2);
    const bool secondPublished = second && history.Publish(second.value());
    second.reset();
    memory2.reset();
    Expect(secondPublished && history.UndoCount() == 2 && history.UndoName() == L"Deselect" && history.IsModified() &&
               !deselected.selection && changed.layers.Identity() == deselected.layers.Identity() &&
               !weakMemory1.expired() && !weakMemory2.expired(),
           "history.deselect-publishes-metadata-without-new-geometry", failures);

    auto undoDeselect = history.Undo();
    Expect(undoDeselect && HasRectangle(undoDeselect->document, 2, 1, 6, 5) &&
               undoDeselect->activeLayerID == activeID && undoDeselect->revision == geometryRevision &&
               history.RedoName() == L"Deselect" && !history.IsModified(),
           "history.undo-restores-literal-changed-geometry-and-saved-metadata", failures);
    undoDeselect.reset();
    auto redoDeselect = history.Redo();
    Expect(redoDeselect && redoDeselect->document && !redoDeselect->document->selection &&
               redoDeselect->activeLayerID == activeID && history.UndoName() == L"Deselect" && history.IsModified(),
           "history.redo-restores-deselect-and-entry-name", failures);
    redoDeselect.reset();
    auto undoAgain = history.Undo();
    auto undoGeometry = history.Undo();
    Expect(undoAgain && HasRectangle(undoAgain->document, 2, 1, 6, 5) && undoGeometry &&
               HasRectangle(undoGeometry->document, 1, 1, 4, 4) && undoGeometry->revision == savedBaseline &&
               history.RedoName() == L"Wand geometry" && history.IsModified(),
           "history.undo-chain-restores-original-revision-and-modified-state", failures);
    undoAgain.reset();
    undoGeometry.reset();
    auto redoGeometry = history.Redo();
    Expect(redoGeometry && HasRectangle(redoGeometry->document, 2, 1, 6, 5) && history.UndoName() == L"Wand geometry" &&
               !history.IsModified(),
           "history.redo-chain-restores-saved-geometry", failures);
    redoGeometry.reset();
    Expect(!weakMemory1.expired() && !weakMemory2.expired(), "history.undo-redo-roots-retain-both-publication-owners",
           failures);
    history.Reset();
    Expect(weakMemory1.expired() && weakMemory2.expired() && history.UndoCount() == 0 && history.RedoName().empty(),
           "history.reset-releases-publication-owners-after-all-handles-drop", failures);

    DocumentHistory trimmed(1);
    auto trimMemory = NewMemory();
    auto firstTrim = DocumentHistory::PrepareEdit(trimmed.CaptureState(), original, activeID, changed, activeID,
                                                  L"first", {}, {}, trimMemory);
    const bool firstTrimmedPublished = firstTrim && trimmed.Publish(firstTrim.value());
    firstTrim.reset();
    auto secondTrim = DocumentHistory::PrepareEdit(trimmed.CaptureState(), changed, activeID, deselected, activeID,
                                                   L"second", {}, {}, trimMemory);
    const bool secondTrimmedPublished = secondTrim && trimmed.Publish(secondTrim.value());
    secondTrim.reset();
    Expect(firstTrimmedPublished && secondTrimmedPublished && trimmed.UndoCount() == 1 &&
               trimmed.UndoName() == L"second",
           "history.entry-limit-trims-oldest-entry", failures);
    trimMemory.reset();
    trimmed.Reset();

    auto refused = DocumentHistory::PrepareEdit({}, original, activeID, changed, activeID, L"invalid", {});
    Expect(!refused, "history.refuses-ownerless-publication", failures);

    DocumentHistory sourceHistory;
    DocumentHistory otherHistory;
    auto foreign = DocumentHistory::PrepareEdit(sourceHistory.CaptureState(), original, activeID, changed, activeID,
                                                L"foreign", {});
    Expect(foreign && !otherHistory.CanPublish(foreign.value()) && !otherHistory.Publish(foreign.value()),
           "history.refuses-publication-from-another-owner", failures);
    foreign.reset();

    DocumentHistory staleHistory;
    auto stale =
        DocumentHistory::PrepareEdit(staleHistory.CaptureState(), original, activeID, changed, activeID, L"stale", {});
    staleHistory.Reset();
    Expect(stale && !staleHistory.CanPublish(stale.value()) && !staleHistory.Publish(stale.value()),
           "history.refuses-stale-publication-after-reset", failures);
    stale.reset();

    DocumentHistory cancelledHistory;
    std::stop_source stop;
    static_cast<void>(stop.request_stop());
    auto cancelled = DocumentHistory::PrepareEdit(cancelledHistory.CaptureState(), original, activeID, changed,
                                                  activeID, L"cancelled", CancelToken{stop.get_token()});
    Expect(!cancelled, "history.cancelled-prepare-produces-no-publication", failures);
}

void RunHistoryRetirementControl(int& failures, FixedSink& sink)
{
    DocumentHistory history;
    const auto id = Guid::Generate();
    const auto before = DocumentWithSelection(id, Selection(Rectangle(1, 1, 4, 4)));
    const auto after = DocumentWithSelection(id, Selection(Rectangle(2, 1, 6, 5)));
    auto memory = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakMemory{memory};
    auto publication =
        DocumentHistory::PrepareEdit(history.CaptureState(), before, {}, after, {}, L"Wand", {}, {}, memory);
    const bool published = publication && history.Publish(publication.value());
    publication.reset();
    memory.reset();
    history.Reset();
    Expect(published && history.UndoCount() == 0 && sink.count > 0 && sink.count <= 8 && !sink.overflow &&
               !weakMemory.expired(),
           "history.retired-containers-hold-memory-through-worker-drain", failures);
    sink.DrainOnWorker();
    Expect(sink.count == 0 && !sink.overflow && sink.drainThread != std::this_thread::get_id() && weakMemory.expired(),
           "history.worker-drain-releases-published-metadata", failures);
}
} // namespace

int RunHistoryPublicationLifetimeTests()
{
    int failures = 0;
    const auto priorSink = DeferredReleaseDetail::sink;
    DeferredReleaseDetail::sink = {};
    FixedSink sink;
    const bool installed = InstallDeferredReleaseSink(&FixedSink::Enqueue, &sink);
    Expect(installed, "sink.installs-fixed-allocation-free-collector", failures);
    if (installed)
    {
        RunPublicationObjectControls(failures, sink);
        Expect(sink.count == 0 && !sink.overflow, "sink.helper-controls-leave-no-retired-nodes", failures);
        RunHistoryRetirementControl(failures, sink);
        Expect(ClearDeferredReleaseSink(&sink), "sink.clears-own-context", failures);
    }
    else
    {
        RunPublicationObjectControls(failures, sink);
    }
    DeferredReleaseDetail::sink = {};
    RunHistoryPublicationControls(failures);
    DeferredReleaseDetail::sink = priorSink;
    Expect(DeferredReleaseDetail::sink.enqueue == priorSink.enqueue &&
               DeferredReleaseDetail::sink.context == priorSink.context,
           "sink.restores-prior-thread-sink", failures);
    return failures;
}

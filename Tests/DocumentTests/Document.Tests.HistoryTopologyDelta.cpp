#include "Document.Tests.HistoryTopologyDelta.h"
#include "Document.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace
{
int failures = 0;
int controls = 0;
void Expect(bool value, const char* label) noexcept
{
    ++controls;
    failures += value ? 0 : 1;
    std::printf("%s history-topology-delta.%s\n", value ? "PASS" : "FAIL", label);
}
struct Sources final
{
    Raster base = Raster::Make(2, 1, RasterFormat::Bgra8888);
    Raster leaf = Raster::Make(2, 1, RasterFormat::Bgra8888);
    Raster inner = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0,
                                      {{0, 0, leaf, 0, 0, 1, 1}, {1, 0, leaf, 1, 0, 1, 1}}, 0);
    Raster outer =
        Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0, {{1, 0, inner}, {3, 1, leaf, 1, 0, 1, 1}}, 0);
    Raster second = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 0, 0, {{0, 0, inner}}, 0);
    Raster thumbnail = Raster::Make(1, 1, RasterFormat::Bgra8888);
    Raster mask = Raster::Make(2, 1, RasterFormat::Gray8);
    Raster maskThumbnail = Raster::Make(1, 1, RasterFormat::Gray8);
    Raster shape = Raster::Make(1, 2, RasterFormat::Bgra8888);
    std::optional<CanvasDocument> Document() const
    {
        DocumentFixture fixture;
        fixture.width = 32;
        fixture.height = 32;
        auto layer = ImageLayer::Blank(L"Roots", {4, 2});
        layer.asset = RasterAsset{outer, thumbnail, L"Image"};
        layer.mask = LayerMask{RasterAsset{mask, maskThumbnail, L"Mask"}};
        layer.shapeImage = shape;
        fixture.layers.push_back(std::move(layer));
        return std::move(fixture).Seal();
    }
};
bool HasExactly(const HistoryStorageTopology& topology, const std::array<Raster, 5>& expected)
{
    if (topology.RasterRoots().size() != expected.size())
        return false;
    for (const auto& root : expected)
        if (!topology.HasRasterRoot(root.StorageSerial()))
            return false;
    return true;
}
bool Publish(DocumentHistory& history, DocumentHistory::Publication& publication)
{
    DocumentHistory::ResidencyPermit permit;
    return permit.TryAcquire(publication) == DocumentHistory::ResidencyValidation::Ready &&
           history.Publish(publication);
}
struct Edges
{
    std::size_t history = 0;
    std::size_t live = 0;
    CancelSource* cancel = nullptr;
};
SelectionLoops Square()
{
    std::vector<SelectionLoop> loops{{{1, 1}, {4, 1}, {4, 4}, {1, 4}}};
    return SelectionLoops{std::move(loops)};
}
void Edge(void* context, DocumentHistory::PrepareStage stage)
{
    auto& edges = *static_cast<Edges*>(context);
    if (stage == DocumentHistory::PrepareStage::StorageIndexHistoryEdge)
    {
        ++edges.history;
        if (edges.cancel)
            static_cast<void>(edges.cancel->Cancel());
    }
    if (stage == DocumentHistory::PrepareStage::StorageIndexLiveEdge)
        ++edges.live;
}
void TopologyReferenceAndSharing()
{
    Sources source;
    auto original = source.Document();
    const std::array expected{source.outer, source.thumbnail, source.mask, source.maskThumbnail, source.shape};
    const auto reference = HistoryStorageTopology::Prepare(original);
    const auto first = HistoryStorageTopology::PrepareIncremental(original);
    Expect(first && HasExactly(*first, expected), "managed-direct-roots-cover-five-layer-source-roles");
    Expect(reference && reference->RasterOwners().size() == 8 && first &&
               !first->HasRasterRoot(source.base.StorageSerial()) &&
               !first->HasRasterRoot(source.inner.StorageSerial()) &&
               !first->HasRasterRoot(source.leaf.StorageSerial()),
           "legacy-eight-owner-closure-stays-independent");
    Expect(first && first->Matches(original) && first->MetadataBytes() >= first->SharedRootMetadataBytes() &&
               first->RetirementMemoryBytes() >= first->MetadataBytes(),
           "managed-wrapper-and-shared-root-bounds-are-complete");
    if (!first)
        return;
    for (int index = 0; index < 6; ++index)
    {
        auto scalar = DocumentFixture::Copy(*original);
        scalar.width = 33 + index;
        scalar.layers[0].opacity = 0.25;
        scalar.layers[0].name = L"Scalar edit";
        scalar.layers[0].transform.origin = {2, 3};
        scalar.layers[0].asset->name = L"Metadata only";
        scalar.layers[0].mask->asset.name = L"Mask metadata only";
        if (index % 2)
            scalar.selection = DocumentSelection{true, Square()};
        auto document = std::optional<CanvasDocument>{std::move(scalar).Seal()};
        const auto prepared = HistoryStorageTopology::PrepareIncremental(document, first);
        Expect(prepared && prepared->Matches(document) && HasExactly(*prepared, expected) &&
                   prepared->RasterRootIdentity() == first->RasterRootIdentity() &&
                   prepared->RecordOwner().Identity() != first->RecordOwner().Identity(),
               "six-scalar-selection-edits-share-exact-raster-root-group");
    }
    auto replacement = DocumentFixture::Copy(*original);
    replacement.layers[0].asset->image = source.second;
    auto changed = std::optional<CanvasDocument>{std::move(replacement).Seal()};
    const auto replaced = HistoryStorageTopology::PrepareIncremental(changed, first);
    Expect(replaced && replaced->HasRasterRoot(source.second.StorageSerial()) &&
               !replaced->HasRasterRoot(source.outer.StorageSerial()) &&
               replaced->RasterRootIdentity() != first->RasterRootIdentity() &&
               first->HasRasterRoot(source.outer.StorageSerial()),
           "changed-image-makes-new-root-group-and-preserves-original");
    auto duplicate = DocumentFixture::Copy(*original);
    auto layer = duplicate.layers[0];
    layer.id = Guid::Generate();
    duplicate.layers.push_back(std::move(layer));
    auto duplicated = std::optional<CanvasDocument>{std::move(duplicate).Seal()};
    const auto shared = HistoryStorageTopology::PrepareIncremental(duplicated, first);
    Expect(shared && HasExactly(*shared, expected), "two-layer-aliases-deduplicate-five-direct-roots");
    Expect(!source.inner.IsMaterialized() && !source.outer.IsMaterialized() && !source.second.IsMaterialized(),
           "all-topology-preparation-keeps-nested-sparse-caches-unmaterialized");
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    Expect(!HistoryStorageTopology::PrepareIncremental(changed, first, cancellation.Token()) &&
               first->Matches(original),
           "pre-cancel-refuses-new-topology-without-changing-original");
}
void ReferenceHistoryAndEdgeCost(int entryLimit, std::uint64_t byteLimit)
{
    Sources source;
    auto current = source.Document();
    DocumentHistory reference(entryLimit, byteLimit);
    DocumentHistory candidate(entryLimit, byteLimit);
    for (int step = 0; step < 32; ++step)
    {
        auto next = DocumentFixture::Copy(*current);
        next.width = 33 + step;
        next.layers[0].asset->image = (step % 2) ? source.second : source.outer;
        if (step % 3 == 0)
            next.layers[0].mask.reset();
        else
            next.layers[0].mask = LayerMask{RasterAsset{source.mask, source.maskThumbnail, L"Mask"}};
        if (step % 5 == 0)
            next.layers[0].shapeImage = {};
        else
            next.layers[0].shapeImage = source.shape;
        if (step % 4 == 0)
            next.selection = DocumentSelection{true, Square()};
        else
            next.selection.reset();
        auto after = std::optional<CanvasDocument>{std::move(next).Seal()};
        reference.Begin(L"Topology edit", current, {});
        reference.End(after, {});
        auto prepared =
            DocumentHistory::PrepareEdit(candidate.CaptureState(), current, {}, after, {}, L"Topology edit");
        const bool published = prepared && Publish(candidate, *prepared);
        Expect(published, "ninety-six-nested-dag-edits-publish-with-existing-limits");
        if (!published)
            return;
        current = std::move(after);
        Expect(candidate.RetainedBytes(current) == reference.RetainedBytes(current) &&
                   candidate.UndoCount() == reference.UndoCount() &&
                   candidate.CaptureState().FutureCount() == reference.CaptureState().FutureCount(),
               "ninety-six-shared-child-mask-shape-selection-charges-match-reference");
        if (step == 12)
            Expect(source.outer.Data() != nullptr && source.inner.Data() != nullptr && source.second.Data() != nullptr,
                   "three-budget-corpora-publish-late-nested-cache-growth");
    }
}
void ScalarEdgesAndCancellation()
{
    Sources source;
    auto original = source.Document();
    auto firstDraft = DocumentFixture::Copy(*original);
    firstDraft.width = 33;
    auto current = std::optional<CanvasDocument>{std::move(firstDraft).Seal()};
    DocumentHistory history(100, 1024 * 1024);
    auto first = DocumentHistory::PrepareEdit(history.CaptureState(), original, {}, current, {}, L"First");
    const bool initialized = first && Publish(history, *first);
    Expect(initialized, "scalar-edge-fixture-publishes-original-shared-dag");
    if (!initialized)
        return;
    auto scalar = DocumentFixture::Copy(*current);
    scalar.layers[0].opacity = 0.5;
    scalar.layers[0].name = L"Renamed";
    auto after = std::optional<CanvasDocument>{std::move(scalar).Seal()};
    Edges edges;
    auto prepared =
        DocumentHistory::PrepareEdit(history.CaptureState(), current, {}, after, {}, L"Scalar", {}, {&Edge, &edges});
    Expect(prepared && edges.history == 10 && edges.live == 0,
           "scalar-history-visits-ten-direct-roots-and-zero-live-descendants");
    Expect(prepared && Publish(history, *prepared) && history.UndoCount() == 2 && history.RetainedBytes(after) == 0,
           "scalar-publication-keeps-zero-shared-raster-charge");
    current = std::move(after);
    auto changed = DocumentFixture::Copy(*current);
    changed.layers[0].asset->image = source.second;
    after = std::optional<CanvasDocument>{std::move(changed).Seal()};
    const auto revision = history.CurrentRevision();
    const auto undo = history.UndoCount();
    CancelSource cancellation;
    Edges cancelled{0, 0, &cancellation};
    auto refused = DocumentHistory::PrepareEdit(history.CaptureState(), current, {}, after, {}, L"Cancelled",
                                                cancellation.Token(), {&Edge, &cancelled});
    Expect(!refused && cancelled.history == 1 && cancelled.live == 0 && history.CurrentRevision() == revision &&
               history.UndoCount() == undo,
           "mid-parent-edge-cancellation-preserves-published-history");
}
} // namespace
int RunHistoryTopologyDeltaTests()
{
    failures = 0;
    controls = 0;
    TopologyReferenceAndSharing();
    ReferenceHistoryAndEdgeCost(7, 1024 * 1024);
    ReferenceHistoryAndEdgeCost(31, 512);
    ReferenceHistoryAndEdgeCost(100, 1);
    ScalarEdgesAndCancellation();
    std::printf("%s history-topology-delta.summary passed=%d failed=%d controls=%d\n", failures ? "FAIL" : "PASS",
                controls - failures, failures, controls);
    return failures;
}

#include "LayerRowsWorkTests.h"

#include "../../Common/DeferredRelease.h"
#include "../../Common/LayerRowsWork.h"
#include "../Support/DocumentFixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s layer-rows-work.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid Id(const char* text)
{
    return Guid::Parse(text).value();
}

constexpr Guid kDocumentId{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kRootBottom{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kFolder{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 3}};
constexpr Guid kChildBottom{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 4}};
constexpr Guid kNestedFolder{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 5}};
constexpr Guid kNestedBottom{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 6}};
constexpr Guid kNestedTop{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 7}};
constexpr Guid kChildTop{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 8}};
constexpr Guid kRootHidden{{0x70, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 9}};

ImageLayer Named(Guid id, std::wstring name, bool visible = true)
{
    auto layer = ImageLayer::Blank(std::move(name), {1, 1});
    layer.id = id;
    layer.visible = visible;
    return layer;
}

CanvasDocument RowsDocument()
{
    DocumentFixture fixture;
    fixture.id = kDocumentId;
    fixture.width = 1;
    fixture.height = 1;

    auto rootBottom = Named(kRootBottom, L"");
    auto folder = Named(kFolder, L"Folder");
    folder.isGroup = true;
    folder.mask = LayerMask{};
    folder.mask->enabled = false;
    auto childBottom = Named(kChildBottom, L"Child bottom");
    childBottom.parentID = kFolder;
    auto nestedFolder = Named(kNestedFolder, L"Nested", false);
    nestedFolder.parentID = kFolder;
    nestedFolder.isGroup = true;
    auto nestedBottom = Named(kNestedBottom, L"Nested bottom");
    nestedBottom.parentID = kNestedFolder;
    auto nestedTop = Named(kNestedTop, L"Nested top");
    nestedTop.parentID = kNestedFolder;
    auto childTop = Named(kChildTop, L"Child top");
    childTop.parentID = kFolder;
    childTop.mask = LayerMask{};
    auto rootHidden = Named(kRootHidden, L"Root hidden", false);

    // Document storage is bottom-to-top. These fixed records independently imply the expected panel order.
    fixture.layers = {rootBottom, folder, childBottom, nestedFolder, nestedBottom, nestedTop, childTop, rootHidden};
    return std::move(fixture).Seal();
}

constexpr std::array<Guid, 8> kTopFirst{kRootHidden, kFolder,       kChildTop,    kNestedFolder,
                                        kNestedTop,  kNestedBottom, kChildBottom, kRootBottom};
constexpr std::array<int, 8> kDepths{0, 0, 1, 1, 2, 2, 1, 0};

LayerRowsAdmission Admit(const CanvasDocument& source, std::size_t collapsed = 0,
                         std::wstring_view pattern = L"[hidden {0}]")
{
    return LayerRowsAdmission::Estimate(source, collapsed, pattern);
}

void PreparationHasLiteralTopFirstHierarchyAndLabels()
{
    const auto source = RowsDocument();
    const auto admission = Admit(source);
    auto prepared = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission);
    Expect(admission.IsValid() && admission.Bytes() >= source.RetirementMemoryBytes() && prepared,
           "valid-admission-prepares-owned-row-snapshot");
    if (!prepared)
        return;

    const auto rows = prepared->Rows();
    const auto text = prepared->DisplayText();
    bool orderDepthVisibility = rows.size() == kTopFirst.size() && text.size() == kTopFirst.size();
    for (std::size_t index = 0; orderDepthVisibility && index < kTopFirst.size(); ++index)
    {
        const bool visible = index != 0 && index != 3 && index != 4 && index != 5;
        orderDepthVisibility =
            rows[index].id == kTopFirst[index] && rows[index].depth == kDepths[index] && rows[index].visible == visible;
    }
    Expect(orderDepthVisibility, "literal-root-sibling-depth-and-inherited-visibility-order");
    Expect(rows.size() == 8 && rows[1].isGroup && !rows[1].collapsed && rows[3].isGroup && !rows[3].collapsed,
           "groups-are-rows-and-expanded-groups-are-not-marked-collapsed");
    Expect(rows[1].hasMask && !rows[1].maskEnabled && rows[2].hasMask && rows[2].maskEnabled && !rows[7].hasMask &&
               rows[7].maskEnabled,
           "mask-badge-and-default-enabled-flags-are-preserved");
    Expect(rows[7].name.empty() && text[7] == L"00000070-4000-8000-0000-000000000002",
           "empty-layer-name-uses-fixed-guid-string-as-display-fallback");
    Expect(text[0] == L"[hidden Root hidden]" && text[3] == L"[hidden Nested]" && text[4] == L"[hidden Nested top]" &&
               text[5] == L"[hidden Nested bottom]",
           "synthetic-hidden-stringtable-pattern-replaces-single-placeholder");
    Expect(text[1] == L"Folder" && text[2] == L"Child top" && text[6] == L"Child bottom",
           "effectively-visible-rows-use-unmodified-layer-names");

    const auto visibleLayer = prepared->FindVisibleRow(kChildTop);
    const auto effectivelyHidden = prepared->FindVisibleRow(kNestedTop);
    const auto absent = prepared->FindVisibleRow(Id("70000000-0000-4000-8000-0000000000ff"));
    Expect(visibleLayer && *visibleLayer == 2 && effectivelyHidden && *effectivelyHidden == 4 && !absent,
           "lookup-finds-visible-and-effectively-hidden-rows-and-refuses-missing-guid");
    Expect(prepared->Source().id == kDocumentId && prepared->Collapsed().empty() &&
               prepared->HiddenPattern() == L"[hidden {0}]",
           "prepared-result-retains-source-collapse-and-pattern-authority");
}

void CollapsedDescendantsAreAbsent()
{
    const auto source = RowsDocument();
    auto prepared = LayerRowsWork::Prepare(source, {kNestedFolder}, L"[hidden {0}]", Admit(source, 1));
    Expect(prepared && prepared->Rows().size() == 6 && prepared->DisplayText().size() == 6,
           "collapsed-nested-folder-omits-both-descendant-rows");
    if (!prepared)
        return;
    constexpr std::array<Guid, 6> expected{kRootHidden, kFolder, kChildTop, kNestedFolder, kChildBottom, kRootBottom};
    bool literalOrder = true;
    for (std::size_t index = 0; index < expected.size(); ++index)
        literalOrder = literalOrder && prepared->Rows()[index].id == expected[index];
    Expect(literalOrder && prepared->Rows()[3].collapsed && prepared->Rows()[3].isGroup,
           "collapsed-row-keeps-group-identity-and-independent-literal-sibling-order");
    Expect(!prepared->FindVisibleRow(kNestedTop) && !prepared->FindVisibleRow(kNestedBottom),
           "lookup-refuses-descendants-hidden-by-collapse");
}

void AdmissionAndMalformedInputsAreRefused()
{
    const auto source = RowsDocument();
    const auto valid = Admit(source);
    Expect(!LayerRowsAdmission{}.IsValid() &&
               !LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", LayerRowsAdmission{}),
           "default-admission-cannot-authorize-preparation");
    Expect(!Admit(source, 0, L"hidden").IsValid() && !Admit(source, 0, L"{0} / {0}").IsValid(),
           "admission-refuses-missing-or-repeated-hidden-placeholder");
    Expect(!LayerRowsWork::Prepare(source, {}, L"hidden", valid),
           "preparation-rechecks-invalid-pattern-after-admission");
    Expect(!LayerRowsWork::Prepare(source, {kFolder}, L"[hidden {0}]", valid),
           "preparation-rechecks-collapse-count-bound-to-admission");

    Expect(!Admit(source, 0, L"{0} {").IsValid() && !Admit(source, 0, L"{1} {0}").IsValid(),
           "admission-refuses-unbalanced-or-unknown-format-arguments");
    auto escaped = LayerRowsWork::Prepare(source, {}, L"{{{0}}}", Admit(source, 0, L"{{{0}}}"));
    Expect(escaped && escaped->DisplayText()[0] == L"{Root hidden}" && escaped->DisplayText()[1] == L"Folder",
           "escaped-literal-braces-follow-stringtable-format-semantics");

    auto foreign = RowsDocument();
    foreign.id = Id("71000000-0000-4000-8000-000000000001");
    Expect(!LayerRowsWork::Prepare(std::move(foreign), {}, L"[hidden {0}]", valid),
           "admission-refuses-foreign-document-root");

    DocumentFixture duplicate;
    duplicate.id = Id("72000000-0000-4000-8000-000000000001");
    auto first = Named(Id("72000000-0000-4000-8000-000000000002"), L"first");
    auto second = Named(Id("72000000-0000-4000-8000-000000000002"), L"second");
    duplicate.layers = {first, second};
    const auto duplicateDocument = std::move(duplicate).Seal();
    const auto duplicateAdmission = Admit(duplicateDocument);
    Expect(duplicateAdmission.IsValid() &&
               !LayerRowsWork::Prepare(duplicateDocument, {}, L"[hidden {0}]", duplicateAdmission),
           "duplicate-layer-guid-is-rejected-before-row-publication");

    CancelSource canceled;
    (void)canceled.Cancel();
    Expect(!LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", valid, canceled.Token()),
           "pre-cancelled-preparation-publishes-no-rows");
}

struct CancelPrefix final
{
    CancelSource* source = nullptr;
    std::size_t target = 0;
    std::size_t reached = 0;
};

void CancelAfterRow(void* context, std::size_t rowCount) noexcept
{
    auto& state = *static_cast<CancelPrefix*>(context);
    state.reached = rowCount;
    if (rowCount == state.target)
        (void)state.source->Cancel();
}

void CancellationAtEveryCompletedRowPrefixPublishesNothing()
{
    const auto source = RowsDocument();
    const auto admission = Admit(source);
    std::size_t reachedAllPrefixes = 0;
    std::size_t escapedResults = 0;
    for (std::size_t prefix = 1; prefix <= kTopFirst.size(); ++prefix)
    {
        CancelSource cancellation;
        CancelPrefix state{&cancellation, prefix, 0};
        auto result = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission, cancellation.Token(),
                                             LayerRowsObserver{CancelAfterRow, &state});
        reachedAllPrefixes += state.reached == prefix && cancellation.Token().IsCancelled() ? 1u : 0u;
        escapedResults += result ? 1u : 0u;
    }
    Expect(reachedAllPrefixes == kTopFirst.size(), "observer-reaches-each-completed-row-prefix-including-last");
    Expect(escapedResults == 0, "cancellation-at-each-row-prefix-publishes-no-partial-result");
}

struct ReleaseQueue final
{
    DeferredReleaseNode* node = nullptr;
    std::thread::id owner;
    std::thread::id enqueuedOn;
};

void EnqueueRelease(void* context, DeferredReleaseNode& node) noexcept
{
    auto& queue = *static_cast<ReleaseQueue*>(context);
    queue.node = &node;
    queue.enqueuedOn = std::this_thread::get_id();
}

void SoleSourceGraphIsRetiredByJoinedWorker()
{
    Raster pixels = Raster::Make(2048, 2048, RasterFormat::Bgra8888);
    auto layer = Named(Id("73000000-0000-4000-8000-000000000002"), L"Large source");
    layer.asset = RasterAsset{pixels, {}, L"Large source"};
    DocumentFixture fixture;
    fixture.id = Id("73000000-0000-4000-8000-000000000001");
    fixture.width = 2048;
    fixture.height = 2048;
    fixture.layers.push_back(std::move(layer));
    auto source = std::move(fixture).Seal();
    const auto sourceBytes = source.RetirementMemoryBytes();
    const auto admission = Admit(source);
    auto rows = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission);
    source = {};
    pixels = {};

    ReleaseQueue queue;
    queue.owner = std::this_thread::get_id();
    const bool installed = InstallDeferredReleaseSink(EnqueueRelease, &queue);
    Expect(installed && rows && sourceBytes >= 16ull * 1024 * 1024,
           "large-prepared-row-snapshot-retains-the-admitted-source-graph");
    if (!installed || !rows)
    {
        if (installed)
            (void)ClearDeferredReleaseSink(&queue);
        return;
    }
    rows.reset();
    const bool queuedOnOwner = queue.node && queue.enqueuedOn == queue.owner;
    const bool wholeGraphCharged =
        queue.node && queue.node->bytes >= sourceBytes && queue.node->bytes >= kDeferredReleaseThreshold;
    std::thread::id destroyedOn;
    std::thread worker(
        [&]()
        {
            if (queue.node && queue.node->destroy)
            {
                queue.node->destroy(queue.node);
                destroyedOn = std::this_thread::get_id();
            }
        });
    worker.join();
    const bool cleared = ClearDeferredReleaseSink(&queue);
    Expect(queuedOnOwner && wholeGraphCharged && destroyedOn != std::thread::id{} && destroyedOn != queue.owner,
           "sole-source-graph-release-is-enqueued-on-owner-and-destroyed-on-joined-worker");
    Expect(cleared, "row-release-sink-clears-after-worker-join");
}

void SparseSourcePixelsStayUnmaterialized()
{
    const auto sparse = Raster::MakeSparse(1024, 1024, RasterFormat::Bgra8888, {}, 0, 0, {}, 37);
    auto layer = Named(Id("74000000-0000-4000-8000-000000000002"), L"Sparse layer");
    layer.asset = RasterAsset{sparse, {}, L"Sparse source"};
    DocumentFixture fixture;
    fixture.id = Id("74000000-0000-4000-8000-000000000001");
    fixture.width = 1024;
    fixture.height = 1024;
    fixture.layers.push_back(std::move(layer));
    const auto source = std::move(fixture).Seal();
    const auto epoch = Raster::ResidencyEpoch();
    auto rows = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", Admit(source));
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && rows &&
               !rows->Source().layers[0].asset->image.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "row-preparation-retains-sparse-source-without-materializing-source-pixels");
}

void EmptySealedDocumentHasNoRowsOrLookupHits()
{
    DocumentFixture fixture;
    fixture.id = Id("75000000-0000-4000-8000-000000000001");
    const auto source = std::move(fixture).Seal();
    const auto admission = Admit(source);
    auto prepared = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission);
    Expect(source.layers.Identity() != nullptr && source.layers.empty() && admission.IsValid() && prepared &&
               prepared->Rows().empty() && prepared->DisplayText().empty(),
           "sealed-empty-document-prepares-zero-rows-and-zero-labels");
    Expect(prepared && !prepared->FindVisibleRow(Id("75000000-0000-4000-8000-000000000002")),
           "sealed-empty-document-missing-guid-has-no-row");
}

void MaximumLongNameRowsKeepLiteralOrderAndCompleteSourceCharge()
{
    const std::wstring longName(8192, L'N'); // 16,384 name bytes per record on the Windows host.
    const auto sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, {}, 0, 0, {}, 37);
    DocumentFixture fixture;
    fixture.id = Id("75000000-0000-4000-8000-000000000003");
    fixture.width = 2;
    fixture.height = 2;
    fixture.layers.reserve(10000);
    for (std::size_t index = 0; index < 10000; ++index)
    {
        Guid id{{0x75, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0}};
        id.bytes[14] = static_cast<std::uint8_t>(index >> 8);
        id.bytes[15] = static_cast<std::uint8_t>(index & 0xff);
        auto layer = Named(id, longName);
        layer.asset = RasterAsset{sparse, {}, L"Shared sparse source"};
        fixture.layers.push_back(std::move(layer));
    }
    const auto source = std::move(fixture).Seal();
    const auto sourceBytes = source.RetirementMemoryBytes();
    const auto sourceIdentity = source.layers.Identity();
    const auto epoch = Raster::ResidencyEpoch();
    const auto admission = Admit(source);

    // An independent change in held selection storage must add its entire source charge exactly once.
    auto selectedSource = source;
    selectedSource.selection =
        DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};
    const auto selectedBytes = selectedSource.RetirementMemoryBytes();
    const auto selectedAdmission = Admit(selectedSource);
    Expect(admission.IsValid() && selectedAdmission.IsValid() && selectedBytes > sourceBytes &&
               selectedAdmission.Bytes() > admission.Bytes() &&
               selectedAdmission.Bytes() - admission.Bytes() == selectedBytes - sourceBytes,
           "maximum-name-admission-adds-independent-held-source-charge-exactly-once");

    auto prepared = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission);
    Expect(prepared && prepared->Rows().size() == 10000 && prepared->DisplayText().size() == 10000,
           "maximum-10000-unique-layer-ids-with-8192-character-names-prepare-completely");
    if (!prepared || prepared->Rows().size() != 10000 || prepared->DisplayText().size() != 10000)
        return;

    bool literalOrderAndNames = true;
    bool untouchedPixels = sparse.IsSparse() && !sparse.IsMaterialized();
    for (std::size_t row = 0; row < 10000; ++row)
    {
        const auto suffix = 9999 - row; // All roots are top-first: literal suffix 9999 down through zero.
        Guid expected{{0x75, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0}};
        expected.bytes[14] = static_cast<std::uint8_t>(suffix >> 8);
        expected.bytes[15] = static_cast<std::uint8_t>(suffix & 0xff);
        const auto lookup = prepared->FindVisibleRow(expected);
        const auto& entry = prepared->Rows()[row];
        literalOrderAndNames = literalOrderAndNames && entry.id == expected && entry.depth == 0 && entry.visible &&
                               !entry.isGroup && !entry.collapsed && entry.name.size() == 8192 &&
                               entry.name == longName && prepared->DisplayText()[row] == longName && lookup &&
                               *lookup == row;
        const auto& asset = prepared->Source().layers[row].asset;
        untouchedPixels = untouchedPixels && asset && asset->image == sparse && !asset->image.IsMaterialized();
    }
    Expect(literalOrderAndNames, "all-10000-top-first-literal-ids-long-labels-and-lookup-indices-agree");
    Expect(prepared->bytes == admission.Bytes() && prepared->Source().layers.Identity() == sourceIdentity &&
               prepared->Source().RetirementMemoryBytes() == sourceBytes && sourceBytes >= 163840000,
           "maximum-name-result-retains-original-root-and-exact-admitted-source-charge");
    Expect(untouchedPixels && !sparse.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "maximum-name-admission-and-row-preparation-touch-no-shared-sparse-source-pixels");
}

void OversizedAdmissionInputsCannotPublish()
{
    const auto source = RowsDocument();
    Expect(Admit(source, 9).IsValid(), "remembered-collapse-count-can-exceed-live-layer-count");

    std::wstring oversizedPattern(1025, L'x');
    oversizedPattern.replace(0, 3, L"{0}");
    Expect(oversizedPattern.size() == 1025 && !Admit(source, 0, oversizedPattern).IsValid(),
           "single-placeholder-pattern-over-1024-characters-refused");

    const auto admission = Admit(source, 1);
    std::vector<Guid> collapsed;
    collapsed.reserve(100000);
    collapsed.push_back(kNestedFolder);
    const bool retainedExcessCapacity = collapsed.size() == 1 && collapsed.capacity() >= 100000;
    auto prepared = LayerRowsWork::Prepare(source, std::move(collapsed), L"[hidden {0}]", admission);
    Expect(admission.IsValid() && retainedExcessCapacity && !prepared,
           "one-collapse-id-with-retained-100000-capacity-cannot-exceed-original-admission");
}

void MaximumDepthChainPreparesAndCancelsAtItsFinalRow()
{
    DocumentFixture fixture;
    fixture.id = Id("76000000-0000-4000-8000-000000000001");
    fixture.layers.reserve(65);
    Guid parent;
    for (std::size_t depth = 0; depth < 65; ++depth)
    {
        Guid id{{0x76, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0}};
        id.bytes[15] = static_cast<std::uint8_t>(depth);
        auto layer = Named(id, depth == 64 ? L"Leaf" : L"Group", depth != 32);
        layer.isGroup = depth < 64;
        if (depth != 0)
            layer.parentID = parent;
        fixture.layers.push_back(std::move(layer));
        parent = id;
    }
    const auto source = std::move(fixture).Seal();
    const auto admission = Admit(source);
    auto prepared = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission);
    bool literalChain =
        admission.IsValid() && prepared && prepared->Rows().size() == 65 && prepared->DisplayText().size() == 65;
    for (std::size_t depth = 0; literalChain && depth < 65; ++depth)
    {
        Guid expected{{0x76, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0}};
        expected.bytes[15] = static_cast<std::uint8_t>(depth);
        const auto& row = prepared->Rows()[depth];
        const auto lookup = prepared->FindVisibleRow(expected);
        const std::wstring_view label = depth == 64 ? L"[hidden Leaf]" : depth < 32 ? L"Group" : L"[hidden Group]";
        literalChain = row.id == expected && row.depth == static_cast<int>(depth) && row.isGroup == (depth < 64) &&
                       !row.collapsed && row.visible == (depth < 32) && prepared->DisplayText()[depth] == label &&
                       lookup && *lookup == depth;
    }
    Expect(literalChain, "literal-64-ancestor-chain-keeps-all-65-depths-inherited-visibility-labels-and-lookups");

    CancelSource cancellation;
    CancelPrefix state{&cancellation, 65, 0};
    auto canceled = LayerRowsWork::Prepare(source, {}, L"[hidden {0}]", admission, cancellation.Token(),
                                           LayerRowsObserver{CancelAfterRow, &state});
    Expect(state.reached == 65 && cancellation.Token().IsCancelled() && !canceled,
           "maximum-depth-chain-cancellation-after-final-completed-row-publishes-nothing");
}
void DeletedCollapsedGroupsKeepTheRemainingListUsable()
{
    DocumentFixture fixture;
    fixture.id = Id("70000000-0000-4000-8000-000000000009");
    fixture.width = 2;
    fixture.height = 1;
    ImageLayer first = ImageLayer::Blank(L"Deleted first group", {2, 1});
    first.isGroup = true;
    ImageLayer second = ImageLayer::Blank(L"Deleted second group", {2, 1});
    second.isGroup = true;
    ImageLayer survivor = ImageLayer::Blank(L"Survivor", {2, 1});
    const Guid firstID = first.id;
    const Guid secondID = second.id;
    const Guid survivorID = survivor.id;
    fixture.layers.push_back(std::move(first));
    fixture.layers.push_back(std::move(second));
    fixture.layers.push_back(std::move(survivor));
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), survivorID);
    session.SetGroupCollapsed(firstID, true);
    session.SetGroupCollapsed(secondID, true);
    session.DeleteLayer(firstID);
    session.DeleteLayer(secondID);
    const auto source = session.Document().value();
    std::vector<Guid> collapsed{firstID, secondID};
    const auto admission = Admit(source, collapsed.size());
    auto prepared = LayerRowsWork::Prepare(source, std::move(collapsed), L"[hidden {0}]", admission);
    Expect(session.CollapsedGroupIDs().size() == 2 && source.layers.size() == 1 && admission.IsValid() && prepared &&
               prepared->Rows().size() == 1 && prepared->Rows()[0].id == survivorID &&
               prepared->DisplayText()[0] == L"Survivor",
           "deleted-collapsed-group-ids-do-not-refuse-remaining-visible-rows");
    session.DeleteLayer(survivorID);
    const auto empty = session.Document().value();
    auto emptyPrepared = LayerRowsWork::Prepare(empty, {firstID, secondID}, L"[hidden {0}]", Admit(empty, 2));
    Expect(empty.layers.empty() && emptyPrepared && emptyPrepared->Rows().empty() &&
               emptyPrepared->Collapsed().size() == 2,
           "empty-document-can-retain-bounded-remembered-collapse-ids");
    Expect(!Admit(source, kMaxLayerCount + 1).IsValid(), "collapse-admission-refuses-fixed-display-id-limit-overflow");
}

} // namespace

int RunLayerRowsWorkTests()
{
    gFailures = 0;
    PreparationHasLiteralTopFirstHierarchyAndLabels();
    CollapsedDescendantsAreAbsent();
    AdmissionAndMalformedInputsAreRefused();
    CancellationAtEveryCompletedRowPrefixPublishesNothing();
    SoleSourceGraphIsRetiredByJoinedWorker();
    SparseSourcePixelsStayUnmaterialized();
    EmptySealedDocumentHasNoRowsOrLookupHits();
    MaximumLongNameRowsKeepLiteralOrderAndCompleteSourceCharge();
    OversizedAdmissionInputsCannotPublish();
    MaximumDepthChainPreparesAndCancelsAtItsFinalRow();
    DeletedCollapsedGroupsKeepTheRemainingListUsable();
    return gFailures;
}

#include "Document.Tests.LayerReadView.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "RecordRoot.h"

#include <array>
#include <cstdio>
#include <span>
#include <utility>

namespace
{
using Root = RecordRoot<ImageLayer>;
int gChecks = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s layer-read-view.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

ImageLayer Layer(unsigned char id, const wchar_t* name)
{
    ImageLayer result;
    result.id.bytes[0] = id;
    result.name = name;
    return result;
}
} // namespace

int RunLayerReadViewTests()
{
    gChecks = 0;
    gFailures = 0;
    // Independent range/ownership fixture: base, hidden folder, child, clipped child. Array storage avoids
    // relying on an owning vector adapter. Existing legacy graph/paint goldens remain in the full suite.
    std::array<ImageLayer, 4> records{Layer(1, L"base"), Layer(2, L"folder"), Layer(3, L"child"), Layer(4, L"clip")};
    records[1].isGroup = true;
    records[1].visible = false;
    records[2].parentID = records[1].id;
    records[3].parentID = records[1].id;
    records[3].maskSourceID = records[2].id;
    const std::span<const ImageLayer> view(records);
    Expect(view.data() == records.data() && view.size() == 4 && IndexOf(view, records[3].id) == 3,
           "array-view-borrows-storage-and-indexes-in-range-order");
    Expect(IndexOf(view.subspan(2), records[3].id) == 1 && !IndexOf(view.subspan(2), records[0].id),
           "sliced-view-index-is-relative-and-excludes-outside-records");
    const std::span<const ImageLayer> empty;
    Expect(!IndexOf(empty, records[0].id) && ValidateHierarchy(empty) == DocumentStatus::Ok &&
               ValidateLiveMaskGraph(empty) == DocumentStatus::Ok && HierarchyEntries(empty).empty() &&
               DescendantIds(empty, records[1].id).empty(),
           "empty-view-preserves-empty-graph-results");
    Expect(ValidateHierarchy(view) == DocumentStatus::Ok && ValidateLiveMaskGraph(view) == DocumentStatus::Ok,
           "complete-array-view-validates-hierarchy-and-clipping");
    Expect(ValidateHierarchy(view.subspan(2)) == DocumentStatus::Invalid &&
               ValidateLiveMaskGraph(view.last(1)) == DocumentStatus::Invalid,
           "incomplete-view-refuses-external-parent-and-clipping-source");
    const auto descendants = DescendantIds(view, records[1].id);
    Expect(descendants.size() == 2 && descendants.contains(records[2].id) && descendants.contains(records[3].id) &&
               !descendants.contains(records[0].id) && !descendants.contains(records[1].id),
           "array-view-descendants-retain-exact-owned-range");
    const auto hierarchy = HierarchyEntries(view);
    Expect(hierarchy.size() == 4 && hierarchy[0].id == records[0].id && hierarchy[0].depth == 0 &&
               hierarchy[0].visible && hierarchy[1].id == records[1].id && hierarchy[1].depth == 0 &&
               hierarchy[1].isGroup && !hierarchy[1].visible && hierarchy[2].id == records[2].id &&
               hierarchy[2].depth == 1 && !hierarchy[2].visible && hierarchy[3].id == records[3].id &&
               hierarchy[3].depth == 1 && !hierarchy[3].visible,
           "array-view-hierarchy-order-depth-and-hidden-parent-remain-literal");
    const auto reversed = HierarchyEntries(view, true);
    const auto collapsed = HierarchyEntries(view, false, {records[1].id});
    Expect(reversed.size() == 4 && reversed[0].id == records[1].id && reversed[1].id == records[3].id &&
               reversed[2].id == records[2].id && reversed[3].id == records[0].id && collapsed.size() == 2 &&
               collapsed[1].id == records[1].id && collapsed[1].collapsed,
           "array-view-reversed-and-collapsed-orders-remain-literal");
    Expect(EffectivelyVisible(view, records[0].id) && !EffectivelyVisible(view, records[2].id),
           "array-view-visibility-follows-hidden-parent");
    DocumentFixture compatibility;
    compatibility.layers.assign(records.begin(), records.end());
    const CanvasDocument sealedCompatibility = std::move(compatibility).Seal();
    Expect(EffectivelyVisible(sealedCompatibility, records[0].id) &&
               !EffectivelyVisible(sealedCompatibility, records[2].id),
           "const-document-forwarder-preserves-existing-caller-results");

    Root::Builder sourceBuilder(records.size());
    for (const auto& record : records)
        (void)sourceBuilder.Append(record);
    auto source = sourceBuilder.Seal();
    const Root captured = source.value();
    const std::span<const ImageLayer> rootView(captured.Read());
    Expect(rootView.data() == &captured[0] && IndexOf(rootView, records[3].id) == 3 &&
               ValidateHierarchy(rootView) == DocumentStatus::Ok &&
               ValidateLiveMaskGraph(rootView) == DocumentStatus::Ok,
           "immutable-root-view-borrows-records-without-an-owning-adapter");
    auto edit = Root::Builder::Clone(source.value(), records.size());
    ImageLayer renamed = captured[2];
    renamed.name = L"new-child";
    (void)edit.value().Replace(2, std::move(renamed));
    auto changed = edit.value().Seal();
    source.reset();
    const auto oldEntries = HierarchyEntries(rootView);
    Expect(oldEntries.size() == 4 && oldEntries[2].name == L"child" && rootView[2].name == L"child" &&
               captured.Identity() != changed.value().Identity() && changed.value()[2].name == L"new-child" &&
               EffectivelyVisible(rootView, records[0].id) && !EffectivelyVisible(rootView, records[2].id),
           "captured-root-keeps-view-valid-through-new-root-publication");
    std::printf("SUMMARY layer-read-view checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

#include "Document.Tests.MergePlanProbe.h"

#include "Document.h"
#include "Executor.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s merge-plan-probe.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid Id(const char* text)
{
    const auto parsed = Guid::Parse(text);
    return parsed.value_or(Guid{});
}

Raster Bgra(std::span<const std::uint8_t> bytes)
{
    return Raster::Make(1, 1, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer PixelLayer(const char* id, const wchar_t* name, const std::optional<Guid>& parent = std::nullopt)
{
    // Premultiplied BGRA: each color channel is no greater than alpha.
    constexpr std::array<std::uint8_t, 4> pixel{12, 20, 30, 128};
    const Raster image = Bgra(pixel);
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    layer.id = Id(id);
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), name};
    layer.parentID = parent;
    return layer;
}

ImageLayer Group(const char* id, const wchar_t* name, const std::optional<Guid>& parent = std::nullopt)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    layer.id = Id(id);
    layer.isGroup = true;
    layer.parentID = parent;
    return layer;
}

DocumentFixture EmptyDocument()
{
    DocumentFixture document;
    document.id = Id("10000000-0000-4000-8000-000000000001");
    document.width = 1;
    document.height = 1;
    return document;
}

bool Matches(const DocumentSession::MergePlan& plan, const std::vector<Guid>& ids,
             const std::unordered_set<Guid>& removed, const wchar_t* name, const std::optional<Guid>& parent,
             const Guid& anchor, const wchar_t* action)
{
    return plan.ids == ids && plan.removed == removed && plan.name == name && plan.parent == parent &&
           plan.anchor == anchor && plan.action == action;
}

void ImmutableSnapshotMergeDown()
{
    constexpr char lowerText[] = "21000000-0000-4000-8000-000000000001";
    constexpr char upperText[] = "21000000-0000-4000-8000-000000000002";
    constexpr char parentText[] = "21000000-0000-4000-8000-000000000003";
    const Guid lower = Id(lowerText);
    const Guid upper = Id(upperText);
    const Guid parent = Id(parentText);
    DocumentFixture document = EmptyDocument();
    document.layers.push_back(Group(parentText, L"Folder"));
    document.layers.push_back(PixelLayer(lowerText, L"Lower", parent));
    document.layers.push_back(PixelLayer(upperText, L"Upper", parent));
    const CanvasDocument sealed = std::move(document).Seal();
    const std::optional<CanvasDocument> source{sealed};
    const std::optional<Guid> active{upper};
    const std::unordered_set<Guid> selected{upper};

    const auto plan = DocumentSession::PlanMergeForWork(source, active, selected);
    Expect(plan.has_value(), "merge-down.snapshot-produces-plan");
    if (plan)
    {
        Expect(Matches(plan.value(), {lower, upper}, {lower, upper}, L"Lower", parent, upper, L"Merge Down"),
               "merge-down.literal-ids-and-metadata");
    }
    Expect(source == std::optional<CanvasDocument>{sealed}, "merge-down-source-document-unchanged");
    Expect(active == std::optional<Guid>{upper}, "merge-down-active-input-unchanged");
    Expect(selected == std::unordered_set<Guid>{upper}, "merge-down-selected-input-unchanged");
}

void NoOpCases()
{
    constexpr char bottomText[] = "22000000-0000-4000-8000-000000000001";
    constexpr char emptyGroupText[] = "22000000-0000-4000-8000-000000000002";
    const Guid bottom = Id(bottomText);
    const Guid emptyGroup = Id(emptyGroupText);
    DocumentFixture document = EmptyDocument();
    document.layers.push_back(PixelLayer(bottomText, L"Only pixel"));
    document.layers.push_back(Group(emptyGroupText, L"Empty folder"));
    const CanvasDocument sealed = std::move(document).Seal();
    const std::optional<CanvasDocument> source{sealed};
    const std::unordered_set<Guid> none;

    Expect(!DocumentSession::PlanMergeForWork(source, bottom, none), "noop-single-bottom-layer");
    Expect(!DocumentSession::PlanMergeForWork(source, std::nullopt, none), "noop-no-active-layer");
    Expect(!DocumentSession::PlanMergeForWork(std::nullopt, bottom, none), "noop-no-document");
    Expect(!DocumentSession::PlanMergeForWork(source, emptyGroup, none), "noop-group-without-pixel-descendants");
}

void GroupDescendantsRespectSiblingParents()
{
    constexpr char rootText[] = "23000000-0000-4000-8000-000000000001";
    constexpr char firstText[] = "23000000-0000-4000-8000-000000000002";
    constexpr char nestedText[] = "23000000-0000-4000-8000-000000000003";
    constexpr char nestedPixelText[] = "23000000-0000-4000-8000-000000000004";
    constexpr char siblingText[] = "23000000-0000-4000-8000-000000000005";
    constexpr char siblingPixelText[] = "23000000-0000-4000-8000-000000000006";
    const Guid root = Id(rootText);
    const Guid first = Id(firstText);
    const Guid nested = Id(nestedText);
    const Guid nestedPixel = Id(nestedPixelText);
    const Guid sibling = Id(siblingText);
    const Guid siblingPixel = Id(siblingPixelText);
    DocumentFixture document = EmptyDocument();
    document.layers.push_back(Group(rootText, L"Root folder"));
    document.layers.push_back(PixelLayer(firstText, L"First", root));
    document.layers.push_back(Group(nestedText, L"Nested folder", root));
    document.layers.push_back(PixelLayer(nestedPixelText, L"Nested pixel", nested));
    document.layers.push_back(Group(siblingText, L"Sibling folder"));
    document.layers.push_back(PixelLayer(siblingPixelText, L"Sibling pixel", sibling));
    const CanvasDocument sealed = std::move(document).Seal();

    const auto plan = DocumentSession::PlanMergeForWork(sealed, root, {});
    Expect(plan.has_value(), "group-descendants.plan-exists");
    if (plan)
    {
        Expect(Matches(plan.value(), {root, first, nested, nestedPixel}, {root, first, nested, nestedPixel},
                       L"Root folder", std::nullopt, root, L"Merge Group"),
               "group-descendants.literal-stack-order-and-metadata");
        Expect(!plan->removed.contains(sibling) && !plan->removed.contains(siblingPixel),
               "group-descendants-excludes-sibling-folder-subtree");
    }

    // A same-parent lower pixel remains eligible even when a different-parent sibling lies between it and active.
    constexpr char childParentText[] = "23000000-0000-4000-8000-000000000007";
    constexpr char childLowerText[] = "23000000-0000-4000-8000-000000000008";
    constexpr char foreignText[] = "23000000-0000-4000-8000-000000000009";
    constexpr char childUpperText[] = "23000000-0000-4000-8000-00000000000a";
    const Guid childParent = Id(childParentText);
    const Guid childLower = Id(childLowerText);
    const Guid foreign = Id(foreignText);
    const Guid childUpper = Id(childUpperText);
    DocumentFixture siblings = EmptyDocument();
    siblings.layers.push_back(Group(childParentText, L"Child folder"));
    siblings.layers.push_back(PixelLayer(childLowerText, L"Child lower", childParent));
    siblings.layers.push_back(PixelLayer(foreignText, L"Root sibling"));
    siblings.layers.push_back(PixelLayer(childUpperText, L"Child upper", childParent));
    const CanvasDocument sealedSiblings = std::move(siblings).Seal();
    const auto down = DocumentSession::PlanMergeForWork(sealedSiblings, childUpper, {});
    Expect(down.has_value(), "merge-down-skips-other-parent-sibling");
    if (down)
    {
        Expect(Matches(down.value(), {childLower, childUpper}, {childLower, childUpper}, L"Child lower", childParent,
                       childUpper, L"Merge Down"),
               "merge-down-sibling-boundary-literal-plan");
        Expect(!down->removed.contains(foreign), "merge-down-sibling-boundary-preserves-foreign-parent");
    }
}

void MultiselectionIncludesDescendantsAndUsesTopSelectedAnchor()
{
    constexpr char baseText[] = "24000000-0000-4000-8000-000000000001";
    constexpr char selectedGroupText[] = "24000000-0000-4000-8000-000000000002";
    constexpr char descendantText[] = "24000000-0000-4000-8000-000000000003";
    constexpr char topSelectedText[] = "24000000-0000-4000-8000-000000000004";
    constexpr char unselectedText[] = "24000000-0000-4000-8000-000000000005";
    const Guid base = Id(baseText);
    const Guid selectedGroup = Id(selectedGroupText);
    const Guid descendant = Id(descendantText);
    const Guid topSelected = Id(topSelectedText);
    const Guid unselected = Id(unselectedText);
    DocumentFixture document = EmptyDocument();
    document.layers.push_back(PixelLayer(baseText, L"Base"));
    document.layers.push_back(Group(selectedGroupText, L"Selected folder"));
    document.layers.push_back(PixelLayer(descendantText, L"Folder content", selectedGroup));
    document.layers.push_back(PixelLayer(topSelectedText, L"Top selected", selectedGroup));
    document.layers.push_back(PixelLayer(unselectedText, L"Unselected", selectedGroup));
    const CanvasDocument sealed = std::move(document).Seal();
    const std::unordered_set<Guid> selected{selectedGroup, topSelected};
    const std::optional<Guid> active{selectedGroup};

    const auto plan = DocumentSession::PlanMergeForWork(sealed, active, selected);
    Expect(plan.has_value(), "multiselect.descendant-expansion-plan-exists");
    if (plan)
    {
        Expect(Matches(plan.value(), {selectedGroup, descendant, topSelected, unselected},
                       {selectedGroup, descendant, topSelected, unselected}, L"Top selected", selectedGroup,
                       topSelected, L"Merge Layers"),
               "multiselect.literal-expanded-ids-and-top-selected-metadata");
        Expect(!plan->removed.contains(base) && plan->removed.contains(descendant) &&
                   plan->removed.contains(unselected),
               "multiselect-preserves-base-and-includes-folder-descendants");
    }
    Expect(selected == std::unordered_set<Guid>{selectedGroup, topSelected}, "multiselect-selected-input-unchanged");
    Expect(active == std::optional<Guid>{selectedGroup}, "multiselect-active-input-unchanged");
}

void CancellationBeforeWorkReturnsNoPlan()
{
    constexpr char lowerText[] = "25000000-0000-4000-8000-000000000001";
    constexpr char upperText[] = "25000000-0000-4000-8000-000000000002";
    const Guid upper = Id(upperText);
    DocumentFixture document = EmptyDocument();
    document.layers.push_back(PixelLayer(lowerText, L"Lower"));
    document.layers.push_back(PixelLayer(upperText, L"Upper"));
    const CanvasDocument sealed = std::move(document).Seal();
    const std::optional<CanvasDocument> source{sealed};
    const std::optional<Guid> active{upper};
    const std::unordered_set<Guid> selected{upper};
    CancelSource cancellation;
    Expect(cancellation.Cancel(), "cancel.before-work-request-is-accepted");
    Expect(!DocumentSession::PlanMergeForWork(source, active, selected, cancellation.Token()),
           "cancel.before-work-produces-no-plan");
    Expect(source == std::optional<CanvasDocument>{sealed} && active == std::optional<Guid>{upper} &&
               selected == std::unordered_set<Guid>{upper},
           "cancel.before-work-keeps-all-inputs-unchanged");
}
} // namespace

int RunMergePlanProbeTests()
{
    gFailures = 0;
    gAssertions = 0;
    ImmutableSnapshotMergeDown();
    NoOpCases();
    GroupDescendantsRespectSiblingParents();
    MultiselectionIncludesDescendantsAndUsesTopSelectedAnchor();
    CancellationBeforeWorkReturnsNoPlan();
    std::printf("merge-plan-probe.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

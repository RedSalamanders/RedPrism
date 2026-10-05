#include "CollapseRevisionTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "LayerCopyWork.h"
#include "Selection.h"
#include "ShapeEditWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s collapse-revision.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentFixture GroupDocument(Guid& groupID, Guid& childID)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    ImageLayer group = ImageLayer::Blank(L"Group", Vec2{8, 8});
    group.isGroup = true;
    groupID = group.id;
    ImageLayer child = ImageLayer::Blank(L"Child", Vec2{8, 8});
    child.parentID = groupID;
    childID = child.id;
    document.layers.push_back(std::move(group));
    document.layers.push_back(std::move(child));
    return document;
}

DocumentSession GroupSession(Guid& groupID, Guid& childID)
{
    DocumentSession session;
    DocumentFixture fixture = GroupDocument(groupID, childID);
    session.ReplaceDocument(std::move(fixture).Seal(), childID);
    return session;
}

void RevisionTransitions()
{
    Guid groupID;
    Guid childID;
    DocumentSession session = GroupSession(groupID, childID);
    const auto initial = session.CollapseRevision();
    Expect(initial != 0, "revision.starts-nonzero");

    session.SetGroupCollapsed(groupID, true);
    const auto inserted = session.CollapseRevision();
    Expect(inserted == initial + 1 && session.CollapsedGroupIDs().contains(groupID), "insert-advances-once");
    session.SetGroupCollapsed(groupID, true);
    Expect(session.CollapseRevision() == inserted, "insert-existing-is-noop");

    session.SetGroupCollapsed(groupID, false);
    const auto erased = session.CollapseRevision();
    Expect(erased == inserted + 1 && !session.CollapsedGroupIDs().contains(groupID), "erase-advances-once");
    session.SetGroupCollapsed(groupID, false);
    Expect(session.CollapseRevision() == erased, "erase-missing-is-noop");

    session.SetGroupCollapsed(groupID, true);
    const auto populated = session.CollapseRevision();
    session.ClearCollapsedGroups();
    const auto cleared = session.CollapseRevision();
    Expect(cleared == populated + 1 && session.CollapsedGroupIDs().empty(), "clear-populated-advances-once");
    session.ClearCollapsedGroups();
    Expect(session.CollapseRevision() == cleared, "clear-empty-is-noop");
}

void CollapseIsDisplayOnly()
{
    Guid groupID;
    Guid childID;
    DocumentSession session = GroupSession(groupID, childID);
    const auto document = session.Document();
    const auto authority = session.CaptureDocumentReadAuthority();
    const auto generation = session.Generation();
    const auto serial = session.DocumentSerial();
    const auto historyRevision = session.history.CurrentRevision();
    const auto undoCount = session.history.UndoCount();
    const auto collapseRevision = session.CollapseRevision();

    session.SetGroupCollapsed(groupID, true);
    Expect(session.Document() == document && session.Generation() == generation && session.DocumentSerial() == serial &&
               session.history.CurrentRevision() == historyRevision && session.history.UndoCount() == undoCount &&
               !session.history.IsModified(),
           "collapse-does-not-edit-document-or-history");
    Expect(authority && session.IsDocumentReadCurrent(authority.value()) &&
               session.CollapseRevision() == collapseRevision + 1,
           "collapse-invalidates-display-revision-but-keeps-document-read-authority");
}

void ReplacementInvalidatesDisplaySnapshot()
{
    Guid groupID;
    Guid childID;
    DocumentSession session = GroupSession(groupID, childID);
    session.SetGroupCollapsed(groupID, true);
    const auto oldReadAuthority = session.CaptureDocumentReadAuthority();
    const auto oldSerial = session.DocumentSerial();
    const auto oldCollapseRevision = session.CollapseRevision();
    const auto oldLayersIdentity = session.Document()->layers.Identity();
    const auto preparedCollapsed = session.CollapsedGroupIDs();
    const auto preparedRows = HierarchyEntries(session.Document()->layers, true, preparedCollapsed);

    DocumentFixture copiedFixture = DocumentFixture::Copy(session.Document().value());
    CanvasDocument copiedDocument = std::move(copiedFixture).Seal();
    const auto copiedLayersIdentity = copiedDocument.layers.Identity();
    const Guid active = copiedDocument.layers.back().id;
    session.ReplaceDocument(std::move(copiedDocument), active);

    const auto replacement = session.Document();
    const bool oldDisplaySnapshotIsStale = !replacement || session.DocumentSerial() != oldSerial ||
                                           session.CollapseRevision() != oldCollapseRevision ||
                                           replacement->layers.Identity() != oldLayersIdentity;
    Expect(oldReadAuthority && !session.IsDocumentReadCurrent(oldReadAuthority.value()) &&
               session.DocumentSerial() != oldSerial,
           "replacement-invalidates-document-read-authority");
    Expect(oldDisplaySnapshotIsStale && preparedCollapsed.contains(groupID) && preparedRows.size() == 1 &&
               session.CollapsedGroupIDs().empty() && session.CollapseRevision() > oldCollapseRevision,
           "replacement-clears-collapse-and-invalidates-prepared-display-state");
    Expect(replacement && replacement->layers.Identity() == copiedLayersIdentity &&
               replacement->layers.Identity() != oldLayersIdentity,
           "copied-document-moved-into-session-retains-new-record-root");
}

void CopiedAndMovedSessionAuthority()
{
    Guid groupID;
    Guid childID;
    DocumentSession original = GroupSession(groupID, childID);
    original.SetGroupCollapsed(groupID, true);
    const auto authority = original.CaptureDocumentReadAuthority();
    const auto root = original.Document()->layers.Identity();
    const auto collapse = original.CollapseRevision();
    DocumentSession copied = original;
    Expect(copied.CollapseRevision() == collapse && copied.CollapsedGroupIDs().contains(groupID) &&
               copied.Document()->layers.Identity() == root && authority &&
               original.IsDocumentReadCurrent(authority.value()) && !copied.IsDocumentReadCurrent(authority.value()),
           "copied-session-retains-collapse-root-but-refuses-original-read-authority");
    const auto copiedAuthority = copied.CaptureDocumentReadAuthority();
    DocumentSession moved = std::move(copied);
    Expect(moved.CollapseRevision() == collapse && moved.CollapsedGroupIDs().contains(groupID) &&
               moved.Document()->layers.Identity() == root && copiedAuthority &&
               !moved.IsDocumentReadCurrent(copiedAuthority.value()),
           "moved-session-retains-collapse-root-but-renews-display-authority");
}

void PreparedShapeExpandsParent()
{
    Guid groupID;
    Guid childID;
    DocumentSession session = GroupSession(groupID, childID);
    session.SelectLayer(groupID);
    session.SetGroupCollapsed(groupID, true);
    const auto revision = session.CollapseRevision();
    InlineExecutor executor;
    const ShapeDraft draft{ShapeKind::Rectangle, Vec2{1, 1}, SelectionBounds{1, 1, 4, 4}, 0};
    auto input = session.PrepareShapeEditInput(draft, 0xFF336699u);
    auto prepared = input ? DocumentSession::PrepareShapeEditPublication(std::move(input.value()), executor, {}, {})
                          : ShapeEditResult{};
    const bool applied = prepared.status == ShapeEditStatus::Ready && prepared.publication &&
                         session.ApplyPreparedShapeEditIfCurrent(prepared.publication.value());
    Expect(applied && !session.CollapsedGroupIDs().contains(groupID) && session.CollapseRevision() == revision + 1,
           "prepared-shape-in-group-expands-parent-on-publication");
}

Raster CopyPixels()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    return Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

void PreparedLayerCopyExpandsParent()
{
    Guid groupID;
    Guid childID;
    DocumentFixture fixture = GroupDocument(groupID, childID);
    ImageLayer& child = fixture.layers.back();
    child.transform.size = Vec2{2, 2};
    const Raster pixels = CopyPixels();
    child.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), L"Child pixels"};
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), childID);
    session.SetGroupCollapsed(groupID, true);
    session.SetSelection(DocumentSelection{false, SelectionGeometry::Rect(0, 0, 2, 2)}, L"Copy selection");
    const auto revision = session.CollapseRevision();
    InlineExecutor executor;
    auto input = session.PrepareLayerCopyInput();
    auto prepared = input ? DocumentSession::PrepareLayerCopyPublication(std::move(input.value()), executor, {}, {})
                          : LayerCopyResult{};
    const bool applied = prepared.status == LayerCopyStatus::Ready && prepared.publication &&
                         session.ApplyPreparedLayerCopyIfCurrent(prepared.publication.value());
    Expect(applied && !session.CollapsedGroupIDs().contains(groupID) && session.CollapseRevision() == revision + 1,
           "prepared-layer-copy-in-group-expands-parent-on-publication");
}
} // namespace

int RunCollapseRevisionTests()
{
    gFailures = 0;
    RevisionTransitions();
    CollapseIsDisplayOnly();
    ReplacementInvalidatesDisplaySnapshot();
    CopiedAndMovedSessionAuthority();
    PreparedShapeExpandsParent();
    PreparedLayerCopyExpandsParent();
    return gFailures;
}

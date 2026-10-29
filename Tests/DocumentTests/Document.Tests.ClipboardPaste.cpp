#include "Document.Tests.ClipboardPaste.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ClipboardPasteWork.h"
#include "Document.h"
#include "Executor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s clipboard-paste.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid Id(const char* text)
{
    return Guid::Parse(text).value();
}

CanvasDocument PixelDocument(int width = 6, int height = 5)
{
    DocumentFixture document;
    document.id = Id("31000000-0000-4000-8000-000000000001");
    document.width = width;
    document.height = height;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            bytes[offset] = static_cast<std::uint8_t>(10 + x * 10 + y);
            bytes[offset + 1] = static_cast<std::uint8_t>(20 + x * 10 + y);
            bytes[offset + 2] = static_cast<std::uint8_t>(30 + x * 10 + y);
            bytes[offset + 3] = 255;
        }
    }
    const Raster image = Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    ImageLayer layer =
        ImageLayer::Blank(L"Paste source", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.id = Id("31000000-0000-4000-8000-000000000002");
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

DocumentSession SessionWithPrivateCopy()
{
    DocumentSession session;
    CanvasDocument document = PixelDocument();
    const Guid active = document.layers.front().id;
    session.ReplaceDocument(std::move(document), active);
    session.SetSelection(DocumentSelection{false, SelectionLoops{{Vec2{2, 1}, Vec2{4, 1}, Vec2{4, 2}, Vec2{2, 2}}}},
                         L"Select literal paste pixels");
    static_cast<void>(session.CopySelection());
    return session;
}

Raster ForeignPixels(int width = 2, int height = 1)
{
    if (width == 2 && height == 1)
    {
        constexpr std::array<std::uint8_t, 8> bytes{71, 72, 73, 255, 81, 82, 83, 255};
        return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    }
    constexpr std::array<std::uint8_t, 4> pixel{71, 72, 73, 255};
    const Raster base = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    return Raster::MakeSparse(width, height, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
}

bool RasterEquals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    const std::uint8_t* pixels = image.Data();
    return pixels && std::equal(expected.begin(), expected.end(), pixels);
}

bool NewLayerMatches(const DocumentSession& session, std::wstring_view name, Vec2 origin, int width, int height,
                     std::span<const std::uint8_t> pixels, const std::optional<Guid>& parent = std::nullopt)
{
    const ImageLayer* layer = session.ActiveLayer();
    return layer && layer->name == name && layer->parentID == parent && layer->asset &&
           layer->transform.origin == origin &&
           layer->transform.size == Vec2{static_cast<double>(width), static_cast<double>(height)} &&
           RasterEquals(layer->asset->image, width, height, pixels);
}

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    Guid revision;
    int undoCount = 0;
    std::size_t redoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::uint64_t generation = 0;
    std::uint64_t clipboardSerial = 0;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    std::unordered_set<Guid> collapsed;
    bool maskSelected = false;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          generation(session.Generation()), clipboardSerial(session.ClipboardSerial()), clipboard(session.Clipboard()),
          active(session.ActiveLayerID()), selected(session.SelectedLayerIDs()), collapsed(session.CollapsedGroupIDs()),
          maskSelected(session.IsMaskSelected())
    {
    }
};

bool SameClipboard(const std::optional<PixelClipboard>& left, const std::optional<PixelClipboard>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || (left->origin == right->origin && left->image == right->image));
}

bool SameOwnerState(DocumentSession& session, const OwnerState& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           history.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.Generation() == before.generation &&
           session.ClipboardSerial() == before.clipboardSerial &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.CollapsedGroupIDs() == before.collapsed &&
           session.IsMaskSelected() == before.maskSelected;
}

std::optional<PreparedClipboardPaste> PreparePrivate(DocumentSession& session, CancelToken cancellation = {},
                                                     DocumentHistory::PrepareObserver observer = {})
{
    auto input = session.PrepareClipboardPasteInput();
    return input ? DocumentSession::PrepareClipboardPastePublication(std::move(input.value()), true, {}, cancellation,
                                                                     observer)
                 : std::nullopt;
}

std::optional<PreparedClipboardPaste> PrepareForeign(DocumentSession& session, const Raster& image,
                                                     CancelToken cancellation = {},
                                                     DocumentHistory::PrepareObserver observer = {})
{
    auto input = session.PrepareClipboardPasteInput();
    return input ? DocumentSession::PrepareClipboardPastePublication(std::move(input.value()), false, image,
                                                                     cancellation, observer)
                 : std::nullopt;
}

void PrivatePasteAndHistory()
{
    DocumentSession session = SessionWithPrivateCopy();
    const OwnerState before(session);
    constexpr std::array<std::uint8_t, 8> copied{31, 41, 51, 255, 41, 51, 61, 255};
    auto prepared = PreparePrivate(session);
    Expect(prepared && SameOwnerState(session, before), "private-prepare-is-detached-and-keeps-owner-state");
    Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()), "private-publication-applies");
    Expect(NewLayerMatches(session, L"Layer 1", Vec2{2, 1}, 2, 1, copied),
           "private-paste-keeps-literal-copied-origin-and-pixels");
    Expect(session.Document() && !session.Document()->selection && session.history.UndoName() == L"Paste" &&
               session.history.UndoCount() == before.undoCount + 1,
           "private-paste-drops-selection-in-one-paste-history-entry");
    Expect(session.ClipboardSerial() == before.clipboardSerial && SameClipboard(session.Clipboard(), before.clipboard),
           "private-paste-retains-private-copy-without-exposing-a-new-image");
    Expect(!session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()), "private-publication-cannot-replay");
    session.Undo();
    Expect(session.Document() == before.document && session.ActiveLayerID() == before.active &&
               session.history.CanRedo(),
           "paste-undo-restores-source-layer-and-selection");
    session.Redo();
    Expect(NewLayerMatches(session, L"Layer 1", Vec2{2, 1}, 2, 1, copied) && !session.Document()->selection,
           "paste-redo-restores-literal-layer-and-dropped-selection");
}

void ForeignPlacementAndNames()
{
    constexpr std::array<std::uint8_t, 8> bytes{71, 72, 73, 255, 81, 82, 83, 255};
    {
        DocumentSession session;
        CanvasDocument document = PixelDocument(5, 4);
        const Guid active = document.layers.front().id;
        session.ReplaceDocument(std::move(document), active);
        auto prepared = PrepareForeign(session, ForeignPixels());
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   NewLayerMatches(session, L"Layer 1", Vec2{1, 1}, 2, 1, bytes),
               "foreign-paste-floors-odd-half-pixel-centering");
    }
    {
        DocumentSession session;
        CanvasDocument document = PixelDocument(5, 4);
        const Guid active = document.layers.front().id;
        session.ReplaceDocument(std::move(document), active);
        const Raster oversized = ForeignPixels(8, 7);
        auto prepared = PrepareForeign(session, oversized);
        Expect(
            prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) && session.ActiveLayer() &&
                session.ActiveLayer()->transform.origin == Vec2{-2, -2} &&
                session.ActiveLayer()->asset->image.Width() == 8 && session.ActiveLayer()->asset->image.Height() == 7 &&
                session.ActiveLayer()->asset->image.IsSparse() && !session.ActiveLayer()->asset->image.IsMaterialized(),
            "oversized-foreign-paste-centers-with-negative-floor-origin");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        ImageLayer first = ImageLayer::Blank(L"Layer 1", Vec2{1, 1});
        ImageLayer third = ImageLayer::Blank(L"Layer 3", Vec2{1, 1});
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    document.layers.push_back(std::move(first));
                                    document.layers.push_back(std::move(third));
                                });
        auto prepared = PreparePrivate(session);
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   NewLayerMatches(session, L"Layer 2", Vec2{2, 1}, 2, 1,
                                   std::array<std::uint8_t, 8>{31, 41, 51, 255, 41, 51, 61, 255}),
               "paste-name-uses-first-unused-layer-number");
    }
}

ImageLayer Group(const char* id, const wchar_t* name, std::optional<Guid> parent = std::nullopt)
{
    ImageLayer layer = ImageLayer::Blank(name, Vec2{1, 1});
    layer.id = Guid::Parse(id).value();
    layer.isGroup = true;
    layer.parentID = parent;
    return layer;
}

void GroupInsertionAndSiblingPlacement()
{
    DocumentSession session;
    constexpr auto outerID = "32000000-0000-4000-8000-000000000001";
    constexpr auto activeID = "32000000-0000-4000-8000-000000000002";
    constexpr auto childID = "32000000-0000-4000-8000-000000000003";
    constexpr auto nestedID = "32000000-0000-4000-8000-000000000004";
    constexpr auto nestedChildID = "32000000-0000-4000-8000-000000000005";
    constexpr auto tailID = "32000000-0000-4000-8000-000000000006";
    constexpr auto outerTailID = "32000000-0000-4000-8000-000000000007";
    constexpr auto unrelatedID = "32000000-0000-4000-8000-000000000008";
    DocumentFixture document = DocumentFixture::Copy(PixelDocument());
    const Raster sourcePixels = document.layers.front().asset->image;
    document.layers.clear();
    document.layers.push_back(Group(outerID, L"Outer"));
    document.layers.push_back(Group(activeID, L"Active folder", Guid::Parse(outerID).value()));
    ImageLayer child = ImageLayer::Blank(L"Child", Vec2{6, 5});
    child.id = Guid::Parse(childID).value();
    child.parentID = Guid::Parse(activeID).value();
    child.asset = RasterAsset{sourcePixels, Raster::MakeThumbnail(sourcePixels), child.name};
    document.layers.push_back(std::move(child));
    document.layers.push_back(Group(nestedID, L"Nested", Guid::Parse(activeID).value()));
    ImageLayer nestedChild = ImageLayer::Blank(L"Nested child", Vec2{1, 1});
    nestedChild.id = Guid::Parse(nestedChildID).value();
    nestedChild.parentID = Guid::Parse(nestedID).value();
    document.layers.push_back(std::move(nestedChild));
    ImageLayer tail = ImageLayer::Blank(L"Tail", Vec2{1, 1});
    tail.id = Guid::Parse(tailID).value();
    tail.parentID = Guid::Parse(activeID).value();
    document.layers.push_back(std::move(tail));
    ImageLayer outerTail = ImageLayer::Blank(L"Outer tail", Vec2{1, 1});
    outerTail.id = Guid::Parse(outerTailID).value();
    outerTail.parentID = Guid::Parse(outerID).value();
    document.layers.push_back(std::move(outerTail));
    document.layers.push_back(Group(unrelatedID, L"Unrelated"));
    session.ReplaceDocument(std::move(document).Seal(), Guid::Parse(childID).value());
    static_cast<void>(session.CopySelection());
    session.SelectLayer(Guid::Parse(activeID).value());
    session.SetGroupCollapsed(Guid::Parse(activeID).value(), true);
    session.SetGroupCollapsed(Guid::Parse(unrelatedID).value(), true);
    auto prepared = PreparePrivate(session);
    const Guid unrelated = Guid::Parse(unrelatedID).value();
    const auto applied = prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value());
    std::vector<std::wstring> names;
    if (session.Document())
        for (const auto& layer : session.Document()->layers)
            names.push_back(layer.name);
    const std::vector<std::wstring> expected{L"Outer", L"Active folder", L"Child",      L"Nested",   L"Nested child",
                                             L"Tail",  L"Layer 1",       L"Outer tail", L"Unrelated"};
    Expect(applied && names == expected, "active-group-paste-follows-entire-descendant-subtree");
    Expect(applied && session.ActiveLayer() && session.ActiveLayer()->parentID == Guid::Parse(activeID).value(),
           "active-group-paste-parent-is-active-group");
    Expect(applied && !session.CollapsedGroupIDs().contains(Guid::Parse(activeID).value()) &&
               session.CollapsedGroupIDs().contains(unrelated),
           "paste-expands-only-new-layer-parent");

    DocumentFixture siblings = DocumentFixture::Copy(PixelDocument());
    const Raster siblingPixels = siblings.layers.front().asset->image;
    const std::optional<RasterAsset> siblingAsset{
        RasterAsset{siblingPixels, Raster::MakeThumbnail(siblingPixels), L"Active sibling"}};
    DocumentSession sibling;
    ImageLayer below = ImageLayer::Blank(L"Sibling below", Vec2{1, 1});
    below.parentID = Guid::Parse(outerID).value();
    ImageLayer active = ImageLayer::Blank(L"Active sibling", Vec2{1, 1});
    active.asset = siblingAsset;
    active.parentID = below.parentID;
    ImageLayer above = ImageLayer::Blank(L"Sibling above", Vec2{1, 1});
    above.parentID = below.parentID;
    const Guid belowID = below.id;
    const Guid activeLayerID = active.id;
    const Guid aboveID = above.id;
    siblings.layers.push_back(Group(outerID, L"Parent"));
    siblings.layers.push_back(std::move(below));
    siblings.layers.push_back(std::move(active));
    siblings.layers.push_back(std::move(above));
    sibling.ReplaceDocument(std::move(siblings).Seal(), activeLayerID);
    static_cast<void>(sibling.CopySelection());
    auto siblingPaste = PreparePrivate(sibling);
    Expect(siblingPaste && sibling.ApplyPreparedClipboardPasteIfCurrent(siblingPaste.value()) && sibling.Document() &&
               sibling.Document()->layers[4].name == L"Layer 1" &&
               sibling.Document()->layers[4].parentID == std::optional<Guid>{Guid::Parse(outerID).value()} &&
               sibling.Document()->layers[5].id == aboveID && sibling.Document()->layers[2].id == belowID,
           "non-group-paste-inserts-above-active-within-same-parent");
}

void SerialAndStaleAuthorities()
{
    {
        auto sessionOwner = std::unique_ptr<DocumentSession>(new DocumentSession(SessionWithPrivateCopy()));
        DocumentSession& session = *sessionOwner;
        auto prepared = PreparePrivate(session);
        const auto oldSerial = session.ClipboardSerial();
        static_cast<void>(session.CopySelection());
        const OwnerState changed(session);
        Expect(session.ClipboardSerial() != oldSerial && prepared &&
                   !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) && SameOwnerState(session, changed),
               "newer-private-copy-refuses-stale-private-paste");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>();
        DocumentSession& session = *sessionOwner;
        CanvasDocument document = PixelDocument(5, 4);
        session.ReplaceDocument(std::move(document), Id("31000000-0000-4000-8000-000000000002"));
        auto prepared = PrepareForeign(session, ForeignPixels());
        static_cast<void>(session.CopySelection());
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   NewLayerMatches(session, L"Layer 1", Vec2{1, 1}, 2, 1,
                                   std::array<std::uint8_t, 8>{71, 72, 73, 255, 81, 82, 83, 255}),
               "foreign-paste-does-not-depend-on-private-clipboard-serial");
    }
    {
        auto sessionOwner = std::unique_ptr<DocumentSession>(new DocumentSession(SessionWithPrivateCopy()));
        DocumentSession& session = *sessionOwner;
        auto prepared = PreparePrivate(session);
        CanvasDocument replacement = PixelDocument();
        session.ReplaceDocument(std::move(replacement), Id("31000000-0000-4000-8000-000000000002"));
        const OwnerState changed(session);
        Expect(prepared && !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   SameOwnerState(session, changed),
               "document-replacement-stales-prepared-paste-without-mutation");
    }
    {
        auto sessionOwner = std::unique_ptr<DocumentSession>(new DocumentSession(SessionWithPrivateCopy()));
        DocumentSession& session = *sessionOwner;
        ImageLayer other = ImageLayer::Blank(L"Other active layer", Vec2{1, 1});
        const Guid otherID = other.id;
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document) { document.layers.push_back(std::move(other)); });
        auto prepared = PreparePrivate(session);
        session.SelectLayer(otherID);
        const OwnerState changed(session);
        Expect(prepared && !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   SameOwnerState(session, changed),
               "active-layer-change-stales-prepared-paste");
    }
    {
        auto sessionOwner = std::unique_ptr<DocumentSession>(new DocumentSession(SessionWithPrivateCopy()));
        DocumentSession& session = *sessionOwner;
        auto prepared = PreparePrivate(session);
        session.RenameLayer(session.ActiveLayerID().value(), L"History changed after preparation");
        const OwnerState changed(session);
        Expect(prepared && !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   SameOwnerState(session, changed),
               "history-change-stales-prepared-paste");
    }
    {
        auto sessionOwner = std::unique_ptr<DocumentSession>(new DocumentSession(SessionWithPrivateCopy()));
        DocumentSession& session = *sessionOwner;
        auto prepared = PreparePrivate(session);
        auto movedOwner = std::make_unique<DocumentSession>(std::move(session));
        DocumentSession& moved = *movedOwner;
        const OwnerState changed(moved);
        Expect(prepared && !moved.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   SameOwnerState(moved, changed),
               "moving-session-retires-prepared-session-authority");
    }
}

void SessionEligibilityAndLiveEdits()
{
    {
        DocumentSession empty;
        Expect(!empty.PrepareClipboardPasteInput() && empty.ClipboardPastePixelLimit() == 0,
               "capture-requires-a-document");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        Expect(session.BeginGradient(Vec2{2, 1}) && !session.PrepareClipboardPasteInput(),
               "capture-refuses-live-gradient");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        Expect(session.BeginPixelMove(false) && !session.PrepareClipboardPasteInput(),
               "capture-refuses-live-pixel-move");
    }
    for (int kind = 0; kind != 2; ++kind)
    {
        DocumentSession session = SessionWithPrivateCopy();
        const bool began = kind == 0 ? session.BeginBrush(Vec2{0, 0}) : session.BeginWarp(Vec2{0, 0});
        const BrushStroke* brush = session.PendingBrush();
        const WarpStroke* warp = session.PendingWarp();
        auto prepared = PreparePrivate(session);
        const bool applied = prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value());
        Expect(began && applied && session.PendingBrush() == brush && session.PendingWarp() == warp,
               kind == 0 ? "ordinary-live-brush-survives-paste-apply" : "ordinary-live-warp-survives-paste-apply");
    }
}

void SelectionMaskAndNoClipboard()
{
    {
        DocumentSession session = SessionWithPrivateCopy();
        const Guid source = session.ActiveLayerID().value();
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    constexpr std::array<std::uint8_t, 1> value{64};
                                    const auto mask =
                                        Raster::Make(1, 1, RasterFormat::Gray8, value.data(), value.size());
                                    document.layers.front().mask =
                                        LayerMask{RasterAsset{mask, mask, L"Literal mask"}, true, std::nullopt, true};
                                });
        session.SelectLayerTarget(source, true);
        const bool maskSelected = session.IsMaskSelected();
        auto prepared = PreparePrivate(session);
        const bool applied = prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value());
        Expect(maskSelected && applied && !session.IsMaskSelected() && session.ActiveLayerID() &&
                   session.SelectedLayerIDs().contains(session.ActiveLayerID().value()) &&
                   session.SelectedLayerIDs().size() == 1 && !session.Document()->selection,
               "paste-targets-new-layer-drops-selection-and-clears-mask-target");
    }
    {
        DocumentSession session;
        CanvasDocument document = PixelDocument();
        session.ReplaceDocument(std::move(document), Id("31000000-0000-4000-8000-000000000002"));
        auto input = session.PrepareClipboardPasteInput();
        auto privatePaste =
            input ? DocumentSession::PrepareClipboardPastePublication(std::move(input.value()), true) : std::nullopt;
        Expect(input && !privatePaste && !session.Clipboard(), "private-paste-refuses-missing-private-pixels");
    }
}

struct ObserverAction final
{
    DocumentHistory::PrepareStage stage{};
    CancelSource* cancellation = nullptr;
    bool throwBadAlloc = false;
    int hits = 0;
};

void ObserveHistory(void* context, DocumentHistory::PrepareStage stage)
{
    auto& action = *static_cast<ObserverAction*>(context);
    if (stage != action.stage)
        return;
    ++action.hits;
    if (action.throwBadAlloc)
        throw std::bad_alloc{};
    static_cast<void>(action.cancellation->Cancel());
}

void CancellationAndHistoryFailures()
{
    {
        DocumentSession session = SessionWithPrivateCopy();
        const OwnerState before(session);
        CancelSource cancellation;
        static_cast<void>(cancellation.Cancel());
        auto prepared = PreparePrivate(session, cancellation.Token());
        Expect(!prepared && SameOwnerState(session, before), "pre-cancelled-private-paste-preserves-all-owner-state");
    }
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : stages)
    {
        for (const bool allocationFailure : {false, true})
        {
            DocumentSession session = SessionWithPrivateCopy();
            const OwnerState before(session);
            CancelSource cancellation;
            ObserverAction action{stage, &cancellation, allocationFailure};
            const DocumentHistory::PrepareObserver observer{&ObserveHistory, &action};
            bool threw = false;
            std::optional<PreparedClipboardPaste> prepared;
            try
            {
                auto input = session.PrepareClipboardPasteInput();
                if (input)
                {
                    auto candidate = DocumentSession::PrepareClipboardPastePublication(
                        std::move(input.value()), true, {}, cancellation.Token(), observer);
                    if (candidate)
                        prepared.emplace(std::move(candidate.value()));
                }
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            Expect(action.hits == 1 && !prepared && (allocationFailure ? threw : cancellation.Token().IsCancelled()),
                   allocationFailure ? "history-stage-bad-alloc-injected" : "history-stage-cancellation-injected");
            Expect(SameOwnerState(session, before), allocationFailure
                                                        ? "bad-alloc-preserves-document-history-and-private-copy"
                                                        : "history-cancel-preserves-document-history-and-private-copy");
        }
    }
}

void InputSingleUseAndBudgetBoundaries()
{
    {
        DocumentSession session = SessionWithPrivateCopy();
        auto input = session.PrepareClipboardPasteInput();
        if (!input)
        {
            Expect(false, "fixture-captures-single-use-input");
            return;
        }
        auto prepared = DocumentSession::PrepareClipboardPastePublication(std::move(input.value()), true);
        Expect(prepared && !DocumentSession::PrepareClipboardPastePublication(std::move(input.value()), true),
               "moved-from-input-cannot-prepare-another-publication");
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()),
               "prepared-private-paste-is-single-use");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        const std::int64_t limit = session.ClipboardPastePixelLimit();
        Expect(limit > 0 && session.ClipboardPastePreparationMemoryBytes(limit) > 0 &&
                   session.ClipboardPastePreparationMemoryBytes(limit + 1) == 0 &&
                   session.ClipboardPastePreparationMemoryBytes(0) == 0,
               "pixel-budget-and-preparation-memory-boundary");
        Expect(session.ClipboardPasteRetirementMemoryBytes() > 0,
               "paste-retirement-estimate-includes-existing-private-copy");
        const Raster tooManyPixels = ForeignPixels(10000, 10000);
        auto prepared = PrepareForeign(session, tooManyPixels);
        Expect(tooManyPixels.IsSparse() && tooManyPixels.PixelCount() > limit && !prepared,
               "sparse-foreign-image-over-document-pixel-budget-is-refused-without-expansion");
    }
    {
        DocumentSession session;
        DocumentFixture document = DocumentFixture::Copy(PixelDocument(1, 1));
        document.layers.clear();
        document.layers.reserve(10'000);
        for (int i = 0; i < 10'000; ++i)
        {
            ImageLayer blank = ImageLayer::Blank(L"Cap fixture", Vec2{1, 1});
            blank.name.append(std::to_wstring(i));
            document.layers.push_back(std::move(blank));
        }
        const Guid active = document.layers.back().id;
        session.ReplaceDocument(std::move(document).Seal(), active);
        Expect(session.ClipboardPastePixelLimit() == 0 && !session.PrepareClipboardPasteInput(),
               "layer-count-cap-refuses-paste-input");
    }
}

void ExactCapacityAndRefusalState()
{
    {
        DocumentSession session;
        DocumentFixture document = DocumentFixture::Copy(PixelDocument(1, 1));
        const Raster source = ForeignPixels(100, 100);
        document.layers.front().asset = RasterAsset{source, {}, L"Sparse budget source"};
        document.layers.front().transform.size = {100, 100};
        const auto active = document.layers.front().id;
        session.ReplaceDocument(std::move(document).Seal(), active);
        const Raster image = ForeignPixels(10000, 9999);
        Expect(session.ClipboardPastePixelLimit() == 99'990'000 && image.PixelCount() == 99'990'000 &&
                   image.IsSparse() && !image.IsMaterialized(),
               "exact-remaining-sparse-budget-fixture");
        auto prepared = PrepareForeign(session, image);
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   ValidateDocument(session.Document().value()) == DocumentStatus::Ok &&
                   session.ClipboardPastePixelLimit() == 0 && session.ActiveLayer()->asset->image == image &&
                   !image.IsMaterialized() && !source.IsMaterialized(),
               "exact-aggregate-image-budget-publishes-without-materializing-sparse-owners");
        Expect(!session.PrepareClipboardPasteInput(), "exhausted-image-budget-refuses-next-paste");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        const OwnerState before(session);
        auto prepared = PrepareForeign(session, {});
        Expect(!prepared && SameOwnerState(session, before), "missing-foreign-image-preserves-all-owner-state");
        Expect(session.ClipboardPastePreparationMemoryBytes(-1) == 0 &&
                   session.ClipboardPastePreparationMemoryBytes(INT64_MAX) == 0,
               "negative-and-overflowing-pixel-admission-refused");
    }
    {
        DocumentSession session = SessionWithPrivateCopy();
        auto prepared = PreparePrivate(session);
        session.history.Begin(L"Epoch only", session.Document(), session.ActiveLayerID());
        session.history.End(session.Document(), session.ActiveLayerID());
        const OwnerState changed(session);
        Expect(prepared && !session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   SameOwnerState(session, changed),
               "history-epoch-only-change-refuses-prepared-paste");
    }
    {
        DocumentSession session;
        auto document = PixelDocument(5, 4);
        session.ReplaceDocument(std::move(document), std::nullopt);
        auto prepared = PrepareForeign(session, ForeignPixels());
        constexpr std::array<std::uint8_t, 8> expected{71, 72, 73, 255, 81, 82, 83, 255};
        Expect(prepared && session.ApplyPreparedClipboardPasteIfCurrent(prepared.value()) &&
                   session.Document()->layers.back().id == session.ActiveLayerID() &&
                   NewLayerMatches(session, L"Layer 1", {1, 1}, 2, 1, expected),
               "no-active-layer-appends-centered-foreign-layer");
    }
}
} // namespace

int RunClipboardPasteTests()
{
    gFailures = 0;
    gAssertions = 0;
    PrivatePasteAndHistory();
    ForeignPlacementAndNames();
    GroupInsertionAndSiblingPlacement();
    SerialAndStaleAuthorities();
    SessionEligibilityAndLiveEdits();
    SelectionMaskAndNoClipboard();
    CancellationAndHistoryFailures();
    InputSingleUseAndBudgetBoundaries();
    ExactCapacityAndRefusalState();
    std::printf("%d clipboard-paste assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

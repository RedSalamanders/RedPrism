#include "ClipboardCommandTests.h"

#include "../Support/DocumentFixture.h"

#include "ClipboardCommandWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s clipboard-command.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kPixels{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 4> kMask{0, 64, 128, 255};

DocumentSession Session(bool withMask = true)
{
    DocumentSession session;
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 1;
    const Raster pixels = Raster::Make(4, 1, RasterFormat::Bgra8888, kPixels.data(), kPixels.size());
    const Raster mask = Raster::Make(4, 1, RasterFormat::Gray8, kMask.data(), kMask.size());
    ImageLayer layer = ImageLayer::Blank(L"Literal clipboard source", Vec2{4, 1});
    const Guid active = layer.id;
    layer.asset = RasterAsset{pixels, {}, layer.name};
    if (withMask)
        layer.mask = LayerMask{RasterAsset{mask, mask, L"Literal clipboard mask"}, true, std::nullopt, true};
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document).Seal(), active);
    return session;
}

void SelectMiddlePixelsForFixture(DocumentSession& session)
{
    session.SetSelection(DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{3, 0}, Vec2{3, 1}, Vec2{1, 1}}}},
                         L"Literal middle pixels");
}

bool RasterEquals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    const auto* pixels = image.Data();
    if (!pixels)
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (pixels[i] != expected[i])
            return false;
    return true;
}

bool PixelEquals(const Raster& image, std::span<const std::uint8_t> expected) noexcept
{
    return RasterEquals(image, 4, 1, expected);
}

bool ClipboardEquals(const std::optional<PixelClipboard>& clipboard, int width, int height, Vec2 origin,
                     std::span<const std::uint8_t> expected) noexcept
{
    return clipboard && clipboard->origin == origin && RasterEquals(clipboard->image, width, height, expected);
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
    std::optional<PixelClipboard> clipboard;

    explicit OwnerState(const DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          generation(session.Generation()), clipboard(session.Clipboard())
    {
    }
};

bool SameOwnerState(const DocumentSession& session, const OwnerState& before)
{
    const auto& currentClipboard = session.Clipboard();
    const bool sameClipboard = currentClipboard.has_value() == before.clipboard.has_value() &&
                               (!currentClipboard || (currentClipboard->origin == before.clipboard->origin &&
                                                      currentClipboard->image == before.clipboard->image));
    const auto currentHistory = session.history.CaptureState();
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           currentHistory.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.Generation() == before.generation && sameClipboard;
}

std::optional<PreparedClipboardCommand> Prepare(DocumentSession& session, ClipboardCommandKind kind)
{
    auto input = session.PrepareClipboardCommandInput(kind);
    return input ? DocumentSession::PrepareClipboardCommandPublication(std::move(input.value())) : std::nullopt;
}

void CopyPublication()
{
    DocumentSession session = Session();
    const OwnerState before(session);
    auto prepared = Prepare(session, ClipboardCommandKind::Copy);
    Expect(prepared && prepared->Clipboard().origin == Vec2{0, 0} && PixelEquals(prepared->Clipboard().image, kPixels),
           "copy-prepares-literal-full-layer");
    Expect(prepared && session.IsPreparedClipboardCommandCurrent(prepared.value()) && SameOwnerState(session, before),
           "copy-preflight-does-not-change-document-generation-history-or-clipboard");
    if (!prepared)
        return;
    Expect(session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()), "copy-publication-succeeds");
    Expect(session.Document() == before.document && session.Generation() == before.generation &&
               session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
               session.history.UndoCount() == before.undoCount &&
               session.history.CaptureState().FutureCount() == before.redoCount &&
               ClipboardEquals(session.Clipboard(), 4, 1, Vec2{0, 0}, kPixels),
           "copy-replaces-only-private-clipboard");
    Expect(!session.IsPreparedClipboardCommandCurrent(prepared.value()) &&
               !session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()),
           "copy-publication-is-single-use");
    Expect(PixelEquals(prepared->Clipboard().image, kPixels), "encoding-source-remains-new-copy-after-publication");

    DocumentSession stale = Session();
    auto old = Prepare(stale, ClipboardCommandKind::Copy);
    const OwnerState beforeYounger(stale);
    Expect(old && stale.CopySelection(), "direct-copy-succeeds-after-captured-younger-stamp");
    Expect(beforeYounger.document == stale.Document() && beforeYounger.generation == stale.Generation() &&
               stale.history.IsCurrent(beforeYounger.history) && old &&
               !stale.IsPreparedClipboardCommandCurrent(old.value()) &&
               !stale.ApplyPreparedClipboardCommandIfCurrent(old.value()),
           "younger-direct-copy-stales-older-private-publication");

    for (int liveEdit = 0; liveEdit < 3; ++liveEdit)
    {
        DocumentSession withDraft = Session();
        bool began = false;
        if (liveEdit == 0)
            began = withDraft.BeginBrush(Vec2{0, 0});
        else if (liveEdit == 1)
        {
            withDraft.BeginLasso(Vec2{0, 0}, SelectionMode::New);
            began = withDraft.PendingLasso().has_value();
        }
        else
            began = withDraft.BeginWarp(Vec2{0, 0});
        const auto* brush = withDraft.PendingBrush();
        const auto* warp = withDraft.PendingWarp();
        const auto generation = withDraft.Generation();
        const auto history = withDraft.history.CaptureState();
        auto draftCopy = Prepare(withDraft, ClipboardCommandKind::Copy);
        Expect(began && draftCopy && draftCopy->Clipboard().origin == Vec2{0, 0} &&
                   PixelEquals(draftCopy->Clipboard().image, kPixels),
               liveEdit == 0   ? "copy-coexists-with-brush-preview"
               : liveEdit == 1 ? "copy-coexists-with-lasso-draft"
                               : "copy-coexists-with-live-warp");
        Expect(draftCopy && withDraft.ApplyPreparedClipboardCommandIfCurrent(draftCopy.value()) &&
                   withDraft.PendingBrush() == brush && withDraft.PendingWarp() == warp &&
                   (liveEdit != 1 || withDraft.PendingLasso().has_value()) && withDraft.Generation() == generation &&
                   withDraft.history.IsCurrent(history),
               "copy-publication-preserves-live-draft-and-history");
    }
}

void MergedAuthority()
{
    auto session = Session();
    SelectMiddlePixelsForFixture(session);
    constexpr std::array<std::uint8_t, 16> mergedBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const auto merged = Raster::Make(4, 1, RasterFormat::Bgra8888, mergedBytes.data(), mergedBytes.size());
    const auto authority = session.CaptureJobToken(std::nullopt, 17);
    Expect(!session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged),
           "merged-refuses-missing-composition-authority");
    Expect(!session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged,
                                                 session.CaptureJobToken(session.ActiveLayerID(), 17)),
           "merged-refuses-layer-only-composition-authority");
    Expect(!session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged,
                                                 Raster::Make(3, 1, RasterFormat::Bgra8888), authority),
           "merged-refuses-incomplete-document-grid");
    auto foreign = Session();
    Expect(!foreign.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged, authority),
           "merged-refuses-foreign-session-composition-authority");
    auto input = session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged, authority);
    auto prepared =
        input ? DocumentSession::PrepareClipboardCommandPublication(std::move(input.value())) : std::nullopt;
    Expect(prepared && prepared->Clipboard().origin == Vec2{1, 0} &&
               RasterEquals(prepared->Clipboard().image, 2, 1, std::array<std::uint8_t, 8>{4, 5, 6, 255, 7, 8, 9, 255}),
           "merged-crops-literal-composite-with-exact-origin");
    const auto generation = session.Generation();
    const auto history = session.history.CaptureState();
    Expect(prepared && session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()) &&
               session.Generation() == generation && session.history.IsCurrent(history),
           "merged-publishes-with-original-nonzero-sequence-and-no-history");
    auto olderInput = session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged, authority);
    auto older =
        olderInput ? DocumentSession::PrepareClipboardCommandPublication(std::move(olderInput.value())) : std::nullopt;
    session.RenameLayer(session.ActiveLayerID().value(), L"Changed after composition");
    const OwnerState changed(session);
    Expect(!session.PrepareClipboardCommandInput(ClipboardCommandKind::CopyMerged, merged, authority),
           "merged-cannot-rebind-stale-composite-to-current-generation");
    Expect(older && !session.ApplyPreparedClipboardCommandIfCurrent(older.value()) && SameOwnerState(session, changed),
           "merged-refuses-whole-document-change-after-preparation");
}

void RemainingAuthorityAndRefusals()
{
    {
        auto session = Session();
        session.RenameLayer(session.ActiveLayerID().value(), L"Before undo");
        session.Undo();
        const auto history = session.history.CaptureState();
        const auto generation = session.Generation();
        auto copy = Prepare(session, ClipboardCommandKind::Copy);
        Expect(history.FutureCount() == 1 && copy && session.ApplyPreparedClipboardCommandIfCurrent(copy.value()) &&
                   session.history.IsCurrent(history) && session.Generation() == generation &&
                   session.history.CaptureState().FutureCount() == 1,
               "copy-preserves-an-actual-redo-branch");
    }
    {
        auto session = Session();
        SelectMiddlePixelsForFixture(session);
        auto cut = Prepare(session, ClipboardCommandKind::Cut);
        Expect(cut && session.CopySelection(), "cut-fixture-prepares-before-younger-copy");
        const OwnerState afterCopy(session);
        Expect(cut && !session.ApplyPreparedClipboardCommandIfCurrent(cut.value()) &&
                   SameOwnerState(session, afterCopy),
               "younger-copy-refuses-both-halves-of-an-old-cut");
    }
    for (int refusal = 0; refusal < 3; ++refusal)
    {
        auto session = Session();
        Expect(session.CopySelection(), "refusal-seeds-prior-private-copy");
        if (refusal != 0)
            SelectMiddlePixelsForFixture(session);
        if (refusal == 1)
            session.ToggleVisibility(session.ActiveLayerID().value());
        if (refusal == 2)
            EditLayerRecordsForTest(session,
                                    [](DocumentFixture& document) { document.layers.front().transform.origin.x = 10; });
        const OwnerState before(session);
        auto input = session.PrepareClipboardCommandInput(ClipboardCommandKind::Cut);
        auto cut = input ? DocumentSession::PrepareClipboardCommandPublication(std::move(input.value())) : std::nullopt;
        Expect(!cut && SameOwnerState(session, before),
               refusal == 0   ? "no-selection-cut-preserves-owner-state"
               : refusal == 1 ? "hidden-cut-preserves-owner-state"
                              : "clear-that-paints-nothing-preserves-prior-copy-and-state");
    }
    {
        auto session = Session(false);
        SelectMiddlePixelsForFixture(session);
        Expect(session.BeginWarp(Vec2{1, 0}), "live-warp-cut-fixture-opens");
        const auto* warp = session.PendingWarp();
        const int before = session.history.UndoCount();
        auto cut = Prepare(session, ClipboardCommandKind::Cut);
        Expect(cut && session.ApplyPreparedClipboardCommandIfCurrent(cut.value()) && session.PendingWarp() == warp &&
                   session.history.UndoCount() == before + 1 && session.history.UndoName() == L"Clear",
               "cut-does-not-finish-or-replace-live-warp");
        const Raster cleared = session.ActiveLayer()->asset->image;
        session.FinishWarp();
        Expect(!session.PendingWarp() && session.ActiveLayer()->asset->image == cleared &&
                   session.history.UndoCount() == before + 1,
               "later-warp-closure-refuses-its-stale-source-after-cut");
    }
    {
        auto session = Session();
        SelectMiddlePixelsForFixture(session);
        session.SelectLayerTarget(session.ActiveLayerID().value(), true);
        session.maskPaintWhite = false;
        auto input = session.PrepareClipboardCommandInput(ClipboardCommandKind::Cut);
        session.maskPaintWhite = true;
        auto cut = input ? DocumentSession::PrepareClipboardCommandPublication(std::move(input.value())) : std::nullopt;
        Expect(cut && session.ApplyPreparedClipboardCommandIfCurrent(cut.value()) &&
                   session.ActiveLayer()->mask->asset.image.Data()[1] == 255 &&
                   session.ActiveLayer()->mask->asset.image.Data()[2] == 255,
               "mask-cut-uses-captured-background-palette");
    }
    {
        auto session = Session();
        Expect(session.CopySelection(), "memory-fixture-holds-old-copy");
        const auto source = session.ActiveLayer()->asset->image.RetirementMemoryBytes();
        const auto oldCopy = session.Clipboard()->image.RetirementMemoryBytes();
        Expect(session.ClipboardCommandMemoryBytes(ClipboardCommandKind::Copy) > kPixels.size() * 3 &&
                   session.ClipboardCommandRetirementMemoryBytes(ClipboardCommandKind::Copy) >= source + oldCopy,
               "copy-declares-private-output-and-both-held-source-roots");
        const auto normal = session.ClipboardCommandMemoryBytes(ClipboardCommandKind::Copy);
        EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                { document.layers.front().mask->asset.name.assign(8000, L'm'); });
        Expect(session.ClipboardCommandMemoryBytes(ClipboardCommandKind::Copy) >= normal + 8000 * sizeof(wchar_t),
               "copy-admission-includes-owned-target-mask-name");
        session.SelectLayerTarget(session.ActiveLayerID().value(), true);
        const auto color = session.ActiveLayer()->asset->image.RetirementMemoryBytes();
        const auto mask = session.ActiveLayer()->mask->asset.image.RetirementMemoryBytes();
        const auto thumbnail = session.ActiveLayer()->mask->asset.thumbnail.RetirementMemoryBytes();
        Expect(session.ClipboardCommandRetirementMemoryBytes(ClipboardCommandKind::Copy) >=
                   color + mask + thumbnail + oldCopy,
               "mask-copy-retirement-includes-color-and-semantic-thumbnail-token-owners");
    }
}

void CutPixelsAndMask()
{
    {
        DocumentSession session = Session();
        SelectMiddlePixelsForFixture(session);
        const int historyBeforeCut = session.history.UndoCount();
        const Guid layer = session.ActiveLayerID().value();
        auto prepared = Prepare(session, ClipboardCommandKind::Cut);
        Expect(prepared && prepared->CutsPixels() && prepared->Clipboard().origin == Vec2{1, 0},
               "cut-prepares-selected-two-pixel-clipboard");
        Expect(prepared && session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()) &&
                   session.history.UndoName() == L"Clear",
               "cut-publishes-one-exact-clear-entry");
        constexpr std::array<std::uint8_t, 16> hole{10, 20, 30, 255, 0, 0, 0, 0, 0, 0, 0, 0, 100, 110, 120, 255};
        const ImageLayer* active = session.ActiveLayer();
        Expect(active && PixelEquals(active->asset->image, hole) &&
                   ClipboardEquals(session.Clipboard(), 2, 1, Vec2{1, 0},
                                   std::array<std::uint8_t, 8>{40, 50, 60, 255, 70, 80, 90, 255}),
               "cut-leaves-literal-hole-and-captures-both-pixels");
        Expect(session.history.UndoCount() == historyBeforeCut + 1 && session.Document()->layers.front().id == layer,
               "cut-adds-one-history-entry-after-selection-to-original-layer");
        session.Paste();
        const ImageLayer* pasted = session.ActiveLayer();
        Expect(
            pasted && pasted->transform.origin == Vec2{1, 0} && pasted->transform.size == Vec2{2, 1} && pasted->asset &&
                RasterEquals(pasted->asset->image, 2, 1, std::array<std::uint8_t, 8>{40, 50, 60, 255, 70, 80, 90, 255}),
            "direct-paste-retains-cut-origin-and-both-pixels");
    }
    {
        DocumentSession session = Session();
        SelectMiddlePixelsForFixture(session);
        session.SelectLayerTarget(session.ActiveLayerID().value(), true);
        auto prepared = Prepare(session, ClipboardCommandKind::Cut);
        Expect(prepared && prepared->CutsPixels() && prepared->Clipboard().origin == Vec2{1, 0},
               "mask-cut-prepares-selected-region");
        Expect(prepared && session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()) &&
                   session.history.UndoName() == L"Fill Mask",
               "mask-cut-publishes-one-exact-fill-mask-entry");
        const ImageLayer* active = session.ActiveLayer();
        constexpr std::array<std::uint8_t, 4> filledMask{0, 255, 255, 255};
        Expect(active && active->mask && active->mask->asset.image.HasStorage() &&
                   active->mask->asset.image.Format() == RasterFormat::Gray8 &&
                   active->mask->asset.image.Width() == 4 && active->mask->asset.image.Height() == 1 &&
                   std::equal(filledMask.begin(), filledMask.end(), active->mask->asset.image.Data()),
               "mask-cut-fills-literal-selected-mask-samples");
    }
}

enum class FailureMode : std::uint8_t
{
    Cancel,
    BadAlloc
};

struct Failure final
{
    DocumentHistory::PrepareStage stage{};
    FailureMode mode{};
    CancelSource* cancellation = nullptr;
    int hits = 0;
};

void FailAtStage(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& failure = *static_cast<Failure*>(opaque);
    if (failure.stage != stage)
        return;
    ++failure.hits;
    if (failure.mode == FailureMode::Cancel)
        static_cast<void>(failure.cancellation->Cancel());
    else
        throw std::bad_alloc{};
}

void FailureAtomicity()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : stages)
    {
        for (const FailureMode mode : {FailureMode::Cancel, FailureMode::BadAlloc})
        {
            DocumentSession session = Session();
            Expect(session.CopySelection(), "failure-fixture-seeds-prior-clipboard");
            SelectMiddlePixelsForFixture(session);
            const OwnerState before(session);
            auto input = session.PrepareClipboardCommandInput(ClipboardCommandKind::Cut);
            CancelSource cancellation;
            Failure failure{stage, mode, &cancellation};
            const DocumentHistory::PrepareObserver observer{&FailAtStage, &failure};
            bool threw = false;
            std::optional<PreparedClipboardCommand> prepared;
            try
            {
                if (input)
                {
                    auto candidate = DocumentSession::PrepareClipboardCommandPublication(
                        std::move(input.value()), cancellation.Token(), observer);
                    if (candidate)
                        prepared.emplace(std::move(candidate.value()));
                }
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            Expect(failure.hits == 1 && !prepared &&
                       (mode == FailureMode::Cancel ? cancellation.Token().IsCancelled() : threw),
                   "history-observer-injects-selected-stage-failure");
            Expect(SameOwnerState(session, before),
                   "observer-failure-preserves-previous-clipboard-document-and-history");
        }
    }

    DocumentSession session = Session();
    Expect(session.CopySelection(), "pre-cancel-fixture-seeds-prior-clipboard");
    SelectMiddlePixelsForFixture(session);
    const OwnerState before(session);
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    auto input = session.PrepareClipboardCommandInput(ClipboardCommandKind::Cut);
    auto prepared =
        input ? DocumentSession::PrepareClipboardCommandPublication(std::move(input.value()), cancellation.Token())
              : std::nullopt;
    Expect(!prepared && SameOwnerState(session, before),
           "pre-cancelled-cut-preserves-previous-clipboard-document-and-history");
}

template <typename Mutate> void StaleAfter(const char* name, Mutate&& mutate)
{
    DocumentSession session = Session();
    auto prepared = Prepare(session, ClipboardCommandKind::Copy);
    Expect(prepared && session.IsPreparedClipboardCommandCurrent(prepared.value()), name);
    if (!prepared)
        return;
    mutate(session);
    const OwnerState changed(session);
    Expect(!session.IsPreparedClipboardCommandCurrent(prepared.value()) &&
               !session.ApplyPreparedClipboardCommandIfCurrent(prepared.value()) && SameOwnerState(session, changed),
           name);
}

void StaleInputsAndOwnership()
{
    StaleAfter("rename-stales-prepared-copy", [](DocumentSession& session)
               { session.RenameLayer(session.ActiveLayerID().value(), L"Renamed literal source"); });
    StaleAfter("selection-stales-prepared-copy",
               [](DocumentSession& session)
               {
                   session.SetSelection(
                       DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{0, 1}}}},
                       L"Select");
               });
    StaleAfter("mask-target-stales-prepared-copy",
               [](DocumentSession& session) { session.SelectLayerTarget(session.ActiveLayerID().value(), true); });
    StaleAfter("placement-stales-prepared-copy",
               [](DocumentSession& session)
               {
                   EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                           { document.layers.front().transform.origin.x = 1; });
               });
    StaleAfter("document-replacement-stales-prepared-copy",
               [](DocumentSession& session)
               {
                   CanvasDocument replacement = session.Document().value();
                   replacement.id = Guid::Generate();
                   session.ReplaceDocument(std::move(replacement), session.ActiveLayerID());
               });

    DocumentSession source = Session();
    auto input = source.PrepareClipboardCommandInput(ClipboardCommandKind::Copy);
    Expect(input.has_value(), "move-ownership-input-captured");
    if (input)
    {
        auto prepared = DocumentSession::PrepareClipboardCommandPublication(std::move(input.value()));
        Expect(prepared && !DocumentSession::PrepareClipboardCommandPublication(std::move(input.value())) &&
                   source.IsPreparedClipboardCommandCurrent(prepared.value()),
               "moved-input-cannot-be-prepared-twice");
        DocumentSession foreign = Session();
        Expect(prepared && !foreign.IsPreparedClipboardCommandCurrent(prepared.value()) &&
                   !foreign.ApplyPreparedClipboardCommandIfCurrent(prepared.value()),
               "prepared-copy-is-bound-to-capturing-session-instance");
    }
}
} // namespace

int RunClipboardCommandTests()
{
    gFailures = 0;
    gAssertions = 0;
    CopyPublication();
    MergedAuthority();
    CutPixelsAndMask();
    FailureAtomicity();
    StaleInputsAndOwnership();
    RemainingAuthorityAndRefusals();
    std::printf("%d clipboard-command assertions; %d failed.\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

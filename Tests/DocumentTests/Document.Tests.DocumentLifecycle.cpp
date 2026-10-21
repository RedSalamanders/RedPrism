#include "Document.Tests.DocumentLifecycle.h"

#include "Document.h"
#include "ImageImportHistory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace internal
{
namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s document-lifecycle.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

CanvasDocument SourceDocument()
{
    constexpr std::array<std::uint8_t, 4> kPixel{23, 47, 89, 255};
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 1;
    document.height = 1;
    ImageLayer imageLayer = ImageLayer::Blank(L"Source image", Vec2{1, 1});
    const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, kPixel.data(), kPixel.size());
    imageLayer.asset = RasterAsset{image, image, imageLayer.name};
    document.layers.push_back(std::move(imageLayer));
    document.layers.push_back(ImageLayer::Blank(L"Second layer", Vec2{1, 1}));
    return document;
}

CanvasDocument NewCanvas()
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 5;
    document.height = 7;
    document.layers.push_back(ImageLayer::Blank(L"Layer 1", Vec2{5, 7}));
    return document;
}

bool ClipboardHasSourcePixel(const DocumentSession& session) noexcept
{
    constexpr std::array<std::uint8_t, 4> kPixel{23, 47, 89, 255};
    const auto& clipboard = session.Clipboard();
    if (!clipboard || clipboard.value().image.Width() != 1 || clipboard.value().image.Height() != 1 ||
        clipboard.value().image.Data() == nullptr)
        return false;
    for (std::size_t index = 0; index < kPixel.size(); ++index)
        if (clipboard.value().image.Data()[index] != kPixel[index])
            return false;
    return true;
}

void SeedSession(DocumentSession& session)
{
    CanvasDocument source = SourceDocument();
    const Guid imageLayer = source.layers.front().id;
    session.ReplaceDocument(std::move(source), imageLayer);
    session.AddBlankLayer();
    session.SelectLayer(imageLayer);
    static_cast<void>(session.CopySelection());
    session.SetForegroundColor(0xFF315779u);
    session.SetBackgroundColor(0xFF9B7531u);
    session.SelectAll();
    CanvasDocument changed = session.Document().value();
    changed.layers.front().name = L"Changed source";
    static_cast<void>(session.ApplyImportedDocument(std::move(changed), imageLayer, L"Prior edit"));
    session.Undo();
}

std::optional<PreparedImageImport> PrepareLifecycle(DocumentSession& session, std::optional<CanvasDocument> source,
                                                    DocumentHistory::StateView history,
                                                    std::optional<CanvasDocument> replacement,
                                                    std::optional<Guid> active, std::wstring name,
                                                    CancelToken cancellation = {},
                                                    DocumentHistory::PrepareObserver observer = {})
{
    return DocumentSession::PrepareDocumentLifecyclePublication(
        session.CaptureImageImportAuthority(), std::move(source), std::move(history), std::move(replacement),
        std::move(active), std::move(name), cancellation, observer);
}

std::optional<PreparedImageImport> PrepareNewCanvas(DocumentSession& session, CancelToken cancellation = {},
                                                    DocumentHistory::PrepareObserver observer = {})
{
    CanvasDocument replacement = NewCanvas();
    const Guid active = replacement.layers.front().id;
    return PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::move(replacement), active,
                            L"New Canvas", cancellation, observer);
}

void CheckNewCanvas()
{
    DocumentSession session;
    SeedSession(session);
    const auto sourceBefore = session.Document();
    const auto historyBefore = session.history.CaptureState();
    const auto serialBefore = session.DocumentSerial();
    const auto generationBefore = session.Generation();
    const auto clipboardSerialBefore = session.ClipboardSerial();
    const auto clipboardIdentityBefore = session.Clipboard().value().image.Identity();
    const std::uint32_t foregroundBefore = session.ForegroundColor();
    const std::uint32_t backgroundBefore = session.BackgroundColor();

    CanvasDocument replacement = NewCanvas();
    const Guid newLayer = replacement.layers.front().id;
    auto prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), replacement, newLayer,
                                     L"New Canvas");
    Expect(session.Document() == sourceBefore && session.history.IsCurrent(historyBefore),
           "new-preparation-does-not-mutate-source-or-live-history");
    const bool applied = prepared && session.ApplyPreparedImageImportIfCurrent(prepared.value());
    const auto document = session.Document();
    Expect(applied && document && document.value().width == 5 && document.value().height == 7 &&
               document.value().layers.size() == 1 && document.value().layers.front().name == L"Layer 1" &&
               !document.value().layers.front().asset && document.value().layers.front().id == newLayer,
           "new-installs-literal-blank-five-by-seven-layer-one");
    Expect(applied && session.ActiveLayerID() == std::optional(newLayer) && session.SelectedLayerIDs().size() == 1 &&
               session.SelectedLayerIDs().contains(newLayer) && !session.Selection() && !session.history.IsModified() &&
               session.history.CanUndo() && !session.history.CanRedo() && session.history.UndoName() == L"New Canvas",
           "new-installs-one-saved-entry-and-layer-target");
    Expect(applied && session.DocumentSerial() > serialBefore && session.Generation() > generationBefore,
           "new-advances-document-lifetime-authority");
    Expect(applied && ClipboardHasSourcePixel(session) && session.ClipboardSerial() == clipboardSerialBefore &&
               session.Clipboard().value().image.Identity() == clipboardIdentityBefore &&
               session.ForegroundColor() == foregroundBefore && session.BackgroundColor() == backgroundBefore,
           "new-preserves-private-clipboard-and-palette");

    session.Undo();
    Expect(!session.Document() && !session.ActiveLayerID() && session.history.IsModified() &&
               !session.history.CanUndo() && session.history.CanRedo() && session.history.RedoName() == L"New Canvas",
           "new-undo-restores-absent-document");
    session.Redo();
    const auto redone = session.Document();
    Expect(redone && redone.value().width == 5 && redone.value().height == 7 && redone.value().layers.size() == 1 &&
               redone.value().layers.front().name == L"Layer 1" && !redone.value().layers.front().asset &&
               !session.history.IsModified() && !session.history.CanRedo(),
           "new-redo-restores-saved-blank-canvas");
}

void CheckClose()
{
    DocumentSession session;
    SeedSession(session);
    session.BeginColorEdit(FilterKind::Exposure, true);
    const auto serialBefore = session.DocumentSerial();
    const auto generationBefore = session.Generation();
    const auto clipboardSerialBefore = session.ClipboardSerial();
    const auto clipboardIdentityBefore = session.Clipboard().value().image.Identity();
    const std::uint32_t foregroundBefore = session.ForegroundColor();
    const std::uint32_t backgroundBefore = session.BackgroundColor();

    auto prepared =
        PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::nullopt, std::nullopt, L"");
    const bool applied = prepared && session.ApplyPreparedImageImportIfCurrent(prepared.value());
    Expect(applied && !session.Document() && !session.ActiveLayerID() && session.SelectedLayerIDs().empty() &&
               session.CollapsedGroupIDs().empty() && !session.Selection() && !session.HasColorEdit() &&
               !session.history.CanUndo() && !session.history.CanRedo() && !session.history.IsModified() &&
               session.history.CaptureState().Depth() == 0,
           "close-clears-document-targets-history-selection-and-draft");
    Expect(applied && session.DocumentSerial() > serialBefore && session.Generation() > generationBefore,
           "close-advances-document-lifetime-authority");
    Expect(applied && ClipboardHasSourcePixel(session) && session.ClipboardSerial() == clipboardSerialBefore &&
               session.Clipboard().value().image.Identity() == clipboardIdentityBefore &&
               session.ForegroundColor() == foregroundBefore && session.BackgroundColor() == backgroundBefore,
           "close-preserves-private-clipboard-and-palette");
}

void CheckAlreadyClosedRetainsRedo()
{
    DocumentSession session;
    session.CreateDocument(3, 4, true);
    session.Undo();
    const auto before = session.history.CaptureState();
    const auto serialBefore = session.DocumentSerial();
    const auto generationBefore = session.Generation();
    const std::wstring redoName = session.history.RedoName();
    auto prepared = PrepareLifecycle(session, session.Document(), before, std::nullopt, std::nullopt, L"");
    const bool applied = prepared && session.ApplyPreparedImageImportIfCurrent(prepared.value());
    Expect(applied && !session.Document() && session.history.CanRedo() && session.history.RedoName() == redoName &&
               session.history.IsCurrent(before),
           "close-of-absent-document-retains-redo-branch");
    Expect(applied && session.DocumentSerial() == serialBefore && session.Generation() == generationBefore,
           "close-of-absent-document-is-lifetime-noop");
}

void CheckPriorCancellation()
{
    DocumentSession session;
    SeedSession(session);
    const auto documentBefore = session.Document();
    const auto historyBefore = session.history.CaptureState();
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    auto prepared = PrepareNewCanvas(session, cancellation.Token());
    Expect(!prepared && session.Document() == documentBefore && session.history.IsCurrent(historyBefore),
           "prior-cancellation-refuses-without-live-mutation");
}

void CheckInvalidInputsAndDeferredClose()
{
    for (const int width : {0, kMaxCanvasSide + 1})
    {
        DocumentSession session;
        SeedSession(session);
        const auto before = session.Document();
        const auto history = session.history.CaptureState();
        const auto generation = session.Generation();
        auto replacement = NewCanvas();
        const auto active = replacement.layers.front().id;
        replacement.width = width;
        auto prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(),
                                         std::move(replacement), active, L"New Canvas");
        Expect(!prepared && session.Document() == before && session.history.IsCurrent(history) &&
                   session.Generation() == generation,
               "invalid-new-width-refuses-before-document-or-history-mutation");
    }
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), NewCanvas(),
                                         Guid::Generate(), L"New Canvas");
        Expect(!prepared, "unknown-new-active-layer-refused");
        prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::nullopt,
                                    session.ActiveLayerID(), L"");
        Expect(!prepared, "close-cannot-install-a-target-without-document");
        const auto replacement = NewCanvas();
        prepared = DocumentSession::PrepareDocumentLifecyclePublication({}, session.Document(),
                                                                        session.history.CaptureState(), replacement,
                                                                        replacement.layers.front().id, L"New Canvas");
        Expect(!prepared, "default-invalid-authority-refused");
    }
    {
        DocumentSession session;
        SeedSession(session);
        Expect(session.BeginDeferredTransform(true), "close-fixture-opens-deferred-transform");
        if (!session.PendingTransformEdit())
            return;
        auto draft = session.PendingTransformEdit()->draft;
        draft.origin = {9, 11};
        session.PreviewTransform(draft);
        const auto document = session.Document();
        const auto history = session.history.CaptureState();
        auto prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::nullopt,
                                         std::nullopt, L"");
        Expect(prepared && session.Document() == document && session.history.IsCurrent(history) &&
                   session.PendingTransformEdit() && session.PendingTransformEdit()->draft == draft,
               "close-preparation-retains-deferred-draft-and-source");
        Expect(prepared && session.ApplyPreparedImageImportIfCurrent(*prepared) && !session.Document() &&
                   !session.PendingTransformEdit() && !session.ActiveLayerID() && session.SelectedLayerIDs().empty() &&
                   !session.history.CanUndo() && !session.history.CanRedo() && !session.history.IsModified() &&
                   ClipboardHasSourcePixel(session),
               "close-publication-transfers-deferred-draft-and-clears-complete-saved-lineage");
    }
}

struct HistoryFailure final
{
    DocumentHistory::PrepareStage target;
    unsigned int hits = 0;
};

void ThrowAtHistoryStage(void* context, DocumentHistory::PrepareStage stage)
{
    auto& failure = *static_cast<HistoryFailure*>(context);
    if (stage == failure.target)
    {
        ++failure.hits;
        throw std::bad_alloc{};
    }
}

void CheckHistoryAllocationRefusal(DocumentHistory::PrepareStage stage, bool close)
{
    DocumentSession session;
    SeedSession(session);
    const auto documentBefore = session.Document();
    const auto activeBefore = session.ActiveLayerID();
    const auto historyBefore = session.history.CaptureState();
    HistoryFailure failure{stage};
    const DocumentHistory::PrepareObserver observer{&ThrowAtHistoryStage, &failure};
    bool threw = false;
    std::optional<PreparedImageImport> prepared;
    std::optional<CanvasDocument> replacement;
    std::optional<Guid> replacementActive;
    if (!close)
    {
        replacement = NewCanvas();
        replacementActive = replacement.value().layers.front().id;
    }
    try
    {
        prepared = DocumentSession::PrepareDocumentLifecyclePublication(
            session.CaptureImageImportAuthority(), session.Document(), session.history.CaptureState(),
            std::move(replacement), replacementActive, close ? std::wstring{} : std::wstring{L"New Canvas"}, {},
            observer);
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    Expect(!prepared && threw && failure.hits == 1,
           close ? "close-history-reset-allocation-failure-reached" : "new-history-entry-allocation-failure-reached");
    Expect(session.Document() == documentBefore && session.ActiveLayerID() == activeBefore &&
               session.history.IsCurrent(historyBefore) && session.history.CanRedo(),
           close ? "close-history-reset-allocation-failure-preserves-session"
                 : "new-history-entry-allocation-failure-preserves-session");
}

struct LifecycleState final
{
    std::optional<CanvasDocument> document;
    DocumentHistory::StateView history;
    Guid revision;
    bool modified = false;
    int undoCount = 0;
    bool canUndo = false;
    bool canRedo = false;
    std::wstring undoName;
    std::wstring redoName;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    std::unordered_set<Guid> collapsed;
    bool maskSelected = false;
    std::optional<DocumentSelection> selection;
    std::optional<CropRect> crop;
    bool hasColorEdit = false;
    std::optional<Guid> adjustmentEditing;
    std::uint64_t generation = 0;
    std::uint64_t documentSerial = 0;
    std::uint64_t clipboardSerial = 0;
    const void* clipboardIdentity = nullptr;
    std::uint32_t foreground = 0;
    std::uint32_t background = 0;
};

LifecycleState CaptureLifecycleState(DocumentSession& session)
{
    return {session.Document(),
            session.history.CaptureState(),
            session.history.CurrentRevision(),
            session.history.IsModified(),
            session.history.UndoCount(),
            session.history.CanUndo(),
            session.history.CanRedo(),
            session.history.UndoName(),
            session.history.RedoName(),
            session.ActiveLayerID(),
            session.SelectedLayerIDs(),
            session.CollapsedGroupIDs(),
            session.IsMaskSelected(),
            session.Selection(),
            session.CropFrame(),
            session.HasColorEdit(),
            session.AdjustmentEditingID(),
            session.Generation(),
            session.DocumentSerial(),
            session.ClipboardSerial(),
            session.Clipboard().value().image.Identity(),
            session.ForegroundColor(),
            session.BackgroundColor()};
}

bool LifecycleStateUnchanged(DocumentSession& session, const LifecycleState& before)
{
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.history.CurrentRevision() == before.revision && session.history.IsModified() == before.modified &&
           session.history.UndoCount() == before.undoCount && session.history.CanUndo() == before.canUndo &&
           session.history.CanRedo() == before.canRedo && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.CollapsedGroupIDs() == before.collapsed &&
           session.IsMaskSelected() == before.maskSelected && session.Selection() == before.selection &&
           session.CropFrame() == before.crop && session.HasColorEdit() == before.hasColorEdit &&
           session.AdjustmentEditingID() == before.adjustmentEditing && session.Generation() == before.generation &&
           session.DocumentSerial() == before.documentSerial && session.ClipboardSerial() == before.clipboardSerial &&
           session.Clipboard().value().image.Identity() == before.clipboardIdentity &&
           ClipboardHasSourcePixel(session) && session.ForegroundColor() == before.foreground &&
           session.BackgroundColor() == before.background;
}

struct HistoryCancellation final
{
    DocumentHistory::PrepareStage target;
    CancelSource* source = nullptr;
    unsigned int hits = 0;
};

void CancelAtHistoryStage(void* context, DocumentHistory::PrepareStage stage)
{
    auto& cancellation = *static_cast<HistoryCancellation*>(context);
    if (stage == cancellation.target)
    {
        ++cancellation.hits;
        static_cast<void>(cancellation.source->Cancel());
    }
}

void CheckHistoryCancellation(DocumentHistory::PrepareStage stage, bool close, const char* reachedName,
                              const char* preservedName)
{
    DocumentSession session;
    SeedSession(session);
    session.BeginColorEdit(FilterKind::Exposure, true);
    const LifecycleState before = CaptureLifecycleState(session);
    CancelSource cancellation;
    HistoryCancellation checkpoint{stage, &cancellation};
    const DocumentHistory::PrepareObserver observer{&CancelAtHistoryStage, &checkpoint};
    std::optional<PreparedImageImport> prepared;
    if (close)
    {
        prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::nullopt,
                                    std::nullopt, L"", cancellation.Token(), observer);
    }
    else
    {
        CanvasDocument replacement = NewCanvas();
        const Guid active = replacement.layers.front().id;
        prepared = PrepareLifecycle(session, session.Document(), session.history.CaptureState(), std::move(replacement),
                                    active, L"New Canvas", cancellation.Token(), observer);
    }
    Expect(checkpoint.hits == 1 && cancellation.Token().IsCancelled() && !prepared, reachedName);
    Expect(LifecycleStateUnchanged(session, before), preservedName);
}

void CheckCancellationAtHistoryStages()
{
    CheckHistoryCancellation(DocumentHistory::PrepareStage::ReplacementSnapshot, false,
                             "new-cancel-replacement-snapshot-reached",
                             "new-cancel-replacement-snapshot-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::ReplacementReset, false,
                             "new-cancel-replacement-reset-reached",
                             "new-cancel-replacement-reset-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::BeforeSnapshot, false, "new-cancel-before-snapshot-reached",
                             "new-cancel-before-snapshot-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::AfterSnapshot, false, "new-cancel-after-snapshot-reached",
                             "new-cancel-after-snapshot-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::Entry, false, "new-cancel-entry-reached",
                             "new-cancel-entry-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::StackGrowth, false, "new-cancel-stack-growth-reached",
                             "new-cancel-stack-growth-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::TrimIndex, false, "new-cancel-trim-index-reached",
                             "new-cancel-trim-index-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::ReplacementSnapshot, true,
                             "close-cancel-replacement-snapshot-reached",
                             "close-cancel-replacement-snapshot-preserves-live-state");
    CheckHistoryCancellation(DocumentHistory::PrepareStage::ReplacementReset, true,
                             "close-cancel-replacement-reset-reached",
                             "close-cancel-replacement-reset-preserves-live-state");
}

void CheckStalePublications()
{
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareNewCanvas(session);
        session.EditForTest([](CanvasDocument& document) { document.layers.front().opacity = 0.25; });
        const auto changed = session.Document();
        const auto generation = session.Generation();
        Expect(prepared && !session.ApplyPreparedImageImportIfCurrent(*prepared) && session.Document() == changed &&
                   session.Generation() == generation,
               "source-generation-staleness-refuses-without-overwriting-newer-source");
    }
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareNewCanvas(session);
        session.ReplaceDocument(session.Document().value(), session.ActiveLayerID());
        const auto replaced = session.Document();
        const auto serial = session.DocumentSerial();
        Expect(prepared && !session.ApplyPreparedImageImportIfCurrent(*prepared) && session.Document() == replaced &&
                   session.DocumentSerial() == serial,
               "same-persisted-id-replacement-refuses-old-lifetime-publication");
    }
    {
        DocumentSession session;
        SeedSession(session);
        auto authority = session.CaptureImageImportAuthority();
        auto staleSource = session.Document();
        staleSource.value().id = Guid::Generate();
        CanvasDocument replacement = NewCanvas();
        const Guid active = replacement.layers.front().id;
        auto prepared = DocumentSession::PrepareDocumentLifecyclePublication(
            std::move(authority), std::move(staleSource), session.history.CaptureState(), std::move(replacement),
            active, L"New Canvas");
        Expect(!prepared, "source-snapshot-staleness-refused-during-prepare");
    }
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareNewCanvas(session);
        const auto source = session.Document();
        const auto active = session.ActiveLayerID();
        session.history.Begin(L"Concurrent history", source, active);
        session.history.End(source, active);
        Expect(prepared && !session.ApplyPreparedImageImportIfCurrent(prepared.value()),
               "history-epoch-staleness-refused-at-apply");
    }
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareNewCanvas(session);
        const auto document = session.Document();
        session.SelectLayer(document.value().layers.back().id);
        Expect(prepared && !session.ApplyPreparedImageImportIfCurrent(prepared.value()),
               "active-target-staleness-refused-at-apply");
    }
    {
        DocumentSession owner;
        DocumentSession foreign;
        SeedSession(foreign);
        auto prepared = PrepareNewCanvas(foreign);
        Expect(prepared && !owner.ApplyPreparedImageImportIfCurrent(prepared.value()),
               "foreign-session-authority-refused-at-apply");
    }
}

void CheckMovedFromAndSingleUse()
{
    // Authority is a copyable stamp; moving it does not invalidate its source. The owning publication is
    // move-only and carries the exact history witness, retirement holders and one application right.
    {
        DocumentSession session;
        SeedSession(session);
        auto prepared = PrepareNewCanvas(session);
        Expect(prepared.has_value(), "move-fixture-prepares-complete-publication");
        if (!prepared)
            return;
        auto moved = std::move(prepared.value());
        Expect(!session.ApplyPreparedImageImportIfCurrent(prepared.value()), "moved-from-publication-refused");
        Expect(session.ApplyPreparedImageImportIfCurrent(moved), "moved-publication-applies-once");
        Expect(!session.ApplyPreparedImageImportIfCurrent(moved), "lifecycle-publication-is-single-use");
    }
}
} // namespace

int RunDocumentLifecycleTests()
{
    gFailures = 0;
    gAssertions = 0;
    CheckNewCanvas();
    CheckClose();
    CheckAlreadyClosedRetainsRedo();
    CheckPriorCancellation();
    CheckInvalidInputsAndDeferredClose();
    for (const auto stage :
         {DocumentHistory::PrepareStage::ReplacementSnapshot, DocumentHistory::PrepareStage::ReplacementReset,
          DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
          DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
          DocumentHistory::PrepareStage::TrimIndex})
        CheckHistoryAllocationRefusal(stage, false);
    CheckHistoryAllocationRefusal(DocumentHistory::PrepareStage::ReplacementReset, true);
    CheckCancellationAtHistoryStages();
    CheckStalePublications();
    CheckMovedFromAndSingleUse();
    std::printf("document-lifecycle.assertions=%d failed=%d\n", gAssertions, gFailures);
    return gFailures;
}
} // namespace internal

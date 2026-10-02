#include "WandLayerSelectionTests.h"

#include "Document.h"
#include "Selection.h"
#include "WandLayerSelectionWork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s wand-layer.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionGeometry::Rect(x, y, width, height)};
}

bool LiteralRect(const std::optional<DocumentSelection>& selection, double x, double y, double width, double height,
                 bool antialiased)
{
    if (!selection || selection->antialiased != antialiased || selection->loops.size() != 1)
        return false;
    const SelectionLoop expected{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}};
    return selection->loops.Get().front() == expected;
}

constexpr std::array<std::uint8_t, 16> kTwoByTwo{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};

DocumentSession SourceSession(bool visible = true, double opacity = 1.0, bool withMask = false, bool translated = true)
{
    DocumentSession session;
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 4;
    document.height = 4;
    ImageLayer active = ImageLayer::Blank(L"Wand proof source", Vec2{2, 2});
    const Guid activeID = active.id;
    active.asset =
        RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, kTwoByTwo.data(), kTwoByTwo.size()), {}, active.name};
    active.visible = visible;
    active.opacity = opacity;
    active.transform.origin = translated ? Vec2{1, 1} : Vec2{0, 0};
    active.transform.size = Vec2{2, 2};
    active.transform.sampling = LayerSampling::Nearest;
    if (withMask)
    {
        constexpr std::array<std::uint8_t, 4> hidden{0, 0, 0, 0};
        const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, hidden.data(), hidden.size());
        active.mask = LayerMask{RasterAsset{mask, mask, L"Hidden mask"}, true, std::nullopt, true};
    }
    document.layers.push_back(std::move(active));
    ImageLayer distractor = ImageLayer::Blank(L"Wand proof distractor", Vec2{4, 4});
    constexpr std::array<std::uint8_t, 4> otherPixel{250, 250, 250, 255};
    distractor.asset = RasterAsset{
        Raster::Make(1, 1, RasterFormat::Bgra8888, otherPixel.data(), otherPixel.size()), {}, distractor.name};
    document.layers.push_back(std::move(distractor));
    session.ReplaceDocument(std::move(document), activeID);
    return session;
}

struct State final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    DocumentHistory::StateView history;
    Guid revision;
    std::uint64_t generation = 0;
    int undoCount = 0;
    std::wstring undoName;
    std::wstring redoName;
    std::optional<CropRect> crop;
    bool maskSelected = false;
    bool moving = false;

    explicit State(const DocumentSession& session)
        : document(session.Document()), active(session.ActiveLayerID()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), generation(session.Generation()),
          undoCount(session.history.UndoCount()), undoName(session.history.UndoName()),
          redoName(session.history.RedoName()), crop(session.CropFrame()), maskSelected(session.IsMaskSelected()),
          moving(session.IsMovingSelection())
    {
    }
};

bool SameState(const DocumentSession& session, const State& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.ActiveLayerID() == before.active &&
           session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
           history.Depth() == before.history.Depth() && history.PastCount() == before.history.PastCount() &&
           history.FutureCount() == before.history.FutureCount() && session.Generation() == before.generation &&
           session.history.UndoCount() == before.undoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.CropFrame() == before.crop &&
           session.IsMaskSelected() == before.maskSelected && session.IsMovingSelection() == before.moving;
}

WandSettings Settings(WandSampleSize sample = WandSampleSize::Point, bool contiguous = true, int tolerance = 0)
{
    WandSettings settings;
    settings.sampleSize = sample;
    settings.contiguous = contiguous;
    settings.tolerance = tolerance;
    return settings;
}

std::optional<PreparedWandLayerSelection> Prepare(DocumentSession& session, Vec2 point = Vec2{1.5, 1.5},
                                                  WandSettings settings = Settings(), bool antialiased = false,
                                                  WandFailure* failure = nullptr, CancelToken cancel = {},
                                                  DocumentHistory::PrepareObserver historyObserver = {},
                                                  const WandLayerSelectionObserver* observer = nullptr)
{
    auto input = session.PrepareWandLayerSelectionInput(point, settings, antialiased);
    if (!input)
        return std::nullopt;
    return DocumentSession::PrepareWandLayerSelectionPublication(std::move(input.value()), failure, cancel,
                                                                 historyObserver, observer);
}

bool FullCanvas(const std::optional<DocumentSelection>& selection, bool antialiased = false)
{
    return LiteralRect(selection, 0, 0, 4, 4, antialiased);
}

void ActiveLayerLiteralAndCapturedSettings()
{
    DocumentSession session = SourceSession(false, 0.0, true);
    auto settings = Settings();
    settings.tolerance = 0;
    settings.contiguous = false;
    auto input = session.PrepareWandLayerSelectionInput(Vec2{1.5, 1.5}, settings, true);
    Expect(input.has_value(), "input.accepts-settled-active-layer-new");
    if (!input)
        return;
    session.wandSettings = Settings(WandSampleSize::FiveByFive, true, 255);
    session.selectionAntialiased = false;
    auto prepared = DocumentSession::PrepareWandLayerSelectionPublication(std::move(input.value()));
    Expect(prepared && prepared->ChangesSelection(), "source.prepares-a-selection");
    if (!prepared)
        return;
    Expect(session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
               LiteralRect(session.Selection(), 1, 1, 1, 1, true) && !session.selectionAntialiased,
           "source.translated-nearest-bgra-literal-rectangle-ignores-mask-opacity-visibility-and-later-settings");
    Expect(session.history.UndoName() == L"Magic Wand" && session.history.UndoCount() == 1,
           "history.changed-outline-is-one-magic-wand-entry");
    session.Undo();
    const bool undoRestored = !session.Selection() && session.history.RedoName() == L"Magic Wand";
    session.Redo();
    Expect(undoRestored && LiteralRect(session.Selection(), 1, 1, 1, 1, true),
           "history.magic-wand-undo-redo-restores-literal-output");

    DocumentSession sparseSession = SourceSession();
    constexpr std::array<std::uint8_t, 16> tilePixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                      70, 80, 90, 255, 100, 110, 120, 255};
    const Raster tile = Raster::Make(2, 2, RasterFormat::Bgra8888, tilePixels.data(), tilePixels.size());
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    sparseSession.EditForTest([&](CanvasDocument& document) { document.layers[0].asset->image = sparse; });
    auto sparseInput = sparseSession.PrepareWandLayerSelectionInput(Vec2{1.5, 1.5}, Settings(), false);
    auto sparsePrepared = sparseInput
                              ? DocumentSession::PrepareWandLayerSelectionPublication(std::move(sparseInput.value()))
                              : std::nullopt;
    Expect(sparsePrepared && sparseSession.ApplyPreparedWandLayerSelectionIfCurrent(sparsePrepared.value()) &&
               LiteralRect(sparseSession.Selection(), 1, 1, 1, 1, false) && !sparse.IsMaterialized(),
           "source.sparse-resampling-literal-outline-keeps-source-cache-unmaterialized");
}

void TransparentAndContiguousGoldens()
{
    for (const WandSampleSize sample :
         {WandSampleSize::Point, WandSampleSize::ThreeByThree, WandSampleSize::FiveByFive})
    {
        DocumentSession session;
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 4;
        ImageLayer layer = ImageLayer::Blank(L"Transparent wand", Vec2{4, 4});
        layer.isGroup = true;
        const Guid active = layer.id;
        document.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(document), active);
        auto prepared = Prepare(session, Vec2{2.5, 1.5}, Settings(sample, false));
        Expect(prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
                   FullCanvas(session.Selection()),
               sample == WandSampleSize::Point          ? "transparent.folder-point-is-literal-full-canvas"
               : sample == WandSampleSize::ThreeByThree ? "transparent.folder-3x3-is-literal-full-canvas"
                                                        : "transparent.folder-5x5-is-literal-full-canvas");
    }
    for (const bool noActive : {false, true})
    {
        DocumentSession session;
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 4;
        document.layers.push_back(ImageLayer::Blank(L"Blank transparent", Vec2{4, 4}));
        const std::optional<Guid> active = noActive ? std::nullopt : std::optional<Guid>{document.layers.front().id};
        session.ReplaceDocument(std::move(document), active);
        if (noActive)
            session.SelectLayer(std::nullopt);
        auto prepared = Prepare(session, Vec2{0.5, 0.5}, Settings(WandSampleSize::Point, true));
        Expect((!noActive || !session.ActiveLayerID()) && prepared &&
                   session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
                   FullCanvas(session.Selection()),
               noActive ? "transparent-no-active-is-literal-full-canvas" : "transparent-blank-is-literal-full-canvas");
    }

    constexpr std::array<std::uint8_t, 40> stripes{0,   0,   30,  255, 0,   0,   30,  255, 200, 200, 200, 255, 200, 200,
                                                   200, 255, 0,   0,   30,  255, 0,   0,   30,  255, 0,   0,   30,  255,
                                                   200, 200, 200, 255, 200, 200, 200, 255, 0,   0,   30,  255};
    const Raster strip = Raster::Make(5, 2, RasterFormat::Bgra8888, stripes.data(), stripes.size());
    for (const bool contiguous : {true, false})
    {
        DocumentSession session;
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 5;
        document.height = 2;
        ImageLayer layer = ImageLayer::Blank(L"Contiguous wand", Vec2{5, 2});
        const Guid active = layer.id;
        layer.asset = RasterAsset{strip, {}, layer.name};
        layer.transform.size = Vec2{5, 2};
        layer.transform.sampling = LayerSampling::Nearest;
        document.layers.push_back(std::move(layer));
        session.ReplaceDocument(std::move(document), active);
        auto prepared = Prepare(session, Vec2{0.5, 0.5}, Settings(WandSampleSize::Point, contiguous));
        const bool applied = prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value());
        const std::vector<SelectionLoop> disconnected{SelectionLoop{{0, 0}, {2, 0}, {2, 2}, {0, 2}},
                                                      SelectionLoop{{4, 0}, {5, 0}, {5, 2}, {4, 2}}};
        const bool expected = contiguous ? LiteralRect(session.Selection(), 0, 0, 2, 2, false)
                                         : (session.Selection() && !session.Selection()->antialiased &&
                                            session.Selection()->loops.Get() == disconnected);
        Expect(applied && expected, contiguous ? "outline.contiguous-literal-2x2-component"
                                               : "outline.noncontiguous-includes-both-literal-components");
    }
}

void DeselectNoMatchNoopUndoAndSavedMarker()
{
    DocumentSession session;
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 3;
    document.height = 3;
    constexpr std::array<std::uint8_t, 36> sampledPixels{100, 100, 100, 255, 100, 100, 100, 255, 100, 100, 100, 255,
                                                         100, 100, 100, 255, 109, 109, 109, 255, 100, 100, 100, 255,
                                                         100, 100, 100, 255, 100, 100, 100, 255, 100, 100, 100, 255};
    ImageLayer layer = ImageLayer::Blank(L"Average wand", Vec2{3, 3});
    const Guid active = layer.id;
    layer.asset = RasterAsset{
        Raster::Make(3, 3, RasterFormat::Bgra8888, sampledPixels.data(), sampledPixels.size()), {}, layer.name};
    layer.transform.size = Vec2{3, 3};
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document), active);
    const auto original = Rect(0, 0, 1, 1);
    session.SetSelection(original, L"Seed selection");
    session.history.MarkSaved();
    const Guid savedRevision = session.history.CurrentRevision();
    auto prepared = Prepare(session, Vec2{1.5, 1.5}, Settings(WandSampleSize::ThreeByThree, true, 0));
    Expect(prepared && prepared->ChangesSelection(), "deselect.no-match-prepares-change");
    Expect(prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && !session.Selection() &&
               session.history.UndoName() == L"Deselect",
           "deselect.average-mismatch-records-named-deselect");
    Expect(session.history.CurrentRevision() != savedRevision && session.history.IsModified(),
           "deselect.saved-revision-remains-distinct");
    session.Undo();
    Expect(session.Selection() == original && session.history.RedoName() == L"Deselect" &&
               session.history.CurrentRevision() == savedRevision && !session.history.IsModified(),
           "deselect.undo-restores-exact-selection-and-saved-revision");

    DocumentSession noOpSession = SourceSession();
    noOpSession.SetSelection(Rect(1, 1, 1, 1), L"Seed matching outline");
    noOpSession.SetSelection(Rect(0, 0, 1, 1), L"Future outline");
    noOpSession.Undo();
    const State beforeNoop(noOpSession);
    auto noOp = Prepare(noOpSession);
    Expect(noOp && !noOp->ChangesSelection() && noOpSession.ApplyPreparedWandLayerSelectionIfCurrent(noOp.value()) &&
               LiteralRect(noOpSession.Selection(), 1, 1, 1, 1, false) && SameState(noOpSession, beforeNoop),
           "noop.same-outline-retains-redo-generation-and-history-epoch");

    DocumentSession nested = SourceSession();
    nested.BeginEdit(L"Retained outer transaction");
    auto nestedResult = Prepare(nested);
    Expect(nestedResult && nested.ApplyPreparedWandLayerSelectionIfCurrent(nestedResult.value()) &&
               nested.history.CaptureState().Depth() == 1 && nested.history.UndoCount() == 0 &&
               LiteralRect(nested.Selection(), 1, 1, 1, 1, false),
           "history.nested-publication-preserves-outer-depth-and-literal-selection");
    nested.EndEdit();
    Expect(nested.history.UndoCount() == 1 && nested.history.UndoName() == L"Retained outer transaction",
           "history.outer-end-retains-original-title");

    DocumentSession saved = SourceSession();
    saved.SetSelection(Rect(0, 0, 1, 1), L"Before save");
    const auto inFlightRevision = saved.history.CurrentRevision();
    auto savedResult = Prepare(saved);
    saved.history.MarkSaved(inFlightRevision);
    Expect(savedResult && saved.IsPreparedWandLayerSelectionCurrent(savedResult.value()) &&
               saved.ApplyPreparedWandLayerSelectionIfCurrent(savedResult.value()) && saved.history.IsModified(),
           "history.save-in-flight-does-not-stale-or-replace-saved-marker");
    saved.Undo();
    Expect(saved.history.CurrentRevision() == inFlightRevision && !saved.history.IsModified(),
           "history.undo-after-save-in-flight-restores-saved-revision");
}

void CaptureAndInputOwnership()
{
    {
        DocumentSession session;
        Expect(!session.PrepareWandLayerSelectionInput(Vec2{0, 0}, Settings(), false), "capture.no-document-refused");
    }
    constexpr std::array pointNames{"negative", "right-boundary", "bottom-boundary", "nan", "infinity"};
    std::size_t pointIndex = 0;
    for (const Vec2 point : {Vec2{-0.01, 0}, Vec2{4, 0}, Vec2{0, 4}, Vec2{std::numeric_limits<double>::quiet_NaN(), 1},
                             Vec2{1, std::numeric_limits<double>::infinity()}})
    {
        DocumentSession session = SourceSession();
        std::array<char, 96> name{};
        std::format_to_n(name.data(), name.size() - 1, "capture.invalid-point-{}-refused", pointNames[pointIndex++]);
        Expect(!session.PrepareWandLayerSelectionInput(point, Settings(), false), name.data());
    }
    {
        DocumentSession session = SourceSession();
        auto allLayers = Settings();
        allLayers.sampleAllLayers = true;
        Expect(!session.PrepareWandLayerSelectionInput(Vec2{1, 1}, allLayers, false),
               "capture.sample-all-layers-refused");
    }
    {
        DocumentSession session = SourceSession();
        session.SetSelection(Rect(0, 0, 1, 1), L"Gesture seed");
        Expect(session.BeginSelectionMove(), "capture.live-gesture-fixture-started");
        const State before(session);
        const auto input = session.PrepareWandLayerSelectionInput(Vec2{1.5, 1.5}, Settings(), false);
        Expect(!input && SameState(session, before), "capture.live-gesture-refused-without-settling");
    }
    {
        DocumentSession session = SourceSession();
        auto input = session.PrepareWandLayerSelectionInput(Vec2{1.5, 1.5}, Settings(), false);
        Expect(input.has_value(), "input.move-fixture-captured");
        if (input)
        {
            WandLayerSelectionInput moved(std::move(input.value()));
            WandFailure movedFromFailure = WandFailure::None;
            auto rejected =
                DocumentSession::PrepareWandLayerSelectionPublication(std::move(input.value()), &movedFromFailure);
            Expect(!rejected && movedFromFailure == WandFailure::Memory, "input.moved-from-prototype-refuses-memory");
            auto prepared = DocumentSession::PrepareWandLayerSelectionPublication(std::move(moved));
            Expect(prepared && session.IsPreparedWandLayerSelectionCurrent(prepared.value()),
                   "input.moved-to-prototype-remains-usable");
            if (prepared)
            {
                PreparedWandLayerSelection movedPublication(std::move(prepared.value()));
                Expect(!session.IsPreparedWandLayerSelectionCurrent(prepared.value()),
                       "publication.moved-from-prototype-refuses");
                Expect(session.ApplyPreparedWandLayerSelectionIfCurrent(movedPublication),
                       "publication.moved-to-applies-once");
                Expect(!session.IsPreparedWandLayerSelectionCurrent(movedPublication) &&
                           !session.ApplyPreparedWandLayerSelectionIfCurrent(movedPublication),
                       "publication.single-use");
            }
        }
    }
}

void StaleAuthorityAndSessionOwnership()
{
    const auto stale = [](const char* name, auto mutate)
    {
        DocumentSession session = SourceSession(true, 1, true);
        auto prepared = Prepare(session);
        std::array<char, 128> opening{};
        std::format_to_n(opening.data(), opening.size() - 1, "{}-opening-is-current", name);
        Expect(prepared && session.IsPreparedWandLayerSelectionCurrent(prepared.value()), opening.data());
        if (!prepared)
            return;
        mutate(session);
        const State changed(session);
        Expect(!session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && SameState(session, changed),
               name);
    };
    stale("stale.active-layer-only-change-refused-untouched",
          [](DocumentSession& session)
          {
              const auto generation = session.Generation();
              const auto history = session.history.CaptureState();
              const auto other = session.Document()->layers[1].id;
              session.SelectLayer(other);
              Expect(session.ActiveLayerID() == other && session.Generation() == generation &&
                         session.history.IsCurrent(history),
                     "stale.active-only-fixture-changes-target-without-generation-or-history");
          });
    stale("stale.mask-target-only-change-refused-untouched",
          [](DocumentSession& session)
          {
              const auto generation = session.Generation();
              const auto history = session.history.CaptureState();
              session.SelectLayerTarget(session.ActiveLayerID().value(), true);
              Expect(session.IsMaskSelected() && session.Generation() == generation &&
                         session.history.IsCurrent(history),
                     "stale.mask-only-fixture-changes-target-without-generation-or-history");
          });
    stale("stale.crop-only-change-refused-untouched",
          [](DocumentSession& session)
          {
              const auto generation = session.Generation();
              const auto history = session.history.CaptureState();
              const CropRect crop{0, 0, 3, 3};
              session.SetCropFrame(crop);
              Expect(session.CropFrame() == crop && session.Generation() == generation &&
                         session.history.IsCurrent(history),
                     "stale.crop-only-fixture-changes-frame-without-generation-or-history");
          });
    stale("stale.history-epoch-only-change-refused-untouched",
          [](DocumentSession& session)
          {
              const auto generation = session.Generation();
              const auto history = session.history.CaptureState();
              session.BeginEdit(L"Epoch only");
              Expect(session.Generation() == generation && !session.history.IsCurrent(history) &&
                         session.history.CaptureState().Depth() == 1,
                     "stale.epoch-only-fixture-opens-history-without-generation");
          });
    stale("stale.document-generation-change-refused-untouched",
          [](DocumentSession& session)
          {
              const auto generation = session.Generation();
              session.EditForTest([](CanvasDocument& doc) { doc.resolution = 144; });
              Expect(session.Document()->resolution == 144 && session.Generation() != generation,
                     "stale.generation-fixture-reaches-document-mutation");
          });

    {
        DocumentSession owner = SourceSession();
        auto prepared = Prepare(owner);
        DocumentSession copy(owner);
        const State before(copy);
        Expect(prepared && !copy.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !copy.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && SameState(copy, before),
               "owner.copied-session-refuses-foreign-publication");
    }
    {
        DocumentSession owner = SourceSession();
        auto prepared = Prepare(owner);
        DocumentSession moved(std::move(owner));
        const State before(moved);
        Expect(prepared && !moved.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !moved.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && SameState(moved, before),
               "owner.moved-session-refuses-prior-publication");
    }
    {
        DocumentSession first = SourceSession();
        DocumentSession second = SourceSession();
        auto prepared = Prepare(first);
        const State firstBefore(first);
        const State secondBefore(second);
        Expect(prepared && !second.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
                   !second.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) &&
                   SameState(first, firstBefore) && SameState(second, secondBefore),
               "owner.foreign-session-refuses-with-both-owners-untouched");
    }
}

struct WandCheckpoint final
{
    WandLayerSelectionStage target{};
    CancelSource* cancellation = nullptr;
    int hits = 0;
};

void CancelWandStage(void* opaque, WandLayerSelectionStage stage) noexcept
{
    auto& checkpoint = *static_cast<WandCheckpoint*>(opaque);
    if (checkpoint.target == stage)
    {
        ++checkpoint.hits;
        static_cast<void>(checkpoint.cancellation->Cancel());
    }
}

struct HistoryCheckpoint final
{
    DocumentHistory::PrepareStage target{};
    CancelSource* cancellation = nullptr;
    bool throwBadAlloc = false;
    int hits = 0;
};

void VisitHistoryStage(void* opaque, DocumentHistory::PrepareStage stage)
{
    auto& checkpoint = *static_cast<HistoryCheckpoint*>(opaque);
    if (checkpoint.target != stage)
        return;
    ++checkpoint.hits;
    if (checkpoint.throwBadAlloc)
        throw std::bad_alloc{};
    if (checkpoint.cancellation)
        static_cast<void>(checkpoint.cancellation->Cancel());
}

void CancellationAndHistoryFailures()
{
    constexpr std::array stages{WandLayerSelectionStage::BeforeSource, WandLayerSelectionStage::SourceReady,
                                WandLayerSelectionStage::OutlineReady, WandLayerSelectionStage::SelectionReady,
                                WandLayerSelectionStage::DocumentReady};
    constexpr std::array names{"before-source", "source-ready", "outline-ready", "selection-ready", "document-ready"};
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        DocumentSession session = SourceSession();
        const State before(session);
        CancelSource cancel;
        WandCheckpoint checkpoint{stages[i], &cancel, 0};
        const WandLayerSelectionObserver observer{&checkpoint, &CancelWandStage};
        WandFailure failure = WandFailure::None;
        auto prepared = Prepare(session, Vec2{1.5, 1.5}, Settings(), false, &failure, cancel.Token(), {}, &observer);
        char reached[96]{};
        char unchanged[96]{};
        std::format_to_n(reached, sizeof(reached) - 1, "cancel.{}-checkpoint-reached", names[i]);
        std::format_to_n(unchanged, sizeof(unchanged) - 1, "cancel.{}-reports-cancelled-and-live-state-unchanged",
                         names[i]);
        Expect(checkpoint.hits == 1, reached);
        Expect(!prepared && failure == WandFailure::Cancelled && SameState(session, before), unchanged);
        if (i == 0)
        {
            CancelSource fresh;
            auto recovered = Prepare(session, Vec2{1.5, 1.5}, Settings(), false, nullptr, fresh.Token());
            Expect(recovered && session.ApplyPreparedWandLayerSelectionIfCurrent(recovered.value()) &&
                       LiteralRect(session.Selection(), 1, 1, 1, 1, false),
                   "cancel.fresh-token-recovers-with-literal-selection");
        }
    }

    using Stage = DocumentHistory::PrepareStage;
    constexpr std::array historyStages{Stage::SelectionSnapshot, Stage::BeforeSnapshot,
                                       Stage::AfterSnapshot,     Stage::Entry,
                                       Stage::StackGrowth,       Stage::TrimIndex};
    constexpr std::array historyNames{"selection-snapshot", "before-snapshot", "after-snapshot", "entry",
                                      "stack-growth",       "trim-index"};
    for (std::size_t i = 0; i < historyStages.size(); ++i)
    {
        DocumentSession session = SourceSession();
        if (historyStages[i] == Stage::TrimIndex)
        {
            for (int n = 0; n < kHistoryEntryLimit + 2; ++n)
                session.SetSelection(Rect(static_cast<double>(n) / 1000.0, 0, 0.5, 0.5), L"History fill");
        }
        const State before(session);
        HistoryCheckpoint checkpoint{historyStages[i], nullptr, true, 0};
        const DocumentHistory::PrepareObserver observer{&VisitHistoryStage, &checkpoint};
        bool caught = false;
        try
        {
            static_cast<void>(Prepare(session, Vec2{1.5, 1.5}, Settings(), false, nullptr, {}, observer));
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        char reached[96]{};
        char unchanged[96]{};
        std::format_to_n(reached, sizeof(reached) - 1, "history.{}-throwing-checkpoint-reached", historyNames[i]);
        std::format_to_n(unchanged, sizeof(unchanged) - 1, "history.{}-bad-alloc-preserves-live-state",
                         historyNames[i]);
        Expect(checkpoint.hits == 1 && caught, reached);
        Expect(SameState(session, before), unchanged);

        CancelSource cancellation;
        HistoryCheckpoint cancelCheckpoint{historyStages[i], &cancellation, false, 0};
        const DocumentHistory::PrepareObserver cancelObserver{&VisitHistoryStage, &cancelCheckpoint};
        WandFailure failure = WandFailure::None;
        auto cancelled =
            Prepare(session, Vec2{1.5, 1.5}, Settings(), false, &failure, cancellation.Token(), cancelObserver);
        char cancellationReached[96]{};
        char cancellationUnchanged[96]{};
        char recovery[96]{};
        std::format_to_n(cancellationReached, sizeof(cancellationReached) - 1, "history.{}-cancel-checkpoint-reached",
                         historyNames[i]);
        std::format_to_n(cancellationUnchanged, sizeof(cancellationUnchanged) - 1,
                         "history.{}-cancel-preserves-live-state", historyNames[i]);
        std::format_to_n(recovery, sizeof(recovery) - 1, "history.{}-fresh-recovery-has-literal-outline",
                         historyNames[i]);
        Expect(cancelCheckpoint.hits == 1, cancellationReached);
        Expect(!cancelled && failure == WandFailure::Cancelled && SameState(session, before), cancellationUnchanged);
        CancelSource fresh;
        auto recovered = Prepare(session, Vec2{1.5, 1.5}, Settings(), false, nullptr, fresh.Token());
        Expect(recovered && session.ApplyPreparedWandLayerSelectionIfCurrent(recovered.value()) &&
                   LiteralRect(session.Selection(), 1, 1, 1, 1, false),
               recovery);
    }

    for (const bool shutdown : {false, true})
    {
        DocumentSession session = SourceSession();
        const State before(session);
        CancelSource local;
        std::stop_source process;
        if (shutdown)
            static_cast<void>(process.request_stop());
        else
            static_cast<void>(local.Cancel());
        const CancelToken token{local.Token().LocalToken(), process.get_token()};
        WandFailure failure = WandFailure::None;
        auto prepared = Prepare(session, Vec2{1.5, 1.5}, Settings(), false, &failure, token);
        Expect(!prepared && failure == WandFailure::Cancelled && SameState(session, before),
               shutdown ? "cancel.precancelled-shutdown-token-is-cancelled"
                        : "cancel.precancelled-local-token-is-cancelled");
    }
}

void MemoryPlansAndProducerCapacities()
{
    DocumentSession empty;
    Expect(!empty.PlanWandLayerSelectionMemory({0, 0}, Settings()), "memory.no-document-refused");
    DocumentSession session = SourceSession();
    const State before(session);
    auto unsupported = Settings();
    unsupported.sampleSize = static_cast<WandSampleSize>(255);
    auto allLayers = Settings();
    allLayers.sampleAllLayers = true;
    Expect(!session.PlanWandLayerSelectionMemory({1, 1}, unsupported) &&
               !session.PrepareWandLayerSelectionInput({1, 1}, unsupported, false) &&
               !session.PlanWandLayerSelectionMemory({1, 1}, allLayers) &&
               !session.PlanWandLayerSelectionMemory({4, 1}, Settings()) && SameState(session, before),
           "memory.unsupported-settings-and-outside-point-refuse-untouched");
    const auto contiguous = session.PlanWandLayerSelectionMemory({1, 1}, Settings());
    const auto matching = session.PlanWandLayerSelectionMemory({1, 1}, Settings(WandSampleSize::Point, false));
    Expect(contiguous && matching && contiguous->sampledPixelsBytes == PixelBuffer::RequiredBytes(64) &&
               contiguous->maskBytes == 16 && contiguous->vertexBytes == 25 && contiguous->privateSourceBytes == 0 &&
               !contiguous->transparentShortcut && contiguous->floodGrowthBytes >= 4096 * sizeof(std::pair<int, int>) &&
               matching->floodGrowthBytes == 0,
           "memory.literal-4x4-source-mask-vertices-and-flood-baseline");
    Expect(contiguous && matching &&
               contiguous->preparationBytes ==
                   contiguous->captureHistoryBytes + std::max({contiguous->sourcePhaseBytes, contiguous->maskPhaseBytes,
                                                               contiguous->tracePhaseBytes}) &&
               contiguous->reservationBytes == contiguous->preparationBytes + contiguous->retirementBytes &&
               contiguous->retirementBytes > session.history.RetirementMemoryBytes(session.Document()),
           "memory.phased-peak-and-complete-retirement-reservation");
    auto prepared = Prepare(session);
    Expect(prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && contiguous &&
               session.Selection()->loops.StorageBytes() <= contiguous->geometryGrowthBytes,
           "memory.actual-literal-output-capacity-fits-geometry-allowance");

    DocumentSession sparseSession = SourceSession();
    const Raster tile = sparseSession.Document()->layers.front().asset->image;
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    sparseSession.EditForTest([&](CanvasDocument& document) { document.layers.front().asset->image = sparse; });
    const auto sparsePlan = sparseSession.PlanWandLayerSelectionMemory({1, 1}, Settings());
    Expect(sparsePlan && sparsePlan->privateSourceBytes == PixelBuffer::RequiredBytes(16) &&
               sparsePlan->sourcePhaseBytes >= sparsePlan->sampledPixelsBytes + sparsePlan->privateSourceBytes &&
               !sparse.IsMaterialized(),
           "memory.sparse-private-source-overlap-plans-without-cache-publication");
    Expect(sparsePlan &&
               sparsePlan->maskPhaseBytes >= sparsePlan->sampledPixelsBytes + sparsePlan->privateSourceBytes +
                                                 sparsePlan->maskBytes + sparsePlan->floodGrowthBytes &&
               sparsePlan->tracePhaseBytes >= sparsePlan->sampledPixelsBytes + sparsePlan->privateSourceBytes +
                                                  sparsePlan->maskBytes + sparsePlan->vertexBytes +
                                                  sparsePlan->geometryGrowthBytes,
           "memory.private-source-recycle-capacity-carried-through-mask-and-trace");
    static_cast<void>(sparse.Data());
    const auto residentPlan = sparseSession.PlanWandLayerSelectionMemory({1, 1}, Settings());
    Expect(residentPlan && sparsePlan && residentPlan->privateSourceBytes == sparsePlan->privateSourceBytes,
           "memory.sparse-residency-does-not-reduce-reservation");

    DocumentSession largeBlank;
    CanvasDocument blank;
    blank.id = Guid::Generate();
    blank.width = kMaxCanvasSide;
    blank.height = kMaxCanvasSide;
    largeBlank.ReplaceDocument(std::move(blank), std::nullopt);
    const auto blankPlan = largeBlank.PlanWandLayerSelectionMemory({0, 0}, Settings());
    auto blankPrepared = Prepare(largeBlank, {0, 0}, Settings());
    Expect(blankPlan && blankPlan->transparentShortcut && blankPlan->sampledPixelsBytes == 0 &&
               blankPlan->maskBytes == 0 && blankPlan->vertexBytes == 0 && blankPlan->floodGrowthBytes == 0 &&
               blankPrepared && largeBlank.ApplyPreparedWandLayerSelectionIfCurrent(blankPrepared.value()) &&
               LiteralRect(largeBlank.Selection(), 0, 0, kMaxCanvasSide, kMaxCanvasSide, false),
           "memory.maximum-canvas-transparent-shortcut-owns-only-literal-quad");

    DocumentSession largeSource = SourceSession();
    largeSource.EditForTest(
        [](CanvasDocument& document)
        {
            document.width = kMaxCanvasSide;
            document.height = kMaxCanvasSide;
        });
    const auto largePlan = largeSource.PlanWandLayerSelectionMemory({1, 1}, Settings());
    Expect(largePlan && largePlan->maskBytes == 900'000'000 && largePlan->vertexBytes == 900'060'001 &&
               largePlan->reservationBytes > std::numeric_limits<std::uint32_t>::max(),
           "memory.maximum-canvas-nonblank-plan-uses-64bit-without-pixel-allocation");
    largeSource.EditForTest([](CanvasDocument& document) { document.width = kMaxCanvasSide + 1; });
    Expect(!largeSource.PlanWandLayerSelectionMemory({1, 1}, Settings()),
           "memory.invalid-grid-refuses-before-allocation");

    // Independent observation of the pinned default allocator's actual before/after capacities. The
    // source proof includes old/new overlap; no universal standard-library growth assumption is made.
    std::vector<std::pair<int, int>> seeds;
    seeds.reserve(4096);
    bool growthFits = seeds.capacity() == 4096;
    for (int i = 0; i < 50'000; ++i)
    {
        const auto old = seeds.capacity();
        seeds.emplace_back(i, i);
        const auto overlap = old == seeds.capacity() ? seeds.capacity() : old + seeds.capacity();
        growthFits = growthFits && overlap <= 3 * std::max<std::size_t>(4096, seeds.size());
    }
    Expect(growthFits, "memory.pinned-default-vector-growth-and-old-new-overlap-observed");

    static_cast<void>(session.BeginSelectionMove());
    Expect(session.IsMovingSelection() && !session.PlanWandLayerSelectionMemory({1, 1}, Settings()),
           "memory.reached-live-move-refuses-planning-without-settling");
}

void WorkerPublicationAndAggregateRetirement()
{
    for (const bool stale : {false, true})
    {
        DocumentSession session = SourceSession();
        session.SetSelection(Rect(0, 0, 2, 2), L"Retired outline");
        auto input = session.PrepareWandLayerSelectionInput({1.5, 1.5}, Settings(), false);
        Expect(input.has_value(), stale ? "worker.stale-fixture-captured" : "worker.apply-fixture-captured");
        if (!input)
            continue;
        std::mutex mutex;
        std::condition_variable condition;
        bool ready = false, retire = false;
        std::thread::id retiredOn;
        struct Aggregate final
        {
            std::optional<WandLayerSelectionInput> input;
            std::optional<PreparedWandLayerSelection> result;
            std::thread::id& retiredOn;
            explicit Aggregate(WandLayerSelectionInput value, std::thread::id& thread)
                : input(std::move(value)), retiredOn(thread)
            {
            }
            ~Aggregate()
            {
                result.reset();
                input.reset();
                retiredOn = std::this_thread::get_id();
            }
        };
        auto owned = std::make_unique<Aggregate>(std::move(input.value()), retiredOn);
        auto* borrowed = owned.get();
        std::jthread worker(
            [&, graph = std::move(owned)]() mutable
            {
                graph->result = DocumentSession::PrepareWandLayerSelectionPublication(std::move(graph->input.value()));
                graph->input.reset();
                {
                    std::unique_lock lock(mutex);
                    ready = true;
                    condition.notify_one();
                    condition.wait(lock, [&] { return retire; });
                }
                graph.reset();
            });
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [&] { return ready; });
        }
        if (stale)
            session.SelectLayer(session.Document()->layers.back().id);
        std::optional<State> atApply{std::in_place, session};
        const bool applied =
            borrowed->result && session.ApplyPreparedWandLayerSelectionIfCurrent(borrowed->result.value());
        Expect(stale ? !applied && SameState(session, atApply.value())
                     : applied && LiteralRect(session.Selection(), 1, 1, 1, 1, false),
               stale ? "worker.stale-drop-preserves-owner-state" : "worker.literal-publication-applies-on-owner");
        // The closed session relinquishes all live/history owners before the worker releases the whole
        // real result graph (including pre-publication document/display/history roots swapped into it).
        session.ClearDocument();
        atApply.reset(); // The test must not retain the source/document/history roots on its owner thread.
        const auto workerID = worker.get_id();
        {
            const std::lock_guard lock(mutex);
            retire = true;
            condition.notify_one();
        }
        worker.join();
        Expect(!atApply && !session.Document() && retiredOn == workerID && retiredOn != std::this_thread::get_id(),
               stale ? "worker.stale-whole-result-retires-after-owner-close-on-worker"
                     : "worker.applied-whole-result-retires-after-owner-close-on-worker");
    }
}
} // namespace

int RunWandLayerSelectionTests()
{
    gAssertions = 0;
    gFailures = 0;
    ActiveLayerLiteralAndCapturedSettings();
    TransparentAndContiguousGoldens();
    DeselectNoMatchNoopUndoAndSavedMarker();
    CaptureAndInputOwnership();
    StaleAuthorityAndSessionOwnership();
    CancellationAndHistoryFailures();
    MemoryPlansAndProducerCapacities();
    WorkerPublicationAndAggregateRetirement();
    std::printf("wand-layer.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

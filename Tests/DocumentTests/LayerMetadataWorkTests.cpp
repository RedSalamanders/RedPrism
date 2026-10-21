#include "LayerMetadataWorkTests.h"

#include "../Support/DocumentFixture.h"

#include "Document.h"
#include "Executor.h"
#include "LayerMetadataWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s layer-metadata.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid Id(const char* text)
{
    return Guid::Parse(text).value();
}

constexpr Guid kDocumentId{{0x71, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
constexpr Guid kTargetId{{0x71, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
constexpr Guid kSiblingId{{0x71, 0, 0, 3, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 3}};
constexpr Guid kGroupId{{0x71, 0, 0, 4, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 4}};

CanvasDocument FixtureDocument()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster maskRaster =
        Raster::Make(2, 2, RasterFormat::Gray8, std::array<std::uint8_t, 4>{25, 75, 125, 200}.data(), 4);
    ImageLayer target = ImageLayer::Blank(L"Original", Vec2{2, 2});
    target.id = kTargetId;
    target.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), L"Original pixels"};
    target.mask = LayerMask{RasterAsset{maskRaster, Raster::MakeThumbnail(maskRaster), L"Original mask"}};
    target.opacity = 0.75;
    target.blendMode = LayerBlendMode::Multiply;

    ImageLayer sibling = ImageLayer::Blank(L"Sibling", Vec2{2, 2});
    sibling.id = kSiblingId;
    sibling.visible = false;
    sibling.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), L"Sibling pixels"};

    ImageLayer group = ImageLayer::Blank(L"Folder", Vec2{2, 2});
    group.id = kGroupId;
    group.isGroup = true;

    DocumentFixture document;
    document.id = kDocumentId;
    document.width = 2;
    document.height = 2;
    document.layers = {group, target, sibling};
    document.layers[1].parentID = kGroupId;
    document.layers[2].parentID = kGroupId;
    document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}}}};
    return std::move(document).Seal();
}

DocumentSession MakeSession()
{
    DocumentSession session;
    session.ReplaceDocument(FixtureDocument(), kTargetId);
    return session;
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
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    std::unordered_set<Guid> collapsed;
    bool maskSelected = false;

    explicit OwnerState(DocumentSession& session)
        : document(session.Document()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), undoCount(session.history.UndoCount()),
          redoCount(history.FutureCount()), undoName(session.history.UndoName()), redoName(session.history.RedoName()),
          generation(session.Generation()), active(session.ActiveLayerID()), selected(session.SelectedLayerIDs()),
          collapsed(session.CollapsedGroupIDs()), maskSelected(session.IsMaskSelected())
    {
    }
};

bool SameOwnerState(DocumentSession& session, const OwnerState& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.history.CurrentRevision() == before.revision && session.history.UndoCount() == before.undoCount &&
           history.FutureCount() == before.redoCount && session.history.UndoName() == before.undoName &&
           session.history.RedoName() == before.redoName && session.Generation() == before.generation &&
           session.ActiveLayerID() == before.active && session.SelectedLayerIDs() == before.selected &&
           session.CollapsedGroupIDs() == before.collapsed && session.IsMaskSelected() == before.maskSelected;
}

std::optional<PreparedLayerMetadata> Prepare(DocumentSession& session, LayerMetadataKind kind, const Guid& target,
                                             std::wstring_view name = {}, double opacity = 1.0,
                                             LayerBlendMode blend = LayerBlendMode::Normal,
                                             CancelToken cancellation = {},
                                             DocumentHistory::PrepareObserver historyObserver = {},
                                             LayerMetadataPrepareObserver observer = {})
{
    auto input = session.PrepareLayerMetadataInput(kind, target, name, opacity, blend);
    if (!input)
        return std::nullopt;
    return PrepareLayerMetadataPublication(std::move(input.value()), cancellation, historyObserver, observer);
}

Raster ResidencyRaster()
{
    constexpr std::array<std::uint8_t, 4> pixel{17, 39, 83, 255};
    const Raster leaf = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    return Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, leaf}}, 0);
}

DocumentSession MakeResidencySession()
{
    constexpr Guid layerId{{0x72, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
    constexpr Guid documentId{{0x72, 0, 0, 2, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 2}};
    ImageLayer layer = ImageLayer::Blank(L"Residency source", Vec2{2, 1});
    layer.id = layerId;
    layer.asset = RasterAsset{ResidencyRaster(), {}, L"literal sparse pixels"};
    DocumentFixture fixture;
    fixture.id = documentId;
    fixture.width = 2;
    fixture.height = 1;
    fixture.layers.push_back(std::move(layer));
    DocumentSession session;
    session.ReplaceDocument(std::move(fixture).Seal(), layerId);
    return session;
}

struct ResidencyFailure final
{
    bool reached = false;
};

void FailResidencyRefreshAtBounds(void* raw, DocumentHistory::PrepareStage stage)
{
    if (stage == DocumentHistory::PrepareStage::StorageIndexBounds)
    {
        static_cast<ResidencyFailure*>(raw)->reached = true;
        throw std::bad_alloc{};
    }
}

Raster ResidencyImage(const DocumentSession& session)
{
    return session.Document().value().layers.front().asset.value().image;
}

void PreparedHistoryResidencyCallers()
{
    using Validation = DocumentHistory::ResidencyValidation;
    constexpr Guid layerId{{0x72, 0, 0, 1, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 1}};
    {
        DocumentSession session = MakeResidencySession();
        const OwnerState before(session);
        auto prepared = Prepare(session, LayerMetadataKind::Rename, layerId, L"One edit");
        DocumentHistory::ResidencyPermit permit;
        const auto ready = prepared ? prepared->TryAcquireHistoryResidency(permit) : Validation::Invalid;
        const bool unchangedWhileHeld = SameOwnerState(session, before) && session.history.UndoCount() == 0 &&
                                        session.Document()->layers.front().name == L"Residency source";
        Expect(prepared && ready == Validation::Ready && unchangedWhileHeld &&
                   session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document()->layers.front().name == L"One edit" && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Rename Layer",
               "residency-ready-keeps-live-state-until-one-original-edit-applies");
        permit.Reset();
        Expect(prepared && prepared->TryAcquireHistoryResidency(permit) == Validation::Invalid &&
                   !prepared->RefreshHistoryResidency() && session.history.UndoCount() == 1,
               "residency-applied-publication-is-invalid-and-cannot-refresh-again");
    }
    {
        DocumentSession session = MakeResidencySession();
        const OwnerState before(session);
        const Guid openingRevision = session.history.CurrentRevision();
        auto prepared = Prepare(session, LayerMetadataKind::Rename, layerId, L"After cache growth");
        const std::uint64_t epoch = Raster::ResidencyEpoch();
        const Raster sparse = ResidencyImage(session);
        const std::uint8_t* pixels = sparse.Data();
        const bool literalGrowth = pixels && pixels[0] == 17 && pixels[1] == 39 && pixels[2] == 83 &&
                                   pixels[3] == 255 && pixels[4] == 0 && pixels[5] == 0 && pixels[6] == 0 &&
                                   pixels[7] == 0 && sparse.ByteSize() == 8 && Raster::ResidencyEpoch() == epoch + 1;
        DocumentHistory::ResidencyPermit permit;
        const auto refresh = prepared ? prepared->TryAcquireHistoryResidency(permit) : Validation::Invalid;
        CancelSource cancellation;
        (void)cancellation.Cancel();
        const bool cancelPreserves = prepared && !prepared->RefreshHistoryResidency(cancellation.Token()) &&
                                     prepared->TryAcquireHistoryResidency(permit) == Validation::Refresh &&
                                     SameOwnerState(session, before) &&
                                     session.history.CurrentRevision() == openingRevision;
        ResidencyFailure failure;
        const bool allocationStillPropagates = prepared && [&]()
        {
            try
            {
                (void)prepared->RefreshHistoryResidency({}, {&FailResidencyRefreshAtBounds, &failure});
                return false; // The wrapper must propagate bad_alloc for the scheduler's E_OUTOFMEMORY boundary.
            }
            catch (const std::bad_alloc&)
            {
                return true;
            }
        }();
        const bool failedRefreshPreserves = failure.reached && allocationStillPropagates &&
                                            prepared->TryAcquireHistoryResidency(permit) == Validation::Refresh &&
                                            SameOwnerState(session, before) &&
                                            session.history.CurrentRevision() == openingRevision;
        const bool refreshed = prepared && prepared->RefreshHistoryResidency();
        const auto ready = prepared ? prepared->TryAcquireHistoryResidency(permit) : Validation::Invalid;
        bool publishOriginal = refreshed && ready == Validation::Ready && SameOwnerState(session, before) &&
                               session.history.CurrentRevision() == openingRevision &&
                               session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                               session.Document()->layers.front().name == L"After cache growth" &&
                               session.history.UndoCount() == 1 && session.history.UndoName() == L"Rename Layer";
        if (publishOriginal)
        {
            session.Undo();
            const bool undone = session.Document()->layers.front().name == L"Residency source";
            session.Redo();
            publishOriginal = undone && session.Document()->layers.front().name == L"After cache growth" &&
                              session.history.UndoCount() == 1;
        }
        permit.Reset();
        Expect(prepared && literalGrowth && refresh == Validation::Refresh,
               "residency-late-literal-cache-growth-requests-worker-refresh");
        Expect(cancelPreserves, "residency-cancelled-refresh-retains-original-edit-and-live-revision");
        Expect(failedRefreshPreserves, "residency-allocation-failed-refresh-retains-original-edit-and-live-revision");
        Expect(publishOriginal, "residency-refresh-reuses-sealed-edit-with-one-undo-redo-entry");
    }
    {
        DocumentSession session = MakeResidencySession();
        auto prepared = Prepare(session, LayerMetadataKind::Rename, layerId, L"Stale");
        session.RenameLayer(layerId, L"Younger live edit");
        DocumentHistory::ResidencyPermit permit;
        const bool residencyIsNotAuthority = prepared &&
                                             prepared->TryAcquireHistoryResidency(permit) == Validation::Ready &&
                                             !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                                             !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                                             session.Document()->layers.front().name == L"Younger live edit";
        permit.Reset();
        auto movable = Prepare(session, LayerMetadataKind::Rename, layerId, L"Moved handle");
        std::optional<PreparedLayerMetadata> moved;
        if (movable)
            moved.emplace(std::move(movable.value()));
        const bool movedFromInvalid = movable && moved &&
                                      movable->TryAcquireHistoryResidency(permit) == Validation::Invalid &&
                                      !movable->RefreshHistoryResidency();
        Expect(residencyIsNotAuthority, "residency-stale-document-authority-is-refused-at-apply-after-ready-permit");
        Expect(movedFromInvalid, "residency-moved-from-prepared-output-is-invalid");
    }
    {
        DocumentSession session = MakeResidencySession();
        auto prepared = Prepare(session, LayerMetadataKind::Rename, layerId, L"Retry after busy");
        if (prepared)
        {
            std::latch acquired{1};
            std::latch release{1};
            bool workerHeld = false;
            std::thread worker(
                [&]
                {
                    auto lease = Raster::AcquireResidencyLease();
                    workerHeld = lease.Held();
                    acquired.count_down();
                    release.wait();
                });
            acquired.wait();
            DocumentHistory::ResidencyPermit permit;
            const auto busy = prepared->TryAcquireHistoryResidency(permit);
            release.count_down();
            worker.join();
            const auto retried = prepared->TryAcquireHistoryResidency(permit);
            permit.Reset();
            Expect(workerHeld && busy == Validation::Busy && retried == Validation::Ready &&
                       session.history.UndoCount() == 0 &&
                       session.Document()->layers.front().name == L"Residency source",
                   "residency-busy-is-nonmutating-and-same-output-retries-ready");
        }
        else
        {
            Expect(false, "residency-busy-is-nonmutating-and-same-output-retries-ready");
        }
    }
}

constexpr std::array kFlipKinds{LayerMetadataKind::FlipHorizontal, LayerMetadataKind::FlipVertical};

const char* FlipName(LayerMetadataKind kind) noexcept
{
    return kind == LayerMetadataKind::FlipHorizontal ? "flip-horizontal" : "flip-vertical";
}

const wchar_t* FlipHistoryName(LayerMetadataKind kind) noexcept
{
    return kind == LayerMetadataKind::FlipHorizontal ? L"Flip Horizontal" : L"Flip Vertical";
}

void FlipValuesAndLiteralHistory()
{
    // Literal metadata expectations from archived behavioral reference,24-49.
    // This is an independent application of that formula, never Mirrored() as the reference.
    for (const auto kind : kFlipKinds)
    {
        for (const bool flipX : {false, true})
        {
            for (const bool flipY : {false, true})
            {
                const std::string label =
                    std::string(FlipName(kind)) + (flipX ? "-x1" : "-x0") + (flipY ? "-y1" : "-y0");
                auto fixture = DocumentFixture::Copy(FixtureDocument());
                fixture.width = 400;
                fixture.height = 200;
                auto& transform = fixture.layers[1].transform;
                transform.origin = Vec2{20, 30};
                transform.size = Vec2{100, 50};
                transform.rotation = 10;
                transform.flipX = flipX;
                transform.flipY = flipY;
                transform.sampling = LayerSampling::Nearest;
                DocumentSession session;
                session.ReplaceDocument(std::move(fixture).Seal(), kTargetId);
                session.SelectLayerTarget(kTargetId, true);
                const OwnerState opening(session);
                const auto before = session.Document().value();
                auto expected = DocumentFixture::Copy(before);
                expected.layers[1].transform.rotation = -10;
                expected.layers[1].transform.flipX = kind == LayerMetadataKind::FlipHorizontal ? !flipX : flipX;
                expected.layers[1].transform.flipY = kind == LayerMetadataKind::FlipVertical ? !flipY : flipY;
                const auto expectedDocument = std::move(expected).Seal();
                auto prepared = Prepare(session, kind, kTargetId);
                Expect(prepared && prepared->ChangesDocument() && SameOwnerState(session, opening),
                       (label + "-preparation-preserves-complete-opening-state").c_str());
                const bool applied = prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value());
                Expect(applied && session.Document().value() == expectedDocument,
                       (label + "-literal-origin-size-angle-flags-and-sampling").c_str());
                if (!applied)
                    continue;
                const auto after = session.Document().value();
                const auto& layer = after.layers[1];
                const auto& oldLayer = before.layers[1];
                Expect(layer.asset && oldLayer.asset && layer.mask && oldLayer.mask &&
                           layer.asset->image.Identity() == oldLayer.asset->image.Identity() &&
                           layer.asset->thumbnail.Identity() == oldLayer.asset->thumbnail.Identity() &&
                           layer.mask->asset.image.Identity() == oldLayer.mask->asset.image.Identity() &&
                           layer.mask->asset.thumbnail.Identity() == oldLayer.mask->asset.thumbnail.Identity() &&
                           after.selection && before.selection &&
                           after.selection->loops.Identity() == before.selection->loops.Identity() &&
                           after.layers[0] == before.layers[0] && after.layers[2] == before.layers[2] &&
                           session.ActiveLayerID() == opening.active &&
                           session.IsMaskSelected() == opening.maskSelected &&
                           session.SelectedLayerIDs() == opening.selected &&
                           session.CollapsedGroupIDs() == opening.collapsed,
                       (label + "-retains-pixel-mask-selection-identities-target-and-siblings").c_str());
                Expect(session.Generation() == opening.generation + 1 && session.history.UndoCount() == 1 &&
                           session.history.UndoName() == FlipHistoryName(kind) &&
                           session.history.CurrentRevision() != opening.revision &&
                           session.history.RetainedBytes(session.Document()) == 0,
                       (label + "-one-named-entry-with-no-new-history-only-pixel-storage").c_str());
                const OwnerState published(session);
                Expect(!session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                           !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                           SameOwnerState(session, published),
                       (label + "-published-output-is-single-use").c_str());
                session.Undo();
                Expect(session.Document().value() == before && session.history.UndoCount() == 0 &&
                           session.history.CurrentRevision() == opening.revision &&
                           session.history.RedoName() == FlipHistoryName(kind),
                       (label + "-undo-restores-every-opening-field").c_str());
                session.Redo();
                Expect(session.Document().value() == after && session.history.UndoCount() == 1 &&
                           session.history.UndoName() == FlipHistoryName(kind),
                       (label + "-redo-restores-every-final-field").c_str());
                auto second = Prepare(session, kind, kTargetId);
                Expect(second && second->ChangesDocument() &&
                           session.ApplyPreparedLayerMetadataIfCurrent(second.value()) &&
                           session.Document().value() == before && session.history.UndoCount() == 2 &&
                           session.history.UndoName() == FlipHistoryName(kind),
                       (label + "-two-flips-restore-placement-as-two-real-edits").c_str());
            }
        }
    }
}

void FlipMaskPlacementLiterals()
{
    enum class MaskCase
    {
        None,
        LinkedImplicit,
        UnlinkedImplicit,
        LinkedPlaced,
        UnlinkedPlaced,
        Uniform
    };
    constexpr std::array cases{std::pair{MaskCase::None, "no-mask"},
                               std::pair{MaskCase::LinkedImplicit, "linked-implicit"},
                               std::pair{MaskCase::UnlinkedImplicit, "unlinked-implicit"},
                               std::pair{MaskCase::LinkedPlaced, "linked-placed"},
                               std::pair{MaskCase::UnlinkedPlaced, "unlinked-placed"},
                               std::pair{MaskCase::Uniform, "uniform"}};
    // LayerMask.swift:54-58: linked placement follows, unlinked placement stays in document space,
    // and a uniform mask has no placement. Following() may choose an equivalent rotation/flip representation.
    for (const auto kind : kFlipKinds)
    {
        for (const auto& [test, name] : cases)
        {
            const std::string label = std::string(FlipName(kind)) + "-mask-" + name;
            auto fixture = DocumentFixture::Copy(FixtureDocument());
            fixture.width = 400;
            fixture.height = 200;
            auto& target = fixture.layers[1];
            target.transform.origin = Vec2{20, 30};
            target.transform.size = Vec2{100, 50};
            target.transform.rotation = 0;
            target.transform.sampling = LayerSampling::Nearest;
            LayerTransform placement = target.transform;
            placement.origin = Vec2{30, 35};
            placement.size = Vec2{20, 10};
            if (test == MaskCase::None)
                target.mask.reset();
            else
            {
                target.mask->linked = test != MaskCase::UnlinkedImplicit && test != MaskCase::UnlinkedPlaced;
                if (test == MaskCase::LinkedPlaced || test == MaskCase::UnlinkedPlaced || test == MaskCase::Uniform)
                    target.mask->placement = placement;
                if (test == MaskCase::Uniform)
                {
                    constexpr std::array<std::uint8_t, 1> value{127};
                    const Raster uniform = Raster::Make(1, 1, RasterFormat::Gray8, value.data(), value.size());
                    target.mask->asset.image = uniform;
                    target.mask->asset.thumbnail = uniform;
                }
            }
            DocumentSession session;
            session.ReplaceDocument(std::move(fixture).Seal(), kTargetId);
            const auto before = session.Document().value();
            auto prepared = Prepare(session, kind, kTargetId);
            const bool applied = prepared && prepared->ChangesDocument() &&
                                 session.ApplyPreparedLayerMetadataIfCurrent(prepared.value());
            LayerTransform expectedTransform = before.layers[1].transform;
            expectedTransform.flipX = kind == LayerMetadataKind::FlipHorizontal;
            expectedTransform.flipY = kind == LayerMetadataKind::FlipVertical;
            Expect(applied && session.Document()->layers[1].transform == expectedTransform,
                   (label + "-applies").c_str());
            if (!applied)
                continue;
            const auto after = session.Document().value();
            const auto& mask = after.layers[1].mask;
            bool literal = false;
            switch (test)
            {
            case MaskCase::None:
                literal = !mask;
                break;
            case MaskCase::LinkedImplicit:
            case MaskCase::Uniform:
                literal = mask && !mask->placement;
                break;
            case MaskCase::UnlinkedImplicit:
                literal = mask && mask->placement == std::optional(before.layers[1].transform);
                break;
            case MaskCase::UnlinkedPlaced:
                literal = mask && mask->placement == std::optional(placement);
                break;
            case MaskCase::LinkedPlaced:
            {
                // Independent world-space reflection: x -> 140-x, or y -> 110-y.
                // Compare unit-square point maps, not a preferred rotation/flip decomposition.
                constexpr std::array<Vec2, 4> units{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};
                constexpr std::array<Vec2, 4> horizontal{Vec2{110, 35}, Vec2{90, 35}, Vec2{90, 45}, Vec2{110, 45}};
                constexpr std::array<Vec2, 4> vertical{Vec2{30, 75}, Vec2{50, 75}, Vec2{50, 65}, Vec2{30, 65}};
                const auto& expected = kind == LayerMetadataKind::FlipHorizontal ? horizontal : vertical;
                literal = mask && mask->placement.has_value();
                if (literal)
                {
                    const auto map = mask->placement->UnitToDocument();
                    for (std::size_t index = 0; index < units.size(); ++index)
                    {
                        const auto point = map.Apply(units[index]);
                        literal = literal && std::abs(point.x - expected[index].x) < 0.0001 &&
                                  std::abs(point.y - expected[index].y) < 0.0001;
                    }
                }
                break;
            }
            }
            Expect(literal, (label + "-independent-placement-expectation").c_str());
            ImageLayer unedited = after.layers[1];
            unedited.transform = before.layers[1].transform;
            if (unedited.mask && before.layers[1].mask)
                unedited.mask->placement = before.layers[1].mask->placement;
            Expect(
                unedited == before.layers[1] && after.layers[0] == before.layers[0] &&
                    after.layers[2] == before.layers[2] && after.selection == before.selection &&
                    (!mask || (mask->asset.image.Identity() == before.layers[1].mask->asset.image.Identity() &&
                               mask->asset.thumbnail.Identity() == before.layers[1].mask->asset.thumbnail.Identity())),
                (label + "-changes-only-placement-and-retains-mask-pixels").c_str());
            session.Undo();
            Expect(session.Document().value() == before, (label + "-undo-restores-complete-mask-state").c_str());
            session.Redo();
            Expect(session.Document().value() == after, (label + "-redo-restores-complete-mask-state").c_str());
        }
    }
}

void FlipRefusalsAndNoOps()
{
    for (const auto kind : kFlipKinds)
    {
        const std::string label = FlipName(kind);
        DocumentSession empty;
        const OwnerState emptyBefore(empty);
        Expect(!empty.PrepareLayerMetadataInput(kind, kTargetId) && empty.LayerMetadataMemoryBytes(kind) == 0 &&
                   SameOwnerState(empty, emptyBefore),
               (label + "-absent-document-refuses-without-mutation").c_str());
        DocumentSession session = MakeSession();
        const OwnerState before(session);
        Expect(session.LayerMetadataMemoryBytes(kind) > 0 &&
                   !session.PreparePreservedLayerMetadataInput(kind, kTargetId) &&
                   session.PreservedLayerMetadataMemoryBytes(kind) == 0 && SameOwnerState(session, before),
               (label + "-settled-only-admission-rejects-preserved-entrypoint").c_str());
        Expect(!session.PrepareLayerMetadataInput(kind, kSiblingId) &&
                   !session.PrepareLayerMetadataInput(kind, Id("71000005-0000-4000-8000-000000000005")) &&
                   SameOwnerState(session, before),
               (label + "-inactive-and-missing-targets-cannot-retarget").c_str());
        session.SelectLayerTarget(kGroupId, false);
        const OwnerState groupBefore(session);
        Expect(!session.PrepareLayerMetadataInput(kind, kGroupId) && session.LayerMetadataMemoryBytes(kind) == 0 &&
                   SameOwnerState(session, groupBefore),
               (label + "-group-is-outside-single-layer-capture").c_str());
        for (const auto& [mode, name] :
             std::array{std::pair{0, "hidden-layer"}, std::pair{1, "hidden-ancestor"}, std::pair{2, "non-raster"}})
        {
            auto fixture = DocumentFixture::Copy(FixtureDocument());
            if (mode == 0)
                fixture.layers[1].visible = false;
            else if (mode == 1)
                fixture.layers[0].visible = false;
            else
                fixture.layers[1].asset.reset();
            DocumentSession ignored;
            ignored.ReplaceDocument(std::move(fixture).Seal(), kTargetId);
            ignored.RenameLayer(kTargetId, L"Seed redo");
            ignored.Undo();
            const OwnerState unchanged(ignored);
            auto prepared = Prepare(ignored, kind, kTargetId);
            Expect(unchanged.redoCount == 1 && prepared && !prepared->ChangesDocument() &&
                       ignored.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                       SameOwnerState(ignored, unchanged),
                   (label + "-" + name + "-no-op-preserves-redo-revision-and-generation").c_str());
            Expect(prepared && !ignored.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                       SameOwnerState(ignored, unchanged),
                   (label + "-" + name + "-no-op-output-is-single-use").c_str());
        }
        DocumentSession nested = MakeSession();
        nested.BeginEdit(L"Outer flip guard");
        const OwnerState nestedBefore(nested);
        Expect(!nested.PrepareLayerMetadataInput(kind, kTargetId) && nested.LayerMetadataMemoryBytes(kind) == 0 &&
                   SameOwnerState(nested, nestedBefore) && nested.history.CaptureState().Depth() == 1,
               (label + "-nested-transaction-refuses-with-depth-intact").c_str());
        nested.EndEdit();
        DocumentSession transform = MakeSession();
        transform.BeginTransform();
        const OwnerState transformBefore(transform);
        Expect(transform.PendingTransformEdit() && !transform.PrepareLayerMetadataInput(kind, kTargetId) &&
                   transform.LayerMetadataMemoryBytes(kind) == 0 && SameOwnerState(transform, transformBefore),
               (label + "-open-transform-refuses-with-draft-intact").c_str());
        transform.CancelTransform();
        DocumentSession panel = MakeSession();
        panel.BeginColorEdit(FilterKind::Levels);
        const OwnerState panelBefore(panel);
        Expect(panel.HasColorEdit() && !panel.PrepareLayerMetadataInput(kind, kTargetId) &&
                   panel.LayerMetadataMemoryBytes(kind) == 0 && SameOwnerState(panel, panelBefore),
               (label + "-open-panel-refuses-with-panel-intact").c_str());
        panel.CancelColorEdit();
        CancelSource cancelled;
        (void)cancelled.Cancel();
        DocumentSession cancelledSession = MakeSession();
        const OwnerState cancelledBefore(cancelledSession);
        auto input = cancelledSession.PrepareLayerMetadataInput(kind, kTargetId);
        auto noResult =
            input ? PrepareLayerMetadataPublication(std::move(input.value()), cancelled.Token()) : std::nullopt;
        Expect(input && !noResult && SameOwnerState(cancelledSession, cancelledBefore),
               (label + "-pre-cancelled-worker-preserves-owner").c_str());
    }
}

void FlipStaleAndMoveGuards()
{
    for (const auto kind : kFlipKinds)
    {
        for (const auto& [mode, name] : std::array{std::pair{0, "active-target"}, std::pair{1, "mask-target"},
                                                   std::pair{2, "replacement-document"}, std::pair{3, "history-epoch"},
                                                   std::pair{4, "generation"}, std::pair{5, "open-brush"}})
        {
            DocumentSession session = MakeSession();
            auto prepared = Prepare(session, kind, kTargetId);
            bool eventReached = true;
            switch (mode)
            {
            case 0:
                session.SelectLayerTarget(kSiblingId, false);
                break;
            case 1:
                session.SelectLayerTarget(kTargetId, true);
                break;
            case 2:
                session.CreateDocument(4, 3, false);
                break;
            case 3:
                session.history.Begin(L"Flip epoch witness", session.Document(), session.ActiveLayerID());
                session.history.End(session.Document(), session.ActiveLayerID());
                break;
            case 4:
                session.RenameLayer(kTargetId, L"Younger metadata");
                break;
            case 5:
                eventReached = session.BeginBrush(Vec2{0.5, 0.5});
                break;
            }
            const OwnerState intervening(session);
            Expect(eventReached && prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                       !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                       SameOwnerState(session, intervening),
                   (std::string(FlipName(kind)) + "-stale-" + name + "-cannot-overwrite-current-owner").c_str());
            if (mode == 5)
                session.CancelBrush();
        }
        DocumentSession session = MakeSession();
        DocumentSession other = MakeSession();
        const OwnerState otherBefore(other);
        auto input = session.PrepareLayerMetadataInput(kind, kTargetId);
        Expect(input.has_value(), (std::string(FlipName(kind)) + "-move-fixture-captures-input").c_str());
        if (!input)
            continue;
        auto moved = std::move(input.value());
        auto fromConsumed = PrepareLayerMetadataPublication(std::move(input.value()));
        auto prepared = PrepareLayerMetadataPublication(std::move(moved));
        Expect(!fromConsumed && prepared && !other.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   SameOwnerState(other, otherBefore),
               (std::string(FlipName(kind)) + "-consumed-input-and-cross-session-apply-refused").c_str());
        if (!prepared)
            continue;
        auto output = std::move(prepared.value());
        const OwnerState opening(session);
        Expect(!session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) && SameOwnerState(session, opening) &&
                   session.ApplyPreparedLayerMetadataIfCurrent(output),
               (std::string(FlipName(kind)) + "-moved-from-output-refused-and-sole-owner-applies").c_str());
    }
}

void MetadataValuesAndLiteralHistory()
{
    {
        DocumentSession session = MakeSession();
        const auto before = session.Document().value();
        const auto generation = session.Generation();
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"  Portrait  ");
        Expect(prepared && prepared->ChangesDocument() && session.Document().value() == before,
               "rename-preparation-leaves-live-document-unchanged");
        Expect(prepared && session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "rename-current-publication-applies-once");
        const auto& after = session.Document().value();
        ImageLayer expectedTarget = before.layers[1];
        expectedTarget.name = L"Portrait";
        Expect(after.layers.size() == 3 && after.layers[1].name == L"Portrait" && after.layers[1].asset.has_value() &&
                   after.layers[1].mask.has_value() && after.layers[1].asset->name == L"Original pixels" &&
                   after.layers[1].mask->asset.name == L"Original mask" && after.layers[0] == before.layers[0] &&
                   after.layers[1] == expectedTarget && after.layers[2] == before.layers[2] &&
                   after.layers[1].asset->image.Identity() == before.layers[1].asset->image.Identity() &&
                   after.layers[1].mask->asset.image.Identity() == before.layers[1].mask->asset.image.Identity() &&
                   after.layers[1].shapeImage.Identity() == before.layers[1].shapeImage.Identity() &&
                   after.selection == before.selection &&
                   after.selection->loops.Identity() == before.selection->loops.Identity(),
               "rename-changes-only-requested-layer-name-and-retains-pixels-mask-selection-and-other-records");
        Expect(session.Generation() == generation + 1 && session.history.UndoCount() == 1 &&
                   session.history.UndoName() == L"Rename Layer",
               "rename-advances-generation-and-records-one-literal-history-entry");
        session.Undo();
        Expect(session.Document().value() == before && session.history.UndoCount() == 0 &&
                   session.history.RedoName() == L"Rename Layer",
               "rename-undo-restores-literal-old-fields-and-all-identities");
        session.Redo();
        Expect(session.Document()->layers[1].name == L"Portrait" && session.history.UndoName() == L"Rename Layer",
               "rename-redo-restores-literal-new-fields");
        Expect(!session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "rename-publication-is-single-use-after-apply");
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = Prepare(session, LayerMetadataKind::ToggleVisibility, kTargetId);
        Expect(prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   !session.Document()->layers[1].visible && session.history.UndoName() == L"Hide Layer",
               "toggle-visibility-flips-captured-value-with-literal-hide-title");
        session.Undo();
        Expect(session.Document()->layers[1].visible, "toggle-visibility-undo-restores-old-visibility");
        session.Redo();
        Expect(!session.Document()->layers[1].visible && session.history.UndoName() == L"Hide Layer",
               "toggle-visibility-redo-restores-new-visibility");
    }
    for (const auto [input, expected] : std::array<std::pair<double, double>, 2>{{{-0.5, 0.0}, {1.5, 1.0}}})
    {
        DocumentSession session = MakeSession();
        auto prepared = Prepare(session, LayerMetadataKind::SetOpacity, kTargetId, {}, input);
        const bool applied = prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value());
        const char* bound = input < 0.0 ? "opacity-lower-bound" : "opacity-upper-bound";
        Expect(applied && session.Document()->layers[1].opacity == expected &&
                   session.history.UndoName() == L"Layer Opacity",
               bound);
        if (applied && input < 0.0)
        {
            session.Undo();
            Expect(session.Document()->layers[1].opacity == 0.75 && session.history.RedoName() == L"Layer Opacity",
                   "opacity-undo-restores-literal-old-value");
            session.Redo();
            Expect(session.Document()->layers[1].opacity == 0.0 && session.history.UndoName() == L"Layer Opacity",
                   "opacity-redo-restores-literal-clamped-value");
        }
    }
    {
        DocumentSession session = MakeSession();
        auto prepared = Prepare(session, LayerMetadataKind::SetBlendMode, kTargetId, {}, 1.0, LayerBlendMode::Normal);
        Expect(prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document()->layers[1].blendMode == LayerBlendMode::Normal &&
                   session.history.UndoName() == L"Layer Blend Mode",
               "blend-mode-sets-normal-and-records-one-literal-entry");
        session.Undo();
        Expect(session.Document()->layers[1].blendMode == LayerBlendMode::Multiply &&
                   session.history.RedoName() == L"Layer Blend Mode",
               "blend-mode-undo-restores-literal-old-value");
        session.Redo();
        Expect(session.Document()->layers[1].blendMode == LayerBlendMode::Normal &&
                   session.history.UndoName() == L"Layer Blend Mode",
               "blend-mode-redo-restores-literal-new-value");
    }
}

void NoOpsAndGroups()
{
    DocumentSession session = MakeSession();
    auto seed = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Seed");
    Expect(seed && session.ApplyPreparedLayerMetadataIfCurrent(seed.value()), "no-op-fixture-seeds-history");
    session.Undo();
    const OwnerState before(session);
    for (const auto& [kind, target, name, label] :
         std::array<std::tuple<LayerMetadataKind, Guid, std::wstring_view, const char*>, 3>{
             {{LayerMetadataKind::Rename, kTargetId, L"Original", "rename-same-name"},
              {LayerMetadataKind::Rename, kTargetId, L"   \t  ", "rename-blank-name"},
              {LayerMetadataKind::Rename, Id("71000005-0000-4000-8000-000000000005"), L"Missing",
               "rename-missing-target"}}})
    {
        auto prepared = Prepare(session, kind, target, name);
        Expect(prepared && !prepared->ChangesDocument() &&
                   session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) && SameOwnerState(session, before),
               label);
    }

    {
        DocumentSession groupSession = MakeSession();
        groupSession.SelectLayerTarget(kGroupId, false);
        const OwnerState groupBefore(groupSession);
        auto opacity = Prepare(groupSession, LayerMetadataKind::SetOpacity, kGroupId, {}, 0.2);
        auto blend =
            Prepare(groupSession, LayerMetadataKind::SetBlendMode, kGroupId, {}, 1.0, LayerBlendMode::Multiply);
        Expect(opacity && blend && !opacity->ChangesDocument() && !blend->ChangesDocument() &&
                   groupSession.ApplyPreparedLayerMetadataIfCurrent(opacity.value()) &&
                   groupSession.ApplyPreparedLayerMetadataIfCurrent(blend.value()) &&
                   SameOwnerState(groupSession, groupBefore),
               "group-opacity-and-blend-are-ignored-without-history-or-generation-change");
    }
    {
        DocumentSession groupSession = MakeSession();
        auto rename = Prepare(groupSession, LayerMetadataKind::Rename, kGroupId, L"New folder");
        Expect(rename && groupSession.ApplyPreparedLayerMetadataIfCurrent(rename.value()) &&
                   groupSession.Document()->layers[0].name == L"New folder" &&
                   groupSession.Document()->layers[0].isGroup && groupSession.history.UndoName() == L"Rename Layer",
               "group-can-be-renamed");
        groupSession.Undo();
        Expect(groupSession.Document()->layers[0].name == L"Folder", "group-rename-undo-restores-original-name");
    }
    {
        DocumentSession groupSession = MakeSession();
        auto toggle = Prepare(groupSession, LayerMetadataKind::ToggleVisibility, kGroupId);
        Expect(toggle && groupSession.ApplyPreparedLayerMetadataIfCurrent(toggle.value()) &&
                   !groupSession.Document()->layers[0].visible && groupSession.history.UndoName() == L"Hide Layer",
               "group-can-be-toggled-with-literal-history-title");
    }
}

void InputValidationAndConsumption()
{
    {
        DocumentSession session = MakeSession();
        const OwnerState before(session);
        Expect(!session.PrepareLayerMetadataInput(static_cast<LayerMetadataKind>(255), kTargetId),
               "invalid-kind-is-not-admitted");
        Expect(!session.PrepareLayerMetadataInput(LayerMetadataKind::SetOpacity, kTargetId, {},
                                                  std::numeric_limits<double>::infinity()),
               "nonfinite-opacity-is-not-admitted");
        Expect(!session.PrepareLayerMetadataInput(LayerMetadataKind::SetBlendMode, kTargetId, {}, 1.0,
                                                  static_cast<LayerBlendMode>(255)),
               "invalid-blend-mode-is-not-admitted");
        const std::wstring oversized(static_cast<std::size_t>(kMaxNameUtf8Bytes) + 1, L'x');
        auto tooLong = Prepare(session, LayerMetadataKind::Rename, kTargetId, oversized);
        Expect(!tooLong && SameOwnerState(session, before), "oversized-name-fails-preparation-with-owner-unchanged");
    }
    {
        DocumentSession session = MakeSession();
        auto source = session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"Moved");
        LayerMetadataInput moved(std::move(source.value()));
        auto fromOriginal = PrepareLayerMetadataPublication(std::move(source.value()));
        auto prepared = PrepareLayerMetadataPublication(std::move(moved));
        auto fromConsumedInput = PrepareLayerMetadataPublication(std::move(moved));
        Expect(prepared && !fromOriginal && !fromConsumedInput,
               "moved-from-input-is-consumed-before-and-during-preparation");
        auto movedPrepared = std::move(prepared.value());
        Expect(!session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   session.ApplyPreparedLayerMetadataIfCurrent(movedPrepared),
               "moved-from-prepared-is-invalid-and-moved-to-value-applies-once");
    }
    {
        DocumentSession session = MakeSession();
        auto source = session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"Original");
        LayerMetadataInput moved(std::move(source.value()));
        auto fromOriginal = PrepareLayerMetadataPublication(std::move(source.value()));
        auto noOp = PrepareLayerMetadataPublication(std::move(moved));
        auto consumedNoOp = PrepareLayerMetadataPublication(std::move(moved));
        Expect(noOp && !noOp->ChangesDocument() && !fromOriginal && !consumedNoOp &&
                   session.ApplyPreparedLayerMetadataIfCurrent(noOp.value()),
               "moved-from-no-op-input-is-consumed-and-owner-acknowledges-once");
        auto movedNoOp = std::move(noOp.value());
        Expect(!session.IsPreparedLayerMetadataCurrent(noOp.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(noOp.value()) &&
                   session.Document()->layers[1].name == L"Original" &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(movedNoOp),
               "moved-from-no-op-prepared-and-repeated-apply-are-rejected");
    }
}

void StalePublicationGuards()
{
    {
        auto firstOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& first = *firstOwner;
        auto secondOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& second = *secondOwner;
        auto prepared = Prepare(first, LayerMetadataKind::Rename, kTargetId, L"Cross-session");
        Expect(prepared && !second.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !second.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "publication-cannot-cross-session");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old document");
        session.ReplaceDocument(FixtureDocument(), kTargetId);
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "replacement-document-invalidates-publication");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old active");
        session.SelectLayerTarget(kSiblingId, false);
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "active-target-change-invalidates-publication");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        session.SelectLayerTarget(kSiblingId, false);
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Inactive rename");
        Expect(prepared && session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()) &&
                   session.Document()->layers[1].name == L"Inactive rename" &&
                   session.ActiveLayerID() == std::optional<Guid>{kSiblingId},
               "rename-addresses-saved-target-when-another-layer-is-active");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old selection");
        session.SetSelection(std::nullopt, L"Clear Selection");
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "selection-change-invalidates-publication");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old sibling");
        EditLayerRecordsForTest(session,
                                [](DocumentFixture& document) { document.layers[2].name = L"Changed sibling"; });
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "other-layer-record-change-invalidates-publication");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old history");
        session.RenameLayer(kTargetId, L"Intervening edit");
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "history-and-generation-change-invalidates-publication");
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Old gesture");
        Expect(session.BeginBrush(Vec2{0.5, 0.5}), "stale-gesture-fixture-opens-brush");
        Expect(prepared && !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "open-brush-gesture-invalidates-publication");
        session.CancelBrush();
    }
    {
        auto sessionOwner = std::make_unique<DocumentSession>(MakeSession());
        auto& session = *sessionOwner;
        auto prepared = Prepare(session, LayerMetadataKind::Rename, kTargetId, L"Limit stale");
        for (int index = 0; index <= kHistoryEntryLimit; ++index)
            session.RenameLayer(kTargetId, (index & 1) == 0 ? L"Limit A" : L"Limit B");
        Expect(session.history.UndoCount() == kHistoryEntryLimit && prepared &&
                   !session.IsPreparedLayerMetadataCurrent(prepared.value()) &&
                   !session.ApplyPreparedLayerMetadataIfCurrent(prepared.value()),
               "history-entry-limit-trim-invalidates-older-publication");
    }
}

struct Inject final
{
    LayerMetadataPrepareStage metadataStage = LayerMetadataPrepareStage::BeforeDraft;
    DocumentHistory::PrepareStage historyStage = DocumentHistory::PrepareStage::BeforeSnapshot;
    CancelSource* cancellation = nullptr;
    bool throwBadAlloc = false;
    bool visited = false;
};

void MetadataCheckpoint(void* raw, LayerMetadataPrepareStage stage)
{
    auto& inject = *static_cast<Inject*>(raw);
    if (stage != inject.metadataStage)
        return;
    inject.visited = true;
    if (inject.throwBadAlloc)
        throw std::bad_alloc{};
    (void)inject.cancellation->Cancel();
}

void HistoryCheckpoint(void* raw, DocumentHistory::PrepareStage stage)
{
    auto& inject = *static_cast<Inject*>(raw);
    if (stage != inject.historyStage)
        return;
    inject.visited = true;
    if (inject.throwBadAlloc)
        throw std::bad_alloc{};
    (void)inject.cancellation->Cancel();
}

const char* MetadataStageName(LayerMetadataPrepareStage stage) noexcept
{
    switch (stage)
    {
    case LayerMetadataPrepareStage::BeforeDraft:
        return "before-draft";
    case LayerMetadataPrepareStage::AfterEdit:
        return "after-edit";
    case LayerMetadataPrepareStage::AfterSeal:
        return "after-seal";
    }
    return "unknown";
}

const char* HistoryStageName(DocumentHistory::PrepareStage stage) noexcept
{
    switch (stage)
    {
    case DocumentHistory::PrepareStage::BeforeSnapshot:
        return "before-snapshot";
    case DocumentHistory::PrepareStage::AfterSnapshot:
        return "after-snapshot";
    case DocumentHistory::PrepareStage::Entry:
        return "entry";
    case DocumentHistory::PrepareStage::StackGrowth:
        return "stack-growth";
    case DocumentHistory::PrepareStage::TrimIndex:
        return "trim-index";
    default:
        return "unexpected-stage";
    }
}

void MetadataPreparationFailures(LayerMetadataKind kind = LayerMetadataKind::Rename)
{
    for (const auto stage : {LayerMetadataPrepareStage::BeforeDraft, LayerMetadataPrepareStage::AfterEdit,
                             LayerMetadataPrepareStage::AfterSeal})
    {
        for (const bool throwBadAlloc : {false, true})
        {
            DocumentSession session = MakeSession();
            const OwnerState before(session);
            CancelSource cancellation;
            Inject inject{stage, DocumentHistory::PrepareStage::BeforeSnapshot, &cancellation, throwBadAlloc};
            const LayerMetadataPrepareObserver observer{&MetadataCheckpoint, &inject};
            bool caught = false;
            bool returnedNull = false;
            try
            {
                auto prepared = Prepare(session, kind, kTargetId, L"Injected", 1.0, LayerBlendMode::Normal,
                                        cancellation.Token(), {}, observer);
                returnedNull = !prepared;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            const std::string stageName = MetadataStageName(stage);
            const std::string modeName = throwBadAlloc ? "bad-alloc" : "cancel";
            const std::string prefix = IsLayerFlipMetadata(kind) ? std::string(FlipName(kind)) + "-" : "";
            const std::string label = prefix + "metadata-" + stageName + "-" + modeName;
            Expect(inject.visited, (label + "-checkpoint-reached").c_str());
            Expect(throwBadAlloc ? caught : returnedNull, (label + "-propagates-or-returns-no-result").c_str());
            Expect(caught == throwBadAlloc && returnedNull == !throwBadAlloc && SameOwnerState(session, before),
                   (label + "-preserves-complete-owner-state").c_str());
        }
    }
}

void HistoryPreparationFailures(LayerMetadataKind kind = LayerMetadataKind::Rename)
{
    constexpr std::array<DocumentHistory::PrepareStage, 5> stages{
        DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
        DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
        DocumentHistory::PrepareStage::TrimIndex};
    for (const auto stage : stages)
    {
        for (const bool throwBadAlloc : {false, true})
        {
            DocumentSession session = MakeSession();
            const OwnerState before(session);
            CancelSource cancellation;
            Inject inject{LayerMetadataPrepareStage::BeforeDraft, stage, &cancellation, throwBadAlloc};
            const DocumentHistory::PrepareObserver observer{&HistoryCheckpoint, &inject};
            bool caught = false;
            bool returnedNull = false;
            try
            {
                auto prepared = Prepare(session, kind, kTargetId, L"Injected", 1.0, LayerBlendMode::Normal,
                                        cancellation.Token(), observer);
                returnedNull = !prepared;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            const std::string stageName = HistoryStageName(stage);
            const std::string modeName = throwBadAlloc ? "bad-alloc" : "cancel";
            const std::string prefix = IsLayerFlipMetadata(kind) ? std::string(FlipName(kind)) + "-" : "";
            const std::string label = prefix + "history-" + stageName + "-" + modeName;
            Expect(inject.visited, (label + "-checkpoint-reached").c_str());
            Expect(throwBadAlloc ? caught : returnedNull, (label + "-propagates-or-returns-no-result").c_str());
            Expect(caught == throwBadAlloc && returnedNull == !throwBadAlloc && SameOwnerState(session, before),
                   (label + "-preserves-document-entry-stack-and-generation").c_str());
        }
    }
}

void AdmissionAndMemory()
{
    DocumentSession session = MakeSession();
    const OwnerState before(session);
    CancelSource alreadyCancelled;
    (void)alreadyCancelled.Cancel();
    auto input = session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"Pre-cancelled");
    auto cancelled =
        input ? PrepareLayerMetadataPublication(std::move(input.value()), alreadyCancelled.Token()) : std::nullopt;
    Expect(!cancelled && SameOwnerState(session, before),
           "pre-cancelled-publication-returns-no-result-and-preserves-owner");
    const auto bytes = session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 12);
    Expect(bytes > 0 && bytes > session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 0),
           "metadata-memory-admission-reserves-nonzero-and-name-storage");
    Expect(session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, (std::numeric_limits<std::size_t>::max)()) == 0,
           "metadata-memory-name-size-overflow-refuses-admission");
    Expect(session.LayerMetadataMemoryBytes(static_cast<LayerMetadataKind>(255), 0) == 0,
           "invalid-kind-has-no-memory-admission");
    session.BeginEdit(L"Literal metadata transaction");
    Expect(session.history.CaptureState().Depth() != 0 &&
               session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 1) == 0 &&
               !session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"During transform"),
           "open-history-transaction-has-no-metadata-memory-or-input-admission");
    session.EndEdit();
    session.BeginTransform();
    Expect(session.PendingTransformEdit() && session.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 1) == 0 &&
               !session.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"During transform"),
           "open-transform-has-no-metadata-memory-or-input-admission");

    DocumentSession panelSession = MakeSession();
    panelSession.BeginColorEdit(FilterKind::Levels);
    Expect(panelSession.PendingColorEdit().has_value() &&
               panelSession.LayerMetadataMemoryBytes(LayerMetadataKind::Rename, 1) == 0 &&
               !panelSession.PrepareLayerMetadataInput(LayerMetadataKind::Rename, kTargetId, L"During panel"),
           "open-color-panel-has-no-metadata-memory-or-input-admission");
    panelSession.CancelColorEdit();
}
} // namespace

int RunLayerMetadataWorkTests()
{
    gFailures = 0;
    MetadataValuesAndLiteralHistory();
    NoOpsAndGroups();
    InputValidationAndConsumption();
    StalePublicationGuards();
    PreparedHistoryResidencyCallers();
    MetadataPreparationFailures();
    HistoryPreparationFailures();
    AdmissionAndMemory();
    FlipValuesAndLiteralHistory();
    FlipMaskPlacementLiterals();
    FlipRefusalsAndNoOps();
    FlipStaleAndMoveGuards();
    for (const auto kind : kFlipKinds)
    {
        MetadataPreparationFailures(kind);
        HistoryPreparationFailures(kind);
    }
    return gFailures;
}

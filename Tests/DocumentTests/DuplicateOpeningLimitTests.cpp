#include "DuplicateOpeningLimitTests.h"

#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "DuplicateOpeningWork.h"
#include "Executor.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stop_token>
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
    std::printf("%s duplicate-opening-limits.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

class SingleThreadExecutor final : public Executor
{
  public:
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 1;
    }

    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return {};
    }

    void RunParticipants(void* context, Participant run, unsigned int) noexcept override
    {
        run(context);
    }
};

constexpr std::array<std::uint8_t, 4> kImageBytes{31, 63, 127, 255};
constexpr std::array<std::uint8_t, 1> kMaskBytes{173};

struct Fixture final
{
    DocumentSession session;
    Raster image;
    Raster mask;
    Guid sourceID;

    explicit Fixture(std::size_t layerCount)
    {
        image = Raster::Make(1, 1, RasterFormat::Bgra8888, kImageBytes.data(), kImageBytes.size());
        mask = Raster::Make(1, 1, RasterFormat::Gray8, kMaskBytes.data(), kMaskBytes.size());

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 1;
        document.height = 1;
        document.layers.reserve(layerCount);

        ImageLayer source = ImageLayer::Blank(L"Sidecar source", Vec2{1, 1});
        source.asset = RasterAsset{image, image, L"Shared 1x1 image"};
        source.mask = LayerMask{RasterAsset{mask, mask, L"Shared 1x1 mask"}, true, std::nullopt, true};
        source.shape = LayerShapeStyle{ShapeKind::Ellipse, 0.25, 0.5, 0.75, 0.125};
        source.shapeImage = image;
        sourceID = source.id;
        document.layers.push_back(std::move(source));

        while (document.layers.size() < layerCount)
        {
            ImageLayer filler = ImageLayer::Blank(L"Sidecar filler", Vec2{1, 1});
            filler.asset = RasterAsset{image, image, L"Shared 1x1 image"};
            document.layers.push_back(std::move(filler));
        }

        session.ReplaceDocument(std::move(document).Seal(), sourceID);
        session.history.Reset();
        session.SelectLayerTarget(sourceID, false);
    }
};

struct OwnerSnapshot final
{
    CanvasDocument document;
    DocumentHistory::StateView history;
    Guid revision;
    std::uint64_t generation = 0;
    int undoCount = 0;
    int historyDepth = 0;
    std::size_t pastCount = 0;
    std::size_t futureCount = 0;
    std::optional<Guid> active;
    std::unordered_set<Guid> selected;
    bool maskSelected = false;

    explicit OwnerSnapshot(DocumentSession& session)
        : document(session.Document().value()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), generation(session.Generation()),
          undoCount(session.history.UndoCount()), historyDepth(history.Depth()), pastCount(history.PastCount()),
          futureCount(history.FutureCount()), active(session.ActiveLayerID()), selected(session.SelectedLayerIDs()),
          maskSelected(session.IsMaskSelected())
    {
    }
};

bool SameDocumentIncludingShapeImages(const CanvasDocument& current, const CanvasDocument& before) noexcept
{
    if (!(current == before) || current.layers.size() != before.layers.size())
        return false;
    for (std::size_t index = 0; index < current.layers.size(); ++index)
        if (current.layers[index].shapeImage.Identity() != before.layers[index].shapeImage.Identity())
            return false;
    return true;
}

bool SameOwnerState(DocumentSession& session, const OwnerSnapshot& before)
{
    const auto history = session.history.CaptureState();
    return session.Document() && SameDocumentIncludingShapeImages(session.Document().value(), before.document) &&
           session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
           session.Generation() == before.generation && history.Depth() == before.historyDepth &&
           history.PastCount() == before.pastCount && history.FutureCount() == before.futureCount &&
           session.history.UndoCount() == before.undoCount && session.ActiveLayerID() == before.active &&
           session.SelectedLayerIDs() == before.selected && session.IsMaskSelected() == before.maskSelected;
}

struct PreparationProbe final
{
    DocumentSession* session = nullptr;
    const OwnerSnapshot* before = nullptr;
    unsigned int checkpoints = 0;
    bool unchanged = true;
};

void CheckPreparationState(void* context, DuplicateOpeningStage)
{
    auto& probe = *static_cast<PreparationProbe*>(context);
    ++probe.checkpoints;
    probe.unchanged = probe.unchanged && SameOwnerState(*probe.session, *probe.before);
}

DuplicateOpeningResult Prepare(DocumentSession& session, Executor& executor, CancelToken cancellation = {},
                               DuplicateOpeningObserver observer = {})
{
    auto input = session.PrepareDuplicateOpeningInput();
    return input ? DocumentSession::PrepareDuplicateOpeningPublication(std::move(input.value()), executor, cancellation,
                                                                       {}, observer)
                 : DuplicateOpeningResult{};
}

const ImageLayer* FindLayer(const CanvasDocument& document, const Guid& id) noexcept
{
    for (const ImageLayer& layer : document.layers)
        if (layer.id == id)
            return &layer;
    return nullptr;
}

void ExactMaximumLayerOpening()
{
    Fixture fixture(static_cast<std::size_t>(kMaxLayerCount) - 1);
    DocumentSession& session = fixture.session;
    const OwnerSnapshot before(session);
    const ImageLayer sourceBefore = *FindLayer(before.document, fixture.sourceID);
    SingleThreadExecutor executor;
    PreparationProbe probe{&session, &before};
    auto prepared = Prepare(session, executor, {}, DuplicateOpeningObserver{&probe, &CheckPreparationState});

    Expect(prepared.status == DuplicateOpeningStatus::Ready && prepared.publication.has_value(),
           "9999-layer-source-prepares-opening");
    Expect(probe.checkpoints == 5 && probe.unchanged && SameOwnerState(session, before),
           "all-worker-preparation-checkpoints-preserve-source-and-owner-state");
    if (!prepared.publication)
        return;

    auto publication = std::move(prepared.publication.value());
    const auto restoration = publication.Restoration();
    Expect(restoration && session.ApplyPreparedDuplicateOpeningIfCurrent(publication),
           "prepared-opening-applies-at-layer-limit");
    if (!session.PendingTransformEdit())
        return;

    const CanvasDocument& opened = session.Document().value();
    const ImageLayer* original = FindLayer(opened, fixture.sourceID);
    const auto active = session.ActiveLayerID();
    const ImageLayer* duplicate = active ? FindLayer(opened, active.value()) : nullptr;
    Expect(opened.layers.size() == static_cast<std::size_t>(kMaxLayerCount) && original && duplicate &&
               opened.layers[1].id == duplicate->id,
           "duplicate-reaches-exactly-10000-layers-adjacent-to-source");
    Expect(original && *original == sourceBefore && original->shape == sourceBefore.shape &&
               original->shapeImage.Identity() == sourceBefore.shapeImage.Identity() && duplicate &&
               duplicate->shape == sourceBefore.shape &&
               duplicate->shapeImage.Identity() == sourceBefore.shapeImage.Identity() && duplicate->asset &&
               duplicate->asset->image.Identity() == fixture.image.Identity() && duplicate->mask &&
               duplicate->mask->asset.image.Identity() == fixture.mask.Identity(),
           "opening-preserves-shape-metadata-and-shares-one-pixel-storage");

    auto draft = session.PendingTransformEdit()->draft;
    draft.origin.x += 2;
    session.PreviewTransform(draft);
    Expect(restoration && session.ApplyPreparedDuplicateRestoreIfCurrent(*restoration),
           "prepared-restoration-applies-after-draft-change");
    const ImageLayer* restoredSource = FindLayer(session.Document().value(), fixture.sourceID);
    Expect(session.Document()->layers.size() == static_cast<std::size_t>(kMaxLayerCount - 1) && restoredSource &&
               *restoredSource == sourceBefore && restoredSource->shape == sourceBefore.shape &&
               restoredSource->shapeImage.Identity() == sourceBefore.shapeImage.Identity() &&
               session.ActiveLayerID() == before.active && session.SelectedLayerIDs() == before.selected &&
               session.Generation() == before.generation + 2 && session.history.UndoCount() == before.undoCount,
           "restoration-recovers-source-shape-and-owner-selection-history");
}

void LimitAndGroupRefusals()
{
    {
        Fixture fixture(static_cast<std::size_t>(kMaxLayerCount));
        DocumentSession& session = fixture.session;
        const OwnerSnapshot before(session);
        SingleThreadExecutor executor;
        const auto prepared = Prepare(session, executor);
        Expect(session.DuplicateOpeningPreparationMemoryBytes() == 0 &&
                   prepared.status != DuplicateOpeningStatus::Ready && !prepared.publication,
               "10000-layer-source-refuses-without-publication");
        Expect(SameOwnerState(session, before),
               "10000-layer-refusal-preserves-document-history-generation-selection-and-active-layer");
    }
    {
        Fixture fixture(1);
        auto document = DocumentFixture::Copy(fixture.session.Document().value());
        ImageLayer group = ImageLayer::Blank(L"Sidecar active group", Vec2{1, 1});
        group.isGroup = true;
        const Guid groupID = group.id;
        document.layers.front().parentID = groupID;
        document.layers.insert(document.layers.begin(), std::move(group));
        fixture.session.ReplaceDocument(std::move(document).Seal(), groupID);
        fixture.session.history.Reset();
        const OwnerSnapshot before(fixture.session);
        SingleThreadExecutor executor;
        const auto prepared = Prepare(fixture.session, executor);
        Expect(!fixture.session.PrepareDuplicateOpeningInput() && !prepared.publication &&
                   prepared.status != DuplicateOpeningStatus::Ready,
               "active-group-source-refuses-opening");
        Expect(SameOwnerState(fixture.session, before), "active-group-refusal-preserves-owner-state");
    }
}

void CancelledPreparationDoesNotPublish()
{
    Fixture fixture(2);
    DocumentSession& session = fixture.session;
    const OwnerSnapshot before(session);
    SingleThreadExecutor executor;
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    const auto prepared = Prepare(session, executor, cancellation.Token());
    Expect(prepared.status == DuplicateOpeningStatus::Cancelled && !prepared.publication,
           "cancelled-worker-preparation-has-no-publication");
    Expect(SameOwnerState(session, before), "cancelled-preparation-preserves-document-and-history-state");
}
} // namespace

int RunDuplicateOpeningLimitTests()
{
    gFailures = 0;
    gAssertions = 0;
    ExactMaximumLayerOpening();
    LimitAndGroupRefusals();
    CancelledPreparationDoesNotPublish();
    std::printf("DUPLICATE OPENING LIMIT SIDECAR checks=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

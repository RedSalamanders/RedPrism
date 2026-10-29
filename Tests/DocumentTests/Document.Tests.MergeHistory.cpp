#include "Document.Tests.MergeHistory.h"

#include "Document.h"
#include "Executor.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s merge-history.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Guid Id(const char* text)
{
    const auto parsed = Guid::Parse(text);
    return parsed.value_or(Guid{});
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer PixelLayer(const char* id, const wchar_t* name, const Raster& pixels, int canvasWidth, int canvasHeight)
{
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(canvasWidth), static_cast<double>(canvasHeight)});
    layer.id = Id(id);
    layer.transform.size = Vec2{static_cast<double>(pixels.Width()), static_cast<double>(pixels.Height())};
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), name};
    return layer;
}

CanvasDocument MergeDocument(bool grouped)
{
    constexpr std::array<std::uint8_t, 4> pixel{19, 31, 47, 255};
    const Raster raster = Bgra(1, 1, pixel);
    DocumentFixture document;
    document.id = Id("10000000-0000-4000-8000-000000000001");
    document.width = 3;
    document.height = 2;
    const Guid parent = Id("20000000-0000-4000-8000-000000000001");
    const Guid upper = Id("20000000-0000-4000-8000-000000000003");
    if (grouped)
    {
        ImageLayer group = ImageLayer::Blank(L"Parent", Vec2{3, 2});
        group.id = parent;
        group.isGroup = true;
        document.layers.push_back(std::move(group));
    }
    ImageLayer bottom = PixelLayer("20000000-0000-4000-8000-000000000002", L"Lower", raster, 3, 2);
    ImageLayer top = PixelLayer("20000000-0000-4000-8000-000000000003", L"Upper", raster, 3, 2);
    ImageLayer clipped = PixelLayer("20000000-0000-4000-8000-000000000004", L"Survivor", raster, 3, 2);
    constexpr std::array<std::uint8_t, 1> maskPixel{255};
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskPixel.data(), maskPixel.size());
    top.mask = LayerMask{RasterAsset{mask, mask, L"Mask"}, true, std::nullopt, true};
    if (grouped)
    {
        clipped.parentID = parent;
        bottom.parentID = parent;
        top.parentID = parent;
    }
    clipped.maskSourceID = upper;
    document.layers.push_back(std::move(bottom));
    document.layers.push_back(std::move(top));
    document.layers.push_back(std::move(clipped));
    DocumentSelection selection;
    selection.antialiased = false;
    selection.loops = SelectionLoops{{Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{0, 1}}};
    document.selection = std::move(selection);
    return std::move(document).Seal();
}

constexpr std::array<std::uint8_t, 24> kCompositePixels{0, 0, 0, 0, 11, 22, 33, 255, 44,  55,  66,  128,
                                                        0, 0, 0, 0, 17, 28, 39, 64,  111, 122, 133, 255};
constexpr std::array<std::uint8_t, 16> kTrimmedPixels{11, 22, 33, 255, 44,  55,  66,  128,
                                                      17, 28, 39, 64,  111, 122, 133, 255};

bool PixelsEqual(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(raster.Width()) * static_cast<std::size_t>(raster.Height()) * 4u)
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, raster.Width(), raster.Height(), actual.data(), raster.Width() * 4, 0);
    return std::equal(actual.begin(), actual.end(), expected.begin());
}

std::optional<DocumentSession::PreparedMergePublication> Prepare(DocumentSession& session,
                                                                 const DocumentSession::MergePlan& plan,
                                                                 const Raster& composite,
                                                                 DocumentSession::MergePublicationStatus& status,
                                                                 CancelToken cancellation = {},
                                                                 DocumentHistory::PrepareObserver observer = {})
{
    const auto token = session.CaptureJobToken(std::nullopt, 73);
    if (!token)
        return std::nullopt;
    return DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(), token.value(),
                                                    session.history.CaptureState(), plan, composite, status,
                                                    cancellation, observer);
}

void LiteralCompositeAndSingleHistoryEntry()
{
    DocumentSession session;
    const CanvasDocument source = MergeDocument(true);
    const Guid upper = Id("20000000-0000-4000-8000-000000000003");
    session.ReplaceDocument(source, upper);
    session.SelectLayerTarget(upper, true);
    const auto plan = session.PlanMerge();
    const Raster composite = Bgra(3, 2, kCompositePixels);
    const CanvasDocument before = session.Document().value();
    Expect(plan && plan->action == L"Merge Down" && plan->name == L"Lower" &&
               plan->parent == std::optional<Guid>{Id("20000000-0000-4000-8000-000000000001")} && plan->anchor == upper,
           "literal.plan-stacking-anchor-parent-name");
    Expect(composite.HasStorage() && ValidateDocument(source) == DocumentStatus::Ok,
           "literal.fixture-and-document-valid");
    if (!plan || !composite.HasStorage())
        return;

    const auto token = session.CaptureJobToken(std::nullopt, 73);
    const auto state = session.history.CaptureState();
    const auto active = session.ActiveLayerID();
    const auto generation = session.Generation();
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    auto prepared = token ? DocumentSession::PrepareMergePublication(session.Document(), active, token.value(), state,
                                                                     plan.value(), composite, status)
                          : std::nullopt;
    Expect(prepared && status == DocumentSession::MergePublicationStatus::Ready,
           "literal.worker-prepares-ready-publication");
    Expect(session.Document() == std::optional<CanvasDocument>{before} && session.ActiveLayerID() == active &&
               session.Generation() == generation && session.history.IsCurrent(state),
           "literal.worker-prepare-does-not-mutate-owner");
    if (!prepared)
        return;

    const int undoBefore = session.history.UndoCount();
    Expect(session.ApplyPreparedMergeIfCurrent(prepared.value()), "literal.owner-publishes-once");
    const CanvasDocument after = session.Document().value();
    const ImageLayer* merged = session.ActiveLayer();
    Expect(merged && merged->name == L"Lower" && merged->parentID == plan->parent &&
               merged->transform.origin == Vec2{1, 0} && merged->transform.size == Vec2{2, 2} && merged->asset &&
               merged->asset->image.Width() == 2 && merged->asset->image.Height() == 2 &&
               PixelsEqual(merged->asset->image, kTrimmedPixels),
           "literal-trim-origin-and-exact-bgra");
    const ImageLayer* survivor = nullptr;
    for (const auto& layer : after.layers)
        if (layer.name == L"Survivor")
            survivor = &layer;
    Expect(after.layers.size() == 3 && survivor && merged && after.layers[1].id == merged->id &&
               survivor->maskSourceID == merged->id &&
               merged->parentID == std::optional<Guid>{Id("20000000-0000-4000-8000-000000000001")} &&
               session.history.UndoCount() == undoBefore + 1 && session.history.UndoName() == L"Merge Down",
           "literal-survivor-clip-parent-and-one-merge-down-entry");
    Expect(after.selection == before.selection && !session.IsMaskSelected(),
           "literal-selection-preserved-and-mask-target-cleared");
    Expect(!session.ApplyPreparedMergeIfCurrent(prepared.value()), "literal-publication-is-single-use");
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{before}, "literal-one-undo-restores-exact-source");
    session.Redo();
    Expect(session.Document() == std::optional<CanvasDocument>{after}, "literal-one-redo-restores-exact-merged-result");
}

void SeedRedo(DocumentSession& session)
{
    session.SetOpacity(0.5);
    session.Undo();
}

bool SeededRedoUnchanged(const DocumentSession& session, const CanvasDocument& before, const Guid& revision,
                         int undoCount, const std::optional<JobToken>& sourceToken)
{
    return sourceToken && session.Document() == std::optional<CanvasDocument>{before} &&
           session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount &&
           undoCount == 0 && session.history.UndoName().empty() && session.history.CanRedo() &&
           session.history.RedoName() == L"Layer Opacity" && session.history.CaptureState().FutureCount() == 1 &&
           session.CanApplyJobResult(sourceToken.value(), 73);
}

void MergeGroupHasLiteralPixelsAndRestoresParent()
{
    const Guid parent = Id("20000000-0000-4000-8000-000000000001");
    const CanvasDocument before = MergeDocument(true);
    DocumentSession session;
    session.ReplaceDocument(before, parent);
    const Guid revision = session.history.CurrentRevision();
    const auto state = session.history.CaptureState();
    const auto plan = session.PlanMerge();
    Expect(plan && plan->action == L"Merge Group" && plan->name == L"Parent" && !plan->parent &&
               plan->anchor == parent && plan->removed.size() == 4 &&
               plan->ids == std::vector<Guid>{parent, Id("20000000-0000-4000-8000-000000000002"),
                                              Id("20000000-0000-4000-8000-000000000003"),
                                              Id("20000000-0000-4000-8000-000000000004")},
           "group.plan-removes-parent-and-all-three-children");
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    auto prepared = plan ? Prepare(session, plan.value(), Bgra(3, 2, kCompositePixels), status) : std::nullopt;
    Expect(prepared && status == DocumentSession::MergePublicationStatus::Ready &&
               session.Document() == std::optional<CanvasDocument>{before} && session.ActiveLayerID() == parent &&
               session.history.IsCurrent(state),
           "group.preparation-keeps-original-records-and-active-parent");
    if (!prepared)
        return;
    Expect(session.ApplyPreparedMergeIfCurrent(prepared.value()), "group.owner-publishes");
    const ImageLayer* merged = session.ActiveLayer();
    Expect(session.Document()->layers.size() == 1 && merged && merged->id != parent && merged->name == L"Parent" &&
               !merged->isGroup && !merged->parentID && !merged->maskSourceID && !merged->mask && merged->asset &&
               merged->transform.origin == Vec2{1, 0} && merged->transform.size == Vec2{2, 2} &&
               merged->asset->image.Width() == 2 && merged->asset->image.Height() == 2 &&
               PixelsEqual(merged->asset->image, kTrimmedPixels) && session.Document()->width == 3 &&
               session.Document()->height == 2 && session.Document()->selection == before.selection,
           "group.only-merged-parent-remains-with-literal-canvas-trim");
    Expect(session.history.UndoCount() == 1 && session.history.UndoName() == L"Merge Group" &&
               session.history.CurrentRevision() != revision,
           "group.one-merge-group-entry");
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{before} && session.ActiveLayerID() == parent &&
               session.history.CurrentRevision() == revision && session.history.UndoCount() == 0 &&
               session.history.UndoName().empty() && session.history.RedoName() == L"Merge Group",
           "group.undo-restores-active-parent-and-all-original-records");
}

void TransparentCompositePreservesExistingExtent()
{
    // Existing native Adjust::TrimmedToAlpha and legacy PixelFilter.trimmed keep an all-transparent input's extent.
    constexpr std::array<std::uint8_t, 24> transparentCanvas{};
    const CanvasDocument before = MergeDocument(false);
    const Guid upper = Id("20000000-0000-4000-8000-000000000003");
    DocumentSession session;
    session.ReplaceDocument(before, upper);
    const auto plan = session.PlanMerge();
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    auto prepared = plan ? Prepare(session, plan.value(), Bgra(3, 2, transparentCanvas), status) : std::nullopt;
    Expect(prepared && status == DocumentSession::MergePublicationStatus::Ready, "transparent.canvas-prepares-ready");
    if (!prepared)
        return;
    Expect(session.ApplyPreparedMergeIfCurrent(prepared.value()), "transparent.owner-publishes");
    const ImageLayer* merged = session.ActiveLayer();
    Expect(merged && merged->asset && merged->transform.origin == Vec2{0, 0} && merged->transform.size == Vec2{3, 2} &&
               merged->asset->image.Width() == 3 && merged->asset->image.Height() == 2 &&
               PixelsEqual(merged->asset->image, transparentCanvas),
           "transparent.existing-trim-preserves-three-by-two-zero-bgra");
    session.Undo();
    Expect(session.Document() == std::optional<CanvasDocument>{before} && session.ActiveLayerID() == upper,
           "transparent.undo-restores-exact-source");
}

void MalformedPlansKeepSeededRedo()
{
    DocumentSession session;
    session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
    SeedRedo(session);
    const auto plan = session.PlanMerge();
    const CanvasDocument before = session.Document().value();
    const Guid revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    const auto sourceToken = session.CaptureJobToken(std::nullopt, 73);
    const auto state = session.history.CaptureState();
    if (!plan)
    {
        Expect(false, "malformed.fixture-has-merge-down-plan");
        return;
    }
    std::array malformed{plan.value(), plan.value()};
    malformed[0].ids.push_back(Id("20000000-0000-4000-8000-000000000002"));
    malformed[1].removed.insert(Id("20000000-0000-4000-8000-000000000004"));
    const Raster composite = Bgra(3, 2, kCompositePixels);
    for (std::size_t index = 0; index < malformed.size(); ++index)
    {
        DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Ready;
        auto prepared = sourceToken
                            ? DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(),
                                                                       sourceToken.value(), state, malformed[index],
                                                                       composite, status)
                            : std::nullopt;
        Expect(!prepared && status == DocumentSession::MergePublicationStatus::Invalid &&
                   SeededRedoUnchanged(session, before, revision, undoCount, sourceToken) &&
                   session.history.IsCurrent(state),
               index == 0 ? "malformed.duplicate-plan-ids-refused" : "malformed.extra-removed-id-refused");
    }
}

void SparseSurvivorMakesMergeExceedD7Budget()
{
    const Raster huge = Raster::MakeSparse(9999, 10000, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    constexpr std::array<std::uint8_t, 4> thumbnailPixel{0, 0, 0, 0};
    const Raster thumbnail = Bgra(1, 1, thumbnailPixel);
    const CanvasDocument baseline = MergeDocument(false);
    DocumentFixture document = DocumentFixture::Copy(baseline);
    document.width = 30000;
    // Assign the huge survivor's thumbnail directly; never sample or materialize its 99,990,000-pixel image.
    document.layers[2].asset = RasterAsset{huge, thumbnail, L"Survivor"};
    document.layers[2].transform.size = Vec2{9999, 10000};
    const CanvasDocument published = std::move(document).Seal();
    DocumentSession session;
    session.ReplaceDocument(published, Id("20000000-0000-4000-8000-000000000003"));
    Expect(ValidateDocument(published) == DocumentStatus::Ok && session.Document() && huge.IsSparse() &&
               huge.PixelCount() == 99'990'000 && !huge.IsMaterialized() && huge.ByteSize() == 0 &&
               published.layers[0].asset->image.PixelCount() + published.layers[1].asset->image.PixelCount() +
                       published.layers[2].asset->image.PixelCount() ==
                   99'990'002,
           "budget.valid-source-has-99990002-pixels-with-no-huge-pixel-buffer");
    if (!session.Document())
        return;
    SeedRedo(session);
    const auto plan = session.PlanMerge();
    Expect(plan &&
               plan->ids == std::vector<Guid>{Id("20000000-0000-4000-8000-000000000002"),
                                              Id("20000000-0000-4000-8000-000000000003")} &&
               plan->removed.size() == 2 && session.PixelBudgetLeft(false, plan->removed) == 10'000,
           "budget.excluding-two-source-pixels-leaves-literal-10000-pixels");
    // Opposite opaque corners force a 30,000 x 2 trim (60,000 pixels), using only this 240,000-byte composite.
    std::vector<std::uint8_t> canvasBytes(240'000, 0);
    constexpr std::array<std::uint8_t, 4> firstCorner{11, 22, 33, 255};
    constexpr std::array<std::uint8_t, 4> lastCorner{44, 55, 66, 255};
    std::copy(firstCorner.begin(), firstCorner.end(), canvasBytes.begin());
    std::copy(lastCorner.begin(), lastCorner.end(), canvasBytes.end() - 4);
    const Raster composite = Bgra(30000, 2, canvasBytes);
    Expect(composite.HasStorage() && composite.PixelCount() == 60'000 && composite.ByteSize() == 240'000,
           "budget.literal-opposite-corners-force-60000-pixel-trim");
    const CanvasDocument before = session.Document().value();
    const Guid revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    const auto sourceToken = session.CaptureJobToken(std::nullopt, 73);
    const auto state = session.history.CaptureState();
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    auto prepared =
        plan && sourceToken
            ? DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(), sourceToken.value(),
                                                       state, plan.value(), composite, status)
            : std::nullopt;
    Expect(!prepared && status == DocumentSession::MergePublicationStatus::TooLarge &&
               SeededRedoUnchanged(session, before, revision, undoCount, sourceToken) &&
               session.history.IsCurrent(state),
           "budget.too-large-refusal-keeps-source-revision-history-redo-and-token");
    const Raster& survivor = session.Document()->layers[2].asset->image;
    Expect(survivor == huge && !survivor.IsMaterialized() && survivor.ByteSize() == 0 && !huge.IsMaterialized() &&
               !huge.Base().HasStorage() && huge.Tiles().empty(),
           "budget.refusal-keeps-huge-survivor-unmaterialized");
}

struct CancelAtStage final
{
    CancelSource* source = nullptr;
    DocumentHistory::PrepareStage stage{};
    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        auto& self = *static_cast<CancelAtStage*>(context);
        if (stage == self.stage)
            (void)self.source->Cancel();
    }
};

struct FailAtStage final
{
    DocumentHistory::PrepareStage stage{};
    static void Checkpoint(void* context, DocumentHistory::PrepareStage stage)
    {
        if (stage == static_cast<FailAtStage*>(context)->stage)
            throw std::bad_alloc{};
    }
};

void CancellationAndAllocationFailuresKeepRedo()
{
    constexpr std::array stages{DocumentHistory::PrepareStage::BeforeSnapshot,
                                DocumentHistory::PrepareStage::AfterSnapshot, DocumentHistory::PrepareStage::Entry,
                                DocumentHistory::PrepareStage::StackGrowth, DocumentHistory::PrepareStage::TrimIndex};
    const Raster composite = Bgra(3, 2, kCompositePixels);
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        SeedRedo(session);
        const auto plan = session.PlanMerge();
        const CanvasDocument before = session.Document().value();
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        const auto sourceToken = session.CaptureJobToken(std::nullopt, 73);
        CancelSource cancel;
        CancelAtStage callback{&cancel, stages[index]};
        DocumentHistory::PrepareObserver observer{&CancelAtStage::Checkpoint, &callback};
        DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
        auto prepared =
            plan && sourceToken
                ? DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(),
                                                           sourceToken.value(), session.history.CaptureState(),
                                                           plan.value(), composite, status, cancel.Token(), observer)
                : std::nullopt;
        const char* label = index == 0   ? "cancel-before-snapshot"
                            : index == 1 ? "cancel-after-snapshot"
                            : index == 2 ? "cancel-entry"
                            : index == 3 ? "cancel-stack-growth"
                                         : "cancel-trim-index";
        Expect(!prepared && status == DocumentSession::MergePublicationStatus::Cancelled &&
                   SeededRedoUnchanged(session, before, revision, undoCount, sourceToken),
               label);
    }
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        SeedRedo(session);
        const auto plan = session.PlanMerge();
        const CanvasDocument before = session.Document().value();
        const Guid revision = session.history.CurrentRevision();
        const int undoCount = session.history.UndoCount();
        const auto sourceToken = session.CaptureJobToken(std::nullopt, 73);
        FailAtStage callback{stages[index]};
        DocumentHistory::PrepareObserver observer{&FailAtStage::Checkpoint, &callback};
        DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
        bool threw = false;
        try
        {
            auto prepared = plan && sourceToken
                                ? DocumentSession::PrepareMergePublication(
                                      session.Document(), session.ActiveLayerID(), sourceToken.value(),
                                      session.history.CaptureState(), plan.value(), composite, status, {}, observer)
                                : std::nullopt;
            (void)prepared;
        }
        catch (const std::bad_alloc&)
        {
            threw = true;
        }
        const char* label = index == 0   ? "oom-before-snapshot"
                            : index == 1 ? "oom-after-snapshot"
                            : index == 2 ? "oom-entry"
                            : index == 3 ? "oom-stack-growth"
                                         : "oom-trim-index";
        Expect(threw && SeededRedoUnchanged(session, before, revision, undoCount, sourceToken), label);
    }
}

void StaleAndUnauthorizedPublicationIsRefused()
{
    const Raster composite = Bgra(3, 2, kCompositePixels);
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        const auto token = session.CaptureJobToken(std::nullopt, 73);
        DocumentSession::MergePublicationStatus status{};
        auto absent = token ? DocumentSession::PrepareMergePublication(std::nullopt, session.ActiveLayerID(),
                                                                       token.value(), session.history.CaptureState(),
                                                                       plan.value(), composite, status)
                            : std::nullopt;
        Expect(!absent && status == DocumentSession::MergePublicationStatus::Invalid &&
                   session.Document().has_value() && session.history.UndoCount() == 0,
               "stale-null-document-rejected");
    }
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        DocumentSession::MergePublicationStatus status{};
        auto prepared = plan ? Prepare(session, plan.value(), composite, status) : std::nullopt;
        Expect(prepared.has_value(), "stale-document-candidate-ready");
        if (prepared)
        {
            session.EditForTest([](CanvasDocument& document) { document.resolution = 144; });
            Expect(!session.ApplyPreparedMergeIfCurrent(prepared.value()), "stale-document-token-rejected");
        }
    }
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        DocumentSession::MergePublicationStatus status{};
        auto prepared = plan ? Prepare(session, plan.value(), composite, status) : std::nullopt;
        Expect(prepared.has_value(), "active-only-candidate-ready");
        if (!prepared)
            return;
        session.SelectLayer(Id("20000000-0000-4000-8000-000000000004"));
        Expect(!session.ApplyPreparedMergeIfCurrent(prepared.value()), "active-only-change-rejected");
    }
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        DocumentSession::MergePublicationStatus status{};
        auto prepared = plan ? Prepare(session, plan.value(), composite, status) : std::nullopt;
        Expect(prepared.has_value(), "epoch-only-candidate-ready");
        if (!prepared)
            return;
        const auto document = session.Document();
        const auto active = session.ActiveLayerID();
        const std::uint64_t generation = session.Generation();
        session.history.Begin(L"Open/close epoch", document, active);
        session.history.End(document, active);
        Expect(session.Generation() == generation && !session.ApplyPreparedMergeIfCurrent(prepared.value()),
               "epoch-only-begin-end-invalidates-publication");
    }
    {
        DocumentSession session;
        DocumentSession foreign;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        foreign.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        const auto token = session.CaptureJobToken(std::nullopt, 73);
        DocumentSession::MergePublicationStatus status{};
        auto candidate = token && plan
                             ? DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(),
                                                                        token.value(), foreign.history.CaptureState(),
                                                                        plan.value(), composite, status)
                             : std::nullopt;
        Expect(candidate && !session.ApplyPreparedMergeIfCurrent(candidate.value()), "foreign-history-owner-rejected");
    }
    {
        DocumentSession session;
        session.ReplaceDocument(MergeDocument(false), Id("20000000-0000-4000-8000-000000000003"));
        const auto plan = session.PlanMerge();
        DocumentSession::MergePublicationStatus status{};
        auto source = plan ? Prepare(session, plan.value(), composite, status) : std::nullopt;
        Expect(source.has_value(), "moved-candidate-ready");
        if (!source)
            return;
        DocumentSession::PreparedMergePublication moved(std::move(source.value()));
        Expect(!session.ApplyPreparedMergeIfCurrent(source.value()) && session.ApplyPreparedMergeIfCurrent(moved),
               "moved-from-refused-and-moved-owner-publishes");
    }
}
} // namespace

int RunMergeHistoryTests()
{
    gFailures = 0;
    gAssertions = 0;
    LiteralCompositeAndSingleHistoryEntry();
    MergeGroupHasLiteralPixelsAndRestoresParent();
    TransparentCompositePreservesExistingExtent();
    MalformedPlansKeepSeededRedo();
    SparseSurvivorMakesMergeExceedD7Budget();
    CancellationAndAllocationFailuresKeepRedo();
    StaleAndUnauthorizedPublicationIsRefused();
    std::printf("merge-history.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}

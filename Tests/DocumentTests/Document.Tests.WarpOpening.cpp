#include "Document.Tests.WarpOpening.h"

#include "../Support/DocumentFixture.h"
#include "Executor.h"
#include "SharedPublicationMemory.h"
#include "WarpOpeningWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s warp-opening.%s\n", condition ? "PASS" : "FAIL", name);
    ++gChecks;
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 2;
constexpr std::array<std::uint8_t, 4> kPremultiplied{12, 24, 48, 96};
constexpr BrushSettings kSettings{2, 1, 0, 0, 0, 1};

Raster LiteralImage()
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t offset = 0; offset < pixels.size(); offset += kPremultiplied.size())
        for (std::size_t channel = 0; channel < kPremultiplied.size(); ++channel)
            pixels[offset + channel] = kPremultiplied[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster TransparentImage()
{
    constexpr std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

CanvasDocument FixtureDocument(const Raster& image, bool attachImage = true)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    ImageLayer layer = ImageLayer::Blank(L"Warp opening", Vec2{kSide, kSide});
    if (attachImage)
        layer.asset = RasterAsset{image, Raster::Make(1, 1, RasterFormat::Bgra8888), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    DocumentSession session;
    Guid active;

    explicit Fixture(const Raster& image, bool attachImage = true)
    {
        CanvasDocument document = FixtureDocument(image, attachImage);
        active = document.layers.front().id;
        session.ReplaceDocument(std::move(document), active);
    }
};

WarpOpeningOptions Options(DocumentSession& session, SmearMode mode = SmearMode::Liquify, Vec2 point = Vec2{0, 0},
                           BrushSettings settings = kSettings)
{
    (void)session;
    return WarpOpeningOptions{settings, mode, point, std::this_thread::get_id()};
}

struct WorkerResult final
{
    std::optional<WarpOpeningResult> result;
    bool joined = false;

    WorkerResult() = default;
    WorkerResult(const WorkerResult&) = delete;
    WorkerResult& operator=(const WorkerResult&) = delete;
    WorkerResult(WorkerResult&&) noexcept = default;
};

WorkerResult PrepareDetached(WarpOpeningInput input, CancelToken cancellation = CancelToken{},
                             WarpOpeningObserver observer = {})
{
    WorkerResult work;
    std::thread worker(
        [&work, input = std::move(input), cancellation, observer]() mutable
        {
            auto memory = SharedPublicationMemory::CreateForWork();
            work.result.emplace(DocumentSession::PrepareWarpOpeningPublication(std::move(input), cancellation, observer,
                                                                               std::move(memory)));
        });
    worker.join();
    work.joined = true;
    return work;
}

// Tiny independent numeric fixtures explicitly request a full snapshot on a joined worker. Production
// opening preparation never creates this test-only snapshot, and the large opening fixture does not call it.
Raster FixtureDisplay(PreparedWarpOpening& prepared)
{
    Raster display;
    std::thread snapshot(
        [&]
        {
            if (auto* draft = prepared.DraftForWork())
                display = draft->ImageForWork({});
        });
    snapshot.join();
    return display;
}

bool PixelEquals(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool LiteralDisplay(const Raster& raster) noexcept
{
    if (raster.Width() != kSide || raster.Height() != kSide || raster.Format() != RasterFormat::Bgra8888)
        return false;
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
            if (!PixelEquals(raster, x, y, kPremultiplied))
                return false;
    return true;
}

bool TransparentDisplay(const Raster& raster) noexcept
{
    if (raster.Width() != kSide || raster.Height() != kSide || raster.Format() != RasterFormat::Bgra8888)
        return false;
    constexpr std::array<std::uint8_t, 4> transparent{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
            if (!PixelEquals(raster, x, y, transparent))
                return false;
    return true;
}

bool SparseLiteralDisplay(const Raster& raster) noexcept
{
    if (raster.Width() != kSide || raster.Height() != kSide || raster.Format() != RasterFormat::Bgra8888)
        return false;
    constexpr std::array<std::uint8_t, 4> transparent{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
            if (!PixelEquals(raster, x, y, x == 0 && y == 0 ? kPremultiplied : transparent))
                return false;
    return true;
}

bool CaptureAndPrepare(Fixture& fixture, SmearMode mode, std::optional<WarpOpeningResult>& result)
{
    auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session, mode));
    if (!input)
        return false;
    WorkerResult work = PrepareDetached(std::move(input.value()));
    if (!work.joined || !work.result)
        return false;
    result.emplace(std::move(work.result.value()));
    return result->status == WarpOpeningStatus::Ready && result->prepared.has_value();
}

bool Apply(Fixture& fixture, PreparedWarpOpening& prepared)
{
    return fixture.session.IsPreparedWarpOpeningCurrent(prepared) &&
           fixture.session.ApplyPreparedWarpOpeningIfCurrent(prepared);
}

void TypeWorkerAndOwnerPublication()
{
    static_assert(!std::is_copy_constructible_v<WarpOpeningInput>);
    static_assert(std::is_nothrow_move_constructible_v<WarpOpeningInput>);
    static_assert(!std::is_copy_constructible_v<PreparedWarpOpening>);
    static_assert(std::is_nothrow_move_constructible_v<PreparedWarpOpening>);

    Fixture fixture(LiteralImage());
    const auto revision = fixture.session.history.CurrentRevision();
    const auto authority = fixture.session.history.CaptureAuthority();
    auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session, SmearMode::Liquify, Vec2{0, 0}));
    Expect(input.has_value(), "capture.accepts-owner-thread-input");
    Expect(fixture.session.PendingWarp() == nullptr && fixture.session.history.CurrentRevision() == revision &&
               fixture.session.history.IsCurrent(authority) && fixture.session.history.UndoCount() == 0,
           "capture-does-not-open-draft-or-enter-history");
    if (!input)
        return;

    WorkerResult work = PrepareDetached(std::move(input.value()));
    Expect(work.joined && work.result && work.result->status == WarpOpeningStatus::Ready && work.result->prepared,
           "worker-builds-detached-ready-state-before-join");
    if (!work.result || !work.result->prepared)
        return;
    PreparedWarpOpening& prepared = work.result->prepared.value();
    Expect(LiteralDisplay(FixtureDisplay(prepared)), "worker-display-matches-independent-premultiplied-2x2-literal");
    Expect(fixture.session.PendingWarp() == nullptr && fixture.session.history.CurrentRevision() == revision,
           "worker-does-not-publish-into-session");
    Expect(!prepared.TakeAppliedDraftForOwner(), "draft-transfer-before-apply-is-empty");
    Expect(Apply(fixture, prepared) && fixture.session.PendingWarp() != nullptr,
           "owner-applies-current-preparation-once");
    Expect(!fixture.session.IsPreparedWarpOpeningCurrent(prepared) &&
               !fixture.session.ApplyPreparedWarpOpeningIfCurrent(prepared),
           "owner-refuses-duplicate-apply");
    const ImageLayer* layer = fixture.session.ActiveLayer();
    Expect(layer && layer->asset && LiteralDisplay(layer->asset->image) &&
               fixture.session.history.CurrentRevision() == revision && fixture.session.history.UndoCount() == 0,
           "opening-does-not-change-source-pixels-or-history");
    auto draft = prepared.TakeAppliedDraftForOwner();
    Expect(draft && draft.get() == fixture.session.PendingWarp() && draft->IsValid() && draft->Points().empty(),
           "applied-draft-transfers-without-replaying-the-initial-point");
    fixture.session.CancelWarp();
}

void ModesBlankAndSparseSource()
{
    for (const SmearMode mode : {SmearMode::Liquify, SmearMode::Smudge})
    {
        Fixture fixture(LiteralImage());
        std::optional<WarpOpeningResult> result;
        const bool ready = CaptureAndPrepare(fixture, mode, result);
        Expect(ready && result && result->prepared && LiteralDisplay(FixtureDisplay(*result->prepared)),
               mode == SmearMode::Liquify ? "mode.liquify-ready-literal-display" : "mode.smudge-ready-literal-display");
        if (ready && result && result->prepared)
        {
            const bool applied = Apply(fixture, *result->prepared);
            const WarpStroke* pending = fixture.session.PendingWarp();
            Expect(applied && pending && pending->Mode() == mode && pending->Points().empty(),
                   mode == SmearMode::Liquify ? "mode.liquify-owner-draft" : "mode.smudge-owner-draft");
            fixture.session.CancelWarp();
        }
    }

    {
        Fixture blank(TransparentImage(), false);
        std::optional<WarpOpeningResult> result;
        const bool ready = CaptureAndPrepare(blank, SmearMode::Liquify, result);
        Expect(ready && result && result->prepared && TransparentDisplay(FixtureDisplay(*result->prepared)),
               "blank-layer-worker-produces-literal-transparent-zero");
        if (ready && result && result->prepared)
        {
            const auto revision = blank.session.history.CurrentRevision();
            const bool applied = Apply(blank, *result->prepared);
            Expect(applied && blank.session.PendingWarp() && blank.session.history.CurrentRevision() == revision &&
                       blank.session.history.UndoCount() == 0,
                   "blank-layer-opens-without-history-entry");
            blank.session.CancelWarp();
        }
    }

    {
        const Raster onePixel =
            Raster::Make(1, 1, RasterFormat::Bgra8888, kPremultiplied.data(), kPremultiplied.size());
        const Raster sparse = Raster::MakeSparse(kSide, kSide, RasterFormat::Bgra8888, onePixel, 0, 0, {}, 0);
        Fixture fixture(sparse);
        Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "sparse.fixture-starts-unmaterialized");
        std::optional<WarpOpeningResult> result;
        const bool ready = CaptureAndPrepare(fixture, SmearMode::Smudge, result);
        const ImageLayer* layer = fixture.session.ActiveLayer();
        Expect(ready && result && result->prepared && SparseLiteralDisplay(FixtureDisplay(*result->prepared)) &&
                   layer && layer->asset && layer->asset->image.IsSparse() && !layer->asset->image.IsMaterialized() &&
                   !sparse.IsMaterialized(),
               "sparse.worker-renders-literal-without-materializing-source");
    }
}

void CapturedSettingsAndModes()
{
    Fixture fixture(LiteralImage());
    fixture.session.brushSettings = BrushSettings{2, 0.5, 0, 0, 0, 0.75};
    fixture.session.smearMode = SmearMode::Smudge;
    auto input = fixture.session.PrepareWarpOpeningInput(
        Options(fixture.session, fixture.session.smearMode, Vec2{0.25, 0.75}, fixture.session.brushSettings));
    Expect(input.has_value(), "frozen.capture-valid-toolbar-state");
    if (!input)
        return;
    fixture.session.brushSettings = BrushSettings{2000, 1, 0, 0, 0, 0.01};
    fixture.session.smearMode = SmearMode::Liquify;
    WorkerResult work = PrepareDetached(std::move(input.value()));
    Expect(work.result && work.result->status == WarpOpeningStatus::Ready && work.result->prepared,
           "frozen.worker-finishes-after-toolbar-change");
    if (!work.result || !work.result->prepared)
        return;
    PreparedWarpOpening& prepared = work.result->prepared.value();
    const bool applied = Apply(fixture, prepared);
    const WarpStroke* pending = fixture.session.PendingWarp();
    Expect(applied && pending && pending->Mode() == SmearMode::Smudge && pending->Diameter() == 2 &&
               pending->Points().empty(),
           "frozen.owner-draft-uses-event-mode-point-and-settings");
    auto draft = prepared.TakeAppliedDraftForOwner();
    bool accepted = false;
    if (draft)
    {
        std::thread nextSample([&]() { accepted = draft->AppendForWork({2.25, 0.75}, {}); });
        nextSample.join();
    }
    Expect(accepted && draft && draft->Points().size() == 2 && draft->Points()[0] == Vec2{1.25, 0.75} &&
               draft->Points()[1] == Vec2{2.25, 0.75},
           "frozen.next-worker-sample-uses-literal-captured-origin-and-spacing");
    fixture.session.CancelWarp();
}

void Refusals()
{
    {
        Fixture fixture(LiteralImage());
        fixture.session.AddLayerMask(true);
        fixture.session.SelectLayerTarget(fixture.active, true);
        Expect(!fixture.session.PrepareWarpOpeningInput(Options(fixture.session)) &&
                   fixture.session.PendingWarp() == nullptr,
               "refusal.mask-target-does-not-open");
    }
    {
        Fixture fixture(LiteralImage());
        auto options = Options(fixture.session);
        options.point.x = (std::numeric_limits<double>::quiet_NaN)();
        Expect(!fixture.session.PrepareWarpOpeningInput(options), "refusal.nonfinite-point");
    }
    {
        Fixture fixture(LiteralImage());
        auto options = Options(fixture.session);
        options.mode = static_cast<SmearMode>(0xFF);
        Expect(!fixture.session.PrepareWarpOpeningInput(options), "refusal.invalid-mode");
    }
    {
        Fixture fixture(LiteralImage());
        fixture.session.SetSelection(DocumentSelection{false, SelectionLoops{}}, L"Empty selection");
        Expect(fixture.session.Selection() && fixture.session.Selection()->loops.Get().empty() &&
                   !fixture.session.PrepareWarpOpeningInput(Options(fixture.session)),
               "refusal.empty-selection");
    }
    {
        Fixture fixture(LiteralImage());
        auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session));
        if (!input)
        {
            Expect(false, "refusal.same-thread-worker-input-captured");
            return;
        }
        std::shared_ptr<SharedPublicationMemory> memory;
        std::thread allocator([&memory] { memory = SharedPublicationMemory::CreateForWork(); });
        allocator.join();
        const auto result = DocumentSession::PrepareWarpOpeningPublication(std::move(input.value()), CancelToken{}, {},
                                                                           std::move(memory));
        Expect(result.status == WarpOpeningStatus::Invalid && !result.prepared &&
                   fixture.session.PendingWarp() == nullptr,
               "refusal.worker-preparation-on-owner-thread");
    }
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = kSide;
        document.height = kSide;
        ImageLayer hidden = ImageLayer::Blank(L"Hidden target", Vec2{kSide, kSide});
        hidden.asset = RasterAsset{LiteralImage(), Raster::Make(1, 1, RasterFormat::Bgra8888), hidden.name};
        hidden.visible = false;
        const Guid target = hidden.id;
        document.layers.push_back(std::move(hidden));
        Fixture fixture(LiteralImage());
        fixture.session.ReplaceDocument(std::move(document).Seal(), target);
        auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session));
        std::optional<WorkerResult> work;
        if (input)
            work.emplace(PrepareDetached(std::move(input.value())));
        Expect(work && work->joined && work->result && work->result->status == WarpOpeningStatus::Invalid &&
                   !work->result->prepared && fixture.session.PendingWarp() == nullptr,
               "refusal.worker-rejects-hidden-target");
    }
    {
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = kSide;
        document.height = kSide;
        ImageLayer group = ImageLayer::Blank(L"Hidden group", Vec2{kSide, kSide});
        group.isGroup = true;
        group.visible = false;
        const Guid groupID = group.id;
        ImageLayer child = ImageLayer::Blank(L"Visible child", Vec2{kSide, kSide});
        child.parentID = groupID;
        child.asset = RasterAsset{LiteralImage(), Raster::Make(1, 1, RasterFormat::Bgra8888), child.name};
        const Guid target = child.id;
        document.layers.push_back(std::move(group));
        document.layers.push_back(std::move(child));
        Fixture fixture(LiteralImage());
        fixture.session.ReplaceDocument(std::move(document).Seal(), target);
        auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session));
        std::optional<WorkerResult> work;
        if (input)
            work.emplace(PrepareDetached(std::move(input.value())));
        Expect(work && work->joined && work->result && work->result->status == WarpOpeningStatus::Invalid &&
                   !work->result->prepared,
               "refusal.worker-rejects-target-under-hidden-ancestor");
    }
}

void StalePreparedState()
{
    enum class Mutation : std::uint8_t
    {
        Replacement,
        ActiveTarget,
        HistoryEpoch,
    };
    constexpr std::array<Mutation, 3> mutations{Mutation::Replacement, Mutation::ActiveTarget, Mutation::HistoryEpoch};
    constexpr std::array<const char*, 3> names{"replacement", "active-target", "history-epoch"};
    for (std::size_t i = 0; i < mutations.size(); ++i)
    {
        Fixture fixture(LiteralImage());
        Guid alternateTarget;
        if (mutations[i] == Mutation::ActiveTarget)
        {
            DocumentFixture changed = DocumentFixture::Copy(fixture.session.Document().value());
            ImageLayer extra = ImageLayer::Blank(L"Second target", Vec2{kSide, kSide});
            extra.asset = RasterAsset{TransparentImage(), Raster::Make(1, 1, RasterFormat::Bgra8888), extra.name};
            alternateTarget = extra.id;
            changed.layers.push_back(std::move(extra));
            fixture.session.ReplaceDocument(std::move(changed).Seal(), fixture.active);
        }
        auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session));
        if (!input)
        {
            Expect(false, "stale.capture-valid-input");
            continue;
        }
        WorkerResult work = PrepareDetached(std::move(input.value()));
        if (!work.result || !work.result->prepared)
        {
            Expect(false, "stale.worker-prepared-input");
            continue;
        }
        PreparedWarpOpening& prepared = work.result->prepared.value();
        bool mutationApplied = false;
        switch (mutations[i])
        {
        case Mutation::Replacement:
        {
            CanvasDocument replacement = FixtureDocument(TransparentImage());
            const Guid target = replacement.layers.front().id;
            fixture.session.ReplaceDocument(std::move(replacement), target);
            mutationApplied = fixture.session.ActiveLayer() && fixture.session.ActiveLayer()->asset &&
                              TransparentDisplay(fixture.session.ActiveLayer()->asset->image);
            break;
        }
        case Mutation::ActiveTarget:
        {
            const auto stableDocumentIdentity = fixture.session.Document()->layers.Identity();
            fixture.session.SelectLayer(alternateTarget);
            mutationApplied = fixture.session.ActiveLayerID() == alternateTarget && fixture.session.Document() &&
                              fixture.session.Document()->layers.Identity() == stableDocumentIdentity;
            break;
        }
        case Mutation::HistoryEpoch:
        {
            const auto authority = fixture.session.history.CaptureAuthority();
            fixture.session.AddBlankLayer();
            const bool added =
                !fixture.session.history.IsCurrent(authority) && fixture.session.history.UndoCount() == 1;
            fixture.session.Undo();
            mutationApplied = added && !fixture.session.history.IsCurrent(authority) &&
                              fixture.session.history.CaptureState().FutureCount() == 1;
            break;
        }
        }
        const auto revision = fixture.session.history.CurrentRevision();
        const int undoCount = fixture.session.history.UndoCount();
        const std::size_t redoCount = fixture.session.history.CaptureState().FutureCount();
        const bool stale = mutationApplied && !fixture.session.IsPreparedWarpOpeningCurrent(prepared);
        const bool refused = mutationApplied && !fixture.session.ApplyPreparedWarpOpeningIfCurrent(prepared);
        Expect(mutationApplied && stale && refused && fixture.session.PendingWarp() == nullptr &&
                   fixture.session.history.CurrentRevision() == revision &&
                   fixture.session.history.UndoCount() == undoCount &&
                   fixture.session.history.CaptureState().FutureCount() == redoCount,
               names[i]);
    }
}

void ReopenedBlankAuthorityAndMemoryBounds()
{
    Fixture blank(TransparentImage(), false);
    const auto options = Options(blank.session);
    const auto originalRoot = blank.session.Document()->layers.Identity();
    const auto originalGeneration = blank.session.Generation();
    const auto revision = blank.session.history.CurrentRevision();
    auto input = blank.session.PrepareWarpOpeningInput(options);
    Expect(input.has_value(), "reopened-blank.capture-original-input");
    if (!input)
        return;
    auto work = PrepareDetached(std::move(input.value()));
    Expect(work.result && work.result->prepared && work.result->status == WarpOpeningStatus::Ready,
           "reopened-blank.original-worker-ready");
    if (!work.result || !work.result->prepared)
        return;
    const bool reopened = blank.session.BeginWarp({0, 0});
    blank.session.CancelWarp();
    Expect(reopened && blank.session.Document()->layers.Identity() == originalRoot &&
               blank.session.Generation() == originalGeneration && blank.session.history.CurrentRevision() == revision,
           "reopened-blank.new-opening-and-cancel-retain-exact-document-history");
    Expect(!blank.session.IsPreparedWarpOpeningCurrent(*work.result->prepared) &&
               !blank.session.ApplyPreparedWarpOpeningIfCurrent(*work.result->prepared) && !blank.session.PendingWarp(),
           "reopened-blank.original-opening-counter-cannot-resurrect-after-cancel");

    Fixture literal(LiteralImage());
    auto liquidity = Options(literal.session);
    auto smudge = liquidity;
    smudge.mode = SmearMode::Smudge;
    const auto sourceRoot = literal.session.Document()->layers.Identity();
    const auto sourceImage = literal.session.ActiveLayer()->asset->image.Identity();
    const auto base = literal.session.WarpOpeningMemoryBytes(liquidity);
    const auto carried = literal.session.WarpOpeningMemoryBytes(smudge);
    // A 2 px tip carries a literal 3x3 four-float plane (144 bytes), plus one lazy 2x2 tile.
    // Liquify's initial origin reads no pixels; the allocation ownership is the PixelBuffer contract.
    Expect(base > 1024 * 1024 && carried - base == 144 + PixelBuffer::RequiredBytes(16) &&
               literal.session.Document()->layers.Identity() == sourceRoot &&
               literal.session.ActiveLayer()->asset->image.Identity() == sourceImage && !literal.session.PendingWarp(),
           "admission.literal-144-byte-pickup-plane-and-query-preserve-source");
    auto invalid = liquidity;
    invalid.settings.diameter = 2001;
    Expect(literal.session.WarpOpeningMemoryBytes(invalid) == 0, "admission.unsupported-tip-refuses-before-capture");
    invalid = liquidity;
    invalid.point.x = (std::numeric_limits<double>::infinity)();
    Expect(literal.session.WarpOpeningMemoryBytes(invalid) == 0,
           "admission.nonfinite-coordinate-refuses-before-capture");
    invalid = liquidity;
    invalid.ownerThread = {};
    Expect(literal.session.WarpOpeningMemoryBytes(invalid) == 0, "admission.missing-owner-refuses-before-capture");
    const auto initialCharge = base;
    const auto imageCharge = literal.session.ActiveLayer()->asset->image.RetirementMemoryBytes();
    const bool started = literal.session.BeginWarp({0, 0});
    literal.session.CancelWarp();
    Expect(started && imageCharge && literal.session.WarpOpeningMemoryBytes(liquidity) - initialCharge == imageCharge,
           "admission.previous-warp-source-retirement-remains-exactly-charged");
}

struct CancelAtStage final
{
    std::stop_source* source = nullptr;
    WarpOpeningStage target = WarpOpeningStage::Source;
    bool observed = false;
};

void CancelCheckpoint(void* context, WarpOpeningStage stage)
{
    auto& state = *static_cast<CancelAtStage*>(context);
    if (stage == state.target)
    {
        state.observed = true;
        static_cast<void>(state.source->request_stop());
    }
}

void CancellationAndNoMutation()
{
    constexpr std::array<WarpOpeningStage, 4> stages{WarpOpeningStage::Source, WarpOpeningStage::Draft,
                                                     WarpOpeningStage::FirstSample, WarpOpeningStage::Complete};
    constexpr std::array<const char*, 4> labels{"source", "draft", "first-sample", "complete"};
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        Fixture fixture(LiteralImage());
        auto input = fixture.session.PrepareWarpOpeningInput(Options(fixture.session));
        std::stop_source local;
        std::stop_source shutdown;
        std::stop_source& selected = (i % 2u) == 0u ? local : shutdown;
        CancelAtStage state{&selected, stages[i], false};
        const WarpOpeningObserver observer{&CancelCheckpoint, &state};
        const auto revision = fixture.session.history.CurrentRevision();
        const auto history = fixture.session.history.CaptureState();
        const auto* layerBefore = fixture.session.ActiveLayer();
        const auto identity = layerBefore && layerBefore->asset ? layerBefore->asset->image.Identity() : nullptr;
        const auto documentRoot = fixture.session.Document()->layers.Identity();
        const auto documentGeneration = fixture.session.Generation();
        std::optional<WorkerResult> work;
        if (input)
            work.emplace(PrepareDetached(std::move(input.value()), CancelToken{local.get_token(), shutdown.get_token()},
                                         observer));
        const auto* layerAfter = fixture.session.ActiveLayer();
        Expect(input.has_value() && state.observed && work && work->joined && work->result &&
                   work->result->status == WarpOpeningStatus::Cancelled && !work->result->prepared,
               labels[i]);
        Expect(fixture.session.PendingWarp() == nullptr && fixture.session.ActiveLayer() == layerBefore && layerAfter &&
                   layerAfter->asset && layerAfter->asset->image.Identity() == identity &&
                   fixture.session.Document()->layers.Identity() == documentRoot &&
                   fixture.session.Generation() == documentGeneration &&
                   fixture.session.history.CurrentRevision() == revision &&
                   fixture.session.history.UndoCount() == static_cast<int>(history.PastCount()) &&
                   fixture.session.history.CaptureState().FutureCount() == history.FutureCount(),
               "cancelled-worker-preserves-document-generation-and-history");
    }
}

void LargeOpeningWithoutRenderedCanvas()
{
    constexpr int side = 10000;
    const auto one = Raster::Make(1, 1, RasterFormat::Bgra8888, kPremultiplied.data(), kPremultiplied.size());
    const auto source = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, one, 0, 0, {}, 0);
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = document.height = side;
    auto layer = ImageLayer::Blank(L"Large Warp opening", {side, side});
    layer.asset = RasterAsset{source, {}, layer.name};
    const auto target = layer.id;
    const auto placement = layer.transform;
    document.layers.push_back(std::move(layer));
    Fixture fixture(LiteralImage());
    fixture.session.ReplaceDocument(std::move(document).Seal(), target);
    const auto options = Options(fixture.session, SmearMode::Smudge, {5000, 5000});
    const auto root = fixture.session.Document()->layers.Identity();
    const auto revision = fixture.session.history.CurrentRevision();
    const auto peak = fixture.session.Document()->RetirementMemoryBytes() + 1024 * 1024 +
                      PixelBuffer::RequiredBytes(256 * 256 * 4) + 144;
    auto input = fixture.session.PrepareWarpOpeningInput(options);
    Expect(input && fixture.session.WarpOpeningMemoryBytes(options) == peak && !source.IsMaterialized(),
           "100mp.capture-original-source-and-complete-first-tip-bound");
    if (!input)
        return;
    auto work = PrepareDetached(std::move(*input));
    Expect(work.joined && work.result && work.result->prepared && work.result->status == WarpOpeningStatus::Ready,
           "100mp.worker-opens-without-rendered-canvas");
    if (!work.result || !work.result->prepared)
        return;
    auto& prepared = *work.result->prepared;
    const auto& view = prepared.SourceViewForJoinedOwner();
    const auto* draft = prepared.DraftForWork();
    Expect(view.image == source && view.placement == placement && view.width == side && view.height == side && draft &&
               draft->IsValid() && draft->Points().empty() &&
               draft->RetirementMemoryBytes() > source.RetirementMemoryBytes() &&
               draft->RetirementMemoryBytes() - source.RetirementMemoryBytes() < 1024 * 1024 &&
               !source.IsMaterialized(),
           "100mp.original-placement-and-less-than-one-mib-private-draft-retained");
    Expect(Apply(fixture, prepared) && fixture.session.PendingWarp() == draft &&
               fixture.session.Document()->layers.Identity() == root &&
               fixture.session.history.CurrentRevision() == revision && fixture.session.history.UndoCount() == 0,
           "100mp.current-owner-opening-preserves-document-and-history");
    fixture.session.CancelWarp();
    Expect(!fixture.session.PendingWarp() && !source.IsMaterialized() &&
               fixture.session.Document()->layers.Identity() == root &&
               fixture.session.history.CurrentRevision() == revision,
           "100mp.cancel-preserves-original-sparse-source-and-history");
}
} // namespace

int RunWarpOpeningTests()
{
    gFailures = 0;
    gChecks = 0;
    TypeWorkerAndOwnerPublication();
    ModesBlankAndSparseSource();
    CapturedSettingsAndModes();
    Refusals();
    StalePreparedState();
    CancellationAndNoMutation();
    ReopenedBlankAuthorityAndMemoryBounds();
    LargeOpeningWithoutRenderedCanvas();
    std::printf("SUMMARY warp-opening checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}

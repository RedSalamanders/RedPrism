#include "Document.Tests.StrokeOpeningMemory.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Executor.h"
#include "Jobs.h"
#include "Selection.h"
#include "StrokeOpeningWork.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* fact) noexcept
{
    std::printf("%s stroke-opening-memory.%s\n", condition ? "PASS" : "FAIL", fact);
    gFailures += condition ? 0 : 1;
}

constexpr BrushSettings kSettings{8, 1, 0.25, 0.5, 0.75, 1};

ImageLayer BlankLayer(const wchar_t* name, int width, int height)
{
    return ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
}

Raster SmallBgra(int width = 8, int height = 8)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 17;
        pixels[i + 1] = 31;
        pixels[i + 2] = 47;
        pixels[i + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster SmallGray(int width = 8, int height = 8, std::uint8_t fill = 127)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

CanvasDocument DocumentWith(ImageLayer layer, int width = 8, int height = 8,
                            std::optional<DocumentSelection> selection = std::nullopt)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = width;
    fixture.height = height;
    fixture.layers.push_back(std::move(layer));
    fixture.selection = std::move(selection);
    return std::move(fixture).Seal();
}

bool GridIs(const std::optional<BrushStroke::GridPlan>& plan, int width, int height, GridRect source) noexcept
{
    return plan && plan->width == width && plan->height == height && plan->sourceRect == source;
}

void LiteralGridReferences()
{
    {
        const auto layer = BlankLayer(L"8 by 8 blank", 8, 8);
        const auto plan = BrushStroke::PlanGrid(layer, false, kSettings, 8, 8);
        Expect(GridIs(plan, 8, 8, {0, 0, 8, 8}), "grid.blank-8x8-literal");
    }
    {
        auto layer = BlankLayer(L"grown source", 8, 8);
        layer.asset = RasterAsset{SmallBgra(), {}, layer.name};
        const auto plan = BrushStroke::PlanGrid(layer, false, kSettings, 16, 16);
        Expect(GridIs(plan, 16, 16, {0, 0, 8, 8}), "grid.canvas-growth-is-16x16");
    }
    {
        auto layer = BlankLayer(L"scaled source", 4, 4);
        layer.asset = RasterAsset{SmallBgra(4, 4), {}, layer.name};
        layer.transform.origin = Vec2{2, 3};
        layer.transform.size = Vec2{16, 16};
        const auto plan = BrushStroke::PlanGrid(layer, false, kSettings, 8, 8);
        Expect(GridIs(plan, 5, 5, {1, 1, 4, 4}), "grid.scaled-source-crops-canvas-to-five");
    }
    {
        auto layer = BlankLayer(L"placed mask", 8, 8);
        LayerMask mask;
        mask.asset = RasterAsset{SmallGray(4, 4), {}, L"placed"};
        mask.placement = LayerTransform{Vec2{2, 1}, Vec2{8, 8}, 0, false, false, LayerSampling::Nearest};
        layer.mask = std::move(mask);
        const auto plan = BrushStroke::PlanGrid(layer, true, kSettings, 16, 16);
        Expect(GridIs(plan, 4, 4, {0, 0, 4, 4}) && plan->sourceTransform == layer.mask->placement.value(),
               "grid.placed-mask-keeps-own-four-pixel-grid");
    }
    {
        const auto empty = BlankLayer(L"empty", 8, 8);
        const auto plan = BrushStroke::PlanGrid(empty, false, kSettings, 8, 8);
        Expect(GridIs(plan, 8, 8, {0, 0, 8, 8}), "grid.empty-source-uses-literal-layer-size");
    }
    {
        const auto layer = BlankLayer(L"30k", 30'000, 30'000);
        const auto plan = BrushStroke::PlanGrid(layer, false, kSettings, 30'000, 30'000);
        Expect(GridIs(plan, 30'000, 30'000, {0, 0, 30'000, 30'000}), "grid.30k-round-boundary");
    }
    {
        auto layer = BlankLayer(L"inverse guard", 30'000, 3'000);
        const Raster sparse = Raster::MakeSparse(30'000, 3'000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
        layer.asset = RasterAsset{sparse, {}, layer.name};
        layer.transform.size = Vec2{1, 1};
        layer.transform.rotation = 45;
        Expect(!BrushStroke::PlanGrid(layer, false, kSettings, 30'000, 30'000), "grid.inverse-grid-side-cap");
    }
    {
        auto layer = BlankLayer(L"non-finite", 8, 8);
        layer.transform.origin.x = (std::numeric_limits<double>::infinity)();
        Expect(!BrushStroke::PlanGrid(layer, false, kSettings, 8, 8), "grid.non-finite-transform-refused");
    }
    {
        auto layer = BlankLayer(L"dimension-rounding", 8, 8);
        layer.transform.size.x = static_cast<double>(BrushStroke::kMaxGridSide) + 0.5;
        Expect(!BrushStroke::PlanGrid(layer, false, kSettings, 8, 8), "grid.dimension-rounding-guard");
    }
    {
        auto invalid = kSettings;
        invalid.diameter = 0;
        Expect(!BrushStroke::PlanGrid(BlankLayer(L"bad settings", 8, 8), false, invalid, 8, 8),
               "grid.invalid-settings-refused");
    }
}

void ActualOwnerLowerBounds()
{
    auto layer = BlankLayer(L"bounded source", 8, 8);
    layer.asset = RasterAsset{SmallBgra(), {}, layer.name};
    const Raster selectionCoverage = SmallGray(8, 8, 255);
    DocumentSelection selection{false, SelectionLoops{{Vec2{1, 1}, Vec2{7, 1}, Vec2{7, 7}, Vec2{1, 7}}}};
    CanvasDocument source = DocumentWith(layer, 8, 8, selection);
    const std::uint64_t sourceBytes = source.RetirementMemoryBytes();
    BrushStroke stroke(layer, false, kSettings, 8, 8, &CpuBrushCoverage::Shared());
    stroke.SetSelectionClip(selectionCoverage);
    const bool painted = stroke.Append(Vec2{4, 4});
    const auto& tiles = stroke.Tiles();
    const bool hasOneTile = painted && tiles.size() == 1 && tiles.front().base.HasStorage() &&
                            tiles.front().image.HasStorage() && !tiles.front().coverage.empty() &&
                            !tiles.front().permanent.empty();
    const auto declared =
        StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Paint);
    const auto strokeBytes = stroke.RetirementMemoryBytes();
    Expect(sourceBytes != 0 && selectionCoverage.RetirementMemoryBytes() != 0,
           "bound.constructed-document-and-coverage-have-owned-byte-records");
    Expect(hasOneTile && strokeBytes != 0, "bound.constructed-stroke-has-base-preview-and-cpu-density");
    Expect(declared >= sourceBytes + strokeBytes, "bound.paint-covers-independent-source-and-live-stroke-owners");
    Expect(StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Erase) != 0,
           "bound.erase-has-a-positive-opening-declaration");
    Expect(StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Heal) != 0,
           "bound.image-Heal-has-a-positive-opening-declaration");

    const Raster blurSample = SmallBgra();
    auto blurLayer = BlankLayer(L"blur source", 8, 8);
    blurLayer.asset = RasterAsset{SmallBgra(), {}, blurLayer.name};
    CanvasDocument blurSource = DocumentWith(blurLayer);
    BrushStroke blurStroke(blurLayer, false, kSettings, 8, 8, &CpuBrushCoverage::Shared(), BrushKind::Blur);
    blurStroke.SetSample(blurSample, 0, 0, false);
    const bool blurTile = blurStroke.Append(Vec2{4, 4}) && blurStroke.Tiles().size() == 1;
    const auto blurBound =
        StrokeOpeningMemoryBytes(blurSource, blurSource.layers.front(), false, 64, kSettings, BrushKind::Blur);
    Expect(blurSample.RetirementMemoryBytes() != 0 && blurTile && blurStroke.RetirementMemoryBytes() != 0,
           "bound.constructed-blur-sample-and-tile-are-live");
    Expect(blurBound >= blurSource.RetirementMemoryBytes() + blurStroke.RetirementMemoryBytes(),
           "bound.blur-covers-independent-source-sample-and-tile-owners");
}

void SparseDeclarationAndRefusals()
{
    {
        constexpr int side = 6'711; // 45,037,521 pixels; the sparse fixture allocates no pixel plane.
        const Raster raster = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
        auto layer = BlankLayer(L"45MP sparse", side, side);
        layer.asset = RasterAsset{raster, {}, layer.name};
        CanvasDocument source = DocumentWith(layer, side, side);
        const auto bytes = StrokeOpeningMemoryBytes(
            source, source.layers.front(), false, static_cast<std::int64_t>(side) * side, kSettings, BrushKind::Paint);
        Expect(raster.HasStorage() && raster.IsSparse() && !raster.IsMaterialized() && bytes != 0,
               "bound.45mp-sparse-declaration-is-nonzero");
    }
    {
        constexpr int side = 10'000;
        const Raster raster = Raster::MakeSparse(side, side, RasterFormat::Gray8, Raster{}, 0, 0, {}, 0);
        auto layer = BlankLayer(L"100M sparse mask", side, side);
        LayerMask mask;
        mask.asset.image = raster;
        layer.mask = std::move(mask);
        CanvasDocument source = DocumentWith(layer, side, side);
        const auto bytes = StrokeOpeningMemoryBytes(
            source, source.layers.front(), true, static_cast<std::int64_t>(side) * side, kSettings, BrushKind::Paint);
        Expect(raster.HasStorage() && raster.IsSparse() && !raster.IsMaterialized() && bytes != 0,
               "bound.100m-sparse-mask-declaration-is-nonzero");
    }
    auto layer = BlankLayer(L"negative controls", 8, 8);
    layer.asset = RasterAsset{SmallBgra(), {}, layer.name};
    CanvasDocument source = DocumentWith(layer);
    const auto zero = [&](std::int64_t budget, BrushKind kind) noexcept
    { return StrokeOpeningMemoryBytes(source, source.layers.front(), false, budget, kSettings, kind) == 0; };
    Expect(zero(-1, BrushKind::Paint), "refuse.negative-pixel-budget-is-exact-zero");
    Expect(zero(64, BrushKind::Clone), "refuse.clone-until-whole-state-handoff-is-wired");
    Expect(zero(64, static_cast<BrushKind>(255)), "refuse.unknown-brush-kind-is-exact-zero");
    Expect(zero(100'000'001, BrushKind::Paint), "refuse.over-limit-budget-is-exact-zero");
    Expect(StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, BrushSettings{0, 1, 0, 0, 0, 1},
                                    BrushKind::Paint) == 0,
           "refuse.invalid-settings-is-exact-zero");
    auto masked = BlankLayer(L"mask Heal refusal", 8, 8);
    LayerMask layerMask;
    layerMask.asset.image = SmallGray();
    masked.mask = std::move(layerMask);
    CanvasDocument maskSource = DocumentWith(masked);
    Expect(StrokeOpeningMemoryBytes(maskSource, maskSource.layers.front(), true, 64, kSettings, BrushKind::Heal) == 0,
           "refuse.mask-Heal-is-exact-zero");
    auto impossible = BlankLayer(L"inverse dimension guard", 8, 8);
    impossible.transform.size.x = static_cast<double>(BrushStroke::kMaxGridSide) + 1.0;
    CanvasDocument unsupported = DocumentWith(impossible);
    Expect(StrokeOpeningMemoryBytes(unsupported, unsupported.layers.front(), false, 64, kSettings, BrushKind::Paint) ==
               0,
           "refuse.unsupported-finite-grid-is-exact-zero");
}

struct OpeningTransferState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    DocumentSession* session = nullptr;
    std::optional<StrokeOpeningInput> input;
    std::optional<StrokeOpeningResult> output;
    SharedRetainedMemoryLease lease;
    std::int32_t transferResult = -1;
    bool refusalPreserved = false;
    bool preparedRetained = false;
    bool duplicateRefused = false;
    bool applied = false;
    bool lateRefused = false;
};

class OpeningTransferJob final : public Job
{
  public:
    explicit OpeningTransferJob(OpeningTransferState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        _state.output.emplace(DocumentSession::PrepareStrokeOpeningPublication(std::move(_state.input.value()),
                                                                               context.cancellation, {}));
        return _state.output->status == StrokeOpeningStatus::Ready ? JobResult{} : JobResult{JobStatus::Failed, -1};
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status != JobStatus::Finished || !_state.output || !_state.output->prepared)
            return;
        _state.transferResult = _state.scheduler->RetainMemory(_state.ticket, _state.lease);
        if (_state.transferResult != 0)
            return;
        const std::uint64_t exactBytes = _state.lease.Bytes();
        const auto* const layer = _state.session->ActiveLayer();
        if (layer)
        {
            BrushStroke ordinary(*layer, false, kSettings, 8, 8, &CpuBrushCoverage::Shared());
            const bool refused = !ordinary.RetainOpeningMemory(std::move(_state.lease));
            _state.refusalPreserved = refused && _state.lease && _state.lease.Bytes() == exactBytes;
        }
        auto& prepared = _state.output->prepared.value();
        _state.preparedRetained = prepared.RetainMemory(std::move(_state.lease));
        SharedRetainedMemoryLease duplicate;
        _state.duplicateRefused = !prepared.RetainMemory(std::move(duplicate));
        _state.applied = _state.session->ApplyPreparedStrokeOpeningIfCurrent(prepared);
        SharedRetainedMemoryLease late;
        _state.lateRefused = !prepared.RetainMemory(std::move(late));
    }

  private:
    OpeningTransferState& _state;
};

void SchedulerReservationMovesIntoPreparedOpening()
{
    auto layer = BlankLayer(L"lease source", 8, 8);
    layer.asset = RasterAsset{SmallBgra(), {}, layer.name};
    CanvasDocument document = DocumentWith(layer);
    const Guid active = document.layers.front().id;
    const auto bytes =
        StrokeOpeningMemoryBytes(document, document.layers.front(), false, 64, kSettings, BrushKind::Paint);
    DocumentSession session;
    session.ReplaceDocument(std::move(document), active);
    session.SetBrushEngine(&CpuBrushCoverage::Shared());
    StrokeOpeningOptions options;
    options.settings = kSettings;
    auto input = session.PrepareStrokeOpeningInput(std::move(options));
    JobScheduler scheduler;
    OpeningTransferState state;
    state.scheduler = &scheduler;
    state.session = &session;
    if (scheduler.Initialize(1) != 0 || !input || bytes == 0)
    {
        Expect(false, "lease.scheduler-and-opening-input-initialize");
        scheduler.Close();
        return;
    }
    state.input.emplace(std::move(input.value()));
    std::unique_ptr<Job> job = std::make_unique<OpeningTransferJob>(state);
    JobTicket ticket;
    const auto submitted = scheduler.Submit(job, {JobClass::Stroke, 1, bytes, false, false}, ticket);
    state.ticket = ticket;
    const auto settled = submitted == 0 ? SettleJobs(scheduler, 10'000) : SettleResult::Failed;
    Expect(submitted == 0 && settled == SettleResult::Settled && state.output &&
               state.output->status == StrokeOpeningStatus::Ready && state.transferResult == 0 &&
               state.refusalPreserved && state.preparedRetained && state.applied && state.duplicateRefused &&
               state.lateRefused,
           "lease.original-callback-reservation-binds-prepared-once-before-publication");
    Expect(session.PendingBrush() != nullptr && scheduler.ReservedMemoryBytes() == bytes,
           "lease.applied-live-stroke-keeps-entire-original-reservation");
    session.CancelBrush();
    state.output.reset();
    static_cast<void>(DrainCompletions(scheduler));
    Expect(session.PendingBrush() == nullptr && scheduler.ReservedMemoryBytes() == 0,
           "lease.last-stroke-owner-release-acknowledges-original-charge");
    scheduler.Close();
}
struct DeferredCapture final
{
    DeferredReleaseNode* node = nullptr;
    unsigned int calls = 0;
    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& capture = *static_cast<DeferredCapture*>(context);
        ++capture.calls;
        capture.node = &node;
    }
};

void OwnerFinalDeferredRetirement()
{
    auto layer = BlankLayer(L"deferred draft", 8, 8);
    std::shared_ptr<BrushStroke> stroke;
    std::thread producer([&] { stroke = BrushStroke::MakeForWork(layer, false, kSettings, 8, 8); });
    producer.join();
    const bool made = stroke && stroke->IsValid();
    const auto bound = made ? stroke->RetirementMemoryBytes() : 0;
    const auto weak = std::weak_ptr<BrushStroke>{stroke};
    DeferredCapture capture;
    const bool installed = InstallDeferredReleaseSink(&DeferredCapture::Enqueue, &capture);
    stroke.reset();
    const bool cleared = installed && ClearDeferredReleaseSink(&capture);
    bool destroyedOnWorker = false;
    if (capture.node)
    {
        DeferredReleaseNode* const node = capture.node;
        const bool nodeCoversBound = node->bytes >= bound;
        capture.node = nullptr;
        std::thread consumer(
            [&destroyedOnWorker, node]
            {
                destroyedOnWorker = true;
                node->destroy(node);
            });
        consumer.join();
        Expect(made && bound != 0 && cleared && weak.expired() && capture.calls == 1 && !capture.node &&
                   nodeCoversBound && destroyedOnWorker,
               "deferred.owner-final-release-queues-whole-declared-node-for-worker-destruction");
    }
    else
        Expect(false, "deferred.owner-final-release-queues-whole-declared-node-for-worker-destruction");
}
int RunCloneOpeningMemoryTests()
{
    int failures = 0;
    int facts = 0;
    const auto expect = [&](bool condition, const char* fact) noexcept
    {
        ++facts;
        std::printf("%s clone-opening-memory.%s\n", condition ? "PASS" : "FAIL", fact);
        failures += condition ? 0 : 1;
    };
    auto layer = BlankLayer(L"Clone opening memory", 8, 8);
    layer.asset = RasterAsset{SmallBgra(), {}, layer.name};
    CanvasDocument source = DocumentWith(layer, 8, 8);
    const Raster sample = SmallBgra(source.width, source.height);
    const auto sampleBytes = sample.RetirementMemoryBytes();
    const auto paint = StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Paint);
    const auto clone =
        StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Clone, &sample);
    expect(sample.HasStorage() && sample.Format() == RasterFormat::Bgra8888 && sampleBytes != 0 && paint != 0 &&
               clone > paint && clone - paint == sampleBytes,
           "valid-clone-bound-is-paint-bound-plus-exact-cached-sample-retention");
    expect(StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Clone) == 0,
           "null-clone-sample-is-exact-zero");
    const Raster gray = SmallGray(source.width, source.height);
    expect(gray.HasStorage() && gray.Format() == RasterFormat::Gray8 &&
               StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Clone, &gray) ==
                   0,
           "gray-clone-sample-is-exact-zero");
    const Raster wrongWidth = SmallBgra(source.width - 1, source.height);
    const Raster wrongHeight = SmallBgra(source.width, source.height - 1);
    expect(wrongWidth.HasStorage() && StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings,
                                                               BrushKind::Clone, &wrongWidth) == 0,
           "wrong-width-clone-sample-is-exact-zero");
    expect(wrongHeight.HasStorage() && StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings,
                                                                BrushKind::Clone, &wrongHeight) == 0,
           "wrong-height-clone-sample-is-exact-zero");
    const Raster empty;
    expect(!empty.HasStorage() && StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings,
                                                           BrushKind::Clone, &empty) == 0,
           "empty-clone-sample-is-exact-zero");
    auto maskedLayer = BlankLayer(L"Clone mask opening memory", 8, 8);
    maskedLayer.asset = RasterAsset{SmallBgra(), {}, maskedLayer.name};
    LayerMask mask;
    mask.asset = RasterAsset{SmallGray(), {}, L"Clone mask"};
    mask.enabled = true;
    maskedLayer.mask = std::move(mask);
    CanvasDocument maskedSource = DocumentWith(std::move(maskedLayer), 8, 8);
    expect(StrokeOpeningMemoryBytes(maskedSource, maskedSource.layers.front(), true, 64, kSettings, BrushKind::Clone,
                                    &sample) == 0,
           "clone-mask-target-is-exact-zero");
    expect(StrokeOpeningMemoryBytes(source, source.layers.front(), false, 64, kSettings, BrushKind::Paint, &sample) ==
               0,
           "non-clone-kind-with-supplied-clone-sample-is-exact-zero");
    std::printf("%s clone-opening-memory.summary facts=%d failures=%d\n", failures == 0 ? "PASS" : "FAIL", facts,
                failures);
    return failures;
}
} // namespace

int RunStrokeOpeningMemoryTests()
{
    gFailures = 0;
    LiteralGridReferences();
    ActualOwnerLowerBounds();
    SparseDeclarationAndRefusals();
    OwnerFinalDeferredRetirement();
    SchedulerReservationMovesIntoPreparedOpening();
    std::printf("%s stroke-opening-memory.summary facts=30 failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures + RunCloneOpeningMemoryTests();
}

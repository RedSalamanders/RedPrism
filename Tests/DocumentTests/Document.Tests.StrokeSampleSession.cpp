#include "Document.Tests.StrokeSampleSession.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "StrokeSampleWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s strokesample-session.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr int kSide = 32;
constexpr std::array<std::uint8_t, 4> kTopBgra{31, 67, 103, 255};
constexpr std::array<std::uint8_t, 4> kBottomBgra{151, 89, 43, 255};
constexpr Vec2 kOpeningPoint{3.5, 4.5};
constexpr Vec2 kPriorPoint{10.5, 8.5};
constexpr Vec2 kAcceptedPoint{22.5, 17.5};
constexpr BrushSettings kSettings{2000, 1, 1, 0, 0, 1};

Raster SolidBgra(const std::array<std::uint8_t, 4>& color)
{
    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += color.size())
        for (std::size_t channel = 0; channel < color.size(); ++channel)
            pixels[i + channel] = color[channel];
    return Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster SolidGray(std::uint8_t value)
{
    std::array<std::uint8_t, kSide * kSide> pixels{};
    pixels.fill(value);
    return Raster::Make(kSide, kSide, RasterFormat::Gray8, pixels.data(), pixels.size());
}

CanvasDocument MakeDocument(const Raster& top, const Raster& bottom, const Raster& mask = {})
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;

    ImageLayer topLayer = ImageLayer::Blank(L"Stroke sample top", Vec2{kSide, kSide});
    topLayer.asset = RasterAsset{top, Raster::MakeThumbnail(top), topLayer.name};
    if (mask.HasStorage())
    {
        LayerMask layerMask;
        layerMask.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Stroke sample mask"};
        topLayer.mask = std::move(layerMask);
    }
    fixture.layers.push_back(std::move(topLayer));

    ImageLayer bottomLayer = ImageLayer::Blank(L"Stroke sample bottom", Vec2{kSide, kSide});
    bottomLayer.asset = RasterAsset{bottom, Raster::MakeThumbnail(bottom), bottomLayer.name};
    fixture.layers.push_back(std::move(bottomLayer));
    return std::move(fixture).Seal();
}

struct Fixture final
{
    Raster top = SolidBgra(kTopBgra);
    Raster bottom = SolidBgra(kBottomBgra);
    Raster mask = SolidGray(128);
    DocumentSession session;
    Guid topID;
    Guid bottomID;

    explicit Fixture(bool selectMask = false)
    {
        CanvasDocument document = MakeDocument(top, bottom, mask);
        topID = document.layers[0].id;
        bottomID = document.layers[1].id;
        session.ReplaceDocument(std::move(document), topID);
        session.SetBrushEngine(&CpuBrushCoverage::Shared());
        session.brushSettings = kSettings;
        session.SelectLayerTarget(topID, selectMask);
    }

    [[nodiscard]] bool BeginSampleStroke()
    {
        if (!session.BeginBrush(kOpeningPoint, BrushKind::Paint))
            return false;
        session.ContinueBrush(kPriorPoint);
        return session.PendingBrush() != nullptr && session.PendingBrushOpening().has_value();
    }
};

struct OwnerState final
{
    std::optional<CanvasDocument> document;
    Guid historyRevision;
    int undoCount = 0;
    bool canUndo = false;
    bool canRedo = false;
    std::optional<Guid> active;
    bool maskSelected = false;
    std::optional<std::uint64_t> opening;
    const BrushStroke* pending = nullptr;
    std::uint64_t generation = 0;

    explicit OwnerState(const DocumentSession& session)
        : document(session.Document()), historyRevision(session.history.CurrentRevision()),
          undoCount(session.history.UndoCount()), canUndo(session.history.CanUndo()),
          canRedo(session.history.CanRedo()), active(session.ActiveLayerID()), maskSelected(session.IsMaskSelected()),
          opening(session.PendingBrushOpening()), pending(session.PendingBrush()), generation(session.Generation())
    {
    }
};

bool SameOwner(const DocumentSession& session, const OwnerState& state) noexcept
{
    const auto document = session.Document();
    return document == state.document && session.history.CurrentRevision() == state.historyRevision &&
           session.history.UndoCount() == state.undoCount && session.history.CanUndo() == state.canUndo &&
           session.history.CanRedo() == state.canRedo && session.ActiveLayerID() == state.active &&
           session.IsMaskSelected() == state.maskSelected && session.PendingBrushOpening() == state.opening &&
           session.PendingBrush() == state.pending && session.Generation() == state.generation;
}

bool ReadPixels(const Raster& raster, std::vector<std::uint8_t>& pixels)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != kSide ||
        raster.Height() != kSide)
        return false;
    pixels.resize(static_cast<std::size_t>(kSide * kSide * 4));
    raster.ReadRegion(0, 0, kSide, kSide, pixels.data(), kSide * 4, 0);
    return true;
}

bool IsLiteralBgra(const Raster& raster, const std::array<std::uint8_t, 4>& expected)
{
    std::vector<std::uint8_t> pixels;
    if (!ReadPixels(raster, pixels))
        return false;
    for (std::size_t i = 0; i < pixels.size(); ++i)
        if (pixels[i] != expected[i % expected.size()])
            return false;
    return true;
}

bool IsLiteralGray(const Raster& raster, std::uint8_t expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || raster.Width() != kSide ||
        raster.Height() != kSide)
        return false;
    std::array<std::uint8_t, kSide * kSide> pixels{};
    raster.ReadRegion(0, 0, kSide, kSide, pixels.data(), kSide, 0);
    for (const std::uint8_t pixel : pixels)
        if (pixel != expected)
            return false;
    return true;
}

const Raster* ActiveImage(const DocumentSession& session) noexcept
{
    const ImageLayer* layer = session.ActiveLayer();
    return layer && layer->asset ? &layer->asset.value().image : nullptr;
}

struct SampleWorkerResult final
{
    bool prepared = false;
    bool composed = false;
    bool joined = false;
};

SampleWorkerResult PrepareAndComposeOnWorker(StrokeSampleInput& input)
{
    SampleWorkerResult result;
    std::thread worker(
        [&]
        {
            // This thread receives only the detached draft. It never calls DocumentSession.
            auto coverage = input.DraftForWork().PrepareSampleCoverage();
            result.prepared = coverage.has_value();
            result.composed = coverage && input.DraftForWork().ComposeSampleCoverage(coverage.value());
        });
    worker.join();
    result.joined = true;
    return result;
}

void CaptureRefusalsAndOwnerMarker()
{
    Fixture fixture;
    const auto restoreAuthority = fixture.session.CaptureHistoryRestoreAuthority();
    Expect(fixture.session.CanApplyHistoryRestore(restoreAuthority), "capture.restore-authority-valid-before-stroke");
    Expect(fixture.BeginSampleStroke(), "capture.synchronous-cpu-compatibility-stroke-opened");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    const auto* live = fixture.session.PendingBrush();
    const auto historyRevision = fixture.session.history.CurrentRevision();
    const int undoCount = fixture.session.history.UndoCount();
    const auto shiftBefore = fixture.session.ShiftLineStart();

    BrushCoverageEngine* nonempty = &CpuBrushCoverage::Shared();
    auto refusedNonempty = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, nonempty);
    Expect(!refusedNonempty && nonempty == &CpuBrushCoverage::Shared(),
           "capture.nonempty-owner-engine-is-refused-and-preserved");
    BrushCoverageEngine* empty = nullptr;
    auto refusedWrongOpening = fixture.session.TakeStrokeSampleInput(opening.value() + 1, kAcceptedPoint, empty);
    Expect(!refusedWrongOpening && empty == nullptr, "capture.wrong-opening-is-refused-with-null-output");
    Expect(fixture.session.PendingBrush() == live && fixture.session.PendingBrushOpening() == opening,
           "capture.refusals-preserve-live-draft-and-exact-opening");

    auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, empty);
    Expect(input.has_value() && empty == &CpuBrushCoverage::Shared(), "capture.exact-opening-detaches-original-engine");
    if (!input)
        return;
    Expect(fixture.session.PendingBrush() == nullptr && fixture.session.PendingBrushOpening() == opening,
           "capture.pending-brush-null-with-scalar-opening-retained");
    Expect(!fixture.session.CanApplyHistoryRestore(restoreAuthority),
           "capture.history-restore-authority-invalid-during-update");
    Expect(fixture.session.ShiftLineStart() == shiftBefore, "capture.shift-endpoint-does-not-advance-before-apply");

    BrushCoverageEngine* repeatedOutput = nullptr;
    auto repeated = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, repeatedOutput);
    Expect(!repeated && repeatedOutput == nullptr, "capture.repeated-take-refused-without-replacing-output");
    Expect(fixture.session.history.CurrentRevision() == historyRevision &&
               fixture.session.history.UndoCount() == undoCount,
           "capture.detachment-adds-no-history-entry");

    auto moved = std::move(input.value());
    Expect(!fixture.session.IsStrokeSampleCurrent(input.value()), "capture.moved-from-input-is-invalid");
    Expect(fixture.session.IsStrokeSampleCurrent(moved), "capture.moved-to-input-retains-current-authority");
    Expect(fixture.session.DiscardStrokeSampleIfCurrent(opening.value()),
           "capture.cleanup-discards-exact-detached-opening");
}

void SuccessfulWorkerApplyMatchesSynchronousReference()
{
    Fixture fixture;
    Fixture direct;
    Expect(fixture.BeginSampleStroke() && direct.BeginSampleStroke(), "success.actual-and-reference-strokes-open");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    const auto beforeShift = fixture.session.ShiftLineStart();
    const std::weak_ptr<BrushStroke> originalOwner = fixture.session.WeakPendingBrushForTest();
    const auto originalDocument = fixture.session.Document();
    const auto directOriginalDocument = direct.session.Document();
    const auto originalHistory = fixture.session.history.CurrentRevision();
    const int originalUndoCount = fixture.session.history.UndoCount();

    BrushCoverageEngine* engine = nullptr;
    auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, engine);
    Expect(input.has_value() && engine == &CpuBrushCoverage::Shared(), "success.exact-sample-input-captured");
    if (!input)
        return;
    const SampleWorkerResult work = PrepareAndComposeOnWorker(input.value());
    Expect(work.joined && work.prepared && work.composed, "success.real-worker-prepares-and-composes-before-join");
    Expect(fixture.session.PendingBrush() == nullptr && fixture.session.PendingBrushOpening() == opening &&
               fixture.session.ShiftLineStart() == beforeShift &&
               fixture.session.history.CurrentRevision() == originalHistory &&
               fixture.session.history.UndoCount() == originalUndoCount,
           "success.worker-does-not-mutate-session-root-history-or-shift-endpoint");
    Expect(fixture.session.IsStrokeSampleCurrent(input.value()), "success.owner-sees-current-input-after-join");
    Expect(fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine),
           "success.owner-applies-original-engine-after-join");
    Expect(fixture.session.PendingBrush() != nullptr && originalOwner.lock().get() == fixture.session.PendingBrush(),
           "success.original-shared-draft-owner-is-restored");
    Expect(fixture.session.PendingBrushOpening() == opening &&
               fixture.session.history.CurrentRevision() == originalHistory &&
               fixture.session.history.UndoCount() == originalUndoCount,
           "success.apply-preserves-opening-and-adds-no-history-entry");
    Expect(fixture.session.ShiftLineStart() == std::optional<Vec2>(kAcceptedPoint),
           "success.shift-endpoint-advances-only-after-apply");
    Expect(!fixture.session.IsStrokeSampleCurrent(input.value()) &&
               !fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine),
           "success.applied-input-is-single-use");

    direct.session.ContinueBrush(kAcceptedPoint);
    fixture.session.FinishBrush();
    direct.session.FinishBrush();
    const Raster* actual = ActiveImage(fixture.session);
    const Raster* expected = ActiveImage(direct.session);
    std::vector<std::uint8_t> actualPixels;
    std::vector<std::uint8_t> expectedPixels;
    Expect(actual && expected && ReadPixels(*actual, actualPixels) && ReadPixels(*expected, expectedPixels) &&
               actualPixels == expectedPixels,
           "success.final-bgra-pixels-equal-direct-cpu-continuebrush-reference");
    Expect(fixture.session.history.UndoCount() == originalUndoCount + 1 &&
               direct.session.history.UndoCount() == originalUndoCount + 1,
           "success.each-completed-stroke-adds-one-entry-after-sample-application");
    Expect(originalDocument && directOriginalDocument &&
               fixture.top.Identity() == originalDocument->layers[0].asset.value().image.Identity() &&
               direct.top.Identity() == directOriginalDocument->layers[0].asset.value().image.Identity() &&
               IsLiteralBgra(fixture.top, kTopBgra) && IsLiteralBgra(direct.top, kTopBgra),
           "success.original-source-identities-and-all-literal-bgra-pixels-remain-stable-on-both-sides");
}

void SuccessfulMaskWorkerApplyPreservesGrayChannelContract()
{
    Fixture fixture(true);
    Fixture direct(true);
    fixture.session.maskPaintWhite = false;
    direct.session.maskPaintWhite = false;
    Expect(fixture.BeginSampleStroke() && direct.BeginSampleStroke(),
           "mask.actual-and-reference-strokes-open-on-gray-target");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    const auto originalDocument = fixture.session.Document();
    const auto directOriginalDocument = direct.session.Document();
    const auto originalMaskIdentity = fixture.session.ActiveLayer()->mask->asset.image.Identity();
    const auto directMaskIdentity = direct.session.ActiveLayer()->mask->asset.image.Identity();
    BrushCoverageEngine* engine = nullptr;
    auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, engine);
    Expect(input.has_value(), "mask.exact-sample-input-captured");
    if (!input)
        return;
    const SampleWorkerResult work = PrepareAndComposeOnWorker(input.value());
    Expect(work.joined && work.prepared && work.composed, "mask.worker-prepares-composes-and-joins");
    Expect(fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine), "mask.owner-applies-after-join");
    direct.session.ContinueBrush(kAcceptedPoint);
    fixture.session.FinishBrush();
    direct.session.FinishBrush();
    const ImageLayer* actualLayer = fixture.session.ActiveLayer();
    const ImageLayer* expectedLayer = direct.session.ActiveLayer();
    Expect(actualLayer && actualLayer->mask && expectedLayer && expectedLayer->mask &&
               actualLayer->mask->asset.image.Format() == RasterFormat::Gray8 &&
               actualLayer->mask->asset.image.Width() == kSide && actualLayer->mask->asset.image.Height() == kSide &&
               expectedLayer->mask->asset.image.Format() == RasterFormat::Gray8 &&
               expectedLayer->mask->asset.image.Width() == kSide &&
               expectedLayer->mask->asset.image.Height() == kSide && IsLiteralGray(actualLayer->mask->asset.image, 0) &&
               IsLiteralGray(expectedLayer->mask->asset.image, 0),
           "mask.final-gray8-dimensions-and-literal-zero-pixels-match-direct-cpu-reference");
    Expect(originalDocument && directOriginalDocument && fixture.mask.Identity() == originalMaskIdentity &&
               direct.mask.Identity() == directMaskIdentity && IsLiteralGray(fixture.mask, 128) &&
               IsLiteralGray(direct.mask, 128) &&
               originalDocument->layers[0].mask->asset.image.Identity() == originalMaskIdentity &&
               directOriginalDocument->layers[0].mask->asset.image.Identity() == directMaskIdentity,
           "mask.original-gray128-source-identities-and-literal-pixels-remain-stable-on-both-sides");
}

enum class StaleMutation : std::uint8_t
{
    CancelAndReplaceWithYoungerStroke,
    DiscardAndReplaceWithYoungerStroke,
    ReplaceDocument,
    SelectNewTarget,
    SelectMaskTarget,
    ChangeSourceAndHistory,
    ResetHistoryAuthority,
    UndoWhileUpdating
};

struct StaleCase final
{
    StaleMutation mutation;
    const char* name;
};

constexpr std::array<StaleCase, 8> kStaleCases{{
    {StaleMutation::CancelAndReplaceWithYoungerStroke, "cancel-younger-stroke"},
    {StaleMutation::DiscardAndReplaceWithYoungerStroke, "discard-younger-stroke"},
    {StaleMutation::ReplaceDocument, "document-replacement"},
    {StaleMutation::SelectNewTarget, "new-layer-target"},
    {StaleMutation::SelectMaskTarget, "mask-target"},
    {StaleMutation::ChangeSourceAndHistory, "source-record-and-history"},
    {StaleMutation::ResetHistoryAuthority, "history-authority-reset"},
    {StaleMutation::UndoWhileUpdating, "undo-cancels-update-before-restore"},
}};

void StaleApplyPreservesNewerOwnerState()
{
    for (const StaleCase& test : kStaleCases)
    {
        Fixture fixture;
        if (test.mutation == StaleMutation::UndoWhileUpdating)
            fixture.session.SetOpacity(0.5);
        Expect(fixture.BeginSampleStroke(), test.name);
        const auto opening = fixture.session.PendingBrushOpening();
        if (!opening)
            continue;
        BrushCoverageEngine* engine = nullptr;
        auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, engine);
        Expect(input.has_value(), test.name);
        if (!input)
            continue;
        const SampleWorkerResult work = PrepareAndComposeOnWorker(input.value());
        Expect(work.joined && work.prepared && work.composed, test.name);

        switch (test.mutation)
        {
        case StaleMutation::CancelAndReplaceWithYoungerStroke:
            fixture.session.CancelBrush();
            Expect(fixture.BeginSampleStroke(), test.name);
            break;
        case StaleMutation::DiscardAndReplaceWithYoungerStroke:
            Expect(fixture.session.DiscardStrokeSampleIfCurrent(opening.value()), test.name);
            Expect(fixture.BeginSampleStroke(), test.name);
            break;
        case StaleMutation::ReplaceDocument:
        {
            CanvasDocument replacement = MakeDocument(fixture.bottom, fixture.top, fixture.mask);
            const Guid replacementID = replacement.layers[0].id;
            fixture.session.ReplaceDocument(std::move(replacement), replacementID);
            Expect(fixture.session.PendingBrush() == nullptr && !fixture.session.PendingBrushOpening() &&
                       fixture.session.ActiveLayerID() == std::optional<Guid>(replacementID),
                   test.name);
            break;
        }
        case StaleMutation::SelectNewTarget:
            fixture.session.SelectLayerTarget(fixture.bottomID, false);
            break;
        case StaleMutation::SelectMaskTarget:
            fixture.session.SelectLayerTarget(fixture.topID, true);
            break;
        case StaleMutation::ChangeSourceAndHistory:
            fixture.session.SetOpacity(0.5);
            break;
        case StaleMutation::ResetHistoryAuthority:
            fixture.session.history.Reset();
            break;
        case StaleMutation::UndoWhileUpdating:
            fixture.session.Undo();
            break;
        }

        const OwnerState newer(fixture.session);
        Expect(!fixture.session.IsStrokeSampleCurrent(input.value()), test.name);
        Expect(!fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine), test.name);
        Expect(SameOwner(fixture.session, newer), test.name);
        if (fixture.session.PendingBrush() != nullptr)
            fixture.session.CancelBrush();
        else
            static_cast<void>(fixture.session.DiscardStrokeSampleIfCurrent(opening.value()));
    }
}

void CancelledPreparedCoverageCannotAttach()
{
    Fixture fixture;
    Expect(fixture.BeginSampleStroke(), "cancel.begin");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    BrushCoverageEngine* engine = nullptr;
    auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, engine);
    Expect(input.has_value(), "cancel.capture");
    if (!input)
        return;

    CancelSource cancellation;
    bool prepared = false;
    bool composed = false;
    bool joined = false;
    std::thread worker(
        [&]
        {
            auto coverage = input->DraftForWork().PrepareSampleCoverage(cancellation.Token());
            prepared = coverage.has_value();
            if (coverage)
            {
                // Cancellation after private preparation leaves the detached draft unpublishable.
                static_cast<void>(cancellation.Cancel());
                composed = input->DraftForWork().ComposeSampleCoverage(coverage.value());
            }
        });
    worker.join();
    joined = true;
    Expect(joined && prepared && !composed, "cancel-prepared-private-coverage-is-cancelled-before-compose");
    Expect(!fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine),
           "cancel-owner-cannot-attach-partially-prepared-draft");
    Expect(fixture.session.PendingBrush() == nullptr && fixture.session.PendingBrushOpening() == opening,
           "cancel-failure-retains-only-exact-update-marker-until-discard");
    Expect(fixture.session.DiscardStrokeSampleIfCurrent(opening.value()), "cancel-owner-discards-exact-opening");
    Expect(!fixture.session.PendingBrushOpening().has_value() &&
               !fixture.session.ApplyStrokeSampleIfCurrent(input.value(), engine),
           "cancel-discard-invalidates-cancelled-input");
}

void WrongEngineCanRetryWithOriginalAfterJoin()
{
    Fixture fixture;
    Expect(fixture.BeginSampleStroke(), "engine.begin");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    BrushCoverageEngine* originalEngine = nullptr;
    auto input = fixture.session.TakeStrokeSampleInput(opening.value(), kAcceptedPoint, originalEngine);
    Expect(input.has_value() && originalEngine == &CpuBrushCoverage::Shared(), "engine.capture-original-cpu-engine");
    if (!input)
        return;
    const SampleWorkerResult work = PrepareAndComposeOnWorker(input.value());
    Expect(work.joined && work.prepared && work.composed, "engine.worker-join-before-owner-calls");
    Expect(!fixture.session.ApplyStrokeSampleIfCurrent(input.value(), nullptr), "engine.wrong-null-engine-refused");
    Expect(fixture.session.IsStrokeSampleCurrent(input.value()), "engine-wrong-engine-refusal-keeps-input-current");
    Expect(fixture.session.ApplyStrokeSampleIfCurrent(input.value(), originalEngine),
           "engine-original-engine-retry-succeeds");
    fixture.session.CancelBrush();
}

class OwnerObservedCpuEngine final : public BrushCoverageEngine
{
  public:
    const std::thread::id owner = std::this_thread::get_id();
    unsigned int begins = 0;
    unsigned int ends = 0;
    bool endOffOwner = false;
    bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept override
    {
        return CpuBrushCoverage::Shared().Render(tiles, settled, tail, params);
    }
    bool UsesCpuPermanent() const noexcept override
    {
        return true;
    }
    void BeginStroke() noexcept override
    {
        ++begins;
    }
    void EndStroke() noexcept override
    {
        ++ends;
        endOffOwner = endOffOwner || std::this_thread::get_id() != owner;
    }
};

void AppliedInputRetiresAfterOwnerCancellation()
{
    OwnerObservedCpuEngine engine;
    Fixture fixture;
    fixture.session.SetBrushEngine(&engine);
    Expect(fixture.BeginSampleStroke() && engine.begins == 1 && engine.ends == 0,
           "joined-retirement.real-engine-opens-on-owner");
    const auto opening = fixture.session.PendingBrushOpening();
    if (!opening)
        return;
    const auto weak = fixture.session.WeakPendingBrushForTest();
    BrushCoverageEngine* detached = nullptr;
    auto input = fixture.session.TakeStrokeSampleInput(*opening, kAcceptedPoint, detached);
    Expect(input && detached == &engine, "joined-retirement.original-engine-detaches");
    if (!input)
        return;
    const auto work = PrepareAndComposeOnWorker(*input);
    Expect(work.prepared && work.composed && work.joined && fixture.session.ApplyStrokeSampleIfCurrent(*input, &engine),
           "joined-retirement.actual-worker-joins-before-owner-apply");
    fixture.session.CancelBrush();
    Expect(weak.expired() && engine.begins == 1 && engine.ends == 1 && !engine.endOffOwner &&
               !fixture.session.PendingBrush() && fixture.session.history.UndoCount() == 0,
           "joined-retirement.owner-cancel-releases-last-engine-draft-before-input-worker");
    std::thread retirement([owned = std::move(input)]() mutable { owned.reset(); });
    retirement.join();
    Expect(weak.expired() && engine.begins == 1 && engine.ends == 1 && !engine.endOffOwner &&
               fixture.session.history.UndoCount() == 0,
           "joined-retirement.worker-retires-input-without-an-engine-end-or-history-write");
}

} // namespace

int RunStrokeSampleSessionTests()
{
    gFailures = 0;
    gAssertions = 0;
    static_assert(!std::is_copy_constructible_v<StrokeSampleInput>);
    static_assert(std::is_nothrow_move_constructible_v<StrokeSampleInput>);
    CaptureRefusalsAndOwnerMarker();
    SuccessfulWorkerApplyMatchesSynchronousReference();
    SuccessfulMaskWorkerApplyPreservesGrayChannelContract();
    StaleApplyPreservesNewerOwnerState();
    CancelledPreparedCoverageCannotAttach();
    WrongEngineCanRetryWithOriginalAfterJoin();
    std::printf("%s strokesample-session.summary assertions=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL",
                gAssertions, gFailures);
    const int originalFailures = gFailures;
    gFailures = 0;
    gAssertions = 0;
    AppliedInputRetiresAfterOwnerCancellation();
    std::printf("%s strokesample-joined-retirement.summary assertions=%d failures=%d\n",
                gFailures == 0 ? "PASS" : "FAIL", gAssertions, gFailures);
    return originalFailures + gFailures;
}

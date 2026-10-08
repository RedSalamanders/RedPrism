#include "Application.h"

#include "ChromeHost.h"
#include "DocumentDraft.h"
#include "GradientWorkTestGate.h"
#include "PixelMoveWorkTestGate.h"
#include "PreparedRasterPreview.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{
enum class GradientTarget
{
    Image,
    Blank,
    MaskUnplaced,
    MaskPlaced,
    MaskUniform
};

bool IsMask(GradientTarget target) noexcept
{
    return target == GradientTarget::MaskUnplaced || target == GradientTarget::MaskPlaced ||
           target == GradientTarget::MaskUniform;
}

std::optional<CanvasDocument> GradientFixture(GradientTarget target)
{
    CanvasDocument document;
    document.width = 4;
    document.height = 1;
    auto draft = CanvasDocumentDraft::Rebuild(document, 1);
    if (!draft)
        return std::nullopt;
    auto layer = ImageLayer::Blank(L"Raster preview", Vec2{4, 1});
    layer.transform.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 16> white{255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255};
    if (target != GradientTarget::Blank)
    {
        auto image = Raster::Make(4, 1, RasterFormat::Bgra8888, white.data(), white.size());
        if (!image.HasStorage())
            return std::nullopt;
        layer.asset = RasterAsset{std::move(image), {}, layer.name};
    }
    if (IsMask(target))
    {
        constexpr std::array<std::uint8_t, 2> samples{31, 219};
        constexpr std::uint8_t uniform = 128;
        const bool constant = target == GradientTarget::MaskUniform;
        auto mask = Raster::Make(constant ? 1 : 2, 1, RasterFormat::Gray8, constant ? &uniform : samples.data(),
                                 constant ? 1 : samples.size());
        if (!mask.HasStorage())
            return std::nullopt;
        layer.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Raster preview mask"}};
        if (target == GradientTarget::MaskPlaced)
        {
            layer.mask->placement = LayerTransform{Vec2{1, 0}, Vec2{2, 1}};
            layer.mask->placement->sampling = LayerSampling::Nearest;
        }
    }
    return draft->Append(std::move(layer)) ? draft->Seal() : std::nullopt;
}

std::array<std::uint8_t, 16> GradientLiteral(GradientTarget target, bool preview, bool committed = false)
{
    std::array<std::uint8_t, 16> pixels{};
    constexpr std::array<std::uint8_t, 4> ramp{0, 85, 170, 255};
    constexpr std::array<std::uint8_t, 4> unplaced{31, 31, 219, 219};
    // Original edge samples sum to 250 < 255: the placed mask hides outside.
    constexpr std::array<std::uint8_t, 4> placed{0, 31, 219, 0};
    // Preview keeps the original mask record. Commit publishes the eager new thumbnail;
    // its 0 + 255 edge reaches the 50 percent threshold and reveals outside.
    constexpr std::array<std::uint8_t, 4> placedRamp{0, 0, 255, 0};
    constexpr std::array<std::uint8_t, 4> placedCommit{255, 0, 255, 255};
    for (std::size_t pixel = 0; pixel < 4; ++pixel)
    {
        const auto alpha = static_cast<std::uint8_t>(
            !IsMask(target) ? (preview || target != GradientTarget::Blank ? 255 : 0)
            : preview ? (target == GradientTarget::MaskPlaced ? (committed ? placedCommit[pixel] : placedRamp[pixel])
                                                              : ramp[pixel])
            : target == GradientTarget::MaskPlaced  ? placed[pixel]
            : target == GradientTarget::MaskUniform ? 128
                                                    : unplaced[pixel]);
        const auto value = preview && !IsMask(target) ? ramp[pixel] : alpha;
        pixels[pixel * 4] = value;
        pixels[pixel * 4 + 1] = value;
        pixels[pixel * 4 + 2] = value;
        pixels[pixel * 4 + 3] = alpha;
    }
    return pixels;
}

bool MatchesLiteral(RecordedDisplayGraph* graph, int expectedWidth, std::span<const std::uint8_t> expected)
{
    if (!graph || !graph->CompositionForTestOwner())
        return false;
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(graph->CompositionForTestOwner()->Readback(pixels, width, height)) ||
        width != static_cast<UINT>(expectedWidth) || height != 1 || pixels.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
        if (pixels[index] != expected[index])
        {
            std::printf("INFO application-pending-raster.literal-byte=%zu expected=%u actual=%u\n", index,
                        expected[index], pixels[index]);
            return false;
        }
    return true;
}

// Three distinct tile windows. A second landing must restore the first landing's omitted tile from the source.
std::vector<std::uint8_t> MoveLiteral(bool duplicate, int offset)
{
    std::vector<std::uint8_t> pixels(520 * 4);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index + 2] = 255;
        pixels[index + 3] = 255;
    }
    const auto green = [&](int x)
    {
        pixels[static_cast<std::size_t>(x) * 4 + 1] = 255;
        pixels[static_cast<std::size_t>(x) * 4 + 2] = 0;
    };
    if (!offset || duplicate)
        green(1);
    else
        for (std::size_t channel = 0; channel < 4; ++channel)
            pixels[4 + channel] = 0;
    if (offset)
        green(1 + offset);
    return pixels;
}

std::optional<CanvasDocument> MoveFixture()
{
    CanvasDocument document;
    document.width = 520;
    document.height = 1;
    document.selection = DocumentSelection{false, {{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}};
    auto draft = CanvasDocumentDraft::Rebuild(document, 1);
    if (!draft)
        return std::nullopt;
    const auto pixels = MoveLiteral(false, 0);
    auto image = Raster::Make(520, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    auto layer = ImageLayer::Blank(L"Three tile move", Vec2{520, 1});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{std::move(image), {}, layer.name};
    return draft->Append(std::move(layer)) ? draft->Seal() : std::nullopt;
}
} // namespace

int Application::RunStandalonePendingRasterDisplaySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        std::printf("%s application-pending-raster.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    for (const auto target : {GradientTarget::Image, GradientTarget::Blank, GradientTarget::MaskUnplaced,
                              GradientTarget::MaskPlaced, GradientTarget::MaskUniform})
    {
        for (const bool commit : {false, true})
        {
            std::printf("INFO application-pending-raster.gradient target=%d commit=%d\n", static_cast<int>(target),
                        commit);
            std::unique_ptr<Application> owner{new (std::nothrow)
                                                   Application(instance, true, L"", 0, nullptr, nullptr)};
            if (!expect(owner != nullptr, "gradient-host-allocated"))
                return 5;
            const auto close = wil::scope_exit(
                [&]() noexcept
                {
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                });
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                            SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                            SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                        "gradient-actual-WARP-host-initializes"))
                return 5;
            auto fixture = GradientFixture(target);
            if (!expect(fixture.has_value(), "gradient-literal-fixture-sealed"))
                return 5;
            const auto active = fixture->layers[0].id;
            owner->_session.ReplaceDocument(std::move(fixture.value()), active);
            owner->_session.SelectLayerTarget(active, IsMask(target));
            owner->_session.SetForegroundColor(0xFF000000u);
            owner->_session.SetBackgroundColor(0xFFFFFFFFu);
            owner->_session.maskPaintWhite = false;
            owner->_session.gradientSettings.style = GradientStyle::ForegroundToBackground;
            owner->_session.gradientSettings.shape = GradientShape::Linear;
            owner->_session.gradientSettings.opacity = 1;
            owner->DocumentChanged();
            const auto settle = [&]() noexcept
            { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
            const auto original = GradientLiteral(target, false);
            const auto preview = GradientLiteral(target, true);
            const auto applied = GradientLiteral(target, true, true);
            if (!expect(settle() && MatchesLiteral(owner->_acknowledgedCommittedDisplay, 4, original),
                        "gradient-committed-original-literal"))
                return 5;
            const auto generation = owner->_session.Generation();
            const auto root = owner->_session.Document()->layers.Identity();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto undo = owner->_session.history.UndoCount();
            const auto legacyComposes = owner->_composition.CompositeCount();
            const auto start = target == GradientTarget::MaskPlaced ? Vec2{1.5, 0.5} : Vec2{0.5, 0.5};
            const auto end = target == GradientTarget::MaskPlaced ? Vec2{2.5, 0.5} : Vec2{3.5, 0.5};
            owner->BeginGradientDrag(owner->_viewport.ViewFromDocument(start, owner->DocumentSize()));
            expect(owner->_session.PendingGradient() && !owner->_session.PendingGradient()->HasLine() &&
                       !owner->_pendingDisplay.GraphForFrameOwner() && !owner->_composition.HasStroke(),
                   "gradient-zero-line-has-no-GPU-or-owner-preview");
            owner->DragGradient(owner->_viewport.ViewFromDocument(end, owner->DocumentSize()), 0);
            owner->EndGradientDrag();
            if (!expect(settle(), "gradient-original-worker-and-GPU-acks-settle"))
                return 5;
            const auto* pending = owner->_session.PendingGradient();
            PendingDisplaySource descriptor;
            expect(pending && pending->display && pending->display->memory && pending->display->memory->IsBound() &&
                       owner->_session.DescribePendingDisplaySource(descriptor) &&
                       descriptor.kind == PendingDisplaySource::Kind::Gradient && descriptor.rasterPreview &&
                       *descriptor.rasterPreview == pending->display && owner->_pendingDisplay.GraphForFrameOwner() &&
                       MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 4, preview),
                   "gradient-bound-original-metadata-and-independent-GPU-literal");
            expect(owner->_session.Generation() == generation &&
                       owner->_session.Document()->layers.Identity() == root &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == undo && !owner->_composition.HasStroke(),
                   "gradient-preview-keeps-source-history-and-empty-owner-stroke");
            const auto presents = owner->_renderer.PresentCount();
            owner->RenderFrame();
            expect(owner->_renderer.PresentCount() == presents + 1 &&
                       owner->_composition.CompositeCount() == legacyComposes &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()),
                   "gradient-actual-renderer-blits-without-owner-composition");
            owner->BeginGradientDrag(owner->_viewport.ViewFromDocument(end, owner->DocumentSize()));
            owner->DragGradient(owner->_viewport.ViewFromDocument(start, owner->DocumentSize()), 0);
            expect(owner->_session.PendingGradient() && !owner->_session.PendingGradient()->HasLine() &&
                       !owner->_session.PendingGradient()->display && !owner->_pendingDisplay.GraphForFrameOwner(),
                   "gradient-zero-redrag-drops-metadata-and-excludes-old-frame");
            owner->DragGradient(owner->_viewport.ViewFromDocument(end, owner->DocumentSize()), 0);
            owner->EndGradientDrag();
            expect(!owner->_pendingDisplay.GraphForFrameOwner(),
                   "gradient-resumed-line-cannot-resurrect-dropped-old-publication");
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 4, preview),
                        "gradient-redrag-publishes-fresh-original-worker-and-GPU-result"))
                return 5;
            const auto opening = owner->_session.PendingGradient() ? owner->_session.PendingGradient()->opening : 0;
            ContinuationReceipt receipt;
            if (commit)
            {
                receipt = owner->QueueHostInput(HostGradientApplyInput{opening});
                expect(receipt.Read().outcome == HostOutcome::Pending && owner->_session.Generation() == generation &&
                           owner->_session.history.CurrentRevision() == revision,
                       "gradient-accepted-Apply-keeps-original-state-until-ack");
            }
            else
                owner->CancelGradient();
            expect(settle() && !owner->_session.PendingGradient() && !owner->_pendingDisplay.GraphForFrameOwner() &&
                       !owner->_pendingDisplay.HasPending() &&
                       owner->_session.history.UndoCount() == undo + (commit ? 1 : 0) &&
                       (!commit || receipt.Read().outcome == HostOutcome::Succeeded) &&
                       MatchesLiteral(owner->_acknowledgedCommittedDisplay, 4, commit ? applied : original),
                   commit ? "gradient-Apply-publishes-one-history-entry-and-exact-literal"
                          : "gradient-Cancel-restores-original-literal-without-history");
            if (commit)
            {
                const auto undone = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                expect(settle() && undone.Read().outcome == HostOutcome::Succeeded &&
                           MatchesLiteral(owner->_acknowledgedCommittedDisplay, 4, original),
                       "gradient-Undo-restores-exact-original-literal");
            }
            owner->Shutdown();
            expect(owner->_shutdownComplete && owner->_jobs.ReservedMemoryBytes() == 0 &&
                       owner->_jobs.ReleasingMemoryBytes() == 0 && !owner->_pendingDisplayInitialized,
                   "gradient-joined-shutdown-retires-CPU-and-GPU-charges");
        }
    }
    for (const bool duplicate : {false, true})
    {
        for (const bool commit : {false, true})
        {
            std::printf("INFO application-pending-raster.move duplicate=%d commit=%d\n", duplicate, commit);
            std::unique_ptr<Application> owner{new (std::nothrow)
                                                   Application(instance, true, L"", 0, nullptr, nullptr)};
            if (!expect(owner != nullptr, "move-host-allocated"))
                return 5;
            const auto close = wil::scope_exit(
                [&]() noexcept
                {
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                });
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                            SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                            SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                        "move-actual-WARP-host-initializes"))
                return 5;
            auto fixture = MoveFixture();
            if (!expect(fixture.has_value(), "move-independent-three-tile-fixture-sealed"))
                return 5;
            const auto active = fixture->layers[0].id;
            owner->_session.ReplaceDocument(std::move(fixture.value()), active);
            owner->DocumentChanged();
            const auto settle = [&]() noexcept
            { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
            const auto original = MoveLiteral(false, 0);
            if (!expect(settle() && MatchesLiteral(owner->_acknowledgedCommittedDisplay, 520, original),
                        "move-committed-original-three-tile-literal"))
                return 5;
            const auto generation = owner->_session.Generation();
            const auto root = owner->_session.Document()->layers.Identity();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto undo = owner->_session.history.UndoCount();
            const auto legacyComposes = owner->_composition.CompositeCount();
            owner->SyncChrome();
            owner->RenderFrame();
            const auto originalLoops = owner->_overlaySelection.value().loops.Identity();
            const auto originalOutlineCounters = owner->_chrome->SyncCountersForTest();
            const auto originalPathCount = owner->_chrome->SelectionResourcesStateForTest().pathsBuilt;
            const auto outlineMatches = [&](int offset) noexcept
            {
                owner->SyncChrome();
                const auto* loops = owner->_chrome->SelectionOutlineForTest();
                if (!loops || loops->size() != 1 || loops->front().size() != 4)
                    return false;
                constexpr std::array<Vec2, 4> literal{{{1, 0}, {2, 0}, {2, 1}, {1, 1}}};
                const auto mapping = owner->_chrome->SelectionMappingForTest();
                const double dipPerPixel = 96.0 / owner->ChromeDpi();
                const auto& viewport = owner->_viewport;
                // Independent canvas-center placement; do not use ViewFromDocument as the oracle.
                const Vec2 base{viewport.viewSize.x / 2 - 520 * viewport.zoom / 2 + viewport.pan.x,
                                viewport.viewSize.y / 2 - viewport.zoom / 2 + viewport.pan.y};
                for (std::size_t index = 0; index < literal.size(); ++index)
                {
                    const auto p = loops->front()[index];
                    const Vec2 actualPoint{p.x * mapping._11 + p.y * mapping._21 + mapping._31,
                                           p.x * mapping._12 + p.y * mapping._22 + mapping._32};
                    const Vec2 expectedPoint{(base.x + (literal[index].x + offset) * viewport.zoom) * dipPerPixel,
                                             (base.y + literal[index].y * viewport.zoom) * dipPerPixel};
                    if (std::abs(actualPoint.x - expectedPoint.x) > 0.001 ||
                        std::abs(actualPoint.y - expectedPoint.y) > 0.001)
                        return false;
                }
                return owner->_overlaySelection.value().loops.Identity() == originalLoops &&
                       owner->_chrome->SyncCountersForTest().selectionOutlines ==
                           originalOutlineCounters.selectionOutlines &&
                       owner->_chrome->SyncCountersForTest().selectionVertices ==
                           originalOutlineCounters.selectionVertices;
            };
            const auto fixedStyleAndPath = [&]() noexcept
            {
                owner->RenderFrame();
                const auto resources = owner->_chrome->SelectionResourcesStateForTest();
                return resources.hasPath && resources.pathsBuilt == originalPathCount && resources.fixedSolid &&
                       resources.fixedDashes && resources.dashes[0] == 4 && resources.dashes[1] == 4;
            };
            expect(originalPathCount != 0 && outlineMatches(0) && fixedStyleAndPath(),
                   "outline-original-four-corners-and-fixed-DIP-inks-render");
            owner->BeginPixelDrag(Vec2{1.5, 0.5}, duplicate);
            expect(owner->_session.PendingPixelMove() && !owner->_session.PendingPixelMove()->HasOffset() &&
                       !owner->_pendingDisplay.GraphForFrameOwner() && !owner->_composition.HasStroke(),
                   "move-zero-offset-has-no-GPU-or-owner-preview");
            owner->DragPixels(Vec2{258.5, 0.5}, false);
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 520,
                                                   MoveLiteral(duplicate, 257)),
                        "move-first-landing-literal-in-second-tile"))
                return 5;
            expect(outlineMatches(257) && fixedStyleAndPath(),
                   "outline-first-landing-reuses-source-points-and-real-path");
            auto* first = owner->_pendingDisplay.GraphForFrameOwner();
            owner->DragPixels(Vec2{514.5, 0.5}, false);
            expect(owner->_pendingDisplay.GraphForFrameOwner() == first,
                   "move-new-request-retains-compatible-acknowledged-frame");
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 520,
                                                   MoveLiteral(duplicate, 513)),
                        "move-second-landing-restores-omitted-previous-tile-from-original"))
                return 5;
            expect(outlineMatches(513) && fixedStyleAndPath(),
                   "outline-second-landing-reuses-source-points-and-real-path");
            const auto originalViewport = owner->_viewport;
            const auto originalDpi = owner->_screenshotDpi;
            owner->_viewport.Translate({17.5, -9.25});
            owner->SyncView();
            expect(outlineMatches(513) && fixedStyleAndPath(),
                   "outline-pan-maps-literal-corners-without-point-or-path-rebuild");
            owner->_viewport.SetZoom(0.5, owner->_viewport.Center(), owner->DocumentSize());
            owner->SyncView();
            expect(outlineMatches(513) && fixedStyleAndPath(),
                   "outline-zoom-keeps-fixed-DIP-inks-without-point-or-path-rebuild");
            owner->_screenshotDpi = 144;
            owner->_renderer.SetLayoutDpi(144);
            owner->_viewport.backingScale = 1.5;
            owner->SyncView();
            expect(outlineMatches(513) && fixedStyleAndPath(),
                   "outline-current-DPI-maps-before-paint-without-point-or-path-rebuild");
            owner->_screenshotDpi = originalDpi;
            owner->_renderer.SetLayoutDpi(static_cast<float>(originalDpi));
            owner->_viewport = originalViewport;
            owner->SyncView();
            const auto* pending = owner->_session.PendingPixelMove();
            PendingDisplaySource descriptor;
            expect(pending && pending->display && pending->display->memory && pending->display->memory->IsBound() &&
                       owner->_pixelMoveOpening && owner->_session.DescribePendingDisplaySource(descriptor) &&
                       descriptor.kind == PendingDisplaySource::Kind::PixelMove && !owner->_composition.HasStroke() &&
                       owner->_session.Generation() == generation &&
                       owner->_session.Document()->layers.Identity() == root &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == undo,
                   "move-bound-opening-cache-and-preview-keep-committed-source-and-history");
            const auto presents = owner->_renderer.PresentCount();
            owner->RenderFrame();
            expect(owner->_renderer.PresentCount() == presents + 1 &&
                       owner->_composition.CompositeCount() == legacyComposes &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()),
                   "move-actual-renderer-blits-without-owner-composition");
            owner->DragPixels(Vec2{1.5, 0.5}, false);
            expect(owner->_session.PendingPixelMove() && !owner->_session.PendingPixelMove()->HasOffset() &&
                       !owner->_session.PendingPixelMove()->display && !owner->_pendingDisplay.GraphForFrameOwner(),
                   "move-zero-offset-drops-metadata-and-excludes-old-frame");
            expect(outlineMatches(0) && fixedStyleAndPath(), "outline-zero-offset-reuses-original-source-and-path");
            owner->DragPixels(Vec2{514.5, 0.5}, false);
            expect(!owner->_pendingDisplay.GraphForFrameOwner(),
                   "move-resumed-offset-cannot-resurrect-dropped-old-publication");
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 520,
                                                   MoveLiteral(duplicate, 513)),
                        "move-redrag-publishes-fresh-original-worker-and-GPU-result"))
                return 5;
            expect(outlineMatches(513) && fixedStyleAndPath(), "outline-redrag-keeps-original-source-points-and-path");
            ContinuationReceipt receipt;
            if (commit)
                receipt = owner->QueueHostInput(HostPixelMoveApplyInput{
                    owner->_session.PendingPixelMove() ? owner->_session.PendingPixelMove()->opening : 0});
            else
                owner->CancelPixelDrag();
            expect(settle() && !owner->_session.PendingPixelMove() && !owner->_pendingDisplay.GraphForFrameOwner() &&
                       !owner->_pendingDisplay.HasPending() &&
                       owner->_session.history.UndoCount() == undo + (commit ? 1 : 0) &&
                       (!commit || receipt.Read().outcome == HostOutcome::Succeeded) &&
                       MatchesLiteral(owner->_acknowledgedCommittedDisplay, 520,
                                      commit ? MoveLiteral(duplicate, 513) : original),
                   commit ? "move-Apply-publishes-one-entry-and-independent-final-literal"
                          : "move-Cancel-restores-independent-source-without-history");
            if (commit)
            {
                const DocumentSelection originalSelection{false, {{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}};
                const auto undone = owner->QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                expect(settle() && undone.Read().outcome == HostOutcome::Succeeded &&
                           MatchesLiteral(owner->_acknowledgedCommittedDisplay, 520, original) &&
                           owner->_session.Document()->selection.has_value() &&
                           *owner->_session.Document()->selection == originalSelection &&
                           owner->_session.history.CurrentRevision() == revision &&
                           owner->_session.history.UndoCount() == undo && owner->_session.history.CanRedo() &&
                           owner->_session.history.RedoName() == (duplicate ? L"Duplicate Pixels" : L"Move Pixels"),
                       "move-queued-Undo-restores-independent-pixels-selection-and-history-revision");
            }
            owner->SyncChrome();
            owner->RenderFrame();
            const auto sourceBefore = owner->_overlaySelection.value().loops.Identity();
            const auto resourcesBefore = owner->_chrome->SelectionResourcesStateForTest();
            SelectionDisplayValue replacement{DocumentSelection{false, {{{1, 0}, {2, 0}, {2, 1}, {1, 1}}}}, {}};
            const auto replacementIdentity = replacement.selection.value().loops.Identity();
            owner->SyncSelectionOverlay(&replacement);
            owner->RenderFrame();
            expect(replacementIdentity != sourceBefore &&
                       owner->_overlaySelection.value().loops.Identity() == replacementIdentity &&
                       owner->_chrome->SelectionResourcesStateForTest().pathsBuilt == resourcesBefore.pathsBuilt + 1,
                   "outline-equal-points-new-source-rebuilds-one-path");
            replacement.selection.reset();
            // Only the application's cache now owns the original replacement geometry.
            expect(owner->_overlaySelection.value().loops.Identity() == replacementIdentity &&
                       owner->_overlaySelection.value().loops.PointCount() == 4,
                   "outline-cache-alone-retains-replaced-source");
            bool phasesKeepPath = true;
            const auto replacementPaths = owner->_chrome->SelectionResourcesStateForTest().pathsBuilt;
            for (int phase = 0; phase < 8; ++phase)
            {
                owner->_chrome->SetSelectionAntPhase(static_cast<float>(phase));
                owner->RenderFrame();
                const auto resources = owner->_chrome->SelectionResourcesStateForTest();
                phasesKeepPath = phasesKeepPath && resources.pathsBuilt == replacementPaths && resources.fixedSolid &&
                                 resources.fixedDashes && resources.dashes[0] == 4 && resources.dashes[1] == 4;
            }
            expect(phasesKeepPath, "outline-eight-ant-phases-keep-path-and-fixed-DIP-dashes");
            SelectionDisplayValue emptyOutline{DocumentSelection{}, {}};
            owner->SyncSelectionOverlay(&emptyOutline);
            expect(owner->_overlaySelection && owner->_overlaySelection.value().IsEmpty() && !owner->_marchingAnts &&
                       !owner->_chrome->SelectionResourcesStateForTest().hasPath,
                   "outline-explicit-empty-stays-present-and-releases-old-path");
            SelectionDisplayValue absentOutline;
            owner->SyncSelectionOverlay(&absentOutline);
            expect(!owner->_overlaySelection && !owner->_marchingAnts &&
                       !owner->_chrome->SelectionResourcesStateForTest().hasPath,
                   "outline-nullopt-clears-cache-and-animation");
            owner->SyncChrome();
            owner->RenderFrame();
            const auto beforeRecovery = owner->_chrome->SelectionResourcesStateForTest();
            const auto pointsBeforeRecovery = owner->_chrome->SyncCountersForTest().selectionVertices;
            const auto generationBeforeRecovery = owner->_renderer.DeviceGeneration();
            const HRESULT recovered = owner->_renderer.RecoverDeviceForTest();
            owner->BindCompositionDevice();
            const bool recoverySettled = settle();
            owner->RenderFrame();
            const auto afterRecovery = owner->_chrome->SelectionResourcesStateForTest();
            expect(SUCCEEDED(recovered) && recoverySettled &&
                       owner->_renderer.DeviceGeneration() != generationBeforeRecovery &&
                       owner->_chrome->SyncCountersForTest().selectionVertices == pointsBeforeRecovery &&
                       afterRecovery.hasPath && afterRecovery.pathsBuilt == beforeRecovery.pathsBuilt + 1 &&
                       afterRecovery.fixedSolid && afterRecovery.fixedDashes &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()),
                   "outline-new-actual-device-factory-rebuilds-resources-from-cached-points");
            owner->Shutdown();
            expect(owner->_shutdownComplete && owner->_jobs.ReservedMemoryBytes() == 0 &&
                       owner->_jobs.ReleasingMemoryBytes() == 0 && !owner->_pendingDisplayInitialized,
                   "move-joined-shutdown-retires-CPU-opening-cache-and-GPU-charges");
        }
    }
    enum class PreviewFault
    {
        CancelBefore,
        CancelAfter,
        WorkerFailure,
        Allocation,
        Admission,
        Capture,
        Reopen
    };
    for (const bool move : {false, true})
    {
        for (const auto mode :
             {PreviewFault::CancelBefore, PreviewFault::CancelAfter, PreviewFault::WorkerFailure,
              PreviewFault::Allocation, PreviewFault::Admission, PreviewFault::Capture, PreviewFault::Reopen})
        {
            std::printf("INFO application-pending-raster.fault move=%d mode=%d\n", move, static_cast<int>(mode));
            std::unique_ptr<Application> owner{new (std::nothrow)
                                                   Application(instance, true, L"", 0, nullptr, nullptr)};
            if (!expect(owner != nullptr, "fault-host-allocated"))
                return 5;
            auto gradientGate = std::make_shared<GradientWorkTestGate>();
            auto moveGate = std::make_shared<PixelMoveWorkTestGate>();
            const auto close = wil::scope_exit(
                [&]() noexcept
                {
                    (void)SetEvent(gradientGate->release.get());
                    (void)SetEvent(moveGate->release.get());
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                });
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (!expect(gradientGate->release && moveGate->release && SUCCEEDED(owner->InitializeProcessing()) &&
                            owner->EnableCommittedDisplayForTest() && SUCCEEDED(owner->RegisterWindowClass()) &&
                            SUCCEEDED(owner->CreateMainWindow(false)) && SUCCEEDED(owner->InitializeRenderer()) &&
                            SUCCEEDED(owner->InitializeChrome()),
                        "fault-real-WARP-host-initializes"))
                return 5;
            auto fixture = move ? MoveFixture() : GradientFixture(GradientTarget::Image);
            if (!expect(fixture.has_value(), "fault-independent-fixture-sealed"))
                return 5;
            const auto active = fixture->layers[0].id;
            owner->_session.ReplaceDocument(std::move(fixture.value()), active);
            owner->DocumentChanged();
            const auto settle = [&]() noexcept
            { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
            if (!expect(settle(), "fault-original-committed-display-acknowledged"))
                return 5;
            owner->_session.SetForegroundColor(0xFF000000u);
            owner->_session.SetBackgroundColor(0xFFFFFFFFu);
            owner->_session.gradientSettings.style = GradientStyle::ForegroundToBackground;
            owner->_session.gradientSettings.shape = GradientShape::Linear;
            owner->_session.gradientSettings.opacity = 1;
            if (move)
            {
                owner->BeginPixelDrag(Vec2{1.5, 0.5}, false);
                owner->DragPixels(Vec2{258.5, 0.5}, false);
            }
            else
            {
                owner->BeginGradientDrag(owner->_viewport.ViewFromDocument(Vec2{0.5, 0.5}, owner->DocumentSize()));
                owner->DragGradient(owner->_viewport.ViewFromDocument(Vec2{3.5, 0.5}, owner->DocumentSize()), 0);
                owner->EndGradientDrag();
            }
            const auto expectedGradient = GradientLiteral(GradientTarget::Image, true);
            const auto expectedMove = MoveLiteral(false, 257);
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), move ? 520 : 4,
                                                   move ? std::span<const std::uint8_t>(expectedMove)
                                                        : std::span<const std::uint8_t>(expectedGradient)),
                        "fault-prior-acknowledged-preview-independent-literal"))
                return 5;
            auto* priorGraph = owner->_pendingDisplay.GraphForFrameOwner();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto generation = owner->_session.Generation();
            const auto root = owner->_session.Document()->layers.Identity();
            const auto undo = owner->_session.history.UndoCount();
            const auto errors = owner->_errorsShown;
            const bool after = mode == PreviewFault::CancelAfter || mode == PreviewFault::WorkerFailure ||
                               mode == PreviewFault::Reopen;
            gradientGate->afterPreparation = after;
            moveGate->afterPreparation = after;
            gradientGate->throwAllocationFailure = moveGate->throwAllocationFailure = mode == PreviewFault::Allocation;
            gradientGate->throwAdmissionFailure = moveGate->throwAdmissionFailure = mode == PreviewFault::Admission;
            gradientGate->throwCaptureFailure = moveGate->throwCaptureFailure = mode == PreviewFault::Capture;
            if (mode == PreviewFault::WorkerFailure)
            {
                gradientGate->result = {JobStatus::Failed, E_ACCESSDENIED};
                moveGate->result = {JobStatus::Failed, E_ACCESSDENIED};
            }
            if (move)
                owner->_pixelMoveWorkTestGate = moveGate;
            else
                owner->_gradientWorkTestGate = gradientGate;
            struct FaultState final
            {
                Application& owner;
                bool move;
                PreviewFault mode;
                std::shared_ptr<GradientWorkTestGate> gradient;
                std::shared_ptr<PixelMoveWorkTestGate> pixels;
                RecordedDisplayGraph* prior;
                bool visited = false;
                bool held = false;
                bool retained = false;
            };
            FaultState state{*owner, move, mode, gradientGate, moveGate, priorGraph};
            const auto dispatch = [](void* context, void* native) noexcept
            {
                auto& state = *static_cast<FaultState*>(context);
                auto& owner = state.owner;
                auto& message = *static_cast<MSG*>(native);
                if (message.message != (state.move ? PixelMoveWorkTestGate::kMessage : GradientWorkTestGate::kMessage))
                {
                    owner.DispatchQueuedMessage(message);
                    return;
                }
                if (state.visited)
                {
                    (void)SetEvent(state.move ? state.pixels->release.get() : state.gradient->release.get());
                    return;
                }
                state.visited = true;
                const auto original = state.move ? owner._pixelMovePreviewTicket : owner._gradientPreviewTicket;
                state.held =
                    original.has_value() && owner._pendingDisplay.GraphForFrameOwner() == state.prior &&
                    (state.move ? state.pixels->captureReservationHeld : state.gradient->captureReservationHeld) &&
                    !owner._composition.HasStroke();
                if (state.mode == PreviewFault::CancelBefore || state.mode == PreviewFault::CancelAfter ||
                    state.mode == PreviewFault::Reopen)
                {
                    if (state.move)
                        owner.CancelPixelDrag();
                    else
                        owner.CancelGradient();
                    state.retained =
                        (state.move ? owner._pixelMovePreviewTicket : owner._gradientPreviewTicket) == original &&
                        !owner._pendingDisplay.GraphForFrameOwner();
                    if (state.mode == PreviewFault::Reopen)
                    {
                        // A new opening queues behind the original cancelled worker; it cannot replace its ticket.
                        if (state.move)
                        {
                            owner.BeginPixelDrag(Vec2{1.5, 0.5}, false);
                            owner.DragPixels(Vec2{514.5, 0.5}, false);
                        }
                        else
                        {
                            owner.BeginGradientDrag(
                                owner._viewport.ViewFromDocument(Vec2{0.5, 0.5}, owner.DocumentSize()));
                            owner.DragGradient(owner._viewport.ViewFromDocument(Vec2{3.5, 0.5}, owner.DocumentSize()),
                                               0);
                            owner.EndGradientDrag();
                        }
                        state.retained = state.retained && (state.move ? owner._pixelMovePreviewTicket
                                                                       : owner._gradientPreviewTicket) == original;
                    }
                }
                (void)SetEvent(state.move ? state.pixels->release.get() : state.gradient->release.get());
            };
            if (move)
                owner->DragPixels(Vec2{514.5, 0.5}, false);
            else
            {
                owner->BeginGradientDrag(owner->_viewport.ViewFromDocument(Vec2{3.5, 0.5}, owner->DocumentSize()));
                owner->DragGradient(owner->_viewport.ViewFromDocument(Vec2{2.5, 0.5}, owner->DocumentSize()), 0);
                owner->EndGradientDrag();
            }
            expect(SettleJobs(owner->_jobs, 15000, dispatch, &state) == SettleResult::Settled,
                   "fault-original-worker-and-recording-acknowledge-with-live-pump");
            const bool reachesWorker = mode != PreviewFault::Admission && mode != PreviewFault::Capture;
            expect(state.visited == reachesWorker && (!reachesWorker || state.held),
                   "fault-exercises-intended-phase-with-original-reservation-and-old-frame");
            if (mode == PreviewFault::CancelBefore || mode == PreviewFault::CancelAfter || mode == PreviewFault::Reopen)
                expect(state.retained, "fault-cancel-and-reopen-retain-original-ticket-until-acknowledgement");
            expect(owner->_session.Generation() == generation &&
                       owner->_session.Document()->layers.Identity() == root &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == undo,
                   "fault-preview-never-mutates-committed-source-or-history");
            if (mode == PreviewFault::Reopen)
            {
                const auto reopenedMove = MoveLiteral(false, 513);
                expect(MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), move ? 520 : 4,
                                      move ? std::span<const std::uint8_t>(reopenedMove)
                                           : std::span<const std::uint8_t>(expectedGradient)),
                       "fault-new-opening-alone-publishes-independent-literal");
            }
            else if (mode == PreviewFault::CancelBefore || mode == PreviewFault::CancelAfter)
            {
                expect(!owner->_pendingDisplay.GraphForFrameOwner() &&
                           !(move ? owner->_session.PendingPixelMove() != nullptr
                                  : owner->_session.PendingGradient() != nullptr),
                       "fault-cancelled-prepared-output-cannot-return");
            }
            else
            {
                expect(owner->_errorsShown == errors + 1 && owner->_pendingDisplay.GraphForFrameOwner() == priorGraph,
                       "fault-failure-reports-once-and-keeps-prior-complete-frame");
            }
            if (reachesWorker)
                expect((move ? moveGate->payloadRetired.load() && moveGate->payloadRetiredOffOwner.load()
                             : gradientGate->payloadRetired.load() && gradientGate->payloadRetiredOffOwner.load()),
                       "fault-complete-small-payload-retires-on-worker");
            if (move)
                owner->CancelPixelDrag();
            else
                owner->CancelGradient();
            expect(settle(), "fault-final-cancel-settles-all-owners");
            owner->Shutdown();
            expect(owner->_shutdownComplete && !owner->_pendingDisplayInitialized &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                   "fault-joined-shutdown-refunds-all-original-and-recorder-memory");
        }
    }

    std::printf("SUMMARY application-pending-raster checks=%u failures=%u\n", checks, failures);
    return failures ? 5 : 0;
}

// Draft only. The primary agent should add the declaration/registration after review.
#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Runtime.Tests.TransformWorkTestGate.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunTransformShapeRedrawSelfTest() noexcept
{
    constexpr std::array<std::uint8_t, 16> sourceRed2x2{
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    // Independent literal copied from Document.Tests.TransformPixelsWork.cpp's 2x2-to-4x3 legacy expectation.
    constexpr std::array<std::uint8_t, 48> expectedRed4x3{
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    };
    constexpr std::array<std::uint8_t, 4> sourceMask2x2{0, 73, 161, 255};

    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool condition, const char* check) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s transform-shape-redraw-host.%s\n", condition ? "PASS" : "FAIL", check);
        std::fflush(stdout);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };

    if (!settle() || _transformWorkTestGate || _transformWorkTicket || _transformWorkStamp || _historyWork.Busy() ||
        _activeHostRequest || _hostRequests.Size())
    {
        expect(false, "fixture-starts-idle");
        return 5;
    }

    // Move the entire session, including its history stacks and settings, so this isolated fixture can restore it
    // exactly after all host continuations and worker ownership have retired.
    auto originalSession = std::make_unique<DocumentSession>(std::move(_session));
    _session = DocumentSession{};
    const CanvasViewport originalViewport = _viewport;
    const bool originalAcceleratorShift = _acceleratorShift;
    auto restore = wil::scope_exit(
        [&]() noexcept
        {
            if (_transformWorkTestGate)
                (void)SetEvent(_transformWorkTestGate->release.get());
            if (!settle())
            {
                SelfTestFailure(L"Transform shape redraw cleanup could not settle borrowed host-test state.\n");
                std::_Exit(5);
            }
            _transformWorkTestGate.reset();
            CancelHostContinuations();
            if (!settle())
            {
                SelfTestFailure(L"Transform shape redraw cleanup could not cancel host continuations.\n");
                std::_Exit(5);
            }
            _session = std::move(*originalSession);
            _viewport = originalViewport;
            _acceleratorShift = originalAcceleratorShift;
            DocumentChanged();
        });

    _session.CreateDocument(16, 8);
    const Guid layerID = InsertPaintedLayer(_session, L"Shape redraw fixture", 2, 2, {1, 2}, 0.0, 0xFFFF0000u);
    Raster sourceImage = Raster::Make(2, 2, RasterFormat::Bgra8888, sourceRed2x2.data(), sourceRed2x2.size());
    Raster maskImage = Raster::Make(2, 2, RasterFormat::Gray8, sourceMask2x2.data(), sourceMask2x2.size());
    if (layerID == Guid{} || !sourceImage.HasStorage() || !maskImage.HasStorage())
    {
        expect(false, "literal-fixture-raster-allocation");
        return 5;
    }

    const Raster sourceThumbnail = Raster::MakeThumbnail(sourceImage);
    const Raster maskThumbnail = Raster::MakeThumbnail(maskImage);
    if (!sourceThumbnail.HasStorage() || !maskThumbnail.HasStorage())
    {
        expect(false, "fixture-thumbnails-created");
        return 5;
    }
    EditLayerRecordsForTest(_session,
                            [&](DocumentFixture& document)
                            {
                                auto& layer = document.layers.back();
                                layer.asset->image = sourceImage;
                                layer.asset->thumbnail = sourceThumbnail;
                                layer.shape = LayerShapeStyle{ShapeKind::Rectangle, 1, 0, 0, 0};
                                layer.shapeImage = sourceImage;
                                LayerMask mask;
                                mask.asset = RasterAsset{maskImage, maskThumbnail, L"Implicit linked mask"};
                                mask.linked = true;
                                mask.placement.reset();
                                layer.mask = std::move(mask);
                            });
    _session.SelectLayer(layerID);
    _session.history.Reset();
    _session.history.MarkSaved();
    _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
    DocumentChanged();

    const ImageLayer* initialLayer = _session.ActiveLayer();
    const auto* initialPixels = initialLayer && initialLayer->asset ? initialLayer->asset->image.Data() : nullptr;
    if (!initialLayer || !initialLayer->asset || !initialLayer->mask || !initialLayer->shape ||
        initialLayer->asset->image != sourceImage || initialLayer->shapeImage != sourceImage ||
        initialLayer->asset->image.Width() != 2 || initialLayer->asset->image.Height() != 2 || !initialPixels ||
        !std::equal(sourceRed2x2.begin(), sourceRed2x2.end(), initialPixels) ||
        initialLayer->mask->asset.image != maskImage || initialLayer->mask->placement)
    {
        expect(false, "literal-live-shape-and-implicit-mask-fixture");
        return 5;
    }
    const CanvasDocument beforeTransform = _session.Document().value();
    const Guid historyBefore = _session.history.CurrentRevision();

    _session.BeginTransform(true);
    auto draft = _session.PendingTransformEdit()->draft;
    draft.size = {4, 3};
    _session.PreviewTransform(draft);
    const std::uint64_t draftRevision = _session.TransformDraftRevision();
    const CanvasDocument pendingDocument = _session.Document().value();
    const std::uint64_t documentSerial = _session.DocumentSerial();

    _transformWorkTestGate = std::make_shared<TransformWorkTestGate>();
    _transformWorkTestGate->kind = TransformWorkTestKind::Commit;
    _transformWorkTestGate->afterPreparation = true;
    if (!_transformWorkTestGate->release)
    {
        expect(false, "after-preparation-gate-created");
        return 5;
    }

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct GateState final
    {
        Application& owner;
        std::shared_ptr<TransformWorkTestGate> gate;
        ContinuationReceipt receipt;
        CanvasDocument document;
        Raster source;
        Raster mask;
        std::array<std::uint8_t, 4> maskBytes{};
        Guid historyRevision;
        std::uint64_t documentSerial = 0;
        double zoom = 0;
        bool visited = false;
        bool held = false;
        bool viewChanged = false;
    };
#pragma warning(pop)

    GateState state{*this,
                    _transformWorkTestGate,
                    QueueHostInput(HostTransformFinishInput{layerID, draftRevision, false}),
                    pendingDocument,
                    sourceImage,
                    maskImage,
                    sourceMask2x2,
                    historyBefore,
                    documentSerial,
                    _viewport.zoom};
    const auto dispatch = [](void* context, void* nativeMessage) noexcept
    {
        auto& gateState = *static_cast<GateState*>(context);
        auto& owner = gateState.owner;
        auto& message = *static_cast<MSG*>(nativeMessage);
        if (message.message != TransformWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }

        gateState.visited = true;
        owner.OnCommand(ChromeHost::kCommandZoomIn);
        gateState.viewChanged = owner._viewport.zoom != gateState.zoom;

        const auto* active = owner._session.ActiveLayer();
        const auto& pending = owner._session.PendingTransformEdit();
        const auto* maskPixels = active && active->mask ? active->mask->asset.image.Data() : nullptr;
        const bool literalMaskStillPresent =
            maskPixels && active->mask->asset.image.Width() == 2 && active->mask->asset.image.Height() == 2 &&
            std::equal(gateState.maskBytes.begin(), gateState.maskBytes.end(), maskPixels);
        gateState.held =
            owner._activeHostRequest && owner._transformWorkTicket && owner._transformWorkStamp &&
            owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() == gateState.gate->reservationBytes &&
            gateState.gate->captureReservationHeld && gateState.receipt.Read().outcome == HostOutcome::Pending &&
            owner._session.DocumentSerial() == gateState.documentSerial && owner._session.Document() &&
            owner._session.Document().value() == gateState.document &&
            owner._session.history.CurrentRevision() == gateState.historyRevision &&
            owner._session.history.UndoCount() == 0 && pending && !pending->floating && active &&
            pending->layerID == active->id && pending->draft.size == Vec2{4, 3} && active->asset &&
            active->asset->image == gateState.source && active->shapeImage == gateState.source && active->mask &&
            active->mask->asset.image == gateState.mask && !active->mask->placement && literalMaskStillPresent;
        (void)SetEvent(gateState.gate->release.get());
    };

    const SettleResult settled = SettleJobs(_jobs, 15000, dispatch, &state);
    expect(settled == SettleResult::Settled && state.visited, "actual-finish-continuation-reaches-worker-gate");
    expect(state.held, "prepared-publication-holds-source-history-preview-and-full-reservation");
    expect(state.viewChanged, "view-input-dispatches-while-worker-is-held");
    if (settled != SettleResult::Settled)
        return 5;

    const auto receipt = state.receipt.Read();
    const ImageLayer* committed = _session.ActiveLayer();
    const auto maskBytesMatch = [&]() noexcept
    {
        const auto* pixels = committed && committed->mask ? committed->mask->asset.image.Data() : nullptr;
        return pixels && committed->mask->asset.image.Width() == 2 && committed->mask->asset.image.Height() == 2 &&
               std::equal(sourceMask2x2.begin(), sourceMask2x2.end(), pixels);
    };
    const auto checkCommittedPixels = [&]() noexcept
    {
        const auto* pixels = committed && committed->asset ? committed->asset->image.Data() : nullptr;
        if (!pixels || !committed->asset || committed->asset->image.Width() != 4 ||
            committed->asset->image.Height() != 3 || committed->asset->image.Stride() != 16)
            return false;
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 16; ++x)
                if (pixels[static_cast<std::size_t>(y) * 16 + x] !=
                    expectedRed4x3[static_cast<std::size_t>(y) * 16 + x])
                    return false;
        return true;
    };
    const bool shapeAndMaskPublished =
        committed && committed->asset && committed->shape && committed->mask &&
        committed->asset->image == committed->shapeImage && committed->shapeImage.HasStorage() &&
        committed->shapeImage.Width() == 4 && committed->shapeImage.Height() == 3 &&
        committed->shape == LayerShapeStyle{ShapeKind::Rectangle, 1, 0, 0, 0} && committed->mask->linked &&
        committed->mask->asset.image == maskImage && committed->mask->placement &&
        committed->mask->placement.value() == committed->transform && committed->mask->asset.image.Width() == 2 &&
        committed->mask->asset.image.Height() == 2 && maskBytesMatch();
    expect(receipt.outcome == HostOutcome::Succeeded, "host-finish-receipt-succeeded");
    expect(shapeAndMaskPublished && checkCommittedPixels(),
           "literal-redraw-pixels-live-identity-and-explicit-mask-grid");
    expect(committed && committed->transform.size == Vec2{4, 3} && _session.history.UndoCount() == 1 &&
               _session.history.UndoName() == L"Transform Layer" && !_session.PendingTransformEdit(),
           "one-transform-history-publication");
    expect(_transformWorkTestGate->retired && _transformWorkTestGate->retiredOffOwner && !_transformWorkTicket &&
               !_transformWorkStamp && !_historyWork.Busy() && !_hostCommitStamp && !_activeHostRequest &&
               !_hostRequests.Size() && !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes(),
           "worker-payload-and-reservation-retired");

    _transformWorkTestGate.reset();
    _session.Undo();
    const ImageLayer* undone = _session.ActiveLayer();
    expect(_session.Document() && _session.Document().value() == beforeTransform && undone && undone->asset &&
               undone->asset->image == sourceImage && undone->shapeImage == sourceImage && undone->mask &&
               undone->mask->asset.image == maskImage && !undone->mask->placement &&
               std::equal(sourceMask2x2.begin(), sourceMask2x2.end(), undone->mask->asset.image.Data()) &&
               _session.history.UndoCount() == 0 && _session.history.CanRedo() &&
               _session.history.RedoName() == L"Transform Layer",
           "undo-restores-independent-literal-source-and-implicit-mask");

    _session.Redo();
    committed = _session.ActiveLayer();
    expect(committed && committed->asset && committed->shapeImage == committed->asset->image &&
               committed->shapeImage.Width() == 4 && committed->shapeImage.Height() == 3 && checkCommittedPixels() &&
               committed->mask && committed->mask->asset.image == maskImage && committed->mask->placement &&
               committed->mask->placement.value() == committed->transform && _session.history.UndoCount() == 1 &&
               !_session.history.CanRedo() && maskBytesMatch(),
           "redo-restores-independent-literal-redraw-and-explicit-mask-grid");

    const int result = failures ? 5 : 0;
    std::printf("%s transform-shape-redraw-host.summary checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks,
                failures);
    std::fflush(stdout);
    return result;
}

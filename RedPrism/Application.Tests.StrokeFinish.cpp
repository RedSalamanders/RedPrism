#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"

#include "../Tests/Support/DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace
{
constexpr int kSide = 8;
constexpr DWORD kWaitMs = 10000;
using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel kClearPixel{0, 0, 0, 0};
constexpr Pixel kRedPixel{0, 0, 255, 255};

CanvasDocument MakeStrokeFinishDocument(bool sparsePixels, bool withMask, Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;

    std::array<std::uint8_t, kSide * kSide * 4> pixels{};
    if (withMask)
        pixels.fill(255); // Opaque white exposes the mask in the independent GPU pixel check.
    Raster image = sparsePixels ? Raster::MakeSparse(kSide, kSide, RasterFormat::Bgra8888, {}, 0, 0, {}, 0)
                                : Raster::Make(kSide, kSide, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    ImageLayer layer = ImageLayer::Blank(L"Stroke finish fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{std::move(image), {}, layer.name};
    active = layer.id;

    if (withMask)
    {
        std::array<std::uint8_t, kSide * kSide> maskBytes{};
        maskBytes.fill(128);
        Raster mask = Raster::Make(kSide, kSide, RasterFormat::Gray8, maskBytes.data(), maskBytes.size());
        LayerMask target;
        target.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Literal 128 mask"};
        layer.mask = std::move(target);
    }
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

bool ReadPixel(const DocumentSession& session, int x, int y, Pixel& pixel) noexcept
{
    const ImageLayer* const layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    const Raster& raster = layer->asset->image;
    const int sourceX = x - static_cast<int>(std::lround(layer->transform.origin.x));
    const int sourceY = y - static_cast<int>(std::lround(layer->transform.origin.y));
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || sourceX < 0 || sourceY < 0 ||
        sourceX >= raster.Width() || sourceY >= raster.Height())
        return false;
    raster.ReadRegion(sourceX, sourceY, 1, 1, pixel.data(), 4, 0);
    return true;
}

bool ReadMaskPixel(const Raster& raster, int x, int y, std::uint8_t& pixel) noexcept
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || x < 0 || y < 0 || x >= raster.Width() ||
        y >= raster.Height())
        return false;
    raster.ReadRegion(x, y, 1, 1, &pixel, 1, 0);
    return true;
}

// Diagnostic readback is confined to this harness after actual publication and settlement.
bool ReadDisplayPixel(RecordedDisplayGraph* graph, int x, int y, Pixel& pixel) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    if (!composition)
        return false;
    std::vector<std::uint8_t> bytes;
    UINT width = 0, height = 0;
    if (FAILED(composition->Readback(bytes, width, height)) || width != kSide || height != kSide || x < 0 || y < 0 ||
        x >= kSide || y >= kSide || bytes.size() != static_cast<std::size_t>(width) * height * 4)
        return false;
    const auto offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4;
    for (std::size_t channel = 0; channel < pixel.size(); ++channel)
        pixel[channel] = bytes[offset + channel];
    return true;
}

} // namespace

int Application::RunStandaloneStrokeFinishSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::UseSynthetic(true);
    const auto keyboard = wil::scope_exit([]() noexcept { KeyboardState::UseSynthetic(false); });
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* control) noexcept
    {
        std::printf("%s application-stroke-finish.%s.%s\n", condition ? "PASS" : "FAIL", scenario, control);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    const auto setupFailure = [](const char* name) noexcept
    {
        std::printf("FAIL application-stroke-finish.setup.%s\n", name);
        std::fflush(stdout);
        return 5;
    };
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return setupFailure("actual-host-allocation");
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    const auto makeGate = [](bool hold = true) noexcept -> std::shared_ptr<StrokeFinishTestGate>
    {
        try
        {
            return std::make_shared<StrokeFinishTestGate>(hold);
        }
        catch (const std::bad_alloc&)
        {
            return {};
        }
    };

    const auto findStrokeOrdinal = [&](std::uint64_t opening) noexcept -> std::optional<std::uint64_t>
    {
        if (owner->_activeHostRequest)
        {
            const auto* const input = std::get_if<HostCanvasBrushInput>(&owner->_activeHostRequest->input.Input());
            if (input && input->kind == HostCanvasInputKind::Brush && input->opening == opening)
                return owner->_activeHostRequest->ordinal;
        }
        for (std::size_t index = 0; index < owner->_hostRequests.Size(); ++index)
        {
            const auto* const entry = owner->_hostRequests.PendingAt(index);
            const auto* const input = entry ? std::get_if<HostCanvasBrushInput>(&entry->input.Input()) : nullptr;
            if (entry && input && input->kind == HostCanvasInputKind::Brush && input->opening == opening)
                return entry->ordinal;
        }
        return std::nullopt;
    };
    const auto queueRecordedStroke = [&](Vec2 start, Vec2 end, std::uint64_t& opening, std::uint64_t& ordinal) noexcept
    {
        DxUi::PointerEvent down{};
        down.action = DxUi::PointerAction::Down;
        down.modifiers = MK_LBUTTON;
        if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument(start, owner->DocumentSize())))
            return false;
        opening = owner->_canvasBrushInput.Opening();
        const auto queuedOrdinal = findStrokeOrdinal(opening);
        if (opening == 0 || !queuedOrdinal)
            return false;
        ordinal = *queuedOrdinal;

        DxUi::PointerEvent move{};
        move.action = DxUi::PointerAction::Move;
        move.modifiers = MK_LBUTTON;
        owner->RecordCanvasBrush(move, owner->_viewport.ViewFromDocument(end, owner->DocumentSize()));
        DxUi::PointerEvent up{};
        up.action = DxUi::PointerAction::Up;
        owner->RecordCanvasBrush(up, owner->_viewport.ViewFromDocument(end, owner->DocumentSize()));
        return owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending;
    };
    const auto waitForGate = [&](HANDLE reached) noexcept
    {
        const ULONGLONG deadline = GetTickCount64() + kWaitMs;
        HANDLE waits[]{static_cast<HANDLE>(owner->_jobs.WakeHandle()), reached};
        for (;;)
        {
            if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                return false;
            (void)DrainCompletions(owner->_jobs);
            (void)RunUiActions(owner->_jobs);
            if (WaitForSingleObject(reached, 0) == WAIT_OBJECT_0)
                return true;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                return false;
            const DWORD wait = MsgWaitForMultipleObjectsEx(2, waits, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                           MWMO_INPUTAVAILABLE);
            if (wait == WAIT_OBJECT_0 + 1)
                return true;
            if (wait == WAIT_OBJECT_0 + 2 || wait == WAIT_OBJECT_0)
                continue;
            return false;
        }
    };
    const auto waitForFinalPrepare = [&](const std::shared_ptr<StrokeFinishTestGate>& gate) noexcept
    { return waitForGate(gate->reached.get()); };
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    const auto installScene = [&](bool sparsePixels, bool withMask, Guid& active) noexcept
    {
        auto document = MakeStrokeFinishDocument(sparsePixels, withMask, active);
        owner->ReplaceDocument(std::move(document), active, {});
        owner->_tool = EditorTool::Brush;
        owner->_spacePan = false;
        owner->_drag = {};
        owner->_canvasPanLast.reset();
        owner->_session.brushMode = BrushToolMode::Paint;
        owner->_session.maskPaintWhite = false;
        owner->_session.brushSettings = BrushSettings{4, 1, 1, 0, 0, 1};
        if (withMask)
            owner->_session.SelectLayerTarget(active, true);
        owner->_session.history.Reset();
        owner->_session.history.MarkSaved();
        owner->_viewport.zoom = 1;
        owner->_viewport.pan = {};
        owner->_viewport.followsFit = false;
        owner->DocumentChanged();
        owner->SyncChrome();
        return settle();
    };
    const auto finalRootsAreCurrent = [&](const Guid& active) noexcept
    {
        const auto* const display = owner->_acknowledgedCommittedDisplay;
        const auto& rows = owner->_preparedLayerRows;
        return display && display->IsPublishedOnOwner(owner->_renderer.Device(), owner->_renderer.Context()) && rows &&
               rows->FindVisibleRow(active).has_value();
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner->_strokeFinishTestGate)
            {
                (void)SetEvent(owner->_strokeFinishTestGate->release.get());
            }
            if (!owner->_shutdownComplete)
            {
                owner->CancelHostContinuations();
                if (SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                    std::_Exit(5);
                owner->Shutdown();
            }
        });

    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return setupFailure("actual-hidden-warp-host-initialize");
    owner->BindCompositionDevice();

    Guid initialActive{};
    auto initialDocument = MakeStrokeFinishDocument(false, false, initialActive);
    owner->ReplaceDocument(std::move(initialDocument), initialActive, {});
    owner->DocumentChanged();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start())
        return setupFailure("publication-and-rows-owner-sources-start");
    owner->_asyncStrokeFinishEnabled = true;
    if (!owner->_renderer.IsWarp() || !settle())
        return setupFailure("initial-hidden-warp-display-and-rows-settle");

    // Case 1: the physical sample stream retains its admitted host ordinal through real worker preparation,
    // then publishes exactly one history entry and the expected literal paint.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("paint-scene-settle");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("paint-gate-events");
        owner->_strokeFinishTestGate = gate;
        std::uint64_t opening = 0;
        std::uint64_t originalOrdinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, originalOrdinal) || !waitForFinalPrepare(gate))
            return setupFailure("paint-real-final-worker-prepare");

        const bool sameOriginalRequest =
            owner->_activeHostRequest && owner->_activeHostRequest->ordinal == originalOrdinal &&
            owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending && owner->_utilityTicket.has_value() &&
            !owner->_session.PendingBrush() && owner->_canvasBrushSessionOpening.has_value() &&
            owner->_session.PendingBrushOpening() == owner->_canvasBrushSessionOpening &&
            owner->_session.history.UndoCount() == 0 && gate->prepares.load(std::memory_order_acquire) == 1 &&
            gate->offowner.load(std::memory_order_acquire) == 1;
        expect(sameOriginalRequest, "paint", "real-up-keeps-original-ordinal-and-draft-through-final-worker-prepare");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("paint-final-publication-settle");
        owner->_strokeFinishTestGate.reset();

        Pixel painted{};
        const bool committed = owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                               owner->_session.history.UndoCount() == 1 &&
                               owner->_session.history.UndoName() == L"Brush Stroke" &&
                               ReadPixel(owner->_session, 3, 3, painted) && painted == kRedPixel;
        expect(committed, "paint", "literal-red-commits-as-one-original-history-entry");
        expect(!owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   owner->_canvasBrushInput.PendingCount() == 0,
               "paint", "completion-releases-utility-ticket-and-queued-canvas-stream");
        expect(finalRootsAreCurrent(active), "paint", "final-display-and-prepared-layer-row-publish");
        Pixel displayed{};
        expect(ReadDisplayPixel(owner->_acknowledgedCommittedDisplay, 3, 3, displayed) && displayed == kRedPixel,
               "paint", "published-gpu-display-matches-independent-literal-red-pixel");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load(std::memory_order_acquire),
               "paint", "actual-worker-retirement-payload-destructor-runs-off-owner");

        owner->OnCommand(ChromeHost::kCommandUndo);
        Pixel undone{};
        const bool undo = settle() && owner->_session.history.UndoCount() == 0 &&
                          owner->_session.history.RedoName() == L"Brush Stroke" &&
                          ReadPixel(owner->_session, 3, 3, undone) && undone == kClearPixel;
        expect(undo, "paint", "undo-restores-exact-literal-source-pixel");
        owner->OnCommand(ChromeHost::kCommandRedo);
        Pixel redone{};
        const bool redo = settle() && owner->_session.history.UndoCount() == 1 &&
                          ReadPixel(owner->_session, 3, 3, redone) && redone == kRedPixel;
        expect(redo, "paint", "redo-restores-exact-literal-painted-pixel");
    }

    // Case 2: a sparse source and its final replacement stay sparse and unassembled after exact pixel reads.
    {
        Guid active{};
        if (!installScene(true, false, active))
            return setupFailure("sparse-scene-settle");
        const ImageLayer* const sourceLayer = owner->_session.ActiveLayer();
        if (!sourceLayer || !sourceLayer->asset || !sourceLayer->asset->image.IsSparse() ||
            sourceLayer->asset->image.IsMaterialized())
            return setupFailure("sparse-source-fixture");
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !settle())
            return setupFailure("sparse-stroke-settle");
        Pixel painted{};
        const ImageLayer* const resultLayer = owner->_session.ActiveLayer();
        expect(resultLayer && resultLayer->asset && resultLayer->asset->image.IsSparse() &&
                   !resultLayer->asset->image.IsMaterialized() && ReadPixel(owner->_session, 3, 3, painted) &&
                   painted == kRedPixel && !resultLayer->asset->image.IsMaterialized(),
               "sparse", "literal-paint-read-keeps-sparse-result-unmaterialized");
    }

    // Case 3: mask publication preserves both semantic mask bytes and the matching thumbnail.
    {
        Guid active{};
        if (!installScene(false, true, active))
            return setupFailure("mask-scene-settle");
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !settle())
            return setupFailure("mask-stroke-settle");
        const ImageLayer* const layer = owner->_session.ActiveLayer();
        std::uint8_t center = 255, outside = 0, thumbnailCenter = 255, thumbnailOutside = 0;
        const bool semanticMask =
            layer && layer->mask && ReadMaskPixel(layer->mask->asset.image, 3, 3, center) && center == 0 &&
            ReadMaskPixel(layer->mask->asset.image, 0, 0, outside) && outside == 128 &&
            ReadMaskPixel(layer->mask->asset.thumbnail, 3, 3, thumbnailCenter) && thumbnailCenter == 0 &&
            ReadMaskPixel(layer->mask->asset.thumbnail, 0, 0, thumbnailOutside) && thumbnailOutside == 128;
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 1 && owner->_session.history.UndoName() == L"Paint Mask" &&
                   semanticMask,
               "mask", "literal-mask-bytes-thumbnail-and-history-publish-together");
        expect(finalRootsAreCurrent(active), "mask", "mask-finish-publishes-current-display-and-rows");
        Pixel masked{}, outsideMask{};
        constexpr Pixel kHalfWhite{128, 128, 128, 128};
        expect(ReadDisplayPixel(owner->_acknowledgedCommittedDisplay, 3, 3, masked) && masked == kClearPixel &&
                   ReadDisplayPixel(owner->_acknowledgedCommittedDisplay, 0, 0, outsideMask) &&
                   outsideMask == kHalfWhite,
               "mask", "published-gpu-mask-is-clear-at-paint-and-literal-half-white-outside");
    }

    // Case 4: the entire four-pixel tip stays outside the canvas, so no tile can be painted.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("unchanged-scene-settle");
        const Guid revision = owner->_session.history.CurrentRevision();
        auto* const priorDisplay = owner->_acknowledgedCommittedDisplay;
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({-20, -20}, {-19, -19}, opening, ordinal) || !settle())
            return setupFailure("unchanged-stroke-settle");
        expect(owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded &&
                   owner->_session.history.UndoCount() == 0 && owner->_session.history.CurrentRevision() == revision &&
                   !owner->_utilityTicket && !owner->_canvasBrushInput.Busy() &&
                   owner->_acknowledgedCommittedDisplay == priorDisplay,
               "unchanged", "empty-final-coverage-releases-ticket-without-history-or-display-replacement");
    }

    // Case 5: cancellation while the actual final worker holds its completed draft cannot publish pixels/history.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("cancel-scene-settle");
        const Guid revision = owner->_session.history.CurrentRevision();
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("cancel-gate-events");
        owner->_strokeFinishTestGate = gate;
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !waitForFinalPrepare(gate) ||
            !owner->_utilityTicket)
            return setupFailure("cancel-real-final-worker-prepare");
        const bool cancelled = owner->CancelCanvasBrush(false);
        if (!cancelled || !SetEvent(gate->release.get()) || !settle())
            return setupFailure("cancel-worker-retirement-settle");
        owner->_strokeFinishTestGate.reset();
        Pixel unchanged{};
        expect(cancelled && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Cancelled &&
                   owner->_session.history.UndoCount() == 0 && owner->_session.history.CurrentRevision() == revision &&
                   ReadPixel(owner->_session, 3, 3, unchanged) && unchanged == kClearPixel && !owner->_utilityTicket &&
                   !owner->_activeHostRequest,
               "cancel", "cancelled-prepared-final-worker-does-not-publish-or-retain-utility-ticket");
    }

    // Case 6: failure after real final-coverage preparation preserves the exact error and retires the draft.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("failure-scene-settle");
        const Guid revision = owner->_session.history.CurrentRevision();
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("failure-gate-events");
        gate->failureResult = JobResult{JobStatus::Failed, static_cast<std::int32_t>(E_OUTOFMEMORY)};
        owner->_strokeFinishTestGate = gate;
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !waitForFinalPrepare(gate))
            return setupFailure("failure-real-final-worker-prepare");

        const bool preparedOriginalRequest =
            owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal &&
            owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending && owner->_utilityTicket.has_value() &&
            !owner->_session.PendingBrush() && owner->_canvasBrushSessionOpening.has_value() &&
            owner->_session.PendingBrushOpening() == owner->_canvasBrushSessionOpening &&
            owner->_session.history.UndoCount() == 0 && owner->_session.history.CurrentRevision() == revision &&
            gate->prepares.load(std::memory_order_acquire) == 1 && gate->offowner.load(std::memory_order_acquire) == 1;
        expect(preparedOriginalRequest, "failure", "real-prepare-retains-original-pending-request-and-opening");

        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("failure-worker-retirement-settle");
        owner->_strokeFinishTestGate.reset();
        const ContinuationReceipt::Result receipt = owner->_canvasBrushReceipt.Read();
        Pixel unchanged{};
        const bool exactFailure =
            receipt.outcome == HostOutcome::Failed && receipt.error == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
            owner->_session.history.UndoCount() == 0 && owner->_session.history.CurrentRevision() == revision &&
            !owner->_session.PendingBrush() && ReadPixel(owner->_session, 3, 3, unchanged) && unchanged == kClearPixel;
        expect(exactFailure, "failure", "exact-eoutofmemory-receipt-without-pixel-or-history-edit");
        expect(!owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   owner->_canvasBrushInput.PendingCount() == 0,
               "failure", "failed-finish-releases-utility-request-and-canvas-stream");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load(std::memory_order_acquire),
               "failure", "actual-worker-retirement-payload-destructor-runs-off-owner");
    }

    // Case 7: failed original retirement offers retain every owner without polling. A real queue binding
    // transition authorizes one retry; even a second failure must preserve the first terminal cause.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("quarantine-scene-settle");
        auto gate = makeGate(false);
        if (!gate || !gate->quarantined)
            return setupFailure("quarantine-gate-event");
        owner->_strokeFinishTestGate = gate;
        owner->_gpuWork.SetRetirementCompletionRefusalsForTest(E_FAIL, 1);
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !waitForGate(gate->quarantined.get()))
            return setupFailure("actual-first-retirement-refusal");
        const auto originalTicket = owner->_utilityTicket;
        const auto originalCharge = owner->_jobs.ReservedMemoryBytes();
        expect(originalTicket && owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_strokeFinishSource.HasPending() && !owner->_strokeFinishSource.HasReady() &&
                   owner->_session.history.UndoCount() == 0 && !gate->retired.load() && gate->quarantines.load() == 1 &&
                   gate->lastRetirementRefusal.load() == E_FAIL,
               "quarantine", "healthy-device-refusal-keeps-original-ticket-charge-and-unpublished-draft");
        for (unsigned int attempt = 0; attempt < 6; ++attempt)
        {
            owner->_strokeFinishSource.DeviceChangedOwner();
            owner->_strokeFinishSource.Pump();
        }
        expect(gate->quarantines.load() == 1 && owner->_utilityTicket == originalTicket &&
                   owner->_jobs.ReservedMemoryBytes() == originalCharge && !owner->_strokeFinishSource.HasReady(),
               "quarantine", "unchanged-owner-pumps-do-not-retry-or-refund-quarantined-work");
        const auto changeBinding = [&]() noexcept
        {
            owner->_gpuWork.RetireDevice();
            if (FAILED(owner->_gpuWork.Bind(owner->_renderer.Device(), owner->_renderer.Context())))
                return false;
            owner->_committedDisplay.DeviceChangedOwner();
            owner->_strokeFinishSource.DeviceChangedOwner();
            return true;
        };
        const auto firstGeneration = owner->_gpuWork.DeviceGeneration();
        (void)ResetEvent(gate->quarantined.get());
        // Rebinding also retires the committed cache. Refuse this original stroke's offer,
        // without consuming the diagnostic failure in an unrelated cache barrier.
        if (!originalTicket)
            return setupFailure("quarantine-original-ticket");
        owner->_gpuWork.SetRetirementCompletionRefusalsForTest(E_ACCESSDENIED, 1, originalTicket.value());
        if (!changeBinding() || !waitForGate(gate->quarantined.get()))
            return setupFailure("actual-second-retirement-refusal");
        expect(owner->_gpuWork.DeviceGeneration() != firstGeneration && gate->quarantines.load() == 2 &&
                   gate->lastRetirementRefusal.load() == E_ACCESSDENIED && owner->_utilityTicket == originalTicket &&
                   !owner->_strokeFinishSource.HasReady() &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
               "quarantine", "real-binding-transition-offers-once-and-second-refusal-stays-dormant");
        for (unsigned int attempt = 0; attempt < 6; ++attempt)
        {
            owner->_strokeFinishSource.DeviceChangedOwner();
            owner->_strokeFinishSource.Pump();
        }
        expect(gate->quarantines.load() == 2 && !gate->retired.load() && owner->_utilityTicket == originalTicket,
               "quarantine", "second-refusal-retains-original-owners-until-another-real-transition");
        if (!changeBinding() || !settle())
            return setupFailure("actual-original-retirement-recovery-settle");
        const auto receipt = owner->_canvasBrushReceipt.Read();
        Pixel unchanged{};
        expect(receipt.outcome == HostOutcome::Failed && receipt.error == E_FAIL &&
                   owner->_session.history.UndoCount() == 0 && ReadPixel(owner->_session, 3, 3, unchanged) &&
                   unchanged == kClearPixel,
               "quarantine", "actual-original-event-recovery-keeps-first-error-without-pixel-or-history-publication");
        expect(gate->retired.load() && gate->retiredOffOwner.load() && !owner->_utilityTicket &&
                   !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   !owner->_strokeFinishSource.HasPending(),
               "quarantine", "acknowledged-recovery-retires-original-payload-and-releases-host-stream");
        owner->_strokeFinishTestGate.reset();
    }

    // Case 8: cancellation acknowledges its skipped worker stage, but cannot release quarantined GPU owners.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("quarantine-cancel-scene-settle");
        auto gate = makeGate(false);
        if (!gate || !gate->quarantined || !gate->cancelled)
            return setupFailure("quarantine-cancel-gate-events");
        owner->_strokeFinishTestGate = gate;
        owner->_gpuWork.SetRetirementCompletionRefusalsForTest(E_FAIL, 1);
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !waitForGate(gate->quarantined.get()))
            return setupFailure("quarantine-cancel-real-refusal");
        const auto originalTicket = owner->_utilityTicket;
        const auto originalCharge = owner->_jobs.ReservedMemoryBytes();
        const bool cancelled = owner->CancelCanvasBrush(false);
        if (!cancelled || !waitForGate(gate->cancelled.get()))
            return setupFailure("quarantine-cancel-worker-acknowledgement");
        expect(gate->cancellationAcknowledgements.load() == 1 && gate->quarantines.load() == 1 &&
                   owner->_utilityTicket == originalTicket && owner->_jobs.ReservedMemoryBytes() == originalCharge &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending && !gate->retired.load(),
               "quarantine-cancel", "cancelled-worker-acknowledgement-keeps-original-gpu-and-cpu-charge");
        for (unsigned int attempt = 0; attempt < 6; ++attempt)
        {
            owner->_strokeFinishSource.DeviceChangedOwner();
            owner->_strokeFinishSource.Pump();
        }
        expect(!owner->_strokeFinishSource.HasReady() && gate->quarantines.load() == 1 && !gate->retired.load(),
               "quarantine-cancel", "cancellation-does-not-self-rearm-or-retire-quarantined-source");
        owner->_gpuWork.RetireDevice();
        if (FAILED(owner->_gpuWork.Bind(owner->_renderer.Device(), owner->_renderer.Context())))
            return setupFailure("quarantine-cancel-real-binding-transition");
        owner->_committedDisplay.DeviceChangedOwner();
        owner->_strokeFinishSource.DeviceChangedOwner();
        if (!settle())
            return setupFailure("quarantine-cancel-original-event-recovery-settle");
        const auto receipt = owner->_canvasBrushReceipt.Read();
        Pixel unchanged{};
        expect(receipt.outcome == HostOutcome::Cancelled && receipt.error == E_ABORT &&
                   owner->_session.history.UndoCount() == 0 && ReadPixel(owner->_session, 3, 3, unchanged) &&
                   unchanged == kClearPixel,
               "quarantine-cancel", "cancellation-wins-only-after-original-recovery-with-no-document-edit");
        expect(gate->retired.load() && gate->retiredOffOwner.load() && !owner->_utilityTicket &&
                   !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy(),
               "quarantine-cancel", "actual-recovery-joins-and-retires-cancelled-original-payload");
        owner->_strokeFinishTestGate.reset();
    }

    // Case 9: the full physical FIFO seals the accepted prefix at its last point, refuses the next point and
    // retains that capacity failure through the real final parent. No message pump drains samples while filling.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("overflow-scene-settle");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("overflow-gate-events");
        owner->_strokeFinishTestGate = gate;

        DxUi::PointerEvent down{};
        down.action = DxUi::PointerAction::Down;
        down.modifiers = MK_LBUTTON;
        if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument({3, 3}, owner->DocumentSize())))
            return setupFailure("overflow-real-down-admission");
        const auto opening = owner->_canvasBrushInput.Opening();
        const auto ordinal = findStrokeOrdinal(opening);
        if (!opening || !ordinal)
            return setupFailure("overflow-original-ordinal");

        DxUi::PointerEvent move{};
        move.action = DxUi::PointerAction::Move;
        move.modifiers = MK_LBUTTON;
        for (std::size_t index = 1; index < CanvasBrushInput::kCapacity; ++index)
            owner->RecordCanvasBrush(move, owner->_viewport.ViewFromDocument({4, 3}, owner->DocumentSize()));
        owner->RecordCanvasBrush(move, owner->_viewport.ViewFromDocument({7, 7}, owner->DocumentSize()));
        DxUi::PointerEvent up{};
        up.action = DxUi::PointerAction::Up;
        owner->RecordCanvasBrush(up, owner->_viewport.ViewFromDocument({7, 7}, owner->DocumentSize()));
        expect(owner->_canvasBrushInput.Overflowed() && !owner->_canvasBrushInput.Sampling() &&
                   owner->_canvasBrushInput.PendingCount() == CanvasBrushInput::kCapacity + 1 &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
               "overflow", "full-fifo-retains-accepted-prefix-and-separate-lost-terminal");
        if (!waitForFinalPrepare(gate))
            return setupFailure("overflow-real-final-worker-prepare");
        expect(owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal.value() &&
                   owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                   owner->_session.history.UndoCount() == 0,
               "overflow", "accepted-prefix-finalization-retains-original-pending-ordinal");
        if (!SetEvent(gate->release.get()) || !settle())
            return setupFailure("overflow-final-publication-settle");

        const auto receipt = owner->_canvasBrushReceipt.Read();
        Pixel start{}, last{}, refused{};
        expect(receipt.outcome == HostOutcome::Failed &&
                   receipt.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                   owner->_session.history.UndoCount() == 1 && owner->_session.history.UndoName() == L"Brush Stroke" &&
                   ReadPixel(owner->_session, 3, 3, start) && start == kRedPixel &&
                   ReadPixel(owner->_session, 4, 3, last) && last == kRedPixel &&
                   ReadPixel(owner->_session, 7, 7, refused) && refused == kClearPixel,
               "overflow", "quota-receipt-commits-only-literal-accepted-prefix");
        Pixel displayed{}, refusedDisplay{};
        expect(finalRootsAreCurrent(active) &&
                   ReadDisplayPixel(owner->_acknowledgedCommittedDisplay, 4, 3, displayed) && displayed == kRedPixel &&
                   ReadDisplayPixel(owner->_acknowledgedCommittedDisplay, 7, 7, refusedDisplay) &&
                   refusedDisplay == kClearPixel,
               "overflow", "published-display-and-rows-exclude-refused-point");
        expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load(std::memory_order_acquire) &&
                   !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                   owner->_canvasBrushInput.PendingCount() == 0,
               "overflow", "acknowledged-overflow-retires-original-worker-payload-and-stream");
        owner->_strokeFinishTestGate.reset();
    }

    // Case 10: real Application shutdown releases the held final worker through its shutdown event and joins it.
    {
        Guid active{};
        if (!installScene(false, false, active))
            return setupFailure("shutdown-scene-settle");
        auto gate = makeGate();
        if (!gate || !gate->reached || !gate->release)
            return setupFailure("shutdown-gate-events");
        owner->_strokeFinishTestGate = gate;
        std::uint64_t opening = 0;
        std::uint64_t ordinal = 0;
        if (!queueRecordedStroke({3, 3}, {4, 3}, opening, ordinal) || !waitForFinalPrepare(gate))
            return setupFailure("shutdown-real-final-worker-prepare");
        const DWORD ownerThread = GetCurrentThreadId();
        owner->Shutdown();
        const auto receipt = owner->_canvasBrushReceipt.Read();
        expect(owner->_shutdownComplete && receipt.outcome == HostOutcome::Cancelled && receipt.error == E_ABORT &&
                   gate->prepares.load(std::memory_order_acquire) == 1 &&
                   gate->offowner.load(std::memory_order_acquire) == 1 &&
                   gate->retired.load(std::memory_order_acquire) &&
                   gate->retiredOffOwner.load(std::memory_order_acquire) && ownerThread == gate->ownerThread,
               "shutdown", "actual-shutdown-event-releases-final-worker-and-close-joins-it");
        owner->_strokeFinishTestGate.reset();
        const auto quit = PumpMessages();
        const auto remainingMessages = PumpMessages();
        expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit, "shutdown",
               "fixture-consumes-one-quit-before-the-next-owner");
    }

    std::printf("%s application-stroke-finish.summary checks=%u failures=%u\n", failures == 0 ? "PASS" : "FAIL", checks,
                failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 5;
}

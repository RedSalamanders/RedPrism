#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "SharedPublicationMemory.h"
#include "StrokeFinishTestGate.h"
#include "WarpOpeningWork.h"
#include "WarpSampleWork.h"

#include "../Tests/Support/DocumentFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <vector>

namespace
{
constexpr int kSide = 16;
constexpr DWORD kWait = 10000;
constexpr BrushSettings kSettings{6, .8, 0, 0, 0, 1};
constexpr Vec2 kDown{7.25, 7.25}, kUp{9.25, 7.25};

CanvasDocument Scene(Guid& active)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = fixture.height = kSide;
    std::array<std::uint8_t, kSide * kSide * 4> bytes{};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
        {
            const auto p = static_cast<std::size_t>((y * kSide + x) * 4);
            bytes[p] = static_cast<std::uint8_t>(x * 11 + y * 3);
            bytes[p + 1] = static_cast<std::uint8_t>(y * 13 + x * 2);
            bytes[p + 2] = static_cast<std::uint8_t>(x * 5 + y * 7);
            bytes[p + 3] = 255;
        }
    auto layer = ImageLayer::Blank(L"Warp final host fixture", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::Make(kSide, kSide, RasterFormat::Bgra8888, bytes.data(), bytes.size()), {}, layer.name};
    active = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

// Independent reference keeps the original contiguous Warp plus scalar Append/Flush route. It does not use
// the candidate preview reader, ordered replay facade, command recorder or mapped-row installation.
std::optional<BrushStroke::Commit> Reference(const ImageLayer& original, SmearMode mode)
{
    if (!original.asset)
        return {};
    WarpStroke warp(original.asset->image, mode, kSettings);
    warp.Append(kDown);
    warp.Append(kUp);
    auto settings = kSettings;
    settings.diameter = 10;
    settings.hardness = settings.opacity = 1;
    BrushStroke stroke(original, false, settings, kSide, kSide, nullptr);
    stroke.SetSample(warp.Image(), 0, 0, true);
    for (const auto point : warp.Points())
        if (!stroke.Append(point))
            return {};
    return stroke.Flush() ? stroke.PaintSnapshot() : std::nullopt;
}

bool SamePixels(const Raster& actual, const Raster& reference, int tolerance) noexcept
{
    if (!actual.HasStorage() || !reference.HasStorage() || actual.Width() != reference.Width() ||
        actual.Height() != reference.Height() || actual.Width() != kSide || actual.Height() != kSide)
        return false;
    std::array<std::uint8_t, kSide * 4> a{}, b{};
    for (int y = 0; y < kSide; ++y)
    {
        actual.ReadRegion(0, y, kSide, 1, a.data(), kSide * 4, 0);
        reference.ReadRegion(0, y, kSide, 1, b.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::abs(static_cast<int>(a[i]) - b[i]) > tolerance)
                return false;
    }
    return true;
}

// Harness-only readback after the actual final publication. Production never synchronously reads this root.
bool SameDisplay(RecordedDisplayGraph* graph, const Raster& reference) noexcept
{
    auto* composition = graph ? graph->CompositionForTestOwner() : nullptr;
    std::vector<std::uint8_t> bytes;
    unsigned int width = 0, height = 0;
    if (!composition || FAILED(composition->Readback(bytes, width, height)) || width != kSide || height != kSide ||
        bytes.size() != static_cast<std::size_t>(kSide * kSide * 4))
        return false;
    std::array<std::uint8_t, kSide * 4> row{};
    for (int y = 0; y < kSide; ++y)
    {
        reference.ReadRegion(0, y, kSide, 1, row.data(), kSide * 4, 0);
        for (std::size_t i = 0; i < row.size(); ++i)
            if (std::abs(static_cast<int>(bytes[static_cast<std::size_t>(y) * row.size() + i]) - row[i]) > 2)
                return false;
    }
    return true;
}
} // namespace

int Application::RunStandaloneWarpFinishSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned int checks = 0, failures = 0;
    const auto expect = [&](bool condition, const char* mode, const char* name) noexcept
    {
        std::printf("%s application-warp-finish.%s.%s\n", condition ? "PASS" : "FAIL", mode, name);
        ++checks;
        failures += condition ? 0u : 1u;
        std::fflush(stdout);
    };
    const auto fail = [](const char* name) noexcept
    {
        std::printf("FAIL application-warp-finish.setup.%s\n", name);
        std::fflush(stdout);
        return 5;
    };
    std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
    if (!owner)
        return fail("owner");
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (owner->_strokeFinishTestGate)
                (void)SetEvent(owner->_strokeFinishTestGate->release.get());
            if (!owner->_shutdownComplete)
            {
                owner->CancelHostContinuations();
                if (SettleJobs(owner->_jobs, kWait, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
                    std::_Exit(5);
                owner->Shutdown();
            }
            (void)PumpMessages(); // Consume this independent hidden owner's quit.
        });
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return fail("initialize");
    owner->BindCompositionDevice();
    Guid initial{};
    owner->ReplaceDocument(Scene(initial), initial, {});
    owner->DocumentChanged();
    if (!owner->EnableCommittedDisplayForTest(true) || !owner->EnablePreparedLayerRows() ||
        !owner->_strokeFinishSource.Start() || !owner->_brushEngine.Ready())
        return fail("sources-and-warp-compute");
    const auto settle = [&]() noexcept
    { return SettleJobs(owner->_jobs, kWait, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
    if (!settle())
        return fail("initial-display");

    // This diagnostic begins with an explicitly worker-prepared opening/sample fixture. The real original
    // admitted final parent and host receipt run below; normal Down/live-preview activation is a separate gate.
    for (const auto mode : {SmearMode::Liquify, SmearMode::Smudge})
        for (const bool cancelled : {false, true})
        {
            const char* name = mode == SmearMode::Liquify ? (cancelled ? "liquify-cancel" : "liquify")
                                                          : (cancelled ? "smudge-cancel" : "smudge");
            Guid active{};
            auto document = Scene(active);
            const auto original = document.layers.front();
            owner->ReplaceDocument(std::move(document), active, {});
            owner->_session.brushSettings = kSettings;
            owner->_session.smearMode = mode;
            owner->_tool = EditorTool::Smear;
            owner->DocumentChanged();
            if (!settle())
                return fail("scene");
            const auto baselineCharge = owner->_jobs.ReservedMemoryBytes();
            std::optional<BrushStroke::Commit> reference;
            auto opening =
                owner->_session.PrepareWarpOpeningInput({kSettings, mode, kDown, std::this_thread::get_id()});
            std::optional<WarpOpeningResult> opened;
            if (!opening)
                return fail("opening-input");
            std::thread construct(
                [&]
                {
                    reference = Reference(original, mode);
                    opened.emplace(DocumentSession::PrepareWarpOpeningPublication(
                        std::move(*opening), {}, {}, SharedPublicationMemory::CreateForWork()));
                });
            construct.join();
            if (!reference || !opened || !opened->prepared ||
                !owner->_session.ApplyPreparedWarpOpeningIfCurrent(*opened->prepared))
                return fail("fixture-opening-apply");
            auto temporary = opened->prepared->TakeAppliedDraftForOwner();
            temporary.reset();
            const auto plan = owner->_session.PendingWarp()->PlanSampleMemory(kUp);
            auto input =
                plan ? owner->_session.TakeWarpSampleInput(*owner->_session.PendingWarpOpening(), *plan) : std::nullopt;
            if (!input)
                return fail("fixture-sample-input");
            bool sampled = false;
            std::thread sample([&] { sampled = input->AppendForWork({}); });
            sample.join();
            if (!sampled || !owner->_session.ApplyWarpSampleIfCurrent(*input))
                return fail("fixture-sample-apply");
            owner->_canvasBrushSessionOpening = owner->_session.PendingWarpOpening();
            const auto hostOpening = ++owner->_canvasBrushOpening;
            CanvasBrushContext captured{
                owner->_session.DocumentSerial(), active, EditorTool::Smear, BrushKind::Paint, kSettings, false, false};
            if (!owner->_canvasBrushInput.Begin(hostOpening, captured, {kDown, MK_LBUTTON}) ||
                owner->_canvasBrushInput.End(hostOpening, {kUp, 0}, false) != CanvasBrushInput::Admission::Recorded ||
                !owner->_canvasBrushInput.Take(hostOpening) || !owner->_canvasBrushInput.Take(hostOpening))
                return fail("sealed-host-stream");
            owner->_canvasBrushReplayed = true;
            owner->_canvasBrushReceipt = owner->QueueHostInput(HostCanvasBrushInput{hostOpening});
            auto selected = owner->_hostRequests.TakeReady(owner->_session.DocumentSerial(), false);
            if (!selected.ready || owner->_activeHostRequest)
                return fail("original-host-selection");
            owner->_activeHostRequest = std::move(selected.ready);
            owner->_hostStage = HostStage::CanvasBrush;
            const auto ordinal = owner->_activeHostRequest->ordinal;
            const auto revision = owner->_session.history.CurrentRevision();
            auto gate = std::make_shared<StrokeFinishTestGate>(true);
            owner->_strokeFinishTestGate = gate;
            if (FAILED(owner->StartWarpFinish(hostOpening)))
                return fail("admitted-final-parent");
            const ULONGLONG deadline = GetTickCount64() + kWait;
            HANDLE waits[]{static_cast<HANDLE>(owner->_jobs.WakeHandle()), gate->reached.get()};
            while (WaitForSingleObject(gate->reached.get(), 0) != WAIT_OBJECT_0)
            {
                if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                    return fail("unexpected-quit");
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                const auto now = GetTickCount64();
                if (now >= deadline)
                    return fail("final-prepare-deadline");
                const auto waited = MsgWaitForMultipleObjectsEx(2, waits, static_cast<DWORD>(deadline - now),
                                                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (waited == WAIT_FAILED)
                    return fail("final-prepare-wait");
            }
            expect(owner->_activeHostRequest && owner->_activeHostRequest->ordinal == ordinal &&
                       owner->_utilityTicket && !owner->_session.PendingWarp() && owner->_session.HasPendingWarp() &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                       gate->prepares.load() == 1 && gate->offowner.load() == 1,
                   name, "original-parent-retains-authority-through-real-recording-uint-readback-and-final-worker");
            const bool cancelAccepted = !cancelled || owner->CancelCanvasBrush(false);
            if (!cancelAccepted || !SetEvent(gate->release.get()) || !settle())
                return fail("final-settle");
            const auto* layer = owner->_session.ActiveLayer();
            if (cancelled)
            {
                expect(layer && layer->asset && SamePixels(layer->asset->image, original.asset->image, 0) &&
                           owner->_session.history.CurrentRevision() == revision &&
                           owner->_session.history.UndoCount() == 0 && !owner->_session.HasPendingWarp() &&
                           owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Cancelled &&
                           owner->_canvasBrushReceipt.Read().error == E_ABORT,
                       name, "held-final-cancellation-keeps-original-source-history-and-receipt");
                expect(owner->_acknowledgedCommittedDisplay &&
                           SameDisplay(owner->_acknowledgedCommittedDisplay, original.asset->image) &&
                           owner->_jobs.ReservedMemoryBytes() == baselineCharge && !owner->_utilityTicket &&
                           !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() && gate->retired.load() &&
                           gate->retiredOffOwner.load(),
                       name, "original-acknowledged-display-and-charge-survive-cancelled-worker-retirement");
            }
            else
            {
                expect(layer && layer->asset && SamePixels(layer->asset->image, reference->image, 2) &&
                           layer->transform == reference->transform && owner->_session.history.UndoCount() == 1 &&
                           owner->_session.history.UndoName() ==
                               (mode == SmearMode::Liquify ? L"Liquify" : L"Smudge") &&
                           owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Succeeded,
                       name, "complete-final-pixels-placement-and-one-history-match-original-scalar-reference");
                expect(owner->_acknowledgedCommittedDisplay &&
                           SameDisplay(owner->_acknowledgedCommittedDisplay, reference->image) &&
                           owner->_preparedLayerRows && owner->_preparedLayerRows->FindVisibleRow(active) &&
                           !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                           gate->retired.load() && gate->retiredOffOwner.load(),
                       name, "display-rows-receipt-and-off-owner-retirement-complete-together");
                owner->_strokeFinishTestGate.reset();
                owner->OnCommand(ChromeHost::kCommandUndo);
                const bool undone = settle();
                layer = owner->_session.ActiveLayer();
                expect(undone && layer && layer->asset && SamePixels(layer->asset->image, original.asset->image, 0) &&
                           owner->_session.history.CurrentRevision() == revision &&
                           owner->_session.history.UndoCount() == 0,
                       name, "undo-restores-original-literal-pattern-and-revision");
            }
            owner->_strokeFinishTestGate.reset();
            std::thread retire(
                [&]
                {
                    opened.reset();
                    input.reset();
                    reference.reset();
                });
            retire.join();
        }
    std::printf("SUMMARY application-warp-finish checks=%u failures=%u\n", checks, failures);
    return failures == 0 ? 0 : 5;
}

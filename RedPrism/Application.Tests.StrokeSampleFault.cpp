#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace
{
constexpr int kSide = 8;
constexpr DWORD kWaitMs = 10000;
using Boundary = StrokeFinishTestGate::SampleBoundary;
enum class Fault : std::uint8_t
{
    Cancel,
    Failure,
    Device,
    Shutdown
};
struct BoundaryCase final
{
    const char* name;
    Boundary boundary;
};
struct FaultCase final
{
    const char* name;
    Fault fault;
};
CanvasDocument MakeFaultScene(Guid& target)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = kSide;
    fixture.height = kSide;
    auto layer = ImageLayer::Blank(L"Sample fault target", {kSide, kSide});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset =
        RasterAsset{Raster::MakeSparse(kSide, kSide, RasterFormat::Bgra8888, {}, 0, 0, {}, 0), {}, layer.name};
    target = layer.id;
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}
bool IsClear(const DocumentSession& session) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset)
        return false;
    std::array<std::uint8_t, 4> pixel{};
    layer->asset->image.ReadRegion(3, 3, 1, 1, pixel.data(), 4, 255);
    return pixel == std::array<std::uint8_t, 4>{0, 0, 0, 0};
}
} // namespace

int Application::RunStandaloneStrokeSampleFaultSelfTest(HINSTANCE instance) noexcept
{
    KeyboardState::UseSynthetic(true);
    const auto keyboard = wil::scope_exit([]() noexcept { KeyboardState::UseSynthetic(false); });
    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool value, const char* phase, const char* fault, const char* control) noexcept
    {
        std::printf("%s application-stroke-sample-fault.%s.%s.%s\n", value ? "PASS" : "FAIL", phase, fault, control);
        std::fflush(stdout);
        ++checks;
        failures += value ? 0u : 1u;
    };
    const auto fail = [](const char* phase, const char* fault, const char* control) noexcept
    {
        std::printf("FAIL application-stroke-sample-fault.setup.%s.%s.%s\n", phase, fault, control);
        std::fflush(stdout);
        return 5;
    };
    constexpr std::array boundaries{
        BoundaryCase{"prepared", Boundary::Prepared}, BoundaryCase{"composed", Boundary::Composed},
        BoundaryCase{"preview-recorded", Boundary::PreviewRecorded},
        BoundaryCase{"returning-draft", Boundary::ReturningDraft}, BoundaryCase{"dormant", Boundary::None}};
    constexpr std::array faults{FaultCase{"cancel", Fault::Cancel}, FaultCase{"failure", Fault::Failure},
                                FaultCase{"device", Fault::Device}, FaultCase{"shutdown", Fault::Shutdown}};
    unsigned scenarios = 0;
    for (const auto& boundary : boundaries)
    {
        for (const auto& fault : faults)
        {
            if (boundary.boundary == Boundary::None && fault.fault == Fault::Failure)
                continue; // A dormant parent has no producer result to fail.
            ++scenarios;
            std::unique_ptr<Application> owner{new (std::nothrow)
                                                   Application(instance, true, L"", 0, nullptr, nullptr)};
            if (!owner)
                return fail(boundary.name, fault.name, "allocation");
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            auto gate = std::make_shared<StrokeFinishTestGate>(false);
            gate->holdSample = boundary.boundary;
            if (fault.fault == Fault::Failure)
                gate->failureResult = JobResult{JobStatus::Failed, static_cast<std::int32_t>(E_OUTOFMEMORY)};
            const auto cleanup = wil::scope_exit(
                [&]() noexcept
                {
                    (void)SetEvent(gate->release.get());
                    if (!owner->_shutdownComplete)
                    {
                        owner->CancelHostContinuations();
                        if (SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) !=
                            SettleResult::Settled)
                            std::_Exit(5);
                        owner->Shutdown();
                    }
                    (void)PumpMessages(); // Each independently owned hidden window posts one quit at shutdown.
                });
            const auto settle = [&]() noexcept
            { return SettleJobs(owner->_jobs, kWaitMs, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
            const auto waitForBoundary = [&]() noexcept
            {
                const auto deadline = GetTickCount64() + kWaitMs;
                for (;;)
                {
                    if (PumpMessages(DispatchMessageThunk, owner.get()).quit)
                        return false;
                    (void)DrainCompletions(owner->_jobs);
                    (void)RunUiActions(owner->_jobs);
                    if (boundary.boundary == Boundary::None
                            ? gate->samplesAcknowledged.load(std::memory_order_acquire) == 1
                            : WaitForSingleObject(gate->reached.get(), 0) == WAIT_OBJECT_0)
                        return true;
                    if (owner->_canvasBrushReceipt.Read().outcome != HostOutcome::Pending)
                        return false;
                    if (owner->_jobs.HasUiWork() || owner->_ownerWork.HasReady())
                        continue;
                    const auto now = GetTickCount64();
                    if (now >= deadline)
                        return false;
                    const HANDLE event = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                    const auto result = MsgWaitForMultipleObjectsEx(1, &event, static_cast<DWORD>(deadline - now),
                                                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                        return false;
                }
            };
            if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
                FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
                FAILED(owner->InitializeChrome()))
                return fail(boundary.name, fault.name, "initialize");
            owner->BindCompositionDevice();
            // Recording is a startup facility: enable it before any history or document producer admission.
            if (!owner->EnableCommittedDisplayForTest(true))
                return fail(boundary.name, fault.name, "enable-initial-display");
            if (!owner->EnablePreparedLayerRows())
                return fail(boundary.name, fault.name, "enable-initial-rows");
            if (!owner->_strokeFinishSource.Start())
                return fail(boundary.name, fault.name, "attach-original-parent-source");
            Guid target{};
            auto scene = MakeFaultScene(target);
            owner->ReplaceDocument(std::move(scene), target, {});
            owner->_tool = EditorTool::Brush;
            owner->_session.brushSettings = BrushSettings{4, 1, 1, 0, 0, 1};
            owner->_session.SetForegroundColor(0xFFFF0000u);
            owner->_session.history.Reset();
            owner->_session.history.MarkSaved();
            owner->_viewport.zoom = 1;
            owner->_viewport.pan = {};
            owner->_viewport.followsFit = false;
            owner->DocumentChanged();
            if (!settle())
                return fail(boundary.name, fault.name, "initial-display-settle");
            owner->_asyncStrokeFinishEnabled = true;
            owner->_asyncStrokeSamplesEnabled = true;
            owner->_strokeFinishTestGate = gate;
            DxUi::PointerEvent down{};
            down.action = DxUi::PointerAction::Down;
            down.modifiers = MK_LBUTTON;
            if (!owner->QueueCanvasBrush(down, owner->_viewport.ViewFromDocument({3, 3}, owner->DocumentSize())) ||
                !waitForBoundary() || !owner->_utilityTicket || !owner->_activeHostRequest)
                return fail(boundary.name, fault.name, "real-sample-boundary");
            const auto ticket = owner->_utilityTicket;
            const auto ordinal = owner->_activeHostRequest->ordinal;
            const auto charge = owner->_jobs.ReservedMemoryBytes();
            const auto revision = owner->_session.history.CurrentRevision();
            const auto gpuCharge = owner->_graphGpuBudget.ReservedOwner();
            const bool returned = boundary.boundary == Boundary::ReturningDraft || boundary.boundary == Boundary::None;
            expect(charge != 0 && gpuCharge != 0 && owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending &&
                       owner->_session.history.UndoCount() == 0 && IsClear(owner->_session) &&
                       (owner->_acknowledgedStrokePreview != nullptr) == returned &&
                       (boundary.boundary == Boundary::None
                            ? !owner->_strokeFinishSource.HasReady()
                            : gate->sampleBoundaryVisits.load() == 1 && gate->sampleBoundaryOffOwner.load() == 1),
                   boundary.name, fault.name, "actual-original-parent-holds-charge-and-uncommitted-source");
            const auto beforeZoom = owner->_viewport.zoom;
            owner->OnCommand(ChromeHost::kCommandZoomIn);
            expect(owner->_viewport.zoom > beforeZoom && owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                       owner->_activeHostRequest->ordinal == ordinal &&
                       owner->_canvasBrushReceipt.Read().outcome == HostOutcome::Pending,
                   boundary.name, fault.name, "view-command-remains-responsive-with-original-ordinal-held");
            if (fault.fault == Fault::Shutdown)
            {
                owner->Shutdown(); // Actual shutdown event releases the producer; no synthetic release signal.
                const auto receipt = owner->_canvasBrushReceipt.Read();
                expect(
                    owner->_shutdownComplete && receipt.outcome == HostOutcome::Cancelled && receipt.error == E_ABORT &&
                        gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load(),
                    boundary.name, fault.name, "shutdown-event-joins-producer-and-retires-original-payload-off-owner");
                expect(!owner->_acknowledgedStrokePreview && !owner->_utilityTicket && !owner->_strokeFinishCurrent &&
                           !owner->_canvasBrushInput.Busy() && !owner->_gpuWork.HasPending() &&
                           owner->_graphGpuBudget.ActiveOwner() == 0,
                       boundary.name, fault.name, "joined-shutdown-releases-all-input-preview-and-gpu-reservations");
                continue;
            }
            if (fault.fault == Fault::Cancel && !owner->CancelCanvasBrush(false))
                return fail(boundary.name, fault.name, "cancel-original-input");
            if (fault.fault == Fault::Device)
            {
                const auto generation = owner->_renderer.DeviceGeneration();
                if (FAILED(owner->_renderer.RecoverDeviceForTest()))
                    return fail(boundary.name, fault.name, "actual-device-recovery");
                owner->BindCompositionDevice();
                expect(owner->_renderer.DeviceGeneration() != generation && !owner->_acknowledgedStrokePreview &&
                           owner->_utilityTicket == ticket && owner->_activeHostRequest &&
                           owner->_activeHostRequest->ordinal == ordinal && !gate->retired.load(),
                       boundary.name, fault.name,
                       "real-replacement-excludes-old-frames-without-fabricating-producer-join");
            }
            else if (boundary.boundary != Boundary::None)
                expect(owner->_utilityTicket == ticket && owner->_jobs.ReservedMemoryBytes() == charge &&
                           owner->_graphGpuBudget.ReservedOwner() == gpuCharge && !gate->retired.load(),
                       boundary.name, fault.name, "unreleased-producer-preserves-original-complete-charges");
            if (!SetEvent(gate->release.get()) || !settle())
                return fail(boundary.name, fault.name, "original-retirement-settle");
            const auto receipt = owner->_canvasBrushReceipt.Read();
            const auto expected = fault.fault == Fault::Failure  ? HostOutcome::Failed
                                  : fault.fault == Fault::Device ? HostOutcome::Stale
                                                                 : HostOutcome::Cancelled;
            const auto error = fault.fault == Fault::Failure ? E_OUTOFMEMORY : E_ABORT;
            expect(receipt.outcome == expected && receipt.error == static_cast<std::int32_t>(error) &&
                       owner->_session.history.UndoCount() == 0 &&
                       owner->_session.history.CurrentRevision() == revision && IsClear(owner->_session),
                   boundary.name, fault.name, "exact-terminal-receipt-preserves-original-pixels-and-history");
            expect(gate->retired.load(std::memory_order_acquire) && gate->retiredOffOwner.load() &&
                       !owner->_utilityTicket && !owner->_activeHostRequest && !owner->_canvasBrushInput.Busy() &&
                       !owner->_acknowledgedStrokePreview && !owner->_session.PendingBrush() &&
                       !owner->_strokeFinishSource.HasPending(),
                   boundary.name, fault.name, "actual-acknowledgement-retires-preview-draft-and-original-input-owner");
        }
    }
    std::printf("%s application-stroke-sample-fault.summary checks=%u failed=%u scenarios=%u\n",
                failures ? "FAIL" : "PASS", checks, failures, scenarios);
    return failures ? 5 : 0;
}

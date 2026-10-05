#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ColorWorkTestGate.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

int Application::RunSamplerHostSelfTest() noexcept
{
    if (!_chrome || !_session.Document() || !_samplerState || !_hostSourceRegistered ||
        (_chrome && _chrome->ColorPickerOpen()))
    {
        SelfTestFailure(L"The sampler host self-test requires an initialized document, chrome, and host graph.\n");
        return 5;
    }

    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"Earlier work did not settle before sampler host qualification.\n");
        return 5;
    }

    const CanvasDocument originalDocument = _session.Document().value();
    const std::optional<Guid> originalActive = _session.ActiveLayerID();
    const std::wstring originalPath = _documentPath;
    const std::uint32_t originalForeground = _session.ForegroundColor();
    const std::uint32_t originalBackground = _session.BackgroundColor();
    bool fixtureInstalled = false;

    const auto restore = [this, &originalDocument, &originalActive, &originalPath, originalForeground,
                          originalBackground, &fixtureInstalled]() noexcept
    {
        if (_chrome && _chrome->ColorPickerOpen())
            _chrome->CloseColorPicker(false);
        CancelHostContinuations();
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            std::_Exit(5);
        if (fixtureInstalled)
            ReplaceDocument(originalDocument, originalActive, originalPath);
        _session.SetForegroundColor(originalForeground);
        _session.SetBackgroundColor(originalBackground);
        SyncChrome();
    };
    const auto cleanup = wil::scope_exit([&restore]() noexcept { restore(); });
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto check = [&fail](bool condition, const wchar_t* message, const char* control) noexcept
    {
        if (!condition)
        {
            (void)fail(message);
            return false;
        }
        std::printf("PASS %s\n", control);
        return true;
    };

    constexpr std::array<std::uint8_t, 12> bgra{
        0xCC, 0x66, 0x33, 0xFF, // #3366CC, opaque first pixel
        0x20, 0x40, 0x80, 0xFF, // #804020, opaque middle pixel
        0xE1, 0x72, 0x3A, 0x00  // transparent final pixel
    };
    const Raster raster = Raster::Make(3, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    if (!raster.HasStorage())
        return fail(L"The sampler host literal BGRA fixture could not be constructed.\n");

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 3;
    fixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Sampler host literal source", Vec2{3, 1});
    const Guid fixtureActive = layer.id;
    layer.asset = RasterAsset{raster, {}, layer.name};
    fixture.layers.push_back(std::move(layer));
    ReplaceDocument(std::move(fixture).Seal(), fixtureActive, {});
    fixtureInstalled = true;
    _session.SetForegroundColor(originalForeground);
    _session.SetBackgroundColor(originalBackground);
    SyncChrome();

    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto graphClear = [this]() noexcept
    {
        return !_samplerHostTicket && !_samplerHostStamp && _samplerHostJob == nullptr && !_utilityTicket &&
               !_utilityStamp && !_historyWork.Busy() && _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 &&
               !_gpuWork.HasPending() && _composition.UtilitySurfaceBytes() == 0 && _jobs.ReservedMemoryBytes() == 0;
    };

    // One pointer gesture owns the first, middle and transparent endpoint pixels. The endpoint is not a
    // synthetic release sample: the sealed FIFO contains only these three accepted pointer positions.
    const Guid sourceRevision = _session.history.CurrentRevision();
    const int sourceUndoCount = _session.history.UndoCount();
    const std::uint64_t sourceGeneration = _session.Generation();
    const auto source = _session.CaptureDocumentReadAuthority();
    const auto opening = BeginSamplerOpening({0.2, 0.2});
    if (!check(source.has_value() && opening.has_value(),
               L"The sampler host graph did not admit its initial point and source authority.\n",
               "sampler-host.graph.opening-admitted"))
        return 5;
    RecordSamplerPoint(opening.value(), {1.2, 0.2});
    RecordSamplerPoint(opening.value(), {2.2, 0.2});
    auto* retainedSlot = FindSamplerOpening(opening.value());
    if (!check(retainedSlot && retainedSlot->points.PendingCount() == 3,
               L"The sampler host FIFO did not retain exactly the three physical points.\n",
               "sampler-host.graph.three-points-no-release-sample"))
        return 5;
    const auto dependency = CaptureSamplerColorDependency();
    const ContinuationReceipt savedReceipt = retainedSlot->receipt;
    SealSamplerOpening(opening.value());
    if (!check(savedReceipt.Read().outcome == ContinuationOutcome::Pending && settled(),
               L"The real sampler readback continuation did not settle.\n",
               "sampler-host.graph.real-warp-continuation-settles"))
        return 5;
    const auto completed = savedReceipt.Read();
    const auto* completedOpening = FindSamplerOpening(opening.value());
    if (!check(completed.outcome == ContinuationOutcome::Succeeded && completedOpening &&
                   completedOpening->resolvedArgb == 0xFF804020u && _session.ForegroundColor() == 0xFF804020u,
               L"The sealed first/middle/transparent stream did not publish the literal opaque middle color.\n",
               "sampler-host.graph.transparent-tail-keeps-middle"))
        return 5;
    if (!check(_session.history.CurrentRevision() == sourceRevision &&
                   _session.history.UndoCount() == sourceUndoCount && _session.Generation() == sourceGeneration &&
                   source && _session.IsDocumentReadCurrent(source.value()) && graphClear(),
               L"Sampler readback changed committed document/history state or retained graph resources.\n",
               "sampler-host.graph.read-only-and-fully-acknowledged"))
        return 5;
    if (!check(dependency.has_value() && ResolveSamplerColor(dependency.value()) == 0xFF804020u,
               L"A captured consumer did not resolve to the admitted prefix's exact middle sample.\n",
               "sampler-host.graph.captured-dependency-resolves-middle"))
        return 5;
    ReleaseSamplerColorDependency(dependency.value());
    if (!check(FindSamplerOpening(opening.value()) == nullptr,
               L"Releasing the final scalar dependency did not retire its completed opening slot.\n",
               "sampler-host.graph.release-retires-opening"))
        return 5;

    // Reuse the just-released fixed slot and cancel before its queued source can select it.
    const auto reusable = BeginSamplerOpening({0.2, 0.2});
    if (!check(reusable.has_value() && FindSamplerOpening(reusable.value()) == retainedSlot,
               L"The released sampler slot was not reusable by the next opening.\n",
               "sampler-host.graph.released-slot-reused"))
        return 5;
    const auto queuedReceipt = FindSamplerOpening(reusable.value())->receipt;
    CancelHostContinuations();
    const bool queuedSettled = settled() && queuedReceipt.Read().outcome == ContinuationOutcome::Cancelled;
    const bool queuedOpeningRetired = FindSamplerOpening(reusable.value()) == nullptr;
    if (!queuedOpeningRetired)
    {
        // Keep the self-test fixture bounded even when this assertion exposes a caller retirement defect.
        RetireSamplerOpening(reusable.value());
        SweepSamplerOpenings();
    }
    if (!check(queuedSettled && queuedOpeningRetired && graphClear(),
               L"A queued sampler cancelled before selection did not retire cleanly.\n",
               "sampler-host.graph.queued-cancel-before-selection"))
        return 5;

    // Select a real descriptor, then cancel its original scheduler record before owner source capture.
    const auto selectedCancel = BeginSamplerOpening({0.2, 0.2});
    if (!selectedCancel)
        return fail(L"The selected cancellation fixture was not admitted.\n");
    const auto selectedReceipt = FindSamplerOpening(selectedCancel.value())->receipt;
    if (!check(RunUiActions(_jobs) && _samplerHostTicket && _historyWork.Busy(),
               L"The sampler selected-cancel fixture did not hold its original admitted ticket.\n",
               "sampler-host.cancel.selected-original-ticket-held"))
        return 5;
    CancelHostContinuations();
    if (!check(settled() && selectedReceipt.Read().outcome == HostOutcome::Cancelled &&
                   !FindSamplerOpening(selectedCancel.value()) && graphClear(),
               L"Selected sampler cancellation was changed to success or retained its opening.\n",
               "sampler-host.cancel.selected-outcome-preserved-after-acknowledgement"))
        return 5;

    // An acknowledged live prefix is suspended with its original reservation and no GPU/chrome polling.
    wil::unique_handle acknowledged{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!acknowledged)
        return fail(L"The sampler acknowledged-prefix harness event could not be created.\n");
    _samplerAcknowledgedForTest = acknowledged.get();
    const auto clearAcknowledgementEvent =
        wil::scope_exit([this]() noexcept { _samplerAcknowledgedForTest = nullptr; });
    const auto liveCancel = BeginSamplerOpening({0.2, 0.2});
    if (!liveCancel)
        return fail(L"The live sampler cancellation fixture was not admitted.\n");
    const auto liveReceipt = FindSamplerOpening(liveCancel.value())->receipt;
    if (!check(WaitForColorWorkTestEvent(_jobs, acknowledged.get(), DispatchMessageThunk, this) && _samplerHostTicket &&
                   _jobs.ReservedMemoryBytes() != 0 && _historyWork.Busy() && _gpuWork.LentCount() == 0 &&
                   _composition.UtilitySurfaceBytes() == 0 && _session.ForegroundColor() == 0xFF3366CCu &&
                   liveReceipt.Read().outcome == HostOutcome::Pending,
               L"The acknowledged sampler prefix did not retain its exact live graph without GPU ownership.\n",
               "sampler-host.cancel.acknowledged-live-prefix-suspended"))
        return 5;
    if (!check(!RunUiActions(_jobs) && !_jobs.HasUiWork(),
               L"An idle acknowledged sampler prefix rearmed or polled its owner action.\n",
               "sampler-host.cancel.live-prefix-no-idle-source-polling"))
        return 5;
    CancelHostContinuations();
    if (!check(settled() && liveReceipt.Read().outcome == HostOutcome::Cancelled &&
                   _session.ForegroundColor() == 0xFF3366CCu && !FindSamplerOpening(liveCancel.value()) && graphClear(),
               L"Idle sampler cancellation changed the published prefix or failed to acknowledge.\n",
               "sampler-host.cancel.live-prefix-keeps-published-color-and-retires"))
        return 5;
    _samplerAcknowledgedForTest = nullptr;

    // Invalid moves update no FIFO point and cannot manufacture a release endpoint or readback job.
    const auto emptyOpening = BeginSamplerOpening({std::numeric_limits<double>::quiet_NaN(), 0.0});
    if (!check(emptyOpening.has_value(), L"The no-work sampler opening was not admitted.\n",
               "sampler-host.no-work.opening-admitted"))
        return 5;
    RecordSamplerPoint(emptyOpening.value(), {-1.0, 0.0});
    RecordSamplerPoint(emptyOpening.value(), {3.0, 0.0});
    RecordSamplerPoint(emptyOpening.value(), {0.0, std::numeric_limits<double>::infinity()});
    SealSamplerOpening(emptyOpening.value());
    const auto emptyReceipt = FindSamplerOpening(emptyOpening.value())->receipt;
    const auto* emptyResult = FindSamplerOpening(emptyOpening.value());
    if (!check(settled() && emptyReceipt.Read().outcome == ContinuationOutcome::Succeeded && graphClear() &&
                   (!emptyResult || !emptyResult->resolvedIntent),
               L"Outside/nonfinite sampler input performed work or changed the resolved prefix.\n",
               "sampler-host.no-work.outside-nonfinite-prefix"))
        return 5;

    // Capture an older stream dependency, then issue equal explicit palette input before its readback. The old
    // stream still resolves its literal pixel for that dependent consumer, while its UI publication is refused.
    _session.SetForegroundColor(0xFF804020u);
    const auto suppressed = BeginSamplerOpening({0.2, 0.2});
    if (!check(suppressed.has_value(), L"The palette suppression opening was not admitted.\n",
               "sampler-host.authority.suppression-opening-admitted"))
        return 5;
    RecordSamplerPoint(suppressed.value(), {0.2, 0.2});
    const auto olderDependency = CaptureSamplerColorDependency();
    const ContinuationReceipt suppressedReceipt = FindSamplerOpening(suppressed.value())->receipt;
    _session.SetForegroundColor(0xFF804020u); // Equal-value explicit input still invalidates manual intent.
    const std::uint32_t manualFallback = _session.ForegroundColor();
    const auto newConsumerDependency = CaptureSamplerColorDependency();
    SealSamplerOpening(suppressed.value());
    if (!check(olderDependency && settled() && suppressedReceipt.Read().outcome == ContinuationOutcome::Succeeded &&
                   _session.ForegroundColor() == manualFallback &&
                   ResolveSamplerColor(olderDependency.value()) == 0xFF3366CCu && graphClear(),
               L"Equal explicit palette input failed to suppress UI publication or preserve the older dependency.\n",
               "sampler-host.authority.equal-manual-write-suppresses-publication"))
        return 5;
    ReleaseSamplerColorDependency(olderDependency.value());

    const std::uint32_t newConsumerFallback = manualFallback;
    if (!check(!newConsumerDependency && newConsumerFallback == manualFallback &&
                   _session.ForegroundColor() == newConsumerFallback,
               L"A consumer accepted after manual palette input did not retain its captured fallback.\n",
               "sampler-host.authority.new-consumer-keeps-manual-fallback"))
        return 5;

    // Close/reopen the picker at the same target and color while a stream is pending. Its old opening must not
    // capture a point or publish into the replacement picker.
    _chrome->OpenColorPicker(PickerTarget::Foreground, _session.ForegroundColor());
    const std::uint32_t pickerOpeningColor = _chrome->PickerColor();
    const auto pickerStream = BeginSamplerOpening({0.2, 0.2});
    if (!check(pickerStream.has_value() && _chrome->ColorPickerOpen(),
               L"The picker-bound sampler stream did not open.\n", "sampler-host.picker.old-opening-admitted"))
        return 5;
    _chrome->CloseColorPicker(false);
    _chrome->OpenColorPicker(PickerTarget::Foreground, pickerOpeningColor);
    RecordSamplerPoint(pickerStream.value(), {1.2, 0.2});
    SealSamplerOpening(pickerStream.value());
    const ContinuationReceipt pickerReceipt = FindSamplerOpening(pickerStream.value())->receipt;
    if (!check(settled() && pickerReceipt.Read().outcome == ContinuationOutcome::Succeeded &&
                   _chrome->ColorPickerOpen() && _chrome->PickerColor() == pickerOpeningColor && graphClear(),
               L"A closed/reopened equal-color picker accepted the old gesture's sample.\n",
               "sampler-host.picker.close-reopen-refuses-old-gesture"))
        return 5;
    _chrome->CloseColorPicker(false);

    // Scope cleanup restores the exact pre-test app document, path and palette after all owners acknowledged.
    std::printf("PASS sampler-host.fixture.restore-scheduled\n");
    return 0;
}

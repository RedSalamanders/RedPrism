#include "Application.h"

#include "ChromeHost.h"
#include "Runtime.Tests.ColorWorkTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "Tests/Support/Support.Tests.HistoryWorkFixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>

int Application::RunHueReadbackHostSelfTest() noexcept
{
    if (!_chrome || !_session.Document() || !_hostSourceRegistered || _session.HasColorEdit())
        return 5;
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    if (!settled())
        return 5;
    const CanvasDocument original = _session.Document().value();
    const auto originalActive = _session.ActiveLayerID();
    const auto originalPath = _documentPath;
    const bool originalEnabled = _hueReadbackEnabled;
    std::uint32_t captures = 0;
    _hueCapturesForTest = &captures;
    _hueReadbackEnabled = true;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_hueReadbackTestGate)
                (void)SetEvent(_hueReadbackTestGate->release.get());
            CancelHostContinuations();
            if (!settled())
                std::_Exit(5);
            _hueReadbackTestGate.reset();
            _hueCapturesForTest = nullptr;
            _hueReadbackEnabled = originalEnabled;
            CancelColorEditForTest();
            ReplaceDocument(original, originalActive, originalPath);
            SyncChrome();
        });
    const auto check = [](bool condition, const char* label) noexcept
    {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
        return condition;
    };
    // Literal #3366CC has hue 220 degrees; #804020 has hue 20. The other pixels exercise
    // the unchanged transparent and saturation <= .02 refusal. No implementation-derived oracle.
    constexpr std::array<std::uint8_t, 16> bgra{0xCC, 0x66, 0x33, 0xFF, 0x20, 0x40, 0x80, 0xFF,
                                                0,    0,    0,    0,    0x80, 0x80, 0x80, 0xFF};
    const Raster raster = Raster::Make(4, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 4;
    fixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Hue original composite fixture", {4, 1});
    const auto active = layer.id;
    layer.asset = RasterAsset{raster, {}, layer.name};
    fixture.layers.push_back(std::move(layer));
    ReplaceDocument(std::move(fixture).Seal(), active, {});
    if (!settled())
        return 5;
    const auto graphClear = [this]() noexcept
    {
        return !_hueReadbackTicket && !_hueReadbackStamp && !_hueInput.Busy() && !_historyWork.Busy() &&
               _gpuWork.LentCount() == 0 && _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() &&
               !_utilityReadback.HasPending() && !_recordedCommands.HasPending() &&
               _recordedCommands.AttachedSourceCountOwnerForTest() == 0 && _jobs.ReservedMemoryBytes() == 0;
    };
    const auto openPanel = [this, &settled](bool targeting) noexcept
    {
        CancelColorEditForTest();
        if (!settled() || !BeginColorEdit(ChromeHost::kCommandHueSaturation))
            return false;
        auto settings = _session.PendingColorEdit()->settings.hsv;
        settings.range = ColorRange::Reds;
        _session.UpdateHueSaturation(settings, true);
        _session.SetHueTargeting(targeting);
        if (!targeting)
            _session.SetHueSampleMode(HueSampleMode::Replace);
        DocumentChanged();
        return settled();
    };
    const auto receiptForPress = [this]() noexcept
    {
        const auto* entry = _hostRequests.Size() ? _hostRequests.PendingAt(_hostRequests.Size() - 1) : nullptr;
        return entry ? entry->input.Receipt() : ContinuationReceipt::Failure(E_UNEXPECTED);
    };
    if (!openPanel(false))
        return 5;
    const auto admissionOpening = _hueInputOpening;
    const auto admissionCaptures = captures;
    const auto admissionOrdinal = HistoryWorkFixture::NextValue(_historyWork);
    if (!check(QueueHuePressOwner({.2, .2}, 0, static_cast<HostHueInput::Purpose>(255), HueSampleMode::Replace) &&
                   _hostRequests.Size() == 0 && !_activeHostRequest && !_hueInput.Busy() && !_hueReadbackTicket &&
                   _hueInputOpening == admissionOpening && captures == admissionCaptures &&
                   HistoryWorkFixture::NextValue(_historyWork) == admissionOrdinal,
               "hue-admission.invalid-purpose-refuses-before-queue-and-opening"))
        return 5;
    if (!check(QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, static_cast<HueSampleMode>(255)) &&
                   _hostRequests.Size() == 0 && !_activeHostRequest && !_hueInput.Busy() && !_hueReadbackTicket &&
                   _hueInputOpening == admissionOpening && captures == admissionCaptures &&
                   HistoryWorkFixture::NextValue(_historyWork) == admissionOrdinal,
               "hue-admission.invalid-mode-refuses-before-queue-and-opening"))
        return 5;
    const auto revision = _session.history.CurrentRevision();
    const int undo = _session.history.UndoCount();
    const auto source = _session.ActiveLayer()->asset.value().image;
    const auto beforeCaptures = captures;
    if (!QueueHuePressOwner({0.2, 0.2}, 100, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto rangeReceipt = receiptForPress();
    if (!check(_hueInput.Busy() && _hueInput.Opening() == admissionOpening + 1 &&
                   _hueInputOpening == admissionOpening + 1 && !_hueReadbackTicket && captures == beforeCaptures &&
                   rangeReceipt.Read().outcome == HostOutcome::Pending,
               "hue-host.range.press-is-scalar-descriptor-before-source-capture"))
        return 5;
    if (!settled())
        return 5;
    const auto band = _session.PendingColorEdit()->settings.hsv.bands[static_cast<std::size_t>(ColorRange::Reds)];
    if (!check(rangeReceipt.Read().outcome == HostOutcome::Succeeded && captures == beforeCaptures + 1 &&
                   band == HueBand{175, 205, 235, 265},
               "hue-host.range.actual-one-pixel-original-composite-hue-220-band"))
        return 5;
    if (!check(graphClear() && _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undo &&
                   _session.ActiveLayer()->asset.value().image == source,
               "hue-host.range.joined-graph-retired-source-history-unchanged"))
        return 5;
    for (unsigned int x : {2u, 3u})
    {
        if (!openPanel(false))
            return 5;
        const auto unchanged = _session.PendingColorEdit()->settings;
        if (!QueueHuePressOwner({static_cast<double>(x) + .2, .2}, 0, HostHueInput::Purpose::RangeSample,
                                HueSampleMode::Replace) ||
            !settled())
            return 5;
        if (!check(graphClear() && _session.PendingColorEdit()->settings == unchanged,
                   x == 2 ? "hue-host.range.transparent-noop" : "hue-host.range.neutral-noop"))
            return 5;
    }
    // Hold the real original job only after its actual GPU graph has retired. Physical moves and
    // release stay in the fixed FIFO; no owner pixel copy or target-drag publication is possible yet.
    if (!openPanel(true))
        return 5;
    _hueReadbackTestGate = std::make_shared<ColorWorkTestGate>();
    const auto gate = _hueReadbackTestGate;
    const Vec2 physicalStart = _viewport.ViewFromDocument({.2, .2}, DocumentSize());
    DxUi::PointerEvent physical;
    physical.action = DxUi::PointerAction::Down;
    if (!gate->entered || !gate->release || !AdjustPointer(physical, physicalStart, true))
        return 5;
    const auto targetedReceipt = receiptForPress();
    const auto opening = _hueInput.Opening();
    if (!WaitForColorWorkTestEvent(_jobs, gate->entered.get(), DispatchMessageThunk, this))
        return 5;
    if (!check(_hueReadbackTicket && _historyWork.Busy() && gate->captureReservationHeld.load() &&
                   gate->reservationBytes != 0 && _jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                   !_session.PendingColorEdit()->targetDrag && _gpuWork.LentCount() == 0 &&
                   _recordedCommands.AttachedSourceCountOwnerForTest() == 0,
               "hue-host.target.wait-retains-original-admission-after-graphics-join"))
        return 5;
    if (!check(AdjustPointer(physical, physicalStart, true) && _hueInput.Opening() == opening &&
                   _hueInput.PendingCount() == 1,
               "hue-host.target.pending-press-consumed-without-tool-or-second-opening"))
        return 5;
    physical.action = DxUi::PointerAction::Move;
    if (!DragPointer(physical, {physicalStart.x + 30, physicalStart.y}))
        return 5;
    physical.modifiers = MK_CONTROL;
    if (!DragPointer(physical, {physicalStart.x - 20, physicalStart.y}))
        return 5;
    physical.action = DxUi::PointerAction::Up;
    physical.modifiers = 0;
    if (!DragPointer(physical, {physicalStart.x + 999, physicalStart.y}))
        return 5;
    if (!check(_hueInput.PendingCount() == 4 && !_session.PendingColorEdit()->targetDrag &&
                   targetedReceipt.Read().outcome == HostOutcome::Pending,
               "hue-host.target.physical-delta-control-and-separate-release-buffered"))
        return 5;
    (void)SetEvent(gate->release.get());
    if (!settled())
        return 5;
    _hueReadbackTestGate.reset();
    const auto target = _session.PendingColorEdit()->settings.hsv;
    const auto adjustment = target.adjustments[static_cast<std::size_t>(ColorRange::Blues)];
    if (!check(targetedReceipt.Read().outcome == HostOutcome::Succeeded && target.range == ColorRange::Blues &&
                   adjustment.saturation == 15 && adjustment.hue == -10 && !_session.PendingColorEdit()->targetDrag,
               "hue-host.target.literal-blue-range-physical-saturation-15-hue-minus10-no-release-sample"))
        return 5;
    if (!check(graphClear(), "hue-host.target.terminal-acknowledged-original-graph-zero"))
        return 5;
    if (!openPanel(false))
        return 5;
    const auto unchanged = _session.PendingColorEdit()->settings;
    const auto cancelledCaptures = captures;
    if (!QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto queuedCancel = receiptForPress();
    CancelHostContinuations();
    if (!check(settled() && queuedCancel.Read().outcome == HostOutcome::Cancelled && captures == cancelledCaptures &&
                   graphClear() && _session.PendingColorEdit()->settings == unchanged,
               "hue-host.cancel.queued-before-capture-preserves-panel-and-no-source"))
        return 5;
    if (!QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto selectedCancel = receiptForPress();
    if (!RunUiActions(_jobs) || !_hueReadbackTicket)
        return 5;
    CancelHostContinuations();
    if (!check(settled() && selectedCancel.Read().outcome == HostOutcome::Cancelled && graphClear() &&
                   _session.PendingColorEdit()->settings == unchanged,
               "hue-host.cancel.selected-original-ticket-acknowledges-before-refund"))
        return 5;
    // A live acknowledged target retains only fixed scalar input; no readback graph or polling
    // survives the initial sample. Later physical input rearms the original continuation source.
    if (!openPanel(true) ||
        !QueueHuePressOwner({.2, .2}, 100, HostHueInput::Purpose::Targeting, HueSampleMode::Replace) || !settled())
        return 5;
    const auto liveOpening = _hueInput.Opening();
    if (!check(_hueAcquiredOpening == liveOpening && _hueInput.Sampling() && _session.PendingColorEdit()->targetDrag &&
                   !_hueReadbackTicket && !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 &&
                   !_utilityReadback.HasPending(),
               "hue-host.live.acknowledged-target-keeps-only-scalar-input"))
        return 5;
    if (!check(!RunUiActions(_jobs) && !_jobs.HasUiWork(),
               "hue-host.live.idle-target-disarms-without-dummy-worker-or-poll"))
        return 5;
    RecordHueMoveOwner(liveOpening, 148, false);
    if (!settled())
        return 5;
    const auto liveAdjustment =
        _session.PendingColorEdit()->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)];
    if (!check(_hueInput.Sampling() && liveAdjustment.saturation == 24 && liveAdjustment.hue == 0,
               "hue-host.live.late-physical-move-rearms-and-applies-literal-delta"))
        return 5;
    SealHueInputOwner(liveOpening, true);
    if (!check(settled() && graphClear() && !_session.PendingColorEdit()->targetDrag &&
                   _session.PendingColorEdit()
                           ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                           .saturation == 24,
               "hue-host.live.capture-loss-keeps-accepted-prefix-without-release-coordinate"))
        return 5;

    if (!openPanel(true) ||
        !QueueHuePressOwner({.2, .2}, 100, HostHueInput::Purpose::Targeting, HueSampleMode::Replace) || !settled())
        return 5;
    {
        const auto exhaustedOpening = _hueInput.Opening();
        const auto originalNext = HistoryWorkFixture::NextValue(_historyWork);
        const auto restoreClock = wil::scope_exit(
            [&]() noexcept
            {
                if (!HistoryWorkFixture::SetNext(_historyWork, originalNext))
                    std::_Exit(5);
            });
        if (!HistoryWorkFixture::SetNext(_historyWork, (std::numeric_limits<std::uint64_t>::max)() - 1))
            return 5;
        RecordHueMoveOwner(exhaustedOpening, 150, false); // Accepted physical MAX owns saturation 25.
        RecordHueMoveOwner(exhaustedOpening, 999, true);  // No ordinal: preserve the exact older FIFO.
        if (!check(_hueCloseAfterPrefix && _hueInput.PendingCount() == 1 &&
                       HistoryWorkFixture::NextValue(_historyWork) == (std::numeric_limits<std::uint64_t>::max)(),
                   "hue-host.clock.exhaustion-retains-accepted-max-sample-without-wrap"))
            return 5;
        if (!check(settled() && graphClear() && !_session.PendingColorEdit()->targetDrag &&
                       _session.PendingColorEdit()
                               ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                               .saturation == 25 &&
                       _session.PendingColorEdit()
                               ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                               .hue == 0,
                   "hue-host.clock.accepted-max-prefix-applies-before-exact-caller-drop"))
            return 5;
    }

    // Capacity exhaustion seals a separate Lost terminal. Its rejected coordinate/modifier may
    // close the accepted prefix, but must never become another value update.
    if (!openPanel(true))
        return 5;
    _hueReadbackTestGate = std::make_shared<ColorWorkTestGate>();
    const auto overflowGate = _hueReadbackTestGate;
    if (!QueueHuePressOwner({.2, .2}, 100, HostHueInput::Purpose::Targeting, HueSampleMode::Replace))
        return 5;
    const auto overflowOpening = _hueInput.Opening();
    const auto overflowReceipt = receiptForPress();
    if (!WaitForColorWorkTestEvent(_jobs, overflowGate->entered.get(), DispatchMessageThunk, this))
        return 5;
    for (std::size_t index = 1; index < HostHueInput::kCapacity; ++index)
        RecordHueMoveOwner(overflowOpening, 100.0 + static_cast<double>(index), false);
    RecordHueMoveOwner(overflowOpening, -9999, true);
    if (!check(_hueInput.Overflowed() && !_hueInput.Sampling() &&
                   _hueInput.PendingCount() == HostHueInput::kCapacity + 1 &&
                   overflowReceipt.Read().outcome == HostOutcome::Pending && !_session.PendingColorEdit()->targetDrag,
               "hue-host.capacity.full-accepted-prefix-and-separate-lost-terminal-retained"))
        return 5;
    (void)SetEvent(overflowGate->release.get());
    if (!check(settled() && graphClear() && overflowReceipt.Read().outcome == HostOutcome::Succeeded &&
                   _session.PendingColorEdit()
                           ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                           .saturation == 100 &&
                   _session.PendingColorEdit()
                           ->settings.hsv.adjustments[static_cast<std::size_t>(ColorRange::Blues)]
                           .hue == 0 &&
                   !_session.PendingColorEdit()->targetDrag,
               "hue-host.capacity.replay-preserves-accepted-prefix-without-rejected-hue-update"))
        return 5;
    _hueReadbackTestGate.reset();

    // A failed actual detached worker publishes neither its sampled range nor partial drag input.
    if (!openPanel(false))
        return 5;
    const auto failedSettings = _session.PendingColorEdit()->settings;
    _hueReadbackTestGate = std::make_shared<ColorWorkTestGate>();
    const auto failedGate = _hueReadbackTestGate;
    failedGate->releasedResult = {JobStatus::Failed, E_FAIL};
    (void)SetEvent(failedGate->release.get());
    if (!QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto failedReceipt = receiptForPress();
    if (!check(settled() && failedReceipt.Read().outcome == HostOutcome::Failed &&
                   failedReceipt.Read().error == static_cast<std::int32_t>(E_FAIL) && graphClear() &&
                   _session.PendingColorEdit()->settings == failedSettings && failedGate->visits.load() == 1,
               "hue-host.failure.actual-worker-failure-preserves-panel-and-original-error"))
        return 5;
    _hueReadbackTestGate.reset();

    // Reopen an equal-valued panel while the older original job is held after GPU retirement.
    // Its immutable source still retires normally, but it has no authority over the new panel.
    if (!openPanel(false))
        return 5;
    const auto stalePanelOpening = _session.PendingColorEdit()->openingSequence;
    _hueReadbackTestGate = std::make_shared<ColorWorkTestGate>();
    const auto staleGate = _hueReadbackTestGate;
    if (!QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto stalePanelReceipt = receiptForPress();
    if (!WaitForColorWorkTestEvent(_jobs, staleGate->entered.get(), DispatchMessageThunk, this))
        return 5;
    _session.CancelColorEdit();
    _session.BeginColorEdit(FilterKind::HueSaturation, true);
    _session.SetHueSampleMode(HueSampleMode::Replace);
    const auto newPanelOpening = _session.PendingColorEdit()->openingSequence;
    const auto newPanelSettings = _session.PendingColorEdit()->settings;
    (void)SetEvent(staleGate->release.get());
    if (!check(settled() && stalePanelOpening != newPanelOpening &&
                   stalePanelReceipt.Read().outcome == HostOutcome::Stale && graphClear() &&
                   _session.PendingColorEdit()->openingSequence == newPanelOpening &&
                   _session.PendingColorEdit()->settings == newPanelSettings,
               "hue-host.stale.equal-panel-reopen-refuses-old-scalar-result"))
        return 5;
    _hueReadbackTestGate.reset();

    // The real Cancel button hides the panel immediately but cannot refund or publish the older
    // held job until its worker acknowledges. Explicit cancellation outranks its hidden-card guard.
    if (!openPanel(false))
        return 5;
    const auto cancelledPanelOpening = _session.PendingColorEdit()->openingSequence;
    _hueReadbackTestGate = std::make_shared<ColorWorkTestGate>();
    const auto panelCancelGate = _hueReadbackTestGate;
    if (!QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto panelSampleReceipt = receiptForPress();
    if (!WaitForColorWorkTestEvent(_jobs, panelCancelGate->entered.get(), DispatchMessageThunk, this))
        return 5;
    const auto panelCancelReceipt = QueuePanelCancel(cancelledPanelOpening);
    if (!check(panelCancelReceipt.Read().outcome == HostOutcome::Pending &&
                   panelSampleReceipt.Read().outcome == HostOutcome::Pending && ColorCancelPending() &&
                   _hueReadbackTicket && _historyWork.Busy() &&
                   _jobs.ReservedMemoryBytes() >= panelCancelGate->reservationBytes,
               "hue-host.cancel.panel-close-retains-original-pending-ticket-and-charge"))
        return 5;
    (void)SetEvent(panelCancelGate->release.get());
    if (!check(settled() && panelSampleReceipt.Read().outcome == HostOutcome::Cancelled &&
                   panelCancelReceipt.Read().outcome == HostOutcome::Succeeded && !_session.HasColorEdit() &&
                   graphClear() && !ColorCancelPending(),
               "hue-host.cancel.hidden-card-cancellation-keeps-exact-outcome-until-acknowledgement"))
        return 5;
    _hueReadbackTestGate.reset();

    // Deliberately bypass Application replacement cancellation to reach TakeReady's stale-descriptor
    // retirement. This is a state-machine control, not an assertion about a physical replacement route.
    if (!openPanel(false) ||
        !QueueHuePressOwner({.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace))
        return 5;
    const auto staleQueued = receiptForPress();
    const auto staleCaptures = captures;
    _session.ReplaceDocument(_session.Document().value(), active);
    if (!check(settled() && staleQueued.Read().outcome == HostOutcome::Stale && captures == staleCaptures &&
                   graphClear() && !ColorControlsFrozen(),
               "hue-host.stale.queued-document-replacement-retires-exact-input-opening"))
        return 5;
    if (!openPanel(false) ||
        !QueueHuePressOwner({1.2, .2}, 0, HostHueInput::Purpose::RangeSample, HueSampleMode::Replace) || !settled())
        return 5;
    const auto reused = _session.PendingColorEdit()->settings.hsv.bands[static_cast<std::size_t>(ColorRange::Reds)];
    if (!check(graphClear() && reused == HueBand{335, 5, 35, 65},
               "hue-host.stale.reusable-opening-literal-brown-hue-20"))
        return 5;
    return 0;
}

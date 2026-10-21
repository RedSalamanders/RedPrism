#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "WandLayerHostTestGate.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <utility>

namespace
{
class OccupyWandHostTestSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};

constexpr Vec2 kClick{1.2, 1.2};
constexpr std::array<std::uint8_t, 16> kSourcePixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                     70, 80, 90, 255, 100, 110, 120, 255};

DocumentSelection ExpectedWandOutline()
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{1, 1}, Vec2{2, 1}, Vec2{2, 2}, Vec2{1, 2}}}};
}

DocumentSelection ExpectedLatestOutline()
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{2, 2}, Vec2{3, 2}, Vec2{3, 3}, Vec2{2, 3}}}};
}

WandSettings CapturedWandSettings()
{
    WandSettings settings;
    settings.tolerance = 0;
    settings.sampleSize = WandSampleSize::Point;
    settings.contiguous = true;
    settings.sampleAllLayers = false;
    return settings;
}

} // namespace

int Application::RunWandLayerHostSelfTest() noexcept
{
    enum class Mode
    {
        LiteralAndCapture,
        BooleanOrdered,
        NoOpRedo,
        NoWorkInputs,
        NoMatch,
        OrderedDeselectUndo,
        CoalesceAdjacent,
        CoalesceFullRing,
        OrdinalGap,
        StaleActive,
        StaleMask,
        Cancel,
        WorkerFailure,
        CaptureFailure,
        HistoryEntryFailure,
        ViewAndWindowPump,
        Admission,
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::array<ContinuationReceipt, 10> queued;
        unsigned int queuedCount = 0;
        std::size_t queueSizeAtGate = 0;
        bool gateVisited = false;
        bool gateHeld = false;
        bool cancelledWhilePending = false;
        bool viewChangedWhilePending = false;
        bool windowMessagePending = false;
        bool windowMessagePosted = false, interveningOrdinalAllocated = false;
        unsigned int windowMessages = 0;
        bool sourceAndHistoryUnchangedAtGate = false;
        bool adjacentCoalescedAtGate = false;
        bool fullRingCoalescedAtGate = false;
        bool ordinalGapRetainedAtGate = false;
        bool booleansQueuedWithoutMutation = false;
        std::optional<CanvasDocument> before;
        std::optional<DocumentSelection> selectionBefore;
        DocumentHistory::StateView historyBefore;
        std::size_t futureCount = 0;
        Guid revision;
        std::uint64_t generation = 0;
        double zoomBefore = 0;

        ContinuationReceipt& NextQueued() noexcept
        {
            if (queuedCount >= queued.size())
            {
                std::fprintf(stderr, "Wand host fixture exceeded its receipt capacity\n");
                std::abort();
            }
            return queued[queuedCount++];
        }
    };

    constexpr UINT kWindowMessage = WM_APP + 0x4c8;
    int checks = 0;
    int failures = 0;
    const auto expect =
        [&checks, &failures](const char* mode, const char* check, bool condition, const char* detail) noexcept
    {
        ++checks;
        std::fprintf(stderr, "%s wand-layer-host.%s.%s%s%s\n", condition ? "PASS" : "FAIL", mode, check,
                     condition ? "" : ": ", condition ? "" : detail);
        failures += condition ? 0 : 1;
        return condition;
    };

    const WandSettings originalSettings = _session.wandSettings;
    const bool originalAntialiased = _session.selectionAntialiased;
    const CanvasViewport originalViewport = _viewport;
    const auto restore = wil::scope_exit(
        [this, originalSettings, originalAntialiased, originalViewport]() noexcept
        {
            _session.wandSettings = originalSettings;
            _session.selectionAntialiased = originalAntialiased;
            _viewport = originalViewport;
            _wandLayerHostTestGate.reset();
            _wandOutputLifetimeTestGate.reset();
        });

    const auto prepare = [this]()
    {
        _session.ClearDocument();
        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 4;

        ImageLayer active = ImageLayer::Blank(L"Wand host source", Vec2{2, 2});
        const Guid activeId = active.id;
        active.asset = RasterAsset{
            Raster::Make(2, 2, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size()), {}, active.name};
        active.visible = false;
        active.opacity = 0.0;
        active.transform.origin = Vec2{1, 1};
        active.transform.size = Vec2{2, 2};
        active.transform.sampling = LayerSampling::Nearest;
        constexpr std::array<std::uint8_t, 4> hiddenMask{0, 0, 0, 0};
        const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, hiddenMask.data(), hiddenMask.size());
        active.mask = LayerMask{RasterAsset{mask, mask, L"Hidden proof mask"}, true, std::nullopt, true};
        document.layers.push_back(std::move(active));

        ImageLayer other = ImageLayer::Blank(L"Wand host other", Vec2{1, 1});
        constexpr std::array<std::uint8_t, 4> otherPixel{250, 250, 250, 255};
        other.asset = RasterAsset{
            Raster::Make(1, 1, RasterFormat::Bgra8888, otherPixel.data(), otherPixel.size()), {}, other.name};
        other.transform.origin = Vec2{3, 3};
        other.transform.size = Vec2{1, 1};
        other.transform.sampling = LayerSampling::Nearest;
        document.layers.push_back(std::move(other));

        _session.ReplaceDocument(std::move(document).Seal(), activeId);
        _session.wandSettings = CapturedWandSettings();
        _session.selectionAntialiased = false;
        _session.history.Reset();
        _session.history.MarkSaved();
        DocumentChanged();
        _viewport.zoom = 1.0;
        _viewport.followsFit = false;
    };

    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == WandLayerHostTestGate::kMessage)
        {
            const auto gate = owner._wandLayerHostTestGate;
            if (!gate)
                return;
            state.gateVisited = true;
            state.gateHeld = owner._wandLayerHostTicket.has_value() && owner._wandLayerHostStamp.has_value() &&
                             owner._historyWork.Busy() && owner._jobs.ReservedMemoryBytes() >= gate->reservationBytes &&
                             state.receipt.Read().outcome == HostOutcome::Pending && gate->captures.load() == 1;
            state.sourceAndHistoryUnchangedAtGate = owner._session.Document() == state.before &&
                                                    owner._session.history.CurrentRevision() == state.revision &&
                                                    owner._session.Generation() == state.generation;

            if (state.mode == Mode::BooleanOrdered)
            {
                owner.RunMagicWand(Vec2{2.2, 1.2}, SelectionMode::Add);
                if (const auto* first = owner._hostRequests.PendingAt(0))
                    state.NextQueued() = first->input.Receipt();
                owner.RunMagicWand(Vec2{2.2, 1.2}, SelectionMode::Subtract);
                if (const auto* second = owner._hostRequests.PendingAt(1))
                    state.NextQueued() = second->input.Receipt();
                state.booleansQueuedWithoutMutation = owner._hostRequests.Size() == 2 && state.queuedCount == 2 &&
                                                      state.queued[0].Read().outcome == HostOutcome::Pending &&
                                                      state.queued[1].Read().outcome == HostOutcome::Pending &&
                                                      owner._session.Document() == state.before &&
                                                      owner._session.history.CurrentRevision() == state.revision;
                owner._wandLayerHostTestGate.reset();
            }
            else if (state.mode == Mode::OrderedDeselectUndo)
            {
                state.NextQueued() = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandDeselect});
                state.NextQueued() = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                owner._wandLayerHostTestGate.reset();
            }
            else if (state.mode == Mode::CoalesceAdjacent)
            {
                state.NextQueued() = owner.QueueHostInput(HostWandLayerNewInput{kClick, CapturedWandSettings(), false});
                state.NextQueued() =
                    owner.QueueHostInput(HostWandLayerNewInput{Vec2{2.2, 2.2}, CapturedWandSettings(), false});
                state.queueSizeAtGate = owner._hostRequests.Size();
                state.adjacentCoalescedAtGate = state.queueSizeAtGate == 1 &&
                                                state.queued[0].Read().outcome == HostOutcome::Cancelled &&
                                                state.queued[1].Read().outcome == HostOutcome::Pending;
                owner._wandLayerHostTestGate.reset();
            }
            else if (state.mode == Mode::CoalesceFullRing)
            {
                for (unsigned int index = 0; index < 7; ++index)
                    state.NextQueued() = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandZoomIn});
                state.NextQueued() = owner.QueueHostInput(HostWandLayerNewInput{kClick, CapturedWandSettings(), false});
                state.NextQueued() =
                    owner.QueueHostInput(HostWandLayerNewInput{Vec2{2.2, 2.2}, CapturedWandSettings(), false});
                state.queueSizeAtGate = owner._hostRequests.Size();
                state.fullRingCoalescedAtGate = state.queueSizeAtGate == 8 &&
                                                state.queued[7].Read().outcome == HostOutcome::Cancelled &&
                                                state.queued[8].Read().outcome == HostOutcome::Pending;
                owner._wandLayerHostTestGate.reset();
            }
            else if (state.mode == Mode::OrdinalGap)
            {
                state.NextQueued() = owner.QueueHostInput(HostWandLayerNewInput{kClick, CapturedWandSettings(), false});
                // A recorded stroke can consume the shared ordinal without occupying the command ring.
                // Exercise that gap directly: the two queue entries themselves remain adjacent WandNew.
                state.interveningOrdinalAllocated = owner._historyWork.NextOrdinal().has_value();
                state.NextQueued() =
                    owner.QueueHostInput(HostWandLayerNewInput{Vec2{2.2, 2.2}, CapturedWandSettings(), false});
                state.queueSizeAtGate = owner._hostRequests.Size();
                state.ordinalGapRetainedAtGate = state.interveningOrdinalAllocated && state.queueSizeAtGate == 2 &&
                                                 state.queued[0].Read().outcome == HostOutcome::Pending &&
                                                 state.queued[1].Read().outcome == HostOutcome::Pending;
                owner._wandLayerHostTestGate.reset();
            }
            else if (state.mode == Mode::StaleActive)
            {
                const auto document = owner._session.Document();
                if (document && document->layers.size() > 1)
                    owner._session.SelectLayer(document->layers[1].id);
            }
            else if (state.mode == Mode::StaleMask)
            {
                const auto active = owner._session.ActiveLayerID();
                if (active)
                    owner._session.SelectLayerTarget(active.value(), true);
            }
            else if (state.mode == Mode::Cancel)
            {
                owner.CancelHostContinuations();
                state.cancelledWhilePending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                              owner._historyWork.Busy() && owner._wandLayerHostTicket.has_value();
            }
            else if (state.mode == Mode::ViewAndWindowPump)
            {
                state.zoomBefore = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.viewChangedWhilePending =
                    owner._viewport.zoom != state.zoomBefore && !owner._viewport.followsFit &&
                    state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
                state.windowMessagePosted = PostMessageW(owner._window.get(), kWindowMessage, 0, 0) != FALSE;
                if (!state.windowMessagePosted)
                    (void)SetEvent(gate->release.get());
                return;
            }
            (void)SetEvent(gate->release.get());
            return;
        }
        if (message.hwnd && message.message == kWindowMessage)
        {
            ++state.windowMessages;
            state.windowMessagePending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                         owner._historyWork.Busy() && owner._wandLayerHostTicket.has_value();
            // The worker cannot acknowledge until this actual window message has been observed.
            if (owner._wandLayerHostTestGate)
                (void)SetEvent(owner._wandLayerHostTestGate->release.get());
        }
        owner.DispatchQueuedMessage(message);
    };

    const auto name = [](Mode mode) noexcept -> const char*
    {
        switch (mode)
        {
        case Mode::LiteralAndCapture:
            return "literal-capture";
        case Mode::BooleanOrdered:
            return "boolean-ordered";
        case Mode::NoOpRedo:
            return "noop-redo";
        case Mode::NoWorkInputs:
            return "no-work-inputs";
        case Mode::NoMatch:
            return "no-match";
        case Mode::OrderedDeselectUndo:
            return "ordered-deselect-undo";
        case Mode::CoalesceAdjacent:
            return "coalesce-adjacent";
        case Mode::CoalesceFullRing:
            return "coalesce-full-ring";
        case Mode::OrdinalGap:
            return "ordinal-gap";
        case Mode::StaleActive:
            return "stale-active";
        case Mode::StaleMask:
            return "stale-mask";
        case Mode::Cancel:
            return "cancel";
        case Mode::WorkerFailure:
            return "worker-failure";
        case Mode::CaptureFailure:
            return "capture-failure";
        case Mode::HistoryEntryFailure:
            return "history-entry-failure";
        case Mode::ViewAndWindowPump:
            return "view-window-pump";
        case Mode::Admission:
            return "admission";
        }
        return "unknown";
    };

    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    if (!settle())
        return 5;

    constexpr std::array modes{Mode::LiteralAndCapture, Mode::BooleanOrdered,   Mode::NoOpRedo,
                               Mode::NoWorkInputs,      Mode::NoMatch,          Mode::OrderedDeselectUndo,
                               Mode::CoalesceAdjacent,  Mode::CoalesceFullRing, Mode::OrdinalGap,
                               Mode::StaleActive,       Mode::StaleMask,        Mode::Cancel,
                               Mode::WorkerFailure,     Mode::CaptureFailure,   Mode::HistoryEntryFailure,
                               Mode::ViewAndWindowPump, Mode::Admission};

    for (const Mode mode : modes)
    {
        if (mode == Mode::NoWorkInputs)
        {
            prepare();
            _session.SetSelection(ExpectedWandOutline(), L"Preserved outside-click outline");
            const auto noWork = [this, &expect](Vec2 point, const char* check)
            {
                const auto before = _session.Document();
                const auto history = _session.history.CaptureState();
                const auto revision = _session.history.CurrentRevision();
                const auto generation = _session.Generation();
                const auto gate = std::make_shared<WandLayerHostTestGate>();
                gate->hold = false;
                _wandLayerHostTestGate = gate;
                _wandOutputLifetimeTestGate = gate;
                const auto receipt = QueueHostInput(HostWandLayerNewInput{point, CapturedWandSettings(), false});
                const bool pending = receipt.Read().outcome == HostOutcome::Pending;
                const auto settled = SettleJobs(_jobs, 15000, DispatchMessageThunk, this);
                (void)expect("no-work-inputs", check,
                             pending && settled == SettleResult::Settled &&
                                 receipt.Read().outcome == HostOutcome::Succeeded && !_wandLayerHostTicket &&
                                 !_wandLayerHostStamp && !_historyWork.Busy() && _jobs.ReservedMemoryBytes() == 0 &&
                                 _hostRequests.Size() == 0 && !_activeHostRequest && gate->reservationBytes == 0 &&
                                 gate->captures.load() == 0 && !gate->retired.load() && _session.Document() == before &&
                                 _session.Generation() == generation &&
                                 _session.history.CurrentRevision() == revision && _session.history.IsCurrent(history),
                             "no-work host receipt changed geometry/history or admitted a wand worker");
                _wandLayerHostTestGate.reset();
            };
            noWork(Vec2{-0.1, 1}, "outside-is-silent-noop");
            noWork(Vec2{std::numeric_limits<double>::quiet_NaN(), 1}, "nonfinite-is-silent-noop");
            _session.ClearDocument();
            noWork(kClick, "empty-document-is-silent-noop");
            if (failures != 0)
                return 5;
            continue;
        }

        if (mode == Mode::Admission)
        {
            prepare();
            const auto before = _session.Document();
            const auto history = _session.history.CaptureState();
            const auto revision = _session.history.CurrentRevision();
            const auto generation = _session.Generation();
            const auto gate = std::make_shared<WandLayerHostTestGate>();
            gate->hold = false;
            _wandLayerHostTestGate = gate;
            _wandOutputLifetimeTestGate = gate;
            // Worker completion alone does not recycle records: only an owner drain frees these slots.
            for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
            {
                std::unique_ptr<Job> job = std::make_unique<OccupyWandHostTestSlot>();
                JobTicket ticket;
                if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
                    return 5;
            }
            const auto refused = QueueHostInput(HostWandLayerNewInput{kClick, CapturedWandSettings(), false});
            for (unsigned int pass = 0; pass < 4 && refused.Read().outcome == HostOutcome::Pending; ++pass)
                (void)RunUiActions(_jobs);
            (void)expect(name(mode), "actual-full-record-table-refuses-before-capture",
                         refused.Read().outcome == HostOutcome::Failed &&
                             refused.Read().error ==
                                 static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                             !_wandLayerHostTicket && !_wandLayerHostStamp && !_historyWork.Busy() &&
                             gate->reservationBytes != 0 && gate->captures.load() == 0 && !gate->retired.load() &&
                             _session.Document() == before && _session.Generation() == generation &&
                             _session.history.CurrentRevision() == revision && _session.history.IsCurrent(history),
                         "actual scheduler refusal captured pixels or partially published selection/history");
            if (!settle() || failures != 0)
                return 5;
            const auto recovery = QueueHostInput(HostWandLayerNewInput{kClick, CapturedWandSettings(), false});
            (void)expect(name(mode), "fresh-request-recovers-after-drain",
                         settle() && recovery.Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedWandOutline()) &&
                             gate->captures.load() == 1 && gate->reservationHeld.load() && gate->retired.load() &&
                             gate->retiredOffOwner.load() && !_wandLayerHostTicket && !_wandLayerHostStamp &&
                             !_historyWork.Busy() && gate->publishedOutputs == 1 && gate->publishedOutputBytes > 0 &&
                             gate->allOutputsBoundToOriginal &&
                             _jobs.ReservedMemoryBytes() == gate->publishedOutputBytes && _hostRequests.Size() == 0 &&
                             !_activeHostRequest,
                         "fresh wand request failed or retained ownership after actual submission refusal");
            _session.ClearDocument();
            DocumentChanged();
            (void)expect(name(mode), "recovery-real-close-refunds-output",
                         settle() && !_session.Document() && _session.history.UndoCount() == 0 &&
                             _jobs.ReservedMemoryBytes() == 0,
                         "closing the recovered document/history did not acknowledge its actual output release");
            _wandLayerHostTestGate.reset();
            _wandOutputLifetimeTestGate.reset();
            if (failures != 0)
                return 5;
            continue;
        }

        prepare();
        if (mode == Mode::NoOpRedo)
        {
            _session.SetSelection(ExpectedWandOutline(), L"Seed matching wand outline");
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}},
                L"Future outline");
            _session.Undo();
        }
        if (mode == Mode::NoMatch)
        {
            _session.SetSelection(
                DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}},
                L"Seed for no match");
            _session.wandSettings.sampleSize = WandSampleSize::ThreeByThree;
        }

        auto gate = std::make_shared<WandLayerHostTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!gate->release)
            return 5;
        if (mode == Mode::WorkerFailure)
            gate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (mode == Mode::CaptureFailure)
            gate->throwCaptureFailure = true;
        if (mode == Mode::HistoryEntryFailure)
            gate->throwHistoryStage = DocumentHistory::PrepareStage::Entry;
        _wandLayerHostTestGate = gate;
        _wandOutputLifetimeTestGate = gate;

        State state{*this, mode};
        state.before = _session.Document();
        state.historyBefore = _session.history.CaptureState();
        state.futureCount = state.historyBefore.FutureCount();
        state.revision = _session.history.CurrentRevision();
        state.generation = _session.Generation();
        if (mode == Mode::LiteralAndCapture || mode == Mode::NoOpRedo || mode == Mode::NoMatch)
        {
            state.selectionBefore = _session.Selection();
            RunMagicWand(kClick, SelectionMode::New);
            const auto* front = _hostRequests.Front();
            if (front)
                state.receipt = front->input.Receipt();
            const bool queuedNoInlineChange =
                front && std::holds_alternative<HostWandLayerNewInput>(front->input.Input()) &&
                state.receipt.Read().outcome == HostOutcome::Pending && _session.Document() == state.before &&
                _session.Selection() == state.selectionBefore && _session.history.CurrentRevision() == state.revision &&
                _session.Generation() == state.generation;
            (void)expect(name(mode), "run-magic-wand-queues-without-inline-publication", queuedNoInlineChange,
                         "RunMagicWand did not enqueue the real New request or mutated document/history inline");
            if (mode == Mode::LiteralAndCapture)
            {
                _session.wandSettings = WandSettings{};
                _session.wandSettings.sampleSize = WandSampleSize::FiveByFive;
                _session.wandSettings.tolerance = 255;
                _session.wandSettings.sampleAllLayers = true;
                _session.selectionAntialiased = true;
            }
            (void)RunUiActions(_jobs);
        }
        else
        {
            state.receipt = QueueHostInput(HostWandLayerNewInput{kClick, _session.wandSettings, false});
            (void)RunUiActions(_jobs);
        }

        if (mode == Mode::ViewAndWindowPump)
        {
            _viewport.zoom = 1.0;
            _viewport.followsFit = false;
        }

        const SettleResult settled = SettleJobs(_jobs, 15000, pump, &state);
        (void)expect(name(mode), "bounded-settlement", settled == SettleResult::Settled,
                     "SettleJobs did not acknowledge the worker and queued host work within 15 seconds");
        const bool expectedGate = mode != Mode::CaptureFailure && mode != Mode::HistoryEntryFailure;
        if (mode != Mode::NoWorkInputs)
        {
            (void)expect(name(mode), "prepared-worker-held-under-reservation",
                         expectedGate
                             ? state.gateVisited && state.gateHeld && state.sourceAndHistoryUnchangedAtGate
                             : !state.gateVisited && gate->captures.load() == (mode == Mode::CaptureFailure ? 0u : 1u),
                         "worker gate, owner capture reservation, or pre-publication state did not match the case");
        }

        if (mode == Mode::NoOpRedo)
        {
            (void)expect(name(mode), "redo-preserved-no-op",
                         state.receipt.Read().outcome == HostOutcome::Succeeded && _session.history.CanRedo() &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.Generation() == state.generation &&
                             _session.history.RedoName() == L"Future outline" && state.futureCount > 0 &&
                             _session.history.CaptureState().FutureCount() == state.futureCount,
                         "same-outline wand discarded redo or changed revision/generation");
        }
        else if (mode == Mode::NoMatch)
        {
            (void)expect(name(mode), "valid-no-match-deselects",
                         state.receipt.Read().outcome == HostOutcome::Succeeded && !_session.Selection() &&
                             _session.history.UndoName() == L"Deselect",
                         "valid unmatched sample did not publish the named deselection");
        }
        else if (mode == Mode::LiteralAndCapture)
        {
            const auto* points = _chrome->SelectionOutlineForTest();
            (void)expect(name(mode), "prepared-point-buffers-adopted-without-owner-copy",
                         gate->outlinePreparedOffOwner.load() && points && points->size() == 1 &&
                             (*points)[0].size() == 4 && (*points)[0].data() == gate->preparedPointBuffer.load() &&
                             (*points)[0][0].x == 0 && (*points)[0][0].y == 0,
                         "worker numeric output was rebuilt, copied or lost at actual owner publication");
            const auto document = _session.Document();
            (void)expect(name(mode), "literal-clockwise-outline-and-one-entry",
                         state.receipt.Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedWandOutline()) &&
                             document && state.before && document->layers == state.before->layers &&
                             _session.history.UndoName() == L"Magic Wand" && _session.history.UndoCount() == 1,
                         "captured nearest-point settings did not produce the literal 1x1 outline and one entry");
            (void)expect(name(mode), "capture-used-event-time-settings-and-aa",
                         gate->captures.load() == 1 && gate->retired.load() && gate->retiredOffOwner.load(),
                         "owner capture or off-owner aggregate retirement did not occur exactly as expected");
        }
        else if (mode == Mode::BooleanOrdered)
        {
            // Exact coordinates and ordering from the independently compiled pinned Clipper2 2.0.1.
            // PreserveCollinear keeps six union vertices; Subtract starts at the lower-right corner.
            const DocumentSelection expectedSubtract{
                false, SelectionLoops{SelectionLoop{Vec2{2, 2}, Vec2{1, 2}, Vec2{1, 1}, Vec2{2, 1}}}};
            (void)expect(name(mode), "real-add-subtract-inputs-queue-fifo",
                         state.booleansQueuedWithoutMutation && state.queuedCount == 2 &&
                             state.receipt.Read().outcome == HostOutcome::Succeeded &&
                             state.queued[0].Read().outcome == HostOutcome::Succeeded &&
                             state.queued[1].Read().outcome == HostOutcome::Succeeded,
                         "real Add/Subtract inputs mutated inline, coalesced, reordered or failed");
            (void)expect(name(mode), "literal-final-subtraction-and-three-history-entries",
                         _session.Selection() == std::optional<DocumentSelection>(expectedSubtract) &&
                             _session.history.UndoCount() == 3 && _session.history.UndoName() == L"Magic Wand" &&
                             _session.history.CurrentRevision() != state.revision,
                         "New/Add/Subtract did not produce the independent final rectangle and three entries");
            const auto undo = [&]()
            {
                const auto receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                return settle() && receipt.Read().outcome == HostOutcome::Succeeded;
            };
            const auto redo = [&]()
            {
                const auto receipt = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
                return settle() && receipt.Read().outcome == HostOutcome::Succeeded;
            };
            const DocumentSelection expectedUnion{
                false,
                SelectionLoops{SelectionLoop{Vec2{2, 1}, Vec2{3, 1}, Vec2{3, 2}, Vec2{2, 2}, Vec2{1, 2}, Vec2{1, 1}}}};
            const auto matchesUnion = [&]()
            { return _session.Selection() == std::optional<DocumentSelection>(expectedUnion); };
            (void)expect(name(mode), "undo-subtract-restores-literal-union",
                         undo() && matchesUnion() && _session.history.UndoCount() == 2,
                         "actual Undo did not restore the independent clockwise union rectangle");
            (void)expect(name(mode), "undo-add-restores-original-new-selection",
                         undo() && _session.Selection() == std::optional<DocumentSelection>(ExpectedWandOutline()) &&
                             _session.history.UndoCount() == 1,
                         "actual Undo Add did not restore the original New result");
            (void)expect(name(mode), "undo-new-restores-saved-revision-and-empty-selection",
                         undo() && !_session.Selection() && _session.history.CurrentRevision() == state.revision &&
                             _session.history.UndoCount() == 0 && _session.history.CanRedo(),
                         "actual Undo New did not restore the saved empty document revision");
            const bool restoredThree = [&]()
            {
                for (int entry = 0; entry < 3; ++entry)
                    if (!redo())
                        return false;
                return true;
            }();
            (void)expect(name(mode), "redo-three-restores-literal-subtract-result",
                         restoredThree && _session.Selection() == std::optional<DocumentSelection>(expectedSubtract) &&
                             _session.history.UndoCount() == 3 && !_session.history.CanRedo(),
                         "actual ordered Redo did not restore all three bounded boolean publications");
        }
        else if (mode == Mode::OrderedDeselectUndo)
        {
            (void)expect(name(mode), "younger-deselect-then-undo",
                         state.receipt.Read().outcome == HostOutcome::Succeeded && state.queuedCount == 2 &&
                             state.queued[0].Read().outcome == HostOutcome::Succeeded &&
                             state.queued[1].Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedWandOutline()),
                         "younger ordered Deselect and Undo did not restore the wand result");
        }
        else if (mode == Mode::CoalesceAdjacent)
        {
            (void)expect(name(mode), "adjacent-wand-retires-superseded-receipt",
                         state.gateVisited && state.queuedCount == 2 && state.adjacentCoalescedAtGate &&
                             state.queued[1].Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedLatestOutline()),
                         "adjacent same-document WandNew requests did not coalesce and cancel the old receipt");
        }
        else if (mode == Mode::CoalesceFullRing)
        {
            (void)expect(name(mode), "full-ring-tail-wand-coalesces",
                         state.gateVisited && state.queuedCount == 9 && state.fullRingCoalescedAtGate &&
                             state.queued[8].Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedLatestOutline()),
                         "full eight-entry ring did not replace its adjacent tail WandNew entry");
        }
        else if (mode == Mode::OrdinalGap)
        {
            (void)expect(name(mode), "intervening-ordinal-prevents-coalescing",
                         state.gateVisited && state.queuedCount == 2 && state.ordinalGapRetainedAtGate &&
                             state.queued[0].Read().outcome == HostOutcome::Succeeded &&
                             state.queued[1].Read().outcome == HostOutcome::Succeeded &&
                             _session.Selection() == std::optional<DocumentSelection>(ExpectedLatestOutline()),
                         "an intervening shared ordinal failed to preserve both adjacent WandNew requests");
        }
        else if (mode == Mode::StaleActive || mode == Mode::StaleMask)
        {
            const bool staleAuthority = mode == Mode::StaleActive
                                            ? _session.ActiveLayerID() == std::optional(state.before->layers[1].id)
                                            : _session.IsMaskSelected();
            (void)expect(name(mode), "held-authority-change-drops-publication",
                         state.gateVisited && staleAuthority && state.receipt.Read().outcome == HostOutcome::Stale &&
                             _session.Selection() == state.before->selection,
                         "captured active layer or mask target changed without a stale result");
        }
        else if (mode == Mode::Cancel)
        {
            (void)expect(name(mode), "receipt-retained-through-cancel-ack",
                         state.cancelledWhilePending && state.receipt.Read().outcome == HostOutcome::Cancelled &&
                             !_wandLayerHostTicket && !_wandLayerHostStamp && !_historyWork.Busy(),
                         "cancellation completed before acknowledgement or retained host ownership afterward");
        }
        else if (mode == Mode::WorkerFailure)
        {
            (void)expect(name(mode), "worker-error-is-preserved",
                         state.gateVisited && state.receipt.Read().outcome == HostOutcome::Failed &&
                             state.receipt.Read().error == static_cast<std::int32_t>(E_ACCESSDENIED) &&
                             _session.Selection() == state.before->selection,
                         "worker E_ACCESSDENIED was lost or partially published");
        }
        else if (mode == Mode::CaptureFailure)
        {
            (void)expect(name(mode), "capture-bad-alloc-fails-without-publication",
                         !state.gateVisited && state.receipt.Read().outcome == HostOutcome::Failed &&
                             state.receipt.Read().error == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
                             gate->captures.load() == 0 && _session.Selection() == state.before->selection,
                         "owner capture bad_alloc was not translated to E_OUTOFMEMORY");
        }
        else if (mode == Mode::HistoryEntryFailure)
        {
            (void)expect(name(mode), "reached-entry-bad-alloc-fails-atomically",
                         !state.gateVisited && gate->historyFaultHits.load() == 1 &&
                             state.receipt.Read().outcome == HostOutcome::Failed &&
                             state.receipt.Read().error == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
                             _session.Selection() == state.before->selection,
                         "the reached Entry allocation fault did not fail atomically with E_OUTOFMEMORY");
        }
        else if (mode == Mode::ViewAndWindowPump)
        {
            (void)expect(name(mode), "direct-zoom-remains-responsive-while-pending",
                         state.gateVisited && state.viewChangedWhilePending &&
                             state.receipt.Read().outcome == HostOutcome::Succeeded,
                         "direct view command waited on or changed the pending wand operation");
            (void)expect(name(mode), "posted-window-message-pumped-while-pending",
                         state.windowMessagePosted && state.windowMessages == 1 && state.windowMessagePending,
                         "posted window message was not dispatched while the wand receipt remained pending");
        }

        const bool refusesPublication = mode == Mode::StaleActive || mode == Mode::StaleMask || mode == Mode::Cancel ||
                                        mode == Mode::WorkerFailure || mode == Mode::CaptureFailure ||
                                        mode == Mode::HistoryEntryFailure;
        if (refusesPublication)
            (void)expect(name(mode), "whole-document-history-and-generation-unchanged",
                         _session.Document() == state.before && _session.Generation() == state.generation &&
                             _session.history.CurrentRevision() == state.revision &&
                             _session.history.IsCurrent(state.historyBefore),
                         "a refused publication changed document bytes, generation, revision or history epoch");
        (void)expect(name(mode), "reserved-capture-and-aggregate-retirement",
                     gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load(),
                     "capture lacked its full reservation or the complete payload retired on the owner");

        (void)expect(name(mode), "owner-work-released",
                     !_wandLayerHostTicket && !_wandLayerHostStamp && !_historyWork.Busy() &&
                         _jobs.ReservedMemoryBytes() == gate->publishedOutputBytes && _hostRequests.Size() == 0 &&
                         !_activeHostRequest,
                     "wand ticket, history stamp, queue entry, active request, or reservation remained owned");
        const bool publishes = !refusesPublication && mode != Mode::NoOpRedo;
        const unsigned int expectedOutputs =
            !publishes ? 0u
            : mode == Mode::BooleanOrdered
                ? 3u
                : (mode == Mode::CoalesceAdjacent || mode == Mode::CoalesceFullRing || mode == Mode::OrdinalGap ? 2u
                                                                                                                : 1u);
        (void)expect(name(mode), "exact-live-output-charge-and-original-ticket",
                     gate->publishedOutputs == expectedOutputs && gate->allOutputsBoundToOriginal &&
                         (publishes ? gate->publishedOutputBytes > 0 : gate->publishedOutputBytes == 0) &&
                         _jobs.ReservedMemoryBytes() == gate->publishedOutputBytes,
                     "successful outputs were not charged to their original tickets, or a no-op/refusal leaked charge");

        std::optional<SelectionLoops> reader;
        std::weak_ptr<const void> weakGeometry;
        if (mode == Mode::LiteralAndCapture && _session.Selection())
        {
            reader = _session.Selection()->loops;
            weakGeometry = reader->WeakStorageForTest();
        }
        // Release actual session/history/display owners after every original outcome and numeric assertion.
        state.before.reset();
        state.selectionBefore.reset();
        state.historyBefore = {};
        _session.ClearDocument();
        DocumentChanged();
        (void)expect(name(mode), "actual-document-history-close",
                     settle() && !_session.Document() && _session.history.UndoCount() == 0 &&
                         !_session.history.CanRedo(),
                     "real document/history teardown did not settle");
        if (reader)
        {
            (void)expect(name(mode), "copied-geometry-reader-retains-original-output",
                         reader.value() == ExpectedWandOutline().loops && !weakGeometry.expired() &&
                             _jobs.ReservedMemoryBytes() == gate->publishedOutputBytes,
                         "copied literal geometry lost its original charge or bytes after actual Close");
            reader.reset();
            (void)expect(name(mode), "last-strong-retired-but-weak-control-stays-charged",
                         settle() && weakGeometry.expired() &&
                             _jobs.ReservedMemoryBytes() == gate->publishedOutputBytes,
                         "a surviving weak control lost the charge before actual allocator deallocation");
            std::thread lastWeak([weak = std::move(weakGeometry)]() mutable { weak.reset(); });
            lastWeak.join();
        }
        (void)expect(name(mode), "real-last-owner-and-ack-refund-output", settle() && _jobs.ReservedMemoryBytes() == 0,
                     "real last-owner destruction and acknowledgement did not refund the exact output charge");
        _wandOutputLifetimeTestGate.reset();
        _wandLayerHostTestGate.reset();
        if (failures != 0)
        {
            (void)settle();
            return 5;
        }
    }

    // Exercise the actual ordinary command handler, not a direct prepared-session or Wand input substitute.
    int loadedChecks = 0;
    constexpr std::array loadModes{Mode::LiteralAndCapture, Mode::StaleActive,    Mode::Cancel,
                                   Mode::WorkerFailure,     Mode::CaptureFailure, Mode::HistoryEntryFailure,
                                   Mode::Admission};
    for (const bool mask : {false, true})
    {
        for (const auto mode : loadModes)
        {
            prepare();
            auto gate = std::make_shared<WandLayerHostTestGate>();
            gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!gate->release)
                return 5;
            if (mode == Mode::WorkerFailure)
                gate->result = {JobStatus::Failed, E_ACCESSDENIED};
            if (mode == Mode::CaptureFailure)
                gate->throwCaptureFailure = true;
            if (mode == Mode::HistoryEntryFailure)
                gate->throwHistoryStage = DocumentHistory::PrepareStage::Entry;
            _wandLayerHostTestGate = gate;
            _wandOutputLifetimeTestGate = gate;
            State state{*this, mode};
            state.before = _session.Document();
            state.historyBefore = _session.history.CaptureState();
            state.revision = _session.history.CurrentRevision();
            state.generation = _session.Generation();
            const auto check = [&](const char* label, bool condition, const char* detail) noexcept
            {
                ++loadedChecks;
                failures += condition ? 0 : 1;
                std::fprintf(stderr, "%s loaded-selection-host.%s.%s.%s%s%s\n", condition ? "PASS" : "FAIL",
                             mask ? "mask" : "alpha", name(mode), label, condition ? "" : ": ",
                             condition ? "" : detail);
            };
            if (mode == Mode::Admission)
            {
                for (std::size_t index = 0; index < JobScheduler::kCapacity; ++index)
                {
                    std::unique_ptr<Job> job = std::make_unique<OccupyWandHostTestSlot>();
                    JobTicket ticket;
                    if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
                        return 5;
                }
            }
            const int command = mask ? ChromeHost::kCommandLoadMaskSelection : ChromeHost::kCommandLoadLayerSelection;
            OnCommand(command);
            const auto* front = _hostRequests.Front();
            if (front)
                state.receipt = front->input.Receipt();
            check("ordinary-command-queues-original-intent-with-no-inline-mutation",
                  front && std::get_if<HostCommandInput>(&front->input.Input()) &&
                      std::get<HostCommandInput>(front->input.Input()).command == command &&
                      state.receipt.Read().outcome == HostOutcome::Pending && gate->captures.load() == 0 &&
                      _session.Document() == state.before && _session.Generation() == state.generation &&
                      _session.history.CurrentRevision() == state.revision,
                  "the real command did not queue its typed input or mutated document/history inline");
            if (mode == Mode::Admission)
            {
                for (unsigned int pass = 0; pass < 4 && state.receipt.Read().outcome == HostOutcome::Pending; ++pass)
                    (void)RunUiActions(_jobs);
                check("full-original-record-table-refuses-before-capture",
                      state.receipt.Read().outcome == HostOutcome::Failed &&
                          state.receipt.Read().error ==
                              static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
                          gate->captures.load() == 0 && !gate->retired.load() && !_historyWork.Busy() &&
                          !_wandLayerHostTicket && !_wandLayerHostStamp && _session.Document() == state.before &&
                          _session.Generation() == state.generation && _session.history.IsCurrent(state.historyBefore),
                      "scheduler refusal captured a source or partially published selection/history");
            }
            const auto settled = SettleJobs(_jobs, 15000, pump, &state);
            check("actual-owner-pump-joins-original-job", settled == SettleResult::Settled,
                  "the original job or its acknowledgement did not finish");
            const bool held = mode == Mode::LiteralAndCapture || mode == Mode::StaleActive || mode == Mode::Cancel ||
                              mode == Mode::WorkerFailure;
            check("worker-boundary-retains-original-authority-and-source",
                  held ? state.gateVisited && state.gateHeld && state.sourceAndHistoryUnchangedAtGate &&
                             gate->captures.load() == 1 && gate->reservationHeld.load() &&
                             gate->outlinePreparedOffOwner.load()
                       : !state.gateVisited,
                  "a held worker lost its original ticket, reservation or unmodified before-document");
            const auto outcome = state.receipt.Read().outcome;
            check("terminal-receipt-matches-operation",
                  mode == Mode::LiteralAndCapture ? outcome == HostOutcome::Succeeded
                  : mode == Mode::StaleActive     ? outcome == HostOutcome::Stale
                  : mode == Mode::Cancel          ? outcome == HostOutcome::Cancelled
                                                  : outcome == HostOutcome::Failed,
                  "the typed command lost its success, stale, cancellation or failure outcome");
            if (mode == Mode::LiteralAndCapture)
            {
                const auto& selection = _session.Selection();
                const std::array corners{Vec2{1, 1}, Vec2{3, 1}, Vec2{3, 3}, Vec2{1, 3}};
                bool literal = selection && !selection->antialiased && selection->loops.size() == 1 &&
                               selection->loops.Get()[0].size() == 4;
                if (literal)
                {
                    const auto& loop = selection->loops.Get()[0];
                    literal = false;
                    for (std::size_t start = 0; start < corners.size(); ++start)
                    {
                        bool match = true;
                        for (std::size_t point = 0; point < corners.size(); ++point)
                            match = match && loop[(start + point) % corners.size()] == corners[point];
                        literal = literal || match;
                    }
                }
                check("literal-hidden-source-outline-and-one-history-entry",
                      literal && _session.history.UndoCount() == 1 &&
                          _session.history.UndoName() == (mask ? L"Load Mask Selection" : L"Load Layer Selection") &&
                          gate->publishedOutputs == 1 && gate->publishedOutputBytes != 0 &&
                          gate->allOutputsBoundToOriginal,
                      "load did not publish the independent full source rectangle and one named history entry");
                const auto undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                check("ordered-undo-restores-absent-selection-and-saved-state",
                      settle() && undo.Read().outcome == HostOutcome::Succeeded && !_session.Selection() &&
                          !_session.history.IsModified(),
                      "actual Undo did not restore the original saved state");
            }
            else
                check("non-success-publishes-no-selection-or-history",
                      _session.Document() == state.before && _session.Generation() == state.generation &&
                          _session.history.IsCurrent(state.historyBefore) && gate->publishedOutputs == 0,
                      "a failed, cancelled, stale or refused command changed pixels, selection or history");
            check("original-job-and-barrier-retire-after-acknowledgement",
                  !_wandLayerHostTicket && !_wandLayerHostStamp && !_historyWork.Busy() && !_activeHostRequest &&
                      _hostRequests.Size() == 0 &&
                      (mode == Mode::Admission || (gate->retired.load() && gate->retiredOffOwner.load())),
                  "original command ownership survived its acknowledged terminal boundary");
            _session.ClearDocument();
            state.before.reset();
            state.historyBefore = {};
            DocumentChanged();
            check("real-close-refunds-complete-original-output-charge",
                  settle() && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0,
                  "last real source/history/outline ownership did not refund the original reservation");
            _wandLayerHostTestGate.reset();
            _wandOutputLifetimeTestGate.reset();
            if (failures != 0)
                return 5;
        }
    }
    std::fprintf(stderr, "loaded-selection-host summary: %d checks, 14 cases\n", loadedChecks);

    if (!settle() || _historyWork.Busy() || _wandLayerHostTicket || _wandLayerHostStamp ||
        _jobs.ReservedMemoryBytes() != 0 || _hostRequests.Size() != 0 || _activeHostRequest)
    {
        (void)expect("cleanup", "bounded-final-settlement", false,
                     "final acknowledgement left queued or borrowed worker ownership");
        return 5;
    }
    _wandLayerHostTestGate.reset();
    (void)restore;
    std::fprintf(stderr, "PASS wand-layer-host.summary.count checks=%d modes=%zu\n", checks, modes.size());
    return 0;
}

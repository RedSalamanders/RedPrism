#include "Application.h"

#include "ChromeHost.h"
#include "TransformCancellationWork.h"
#include "TransformPreviewTestGate.h"
#include "TransformPreviewWork.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

int Application::RunTransformPreviewSelfTest() noexcept
{
    enum class Action : unsigned char
    {
        View,
        LatestTicks,
        Cancel,
        ReplaceSource,
        ReplaceHistory,
        QueueUndo,
        QueueApply,
        CancelReopen
    };
    struct State final
    {
        Application& owner;
        std::shared_ptr<TransformPreviewTestGate> gate;
        Action action = Action::View;
        CanvasDocument document;
        const CanvasDocument* display = nullptr;
        Raster source;
        Raster shown;
        Raster clipboardImage;
        Vec2 clipboardOrigin{};
        std::uint64_t documentSerial = 0;
        std::uint64_t generation = 0;
        Guid historyRevision;
        std::uint64_t clipboardSerial = 0;
        std::uint64_t draftRevision = 0;
        LayerTransform draft;
        std::uint64_t presents = 0;
        int errors = 0;
        bool visited = false;
        bool reserved = false;
        bool responsive = false;
        bool queued = false;
        bool displayRetained = false;
        bool clipboardPresent = false;
        std::optional<ContinuationReceipt> receipt;
    };

    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s transform-preview-host.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto closeDeferred = [&]() noexcept
    {
        if (!_session.PendingTransformEdit())
            return true;
        auto input = _session.PrepareTransformCancellationInput();
        if (!input)
            return false;
        auto prepared = DocumentSession::PrepareTransformCancellationPublication(std::move(input.value()));
        return prepared && _session.ApplyPreparedTransformCancellationIfCurrent(prepared.value());
    };
    const auto terminal = [&]() noexcept
    {
        return !_transformPreviewTicket && !_transformPreviews.HasPending() && !_jobs.ReservedMemoryBytes() &&
               !_jobs.ReleasingMemoryBytes();
    };
    const auto prepare = [&]() noexcept
    {
        if (!settle() || !closeDeferred())
            return false;
        if (_transformPreviewMailboxOpen)
            CancelTransformPreviewJobs();
        if (!settle())
            return false;
        constexpr std::array<std::uint8_t, 16> pixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                      71, 81, 91, 255, 101, 111, 121, 255};
        constexpr std::array<std::uint8_t, 4> maskPixels{255, 128, 64, 0};
        const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, maskPixels.data(), maskPixels.size());
        if (!image.HasStorage() || !mask.HasStorage())
            return false;
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 8;
        document.height = 8;
        document.selection = DocumentSelection{false, SelectionLoops{{Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}}};
        ImageLayer layer = ImageLayer::Blank(L"Preview source", Vec2{2, 2});
        layer.asset = RasterAsset{image, image, layer.name};
        layer.mask = LayerMask{RasterAsset{mask, mask, L"Linked mask"}, true, std::nullopt, true};
        layer.transform.sampling = LayerSampling::Nearest;
        const Guid target = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document), target);
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
        DocumentChanged();
        if (!_session.BeginDeferredTransform())
            return false;
        _session.BeginDistort();
        auto corners = _session.PendingTransformEdit()->corners.value();
        corners[1].x = 5;
        corners[2].x = 5;
        corners[2].y = 5;
        corners[3].y = 5;
        _session.PreviewCorners(corners);
        return _session.PendingTransformEdit() && _session.PendingTransformEdit()->deferred && _session.ActiveLayer() &&
               _session.ActiveLayer()->asset && _session.ActiveLayer()->asset->image == image;
    };
    const auto snapshot = [&](State& state)
    {
        state.document = _session.Document().value();
        state.display = _session.DisplayDocument();
        state.shown = state.display ? state.display->layers.front().asset->image : Raster{};
        state.source = _session.ActiveLayer()->asset->image;
        state.documentSerial = _session.DocumentSerial();
        state.generation = _session.Generation();
        state.historyRevision = _session.history.CurrentRevision();
        state.clipboardSerial = _session.ClipboardSerial();
        state.clipboardPresent = _session.Clipboard().has_value();
        if (state.clipboardPresent)
        {
            state.clipboardImage = _session.Clipboard()->image;
            state.clipboardOrigin = _session.Clipboard()->origin;
        }
        state.draftRevision = _session.TransformDraftRevision();
        state.draft = _session.PendingTransformEdit()->draft;
        state.presents = _renderer.PresentCount();
        state.errors = _errorsShown;
    };
    const auto dispatch = [](void* value, void* native) noexcept
    {
        auto& state = *static_cast<State*>(value);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != TransformPreviewTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        const bool firstVisit = !state.visited;
        state.visited = true;
        if (firstVisit)
        {
            state.reserved = state.gate->reservationBytes != 0 &&
                             owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes &&
                             state.gate->captureReservationHeld;
            state.displayRetained = owner._session.DisplayDocument() == state.display;
            const double zoom = owner._viewport.zoom;
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            state.responsive = owner._viewport.zoom != zoom;
            owner._viewport.SetZoom(1, owner._viewport.Center(), owner.DocumentSize());
            switch (state.action)
            {
            case Action::View:
                break;
            case Action::LatestTicks:
                state.queued = state.gate->visits.load() == 1;
                for (const double right : {6.0, 7.0, 8.0})
                {
                    auto corners = owner._session.PendingTransformEdit()->corners.value();
                    corners[1].x = right;
                    corners[2].x = right;
                    owner._session.PreviewCorners(corners);
                    owner.DocumentChanged();
                    owner.RefreshTransformPreviewJobs();
                }
                state.responsive = state.responsive && owner._session.DisplayDocument() == state.display;
                state.displayRetained = state.displayRetained && owner._session.DisplayDocument() == state.display;
                break;
            case Action::Cancel:
                owner.CancelTransformPreviewJobs();
                state.queued = owner._transformPreviewTicket.has_value() && owner._jobs.ReservedMemoryBytes() != 0;
                break;
            case Action::CancelReopen:
                owner.CancelTransformPreviewJobs();
                owner.DocumentChanged();
                owner.RefreshTransformPreviewJobs();
                state.queued = owner._transformPreviewMailboxOpen && owner._transformPreviewTicket.has_value() &&
                               state.gate->visits.load() == 1;
                break;
            case Action::ReplaceSource:
            {
                owner._session.EditForTest([](CanvasDocument& document) { document.layers.front().opacity = 0.5; });
                owner.DocumentChanged();
                owner
                    .CancelTransformPreviewJobs(); // Refuse the captured source; next-epoch refresh is a separate case.
                break;
            }
            case Action::ReplaceHistory:
                owner._session.history.Reset();
                owner.DocumentChanged();
                break;
            case Action::QueueUndo:
                state.receipt = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.queued = state.receipt.has_value();
                break;
            case Action::QueueApply:
                state.receipt = owner.QueueHostInput(HostTransformFinishInput{
                    owner._session.ActiveLayerID().value(), owner._session.TransformDraftRevision(), false});
                state.queued = state.receipt.has_value();
                break;
            }
        }
        (void)SetEvent(state.gate->release.get());
    };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_transformPreviewTestGate)
                (void)SetEvent(_transformPreviewTestGate->release.get());
            if (!settle())
                std::_Exit(5);
            _transformPreviewTestGate.reset();
            if (_transformPreviewMailboxOpen)
                CancelTransformPreviewJobs();
            if (!settle() || !closeDeferred() || !settle())
                std::_Exit(5);
        });

    if (!prepare())
        return 5;
    expect(_session.PendingTransformEdit()->opening != 0, "fixture-has-opening-epoch");
    expect(_session.ActiveLayer()->asset->image.Width() == 2 && _session.ActiveLayer()->asset->image.Height() == 2,
           "fixture-uses-literal-two-by-two-image");
    expect(_session.ActiveLayer()->mask && _session.ActiveLayer()->mask->linked, "fixture-uses-linked-literal-mask");
    expect(!_session.DisplayDocument() || _session.DisplayDocument() == &_session.Document().value(),
           "deferred-start-does-not-expose-partial-display");

    for (const bool after : {false, true})
    {
        if (!prepare())
            return 5;
        auto value = std::make_shared<TransformPreviewTestGate>();
        if (!value->release)
            return 5;
        value->afterPreparation = after;
        _transformPreviewTestGate = value;
        State state{*this, value};
        snapshot(state);
        RefreshTransformPreviewJobs();
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        const auto* shown = _session.DisplayDocument();
        expect(state.visited && value->visits.load() == 1,
               after ? "success-after-ready-reaches-one-gate" : "success-before-ready-reaches-one-gate");
        expect(state.reserved && value->captureReservationHeld, "snapshot-admitted-before-capture-reservation-held");
        expect(state.responsive && _viewport.zoom == 1, "view-command-pumped-and-zoom-restored-to-baseline");
        expect(shown && shown != &_session.Document().value(), "success-publishes-complete-display");
        expect(shown && shown->layers.front().asset->image.Identity() != state.source.Identity(),
               "success-display-contains-prepared-distorted-raster");
        expect(_session.Document().value() == state.document && _session.DocumentSerial() == state.documentSerial,
               "preview-success-leaves-committed-document-and-serial-exact");
        expect(_session.Generation() == state.generation, "preview-success-leaves-session-generation-exact");
        expect(_session.history.CurrentRevision() == state.historyRevision && _session.history.UndoCount() == 0,
               "preview-success-leaves-history-exact");
        expect(_session.ClipboardSerial() == state.clipboardSerial &&
                   _session.Clipboard().has_value() == state.clipboardPresent &&
                   (!_session.Clipboard() || (_session.Clipboard()->image == state.clipboardImage &&
                                              _session.Clipboard()->origin == state.clipboardOrigin)) &&
                   _renderer.PresentCount() == state.presents,
               "preview-success-keeps-exact-clipboard-and-hidden-window-does-not-present");
        expect(value->retired.load() && value->retiredOffOwner.load(), "success-payload-retires-off-owner");
        expect(_errorsShown == state.errors && terminal(),
               "success-has-no-notice-and-reaches-terminal-reservation-state");
        _transformPreviewTestGate.reset();
    }

    if (!prepare())
        return 5;
    auto latestGate = std::make_shared<TransformPreviewTestGate>();
    if (!latestGate->release)
        return 5;
    latestGate->afterPreparation = false;
    _transformPreviewTestGate = latestGate;
    State latest{*this, latestGate};
    latest.action = Action::LatestTicks;
    snapshot(latest);
    RefreshTransformPreviewJobs();
    if (SettleJobs(_jobs, 15000, dispatch, &latest) != SettleResult::Settled)
        return 5;
    expect(latest.visited && latest.queued && latestGate->visits.load() == 2,
           "tick-storm-keeps-one-active-worker-graph");
    expect(latest.displayRetained, "tick-storm-retains-last-complete-display-during-new-settings");
    expect(_session.TransformDraftRevision() > latest.draftRevision, "tick-storm-advances-draft-revision");
    expect(_session.Document().value() == latest.document &&
               _session.history.CurrentRevision() == latest.historyRevision,
           "tick-storm-keeps-document-and-history-stable");
    expect(_session.DisplayDocument() && _session.DisplayDocument()->layers.front().asset->image.Width() == 8 &&
               _session.DisplayDocument()->layers.front().asset->image.Height() == 5,
           "only-latest-settings-publish-complete-literal-preview-bounds");
    expect(_errorsShown == latest.errors && _renderer.PresentCount() == latest.presents,
           "tick-storm-has-no-error-or-hidden-present");
    _transformPreviewTestGate.reset();
    if (!settle())
        return 5;
    expect(_session.DisplayDocument() &&
               _session.DisplayDocument()->layers.front().asset->image.Identity() != latest.source.Identity(),
           "latest-pending-settings-eventually-publish-complete-display");
    expect(!_transformPreviewTicket && !_jobs.ReservedMemoryBytes(),
           "latest-pending-ack-retires-ticket-and-reservation");
    expect(latestGate->visits.load() == 2, "tick-storm-replaces-pending-work-with-only-latest-request");

    if (!prepare())
        return 5;
    auto reopenGate = std::make_shared<TransformPreviewTestGate>();
    if (!reopenGate->release)
        return 5;
    reopenGate->afterPreparation = false;
    _transformPreviewTestGate = reopenGate;
    State reopened{*this, reopenGate};
    reopened.action = Action::CancelReopen;
    snapshot(reopened);
    RefreshTransformPreviewJobs();
    if (SettleJobs(_jobs, 15000, dispatch, &reopened) != SettleResult::Settled)
        return 5;
    expect(reopened.queued, "reopen-waits-for-cancelled-epoch-ack-before-second-graph");
    expect(reopenGate->visits.load() == 2 && _transformPreviewMailboxOpen,
           "reopened-epoch-launches-once-after-old-ticket-acknowledges");
    expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->deferred,
           "reopen-keeps-current-deferred-transform");
    _transformPreviewTestGate.reset();

    for (const bool after : {false, true})
    {
        if (!prepare())
            return 5;
        auto cancelGate = std::make_shared<TransformPreviewTestGate>();
        if (!cancelGate->release)
            return 5;
        cancelGate->afterPreparation = after;
        _transformPreviewTestGate = cancelGate;
        State state{*this, cancelGate};
        state.action = Action::Cancel;
        snapshot(state);
        RefreshTransformPreviewJobs();
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        expect(state.visited && !_transformPreviewMailboxOpen, "cancel-closes-mailbox-at-gate");
        expect(state.queued, "cancel-keeps-active-ticket-and-reservation-until-ack");
        expect(!_transformPreviewTicket && terminal(), "cancel-holds-ticket-through-ack-then-retires");
        expect(_session.Document().value() == state.document &&
                   _session.history.CurrentRevision() == state.historyRevision,
               "cancel-before-or-after-ready-does-not-change-document-or-history");
        expect(_session.DisplayDocument() == state.display, "cancel-before-or-after-ready-keeps-exact-display-cache");
        expect(_session.ClipboardSerial() == state.clipboardSerial && _errorsShown == state.errors,
               "cancel-is-silent-and-preserves-clipboard");
        expect(state.gate->retiredOffOwner.load(), "cancelled-payload-retires-off-owner");
        _transformPreviewTestGate.reset();
    }

    for (const auto fault : {0, 1, 2, 3})
    {
        if (!prepare())
            return 5;
        auto faultGate = std::make_shared<TransformPreviewTestGate>();
        if (!faultGate->release)
            return 5;
        faultGate->afterPreparation = fault != 1;
        faultGate->throwAllocationFailure = fault == 0;
        faultGate->throwCaptureFailure = fault == 2;
        faultGate->throwAdmissionFailure = fault == 3;
        if (fault == 1)
            faultGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        _transformPreviewTestGate = faultGate;
        State state{*this, faultGate};
        snapshot(state);
        RefreshTransformPreviewJobs();
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        expect(_errorsShown == state.errors + 1, "worker-capture-admission-fault-posts-one-notice");
        expect(_session.Document().value() == state.document &&
                   _session.history.CurrentRevision() == state.historyRevision,
               "worker-fault-does-not-publish-document-or-history");
        expect(_session.ClipboardSerial() == state.clipboardSerial, "worker-fault-preserves-clipboard");
        expect(!_transformPreviewTicket && !_jobs.ReservedMemoryBytes(), "worker-fault-retires-ticket-and-reservation");
        if (fault != 3)
            expect(faultGate->retired.load(), "worker-fault-payload-retired");
        _transformPreviewTestGate.reset();
    }

    for (const Action stale : {Action::ReplaceSource, Action::ReplaceHistory})
    {
        if (!prepare())
            return 5;
        auto staleGate = std::make_shared<TransformPreviewTestGate>();
        if (!staleGate->release)
            return 5;
        staleGate->afterPreparation = true;
        _transformPreviewTestGate = staleGate;
        State state{*this, staleGate};
        state.action = stale;
        snapshot(state);
        RefreshTransformPreviewJobs();
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        expect(state.visited && staleGate->visits.load() == 1, "stale-authority-case-reaches-real-worker-gate");
        expect(_session.DisplayDocument() == &_session.Document().value() || !_session.DisplayDocument(),
               "stale-result-does-not-install-obsolete-preview");
        expect(!_transformPreviewTicket && !_jobs.ReservedMemoryBytes(), "stale-result-acknowledges-and-retires");
        _transformPreviewTestGate.reset();
    }

    if (!prepare())
        return 5;
    _transformPreviewTestGate.reset();
    RefreshTransformPreviewJobs();
    if (!settle() || !_session.DisplayDocument())
        return 5;
    const CanvasDocument* retained = _session.DisplayDocument();
    const Raster retainedRaster = retained->layers.front().asset->image;
    auto failingGate = std::make_shared<TransformPreviewTestGate>();
    if (!failingGate->release)
        return 5;
    failingGate->afterPreparation = false;
    failingGate->result = {JobStatus::Failed, E_ACCESSDENIED};
    _transformPreviewTestGate = failingGate;
    auto changed = _session.PendingTransformEdit()->corners.value();
    changed[1].x = 7;
    changed[2].x = 7;
    _session.PreviewCorners(changed);
    DocumentChanged();
    RefreshTransformPreviewJobs();
    State failedPreview{*this, failingGate};
    snapshot(failedPreview);
    RefreshTransformPreviewJobs();
    if (SettleJobs(_jobs, 15000, dispatch, &failedPreview) != SettleResult::Settled)
        return 5;
    expect(_session.DisplayDocument() == retained, "failed-younger-preview-retains-last-complete-document");
    expect(failedPreview.displayRetained, "failed-younger-preview-keeps-cache-while-worker-is-gated");
    expect(_session.DisplayDocument()->layers.front().asset->image == retainedRaster,
           "failed-younger-preview-retains-last-complete-raster");
    expect(_errorsShown == failedPreview.errors + 1, "failed-younger-preview-notifies-once");
    expect(_session.history.UndoCount() == 0 && _session.Document().value() == failedPreview.document,
           "failed-younger-preview-does-not-commit");
    _transformPreviewTestGate.reset();

    if (!prepare())
        return 5;
    auto orderGate = std::make_shared<TransformPreviewTestGate>();
    if (!orderGate->release)
        return 5;
    orderGate->afterPreparation = false;
    _transformPreviewTestGate = orderGate;
    State ordered{*this, orderGate};
    ordered.action = Action::QueueApply;
    snapshot(ordered);
    RefreshTransformPreviewJobs();
    if (SettleJobs(_jobs, 15000, dispatch, &ordered) != SettleResult::Settled)
        return 5;
    expect(ordered.queued && ordered.receipt && ordered.receipt->Read().outcome == HostOutcome::Succeeded,
           "younger-apply-settles-after-captured-last-geometry");
    expect(!_session.PendingTransformEdit() && _session.history.UndoCount() == 1,
           "younger-apply-publishes-one-history-entry-and-closes-draft");
    const auto* committed = _session.ActiveLayer();
    std::array<std::uint8_t, 4> firstPixel{};
    if (committed && committed->asset)
        committed->asset->image.ReadRegion(0, 0, 1, 1, firstPixel.data(), 4, 0);
    expect(committed && committed->asset && committed->asset->image != ordered.source &&
               committed->asset->image.Width() == 5 && committed->asset->image.Height() == 5 &&
               firstPixel == std::array<std::uint8_t, 4>{11, 21, 31, 255},
           "ordered-apply-commits-full-resolution-distortion-with-literal-source-pixel");
    expect(committed && committed->transform.origin == Vec2{0, 0} && committed->transform.size == Vec2{5, 5},
           "ordered-apply-commits-only-captured-last-distorted-bounds");
    _transformPreviewTestGate.reset();

    if (!prepare())
        return 5;
    auto undoGate = std::make_shared<TransformPreviewTestGate>();
    if (!undoGate->release)
        return 5;
    undoGate->afterPreparation = false;
    _transformPreviewTestGate = undoGate;
    State undo{*this, undoGate};
    undo.action = Action::QueueUndo;
    snapshot(undo);
    RefreshTransformPreviewJobs();
    if (SettleJobs(_jobs, 15000, dispatch, &undo) != SettleResult::Settled)
        return 5;
    expect(undo.queued && undo.receipt && undo.receipt->Read().outcome == HostOutcome::Succeeded,
           "younger-undo-settles-after-preview-acknowledgement");
    expect(!_session.PendingTransformEdit() && _session.history.UndoCount() == 0,
           "younger-undo-cancels-only-captured-draft-without-history");
    expect(_session.Document().value() == undo.document && _session.ActiveLayer()->asset->image == undo.source,
           "younger-undo-preserves-exact-document-and-source-raster");
    expect(!_transformPreviewTicket && !_jobs.ReservedMemoryBytes(), "younger-undo-retires-preview-ownership");

    std::printf("transform-preview-host assertions=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

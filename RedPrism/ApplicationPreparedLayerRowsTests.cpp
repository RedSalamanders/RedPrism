#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "LayerRowsTestGate.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

int Application::RunPreparedLayerRowsSelfTest() noexcept
{
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled || _preparedLayerRowsEnabled)
        return 5;
    auto saved = std::make_unique<DocumentSession>(std::move(_session));
    _session = DocumentSession{};
    const auto cleanup = wil::scope_exit(
        [this, &saved]() noexcept
        {
            _layerRowsSource.StopNotifications();
            if (_layerRowsTestGate && _layerRowsTestGate->release)
                (void)SetEvent(_layerRowsTestGate->release.get());
            if (_layerRowsTicket)
                (void)_jobs.Cancel(_layerRowsTicket.value());
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            ClearPreparedLayerRows();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _layerRowsSource.CloseAfterWorkers();
            _layerRowsTestGate.reset();
            _layerRowsRequestedKey.reset();
            _preparedLayerRowsEnabled = false;
            _chrome->SetPreparedLayerInteraction(true);
            _session = std::move(*saved);
            SyncChrome();
        });
    enum class Mode
    {
        Success,
        CancelBeforeCapture,
        Cancel,
        LateCancel,
        StaleRoot,
        Collapse,
        Replace,
        CopySession,
        CaptureFailure,
        WorkerFailure
    };
    constexpr std::array names{
        "success", "cancel-before-capture", "cancel",          "late-cancel",   "stale-root", "collapse",
        "replace", "copy-session",          "capture-failure", "worker-failure"};
    int failures = 0;
    unsigned int checks = 0;
    const auto expect = [&](bool condition, const char* mode, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s layer-rows-host.%s.%s\n", condition ? "PASS" : "FAIL", mode, name);
        std::fflush(stdout);
    };
    if (!EnablePreparedLayerRows())
        return 5;
    for (unsigned int ordinal = 0; ordinal < names.size(); ++ordinal)
    {
        const auto mode = static_cast<Mode>(ordinal);
        const auto name = names[ordinal];
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        ClearPreparedLayerRows();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        DocumentFixture fixture;
        fixture.width = 2;
        fixture.height = 1;
        fixture.id = Guid::Generate();
        ImageLayer group = ImageLayer::Blank(L"Prepared group", {2, 1});
        group.isGroup = true;
        const Guid groupID = group.id;
        ImageLayer child = ImageLayer::Blank(L"Prepared child", {2, 1});
        child.parentID = groupID;
        child.visible = false;
        const Guid childID = child.id;
        constexpr std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
        constexpr std::array<std::uint8_t, 2> mask{0, 255};
        const Raster image = Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        const Raster coverage = Raster::Make(2, 1, RasterFormat::Gray8, mask.data(), mask.size());
        child.asset = RasterAsset{image, {}, L"Original pixels"};
        child.mask = LayerMask{RasterAsset{coverage, Raster::MakeThumbnail(coverage), L"Original mask"}, true,
                               std::nullopt, true};
        fixture.layers.push_back(std::move(group));
        fixture.layers.push_back(std::move(child));
        _session.ReplaceDocument(std::move(fixture).Seal(), childID);
        _session.SelectLayerTarget(childID, true);
        const auto original = CurrentLayerRowsKey();
        const auto revision = _session.history.CurrentRevision();
        const auto gate = std::make_shared<LayerRowsTestGate>();
        gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        gate->holdAfterPrepare = mode == Mode::LateCancel;
        gate->hold = mode != Mode::CaptureFailure && mode != Mode::WorkerFailure;
        gate->throwCapture = mode == Mode::CaptureFailure;
        gate->throwPrepare = mode == Mode::WorkerFailure;
        if (!gate->release)
            return 5;
        _layerRowsTestGate = gate;
        const auto release = wil::scope_exit([gate]() noexcept { (void)SetEvent(gate->release.get()); });
        struct State final
        {
            Application& owner;
            Mode mode;
            Guid groupID, childID;
            LayerRowsKey original;
            std::shared_ptr<LayerRowsTestGate> gate;
            bool visited = false, held = false, view = false;
        } state{*this, mode, groupID, childID, original, gate};
        const auto dispatch = [](void* raw, void* native) noexcept
        {
            auto& state = *static_cast<State*>(raw);
            auto& owner = state.owner;
            const auto& message = *static_cast<MSG*>(native);
            if (message.message != LayerRowsTestGate::kMessage)
            {
                DispatchMessageThunk(&owner, native);
                return;
            }
            state.visited = true;
            state.held = owner._layerRowsTicket && !owner._preparedLayerRows && state.gate->captureReserved.load() &&
                         state.gate->captures.load() == 1 && state.gate->preparations.load() == 1 &&
                         owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes &&
                         owner.CurrentLayerRowsKey() == state.original;
            const auto zoom = owner._viewport.zoom;
            owner.OnCommand(ChromeHost::kCommandZoomIn);
            state.view = owner._viewport.zoom != zoom;
            owner._layerRowsTestGate.reset(); // The one newest replacement must not inherit this older hold.
            switch (state.mode)
            {
            case Mode::Cancel:
            case Mode::LateCancel:
                if (owner._layerRowsTicket)
                    (void)owner._jobs.Cancel(owner._layerRowsTicket.value());
                break;
            case Mode::StaleRoot:
                owner._session.RenameLayer(state.childID, L"Younger child");
                owner.SyncPreparedLayerRows();
                break;
            case Mode::Collapse:
                owner._session.SetGroupCollapsed(state.groupID, true);
                owner.SyncPreparedLayerRows();
                break;
            case Mode::Replace:
            {
                auto replacement = DocumentFixture::Copy(owner._session.Document().value());
                replacement.layers.back().name = L"Replacement child";
                owner._session.ReplaceDocument(std::move(replacement).Seal(), state.childID);
                owner.SyncPreparedLayerRows();
                break;
            }
            case Mode::CopySession:
            {
                DocumentSession copy = owner._session;
                owner._session = std::move(copy);
                owner.SyncPreparedLayerRows();
                break;
            }
            default:
                break;
            }
            (void)SetEvent(state.gate->release.get());
        };
        SyncPreparedLayerRows();
        _layerRowsSource.Pump();
        expect(_layerRowsTicket.has_value() && !gate->captures.load(), name,
               "accepted-original-ticket-precedes-owner-capture");
        if (mode == Mode::CancelBeforeCapture && _layerRowsTicket)
            expect(_jobs.Cancel(_layerRowsTicket.value()), name, "queued-cancel-accepted");
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
            return 5;
        const bool refreshed =
            mode == Mode::StaleRoot || mode == Mode::Collapse || mode == Mode::Replace || mode == Mode::CopySession;
        const bool published = mode == Mode::Success || refreshed;
        expect(!_layerRowsTicket && !_layerRowsSource.HasReady() && _jobs.IsIdle(), name,
               "original-receipt-acknowledged-and-no-idle-rearm");
        expect(gate->publications.load() == (mode == Mode::Success ? 1u : 0u), name,
               "stale-cancelled-and-failed-candidates-cannot-publish");
        expect(bool(_preparedLayerRows) == published && bool(_layerRowsCacheTicket) == published &&
                   (_jobs.ReservedMemoryBytes() != 0) == published,
               name, "active-cache-retains-the-original-memory-charge");
        if (mode != Mode::CancelBeforeCapture)
        {
            expect(gate->captureReserved.load() && gate->retired.load() && gate->retiredOffOwner.load(), name,
                   "admitted-input-retirement-is-acknowledged-off-owner");
            expect(gate->captures.load() == (mode == Mode::CaptureFailure ? 0u : 1u), name,
                   "capture-failure-publishes-no-source");
        }
        else
            expect(gate->captures.load() == 0 && gate->preparations.load() == 0, name,
                   "cancel-before-first-run-captures-no-source");
        if (mode != Mode::CaptureFailure && mode != Mode::WorkerFailure && mode != Mode::CancelBeforeCapture)
            expect(state.visited && state.held && state.view, name,
                   "real-message-dispatch-and-view-input-run-while-worker-is-held");
        if (published)
        {
            const auto current = CurrentLayerRowsKey();
            expect(_layerRowsPublishedKey == std::optional<LayerRowsKey>(current) &&
                       _preparedLayerRows->Source().layers.Identity() == current.root &&
                       _preparedLayerRows->Rows().size() == (mode == Mode::Collapse ? 1u : 2u),
                   name, "only-current-root-and-collapse-hierarchy-is-adopted");
            const auto groupItem = _chrome->LayerRowItemForTest(0);
            expect(groupItem.text == L"Prepared group" && groupItem.hasChildren &&
                       groupItem.expanded == (mode != Mode::Collapse),
                   name, "actual-pinned-tree-receives-prepared-group");
            if (mode != Mode::Collapse)
            {
                const auto row = _chrome->LayerRowItemForTest(1);
                const auto expectedName = mode == Mode::StaleRoot ? L"Younger child"
                                          : mode == Mode::Replace ? L"Replacement child"
                                                                  : L"Prepared child";
                expect(row.text == FormatStringResource(nullptr, IDS_LAYER_HIDDEN_SUFFIX, expectedName) &&
                           row.depth == 1 && row.badgeText == LoadStringResource(nullptr, IDS_LAYER_MASK_BADGE) &&
                           row.badgeTone ==
                               (mode == Mode::Replace ? DxUi::AdornmentTone::Info : DxUi::AdornmentTone::Accent),
                       name, "actual-pinned-tree-localized-hidden-mask-target-semantics");
            }
            const auto cache = _preparedLayerRows.get();
            const auto memory = _layerRowsCacheTicket;
            const auto syncs = _chrome->SyncCountersForTest().layerRows;
            SyncPreparedLayerRows();
            SyncPreparedLayerRows();
            expect(_preparedLayerRows.get() == cache && _layerRowsCacheTicket == memory && !_layerRowsTicket &&
                       _chrome->SyncCountersForTest().layerRows == syncs,
                   name, "settled-sync-reuses-borrowing-and-accounting-with-zero-row-writes");
        }
        if (mode != Mode::StaleRoot && mode != Mode::Replace)
            expect(_session.history.CurrentRevision() == revision && _session.history.UndoCount() == 0 &&
                       !_session.history.IsModified(),
                   name, "row-preparation-does-not-create-history");
        ClearPreparedLayerRows();
        if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
            return 5;
        expect(!_preparedLayerRows && !_layerRowsCacheTicket && _jobs.ReservedMemoryBytes() == 0 &&
                   _jobs.ReleasingMemoryBytes() == 0 && _chrome->LayerRowItemForTest(0).id == 0,
               name, "clear-detaches-borrowing-before-complete-worker-retirement");
        _layerRowsTestGate.reset();
    }

    // Collapse ids deliberately outlive deletion in the legacy session. More remembered ids than live layers
    // must still admit the remaining list; do not turn a legitimate deletion into a disabled empty panel.
    DocumentFixture deleted;
    deleted.id = Guid::Generate();
    deleted.width = 2;
    deleted.height = 1;
    ImageLayer first = ImageLayer::Blank(L"First deleted group", {2, 1});
    ImageLayer second = ImageLayer::Blank(L"Second deleted group", {2, 1});
    first.isGroup = true;
    second.isGroup = true;
    const Guid firstID = first.id;
    const Guid secondID = second.id;
    ImageLayer remaining = ImageLayer::Blank(L"Survivor", {2, 1});
    const Guid remainingID = remaining.id;
    deleted.layers.push_back(std::move(first));
    deleted.layers.push_back(std::move(second));
    deleted.layers.push_back(std::move(remaining));
    _session.ReplaceDocument(std::move(deleted).Seal(), remainingID);
    _session.SetGroupCollapsed(firstID, true);
    _session.SetGroupCollapsed(secondID, true);
    _session.DeleteLayer(firstID);
    _session.DeleteLayer(secondID);
    const auto deletedRevision = _session.history.CurrentRevision();
    const auto deletedUndo = _session.history.UndoCount();
    SyncPreparedLayerRows();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
        return 5;
    expect(_preparedLayerRows && _preparedLayerRows->Rows().size() == 1 && _layerRowsCacheTicket &&
               _chrome->LayerRowItemForTest(0).text == L"Survivor",
           "deleted-folders", "remembered-collapse-ids-do-not-disable-the-actual-pinned-tree");
    expect(_session.history.CurrentRevision() == deletedRevision && _session.history.UndoCount() == deletedUndo,
           "deleted-folders", "row-preparation-preserves-deletion-history");
    ClearPreparedLayerRows();
    if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
        return 5;
    expect(!_layerRowsCacheTicket && _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0,
           "deleted-folders", "surviving-list-cache-retires-with-no-accounting-leak");
    std::printf("%s layer-rows-host.summary checks=%u failures=%d\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 5 : 0;
}

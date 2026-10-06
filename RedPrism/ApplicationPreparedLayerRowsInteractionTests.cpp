#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "LayerRowsTestGate.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <memory>
#include <utility>

int Application::RunStandalonePreparedLayerRowsInteractionSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    std::array<BYTE, 256> savedKeys{};
    if (!KeyboardState::Read(savedKeys))
        return 5;
    KeyboardState::Write({});
    const auto restoreKeys = wil::scope_exit([savedKeys]() noexcept { KeyboardState::Write(savedKeys); });
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-interaction.%s.%s\n", condition ? "PASS" : "FAIL", scenario,
                    name);
        std::fflush(stdout);
    };

    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto shutdown = wil::scope_exit(
        [&]() noexcept
        {
            if (owner)
                owner->Shutdown();
        });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
    {
        SelfTestFailure(L"Prepared layer rows interaction fixture could not initialize its hidden WARP host.\n");
        return 5;
    }
    owner->BindCompositionDevice();
    expect(owner->_renderer.IsWarp() && owner->_window && owner->_chrome && owner->_jobs.WorkerCount() != 0, "fixture",
           "uses-real-hidden-WARP-application-and-scheduler");

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    ImageLayer group = ImageLayer::Blank(L"Interaction group", {2, 1});
    group.isGroup = true;
    const Guid groupId = group.id;
    ImageLayer child = ImageLayer::Blank(L"Interaction child", {2, 1});
    child.parentID = groupId;
    const Guid childId = child.id;
    ImageLayer sibling = ImageLayer::Blank(L"Interaction sibling", {2, 1});
    const Guid siblingId = sibling.id;
    constexpr std::array<std::uint8_t, 8> childPixels{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 8> siblingPixels{70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 2> childMask{0, 255};
    const Raster childImage = Raster::Make(2, 1, RasterFormat::Bgra8888, childPixels.data(), childPixels.size());
    const Raster siblingImage = Raster::Make(2, 1, RasterFormat::Bgra8888, siblingPixels.data(), siblingPixels.size());
    const Raster maskImage = Raster::Make(2, 1, RasterFormat::Gray8, childMask.data(), childMask.size());
    child.asset = RasterAsset{childImage, {}, L"Interaction child pixels"};
    child.mask = LayerMask{RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"Interaction child mask"}, true,
                           std::nullopt, true};
    sibling.asset = RasterAsset{siblingImage, {}, L"Interaction sibling pixels"};
    fixture.layers.push_back(std::move(sibling));
    fixture.layers.push_back(std::move(group));
    fixture.layers.push_back(std::move(child));
    owner->_session.ReplaceDocument(std::move(fixture).Seal(), siblingId);
    const Guid initialRevision = owner->_session.history.CurrentRevision();
    const int initialUndoCount = owner->_session.history.UndoCount();

    struct PumpState final
    {
        Application& owner;
        std::shared_ptr<LayerRowsTestGate> gate;
        Guid groupId;
        Guid childId;
        Guid siblingId;
        int staleRowX = 0;
        int staleRowY = 0;
        int staleMaskX = 0;
        int staleMaskY = 0;
        bool exerciseStaleInteractions = false;
        bool gateVisited = false;
        bool gateHeld = false;
        bool staleRowBlocked = false;
        bool staleMaskBlocked = false;
    } pump{*owner, {}, groupId, childId, siblingId};
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<PumpState*>(opaque);
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerRowsTestGate::kMessage)
        {
            state.owner.DispatchQueuedMessage(message);
            return;
        }

        state.gateVisited = true;
        state.gateHeld = state.owner._layerRowsTicket.has_value() && state.owner._preparedLayerRows &&
                         state.gate->captureReserved.load() && state.gate->captures.load() == 1 &&
                         state.gate->preparations.load() == 1 &&
                         state.owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes;
        if (state.exerciseStaleInteractions)
        {
            // These are actual client-area mouse messages. The row cache still describes the old expanded tree,
            // while SyncPreparedLayerRows has disabled that stale tree for the duration of the replacement.
            state.owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(state.staleRowX, state.staleRowY));
            state.owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(state.staleRowX, state.staleRowY));
            state.staleRowBlocked = state.owner._session.ActiveLayerID() == std::optional<Guid>(state.groupId) &&
                                    !state.owner._session.IsMaskSelected();

            std::array<BYTE, 256> keys{};
            keys[VK_SHIFT] = 0x80;
            KeyboardState::Write(keys);
            state.owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT,
                                  MAKELPARAM(state.staleMaskX, state.staleMaskY));
            state.owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(state.staleMaskX, state.staleMaskY));
            KeyboardState::Write({});
            const auto& document = state.owner._session.Document();
            state.staleMaskBlocked = document && document->layers.size() == 3 && document->layers[2].mask &&
                                     document->layers[2].mask->enabled &&
                                     state.owner._session.ActiveLayerID() == std::optional<Guid>(state.groupId) &&
                                     !state.owner._session.IsMaskSelected();
        }
        (void)SetEvent(state.gate->release.get());
    };

    // The real host sizes chrome before accepting pointer input. Publishing a selected row into an unsized
    // pinned Tree scrolls using its one-DIP fallback viewport and leaves the later row bounds offscreen.
    const UINT dpi = GetDpiForWindow(owner->_window.get());
    const float scale = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
    if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi))))
        return 5;
    if (!owner->EnablePreparedLayerRows())
    {
        SelfTestFailure(L"Prepared layer rows interaction fixture could not enable prepared rows.\n");
        return 5;
    }
    owner->_layerRowsSource.Pump();
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    owner->SyncChrome();
    if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi))))
        return 5;

    auto gate = std::make_shared<LayerRowsTestGate>();
    gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    gate->hold = true;
    gate->holdAfterPrepare = true;
    gate->reservationBytes = 1;
    if (!gate->release)
        return 5;
    owner->_layerRowsTestGate = gate;
    pump.gate = gate;
    const auto unblock = wil::scope_exit([gate]() noexcept { (void)SetEvent(gate->release.get()); });

    const auto rowsAre = [&](std::size_t count, bool collapsed) noexcept
    {
        if (!owner->_preparedLayerRows || owner->_preparedLayerRows->Rows().size() != count)
            return false;
        const auto first = owner->_chrome->LayerRowItemForTest(0);
        if (first.id != 1 || first.text != L"Interaction group" || !first.hasChildren || first.expanded != !collapsed)
            return false;
        if (collapsed)
        {
            const auto second = owner->_chrome->LayerRowItemForTest(1);
            return count == 2 && second.id == 2 && second.text == L"Interaction sibling" && second.depth == 0;
        }
        const auto second = owner->_chrome->LayerRowItemForTest(1);
        const auto third = owner->_chrome->LayerRowItemForTest(2);
        return count == 3 && second.id == 2 && second.text == L"Interaction child" && second.depth == 1 &&
               second.badgeText == LoadStringResource(nullptr, IDS_LAYER_MASK_BADGE) && third.id == 3 &&
               third.text == L"Interaction sibling" && third.depth == 0;
    };
    const auto clickAt = [&](float xDip, float yDip, UINT modifiers = 0) noexcept
    {
        const int x = static_cast<int>(xDip * scale);
        const int y = static_cast<int>(yDip * scale);
        owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON | modifiers, MAKELPARAM(x, y));
        owner->OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    const auto settleRows = [&]() noexcept
    {
        if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
            return false;
        owner->SyncChrome();
        if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                           dpi == 0 ? 96.0f : static_cast<float>(dpi))))
            return false;
        // Publication can release the old semantic snapshot's final row owner. Its whole-graph retirement and
        // lease acknowledgement are new useful work; observe that work before asserting a settled ledger.
        return SettleJobs(owner->_jobs, 15000, dispatch, &pump) == SettleResult::Settled;
    };
    const auto documentIsLiteral = [&]() noexcept
    {
        const auto& document = owner->_session.Document();
        if (!document || document->layers.size() != 3 || !document->layers[2].asset || !document->layers[2].mask ||
            !document->layers[2].mask->enabled || !document->layers[0].asset)
            return false;
        const Raster& actualChild = document->layers[2].asset->image;
        const Raster& actualMask = document->layers[2].mask->asset.image;
        const Raster& actualSibling = document->layers[0].asset->image;
        if (actualChild.Width() != 2 || actualChild.Height() != 1 || actualMask.Width() != 2 ||
            actualMask.Height() != 1 || actualSibling.Width() != 2 || actualSibling.Height() != 1)
            return false;
        for (std::size_t i = 0; i < childPixels.size(); ++i)
            if (actualChild.Data()[i] != childPixels[i] || actualSibling.Data()[i] != siblingPixels[i])
                return false;
        for (std::size_t i = 0; i < childMask.size(); ++i)
            if (actualMask.Data()[i] != childMask[i])
                return false;
        return true;
    };

    expect(rowsAre(3, false), "initial", "pinned-tree-preserves-literal-visible-row-order-and-ids");
    expect(documentIsLiteral() && owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId) &&
               !owner->_session.IsMaskSelected(),
           "initial", "fixture-starts-with-literal-pixels-enabled-mask-and-sibling-target");

    const D2D1_RECT_F childRow = owner->_chrome->LayerRowBoundsDip(1);
    const D2D1_RECT_F childBadge = owner->_chrome->LayerMaskBadgeBoundsDip(1);
    const float layerContentTop = ChromeHost::kMenuHeightDip + ChromeHost::kTabHeightDip + ChromeHost::kLayersTitleDip;
    if (childRow.bottom <= childRow.top || childBadge.right <= childBadge.left || childRow.top < layerContentTop)
    {
        SelfTestFailure(L"The prepared layer tree did not lay out the child row and mask badge.\n");
        return 5;
    }
    const float childPixelsX = childBadge.left - 4.0f;
    if (childPixelsX <= childRow.left || childPixelsX >= childRow.right)
    {
        SelfTestFailure(L"The child row did not leave a pixel-target area beside its mask badge.\n");
        return 5;
    }
    const auto badgeHit = owner->_chrome->LayerTargetAt((childBadge.left + childBadge.right) * 0.5f * scale,
                                                        (childBadge.top + childBadge.bottom) * 0.5f * scale);
    std::printf("LAYER ROW INTERACTION dpi=%u child=[%.2f %.2f %.2f %.2f] badge=[%.2f %.2f %.2f %.2f] "
                "badgeHit=%d current=%d hostBusy=%d\n",
                dpi, childRow.left, childRow.top, childRow.right, childRow.bottom, childBadge.left, childBadge.top,
                childBadge.right, childBadge.bottom,
                badgeHit && badgeHit.value().id == childId && badgeHit.value().mask,
                owner->_layerRowsPublishedKey && owner->_layerRowsPublishedKey.value() == owner->CurrentLayerRowsKey(),
                owner->_activeHostRequest.has_value() || owner->_hostRequests.Size() != 0);
    clickAt(childPixelsX, (childRow.top + childRow.bottom) * 0.5f);
    std::printf("LAYER ROW INTERACTION pixel click: child=%d group=%d sibling=%d mask=%d current=%d\n",
                owner->_session.ActiveLayerID() == std::optional<Guid>(childId),
                owner->_session.ActiveLayerID() == std::optional<Guid>(groupId),
                owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId), owner->_session.IsMaskSelected(),
                owner->_layerRowsPublishedKey && owner->_layerRowsPublishedKey.value() == owner->CurrentLayerRowsKey());
    expect(owner->_session.ActiveLayerID() == std::optional<Guid>(childId) && !owner->_session.IsMaskSelected(),
           "selection", "actual-tree-row-pointer-selects-child-pixels");
    expect(owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount && documentIsLiteral(),
           "selection", "row-selection-preserves-literal-pixels-mask-and-history");
    clickAt((childBadge.left + childBadge.right) * 0.5f, (childBadge.top + childBadge.bottom) * 0.5f);
    expect(owner->_session.ActiveLayerID() == std::optional<Guid>(childId) && owner->_session.IsMaskSelected(),
           "selection", "actual-prepared-mask-badge-pointer-selects-child-mask");
    expect(owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount && documentIsLiteral(),
           "selection", "prepared-mask-target-selection-preserves-literal-pixels-mask-and-history");
    const D2D1_RECT_F siblingRow = owner->_chrome->LayerRowBoundsDip(2);
    clickAt(siblingRow.left + 100.0f, (siblingRow.top + siblingRow.bottom) * 0.5f);
    expect(owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId) && !owner->_session.IsMaskSelected(),
           "selection", "actual-tree-row-pointer-restores-sibling-pixel-target");

    const D2D1_RECT_F groupExpander = owner->_chrome->LayerRowExpanderBoundsDipForTest(0);
    if (groupExpander.right <= groupExpander.left || groupExpander.bottom <= groupExpander.top)
        return 5;
    pump.staleRowX = static_cast<int>(childPixelsX * scale);
    pump.staleRowY = static_cast<int>((childRow.top + childRow.bottom) * 0.5f * scale);
    pump.staleMaskX = static_cast<int>((childBadge.left + childBadge.right) * 0.5f * scale);
    pump.staleMaskY = static_cast<int>((childBadge.top + childBadge.bottom) * 0.5f * scale);
    pump.exerciseStaleInteractions = true;
    clickAt((groupExpander.left + groupExpander.right) * 0.5f, (groupExpander.top + groupExpander.bottom) * 0.5f);
    owner->_layerRowsSource.Pump();
    expect(owner->_session.CollapsedGroupIDs().contains(groupId) && owner->_layerRowsTicket.has_value() &&
               owner->_chrome->LayerRowItemForTest(0).expanded &&
               owner->_chrome->LayerRowItemForTest(1).text == L"Interaction child",
           "stale-held", "actual-group-expander-starts-collapse-while-old-tree-awaits-worker-publication");
    const Guid collapsedRevision = owner->_session.history.CurrentRevision();
    const int collapsedUndoCount = owner->_session.history.UndoCount();
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    pump.exerciseStaleInteractions = false;
    owner->SyncChrome();
    if (FAILED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi))))
        return 5;
    expect(pump.gateVisited && pump.gateHeld && pump.staleRowBlocked && pump.staleMaskBlocked, "stale-held",
           "held-real-worker-rejects-stale-row-selection-and-shift-mask-toggle");
    expect(gate->retired.load() && gate->retiredOffOwner.load(), "stale-held",
           "held-worker-input-retires-off-owner-after-acknowledgement");
    expect(rowsAre(2, true) && owner->_session.history.CurrentRevision() == collapsedRevision &&
               owner->_session.history.UndoCount() == collapsedUndoCount && documentIsLiteral(),
           "collapse", "published-collapse-hides-child-with-literal-document-and-history-preserved");

    const auto toggleGroup = [&]() noexcept
    {
        const D2D1_RECT_F expander = owner->_chrome->LayerRowExpanderBoundsDipForTest(0);
        if (expander.right <= expander.left || expander.bottom <= expander.top)
            return false;
        clickAt((expander.left + expander.right) * 0.5f, (expander.top + expander.bottom) * 0.5f);
        owner->_layerRowsSource.Pump();
        return true;
    };
    if (!toggleGroup() || !settleRows())
        return 5;
    expect(!owner->_session.CollapsedGroupIDs().contains(groupId) && rowsAre(3, false), "group-A-B-A",
           "pointer-expander-reopens-group-with-literal-visible-row-ids");
    if (!toggleGroup() || !settleRows())
        return 5;
    expect(owner->_session.CollapsedGroupIDs().contains(groupId) && rowsAre(2, true), "group-A-B-A",
           "second-pointer-expander-collapse-restores-literal-row-ids");
    expect(owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount && documentIsLiteral(),
           "group-A-B-A", "expansion-cycle-creates-no-history-and-keeps-source-pixels-and-mask");
    std::printf("LAYER ROW INTERACTION closeout: idle=%d ticket=%d bytes=%llu reserved=%llu resident=%llu "
                "releasing=%llu readers=%ld accessibilityStale=%d\n",
                owner->_jobs.IsIdle(), owner->_layerRowsTicket.has_value(),
                static_cast<unsigned long long>(owner->_preparedLayerRows->MemoryBytes()),
                static_cast<unsigned long long>(owner->_jobs.ReservedMemoryBytes()),
                static_cast<unsigned long long>(owner->_preparedLayerRows->ResidentBytes()),
                static_cast<unsigned long long>(owner->_jobs.ReleasingMemoryBytes()),
                owner->_preparedLayerRows.use_count(), owner->_chrome->AccessibilityStaleForTest());
    expect(!owner->_layerRowsTicket && owner->_preparedLayerRows && !owner->_preparedLayerRows->Rows().empty() &&
               owner->_jobs.IsIdle() && owner->_preparedLayerRows->MemoryBytes() != 0 &&
               owner->_jobs.ReservedMemoryBytes() == owner->_preparedLayerRows->MemoryBytes() &&
               owner->_preparedLayerRows->ResidentBytes() == owner->_preparedLayerRows->MemoryBytes() &&
               owner->_jobs.ReleasingMemoryBytes() == 0,
           "closeout", "settled-tree-retains-only-its-current-prepared-cache");

    gate->hold = false;
    (void)SetEvent(gate->release.get());
    owner->Shutdown();
    expect(owner->_shutdownComplete && owner->_jobs.WorkerCount() == 0 && gate->retired.load() &&
               gate->retiredOffOwner.load(),
           "closeout", "shutdown-joins-workers-after-off-owner-interaction-input-retirement");
    // WM_QUIT is generated only after pending posted messages are drained. COM notification delivery may
    // leave messages after host disconnect; a quit-only peek cannot establish fixture closeout.
    const auto quit = PumpMessages();
    const auto remainingMessages = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainingMessages.quit, "closeout",
           "fixture-quit-consumed-before-the-next-fresh-owner");
    std::printf("%s application-prepared-layer-rows-interaction.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures == 0 ? 0 : 5;
}

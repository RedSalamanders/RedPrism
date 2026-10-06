#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "KeyboardState.h"
#include "LayerRowsTestGate.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

int Application::RunStandalonePreparedLayerRowsEditSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-edit.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
        std::fflush(stdout);
    };

    std::array<BYTE, 256> savedKeys{};
    if (!KeyboardState::Read(savedKeys))
        return 5;
    KeyboardState::Write({});
    const auto restoreKeys = wil::scope_exit([savedKeys]() noexcept { KeyboardState::Write(savedKeys); });

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
        SelfTestFailure(L"Prepared layer rows edit fixture could not initialize its hidden WARP host.\n");
        return 5;
    }
    owner->BindCompositionDevice();
    expect(owner->_renderer.IsWarp() && owner->_window && owner->_chrome && owner->_jobs.WorkerCount() != 0, "fixture",
           "uses-real-hidden-WARP-application-and-scheduler");

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    ImageLayer sibling = ImageLayer::Blank(L"Literal sibling", {2, 1});
    const Guid siblingId = sibling.id;
    constexpr std::array<std::uint8_t, 8> fixtureSiblingPixels{70, 80, 90, 255, 100, 110, 120, 255};
    const Raster siblingImage =
        Raster::Make(2, 1, RasterFormat::Bgra8888, fixtureSiblingPixels.data(), fixtureSiblingPixels.size());
    sibling.asset = RasterAsset{siblingImage, {}, L"Literal sibling pixels"};

    ImageLayer group = ImageLayer::Blank(L"Literal group", {2, 1});
    group.isGroup = true;
    const Guid groupId = group.id;
    ImageLayer child = ImageLayer::Blank(L"Literal child", {2, 1});
    child.parentID = groupId;
    const Guid childId = child.id;
    constexpr std::array<std::uint8_t, 8> fixtureChildPixels{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 2> fixtureChildMask{0, 255};
    const Raster childImage =
        Raster::Make(2, 1, RasterFormat::Bgra8888, fixtureChildPixels.data(), fixtureChildPixels.size());
    const Raster maskImage = Raster::Make(2, 1, RasterFormat::Gray8, fixtureChildMask.data(), fixtureChildMask.size());
    child.asset = RasterAsset{childImage, {}, L"Literal child pixels"};
    child.mask = LayerMask{RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"Literal child mask"}, true,
                           std::nullopt, true};

    // Document storage is bottom-to-top; the independent expected Tree order is group, child, sibling.
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
        int sourceX = 0;
        int sourceY = 0;
        int targetX = 0;
        int targetY = 0;
        int invokeX = 0;
        int invokeY = 0;
        Guid revision{};
        int undoCount = 0;
        Guid siblingId{};
        Guid groupId{};
        Guid childId{};
        bool visited = false;
        bool held = false;
        bool staleReorderBlocked = false;
        bool staleInvokeBlocked = false;
    } pump{*owner};
    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<PumpState*>(opaque);
        auto& message = *static_cast<MSG*>(native);
        if (message.message != LayerRowsTestGate::kMessage)
        {
            state.owner.DispatchQueuedMessage(message);
            return;
        }

        state.visited = true;
        state.held = state.owner._layerRowsTicket.has_value() && state.owner._preparedLayerRows && state.gate &&
                     state.gate->captureReserved.load() && state.gate->captures.load() == 1 &&
                     state.gate->preparations.load() == 1 &&
                     state.owner._jobs.ReservedMemoryBytes() >= state.gate->reservationBytes;

        // This old Tree still paints the expanded rows, but the refresh has disabled interaction before its worker
        // was released. Drive the same client-area pointer path used by the application window procedure.
        state.owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(state.sourceX, state.sourceY));
        state.owner.OnPointer(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(state.targetX, state.targetY));
        state.owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(state.targetX, state.targetY));
        const auto& layers = state.owner._session.Document().value().layers;
        state.staleReorderBlocked = layers.size() == 3 && layers[0].id == state.siblingId &&
                                    layers[1].id == state.groupId && layers[2].id == state.childId &&
                                    state.owner._session.history.CurrentRevision() == state.revision &&
                                    state.owner._session.history.UndoCount() == state.undoCount;

        // EmbeddedHost recognizes a double-click from two ordinary pointer presses. Application::OnPointer maps
        // WM_LBUTTONDBLCLK to Move, so drive the supported embedded route instead.
        state.owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(state.invokeX, state.invokeY));
        state.owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(state.invokeX, state.invokeY));
        state.owner.OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(state.invokeX, state.invokeY));
        state.owner.OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(state.invokeX, state.invokeY));
        state.staleInvokeBlocked = !state.owner._chrome->RenameCardOpen() &&
                                   state.owner._session.history.CurrentRevision() == state.revision &&
                                   state.owner._session.history.UndoCount() == state.undoCount;
        (void)SetEvent(state.gate->release.get());
    };

    const auto dpi = GetDpiForWindow(owner->_window.get());
    const float scale = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
    const float dpiValue = dpi == 0 ? 96.0f : static_cast<float>(dpi);
    const auto layoutChrome = [&]() noexcept
    { return SUCCEEDED(owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(), dpiValue)); };
    const auto prepare = [&]() noexcept
    {
        owner->SyncChrome();
        return layoutChrome();
    };
    const auto settleRows = [&]() noexcept
    {
        if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
            return false;
        return prepare();
    };
    const auto pointer = [&](UINT message, WPARAM keys, float xDip, float yDip) noexcept
    {
        const int x = static_cast<int>(xDip * scale);
        const int y = static_cast<int>(yDip * scale);
        owner->OnPointer(message, keys, MAKELPARAM(x, y));
    };
    const auto click = [&](const D2D1_RECT_F& bounds) noexcept
    {
        const float x = (bounds.left + bounds.right) * 0.5f;
        const float y = (bounds.top + bounds.bottom) * 0.5f;
        pointer(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        pointer(WM_LBUTTONUP, 0, x, y);
    };
    const auto doubleClick = [&](const D2D1_RECT_F& bounds) noexcept
    {
        click(bounds);
        click(bounds);
    };
    const auto drag = [&](const D2D1_RECT_F& source, float targetX, float targetY) noexcept
    {
        const float sourceX = (source.left + source.right) * 0.5f;
        const float sourceY = (source.top + source.bottom) * 0.5f;
        pointer(WM_LBUTTONDOWN, MK_LBUTTON, sourceX, sourceY);
        pointer(WM_MOUSEMOVE, MK_LBUTTON, targetX, targetY);
        pointer(WM_LBUTTONUP, 0, targetX, targetY);
    };
    constexpr std::array<std::uint8_t, 8> expectedSiblingPixels{70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 8> expectedChildPixels{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 2> expectedChildMask{0, 255};
    const auto documentMatches =
        [&](const std::array<Guid, 3>& storageOrder, const wchar_t* groupName, const wchar_t* siblingName) noexcept
    {
        const auto& document = owner->_session.Document();
        if (!document.has_value() || document.value().layers.size() != storageOrder.size())
            return false;
        for (std::size_t index = 0; index < storageOrder.size(); ++index)
            if (document.value().layers[index].id != storageOrder[index])
                return false;

        const ImageLayer* actualGroup = nullptr;
        const ImageLayer* actualChild = nullptr;
        const ImageLayer* actualSibling = nullptr;
        for (const ImageLayer& layer : document.value().layers)
        {
            if (layer.id == groupId)
                actualGroup = &layer;
            else if (layer.id == childId)
                actualChild = &layer;
            else if (layer.id == siblingId)
                actualSibling = &layer;
        }
        if (!actualGroup || !actualChild || !actualSibling || !actualGroup->isGroup || actualGroup->name != groupName ||
            actualGroup->parentID.has_value() || actualGroup->asset.has_value() || actualGroup->mask.has_value() ||
            actualChild->parentID != std::optional<Guid>(groupId) || actualChild->name != L"Literal child" ||
            actualSibling->parentID.has_value() || actualSibling->name != siblingName ||
            !actualChild->asset.has_value() || !actualChild->mask.has_value() || !actualChild->mask.value().enabled ||
            actualChild->mask.value().placement.has_value() || !actualChild->mask.value().linked ||
            !actualSibling->asset.has_value() || actualSibling->mask.has_value())
            return false;
        const Raster& actualChildPixels = actualChild->asset.value().image;
        const Raster& actualMask = actualChild->mask.value().asset.image;
        const Raster& actualSiblingPixels = actualSibling->asset.value().image;
        if (actualChildPixels.Width() != 2 || actualChildPixels.Height() != 1 || actualMask.Width() != 2 ||
            actualMask.Height() != 1 || actualSiblingPixels.Width() != 2 || actualSiblingPixels.Height() != 1 ||
            actualChildPixels.Format() != RasterFormat::Bgra8888 ||
            actualSiblingPixels.Format() != RasterFormat::Bgra8888 || actualMask.Format() != RasterFormat::Gray8 ||
            actualChildPixels.Stride() != 8 || actualSiblingPixels.Stride() != 8 || actualMask.Stride() != 2)
            return false;
        for (std::size_t index = 0; index < expectedChildPixels.size(); ++index)
            if (actualChildPixels.Data()[index] != expectedChildPixels[index] ||
                actualSiblingPixels.Data()[index] != expectedSiblingPixels[index])
                return false;
        for (std::size_t index = 0; index < expectedChildMask.size(); ++index)
            if (actualMask.Data()[index] != expectedChildMask[index])
                return false;
        return true;
    };
    const auto rowsAre =
        [&](const std::array<Guid, 3>& order, const wchar_t* groupName, const wchar_t* siblingName) noexcept
    {
        if (!owner->_preparedLayerRows || owner->_preparedLayerRows->Rows().size() != order.size())
            return false;
        const auto modelRows = owner->_session.LayerRows();
        if (modelRows.size() != order.size())
            return false;
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            const auto row = owner->_chrome->LayerRowItemForTest(index);
            const Guid id = order[index];
            const wchar_t* const name = id == groupId ? groupName : id == childId ? L"Literal child" : siblingName;
            const std::uint32_t depth = id == childId ? 1u : 0u;
            if (modelRows[index].id != id || row.id != index + 1 || row.text != name || row.depth != depth ||
                row.hasChildren != (id == groupId) || (id == groupId && row.expanded != true))
                return false;
            if (id == childId && row.badgeText != LoadStringResource(nullptr, IDS_LAYER_MASK_BADGE))
                return false;
        }
        return true;
    };

    // Size the real Tree before prepared publication. NotifyDataChanged preserves Tree scroll, and publishing a
    // selected row against its pre-layout one-DIP viewport can scroll the eventual rows outside the actual bounds.
    if (!layoutChrome() || !owner->EnablePreparedLayerRows())
    {
        SelfTestFailure(L"Prepared layer rows edit fixture could not enable prepared rows.\n");
        return 5;
    }
    owner->_layerRowsSource.Pump();
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled || !prepare())
        return 5;

    const std::array<Guid, 3> initialOrder{groupId, childId, siblingId};
    const std::array<Guid, 3> postReorderStorage{groupId, siblingId, childId};
    const std::array<Guid, 3> initialStorage{siblingId, groupId, childId};
    expect(rowsAre(initialOrder, L"Literal group", L"Literal sibling") &&
               documentMatches(initialStorage, L"Literal group", L"Literal sibling"),
           "initial", "prepared-tree-matches-independent-literal-group-child-sibling-and-raster-fixture");
    expect(owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId) && !owner->_session.IsMaskSelected() &&
               owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount,
           "initial", "fixture-starts-at-literal-sibling-with-no-edit-history");

    const D2D1_RECT_F groupRow = owner->_chrome->LayerRowBoundsDip(0);
    const D2D1_RECT_F childRow = owner->_chrome->LayerRowBoundsDip(1);
    const D2D1_RECT_F siblingRow = owner->_chrome->LayerRowBoundsDip(2);
    const D2D1_RECT_F expander = owner->_chrome->LayerRowExpanderBoundsDipForTest(0);
    if (groupRow.bottom <= groupRow.top || childRow.bottom <= childRow.top || siblingRow.bottom <= siblingRow.top ||
        expander.right <= expander.left || expander.bottom <= expander.top)
        return 5;

    auto gate = std::make_shared<LayerRowsTestGate>();
    gate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!gate->release)
        return 5;
    gate->hold = true;
    gate->holdAfterPrepare = true;
    gate->reservationBytes = 1;
    const auto unblock = wil::scope_exit([gate]() noexcept { (void)SetEvent(gate->release.get()); });
    owner->_layerRowsTestGate = gate;
    pump.gate = gate;
    pump.revision = initialRevision;
    pump.undoCount = initialUndoCount;
    pump.siblingId = siblingId;
    pump.groupId = groupId;
    pump.childId = childId;
    pump.sourceX = static_cast<int>((groupRow.left + groupRow.right) * 0.5f * scale);
    pump.sourceY = static_cast<int>((groupRow.top + groupRow.bottom) * 0.5f * scale);
    pump.targetX = static_cast<int>((siblingRow.left + siblingRow.right) * 0.5f * scale);
    pump.targetY = static_cast<int>((siblingRow.bottom - 3.0f) * scale);
    pump.invokeX = pump.targetX;
    pump.invokeY = static_cast<int>((siblingRow.top + siblingRow.bottom) * 0.5f * scale);

    click(expander);
    owner->_layerRowsSource.Pump();
    expect(owner->_session.CollapsedGroupIDs().contains(groupId) && owner->_layerRowsTicket.has_value() &&
               owner->_chrome->LayerRowItemForTest(1).text == L"Literal child",
           "stale-held", "group-collapse-keeps-old-painted-rows-while-replacement-is-pending");
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled || !prepare())
        return 5;
    expect(pump.visited && pump.held && pump.staleReorderBlocked, "stale-held",
           "held-real-worker-rejects-stale-row-reorder-before-publication");
    expect(pump.staleInvokeBlocked && !owner->_chrome->RenameCardOpen(), "stale-held",
           "held-real-worker-rejects-stale-embedded-double-click-rename-invocation");
    expect(gate->retired.load() && gate->retiredOffOwner.load(), "stale-held",
           "held-worker-input-retires-off-owner-after-acknowledgement");
    expect(owner->_session.CollapsedGroupIDs().contains(groupId) &&
               documentMatches(initialStorage, L"Literal group", L"Literal sibling") &&
               owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount,
           "stale-held", "stale-attempts-preserve-literal-hierarchy-pixels-mask-and-history");

    gate->hold = false;
    const D2D1_RECT_F collapsedExpander = owner->_chrome->LayerRowExpanderBoundsDipForTest(0);
    if (collapsedExpander.right <= collapsedExpander.left || collapsedExpander.bottom <= collapsedExpander.top)
        return 5;
    click(collapsedExpander);
    owner->_layerRowsSource.Pump();
    if (!settleRows())
        return 5;
    expect(!owner->_session.CollapsedGroupIDs().contains(groupId) &&
               rowsAre(initialOrder, L"Literal group", L"Literal sibling"),
           "refresh", "settled-expansion-restores-literal-prepared-row-order");

    // The pinned Tree reports one drop on release. The sibling row's lower half is TreeDropPlace::After; RedPrism
    // translates that to PlaceLayer(group, root, sibling, false, true), yielding this literal top-first/storage order.
    drag(owner->_chrome->LayerRowBoundsDip(0),
         (owner->_chrome->LayerRowBoundsDip(2).left + owner->_chrome->LayerRowBoundsDip(2).right) * 0.5f,
         owner->_chrome->LayerRowBoundsDip(2).bottom - 3.0f);
    owner->_layerRowsSource.Pump();
    if (!settleRows())
        return 5;
    const std::array<Guid, 3> reorderedRows{siblingId, groupId, childId};
    expect(owner->_session.history.UndoCount() == initialUndoCount + 1 &&
               owner->_session.history.CurrentRevision() != initialRevision &&
               owner->_session.history.UndoName() == L"Move Layer",
           "reorder", "actual-prepared-tree-drag-creates-one-move-layer-history-entry");
    expect(rowsAre(reorderedRows, L"Literal group", L"Literal sibling") &&
               owner->_chrome->LayerRowItemForTest(0).text == L"Literal sibling" &&
               owner->_chrome->LayerRowItemForTest(1).text == L"Literal group" &&
               owner->_chrome->LayerRowItemForTest(2).text == L"Literal child" &&
               owner->_chrome->LayerRowItemForTest(2).depth == 1 &&
               documentMatches(postReorderStorage, L"Literal group", L"Literal sibling"),
           "reorder", "real-row-drag-moves-root-after-sibling-and-keeps-child-parent-and-literal-pixels-mask");

    const int beforeRenameUndoCount = owner->_session.history.UndoCount();
    const Guid beforeRenameRevision = owner->_session.history.CurrentRevision();
    // The pinned Tree invokes leaf rows on its synthesized double-click and toggles folders. Invoke the sibling leaf
    // through two ordinary pointer clicks, which selects it before opening the Rename Layer card.
    doubleClick(owner->_chrome->LayerRowBoundsDip(0));
    expect(owner->_chrome->RenameCardOpen() && owner->_chrome->TextInputFocused(), "rename-open",
           "embedded-double-click-invokes-leaf-rename-card-and-focuses-its-text-field");
    // Opening the card changes the pinned host's interaction revision. Prepare its visible text field before
    // sending character/key input through the same coherence guard used by the application.
    if (!prepare())
        return 5;
    constexpr std::wstring_view renamedName = L"Edited sibling";
    for (const wchar_t character : renamedName)
        (void)SendMessageW(owner->_window.get(), WM_CHAR, character, 1);
    (void)owner->HandleAccelerator(VK_RETURN, 0);
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    const bool renameCommitted = !owner->_chrome->RenameCardOpen() &&
                                 owner->_session.history.UndoCount() == beforeRenameUndoCount + 1 &&
                                 owner->_session.history.CurrentRevision() != beforeRenameRevision &&
                                 owner->_session.history.UndoName() == L"Rename Layer" &&
                                 documentMatches(postReorderStorage, L"Literal group", L"Edited sibling");
    expect(renameCommitted, "rename-commit",
           "typed-window-messages-and-enter-rename-the-invoked-sibling-as-one-history-entry");
    owner->_layerRowsSource.Pump();
    if (!settleRows())
        return 5;
    expect(rowsAre(reorderedRows, L"Literal group", L"Edited sibling") &&
               owner->_chrome->LayerRowItemForTest(0).text == L"Edited sibling" &&
               owner->_chrome->LayerRowItemForTest(1).text == L"Literal group" &&
               owner->_chrome->LayerRowItemForTest(2).text == L"Literal child" &&
               documentMatches(postReorderStorage, L"Literal group", L"Edited sibling"),
           "rename-refresh", "prepared-rows-publish-renamed-sibling-with-child-and-literal-mask-pixels");

    (void)owner->HandleAccelerator('Z', MK_CONTROL);
    if (SettleJobs(owner->_jobs, 15000, dispatch, &pump) != SettleResult::Settled)
        return 5;
    expect(owner->_session.history.UndoCount() == initialUndoCount + 1 &&
               owner->_session.history.UndoName() == L"Move Layer" &&
               documentMatches(postReorderStorage, L"Literal group", L"Literal sibling") &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId),
           "undo", "one-undo-reverses-only-ui-rename-and-retains-reordered-literal-hierarchy");

    owner->Shutdown();
    expect(owner->_shutdownComplete && owner->_jobs.WorkerCount() == 0 && owner->_jobs.ReservedMemoryBytes() == 0 &&
               owner->_jobs.ReleasingMemoryBytes() == 0 && !owner->_preparedLayerRows &&
               !owner->_layerRowsCacheTicket && gate->retired.load() && gate->retiredOffOwner.load(),
           "closeout", "shutdown-joins-worker-clears-borrow-and-retires-input-off-owner");
    MSG quit{};
    const bool quitRemoved = PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;
    expect(quitRemoved && quit.wParam == 0 && PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE) == FALSE,
           "closeout", "fixture-quit-consumed-before-the-next-fresh-owner");
    std::printf("%s application-prepared-layer-rows-edit.summary checks=%u failures=%d\n", failures ? "FAIL" : "PASS",
                checks, failures);
    return failures == 0 ? 0 : 5;
}

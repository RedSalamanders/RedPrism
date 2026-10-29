#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "PreparedLayerRows.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

// Standalone hidden-WARP application self-test covering the
// documented selected-folder grouping flow through an actual prepared Tree row.
int Application::RunStandalonePreparedLayerRowsGroupSelectionSelfTest(HINSTANCE instance) noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    unsigned checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-group-selection.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };

    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto cleanup = wil::scope_exit(
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
        return 5;
    owner->BindCompositionDevice();

    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    ImageLayer sourceGroup = ImageLayer::Blank(L"Source group", {2, 1});
    sourceGroup.isGroup = true;
    const Guid sourceGroupId = sourceGroup.id;
    ImageLayer upperChild = ImageLayer::Blank(L"Upper child", {2, 1});
    upperChild.parentID = sourceGroupId;
    const Guid upperChildId = upperChild.id;
    ImageLayer lowerChild = ImageLayer::Blank(L"Lower child", {2, 1});
    lowerChild.parentID = sourceGroupId;
    const Guid lowerChildId = lowerChild.id;
    ImageLayer sibling = ImageLayer::Blank(L"Sibling", {2, 1});
    const Guid siblingId = sibling.id;
    constexpr std::array<std::uint8_t, 8> upperBytes{11, 21, 31, 255, 41, 51, 61, 255};
    constexpr std::array<std::uint8_t, 8> lowerBytes{12, 22, 32, 255, 42, 52, 62, 255};
    constexpr std::array<std::uint8_t, 8> siblingBytes{13, 23, 33, 255, 43, 53, 63, 255};
    const Raster upperImage = Raster::Make(2, 1, RasterFormat::Bgra8888, upperBytes.data(), upperBytes.size());
    const Raster lowerImage = Raster::Make(2, 1, RasterFormat::Bgra8888, lowerBytes.data(), lowerBytes.size());
    const Raster siblingImage = Raster::Make(2, 1, RasterFormat::Bgra8888, siblingBytes.data(), siblingBytes.size());
    upperChild.asset = RasterAsset{upperImage, {}, L"Upper pixels"};
    lowerChild.asset = RasterAsset{lowerImage, {}, L"Lower pixels"};
    sibling.asset = RasterAsset{siblingImage, {}, L"Sibling pixels"};
    // CanvasDocument stores bottom-to-top. The prepared tree is expected to publish top-first.
    fixture.layers.push_back(std::move(sibling));
    fixture.layers.push_back(std::move(sourceGroup));
    fixture.layers.push_back(std::move(lowerChild));
    fixture.layers.push_back(std::move(upperChild));
    owner->_session.ReplaceDocument(std::move(fixture).Seal(), siblingId);
    const Guid initialRevision = owner->_session.history.CurrentRevision();
    const int initialUndoCount = owner->_session.history.UndoCount();

    expect(owner->_renderer.IsWarp() && owner->_jobs.WorkerCount() != 0,
           "fixture.real-hidden-warp-application-and-scheduler");
    const UINT dpi = GetDpiForWindow(owner->_window.get());
    const float scale = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
    const auto prepareChrome = [&]() noexcept
    {
        return owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi));
    };
    const auto settleRows = [&]() noexcept
    {
        owner->SyncPreparedLayerRows();
        if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled ||
            FAILED(prepareChrome()))
            return false;
        return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled;
    };
    // Size the real Tree before publishing selection. NotifyDataChanged preserves scroll; publishing against
    // its initial one-DIP fallback viewport can scroll every later hit rectangle out of the sized Tree.
    if (FAILED(prepareChrome()) || !owner->EnablePreparedLayerRows() || !settleRows())
        return 5;
    owner->SyncChrome();
    if (FAILED(prepareChrome()))
        return 5;

    const auto rowsAre =
        [&](std::size_t count, std::initializer_list<std::pair<const wchar_t*, unsigned int>> expected) noexcept
    {
        if (!owner->_preparedLayerRows || owner->_preparedLayerRows->Rows().size() != count || expected.size() != count)
            return false;
        std::size_t index = 0;
        for (const auto& [text, depth] : expected)
        {
            const auto item = owner->_chrome->LayerRowItemForTest(index++);
            if (item.text != text || item.depth != depth)
                return false;
        }
        return true;
    };
    const auto clickRow = [&](const Guid& id) noexcept
    {
        const auto index = owner->_preparedLayerRows->FindVisibleRow(id);
        if (!index)
            return false;
        const D2D1_RECT_F row = owner->_chrome->LayerRowBoundsDip(index.value());
        if (row.right <= row.left || row.bottom <= row.top)
            return false;
        const int x = static_cast<int>((row.left + (row.right - row.left) * 0.65f) * scale);
        const int y = static_cast<int>((row.top + row.bottom) * 0.5f * scale);
        owner->OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        owner->OnPointer(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return true;
    };
    const auto exactPixelsAndHierarchy = [&](std::optional<Guid> outer, std::optional<Guid> inner) noexcept
    {
        const auto& document = owner->_session.Document();
        const std::size_t expectedLayerCount = 4u + (outer ? 1u : 0u) + (inner ? 1u : 0u);
        if (!document || document->layers.size() != expectedLayerCount)
            return false;
        const auto find = [&](const Guid& id) -> const ImageLayer*
        {
            for (const ImageLayer& layer : document->layers)
                if (layer.id == id)
                    return &layer;
            return nullptr;
        };
        const ImageLayer* outerLayer = outer ? find(outer.value()) : nullptr;
        const ImageLayer* innerLayer = inner ? find(inner.value()) : nullptr;
        const ImageLayer* groupLayer = find(sourceGroupId);
        const ImageLayer* upperLayer = find(upperChildId);
        const ImageLayer* lowerLayer = find(lowerChildId);
        const ImageLayer* siblingLayer = find(siblingId);
        if ((outer && (!outerLayer || !outerLayer->isGroup || outerLayer->parentID.has_value())) || !groupLayer ||
            !groupLayer->isGroup || groupLayer->parentID != (inner ? inner : outer) || !upperLayer ||
            upperLayer->parentID != std::optional<Guid>(sourceGroupId) || !upperLayer->asset || !lowerLayer ||
            lowerLayer->parentID != std::optional<Guid>(sourceGroupId) || !lowerLayer->asset || !siblingLayer ||
            siblingLayer->parentID.has_value() || !siblingLayer->asset)
            return false;
        if (innerLayer && (!innerLayer->isGroup || innerLayer->parentID != outer))
            return false;
        const auto hasBytes = [](const ImageLayer& layer, const auto& expected) noexcept
        {
            if (!layer.asset || layer.asset->image.Width() != 2 || layer.asset->image.Height() != 1)
                return false;
            std::array<std::uint8_t, 8> actual{};
            layer.asset->image.ReadRegion(0, 0, 2, 1, actual.data(), 8, 0);
            return actual == expected;
        };
        return hasBytes(*upperLayer, upperBytes) && hasBytes(*lowerLayer, lowerBytes) &&
               hasBytes(*siblingLayer, siblingBytes);
    };

    expect(rowsAre(4, {{L"Source group", 0}, {L"Upper child", 1}, {L"Lower child", 1}, {L"Sibling", 0}}) &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(siblingId) &&
               owner->_session.SelectedLayerIDs().size() == 1 && owner->_session.SelectedLayerIDs().contains(siblingId),
           "initial.prepared-tree-publishes-literal-hierarchy-and-single-active-selection");
    expect(exactPixelsAndHierarchy(std::nullopt, std::nullopt) &&
               owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount,
           "initial.source-pixels-and-history-are-independent-baseline");

    if (!clickRow(sourceGroupId))
        return 5;
    const auto selectedSet = owner->_session.SelectedLayerIDs();
    expect(owner->_session.ActiveLayerID() == std::optional<Guid>(sourceGroupId) && selectedSet.size() == 1 &&
               selectedSet.contains(sourceGroupId) && !owner->_session.IsMaskSelected(),
           "prepared-row-pointer-selects-the-group-as-the-sole-layer-target");
    expect(owner->_session.history.CurrentRevision() == initialRevision &&
               owner->_session.history.UndoCount() == initialUndoCount,
           "prepared-group-row-selection-does-not-edit-history");

    owner->OnCommand(ChromeHost::kCommandGroupLayers);
    if (!settleRows())
        return 5;
    const auto firstFolderId = owner->_session.ActiveLayerID();
    if (!firstFolderId || !owner->_session.ActiveLayer() || !owner->_session.ActiveLayer()->isGroup)
        return 5;
    expect(
        owner->_session.ActiveLayer()->name == L"Folder 1" && owner->_session.SelectedLayerIDs().size() == 1 &&
            owner->_session.SelectedLayerIDs().contains(firstFolderId.value()) &&
            owner->_session.history.UndoCount() == initialUndoCount + 1 &&
            owner->_session.history.UndoName() == L"Group Layers" &&
            owner->_session.history.CurrentRevision() != initialRevision &&
            exactPixelsAndHierarchy(firstFolderId, std::nullopt) &&
            rowsAre(
                5, {{L"Folder 1", 0}, {L"Source group", 1}, {L"Upper child", 2}, {L"Lower child", 2}, {L"Sibling", 0}}),
        "selected-folder-grouping-wraps-the-complete-subtree-in-one-history-entry");

    const Guid firstGroupRevision = owner->_session.history.CurrentRevision();
    owner->OnCommand(ChromeHost::kCommandGroupLayers);
    if (!settleRows())
        return 5;
    const auto secondFolderId = owner->_session.ActiveLayerID();
    if (!secondFolderId || !owner->_session.ActiveLayer() || !owner->_session.ActiveLayer()->isGroup)
        return 5;
    expect(owner->_session.ActiveLayer()->name == L"Folder 2" && secondFolderId.value() != firstFolderId.value() &&
               owner->_session.SelectedLayerIDs().size() == 1 &&
               owner->_session.SelectedLayerIDs().contains(secondFolderId.value()) &&
               owner->_session.history.UndoCount() == initialUndoCount + 2 &&
               owner->_session.history.UndoName() == L"Group Layers" &&
               owner->_session.history.CurrentRevision() != firstGroupRevision &&
               exactPixelsAndHierarchy(secondFolderId, firstFolderId) &&
               rowsAre(6, {{L"Folder 2", 0},
                           {L"Folder 1", 1},
                           {L"Source group", 2},
                           {L"Upper child", 3},
                           {L"Lower child", 3},
                           {L"Sibling", 0}}),
           "subsequent-group-command-wraps-the-active-folder-and-keeps-original-descendants-and-pixels");

    const Guid secondGroupRevision = owner->_session.history.CurrentRevision();
    owner->OnCommand(ChromeHost::kCommandUndo);
    if (!settleRows())
        return 5;
    expect(
        owner->_session.ActiveLayerID() == firstFolderId && owner->_session.SelectedLayerIDs().size() == 1 &&
            owner->_session.SelectedLayerIDs().contains(firstFolderId.value()) &&
            owner->_session.history.UndoCount() == initialUndoCount + 1 && owner->_session.history.CanRedo() &&
            owner->_session.history.RedoName() == L"Group Layers" &&
            owner->_session.history.CurrentRevision() == firstGroupRevision &&
            exactPixelsAndHierarchy(firstFolderId, std::nullopt) &&
            rowsAre(
                5, {{L"Folder 1", 0}, {L"Source group", 1}, {L"Upper child", 2}, {L"Lower child", 2}, {L"Sibling", 0}}),
        "undo-of-second-group-restores-first-group-and-exact-literal-source-state");
    owner->OnCommand(ChromeHost::kCommandRedo);
    if (!settleRows())
        return 5;
    expect(owner->_session.ActiveLayerID() == secondFolderId && owner->_session.SelectedLayerIDs().size() == 1 &&
               owner->_session.SelectedLayerIDs().contains(secondFolderId.value()) &&
               owner->_session.history.UndoCount() == initialUndoCount + 2 && !owner->_session.history.CanRedo() &&
               owner->_session.history.CurrentRevision() == secondGroupRevision &&
               exactPixelsAndHierarchy(secondFolderId, firstFolderId) &&
               rowsAre(6, {{L"Folder 2", 0},
                           {L"Folder 1", 1},
                           {L"Source group", 2},
                           {L"Upper child", 3},
                           {L"Lower child", 3},
                           {L"Sibling", 0}}),
           "redo-restores-second-group-with-exact-literal-hierarchy-and-source-pixels");

    std::printf("%s application-prepared-layer-rows-group-selection.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    owner->Shutdown();
    const auto quit = PumpMessages();
    const auto remaining = PumpMessages();
    if (!owner->_shutdownComplete || owner->_jobs.WorkerCount() != 0 || !quit.quit || quit.exitCode != 0 ||
        remaining.quit)
        return 5;
    return failures ? 5 : 0;
}

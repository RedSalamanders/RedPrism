#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "PreparedLayerRows.h"
#include "Resources.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace
{
HRESULT IdentityProviderName(IRawElementProviderFragment* provider, std::wstring& text)
{
    text.clear();
    if (!provider)
        return E_POINTER;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    RETURN_IF_FAILED(provider->QueryInterface(IID_PPV_ARGS(simple.put())));
    wil::unique_variant name;
    RETURN_IF_FAILED(simple->GetPropertyValue(UIA_NamePropertyId, &name));
    if (name.vt != VT_BSTR)
        return E_UNEXPECTED;
    if (name.bstrVal)
        text.assign(name.bstrVal, SysStringLen(name.bstrVal));
    return S_OK;
}

wil::com_ptr_nothrow<IRawElementProviderFragment> IdentityFindTree(IRawElementProviderFragment* parent,
                                                                   std::wstring_view name, unsigned& budget)
{
    wil::com_ptr_nothrow<IRawElementProviderFragment> found;
    if (!parent || budget == 0)
        return found;
    --budget;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant type;
    std::wstring text;
    if (SUCCEEDED(parent->QueryInterface(IID_PPV_ARGS(simple.put()))) &&
        SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &type)) && type.vt == VT_I4 &&
        type.lVal == UIA_TreeControlTypeId && SUCCEEDED(IdentityProviderName(parent, text)) && text == name)
    {
        (void)parent->QueryInterface(IID_PPV_ARGS(found.put()));
        return found;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(parent->Navigate(NavigateDirection_FirstChild, child.put())))
        return found;
    while (child && budget != 0)
    {
        found = IdentityFindTree(child.get(), name, budget);
        if (found)
            return found;
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
            return found;
        child = std::move(next);
    }
    return found;
}

struct IdentityRuntimeId final
{
    std::array<LONG, 64> values{};
    unsigned count = 0;
    HRESULT status = E_FAIL;
    bool operator==(const IdentityRuntimeId&) const noexcept = default;
};

IdentityRuntimeId IdentityReadRuntimeId(IRawElementProviderFragment* provider) noexcept
{
    IdentityRuntimeId result;
    SAFEARRAY* array = nullptr;
    const auto release = wil::scope_exit(
        [&]() noexcept
        {
            if (array)
                (void)SafeArrayDestroy(array);
        });
    if (!provider || FAILED(result.status = provider->GetRuntimeId(&array)) || !array)
        return result;
    LONG lower = 0, upper = -1;
    if (SafeArrayGetDim(array) != 1 || FAILED(result.status = SafeArrayGetLBound(array, 1, &lower)) ||
        FAILED(result.status = SafeArrayGetUBound(array, 1, &upper)) || upper < lower ||
        static_cast<std::int64_t>(upper) - lower + 1 > static_cast<std::int64_t>(result.values.size()))
    {
        result.status = E_INVALIDARG;
        return result;
    }
    const auto length = static_cast<unsigned>(static_cast<std::int64_t>(upper) - lower + 1);
    for (unsigned offset = 0; offset < length; ++offset)
    {
        LONG index = lower + static_cast<LONG>(offset);
        result.status = SafeArrayGetElement(array, &index, &result.values[result.count]);
        if (FAILED(result.status))
            return result;
        ++result.count;
    }
    return result;
}
} // namespace

// Independent layer identities and state transitions through real pinned providers. This is not a timing gate.
int Application::RunStandalonePreparedLayerRowsIdentitySelfTest(HINSTANCE instance) noexcept
{
    unsigned checks = 0;
    int failures = 0;
    const auto expect = [&](bool passed, const char* name) noexcept
    {
        ++checks;
        failures += passed ? 0 : 1;
        std::printf("%s application-prepared-layer-rows-identity.%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
    const auto cleanup = wil::scope_exit([&]() noexcept { owner->Shutdown(); });
    owner->_quietUi = true;
    owner->_prompts = &QuietPrompts();
    if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
        FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
        FAILED(owner->InitializeChrome()))
        return 5;
    owner->BindCompositionDevice();
    Raster image;
    std::array<Guid, 4> ids{};
    bool literalAcrossTransitions = true;
    const auto prepare = [&]() noexcept
    {
        const auto dpi = GetDpiForWindow(owner->_window.get());
        return owner->_chrome->Prepare(owner->_renderer.Width(), owner->_renderer.Height(),
                                       dpi == 0 ? 96.0f : static_cast<float>(dpi));
    };
    const auto settle = [&]() noexcept
    {
        owner->SyncPreparedLayerRows();
        const bool settled =
            SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled &&
            SUCCEEDED(prepare()) &&
            SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled;
        if (owner->_session.Document())
        {
            for (const auto& layer : owner->_session.Document()->layers)
                if (layer.id == ids[0] || layer.id == ids[2] || layer.id == ids[3])
                    literalAcrossTransitions = literalAcrossTransitions && layer.asset && layer.asset->image == image;
        }
        else
            literalAcrossTransitions = false;
        return settled;
    };
    expect(owner->_renderer.IsWarp() && owner->_jobs.WorkerCount() != 0,
           "fixture.real-hidden-warp-host-and-original-memory-admission");
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 1;
    constexpr std::array<std::uint8_t, 8> pixels{9, 19, 29, 255, 39, 49, 59, 255};
    image = Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const std::array names{L"Alpha α", L"Group γ", L"Beta β", L"Omega Ω"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        auto layer = ImageLayer::Blank(names[index], {2, 1});
        ids[index] = layer.id;
        if (index == 1)
            layer.isGroup = true;
        else
            layer.asset = RasterAsset{image, {}, names[index]};
        if (index == 2)
            layer.parentID = ids[1];
        fixture.layers.push_back(std::move(layer));
    }
    const auto original = std::move(fixture).Seal();
    owner->_session.ReplaceDocument(original, ids[0]);
    if (FAILED(prepare()) || !owner->EnablePreparedLayerRows() || !settle())
        return 5;
    const auto base = PreparedLayerRows::FirstItemIdentity;
    const std::array expectedItemIds{base + 3, base + 1, base + 2, base};
    bool initialMapping = owner->_preparedLayerRows->GetCount() == 4;
    for (std::size_t index = 0; index < expectedItemIds.size(); ++index)
        initialMapping = initialMapping && owner->_preparedLayerRows->ItemIdAt(index) == expectedItemIds[index] &&
                         owner->_chrome->LayerRowItemForTest(index).id == expectedItemIds[index];
    expect(initialMapping, "initial.literal-storage-order-assigns-disjoint-stable-item-ids");
    const auto originalSerial = owner->_session.DocumentSerial();
    wil::com_ptr_nothrow<IRawElementProviderFragment> root;
    auto* rawRoot = owner->_chrome->AccessibilityRoot();
    if (!rawRoot || FAILED(rawRoot->QueryInterface(IID_PPV_ARGS(root.put()))))
        return 5;
    unsigned budget = 512;
    auto tree = IdentityFindTree(root.get(), LoadStringResource(nullptr, IDS_A11Y_LAYERS), budget);
    if (!tree)
        return 5;
    std::array<wil::com_ptr_nothrow<IRawElementProviderFragment>, 4> rows;
    if (FAILED(tree->Navigate(NavigateDirection_FirstChild, rows[0].put())))
        return 5;
    for (std::size_t index = 1; index < rows.size(); ++index)
        if (!rows[index - 1] || FAILED(rows[index - 1]->Navigate(NavigateDirection_NextSibling, rows[index].put())))
            return 5;
    std::wstring name;
    expect(SUCCEEDED(IdentityProviderName(rows[2].get(), name)) && name == L"Beta β",
           "provider.real-retained-child-starts-with-the-independent-layer-name");
    const auto betaRuntimeId = IdentityReadRuntimeId(rows[2].get());
    wil::com_ptr_nothrow<ISelectionItemProvider> betaSelection;
    if (FAILED(rows[2].query_to(betaSelection.put())))
        return 5;
    owner->_session.RenameLayer(ids[2], L"Beta nouveau β");
    if (!settle())
        return 5;
    expect(SUCCEEDED(IdentityProviderName(rows[2].get(), name)) && name == L"Beta nouveau β" &&
               owner->_preparedLayerRows->ItemIdAt(2) == base + 2,
           "rename.retained-provider-and-live-tree-preserve-the-same-layer-id");
    expect(betaRuntimeId.status == S_OK && betaRuntimeId == IdentityReadRuntimeId(rows[2].get()),
           "rename.uia-runtime-id-is-stable-through-the-new-source-epoch");
    const bool placed = owner->_session.PlaceLayer(ids[0], std::nullopt, ids[3]);
    if (!settle())
        return 5;
    expect(placed && owner->_preparedLayerRows->Rows()[0].id == ids[0] &&
               owner->_preparedLayerRows->ItemIdAt(0) == base,
           "reorder.independent-alpha-layer-moves-to-the-top-with-its-original-id");
    const auto reorderedBeta = owner->_preparedLayerRows->FindItem(base + 2);
    expect(SUCCEEDED(IdentityProviderName(rows[2].get(), name)) && name == L"Beta nouveau β" && reorderedBeta &&
               reorderedBeta->visibleIndex == 3,
           "reorder.retained-provider-follows-beta-rather-than-the-reused-visible-ordinal");
    expect(SUCCEEDED(betaSelection->Select()) && owner->_session.ActiveLayerID() == std::optional<Guid>(ids[2]) &&
               !owner->_session.IsMaskSelected(),
           "reorder.real-selection-pattern-selects-beta-exactly-through-current-id-lookup");
    if (FAILED(prepare()))
        return 5;
    owner->_session.AddBlankLayer();
    const auto inserted = owner->_session.ActiveLayerID();
    if (!inserted)
        return 5;
    owner->_session.RenameLayer(inserted.value(), L"Inserted δ");
    if (!settle())
        return 5;
    const auto insertedIndex = owner->_preparedLayerRows->FindVisibleRow(inserted.value());
    expect(insertedIndex && owner->_preparedLayerRows->ItemIdAt(insertedIndex.value()) >= base + 4 &&
               SUCCEEDED(IdentityProviderName(rows[2].get(), name)) && name == L"Beta nouveau β",
           "insertion.new-layer-gets-a-new-id-and-retained-beta-never-aliases-it");
    owner->_session.SelectLayer(ids[0]);
    owner->_session.SetGroupCollapsed(ids[1], true);
    if (!settle())
        return 5;
    expect(!owner->_preparedLayerRows->FindItem(base + 2) &&
               IdentityProviderName(rows[2].get(), name) == UIA_E_ELEMENTNOTAVAILABLE,
           "collapse.hidden-child-provider-is-unavailable-without-reusing-its-id");
    expect(betaSelection->Select() == UIA_E_ELEMENTNOTAVAILABLE &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(ids[0]),
           "collapse.stale-child-selection-cannot-select-an-unrelated-visible-row");
    owner->_session.SetGroupCollapsed(ids[1], false);
    if (!settle())
        return 5;
    expect(owner->_preparedLayerRows->FindItem(base + 2) && SUCCEEDED(IdentityProviderName(rows[2].get(), name)) &&
               name == L"Beta nouveau β" && betaRuntimeId == IdentityReadRuntimeId(rows[2].get()),
           "reopen.same-guid-child-recovers-its-original-name-id-and-runtime-id");
    owner->_session.DeleteLayer(ids[2]);
    if (!settle())
        return 5;
    expect(!owner->_preparedLayerRows->FindItem(base + 2) &&
               IdentityProviderName(rows[2].get(), name) == UIA_E_ELEMENTNOTAVAILABLE &&
               betaSelection->Select() == UIA_E_ELEMENTNOTAVAILABLE &&
               owner->_session.ActiveLayerID() == std::optional<Guid>(ids[0]),
           "deletion.retained-provider-and-action-stay-unavailable-after-ordinal-reuse");
    owner->_session.ReplaceDocument(original, ids[0]);
    if (!settle())
        return 5;
    expect(owner->_session.DocumentSerial() != originalSerial &&
               IdentityProviderName(rows[2].get(), name) == UIA_E_ELEMENTNOTAVAILABLE &&
               betaSelection->Select() == UIA_E_ELEMENTNOTAVAILABLE,
           "document-replacement.same-guids-do-not-revive-old-document-provider-authority");
    const auto restored = owner->_preparedLayerRows->FindVisibleRow(ids[2]);
    expect(restored && owner->_preparedLayerRows->ItemIdAt(restored.value()) > base + 3 &&
               owner->_preparedLayerRows->GetItem(restored.value())->text == L"Beta β",
           "document-replacement.current-beta-has-a-fresh-id-and-original-literal-name");
    bool literal = true;
    for (const auto& layer : owner->_session.Document()->layers)
        if (!layer.isGroup)
            literal = literal && layer.asset && layer.asset->image == image;
    expect(literal && literalAcrossTransitions, "all-transitions.preserve-independent-literal-pixel-storage");
    betaSelection.reset();
    for (auto& row : rows)
        row.reset();
    tree.reset();
    root.reset();
    if (SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) != SettleResult::Settled)
        return 5;
    expect(owner->_jobs.ReservedMemoryBytes() == owner->_preparedLayerRows->MemoryBytes(),
           "retirement.final-old-providers-release-every-original-epoch-charge");
    const auto nextId = owner->_nextLayerRowItemId;
    const auto publishedBeforeOverflow = owner->_preparedLayerRows;
    owner->_nextLayerRowItemId = (std::numeric_limits<std::uint64_t>::max)();
    owner->_session.RenameLayer(ids[0], L"Overflow retry α");
    owner->SyncPreparedLayerRows();
    owner->_layerRowsSource.Pump();
    expect(!owner->_layerRowsTicket && owner->_layerRowsRetryAvailable &&
               owner->_preparedLayerRows == publishedBeforeOverflow &&
               owner->_nextLayerRowItemId == (std::numeric_limits<std::uint64_t>::max)(),
           "range-overflow.refuses-before-capture-and-preserves-the-charged-old-epoch");
    owner->_nextLayerRowItemId = nextId;
    owner->RetryPreparedLayerRows();
    if (!settle())
        return 5;
    const auto retriedAlpha = owner->_preparedLayerRows->FindVisibleRow(ids[0]);
    expect(retriedAlpha &&
               owner->_preparedLayerRows->ItemIdAt(retriedAlpha.value()) ==
                   publishedBeforeOverflow->ItemIdAt(retriedAlpha.value()) &&
               owner->_preparedLayerRows->GetItem(retriedAlpha.value())->text == L"Overflow retry α" &&
               !owner->_layerRowsRetryAvailable,
           "range-overflow.explicit-retry-preserves-current-guid-identity-without-wraparound");
    owner->Shutdown();
    expect(owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
           "shutdown.retires-current-identity-map-and-shared-charge");
    const auto quit = PumpMessages();
    const auto remainder = PumpMessages();
    expect(quit.quit && quit.exitCode == 0 && !remainder.quit,
           "shutdown.consumes-one-quit-after-full-queued-message-drain");
    std::printf("%s application-prepared-layer-rows-identity.summary checks=%u failures=%d\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 5 : 0;
}

#include "Application.h"

#include "../Tests/Support/Support.Tests.PreparedLayerRowsStressFixture.h"
#include "BenchmarkMachine.h"
#include "ChromeHost.h"
#include "Kernels.h"
#include "PreparedLayerRows.h"
#include "Resources.h"
#include "Runtime.Tests.LayerMetadataTestGate.h"
#include "UiTiming.h"
#include "VersionInfo.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <format>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace
{
struct LayerRowsMeasurement final
{
    UiTimingStats owner;
    UiTimingHistogram request;
    UiTimingHistogram prepare;
    UiTimingHistogram navigation;
    UiTimingHistogram name;
    std::uint64_t elapsedUs = 0;
    std::uint64_t charge = 0;
    std::uint64_t namesRead = 0;
    std::uint64_t nameUnits = 0;
};

std::string RowsUtf8(std::wstring_view value)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                            size, nullptr, nullptr) != size)
        return {};
    return result;
}

bool RowsClock(std::int64_t& value) noexcept
{
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter))
        return false;
    value = counter.QuadPart;
    return true;
}

bool RowsRecord(UiTimingHistogram& histogram, std::int64_t begin, std::int64_t frequency) noexcept
{
    std::int64_t end = 0;
    if (!RowsClock(end))
        return false;
    const auto duration = UiDurationMicroseconds(begin, end, frequency);
    if (!duration)
        return false;
    histogram.Record(duration.value());
    return true;
}

bool RowsNamed(IRawElementProviderFragment* provider, std::wstring_view expected) noexcept
{
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant name;
    return provider && SUCCEEDED(provider->QueryInterface(IID_PPV_ARGS(simple.put()))) &&
           SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR && name.bstrVal &&
           std::wstring_view(name.bstrVal, SysStringLen(name.bstrVal)) == expected;
}

bool RowsLiteralPixels(const ImageLayer* first, const ImageLayer* last) noexcept
{
    if (!first || !first->asset || !first->mask || first->asset->image.Width() != 2 ||
        first->asset->image.Height() != 1 || first->mask->asset.image.Width() != 2 ||
        first->mask->asset.image.Height() != 1)
        return false;
    std::array<std::uint8_t, 8> image{};
    std::array<std::uint8_t, 2> mask{};
    first->asset->image.ReadRegion(0, 0, 2, 1, image.data(), 8, 0);
    first->mask->asset.image.ReadRegion(0, 0, 2, 1, mask.data(), 2, 0);
    if (image != PreparedLayerRowsStressFixture::kFirstExpectedBgra ||
        mask != PreparedLayerRowsStressFixture::kFirstExpectedMask)
        return false;
    if (last)
    {
        if (!last->asset || last->asset->image.Width() != 2 || last->asset->image.Height() != 1)
            return false;
        last->asset->image.ReadRegion(0, 0, 2, 1, image.data(), 8, 0);
        if (image != PreparedLayerRowsStressFixture::kLastExpectedBgra)
            return false;
    }
    return true;
}

wil::com_ptr_nothrow<IRawElementProviderFragment> RowsTree(IRawElementProviderFragment* parent,
                                                           std::wstring_view expected, unsigned int& remaining) noexcept
{
    wil::com_ptr_nothrow<IRawElementProviderFragment> result;
    if (!parent || remaining == 0)
        return result;
    --remaining;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    wil::unique_variant type;
    if (SUCCEEDED(parent->QueryInterface(IID_PPV_ARGS(simple.put()))) &&
        SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &type)) && type.vt == VT_I4 &&
        type.lVal == UIA_TreeControlTypeId && RowsNamed(parent, expected))
    {
        (void)parent->QueryInterface(IID_PPV_ARGS(result.put()));
        return result;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(parent->Navigate(NavigateDirection_FirstChild, child.put())))
        return result;
    while (child && remaining != 0)
    {
        result = RowsTree(child.get(), expected, remaining);
        if (result)
            return result;
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
            break;
        child = std::move(next);
    }
    return result;
}

void RowsReport(const LayerRowsMeasurement& measurement, unsigned int trial, std::size_t layers)
{
    std::printf(
        "LAYER ROWS BENCH trial=%u boundary=%s layers=%zu nameUnits=1024 elapsedWithWorkerWaitUs=%llu "
        "reservedBytes=%llu namesRead=%llu totalNameUnits=%llu clockFailures=%llu\n",
        trial, trial == 0 ? "initial" : "rename", layers, static_cast<unsigned long long>(measurement.elapsedUs),
        static_cast<unsigned long long>(measurement.charge), static_cast<unsigned long long>(measurement.namesRead),
        static_cast<unsigned long long>(measurement.nameUnits),
        static_cast<unsigned long long>(measurement.owner.clockFailures));
    const auto report = [trial](const char* name, const UiTimingHistogram& histogram) noexcept
    {
        std::printf("LAYER ROWS COST trial=%u kind=%s samples=%llu totalUs=%llu p99UpperUs=%llu maxUs=%llu "
                    "totalOverflow=%d\n",
                    trial, name, static_cast<unsigned long long>(histogram.SampleCount()),
                    static_cast<unsigned long long>(histogram.TotalMicroseconds()),
                    static_cast<unsigned long long>(histogram.PercentileUpperBound(99)),
                    static_cast<unsigned long long>(histogram.MaximumMicroseconds()), histogram.TotalOverflowed());
    };
    report("request", measurement.request);
    report("prepare", measurement.prepare);
    report("uia-navigation-server", measurement.navigation);
    report("uia-name-server", measurement.name);
    constexpr std::array<std::pair<UiTimingKind, const char*>, 5> kinds{{
        {UiTimingKind::MessageDispatch, "dispatch"},
        {UiTimingKind::WindowMessage, "window-message"},
        {UiTimingKind::CompletionDrain, "completion-drain"},
        {UiTimingKind::ResultApply, "result-apply"},
        {UiTimingKind::ChromeSync, "chrome-sync"},
    }};
    for (const auto& [kind, name] : kinds)
        report(name, measurement.owner.durations[static_cast<std::size_t>(kind)]);
    if (measurement.owner.attributionEnabled)
    {
        for (std::uint32_t index = 0; index < measurement.owner.attributionCursor; ++index)
        {
            const auto& record = measurement.owner.attributionRecords[index];
            std::printf("LAYER ROWS ATTRIBUTION trial=%u index=%u kind=%u phase=%u detail=%u wallUs=%llu "
                        "qpcBegin=%lld qpcEnd=%lld qpcFrequency=%lld cyclesKnown=%d threadCycles=%llu "
                        "sourceLine=%u diagnostic=1\n",
                        trial, index, static_cast<unsigned int>(record.kind), static_cast<unsigned int>(record.phase),
                        record.detail, static_cast<unsigned long long>(record.wallMicroseconds),
                        static_cast<long long>(record.qpcBegin), static_cast<long long>(record.qpcEnd),
                        static_cast<long long>(record.qpcFrequency), record.threadCycleKnown,
                        static_cast<unsigned long long>(record.threadCycleDelta),
                        static_cast<unsigned int>(record.line));
        }
        std::printf("LAYER ROWS ATTRIBUTION SUMMARY trial=%u count=%u overflow=%llu clockFailures=%llu "
                    "capacity=%zu minimumUs=%llu diagnostic=1\n",
                    trial, measurement.owner.attributionCursor,
                    static_cast<unsigned long long>(measurement.owner.attributionOverflowCount),
                    static_cast<unsigned long long>(measurement.owner.attributionClockFailures),
                    kUiTimingAttributionCapacity,
                    static_cast<unsigned long long>(kUiTimingAttributionMinimumMicroseconds));
    }
    std::fflush(stdout);
}
} // namespace

// Explicit metadata/provider cost harness. The hidden 2x1 canvas and direct embedded STA provider calls do not
// qualify visible frames, client RPC latency, GPU residency, or the complete G1 contract. Worker waits are not
// owner-work scopes. Every initial/rename boundary and every offscreen full name is reported without a warmup
// exclusion. Main performs the normal all-owner shutdown on every return.
int Application::RunLayerRowsBenchmark() noexcept
{
    if (!_screenshotPath.empty() || !_demo.empty())
        return 2;
    const char* step = "setup";
    unsigned int measuredTrial = 0;
    bool complete = false;
    const auto reportFailure = wil::scope_exit(
        [&]() noexcept
        {
            if (!complete)
                std::printf("LAYER ROWS BENCH REPORT status=failed trial=%u step=%s\n", measuredTrial, step);
            std::fflush(stdout);
        });
    try
    {
        _quietUi = true;
        _prompts = &QuietPrompts();
        _screenshotDpi = 0;
        _renderer.SetLayoutDpi(0);
        if (FAILED(RegisterWindowClass()) || FAILED(CreateMainWindow(false)) || FAILED(InitializeRenderer()) ||
            FAILED(InitializeChrome()))
            return 5;
        BindCompositionDevice();
        wchar_t attributionValue[2]{};
        const bool attribution = GetEnvironmentVariableW(L"REDPRISM_UI_TIMING_ATTRIBUTION", attributionValue, 2) == 1 &&
                                 attributionValue[0] == L'1';
        const bool smoke = _bench == L"layer-rows-smoke";
        const std::size_t layers = smoke ? 128 : PreparedLayerRowsStressFixture::kLayerCount;
        const unsigned int renames = smoke ? 2 : 10;
        auto fixture = MakePreparedLayerRowsStressFixture();
        if (smoke)
        {
            const auto lastAsset = fixture.builder.layers.back().asset;
            fixture.builder.layers.resize(layers);
            fixture.builder.layers.back().asset = lastAsset;
        }
        // Use the final storage record to include the application's longest active-layer lookup path.
        const Guid target = PreparedLayerRowsStressFixtureDetail::LayerId(layers - 1);
        _session.ReplaceDocument(std::move(fixture.builder).Seal(), target);
        const auto* initialLayer = _session.ActiveLayer();
        if (!initialLayer || !initialLayer->asset)
            return 5;
        const auto originalImage = initialLayer->asset->image;
        const auto originalItemId = PreparedLayerRows::FirstItemIdentity + layers - 1;
        const auto originalRevision = _session.history.CurrentRevision();
        const auto originalUndoCount = _session.history.UndoCount();
        auto machine = ReadBenchmarkMachine();
        machine.applicationVersion = RowsUtf8(RedPrism::FormatRunningModuleVersion(L"unknown"));
        machine.isa = RowsUtf8(KernelIsaName(ActiveKernels().isa));
        machine.workerCount = _jobs.WorkerCount();
        wil::com_ptr_nothrow<IDXGIDevice> dxgi;
        wil::com_ptr_nothrow<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (!_renderer.Device() || FAILED(_renderer.Device()->QueryInterface(IID_PPV_ARGS(dxgi.put()))) ||
            FAILED(dxgi->GetAdapter(adapter.put())) || FAILED(adapter->GetDesc(&description)))
            return 5;
        machine.adapterName = RowsUtf8(description.Description);
        machine.adapterLuid = std::format("{:08x}:{:08x}", static_cast<std::uint32_t>(description.AdapterLuid.HighPart),
                                          description.AdapterLuid.LowPart);
        LARGE_INTEGER driver{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
            machine.driverVersion = std::format("{}.{}.{}.{}", HIWORD(driver.HighPart), LOWORD(driver.HighPart),
                                                HIWORD(driver.LowPart), LOWORD(driver.LowPart));
        std::printf("LAYER ROWS MACHINE %s device=%s smoke=%d uia=embedded-STA-server frameGate=unmeasured\n",
                    FormatBenchmarkMachineInfo(machine).c_str(), _renderer.IsWarp() ? "WARP" : "hardware", smoke);
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            return 5;
        const auto treeName = LoadStringResource(nullptr, IDS_A11Y_LAYERS);
        if (treeName.empty())
            return 5;
        const UINT dpi = GetDpiForWindow(_window.get());
        // Establish the real empty Tree viewport before selecting a deep row. This setup has no row source;
        // each later full row publication and Prepare remains measured, including the initial one.
        _chrome->Layout(_renderer.Width(), _renderer.Height(), dpi ? static_cast<float>(dpi) : 96.0f);
        const auto prepare = [&]() noexcept
        { return _chrome->Prepare(_renderer.Width(), _renderer.Height(), dpi ? static_cast<float>(dpi) : 96.0f); };
        std::wstring renamed = PreparedLayerRowsStressFixtureDetail::LayerName(layers - 1);
        std::uint64_t ownedMetadataBytes = 0;
        std::uint64_t originalMetadataCount = 0;
        for (unsigned int trial = 0; trial <= renames; ++trial)
        {
            measuredTrial = trial;
            step = "request";
            LayerRowsMeasurement measurement;
            measurement.owner.attributionEnabled = attribution;
            // Harness-only weak provenance observations; no hold, failure injection or useful work.
            // Original admission is still measured. Weak controls are reset before the retirement oracle.
            std::shared_ptr<LayerMetadataTestGate> accounting;
            if (trial != 0)
            {
                accounting = std::make_shared<LayerMetadataTestGate>();
                accounting->hold = false;
                _layerMetadataTestGate = accounting;
            }
            const auto clearAccounting = wil::scope_exit([&]() noexcept { _layerMetadataTestGate.reset(); });
            const auto beforePublication = _chrome->Statistics();
            const UiTimingCapture capture{measurement.owner};
            std::int64_t start = 0;
            if (!RowsClock(start))
                return 5;
            if (trial == 0)
            {
                if (!EnablePreparedLayerRows() || !RowsRecord(measurement.request, start, frequency.QuadPart))
                    return 5;
            }
            else
            {
                // Build this independent 1024-unit reference outside the request's timed boundary.
                renamed[1016] = static_cast<wchar_t>(L'A' + trial);
                renamed[1017] = L'\u03a9';
                std::int64_t requestStart = 0;
                if (!RowsClock(requestStart))
                    return 5;
                _renameLayer = target;
                OnRename(std::wstring_view(renamed));
                if (!RowsRecord(measurement.request, requestStart, frequency.QuadPart))
                    return 5;
            }
            step = "publication";
            if (SettleJobs(_jobs, 30000, DispatchMessageThunk, this) != SettleResult::Settled || !_preparedLayerRows ||
                _preparedLayerRows->GetCount() != layers || _preparedLayerRows->ItemIdAt(0) != originalItemId ||
                !_jobs.IsIdle() || !_preparedLayerRows->HasMemoryLease())
                return 5;
            std::int64_t end = 0;
            if (!RowsClock(end))
                return 5;
            const auto elapsed = UiDurationMicroseconds(start, end, frequency.QuadPart);
            if (!elapsed)
                return 5;
            measurement.elapsedUs = elapsed.value();
            // Publication updates retained state. Painting and its GPU work belong to the later frame.
            // This registered smoke invariant detects an accidental Prepare in request/completion paths.
            step = "owner-state-only";
            const auto afterPublication = _chrome->Statistics();
            if (afterPublication.preparations != beforePublication.preparations ||
                afterPublication.composites != beforePublication.composites ||
                afterPublication.surfaceAllocations != beforePublication.surfaceAllocations)
                return 5;
            step = "prepare";
            const auto prepareBefore = attribution ? _chrome->Statistics() : DxUi::EmbeddedStatistics{};
            std::int64_t prepareStart = 0;
            if (!RowsClock(prepareStart) || FAILED(prepare()) ||
                !RowsRecord(measurement.prepare, prepareStart, frequency.QuadPart))
                return 5;
            if (attribution)
            {
                const auto after = _chrome->Statistics();
                std::printf("LAYER ROWS PREPARE STATE trial=%u preparationsBefore=%llu preparationsAfter=%llu "
                            "allocationsBefore=%llu allocationsAfter=%llu surfaceBytes=%llu replacementPeakBytes=%llu "
                            "brushesBefore=%llu brushesAfter=%llu formatsBefore=%llu formatsAfter=%llu diagnostic=1\n",
                            trial, static_cast<unsigned long long>(prepareBefore.preparations),
                            static_cast<unsigned long long>(after.preparations),
                            static_cast<unsigned long long>(prepareBefore.surfaceAllocations),
                            static_cast<unsigned long long>(after.surfaceAllocations),
                            static_cast<unsigned long long>(after.surfaceBytes),
                            static_cast<unsigned long long>(after.replacementPeakBytes),
                            static_cast<unsigned long long>(prepareBefore.cachedBrushes),
                            static_cast<unsigned long long>(after.cachedBrushes),
                            static_cast<unsigned long long>(prepareBefore.cachedTextFormats),
                            static_cast<unsigned long long>(after.cachedTextFormats));
            }
            wil::com_ptr_nothrow<IRawElementProviderFragment> root;
            auto* rawRoot = _chrome->AccessibilityRoot();
            if (!rawRoot || FAILED(rawRoot->QueryInterface(IID_PPV_ARGS(root.put()))))
                return 5;
            unsigned int budget = 512;
            step = "provider-root";
            auto tree = RowsTree(root.get(), treeName, budget);
            wil::com_ptr_nothrow<IRawElementProviderFragment> row;
            if (!tree || FAILED(tree->Navigate(NavigateDirection_FirstChild, row.put())))
                return 5;
            for (std::size_t visible = 0; visible < layers; ++visible)
            {
                step = "provider-name";
                const auto storage = layers - 1 - visible;
                const auto expected =
                    storage == layers - 1 ? renamed : PreparedLayerRowsStressFixtureDetail::LayerName(storage);
                std::int64_t nameStart = 0;
                if (!RowsClock(nameStart) || !RowsNamed(row.get(), expected) ||
                    !RowsRecord(measurement.name, nameStart, frequency.QuadPart))
                    return 5;
                ++measurement.namesRead;
                measurement.nameUnits += expected.size();
                wil::com_ptr_nothrow<IRawElementProviderFragment> next;
                step = "provider-navigation";
                std::int64_t navigateStart = 0;
                if (!RowsClock(navigateStart) || FAILED(row->Navigate(NavigateDirection_NextSibling, next.put())) ||
                    !RowsRecord(measurement.navigation, navigateStart, frequency.QuadPart))
                    return 5;
                row = std::move(next);
            }
            const auto* active = _session.ActiveLayer();
            step = "independent-reference";
            const auto& document = std::as_const(_session).Document();
            if (row || measurement.namesRead != layers || measurement.owner.clockFailures != 0 ||
                _session.ActiveLayerID() != target || !active || !active->asset ||
                active->asset->image != originalImage || !document || document->layers.size() != layers ||
                !RowsLiteralPixels(&document->layers.front(), &document->layers.back()) ||
                _session.history.UndoCount() != originalUndoCount + static_cast<int>(trial) ||
                (trial == 0 && _session.history.CurrentRevision() != originalRevision))
                return 5;
            tree.reset();
            root.reset();
            step = "retirement";
            if (SettleJobs(_jobs, 30000, DispatchMessageThunk, this) != SettleResult::Settled)
                return 5;
            if (accounting)
            {
                if (!accounting->originalAccountingBound || !accounting->retired.load() ||
                    !accounting->retiredOffOwner.load() || accounting->captures.load() != 1 ||
                    !accounting->accountingTemporary.expired())
                    return 5;
                auto output = accounting->accountingOutput.lock();
                const auto bytes = accounting->accountingOutputBytes;
                if (!output || !bytes || !output->BoundTo(_jobs, accounting->accountingTicket) ||
                    output->Bytes() != bytes || bytes < SharedPublicationMemory::OwnershipAllowance() ||
                    output->LocalAllocationBytes() > bytes - SharedPublicationMemory::OwnershipAllowance() ||
                    bytes > (std::numeric_limits<std::uint64_t>::max)() - ownedMetadataBytes)
                    return 5;
                ownedMetadataBytes += bytes;
                ++originalMetadataCount;
                accounting->accountingTemporary.reset();
                accounting->accountingOutput.reset();
                output.reset();
                // Consume only actual shared-control retirement. This never cancels live history/document output.
                if (SettleJobs(_jobs, 30000, DispatchMessageThunk, this) != SettleResult::Settled)
                    return 5;
            }
            const auto rowBytes = _preparedLayerRows->MemoryBytes();
            if (rowBytes > (std::numeric_limits<std::uint64_t>::max)() - ownedMetadataBytes)
                return 5;
            measurement.charge = _jobs.ReservedMemoryBytes();
            // The independent history count above proves every prior metadata epoch remains in this untrimmed
            // literal sequence. Count each observed original output admission once; old row caches and metadata
            // temporary phases must retire. Any unexplained charge, missing output or releasing work still fails.
            if (measurement.charge != rowBytes + ownedMetadataBytes || _jobs.ReleasingMemoryBytes() != 0 ||
                originalMetadataCount != trial)
            {
                std::printf("LAYER ROWS RETIRE DIAG trial=%u reservedBytes=%llu rowBytes=%llu metadataBytes=%llu "
                            "releasingBytes=%llu originals=%llu\n",
                            trial, static_cast<unsigned long long>(measurement.charge),
                            static_cast<unsigned long long>(rowBytes),
                            static_cast<unsigned long long>(ownedMetadataBytes),
                            static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()),
                            static_cast<unsigned long long>(originalMetadataCount));
                return 5;
            }
            std::printf("LAYER ROWS ACCOUNTING trial=%u reservedBytes=%llu rowBytes=%llu metadataBytes=%llu "
                        "releasingBytes=0 originals=%llu\n",
                        trial, static_cast<unsigned long long>(measurement.charge),
                        static_cast<unsigned long long>(rowBytes), static_cast<unsigned long long>(ownedMetadataBytes),
                        static_cast<unsigned long long>(originalMetadataCount));
            RowsReport(measurement, trial, layers);
        }
        std::printf("LAYER ROWS BENCH REPORT status=ok layers=%zu renames=%u smoke=%d performanceGate=unmeasured\n",
                    layers, renames, smoke);
        complete = true;
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return 5; // Opt-in harness setup/admission failed; Main still joins all owners.
    }
}

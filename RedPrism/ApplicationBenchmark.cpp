#include "Application.h"

#include "DocumentDraft.h"

#include "BenchmarkMachine.h"
#include "BrushStroke.h"
#include "ChromeHost.h"
#include "Kernels.h"
#include "UiBenchmark.h"
#include "VersionInfo.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <new>
#include <string_view>
#include <utility>

namespace
{
enum class Expected
{
    View,
    Histogram,
    Panel,
    Pixels,
    Size,
    Refusal,
    Saved,
    Opened,
    PngExported,
    JpegExported,
    ImportedProject,
    ImportedLayers,
    ImportedLayersRefused,
    GradientPreview,
    GradientApplied
};

struct Operation final
{
    const char* name;
    int prepareCommand;
    int command;
    AdjustAction action;
    double value;
    Expected expected;
    bool sizeAction = false;
};

constexpr std::array<Operation, 26> kOperations{{
    {"zoom-actual", 0, ChromeHost::kCommandZoomActual, {}, 0, Expected::View},
    {"zoom-fit", 0, ChromeHost::kCommandZoomFit, {}, 0, Expected::View},
    {"levels-open", 0, ChromeHost::kCommandLevels, {}, 0, Expected::Histogram},
    {"levels-update", ChromeHost::kCommandLevels, 0, AdjustAction::SetInputGamma, 1.25, Expected::Panel},
    {"levels-commit", ChromeHost::kCommandLevels, 0, AdjustAction::Apply, 0, Expected::Pixels},
    {"hsv-update", ChromeHost::kCommandHueSaturation, 0, AdjustAction::SetSaturation, 20, Expected::Panel},
    {"exposure-update", ChromeHost::kCommandExposure, 0, AdjustAction::SetExposure, 0.5, Expected::Panel},
    {"gaussian-update", ChromeHost::kCommandGaussianBlur, 0, AdjustAction::SetRadius, 10, Expected::Panel},
    {"noise-update", ChromeHost::kCommandAddNoise, 0, AdjustAction::SetAmount, 20, Expected::Panel},
    {"grain-update", ChromeHost::kCommandGrain, 0, AdjustAction::SetGrainAmount, 40, Expected::Panel},
    {"lens-update", ChromeHost::kCommandLensCorrection, 0, AdjustAction::SetDistortion, 20, Expected::Panel},
    {"invert", 0, ChromeHost::kCommandInvert, {}, 0, Expected::Pixels},
    {"fill", 0, ChromeHost::kCommandFillForeground, {}, 0, Expected::Pixels},
    {"clear", ChromeHost::kCommandSelectAll, ChromeHost::kCommandClearPixels, {}, 0, Expected::Pixels},
    {"undo", ChromeHost::kCommandInvert, ChromeHost::kCommandUndo, {}, 0, Expected::Pixels},
    {"canvas-size-fill", ChromeHost::kCommandCanvasSize, 0, {}, 0, Expected::Size, true},
    {"image-size-nearest", ChromeHost::kCommandImageSize, 0, {}, 0, Expected::Size, true},
    {"image-size-resolution", ChromeHost::kCommandImageSize, 0, {}, 300, Expected::Size, true},
    {"project-save", 0, 0, {}, 0, Expected::Saved},
    {"project-open", 0, 0, {}, 0, Expected::Opened},
    {"png-export", 0, 0, {}, 0, Expected::PngExported},
    {"image-import-new-canvas", 0, 0, {}, 0, Expected::ImportedProject},
    {"image-import-existing-layer", 0, 0, {}, 0, Expected::ImportedLayers},
    {"jpeg-export", 0, 0, {}, 0, Expected::JpegExported},
    {"gradient-preview-coalesced", 0, 0, {}, 0, Expected::GradientPreview},
    {"gradient-apply-ordered", 0, 0, {}, 0, Expected::GradientApplied},
}};

Operation CurrentOperation(const UiBenchmarkState& state) noexcept
{
    auto operation = kOperations[state.operation];
    if (state.fixture.width == 10000 && operation.prepareCommand == ChromeHost::kCommandGaussianBlur)
    {
        // At today's pixel ceiling even the initial blur grow is refused. Measure that real command
        // explicitly; do not label a missing preview or an ignored slider action as image processing.
        operation = {"gaussian-refused-pixel-budget", 0, ChromeHost::kCommandGaussianBlur, {}, 0, Expected::Refusal};
    }
    if (operation.prepareCommand == ChromeHost::kCommandCanvasSize)
    {
        std::int64_t usedPixels = 0;
        for (const auto& layer : state.fixture.layers)
            if (layer.asset)
                usedPixels += static_cast<std::int64_t>(layer.asset->image.Width()) * layer.asset->image.Height();
        if (static_cast<std::int64_t>(state.fixture.width + 16) * (state.fixture.height + 16) >
            kMaxLayerPixels - usedPixels)
            operation = {"canvas-size-fill-refused-pixel-budget",
                         ChromeHost::kCommandCanvasSize,
                         0,
                         {},
                         0,
                         Expected::Refusal,
                         true};
    }
    if (operation.expected == Expected::ImportedLayers &&
        static_cast<std::int64_t>(state.fixture.width) * state.fixture.height * 2 > kMaxLayerPixels)
    {
        operation = {"image-import-existing-refused-pixel-budget", 0, 0, {}, 0, Expected::ImportedLayersRefused};
    }
    return operation;
}

[[nodiscard]] std::int64_t Now() noexcept
{
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : -1;
}

[[nodiscard]] std::string Utf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int length = static_cast<int>(value.size());
    const int count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr);
    if (count <= 0)
    {
        return "unknown";
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(), count, nullptr,
                            nullptr) != count)
    {
        return "unknown";
    }
    return result;
}

[[nodiscard]] Raster Pattern(int width, int height, unsigned int seed, bool mask, std::uint64_t& checksum)
{
    const unsigned int channels = mask ? 1 : 4;
    auto buffer = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * height * channels);
    if (!buffer)
    {
        return {};
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            auto* pixel = buffer.Data() + (static_cast<std::size_t>(y) * width + x) * channels;
            if (mask)
            {
                pixel[0] = static_cast<std::uint8_t>(128 + ((x / 32 + y / 16) & 127));
            }
            else
            {
                pixel[0] = static_cast<std::uint8_t>((x * 17 + y * 3 + seed) & 255);
                pixel[1] = static_cast<std::uint8_t>((x * 5 + y * 11 + seed) & 255);
                pixel[2] = static_cast<std::uint8_t>((x * 13 + y * 7 + seed) & 255);
                pixel[3] = 255;
            }
            for (unsigned int channel = 0; channel < channels; ++channel)
            {
                checksum = (checksum ^ pixel[channel]) * 1099511628211ULL;
            }
        }
    }
    return Raster::Adopt(width, height, mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888, std::move(buffer));
}

[[nodiscard]] bool GradientEndpointPixelsMatch(const Raster& image, int row) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || image.Width() < 2 || row < 0 ||
        row >= image.Height())
        return false;
    std::array<std::uint8_t, 4> first{};
    std::array<std::uint8_t, 4> last{};
    image.ReadRegion(0, row, 1, 1, first.data(), 4, 0);
    image.ReadRegion(image.Width() - 1, row, 1, 1, last.data(), 4, 0);
    // Independent fixture oracle: opaque foreground #936427 at t=0 and white background at t=1.
    constexpr std::array<std::uint8_t, 4> kForegroundBgra{0x27, 0x64, 0x93, 0xff};
    constexpr std::array<std::uint8_t, 4> kBackgroundBgra{0xff, 0xff, 0xff, 0xff};
    return first == kForegroundBgra && last == kBackgroundBgra;
}

[[nodiscard]] bool ReadGradientPreviewPixel(const BrushStroke& preview, int x, int y,
                                            std::array<std::uint8_t, 4>& pixel) noexcept
{
    for (const auto& tile : preview.Tiles())
    {
        if (!tile.image.HasStorage() || x < tile.rect.x || y < tile.rect.y || x >= tile.rect.x + tile.rect.width ||
            y >= tile.rect.y + tile.rect.height)
            continue;
        tile.image.ReadRegion(x - tile.rect.x, y - tile.rect.y, 1, 1, pixel.data(), 4, 0);
        return true;
    }
    return false;
}

[[nodiscard]] bool GradientPreviewEndpointPixelsMatch(const BrushStroke& preview, int width, int row) noexcept
{
    std::array<std::uint8_t, 4> first{};
    std::array<std::uint8_t, 4> last{};
    return width >= 2 && ReadGradientPreviewPixel(preview, 0, row, first) &&
           ReadGradientPreviewPixel(preview, width - 1, row, last) &&
           first == std::array<std::uint8_t, 4>{0x27, 0x64, 0x93, 0xff} &&
           last == std::array<std::uint8_t, 4>{0xff, 0xff, 0xff, 0xff};
}

HRESULT CreateBenchmark(std::wstring_view mode, std::unique_ptr<UiBenchmarkState>& state) noexcept
{
    // Setup is a recoverable construction boundary. Nothing measured or published exists on failure.
    try
    {
        auto next = std::make_unique<UiBenchmarkState>();
        const bool smoke = mode == L"ui-smoke";
        const bool ceiling = mode == L"ui100";
        const int width = smoke ? 64 : ceiling ? 10000 : mode == L"ui45" ? 8192 : 3840;
        const int height = smoke ? 48 : ceiling ? 10000 : mode == L"ui45" ? 5464 : 2160;
        next->runs = smoke ? 2 : 10;
        next->smoke = smoke;
        const auto temporaryStorage = std::make_unique<std::array<wchar_t, 32768>>();
        auto& temporary = *temporaryStorage;
        const DWORD units = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (units == 0 || units >= temporary.size())
            return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        next->saveDirectory =
            std::wstring(temporary.data(), units) + L"RedPrism.UiBench." + Guid::Generate().ToString();
        if (!CreateDirectoryW(next->saveDirectory.c_str(), nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        next->ownsSaveDirectory = true;
        next->savePath = next->saveDirectory + L"\\saved.prism";
        next->pngExportPath = next->saveDirectory + L"\\export.png";
        next->jpegExportPath = next->saveDirectory + L"\\export.jpg";
        next->fixture.id = Guid::Generate();
        next->fixture.width = width;
        next->fixture.height = height;
        next->fixtureChecksum = 14695981039346656037ULL;
        const Vec2 size{static_cast<double>(width), static_cast<double>(height)};
        auto top = ImageLayer::Blank(L"Benchmark active", size);
        Raster image = Pattern(width, height, 29, false, next->fixtureChecksum);
        if (!image.HasStorage())
        {
            return E_OUTOFMEMORY;
        }
        top.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Benchmark active"};
        next->activeLayer = top.id;
        next->imageImportBaseline.id = Guid::Generate();
        next->imageImportBaseline.width = width;
        next->imageImportBaseline.height = height;
        auto importDraft = CanvasDocumentDraft::Rebuild(next->imageImportBaseline, 1);
        auto fixtureDraft = CanvasDocumentDraft::Rebuild(next->fixture, ceiling ? 1 : 3);
        if (!importDraft || !fixtureDraft)
            return E_INVALIDARG;
        auto importBaseLayer = top;
        importBaseLayer.mask.reset();
        if (!importDraft.value().Append(std::move(importBaseLayer)))
            return E_INVALIDARG;
        if (!ceiling)
        {
            Raster mask = Pattern(width, height, 0, true, next->fixtureChecksum);
            Raster bottomPixels = Pattern(width, height, 71, false, next->fixtureChecksum);
            if (!mask.HasStorage() || !bottomPixels.HasStorage())
            {
                return E_OUTOFMEMORY;
            }
            top.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Benchmark mask"}};
            auto bottom = ImageLayer::Blank(L"Benchmark bottom", size);
            bottom.asset = RasterAsset{bottomPixels, Raster::MakeThumbnail(bottomPixels), L"Benchmark bottom"};
            if (!fixtureDraft.value().Append(std::move(bottom)))
                return E_INVALIDARG;
        }
        if (!fixtureDraft.value().Append(std::move(top)))
            return E_INVALIDARG;
        if (!ceiling)
        {
            ImageLayer adjustment;
            adjustment.id = Guid::Generate();
            adjustment.name = L"Benchmark exposure adjustment";
            adjustment.transform.size = size;
            adjustment.adjustment = LayerAdjustment{};
            adjustment.adjustment->kind = AdjustmentKind::Exposure;
            adjustment.adjustment->exposureSettings = ExposureSettings{0.25, 0, 1};
            if (!fixtureDraft.value().Append(std::move(adjustment)))
                return E_INVALIDARG;
        }
        auto importDocument = importDraft.value().Seal();
        auto fixtureDocument = fixtureDraft.value().Seal();
        if (!importDocument || !fixtureDocument)
            return E_INVALIDARG;
        next->imageImportBaseline = std::move(importDocument.value());
        next->fixture = std::move(fixtureDocument.value());
        if (ValidateDocument(next->fixture) != DocumentStatus::Ok)
        {
            return E_INVALIDARG;
        }
        if (ValidateDocument(next->imageImportBaseline) != DocumentStatus::Ok ||
            next->imageImportBaseline.layers.size() != 1 || !next->imageImportBaseline.layers.front().asset.has_value())
        {
            return E_INVALIDARG;
        }
        next->machine = ReadBenchmarkMachine();
        state = std::move(next);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

struct Distribution final
{
    std::uint64_t p99UpperUs = 0;
    std::uint64_t maximumUs = 0;
};

Distribution Summarize(const UiBenchmarkState& state, UiTimingKind kind) noexcept
{
    std::array<std::uint64_t, UiTimingHistogram::kBucketCount> buckets{};
    std::uint64_t count = 0;
    Distribution result;
    for (unsigned int run = 0; run < state.runs; ++run)
    {
        const auto& histogram = state.samples[run].timing.durations[static_cast<std::size_t>(kind)];
        count += histogram.SampleCount();
        result.maximumUs = (std::max)(result.maximumUs, histogram.MaximumMicroseconds());
        for (std::size_t bucket = 0; bucket < buckets.size(); ++bucket)
        {
            buckets[bucket] += histogram.BucketCounts()[bucket];
        }
    }
    if (count == 0)
    {
        return result;
    }
    const std::uint64_t rank = count / 100 * 99 + ((count % 100) * 99 + 99) / 100;
    std::uint64_t cumulative = 0;
    for (std::size_t bucket = 0; bucket < buckets.size(); ++bucket)
    {
        cumulative += buckets[bucket];
        if (cumulative >= rank)
        {
            result.p99UpperUs =
                bucket + 1 == buckets.size() ? result.maximumUs : UiTimingHistogram::kUpperBounds[bucket];
            break;
        }
    }
    return result;
}

void Report(const UiBenchmarkState& state)
{
    std::array<std::uint64_t, 10> elapsed{};
    std::uint64_t frames = 0;
    GpuReadbackStatistics readback;
    for (unsigned int run = 0; run < state.runs; ++run)
    {
        elapsed[run] = state.samples[run].elapsedUs;
        frames += state.samples[run].frames;
        readback.copies += state.samples[run].readback.copies;
        readback.maps += state.samples[run].readback.maps;
        readback.readinessBarriers += state.samples[run].readback.readinessBarriers;
        readback.busyFailures += state.samples[run].readback.busyFailures;
    }
    std::sort(elapsed.begin(), elapsed.begin() + state.runs);
    const auto dispatch = Summarize(state, UiTimingKind::MessageDispatch);
    const auto message = Summarize(state, UiTimingKind::WindowMessage);
    const auto drain = Summarize(state, UiTimingKind::CompletionDrain);
    const auto apply = Summarize(state, UiTimingKind::ResultApply);
    const auto action = Summarize(state, UiTimingKind::UiAction);
    const auto frame = Summarize(state, UiTimingKind::Frame);
    const double median =
        static_cast<double>(elapsed[(state.runs - 1) / 2]) * 0.5 + static_cast<double>(elapsed[state.runs / 2]) * 0.5;
    std::printf("%s\n",
                std::format("UI BENCH name={} width={} height={} runs={} warmup=1 elapsedMedianUs={:.1f} "
                            "elapsedP95Us={} elapsedMaxUs={} "
                            "dispatchP99UpperUs={} dispatchMaxUs={} messageP99UpperUs={} messageMaxUs={} "
                            "drainP99UpperUs={} drainMaxUs={} "
                            "applyMaxUs={} actionMaxUs={} frameP99UpperUs={} frameMaxUs={} frames={} "
                            "readbackCopies={} readbackMaps={} readinessBarriers={} busyFailures={}",
                            CurrentOperation(state).name, state.fixture.width, state.fixture.height, state.runs, median,
                            elapsed[(state.runs * 95 + 99) / 100 - 1], elapsed[state.runs - 1], dispatch.p99UpperUs,
                            dispatch.maximumUs, message.p99UpperUs, message.maximumUs, drain.p99UpperUs,
                            drain.maximumUs, apply.maximumUs, action.maximumUs, frame.p99UpperUs, frame.maximumUs,
                            frames, readback.copies, readback.maps, readback.readinessBarriers, readback.busyFailures)
                    .c_str());
    std::fflush(stdout);
}
} // namespace

UiBenchmarkState::~UiBenchmarkState()
{
    // Application shutdown has already joined every writer. This opt-in harness owns the exclusively created
    // directory and only these three result paths; never recursively remove unexpected contents or a caller's
    // destination. The directory is removed only after those known files are gone.
    if (ownsSaveDirectory)
    {
        if (!savePath.empty())
            (void)DeleteFileW(savePath.c_str());
        if (!pngExportPath.empty())
            (void)DeleteFileW(pngExportPath.c_str());
        if (!jpegExportPath.empty())
            (void)DeleteFileW(jpegExportPath.c_str());
        if (!RemoveDirectoryW(saveDirectory.c_str()))
            std::fprintf(stderr, "UI BENCH cleanup failed win32=%lu\n", GetLastError());
    }
}

int Application::RunUiBenchmark() noexcept
{
    if (!_screenshotPath.empty() || !_demo.empty())
    {
        return 2;
    }
    // This opt-in foreground-frame measurement needs an awake display, without periodic keep-alive calls.
    // Restore this thread's prior request on every exit; ordinary application runs never take this lease.
    const EXECUTION_STATE previousExecution =
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (previousExecution == 0)
    {
        std::printf("UI BENCH REPORT status=failed reason=display-lease\n");
        return 5;
    }
    const auto restoreExecution =
        wil::scope_exit([previousExecution]() noexcept { (void)SetThreadExecutionState(previousExecution); });
    const HRESULT status = CreateBenchmark(_bench, _uiBenchmark);
    if (FAILED(status))
    {
        std::printf("UI BENCH REPORT status=failed setup=0x%08lx\n", static_cast<unsigned long>(status));
        return 5;
    }
    _quietUi = true;
    _prompts = &QuietPrompts();
    _screenshotDpi = 0;
    _renderer.SetLayoutDpi(0);
    const UiTimingCapture capture{_uiBenchmark->timing};
    const int result = Run(SW_SHOWNOACTIVATE);
    return result == 0 && _uiBenchmark->complete && !_uiBenchmark->failed ? 0 : 5;
}

HRESULT Application::InitializeUiBenchmark() noexcept
{
    try
    {
        auto& state = *_uiBenchmark;
        auto& machine = state.machine;
        machine.applicationVersion = Utf8(RedPrism::FormatRunningModuleVersion(L"unknown"));
        machine.isa = Utf8(KernelIsaName(ActiveKernels().isa));
        machine.workerCount = _jobs.WorkerCount();
        wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
        wil::com_ptr_nothrow<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (_renderer.Device() && SUCCEEDED(_renderer.Device()->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) && SUCCEEDED(adapter->GetDesc(&description)))
        {
            machine.adapterName = Utf8(description.Description);
            machine.adapterLuid =
                std::format("{:08x}:{:08x}", static_cast<std::uint32_t>(description.AdapterLuid.HighPart),
                            description.AdapterLuid.LowPart);
            LARGE_INTEGER driver{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
            {
                machine.driverVersion = std::format("{}.{}.{}.{}", HIWORD(driver.HighPart), LOWORD(driver.HighPart),
                                                    HIWORD(driver.LowPart), LOWORD(driver.LowPart));
            }
        }
        std::printf("BENCH MACHINE %s device=%s fixtureChecksum=%016llx\n", FormatBenchmarkMachineInfo(machine).c_str(),
                    _renderer.IsWarp() ? "WARP" : "hardware", static_cast<unsigned long long>(state.fixtureChecksum));
        // A single failure deadline for an opt-in harness, never a normal application polling timer.
        if (SetTimer(_window.get(), kUiBenchmarkTimerId, _bench == L"ui-smoke" ? 90000 : 1800000, nullptr) == 0)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        PrepareUiBenchmarkStep();
        return state.failed ? E_FAIL : S_OK;
    }
    catch (const std::bad_alloc&)
    {
        // Setup failed; Run exits and Main performs the normal all-owner shutdown.
        return E_OUTOFMEMORY;
    }
}

void Application::FailUiBenchmark(const char* reason) noexcept
{
    if (_uiBenchmark && !_uiBenchmark->failed)
    {
        if (_uiBenchmark->exportReceipt)
        {
            const auto result = _uiBenchmark->exportReceipt->Read();
            const auto* layer = _session.ActiveLayer();
            std::printf("UI BENCH DETAIL writerOutcome=%u writerError=0x%08x pathSame=%d revisionSame=%d "
                        "modifiedSame=%d imageSame=%d errorId=%u\n",
                        static_cast<unsigned int>(result.outcome), static_cast<unsigned int>(result.error),
                        std::wstring_view(_documentPath) == std::wstring_view(_uiBenchmark->previousDocumentPath.data(),
                                                                              _uiBenchmark->previousDocumentPathUnits),
                        _session.history.CurrentRevision() == _uiBenchmark->previousRevision,
                        _session.history.IsModified() == _uiBenchmark->previousModified,
                        layer && layer->asset && layer->asset->image == _uiBenchmark->previousImage, _lastErrorId);
        }
        _uiBenchmark->failed = true;
        _uiBenchmark->phase = UiBenchmarkState::Phase::Done;
        std::printf("UI BENCH REPORT status=failed reason=%s operation=%zu name=%s trial=%u "
                    "visible=%d displayOn=%d suspended=%d occluded=%d jobsIdle=%d presents=%llu expectedAfter=%llu "
                    "frameFailure=0x%08lx canvasFailure=0x%08lx failedStage=%u\n",
                    reason, _uiBenchmark->operation, CurrentOperation(*_uiBenchmark).name, _uiBenchmark->trial,
                    _windowVisible, _displayPoweredOn, _renderer.IsSuspended(), _renderer.IsOccluded(), _jobs.IsIdle(),
                    static_cast<unsigned long long>(_renderer.PresentCount()),
                    static_cast<unsigned long long>(_uiBenchmark->previousPresents),
                    static_cast<unsigned long>(_lastFrameFailure),
                    static_cast<unsigned long>(_renderer.CanvasFailure()),
                    static_cast<unsigned int>(_renderer.FailedStage()));
        std::fflush(stdout);
        (void)KillTimer(_window.get(), kUiBenchmarkTimerId);
        PostQuitMessage(5);
    }
}

void Application::PrepareUiBenchmarkStep() noexcept
{
    auto& state = *_uiBenchmark;
    _session.CancelColorEdit();
    const auto operation = CurrentOperation(state);
    if (operation.expected == Expected::ImportedLayers || operation.expected == Expected::ImportedLayersRefused)
        _session.ReplaceDocument(state.imageImportBaseline, state.activeLayer);
    else
        _session.ReplaceDocument(state.fixture, state.activeLayer);
    _session.filterSettings = {};
    _session.SetForegroundColor(0xff936427);
    OnTool(EditorTool::Move);
    _viewport.Fit(DocumentSize());
    if (operation.expected == Expected::Saved)
    {
        state.saveReceipt.reset();
        state.savedBaselineReady = false;
    }
    if (operation.expected == Expected::Opened)
        state.openReceipt.reset();
    if (operation.expected == Expected::ImportedProject || operation.expected == Expected::ImportedLayers ||
        operation.expected == Expected::ImportedLayersRefused)
        state.importReceipt.reset();
    if (operation.expected == Expected::PngExported || operation.expected == Expected::JpegExported)
        state.exportReceipt.reset();
    if (operation.expected == Expected::GradientApplied)
        state.gradientApplyReceipt.reset();
    _documentPath = operation.expected == Expected::Saved ? state.savePath : std::wstring{};
    if (operation.prepareCommand)
    {
        OnCommand(operation.prepareCommand);
        if (operation.action == AdjustAction::Apply)
        {
            OnAdjustAction(AdjustAction::SetInputGamma, 1.25);
        }
    }
    if (operation.sizeAction)
    {
        // Card-opening commands use the owned continuation source. Advance their metadata-only opening before
        // editing the draft; preparation is still measured only after the real-loop readiness/frame boundary.
        (void)RunUiActions(_jobs);
        if (operation.prepareCommand == ChromeHost::kCommandCanvasSize)
        {
            OnSizeAction(SizeAction::SetWidth, state.fixture.width + 16);
            OnSizeAction(SizeAction::SetHeight, state.fixture.height + 16);
            OnSizeAction(SizeAction::SetAnchor, 8);
            OnSizeAction(SizeAction::SetExtension, 1);
        }
        else if (operation.value != 0)
        {
            OnSizeAction(SizeAction::SetResample, 0);
            OnSizeAction(SizeAction::SetResolution, operation.value);
        }
        else
        {
            OnSizeAction(SizeAction::SetWidth, state.fixture.width / 2);
            OnSizeAction(SizeAction::SetHeight, state.fixture.height / 2);
            OnSizeAction(SizeAction::SetSampling, 0);
        }
    }
    // Fit must begin at a different zoom, so both view cases measure real changes.
    if (operation.command == ChromeHost::kCommandZoomFit)
    {
        OnCommand(ChromeHost::kCommandZoomActual);
    }
    if (operation.expected == Expected::GradientPreview || operation.expected == Expected::GradientApplied)
    {
        // The direct session surface sets only deterministic fixture metadata. The real Application gradient
        // producer creates the deferred edit and its asynchronous preview through Begin/DragGradient below.
        _session.gradientSettings.shape = GradientShape::Linear;
        _session.gradientSettings.style = GradientStyle::ForegroundToBackground;
        _session.gradientSettings.reversed = false;
        _session.gradientSettings.opacity = 1.0;
        _session.SetForegroundColor(0xff936427);
        _session.SetBackgroundColor(0xffffffff);
        OnTool(EditorTool::Gradient);
        // Keep one pixel of clamp range at both ends so inverse view/document rounding cannot perturb the oracle.
        const Vec2 start{1.5, static_cast<double>(state.fixture.height / 2) + 0.5};
        const Vec2 end{static_cast<double>(state.fixture.width) - 1.5, start.y};
        BeginGradientDrag(_viewport.ViewFromDocument(start, DocumentSize()));
        DragGradient(_viewport.ViewFromDocument(end, DocumentSize()), 0);
        EndGradientDrag();
        const auto* pending = _session.PendingGradient();
        if (!pending || !pending->deferred || !pending->HasLine())
        {
            FailUiBenchmark("gradient-fixture-not-ready");
            return;
        }
        state.gradientOpening = pending->opening;
        state.gradientSequence = pending->sequence;
    }
    DocumentChanged();
    state.previousPresents = _renderer.PresentCount();
    state.phase = UiBenchmarkState::Phase::Preparing;
}

void Application::ExecuteUiBenchmarkStep() noexcept
{
    if (!_uiBenchmark || _uiBenchmark->phase != UiBenchmarkState::Phase::Posted)
    {
        return;
    }
    const auto operation = CurrentOperation(*_uiBenchmark);
    if (operation.expected == Expected::GradientPreview)
    {
        auto& state = *_uiBenchmark;
        const double startX = 1.5;
        const double endX = static_cast<double>(state.fixture.width) - 1.5;
        const double y = static_cast<double>(state.fixture.height / 2) + 0.5;
        const Vec2 end{endX, y};
        BeginGradientDrag(_viewport.ViewFromDocument(end, DocumentSize()));
        // Offer a burst while the first preview is still active. RefreshGradientJobs keeps only the newest
        // pending request; no worker wait or synchronous raster operation is performed here.
        for (int step = 1; step <= 8; ++step)
        {
            const double x = startX + (endX - startX) * step / 8.0;
            DragGradient(_viewport.ViewFromDocument(Vec2{x, y}, DocumentSize()), 0);
        }
        EndGradientDrag();
        const auto* pending = _session.PendingGradient();
        if (!pending || pending->opening != state.gradientOpening || !pending->HasLine())
        {
            FailUiBenchmark("gradient-coalescing-admission");
            return;
        }
        state.gradientSequence = pending->sequence;
    }
    else if (operation.expected == Expected::GradientApplied)
    {
        const auto* pending = _session.PendingGradient();
        if (!pending || pending->opening != _uiBenchmark->gradientOpening || !pending->HasLine())
        {
            FailUiBenchmark("gradient-apply-admission");
            return;
        }
        // Drive the same host action as the Gradient header. ReportHostAdmission captures the receipt created by
        // CommitGradient's production HostGradientApplyInput path for terminal-state validation below.
        _uiBenchmark->captureGradientApplyReceipt = true;
        OnGradientAction(GradientAction::Apply, 0);
        _uiBenchmark->captureGradientApplyReceipt = false;
        if (!_uiBenchmark->gradientApplyReceipt)
        {
            FailUiBenchmark("gradient-apply-receipt-missing");
            return;
        }
    }
    else if (operation.expected == Expected::Saved)
    {
        _uiBenchmark->saveReceipt = QueueHostWrite(HostWriteFormat::Project, false);
        ReportHostAdmission(_uiBenchmark->saveReceipt.value());
    }
    else if (operation.expected == Expected::Opened)
    {
        const std::array<std::wstring_view, 1> paths{_uiBenchmark->savePath};
        _uiBenchmark->openReceipt = QueueHostPaths(paths, HostPathAction::OpenPaths);
        ReportHostAdmission(_uiBenchmark->openReceipt.value());
    }
    else if (operation.expected == Expected::PngExported || operation.expected == Expected::JpegExported)
    {
        const auto format = operation.expected == Expected::PngExported ? HostWriteFormat::Png : HostWriteFormat::Jpeg;
        const auto& path = format == HostWriteFormat::Png ? _uiBenchmark->pngExportPath : _uiBenchmark->jpegExportPath;
        HostWriteInput input{format, false, path}; // QueueHostInput takes an owned copy for the continuation.
        _uiBenchmark->exportReceipt = QueueHostInput(std::move(input));
        if (operation.expected == Expected::PngExported)
            _uiBenchmark->pngExportReceipt = _uiBenchmark->exportReceipt;
        ReportHostAdmission(_uiBenchmark->exportReceipt.value());
    }
    else if (operation.expected == Expected::ImportedProject || operation.expected == Expected::ImportedLayers ||
             operation.expected == Expected::ImportedLayersRefused)
    {
        const std::array<std::wstring_view, 1> paths{_uiBenchmark->pngExportPath};
        const auto action = operation.expected == Expected::ImportedProject ? HostPathAction::ImagesAsProject
                                                                            : HostPathAction::ImagesAsLayers;
        _uiBenchmark->importReceipt = QueueHostPaths(paths, action);
        ReportHostAdmission(_uiBenchmark->importReceipt.value());
    }
    else if (operation.sizeAction)
        OnSizeAction(SizeAction::Apply, 0);
    else if (operation.command)
    {
        OnCommand(operation.command);
    }
    else
    {
        OnAdjustAction(operation.action, operation.value);
    }
    _uiBenchmark->phase = UiBenchmarkState::Phase::AwaitingFrame;
    _frameInvalidated = true;
}

void Application::AdvanceUiBenchmark() noexcept
{
    auto& state = *_uiBenchmark;
    if (state.phase == UiBenchmarkState::Phase::Done)
    {
        return;
    }
    if (!_windowVisible || (!_displayPoweredOn && !state.smoke) || _renderer.IsSuspended() || _renderer.IsOccluded())
    {
        FailUiBenchmark("presentation-unavailable");
        return;
    }
    if (_failedFrames != 0 || FAILED(_frameLoopFailure) || FAILED(_renderer.CanvasFailure()))
    {
        FailUiBenchmark("render-failure");
        return;
    }
    // A completion can arrive after an earlier frame already presented. Wait for the frame invalidated by
    // that completion, so asynchronous work cannot pass readiness while its final image/chrome is still unseen.
    if (!_jobs.IsIdle() || _frameInvalidated || _renderer.PresentCount() <= state.previousPresents)
    {
        return;
    }
    if (state.phase == UiBenchmarkState::Phase::Preparing)
    {
        const auto operation = CurrentOperation(state);
        if ((operation.expected == Expected::ImportedProject || operation.expected == Expected::ImportedLayers ||
             operation.expected == Expected::ImportedLayersRefused) &&
            (!state.pngExportReceipt || state.pngExportReceipt->Read().outcome != HostOutcome::Succeeded))
        {
            FailUiBenchmark("png-export-not-ready");
            return;
        }
        if (operation.expected == Expected::GradientPreview || operation.expected == Expected::GradientApplied)
        {
            const auto* pending = _session.PendingGradient();
            if (!pending || pending->opening != state.gradientOpening || pending->sequence != state.gradientSequence ||
                !pending->raster || pending->publishedSequence != state.gradientSequence ||
                pending->publishedGeneration != _session.Generation())
            {
                FailUiBenchmark("gradient-preview-not-ready");
                return;
            }
        }
        state.previousRevision = _session.history.CurrentRevision();
        state.previousSessionSerial = _session.DocumentSerial();
        if (_documentPath.size() >= state.previousDocumentPath.size())
        {
            FailUiBenchmark("document-path-too-long");
            return;
        }
        std::copy(_documentPath.begin(), _documentPath.end(), state.previousDocumentPath.begin());
        state.previousDocumentPathUnits = _documentPath.size();
        state.previousModified = _session.history.IsModified();
        state.previousActiveLayer = _session.ActiveLayerID();
        state.previousUndoCount = _session.history.UndoCount();
        state.previousErrorsShown = _errorsShown;
        state.expectedImageWidth = state.fixture.width;
        state.expectedImageHeight = state.fixture.height;
        if (operation.expected == Expected::ImportedLayers || operation.expected == Expected::ImportedLayersRefused)
        {
            const auto& document = _session.Document();
            const auto* baseline = _session.ActiveLayer();
            if (!document || document->layers.size() != 1 || !baseline || !baseline->asset ||
                baseline->asset->image.Width() != state.expectedImageWidth ||
                baseline->asset->image.Height() != state.expectedImageHeight || state.previousUndoCount != 0 ||
                state.previousActiveLayer != state.activeLayer)
            {
                FailUiBenchmark("image-import-baseline-not-ready");
                return;
            }
        }
        state.previousReadback = _gpuWork.Statistics();
        const auto* layer = _session.ActiveLayer();
        if (!layer || !layer->asset.has_value())
        {
            FailUiBenchmark("missing-active-image");
            return;
        }
        state.previousImage = layer->asset->image;
        const auto& edit = _session.PendingColorEdit();
        state.previousPreview = edit.has_value() ? edit->previewRaster : Raster{};
        state.previousPreviewSequence = edit.has_value() ? edit->previewSequence : 0;
        state.previousZoom = _viewport.zoom;
        state.previousPan = _viewport.pan;
        state.timing = {};
        state.previousPresents = _renderer.PresentCount();
        state.requestedAt = Now();
        state.phase = UiBenchmarkState::Phase::Posted;
        if (state.requestedAt < 0 || !PostMessageW(_window.get(), kUiBenchmarkMessage, 0, 0))
        {
            FailUiBenchmark("post-or-clock");
        }
        return;
    }
    if (state.phase != UiBenchmarkState::Phase::AwaitingFrame)
    {
        return;
    }
    const auto duration = UiDurationMicroseconds(state.requestedAt, Now(), static_cast<std::int64_t>(_jobQpcFrequency));
    const auto expected = CurrentOperation(state).expected;
    const auto& edit = _session.PendingColorEdit();
    const auto* layer = _session.ActiveLayer();
    const bool panelReady = edit.has_value() && !edit->previewFailed &&
                            ((edit->previewLookup && edit->previewSequence > state.previousPreviewSequence) ||
                             (edit->previewRaster.HasStorage() && edit->previewRaster != state.previousPreview));
    const bool histogramReady = expected == Expected::Histogram && edit.has_value() &&
                                edit->kind == FilterKind::Levels && edit->histogramReady &&
                                edit->original == state.previousImage;
    const bool changedPixels = layer && layer->asset.has_value() && layer->asset->image != state.previousImage &&
                               _session.history.CurrentRevision() != state.previousRevision && !edit.has_value();
    const bool viewReady = expected == Expected::View && !edit.has_value() &&
                           (_viewport.zoom != state.previousZoom || _viewport.pan != state.previousPan);
    const bool refused = expected == Expected::Refusal && !edit.has_value() && layer && layer->asset.has_value() &&
                         layer->asset->image == state.previousImage &&
                         _session.history.CurrentRevision() == state.previousRevision;
    const auto operation = CurrentOperation(state);
    const auto& document = _session.Document();
    // Successful writer receipts require the captured backend publication fact. Timing validation never reads or
    // decodes output files synchronously; separate codec and continuation tests validate the written bytes.
    const bool saved = expected == Expected::Saved && state.saveReceipt &&
                       state.saveReceipt->Read().outcome == HostOutcome::Succeeded && _documentPath == state.savePath &&
                       !edit && !_session.history.IsModified() &&
                       _session.history.CurrentRevision() == state.previousRevision && layer && layer->asset &&
                       layer->asset->image == state.previousImage;
    const bool opened =
        expected == Expected::Opened && state.savedBaselineReady && state.saveReceipt &&
        state.saveReceipt->Read().outcome == HostOutcome::Succeeded && state.openReceipt &&
        state.openReceipt->Read().outcome == HostOutcome::Succeeded &&
        _session.DocumentSerial() != state.previousSessionSerial && _documentPath == state.savePath && document &&
        document->width == state.fixture.width && document->height == state.fixture.height &&
        document->layers.size() == state.fixture.layers.size() && _session.ActiveLayerID() == state.activeLayer &&
        !_session.history.IsModified() && !_session.history.CanUndo() && !_session.history.CanRedo() &&
        _session.history.UndoCount() == 0 && _session.history.UndoName().empty();
    const bool exportUnchanged =
        state.exportReceipt && state.exportReceipt->Read().outcome == HostOutcome::Succeeded &&
        std::wstring_view(_documentPath) ==
            std::wstring_view(state.previousDocumentPath.data(), state.previousDocumentPathUnits) &&
        _session.history.CurrentRevision() == state.previousRevision &&
        _session.history.IsModified() == state.previousModified && layer && layer->asset &&
        layer->asset->image == state.previousImage && !edit;
    const bool pngExported = expected == Expected::PngExported && exportUnchanged;
    const bool jpegExported = expected == Expected::JpegExported && exportUnchanged;
    const auto* importedLayer = _session.ActiveLayer();
    const bool importedImage = importedLayer && importedLayer->asset.has_value() &&
                               importedLayer->asset->image.Width() == state.expectedImageWidth &&
                               importedLayer->asset->image.Height() == state.expectedImageHeight &&
                               importedLayer->asset->image != state.previousImage;
    const auto importResult = state.importReceipt ? state.importReceipt->Read() : ContinuationReceipt::Result{};
    const bool importedProject =
        expected == Expected::ImportedProject && state.importReceipt &&
        importResult.outcome == HostOutcome::Succeeded && _session.DocumentSerial() != state.previousSessionSerial &&
        _documentPath.empty() && document && document->width == state.expectedImageWidth &&
        document->height == state.expectedImageHeight && document->layers.size() == 1 &&
        _session.ActiveLayerID().has_value() && _session.ActiveLayerID() != state.previousActiveLayer &&
        importedImage && !_session.history.IsModified() && !_session.history.CanUndo() && !_session.history.CanRedo() &&
        _session.history.UndoCount() == 0 && state.previousUndoCount == 0;
    const bool importedLayers =
        expected == Expected::ImportedLayers && state.importReceipt && importResult.outcome == HostOutcome::Succeeded &&
        _session.DocumentSerial() == state.previousSessionSerial && _documentPath.empty() && document &&
        document->width == state.expectedImageWidth && document->height == state.expectedImageHeight &&
        document->layers.size() == 2 && _session.ActiveLayerID().has_value() &&
        _session.ActiveLayerID() != state.previousActiveLayer && importedImage &&
        _session.history.UndoCount() == state.previousUndoCount + 1 && _session.history.UndoName() == L"Import Image";
    const bool importRefused =
        expected == Expected::ImportedLayersRefused && state.importReceipt &&
        importResult.outcome == HostOutcome::Failed && importResult.error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) &&
        _session.DocumentSerial() == state.previousSessionSerial && _documentPath.empty() && document &&
        document->id == state.imageImportBaseline.id && document->width == state.expectedImageWidth &&
        document->height == state.expectedImageHeight && document->layers.size() == 1 &&
        _session.ActiveLayerID() == state.previousActiveLayer && layer && layer->asset &&
        layer->asset->image == state.previousImage && _session.history.CurrentRevision() == state.previousRevision &&
        _session.history.UndoCount() == state.previousUndoCount && _errorsShown == state.previousErrorsShown + 1 &&
        _lastErrorId == IDS_ERROR_TOO_LARGE;
    const bool sizeReady =
        expected == Expected::Size && document && !SizeCardOpen() &&
        _session.history.CurrentRevision() != state.previousRevision &&
        (operation.prepareCommand == ChromeHost::kCommandCanvasSize
             ? document->width == state.fixture.width + 16 && document->height == state.fixture.height + 16 &&
                   _session.history.UndoName() == L"Canvas Size"
         : operation.value != 0
             ? document->width == state.fixture.width && document->height == state.fixture.height &&
                   document->resolution == operation.value && _session.history.UndoName() == L"Image Size"
             : document->width == state.fixture.width / 2 && document->height == state.fixture.height / 2 && layer &&
                   layer->asset && layer->asset->image != state.previousImage &&
                   _session.history.UndoName() == L"Image Size");
    const auto* pendingGradient = _session.PendingGradient();
    const bool gradientPreviewReady =
        expected == Expected::GradientPreview && pendingGradient && pendingGradient->opening == state.gradientOpening &&
        pendingGradient->sequence == state.gradientSequence && pendingGradient->raster &&
        pendingGradient->publishedSequence == state.gradientSequence &&
        pendingGradient->publishedGeneration == _session.Generation() && layer && layer->asset &&
        GradientPreviewEndpointPixelsMatch(*pendingGradient->raster, state.fixture.width, state.fixture.height / 2) &&
        layer->asset->image == state.previousImage && _session.history.CurrentRevision() == state.previousRevision &&
        _session.history.UndoCount() == state.previousUndoCount;
    const auto gradientResult =
        state.gradientApplyReceipt ? state.gradientApplyReceipt->Read() : ContinuationReceipt::Result{};
    const bool gradientApplied =
        expected == Expected::GradientApplied && state.gradientApplyReceipt &&
        gradientResult.outcome == HostOutcome::Succeeded && pendingGradient == nullptr && layer && layer->asset &&
        layer->asset->image != state.previousImage && _session.history.CurrentRevision() != state.previousRevision &&
        _session.history.UndoCount() == state.previousUndoCount + 1 && _session.history.UndoName() == L"Gradient" &&
        GradientEndpointPixelsMatch(layer->asset->image, state.fixture.height / 2);
    if (!duration.has_value() || state.timing.clockFailures != 0 ||
        !(viewReady || refused || saved || opened || pngExported || jpegExported || importedProject || importedLayers ||
          importRefused || sizeReady || histogramReady || gradientPreviewReady || gradientApplied ||
          (expected == Expected::Panel && panelReady) || (expected == Expected::Pixels && changedPixels)))
    {
        FailUiBenchmark("command-result-or-clock");
        return;
    }
    if (state.trial != 0)
    {
        const auto readback = _gpuWork.Statistics();
        state.samples[state.trial - 1] = {state.timing,
                                          duration.value(),
                                          _renderer.PresentCount() - state.previousPresents,
                                          {readback.copies - state.previousReadback.copies,
                                           readback.maps - state.previousReadback.maps,
                                           readback.readinessBarriers - state.previousReadback.readinessBarriers,
                                           readback.busyFailures - state.previousReadback.busyFailures}};
    }
    if (saved)
        state.savedBaselineReady = true;
    if (++state.trial > state.runs)
    {
        Report(state);
        state.trial = 0;
        if (++state.operation == kOperations.size())
        {
            state.phase = UiBenchmarkState::Phase::Done;
            state.complete = true;
            (void)KillTimer(_window.get(), kUiBenchmarkTimerId);
            std::printf("UI BENCH REPORT status=ok width=%d height=%d operations=%zu\n", state.fixture.width,
                        state.fixture.height, kOperations.size());
            std::fflush(stdout);
            PostQuitMessage(0);
            return;
        }
    }
    PrepareUiBenchmarkStep();
}

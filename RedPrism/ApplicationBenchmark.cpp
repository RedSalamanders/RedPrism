#include "Application.h"

#include "BenchmarkMachine.h"
#include "ChromeHost.h"
#include "Kernels.h"
#include "UiBenchmark.h"
#include "VersionInfo.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <new>
#include <string_view>

namespace
{
enum class Expected
{
    View,
    Histogram,
    Panel,
    Pixels,
    Refusal
};

struct Operation final
{
    const char* name;
    int prepareCommand;
    int command;
    AdjustAction action;
    double value;
    Expected expected;
};

constexpr std::array<Operation, 15> kOperations{{
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
            next->fixture.layers.push_back(std::move(bottom));
        }
        next->fixture.layers.push_back(std::move(top));
        if (!ceiling)
        {
            ImageLayer adjustment;
            adjustment.id = Guid::Generate();
            adjustment.name = L"Benchmark exposure adjustment";
            adjustment.transform.size = size;
            adjustment.adjustment = LayerAdjustment{};
            adjustment.adjustment->kind = AdjustmentKind::Exposure;
            adjustment.adjustment->exposureSettings = ExposureSettings{0.25, 0, 1};
            next->fixture.layers.push_back(std::move(adjustment));
        }
        if (ValidateDocument(next->fixture) != DocumentStatus::Ok)
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
    for (unsigned int run = 0; run < state.runs; ++run)
    {
        elapsed[run] = state.samples[run].elapsedUs;
        frames += state.samples[run].frames;
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
    std::printf("%s\n", std::format("UI BENCH name={} width={} height={} runs={} warmup=1 elapsedMedianUs={:.1f} "
                                    "elapsedP95Us={} elapsedMaxUs={} "
                                    "dispatchP99UpperUs={} dispatchMaxUs={} messageP99UpperUs={} messageMaxUs={} "
                                    "drainP99UpperUs={} drainMaxUs={} "
                                    "applyMaxUs={} actionMaxUs={} frameP99UpperUs={} frameMaxUs={} frames={}",
                                    CurrentOperation(state).name, state.fixture.width, state.fixture.height, state.runs,
                                    median, elapsed[(state.runs * 95 + 99) / 100 - 1], elapsed[state.runs - 1],
                                    dispatch.p99UpperUs, dispatch.maximumUs, message.p99UpperUs, message.maximumUs,
                                    drain.p99UpperUs, drain.maximumUs, apply.maximumUs, action.maximumUs,
                                    frame.p99UpperUs, frame.maximumUs, frames)
                            .c_str());
    std::fflush(stdout);
}
} // namespace

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
    _session.ReplaceDocument(state.fixture, state.activeLayer);
    _session.filterSettings = {};
    _session.SetForegroundColor(0xff936427);
    OnTool(EditorTool::Move);
    _viewport.Fit(DocumentSize());
    const auto operation = CurrentOperation(state);
    if (operation.prepareCommand)
    {
        OnCommand(operation.prepareCommand);
        if (operation.action == AdjustAction::Apply)
        {
            OnAdjustAction(AdjustAction::SetInputGamma, 1.25);
        }
    }
    // Fit must begin at a different zoom, so both view cases measure real changes.
    if (operation.command == ChromeHost::kCommandZoomFit)
    {
        OnCommand(ChromeHost::kCommandZoomActual);
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
    if (operation.command)
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
    if (!_jobs.IsIdle() || _renderer.PresentCount() <= state.previousPresents)
    {
        return;
    }
    if (state.phase == UiBenchmarkState::Phase::Preparing)
    {
        state.previousRevision = _session.history.CurrentRevision();
        const auto* layer = _session.ActiveLayer();
        if (!layer || !layer->asset.has_value())
        {
            FailUiBenchmark("missing-active-image");
            return;
        }
        state.previousImage = layer->asset->image;
        const auto& edit = _session.PendingColorEdit();
        state.previousPreview = edit.has_value() ? edit->previewRaster : Raster{};
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
    const bool panelReady = edit.has_value() && !edit->previewFailed && edit->previewRaster.HasStorage() &&
                            edit->previewRaster != state.previousPreview;
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
    if (!duration.has_value() || state.timing.clockFailures != 0 ||
        !(viewReady || refused || histogramReady || (expected == Expected::Panel && panelReady) ||
          (expected == Expected::Pixels && changedPixels)))
    {
        FailUiBenchmark("command-result-or-clock");
        return;
    }
    if (state.trial != 0)
    {
        state.samples[state.trial - 1] = {state.timing, duration.value(),
                                          _renderer.PresentCount() - state.previousPresents};
    }
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

#include "DocumentReadbackTests.h"

#include "../Support/DocumentFixture.h"

#include "Adjust.h"
#include "CanvasRenderer.h"
#include "DocumentReadbackJob.h"
#include "Jobs.h"
#include "PixelBuffer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <d3d11_1.h>
#include <memory>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
constexpr UINT kReadbackOwnerMessage = WM_APP + 0x4b1;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s document-readback.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!condition)
        ++gFailures;
}

HRESULT MakeWarp(wil::com_ptr_nothrow<ID3D11Device>& device,
                 wil::com_ptr_nothrow<ID3D11DeviceContext>& context) noexcept
{
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL used = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, device.put(), &used, context.put());
}

[[nodiscard]] std::array<std::uint8_t, 4> PatternPixel(unsigned int x, unsigned int y) noexcept
{
    return {static_cast<std::uint8_t>((x * 17u + y * 3u + 19u) & 0xffu),
            static_cast<std::uint8_t>((x * 5u + y * 29u + 47u) & 0xffu),
            static_cast<std::uint8_t>((x * 11u + y * 7u + 83u) & 0xffu), 255};
}

[[nodiscard]] Raster PatternRaster(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto pixel = PatternPixel(static_cast<unsigned int>(x), static_cast<unsigned int>(y));
            std::memcpy(
                pixels.data() +
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u,
                pixel.data(), pixel.size());
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] CanvasDocument PatternDocument(int width, int height)
{
    CanvasDocument document{};
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    ImageLayer layer =
        ImageLayer::Blank(L"Readback pattern", Vec2{static_cast<double>(width), static_cast<double>(height)});
    const Raster image = PatternRaster(width, height);
    layer.asset = RasterAsset{image, image, layer.name};
    layer.transform.sampling = LayerSampling::Nearest;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(std::move(layer)); });
    return document;
}

[[nodiscard]] std::vector<std::uint8_t> FormulaRegion(const D3D11_BOX& region)
{
    const unsigned int width = region.right - region.left;
    const unsigned int height = region.bottom - region.top;
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(width) * height * 4u);
    for (unsigned int y = 0; y < height; ++y)
    {
        for (unsigned int x = 0; x < width; ++x)
        {
            const auto pixel = PatternPixel(region.left + x, region.top + y);
            std::memcpy(expected.data() + (static_cast<std::size_t>(y) * width + x) * 4u, pixel.data(), pixel.size());
        }
    }
    return expected;
}

struct Fixture final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    CanvasRenderer composition;
    JobScheduler scheduler;
    GpuWorkQueue queue;
    bool schedulerReady = false;
    bool queueReady = false;
    bool usable = true;

    [[nodiscard]] bool Initialize() noexcept
    {
        const HRESULT deviceResult = MakeWarp(device, context);
        Expect(SUCCEEDED(deviceResult), "setup.warp-device");
        if (FAILED(deviceResult) || !device || !context)
            return false;

        const HRESULT compositionResult = composition.CreateDeviceResources(device.get(), context.get());
        Expect(SUCCEEDED(compositionResult), "setup.composition-resources");
        if (FAILED(compositionResult))
            return false;

        const HRESULT schedulerResult = scheduler.Initialize(1);
        schedulerReady = schedulerResult == S_OK;
        Expect(schedulerReady, "setup.scheduler");
        if (!schedulerReady)
            return false;

        const HRESULT queueResult = queue.Initialize(scheduler);
        queueReady = SUCCEEDED(queueResult);
        Expect(queueReady, "setup.gpu-work-queue");
        if (!queueReady)
            return false;

        MSG message{};
        (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        return true;
    }

    void Close() noexcept
    {
        if (schedulerReady)
            queue.StopNotifications();
        if (schedulerReady)
            scheduler.Close();
        queue.CloseAfterWorkers();
        queueReady = false;
        schedulerReady = false;
    }

    ~Fixture()
    {
        Close();
    }
};

struct CaptureState final
{
    Fixture& fixture;
    const CanvasDocument& displayDocument;
    JobTicket ticket{};
    std::uint64_t expectedReservation = 0;
    bool failPrepare = false;
    bool cancelFromOwnerMessage = false;
    bool releaseDisplayWhilePending = false;
    bool generationRebind = false;
    unsigned int requestedRebinds = 0;
    unsigned int prepareCalls = 0;
    unsigned int queueBinds = 0;
    unsigned int recoveryPrepareCalls = 0;
    unsigned int completionCalls = 0;
    bool prepareSucceeded = false;
    bool reservationWasHeld = false;
    bool ownerMessageSeen = false;
    bool ownerMessageSawPending = false;
    bool cancelAccepted = false;
    bool rebindPendingObserved = true;
    bool rebindSetupFailed = false;
    HRESULT rebindSetupResult = S_OK;
    unsigned int rebindMessages = 0;
    std::uint64_t initialDeviceGeneration = 0;
    std::uint64_t reboundDeviceGeneration = 0;
    bool reservationHeldOnEveryPrepare = true;
    bool displayComposeSucceeded = false;
    bool displayReleased = false;
    std::uint64_t displayCompositeCount = 0;
    std::uint64_t displaySurfaceBytes = 0;
    JobResult result{};
    Raster image;
    std::uint64_t deviceGeneration = 0;
    void (*continuationOwnerAction)(CaptureState&) noexcept = nullptr;
    void* continuationContext = nullptr;
};

class ReadbackProbe final : public DocumentReadbackJob
{
  public:
    ReadbackProbe(Fixture& fixture, CaptureState& state, CanvasDocument document, D3D11_BOX region) noexcept
        : DocumentReadbackJob(fixture.scheduler, fixture.queue, fixture.composition, std::move(document), region),
          _fixture(fixture), _state(state)
    {
    }

  protected:
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept final
    {
        ++_state.prepareCalls;
        if (_state.failPrepare)
            return E_ACCESSDENIED;
        if (_fixture.composition.Device() != _fixture.device.get())
            return E_UNEXPECTED;

        const bool reservationHeld = _fixture.scheduler.ReservedMemoryBytes() == _state.expectedReservation;
        _state.reservationWasHeld = reservationHeld;
        _state.reservationHeldOnEveryPrepare = _state.reservationHeldOnEveryPrepare && reservationHeld;
        if (reason == E_ABORT)
        {
            ++_state.recoveryPrepareCalls;
            if (!_state.generationRebind || _state.reboundDeviceGeneration == 0 ||
                _fixture.queue.DeviceGeneration() != _state.reboundDeviceGeneration)
                return E_UNEXPECTED;
            _state.prepareSucceeded = true;
            return S_OK;
        }
        if (reason != S_OK)
            return E_UNEXPECTED;

        if (_state.prepareCalls == 1)
        {
            const HRESULT bindResult = _fixture.queue.Bind(_fixture.device.get(), _fixture.context.get());
            if (FAILED(bindResult))
                return bindResult;
            ++_state.queueBinds;
            _state.reboundDeviceGeneration = _fixture.queue.DeviceGeneration();
        }
        else if (!_state.generationRebind || _state.queueBinds != 1 ||
                 _fixture.queue.DeviceGeneration() != _state.reboundDeviceGeneration)
            return E_UNEXPECTED;

        _state.prepareSucceeded = true;
        const bool postOwnerMessage = !_state.generationRebind || _state.rebindMessages < _state.requestedRebinds;
        if (postOwnerMessage &&
            !PostThreadMessageW(GetCurrentThreadId(), kReadbackOwnerMessage, 0, reinterpret_cast<LPARAM>(&_state)))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }

    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept final
    {
        ++_state.completionCalls;
        _state.result = result;
        _state.image = std::move(image);
        _state.deviceGeneration = deviceGeneration;
    }

  private:
    Fixture& _fixture;
    CaptureState& _state;
};

[[nodiscard]] bool ReplaceReadbackDevice(CaptureState& state) noexcept
{
    wil::com_ptr_nothrow<ID3D11Device> replacementDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> replacementContext;
    HRESULT result = MakeWarp(replacementDevice, replacementContext);
    if (SUCCEEDED(result))
        result = state.fixture.composition.CreateDeviceResources(replacementDevice.get(), replacementContext.get());
    if (SUCCEEDED(result))
        result = state.fixture.queue.Bind(replacementDevice.get(), replacementContext.get());
    if (FAILED(result))
    {
        state.rebindSetupFailed = true;
        state.rebindSetupResult = result;
        state.fixture.usable = false;
        (void)state.fixture.scheduler.Cancel(state.ticket);
        return false;
    }
    state.fixture.device = std::move(replacementDevice);
    state.fixture.context = std::move(replacementContext);
    state.reboundDeviceGeneration = state.fixture.queue.DeviceGeneration();
    ++state.rebindMessages;
    return true;
}

void DispatchReadbackMessage(void* context, void* nativeMessage) noexcept
{
    auto& state = *static_cast<CaptureState*>(context);
    auto& message = *static_cast<MSG*>(nativeMessage);
    if (message.message != kReadbackOwnerMessage || message.lParam != reinterpret_cast<LPARAM>(&state))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        return;
    }

    state.ownerMessageSeen = true;
    state.ownerMessageSawPending = state.fixture.queue.HasPending();
    if (state.continuationOwnerAction)
    {
        if (state.generationRebind && state.rebindMessages < state.requestedRebinds)
        {
            if (!ReplaceReadbackDevice(state))
                return;
        }
        state.continuationOwnerAction(state);
    }
    else if (state.cancelFromOwnerMessage)
        state.cancelAccepted = state.fixture.scheduler.Cancel(state.ticket);
    else if (state.generationRebind)
    {
        state.rebindPendingObserved = state.rebindPendingObserved && state.ownerMessageSawPending;
        if (!ReplaceReadbackDevice(state))
            return;
    }
    else
    {
        state.displayComposeSucceeded = SUCCEEDED(state.fixture.composition.Compose(state.displayDocument));
        state.displayCompositeCount = state.fixture.composition.CompositeCount();
        state.displaySurfaceBytes = state.fixture.composition.CanvasSurfaceBytes();
        if (state.releaseDisplayWhilePending)
        {
            state.fixture.composition.ReleaseDocumentSurfaces();
            state.displayReleased = state.fixture.composition.CanvasSurfaceBytes() == 0;
        }
    }
}

struct ContinuationState final
{
    static constexpr std::array<std::array<unsigned int, 2>, 3> samples{
        std::array<unsigned int, 2>{0, 0}, std::array<unsigned int, 2>{6, 4}, std::array<unsigned int, 2>{2, 3}};

    Fixture& fixture;
    CaptureState& capture;
    bool cancelWhileSuspended = false;
    bool earlyRefused = false;
    bool invalidTicketRefused = false;
    bool staleTicketRefused = false;
    bool outOfBoundsRefused = false;
    bool duplicateRefused = false;
    bool resumeAccepted = true;
    bool duplicateWasRefused = true;
    bool boundaryClean = true;
    bool reservationHeldAtBoundary = true;
    bool cancelAccepted = false;
    bool copiedOffOwner = true;
    bool checkBoundedRetirement = false;
    bool boundedRetirement = true;
    bool ticketStable = true;
    bool ticketCaptured = false;
    bool replacementAfterFirstPixel = false;
    JobTicket continuationTicket{};
    const unsigned long ownerThread = GetCurrentThreadId();
    unsigned int detachedCalls = 0;
    unsigned int resetCalls = 0;
    unsigned int capturedPixels = 0;
    std::array<std::array<std::uint8_t, 4>, samples.size()> actualPixels{};

    [[nodiscard]] bool AtDetachedBoundary() noexcept
    {
        ++detachedCalls;
        boundaryClean = boundaryClean && fixture.queue.LentCount() == 0 && fixture.queue.InFlightCount() == 0 &&
                        !fixture.queue.HasPending() && fixture.composition.UtilitySurfaceBytes() == 0;
        reservationHeldAtBoundary =
            reservationHeldAtBoundary && fixture.scheduler.ReservedMemoryBytes() == capture.expectedReservation;
        if (checkBoundedRetirement)
            boundedRetirement = boundedRetirement && resetCalls == detachedCalls - 1;
        return boundaryClean && reservationHeldAtBoundary;
    }
};

class PixelContinuationProbe final : public DocumentReadbackJob
{
  public:
    PixelContinuationProbe(Fixture& fixture, CaptureState& capture, ContinuationState& continuation,
                           CanvasDocument document) noexcept
        : DocumentReadbackJob(fixture.scheduler, fixture.queue, fixture.composition, std::move(document),
                              D3D11_BOX{0, 0, 0, 1, 1, 1}),
          _fixture(fixture), _capture(capture), _continuation(continuation)
    {
    }

    [[nodiscard]] bool Continue(JobTicket ticket, unsigned int x, unsigned int y) noexcept
    {
        return ContinuePixelReadback(ticket, x, y);
    }

  protected:
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept final
    {
        const bool reservationHeld = _fixture.scheduler.ReservedMemoryBytes() == _capture.expectedReservation;
        _capture.reservationWasHeld = reservationHeld;
        _capture.reservationHeldOnEveryPrepare = _capture.reservationHeldOnEveryPrepare && reservationHeld;
        if (reason != S_OK || _fixture.composition.Device() != _fixture.device.get() || !reservationHeld)
            return E_UNEXPECTED;
        if (_capture.prepareCalls++ == 0)
        {
            const HRESULT bind = _fixture.queue.Bind(_fixture.device.get(), _fixture.context.get());
            if (FAILED(bind))
                return bind;
            ++_capture.queueBinds;
            _capture.prepareSucceeded = true;
            _continuation.earlyRefused = !Continue(_capture.ticket, 1, 0);
            if (!_capture.generationRebind && !PostThreadMessageW(GetCurrentThreadId(), kReadbackOwnerMessage, 0,
                                                                  reinterpret_cast<LPARAM>(&_capture)))
                return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    [[nodiscard]] JobResult ProcessReadback(Raster& image, JobContext&) final
    {
        _continuation.copiedOffOwner =
            _continuation.copiedOffOwner && GetCurrentThreadId() != _continuation.ownerThread;
        if (!image.Data() || image.Width() != 1 || image.Height() != 1 ||
            _continuation.capturedPixels >= _continuation.actualPixels.size())
            return {JobStatus::Failed, E_UNEXPECTED};
        std::memcpy(_continuation.actualPixels[_continuation.capturedPixels].data(), image.Data(), 4);
        ++_continuation.capturedPixels;
        return {};
    }

    [[nodiscard]] bool ProcessReadbackDetached() const noexcept final
    {
        return true;
    }

    void ResetReadback() noexcept final
    {
        _continuation.copiedOffOwner =
            _continuation.copiedOffOwner && GetCurrentThreadId() != _continuation.ownerThread;
        ++_continuation.resetCalls;
    }

    [[nodiscard]] bool DeferDetachedCompletion(JobResult result, JobTicket ticket) noexcept final
    {
        if (result.status != JobStatus::Finished || ticket != _capture.ticket || !_continuation.AtDetachedBoundary())
            return false;
        if (_continuation.cancelWhileSuspended && _continuation.detachedCalls == 1)
        {
            _capture.continuationContext = this;
            _capture.continuationOwnerAction = &CancelSuspendedContinuation;
            return PostThreadMessageW(GetCurrentThreadId(), kReadbackOwnerMessage, 0,
                                      reinterpret_cast<LPARAM>(&_capture)) != FALSE;
        }
        if (_continuation.detachedCalls == 1)
        {
            JobTicket wrongSlot = ticket;
            ++wrongSlot.slot;
            JobTicket stale = ticket;
            ++stale.generation;
            _continuation.invalidTicketRefused = !Continue(wrongSlot, 6, 4);
            _continuation.staleTicketRefused = !Continue(stale, 6, 4);
            _continuation.outOfBoundsRefused = !Continue(ticket, 7, 0);
        }
        const unsigned int sampleIndex = _continuation.detachedCalls;
        if (sampleIndex >= ContinuationState::samples.size())
            return false;
        _capture.continuationContext = this;
        _capture.continuationOwnerAction = &ResumePixelContinuation;
        return PostThreadMessageW(GetCurrentThreadId(), kReadbackOwnerMessage, 0,
                                  reinterpret_cast<LPARAM>(&_capture)) != FALSE;
    }

    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept final
    {
        _capture.continuationContext = nullptr;
        _capture.continuationOwnerAction = nullptr;
        ++_capture.completionCalls;
        _capture.result = result;
        _capture.image = std::move(image);
        _capture.deviceGeneration = deviceGeneration;
    }

  private:
    static void ResumePixelContinuation(CaptureState& capture) noexcept
    {
        auto& self = *static_cast<PixelContinuationProbe*>(capture.continuationContext);
        const unsigned int sampleIndex = self._continuation.detachedCalls;
        if (sampleIndex >= ContinuationState::samples.size())
        {
            self._continuation.resumeAccepted = false;
            return;
        }
        const auto [x, y] = ContinuationState::samples[sampleIndex];
        if (capture.generationRebind && sampleIndex == 1)
            self._continuation.replacementAfterFirstPixel =
                self._continuation.capturedPixels == 1 && capture.rebindMessages == 1 &&
                self._continuation.boundaryClean && !self._fixture.queue.HasPending();
        if (!self._continuation.ticketCaptured)
        {
            self._continuation.continuationTicket = capture.ticket;
            self._continuation.ticketCaptured = true;
        }
        else
            self._continuation.ticketStable =
                self._continuation.ticketStable && self._continuation.continuationTicket == capture.ticket;
        self._continuation.resumeAccepted = self.Continue(capture.ticket, x, y);
        if (self._continuation.resumeAccepted && sampleIndex == 1)
            self._continuation.duplicateWasRefused = !self.Continue(capture.ticket, 2, 3);
        self._continuation.duplicateRefused = self._continuation.duplicateWasRefused;
    }

    static void CancelSuspendedContinuation(CaptureState& capture) noexcept
    {
        auto& self = *static_cast<PixelContinuationProbe*>(capture.continuationContext);
        self._continuation.cancelAccepted = self._fixture.scheduler.Cancel(capture.ticket);
        self._continuation.resumeAccepted = self.Continue(capture.ticket, 6, 4);
    }

    Fixture& _fixture;
    CaptureState& _capture;
    ContinuationState& _continuation;
};

[[nodiscard]] bool RasterMatches(const Raster& image, int width, int height,
                                 const std::vector<std::uint8_t>& expected) noexcept
{
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    return image.HasStorage() && image.Width() == width && image.Height() == height &&
           image.Format() == RasterFormat::Bgra8888 && image.Data() && expected.size() == bytes &&
           std::memcmp(image.Data(), expected.data(), bytes) == 0;
}

void ExpectNoLeaks(Fixture& fixture, const char* prefix) noexcept
{
    const bool clean = fixture.queue.LentCount() == 0 && fixture.queue.InFlightCount() == 0 &&
                       !fixture.queue.HasPending() && fixture.scheduler.ReservedMemoryBytes() == 0 &&
                       fixture.composition.UtilitySurfaceBytes() == 0;
    Expect(clean, prefix);
}

void RunPatternCase(Fixture& fixture, const char* name, D3D11_BOX region, int textureLimit,
                    bool releaseDisplayWhilePending)
{
    CanvasDocument document = PatternDocument(137, 93);
    CanvasDocument displayDocument = PatternDocument(8, 6);
    fixture.composition.SetTextureLimitForTest(textureLimit);
    fixture.composition.InvalidateDocument();
    const HRESULT displayResult = fixture.composition.Compose(displayDocument);
    Expect(SUCCEEDED(displayResult), "pattern.display-baseline-ready");
    if (FAILED(displayResult))
        return;

    CaptureState state{fixture, displayDocument};
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    state.releaseDisplayWhilePending = releaseDisplayWhilePending;
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket);
    Expect(submit == S_OK, name);
    if (submit != S_OK)
        return;
    state.ticket = ticket;
    EditDocumentFixture(document, [&](DocumentFixture& fixture)
                        { fixture.layers.clear(); }); // the admitted job must retain the immutable snapshot

    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "pattern.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    Expect(state.completionCalls == 1 && state.result.status == JobStatus::Finished && state.image.HasStorage(),
           "pattern.single-complete-publication");
    Expect(state.prepareCalls == 1 && state.queueBinds == 1 && state.prepareSucceeded,
           "pattern.prepare-binds-once-for-all-tiles");
    Expect(state.reservationWasHeld && state.expectedReservation != 0,
           "pattern.required-memory-reservation-held-through-prepare");
    Expect(state.ownerMessageSeen && state.ownerMessageSawPending,
           "pattern.owner-message-during-pending-utility-readback");

    if (state.result.status == JobStatus::Finished && state.image.HasStorage())
    {
        const int outputWidth = static_cast<int>(region.right - region.left);
        const int outputHeight = static_cast<int>(region.bottom - region.top);
        const auto expected = FormulaRegion(region);
        const bool matches = RasterMatches(state.image, outputWidth, outputHeight, expected);
        if (!matches)
        {
            std::printf("     %s result=%dx%d expected=%dx%d\n", name, state.image.Width(), state.image.Height(),
                        outputWidth, outputHeight);
            if (state.image.Data())
            {
                const std::size_t count = (std::min)(expected.size(), static_cast<std::size_t>(32));
                std::printf("     actual:");
                for (std::size_t i = 0; i < count; ++i)
                    std::printf(" %u", static_cast<unsigned int>(state.image.Data()[i]));
                std::printf("\n     expected:");
                for (std::size_t i = 0; i < count; ++i)
                    std::printf(" %u", static_cast<unsigned int>(expected[i]));
                std::printf("\n");
            }
        }
        Expect(matches, name);
    }

    if (releaseDisplayWhilePending)
        Expect(state.displayReleased && fixture.composition.CompositeCount() == state.displayCompositeCount,
               "pattern.hidden-release-drops-display-surfaces-during-utility-owner");
    else
    {
        std::vector<std::uint8_t> displayPixels;
        UINT displayWidth = 0;
        UINT displayHeight = 0;
        const HRESULT displayReadback = fixture.composition.Readback(displayPixels, displayWidth, displayHeight);
        const auto expectedDisplay = FormulaRegion(D3D11_BOX{0, 0, 0, 8, 6, 1});
        const bool displayMatches =
            SUCCEEDED(displayReadback) && displayWidth == 8 && displayHeight == 6 && displayPixels == expectedDisplay;
        Expect(state.displayComposeSucceeded && displayMatches,
               "pattern.utility-readback-keeps-settled-display-pixels");
        Expect(fixture.composition.CompositeCount() == state.displayCompositeCount &&
                   fixture.composition.CanvasSurfaceBytes() == state.displaySurfaceBytes,
               "pattern.utility-readback-preserves-settled-display-resources-and-count");
    }
    ExpectNoLeaks(fixture, "pattern.queue-reservation-and-utility-resources-released");
}

[[nodiscard]] CanvasDocument ComplexDocument()
{
    DocumentFixture fixture = DocumentFixture::Copy(PatternDocument(12, 9));
    ImageLayer folder = ImageLayer::Blank(L"Masked group", Vec2{12, 9});
    folder.isGroup = true;
    const std::array<std::uint8_t, 4> folderMaskPixels{255, 128, 64, 220};
    const Raster folderMask = Raster::Make(2, 2, RasterFormat::Gray8, folderMaskPixels.data(), folderMaskPixels.size());
    folder.mask =
        LayerMask{RasterAsset{folderMask, Raster::MakeThumbnail(folderMask), L"Folder mask"}, true, std::nullopt, true};
    fixture.layers.insert(fixture.layers.begin(), folder);
    ImageLayer& base = fixture.layers[1];
    base.parentID = folder.id;
    base.maskSourceID = std::nullopt;
    const std::array<std::uint8_t, 4> layerMaskPixels{255, 96, 192, 32};
    const Raster layerMask = Raster::Make(2, 2, RasterFormat::Gray8, layerMaskPixels.data(), layerMaskPixels.size());
    base.mask =
        LayerMask{RasterAsset{layerMask, Raster::MakeThumbnail(layerMask), L"Layer mask"}, true, std::nullopt, true};
    ImageLayer clipped = ImageLayer::Blank(L"Clipped nearest pixels", Vec2{12, 9});
    const Raster overlay = PatternRaster(12, 9);
    clipped.asset = RasterAsset{overlay, overlay, clipped.name};
    clipped.transform.sampling = LayerSampling::Nearest;
    clipped.parentID = folder.id;
    clipped.maskSourceID = base.id;
    clipped.opacity = 0.65;
    fixture.layers.push_back(clipped);
    ImageLayer adjustment = ImageLayer::Blank(L"Levels adjustment", Vec2{12, 9});
    LayerAdjustment levels{};
    levels.kind = AdjustmentKind::Levels;
    Adjust::SetLevelsCurrent(levels.levels, LevelRange{8, 1.35, 245, 4, 250});
    adjustment.adjustment = levels;
    adjustment.parentID = folder.id;
    fixture.layers.push_back(std::move(adjustment));
    return std::move(fixture).Seal();
}

void TestComplexSynchronousReference(Fixture& fixture)
{
    fixture.composition.SetTextureLimitForTest(16);
    CanvasDocument document = ComplexDocument();
    Raster synchronous;
    const HRESULT referenceResult = fixture.composition.ComposeReadback(document, synchronous);
    Expect(SUCCEEDED(referenceResult) && synchronous.HasStorage(), "complex.synchronous-committed-reference");
    if (FAILED(referenceResult) || !synchronous.HasStorage())
        return;

    CanvasDocument displayDocument = PatternDocument(7, 5);
    fixture.composition.InvalidateDocument();
    const HRESULT displayResult = fixture.composition.Compose(displayDocument);
    Expect(SUCCEEDED(displayResult), "complex.display-baseline-ready");
    if (FAILED(displayResult))
        return;

    const D3D11_BOX region{0, 0, 0, 12, 9, 1};
    CaptureState state{fixture, displayDocument};
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket);
    Expect(submit == S_OK, "complex.submit");
    if (submit != S_OK)
        return;
    state.ticket = ticket;
    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "complex.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    const bool same = state.completionCalls == 1 && state.result.status == JobStatus::Finished &&
                      state.image.Width() == synchronous.Width() && state.image.Height() == synchronous.Height() &&
                      state.image.Data() && synchronous.Data() &&
                      std::memcmp(state.image.Data(), synchronous.Data(),
                                  static_cast<std::size_t>(synchronous.Width()) * synchronous.Height() * 4u) == 0;
    if (!same)
        std::printf("     complex async=%dx%d sync=%dx%d status=%u error=0x%08x\n", state.image.Width(),
                    state.image.Height(), synchronous.Width(), synchronous.Height(),
                    static_cast<unsigned int>(state.result.status), static_cast<unsigned int>(state.result.error));
    Expect(same, "complex.mask-clipping-folder-adjustment-match-committed-sync-readback");
    Expect(state.prepareCalls == 1 && state.queueBinds == 1, "complex.prepare-once-not-per-tile");
    Expect(fixture.composition.CompositeCount() == state.displayCompositeCount &&
               fixture.composition.CanvasSurfaceBytes() == state.displaySurfaceBytes,
           "complex.utility-readback-preserves-settled-display");
    ExpectNoLeaks(fixture, "complex.queue-reservation-and-utility-resources-released");
}

void TestOwnerCancellation(Fixture& fixture)
{
    CanvasDocument document = PatternDocument(24, 17);
    CanvasDocument displayDocument = PatternDocument(5, 4);
    fixture.composition.SetTextureLimitForTest(64);
    const D3D11_BOX region{0, 0, 0, 24, 17, 1};
    CaptureState state{fixture, displayDocument};
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    state.cancelFromOwnerMessage = true;
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket);
    Expect(submit == S_OK, "cancel.submit");
    if (submit != S_OK)
        return;
    state.ticket = ticket;
    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "cancel.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    Expect(state.ownerMessageSawPending && state.cancelAccepted && state.completionCalls == 1 &&
               state.result.status == JobStatus::Cancelled && !state.image.HasStorage(),
           "cancel.owner-message-cancels-pending-readback-without-publishing");
    ExpectNoLeaks(fixture, "cancel.queue-reservation-and-utility-resources-released");
}

void TestPixelContinuation(Fixture& fixture)
{
    constexpr D3D11_BOX initialRegion{0, 0, 0, 1, 1, 1};
    CanvasDocument document = PatternDocument(7, 5);
    CanvasDocument displayDocument = PatternDocument(4, 3);
    CaptureState capture{fixture, displayDocument};
    capture.expectedReservation = DocumentReadbackJob::RequiredMemory(initialRegion);
    ContinuationState continuation{fixture, capture};
    std::unique_ptr<Job> job = std::make_unique<PixelContinuationProbe>(fixture, capture, continuation, document);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, capture.expectedReservation}, ticket);
    Expect(submit == S_OK, "continue.submit-one-pixel-reservation");
    if (submit != S_OK)
        return;
    capture.ticket = ticket;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.clear(); });

    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &capture);
    Expect(settled == SettleResult::Settled, "continue.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }

    bool samplesMatch = continuation.capturedPixels == ContinuationState::samples.size();
    for (std::size_t i = 0; i < ContinuationState::samples.size(); ++i)
    {
        const auto [x, y] = ContinuationState::samples[i];
        samplesMatch = samplesMatch && continuation.actualPixels[i] == PatternPixel(x, y);
    }
    const auto [lastX, lastY] = ContinuationState::samples.back();
    const auto lastPixel = PatternPixel(lastX, lastY);
    Expect(samplesMatch && continuation.copiedOffOwner,
           "continue.each-one-pixel-result-matches-independent-pattern-formula-off-owner");
    Expect(continuation.earlyRefused && continuation.invalidTicketRefused && continuation.staleTicketRefused &&
               continuation.outOfBoundsRefused && continuation.duplicateRefused && continuation.resumeAccepted,
           "continue.early-wrong-stale-out-of-bounds-and-duplicate-refused-before-valid-resume");
    Expect(capture.prepareCalls == 3 && capture.queueBinds == 1 && capture.reservationHeldOnEveryPrepare &&
               continuation.detachedCalls == 3 && continuation.boundaryClean && continuation.reservationHeldAtBoundary,
           "continue.same-record-reservation-and-clean-detached-boundary-through-three-samples");
    Expect(capture.ownerMessageSeen && capture.completionCalls == 1 && capture.result.status == JobStatus::Finished &&
               RasterMatches(capture.image, 1, 1, std::vector<std::uint8_t>(lastPixel.begin(), lastPixel.end())),
           "continue.owner-message-resumes-and-publishes-one-final-four-byte-pixel");
    ExpectNoLeaks(fixture, "continue.reservation-mapping-and-utility-released");
}

void TestPixelContinuationDeviceReplacement(Fixture& fixture)
{
    constexpr D3D11_BOX initialRegion{0, 0, 0, 1, 1, 1};
    CanvasDocument document = PatternDocument(7, 5);
    CanvasDocument displayDocument = PatternDocument(4, 3);
    CaptureState capture{fixture, displayDocument};
    capture.expectedReservation = DocumentReadbackJob::RequiredMemory(initialRegion);
    capture.generationRebind = true;
    capture.requestedRebinds = 1;
    capture.initialDeviceGeneration = fixture.queue.DeviceGeneration();
    ContinuationState continuation{fixture, capture};
    continuation.checkBoundedRetirement = true;
    std::unique_ptr<Job> job = std::make_unique<PixelContinuationProbe>(fixture, capture, continuation, document);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, capture.expectedReservation}, ticket);
    Expect(submit == S_OK, "continue-rebind.submit-one-pixel-reservation");
    if (submit != S_OK)
        return;
    capture.ticket = ticket;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.clear(); });

    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &capture);
    Expect(settled == SettleResult::Settled, "continue-rebind.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    if (capture.rebindSetupFailed)
    {
        std::printf("     continue-rebind device setup HRESULT=0x%08x\n",
                    static_cast<unsigned int>(capture.rebindSetupResult));
        Expect(false, "continue-rebind.replacement-device-setup");
        fixture.usable = false;
        return;
    }

    bool samplesMatch = continuation.capturedPixels == ContinuationState::samples.size();
    for (std::size_t i = 0; i < ContinuationState::samples.size(); ++i)
    {
        const auto [x, y] = ContinuationState::samples[i];
        samplesMatch = samplesMatch && continuation.actualPixels[i] == PatternPixel(x, y);
    }
    const auto [lastX, lastY] = ContinuationState::samples.back();
    const auto lastPixel = PatternPixel(lastX, lastY);
    Expect(samplesMatch && continuation.copiedOffOwner,
           "continue-rebind.exact-pixels-from-original-snapshot-after-caller-edit");
    Expect(capture.ticket == ticket && continuation.ticketCaptured && continuation.ticketStable &&
               capture.rebindMessages == 1 && continuation.replacementAfterFirstPixel &&
               fixture.queue.DeviceGeneration() == capture.initialDeviceGeneration + 2,
           "continue-rebind.same-ticket-resumes-on-replacement-device-generation");
    Expect(capture.prepareCalls == 3 && capture.queueBinds == 1 && capture.reservationHeldOnEveryPrepare &&
               continuation.detachedCalls == 3 && continuation.boundaryClean &&
               continuation.reservationHeldAtBoundary && continuation.boundedRetirement && continuation.resetCalls == 2,
           "continue-rebind.one-reservation-and-prior-pixel-retires-on-next-worker");
    Expect(capture.completionCalls == 1 && capture.result.status == JobStatus::Finished &&
               RasterMatches(capture.image, 1, 1, std::vector<std::uint8_t>(lastPixel.begin(), lastPixel.end())),
           "continue-rebind.single-final-exact-pixel-publication");
    ExpectNoLeaks(fixture, "continue-rebind.reservation-mapping-and-utility-released");
}

void TestPixelContinuationCancellation(Fixture& fixture)
{
    constexpr D3D11_BOX initialRegion{0, 0, 0, 1, 1, 1};
    CanvasDocument document = PatternDocument(7, 5);
    CanvasDocument displayDocument = PatternDocument(4, 3);
    CaptureState capture{fixture, displayDocument};
    capture.expectedReservation = DocumentReadbackJob::RequiredMemory(initialRegion);
    ContinuationState continuation{fixture, capture};
    continuation.cancelWhileSuspended = true;
    std::unique_ptr<Job> job =
        std::make_unique<PixelContinuationProbe>(fixture, capture, continuation, std::move(document));
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, capture.expectedReservation}, ticket);
    Expect(submit == S_OK, "continue-cancel.submit");
    if (submit != S_OK)
        return;
    capture.ticket = ticket;

    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &capture);
    Expect(settled == SettleResult::Settled, "continue-cancel.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    Expect(capture.ownerMessageSeen && continuation.detachedCalls == 1 && continuation.cancelAccepted &&
               continuation.resumeAccepted && capture.completionCalls == 1 &&
               capture.result.status == JobStatus::Cancelled && !capture.image.HasStorage(),
           "continue-cancel.owner-message-cancels-suspended-request-and-wakes-cancelled-worker");
    Expect(continuation.boundaryClean && continuation.reservationHeldAtBoundary,
           "continue-cancel.suspended-boundary-retains-reservation-without-mapped-or-utility-resources");
    ExpectNoLeaks(fixture, "continue-cancel.reservation-mapping-and-utility-released");
}

void TestPrepareFailure(Fixture& fixture)
{
    CanvasDocument document = PatternDocument(9, 7);
    CanvasDocument displayDocument = PatternDocument(4, 3);
    const D3D11_BOX region{0, 0, 0, 9, 7, 1};
    CaptureState state{fixture, displayDocument};
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    state.failPrepare = true;
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket);
    Expect(submit == S_OK, "failure.submit");
    if (submit != S_OK)
        return;
    state.ticket = ticket;
    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "failure.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    Expect(state.completionCalls == 1 && state.result.status == JobStatus::Failed &&
               state.result.error == E_ACCESSDENIED && !state.image.HasStorage(),
           "failure.prepare-error-is-reported-without-partial-image");
    ExpectNoLeaks(fixture, "failure.reservation-and-utility-resources-released");
}

void TestLaterTileMapFailure(Fixture& fixture)
{
    // One complete tile is copied before a persistent driver-busy second tile. No prefix of the requested
    // document may be delivered as a successful Raster.
    CanvasDocument document = PatternDocument(17, 1);
    CanvasDocument displayDocument = PatternDocument(4, 3);
    fixture.composition.SetTextureLimitForTest(16);
    fixture.queue.SetMapBusyForTest(2, 1);
    const auto before = fixture.queue.Statistics();
    const D3D11_BOX region{0, 0, 0, 17, 1, 1};
    CaptureState state{fixture, displayDocument};
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    Expect(fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket) == S_OK,
           "later-tile-failure.submit");
    state.ticket = ticket;
    const auto settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "later-tile-failure.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    const auto after = fixture.queue.Statistics();
    Expect(state.completionCalls == 1 && state.result.status == JobStatus::Failed &&
               state.result.error == DXGI_ERROR_WAS_STILL_DRAWING && !state.image.HasStorage() &&
               after.copies - before.copies == 2 && after.maps - before.maps == 3 &&
               after.readinessBarriers - before.readinessBarriers == 1 && after.busyFailures - before.busyFailures == 1,
           "later-tile-failure.one-acknowledged-prefix-no-partial-publication-or-restart");
    ExpectNoLeaks(fixture, "later-tile-failure.reservation-mappings-and-utility-released");
    fixture.queue.SetMapBusyForTest(0);
}

void TestGenerationRebind(Fixture& fixture, unsigned int requestedRebinds)
{
    CanvasDocument document = PatternDocument(19, 13);
    CanvasDocument displayDocument = PatternDocument(8, 6);
    fixture.composition.SetTextureLimitForTest(64);
    const D3D11_BOX region{0, 0, 0, 19, 13, 1};
    CaptureState state{fixture, displayDocument};
    state.generationRebind = true;
    state.requestedRebinds = requestedRebinds;
    state.expectedReservation = DocumentReadbackJob::RequiredMemory(region);
    state.initialDeviceGeneration = fixture.queue.DeviceGeneration();
    std::unique_ptr<Job> job = std::make_unique<ReadbackProbe>(fixture, state, document, region);
    JobTicket ticket{};
    const HRESULT submit = fixture.scheduler.Submit(job, {JobClass::Commit, 0, state.expectedReservation}, ticket);
    Expect(submit == S_OK, "rebind.submit");
    if (submit != S_OK)
        return;
    state.ticket = ticket;

    const SettleResult settled = SettleJobs(fixture.scheduler, 15000, DispatchReadbackMessage, &state);
    Expect(settled == SettleResult::Settled, "rebind.settle");
    if (settled != SettleResult::Settled)
    {
        fixture.usable = false;
        return;
    }
    if (state.rebindSetupFailed)
    {
        std::printf("     rebind device setup HRESULT=0x%08x\n", static_cast<unsigned int>(state.rebindSetupResult));
        Expect(false, "rebind.replacement-device-setup");
        fixture.usable = false;
        return;
    }

    const unsigned int recoveryCount = (std::min)(requestedRebinds, 2u);
    const unsigned int expectedPrepareCalls = 1u + recoveryCount * 2u;
    Expect(state.rebindMessages == requestedRebinds && state.rebindPendingObserved,
           "rebind.each-owner-message-retires-a-pending-request");
    Expect(state.prepareCalls == expectedPrepareCalls && state.recoveryPrepareCalls == recoveryCount &&
               state.queueBinds == 1 && state.prepareSucceeded && state.reservationHeldOnEveryPrepare,
           "rebind.prepare-accepts-current-generation-without-per-tile-rebind");
    Expect(fixture.queue.DeviceGeneration() == state.initialDeviceGeneration + 1u + requestedRebinds,
           "rebind.device-generation-advances-on-each-owner-rebind");

    if (requestedRebinds == 1)
    {
        const auto expected = FormulaRegion(region);
        const bool complete = state.completionCalls == 1 && state.result.status == JobStatus::Finished &&
                              RasterMatches(state.image, 19, 13, expected) &&
                              state.deviceGeneration == fixture.queue.DeviceGeneration();
        if (!complete)
            std::printf("     rebind-once status=%u error=0x%08x image=%dx%d captured-generation=%llu current=%llu\n",
                        static_cast<unsigned int>(state.result.status), static_cast<unsigned int>(state.result.error),
                        state.image.Width(), state.image.Height(),
                        static_cast<unsigned long long>(state.deviceGeneration),
                        static_cast<unsigned long long>(fixture.queue.DeviceGeneration()));
        Expect(complete, "rebind.once-restarts-and-publishes-complete-pattern");
    }
    else
        Expect(state.completionCalls == 1 && state.result.status == JobStatus::Failed &&
                   state.result.error == E_ABORT && !state.image.HasStorage(),
               "rebind.third-generation-change-fails-after-two-restarts-without-partial-output");

    ExpectNoLeaks(fixture, "rebind.no-lent-inflight-reservation-or-utility-resources");
}
} // namespace

int RunDocumentReadbackTests()
{
    gFailures = 0;
    Fixture fixture;
    if (!fixture.Initialize())
    {
        fixture.Close();
        return 1;
    }

    RunPatternCase(fixture, "pattern.full-image-literal-pixels", D3D11_BOX{0, 0, 0, 137, 93, 1}, 2048, false);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    RunPatternCase(fixture, "pattern.cropped-region-coordinate-offsets", D3D11_BOX{11, 7, 0, 28, 18, 1}, 2048, false);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    RunPatternCase(fixture, "pattern.one-pixel-readback", D3D11_BOX{63, 41, 0, 64, 42, 1}, 2048, false);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    RunPatternCase(fixture, "pattern.tiled-image-literal-pixels", D3D11_BOX{0, 0, 0, 137, 93, 1}, 64, true);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestComplexSynchronousReference(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestOwnerCancellation(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestPixelContinuation(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestPixelContinuationDeviceReplacement(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestPixelContinuationCancellation(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestPrepareFailure(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestLaterTileMapFailure(fixture);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestGenerationRebind(fixture, 1);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }
    TestGenerationRebind(fixture, 3);
    if (!fixture.usable)
    {
        fixture.Close();
        return gFailures;
    }

    fixture.Close();
    if (gFailures != 0)
        std::printf("%d document readback test(s) failed.\n", gFailures);
    else
        std::printf("All document readback tests passed.\n");
    return gFailures;
}

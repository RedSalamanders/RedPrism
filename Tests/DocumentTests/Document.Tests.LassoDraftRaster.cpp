#include "Document.Tests.LassoDraftRaster.h"

#include "Jobs.h"
#include "LassoDraftRasterWork.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d2d1_1.h>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint32_t kWidth = 48;
constexpr std::uint32_t kHeight = 40;
constexpr std::uint64_t kSourceAdmission = 4ull * 1024ull * 1024ull;
constexpr std::uint64_t kAdmission = 96ull * 1024ull * 1024ull;
constexpr std::uint32_t kWaitMs = 10000;
constexpr UINT kCancelAtCheckpointMessage = WM_APP + 0x31;
unsigned int gChecks = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s lasso-draft-raster.%s\n", condition ? "PASS" : "FAIL", name);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kMemoryAdmissionHeadroomBytes + kSourceAdmission + kAdmission,
                kMemoryAdmissionHeadroomBytes + kSourceAdmission + kAdmission};
    return true;
}

enum class Shape : std::uint8_t
{
    Horizontal,
    Rectangle,
    Ellipse,
    Freehand,
    Polygon
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Case final
{
    Shape shape = Shape::Horizontal;
    float dpi = 96.0f;
    std::array<Vec2, 4> referencePoints{};
    std::size_t referencePointCount = 0;
    SelectionBounds referenceBounds{};
    Vec2 referenceFirst{};
    std::optional<Vec2> referenceCursor;
    BoundedLassoGesture::Display source;
    PreparedLassoDraftRaster result;
    std::shared_ptr<SharedPublicationMemory> sourceMemory;
    std::shared_ptr<SharedPublicationMemory> outputMemory;
    std::optional<BoundedLassoGesture> gesture;
    std::optional<BoundedLassoPoints> arena;
    JobScheduler* scheduler = nullptr;
    JobTicket sourceTicket{};
    JobTicket ticket{};
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::thread::id owner{};
    std::thread::id worker{};
    bool capturedOnOwner = false;
    bool resumedOriginal = false;
    bool comReady = false;
    bool metadataMatches = false;
    bool refusedOutputUnchanged = false;
    HRESULT prepareResult = E_UNEXPECTED;
    bool prepareReturned = false;
    HRESULT memoryBindResult = E_UNEXPECTED;
    bool memoryBoundToOriginal = false;
    HRESULT sourceBindResult = E_UNEXPECTED;
    bool sourceBoundToOriginal = false;
    bool cancelBeforePrepare = false;
    bool cancelAtCheckpoint = false;
    bool sealOutput = false;
    std::uint64_t outputMemoryLimit = 2ull * 1024ull * 1024ull;
    JobStatus expectedStatus = JobStatus::Finished;
    HRESULT expectedError = S_OK;
    DWORD ownerThreadId = 0;
    std::atomic<bool> checkpointPosted{false};
    std::atomic<bool> gatePostSucceeded{false};
    std::atomic<bool> gateWaitCompleted{false};
    std::atomic<bool> gateReleaseSucceeded{true};
    bool cancellationDelivered = false;
    wil::unique_handle checkpointRelease;
    HANDLE shutdownEvent = nullptr;
    HRESULT refusal = E_UNEXPECTED;
    std::weak_ptr<const void> weak;
    std::vector<std::uint8_t> beforeRefusal;
};
#pragma warning(pop)

void BindForJoinedRetirement(Case& test, const std::shared_ptr<SharedPublicationMemory>& memory, JobTicket ticket,
                             HRESULT& bindResult, bool& bound) noexcept
{
    if (!memory)
        return;
    if (memory->IsBound())
    {
        bound = memory->BoundTo(*test.scheduler, ticket);
        bindResult = bound ? S_OK : E_UNEXPECTED;
        return;
    }
    bindResult = memory->BindForRetirement(*test.scheduler, ticket);
    bound = memory->BoundTo(*test.scheduler, ticket);
}

void ReleaseGate(Case& test) noexcept
{
    if (test.checkpointRelease && SetEvent(test.checkpointRelease.get()) == FALSE)
        test.gateReleaseSucceeded.store(false);
}

void StopAndClose(JobScheduler& scheduler, Case& test) noexcept
{
    ReleaseGate(test);
    scheduler.RequestStop();
    scheduler.Close();
}

PreparedLassoDraftRaster::GridKey MakeGrid(float dpi, bool halfPixelLine = false) noexcept
{
    PreparedLassoDraftRaster::GridKey grid{};
    grid.width = kWidth;
    grid.height = kHeight;
    grid.documentScalePixel = 1.0;
    grid.originPixelX = 5.0;
    grid.originPixelY = halfPixelLine ? 4.5 : 4.0;
    grid.dpi = dpi;
    return grid;
}

void SeedIndependentReference(Case& test) noexcept
{
    if (test.shape == Shape::Horizontal)
    {
        test.referencePoints = {{{10.0, 12.5}, {34.0, 12.5}, {}, {}}};
        test.referencePointCount = 2;
        test.referenceBounds = {10.0, 12.5, 34.0, 12.5};
        test.referenceFirst = {10.0, 12.5};
    }
    else if (test.shape == Shape::Rectangle || test.shape == Shape::Ellipse)
    {
        test.referencePoints = {{{10.0, 12.0}, {30.0, 12.0}, {30.0, 28.0}, {10.0, 28.0}}};
        test.referencePointCount = 4;
        test.referenceBounds = {10.0, 12.0, 30.0, 28.0};
        test.referenceFirst = {10.0, 12.0};
    }
    else if (test.shape == Shape::Freehand)
    {
        test.referencePoints = {{{10.0, 12.0}, {18.0, 8.0}, {27.0, 24.0}, {36.0, 12.0}}};
        test.referencePointCount = 4;
        test.referenceBounds = {10.0, 8.0, 36.0, 24.0};
        test.referenceFirst = {10.0, 12.0};
    }
    else
    {
        test.referencePoints = {{{16.0, 16.0}, {10.0, 12.0}, {26.0, 28.0}, {}}};
        test.referencePointCount = 3;
        test.referenceBounds = {10.0, 12.0, 26.0, 28.0};
        test.referenceFirst = {16.0, 16.0};
        test.referenceCursor = Vec2{36.0, 20.0};
    }
}

bool SeedGeometry(Case& test) noexcept
{
    if (!test.arena)
        return false;
    test.gesture.emplace(std::move(test.arena.value()));
    test.arena.reset();
    const auto kind = test.shape == Shape::Rectangle ? LassoKind::Rectangle
                      : test.shape == Shape::Ellipse ? LassoKind::Ellipse
                      : test.shape == Shape::Polygon ? LassoKind::Polygonal
                                                     : LassoKind::Freehand;
    const Vec2 start{test.shape == Shape::Polygon ? 16.0 : 10.0, test.shape == Shape::Horizontal ? 12.5
                                                                 : test.shape == Shape::Polygon  ? 16.0
                                                                                                 : 12.0};
    if (!test.gesture.value().BeginOwner(73, kind, SelectionMode::Add, true, start))
        return false;
    if (test.shape == Shape::Rectangle || test.shape == Shape::Ellipse)
    {
        if (test.gesture.value().MarqueeOwner(test.gesture.value().CurrentOwner(), {30.0, 28.0}, false, false) !=
            BoundedLassoGesture::Outcome::Changed)
            return false;
    }
    else if (test.shape == Shape::Horizontal)
    {
        if (test.gesture.value().ExtendOwner(test.gesture.value().CurrentOwner(), {34.0, 12.5}) !=
            BoundedLassoGesture::Outcome::Changed)
            return false;
    }
    else if (test.shape == Shape::Freehand)
    {
        for (const Vec2 point : std::array<Vec2, 3>{{{18.0, 8.0}, {27.0, 24.0}, {36.0, 12.0}}})
            if (test.gesture.value().ExtendOwner(test.gesture.value().CurrentOwner(), point) !=
                BoundedLassoGesture::Outcome::Changed)
                return false;
    }
    else
    {
        for (const Vec2 point : std::array<Vec2, 2>{{{10.0, 12.0}, {26.0, 28.0}}})
            if (test.gesture.value().ExtendOwner(test.gesture.value().CurrentOwner(), point) !=
                BoundedLassoGesture::Outcome::Changed)
                return false;
        if (test.gesture.value().CursorOwner(test.gesture.value().CurrentOwner(), Vec2{36.0, 20.0}) !=
            BoundedLassoGesture::Outcome::Changed)
            return false;
    }
    return true;
}

bool CaptureForRaster(Case& test) noexcept
{
    if (!test.gesture || !test.sourceBoundToOriginal)
        return false;
    test.source = test.gesture.value().CaptureOwner();
    return !test.source.points.Empty();
}

void Checkpoint(void* context, std::uint64_t) noexcept
{
    auto& test = *static_cast<Case*>(context);
    if (!test.cancelAtCheckpoint || test.checkpointPosted.exchange(true))
        return;
    if (!PostThreadMessageW(test.ownerThreadId, kCancelAtCheckpointMessage, 0, 0))
    {
        test.gatePostSucceeded.store(false);
        if (SetEvent(test.checkpointRelease.get()) == FALSE)
            test.gateReleaseSucceeded.store(false);
        return;
    }
    test.gatePostSucceeded.store(true);
    const HANDLE waits[]{test.checkpointRelease.get(), test.shutdownEvent};
    const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, INFINITE);
    test.gateWaitCompleted.store(waited == WAIT_OBJECT_0 || waited == WAIT_OBJECT_0 + 1);
}

void GateBeforePrepare(Case& test) noexcept
{
    if (!test.cancelBeforePrepare)
        return;
    if (!PostThreadMessageW(test.ownerThreadId, kCancelAtCheckpointMessage, 0, 0))
    {
        test.gatePostSucceeded.store(false);
        if (SetEvent(test.checkpointRelease.get()) == FALSE)
            test.gateReleaseSucceeded.store(false);
        return;
    }
    test.gatePostSucceeded.store(true);
    const HANDLE waits[]{test.checkpointRelease.get(), test.shutdownEvent};
    const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, INFINITE);
    test.gateWaitCompleted.store(waited == WAIT_OBJECT_0 || waited == WAIT_OBJECT_0 + 1);
}

void DispatchOwnerMessage(void* context, void* nativeMessage) noexcept
{
    auto& test = *static_cast<Case*>(context);
    const auto* message = static_cast<const MSG*>(nativeMessage);
    if (message && message->hwnd == nullptr && message->message == kCancelAtCheckpointMessage)
    {
        test.cancellationDelivered = test.scheduler->Cancel(test.ticket);
        if (SetEvent(test.checkpointRelease.get()) == FALSE)
            test.gateReleaseSucceeded.store(false);
        return;
    }
    if (message)
    {
        TranslateMessage(message);
        DispatchMessageW(message);
    }
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SourceArenaJob final : public Job
{
  public:
    explicit SourceArenaJob(Case& test) noexcept : _test(test) {}
    JobResult Run(JobContext& context) override
    {
        if (std::this_thread::get_id() == _test.owner || context.reservedMemoryBytes != kSourceAdmission)
            return {JobStatus::Failed, E_INVALIDARG};
        _test.sourceMemory = SharedPublicationMemory::CreateForWork(kSourceAdmission);
        if (!_test.sourceMemory)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _test.arena = BoundedLassoPoints::CreateForWork(_test.sourceMemory, 1, context.cancellation);
        return _test.arena ? JobResult{JobStatus::Finished, S_OK} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status != JobStatus::Finished || result.error != S_OK || !_test.sourceMemory || !_test.arena)
        {
            _test.terminal = result;
            BindForJoinedRetirement(_test, _test.sourceMemory, _test.sourceTicket, _test.sourceBindResult,
                                    _test.sourceBoundToOriginal);
            return;
        }
        _test.sourceBindResult = _test.sourceMemory->BindAfterCompletedWork(*_test.scheduler, _test.sourceTicket);
        _test.sourceBoundToOriginal = _test.sourceMemory->BoundTo(*_test.scheduler, _test.sourceTicket);
        if (!_test.sourceBoundToOriginal)
            BindForJoinedRetirement(_test, _test.sourceMemory, _test.sourceTicket, _test.sourceBindResult,
                                    _test.sourceBoundToOriginal);
        _test.capturedOnOwner = _test.sourceBindResult == S_OK && _test.sourceBoundToOriginal && SeedGeometry(_test);
        _test.terminal = result;
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _test.scheduler = &scheduler;
        if (ticket != _test.sourceTicket)
        {
            _test.sourceBindResult = E_INVALIDARG;
            _test.sourceBoundToOriginal = false;
            return;
        }
        BindForJoinedRetirement(_test, _test.sourceMemory, _test.sourceTicket, _test.sourceBindResult,
                                _test.sourceBoundToOriginal);
    }

  private:
    Case& _test;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class RasterJob final : public Job
{
  public:
    explicit RasterJob(Case& test) noexcept : _test(test) {}
    JobResult Run(JobContext& context) override
    {
        _test.worker = std::this_thread::get_id();
        _test.shutdownEvent = static_cast<HANDLE>(context.shutdownEvent);
        auto clearShutdownEvent = wil::scope_exit([&]() noexcept { _test.shutdownEvent = nullptr; });
        if (_test.worker == _test.owner || context.reservedMemoryBytes != kAdmission || !context.deferredRetirement)
            return {JobStatus::Failed, E_INVALIDARG};
        if (!_captured)
        {
            _captured = true;
            return {JobStatus::Finished, S_OK};
        }
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized))
            return {JobStatus::Failed, initialized};
        auto uninitialize = wil::scope_exit([&]() noexcept { CoUninitialize(); });
        _test.comReady = true;
        _test.outputMemory = SharedPublicationMemory::CreateForWork(_test.outputMemoryLimit);
        if (!_test.outputMemory)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        if (_test.sealOutput)
            (void)_test.outputMemory->SealLocalAllocations();
        const auto grid = MakeGrid(_test.dpi, _test.shape == Shape::Horizontal);
        const PreparedLassoDraftRaster::Observer observer{&_test, _test.cancelAtCheckpoint ? &Checkpoint : nullptr};
        GateBeforePrepare(_test);
        _test.prepareResult = PreparedLassoDraftRaster::Prepare(_test.source, grid, _test.outputMemory,
                                                                context.cancellation, observer, _test.result);
        _test.prepareReturned = true;
        if (FAILED(_test.prepareResult))
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled, _test.prepareResult}
                                                      : JobResult{JobStatus::Failed, _test.prepareResult};
        _test.weak = _test.result.WeakStorageForTest();
        _test.metadataMatches =
            _test.result.Source() && _test.result.Source()->points.SameCapture(_test.source.points) &&
            _test.result.Source()->points.ArenaIdentity() == _test.source.points.ArenaIdentity() &&
            _test.result.Source()->key == _test.source.key && _test.result.Source()->kind == _test.source.kind &&
            _test.result.Source()->cursor == _test.source.cursor && _test.result.Source()->mode == _test.source.mode &&
            _test.result.Key() == grid &&
            _test.result.Pixels().size() == static_cast<std::size_t>(kWidth) * kHeight * 4;
        _test.beforeRefusal.assign(_test.result.Pixels().begin(), _test.result.Pixels().end());
        auto invalid = grid;
        invalid.width = 0;
        _test.refusal = PreparedLassoDraftRaster::Prepare(_test.source, invalid, _test.outputMemory,
                                                          context.cancellation, {}, _test.result);
        _test.refusedOutputUnchanged = _test.refusal == E_INVALIDARG && _test.result.HasStorage() &&
                                       !_test.weak.expired() &&
                                       std::equal(_test.beforeRefusal.begin(), _test.beforeRefusal.end(),
                                                  _test.result.Pixels().begin(), _test.result.Pixels().end());
        return {JobStatus::Finished, S_OK};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_resumed || result.status != JobStatus::Finished || ticket != _test.ticket ||
            !_test.scheduler->InUiCallback())
            return false;
        _test.owner = std::this_thread::get_id();
        // Capture the already-accounted immutable source only after this raster ticket is admitted.
        const bool captured = CaptureForRaster(_test);
        _resumed = true;
        _test.resumedOriginal = captured && _test.scheduler->Resume(ticket);
        return _test.resumedOriginal;
    }
    void Completed(JobResult result) noexcept override
    {
        _test.terminal = result;
        if (_test.outputMemory)
        {
            if (result.status == JobStatus::Finished && result.error == S_OK)
            {
                _test.memoryBindResult = _test.outputMemory->BindAfterCompletedWork(*_test.scheduler, _test.ticket);
                _test.memoryBoundToOriginal = _test.outputMemory->BoundTo(*_test.scheduler, _test.ticket);
                if (!_test.memoryBoundToOriginal)
                    BindForJoinedRetirement(_test, _test.outputMemory, _test.ticket, _test.memoryBindResult,
                                            _test.memoryBoundToOriginal);
            }
            else
                BindForJoinedRetirement(_test, _test.outputMemory, _test.ticket, _test.memoryBindResult,
                                        _test.memoryBoundToOriginal);
        }
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _test.scheduler = &scheduler;
        if (ticket != _test.ticket)
        {
            _test.memoryBindResult = E_INVALIDARG;
            _test.memoryBoundToOriginal = false;
            return;
        }
        BindForJoinedRetirement(_test, _test.outputMemory, _test.ticket, _test.memoryBindResult,
                                _test.memoryBoundToOriginal);
    }

  private:
    Case& _test;
    bool _captured = false;
    bool _resumed = false;
};
#pragma warning(pop)

bool ReferencePixels(const Case& test, const PreparedLassoDraftRaster::GridKey& grid, std::vector<std::uint8_t>& pixels)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return false;
    auto uninitialize = wil::scope_exit([&]() noexcept { CoUninitialize(); });
    D2D1_FACTORY_OPTIONS options{};
    wil::com_ptr_nothrow<ID2D1Factory1> factory;
    HRESULT result =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, factory.put_void());
    if (FAILED(result))
        return false;
    wil::com_ptr_nothrow<IWICImagingFactory2> imaging;
    result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2),
                              imaging.put_void());
    if (FAILED(result))
        return false;
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    result = imaging->CreateBitmap(grid.width, grid.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                   bitmap.put());
    if (FAILED(result))
        return false;
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    wil::com_ptr_nothrow<ID2D1RenderTarget> target;
    result = factory->CreateWicBitmapRenderTarget(bitmap.get(), props, target.put());
    if (FAILED(result))
        return false;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> shadow;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> white;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> black;
    result = target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.8f), shadow.put());
    if (SUCCEEDED(result))
        result = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), white.put());
    if (SUCCEEDED(result))
        result = target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), black.put());
    if (FAILED(result))
        return false;
    pixels.assign(static_cast<std::size_t>(grid.width) * grid.height * 4, 0);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto bounds = test.referenceBounds;
    const Vec2 anchor{bounds.minX, bounds.minY};
    const auto point = [&](Vec2 value) noexcept
    {
        return D2D1::Point2F(static_cast<float>(grid.originPixelX + (value.x - anchor.x) * grid.documentScalePixel),
                             static_cast<float>(grid.originPixelY + (value.y - anchor.y) * grid.documentScalePixel));
    };
    const bool marquee =
        (test.shape == Shape::Rectangle || test.shape == Shape::Ellipse) && test.referencePointCount == 4;
    for (unsigned int pass = 0; pass != 2; ++pass)
    {
        auto* brush = pass == 0 ? shadow.get() : white.get();
        const float width = (pass == 0 ? 2.0f : 1.0f) * grid.dpi / 96.0f;
        if (marquee)
        {
            const auto topLeft = point({bounds.minX, bounds.minY});
            const auto bottomRight = point({bounds.maxX, bounds.maxY});
            const auto rect = D2D1::RectF(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
            if (test.shape == Shape::Ellipse)
                target->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f),
                                  (rect.right - rect.left) * 0.5f, (rect.bottom - rect.top) * 0.5f),
                    brush, width);
            else
                target->DrawRectangle(rect, brush, width);
        }
        else
        {
            std::optional<D2D1_POINT_2F> previous;
            for (std::size_t index = 0; index < test.referencePointCount; ++index)
            {
                const auto mapped = point(test.referencePoints[index]);
                if (previous)
                    target->DrawLine(previous.value(), mapped, brush, width);
                previous = mapped;
            }
            if (test.shape == Shape::Polygon && test.referenceCursor && previous)
                target->DrawLine(previous.value(), point(test.referenceCursor.value()), brush, width);
        }
        result = target->Flush();
        if (FAILED(result))
            return false;
    }
    if (test.shape == Shape::Polygon)
    {
        const auto first = point(test.referenceFirst);
        const float scale = grid.dpi / 96.0f;
        const auto handle =
            D2D1::RectF(first.x - 4 * scale, first.y - 4 * scale, first.x + 4 * scale, first.y + 4 * scale);
        target->FillRectangle(handle, white.get());
        target->DrawRectangle(handle, black.get(), scale);
    }
    result = target->EndDraw();
    if (FAILED(result))
        return false;
    wil::com_ptr_nothrow<IWICBitmapLock> lock;
    WICRect rect{0, 0, static_cast<int>(grid.width), static_cast<int>(grid.height)};
    result = bitmap->Lock(&rect, WICBitmapLockRead, lock.put());
    if (FAILED(result))
        return false;
    UINT stride = 0;
    UINT available = 0;
    BYTE* data = nullptr;
    result = lock->GetStride(&stride);
    if (SUCCEEDED(result))
        result = lock->GetDataPointer(&available, &data);
    const std::size_t rowBytes = static_cast<std::size_t>(grid.width) * 4;
    if (FAILED(result) || !data || stride < rowBytes ||
        static_cast<std::uint64_t>(grid.height - 1) * stride + rowBytes > available)
        return false;
    for (std::uint32_t row = 0; row < grid.height; ++row)
        std::memcpy(pixels.data() + static_cast<std::size_t>(row) * rowBytes,
                    data + static_cast<std::size_t>(row) * stride, rowBytes);
    return true;
}

bool RunOne(Case& test, JobScheduler& scheduler)
{
    test.scheduler = &scheduler;
    test.owner = std::this_thread::get_id();
    test.ownerThreadId = GetCurrentThreadId();
    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    if (test.cancelAtCheckpoint || test.cancelBeforePrepare)
    {
        test.checkpointRelease.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!test.checkpointRelease)
            return false;
    }
    JobOptions sourceOptions{};
    sourceOptions.kind = JobClass::Selection;
    sourceOptions.memoryBytes = kSourceAdmission;
    sourceOptions.deferredRetirement = true;
    sourceOptions.allowMemoryWaiting = false;
    sourceOptions.reserveRetainedMemory = true;
    std::unique_ptr<Job> sourceJob = std::make_unique<SourceArenaJob>(test);
    if (scheduler.Submit(sourceJob, sourceOptions, test.sourceTicket) != S_OK || sourceJob)
        return false;
    const auto sourceSettled = SettleJobs(scheduler, kWaitMs);
    if (sourceSettled != SettleResult::Settled)
    {
        StopAndClose(scheduler, test);
        return false;
    }
    if (!test.capturedOnOwner || test.sourceBindResult != S_OK || !test.sourceBoundToOriginal)
        return false;
    test.terminal = {JobStatus::Failed, E_UNEXPECTED};
    JobOptions rasterOptions{};
    rasterOptions.kind = JobClass::Selection;
    rasterOptions.memoryBytes = kAdmission;
    rasterOptions.deferredRetirement = true;
    rasterOptions.allowMemoryWaiting = false;
    rasterOptions.reserveRetainedMemory = true;
    std::unique_ptr<Job> job = std::make_unique<RasterJob>(test);
    if (scheduler.Submit(job, rasterOptions, test.ticket) != S_OK || job)
        return false;
    auto settled = (test.cancelAtCheckpoint || test.cancelBeforePrepare)
                       ? SettleJobs(scheduler, kWaitMs, DispatchOwnerMessage, &test)
                       : SettleJobs(scheduler, kWaitMs);
    if (test.cancelAtCheckpoint || test.cancelBeforePrepare)
    {
        ReleaseGate(test);
        if (settled != SettleResult::Settled)
        {
            scheduler.RequestStop();
            settled = SettleJobs(scheduler, kWaitMs, DispatchOwnerMessage, &test);
        }
    }
    if (settled != SettleResult::Settled)
    {
        StopAndClose(scheduler, test);
        return false;
    }
    return test.terminal.status == test.expectedStatus &&
           test.terminal.error == static_cast<std::int32_t>(test.expectedError) && test.capturedOnOwner &&
           test.resumedOriginal && (test.expectedStatus != JobStatus::Finished || test.comReady);
}

void CheckRefusal(bool sealed, JobScheduler& scheduler)
{
    auto fixture = std::make_unique<Case>();
    fixture->sealOutput = sealed;
    fixture->outputMemoryLimit = sealed ? 2ull * 1024ull * 1024ull : 1;
    fixture->expectedStatus = JobStatus::Failed;
    fixture->expectedError = E_OUTOFMEMORY;
    const bool refusedAsExpected = RunOne(*fixture, scheduler) && fixture->terminal.status == JobStatus::Failed &&
                                   fixture->terminal.error == E_OUTOFMEMORY && !fixture->prepareReturned &&
                                   fixture->prepareResult == E_UNEXPECTED;
    const bool noPartial = !fixture->result.HasStorage() && fixture->source.points.Count() != 0;
    const bool bound = fixture->memoryBindResult == S_OK && fixture->memoryBoundToOriginal;
    fixture->source = {};
    fixture->outputMemory.reset();
    fixture->sourceMemory.reset();
    fixture->arena.reset();
    fixture->gesture.reset();
    fixture->result = {};
    const bool retired = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
    Expect(refusedAsExpected && noPartial && bound && retired,
           sealed ? "memory.sealed-owner-refuses-with-source-readable-no-partial-result-and-retirement"
                  : "memory.insufficient-owner-refuses-with-source-readable-no-partial-result-and-retirement");
}

void CheckCancellation(bool atCheckpoint, JobScheduler& scheduler)
{
    auto fixture = std::make_unique<Case>();
    fixture->cancelBeforePrepare = !atCheckpoint;
    fixture->cancelAtCheckpoint = atCheckpoint;
    fixture->expectedStatus = JobStatus::Cancelled;
    fixture->expectedError = 0;
    const bool completed = RunOne(*fixture, scheduler);
    const bool gated = fixture->cancellationDelivered && fixture->gatePostSucceeded.load() &&
                       fixture->gateWaitCompleted.load() && fixture->gateReleaseSucceeded.load() &&
                       (atCheckpoint ? fixture->checkpointPosted.load() : !fixture->checkpointPosted.load());
    const bool noPartial = !fixture->result.HasStorage() && fixture->source.points.Count() != 0;
    const bool returnsCancel = fixture->prepareResult == HRESULT_FROM_WIN32(ERROR_CANCELLED);
    const bool bound = fixture->memoryBindResult == S_OK && fixture->memoryBoundToOriginal;
    fixture->source = {};
    fixture->outputMemory.reset();
    fixture->sourceMemory.reset();
    fixture->arena.reset();
    fixture->gesture.reset();
    fixture->result = {};
    const bool retired = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
    Expect(completed && gated && returnsCancel && noPartial && bound && retired,
           atCheckpoint ? "cancel.after-checkpoint-returns-cancel-hresult-no-partial-result-and-retires"
                        : "cancel.before-first-checkpoint-returns-cancel-hresult-no-partial-result-and-retires");
}

void CheckShape(Shape shape, float dpi, const char* label, JobScheduler& scheduler)
{
    auto fixture = std::make_unique<Case>();
    fixture->shape = shape;
    fixture->dpi = dpi;
    SeedIndependentReference(*fixture);
    auto& test = *fixture;
    const bool ran = RunOne(test, scheduler);
    std::vector<std::uint8_t> reference;
    const auto grid = MakeGrid(dpi, shape == Shape::Horizontal);
    const bool referenceReady = ran && ReferencePixels(test, grid, reference);
    const auto actual = test.result.Pixels();
    const bool parity = referenceReady && actual.size() == reference.size() &&
                        std::equal(actual.begin(), actual.end(), reference.begin(), reference.end());
    const bool accepted = ran && test.metadataMatches && test.refusedOutputUnchanged && parity;
    char name[128]{};
    const auto formatted =
        std::format_to_n(name, sizeof(name) - 1, "{}.dpi-{}.matches-independent-d2d-and-retains-source", label,
                         static_cast<unsigned int>(dpi));
    name[(std::min)(static_cast<std::size_t>(formatted.size), sizeof(name) - 1)] = '\0';
    if (shape == Shape::Horizontal && dpi == 96.0f && actual.size() == kWidth * kHeight * 4)
    {
        const auto pixel = [&](std::uint32_t x, std::uint32_t y) noexcept
        {
            const auto offset = (static_cast<std::size_t>(y) * kWidth + x) * 4;
            return std::array<std::uint8_t, 4>{actual[offset], actual[offset + 1], actual[offset + 2],
                                               actual[offset + 3]};
        };
        Expect(pixel(20, 4) == std::array<std::uint8_t, 4>{255, 255, 255, 255},
               "pixels.horizontal-line-white-source-over-shadow-is-premultiplied-bgra");
        Expect(pixel(20, 3) == std::array<std::uint8_t, 4>{0, 0, 0, 102},
               "pixels.horizontal-line-shadow-edge-premultiplied-alpha-102");
        Expect(pixel(20, 20) == std::array<std::uint8_t, 4>{0, 0, 0, 0},
               "pixels.horizontal-line-interior-is-transparent");
    }
    if (shape == Shape::Polygon && dpi == 96.0f && actual.size() == kWidth * kHeight * 4)
    {
        const auto offset = (static_cast<std::size_t>(8) * kWidth + 11) * 4;
        Expect(actual[offset] == 255 && actual[offset + 1] == 255 && actual[offset + 2] == 255 &&
                   actual[offset + 3] == 255,
               "pixels.polygon-handle-interior-is-opaque-white-bgra");
    }
    bool resultRetainsSource = false;
    bool weakRetired = test.weak.expired();
    if (!weakRetired)
    {
        const auto sourceCount = test.source.points.Count();
        const auto sourceIdentity = test.source.points.ArenaIdentity();
        test.source = {};
        test.gesture.reset();
        resultRetainsSource = test.result.Source() && test.result.Source()->points.Count() == sourceCount &&
                              test.result.Source()->points.ArenaIdentity() == sourceIdentity;
        test.result = {};
        weakRetired = test.weak.expired();
        test.weak.reset();
    }
    const bool bound = test.memoryBindResult == S_OK && test.memoryBoundToOriginal;
    test.source = {};
    test.outputMemory.reset();
    test.sourceMemory.reset();
    test.arena.reset();
    test.gesture.reset();
    test.result = {};
    const bool retired = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
    Expect(accepted && resultRetainsSource && weakRetired && bound && retired, name);
}

} // namespace

int RunLassoDraftRasterTests()
{
    gChecks = 0;
    gFailures = 0;
    JobScheduler scheduler;
    const HRESULT initialized = scheduler.Initialize(1, {}, {SampleMemory, nullptr});
    const bool ready = initialized == S_OK && scheduler.EnableDeferredRelease();
    Expect(ready, "scheduler.one-worker-original-ticket-admission-is-ready");
    if (ready)
    {
        CheckShape(Shape::Horizontal, 96.0f, "horizontal", scheduler);
        for (const float dpi : std::array<float, 3>{96.0f, 144.0f, 192.0f})
        {
            CheckShape(Shape::Rectangle, dpi, "rectangle", scheduler);
            CheckShape(Shape::Ellipse, dpi, "ellipse", scheduler);
            CheckShape(Shape::Freehand, dpi, "freehand", scheduler);
            CheckShape(Shape::Polygon, dpi, "polygon", scheduler);
        }
        CheckRefusal(false, scheduler);
        CheckRefusal(true, scheduler);
        CheckCancellation(false, scheduler);
        CheckCancellation(true, scheduler);
        const bool drained = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled && scheduler.IsIdle() &&
                             scheduler.ReservedMemoryBytes() == 0;
        Expect(drained, "scheduler.original-tickets-and-reservations-drain");
        scheduler.Close();
    }
    else
    {
        Expect(false, "setup-failed.horizontal-raster-and-literal-pixels-not-run");
        Expect(false, "setup-failed.rectangle-ellipse-freehand-polygon-dpi-matrix-not-run");
        Expect(false, "setup-failed.grid-refusal-output-preservation-not-run");
        Expect(false, "setup-failed.insufficient-and-sealed-local-owner-refusals-not-run");
        Expect(false, "setup-failed.pre-and-post-checkpoint-cancellation-not-run");
        scheduler.Close();
    }
    std::printf("SUMMARY lasso-draft-raster checks=%u failures=%u\n", gChecks, gFailures);
    return static_cast<int>(gFailures);
}

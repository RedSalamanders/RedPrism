#include "SelectionOutlineRasterWork.h"

#include "AccountedPublicationObject.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <objbase.h>
#include <wincodec.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
constexpr std::uint64_t kOpaqueD2DAllowanceBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kFixedWorkerAllowanceBytes = 256ull * 1024ull;
constexpr std::uint64_t kPlaneAllocationAllowanceBytes = 64;
constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();

[[nodiscard]] bool AddBytes(std::uint64_t value, std::uint64_t& total) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}

[[nodiscard]] bool MultiplyBytes(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right != 0 && left > kMaximum / right)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool ValidGrid(const PreparedSelectionOutlineRaster::GridKey& grid, std::uint64_t& pixels) noexcept
{
    if (grid.width == 0 || grid.height == 0 || grid.width > PreparedSelectionOutlineRaster::MaximumSide ||
        grid.height > PreparedSelectionOutlineRaster::MaximumSide || !std::isfinite(grid.documentScalePixel) ||
        grid.documentScalePixel <= 0.0 || !std::isfinite(grid.originPixelX) || !std::isfinite(grid.originPixelY) ||
        !std::isfinite(grid.dpi) || grid.dpi <= 0.0f || grid.dpi / 96.0f <= 0.0f ||
        grid.documentScalePixel > std::numeric_limits<float>::max() ||
        static_cast<float>(grid.documentScalePixel) <= 0.0f)
        return false;
    if (grid.clipped &&
        (!std::isfinite(grid.clipLeft) || !std::isfinite(grid.clipTop) || !std::isfinite(grid.clipRight) ||
         !std::isfinite(grid.clipBottom) || grid.clipLeft < 0.0 || grid.clipTop < 0.0 || grid.clipRight > grid.width ||
         grid.clipBottom > grid.height || grid.clipRight <= grid.clipLeft || grid.clipBottom <= grid.clipTop))
        return false;
    if (!MultiplyBytes(grid.width, grid.height, pixels) || pixels > PreparedSelectionOutlineRaster::MaximumPixels)
        return false;
    return true;
}

[[nodiscard]] bool ToFloat(double value, float& output) noexcept
{
    if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(value);
    return std::isfinite(output);
}

[[nodiscard]] HRESULT MakePath(ID2D1Factory1* factory, const SelectionLoops& source, Vec2 anchor,
                               CancelToken cancellation, PreparedSelectionOutlineRaster::Observer observer,
                               std::uint64_t& work, wil::com_ptr_nothrow<ID2D1PathGeometry>& geometry) noexcept
{
    HRESULT result = factory->CreatePathGeometry(geometry.put());
    if (FAILED(result))
        return result;
    wil::com_ptr_nothrow<ID2D1GeometrySink> sink;
    result = geometry->Open(sink.put());
    if (FAILED(result))
        return result;

    const auto checkpoint = [&]() noexcept
    {
        const std::uint64_t current = work++;
        if ((current & 63u) == 0)
        {
            if (observer.checkpoint)
                observer.checkpoint(observer.context, current);
            return !cancellation.IsCancelled();
        }
        return true;
    };

    for (const auto& loop : source)
    {
        if (!checkpoint())
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (loop.size() < 2)
            continue;
        float x = 0.0f;
        float y = 0.0f;
        if (!ToFloat(loop.front().x - anchor.x, x) || !ToFloat(loop.front().y - anchor.y, y))
            return E_INVALIDARG;
        sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
        for (std::size_t index = 1; index < loop.size(); ++index)
        {
            if (!checkpoint())
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (!ToFloat(loop[index].x - anchor.x, x) || !ToFloat(loop[index].y - anchor.y, y))
                return E_INVALIDARG;
            sink->AddLine(D2D1::Point2F(x, y));
        }
        // Selection outline semantics close every contour, including contours representing holes.
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    if (cancellation.IsCancelled())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return sink->Close();
}

[[nodiscard]] HRESULT RasterizePlane(ID2D1Factory1* d2dFactory, IWICBitmap* bitmap, ID2D1RenderTarget* target,
                                     ID2D1SolidColorBrush* white, ID2D1PathGeometry* geometry, std::size_t phase,
                                     const PreparedSelectionOutlineRaster::GridKey& grid,
                                     std::span<std::uint8_t> destination, CancelToken cancellation,
                                     PreparedSelectionOutlineRaster::Observer observer, std::uint64_t& work) noexcept
{
    const float pixelsPerDip = grid.dpi / 96.0f;
    const auto styleProperties = D2D1::StrokeStyleProperties1(
        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
        phase == 0 ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM, phase == 0 ? 0.0f : static_cast<float>(phase - 1),
        D2D1_STROKE_TRANSFORM_TYPE_FIXED);
    constexpr std::array<float, 2> dashPattern{4.0f, 4.0f};
    wil::com_ptr_nothrow<ID2D1StrokeStyle1> style;
    HRESULT result =
        d2dFactory->CreateStrokeStyle(styleProperties, phase == 0 ? nullptr : dashPattern.data(),
                                      phase == 0 ? 0u : static_cast<UINT32>(dashPattern.size()), style.put());
    if (FAILED(result))
        return result;

    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    // The view clip applies once to the combined white/black result in the composition.
    // Keep both individual coverage planes raw, matching the legacy single Push/Pop pair.
    target->DrawGeometry(geometry, white, pixelsPerDip, style.get());
    // DrawGeometry and EndDraw are opaque Direct2D intervals; cancellation cannot interrupt either call.
    result = target->EndDraw();
    if (FAILED(result))
        return result;
    if (cancellation.IsCancelled())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    const WICRect rect{0, 0, static_cast<INT>(grid.width), static_cast<INT>(grid.height)};
    wil::com_ptr_nothrow<IWICBitmapLock> lock;
    result = bitmap->Lock(&rect, WICBitmapLockRead, lock.put());
    if (FAILED(result))
        return result;
    UINT stride = 0;
    UINT available = 0;
    BYTE* pixels = nullptr;
    result = lock->GetStride(&stride);
    if (SUCCEEDED(result))
        result = lock->GetDataPointer(&available, &pixels);
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(grid.width) * 4u;
    const std::uint64_t lastRowEnd =
        grid.height == 0 ? 0 : static_cast<std::uint64_t>(grid.height - 1) * stride + rowBytes;
    if (FAILED(result))
        return result;
    if (!pixels || stride < rowBytes || lastRowEnd > available ||
        destination.size() != static_cast<std::size_t>(grid.width) * grid.height)
        return E_UNEXPECTED;

    for (std::uint32_t row = 0; row < grid.height; ++row)
    {
        if ((row & 15u) == 0)
        {
            const auto current = work++;
            if (observer.checkpoint)
                observer.checkpoint(observer.context, current);
            if (cancellation.IsCancelled())
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        const std::size_t rowOffset = static_cast<std::size_t>(row) * stride;
        std::uint8_t* outputRow = destination.data() + static_cast<std::size_t>(row) * grid.width;
        for (std::uint32_t column = 0; column < grid.width; ++column)
        {
            const auto input = rowOffset + static_cast<std::size_t>(column) * 4u + 3u;
            // Validate the final address in the original WIC allocation, not an interior pointer with
            // a lost extent. The preceding row-layout checks and this check agree on every valid bitmap.
            if (input >= available)
                return E_UNEXPECTED;
            outputRow[column] = pixels[input];
        }
    }
    // Drop the WIC lock before the next plane's draw reuses this same bitmap.
    lock.reset();
    return S_OK;
}
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PreparedSelectionOutlineRaster::Storage final
{
    SelectionLoops source;
    GridKey grid;
    std::unique_ptr<std::uint8_t[]> pixels;
    std::size_t planeBytes = 0;
    std::uint64_t outputBytes = 0;
};
#pragma warning(pop)

std::int32_t PreparedSelectionOutlineRaster::PlanMemory(const GridKey& grid, std::uint64_t loopCount,
                                                        std::uint64_t pointCount, MemoryPlan& output) noexcept
{
    std::uint64_t pixels = 0;
    if (!ValidGrid(grid, pixels) || loopCount > pointCount || (pointCount != 0 && loopCount == 0))
        return E_INVALIDARG;

    MemoryPlan plan{};
    plan.opaqueD2DAllowanceBytes = kOpaqueD2DAllowanceBytes;
    plan.outputBytes = AccountedPublicationObjectAllowance<Storage>();
    std::uint64_t planes = 0;
    if (!MultiplyBytes(pixels, PlaneCount, planes) || !AddBytes(planes, plan.outputBytes) ||
        !AddBytes(kPlaneAllocationAllowanceBytes, plan.outputBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    // Temporary declaration includes one reusable 32bpp PBGRA WIC bitmap, approximate point/loop
    // geometry storage, bounded worker/factory objects and the explicit opaque D2D allowance.
    std::uint64_t bitmapBytes = 0;
    std::uint64_t geometryBytes = 0;
    std::uint64_t loopBytes = 0;
    if (!MultiplyBytes(pixels, 4u, bitmapBytes) || !MultiplyBytes(pointCount, 32u, geometryBytes) ||
        !MultiplyBytes(loopCount, 256u, loopBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    plan.temporaryBytes = kFixedWorkerAllowanceBytes;
    if (!AddBytes(bitmapBytes, plan.temporaryBytes) || !AddBytes(geometryBytes, plan.temporaryBytes) ||
        !AddBytes(loopBytes, plan.temporaryBytes) || !AddBytes(kOpaqueD2DAllowanceBytes, plan.temporaryBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    plan.totalBytes = plan.outputBytes;
    if (!AddBytes(plan.temporaryBytes, plan.totalBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    output = plan;
    return S_OK;
}

std::int32_t PreparedSelectionOutlineRaster::Prepare(SelectionLoops source, GridKey grid,
                                                     std::shared_ptr<const SharedPublicationMemory> outputMemory,
                                                     CancelToken cancellation, Observer observer,
                                                     PreparedSelectionOutlineRaster& output)
{
    if (outputMemory.use_count() == 0)
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    MemoryPlan plan{};
    HRESULT result = PlanMemory(grid, source.size(), source.PointCount(), plan);
    if (FAILED(result))
        return result;
    if (source.empty() || source.PointCount() == 0)
        return E_INVALIDARG;

    Vec2 anchor{};
    const auto bounds = source.Bounds();
    if (!bounds)
        return E_INVALIDARG;
    anchor = {bounds->minX, bounds->minY};
    float physicalScale = 0.0f;
    if (!ToFloat(grid.documentScalePixel, physicalScale) || physicalScale <= 0.0f || grid.dpi / 96.0f <= 0.0f)
        return E_INVALIDARG;
    float originX = 0.0f;
    float originY = 0.0f;
    if (!ToFloat(grid.originPixelX, originX) || !ToFloat(grid.originPixelY, originY))
        return E_INVALIDARG;

    // Refuse all non-finite/out-of-float geometry before output or COM allocations.
    std::uint64_t checkedPoints = 0;
    for (const auto& loop : source)
    {
        for (const auto& point : loop)
        {
            if ((checkedPoints++ & 63u) == 0)
            {
                if (observer.checkpoint)
                    observer.checkpoint(observer.context, checkedPoints - 1);
                if (cancellation.IsCancelled())
                    return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            float x = 0.0f;
            float y = 0.0f;
            if (!ToFloat(point.x - anchor.x, x) || !ToFloat(point.y - anchor.y, y))
                return E_INVALIDARG;
        }
    }

    // Include the complete new buffer in the local charge before pixel allocation. Source keeps its older owner.
    // The caller's immutable local limit and sealed state additionally guard this original publication.
    const auto planeBytes = static_cast<std::uint64_t>(grid.width) * grid.height * PlaneCount;
    auto storage = MakeAccountedPublicationObject<Storage>(outputMemory, plan.outputBytes,
                                                           planeBytes + kPlaneAllocationAllowanceBytes);
    storage->source = source;
    storage->grid = grid;
    storage->planeBytes = static_cast<std::size_t>(grid.width) * grid.height;
    storage->pixels = std::make_unique_for_overwrite<std::uint8_t[]>(storage->planeBytes * PlaneCount);

    D2D1_FACTORY_OPTIONS options{};
    wil::com_ptr_nothrow<ID2D1Factory1> d2dFactory;
    result =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, d2dFactory.put_void());
    if (FAILED(result))
        return result;
    wil::com_ptr_nothrow<ID2D1PathGeometry> geometry;
    std::uint64_t work = checkedPoints;
    result = MakePath(d2dFactory.get(), source, anchor, cancellation, observer, work, geometry);
    if (FAILED(result))
        return result;

    wil::com_ptr_nothrow<IWICImagingFactory2> imaging;
    result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2),
                              imaging.put_void());
    if (FAILED(result))
        return result;
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    result = imaging->CreateBitmap(grid.width, grid.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                   bitmap.put());
    if (FAILED(result))
        return result;
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    wil::com_ptr_nothrow<ID2D1RenderTarget> target;
    result = d2dFactory->CreateWicBitmapRenderTarget(bitmap.get(), properties, target.put());
    if (FAILED(result))
        return result;
    target->SetTransform(D2D1::Matrix3x2F::Scale(physicalScale, physicalScale) *
                         D2D1::Matrix3x2F::Translation(originX, originY));
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> white;
    result = target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), white.put());
    if (FAILED(result))
        return result;

    for (std::size_t phase = 0; phase < PlaneCount; ++phase)
    {
        if (cancellation.IsCancelled())
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        result = RasterizePlane(
            d2dFactory.get(), bitmap.get(), target.get(), white.get(), geometry.get(), phase, grid,
            std::span<std::uint8_t>{storage->pixels.get() + phase * storage->planeBytes, storage->planeBytes},
            cancellation, observer, work);
        if (FAILED(result))
            return result;
    }
    if (cancellation.IsCancelled())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    storage->outputBytes = plan.outputBytes;
    PreparedSelectionOutlineRaster completed;
    completed._storage = std::move(storage);
    output = std::move(completed);
    return S_OK;
}

const PreparedSelectionOutlineRaster::GridKey& PreparedSelectionOutlineRaster::Key() const noexcept
{
    static const GridKey empty{};
    return _storage ? _storage->grid : empty;
}

const void* PreparedSelectionOutlineRaster::SourceIdentity() const noexcept
{
    return _storage ? _storage->source.Identity() : nullptr;
}

const SelectionLoops& PreparedSelectionOutlineRaster::Source() const noexcept
{
    static const SelectionLoops empty;
    return _storage ? _storage->source : empty;
}

std::span<const std::uint8_t> PreparedSelectionOutlineRaster::Plane(std::size_t index) const noexcept
{
    return _storage && index < PlaneCount
               ? std::span<const std::uint8_t>{_storage->pixels.get() + index * _storage->planeBytes,
                                               _storage->planeBytes}
               : std::span<const std::uint8_t>{};
}

std::uint64_t PreparedSelectionOutlineRaster::DeclaredOutputBytes() const noexcept
{
    return _storage ? _storage->outputBytes : 0;
}

std::weak_ptr<const void> PreparedSelectionOutlineRaster::WeakStorageForTest() const noexcept
{
    return _storage;
}

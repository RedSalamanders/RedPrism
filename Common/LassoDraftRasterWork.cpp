#include "LassoDraftRasterWork.h"

#include "LocallyOwnedPublication.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d2d1_1.h>
#include <limits>
#include <optional>
#include <utility>
#include <wincodec.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
constexpr std::uint64_t kOpaqueAllowance = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kFixedAllowance = 256ull * 1024ull;
constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
[[nodiscard]] bool Add(std::uint64_t value, std::uint64_t& total) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}
[[nodiscard]] bool ToPoint(Vec2 point, Vec2 anchor, const PreparedLassoDraftRaster::GridKey& grid,
                           D2D1_POINT_2F& output) noexcept
{
    const double x = grid.originPixelX + (point.x - anchor.x) * grid.documentScalePixel;
    const double y = grid.originPixelY + (point.y - anchor.y) * grid.documentScalePixel;
    constexpr double maximum = (std::numeric_limits<float>::max)();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > maximum || std::abs(y) > maximum)
        return false;
    output = D2D1::Point2F(static_cast<float>(x), static_cast<float>(y));
    return true;
}
} // namespace
struct PreparedLassoDraftRaster::Storage final
{
    BoundedLassoGesture::Display source;
    GridKey grid;
    std::unique_ptr<std::uint8_t[]> pixels;
    std::size_t bytes = 0;
    std::uint64_t outputBytes = 0;
};
std::int32_t PreparedLassoDraftRaster::PlanMemory(const BoundedLassoGesture::Display& source, const GridKey& grid,
                                                  MemoryPlan& output) noexcept
{
    PreparedSelectionOutlineRaster::MemoryPlan validation;
    const auto valid = PreparedSelectionOutlineRaster::PlanMemory(grid, 0, 0, validation);
    if (FAILED(valid))
        return valid;
    if (!source.key.opening || !source.key.revision || !source.points.Count() || !source.points.Bounds() ||
        source.kind > LassoKind::Ellipse || source.mode > SelectionMode::Subtract)
        return E_INVALIDARG;
    MemoryPlan plan;
    const auto bytes = static_cast<std::uint64_t>(grid.width) * grid.height * 4;
    plan.outputBytes = LocallyOwnedPublicationAllowance<Storage>();
    if (!Add(bytes + 64, plan.outputBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    plan.opaqueD2DAllowanceBytes = kOpaqueAllowance;
    plan.temporaryBytes = kFixedAllowance;
    if (!Add(bytes, plan.temporaryBytes) || !Add(source.points.TraversalScratchBytes(), plan.temporaryBytes) ||
        !Add(kOpaqueAllowance, plan.temporaryBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    plan.totalBytes = plan.outputBytes;
    if (!Add(plan.temporaryBytes, plan.totalBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    output = plan;
    return S_OK;
}
std::int32_t PreparedLassoDraftRaster::Prepare(BoundedLassoGesture::Display source, GridKey grid,
                                               std::shared_ptr<const SharedPublicationMemory> outputMemory,
                                               CancelToken cancellation, Observer observer,
                                               PreparedLassoDraftRaster& output)
{
    if (!outputMemory)
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    MemoryPlan plan;
    HRESULT result = PlanMemory(source, grid, plan);
    if (FAILED(result))
        return result;
    const auto bounds = source.points.Bounds().value();
    const Vec2 anchor{bounds.minX, bounds.minY};
    std::uint64_t work = 0;
    const auto checkpoint = [&]() noexcept
    {
        if (observer.checkpoint)
            observer.checkpoint(observer.context, work++);
        return !cancellation.IsCancelled();
    };
    D2D1_POINT_2F first{};
    bool haveFirst = false;
    const bool validPoints = source.points.VisitForWork(
        [&](std::span<const Vec2> points)
        {
            if (!checkpoint())
                return false;
            for (const auto point : points)
            {
                D2D1_POINT_2F mapped{};
                if (!ToPoint(point, anchor, grid, mapped))
                {
                    result = E_INVALIDARG;
                    return false;
                }
                if (!haveFirst)
                {
                    first = mapped;
                    haveFirst = true;
                }
            }
            return true;
        },
        cancellation);
    D2D1_POINT_2F cursor{};
    if (!validPoints || !haveFirst || (source.cursor && !ToPoint(source.cursor.value(), anchor, grid, cursor)))
        return cancellation.IsCancelled() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : E_INVALIDARG;
    const auto bytes = static_cast<std::size_t>(grid.width) * grid.height * 4;
    std::uint64_t retirementBytes = plan.outputBytes;
    if (!Add(source.points.RetainedArenaBytes(), retirementBytes))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    auto storage = MakeLocallyOwnedPublicationObject<Storage>(outputMemory, retirementBytes, bytes + 64);
    storage->source = source;
    storage->grid = grid;
    storage->bytes = bytes;
    storage->pixels = std::make_unique_for_overwrite<std::uint8_t[]>(bytes);
    D2D1_FACTORY_OPTIONS options{};
    wil::com_ptr_nothrow<ID2D1Factory1> factory;
    result =
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, factory.put_void());
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
    wil::com_ptr_nothrow<ID2D1RenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    result = factory->CreateWicBitmapRenderTarget(bitmap.get(), properties, target.put());
    if (FAILED(result))
        return result;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> shadow;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> white;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> black;
    result = target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f), shadow.put());
    if (SUCCEEDED(result))
        result = target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), white.put());
    if (SUCCEEDED(result))
        result = target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), black.put());
    if (FAILED(result))
        return result;
    bool drawing = true;
    target->BeginDraw();
    auto endOnRefusal = wil::scope_exit(
        [&]() noexcept
        {
            if (drawing)
            {
                (void)target->EndDraw();
            }
        });
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    // View clipping applies once to this completed premultiplied color in the composition. Clipping
    // shadow, white and handle commands separately would change fractional edge colors.
    const float dipScale = grid.dpi / 96.0f;
    const bool marquee =
        (source.kind == LassoKind::Rectangle || source.kind == LassoKind::Ellipse) && source.points.Count() == 4;
    for (unsigned int pass = 0; pass < 2; ++pass)
    {
        if (!checkpoint())
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        auto* brush = pass == 0 ? shadow.get() : white.get();
        const float width = (pass == 0 ? 2.0f : 1.0f) * dipScale;
        if (marquee)
        {
            D2D1_POINT_2F minimum{}, maximum{};
            if (!ToPoint({bounds.minX, bounds.minY}, anchor, grid, minimum) ||
                !ToPoint({bounds.maxX, bounds.maxY}, anchor, grid, maximum))
                return E_INVALIDARG;
            const auto box = D2D1::RectF(minimum.x, minimum.y, maximum.x, maximum.y);
            if (source.kind == LassoKind::Ellipse)
                target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((box.left + box.right) / 2, (box.top + box.bottom) / 2),
                                                  (box.right - box.left) / 2, (box.bottom - box.top) / 2),
                                    brush, width);
            else
                target->DrawRectangle(box, brush, width);
        }
        else
        {
            std::optional<D2D1_POINT_2F> previous;
            const bool drawn = source.points.VisitForWork(
                [&](std::span<const Vec2> points)
                {
                    if (!checkpoint())
                        return false;
                    for (const auto point : points)
                    {
                        D2D1_POINT_2F mapped{};
                        if (!ToPoint(point, anchor, grid, mapped))
                        {
                            result = E_INVALIDARG;
                            return false;
                        }
                        if (previous)
                            target->DrawLine(previous.value(), mapped, brush, width);
                        previous = mapped;
                    }
                    // Bound queued line commands and check cancellation at every immutable chunk.
                    // This does not manufacture a bound for opaque software-renderer allocation or Flush latency.
                    result = target->Flush();
                    return SUCCEEDED(result) && checkpoint();
                },
                cancellation);
            if (!drawn)
                return cancellation.IsCancelled() ? HRESULT_FROM_WIN32(ERROR_CANCELLED)
                       : FAILED(result)           ? result
                                                  : E_FAIL;
            if (source.kind == LassoKind::Polygonal && source.cursor && previous)
                target->DrawLine(previous.value(), cursor, brush, width);
        }
        result = target->Flush();
        if (FAILED(result))
            return result;
    }
    if (source.kind == LassoKind::Polygonal)
    {
        const auto handle =
            D2D1::RectF(first.x - 4 * dipScale, first.y - 4 * dipScale, first.x + 4 * dipScale, first.y + 4 * dipScale);
        target->FillRectangle(handle, white.get());
        target->DrawRectangle(handle, black.get(), dipScale);
    }
    result = target->EndDraw();
    drawing = false;
    if (FAILED(result))
        return result;
    if (!checkpoint())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    wil::com_ptr_nothrow<IWICBitmapLock> lock;
    WICRect rect{0, 0, static_cast<int>(grid.width), static_cast<int>(grid.height)};
    result = bitmap->Lock(&rect, WICBitmapLockRead, lock.put());
    if (FAILED(result))
        return result;
    UINT stride = 0, available = 0;
    BYTE* data = nullptr;
    result = lock->GetStride(&stride);
    if (SUCCEEDED(result))
        result = lock->GetDataPointer(&available, &data);
    const auto rowBytes = static_cast<std::uint64_t>(grid.width) * 4;
    const auto lastEnd = static_cast<std::uint64_t>(grid.height - 1) * stride + rowBytes;
    if (FAILED(result))
        return result;
    if (!data || stride < rowBytes || lastEnd > available)
        return E_UNEXPECTED;
    for (std::uint32_t row = 0; row < grid.height; ++row)
    {
        if ((row & 15u) == 0 && !checkpoint())
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        const auto inputOffset = static_cast<std::uint64_t>(row) * stride;
        if (inputOffset > available || rowBytes > available - inputOffset)
            return E_UNEXPECTED;
        std::memcpy(storage->pixels.get() + static_cast<std::size_t>(row) * grid.width * 4,
                    data + static_cast<std::size_t>(inputOffset), static_cast<std::size_t>(rowBytes));
    }
    if (!checkpoint())
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    storage->outputBytes = plan.outputBytes;
    PreparedLassoDraftRaster prepared;
    prepared._storage = std::move(storage);
    output = std::move(prepared);
    return S_OK;
}
const BoundedLassoGesture::Display* PreparedLassoDraftRaster::Source() const noexcept
{
    return _storage ? &_storage->source : nullptr;
}
const PreparedLassoDraftRaster::GridKey& PreparedLassoDraftRaster::Key() const noexcept
{
    static const GridKey empty{};
    return _storage ? _storage->grid : empty;
}
std::span<const std::uint8_t> PreparedLassoDraftRaster::Pixels() const noexcept
{
    return _storage ? std::span<const std::uint8_t>{_storage->pixels.get(), _storage->bytes}
                    : std::span<const std::uint8_t>{};
}
std::uint64_t PreparedLassoDraftRaster::DeclaredOutputBytes() const noexcept
{
    return _storage ? _storage->outputBytes : 0;
}
std::weak_ptr<const void> PreparedLassoDraftRaster::WeakStorageForTest() const noexcept
{
    return _storage;
}

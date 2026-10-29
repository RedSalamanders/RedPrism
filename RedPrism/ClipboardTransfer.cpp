#include "ClipboardTransfer.h"
#include "ClipboardEncodingBudget.h"
#include "CodecMemoryStream.h"

#include "ClipboardDibWork.h"
#include "ImageCodecs.h"

#include <Windows.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace ClipboardTransfer
{
namespace
{
void Reached(Observer observer, Stage stage) noexcept
{
    if (observer.reached)
        observer.reached(observer.context, stage);
}

void NativeFailure(Observer observer, NativeCall call, UINT format, DWORD error) noexcept
{
    if (observer.nativeFailure)
        observer.nativeFailure(observer.context, call, format, error);
}

DWORD Injected(Observer observer, Fault stage) noexcept
{
    return observer.fault ? observer.fault(observer.context, stage) : 0;
}

DWORD Error() noexcept
{
    const DWORD error = GetLastError();
    return error ? error : ERROR_GEN_FAILURE;
}

UINT PngFormat() noexcept
{
    return RegisterClipboardFormatW(L"PNG");
}

UINT ReceiptFormat() noexcept
{
    return RegisterClipboardFormatW(L"RedPrism.ClipboardReceipt.v1");
}

Status HashStatus(std::int32_t error) noexcept
{
    return error == 0               ? Status::Ok
           : error == E_ABORT       ? Status::Cancelled
           : error == E_OUTOFMEMORY ? Status::OutOfMemory
                                    : Status::NativeFailure;
}

std::optional<ClipboardReceipt::Key> ReadReceipt() noexcept
{
    const auto format = ReceiptFormat();
    if (!format || !IsClipboardFormatAvailable(format))
        return std::nullopt;
    const auto handle = GetClipboardData(format);
    if (!handle || GlobalSize(handle) != ClipboardReceipt::kEncodedBytes)
        return std::nullopt;
    const wil::unique_hglobal_locked locked(handle);
    ClipboardReceipt::Key key;
    return locked.get() && ClipboardReceipt::Decode(
                               {static_cast<const std::uint8_t*>(locked.get()), ClipboardReceipt::kEncodedBytes}, key)
               ? std::optional<ClipboardReceipt::Key>{key}
               : std::nullopt;
}

wil::unique_hwnd ReadWindow(Observer observer) noexcept
{
    if (Injected(observer, Fault::Window) != 0)
        return {};
    wil::unique_hwnd window(
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!window)
        NativeFailure(observer, NativeCall::Window, 0, GetLastError());
    else
        Reached(observer, Stage::WindowCreated);
    return window;
}

class OpenScope final
{
  public:
    OpenScope() noexcept = default;
    ~OpenScope()
    {
        if (_open)
            (void)CloseClipboard();
    }
    OpenScope(const OpenScope&) = delete;
    OpenScope& operator=(const OpenScope&) = delete;
    bool Open(HWND window) noexcept
    {
        _open = OpenClipboard(window) != FALSE;
        return _open;
    }
    bool Close() noexcept
    {
        if (!_open)
            return false;
        if (!CloseClipboard())
            return false;
        _open = false;
        return true;
    }

  private:
    bool _open = false;
};

Status GlobalCopy(std::span<const std::uint8_t> bytes, wil::unique_hglobal& output, CancelToken cancellation) noexcept
{
    wil::unique_hglobal local(GlobalAlloc(GMEM_MOVEABLE, bytes.size()));
    if (!local)
        return Status::OutOfMemory;
    {
        const wil::unique_hglobal_locked locked(local.get());
        if (!locked.get())
            return Status::NativeFailure;
        auto* destination = static_cast<std::uint8_t*>(locked.get());
        const auto allocated = GlobalSize(local.get());
        if (allocated < bytes.size())
            return Status::NativeFailure;
        if (allocated > kMaxImageEncodedBytes || allocated - bytes.size() > 64 * 1024)
            return Status::TooLarge;
        for (std::size_t offset = 0; offset < bytes.size();)
        {
            if (cancellation.IsCancelled())
                return Status::Cancelled;
            const std::size_t count = (std::min)(std::size_t{64 * 1024}, bytes.size() - offset);
            std::memcpy(destination + offset, bytes.data() + offset, count);
            offset += count;
        }
        // Windows may expose a larger block than requested. Never publish uninitialized trailing bytes.
        for (std::size_t offset = bytes.size(); offset < allocated;)
        {
            if (cancellation.IsCancelled())
                return Status::Cancelled;
            const auto count = (std::min)(std::size_t{64 * 1024}, allocated - offset);
            std::memset(destination + offset, 0, count);
            offset += count;
        }
    }
    output = std::move(local);
    return Status::Ok;
}

Status HashGlobal(HGLOBAL block, ClipboardFingerprint::Format& output, CancelToken cancellation) noexcept
{
    const auto size = GlobalSize(block);
    const wil::unique_hglobal_locked locked(block);
    return size && locked.get() ? HashStatus(ClipboardFingerprint::Hash(
                                      {static_cast<const std::uint8_t*>(locked.get()), size}, output, cancellation))
                                : Status::NativeFailure;
}

Status CodecStatus(ImageCodecStatus status) noexcept
{
    if (status == ImageCodecStatus::Ok)
        return Status::Ok;
    if (status == ImageCodecStatus::Cancelled)
        return Status::Cancelled;
    if (status == ImageCodecStatus::TooLarge)
        return Status::TooLarge;
    return Status::Invalid;
}

Status DibStatus(ClipboardDibStatus status) noexcept
{
    if (status == ClipboardDibStatus::Ok)
        return Status::Ok;
    if (status == ClipboardDibStatus::Cancelled)
        return Status::Cancelled;
    if (status == ClipboardDibStatus::TooLarge)
        return Status::TooLarge;
    if (status == ClipboardDibStatus::OutOfMemory)
        return Status::OutOfMemory;
    return Status::Invalid;
}

Status ProbeBytes(std::span<const std::uint8_t> bytes, bool png, std::int64_t pixelBudget, FormatInfo& info,
                  CancelToken cancellation)
{
    info = {};
    if (png)
    {
        ImageImportMetadata metadata;
        const Status status =
            CodecStatus(ProbeProjectPng(bytes.data(), bytes.size(), pixelBudget, metadata, cancellation));
        if (status == Status::Ok)
            info = {bytes.size(), metadata.pixels, metadata.rawWidth, metadata.rawHeight};
        return status;
    }
    ClipboardDibMetadata metadata;
    const Status status = DibStatus(ProbeClipboardDib(bytes, pixelBudget, metadata, cancellation));
    if (status == Status::Ok)
        info = {bytes.size(), metadata.pixels, metadata.width, metadata.height};
    return status;
}

// ClipboardScope has already opened. GetClipboardData may block a worker while an external owner renders.
// The OS handle is borrowed; only the locked pointer's scope owns an unlock, never GlobalFree.
Status InspectFormat(UINT format, bool png, std::int64_t pixelBudget, FormatInfo& info, CancelToken cancellation,
                     Observer observer)
{
    info = {};
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    const HANDLE handle = GetClipboardData(format);
    if (!handle)
    {
        NativeFailure(observer, NativeCall::Read, format, GetLastError());
        return Status::NativeFailure;
    }
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    const SIZE_T size = GlobalSize(handle);
    if (size == 0)
    {
        NativeFailure(observer, NativeCall::Size, format, GetLastError());
        return Status::Invalid;
    }
    if (size > kMaxImageEncodedBytes)
        return Status::TooLarge;
    const wil::unique_hglobal_locked locked(handle);
    if (!locked.get())
    {
        NativeFailure(observer, NativeCall::Lock, format, GetLastError());
        return Status::NativeFailure;
    }
    return ProbeBytes({static_cast<const std::uint8_t*>(locked.get()), size}, png, pixelBudget, info, cancellation);
}

Status CopyFormat(UINT format, bool png, std::int64_t pixelBudget, const FormatInfo& inspected,
                  std::vector<std::uint8_t>& output, CancelToken cancellation, Observer observer)
{
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    const HANDLE handle = GetClipboardData(format);
    if (!handle)
    {
        NativeFailure(observer, NativeCall::Read, format, GetLastError());
        return Status::NativeFailure;
    }
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    const SIZE_T size = GlobalSize(handle);
    if (size == 0)
    {
        NativeFailure(observer, NativeCall::Size, format, GetLastError());
        return Status::Stale;
    }
    if (size != inspected.encodedBytes || size > kMaxImageEncodedBytes)
        return Status::Stale;
    const wil::unique_hglobal_locked locked(handle);
    if (!locked.get())
    {
        NativeFailure(observer, NativeCall::Lock, format, GetLastError());
        return Status::NativeFailure;
    }
    const auto* source = static_cast<const std::uint8_t*>(locked.get());
    FormatInfo current;
    const Status probed = ProbeBytes({source, size}, png, pixelBudget, current, cancellation);
    if (probed == Status::Cancelled)
        return probed;
    if (probed != Status::Ok || current != inspected)
        return Status::Stale;
    output.resize(size);
    for (std::size_t offset = 0; offset < size;)
    {
        if (cancellation.IsCancelled())
            return Status::Cancelled;
        const std::size_t count = (std::min)(std::size_t{64 * 1024}, size - offset);
        std::memcpy(output.data() + offset, source + offset, count);
        offset += count;
    }
    return Status::Ok;
}
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PreparedImage::Storage final
{
    wil::unique_hglobal png;
    wil::unique_hglobal dib;
    UINT pngFormat = 0;
    std::uint32_t sourceSequence = 0;
    wil::unique_hglobal receipt;
    UINT receiptFormat = 0;
    std::optional<ClipboardFingerprint::Binding> binding;
};
#pragma warning(pop)

static_assert(sizeof(CodecMemoryStream) <= 256);

PreparedImage::PreparedImage() noexcept = default;
PreparedImage::~PreparedImage() = default;
PreparedImage::PreparedImage(PreparedImage&&) noexcept = default;
PreparedImage& PreparedImage::operator=(PreparedImage&&) noexcept = default;
PreparedImage::operator bool() const noexcept
{
    return _storage && _storage->png && _storage->dib && _storage->pngFormat != 0 &&
           (!_storage->binding || (_storage->receipt && _storage->receiptFormat));
}

std::uint64_t PreparedImage::RetainedBytes() const noexcept
{
    return _storage ? sizeof(Storage) + GlobalSize(_storage->png.get()) + GlobalSize(_storage->dib.get()) +
                          (_storage->receipt ? GlobalSize(_storage->receipt.get()) : 0)
                    : 0;
}

std::optional<ClipboardFingerprint::Binding> PreparedImage::ContentBinding() const noexcept
{
    return _storage ? _storage->binding : std::nullopt;
}

std::uint32_t PreparedImage::SourceSequence() const noexcept
{
    return _storage ? _storage->sourceSequence : 0;
}

Status Prepare(const Raster& image, double resolution, std::uint32_t sourceSequence, PreparedImage& output,
               CancelToken cancellation, Observer observer, const std::optional<ClipboardReceipt::Key>& receipt)
{
    static_assert(sizeof(PreparedImage::Storage) <= 256);
    static_assert(kClipboardEncodingFixedMetadataBytes >= sizeof(PreparedImage::Storage) + sizeof(CodecMemoryStream) +
                                                              4 * sizeof(std::vector<std::uint8_t>) + 128 +
                                                              16 * sizeof(void*));
    output = {};
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888)
        return Status::Invalid;
    if (image.PixelCount() > kMaxLayerPixels)
        return Status::TooLarge;
    PreparedImage prepared;
    prepared._storage = std::make_unique<PreparedImage::Storage>();
    prepared._storage->pngFormat = PngFormat();
    prepared._storage->sourceSequence = sourceSequence;
    if (prepared._storage->pngFormat == 0)
        return Status::NativeFailure;
    if (receipt)
    {
        std::array<std::uint8_t, ClipboardReceipt::kEncodedBytes> packet{};
        if (!ClipboardReceipt::Encode(receipt.value(), packet))
            return Status::Invalid;
        prepared._storage->receiptFormat = ReceiptFormat();
        if (!prepared._storage->receiptFormat)
            return Status::NativeFailure;
        if (const auto error = Injected(observer, Fault::ReceiptGlobal); error != 0)
            return error == ERROR_NOT_ENOUGH_MEMORY ? Status::OutOfMemory : Status::NativeFailure;
        const auto status = GlobalCopy(packet, prepared._storage->receipt, cancellation);
        if (status != Status::Ok)
            return status;
        prepared._storage->binding.emplace();
        prepared._storage->binding->key = receipt.value();
    }
    {
        std::vector<std::uint8_t> png;
        Status status = CodecStatus(EncodePng(image, resolution, png, cancellation));
        if (status != Status::Ok)
            return status;
        if (const DWORD error = Injected(observer, Fault::PngGlobal); error != 0)
            return error == ERROR_NOT_ENOUGH_MEMORY ? Status::OutOfMemory : Status::NativeFailure;
        status = GlobalCopy(png, prepared._storage->png, cancellation);
        if (status != Status::Ok)
            return status;
        if (prepared._storage->binding)
        {
            status = HashGlobal(prepared._storage->png.get(), prepared._storage->binding->png, cancellation);
            if (status != Status::Ok)
                return status;
        }
    }
    {
        std::vector<std::uint8_t> dib;
        Status status = DibStatus(EncodeClipboardDibV5(image, dib, cancellation));
        if (status != Status::Ok)
            return status;
        if (const DWORD error = Injected(observer, Fault::DibGlobal); error != 0)
            return error == ERROR_NOT_ENOUGH_MEMORY ? Status::OutOfMemory : Status::NativeFailure;
        status = GlobalCopy(dib, prepared._storage->dib, cancellation);
        if (status != Status::Ok)
            return status;
        if (prepared._storage->binding)
        {
            status = HashGlobal(prepared._storage->dib.get(), prepared._storage->binding->dib, cancellation);
            if (status != Status::Ok)
                return status;
        }
    }
    Reached(observer, Stage::GlobalsPrepared);
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    output = std::move(prepared);
    return Status::Ok;
}

Publication Publish(PreparedImage& image, CancelToken cancellation, Observer observer) noexcept
{
    Publication result;
    if (!image)
        return result;
    if (cancellation.IsCancelled())
    {
        result.status = Status::Cancelled;
        return result;
    }
    if (const DWORD error = Injected(observer, Fault::Window); error != 0)
    {
        result.status = Status::NativeFailure;
        result.nativeError = error;
        return result;
    }
    // Built-in window procedure; no Application pointer, subclass, visible surface or borrowed HWND.
    wil::unique_hwnd window(
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!window)
    {
        result.status = Status::NativeFailure;
        result.nativeError = Error();
        return result;
    }
    Reached(observer, Stage::WindowCreated);
    if (cancellation.IsCancelled())
    {
        result.status = Status::Cancelled;
        return result;
    }
    OpenScope scope;
    if (const DWORD error = Injected(observer, Fault::Open); error != 0)
    {
        result.status = Status::Busy;
        result.nativeError = error;
        return result;
    }
    if (!scope.Open(window.get()))
    {
        result.status = Status::Busy;
        result.nativeError = Error();
        return result;
    }
    Reached(observer, Stage::Opened);
    result.sequence = GetClipboardSequenceNumber();
    if (cancellation.IsCancelled())
    {
        result.status = Status::Cancelled;
        return result;
    }
    if (result.sequence != image._storage->sourceSequence)
    {
        result.status = Status::Stale;
        return result;
    }
    if (const DWORD error = Injected(observer, Fault::Empty); error != 0)
    {
        result.status = Status::NativeFailure;
        result.nativeError = error;
        return result;
    }
    if (!EmptyClipboard())
    {
        result.status = Status::NativeFailure;
        result.nativeError = Error();
        return result;
    }
    result.emptied = true;
    Reached(observer, Stage::Emptied);
    if (image._storage->binding)
    {
        const auto error = Injected(observer, Fault::ReceiptPlace);
        if (error || !SetClipboardData(image._storage->receiptFormat, image._storage->receipt.get()))
        {
            result.nativeError = error ? error : Error();
            result.status = Status::NativeFailure;
            // No unbound image formats escape when receipt placement fails. Close precedes owner destruction.
            result.sequence = GetClipboardSequenceNumber();
            (void)scope.Close();
            Reached(observer, Stage::Closed);
            image = {};
            return result;
        }
        (void)image._storage->receipt.release();
        result.receiptPlaced = true;
        Reached(observer, Stage::ReceiptPlaced);
    }
    // From Empty onward this attempt cannot be rolled back or retried. Complete both independent formats,
    // retain the first error, and report the exact held sequence even when a stop arrives after publication.
    DWORD error = Injected(observer, Fault::Png);
    if (!error && SetClipboardData(image._storage->pngFormat, image._storage->png.get()))
    {
        (void)image._storage->png.release();
        result.pngPlaced = true;
        Reached(observer, Stage::PngPlaced);
    }
    else
        result.nativeError = error ? error : Error();
    error = Injected(observer, Fault::Dib);
    if (!error && SetClipboardData(CF_DIBV5, image._storage->dib.get()))
    {
        (void)image._storage->dib.release();
        result.dibPlaced = true;
        Reached(observer, Stage::DibPlaced);
    }
    else if (result.nativeError == 0)
        result.nativeError = error ? error : Error();
    result.sequence = GetClipboardSequenceNumber();
    result.status = result.pngPlaced && result.dibPlaced ? Status::Ok : Status::NativeFailure;
    const bool closed = scope.Close();
    const DWORD closeError = closed ? Injected(observer, Fault::Close) : Error();
    if (closeError && result.nativeError == 0)
    {
        result.nativeError = closeError;
        result.status = Status::NativeFailure;
    }
    Reached(observer, Stage::Closed);
    // Scope closes before the immediate-format window is destroyed. Transferred global blocks belong to
    // Windows, and remaining blocks are still private. Tests must prove contents survive window destruction.
    image = {};
    return result;
}

Inspection::Inspection(Inspection&& other) noexcept
{
    *this = std::move(other);
}
Inspection& Inspection::operator=(Inspection&& other) noexcept
{
    if (this != &other)
    {
        _valid = std::exchange(other._valid, false);
        _sequence = other._sequence;
        _pngFormat = other._pngFormat;
        _dibFormat = other._dibFormat;
        _png = other._png;
        _dib = other._dib;
        _pixelBudget = other._pixelBudget;
    }
    return *this;
}

std::uint64_t Inspection::CopyAndPixelsBytes() const noexcept
{
    return _valid ? _png.encodedBytes + _dib.encodedBytes + (std::max)(_png.pixels, _dib.pixels) * 4u : 0;
}

std::uint64_t ImageCopy::RetainedBytes() const noexcept
{
    return _png.capacity() + _dib.capacity();
}

Status Inspect(std::int64_t pixelBudget, Inspection& output, CancelToken cancellation, Observer observer,
               Availability* availability)
{
    output = {};
    if (availability)
        *availability = {};
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (pixelBudget < 0)
        return Status::TooLarge;
    // NULL scopes may both open successfully on different threads and one Close invalidates the other.
    // A distinct worker-owned HWND makes native acquisition exclusive, including concurrent hint/availability.
    const auto window = ReadWindow(observer);
    if (!window)
        return Status::NativeFailure;
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    OpenScope scope;
    if (Injected(observer, Fault::Open) != 0)
        return Status::Busy;
    if (!scope.Open(window.get()))
    {
        NativeFailure(observer, NativeCall::Open, 0, GetLastError());
        return Status::Busy;
    }
    Reached(observer, Stage::Opened);
    Inspection inspected;
    inspected._pixelBudget = pixelBudget;
    const UINT png = PngFormat();
    if (png == 0)
    {
        NativeFailure(observer, NativeCall::RegisterFormat, 0, GetLastError());
        return Status::NativeFailure;
    }
    const bool hasPng = IsClipboardFormatAvailable(png) != FALSE;
    const bool hasV5 = IsClipboardFormatAvailable(CF_DIBV5) != FALSE;
    const bool hasDib = IsClipboardFormatAvailable(CF_DIB) != FALSE;
    Status pngStatus = Status::Invalid;
    if (hasPng)
    {
        pngStatus = InspectFormat(png, true, pixelBudget, inspected._png, cancellation, observer);
        if (pngStatus == Status::Ok)
            inspected._pngFormat = png;
    }
    // The first enumerated DIB variant is the producer's representation. Windows enumerates synthesized
    // variants after it; requesting one can run native bitmap conversion before our size/header validation.
    // In particular, never request a conversion of an invalid or over-budget source as a fallback.
    UINT dib = 0;
    if (hasV5 || hasDib)
    {
        UINT format = 0;
        for (;;)
        {
            if (cancellation.IsCancelled())
                return Status::Cancelled;
            SetLastError(ERROR_SUCCESS);
            format = EnumClipboardFormats(format);
            if (!format)
            {
                const DWORD error = GetLastError();
                if (error != ERROR_SUCCESS)
                {
                    NativeFailure(observer, NativeCall::Enumerate, 0, error);
                    return Status::NativeFailure;
                }
                break;
            }
            if (format == CF_DIBV5 || format == CF_DIB)
            {
                dib = format;
                break;
            }
        }
    }
    Status dibStatus = Status::Invalid;
    if (dib)
    {
        Reached(observer, dib == CF_DIBV5 ? Stage::DibV5InspectRead : Stage::DibInspectRead);
        dibStatus = InspectFormat(dib, false, pixelBudget, inspected._dib, cancellation, observer);
        if (dibStatus == Status::Cancelled)
            return dibStatus;
        if (dibStatus == Status::Ok)
            inspected._dibFormat = dib;
    }
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    inspected._sequence = GetClipboardSequenceNumber();
    if (!scope.Close())
    {
        NativeFailure(observer, NativeCall::Close, 0, GetLastError());
        return Status::NativeFailure;
    }
    Reached(observer, Stage::Inspected);
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (availability)
        *availability = {inspected._sequence, hasPng || hasV5 || hasDib, true};
    if (inspected._pngFormat == 0 && inspected._dibFormat == 0)
        return pngStatus == Status::TooLarge || dibStatus == Status::TooLarge ? Status::TooLarge : Status::Invalid;
    inspected._valid = true;
    output = std::move(inspected);
    return Status::Ok;
}

Status Copy(const Inspection& inspected, ImageCopy& output, CancelToken cancellation, Observer observer)
{
    output = {};
    if (!inspected)
        return Status::Invalid;
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    const auto window = ReadWindow(observer);
    if (!window)
        return Status::NativeFailure;
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    OpenScope scope;
    if (Injected(observer, Fault::Open) != 0)
        return Status::Busy;
    if (!scope.Open(window.get()))
    {
        NativeFailure(observer, NativeCall::Open, 0, GetLastError());
        return Status::Busy;
    }
    Reached(observer, Stage::Opened);
    if (GetClipboardSequenceNumber() != inspected._sequence)
        return Status::Stale;
    ImageCopy copy;
    copy._pixelBudget = (std::min)(inspected._pixelBudget, static_cast<std::int64_t>(kMaxLayerPixels));
    if (Injected(observer, Fault::ReceiptRead) == 0)
        copy._receipt = ReadReceipt();
    Reached(observer, Stage::ReceiptRead);
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (GetClipboardSequenceNumber() != inspected._sequence)
        return Status::Stale;
    Status pngStatus = Status::Invalid;
    if (inspected._pngFormat != 0)
        pngStatus = Injected(observer, Fault::PngRead) != 0
                        ? Status::NativeFailure
                        : CopyFormat(inspected._pngFormat, true, inspected._pixelBudget, inspected._png, copy._png,
                                     cancellation, observer);
    if (pngStatus == Status::Stale || pngStatus == Status::Cancelled)
        return pngStatus;
    if (GetClipboardSequenceNumber() != inspected._sequence)
        return Status::Stale;
    Status dibStatus = Status::Invalid;
    if (inspected._dibFormat != 0)
        dibStatus = Injected(observer, Fault::DibRead) != 0
                        ? Status::NativeFailure
                        : CopyFormat(inspected._dibFormat, false, inspected._pixelBudget, inspected._dib, copy._dib,
                                     cancellation, observer);
    if (dibStatus == Status::Stale || dibStatus == Status::Cancelled)
        return dibStatus;
    if (pngStatus != Status::Ok && dibStatus != Status::Ok)
        return Status::NativeFailure;
    if (GetClipboardSequenceNumber() != inspected._sequence)
        return Status::Stale;
    copy._sequence = inspected._sequence;
    if (!scope.Close())
    {
        NativeFailure(observer, NativeCall::Close, 0, GetLastError());
        return Status::NativeFailure;
    }
    Reached(observer, Stage::Copied);
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    output = std::move(copy);
    return Status::Ok;
}

Status Decode(const ImageCopy& copy, Raster& output, CancelToken cancellation)
{
    output = {};
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (copy._pixelBudget < 0 || copy._pixelBudget > (std::numeric_limits<int>::max)())
        return Status::Invalid;
    if (!copy._png.empty())
    {
        DecodedImage decoded;
        const Status status = CodecStatus(DecodeProjectPng(copy._png.data(), copy._png.size(), false,
                                                           static_cast<int>(copy._pixelBudget), decoded, cancellation));
        if (status == Status::Ok)
        {
            output = std::move(decoded.image);
            return Status::Ok;
        }
        if (status == Status::Cancelled)
            return status;
    }
    if (copy._dib.empty())
        return Status::Invalid;
    return DibStatus(DecodeClipboardDib(copy._dib, copy._pixelBudget, output, cancellation));
}

Status HasPrivateOrigin(const ImageCopy& copy, const ClipboardFingerprint::Binding& binding,
                        const ClipboardReceipt::Key& currentPrivate, bool& matches, CancelToken cancellation)
{
    matches = false;
    if (cancellation.IsCancelled())
        return Status::Cancelled;
    if (!copy._receipt || copy._receipt.value() != binding.key || currentPrivate != binding.key)
        return Status::Ok;
    ClipboardFingerprint::Format png, dib;
    if (!copy._png.empty())
    {
        const auto status = HashStatus(ClipboardFingerprint::Hash(copy._png, png, cancellation));
        if (status != Status::Ok)
            return status;
    }
    if (!copy._dib.empty())
    {
        const auto status = HashStatus(ClipboardFingerprint::Hash(copy._dib, dib, cancellation));
        if (status != Status::Ok)
            return status;
    }
    matches = ClipboardFingerprint::Matches(binding, currentPrivate, copy._receipt, png, dib);
    return Status::Ok;
}
} // namespace ClipboardTransfer

#include "ImageCodecs.h"

#include "CodecFileStream.h"
#include "RasterWork.h"
#include "Utf8.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <span>
#include <string>
#include <utility>
#include <wincodec.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
constexpr int kThumbnailMaxSide = 96;
constexpr std::uint64_t kMaxEncodedBytes = 512ull * 1024ull * 1024ull;

[[nodiscard]] std::wstring_view ExtensionOf(std::wstring_view path) noexcept
{
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring_view name = slash == std::wstring_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot + 1 >= name.size())
    {
        return {};
    }
    return name.substr(dot);
}

[[nodiscard]] bool EqualsExtension(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (towlower(left[index]) != right[index])
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] HRESULT MakeFactory(wil::com_ptr_nothrow<IWICImagingFactory>& factory) noexcept
{
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
}

[[nodiscard]] bool SameGuid(const GUID& left, const GUID& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

[[nodiscard]] ImageCodecStatus MapDecodeError(HRESULT result, bool heifRequested) noexcept
{
    if (result == WINCODEC_ERR_COMPONENTNOTFOUND || result == WINCODEC_ERR_COMPONENTINITIALIZEFAILURE ||
        result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
    {
        return heifRequested ? ImageCodecStatus::HeifUnavailable : ImageCodecStatus::Unsupported;
    }
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    return ImageCodecStatus::Ok;
}

// The EXIF/TIFF Orientation tag (274), read from whichever metadata path this container exposes it under, else 1
// (identity). "System.Photo.Orientation" is the Windows Property System's container-agnostic mirror of the same tag.
[[nodiscard]] int ReadOrientation(IWICBitmapFrameDecode* frame) noexcept
{
    wil::com_ptr_nothrow<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(reader.put())) || !reader)
    {
        return 1;
    }
    static constexpr const wchar_t* kQueries[] = {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}",
                                                  L"System.Photo.Orientation"};
    for (const wchar_t* query : kQueries)
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(query, &value)))
        {
            int orientation = 0;
            switch (value.vt)
            {
            case VT_UI1:
                orientation = value.bVal;
                break;
            case VT_UI2:
                orientation = value.uiVal;
                break;
            case VT_UI4:
                orientation = static_cast<int>(value.ulVal);
                break;
            case VT_I4:
                orientation = value.lVal;
                break;
            default:
                break;
            }
            PropVariantClear(&value);
            if (orientation >= 1 && orientation <= 8)
            {
                return orientation;
            }
        }
        else
        {
            PropVariantClear(&value);
        }
    }
    return 1;
}

// EXIF orientation -> the flip/rotate that corrects it (ExifTool's table). Only 6 has a golden
// (ImageImportTests.orientationAndColorConversion: a 64x32 source at orientation 6 imports as 32x64); the others
// follow the same well-established mapping.
[[nodiscard]] WICBitmapTransformOptions TransformForOrientation(int orientation) noexcept
{
    switch (orientation)
    {
    case 2:
        return WICBitmapTransformFlipHorizontal;
    case 3:
        return WICBitmapTransformRotate180;
    case 4:
        return WICBitmapTransformFlipVertical;
    case 5:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformFlipHorizontal | WICBitmapTransformRotate270);
    case 6:
        return WICBitmapTransformRotate90;
    case 7:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformFlipHorizontal | WICBitmapTransformRotate90);
    case 8:
        return WICBitmapTransformRotate270;
    default:
        return WICBitmapTransformRotate0;
    }
}

// True when the frame embeds a real ICC profile (`sourceContext` set to it), so IWICColorTransform has profile
// bytes to convert from. The lightweight EXIF ColorSpace flag (WICColorContextExifColorSpace: sRGB or
// "uncalibrated") carries no profile data to transform from either way, so it is left alone rather than risking an
// IWICColorTransform call with nothing meaningful to source from; a source with no embedded profile is assumed
// sRGB, the industry-standard fallback.
[[nodiscard]] bool FindColorContextNeedingTransform(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame,
                                                    wil::com_ptr_nothrow<IWICColorContext>& sourceContext)
{
    UINT count = 0;
    if (FAILED(frame->GetColorContexts(0, nullptr, &count)) || count == 0)
    {
        return false;
    }
    std::vector<wil::com_ptr_nothrow<IWICColorContext>> contexts(count);
    std::vector<IWICColorContext*> raw(count);
    for (UINT index = 0; index < count; ++index)
    {
        if (FAILED(factory->CreateColorContext(contexts[index].put())))
        {
            return false;
        }
        raw[index] = contexts[index].get();
    }
    UINT actual = 0;
    if (FAILED(frame->GetColorContexts(count, raw.data(), &actual)))
    {
        return false;
    }
    for (UINT index = 0; index < actual; ++index)
    {
        WICColorContextType type{};
        if (SUCCEEDED(contexts[index]->GetType(&type)) && type == WICColorContextProfile)
        {
            sourceContext = contexts[index];
            return true;
        }
    }
    return false;
}

[[nodiscard]] ImageCodecStatus CopyFrame(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, bool mask,
                                         bool applyImportTransforms, int remainingPixels, DecodedImage& decoded,
                                         CancelToken cancellation)
{
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (mask)
    {
        // IO_ProjectFormat.md / legacy LayerMask.isValid: a mask must already be monochrome 8-bit with no alpha.
        // Converting a color source would silently desaturate it into a mask nobody asked for.
        WICPixelFormatGUID nativeFormat{};
        if (FAILED(frame->GetPixelFormat(&nativeFormat)) || !SameGuid(nativeFormat, GUID_WICPixelFormat8bppGray))
        {
            return ImageCodecStatus::Unreadable;
        }
    }

    IWICBitmapSource* source = frame;
    wil::com_ptr_nothrow<IWICBitmapFlipRotator> rotator;
    wil::com_ptr_nothrow<IWICFormatConverter> straightConverter;
    wil::com_ptr_nothrow<IWICColorContext> sourceContext;
    wil::com_ptr_nothrow<IWICColorContext> destContext;
    wil::com_ptr_nothrow<IWICColorTransform> colorTransform;
    if (applyImportTransforms)
    {
        // Import only: project assets are RedPrism's own straight-sRGB PNGs and never carry orientation or a
        // profile, so re-checking them on every load would cost steady-state work for nothing
        // (Core_PerformanceAndResources.md).
        const WICBitmapTransformOptions orientation = TransformForOrientation(ReadOrientation(frame));
        if (orientation != WICBitmapTransformRotate0)
        {
            if (FAILED(factory->CreateBitmapFlipRotator(rotator.put())) ||
                FAILED(rotator->Initialize(source, orientation)))
            {
                return ImageCodecStatus::Unreadable;
            }
            source = rotator.get();
        }
        if (!mask && FindColorContextNeedingTransform(factory, frame, sourceContext))
        {
            // Color-manage in straight alpha, then let the converter below premultiply once, at the end.
            if (FAILED(factory->CreateFormatConverter(straightConverter.put())) ||
                FAILED(straightConverter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                                     nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
                FAILED(factory->CreateColorContext(destContext.put())) ||
                FAILED(destContext->InitializeFromExifColorSpace(1)) || // 1 = sRGB.
                FAILED(factory->CreateColorTransformer(colorTransform.put())) ||
                FAILED(colorTransform->Initialize(straightConverter.get(), sourceContext.get(), destContext.get(),
                                                  GUID_WICPixelFormat32bppBGRA)))
            {
                return ImageCodecStatus::Unreadable;
            }
            source = colorTransform.get();
        }
    }

    UINT width = 0;
    UINT height = 0;
    HRESULT result = source->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0)
    {
        return ImageCodecStatus::Unreadable;
    }
    if (width > static_cast<UINT>(kMaxCanvasSide) || height > static_cast<UINT>(kMaxCanvasSide) ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
            static_cast<std::uint64_t>(remainingPixels))
    {
        return ImageCodecStatus::TooLarge;
    }

    const RasterFormat format = mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888;
    const GUID pixelFormat = mask ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppPBGRA;
    wil::com_ptr_nothrow<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(converter.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    result =
        converter->Initialize(source, pixelFormat, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }

    const UINT stride = mask ? width : width * 4;
    const std::size_t bytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    PixelBuffer pixels = PixelBuffer::TryAllocate(bytes);
    if (!pixels)
    {
        return ImageCodecStatus::Unreadable;
    }
    const UINT bandRows = (std::max)(1u, 64u * 1024u / stride);
    for (UINT y = 0; y < height;)
    {
        if (cancellation.IsCancelled())
        {
            return ImageCodecStatus::Cancelled;
        }
        const UINT rows = (std::min)(bandRows, height - y);
        const UINT bandBytes = stride * rows;
        WICRect rect{0, static_cast<INT>(y), static_cast<INT>(width), static_cast<INT>(rows)};
        result = converter->CopyPixels(&rect, stride, bandBytes, pixels.Data() + static_cast<std::size_t>(y) * stride);
        if (FAILED(result))
        {
            return ImageCodecStatus::Unreadable;
        }
        y += rows;
    }

    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    Raster image = Raster::Adopt(static_cast<int>(width), static_cast<int>(height), format, std::move(pixels));
    if (!image.HasStorage())
    {
        return ImageCodecStatus::Unreadable;
    }
    if (mask && !LayerMask::IsValid(image))
    {
        return ImageCodecStatus::Unreadable;
    }
    Raster thumbnail = ThumbnailRasterForWork(image, kThumbnailMaxSide, cancellation);
    if (cancellation.IsCancelled() || !thumbnail.HasStorage())
    {
        return cancellation.IsCancelled() ? ImageCodecStatus::Cancelled : ImageCodecStatus::Unreadable;
    }
    decoded.image = std::move(image);
    decoded.thumbnail = std::move(thumbnail);
    return ImageCodecStatus::Ok;
}

[[nodiscard]] ImageCodecStatus DecodeFromDecoder(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, bool mask,
                                                 bool requirePng, bool allowHeif, int remainingPixels,
                                                 DecodedImage& decoded, CancelToken cancellation)
{
    GUID container{};
    HRESULT result = decoder->GetContainerFormat(&container);
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    const bool png = SameGuid(container, GUID_ContainerFormatPng);
    const bool jpeg = SameGuid(container, GUID_ContainerFormatJpeg);
    const bool tiff = SameGuid(container, GUID_ContainerFormatTiff);
    const bool heif = SameGuid(container, GUID_ContainerFormatHeif);
    if (requirePng && !png)
    {
        return ImageCodecStatus::Unreadable;
    }
    if (!requirePng && !png && !jpeg && !tiff && !heif)
    {
        return ImageCodecStatus::Unsupported;
    }
    if (heif && !allowHeif)
    {
        return ImageCodecStatus::Unsupported;
    }

    UINT frames = 0;
    result = decoder->GetFrameCount(&frames);
    if (FAILED(result) || frames < 1)
    {
        return ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    // `requirePng` is true only for RedPrism's own project assets (DecodeProjectPng/File); orientation and color
    // management apply to arbitrary imports (ImportImageFile), never to what RedPrism wrote itself.
    return CopyFrame(factory, frame.get(), mask, !requirePng, remainingPixels, decoded, cancellation);
}

[[nodiscard]] ImageCodecStatus EncodeToFormat(const Raster& image, const GUID& container, double quality,
                                              double resolution, std::vector<std::uint8_t>& bytes,
                                              CancelToken cancellation, IStream* destination = nullptr)
{
    bytes.clear();
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (!image.HasStorage())
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IStream> memory;
    HRESULT result = S_OK;
    if (!destination)
    {
        result = CreateStreamOnHGlobal(nullptr, TRUE, memory.put());
        if (FAILED(result))
            return ImageCodecStatus::Encode;
        destination = memory.get();
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    result = MakeFactory(factory);
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    result = factory->CreateStream(stream.put());
    if (FAILED(result) || FAILED(stream->InitializeFromIStream(destination)))
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(container, nullptr, encoder.put());
    if (FAILED(result) || FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    wil::com_ptr_nothrow<IPropertyBag2> bag;
    result = encoder->CreateNewFrame(frame.put(), bag.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    if (SameGuid(container, GUID_ContainerFormatJpeg) && bag)
    {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = static_cast<FLOAT>(std::min(1.0, std::max(0.1, quality)));
        (void)bag->Write(1, &option, &value);
        VariantClear(&value);
    }
    if (FAILED(frame->Initialize(bag.get())))
    {
        return ImageCodecStatus::Encode;
    }
    const UINT width = static_cast<UINT>(image.Width());
    const UINT height = static_cast<UINT>(image.Height());
    if (width == 0 || height == 0 || FAILED(frame->SetSize(width, height)))
    {
        return ImageCodecStatus::Encode;
    }

    const bool jpegColor = SameGuid(container, GUID_ContainerFormatJpeg) && image.Format() == RasterFormat::Bgra8888;
    WICPixelFormatGUID pixelFormat =
        image.Format() == RasterFormat::Gray8 ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppPBGRA;
    if (jpegColor)
    {
        pixelFormat = GUID_WICPixelFormat24bppBGR;
    }
    if (FAILED(frame->SetPixelFormat(&pixelFormat)))
    {
        return ImageCodecStatus::Encode;
    }
    if (jpegColor && !SameGuid(pixelFormat, GUID_WICPixelFormat24bppBGR))
        return ImageCodecStatus::Encode;
    const bool unpremultiply =
        !jpegColor && image.Format() == RasterFormat::Bgra8888 && !SameGuid(pixelFormat, GUID_WICPixelFormat32bppPBGRA);
    if (unpremultiply && !SameGuid(pixelFormat, GUID_WICPixelFormat32bppBGRA))
    {
        return ImageCodecStatus::Encode;
    }
    if (image.Format() == RasterFormat::Gray8 && !SameGuid(pixelFormat, GUID_WICPixelFormat8bppGray))
    {
        return ImageCodecStatus::Encode;
    }

    const UINT sourceStride = static_cast<UINT>(image.Stride());
    const UINT channels = jpegColor ? 3u : (image.Format() == RasterFormat::Gray8 ? 1u : 4u);
    const UINT stride = width * channels;
    const UINT bandRows = (std::max)(1u, 64u * 1024u / sourceStride);
    std::vector<std::uint8_t> band(static_cast<std::size_t>(sourceStride) * (std::min)(bandRows, height));
    if (resolution > 0)
    {
        (void)frame->SetResolution(resolution, resolution);
    }
    for (UINT y = 0; y < height;)
    {
        if (cancellation.IsCancelled())
        {
            return ImageCodecStatus::Cancelled;
        }
        const UINT rows = (std::min)(bandRows, height - y);
        std::span<std::uint8_t> sourceRows{band.data(), static_cast<std::size_t>(sourceStride) * rows};
        if (!ReadRasterRegionForWork(image, {0, static_cast<int>(y), static_cast<int>(width), static_cast<int>(rows)},
                                     sourceRows, sourceStride, 0, cancellation))
        {
            return cancellation.IsCancelled() ? ImageCodecStatus::Cancelled : ImageCodecStatus::Encode;
        }
        if (unpremultiply || jpegColor)
        {
            const std::size_t pixelCount = static_cast<std::size_t>(width) * rows;
            for (std::size_t index = 0; index < pixelCount; ++index)
            {
                std::uint8_t* pixel = band.data() + index * 4u;
                if (jpegColor)
                {
                    const int white = 255 - pixel[3];
                    const std::uint8_t b = static_cast<std::uint8_t>(std::min(255, pixel[0] + white));
                    const std::uint8_t g = static_cast<std::uint8_t>(std::min(255, pixel[1] + white));
                    const std::uint8_t r = static_cast<std::uint8_t>(std::min(255, pixel[2] + white));
                    std::uint8_t* output = band.data() + index * channels;
                    output[0] = b;
                    output[1] = g;
                    output[2] = r;
                }
                else
                {
                    const std::uint8_t alpha = pixel[3];
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        const int premultiplied = pixel[channel];
                        pixel[channel] =
                            alpha == 0
                                ? 0
                                : static_cast<std::uint8_t>(std::min(255, (premultiplied * 255 + alpha / 2) / alpha));
                    }
                }
            }
        }
        if (cancellation.IsCancelled())
        {
            return ImageCodecStatus::Cancelled;
        }
        const UINT bandBytes = stride * rows;
        result = frame->WritePixels(rows, stride, bandBytes, band.data());
        if (FAILED(result))
        {
            return ImageCodecStatus::Encode;
        }
        y += rows;
    }
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        return ImageCodecStatus::Encode;
    }
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (!memory)
        return ImageCodecStatus::Ok; // Caller owns checked durability/close and destination publication.
    STATSTG stats{};
    if (FAILED(memory->Stat(&stats, STATFLAG_NONAME)) || stats.cbSize.QuadPart <= 0 ||
        static_cast<ULONGLONG>(stats.cbSize.QuadPart) > kMaxEncodedBytes)
    {
        return ImageCodecStatus::Encode;
    }
    LARGE_INTEGER zero{};
    if (FAILED(memory->Seek(zero, STREAM_SEEK_SET, nullptr)))
    {
        return ImageCodecStatus::Encode;
    }
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(stats.cbSize.QuadPart));
    ULONG read = 0;
    if (FAILED(memory->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read)) || read != encoded.size())
    {
        return ImageCodecStatus::Encode;
    }
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    bytes = std::move(encoded);
    return ImageCodecStatus::Ok;
}

[[nodiscard]] ImageCodecStatus EncodeToFile(const Raster& image, const GUID& container, double quality,
                                            double resolution, const wchar_t* path, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return ImageCodecStatus::Cancelled;
    if (path == nullptr || !image.HasStorage())
    {
        return ImageCodecStatus::Encode;
    }
    // A temp file beside the destination, flushed and then swapped in atomically, so a crash or a failed write
    // never leaves a truncated PNG/JPEG at `path` and a concurrent reader never sees a partial file.
    if (cancellation.IsCancelled())
        return ImageCodecStatus::Cancelled;
    const std::wstring temp = std::wstring(path) + L".~" + Guid::Generate().ToString();
    wil::com_ptr_nothrow<CodecFileStream> file;
    const HRESULT created = CodecFileStream::Create(temp.c_str(), kMaxEncodedBytes, cancellation, file.put());
    if (FAILED(created))
        return cancellation.IsCancelled() ? ImageCodecStatus::Cancelled : ImageCodecStatus::Encode;
    auto cleanup = wil::scope_exit(
        [&]
        {
            file.reset();
            (void)DeleteFileW(temp.c_str());
        });
    std::vector<std::uint8_t> unused;
    const auto encoded = EncodeToFormat(image, container, quality, resolution, unused, cancellation, file.get());
    if (encoded != ImageCodecStatus::Ok)
        return cancellation.IsCancelled() ? ImageCodecStatus::Cancelled : encoded;
    STATSTG stats{};
    if (FAILED(file->Stat(&stats, STATFLAG_NONAME)) || stats.cbSize.QuadPart == 0 ||
        stats.cbSize.QuadPart > kMaxEncodedBytes || FAILED(file->CloseChecked()))
        return cancellation.IsCancelled() ? ImageCodecStatus::Cancelled : ImageCodecStatus::Encode;
    file.reset();
    if (cancellation.IsCancelled())
        return ImageCodecStatus::Cancelled;
    const DWORD existing = GetFileAttributesW(path);
    bool replaced = false;
    if (existing != INVALID_FILE_ATTRIBUTES && (existing & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        replaced = ReplaceFileW(path, temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
    }
    if (!replaced)
    {
        replaced = MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING) != 0;
    }
    if (!replaced)
    {
        return ImageCodecStatus::Encode;
    }
    cleanup.release();
    return ImageCodecStatus::Ok;
}
} // namespace

bool WriteFileBytesToDisk(const wchar_t* path, const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (path == nullptr)
    {
        return false;
    }
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    wil::unique_hfile owned{file};
    DWORD written = 0;
    if (size > 0 && (!WriteFile(owned.get(), bytes, static_cast<DWORD>(size), &written, nullptr) || written != size))
    {
        return false;
    }
    // Durable before the caller renames or replaces another file with this one.
    return FlushFileBuffers(owned.get()) != 0;
}

bool IsHeifContainer(std::wstring_view path) noexcept
{
    const std::wstring_view ext = ExtensionOf(path);
    return EqualsExtension(ext, L".heic") || EqualsExtension(ext, L".heif") || EqualsExtension(ext, L".hif");
}

ImageCodecStatus DecodeProjectPng(const std::uint8_t* bytes, std::size_t byteCount, bool mask, int remainingPixels,
                                  DecodedImage& decoded, CancelToken cancellation)
{
    decoded = {};
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (bytes == nullptr || byteCount == 0 || byteCount > kMaxEncodedBytes)
    {
        return byteCount > kMaxEncodedBytes ? ImageCodecStatus::TooLarge : ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result = MakeFactory(factory);
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    result = factory->CreateStream(stream.put());
    if (FAILED(result) || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(byteCount))))
    {
        return ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    const ImageCodecStatus status =
        DecodeFromDecoder(factory.get(), decoder.get(), mask, true, false, remainingPixels, decoded, cancellation);
    if (cancellation.IsCancelled())
    {
        decoded = {};
        return ImageCodecStatus::Cancelled;
    }
    return status;
}

ImageCodecStatus DecodeProjectPngFile(const wchar_t* path, bool mask, int remainingPixels, DecodedImage& decoded,
                                      CancelToken cancellation, std::size_t maximumEncodedBytes)
{
    decoded = {};
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (path == nullptr)
    {
        return ImageCodecStatus::Unreadable;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info))
    {
        return ImageCodecStatus::Unreadable;
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return ImageCodecStatus::Unreadable;
    }
    ULARGE_INTEGER size{};
    size.HighPart = info.nFileSizeHigh;
    size.LowPart = info.nFileSizeLow;
    if (size.QuadPart > (std::min)(static_cast<std::uint64_t>(maximumEncodedBytes), kMaxEncodedBytes))
    {
        return ImageCodecStatus::TooLarge;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result = MakeFactory(factory);
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    result =
        factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    const ImageCodecStatus status =
        DecodeFromDecoder(factory.get(), decoder.get(), mask, true, false, remainingPixels, decoded, cancellation);
    if (cancellation.IsCancelled())
    {
        decoded = {};
        return ImageCodecStatus::Cancelled;
    }
    return status;
}

ImageCodecStatus ImportImageFile(const wchar_t* path, int remainingPixels, DecodedImage& decoded,
                                 CancelToken cancellation)
{
    decoded = {};
    if (cancellation.IsCancelled())
    {
        return ImageCodecStatus::Cancelled;
    }
    if (path == nullptr)
    {
        return ImageCodecStatus::Unreadable;
    }
    const bool heif = IsHeifContainer(path);
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result = MakeFactory(factory);
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    result =
        factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result))
    {
        return MapDecodeError(result, heif);
    }
    const ImageCodecStatus status =
        DecodeFromDecoder(factory.get(), decoder.get(), false, false, true, remainingPixels, decoded, cancellation);
    if (status != ImageCodecStatus::Ok)
    {
        return status;
    }
    if (cancellation.IsCancelled())
    {
        decoded = {};
        return ImageCodecStatus::Cancelled;
    }
    std::wstring wide(path);
    const std::size_t slash = wide.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? wide : wide.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
    {
        name.resize(dot);
    }
    if (cancellation.IsCancelled())
    {
        decoded = {};
        return ImageCodecStatus::Cancelled;
    }
    decoded.name = std::move(name);
    return ImageCodecStatus::Ok;
}

ImageCodecStatus EncodePng(const Raster& image, double resolution, std::vector<std::uint8_t>& bytes,
                           CancelToken cancellation)
{
    bytes.clear();
    return EncodeToFormat(image, GUID_ContainerFormatPng, 1.0, resolution, bytes, cancellation);
}

ImageCodecStatus EncodePngFile(const Raster& image, double resolution, const wchar_t* path, CancelToken cancellation)
{
    return EncodeToFile(image, GUID_ContainerFormatPng, 1.0, resolution, path, cancellation);
}

ImageCodecStatus EncodeJpeg(const Raster& image, const JpegExportOptions& options, std::vector<std::uint8_t>& bytes,
                            CancelToken cancellation)
{
    bytes.clear();
    return EncodeToFormat(image, GUID_ContainerFormatJpeg, options.quality, options.resolution, bytes, cancellation);
}

ImageCodecStatus EncodeJpegFile(const Raster& image, const JpegExportOptions& options, const wchar_t* path,
                                CancelToken cancellation)
{
    return EncodeToFile(image, GUID_ContainerFormatJpeg, options.quality, options.resolution, path, cancellation);
}

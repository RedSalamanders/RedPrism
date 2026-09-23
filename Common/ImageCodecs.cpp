#include "ImageCodecs.h"

#include "Utf8.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <string>
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

[[nodiscard]] std::wstring ExtensionOf(std::wstring_view path) noexcept
{
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring_view name = slash == std::wstring_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot + 1 >= name.size())
    {
        return {};
    }
    std::wstring ext(name.substr(dot));
    for (wchar_t& ch : ext)
    {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return ext;
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
                                                    wil::com_ptr_nothrow<IWICColorContext>& sourceContext) noexcept
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
                                         bool applyImportTransforms, int remainingPixels,
                                         DecodedImage& decoded) noexcept
{
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
    std::vector<std::uint8_t> pixels(bytes);
    result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(bytes), pixels.data());
    if (FAILED(result))
    {
        return ImageCodecStatus::Unreadable;
    }

    decoded.image =
        Raster::Make(static_cast<int>(width), static_cast<int>(height), format, pixels.data(), pixels.size());
    if (!decoded.image.HasStorage())
    {
        return ImageCodecStatus::Unreadable;
    }
    if (mask && !LayerMask::IsValid(decoded.image))
    {
        return ImageCodecStatus::Unreadable;
    }
    decoded.thumbnail = Raster::MakeThumbnail(decoded.image, kThumbnailMaxSide);
    return ImageCodecStatus::Ok;
}

[[nodiscard]] ImageCodecStatus DecodeFromDecoder(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, bool mask,
                                                 bool requirePng, bool allowHeif, int remainingPixels,
                                                 DecodedImage& decoded) noexcept
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
    return CopyFrame(factory, frame.get(), mask, !requirePng, remainingPixels, decoded);
}

[[nodiscard]] ImageCodecStatus EncodeToFormat(const Raster& image, const GUID& container, double quality,
                                              double resolution, std::vector<std::uint8_t>& bytes) noexcept
{
    if (!image.HasStorage() || image.Data() == nullptr)
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IStream> memory;
    HRESULT result = CreateStreamOnHGlobal(nullptr, TRUE, memory.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    result = MakeFactory(factory);
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    result = factory->CreateStream(stream.put());
    if (FAILED(result) || FAILED(stream->InitializeFromIStream(memory.get())))
    {
        return ImageCodecStatus::Encode;
    }

    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(container, nullptr, encoder.put());
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    if (FAILED(result))
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
    result = frame->Initialize(bag.get());
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    const UINT width = static_cast<UINT>(image.Width());
    const UINT height = static_cast<UINT>(image.Height());
    result = frame->SetSize(width, height);
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    WICPixelFormatGUID pixelFormat =
        image.Format() == RasterFormat::Gray8 ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppPBGRA;
    std::vector<std::uint8_t> jpegBgr;
    std::vector<std::uint8_t> straightBgra;
    const std::uint8_t* pixels = image.Data();
    UINT stride = static_cast<UINT>(image.Stride());
    UINT bufferSize = stride * height;
    const bool jpegColor = SameGuid(container, GUID_ContainerFormatJpeg) && image.Format() == RasterFormat::Bgra8888;
    if (jpegColor)
    {
        pixelFormat = GUID_WICPixelFormat24bppBGR;
        stride = width * 3;
        bufferSize = stride * height;
        jpegBgr.resize(bufferSize);
        for (UINT y = 0; y < height; ++y)
        {
            const std::uint8_t* src =
                image.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride());
            std::uint8_t* dst = jpegBgr.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
            for (UINT x = 0; x < width; ++x)
            {
                // JPEG carries no alpha, so a transparent pixel is flattened onto a white matte, matching legacy
                // ImageExporter.jpeg's default JPEGOptions() (red = green = blue = 1): source-over premultiplied
                // color over opaque white is `premultiplied + (255 - alpha)` per channel.
                const int white = 255 - src[x * 4 + 3];
                dst[x * 3 + 0] = static_cast<std::uint8_t>(std::min(255, src[x * 4 + 0] + white));
                dst[x * 3 + 1] = static_cast<std::uint8_t>(std::min(255, src[x * 4 + 1] + white));
                dst[x * 3 + 2] = static_cast<std::uint8_t>(std::min(255, src[x * 4 + 2] + white));
            }
        }
        pixels = jpegBgr.data();
    }
    result = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
    }
    if (!jpegColor)
    {
        const GUID requestedFormat =
            image.Format() == RasterFormat::Gray8 ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat32bppPBGRA;
        if (!SameGuid(pixelFormat, requestedFormat))
        {
            // The PNG encoder has no premultiplied-alpha support and silently substitutes straight alpha
            // (WindowCapture.cpp checks the same way); un-premultiply our premultiplied raster bytes before
            // WritePixels, or every semi-transparent pixel is written roughly half as bright as it should be.
            // Any other substitution isn't one we know how to feed correctly.
            if (image.Format() != RasterFormat::Bgra8888 || !SameGuid(pixelFormat, GUID_WICPixelFormat32bppBGRA))
            {
                return ImageCodecStatus::Encode;
            }
            straightBgra.resize(bufferSize);
            for (UINT y = 0; y < height; ++y)
            {
                const std::uint8_t* src =
                    image.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride());
                std::uint8_t* dst =
                    straightBgra.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
                for (UINT x = 0; x < width; ++x)
                {
                    const std::uint8_t alpha = src[x * 4 + 3];
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        const int premultiplied = src[x * 4 + channel];
                        dst[x * 4 + channel] =
                            alpha == 0
                                ? 0
                                : static_cast<std::uint8_t>(std::min(255, (premultiplied * 255 + alpha / 2) / alpha));
                    }
                    dst[x * 4 + 3] = alpha;
                }
            }
            pixels = straightBgra.data();
        }
    }
    if (resolution > 0)
    {
        (void)frame->SetResolution(resolution, resolution);
    }
    result = frame->WritePixels(height, stride, bufferSize, const_cast<BYTE*>(pixels));
    if (FAILED(result) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        return ImageCodecStatus::Encode;
    }
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
    bytes.resize(static_cast<std::size_t>(stats.cbSize.QuadPart));
    ULONG read = 0;
    if (FAILED(memory->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size())
    {
        bytes.clear();
        return ImageCodecStatus::Encode;
    }
    return ImageCodecStatus::Ok;
}

[[nodiscard]] ImageCodecStatus WriteAllBytes(const wchar_t* path, const std::vector<std::uint8_t>& bytes) noexcept
{
    if (path == nullptr || bytes.empty())
    {
        return ImageCodecStatus::Encode;
    }
    // A temp file beside the destination, flushed and then swapped in atomically, so a crash or a failed write
    // never leaves a truncated PNG/JPEG at `path` and a concurrent reader never sees a partial file.
    const std::wstring temp = std::wstring(path) + L".tmp";
    if (!WriteFileBytesToDisk(temp.c_str(), bytes.data(), bytes.size()))
    {
        DeleteFileW(temp.c_str());
        return ImageCodecStatus::Encode;
    }
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
        DeleteFileW(temp.c_str());
        return ImageCodecStatus::Encode;
    }
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
    const std::wstring ext = ExtensionOf(path);
    return ext == L".heic" || ext == L".heif" || ext == L".hif";
}

ImageCodecStatus DecodeProjectPng(const std::uint8_t* bytes, std::size_t byteCount, bool mask, int remainingPixels,
                                  DecodedImage& decoded) noexcept
{
    decoded = {};
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
    return DecodeFromDecoder(factory.get(), decoder.get(), mask, true, false, remainingPixels, decoded);
}

ImageCodecStatus DecodeProjectPngFile(const wchar_t* path, bool mask, int remainingPixels,
                                      DecodedImage& decoded) noexcept
{
    decoded = {};
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
    if (size.QuadPart > kMaxEncodedBytes)
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
    return DecodeFromDecoder(factory.get(), decoder.get(), mask, true, false, remainingPixels, decoded);
}

ImageCodecStatus ImportImageFile(const wchar_t* path, int remainingPixels, DecodedImage& decoded) noexcept
{
    decoded = {};
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
        DecodeFromDecoder(factory.get(), decoder.get(), false, false, true, remainingPixels, decoded);
    if (status != ImageCodecStatus::Ok)
    {
        return status;
    }
    std::wstring wide(path);
    const std::size_t slash = wide.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? wide : wide.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
    {
        name.resize(dot);
    }
    decoded.name = std::move(name);
    return ImageCodecStatus::Ok;
}

ImageCodecStatus EncodePng(const Raster& image, double resolution, std::vector<std::uint8_t>& bytes) noexcept
{
    bytes.clear();
    return EncodeToFormat(image, GUID_ContainerFormatPng, 1.0, resolution, bytes);
}

ImageCodecStatus EncodePngFile(const Raster& image, double resolution, const wchar_t* path) noexcept
{
    std::vector<std::uint8_t> bytes;
    const ImageCodecStatus status = EncodePng(image, resolution, bytes);
    if (status != ImageCodecStatus::Ok)
    {
        return status;
    }
    return WriteAllBytes(path, bytes);
}

ImageCodecStatus EncodeJpeg(const Raster& image, const JpegExportOptions& options,
                            std::vector<std::uint8_t>& bytes) noexcept
{
    bytes.clear();
    return EncodeToFormat(image, GUID_ContainerFormatJpeg, options.quality, options.resolution, bytes);
}

ImageCodecStatus EncodeJpegFile(const Raster& image, const JpegExportOptions& options, const wchar_t* path) noexcept
{
    std::vector<std::uint8_t> bytes;
    const ImageCodecStatus status = EncodeJpeg(image, options, bytes);
    if (status != ImageCodecStatus::Ok)
    {
        return status;
    }
    return WriteAllBytes(path, bytes);
}

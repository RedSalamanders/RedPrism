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

[[nodiscard]] ImageCodecStatus CopyFrame(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, bool mask,
                                         int remainingPixels, DecodedImage& decoded) noexcept
{
    UINT width = 0;
    UINT height = 0;
    HRESULT result = frame->GetSize(&width, &height);
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
        converter->Initialize(frame, pixelFormat, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
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
    return CopyFrame(factory, frame.get(), mask, remainingPixels, decoded);
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
    const std::uint8_t* pixels = image.Data();
    UINT stride = static_cast<UINT>(image.Stride());
    UINT bufferSize = stride * height;
    if (SameGuid(container, GUID_ContainerFormatJpeg) && image.Format() == RasterFormat::Bgra8888)
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
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }
        pixels = jpegBgr.data();
    }
    result = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(result))
    {
        return ImageCodecStatus::Encode;
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
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return ImageCodecStatus::Encode;
    }
    wil::unique_hfile owned{file};
    DWORD written = 0;
    if (!WriteFile(owned.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size())
    {
        return ImageCodecStatus::Encode;
    }
    return ImageCodecStatus::Ok;
}
} // namespace

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

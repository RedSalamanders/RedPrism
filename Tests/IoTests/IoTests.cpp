#include "Document.h"
#include "ImageCodecWorkTests.h"
#include "ImageCodecs.h"
#include "PathClassificationWorkTests.h"
#include "ProjectStore.h"
#include "SettingsStore.h"
#include "ZipOwner.h"

#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <objbase.h>
#include <string>
#include <vector>
#include <wincodec.h>
#include <windows.h>
#include <zip.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", name);
    std::fflush(stdout);
    ++gFailures;
}

[[nodiscard]] bool Near(double actual, double expected, double slop = 1e-9) noexcept
{
    return std::abs(actual - expected) <= slop;
}

[[nodiscard]] std::wstring FixtureRoot()
{
    return std::filesystem::path(__FILE__).parent_path() / L"Fixtures" / L"v7-exploded";
}

[[nodiscard]] std::wstring FixtureRootNamed(const wchar_t* name)
{
    return std::filesystem::path(__FILE__).parent_path() / L"Fixtures" / name;
}

[[nodiscard]] std::wstring ScratchRoot()
{
    // Per-process, not just per-suite: two concurrent IoTests.exe runs (different platforms/configurations, or two
    // worktrees) must not collide on the same fixed path (RF-708).
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / L"RedPrism.IoTests" / std::to_wstring(GetCurrentProcessId());
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

[[nodiscard]] Raster SolidBgra(int width, int height, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = b;
        pixels[i + 1] = g;
        pixels[i + 2] = r;
        pixels[i + 3] = a;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] Raster Gray(int width, int height, std::initializer_list<std::uint8_t> values)
{
    std::vector<std::uint8_t> pixels(values);
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

[[nodiscard]] bool RastersEqual(const Raster& left, const Raster& right) noexcept
{
    if (!left.HasStorage() || !right.HasStorage() || left.Width() != right.Width() || left.Height() != right.Height() ||
        left.Format() != right.Format() || left.ByteSize() != right.ByteSize())
    {
        return false;
    }
    return std::memcmp(left.Data(), right.Data(), left.ByteSize()) == 0;
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] int ZipCompressionMethod(const std::wstring& path, const char* name)
{
    zip_error_t error{};
    zip_error_init(&error);
    zip_source_t* source = zip_source_win32w_create(path.c_str(), 0, -1, &error);
    zip_t* raw = source == nullptr ? nullptr : zip_open_from_source(source, ZIP_RDONLY, &error);
    if (raw == nullptr && source != nullptr)
    {
        zip_source_free(source);
    }
    zip_error_fini(&error);
    unique_zip archive{raw};
    if (!archive)
    {
        return -1;
    }
    const zip_int64_t index = zip_name_locate(archive.get(), name, 0);
    zip_stat_t stat{};
    zip_stat_init(&stat);
    return index < 0 || zip_stat_index(archive.get(), static_cast<zip_uint64_t>(index), 0, &stat) != 0 ||
                   (stat.valid & ZIP_STAT_COMP_METHOD) == 0
               ? -1
               : stat.comp_method;
}

[[nodiscard]] bool RewriteZipDeflated(const std::wstring& sourcePath, const std::wstring& destinationPath)
{
    zip_error_t inputError{};
    zip_error_init(&inputError);
    zip_source_t* inputSource = zip_source_win32w_create(sourcePath.c_str(), 0, -1, &inputError);
    zip_t* inputRaw = inputSource == nullptr ? nullptr : zip_open_from_source(inputSource, ZIP_RDONLY, &inputError);
    if (inputRaw == nullptr && inputSource != nullptr)
    {
        zip_source_free(inputSource);
    }
    zip_error_fini(&inputError);
    unique_zip input{inputRaw};
    if (!input)
    {
        return false;
    }
    struct Entry
    {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Entry> entries;
    const zip_int64_t count = zip_get_num_entries(input.get(), 0);
    if (count < 0)
    {
        return false;
    }
    entries.reserve(static_cast<std::size_t>(count));
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat{};
        zip_stat_init(&stat);
        if (zip_stat_index(input.get(), index, 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0 ||
            (stat.valid & ZIP_STAT_NAME) == 0 || stat.size > SIZE_MAX)
        {
            return false;
        }
        Entry entry;
        entry.name = stat.name;
        entry.bytes.resize(static_cast<std::size_t>(stat.size));
        unique_zip_file file{zip_fopen_index(input.get(), index, 0)};
        if (!file)
        {
            return false;
        }
        zip_uint64_t offset = 0;
        while (offset < stat.size)
        {
            const zip_int64_t read =
                zip_fread(file.get(), entry.bytes.data() + static_cast<std::size_t>(offset), stat.size - offset);
            if (read <= 0)
            {
                return false;
            }
            offset += static_cast<zip_uint64_t>(read);
        }
        if (zip_fclose(file.release()) != 0)
        {
            return false;
        }
        entries.push_back(std::move(entry));
    }

    zip_error_t outputError{};
    zip_error_init(&outputError);
    zip_source_t* outputSource = zip_source_win32w_create(destinationPath.c_str(), 0, -1, &outputError);
    zip_t* outputRaw =
        outputSource == nullptr ? nullptr : zip_open_from_source(outputSource, ZIP_CREATE | ZIP_EXCL, &outputError);
    if (outputRaw == nullptr && outputSource != nullptr)
    {
        zip_source_free(outputSource);
    }
    zip_error_fini(&outputError);
    unique_zip output{outputRaw};
    if (!output)
    {
        return false;
    }
    for (Entry& entry : entries)
    {
        zip_source_t* buffer = zip_source_buffer(output.get(), entry.bytes.data(), entry.bytes.size(), 0);
        if (buffer == nullptr)
        {
            return false;
        }
        const zip_int64_t index = zip_file_add(output.get(), entry.name.c_str(), buffer, ZIP_FL_ENC_UTF_8);
        if (index < 0)
        {
            zip_source_free(buffer);
            return false;
        }
        if (zip_set_file_compression(output.get(), static_cast<zip_uint64_t>(index), ZIP_CM_DEFLATE, 6) != 0)
        {
            return false;
        }
    }
    return ZipCommit(output);
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

[[nodiscard]] const ImageLayer* FindLayer(const CanvasDocument& document, const std::wstring& name) noexcept
{
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.name == name)
        {
            return &layer;
        }
    }
    return nullptr;
}

[[nodiscard]] std::wstring EmptyProjectRoot(const wchar_t* name)
{
    const std::wstring root = ScratchRoot() + L"\\" + name;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(std::filesystem::path(root) / L"images", error);
    return root;
}

// A minimal manifest with one blank layer. Extra fields come first in their objects so a duplicate key overrides
// the default (yyjson returns the first occurrence).
[[nodiscard]] std::string MinimalManifest(int version, const std::string& extraLayerFields,
                                          const std::string& extraRootFields = {})
{
    return "{\n" + (extraRootFields.empty() ? std::string{} : "  " + extraRootFields + ",\n") +
           "  \"colorSpace\": \"sRGB\",\n  \"documentID\": \"00000000-0000-4000-8000-000000000001\",\n"
           "  \"format\": \"com.redprism.project\",\n  \"height\": 1,\n  \"layers\": [\n    {\n" +
           (extraLayerFields.empty() ? std::string{} : "      " + extraLayerFields + ",\n") +
           "      \"id\": \"00000000-0000-4000-8000-000000000002\",\n      \"isVisible\": true,\n"
           "      \"name\": \"Blank\",\n      \"transform\": {\"flipX\": false, \"flipY\": false, "
           "\"origin\": {\"x\": 0, \"y\": 0}, \"rotation\": 0, \"sampling\": \"High quality\", "
           "\"size\": {\"height\": 1, \"width\": 1}}\n    }\n  ],\n  \"version\": " +
           std::to_string(version) + ",\n  \"width\": 1\n}\n";
}

// Two valid, distinct layers where the first clips to the second (a real sibling, not itself), so a version gate
// that requires `maskSourceID` is the ONLY thing that can reject this manifest — a self-clip would also be caught
// by the live-mask cycle check regardless of the gate, masking a regression there (RF-111).
[[nodiscard]] std::string ManifestWithMaskSourceSibling(int version)
{
    return "{\n  \"colorSpace\": \"sRGB\",\n  \"documentID\": \"00000000-0000-4000-8000-000000000001\",\n"
           "  \"format\": \"com.redprism.project\",\n  \"height\": 1,\n  \"layers\": [\n    {\n"
           "      \"id\": \"00000000-0000-4000-8000-000000000002\",\n      \"isVisible\": true,\n"
           "      \"maskSourceID\": \"00000000-0000-4000-8000-000000000003\",\n"
           "      \"name\": \"Blank\",\n      \"transform\": {\"flipX\": false, \"flipY\": false, "
           "\"origin\": {\"x\": 0, \"y\": 0}, \"rotation\": 0, \"sampling\": \"High quality\", "
           "\"size\": {\"height\": 1, \"width\": 1}}\n    },\n    {\n"
           "      \"id\": \"00000000-0000-4000-8000-000000000003\",\n      \"isVisible\": true,\n"
           "      \"name\": \"Sibling\",\n      \"transform\": {\"flipX\": false, \"flipY\": false, "
           "\"origin\": {\"x\": 0, \"y\": 0}, \"rotation\": 0, \"sampling\": \"High quality\", "
           "\"size\": {\"height\": 1, \"width\": 1}}\n    }\n  ],\n  \"version\": " +
           std::to_string(version) + ",\n  \"width\": 1\n}\n";
}

[[nodiscard]] ProjectIoStatus LoadManifestText(const wchar_t* name, const std::string& manifest)
{
    const std::wstring root = EmptyProjectRoot(name);
    WriteText(std::filesystem::path(root) / L"manifest.json", manifest);
    const ProjectIoStatus status = LoadProject(root.c_str()).status;
    std::printf("     %ls -> status %d\n", name, static_cast<int>(status));
    return status;
}

void TestGuidParse()
{
    const Guid id = Guid::Generate();
    const std::optional<Guid> parsed = Guid::Parse(id.ToUtf8());
    Expect(parsed.has_value() && parsed.value() == id, "guid.round-trip");
    Expect(!Guid::Parse("not-a-guid").has_value(), "guid.invalid");
    std::string lower = id.ToUtf8();
    for (char& ch : lower)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const std::optional<Guid> fromLower = Guid::Parse(lower);
    Expect(fromLower.has_value() && fromLower.value() == id, "guid.lowercase");
    // A GUID with hex letters actually exercises casing (Generate() may not roll any); compare against the
    // independently-uppercased text, not a copy of itself.
    const std::optional<Guid> hex = Guid::Parse("aabbccdd-eeff-1122-8899-aabbccddeeff");
    std::string upper = "aabbccdd-eeff-1122-8899-aabbccddeeff";
    for (char& ch : upper)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    Expect(hex.has_value() && hex.value().ToUtf8().size() == 36 && hex.value().ToUtf8() == upper, "guid.uppercase-36");
}

void TestNameTables()
{
    for (int index = 0; index < LayerBlendModeCount(); ++index)
    {
        const LayerBlendMode mode = static_cast<LayerBlendMode>(index);
        const std::optional<LayerBlendMode> back = LayerBlendModeFromName(LayerBlendModeName(mode));
        Expect(back.has_value() && back.value() == mode, "names.blend-mode");
    }
    Expect(std::string(LayerBlendModeName(LayerBlendMode::ColorDodge)) == "Color Dodge", "names.color-dodge");
    Expect(std::string(LayerSamplingName(LayerSampling::High)) == "High quality", "names.sampling");
    Expect(std::string(AdjustmentKindName(AdjustmentKind::GradientMap)) == "Gradient Map", "names.gradient-map");
    Expect(!LayerBlendModeFromName("Vivid Light").has_value(), "names.unknown-blend");
    Expect(ColorRangeFromName("Magentas") == ColorRange::Magentas, "names.color-range");
}

void TestPngRoundTrip()
{
    const Raster red = SolidBgra(2, 2, 0, 0, 255, 255);
    std::vector<std::uint8_t> png;
    Expect(EncodePng(red, 72, png) == ImageCodecStatus::Ok && !png.empty(), "png.encode");
    DecodedImage decoded{};
    Expect(DecodeProjectPng(png.data(), png.size(), false, kMaxLayerPixels, decoded) == ImageCodecStatus::Ok,
           "png.decode");
    Expect(RastersEqual(red, decoded.image), "png.pixels");
    Expect(decoded.thumbnail.Width() <= 96 && decoded.thumbnail.Height() <= 96, "png.thumbnail");

    const Raster mask = Gray(2, 2, {255, 0, 128, 255});
    std::vector<std::uint8_t> maskPng;
    Expect(EncodePng(mask, 72, maskPng) == ImageCodecStatus::Ok, "png.mask-encode");
    DecodedImage decodedMask{};
    Expect(DecodeProjectPng(maskPng.data(), maskPng.size(), true, kMaxMaskPixels, decodedMask) == ImageCodecStatus::Ok,
           "png.mask-decode");
    Expect(RastersEqual(mask, decodedMask.image) && decodedMask.image.Format() == RasterFormat::Gray8,
           "png.mask-pixels");
    Expect(DecodeProjectPng(png.data(), png.size(), false, 3, decoded) == ImageCodecStatus::TooLarge,
           "png.remaining-pixel-budget");

    const Raster large = SolidBgra(300, 100, 1, 2, 3, 255);
    Expect(Raster::MakeThumbnail(large).Width() == 96 && Raster::MakeThumbnail(large).Height() == 32,
           "png.thumbnail-96-long-side");

    // RF-109: a mask must already be 8-bit gray with no alpha (legacy LayerMask.isValid); a color PNG is rejected,
    // not silently desaturated into a mask.
    const Raster colorAsMask = SolidBgra(2, 2, 10, 20, 30, 255);
    std::vector<std::uint8_t> colorPng;
    Expect(EncodePng(colorAsMask, 72, colorPng) == ImageCodecStatus::Ok, "mask.reject-color-encode");
    DecodedImage rejectedMask{};
    Expect(DecodeProjectPng(colorPng.data(), colorPng.size(), true, kMaxMaskPixels, rejectedMask) ==
               ImageCodecStatus::Unreadable,
           "mask.reject-non-gray");

    // RF-106: exports write a temp file and replace the destination, so re-exporting to an existing path still
    // overwrites correctly and never leaves a `.tmp` file behind.
    const std::wstring exportPath = ScratchRoot() + L"\\durable-export.png";
    Expect(EncodePngFile(SolidBgra(2, 2, 1, 2, 3, 255), 72, exportPath.c_str()) == ImageCodecStatus::Ok,
           "png.export-writes");
    Expect(EncodePngFile(SolidBgra(3, 3, 9, 8, 7, 255), 72, exportPath.c_str()) == ImageCodecStatus::Ok,
           "png.export-overwrites");
    DecodedImage overwritten{};
    Expect(DecodeProjectPngFile(exportPath.c_str(), false, kMaxLayerPixels, overwritten) == ImageCodecStatus::Ok &&
               overwritten.image.Width() == 3 && overwritten.image.Height() == 3,
           "png.export-overwrite-content");
    Expect(!std::filesystem::exists(std::filesystem::path(exportPath + L".tmp")), "png.export-leaves-no-temporary");
}

void TestPngAlphaRoundTrip()
{
    // RF-101: the WIC PNG encoder has no premultiplied pixel format and silently substitutes straight alpha for
    // the PBGRA we ask for; raster pixels are premultiplied (Document_Model.md), so every semi-transparent pixel
    // must be un-premultiplied before WritePixels or it round-trips at roughly half its brightness (probe:
    // premultiplied (B0,G0,R128,A128) decoded back as R64 without the fix).
    constexpr std::uint8_t alphas[] = {1, 64, 128, 254, 255};
    for (const std::uint8_t alpha : alphas)
    {
        // Premultiplied channels expressed as fractions of alpha itself (not of 255), so none collapse to 0 even
        // at alpha = 1: straight blue = 255 (fully saturated), green = straight 25%, red = straight 50%.
        const std::uint8_t b = alpha;
        const std::uint8_t g = static_cast<std::uint8_t>(alpha / 4);
        const std::uint8_t r = static_cast<std::uint8_t>(alpha / 2);
        const Raster soft = SolidBgra(2, 2, b, g, r, alpha);
        std::vector<std::uint8_t> png;
        Expect(EncodePng(soft, 72, png) == ImageCodecStatus::Ok, "png.alpha-round-trip-encode");
        DecodedImage decoded{};
        Expect(DecodeProjectPng(png.data(), png.size(), false, kMaxLayerPixels, decoded) == ImageCodecStatus::Ok,
               "png.alpha-round-trip-decode");
        if (decoded.image.HasStorage())
        {
            const std::uint8_t* pixel = decoded.image.Data();
            Expect(std::abs(static_cast<int>(pixel[0]) - b) <= 2 && std::abs(static_cast<int>(pixel[1]) - g) <= 2 &&
                       std::abs(static_cast<int>(pixel[2]) - r) <= 2 && pixel[3] == alpha,
                   "png.alpha-round-trip-pixels");
        }
    }

    // A project save/load of a soft (semi-transparent) pixel goes through the same encoder path.
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 2;
    ImageLayer layer = ImageLayer::Blank(L"Soft", Vec2{2, 2});
    const Raster softAsset = SolidBgra(2, 2, 40, 60, 90, 128); // premultiplied, ~50% opacity
    layer.asset = RasterAsset{softAsset, Raster::MakeThumbnail(softAsset), layer.name};
    document.layers.push_back(layer);
    const std::wstring path = ScratchRoot() + L"\\soft-alpha";
    Expect(SaveProject(path.c_str(), document, std::nullopt, false) == ProjectIoStatus::Ok, "png.alpha-project-save");
    const ProjectLoadResult reloaded = LoadProject(path.c_str());
    Expect(reloaded.status == ProjectIoStatus::Ok && !reloaded.document.layers.empty() &&
               reloaded.document.layers.front().asset.has_value(),
           "png.alpha-project-load");
    if (reloaded.status == ProjectIoStatus::Ok && !reloaded.document.layers.empty() &&
        reloaded.document.layers.front().asset.has_value())
    {
        const std::uint8_t* pixel = reloaded.document.layers.front().asset.value().image.Data();
        Expect(std::abs(static_cast<int>(pixel[0]) - 40) <= 2 && std::abs(static_cast<int>(pixel[1]) - 60) <= 2 &&
                   std::abs(static_cast<int>(pixel[2]) - 90) <= 2 && pixel[3] == 128,
               "png.alpha-project-round-trip-pixels");
    }
}

void TestJpegExport()
{
    const Raster blue = SolidBgra(4, 4, 255, 0, 0, 128);
    std::vector<std::uint8_t> jpeg;
    Expect(EncodeJpeg(blue, JpegExportOptions{0.4, 72}, jpeg) == ImageCodecStatus::Ok && !jpeg.empty(), "jpeg.encode");
    const std::wstring path = ScratchRoot() + L"\\export.jpg";
    Expect(EncodeJpegFile(blue, JpegExportOptions{0.9, 300}, path.c_str()) == ImageCodecStatus::Ok, "jpeg.file");
    DecodedImage decoded{};
    Expect(ImportImageFile(path.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
               decoded.image.Width() == 4 && decoded.image.Height() == 4 && decoded.name == L"export",
           "jpeg.import");
    Expect(decoded.image.HasStorage() && decoded.image.Data()[3] == 255, "jpeg.alpha-stripped");

    // RF-108: JPEG carries no alpha, so a transparent pixel is flattened onto a white matte (legacy
    // ImageExporter.jpeg's default JPEGOptions(), red = green = blue = 1), not dropped to black by discarding the
    // premultiplied alpha. Premultiplied (B0,G0,R128,A128) over white -> straight (B~127,G~127,R~255).
    const Raster halfRed = SolidBgra(2, 2, 0, 0, 128, 128);
    const std::wstring flattenPath = ScratchRoot() + L"\\flatten.jpg";
    Expect(EncodeJpegFile(halfRed, JpegExportOptions{1.0, 72}, flattenPath.c_str()) == ImageCodecStatus::Ok,
           "jpeg.flatten-file");
    DecodedImage flattened{};
    Expect(ImportImageFile(flattenPath.c_str(), kMaxLayerPixels, flattened) == ImageCodecStatus::Ok &&
               flattened.image.HasStorage(),
           "jpeg.flatten-import");
    if (flattened.image.HasStorage())
    {
        const std::uint8_t* pixel = flattened.image.Data();
        Expect(std::abs(static_cast<int>(pixel[0]) - 127) <= 6 && std::abs(static_cast<int>(pixel[1]) - 127) <= 6 &&
                   std::abs(static_cast<int>(pixel[2]) - 255) <= 6 && pixel[3] == 255,
               "jpeg.flatten-white-matte");
    }
}

void TestJpegQualityGolden()
{
    // Ported from legacy JPEGExportTests.qualityChangesBytesAndDecodedPixels: a 128x128 textured image (not a
    // solid color, which compresses to about the same size at any quality and would make `<` meaningless).
    constexpr int side = 128;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(side) * side * 4);
    for (int y = 0; y < side; ++y)
    {
        for (int x = 0; x < side; ++x)
        {
            std::uint8_t* p = &pixels[(static_cast<std::size_t>(y) * side + x) * 4];
            p[0] = static_cast<std::uint8_t>((x * 79 + y * 7) % 256);  // B
            p[1] = static_cast<std::uint8_t>((x * 11 + y * 53) % 256); // G
            p[2] = static_cast<std::uint8_t>((x * 37 + y * 17) % 256); // R
            p[3] = 255;
        }
    }
    const Raster textured = Raster::Make(side, side, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    std::vector<std::uint8_t> low;
    std::vector<std::uint8_t> high;
    Expect(EncodeJpeg(textured, JpegExportOptions{0.1, 72}, low) == ImageCodecStatus::Ok &&
               EncodeJpeg(textured, JpegExportOptions{1.0, 72}, high) == ImageCodecStatus::Ok,
           "jpeg.quality-golden-encode");
    Expect(low.size() < high.size(), "jpeg.quality-golden-bytes");

    const std::wstring lowPath = ScratchRoot() + L"\\quality-low.jpg";
    const std::wstring highPath = ScratchRoot() + L"\\quality-high.jpg";
    WriteText(lowPath, std::string(reinterpret_cast<const char*>(low.data()), low.size()));
    WriteText(highPath, std::string(reinterpret_cast<const char*>(high.data()), high.size()));
    DecodedImage lowDecoded{};
    DecodedImage highDecoded{};
    Expect(ImportImageFile(lowPath.c_str(), kMaxLayerPixels, lowDecoded) == ImageCodecStatus::Ok &&
               ImportImageFile(highPath.c_str(), kMaxLayerPixels, highDecoded) == ImageCodecStatus::Ok &&
               lowDecoded.image.HasStorage() && highDecoded.image.HasStorage(),
           "jpeg.quality-golden-decode");
    if (!lowDecoded.image.HasStorage() || !highDecoded.image.HasStorage())
    {
        return;
    }
    // legacy: sums abs(redComponent difference) (0…1 scale) over a 16x16 block and expects > 1.
    double difference = 0;
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            const std::size_t index = (static_cast<std::size_t>(y) * side + x) * 4;
            difference += std::abs(static_cast<double>(lowDecoded.image.Data()[index + 2]) / 255.0 -
                                   static_cast<double>(highDecoded.image.Data()[index + 2]) / 255.0);
        }
    }
    Expect(difference > 1, "jpeg.quality-golden-decoded-pixels-differ");
}

void TestHeifMissing()
{
    const std::wstring path = ScratchRoot() + L"\\missing.heic";
    const std::uint8_t junk[] = {'n', 'o', 't', 'h', 'e', 'i', 'f'};
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") == 0 && file)
    {
        (void)fwrite(junk, 1, sizeof(junk), file);
        (void)fclose(file);
    }
    DecodedImage decoded{};
    const ImageCodecStatus status = ImportImageFile(path.c_str(), kMaxLayerPixels, decoded);
    Expect(status == ImageCodecStatus::HeifUnavailable || status == ImageCodecStatus::Unreadable ||
               status == ImageCodecStatus::Unsupported,
           "heif.clean-error");
    Expect(IsHeifContainer(L"photo.HEIC") && !IsHeifContainer(L"photo.png"), "heif.extension");
    Expect(ImportImageFile(L"Z:\\no\\such\\file.png", kMaxLayerPixels, decoded) != ImageCodecStatus::Ok,
           "import.missing-file");
}

// Writes a small WIC TIFF fixture with an EXIF orientation tag (274) and, optionally, a real embedded ICC profile,
// so RF-104 (import applies orientation and color management) can be tested without a checked-in binary asset.
// `paintPixel`, when set, is called for every (x, y) to fill its 4 BGRA bytes; the default paints the same
// per-quadrant pattern the orientation/color tests originally used.
[[nodiscard]] std::wstring WriteOrientedFixture(const wchar_t* name, int width, int height, int orientation,
                                                const wchar_t* iccProfilePath,
                                                const std::function<void(int, int, std::uint8_t*)>& paintPixel = {})
{
    const std::wstring path = ScratchRoot() + L"\\" + name;
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))))
    {
        return {};
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
    {
        return {};
    }
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatTiff, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
    {
        return {};
    }
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    wil::com_ptr_nothrow<IPropertyBag2> bag;
    if (FAILED(encoder->CreateNewFrame(frame.put(), bag.put())) || FAILED(frame->Initialize(bag.get())))
    {
        return {};
    }
    if (FAILED(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height))))
    {
        return {};
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)))
    {
        return {};
    }
    wil::com_ptr_nothrow<IWICColorContext> iccContext;
    if (iccProfilePath != nullptr)
    {
        if (FAILED(factory->CreateColorContext(iccContext.put())) ||
            FAILED(iccContext->InitializeFromFilename(iccProfilePath)))
        {
            return {};
        }
        IWICColorContext* contexts[] = {iccContext.get()};
        (void)frame->SetColorContexts(1, contexts);
    }
    // A distinct color per quadrant (or whatever `paintPixel` draws) so a rotation is visible in more than just
    // the reported dimensions.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            std::uint8_t* p = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
            if (paintPixel)
            {
                paintPixel(x, y, p);
                continue;
            }
            p[0] = x < width / 2 ? 255 : 0; // B
            p[1] = 0;
            p[2] = y < height / 2 ? 255 : 0; // R
            p[3] = 255;
        }
    }
    if (FAILED(frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 4,
                                  static_cast<UINT>(pixels.size()), pixels.data())))
    {
        return {};
    }
    if (orientation != 1)
    {
        wil::com_ptr_nothrow<IWICMetadataQueryWriter> writer;
        if (SUCCEEDED(frame->GetMetadataQueryWriter(writer.put())))
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            value.vt = VT_UI2;
            value.uiVal = static_cast<USHORT>(orientation);
            (void)writer->SetMetadataByName(L"/ifd/{ushort=274}", &value);
            PropVariantClear(&value);
        }
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        return {};
    }
    return path;
}

void TestImportOrientationAndColor()
{
    // Ported from legacy ImageImportTests.orientationAndColorConversion: a 64x32 source at EXIF orientation 6
    // imports as 32x64 (rotated 90 degrees clockwise to correct it).
    const std::wstring rotated = WriteOrientedFixture(L"oriented.tif", 64, 32, 6, nullptr);
    Expect(!rotated.empty(), "orientation.fixture-written");
    DecodedImage decoded{};
    Expect(ImportImageFile(rotated.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
               decoded.image.HasStorage(),
           "orientation.import-ok");
    if (decoded.image.HasStorage())
    {
        Expect(decoded.image.Width() == 32 && decoded.image.Height() == 64, "orientation.dimensions-swapped");
    }

    // No orientation tag (1, identity): unrotated, as a plain image always was.
    const std::wstring plain = WriteOrientedFixture(L"unoriented.tif", 64, 32, 1, nullptr);
    DecodedImage plainDecoded{};
    Expect(ImportImageFile(plain.c_str(), kMaxLayerPixels, plainDecoded) == ImageCodecStatus::Ok &&
               plainDecoded.image.Width() == 64 && plainDecoded.image.Height() == 32,
           "orientation.identity-unrotated");

    // RedPrism's own project assets never carry orientation and must not be rotated by DecodeProjectPngFile; the
    // gate is `requirePng` (only ImportImageFile passes false), proven structurally since a PNG round trip through
    // EncodePngFile/DecodeProjectPngFile never changes dimensions anywhere else in this file (png.thumbnail etc).

    // Color management: a real embedded ICC profile (the system's own sRGB one — the only profile guaranteed
    // present on any machine) takes the code through IWICColorContext/IWICColorTransform; sRGB -> sRGB is near
    // identity, so the corner colors survive, proving the transform wiring itself (context creation, the
    // straight-alpha intermediate format, Initialize's argument order) is correct and does not fail or crash.
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    const std::wstring iccPath =
        (length > 0 && length < MAX_PATH)
            ? std::wstring(systemDirectory) + L"\\spool\\drivers\\color\\sRGB Color Space Profile.icm"
            : std::wstring{};
    if (!iccPath.empty() && std::filesystem::exists(iccPath))
    {
        const std::wstring profiled = WriteOrientedFixture(L"profiled.tif", 4, 4, 1, iccPath.c_str());
        Expect(!profiled.empty(), "orientation.icc-fixture-written");
        DecodedImage profiledDecoded{};
        Expect(ImportImageFile(profiled.c_str(), kMaxLayerPixels, profiledDecoded) == ImageCodecStatus::Ok &&
                   profiledDecoded.image.HasStorage() && profiledDecoded.image.Width() == 4 &&
                   profiledDecoded.image.Height() == 4,
               "orientation.icc-import-ok");
        if (profiledDecoded.image.HasStorage())
        {
            const std::uint8_t* topLeft = profiledDecoded.image.Data();
            Expect(topLeft[0] > 200 && topLeft[2] > 200, "orientation.icc-srgb-round-trip-close");
        }
    }
    else
    {
        std::printf("SKIP orientation.icc-fixture-written (no system sRGB profile)\n");
    }
}

// The EXIF Orientation tag's definition (ISO/EXIF: "Row 0 is at the visual <edge>, Column 0 is at the visual
// <edge>") turned into a direct display-pixel -> source-pixel map, independent of TransformForOrientation /
// IWICBitmapFlipRotator, so it is a genuine oracle rather than re-checking this file's own logic against itself.
// Orientations 5-8 swap which source axis maps to which display axis; 5 (transpose) and 7 (transverse) also each
// combine a mirror with a 90-degree rotation, so they are the cases most likely to come out swapped depending on
// the order an implementation applies "flip" and "rotate" in.
void SourceCoordinateForDisplay(int orientation, int srcWidth, int srcHeight, int displayX, int displayY, int& srcX,
                                int& srcY) noexcept
{
    switch (orientation)
    {
    case 2: // Mirror horizontal.
        srcX = srcWidth - 1 - displayX;
        srcY = displayY;
        return;
    case 3: // Rotate 180.
        srcX = srcWidth - 1 - displayX;
        srcY = srcHeight - 1 - displayY;
        return;
    case 4: // Mirror vertical.
        srcX = displayX;
        srcY = srcHeight - 1 - displayY;
        return;
    case 5: // Transpose (mirror across the top-left/bottom-right diagonal).
        srcX = displayY;
        srcY = displayX;
        return;
    case 6: // Rotate 90 clockwise.
        srcX = displayY;
        srcY = srcHeight - 1 - displayX;
        return;
    case 7: // Transverse (mirror across the other diagonal).
        srcX = srcWidth - 1 - displayY;
        srcY = srcHeight - 1 - displayX;
        return;
    case 8: // Rotate 270 clockwise (90 counterclockwise).
        srcX = srcWidth - 1 - displayY;
        srcY = displayX;
        return;
    default: // 1: identity.
        srcX = displayX;
        srcY = displayY;
        return;
    }
}

void TestImportAllOrientations()
{
    // All eight EXIF orientations, not just 6: a 3x2 source with a distinct color per pixel (so every position is
    // individually identifiable), checked against SourceCoordinateForDisplay's independent oracle.
    constexpr int srcWidth = 3;
    constexpr int srcHeight = 2;
    const std::uint8_t colors[srcHeight][srcWidth][3] = {{{10, 20, 30}, {40, 50, 60}, {70, 80, 90}},
                                                         {{100, 110, 120}, {130, 140, 150}, {160, 170, 180}}};
    const auto paint = [&colors](int x, int y, std::uint8_t* bgra)
    {
        bgra[0] = colors[y][x][0];
        bgra[1] = colors[y][x][1];
        bgra[2] = colors[y][x][2];
        bgra[3] = 255;
    };

    for (int orientation = 1; orientation <= 8; ++orientation)
    {
        const std::wstring name = L"orientation-" + std::to_wstring(orientation) + L".tif";
        const std::wstring path = WriteOrientedFixture(name.c_str(), srcWidth, srcHeight, orientation, nullptr, paint);
        char label[48];
        std::snprintf(label, sizeof(label), "orientation.all.%d-fixture-written", orientation);
        Expect(!path.empty(), label);

        DecodedImage decoded{};
        std::snprintf(label, sizeof(label), "orientation.all.%d-import-ok", orientation);
        Expect(ImportImageFile(path.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
                   decoded.image.HasStorage(),
               label);
        if (!decoded.image.HasStorage())
        {
            continue;
        }

        const bool swapped = orientation >= 5;
        const int expectedWidth = swapped ? srcHeight : srcWidth;
        const int expectedHeight = swapped ? srcWidth : srcHeight;
        std::snprintf(label, sizeof(label), "orientation.all.%d-dimensions", orientation);
        Expect(decoded.image.Width() == expectedWidth && decoded.image.Height() == expectedHeight, label);
        if (decoded.image.Width() != expectedWidth || decoded.image.Height() != expectedHeight)
        {
            continue;
        }

        bool everyPixelCorrect = true;
        for (int displayY = 0; displayY < expectedHeight && everyPixelCorrect; ++displayY)
        {
            for (int displayX = 0; displayX < expectedWidth; ++displayX)
            {
                int srcX = 0;
                int srcY = 0;
                SourceCoordinateForDisplay(orientation, srcWidth, srcHeight, displayX, displayY, srcX, srcY);
                const std::uint8_t* pixel =
                    decoded.image.Data() +
                    (static_cast<std::size_t>(displayY) * expectedWidth + static_cast<std::size_t>(displayX)) * 4;
                if (pixel[0] != colors[srcY][srcX][0] || pixel[1] != colors[srcY][srcX][1] ||
                    pixel[2] != colors[srcY][srcX][2])
                {
                    everyPixelCorrect = false;
                    std::printf("     orientation %d: display(%d,%d) expected src(%d,%d) = "
                                "(%u,%u,%u), got (%u,%u,%u)\n",
                                orientation, displayX, displayY, srcX, srcY, colors[srcY][srcX][0],
                                colors[srcY][srcX][1], colors[srcY][srcX][2], pixel[0], pixel[1], pixel[2]);
                    break;
                }
            }
        }
        std::snprintf(label, sizeof(label), "orientation.all.%d-every-pixel", orientation);
        Expect(everyPixelCorrect, label);
    }
}

[[nodiscard]] std::wstring MaterializeV7Fixture()
{
    const std::wstring dest = ScratchRoot() + L"\\v7-exploded";
    std::error_code error;
    std::filesystem::remove_all(dest, error);
    std::filesystem::create_directories(std::filesystem::path(dest) / L"images", error);
    std::filesystem::copy_file(std::filesystem::path(FixtureRoot()) / L"manifest.json",
                               std::filesystem::path(dest) / L"manifest.json",
                               std::filesystem::copy_options::overwrite_existing, error);
    const std::wstring images = dest + L"\\images\\";
    (void)EncodePngFile(SolidBgra(2, 2, 0, 0, 255, 255), 72,
                        (images + L"00000000-0000-4000-8000-000000000002.png").c_str());
    (void)EncodePngFile(Gray(2, 2, {255, 0, 128, 255}), 72,
                        (images + L"00000000-0000-4000-8000-000000000002.mask.png").c_str());
    (void)EncodePngFile(SolidBgra(2, 2, 255, 0, 0, 255), 72,
                        (images + L"00000000-0000-4000-8000-000000000004.png").c_str());
    return dest;
}

void ExpectFixtureSemantics(const CanvasDocument& document, const char* stage)
{
    const ImageLayer* red = FindLayer(document, L"Red");
    const ImageLayer* child = FindLayer(document, L"Child");
    const ImageLayer* hsv = FindLayer(document, L"Hue/Saturation");
    const ImageLayer* grain = FindLayer(document, L"Grain");
    const ImageLayer* group = FindLayer(document, L"Group");
    std::printf("-- %s\n", stage);
    Expect(document.layers.size() == 5 && red && child && hsv && grain && group, "v7.layers");
    Expect(Near(document.resolution, 300), "v7.resolution");
    if (!red || !child || !hsv || !grain || !group)
    {
        return;
    }
    Expect(red->blendMode == LayerBlendMode::Multiply && Near(red->opacity, 0.75), "v7.appearance");
    Expect(red->mask.has_value() && red->mask.value().enabled && !red->mask.value().linked, "v7.mask-linked");
    Expect(red->mask.has_value() && red->mask.value().placement.has_value() &&
               Near(red->mask.value().placement.value().origin.x, 1) && red->mask.value().placement.value().flipY &&
               red->mask.value().placement.value().sampling == LayerSampling::Nearest,
           "v7.mask-placement");
    Expect(red->mask.has_value() && RastersEqual(red->mask.value().asset.image, Gray(2, 2, {255, 0, 128, 255})),
           "v7.mask-pixels");
    Expect(group->isGroup && child->parentID == group->id, "v7.group");
    Expect(child->transform.flipX && Near(child->transform.rotation, 12.5) && Near(child->transform.origin.y, -0.25) &&
               child->transform.sampling == LayerSampling::Smooth,
           "v7.transform");
    Expect(child->shape.has_value() && child->shape.value().kind == ShapeKind::Ellipse &&
               Near(child->shape.value().cornerRadius, 3) && Near(child->shape.value().green, 0.5),
           "v7.shape");
    Expect(hsv->adjustment.has_value() && hsv->adjustment.value().kind == AdjustmentKind::Hsv, "v7.hsv-kind");
    if (hsv->adjustment.has_value())
    {
        const LayerAdjustment& adjustment = hsv->adjustment.value();
        Expect(adjustment.hsvSettings.has_value(), "v7.hsv-settings-present");
        if (adjustment.hsvSettings.has_value())
        {
            const HueSaturationSettings& settings = adjustment.hsvSettings.value();
            const RangeAdjustment& reds = settings.adjustments[static_cast<std::size_t>(ColorRange::Reds)];
            Expect(settings.range == ColorRange::Reds && settings.invertRange && !settings.colorize, "v7.hsv-range");
            Expect(Near(reds.hue, 10) && Near(reds.saturation, 20) && Near(reds.lightness, -5), "v7.hsv-reds");
            Expect(Near(settings.bands[static_cast<std::size_t>(ColorRange::Reds)].falloffStart, 315), "v7.hsv-band");
        }
        Expect(adjustment.levels.channel == LevelsChannel::Red && Near(adjustment.levels.ranges[1].white, 240) &&
                   Near(adjustment.levels.ranges[1].gamma, 1.2) && Near(adjustment.levels.ranges[1].outputWhite, 250),
               "v7.levels");
        Expect(adjustment.curves.channels[1].size() == 3 && Near(adjustment.curves.channels[1][1].y, 160), "v7.curves");
        Expect(!adjustment.exposureSettings.has_value() && !adjustment.grainSettings.has_value(),
               "v7.hsv-no-optional-payloads");
    }
    Expect(grain->adjustment.has_value() && grain->adjustment.value().kind == AdjustmentKind::Grain && !grain->visible,
           "v7.grain-kind");
    Expect(grain->maskSourceID.has_value() && grain->maskSourceID.value() == red->id, "v7.clipped-adjustment");
    if (grain->adjustment.has_value())
    {
        const LayerAdjustment& adjustment = grain->adjustment.value();
        Expect(adjustment.grainSettings.has_value() && adjustment.grainSettings.value().seed == 4000000009u &&
                   Near(adjustment.grainSettings.value().amount, 40) &&
                   Near(adjustment.grainSettings.value().size, 3) &&
                   Near(adjustment.grainSettings.value().roughness, 10),
               "v7.grain-settings");
        Expect(adjustment.exposureSettings.has_value() && Near(adjustment.exposureSettings.value().exposure, 1.5) &&
                   Near(adjustment.exposureSettings.value().gamma, 0.8) &&
                   Near(adjustment.exposureSettings.value().offset, -0.1),
               "v7.exposure-settings");
        Expect(adjustment.gradientMapSettings.has_value() && adjustment.gradientMapSettings.value().reversed &&
                   Near(adjustment.gradientMapSettings.value().shadows.red, 1) &&
                   Near(adjustment.gradientMapSettings.value().highlights.green, 0.5),
               "v7.gradient-map-settings");
    }
}

void TestExplodedGolden()
{
    const std::wstring fixture = MaterializeV7Fixture();
    const ProjectLoadResult loaded = LoadProject(fixture.c_str());
    std::printf("     fixture -> status %d\n", static_cast<int>(loaded.status));
    Expect(loaded.status == ProjectIoStatus::Ok, "v7.load");
    Expect(loaded.formatVersion == 7, "v7.version");
    Expect(loaded.document.width == 2 && loaded.document.height == 2, "v7.size");
    Expect(loaded.activeLayerID.has_value() && loaded.activeLayerID == FindLayer(loaded.document, L"Red")->id,
           "v7.active-layer");
    if (loaded.status != ProjectIoStatus::Ok)
    {
        return;
    }
    ExpectFixtureSemantics(loaded.document, "loaded fixture");

    const std::wstring exploded = ScratchRoot() + L"\\roundtrip-exploded";
    const std::wstring zipPath = ScratchRoot() + L"\\roundtrip.prism";
    Expect(SaveProject(exploded.c_str(), loaded.document, loaded.activeLayerID, false) == ProjectIoStatus::Ok,
           "v7.save-exploded");
    const ProjectLoadResult explodedAgain = LoadProject(exploded.c_str());
    Expect(explodedAgain.status == ProjectIoStatus::Ok, "v7.load-exploded");
    if (explodedAgain.status == ProjectIoStatus::Ok)
    {
        ExpectFixtureSemantics(explodedAgain.document, "exploded round trip");
    }
    const std::string json = ReadText(std::filesystem::path(exploded) / L"manifest.json");
    Expect(json.find("\"hsvSettings\"") != std::string::npos && json.find("\"grainSettings\"") != std::string::npos &&
               json.find("\"exposureSettings\"") != std::string::npos &&
               json.find("\"gradientMapSettings\"") != std::string::npos,
           "v7.manifest-keeps-optional-payloads");
    Expect(json.find("\"adjustments\": [") != std::string::npos && json.find("\"bands\": [") != std::string::npos,
           "v7.manifest-swift-dictionary-arrays");
    Expect(json.find("\"levels\"") != std::string::npos && json.find("\"curves\"") != std::string::npos,
           "v7.manifest-always-writes-levels-and-curves");
    Expect(json.find("\"maskPlacement\"") != std::string::npos &&
               json.find("\"maskLinked\": false") != std::string::npos,
           "v7.manifest-mask-placement");
    Expect(json.find("\"version\": 7") != std::string::npos, "v7.manifest-version");
    Expect(std::filesystem::exists(std::filesystem::path(exploded) / L"images" /
                                   L"00000000-0000-4000-8000-000000000002.mask.png"),
           "v7.mask-asset-written");

    Expect(SaveProject(zipPath.c_str(), loaded.document, loaded.activeLayerID, true) == ProjectIoStatus::Ok,
           "v7.save-zip");
    Expect(PathLooksLikeZipProject(zipPath.c_str()), "v7.zip-path");
    const std::string redEntry = "images/" + loaded.document.layers.front().id.ToUtf8() + ".png";
    Expect(ZipCompressionMethod(zipPath, "manifest.json") == ZIP_CM_DEFLATE, "v7.zip-manifest-deflated");
    Expect(ZipCompressionMethod(zipPath, redEntry.c_str()) == ZIP_CM_STORE, "v7.zip-png-stored");
    const ProjectLoadResult zipAgain = LoadProject(zipPath.c_str());
    Expect(zipAgain.status == ProjectIoStatus::Ok, "v7.load-zip");
    if (zipAgain.status == ProjectIoStatus::Ok)
    {
        ExpectFixtureSemantics(zipAgain.document, "zip round trip");
    }
    const std::wstring deflatedPath = ScratchRoot() + L"\\legacy-deflated.prism";
    Expect(RewriteZipDeflated(zipPath, deflatedPath), "v7.legacy-deflate-fixture-created");
    Expect(ZipCompressionMethod(deflatedPath, redEntry.c_str()) == ZIP_CM_DEFLATE, "v7.legacy-deflate-fixture-method");
    const ProjectLoadResult deflatedAgain = LoadProject(deflatedPath.c_str());
    Expect(deflatedAgain.status == ProjectIoStatus::Ok && deflatedAgain.document.layers.size() == 5,
           "v7.legacy-deflated-project-loads");
    // Overwriting a saved package drops assets that are no longer referenced.
    CanvasDocument trimmed = loaded.document;
    trimmed.layers.erase(trimmed.layers.begin());
    trimmed.layers.back().maskSourceID.reset();
    Expect(SaveProject(exploded.c_str(), trimmed, std::nullopt, false) == ProjectIoStatus::Ok, "v7.overwrite");
    Expect(!std::filesystem::exists(std::filesystem::path(exploded) / L"images" /
                                    L"00000000-0000-4000-8000-000000000002.png"),
           "v7.overwrite-drops-removed-assets");
}

void TestTransformArrayShape()
{
    // RF-102: Swift's CGPoint/CGSize Codable overlay (legacy `LayerTransform.origin`/`.size`) encodes as a
    // 2-element array, not the `{x,y}`/`{width,height}` object RedPrism used to write. The reader must accept the
    // Swift-shaped fixture, the writer must emit arrays, and the old object shape must still load (compat).
    const std::wstring dest = ScratchRoot() + L"\\v7-arrays";
    std::error_code error;
    std::filesystem::remove_all(dest, error);
    std::filesystem::create_directories(std::filesystem::path(dest) / L"images", error);
    std::filesystem::copy_file(std::filesystem::path(FixtureRootNamed(L"v7-arrays")) / L"manifest.json",
                               std::filesystem::path(dest) / L"manifest.json",
                               std::filesystem::copy_options::overwrite_existing, error);
    const std::wstring images = dest + L"\\images\\";
    (void)EncodePngFile(SolidBgra(2, 2, 9, 8, 7, 255), 72,
                        (images + L"00000000-0000-4000-8000-000000000012.png").c_str());
    (void)EncodePngFile(Gray(2, 2, {255, 255, 255, 255}), 72,
                        (images + L"00000000-0000-4000-8000-000000000012.mask.png").c_str());

    const ProjectLoadResult loaded = LoadProject(dest.c_str());
    Expect(loaded.status == ProjectIoStatus::Ok, "transform.array-shape-loads");
    if (loaded.status != ProjectIoStatus::Ok || loaded.document.layers.empty())
    {
        return;
    }
    const ImageLayer& layer = loaded.document.layers.front();
    Expect(Near(layer.transform.origin.x, 0) && Near(layer.transform.origin.y, 0) && Near(layer.transform.size.x, 2) &&
               Near(layer.transform.size.y, 2),
           "transform.array-shape-transform-values");
    Expect(layer.mask.has_value() && layer.mask.value().placement.has_value() &&
               Near(layer.mask.value().placement.value().origin.x, 1) &&
               Near(layer.mask.value().placement.value().origin.y, 0.5) &&
               Near(layer.mask.value().placement.value().size.x, 2) &&
               Near(layer.mask.value().placement.value().size.y, 3) && layer.mask.value().placement.value().flipY,
           "transform.array-shape-mask-placement-values");

    const std::wstring roundTrip = ScratchRoot() + L"\\v7-arrays-roundtrip";
    const auto saved = SaveProject(roundTrip.c_str(), loaded.document, loaded.activeLayerID, false);
    if (saved != ProjectIoStatus::Ok)
        std::fprintf(stderr, "Array writer status=%u win32=%lu path=%ls\n", static_cast<unsigned int>(saved),
                     GetLastError(), roundTrip.c_str());
    Expect(saved == ProjectIoStatus::Ok, "transform.writer-saves");
    const std::string json = ReadText(std::filesystem::path(roundTrip) / L"manifest.json");
    Expect(json.find("\"origin\": [") != std::string::npos && json.find("\"size\": [") != std::string::npos &&
               json.find("\"origin\": {") == std::string::npos && json.find("\"size\": {") == std::string::npos,
           "transform.writer-emits-arrays");
    const ProjectLoadResult reloaded = LoadProject(roundTrip.c_str());
    Expect(reloaded.status == ProjectIoStatus::Ok && !reloaded.document.layers.empty() &&
               reloaded.document.layers.front().mask.has_value() &&
               reloaded.document.layers.front().mask.value().placement.has_value() &&
               Near(reloaded.document.layers.front().mask.value().placement.value().origin.x, 1),
           "transform.array-shape-round-trip");

    // Compat: the older RedPrism object shape `{x,y}`/`{width,height}` still reads, for already-saved projects.
    const std::string objectShaped = MinimalManifest(
        7, "\"maskFile\": \"00000000-0000-4000-8000-000000000002.mask.png\", \"maskPlacement\": {\"flipX\": false, "
           "\"flipY\": false, \"origin\": {\"x\": 3, \"y\": 4}, \"rotation\": 0, \"sampling\": \"Nearest\", "
           "\"size\": {\"width\": 5, \"height\": 6}}");
    const std::wstring compatRoot = EmptyProjectRoot(L"transform-object-shape");
    WriteText(std::filesystem::path(compatRoot) / L"manifest.json", objectShaped);
    (void)EncodePngFile(Gray(1, 1, {255}), 72,
                        (compatRoot + L"\\images\\00000000-0000-4000-8000-000000000002.mask.png").c_str());
    const ProjectLoadResult compat = LoadProject(compatRoot.c_str());
    Expect(compat.status == ProjectIoStatus::Ok && !compat.document.layers.empty() &&
               compat.document.layers.front().mask.has_value() &&
               compat.document.layers.front().mask.value().placement.has_value() &&
               Near(compat.document.layers.front().mask.value().placement.value().origin.x, 3) &&
               Near(compat.document.layers.front().mask.value().placement.value().size.x, 5),
           "transform.object-shape-still-reads");
}

void TestShapeLiveness()
{
    // RF-103: `shape` is written only while the layer is still live (its asset is still the raster the shape
    // drew); once the pixels are painted over, `shapeImage` no longer matches `asset.image`, and the shape must
    // not round trip as live (legacy EditorSession+Projects.swift writes `shape: layer.liveShape?.style`).
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 2;
    ImageLayer layer = ImageLayer::Blank(L"Shape", Vec2{2, 2});
    LayerShapeStyle style{};
    style.kind = ShapeKind::Rectangle;
    style.red = 1;
    style.green = 0;
    style.blue = 0;
    layer.shape = style;
    const Raster shapePixels = SolidBgra(2, 2, 0, 0, 255, 255);
    layer.asset = RasterAsset{shapePixels, Raster::MakeThumbnail(shapePixels), layer.name};
    layer.shapeImage = shapePixels; // Live: the asset is exactly what the shape drew.
    document.layers.push_back(layer);

    const std::wstring livePath = ScratchRoot() + L"\\shape-live";
    Expect(SaveProject(livePath.c_str(), document, std::nullopt, false) == ProjectIoStatus::Ok, "shape.live-saves");
    const std::string liveJson = ReadText(std::filesystem::path(livePath) / L"manifest.json");
    Expect(liveJson.find("\"shape\"") != std::string::npos, "shape.live-writes-shape");
    const ProjectLoadResult liveLoaded = LoadProject(livePath.c_str());
    Expect(liveLoaded.status == ProjectIoStatus::Ok && !liveLoaded.document.layers.empty() &&
               liveLoaded.document.layers.front().shape.has_value(),
           "shape.live-round-trip");

    // Paint over it: a different raster identity in `asset`, same shape metadata still attached, as BrushStroke's
    // paint commit leaves it (it never clears `shape`, only `shapeImage` stops matching `asset`).
    document.layers.back().asset =
        RasterAsset{SolidBgra(2, 2, 9, 9, 9, 255), Raster::MakeThumbnail(shapePixels), layer.name};
    const std::wstring paintedPath = ScratchRoot() + L"\\shape-painted";
    Expect(SaveProject(paintedPath.c_str(), document, std::nullopt, false) == ProjectIoStatus::Ok,
           "shape.painted-saves");
    const std::string paintedJson = ReadText(std::filesystem::path(paintedPath) / L"manifest.json");
    Expect(paintedJson.find("\"shape\"") == std::string::npos, "shape.painted-not-written");
    const ProjectLoadResult paintedLoaded = LoadProject(paintedPath.c_str());
    Expect(paintedLoaded.status == ProjectIoStatus::Ok && !paintedLoaded.document.layers.empty() &&
               !paintedLoaded.document.layers.front().shape.has_value(),
           "shape.painted-loads-not-live");
}

void TestExplodedReparseAndHostileZip()
{
    const std::wstring root = L"C:\\proj";
    const std::wstring file = root + L"\\images\\a.png";
    const auto plain = [](const std::wstring&) -> std::uint32_t { return 0x20u; };
    Expect(ExplodedMemberPathAllowed(root, file, plain), "path.plain-allowed");
    const auto junction = [](const std::wstring& path) -> std::uint32_t
    { return path.find(L".png") == std::wstring::npos ? 0x410u : 0x20u; };
    Expect(!ExplodedMemberPathAllowed(root, file, junction), "path.intermediate-reparse");
    Expect(!ExplodedMemberPathAllowed(root, root + L"\\images\\..\\secret.png", plain), "path.dotdot-component");

    const std::wstring project = ScratchRoot() + L"\\junction-project";
    const std::wstring outside = ScratchRoot() + L"\\junction-outside";
    const std::wstring link = project + L"\\images";
    std::error_code error;
    RemoveDirectoryW(link.c_str());
    std::filesystem::remove_all(project, error);
    std::filesystem::remove_all(outside, error);
    std::filesystem::create_directories(outside, error);
    std::filesystem::create_directories(project, error);
    WriteText(std::filesystem::path(project) / L"manifest.json",
              MinimalManifest(7, "\"imageFile\": \"00000000-0000-4000-8000-000000000002.png\""));
    const DWORD linkFlags = SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkW(link.c_str(), outside.c_str(), linkFlags))
    {
        Expect(LoadProject(project.c_str()).status == ProjectIoStatus::Invalid, "path.junction-load");
        RemoveDirectoryW(link.c_str());
    }
    else
    {
        std::printf("SKIP path.junction-load (CreateSymbolicLinkW %lu)\n", GetLastError());
    }

    const std::wstring emptyPath = ScratchRoot() + L"\\empty.prism";
    FILE* emptyFile = nullptr;
    Expect(_wfopen_s(&emptyFile, emptyPath.c_str(), L"wb") == 0 && emptyFile != nullptr, "zip.empty-create");
    if (emptyFile != nullptr)
    {
        (void)fclose(emptyFile);
    }
    const ProjectLoadResult empty = LoadProject(emptyPath.c_str());
    Expect(empty.status == ProjectIoStatus::Invalid && empty.document.layers.empty(), "zip.empty");
    WriteText(std::filesystem::path(ScratchRoot()) / L"garbage.prism", "not-a-zip");
    Expect(LoadProject((ScratchRoot() + L"\\garbage.prism").c_str()).status == ProjectIoStatus::Invalid, "zip.garbage");

    const ProjectLoadResult loaded = LoadProject(MaterializeV7Fixture().c_str());
    const std::wstring zipPath = ScratchRoot() + L"\\truncated.prism";
    Expect(loaded.status == ProjectIoStatus::Ok &&
               SaveProject(zipPath.c_str(), loaded.document, loaded.activeLayerID, true) == ProjectIoStatus::Ok,
           "zip.truncate-save");
    std::ifstream input(zipPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    if (bytes.size() > 64)
    {
        bytes.resize(bytes.size() - 64);
        std::ofstream output(zipPath, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const ProjectLoadResult truncated = LoadProject(zipPath.c_str());
    Expect((truncated.status == ProjectIoStatus::Invalid || truncated.status == ProjectIoStatus::MissingImage) &&
               truncated.document.layers.empty(),
           "zip.truncated");
}

void TestPathTraversal()
{
    const std::string evil = MinimalManifest(7, "\"imageFile\": \"../secret.png\"");
    Expect(LoadManifestText(L"traverse", evil) == ProjectIoStatus::Invalid, "path.traversal");
    const std::string wrongName = MinimalManifest(7, "\"imageFile\": \"00000000-0000-4000-8000-000000000009.png\"");
    Expect(LoadManifestText(L"wrong-name", wrongName) == ProjectIoStatus::Invalid, "path.image-name-mismatch");

    // RF-110: ids are read in either case (IO_ProjectFormat.md); a hex-letter id (Generate() rarely rolls one)
    // matched by a lowercase filename must be ACCEPTED by the manifest parser and reach asset loading, where it
    // fails for the unrelated, expected reason that no such file exists (MissingImage, not Invalid).
    const std::string lowerCaseImage =
        MinimalManifest(7, "\"id\": \"AABBCCDD-EEFF-4A99-8ABC-AABBCCDDEEFF\", "
                           "\"imageFile\": \"aabbccdd-eeff-4a99-8abc-aabbccddeeff.png\"");
    Expect(LoadManifestText(L"lowercase-image", lowerCaseImage) == ProjectIoStatus::MissingImage,
           "path.image-name-case-insensitive");
}

void TestManifestTooLarge()
{
    const std::wstring root = EmptyProjectRoot(L"huge");
    const std::filesystem::path manifestPath = std::filesystem::path(root) / L"manifest.json";
    FILE* file = nullptr;
    if (_wfopen_s(&file, manifestPath.c_str(), L"wb") == 0 && file)
    {
        const std::string start = "{\"format\":\"com.redprism.project\",\"pad\":\"";
        (void)fwrite(start.data(), 1, start.size(), file);
        std::vector<char> pad(4 * 1024 * 1024, 'x');
        (void)fwrite(pad.data(), 1, pad.size(), file);
        const std::string end = "\"}";
        (void)fwrite(end.data(), 1, end.size(), file);
        (void)fclose(file);
    }
    const ProjectLoadResult loaded = LoadProject(root.c_str());
    Expect(loaded.status == ProjectIoStatus::TooLarge, "manifest.4mib");
}

void TestVersionGatesAndRejects()
{
    Expect(LoadManifestText(L"v42", MinimalManifest(42, {})) == ProjectIoStatus::UnsupportedVersion,
           "reject.future-version");
    Expect(LoadManifestText(L"v0", MinimalManifest(0, {})) == ProjectIoStatus::UnsupportedVersion, "reject.version-0");
    Expect(LoadManifestText(L"corrupt", "not json") == ProjectIoStatus::Invalid, "reject.corrupt-json");
    // A manifest that is valid except for `format`, so this can only pass because the format check specifically
    // fires; the old `||` fallback (an unrelated, separately-invalid manifest) could mask a broken format check.
    Expect(LoadManifestText(L"format", MinimalManifest(7, {}, "\"format\": \"other\"")) == ProjectIoStatus::Invalid,
           "reject.format");
    Expect(LoadManifestText(L"colorspace", MinimalManifest(7, {}, "\"colorSpace\": \"Display P3\"")) ==
               ProjectIoStatus::Invalid,
           "reject.color-space");
    Expect(LoadManifestText(L"v1-ok", MinimalManifest(1, {})) == ProjectIoStatus::Ok, "accept.v1");
    Expect(LoadManifestText(L"v1-group", MinimalManifest(1, "\"isGroup\": true")) == ProjectIoStatus::Invalid,
           "reject.v1-group");
    Expect(LoadManifestText(L"v2-blend", MinimalManifest(2, "\"blendMode\": \"Multiply\"")) == ProjectIoStatus::Invalid,
           "reject.v2-blend");
    Expect(LoadManifestText(L"v3-blend", MinimalManifest(3, "\"blendMode\": \"Multiply\"")) == ProjectIoStatus::Ok,
           "accept.v3-blend");
    Expect(LoadManifestText(L"v3-mask",
                            MinimalManifest(3, "\"maskFile\": \"00000000-0000-4000-8000-000000000002.mask.png\"")) ==
               ProjectIoStatus::Invalid,
           "reject.v3-mask");
    // Clips to a different, valid sibling (not itself) so only the version gate can reject it; reject.self-clip
    // below covers the self-cycle separately.
    Expect(LoadManifestText(L"v4-clip", ManifestWithMaskSourceSibling(4)) == ProjectIoStatus::Invalid,
           "reject.v4-clip");
    Expect(
        LoadManifestText(
            L"v5-group-mask",
            MinimalManifest(5, "\"isGroup\": true, \"maskFile\": \"00000000-0000-4000-8000-000000000002.mask.png\"")) ==
            ProjectIoStatus::Invalid,
        "reject.v5-folder-mask");
    Expect(LoadManifestText(L"v6-adjust", MinimalManifest(6, "\"adjustment\": {\"kind\": \"Levels\"}")) ==
               ProjectIoStatus::Invalid,
           "reject.v6-adjustment");
    Expect(LoadManifestText(L"v7-adjust", MinimalManifest(7, "\"adjustment\": {\"kind\": \"Levels\"}")) ==
               ProjectIoStatus::Ok,
           "accept.v7-minimal-adjustment");
    Expect(LoadManifestText(
               L"v7-adjust-bad",
               MinimalManifest(7, "\"adjustment\": {\"kind\": \"Levels\", \"exposureSettings\": {\"gamma\": 0}}")) ==
               ProjectIoStatus::Invalid,
           "reject.invalid-adjustment-payload");
    Expect(LoadManifestText(L"v7-adjust-kind", MinimalManifest(7, "\"adjustment\": {\"kind\": \"Vibrance\"}")) ==
               ProjectIoStatus::Invalid,
           "reject.unknown-adjustment-kind");
    Expect(LoadManifestText(L"v7-adjust-group",
                            MinimalManifest(7, "\"isGroup\": true, \"adjustment\": {\"kind\": \"Levels\"}")) ==
               ProjectIoStatus::Invalid,
           "reject.adjustment-on-group");
    Expect(LoadManifestText(L"group-opacity", MinimalManifest(7, "\"isGroup\": true, \"opacity\": 0.5")) ==
               ProjectIoStatus::Invalid,
           "reject.group-opacity");
    Expect(LoadManifestText(L"mask-enabled-no-file", MinimalManifest(7, "\"maskEnabled\": true")) ==
               ProjectIoStatus::Invalid,
           "reject.mask-enabled-without-file");
    Expect(
        LoadManifestText(
            L"mask-placement-no-file",
            MinimalManifest(
                7, "\"maskPlacement\": {\"origin\": {\"x\": 0, \"y\": 0}, \"size\": {\"width\": 1, \"height\": 1}}")) ==
            ProjectIoStatus::Invalid,
        "reject.mask-placement-without-file");
    Expect(LoadManifestText(L"missing-parent",
                            MinimalManifest(7, "\"parentID\": \"00000000-0000-4000-8000-000000000099\"")) ==
               ProjectIoStatus::Invalid,
           "reject.missing-parent");
    Expect(LoadManifestText(L"self-clip",
                            MinimalManifest(7, "\"maskSourceID\": \"00000000-0000-4000-8000-000000000002\"")) ==
               ProjectIoStatus::Invalid,
           "reject.self-clip");
    Expect(LoadManifestText(L"empty-name", MinimalManifest(7, "\"name\": \"   \"")) == ProjectIoStatus::Invalid,
           "reject.blank-name");
    Expect(LoadManifestText(
               L"bad-transform",
               MinimalManifest(
                   7, "\"transform\": {\"origin\": {\"x\": 0, \"y\": 0}, \"size\": {\"width\": 0, \"height\": 1}}")) ==
               ProjectIoStatus::Invalid,
           "reject.zero-size-transform");
    Expect(LoadManifestText(L"active-missing",
                            MinimalManifest(7, {}, "\"activeLayerID\": \"00000000-0000-4000-8000-000000000077\"")) ==
               ProjectIoStatus::Invalid,
           "reject.active-layer-missing");
    Expect(LoadManifestText(L"bad-resolution", MinimalManifest(7, {}, "\"resolution\": 0")) == ProjectIoStatus::Invalid,
           "reject.resolution-range");
    Expect(LoadManifestText(L"missing-image",
                            MinimalManifest(7, "\"imageFile\": \"00000000-0000-4000-8000-000000000002.png\"")) ==
               ProjectIoStatus::MissingImage,
           "reject.missing-image");
}

void TestImportOntoDocument()
{
    const std::wstring path = ScratchRoot() + L"\\import.png";
    Expect(EncodePngFile(SolidBgra(6, 3, 10, 20, 30, 255), 72, path.c_str()) == ImageCodecStatus::Ok, "import.fixture");
    CanvasDocument document;
    document.width = 100;
    document.height = 100;
    std::optional<Guid> active;
    Expect(!PathLooksLikeZipProject(path.c_str()), "import.png-is-not-a-project");
    Expect(ImportImageOnto(document, path.c_str(), active) == ProjectIoStatus::Ok, "import.ok");
    Expect(document.layers.size() == 1 && document.width == 6 && document.height == 3, "import.empty-adopts-size");
    Expect(active.has_value() && document.layers[0].id == active.value() && document.layers[0].name == L"import",
           "import.active-layer");
    Expect(document.layers[0].asset.has_value() && document.layers[0].asset.value().image.Width() == 6 &&
               Near(document.layers[0].transform.size.x, 6) && Near(document.layers[0].transform.size.y, 3),
           "import.transform");
    Expect(ImportImageOnto(document, path.c_str(), active) == ProjectIoStatus::Ok && document.width == 6 &&
               document.layers.size() == 2,
           "import.second-keeps-size");
    Expect(ImportImageOnto(document, L"Z:\\no\\such.png", active) == ProjectIoStatus::MissingImage, "import.missing");

    // RF-105: onto a non-empty document, the import is centered on the canvas and nested above the active layer
    // inside its folder (legacy EditorSession.insert), not appended at the root at (0,0).
    CanvasDocument nested;
    nested.id = Guid::Generate();
    nested.width = 200;
    nested.height = 100;
    ImageLayer folder = ImageLayer::Blank(L"Folder", Vec2{200, 100});
    folder.isGroup = true;
    nested.layers.push_back(folder);
    std::optional<Guid> nestedActive = folder.id;
    Expect(ImportImageOnto(nested, path.c_str(), nestedActive) == ProjectIoStatus::Ok, "import.nested-ok");
    Expect(nested.layers.size() == 2 && nested.layers.back().parentID.has_value() &&
               nested.layers.back().parentID.value() == folder.id,
           "import.nested-into-active-folder");
    Expect(Near(nested.layers.back().transform.origin.x, std::floor(100.0 - 6.0 / 2.0)) &&
               Near(nested.layers.back().transform.origin.y, std::floor(50.0 - 3.0 / 2.0)),
           "import.centered-on-canvas");
    Expect(nestedActive.has_value() && nestedActive.value() == nested.layers.back().id, "import.nested-active-layer");

    // RF-1106: a drop point (document pixels) centers the import there instead (legacy insert(_:centeredAt:)); into
    // an empty document the image is the canvas and the point does not apply.
    Expect(ImportImageOnto(nested, path.c_str(), nestedActive, Vec2{30.5, 70.0}) == ProjectIoStatus::Ok &&
               Near(nested.layers.back().transform.origin.x, std::floor(30.5 - 6.0 / 2.0)) &&
               Near(nested.layers.back().transform.origin.y, std::floor(70.0 - 3.0 / 2.0)),
           "import.centered-on-drop-point");
    CanvasDocument dropped;
    std::optional<Guid> droppedActive;
    Expect(ImportImageOnto(dropped, path.c_str(), droppedActive, Vec2{500.0, 500.0}) == ProjectIoStatus::Ok &&
               dropped.width == 6 && dropped.height == 3 && Near(dropped.layers.back().transform.origin.x, 0.0) &&
               Near(dropped.layers.back().transform.origin.y, 0.0),
           "import.drop-point-ignored-for-a-new-canvas");

    // RF-105: a budget failure pops the layer, restores the previous active layer, and reports TooLarge (not
    // Invalid). `Raster::MakeSparse` with an empty base and no tiles reports 100,000,000 pixels for the budget
    // check without allocating a 400 MB buffer.
    CanvasDocument full;
    full.id = Guid::Generate();
    full.width = 100;
    full.height = 100;
    ImageLayer huge = ImageLayer::Blank(L"Huge", Vec2{10000, 10000});
    const Raster hugeRaster = Raster::MakeSparse(10000, 10000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    huge.asset = RasterAsset{hugeRaster, Raster{}, huge.name};
    full.layers.push_back(huge);
    std::optional<Guid> fullActive = huge.id;
    Expect(ImportImageOnto(full, path.c_str(), fullActive) == ProjectIoStatus::TooLarge, "import.budget-too-large");
    Expect(full.layers.size() == 1 && fullActive.has_value() && fullActive.value() == huge.id,
           "import.budget-failure-restores-active-layer");
}

void TestSaveRejectsInvalidDocument()
{
    CanvasDocument bad;
    bad.width = 0;
    bad.height = 1;
    const std::wstring path = ScratchRoot() + L"\\invalid.prism";
    std::error_code error;
    std::filesystem::remove(path, error);
    Expect(SaveProject(path.c_str(), bad, std::nullopt, true) == ProjectIoStatus::Invalid, "save.rejects-invalid");
    Expect(!std::filesystem::exists(path), "save.no-partial-file");

    CanvasDocument good;
    good.width = 3;
    good.height = 2;
    good.id = Guid::Generate();
    good.layers.push_back(ImageLayer::Blank(L"Blank", Vec2{3, 2}));
    Expect(SaveProject(path.c_str(), good, std::nullopt, true) == ProjectIoStatus::Ok, "save.blank-layer");
    const ProjectLoadResult loaded = LoadProject(path.c_str());
    Expect(loaded.status == ProjectIoStatus::Ok && loaded.document.layers.size() == 1 &&
               !loaded.document.layers[0].asset.has_value() && loaded.document.id == good.id,
           "save.blank-layer-round-trip");
    // A failed overwrite keeps the previous archive intact.
    Expect(SaveProject(path.c_str(), bad, std::nullopt, true) == ProjectIoStatus::Invalid, "save.failed-overwrite");
    Expect(LoadProject(path.c_str()).status == ProjectIoStatus::Ok, "save.previous-archive-kept");
}

void TestProjectSaveDurability()
{
    CanvasDocument document;
    document.width = 2;
    document.height = 2;
    document.id = Guid::Generate();
    ImageLayer layer = ImageLayer::Blank(L"Durability", Vec2{2, 2});
    const Raster pixels = SolidBgra(2, 2, 7, 19, 31, 255);
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), layer.name};
    document.layers.push_back(layer);

    const std::wstring zipPath = ScratchRoot() + L"\\durability.prism";
    const std::wstring zipSentinel = zipPath + L".~sentinel";
    WriteText(zipSentinel, "keep this unrelated file");
    Expect(SaveProject(zipPath.c_str(), document, std::nullopt, true) == ProjectIoStatus::Ok,
           "save-durability.zip-first-save");
    Expect(SaveProject(zipPath.c_str(), document, std::nullopt, true) == ProjectIoStatus::Ok,
           "save-durability.zip-overwrite");
    Expect(ReadText(zipSentinel) == "keep this unrelated file", "save-durability.zip-keeps-unowned-temp");
    Expect(ZipCompressionMethod(zipPath, "manifest.json") == ZIP_CM_DEFLATE &&
               ZipCompressionMethod(zipPath, ("images/" + layer.id.ToUtf8() + ".png").c_str()) == ZIP_CM_STORE,
           "save-durability.zip-compression-policy");

    const std::wstring folderPath = ScratchRoot() + L"\\durability-folder";
    const std::filesystem::path folderSentinel = folderPath + L".~sentinel";
    std::error_code error;
    std::filesystem::create_directories(folderSentinel, error);
    WriteText(folderSentinel / L"keep.txt", "keep this unrelated directory");
    Expect(SaveProject(folderPath.c_str(), document, std::nullopt, false) == ProjectIoStatus::Ok,
           "save-durability.folder-first-save");
    const auto rotated = SaveProject(folderPath.c_str(), document, std::nullopt, false);
    if (rotated != ProjectIoStatus::Ok)
        std::fprintf(stderr, "Folder rotation status=%u win32=%lu path=%ls\n", static_cast<unsigned int>(rotated),
                     GetLastError(), folderPath.c_str());
    Expect(rotated == ProjectIoStatus::Ok, "save-durability.folder-rotation");
    Expect(ReadText(folderSentinel / L"keep.txt") == "keep this unrelated directory",
           "save-durability.folder-keeps-unowned-staging");
    const ProjectLoadResult folderReloaded = LoadProject(folderPath.c_str());
    Expect(folderReloaded.status == ProjectIoStatus::Ok && folderReloaded.document.id == document.id,
           "save-durability.folder-round-trip");

    bool unexpectedTemporary = false;
    const std::filesystem::path scratch = ScratchRoot();
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(scratch, error))
    {
        const std::wstring name = entry.path().filename().wstring();
        if ((name.starts_with(L"durability.prism.~") && name != L"durability.prism.~sentinel") ||
            (name.starts_with(L"durability-folder.~") && name != L"durability-folder.~sentinel"))
        {
            unexpectedTemporary = true;
        }
    }
    Expect(!unexpectedTemporary, "save-durability.owned-temporaries-cleaned");

    const std::string savedArchive = ReadText(zipPath);
    document.resolution = 144; // a successful replacement would observably change the previous package
    HANDLE lockedRaw = CreateFileW(zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    wil::unique_hfile locked{lockedRaw};
    Expect(static_cast<bool>(locked), "save-durability.lock-existing-archive");
    ProjectIoStatus blockedSave = ProjectIoStatus::Ok;
    if (locked)
    {
        blockedSave = SaveProject(zipPath.c_str(), document, std::nullopt, true);
    }
    locked.reset();
    Expect(blockedSave == ProjectIoStatus::Encode, "save-durability.replace-failure-reported");
    const ProjectLoadResult preserved = LoadProject(zipPath.c_str());
    Expect(preserved.status == ProjectIoStatus::Ok && preserved.document.id == document.id &&
               preserved.document.resolution != document.resolution && ReadText(zipPath) == savedArchive,
           "save-durability.replace-failure-preserves-destination");
    Expect(ReadText(zipSentinel) == "keep this unrelated file", "save-durability.failure-keeps-unowned-temp");
    bool failedTempLeft = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(scratch, error))
    {
        const std::wstring name = entry.path().filename().wstring();
        failedTempLeft =
            failedTempLeft || (name.starts_with(L"durability.prism.~") && name != L"durability.prism.~sentinel");
    }
    Expect(!failedTempLeft, "save-durability.failed-owned-temporary-cleaned");

    std::filesystem::remove(zipSentinel, error);
    std::filesystem::remove_all(folderSentinel, error);
}
} // namespace

// ── Settings store (C14) ──────────────────────────────────────

[[nodiscard]] std::string FileText(const std::wstring& path)
{
    std::string bytes;
    return SettingsStore::ReadFileBytes(path, bytes) ? bytes : std::string{};
}

void TestSettingsStore()
{
    Expect(std::wstring_view{SettingsStore::FileName(true)} == L"settings.debug.json" &&
               std::wstring_view{SettingsStore::FileName(false)} == L"settings.json",
           "settings.file-names");
    const std::wstring directory = SettingsStore::DefaultDirectory();
    Expect(directory.ends_with(L"\\RedPrism") && SettingsStore::DefaultPath(false) == directory + L"\\settings.json",
           "settings.default-paths");

    const std::optional<AppSettings> minimal = SettingsStore::Parse("{\"schema\":1}");
    Expect(minimal.has_value() && minimal.value() == AppSettings{}, "settings.parse-minimal-is-defaults");
    const std::optional<AppSettings> full = SettingsStore::Parse(
        R"({"schema":1,"theme":"dark","language":"fr-FR","layersPanelWidth":300.5,"sampleRing":false,)"
        R"("jpegQuality":0.65,"window":{"x":10,"y":20,"width":800,"height":600,"maximized":true},"future":[1,2]})");
    Expect(full.has_value() && full->theme == ThemePreference::Dark && full->language == L"fr-FR" &&
               full->layersPanelWidth == 300.5 && !full->sampleRing && full->jpegQuality == 0.65 &&
               full->window == WindowPlacementSetting{true, 10, 20, 800, 600, true} && full->shortcuts.empty(),
           "settings.parse-fields");
    const std::optional<AppSettings> chords = SettingsStore::Parse(
        R"({"schema":1,"shortcuts":[{"commandId":"cmd/edit/swapPalette","vk":"Q"},{"commandId":"swap","vk":"Z"},)"
        R"({"commandId":"cmd/view/zoomIn","keyPosition":"numberRowPlus","ctrl":true},)"
        R"({"commandId":"cmd/edit/delete","vk":"Delete","shift":true},)"
        R"({"commandId":"cmd/edit/undo","vk":"Z","keyPosition":"bracketLeft"},{"command":1}]})");
    Expect(chords.has_value() && chords->shortcuts.size() == 3 &&
               chords->shortcuts[0].commandId == L"cmd/edit/swapPalette" &&
               chords->shortcuts[0].chord.virtualKey == static_cast<std::uint16_t>('Q') &&
               !chords->shortcuts[0].chord.ctrl && chords->shortcuts[1].commandId == L"cmd/view/zoomIn" &&
               chords->shortcuts[1].chord.position == KeyPosition::NumberRowPlus && chords->shortcuts[1].chord.ctrl &&
               chords->shortcuts[2].commandId == L"cmd/edit/delete" &&
               chords->shortcuts[2].chord.virtualKey == VK_DELETE && chords->shortcuts[2].chord.shift,
           "settings.parse-shortcuts");
    const std::string chordText = SettingsStore::Serialize(chords.value());
    const std::optional<AppSettings> chordBack = SettingsStore::Parse(chordText);
    Expect(chordBack.has_value() && chordBack->shortcuts == chords->shortcuts &&
               chordText.find("\"shortcuts\"") != std::string::npos,
           "settings.shortcuts-round-trip");
    // Named keys and positions read in any case, as letters and F-keys do.
    const std::optional<AppSettings> cased = SettingsStore::Parse(
        R"({"schema":1,"shortcuts":[{"commandId":"cmd/edit/delete","vk":"delete"},)"
        R"({"commandId":"cmd/view/zoomIn","vk":"NUMPADPLUS","ctrl":true},)"
        R"({"commandId":"cmd/brush/smaller","keyPosition":"BracketLeft"},{"commandId":"cmd/file/save","vk":"f5"}]})");
    Expect(cased.has_value() && cased->shortcuts.size() == 4 && cased->shortcuts[0].chord.virtualKey == VK_DELETE &&
               cased->shortcuts[1].chord.virtualKey == VK_ADD &&
               cased->shortcuts[2].chord.position == KeyPosition::BracketLeft &&
               cased->shortcuts[3].chord.virtualKey == VK_F5,
           "settings.shortcut-names-ignore-case");
    // A save keeps the `shortcuts` entries this version cannot read, and the unknown fields of those it can, both
    // when the bindings are unchanged and when they change.
    const std::string withUnknown =
        R"({"schema":1,"shortcuts":[{"commandId":"cmd/edit/swapPalette","vk":"Q","note":"mine"},)"
        R"({"commandId":"cmd/edit/undo","vk":"Hyper"},{"commandId":"cmd/view/zoomIn","vk":"K","ctrl":true}]})";
    const std::optional<AppSettings> unknownParsed = SettingsStore::Parse(withUnknown);
    const std::string keptText =
        unknownParsed.has_value() ? SettingsStore::Serialize(unknownParsed.value(), withUnknown) : std::string{};
    AppSettings fewer = unknownParsed.value_or(AppSettings{});
    if (!fewer.shortcuts.empty())
    {
        fewer.shortcuts.pop_back();
    }
    fewer.shortcuts.push_back(ShortcutBinding{L"cmd/tool/brush", Key('K')});
    const std::string changedText = SettingsStore::Serialize(fewer, withUnknown);
    const std::optional<AppSettings> changedBack = SettingsStore::Parse(changedText);
    Expect(unknownParsed.has_value() && unknownParsed->shortcuts.size() == 2 &&
               keptText.find("\"note\": \"mine\"") != std::string::npos &&
               keptText.find("\"Hyper\"") != std::string::npos &&
               changedText.find("\"note\": \"mine\"") != std::string::npos &&
               changedText.find("\"Hyper\"") != std::string::npos &&
               changedText.find("cmd/view/zoomIn") == std::string::npos && changedBack.has_value() &&
               changedBack->shortcuts == fewer.shortcuts,
           "settings.save-keeps-unread-shortcut-entries");
    // A UTF-8 byte order mark is still a settings document, at a cold start too.
    const std::string bom = "\xEF\xBB\xBF{\"schema\":1,\"theme\":\"light\",\"extra\":7}";
    const std::optional<AppSettings> bomParsed = SettingsStore::Parse(bom);
    Expect(bomParsed.has_value() && bomParsed->theme == ThemePreference::Light &&
               SettingsStore::Serialize(AppSettings{}, bom).find("\"extra\": 7") != std::string::npos,
           "settings.parse-accepts-a-byte-order-mark");
    // Integral reals read as ints (as ProjectStore reads them); INT_MIN is outside ±32767, not a crash.
    const std::optional<AppSettings> reals = SettingsStore::Parse(
        R"({"schema":1.0,"window":{"x":10.0,"y":-20,"width":800.0,"height":600,"maximized":false}})");
    const std::optional<AppSettings> extreme =
        SettingsStore::Parse(R"({"schema":1,"window":{"x":-2147483648,"y":0,"width":800,"height":600}})");
    const std::optional<AppSettings> fraction =
        SettingsStore::Parse(R"({"schema":1,"window":{"x":10.5,"y":0,"width":800,"height":600}})");
    Expect(reals.has_value() && reals->window == WindowPlacementSetting{true, 10, -20, 800, 600, false} &&
               extreme.has_value() && !extreme->window.valid && fraction.has_value() && !fraction->window.valid &&
               !SettingsStore::Parse(R"({"schema":1.5})").has_value(),
           "settings.integral-reals-and-ranges");
    const std::optional<AppSettings> odd = SettingsStore::Parse(
        R"({"schema":3,"theme":"neon","language":"not a tag!","layersPanelWidth":5,"sampleRing":"yes",)"
        R"("jpegQuality":7,"window":{"x":0,"y":0,"width":10,"height":10}})");
    Expect(odd.has_value() && odd->theme == ThemePreference::System && odd->language.empty() &&
               odd->layersPanelWidth == AppSettings::kLayersPanelMin && odd->sampleRing && odd->jpegQuality == 1.0 &&
               !odd->window.valid,
           "settings.parse-unusable-values-fall-back");
    Expect(!SettingsStore::Parse("").has_value() && !SettingsStore::Parse("[1]").has_value() &&
               !SettingsStore::Parse("{}").has_value() && !SettingsStore::Parse("{\"schema\":0}").has_value() &&
               !SettingsStore::Parse("{\"schema\":\"1\"}").has_value() &&
               !SettingsStore::Parse("{\"schema\":1").has_value() && !SettingsStore::Parse("null").has_value(),
           "settings.parse-rejects");

    AppSettings changed;
    changed.theme = ThemePreference::Light;
    changed.language = L"de";
    changed.layersPanelWidth = 321;
    changed.sampleRing = false;
    changed.jpegQuality = 0.5;
    changed.window = WindowPlacementSetting{true, -5, 7, 1024, 768, false};
    const std::string text = SettingsStore::Serialize(changed);
    const std::optional<AppSettings> back = SettingsStore::Parse(text);
    Expect(back.has_value() && back.value() == changed && text.find("\"schema\": 1") != std::string::npos &&
               text.back() == '\n',
           "settings.serialize-round-trip");
    const std::string merged = SettingsStore::Serialize(changed, R"({"schema":2,"future":{"x":1},"theme":"dark"})");
    Expect(merged.find("\"future\"") != std::string::npos && merged.find("\"schema\": 2") != std::string::npos &&
               merged.find("\"theme\": \"light\"") != std::string::npos && merged.find("\"dark\"") == std::string::npos,
           "settings.serialize-keeps-unknown-and-newer-schema");
    Expect(SettingsStore::Serialize(AppSettings{}, text).find("\"window\"") == std::string::npos,
           "settings.serialize-drops-an-unplaced-window");
    Expect(SettingsStore::Parse(SettingsStore::Serialize(changed, "{ not json")) == changed,
           "settings.serialize-ignores-invalid-existing");

    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::path(ScratchRoot()) / (L"settings-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root, error);
    // Each check below gets its own subdirectory instead of sharing one `path` through the whole sequence (RF-1116:
    // on another branch, IoTests failed four settings.* checks together, once, then passed on a rerun; sharing one
    // path meant a single transient file-system hiccup, such as a real-time scanner briefly holding a just-written
    // file on a machine building several worktrees at once, left the wrong starting state for every later check in
    // the chain instead of failing just the one check it hit).
    const auto scratchPath = [&root](const wchar_t* name) { return (root / name / L"settings.json").wstring(); };

    const std::wstring missingPath = scratchPath(L"cold-missing");
    const SettingsLoadResult missing = SettingsStore::LoadCold(missingPath);
    Expect(missing.status == SettingsLoadStatus::Missing && missing.templateWritten &&
               missing.settings == AppSettings{} && SettingsStore::Parse(FileText(missingPath)) == AppSettings{},
           "settings.cold-missing-writes-template");
    const std::wstring bomPath = scratchPath(L"cold-bom");
    const bool bomWritten = SettingsStore::WriteFileAtomic(bomPath, bom);
    const SettingsLoadResult bomCold = SettingsStore::LoadCold(bomPath);
    Expect(bomWritten && bomCold.status == SettingsLoadStatus::Loaded && bomCold.backupPath.empty() &&
               bomCold.settings.theme == ThemePreference::Light && FileText(bomPath) == bom,
           "settings.cold-byte-order-mark-is-loaded");
    const std::wstring invalidPath = scratchPath(L"cold-invalid");
    const bool brokenWritten = SettingsStore::WriteFileAtomic(invalidPath, "{ nope");
    const SettingsLoadResult invalid = SettingsStore::LoadCold(invalidPath);
    Expect(brokenWritten && invalid.status == SettingsLoadStatus::Invalid && !invalid.backupPath.empty() &&
               FileText(invalid.backupPath) == "{ nope" && invalid.templateWritten &&
               invalid.settings == AppSettings{} && SettingsStore::Parse(FileText(invalidPath)) == AppSettings{},
           "settings.cold-invalid-backs-up-and-resets");
    const std::wstring liveInvalidPath = scratchPath(L"live-invalid");
    const bool brokenAgain = SettingsStore::WriteFileAtomic(liveInvalidPath, "{ nope again");
    Expect(brokenAgain && !SettingsStore::LoadLive(liveInvalidPath).has_value() &&
               FileText(liveInvalidPath) == "{ nope again",
           "settings.live-invalid-keeps-the-file");
    const std::wstring liveValidPath = scratchPath(L"live-valid");
    Expect(SettingsStore::WriteFileAtomic(liveValidPath, SettingsStore::Serialize(changed)) &&
               SettingsStore::LoadLive(liveValidPath) == changed &&
               !SettingsStore::LoadLive((root / L"live-valid" / L"none.json").wstring()).has_value(),
           "settings.live-valid-and-missing");
    std::wstring backup;
    const std::wstring saveBackupPath = scratchPath(L"save-backup");
    Expect(SettingsStore::WriteFileAtomic(saveBackupPath, "broken") &&
               SettingsStore::Save(saveBackupPath, changed, &backup) && !backup.empty() &&
               FileText(backup) == "broken" && SettingsStore::LoadLive(saveBackupPath) == changed,
           "settings.save-over-invalid-backs-up");
    const std::wstring saveUnknownPath = scratchPath(L"save-unknown");
    Expect(SettingsStore::WriteFileAtomic(saveUnknownPath, R"({"schema":1,"custom":true})") &&
               SettingsStore::Save(saveUnknownPath, changed) &&
               FileText(saveUnknownPath).find("\"custom\": true") != std::string::npos &&
               SettingsStore::LoadLive(saveUnknownPath) == changed,
           "settings.save-keeps-unknown-fields");
    bool temporaryLeft = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, error))
    {
        temporaryLeft = temporaryLeft || entry.path().wstring().find(L".tmp-") != std::wstring::npos;
    }
    Expect(!temporaryLeft, "settings.save-leaves-no-temporary");
    const std::wstring nested = (root / L"nested" / L"deeper" / L"dir" / L"settings.json").wstring();
    Expect(SettingsStore::Save(nested, changed) && SettingsStore::LoadLive(nested) == changed,
           "settings.save-creates-the-directory");
    // A directory where LoadCold expects a file, made explicitly rather than reused from the previous check's
    // `deeper`, so this check no longer depends on that one having run first.
    const std::filesystem::path unreadablePath = root / L"unreadable" / L"settings.json";
    std::filesystem::create_directories(unreadablePath, error);
    const SettingsLoadResult unreadable = SettingsStore::LoadCold(unreadablePath.wstring());
    Expect(unreadable.status == SettingsLoadStatus::Unreadable && unreadable.settings == AppSettings{},
           "settings.cold-unreadable-keeps-defaults");
    std::filesystem::remove_all(root, error);
}

int gCrtReportFailures = 0; // incremented only in Debug / ASan Debug; see CountCrtReportFailures below.

#if defined(_DEBUG)
// Counts _CRT_ERROR / _CRT_ASSERT reports instead of trusting the CRT's own default handling to abort: a debug-heap
// corruption report does not always abort by itself, so a run could otherwise print the report and still exit 0
// (RF-702, Specs/Testing/Testing_Validation.md). _CRTDBG_MODE_FILE / _CRTDBG_FILE_STDERR (installed alongside this
// hook, in wmain) still write the report text to stderr; this hook only has to observe it.
int __cdecl CountCrtReportFailures(int reportType, char* /*message*/, int* /*returnValue*/) noexcept
{
    if (reportType == _CRT_ERROR || reportType == _CRT_ASSERT)
    {
        ++gCrtReportFailures;
    }
    return 0;
}
#endif

int wmain()
{
#if defined(_DEBUG)
    // A Debug assertion or error report goes to stderr, not a dialog no harness can click, and the hook above
    // counts it so this run fails even when the CRT's own default handling would just continue.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &CountCrtReportFailures);
#endif
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        std::printf("FAIL com.init\n");
        return 1;
    }
    TestGuidParse();
    TestNameTables();
    TestPngRoundTrip();
    TestPngAlphaRoundTrip();
    TestJpegExport();
    TestJpegQualityGolden();
    TestHeifMissing();
    TestImportOrientationAndColor();
    TestImportAllOrientations();
    TestExplodedGolden();
    TestTransformArrayShape();
    TestShapeLiveness();
    TestExplodedReparseAndHostileZip();
    TestPathTraversal();
    TestManifestTooLarge();
    TestVersionGatesAndRejects();
    TestImportOntoDocument();
    TestSaveRejectsInvalidDocument();
    TestProjectSaveDurability();
    gFailures += RunPathClassificationWorkTests();
    gFailures += RunImageCodecWorkTests();
    TestSettingsStore();
    if (gFailures != 0 || gCrtReportFailures != 0)
    {
        std::printf("%d test(s) failed (%d CRT report(s)).\n", gFailures, gCrtReportFailures);
        return 1;
    }
    std::printf("All I/O tests passed.\n");
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return 0;
}

#include "Document.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <objbase.h>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
    ++gFailures;
}

[[nodiscard]] std::wstring FixtureRoot()
{
    return std::filesystem::path(__FILE__).parent_path() / L"Fixtures" / L"v7-exploded";
}

[[nodiscard]] std::wstring ScratchRoot()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / L"RedPrism.IoTests";
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

[[nodiscard]] bool RastersEqual(const Raster& left, const Raster& right) noexcept
{
    if (!left.HasStorage() || !right.HasStorage() || left.Width() != right.Width() || left.Height() != right.Height() ||
        left.Format() != right.Format() || left.ByteSize() != right.ByteSize())
    {
        return false;
    }
    return std::memcmp(left.Data(), right.Data(), left.ByteSize()) == 0;
}

void TestGuidParse()
{
    const Guid id = Guid::Generate();
    const std::optional<Guid> parsed = Guid::Parse(id.ToUtf8());
    Expect(parsed.has_value() && parsed.value() == id, "guid.round-trip");
    Expect(!Guid::Parse("not-a-guid").has_value(), "guid.invalid");
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
}

void TestJpegExport()
{
    const Raster blue = SolidBgra(4, 4, 255, 0, 0, 255);
    std::vector<std::uint8_t> jpeg;
    Expect(EncodeJpeg(blue, JpegExportOptions{0.4, 72}, jpeg) == ImageCodecStatus::Ok && !jpeg.empty(), "jpeg.encode");
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
    (void)EncodePngFile(SolidBgra(2, 2, 255, 0, 0, 255), 72,
                        (images + L"00000000-0000-4000-8000-000000000004.png").c_str());
    return dest;
}

void TestExplodedGolden()
{
    const std::wstring fixture = MaterializeV7Fixture();
    const ProjectLoadResult loaded = LoadProject(fixture.c_str());
    Expect(loaded.status == ProjectIoStatus::Ok, "v7.load");
    Expect(loaded.formatVersion == 7, "v7.version");
    Expect(loaded.document.width == 2 && loaded.document.height == 2, "v7.size");
    Expect(loaded.document.layers.size() == 4, "v7.layers");
    bool sawGroup = false;
    bool sawAdjustment = false;
    bool sawImage = false;
    for (const ImageLayer& layer : loaded.document.layers)
    {
        sawGroup = sawGroup || layer.isGroup;
        sawAdjustment = sawAdjustment || layer.adjustment.has_value();
        sawImage = sawImage || layer.asset.has_value();
    }
    Expect(sawGroup && sawAdjustment && sawImage, "v7.features");

    const std::wstring exploded = ScratchRoot() + L"\\roundtrip-exploded";
    const std::wstring zipPath = ScratchRoot() + L"\\roundtrip.prism";
    Expect(SaveProject(exploded.c_str(), loaded.document, loaded.activeLayerID, false) == ProjectIoStatus::Ok,
           "v7.save-exploded");
    const ProjectLoadResult explodedAgain = LoadProject(exploded.c_str());
    Expect(explodedAgain.status == ProjectIoStatus::Ok && explodedAgain.document.layers.size() == 4,
           "v7.load-exploded");
    Expect(SaveProject(zipPath.c_str(), loaded.document, loaded.activeLayerID, true) == ProjectIoStatus::Ok,
           "v7.save-zip");
    const ProjectLoadResult zipAgain = LoadProject(zipPath.c_str());
    Expect(zipAgain.status == ProjectIoStatus::Ok && zipAgain.document.layers.size() == 4, "v7.load-zip");
}

void TestPathTraversal()
{
    const std::wstring evil = ScratchRoot() + L"\\traverse";
    std::error_code error;
    std::filesystem::remove_all(evil, error);
    std::filesystem::create_directories(std::filesystem::path(evil) / L"images", error);
    const std::string manifest = R"({
  "activeLayerID": "00000000-0000-4000-8000-000000000002",
  "colorSpace": "sRGB",
  "documentID": "00000000-0000-4000-8000-000000000001",
  "format": "com.redprism.project",
  "height": 1,
  "layers": [
    {
      "id": "00000000-0000-4000-8000-000000000002",
      "imageFile": "../secret.png",
      "isVisible": true,
      "name": "Evil",
      "transform": {
        "flipX": false,
        "flipY": false,
        "origin": {"x": 0, "y": 0},
        "rotation": 0,
        "sampling": "High quality",
        "size": {"height": 1, "width": 1}
      }
    }
  ],
  "resolution": 72,
  "version": 7,
  "width": 1
})";
    const std::filesystem::path manifestPath = std::filesystem::path(evil) / L"manifest.json";
    FILE* file = nullptr;
    if (_wfopen_s(&file, manifestPath.c_str(), L"wb") == 0 && file)
    {
        (void)fwrite(manifest.data(), 1, manifest.size(), file);
        (void)fclose(file);
    }
    const ProjectLoadResult loaded = LoadProject(evil.c_str());
    Expect(loaded.status == ProjectIoStatus::Invalid, "path.traversal");
}

void TestManifestTooLarge()
{
    const std::wstring root = ScratchRoot() + L"\\huge";
    std::error_code error;
    std::filesystem::create_directories(root, error);
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
} // namespace

int wmain()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        std::printf("FAIL com.init\n");
        return 1;
    }
    TestGuidParse();
    TestPngRoundTrip();
    TestJpegExport();
    TestHeifMissing();
    TestExplodedGolden();
    TestPathTraversal();
    TestManifestTooLarge();
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All I/O tests passed.\n");
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return 0;
}

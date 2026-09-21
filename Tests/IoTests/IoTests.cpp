#include "Document.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "SettingsStore.h"

#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    Expect(id.ToUtf8().size() == 36 && id.ToUtf8() == std::string(id.ToUtf8()), "guid.uppercase-36");
}

void TestNameTables()
{
    for (int index = 0; index < 13; ++index)
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
}

void TestJpegExport()
{
    const Raster blue = SolidBgra(4, 4, 255, 0, 0, 128);
    std::vector<std::uint8_t> jpeg;
    Expect(EncodeJpeg(blue, JpegExportOptions{0.4, 72}, jpeg) == ImageCodecStatus::Ok && !jpeg.empty(), "jpeg.encode");
    std::vector<std::uint8_t> better;
    Expect(EncodeJpeg(blue, JpegExportOptions{1.0, 72}, better) == ImageCodecStatus::Ok && better.size() >= jpeg.size(),
           "jpeg.quality-changes-bytes");
    const std::wstring path = ScratchRoot() + L"\\export.jpg";
    Expect(EncodeJpegFile(blue, JpegExportOptions{0.9, 300}, path.c_str()) == ImageCodecStatus::Ok, "jpeg.file");
    DecodedImage decoded{};
    Expect(ImportImageFile(path.c_str(), kMaxLayerPixels, decoded) == ImageCodecStatus::Ok &&
               decoded.image.Width() == 4 && decoded.image.Height() == 4 && decoded.name == L"export",
           "jpeg.import");
    Expect(decoded.image.HasStorage() && decoded.image.Data()[3] == 255, "jpeg.alpha-stripped");
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
    const ProjectLoadResult zipAgain = LoadProject(zipPath.c_str());
    Expect(zipAgain.status == ProjectIoStatus::Ok, "v7.load-zip");
    if (zipAgain.status == ProjectIoStatus::Ok)
    {
        ExpectFixtureSemantics(zipAgain.document, "zip round trip");
    }
    // Overwriting a saved package drops assets that are no longer referenced.
    CanvasDocument trimmed = loaded.document;
    trimmed.layers.erase(trimmed.layers.begin());
    trimmed.layers.back().maskSourceID.reset();
    Expect(SaveProject(exploded.c_str(), trimmed, std::nullopt, false) == ProjectIoStatus::Ok, "v7.overwrite");
    Expect(!std::filesystem::exists(std::filesystem::path(exploded) / L"images" /
                                    L"00000000-0000-4000-8000-000000000002.png"),
           "v7.overwrite-drops-removed-assets");
}

void TestPathTraversal()
{
    const std::string evil = MinimalManifest(7, "\"imageFile\": \"../secret.png\"");
    Expect(LoadManifestText(L"traverse", evil) == ProjectIoStatus::Invalid, "path.traversal");
    const std::string wrongName = MinimalManifest(7, "\"imageFile\": \"00000000-0000-4000-8000-000000000009.png\"");
    Expect(LoadManifestText(L"wrong-name", wrongName) == ProjectIoStatus::Invalid, "path.image-name-mismatch");
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
    Expect(LoadManifestText(L"format", MinimalManifest(7, {}, "\"format\": \"other\"")) == ProjectIoStatus::Invalid ||
               LoadManifestText(L"format2", "{\"format\":\"x\",\"version\":7}") == ProjectIoStatus::Invalid,
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
    Expect(LoadManifestText(L"v4-clip",
                            MinimalManifest(4, "\"maskSourceID\": \"00000000-0000-4000-8000-000000000002\"")) ==
               ProjectIoStatus::Invalid,
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
               full->window == WindowPlacementSetting{true, 10, 20, 800, 600, true},
           "settings.parse-fields");
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
    const std::wstring path = (root / L"settings.json").wstring();
    const SettingsLoadResult missing = SettingsStore::LoadCold(path);
    Expect(missing.status == SettingsLoadStatus::Missing && missing.templateWritten &&
               missing.settings == AppSettings{} && SettingsStore::Parse(FileText(path)) == AppSettings{},
           "settings.cold-missing-writes-template");
    const bool brokenWritten = SettingsStore::WriteFileAtomic(path, "{ nope");
    const SettingsLoadResult invalid = SettingsStore::LoadCold(path);
    Expect(brokenWritten && invalid.status == SettingsLoadStatus::Invalid && !invalid.backupPath.empty() &&
               FileText(invalid.backupPath) == "{ nope" && invalid.templateWritten &&
               invalid.settings == AppSettings{} && SettingsStore::Parse(FileText(path)) == AppSettings{},
           "settings.cold-invalid-backs-up-and-resets");
    const bool brokenAgain = SettingsStore::WriteFileAtomic(path, "{ nope again");
    Expect(brokenAgain && !SettingsStore::LoadLive(path).has_value() && FileText(path) == "{ nope again",
           "settings.live-invalid-keeps-the-file");
    Expect(SettingsStore::WriteFileAtomic(path, SettingsStore::Serialize(changed)) &&
               SettingsStore::LoadLive(path) == changed &&
               !SettingsStore::LoadLive((root / L"none.json").wstring()).has_value(),
           "settings.live-valid-and-missing");
    std::wstring backup;
    Expect(SettingsStore::WriteFileAtomic(path, "broken") && SettingsStore::Save(path, changed, &backup) &&
               !backup.empty() && FileText(backup) == "broken" && SettingsStore::LoadLive(path) == changed,
           "settings.save-over-invalid-backs-up");
    Expect(SettingsStore::WriteFileAtomic(path, R"({"schema":1,"custom":true})") &&
               SettingsStore::Save(path, changed) && FileText(path).find("\"custom\": true") != std::string::npos &&
               SettingsStore::LoadLive(path) == changed,
           "settings.save-keeps-unknown-fields");
    bool temporaryLeft = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error))
    {
        temporaryLeft = temporaryLeft || entry.path().wstring().find(L".tmp-") != std::wstring::npos;
    }
    Expect(!temporaryLeft, "settings.save-leaves-no-temporary");
    const std::wstring nested = (root / L"deeper" / L"dir" / L"settings.json").wstring();
    Expect(SettingsStore::Save(nested, changed) && SettingsStore::LoadLive(nested) == changed,
           "settings.save-creates-the-directory");
    const SettingsLoadResult unreadable = SettingsStore::LoadCold((root / L"deeper").wstring());
    Expect(unreadable.status == SettingsLoadStatus::Unreadable && unreadable.settings == AppSettings{},
           "settings.cold-unreadable-keeps-defaults");
    std::filesystem::remove_all(root, error);
}

int wmain()
{
#if defined(_DEBUG)
    // A Debug assertion reports on stderr and aborts instead of waiting on a dialog no harness can click.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
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
    TestJpegExport();
    TestHeifMissing();
    TestExplodedGolden();
    TestPathTraversal();
    TestManifestTooLarge();
    TestVersionGatesAndRejects();
    TestImportOntoDocument();
    TestSaveRejectsInvalidDocument();
    TestSettingsStore();
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

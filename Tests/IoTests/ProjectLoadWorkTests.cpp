#include "ProjectLoadWorkTests.h"

#include "PixelBuffer.h"
#include "ProjectLoadWork.h"
#include "ProjectStore.h"
#include "ZipOwner.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>
#include <zip.h>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

class ScratchProject final
{
  public:
    ScratchProject()
    {
        const auto base = std::filesystem::temp_directory_path() / L"RedPrism.IoTests" /
                          std::to_wstring(GetCurrentProcessId()) / L"ProjectLoadWorkTests";
        std::error_code error;
        std::filesystem::create_directories(base, error);
        if (error)
            return;
        _parent = std::filesystem::weakly_canonical(base, error);
        if (error || !_parent.is_absolute())
            return;
        _leaf = Guid::Generate().ToString();
        _root = _parent / _leaf;
        if (!std::filesystem::create_directory(_root, error) || error)
            _root.clear();
    }

    ScratchProject(const ScratchProject&) = delete;
    ScratchProject& operator=(const ScratchProject&) = delete;

    ~ScratchProject()
    {
        if (!_root.empty())
        {
            std::error_code error;
            const auto resolved = std::filesystem::weakly_canonical(_root, error);
            const bool owned = !error && resolved.is_absolute() && resolved == _root &&
                               resolved.parent_path() == _parent && resolved.filename().wstring() == _leaf;
            if (owned)
                std::filesystem::remove_all(resolved, error);
            const bool remains = std::filesystem::exists(_root, error);
            Expect(owned && !error && !remains, "project-load-work.guid-scratch-cleanup");
        }
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return !_root.empty();
    }
    [[nodiscard]] std::filesystem::path Root() const
    {
        return _root;
    }
    [[nodiscard]] std::wstring RootString() const
    {
        return _root.wstring();
    }
    [[nodiscard]] std::filesystem::path Image(const std::string& name) const
    {
        return _root / L"images" / std::filesystem::path(name);
    }
    [[nodiscard]] std::filesystem::path Manifest() const
    {
        return _root / L"manifest.json";
    }

    bool CreateFolder()
    {
        std::error_code error;
        std::filesystem::create_directories(_root / L"images", error);
        return !error && std::filesystem::is_directory(_root / L"images", error) && !error;
    }

  private:
    std::filesystem::path _root;
    std::filesystem::path _parent;
    std::wstring _leaf;
};

[[nodiscard]] std::string MakeLayer(const char* id, const char* name, int width, int height, const std::string& image,
                                    const std::string& mask)
{
    return std::string("{\"id\":\"") + id + "\",\"name\":\"" + name + "\",\"isVisible\":true," + "\"imageFile\":\"" +
           image + "\",\"maskFile\":\"" + mask + "\"," +
           "\"transform\":{\"flipX\":false,\"flipY\":false,\"origin\":{\"x\":0,\"y\":0}," +
           "\"rotation\":0,\"sampling\":\"Nearest\",\"size\":{\"height\":" + std::to_string(height) +
           ",\"width\":" + std::to_string(width) + "}}}";
}

[[nodiscard]] std::string MakeManifest(const std::vector<std::string>& layers, int version = 7)
{
    std::string text = "{";
    if (!layers.empty())
        text += "\"activeLayerID\":\"00000000-0000-4000-8000-000000000002\",";
    text += "\"colorSpace\":\"sRGB\",\"documentID\":\"00000000-0000-4000-8000-000000000001\","
            "\"format\":\"com.redprism.project\",\"height\":2,\"layers\":[";
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (i != 0)
            text += ',';
        text += layers[i];
    }
    text += "],\"version\":" + std::to_string(version) + ",\"width\":2}";
    return text;
}

[[nodiscard]] std::string FixtureManifest(int version = 7)
{
    return MakeManifest(
        {MakeLayer("00000000-0000-4000-8000-000000000002", "First", 2, 1, "00000000-0000-4000-8000-000000000002.png",
                   "00000000-0000-4000-8000-000000000002.mask.png"),
         MakeLayer("00000000-0000-4000-8000-000000000003", "Second", 1, 2, "00000000-0000-4000-8000-000000000003.png",
                   "00000000-0000-4000-8000-000000000003.mask.png")},
        version);
}

[[nodiscard]] bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool WritePng(const std::filesystem::path& path, int width, int height,
                            const std::vector<std::uint8_t>& bytes, bool gray)
{
    // Fixtures use tiny known pixels; encoding is only fixture setup. Probe assertions below hard-code the
    // independent expected dimensions and aggregate counts.
    const Raster raster =
        Raster::Make(width, height, gray ? RasterFormat::Gray8 : RasterFormat::Bgra8888, bytes.data(), bytes.size());
    return raster.HasStorage() && EncodePngFile(raster, 72, path.c_str()) == ImageCodecStatus::Ok;
}

[[nodiscard]] bool WriteFolderFixture(ScratchProject& project, int version = 7)
{
    if (!project.IsValid() || !project.CreateFolder() || !WriteText(project.Manifest(), FixtureManifest(version)))
        return false;
    return WritePng(project.Image("00000000-0000-4000-8000-000000000002.png"), 2, 1, {10, 20, 30, 255, 40, 50, 60, 255},
                    false) &&
           WritePng(project.Image("00000000-0000-4000-8000-000000000002.mask.png"), 2, 1, {0, 255}, true) &&
           WritePng(project.Image("00000000-0000-4000-8000-000000000003.png"), 1, 2,
                    {70, 80, 90, 255, 100, 110, 120, 255}, false) &&
           WritePng(project.Image("00000000-0000-4000-8000-000000000003.mask.png"), 1, 2, {64, 192}, true);
}

[[nodiscard]] bool WriteZipFixture(const ScratchProject& project, const std::filesystem::path& destination)
{
    struct Entry final
    {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Entry> entries;
    const std::string manifest = FixtureManifest();
    entries.push_back({"manifest.json", {manifest.begin(), manifest.end()}});
    for (const char* name :
         {"00000000-0000-4000-8000-000000000002.png", "00000000-0000-4000-8000-000000000002.mask.png",
          "00000000-0000-4000-8000-000000000003.png", "00000000-0000-4000-8000-000000000003.mask.png"})
        entries.push_back({std::string("images/") + name, ReadBytes(project.Image(name))});

    zip_error_t error{};
    zip_error_init(&error);
    const auto cleanupError = wil::scope_exit([&]() noexcept { zip_error_fini(&error); });
    using unique_zip_source = wil::unique_any<zip_source_t*, decltype(&zip_source_free), zip_source_free>;
    unique_zip_source source{zip_source_win32w_create(destination.c_str(), 0, -1, &error)};
    unique_zip archive{source ? zip_open_from_source(source.get(), ZIP_CREATE | ZIP_EXCL, &error) : nullptr};
    if (!archive)
    {
        return false;
    }
    (void)source.release(); // archive owns its successfully opened source
    for (Entry& entry : entries)
    {
        unique_zip_source buffer{zip_source_buffer(archive.get(), entry.bytes.data(), entry.bytes.size(), 0)};
        if (!buffer || zip_file_add(archive.get(), entry.name.c_str(), buffer.get(), ZIP_FL_ENC_UTF_8) < 0)
        {
            return false;
        }
        (void)buffer.release(); // archive owns accepted buffers until commit/discard, while entries retain bytes
    }
    return ZipCommit(archive);
}

struct ProbeEvents final
{
    unsigned int manifestReady = 0;
    unsigned int inspected = 0;
    unsigned int lastInspectedValue = 0;
    unsigned int decoded = 0;
    ProjectIoStage cancelAt = ProjectIoStage::WriteFailed;
    CancelSource* cancel = nullptr;
};

[[nodiscard]] ProjectIoObserver Observer(ProbeEvents& events)
{
    return {[](void* context, ProjectIoStage stage, unsigned int value) noexcept
            {
                auto& observed = *static_cast<ProbeEvents*>(context);
                if (stage == ProjectIoStage::ManifestReady)
                    ++observed.manifestReady;
                if (stage == ProjectIoStage::AssetInspected)
                {
                    ++observed.inspected;
                    observed.lastInspectedValue = value;
                }
                if (stage == ProjectIoStage::AssetDecoded)
                    ++observed.decoded;
                if (stage == observed.cancelAt && observed.cancel != nullptr)
                    (void)observed.cancel->Cancel();
            },
            &events};
}

[[nodiscard]] bool HasPixelBytes(const Raster& raster, const std::vector<std::uint8_t>& expected, int width, int height,
                                 RasterFormat format)
{
    return raster.HasStorage() && raster.Width() == width && raster.Height() == height && raster.Format() == format &&
           raster.ByteSize() == expected.size() && std::equal(expected.begin(), expected.end(), raster.Data());
}

void ExpectEmptyResult(const ProjectLoadResult& result, ProjectIoStatus status, const char* stem)
{
    const bool empty = result.status == status && result.document.layers.empty() && !result.activeLayerID.has_value();
    Expect(empty, stem);
}

void TestFolderAndZipProbe()
{
    ScratchProject folder;
    Expect(WriteFolderFixture(folder), "project-load-probe.folder-fixture");
    if (!folder.IsValid())
        return;

    ProbeEvents folderEvents{};
    const ProjectLoadProbeResult probed = ProbeProjectLoad(folder.RootString().c_str(), {}, Observer(folderEvents));
    Expect(probed.status == ProjectIoStatus::Ok && probed.formatVersion == 7, "project-load-probe.folder-version");
    Expect(probed.limits.imagePixels == 4 && probed.limits.maskPixels == 4 && probed.limits.layerCount == 2,
           "project-load-probe.folder-independent-counts");
    const auto manifestSize = std::filesystem::file_size(folder.Manifest());
    const auto largestEncoded =
        (std::max)({std::filesystem::file_size(folder.Image("00000000-0000-4000-8000-000000000002.png")),
                    std::filesystem::file_size(folder.Image("00000000-0000-4000-8000-000000000002.mask.png")),
                    std::filesystem::file_size(folder.Image("00000000-0000-4000-8000-000000000003.png")),
                    std::filesystem::file_size(folder.Image("00000000-0000-4000-8000-000000000003.mask.png"))});
    Expect(probed.limits.manifestBytes == manifestSize && probed.limits.encodedAssetBytes == largestEncoded,
           "project-load-probe.folder-byte-limits");
    Expect(folderEvents.manifestReady == 1 && folderEvents.inspected == 4 && folderEvents.lastInspectedValue == 4 &&
               folderEvents.decoded == 0,
           "project-load-probe.folder-stage-count-without-decoding");

    const ProjectLoadResult loaded = LoadProject(folder.RootString().c_str(), {}, {}, probed.limits);
    Expect(loaded.status == ProjectIoStatus::Ok && loaded.formatVersion == 7 && loaded.document.layers.size() == 2,
           "project-load-probe.folder-exact-limits-load");
    if (loaded.status == ProjectIoStatus::Ok && loaded.document.layers.size() == 2)
    {
        const ImageLayer& first = loaded.document.layers[0];
        const ImageLayer& second = loaded.document.layers[1];
        Expect(first.name == L"First" && second.name == L"Second" && first.asset.has_value() &&
                   second.asset.has_value() && first.mask.has_value() && second.mask.has_value(),
               "project-load-probe.folder-layer-order-and-assets");
        if (first.asset.has_value() && second.asset.has_value() && first.mask.has_value() && second.mask.has_value())
        {
            Expect(HasPixelBytes(first.asset.value().image, {10, 20, 30, 255, 40, 50, 60, 255}, 2, 1,
                                 RasterFormat::Bgra8888) &&
                       HasPixelBytes(second.asset.value().image, {70, 80, 90, 255, 100, 110, 120, 255}, 1, 2,
                                     RasterFormat::Bgra8888),
                   "project-load-probe.folder-literal-image-pixels");
            Expect(HasPixelBytes(first.mask.value().asset.image, {0, 255}, 2, 1, RasterFormat::Gray8) &&
                       HasPixelBytes(second.mask.value().asset.image, {64, 192}, 1, 2, RasterFormat::Gray8),
                   "project-load-probe.folder-literal-mask-pixels");
        }
    }

    const std::filesystem::path zipPath = folder.Root() / L"fixture.prism";
    Expect(WriteZipFixture(folder, zipPath), "project-load-probe.zip-fixture");
    if (std::filesystem::exists(zipPath))
    {
        ProbeEvents zipEvents{};
        const ProjectLoadProbeResult zipProbe = ProbeProjectLoad(zipPath.c_str(), {}, Observer(zipEvents));
        Expect(zipProbe.status == ProjectIoStatus::Ok && zipProbe.formatVersion == 7 &&
                   zipProbe.limits.imagePixels == 4 && zipProbe.limits.maskPixels == 4 &&
                   zipProbe.limits.layerCount == 2 && zipEvents.inspected == 4 && zipEvents.decoded == 0,
               "project-load-probe.zip-independent-counts-and-version");
        Expect(zipProbe.limits.manifestBytes == manifestSize && zipProbe.limits.encodedAssetBytes == largestEncoded,
               "project-load-probe.zip-byte-limits");
        const ProjectLoadResult zipLoaded = LoadProject(zipPath.c_str(), {}, {}, zipProbe.limits);
        Expect(zipLoaded.status == ProjectIoStatus::Ok && zipLoaded.document.layers.size() == 2,
               "project-load-probe.zip-exact-limits-load");
        if (zipLoaded.status == ProjectIoStatus::Ok && zipLoaded.document.layers.size() == 2 &&
            zipLoaded.document.layers[0].asset.has_value() && zipLoaded.document.layers[1].asset.has_value() &&
            zipLoaded.document.layers[0].mask.has_value() && zipLoaded.document.layers[1].mask.has_value())
        {
            Expect(HasPixelBytes(zipLoaded.document.layers[0].asset.value().image, {10, 20, 30, 255, 40, 50, 60, 255},
                                 2, 1, RasterFormat::Bgra8888) &&
                       HasPixelBytes(zipLoaded.document.layers[1].asset.value().image,
                                     {70, 80, 90, 255, 100, 110, 120, 255}, 1, 2, RasterFormat::Bgra8888),
                   "project-load-probe.zip-literal-pixels");
            Expect(HasPixelBytes(zipLoaded.document.layers[0].mask.value().asset.image, {0, 255}, 2, 1,
                                 RasterFormat::Gray8) &&
                       HasPixelBytes(zipLoaded.document.layers[1].mask.value().asset.image, {64, 192}, 1, 2,
                                     RasterFormat::Gray8),
                   "project-load-probe.zip-literal-mask-pixels");
        }
    }
}

void TestProbeFailuresAndCancellation()
{
    ScratchProject empty;
    Expect(empty.IsValid() && empty.CreateFolder() && WriteText(empty.Manifest(), MakeManifest({})),
           "project-load-probe.empty-fixture");
    if (empty.IsValid())
    {
        const ProjectLoadProbeResult probe = ProbeProjectLoad(empty.RootString().c_str());
        const ProjectLoadResult load = LoadProject(empty.RootString().c_str(), {}, {}, probe.limits);
        Expect(probe.status == ProjectIoStatus::Ok && probe.formatVersion == 7 && probe.limits.layerCount == 0 &&
                   probe.limits.imagePixels == 0 && probe.limits.maskPixels == 0 &&
                   load.status == ProjectIoStatus::Ok && load.formatVersion == 7 && load.document.layers.empty(),
               "project-load-probe.valid-empty-document");
    }

    ScratchProject unsupported;
    Expect(WriteFolderFixture(unsupported, 99), "project-load-probe.unsupported-fixture");
    const ProjectLoadProbeResult unsupportedResult = ProbeProjectLoad(unsupported.RootString().c_str());
    Expect(unsupportedResult.status == ProjectIoStatus::UnsupportedVersion && unsupportedResult.formatVersion == 99 &&
               unsupportedResult.limits.layerCount == 0 && unsupportedResult.limits.manifestBytes == 0,
           "project-load-probe.unsupported-version-clears-limits");

    ScratchProject malformed;
    Expect(malformed.IsValid() && malformed.CreateFolder() && WriteText(malformed.Manifest(), FixtureManifest()),
           "project-load-probe.malformed-fixture");
    if (malformed.IsValid())
    {
        const auto image = malformed.Image("00000000-0000-4000-8000-000000000002.png");
        Expect(WriteBytes(image, {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13}),
               "project-load-probe.truncated-png-written");
        const ProjectLoadProbeResult truncated = ProbeProjectLoad(malformed.RootString().c_str());
        Expect(truncated.status == ProjectIoStatus::MissingImage && truncated.limits.layerCount == 0 &&
                   truncated.limits.imagePixels == 0,
               "project-load-probe.truncated-png-rejected");

        std::vector<std::uint8_t> badHeader(33, 0);
        badHeader[0] = 137;
        badHeader[1] = 80;
        badHeader[2] = 78;
        badHeader[3] = 71;
        badHeader[4] = 13;
        badHeader[5] = 10;
        badHeader[6] = 26;
        badHeader[7] = 10;
        Expect(WriteBytes(image, badHeader), "project-load-probe.invalid-png-written");
        const ProjectLoadProbeResult invalid = ProbeProjectLoad(malformed.RootString().c_str());
        Expect(invalid.status == ProjectIoStatus::MissingImage && invalid.limits.layerCount == 0,
               "project-load-probe.invalid-png-signature-or-ihdr-rejected");

        Expect(WritePng(image, 2, 1, {10, 20, 30, 255, 40, 50, 60, 255}, false),
               "project-load-probe.restore-first-image");
        const auto missing = malformed.Image("00000000-0000-4000-8000-000000000002.mask.png");
        std::error_code error;
        std::filesystem::remove(missing, error);
        const ProjectLoadProbeResult absent = ProbeProjectLoad(malformed.RootString().c_str());
        Expect(absent.status == ProjectIoStatus::MissingImage && absent.limits.imagePixels == 0 &&
                   absent.limits.maskPixels == 0,
               "project-load-probe.missing-mask-rejected");
    }

    ScratchProject cancellable;
    Expect(WriteFolderFixture(cancellable), "project-load-probe.cancel-fixture");
    for (const ProjectIoStage point : {ProjectIoStage::ManifestReady, ProjectIoStage::AssetInspected})
    {
        CancelSource source;
        ProbeEvents events{};
        events.cancelAt = point;
        events.cancel = &source;
        const ProjectLoadProbeResult cancelled =
            ProbeProjectLoad(cancellable.RootString().c_str(), source.Token(), Observer(events));
        const bool cleared = cancelled.status == ProjectIoStatus::Cancelled && cancelled.limits.imagePixels == 0 &&
                             cancelled.limits.maskPixels == 0 && cancelled.limits.layerCount == 0 &&
                             cancelled.limits.manifestBytes == 0 && cancelled.limits.encodedAssetBytes == 0;
        Expect(cleared, point == ProjectIoStage::ManifestReady ? "project-load-probe.cancel-at-manifest-clears"
                                                               : "project-load-probe.cancel-at-asset-clears");
    }
}

void TestChangedInputCeilings()
{
    ScratchProject original;
    Expect(WriteFolderFixture(original), "project-load-probe.change-fixture");
    const ProjectLoadProbeResult probed = ProbeProjectLoad(original.RootString().c_str());
    if (probed.status != ProjectIoStatus::Ok)
        return;

    // A source that grows after probing is bounded by the original manifest-byte allowance.
    const std::string largerManifest = FixtureManifest() + " ";
    Expect(WriteText(original.Manifest(), largerManifest), "project-load-probe.grown-manifest-written");
    struct DecodeEvents final
    {
        unsigned int decoded = 0;
    };
    const auto decodeObserver = [](DecodeEvents& events)
    {
        return ProjectIoObserver{[](void* context, ProjectIoStage stage, unsigned int) noexcept
                                 {
                                     if (stage == ProjectIoStage::AssetDecoded)
                                         ++static_cast<DecodeEvents*>(context)->decoded;
                                 },
                                 &events};
    };
    DecodeEvents manifestEvents{};
    ExpectEmptyResult(LoadProject(original.RootString().c_str(), {}, decodeObserver(manifestEvents), probed.limits),
                      ProjectIoStatus::TooLarge, "project-load-probe.grown-manifest-refused-without-document");
    Expect(manifestEvents.decoded == 0, "project-load-probe.grown-manifest-refused-before-decode");
    Expect(WriteText(original.Manifest(), FixtureManifest()), "project-load-probe.manifest-restored");

    // Raise only the manifest byte ceiling for this mutation; the probed layer count remains two.
    ProjectLoadLimits layerCeiling = probed.limits;
    layerCeiling.manifestBytes = FixtureManifest().size() + 2048;
    const std::string extraLayer = MakeLayer("00000000-0000-4000-8000-000000000004", "Third", 1, 1, "", "");
    Expect(WriteText(original.Manifest(), MakeManifest({MakeLayer("00000000-0000-4000-8000-000000000002", "First", 2, 1,
                                                                  "00000000-0000-4000-8000-000000000002.png",
                                                                  "00000000-0000-4000-8000-000000000002.mask.png"),
                                                        MakeLayer("00000000-0000-4000-8000-000000000003", "Second", 1,
                                                                  2, "00000000-0000-4000-8000-000000000003.png",
                                                                  "00000000-0000-4000-8000-000000000003.mask.png"),
                                                        extraLayer})),
           "project-load-probe.extra-layer-manifest-written");
    DecodeEvents layerEvents{};
    ExpectEmptyResult(LoadProject(original.RootString().c_str(), {}, decodeObserver(layerEvents), layerCeiling),
                      ProjectIoStatus::TooLarge, "project-load-probe.layer-count-refused-independently");
    Expect(layerEvents.decoded == 0, "project-load-probe.layer-count-refused-before-decode");

    Expect(WriteFolderFixture(original), "project-load-probe.encoded-fixture-restored");
    const auto grownImage = original.Image("00000000-0000-4000-8000-000000000002.png");
    auto encoded = ReadBytes(grownImage);
    encoded.resize(probed.limits.encodedAssetBytes + 1, 'x');
    Expect(WriteBytes(grownImage, encoded), "project-load-probe.grown-first-encoded-asset-written");
    DecodeEvents encodedEvents{};
    ExpectEmptyResult(LoadProject(original.RootString().c_str(), {}, decodeObserver(encodedEvents), probed.limits),
                      ProjectIoStatus::TooLarge, "project-load-probe.grown-encoded-asset-refused-without-document");
    Expect(encodedEvents.decoded == 0, "project-load-probe.grown-encoded-asset-refused-before-decode");

    const auto grownArchive = original.Root() / L"grown-encoded.prism";
    Expect(WriteZipFixture(original, grownArchive), "project-load-probe.grown-zip-fixture");
    DecodeEvents zipEncodedEvents{};
    ExpectEmptyResult(LoadProject(grownArchive.c_str(), {}, decodeObserver(zipEncodedEvents), probed.limits),
                      ProjectIoStatus::TooLarge, "project-load-probe.grown-zip-entry-refused-without-document");
    Expect(zipEncodedEvents.decoded == 0, "project-load-probe.grown-zip-entry-refused-before-decode");

    Expect(WriteFolderFixture(original), "project-load-probe.pixel-fixture-restored");
    Expect(WritePng(grownImage, 5, 1,
                    {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255, 130, 140, 150, 255}, false),
           "project-load-probe.grown-pixel-asset-written");
    auto pixelCeiling = probed.limits;
    pixelCeiling.encodedAssetBytes = kMaxProjectEncodedAssetBytes; // isolate the decoded-pixel ceiling
    DecodeEvents pixelEvents{};
    ExpectEmptyResult(LoadProject(original.RootString().c_str(), {}, decodeObserver(pixelEvents), pixelCeiling),
                      ProjectIoStatus::TooLarge, "project-load-probe.grown-pixels-refused-without-document");
    Expect(pixelEvents.decoded == 0, "project-load-probe.grown-pixels-refused-before-output-decode");
}

void TestProbeMemoryEstimators()
{
    ProjectLoadLimits limits{};
    limits.imagePixels = 4;
    limits.maskPixels = 4;
    limits.layerCount = 2;
    limits.manifestBytes = 4096;
    limits.encodedAssetBytes = 1024;
    constexpr std::size_t pathUnits = 96;
    const std::uint64_t metadata = ProjectLoadMetadataMemoryBytes(limits, pathUnits);
    const std::uint64_t peak = ProjectLoadMemoryBytes(limits, pathUnits);
    const std::uint64_t minimumPixelWorkingSet = 2ull * (4ull * 4 + 4ull);
    Expect(metadata != 0 && peak >= metadata + minimumPixelWorkingSet + limits.encodedAssetBytes,
           "project-load-work.declared-peak-covers-metadata-pixels-and-encoded");

    ProjectLoadLimits morePixels = limits;
    morePixels.imagePixels = 8;
    Expect(ProjectLoadMemoryBytes(morePixels, pathUnits) > peak, "project-load-work.peak-increases-with-image-ceiling");
    ProjectLoadLimits moreEncoded = limits;
    moreEncoded.encodedAssetBytes += 17;
    Expect(ProjectLoadMemoryBytes(moreEncoded, pathUnits) > peak,
           "project-load-work.peak-increases-with-encoded-ceiling");

    const auto rejected = [](ProjectLoadLimits invalid, std::size_t path)
    { return ProjectLoadMetadataMemoryBytes(invalid, path) == 0 && ProjectLoadMemoryBytes(invalid, path) == 0; };
    ProjectLoadLimits invalid = limits;
    invalid.imagePixels = -1;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-negative-pixels");
    invalid = limits;
    invalid.maskPixels = -1;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-negative-mask-pixels");
    invalid = limits;
    invalid.imagePixels = kMaxLayerPixels + 1;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-image-pixels-above-hard-cap");
    invalid = limits;
    invalid.maskPixels = kMaxMaskPixels + 1;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-mask-pixels-above-hard-cap");
    invalid = limits;
    invalid.layerCount = 10001;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-layer-count-above-hard-cap");
    invalid = limits;
    invalid.manifestBytes = 4u * 1024u * 1024u + 1u;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-manifest-above-hard-cap");
    invalid = limits;
    invalid.encodedAssetBytes = 512ull * 1024u * 1024u + 1u;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-encoded-above-hard-cap");
    Expect(ProjectLoadMemoryBytes(limits, 32768) == 0, "project-load-work.rejects-path-outside-owned-buffer-limit");
    Expect(rejected(limits, 0), "project-load-work.rejects-empty-owned-path");
    invalid = limits;
    invalid.manifestBytes = 0;
    Expect(rejected(invalid, pathUnits), "project-load-work.rejects-empty-manifest-plan");
    const ProjectLoadLimits empty{0, 0, 0, 128, 0};
    Expect(ProjectLoadMemoryBytes(empty, pathUnits) == ProjectLoadMetadataMemoryBytes(empty, pathUnits),
           "project-load-work.empty-project-needs-no-asset-reservation");
    const ProjectLoadLimits maximum{};
    Expect(ProjectLoadMemoryBytes(maximum, 32767) != 0 &&
               ProjectLoadMemoryBytes(maximum, 32767) > ProjectLoadMetadataMemoryBytes(maximum, 32767),
           "project-load-work.hard-maxima-have-a-bounded-complete-reservation");

    // Compare the reservation with PixelBuffer's public allocation contract, not with the estimator's formula.
    const ProjectLoadLimits maskOnly{0, kMaxMaskPixels, 1, 128, 1};
    Expect(ProjectLoadMemoryBytes(maskOnly, pathUnits) >= PixelBuffer::RequiredBytes(kMaxMaskPixels),
           "project-load-work.mask-only-peak-covers-one-full-output-buffer");
    const ProjectLoadLimits colorOnly{kMaxLayerPixels, 0, 1, 128, 1};
    Expect(ProjectLoadMemoryBytes(colorOnly, pathUnits) >=
               PixelBuffer::RequiredBytes(static_cast<std::size_t>(kMaxLayerPixels) * 4),
           "project-load-work.color-only-peak-covers-one-full-output-buffer");

    const ProjectLoadLimits recordsOnly{0, 0, static_cast<std::size_t>(kMaxLayerCount), 128, 0};
    Expect(ProjectLoadMetadataMemoryBytes(recordsOnly, pathUnits) >=
               static_cast<std::uint64_t>(kMaxLayerCount) * sizeof(ImageLayer),
           "project-load-work.record-peak-covers-document-layer-array");
    const ProjectLoadLimits manifestOnly{0, 0, 0, kMaxProjectManifestBytes, 0};
    Expect(ProjectLoadMetadataMemoryBytes(manifestOnly, pathUnits) >= kMaxProjectManifestBytes,
           "project-load-work.manifest-peak-covers-complete-retained-input");
}
} // namespace

int RunProjectLoadWorkTests()
{
    gFailures = 0;
    TestFolderAndZipProbe();
    TestProbeFailuresAndCancellation();
    TestChangedInputCeilings();
    TestProbeMemoryEstimators();
    return gFailures;
}

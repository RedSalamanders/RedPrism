#include "IO.Tests.ProjectCancellation.h"

#include "Document.h"
#include "Executor.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
std::atomic<std::uint32_t> gDirectorySequence{0};

constexpr std::array<std::uint8_t, 24> kFixturePixels{
    17, 31, 47, 255, 53, 71, 89, 255, 101, 127, 149, 255, 163, 181, 199, 255, 211, 223, 239, 255, 7, 13, 23, 255,
};
constexpr std::array<std::uint8_t, 24> kReplacementPixels{
    241, 227, 211, 255, 197, 179, 163, 255, 151, 131, 113, 255, 97, 79, 61, 255, 43, 29, 11, 255, 239, 193, 107, 255,
};

constexpr std::array<ProjectIoStage, 6> kStages{
    ProjectIoStage::ManifestReady,  ProjectIoStage::AssetDecoded, ProjectIoStage::AssetEncoded,
    ProjectIoStage::ArchiveWriting, ProjectIoStage::Staged,       ProjectIoStage::Published,
};

struct StageProbe
{
    std::stop_source* source = nullptr;
    ProjectIoStage stopAt = ProjectIoStage::ManifestReady;
    std::array<unsigned int, kStages.size()> visits{};
    ProjectIoStage lastStage = ProjectIoStage::ManifestReady;
    unsigned int lastValue = 0;
};

[[nodiscard]] std::size_t StageIndex(ProjectIoStage stage) noexcept
{
    for (std::size_t index = 0; index < kStages.size(); ++index)
    {
        if (kStages[index] == stage)
        {
            return index;
        }
    }
    return kStages.size();
}

void ObserveStage(void* context, ProjectIoStage stage, unsigned int value) noexcept
{
    auto& probe = *static_cast<StageProbe*>(context);
    probe.lastStage = stage;
    probe.lastValue = value;
    const std::size_t index = StageIndex(stage);
    if (index < probe.visits.size() && probe.visits[index] != std::numeric_limits<unsigned int>::max())
    {
        ++probe.visits[index];
    }
    if (stage == probe.stopAt && probe.source != nullptr)
    {
        (void)probe.source->request_stop();
    }
}

[[nodiscard]] ProjectIoObserver MakeObserver(StageProbe& probe) noexcept
{
    return ProjectIoObserver{&ObserveStage, &probe};
}

[[nodiscard]] CancelToken TokenFor(std::stop_source& source) noexcept
{
    return CancelToken{source.get_token()};
}

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
    }
    else
    {
        std::printf("FAIL %s\n", name);
        ++gFailures;
    }
    std::fflush(stdout);
}

[[nodiscard]] std::filesystem::path CreateUniqueTempDirectory()
{
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
    if (length == 0 || length >= std::size(tempPath))
    {
        return {};
    }

    const std::filesystem::path tempRoot(tempPath);
    const DWORD tempAttributes = GetFileAttributesW(tempRoot.c_str());
    if (tempAttributes == INVALID_FILE_ATTRIBUTES || (tempAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (tempAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return {};
    }

    const std::filesystem::path suiteRoot = tempRoot / L"RedPrism.IoTests";
    std::error_code error;
    std::filesystem::create_directories(suiteRoot, error);
    if (error)
    {
        return {};
    }
    const DWORD suiteAttributes = GetFileAttributesW(suiteRoot.c_str());
    if (suiteAttributes == INVALID_FILE_ATTRIBUTES || (suiteAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (suiteAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return {};
    }

    for (int attempt = 0; attempt < 32; ++attempt)
    {
        const std::uint32_t sequence = gDirectorySequence.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            suiteRoot /
            (L"ProjectCancellation-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(sequence));
        if (CreateDirectoryW(candidate.c_str(), nullptr) != FALSE)
        {
            return candidate;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return {};
        }
    }
    return {};
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes;
    for (std::istreambuf_iterator<char> it(file), end; it != end; ++it)
    {
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(*it)));
    }
    return bytes;
}

[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

struct TreeEntry
{
    std::filesystem::path relativePath;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool operator==(const TreeEntry&) const = default;
};

[[nodiscard]] std::vector<TreeEntry> SnapshotTree(const std::filesystem::path& root)
{
    std::vector<TreeEntry> entries;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error) || error)
        {
            continue;
        }
        entries.push_back(TreeEntry{std::filesystem::relative(it->path(), root, error), ReadBytes(it->path())});
        if (error)
        {
            return {};
        }
    }
    if (error)
    {
        return {};
    }
    std::sort(entries.begin(), entries.end(),
              [](const TreeEntry& left, const TreeEntry& right) { return left.relativePath < right.relativePath; });
    return entries;
}

[[nodiscard]] Raster FixtureRaster(bool replacement = false)
{
    const auto& pixels = replacement ? kReplacementPixels : kFixturePixels;
    return Raster::Make(3, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] CanvasDocument FixtureDocument(bool replacement = false)
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 3;
    document.height = 2;
    document.resolution = replacement ? 300 : 144;

    ImageLayer layer = ImageLayer::Blank(replacement ? L"Replacement Fixture" : L"Cancellation Fixture", Vec2{3, 2});
    layer.id = Guid::Generate();
    const Raster image = FixtureRaster(replacement);
    layer.asset = RasterAsset{image, Raster{}, layer.name};
    layer.opacity = replacement ? 0.625 : 0.75;
    document.layers.push_back(std::move(layer));
    return document;
}

[[nodiscard]] CanvasDocument ArchiveWritingDocument()
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 256;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
    std::uint32_t state = 0x6D2B79F5u;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        pixels[offset] = static_cast<std::uint8_t>(state);
        pixels[offset + 1] = static_cast<std::uint8_t>(state >> 8);
        pixels[offset + 2] = static_cast<std::uint8_t>(state >> 16);
        pixels[offset + 3] = 255;
    }

    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = kWidth;
    document.height = kHeight;
    document.resolution = 144;
    ImageLayer layer = ImageLayer::Blank(L"Archive Progress Fixture", Vec2{kWidth, kHeight});
    layer.id = Guid::Generate();
    const Raster image = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    layer.asset = RasterAsset{image, Raster{}, layer.name};
    document.layers.push_back(std::move(layer));
    return document;
}

[[nodiscard]] CancelToken PreCancelledToken(std::stop_source& source) noexcept
{
    (void)source.request_stop();
    return CancelToken{source.get_token()};
}

void RunCancelledLoad(const std::filesystem::path& path, ProjectIoStage stopAt, const char* caseName)
{
    std::stop_source source;
    StageProbe probe{&source, stopAt};
    const ProjectLoadResult result = LoadProject(path.c_str(), TokenFor(source), MakeObserver(probe));
    const std::size_t index = StageIndex(stopAt);
    const std::string stageName = std::string(caseName) + ".stage-visited";
    const std::string statusName = std::string(caseName) + ".cancelled";
    const std::string resultName = std::string(caseName) + ".no-partial-result";
    Expect(index < probe.visits.size() && probe.visits[index] > 0, stageName.c_str());
    Expect(result.status == ProjectIoStatus::Cancelled, statusName.c_str());
    Expect(result.document.layers.empty() && !result.activeLayerID.has_value() && result.formatVersion == 0,
           resultName.c_str());
}

void RunPreCancelledLoad(const std::filesystem::path& path, const char* caseName)
{
    std::stop_source source;
    const ProjectLoadResult result = LoadProject(path.c_str(), PreCancelledToken(source));
    const std::string statusName = std::string(caseName) + ".cancelled";
    const std::string resultName = std::string(caseName) + ".no-partial-result";
    Expect(result.status == ProjectIoStatus::Cancelled, statusName.c_str());
    Expect(result.document.layers.empty() && !result.activeLayerID.has_value() && result.formatVersion == 0,
           resultName.c_str());
}

void TestCancelledLoads(const std::filesystem::path& root, const CanvasDocument& document,
                        const std::optional<Guid>& activeLayerID)
{
    const std::filesystem::path zipPath = root / L"load.zip.prism";
    const std::filesystem::path explodedPath = root / L"load-exploded";
    Expect(SaveProject(zipPath.c_str(), document, activeLayerID, true) == ProjectIoStatus::Ok,
           "project-cancel.load-fixture-zip");
    Expect(SaveProject(explodedPath.c_str(), document, activeLayerID, false) == ProjectIoStatus::Ok,
           "project-cancel.load-fixture-exploded");
    RunPreCancelledLoad(zipPath, "project-cancel.precancel-load-zip");
    RunPreCancelledLoad(explodedPath, "project-cancel.precancel-load-exploded");
    RunCancelledLoad(zipPath, ProjectIoStage::ManifestReady, "project-cancel.load-zip-manifest-ready");
    RunCancelledLoad(explodedPath, ProjectIoStage::ManifestReady, "project-cancel.load-exploded-manifest-ready");
    RunCancelledLoad(zipPath, ProjectIoStage::AssetDecoded, "project-cancel.load-zip-asset-decoded");
    RunCancelledLoad(explodedPath, ProjectIoStage::AssetDecoded, "project-cancel.load-exploded-asset-decoded");
}

[[nodiscard]] bool CreateSidecar(const std::filesystem::path& path, const char* contents)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error && WriteText(path / L"sentinel.txt", contents);
}

void RunCancelledSave(const std::filesystem::path& root, const CanvasDocument& original,
                      const CanvasDocument& replacement, bool zipArchive, ProjectIoStage stopAt,
                      const wchar_t* directoryName, const char* caseName)
{
    const std::filesystem::path caseRoot = root / directoryName;
    std::error_code error;
    std::filesystem::create_directories(caseRoot, error);
    const std::filesystem::path destination = caseRoot / (zipArchive ? L"project.prism" : L"project");
    const std::filesystem::path temporarySentinel(destination.wstring() + L".~sentinel");
    const std::filesystem::path oldSentinel(destination.wstring() + L".old");
    const std::optional<Guid> originalActive = original.layers.front().id;
    const bool fixtureReady =
        !error && SaveProject(destination.c_str(), original, originalActive, zipArchive) == ProjectIoStatus::Ok &&
        CreateSidecar(temporarySentinel, "unrelated temp-name sibling") &&
        CreateSidecar(oldSentinel, "preexisting old sibling");
    const std::string fixtureName = std::string(caseName) + ".fixture-created";
    Expect(fixtureReady, fixtureName.c_str());
    const std::vector<TreeEntry> before = SnapshotTree(caseRoot);

    std::stop_source source;
    StageProbe probe{&source, stopAt};
    const std::optional<Guid> replacementActive = replacement.layers.front().id;
    const ProjectIoStatus status = SaveProject(destination.c_str(), replacement, replacementActive, zipArchive,
                                               TokenFor(source), MakeObserver(probe));
    const std::size_t index = StageIndex(stopAt);
    const std::string stageName = std::string(caseName) + ".stage-visited";
    const std::string statusName = std::string(caseName) + ".cancelled";
    const std::string preserveName = std::string(caseName) + ".preserves-destination-and-cleans-temp";
    Expect(index < probe.visits.size() && probe.visits[index] > 0, stageName.c_str());
    Expect(status == ProjectIoStatus::Cancelled, statusName.c_str());
    Expect(!before.empty() && SnapshotTree(caseRoot) == before, preserveName.c_str());
}

void RunPreCancelledSave(const std::filesystem::path& root, const CanvasDocument& original,
                         const CanvasDocument& replacement, bool zipArchive, const wchar_t* directoryName,
                         const char* caseName)
{
    const std::filesystem::path caseRoot = root / directoryName;
    std::error_code error;
    std::filesystem::create_directories(caseRoot, error);
    const std::filesystem::path destination = caseRoot / (zipArchive ? L"project.prism" : L"project");
    const bool fixtureReady =
        !error &&
        SaveProject(destination.c_str(), original, original.layers.front().id, zipArchive) == ProjectIoStatus::Ok &&
        CreateSidecar(std::filesystem::path(destination.wstring() + L".~sentinel"), "unrelated temp-name sibling") &&
        CreateSidecar(std::filesystem::path(destination.wstring() + L".old"), "preexisting old sibling");
    const std::string fixtureName = std::string(caseName) + ".fixture-created";
    Expect(fixtureReady, fixtureName.c_str());
    const std::vector<TreeEntry> before = SnapshotTree(caseRoot);
    std::stop_source source;
    const ProjectIoStatus status = SaveProject(destination.c_str(), replacement, replacement.layers.front().id,
                                               zipArchive, PreCancelledToken(source));
    const std::string statusName = std::string(caseName) + ".cancelled";
    const std::string preserveName = std::string(caseName) + ".preserves-destination-and-siblings";
    Expect(status == ProjectIoStatus::Cancelled, statusName.c_str());
    Expect(!before.empty() && SnapshotTree(caseRoot) == before, preserveName.c_str());
}

void TestCancelledSaves(const std::filesystem::path& root, const CanvasDocument& original,
                        const CanvasDocument& replacement)
{
    RunPreCancelledSave(root, original, replacement, true, L"precancel-save-zip", "project-cancel.precancel-save-zip");
    RunPreCancelledSave(root, original, replacement, false, L"precancel-save-exploded",
                        "project-cancel.precancel-save-exploded");
    RunCancelledSave(root, original, replacement, true, ProjectIoStage::AssetEncoded, L"save-zip-asset-encoded",
                     "project-cancel.save-zip-asset-encoded");
    RunCancelledSave(root, original, replacement, false, ProjectIoStage::AssetEncoded, L"save-exploded-asset-encoded",
                     "project-cancel.save-exploded-asset-encoded");
    RunCancelledSave(root, original, replacement, true, ProjectIoStage::Staged, L"save-zip-staged",
                     "project-cancel.save-zip-staged");
    RunCancelledSave(root, original, replacement, false, ProjectIoStage::Staged, L"save-exploded-staged",
                     "project-cancel.save-exploded-staged");
    RunCancelledSave(root, original, ArchiveWritingDocument(), true, ProjectIoStage::ArchiveWriting,
                     L"save-zip-archive-writing", "project-cancel.save-zip-archive-writing");
}

void RunPublishedSave(const std::filesystem::path& root, const CanvasDocument& original,
                      const CanvasDocument& replacement, bool zipArchive, const wchar_t* directoryName,
                      const char* caseName)
{
    const std::filesystem::path caseRoot = root / directoryName;
    std::error_code error;
    std::filesystem::create_directories(caseRoot, error);
    const std::filesystem::path destination = caseRoot / (zipArchive ? L"project.prism" : L"project");
    const std::filesystem::path temporarySentinel(destination.wstring() + L".~sentinel");
    const std::filesystem::path oldSentinel(destination.wstring() + L".old");
    const bool fixtureReady =
        !error &&
        SaveProject(destination.c_str(), original, original.layers.front().id, zipArchive) == ProjectIoStatus::Ok &&
        CreateSidecar(temporarySentinel, "unrelated temp-name sibling") &&
        CreateSidecar(oldSentinel, "preexisting old sibling");
    const std::string fixtureName = std::string(caseName) + ".fixture-created";
    Expect(fixtureReady, fixtureName.c_str());

    std::stop_source source;
    StageProbe probe{&source, ProjectIoStage::Published};
    const ProjectIoStatus status = SaveProject(destination.c_str(), replacement, replacement.layers.front().id,
                                               zipArchive, TokenFor(source), MakeObserver(probe));
    if (status != ProjectIoStatus::Ok)
        std::fprintf(stderr, "%s save status=%u stage=%u value=%u path=%ls\n", caseName,
                     static_cast<unsigned int>(status), static_cast<unsigned int>(probe.lastStage), probe.lastValue,
                     destination.c_str());
    const std::string stageName = std::string(caseName) + ".published-stage-visited";
    const std::string statusName = std::string(caseName) + ".returns-ok-after-stop";
    const std::string siblingName = std::string(caseName) + ".preserves-unrelated-sentinel";
    Expect(probe.visits[StageIndex(ProjectIoStage::Published)] > 0, stageName.c_str());
    Expect(status == ProjectIoStatus::Ok && source.stop_requested(), statusName.c_str());
    Expect(ReadText(temporarySentinel / L"sentinel.txt") == "unrelated temp-name sibling", siblingName.c_str());

    const ProjectLoadResult loaded = LoadProject(destination.c_str());
    const std::string contentName = std::string(caseName) + ".publishes-new-literal-content";
    const std::array<std::uint8_t, 24>& pixels = kReplacementPixels;
    Expect(loaded.status == ProjectIoStatus::Ok && loaded.document.id == replacement.id &&
               loaded.document.width == replacement.width && loaded.document.height == replacement.height &&
               loaded.document.resolution == replacement.resolution && loaded.document.layers.size() == 1 &&
               loaded.document.layers.front().id == replacement.layers.front().id &&
               loaded.document.layers.front().name == replacement.layers.front().name &&
               loaded.document.layers.front().opacity == replacement.layers.front().opacity &&
               loaded.document.layers.front().asset.has_value() &&
               loaded.document.layers.front().asset.value().image.HasStorage() &&
               loaded.document.layers.front().asset.value().image.Width() == 3 &&
               loaded.document.layers.front().asset.value().image.Height() == 2 &&
               loaded.document.layers.front().asset.value().image.Format() == RasterFormat::Bgra8888 &&
               loaded.document.layers.front().asset.value().image.ByteSize() == pixels.size() &&
               std::memcmp(loaded.document.layers.front().asset.value().image.Data(), pixels.data(), pixels.size()) ==
                   0 &&
               loaded.activeLayerID == std::optional<Guid>(replacement.layers.front().id) && loaded.formatVersion > 0,
           contentName.c_str());
}

void TestPublishedStops(const std::filesystem::path& root, const CanvasDocument& original,
                        const CanvasDocument& replacement)
{
    RunPublishedSave(root, original, replacement, true, L"publish-zip", "project-cancel.publish-zip");
    RunPublishedSave(root, original, replacement, false, L"publish-exploded", "project-cancel.publish-exploded");
}

void RunCancelledImport(const std::filesystem::path& imagePath, CanvasDocument& document,
                        std::optional<Guid>& activeLayerID, bool emptyDocument, const char* caseName)
{
    const CanvasDocument before = document;
    const std::optional<Guid> activeBefore = activeLayerID;
    std::stop_source source;
    StageProbe probe{&source, ProjectIoStage::AssetDecoded};
    const ProjectIoStatus status = ImportImageOnto(document, imagePath.c_str(), activeLayerID, std::nullopt,
                                                   TokenFor(source), MakeObserver(probe));
    const std::string stageName = std::string(caseName) + ".asset-decoded";
    const std::string statusName = std::string(caseName) + ".cancelled";
    const std::string preserveName = std::string(caseName) + ".preserves-document-and-active";
    Expect(probe.visits[StageIndex(ProjectIoStage::AssetDecoded)] > 0, stageName.c_str());
    Expect(status == ProjectIoStatus::Cancelled, statusName.c_str());
    Expect(document == before && activeLayerID == activeBefore, preserveName.c_str());
    if (emptyDocument)
    {
        const std::string dimensionsName = std::string(caseName) + ".preserves-empty-canvas-dimensions";
        Expect(document.layers.empty() && document.width == before.width && document.height == before.height,
               dimensionsName.c_str());
    }
}

void TestCancelledImport(const std::filesystem::path& root)
{
    const std::filesystem::path imagePath = root / L"import-source.png";
    const Raster image = FixtureRaster();
    Expect(EncodePngFile(image, 144, imagePath.c_str()) == ImageCodecStatus::Ok, "project-cancel.import-fixture-image");

    CanvasDocument document = FixtureDocument();
    const CanvasDocument preCancelledBefore = document;
    std::optional<Guid> activeLayerID = document.layers.front().id;
    const std::optional<Guid> preCancelledActive = activeLayerID;
    std::stop_source preCancelledSource;
    const ProjectIoStatus preCancelledStatus = ImportImageOnto(document, imagePath.c_str(), activeLayerID, std::nullopt,
                                                               PreCancelledToken(preCancelledSource));
    Expect(preCancelledStatus == ProjectIoStatus::Cancelled, "project-cancel.precancel-import-status");
    Expect(document == preCancelledBefore && activeLayerID == preCancelledActive,
           "project-cancel.precancel-import-preserves-document-and-active");
    RunCancelledImport(imagePath, document, activeLayerID, false, "project-cancel.import-existing");

    CanvasDocument emptyDocument;
    emptyDocument.id = Guid::Generate();
    emptyDocument.width = 17;
    emptyDocument.height = 23;
    emptyDocument.resolution = 96;
    std::optional<Guid> emptyActive;
    RunCancelledImport(imagePath, emptyDocument, emptyActive, true, "project-cancel.import-empty");
}

void TestCancelledExports(const std::filesystem::path& root)
{
    const Raster image = FixtureRaster();
    const std::filesystem::path pngPath = root / L"existing-export.png";
    const std::filesystem::path jpegPath = root / L"existing-export.jpg";
    Expect(ExportDocumentPng(image, 144, pngPath.c_str()) == ProjectIoStatus::Ok, "project-cancel.export-fixture-png");
    Expect(ExportDocumentJpeg(image, JpegExportOptions{0.83, 144}, jpegPath.c_str()) == ProjectIoStatus::Ok,
           "project-cancel.export-fixture-jpeg");
    const std::vector<std::uint8_t> pngBefore = ReadBytes(pngPath);
    const std::vector<std::uint8_t> jpegBefore = ReadBytes(jpegPath);

    std::stop_source pngSource;
    const ProjectIoStatus pngStatus = ExportDocumentPng(image, 144, pngPath.c_str(), PreCancelledToken(pngSource));
    Expect(pngStatus == ProjectIoStatus::Cancelled, "project-cancel.export-png-status");
    Expect(!pngBefore.empty() && ReadBytes(pngPath) == pngBefore, "project-cancel.export-png-preserves-existing-file");

    std::stop_source jpegSource;
    const ProjectIoStatus jpegStatus =
        ExportDocumentJpeg(image, JpegExportOptions{0.83, 144}, jpegPath.c_str(), PreCancelledToken(jpegSource));
    Expect(jpegStatus == ProjectIoStatus::Cancelled, "project-cancel.export-jpeg-status");
    Expect(!jpegBefore.empty() && ReadBytes(jpegPath) == jpegBefore,
           "project-cancel.export-jpeg-preserves-existing-file");
}

[[nodiscard]] bool HasFixtureMetadata(const ProjectLoadResult& loaded, const CanvasDocument& expected)
{
    if (loaded.status != ProjectIoStatus::Ok || loaded.document.id != expected.id ||
        loaded.document.width != expected.width || loaded.document.height != expected.height ||
        loaded.document.resolution != expected.resolution || loaded.document.layers.size() != expected.layers.size() ||
        loaded.document.selection.has_value() != expected.selection.has_value())
    {
        return false;
    }
    for (std::size_t index = 0; index < expected.layers.size(); ++index)
    {
        const ImageLayer& actual = loaded.document.layers[index];
        const ImageLayer& wanted = expected.layers[index];
        if (actual.id != wanted.id || actual.name != wanted.name || actual.visible != wanted.visible ||
            actual.transform != wanted.transform || actual.parentID != wanted.parentID ||
            actual.isGroup != wanted.isGroup || actual.opacity != wanted.opacity ||
            actual.blendMode != wanted.blendMode || actual.maskSourceID != wanted.maskSourceID ||
            actual.mask.has_value() != wanted.mask.has_value() ||
            actual.adjustment.has_value() != wanted.adjustment.has_value() ||
            actual.shape.has_value() != wanted.shape.has_value() ||
            actual.asset.has_value() != wanted.asset.has_value())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasLiteralFixturePixels(const ProjectLoadResult& loaded,
                                           const std::array<std::uint8_t, 24>& expectedPixels)
{
    if (loaded.document.layers.size() != 1 || !loaded.document.layers.front().asset.has_value())
    {
        return false;
    }
    const Raster& actual = loaded.document.layers.front().asset.value().image;
    return actual.HasStorage() && actual.Width() == 3 && actual.Height() == 2 &&
           actual.Format() == RasterFormat::Bgra8888 && actual.ByteSize() == expectedPixels.size() &&
           std::memcmp(actual.Data(), expectedPixels.data(), expectedPixels.size()) == 0;
}

void TestUncancelledRoundTrip(const std::filesystem::path& root)
{
    const CanvasDocument expected = FixtureDocument();
    const std::optional<Guid> activeLayerID = expected.layers.front().id;
    const std::filesystem::path path = root / L"uncancelled-roundtrip.prism";
    const ProjectIoStatus saved = SaveProject(path.c_str(), expected, activeLayerID, true);
    const ProjectLoadResult loaded = LoadProject(path.c_str());
    Expect(saved == ProjectIoStatus::Ok && loaded.status == ProjectIoStatus::Ok,
           "project-cancel.uncancelled-roundtrip-status");
    Expect(HasFixtureMetadata(loaded, expected), "project-cancel.uncancelled-roundtrip-persistent-metadata");
    Expect(loaded.activeLayerID == activeLayerID && loaded.formatVersion > 0,
           "project-cancel.uncancelled-roundtrip-active-and-version");
    Expect(HasLiteralFixturePixels(loaded, kFixturePixels), "project-cancel.uncancelled-roundtrip-literal-pixels");
}
} // namespace

int RunProjectCancellationTests()
{
    gFailures = 0;
    const std::filesystem::path root = CreateUniqueTempDirectory();
    Expect(!root.empty(), "project-cancel.temp-directory-created-exclusively");
    if (root.empty())
    {
        return 1;
    }
    const auto cleanup = wil::scope_exit(
        [&root]() noexcept
        {
            std::error_code error;
            const std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, error);
            if (error)
            {
                return;
            }
            const std::filesystem::path normalizedSuiteRoot =
                std::filesystem::weakly_canonical(root.parent_path(), error);
            if (!error && normalizedRoot.parent_path() == normalizedSuiteRoot)
            {
                std::filesystem::remove_all(normalizedRoot, error);
            }
        });
    (void)cleanup;

    const CanvasDocument document = FixtureDocument();
    const std::optional<Guid> activeLayerID = document.layers.front().id;
    TestCancelledLoads(root, document, activeLayerID);
    const CanvasDocument replacement = FixtureDocument(true);
    TestCancelledSaves(root, document, replacement);
    TestPublishedStops(root, document, replacement);
    TestCancelledImport(root);
    TestCancelledExports(root);
    TestUncancelledRoundTrip(root);
    return gFailures;
}

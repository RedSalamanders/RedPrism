#include "Io.Tests.ProjectDurabilityFaults.h"

#include "../../Common/ProjectStore.h"
#include "../Support/DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
constexpr std::size_t kFaultStageCount = static_cast<std::size_t>(ProjectSaveFaultStage::Count);
constexpr std::size_t kMaxFaultCalls = 32;
constexpr std::size_t kMaxObservedStages = 32;
constexpr std::uint32_t kInjectedError = ERROR_WRITE_FAULT;
constexpr wchar_t kOwnedRootPrefix[] = L"PD-";
constexpr std::array<std::uint8_t, 24> kOriginalPixels{
    17, 31, 47, 255, 53, 71, 89, 255, 101, 127, 149, 255, 163, 181, 199, 255, 211, 223, 239, 255, 7, 13, 23, 255,
};
constexpr std::array<std::uint8_t, 24> kReplacementPixels{
    241, 227, 211, 255, 197, 179, 163, 255, 151, 131, 113, 255, 97, 79, 61, 255, 43, 29, 11, 255, 239, 193, 107, 255,
};
constexpr std::array<ProjectSaveFaultStage, 6> kDurabilityOrder{
    ProjectSaveFaultStage::ManifestFlush, ProjectSaveFaultStage::ManifestClose, ProjectSaveFaultStage::ArchiveCommit,
    ProjectSaveFaultStage::ArchiveFlush,  ProjectSaveFaultStage::ArchiveClose,  ProjectSaveFaultStage::ArchiveReplace,
};

int gFailures = 0;

struct TreeEntry final
{
    std::wstring relativePath;
    std::vector<std::uint8_t> bytes;
    bool directory = false;
    [[nodiscard]] bool operator==(const TreeEntry&) const = default;
};
using TreeSnapshot = std::vector<TreeEntry>;

struct FaultCall final
{
    ProjectSaveFaultStage stage = ProjectSaveFaultStage::ManifestFlush;
    std::uint32_t error = 0;
};

struct FaultTrace final
{
    ProjectSaveFaultStage failAt = ProjectSaveFaultStage::ManifestFlush;
    std::array<unsigned int, kFaultStageCount> visits{};
    std::array<FaultCall, kMaxFaultCalls> calls{};
    std::size_t callCount = 0;
    bool overflow = false;
};

struct ObservedStage final
{
    ProjectIoStage stage = ProjectIoStage::ManifestReady;
    unsigned int value = 0;
};

struct ObserverTrace final
{
    std::array<ObservedStage, kMaxObservedStages> stages{};
    std::size_t count = 0;
    bool overflow = false;
};

struct FaultCase final
{
    const char* name;
    bool zip;
    ProjectSaveFaultStage fault;
    ProjectIoStage expectedErrorStage;
};

constexpr std::array<FaultCase, 8> kFaultCases{{
    {"manifest-flush-zip", true, ProjectSaveFaultStage::ManifestFlush, ProjectIoStage::FlushFailed},
    {"manifest-close-zip", true, ProjectSaveFaultStage::ManifestClose, ProjectIoStage::CloseFailed},
    {"manifest-flush-folder", false, ProjectSaveFaultStage::ManifestFlush, ProjectIoStage::FlushFailed},
    {"manifest-close-folder", false, ProjectSaveFaultStage::ManifestClose, ProjectIoStage::CloseFailed},
    {"archive-commit-zip", true, ProjectSaveFaultStage::ArchiveCommit, ProjectIoStage::CloseFailed},
    {"archive-flush-zip", true, ProjectSaveFaultStage::ArchiveFlush, ProjectIoStage::FlushFailed},
    {"archive-close-zip", true, ProjectSaveFaultStage::ArchiveClose, ProjectIoStage::CloseFailed},
    {"archive-replace-zip", true, ProjectSaveFaultStage::ArchiveReplace, ProjectIoStage::StagingToDestinationFailed},
}};

void Expect(bool condition, const char* caseName, const char* fact) noexcept
{
    std::printf("%s io-project-durability-fault.%s.%s\n", condition ? "PASS" : "FAIL", caseName, fact);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

[[nodiscard]] std::size_t StageIndex(ProjectSaveFaultStage stage) noexcept
{
    return static_cast<std::size_t>(stage);
}

std::uint32_t InjectFault(void* opaque, ProjectSaveFaultStage stage) noexcept
{
    auto& trace = *static_cast<FaultTrace*>(opaque);
    const std::size_t index = StageIndex(stage);
    if (index >= trace.visits.size())
    {
        trace.overflow = true;
        return ERROR_INVALID_PARAMETER;
    }
    ++trace.visits[index];
    const std::uint32_t error = stage == trace.failAt ? kInjectedError : 0;
    if (trace.callCount == trace.calls.size())
    {
        trace.overflow = true;
        return ERROR_INVALID_PARAMETER;
    }
    trace.calls[trace.callCount++] = FaultCall{stage, error};
    return error;
}

void Observe(void* opaque, ProjectIoStage stage, unsigned int value) noexcept
{
    auto& trace = *static_cast<ObserverTrace*>(opaque);
    if (trace.count == trace.stages.size())
    {
        trace.overflow = true;
        return;
    }
    trace.stages[trace.count++] = ObservedStage{stage, value};
}

[[nodiscard]] bool Saw(const ObserverTrace& trace, ProjectIoStage stage, unsigned int value,
                       unsigned int& count) noexcept
{
    count = 0;
    bool exact = false;
    for (std::size_t index = 0; index < trace.count; ++index)
    {
        if (trace.stages[index].stage == stage)
        {
            ++count;
            exact = exact || trace.stages[index].value == value;
        }
    }
    return exact;
}

[[nodiscard]] bool HasStage(const ObserverTrace& trace, ProjectIoStage stage) noexcept
{
    for (std::size_t index = 0; index < trace.count; ++index)
        if (trace.stages[index].stage == stage)
            return true;
    return false;
}

[[nodiscard]] bool FailureBoundaryIsOrdered(const ObserverTrace& trace, const FaultCase& test) noexcept
{
    std::size_t errorIndex = trace.count;
    std::size_t stagedIndex = trace.count;
    unsigned int errorCount = 0;
    for (std::size_t index = 0; index < trace.count; ++index)
    {
        if (trace.stages[index].stage == test.expectedErrorStage && trace.stages[index].value == kInjectedError)
        {
            errorIndex = index;
            ++errorCount;
        }
        if (trace.stages[index].stage == ProjectIoStage::Staged)
            stagedIndex = index;
    }
    if (errorCount != 1)
        return false;
    return test.fault == ProjectSaveFaultStage::ArchiveReplace ? stagedIndex < errorIndex : stagedIndex == trace.count;
}
[[nodiscard]] std::filesystem::path CreateOwnedRoot()
{
    const std::filesystem::path suiteRoot = std::filesystem::temp_directory_path() / L"RedPrism.IoTests";
    std::error_code error;
    std::filesystem::create_directories(suiteRoot, error);
    if (error)
        return {};
    for (unsigned int attempt = 0; attempt < 16; ++attempt)
    {
        const std::filesystem::path candidate =
            // Keep room for the project staging GUID, asset ID and codec temporary GUID.
            // The original long label exceeded MAX_PATH in the non-long-path-aware I/O harness.
            suiteRoot / (std::wstring{kOwnedRootPrefix} + Guid::Generate().ToString().substr(0, 8));
        if (std::filesystem::create_directory(candidate, error))
            return candidate;
        if (error != std::errc::file_exists)
            return {};
        error.clear();
    }
    return {};
}

[[nodiscard]] bool IsOwnedRoot(const std::filesystem::path& root) noexcept
{
    std::error_code error;
    const auto absoluteRoot = std::filesystem::absolute(root, error).lexically_normal();
    if (error)
        return false;
    const auto canonicalRoot = std::filesystem::weakly_canonical(absoluteRoot, error);
    if (error)
        return false;
    const auto suiteRoot =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path() / L"RedPrism.IoTests", error);
    if (error || canonicalRoot.parent_path() != suiteRoot ||
        !canonicalRoot.filename().wstring().starts_with(kOwnedRootPrefix))
        return false;
    // Inspect the original leaf, and refuse any redirection of the exact exclusively created root.
    if (canonicalRoot != absoluteRoot)
        return false;
    const DWORD attributes = GetFileAttributesW(absoluteRoot.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (input.bad() || error || bytes.size() != size)
        return std::nullopt;
    return bytes;
}

[[nodiscard]] std::optional<TreeSnapshot> SnapshotTree(const std::filesystem::path& root)
{
    TreeSnapshot result;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        const auto attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return std::nullopt;
        const auto relative = it->path().lexically_relative(root).generic_wstring();
        if (relative.empty())
            return std::nullopt;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            result.push_back(TreeEntry{relative, {}, true}); // Empty owned staging is observable too.
        else
        {
            auto bytes = ReadBytes(it->path());
            if (!bytes)
                return std::nullopt;
            result.push_back(TreeEntry{relative, std::move(*bytes), false});
        }
    }
    if (error)
        return std::nullopt;
    std::sort(result.begin(), result.end(),
              [](const TreeEntry& left, const TreeEntry& right) { return left.relativePath < right.relativePath; });
    return result;
}

[[nodiscard]] bool WriteSentinel(const std::filesystem::path& path)
{
    constexpr std::array<std::uint8_t, 19> bytes{0x52, 0x50, 0x2D, 0x53, 0x49, 0x42, 0x4C, 0x49, 0x4E, 0x47,
                                                 0x2D, 0x53, 0x45, 0x45, 0x44, 0x2D, 0x31, 0x00, 0xA5};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

[[nodiscard]] bool MatchesProjectEntries(const TreeSnapshot& snapshot, const CanvasDocument& document, bool zip)
{
    // The folder schema uses the document's actual layer identity for its PNG basename.
    // A replacement deliberately has a different identity; retaining the original path set would be wrong.
    std::vector<std::pair<std::wstring, bool>> expected{{L"unrelated-sibling.bin", false}};
    if (zip)
        expected.emplace_back(L"project.prism", false);
    else
    {
        const auto id = document.layers.front().id.ToUtf8();
        expected.emplace_back(L"project-folder", true);
        expected.emplace_back(L"project-folder/images", true);
        expected.emplace_back(L"project-folder/images/" + std::wstring{id.begin(), id.end()} + L".png", false);
        expected.emplace_back(L"project-folder/manifest.json", false);
    }
    std::sort(expected.begin(), expected.end());
    if (snapshot.size() != expected.size())
        return false;
    constexpr std::array<std::uint8_t, 19> sibling{0x52, 0x50, 0x2D, 0x53, 0x49, 0x42, 0x4C, 0x49, 0x4E, 0x47,
                                                   0x2D, 0x53, 0x45, 0x45, 0x44, 0x2D, 0x31, 0x00, 0xA5};
    for (std::size_t index = 0; index < snapshot.size(); ++index)
    {
        const auto& entry = snapshot[index];
        if (entry.relativePath != expected[index].first || entry.directory != expected[index].second)
            return false;
        if (entry.relativePath == L"unrelated-sibling.bin" &&
            (entry.bytes.size() != sibling.size() ||
             !std::equal(entry.bytes.begin(), entry.bytes.end(), sibling.begin())))
            return false;
    }
    return true;
}

[[nodiscard]] CanvasDocument MakeDocument(bool replacement)
{
    const auto documentId =
        Guid::Parse(replacement ? "D2D20000-0000-4000-8000-000000000002" : "D1D10000-0000-4000-8000-000000000001");
    const auto layerId =
        Guid::Parse(replacement ? "A2A20000-0000-4000-8000-000000000002" : "A1A10000-0000-4000-8000-000000000001");
    CanvasDocument document;
    document.id = documentId.value();
    document.width = 3;
    document.height = 2;
    document.resolution = replacement ? 300.0 : 144.0;
    ImageLayer layer =
        ImageLayer::Blank(replacement ? L"Durability Replacement" : L"Durability Original", Vec2{3.0, 2.0});
    layer.id = layerId.value();
    const auto& pixels = replacement ? kReplacementPixels : kOriginalPixels;
    const Raster image = Raster::Make(3, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    layer.asset = RasterAsset{image, Raster{}, layer.name};
    layer.opacity = replacement ? 0.625 : 0.75;
    EditDocumentFixture(document, [&](DocumentFixture& fixture) { fixture.layers.push_back(std::move(layer)); });
    return document;
}

[[nodiscard]] bool HasDocument(const ProjectLoadResult& loaded, const CanvasDocument& expected,
                               const std::array<std::uint8_t, 24>& pixels) noexcept
{
    if (loaded.status != ProjectIoStatus::Ok || loaded.document.id != expected.id ||
        loaded.document.width != expected.width || loaded.document.height != expected.height ||
        loaded.document.resolution != expected.resolution || loaded.document.layers.size() != 1 ||
        loaded.activeLayerID != std::optional<Guid>{expected.layers.front().id})
        return false;
    const ImageLayer& layer = loaded.document.layers.front();
    return layer.id == expected.layers.front().id && layer.name == expected.layers.front().name &&
           layer.opacity == expected.layers.front().opacity && layer.asset.has_value() &&
           layer.asset.value().image.HasStorage() && layer.asset.value().image.Width() == 3 &&
           layer.asset.value().image.Height() == 2 && layer.asset.value().image.Format() == RasterFormat::Bgra8888 &&
           layer.asset.value().image.ByteSize() == pixels.size() &&
           std::memcmp(layer.asset.value().image.Data(), pixels.data(), pixels.size()) == 0;
}

[[nodiscard]] std::size_t FaultVisit(const FaultTrace& trace, ProjectSaveFaultStage stage) noexcept
{
    const std::size_t index = StageIndex(stage);
    return index < trace.visits.size() ? trace.visits[index] : 0;
}

[[nodiscard]] bool LaterStagesNotVisited(const FaultTrace& trace, ProjectSaveFaultStage fault) noexcept
{
    const std::size_t faultIndex = static_cast<std::size_t>(
        std::find(kDurabilityOrder.begin(), kDurabilityOrder.end(), fault) - kDurabilityOrder.begin());
    if (faultIndex >= kDurabilityOrder.size())
        return false;
    for (std::size_t index = faultIndex + 1; index < kDurabilityOrder.size(); ++index)
    {
        if (FaultVisit(trace, kDurabilityOrder[index]) != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool PrefixFaultOrderMatches(const FaultTrace& trace, const FaultCase& test) noexcept
{
    const auto found = std::find(kDurabilityOrder.begin(), kDurabilityOrder.end(), test.fault);
    if (found == kDurabilityOrder.end())
        return false;
    for (auto it = kDurabilityOrder.begin(); it <= found; ++it)
    {
        if (!test.zip && static_cast<std::size_t>(it - kDurabilityOrder.begin()) >= 2)
            return false;
        const std::size_t index = StageIndex(*it);
        const unsigned int expected = 1u;
        if (index >= trace.visits.size() || trace.visits[index] != expected)
            return false;
    }
    // Count equality alone cannot establish callback order. Filter the fixed recorded
    // calls to the durability namespace; cleanup has its own later policy stages.
    std::size_t next = 0;
    for (std::size_t index = 0; index < trace.callCount; ++index)
    {
        const auto stage = trace.calls[index].stage;
        if (stage < ProjectSaveFaultStage::ManifestFlush || stage > ProjectSaveFaultStage::ArchiveReplace)
            continue;
        if (next >= kDurabilityOrder.size() || stage != kDurabilityOrder[next])
            return false;
        ++next;
    }
    return next == static_cast<std::size_t>(found - kDurabilityOrder.begin()) + 1;
}

[[nodiscard]] std::filesystem::path DestinationFor(const std::filesystem::path& caseRoot, bool zip)
{
    return caseRoot / (zip ? L"project.prism" : L"project-folder");
}

[[nodiscard]] bool PrepareOriginal(const std::filesystem::path& caseRoot, const CanvasDocument& original, bool zip,
                                   std::filesystem::path& destination, TreeSnapshot& before)
{
    std::error_code error;
    if (!std::filesystem::create_directory(caseRoot, error) || error)
        return false;
    destination = DestinationFor(caseRoot, zip);
    if (SaveProject(destination.c_str(), original, original.layers.front().id, zip) != ProjectIoStatus::Ok ||
        !WriteSentinel(caseRoot / L"unrelated-sibling.bin"))
        return false;
    auto snapshot = SnapshotTree(caseRoot);
    if (!snapshot || !MatchesProjectEntries(*snapshot, original, zip))
        return false;
    before = std::move(*snapshot);
    return true;
}

void RunFaultCase(const std::filesystem::path& root, const CanvasDocument& original, const CanvasDocument& replacement,
                  const FaultCase& test)
{
    const std::filesystem::path caseRoot = root / test.name;
    std::filesystem::path destination;
    TreeSnapshot before;
    const bool fixtureReady = PrepareOriginal(caseRoot, original, test.zip, destination, before);
    Expect(fixtureReady, test.name, "fixture-ready");
    if (!fixtureReady)
        return;

    FaultTrace faultTrace{test.fault};
    ObserverTrace observerTrace;
    const ProjectIoStatus status =
        SaveProject(destination.c_str(), replacement, replacement.layers.front().id, test.zip, {},
                    ProjectIoObserver{&Observe, &observerTrace}, ProjectSaveFaultPolicy{&InjectFault, &faultTrace});
    Expect(status == ProjectIoStatus::Encode, test.name, "status-encode");

    unsigned int errorEvents = 0;
    const bool exactError = Saw(observerTrace, test.expectedErrorStage, kInjectedError, errorEvents);
    Expect(exactError && errorEvents == 1 && !observerTrace.overflow && FailureBoundaryIsOrdered(observerTrace, test),
           test.name, "exact-error-stage-value-and-order");
    bool targetCallExact = false;
    unsigned int targetCalls = 0;
    for (std::size_t index = 0; index < faultTrace.callCount; ++index)
    {
        if (faultTrace.calls[index].stage == test.fault)
        {
            ++targetCalls;
            targetCallExact = faultTrace.calls[index].error == kInjectedError;
        }
    }
    Expect(targetCalls == 1 && targetCallExact && !faultTrace.overflow && PrefixFaultOrderMatches(faultTrace, test),
           test.name, "fault-callback-once-in-order");

    unsigned int stagedCount = 0;
    for (std::size_t index = 0; index < observerTrace.count; ++index)
        stagedCount += observerTrace.stages[index].stage == ProjectIoStage::Staged ? 1u : 0u;
    const unsigned int expectedStaged = test.fault == ProjectSaveFaultStage::ArchiveReplace ? 1u : 0u;
    Expect(stagedCount == expectedStaged && !HasStage(observerTrace, ProjectIoStage::Published), test.name,
           "staged-and-published-boundary");
    Expect(LaterStagesNotVisited(faultTrace, test.fault), test.name, "later-durability-hooks-not-reached");
    const auto after = SnapshotTree(caseRoot);
    Expect(after && *after == before, test.name, "original-bytes-sibling-and-owned-temp-cleanup");
    const ProjectLoadResult loaded = LoadProject(destination.c_str());
    Expect(HasDocument(loaded, original, kOriginalPixels), test.name, "old-valid-document-loads-exactly");
}

void RunSuccessControl(const std::filesystem::path& root, const CanvasDocument& original,
                       const CanvasDocument& replacement, bool zip)
{
    constexpr const char* kZipName = "success-zip";
    constexpr const char* kFolderName = "success-folder";
    const char* name = zip ? kZipName : kFolderName;
    const std::filesystem::path caseRoot = root / name;
    std::filesystem::path destination;
    TreeSnapshot before;
    const bool fixtureReady = PrepareOriginal(caseRoot, original, zip, destination, before);
    Expect(fixtureReady, name, "fixture-ready");
    if (!fixtureReady)
        return;
    ObserverTrace observerTrace;
    const ProjectIoStatus status = SaveProject(destination.c_str(), replacement, replacement.layers.front().id, zip, {},
                                               ProjectIoObserver{&Observe, &observerTrace});
    Expect(status == ProjectIoStatus::Ok, name, "status-ok");
    unsigned int publications = 0;
    for (std::size_t index = 0; index < observerTrace.count; ++index)
        publications += observerTrace.stages[index].stage == ProjectIoStage::Published ? 1u : 0u;
    Expect(publications == 1 && !observerTrace.overflow, name, "published-once");
    const ProjectLoadResult loaded = LoadProject(destination.c_str());
    Expect(HasDocument(loaded, replacement, kReplacementPixels), name, "replacement-roundtrip-literal-content");
    const auto after = SnapshotTree(caseRoot);
    Expect(after && *after != before && MatchesProjectEntries(*after, replacement, zip), name,
           "destination-replaced-and-sibling-retained");
}

void RunPreCancelledControl(const std::filesystem::path& root, const CanvasDocument& original,
                            const CanvasDocument& replacement, bool zip)
{
    constexpr const char* kZipName = "precancel-zip";
    constexpr const char* kFolderName = "precancel-folder";
    const char* name = zip ? kZipName : kFolderName;
    const std::filesystem::path caseRoot = root / name;
    std::filesystem::path destination;
    TreeSnapshot before;
    const bool fixtureReady = PrepareOriginal(caseRoot, original, zip, destination, before);
    Expect(fixtureReady, name, "fixture-ready");
    if (!fixtureReady)
        return;
    std::stop_source source;
    (void)source.request_stop();
    FaultTrace faultTrace{ProjectSaveFaultStage::ManifestFlush};
    ObserverTrace observerTrace;
    const ProjectIoStatus status = SaveProject(
        destination.c_str(), replacement, replacement.layers.front().id, zip, CancelToken{source.get_token()},
        ProjectIoObserver{&Observe, &observerTrace}, ProjectSaveFaultPolicy{&InjectFault, &faultTrace});
    Expect(status == ProjectIoStatus::Cancelled, name, "status-cancelled-before-work");
    Expect(faultTrace.callCount == 0 && !faultTrace.overflow, name, "fault-policy-not-consulted");
    Expect(!HasStage(observerTrace, ProjectIoStage::Staged) && !HasStage(observerTrace, ProjectIoStage::Published),
           name, "no-staged-or-published-event");
    const auto after = SnapshotTree(caseRoot);
    Expect(after && *after == before, name, "original-bytes-sibling-and-temp-preserved");
    const ProjectLoadResult loaded = LoadProject(destination.c_str());
    Expect(HasDocument(loaded, original, kOriginalPixels), name, "old-valid-document-loads-exactly");
}
} // namespace

int RunIoProjectDurabilityFaultTests()
{
    gFailures = 0;
    const std::filesystem::path root = CreateOwnedRoot();
    Expect(!root.empty(), "setup", "exclusive-guid-temp-root-created");
    if (root.empty())
    {
        std::printf("SUMMARY io-project-durability-fault cases=12 faults=8 controls=4 assertions=87\n");
        return 1;
    }
    const auto cleanup = wil::scope_exit(
        [&root]() noexcept
        {
            if (IsOwnedRoot(root))
            {
                std::error_code error;
                // IsOwnedRoot checked the resolved exact absolute target and its leaf before recursion.
                std::filesystem::remove_all(root, error);
            }
        });
    (void)cleanup;

    const CanvasDocument original = MakeDocument(false);
    const CanvasDocument replacement = MakeDocument(true);
    for (const FaultCase& test : kFaultCases)
        RunFaultCase(root, original, replacement, test);
    RunSuccessControl(root, original, replacement, true);
    RunSuccessControl(root, original, replacement, false);
    RunPreCancelledControl(root, original, replacement, true);
    RunPreCancelledControl(root, original, replacement, false);
    std::printf("SUMMARY io-project-durability-fault cases=12 faults=8 controls=4 assertions=87\n");
    return gFailures;
}

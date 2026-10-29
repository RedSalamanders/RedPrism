#include "Io.Tests.LongPathSave.h"

#include "Common/ImageCodecs.h"
#include "Common/ProjectStore.h"
#include "Common/Win32FilePath.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s long-path-save.%s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

[[nodiscard]] std::wstring Repeat(wchar_t value, std::size_t count)
{
    return std::wstring(count, value);
}

[[nodiscard]] bool MakeDirectory(std::wstring_view path)
{
    const std::wstring terminated{path};
    std::wstring apiPath;
    if (FAILED(Win32FilePath::Prepare(terminated.c_str(), Win32FilePath::Use::Directory, apiPath)))
        return false;
    if (CreateDirectoryW(apiPath.c_str(), nullptr))
        return true;
    const DWORD failure = GetLastError();
    if (failure != ERROR_ALREADY_EXISTS)
        return false;
    const DWORD attributes = GetFileAttributesW(apiPath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

[[nodiscard]] bool RemoveTree(std::wstring_view path)
{
    std::wstring root;
    if (FAILED(Win32FilePath::Prepare(std::wstring{path}.c_str(), Win32FilePath::Use::Directory, root)))
        return false;
    const std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW entry{};
    wil::unique_hfind search{FindFirstFileW(pattern.c_str(), &entry)};
    if (!search)
    {
        const DWORD failure = GetLastError();
        if (failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND)
            return RemoveDirectoryW(root.c_str()) != 0 || GetLastError() == ERROR_PATH_NOT_FOUND;
        return false;
    }
    for (;;)
    {
        const std::wstring_view name{entry.cFileName};
        if (name != L"." && name != L"..")
        {
            const std::wstring child = root + L"\\" + std::wstring{name};
            const bool directory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool reparse = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            bool removed = false;
            if (directory && !reparse)
                removed = RemoveTree(child);
            else if (directory)
            {
                std::wstring apiChild;
                removed = SUCCEEDED(Win32FilePath::Prepare(child.c_str(), Win32FilePath::Use::Directory, apiChild)) &&
                          RemoveDirectoryW(apiChild.c_str()) != 0;
            }
            else
            {
                std::wstring apiChild;
                removed = SUCCEEDED(Win32FilePath::Prepare(child.c_str(), Win32FilePath::Use::File, apiChild)) &&
                          DeleteFileW(apiChild.c_str()) != 0;
            }
            if (!removed)
                return false;
        }
        if (!FindNextFileW(search.get(), &entry))
        {
            const DWORD failure = GetLastError();
            if (failure != ERROR_NO_MORE_FILES)
                return false;
            break;
        }
    }
    search.reset();
    return RemoveDirectoryW(root.c_str()) != 0;
}

[[nodiscard]] std::wstring TempRoot()
{
    const auto bufferStorage = std::make_unique<std::array<wchar_t, 32768>>();
    auto& buffer = *bufferStorage;
    // GetTempPathW has a MAX_PATH return limit and may select USERPROFILE for a long TMP. These explicit
    // long-path fixtures use the requested TMP directory; predecessor fixtures keep their OS-selected root.
    DWORD length = GetEnvironmentVariableW(L"TMP", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
        length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0 || length >= buffer.size())
        return {};
    std::wstring result{buffer.data(), length};
    if (result.back() != L'\\')
        result.push_back(L'\\');
    return result;
}

// Exact MAX_PATH boundary cases need a short base even when the full suite's TEMP is long.
// Walk only the directory spelling; create and remove exclusively uniquely named children of this base.
[[nodiscard]] std::wstring BoundaryTempRoot()
{
    std::wstring root = TempRoot();
    while (root.size() > 80)
    {
        if (root.back() == L'\\')
            root.pop_back();
        const auto separator = root.find_last_of(L'\\');
        if (separator == std::wstring::npos || separator < 2)
            return {};
        root.resize(separator + 1);
    }
    return root;
}

void TestActualTempEnvironment()
{
    const auto expectedStorage = std::make_unique<std::array<wchar_t, 32768>>();
    auto& expected = *expectedStorage;
    const DWORD length = GetEnvironmentVariableW(L"TMP", expected.data(), static_cast<DWORD>(expected.size()));
    std::wstring spelling;
    if (length > 0 && length < expected.size())
    {
        spelling.assign(expected.data(), length);
        if (spelling.back() != L'\\')
            spelling.push_back(L'\\');
    }
    const auto actual = TempRoot();
    Expect(!spelling.empty() && actual == spelling, "runtime-temp-path-matches-explicit-environment");
    std::printf("LONG_PATH_RUNTIME_TEMP code_units=%zu\n", actual.size());
    const auto systemStorage = std::make_unique<std::array<wchar_t, 32768>>();
    const DWORD systemLength = GetTempPathW(static_cast<DWORD>(systemStorage->size()), systemStorage->data());
    std::printf("LONG_PATH_SYSTEM_TEMP code_units=%lu\n", static_cast<unsigned long>(systemLength));
}

[[nodiscard]] CanvasDocument TestDocument()
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 2;
    fixture.height = 2;
    ImageLayer layer = ImageLayer::Blank(L"Long path asset", Vec2{2, 2});
    constexpr std::array<std::uint8_t, 16> pixels{11, 23, 37, 255, 41,  53,  67,  255,
                                                  71, 83, 97, 255, 101, 113, 127, 255};
    const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    fixture.layers.push_back(std::move(layer));
    return std::move(fixture).Seal();
}

[[nodiscard]] bool LoadedDocumentMatches(const ProjectLoadResult& loaded, const Guid& id) noexcept
{
    if (loaded.status != ProjectIoStatus::Ok || loaded.document.id != id || loaded.document.layers.size() != 1 ||
        !loaded.document.layers[0].asset.has_value())
        return false;
    const Raster& raster = loaded.document.layers[0].asset.value().image;
    constexpr std::array<std::uint8_t, 16> expected{11, 23, 37, 255, 41,  53,  67,  255,
                                                    71, 83, 97, 255, 101, 113, 127, 255};
    return raster.Width() == 2 && raster.Height() == 2 && raster.ByteSize() == expected.size() &&
           std::memcmp(raster.Data(), expected.data(), expected.size()) == 0;
}

[[nodiscard]] std::uint32_t FailManifestFlush(void*, ProjectSaveFaultStage stage) noexcept
{
    return stage == ProjectSaveFaultStage::ManifestFlush ? ERROR_WRITE_FAULT : 0;
}

[[nodiscard]] bool HasMatchingSibling(std::wstring_view pattern)
{
    std::wstring apiPattern;
    if (FAILED(Win32FilePath::Prepare(std::wstring{pattern}.c_str(), Win32FilePath::Use::File, apiPattern)))
        return false;
    WIN32_FIND_DATAW entry{};
    wil::unique_hfind search{FindFirstFileW(apiPattern.c_str(), &entry)};
    return static_cast<bool>(search);
}

void TestHelperContracts()
{
    std::wstring input = L"C:\\" + Repeat(L'a', 244);
    std::wstring output;
    Expect(input.size() == 247 &&
               Win32FilePath::PrefixResolvedPath(input.c_str(), Win32FilePath::Use::Directory, output) == S_FALSE &&
               output == input,
           "directory-247-remains-ordinary");
    input = L"C:\\" + Repeat(L'a', 245);
    const HRESULT directoryThreshold =
        Win32FilePath::PrefixResolvedPath(input.c_str(), Win32FilePath::Use::Directory, output);
    Expect(input.size() == 248 && directoryThreshold == S_OK && output == L"\\\\?\\" + input,
           "directory-248-uses-verbatim-prefix");

    input = L"C:\\" + Repeat(L'f', 256);
    const HRESULT fileBelow = Win32FilePath::PrefixResolvedPath(input.c_str(), Win32FilePath::Use::File, output);
    Expect(input.size() == 259 && fileBelow == S_FALSE && output == input, "file-259-remains-ordinary");
    input = L"C:\\" + Repeat(L'f', 257);
    const HRESULT fileThreshold = Win32FilePath::PrefixResolvedPath(input.c_str(), Win32FilePath::Use::File, output);
    Expect(input.size() == 260 && fileThreshold == S_OK && output == L"\\\\?\\" + input,
           "file-260-uses-verbatim-prefix");

    const std::wstring unc = L"\\\\server\\share\\" + Repeat(L'u', 250);
    const HRESULT uncResult = Win32FilePath::PrefixResolvedPath(unc.c_str(), Win32FilePath::Use::File, output);
    Expect(uncResult == S_OK && output == L"\\\\?\\UNC\\server\\share\\" + Repeat(L'u', 250),
           "unc-uses-verbatim-unc-prefix");

    const std::wstring ordinary = L"C:/folder/./child/../file.tmp";
    const HRESULT ordinaryResult = Win32FilePath::Prepare(ordinary.c_str(), Win32FilePath::Use::File, output);
    Expect(ordinaryResult == S_FALSE && output == ordinary, "short-ordinary-spelling-preserved");
    const std::wstring unusualVerbatim = L"\\\\?\\C:\\literal/../trailing. ";
    const HRESULT verbatimResult = Win32FilePath::Prepare(unusualVerbatim.c_str(), Win32FilePath::Use::File, output);
    Expect(verbatimResult == S_FALSE && output == unusualVerbatim, "verbatim-name-preserved-without-trimming");

    const std::wstring temp = BoundaryTempRoot();
    const std::wstring root = temp + L"LongPathResolve-" + Guid::Generate().ToString();
    const std::wstring keep = L"keep-" + Repeat(L'k', 45);
    const std::wstring leaf = L"leaf-" + Repeat(L'l', 45);
    const std::size_t targetLength = root.size() + 1 + keep.size() + 1 + leaf.size();
    const std::size_t dropLength = 252 > targetLength + 5 ? 252 - targetLength - 5 : 1;
    const std::wstring dotted = root + L"\\" + L"drop-" + Repeat(L'd', dropLength) + L"/../" + keep + L"/" + leaf;
    const std::wstring resolvedExpected = root + L"\\" + keep + L"\\" + leaf;
    const HRESULT dottedResult = Win32FilePath::Prepare(dotted.c_str(), Win32FilePath::Use::Directory, output);
    Expect(dotted.size() >= MAX_PATH - 12 && dotted.size() < MAX_PATH && dottedResult == S_FALSE &&
               output == resolvedExpected,
           "ordinary-slash-and-dotdot-resolved-before-verbatim-prefix");
}

void TestAliasingPathPreparation()
{
    std::wstring dos = L"C:\\" + Repeat(L'a', 60) + L"\\" + Repeat(L'b', 200);
    const std::wstring expectedDos = L"\\\\?\\" + dos;
    const HRESULT dosResult = Win32FilePath::Prepare(dos.c_str(), Win32FilePath::Use::File, dos);
    Expect(dosResult == S_OK && dos == expectedDos, "dos-long-input-aliases-output-without-losing-source");

    std::wstring unc = L"\\\\server\\share\\" + Repeat(L'u', 250);
    const std::wstring expectedUnc = L"\\\\?\\UNC\\" + unc.substr(2);
    const HRESULT uncResult = Win32FilePath::Prepare(unc.c_str(), Win32FilePath::Use::File, unc);
    Expect(uncResult == S_OK && unc == expectedUnc, "unc-long-input-aliases-output-without-losing-source");
}

void TestNamespaceEquivalentContainment()
{
    const auto none = [](const std::wstring&) -> std::uint32_t { return 0; };
    const std::wstring dos = L"C:\\Projects\\RedPrism";
    const std::wstring dosExtended = L"\\\\?\\" + dos;
    const std::wstring dosMember = L"C:\\Projects\\RedPrism\\images\\asset.png";
    const std::wstring dosMemberExtended = L"\\\\?\\" + dosMember;
    Expect(ExplodedMemberPathAllowed(dos, dosMemberExtended, none), "dos-ordinary-root-extended-member-equivalent");
    Expect(ExplodedMemberPathAllowed(dosExtended, dosMember, none), "dos-extended-root-ordinary-member-equivalent");

    const std::wstring unc = L"\\\\server\\share\\RedPrism";
    const std::wstring uncExtended = L"\\\\?\\UNC\\server\\share\\RedPrism";
    const std::wstring uncMember = unc + L"\\images\\asset.png";
    const std::wstring uncMemberExtended = L"\\\\?\\UNC\\server\\share\\RedPrism\\images\\asset.png";
    Expect(ExplodedMemberPathAllowed(unc, uncMemberExtended, none), "unc-ordinary-root-extended-member-equivalent");
    Expect(ExplodedMemberPathAllowed(uncExtended, uncMember, none), "unc-extended-root-ordinary-member-equivalent");

    const auto driveRootMatches =
        [](const std::wstring& root, const std::wstring& member, const std::array<std::wstring, 2>& expectedQueries)
    {
        std::vector<std::wstring> queries;
        const auto attributes = [&](const std::wstring& path) -> std::uint32_t
        {
            queries.push_back(path);
            return 0;
        };
        const bool allowed = ExplodedMemberPathAllowed(root, member, attributes);
        return allowed && queries.size() == expectedQueries.size() &&
               std::equal(queries.begin(), queries.end(), expectedQueries.begin());
    };
    Expect(driveRootMatches(L"\\\\?\\C:\\", L"C:\\images\\asset.png", {L"C:\\images", L"C:\\images\\asset.png"}),
           "dos-drive-root-extended-root-ordinary-member-preserves-query-spelling");
    Expect(driveRootMatches(L"\\\\?\\C:\\", L"\\\\?\\C:\\images\\asset.png",
                            {L"\\\\?\\C:\\images", L"\\\\?\\C:\\images\\asset.png"}),
           "dos-drive-root-same-verbatim-namespace-retains-native-containment");
    Expect(driveRootMatches(L"C:\\", L"\\\\?\\C:\\images\\asset.png",
                            {L"\\\\?\\C:\\images", L"\\\\?\\C:\\images\\asset.png"}),
           "dos-drive-root-ordinary-root-extended-member-preserves-query-spelling");

    unsigned int attributeCalls = 0;
    const auto shouldNotBeCalled = [&](const std::wstring&) -> std::uint32_t
    {
        ++attributeCalls;
        return 0;
    };
    Expect(!ExplodedMemberPathAllowed(dos, L"\\\\?\\C:\\Projects\\RedPrismSibling\\images\\asset.png",
                                      shouldNotBeCalled) &&
               attributeCalls == 0,
           "mixed-prefix-sibling-root-boundary-rejected");
    const auto reparseImages = [](const std::wstring& path) -> std::uint32_t
    { return path.ends_with(L"\\images") ? kProjectReparseAttribute : 0; };
    Expect(!ExplodedMemberPathAllowed(dos, dosMemberExtended, reparseImages),
           "mixed-prefix-reparse-component-still-rejected");
    const std::wstring volumeRoot = L"\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\root";
    const std::wstring volumeMember = volumeRoot + L"\\images\\asset.png";
    std::vector<std::wstring> volumeQueries;
    const auto volumeAttributes = [&](const std::wstring& path) -> std::uint32_t
    {
        volumeQueries.push_back(path);
        return 0;
    };
    Expect(ExplodedMemberPathAllowed(volumeRoot, volumeMember, volumeAttributes) && volumeQueries.size() == 2 &&
               volumeQueries[0] == volumeRoot + L"\\images" && volumeQueries[1] == volumeMember,
           "volume-guid-same-namespace-exact-containment-preserves-spelling");

    attributeCalls = 0;
    const std::wstring otherVolumeRoot = L"\\\\?\\Volume{11111111-1111-1111-1111-111111111111}\\root";
    Expect(!ExplodedMemberPathAllowed(volumeRoot, otherVolumeRoot + L"\\images\\asset.png", shouldNotBeCalled) &&
               attributeCalls == 0,
           "volume-guid-distinct-volume-root-refused-before-attributes");
    Expect(!ExplodedMemberPathAllowed(volumeRoot, L"\\\\?\\C:\\Projects\\RedPrism\\images\\asset.png",
                                      shouldNotBeCalled) &&
               attributeCalls == 0,
           "volume-guid-and-dos-verbatim-namespace-not-equated");
    Expect(!ExplodedMemberPathAllowed(volumeRoot, L"\\\\.\\C:\\Projects\\RedPrism\\images\\asset.png",
                                      shouldNotBeCalled) &&
               attributeCalls == 0,
           "volume-guid-and-device-namespace-not-equated");
    Expect(!ExplodedMemberPathAllowed(volumeRoot, volumeRoot + L"Sibling\\images\\asset.png", shouldNotBeCalled) &&
               attributeCalls == 0,
           "volume-guid-sibling-root-boundary-refused");
    volumeQueries.clear();
    Expect(!ExplodedMemberPathAllowed(volumeRoot, volumeRoot + L"\\images\\.\\asset.png", volumeAttributes) &&
               volumeQueries.size() == 1 && volumeQueries[0] == volumeRoot + L"\\images",
           "volume-guid-dot-component-refused");
    volumeQueries.clear();
    Expect(!ExplodedMemberPathAllowed(volumeRoot, volumeRoot + L"\\images\\..\\outside\\asset.png", volumeAttributes) &&
               volumeQueries.size() == 1 && volumeQueries[0] == volumeRoot + L"\\images",
           "volume-guid-dotdot-component-refused");
    const auto volumeReparseImages = [&](const std::wstring& path) -> std::uint32_t
    {
        volumeQueries.push_back(path);
        return path.ends_with(L"\\images") ? kProjectReparseAttribute : 0;
    };
    volumeQueries.clear();
    Expect(!ExplodedMemberPathAllowed(volumeRoot, volumeMember, volumeReparseImages) && volumeQueries.size() == 1 &&
               volumeQueries[0] == volumeRoot + L"\\images",
           "volume-guid-reparse-component-refused-with-original-spelling");
    volumeQueries.clear();
    Expect(!ExplodedMemberPathAllowed(volumeRoot, volumeRoot + L"\\images\\\\asset.png", volumeAttributes) &&
               volumeQueries.size() == 1 && volumeQueries[0] == volumeRoot + L"\\images",
           "volume-guid-empty-component-refused");
}

void TestVolumeGuidAliasRoundTrip()
{
    const std::wstring temp = TempRoot();
    if (temp.size() < 4 || temp[1] != L':' || temp[2] != L'\\')
    {
        std::printf("SKIP long-path-save.volume-guid-alias temp-root-is-not-local-dos\n");
        return;
    }

    const std::wstring driveRoot = temp.substr(0, 3);
    std::array<wchar_t, 128> volumeName{};
    if (!GetVolumeNameForVolumeMountPointW(driveRoot.c_str(), volumeName.data(), static_cast<DWORD>(volumeName.size())))
    {
        std::printf("SKIP long-path-save.volume-guid-alias GetVolumeNameForVolumeMountPointW-win32=%lu\n",
                    GetLastError());
        return;
    }

    std::wstring ordinaryRoot = temp + L"LongPathVolumeAlias-" + Guid::Generate().ToString();
    std::wstring aliasRoot{volumeName.data()};
    aliasRoot += temp.substr(3);
    if (!aliasRoot.empty() && aliasRoot.back() != L'\\')
        aliasRoot.push_back(L'\\');
    const std::wstring leaf = ordinaryRoot.substr(temp.size());
    aliasRoot += leaf;
    const CanvasDocument document = TestDocument();
    const bool created = MakeDirectory(ordinaryRoot);
    const DWORD aliasAttributes = GetFileAttributesW(aliasRoot.c_str());
    Expect(created && aliasAttributes != INVALID_FILE_ATTRIBUTES && (aliasAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
           "volume-guid-temp-root-alias-resolves-to-ordinary-temp-directory");
    const ProjectIoStatus saved =
        created ? SaveProject(aliasRoot.c_str(), document, std::nullopt, false) : ProjectIoStatus::Invalid;
    Expect(saved == ProjectIoStatus::Ok, "volume-guid-alias-exploded-save");
    const ProjectLoadProbeResult probe =
        saved == ProjectIoStatus::Ok ? ProbeProjectLoad(aliasRoot.c_str()) : ProjectLoadProbeResult{};
    Expect(probe.status == ProjectIoStatus::Ok, "volume-guid-alias-exploded-probe");
    const ProjectLoadResult loaded =
        probe.status == ProjectIoStatus::Ok ? LoadProject(aliasRoot.c_str()) : ProjectLoadResult{};
    Expect(LoadedDocumentMatches(loaded, document.id), "volume-guid-alias-exploded-load");
    Expect(RemoveTree(ordinaryRoot), "volume-guid-alias-exploded-cleanup");
}

void TestExplodedBoundaryRoundTrips()
{
    const std::wstring temp = BoundaryTempRoot();
    if (temp.empty())
    {
        Expect(false, "boundary-temp-root-available");
        return;
    }
    const CanvasDocument document = TestDocument();
    for (const std::size_t targetLength : {230u, 247u, 248u})
    {
        const std::wstring fixed = L"LongPathBoundary-" + Guid::Generate().ToString() + L"-";
        if (temp.size() + fixed.size() >= targetLength)
        {
            Expect(false, "boundary-root-spelling-fits-target");
            continue;
        }
        const std::wstring root = temp + fixed + Repeat(L'r', targetLength - temp.size() - fixed.size());
        const bool exactLength = root.size() == targetLength;
        const ProjectIoStatus saved =
            exactLength ? SaveProject(root.c_str(), document, std::nullopt, false) : ProjectIoStatus::Invalid;
        const ProjectLoadProbeResult probe =
            saved == ProjectIoStatus::Ok ? ProbeProjectLoad(root.c_str()) : ProjectLoadProbeResult{};
        const ProjectLoadResult loaded =
            probe.status == ProjectIoStatus::Ok ? LoadProject(root.c_str()) : ProjectLoadResult{};
        const std::string suffix = std::to_string(targetLength);
        Expect(exactLength && saved == ProjectIoStatus::Ok, ("exploded-boundary-" + suffix + "-save").c_str());
        Expect(probe.status == ProjectIoStatus::Ok, ("exploded-boundary-" + suffix + "-probe").c_str());
        Expect(LoadedDocumentMatches(loaded, document.id), ("exploded-boundary-" + suffix + "-load").c_str());
        Expect(RemoveTree(root), ("exploded-boundary-" + suffix + "-cleanup").c_str());
    }
}
void TestLongSaveRoutes()
{
    const std::wstring temp = TempRoot();
    if (temp.empty())
    {
        Expect(false, "temp-root-available");
        return;
    }
    const std::wstring root = temp + L"LongPathSave-r2-" + Guid::Generate().ToString();
    bool created = MakeDirectory(root);
    std::wstring deep = root;
    for (const auto [prefix, letter] :
         {std::pair{L"segment-a-", L'a'}, std::pair{L"segment-b-", L'b'}, std::pair{L"segment-c-", L'c'}})
    {
        deep += L"\\" + std::wstring{prefix} + Repeat(letter, 64);
        created = MakeDirectory(deep) && created;
    }
    Expect(created && deep.size() > MAX_PATH, "unicode-deep-directory-crosses-max-path");
    if (!created)
        return;

    const std::wstring rawProbe = deep + L"\\raw-unprefixed-" + Guid::Generate().ToString() + L".tmp";
    bool rawCreated = false;
    DWORD rawError = ERROR_SUCCESS;
    {
        wil::unique_hfile rawHandle{
            CreateFileW(rawProbe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        rawCreated = static_cast<bool>(rawHandle);
        rawError = rawCreated ? ERROR_SUCCESS : GetLastError();
    }
    std::wstring rawProbeApi;
    const HRESULT rawPrepare =
        Win32FilePath::PrefixResolvedPath(rawProbe.c_str(), Win32FilePath::Use::File, rawProbeApi);
    bool rawClosed = !rawCreated;
    if (rawCreated && rawPrepare == S_OK)
    {
        wil::unique_hfile reopened{CreateFileW(rawProbeApi.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        rawClosed = static_cast<bool>(reopened);
    }
    const bool rawCleaned = rawCreated ? (rawPrepare == S_OK && DeleteFileW(rawProbeApi.c_str()) != 0 &&
                                          GetFileAttributesW(rawProbeApi.c_str()) == INVALID_FILE_ATTRIBUTES)
                                       : true;
    std::printf("DIAG long-path-save.raw-create path-chars=%zu win32=%lu succeeded=%u\n", rawProbe.size(), rawError,
                rawCreated ? 1u : 0u);
    Expect(rawProbe.size() > MAX_PATH && rawClosed && rawCleaned && (rawCreated || rawError != 0),
           "raw-long-api-outcome-recorded");

    const std::wstring prefixProbe = deep + L"\\prefix-unicode-保存-" + Guid::Generate().ToString() + L".tmp";
    std::wstring prefixProbeApi;
    const HRESULT prefixPrepared =
        Win32FilePath::PrefixResolvedPath(prefixProbe.c_str(), Win32FilePath::Use::File, prefixProbeApi);
    wil::unique_hfile prefixHandle;
    bool prefixRoundTrip = false;
    if (SUCCEEDED(prefixPrepared))
    {
        prefixHandle.reset(
            CreateFileW(prefixProbeApi.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (prefixHandle)
        {
            constexpr std::array<std::uint8_t, 4> marker{0x52, 0x50, 0x4C, 0x32};
            DWORD written = 0;
            prefixRoundTrip = WriteFile(prefixHandle.get(), marker.data(), static_cast<DWORD>(marker.size()), &written,
                                        nullptr) != 0 &&
                              written == marker.size() && FlushFileBuffers(prefixHandle.get()) != 0;
            prefixHandle.reset();
            wil::unique_hfile readBack{CreateFileW(prefixProbeApi.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
            std::array<std::uint8_t, marker.size()> actual{};
            DWORD read = 0;
            prefixRoundTrip =
                prefixRoundTrip && readBack &&
                ReadFile(readBack.get(), actual.data(), static_cast<DWORD>(actual.size()), &read, nullptr) &&
                read == actual.size() && actual == marker;
        }
    }
    if (!prefixProbeApi.empty())
        (void)DeleteFileW(prefixProbeApi.c_str());
    std::printf("DIAG long-path-save.explicit-prefix path-chars=%zu hresult=0x%08lx succeeded=%u\n", prefixProbe.size(),
                static_cast<unsigned long>(prefixPrepared), prefixRoundTrip ? 1u : 0u);
    Expect(prefixProbe.size() > MAX_PATH && prefixRoundTrip, "explicit-prefix-create-write-flush-reopen");

    const CanvasDocument document = TestDocument();
    const std::wstring zipPath = deep + L"\\selection-保存-" + Guid::Generate().ToString() + L".prism";
    const ProjectIoStatus zipSave = SaveProject(zipPath.c_str(), document, std::nullopt, true);
    Expect(zipPath.size() > MAX_PATH && zipSave == ProjectIoStatus::Ok, "zip-save-with-generated-long-paths");
    const ProjectLoadResult zipLoad = LoadProject(zipPath.c_str());
    Expect(LoadedDocumentMatches(zipLoad, document.id), "zip-load-after-long-path-save");

    const std::wstring folderPath = deep + L"\\folder-保存-" + Guid::Generate().ToString();
    const ProjectIoStatus folderFirst = SaveProject(folderPath.c_str(), document, std::nullopt, false, {});
    const ProjectIoStatus folderSecond = SaveProject(folderPath.c_str(), document, std::nullopt, false, {});
    Expect(folderFirst == ProjectIoStatus::Ok && folderSecond == ProjectIoStatus::Ok,
           "exploded-save-and-rotation-with-long-paths");
    Expect(LoadedDocumentMatches(LoadProject(folderPath.c_str()), document.id), "exploded-load-after-long-path-save");

    const ProjectSaveFaultPolicy fault{FailManifestFlush, nullptr};
    const ProjectIoStatus failedSave = SaveProject(folderPath.c_str(), document, std::nullopt, false, {}, {}, fault);
    Expect(failedSave == ProjectIoStatus::Encode, "long-path-manifest-fault-is-reported");
    const std::wstring stagedPattern = folderPath + L".~*";
    Expect(!HasMatchingSibling(stagedPattern), "long-path-manifest-fault-removes-staging");
    Expect(LoadedDocumentMatches(LoadProject(folderPath.c_str()), document.id),
           "long-path-manifest-fault-keeps-published-project");

    const std::wstring cleanupPath = root;
    Expect(RemoveTree(cleanupPath), "private-long-path-fixtures-cleaned");
}

struct SiblingCase final
{
    std::size_t inputLeaf;
    bool fallback;
};

constexpr wchar_t kR6Suffix[] = L".~00000000-0000-4000-8000-000000000001";
constexpr std::wstring_view kR6SentinelName = L"RedPrism.~sentinel";
constexpr std::array<std::uint8_t, 8> kR6SentinelBytes{0x52, 0x36, 0x53, 0x45, 0x4E, 0x54, 0x49, 0x4C};

void TestSiblingWithSuffixContracts()
{
    const std::wstring parent = L"C:\\literal-parent";
    const std::wstring suffix{kR6Suffix};
    constexpr std::array<SiblingCase, 6> cases{
        {{216, false}, {217, false}, {218, true}, {233, true}, {234, true}, {255, true}}};
    for (const SiblingCase test : cases)
    {
        const std::wstring leaf = Repeat(L'd', test.inputLeaf);
        const std::wstring destination = parent + L"\\" + leaf;
        const std::wstring expectedLeaf = test.fallback ? L"RedPrism" + suffix : leaf + suffix;
        const std::wstring expected = parent + L"\\" + expectedLeaf;
        std::wstring actual;
        const HRESULT status = Win32FilePath::SiblingWithSuffix(destination.c_str(), suffix, actual);
        const std::string number = std::to_string(test.inputLeaf);
        const std::string label = "helper-leaf-" + number + "-plus-38-exact-parent-and-name";
        Expect(status == S_OK && actual == expected && actual.substr(0, parent.size()) == parent &&
                   actual.substr(actual.find_last_of(L"\\") + 1) == expectedLeaf &&
                   expectedLeaf.size() == (test.fallback ? 46u : test.inputLeaf + 38u),
               label.c_str());
    }

    std::wstring destinationAlias = parent + L"\\" + Repeat(L'a', 218);
    const std::wstring expectedDestinationAlias = parent + L"\\RedPrism" + suffix;
    const HRESULT destinationAliasStatus =
        Win32FilePath::SiblingWithSuffix(destinationAlias.c_str(), suffix, destinationAlias);
    Expect(destinationAliasStatus == S_OK && destinationAlias == expectedDestinationAlias,
           "helper-destination-may-alias-output");

    const std::wstring shortDestination = parent + L"\\alias-source";
    std::wstring suffixAlias{kR6Suffix};
    const std::wstring expectedSuffixAlias = shortDestination + suffixAlias;
    const HRESULT suffixAliasStatus =
        Win32FilePath::SiblingWithSuffix(shortDestination.c_str(), std::wstring_view{suffixAlias}, suffixAlias);
    Expect(suffixAliasStatus == S_OK && suffixAlias == expectedSuffixAlias, "helper-suffix-view-may-alias-output");

    const auto refusesWithoutChangingOutput = [](const wchar_t* path, std::wstring_view invalidSuffix)
    {
        std::wstring output = L"unchanged";
        return Win32FilePath::SiblingWithSuffix(path, invalidSuffix, output) == E_INVALIDARG && output == L"unchanged";
    };
    Expect(refusesWithoutChangingOutput(L"", suffix), "helper-empty-destination-refused-unchanged-output");
    const std::wstring trailing = parent + L"\\";
    Expect(refusesWithoutChangingOutput(trailing.c_str(), suffix),
           "helper-trailing-separator-refused-unchanged-output");
    const std::wstring overlongLeaf = parent + L"\\" + Repeat(L'x', 256);
    Expect(refusesWithoutChangingOutput(overlongLeaf.c_str(), suffix), "helper-256-unit-leaf-refused-unchanged-output");
    Expect(refusesWithoutChangingOutput((parent + L"\\valid").c_str(), L""),
           "helper-empty-suffix-refused-unchanged-output");
    Expect(refusesWithoutChangingOutput((parent + L"\\valid").c_str(), L".~bad/name"),
           "helper-separator-in-suffix-refused-unchanged-output");
    Expect(refusesWithoutChangingOutput((parent + L"\\valid").c_str(), Repeat(L's', 248)),
           "helper-248-unit-suffix-refused-unchanged-output");

    const std::wstring uncParent = L"\\\\server\\share\\literal-parent";
    const std::wstring uncDestination = uncParent + L"\\" + Repeat(L'u', 218);
    const std::wstring uncExpected = uncParent + L"\\RedPrism" + suffix;
    std::wstring uncActual;
    const HRESULT uncStatus = Win32FilePath::SiblingWithSuffix(uncDestination.c_str(), suffix, uncActual);
    Expect(uncStatus == S_OK && uncActual == uncExpected && uncActual.starts_with(uncParent + L"\\"),
           "helper-unc-parent-spelling-preserved");

    const std::wstring verbatimParent = L"\\\\?\\C:\\literal\\parent";
    const std::wstring verbatimDestination = verbatimParent + L"\\" + Repeat(L'v', 218);
    const std::wstring verbatimExpected = verbatimParent + L"\\RedPrism" + suffix;
    std::wstring verbatimActual;
    const HRESULT verbatimStatus =
        Win32FilePath::SiblingWithSuffix(verbatimDestination.c_str(), suffix, verbatimActual);
    Expect(verbatimStatus == S_OK && verbatimActual == verbatimExpected &&
               verbatimActual.starts_with(verbatimParent + L"\\"),
           "helper-verbatim-parent-spelling-preserved");
}

[[nodiscard]] bool ReadR6File(std::wstring_view path, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    const std::wstring terminated{path};
    std::wstring apiPath;
    if (FAILED(Win32FilePath::Prepare(terminated.c_str(), Win32FilePath::Use::File, apiPath)))
        return false;
    wil::unique_hfile file{CreateFileW(apiPath.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
        return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > 64ull * 1024ull * 1024ull)
        return false;
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const DWORD request = static_cast<DWORD>(
            (std::min)(bytes.size() - offset, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD read = 0;
        if (!ReadFile(file.get(), bytes.data() + offset, request, &read, nullptr) || read == 0)
            return false;
        offset += read;
    }
    return true;
}

[[nodiscard]] bool WriteR6Sentinel(std::wstring_view path)
{
    const std::wstring terminated{path};
    std::wstring apiPath;
    if (FAILED(Win32FilePath::Prepare(terminated.c_str(), Win32FilePath::Use::File, apiPath)))
        return false;
    wil::unique_hfile file{
        CreateFileW(apiPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
        return false;
    DWORD written = 0;
    return WriteFile(file.get(), kR6SentinelBytes.data(), static_cast<DWORD>(kR6SentinelBytes.size()), &written,
                     nullptr) != 0 &&
           written == kR6SentinelBytes.size();
}

[[nodiscard]] bool OnlyR6SentinelSibling(std::wstring_view parent)
{
    const std::wstring pattern = std::wstring{parent} + L"\\RedPrism.~*";
    std::wstring apiPattern;
    if (FAILED(Win32FilePath::Prepare(pattern.c_str(), Win32FilePath::Use::File, apiPattern)))
        return false;
    WIN32_FIND_DATAW entry{};
    wil::unique_hfind search{FindFirstFileW(apiPattern.c_str(), &entry)};
    if (!search)
        return false;
    unsigned int count = 0;
    bool exact = true;
    for (;;)
    {
        ++count;
        exact = exact && std::wstring_view{entry.cFileName} == kR6SentinelName &&
                (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        if (!FindNextFileW(search.get(), &entry))
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
                return false;
            break;
        }
    }
    return count == 1 && exact;
}

using R6TreeSnapshot = std::vector<std::pair<std::wstring, std::vector<std::uint8_t>>>;

[[nodiscard]] bool CaptureR6Tree(std::wstring_view root, std::wstring_view relative, R6TreeSnapshot& snapshot)
{
    const std::wstring current =
        relative.empty() ? std::wstring{root} : std::wstring{root} + L"\\" + std::wstring{relative};
    const std::wstring pattern = current + L"\\*";
    std::wstring apiPattern;
    if (FAILED(Win32FilePath::Prepare(pattern.c_str(), Win32FilePath::Use::Directory, apiPattern)))
        return false;
    WIN32_FIND_DATAW entry{};
    wil::unique_hfind search{FindFirstFileW(apiPattern.c_str(), &entry)};
    if (!search)
        return false;
    for (;;)
    {
        const std::wstring_view name{entry.cFileName};
        if (name != L"." && name != L"..")
        {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return false;
            const std::wstring childRelative =
                relative.empty() ? std::wstring{name} : std::wstring{relative} + L"\\" + std::wstring{name};
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (!CaptureR6Tree(root, childRelative, snapshot))
                    return false;
            }
            else
            {
                std::vector<std::uint8_t> bytes;
                if (!ReadR6File(std::wstring{root} + L"\\" + childRelative, bytes))
                    return false;
                snapshot.emplace_back(childRelative, std::move(bytes));
            }
        }
        if (!FindNextFileW(search.get(), &entry))
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
                return false;
            break;
        }
    }
    return true;
}

[[nodiscard]] bool SameR6Tree(const R6TreeSnapshot& left, const R6TreeSnapshot& right)
{
    if (left.size() != right.size())
        return false;
    R6TreeSnapshot orderedLeft = left;
    R6TreeSnapshot orderedRight = right;
    const auto byRelativeName = [](const auto& first, const auto& second) { return first.first < second.first; };
    std::sort(orderedLeft.begin(), orderedLeft.end(), byRelativeName);
    std::sort(orderedRight.begin(), orderedRight.end(), byRelativeName);
    for (std::size_t index = 0; index < orderedLeft.size(); ++index)
    {
        if (orderedLeft[index].first != orderedRight[index].first ||
            orderedLeft[index].second != orderedRight[index].second)
            return false;
    }
    return true;
}

[[nodiscard]] std::wstring LongLeafDestination(std::wstring_view parent, std::wstring_view route,
                                               std::size_t leafLength, std::wstring_view extension)
{
    const std::wstring fixed = L"lp6-" + Guid::Generate().ToString() + L"-" + std::wstring{route} + L"-";
    if (fixed.size() + extension.size() >= leafLength)
        return {};
    const std::wstring leaf =
        fixed + Repeat(L'x', leafLength - fixed.size() - extension.size()) + std::wstring{extension};
    return std::wstring{parent} + L"\\" + leaf;
}

struct R6Fault final
{
    ProjectSaveFaultStage stage;
    unsigned int hits = 0;
};

[[nodiscard]] std::uint32_t FailR6Stage(void* context, ProjectSaveFaultStage stage) noexcept
{
    auto& fault = *static_cast<R6Fault*>(context);
    if (stage != fault.stage)
        return 0;
    ++fault.hits;
    return ERROR_WRITE_FAULT;
}

[[nodiscard]] Raster R6SolidRaster(const std::array<std::uint8_t, 4>& bgra)
{
    constexpr int width = 64;
    constexpr int height = 64;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t offset = 0; offset < pixels.size(); offset += bgra.size())
        std::copy(bgra.begin(), bgra.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

[[nodiscard]] bool R6SolidPixelsNear(const Raster& image, const std::array<std::uint8_t, 4>& expected,
                                     unsigned int tolerance) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || image.Width() != 64 || image.Height() != 64)
        return false;
    for (int y = 0; y < image.Height(); ++y)
    {
        const std::uint8_t* row = image.Data() + static_cast<std::size_t>(y) * image.Stride();
        for (int x = 0; x < image.Width(); ++x)
        {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const unsigned int actual = pixel[channel];
                const unsigned int wanted = expected[channel];
                const unsigned int distance = actual > wanted ? actual - wanted : wanted - actual;
                if (distance > tolerance)
                    return false;
            }
        }
    }
    return true;
}

struct R6Route final
{
    const char* name;
    std::wstring_view leafStem;
    std::wstring_view extension;
    bool zipProject;
    bool folderProject;
};

constexpr std::array<R6Route, 4> kR6Routes{{{"zip", L"zip", L".prism", true, false},
                                            {"folder", L"folder", L"", false, true},
                                            {"png", L"png", L".png", false, false},
                                            {"jpeg", L"jpeg", L".jpg", false, false}}};

void EmitR6RouteRefused(const char* route, std::size_t leafLength)
{
    const std::string prefix = std::string{route} + "-" + std::to_string(leafLength) + "-";
    Expect(false, (prefix + "first-save-ok").c_str());
    Expect(false, (prefix + "first-roundtrip-literal").c_str());
    Expect(false, (prefix + "rewrite-save-ok").c_str());
    Expect(false, (prefix + "rewrite-roundtrip-literal").c_str());
    Expect(false, (prefix + "fault-status").c_str());
    Expect(false, (prefix + "fault-preserves-rewritten-destination").c_str());
    Expect(false, (prefix + "fault-preserves-sentinel-ownership").c_str());
}

[[nodiscard]] ImageCodecStatus R6WriteDestination(const R6Route& route, const std::wstring& path, const Raster& raster)
{
    if (route.name == std::string_view{"png"})
        return EncodePngFile(raster, 72, path.c_str());
    if (route.name == std::string_view{"jpeg"})
        return EncodeJpegFile(raster, JpegExportOptions{1.0, 72}, path.c_str());
    return ImageCodecStatus::Unsupported;
}

[[nodiscard]] bool R6ImageRoundTrip(const R6Route& route, const std::wstring& path,
                                    const std::array<std::uint8_t, 4>& expected)
{
    DecodedImage decoded;
    const ImageCodecStatus status = ImportImageFile(path.c_str(), 64 * 64, decoded);
    const unsigned int tolerance = route.name == std::string_view{"jpeg"} ? 1u : 0u;
    return status == ImageCodecStatus::Ok && R6SolidPixelsNear(decoded.image, expected, tolerance);
}

void RunR6ProjectRoute(const R6Route& route, std::size_t leafLength, std::wstring_view parent,
                       std::wstring_view sentinelPath)
{
    const std::wstring destination = LongLeafDestination(parent, route.leafStem, leafLength, route.extension);
    const bool exactLeaf = !destination.empty() && destination.size() > MAX_PATH &&
                           destination.substr(destination.find_last_of(L'\\') + 1).size() == leafLength;
    auto first = std::make_unique<CanvasDocument>(TestDocument());
    auto rewritten = std::make_unique<CanvasDocument>(TestDocument());
    auto attempted = std::make_unique<CanvasDocument>(TestDocument());
    const ProjectIoStatus firstStatus =
        exactLeaf ? SaveProject(destination.c_str(), *first, std::nullopt, route.zipProject) : ProjectIoStatus::Invalid;
    const ProjectLoadProbeResult firstProbe =
        firstStatus == ProjectIoStatus::Ok ? ProbeProjectLoad(destination.c_str()) : ProjectLoadProbeResult{};
    const ProjectLoadResult firstLoaded =
        firstProbe.status == ProjectIoStatus::Ok ? LoadProject(destination.c_str()) : ProjectLoadResult{};
    const std::string prefix = std::string{route.name} + "-" + std::to_string(leafLength) + "-";
    Expect(exactLeaf && firstStatus == ProjectIoStatus::Ok, (prefix + "first-save-ok").c_str());
    Expect(firstProbe.status == ProjectIoStatus::Ok && LoadedDocumentMatches(firstLoaded, first->id),
           (prefix + "first-roundtrip-literal").c_str());

    const ProjectIoStatus rewriteStatus =
        exactLeaf ? SaveProject(destination.c_str(), *rewritten, std::nullopt, route.zipProject)
                  : ProjectIoStatus::Invalid;
    const ProjectLoadProbeResult rewriteProbe =
        rewriteStatus == ProjectIoStatus::Ok ? ProbeProjectLoad(destination.c_str()) : ProjectLoadProbeResult{};
    const ProjectLoadResult rewriteLoaded =
        rewriteProbe.status == ProjectIoStatus::Ok ? LoadProject(destination.c_str()) : ProjectLoadResult{};
    Expect(exactLeaf && rewriteStatus == ProjectIoStatus::Ok, (prefix + "rewrite-save-ok").c_str());
    Expect(rewriteProbe.status == ProjectIoStatus::Ok && LoadedDocumentMatches(rewriteLoaded, rewritten->id),
           (prefix + "rewrite-roundtrip-literal").c_str());

    std::vector<std::uint8_t> zipBefore;
    R6TreeSnapshot folderBefore;
    const bool baselineCaptured =
        route.zipProject ? ReadR6File(destination, zipBefore) : CaptureR6Tree(destination, L"", folderBefore);
    const ProjectSaveFaultStage stage =
        route.zipProject ? ProjectSaveFaultStage::ArchiveReplace : ProjectSaveFaultStage::StagingToDestinationRename;
    R6Fault fault{stage};
    const ProjectSaveFaultPolicy policy{FailR6Stage, &fault};
    const ProjectIoStatus failed =
        exactLeaf ? SaveProject(destination.c_str(), *attempted, std::nullopt, route.zipProject, {}, {}, policy)
                  : ProjectIoStatus::Invalid;
    Expect(exactLeaf && rewriteProbe.status == ProjectIoStatus::Ok &&
               LoadedDocumentMatches(rewriteLoaded, rewritten->id) && baselineCaptured && fault.hits == 1 &&
               failed == ProjectIoStatus::Encode,
           (prefix + "fault-status").c_str());

    const ProjectLoadProbeResult preservedProbe =
        failed == ProjectIoStatus::Encode ? ProbeProjectLoad(destination.c_str()) : ProjectLoadProbeResult{};
    const ProjectLoadResult preserved =
        preservedProbe.status == ProjectIoStatus::Ok ? LoadProject(destination.c_str()) : ProjectLoadResult{};
    std::vector<std::uint8_t> zipAfter;
    R6TreeSnapshot folderAfter;
    bool backupAbsent = true;
    if (route.folderProject)
    {
        std::wstring backupApi;
        const std::wstring backup = destination + L".old";
        backupAbsent = SUCCEEDED(Win32FilePath::Prepare(backup.c_str(), Win32FilePath::Use::Directory, backupApi)) &&
                       GetFileAttributesW(backupApi.c_str()) == INVALID_FILE_ATTRIBUTES;
    }
    const bool unchanged = route.zipProject ? ReadR6File(destination, zipAfter) && zipBefore == zipAfter
                                            : CaptureR6Tree(destination, L"", folderAfter) &&
                                                  SameR6Tree(folderBefore, folderAfter) && backupAbsent;
    Expect(failed == ProjectIoStatus::Encode && preservedProbe.status == ProjectIoStatus::Ok &&
               LoadedDocumentMatches(preserved, rewritten->id) && unchanged,
           (prefix + "fault-preserves-rewritten-destination").c_str());
    std::vector<std::uint8_t> sentinelAfter;
    const bool sentinelBytesIntact =
        ReadR6File(sentinelPath, sentinelAfter) &&
        std::equal(sentinelAfter.begin(), sentinelAfter.end(), kR6SentinelBytes.begin(), kR6SentinelBytes.end());
    Expect(sentinelBytesIntact && OnlyR6SentinelSibling(parent),
           (prefix + "fault-preserves-sentinel-ownership").c_str());
}

void RunR6ImageRoute(const R6Route& route, std::size_t leafLength, std::wstring_view parent,
                     std::wstring_view sentinelPath, bool cleanupAfter)
{
    const std::wstring destination = LongLeafDestination(parent, route.leafStem, leafLength, route.extension);
    const bool exactLeaf = !destination.empty() && destination.size() > MAX_PATH &&
                           destination.substr(destination.find_last_of(L'\\') + 1).size() == leafLength;
    constexpr std::array<std::uint8_t, 4> firstPixel{40, 80, 120, 255};
    constexpr std::array<std::uint8_t, 4> rewritePixel{20, 100, 60, 255};
    constexpr std::array<std::uint8_t, 4> attemptedPixel{200, 30, 80, 255};
    const Raster first = R6SolidRaster(firstPixel);
    const Raster rewritten = R6SolidRaster(rewritePixel);
    const Raster attempted = R6SolidRaster(attemptedPixel);
    const bool firstSaved = exactLeaf && R6WriteDestination(route, destination, first) == ImageCodecStatus::Ok;
    Expect(firstSaved, (std::string{route.name} + "-" + std::to_string(leafLength) + "-first-save-ok").c_str());
    Expect(firstSaved && R6ImageRoundTrip(route, destination, firstPixel),
           (std::string{route.name} + "-" + std::to_string(leafLength) + "-first-roundtrip-literal").c_str());

    const bool rewriteSaved = exactLeaf && R6WriteDestination(route, destination, rewritten) == ImageCodecStatus::Ok;
    Expect(rewriteSaved, (std::string{route.name} + "-" + std::to_string(leafLength) + "-rewrite-save-ok").c_str());
    Expect(rewriteSaved && R6ImageRoundTrip(route, destination, rewritePixel),
           (std::string{route.name} + "-" + std::to_string(leafLength) + "-rewrite-roundtrip-literal").c_str());

    std::vector<std::uint8_t> before;
    const bool baselineCaptured = ReadR6File(destination, before);
    bool lockAcquired = false;
    ImageCodecStatus failed = ImageCodecStatus::Ok;
    {
        std::wstring apiPath;
        const HRESULT prepared = Win32FilePath::Prepare(destination.c_str(), Win32FilePath::Use::File, apiPath);
        wil::unique_hfile lock;
        if (SUCCEEDED(prepared))
            lock.reset(CreateFileW(apiPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
        lockAcquired = static_cast<bool>(lock);
        if (lockAcquired)
            failed = R6WriteDestination(route, destination, attempted);
    }
    const bool failedAsExpected = lockAcquired && failed == ImageCodecStatus::Encode;
    Expect(rewriteSaved && R6ImageRoundTrip(route, destination, rewritePixel) && baselineCaptured && failedAsExpected,
           (std::string{route.name} + "-" + std::to_string(leafLength) + "-fault-status").c_str());

    std::vector<std::uint8_t> after;
    const bool bytesPreserved = ReadR6File(destination, after) && before == after;
    const bool pixelsPreserved = R6ImageRoundTrip(route, destination, rewritePixel);
    Expect(failedAsExpected && bytesPreserved && pixelsPreserved,
           (std::string{route.name} + "-" + std::to_string(leafLength) + "-fault-preserves-rewritten-destination")
               .c_str());

    std::vector<std::uint8_t> sentinelAfter;
    const bool sentinelBytesIntact =
        ReadR6File(sentinelPath, sentinelAfter) &&
        std::equal(sentinelAfter.begin(), sentinelAfter.end(), kR6SentinelBytes.begin(), kR6SentinelBytes.end());
    const bool sentinelOwned = sentinelBytesIntact && OnlyR6SentinelSibling(parent);
    const bool cleaned = !cleanupAfter || RemoveTree(parent);
    Expect(
        sentinelOwned && cleaned,
        (std::string{route.name} + "-" + std::to_string(leafLength) + "-fault-preserves-sentinel-ownership").c_str());
}

void TestLongLeafSaveRoutes()
{
    std::wstring selected = TempRoot();
    while (!selected.empty() && selected.back() == L'\\')
        selected.pop_back();
    std::wstring ancestor = selected;
    while (ancestor.size() > 40)
    {
        const std::size_t separator = ancestor.find_last_of(L'\\');
        if (separator == std::wstring::npos || separator < 2)
        {
            ancestor.clear();
            break;
        }
        ancestor.resize(separator);
    }
    const std::wstring parent = ancestor.empty() ? std::wstring{} : ancestor + L"\\L6-" + Guid::Generate().ToString();
    const bool parentSpelling = !selected.empty() && !ancestor.empty() && selected.starts_with(ancestor) &&
                                (selected.size() == ancestor.size() || selected[ancestor.size()] == L'\\') &&
                                parent.size() <= 80;
    const bool madeParent = parentSpelling && MakeDirectory(parent);
    std::array<wchar_t, 64> fileSystem{};
    DWORD maximumComponent = 0;
    const auto volumeRoot = std::make_unique<std::array<wchar_t, 32768>>();
    const bool volumeRead =
        madeParent &&
        GetVolumePathNameW(parent.c_str(), volumeRoot->data(), static_cast<DWORD>(volumeRoot->size())) != 0 &&
        GetVolumeInformationW(volumeRoot->data(), nullptr, 0, nullptr, &maximumComponent, nullptr, fileSystem.data(),
                              static_cast<DWORD>(fileSystem.size())) != 0;
    const bool localFilesystem =
        volumeRead && parent.size() >= 3 && parent[1] == L':' && parent[2] == L'\\' &&
        GetDriveTypeW(volumeRoot->data()) == DRIVE_FIXED &&
        (std::wstring_view{fileSystem.data()} == L"NTFS" || std::wstring_view{fileSystem.data()} == L"ReFS");
    const bool sentinelCreated = madeParent && WriteR6Sentinel(parent + L"\\" + std::wstring{kR6SentinelName});
    const bool scope = parentSpelling && madeParent && localFilesystem && maximumComponent == 255 && sentinelCreated;
    Expect(scope, "fixture-parent-local-fixed-ntfs-or-refs-255-component-limit-and-short-path");
    std::printf("DIAG long-path-save.local-filesystem-scope filesystem=%ls max-component=%lu parent-units=%zu\n",
                fileSystem.data(), static_cast<unsigned long>(maximumComponent), parent.size());
    if (!scope)
    {
        std::printf("REFUSAL long-path-save.long-leaf-cases filesystem-or-short-parent-scope-not-proven\n");
        for (const R6Route& route : kR6Routes)
        {
            EmitR6RouteRefused(route.name, 233);
            EmitR6RouteRefused(route.name, 234);
        }
        if (madeParent)
            (void)RemoveTree(parent);
        return;
    }

    for (const std::size_t leafLength : {233u, 234u})
    {
        for (const R6Route& route : kR6Routes)
        {
            if (route.zipProject || route.folderProject)
                RunR6ProjectRoute(route, leafLength, parent, parent + L"\\" + std::wstring{kR6SentinelName});
            else
                RunR6ImageRoute(route, leafLength, parent, parent + L"\\" + std::wstring{kR6SentinelName},
                                leafLength == 234 && route.name == std::string_view{"jpeg"});
        }
    }
}

} // namespace

int RunLongPathSaveTests()
{
    gFailures = 0;
    TestActualTempEnvironment();
    TestHelperContracts();
    TestAliasingPathPreparation();
    TestNamespaceEquivalentContainment();
    TestExplodedBoundaryRoundTrips();
    TestVolumeGuidAliasRoundTrip();
    TestLongSaveRoutes();
    TestSiblingWithSuffixContracts();
    TestLongLeafSaveRoutes();
    return gFailures;
}

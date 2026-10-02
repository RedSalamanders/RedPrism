#include "ProjectPublicationRetryTests.h"

#include "../../Common/Document.h"
#include "../../Common/ProjectStore.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
int gChecks = 0;

void Expect(bool condition, const char* name, bool replacement, unsigned int mode) noexcept
{
    ++gChecks;
    gFailures += condition ? 0 : 1;
    std::printf("%s publication-retry.%s replacement=%u mode=%u\n", condition ? "PASS" : "FAIL", name,
                replacement ? 1u : 0u, mode);
}

using Tree = std::vector<std::pair<std::wstring, std::string>>;

Tree Snapshot(const std::filesystem::path& root)
{
    Tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        std::ifstream input(entry.path(), std::ios::binary);
        result.emplace_back(std::filesystem::relative(entry.path(), root).generic_wstring(),
                            std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}});
    }
    std::sort(result.begin(), result.end());
    return result;
}

enum class Mode : unsigned int
{
    Healthy,
    ReleaseChild,
    ReleaseDirectory,
    PersistentChild,
    CancelAtRetry,
    CancelPersistent,
    CancelAtStaged,
    CancelAtPublished,
    TimerCreate,
    TimerArm,
    TimerWait,
    InjectedSharing,
    InjectedOther,
    UnexpectedDestination
};

struct Control final
{
    Mode mode;
    std::filesystem::path destination;
    bool replacement;
    CancelSource cancellation;
    wil::unique_hfile reader;
    bool opened = false;
    DWORD openError = ERROR_SUCCESS;
    unsigned int publications = 0;
    unsigned int retries = 0;
    unsigned int retryError = 0;
    unsigned int finalError = 0;
    unsigned int rollbackFailures = 0;
    unsigned int timerFaultError = 0;
    bool foreignCreated = false;
    std::array<unsigned int, static_cast<std::size_t>(ProjectSaveFaultStage::PublicationTimerWait) + 1> visits{};
};

bool LocksSource(Mode mode) noexcept
{
    return mode == Mode::ReleaseChild || mode == Mode::ReleaseDirectory || mode == Mode::PersistentChild ||
           mode == Mode::CancelAtRetry || mode == Mode::CancelPersistent || mode == Mode::TimerCreate ||
           mode == Mode::TimerArm || mode == Mode::TimerWait;
}

void Observe(void* opaque, ProjectIoStage stage, unsigned int value) noexcept
{
    auto& control = *static_cast<Control*>(opaque);
    if (stage == ProjectIoStage::Published)
    {
        ++control.publications;
        if (control.mode == Mode::CancelAtPublished)
            (void)control.cancellation.Cancel();
    }
    else if (stage == ProjectIoStage::PublicationRetryScheduled)
    {
        ++control.retries;
        control.retryError = value;
        if (control.mode == Mode::ReleaseChild || control.mode == Mode::ReleaseDirectory ||
            control.mode == Mode::CancelAtRetry)
            control.reader.reset();
        if (control.mode == Mode::CancelAtRetry || control.mode == Mode::CancelPersistent)
            (void)control.cancellation.Cancel();
        // First-save cancellation has no publication-failure observation. Release the persistent reader
        // after requesting stop so owned staging cleanup can run; replacements retain it through exhaustion.
        if (control.mode == Mode::CancelPersistent && !control.replacement)
            control.reader.reset();
    }
    else if (stage == ProjectIoStage::StagingToDestinationFailed)
    {
        control.finalError = value;
        control.reader.reset(); // Keep the native reader through the final attempt, release before cleanup.
    }
    else if (stage == ProjectIoStage::RollbackFailed)
        ++control.rollbackFailures;
}

// This operation seam is reached after all staged writers close and after any destination rotation. It locates
// only the uniquely owned sibling, then creates a real Windows reader; the native rename is never simulated.
std::uint32_t Fault(void* opaque, ProjectSaveFaultStage stage) noexcept
{
    auto& control = *static_cast<Control*>(opaque);
    ++control.visits[static_cast<std::size_t>(stage)];
    if (stage == ProjectSaveFaultStage::StagingToDestinationRename)
    {
        if (control.mode == Mode::InjectedSharing)
            return ERROR_SHARING_VIOLATION;
        if (control.mode == Mode::InjectedOther)
            return ERROR_INVALID_NAME;
        if (control.mode == Mode::UnexpectedDestination)
        {
            if (CreateDirectoryW(control.destination.c_str(), nullptr))
            {
                const auto marker = control.destination / L"foreign.txt";
                wil::unique_hfile file{CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                                   FILE_ATTRIBUTE_NORMAL, nullptr)};
                constexpr char content[] = "Independent foreign destination";
                constexpr DWORD bytes = static_cast<DWORD>(sizeof(content) - 1);
                DWORD written = 0;
                control.foreignCreated =
                    file && WriteFile(file.get(), content, bytes, &written, nullptr) && written == bytes;
            }
        }
        if (LocksSource(control.mode))
        {
            std::error_code error;
            const auto prefix = control.destination.filename().wstring() + L".~";
            for (const auto& entry : std::filesystem::directory_iterator(control.destination.parent_path(), error))
            {
                if (!entry.is_directory(error) || !entry.path().filename().wstring().starts_with(prefix))
                    continue;
                const auto path =
                    control.mode == Mode::ReleaseDirectory ? entry.path() : entry.path() / L"manifest.json";
                control.reader.reset(CreateFileW(
                    path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    control.mode == Mode::ReleaseDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
                    nullptr));
                control.openError = control.reader ? ERROR_SUCCESS : GetLastError();
                control.opened = static_cast<bool>(control.reader);
                break;
            }
        }
    }
    const bool timerFault =
        (control.mode == Mode::TimerCreate && stage == ProjectSaveFaultStage::PublicationTimerCreate) ||
        (control.mode == Mode::TimerArm && stage == ProjectSaveFaultStage::PublicationTimerArm) ||
        (control.mode == Mode::TimerWait && stage == ProjectSaveFaultStage::PublicationTimerWait);
    if (timerFault)
    {
        control.timerFaultError = control.mode == Mode::TimerCreate ? ERROR_NOT_ENOUGH_MEMORY
                                  : control.mode == Mode::TimerArm  ? ERROR_INVALID_HANDLE
                                                                    : ERROR_TIMEOUT;
        return control.timerFaultError;
    }
    return 0;
}

void RunCase(const std::filesystem::path& root, bool replacement, Mode mode)
{
    const auto modeNumber = static_cast<unsigned int>(mode);
    const auto destination = root / (std::to_wstring(replacement ? 1 : 0) + L"-" + std::to_wstring(modeNumber));
    CanvasDocument original;
    original.id = Guid::Generate();
    original.width = 2;
    original.height = 2;
    auto layer = ImageLayer::Blank(L"Original publication", {2, 2});
    constexpr std::array<std::uint8_t, 16> pixels{7, 19, 31, 255, 7, 19, 31, 255, 7, 19, 31, 255, 7, 19, 31, 255};
    layer.asset = RasterAsset{Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, layer.name};
    original.layers.push_back(layer);
    Tree before;
    Tree earlierBackup;
    const auto backup = destination.wstring() + L".old";
    if (replacement)
    {
        const bool prepared = SaveProject(destination.c_str(), original, layer.id, false) == ProjectIoStatus::Ok;
        Expect(prepared, "original-fixture-published", replacement, modeNumber);
        if (!prepared)
            return;
        before = Snapshot(destination);
        if (mode == Mode::CancelAtStaged)
        {
            auto older = original;
            older.resolution = 96;
            const bool backupPrepared = SaveProject(backup.c_str(), older, layer.id, false) == ProjectIoStatus::Ok;
            Expect(backupPrepared, "distinct-earlier-backup-published", replacement, modeNumber);
            if (!backupPrepared)
                return;
            earlierBackup = Snapshot(backup);
        }
    }
    auto next = original;
    next.resolution = 144;
    next.layers.front().name = L"Published replacement";
    Control control{mode, destination, replacement};
    const ProjectIoObserver observer{[](void* opaque, ProjectIoStage stage, unsigned int value) noexcept
                                     {
                                         auto& state = *static_cast<Control*>(opaque);
                                         if (stage == ProjectIoStage::Staged && state.mode == Mode::CancelAtStaged)
                                             (void)state.cancellation.Cancel();
                                         Observe(opaque, stage, value);
                                     },
                                     &control};
    const auto status = SaveProject(destination.c_str(), next, layer.id, false, control.cancellation.Token(), observer,
                                    {&Fault, &control});
    control.reader.reset();
    const bool cancelled = mode == Mode::CancelAtStaged ||
                           (!replacement && (mode == Mode::CancelAtRetry || mode == Mode::CancelPersistent));
    const bool published = mode == Mode::Healthy || mode == Mode::ReleaseChild || mode == Mode::ReleaseDirectory ||
                           mode == Mode::CancelAtPublished || (replacement && mode == Mode::CancelAtRetry);
    Expect(status == (published   ? ProjectIoStatus::Ok
                      : cancelled ? ProjectIoStatus::Cancelled
                                  : ProjectIoStatus::Encode),
           "exact-terminal-status", replacement, modeNumber);
    Expect(control.publications == (published ? 1u : 0u), "truthful-publication-count", replacement, modeNumber);
    const bool foreign = mode == Mode::UnexpectedDestination;
    Expect(control.rollbackFailures == (foreign && replacement ? 1u : 0u), "exact-rollback-failure-count", replacement,
           modeNumber);
    if (LocksSource(mode))
        Expect(control.opened && control.openError == ERROR_SUCCESS, "real-source-reader-opened", replacement,
               modeNumber);
    if (published)
    {
        const auto loaded = LoadProject(destination.c_str());
        const bool valid = loaded.status == ProjectIoStatus::Ok && loaded.document.id == original.id &&
                           loaded.document.resolution == 144 && loaded.document.layers.size() == 1 &&
                           loaded.document.layers.front().name == L"Published replacement" &&
                           loaded.document.layers.front().asset.has_value();
        Expect(valid, "complete-selected-document-loaded", replacement, modeNumber);
        const Raster image = valid ? loaded.document.layers.front().asset.value().image : Raster{};
        Expect(image.HasStorage() && image.Width() == 2 && image.Height() == 2 &&
                   std::equal(pixels.begin(), pixels.end(), image.Data()),
               "literal-pixel-bytes-preserved", replacement, modeNumber);
    }
    else if (foreign)
    {
        const Tree expected{{L"foreign.txt", "Independent foreign destination"}};
        Expect(control.foreignCreated && Snapshot(destination) == expected,
               "unexpected-destination-never-replaced-or-cleaned", replacement, modeNumber);
        Expect(control.finalError != 0 && control.retries <= 2, "unexpected-destination-reports-native-failure",
               replacement, modeNumber);
    }
    else if (replacement)
    {
        Expect(std::filesystem::is_directory(destination) && Snapshot(destination) == before,
               "byte-identical-original-destination", replacement, modeNumber);
    }
    else
        Expect(!std::filesystem::exists(destination), "first-save-never-partially-published", replacement, modeNumber);
    const bool retainedEarlierBackup = replacement && mode == Mode::CancelAtStaged;
    Expect(foreign && replacement  ? std::filesystem::is_directory(backup) && Snapshot(backup) == before
           : retainedEarlierBackup ? std::filesystem::is_directory(backup) && Snapshot(backup) == earlierBackup
                                   : !std::filesystem::exists(backup),
           "exact-earlier-backup-or-no-backup", replacement, modeNumber);
    std::size_t staged = 0;
    const auto prefix = destination.filename().wstring() + L".~";
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.path().filename().wstring().starts_with(prefix))
            ++staged;
    Expect(staged == 0, "owned-staging-cleaned", replacement, modeNumber);
    const auto visits = [&](ProjectSaveFaultStage stage) { return control.visits[static_cast<std::size_t>(stage)]; };
    const bool injected = mode == Mode::InjectedSharing || mode == Mode::InjectedOther;
    if (injected || mode == Mode::CancelAtStaged)
    {
        Expect(control.retries == 0 && visits(ProjectSaveFaultStage::PublicationTimerCreate) == 0 &&
                   visits(ProjectSaveFaultStage::PublicationTimerArm) == 0 &&
                   visits(ProjectSaveFaultStage::PublicationTimerWait) == 0,
               "final-injection-or-early-cancel-skips-timers", replacement, modeNumber);
        if (injected)
            Expect(control.finalError == static_cast<unsigned int>(mode == Mode::InjectedSharing
                                                                       ? ERROR_SHARING_VIOLATION
                                                                       : ERROR_INVALID_NAME),
                   "injected-native-error-preserved", replacement, modeNumber);
        else
            Expect(visits(ProjectSaveFaultStage::DestinationToBackupRename) == 0 &&
                       visits(ProjectSaveFaultStage::StagingToDestinationRename) == 0 &&
                       visits(ProjectSaveFaultStage::BackupCleanup) == 0,
                   "early-stop-preserves-both-external-trees", replacement, modeNumber);
    }
    else if (LocksSource(mode))
    {
        const bool timerCreate = mode == Mode::TimerCreate;
        const bool timerArm = mode == Mode::TimerArm;
        const bool exhausted = mode == Mode::PersistentChild || (replacement && mode == Mode::CancelPersistent);
        const unsigned int expectedRetries = timerCreate || timerArm ? 0u : exhausted ? 2u : 1u;
        Expect(control.retries == expectedRetries &&
                   (control.retries == 0 || control.retryError == ERROR_ACCESS_DENIED ||
                    control.retryError == ERROR_SHARING_VIOLATION),
               "exact-bounded-native-retry-events", replacement, modeNumber);
        if (!published && !cancelled)
            Expect((control.finalError == ERROR_ACCESS_DENIED || control.finalError == ERROR_SHARING_VIOLATION) &&
                       control.finalError != control.timerFaultError,
                   "terminal-native-error-survives-resource-fault", replacement, modeNumber);
        Expect(visits(ProjectSaveFaultStage::PublicationTimerCreate) == 1 &&
                   visits(ProjectSaveFaultStage::PublicationTimerArm) ==
                       (timerCreate ? 0u : expectedRetries + (timerArm ? 1u : 0u)) &&
                   visits(ProjectSaveFaultStage::PublicationTimerWait) ==
                       (timerCreate || timerArm || cancelled ? 0u : expectedRetries),
               "exact-timer-hook-boundaries", replacement, modeNumber);
    }
    else
        Expect(control.retries <= 2 &&
                   (control.retries != 0 || (visits(ProjectSaveFaultStage::PublicationTimerCreate) == 0 &&
                                             visits(ProjectSaveFaultStage::PublicationTimerArm) == 0 &&
                                             visits(ProjectSaveFaultStage::PublicationTimerWait) == 0)),
               "healthy-native-rename-has-no-timer", replacement, modeNumber);
    if (status != (published ? ProjectIoStatus::Ok : cancelled ? ProjectIoStatus::Cancelled : ProjectIoStatus::Encode))
        std::printf("DIAG publication-retry status=%u error=%u retry-error=%u retries=%u open=%lu\n",
                    static_cast<unsigned int>(status), control.finalError, control.retryError, control.retries,
                    control.openError);
}
} // namespace

int RunProjectPublicationRetryTests()
{
    gFailures = 0;
    gChecks = 0;
    const auto parent = std::filesystem::temp_directory_path() / L"RedPrism.IoTests";
    std::filesystem::create_directories(parent);
    std::filesystem::path root;
    for (unsigned int attempt = 0; attempt < 16; ++attempt)
    {
        const auto candidate = parent / (L"publication-" + Guid::Generate().ToString().substr(0, 8));
        if (std::filesystem::create_directory(candidate))
        {
            root = candidate;
            break;
        }
    }
    if (root.empty())
    {
        std::printf("FAIL publication-retry.exclusive-fixture-root\n");
        return 1;
    }
    for (const bool replacement : {false, true})
        for (const auto mode :
             {Mode::Healthy, Mode::ReleaseChild, Mode::ReleaseDirectory, Mode::PersistentChild, Mode::CancelAtRetry,
              Mode::CancelPersistent, Mode::CancelAtStaged, Mode::CancelAtPublished, Mode::TimerCreate, Mode::TimerArm,
              Mode::TimerWait, Mode::InjectedSharing, Mode::InjectedOther, Mode::UnexpectedDestination})
            RunCase(root, replacement, mode);
    std::printf("publication-retry summary: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures;
}

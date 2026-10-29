#include "Application.h"

#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "Resources.h"
#include "Runtime.Tests.EndSessionWriteTestGate.h"
#include "Runtime.Tests.ExportWorkTestGate.h"
#include "Runtime.Tests.ProjectSaveTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "Win32FilePath.h"
#include "WorkerWriteCompletion.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ReleaseWriter final
{
    HANDLE waitEntered = nullptr;
    HANDLE release = nullptr;
    HANDLE nativeWaitReturned = nullptr; // optional controlled deadline: no producer release before owner return
    std::atomic<bool> observed{false}, released{false};
};
#pragma warning(pop)

DWORD WINAPI ReleaseWriterAfterWait(void* raw) noexcept
{
    auto& state = *static_cast<ReleaseWriter*>(raw);
    const DWORD result = WaitForSingleObject(state.waitEntered, 10000);
    state.observed.store(result == WAIT_OBJECT_0);
    if (state.nativeWaitReturned && WaitForSingleObject(state.nativeWaitReturned, 10000) != WAIT_OBJECT_0)
        return 1;
    state.released.store(SetEvent(state.release) != FALSE);
    return 0;
}

bool ReadLiteralFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::wstring api;
    if (FAILED(Win32FilePath::Prepare(path.c_str(), Win32FilePath::Use::File, api)))
        return false;
    wil::unique_hfile file(CreateFileW(api.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart < 0 || length.QuadPart > 1024 * 1024)
        return false;
    bytes.resize(static_cast<std::size_t>(length.QuadPart));
    DWORD read = 0;
    return ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE &&
           read == bytes.size();
}

struct SessionProjectEntry final
{
    std::wstring relative;
    bool directory = false;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] bool operator==(const SessionProjectEntry&) const = default;
};

bool ReadLiteralTree(const std::filesystem::path& root, std::vector<SessionProjectEntry>& entries)
{
    entries.clear();
    std::wstring rootApi;
    if (FAILED(Win32FilePath::Prepare(root.c_str(), Win32FilePath::Use::Directory, rootApi)))
        return false;
    const std::filesystem::path preparedRoot{rootApi};
    const auto rootAttributes = GetFileAttributesW(preparedRoot.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(preparedRoot, error), end;
    if (error)
        return false;
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
            return false;
        const auto& path = iterator->path();
        std::wstring api;
        if (FAILED(Win32FilePath::Prepare(path.c_str(), Win32FilePath::Use::File, api)))
            return false;
        const auto attributes = GetFileAttributesW(api.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return false;
        SessionProjectEntry entry{
            path.lexically_relative(preparedRoot).generic_wstring(), (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, {}};
        if (!entry.directory && !ReadLiteralFile(path, entry.bytes))
            return false;
        entries.push_back(std::move(entry));
    }
    if (error)
        return false;
    std::ranges::sort(entries, {}, &SessionProjectEntry::relative);
    return true;
}

bool HasNoOwnedSaveSiblings(const std::filesystem::path& destination, bool retainedBackup = false)
{
    const auto stagingPrefix = destination.filename().wstring() + L".~";
    const auto backupName = destination.filename().wstring() + L".old";
    std::error_code error;
    std::filesystem::directory_iterator iterator(destination.parent_path(), error), end;
    if (error)
        return false;
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
            return false;
        const auto name = iterator->path().filename().wstring();
        if (name.starts_with(stagingPrefix) || (!retainedBackup && name == backupName))
            return false;
    }
    return !error;
}

CanvasDocument LiteralFileDocument(std::uint32_t color)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Session-end literal", Vec2{2, 1});
    std::array<std::uint8_t, 8> pixels{};
    for (std::size_t pixel = 0; pixel < 2; ++pixel)
        for (std::size_t channel = 0; channel < 4; ++channel)
            pixels[pixel * 4 + channel] = static_cast<std::uint8_t>(color >> (channel * 8));
    layer.asset =
        RasterAsset{Raster::Make(2, 1, RasterFormat::Bgra8888, pixels.data(), sizeof(pixels)), {}, layer.name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

bool HasLiteralPixel(const Raster& image, unsigned int channel, unsigned int tolerance = 0) noexcept
{
    if (!image.HasStorage() || image.Width() != 2 || image.Height() != 1)
        return false;
    std::array<std::uint8_t, 8> bytes{};
    image.ReadRegion(0, 0, 2, 1, bytes.data(), static_cast<int>(bytes.size()), 0);
    for (std::size_t pixel = 0; pixel < 2; ++pixel)
    {
        for (std::size_t color = 0; color < 3; ++color)
        {
            const unsigned int value = bytes[pixel * 4 + color];
            if (value + tolerance < channel || value > channel + tolerance)
                return false;
        }
        if (bytes[pixel * 4 + 3] != 255)
            return false;
    }
    return true;
}
} // namespace

int Application::RunFileEndSessionSelfTest() noexcept
{
    enum class RecoveryMode : unsigned int
    {
        None,
        Exhaustion,
        TimerCreate,
        TimerArm,
        TimerWait
    };
    struct Scenario final
    {
        const char* name;
        HostWriteFormat format;
        bool afterPublication;
        bool ending;
        bool timeout;
        bool readback;
        bool savedReadback = false;
        bool pendingPrompt = false;
        ProjectSaveFaultStage fault = ProjectSaveFaultStage::Count;
        bool folder = false;
        bool publishedCleanup = false;
        bool failedRollback = false;
        RecoveryMode recovery = RecoveryMode::None;
        bool timeoutFault = false;
        bool lockedOutput = false;
        bool nativeAssetError = false;
        bool holdUntilNativeReturn = false;
    };
    constexpr auto faultCase = [](const char* name, ProjectSaveFaultStage stage, bool ending) constexpr noexcept
    { return Scenario{name, HostWriteFormat::Project, false, ending, false, false, false, false, stage}; };
    constexpr auto folderFaultCase = [](const char* name, ProjectSaveFaultStage stage, bool ending) constexpr noexcept
    { return Scenario{name, HostWriteFormat::Project, false, ending, false, false, false, false, stage, true}; };
    constexpr auto cleanupCase = [](const char* name, bool ending) constexpr noexcept
    {
        return Scenario{name,
                        HostWriteFormat::Project,
                        false,
                        ending,
                        false,
                        false,
                        false,
                        false,
                        ProjectSaveFaultStage::BackupCleanup,
                        true,
                        true};
    };
    constexpr auto rollbackCase = [](const char* name, bool ending) constexpr noexcept
    {
        return Scenario{name,
                        HostWriteFormat::Project,
                        false,
                        ending,
                        false,
                        false,
                        false,
                        false,
                        ProjectSaveFaultStage::RollbackRename,
                        true,
                        false,
                        true};
    };
    constexpr auto recoveryCase = [](const char* name, bool ending, RecoveryMode mode) constexpr noexcept
    {
        const auto stage = mode == RecoveryMode::TimerCreate ? ProjectSaveFaultStage::RecoveryTimerCreate
                           : mode == RecoveryMode::TimerArm  ? ProjectSaveFaultStage::RecoveryTimerArm
                           : mode == RecoveryMode::TimerWait ? ProjectSaveFaultStage::RecoveryTimerWait
                                                             : ProjectSaveFaultStage::Count;
        return Scenario{
            name, HostWriteFormat::Project, false, ending, false, false, false, false, stage, true, false, false, mode};
    };
    constexpr auto timeoutFaultCase = [](const char* name, bool folder, ProjectSaveFaultStage stage) constexpr noexcept
    {
        return Scenario{name,  HostWriteFormat::Project, false, true, true, false, false, false, stage, folder, false,
                        false, RecoveryMode::None,       true};
    };
    constexpr auto lockedExportCase = [](const char* name, HostWriteFormat format, bool ending) constexpr noexcept
    {
        return Scenario{name,
                        format,
                        false,
                        ending,
                        false,
                        false,
                        false,
                        false,
                        ProjectSaveFaultStage::Count,
                        false,
                        false,
                        false,
                        RecoveryMode::None,
                        false,
                        true};
    };
    constexpr auto nativeAssetCase = [](const char* name, bool folder, bool ending) constexpr noexcept
    {
        return Scenario{name,
                        HostWriteFormat::Project,
                        false,
                        ending,
                        false,
                        false,
                        false,
                        false,
                        ProjectSaveFaultStage::Count,
                        folder,
                        false,
                        false,
                        RecoveryMode::None,
                        false,
                        false,
                        true};
    };
    constexpr auto deadlineCase = [](const char* name, bool published) constexpr noexcept
    {
        auto result = Scenario{name, HostWriteFormat::Project, published, true, false, false, false, true};
        result.holdUntilNativeReturn = true;
        return result;
    };
    constexpr std::array scenarios{
        Scenario{"save-before-publication", HostWriteFormat::Project, false, true, false, false},
        Scenario{"save-after-publication", HostWriteFormat::Project, true, true, false, false},
        Scenario{"save-cancelled-session", HostWriteFormat::Project, false, false, false, false},
        Scenario{"save-timeout", HostWriteFormat::Project, false, true, true, false},
        Scenario{"png-before-publication", HostWriteFormat::Png, false, true, false, false},
        Scenario{"png-after-publication", HostWriteFormat::Png, true, true, false, false},
        Scenario{"png-cancelled-session", HostWriteFormat::Png, false, false, false, false},
        Scenario{"png-timeout", HostWriteFormat::Png, false, true, true, false},
        Scenario{"png-readback-cancel", HostWriteFormat::Png, false, true, false, true},
        Scenario{"png-saved-readback-cancel", HostWriteFormat::Png, false, true, false, true, true},
        Scenario{"jpeg-worker-wait", HostWriteFormat::Jpeg, false, true, false, false},
        Scenario{"save-with-earlier-unsaved-query", HostWriteFormat::Project, false, true, false, false, false, true},
        deadlineCase("save-deadline-before-publication-with-earlier-unsaved-query", false),
        deadlineCase("save-deadline-after-publication-with-earlier-unsaved-query", true),
        faultCase("save-manifest-flush-end", ProjectSaveFaultStage::ManifestFlush, true),
        faultCase("save-manifest-flush-false", ProjectSaveFaultStage::ManifestFlush, false),
        faultCase("save-manifest-close-end", ProjectSaveFaultStage::ManifestClose, true),
        faultCase("save-manifest-close-false", ProjectSaveFaultStage::ManifestClose, false),
        faultCase("save-archive-commit-end", ProjectSaveFaultStage::ArchiveCommit, true),
        faultCase("save-archive-commit-false", ProjectSaveFaultStage::ArchiveCommit, false),
        faultCase("save-archive-flush-end", ProjectSaveFaultStage::ArchiveFlush, true),
        faultCase("save-archive-flush-false", ProjectSaveFaultStage::ArchiveFlush, false),
        faultCase("save-archive-close-end", ProjectSaveFaultStage::ArchiveClose, true),
        faultCase("save-archive-close-false", ProjectSaveFaultStage::ArchiveClose, false),
        faultCase("save-archive-replace-end", ProjectSaveFaultStage::ArchiveReplace, true),
        faultCase("save-archive-replace-false", ProjectSaveFaultStage::ArchiveReplace, false),
        folderFaultCase("folder-manifest-flush-end", ProjectSaveFaultStage::ManifestFlush, true),
        folderFaultCase("folder-manifest-flush-false", ProjectSaveFaultStage::ManifestFlush, false),
        folderFaultCase("folder-manifest-close-end", ProjectSaveFaultStage::ManifestClose, true),
        folderFaultCase("folder-manifest-close-false", ProjectSaveFaultStage::ManifestClose, false),
        folderFaultCase("folder-rotation-end", ProjectSaveFaultStage::DestinationToBackupRename, true),
        folderFaultCase("folder-rotation-false", ProjectSaveFaultStage::DestinationToBackupRename, false),
        folderFaultCase("folder-publication-end", ProjectSaveFaultStage::StagingToDestinationRename, true),
        folderFaultCase("folder-publication-false", ProjectSaveFaultStage::StagingToDestinationRename, false),
        cleanupCase("folder-published-cleanup-end", true),
        cleanupCase("folder-published-cleanup-false", false),
        rollbackCase("folder-failed-rollback-end", true),
        rollbackCase("folder-failed-rollback-false", false),
        recoveryCase("folder-recovery-exhaustion-end", true, RecoveryMode::Exhaustion),
        recoveryCase("folder-recovery-exhaustion-false", false, RecoveryMode::Exhaustion),
        recoveryCase("folder-recovery-create-end", true, RecoveryMode::TimerCreate),
        recoveryCase("folder-recovery-create-false", false, RecoveryMode::TimerCreate),
        recoveryCase("folder-recovery-arm-end", true, RecoveryMode::TimerArm),
        recoveryCase("folder-recovery-arm-false", false, RecoveryMode::TimerArm),
        recoveryCase("folder-recovery-wait-end", true, RecoveryMode::TimerWait),
        recoveryCase("folder-recovery-wait-false", false, RecoveryMode::TimerWait),
        timeoutFaultCase("save-late-manifest-close-timeout", false, ProjectSaveFaultStage::ManifestClose),
        timeoutFaultCase("folder-late-publication-timeout", true, ProjectSaveFaultStage::StagingToDestinationRename),
        lockedExportCase("png-native-locked-output-end", HostWriteFormat::Png, true),
        lockedExportCase("png-native-locked-output-false", HostWriteFormat::Png, false),
        lockedExportCase("jpeg-native-locked-output-end", HostWriteFormat::Jpeg, true),
        lockedExportCase("jpeg-native-locked-output-false", HostWriteFormat::Jpeg, false),
        nativeAssetCase("save-native-asset-collision-end", false, true),
        nativeAssetCase("save-native-asset-collision-false", false, false),
        nativeAssetCase("folder-native-asset-collision-end", true, true),
        nativeAssetCase("folder-native-asset-collision-false", true, false),
    };
    std::error_code error;
    // Keep headroom for both GUID siblings and the staged asset name on hosts with legacy Win32 path limits.
    const auto root = std::filesystem::temp_directory_path(error) / (L"SE-" + Guid::Generate().ToString());
    if (error || !std::filesystem::create_directory(root, error) || error)
        return 5;
    const std::wstring previousSettings = _settingsPath;
    const auto previousDiscard = _forcedDiscard;
    bool retainFailedFixture = false;
    _settingsPath = (root / L"settings.json").wstring();
    _forcedDiscard = false;
    const auto restore = wil::scope_exit(
        [&]() noexcept
        {
            if (_projectSaveTestGate)
                (void)SetEvent(_projectSaveTestGate->release.get());
            if (_exportWorkTestGate)
                (void)SetEvent(_exportWorkTestGate->release.get());
            CancelHostContinuations();
            if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                std::_Exit(5);
            _projectSaveTestGate.reset();
            _exportWorkTestGate.reset();
            _endSessionWriteTestGate.reset();
            OnEndSession(false);
            _settingsPath = previousSettings;
            _forcedDiscard = previousDiscard;
            if (retainFailedFixture)
                std::fwprintf(stderr, L"session-end-fixture.retained root=%ls\n", root.c_str());
            else
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored); // exact Guid-owned test directory
            }
        });

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        const Scenario& scenario;
        ContinuationReceipt receipt;
        std::wstring output;
        Guid revision;
        JobTicket ticket{};
        HistoryWorkStamp stamp{};
        std::uint64_t reserved = 0;
        bool visited = false, heldBefore = false, reason = false, critical = false;
        bool queryDidNotCancel = false, originalHeldAfter = false, cancelAfter = false;
        bool waitAcknowledged = false, noOwnerApply = false, helperJoined = false;
        bool earlierQueryHeld = true;
        bool helperObserved = false, helperReleased = false;
        unsigned int waitVisits = 0, phaseAfterWait = 0;
        DWORD waitResult = WAIT_FAILED, remaining = 0;
    };
#pragma warning(pop)

    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<State*>(raw);
        auto& owner = state.owner;
        const auto& scenario = state.scenario;
        auto& message = *static_cast<MSG*>(native);
        const UINT expected = scenario.format == HostWriteFormat::Project ? ProjectSaveTestGate::kMessage
                              : scenario.readback                         ? ExportWorkTestGate::kReadbackMessage
                                                                          : ExportWorkTestGate::kMessage;
        if (message.message != expected)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        const bool project = scenario.format == HostWriteFormat::Project;
        const auto write = project ? owner._projectSaveWrite : owner._exportWrite;
        const auto& ticket = project ? owner._projectSaveTicket : owner._exportTicket;
        const auto& stamp = project ? owner._projectSaveStamp : owner._exportStamp;
        const HANDLE release =
            project ? owner._projectSaveTestGate->release.get() : owner._exportWorkTestGate->release.get();
        state.ticket = ticket.value_or(JobTicket{});
        state.stamp = stamp.value_or(HistoryWorkStamp{});
        state.reserved = owner._jobs.ReservedMemoryBytes();
        state.heldBefore = write && ticket && stamp && owner._activeHostRequest &&
                           state.receipt.Read().outcome == HostOutcome::Pending && state.reserved != 0 &&
                           owner._historyWork.IsCurrent(state.stamp) &&
                           write->Current() == (scenario.readback ? WorkerWriteCompletion::Phase::Readback
                                                                  : WorkerWriteCompletion::Phase::Writing);
        const LRESULT query = SendMessageW(owner._window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
        std::array<wchar_t, 512> reason{};
        DWORD reasonLength = static_cast<DWORD>(reason.size());
        const bool hasReason = ShutdownBlockReasonQuery(owner._window.get(), reason.data(), &reasonLength) != FALSE;
        const std::wstring expectedReason =
            scenario.readback ? FormatStringResource(nullptr, IDS_SHUTDOWN_UNSAVED, FileTitle(owner._documentPath))
                              : FormatStringResource(nullptr, IDS_SHUTDOWN_SAVING, FileTitle(state.output));
        state.reason = scenario.savedReadback ? query == TRUE && !hasReason && !owner._endSessionPending
                       : scenario.readback
                           ? query == FALSE && hasReason && reason.data() == expectedReason &&
                                 owner._endSessionPending && !owner._endSessionWriteBlocked
                           : query == FALSE && hasReason && reason.data() == expectedReason &&
                                 owner._endSessionWriteBlocked && owner._endSessionPending == scenario.pendingPrompt;
        state.queryDidNotCancel =
            !owner._historyWork.CancelRequested(state.stamp) && state.receipt.Read().outcome == HostOutcome::Pending;
        state.critical = SendMessageW(owner._window.get(), WM_QUERYENDSESSION, 0, ENDSESSION_CRITICAL) == TRUE;
        wil::unique_handle waitReturned;
        if (scenario.holdUntilNativeReturn)
        {
            waitReturned.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!waitReturned)
                std::_Exit(5);
        }
        ReleaseWriter helper{owner._endSessionWriteTestGate->waitEntered.get(), release, waitReturned.get()};
        wil::unique_handle helperThread;
        const bool expectsWait = scenario.ending && !scenario.readback;
        if (expectsWait && !scenario.timeout)
        {
            helperThread.reset(CreateThread(nullptr, 0, ReleaseWriterAfterWait, &helper, 0, nullptr));
            if (!helperThread)
                std::_Exit(5); // no unreleased borrowed producer survives this fixture
        }
        (void)SendMessageW(owner._window.get(), WM_ENDSESSION, scenario.ending ? TRUE : FALSE, ENDSESSION_LOGOFF);
        if (waitReturned && !SetEvent(waitReturned.get()))
            std::_Exit(5);
        if (helperThread)
        {
            if (WaitForSingleObject(helperThread.get(), 15000) != WAIT_OBJECT_0)
                std::_Exit(5);
            state.helperJoined = helper.observed.load() && helper.released.load();
        }
        else
            state.helperJoined = true;
        state.helperObserved = helper.observed.load();
        state.helperReleased = helper.released.load();
        state.waitVisits = owner._endSessionWriteTestGate->waitVisits.load();
        state.waitResult = owner._endSessionWriteTestGate->waitResult.load();
        state.remaining = owner._endSessionWriteTestGate->waits[0].remaining;
        state.phaseAfterWait =
            write ? static_cast<unsigned int>(write->Current()) : (std::numeric_limits<unsigned int>::max)();
        state.cancelAfter = owner._historyWork.CancelRequested(state.stamp);
        // The genuine policy may reach its deadline after publication but before worker acknowledgement.
        // This is a valid cancellation path, not a requirement for real filesystem work to finish in 1.5s.
        const bool expectedDeadline = scenario.timeout || scenario.holdUntilNativeReturn;
        const bool validResult = expectedDeadline
                                     ? state.waitResult == WAIT_TIMEOUT
                                     : state.waitResult == WAIT_TIMEOUT || state.waitResult == WAIT_OBJECT_0;
        state.waitAcknowledged = state.waitVisits == (expectsWait ? 1u : 0u) &&
                                 (!expectsWait || (validResult && state.remaining <= 1500)) &&
                                 (!expectsWait || state.waitResult == WAIT_TIMEOUT ||
                                  (write && write->Current() == WorkerWriteCompletion::Phase::Finished));
        state.originalHeldAfter = ticket && stamp && ticket.value() == state.ticket && stamp.value() == state.stamp &&
                                  state.receipt.Read().outcome == HostOutcome::Pending &&
                                  owner._jobs.ReservedMemoryBytes() == state.reserved && owner._activeHostRequest;
        state.noOwnerApply = owner._session.history.CurrentRevision() == state.revision &&
                             owner._session.history.IsModified() == !scenario.savedReadback &&
                             owner._documentPath == L"before-session-write.prism" && !owner._endSessionWriteBlocked &&
                             !owner._endSessionPending;
        (void)SetEvent(release); // timeout/readback/false cases release only after the window procedure returned
    };

    int failures = 0;
    for (const auto& scenario : scenarios)
    {
        const bool nativeRecovery = scenario.recovery != RecoveryMode::None;
        const bool project = scenario.format == HostWriteFormat::Project;
        const bool jpeg = scenario.format == HostWriteFormat::Jpeg;
        const auto output =
            root / (std::wstring(scenario.name, scenario.name + std::strlen(scenario.name)) + (project ? L".prism"
                                                                                               : jpeg  ? L".jpg"
                                                                                                       : L".png"));
        // Setup is a tiny harness operation. Expected final pixels are literals, independent from the writer.
        auto oldDocument = LiteralFileDocument(0xFF202020);
        const auto oldActive = oldDocument.layers.front().id;
        const auto oldProjectStatus =
            project ? SaveProject(output.c_str(), oldDocument, oldActive, !scenario.folder) : ProjectIoStatus::Invalid;
        const auto oldImageStatus = project ? ImageCodecStatus::Encode
                                    : jpeg ? EncodeJpegFile(oldDocument.layers.front().asset->image, {}, output.c_str())
                                           : EncodePngFile(oldDocument.layers.front().asset->image, 72, output.c_str());
        bool installedOld = project ? oldProjectStatus == ProjectIoStatus::Ok : oldImageStatus == ImageCodecStatus::Ok;
        std::vector<std::uint8_t> oldBytes;
        std::vector<SessionProjectEntry> oldTree;
        installedOld =
            installedOld && (scenario.folder ? ReadLiteralTree(output, oldTree) : ReadLiteralFile(output, oldBytes));
        if (!installedOld)
        {
            std::fprintf(stderr,
                         "session-end-fixture.setup case=%s project=%u folder=%u project-status=%u image-status=%u "
                         "root-units=%zu output-units=%zu\n",
                         scenario.name, project ? 1u : 0u, scenario.folder ? 1u : 0u,
                         static_cast<unsigned int>(oldProjectStatus), static_cast<unsigned int>(oldImageStatus),
                         root.native().size(), output.native().size());
            return 5;
        }
        wil::unique_hfile outputReader;
        if (scenario.lockedOutput)
        {
            outputReader.reset(CreateFileW(output.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!outputReader)
                return 5; // Exclusive fixture destination must be held through the actual encoder publication.
        }
        auto input = LiteralFileDocument(0xFF404040);
        const auto active = input.layers.front().id;
        _session.ReplaceDocument(std::move(input), active);
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.InvertPixels(); // exact 64 -> 191 in B, G and R; alpha stays 255
        if (scenario.savedReadback)
            _session.history.MarkSaved();
        const auto revision = _session.history.CurrentRevision();
        const auto undoCount = _session.history.UndoCount();
        const auto presents = _renderer.PresentCount();
        const auto notices = _errorsShown;
        _documentPath = L"before-session-write.prism";
        _projectSaveTestGate = project ? std::make_shared<ProjectSaveTestGate>() : nullptr;
        _exportWorkTestGate = !project ? std::make_shared<ExportWorkTestGate>() : nullptr;
        _endSessionWriteTestGate = std::make_shared<EndSessionWriteTestGate>();
        if (!_endSessionWriteTestGate->waitEntered ||
            (project ? !_projectSaveTestGate->release : !_exportWorkTestGate->release))
            return 5;
        if (project)
        {
            // The two explicit deadline cases hold the real selected phase through native wait return.
            _projectSaveTestGate->afterPublication = scenario.afterPublication;
            _projectSaveTestGate->traceOnly = scenario.timeoutFault;
            _projectSaveTestGate->holdAtFaultForTest = scenario.timeoutFault;
            _projectSaveTestGate->createAssetCollisionForTest = scenario.nativeAssetError;
            if (scenario.nativeAssetError)
            {
                auto& trace = *_projectSaveTestGate;
                trace.assetLeafForTest = _session.Document()->layers.front().id.ToString() + L".png";
                // Prefix for the complete future directory conflict, not just its shorter search pattern.
                // All allocation remains in fixture setup; the worker's actual filesystem seam stays bounded.
                const auto tail = L".~00000000-0000-4000-8000-000000000000\\images\\" + trace.assetLeafForTest;
                const auto candidate = output.wstring() + tail;
                std::wstring prepared;
                if (FAILED(Win32FilePath::PrefixResolvedPath(candidate.c_str(), Win32FilePath::Use::Directory,
                                                             prepared)) ||
                    prepared.size() < tail.size())
                    return 5;
                prepared.resize(prepared.size() - tail.size());
                trace.assetStagingPatternForTest = prepared + L".~*";
                trace.assetStagingParentForTest = std::filesystem::path(prepared).parent_path().wstring() + L"\\";
            }
            _projectSaveTestGate->faultStageForTest = scenario.fault;
            _projectSaveTestGate->faultAtVisitForTest = scenario.publishedCleanup ? 2u : 1u;
            _projectSaveTestGate->precedingFaultStageForTest = (scenario.failedRollback || nativeRecovery)
                                                                   ? ProjectSaveFaultStage::StagingToDestinationRename
                                                                   : ProjectSaveFaultStage::Count;
            _projectSaveTestGate->holdRecoveryReaderForTest = nativeRecovery;
            if (nativeRecovery)
                _projectSaveTestGate->recoveryManifestForTest = output.wstring() + L".old\\manifest.json";
        }
        else
        {
            _exportWorkTestGate->afterPublication = scenario.afterPublication;
            _exportWorkTestGate->holdReadback = scenario.readback;
        }
        DocumentChanged();
        bool earlierQueryHeld = true;
        if (scenario.pendingPrompt)
        {
            // This is an actual earlier query, with its posted prompt held until this session-end boundary.
            // An active writer must report saving while preserving the earlier prompt's generation.
            MSG earlierPrompt{};
            const auto generation = _endSessionGeneration;
            earlierQueryHeld = SendMessageW(_window.get(), WM_QUERYENDSESSION, 0, 0) == FALSE && _endSessionPending &&
                               _endSessionGeneration == generation + 1 &&
                               PeekMessageW(&earlierPrompt, _window.get(), kEndSessionMessage, kEndSessionMessage,
                                            PM_REMOVE) != FALSE &&
                               earlierPrompt.wParam == static_cast<WPARAM>(_endSessionGeneration);
        }
        State state{*this, scenario, QueueHostInput(HostWriteInput{scenario.format, false, output.wstring()}),
                    output.wstring(), revision};
        state.earlierQueryHeld = earlierQueryHeld;
        if (SettleJobs(_jobs, 25000, dispatch, &state) != SettleResult::Settled)
            std::_Exit(5);
        const auto expect = [&](bool condition, const char* name) noexcept
        {
            std::printf("%s file-end-session.%s.%s\n", condition ? "PASS" : "FAIL", scenario.name, name);
            if (!condition)
            {
                ++failures;
                retainFailedFixture = true;
            }
        };
        if (!state.helperJoined || !state.waitAcknowledged ||
            state.cancelAfter != (scenario.timeout || scenario.readback))
            std::printf("DIAG file-end-session case=%s helperObserved=%u helperReleased=%u joined=%u visits=%u "
                        "waitResult=%lu remaining=%lu phase=%u cancel=%u receipt=%u error=%08X\n",
                        scenario.name, state.helperObserved ? 1u : 0u, state.helperReleased ? 1u : 0u,
                        state.helperJoined ? 1u : 0u, state.waitVisits, state.waitResult, state.remaining,
                        state.phaseAfterWait, state.cancelAfter ? 1u : 0u,
                        static_cast<unsigned int>(state.receipt.Read().outcome),
                        static_cast<unsigned int>(state.receipt.Read().error));
        const bool nativeTimeout = scenario.ending && !scenario.readback && state.waitResult == WAIT_TIMEOUT;
        const bool cancelled = nativeTimeout || scenario.readback;
        unsigned int publicationCount = 0;
        bool traceValid = true;
        if (project)
        {
            const auto& trace = *_projectSaveTestGate;
            traceValid = !trace.traceTruncated;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
                publicationCount += trace.observations[index].stage == ProjectIoStage::Published ? 1u : 0u;
        }
        else
            publicationCount = _exportWorkTestGate->publishedForTest ? 1u : 0u;
        const bool workerPublished = publicationCount == 1;
        expect(traceValid && publicationCount <= 1,
               "publication-truth-is-read-only-after-original-worker-acknowledgement");
        if (scenario.holdUntilNativeReturn)
            expect(nativeTimeout && state.helperObserved && state.helperReleased && state.helperJoined &&
                       state.cancelAfter && workerPublished == scenario.afterPublication &&
                       state.receipt.Read().outcome == HostOutcome::Cancelled,
                   "actual-deadline-cancels-original-receipt-and-preserves-selected-publication-truth");
        const bool fault = (scenario.fault != ProjectSaveFaultStage::Count && !scenario.publishedCleanup) ||
                           nativeRecovery || scenario.lockedOutput || scenario.nativeAssetError;
        const auto failureNotice = static_cast<unsigned int>(project ? IDS_ERROR_SAVE_PROJECT
                                                             : jpeg  ? IDS_ERROR_EXPORT_JPEG
                                                                     : IDS_ERROR_EXPORT_PNG);
        expect(state.visited && state.heldBefore, "original-writer-graph-is-held-at-actual-phase");
        expect(state.earlierQueryHeld && state.reason && state.critical && state.queryDidNotCancel,
               "queries-obey-stage-policy-with-localized-reason");
        expect(state.helperJoined && state.waitAcknowledged, "confirmed-end-waits-only-for-worker-event-or-timeout");
        expect(state.originalHeldAfter && state.noOwnerApply,
               "event-confers-no-receipt-path-or-saved-revision-authority");
        expect(state.cancelAfter == cancelled, "only-timeout-or-readback-cancels-original-history-stamp");
        expect(state.receipt.Read().outcome == (cancelled ? HostOutcome::Cancelled
                                                : fault   ? HostOutcome::Failed
                                                          : HostOutcome::Succeeded),
               "original-receipt-finishes-after-ordinary-acknowledgement");
        bool outputCorrect = false;
        if (scenario.failedRollback || nativeRecovery)
        {
            const std::filesystem::path backup{output.wstring() + L".old"};
            std::vector<SessionProjectEntry> retained;
            outputCorrect = ReadLiteralTree(backup, retained) && retained == oldTree;
        }
        else if (!workerPublished)
        {
            std::vector<std::uint8_t> actualBytes;
            std::vector<SessionProjectEntry> actualTree;
            outputCorrect = scenario.folder ? ReadLiteralTree(output, actualTree) && actualTree == oldTree
                                            : ReadLiteralFile(output, actualBytes) && actualBytes == oldBytes;
        }
        else if (project)
        {
            const auto reloaded = LoadProject(output.c_str());
            outputCorrect = reloaded.status == ProjectIoStatus::Ok && reloaded.document.layers.size() == 1 &&
                            reloaded.document.layers.front().asset &&
                            HasLiteralPixel(reloaded.document.layers.front().asset->image, 191);
        }
        else
        {
            DecodedImage decoded;
            outputCorrect = ImportImageFile(output.c_str(), 2, decoded) == ImageCodecStatus::Ok &&
                            HasLiteralPixel(decoded.image, 191, jpeg ? 3 : 0);
        }
        expect(outputCorrect, "publication-is-literal-or-old-file-is-byte-exact");
        expect(_session.history.CurrentRevision() == revision && _session.history.UndoCount() == undoCount &&
                   _session.history.IsModified() == (!scenario.savedReadback && (!project || !workerPublished)) &&
                   _documentPath == (project && workerPublished ? output.wstring() : L"before-session-write.prism"),
               "only-current-save-completion-marks-its-published-revision");
        expect(!_projectSaveTicket && !_projectSaveStamp && !_projectSaveWrite && !_exportTicket && !_exportStamp &&
                   !_exportWrite && !_historyWork.Busy() && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                   _hostInputBudget.Used() == 0 && _gpuWork.LentCount() == 0 && !_gpuWork.HasPending() &&
                   _composition.UtilitySurfaceBytes() == 0 && _renderer.PresentCount() == presents &&
                   _errorsShown == notices + (fault && !cancelled ? 1 : 0) &&
                   (!fault || cancelled || _lastErrorId == failureNotice),
               fault && !cancelled ? "tickets-events-budgets-and-utility-owners-retire-with-one-failure-notice"
                                   : "tickets-events-budgets-and-utility-owners-retire-without-present-or-notice");
        if (fault && project && !scenario.failedRollback && !nativeRecovery && !scenario.timeoutFault &&
            !scenario.nativeAssetError)
        {
            const auto& trace = *_projectSaveTestGate;
            const auto index = static_cast<std::size_t>(scenario.fault);
            const auto rollback = static_cast<std::size_t>(ProjectSaveFaultStage::RollbackRename);
            bool correctShape = true;
            if (scenario.folder)
            {
                correctShape = trace.faultVisits[rollback] ==
                               (scenario.fault == ProjectSaveFaultStage::StagingToDestinationRename ? 1u : 0u);
                for (const auto zipStage : {ProjectSaveFaultStage::ArchiveCommit, ProjectSaveFaultStage::ArchiveFlush,
                                            ProjectSaveFaultStage::ArchiveClose, ProjectSaveFaultStage::ArchiveReplace})
                    correctShape = correctShape && trace.faultVisits[static_cast<std::size_t>(zipStage)] == 0;
            }
            expect(trace.saveStatus == ProjectIoStatus::Encode && index < trace.faultVisits.size() &&
                       trace.faultVisits[index] == 1 && !trace.faultTraceTruncated && correctShape &&
                       state.receipt.Read().error == static_cast<std::int32_t>(E_FAIL),
                   "original-writer-fault-once-and-ordinary-failure-acknowledgement");
            const ProjectIoStage expectedStage =
                scenario.fault == ProjectSaveFaultStage::ManifestFlush ||
                        scenario.fault == ProjectSaveFaultStage::ArchiveFlush
                    ? ProjectIoStage::FlushFailed
                : scenario.fault == ProjectSaveFaultStage::ArchiveReplace ||
                        scenario.fault == ProjectSaveFaultStage::StagingToDestinationRename
                    ? ProjectIoStage::StagingToDestinationFailed
                : scenario.fault == ProjectSaveFaultStage::DestinationToBackupRename
                    ? ProjectIoStage::DestinationToBackupFailed
                    : ProjectIoStage::CloseFailed;
            unsigned int exactErrors = 0, publications = 0, staged = 0;
            std::size_t errorIndex = trace.observationCount, stagedIndex = trace.observationCount;
            for (std::size_t i = 0; i < trace.observationCount; ++i)
            {
                const auto& observed = trace.observations[i];
                if (observed.stage == expectedStage && observed.value == ERROR_WRITE_FAULT)
                {
                    ++exactErrors;
                    errorIndex = i;
                }
                publications += observed.stage == ProjectIoStage::Published ? 1u : 0u;
                if (observed.stage == ProjectIoStage::Staged)
                {
                    ++staged;
                    stagedIndex = i;
                }
            }
            const bool replacement = scenario.fault == ProjectSaveFaultStage::ArchiveReplace ||
                                     scenario.fault == ProjectSaveFaultStage::DestinationToBackupRename ||
                                     scenario.fault == ProjectSaveFaultStage::StagingToDestinationRename;
            expect(exactErrors == 1 && publications == 0 && !trace.traceTruncated &&
                       staged == (replacement ? 1u : 0u) && (!replacement || stagedIndex < errorIndex),
                   "exact-io-error-and-unpublished-durability-boundary");
            const auto reloaded = LoadProject(output.c_str());
            expect(reloaded.status == ProjectIoStatus::Ok && reloaded.activeLayerID == oldActive &&
                       reloaded.document.id == oldDocument.id && reloaded.document.layers.size() == 1 &&
                       reloaded.document.layers.front().id == oldActive && reloaded.document.layers.front().asset &&
                       HasLiteralPixel(reloaded.document.layers.front().asset->image, 32),
                   "prepublication-fault-keeps-old-literal-project");
            if (scenario.folder)
                expect(outputCorrect && HasNoOwnedSaveSiblings(output),
                       "folder-shape-retains-old-tree-and-cleans-owned-siblings");
        }
        if (scenario.publishedCleanup)
        {
            const auto& trace = *_projectSaveTestGate;
            expect(trace.saveStatus == ProjectIoStatus::Ok && !trace.faultTraceTruncated &&
                       trace.faultVisits[static_cast<std::size_t>(ProjectSaveFaultStage::BackupCleanup)] == 2 &&
                       state.receipt.Read().outcome == HostOutcome::Succeeded && state.receipt.Read().error == S_OK,
                   "postpublication-cleanup-fault-keeps-succeeded-original-receipt");
            unsigned int publications = 0, cleanupErrors = 0, staged = 0;
            std::size_t stagedAt = trace.observationCount, publishedAt = trace.observationCount,
                        cleanupAt = trace.observationCount;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
            {
                const auto& observed = trace.observations[index];
                if (observed.stage == ProjectIoStage::Staged)
                {
                    ++staged;
                    stagedAt = index;
                }
                if (observed.stage == ProjectIoStage::Published)
                {
                    ++publications;
                    publishedAt = index;
                }
                if (observed.stage == ProjectIoStage::BackupCleanupFailed && observed.value == ERROR_WRITE_FAULT)
                {
                    ++cleanupErrors;
                    cleanupAt = index;
                }
            }
            expect(!trace.traceTruncated && staged == 1 && publications == 1 && cleanupErrors == 1 &&
                       stagedAt < publishedAt && publishedAt < cleanupAt,
                   "published-fact-precedes-exact-cleanup-error-once");
            const std::filesystem::path backup{output.wstring() + L".old"};
            std::vector<SessionProjectEntry> retained;
            const auto recovered = LoadProject(backup.c_str());
            expect(ReadLiteralTree(backup, retained) && retained == oldTree &&
                       recovered.status == ProjectIoStatus::Ok && recovered.document.id == oldDocument.id &&
                       recovered.activeLayerID == oldActive && recovered.document.layers.size() == 1 &&
                       recovered.document.layers.front().id == oldActive && recovered.document.layers.front().asset &&
                       HasLiteralPixel(recovered.document.layers.front().asset->image, 32),
                   "cleanup-fault-retains-exact-reloadable-old-backup");
            const auto published = LoadProject(output.c_str());
            expect(outputCorrect && published.status == ProjectIoStatus::Ok &&
                       published.document.id == _session.Document()->id && published.activeLayerID == active &&
                       published.document.layers.size() == 1 && published.document.layers.front().id == active &&
                       published.document.layers.front().asset &&
                       HasLiteralPixel(published.document.layers.front().asset->image, 191),
                   "cleanup-fault-keeps-literal-new-published-project");
            expect(HasNoOwnedSaveSiblings(output, true) && _errorsShown == notices &&
                       _documentPath == output.wstring() && !_session.history.IsModified(),
                   "only-owned-backup-remains-with-saved-authority-and-no-failure-notice");
        }
        if (scenario.failedRollback)
        {
            const auto& trace = *_projectSaveTestGate;
            const auto visits = [&](ProjectSaveFaultStage stage) noexcept
            { return trace.faultVisits[static_cast<std::size_t>(stage)]; };
            expect(trace.saveStatus == ProjectIoStatus::Encode && !trace.faultTraceTruncated &&
                       visits(ProjectSaveFaultStage::DestinationToBackupRename) == 1 &&
                       visits(ProjectSaveFaultStage::StagingToDestinationRename) == 1 &&
                       visits(ProjectSaveFaultStage::RollbackRename) == 1 &&
                       state.receipt.Read().outcome == HostOutcome::Failed && state.receipt.Read().error == E_FAIL,
                   "original-publication-and-rollback-fail-once-with-failed-receipt");
            unsigned int staged = 0, publicationErrors = 0, rollbackErrors = 0, publications = 0, retries = 0;
            std::size_t stagedAt = trace.observationCount, publicationAt = trace.observationCount,
                        rollbackAt = trace.observationCount;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
            {
                const auto& observed = trace.observations[index];
                if (observed.stage == ProjectIoStage::Staged)
                {
                    ++staged;
                    stagedAt = index;
                }
                if (observed.stage == ProjectIoStage::StagingToDestinationFailed && observed.value == ERROR_WRITE_FAULT)
                {
                    ++publicationErrors;
                    publicationAt = index;
                }
                if (observed.stage == ProjectIoStage::RollbackFailed && observed.value == ERROR_WRITE_FAULT)
                {
                    ++rollbackErrors;
                    rollbackAt = index;
                }
                publications += observed.stage == ProjectIoStage::Published ? 1u : 0u;
                retries += observed.stage == ProjectIoStage::RollbackRetryScheduled ? 1u : 0u;
            }
            expect(!trace.traceTruncated && staged == 1 && publicationErrors == 1 && rollbackErrors == 1 &&
                       publications == 0 && stagedAt < publicationAt && publicationAt < rollbackAt,
                   "staged-then-exact-publication-and-rollback-errors-without-publication");
            const std::filesystem::path backup{output.wstring() + L".old"};
            const auto recovered = LoadProject(backup.c_str());
            expect(outputCorrect && recovered.status == ProjectIoStatus::Ok &&
                       recovered.document.id == oldDocument.id && recovered.activeLayerID == oldActive &&
                       recovered.document.layers.size() == 1 && recovered.document.layers.front().id == oldActive &&
                       recovered.document.layers.front().asset &&
                       HasLiteralPixel(recovered.document.layers.front().asset->image, 32),
                   "failed-rollback-keeps-byte-exact-reloadable-old-backup");
            const auto attributes = GetFileAttributesW(output.c_str());
            const auto absentError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
            expect(attributes == INVALID_FILE_ATTRIBUTES &&
                       (absentError == ERROR_FILE_NOT_FOUND || absentError == ERROR_PATH_NOT_FOUND) &&
                       HasNoOwnedSaveSiblings(output, true),
                   "destination-is-truthfully-absent-and-owned-staging-is-retired");
            expect(retries == 0 && visits(ProjectSaveFaultStage::RecoveryTimerCreate) == 0 &&
                       visits(ProjectSaveFaultStage::RecoveryTimerArm) == 0 &&
                       visits(ProjectSaveFaultStage::RecoveryTimerWait) == 0,
                   "injected-final-rollback-error-never-starts-recovery-timers");
            expect(_session.ActiveLayerID() == active && _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                       HasLiteralPixel(_session.ActiveLayer()->asset->image, 191) &&
                       _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undoCount &&
                       _session.history.IsModified() && _documentPath == L"before-session-write.prism" &&
                       _errorsShown == notices + 1 && _lastErrorId == IDS_ERROR_SAVE_PROJECT,
                   "failed-rollback-preserves-source-history-path-and-one-notice-authority");
        }
        if (nativeRecovery)
        {
            const auto& trace = *_projectSaveTestGate;
            const std::filesystem::path backup{output.wstring() + L".old"};
            wil::unique_handle deleteAccess{CreateFileW(trace.recoveryManifestForTest.c_str(), DELETE,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
            const auto deleteAccessError = deleteAccess ? ERROR_SUCCESS : GetLastError();
            const bool deleteAccessAfterAcknowledgement = static_cast<bool>(deleteAccess);
            deleteAccess.reset(); // Do not leave DELETE access open across the independent backup reload.
            if (!trace.recoveryDeleteAccessDeniedWhileHeldForTest || !deleteAccessAfterAcknowledgement)
                std::fprintf(stderr,
                             "session-end-fixture.reader-probe case=%s held-denied=%u held-error=%u "
                             "postack-opened=%u postack-error=%lu manifest-units=%zu\n",
                             scenario.name, trace.recoveryDeleteAccessDeniedWhileHeldForTest ? 1u : 0u,
                             trace.recoveryDeleteAccessErrorWhileHeldForTest,
                             deleteAccessAfterAcknowledgement ? 1u : 0u, deleteAccessError,
                             trace.recoveryManifestForTest.size());
            expect(trace.recoveryDeleteAccessDeniedWhileHeldForTest && deleteAccessAfterAcknowledgement,
                   "actual-reader-retirement-permits-postack-delete-access-without-mutating-backup");
            const auto visits = [&](ProjectSaveFaultStage stage) noexcept
            { return trace.faultVisits[static_cast<std::size_t>(stage)]; };
            const unsigned int retriesExpected = scenario.recovery == RecoveryMode::Exhaustion  ? 2u
                                                 : scenario.recovery == RecoveryMode::TimerWait ? 1u
                                                                                                : 0u;
            const unsigned int armsExpected = scenario.recovery == RecoveryMode::TimerCreate  ? 0u
                                              : scenario.recovery == RecoveryMode::Exhaustion ? 2u
                                                                                              : 1u;
            const unsigned int waitsExpected =
                scenario.recovery == RecoveryMode::TimerCreate || scenario.recovery == RecoveryMode::TimerArm ? 0u
                : scenario.recovery == RecoveryMode::Exhaustion                                               ? 2u
                                                                                                              : 1u;
            expect(trace.recoveryReaderOpenedForTest && trace.recoveryReaderErrorForTest == ERROR_SUCCESS &&
                       trace.recoveryDeleteAccessDeniedWhileHeldForTest &&
                       trace.recoveryDeleteAccessErrorWhileHeldForTest == ERROR_SHARING_VIOLATION &&
                       trace.recoveryReaderRetiredForTest && !trace.faultTraceTruncated &&
                       visits(ProjectSaveFaultStage::DestinationToBackupRename) == 1 &&
                       visits(ProjectSaveFaultStage::StagingToDestinationRename) == 1 &&
                       visits(ProjectSaveFaultStage::RollbackRename) == 1,
                   "real-backup-child-reader-blocks-native-rollback-and-retires-before-ack");
            expect(visits(ProjectSaveFaultStage::RecoveryTimerCreate) == 1 &&
                       visits(ProjectSaveFaultStage::RecoveryTimerArm) == armsExpected &&
                       visits(ProjectSaveFaultStage::RecoveryTimerWait) == waitsExpected,
                   "recovery-timer-visits-match-exhaustion-or-selected-final-refusal");
            unsigned int staged = 0, publicationErrors = 0, rollbackErrors = 0, publications = 0, retries = 0;
            unsigned int nativeError = 0;
            std::size_t stagedAt = trace.observationCount, publicationAt = trace.observationCount,
                        rollbackAt = trace.observationCount;
            bool retryErrorsMatch = true;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
            {
                const auto& observed = trace.observations[index];
                if (observed.stage == ProjectIoStage::Staged)
                {
                    ++staged;
                    stagedAt = index;
                }
                if (observed.stage == ProjectIoStage::StagingToDestinationFailed && observed.value == ERROR_WRITE_FAULT)
                {
                    ++publicationErrors;
                    publicationAt = index;
                }
                if (observed.stage == ProjectIoStage::RollbackFailed)
                {
                    ++rollbackErrors;
                    rollbackAt = index;
                    nativeError = observed.value;
                }
                publications += observed.stage == ProjectIoStage::Published ? 1u : 0u;
                retries += observed.stage == ProjectIoStage::RollbackRetryScheduled ? 1u : 0u;
            }
            for (std::size_t index = 0; index < trace.observationCount; ++index)
                if (trace.observations[index].stage == ProjectIoStage::RollbackRetryScheduled)
                    retryErrorsMatch = retryErrorsMatch && trace.observations[index].value == nativeError &&
                                       index > publicationAt && index < rollbackAt;
            expect(!trace.traceTruncated && staged == 1 && publicationErrors == 1 && rollbackErrors == 1 &&
                       publications == 0 && stagedAt < publicationAt && publicationAt < rollbackAt &&
                       retries == retriesExpected && retryErrorsMatch &&
                       (nativeError == ERROR_ACCESS_DENIED || nativeError == ERROR_SHARING_VIOLATION) &&
                       nativeError != trace.faultErrorForTest,
                   "native-rename-error-survives-bounded-retries-and-timer-refusal-with-no-published-fact");
            expect(trace.saveStatus == ProjectIoStatus::Encode && state.receipt.Read().outcome == HostOutcome::Failed &&
                       state.receipt.Read().error == E_FAIL && _errorsShown == notices + 1 &&
                       _lastErrorId == IDS_ERROR_SAVE_PROJECT,
                   "native-recovery-failure-applies-one-failed-original-receipt-and-localized-notice");
            const auto recovered = LoadProject(backup.c_str());
            expect(outputCorrect && recovered.status == ProjectIoStatus::Ok &&
                       recovered.document.id == oldDocument.id && recovered.activeLayerID == oldActive &&
                       recovered.document.layers.size() == 1 && recovered.document.layers.front().id == oldActive &&
                       recovered.document.layers.front().asset &&
                       HasLiteralPixel(recovered.document.layers.front().asset->image, 32),
                   "native-recovery-failure-retains-byte-exact-reloadable-old-backup");
            expect(GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES &&
                       HasNoOwnedSaveSiblings(output, true),
                   "native-recovery-leaves-truthful-destination-absence-and-only-recovery-backup");
            expect(HasLiteralPixel(_session.Document()->layers.front().asset->image, 191) &&
                       _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undoCount &&
                       _session.history.IsModified() && _documentPath == L"before-session-write.prism",
                   "native-recovery-failure-preserves-dirty-source-path-and-history-authority");
        }
        if (scenario.timeoutFault)
        {
            const auto& trace = *_projectSaveTestGate;
            expect(trace.heldFaultResultForTest.status == JobStatus::Cancelled && trace.heldFaultCancelledForTest &&
                       trace.visits.load() == 1 && state.cancelAfter && state.waitAcknowledged && state.noOwnerApply,
                   "late-fault-gate-releases-only-after-native-timeout-and-records-cancellation");
            unsigned int errors = 0, publications = 0, staged = 0, rollbackErrors = 0;
            std::size_t stagedAt = trace.observationCount, errorAt = trace.observationCount;
            const auto expectedStage =
                scenario.folder ? ProjectIoStage::StagingToDestinationFailed : ProjectIoStage::CloseFailed;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
            {
                const auto& observed = trace.observations[index];
                if (observed.stage == expectedStage && observed.value == ERROR_WRITE_FAULT)
                {
                    ++errors;
                    errorAt = index;
                }
                if (observed.stage == ProjectIoStage::Staged)
                {
                    ++staged;
                    stagedAt = index;
                }
                publications += observed.stage == ProjectIoStage::Published ? 1u : 0u;
                rollbackErrors += observed.stage == ProjectIoStage::RollbackFailed ? 1u : 0u;
            }
            expect(trace.saveStatus == ProjectIoStatus::Encode && !trace.traceTruncated && !trace.faultTraceTruncated &&
                       trace.faultVisits[static_cast<std::size_t>(scenario.fault)] == 1 && errors == 1 &&
                       publications == 0 && rollbackErrors == 0 && staged == (scenario.folder ? 1u : 0u) &&
                       (!scenario.folder || stagedAt < errorAt),
                   "actual-posttimeout-io-failure-keeps-exact-unpublished-native-error-fact");
            const auto reloaded = LoadProject(output.c_str());
            expect(outputCorrect && reloaded.status == ProjectIoStatus::Ok && reloaded.document.id == oldDocument.id &&
                       reloaded.activeLayerID == oldActive && reloaded.document.layers.size() == 1 &&
                       reloaded.document.layers.front().id == oldActive && reloaded.document.layers.front().asset &&
                       HasLiteralPixel(reloaded.document.layers.front().asset->image, 32) &&
                       HasNoOwnedSaveSiblings(output),
                   "posttimeout-failure-preserves-exact-reloadable-old-project-and-retires-staging");
            expect(state.receipt.Read().outcome == HostOutcome::Cancelled && state.receipt.Read().error == E_ABORT &&
                       _errorsShown == notices && _session.history.CurrentRevision() == revision &&
                       _session.history.UndoCount() == undoCount && _session.history.IsModified() &&
                       HasLiteralPixel(_session.Document()->layers.front().asset->image, 191) &&
                       _documentPath == L"before-session-write.prism",
                   "cancelled-stamp-keeps-one-quiet-cancelled-receipt-and-no-late-save-authority");
            expect(state.originalHeldAfter && !_projectSaveTicket && !_projectSaveStamp && !_projectSaveWrite &&
                       !_historyWork.Busy() && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                       _hostInputBudget.Used() == 0 && _renderer.PresentCount() == presents,
                   "original-timeout-ticket-event-and-reservation-retire-only-after-late-worker-ack");
        }
        if (scenario.lockedOutput)
        {
            const auto& trace = *_exportWorkTestGate;
            expect(outputReader && trace.encodeAttempts.load() == 1 &&
                       trace.encodedStatusForTest == ImageCodecStatus::Encode && !trace.publishedForTest,
                   "real-destination-reader-refuses-original-encoder-publication-once");
            expect(state.receipt.Read().outcome == HostOutcome::Failed && state.receipt.Read().error == E_FAIL &&
                       _errorsShown == notices + 1 && _lastErrorId == failureNotice,
                   "actual-native-encode-failure-applies-one-original-receipt-and-format-notice");
            std::vector<std::uint8_t> retained;
            expect(ReadLiteralFile(output, retained) && retained == oldBytes && HasNoOwnedSaveSiblings(output),
                   "actual-native-encode-failure-preserves-exact-old-bytes-and-retires-owned-temporaries");
            outputReader.reset(); // The original OS reader has no authority over any application state.
            DecodedImage decoded;
            expect(ImportImageFile(output.c_str(), 2, decoded) == ImageCodecStatus::Ok &&
                       HasLiteralPixel(decoded.image, 32, jpeg ? 3 : 0),
                   "failed-native-publication-keeps-independent-reloadable-old-pixels");
            expect(HasLiteralPixel(_session.Document()->layers.front().asset->image, 191) &&
                       _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undoCount &&
                       _session.history.IsModified() && _documentPath == L"before-session-write.prism",
                   "actual-native-export-error-keeps-dirty-source-history-and-project-path");
            expect(state.originalHeldAfter && !_exportTicket && !_exportStamp && !_exportWrite &&
                       !_historyWork.Busy() && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                       _hostInputBudget.Used() == 0 && _gpuWork.LentCount() == 0 && !_gpuWork.HasPending() &&
                       _composition.UtilitySurfaceBytes() == 0 && _renderer.PresentCount() == presents,
                   "original-native-error-graph-retires-only-after-owner-ack-with-no-present");
        }
        if (scenario.nativeAssetError)
        {
            const auto& trace = *_projectSaveTestGate;
            std::printf("INFO file-end-session-native-collision case=%s matches=%u created=%u error=%u "
                        "parentLength=%zu patternLength=%zu status=%u\n",
                        scenario.name, trace.assetStagingMatchesForTest, trace.assetCollisionCreatedForTest ? 1u : 0u,
                        trace.assetCollisionErrorForTest, trace.assetStagingParentForTest.size(),
                        trace.assetStagingPatternForTest.size(), static_cast<unsigned int>(trace.saveStatus));
            expect(trace.assetStagingMatchesForTest == 1 && trace.assetCollisionCreatedForTest &&
                       trace.assetCollisionErrorForTest == ERROR_SUCCESS && !trace.faultTraceTruncated &&
                       trace.faultVisits[static_cast<std::size_t>(ProjectSaveFaultStage::ManifestClose)] == 1,
                   "one-owned-staging-asset-directory-conflict-is-created-without-synthesized-failure");
            unsigned int assets = 0, staged = 0, publications = 0;
            for (std::size_t index = 0; index < trace.observationCount; ++index)
            {
                assets += trace.observations[index].stage == ProjectIoStage::AssetEncoded ? 1u : 0u;
                staged += trace.observations[index].stage == ProjectIoStage::Staged ? 1u : 0u;
                publications += trace.observations[index].stage == ProjectIoStage::Published ? 1u : 0u;
            }
            expect(!trace.traceTruncated && trace.saveStatus == ProjectIoStatus::Encode && assets == 0 && staged == 0 &&
                       publications == 0,
                   "real-asset-encoder-failure-precedes-staging-and-publication-in-original-save-graph");
            const auto old = LoadProject(output.c_str());
            expect(outputCorrect && old.status == ProjectIoStatus::Ok && old.document.id == oldDocument.id &&
                       old.activeLayerID == oldActive && old.document.layers.size() == 1 &&
                       old.document.layers.front().id == oldActive && old.document.layers.front().asset &&
                       HasLiteralPixel(old.document.layers.front().asset->image, 32) && HasNoOwnedSaveSiblings(output),
                   "actual-native-asset-error-keeps-exact-old-project-and-cleans-only-owned-staging");
            expect(state.receipt.Read().outcome == HostOutcome::Failed && state.receipt.Read().error == E_FAIL &&
                       _errorsShown == notices + 1 && _lastErrorId == IDS_ERROR_SAVE_PROJECT &&
                       HasLiteralPixel(_session.Document()->layers.front().asset->image, 191) &&
                       _session.history.CurrentRevision() == revision && _session.history.UndoCount() == undoCount &&
                       _session.history.IsModified() && _documentPath == L"before-session-write.prism",
                   "actual-native-asset-error-applies-one-failed-receipt-notice-and-no-late-source-authority");
            expect(state.originalHeldAfter && !_projectSaveTicket && !_projectSaveStamp && !_projectSaveWrite &&
                       !_historyWork.Busy() && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                       _hostInputBudget.Used() == 0 && _renderer.PresentCount() == presents,
                   "original-native-asset-error-retains-ticket-event-and-reservation-through-acknowledgement");
        }
        _projectSaveTestGate.reset();
        _exportWorkTestGate.reset();
        _endSessionWriteTestGate.reset();
    }
    {
        // Deliberately synthetic policy records: ordinary host admission serializes project save and
        // export. These invalid, unregistered tickets test only the defensive overlapping-flag budget,
        // never the original writer graphs, their receipts or two admitted concurrent writers.
        const auto expect = [&](bool value, const char* label)
        {
            std::printf("%s session-end-shared-deadline-policy.%s\n", value ? "PASS" : "FAIL", label);
            failures += value ? 0 : 1;
        };
        const bool idle = !_projectSaveTicket && !_exportTicket && !_projectSaveWrite && !_exportWrite &&
                          !_projectSaveStamp && !_exportStamp && _jobs.IsIdle() && !_historyWork.Busy() &&
                          !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0;
        expect(idle, "fixture-has-no-admitted-original-writer");
        if (!idle)
            return 5;
        const auto save = std::make_shared<WorkerWriteCompletion>(WorkerWriteCompletion::Phase::Writing);
        const auto exportWrite = std::make_shared<WorkerWriteCompletion>(WorkerWriteCompletion::Phase::Queued);
        auto gate = std::make_shared<EndSessionWriteTestGate>();
        const bool created =
            SUCCEEDED(save->CreationError()) && SUCCEEDED(exportWrite->CreationError()) && gate->waitEntered;
        expect(created, "owned-policy-events-exist");
        if (!created)
            return 5;
        const auto revision = _session.history.CurrentRevision();
        const auto undoCount = _session.history.UndoCount();
        const auto modified = _session.history.IsModified();
        const auto path = _documentPath;
        const auto presents = _renderer.PresentCount();
        const auto notices = _errorsShown;
        const auto restorePolicyRecords = wil::scope_exit(
            [&]() noexcept
            {
                _projectSaveTicket.reset();
                _exportTicket.reset();
                _projectSaveWrite.reset();
                _exportWrite.reset();
                _endSessionWriteTestGate.reset();
                OnEndSession(false);
            });
        _projectSaveTicket = JobTicket{};
        _exportTicket = JobTicket{};
        _projectSaveWrite = save;
        _exportWrite = exportWrite;
        _endSessionWriteTestGate = gate;
        const JobTicket unregistered{};
        expect(!_jobs.Cancel(unregistered), "invalid-policy-ticket-is-not-a-scheduler-job");
        (void)SendMessageW(_window.get(), WM_ENDSESSION, FALSE, 0);
        expect(gate->waitVisits.load() == 0 && save->Current() == WorkerWriteCompletion::Phase::Writing &&
                   exportWrite->Current() == WorkerWriteCompletion::Phase::Queued,
               "false-session-end-does-not-wait-or-cancel-either-policy-record");
        (void)SendMessageW(_window.get(), WM_ENDSESSION, TRUE, 0);
        expect(gate->waitVisits.load() == 2 && !gate->traceTruncated && gate->waits[0].deadline != 0 &&
                   gate->waits[0].deadline == gate->waits[1].deadline,
               "true-session-end-forwards-one-absolute-deadline-to-both-records");
        expect(gate->waits[0].remaining <= 1500 && gate->waits[0].result == WAIT_TIMEOUT &&
                   gate->waits[1].remaining <= gate->waits[0].remaining && gate->waits[1].result == WAIT_TIMEOUT,
               "second-record-reuses-the-remaining-absolute-budget");
        expect(_projectSaveWrite == save && _exportWrite == exportWrite && _projectSaveTicket == unregistered &&
                   _exportTicket == unregistered && _jobs.IsIdle() && _jobs.ReservedMemoryBytes() == 0 &&
                   !_historyWork.Busy() && _session.history.CurrentRevision() == revision &&
                   _session.history.UndoCount() == undoCount && _session.history.IsModified() == modified &&
                   _documentPath == path && _renderer.PresentCount() == presents && _errorsShown == notices,
               "native-policy-waits-have-no-owner-publication-or-receipt-authority");
        save->Acknowledge();
        exportWrite->Acknowledge();
        WaitForEndSessionWriter(save, _projectSaveTicket, std::nullopt, gate->waits[0].deadline);
        WaitForEndSessionWriter(exportWrite, _exportTicket, std::nullopt, gate->waits[0].deadline);
        expect(save->Current() == WorkerWriteCompletion::Phase::Finished &&
                   exportWrite->Current() == WorkerWriteCompletion::Phase::Finished && gate->waitVisits.load() == 2 &&
                   WaitForSingleObject(save->Event(), 0) == WAIT_OBJECT_0 &&
                   WaitForSingleObject(exportWrite->Event(), 0) == WAIT_OBJECT_0,
               "finished-policy-records-skip-subsequent-native-waits");
    }
    return failures == 0 ? 0 : 5;
}

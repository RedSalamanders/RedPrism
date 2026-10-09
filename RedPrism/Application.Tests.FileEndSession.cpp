#include "Application.h"

#include "../Tests/Support/DocumentFixture.h"
#include "EndSessionWriteTestGate.h"
#include "ExportWorkTestGate.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectSaveTestGate.h"
#include "ProjectStore.h"
#include "Resources.h"
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
struct ReleaseWriter final
{
    HANDLE waitEntered = nullptr;
    HANDLE release = nullptr;
    std::atomic<bool> observed{false}, released{false};
};

DWORD WINAPI ReleaseWriterAfterWait(void* raw) noexcept
{
    auto& state = *static_cast<ReleaseWriter*>(raw);
    const DWORD result = WaitForSingleObject(state.waitEntered, 10000);
    state.observed.store(result == WAIT_OBJECT_0);
    state.released.store(SetEvent(state.release) != FALSE);
    return 0;
}

bool ReadLiteralFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    wil::unique_hfile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
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
    const auto rootAttributes = GetFileAttributesW(root.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    if (error)
        return false;
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
            return false;
        const auto& path = iterator->path();
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return false;
        SessionProjectEntry entry{
            path.lexically_relative(root).generic_wstring(), (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, {}};
        if (!entry.directory && !ReadLiteralFile(path, entry.bytes))
            return false;
        entries.push_back(std::move(entry));
    }
    if (error)
        return false;
    std::ranges::sort(entries, {}, &SessionProjectEntry::relative);
    return true;
}

bool HasNoOwnedSaveSiblings(const std::filesystem::path& destination)
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
        if (name.starts_with(stagingPrefix) || name == backupName)
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
    };
    constexpr auto faultCase = [](const char* name, ProjectSaveFaultStage stage, bool ending) constexpr noexcept
    { return Scenario{name, HostWriteFormat::Project, false, ending, false, false, false, false, stage}; };
    constexpr auto folderFaultCase = [](const char* name, ProjectSaveFaultStage stage, bool ending) constexpr noexcept
    { return Scenario{name, HostWriteFormat::Project, false, ending, false, false, false, false, stage, true}; };
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
    };
    std::error_code error;
    const auto root =
        std::filesystem::temp_directory_path(error) / (L"RedPrism.SessionEndWrites." + Guid::Generate().ToString());
    if (error || !std::filesystem::create_directory(root, error) || error)
        return 5;
    const std::wstring previousSettings = _settingsPath;
    const auto previousDiscard = _forcedDiscard;
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
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored); // exact Guid-owned test directory
        });

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
    };
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
        ReleaseWriter helper{owner._endSessionWriteTestGate->waitEntered.get(), release};
        wil::unique_handle helperThread;
        const bool expectsWait = scenario.ending && !scenario.readback;
        if (expectsWait && !scenario.timeout)
        {
            helperThread.reset(CreateThread(nullptr, 0, ReleaseWriterAfterWait, &helper, 0, nullptr));
            if (!helperThread)
                std::_Exit(5); // no unreleased borrowed producer survives this fixture
        }
        (void)SendMessageW(owner._window.get(), WM_ENDSESSION, scenario.ending ? TRUE : FALSE, ENDSESSION_LOGOFF);
        if (helperThread)
        {
            if (WaitForSingleObject(helperThread.get(), 15000) != WAIT_OBJECT_0)
                std::_Exit(5);
            state.helperJoined = helper.observed.load() && helper.released.load();
        }
        else
            state.helperJoined = true;
        state.cancelAfter = owner._historyWork.CancelRequested(state.stamp);
        state.waitAcknowledged =
            owner._endSessionWriteTestGate->waitVisits.load() == (expectsWait ? 1u : 0u) &&
            (expectsWait ? owner._endSessionWriteTestGate->waitResult.load() ==
                               (scenario.timeout ? WAIT_TIMEOUT : WAIT_OBJECT_0)
                         : true) &&
            (!expectsWait || scenario.timeout || (write && write->Current() == WorkerWriteCompletion::Phase::Finished));
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
        const bool project = scenario.format == HostWriteFormat::Project;
        const bool jpeg = scenario.format == HostWriteFormat::Jpeg;
        const auto output =
            root / (std::wstring(scenario.name, scenario.name + std::strlen(scenario.name)) + (project ? L".prism"
                                                                                               : jpeg  ? L".jpg"
                                                                                                       : L".png"));
        // Setup is a tiny harness operation. Expected final pixels are literals, independent from the writer.
        auto oldDocument = LiteralFileDocument(0xFF202020);
        const auto oldActive = oldDocument.layers.front().id;
        bool installedOld =
            project ? SaveProject(output.c_str(), oldDocument, oldActive, !scenario.folder) == ProjectIoStatus::Ok
                    : (jpeg ? EncodeJpegFile(oldDocument.layers.front().asset->image, {}, output.c_str())
                            : EncodePngFile(oldDocument.layers.front().asset->image, 72, output.c_str())) ==
                          ImageCodecStatus::Ok;
        std::vector<std::uint8_t> oldBytes;
        std::vector<SessionProjectEntry> oldTree;
        installedOld =
            installedOld && (scenario.folder ? ReadLiteralTree(output, oldTree) : ReadLiteralFile(output, oldBytes));
        if (!installedOld)
            return 5;
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
            _projectSaveTestGate->afterPublication = scenario.afterPublication;
            _projectSaveTestGate->faultStageForTest = scenario.fault;
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
                ++failures;
        };
        const bool cancelled = scenario.timeout || scenario.readback;
        const bool fault = scenario.fault != ProjectSaveFaultStage::Count;
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
        if (cancelled || fault)
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
                   _session.history.IsModified() == (!scenario.savedReadback && (!project || cancelled || fault)) &&
                   _documentPath == (project && !cancelled && !fault ? output.wstring() : L"before-session-write.prism"),
               "only-current-save-completion-marks-its-published-revision");
        expect(!_projectSaveTicket && !_projectSaveStamp && !_projectSaveWrite && !_exportTicket && !_exportStamp &&
                   !_exportWrite && !_historyWork.Busy() && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                   _hostInputBudget.Used() == 0 && _gpuWork.LentCount() == 0 && !_gpuWork.HasPending() &&
                   _composition.UtilitySurfaceBytes() == 0 && _renderer.PresentCount() == presents &&
                   _errorsShown == notices + (fault ? 1 : 0) && (!fault || _lastErrorId == IDS_ERROR_SAVE_PROJECT),
               fault ? "tickets-events-budgets-and-utility-owners-retire-with-one-failure-notice"
                     : "tickets-events-budgets-and-utility-owners-retire-without-present-or-notice");
        if (fault)
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
        _projectSaveTestGate.reset();
        _exportWorkTestGate.reset();
        _endSessionWriteTestGate.reset();
    }
    return failures == 0 ? 0 : 5;
}

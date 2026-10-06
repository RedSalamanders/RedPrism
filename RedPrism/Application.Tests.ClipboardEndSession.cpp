#include "Application.h"

#include "../Common/ClipboardReceipt.h"
#include "../Tests/Support/DocumentFixture.h"
#include "ChromeHost.h"
#include "ClipboardTransfer.h"
#include "ClipboardWorkTestGate.h"
#include "SystemClipboard.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace
{
constexpr std::array<std::uint8_t, 12> kSourcePixels{11, 22, 33, 255, 20, 40, 80, 255, 99, 100, 101, 255};
constexpr std::array<std::uint8_t, 4> kSelectedPixel{20, 40, 80, 255};
constexpr std::array<std::uint8_t, 4> kForeignPixel{17, 33, 65, 255};
constexpr UINT kClipboardPublishedMessage = ClipboardWorkTestGate::kPublicationMessage;

CanvasDocument MakeEndSessionClipboardFixture(Guid& activeLayer)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 9;
    document.height = 7;
    ImageLayer layer = ImageLayer::Blank(L"End-session clipboard source", Vec2{3, 1});
    layer.asset = RasterAsset{
        Raster::Make(3, 1, RasterFormat::Bgra8888, kSourcePixels.data(), kSourcePixels.size()), {}, layer.name};
    activeLayer = layer.id;
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

bool PrivateClipboardIsLiteral(const DocumentSession& session) noexcept
{
    const auto& clipboard = session.Clipboard();
    return clipboard && clipboard->origin == Vec2{1, 0} && clipboard->image.Width() == 1 &&
           clipboard->image.Height() == 1 && clipboard->image.HasStorage() &&
           std::memcmp(clipboard->image.Data(), kSelectedPixel.data(), kSelectedPixel.size()) == 0;
}

bool ActiveLayerHasSourcePixels(const DocumentSession& session) noexcept
{
    const auto* layer = session.ActiveLayer();
    return layer && layer->asset && layer->asset->image.Width() == 3 && layer->asset->image.Height() == 1 &&
           std::memcmp(layer->asset->image.Data(), kSourcePixels.data(), kSourcePixels.size()) == 0;
}

bool ActiveLayerHasCutPixel(const DocumentSession& session) noexcept
{
    const auto* layer = session.ActiveLayer();
    if (!layer || !layer->asset || layer->asset->image.Width() != 3 || layer->asset->image.Height() != 1)
        return false;
    std::array<std::uint8_t, 12> pixels{};
    layer->asset->image.ReadRegion(0, 0, 3, 1, pixels.data(), static_cast<int>(pixels.size()), 0);
    constexpr std::array<std::uint8_t, 12> expected{11, 22, 33, 255, 0, 0, 0, 0, 99, 100, 101, 255};
    return pixels == expected;
}

bool ReadOsClipboardPixel(HWND owner, const std::array<std::uint8_t, 4>& expected) noexcept
{
    Raster image;
    if (!SystemClipboard::ReadImage(owner, 9 * 7, image, SystemClipboard::kHarnessAttempts) || image.Width() != 1 ||
        image.Height() != 1 || !image.HasStorage())
        return false;
    return std::memcmp(image.Data(), expected.data(), expected.size()) == 0;
}

bool OsClipboardHasPrivateReceipt(HWND owner, const ClipboardFingerprint::Binding& binding,
                                  const ClipboardReceipt::Key& expectedKey) noexcept
{
    // One bounded harness acquisition owns all observed bytes; separate Inspect/Copy openings would allow
    // clipboard listeners to interpose. Production hashes its owned copies on a worker. This tiny literal
    // harness observation runs after retirement and asserts content, never owner cost.
    SystemClipboard::SavedContents bytes;
    if (!SystemClipboard::SaveContents(owner, bytes))
        return false;
    const UINT receiptFormat = RegisterClipboardFormatW(L"RedPrism.ClipboardReceipt.v1");
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (receiptFormat == 0 || pngFormat == 0)
        return false;
    std::optional<ClipboardReceipt::Key> receipt;
    ClipboardFingerprint::Format png, dib;
    for (const auto& [format, payload] : bytes.formats)
    {
        if (format == receiptFormat)
        {
            ClipboardReceipt::Key decoded;
            if (!ClipboardReceipt::Decode(payload, decoded))
                return false;
            receipt = decoded;
        }
        else if (format == pngFormat)
        {
            if (FAILED(ClipboardFingerprint::Hash(payload, png)))
                return false;
        }
        else if (format == CF_DIBV5)
        {
            if (FAILED(ClipboardFingerprint::Hash(payload, dib)))
                return false;
        }
    }
    return ClipboardFingerprint::Matches(binding, expectedKey, receipt, png, dib) && png.bytes != 0 && dib.bytes != 0;
}

bool OsClipboardHasNoReceipt(HWND owner) noexcept
{
    SystemClipboard::SavedContents observed;
    if (!SystemClipboard::SaveContents(owner, observed))
        return false;
    const UINT receiptFormat = RegisterClipboardFormatW(L"RedPrism.ClipboardReceipt.v1");
    return receiptFormat != 0 && std::ranges::none_of(observed.formats, [receiptFormat](const auto& format)
                                                      { return format.first == receiptFormat; });
}
} // namespace

// Real confirmed/cancelled session-end messages through the hidden application host.
int Application::RunClipboardEndSessionSelfTest() noexcept
{
    if (!_window)
        return 5;

    int failures = 0;
    unsigned int checks = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* predicate) noexcept
    {
        ++checks;
        std::printf("%s clipboard-end-session.%s.%s\n", condition ? "PASS" : "FAIL", scenario, predicate);
        failures += condition ? 0 : 1;
    };

    struct Scenario final
    {
        const char* name;
        int command;
        bool cut;
        bool afterEmpty;
        bool ending;
    };
    constexpr std::array scenarios{
        Scenario{"copy-before-empty", ChromeHost::kCommandCopy, false, false, true},
        Scenario{"cut-before-empty", ChromeHost::kCommandCut, true, false, true},
        Scenario{"copy-after-empty", ChromeHost::kCommandCopy, false, true, true},
        Scenario{"cut-after-empty", ChromeHost::kCommandCut, true, true, true},
        Scenario{"copy-cancelled-session", ChromeHost::kCommandCopy, false, false, false},
        Scenario{"cut-cancelled-session", ChromeHost::kCommandCut, true, false, false},
    };

    SystemClipboard::SavedContents savedClipboard;
    if (!SystemClipboard::SaveContents(_window.get(), savedClipboard))
    {
        std::printf("FAIL clipboard-end-session.save-user-clipboard error=%lu\n", GetLastError());
        return 5;
    }
    const std::wstring priorSettingsPath = _settingsPath;
    std::error_code filesystemError;
    const auto settingsScratch = std::filesystem::temp_directory_path(filesystemError) /
                                 (L"RedPrism.EndSessionClipboard." + Guid::Generate().ToString());
    if (filesystemError || !std::filesystem::create_directory(settingsScratch, filesystemError) || filesystemError)
        return 5;
    _settingsPath = (settingsScratch / L"settings.json").wstring();
    const auto restore = wil::scope_exit(
        [&]() noexcept
        {
            _clipboardWorkTestGate.reset();
            _settingsPath = priorSettingsPath;
            (void)SystemClipboard::RestoreContents(_window.get(), savedClipboard);
            std::error_code ignored;
            std::filesystem::remove_all(settingsScratch, ignored);
        });

    struct DispatchState final
    {
        Application& owner;
        const Scenario& scenario;
        std::shared_ptr<ClipboardWorkTestGate> gate;
        ContinuationReceipt receipt;
        bool phaseMessage = false;
        bool sessionMessage = false;
        bool heldBefore = false;
        bool heldAfter = false;
        bool cancelRequestedAfter = false;
        bool exactHeldIdentityAndReservation = false;
        JobTicket originalTicket{};
        HistoryWorkStamp originalStamp{};
        std::uint64_t reservationBytes = 0;
    };
    const auto dispatch = [](void* raw, void* native) noexcept
    {
        auto& state = *static_cast<DispatchState*>(raw);
        auto& message = *static_cast<MSG*>(native);
        auto& owner = state.owner;
        if (message.message == ClipboardWorkTestGate::kMessage || message.message == kClipboardPublishedMessage)
        {
            state.phaseMessage = true;
            state.originalTicket = owner._clipboardTicket.value_or(JobTicket{});
            state.originalStamp = owner._clipboardStamp.value_or(HistoryWorkStamp{});
            state.reservationBytes = owner._jobs.ReservedMemoryBytes();
            state.heldBefore = owner._clipboardTicket && owner._clipboardStamp && owner._historyWork.Busy() &&
                               state.receipt.Read().outcome == HostOutcome::Pending &&
                               state.reservationBytes >= state.gate->reservationBytes &&
                               state.gate->reservationBytes != 0;
            (void)SendMessageW(owner._window.get(), WM_ENDSESSION, state.scenario.ending ? TRUE : FALSE,
                               ENDSESSION_LOGOFF);
            state.sessionMessage = true;
            state.heldAfter = owner._clipboardTicket && owner._clipboardStamp && owner._historyWork.Busy() &&
                              state.receipt.Read().outcome == HostOutcome::Pending;
            state.cancelRequestedAfter = owner._historyWork.CancelRequested(state.originalStamp);
            state.exactHeldIdentityAndReservation = owner._clipboardTicket && owner._clipboardStamp &&
                                                    owner._clipboardTicket.value() == state.originalTicket &&
                                                    owner._clipboardStamp.value() == state.originalStamp &&
                                                    owner._jobs.ReservedMemoryBytes() == state.reservationBytes;
            // Release immediately after the actual window procedure returns; the owner never waits for this job.
            (void)SetEvent(state.gate->release.get());
            return;
        }
        owner.DispatchQueuedMessage(message);
    };

    for (const Scenario& scenario : scenarios)
    {
        const bool expectedPublication = scenario.afterEmpty || !scenario.ending;
        _clipboardWorkTestGate.reset();
        Guid activeLayer;
        auto document = MakeEndSessionClipboardFixture(activeLayer);
        _session.ReplaceDocument(std::move(document), activeLayer);
        _session.SetSelection(
            DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{2, 0}, Vec2{2, 1}, Vec2{1, 1}}}},
            L"End-session literal selection");
        _session.history.Reset();
        _session.history.MarkSaved();
        _clipboardBinding.reset();
        DocumentChanged();

        const Raster foreign = Raster::Make(1, 1, RasterFormat::Bgra8888, kForeignPixel.data(), kForeignPixel.size());
        if (!SystemClipboard::WaitUntilFree(_window.get(), SystemClipboard::kHarnessAttempts) ||
            !SystemClipboard::WriteImage(_window.get(), foreign, SystemClipboard::kHarnessAttempts))
        {
            std::printf("FAIL clipboard-end-session.%s.install-foreign-clipboard error=%lu\n", scenario.name,
                        GetLastError());
            return 5;
        }
        const std::uint32_t initialSequence = SystemClipboard::SequenceNumber();
        const std::uint64_t initialSerial = _session.ClipboardSerial();
        const Guid initialRevision = _session.history.CurrentRevision();
        const int initialUndoCount = _session.history.UndoCount();
        const bool initialRedo = _session.history.CanRedo();
        const bool initialModified = _session.history.IsModified();

        _clipboardWorkTestGate = std::make_shared<ClipboardWorkTestGate>();
        auto gate = _clipboardWorkTestGate;
        if (!gate->release)
            return 5;
        gate->phase = scenario.afterEmpty ? ClipboardWorkPhase::PasteProbe : ClipboardWorkPhase::CopyEncoded;
        // A false session end preserves the ordinary bounded retry before the irreversible boundary.
        // Force one Busy result so the post-Empty oracle cannot accidentally forbid that supported path.
        if (!scenario.cut && !scenario.ending)
            gate->openFailures.store(1);
        if (scenario.afterEmpty)
        {
            gate->publicationOwnerThread = GetCurrentThreadId();
            gate->publicationStage = ClipboardTransfer::Stage::Emptied;
        }

        DispatchState state{*this, scenario, gate, QueueHostInput(HostCommandInput{scenario.command})};
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
        {
            SelfTestFailure(L"Clipboard end-session work did not acknowledge its owned event gate.\n");
            std::_Exit(5); // keep user clipboard and borrowed owners intact until the worker has actually joined
        }

        const bool expectedPrivateState =
            PrivateClipboardIsLiteral(_session) &&
            (scenario.cut ? ActiveLayerHasCutPixel(_session) : ActiveLayerHasSourcePixels(_session));
        const bool expectedHistory = scenario.cut
                                         ? _session.history.UndoCount() == initialUndoCount + 1 &&
                                               _session.history.UndoName() == L"Clear" && _session.history.IsModified()
                                         : _session.history.CurrentRevision() == initialRevision &&
                                               _session.history.UndoCount() == initialUndoCount &&
                                               _session.history.CanRedo() == initialRedo &&
                                               _session.history.IsModified() == initialModified;
        const bool clipboardAvailable =
            SystemClipboard::WaitUntilFree(_window.get(), SystemClipboard::kHarnessAttempts);
        const bool osPixels = clipboardAvailable &&
                              ReadOsClipboardPixel(_window.get(), expectedPublication ? kSelectedPixel : kForeignPixel);
        bool osReceipt = false;
        if (expectedPublication)
        {
            const ClipboardReceipt::Key key{_session.ClipboardSessionIdentity().bytes, _session.ClipboardSerial()};
            const bool receiptClipboardAvailable =
                SystemClipboard::WaitUntilFree(_window.get(), SystemClipboard::kHarnessAttempts);
            osReceipt = receiptClipboardAvailable && _clipboardBinding.has_value() &&
                        OsClipboardHasPrivateReceipt(_window.get(), _clipboardBinding.value(), key);
        }
        else
            osReceipt = clipboardAvailable && OsClipboardHasNoReceipt(_window.get());

        expect(state.phaseMessage && state.heldBefore && state.sessionMessage && state.heldAfter, scenario.name,
               "actual-wndproc-message-while-original-job-pending");
        expect(state.exactHeldIdentityAndReservation, scenario.name,
               "original-ticket-stamp-and-reservation-retained-until-ack");
        expect(state.cancelRequestedAfter == scenario.ending, scenario.name,
               "true-cancels-and-false-preserves-active-clipboard-work");
        expect(state.receipt.Read().outcome == HostOutcome::Succeeded, scenario.name,
               "private-command-receipt-acknowledged");
        expect(expectedPrivateState && expectedHistory && _session.ClipboardSerial() == initialSerial + 1,
               scenario.name, "literal-private-pixels-clipboard-and-history");
        const bool publicationCounts =
            expectedPublication
                ? gate->emptyAttempts.load() == 1 && gate->publications.load() == 1 && gate->openedScopes.load() == 1 &&
                      gate->receiptPlacements.load() == 1 && gate->pngPlacements.load() == 1 &&
                      gate->dibPlacements.load() == 1 &&
                      gate->openAttempts.load() == gate->openAttemptsAtEmpty.load() &&
                      gate->openAttemptsAtEmpty.load() >= (!scenario.cut && !scenario.ending ? 2u : 1u) &&
                      gate->openAttemptsAtEmpty.load() <= 6 &&
                      gate->forcedBusyAttempts.load() == (!scenario.cut && !scenario.ending ? 1u : 0u) &&
                      _clipboardWriteSucceeded && _clipboardSequence != initialSequence &&
                      (scenario.afterEmpty ? gate->publicationVisits.load() == 1 && !gate->publicationGateFailed.load()
                                           : gate->publicationVisits.load() == 0)
                : gate->emptyAttempts.load() == 0 && gate->publications.load() == 0 &&
                      gate->publicationVisits.load() == 0 && gate->openAttempts.load() == 0 &&
                      gate->openedScopes.load() == 0 && gate->receiptPlacements.load() == 0 &&
                      gate->pngPlacements.load() == 0 && gate->dibPlacements.load() == 0 && !_clipboardWriteSucceeded &&
                      _clipboardSequence == initialSequence;
        if (!publicationCounts)
            std::printf("DIAG clipboard-end-session.%s empty=%u closed=%u opened=%u attempts=%u "
                        "at-empty=%u receipt=%u png=%u dib=%u forced-busy=%u success=%d "
                        "initial-sequence=%lu captured-sequence=%lu gate-visits=%u gate-failed=%d\n",
                        scenario.name, gate->emptyAttempts.load(), gate->publications.load(), gate->openedScopes.load(),
                        gate->openAttempts.load(), gate->openAttemptsAtEmpty.load(), gate->receiptPlacements.load(),
                        gate->pngPlacements.load(), gate->dibPlacements.load(), gate->forcedBusyAttempts.load(),
                        _clipboardWriteSucceeded ? 1 : 0, static_cast<unsigned long>(initialSequence),
                        static_cast<unsigned long>(_clipboardSequence), gate->publicationVisits.load(),
                        gate->publicationGateFailed.load() ? 1 : 0);
        expect(publicationCounts, scenario.name,
               expectedPublication ? "irreversible-formats-published-once-with-no-post-empty-retry"
                                   : "pre-empty-cancel-never-attempts-publication");
        expect(osPixels && osReceipt, scenario.name,
               expectedPublication ? "os-pixels-and-content-bound-receipt-match-private-copy"
                                   : "foreign-os-pixels-remain-and-no-private-receipt-escaped");

        const bool retired = !_clipboardTicket && !_clipboardStamp && !_historyWork.Busy() && !_activeHostRequest &&
                             _hostRequests.Size() == 0 && !_hostInputBudget.Used() &&
                             _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 &&
                             gate->retiredNodes.load() != 0 && gate->retiredOwnerNodes.load() == 0;
        expect(retired, scenario.name, "exact-terminal-retirement-after-owner-ack");
    }

    constexpr unsigned int kExpectedAssertions = 48; // six scenarios x eight independent numeric expectations
    if (checks != kExpectedAssertions)
    {
        std::printf("FAIL clipboard-end-session.summary.exact-independent-assertion-count actual=%u expected=%u\n",
                    checks, kExpectedAssertions);
        ++failures;
    }
    std::printf("clipboard-end-session summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}

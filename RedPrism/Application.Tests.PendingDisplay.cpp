#include "Application.h"
#include "ChromeHost.h"
#include "DocumentDraft.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
std::optional<CanvasDocument> PendingFixture()
{
    CanvasDocument document;
    document.width = 2;
    document.height = 2;
    auto draft = CanvasDocumentDraft::Rebuild(document, 1);
    if (!draft)
        return std::nullopt;
    constexpr std::array<std::uint8_t, 16> gray{128, 128, 128, 255, 128, 128, 128, 255,
                                                128, 128, 128, 255, 128, 128, 128, 255};
    auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, gray.data(), gray.size());
    if (!image.HasStorage())
        return std::nullopt;
    auto layer = ImageLayer::Blank(L"Pending display", Vec2{2, 2});
    layer.asset = {std::move(image), {}, L"Pending display"};
    layer.transform.sampling = LayerSampling::Nearest;
    if (!draft->Append(std::move(layer)))
        return std::nullopt;
    return draft->Seal();
}

// Exposure reference: legacy ImageAdjustmentTests.exposureWorksInLinearLightWithOffsetAndGamma
// gives +1 stop at sRGB 128 as 176. Independently decoding sRGB, scaling linear light by 1/2,
// encoding sRGB and rounding gives -1 stop as 92; alpha remains 255.
bool MatchesLiteral(RecordedDisplayGraph* graph, std::uint8_t expected)
{
    if (!graph || !graph->CompositionForTestOwner())
        return false;
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(graph->CompositionForTestOwner()->Readback(pixels, width, height)) || width != 2 || height != 2 ||
        pixels.size() != 16)
        return false;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        if (pixels[offset] != expected || pixels[offset + 1] != expected || pixels[offset + 2] != expected ||
            pixels[offset + 3] != 255)
        {
            std::printf("INFO application-pending-display.literal expected=%u actual=%u,%u,%u,%u\n", expected,
                        pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
            return false;
        }
    return true;
}
} // namespace

int Application::RunStandalonePendingDisplaySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        std::printf("%s application-pending-display.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    for (const bool live : {false, true})
    {
        for (const bool commit : {false, true})
        {
            std::unique_ptr<Application> owner{new (std::nothrow)
                                                   Application(instance, true, L"", 0, nullptr, nullptr)};
            if (!expect(owner != nullptr, "actual-host-allocated"))
                return 5;
            const auto close = wil::scope_exit(
                [&]() noexcept
                {
                    owner->Shutdown();
                    owner.reset();
                    while (PumpMessages().quit)
                    {
                    }
                });
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                            SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                            SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                        "actual-WARP-host-shares-one-recorder-pump"))
                return 5;
            auto fixture = PendingFixture();
            if (!expect(fixture.has_value(), "literal-source-prepared"))
                return 5;
            const auto active = fixture->layers[0].id;
            owner->_session.ReplaceDocument(std::move(fixture.value()), active);
            owner->DocumentChanged();
            const auto settle = [&]() noexcept
            { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
            if (!expect(settle() && MatchesLiteral(owner->_acknowledgedCommittedDisplay, 128),
                        "committed-original-publishes-before-preview"))
                return 5;
            owner->OnCommand(live ? ChromeHost::kCommandNewExposureAdjustment : ChromeHost::kCommandExposure);
            if (!expect(settle() && owner->_session.HasColorEdit(FilterKind::Exposure) &&
                            owner->_session.AdjustmentEditingID().has_value() == live,
                        "real-panel-opening-acknowledges"))
                return 5;
            const auto generation = owner->_session.Generation();
            const auto* sourceIdentity = owner->_session.Document()->layers.Identity();
            const auto revision = owner->_session.history.CurrentRevision();
            const int undo = owner->_session.history.UndoCount();
            const auto legacy = owner->_composition.CompositeCount();
            owner->OnAdjustAction(AdjustAction::SetExposure, 1.0);
            if (!expect(settle(), "current-worker-source-and-original-GPU-acks-settle"))
                return 5;
            auto* first = owner->_pendingDisplay.GraphForFrameOwner();
            PendingDisplaySource source;
            expect(first && first != owner->_acknowledgedCommittedDisplay &&
                       owner->_session.DescribePendingDisplaySource(source) &&
                       source.kind ==
                           (live ? PendingDisplaySource::Kind::Adjustment : PendingDisplaySource::Kind::Color) &&
                       first->IsPublishedOnOwner(owner->_renderer.Device(), owner->_renderer.Context()) &&
                       MatchesLiteral(first, 176),
                   "private-preview-publishes-independent-plus-one-stop-literal");
            expect(owner->_session.Generation() == generation &&
                       owner->_session.Document()->layers.Identity() == sourceIdentity &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_session.history.UndoCount() == undo,
                   "preview-keeps-committed-source-generation-and-history");
            const auto presents = owner->_renderer.PresentCount();
            owner->RenderFrame();
            expect(owner->_renderer.PresentCount() == presents + 1 && owner->_composition.CompositeCount() == legacy &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()),
                   "actual-renderer-presents-preview-without-owner-composition");
            CommittedDisplayDescriptor descriptor;
            CommittedDisplaySeedLease forbiddenSeed;
            expect(owner->_pendingDisplayClient.DescribeCommittedDisplayOwner(descriptor) &&
                       !owner->_pendingDisplay.AcquirePublishedSeedOwner(descriptor.key, forbiddenSeed) &&
                       !forbiddenSeed,
                   "pending-root-cannot-be-borrowed-as-committed-seed");
            owner->OnAdjustAction(AdjustAction::SetExposure, -1.0);
            expect(owner->_pendingDisplay.GraphForFrameOwner() == first && owner->_session.Generation() == generation,
                   "newer-control-retains-compatible-acknowledged-preview");
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 92),
                        "replacement-publishes-independent-minus-one-stop-literal"))
                return 5;
            expect(!owner->_pendingDisplay.HasPending() && owner->_jobs.ReservedMemoryBytes() != 0 &&
                       owner->_jobs.ReleasingMemoryBytes() == 0,
                   "published-replacement-keeps-full-original-charge-after-old-retirement");
            const auto deviceGeneration = owner->_renderer.DeviceGeneration();
            if (!expect(SUCCEEDED(owner->_renderer.RecoverDeviceForTest()) &&
                            owner->_renderer.DeviceGeneration() != deviceGeneration,
                        "actual-renderer-rebuilds-device-with-pending-and-committed-roots-held"))
                return 5;
            owner->BindCompositionDevice();
            expect(!owner->_pendingDisplay.GraphForFrameOwner() && !owner->_acknowledgedCommittedDisplay,
                   "new-device-key-excludes-both-old-graphs-before-next-frame");
            if (!expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 92) &&
                            MatchesLiteral(owner->_acknowledgedCommittedDisplay, 128),
                        "both-current-sources-rebind-and-publish-original-literals-on-new-device"))
                return 5;
            expect(owner->_session.Generation() == generation &&
                       owner->_session.Document()->layers.Identity() == sourceIdentity &&
                       owner->_session.history.CurrentRevision() == revision &&
                       owner->_jobs.ReservedMemoryBytes() != 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                   "device-rebind-retires-old-root-charges-without-changing-document-or-history");
            const auto reboundComposes = owner->_composition.CompositeCount();
            const auto reboundPresents = owner->_renderer.PresentCount();
            owner->RenderFrame();
            expect(owner->_renderer.PresentCount() == reboundPresents + 1 &&
                       owner->_composition.CompositeCount() == reboundComposes &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()),
                   "actual-renderer-blits-rebound-preview-without-owner-composition");
            owner->OnAdjustAction(AdjustAction::SetPreview, 0.0);
            expect(owner->_pendingDisplay.GraphForFrameOwner() == nullptr &&
                       owner->_session.Generation() == generation &&
                       owner->_session.history.CurrentRevision() == revision,
                   "preview-off-excludes-old-pixels-before-next-frame");
            expect(settle() && !owner->_pendingDisplay.HasPending() &&
                       MatchesLiteral(owner->_acknowledgedCommittedDisplay, 128),
                   "preview-off-retires-root-and-displays-original-literal");
            owner->OnAdjustAction(AdjustAction::SetPreview, 1.0);
            expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 92),
                   "preview-on-publishes-one-current-replacement");
            owner->OnAdjustAction(commit ? AdjustAction::Apply : AdjustAction::Cancel, 0.0);
            CommittedDisplayDescriptor closingDescriptor;
            expect(
                owner->_session.HasColorEdit() && owner->ColorControlsFrozen() &&
                    (commit || (!owner->_pendingDisplay.GraphForFrameOwner() &&
                                !owner->_pendingDisplayClient.DescribeCommittedDisplayOwner(closingDescriptor))) &&
                    owner->_session.Generation() == generation && owner->_session.history.CurrentRevision() == revision,
                commit
                    ? "accepted-OK-retains-panel-and-original-authority-until-worker-acknowledgement"
                    : "accepted-Cancel-excludes-acknowledged-preview-but-retains-panel-until-worker-acknowledgement");
            expect(settle() && !owner->_session.HasColorEdit() && !owner->_pendingDisplay.GraphForFrameOwner() &&
                       !owner->_pendingDisplay.HasPending() &&
                       owner->_session.history.UndoCount() == undo + (commit ? 1 : 0) &&
                       MatchesLiteral(owner->_acknowledgedCommittedDisplay, commit ? 92 : 128),
                   commit ? "OK-publishes-exact-pixels-and-one-entry-after-pending-retirement"
                          : "Cancel-keeps-original-pixels-and-history-after-pending-retirement");
            owner->Shutdown();
            expect(owner->_shutdownComplete && !owner->_pendingDisplayInitialized &&
                       !owner->_committedDisplayInitialized && !owner->_pendingDisplay.HasPending() &&
                       !owner->_committedDisplay.HasPending() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                       owner->_jobs.ReleasingMemoryBytes() == 0,
                   "actual-joined-shutdown-retires-both-root-owners-and-one-shared-pump");
        }
    }
    for (unsigned int mode = 0; mode < 4; ++mode)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
        if (!expect(owner != nullptr, "in-flight-host-allocated"))
            return 5;
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                owner->Shutdown();
                owner.reset();
                while (PumpMessages().quit)
                {
                }
            });
        owner->_quietUi = true;
        owner->_prompts = &QuietPrompts();
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest() &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                    "in-flight-actual-WARP-host-initializes"))
            return 5;
        auto fixture = PendingFixture();
        if (!expect(fixture.has_value(), "in-flight-original-source-prepared"))
            return 5;
        const auto active = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(fixture.value()), active);
        owner->DocumentChanged();
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (!expect(settle() && MatchesLiteral(owner->_acknowledgedCommittedDisplay, 128) &&
                        !owner->_recordedCommands.HasPending(),
                    "in-flight-committed-original-has-detached-its-recorder"))
            return 5;
        owner->OnCommand(ChromeHost::kCommandExposure);
        if (!expect(settle() && owner->_session.HasColorEdit(FilterKind::Exposure) &&
                        !owner->_recordedCommands.HasPending(),
                    "in-flight-panel-opens-with-no-attached-recorder"))
            return 5;
        const auto authority = owner->_session.CaptureDocumentReadAuthority();
        const auto* originalRoot = owner->_session.Document()->layers.Identity();
        const auto revision = owner->_session.history.CurrentRevision();
        const auto retained = owner->_jobs.ReservedMemoryBytes();
        owner->OnAdjustAction(AdjustAction::SetExposure, 1.0);
        // Planning attaches the original source in its real completion callback, after this drain's single
        // owner-pump pass. Stop here before another outer pass can execute its first command. The worker may
        // record concurrently; its actual producer and all held references remain owned by this parent.
        const auto started = GetTickCount64();
        while (!owner->_recordedCommands.HasPending())
        {
            (void)DrainCompletions(owner->_jobs);
            if (owner->_recordedCommands.HasPending())
                break;
            const auto elapsed = GetTickCount64() - started;
            if (elapsed >= 15000)
                break;
            if (!owner->_jobs.HasUiWork() && WaitForSingleObject(static_cast<HANDLE>(owner->_jobs.WakeHandle()),
                                                                 static_cast<DWORD>(15000 - elapsed)) != WAIT_OBJECT_0)
                break;
        }
        if (!expect(owner->_recordedCommands.HasPending() && owner->_pendingDisplay.HasPending() &&
                        !owner->_pendingDisplay.GraphForFrameOwner() && owner->_jobs.ReservedMemoryBytes() > retained &&
                        owner->_session.CaptureDocumentReadAuthority() == authority &&
                        owner->_session.Document()->layers.Identity() == originalRoot &&
                        owner->_session.history.CurrentRevision() == revision,
                    "in-flight-original-recorder-attaches-with-full-admission-and-no-published-preview"))
            return 5;
        if (mode == 1)
        {
            owner->Shutdown();
            expect(owner->_shutdownComplete && !owner->_pendingDisplay.HasPending() &&
                       !owner->_committedDisplay.HasPending() && !owner->_recordedCommands.HasPending() &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                   "in-flight-joined-shutdown-retires-original-recorder-and-both-root-charges");
            continue;
        }
        if (mode == 2)
        {
            auto replacement = PendingFixture();
            if (!expect(replacement.has_value(), "in-flight-replacement-source-prepared"))
                return 5;
            const auto replacementActive = replacement->layers[0].id;
            owner->_session.ReplaceDocument(std::move(replacement.value()), replacementActive);
            owner->DocumentChanged();
            expect(!owner->_pendingDisplay.GraphForFrameOwner() && !owner->_session.HasColorEdit(),
                   "in-flight-document-replacement-excludes-old-opening-before-next-frame");
        }
        else if (mode == 3)
        {
            owner->_pendingDisplay.SetSuspendedOwner(true);
            expect(!owner->_pendingDisplay.GraphForFrameOwner(),
                   "in-flight-suspension-excludes-unpublished-opening-before-next-frame");
        }
        else
        {
            owner->OnAdjustAction(AdjustAction::Cancel, 0.0);
            CommittedDisplayDescriptor closingDescriptor;
            expect(!owner->_pendingDisplay.GraphForFrameOwner() && owner->_session.HasColorEdit() &&
                       owner->ColorControlsFrozen() &&
                       owner->_pendingCancelReceipt.Read().outcome == HostOutcome::Pending &&
                       !owner->_pendingDisplayClient.DescribeCommittedDisplayOwner(closingDescriptor) &&
                       owner->_session.CaptureDocumentReadAuthority() == authority &&
                       owner->_session.Document()->layers.Identity() == originalRoot &&
                       owner->_session.history.CurrentRevision() == revision,
                   "in-flight-Cancel-excludes-preview-but-retains-original-panel-source-and-history-until-"
                   "acknowledgement");
        }
        if (!expect(settle() && !owner->_pendingDisplay.HasPending() && !owner->_recordedCommands.HasPending() &&
                        MatchesLiteral(owner->_acknowledgedCommittedDisplay, 128) &&
                        owner->_jobs.ReleasingMemoryBytes() == 0,
                    "in-flight-original-producer-joins-and-retires-without-publishing-cancelled-pixels"))
            return 5;
        if (mode == 0)
        {
            owner->OnCommand(ChromeHost::kCommandExposure);
            expect(settle() && owner->_session.HasColorEdit(FilterKind::Exposure),
                   "in-flight-Cancel-can-reopen-with-a-new-original-panel-source");
        }
        if (mode != 2)
        {
            if (mode == 3)
                owner->_pendingDisplay.SetSuspendedOwner(false);
            owner->OnAdjustAction(AdjustAction::SetExposure, -1.0);
            expect(settle() && MatchesLiteral(owner->_pendingDisplay.GraphForFrameOwner(), 92) &&
                       owner->_session.CaptureDocumentReadAuthority() == authority &&
                       owner->_session.history.CurrentRevision() == revision,
                   "in-flight-next-current-opening-publishes-minus-one-stop-after-old-retirement");
        }
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_pendingDisplay.HasPending() &&
                   !owner->_committedDisplay.HasPending() && !owner->_recordedCommands.HasPending() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
               "in-flight-followup-shutdown-refunds-all-original-and-replacement-root-owners");
    }
    std::printf("application-pending-display summary: %u checks, %u failures\n", checks, failures);
    return failures ? 5 : 0;
}

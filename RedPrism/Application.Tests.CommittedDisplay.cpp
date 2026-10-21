#include "Application.h"
#include "DocumentDraft.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
std::optional<CanvasDocument> DisplayFixture()
{
    CanvasDocument document;
    document.width = 2;
    document.height = 2;
    auto draft = CanvasDocumentDraft::Rebuild(document, 1);
    if (!draft)
        return std::nullopt;
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    auto image = Raster::Make(2, 2, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    auto layer = ImageLayer::Blank(L"Admitted display", Vec2{2, 2});
    layer.asset = {std::move(image), {}, L"Admitted display"};
    layer.transform.sampling = LayerSampling::Nearest;
    if (!draft->Append(std::move(layer)))
        return std::nullopt;
    return draft->Seal();
}
} // namespace

int Application::RunStandaloneCommittedDisplaySelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        std::printf("%s application-committed-display.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        ++checks;
        failures += condition ? 0u : 1u;
        return condition;
    };
    for (unsigned int mode = 0; mode < 3; ++mode)
    {
        std::unique_ptr<Application> owner{new (std::nothrow) Application(instance, true, L"", 0, nullptr, nullptr)};
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
        if (!expect(SUCCEEDED(owner->InitializeProcessing()) && owner->EnableCommittedDisplayForTest(mode != 2) &&
                        SUCCEEDED(owner->RegisterWindowClass()) && SUCCEEDED(owner->CreateMainWindow(false)) &&
                        SUCCEEDED(owner->InitializeRenderer()) && SUCCEEDED(owner->InitializeChrome()),
                    "actual-hidden-WARP-host-and-original-recorder-initialize"))
            return 5;
        auto fixture = DisplayFixture();
        if (!expect(fixture.has_value(), "literal-source-and-immutable-index-prepared"))
            return 5;
        const auto active = fixture->layers[0].id;
        owner->_session.ReplaceDocument(std::move(*fixture), active);
        owner->DocumentChanged();
        const auto legacyComposites = owner->_composition.CompositeCount();
        const auto settle = [&]() noexcept
        { return SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get()) == SettleResult::Settled; };
        if (mode == 0)
        {
            if (!expect(settle(), "original-display-parent-and-all-original-GPU-uses-settle"))
                return 5;
            expect(owner->_acknowledgedCommittedDisplay &&
                       owner->_acknowledgedCommittedDisplay->IsPublishedOnOwner(owner->_renderer.Device(),
                                                                                owner->_renderer.Context()) &&
                       owner->_jobs.ReservedMemoryBytes() != 0 && !owner->_committedDisplay.HasPending(),
                   "exact-display-publishes-with-retained-original-charge");
            const auto presents = owner->_renderer.PresentCount();
            const auto attempts = owner->_renderer.PresentAttemptCount();
            owner->RenderFrame();
            const HRESULT presentOutcome = owner->_renderer.LastPresentResult();
            const bool completedAttempt = owner->_renderer.PresentAttemptCount() == attempts + 1 &&
                                          (presentOutcome == S_OK || presentOutcome == DXGI_STATUS_OCCLUDED);
            const bool visibleCountMatches =
                owner->_renderer.PresentCount() == presents + (presentOutcome == S_OK ? 1u : 0u);
            const bool blittedWithoutCompose = owner->_renderer.CanvasDrawn() &&
                                               SUCCEEDED(owner->_renderer.CanvasFailure()) &&
                                               owner->_composition.CompositeCount() == legacyComposites;
            if (!completedAttempt || !visibleCountMatches || !blittedWithoutCompose)
                std::printf("committed-display frame diagnostic: attempts=%llu->%llu presents=%llu->%llu "
                            "present-result=0x%08lx canvas=0x%08lx drawn=%u suspended=%u occluded=%u "
                            "composites=%llu->%llu frame-failure=0x%08lx failed-frames=%u\n",
                            static_cast<unsigned long long>(attempts),
                            static_cast<unsigned long long>(owner->_renderer.PresentAttemptCount()),
                            static_cast<unsigned long long>(presents),
                            static_cast<unsigned long long>(owner->_renderer.PresentCount()),
                            static_cast<unsigned long>(presentOutcome),
                            static_cast<unsigned long>(owner->_renderer.CanvasFailure()),
                            owner->_renderer.CanvasDrawn() ? 1u : 0u, owner->_renderer.IsSuspended() ? 1u : 0u,
                            owner->_renderer.IsOccluded() ? 1u : 0u, static_cast<unsigned long long>(legacyComposites),
                            static_cast<unsigned long long>(owner->_composition.CompositeCount()),
                            static_cast<unsigned long>(owner->_lastFrameFailure),
                            static_cast<unsigned int>(owner->_failedFrames));
            expect(completedAttempt && visibleCountMatches && blittedWithoutCompose,
                   "actual-renderer-blits-and-attempts-present-without-owner-composition");
            const auto occludedPresents = owner->_renderer.PresentCount();
            const auto occludedAttempts = owner->_renderer.PresentAttemptCount();
            owner->_renderer.FailNextStageForTest(Renderer::Stage::Present, DXGI_STATUS_OCCLUDED);
            owner->RenderFrame();
            expect(owner->_renderer.PresentAttemptCount() == occludedAttempts + 1 &&
                       owner->_renderer.LastPresentResult() == DXGI_STATUS_OCCLUDED &&
                       owner->_renderer.PresentCount() == occludedPresents && owner->_renderer.IsOccluded() &&
                       owner->_renderer.CanvasDrawn() && SUCCEEDED(owner->_renderer.CanvasFailure()) &&
                       owner->_composition.CompositeCount() == legacyComposites,
                   "explicit-occlusion-still-blits-and-attempts-present-without-counting-visible-frame");
            const auto generation = owner->_session.Generation();
            owner->_session.SetOpacity(0.5);
            owner->DocumentChanged();
            expect(owner->_session.Generation() != generation && settle() && owner->_acknowledgedCommittedDisplay &&
                       !owner->_committedDisplay.HasPending(),
                   "actual-session-edit-replaces-display-and-retires-old-frame");
            owner->RenderFrame();
            expect(owner->_composition.CompositeCount() == legacyComposites &&
                       SUCCEEDED(owner->_renderer.CanvasFailure()) && owner->_jobs.ReleasingMemoryBytes() == 0,
                   "replacement-keeps-legacy-compose-unused-and-retires-whole-root");
            owner->_session.ClearDocument();
            owner->DocumentChanged();
            expect(owner->_acknowledgedCommittedDisplay == nullptr,
                   "clearing-actual-session-excludes-old-frame-immediately");
            expect(settle() && owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
                   "actual-last-frame-and-worker-retirement-refund-display-charge");
            auto again = DisplayFixture();
            if (!expect(again.has_value(), "shutdown-cache-source-prepared"))
                return 5;
            const auto target = again->layers[0].id;
            owner->_session.ReplaceDocument(std::move(*again), target);
            owner->DocumentChanged();
            expect(settle() && owner->_acknowledgedCommittedDisplay, "published-cache-exists-before-actual-shutdown");
        }
        else if (mode == 1)
        {
            // The shared multiplexer may service another ready leaf first. Reach actual admission,
            // without settling the useful display or treating an unrelated notification as its job.
            const auto started = GetTickCount64();
            while (!owner->_committedDisplay.HasPending() && !owner->_acknowledgedCommittedDisplay &&
                   SUCCEEDED(owner->_committedDisplayFailure))
            {
                (void)DrainCompletions(owner->_jobs);
                if (owner->_committedDisplay.HasPending())
                    break;
                const auto elapsed = GetTickCount64() - started;
                if (elapsed >= 15000)
                    break;
                if (!owner->_jobs.HasUiWork() &&
                    WaitForSingleObject(static_cast<HANDLE>(owner->_jobs.WakeHandle()),
                                        static_cast<DWORD>(15000 - elapsed)) != WAIT_OBJECT_0)
                    break;
            }
            expect(owner->_committedDisplay.HasPending() && owner->_jobs.ReservedMemoryBytes() != 0,
                   "actual-shutdown-starts-with-admitted-original-display-in-flight");
        }
        else
        {
            expect(settle() && !owner->_acknowledgedCommittedDisplay && !owner->_committedDisplay.HasPending() &&
                       owner->_jobs.ReservedMemoryBytes() == 0,
                   "ordinary-hidden-host-admits-no-display-work");
            const auto resumed = [&](const char* name) noexcept
            {
                expect(settle() && owner->_acknowledgedCommittedDisplay &&
                           owner->_acknowledgedCommittedDisplay->IsPublishedOnOwner(owner->_renderer.Device(),
                                                                                    owner->_renderer.Context()) &&
                           owner->_jobs.ReservedMemoryBytes() != 0,
                       name);
            };
            const auto suspended = [&](const char* immediateName, const char* retiredName) noexcept
            {
                expect(owner->_acknowledgedCommittedDisplay == nullptr, immediateName);
                expect(settle() && !owner->_committedDisplay.HasPending() && owner->_jobs.ReservedMemoryBytes() == 0 &&
                           owner->_jobs.ReleasingMemoryBytes() == 0,
                       retiredName);
            };
            // Exercise actual handlers without exposing a native test window. The route uses their visibility
            // state, while this fixture's rendering tests remain hidden and independently opt in above.
            owner->_displayPoweredOn = true;
            (void)owner->HandleMessage(owner->_window.get(), WM_SHOWWINDOW, TRUE, 0);
            resumed("show-transition-admits-current-source-on-real-events");
            (void)owner->HandleMessage(owner->_window.get(), WM_SHOWWINDOW, FALSE, 0);
            suspended("hide-handler-excludes-old-display-before-return", "hidden-last-use-and-worker-refund-settle");
            owner->DocumentChanged();
            expect(settle() && !owner->_acknowledgedCommittedDisplay && owner->_jobs.ReservedMemoryBytes() == 0,
                   "document-change-while-hidden-admits-no-display");
            (void)owner->HandleMessage(owner->_window.get(), WM_SHOWWINDOW, TRUE, 0);
            resumed("second-show-transition-rebuilds-the-current-source");
            RECT client{};
            if (!expect(GetClientRect(owner->_window.get(), &client) != FALSE, "actual-client-size-observed"))
                return 5;
            (void)owner->HandleMessage(owner->_window.get(), WM_SIZE, SIZE_MINIMIZED, 0);
            suspended("minimize-handler-excludes-display-before-return", "minimized-root-retires-on-original-events");
            (void)owner->HandleMessage(owner->_window.get(), WM_SIZE, SIZE_RESTORED,
                                       MAKELPARAM(client.right - client.left, client.bottom - client.top));
            resumed("restore-handler-requests-current-display-once");
            struct DisplayPower final
            {
                GUID setting = GUID_SESSION_DISPLAY_STATUS;
                DWORD length = sizeof(DWORD);
                DWORD status = 0;
            } power;
            static_assert(offsetof(DisplayPower, status) == offsetof(POWERBROADCAST_SETTING, Data));
            (void)owner->HandleMessage(owner->_window.get(), WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                                       reinterpret_cast<LPARAM>(&power));
            suspended("power-off-handler-excludes-display-before-return", "power-off-root-retirement-refunds-charge");
            power.status = 1;
            (void)owner->HandleMessage(owner->_window.get(), WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                                       reinterpret_cast<LPARAM>(&power));
            resumed("power-on-handler-admits-one-current-display");
            owner->_renderer.SetOccludedForTest(true);
            owner->ReleaseSurfacesWhileUnseen();
            suspended("occlusion-policy-excludes-display-before-next-frame", "occluded-root-retires-without-polling");
            owner->_renderer.SetOccludedForTest(false);
            owner->ReleaseSurfacesWhileUnseen();
            resumed("confirmed-visible-policy-resumes-current-display");
        }
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_committedDisplayInitialized &&
                   !owner->_acknowledgedCommittedDisplay && !owner->_committedDisplay.HasPending() &&
                   owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0,
               "actual-joined-shutdown-retires-cached-or-in-flight-display-before-refund");
        owner->Shutdown();
        expect(owner->_shutdownComplete && !owner->_committedDisplayInitialized,
               "actual-shutdown-is-idempotent-after-display-route-detach");
    }
    std::printf("application-committed-display summary: %u checks, %u failures\n", checks, failures);
    return failures ? 5 : 0;
}

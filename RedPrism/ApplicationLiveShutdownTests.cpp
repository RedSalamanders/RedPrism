#include "Application.h"
#include "ApplicationShutdownTestProbe.h"

#include "BrushStroke.h"
#include "ChromeHost.h"
#include "DeferredRelease.h"
#include "DocumentLifecycleTestGate.h"
#include "WarpStroke.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>

namespace
{
enum class LiveEdit : std::uint8_t
{
    ImagePaint,
    ImageErase,
    MaskPaint,
    MaskErase,
    HealContentAware,
    HealCreateTexture,
    HealProximity,
    Clone,
    ImageBlur,
    MaskBlur,
    Liquify,
    Smudge,
};

struct LiveCase final
{
    const char* name;
    LiveEdit edit;
};

constexpr std::array<LiveCase, 12> kLiveCases{{
    {"imagePaint", LiveEdit::ImagePaint},
    {"imageErase", LiveEdit::ImageErase},
    {"maskPaint", LiveEdit::MaskPaint},
    {"maskErase", LiveEdit::MaskErase},
    {"HealContentAware", LiveEdit::HealContentAware},
    {"HealCreateTexture", LiveEdit::HealCreateTexture},
    {"HealProximity", LiveEdit::HealProximity},
    {"Clone", LiveEdit::Clone},
    {"imageBlur", LiveEdit::ImageBlur},
    {"maskBlur", LiveEdit::MaskBlur},
    {"Liquify", LiveEdit::Liquify},
    {"Smudge", LiveEdit::Smudge},
}};

struct LiveAuthority final
{
    Application* owner = nullptr;
    LiveEdit edit = LiveEdit::ImagePaint;
    std::uint64_t generation = 0;
    DocumentHistory::StateView history;
    std::size_t layerCount = 0;
    Guid layerId;
    LayerTransform transform;
    std::optional<Raster> mask;
    Raster source;
    const void* draftIdentity = nullptr;
    std::weak_ptr<BrushStroke> brush;
    std::weak_ptr<WarpStroke> warp;
    bool authorityUnchanged = false;
    bool exactDraftAlive = false;
    bool brushCoverageDetached = false;
    bool graphicsEngineDetached = false;
};

[[nodiscard]] bool HasLiveMask(LiveEdit edit) noexcept
{
    return edit == LiveEdit::MaskPaint || edit == LiveEdit::MaskErase || edit == LiveEdit::MaskBlur;
}

[[nodiscard]] bool IsWarp(LiveEdit edit) noexcept
{
    return edit == LiveEdit::Liquify || edit == LiveEdit::Smudge;
}

[[nodiscard]] bool SourceIsLiteral(const Raster& source) noexcept
{
    if (!source.HasStorage() || source.Width() != 2048 || source.Height() != 2048 ||
        source.ByteSize() != 16ull * 1024 * 1024 || source.Data() == nullptr)
        return false;

    const std::uint8_t* const pixels = source.Data();
    const std::size_t stride = static_cast<std::size_t>(source.Stride());
    for (int y = 0; y < source.Height(); ++y)
    {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < source.Width(); ++x)
        {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4;
            if (pixel[0] != 31 || pixel[1] != 63 || pixel[2] != 127 || pixel[3] != 255)
                return false;
        }
    }
    return true;
}

} // namespace

int Application::RunStandaloneLiveShutdownSelfTest(HINSTANCE instance) noexcept
{
    unsigned int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const char* scenario, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0 : 1;
        std::printf("%s application-live-shutdown.%s.%s\n", condition ? "PASS" : "FAIL", scenario, name);
    };

    for (const LiveCase& liveCase : kLiveCases)
    {
        for (const bool afterPreparation : std::array{false, true})
        {
            char scenario[80]{};
            (void)std::format_to_n(scenario, sizeof(scenario) - 1, "{}-{}", liveCase.name,
                                   afterPreparation ? "after-preparation" : "before-preparation");
            expect(!DeferredReleaseDetail::sink.enqueue, scenario, "no-surrogate-outer-sink");

            APTTYPE originalApartment{};
            APTTYPEQUALIFIER originalQualifier{};
            const HRESULT apartmentResult = CoGetApartmentType(&originalApartment, &originalQualifier);
            ApplicationShutdownTestProbe probe;
            LiveAuthority authority;
            auto gate = std::make_shared<DocumentLifecycleTestGate>();
            auto owner = std::make_unique<Application>(instance, true, nullptr, 0);
            const auto shutdown = wil::scope_exit(
                [&]() noexcept
                {
                    if (owner)
                        owner->Shutdown();
                });
            const auto checkLiveDraftAtOwnerDetach = [](void* opaque) noexcept
            {
                auto& check = *static_cast<LiveAuthority*>(opaque);
                Application& application = *check.owner;
                check.graphicsEngineDetached = !application._brushEngine.Ready();
                const ImageLayer* const layer = application._session.ActiveLayer();
                const auto& document = application._session.Document();
                const auto history = application._session.history.CaptureState();
                check.authorityUnchanged =
                    application._session.Generation() == check.generation &&
                    application._session.history.IsCurrent(check.history) && layer &&
                    history.Depth() == check.history.Depth() && history.PastCount() == check.history.PastCount() &&
                    history.FutureCount() == check.history.FutureCount() && layer->id == check.layerId &&
                    layer->transform == check.transform && layer->asset && layer->asset->image == check.source &&
                    (layer->mask.has_value() == check.mask.has_value()) &&
                    (!check.mask || layer->mask.value().asset.image == check.mask.value()) && document.has_value() &&
                    document.value().layers.size() == check.layerCount &&
                    application._session.IsMaskSelected() == HasLiveMask(check.edit) &&
                    (!HasLiveMask(check.edit) || layer->mask.has_value());
                // Do not retain an extra history root across final CPU retirement. Session roots still own it here.
                check.history = {};

                if (IsWarp(check.edit))
                {
                    const std::shared_ptr<WarpStroke> live = check.warp.lock();
                    check.exactDraftAlive =
                        live && live.get() == check.draftIdentity && live->IsValid() && live->Width() == 2048 &&
                        live->Height() == 2048 && live->Points().size() >= 2 &&
                        live->Mode() == (check.edit == LiveEdit::Liquify ? SmearMode::Liquify : SmearMode::Smudge);
                }
                else
                {
                    const std::shared_ptr<BrushStroke> live = check.brush.lock();
                    check.exactDraftAlive = live && live.get() == check.draftIdentity && live->IsValid() &&
                                            !live->Tiles().empty() && live->IsMask() == HasLiveMask(check.edit) &&
                                            live->Healing() == (check.edit == LiveEdit::HealContentAware ||
                                                                check.edit == LiveEdit::HealCreateTexture ||
                                                                check.edit == LiveEdit::HealProximity) &&
                                            live->Erasing() == (check.edit == LiveEdit::ImageErase);
                    check.brushCoverageDetached = live && !live->HasCoverageEngineForTest();
                }
                // The temporary strong lock is scoped to this callback; MTA retirement must observe expiration.
                check.brushCoverageDetached = check.brushCoverageDetached || IsWarp(check.edit);
            };
            owner->_quietUi = true;
            owner->_prompts = &QuietPrompts();
            if (FAILED(owner->InitializeProcessing()) || FAILED(owner->RegisterWindowClass()) ||
                FAILED(owner->CreateMainWindow(false)) || FAILED(owner->InitializeRenderer()) ||
                FAILED(owner->InitializeChrome()))
            {
                SelfTestFailure(
                    L"Standalone live shutdown fixture could not initialize its actual hidden WARP host.\n");
                return 5;
            }
            owner->BindCompositionDevice();
            expect(DeferredReleaseDetail::sink.enqueue && owner->_jobs.WorkerCount() != 0, scenario,
                   "actual-owner-has-own-sink-and-pool");
            expect(owner->_renderer.IsWarp() && owner->_brushEngine.Ready() && owner->_window && owner->_chrome,
                   scenario, "actual-hidden-WARP-graphics-and-chrome-live");

            owner->_session.CreateDocument(2048, 2048, true);
            const Guid layerId =
                InsertPaintedLayer(owner->_session, L"Live shutdown source", 2048, 2048, {}, 0, 0xFF7F3F1Fu);
            if (HasLiveMask(liveCase.edit))
                owner->_session.AddLayerMask(true);
            const ImageLayer* const initialLayer = owner->_session.ActiveLayer();
            const auto& initialDocument = owner->_session.Document();
            if (!initialLayer || !initialLayer->asset)
            {
                SelfTestFailure(L"Standalone live shutdown fixture did not create its actual source layer.\n");
                return 5;
            }
            Raster source = initialLayer->asset->image;
            expect(initialLayer->id == layerId && source.HasStorage() && source.Width() == 2048 &&
                       source.Height() == 2048 && source.ByteSize() == 16ull * 1024 * 1024 &&
                       owner->_session.history.CanUndo() && initialDocument.has_value() &&
                       initialDocument.value().layers.size() == 2 &&
                       (!HasLiveMask(liveCase.edit) ||
                        (initialLayer->mask.has_value() && owner->_session.IsMaskSelected())),
                   scenario, "literal-16MiB-source-history-and-semantic-mask-setup");

            gate->afterPreparation = afterPreparation;
            owner->_documentLifecycleTestGate = gate;
            owner->OnCommand(ChromeHost::kCommandNew);
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (gate->visits.load() == 0)
            {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message != DocumentLifecycleTestGate::kMessage)
                        owner->DispatchQueuedMessage(message);
                }
                (void)DrainCompletions(owner->_jobs);
                (void)RunUiActions(owner->_jobs);
                if (gate->visits.load() != 0)
                    break;
                const ULONGLONG now = GetTickCount64();
                if (now >= deadline)
                {
                    // Preserve reached state before the deadlock exit. A missed prerequisite, refused capture
                    // or preparation failure must be distinguishable from a worker/event that failed to wake.
                    const auto ready = owner->_jobs.UiReadinessForTest();
                    std::printf("DIAGNOSTIC application-live-shutdown.%s gate=%u captured=%d requested=%llu "
                                "reserved=%llu releasing=%llu lifecycle-ticket=%d lifecycle-stamp=%d "
                                "host-active=%d host-stamp=%d history-busy=%d idle=%d ui=%d "
                                "completions=%u actions=%u source=%d resumes=%d errors=%u error-id=%u\n",
                                scenario, gate->visits.load(), gate->captureReservationHeld.load() ? 1 : 0,
                                static_cast<unsigned long long>(gate->reservationBytes),
                                static_cast<unsigned long long>(owner->_jobs.ReservedMemoryBytes()),
                                static_cast<unsigned long long>(owner->_jobs.ReleasingMemoryBytes()),
                                owner->_documentLifecycleTicket.has_value() ? 1 : 0,
                                owner->_documentLifecycleStamp.has_value() ? 1 : 0,
                                owner->_activeHostRequest.has_value() ? 1 : 0,
                                owner->_hostCommitStamp.has_value() ? 1 : 0, owner->_historyWork.Busy() ? 1 : 0,
                                owner->_jobs.IsIdle() ? 1 : 0, owner->_jobs.HasUiWork() ? 1 : 0, ready.completions,
                                ready.actions, ready.source ? 1 : 0, ready.resumes ? 1 : 0,
                                static_cast<unsigned int>(owner->_errorsShown),
                                static_cast<unsigned int>(owner->_lastErrorId));
                    std::fflush(stdout);
                    SelfTestFailure(L"Standalone live shutdown fixture did not reach its event gate.\n");
                    std::_Exit(5);
                }
                const HANDLE wake = static_cast<HANDLE>(owner->_jobs.WakeHandle());
                if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    std::_Exit(5);
            }
            expect(gate->visits.load() == 1 && gate->captureReservationHeld.load(), scenario,
                   "lifecycle-job-reached-owned-event-gate");
            expect(owner->_documentLifecycleTicket && owner->_documentLifecycleStamp && owner->_historyWork.Busy() &&
                       owner->_jobs.ReservedMemoryBytes() != 0,
                   scenario, "lifecycle-authority-and-reservation-held-before-live-draft");

            owner->_session.brushSettings.diameter = 2.0;
            owner->_session.brushSettings.hardness = 1.0;
            owner->_session.brushSettings.opacity = 1.0;
            if (liveCase.edit == LiveEdit::MaskPaint)
                owner->_session.maskPaintWhite = true;
            else if (liveCase.edit == LiveEdit::MaskErase)
                owner->_session.maskPaintWhite = false;
            if (liveCase.edit == LiveEdit::HealContentAware)
                owner->_session.spotHealingMode = SpotHealingMode::ContentAware;
            else if (liveCase.edit == LiveEdit::HealCreateTexture)
                owner->_session.spotHealingMode = SpotHealingMode::CreateTexture;
            else if (liveCase.edit == LiveEdit::HealProximity)
                owner->_session.spotHealingMode = SpotHealingMode::ProximityMatch;
            if (liveCase.edit == LiveEdit::Clone)
            {
                owner->_session.cloneSettings.sampleAllLayers = false;
                owner->_session.cloneSettings.aligned = false;
                owner->_session.SetCloneSource(Vec2{256, 256});
            }
            if (liveCase.edit == LiveEdit::Liquify)
                owner->_session.smearMode = SmearMode::Liquify;
            else if (liveCase.edit == LiveEdit::Smudge)
                owner->_session.smearMode = SmearMode::Smudge;

            authority.owner = owner.get();
            authority.edit = liveCase.edit;
            authority.generation = owner->_session.Generation();
            authority.history = owner->_session.history.CaptureState();
            const auto& currentDocument = owner->_session.Document();
            authority.layerCount = currentDocument.has_value() ? currentDocument.value().layers.size() : 0;
            authority.source = source;
            authority.layerId = initialLayer->id;
            authority.transform = initialLayer->transform;
            if (initialLayer->mask)
                authority.mask = initialLayer->mask.value().asset.image;
            std::weak_ptr<BrushStroke> brushWeak;
            std::weak_ptr<WarpStroke> warpWeak;
            bool liveStarted = false;
            if (IsWarp(liveCase.edit))
            {
                owner->_session.brushSettings.diameter = 4.0;
                liveStarted = owner->_session.BeginWarp(Vec2{1024, 1024});
                if (liveStarted)
                    owner->_session.ContinueWarp(Vec2{1028, 1024});
                const WarpStroke* const warp = owner->_session.PendingWarp();
                if (warp)
                {
                    warpWeak = owner->_session.WeakPendingWarpForTest();
                    authority.draftIdentity = warp;
                    authority.warp = warpWeak;
                }
            }
            else
            {
                BrushKind kind = BrushKind::Paint;
                switch (liveCase.edit)
                {
                case LiveEdit::ImageErase:
                case LiveEdit::MaskErase:
                    kind = BrushKind::Erase;
                    break;
                case LiveEdit::HealContentAware:
                case LiveEdit::HealCreateTexture:
                case LiveEdit::HealProximity:
                    kind = BrushKind::Heal;
                    break;
                case LiveEdit::Clone:
                    kind = BrushKind::Clone;
                    break;
                case LiveEdit::ImageBlur:
                case LiveEdit::MaskBlur:
                    kind = BrushKind::Blur;
                    break;
                default:
                    break;
                }
                liveStarted = owner->_session.BeginBrush(Vec2{1024, 1024}, kind, nullptr);
                const BrushStroke* const brush = owner->_session.PendingBrush();
                if (brush)
                {
                    brushWeak = owner->_session.WeakPendingBrushForTest();
                    authority.draftIdentity = brush;
                    authority.brush = brushWeak;
                }
            }

            bool draftIsMeaningful = false;
            if (IsWarp(liveCase.edit))
            {
                const WarpStroke* const warp = owner->_session.PendingWarp();
                draftIsMeaningful =
                    liveStarted && warp && warp->IsValid() && warp->Width() == 2048 && warp->Height() == 2048 &&
                    warp->Points().size() >= 2 &&
                    warp->Mode() == (liveCase.edit == LiveEdit::Liquify ? SmearMode::Liquify : SmearMode::Smudge);
            }
            else
            {
                const BrushStroke* const brush = owner->_session.PendingBrush();
                const bool healing = liveCase.edit == LiveEdit::HealContentAware ||
                                     liveCase.edit == LiveEdit::HealCreateTexture ||
                                     liveCase.edit == LiveEdit::HealProximity;
                const bool erasing = liveCase.edit == LiveEdit::ImageErase;
                const bool clone = liveCase.edit == LiveEdit::Clone;
                const bool blur = liveCase.edit == LiveEdit::ImageBlur || liveCase.edit == LiveEdit::MaskBlur;
                const bool exactHealMode =
                    !healing || owner->_session.spotHealingMode ==
                                    (liveCase.edit == LiveEdit::HealContentAware    ? SpotHealingMode::ContentAware
                                     : liveCase.edit == LiveEdit::HealCreateTexture ? SpotHealingMode::CreateTexture
                                                                                    : SpotHealingMode::ProximityMatch);
                const bool exactMaskValue =
                    liveCase.edit != LiveEdit::MaskPaint && liveCase.edit != LiveEdit::MaskErase
                        ? true
                        : owner->_session.maskPaintWhite == (liveCase.edit == LiveEdit::MaskPaint);
                const bool exactCloneSource =
                    !clone ||
                    (!owner->_session.cloneSettings.sampleAllLayers && owner->_session.cloneSource.has_value() &&
                     owner->_session.CloneStrokeOffset(Vec2{1024, 1024}).has_value());
                const bool exactEditName = (healing && brush && brush->EditName() == L"Spot Healing") ||
                                           (blur && brush && brush->EditName() == L"Blur") ||
                                           (clone && brush && brush->EditName() == L"Clone Stamp") ||
                                           (!healing && !blur && !clone && brush && brush->EditName().empty());
                draftIsMeaningful = liveStarted && brush && brush->IsValid() && !brush->Tiles().empty() &&
                                    brush->IsMask() == HasLiveMask(liveCase.edit) && brush->Healing() == healing &&
                                    brush->Erasing() == erasing && brush->HasCoverageEngineForTest() && exactHealMode &&
                                    exactMaskValue && exactCloneSource && exactEditName &&
                                    (!blur || brush->SourceImage().HasStorage()) &&
                                    (!clone || brush->SourceImage().HasStorage());
            }
            expect(draftIsMeaningful, scenario,
                   IsWarp(liveCase.edit) ? "valid-warp-mode-dimensions-and-real-points"
                                         : "actual-brush-kind-mode-mask-and-real-tiles");
            expect(!brushWeak.expired() || !warpWeak.expired(), scenario,
                   "exact-session-draft-observed-weakly-with-no-test-owner");

            std::weak_ptr<void> liveWeak;
            if (IsWarp(liveCase.edit))
                liveWeak = warpWeak;
            else
                liveWeak = brushWeak;
            if (!IsWarp(liveCase.edit))
                probe.draft = brushWeak;
            probe.liveDraft = liveWeak;
            probe.checkContext = &authority;
            probe.checkOwner = checkLiveDraftAtOwnerDetach;
            owner->_shutdownTestProbe = &probe;
            owner->Shutdown();

            expect(owner->_shutdownComplete && !owner->_window && !owner->_chrome && !owner->_rendererReady, scenario,
                   "actual-application-shutdown-complete");
            expect(probe.ownerCalls == 1 && probe.liveDraftAliveAtDetach &&
                       probe.draftAliveAtDetach == !IsWarp(liveCase.edit) && authority.exactDraftAlive &&
                       authority.authorityUnchanged && authority.brushCoverageDetached &&
                       authority.graphicsEngineDetached && probe.graphicsDetachedOnOwner &&
                       !owner->_brushEngine.Ready(),
                   scenario, "owner-callback-saw-original-authority-and-detached-live-draft-graphics");
            expect(probe.workerCalls.load() == 1 && probe.mta.load() && probe.offOwner.load() &&
                       (IsWarp(liveCase.edit) || probe.draftRetired.load()) && probe.liveDraftRetired.load() &&
                       liveWeak.expired() && brushWeak.expired() && warpWeak.expired(),
                   scenario, "exact-live-draft-expires-on-final-MTA-retirement");
            expect(!owner->_session.Document() && !owner->_session.history.CanUndo() &&
                       owner->_session.Generation() > authority.generation,
                   scenario, "session-empty-after-authority-callback-and-retirement");
            expect(SourceIsLiteral(source), scenario, "independently-retained-literal-BGRA-bytes-unchanged");
            expect(gate->retired.load() == 1 && gate->retiredOffOwner.load() && !owner->_documentLifecycleTicket &&
                       !owner->_documentLifecycleStamp && !owner->_historyWork.Busy(),
                   scenario, "lifecycle-payload-retired-and-ticket-barrier-acknowledged");
            expect(owner->_jobs.IsIdle() && owner->_jobs.WorkerCount() == 0 && !owner->_jobs.WakeHandle() &&
                       owner->_jobs.ReservedMemoryBytes() == 0 && owner->_jobs.ReleasingMemoryBytes() == 0 &&
                       !DeferredReleaseDetail::sink.enqueue,
                   scenario, "pool-tickets-reservations-events-and-sink-released");
            APTTYPE apartment{};
            APTTYPEQUALIFIER qualifier{};
            expect(SUCCEEDED(apartmentResult) && SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
                       apartment == originalApartment && qualifier == originalQualifier,
                   scenario, "caller-STA-preserved");

            owner->Shutdown();
            expect(owner->_shutdownComplete && gate->retired.load() == 1 && probe.ownerCalls == 1 &&
                       probe.workerCalls.load() == 1,
                   scenario, "second-shutdown-does-not-repeat-retirement-hooks");
            owner->_shutdownTestProbe = nullptr;
            owner.reset();

            bool consumedQuit = false;
            for (;;)
            {
                const auto drained = PumpMessages();
                consumedQuit = consumedQuit || drained.quit;
                if (!drained.quit)
                    break;
            }
            expect(consumedQuit, scenario, "fixture-quit-consumed-before-next-application");
        }
    }

    std::printf("application-live-shutdown summary: %u checks, %d failures\n", checks, failures);
    return failures ? 5 : 0;
}

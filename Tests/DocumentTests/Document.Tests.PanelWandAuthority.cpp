#include "Document.Tests.PanelWandAuthority.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "Selection.h"
#include "WandLayerSelectionWork.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    gFailures += condition ? 0 : 1;
    std::printf("%s panel-wand-authority.%s\n", condition ? "PASS" : "FAIL", name);
}

consteval std::array<std::uint8_t, 64> OpaquePixels()
{
    std::array<std::uint8_t, 64> pixels{};
    for (std::size_t index = 3; index < pixels.size(); index += 4)
        pixels[index] = 255;
    return pixels;
}

constexpr auto kPixels = OpaquePixels();
constexpr std::array<std::uint8_t, 16> kMaskPixels{255, 255, 255, 255, 255, 255, 255, 255,
                                                   255, 255, 255, 255, 255, 255, 255, 255};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Fixture final
{
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    Guid active;
    Guid other;
    bool ready = false;

    explicit Fixture(bool openPanel = true)
    {
        auto document = std::make_unique<DocumentFixture>();
        document->id = Guid::Generate();
        document->width = 4;
        document->height = 4;

        ImageLayer layer = ImageLayer::Blank(L"Panel Wand authority source", Vec2{4, 4});
        active = layer.id;
        layer.asset =
            RasterAsset{Raster::Make(4, 4, RasterFormat::Bgra8888, kPixels.data(), kPixels.size()), {}, layer.name};
        layer.transform.size = Vec2{4, 4};
        constexpr std::array<std::uint8_t, 16> maskPixels = kMaskPixels;
        layer.mask =
            LayerMask{RasterAsset{Raster::Make(4, 4, RasterFormat::Gray8, maskPixels.data(), maskPixels.size()),
                                  {},
                                  L"Panel Wand authority mask"},
                      true, std::nullopt, true};
        document->layers.push_back(std::move(layer));

        ImageLayer second = ImageLayer::Blank(L"Panel Wand authority other layer", Vec2{1, 1});
        other = second.id;
        constexpr std::array<std::uint8_t, 4> secondPixel{0, 0, 0, 255};
        second.asset = RasterAsset{
            Raster::Make(1, 1, RasterFormat::Bgra8888, secondPixel.data(), secondPixel.size()), {}, second.name};
        document->layers.push_back(std::move(second));

        session->ReplaceDocument(std::move(*document).Seal(), active);
        if (openPanel)
            session->BeginColorEdit(FilterKind::Levels, true);
        ready = session->Document().has_value() && session->ActiveLayerID() == std::optional(active) &&
                session->ActiveLayer() != nullptr &&
                (openPanel ? session->HasColorEdit(FilterKind::Levels) : !session->HasColorEdit());
    }
};
#pragma warning(pop)

bool AbsentPanelRefusesDefaults()
{
    auto fixture = std::make_unique<Fixture>(false);
    if (!fixture->ready)
        return false;
    PanelWandOpening opening;
    PanelWandAuthority authority;
    auto capturedOpening = fixture->session->CapturePanelWandOpening();
    auto capturedAuthority = fixture->session->CapturePanelWandAuthority();
    return !capturedOpening && !capturedAuthority && !fixture->session->IsPanelWandOpeningCurrent(opening) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority);
}

bool CurrentOpeningAccepted()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    return opening.has_value() && fixture->session->IsPanelWandOpeningCurrent(opening.value());
}

bool CompleteAuthorityAccepted()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto authority = fixture->session->CapturePanelWandAuthority();
    return authority.has_value() && fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool StrictHistoryRestoreRefused()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto authority = fixture->session->CapturePanelWandAuthority();
    return authority.has_value() && fixture->session->IsPanelWandAuthorityCurrent(authority.value()) &&
           !fixture->session->CanApplyHistoryRestore(fixture->session->CaptureHistoryRestoreAuthority());
}

bool GenerationChangeKeepsOpeningStalesSource()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority)
        return false;
    const std::uint64_t generationBefore = fixture->session->Generation();
    const DocumentSelection selection{true, {SelectionLoop{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}};
    fixture->session->SetSelection(selection, L"Panel Wand authority generation transition");
    return fixture->session->Generation() == generationBefore + 1 &&
           fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool ActiveTargetStalesSource()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority)
        return false;
    fixture->session->SelectLayer(fixture->other);
    return fixture->session->ActiveLayerID() == std::optional(fixture->other) &&
           fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool MaskTargetStalesSource()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority)
        return false;
    fixture->session->SelectLayerTarget(fixture->active, true);
    return fixture->session->IsMaskSelected() && fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool CropFrameStalesSource()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority)
        return false;
    const CropRect crop{0, 0, 2, 2};
    fixture->session->SetCropFrame(crop);
    return fixture->session->CropFrame() == std::optional(crop) &&
           fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool SettingsSequenceStalesBoth()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready)
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority || !fixture->session->PendingColorEdit())
        return false;
    const std::uint64_t sequenceBefore = fixture->session->ColorEditSequence();
    auto settings = std::make_unique<FilterSettings>(fixture->session->PendingColorEdit().value().settings);
    settings->levels.ranges[0].gamma = 1.25;
    fixture->session->UpdateColorEdit(*settings, false);
    return fixture->session->ColorEditSequence() == sequenceBefore + 1 &&
           !fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value());
}

bool CancelAndEqualReopenStalesOldWitnesses()
{
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->ready || !fixture->session->PendingColorEdit())
        return false;
    auto opening = fixture->session->CapturePanelWandOpening();
    auto authority = fixture->session->CapturePanelWandAuthority();
    if (!opening || !authority)
        return false;
    const std::uint64_t sequenceBefore = fixture->session->ColorEditSequence();
    const std::uint64_t openingBefore = fixture->session->PendingColorEdit().value().openingSequence;
    auto settingsBefore = std::make_unique<FilterSettings>(fixture->session->PendingColorEdit().value().settings);
    fixture->session->CancelColorEdit();
    if (fixture->session->HasColorEdit())
        return false;
    fixture->session->BeginColorEdit(FilterKind::Levels, true);
    if (!fixture->session->PendingColorEdit())
        return false;
    auto reopenedOpening = fixture->session->CapturePanelWandOpening();
    auto reopenedAuthority = fixture->session->CapturePanelWandAuthority();
    const auto& reopened = fixture->session->PendingColorEdit().value();
    return reopened.kind == FilterKind::Levels && reopened.settings == *settingsBefore &&
           reopened.openingSequence != openingBefore && fixture->session->ColorEditSequence() > sequenceBefore &&
           !fixture->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !fixture->session->IsPanelWandAuthorityCurrent(authority.value()) && reopenedOpening &&
           fixture->session->IsPanelWandOpeningCurrent(reopenedOpening.value()) && reopenedAuthority &&
           fixture->session->IsPanelWandAuthorityCurrent(reopenedAuthority.value());
}

bool CrossSessionWitnessRefused()
{
    auto owner = std::make_unique<Fixture>();
    auto otherSession = std::make_unique<Fixture>();
    if (!owner->ready || !otherSession->ready)
        return false;
    auto opening = owner->session->CapturePanelWandOpening();
    auto authority = owner->session->CapturePanelWandAuthority();
    return opening && authority && owner->session->IsPanelWandOpeningCurrent(opening.value()) &&
           owner->session->IsPanelWandAuthorityCurrent(authority.value()) &&
           !otherSession->session->IsPanelWandOpeningCurrent(opening.value()) &&
           !otherSession->session->IsPanelWandAuthorityCurrent(authority.value());
}
} // namespace

int RunPanelWandAuthorityTests()
{
    gFailures = 0;
    Expect(AbsentPanelRefusesDefaults(), "panel.absent-default-opening-and-authority-refuse");
    Expect(CurrentOpeningAccepted(), "opening.current-session-opening-valid");
    Expect(CompleteAuthorityAccepted(), "authority.complete-source-valid");
    Expect(StrictHistoryRestoreRefused(), "history.restore-remains-strict-with-live-panel");
    Expect(GenerationChangeKeepsOpeningStalesSource(),
           "generation.selection-edit-plus-one-keeps-opening-stales-source");
    Expect(ActiveTargetStalesSource(), "stale.active-target-change-invalidates-source");
    Expect(MaskTargetStalesSource(), "stale.mask-target-change-invalidates-source");
    Expect(CropFrameStalesSource(), "stale.crop-frame-change-invalidates-source");
    Expect(SettingsSequenceStalesBoth(), "sequence.settings-update-plus-one-stales-opening-and-source");
    Expect(CancelAndEqualReopenStalesOldWitnesses(), "reopen.cancel-equal-settings-stales-old-witnesses");
    Expect(CrossSessionWitnessRefused(), "session.cross-session-witnesses-refuse");
    return gFailures;
}

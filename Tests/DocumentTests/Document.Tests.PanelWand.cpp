#include "Document.Tests.PanelWand.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "Document.h"
#include "Selection.h"
#include "WandLayerSelectionWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s panel-wand-core.%s\n", condition ? "PASS" : "FAIL", name);
}

enum class Route
{
    Layer,
    Composite,
};

// Independent 4x4 BGRA inputs: opaque black except the red pixel at (1,1).
// The detached composite is supplied explicitly, never obtained by rendering the source.
constexpr std::array<std::uint8_t, 64> kLayerPixels{
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255,
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0, 255, 0, 0, 0, 255};
constexpr std::array<std::uint8_t, 64> kCompositePixels{
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255,
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0, 255, 0, 0, 0, 255};
constexpr std::array<std::uint8_t, 64> kChangedSource{
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0,   0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
// Separate route controls use this literal 2x1 red region, while the active layer remains 1x1 red.
constexpr std::array<std::uint8_t, 64> kTwoRedComposite{
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 255,
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0,   255, 0, 0, 0,   255, 0, 0, 0, 255};
constexpr Vec2 kPoint{1.5, 1.5};

DocumentSelection Seed(SelectionMode mode)
{
    if (mode == SelectionMode::Subtract)
        return DocumentSelection{true, {SelectionLoop{{0, 1}, {2, 1}, {2, 2}, {0, 2}}}};
    return DocumentSelection{true, {SelectionLoop{{0, 1}, {1, 1}, {1, 2}, {0, 2}}}};
}

bool LiteralRect(const std::optional<DocumentSelection>& value, double x, double width, bool antialiased)
{
    if (!value || value.value().antialiased != antialiased || value.value().loops.size() != 1)
        return false;
    // Precision-2 boolean output may choose a different first corner or retain collinear edge vertices.
    // The reference is these four clockwise corners, not geometry produced by the Wand implementation.
    const std::array expected{Vec2{x, 1}, Vec2{x + width, 1}, Vec2{x + width, 2}, Vec2{x, 2}};
    const auto& points = value.value().loops.Get().front();
    SelectionLoop corners;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const auto& before = points[(index + points.size() - 1) % points.size()];
        const auto& point = points[index];
        const auto& after = points[(index + 1) % points.size()];
        const double dx = point.x - before.x;
        const double dy = point.y - before.y;
        const double nextDx = after.x - point.x;
        const double nextDy = after.y - point.y;
        if (dx * nextDy - dy * nextDx != 0 || dx * nextDx + dy * nextDy <= 0)
            corners.push_back(point);
    }
    if (corners.size() != expected.size())
        return false;
    for (std::size_t first = 0; first < expected.size(); ++first)
    {
        bool equal = true;
        for (std::size_t index = 0; index < expected.size(); ++index)
            equal = equal && corners[(first + index) % expected.size()] == expected[index];
        if (equal)
            return true;
    }
    return false;
}

bool SameTarget(const std::optional<TargetIdentity>& left, const std::optional<TargetIdentity>& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    const auto& a = left.value();
    const auto& b = right.value();
    return a.layerId == b.layerId && a.image == b.image && a.transform == b.transform && a.mask == b.mask &&
           a.isGroup == b.isGroup && a.adjustment == b.adjustment;
}

bool SameToken(const std::optional<JobToken>& left, const std::optional<JobToken>& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return true;
    const auto& a = left.value();
    const auto& b = right.value();
    return a.sessionIdentity == b.sessionIdentity && a.documentSerial == b.documentSerial &&
           a.generation == b.generation && a.restoreSerial == b.restoreSerial && SameTarget(a.target, b.target) &&
           a.selection == b.selection && a.canvasWidth == b.canvasWidth && a.canvasHeight == b.canvasHeight &&
           a.sequence == b.sequence;
}

bool SameDrag(const std::optional<HueTargetDrag>& left, const std::optional<HueTargetDrag>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || (left.value().range == right.value().range && left.value().hue == right.value().hue &&
                      left.value().saturation == right.value().saturation));
}

// Compare every stored public ColorEdit field explicitly. The full ColorEdit copy lives on the heap.
// No private Wand authority representation is inspected by this fixture.
bool SamePanel(const ColorEdit& a, const ColorEdit& b) noexcept
{
    return a.kind == b.kind && a.layerID == b.layerID && a.original == b.original && a.transform == b.transform &&
           a.selection == b.selection && a.settings == b.settings && SameToken(a.sourceToken, b.sourceToken) &&
           a.deferred == b.deferred && a.openingSequence == b.openingSequence &&
           a.previewSequence == b.previewSequence && a.previewInvalidatedSequence == b.previewInvalidatedSequence &&
           a.commitFailed == b.commitFailed && a.preview == b.preview && a.seed == b.seed &&
           a.grownImage == b.grownImage && a.retiredGrownImage == b.retiredGrownImage && a.grownGrid == b.grownGrid &&
           a.grownTransform == b.grownTransform && a.grownMargin == b.grownMargin && a.grownOffsetX == b.grownOffsetX &&
           a.grownOffsetY == b.grownOffsetY && a.previewSource == b.previewSource && a.previewScale == b.previewScale &&
           a.previewRaster == b.previewRaster && a.previewCoverage == b.previewCoverage &&
           a.previewLookup == b.previewLookup && a.previewLookupOnly == b.previewLookupOnly &&
           a.previewTransform == b.previewTransform && a.previewFailed == b.previewFailed &&
           a.histogram == b.histogram && a.histogramReady == b.histogramReady && a.levelsSample == b.levelsSample &&
           a.hueSample == b.hueSample && a.targeting == b.targeting && SameDrag(a.targetDrag, b.targetDrag);
}

bool LiteralPixels(const Raster& image, const std::array<std::uint8_t, 64>& expected)
{
    if (image.Width() != 4 || image.Height() != 4 || image.Format() != RasterFormat::Bgra8888 || image.Stride() != 16)
        return false;
    const auto* bytes = image.Data();
    return bytes && std::equal(expected.begin(), expected.end(), bytes);
}

void ConfigurePanel(DocumentSession& session, FilterKind kind)
{
    auto settings = std::make_unique<FilterSettings>(session.PendingColorEdit().value().settings);
    if (kind == FilterKind::Levels)
        settings->levels.ranges[0].gamma = 1.25;
    else if (kind == FilterKind::HueSaturation)
        settings->hsv.adjustments[0] = RangeAdjustment{17, 23, -11};
    else
        settings->exposure = ExposureSettings{1.25, 0.125, 1.5};
    session.UpdateColorEdit(*settings, true);
    if (kind == FilterKind::Levels)
        session.SetLevelsSampleMode(LevelsSample::Gray);
    else if (kind == FilterKind::HueSaturation)
        session.SetHueSampleMode(HueSampleMode::Add);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Fixture final
{
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    Guid active;
    Guid other;
    Raster composite;
    bool ready = false;

    Fixture(FilterKind kind, SelectionMode mode, bool openPanel = true)
    {
        auto document = std::make_unique<DocumentFixture>();
        document->id = Guid::Generate();
        document->width = 4;
        document->height = 4;
        ImageLayer layer = ImageLayer::Blank(L"Panel Wand literal source", Vec2{4, 4});
        active = layer.id;
        layer.asset = RasterAsset{
            Raster::Make(4, 4, RasterFormat::Bgra8888, kLayerPixels.data(), kLayerPixels.size()), {}, layer.name};
        layer.transform.size = Vec2{4, 4};
        layer.transform.sampling = LayerSampling::Nearest;
        constexpr std::array<std::uint8_t, 16> mask{255, 255, 255, 255, 255, 255, 255, 255,
                                                    255, 255, 255, 255, 255, 255, 255, 255};
        layer.mask = LayerMask{
            RasterAsset{Raster::Make(4, 4, RasterFormat::Gray8, mask.data(), mask.size()), {}, L"Panel Wand mask"},
            true, std::nullopt, true};
        document->layers.push_back(std::move(layer));
        ImageLayer second = ImageLayer::Blank(L"Unrelated active target", Vec2{1, 1});
        other = second.id;
        constexpr std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
        second.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, blue.data(), blue.size()), {}, second.name};
        document->layers.push_back(std::move(second));
        session->ReplaceDocument(std::move(*document).Seal(), active);
        session->SetSelection(Seed(mode), L"Panel Wand seed");
        session->history.Reset();
        composite = Raster::Make(4, 4, RasterFormat::Bgra8888, kCompositePixels.data(), kCompositePixels.size());
        if (openPanel)
        {
            session->BeginColorEdit(kind, true);
            if (!session->HasColorEdit(kind))
                return;
            ConfigurePanel(*session, kind);
        }
        ready = session->Document().has_value() && LiteralPixels(session->ActiveLayer()->asset->image, kLayerPixels) &&
                LiteralPixels(composite, kCompositePixels) &&
                (!openPanel ||
                 (session->PendingColorEdit()->deferred && session->PendingColorEdit()->selection == Seed(mode) &&
                  LiteralPixels(session->PendingColorEdit()->original, kLayerPixels)));
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Snapshot final
{
    std::optional<CanvasDocument> document;
    std::optional<Guid> active;
    DocumentHistory::StateView history;
    Guid revision;
    std::uint64_t generation;
    std::uint64_t colorSequence;
    std::unique_ptr<ColorEdit> panel;
    FilterSettings settingsTemplate;
    std::optional<CropRect> crop;
    bool mask;
    std::optional<LayerTransform> transform;
    std::uint64_t transformRevision;

    explicit Snapshot(const DocumentSession& session)
        : document(session.Document()), active(session.ActiveLayerID()), history(session.history.CaptureState()),
          revision(session.history.CurrentRevision()), generation(session.Generation()),
          colorSequence(session.ColorEditSequence()),
          panel(session.PendingColorEdit() ? std::make_unique<ColorEdit>(session.PendingColorEdit().value()) : nullptr),
          settingsTemplate(session.filterSettings), crop(session.CropFrame()), mask(session.IsMaskSelected()),
          transform(session.PendingTransformEdit() ? std::optional(session.PendingTransformEdit()->draft)
                                                   : std::nullopt),
          transformRevision(session.TransformDraftRevision())
    {
    }
};
#pragma warning(pop)

bool PanelUnchanged(const DocumentSession& session, const Snapshot& before)
{
    return session.PendingColorEdit().has_value() == static_cast<bool>(before.panel) &&
           (!before.panel || (SamePanel(session.PendingColorEdit().value(), *before.panel) &&
                              LiteralPixels(session.PendingColorEdit()->original, kLayerPixels))) &&
           session.ColorEditSequence() == before.colorSequence && session.filterSettings == before.settingsTemplate;
}

bool StateUnchanged(const DocumentSession& session, const Snapshot& before)
{
    const auto& transform = session.PendingTransformEdit();
    return session.Document() == before.document && session.ActiveLayerID() == before.active &&
           session.history.IsCurrent(before.history) && session.history.CurrentRevision() == before.revision &&
           session.Generation() == before.generation && PanelUnchanged(session, before) &&
           session.CropFrame() == before.crop && session.IsMaskSelected() == before.mask &&
           transform.has_value() == before.transform.has_value() &&
           (!transform || transform->draft == before.transform.value()) &&
           session.TransformDraftRevision() == before.transformRevision;
}

WandSettings Settings(Route route) noexcept
{
    WandSettings result;
    result.tolerance = 0;
    result.sampleSize = WandSampleSize::Point;
    result.contiguous = true;
    result.sampleAllLayers = route == Route::Composite;
    return result;
}

std::optional<WandLayerSelectionInput> Capture(DocumentSession& session, Route route,
                                               SelectionMode mode = SelectionMode::New)
{
    return route == Route::Layer ? session.PreparePanelWandLayerSelectionInput(kPoint, Settings(route), false, mode)
                                 : session.PreparePanelWandCompositeSelectionInput(kPoint, Settings(route), true, mode);
}

std::optional<PreparedWandLayerSelection> Prepare(WandLayerSelectionInput input, const Fixture& fixture, Route route,
                                                  WandFailure* failure = nullptr, CancelToken cancellation = {})
{
    return route == Route::Layer
               ? DocumentSession::PrepareWandLayerSelectionPublication(std::move(input), failure, cancellation)
               : DocumentSession::PrepareWandCompositeSelectionPublication(std::move(input), fixture.composite, failure,
                                                                           cancellation);
}

bool Planned(DocumentSession& session, Route route, SelectionMode mode)
{
    const auto plan = route == Route::Layer
                          ? session.PlanPanelWandLayerSelectionMemory(kPoint, Settings(route), mode)
                          : session.PlanPanelWandCompositeSelectionMemory(kPoint, Settings(route), mode);
    if (!plan)
        return false;
    std::uint64_t required = 0;
    return EvaluateWandLayerSelectionAdmission(plan.value(), mode, session.Selection().has_value(), required) ==
               WandLayerSelectionAdmission::Ready &&
           required == plan->reservationBytes && required != 0;
}

struct GeometryCase final
{
    FilterKind kind;
    Route route;
    SelectionMode mode;
    const char* name;
};

constexpr std::array kGeometryCases{
    GeometryCase{FilterKind::Levels, Route::Layer, SelectionMode::New, "levels.layer.new-preserves-whole-panel"},
    GeometryCase{FilterKind::Levels, Route::Layer, SelectionMode::Add, "levels.layer.add-preserves-whole-panel"},
    GeometryCase{FilterKind::Levels, Route::Layer, SelectionMode::Subtract,
                 "levels.layer.subtract-preserves-whole-panel"},
    GeometryCase{FilterKind::Levels, Route::Composite, SelectionMode::New,
                 "levels.composite.new-preserves-whole-panel"},
    GeometryCase{FilterKind::Levels, Route::Composite, SelectionMode::Add,
                 "levels.composite.add-preserves-whole-panel"},
    GeometryCase{FilterKind::Levels, Route::Composite, SelectionMode::Subtract,
                 "levels.composite.subtract-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Layer, SelectionMode::New, "hsv.layer.new-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Layer, SelectionMode::Add, "hsv.layer.add-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Layer, SelectionMode::Subtract,
                 "hsv.layer.subtract-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Composite, SelectionMode::New,
                 "hsv.composite.new-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Composite, SelectionMode::Add,
                 "hsv.composite.add-preserves-whole-panel"},
    GeometryCase{FilterKind::HueSaturation, Route::Composite, SelectionMode::Subtract,
                 "hsv.composite.subtract-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Layer, SelectionMode::New, "exposure.layer.new-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Layer, SelectionMode::Add, "exposure.layer.add-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Layer, SelectionMode::Subtract,
                 "exposure.layer.subtract-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Composite, SelectionMode::New,
                 "exposure.composite.new-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Composite, SelectionMode::Add,
                 "exposure.composite.add-preserves-whole-panel"},
    GeometryCase{FilterKind::Exposure, Route::Composite, SelectionMode::Subtract,
                 "exposure.composite.subtract-preserves-whole-panel"}};

bool Geometry(const GeometryCase& test)
{
    auto fixture = std::make_unique<Fixture>(test.kind, test.mode);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto before = std::make_unique<Snapshot>(session);
    if (!Planned(session, test.route, test.mode))
        return false;
    auto input = Capture(session, test.route, test.mode);
    if (!input || !StateUnchanged(session, *before))
        return false;
    WandFailure failure = WandFailure::None;
    auto prepared = Prepare(std::move(input.value()), *fixture, test.route, &failure);
    if (!prepared || failure != WandFailure::None || !StateUnchanged(session, *before) ||
        !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) ||
        !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()))
        return false;
    const double x = test.mode == SelectionMode::New ? 1 : 0;
    const double width = test.mode == SelectionMode::Add ? 2 : 1;
    return LiteralRect(session.Selection(), x, width, test.route == Route::Composite) &&
           PanelUnchanged(session, *before) && session.PendingColorEdit()->selection == Seed(test.mode) &&
           LiteralPixels(session.ActiveLayer()->asset->image, kLayerPixels) && session.history.UndoCount() == 1 &&
           session.history.UndoName() == L"Magic Wand" && session.history.CaptureState().Depth() == 0;
}

bool RouteDifference(Route route)
{
    auto fixture = std::make_unique<Fixture>(FilterKind::HueSaturation, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    fixture->composite = Raster::Make(4, 4, RasterFormat::Bgra8888, kTwoRedComposite.data(), kTwoRedComposite.size());
    auto before = std::make_unique<Snapshot>(session);
    if (!LiteralPixels(fixture->composite, kTwoRedComposite) || !Planned(session, route, SelectionMode::New))
        return false;
    auto input = Capture(session, route);
    if (!input)
        return false;
    auto prepared = Prepare(std::move(input.value()), *fixture, route);
    if (!prepared || !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) ||
        !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()))
        return false;
    const double width = route == Route::Composite ? 2 : 1;
    return LiteralRect(session.Selection(), 1, width, route == Route::Composite) && PanelUnchanged(session, *before) &&
           LiteralPixels(session.ActiveLayer()->asset->image, kLayerPixels) &&
           LiteralPixels(fixture->composite, kTwoRedComposite) && session.history.UndoCount() == 1 &&
           session.history.UndoName() == L"Magic Wand";
}

bool StrictRefusal(Route route)
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Levels, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto before = std::make_unique<Snapshot>(session);
    const auto strictPlan = route == Route::Layer ? session.PlanWandLayerSelectionMemory(kPoint, Settings(route))
                                                  : session.PlanWandCompositeSelectionMemory(kPoint, Settings(route));
    auto strictCapture = route == Route::Layer
                             ? session.PrepareWandLayerSelectionInput(kPoint, Settings(route), false)
                             : session.PrepareWandCompositeSelectionInput(kPoint, Settings(route), true);
    auto panelCapture = Capture(session, route);
    return !session.CanApplyHistoryRestore(session.CaptureHistoryRestoreAuthority()) && !strictPlan && !strictCapture &&
           Planned(session, route, SelectionMode::New) && panelCapture && panelCapture->ReadbackDocument() != nullptr &&
           StateUnchanged(session, *before);
}

bool MoveConstructSettingsStale()
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Levels, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto input = Capture(session, Route::Layer);
    if (!input)
        return false;
    auto moved = std::make_unique<WandLayerSelectionInput>(std::move(input.value()));
    const bool transferred = input->ReadbackDocument() == nullptr && moved->ReadbackDocument() != nullptr;
    auto prepared = Prepare(std::move(*moved), *fixture, Route::Layer);
    const bool current = prepared && session.IsPreparedWandLayerSelectionCurrent(prepared.value());
    const auto opening = session.PendingColorEdit()->openingSequence;
    const auto sequence = session.ColorEditSequence();
    auto changed = std::make_unique<FilterSettings>(session.PendingColorEdit()->settings);
    changed->levels.ranges[0].gamma = 1.75;
    session.UpdateColorEdit(*changed, true);
    auto beforeApply = std::make_unique<Snapshot>(session);
    return transferred && current && session.PendingColorEdit()->openingSequence == opening &&
           session.ColorEditSequence() != sequence &&
           session.PendingColorEdit()->settings.levels.ranges[0].gamma == 1.75 &&
           !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
           !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && StateUnchanged(session, *beforeApply);
}

bool MoveAssignEqualReopenStale()
{
    auto fixture = std::make_unique<Fixture>(FilterKind::HueSaturation, SelectionMode::New);
    auto other = std::make_unique<Fixture>(FilterKind::Exposure, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready || !other->ready)
        return false;
    auto input = Capture(session, Route::Composite);
    auto replaced = Capture(*other->session, Route::Layer);
    if (!input || !replaced)
        return false;
    auto assigned = std::make_unique<WandLayerSelectionInput>(std::move(replaced.value()));
    *assigned = std::move(input.value());
    const bool transferred = input->ReadbackDocument() == nullptr && replaced->ReadbackDocument() == nullptr &&
                             assigned->ReadbackDocument() != nullptr;
    auto prepared = Prepare(std::move(*assigned), *fixture, Route::Composite);
    const bool current = prepared && session.IsPreparedWandLayerSelectionCurrent(prepared.value());
    auto oldPanel = std::make_unique<ColorEdit>(session.PendingColorEdit().value());
    session.CancelColorEdit();
    session.BeginColorEdit(FilterKind::HueSaturation, true);
    if (!session.HasColorEdit(FilterKind::HueSaturation))
        return false;
    ConfigurePanel(session, FilterKind::HueSaturation);
    const bool equalReopen = session.PendingColorEdit()->settings == oldPanel->settings &&
                             session.PendingColorEdit()->original == oldPanel->original &&
                             session.PendingColorEdit()->selection == oldPanel->selection &&
                             session.PendingColorEdit()->openingSequence != oldPanel->openingSequence;
    auto beforeApply = std::make_unique<Snapshot>(session);
    return transferred && current && equalReopen && !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
           !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && StateUnchanged(session, *beforeApply);
}

enum class Stale
{
    Source,
    Selection,
    Active,
    Mask,
    Crop,
};

bool StaleWitness(Stale witness, Route route)
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Exposure, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto input = Capture(session, route);
    if (!input)
        return false;
    auto prepared = Prepare(std::move(input.value()), *fixture, route);
    if (!prepared || !session.IsPreparedWandLayerSelectionCurrent(prepared.value()))
        return false;
    bool changed = false;
    switch (witness)
    {
    case Stale::Source:
        EditLayerRecordsForTest(session,
                                [](DocumentFixture& document)
                                {
                                    document.layers[0].asset->image = Raster::Make(
                                        4, 4, RasterFormat::Bgra8888, kChangedSource.data(), kChangedSource.size());
                                });
        changed = LiteralPixels(session.ActiveLayer()->asset->image, kChangedSource);
        break;
    case Stale::Selection:
        session.SetSelection(DocumentSelection{true, {SelectionLoop{{3, 0}, {4, 0}, {4, 1}, {3, 1}}}},
                             L"Later selection");
        changed = session.Selection() == DocumentSelection{true, {SelectionLoop{{3, 0}, {4, 0}, {4, 1}, {3, 1}}}};
        break;
    case Stale::Active:
        session.SelectLayer(fixture->other);
        changed = session.ActiveLayerID() == fixture->other;
        break;
    case Stale::Mask:
        session.SelectLayerTarget(fixture->active, true);
        changed = session.IsMaskSelected();
        break;
    case Stale::Crop:
        session.SetCropFrame(CropRect{0, 0, 3, 3});
        changed = session.CropFrame() == CropRect{0, 0, 3, 3};
        break;
    }
    auto beforeApply = std::make_unique<Snapshot>(session);
    return changed && session.HasColorEdit(FilterKind::Exposure) &&
           !session.IsPreparedWandLayerSelectionCurrent(prepared.value()) &&
           !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && StateUnchanged(session, *beforeApply);
}

bool NoPanelRefusal()
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Levels, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto input = Capture(session, Route::Layer);
    if (!input)
        return false;
    auto prepared = Prepare(std::move(input.value()), *fixture, Route::Layer);
    if (!prepared || !session.IsPreparedWandLayerSelectionCurrent(prepared.value()))
        return false;
    session.CancelColorEdit();
    auto before = std::make_unique<Snapshot>(session);
    return !session.HasColorEdit() && !Capture(session, Route::Layer) && !Capture(session, Route::Composite) &&
           !session.PlanPanelWandLayerSelectionMemory(kPoint, Settings(Route::Layer)) &&
           !session.PlanPanelWandCompositeSelectionMemory(kPoint, Settings(Route::Composite)) &&
           !session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value()) && StateUnchanged(session, *before);
}

bool NestedHistory(Route route)
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Levels, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    session.BeginEdit(L"Panel Wand retained outer");
    auto before = std::make_unique<Snapshot>(session);
    auto input = Capture(session, route);
    if (!input)
        return false;
    auto prepared = Prepare(std::move(input.value()), *fixture, route);
    const bool applied = prepared && session.ApplyPreparedWandLayerSelectionIfCurrent(prepared.value());
    const bool nested = applied && LiteralRect(session.Selection(), 1, 1, route == Route::Composite) &&
                        session.history.CaptureState().Depth() == 1 && session.history.UndoCount() == 0 &&
                        PanelUnchanged(session, *before);
    session.EndEdit();
    return nested && session.history.CaptureState().Depth() == 0 && session.history.UndoCount() == 1 &&
           session.history.UndoName() == L"Panel Wand retained outer" && PanelUnchanged(session, *before) &&
           LiteralPixels(session.PendingColorEdit()->original, kLayerPixels);
}

bool CancelledPreparation(Route route)
{
    auto fixture = std::make_unique<Fixture>(FilterKind::HueSaturation, SelectionMode::Add);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto before = std::make_unique<Snapshot>(session);
    auto input = Capture(session, route, SelectionMode::Add);
    if (!input)
        return false;
    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    WandFailure failure = WandFailure::None;
    auto prepared = Prepare(std::move(input.value()), *fixture, route, &failure, cancellation.Token());
    return !prepared && failure == WandFailure::Cancelled && StateUnchanged(session, *before);
}

bool UnresolvedTransformRefusal()
{
    // Public panel opening deliberately refuses an unresolved deferred transform. Do not fabricate
    // an impossible panel/transform overlap or weaken this prerequisite just to reach a test state.
    auto fixture = std::make_unique<Fixture>(FilterKind::Levels, SelectionMode::New, false);
    auto& session = *fixture->session;
    if (!fixture->ready || !session.BeginDeferredTransform(true) || !session.PendingTransformEdit())
        return false;
    auto before = std::make_unique<Snapshot>(session);
    session.BeginColorEdit(FilterKind::Levels, true);
    return !session.HasColorEdit() && session.PendingTransformEdit().has_value() && !Capture(session, Route::Layer) &&
           !Capture(session, Route::Composite) &&
           !session.PlanPanelWandLayerSelectionMemory(kPoint, Settings(Route::Layer)) &&
           !session.PlanPanelWandCompositeSelectionMemory(kPoint, Settings(Route::Composite)) &&
           StateUnchanged(session, *before);
}

bool InvalidModeRefusal()
{
    auto fixture = std::make_unique<Fixture>(FilterKind::Exposure, SelectionMode::New);
    auto& session = *fixture->session;
    if (!fixture->ready)
        return false;
    auto before = std::make_unique<Snapshot>(session);
    constexpr auto invalid = static_cast<SelectionMode>(255);
    return !Capture(session, Route::Layer, invalid) && !Capture(session, Route::Composite, invalid) &&
           !session.PlanPanelWandLayerSelectionMemory(kPoint, Settings(Route::Layer), invalid) &&
           !session.PlanPanelWandCompositeSelectionMemory(kPoint, Settings(Route::Composite), invalid) &&
           StateUnchanged(session, *before);
}
} // namespace

int RunPanelWandTests()
{
    gAssertions = 0;
    gFailures = 0;
    for (const auto& test : kGeometryCases)
        Expect(Geometry(test), test.name);
    Expect(RouteDifference(Route::Layer), "route.layer-ignores-two-pixel-composite-keeps-panel");
    Expect(RouteDifference(Route::Composite), "route.composite-selects-two-pixels-keeps-panel");
    Expect(StrictRefusal(Route::Layer), "strict.layer-refuses-panel-opt-in-plan-and-capture-accept");
    Expect(StrictRefusal(Route::Composite), "strict.composite-refuses-panel-opt-in-plan-and-capture-accept");
    Expect(MoveConstructSettingsStale(), "move.construct-keeps-settings-sequence-witness");
    Expect(MoveAssignEqualReopenStale(), "move.assign-keeps-equal-reopen-witness");
    Expect(StaleWitness(Stale::Source, Route::Layer), "stale.source-refuses-without-overwrite");
    Expect(StaleWitness(Stale::Selection, Route::Composite), "stale.selection-refuses-without-overwrite");
    Expect(StaleWitness(Stale::Active, Route::Layer), "stale.active-refuses-without-overwrite");
    Expect(StaleWitness(Stale::Mask, Route::Composite), "stale.mask-refuses-without-overwrite");
    Expect(StaleWitness(Stale::Crop, Route::Layer), "stale.crop-refuses-without-overwrite");
    Expect(NoPanelRefusal(), "panel.absent-refuses-capture-plan-and-old-publication");
    Expect(NestedHistory(Route::Layer), "history.layer-retains-outer-depth-and-title");
    Expect(NestedHistory(Route::Composite), "history.composite-retains-outer-depth-and-title");
    Expect(CancelledPreparation(Route::Layer), "cancel.layer-preparation-preserves-live-state");
    Expect(CancelledPreparation(Route::Composite), "cancel.composite-preparation-preserves-live-state");
    Expect(UnresolvedTransformRefusal(), "guard.unresolved-transform-refuses-without-settlement");
    Expect(InvalidModeRefusal(), "guard.invalid-mode-refuses-without-mutation");
    std::printf("%s panel-wand-core.summary: %d assertions, %d failures\n", gFailures == 0 ? "PASS" : "FAIL",
                gAssertions, gFailures);
    return gFailures;
}

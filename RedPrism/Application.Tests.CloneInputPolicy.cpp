#include "Application.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Runtime.Tests.CloneReadbackTestGate.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
using Pixel = std::array<std::uint8_t, 4>; // BGRA

constexpr Pixel kRed{0, 0, 255, 255};
constexpr Pixel kGreen{0, 255, 0, 255};
constexpr Pixel kBlue{255, 0, 0, 255};

void SetSyntheticKey(int key, bool held) noexcept
{
    std::array<BYTE, 256> keys{};
    (void)KeyboardState::Read(keys);
    keys[static_cast<std::size_t>(key) & 0xFFu] = held ? 0x80u : 0u;
    KeyboardState::Write(keys);
}
} // namespace

int Application::RunCloneInputPolicySelfTest() noexcept
{
    const KeyboardState::ScopedSynthetic keyboard;
    const auto originalTool = _tool;
    const auto originalBrushSettings = _session.brushSettings;
    const auto originalCloneSettings = _session.cloneSettings;
    const auto originalCloneSource = _session.cloneSource;
    const auto originalViewport = _viewport;
    const bool originalSpacePan = _spacePan;
    std::array<BYTE, 256> originalKeyboard{};
    const bool originalKeyboardCaptured = KeyboardState::Read(originalKeyboard);
    int failures = 0;
    int assertions = 0;
    const auto expect = [&failures, &assertions](bool condition, const char* name) noexcept
    {
        ++assertions;
        std::printf("%s clone-input-policy.%s\n", condition ? "PASS" : "FAIL", name);
        failures += condition ? 0 : 1;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_cloneReadbackTestGate)
                (void)SetEvent(_cloneReadbackTestGate->release.get());
            CancelHostContinuations();
            if (!settle())
                std::_Exit(5);
            if (_session.PendingBrush())
            {
                _session.CancelBrush();
                _composition.EndStroke();
                _previewSource.reset();
            }
            _cloneReadbackTestGate.reset();
            _canvasCloneSample.reset();
            _tool = originalTool;
            _session.brushSettings = originalBrushSettings;
            _session.cloneSettings = originalCloneSettings;
            _session.cloneSource = originalCloneSource;
            _viewport = originalViewport;
            _spacePan = originalSpacePan;
            SyncChrome();
            if (originalKeyboardCaptured)
                KeyboardState::Write(originalKeyboard);
        });

    const auto installFixture = [this, &settle]() noexcept
    {
        if (!settle())
            return false;
        constexpr int width = 16;
        constexpr int height = 12;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const Pixel color = x < 5 ? kRed : x < 8 ? kGreen : kBlue;
                const std::size_t offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
                std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
        }

        DocumentFixture fixture;
        fixture.id = Guid::Generate();
        fixture.width = width;
        fixture.height = height;
        ImageLayer source = ImageLayer::Blank(L"Policy source", {width, height});
        source.transform.sampling = LayerSampling::Nearest;
        source.asset = RasterAsset{
            Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size()), {}, source.name};
        fixture.layers.push_back(std::move(source));
        ImageLayer target = ImageLayer::Blank(L"Policy target", {width, height});
        target.transform.sampling = LayerSampling::Nearest;
        std::vector<std::uint8_t> transparent(static_cast<std::size_t>(width * height * 4));
        target.asset =
            RasterAsset{Raster::Make(width, height, RasterFormat::Bgra8888, transparent.data(), transparent.size()),
                        {},
                        target.name};
        const Guid targetId = target.id;
        fixture.layers.push_back(std::move(target));
        const auto fixtureId = fixture.id;
        ReplaceDocument(std::move(fixture).Seal(), targetId, {});
        std::fprintf(stderr, "INFO clone-policy-install active=%u mask=%u host-busy=%u queued=%zu keyboard=%u\n",
                     _session.ActiveLayerID() == std::optional(targetId) ? 1u : 0u, _session.IsMaskSelected() ? 1u : 0u,
                     _historyWork.Busy() ? 1u : 0u, _hostRequests.Size(),
                     KeyboardState::Detail::g_synthetic.active ? 1u : 0u);

        if (!_session.Document() || _session.Document()->id != fixtureId ||
            _session.ActiveLayerID() != std::optional(targetId) || _session.IsMaskSelected())
            return false;
        _tool = EditorTool::Clone;
        _spacePan = false;
        _drag = PointerDrag{};
        _session.brushSettings = BrushSettings{3, 1, 0, 0, 0, 1};
        _session.cloneSettings = CloneSettings{false, true}; // Unaligned, Sample All Layers.
        _session.SetCloneSource({4, 6});
        _viewport.zoom = 1;
        _viewport.pan = {};
        _viewport.followsFit = false;
        DocumentChanged();
        SyncChrome();
        // WM_* coordinates are whole client pixels. Align the document grid after layout so literal source,
        // endpoint and offset expectations do not acquire a half pixel when the canvas hole has an odd extent.
        const auto hole = _chrome->HolePixels();
        const auto origin = _viewport.ViewFromDocument({}, DocumentSize());
        _viewport.pan.x += std::round(hole.left + origin.x) - (hole.left + origin.x);
        _viewport.pan.y += std::round(hole.top + origin.y) - (hole.top + origin.y);
        return true;
    };
    const auto pointer = [](Application& owner, UINT message, WPARAM modifiers, Vec2 pixel) noexcept
    {
        const auto hole = owner._chrome->HolePixels();
        const auto view = owner._viewport.ViewFromDocument(pixel, owner.DocumentSize());
        owner.OnPointer(message, modifiers,
                        MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                                   static_cast<int>(std::lround(hole.top + view.y))));
    };
    const auto activePixel = [this](int x, int y) noexcept
    {
        Pixel pixel{};
        const ImageLayer* active = _session.ActiveLayer();
        if (active && active->asset)
        {
            const int localX = static_cast<int>(std::lround(static_cast<double>(x) - active->transform.origin.x));
            const int localY = static_cast<int>(std::lround(static_cast<double>(y) - active->transform.origin.y));
            active->asset->image.ReadRegion(localX, localY, 1, 1, pixel.data(), 4, 0);
        }
        return pixel;
    };

    // Alt on Clone Stamp changes the source only, including when Sample All Layers is selected.
    const bool altInstalled = installFixture();
    expect(altInstalled, "alt-fixture-installed-before-pointer");
    if (!altInstalled)
        return failures;
    _cloneReadbackTestGate = std::make_shared<CloneReadbackTestGate>();
    const auto altHistoryCount = _session.history.UndoCount();
    const auto altHistoryRevision = _session.history.CurrentRevision();
    SetSyntheticKey(VK_MENU, true);
    pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {3, 4});
    SetSyntheticKey(VK_MENU, false);
    pointer(*this, WM_LBUTTONUP, 0, {3, 4});
    expect(_session.cloneSource == std::optional<Vec2>{Vec2{3, 4}} && !_session.PendingBrush(),
           "alt-pointer-updates-source-without-opening-stroke");
    expect(_session.history.UndoCount() == altHistoryCount &&
               _session.history.CurrentRevision() == altHistoryRevision && !_utilityTicket && !_utilityStamp &&
               _cloneReadbackTestGate->captures.load() == 0,
           "alt-source-only-has-no-readback-or-history");

    // A mask target cannot be cloned. Prime and snapshot a real image and aligned offset before the refused press.
    const bool maskInstalled = installFixture();
    expect(maskInstalled, "mask-fixture-installed-before-pointer");
    if (!maskInstalled)
        return failures;
    _cloneReadbackTestGate = std::make_shared<CloneReadbackTestGate>();
    pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
    pointer(*this, WM_LBUTTONUP, 0, {12, 6});
    expect(settle(), "mask-case-prior-host-clone-settles");
    _session.cloneSettings.aligned = true;
    _session.AddLayerMask(true);
    const auto maskLayer = _session.ActiveLayerID();
    if (maskLayer)
        _session.SelectLayerTarget(maskLayer.value(), true);
    const ImageLayer* const maskTargetBefore = _session.ActiveLayer();
    const bool hadMaskTargetImage = maskTargetBefore && maskTargetBefore->asset.has_value();
    const void* const maskImageIdentity = hadMaskTargetImage ? maskTargetBefore->asset->image.Identity() : nullptr;
    const auto maskCloneOffset = _session.CloneStrokeOffset({14, 6});
    const auto maskHistoryCount = _session.history.UndoCount();
    const auto maskHistoryRevision = _session.history.CurrentRevision();
    const auto maskCaptures = _cloneReadbackTestGate->captures.load();
    pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
    pointer(*this, WM_LBUTTONUP, 0, {12, 6});
    expect(_session.IsMaskSelected() && !_session.PendingBrush(), "mask-target-clone-is-a-no-op");
    const ImageLayer* maskTargetAfter = _session.ActiveLayer();
    expect(hadMaskTargetImage && maskCloneOffset == std::optional<Vec2>{Vec2{-8, 0}} && maskTargetAfter &&
               maskTargetAfter->asset && maskTargetAfter->asset->image.Identity() == maskImageIdentity &&
               _session.CloneStrokeOffset({14, 6}) == maskCloneOffset &&
               _session.history.UndoCount() == maskHistoryCount &&
               _session.history.CurrentRevision() == maskHistoryRevision && !_utilityTicket && !_utilityStamp &&
               _cloneReadbackTestGate->captures.load() == maskCaptures,
           "mask-refusal-preserves-image-identity-clone-offset-and-capture-utility-history");

    // Seed the previous stroke endpoint through the real host path, then Shift-click elsewhere. The captured
    // offset must be computed from that prior endpoint (12,6), not the new click (14,6).
    const bool lineInstalled = installFixture();
    expect(lineInstalled, "line-fixture-installed-before-pointer");
    if (!lineInstalled)
        return failures;
    _session.SetCloneSource({4, 6});
    _cloneReadbackTestGate = std::make_shared<CloneReadbackTestGate>();
    pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON, {12, 6});
    pointer(*this, WM_LBUTTONUP, 0, {12, 6});
    expect(settle(), "prior-host-clone-settles");
    const auto endpoint = _session.ShiftLineStart();
    const auto priorPixel = activePixel(12, 6);
    std::fprintf(stderr,
                 "INFO clone-policy-prior endpoint=%u %.6f,%.6f pixel=%u,%u,%u,%u history=%zu "
                 "named=%u captures=%u outcome=%u error=%08X mask=%u keys=%u,%u,%u zoom=%.6f pan=%.6f,%.6f\n",
                 endpoint ? 1u : 0u, endpoint ? endpoint->x : 0.0, endpoint ? endpoint->y : 0.0,
                 static_cast<unsigned int>(priorPixel[0]), static_cast<unsigned int>(priorPixel[1]),
                 static_cast<unsigned int>(priorPixel[2]), static_cast<unsigned int>(priorPixel[3]),
                 static_cast<std::size_t>(_session.history.UndoCount()),
                 _session.history.UndoName() == L"Clone Stamp" ? 1u : 0u, _cloneReadbackTestGate->captures.load(),
                 static_cast<unsigned int>(_canvasBrushReceipt.Read().outcome),
                 static_cast<unsigned int>(_canvasBrushReceipt.Read().error), _session.IsMaskSelected() ? 1u : 0u,
                 KeyboardState::Held(VK_SHIFT) ? 1u : 0u, KeyboardState::Held(VK_CONTROL) ? 1u : 0u,
                 KeyboardState::Held(VK_MENU) ? 1u : 0u, _viewport.zoom, _viewport.pan.x, _viewport.pan.y);
    expect(_session.ShiftLineStart() == std::optional<Vec2>{Vec2{12, 6}} && activePixel(12, 6) == kRed &&
               _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Clone Stamp",
           "prior-host-stroke-establishes-literal-red-endpoint-and-one-entry");

    SetSyntheticKey(VK_SHIFT, true);
    pointer(*this, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, {14, 6});
    const auto* captured = _canvasBrushInput.CapturedContext(_canvasBrushInput.Opening());
    const bool capturedPreviousOffset = captured && captured->cloneOffset == std::optional<Vec2>{Vec2{-8, 0}};
    pointer(*this, WM_LBUTTONUP, MK_SHIFT, {14, 6});
    SetSyntheticKey(VK_SHIFT, false);
    expect(capturedPreviousOffset, "shift-down-captures-minus-eight-offset-from-previous-stroke-end");
    expect(settle(), "shift-host-clone-settles");
    expect(activePixel(12, 6) == kRed && activePixel(14, 6) == kGreen && _session.history.UndoCount() == 2 &&
               _session.history.UndoName() == L"Clone Stamp",
           "shift-clone-uses-previous-endpoint-source-pixel-and-publishes-one-entry");

    OnCommand(ChromeHost::kCommandUndo);
    expect(settle(), "shift-clone-undo-settles");
    expect(activePixel(12, 6) == kRed && activePixel(14, 6) == Pixel{} && _session.history.UndoCount() == 1 &&
               _session.history.UndoName() == L"Clone Stamp" && _session.history.RedoName() == L"Clone Stamp",
           "shift-undo-restores-literal-red-prefix-and-transparent-endpoint");
    OnCommand(ChromeHost::kCommandRedo);
    expect(settle(), "shift-clone-redo-settles");
    expect(activePixel(12, 6) == kRed && activePixel(14, 6) == kGreen && _session.history.UndoCount() == 2 &&
               _session.history.UndoName() == L"Clone Stamp" && _session.history.RedoName().empty(),
           "shift-redo-restores-literal-green-endpoint-and-second-clone-entry");

    // This direct Application host-method probe intentionally passes no composite. It is expected to refuse after
    // the production synchronous fallback is removed; DocumentSession direct-core tests remain unchanged.
    _session.cloneSettings.aligned = true;
    const ImageLayer* const missingTargetBefore = _session.ActiveLayer();
    const bool hadMissingTargetImage = missingTargetBefore && missingTargetBefore->asset.has_value();
    const void* const missingImageIdentity =
        hadMissingTargetImage ? missingTargetBefore->asset->image.Identity() : nullptr;
    const auto missingCloneOffset = _session.CloneStrokeOffset({14, 6});
    const auto missingHistoryCount = _session.history.UndoCount();
    const auto missingHistoryRevision = _session.history.CurrentRevision();
    const Pixel missingFirst = activePixel(12, 6);
    const Pixel missingSecond = activePixel(14, 6);
    BeginBrushStrokeAt({12, 6}, 0, BrushKind::Clone, nullptr, nullptr);
    const ImageLayer* missingTargetAfter = _session.ActiveLayer();
    expect(!_session.PendingBrush() && hadMissingTargetImage &&
               missingCloneOffset == std::optional<Vec2>{Vec2{-8, 0}} && missingTargetAfter &&
               missingTargetAfter->asset && missingTargetAfter->asset->image.Identity() == missingImageIdentity &&
               _session.CloneStrokeOffset({14, 6}) == missingCloneOffset && activePixel(12, 6) == missingFirst &&
               activePixel(14, 6) == missingSecond && !_utilityTicket && !_utilityStamp &&
               _cloneReadbackTestGate->captures.load() == 2 && _session.history.UndoCount() == missingHistoryCount &&
               _session.history.CurrentRevision() == missingHistoryRevision,
           "missing-composite-refusal-preserves-image-offset-pixels-and-history-without-new-capture");
    if (_session.PendingBrush())
    {
        _session.CancelBrush();
        _composition.EndStroke();
        _previewSource.reset();
    }

    std::printf("%s clone-input-policy.summary assertions=%d failures=%d\n", failures == 0 ? "PASS" : "FAIL",
                assertions, failures);
    return failures;
}

#include "AdapterSelection.h"
#include "AddressSanitizerProbe.h"
#include "CanvasViewport.h"
#include "CommandLine.h"
#include "Document.h"
#include "EditorTool.h"
#include "FrameScheduler.h"
#include "Resources.h"
#include "TransformOverlay.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <dwrite.h>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#pragma comment(lib, "dwrite.lib")

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
    ++gFailures;
}

void TestAdapterSelection() noexcept
{
    constexpr LUID first{1, 0};
    constexpr LUID second{2, 0};
    const HMONITOR monitorA = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x100));
    const HMONITOR monitorB = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x200));
    const std::array<AdapterOutputRecord, 3> records{
        AdapterOutputRecord{first, monitorA, true},
        AdapterOutputRecord{second, monitorA, false},
        AdapterOutputRecord{first, monitorB, false},
    };

    Expect(SelectAdapterRecordForMonitor(records, nullptr) == SIZE_MAX, "adapter.null-monitor");
    Expect(SelectAdapterRecordForMonitor(records, monitorA) == 1, "adapter.skip-software");
    Expect(SelectAdapterRecordForMonitor(records, monitorB) == 2, "adapter.other-monitor");
    Expect(SelectAdapterRecordForMonitor(records, reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x300))) ==
               SIZE_MAX,
           "adapter.unknown-monitor");
    Expect(SameLuid(first, first) && !SameLuid(first, second), "adapter.luid");
}

void TestFrameScheduler() noexcept
{
    HostFrameState state{};
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.hidden-suspended");

    state.windowVisible = true;
    state.displayPoweredOn = true;
    state.rendererSuspended = false;
    state.frameInvalidated = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::Render, "frame.dirty");

    state.frameInvalidated = false;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.idle");

    state.rendererOccluded = true;
    state.occlusionStatusChanged = false;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.occluded-wait");

    state.occlusionStatusChanged = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::ProbeOcclusion, "frame.occluded-probe");

    state.rendererOccluded = false;
    state.occlusionStatusChanged = false;
    state.frameInvalidated = false;
    state.chromeAnimating = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::Render, "frame.chrome-animating");

    state.rendererSuspended = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.suspended");

    state.rendererSuspended = false;
    state.displayPoweredOn = false;
    state.frameInvalidated = true;
    Expect(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, "frame.display-off");
}

void TestCommandLine() noexcept
{
    Expect(RedPrismIsHelpArgument(L"--help") && RedPrismIsHelpArgument(L"/?"), "cli.help-aliases");
    Expect(RedPrismFindSwitch(L"--warp") != nullptr, "cli.warp");
    Expect(RedPrismFindSwitch(L"--self-test") != nullptr, "cli.self-test");
    Expect(RedPrismFindSwitch(L"--screenshot") != nullptr, "cli.screenshot");
    Expect(RedPrismFindSwitch(L"--dpi") != nullptr, "cli.dpi");
    Expect(RedPrismFindSwitch(L"--demo") != nullptr, "cli.demo");
    Expect(RedPrismFindSwitch(L"--no-such") == nullptr, "cli.unknown-switch");

    wchar_t unknown[] = L"--no-such";
    wchar_t* args[] = {const_cast<wchar_t*>(L"RedPrism.exe"), unknown};
    Expect(std::wstring_view{RedPrismFindUnknownArgument(args, 2)} == L"--no-such", "cli.unknown-scan");

    wchar_t screenshot[] = L"--screenshot";
    wchar_t png[] = L"overlay.png";
    wchar_t* shotArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png};
    Expect(RedPrismFindUnknownArgument(shotArgs, 3) == nullptr, "cli.screenshot-value");

    RedPrismCommandLine parsed{};
    const wchar_t* error = nullptr;
    Expect(RedPrismParseCommandLine(shotArgs, 3, parsed, &error) && parsed.screenshotPath == std::wstring_view{png},
           "cli.screenshot-parse");

    wchar_t dpiSwitch[] = L"--dpi";
    wchar_t dpiValue[] = L"144";
    wchar_t* dpiArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png, dpiSwitch, dpiValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(dpiArgs, 5, parsed, &error) && parsed.screenshotDpi == 144, "cli.dpi-parse");

    wchar_t badDpi[] = L"120";
    wchar_t* badArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), dpiSwitch, badDpi};
    parsed = {};
    Expect(!RedPrismParseCommandLine(badArgs, 3, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"120",
           "cli.dpi-rejects-other-values");

    wchar_t demoSwitch[] = L"--demo";
    wchar_t demoValue[] = L"transform";
    wchar_t* demoArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), screenshot, png, demoSwitch, demoValue};
    parsed = {};
    Expect(RedPrismParseCommandLine(demoArgs, 5, parsed, &error) && parsed.demo == std::wstring_view{demoValue},
           "cli.demo-parse");
    wchar_t badDemo[] = L"brush";
    wchar_t* badDemoArgs[] = {const_cast<wchar_t*>(L"RedPrism.exe"), demoSwitch, badDemo};
    parsed = {};
    Expect(!RedPrismParseCommandLine(badDemoArgs, 3, parsed, &error) && error != nullptr &&
               std::wstring_view{error} == L"brush",
           "cli.demo-rejects-unknown");

    const std::wstring help = RedPrismFormatCommandLineHelp();
    Expect(help.find(L"--warp") != std::wstring::npos && help.find(L"--self-test") != std::wstring::npos &&
               help.find(L"--screenshot") != std::wstring::npos && help.find(L"--dpi") != std::wstring::npos,
           "cli.help-text");
}

void TestEditorTools() noexcept
{
    std::set<wchar_t> keys;
    std::set<wchar_t> glyphs;
    std::set<UINT> names;
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        const EditorTool tool = EditorToolAt(index);
        keys.insert(EditorToolKey(tool));
        glyphs.insert(EditorToolGlyph(tool));
        names.insert(EditorToolNameId(tool));
        Expect(EditorToolFromKey(EditorToolKey(tool)) == tool, "tools.key-round-trip");
        Expect(EditorToolIndex(tool) == index, "tools.index-round-trip");
    }
    Expect(keys.size() == kEditorToolCount, "tools.unique-keys");
    Expect(glyphs.size() == kEditorToolCount, "tools.unique-glyphs");
    Expect(names.size() == kEditorToolCount, "tools.unique-names");
    Expect(EditorToolFromKey(L'Q') == EditorTool::Count && EditorToolFromKey(L'V') == EditorTool::Move,
           "tools.unknown-key");
    Expect(EditorToolAt(999) == EditorTool::Move, "tools.index-clamp");
}

// Every rail glyph must exist in the installed Segoe Fluent Icons font; a missing glyph renders as a box.
void TestRailGlyphsExistInFluentFont() noexcept
{
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.put()));
    if (FAILED(result))
    {
        Expect(false, "glyphs.dwrite-factory");
        return;
    }
    wil::com_ptr_nothrow<IDWriteFontCollection> fonts;
    result = factory->GetSystemFontCollection(fonts.put(), FALSE);
    if (FAILED(result))
    {
        Expect(false, "glyphs.system-fonts");
        return;
    }
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    result = fonts->FindFamilyName(L"Segoe Fluent Icons", &familyIndex, &exists);
    if (FAILED(result) || !exists)
    {
        std::printf("SKIP glyphs.fluent-family (Segoe Fluent Icons is not installed on this host)\n");
        return;
    }
    wil::com_ptr_nothrow<IDWriteFontFamily> family;
    wil::com_ptr_nothrow<IDWriteFont> font;
    if (FAILED(fonts->GetFontFamily(familyIndex, family.put())) || FAILED(family->GetFont(0, font.put())))
    {
        Expect(false, "glyphs.fluent-font");
        return;
    }
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        BOOL has = FALSE;
        const wchar_t glyph = EditorToolGlyph(EditorToolAt(index));
        const bool ok = SUCCEEDED(font->HasCharacter(static_cast<UINT32>(glyph), &has)) && has;
        if (!ok)
        {
            std::printf("     missing glyph U+%04X for tool %zu\n", static_cast<unsigned>(glyph), index);
        }
        Expect(ok, "glyphs.fluent-has-tool-glyph");
    }
}

[[nodiscard]] bool PlaceholdersArePositional(std::wstring_view text) noexcept
{
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != L'{')
        {
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == L'{')
        {
            ++index;
            continue;
        }
        if (index + 1 >= text.size() || text[index + 1] < L'0' || text[index + 1] > L'9')
        {
            return false;
        }
    }
    return true;
}

// The STRINGTABLE in RedPrism.exe (next to this test) must define every id the chrome uses, with positional
// placeholders only. Loading the executable as a data file never runs its code.
void TestStringTable() noexcept
{
    wchar_t module[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0)
    {
        Expect(false, "strings.module-path");
        return;
    }
    std::wstring path(module);
    const std::size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1)) + L"RedPrism.exe";
    wil::unique_hmodule exe{
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    if (!exe)
    {
        Expect(false, "strings.load-redprism-exe");
        return;
    }
    constexpr std::array<UINT, 119> kIds{IDS_APP_TITLE,
                                         IDS_UNTITLED,
                                         IDS_DIRTY_MARK,
                                         IDS_STATUS_ZOOM,
                                         IDS_STATUS_SIZE,
                                         IDS_STATUS_TOOL,
                                         IDS_STATUS_NO_DOCUMENT,
                                         IDS_LAYERS_TITLE,
                                         IDS_LAYER_HIDDEN_SUFFIX,

                                         IDS_LAYER_OPACITY,

                                         IDS_PALETTE_FOREGROUND,

                                         IDS_PALETTE_BACKGROUND,

                                         IDS_PICKER_NEW,

                                         IDS_PICKER_CURRENT,

                                         IDS_PICKER_HEX,

                                         IDS_PICKER_RED,

                                         IDS_PICKER_GREEN,

                                         IDS_PICKER_BLUE,

                                         IDS_PICKER_OK,

                                         IDS_PICKER_CANCEL,
                                         IDS_MENU_FILE,
                                         IDS_MENU_EDIT,
                                         IDS_MENU_IMAGE,
                                         IDS_MENU_LAYER,
                                         IDS_MENU_SELECT,
                                         IDS_MENU_FILTER,
                                         IDS_MENU_VIEW,
                                         IDS_MENU_HELP,
                                         IDS_CMD_NEW,
                                         IDS_CMD_OPEN,
                                         IDS_CMD_SAVE,
                                         IDS_CMD_SAVE_AS,
                                         IDS_CMD_EXPORT_PNG,
                                         IDS_CMD_EXPORT_JPEG,
                                         IDS_CMD_UNDO,
                                         IDS_CMD_REDO,
                                         IDS_CMD_NEW_LAYER,
                                         IDS_CMD_NEW_FOLDER,
                                         IDS_CMD_DELETE_LAYER,
                                         IDS_CMD_TOGGLE_LAYER_VISIBILITY,
                                         IDS_CMD_MOVE_OUT_OF_GROUP,
                                         IDS_CMD_ZOOM_FIT,
                                         IDS_CMD_ZOOM_ACTUAL,
                                         IDS_CMD_ZOOM_IN,
                                         IDS_CMD_ZOOM_OUT,
                                         IDS_CMD_ABOUT,
                                         IDS_CMD_EXIT,
                                         IDS_CMD_UNDO_NAMED,
                                         IDS_CMD_REDO_NAMED,
                                         IDS_CMD_FREE_TRANSFORM,
                                         IDS_CMD_FLIP_LAYER_HORIZONTAL,
                                         IDS_CMD_FLIP_LAYER_VERTICAL,
                                         IDS_CMD_FLIP_CANVAS_HORIZONTAL,
                                         IDS_CMD_FLIP_CANVAS_VERTICAL,
                                         IDS_TOOL_MOVE,
                                         IDS_TOOL_MARQUEE,
                                         IDS_TOOL_LASSO,
                                         IDS_TOOL_WAND,
                                         IDS_TOOL_CROP,
                                         IDS_TOOL_BRUSH,
                                         IDS_TOOL_ERASER,
                                         IDS_TOOL_HEAL,
                                         IDS_TOOL_CLONE,
                                         IDS_TOOL_SMEAR,
                                         IDS_TOOL_GRADIENT,
                                         IDS_TOOL_SHAPE,
                                         IDS_TOOL_EYEDROPPER,
                                         IDS_TOOL_HAND,
                                         IDS_TOOL_ZOOM,
                                         IDS_TOOL_IDLE,
                                         IDS_TOOL_TOOLTIP,
                                         IDS_ERROR_OPEN_PROJECT,
                                         IDS_ERROR_IMPORT_IMAGE,
                                         IDS_ERROR_SAVE_PROJECT,
                                         IDS_ERROR_FLATTEN,
                                         IDS_ERROR_READBACK,
                                         IDS_ERROR_EXPORT_JPEG,
                                         IDS_ERROR_EXPORT_PNG,
                                         IDS_ERROR_START_GENERIC,
                                         IDS_ERROR_START_WINDOW,
                                         IDS_ERROR_START_GRAPHICS,
                                         IDS_ERROR_UNSUPPORTED_VERSION,
                                         IDS_ERROR_TOO_LARGE,
                                         IDS_ERROR_MISSING_IMAGE,
                                         IDS_FILTER_PROJECT,
                                         IDS_FILTER_IMAGES,
                                         IDS_FILTER_ALL,
                                         IDS_FILTER_EXPLODED,
                                         IDS_FILTER_JPEG,
                                         IDS_FILTER_PNG,
                                         IDS_DEFAULT_PROJECT_NAME,
                                         IDS_DEFAULT_EXPORT_JPEG,
                                         IDS_DEFAULT_EXPORT_PNG,
                                         IDS_ABOUT_TEXT,
                                         IDS_COMMAND_LINE_CAPTION,

                                         IDS_UNIT_PERCENT,
                                         IDS_UNIT_DEGREES,
                                         IDS_TRANSFORM_TITLE,
                                         IDS_TRANSFORM_MASK_TITLE,
                                         IDS_TRANSFORM_AUTO_SELECT,
                                         IDS_TRANSFORM_AUTO_SELECT_TIP,
                                         IDS_TRANSFORM_SHOW_CONTROLS,
                                         IDS_TRANSFORM_SHOW_CONTROLS_TIP,
                                         IDS_TRANSFORM_X,
                                         IDS_TRANSFORM_Y,
                                         IDS_TRANSFORM_WIDTH,
                                         IDS_TRANSFORM_HEIGHT,
                                         IDS_TRANSFORM_LOCK_RATIO,
                                         IDS_TRANSFORM_SCALE,
                                         IDS_TRANSFORM_SCALE_TIP,
                                         IDS_TRANSFORM_ANGLE,
                                         IDS_TRANSFORM_SAMPLING_NEAREST,
                                         IDS_TRANSFORM_SAMPLING_SMOOTH,
                                         IDS_TRANSFORM_SAMPLING_HIGH,
                                         IDS_TRANSFORM_FLIP_HORIZONTAL,
                                         IDS_TRANSFORM_FLIP_VERTICAL,
                                         IDS_TRANSFORM_APPLY,
                                         IDS_TRANSFORM_CANCEL,
                                         IDS_TRANSFORM_LOCK};
    for (const UINT id : kIds)
    {
        const std::wstring text = LoadStringResource(exe.get(), id);
        if (text.empty())
        {
            std::printf("     missing string %u\n", id);
        }
        Expect(!text.empty(), "strings.defined");
        Expect(PlaceholdersArePositional(text), "strings.positional-placeholders");
    }
    Expect(LoadStringResource(exe.get(), 99).empty(), "strings.unused-id-empty");
    Expect(FormatStringResource(exe.get(), IDS_STATUS_SIZE, 1280, 800) == L"1280 × 800", "strings.format-positional");
    Expect(FormatStringResource(exe.get(), IDS_TOOL_TOOLTIP, std::wstring(L"Move"), std::wstring(L"V")) == L"Move (V)",
           "strings.format-two-arguments");
    Expect(StripAcceleratorColumn(LoadStringResource(exe.get(), IDS_CMD_SAVE_AS)) == L"Save As...",
           "strings.strip-accelerator-column");
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        Expect(!LoadStringResource(exe.get(), EditorToolNameId(EditorToolAt(index))).empty(), "strings.tool-names");
    }
}
} // namespace

// Ported from legacy TransformTests: hover regions and document mapping of the Move tool's box.
void TestTransformOverlayGeometry() noexcept
{
    const auto closeTo = [](Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 0.0001 && std::abs(a.y - b.y) < 0.0001; };
    CanvasViewport viewport;
    const Vec2 size{1000, 800};
    viewport.Resize(Vec2{1000, 800}, 1.0, size);
    viewport.SetZoom(1.0, viewport.Center(), size);
    LayerTransform transform;
    transform.origin = Vec2{100, 100};
    transform.size = Vec2{400, 300};
    transform.rotation = 37;
    const TransformOverlayGeometry geometry = TransformOverlayGeometry::FromTransform(transform, viewport, size);
    bool edges = true;
    constexpr std::array<std::array<int, 3>, 4> kEdges{{{0, 2, 1}, {2, 4, 3}, {4, 6, 5}, {6, 0, 7}}};
    for (const std::array<int, 3>& edge : kEdges)
    {
        const Vec2 a = geometry.handles[static_cast<size_t>(edge[0])];
        const Vec2 b = geometry.handles[static_cast<size_t>(edge[1])];
        const std::optional<TransformHit> hit = geometry.Hit(Vec2{a.x * 0.75 + b.x * 0.25, a.y * 0.75 + b.y * 0.25});
        edges = edges && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == edge[2];
    }
    Expect(edges, "overlay.rotated-edges-hit-their-handle");
    bool corners = true;
    for (const int index : {0, 2, 4, 6})
    {
        const std::optional<TransformHit> hit = geometry.Hit(geometry.handles[static_cast<size_t>(index)]);
        corners = corners && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == index;
    }
    Expect(corners, "overlay.corners-hit");
    const std::optional<TransformHit> rotate = geometry.Hit(geometry.rotationHandle);
    Expect(rotate.has_value() && rotate->mode == TransformDrag::Mode::Rotate, "overlay.rotation-knob-hit");
    Expect(!geometry.Hit(viewport.ViewFromDocument(transform.Center(), size)).has_value(), "overlay.center-misses");

    CanvasViewport zoomed;
    zoomed.Resize(Vec2{900, 600}, 2.0, size);
    zoomed.SetZoom(1.5, Vec2{300, 200}, size);
    zoomed.Translate(Vec2{57, -30});
    LayerTransform turned;
    turned.origin = Vec2{100, 200};
    turned.size = Vec2{100, 50};
    turned.rotation = 90;
    const TransformOverlayGeometry mapped = TransformOverlayGeometry::FromTransform(turned, zoomed, size);
    Expect(closeTo(zoomed.DocumentFromView(mapped.handles[4], size), turned.Point(Vec2{1, 1})),
           "overlay.document-mapping");
    Expect(turned.Contains(turned.Center()) && turned.Contains(Vec2{150, 265}) && !turned.Contains(Vec2{190, 225}),
           "overlay.rotated-contains");
    bool handles = true;
    for (int index = 0; index < 8; ++index)
    {
        const std::optional<TransformHit> hit = mapped.Hit(mapped.handles[static_cast<size_t>(index)]);
        handles = handles && hit.has_value() && hit->mode == TransformDrag::Mode::Resize && hit->handle == index;
    }
    Expect(handles, "overlay.zoomed-handles-hit");
    Expect(mapped.Hit(mapped.rotationHandle).has_value() &&
               mapped.Hit(mapped.rotationHandle)->mode == TransformDrag::Mode::Rotate,
           "overlay.zoomed-rotation-hit");

    LayerTransform upright;
    upright.size = Vec2{100, 100};
    const TransformOverlayGeometry axisAligned = TransformOverlayGeometry::FromTransform(upright, viewport, size);
    Expect(axisAligned.ResizeCursorDirection(3) == 0 && axisAligned.ResizeCursorDirection(1) == 2 &&
               axisAligned.ResizeCursorDirection(4) == 1 && axisAligned.ResizeCursorDirection(2) == 3,
           "overlay.resize-cursor-directions");
    const std::array<Vec2, 4> shape{Vec2{10, 10}, Vec2{60, 10}, Vec2{30, 30}, Vec2{10, 30}};
    const TransformOverlayGeometry distorted = TransformOverlayGeometry::FromCorners(shape, viewport, size);
    Expect(!distorted.showsRotation && closeTo(distorted.handles[3], viewport.ViewFromDocument(Vec2{45, 20}, size)),
           "overlay.distortion-midpoints");
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && std::wstring_view{argv[1]} == L"--asan-probe")
    {
        return RunAddressSanitizerProbe();
    }

    TestAdapterSelection();
    TestFrameScheduler();
    TestCommandLine();
    TestEditorTools();
    TestTransformOverlayGeometry();
    TestRailGlyphsExistInFluentFont();
    TestStringTable();
    if (gFailures != 0)
    {
        std::printf("%d test(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All host tests passed.\n");
    return 0;
}

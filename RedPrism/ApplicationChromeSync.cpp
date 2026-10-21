#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "KeyboardState.h"
#include "Resources.h"
#include "resource.h"

#include <cmath>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include <UIAutomation.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// What a pointer move costs the chrome (review fixes RF-1001): the self-test drives pans, wheel steps, drags and a
// brush through the window procedure's own handlers and counts what the chrome wrote (ChromeHost::SyncCounters),
// DxUi's preparations and the UI Automation snapshots published, with and without a listening client. The numbers
// print as CHROME SYNC lines (the time per input includes the chrome's share of the frame that follows it).

namespace
{
// Measurement lines go to stdout when the parent redirected it (and always to the debugger).
void ChromeSyncReport(const std::wstring& text) noexcept
{
    OutputDebugStringW(text.c_str());
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return;
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr,
                            nullptr) != size)
    {
        return;
    }
    DWORD written = 0;
    (void)WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

[[nodiscard]] double NowMicroseconds() noexcept
{
    static const double frequency = []
    {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1'000'000.0 / frequency;
}

// The value (UIA ValueValue) of the first element named `name` under `fragment`, walked as a client would.
[[nodiscard]] std::optional<std::wstring> NamedValue(IRawElementProviderFragment* fragment, const std::wstring& name,
                                                     int& budget) noexcept
{
    if (fragment == nullptr || budget <= 0)
    {
        return std::nullopt;
    }
    --budget;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    if (SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))) && simple)
    {
        VARIANT value;
        VariantInit(&value);
        const bool named = SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &value)) && value.vt == VT_BSTR &&
                           value.bstrVal != nullptr && name == value.bstrVal;
        (void)VariantClear(&value);
        if (named && SUCCEEDED(simple->GetPropertyValue(UIA_ValueValuePropertyId, &value)) && value.vt == VT_BSTR &&
            value.bstrVal != nullptr)
        {
            std::wstring text{value.bstrVal};
            (void)VariantClear(&value);
            return text;
        }
        (void)VariantClear(&value);
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(fragment->Navigate(NavigateDirection_FirstChild, child.put())))
    {
        return std::nullopt;
    }
    while (child && budget > 0)
    {
        if (std::optional<std::wstring> found = NamedValue(child.get(), name, budget); found.has_value())
        {
            return found;
        }
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
        {
            break;
        }
        child = std::move(next);
    }
    return std::nullopt;
}
} // namespace

int Application::RunChromeSyncSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The chrome-sync self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto fail = [this](const std::wstring& message) noexcept
    {
        _chrome->SetAccessibilityListenerForTest(std::nullopt);
        SelfTestFailure(message.c_str());
        return 5;
    };
    const float dpi = ChromeDpi();
    // The chrome's share of the frame the loop draws after an input.
    const auto prepare = [&]() noexcept { (void)_chrome->Prepare(_renderer.Width(), _renderer.Height(), dpi); };
    struct Tally
    {
        ChromeHost::SyncCounters counters;
        std::uint64_t preparations = 0;
        std::uint64_t syncs = 0;
    };
    const auto tally = [&]() noexcept
    { return Tally{_chrome->SyncCountersForTest(), _chrome->Statistics().preparations, _chromeSyncs}; };
    // Runs `moves` inputs, each followed by the chrome's share of a frame; reports and returns the work they did.
    const auto measure = [&](const std::wstring& name, int moves, const auto& input) noexcept -> Tally
    {
        prepare();
        const Tally before = tally();
        double elapsed = 0.0;
        for (int index = 0; index < moves; ++index)
        {
            const double start = NowMicroseconds();
            input(index);
            prepare();
            elapsed += NowMicroseconds() - start;
        }
        const Tally after = tally();
        Tally delta;
        delta.counters.layerRows = after.counters.layerRows - before.counters.layerRows;
        delta.counters.menus = after.counters.menus - before.counters.menus;
        delta.counters.texts = after.counters.texts - before.counters.texts;
        delta.counters.headers = after.counters.headers - before.counters.headers;
        delta.counters.overlays = after.counters.overlays - before.counters.overlays;
        delta.counters.publishes = after.counters.publishes - before.counters.publishes;
        delta.preparations = after.preparations - before.preparations;
        delta.syncs = after.syncs - before.syncs;
        ChromeSyncReport(std::format(L"CHROME SYNC {} ({} inputs): syncs={} layerRows={} menus={} texts={} "
                                     L"headers={} overlays={} preparations={} publishes={} time={:.1f}us per input\n",
                                     name, moves, delta.syncs, delta.counters.layerRows, delta.counters.menus,
                                     delta.counters.texts, delta.counters.headers, delta.counters.overlays,
                                     delta.preparations, delta.counters.publishes, elapsed / moves));
        return delta;
    };
    const auto describe = [](const Tally& work)
    {
        return std::format(L"syncs {}, layer rows {}, menus {}, texts {}, headers {}, overlays {}, preparations {}, "
                           L"publishes {}",
                           work.syncs, work.counters.layerRows, work.counters.menus, work.counters.texts,
                           work.counters.headers, work.counters.overlays, work.preparations, work.counters.publishes);
    };

    // A fresh document with one painted layer, shown whole.
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The New command did not settle in the harness.\n");
        std::_Exit(5);
    }
    _forcedDiscard.reset();
    (void)InsertPaintedLayer(_session, L"Measured", 200, 150, Vec2{300, 200}, 0.0, 0xFF2F6FB0u);
    OnTool(EditorTool::Move);
    DocumentChanged();
    const CanvasHole hole = _chrome->HolePixels();
    const auto client = [&](Vec2 view) noexcept
    {
        return MAKELPARAM(static_cast<int>(std::lround(hole.left + view.x)),
                          static_cast<int>(std::lround(hole.top + view.y)));
    };
    const auto layerCenter = [&]() noexcept
    {
        const ImageLayer* layer = _session.ActiveLayer();
        return _viewport.ViewFromDocument(layer != nullptr ? layer->transform.Center() : Vec2{}, DocumentSize());
    };
    constexpr int kMoves = 40;

    // Each pointer move pushes only what it changed (RF-1001), and a UI Automation snapshot is published after it only
    // while a client listens: without one, nothing a pointer move changes is structural.
    for (const bool listening : {false, true})
    {
        _chrome->SetAccessibilityListenerForTest(listening);
        const auto label = [listening](const wchar_t* name)
        { return std::format(L"{} [{}]", name, listening ? L"UIA client" : L"no UIA client"); };
        const auto published = [listening](std::uint64_t changed) noexcept
        { return listening ? changed : std::uint64_t{0}; };
        const wchar_t* const clientState = listening ? L"with" : L"without";

        // A pan with the Hand: the view moves under the pointer; nothing the chrome draws depends on it.
        OnTool(EditorTool::Hand);
        Vec2 start = layerCenter();
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, client(start));
        const Tally pan =
            measure(label(L"pan (Hand)"), kMoves, [&](int index) noexcept
                    { OnPointer(WM_MOUSEMOVE, MK_LBUTTON, client(Vec2{start.x + 2.0 * (index + 1), start.y})); });
        OnPointer(WM_LBUTTONUP, 0, client(Vec2{start.x + 2.0 * kMoves, start.y}));
        if (pan.syncs != 0 || pan.counters.layerRows != 0 || pan.counters.menus != 0 || pan.counters.texts != 0 ||
            pan.counters.headers != 0 || pan.counters.overlays != 0 || pan.preparations != 0 ||
            pan.counters.publishes != 0)
        {
            return fail(std::format(L"A pan with the Hand {} a UIA client rebuilt the chrome ({} over {} moves; "
                                    L"expected nothing).\n",
                                    clientState, describe(pan), kMoves));
        }

        // A pan with Space in the Move tool: the transform box follows the canvas, nothing else.
        OnTool(EditorTool::Move);
        _spacePan = true;
        start = layerCenter();
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, client(start));
        const Tally spacePan =
            measure(label(L"pan (Space, Move tool)"), kMoves, [&](int index) noexcept
                    { OnPointer(WM_MOUSEMOVE, MK_LBUTTON, client(Vec2{start.x - 2.0 * (index + 1), start.y})); });
        OnPointer(WM_LBUTTONUP, 0, client(Vec2{start.x - 2.0 * kMoves, start.y}));
        _spacePan = false;
        if (spacePan.syncs != 0 || spacePan.counters.layerRows != 0 || spacePan.counters.menus != 0 ||
            spacePan.counters.texts != 0 || spacePan.counters.headers != 0 || spacePan.counters.overlays != kMoves ||
            spacePan.preparations != kMoves || spacePan.counters.publishes != published(kMoves))
        {
            return fail(std::format(L"A Space pan in the Move tool {} a UIA client pushed more than the transform box "
                                    L"or published wrongly ({} over {} moves).\n",
                                    clientState, describe(spacePan), kMoves));
        }

        // Wheel steps, in and out: the status bar's zoom and the box.
        POINT screen{static_cast<LONG>(hole.left + hole.width / 2.0f),
                     static_cast<LONG>(hole.top + hole.height / 2.0f)};
        (void)ClientToScreen(_window.get(), &screen);
        const Tally wheel = measure(label(L"wheel step"), kMoves,
                                    [&](int index) noexcept
                                    {
                                        const short delta = index % 2 == 0 ? WHEEL_DELTA : -WHEEL_DELTA;
                                        OnPointer(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)),
                                                  MAKELPARAM(screen.x, screen.y));
                                    });
        if (wheel.syncs != 0 || wheel.counters.layerRows != 0 || wheel.counters.menus != 0 ||
            wheel.counters.texts != kMoves || wheel.counters.headers != 0 || wheel.preparations != kMoves ||
            wheel.counters.publishes != published(kMoves))
        {
            return fail(std::format(L"A wheel step {} a UIA client pushed more than the zoom text and the overlay or "
                                    L"published wrongly ({} over {} steps).\n",
                                    clientState, describe(wheel), kMoves));
        }

        // A transform drag: the header's numbers and the box, never the layer rows or the menus.
        start = layerCenter();
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, client(start));
        const Tally drag =
            measure(label(L"transform drag"), kMoves, [&](int index) noexcept
                    { OnPointer(WM_MOUSEMOVE, MK_LBUTTON, client(Vec2{start.x + 1.0 * (index + 1), start.y + 1.0})); });
        OnPointer(WM_LBUTTONUP, 0, client(Vec2{start.x + 1.0 * kMoves, start.y + 1.0}));
        if (drag.counters.layerRows != 0 || drag.counters.menus != 0 || drag.counters.texts != 0 ||
            drag.preparations == 0 || drag.preparations > kMoves ||
            drag.counters.publishes != published(drag.preparations))
        {
            return fail(std::format(L"A transform drag {} a UIA client rebuilt layer rows or menus or published "
                                    L"wrongly ({} over {} moves).\n",
                                    clientState, describe(drag), kMoves));
        }

        // The brush tip following the pointer, then a stroke: the overlay alone.
        OnTool(EditorTool::Brush);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            return fail(L"The transform release and brush tool switch did not settle.\n");
        start = layerCenter();
        const Tally hover =
            measure(label(L"brush hover"), kMoves, [&](int index) noexcept
                    { OnPointer(WM_MOUSEMOVE, 0, client(Vec2{start.x + 1.0 * (index + 1), start.y})); });
        OnPointer(WM_LBUTTONDOWN, MK_LBUTTON, client(start));
        // Opening now prepares the private draft on its admitted worker. Measure actual live moves only after
        // that original opening has acknowledged and replayed Down; leave every chrome cost assertion intact.
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled || !_session.PendingBrush() ||
            !_canvasBrushInput.Sampling())
            return fail(L"The measured brush opening did not acknowledge its live draft.\n");
        bool replayed = true;
        const Tally stroke =
            measure(label(L"brush drag"), kMoves,
                    [&](int index) noexcept
                    {
                        OnPointer(WM_MOUSEMOVE, MK_LBUTTON, client(Vec2{start.x + 1.0 * (index + 1), start.y + 2.0}));
                        // The real outer loop replays this accepted sample before its next frame.
                        // Settle the source here before measure's Prepare; do not measure an idle
                        // frame while the move still belongs to the fixed input FIFO.
                        replayed = replayed &&
                                   SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled &&
                                   _session.PendingBrush() && _canvasBrushInput.PendingCount() == 0;
                    });
        if (!replayed)
            return fail(L"A measured brush sample did not acknowledge replay before its chrome frame.\n");
        OnPointer(WM_LBUTTONUP, 0, client(Vec2{start.x + 1.0 * kMoves, start.y + 2.0}));
        OnTool(EditorTool::Move);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
            return fail(L"The measured stroke and Move tool switch did not settle.\n");
        for (const Tally* brush : {&hover, &stroke})
        {
            if (brush->syncs != 0 || brush->counters.overlays != kMoves || brush->preparations != kMoves ||
                brush->counters.publishes != published(kMoves))
            {
                return fail(std::format(L"A brush move {} a UIA client pushed more than its tip or published wrongly "
                                        L"({} over {} moves).\n",
                                        clientState, describe(*brush), kMoves));
            }
        }

        // A settled chrome: a sync that finds nothing new writes nothing and costs no preparation.
        const Tally settled = measure(label(L"settled sync"), kMoves, [&](int) noexcept { SyncChrome(); });
        if (settled.syncs != kMoves || settled.counters.layerRows != 0 || settled.counters.menus != 0 ||
            settled.counters.texts != 0 || settled.counters.headers != 0 || settled.counters.overlays != 0 ||
            settled.preparations != 0 || settled.counters.publishes != 0)
        {
            return fail(std::format(L"A sync of a settled chrome {} a UIA client still wrote to it ({} over {} "
                                    L"syncs).\n",
                                    clientState, describe(settled), kMoves));
        }
    }

    // Without a listening client the snapshot waits, and a client connecting later still reads a fresh one: the
    // window's WM_GETOBJECT publishes what waited, as does the in-process root a self-test walks.
    {
        _chrome->SetAccessibilityListenerForTest(false);
        const std::wstring opacityName = LoadStringResource(nullptr, IDS_LAYER_OPACITY);
        const auto opacityShown = [&]() -> std::optional<std::wstring>
        {
            IRawElementProviderFragmentRoot* root = _chrome->AccessibilityRoot();
            wil::com_ptr_nothrow<IRawElementProviderFragment> fragment;
            int budget = 4000;
            if (root == nullptr || FAILED(root->QueryInterface(IID_PPV_ARGS(fragment.put()))) || !fragment)
            {
                return std::nullopt;
            }
            return NamedValue(fragment.get(), opacityName, budget);
        };
        // A first edit marks the title dirty (a name, published at once); the next ones change only a value.
        const auto setOpacity = [&](double opacity) noexcept
        {
            _session.SetOpacity(opacity);
            DocumentChanged();
            prepare();
        };
        setOpacity(0.9);
        const std::uint64_t before = _chrome->SyncCountersForTest().publishes;
        setOpacity(0.3);
        const bool waited = _chrome->AccessibilityStaleForTest() && _chrome->SyncCountersForTest().publishes == before;
        (void)SendMessageW(_window.get(), WM_GETOBJECT, 0, static_cast<LPARAM>(UiaRootObjectId));
        const bool connected =
            !_chrome->AccessibilityStaleForTest() && _chrome->SyncCountersForTest().publishes == before + 1;
        const std::optional<std::wstring> afterConnect = opacityShown();
        setOpacity(0.45);
        const bool waitedAgain = _chrome->AccessibilityStaleForTest();
        const std::optional<std::wstring> inProcess = opacityShown();
        const bool readFresh =
            !_chrome->AccessibilityStaleForTest() && _chrome->SyncCountersForTest().publishes == before + 2;
        // A structural change (a card opening) is published without a client.
        const std::uint64_t beforeCard = _chrome->SyncCountersForTest().publishes;
        _chrome->ShowNotice(L"RF-1001");
        prepare();
        const bool cardPublished =
            !_chrome->AccessibilityStaleForTest() && _chrome->SyncCountersForTest().publishes == beforeCard + 1;
        _chrome->CloseNotice();
        prepare();
        _chrome->SetAccessibilityListenerForTest(std::nullopt);
        if (!waited || !connected || afterConnect != std::optional<std::wstring>(L"30") || !waitedAgain || !readFresh ||
            inProcess != std::optional<std::wstring>(L"45") || !cardPublished)
        {
            return fail(std::format(L"UI Automation snapshots were not published as specified (a value change waited "
                                    L"{}, WM_GETOBJECT published {} and showed '{}', again waited {}, the in-process "
                                    L"root published {} and showed '{}', a card published {}).\n",
                                    waited, connected, afterConnect.value_or(L"none"), waitedAgain, readFresh,
                                    inProcess.value_or(L"none"), cardPublished));
        }
    }

    // The cursor (RF-1002): WM_SETCURSOR comes with every pointer message, so what it asks of the session is decided
    // once per pointer position and session state, the layer scan runs only when a rule needs it, and a pointer
    // outside the selection's bounds skips the winding test. The cursors stay what they were.
    {
        _forcedDiscard = true;
        OnCommand(ChromeHost::kCommandNew);
        if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
        {
            SelfTestFailure(L"The New command did not settle in the harness.\n");
            std::_Exit(5);
        }
        _forcedDiscard.reset();
        // 1000 small layers, then "Target" on top: 100 x 100 at (500, 300), active.
        const Raster tile = SolidRaster(8, 8, 0xFF3366AAu);
        _session.BeginEdit(L"Import Image");
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (int index = 0; index < 1000; ++index)
                                    {
                                        ImageLayer layer = ImageLayer::Blank(L"Many", Vec2{8, 8});
                                        layer.asset = RasterAsset{tile, Raster::MakeThumbnail(tile), layer.name};
                                        layer.transform.origin = Vec2{static_cast<double>((index * 37) % 1200),
                                                                      static_cast<double>((index * 53) % 760)};
                                        document.layers.push_back(layer);
                                    }
                                });
        _session.EndEdit();
        const Guid target = InsertPaintedLayer(_session, L"Target", 100, 100, Vec2{500, 300}, 0.0, 0xFFCC3322u);
        OnTool(EditorTool::Move);
        DocumentChanged();
        prepare();
        const auto clientAt = [&](Vec2 pixel) noexcept
        {
            const Vec2 view = _viewport.ViewFromDocument(pixel, DocumentSize());
            return POINT{static_cast<LONG>(std::lround(hole.left + view.x)),
                         static_cast<LONG>(std::lround(hole.top + view.y))};
        };
        // A point covered by one small layer alone (outside Target) and a point over nothing, as the cursor reads them
        // back from client pixels. Nothing stands in x 1210-1280 or y 770-800 but the blank bottom layer.
        const auto pixelOf = [&](POINT point) noexcept
        {
            return _viewport.DocumentFromView(
                Vec2{static_cast<double>(point.x) - hole.left, static_cast<double>(point.y) - hole.top},
                DocumentSize());
        };
        const auto covering = [&](Vec2 pixel) noexcept
        {
            std::vector<Guid> ids;
            for (const ImageLayer& layer : _session.Document().value().layers)
            {
                if (layer.asset.has_value() && layer.visible && _session.DisplayedTransform(layer).Contains(pixel))
                {
                    ids.push_back(layer.id);
                }
            }
            return ids;
        };
        std::optional<Guid> smallId;
        POINT onSmall{};
        for (const ImageLayer& layer : _session.Document().value().layers)
        {
            if (layer.name != L"Many")
            {
                continue;
            }
            const POINT point = clientAt(layer.transform.Center());
            const std::vector<Guid> ids = covering(pixelOf(point));
            const Vec2 view{static_cast<double>(point.x) - hole.left, static_cast<double>(point.y) - hole.top};
            if (ids.size() == 1 && ids.front() == layer.id &&
                !(_overlayGeometry.has_value() && _overlayGeometry->Hit(view).has_value()))
            {
                smallId = layer.id;
                onSmall = point;
                break;
            }
        }
        const POINT onNothing = clientAt(Vec2{1250.0, 785.0});
        if (!smallId.has_value() || !covering(pixelOf(onNothing)).empty())
        {
            return fail(L"The cursor check found no point over one small layer alone, or over nothing.\n");
        }
        std::array<BYTE, 256> keys{};
        const auto holdControl = [&](bool held) noexcept
        {
            (void)KeyboardState::Read(keys);
            keys[VK_CONTROL] = held ? 0x80 : 0;
            keys[VK_LCONTROL] = held ? 0x80 : 0;
            KeyboardState::Write(keys);
        };
        const auto cursorAt = [&](POINT point) noexcept { return CanvasCursorAt(point).value_or(nullptr); };

        // Auto-select off with a usable active layer: no rule looks under the pointer, wherever it moves.
        _transformAutoSelect = false;
        const std::uint64_t scansBefore = _pointerLayerScans;
        bool movingSizeAll = true;
        double lazyElapsed = 0.0;
        constexpr int kProbes = 200;
        for (int index = 0; index < kProbes; ++index)
        {
            // Across the canvas below Target, clear of its box and handles.
            const POINT point = clientAt(Vec2{100.0 + 1000.0 * index / kProbes, 600.0});
            const double t0 = NowMicroseconds();
            movingSizeAll = cursorAt(point) == IDC_SIZEALL && movingSizeAll;
            lazyElapsed += NowMicroseconds() - t0;
        }
        const std::uint64_t lazyScans = _pointerLayerScans - scansBefore;
        // Auto-select on, outside Target over a small layer: one scan for as many WM_SETCURSOR as come at that point.
        _transformAutoSelect = true;
        SyncChrome();
        const std::uint64_t repeatBefore = _pointerLayerScans;
        bool repeatSizeAll = true;
        for (int index = 0; index < 10; ++index)
        {
            repeatSizeAll = cursorAt(onSmall) == IDC_SIZEALL && repeatSizeAll;
        }
        const std::uint64_t repeatScans = _pointerLayerScans - repeatBefore;
        double scanElapsed = 0.0;
        for (int index = 0; index < kProbes; ++index)
        {
            const POINT point{onSmall.x + (index % 7), onSmall.y + (index % 5)};
            const double t0 = NowMicroseconds();
            (void)cursorAt(point);
            scanElapsed += NowMicroseconds() - t0;
        }
        _transformAutoSelect = false;
        // Without a usable active layer (a blank one) the arrow shows unless Ctrl picks a layer under the pointer; a
        // session change decides afresh at the same point.
        _session.AddBlankLayer();
        SyncChrome();
        const bool blankArrow = cursorAt(onSmall) == IDC_ARROW;
        holdControl(true);
        const bool controlOnNothing = cursorAt(onNothing) == IDC_ARROW;
        const bool controlPicks = cursorAt(onSmall) == IDC_SIZEALL;
        EditLayerRecordsForTest(_session,
                                [&](DocumentFixture& document)
                                {
                                    for (ImageLayer& layer : document.layers)
                                    {
                                        if (layer.id == smallId.value())
                                        {
                                            layer.visible = false;
                                        }
                                    }
                                });
        DocumentChanged();
        const bool followsSession = cursorAt(onSmall) == IDC_ARROW;
        holdControl(false);
        ChromeSyncReport(std::format(L"CURSOR Move tool, 1000 layers: {:.2f}us per move with auto-select off ({} "
                                     L"scans), {:.2f}us per move with auto-select on\n",
                                     lazyElapsed / kProbes, lazyScans, scanElapsed / kProbes));
        static_cast<void>(target);
        if (lazyScans != 0 || !movingSizeAll || repeatScans != 1 || !repeatSizeAll || !blankArrow || !controlPicks ||
            !controlOnNothing || !followsSession)
        {
            return fail(std::format(L"The Move tool's cursor asked the layers more than it needed or changed (scans "
                                    L"while moving with auto-select off {}, for ten WM_SETCURSOR at one point {}; "
                                    L"size-all while moving {}, at the point {}; arrow over a blank active layer {}, "
                                    L"Ctrl picks {}, Ctrl over nothing {}, follows the session {}).\n",
                                    lazyScans, repeatScans, movingSizeAll, repeatSizeAll, blankArrow, controlPicks,
                                    controlOnNothing, followsSession));
        }

        // A 50 000-point selection with the Marquee: one winding test per point for as many WM_SETCURSOR as come,
        // none outside the selection's bounds.
        std::vector<Vec2> ring;
        constexpr int kPoints = 50'000;
        ring.reserve(kPoints);
        for (int index = 0; index < kPoints; ++index)
        {
            const double angle = 6.283185307179586 * index / kPoints;
            const double radius = 250.0 + 40.0 * std::sin(angle * 97.0);
            ring.push_back(Vec2{640.0 + radius * std::cos(angle), 400.0 + radius * std::sin(angle)});
        }
        _session.ApplySelection({ring}, SelectionMode::New, L"Select");
        OnTool(EditorTool::Marquee);
        SelectionChanged();
        const POINT inside = clientAt(Vec2{640.0, 400.0});
        const POINT corner = clientAt(Vec2{640.0 - 280.0, 400.0 - 280.0}); // in the bounds, outside the ring
        const POINT outside = clientAt(Vec2{20.0, 20.0});
        const std::uint64_t testsBefore = _cursorSelectionTests;
        bool insideMoves = true;
        for (int index = 0; index < 10; ++index)
        {
            insideMoves = cursorAt(inside) == IDC_SIZEALL && insideMoves;
        }
        const std::uint64_t insideTests = _cursorSelectionTests - testsBefore;
        const bool cornerCross = cursorAt(corner) == IDC_CROSS;
        const std::uint64_t cornerTests = _cursorSelectionTests - testsBefore - insideTests;
        const bool outsideCross = cursorAt(outside) == IDC_CROSS;
        const std::uint64_t outsideTests = _cursorSelectionTests - testsBefore - insideTests - cornerTests;
        double outsideElapsed = 0.0;
        double insideElapsed = 0.0;
        for (int index = 0; index < kProbes; ++index)
        {
            double t0 = NowMicroseconds();
            (void)cursorAt(POINT{outside.x + index % 9, outside.y + index % 4});
            outsideElapsed += NowMicroseconds() - t0;
            t0 = NowMicroseconds();
            (void)cursorAt(POINT{inside.x + index % 9, inside.y + index % 4});
            insideElapsed += NowMicroseconds() - t0;
        }
        ChromeSyncReport(std::format(L"CURSOR Marquee, {} point selection: {:.2f}us per move outside its bounds, "
                                     L"{:.2f}us per move inside (one winding test per pointer position)\n",
                                     kPoints, outsideElapsed / kProbes, insideElapsed / kProbes));
        _session.Deselect();
        OnTool(EditorTool::Move);
        if (!insideMoves || insideTests != 1 || !cornerCross || cornerTests != 1 || !outsideCross || outsideTests != 0)
        {
            return fail(std::format(L"The selection tools' cursor walked the selection more than it needed or changed "
                                    L"(size-all inside {} after {} tests for ten WM_SETCURSOR; crosshair in the bounds "
                                    L"outside the ring {} after {} tests; crosshair outside the bounds {} after {} "
                                    L"tests).\n",
                                    insideMoves, insideTests, cornerCross, cornerTests, outsideCross, outsideTests));
        }
    }
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The New command did not settle in the harness.\n");
        std::_Exit(5);
    }
    _forcedDiscard.reset();
    OnTool(EditorTool::Move);
    return 0;
}

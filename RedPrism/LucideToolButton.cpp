#include "LucideToolButton.h"

#include <d2d1_3.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
struct IconResource
{
    EditorTool tool;
    int resourceId;
};

constexpr IconResource kIcons[] = {
    {EditorTool::Move, IDR_LUCIDE_MOVE},          {EditorTool::Marquee, IDR_LUCIDE_SQUARE_DASHED},
    {EditorTool::Lasso, IDR_LUCIDE_LASSO},        {EditorTool::Wand, IDR_LUCIDE_WAND},
    {EditorTool::Crop, IDR_LUCIDE_CROP},          {EditorTool::Brush, IDR_LUCIDE_BRUSH},
    {EditorTool::Eraser, IDR_LUCIDE_ERASER},      {EditorTool::Heal, IDR_LUCIDE_BANDAGE},
    {EditorTool::Clone, IDR_LUCIDE_STAMP},        {EditorTool::Smear, IDR_LUCIDE_DROPLET},
    {EditorTool::Gradient, IDR_ICON_GRADIENT},    {EditorTool::Shape, IDR_ICON_SHAPE},
    {EditorTool::Eyedropper, IDR_LUCIDE_PIPETTE}, {EditorTool::Hand, IDR_LUCIDE_HAND},
    {EditorTool::Zoom, IDR_LUCIDE_SEARCH},
};

int g_drawnMask = 0;
int g_preparedMask = 0;
int g_createCount = 0;
// Every tool's icon is warmed once (in the inactive color) the first time any rail button paints, so a tool
// hidden behind More already has a cached document before it is ever shown; each button's own Paint call then
// only ever touches its own slot, so a settled rail costs zero CreateSvgDocument calls per frame.
bool g_allToolsWarmed = false;

struct DocumentCache
{
    // A strong reference, not a raw pointer: a cache keyed by pointer identity must own a reference that keeps
    // the address stable, or a device swap that frees the old context can have a new one reallocated at the same
    // address, aliasing a document built for a context that no longer exists.
    wil::com_ptr_nothrow<ID2D1DeviceContext> context;
    std::uint32_t rgb = 0;
    float size = 0.0f;
    wil::com_ptr_nothrow<ID2D1SvgDocument> document;
};

DocumentCache g_caches[kEditorToolCount];

// DrawLucideSvg's small shared cache (the palette hot zones and the More button), four documents by resource id.
struct SharedSlot
{
    int resourceId = 0;
    // A strong reference, not a raw pointer: see DocumentCache's comment above.
    wil::com_ptr_nothrow<ID2D1DeviceContext> context;
    std::uint32_t rgb = 0;
    float size = 0.0f;
    wil::com_ptr_nothrow<ID2D1SvgDocument> document;
};

SharedSlot g_sharedSlots[4];

[[nodiscard]] int IconResourceId(EditorTool tool) noexcept
{
    for (const IconResource& icon : kIcons)
    {
        if (icon.tool == tool)
        {
            return icon.resourceId;
        }
    }
    return 0;
}

[[nodiscard]] std::string LoadSvg(int resourceId)
{
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr)
    {
        return {};
    }
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    if (bytes == nullptr || size == 0)
    {
        return {};
    }
    std::string xml(static_cast<const char*>(bytes), static_cast<const char*>(bytes) + size);
    const std::string token = "currentColor";
    for (std::size_t at = xml.find(token); at != std::string::npos; at = xml.find(token, at))
    {
        xml.replace(at, token.size(), "#000000");
        at += 7;
    }
    return xml;
}

// Lucide authors a 2px stroke on a 24 grid. `width` is in that grid, so the on-screen stroke is width * size / 24.
void SetStrokeWidth(std::string& xml, float width)
{
    const std::string heavy = "stroke-width=\"2\"";
    const std::string light = std::format("stroke-width=\"{:.2f}\"", width);
    for (std::size_t at = xml.find(heavy); at != std::string::npos; at = xml.find(heavy, at))
    {
        xml.replace(at, heavy.size(), light);
        at += light.size();
    }
}

void ApplyStrokeColor(std::string& xml, std::uint32_t rgb)
{
    char hex[8] = {'#', '0', '0', '0', '0', '0', '0', '\0'};
    constexpr char digits[] = "0123456789ABCDEF";
    for (int nibble = 5; nibble >= 0; --nibble)
    {
        hex[6 - nibble] = digits[(rgb >> (nibble * 4)) & 0xF];
    }
    const std::string black = "#000000";
    for (std::size_t at = xml.find(black); at != std::string::npos; at = xml.find(black, at))
    {
        xml.replace(at, black.size(), hex);
        at += 7;
    }
}

[[nodiscard]] std::uint32_t RgbOf(const D2D1_COLOR_F& color) noexcept
{
    const auto channel = [](float value)
    { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
}
} // namespace

int LucideIconsDrawn() noexcept
{
    int count = 0;
    for (int bit = g_drawnMask; bit != 0; bit &= bit - 1)
    {
        ++count;
    }
    return count;
}

int LucideIconsPrepared() noexcept
{
    int count = 0;
    for (int bit = g_preparedMask; bit != 0; bit &= bit - 1)
    {
        ++count;
    }
    return count;
}

int LucideSvgCreateCount() noexcept
{
    return g_createCount;
}

void ReleaseLucideIcons() noexcept
{
    // Each document goes before the context it was made for.
    for (DocumentCache& cache : g_caches)
    {
        cache.document.reset();
        cache.context.reset();
        cache.rgb = 0;
        cache.size = 0.0f;
    }
    for (SharedSlot& slot : g_sharedSlots)
    {
        slot.document.reset();
        slot.context.reset();
        slot.resourceId = 0;
        slot.rgb = 0;
        slot.size = 0.0f;
    }
    // Nothing is prepared any more; the next rail paint warms every tool again on the device it draws with.
    g_preparedMask = 0;
    g_allToolsWarmed = false;
}

int LucideCachedDocumentCount() noexcept
{
    int count = 0;
    for (const DocumentCache& cache : g_caches)
    {
        count += cache.document ? 1 : 0;
    }
    for (const SharedSlot& slot : g_sharedSlots)
    {
        count += slot.document ? 1 : 0;
    }
    return count;
}

bool DrawLucideSvg(ID2D1DeviceContext* context, int resourceId, const D2D1_RECT_F& bounds, const D2D1_COLOR_F& color)
{
    if (context == nullptr || resourceId == 0)
    {
        return false;
    }
    wil::com_ptr_nothrow<ID2D1DeviceContext5> svgContext;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(svgContext.put()))) || !svgContext)
    {
        return false;
    }
    const float size = std::min(bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (size < 4.0f)
    {
        return false;
    }
    const std::uint32_t rgb = RgbOf(color);
    SharedSlot* slot = &g_sharedSlots[0];
    for (SharedSlot& candidate : g_sharedSlots)
    {
        if (candidate.resourceId == resourceId)
        {
            slot = &candidate;
            break;
        }
        if (candidate.resourceId == 0)
        {
            slot = &candidate;
        }
    }
    if (slot->resourceId != resourceId || slot->context.get() != context || slot->rgb != rgb || slot->size != size ||
        !slot->document)
    {
        std::string xml = LoadSvg(resourceId);
        // Keep the drawn stroke at least 1.6 device pixels. The authored 1.25 on a 24 grid is under a pixel
        // once the icon is smaller than the rail buttons, and the path breaks into specks.
        const float stroke = std::max(1.25f, 1.6f * 24.0f / size);
        SetStrokeWidth(xml, stroke);
        ApplyStrokeColor(xml, rgb);
        if (xml.empty())
        {
            return false;
        }
        wil::unique_hglobal memory(GlobalAlloc(GMEM_MOVEABLE, xml.size()));
        if (!memory)
        {
            return false;
        }
        void* destination = GlobalLock(memory.get());
        if (destination == nullptr)
        {
            return false;
        }
        std::memcpy(destination, xml.data(), xml.size());
        GlobalUnlock(memory.get());
        wil::com_ptr_nothrow<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(memory.get(), TRUE, stream.put())) || !stream)
        {
            return false;
        }
        // CreateStreamOnHGlobal(..., TRUE, ...) made the stream the HGLOBAL's owner (it frees it on release).
        static_cast<void>(memory.release());
        wil::com_ptr_nothrow<ID2D1SvgDocument> document;
        if (FAILED(svgContext->CreateSvgDocument(stream.get(), D2D1::SizeF(size, size), document.put())) || !document)
        {
            return false;
        }
        ++g_createCount;
        slot->resourceId = resourceId;
        slot->context = context;
        slot->rgb = rgb;
        slot->size = size;
        slot->document = std::move(document);
    }
    D2D1_MATRIX_3X2_F previous = D2D1::Matrix3x2F::Identity();
    context->GetTransform(&previous);
    const float left = bounds.left + ((bounds.right - bounds.left) - size) * 0.5f;
    const float top = bounds.top + ((bounds.bottom - bounds.top) - size) * 0.5f;
    // The SVG root is 24 by 24. The viewport argument does not replace that, so scale it into the hotspot.
    const float svgScale = size / 24.0f;
    svgContext->SetTransform(D2D1::Matrix3x2F::Scale(svgScale, svgScale) * D2D1::Matrix3x2F::Translation(left, top) *
                             previous);
    svgContext->DrawSvgDocument(slot->document.get());
    svgContext->SetTransform(previous);
    return true;
}

[[nodiscard]] bool EnsureSvgDocument(ID2D1DeviceContext* context, ID2D1DeviceContext5* svgContext, std::size_t index,
                                     std::uint32_t rgb, float size)
{
    DocumentCache& cache = g_caches[index];
    if (cache.context.get() == context && cache.rgb == rgb && cache.size == size && cache.document)
    {
        g_preparedMask |= 1 << index;
        return true;
    }
    const int resourceId = IconResourceId(EditorToolAt(index));
    std::string xml = LoadSvg(resourceId);
    SetStrokeWidth(xml, 1.25f);
    ApplyStrokeColor(xml, rgb);
    if (xml.empty())
    {
        return false;
    }
    wil::unique_hglobal memory(GlobalAlloc(GMEM_MOVEABLE, xml.size()));
    if (!memory)
    {
        return false;
    }
    void* destination = GlobalLock(memory.get());
    if (destination == nullptr)
    {
        return false;
    }
    std::memcpy(destination, xml.data(), xml.size());
    GlobalUnlock(memory.get());
    wil::com_ptr_nothrow<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(memory.get(), TRUE, stream.put())) || !stream)
    {
        return false;
    }
    // CreateStreamOnHGlobal(..., TRUE, ...) made the stream the HGLOBAL's owner (it frees it on release).
    static_cast<void>(memory.release());
    wil::com_ptr_nothrow<ID2D1SvgDocument> document;
    const HRESULT created = svgContext->CreateSvgDocument(stream.get(), D2D1::SizeF(size, size), document.put());
    if (FAILED(created) || !document)
    {
        return false;
    }
    ++g_createCount;
    cache.context = context;
    cache.rgb = rgb;
    cache.size = size;
    cache.document = std::move(document);
    g_preparedMask |= 1 << index;
    return true;
}

LucideToolButton::LucideToolButton(std::wstring tooltip, EditorTool tool, bool fluentFallback)
    : DxUi::Button(L""), _tool(tool), _fluentFallback(fluentFallback)
{
    SetVariant(DxUi::ButtonVariant::IconOnly);
    SetTooltipText(std::move(tooltip));
}

void LucideToolButton::Paint(DxUi::ControlHost& host) const
{
    if (_svgFailed)
    {
        DxUi::Button::Paint(host);
        return;
    }
    const std::wstring glyph(1, EditorToolGlyph(_tool, _fluentFallback));
    DxUi::Button::Paint(host);

    ID2D1DeviceContext* context = host.GetDeviceContext();
    const int resourceId = IconResourceId(_tool);
    if (context == nullptr || resourceId == 0)
    {
        _svgFailed = true;
        const_cast<LucideToolButton*>(this)->SetText(glyph);
        RequestInvalidate();
        return;
    }

    const DxUi::ThemePalette& theme = host.GetTheme();
    const D2D1_COLOR_F color = !IsEnabled() ? theme.disabledText : IsPressed() ? theme.selectionText : theme.text;
    const std::uint32_t rgb = RgbOf(color);
    const D2D1_RECT_F bounds = GetBounds();
    const float span = std::min(bounds.right - bounds.left, bounds.bottom - bounds.top);
    const float size = std::clamp(span - 16.0f, 12.0f, 22.0f);
    if (size < 12.0f)
    {
        return;
    }

    wil::com_ptr_nothrow<ID2D1DeviceContext5> svgContext;
    const HRESULT queried = context->QueryInterface(IID_PPV_ARGS(svgContext.put()));
    if (FAILED(queried) || !svgContext)
    {
        _svgFailed = true;
        const_cast<LucideToolButton*>(this)->SetText(glyph);
        RequestInvalidate();
        return;
    }

    const std::size_t toolIndex = EditorToolIndex(_tool);
    if (!EnsureSvgDocument(context, svgContext.get(), toolIndex, rgb, size))
    {
        _svgFailed = true;
        const_cast<LucideToolButton*>(this)->SetText(glyph);
        RequestInvalidate();
        return;
    }
    if (!g_allToolsWarmed)
    {
        // A tool hidden behind More never paints on its own until chosen, so its icon would otherwise stay
        // unprepared. Warm every other tool once here, in the ordinary (inactive) color, using this call's
        // context and size (the same for every rail button). Steady state costs nothing after that: each
        // button's own Paint call above is the only one that ever touches its slot again, and EnsureSvgDocument
        // is a cache hit once a slot already matches.
        const std::uint32_t restRgb = RgbOf(theme.text);
        for (std::size_t index = 0; index < kEditorToolCount; ++index)
        {
            if (index != toolIndex)
            {
                static_cast<void>(EnsureSvgDocument(context, svgContext.get(), index, restRgb, size));
            }
        }
        g_allToolsWarmed = true;
    }
    DocumentCache& cache = g_caches[toolIndex];

    D2D1_MATRIX_3X2_F previous = D2D1::Matrix3x2F::Identity();
    context->GetTransform(&previous);
    const float left = bounds.left + ((bounds.right - bounds.left) - size) * 0.5f;
    const float top = bounds.top + ((bounds.bottom - bounds.top) - size) * 0.5f;
    D2D1_MATRIX_3X2_F placed = D2D1::Matrix3x2F::Translation(left, top);
    if (_tool == EditorTool::Heal)
    {
        // The Lucide bandage is horizontal. Turn it about the icon center so it reads as a 45° plaster.
        const float center = size * 0.5f;
        placed = D2D1::Matrix3x2F::Translation(-center, -center) * D2D1::Matrix3x2F::Rotation(45.0f) *
                 D2D1::Matrix3x2F::Scale(0.78f, 0.78f) * D2D1::Matrix3x2F::Translation(center + left, center + top);
    }
    svgContext->SetTransform(placed * previous);
    svgContext->DrawSvgDocument(cache.document.get());
    svgContext->SetTransform(previous);
    g_drawnMask |= 1 << EditorToolIndex(_tool);
}

LucideIconButton::LucideIconButton(std::wstring tooltip, int resourceId) : DxUi::Button(L""), _resourceId(resourceId)
{
    SetVariant(DxUi::ButtonVariant::IconOnly);
    SetTooltipText(std::move(tooltip));
}

void LucideIconButton::Paint(DxUi::ControlHost& host) const
{
    DxUi::Button::Paint(host);
    ID2D1DeviceContext* context = host.GetDeviceContext();
    if (context == nullptr || _resourceId == 0)
    {
        return;
    }
    const DxUi::ThemePalette& theme = host.GetTheme();
    const D2D1_COLOR_F color = !IsEnabled() ? theme.disabledText : IsPressed() ? theme.selectionText : theme.text;
    const D2D1_RECT_F bounds = GetBounds();
    const float span = std::min(bounds.right - bounds.left, bounds.bottom - bounds.top);
    const float size = std::clamp(span - 16.0f, 12.0f, 22.0f);
    const float left = bounds.left + ((bounds.right - bounds.left) - size) * 0.5f;
    const float top = bounds.top + ((bounds.bottom - bounds.top) - size) * 0.5f;
    DrawLucideSvg(context, _resourceId, D2D1::RectF(left, top, left + size, top + size), color);
}

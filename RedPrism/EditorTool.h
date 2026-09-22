#pragma once

#include "resource.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The tool rail, top to bottom, in legacy order (ContentView tool rail plus Eraser as its own button).
// Names live in the STRINGTABLE (EditorToolNameId); keys are the Photoshop-style shortcuts.
enum class EditorTool : std::uint8_t
{
    Move = 0,
    Marquee,
    Lasso,
    Wand,
    Crop,
    Brush,
    Eraser,
    Heal,
    Clone,
    Smear,
    Gradient,
    Shape,
    Eyedropper,
    Hand,
    Zoom,
    Count,
};

inline constexpr std::size_t kEditorToolCount = static_cast<std::size_t>(EditorTool::Count);

[[nodiscard]] constexpr EditorTool EditorToolAt(std::size_t index) noexcept
{
    return index < kEditorToolCount ? static_cast<EditorTool>(index) : EditorTool::Move;
}

[[nodiscard]] constexpr std::size_t EditorToolIndex(EditorTool tool) noexcept
{
    return static_cast<std::size_t>(tool) < kEditorToolCount ? static_cast<std::size_t>(tool) : 0;
}

[[nodiscard]] constexpr unsigned int EditorToolNameId(EditorTool tool) noexcept
{
    switch (tool)
    {
    case EditorTool::Move:
        return IDS_TOOL_MOVE;
    case EditorTool::Marquee:
        return IDS_TOOL_MARQUEE;
    case EditorTool::Lasso:
        return IDS_TOOL_LASSO;
    case EditorTool::Wand:
        return IDS_TOOL_WAND;
    case EditorTool::Crop:
        return IDS_TOOL_CROP;
    case EditorTool::Brush:
        return IDS_TOOL_BRUSH;
    case EditorTool::Eraser:
        return IDS_TOOL_ERASER;
    case EditorTool::Heal:
        return IDS_TOOL_HEAL;
    case EditorTool::Clone:
        return IDS_TOOL_CLONE;
    case EditorTool::Smear:
        return IDS_TOOL_SMEAR;
    case EditorTool::Gradient:
        return IDS_TOOL_GRADIENT;
    case EditorTool::Shape:
        return IDS_TOOL_SHAPE;
    case EditorTool::Eyedropper:
        return IDS_TOOL_EYEDROPPER;
    case EditorTool::Hand:
        return IDS_TOOL_HAND;
    case EditorTool::Zoom:
        return IDS_TOOL_ZOOM;
    case EditorTool::Count:
        return IDS_TOOL_MOVE;
    }
    return IDS_TOOL_MOVE;
}

// The tools that draw or move a selection outline (legacy NavigationTool.isSelectionTool).
[[nodiscard]] constexpr bool EditorToolIsSelection(EditorTool tool) noexcept
{
    return tool == EditorTool::Marquee || tool == EditorTool::Lasso || tool == EditorTool::Wand;
}

// The tools that work through a brush tip: the Brush (which can erase), the Eraser, Spot Healing, Clone Stamp and
// Smear (whose Liquify and Smudge modes warp instead of paint).
[[nodiscard]] constexpr bool EditorToolIsBrush(EditorTool tool) noexcept
{
    return tool == EditorTool::Brush || tool == EditorTool::Eraser || tool == EditorTool::Heal ||
           tool == EditorTool::Clone || tool == EditorTool::Smear;
}

// Tools that keep their own tip size, hardness and opacity (Clone Stamp and Smear); the rest share one.
[[nodiscard]] constexpr int EditorToolTipFamily(EditorTool tool) noexcept
{
    return tool == EditorTool::Clone ? 1 : tool == EditorTool::Smear ? 2 : 0;
}

// Unicode stand-ins when Segoe Fluent Icons is not installed. The rail draws these in the UI font.
[[nodiscard]] constexpr wchar_t EditorToolFallbackGlyph(EditorTool tool) noexcept
{
    switch (tool)
    {
    case EditorTool::Move:
        return L'\u2194';
    case EditorTool::Marquee:
        return L'\u25A2';
    case EditorTool::Lasso:
        return L'\u270E';
    case EditorTool::Wand:
        return L'\u2726';
    case EditorTool::Crop:
        return L'\u2702';
    case EditorTool::Brush:
        return L'\u270E';
    case EditorTool::Eraser:
        return L'\u232B';
    case EditorTool::Heal:
        return L'\u2695';
    case EditorTool::Clone:
        return L'\u2398';
    case EditorTool::Smear:
        return L'\u261D';
    case EditorTool::Gradient:
        return L'\u25D0';
    case EditorTool::Shape:
        return L'\u25A1';
    case EditorTool::Eyedropper:
        return L'\u2317';
    case EditorTool::Hand:
        return L'\u270B';
    case EditorTool::Zoom:
        return L'\u2315';
    case EditorTool::Count:
        return L'\u2315';
    }
    return L'\u2194';
}

// Segoe Fluent Icons code points, the same family RedSalamander uses for chrome glyphs. `fluent` false selects
// the Unicode stand-in. HostTests proves every Fluent glyph exists in the installed icon font.
[[nodiscard]] constexpr wchar_t EditorToolGlyph(EditorTool tool, bool fluent = true) noexcept
{
    if (!fluent)
    {
        return EditorToolFallbackGlyph(tool);
    }
    switch (tool)
    {
    case EditorTool::Move:
        return L'\xE759'; // SIPMove: the four-way move arrow
    case EditorTool::Marquee:
        return L'\xEF20'; // Marquee: dashed selection rectangle
    case EditorTool::Lasso:
        return L'\xF1BE'; // Lasso
    case EditorTool::Wand:
        return L'\xF4A5'; // SpecialEffectSize: sparkles, the wand
    case EditorTool::Crop:
        return L'\xE7A8'; // Crop
    case EditorTool::Brush:
        return L'\xE76D'; // InkingTool: the drawing pen
    case EditorTool::Eraser:
        return L'\xE75C'; // EraseTool
    case EditorTool::Heal:
        return L'\xE95E'; // Health: the bandage stand-in
    case EditorTool::Clone:
        return L'\xE8C8'; // Copy: no stamp glyph in the icon font
    case EditorTool::Smear:
        return L'\xF271'; // PointerHand: the smudge finger
    case EditorTool::Gradient:
        return L'\xEF90'; // Flow: a blend, not a paint drop
    case EditorTool::Shape:
        return L'\xE739'; // Checkbox: a square
    case EditorTool::Eyedropper:
        return L'\xEF3C'; // Eyedropper
    case EditorTool::Hand:
        return L'\xEB8A'; // NUIFPPressHand: an open hand
    case EditorTool::Zoom:
        return L'\xE8A3'; // ZoomIn: magnifier
    case EditorTool::Count:
        return L'\xE8A3';
    }
    return L'\xE7C2';
}

[[nodiscard]] constexpr wchar_t EditorToolKey(EditorTool tool) noexcept
{
    switch (tool)
    {
    case EditorTool::Move:
        return L'V';
    case EditorTool::Marquee:
        return L'M';
    case EditorTool::Lasso:
        return L'L';
    case EditorTool::Wand:
        return L'W';
    case EditorTool::Crop:
        return L'C';
    case EditorTool::Brush:
        return L'B';
    case EditorTool::Eraser:
        return L'E';
    case EditorTool::Heal:
        return L'J';
    case EditorTool::Clone:
        return L'S';
    case EditorTool::Smear:
        return L'R';
    case EditorTool::Gradient:
        return L'G';
    case EditorTool::Shape:
        return L'U';
    case EditorTool::Eyedropper:
        return L'I';
    case EditorTool::Hand:
        return L'H';
    case EditorTool::Zoom:
        return L'Z';
    case EditorTool::Count:
        return L'Z';
    }
    return L'V';
}

// The tool whose shortcut is `virtualKey`, or Count when the key is not a tool key.
[[nodiscard]] constexpr EditorTool EditorToolFromKey(wchar_t virtualKey) noexcept
{
    for (std::size_t index = 0; index < kEditorToolCount; ++index)
    {
        if (EditorToolKey(EditorToolAt(index)) == virtualKey)
        {
            return EditorToolAt(index);
        }
    }
    return EditorTool::Count;
}

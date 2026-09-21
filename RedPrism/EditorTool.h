#pragma once

#include "resource.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The tool rail, top to bottom, in legacy order (ContentView tool rail plus Eraser as its own button and
// Idle last). Names live in the STRINGTABLE (EditorToolNameId); keys are the Photoshop-style shortcuts.
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
    Idle,
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
    case EditorTool::Idle:
    case EditorTool::Count:
        return IDS_TOOL_IDLE;
    }
    return IDS_TOOL_MOVE;
}

// Segoe Fluent Icons code points. HostTests proves every glyph exists in the installed icon font.
[[nodiscard]] constexpr wchar_t EditorToolGlyph(EditorTool tool) noexcept
{
    switch (tool)
    {
    case EditorTool::Move:
        return L'\xE7C2'; // Move
    case EditorTool::Marquee:
        return L'\xEF20'; // Dashed selection rectangle
    case EditorTool::Lasso:
        return L'\xEC87'; // Pen: freehand outline
    case EditorTool::Wand:
        return L'\xF4A5'; // Sparkles
    case EditorTool::Crop:
        return L'\xE7A8'; // Crop
    case EditorTool::Brush:
        return L'\xE70F'; // Edit (pencil)
    case EditorTool::Eraser:
        return L'\xE75C'; // EraseTool
    case EditorTool::Heal:
        return L'\xE90F'; // Repair
    case EditorTool::Clone:
        return L'\xE8C8'; // Copy
    case EditorTool::Smear:
        return L'\xEA99'; // Broom
    case EditorTool::Gradient:
        return L'\xEB42'; // Paint drop (fill)
    case EditorTool::Shape:
        return L'\xE7FB'; // Rounded rectangle
    case EditorTool::Eyedropper:
        return L'\xEF3C'; // Eyedropper
    case EditorTool::Hand:
        return L'\xE7C9'; // TouchPointer
    case EditorTool::Zoom:
        return L'\xE71E'; // Zoom
    case EditorTool::Idle:
    case EditorTool::Count:
        return L'\xE711'; // Cancel
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
    case EditorTool::Idle:
    case EditorTool::Count:
        return L'A';
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

#pragma once

#include <cstdint>

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
};

[[nodiscard]] constexpr const wchar_t* EditorToolName(EditorTool tool) noexcept
{
    switch (tool)
    {
    case EditorTool::Move:
        return L"Move";
    case EditorTool::Marquee:
        return L"Marquee";
    case EditorTool::Lasso:
        return L"Lasso";
    case EditorTool::Wand:
        return L"Magic Wand";
    case EditorTool::Crop:
        return L"Crop";
    case EditorTool::Brush:
        return L"Brush";
    case EditorTool::Eraser:
        return L"Eraser";
    case EditorTool::Heal:
        return L"Heal";
    case EditorTool::Clone:
        return L"Clone";
    case EditorTool::Smear:
        return L"Smear";
    case EditorTool::Gradient:
        return L"Gradient";
    case EditorTool::Shape:
        return L"Shape";
    case EditorTool::Eyedropper:
        return L"Eyedropper";
    case EditorTool::Hand:
        return L"Hand";
    case EditorTool::Zoom:
        return L"Zoom";
    case EditorTool::Idle:
        return L"Idle";
    }
    return L"Move";
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
        return L'A';
    }
    return L'V';
}

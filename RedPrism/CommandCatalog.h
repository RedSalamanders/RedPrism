#pragma once

#include "EditorTool.h"
#include "KeyChord.h"

#include <cstdint>
#include <span>
#include <string_view>

// The command catalog (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators", Specs/Core/Core_Settings.md
// "Command chords"): every finished user action has one stable integer id, one stable text id for the settings file,
// the STRINGTABLE caption that names it, where its keys apply, and its default chords. A key, a menu item and a
// control all run the action through Application::OnCommand with the integer id. Pure data (no windows.h typedefs),
// so HostTests reads the same table the application dispatches from.

// The integer ids. ChromeHost derives from this, so `ChromeHost::kCommandSave` names the same constant.
struct CommandIds
{
    static constexpr int kCommandNew = 100;
    static constexpr int kCommandOpen = 101;
    static constexpr int kCommandSave = 102;
    static constexpr int kCommandSaveAs = 103;
    static constexpr int kCommandExportPng = 104;
    static constexpr int kCommandExportJpeg = 105;
    static constexpr int kCommandExit = 106;
    static constexpr int kCommandNewCanvas = 107;
    static constexpr int kCommandImportImages = 108;
    static constexpr int kCommandCloseProject = 109;
    static constexpr int kCommandUndo = 200;
    static constexpr int kCommandRedo = 201;
    static constexpr int kCommandFreeTransform = 202;
    static constexpr int kCommandCut = 203;
    static constexpr int kCommandCopy = 204;
    static constexpr int kCommandCopyMerged = 205;
    static constexpr int kCommandPaste = 206;
    static constexpr int kCommandFillForeground = 207;
    static constexpr int kCommandFillBackground = 208;
    static constexpr int kCommandClearPixels = 209;
    static constexpr int kCommandInvert = 210;
    static constexpr int kCommandContentAwareFill = 211;
    static constexpr int kCommandSwapPalette = 212;
    static constexpr int kCommandResetPalette = 213;
    static constexpr int kCommandNewLayer = 300;
    static constexpr int kCommandNewFolder = 301;
    static constexpr int kCommandDeleteLayer = 302;
    static constexpr int kCommandToggleLayerVisibility = 303;
    static constexpr int kCommandMoveOutOfGroup = 304;
    static constexpr int kCommandFlipLayerHorizontal = 305;
    static constexpr int kCommandFlipLayerVertical = 306;
    static constexpr int kCommandLayerViaCopy = 307;
    static constexpr int kCommandAddMask = 308;
    static constexpr int kCommandAddBlackMask = 309;
    static constexpr int kCommandNewLevelsAdjustment = 310;
    static constexpr int kCommandNewCurvesAdjustment = 311;
    static constexpr int kCommandNewHueSaturationAdjustment = 312;
    static constexpr int kCommandNewExposureAdjustment = 313;
    static constexpr int kCommandNewGradientMapAdjustment = 314;
    static constexpr int kCommandNewGrainAdjustment = 315;
    static constexpr int kCommandEditAdjustment = 316;
    static constexpr int kCommandGroupLayers = 317;
    static constexpr int kCommandRenameLayer = 318;
    static constexpr int kCommandMoveLayerUp = 319;
    static constexpr int kCommandMoveLayerDown = 320;
    static constexpr int kCommandClipToggle = 321;
    static constexpr int kCommandMergeDown = 322;
    static constexpr int kCommandTargetLayerMask = 323;
    static constexpr int kCommandToggleMaskLink = 324;
    static constexpr int kCommandToggleLayerMask = 325;
    static constexpr int kCommandZoomFit = 400;
    static constexpr int kCommandZoomActual = 401;
    static constexpr int kCommandZoomIn = 402;
    static constexpr int kCommandZoomOut = 403;
    static constexpr int kCommandSampleRing = 404;
    static constexpr int kCommandKeyboardShortcuts = 405;
    static constexpr int kCommandAbout = 500;
    static constexpr int kCommandFlipCanvasHorizontal = 600;
    static constexpr int kCommandFlipCanvasVertical = 601;
    static constexpr int kCommandCurves = 602;
    static constexpr int kCommandLevels = 603;
    static constexpr int kCommandHueSaturation = 604;
    static constexpr int kCommandExposure = 605;
    static constexpr int kCommandGradientMap = 606;
    static constexpr int kCommandGrain = 607;
    static constexpr int kCommandCanvasSize = 608;
    static constexpr int kCommandImageSize = 609;
    static constexpr int kCommandSelectAll = 700;
    static constexpr int kCommandDeselect = 701;
    static constexpr int kCommandInverse = 702;
    static constexpr int kCommandLoadLayerSelection = 703;
    static constexpr int kCommandLoadMaskSelection = 704;
    static constexpr int kCommandGaussianBlur = 800;
    static constexpr int kCommandMotionBlur = 801;
    static constexpr int kCommandAddNoise = 802;
    static constexpr int kCommandLensCorrection = 803;
    // One id per finished local action, so a settings replacement can name it. Tool order matches EditorTool.
    static constexpr int kCommandSelectToolBase = 900;
    static constexpr int kCommandNudgeSelectionLeft = 920;
    static constexpr int kCommandNudgeSelectionRight = 921;
    static constexpr int kCommandNudgeSelectionUp = 922;
    static constexpr int kCommandNudgeSelectionDown = 923;
    static constexpr int kCommandNudgeLayerLeft = 924;
    static constexpr int kCommandNudgeLayerRight = 925;
    static constexpr int kCommandNudgeLayerUp = 926;
    static constexpr int kCommandNudgeLayerDown = 927;
    static constexpr int kCommandNudgePixelsLeft = 928;
    static constexpr int kCommandNudgePixelsRight = 929;
    static constexpr int kCommandNudgePixelsUp = 930;
    static constexpr int kCommandNudgePixelsDown = 931;
    static constexpr int kCommandBrushSmaller = 940;
    static constexpr int kCommandBrushLarger = 941;
    static constexpr int kCommandBrushSofter = 942;
    static constexpr int kCommandBrushHarder = 943;
    static constexpr int kCommandBlendPrevious = 944;
    static constexpr int kCommandBlendNext = 945;
    static constexpr int kCommandDeleteKey = 946;
    static constexpr int kCommandToggleMarqueeKind = 947;
    static constexpr int kCommandToggleLassoKind = 948;
    static constexpr int kCommandToggleShapeKind = 949;

    [[nodiscard]] static constexpr int SelectToolCommand(EditorTool tool) noexcept
    {
        return kCommandSelectToolBase + static_cast<int>(tool);
    }
    [[nodiscard]] static constexpr EditorTool ToolForSelectCommand(int command) noexcept
    {
        const int index = command - kCommandSelectToolBase;
        if (index < 0 || index >= static_cast<int>(kEditorToolCount))
        {
            return EditorTool::Count;
        }
        return static_cast<EditorTool>(index);
    }
};

// Where a command's keys apply (the same for its default and its replacement chords).
enum class CommandScope : std::uint8_t
{
    Canvas, // the canvas has the keyboard focus
    App,    // anywhere, except while a chrome text input has the focus (the field takes the key, Decision D4)
    Global, // also while a text input has the focus, for a chord with Ctrl or Alt or a function key (D4)
};

// A state the command's keys also require.
enum class CommandCondition : std::uint8_t
{
    None,
    PixelsSelected,     // a non-empty selection and no lasso outline in progress (Ctrl-arrows move pixels)
    SelectionOnOutline, // a selection tool, a non-empty selection and no outline in progress (arrows move it)
    MoveTool,
    BrushTool, // a tool that works through a brush tip (EditorToolIsBrush)
    NoLassoDraft,
    MarqueeTool,
    LassoTool,
    ShapeTool,
};

struct CommandCatalogEntry
{
    int command = 0;
    unsigned captionId = 0;      // the STRINGTABLE caption (menu row, Command List name)
    const wchar_t* textId = L""; // the settings file's stable id (`cmd/edit/swapPalette`)
    CommandScope scope = CommandScope::App;
    CommandCondition condition = CommandCondition::None;
    bool shiftAllowed = false; // a chord without Shift also runs with Shift held (a nudge steps by ten; a tool key
                               // or the palette keys ignore it, as the legacy canvas did)
    bool repeats = true;       // an auto-repeated key runs the command again; false: holding the key runs it once
};

// How a default chord belongs to its command.
enum class DefaultChordKind : std::uint8_t
{
    Dispatch,    // the key runs the command
    SharedLabel, // shown for the command while another command dispatches the same chord (Delete Layer's Delete:
                 // the Delete key runs kCommandDeleteKey, which deletes the layer when nothing else is targeted)
    System,      // a window-manager gesture shown for the command and never dispatched by the app (Exit's Alt+F4,
                 // Decision D5: a replacement cannot take it away)
};

struct DefaultChord
{
    int command = 0;
    KeyChord chord;
    DefaultChordKind kind = DefaultChordKind::Dispatch;
};

namespace CommandCatalogDetail
{
using C = CommandIds;
constexpr CommandScope kCanvas = CommandScope::Canvas;
constexpr CommandScope kApp = CommandScope::App;
constexpr CommandScope kGlobal = CommandScope::Global;
constexpr CommandCondition kNone = CommandCondition::None;

[[nodiscard]] constexpr CommandCatalogEntry Tool(EditorTool tool, unsigned captionId, const wchar_t* textId) noexcept
{
    return CommandCatalogEntry{C::SelectToolCommand(tool), captionId, textId, kCanvas, kNone, true, true};
}

// A tool's default key is its letter (EditorToolKey), the key of its select command.
[[nodiscard]] constexpr DefaultChord ToolDefault(EditorTool tool) noexcept
{
    return DefaultChord{C::SelectToolCommand(tool), Key(static_cast<std::uint16_t>(EditorToolKey(tool)))};
}
} // namespace CommandCatalogDetail

// Every command, in the Command List's order.
[[nodiscard]] inline std::span<const CommandCatalogEntry> CommandCatalog() noexcept
{
    using namespace CommandCatalogDetail;
    static constexpr CommandCatalogEntry kCatalog[] = {
        {C::kCommandNew, IDS_CMD_NEW, L"cmd/file/new", kGlobal},
        {C::kCommandNewCanvas, IDS_CMD_NEW_CANVAS, L"cmd/file/newCanvas", kGlobal},
        {C::kCommandOpen, IDS_CMD_OPEN, L"cmd/file/open", kGlobal},
        {C::kCommandImportImages, IDS_CMD_IMPORT_IMAGES, L"cmd/file/importImages", kGlobal},
        {C::kCommandSave, IDS_CMD_SAVE, L"cmd/file/save", kGlobal},
        {C::kCommandSaveAs, IDS_CMD_SAVE_AS, L"cmd/file/saveAs", kGlobal},
        {C::kCommandExportPng, IDS_CMD_EXPORT_PNG, L"cmd/file/exportPng", kGlobal},
        {C::kCommandExportJpeg, IDS_CMD_EXPORT_JPEG, L"cmd/file/exportJpeg", kGlobal},
        {C::kCommandCloseProject, IDS_CMD_CLOSE_PROJECT, L"cmd/file/closeProject", kGlobal},
        {C::kCommandExit, IDS_CMD_EXIT, L"cmd/file/exit", kGlobal},
        {C::kCommandUndo, IDS_CMD_UNDO, L"cmd/edit/undo", kApp},
        {C::kCommandRedo, IDS_CMD_REDO, L"cmd/edit/redo", kApp},
        {C::kCommandCut, IDS_CMD_CUT, L"cmd/edit/cut", kApp},
        {C::kCommandCopy, IDS_CMD_COPY, L"cmd/edit/copy", kApp},
        {C::kCommandCopyMerged, IDS_CMD_COPY_MERGED, L"cmd/edit/copyMerged", kApp},
        {C::kCommandPaste, IDS_CMD_PASTE, L"cmd/edit/paste", kApp},
        {C::kCommandFillForeground, IDS_CMD_FILL_FOREGROUND, L"cmd/edit/fillForeground", kCanvas,
         CommandCondition::NoLassoDraft},
        {C::kCommandFillBackground, IDS_CMD_FILL_BACKGROUND, L"cmd/edit/fillBackground", kApp},
        {C::kCommandSwapPalette, IDS_CMD_SWAP_PALETTE, L"cmd/edit/swapPalette", kCanvas, kNone, true},
        {C::kCommandResetPalette, IDS_CMD_RESET_PALETTE, L"cmd/edit/resetPalette", kCanvas, kNone, true},
        {C::kCommandClearPixels, IDS_CMD_CLEAR_PIXELS, L"cmd/edit/clearPixels", kApp},
        {C::kCommandContentAwareFill, IDS_CMD_CONTENT_AWARE_FILL, L"cmd/edit/contentAwareFill", kCanvas},
        {C::kCommandInvert, IDS_CMD_INVERT, L"cmd/edit/invert", kApp},
        {C::kCommandFreeTransform, IDS_CMD_FREE_TRANSFORM, L"cmd/edit/freeTransform", kApp},
        {C::kCommandCurves, IDS_CMD_CURVES, L"cmd/image/curves", kApp},
        {C::kCommandLevels, IDS_CMD_LEVELS, L"cmd/image/levels", kApp},
        {C::kCommandHueSaturation, IDS_CMD_HUE_SATURATION, L"cmd/image/hueSaturation", kApp},
        {C::kCommandExposure, IDS_CMD_EXPOSURE, L"cmd/image/exposure", kApp},
        {C::kCommandGradientMap, IDS_CMD_GRADIENT_MAP, L"cmd/image/gradientMap", kApp},
        {C::kCommandGrain, IDS_CMD_GRAIN, L"cmd/image/grain", kApp},
        {C::kCommandCanvasSize, IDS_CMD_CANVAS_SIZE, L"cmd/image/canvasSize", kApp},
        {C::kCommandImageSize, IDS_CMD_IMAGE_SIZE, L"cmd/image/imageSize", kApp},
        {C::kCommandFlipCanvasHorizontal, IDS_CMD_FLIP_CANVAS_HORIZONTAL, L"cmd/image/flipHorizontal", kApp},
        {C::kCommandFlipCanvasVertical, IDS_CMD_FLIP_CANVAS_VERTICAL, L"cmd/image/flipVertical", kApp},
        {C::kCommandNewLayer, IDS_CMD_NEW_LAYER, L"cmd/layer/new", kApp},
        {C::kCommandNewFolder, IDS_CMD_NEW_FOLDER, L"cmd/layer/newFolder", kApp},
        {C::kCommandLayerViaCopy, IDS_CMD_LAYER_VIA_COPY, L"cmd/layer/viaCopy", kApp},
        {C::kCommandNewLevelsAdjustment, IDS_CMD_ADJUST_LEVELS, L"cmd/layer/newLevels", kApp},
        {C::kCommandNewCurvesAdjustment, IDS_CMD_ADJUST_CURVES, L"cmd/layer/newCurves", kApp},
        {C::kCommandNewHueSaturationAdjustment, IDS_CMD_ADJUST_HSV, L"cmd/layer/newHueSaturation", kApp},
        {C::kCommandNewExposureAdjustment, IDS_CMD_ADJUST_EXPOSURE, L"cmd/layer/newExposure", kApp},
        {C::kCommandNewGradientMapAdjustment, IDS_CMD_ADJUST_GRADIENT_MAP, L"cmd/layer/newGradientMap", kApp},
        {C::kCommandNewGrainAdjustment, IDS_CMD_ADJUST_GRAIN, L"cmd/layer/newGrain", kApp},
        {C::kCommandEditAdjustment, IDS_CMD_EDIT_ADJUSTMENT, L"cmd/layer/editAdjustment", kApp},
        {C::kCommandGroupLayers, IDS_CMD_GROUP_LAYERS, L"cmd/layer/group", kApp},
        {C::kCommandMoveOutOfGroup, IDS_CMD_MOVE_OUT_OF_GROUP, L"cmd/layer/moveOutOfGroup", kApp},
        {C::kCommandRenameLayer, IDS_CMD_RENAME_LAYER, L"cmd/layer/rename", kApp},
        {C::kCommandDeleteLayer, IDS_CMD_DELETE_LAYER, L"cmd/layer/delete", kApp},
        {C::kCommandToggleLayerVisibility, IDS_CMD_TOGGLE_LAYER_VISIBILITY, L"cmd/layer/toggleVisibility", kApp},
        {C::kCommandMoveLayerUp, IDS_CMD_MOVE_LAYER_UP, L"cmd/layer/moveUp", kApp},
        {C::kCommandMoveLayerDown, IDS_CMD_MOVE_LAYER_DOWN, L"cmd/layer/moveDown", kApp},
        {C::kCommandClipToggle, IDS_CMD_CLIP_CREATE, L"cmd/layer/clip", kApp},
        {C::kCommandMergeDown, IDS_CMD_MERGE_DOWN, L"cmd/layer/mergeDown", kApp},
        {C::kCommandAddMask, IDS_CMD_ADD_MASK, L"cmd/layer/addMask", kApp},
        {C::kCommandAddBlackMask, IDS_CMD_ADD_BLACK_MASK, L"cmd/layer/addBlackMask", kApp},
        {C::kCommandTargetLayerMask, IDS_CMD_TARGET_MASK, L"cmd/layer/targetMask", kApp},
        {C::kCommandToggleMaskLink, IDS_CMD_UNLINK_MASK, L"cmd/layer/toggleMaskLink", kApp},
        {C::kCommandToggleLayerMask, IDS_CMD_DISABLE_MASK, L"cmd/layer/toggleMask", kApp},
        {C::kCommandFlipLayerHorizontal, IDS_CMD_FLIP_LAYER_HORIZONTAL, L"cmd/layer/flipHorizontal", kApp},
        {C::kCommandFlipLayerVertical, IDS_CMD_FLIP_LAYER_VERTICAL, L"cmd/layer/flipVertical", kApp},
        {C::kCommandSelectAll, IDS_CMD_SELECT_ALL, L"cmd/select/all", kApp},
        {C::kCommandDeselect, IDS_CMD_DESELECT, L"cmd/select/deselect", kApp},
        {C::kCommandInverse, IDS_CMD_INVERSE, L"cmd/select/inverse", kApp},
        {C::kCommandLoadLayerSelection, IDS_CMD_LOAD_LAYER_SELECTION, L"cmd/select/loadLayer", kApp},
        {C::kCommandLoadMaskSelection, IDS_CMD_LOAD_MASK_SELECTION, L"cmd/select/loadMask", kApp},
        {C::kCommandGaussianBlur, IDS_CMD_GAUSSIAN_BLUR, L"cmd/filter/gaussianBlur", kApp},
        {C::kCommandMotionBlur, IDS_CMD_MOTION_BLUR, L"cmd/filter/motionBlur", kApp},
        {C::kCommandAddNoise, IDS_CMD_ADD_NOISE, L"cmd/filter/addNoise", kApp},
        {C::kCommandLensCorrection, IDS_CMD_LENS_CORRECTION, L"cmd/filter/lensCorrection", kApp},
        {C::kCommandZoomFit, IDS_CMD_ZOOM_FIT, L"cmd/view/zoomFit", kApp},
        {C::kCommandZoomActual, IDS_CMD_ZOOM_ACTUAL, L"cmd/view/zoomActual", kApp},
        {C::kCommandZoomIn, IDS_CMD_ZOOM_IN, L"cmd/view/zoomIn", kApp},
        {C::kCommandZoomOut, IDS_CMD_ZOOM_OUT, L"cmd/view/zoomOut", kApp},
        {C::kCommandSampleRing, IDS_CMD_SAMPLE_RING, L"cmd/view/sampleRing", kApp},
        {C::kCommandKeyboardShortcuts, IDS_CMD_KEYBOARD_SHORTCUTS, L"cmd/help/keyboardShortcuts", kApp},
        {C::kCommandAbout, IDS_CMD_ABOUT, L"cmd/help/about", kApp},
        Tool(EditorTool::Move, IDS_CMD_TOOL_MOVE, L"cmd/tool/move"),
        Tool(EditorTool::Marquee, IDS_CMD_TOOL_MARQUEE, L"cmd/tool/marquee"),
        Tool(EditorTool::Lasso, IDS_CMD_TOOL_LASSO, L"cmd/tool/lasso"),
        Tool(EditorTool::Wand, IDS_CMD_TOOL_WAND, L"cmd/tool/wand"),
        Tool(EditorTool::Crop, IDS_CMD_TOOL_CROP, L"cmd/tool/crop"),
        Tool(EditorTool::Brush, IDS_CMD_TOOL_BRUSH, L"cmd/tool/brush"),
        Tool(EditorTool::Eraser, IDS_CMD_TOOL_ERASER, L"cmd/tool/eraser"),
        Tool(EditorTool::Heal, IDS_CMD_TOOL_HEAL, L"cmd/tool/heal"),
        Tool(EditorTool::Clone, IDS_CMD_TOOL_CLONE, L"cmd/tool/clone"),
        Tool(EditorTool::Smear, IDS_CMD_TOOL_SMEAR, L"cmd/tool/smear"),
        Tool(EditorTool::Gradient, IDS_CMD_TOOL_GRADIENT, L"cmd/tool/gradient"),
        Tool(EditorTool::Shape, IDS_CMD_TOOL_SHAPE, L"cmd/tool/shape"),
        Tool(EditorTool::Eyedropper, IDS_CMD_TOOL_EYEDROPPER, L"cmd/tool/eyedropper"),
        Tool(EditorTool::Hand, IDS_CMD_TOOL_HAND, L"cmd/tool/hand"),
        Tool(EditorTool::Zoom, IDS_CMD_TOOL_ZOOM, L"cmd/tool/zoom"),
        {C::kCommandNudgeSelectionLeft, IDS_CMD_NUDGE_SELECTION_LEFT, L"cmd/nudge/selectionLeft", kCanvas,
         CommandCondition::SelectionOnOutline, true},
        {C::kCommandNudgeSelectionRight, IDS_CMD_NUDGE_SELECTION_RIGHT, L"cmd/nudge/selectionRight", kCanvas,
         CommandCondition::SelectionOnOutline, true},
        {C::kCommandNudgeSelectionUp, IDS_CMD_NUDGE_SELECTION_UP, L"cmd/nudge/selectionUp", kCanvas,
         CommandCondition::SelectionOnOutline, true},
        {C::kCommandNudgeSelectionDown, IDS_CMD_NUDGE_SELECTION_DOWN, L"cmd/nudge/selectionDown", kCanvas,
         CommandCondition::SelectionOnOutline, true},
        {C::kCommandNudgeLayerLeft, IDS_CMD_NUDGE_LAYER_LEFT, L"cmd/nudge/layerLeft", kCanvas,
         CommandCondition::MoveTool, true},
        {C::kCommandNudgeLayerRight, IDS_CMD_NUDGE_LAYER_RIGHT, L"cmd/nudge/layerRight", kCanvas,
         CommandCondition::MoveTool, true},
        {C::kCommandNudgeLayerUp, IDS_CMD_NUDGE_LAYER_UP, L"cmd/nudge/layerUp", kCanvas, CommandCondition::MoveTool,
         true},
        {C::kCommandNudgeLayerDown, IDS_CMD_NUDGE_LAYER_DOWN, L"cmd/nudge/layerDown", kCanvas,
         CommandCondition::MoveTool, true},
        {C::kCommandNudgePixelsLeft, IDS_CMD_NUDGE_PIXELS_LEFT, L"cmd/nudge/pixelsLeft", kApp,
         CommandCondition::PixelsSelected, true},
        {C::kCommandNudgePixelsRight, IDS_CMD_NUDGE_PIXELS_RIGHT, L"cmd/nudge/pixelsRight", kApp,
         CommandCondition::PixelsSelected, true},
        {C::kCommandNudgePixelsUp, IDS_CMD_NUDGE_PIXELS_UP, L"cmd/nudge/pixelsUp", kApp,
         CommandCondition::PixelsSelected, true},
        {C::kCommandNudgePixelsDown, IDS_CMD_NUDGE_PIXELS_DOWN, L"cmd/nudge/pixelsDown", kApp,
         CommandCondition::PixelsSelected, true},
        {C::kCommandBrushSmaller, IDS_CMD_BRUSH_SMALLER, L"cmd/brush/smaller", kCanvas, CommandCondition::BrushTool},
        {C::kCommandBrushLarger, IDS_CMD_BRUSH_LARGER, L"cmd/brush/larger", kCanvas, CommandCondition::BrushTool},
        {C::kCommandBrushSofter, IDS_CMD_BRUSH_SOFTER, L"cmd/brush/softer", kCanvas, CommandCondition::BrushTool},
        {C::kCommandBrushHarder, IDS_CMD_BRUSH_HARDER, L"cmd/brush/harder", kCanvas, CommandCondition::BrushTool},
        {C::kCommandBlendPrevious, IDS_CMD_BLEND_PREVIOUS, L"cmd/blend/previous", kCanvas},
        {C::kCommandBlendNext, IDS_CMD_BLEND_NEXT, L"cmd/blend/next", kCanvas},
        {C::kCommandDeleteKey, IDS_CMD_DELETE_KEY, L"cmd/edit/delete", kCanvas},
        {C::kCommandToggleMarqueeKind, IDS_CMD_TOGGLE_MARQUEE, L"cmd/tool/toggleMarquee", kCanvas,
         CommandCondition::MarqueeTool, true, false},
        {C::kCommandToggleLassoKind, IDS_CMD_TOGGLE_LASSO, L"cmd/tool/toggleLasso", kCanvas,
         CommandCondition::LassoTool, true, false},
        {C::kCommandToggleShapeKind, IDS_CMD_TOGGLE_SHAPE, L"cmd/tool/toggleShape", kCanvas,
         CommandCondition::ShapeTool, false, false},
    };
    return kCatalog;
}

// The default chords in the order a key tries them: a command with a condition that shares a chord with another
// comes first (M on the Marquee switches its shape before it would choose the Marquee again). A command's first
// chord is the one its menu row, the Command List and the STRINGTABLE column show; the rest are aliases.
[[nodiscard]] inline std::span<const DefaultChord> DefaultChords() noexcept
{
    using C = CommandIds;
    using P = KeyPosition;
    using CommandCatalogDetail::ToolDefault;
    namespace V = VirtualKey;
    constexpr DefaultChordKind kShared = DefaultChordKind::SharedLabel;
    static constexpr DefaultChord kDefaults[] = {
        {C::kCommandToggleMarqueeKind, Key('M')},
        {C::kCommandToggleLassoKind, Key('L')},
        {C::kCommandToggleShapeKind, Shift(Key('U'))},
        {C::kCommandNew, Ctrl(Key('N'))},
        {C::kCommandOpen, Ctrl(Key('O'))},
        {C::kCommandSave, Ctrl(Key('S'))},
        {C::kCommandSaveAs, Ctrl(Shift(Key('S')))},
        // The legacy key equivalents, Command as Ctrl and Option as Alt (RF-1117).
        {C::kCommandExportPng, Ctrl(Shift(Key('E')))},
        {C::kCommandExportJpeg, Ctrl(Alt(Shift(Key('S'))))},
        {C::kCommandCloseProject, Ctrl(Key('W'))},
        {C::kCommandExit, Alt(Key(V::kF4)), DefaultChordKind::System},
        {C::kCommandUndo, Ctrl(Key('Z'))},
        {C::kCommandRedo, Ctrl(Shift(Key('Z')))},
        {C::kCommandRedo, Ctrl(Key('Y'))},
        {C::kCommandCut, Ctrl(Key('X'))},
        {C::kCommandCopy, Ctrl(Key('C'))},
        {C::kCommandCopyMerged, Ctrl(Shift(Key('C')))},
        {C::kCommandPaste, Ctrl(Key('V'))},
        {C::kCommandFillForeground, Alt(Key(V::kBack))},
        {C::kCommandFillBackground, Ctrl(Key(V::kBack))},
        {C::kCommandSwapPalette, Key('X')},
        {C::kCommandResetPalette, Key('D')},
        {C::kCommandContentAwareFill, Shift(Key(V::kDelete))},
        {C::kCommandInvert, Ctrl(Key('I'))},
        {C::kCommandFreeTransform, Ctrl(Key('T'))},
        {C::kCommandCurves, Ctrl(Key('M'))},
        {C::kCommandLevels, Ctrl(Key('L'))},
        {C::kCommandHueSaturation, Ctrl(Key('U'))},
        {C::kCommandCanvasSize, Ctrl(Alt(Key('C')))},
        {C::kCommandImageSize, Ctrl(Alt(Key('I')))},
        {C::kCommandNewLayer, Ctrl(Shift(Key('N')))},
        {C::kCommandNewFolder, Ctrl(Key('G'))},
        {C::kCommandLayerViaCopy, Ctrl(Key('J'))},
        {C::kCommandMoveOutOfGroup, Ctrl(Shift(Key('G')))},
        {C::kCommandRenameLayer, Key(V::kF2)},
        {C::kCommandDeleteLayer, Key(V::kDelete), kShared},
        {C::kCommandMoveLayerUp, Ctrl(At(P::BracketRight))},
        {C::kCommandMoveLayerDown, Ctrl(At(P::BracketLeft))},
        {C::kCommandClipToggle, Ctrl(Alt(Key('G')))},
        {C::kCommandMergeDown, Ctrl(Key('E'))},
        {C::kCommandSelectAll, Ctrl(Key('A'))},
        {C::kCommandDeselect, Ctrl(Key('D'))},
        {C::kCommandInverse, Ctrl(Shift(Key('I')))},
        {C::kCommandZoomFit, Ctrl(Key('0'))},
        {C::kCommandZoomActual, Ctrl(Key('1'))},
        {C::kCommandZoomIn, Ctrl(At(P::NumberRowPlus))},
        {C::kCommandZoomIn, Ctrl(Shift(At(P::NumberRowPlus)))},
        {C::kCommandZoomIn, Ctrl(Key(V::kAdd))},
        {C::kCommandZoomOut, Ctrl(At(P::NumberRowMinus))},
        {C::kCommandZoomOut, Ctrl(Shift(At(P::NumberRowMinus)))},
        {C::kCommandZoomOut, Ctrl(Key(V::kSubtract))},
        ToolDefault(EditorTool::Move),
        ToolDefault(EditorTool::Marquee),
        ToolDefault(EditorTool::Lasso),
        ToolDefault(EditorTool::Wand),
        ToolDefault(EditorTool::Crop),
        ToolDefault(EditorTool::Brush),
        ToolDefault(EditorTool::Eraser),
        ToolDefault(EditorTool::Heal),
        ToolDefault(EditorTool::Clone),
        ToolDefault(EditorTool::Smear),
        ToolDefault(EditorTool::Gradient),
        ToolDefault(EditorTool::Shape),
        ToolDefault(EditorTool::Eyedropper),
        ToolDefault(EditorTool::Hand),
        ToolDefault(EditorTool::Zoom),
        {C::kCommandNudgeSelectionLeft, Key(V::kLeft)},
        {C::kCommandNudgeSelectionRight, Key(V::kRight)},
        {C::kCommandNudgeSelectionUp, Key(V::kUp)},
        {C::kCommandNudgeSelectionDown, Key(V::kDown)},
        {C::kCommandNudgeLayerLeft, Key(V::kLeft)},
        {C::kCommandNudgeLayerRight, Key(V::kRight)},
        {C::kCommandNudgeLayerUp, Key(V::kUp)},
        {C::kCommandNudgeLayerDown, Key(V::kDown)},
        {C::kCommandNudgePixelsLeft, Ctrl(Key(V::kLeft))},
        {C::kCommandNudgePixelsRight, Ctrl(Key(V::kRight))},
        {C::kCommandNudgePixelsUp, Ctrl(Key(V::kUp))},
        {C::kCommandNudgePixelsDown, Ctrl(Key(V::kDown))},
        {C::kCommandBrushSmaller, At(P::BracketLeft)},
        {C::kCommandBrushLarger, At(P::BracketRight)},
        {C::kCommandBrushSofter, Shift(At(P::BracketLeft))},
        {C::kCommandBrushHarder, Shift(At(P::BracketRight))},
        {C::kCommandBlendPrevious, Shift(At(P::NumberRowMinus))},
        {C::kCommandBlendNext, Shift(At(P::NumberRowPlus))},
        {C::kCommandDeleteKey, Key(V::kDelete)},
    };
    return kDefaults;
}

// The catalog row of `command`; nullptr for an id the catalog does not know.
[[nodiscard]] inline const CommandCatalogEntry* FindCommand(int command) noexcept
{
    for (const CommandCatalogEntry& entry : CommandCatalog())
    {
        if (entry.command == command)
        {
            return &entry;
        }
    }
    return nullptr;
}

// The integer command a settings `commandId` names; 0 when no command has that text id.
[[nodiscard]] inline int CommandForTextId(std::wstring_view textId) noexcept
{
    for (const CommandCatalogEntry& entry : CommandCatalog())
    {
        if (textId == entry.textId)
        {
            return entry.command;
        }
    }
    return 0;
}

// The first default chord of `command` (what its menu row shows before any replacement); empty when it has none.
[[nodiscard]] inline KeyChord PrimaryDefaultChord(int command) noexcept
{
    for (const DefaultChord& chord : DefaultChords())
    {
        if (chord.command == command)
        {
            return chord.chord;
        }
    }
    return KeyChord{};
}

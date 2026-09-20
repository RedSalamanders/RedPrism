# Application chrome

Status: current intended chrome layout contract
Last reviewed: 2026-09-20
Owner: `ChromeHost`, `Application` file/session commands

## Scope

This contract owns how editor chrome is laid out on the one HWND. Widget catalog work lives in DxUi.
Document pixels live in the canvas composition ([`Rendering/Rendering_Composition.md`](../Rendering/Rendering_Composition.md)),
not inside a DxUi control. Common file dialogs (`IFileDialog`) and `WM_DROPFILES` are host jobs, not chrome HWNDs.

## Shell layout (DIP)

At 96 DPI one DIP equals one pixel. Physical size scales with window DPI (or `--dpi` during `--screenshot`).

| Band | Height/width |
| --- | --- |
| MenuBar | 30 DIP |
| TabControl | 32 DIP |
| Tool header | 36 DIP |
| StatusStrip | 24 DIP |
| Tool rail | 48 DIP |
| Layers splitter | 6 DIP |
| Layers panel | 160–480 DIP, default 240 |

The canvas hole is the remaining rectangle. Escape moves focus from the canvas to chrome. Pointer down in the hole
focuses the canvas and is not consumed by DxUi.

Native `HMENU` flyouts for File and Edit go through `DxUi::ShowNativeHMenuContextMenu`. That is the supported menu
path. GDI owner-draw menus and child edit HWNDs on the swap chain are prohibited.

## Tools and accelerators

Toolbar exclusive tools, idle (A). Keys: V M L W C B E J S R G U I H Z, Space-hand. Ctrl+N/O/S, Ctrl+Shift+S save as,
Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo.

StatusStrip shows zoom percent, document size, and the current tool. The tab title is the file name (or Untitled) with
a dirty mark when history is modified.

The layers Tree lists names (hidden rows annotated), selection updates `DocumentSession`. Tree has no drag API on the
current DxUi pin; reorder stays a later interaction (C8) rather than a second retained toolkit.

## Splitter, picker, and stepper (C3 fallback)

DxUi Splitter, ColorPicker, and NumericStepper remain HOLD in
`Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md` while LocalizedAdaptiveLayout owns those files.
Until RedPrism bumps `Dependencies/DxUi.lock.json` onto a commit that ships them:

1. Size the tool rail, canvas, and layers panel with **app-owned DIP rectangles** (persistable layers width,
   minimum/maximum, drag to resize).
2. Treat `ColorSwatch` as the compact well. A saturation field MAY be an EmbeddedHost custom mesh (document-adjacent).
3. Use `TextField` plus host arrow-key nudge for inspector numbers when C8 lands.

Do not implement Splitter as a second retained-control toolkit inside RedPrism.

## Files

New creates a 1280×800 blank layer and marks history saved. Open/Save use `IFileDialog`. Dropped files open a `.prism`
or exploded folder, or import an image onto the current document.

## Validation

Harness screenshots of the empty-document shell at **96** and **144** DPI (`--screenshot <png> --warp --dpi 96|144`).
`--self-test --warp` composites chrome, then proves 0 DxUi and 0 canvas surface bytes when hidden. Keyboard Escape
leaves the canvas.

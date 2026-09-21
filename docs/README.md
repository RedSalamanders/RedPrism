# RedPrism

Windows-native raster composition (Direct3D 11 + DxUi). This guide is not a behavior authority; see `Specs/`.

## Run

```powershell
.\build.ps1
.\.build\x64\Debug\RedPrism.exe
.\.build\x64\Debug\RedPrism.exe --warp
.\.build\x64\Debug\RedPrism.exe --help
```

`--screenshot <png> [--dpi 96|144] [--warp]` captures the titled window without activating it.

## Shell

One window: menu bar, document tab, tool rail with the foreground/background wells, canvas, layers splitter,
layers panel with the opacity stepper, status strip. New / Open / Save use the
common file dialog. Drop a `.prism` package, an exploded project folder, or an image onto the window.

- **Tool rail**: sixteen tools with Photoshop-style keys (V move, M marquee, L lasso, W wand, C crop, B brush,
  E eraser, J heal, S clone, R smear, G gradient, U shape, I eyedropper, H hand, Z zoom, A no tool). Keys apply
  when the canvas has focus; Escape returns focus to chrome. Hover a button for its name and key.
- **Palette**: the two wells at the bottom of the rail are the foreground (front) and background (behind) colors.
  Click one to open the color picker (saturation/value field, hue strip, R/G/B, hex); OK or Enter commits, Cancel or
  Escape restores. X swaps the wells, D resets them to black over white.
- **Layers**: drag the 6 DIP splitter (or use Left/Right, Shift, Home/End while it has focus) to size the panel
  between 160 and 480 DIP; the canvas keeps at least 200 DIP. The title row's stepper edits the active layer's
  opacity (type, Up/Down, buttons) as one undo step.
- **Move tool**: drag the active layer (or the layer under the pointer with Ctrl or Auto Select), pull a handle to
  resize (Alt about the center, Shift for a free ratio, Ctrl to distort), turn the knob to rotate (Shift snaps to
  15°). Moves snap to the canvas and the other layers unless Ctrl is held. Alt-drag duplicates, arrows nudge,
  Ctrl+T opens Free Transform with the header's numbers; Enter or Apply commits, Escape or Cancel discards. The
  Image menu flips the canvas, the Layer menu flips the layer.
- **Canvas**: wheel zooms around the cursor, Space or the Hand tool pans, View menu fits (Ctrl+0), shows actual
  pixels (Ctrl+1) or zooms in/out (Ctrl+= / Ctrl+-). A pixel grid appears from 800 % zoom.
- **Layers**: click a row to select it, click a folder's expander to collapse it. Layer menu adds a layer
  (Ctrl+Shift+N) or folder (Ctrl+G), deletes the active layer (Delete), toggles its visibility, or moves it out of
  its folder (Ctrl+Shift+G).
- **Edit**: Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z or Ctrl+Y) name the step they revert.
- **Selection**: the Marquee (M, rectangle or ellipse), Lasso (L, freehand or polygonal) and Magic Wand (W) draw
  outlines; Shift adds, Alt subtracts, dragging inside a selection moves it, the header expands or contracts it, and
  the Select menu selects all (Ctrl+A), deselects (Ctrl+D), inverts (Ctrl+Shift+I) or loads a layer's pixels or a
  mask as the selection.

Empty-document harness captures:

- [96 DPI](screenshots/empty-shell-96.png)
- [144 DPI](screenshots/empty-shell-144.png)
- [Move tool demo](screenshots/transform-96.png) (`--demo transform`)
- [Selection demo](screenshots/selection-96.png) (`--demo selection`)

Brush, retouch and filters are later phases (indexed plans under `Specs/Plans/WIP/`). See
[usage.md](usage.md) for the command line and file formats.

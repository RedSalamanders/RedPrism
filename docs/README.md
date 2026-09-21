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

## Install

Releases are portable ZIPs from `.\package.ps1` (`RedPrism-<version>-<x64|ARM64>.zip` with a `.sha256` beside it):
extract the folder anywhere and run `RedPrism.exe`. The Visual C++ runtime is inside; Windows 10 or later supplies
the rest. Nothing is installed or registered; settings and crash reports live under `%LocalAppData%\RedPrism`. The
executable is unsigned in this version, so SmartScreen may ask once.

## Shell

One window: menu bar, document tab, tool rail with the foreground/background wells, canvas, layers splitter,
layers panel with the opacity stepper, status strip. New / Open / Save use the common file dialog. Drop a `.prism`
package, an exploded project folder, or images onto the window. An image opened or dropped asks whether it becomes
a layer of the current project or a new project of its own size; a modified project asks to save before it is
replaced or closed. The menus list every function of the Mac app; one that is not implemented yet says so when
invoked instead of doing nothing.

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
- **Crop**: the Crop tool (C) frames the whole canvas; drag a handle (Alt from the center), the inside to move
  the frame or the outside to draw a new one, pick a ratio in the header, and Enter or Apply Crop resizes the
  canvas to the frame without resampling; Escape or Cancel returns to the whole canvas. Edges snap to the canvas
  and to layer bounds.
- **Sizes**: File › New Canvas… asks for a size (an image on the clipboard suggests its own), Image › Canvas Size…
  grows or shrinks the canvas around an anchor in pixels, percent, inches or centimeters (relative or absolute,
  ratio locked or free) with a transparent or colored extension, and Image › Image Size… resamples every layer
  (nearest, smooth or high quality) or changes only the resolution; each is one undo step.
- **Canvas**: wheel zooms around the cursor, Space or the Hand tool pans, View menu fits (Ctrl+0), shows actual
  pixels (Ctrl+1) or zooms in/out (Ctrl+= / Ctrl+-). A pixel grid appears from 800 % zoom.
- **Layers**: click a row to select it, click a folder's expander to collapse it. Layer menu adds a layer
  (Ctrl+Shift+N) or folder (Ctrl+G), wraps the active layer in a folder (Group Selected Layers), copies the
  selection or the layer to a new layer (Ctrl+J), renames it (F2, or double-click its row), deletes the active
  layer or its targeted mask (Delete, without a selection), toggles its visibility, moves it up or down among its
  siblings (Ctrl+] / Ctrl+[) or out of its folder (Ctrl+Shift+G), clips it to the layer below or releases the clip
  (Ctrl+Alt+G), merges it down onto the layer beneath (or a folder into one layer; Ctrl+E), or adds a mask (white,
  or black; from the selection when there is one).
- **Edit**: Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z or Ctrl+Y) name the step they revert. Cut, Copy, Copy Merged and
  Paste (Ctrl+X, Ctrl+C, Ctrl+Shift+C, Ctrl+V) move pixels between layers and other apps (a paste lands in place as a
  new layer); Fill with the foreground (Alt+Backspace) or background (Ctrl+Backspace) color, Clear Selection Pixels
  (Delete) and Invert (Ctrl+I) edit the selection, or the whole layer or mask without one.
- **Selection**: the Marquee (M, rectangle or ellipse), Lasso (L, freehand or polygonal) and Magic Wand (W) draw
  outlines; Shift adds, Alt subtracts, dragging inside a selection moves it, the header expands or contracts it, and
  the Select menu selects all (Ctrl+A), deselects (Ctrl+D), inverts (Ctrl+Shift+I) or loads a layer's pixels or a
  mask as the selection. Ctrl-drag inside a selection moves its pixels (Alt duplicates), Ctrl+arrows nudge them,
  Ctrl+T transforms them on a floating layer that merges back, Layer via Copy (Ctrl+J) copies them to a layer, and
  Add Mask or Add Black Mask on the Layer menu turns the selection into a mask.
- **Brush and Eraser**: B paints with the foreground color, E erases (the Brush's header also has an Erase mode).
  The header sets the size, hardness and opacity; `[` and `]` step the size, Shift-`[` and Shift-`]` the hardness,
  digits the opacity. Shift-click draws a straight line from the last stroke. With a layer mask selected the brush
  paints it black (hide) or white (reveal). Strokes show as you draw and commit on release as one undo step that
  shares every untouched tile with the previous state.
- **Retouch**: Spot Healing (J) blends a blemish into its surroundings (Content-Aware, Create Texture or Proximity
  Match); Clone Stamp (S) copies from an Alt-clicked source, aligned or not, from this layer or all layers; Smear
  (R) liquifies, blurs or smudges; Gradient (G) drags a linear or radial fill that previews until Enter or Apply;
  Shape (U) drags a rectangle (with a corner radius) or an ellipse into a new layer, Shift-U switching the kind;
  the Eyedropper (I), or Alt with the Brush, reads a color from the canvas.
- **Adjust**: the Image menu's Curves (Ctrl+M), Levels (Ctrl+L), Hue/Saturation (Ctrl+U), Exposure, Gradient Map
  and Grain and the Filter menu's Gaussian Blur, Motion Blur, Add Noise and Lens Correction edit the active layer
  (inside the selection) in a panel with a live preview; OK applies one undo step. The Layer menu adds adjustment
  layers of the same kinds that stay live over everything beneath them (or their clipping base) and edit through
  the same panels. Shift+= and Shift+- step a layer's blend mode; Hue, Saturation, Color and Luminosity blend as in
  Photoshop. Edit › Content-Aware Fill (Shift+Delete) fills a selection from the surrounding pixels of the layer,
  growing the layer when the selection reaches past its edge; the panel says when nothing opaque surrounds it.
- **Settings, reports, access**: a JSON settings file under `%LocalAppData%\RedPrism` applies live as you edit
  it (theme, sample ring, panel width, JPEG quality) and remembers the window; a crash leaves a local minidump
  and the next start says where (nothing is uploaded); the chrome follows Windows high contrast and exposes every
  control to UI Automation with a name; a `Languages\<tag>` satellite table translates the chrome.

Empty-document harness captures:

- [96 DPI](screenshots/empty-shell-96.png)
- [144 DPI](screenshots/empty-shell-144.png)
- [Move tool demo](screenshots/transform-96.png) (`--demo transform`)
- [Selection demo](screenshots/selection-96.png) (`--demo selection`)
- [Brush demo](screenshots/brush-96.png) (`--demo brush`)
- [Retouch demo](screenshots/retouch-96.png) (`--demo retouch`)
- [Adjust demo](screenshots/adjust-96.png) (`--demo adjust`)
- [Canvas Size card](screenshots/canvassize-96.png) (`--demo canvassize`)
- [Crop tool](screenshots/crop-96.png) (`--demo crop`)

Filters are later phases (indexed plans under `Specs/Plans/WIP/`). See
[usage.md](usage.md) for the command line and file formats.

# Command line and files

Every switch is declared in `RedPrism/CommandLine.h`. `--help` prints that catalog.

| Switch | Meaning |
| --- | --- |
| *(none)* | Titled editor window |
| `--warp` | Microsoft Basic Render Driver |
| `--self-test` | Hidden WARP smoke (used by `test.ps1`) |
| `--screenshot <png>` | Capture without activation, then exit |
| `--dpi 96\|144` | Layout DPI for `--screenshot` |
| `--demo transform\|selection\|brush\|retouch\|adjust` | Populate the new document for a `--screenshot` |
| `--bench brush` | Run the brush benchmark (4K document, 40 and 800 px strokes, blank and opaque layers), print `BRUSH BENCH` lines and exit |
| `--help` | Print the catalog and exit 0 |

## Projects

Projects are ZIP `.prism` files or exploded folders (`manifest.json` + `images/`), the project package layout,
format versions 1–7. Everything the Mac app stores round-trips: layers and folders, opacity and blend modes, raster
masks (including masks moved apart from their layer and unlinked masks), clipping masks, adjustment layers with
their Hue/Saturation, Levels, Curves, Exposure, Gradient Map and Grain settings, and shape metadata. Saving writes
version 7. A project the app cannot open (unsupported version, damaged metadata, missing image, over the 30 000 px
/ 100-megapixel limits) is reported and the current document is left untouched.

## Images

PNG, JPEG, TIFF, and HEIC (when Windows provides the HEIF codec) import onto the canvas as a new layer named after
the file; an empty document takes the image size. Export PNG (with transparency) or JPEG (opaque, quality 0.8) from
the File menu.

## Keyboard reference

| Keys | Action |
| --- | --- |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | New, open, save, save as |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | Undo, redo |
| Ctrl+Shift+N / Ctrl+G / Ctrl+Shift+G / Ctrl+J | New layer, new folder, move out of folder, layer via copy (the selection to a new layer, or a duplicate) |
| Delete | Clear the selected pixels; without a selection delete the targeted mask, or the layer (canvas focused) |
| Shift+Delete | Content-Aware Fill: synthesize the selected pixels from the surrounding image of the layer (also on the Edit menu; OK applies one undo step) |
| Ctrl+X / Ctrl+C / Ctrl+Shift+C / Ctrl+V | Cut, copy, copy merged, paste in place as a new layer (another app's image pastes centered) |
| Alt+Backspace / Ctrl+Backspace / Ctrl+I | Fill the selection (or the whole layer) with the foreground / background color; invert the layer or its mask |
| X / D | Swap the foreground and background colors / reset them to black over white (canvas focused) |
| Ctrl+T / Enter / Escape | Free Transform, or Transform Selection with a selection (the selected pixels float and merge back); apply / cancel a pending transform (canvas focused) |
| Ctrl-drag / Ctrl+Alt-drag / Ctrl+Arrows | Move / duplicate the selected pixels with a selection tool; nudge them by 1 (Shift 10) in any tool |
| Arrows / Shift+Arrows | Nudge the layer by 1 / 10 pixels with the Move tool; nudge the selection outline with a selection tool |
| M / L (again) | Switch the Marquee between Rectangle and Ellipse / the Lasso between Freehand and Polygonal |
| Shift-drag / Alt-drag | Add to / subtract from the selection; a fresh Shift during a Marquee drag squares it |
| Enter / Backspace / Escape | Close a polygonal lasso / remove its last corner / drop the outline |
| Ctrl+A / Ctrl+D / Ctrl+Shift+I | Select all, deselect, inverse |
| [ / ] and Shift+[ / Shift+] | Brush size down / up by a fifth; hardness down / up by a quarter (brush tools) |
| 0-9 | Brush opacity 100 %, 10-90 % (two digits within 0.6 s for other percents); the active layer's opacity with the Move tool |
| Shift-click / Escape | Straight brush line from the last stroke / drop the stroke in progress |
| Alt-click | Set the Clone Stamp source; with the Brush, Spot Healing or Gradient, sample a color |
| Ctrl+M / Ctrl+L / Ctrl+U | Curves, Levels, Hue/Saturation on the active layer (the Image menu adds Exposure, Gradient Map, Grain; the Filter menu the blurs, noise and lens correction) |
| Enter / Escape | OK / Cancel the open adjustment panel (canvas focused) |
| Shift+= / Shift+- | Step the active layer's blend mode forward / back |
| Enter / Escape | Apply / drop a pending gradient; Escape also drops a shape draft (canvas focused) |
| Shift+U | Switch the Shape tool between Rectangle and Ellipse |
| Alt-drag / Ctrl-drag handle / Shift | Duplicate while moving / distort / constrain an axis, snap rotation to 15°, free the ratio |
| Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+- | Fit, actual pixels, zoom in, zoom out |
| V M L W C B E J S R G U I H Z A | Tools (canvas focused) |
| Space (held) | Pan |
| Escape | Focus chrome |

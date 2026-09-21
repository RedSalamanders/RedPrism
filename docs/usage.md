# Command line and files

Every switch is declared in `RedPrism/CommandLine.h`. `--help` prints that catalog.

| Switch | Meaning |
| --- | --- |
| *(none)* | Titled editor window |
| `--warp` | Microsoft Basic Render Driver |
| `--self-test` | Hidden WARP smoke (used by `test.ps1`) |
| `--screenshot <png>` | Capture without activation, then exit |
| `--dpi 96\|144` | Layout DPI for `--screenshot` |
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
| Ctrl+Shift+N / Ctrl+G / Ctrl+Shift+G / Delete | New layer, new folder, move out of folder, delete layer |
| X / D | Swap the foreground and background colors / reset them to black over white (canvas focused) |
| Ctrl+T / Enter / Escape | Free Transform; apply / cancel a pending transform (canvas focused) |
| Arrows / Shift+Arrows | Nudge the layer by 1 / 10 pixels with the Move tool |
| Alt-drag / Ctrl-drag handle / Shift | Duplicate while moving / distort / constrain an axis, snap rotation to 15°, free the ratio |
| Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+- | Fit, actual pixels, zoom in, zoom out |
| V M L W C B E J S R G U I H Z A | Tools (canvas focused) |
| Space (held) | Pan |
| Escape | Focus chrome |

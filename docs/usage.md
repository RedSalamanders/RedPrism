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

PNG, JPEG, TIFF, and HEIC (when Windows provides the HEIF codec) open through File › Open, File › Import Images…
(several at once) or a drop on the window. Open and a drop ask whether the image is added as a layer named after
the file above the active layer, or opened as a new project of the image's size (the current project is replaced
after the save prompt); Import Images… always adds layers. A drop of several images asks once. Close Project
(Ctrl+W) leaves the window empty until the next New, Open, import or drop. Export PNG (with transparency) or JPEG
(opaque, quality 0.8) from the File menu.

## Keyboard reference

| Keys | Action |
| --- | --- |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S / Ctrl+W | New, open, save, save as, close project (a modified project asks to save first) |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | Undo, redo |
| Ctrl+Shift+N / Ctrl+G / Ctrl+Shift+G / Ctrl+J | New layer, new folder, move out of folder, layer via copy (the selection to a new layer, or a duplicate) |
| Ctrl+] / Ctrl+[ / Ctrl+Alt+G / F2 / Ctrl+E | Move the layer up / down among its siblings, clip it to the layer below (again releases), rename it, merge down (rename and merge say they are not implemented yet) |
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

## Settings, crash reports and languages

RedPrism keeps its settings in `%LocalAppData%\RedPrism\settings.json` (`settings.debug.json` for Debug builds), a
small JSON document written on exit and readable in any editor: `theme` (`system`, `light`, `dark`), `language`
(a tag such as `fr-FR`, empty for Windows' display language), `layersPanelWidth`, `sampleRing`, `jpegQuality` and
the `window` placement. Edits while RedPrism runs apply as you save the file (the theme, the sample ring, the
panel width, the JPEG quality); the language and the window placement take effect at the next start. A file that
is not valid JSON is left alone while RedPrism runs; at the next start it is backed up as `settings.json.invalid-…`
and replaced by the defaults. Windows high contrast overrides the theme while it is on.

If RedPrism crashes it writes a minidump to `%LocalAppData%\RedPrism\Crashes` and, at the next start, says so and
offers to open the folder. Nothing is sent anywhere; the newest ten reports are kept.

A translated string table goes to `<RedPrism folder>\Languages\<tag>\RedPrism.resources.dll` (a resource-only
DLL built from `RedPrism/RedPrism.rc` with the same ids); the `language` setting or Windows' display language
picks it, and any missing string falls back to English.

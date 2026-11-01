# Command line and files

Every switch is declared in `RedPrism/CommandLine.h`. `--help` prints that catalog.

| Switch | Meaning |
| --- | --- |
| *(none)* | Titled editor window |
| `--warp` | Microsoft Basic Render Driver |
| `--self-test` | Hidden WARP smoke (used by `test.ps1`) |
| `--screenshot <png>` | Capture without activation, then exit (0 written, 8 capture unsupported, 9 capture failed) |
| `--dpi 96\|144` | Layout DPI for `--screenshot` |
| `--demo transform\|selection\|brush\|retouch\|adjust\|canvassize\|crop` | Populate the new document for a `--screenshot` (`canvassize` opens the Canvas Size card, `crop` frames the canvas with the Crop tool) |
| `--bench brush` | Run the brush benchmark (4K document, 40 and 800 px strokes, blank and opaque layers), print `BRUSH BENCH` lines and exit |
| `--bench kernels` | Run the CPU kernel benchmark at 3840×2160 with 10 samples; exits without window or GPU setup |
| `--bench kernels45` | Run the CPU kernel benchmark at 8192×5464 with 10 samples; exits without window or GPU setup |
| `--bench kernels-smoke` | Run the CPU kernel benchmark at 32×24 with 2 samples; this smoke run is not a performance measurement |
| `--bench buffers`, `--bench buffers45` | Compare copy/adopt image-result ownership in alternating pairs at 4K or 45 MP, with identical output checksums |
| `--crash-probe <dir>` | Hidden crash-handler check: crash on purpose and confirm the dump lands in `<dir>` (used by `test.ps1`) |
| `--help` | Print the catalog and exit 0 |

Exit codes: 0 ok, 1 allocation, 2 command line or window, 5 graphics, 8 screenshot capture unsupported, 9 screenshot
capture failed.

## Projects

Projects are ZIP `.prism` files or exploded folders (`manifest.json` + `images/`), the project package layout,
format versions 1–7. Everything the Mac app stores round-trips: layers and folders, opacity and blend modes, raster
masks (including masks moved apart from their layer and unlinked masks), clipping masks, adjustment layers with
their Hue/Saturation, Levels, Curves, Exposure, Gradient Map and Grain settings, and shape metadata. Saving writes
version 7. A project the app cannot open (unsupported version, damaged metadata, missing image, over the 30 000 px
/ 100-megapixel limits) is reported and the current document is left untouched. Growing an open document past that
same 100-megapixel budget while editing (a paste, a duplicate, a new mask, a filter, a merge, a distortion, a
shape) is refused with a notice the same way, and nothing changes.

Long local paths are supported for opening and saving projects and for PNG/JPEG export, within Windows and
filesystem path limits.

## Images

PNG, JPEG, TIFF, and HEIC (when Windows provides the HEIF codec) open through File › Open, File › Import Images…
(several at once) or a drop on the window. Open and a drop ask whether the image is added as a layer named after
the file above the active layer (centered where it was dropped when the drop lands on the canvas, else on the
canvas), or opened as a new project of the image's size (the current project is replaced after the save prompt);
Import Images… always adds layers. A drop of several images asks once. Close Project
(Ctrl+W) leaves the window empty until the next New, Open, import or drop. Export PNG (with transparency) or JPEG
(opaque, quality 0.8) from the File menu. Save, Save As and Export apply anything still open first — a typed
value, a live stroke, a pending gradient or transform, an adjustment panel — so the saved or exported file always
matches what the canvas shows.

Windows may wait briefly for a project save or image export that is already writing before signing out. If an export
is still preparing the canvas, Windows cancels that work.

## Keyboard reference

| Keys | Action |
| --- | --- |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S / Ctrl+W | New, open, save, save as, close project (a modified project asks to save first) |
| Ctrl+Shift+E / Ctrl+Alt+Shift+S | Export PNG / JPEG |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | Undo, redo |
| Ctrl+Shift+N / Ctrl+G / Ctrl+Shift+G / Ctrl+J | New layer, new folder, move out of folder, layer via copy (the selection to a new layer, or a duplicate) |
| Ctrl+] / Ctrl+[ / Ctrl+Alt+G / F2 / Ctrl+E | Move the layer up / down among its siblings, clip it to the layer below (again releases), rename it (double-click its row also does), merge it down onto the layer beneath (a folder merges into one layer) |
| Click a layer's Mask badge / Shift-click it | Target that layer's mask / turn the mask on or off; click elsewhere on the row to target its pixels again (also on the Layer menu: Target Layer Mask, Target Layer Pixels, Link/Unlink and Enable/Disable Layer Mask; none has a default shortcut) |
| Enter / Escape | OK / Cancel the open New Canvas, Canvas Size or Image Size card (canvas focused) |
| Enter / Escape / Alt-drag | Apply / drop the Crop tool's frame; Alt resizes or draws the frame about its center (canvas focused) |
| Delete | Clear the selected pixels; without a selection delete the targeted mask, or the layer (canvas focused) |
| Shift+Delete | Content-Aware Fill: synthesize the selected pixels from the surrounding image of the layer (also on the Edit menu; OK applies one undo step) |
| Ctrl+X / Ctrl+C / Ctrl+Shift+C / Ctrl+V | Cut, copy, copy merged, paste in place as a new layer (another app's image pastes centered) |
| Alt+Backspace / Ctrl+Backspace / Ctrl+I | Fill the selection (or the whole layer) with the foreground / background color; invert the layer or its mask |
| X / D | Swap the foreground and background colors / reset them to black over white; with a mask targeted, switch its Paint between black and white / reset it to black (canvas focused) |
| Ctrl+T / Enter / Escape | Free Transform, or Transform Selection with a selection (the selected pixels float and merge back); apply / cancel a pending transform (canvas focused) |
| Enter / Escape after a click in the chrome | Still apply / cancel the edit under way (transform, crop, gradient, lasso, adjustment panel, size card): the clicked button is not pressed again; a control reached with Tab takes Enter as its press |
| Ctrl-drag / Ctrl+Alt-drag / Ctrl+Arrows | Move / duplicate the selected pixels with a selection tool; nudge them by 1 (Shift 10) in any tool |
| Arrows / Shift+Arrows | Nudge the layer by 1 / 10 pixels with the Move tool; nudge the selection outline with a selection tool |
| M / L (again) | Switch the Marquee between Rectangle and Ellipse / the Lasso between Freehand and Polygonal |
| Shift-drag / Alt-drag | Add to / subtract from the selection; a fresh Shift during a Marquee drag squares it |
| Enter / Backspace / Escape | Close a polygonal lasso / remove its last corner / drop the outline |
| Ctrl+A / Ctrl+D / Ctrl+Shift+I | Select all, deselect, inverse |
| [ / ] and Shift+[ / Shift+] | Brush size down / up by a fifth; hardness down / up by a quarter (brush tools) |
| 0-9 | Brush opacity 100 %, 10-90 % (two digits within 0.6 s for other percents); the active layer's opacity with the Move tool. Holding a digit sets it once, not repeatedly |
| Shift-click / Escape | Straight brush line from the last stroke / cancel the brush stroke while the pointer is down |
| Alt-click | Set the Clone Stamp source; with the Brush, Spot Healing or Gradient, sample a color |
| Ctrl+M / Ctrl+L / Ctrl+U | Curves, Levels, Hue/Saturation on the active layer (the Image menu adds Exposure, Gradient Map, Grain; the Filter menu the blurs, noise and lens correction) |
| Ctrl+Alt+C / Ctrl+Alt+I | Canvas Size, Image Size (Image menu) |
| Enter / Escape | OK / Cancel the open adjustment panel (canvas focused) |
| Shift+= / Shift+- | Step the active layer's blend mode forward / back |
| Enter / Escape | Apply / drop a pending gradient; Escape also drops a shape draft (canvas focused) |
| Shift+U | Switch the Shape tool between Rectangle and Ellipse |
| Alt-drag / Ctrl-drag handle / Shift | Duplicate while moving / distort / constrain an axis, snap rotation to 15°, free the ratio |
| Ctrl+0 / Ctrl+1 / Ctrl+= / Ctrl+- | Fit, actual pixels, zoom in, zoom out (zoom counts device pixels per document pixel: hard pixel edges from 200 %, a grid from 800 %) |
| V M L W C B E J S R G U I H Z | Tools (canvas focused) |
| Space (held) | Pan; types a space in a text field, or presses a control reached by keyboard (Tab) instead |
| Escape | Focus chrome |

With Clone Stamp, after choosing a source with Alt-click, you can press and drag while the
source image is preparing; your movement and release are kept. Zoom remains available during preparation.
The press keeps its current All Layers choice: either the committed composite or the active layer's placed pixels.
After releasing the stroke, hold Space and drag to pan while the source finishes preparing.
Escape cancels while the pointer is down. After release or capture loss, the accepted stroke is kept even if
its source is still preparing. Undo after release waits for the stroke, then undoes it.

## Hue/Saturation sampling

In Hue/Saturation, the range eyedroppers recenter, widen or narrow a color band's range. Transparent and
near-neutral pixels leave it unchanged. Targeted adjustment drags change saturation; hold Ctrl to change hue.
The amount is measured from the original press. Releasing ends the drag without sampling another position.

## Settings, crash reports and languages

RedPrism keeps its settings in `%LocalAppData%\RedPrism\settings.json` (`settings.debug.json` for Debug builds), a
small JSON document written when RedPrism closes or Windows ends the session, and readable in any editor: `theme`
(`system`, `light`, `dark`), `language` (a tag such as `fr-FR`, empty for Windows' display language),
`layersPanelWidth`, `sampleRing`, `jpegQuality`, the `window` placement and `shortcuts` (keyboard shortcut
replacements, below). Edits while RedPrism runs apply as you save the file (the theme, the sample ring, the panel
width, the JPEG quality, shortcut replacements); the language and the window placement take effect at the next
start. A file that is not valid JSON is left alone while RedPrism runs; at the next start it is backed up as
`settings.json.invalid-…` and replaced by the defaults. Windows high contrast overrides the theme while it is on.

Help › Command List (or its search field) finds and runs any command by name and shows its current shortcut. To
change one, add an entry to the `shortcuts` array, such as `{"commandId": "cmd/edit/swapPalette", "ctrl": true,
"vk": "X"}`: `commandId` names the command in its `cmd/...` form (as the Command List would), and the key is
either `vk` (a letter, digit, `F1`-`F24`, a named key such as `Delete`, or `VK_XX`) or `keyPosition`
(`bracketLeft`, `bracketRight`, `numberRowMinus`, `numberRowPlus`, for chords that follow a physical key, like the
brush-size keys); `ctrl`, `alt` and `shift` default to false. A later entry for the same command wins, and it also
takes over that key from whatever else used it. Alt+F4, Alt+Space, F10 and Shift+F10 always stay Windows' own
gestures and cannot be reassigned.

If RedPrism crashes it writes a minidump to `%LocalAppData%\RedPrism\Crashes` and, at the next start, says so and
offers to open the folder. Nothing is sent anywhere; the newest ten reports are kept. Diagnostic runs
(`--self-test`, `--screenshot`, `--demo`, `--bench`) never touch that folder: their crash dumps go to
`%TEMP%\RedPrism.Harness` instead, and `--crash-probe <dir>` writes its one dump to the directory you give it.

A translated string table goes to `<RedPrism folder>\Languages\<tag>\RedPrism.resources.dll` (a resource-only
DLL built from `RedPrism/RedPrism.rc` with the same ids); the `language` setting or Windows' display language
picks it, and any missing string falls back to English.

Project manifests use the format marker `com.redprism.project`. Packages written with a different marker require
explicit conversion before this version can open them. The supported manifest versions and package layout are unchanged.

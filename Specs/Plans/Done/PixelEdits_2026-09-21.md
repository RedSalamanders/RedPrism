# C9b Pixel edits

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md` through the C9 Selection closeout, which
> deferred every edit that needs raster primitives. Index before coding.

- **Status**: DONE
- **Depends on**: C9 (selection coverage), C10 (sparse rasters, `BrushStroke` canvas fills), C11
  (`AddPixelLayer`, `Fill`, `ClearPixels`)
- **Owning specs**: `Document/Document_Tools.md`, `UI/UI_ApplicationChrome.md`, `Rendering/Rendering_Brush.md`

## Outcome

The legacy selection edits (`SelectionEdits.swift`, `FloatingSelection.swift`, `SelectionClipboard.swift`): Fill
with the foreground or background (whole layer without a selection; a mask fills black or white), Delete clears the
selected pixels (or deletes the targeted mask, or the layer), Invert inside the selection or across the layer or
mask, Ctrl-drag and Ctrl-arrow moves of the selected pixels (Alt duplicates) as one entry that moves the outline
too, Ctrl+T with a selection transforming the selected pixels on a floating layer merged back as one entry (Escape
restores exactly), masks from the selection (Add Mask hides the selection, Add Black Mask shows only it), and the
clipboard: Copy, Copy Merged, Cut, Paste in place as a new layer, Layer via Copy, with the system clipboard for
other apps.

## Remaining work

- [x] `BrushStroke` lift / move of the selected pixels and the invert pass; session fill, clear, invert, pixel
      move, mask from selection, delete key and mask deletion
- [x] Floating selection transform on `TransformEdit` (merge on commit, restore on cancel, outline follows the
      handles and a distortion)
- [x] Clipboard model (copy region, selected and merged pixels, paste in place, layer via copy) plus the Win32
      clipboard in the host
- [x] Edit menu, keys and pointer rules; self-test; DocumentTests ported from `SelectionEditTests` and
      `SelectionClipboardTests`
- [x] Spec, docs, plan closeout

## STOP

Pasting through a DIB conversion that loses alpha; moving pixels by re-uploading the whole layer per pointer
update.

## Done

Checklist green, spec updated, plan moved to Done.

## Closeout (2026-09-21)

Shipped on the C10/C11 raster primitives: `PaintCanvas` takes a mode (Over, DestinationOut, Invert) and the stroke
lifts pixels (`LiftSelection`, `MoveLifted`), so fills, Clear, Invert, pixel moves and nudges are `BrushStroke` edits
committed through `CommitStroke` (a moved outline applies inside the same entry); the floating selection transform
rides `TransformEdit::floating` (merge on commit growing the layer and its mask, exact restore on cancel or an
unchanged draft, the outline mapped through the handles or the distortion's homography); masks from the selection
rasterize on the layer's own grid; the clipboard keeps a session copy with its place and mirrors it to the system
clipboard as PNG and a 32-bit DIB (`RedPrism/SystemClipboard.*`), pasting in place from the session and centered
from other apps (PNG preferred, a DIB's alpha honored). The host adds the Edit menu items and keys, the Ctrl-drag
through the stroke preview (adopted on commit, so a move never re-uploads the layer per pointer update), Delete,
Ctrl+T on a selection, Layer via Copy and the mask items, with the Invert, Delete Layer and transform captions
following the target.

Deviations from the legacy app, recorded in the owning specs: the legacy "Delete Layers" for several selected
layers is not ported (the port keeps one active layer); the layers panel has no mask buttons yet, so Add Mask and
Add Black Mask live on the Layer menu; the pixel-move cursor is the four-way arrow (no move-pixels or duplicate
badges); Copy Merged uses the host's flattened readback, so it needs a device. Running the self-test writes the cut
pixels to the system clipboard. Gates: `test.ps1` Debug, Release and ASan Debug, `build.ps1 -Platform ARM64
-Configuration Debug`, `format.ps1 -Check`, `validate-skills.ps1`.

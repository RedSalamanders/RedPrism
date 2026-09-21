# C9b Pixel edits

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md` through the C9 Selection closeout, which
> deferred every edit that needs raster primitives. Index before coding.

- **Status**: ACTIVE
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

- [ ] `BrushStroke` lift / move of the selected pixels and the invert pass; session fill, clear, invert, pixel
      move, mask from selection, delete key and mask deletion
- [ ] Floating selection transform on `TransformEdit` (merge on commit, restore on cancel, outline follows the
      handles and a distortion)
- [ ] Clipboard model (copy region, selected and merged pixels, paste in place, layer via copy) plus the Win32
      clipboard in the host
- [ ] Edit menu, keys and pointer rules; self-test; DocumentTests ported from `SelectionEditTests` and
      `SelectionClipboardTests`
- [ ] Spec, docs, plan closeout

## STOP

Pasting through a DIB conversion that loses alpha; moving pixels by re-uploading the whole layer per pointer
update.

## Done

Checklist green, spec updated, plan moved to Done.

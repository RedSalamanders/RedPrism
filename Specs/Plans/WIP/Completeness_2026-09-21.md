# C16 Completeness

> Spawned on 2026-09-21 from the request that followed C15: "an unimplemented function must display a message
> until the implementation is done; opening an image must work and propose to add it as a layer or as a new
> project; update the spec documents and continue for the complete implementation." Index before coding.

- **Status**: ACTIVE
- **Depends on**: C15
- **Owning specs**: `UI/UI_ApplicationChrome.md` (menus, files, the unimplemented-function rule),
  `Document/Document_Model.md` (session edits), `Document/Document_Tools.md` (crop, canvas size, image size),
  `Testing/Testing_Validation.md`

## Outcome

The shell exposes the whole legacy command surface (archived behavioral reference and the rail); every
exposed function either works as the legacy app does or says that it is not implemented yet; an image opened from
File › Open or dropped on the window asks whether it becomes a layer of the current project or a project of its
own.

## Rules

1. An exposed function with no implementation shows `IDS_NOT_IMPLEMENTED` naming the function
   (`Application::ShowNotImplemented`) and changes nothing else. The stub leaves with the implementation; the
   inventory below and the list in `UI_ApplicationChrome.md` are the record.
2. Behavior follows the legacy oracle; a deviation forced by the single-document shell is recorded here and in the
   owning spec.
3. Every function lands with its spec text, its DocumentTests/IoTests cases ported from the legacy suite that
   covers it, and a self-test case that drives it through the shell.

## Inventory (the legacy surface missing from the port on 2026-09-21)

| Function | Legacy owner | Status |
| --- | --- | --- |
| File › New Canvas… (size sheet, clipboard size suggestion) | `NewCanvasSheet.swift`, `ProjectWorkspace.newCanvas` | stub |
| File › Import Images… (several files as layers) | `EditorSession.importImages` | done |
| File › Close Project (save prompt, empty shell) | `ProjectController.close`, `EditorSession.clearProject` | done |
| "Save changes?" before New, Open, Close Project, Exit | `ProjectController.confirmReplacement` | done |
| An image opened as a layer or as a new project | new (legacy adds a layer on a tab and opens a project per dock drop) | done |
| Image › Canvas Size… (anchor, units, relative, extension color) | `CanvasSizeSheet.swift`, `CanvasResizer.swift`, `CanvasSize.swift` | stub |
| Image › Image Size… (resample, resolution, units, sampling) | `ImageSizeSheet.swift`, `ImageResizer.swift` | stub |
| Layer › Group Selected Layers | `LayerGroups.groupSelectedLayers` | done |
| Layer › Rename Layer… | `EditorSession.renameLayer`, `NativeLayerList` inline editor | stub (the model edit exists) |
| Layer › Move Layer Up / Down (Ctrl+] / Ctrl+[) | `EditorSession.moveActiveLayer` | done |
| Layer › Create / Release Clipping Mask (Ctrl+Alt+G) | `LiveLayerMask.toggleClippingMask` | done |
| Layer › Merge Down (Ctrl+E) | `LayerMerge.swift` | stub |
| Crop tool (C): frame, ratios, snapping, Apply / Cancel | `Crop.swift`, `CropControls.swift` | stub |
| Several layers selected at once in the layers panel | `LayerGroups.selectLayers`, `NativeLayerList` | open, no stub: the panel selects one row and every command works on it |
| Projects in tabs | `ProjectWorkspace.swift`, `ProjectTabs.swift` | out of scope: one document per window; a second project opens in the same window after the save prompt |
| Check for Updates…, Hide, Hide Others, Show All | the macOS application menu | not applicable on Windows |

## Remaining work

- [x] Slice 1 — policy and files: `ShowNotImplemented`, the full menu surface with stubs, the image-open prompt,
      the save prompt, Import Images…, Close Project, Move Layer Up/Down, Group Selected Layers, the clipping
      toggle; specs, self-test, DocumentTests
- [ ] Slice 2 — sizes: `ResizeCanvas` / `ResizeImage` in Common with the legacy `CanvasSizeTests` /
      `ImageSizeTests` goldens; the New Canvas, Canvas Size and Image Size cards
- [ ] Slice 3 — Crop tool: `CropGeometry` / `CropDrag` / `CropSnap` with the legacy `CropTests` goldens, the frame
      overlay, the header, Apply / Cancel
- [ ] Slice 4 — Rename Layer… card; Merge Down through the composition readback
- [ ] Closeout: docs, screenshots, memory, plan to Done

## STOP

Rebaselining a legacy golden to pass; a stub whose message names the wrong function; a stub that changes the
document, the tool or the history; killing `RedPrism.exe`; committing `.build/`.

## Done

Every inventory row is done, out of scope or not applicable; the spec's list of unimplemented functions is empty
or names only recorded follow-ons; the gates are green.

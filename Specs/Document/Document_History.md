# Document history

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Common/DocumentHistory.h`, `Common/DocumentHistory.cpp`

## Scope

This contract owns undo/redo for `CanvasDocument`. It does not own tools, I/O, or GPU tiles.

Independent history expectations are retained in the document tests.

## Budget

Default `entryLimit` is **100**. Default `retainedByteLimit` is **256 MiB**. Limits are clamped to ≥0 at
construction. `retainedBytes(current)` counts unique raster storage that appears only in history snapshots
(before/after), excluding images still referenced by the live document, using pointer identity of `Raster`
storage (Swift `ObjectIdentifier` of `CGImage`). Thumbnails count separately when they are distinct storage.

## Transactions

`Begin(name, document, selection)` / `End(document, selection)` nest. Only `depth == 0` records an entry.
If the ended document equals the pending before-document, **do not** push an entry and **do not** clear redo
(selecting, navigating, and no-op edits preserve redo). A real change assigns a new revision, appends the
entry, and clears redo, then trims.

Undo/redo are ignored while `depth != 0`. Undo restores the entry’s before snapshot (document + active layer
id + revision). Redo restores after.

A pending edit that is not a history entry yet (gradient, transform, brush, warp, pixel move, shape draft, crop
frame) is dropped by the first Undo and by the first Redo, and history does not move on that call. An open color
panel also consumes the key: history stays put until OK or Cancel. The host drops any composition stroke in the
same command. `ConfirmDiscardChanges` settles a live stroke before it reads `IsModified`, so Save writes that
stroke and a clean document plus a stroke still asks.

`MarkSaved` stores the current revision. `IsModified` is revision ≠ saved revision. `Reset` clears stacks and
starts a new saved revision.

## Trim

While `past.size + future.size > entryLimit` **or** retained bytes exceed the budget, drop the oldest past
entry, else the oldest future entry. A 256 MiB trim test MUST exist (`history.trim-256mib`).

## Validation

`Tests/DocumentTests` covers nested transactions, no-op rename/empty name preserving redo, entry-limit trim,
shared live rasters reporting 0 retained bytes, the 256 MiB unique-pixel trim, and `pending.*` (Undo/Redo drop a
live brush, gradient, warp, pixel move and crop frame without moving history; Cut on a hidden layer or under an
open Levels panel leaves the clipboard and the pixels; a paintable Cut clears; a float aborts when the clear
paints nothing). The WARP self-test covers Ctrl+Z during a stroke and the save prompt committing that stroke.
Debug and ASan Debug `test.ps1` MUST run `DocumentTests.exe`.

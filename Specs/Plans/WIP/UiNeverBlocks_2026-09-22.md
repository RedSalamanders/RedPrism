# The UI thread never waits on a filter or any other action

- **Status**: ACTIVE
- **Priority**: P0
- **Planned at**: 2026-09-22, after the splitter drag fix
- **Owning specs**: `Core/Core_PerformanceAndResources.md`, `Document/Document_Tools.md`, `UI/UI_ApplicationChrome.md`,
  `Rendering/Rendering_Composition.md`

## Why

A Gaussian blur, a levels commit, a content-aware fill, a merge, or a project save runs to completion on the UI
thread before the window can paint, move, or cancel. The message pump is stuck for the whole kernel. The performance
contract already forbids busy-polling and requires idle windows to block on events. It does not yet say that a
command must return to that pump within a frame.

Modal file dialogs (`IFileDialog`) stay as they are: Windows owns that loop. Everything RedPrism computes itself
must not.

## Rule

The UI thread only pumps messages, applies a result that is already finished, and presents. Any document or image
operation that can exceed one frame runs off that thread. The latest request wins. Cancel, Escape, and closing the
panel abandon the in-flight work and leave the document as it was when the command started. A finished result is
applied on the UI thread as one history entry, or dropped if the document changed underneath it.

## What blocks today

| Work | Where it runs now |
| --- | --- |
| Levels, curves, HSV, exposure, gradient map, grain preview and OK | `DocumentSession::CommitLevels` and the preview path in `AdjustEdit.cpp`, `Adjust::ApplyAdjustment` |
| Gaussian blur, motion blur, noise, lens, content-aware fill | `DocumentSession::CommitFilter`, `ImageFilters::*`, `Adjust::RunFilter` |
| Blur and clone stroke start | `DocumentSession::BlurSample`, `CloneSample` (full-canvas) |
| Spot healing commit | `BrushStroke::Heal` |
| Merge | composition readback then `MergeLayers` |
| Open, save, import | `LoadProject`, `SaveProject`, `ImportImageOnto` on the command path |

Goldens in `DocumentTests` call those kernels directly. They stay on the test thread. The host path is what moves.

## Shape

One worker, not a pool per command. The UI thread pushes a job and returns. The worker runs the job. Completion is
posted to the window and applied on the UI thread. A newer job for the same target cancels the older one. The
panel shows that work is running and stays movable; OK and Apply stay disabled until the result is ready, or they
start the job and the entry appears when it finishes. Status text names the action.

Do not poll. The worker blocks on the next job. The UI thread blocks on the message queue, as it does when idle.
No `catch (...)`. `std::bad_alloc` stays fatal. The posted payload is owned until the UI thread takes it, and a
destroyed window drops it.

## Slices

1. Measure the commands above on a 4K layer and record which exceed 8 ms. That list is the queue. A command already
   under 8 ms stays synchronous.
2. Add the job queue and the UI completion message, with a test that a slow stand-in job does not prevent a paint
   message from being dispatched, and that cancel delivers no history entry.
3. Move filter and adjustment preview and commit onto the queue. Preview coalesces to the latest slider value.
   `DocumentTests` goldens stay direct calls.
4. Move merge, import, save, blur/clone sampling, and heal the same way. Save still writes the settled document;
   a save started while a filter job is running waits for that job or cancels it first, on the worker, not by
   stalling the pump.
5. Spec the rule in `Core_PerformanceAndResources.md` and the panel behavior in `UI_ApplicationChrome.md`.

## STOP

- A modal progress dialog that nests a message loop.
- Running the kernel on the UI thread "just this once" for OK.
- Rebaselining a golden because the worker reordered floating-point work. The result must match the direct call.
- A second D3D device for the worker. GPU work stays on the UI thread's device; only CPU kernels move.
- Killing `RedPrism.exe`.

## Done

A 4K Gaussian blur OK returns to the pump immediately, the window still drags and paints, Escape leaves the layer
unchanged, and Enter after the job finishes records one history entry whose pixels match `DocumentTests`.

# Worker-backed color panel host — 2026-09-28

Status: P1.1c.2 complete on x64; ARM64 cross-builds passed

## Change

The production Application now opens deferred color panels. It submits preview and opening-histogram jobs with
the P1.1c.1 immutable inputs, publication checks and preparation reservations. One running preview is followed by
only the latest pending settings; compatible prepared sources can be reused. Histograms remain valid across ticks.
Cancel/reopen, preview off, identity, document replacement and shutdown invalidate the appropriate tickets. Worker
code never accesses UI/document owners. Completion publishes outside dispatch; failures use deferred UI work.

The UI benchmark now rejects readiness while a newly invalidated frame remains outstanding, preventing a frame
presented before completion from counting as the completed result.

## Evidence

The WARP self-test holds the actual histogram worker on an owned event and proves that the shared pump handles a
posted message and Zoom In command before the histogram can return. Its four gray input pixels give an independent
histogram expectation. A second event holds a preview across thirteen settings ticks and proves exactly two worker
executions and the final rendered sequence. A third lets the old worker finish pixels before Cancel/reopen, then
drains and proves those successful old pixels cannot cross into the new panel. Gates have ten-second failure bounds;
no assertion depends on this machine completing image work within an elapsed-time target.

The complete x64 Debug, Release and ASan Debug test runs passed, including WARP self-tests, the sanitizer
detection probe and harness screenshots. Skill validation and the formatting check passed. All three ARM64
configurations cross-built successfully; native ARM64 execution remains unqualified. The older color harness
now observes worker results with the shared test pump before checking numeric pixels or a failed fill; production
does not use that settlement path. Logs use
`.build/logs/color-host-x64-{Debug,Release,ASan-Debug}-tests.log` and
`.build/logs/color-host-arm64-{Debug,Release,ASan-Debug}-build.log`.

## Limits

Commit and dependent Save/Close/import continuations are the next atomic migration. Existing GPU readbacks,
history work and other P1 operations can still block. G1 and Photoshop comparisons remain open. ARM64 runtime
qualification requires a native host; cross-compilation does not establish it.

# Application continuation source bridge — 2026-09-30

Status: initial Application panel bridge qualified; production caller migration remains open

## Changes

Application registers one stable borrowed UI-action source and explicitly unregisters it before scheduler stop
and join. The initial bridge owns numeric panel OK inputs through its eight-entry queue and one active entry.
Its construction/admission boundary returns an allocation-free failed receipt on `bad_alloc`; Full refuses the
ninth request locally. Production UI callers are still inline and do not call this bridge yet.

Selection and snapshot preparation occur on a deferred UI action. A waiting commit consumes readiness; exact
acquisition completion stores its result/error and requests a later action. Apply/drop and history finish precede
terminal receipt publication. `ResumeHostHistory` is the common finisher notification after apply/drop and barrier
release. The color finisher calls it now; other operation migrations must wire their own completion boundaries.
Queued cancellation retires each entry; active cancellation retains its input and barrier until acknowledgement.
Shutdown unregisters and retires observer results before joining independently owned immutable worker inputs.

The color engine preserves the actual runner/admission error for terminal publication. Existing injected and
throwing allocation-failure tests now assert `E_OUTOFMEMORY`, as does the new actual host receipt test.
Typed-size capture tests additionally pin relative mode, image original dimensions, unit, lock and sampling,
each different from defaults. Receipt counts are reported per HostTests invocation; ASan runs normal and stress.

## Evidence

The primary owns host integration and its WARP tests. A cheaper agent reviews the source lifetime, wake-up and
ownership transitions while configuration qualification runs. The actual source starts a worker with the ordinary
UI-action queue full, remains disarmed behind its event gate, rejects a mismatched acquisition, and publishes
Succeeded only on a later UI action after the Gamma 2 pixels/history have applied. The pixel expectation is the
independent literal square-root reference. Failure/cancellation preserve original pixels/history and the panel.
Tests use event gates and a fixed failure deadline; no timing threshold asserts performance.

Primary verified and addressed two review gaps: receipt/idle coverage for immediate adjustment metadata settlement
and a common wake-up boundary for future non-color history work. Tests use an actual independent color commit,
plus explicit finisher seams for all four other history kinds; they prove notification routing without claiming
those future kernels/callers have migrated. Additional event-gated tests compute successful pixels before Cancel
or replacement and prove acknowledged drop precedes terminal publication. Queued panel/document lifetime tests
remain distinct from active worker lifetime checks.

Primary review also caught cancellation during the interval after acknowledged apply/drop but before deferred
receipt publication. Panel OK has no remaining work then, so Cancel must preserve the known success/failure and
its effects. New event-gated tests hold publication until acknowledgement, call Cancel, and verify Succeeded with
one history entry or Failed/E_OUTOFMEMORY with unchanged pixels/panel. The final matrix is rerun after this fix;
earlier green runs are not its qualification.

## Validation

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Skill validation and formatting | Passed: 15 skills, 208 source files | Matrix preflight |
| x64 Debug full tests | All tests passed, including WARP host cases and existing goldens | `host-continuation-x64-Debug-tests.log` |
| x64 Release full tests | All tests passed | `host-continuation-x64-Release-tests.log` |
| x64 ASan Debug full tests | All tests passed, including real sanitizer detection, stress and WARP | `host-continuation-x64-ASan-Debug-tests.log` |
| ARM64 Debug cross-build | Passed, pinned dependency provenance verified | `host-continuation-arm64-Debug-build.log` |
| ARM64 Release cross-build | Passed, pinned dependency provenance verified | `host-continuation-arm64-Release-build.log` |
| ARM64 ASan Debug cross-build | Passed, pinned dependency provenance verified | `host-continuation-arm64-ASan-Debug-build.log` |

No compiled source changed during the final matrix after the acknowledged-cancellation fix. Builds were serial;
no benchmark ran concurrently with a build. ARM64 qualification is cross-compilation only.

## Limits

This is the first panel-request bridge, with fixed payloads. The staged prompt/path/settle/write/discard flows,
variable-length input byte bounds, command/callback guards, forced three-way answers and the integrated caller
effect matrix remain open. Production OK and dependent callers activate together after that work qualifies.
GPU/IO/history blocking migrations and P2–P6 remain open. No native ARM64 runtime, RAW decode, whole-host latency
or Photoshop speed comparison was performed in this checkpoint.

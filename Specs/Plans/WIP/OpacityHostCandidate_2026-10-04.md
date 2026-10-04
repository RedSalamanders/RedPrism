# Private immutable layer and typed opacity host candidate

## Implementation checklist

- [x] Preserve the reviewed Common, application, document suite and fixture source from the completed private archive.
- [x] Independently check all 500 copied files by length and SHA-256 against their archived inputs.
- [x] Preserve the private Debug, Release and ASan application qualification and all preceding assertions.
- [x] Preserve the private document suite qualification in the same three configurations.
- [x] Preserve the captured-settings brush core and opening observer from refreshed Debug/Release/ASan proofs; same 321 core/test inputs and all 6,226 controls/profile.
- [x] Preserve canvas Down/Move/Up, pointer loss, exact-opening cancellation, competing presses, captured mask palette and admitted tool/target/save boundaries from the combined private Debug/Release/ASan application qualification.
- [x] Preserve basic lifetime preflight and its 160 cancellation/80 accepted Save/discard/write-failure controls from the full private Debug/Release/ASan application qualification.
- [ ] Migrate remaining pointer and lifetime opacity closure callers.
- [ ] Move adjustment and display record construction and retirement to admitted workers.
- [ ] Qualify the complete native caller migration with the full x64 suites and ARM64 cross-builds.
- [ ] Measure admission, capture, publication, history and frame costs before claiming G1.

Status: experimental source checkpoint on `codex/p1-opacity-host-proof`, based on `7a0db7c`.
This branch preserves work for review and continuation. The native working branch retains vector layer storage.
This candidate has not completed the integration checklist and is not a completed P1 release step.

The [lifetime checkpoint](../../TestRuns/P1DeferredCanvasLifetime_2026-10-04.md) preserves the nonmutating
prompt/output preflight, exact detached ordinal and original-stream acknowledgement. Modal and broader shutdown
qualification remain open; native activation and the fresh normal configuration matrix remain required.

## Source and qualification

The checkpoint copies only owned source under `Common/`, `RedPrism/`, `Tests/DocumentTests/` and `Tests/Support/`.
It omits private proof projects, generated products, seed receipts and machine-specific private DxUi imports.
The normal pinned dependency restore/build entry points remain in this worktree. Consequently, qualification of
the archived private workspace is not qualification of a fresh build of this worktree or of the full native suite.

The private workspace is `%LocalAppData%\RedPrism\PrivateWork\p1-opacity-host-proof`.
Qualification archives live under `%LocalAppData%\RedPrism\QualificationArchives`:

| Application archive suffix | Completed receipt SHA-256 |
| --- | --- |
| `p1-opacity-host-application-r6-Debug-qualified-private-full-application` | `8A8ED4610635008695F56715BEDC7984CE8A1C1FE9B6D0758FDFC5691DFA1DE9` |
| `p1-opacity-host-application-r7-Release-qualified-private-full-application` | `50C5B614092E6FFC1044C472416A45114439F8A207AFE47E2B90DE3F9A496FBE` |
| `p1-opacity-host-application-r8-ASanDebug-qualified-private-full-application` | `43413832DB8B7A527569A050324C35F2D7CE776924444B302F6A15477D855393` |

Each actual application run exits 0 with 66 stages, all 7,585 preceding application checks, all 139 metadata checks,
31 typed opacity checks and 36 preserved-context/reopened-opening checks. The complete document suites pass
6,226 checks each. The [qualification record](../../TestRuns/DocumentRecordRoots_2026-10-03.md) keeps failures,
repairs and core archive pins. No RAW decoder, SIMD/GPU throughput, Photoshop comparison or G1 deadline is claimed.

## Known next integration requirement

The [captured brush core record](../../TestRuns/P1CapturedBrushCore_2026-10-04.md) qualifies the explicit settings/
mask-palette BeginBrush overload, opening observer and exhaustion refusal. The existing overload forwards the
current settings. The [combined caller checkpoint](../../TestRuns/P1DeferredCanvasCallerCheckpoint_2026-10-04.md)
now preserves the separately qualified application route on this experimental branch. Native integration remains open.

A canvas press must own its document lifetime, target, tool, modifiers and event-time coordinates before waiting
for opacity closure. Bounded storage must retain the release or pointer-loss outcome even at capacity. Replay must
preserve input order, avoid later viewport/keyboard/target reinterpretation and yield through the owner pump.
Admission refusal or failed closure must have an explicit terminal outcome; it must not silently lose a press or
leave a gesture live. View input remains responsive while document mutation waits.

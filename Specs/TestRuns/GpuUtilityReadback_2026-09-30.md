# GPU utility readback qualification — 2026-09-30

## Scope and result

Separate utility composition, tiled worker readback, Merge and adjustment-editor opening pass the complete
configuration matrix on the source following `a384df4`. This closes a P1.3 slice. Export, Copy Merged, wand,
clone and sampler consumers, upload preparation and real-loop G1 measurements remain open.

The source/project/script freeze contains 266 files in `.build/p1-utility-source-hashes.json`. Hashes were checked
before each subsequent configuration; the final check found zero changes. Domain specifications and checklists
were reconciled after source qualification.

## Executed gates

| Gate | Result | Local evidence |
| --- | --- | --- |
| `validate-skills.ps1` | PASS, 15 skills | `.build/p1-utility-skills.log` |
| `format.ps1 -Check` | PASS, 226 files | `.build/p1-utility-format-check.log` |
| x64 Debug `test.ps1` | PASS | `.build/p1-utility-x64-Debug.log` |
| x64 Release `test.ps1` | PASS | `.build/p1-utility-x64-Release.log` |
| x64 ASan Debug `test.ps1` | PASS, stress and real detection probe | `.build/p1-utility-x64-ASanDebug.log` |
| ARM64 Debug cross-build | PASS | `.build/p1-utility-ARM64-Debug.log` |
| ARM64 Release cross-build | PASS | `.build/p1-utility-ARM64-Release.log` |
| ARM64 ASan Debug cross-build | PASS | `.build/p1-utility-ARM64-ASanDebug.log` |

Existing legacy numeric document/composition/I/O goldens, compiled ISA controls, crash probes, hidden WARP
self-test, UI smoke, harness captures and offline RAW corpus checks remain green. The RAW checks verify inputs
and licenses; they do not qualify decode or develop. Native ARM64 runtime is unavailable and is not claimed.

## New runtime evidence

- 130 `gpu-work.*` checks in normal Debug, including explicit EnqueueSetEvent execution on the supported WARP
  device. The fallback copies a single pixel and exercises cancellation and old-device lent-row retention.
  Production still selects the conservative supported completion route.
- 77 `document-readback.*` checks. Serial coordinate formulas establish exact full, cropped and 1×1 BGRA bytes;
  input snapshots survive removal of the caller's records. Tiles retain one reservation and publish one adopted
  raster only at completion. Owner messages run while utility work is pending. Display resources can be dropped;
  settled display composition counts and pixels remain unchanged by utility recording.
- A 12×9 folder/clipping/mask/nonidentity-adjustment fixture is byte-identical to the committed synchronous
  traversal across target tile seams. This preserves the existing numeric reference; it is not a general promise
  of bit identity between arbitrary GPU hardware and CPU kernels.
- Actual replacement WARP devices prove full graph restart and publication from the current generation. Three
  replacements fail with `E_ABORT` after two restarts, with no partial output and no leaked reservation, lent slot
  or utility owner. These are deliberate rebind tests, not physical device-removal fault injection.
- The real application WARP self-test holds a Merge worker while its mapping is lent. It proves Merge before a
  younger Invert, exactly one merge history entry, acknowledged Cancel, injected `E_ACCESSDENIED`, stale-opacity
  refusal and successful device-rebind restart. Hidden completion drops display targets and performs no Present.
- Prepared Merge tests prove trim origin/extent, thumbnail ownership, direct-wrapper pixel equivalence, plan/name/
  parent/canvas/missing-layer refusal, undo/redo and no output or document mutation after pre-cancellation.
  Production cancellation reaches the trim scan and crop, as well as copied rows.

Initial validation caught transformed sampling guard-room errors at tile seams. The asynchronous traversal now
uses at most 2048 pixels or half the smaller device/test texture limit, matching the synchronous guard policy.
Old harness observations were changed to await acknowledged asynchronous results. GPU readiness fixtures use a
deterministic memory source so unrelated OS pressure notifications cannot masquerade as GPU readiness.

## ISA parser qualification

The new Debug `std::variant` dispatcher uses an observed RAX image base, frame-relative RCX index, DWORD load
through RAX + 4×RCX, ADD RCX/RAX and JMP RCX. The scanner proves that exact contiguous register flow, the owning
contribution's anonymous 16-byte untyped PDB data start, decoded targets and complete table boundaries.
No runtime object/function allowlist entry was added. All 72 synthetic parser controls pass, including corrupt
registers/sequences/symbols/targets and real VEX next to a valid table. All five native images and intentionally
failing compiled controls pass their expected outcomes in each x64 configuration.

## Bounded positional-path slice

Command-line tests qualify ordered positional paths, the `--` marker, count/length/null/empty refusal, diagnostic
mode conflicts, marker-aware mode preflight and unchanged parse output on failure. Startup copies borrowed argv
views into an admitted owning request before queuing against the initial document's serial. The actual file
classification/decode/import jobs remain P1.7 work; this parser/handoff slice does not close file responsiveness.

## Remaining limits

First raster uploads and lookup preparation in utility recording still need the later P1 migration/optimization.
No new absolute speed target, UI latency distribution, Photoshop comparison or RAW speed claim is established by
these correctness gates. Application-wide nonblocking processing and G1 remain open in the master checklist.

# Deferred color panel computation — 2026-09-28

Status: P1.1c.1 complete on x64; all ARM64 configurations cross-compile

## Implemented boundary

The session can open a panel in deferred mode: after earlier interactions settle, begin/update retain immutable
inputs and growth metadata. Worker functions prepare padding and downscale sources, compute previews and measure
the opening histogram. The default direct API runs those same functions inline. Histogram results use the opening
lifetime; preview reuse requires the exact current settings sequence. An intermediate frame can display while newer
settings wait, without replacing those settings or their grid plan. Off/identity invalidate earlier frames even
after preview is enabled again. Preparation memory is reserved by stage, using preview dimensions for render scratch.

Selection bounds now live with their immutable geometry, making bounds and empty checks constant-time. Finite-point
and degenerate semantics, shared copies and geometry equality remain unchanged. Sampling a source pixel uses a private
region read rather than assembling a full shared sparse image. The host sampling job migration remains later work.

## Review and tests

A cheaper agent prepared the bounds patch and another wrote the deferred-state tests. The primary reviewed both,
fixed ignored cancellation return values, and added destructive-target and adjustment-layer histogram checks.
The primary owns the worker preparation, memory estimates and publication logic. Existing Debug document goldens
passed immediately after the computation split. New deferred-state tests also passed before the final target checks
were added; the final matrix below includes those checks.

The first full Debug run passed document, host, I/O and WARP tests, then the UI benchmark smoke rejected Levels
opening because its readiness predicate required an unused downscaled preview source. Identity Levels displays
the original and now prepares a smaller source only when settings need one. The benchmark still requires the
opening histogram and the original image identity, plus its presentation/completion gates; the unnecessary source
allocation is no longer a readiness requirement. This preserves the user-visible operation being measured.

The first Release link exposed a second validation issue: linear disassembly decoded an anonymous inline DWORD
switch table in the pinned DxUi accessibility object as `vpinsrb`. Its matching PDB marks the data start, and the
decoded R8-base load/add/jump identifies the table. The ISA scanner now recognizes those exact register-consistent
forms and requires either the existing return boundary or matching anonymous PDB data ownership. Every entry still
targets a decoded instruction and the entire exempt row must fit the verified table. No object is newly allowlisted.
New parser controls reject wrong data ownership/start, mismatched add/jump registers, absent markers, invalid targets,
cross-contribution spans and adjacent executable VEX instructions. The actual Release image passes this proof.

Cases include literal four-gray-pixel histogram bins and clipped Levels pixels, metadata-only sparse capture,
settings ticks during histogram work, old preview/new commit, exact reuse, off/on, Cancel, reopen, replacement,
grid cache changes, memory peaks, target movement, changed selection and adjustment-layer parameter changes.
The direct legacy goldens remain required. Tests assert state and pixels, not machine-dependent elapsed times.

## Gates and limits

Final x64 Debug, Release and ASan gates passed, including all 43 `coloreditdeferred` checks, retained numeric
goldens, WARP self-tests, the 15-operation UI benchmark smoke and harness screenshots. ASan also passed the
concurrent raster/worker stress and independently detected the intentional heap-use-after-free probe. The ISA
controls include 28 inline-table cases and the compiled positive/negative fixtures. ARM64 Debug, Release and
ASan Debug cross-builds passed. Skill validation, format check and whitespace check passed. Logs use
`.build/logs/color-panel-work-x64-{Debug,Release,ASan-Debug}-tests.log` and
`.build/logs/color-panel-work-arm64-{Debug,Release,ASan-Debug}-build.log`.

Production preview/histogram submission, ordered commit/continuation flows and busy controls follow this boundary.
This slice alone does not establish G1 or a Photoshop comparison. Native ARM64 runtime qualification remains
unavailable on this host.

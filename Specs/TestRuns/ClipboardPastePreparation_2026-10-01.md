# Detached clipboard Paste and availability preparation

Status: detached core and availability qualify in all six profiles. Production host callers remain open.

## Private proof

Detached Paste passes 62 controls and all 92 existing Copy/Cut controls. Primary review corrects sibling indices,
an undeclared fixture variable, a mask-target case without a mask and an invalid large-raster fixture. It adds
negative half-offset centering, exact 100 M aggregate equality with untouched sparse storage, no-active-layer
placement, empty/overflow refusal and history-epoch staleness. Preserve all 128 inputs and seven artifacts in
`.build/p1-clipboard-paste-draft/private-proof-r3/`.

Clipboard availability passes 39 controls alongside 25 receipt, 45 fingerprint, 34 binding and 125 transfer
controls. Primary review corrects reversed BGRA fixture expectations and adds deliberate post-close replacement.
The initial runtime has 13 failures cascading from fixture writes denied by external clipboard listeners;
preserve all 140 inputs and ten artifacts in `.build/p1-clipboard-availability-probe/runtime-failure-r2/`.
Bounded one-shot fixture waits correct contention handling; the injected Busy control still calls once.
The corrected proof preserves all 140 inputs and 13 artifacts in `private-proof-r3/`. No production retry
policy or expectation is weakened.

## Native activation and limits

After qualified capped-encoder commit `ca51108`, verify all 462 native input hashes and both private proof
manifests before copying the exact proved modules and controls. Register Paste in DocumentTests and availability
in IoTests; compile the new core module across consumers. The adapter also refuses movable global blocks above
the hard encoded cap or with more than 64 KiB exposed padding, before clipboard mutation. This bound is required
by the future complete graph planner.

The availability record is populated only after successful close and cancellation validation, even when no
format is valid. No offered image permits private fallback; malformed offered images do not. Enumerate and read
only the producer's first DIB variant. Invalid/over-budget bytes never request a synthesized alternate.
Origin proof still hashes owned bytes and uses the
held sequence and receipt binding, never a later owner-window sequence.

The r2 frozen matrix passes x64 ASan Debug, Release and Debug tests, then ARM64 ASan Debug, Release and Debug
clean cross-builds. Its process exits zero after verifying all 468 input hashes. Each x64 suite passes 62 Paste,
45 availability and the unchanged 125 transfer controls. The real sanitizer probe, worker crash, WARP, UI smoke
and harness screenshots pass; all five Release image ISA scans and 434 independent parser controls pass.
Fifteen skills and 417 formatted source files validate. Preserve all 468 physical inputs and 45 evidence files
under `.build/p1-clipboard-paste-r2-qualified/`, including the five Release executables/PDBs, DLLs, provenance,
all ISA reports/disassembly and six profile logs.

Release app version: `0.1.138.0`; SHA-256:
`333A3C2926D508B388EF213737D75030692486114B9953DB5C76A6BAD8DE2488`.
Freeze SHA-256: `6BE55CA20C7B69B4AB5944332AD5819E6797E44CFE4817FF34B95AA45CA9167B`.

Production Copy/Cut/Paste/Copy Merged callers,
full reservation accounting, shutdown/delayed rendering, G1, native ARM64 runtime and Photoshop performance are
not established by private or compile-only proof. Owner capture still copies layer metadata.

## First native failure and correction

The 468-input r1 matrix passes x64 Release and Debug, then ASan IoTests fails inside Windows
`GdiConvertBitmapV5` during `GetClipboardData(CF_DIB)`, before RedPrism can validate the returned block.
The existing hard-pixel-limit fixture offers a short oversized V5 input. The newly added alternate-format
fallback requests Windows' synthesized DIB after rejecting that source. Preserve all 468 physical inputs and
25 binary/PDB/DLL/log/scan artifacts in `.build/p1-clipboard-paste-r1-ASan-failure/`.

The correction follows [Microsoft's enumeration order](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats):
read only the first DIB representation. Native read-stage controls prove no alternate request after malformed
or over-budget input, and a producer-first plain DIB preserves its literal pixels without V5 conversion.
The existing 125 transfer expectations and sanitizer instrumentation remain unchanged. The 39-control private
proof predates this correction; its alternate-DIB fallback expectations are superseded, not claimed qualified.
The separate r2 frozen matrix starts with ASan and passes every configuration, as recorded above.

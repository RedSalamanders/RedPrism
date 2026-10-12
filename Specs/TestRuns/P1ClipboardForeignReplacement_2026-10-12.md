# P1 clipboard foreign replacement private run — 2026-10-12

Status: **integrated; changed-source Release analysis, full Release, full Debug, real ASan and ARM64 cross-builds qualified**. See the [native receipt](P1OpacityClipboardNativeQualification_2026-10-12.md). This record does not close F5, P1, or G1.

## Evidence identity

The run was captured under `.build/P1Drafts/ResponsiveStartupClipboard-r7-Main/actual-startup-host-Release-r7/`. `qualified.json` reports `PASS`, preserves all original records in order, and binds origin `D69F9A3E11FFA6622C8C66D65D5C7E0EFB7890690F48197252DF9EE45FE84E12` to private manifest `64477F3E210CEFB403E32E76181520EE865E449F3F6B63F8D5575E8D06983264`.

| Process | PID | UTC interval | Result |
| --- | ---: | --- | --- |
| Release x64 MSBuild | 62748 | 2026-10-12 03:30:12.075539–03:33:32.648159 | exit 0; stdout SHA-256 `56A77CD0936FE6C12CBDE4250EB334CD068886216A98D90C4F311DA05DD9D2D8`; stderr SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `RedPrism.exe --self-test --warp` | 25936 | 2026-10-12 03:33:46.467332–03:34:58.080609 | exit 0 on inherited default desktop; stdout SHA-256 `615BAAEE2156042AA416496D2B6F98324334CB1AF62852B53D60E69A2D222D10`; stderr SHA-256 `8089BFCCF26506872FFF240F548565376B3A979D07CC49AEF78E1767F99C9863` |

The complete hidden WARP application run retained 7,399 original stdout pass records and 7,556 original stderr pass records, and added 11 clipboard, 34 timing, and 18 guard records. The stage roster grew from 131 to 133, preserving the predecessor order and multiplicity. The pre-runtime protocol binds reader SHA-256 `AB00803A7FCB00C2EE76B6B9E00810E4080236216237CF6F9237BC79180AFCA1`. Main independently ran its reader and reported PASS, report SHA-256 `0912E0D8311A4721274CC78704DF94E2B88A82A545DCF89ABD868B9485D448D4`.

## Foreign replacement scenario

The inspected draft source is `Source/RedPrism/ApplicationClipboardTests.cpp`, mode `ForeignReplacement`, scenario `copy-foreign-replacement-after-close`. It creates and validates a distinct message-only clipboard-owner HWND, keeps it alive through the later real Paste, and installs an independently authored 2×1, 32-bit BI_RGB DIB. The fixture checks allocation and lock, opens and empties the clipboard, places `CF_DIB`, and closes it; the r7 output records `installed=1 error=0 nativeStage=6 windowValid=1 closed=1`.

While the worker is held, the test requires one private publication and a still-pending receipt after clipboard close. After foreign replacement, it requires the private receipt's sequence to differ from the live clipboard sequence, then issues a real Paste. Its independent literal expectations are:

- DIB pixels in BGRA order: `(13, 27, 41, 255)` and `(59, 73, 89, 255)`.
- Pasted image: 2×1, placed at origin `(3, 3)` on the 9×7 fixture canvas.
- Document/history: two layers, no selection, one `Paste` undo entry, no redo, unchanged private clipboard serial, and original-ticket refund/worker-retirement acknowledgement.

All 11 clipboard additions pass in the r7 stdout. No assertion was weakened to obtain the pass.

## Preserved fixture failures

The r5 and r6 private attempts remain failed evidence. Their stdout records failed replacement/pixel assertions and shows the later Paste still returning the original 1×1 private copy `(20, 40, 80, 255)` at `(1, 0)`. r6 records `installed=0 error=5`; its application PID was 62832, exit 5, from 2026-10-12 03:25:39.147417 to 03:26:32.691444 UTC. The native API call responsible for error 5 was not established in r5 or r6, so no narrower cause is claimed. r7's distinct held owner fixture resolves the observed installation failure without changing the literal expectations.

## Closeout checklist

- [x] Private complete hidden WARP Release host pass, predecessor records and stage roster preserved.
- [x] Independent foreign DIB replacement followed by actual Paste with literal pixels, placement, history, and retirement checks.
- [x] Native integration, with ordinary startup still test-gated.
- [x] Changed-source Release analysis r926 and full Release r927.
- [x] Changed-source full Debug r928.
- [x] Changed-source real ASan and ARM64 cross-builds.
- [ ] F5, overall P1, and G1 remain open.

The run's own scope explicitly excludes G1, presentation, and outline-worker activation. The contract's full native qualification and independent production timing/activation requirements remain separate.

Main subsequently integrates the clipboard control together with the checked opacity guard and owner diagnostic
into the native worktree; the native ordinary startup gate is preserved. Integration receipt `A8EFF557…` binds
17 changed files. Fresh native binding `5D84694D230E9C0AC94188224F890D7DD95D093299C63CCCFFD34F6B2A8416B8`
covers 1,255 inputs and 20 contracts. Formatting checks 1,108 files and all 15 skills pass; Release analysis r926
qualifies (`D9919747…`), followed by full Release r927 (`2BEEE515…`, actual child 64296, exit 0,
03:56:08.984851–04:12:31.261660 UTC). Full Debug r928 qualifies (`87ADC6A8…`, actual child 31800,
exit 0, 04:12:58.389785–04:32:34.266591 UTC). Real ASan r929 qualifies (`3A058741…`, actual child
84528, exit 0, 04:33:14.865845–05:02:21.443913 UTC), including real detection and stress controls.
ARM64 r105 qualifies all three cross-builds (`6802766D…`, 15 PE/PDB pairs, five sanitizer controls).
Integration and this changed-source matrix are complete; overall F5/P1/G1 requirements remain open.
Native ARM64 runtime is unavailable. The exact source and contracts match every qualification bookend.

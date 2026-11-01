# Sizing reservation and Clone policy fixtures

These observations cover native x64 Debug correctness controls, with zero compiler warnings/errors and the
existing linked-image ISA controls. They establish no G1 timing or completed hosted native qualification.

## Sizing original charge

- Hosted source `554e8d5589dc27870970f59567302bd87939c896`, run
  [41334556660](https://github.com/RedSalamanders/RedPrism/actions/runs/41334556660), ARM64 job
  `124931621050`, failed the held-state predicate in panel → size → Undo mode 2. Its operation succeeded,
  final exact clipboard baseline and retirement checks held; individual held-state fields were not logged.
- The deterministic control admits a separate real 65,536-byte retained scheduler lease before the same
  composed sequence. The original worker publishes its actual ticket and `JobContext` reservation before
  the held message. Queue order, source, pixels, history and receipts are unchanged.
- Before correction, run `20261101T200351507Z-79048-6aa3f1fccf90494eaac48a31d25e3aeb` fails mode 2:
  reserved `397873997`, clipboard baseline `395830069`, declared and worker charge both `1978392`,
  original slot `0`, generation `420`, pending receipt and Levels history present, independent charge
  `65536`, held `0`. The aggregate difference is exactly sizing plus the independent lease.
  The test-only patch against source `29f876519aca7f72ad75dbaff388a78141ca2b37` has SHA-256
  `e04adfcfe9c74b15bd9ef33e9dbc3d5f3a752caeda4c02f61e4a4eb57255b4c9`.
- After correction, run `20261101T201203903Z-81148-b0bdc1a5582c48b786e8105216642b9d` passes all 22 modes.
  Mode 2 requires the exact original ticket and worker charge while permitting separate residency, including
  the known independent charge. Other modes retain their exact aggregate delta. The independent lease releases
  only after the original sequence settles; its real acknowledgement drains before every unchanged exact
  final baseline, clipboard, pixel/history, hidden-Present and off-owner retirement check.

## Clone input policy

- Source `29f876519aca7f72ad75dbaff388a78141ca2b37`, Full PrePush run
  `20261101T194231759Z-48084-94c0b17644e94fa3b7064fa18c50705a`, passed all 81 original Clone readback
  checks, including three rebinds, two recoveries and three preparations with one capture at the restart limit.
  Later, six of the 15 input-policy assertions failed, beginning with the prior host stroke's literal endpoint,
  pixel and history check. That attempt had no detailed endpoint/receipt diagnostics; its cause is unproven.
- The policy fixture now settles before replacement, verifies the actual new document/active target/mask state,
  and owns the canonical scoped synthetic keyboard. The original 15 literal checks are unchanged; three
  installation assertions bring the total to 18. Detailed actual-state diagnostics remain available.
- Runtime run `20261101T201203903Z-81148-b0bdc1a5582c48b786e8105216642b9d` passes all 18 checks and all
  81 original Clone readback checks. Its prior endpoint is exactly `(12,6)`, BGRA `(0,0,255,255)`, one
  Clone Stamp history entry, one capture, successful receipt, unselected mask and all three modifiers up.
  A subsequent passing fixture does not identify an unrecorded cause in the older attempt.

All failed build attempts, negative patches, raw logs and canonical run payloads remain separately preserved.
Persistent archives use `archive-qualification.ps1`; archive completion itself never establishes a test pass.

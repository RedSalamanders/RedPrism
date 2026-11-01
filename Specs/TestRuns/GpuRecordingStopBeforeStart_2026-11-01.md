# Recorder stop before worker entry

This record covers deterministic correctness and resource-bound observations on native x64 Debug.
It does not establish G1 latency, extended qualification, or a completed hosted native matrix.

- Base source: `554e8d5589dc27870970f59567302bd87939c896`; MSVC `14.52.37025`.
- Hosted trigger: [native run 41334556660](https://github.com/RedSalamanders/RedPrism/actions/runs/41334556660),
  x64 regression job `124931621009`. Clone restart-limit returned `0x80070057` after one deliberate
  device rebind, with one capture, zero recoveries and one preparation. Preserve this failed attempt.
- The controlled input stops an actual attached command source through `GpuCommandPump::DeviceChangedOwner`
  before resuming that same admitted recorder. No sleep or scheduling probability determines the ordering.
- Before fix: Composition run `20261101T192340542Z-105208-559c3ad1d1714debb524670346c9594b`
  failed three new policy/utility/display retryable-abort assertions. The uninitialized-input and no-upload/live-token
  controls passed. The exact test-only patch against the base has SHA-256
  `9a6af5ec62cd97a544a8e451a058a7c5a18c3915070f3b777844b27abd43939b`.
- After classification fix: Composition run `20261101T192739691Z-36176-9c017836e9104f0182da07a381fea6c9`
  passed. Policy, utility and display returned `0x80004004`; uninitialized policy and utility still returned
  `0x80070057`; recorded upload bytes were zero. All existing Composition controls also passed.
  Both builds used zero compiler warnings/errors and the existing linked-image ISA controls.
- The new case retains the original live cancellation token and 65,536-byte charge, permits independent
  compute progress, acknowledges zero GPU commands and returns the charge only after actual joined worker
  retirement. Existing original-source, foreign-device, cancellation and quarantine controls remain required.
- The readiness decision reads the same immutable owner binding, atomics and existing stop event. It adds
  no storage, event, wait, polling, recording resource or admission. Expected stops return directly without
  adding failure-logging work. The original Clone one-capture/two-restart/three-preparation limits and pixel/history
  expectations remain mandatory in the full Runtime suite; this control does not replace them.

The original failed log, test-only patch, corrected build attempt and positive run payloads remain in the unique
local qualification evidence. Persistent preservation uses `archive-qualification.ps1`; archive completion alone
never establishes a test pass. Subsequent complete-source validation and hosted results have their own receipts.

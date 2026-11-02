# Ctrl-pixel fixture owner readiness

Hosted source `e650dc5018234943f1d117b1b617658c1569e9fd`, native qualification
[41362032887](https://github.com/RedSalamanders/RedPrism/actions/runs/41362032887), ARM64 job `125014360229`,
passed the held private-final state, then failed
`final-slot-close.actual-suspension-retires-source-before-original-final-capture`. The attempt did not log
the individual retirement/published-graph operands or its last wake state. Its exact historical cause remains
unproven. The x64 failure in that same run was the separately corrected loaded-selection capacity fixture.

Local source `0016e730535c065e331f90f61d51e2d952496c4b` passed full x64 Debug PrePush in canonical run
`20261102T004058685Z-83568-ec0dfef1a608462292e097850d88c4be`: all seven native suites, tooling and format.
Its loaded-selection controls and complete original refusal suite passed. All 473 original Ctrl-pixel facts
also passed.

A first controlled notification experiment added an owned hidden-window notification and diagnostics after
direct controller suspension, leaving the existing hidden-display override enabled. Canonical Runtime run
`20261102T005903387Z-68708-d0bec014a6324174957cd9b579e3788c` passed all original assertions, including all
six slot cases. That experiment did not reproduce the ARM64 failure. Preserve it as a passing control, not
as a failing baseline. Its exact source patch against `0016e730` has SHA-256
`58abe576c7cb49f1ce8a5eb934472a322518d378ab9d6082e1106cdd6a8832b8`.

The fixture's predicate wait omitted the canonical `HasUiWork()` check before waiting on an auto-reset wake.
A separate control posts three real owner actions into an otherwise settled, document-free Application,
consumes the single wake signal, and exercises that same unchanged predicate wait. Canonical x64 Debug
Runtime run `20261102T062508991Z-103976-a5e682bcf76449e9a1f074c82b1fdde5` fails exactly the new progress
assertion and exits 5: all actions admitted, initial wait `WAIT_OBJECT_0`, second wait `WAIT_TIMEOUT`, two
actions observed, ready work still present, and predicate progress false. The third callback is drained before
its borrowed counter dies. The summary records 276 checks and one failure. The exact control patch against
`0016e730` has SHA-256 `243440b15e09c270da1af068bf53b0239d775e4e35d429f5d44ab2fbccaabbd6`.

The correction retains the original deadline and adds the same ready-work check as `SettleJobs`. It also
disables the existing hidden-display override throughout each slot-suspension case, exercises repeated actual
window-handler notifications, and restores the override through that handler for resume. All 473 predecessor
assertions remain; three queued-action facts and six notification facts bring the required total to 482.
Production scheduling, visibility, memory admission, GPU retirement and feature activation are unchanged.
Subsequent exact-source local and hosted qualification has its own independent receipts. This controlled
wait-loop failure does not retroactively identify the unrecorded ARM64 wake/graph state.

# Hue caller qualification — 2026-10-16

## Status

P1 remains WIP. The qualified native source is `67a77dd`; its completed x64 and ARM64 matrix
is unchanged. The private Hue candidate is not adopted. R4 private correctness and independent archive review pass after the
retained R3 failure below. The redundant older-source Wand build is stopped before compilation; every Wand
predicate remains required in the stronger combined current-source native matrix.

## Retained R2 compile refusal

DocumentTests fails with C4189/C2220 for an unused `historyCount` fixture local. Main verifies the
existing `HueStateSnapshot` / `SameHuePanelState` already compares the history count. R3 removes only
that redundant local; all 55 predicates and warnings-as-errors remain. No R2 runtime is claimed.
The complete 1,889-file archive completion is
`95B03EBE38BA6FFDE77CBC1C610263B4455D93BEB30752FABA5EC38F179A265B`.

## Actual R3 runtime refusal

All three private Release builds, their ISA scans and actual DocumentTests/HostTests return zero.
The Application process (PID 12384), `--self-test --warp`, runs from
2026-10-16 10:18:00.969260 to 10:18:34.114797 UTC and returns **5** at the unchanged assertion:

`hue-host.target.literal-blue-range-physical-saturation-15-hue-minus10-no-release-sample`

Stdout SHA-256: `C54EB72EB2843C8C488A93007CD8DB813A6DBB28E5715E97220CC91A4BE7C4BA`.
Stderr SHA-256: `43237CCA6925D8DD2296969B1972072F488D86FFE8A4B947154BCBD30B9CC35B`.
The Application build reports MSB8028 during DxUi GetProjectInfoForReference because the private command
inherits its IntDir. No DxUi compilation or pin change occurs; frozen dependency bookends pass.
This is not a zero-warning build claim or native qualification.

Main independently rehashes every one of the 2,443 archived files in
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-HueReadbackHost-R3-private-failed-20261016`.
Manifest SHA-256: `73ADFAD8DED12E6BC0ED1CD85433589D4818B6539BE5B763295A17B37D24EB95`.
Completion SHA-256: `F3FE301372975223CB2AED2DA767D196C41A2A5E8CC0450FA01D74AC72807458`.
Raw outputs, source, dependencies, objects, executables and PDBs remain retained.

## R4 source repair at preparation

`FinishHostContinuation` previously rearmed commands, canvas lifetime and acquired lasso events.
It omitted acquired Hue events. The readback completion requests the continuation, but that invocation
can finish the active request without scheduling replay of its pending Hue prefix. R4 includes
acquired Hue with pending events or the accepted-prefix clock closure in the existing rearm condition.
It creates no new UI source, worker, ticket, polling wake or reservation. All original test predicates,
numeric expectations and source rosters remain unchanged. Formatting and source-only roster controls pass.
Actual R4 compilation/runtime, independent reader and archive review were pending at preparation and are
recorded below. Source inspection alone qualifies neither the repair nor ordinary activation, device faults,
G1 or P1 Done.

## Completed R4 private correctness checkpoint

The only C++ change from failed R3 is the `FinishHostContinuation` acquired-Hue wake condition.
All three builds, three ISA scans and actual DocumentTests/HostTests/Application processes exit zero.
The Application PID 25584 runs from 2026-10-16 10:54:20.509983 to 10:55:42.265343 UTC and passes the original
failed predicate and every later Hue case. Its stdout SHA-256 is
`C87E402CCBC8342A7F617D4C0CF25AF2698627ADE9646D27FCA830B4CBFA8923`.
The strict independent reader retains all 55 scalar facts, 70 Host Hue checks plus summary,26 original Hue
caller facts and 2 admission facts, and all 138 stages/7,793 stdout/7,613 stderr predecessor assertions.
This private base predates the separately qualified complete-startup 139th stage; it does not replace the
current native qualification.

Source/dependency/product bookends, all three actual ISA reports and executable/PDB GUID/age pairs pass.
The private MSB8028 GetProjectInfoForReference warning remains explicitly scoped above. Main rehashes every
file in the complete 2,451-file archive:
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-HueReadbackHost-R4-private-passed-20261016`.
Manifest SHA-256: `A28545309C12EB630A0B1D0BD387F87D0D9882FB7318A5667BBDF4E4948FD1BE`.
Completion SHA-256: `DBB9D7EAEEB4A4954600E27446FC18FD6D4D179B98AB961A4DD8F69D314DD28A`.
No source/test predicate is weakened. Native combined adoption, the full latest-source matrix, ordinary
activation, complete admission/lifetime/device qualification, G1 and P1 Done remain open.

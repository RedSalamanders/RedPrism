# Utility-owner idle and memory-notification fixture

Status: complete same-source local matrix qualified; publication pending. P1 remains WIP.

## Checklist

- [x] Preserve the complete failed canonical ASan attempt and its original assertion.
- [x] Identify independent memory-notification readiness in the scheduler's idle contract.
- [x] Preserve all original routing assertions; isolate their memory input and add three state controls.
- [x] Qualify the original complete Composition roster plus exactly three new facts in real ASan.
- [x] Complete x64 Debug/Release/real ASan, all three ARM64 cross-builds and canonical Release analysis.
- [ ] Commit and push this qualified step; verify the destination SHA.

## Complete integrated qualification

The [ordinary-Hue integration matrix](P1OrdinaryHueStartup_2026-10-20.md#complete-same-source-local-qualification)
records all seven actual-zero profiles, source identity and independent archive/profile/runtime checks.
Each x64 profile preserves8,425 Composition predecessor facts and passes exactly three additional explicit
notification/drain assertions. The original empty-idle predicate and all41 cancellation predicates pass.
The production scheduler and real memory-notification behavior are unchanged. Complete native ARM64
runtime, performance and the parent P1 delivery gates remain open.

## Preserved failure

Canonical `oh3a1`, actual parent1716, exits1 at2026-10-20T20:15:20.964841Z.
The sole reported assertion is `utility-owner.empty-scheduler-is-idle`; all41 recorded-parent
cancellation controls pass. The attempt stops in CompositionTests, before I/O and Application runtime.
The 3,353-payload archive and source bookends are independently verified; manifest SHA:
`509243D3A3782429D3D01FD19C38327960CB12375F8EDB4DF19495C39EA0D408`.
Source freeze:`973D008F1AE4CF84EFA848419CC09579191D60AACCF44D57D83ECB37D537A866`.

## Mechanism and correction

The router fixture used the live default memory source. Scheduler initialization registers system
low/high-memory event waits. Their callback marks memoryChanged and wakes the owner. IsIdle correctly
includes that ready UI work even with zero job records; the notification requires a drain.
A direct snapshot of an otherwise empty router therefore cannot assume no unrelated memory event.
The failed run did not capture which notification fired, so the record does not identify an OS-pressure
transition, external allocation or other historical environmental cause.

This routing fixture now uses the same fixed memory-source pattern as the GPU-work fixtures. Its original
empty-scheduler assertion remains unchanged. Three new independent state expectations explicitly notify
the scheduler, require non-idle/ready UI work while the utility service remains empty, then drain and
require idle again. The original no-receiver-visits check also remains. No sleep, polling, timeout increase,
production scheduler change or golden change is introduced.

Real system memory notifications remain active in ordinary application startup. This fixture correction
does not qualify performance, complete ordinary activation or P1 Done.

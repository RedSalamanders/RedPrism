# P1 worker body observation and cancellation measurements

## Delivery checklist

Status: **preparation only**. The recovery source is frozen for qualification. The candidate described here has
not been integrated, compiled or executed. P1 remains WIP.

- [x] Independently review the optional worker observation boundary, callback lifetime, concurrent lanes,
  resumed tickets, cancellation normalization and shutdown ownership.
- [x] Prepare seven native candidates and four owning-contract updates; preserve existing goldens and the
  historical measurement values. Two new fixture files add 36 independent event-driven controls.
- [x] Review the report parsers against independent fixed models. Synthetic acceptance/refusal controls are
  parser proof only; the original rejected ordinary HostTests namespace is retained with its corrected checker.
- [ ] Commit and push the independently qualified recovery checkpoint before integrating this slice.
- [ ] Verify every candidate/base hash, integrate, compile and execute the original HostTests on the new source.
- [ ] Run the actual Release 45 MP cancellation diagnostic; authenticate its executable/source/machine,
  twenty operation records, race classifications, exact sizing outcomes and unchanged source.
- [ ] Complete current-source Debug, Release, real ASan, analysis and ARM64 cross-build qualification.
- [ ] Update this checklist, owning contracts and the P1 plan; commit and push the qualified step.
- [ ] Measure actual Escape acceptance, restored-source presentation, body exit and external acknowledgement
  separately in the application loop. This headless diagnostic cannot close measured G1 or P1.

## Boundary and ownership

The proposed scheduler observer is immutable from initialization through joined `Close`. Its borrowed context
survives that join. Compute and recording callbacks may overlap; callbacks use bounded thread-safe storage and
cannot wait, call the scheduler, access UI/document state or authorize result application. The default observer
captures no clock, creates no event and allocates no diagnostic storage.

`BodyEntered` precedes `Run`; `BodyExited` follows its return and local destruction, or the runner's translated
`std::bad_alloc` unwind. Exit reports the raw result before cancellation normalization, worker retirement and
owner apply/drop. A dispatched cancelled stage that never calls `Run` reports `BodySkipped`. Descriptors dropped
without dispatch report no body notice. A resumed original ticket can have several body pairs. Body exit does
not acknowledge pending native I/O or GPU work.

The fixed fixture covers normal order/identity/local destruction; concurrent compute and recording lanes;
raw failure and allocation unwind; queued and running cancellation; three resumed stages; exit before held
retirement; callback-free default initialization; and owner-thread shutdown joining the borrowed observer.
Its 36 literal expectations are independent of the implementation's returned values.

## Measurement method

The proposed `HostTests.exe --bench-filter-cancel` diagnostic selects the scalar reference, uses an
8192 × 5464 fixture, and records one excluded warmup plus ten trials for each of twenty operations. After actual
body entry it waits up to 20 ms on the returned-body event. On timeout, it captures QPC immediately before the
original ticket's cancellation request. The worker observer captures the matching body's exit QPC.

Reported request-to-return time includes `Run` local cleanup and excludes subsequent worker retirement and
owner application. Already-finished work and valid-output/stop races are counted separately. Failed clocks,
failed admission, worker errors and invalid sizing results fail the diagnostic. Sizing cancellation must return
an empty detached result and preserve the source; the admitted reservation must not exceed 2 GiB. Machine and
actual worker-count collection occur outside measured intervals.

The earlier [cancellation report](FilterCancellation_2026-09-28.md) captured its clock inside `Run`. Its numbers
remain historical measurements of that earlier boundary; they cannot prove completion of final local cleanup.
The candidate owning-contract changes make this distinction explicit without changing the recorded numbers.

## Preparation provenance

Main's candidate/base hashes and fixed 36-record model are in
`.build/P1Drafts/JobExecutionObservation-r3/Main-final-preparation.json`. The corrected namespace checker is
under that directory's `observation-checker`; the twenty-operation report parser is under
`.build/P1Drafts/JobExecutionObservation-r2/report-parser`. Main also reviews the cheaper full-profile checker in `JobExecutionObservationQualification-r2`: it adapts the actual `ArchivedPath` schema, preserves original producer identities and all unaffected source bytes, and compares complete Application records and ordered smoke operation names while retaining each original timing log hash. All 18 controls pass, including actual qualified-baseline metadata. Its first incorrectly selected focused-archive product log is preserved with its corrected receipt-bound stdout selection. These ignored files contain preparation and parser evidence, not native execution of the proposed observer. The current recovery qualification is recorded in
[the durability ledger](P1ProjectDurabilityFaults_2026-10-09.md).

Actual source, executable, runtime receipts, complete configuration results and measurement values will be
recorded here after integration. Native ARM64 runtime, RAW/Photoshop comparison and ordinary application
activation are outside this bounded diagnostic's proof.

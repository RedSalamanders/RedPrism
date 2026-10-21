# P1 isolated lasso point arena and ticket — 2026-10-12

Status: **private x64 Release DocumentTests build, ISA scan, and actual test run PASS for isolated CPU storage only.** This does not qualify host/display/finish integration, native configurations, ordinary activation, or G1.

## Qualified evidence

The exact report is `.build/P1Drafts/LassoPointArena-r1-Main/actual-release-r3/qualified.json`, SHA-256 `C243B39B962886737AE7426CC62450DD5DF135EFF923C0C49982AED278C2EC92`. It records source count 1,301; 575 new assertions plus two summaries; all 20,782 qualified predecessor records; and 21,359 total records. Protocol SHA-256 `65428CBA06CFB3AE498BF384AF129929AD0994916860A70332AA0BF1C949F5D2`; before-book SHA-256 `522779AA84DAA46E32E975B9F1A653AB720668802AB8D9DDBE0048B8414C4389`; products-book SHA-256 `D76F314CB71F983017C45B1C545168631F671A488BFABA44DB7B0A5227621850`; reader SHA-256 `283519B994FDF6F1489140C2B427F76E24478F48E89C31B0DFA558F27677B408`. Source/dependency/product bookends pass.

Build PID 39308 exited 0 (21:23:34.105113–21:23:38.061041 UTC), ISA PID 63752 exited 0 (21:23:38.383170–21:23:46.372537 UTC), and actual DocumentTests PID 98784 exited 0 (21:23:46.488641–21:23:55.092074 UTC). The tested private header `Source/Common/BoundedLassoPoints.h` SHA-256 is `B4999A5CFA4B72C096B67C4A2CCEC8FE3AFB9F4523C0F3BD060A38B6A1919E8B`.

The 536 independent point-model checks cover block boundaries, snapshot behavior, refusal, cancellation and retirement. The 39 ticket checks include the literal 4 MiB original admission before arena/output creation, retained snapshot ownership, deferred arena destruction and refund only after original-owner acknowledgment. Together these are 575 new assertions; the summaries account for the two additional records.

## Preserved earlier failures and correction scope

The r1 Release compile failed (PID 81340, exit 1); its diagnostics include invalid `std::array` deduction and related test-fixture errors. The r2 Release build passed, but actual DocumentTests PID 71176 exited 1 with two new reference assertion failures (one point-local lifetime and one cancellation-error assertion). In the private test candidate, Main corrected the local counter and cancellation-error references against the independent ownership and scheduler contracts. The r3 receipt is the qualified result; no product integration is claimed.

## Limits

The first Lasso storage checklist item is complete only for isolated arena/ticket qualification. Worker-owned gesture acquisition, event buffering, display preparation, finish/history publication, all host readers and lifecycle paths, native configurations, ordinary activation and G1 remain open. This receipt does not relax the proposed point/slot ceilings into current product behavior.

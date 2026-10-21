# P1 selection-outline owner capacity — 2026-10-12

Status: **private isolated mock proof PASS; actual simultaneous application owners, native integration and ordinary activation remain due.**

Main rejected the delegated draft because it used the unrelated Z checkout with an eight-slot baseline. Its generated artifacts and refusal are retained. The replacement copies the authorized C checkout's current ten-slot baseline. It changes only the bounded array capacity to eleven and adds explicit admissions and idle observations; the ten existing attach observations and original round-robin, pending/ready, detach/reuse and reentrancy checks remain. The literal capacity observation intentionally becomes eleven.

## Actual checks

The x64 Release isolated test emits exactly 47 expected PASS records. A separate compiled ten-slot control retains the runtime checks while omitting the compile-time capacity assertion. It exits 1 and fails exactly the literal-eleven capacity and actual eleventh-leaf admission observations. The positive executable passes ISA with zero VEX/EVEX, undecoded prefixes or verified table data. No application, window, clipboard, GPU or DxUi code is compiled by this proof.

Qualified report: `.build/P1Drafts/SelectionOutlineCapacity11-r2-Main/actual-release-r1/qualified.json`, SHA-256 `95955B0E481BBF20AB51A781805565313AB75C3D22955D198D0A33BC1D4F10AB`. Main independently rechecked source/dependency/product bookends and actual stdout/stderr hashes.

| Actual child | PID / exit | UTC interval | Stdout SHA-256 |
| --- | --- | --- | --- |
| positive-build | 85636 / 0 | 2026-10-12T18:44:53.299925+00:00–2026-10-12T18:45:00.723536+00:00 | `41E2311BC10498347159F50D92B6889A2930D352CBC32F6BA93075C01062A999` |
| negative-build | 32900 / 0 | 2026-10-12T18:45:00.756314+00:00–2026-10-12T18:45:03.680234+00:00 | `242BDACD01F3D0DA95AC86FA53F9CCB77CBCB6D8123AC940C8C5487CDBEB3403` |
| positive-isa | 105236 / 0 | 2026-10-12T18:45:04.002671+00:00–2026-10-12T18:45:08.906359+00:00 | `2809637C4596B606CCFEDAC5230F796D20DBE35FF1A6B3B0F16EF4E799FE2D38` |
| positive-runtime | 76492 / 0 | 2026-10-12T18:45:08.940181+00:00–2026-10-12T18:45:09.010642+00:00 | `4F8C7388932616CFD46CEE16F27FCDFF0EDC4DEF1D033CC17327CE911CCF3CA1` |
| negative-runtime | 94160 / 1 | 2026-10-12T18:45:09.041904+00:00–2026-10-12T18:45:09.126214+00:00 | `21B09F0F1A5E4A3FC458271C4CA6628DC466D0D2C507E4C08056269C5FF90A31` |

All five stderr streams are empty.

## Remaining application acceptance

The mock result proves the array semantics; it does not prove a startup owner count. Enumerate the actual initialized owners, exercise complete responsive startup plus live stroke and selection, refuse a twelfth concurrent owner, and verify failure unwind and joined shutdown before integrating the capacity change. No P1 delivery checkbox is closed by this receipt.

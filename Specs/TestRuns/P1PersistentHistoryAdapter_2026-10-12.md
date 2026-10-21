# P1 persistent history adapter — private Release receipt — 2026-10-12

**Result:** PASS for the bounded private actual `DocumentHistory::StorageIndex` adapter controls and the full
original `DocumentTests` assertion stream. The evidence covers O(1) root copies, incremental unique self-storage
bounds, and skipping charge sampling when the residency epoch is unchanged. It does not qualify changed-epoch
residency updates, complete topology preparation, operation admission or retirement cost, native integration,
ordinary activation, H3, G1, or P1.

## Actual run

The isolated packet is `.build/P1Drafts/PersistentHistoryAdapter-r2-Main/`. The independent reader is
`.build/P1Drafts/qualify-persistent-history-adapter-r2.py` (SHA-256
`E45E7DF3D2B8D54D663FF28FC28FF9C1B2D26F0F42BC04C02F5E8CD89069514D`). Its report is `qualified.json`, SHA-256
`602E83FC0615C215895D710CA389AB56E387A93B2E3D7E7BB77654B6A96C26CB`.

| Process | PID | UTC interval | Exit | Captured stdout SHA-256 |
| --- | ---: | --- | ---: | --- |
| x64 Release `DocumentTests.vcxproj` build | 25316 | 2026-10-12 04:43:22.606243–04:47:25.858144 | 0 | `979A5DBC2B43E1FE3C7A20964CC432785940F36F79A51A618115CA3FF2FD7327` |
| x64 Release `DocumentTests.exe` | 15084 | 2026-10-12 04:48:14.218520–04:48:25.529591 | 0 | `86E6939C7521FEB6C1571E5E13DF1ABE6F8B58BECDA61F37684CDFB13CFB53AB` |

Both captured stderr logs are empty, SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.
The build log reports zero warnings and zero errors. The executable was built at
`.build/P1Drafts/PersistentHistoryAdapter-r2-Main/Products/x64/Release/DocumentTests.exe`; its pre-run SHA-256 is
`763F7B66C97A43D77606EB3D3E15D3090E8158B6D9F577D797C5AA81A1A2F2A6`.

## Assertion stream and binding

The independent reader verifies the r2 input roster, bound native sources and contracts, pre-runtime products,
process IDs/times/exit codes, and captured output hashes. It confirms that all 18,297 predecessor assertion records
remain in order and with their original multiplicities, then counts 435 passing adapter controls plus the passing
summary record: 436 added records and 18,733 total records. The predecessor count comprises 9,204 original native
Document records, 37 raster-core records, and 9,056 persistent-index foundation records. No failed assertion
records were observed.

Key identities checked from the retained artifacts:

- `before.json`: SHA-256 `EB31D7BE80F423A1F3846BD217B88C7F481D54C2CC2430C516B83A34BA8D23EA`.
- `products-before-runtime.json`: SHA-256 `3BB2A4926A5E440F104DE17FF88A1DF1C8F2410722EDFB80505242F3457A613E`.
- `build-result.json`: SHA-256 `3FBFF63719D921FC40AD4078BFB70ADDC1FE2533DEF75FA99804986FDB55F086`.
- `document-result.json`: SHA-256 `DAC0D00C376D6C7A9CE82619A79FAABBAF3CF7945D86AD57C30CCBC10F6A308B`.
- Private `Source/Common/DocumentHistory.cpp`: SHA-256 `63E737206DF1014CF651F737C41A926BC21F1E3AB66243D636CD2CC5D6A0BF26`.
- Private `Source/tests/documenttests/Document.Tests.PersistentHistory.cpp`: SHA-256 `E4DC22DD3CF498B27781CEAC6B7E357BCE957902147F733C3696A091FD115C59`.
- Adapter draft `.build/P1Drafts/history-storage-persistent-adapter-r1.inc`: SHA-256
  `94E3B4D54E1A48456339E7183CFC3AFC90BAA4C87C6D3C6DBD2F9F0FFB7A6045`.
- Corrected fixture `.build/P1Drafts/PersistentHistoryFixtures-r2-Main/Document.Tests.PersistentHistory.cpp`:
  SHA-256 `03BDE001EEA320BE1698AC62C9C6D3A9D7E60E4FF22E02888325661077158B15`.

The exact scope is the reader's recorded scope: “Private actual history adapter: O(1) root copies, incremental
unique self-storage bounds, unchanged-epoch sampling skip. Current topology traversal, journal deltas,
admission/retirement cost, ordinary activation, native gates and H3/G1/P1 remain due.”

## Preserved failed attempt and fixture correction

The earlier r1 attempt remains preserved under `.build/P1Drafts/PersistentHistoryAdapter-r1-Main/`. Its build failed
on eight fixture-only API errors: calls to nonexistent `RedoCount` and a sparse-raster factory call missing its
required fill argument. No r1 runtime started. In r2, the fixture reads redo count through
`CaptureState().FutureCount()` and supplies literal zero for sparse fill. The assertions and expected values were
preserved. The independent reader then qualified the fresh r2 Release build and runtime above.

## Remaining work

- [ ] Integrate and qualify the adapter in native sources; native sources and normative contracts remain frozen
  for the current ARM builds.
- [ ] Prove admission and retirement bounds, including original-ticket ownership, for production callers.
- [ ] Replace changed-epoch global resampling with bounded residency deltas and an explicit overflow fallback.
- [ ] Avoid full topology walks for scalar edits and qualify changed-owner topology preparation.
- [ ] Qualify host activation and all required native Debug, Release, real ASan, analysis, and ARM64 gates.
- [ ] Close H3, G1, or P1 only after their independent remaining requirements pass.

This is private, bounded CPU evidence only. It records no timing claim and does not change a normative contract.

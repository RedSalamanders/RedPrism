# P1 persistent history foundation — qualification receipt

**Result:** PASS for the private CPU immutable AVL owner-index foundation and its independent reference controls. This is a bounded documentation sidecar for Main’s ongoing real history adapter work; it does not close P1.

## Evidence identity and execution

Evidence is from `.build/P1Drafts/HistoryPersistentIndexCore-r2-Main/` in this worktree. `qualified.json` SHA-256: `8AC833694E629A8DD6122F78A76BB1315CBA883D5B4AE71EFA6FFD1C4120905D`. Its roster input `before.json` SHA-256: `98ED73C90032BE8C15482A114890EFC66A6387F830A19AAFCF241EA50D3A69B3`; the independent reader `.build/P1Drafts/qualify-history-persistent-index-core-r2.py` SHA-256: `F93C3396B95A1BD2658BFCE5EE61EB488CB8CFE2E538F5FAAA8BC6745D35669C`.

| Run | Actual process | UTC interval | Exit | Captured stdout SHA-256 |
| --- | --- | --- | --- | --- |
| x64 Release `DocumentTests.vcxproj` build | MSBuild PID `33080` | `2026-10-12 04:22:03.249667`–`04:25:16.494275` | 0 | `FFEE4B2C0AA0FE135EAD38DCEF14618CDAD1E9667EFF650164BC4A511B6AAAA7` |
| x64 Release `DocumentTests.exe` | PID `76732` | `2026-10-12 04:25:18.082042`–`04:25:28.521614` | 0 | `E39E2B7EAD6C35CEE2F39418F1A875C9476D137F374B7B6EF7A842ECDC619D83` |

Both captured stderr streams are empty (SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`). `document-result.json` SHA-256: `C2122122F5B521E6F09D42FE36C0C835B1F15A423C8EAEA6B0FD6B4A15D4A4B4`. The build log records zero warnings and zero errors. These are the actual recorded process IDs, times, and captured-stream hashes; this receipt did not launch either process.

The source/contract binding is SHA-256 `5D84694D230E9C0AC94188224F890D7DD95D093299C63CCCFFD34F6B2A8416B8`. **This is a SHA-256 source/contract binding, not a Git commit.** The independent reader rechecks bound source and contract files, products captured before runtime, result PIDs/times/exit codes, and captured output hashes. The tested implementation header `Source/common/PersistentOwnerIndex.h` is SHA-256 `6A5E31B24D76B91D9FBC6B89F893545AB0E060238BD4640F0691164E727754DE`; test source `Source/tests/documenttests/Document.Tests.PersistentOwnerIndex.cpp` is SHA-256 `B0CCFC25D97665B1C195827FF9E2833AF1399F3E251E1B1755712E504F93DFDF`.

## Reference map and record preservation

The independent reader pins the predecessor qualification at `.build/P1Drafts/SelectionOutlineRasterCore-r3-Main/qualified.json`, SHA-256 `24BAE8544C9E83F91D9F4DC608426E38FC20A36739968CE178BC42357310BB04`. From its qualified predecessor stream it accounts for **9,204 original native Document records + 37 raster-core records = 9,241 predecessor records**. Main readback confirms every predecessor record remains in order. The reader compares the predecessor record stream and preserves its pre-runtime multiplicities; only bounded private-wand scratch/allocation/work statistics are normalized for comparison.

Against that fixed predecessor, the reader checks the pre-runtime expected-label roster from `before.json` by exact occurrence counts: **9,055 independent history-control assertion occurrences**, including deliberate repetitions in the 1,024-key corpus, plus the `persistent-owner-index.summary passed=9055 failed=0 controls=9055` output record. Thus the receipt reports 9,055 controls and one summary record (9,056 added output records total), not 9,055 unique labels. The qualified readback reports zero failed records and `all_predecessor_records_preserved_in_order: true`.

Main’s additional source controls exercise partial allocation faults at each of five allocation positions and assert propagation while preserving source and output roots and withholding publication of the new owner. The old-owner controls path-copy a replacement under a second publication owner, verify old/new payload independence and retention of both original owners through their respective roots/shared leaf, then verify each owner is released when its last retaining root is dropped. These supplement the broad `std::map` reference checks, ordered visits, AVL height/allocation bounds, cancellation, and transactional refusal controls.

## Initial preparation refusal

The preserved `.build/P1Drafts/HistoryPersistentIndexCore-r1-Main/preparation-refused.json` (SHA-256 `B2D84923D948F6502839ECC1B4C4449A303AF7DD42DBA4A538C89CFA7935CAF8`) records `PREPARATION_REFUSED`: expected unique labels `98`, statically observed `96`, and `build_or_runtime_started: false`. This was a metadata-only preparation refusal; no compiler or runtime was launched for it. Main corrected the pre-runtime metadata count from 98 to 96 without changing the product or test predicate, then captured the successful qualification above.

## Explicit scope boundary

This evidence qualifies only the private CPU persistent index foundation. It does **not** qualify or implement the actual `StorageIndex` history adapter, topology deltas, sparse-residency journal, operation admission, original-ticket binding, host activation, H3, G1, or P1 closure. The child plan remains WIP and ordinary activation remains disabled. No native source was edited for this sidecar.

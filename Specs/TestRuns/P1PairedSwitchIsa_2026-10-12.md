# Paired R9/R15 switch-data ISA qualification — 2026-10-12

Status: **private scanner/data proof PASS; integrated native scanner adoption awaits fresh r2 configuration qualification.** This evidence does not change the DxUi source, library, pin, or runtime allowlist, and does not claim P1 acceptance or activation.

## Exact paired proof

The private paired qualification reports PASS for 113 cases: six accepted and 107 rejected. Candidate scanner SHA-256: `513445115E1C362D3BAF7A3A495A8060E37AEC2F4E48FC257FE2AC52276150F6`; native fixture SHA-256: `60527D39F851E61D0A5DBB139C39AE32C1B7EEA4080EC318B07D65BEB7F9692E`. Receipt: `.build/P1Drafts/IsaR9R15Pair-r1-Main/synthetic-qualification-r1.json`.

The exact old r22 image scan remains the runtime evidence: two EVEX-looking rows are adjacent relative-DWORD/byte switch data, established with its PE, anonymous PDB symbols and guarded dispatch. The executable vector-instruction count remains 488 with zero violations. The synthetic qualification says no synthetic image was executed; it tests paired parser inputs and preserves the original output. The original scanner refusal is retained in the parent evidence history.

The actual adapter child 69744 exits 0 at 15:09:14.641740–15:09:24.704348 UTC. Its stdout SHA-256 is `54E3CF303318FF3A3D7FAF77F4E3B295243DB89F56A7A21D82704844D4B7D99C`; stderr is empty. Aggregate qualification SHA-256 is `581784E408E7CF509BADEDCA3AC718652F4635C3F394E7A1781DB9BF2CA84C94`. The unchanged original suites preserve all 2,470 ordered lines; the sole projection normalizes three whole announced fixture-folder GUID lines after validating their exact paths, existence and unique per-run IDs. Missing, duplicate, changed and reordered records refuse. The first raw-byte reader refusal is preserved.

## Remaining work

The exact recognizer and 113-control fixture are integrated in the current native source set. Accounting integration binding is `83AEC5FF3CF381779D2F3BAC5F49EDD5D7531045A1B53C925E62733F38AF30A3` (1,289 inputs / 20 contracts); r962 analysis, r963 Release, r964 Debug and r965 ASan qualification is active. The standard PS5.1/PS7 bridge passes 18 controls plus summary. Preserve r958 analysis PASS, r959 full-native failure, and the historical Accounting-r1 partial-link ISA attribution. The new protocol corrects only its current-source hash assertion; historical checks and scanner predicates remain. See [layer-rows](P1LayerRowsRetirement_2026-10-12.md) and [host](P1SelectionOutlineHost_2026-10-12.md). Full profiles, ordinary activation and delivery acceptance remain due.

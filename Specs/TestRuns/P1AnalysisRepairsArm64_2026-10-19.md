# P1 analysis repairs: ARM64 archive evidence (2026-10-19)

Status: successful ARM64 cross-build evidence for the frozen candidate. P1 remains WIP.

## Source identity and scope

The two successful build attempts below use source-freeze SHA-256 `0AB879EF80A0B057E8269317CFFB434FB51271B19AD5C8F2E03A4085930FC6D8` over 1,825 frozen inputs. Their recorded Git HEAD is `8c3bd38c39120d7beb64a1f58b5d91ebca2f8b3d`; their working-delta SHA-256 is `196D950118C1052CA16F5AEA6DB81C96989D0F7777A2BE1352E4EE510BBA2A29`.

The source identity is **HEAD plus the recorded uncommitted delta**, which contains six test translation-unit repairs. It is not a clean-commit qualification of `8c3bd38` alone. The matching source-freeze hash and source bookends establish the archived inputs recorded for these attempts; they do not make the uncommitted repairs part of the commit.

## Attempt receipts

| Attempt | Profile | PID | Start → end (UTC) | Elapsed | Exit |
| --- | --- | ---: | --- | ---: | ---: |
| r1150 | ARM64 Release | 36456 | 2026-10-19 02:39:32.729140 → 02:40:54.974930 UTC | 82.246 s | 0 |
| r1151 | ARM64 ASan Debug | 89620 | 2026-10-19 02:42:24.683745 → 02:43:12.465550 UTC | 47.782 s | 0 |

Each result records `frozen_inputs: 1825`, `source_bookends: true`, `ordinary_activation: false`, and `P1_done: false`. Archive qualification records same-attempt archive-byte verification and retains profile/runtime qualification as a separate scope.

| Attempt | Archive files | Archive manifest SHA-256 | Final object-manifest SHA-256 |
| --- | ---: | --- | --- |
| r1150 | 2,907 | `26470735B16692F99F8743447F3405BCB6F7E9CFB378599412B850AB033B6832` | `2AB2F43AF90C778CB417783C8F81F13971A897317CB01624FA42FA88BFA3522B` |
| r1151 | 2,911 | `A90E2621C5BEF7BD04A4809D433B651935E6A882EB7BFB4EE74B00F3CC08F73D` | `389A68981E79BFF4DA77E90AE5769B4AB1BADCE9993B089A77301E566B76F552` |

Both profile receipts record 994 compile records, distributed identically: RedPrism 351, HostTests 136, DocumentTests 285, CompositionTests 138, and IoTests 84. Each records five PE/full-PDB pairs passing its profile reader. The executable/PDB GUID and age pairs are in the corresponding profile-qualified receipt; these receipts bind them to that attempt's archive manifest.

## Object-manifest comparison

Comparing each final `*-objects.json` against that attempt's `pre-objects.json` by object path and SHA-256 gives:

| Attempt | Pre objects | Final objects | Hash changed | Added | Removed |
| --- | ---: | ---: | ---: | ---: | ---: |
| r1150 | 994 | 994 | 8 | 0 | 0 |
| r1151 | 994 | 994 | 8 | 0 | 0 |

These are changed-object counts from the two manifests. They do **not** mean only eight objects were freshly compiled: byte/hash differences alone cannot establish compiler freshness. The profile receipt separately records 994 compile records; a claim about incremental freshness must be tied to the corresponding compiler tracking-log evidence.

## Reader evidence and limits

- The r1150 profile-reader r1 attempt is explicitly retained as `RETAINED_READER_REFUSAL_NOT_NATIVE_FAILURE`. Its stated refusal asserted `/DEBUG:FULL`, while the canonical `Directory.Build.props` emits `/DEBUG` without a suffix. The refusal receipt cites the same archived link tracking log showing `/DEBUG`, and the actual PDB properties `HasDebugInfo`, `HasTypes`, `HasIDs`, `HasGlobals`, and `HasPublics` as true, with `IsStripped` false. It was not natively replayed. Preserve the refusal and its reason; do not recast it as a native build failure.
- Main's r2 reader evidence is same-archive evidence: matching PE/PDB pairs, debug/type/ID/global/public records, and not-stripped state are proven for the five images; it excludes FASTLINK/NONE linker modes and independently checks the actual full debug content without native replay. This is archive-backed reader validation, not a native ARM64 runtime test.
- The generic readers have no newly executed negative controls for these new readers. The earlier four-positive/31-negative results do not transfer to r1150/r1151's reader implementations.
- No native ARM64 runtime was available. Neither ordinary activation nor runtime behavior is established by these receipts.

## Qualification boundary at drafting

The evidence here supports successful, archived ARM64 Release and ARM64 ASan Debug cross-builds for the exact frozen input set and working delta, with the stated per-profile PE/PDB and archive checks. It does not establish clean-commit qualification of HEAD alone, P1 Done, ordinary activation, native ARM64 runtime behavior, G1, or the latest full matrix. Full x64 ASan r1148 has separate qualified evidence in `P1AnalysisRepairsNative_2026-10-19.md`. Canonical Release analysis and latest Debug validation remain separate. Neither ARM64 build used -Analyze.

## Receipt index

Source receipts are under `D:\RedPrismBuildCaches\.build\p1-analysis-repairs-native-r1\`:

- `r1150\result.json`, `r1150\started.json`, `r1150\pre-objects.json`, `r1150-objects.json`, `r1150-archive-qualified.json`, `r1150-profile-qualified.json`, `r1150-profile-reader-r1-refusal.json`.
- `r1151\result.json`, `r1151\started.json`, `r1151\pre-objects.json`, `r1151-objects.json`, `r1151-archive-qualified.json`, `r1151-profile-qualified.json`.


The later exact terminal-status cast exposed by r1149 is outside this frozen input set; affected requalification is separate.

Later checkpoint: [completed affected repair validation](P1AnalysisRepairLatest_2026-10-19.md). The older input sets above retain their original scope.

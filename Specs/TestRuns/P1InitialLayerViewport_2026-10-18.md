# Initial Layers viewport ordering

Date: 2026-10-18
Base: `58be68e85310b80e3387150308f667850e3b31cf`.
Status: initial positive-extent correction QUALIFIED in focused Release/real ASan and adopted; P1 remains WIP.

## Reproduction and correction

Fresh canonical captures expose row/header collision. Main reviews the cheaper logging draft and corrects optional access, formatting, captured stderr and DPI/extent interpretation. The private trace shows initial Tree bounds [0,94,0,-24]; model selection uses the pinned Tree's one-pixel fallback viewport and retains a 27-DIP scroll. After real layout, Tree top94 but row top69/bottom97 persists through Prepare at96/144 DPI. A later adjustment model change restores the expected top96 (2-DIP inset). The diagnostic's 2,391 exact source inputs and raw captures/logs are preserved.

The unchanged-product startup control independently reproduces top69/bottom97/titleBottom94, actual PID87776 exit5. Main moves the existing direct/prepared row-publication block after the existing Layout in SyncChrome. There is still one Layout call per sync, with no added allocation, model traversal, preparation, composition or Present. No pixel, document, history or DxUi behavior changes. Two native RunSelfTest facts check before first preparation and after initial rendering. The full adoption source contains the complete self-test; no private focused return or logging ships.

## Actual focused qualification

| Scope | Runtime PID / exit | New facts | Exact fresh / inherited objects | Verified files | Artifact index SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Unchanged-product control | 87776 / 5 | 0 | 1 / 350 | 57 | AC04D015EDC6D30E6334035A24B6606E397FD71112F78561C3088FB7EA9DC62C |
| Release | 80760 / 0 | 2 | 1 / 350 | 70 | C6C1F7A749AD709ACFA1922FBA77692892A8027442AF7BE1C69E95554ECA988E |
| ASan Debug | 20460 / 0 | 2 | 1 / 350 | 60 | E67770AD4B12894A1F9E0A314B737DA3BD7EF8CD4AA9A8573938D7E7BBD7365C |

Each profile preserves all 2,394 exact source inputs in hash-addressed blobs. Main independently validates fresh compiler W4/WX/SDL/stdcpplatest/conformance/UTF-8, real ASan/forced header, all350 unchanged matching-profile source/object pairs, linked ISA, executable/full-PDB identity and every inventoried artifact byte. Matching-profile assertions before startup remain exact. This is a focused startup driver, not the full Application stream or the 41-case dispatcher integration.

## Captured visual result

Four corrected real harness captures (brush96, empty96, brush144, adjust96) exit0 and restore row0TopMinusTreeTop=+2 throughout preparation, with no -25 trace. Independent decoding checks all PNG chunk CRCs, bounded inflate, dimensions and nontransparent/nonuniform content. Main visually reviews the brush capture and confirms the title/opacity band and first row are separated. These are harness images, with no screenshot golden rebaseline.

- brush-96: 1278×830, SHA-256 `EB6D9CC29B6970090F56D96083259B3602CE883BD8A86AE0DC0E1DDE9D5BC952`.
- empty-96: 1278×830, SHA-256 `847F62856CE4E33F93353582AE67BB29B9C9BB837A4BDC395997042F495BE19D`.
- brush-144: 1924×1247, SHA-256 `5327014B2A47190FA25112C5C3E7812051C21F8358C91D7D677799ABF806AA6F`.
- adjust-96: 1278×830, SHA-256 `3F16A9061C606BD1759F58F44B56436DEA7800F527BF238DD47A00D0A78F0DD0`.

## Retained refusals and scope

Private ASan R1 fails compilation at C4702 because its focused driver unconditionally returns before the unused remainder of RunSelfTest. Preserve its actual build and all source bytes. R2 makes only that private early return conditional; W4/WX and all product checks remain. The full adopted source has no private return.

The ASan launcher's 180-second wait expires while existing shutdown/WARP tests continue. Main reopens and verifies the same owned child PID20460, exact image and creation time, waits on that process handle and records its genuine exit0 (20:01:16.738516–20:08:48.905343 UTC). No child is terminated/replayed, and native event-failure bounds remain unchanged.

The original geometry-instrumentation-only Release image later reports a separate apparent VEX instruction in the RBX-guarded terminal table of Application::RunAdjustmentClosureSelfTest. Preserve that original report/image/PDB; its scanner gap remains separately open. The unchanged-product failure control and both fixed profiles have zero ISA violations at their own exact layouts. A different passing layout does not repair the older diagnostic scanner issue.

Artifacts: D:\RedPrismBuildCaches\.build\p1-layer-header-layout-fix-native-r1 (control, Release, original failed ASan, independent readers and combined report), plus sibling p1-layer-header-layout-fix-native-r2 (corrected real ASan). Original raw geometry evidence remains in p1-layer-header-geometry-native-r1.

Minimized/zero-size restoration, later large-model scroll/resize, prepared-row/UIA activation, latest full x64 Debug/Release/ASan/analysis/ARM integration and measured G1 remain open. The earlier ARM64 Release/ASan receipts cover their own pre-fix source, and the full canonical ASan receipt covers its own 35-case source. No P1 Done or main merge.



## Latest canonical ARM64 Release and ASan — r1139 / r1140

Source commit `710bf4c5446f6c645a48bbc8f145ed701fadbb20` includes the production viewport change from
`4d7fac8`, adopted 41-case / 420-fact dispatcher fixture and final scanner correction. The only compiled-source
change from the qualified `9ce8b62` ARM baseline is `RedPrism/Application.cpp`. Both canonical builds run
serially with one worker and finish with actual exit 0.

| Profile / attempt | Actual PID | Archived files | Archive completion SHA256 |
| --- | ---: | ---: | --- |
| Release / r1139 | 55972 | 1,560 | `4D014AC564AA30C9BB06FB48B2AE4251D1A02264B1B9253DDC706AE2FA84B4D0` |
| ASan Debug / r1140 | 58564 | 1,564 | `17CBD818F377749D5F83F9EBE5FB1D2A33BEF9CB5398FEAA541DB7C8485367D2` |

Main independently verifies all 994 actual compiler records, required warnings/conformance/UTF-8 flags,
CRT/optimization/defines, real ASan plus its forced header, all five AA64 executable/full-PDB identities and
all archived bytes. Each profile has one fresh Application object and 993 byte-exact inherited objects;
all 351 Application objects occur in its actual link. The current baseline maps check object sizes as well
as hashes before preservation, avoiding the earlier stale-size reader defect.

The immutable 1,487-input / 1,483-Root-input freeze is
`571E376F26BCEC049E2F5B462278271C428DFE42144908CF202DAD90E9DA20D3`.
Four positive and 31 negative source-only reader controls pass. Main corrects the cheaper helper's current
source-commit attribution before preparation; both preflight and final receipts name the actual source commit.
Existing storage mappings, original caches and all prior refusal evidence remain untouched.

Cohort: `D:/RedPrismBuildCaches/.build/p1-layer-viewport-arm-native-r1`.
Final receipts: `r1139-main-qualified.json`, `r1140-main-qualified.json`; all archive, PE/PDB, mandatory-profile
and object-lineage readers exit 0. These are cross-builds. Native ARM64 execution is unavailable. Latest full
x64 ASan, broader H4/ordinary activation, measured G1 and the remaining P1 delivery gates remain open.

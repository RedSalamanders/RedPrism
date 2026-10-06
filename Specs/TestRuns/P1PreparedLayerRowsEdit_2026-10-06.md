# P1 prepared layer row drag, rename and upload acknowledgement — 2026-10-06

- [x] Read the current Caller fixture and verify its exact source hash.
- [x] Inventory all 17 distinct named checks and the separate summary.
- [x] Record independent literal orders, history, pixels, mask and lifetime expectations.
- [x] Include the added exact `WM_QUIT` cleanup control.
- [x] Check the primary's rename preparation repair and record the resulting source identity.
- [x] Primary integrates declarations, project/runner registration and normative validation; strict r11 checklist is prepared for the next source matrix.
- [x] Primary retains all predecessor inventories and qualifies the complete repaired frozen runtime matrix.

Status: **QUALIFIED STEP. Full Debug r193, Release r194 and real ASan r191, strict r12 predecessor proofs, Release analysis r192 and all three ARM64 r25 cross-builds pass on the same 846 inputs. Original failures remain archived. Production activation, accessibility adoption, complete H4 and G1 remain open.**
The literal inventory below supplied independent intended assertions before execution. The current matrix and archives at the end of this record qualify that exact inventory.
Source: [ApplicationPreparedLayerRowsEditTests.cpp](../../RedPrism/ApplicationPreparedLayerRowsEditTests.cpp), Caller workspace `C:\Users\eric\.codex\worktrees\p1-sampler-callers\RedPrism`.
Current verified SHA-256: `6808C575E6D7F4352D9B16E26F9FEC5E791E8F6BD9C10284888926BD0E60BBAE` (Main, after the signed/unsigned comparison repair).
Superseded source inventory SHA-256: `F17151720389F33AAEBEBB816FB086ABD7B7259BB330089FE6339EC1356AE8BB`; retained for the original ordering-gap record, not qualification evidence.
Entry: `Application::RunStandalonePreparedLayerRowsEditSelfTest(HINSTANCE)`; DxUi pin `271bd54be24eadf3f03ba5221d1be7be069860f2`.
Owning guidance: [Layers panel](../UI/UI_ApplicationChrome.md#layers-panel), [validation](../Testing/Testing_Validation.md), [authority](../README.md).

## Exact literal PASS inventory

There are **17 unique scenario/name pairs across 10 scenarios**, unchanged from the superseded inventory. The following are expected success labels extracted from source, not execution results.

```text
PASS application-prepared-layer-rows-edit.fixture.uses-real-hidden-WARP-application-and-scheduler
PASS application-prepared-layer-rows-edit.initial.prepared-tree-matches-independent-literal-group-child-sibling-and-raster-fixture
PASS application-prepared-layer-rows-edit.initial.fixture-starts-at-literal-sibling-with-no-edit-history
PASS application-prepared-layer-rows-edit.stale-held.group-collapse-keeps-old-painted-rows-while-replacement-is-pending
PASS application-prepared-layer-rows-edit.stale-held.held-real-worker-rejects-stale-row-reorder-before-publication
PASS application-prepared-layer-rows-edit.stale-held.held-real-worker-rejects-stale-embedded-double-click-rename-invocation
PASS application-prepared-layer-rows-edit.stale-held.held-worker-input-retires-off-owner-after-acknowledgement
PASS application-prepared-layer-rows-edit.stale-held.stale-attempts-preserve-literal-hierarchy-pixels-mask-and-history
PASS application-prepared-layer-rows-edit.refresh.settled-expansion-restores-literal-prepared-row-order
PASS application-prepared-layer-rows-edit.reorder.actual-prepared-tree-drag-creates-one-move-layer-history-entry
PASS application-prepared-layer-rows-edit.reorder.real-row-drag-moves-root-after-sibling-and-keeps-child-parent-and-literal-pixels-mask
PASS application-prepared-layer-rows-edit.rename-open.embedded-double-click-invokes-leaf-rename-card-and-focuses-its-text-field
PASS application-prepared-layer-rows-edit.rename-commit.typed-window-messages-and-enter-rename-the-invoked-sibling-as-one-history-entry
PASS application-prepared-layer-rows-edit.rename-refresh.prepared-rows-publish-renamed-sibling-with-child-and-literal-mask-pixels
PASS application-prepared-layer-rows-edit.undo.one-undo-reverses-only-ui-rename-and-retains-reordered-literal-hierarchy
PASS application-prepared-layer-rows-edit.closeout.shutdown-joins-worker-clears-borrow-and-retires-input-off-owner
PASS application-prepared-layer-rows-edit.closeout.fixture-quit-consumed-before-the-next-fresh-owner
```

Summary is separate and excluded from the named count; its expected successful form is:

```text
PASS application-prepared-layer-rows-edit.summary checks=17 failures=0
```

Each failed assertion emits `FAIL`; final return is 0 only when `failures == 0`, otherwise 5. Early fixture failures return 5 and can omit later labels and the summary. Qualification must compare the exact unique label set, summary and terminal process result.

## Independent literal expectations

`G`, `C`, `S` denote the generated identities of `Literal group`, `Literal child`, `Literal sibling`; expected orders are explicitly written arrays, not derived from the observed model.

| Stage | Exact storage order | Exact top-first order | Names and depths |
| --- | --- | --- | --- |
| Initial; stale attempts; settled re-expansion | `S, G, C` | `G, C, S` | Original names; depths `0, 1, 0` |
| Successful drag | `G, S, C` | `S, G, C` | Original names; depths `0, 0, 1` |
| Rename and prepared refresh | `G, S, C` | `S, G, C` | Only S becomes `Edited sibling`; depths `0, 0, 1` |
| One Undo of rename | `G, S, C` | `S, G, C` expected from retained hierarchy | S returns to `Literal sibling`; document checked, refreshed Tree order not asserted here |

G is a root group without asset/mask; S is a root without mask; C retains `parentID == G` and name `Literal child`. Row IDs are `index + 1`; G has children and is expanded when three-row orders are checked; C has the localized mask badge.
The fixture is 2×1. Separate literal expected arrays check every byte: S BGRA `[70,80,90,255,100,110,120,255]`, C BGRA `[10,20,30,255,40,50,60,255]`, C Gray8 mask `[0,255]`.
Image strides are 8, mask stride 2; formats and 2×1 dimensions are checked. C's mask stays enabled and linked, with no explicit placement. These are stored-raster assertions; mask thumbnails and composed GPU pixels are not independently asserted.
With captured baseline Undo count U0 and revision R0, initial/stale attempts retain U0/R0; drag expects U0+1, a different revision and `Move Layer`; rename expects one additional entry, a changed revision and `Rename Layer`; one Undo retains U0+1/`Move Layer`, restores S's name and leaves S active.
Initial active identity is S with pixel targeting. The Undo check does not require a particular revision ID or test a second Undo/Redo.

## Input, owner lifetime and quit cleanup

Initial sized `ChromeHost::Prepare` precedes enabling prepared rows; publication settles before the existing sync/prepare. No direct Tree scroll reset or hidden selection manipulation is used.
The held real worker has a ticket, prepared cache, reserved capture, exactly one capture/preparation and a nonzero reservation. Collapse retains the old child label while interaction is disabled; stale drag and leaf invocation must leave hierarchy/history unchanged and no Rename card open after settlement.
Drag uses `OnPointer` down/move/up. The sibling row's lower half is `TreeDropPlace::After`, translated to `PlaceLayer(G, root, S, false, true)`. A leaf invocation uses two ordinary down/up pairs; pinned Tree folder double-click toggles expansion. `WM_LBUTTONDBLCLK` is not sent as invocation.
After the rename-open assertion, a checked `prepare()` now precedes literal `WM_CHAR` messages through the owned HWND and `HandleAccelerator(VK_RETURN)`; the latter sends window key messages. Rename and Ctrl+Z each settle admitted jobs before document/history inspection.
The unique Application remains alive through explicit `Shutdown`, checks and quit removal. Scope guards restore the private keyboard state, signal the shared gate's release event before fallback shutdown on unwinding, and call idempotent shutdown before owner destruction.
Closeout expects shutdown complete, zero worker/reserved/releasing memory, cleared prepared rows/memory handle and reached off-owner input retirement flags.
After explicit shutdown, `PeekMessageW(..., nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)` must remove a quit with `wParam == 0`; a second filtered `PM_NOREMOVE` must find none. This assertion covers normal closeout; early-return paths do not reach this quit-removal check.

## Source repair checked and integration limits

The superseded inventory opened Rename and immediately sent text/Enter without another prepare. `ChromeHostRename.cpp:83–87` changes visibility/bounds; pinned `src/Controls/DxUi.cpp:389–394` advances the embedded interaction revision on visibility changes.
Pinned `src/Rendering/Embedded.cpp:445–453`, `536–539` and `556–559` reject key/character dispatch when that revision differs from the prepared revision. `Arrange`/`MarkDirty` does not perform `Prepare`; the primary independently confirmed this rejection and the Application `OnCharacter` path.
Current source lines 376–379 contain the primary's checked `if (!prepare()) return 5;` immediately after rename-open and before `WM_CHAR`/Enter. This repair and the new hash were checked from Caller source; all 17 labels remain identical. No runtime consequence or successful execution is inferred.
Primary registration in Main copies only the reviewed C++ file and adds its static declaration, project entry and runner route. The older Caller header/project is not copied wholesale. Scanner and runtime qualification remain due; the cheaper documentation task did not modify integration files.
Registry/norm integration must add these 17 labels and separate summary without replacing existing prepared-row core, interaction, retry or boundary coverage, or reducing any retained application/scanner inventory. This draft supplies no predecessor runtime totals.
Primary owns the frozen Main pressure/retry qualification and subsequent Edit integration/full runtime/scanner qualification. Separate Main Release r182 and ASan r183 results do not qualify, and are not attributed to, this Caller source hash.
Native activation, later resize/scroll reliability, full x64 Debug/Release/ASan gates, native ARM64 runtime and performance qualification remain unclaimed by this record.

## Current qualification attempt

Debug r184 failed before runtime: C4389 at the row depth comparison was promoted to an error by the unchanged warning gate. The primary changed the independent expected depth to uint32_t, matching the pinned TreeItemData field; expectation values remain 0 and 1. The failed actual child exit and all 1,115 archive copies are preserved and independently verified at p1-layer-rows-edit-x64-Debug-r184-build-failed-owned, completion hash 5B9CC459B293352900ACBC7C591682FD167BA54E8E8FD7958495DDDBA06FD152. Formatting (723 files) and all 15 skills pass before fresh r185. r185 remains unqualified until actual exit and independent registry/archive verification.

The pinned DxUi snapshot currently copies all Tree row names, including offscreen rows (DxUi.Accessibility.cpp:1704-1721). The 10,000-row /1,024-name owner snapshot cost and a preserving solution remain open; worker-prepared text alone does not close that cost gate.

## Passed steps; current matrix

- Debug r185: child 96892, exit 0, 2026-10-06T02:52:52.778037 to 03:02:27.620647 UTC; 846-input freeze ED3FCEC4B15C75B6D42F60BDC47B3C8B49CD3254FE4093067AA9D682A16A269E. All 17 named expectations plus exact zero-failure summary pass, yielding 9,960 named application controls /84 stages. Strict r11 preserves every predecessor and verifies the source delta, pin and private components. Archive 1,116 copies; completion B2739A42D53C7AAB32B42F50BD9856344376A4F6C9B2D431CC3C308806803AF6. Registry supplement 11FDFCB903C40833E1CAF911AD2DE30CCDB103CEBA959679710E9BF86EF4D8FE.
- Release r187: child 91936, exit 0, 03:08:15.713765 to 03:14:22.782165 UTC; exact same source freeze, strict r11 passes. Archive 1,116 copies; completion 58AD4086E2A32D7F898672C6724BFBF9BC23DA8B03A509008A51572952F79217.
- Release analysis r186: child 15032, exit 0, 02:58:09.221134 to 03:05:09.879120 UTC; exact same freeze. All warnings/stack/ISA gates unchanged. Archive 1,129 copies; completion 8F1C2C3F953D5E634E98433322A54CD8214E8BCD20CF115E5B403924230194F5.
- ARM64 r24: all three cross-build children exit 0 on the exact same frozen source; independently verified archive copies 917/917/921, completions BCC87BE759C3F481F129134C6742AB205D1EDFA37E931FF62DB0290C47ABEEA9, 3416C8E65B03BF586ACF6768FC22BFE0FB7647CE459939A3C21DF917BFE26C94, D73F82017C941E485D865DB32A64F80A3B5DDE1E0FF88E79DB1412F635FF624C. No native ARM64 runtime claim.
- Real ASan r188: child 108508, exit 1, 03:16:22.595233 to 03:21:32.051877 UTC, exact same frozen inputs. CompositionTests asserts resumed at GpuRasterUploadJob.cpp:371 before the application registry executes. Archive p1-layer-rows-edit-x64-ASanDebug-r188-failed-cancel-resume-owned verifies all 1,122 copies; completion 8052F7570560473D4C94694CC13F1B3AFD06FB5275EFAA98303F98AC3282807C. This failure is preserved; there is no automatic retry or successful matrix claim.

The cancellation acknowledgement can already be queued when the fence owner callback runs. The old fixture's later marker may run before a requested Resume is promoted, so its retained-ticket check does not guarantee terminal worker acknowledgement. A new independent fixture holds the only worker, queues cancellation, acknowledges the real copy fence first, verifies the original charge/ticket survive, then releases the skipped worker and requires one Cancelled callback and zero remaining charge. Its pre-repair ASan build/runtime proof and a fresh repaired matrix are due.

The primary isolated a DxUi branch from the exact pin: codex/p1-prepared-tree-accessibility, local worktree p1-prepared-tree-accessibility/DxUi. The unchanged-library Release 10,000-row/1,024-name snapshot baseline executes all 12 rounds, allocating 41,924,539 C++ bytes and reading 10,000 items per update. Source/product/raw receipts are retained in that repository. Optional immutable UIA source and viewport capture are drafted, but neither library qualification nor the RedPrism pin/ownership adoption is complete. These remain separate P1 cost/ownership gates.

## Deterministic cancellation regression and repair

The original assertion requires a successful second `Resume` from the GPU callback even when cancellation already queued the skipped worker acknowledgement. The scheduler correctly rejects that duplicate. The repair accepts a rejected resume only when the original job's cancellation token is set; ticket, phase and receiver checks remain unchanged. No second record or reservation is created. Final completion still waits for both producer acknowledgements.

Two independent gated tests cover the real copy fence and mapped-row callback arriving before the queued cancellation stage. The first preserves all 16 literal pre-cancel copied bytes, the original 1 MiB charge and zero callbacks until the worker is released. The second preserves all 16 destination guard bytes (89), lends one mapped slot, submits no copy, and retains the same charge. Both require one Cancelled callback and zero remaining reservations/receiver state. There are 15 `queued-ack` plus 11 `queued-rows` distinct controls.

Unfixed ASan build r189 exits 0; unfixed runtime r190 exits 3 after the deterministic gate reaches the original assertion. Their independently readback-verified archives retain 1,122 copies each; completion hashes 56036445BEEB8B0F81595D5F47FA565171DA629176D2ADD08AE01B72FA70FCD6 and BD6D13E278304A03C0492F18A55CA4B3BC5CC970799E546404618BD75A82DA8E. The unfixed source freeze is 9ABFF8B8F373745C2FF1DA323C79CECBA599F3394A434B656A4667027B224D3A.

Repaired inputs: 846 files, freeze 935B36D17F8017F2F0F3AA0A2D7A88831F163AA3F10CF8F9678DFE0D3D0011BA. No source changes occurred during these actual children.

- Full real ASan r191: PID 84532, exit 0, 03:34:13.007053 to 03:50:21.893135 UTC. All 26 new controls and every earlier composition control pass (1,694 total). Strict r12 preserves the 7,197 document, 9,960 application /84 stage and 7,994 stress inventories, both private components, source delta, pin and unchanged ISA fixtures. Archive `p1-layer-rows-edit-upload-ack-x64-ASanDebug-r191-owned` verifies 1,122 copies; completion ABF918D4AEF67D96AA3766A8E50582A4E278B802A08DF7AF1394440B8F850BD9. Actual expected heap-use-after-free probe hash 5A00353C0660AE8595269C7B552DDAB0F9B2E3C327E40F176DD6F6B9EA1F1089; independently retained ASan supplement receipt E5C3F8832747CC38AB1DAA0F661EA35E7DA2B35D6532E9C438E8C038781156D4. Registry/helper copy receipt 4CF9F88445C247C1A36588F3BFE66CAF10DFB8B776F3FCD97E5C05DAC7C47718; original completion is unchanged.
- Release analysis r192: PID 15988, exit 0; 1,129 verified archive copies, completion 2EAC535732AC6F57ECEB35D7C11E738B6745970CD75B4688799992ED59DC8A4A. Warning, stack and ISA gates remain unchanged.
- ARM64 r25 cross-builds: Debug/Release/ASan Debug exit 0 on the same freeze, with 917/917/921 verified copies. Completion hashes 0287FC2769B867805207B4F6D7C0B6C751F7AA20A47C8A10EA249184D4C23B4C, 47FD92244690EC03F4A3D3AF1A6BBEBB6BDB21EE42876662CF0B434002000EE4 and 04EF147A6028EFCB3078B83725B6817D1140951628FFD54050521D18DB6F587E. Native ARM64 runtime is unavailable.

Fresh full Debug r193 and Release r194 qualification now pass with the exact same repaired source freeze. The successful earlier Debug r185 and Release r187 do not qualify the repaired GPU source. Prepared row activation, complete first-display consumers and measured G1 remain separate open requirements.

## Completed repaired matrix

- Full Debug r193: PID 96536, exit 0, 03:52:45.938897 to 04:02:07.728423 UTC. All 17 edit plus 26 queued acknowledgement controls and every predecessor pass; strict r12 verifies the full registry, original routes, 84 stages, private components and exact source delta. Archive `p1-layer-rows-edit-upload-ack-x64-Debug-r193-owned` independently verifies 1,115 copies; completion FFA0BCBF2446C322286D88582A2397161BFEA698B331AD05789ED8006FC263A0. Registry/helper readback receipt B75AD46BE67EAB933542595268ED27BB4C6AB10A9327765995D4E9FF5FD0B5B2.
- Full Release r194: PID 88308, exit 0, 04:03:12.186403 to 04:11:53.009208 UTC. Strict r12 passes with the same exact repaired source freeze; archive `p1-layer-rows-edit-upload-ack-x64-Release-r194-owned` independently verifies 1,115 copies; completion C32FA3897CA7F43B38FE883FC50DE6B5EDEE0DC13C681B8A6C0E1C11A4F1D13C. Registry/helper readback receipt A0C52BE7EDE9E543B14490F6AD81E73D827BFDD01532FA7111FD51C84F33FF02.
- All 15 skills and formatting across 723 files pass. The earlier real ASan r191, Release analysis r192 and all three ARM64 r25 cross-builds use the identical repaired 846-input freeze recorded above. Every original archive completion remains unchanged after independent supplements.

This completes the registered drag/rename and producer-ordering repair step. It leaves production prepared rows disabled and does not close P1. The isolated library's real-client callback reads the new row name correctly but its invocation times out before owner execution (r7: S_OK read, 0x800705B4 invoke, zero replacements); the complete failed library inputs/products are retained separately. Library reentrancy, consumer charged reader lifetime, large-row measured costs, remaining H4/P1 callers, Native activation and G1 remain requirements.

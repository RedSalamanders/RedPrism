# P1 prepared layer rows edit draft — 2026-10-06

- [x] Read the current Caller fixture and verify its exact source hash.
- [x] Inventory all 17 distinct named checks and the separate summary.
- [x] Record independent literal orders, history, pixels, mask and lifetime expectations.
- [x] Include the added exact `WM_QUIT` cleanup control.
- [x] Check the primary's rename preparation repair and record the resulting source identity.
- [x] Primary integrates declarations, project/runner registration and normative validation; strict r11 checklist is prepared for the next source matrix.
- [ ] Primary retains all predecessor inventories and qualifies the complete frozen runtime matrix.

Status: **PARTIALLY QUALIFIED. Full Debug r185 and Release r187, strict r11 predecessor/17-control registry proofs, Release analysis r186 and ARM64 r24 cross-builds pass on the same 846 inputs. Real ASan r188 fails on a GPU upload cancellation/fence resume assertion; its complete failure archive is retained. No complete matrix, production activation or G1 claim.**
This draft records intended assertions for integration; placement in `Specs/TestRuns/` does not make it run evidence.
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

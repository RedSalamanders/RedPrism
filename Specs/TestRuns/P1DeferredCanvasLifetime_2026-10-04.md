# Deferred canvas lifetime preflight qualification

## Qualification checklist

- [x] Review exact pending detachment: 21 independent draft controls pass Debug, Release and instrumented ASan.
- [x] Preserve the actual r1 compiler failure and independently verify its 967 source/evidence copies.
- [x] Qualify the complete private application in Debug, Release and ASan: 74 ordered stages and 8,358 controls/profile.
- [x] Retain all 8,118 predecessor controls and 160 original-stream cancellation/80 accepted-action controls.
- [x] Independently verify every archive copy: 790 Debug, 790 Release and 792 ASan.
- [x] Compare all 515 owned candidate C++/header/shader/resource inputs across these profiles: identical.
- [x] Verify `/fsanitize=address` in the actual ASan application's compiler command log.
- [x] Preserve only changed owned sources and portable project registrations in the experimental worktree.
- [x] Qualify 192 injected modal-message/reentrancy controls in all three private profiles: 8,550 total controls/75 stages; verify 793/793/795 archive copies and 517 identical owned inputs.
- [ ] Qualify stale document/core/generation, interrupted-owner and repeated close cases.
- [ ] Qualify accepted window/end-session actions and shutdown during held/live/presenter-owned states.
- [ ] Complete other callers, native integration, the fresh normal matrix and measured G1.

Status: private source checkpoint on `codex/p1-opacity-host-proof`, extending `a0ab3f0`. The native branch has
no active deferred canvas lifetime caller. This is not a completed P1 release or a fresh normal worktree matrix.

## Behavior and independent reference

A live brush can own the earliest ordinal while a close waits for its physical release. The new owner performs
nonmutating lifetime confirmation before waiting for that brush. One additional fixed owner detaches only the
exact pending document/ordinal pair from the normal eight-slot queue. Its charged request stays alive through
both presenters. Detachment preserves FIFO and the last admitted ordinal, so removed barriers still prevent
adjacent preview/Wand coalescence. Later selection considers the original detached ordinal before younger inputs.
The request never obtains a document/pixel snapshot before its prerequisites acknowledge.

Unreplayed admitted brush input counts as unsaved work. Explicit request-based output selection does not replace
the older active request. Prompt or picker Cancel finishes only the lifetime receipt and retains the original
brush opening, captured settings, path and pending receipt. Save selects output before sealing its accepted
prefix; Don't Save requests cancellation of that exact opening. The dependent action waits for the original
receipt and suppresses replacement after failed/stale prerequisites. End-session cleanup matches its generation.

The 16 cancellation scenarios are four inputs (New, Close Project, Close Window, End Session), held/live brush
states and prompt/picker Cancel. They verify the original receipt remains pending and charged while the opacity
worker retires, source pixels and opacity-only history remain unchanged, and later Move/Up commits the same
stream. The 10 accepted scenarios cover held/live Close Project/New Save and discard plus two write failures.
They verify admission alone does not stop sampling, younger input cannot pass the detached ordinal, saved files
contain only acknowledged prefix pixels, and a failed write preserves the document and suppresses close.

The independent literal reference is a 32x24 uniform BGRA `{19,71,129,255}` image, a size-3 opaque red tip, Down
at `(8,8)`, accepted Move `(12,8)` and later `(20,8)`. Cancel permits red at all three path positions on the same
receipt; accepted Save contains red only at the first two. `(8,11)` remains the source pixel. Opacity is `.25`,
and its separate history entry precedes the brush entry. No legacy golden changes.

## Immutable run evidence

Archives under `%LocalAppData%\RedPrism\QualificationArchives`:

| Archive | Inputs / evidence / verified copies | Completion SHA-256 |
| --- | --- | --- |
| `p1-canvas-lifetime-application-r1-Debug-failure` | 761 / 206 / 967 | `EF7BD1E9DC87B6F791622D530FE6BFA804E65677D73295A74F41AD9191C76696` |
| `p1-canvas-lifetime-application-r2-Debug-qualified-private-full-application` | 761 / 29 / 790 | `D9193A92E91D8F76E3CF29E96C141B996E077D96750CFED8D92E98B12E01CA7A` |
| `p1-canvas-lifetime-application-r3-Release-qualified-private-full-application` | 761 / 29 / 790 | `3740999BB9D8819BC6D9D246295647385C280C35DEC100C12A60C90254D967EE` |
| `p1-canvas-lifetime-application-r4-ASanDebug-qualified-private-full-application` | 762 / 30 / 792 | `CB3437B301D490125251B570B2F11FDAB1B1E99304FF19217F15456484AFB3F0` |

Source manifest SHA-256: Debug `F4950E036E98660066B2EA2FD508BF45A1195DD80B23CD7D00B15B47120F9E95`,
Release `3F54DC26111C9363EFFE569EB5A91DC7CD5ED5039A6ACA7F87AF00BA5EF5FCC5`,
ASan `27DEE86643963F05B03AF332209668D70F1E20EEC2BFDDBB945A1BEE8BD1CD81`.

Evidence manifest SHA-256: Debug `8BFBD3712B1FEA16723FAC873CD3EA4A221B1117AE73500A12B89FB3F6F7718F`,
Release `0E3E354CAAA3E8D74A26A2F9461CF637493CCA2F3C53D3E03F1414D3BD4E42C4`,
ASan `97A87056CD9385E512CC0CCE1701D7EC53E88F72EC67F6305D32F42AAC1F358A`.

The r1 actual compiler exits 1 on an extra closing parenthesis in the cancellation test. Its child receipt records
that exit even though the throwing wrapper never assigns its top-level Build property. Primary review also
corrects the initial draft's wrong cancelled-brush/new-stroke expectation before qualification. The r2 fix preserves
all literal expectations and restores test error counters after write failure. Source/helper/compiler/dependencies
remain frozen through each actual process and independent archive verification.

### Modal candidate r1 Debug compile failure

Archive under `%LocalAppData%\RedPrism\QualificationArchives`:

| Archive | Inputs / evidence / independently verified copies | Completion SHA-256 |
| --- | --- | --- |
| `p1-canvas-modal-application-r1-Debug-failure` | 764 / 207 / 971 | `3320DF7331FAD7965390F6BBF3C62353255BB4BCF152AF044BC73707F88A3454` |

Source manifest SHA-256: `6F5FF81A7C1603B26098C230D6E54C515FB151E34511B5E6B4713FCFC5902959`.
Evidence manifest SHA-256: `8C406DF8A739E2FBFA99B6B489A3C37D6CB42905E53D8F9EF2DC12701AB39D82`.
Independent verification completed at `2026-10-04T07:57:49.8019573UTC`.

The modal test candidate's actual Debug build child exited 1 because the test referenced nonexistent
`_injectPointerLossForTest`. The build helper threw before assigning its top-level `Build` property, which
remained null; the child receipt is the evidence of the actual exit code. Runtime did not run. This records a
test compile failure and does not indicate a product runtime failure or qualify the modal behavior. Corrected
all-profile qualification is recorded below. The existing Debug/Release/ASan basic lifetime qualification claims above remain
unchanged and apply only to their recorded private application runs.

## Corrected modal lifetime qualification

The corrected test sends pointer-loss messages on the owner thread, which `HonorsPointerLoss` already admits.
It also asserts that loss routing is enabled; no product test flag or product behavior was added. Sixteen
held/live cancellation scenarios each preserve the original ten literal expectations and add presenter-owner
reentrancy and focus/capture-loss checks: 192 new checks. All predecessor expectations remain unchanged.

`p1-canvas-modal-application-r2-Debug-qualified-private-full-application` passes actual build/runtime exit 0,
all 8,550 controls and 75 stages. Its 764 inputs and 29 evidence copies (793 total) were independently verified
at `2026-10-04T08:06:51.3585474UTC`.

- Completion SHA-256: `63C8A08FB8B5CE54B63ADD592A294854B46E54829CB41613C2743DCF27E7F01C`.
- Source manifest: `8C2C9E15E24AC5F9A1D6092D8FE6D7DCCE666F27A8BCA7D5946AE69E00F0AF63`.
- Evidence manifest: `0CFF9318F5F5616CDB12510720EBA8280E52DA69C7F6FB2DFE6A86C30A3D207C`.

Release and ASan also pass actual build/runtime exit 0 and the same 8,550 controls/75 stages. All 517 owned
C++/header/shader/resource inputs match across the three profiles. Actual ASan compilation contains
`/fsanitize=address`. Every archive copy was independently verified:

| Archive | Inputs / evidence / copies | Completion SHA-256 |
| --- | --- | --- |
| `p1-canvas-modal-application-r3-Release-qualified-private-full-application` | 764 / 29 / 793 | `B5C73EC706C6A39C53EDD1FE4AD01608CA872C1AAFCAB7ABA93F2D5334302578` |
| `p1-canvas-modal-application-r4-ASanDebug-qualified-private-full-application` | 765 / 30 / 795 | `EFABE880A0E1777093ED16F05ECF4D8167C6B63A9074095A0F5A3DC20EDC64D7` |

Release source manifest: `70FB907CC6F454682A964C19BEC3B59442628EEDD1789DBFEE25CDFD04C5340D`;
evidence manifest: `779AA1E04628331ED438DC6416D446966D7690962CECF9984F2124958AA88D62`.
Independent verification: `2026-10-04T08:12:18.6669691UTC`.

ASan source manifest: `C871F7F9D5E28484EB15C3285D7C88925BB7FAF07B7A1143DBA276744886A010`;
evidence manifest: `04214FBB555F5585FF5076A791FFA4537F463B6C0F06B7E5BBDDE4A07094BBF9`.
Independent verification: `2026-10-04T08:25:51.9489357UTC`.

The missing vendor DxUi PDB warning remains recorded in all private profiles. This does not qualify a fresh
normal worktree matrix, native activation, actual output-picker modal injection or G1 timing.

## Combined lifetime-gates failures and corrected qualification

The combined candidate adds 200 authority and 108 window/end-session controls. Its shutdown fixture now has
104 controls across held/live streams, accepted discard, accepted Save, and presenter-owned shutdown. Expected
full application qualification is 8,962 controls/78 stages. Corrected Debug, Release and ASan pass the complete
suite with actual build/runtime exit 0. All 520 owned C++/header/shader/resource inputs match across profiles.
This qualifies the private candidate; native activation and G1 measurements remain open.

Each failed Debug attempt built successfully and its actual runtime child exited 5. Every source/evidence copy
was independently verified before changing the candidate. The throwing wrapper left its top-level Runtime
property null; the archived child receipt records the actual exit and is retained by the verification receipt.

| Attempt / archive suffix | Inputs / evidence / verified copies | Completion SHA-256 |
| --- | --- | --- |
| `r1-Debug-failure` | 780 / 243 / 1,023 | `0EAA77D3518974B88DA4197E24D7040F84B6CE23DD9B7A1DC7659C719691E138` |
| `r2-Debug-failure` | 780 / 243 / 1,023 | `74E268C4811390188126331945F37EC84DCC9CBB6C48EA4C542C00745D286250` |
| `r3-Debug-failure` | 780 / 243 / 1,023 | `67D189048D486A014CC167C40EC209B4D33830F735D6FA02D397E48B2BFCE3D7` |
| `r4-Debug-failure` | 780 / 243 / 1,023 | `90ECB2F7D8AFE983FAF5911BC76D446E16A7A8DF082C3E72BF0814C2B0061334` |

Archive names begin `p1-canvas-lifetime-gates-application-` under the qualification archive folder above.
r1 exposed two fixture errors: the original brush request is selected even while its opacity prerequisite is
held, so shutdown reports `Cancelled/E_ABORT`; and destruction posts WM_QUIT, which each isolated fixture must
drain before starting the next host. r2 passed 77/78 shutdown checks; the remaining expectation incorrectly
required a live core after accepted Don't Save had already cancelled it in preflight. The corrected fixture
keeps that cancellation case and adds accepted Save to verify core retention. r3 passed 101/104 shutdown checks;
the complete host source synchronously acknowledged its live Save prefix before the intended shutdown point.
r4 passed 102/104 shutdown checks: stopping at preflight also deferred the discard source's cancellation,
while the fixture still required it to have executed. r5 exercises the actual discard source for discard and
stops at the preflight boundary before Save acknowledgement, preserving both distinct ownership expectations.
No product
receipt, history or pixel expectation is rebaselined to accommodate these fixture errors.

The corrected r5 Debug archive is `p1-canvas-lifetime-gates-application-r5-Debug-qualified-private-full-application`.
All 780 inputs and 29 evidence copies (809 total) were independently verified at
`2026-10-04T09:06:14.7381632UTC`.

- Completion SHA-256: `6F9848244C714C2F0B7C36DE721E9B2DF6D61F3703AA2110893A10A4A9215558`.
- Source manifest: `F6DFBDF5FC90DA6243B637268D6D6B94E39E84D4EC50175724CD509331D0EED5`.
- Evidence manifest: `53B99734B0B6301FEA1D689F8C62B3050F04AFED9AC32287D9B137D1C91AFCBD`.

Release r6 also passes 8,962 controls/78 stages, actual build/runtime exit 0. All 780 source and 29 evidence
copies (809 total) were independently verified at `2026-10-04T09:12:21.1488428UTC`.

- Completion SHA-256: `0EE20AE6334099B737F46A9B319EAFBA4B8D4BCB5E1517ABAA9B311955C2D572`.
- Source manifest: `129A142AE3BD2F88003FA40098073B9693B8EE12E34BB497790C0BEF98C388A6`.
- Evidence manifest: `47F5225DE8E56752544DE3DDF678BF442C8ED1BA27D4373AC413237D20B6270B`.

ASan r7 passes the same suite with actual `/fsanitize=address` compilation and build/runtime exit 0. All 781
source and 30 evidence copies (811 total) were independently verified at `2026-10-04T09:23:55.0528973UTC`.

- Completion SHA-256: `8197D0BAC816FA1CE6269502B2BB014271C4C50E6EF7DD1680D1C7C8F3131C50`.
- Source manifest: `CA3EE8C9249690AF239379671E2D5F18AABF8AA3AF4D7B7B2FCDC057DC8928EF`.
- Evidence manifest: `63E3CD9D3670751841C4B3070725D63141DCB8438DF2975DC2F6E84C778DF693`.

Archive suffixes are `r6-Release-qualified-private-full-application` and
`r7-ASanDebug-qualified-private-full-application` under the same prefix above. These runs tighten refusal of
an unreplayed stream owned by a different core and retire a stale queued stream in the same owner action.
They retain every preceding literal pixel/history/receipt expectation.

The normal managed Debug build separately failed during DxUi vcpkg restore, before MSBuild or ISA gates, because
its 64-character output folder made a temporary WIL directory too long. The proposed build-path correction keeps
the full identity in a marker and makes resolved props authoritative for provenance. Its 29 focused path controls
and eight existing provenance refusal cases passed; a later timestamp assertion adds one more path check.
Normal restore now succeeds at the short path and the actual application compiles/links. The normal solution
build then fails on old mutable-vector fixture calls in HostTests, IoTests and CompositionTests; ISA scanning was
not reached. All 735 source copies and 34 compiler/link command logs were preserved and independently verified
before fixture migration. The reviewed I/O/benchmark and composition/readback fixtures now use explicit
`DocumentFixture` construction/edit/sealing; numeric expectations are preserved. The deep worktree also exposed
formatter command-line overflow; bounded batches now pass all 617 sources. Two failed normal solution attempts
precede migration of the remaining import/cancellation/publication and benchmark fixtures. Corrected normal
Debug r3 builds all five images, passes matching-PDB/full-image ISA scanning and all compiled rejection controls,
and writes/validates actual DxUi provenance. Its actual child PID 50036 exited 0 at
`2026-10-04T09:14:30.5799552UTC`; all 733 archived inputs were independently verified before further integration.
Freeze SHA-256: `3D23FBD86C0A892694085FDC6715A5A706702A86B1F880264D2DA3B9565A60E3`;
result receipt: `C0C3D5D49A291441804E65CA63481D64BDE982CAE4B9FAF0A402790DA693D25D`.
This is build-only qualification. A fresh normal runtime/configuration matrix remains open.

## Remaining limits

The presenter-lifetime guard passes injected Win32 focus/capture loss and nested owner actions with quiet
prompt answers. The combined candidate additionally covers positive close/end-session actions, stale/reentrant
authority and actual shutdown within its named cases. Actual output-picker modal behavior, broader repeated
close/live-tool routes and native integration remain explicit follow-ups. Private builds reuse immutable DxUi products and retain
the missing vendor-PDB warning, so native symbols/ISA and the normal matrix remain unqualified by these runs.

Only owned source and portable project entries are copied. Private imports, proof projects, seed receipts,
products and dependency snapshots remain outside Git. Native activation, ARM64 runtime, other tool callers,
history accounting, upload/chrome bounds and measured G1 remain open. These checks do not qualify RAW decoding
or Photoshop performance.

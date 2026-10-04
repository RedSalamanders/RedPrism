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
- [ ] Qualify real modal-message/reentrancy, stale document/core/generation and repeated close cases.
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

## Remaining limits

The new presenter-lifetime guard and shutdown fixed-input retirement are implemented but broader actual modal
and shutdown scenarios remain unqualified. The current cancellation tests use quiet prompt answers and do not
establish real Win32 focus/capture-loss behavior; that is the next gate. Positive window/end-session actions and
stale/reentrant authority remain explicit follow-ups. Private builds reuse immutable DxUi products and retain
the missing vendor-PDB warning, so native symbols/ISA and the normal matrix remain unqualified by these runs.

Only owned source and portable project entries are copied. Private imports, proof projects, seed receipts,
products and dependency snapshots remain outside Git. Native activation, ARM64 runtime, other tool callers,
history accounting, upload/chrome bounds and measured G1 remain open. These checks do not qualify RAW decoding
or Photoshop performance.

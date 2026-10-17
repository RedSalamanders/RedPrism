# P1 lasso opening settings and exact history authority — 2026-10-17

## Qualified scope

Private CPU model extension, independently qualified in Release and real x64 ASan. This is a prerequisite for the outside-hit physical gesture path. It does not activate ordinary routing or close S5, G1 or P1. The native integration remains frozen at code `29a8ac2` for its separate ARM64 matrix.

`LassoOpeningSettings` seals the accepted Down descriptor's kind and antialias flag. Subsequent header changes cannot replace those values. The existing bool overload preserves its live-kind validation and live-AA-at-Apply behavior. Invalid kind/mode/nonfinite coordinates refuse before changing the pending intent. Both overloads now capture `DocumentHistory::Authority` and require the exact history epoch at acquisition; a foreign Begin can change neither document generation nor revision.

## Behavioral proof

The new family contains 46 facts: 42 reviewed settings/authority/lifetime controls and four foreign-Begin controls. The full suite retains all 22,381 predecessor facts. Numeric references include Rectangle anchor `(8.5, -2.5)` rounded to `(9, -3)`, signed Ellipse rounding, unrounded polygon/freehand points, captured true/false AA, legacy live AA, moved-from and one-shot refusal, original-ticket binding, exact pending-intent preservation and final memory/scheduler refunds. New behavior uses primitive numeric and state-transition references; legacy goldens are unchanged.

## Retained regression and repair

Release R1 actually exits 1 with exactly these failures; all 22,381 predecessors and 44 other new facts pass:

- `lasso-opening-settings.history.captured.rejects-opening-and-preserves-foreign-pending`
- `lasso-opening-settings.history.live.rejects-opening-and-preserves-foreign-pending`

Actual child PID 87132, 02:39:09.285338–02:39:18.078933 UTC. Main verifies all 1,615 archived files, 274 command records, exact source/dependency/product bookends, full ordered assertion roster, PE/PDB identity and ISA. Completion SHA-256: `13E8A95CBE2047A1CDBE38203DFBA6579065BF3214D62D10007418BF597FB36F`. This remains a failed receipt.

The fix adds/carries the fixed-size history authority in `LassoOpeningInput` and checks `history.IsCurrent` when acquiring it. All 46 predicates remain unchanged. R2 refuses an existing temporary directory before starting a compiler or test child; its log/receipt are retained. R3 uses a fresh attempt directory and preserves the original source/output paths.

## Passing profiles

| Profile | Actual child / exit | Assertions | Compiler records | Archive files | Completion SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Release R3 | 73592 / 0 | 22,427 | 274 | 1,618 | `BD541A292D4146ABEF852FAEFA3D9DED818E98D985AD5999E31B475E89361169` |
| Real ASan R1 | 40092 / 0 | 22,427 | 274 | 1,626 | `264988ECCEB9865A553F1334FF88A38E2156587F34F348C9605F84FFB73ED1C3` |

Release R3 is an incremental rebuild: the two changed production files compile against the same qualified predecessor objects, with 274 exact command records verified. It is not a fresh 274-source rebuild. ASan freshly compiles all 274 sources with actual `/fsanitize=address /MDd /Od /Zi` instrumentation and the required header, and runs its own `--asan-probe`: PID 62352 exits 1 with `heap-use-after-free`.

The strict reader verifies the entire ordered stream and the original full predecessor chain, not just totals. Main's source-only reader controls include four positives and 55 negative mutations; the ASan stream retains its original profile-specific predecessor witness. Missing/reordered/duplicated assertions, unexpected failures, wrong summaries/exit/profile and truncated streams refuse. These synthetic controls are reader validation, not application execution.

## Artifact identity

| Profile | EXE SHA-256 | PDB SHA-256 | RSDS GUID / age |
| --- | --- | --- | --- |
| Release R3 | `B94705E08F4866CDEC74216035D62427F36474DCB75E0225B43CE1039C3C9229` | `C48D5A0B52ABFD74AF90358AF141E50AE9D394E3EFAF4A6F05026FB25A33ED1D` | `02C23526-23A4-4FBC-B418-A36D23D60CC5` / 2 |
| Real ASan R1 | `8189B7691124104345F8DB7FFD3629CFA05A3999E90E0F436502DDF6198D8556` | `5358C6C82DDC86E381EA0A2F5C760690DDFD467058CF79E38024848E0E8A5804` | `7AC16EF8-2BAD-43F5-A4DA-24D3217C6234` / 1 |

Both profiles have matching full PDB identity, zero ISA violations, exact source/dependency/product bookends and independently verified full archives.

## Retained local receipts

- `.build/P1Drafts/S5PressSettingsRelease-r1-Main/main-failed-verified.json` and `main-independent-failed-roster-r1.json`.
- `.build/P1Drafts/S5PressSettingsRelease-r2-Main/main-pre-dispatch-refusal.json`.
- `.build/P1Drafts/S5PressSettingsRelease-r3-Main/main-qualified.json`.
- `.build/P1Drafts/S5PressSettingsAsan-r1-Main/main-qualified.json`.
- `.build/P1Drafts/S5PressOutside-r1-Main/registered-settings-fixed-source.json` and `history-guard-fix.json`.

Generated archives/products stay untracked under `.build/`. The next private own-writer/rectangle-plan draft is uncompiled and excluded from these receipts. Native latest-source x64 and remaining ARM64 validation, actual outside-hit Win32 routing, complete phase/display retirement bounds, ordinary activation and measured G1 remain open.

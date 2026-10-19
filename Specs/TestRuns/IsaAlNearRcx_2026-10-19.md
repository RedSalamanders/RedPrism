# Exact AL/RCX guarded switch-table scanner — 2026-10-19

**Evidence date:** 2026-10-19  
**Scope:** strict scanner and fixture qualification against the unchanged original x64 Release image/PDB.

## Result

`main-final-scanner-qualified.json` records 14,133 exact default PASS records: 14,012 predecessor records plus 121 new records. The final default launcher exited 0 with source bookends. Compiler positive/negative controls passed; the predecessor and new-control r2 receipts also report exit 0. The scan was repeated with the corrected scanner against the same original image and PDB; the native Application was not replayed.

The recognized AL-near-RCX form is narrow: the exact guarded terminal dispatch, body-flow and table proof must agree with PE bytes and decoded instruction boundaries, with matching ownership and bounds. Malformed, unknown, overlapping, mismatched, or out-of-range rows remain rejected; adjacent real vector instructions remain gated. This adds no runtime allowlist exemption. See the current normative scanner section in `Specs/Build/Build_Toolchain.md`.

Main corrects the cheaper fixture’s source-root/generated-output handling, arithmetic array parsing, intentionally overlapping PE-correlated rows, independent PDB-start fault, valid alternate displacement, flow-injection location and exact real-vector/data counts. It adds terminal missing/duplicate/unknown/PE mismatch cases. Those original refusals remain retained.

Main’s qualified source set is the scanner plus its two build-process test files. The documented Main correction scope is limited to these scanner-control and fixture sources; the final receipt binds their exact bytes. It does not attest a full Application runtime or a broader Main source change.

## Identity and qualification anchors

- Original Release image SHA-256: `F27322DBF26A4C04D38CF32F7C02BA4B363F2BA598DF27AACDE2307511C194A8`; PDB SHA-256: `458727AC51549E66DA2D513993C4813E66847EA4CA3CF3D4D67FA3D84A2B6405`.
- Final original-image scan: 1,323,471 instructions; 488 vector instructions; 0 EVEX instructions; 14 undecoded prefixes; 19 inline-table vector-data rows; 0 violations. Original-scan report SHA-256: `A9395B861CC98B1000825A1DB4F160D1383E8FB293E0E172A1E3456F59173CDC`.
- Final default output SHA-256: `426DC153AB794FD228ED5EA2E0388682806CEA7ED0B60EEC9DD4AD9ADBF44406`. Final default receipt SHA-256: `BA72317B44721F03ECD7E2EE56CC0C7DBB951F6348F486A298C0CC6A2BB81215`.
- Runtime allowlist SHA-256 is unchanged at `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`.
- Main-qualified source hashes: `Build/IsaScan.cs` `9B6DE9618D86595472AD70E17BF68B952B6353F15DBE902FC9E68000E663D054`; `Tests/BuildProcessTests/IsaAlNearRcxTests.ps1` `95B3C7D7013D502117AB4746F8A1520C40F18780C4239E6D9842307B84B451AD`; `Tests/BuildProcessTests/IsaScanTests.ps1` `BF01367E81FBE8FD9ABA8A2750DF9D2BE219B9EF39AE0B02BE531341E8175C5B`.

## Preserved failures and limits

Failed attempts remain in `D:\RedPrismBuildCaches\.build\p1-isa-al-near-rcx-r1\`: the initial strict scanner withheld proof for DWORD table bytes decoded as `vcvtne2ps2bf16` (`original-recheck.error.txt`); the first fixture run failed because `BodyVex` expected 21 inline-table rows but got 0 (`new-controls-r1.error.txt`); the predecessor launcher run exited 1 (`predecessor.result.json`). The passing r2 controls and final default launcher receipts are retained beside those failures. The original-image correction is a scanner qualification only. No ARM64 runtime, ordinary activation, full Application run, or P1 completion is claimed; the qualification receipt sets `native_Application_replayed: false` and `P1_done: false`.

## Sources consulted

- `D:\RedPrismBuildCaches\.build\p1-isa-al-near-rcx-r1\main-final-scanner-qualified.json`, `final-default.result.json`, `final-default-freeze.json`, `new-controls-r2.result.json`, `predecessor-r2.result.json`, and preserved original/recheck/control receipts.
- `D:\RedPrismBuildCaches\.build\p1-isa-al-near-rcx-r1\SourcesFinal\Build\IsaScan.cs` and `SourcesFinal\Tests\BuildProcessTests\{IsaAlNearRcxTests.ps1,IsaScanTests.ps1}`.
- `Specs/Build/Build_Toolchain.md` in the recorded implementation worktree (current ISA scanner qualification rules).

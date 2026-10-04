# Guarded message switch with a hoisted R9 image base

## Checklist

- [x] Preserve and independently verify the failed native metadata r5 archive.
- [x] Confirm the flagged Release bytes are an anonymous DWORD RVA table.
- [x] Prepare a private encoding, bounds, target and reaching-definition proof.
- [x] Review the delegated fixtures and repair their independent PE construction.
- [x] Qualify 125 new controls (11 accepted / 114 refused), retaining all 12 existing parser suites.
- [x] Reparse the unchanged failed image and four preceding images with the unchanged allowlist.
- [x] Verify the completed private proof archive before native activation.
- [x] Complete the native metadata runtime/build matrix and update its checklist.
- [ ] Commit and push the qualified step.

## Current evidence and proof

The native metadata r5 Release image links successfully, then the ISA gate reports `vcvtsi2ss` at
`0x1402b155c` in `DxUi.WindowHost.obj`. This is the second DWORD in a 15-entry switch table at
`[0x1402b1558, 0x1402b1594)`. Its bytes `c5 fe 2a 00` encode the RVA of a decoded case target.
The image hash is `BE6588AF4F4F352DA9C60C90F66501CD55BEB3090138EF7D0EF95B00EF8AFDAA`;
the PDB hash is `865CB1E5625848E478FB10375973C7C7F4362D291652E7C4799666C5A7B626EE`.
Release runtime validation has not run in r5.

The private recognizer requires the exact `LEA EAX,[R15-257]`, unsigned `CMP/JA`, R9-indexed DWORD
load, `ADD RCX,R9`, and `JMP RCX`. Every executable row must match the PE and a nonoverlapping
decoded boundary. Anonymous same-owner PDB symbols and the immediate upper bound determine the
complete table extent. All table targets and the fallback must be decoded body boundaries outside
the guarded dispatch. No object, procedure name, address or allowlist exemption is added.

A bounded reverse control-flow proof checks every admitted message value. It distinguishes an
impossible path from an unsafe register definition. It accepts only intact RIP-relative image-base
definitions reached through explicitly understood register-preserving rows. Companion direct and
byte-remapped switches require their own guards, bounds and target sets; a bypass into a dispatch,
unknown indirect jump, register clobber, call on a required path, unsupported flags or a cycle cannot
provide the base proof. Unreachable terminal alignment is excluded from code only after reachability
checks. Table data need not decode as instructions; reported exempt vector bytes must still match the PE.

This is parser work required by the native qualification gate. It supplies no UI latency, RAW,
SIMD kernel throughput or GPU performance claim.

## Qualification

Private r1 stops at an existing RDI synthetic control whose reported table bytes differ from its PE after
changing a backward target. The new byte-correspondence rule is scoped to the new hoisted-table proof;
existing table recognizers and their controls retain their established behavior. No previous control is
removed or rebaselined. The complete failure archive is verified before this correction: 19 inputs / 11,556
evidence files, completion `E9B6B7CF08A455A33AE627ED350947A40AB63698A4B493BB01C5CBE2218EBFDD`.

Private r2 passes every exact predecessor control name and the 125 new controls. The new matrix includes
companion R8 dispatch bounds/base/target/bypass mutations, numeric branch pruning, unknown flags,
R9/R15 clobbers, calls, every guard instruction's PE/listing/boundary, symbols/ownership/bounds/targets,
and executable VEX instructions before/after the table. Opaque data may have missing, unknown or
overlapping linear decoder rows; its extent comes from the guard, PDB and PE. A reported exempt vector
row with bytes differing from the PE remains a violation.

Actual process exit is zero at `2026-10-03T22:47:07.5823547+00:00`; archival completes at
`2026-10-03T22:49:26.0026295+00:00`. The private archive is
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\isa-r9-hoisted-proof-r2-qualified`,
19 inputs / 25,169 evidence files. All 25,188 source/evidence copies and both manifests are independently
verified before activating the exact scanner/test source and registering the native test gate.

| Receipt | SHA-256 |
| --- | --- |
| Archive completion | `6E39430B4E3530C3066C715E8C9EBAA1B57A6DED929528F893A3178D06D6581E` |
| Scanner | `2B45C9D3CE93EDC0E1E709653ECC6C9D0016A1460823098ECBC810B1FE67EC84` |
| Source manifest | `FC86F96318FA8C958D9B30B4C803683A87A67D595D0130B1C4F819C5D43E8472` |
| Evidence manifest | `31B9B49DDC4EB2844A5EA15E2D867CDE5403F63EA304D1DC80CEF0913BCA04EA` |

The failing image now has 910,179 decoded rows, 459 executable vector instructions (all previously
approved runtime), 45 proved vector-looking table rows and zero violations. Four unchanged prior images
retain their exact decoded counts and table counts (27, 8, 14 and 54). The allowlist remains
`6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

Native r6 passes fresh full Debug and Release suites and retains exact-input ARM64 builds from r5.
ASan reaches the application suite and fails two existing transform-preview fixture retirement assertions;
its complete failure archive is independently verified before repairing the missing harness acknowledgement.
The r7 matrix freshly rebuilds all six profiles; all actual processes pass. Its collector initially miscounts
ASan's extra string checks from the second HostTests stress run. The verified r8 collector retains every exact
compiler/test input, product, log and actual process receipt, qualifies all six profiles, and preserves every
old assertion. Completion `0FD3F4C7E93D7104C93A42C879889999D9F8462F7A60768946E45F01D42025AB`
([qualification, failure and repair](DocumentRecordRoots_2026-10-03.md)).

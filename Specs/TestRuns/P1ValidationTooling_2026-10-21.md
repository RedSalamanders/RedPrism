# P1 validation tooling: fixture reuse and native ARM64 evidence

Status: adopted affected controls qualified; publication pending. P1 remains WIP.

## Checklist

- [x] Main independently reviews the extraction, entrypoint, helper and regression controls.
- [x] Adopt 24 scanner-fixture paths and the three coherent native-helper/entrypoint/test paths.
- [x] Run the complete affected ISA controls in the authoritative checkout with real compiled probes.
- [x] Run all 30 native diagnostic controls and all three exact typed-refusal children against adopted bytes.
- [x] Preserve failed native runs, private counterexamples, source identity and raw output.
- [x] Canonical formatting checks 1,354 files; all 15 repository skills validate.
- [ ] Commit/push and verify destination identity.
- [ ] Request and independently qualify the next native ARM64 Release run; native Debug/ASan remain separate.

## Scanner extraction

The complete assertion suite now imports helpers without reexecuting the baseline in each consumer.
All 21 helper function bodies, 13 setup statements and 181 original non-function baseline statements
are preserved, with 21 consumer imports/AST readers retargeted. Nested helpers retain their parent
scope and successful imports retain the prior exit status. The canonical harness invokes the full
baseline once, followed by every original family and compiled control.

Main's exact assertion multiset check compares the earlier 14,122 parser occurrences with the new
4,350 plus fourteen identical 698-record baseline repetitions. Every one of the 4,305 distinct PASS
records remains; missing or unexpected records are zero. The new full run also preserves all 15
compiled named controls and six actual compiled probes, including the wrong ISA/library/object/name,
missing or mismatched PDB, malformed contribution ordering and exact accepted identities.

No production scanner decoder, allowlist, project, compiler setting, instruction policy or golden
changes. Private unpaired elapsed observations are diagnostic only; no benchmark speedup is claimed.

## Native ARM64 evidence and failure boundary

The entrypoint defaults to 1,200 seconds. Optional 1,200–5,400-second values above the default require
native ARM64 and are refused before build elsewhere. The hosted Release helper opts into 5,400 seconds
to observe slow execution or a stall with independent stdout/stderr and owned-process CPU/memory
progress. The previous 1,200-second failure remains failed; no historical cause or performance fix
follows from the longer observation window.

Actual parent exit and terminal progress are written before source/environment/product closeout and
before raw-pipe drain. Missing products, nonzero exit or drain refusal still fail. Optional diagnostics
record bounded errors and unavailable states without stopping canonical work. Workflow job/parent
limits, single Release configuration, branch/path/commit guards, action pins, permissions and no-kill
policy are unchanged. This mixed coherent tooling commit has no native-validation marker; a later
explicit CI-only marked commit may request a new run under the existing workflow.

The 30 pure-PowerShell controls use independent literal progress fixtures and executable AST ordering.
Three isolated copies of exact entrypoint bytes refuse 1,199, 5,401 and x64 5,400 seconds with exact
exception/parameter/message checks. A build sentinel must remain untouched. Main's retained negative
counterexamples prove a misleading comment cannot conceal reordered closing evidence and unrelated
error text cannot satisfy native-only refusal. Controls execute no Application or native CI helper.

## Adopted-source actual results

| Scope | Actual parent PID / exit | Verified facts | Raw stdout SHA256 |
| --- | --- | --- | --- |
| Complete ISA controls, ASan fixture configuration | 84176 / 0 | 4,350 parser records, all 15 named compiled controls | `C8F1D6D623ADF6AFED47B20FA89AF936ACB0AC1CF49C2195340E2B96EC8B587E` |
| Native-wrapper pure-PowerShell controls | 34728 / 0 | 30 checks, three typed-refusal child exits 1 | `22C6A007E06337D08820C32CFC36988E1003B5B819654B15B053CFE5250CE1B6` |

Both stderr streams are empty. All 27 adopted source inputs remain byte-identical across the affected
runs. The unchanged current Directory.Build.props/targets govern the real C: fixture builds; no stale
primary Z: checkout imports are used. The qualified product C++ source is unchanged from `a2f5626`.
Its seven-profile local matrix remains the preceding product qualification, not a rerun of this tooling.

Evidence: `Z:\src\RedPrism\.build\P1Evidence\p1-adopted-tooling-main-r1`.
Extraction/private counterexamples: `P1Drafts/IsaFixtureLibrary-Main-r1` and
`P1Drafts/NativeArmCloseout-Main-r10/NegativeControls-r1`.

The final diff check first refuses CR line endings on two touched legacy PowerShell files. The
failed check is retained. Main normalizes only the native helper and `IsaR9DirectTests.ps1`, including
the latter's pre-existing final CRCRLF. Every non-newline token, literal and comment remains identical
under line-ending normalization. All 30 native controls and three typed children pass again against
the normalized helper; all 64 R9 direct controls pass again. The final diff check passes. The initial
27-file bookends and these separately recorded normalization checks retain their distinct identities.
No Git policy or assertion is changed.

Native ARM64 runtime, ordinary prepared consumers, measured G1 and full P1 delivery remain open.

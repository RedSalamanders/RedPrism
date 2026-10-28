# Native build policy, terminal results and short DxUi paths

Status: ACTIVE
Date: 2026-10-28
Owner: build maintainers

The user's agreed policy is /Wall with /WX disabled and zero warnings accepted;
Debug uses EditAndContinue, ASan Debug uses ProgramDatabase. Release enables
explicit COMDAT folding (/OPT:ICF); Debug and ASan Debug disable it.
The owning build contract is [the toolchain contract](../../Build/Build_Toolchain.md).
Five repositories implement the same policy without implicitly advancing DxUi pins.

## Checklist

- [x] Preserve the pre-change effective-profile audit and isolate changes from concurrent tooling work.
- [x] Restore RedTilde ARM64 through its normal entrypoint and pass the required policy/lease checks.
- [ ] Enforce and negatively test the common compiler/debug/linker policy in every supported native profile and pinned archive.
- [x] Unify green zero-warning build results, diagnostic/log links and opt-in terminal executable hyperlinks, preserving plain text and existing build/run semantics.
- [x] Shorten source-commit and output-fingerprint folders to 16 hex characters with full identity validation and collision refusal; retain existing data without cleanup.
- [x] Update source discovery, direct helper builds, provenance, test fixtures and dependency identities together.
- [x] Publish zero-warning guidance and normative build/path/terminal contracts, with meaningful tooling tests.
- [ ] Execute affected tests, evaluated profiles, required native builds and applicable runtime qualification; retain every failed result and distinguish cross-builds from runtime tests.
- [ ] Fold/check the changelog; validate formatting/specs/tooling and close this plan only after all required gates pass.

## Decisions and limits

Source pins and full SHA256 identities remain authoritative; folder prefixes are
locators. No source clone is reset for a prefix collision. Existing historical
output is retained in place. Build success is green only after successful
native exit, zero diagnostics and required attestation. Hyperlinks never launch
an application automatically and fall back to quoted launch commands.
The existing explicit RedSalamander Embedded compiler-debug recovery mode remains
an attested exception; ordinary Debug/ASan profiles use the shared defaults.

## Evidence

Baseline audit: E:/DxUi.Perf/flag-coherence-20261028-aea0f188.
Current task evidence: E:/DxUi.Perf/build-policy-evidence-20261028.
RedTilde ARM64 restore and required policy/lease logs are under that task's
RedTilde-ARM64-restore folder; both exited zero before implementation.

Candidate pin: `a059e07c39c58cbf5218c34f7b9f1a9ac29baf3b` (API3). Six upstream native builds and all 54 nonforeground suite runs pass. The declined focus countdown remains an open qualification gate; this candidate lock enables independent product builds and is not a final green receipt.

The user subsequently authorized the 30-second automatic start and resumed focus qualification. All three upstream
focus configurations now pass with restoration; the earlier declined run remains retained as historical evidence.

The product ARM64 Debug build exposed MSVC C5291 on private `Raster::ChargedStorage`. The interrupted run is not a
pass. An ARM64-only 8-byte base tail member addresses the compiler's recommendation without suppressing C5291.
An independent four-way header compile reproduces the warning before the change only on ARM64, and compiles the
new header with zero warnings on ARM64 and x64 using x64-hosted MSVC 14.52.37025. Its immutable receipt is under
`E:/RedPrism.Perf/build-policy-arm64-layout-proof-20261028/`. A static assertion checks the charged object-size
increment, and the crop lifetime test checks retained pixels after source/caller release. Full native builds and
runtime qualification on the final product source remain required; no native ARM64 runtime execution is claimed.

The subsequent fresh ARM64 Debug rebuild found intentional page-touch/capacity C4746 observations and later
C2471/C1907 compiler PDB write errors. This failed/interrupted result is preserved, with no attributed cause.
Definition-local ARM64 annotations retain page-touch semantics and shared test capacity observations. A second
four-way compiler control reproduces the original ARM64 notices and compiles the scoped version with zero
warnings on ARM64/x64 (`E:/RedPrism.Perf/build-policy-arm64-volatile-proof-20261028/`). The normal build entrypoint
will clean/rebuild ARM64 Debug with bounded parallelism before the remaining profiles and Full runtime gates.

That clean rebuild compiles the repaired owned observations without their former warnings, but exposes six SDK
WRL volatile notices in the UI Automation notification fixture. A real RuntimeClass/FtmBase compiler control
reproduces all six only on ARM64; parsing the WRL header under a definition-local SDK include annotation removes
them while both x64 controls stay warning-free. The fixture parses that annotated SDK include before application
headers can include it. Evidence: `E:/RedPrism.Perf/build-policy-arm64-wrl-scope-proof-20261028-r2/`.
The first proof attempt is retained: its fixture omitted the original test's noncopyable-trait annotations,
so unrelated copy/move notices correctly refused its zero-warning gate. No result from that attempt is accepted.

Candidate22b8 passed five native profiles; ARM64 ASan Debug compiled/linked but correctly failed the zero-warning
gate on two uses of the shared deliberate use-after-free probe. Its function now has an ARM64 sanitizer-only
definition-local C4746 annotation, with the faulting body unchanged. Four real sanitizer compiler controls reproduce
the old notice only on ARM64 and accept the fixed header warning-free on ARM64/x64. Receipt:
`E:/RedPrism.Perf/build-policy-sanitizer-probe-scope-proof-20261028/`. Real x64 sanitizer child diagnostics and the
final six-build/three-Full matrix remain required; no runtime result follows from compile-only controls.

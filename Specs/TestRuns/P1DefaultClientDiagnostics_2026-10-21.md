# P1 native default-client failure and diagnostics

Status: failed r5 identity verified; MSVC 14.52 repaired Release, ASan, analysis and ARM cross-builds qualified.
Post-default full Debug/Release/real ASan and all three ARM cross profiles qualify.
P1 and native ARM64 runtime qualification remain open.

## Preserved native failure

[Run 39541110772, attempt 1](https://github.com/RedSalamanders/RedPrism/actions/runs/39541110772),
job 119028817034, built `c805d5d638eda67858a4d39f838f3656a5e78298` on native ARM64 Windows
build 26200, image `win11-vs2026-arm64` / `20261013.183.4`, Cobalt 100, two logical processors.
The selected MSBuild and compiler were ARM64-hosted VS 2026 / MSVC 14.51.36231.

Main independently verified all 2,084 exact commit-tree source bytes after declared Git filters, unchanged
source bookends, matching environment/toolchain identities, all 36 downloaded artifact sizes/hashes, and
five AA64 executable/full-PDB GUID-age pairs. The complete suite rosters match the retained predecessor,
with the named x64/ARM64 Host substitutions and the two unavailable ARM64 debug-layer facts accounted for:

| Suite | Native PASS facts |
| --- | ---: |
| Host | 12,548 |
| Document | 24,003 |
| Composition | 8,426 |
| I/O | 1,953 |

All four suite footers passed. Observed Application stdout contains 8,079 PASS facts, including 14 pending-raster
joined-shutdown refunds; Polygonal dispatch and press summaries report 729/0 and 1,002/0. Composition retains
all 41 recorded-parent cancellation controls. These are observed subsets, not complete Application qualification.

Canonical PID 9936 exited 1. Hidden WARP Application PID 3676 exited 2 with exactly
`The default client area does not match the current monitor DPI.` The last stdout BEGIN names
`RunJobShutdownSelfTest`; source tracing locates the actual later failure in `Application::RunSelfTest`
after normal hidden-window creation. No product process remained at terminal observation.

The original combined assertion checks window DPI, two style flags, successful GetClientRect and scaled
1280x800 DIP client dimensions. r5 logs none of their individual values. The precise predicate and cause are
unproven. `CreateMainWindow` already recalculates and applies the actual window DPI before showing it;
`OnGetMinMaxInfo` preserves OS maximum-track limits. No second-resize repair or OS-clamp explanation follows
from this failure alone. This run precedes the loaded-selection component and cannot qualify its native runtime.

## Exact evidence

```text
Z:\src\RedPrism\.build\P1Evidence\p1-native-arm-hosted-r5
  download-manifest.json
  main-failed-identity-and-rosters-r5.json
  MainPdbSummaries-r5\
Z:\src\RedPrism\.build\P1Drafts\verify-observed-arm-failure-main-r5.py
```

Download manifest SHA-256: `FC2398C24370D89E30B2D36FEF648B8C37E4255D9BF755B3888CFD8735902951`.
Fresh failure-reader SHA-256: `E805956C5AE89CC4587EA57242794E2FF7E6E2F3C581E7CC2FDDDA1A60AE5C6A`.
The reader loads neutral identity functions from the retained reviewed helper; it never executes a success-only
main. The report binds that helper's hash, source, actual exits, products, rosters and qualification limits.
Original artifacts and failed receipts remain unchanged. No native job was dispatched or retried to verify them.

## Diagnostic candidate

Before the original default-client predicate, the hidden self-test now records window/system DPI, both styles,
GetClientRect/GetWindowRect success and immediate errors, actual/expected client size, requested default outer
size and its HRESULT, monitor/work rectangles, and DPI-aware OS maximum-track dimensions. All original
predicates and failure exit remain in place. No ordinary sizing, DPI, minimum/maximum or rendering behavior
changes. The UI windowing contract records this diagnostic requirement.

The first canonical local Release attempt `dpi1r1` exits 1 during compilation under the currently discovered
MSVC 14.52.37025 toolset, before runtime. Its exact 1,903 source inputs, logs and available products are retained
under `P1Evidence/p1-default-client-diagnostics-r1`; no diagnostic runtime result follows from this attempt.
MSVC identifies inline `Raster::Storage` constructor cleanup instantiating `vector<RasterTile>` destruction
before `RasterTile` is complete. `ChargedStorage` has the same header ordering. Both constructor definitions
are moved inline after the complete `RasterTile` definition, preserving their bodies, layout, inlining and
ownership. The existing first-header consumers that failed remain in the canonical build; no include-order
workaround or diagnostic suppression is added.

The next Release attempt `dpi1r2` clears those incomplete-type errors but exits 1 before runtime on two
warnings treated as errors. Its 3,241-file archive has manifest SHA-256
`20E1818CE64A04D9FA76787CC7AD66795D1564CB7150FC06D8427E2273834934`.
`RecordRootBench.cpp` places `[[nodiscard]]` after `__declspec(noinline)`, which the current compiler diagnoses
as ignored (C5240). The attribute moves before the declspec, retaining both intended properties.
The inlined thumbnail-height diagnostic is C4723 in the concurrency reference. The test now uses its unchanged
asserted 23x15 dimensions as the literal loop bounds and serial-reference divisors, retaining the dimension/null
guard and checking all the same pixels. No warning suppression, reference rebaseline or product pixel change is made.

## Current repaired-source qualification

All runs below bind the same 1,903 frozen inputs, source SHA-256
`570A605D34C4013DCE4138BD8F6EA209B9E36E59BAB9766288F2ACA2EADD80AE`, at base `4fffd37`.
The compiler is MSVC 14.52.37025. Main independently verified each successful profile's 1,022 actual compile
records, linked-object set, exact archive bytes, and five executable/full-PDB GUID-age pairs. x64 records include ISA checks.

| Attempt | Canonical result | Independent readback |
| --- | --- | --- |
| dpi1r3 / x64 Release | Pass | All four suite rosters, 155 application stages / 12,301 assertions, 102 byte-identical loaded-selection stderr facts, both cancellation routes and nine fresh harness captures |
| dpi1d1 / x64 Debug | Exit 1 | All native suites and application pass; direct cancellation smoke refuses ambient keyboard state |
| dpi1d2 / x64 Debug | Exit 1 | All native suites, application and direct smoke pass; prepared cancellation smoke refuses ambient keyboard state |
| dpi1a1 / x64 ASan Debug | Pass | Complete predecessor rosters/application, actual sanitizer detection and stress/crash probes, both cancellation routes, nine fresh captures |
| dpi1an1 / Release analysis | Pass | All 1,022 analyzed compile records, real concurrency plugin, fresh positive/negative controls with the negative diagnostic at its intended unsafe access |
| dpi1armd1 / ARM64 Debug | Build pass | Profile, linked objects and five AA64/full-PDB pairs; no runtime claim |
| dpi1armr1 / ARM64 Release | Build pass | Profile, linked objects and five AA64/full-PDB pairs; no runtime claim |
| dpi1arma1 / ARM64 ASan Debug | Build pass | Actual sanitizer compile/link flags, profile, linked objects and five AA64/full-PDB pairs; no runtime claim |

The unchanged Debug direct retry and the later prepared retry both pass. The prepared retry follows the human's
announced one-minute released-key window and records 30 successful trials. Those focused receipts do not convert
either failed canonical Debug attempt to a pass. The original guards, failed receipts and complete logs remain retained.

The current x64 diagnostic reports 144 DPI with expected/actual client 1920x1200, both required styles,
successful rectangle/monitor queries and zero errors. This is diagnostic coverage on x64, not the missing native
ARM64 predicate or its cause. The older native run uses 14.51.36231 and cannot qualify the 14.52 environment.

Archive manifest SHA-256 values include:

```text
dpi1r3     131EE7E24322754B446E54A7FEE81356A93A9FE8297A55A3E8212646E6692B28
dpi1d1     DA7A1D646DD1611BA678CD4D27EBA1AA8CFB4546253BBB49675E439EDF03DAA2
dpi1d2     9F234C45FD3275FFAD920912DBEC2D2D00485F8941D71716EDA0874B8151903B
dpi1a1     1914D8E423FCD8BF5E22B99B1CE39FF01D578996E42B11F96AA0605F2B368DD2
dpi1an1    24716B8DEE95DC527EAE5C85B986594202E42B21AD0D7228B32AC33126CDCE09
dpi1armd1  A0522089B0A41E7E87D50A19BAA2E00AA33B1623DCC9EAF9C6ED96024635FBA2
dpi1armr1  23613CDCD2F39EA65DADDF846675D67D0588451CBF53EB357051161881F29E57
dpi1arma1  DB09F6085EB1F9EA05CE18383F1FD003BDE52960E49C1001BDAB243C70614887
```

The repeated LLVM inspection of Release products finds frontend/backend 19.52.37025.0 in every one of the
1,022 project objects and all 23 actual members of the selected pinned DxUi library. The actual compiler,
linker, library and command/read tracking inputs are retained under `ToolchainInputs/dpi1r3`.
These direct product facts supplement the profile records rather than infer the compiler from project XML.

## Compiler-host stabilization

The ARM64 cross-builds above use x86-hosted 14.52 compiler/linker tools despite amd64 MSBuild. Their lastbuildstate,
main PDB build-info, dependency identity and read logs agree. Release records the x86 linker's memory-map failure
and automatic restart with the x64-hosted ARM64 linker. The final build passes, but this is not a stable x64-host choice.

`build.ps1` now scopes an unset X64-process `PreferredToolArchitecture` default to `x64` across dependency
identity/restore, solution invocation, validation and provenance. Explicit values, native ARM64's `arm64`, discovery
precedence and the caller's environment remain intact. Ten exact-entrypoint stand-in scenarios plus discovery
precedence pass. They cover unset/explicit/empty inputs, dependency/restore/link/provenance failures and Clean;
no compiler or native runtime success is inferred from the stand-ins. The original entrypoint counterfactual
leaves all four observed boundaries unset. On this PowerShell runtime a bare `$null` argument to the .NET string
setter becomes an explicit empty value; fixture removal and null-string restoration preserve true absence.

### Post-default qualification

The post-default batch binds 1,904 frozen inputs at base `4fffd37`, source SHA-256
`E81E1645C5130105EBECC8AD2FD43FB00C99C584312BE52F2B1C5DCBCF3660BF`, working delta
`3138C9B587CF86B35B2B429B7D9204CBBEBC675AF377939686B8E26467F6F529`.
The existing canonical runner, archiver and profile/runtime readers are reused.

| Attempt | Result and independent scope |
| --- | --- |
| host1d1 / x64 Debug | Full pass; 1,022 compile records, five PE/full-PDB pairs, all four predecessor suite rosters, 155 application stages / 12,301 facts, unchanged stderr facts, both cancellation routes and nine captures |
| host1r1 / x64 Release | Full pass with the same profile, predecessor/runtime and capture checks |
| host1a1 / x64 ASan Debug | Full pass; 1,022 compile records, five PE/full-PDB pairs, all predecessor rosters, 155 application stages / 12,301 facts, actual sanitizer detection/stress/crash probes, both cancellation routes and nine captures |
| host1armd1 / ARM64 Debug | Cross-build/profile/link/object/five AA64-full-PDB pass; no native runtime claim |
| host1armr1 / ARM64 Release | Cross-build/profile pass; actual 1,022 project compiler records and 23 selected dependency members also verified below |
| host1arma1 / ARM64 ASan Debug | Cross-build/profile and real sanitizer flags/link pass; no native runtime claim |

Archive manifest SHA-256 values:

```text
host1d1     95B0EB28E4192CEBE0FC8FA9BE8177D01DFC10CF4B8E4CADA546CC10AC4CDC66
host1r1     A75FE3468C0E2364C786387B1AA72FBD07711A4F1F8C5B6D1D8FFD07846B986A
host1a1     2664824506C6710EE09BE17F3FDD5ABB3B57F51D36E9E07B6A1E83CDBBE7D93A
host1armd1  E70771A793669F4C35A7F8FCBEF4A30D80C6791237E3B49BB29899209DB0EB2D
host1armr1  C4C20CBC3B683F17EC7004A7CE54584C7AC9C19B3814F654329D57509EDF8ABE
host1arma1  167BCBBE005A471E831C3ABD7319FD69C043D28B030BBF2D208560A2DFEB1598
```

The ARM Release readback finds actual frontend/backend `19.52.37025.0` in all 1,022 project objects and all
23 selected DxUi archive members. Each selected project tool-host state is `Native64Bit`; current CL reads
include `HostX64/arm64`. Aggregate tracking logs can retain earlier x86 inputs, so their mere presence does
not prove a fresh old-host compilation. The selected matching dependency fingerprint is
`f9d91866008b58a3a9f957f8ed0635cb28977944086cbcdea66617f7efdc79a9` and its library hash is
`5AA09299B8A68FC3D0A505722831D0D217A3752CF1037DC5BA60A268130AF89A`.
Retained tools, dependency and tracking records live under `ToolchainInputs/host1armr1`, manifest
`C9351BB74A2AC1C2EA4B7FE1EBBAAC08643164D80AECC6A4E4EEF5E1A569EA83`.
The initial readback's CLUI locale-directory assumption refused before confirmation; unchanged original
records show the actual `1033` subdirectory and the corrected readback passes.

### Renewed native preflight

The existing native workflow follows the selected installation's default toolset. Its helper now requires
`14.52.37025`, writes required/actual selection to `toolset-requirement.json`, and refuses a mismatch before
canonical work or discovery-environment mutation. The hosted image must provide that native default.
Exact-function installation/discovery stand-ins in the existing `NativeArmValidationTests.ps1` exercise the
boundary; they neither execute ARM64 tools nor qualify real compiler products or native runtime.
Main corrected two draft-only control issues before execution: absent environment values need `NullString`
restoration, and the real wrapper has two opening/closing environment calls. The unchanged existing fixture
then runs all 60 controls, including four exact initializer cases and three invalid-parameter child receipts;
actual child PID 71876 exits 0. Main independently reads the complete logs, all 60 unique checks, seven case
receipts, retained input hashes and unchanged source bookends. Evidence is retained under
`native-toolset-controls-r1` in the same diagnostics packet. The copied MSBuild metadata and compiler bytes
are stand-ins; no native tool or canonical application executes. The runtime source freeze is closed;
integrated-script controls and publication are recorded below.

### Same-attempt ASan archive recovery

Canonical ASan PID 79368 exits 0 at 11:02:02 UTC. Its archiver later fails with `OSError: [Errno 28] No space left
on device`; the controller exit 1 is an archive/storage failure. The partial archive is retained as
`Archives/host1a1-nospace-partial`, with `host1a1-archive-storage-failure.json`.

The 65,176,337,090-byte diagnostics packet is copied to
`S:\CodexEvidence\RedPrism\P1_2026-10-21\p1-default-client-diagnostics-r1`.
Every one of 86,300 files matches SHA-256, with unchanged source/destination bookends. The complete relative file
and directory sets, metadata and 32 hash sentinels are checked through the original Z: path's junction.
The old Z: duplicate is retained at the explicit sibling `p1-default-client-diagnostics-r1-relocation-source`:
automatic approval review rejected its deletion as blocked by policy; deletion is not retried.
Recovery logs, scripts and the per-file hash manifest are retained under `StorageRecovery`; the manifest SHA-256
is `3EDA979482C3533BB6D5ECD3D0F515EBAE93AA72CDF55ABA07EB2970D92CBE1F`.

Unchanged product/source bytes are archived with the existing archiver and independently pass the existing
profile and runtime readers. No native test is rerun or substituted. The final same-attempt archive and its
manifest are the `host1a1` entry above. Original failures and partial bytes remain evidence.

Subsequent native observation is still due. This diagnostic candidate does not repair or waive the original
native failure, qualify G1 or close P1.

## Integration and publication

The six reviewed native-guard/contract/evidence files are copied only after their original worktree hashes
match and the native runtime freeze closes. The unchanged regression fixture then executes from the actual
P1 worktree: PID 86524 exits 0, all 60 unique controls and seven case receipts pass, all four input bookends
match, and stdout is byte-identical to the private run. Main verifies the complete logs, original check-name
set and current source hashes. Its report is `native-toolset-controls-integrated-r1/main-qualified.json`;
the control report SHA-256 is `336A55AE4C0CAE23202A98ADAA975A00F9DE7C8D6FBEC3D160241096ECC81CE6`.
The first readback refuses a PowerShell collection-count expression; corrected counting verifies the same
unchanged successful run. The refusal is retained and no controls or native products are rerun to replace it.

Final closeout documentation and native CI guard/control changes are separate from the 1,904-input native
freeze above. All other frozen inputs, including every C++ and build input, remain unchanged. The preceding
1,903-input Release analysis remains its exact historical scope; no post-default analysis run is claimed.
Code, regression controls, contracts, checklist and this evidence publish in one coherent change. Renewed native
workflow observation and the final integrated P1 matrix remain due. No preceding-commit-SHA-only update is required.

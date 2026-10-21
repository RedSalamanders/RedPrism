# P1 admitted committed display integration

Workspace: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`.
Branch: `codex/p1-opacity-host-proof`; committed predecessor `0773548`.
DxUi remains pinned to `8e8994079e82f79e6765e6a885c03fc687957591`, API 3.

## Checklist

- [x] Integrate the immutable recorded graph, original admitted parent/controller and opt-in application renderer route.
- [x] Preserve separate experimental WARP/hardware successes and every failed integrated attempt below.
- [ ] Pass the actual integrated application display/shutdown cases and complete current-source validation matrix.
- [ ] Qualify refusal, cancellation, supersession, hidden-state retirement and complete peak bounds.
- [ ] Migrate pending edits, stroke/utility consumers and rows before normal activation; measure G1.

P1 remains WIP. The committed display route is opt-in in the actual hidden application test. Normal startup
still uses the existing path. Experimental results and partial failed-run results are not complete qualification.

## Integrated Debug r334: build failure

The full canonical test command failed while compiling the new application fixture. Four calls incorrectly
named `Application::SettleJobs`; the actual helper is the shared free function. The correction calls
`SettleJobs(owner->_jobs, 15000, DispatchMessageThunk, owner.get())` and preserves all expectations.
No application runtime test was reached.

Actual owned child PID 102496, `2026-10-07T20:37:38.555453Z`–`20:38:36.304190Z`, exit 1.
963 inputs, freeze `B857FBB0B1BD9431DEFD1930AC5C45669E3B189A6BE693CE027D5A3CED2B2201`.

| Evidence | SHA-256 |
| --- | --- |
| Stdout | `D9F3B36E54A8CEFD31CEB9CB27C33CF8D5EEA8E01E06C02996DE8C594EBBEBBA` |
| Stderr | `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7` |
| Source manifest | `029F86189A32E068833076C8A804DBA6A65FFBB95091077341D12198DA85EBB4` |
| Evidence manifest | `9F831D4DB8B470EFD3D58522D8CEC39C6E498B114BB8AD6E49E8CBD234966662` |
| Archive completion | `D1E019B1E973B578F3502DF0021A6A503897D0ACD365A40611B50FA24426B413` |

Archive `D:\RedPrismQualificationArchives\P1-display-963-Debug334-build-failed-2026-10-07`:
1,262 copies verified at `2026-10-07T20:39:26.7668423Z`; zero application stages.

## Integrated Debug r335: shutdown fixture assertion

Actual child PID 89384, `2026-10-07T20:42:26.807508Z`–`20:49:59.338471Z`, exit 1.
963 inputs, freeze `5B940A1D551F0078A499F7B473608D5F334C179F2EB5BE305C5A1B6A24AB2DF4`.
The integrated recorded-blit parent passes on WARP and hardware, with 75 actual original GPU acknowledgements
per device. The admitted controller also passes both devices. The new empty-recorder shutdown test then
calls owner-only `JobScheduler::ReservedMemoryBytes` from its final MTA hook and hits the owner assertion
in `Jobs.cpp`. Application stages were not reached; this is no full Debug pass.

The correction removes that invalid worker read. The owner hook observes the actual complete original charge
after producer join. The worker observes that acknowledged hook ordering, retires the stream and captured
context in its MTA, and the owner checks zero actual charge only after Close returns. The test names now
describe those observation points. The scheduler releases the budget after the final worker returns;
production ownership, lifecycle ordering, deadlines and legacy goldens are unchanged.

| Evidence | SHA-256 |
| --- | --- |
| Stdout | `55E86183FE9F23F8558BA8D60B7EEB58D52C16249010B8AA1BB27A2B1F1A9036` |
| Stderr | `AE7D06C55841A048503F644C642A89C83D2EEE24505342A38209625B60B99EB3` |
| Source manifest | `A96F22E410AD62E671AA6A83F97356CBFCCBBBA5D6FC82F3C4F11122F001A9D7` |
| Evidence manifest | `A81B2B233815EE3F170E041181C9298599AD9BE783671E6DAEB95E5C48DB492D` |
| Archive completion | `8D1D908CFED90873F009D51D329524BB4AF1B0217023BF8D3E80C5EE57C0C135` |

Archive `D:\RedPrismQualificationArchives\P1-display-963-Debug335-shutdown-failed-2026-10-07`:
1,262 copies verified at `2026-10-07T20:50:30.4058458Z`; zero application stages.

## Separate experiments

The preserved experiment tree is `.build/P1Evidence/RecordedDisplay-2026-10-07/`, with direct-source projects,
selected SDK/MSVC/dependency input inventories, copied executable/PDB/source identities and attempt receipts.
These are design and standalone functional evidence, not application or G1 qualification.

- Display parent r2: actual PID 84804, `20:22:05.3947192Z`–`20:22:06.0165647Z`, exit 0. WARP and hardware
  each deliver 75 real command acknowledgements and 22 literal blit controls. One original 128 MiB fixture
  reservation remains through the real post-blit barrier and worker retirement, then refunds to zero.
  Executable `15B24884D95EAB894F62C5D4B4F215D9629578EE0C144F8EC1BBCA15AE3F4DF8`;
  stdout `BA5CE1CF7043CCB123669D3AD3EB5E7542A5C82E69A604A8D0EC4D16A3A52B19`.
  Earlier r1 failed because its source-lease output was not cleared after Detach. Its logs/result survive,
  but its overwritten executable was not preserved separately; no complete r1 artifact archive is claimed.
- Controller runtime r1: actual PID 124328, `20:26:39.5137396Z`–`20:28:20.6609716Z`, exit -1. The owned
  probe was stopped after a termination dialog; executable, PDB, sources, result and dump are retained.
  Source review found repeated transport Resume while its worker stage was running. The correction consumes
  the joined-stage flag before Resume. The dump has not been stack-analyzed, so no dump-derived cause is claimed.
  Executable `3E4CA5F8F922C299D4397BB062D68DBB6BF7BD35C78DE92AF7433D114C9789F3`;
  dump `FE0110DB9DF1CAA06907D33E0C1A4C121E1D10433BE86A6F9F68BB9F76664AC0`.
- Controller runtime r2: actual PID 72212, `20:28:31.7986460Z`–`20:28:32.3861215Z`, exit 0.
  Seventeen controls per device pass, using fresh actual WARP/hardware DXGI observations. Executable
  `2FA721185D561F8B38B0BC4900060642D0D46402C2DAB669CAD2D1E9A28CCA42`;
  stdout `D51BD8348D070513B2FDA69FCAD82C478BE8C6D875D9D8159279CF46F2C45ED7`.

## Debug build r336 and focused composition r337

After the shutdown fixture observation repair, the canonical Debug build passes: actual child PID 103000,
`2026-10-07T20:54:10.333695Z`–`21:00:06.697568Z`, exit 0. The 963-input freeze is
`EE40FB86816B5032B8483A42FD9E89D1E69507E9D1E7756A501A9637FAB0F623`.
Stdout `8B646D7E0275F27F123DF1B373567DE37D9239A44E086673D933412E2A3E3BA9`; stderr empty.
Archive `P1-display-963-Debug336-build-2026-10-07` under the qualification archive root has 1,253 copies,
verified `2026-10-07T21:00:35.9858890Z`; source/evidence/completion manifests:
`9C3B0CE4E09DAA48C96B35A9687BF137EF087BB4FA97485551D6B3C4D80D404A` /
`80AD343D4885DC58C1A01868C5EF4F6AD205D2FE6894F9BF9A2EFA1BEB7E8ECD` /
`F158C240D312D6BF638DD71891D96F007C4F89828BDA026A5369C5CE3CAB4781`.

Focused `CompositionTests` r337 uses the unchanged build/freeze: actual child PID 73464,
`2026-10-07T21:00:51.676397Z`–`21:00:58.180490Z`, exit 0. All composition controls pass, including all four
new joined-shutdown modes. Executable `4C1380D20B616D6546145597D318239C43E0E644DFE63753B9AC39E981C028E6`;
stdout `E94BDFA0743058D7D82EE200A62E9010D8A5E1682BA0039FDFE4EACEFC30436C`; stderr empty.
Archive `P1-display-963-Debug337-composition-2026-10-07` has 1,253 copies, verified
`2026-10-07T21:01:31.8258818Z`; source/evidence/completion:
`07CA9E9B0DB1A03D6FCCA45FB79FDA0455464736DC49E66634EA0123AFEE6194` /
`A17EED0ABD48AD657E92D957B236222DCBAEA48922EDA24683439F67F0C88213` /
`C3E621884F61AFCDE4F40E1928A736EDA9FDDEA42AA80A33F709389B7F8D210D`.
These are build and focused-runtime passes, not a full profile or application pass.

## Focused actual application r338: first-source startup gap

The unchanged Debug executable fails two of twenty new committed-display application controls after all
earlier reached standalone groups pass. Actual PID 114872, `2026-10-07T21:02:02.028981Z`–`21:03:45.336055Z`,
exit 5, same 963-input freeze. First settle succeeds without a graph because InitializeRenderer did not bind
the actual queue and `_gpuBindingError` remained `E_PENDING`; Describe refused before the first RenderFrame.
Rendering then binds the queue, and later edit/replacement controls pass. The corrected opt-in startup binds
the actual queue before the first admitted source request and propagates a startup bind failure.

The in-flight shutdown fixture also assumed one drain reached its controller leaf. The shared rotating
multiplexer can service another ready source first. The fixture now waits on real event/readiness transitions
until this display has an admitted pending record, preserving the original charge/shutdown expectation.
No timeout or legacy golden changes. Neither correction is qualified by r338.

Executable `9065C1DF7156028AF42571634B65F68812488E4A8B4B596917A7E6168EC75001`;
stdout `63DDAFBFFB541DAD636ABF3271ACB40BC4D676E10DE25401876D91C53BE8A42E`; stderr empty.
Archive `P1-display-963-Debug338-app-failed-2026-10-07` has 1,253 copies and 14 reached stages, verified
`2026-10-07T21:04:11.4719891Z`; source/evidence/completion:
`97E971BEF6C94D6F7C79E2AD3C13A17EF5B92CA8991B692D72CD292517E042DC` /
`6BC71B4BA8E472C5DE73B316B47996DD3A59E4B8D5F401B2A20DD6460AA34C12` /
`B700A3BDAB76972B19B51E536F390B1E096964FA70358CE9BC2490D1A113D1C9`.

## Revised candidate review

Primary review corrects two delegated retry waits that would return early after the expected first refusal.
The next candidate adds deterministic CPU admission refusal/retry and descriptor supersession. Real GPU
residency still requires fresh actual DXGI observations. No guessed budget or synchronous fallback is added.
Visibility suspension excludes the published pointer immediately, cancels useful preparation, and leaves
original command/last-use/worker retirement running on events. Repeated suspended requests admit nothing;
one real resume transition may admit the current document. Actual show, minimize, power and occlusion handlers
are wired, with an explicit hidden-rendering opt-in for qualification only. Normal activation remains due.

All successful experiment stderr files are empty. No timing comparison, throughput advantage,
native ARM64 runtime, final admission bound or normal display activation follows from these results.

## Corrected application startup and visibility: r339–r341

The 965-input candidate builds successfully in Debug r339. Actual PID 29464,
`2026-10-07T21:14:05.639740Z`–`21:20:45.171069Z`, exit 0; freeze
`6082F53DF5BC0EB9000857C12961BCBF058BFA274D58F75E80CE0ADD1C087AC0`.
Stdout `3D99657A3D5D7CC5F44CF045BA4504702DB841EC89536BB79E8619A17A310907`; stderr empty.
Verified 1,255-copy archive `P1-display-965-Debug339-build-2026-10-07` at
`2026-10-07T21:21:20.0822898Z`; source/evidence/completion manifests:
`F4C52D3A36AF07FB12556BFDC653DC5EEA033324197E06052C9E293CDC29457E` /
`75EC5B516C9155F683EC6260F637BADF5072EF4A368D6F0DC5C93EA45616FCE9` /
`B635E7C88FF81A70EC745FFB0093A2CF05A18EEE3A28A141EF68C1860DED1A45`.

Focused composition r340 fails two new close-fixture admission assertions. Actual PID 122212,
`2026-10-07T21:21:47.590114Z`–`21:22:04.658095Z`, exit 1, unchanged freeze/binary. Executable
`FDB2EAEBE9D96352A3C73961CB434276766A6FF9CAE8F39EEC2C0744D538B9D5`; stdout
`9271C30AE0EA2F65441BAD16E2EFA52549ADC4D0D7A318A803EBB73A5E4B04E4`; stderr empty.
The fixture replaced the document while comparing aggregate reserved bytes with the old cached charge.
Original-cache retirement can legitimately refund that charge concurrently with replacement admission.
The corrected fixture edits opacity within the same document, keeping the cache resident until suspension,
so the blocked new Opening and its separate admitted charge are observable before either can retire.
Its literal-pixel, suspension, held-opening cancellation and MTA close controls retain their expectations.
Verified 1,255-copy failed archive `P1-display-965-Debug340-composition-failed-2026-10-07` at
`2026-10-07T21:24:45.0837358Z`; source/evidence/completion:
`D6EDC3B26C0945FD996FAB84F2E3625ED43A579B26EDC4371BD9355925EC9588` /
`40725093C116376D6B9EA52062351392A3DBE058CA3CDBA2D308ECF0FE0D4B12` /
`B1516ADC0144E381D5ECDE13F1575FE81A31E8F909D3AD8A0872F4FB10D7ACF3`.

Focused actual application r341 passes `--self-test --warp`, all 41 new display controls and 103 application
stages. Actual PID 47716, `2026-10-07T21:24:55.356019Z`–`21:27:42.660348Z`, exit 0, unchanged freeze.
Executable `8FE42AC7FAD27D6629D5224B4D3522EABB65986BA886F47440A7B65BCA958DC0`; stdout
`59EF5195EAE98DD19B4F0433C094A71719DFBED2B44702921621ED29ECF5C71D`; stderr
`6FEC67C8403B600B345DF1EFFA8C39810B4820635A31E24BF3E723E2C24858F6`.
The opt-in route publishes its initial complete graph before the first render, blits without legacy owner
Compose, replaces an edited source, and retires cleared, cached and in-flight roots through real original-use
and worker acknowledgements. Actual show/minimize/power handlers and occlusion policy exclude old pixels
synchronously; resumed visibility admits the current source. The native test window remains hidden.
Verified 1,255-copy / 103-stage archive `P1-display-965-Debug341-app-2026-10-07` at
`2026-10-07T21:28:56.7659933Z`; source/evidence/completion:
`924C5CCDEF34A91388730CDF909DE749A5363837AD26AB99184B804EA637BE56` /
`D47267FFBA7F4A36984BAFA26B7E1A168D5893CAD7ABCE241011AC755D9364E2` /
`34738DD26065187768E9AAFF47FEE7CADB61D0C8D033A1FF97C1791FFC05C702`.
This does not close the failed composition fixture, remaining profiles, normal activation or G1.

## Primary review of delegated device rebind controls

The integrated source adds a two-device cached rebind fixture, fresh actual DXGI budget policy, exact four-pixel
readbacks on each original context, immediate exclusion and post-last-use worker refunds. A known budget must
produce a graph; unsupported budgets prove refusal only. The proposed active-command gate was removed after
primary review: stopping completion drains can leave scheduler notifications unapplied and its wake event
signaled, while draining can execute the very command being observed. No deterministic active-command gate
or result is claimed by this fixture. Actual active replacement remains due.

## Full x64 Debug r342 qualification

The canonical full `test.ps1 -Platform x64 -Configuration Debug` child PID 76220 ran
2026-10-07T21:32:26.735678Z–21:45:00.302919Z and exited 0. All four suites, build-time ISA
controls, crash probes, hidden WARP application self-test, licensed RAW corpus checks and harness
screenshots pass. All 103 application stages remain reached. The corrected r340 shutdown fixture,
visibility controls, cached two-device rebind and byte-exact worker brush controls pass on this source.

- Inputs: 969; freeze `0169FC16C46215A8F236D62224EDCC88C41AC3DB01F6CCCC09D7D1C370625B17`.
- Stdout: `95BEAC8B35F81338B19CC5DB4109EBFA89E0B25F4CD654387FC59A5C15456AB2`.
- Stderr: `302A4EBF455284AF34D079C522AC57CAFBA85A3A38697B5749C750B454DBF091`.
- Archive: `D:\RedPrismQualificationArchives\P1-recorded-brush-display-969-Debug342-qualified-2026-10-07`;
  1,270 copied products/evidence files, independently read back at 21:45:57.5007515Z.
- Source manifest: `EBF38F747AA98A3B229030B8D8B3718855FCC9D5EE4CDEB481C31FB2DD841E75`.
- Evidence manifest: `874F1CBCE541E78E5A38F6421C2C91D4456F58B7123105C37F787BE571B736FE`.
- Completion receipt: `6C5925AA65688E5D6FEB7000818ED1A09F2D6EADC7CD04E23947C397FA472537`.

This qualifies the reached Debug foundation/application states only. Release, real ASan, analysis,
ARM64 cross-builds, normal activation, actual asynchronous Append/Up and measured G1 remain due.
Subsequent exclusive-sample, lease-growth and epoch-return changes are a new unqualified candidate.

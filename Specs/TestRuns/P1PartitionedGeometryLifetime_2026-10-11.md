# Partitioned geometry lifetime focused qualification

Date: 2026-10-11
Scope: integrated x64 Debug build r842, Host runtime r843, and Document runtime r844. This is focused foundation evidence; it is not the current-source configuration matrix or P1 closeout.

## Shared source identity

All three archived attempts identify 1,180 source inputs and freeze SHA256
`91A50EE711CA3276EC065B34A8AF4B1156442189CDDBBF661C3E93C9B383BA2B`. The qualification profiles bind the
same source as `35CE837741F8F8CB794E5C30A7D2186C8391012A0B0DFB4C6FE90AB283661866`; their archive manifests
report source and archive-copy verification. The initial private mixed-header ABI failure remains preserved.

## Actual integrated runs

| Run | Actual result | Independently inspectable evidence |
| --- | --- | --- |
| Debug build r842 | PASS; x64 incremental build; child exit 0; 1,180 inputs; stdout SHA256 `C63091D33AC568D280523DE6AC2C439748E671FB712F145D31218C4D9266F387`; stderr empty. | `D:/RedPrismQualificationArchives/P1-geometry-retention-1180-Debugr842-qualified-2026-10-11/Evidence/.build/incremental-x64-build-Debug-r842/result.json` and `stdout.log`; archive receipt records 382 evidence files. |
| Host Debug r843 | PASS; actual `HostTests.exe` exit 0; binary SHA256 `07751C823575A786DC491115BDE879F2041715985A0C34A3D62A84C93CF47AB9`, unchanged during run. Profile: 8,838 pass records = 8,795 preserved + 43 additions. | `.build/P1Drafts/RetainedPartition-r1-Main/r843-Host-native-profile.json`; archived output at `D:/RedPrismQualificationArchives/P1-geometry-retention-1180-Debugr843-qualified-2026-10-11/Evidence/.build/incremental-x64-runtime-Debug-r843/stdout.log`. The `retained-partition.*` records exercise exact partition charges, no-query transfer, weak-reader accounting, order-independent acknowledgements, slot reuse and worker geometry destruction. The added publication callback/retention/owner/endpoint records exercise real scheduler tickets, worker retirement and Close. |
| Document Debug r844 | PASS; actual `DocumentTests.exe` exit 0; binary SHA256 `62FE793955C5211A6041AA005E452E5CE0C59A780DCE74242CB7DB56520E9F3C`, unchanged during run. Profile: 8,820 pass records = 8,784 preserved + 36 additions. | `.build/P1Drafts/RetainedPartition-r1-Main/r844-Document-native-profile.json`; archived output at `D:/RedPrismQualificationArchives/P1-geometry-retention-1180-Debugr844-qualified-2026-10-11/Evidence/.build/incremental-x64-runtime-Debug-r844/stdout.log`. The `private-geometry.*` additions cover immutable geometry adoption, cached metadata, cancellation, worker-node destruction and weak-control lifetime. |

The Host scheduler controls are actual integrated scheduler/retirement controls, but neither runtime profile
claims a production Wand boolean caller or its admission path. The Document geometry records are primitive
adoption/lifetime controls; existing Wand behavior in the preserved suite does not turn them into a qualified
partitioned native Wand caller. Neither profile claims G1 or ordinary activation; both mark those false, and
the Document profile marks P1 done false. No H3 completion is established by these focused runs.

## Remaining qualification

Full current-source Debug r845 passed: child39428 ran03:21:18.987966–03:39:32.474925UTC with exit0,
stdout SHA256 `A4AB5427E265B8D637FDDFFE4A8C09B2268BF491AD67BC63A99F198D59804FF4` and all126
application stages. `r845-Debug-full-profile.json` verifies Host8838, Document8820, Composition6838,
Io1823, application stdout7393/stderr6773, all63 prior parser controls and132 RBX controls. The reader
also preserves the entire nested parser inventory. All1573 archive copies authenticate; completion is
`935C9DC6CFA50712D3FA62984A29B1AB3408F02AD762BE15428E3B4CFA04ED63`.

Analysis r846 failed before compilation because parallel dependency restoration could not acquire the
shared x64 vcpkg lock. Its exit1 archive remains preserved and independently read back, completion
`2FCD2BE2D0A16AE29002B913671AAE829CDFDB548FC04B538606285BA5CB20B1`. Analysis was restarted serially
as r847 after the active dependency restoration finished. Its actual child47852 exits0 after
03:31:34.321215–03:45:00.986129UTC; stdout SHA256 is
`9DE658FDCDA80D476314D486B8BD184C234A38E80B809406298A8F602E7A1749`. Independent read-back verifies
all1565 source/evidence copies and the1180-input freeze; archive completion is
`FDC50FA39152B5FC3CB6A77F9369ED12B2D0F0283200E1A8855E1B9DECFDF26D`.
`r847-analysis-archive-audit.json` records the actual analysis-only scope. The fresh full ASan and Release results below supersede their earlier pending status.

ARM64 qualification, native Wand caller/admission, measured G1 and P1/H3 closeout remain pending. Native runtime passes do not establish activation or latency.

## Full x64 foundation matrix completed

ASan full r848: actual child45896, exit0, 2026-10-11T03:41:56.273853+00:00 to 2026-10-11T04:05:49.746168+00:00.
Stdout SHA256 `9E9117159AD1F0AC19F2E68E70D5917BE264426EE40AB00F5E78F83DC6FD6BAA`, stderr `6DD5CC5B7D296199C5FA4F7EC5F5C26984685955DCA6EBF785C1AD4C5861BD54`.
Main's `r848-ASan-full-profile.json` authenticates the exact1180-input freeze, all126 ordered application stages,
all preserved predecessor and addition inventories,63 parser controls and132 RBX controls.
Archive `D:\RedPrismQualificationArchives\P1-geometry-retention-1180-ASan848-full-qualified-2026-10-11` has1543 verified source/evidence copies;
completion SHA256 `F23A539DC4A21FB6F894D8C4B699CAF2916470BFE2578E8C682E36899DC06398`.

The actual isolated sanitizer diagnostic SHA256 is `A4E0E18B8EBB0AF91CE3A728A843B567F941CFFA1BEBF20B82062EF5206A0C89`.
Host8839, Document8820, Composition6838, Io1823, HostStress9237, DocumentStress1 and application
stdout7393/stderr6773 pass.

Release full r849: actual child55844, exit0, 2026-10-11T04:08:11.253762+00:00 to 2026-10-11T04:21:41.343251+00:00.
Stdout SHA256 `C661CBFD75CD2ECD5D4577AB20517841A047CF42CB22831481F60BD645DF56FA`, stderr `8A479E3463959EA764BF588FB4FCCEBD40E8EBC1A98B0B42EAE5462D667EB1AD`.
Main's `r849-Release-full-profile.json` authenticates the exact1180-input freeze, all126 ordered application stages,
all preserved predecessor and addition inventories,63 parser controls and132 RBX controls.
Archive `D:\RedPrismQualificationArchives\P1-geometry-retention-1180-Release849-full-qualified-2026-10-11` has1553 verified source/evidence copies;
completion SHA256 `7E14E24E96FC0625F088BD830351277C120C9F1EEC3BB30FCFB4EA8F9CA4BA9A`.

All15 skills validate and formatting checks1042 files. ARM64 r95 is running after a reviewed
launch gate authenticated Debug/Release/real ASan/analysis archives on the exact source and20 contracts.
This matrix qualifies the memory/geometry foundation; native Wand integration, ordinary activation,
measured G1 and P1 Done remain open. Native ARM64 runtime is unavailable.

## ARM64 cross-build matrix completed

All three actual children ran serially on the same1180-input source and20-contract binding after Main
authenticated the four current x64 reports. All archive source/evidence copies and pinned dependency identity
verify. The real-ASan profile authenticates `/fsanitize=address` and the forced sanitizer requirement in
each of the five actual compiler command records and corresponding instrumented images.

| Profile | Actual child | Archive completion SHA256 |
| --- | --- | --- |
| Debug r95 | child2288, exit0; 2026-10-11T04:32:14.942947+00:00 to 2026-10-11T04:33:59.274730+00:00 | `90CB6926DE2F5B99EE96E5F87C18CF4AFF846486F95A88DE6CD243FD64269F23`; five ARM64 PE/PDB products |
| Release r95 | child73936, exit0; 2026-10-11T04:34:43.904236+00:00 to 2026-10-11T04:37:09.856214+00:00 | `4CEB1AC76699FFEA0CF26FADB6106D042DA14B5A13C6B08DF03540B69E0891B8`; five ARM64 PE/PDB products |
| ASan Debug r95 | child35608, exit0; 2026-10-11T04:37:52.349472+00:00 to 2026-10-11T04:39:36.246950+00:00 | `AB0EAACDB1D6A586A2544203A63F4388F08FE319D41DCB271FBC820DAAB7740B`; five ARM64 PE/PDB products |

The independent PE CodeView/PDB reader verifies all15 GUID/age pairs; report SHA256 `D1EA5F66B16548E0E693CE8213188323227A2F06ED61E9AD47A577547FF9B40E`.
Native ARM64 runtime is unavailable. The foundation's x64 matrix, analysis, three ARM64 cross-builds, skills
and formatting are complete. This qualifies the component for commit; native Wand integration, ordinary
activation, H3 completeness, measured G1 and P1 Done remain open.

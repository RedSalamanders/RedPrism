# P1 layer-rows retirement — 2026-10-12

Status: **Accounting-r2 private strict original-output sum smoke PASS; native integration is complete and fresh r962–r965 qualification is active.** Binding `83AEC5FF3CF381779D2F3BAC5F49EDD5D7531045A1B53C925E62733F38AF30A3` covers 1,289 inputs / 20 contracts. The standard bridge passed under PowerShell 5.1 and 7 (18 controls plus summary); receipts are `standard-smoke-bridge/ps51.json` and `ps7.json`. This is not full native qualification or P1 acceptance. r959 failure and earlier failures remain below; the Accounting-r1 partial-link ISA result remains historical and unresolved. See the [P1 checklist](../Plans/WIP/UiNeverBlocks_2026-09-22.md) and [incremental-history plan](../Plans/WIP/IncrementalHistoryStorage_2026-10-12.md).

## Preserved native r959 failure

Native r959 Release failed at `layer-rows-smoke`, trial 1, retirement: actual child 70020, exit 1, 2026-10-12 16:28:17.214130–16:52:37.112716 UTC. Archive manifest SHA-256 `F0161E7A62DE3123B0256EFB27D4347A806C307903D5F8FBB896F9B01FE91CC4`; completion `50CED07E7749B76257377836A07AF250DAF74259896ABF8A96E3AD0112E99D95`; stdout `CDCD7CBEA1B8CA6C7D3C6BB3A02EC03FB113B8BBB76C3D4303280F08FB42EBAB`; stderr `F034AF946344E0547D1740FA1CB12D6AC2B35A8F9443FBD76F07AD04AA33CF0D`. r960 Debug and r961 real ASan were NEVER LAUNCHED; ARM111 remains held and NEVER LAUNCHED. The r958 analysis PASS and the earlier r954 C6262 failure remain preserved in the parent receipts.

## Isolated Diagnosis-r1 observation

Diagnosis-r1 used source count 1,287 and reports status `REPRODUCED_FAILURE_OBSERVATIONS_ONLY`. Its actual Release build was PID 94516, exit 0, 2026-10-12 17:11:05.630237–17:19:51.854084 UTC; build stdout SHA-256 `0DAD351ACE64D866111DEBD1B7FDF487A75B394AAE9AF398027127A61682874D`, stderr empty (`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`). The actual WARP smoke was PID 96548, exit 5, 17:20:06.626943–17:20:06.839454 UTC; stdout `B09D8D9377FBCA98BB47CF2C0ABFD89EAF63CCD2C70BD9CE3FE7E215EDDADEFC`, stderr empty (same empty-file hash).

The unchanged predicate observed `reserved_bytes=2780348`, `row_bytes=2306436`, `extra_resident_bytes=473912`, `releasing_bytes=0`, and `undo_count=1`. Diagnosis receipt SHA-256: `6C22E675A3E57ED2ECF0144E04A36B358F6A4FEF0E72CD5A76A99C2175C03CE8`. This establishes a whole-scheduler charge exceeding row charge in that trial; it does **not** establish that all 473,912 bytes are metadata/document/history outputs or that the old predicate is invalid. Accounting-r2 now qualifies the exact original-output sum in an isolated private Release/WARP smoke; details and remaining limits are below.

## Accounting-r1 ISA result and attribution limit

The partial-link Accounting-r1 image was refused by the ISA scanner before runtime: 488 vector instructions, 480 accepted runtime instructions, zero EVEX, and 8 rejected VEX instructions attributed to `ApplicationFiles.obj`. The original full-build Diagnosis-r1 scan passed all 488 runtime vector instructions. Accounting-r1 reused 305 prior objects and appended its rebuilt benchmark object after them; the original link order placed that benchmark earlier. The changed order can change addresses and contribution attribution, but the saved evidence does not prove ICF caused the rejection or identify a trustworthy containing function. No scanner/allowlist bypass was applied, and Accounting-r1 runtime was NEVER LAUNCHED.



## S5 selection-outline host route

The private host-r1 build passed (PID 104500, exit 0; 17:06:21.188658–17:16:51.173558 UTC; stdout SHA-256 `68430CFC3DB29A2A2D789E73DCC4F79F73C1768BA8EF4DDF1849F9D9CF30ED24`). Its ISA scan passed (PID 101324, exit 0; 17:16:59.170212–17:17:09.486246 UTC; report has 488 vector/runtime instructions, zero EVEX, zero violations). The host self-test actually ran as PID 105596 and exited 5 at 17:17:09.636628–17:17:12.230340 UTC: 46/47; the cold-acquisition case requires another display route. Its stdout SHA-256 is `13B8002F0F7CB20C11A651C7AFB83908E2A80C8A86CFF204895BC9A210BA1289`, stderr empty. These are private draft results, not host acceptance.

Host-r2 failed at manifest tooling before runtime: actual build PID 101356, exit 1, 17:22:37.489053–17:23:04.098486 UTC, `mt.exe`/LNK1327; build stdout SHA-256 `A287D85333F6E41484FC1D2AB2D8D936419397A491863131C9BF4F9E52C5F245`, stderr empty. Host-r3 now has a scoped private Release/WARP PASS; the cold-acquisition fix and actual build/ISA/runtime receipts are recorded in [P1 selection-outline host](P1SelectionOutlineHost_2026-10-12.md). Earlier host-r1 and host-r2 failures remain preserved. This does not qualify ordinary application activation or close S5.

The private selection residency/controller evidence remains scoped evidence. Fully responsive current routing peaks at 10 owner-work slots; a concurrent selection source requires 11. Complete host integration, admission, lifetime, current-view/frame activation and G1 remain open. The parent’s nine delivery areas stay unchecked and P1 remains WIP.

## Accounting-r2 exact original-output sum

The private Accounting-r2 qualification reports PASS (source count 1,287; 5 negative controls), receipt SHA-256 `14B860412A1A511C86063052A9E22F7B81BC023BC59488639F2D8FA355080233`. It preserves the strict original-output equality: the exact expected sum is rows plus the uniquely bound metadata outputs, with the independent original name/pixel/history references retained. The observed reserved total equals that sum for every trial:

- Initial, no rename: `40,676,124 = 40,676,124 rows + 0 metadata`; 0 releasing bytes; 0 originals.
- After one real rename: `2,780,348 = 2,306,436 rows + 473,912 metadata`; 0 releasing bytes; 1 original.
- After two real renames: `3,231,012 = 2,306,436 rows + 924,576 metadata`; 0 releasing bytes; 2 originals.

The actual Release build was PID 91900, exit 0, 17:33:11.586282–17:41:22.839192 UTC; stdout SHA-256 `EC7590C4BA7FF263D7C67D98D6C16B3987780ACE00828B328AE810FAE95556F1`, stderr empty. ISA scan PID 38212 exited 0, 17:41:26.969679–17:41:37.806962 UTC; stdout SHA-256 `0FE78973D8E999A5ACB5F409B75F15545476DB306A5560E7DB31521DC6F48555`, stderr empty. The actual WARP layer-rows smoke was PID 52980, exit 0, 17:41:37.942096–17:41:38.178702 UTC; stdout SHA-256 `DDEB28AB79F963EEE542A475972FFFB48C9D9DF67A46606EDF4E04C933C21602`, stderr empty. The source/dependency/product bookends passed; reader SHA-256 `E3921650FF423F54BC46B6723BA3D864E29919166409CA29F67587C0662A9004`.

This is isolated private Release/WARP evidence for the named smoke and strict output-sum accounting. It does not qualify full native profiles, other callers, performance/G1, ordinary activation, or P1 acceptance. The r959 native failure, Diagnosis-r1 observation, and Accounting-r1 partial-link ISA refusal above remain preserved.


## Native integration follow-up

The 1,289-input / 20-contract source binding `83AEC5FF…` passes Release analysis r962 (actual child 38348, exit 0, 17:53:53–18:36:40 UTC) and full Release r963 (55884, exit 0, 18:37:30–19:13:12 UTC). Strict reports are `D7BC892638F92312FCFE081EF52EE50AA12CB5CC552D92BBA0B7E3F17E949E44` and `60AD8525F8477044C8E160422B40BD0AD355B47321BDD218092BCA4F581277CC`. Release preserves Host 8,940, Document 20,782, Composition 6,838, IO 1,823 and application 7,410 stdout PASS / 7,613 stderr / 133 stages. The standard bridge verifies 18 parser controls plus summary and every exact three-trial original-output sum above.

The original r963 reader refused a stale sizing-source hash after native exit zero. The corrected reader authenticates the historical CRLF preimage (`B9941ECA…`) and current fixture-only change (`B8294DC1…`), preserving all 22 modes, two retirement records and exact charges/identity assertions. No successful native runtime was repeated. The original reader refusal remains retained.

Debug r964 (actual child 66920, exit 1, 19:41:37–19:54:48 UTC) stops at MSVC C1001 in `ApplicationCanvasBrushLifetimeShutdownSelfTest.cpp:325` (`mre.cpp:508`), before runtime. Actual stdout `B5B169ECEA06923602FF361EEC76A5BDEDCC238109171938F4718EB03BF7AAAB`, stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`. Failed source/build logs are archived; ASan r965 was NEVER LAUNCHED. Debug/ASan/ARM remain unqualified for this source; all P1 delivery/performance areas remain open.


## Fresh full Debug r966 and first ASan r967

Debug r966 actual child 77476 exits 0 (20:11:17.132648–20:43:26.638622 UTC). Strict full-profile report `8310B63B873369AD61EB379E03CBEBBED938EEAD0110827B2604EE6DF32455F6` authenticates the unchanged 1,289-input / 20-contract closure. Its native stdout hash is `EEB1E895B6F5942E75B29FC15D0309FBF31F93B703142D587FE842756242F808`; stderr is `BB52A27BA9E5DF37DBA089FAAA0DA1C0E90DECBD65DB86C764F7EBDEA60529EA`. Full Debug is qualified; the original C1001 remains failed evidence.

ASan r967 actual child 43332 exits 1 (20:44:04.946624–20:52:39.863303 UTC), before runtime: the ISA scanner rejects instructions attributed to pinned DxUi.Menu.obj. This is an ISA failure, not a successful sanitizer run. Native stdout hash `8CF44F7D781EE939A6449CA7F528BEC9F2C984B3256A54A61CB2BC602274735F`; stderr `7B58803B05B491F2E41338EB1EA2F740EEECE81BCE6B0AA684018FA490A688F2`. The archive copy separately fails for disk space and is preserved as partial evidence. Main is diagnosing the instruction/symbol provenance; no ISA exception, pin or threshold change is accepted without proof. Full ASan and ARM remain open.

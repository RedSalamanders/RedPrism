# P1 size final host evidence

**Receipt:** `P1Evidence/p1-size-final-coherent-app-private-r5/ASanDebug/runtime-qualified.json`  
**Result:** private native R5 actual process PID 88476, exit 0; 84 Size facts and 473 exact Ctrl predecessors passed under ASan Debug. Size cases covered image, canvas extension, cancel, history cancel, invocation, restore, final admission failure, joined close, and late cancel after apply. Source and product bookends passed; ISA violations: 0. The receipt retains restore-control exit 5.

The sequence includes failures. R2 failed its restore-pointer predicate. R4 failure-only diagnostics showed current/new rows while the old GPU graph was not current after a foreign Undo path bypassed `DocumentChanged`; CPU state and foreign redo remained intact. Fixed R5 calls real `DocumentChanged` and requires current rows, stable item ID, literal CPU state, GPU publication, and foreign redo. R5 is the relevant passing host proof; earlier failures remain part of the history.

This is private host evidence only. It does not establish G1 or a 2 ms bound. Ordinary activation is disabled, and the private Main selector is excluded from adoption; the selected run is not full Application runtime. The complete native and header-coherent matrix is still pending adoption. Do not describe P1 as done.

Receipt identity: exe SHA-256 `3EE203713DF26D732D35C2D6A7229F9DB3934C45A98E6952A49D057468AAE7AE`; PDB SHA-256 `11CCC36CECC14A8F13420D3350D84E805A49831B31B37E801214C286176F34DF`; stdout SHA-256 `383FFDDEDF1C4D05C42FBFEBF65E5D5671FDDFDBF8B030851EF53CCAA1249CF2`.


## First-row extension and native adoption

R6 actual PID97580 exits0 at2026-10-20T02:38:00Z with 129 Size facts and all473 exact Ctrl predecessors.
Its additional45 controls hold or fail the actual first-row producer and require same-original resume,
cancellation before that producer joins, current-epoch failure with explicit retry and joined Close.
All84 prior Size predicates remain unchanged. The final stage completes hidden without a RenderFrame pump.
Source/product bookends, exact358-object/header cohort, full PE/PDB identity and actual ISA0 qualify.
Compile-only Release analysis of the expanded test also passes with the production16KiB stack limit.

Reviewed code/test/standard Main registration is now adopted with two owning headers. The private Main
selector and absolute fixture include spelling are excluded. Current native1,022-TU/155-stage qualification,
the complete Size phase bounds/2ms, ordinary activation and measuredG1 remain pending. P1 is WIP.


## Current canonical native integration

The exact1881-input source freeze `6B6218AFA5A008BAD01526F345FF4FBC32E7033B637063A12BEE4B53369B1D04` and working delta `3BE97567531A1355D94D39DD4CDAA849ABFA607513833F58F3C2BB837798E153` pass the following complete current scopes. The qualification uses
1022 actual compiler records: Application358, Host136, Document299, Composition145 and I/O84, with
five matching full PE/PDB pairs per profile.

| Attempt | Scope | Actual child | Exit | Archive manifest |
| --- | --- | --- | --- | --- |
| f3a1 | x64 ASan Debug test | 81344 | 0 | `B823A8C3EAE0541469E413AD1F4D253CC6C93D37CBDA8127314486B9473DF278` |
| f3d1 | x64 Debug test | 32404 | 0 | `47EEB47FD025553DE8AA99ABAB37597FE54CFDBADFD32882DAC7E00739AB907D` |
| f3r1 | x64 Release test | 92364 | 0 | `03673B19A1F4C767FAC7F6DC2EAB49E486E374B96DB77D4AD513A5993867CAB9` |
| f3armr1 | ARM64 Release build | 78048 | 0 | `0769EDA9255F4879B280C2747F7F636D61DAF07E67828D7E74409CFA8C5BFEC4` |
| f3armd1 | ARM64 Debug build | 105524 | 0 | `F8A986FEE98A35E60CD4645645323B372F400FAE5696BA9278D74550AA7EA763` |
| f3arma1 | ARM64 ASan Debug build | 106024 | 0 | `C202F1D8E4D2F40D48AABDE6E75470717A19E96C3573D20DC4030ED25B290A62` |
| f3an1 | x64 Release analysis | 15712 | 0 | `93635B6705C86D85B46480E2623ABE7A0FED2F7DC818E10D05113C875289CFFE` |

All three x64 full runtimes preserve155 Application stages and12155 facts, including129 Size and473 Pixel
publication facts; DocumentTests24009 in Debug and24003 in Release/ASan;242 original component facts.
Real ASan additionally proves the isolated UAF diagnostic,256-round raster and64-worker stress. Both crash
probes, normal smoke routes and all nine fresh harness captures pass. Release analysis includes all1022
actual analyze commands, a clean positive SAL control and the expected negativeC26130. ARM64 results are
cross-builds only, including real instrumentation; no native ARM64 execution is claimed.

The separate privateR7 follow-up passes181 Size plus473 Pixel assertions, adding52 actual suspended-slot
resume/cancel/Close and device-rebuild/refusal/retry facts. Its one-fresh-plus357 unchanged private objects,
full PDB, actual ISA0 and source/product bookends pass; its expanded test is not yet adopted here.

The focused actual-handoff benchmark and row-holder worker construction remain unqualified drafts. Complete
phase bounds,2ms Apply, ordinary activation, measuredG1 and the remaining parent delivery gates are open.
This is a qualified implementation step; P1 remains WIP.

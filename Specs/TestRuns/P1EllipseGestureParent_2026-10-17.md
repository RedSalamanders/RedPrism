# P1 Ellipse/New parent and geometry — 2026-10-17

## Qualified scope

Private CPU Release and fresh real x64 ASan both pass **22,693 assertions**: 20 new geometry/reference facts,
77 original-parent/authority/lifetime facts and all 22,596 predecessors. The actual Win32 Ellipse caller is a
separate compiling snapshot. Ordinary activation, combined native profiles, G1 and P1 Done remain open.

`NewMarqueeLimits` and `PlanNewMarquee` accept Rectangle/Ellipse only. Rectangle retains its exact old small
plan. Ellipse budgets a 4 MiB clip workspace, 2,052 input points/two loops and 4,096 output points/16 loops,
one opening arena block and no sealed-prefix traversal. The worker uses those same limits. A New result does
not feed a large old outline into the clipper; source/history/control ownership remains separately admitted.
The descriptor carries its captured kind, and the parent refuses a mismatched Rectangle-only admission before
source capture or mutation. Default old callers retain their Rectangle/inside-outline semantics.

## Independent references and review

The cheaper agent writes the bounded 20-fact geometry fixture; Main reviews every predicate. Before runtime,
Main requires the serial reference to honor ClipperD precision2 scale 128. It generates grid-rounded vertices,
clips with a separate serial convex algorithm, then compares shoelace area using an IEEE-754 accumulation
bound derived from coordinate magnitude/vertex count. Geometry error is not hidden in an arbitrary tolerance.
Main strengthens the ported legacy case to exact coverage bytes 255 at (40,40) and 0 at (11,21), retaining
`retained numeric golden cases–354` bounds and circle references. No old golden is changed.

Main adds 77 parent facts: absent/empty/old/65,540-point prior/no-layer sources, Shift/Alt, Lost, cancellation,
joined Close, foreign history/transform, prepared cancellation, single history entry/Undo/Redo, original-ticket
charges and final refunds. The oval references require 80/64/32 vertices for the named literal bounds. The
old 75 Rectangle facts keep their exact calls/defaults and output labels. The final two cases deliberately
supply the smaller Rectangle admission and require refusal before capture plus a complete refund.

## Passing profiles

| Profile | Actual PID / exit | Assertions | Exact compile records | Archive leaves | Completion SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Release R1 | 82848 / 0 | 22,693 | 278 | 1630 | `904878345991BCFCEAB3A89692AF494A205F5E3F0DAFBF97505E6A3D610F8063` |
| Real ASan R1 | 87516 / 0 | 22,693 | 278 | 1636 | `FCEDCDFF4DFFFF470778DF033628C87090E3D354BC218BE17524D57BC39DD628` |

Both profiles are fresh 278-TU builds. Main independently verifies every exact compiler command, source/
dependency/product bookend, complete archive, matching full PE/PDB, zero ISA violations and every ordered
runtime record. The new source-only tail reader passes four positive and 50 negative mutation controls and
delegates the unchanged complete predecessor checks; it never manufactures a passing full baseline.

Release's first artifact-reader invocation refuses a copied stale S10 recipe-status literal. The second
read-only verifier corrects that single literal and verifies the same actual source/process/artifacts. The
original refusal is retained; no C++ build or runtime is repeated.

The same fresh instrumented CPU binary's intentional UAF probe is PID 63548, expected exit 1 with
`AddressSanitizer: heap-use-after-free`. Its normal test child exits 0.

| Profile | EXE SHA-256 | Full PDB SHA-256 | RSDS GUID / age |
| --- | --- | --- | --- |
| Release R1 | `5D9FE44A09EA9B9C24ED8683344311B43DEF2E81FCD58CE4D4D84F59B5CA7ABB` | `BE4210A45E5C2BDC526B7C5510CFC356A32F921B60A74B047EB8863E998C3FD0` | `C40AB4C1-F983-4250-8D55-AF1080B88EA2` / 1 |
| Real ASan R1 | `D58E68296725546579C2890D98ADCBF2FDBFE7DE06D991D3D56F7C933683B218` | `C50D5DED864EBA269760BDFC5CBD4D012122A4FD9B37057ADAA1858F268C33B7` | `C0794C50-C6FC-42FA-B91E-1983602028F5` / 1 |

Local Main receipts are `.build/P1Drafts/S5PressMarqueeRelease-r1-Main/main-qualified.json` and
`.build/P1Drafts/S5PressMarqueeAsan-r1-Main/main-qualified.json`. Generated sources/products/full PDBs/logs and
archives remain local. Closed, qualified ARM64 object caches (2,907 files, 6.89 GB) are reclaimed only after
exact cohort/hash and retained executable/PDB checks; all source, products, libraries, logs and archives remain.

The separate Win32 snapshot changes only Application declarations, Main registration and the factored physical
fixture; all 278 CPU sources/headers match this qualified source. Its 89 new facts and complete 147-stage
Release/ASan caller gates are still due. The current host route update is excluded from this CPU commit.

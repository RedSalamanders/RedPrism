# P1 Polygonal original-parent branch qualification — 2026-10-18

Status: complete scoped CPU Release and real-ASan qualification, independently verified. This is a CPU branch prerequisite, not physical dispatcher activation or P1 Done.

## Implementation

`HostSelectionGestureInput` borrows either the existing fixed press stream or the bounded Polygonal journal. Its samples preserve the complete journal view snapshot. Consuming a physical Up does not itself terminate this view; the selected branch explicitly chooses its last-consumed terminal. Acknowledging or dropping one borrow never closes, drains or finishes the family.

The original `SelectionOutlineGestureJob` admits both possible peaks before source capture. Polygonal uses the independently bounded `PolygonalLimits`/`PlanPolygonal` via a private mapping; the public drag-Up `PlanDrawing` continues rejecting Polygonal. New with a nonempty prior selection runs exactly one cancellable worker hit and chooses inside outline movement or outside drawing on the same admitted ticket. Add/Subtract draw without containment.

Outside replay preserves Up, hover, Leave and actual event-time view/DPI metadata; no hover adds a corner. Every continuation consumes at most 16 accepted journal records, and empty/unready work parks without polling or re-admission. Readiness is checked before Take and before selecting/capturing a terminal. Inside Up selects only its own parent boundary and retains later Down records. A missing release ends only the old inside drag at its last sample while preserving the younger Down. Overflow after an accepted Up cannot retroactively cancel that parent.

Terminal callbacks see `controller == nullptr` and the parent acknowledgement already set. The callback is the final state access, so an owner observer may retire its state. Joined shutdown suppresses useful callbacks. Stale journal Down generation/restore/target and foreign pending history refuse before source capture; validated owned predecessor transitions remain caller work.

## Independent oracles and scope

105 new assertions over 24 scenarios preserve every one of the 23,454 predecessor PASS records. All 120 pixel centers compare against literal rectangles, union and subtraction. Inside movement translates the original rectangle by (3, 2), invalidates generation once at Move and once at End, and yields one Undo/Redo entry. Short New clears once; short Add/Subtract retain exact prior selection/history. Other controls cover explicit family retirement, younger Down preservation, missing Up, all 512 accepted records plus emergency loss, original reservation/refunds, ordinal/terminal readiness, Escape, parked/running cancellation, joined close, Backspace-to-zero, genuine foreign History Begin, forged generation/restore/active/crop/mask, double-click without appending, sixteen-record hover replay and captured physical view/DPI closure.

This complete affected DocumentTests qualification does not execute the physical Win32 dispatcher, enable ordinary routing, establish native ARM64 runtime, close G1 or replace the final latest-source matrix.

## Retained failures and corrections

1. The cheaper prepared private project pointed at its own stale parent copy. The cheaper review checked basename metadata and the same wrong folder. Main's exact path/hash/metadata review refused it before compilation, relocated all private mappings and retained both outputs. Main's first missing-file preflight assertion was also wrong: the stale file existed; its bytes were the defect.
2. Initial Release r1 compiled 284 objects but failed the parent on Windows' legacy `near` macro. Main renamed the local to `proximity`; the complete failed products, commands and candidate sources are preserved.
3. Before replay, Main independently found that the public drag-Up planner intentionally rejects Polygonal and mapped the existing bounded Polygonal plan explicitly. This does not broaden the public drag-Up contract.
4. Release r2 ran the whole suite and failed two predecessor physical-prefix assertions: the new sixteen-record quantum was also applied to the old press input. Production correction restricts the quantum to the journal; predecessor tests are unchanged.
5. Four new r2 fixture predicates were incorrect: three expected one inside-generation invalidation instead of existing Move+End's two; one attempted a transform on a blank nontransformable layer and never opened the intended foreign editor. Main used the existing generation contract and a genuine pending History Begin, adding an assertion that this foreign history survives.
6. Changed Subtract uses a literal overlapping rectangle. Short preserving terminals separately prove no-op history; this does not change Boolean math or rebaseline legacy goldens.

## Release r3 — qualified

- Actual build, runtime, ISA and independent reader exits0.
- 285 actual source/command/object records. The two changed CPPs recompile; the other 283 object bytes are independently identical to the retained completed predecessor. Source bookends cover 1,498 root/private/toolchain/policy/domain inputs. All headers are consistently private; canonical old State/Job/Memory headers are absent from compiler read logs.
- 23,560 total PASS lines: 23,454 predecessors +105 new assertions +one new summary. Full predecessor multiset unchanged; new labels unique.
- 317 archived files, manifest SHA256 `4D788436AA1AC2401677C98A97E1AA1912696FA41AD2C130B5BBBCEE8FB422C1`.
- x64 machine 8664, matching executable/full-PDB GUID `995186CD-9B28-4CC0-A2CF-39D9877F6E52`, age 2.
- EXE SHA256 `5AEF035F2FA9BED196563BD7563D4368E3CD7380D4229B20B4E31F32278C326A`.
- PDB SHA256 `4F4699BBE8537A92E2CECEB94BC3BD1DD6033E0272C7808BB92C44A7E75624C5`.

## ASan r3 — qualified

- Fresh 285 translation units each carry `/fsanitize=address`, debug CRT and the mandatory forced instrumentation header. Build, complete runtime, ISA and independent reader exit 0. The isolated UAF control exits 1 and reports `AddressSanitizer: heap-use-after-free`.
- All 23,454 matching-profile predecessor PASS records plus the 105 new assertions and summary pass: 23,560 total. All 1,498 source inputs and every archived byte pass independent verification.
- 325 archived files, manifest SHA256 `0CE36E8F2D11865CF125157EC661B23C407BEF5EBACBB7A49C3044F4426C5704`.
- x64 machine 8664; matching executable/full-PDB GUID `108ED168-ED7C-4916-ABE7-89127D54836E`, age 1.
- EXE SHA256 `76646821A85632282E695C449F6E414C9A57D0213480C6A22E89B38A41466344`.
- PDB SHA256 `1AB5FB55604BF5E5888E44125B8E78D7CA512745F1382A1D86338306504BCD83`.
- Retain the original post-runtime reader refusal: it compared ASan against Release allocation/cancellation and scratch diagnostic payloads. The independently qualified matching ASan baseline contains all 23,454 unchanged predecessor records. Main also retains the first overly narrow diagnostic-prefix guard before its precise 40-record audit. Only remaining ISA/probe/readback steps run after correction; build and complete tests are not replayed.

## Adoption

Main copies the seven qualified implementation/test/runner files byte-for-byte, adds the new module to DocumentTests and lists the two transitive owner headers in both owning projects. The five temporary test-TU clones differ only in private header include resolution; canonical predecessor tests stay unchanged. Three owning contracts, both opening checklists and the detailed upcoming physical caller sequence are synchronized. This is nine code/test/project paths; the physical dispatcher and final milestone matrix remain open.

## Local receipts

- `D:\RedPrismBuildCaches\.build\p1-polygonal-dispatcher-cpu-r1` — original compile failure, original sources/products and initial284-object lineage.
- `D:\RedPrismBuildCaches\.build\p1-polygonal-dispatcher-cpu-r2` — complete failed runtime, exact candidate and complete products.
- `D:\RedPrismBuildCaches\.build\p1-polygonal-dispatcher-cpu-r3` — Release/ASan runner, source identity, exact original285-object lineage, manifests, full logs, ISA and independent reader.
- `.build/P1Drafts/PolygonalGestureDispatch-Main-r1/NextPhysicalIntegration.md` — required caller work, source-transition/Save/key/lifetime decisions and actual host matrix.


Repository checks after adoption: all 15 skills validate, formatting checks all 1,309 files and `git diff --check` passes. No DxUi source/pin, runtime ISA allowlist, legacy golden or ordinary activation gate changes.

# P1 cursor request-state prerequisite — 2026-10-17

## Scope

Five code/test paths implement and register the fixed-storage cursor request state. This is a
prerequisite with no ordinary runtime routing change. The Application/model cursor work is still
private and incomplete. P1 remains WIP.

## Actual focused execution

| x64 profile | Runtime PID | Exit | Exact raw records | Compile records |
| --- | ---: | ---: | ---: | ---: |
| Release | 79916 | 0 | 114 | 2 |
| ASan Debug | 71600 | 0 | 114 | 2 |

Each actual transcript matches 113 source-defined assertions followed by the successful summary,
in exact order with empty stderr. The ASan compiler records contain `/fsanitize=address` and the
required sanitizer header; the matching runtime DLL is present. The Release records are not
sanitizer builds. Main reviewed the implementation, all literal expectations and the verifier, then
reran the verifier successfully (25/25 predicates). No runtime transcript defined expectations.

The source and command pins, logs, actual child results and independently reviewed verifier are
retained in `.build/P1Drafts/S5SelectionCursor-r1-Main/FocusedState-r2/` and its
`Review-Agent-r1/` child. Adoption hashes are in the parent `state-component-adoption.json`.
The old r1 evidence has 107 cases and is retained as earlier-source evidence only.

## Boundaries and remaining proof

These are focused two-TU binaries, not the complete HostTests or Application suite. This run does
not supply a new genuine UAF control, full archive/PE/PDB/ISA qualification, native ARM64 execution,
latest-source native build matrix, or any timing claim. Complete native and physical-consumer gates
remain on the opening P1 checklist. The 280-TU model Release R1 failure is retained separately:
a blank layer had no image asset and correctly could not begin the transform used by its fixture.
A fixture-only repair is rebuilding; no failed assertion was removed or rebaselined.

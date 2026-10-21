# Live-shape redraw checklist reconciliation

Status: scoped functional route qualified; parent P1 remains WIP.

The current managed C checkout already implements shape redraw on the transform worker. The active child
checklist had not recorded that completed route. Main verifies the implementation, actual registration,
frozen fixture bytes and the same nine passing host facts in all three current x64 profiles:

| Actual native profile | Parent exit | Host checks | Archived self-test SHA256 |
| --- | ---: | ---: | --- |
| `f4a1` | 0 | 9 / 9 | `A2F828D0215386506C55BFB2FAE6F4636263F105E5285CD1036F600BF757CC72` |
| `f4d1` | 0 | 9 / 9 | `67E8BAF9629E04F224E333641EE5394BA7A6DB996E338DA4148C0B014A9C9868` |
| `f4r1` | 0 | 9 / 9 | `B35180A0104323D796EA48756D4DA05051F83D0F58489206A9E21994833F0273` |

The fixture source SHA256 is `EB5EB16863091940ED23612B0A395B06853EA77A6D9123C2E97EDFDB05B5B6AB`. It is registered by
`Application.cpp::RunSelfTest`; the file's old “Draft only” comment is stale and is not its registration authority.
The actual host assertions cover the original finish receipt, gated private preparation, held literal source,
history and complete declared reservation, view input while held, literal red 2×2-to-4×3 output, carried
implicit mask placement, exactly one history entry, worker payload/reservation retirement and undo/redo.
The existing Document shape expectations separately retain rectangle, ellipse and rounded-rectangle numeric
references and the redraw allocation declaration. No numeric golden or source behavior changes here.

`TransformPixelsWork.cpp` uses `ShapeImageForWork` before `TransformCommitWork.cpp` seals the private after-document
and history. `ApplicationTransformJobs.cpp` publishes the checked result; the synchronous `RedrawShape` route
refuses a deferred transform. The actual source and full native matrix are retained under
`.build/P1Evidence/p1-size-recovery-native-integration-r1/`.

This reconciliation closes A7.3c's functional worker/publication route only. It does not close A7.3's other
callers, complete phase/weak-owner accounting, ordinary prepared activation, measured frame/apply/G1 or P1 Done.
No native ARM64 host is available, and no ARM64 runtime evidence is inferred from its cross-builds.

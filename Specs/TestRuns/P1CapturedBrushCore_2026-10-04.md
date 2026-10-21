# Captured brush core qualification

## Qualification checklist

- [x] Pass the refreshed full DocumentTests suite and caller compile proof in Debug, Release and ASan.
- [x] Preserve all 6,226 controls per configuration, including every unchanged predecessor assertion.
- [x] Independently verify 869/866/871 source/evidence archive copies and identical 321 core/test source hashes.
- [x] Copy only the exact archived `Common/Document.h` and `Common/BrushStroke.cpp` inputs into the managed candidate.
- [ ] Activate and qualify complete native callers, other tools, lifetime handling and measured G1.

The explicit BeginBrush overload uses captured settings and mask palette. The existing overload forwards the
current session values and preserves its previous behavior. `PendingBrushOpening` exposes the exact current
core owner without extending its lifetime; callers must also check document lifetime. Opening exhaustion
refuses before settling another edit or allocating a stroke. Old path/retirement data cannot authorize a
replacement core stroke merely because its document and layer still exist.

These are private core suite results, not qualification of a fresh full application build of this worktree.
The remaining canvas caller, root activation, ISA, ARM64 runtime and G1 requirements stay open.

Archives live under `%LocalAppData%/RedPrism/QualificationArchives/`:

| Suffix | Inputs / evidence / verified copies | Completion SHA-256 |
| --- | --- | --- |
| `p1-opacity-host-core-private-r5-Debug-qualified-private-document-suite` | 695 / 174 / 869 | `5F9D3DA0DEAB493DA5238A5AFFA6D79B5918BE8C8AFCE37E38A2D9F63705091A` |
| `p1-opacity-host-core-private-r6-Release-qualified-private-document-suite` | 695 / 171 / 866 | `5C4F2428B1AFD4BE8411A5C3B24A7607E33F2BFE1F54E7A7D1D67FB8467B7D40` |
| `p1-opacity-host-core-private-r7-ASanDebug-qualified-private-document-suite` | 696 / 175 / 871 | `5C8C8E1A5697F2C36F1E5AC03C3639D2CA7CDC0B41534DFAEBA907F77096D588` |

ASan source manifest SHA-256: `26771B0E8F16042D8113C2B480E4051AE45355F9C0376C2B2A2EEA8CE36DCBAD`.
ASan evidence manifest SHA-256: `3A9FAC6C5A7F92B98165D64B9A4BABEFEFA3E2AD6A381D16A0445E1AE5C482AA`.
Independent ASan verification finishes at `2026-10-04T06:07:51.9220110Z`.

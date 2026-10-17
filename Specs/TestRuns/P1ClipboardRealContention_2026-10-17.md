# Real external clipboard contention — Release and ASan

Date: 2026-10-17. Scope: actual production Copy/Paste retry callers with a separate clipboard owner.
P1 remains WIP.

## Scenarios and references

An owned native thread creates a message-only HWND and really holds `OpenClipboard`. Four cases cover
Copy/Paste release after the first armed event wait and six-attempt exhaustion. No forced Busy injection
is used. At every wait the original ticket, history barrier, reservation and actual holder remain valid.
Wait-pool readiness waits for the holder's close event; the UI never waits for it.

Paste uses literal one-pixel DIB bytes B=17/G=33/R=65/A=255. Copy selects B=20/G=40/R=80/A=255 at origin
(1,0). Independent pixels and public outcome/history are checked. Copy retains its successful private
result when system mirroring exhausts. Paste exhaustion fails without changing source/history. The
previous system clipboard is restored, and all holder HWND/thread, events, jobs and leases are joined.

## Completed exact-source proof

All 53 new facts and every predecessor pass: 148 Application stages, 8,630 stdout and 7,613 stderr facts.
Both full linked builds have 342 exact compiler records and zero warnings/errors/ISA violations. Main
independently checks source/dependency/product bookends, all archived bytes, PE/full-PDB identity and
complete ordered raw streams. Real ASan includes actual instrumentation and a genuine separate UAF probe.

| Evidence | Files | Completion SHA-256 |
| --- | ---: | --- |
| Release complete archive | 1,711 | `1282545D693E6BC8A1D349400E03A1F81E177468DE9AB94F0FF76B9FE87E964A` |
| ASan build archive | 1,707 | `17C1F8BFC34E484028CEF45C6763C1C9220301545468981F12E5E929FE0240E8` |
| ASan runtime archive | 31 | `0214745AB28556DF818EF4D64A3B32E9530FD27D99355D31AB0AA8DF6AC16F37` |

| Profile | Executable SHA-256 | Full PDB SHA-256 |
| --- | --- | --- |
| Release | `A2331C673FAF20208DA7049957F158EF7CCEFF40D274644D1377136126552E8F` | `5E7DCCABBB904E825B35F7046C41D120839F422084F0670B40A6C5AE401D18E9` |
| ASan Debug | `019F85F68264EC9EA21A1EDF78000EB9017AA9117F7364D8241AB0FA75B79A68` | `A198C03BFE2784C46F3D1B1DFFC44BBBC6EC20561656267D94816E153D1B010A` |

Detailed local receipts are `.build/P1Drafts/F5RealContention-r1-Main/main-qualified.json` and
`.build/P1Drafts/F5RealContentionAsan-r1-Main/main-qualified.json`. The adopted fixture SHA-256 is
`0FEADBCC17811E8DBECD7B89EB2C841912BBBAC3DC6FFBC4EAAA1D94E22DF51E`.

## Retained failures and reader review

Earlier focused preparation and C4389/C2220 compile failures remain retained. Main repaired the signed
history-count expectation and completed the full build on the final convention-clean source. No
production clipboard implementation changed in this step.

The first Release reader refused seven legitimate existing native Inspect diagnostics. Its exit 1 and
logs remain retained; the application itself exited 0. Reader R2 accepts exactly one/six failed native
Open rows in the two source-defined Paste Pending-to-Settled windows, validates every DWORD and forwards
all remaining bytes unchanged to the complete predecessor verifier. Two additional source-only positive
and 15 negative controls cover those windows. The same archived runtime is read again; no C++ or runtime
is repeated. ASan uses the corrected reader from the start. No complete passing predecessor is synthesized.

## Integration and remaining scope

Four exact fixture/declaration/registration/project paths and the test contract are integrated. This
closes the scoped real-contention proof; latest adopted-source full native profiles, other F5/F6 lifecycle
and phase-bound requirements, ordinary activation and measured G1 remain separate gates. ARM64 evidence
for `6a7afac` is cross-build-only and does not qualify these later test additions. P1 is not Done.

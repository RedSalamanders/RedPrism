# P1 selection-outline controller component — 2026-10-12

Status: **private Release qualification PASS; host integration and parent P1 remain open.** This receipt covers the private original-job controller and GPU resource component. It does not claim Application/Renderer host integration, ordinary/default activation, a large-view opaque GPU-driver hard bound, latency/G1 acceptance, or P1 completion.

## Actual build and composition run

The qualified report SHA-256 is `68247F00ECA76FB1AA282BD47B2FAC533BCA65E6B033CB6A13963C90571216B7`. Its protocol/source composition SHA-256 is `E1BD9CE43EC5650CC1760AC92695917254213A72DE64DF8B345E6BB2D434F527`; it binds 1,297 source inputs. Build and composition result books report source/dependency/product bookends verified.

- Release x64 MSBuild child **32812**, exit 0, 2026-10-12 16:33:17.425335–16:34:57.405462 UTC. Stdout SHA-256 `53EC6B4ADF4E49276D2935AB985D4F0CBD87C4D9D1BD2E2119305DFDEF3103F5`; stderr empty, SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`.
- CompositionTests child **99496**, exit 0, 2026-10-12 16:34:58.295345–16:35:07.003474 UTC. Stdout SHA-256 `B292B40C111123DF4AE15109656C36AE6DA8ED0AE8D30663CD532422FF783EC6`; stderr empty with the same empty-file hash.

The qualified reader reports **7,287** ordered records: 6,838 original + 70 prior GPU + 338 residency + 41 controller records. All original assertion occurrences are preserved; the reader reports 10 negative controls.

## Controller assertions

The 40 controller assertions cover:

- Actual GPU-budget queries, original CPU/GPU admission before source capture, one original job across two stages, and 1,000 settled cache borrows without pending useful work.
- Six stale-grid refusals; document and view epoch refusal before pump; new-view replacement through real old-context/worker retirement, limited to at most two graphs.
- Hidden and suspended state barriers using real GPU events while retaining worker charges; repeated suspension stays idle.
- A quarantined failed GPU barrier leaves charges dormant and pixels excluded until explicit retry through the actual event and worker join.
- CPU pressure refuses before source capture and refunds the unused original GPU envelope; unchanged-key failure does not spin or re-admit.
- Joined Close retires both cached graphs and queued unpublished descriptors in the real close hooks.

The report's scope includes complete conservative CPU/GPU admission in this private fixture. The opaque large-view GPU-driver headroom is not established as a hard bound. The prior selection-outline residency receipt separately documents creator-thread CPU-mask release and original source/weak-control lifetime.

## Preserved compilation and reader history

The r1 build failure is preserved: actual MSBuild child 46092 exited 1 with C4457 because a local `result` declaration shadowed a function parameter; no runtime ran. r2 changes only the parameter name; predicates remain unchanged and the actual build/composition run above passes. The earlier residency reader's permitted-interleaving refusal remains preserved; the controller reader accepts only the pre-reviewed bounded recovery/brush regions with counts and order constraints intact.

## Evidence identities

| Input | SHA-256 |
|---|---|
| `actual-release-r2/qualified.json` | `68247F00ECA76FB1AA282BD47B2FAC533BCA65E6B033CB6A13963C90571216B7` |
| `actual-release-r2/before.json` | `FFCB15CBEFCF282EDEBE0986E3DE39962D28F0A25878B66CB4B04F1F876AB9BC` |
| `actual-release-r2/build-result.json` | `9E4DB9F6E11D07A7D61713EC9AB183C56DD498477F28A3F8183FCB810486F116` |
| `actual-release-r2/composition-result.json` | `03C9AF63CE26FF1E2E56032604258115A66DB9DBF703FF51E82DBC8AEF96C670` |
| `actual-release-r2/result.json` | `E28C850728EE65C6C296B45A6ABB21025CEDB6569248E61A3FEF210577701F1A` |
| `actual-release-r2/products-before-runtime.json` | `301061B3D371B0651C3ABEA4C6006F92A8671DAE14DDD6E397B9CCE69FC849A5` |
| `composition-before-build-r1.json` / protocol | `E1BD9CE43EC5650CC1760AC92695917254213A72DE64DF8B345E6BB2D434F527` |
| `expected-predecessor-records.json` | `3336D8F3D2366BD808E3A902F0C15967FD231D505ED2F31CB690EA23CBC364AD` |
| `expected-records.json` | `6E7D4E5A8316182C00B9EF0B428D15419ADDB8A1A28EAE58A8B68C76287B8333` |
| `expected-residency-records-r1.json` | `65B21905F137AC07071CA110A70FF2A35A658E79B0BB90298213A2FD1A77A664` |
| `recorded-brush-interleaving-review-r1.json` | `AD0673DE6E66031E40E3B5BBC154E81C6AF12ECAF387F61693EEA2385AE153F4` |

Main's next step is a private host-integration draft; no host integration is in native source. Host/current-frame activation, complete parent CPU/GPU/D2D adoption, the opaque hard-bound question, measured G1 and all nine parent delivery areas remain open. The parent plan remains WIP and Done unchecked.

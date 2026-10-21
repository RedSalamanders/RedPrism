# P1 selection-outline residency component — 2026-10-12

Status: **private Release qualification PASS; S5 and the parent P1 remain open.** This component evidence does not claim complete CPU/GPU/D2D admission, current-host or frame activation, ordinary application adoption, or G1 acceptance.

## Actual build and composition run

The private 1,293-input build and composition attempt completed successfully. The qualified receipt reports `PASS`; its SHA-256 is `F7A45BC5CD86E041FCA50D8FA96EB2F31E0D50D4F0595C1FAE75DA5A227968EE`. The frozen composition/protocol SHA-256 is `13FBD771FC74DA94E8CFC3D430A07485547E70A3B01C1C0E1B13A59E3F945FF6`.

- Release x64 MSBuild child **97744**, exit 0, 2026-10-12 16:08:28.776504–16:10:30.125699 UTC. Stdout SHA-256 `24390BE0BC26D7ABA2F8128E94857B062DF4EB8F82E9C9CE59C2F5AE26BC4F7D`; stderr was empty (SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`).
- CompositionTests child **79528**, exit 0, 2026-10-12 16:10:31.060999–16:10:41.525338 UTC. Stdout SHA-256 `3D5645AA4651C0D716B900EEE3A840469FFFFC98B9EFC048B2A233E4E891A3BB`; stderr was empty with the same empty-file hash.

The result books bind both actual child receipts and report source/dependency/product bookends verified. The qualified reader reports 1,293 source inputs, preserves all original assertion occurrences, and verifies total **7,246** records: 6,838 original + 70 previous GPU + 337 new residency assertions + one new summary (338 residency records). Reader negative controls: 10.

## Residency and ownership controls

The new assertions cover 18 installed resource-creation fault prefixes, checking the exact created prefix, original error, no partial publication and charge retention through worker retirement; 19 cancellation prefixes, checking exact prefix/native cancellation status, no published views and no early refund. The stale matrix has six grid-refusal cases for publication and six for borrowing, plus device, actual context, generation, source identity and phase refusals.

On the worker, the fixture makes 18 real resource calls to create nine R8 textures and their views. It verifies nine byte-exact staging planes only after the original GPU completion event, with all nine planes matching their prepared CPU bytes. CPU mask storage is released on its creator. Original source ownership and weak-control lifetime are checked through detached worker retirement: the original GPU charge stays held until worker join and owner-side refund; the source object expires before the last weak control releases the retained owner.

The fixture records conservative GPU-driver headroom; that value is not proven to be a hard bound. This result also does not establish parent CPU/GPU/D2D admission or actual application/current-host/frame activation.

## Reader history and interleaving review

The preserved r1 attempt failed shader shared-header assembly (actual PID 97228). r2 failed compilation on a WindowsRPC small-macro fixture issue. r3's actual build and composition child passed, while its initial reader refused the permitted interleaving; that refusal remains preserved. Main's reader-only projection normalizes exactly two previously verified recovery-callback triplets and two brush recording-stage/ack regions. Counts and ordering bounds remain checked; 10 negative controls reject out-of-bound changes. No product or test source was changed to obtain the final reader PASS.

Recorded-brush interleaving review SHA-256: `AD0673DE6E66031E40E3B5BBC154E81C6AF12ECAF387F61693EEA2385AE153F4`.

## Evidence identities

| Input | SHA-256 |
|---|---|
| `actual-release-r3/qualified.json` | `F7A45BC5CD86E041FCA50D8FA96EB2F31E0D50D4F0595C1FAE75DA5A227968EE` |
| Qualified reader SHA-256 | `86A7391C622F0DB68669742A953B1B06EA8CFBAE09C6D7BD5BF3CC7943AB0D29` |
| `actual-release-r3/before.json` | `67DEE47680B21DA8E67059CF2117A6DE6EEC3534E504B50F84678B70BF16A18B` |
| `actual-release-r3/build-result.json` | `933DC9889842D2AAFDAF4325475791802C3F272C5A083A8C7487674F45ACE991` |
| `actual-release-r3/composition-result.json` | `855A34D2CDBB0AF03866F1BBCD4A07B84CA406B2DD7FD7812A38078EED513278` |
| `actual-release-r3/result.json` | `75DA3F583FA0499A15B4FFB183202046410D4CACF74D80C059DA368FCB02BCAD` |
| `actual-release-r3/products-before-runtime.json` | `31D59C233C5A9DB88BE7DA9B002A872EB13CEE3BE635FF62A782FFC67991E9C6` |
| `composition-before-build-r1.json` / protocol | `13FBD771FC74DA94E8CFC3D430A07485547E70A3B01C1C0E1B13A59E3F945FF6` |
| `expected-predecessor-records.json` | `3336D8F3D2366BD808E3A902F0C15967FD231D505ED2F31CB690EA23CBC364AD` |
| `expected-records.json` | `6E7D4E5A8316182C00B9EF0B428D15419ADDB8A1A28EAE58A8B68C76287B8333` |
| `expected-residency-records-r1.json` | `A08812A906AFE8ACA47E7BCF31D3DA26128FAA714B417F326B4B2FEF54D16F44` |
| `recorded-brush-interleaving-review-r1.json` | `AD0673DE6E66031E40E3B5BBC154E81C6AF12ECAF387F61693EEA2385AE153F4` |

The private SelectionOutlineController r2 now qualifies its original-job controller component; see [controller evidence](P1SelectionOutlineController_2026-10-12.md). Application/Renderer host integration and current-host/frame activation remain due. Complete parent CPU/GPU/D2D admission and measured G1 remain open. All nine parent delivery areas remain open; the parent plan remains WIP and Done unchecked.

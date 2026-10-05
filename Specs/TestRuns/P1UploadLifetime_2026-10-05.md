# P1 bounded raster upload lifetime — 2026-10-05

## Qualification checklist

- [x] Primary review of reduction/layout, opt-in terminal acknowledgement, queue faults and upload receiver.
- [x] Private literal CPU and actual WARP probes; failures and repaired test fixtures retained.
- [x] Normal full x64 Debug r130; exact predecessor controls, routes and stages preserved.
- [x] Release analysis r131; scanner/PDB/provenance/analysis controls pass.
- [x] All three ARM64 r9 cross-builds on the same 805 frozen inputs.
- [x] 15 skills and 685 formatted files pass.
- [x] Normal full x64 Release r133 and real ASan Debug r134 on the same current source; all predecessor controls preserved.
- [ ] Complete resource-plan admission, device resource creation/mips, consumers, retirement and G1.

Status: managed dormant foundations fully qualified and pushed as `1cd2b699b38edb2f175113608822fb5c8e0b52d3`; production activation remains open. No application upload activation, Native source
integration, RAW speed result or P1 exit is established. ARM64 runtime remains unavailable.

## Reviewed behavior

`ReduceRasterBlocksForUpload` keeps a fixed crop grid across chunk windows, uses the existing encoded
premultiplied integer rounding, and reads sparse rows without materializing them. Its 41 independent controls
cover literal rounding, crop edges, scratch/capacity, sparse ownership and cancellation. `RasterUploadLayout`
adds 34 arithmetic mip/crop/chunk/scratch/retention controls. The fill-only 8192×5464 case plans 171 chunks
without changing its sparse residency epoch; this arithmetic layout is not complete resource admission.

The scheduler's terminal acknowledgement is opt-in and defaults off. Its 88 controls preserve the original
record/ticket, FIFO position and whole reservation until an external owner acknowledges an outstanding
producer/loan/fence. Finished, failed and cancelled stages participate; existing jobs retain their old hooks.
The queue adds 58 startup and 123 fault controls for acquired-prefix unwind, loan/map/pitch/rearm/signal
failure, quarantine, cancellation and reuse. The base-chunk receiver adds 127 controls with real WARP copies,
including skipped mapped fills, held producer rows, early queue completion, pending map/fence cancellation,
device retirement and joined shutdown. It owns one precreated destination and builds no mips or display.

Primary reviewed the cheaper agents' tests and registrations. Repairs included observing actual queue
completion rather than lent-slot count, using the real callback ticket, waiting on both test and scheduler
events, and holding an actual four-row chunk for middle/final-row cancellation. Assertions were retained.
Normal app/project/entry registrations include the original copy module and each new suite exactly once.

## Private evidence and preserved failures

Private roots are under the Native ignored `.build` directory; each holds exact source copies, compile command,
probe executable/PDB, logs and actual child exit receipts. They qualify only their named source snapshots.

| Probe | Child / exit | Inputs | Source freeze |
| --- | --- | --- | --- |
| `private-upload-layout-runtime-r2-20261005` | 89740 / 0 | 377 | `2EA2D2026F7DF7EB99B4F809AEF7172904529AAF3C65EA6E4B86E4BB98191DD6` |
| `private-upload-retirement-runtime-r1-20261005` | 55120 / 0 | 268 | `B49504BE539E19C2030E5144298050D4F58EDC76A542AD5CDF0EB8C7C746296C` |
| `private-upload-job-runtime-r1-20261005` | 97240 / 8 | 389 | `C933CDF6973AC5BF119BB61BD6C75DD643FB7E6EDC8F0444F9342F7CCD3399EF` |
| `private-upload-job-runtime-r2-20261005` | 92232 / 0 | 389 | `3767A2FE77A470E59D0EB8C5D566EB30B21F076E24C28AF5EA97A94F6996171C` |
| `private-upload-job-runtime-r3-20261005` | 90316 / 0 | 389 | `E816201267F644A144BECBC6123BC94C448F7FB8BD1DAAB152606051BC325806` |

The first GPU leaf probe exited 8 on held-row test fixtures. Primary corrected the lost wake and wrong chunk
height; r2 passed all 473 queue/leaf controls. The normal analysis r129 then found C6221 in a test HRESULT
comparison. An explicit int32 conversion preserves the exact assertion; r3 recompiles and runs the repaired
Main leaf and passes all 473 controls. Production logic did not change for that cast.

Analysis r128 stopped before compilation on the shared x64 vcpkg lock. Release r132 failed before runtime on
DxUi C1041 because analysis and the Release build overlapped the same PDB. Both are archived as failures;
Release r133 runs after analysis/archive completion. Future builds that share a configuration are serialized.
No /FS workaround, assertion change or DxUi pin change was made for this orchestration failure.

## Current frozen normal source

All current normal runs below bind 805 owned inputs to SHA-256
`7CD2D4ACD7FA369A273A891711F4C3ABA3A0F60AB77B1BE9DDDB23D2E0690ECB`. Their successful archives independently verify every copied input and evidence file.
The normal runner invokes the repository's full `test.ps1` entry point; its word “focused” describes the
incremental recorder, not a reduced suite. No forced rebuild is necessary to identify the frozen source.

| Qualification | Actual child / exit | UTC interval | Archive completion SHA-256 |
| --- | --- | --- | --- |
| Full x64 Debug r130 | 62100 / 0 | 2026-10-05T14:07:24.405892+00:00 to 2026-10-05T14:15:16.762534+00:00 | `C9A30EC0CACB5E71FAE0DF0F052FE23C252116977C33132CBA4AB34AB0F7E5D2` |
| Release analysis r131 | 84488 / 0 | 2026-10-05T14:14:58.674478+00:00 to 2026-10-05T14:23:52.515006+00:00 | `EB472AF4E25964A724C89ACA6963469ACFA4BF8A4B7CFC9EB635A75AA05EE3DD` |
| Full x64 Release r133 | 88188 / 0 | 2026-10-05T14:26:05.601869+00:00 to 2026-10-05T14:32:30.225580+00:00 | `79963193B13F34D6C5F609DD583897F3A8041E8CEB9B1FC0476C913729E1B287` |
| Full x64 ASan Debug r134 | 5532 / 0 | 2026-10-05T14:33:10.509646+00:00 to 2026-10-05T14:46:07.168403+00:00 | `00473752DA7C462353ACF99744DC19FD2D19932EF426B277CB80A0070F913571` |
| ARM64 Debug r9 | 93044 / 0 | 2026-10-05T14:14:58.712209+00:00 to 2026-10-05T14:16:20.430659+00:00 | `F5EF9B129ECCF1C67CDF4F444E22158EEA3969CB6F2A66DFFCAEFFBCD83D7B08` |
| ARM64 Release r9 | 85772 / 0 | 2026-10-05T14:16:25.855808+00:00 to 2026-10-05T14:17:45.998120+00:00 | `24A7C7F24587A83D5708B9F768946BB2351FB53B38FA3EC8877B9BF4AFAFBF9C` |
| ARM64 ASan Debug r9 | 66000 / 0 | 2026-10-05T14:17:55.662332+00:00 to 2026-10-05T14:19:28.976760+00:00 | `27B50BF202C48BE76728C2D8BC5E1AE894D8E6A300A368A73F2E4054959DFDDA` |

Full Debug r130 passes all five images, linked ISA/matching-PDB/scanner controls, dependency provenance,
registered runtime suites, main/worker crash probes, hidden WARP/self-test/UI smoke, offline RAW corpus controls
and harness screenshots. Its strict registry proof binds frozen private/normal inputs and preserves every
predecessor named control, multiplicity, application route and ordered stage: Document 6963→7038 (+75),
Host 7431→7519 (+88), Composition 911→1219 (+308), application 9744 and 79 stages unchanged.

Earlier Debug r127 and ARM64 r8 passed on the pre-cast 805-input freeze
`C1C518DF7BE7D318E32AADCAF207EC78F20BF231253F07B167A55279C095DE78`.
They remain historical evidence; current qualification uses the later freeze above. Normal source and raw
archive manifests, not rounded counters or this narrative, identify the tested input.

## Remaining H4 boundary

The application does not initialize or use this upload queue for its display yet. Whole CPU/VRAM admission
must include old/new live and retired graphs, staging, canvas/stack/coverage/lookup surfaces and declared
opaque headroom before capture. Device resource creation, mip work, first composition and chrome need
separate stage measurements and a bounded production protocol. Keep the previous complete canvas until a
current plan is fully ready; preserve existing sparse/brush adoption, mask edge semantics and full-resolution
utility inputs. No helper/private probe replaces the actual large-fixture Release/ETW G1 gate.

The next resource-timing and committed-document plan drafts are separate unqualified work in the Caller
worktree. Their code is excluded from this normal 805-input freeze and cannot inherit these results.

## Full matrix completion

Release r133 and real ASan Debug r134 pass the full repository entrypoint on the same 805-input freeze as
Debug, analysis and ARM64 cross-builds. Their exact private/normal source bindings and predecessor counters
pass. Debug/Release Host counts are 7431→7519; ASan's existing instrumentation control gives 7432→7520.
ASan separately reruns the worker-stress host registry: 7829→7917, with every predecessor and the same 88
new terminal-ack controls preserved. The checker binds primary Host completion to the following named Document
suite and compares the additional stress invocation separately; it does not drop or merge repeated controls.

The real ASan diagnostic is retained in the ASan archive's `Supplement/ASan` directory, with verified byte/hash
copy and timestamp within r134. Its `ERROR: AddressSanitizer: heap-use-after-free` identifies actual instrumented
HostTests child 92700 and the deliberate one-byte detection probe. This expected diagnostic is independent of
the passing runtime suites; no unexpected sanitizer report was accepted.

All normal runtime/configuration gates for this dormant foundation step are closed. Complete first-display
admission, production resource/mip/consumer/retirement integration and measured G1 remain required under H4.

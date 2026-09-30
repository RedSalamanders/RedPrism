# Bounded file encoding and project cancellation — 2026-09-30

## Scope and result

P1.7 F3a/F4a/F4a.1 qualifies cancellable project load/import/save/export helpers and direct bounded file encoding.
PNG/JPEG file encoding uses one exclusive capped stream, without a whole encoded HGLOBAL/vector. ZIP saves
stream durable staged asset files using STORE; manifest DEFLATE compatibility, schema and pixel goldens remain.
This reduces encoded-buffer retention and adds staging IO. Assets are still encoded serially.

86 independent stream checks, 75 project cancellation/publication checks, and all 59 codec-work checks pass
in each x64 configuration. Seven additional codec checks cover direct sparse PNG/JPEG bands, unchanged sparse
source storage, literal premultiplied PNG and white-matte JPEG output. Stop tests cover manifest/decoded asset,
encoded asset, libzip close, staged and published boundaries for ZIP/exploded projects. Before publication the
entire destination/sibling tree survives; after publication Ok and the new literal pixels/metadata survive a
stop. Cancelled imports retain their original canvas dimensions/active/layers. Unsupported-version results
retain the declared version. Worker observers retain borrowed contexts through close/discard only.

Primary review corrected a test assumption about undefined extended-file bytes and added direct sparse file
coverage. A cheaper independent stream review reported no remaining actionable findings. New tests were
integrated and qualified by the primary agent; subagent completion alone was not treated as proof.

## Configuration gates

All x64 Debug, Release and ASan Debug `test.ps1` runs pass, including the real sanitizer probe/stress, existing
document/composition/IO goldens, WARP self-test, UI smoke, crash probes, harness captures and 19 RAW provenance
checks. All ARM64 Debug, Release and ASan Debug cross-builds pass; native ARM64 runtime was not run.
Skills pass (15); formatting checks 250 files. The initial builds caught a shadowed variable and Windows SDK
header include ordering; both were corrected before the final matrix. Final logs contain no failures.

Logs: `.build/p1-project-stream-final-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format logs and
`.build/p1-project-stream-file-bench.log`. The 290-file source/project/script freeze was checked before every
configuration and after all measurements. SHA256 `ACBAA2D01AE89383B5AC3990EA0180A994BF2CFD1737CF4C308BC78977C08C11`.
Release RedPrism SHA256 `73451086DCA9C3CA93EAEEFD48517AA7D80A823B94867D47DB82B7FB27F465DF`;
IoTests SHA256 `9C9ED037D09C85CEBB2E3F1D9E5A108D3F5E2683A1F796AFACB7F179FDBE7140`.

## Measurement scope

Release on the Ryzen 9 9950X3D host, 66,155,405,312 physical RAM bytes, Windows build 26200, Balanced power.
Three serial fresh-process observations per mode/size. Adopted opaque coordinate-hash noise; ZIP/folder save
has two layer identities sharing one source raster (89.52 MP aggregate in the 45 MP case). Each output is
verified after timing and memory capture: dimensions, persistent document/layer identity/order and three
independent pixel samples per layer. Four 256² benchmark modes and invalid mode/zero/side/pixel-budget arguments
were separately checked. No timing assertions are in the normal suite.

Private delta is the before/after private-byte difference, not peak private commit. Process peak working set
includes fixture/setup through encoding and excludes later verification. Disk modes include durability and
publication; memory PNG retains bytes without a durable write. PNG output sizes match (28,364,878 bytes at 4K,
153,067,374 bytes at 45 MP). ZIP bytes vary slightly with generated manifest identities. These modes do not
constitute a measured before/after comparison of the prior file implementation or a Photoshop benchmark.

## Measurements

| Size | Mode | Median ms | Min–max ms | Max private delta MiB | Max peak working set MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 4K | folder-save | 1016.893 | 1011.919–1027.290 | 0.215 | 43.570 |
| 4K | png-file | 496.313 | 494.480–506.652 | 0.203 | 43.434 |
| 4K | png-memory | 484.179 | 480.804–491.221 | 27.277 | 97.188 |
| 4K | zip-save | 1290.006 | 1167.048–1897.075 | 0.215 | 43.590 |
| 45 MP | folder-save | 6173.087 | 6029.416–6288.420 | 0.148 | 182.711 |
| 45 MP | png-file | 2664.328 | 2662.437–2704.019 | 0.137 | 182.582 |
| 45 MP | png-memory | 6001.230 | 5938.643–6047.132 | 146.465 | 474.461 |
| 45 MP | zip-save | 6739.570 | 6727.315–6850.422 | 0.215 | 182.715 |

At 4K, direct file PNG has a slightly higher median than memory PNG (496.313 versus 484.179 ms), with lower
measured peak working set. At 45 MP, direct file PNG is 2,664.328 ms versus 6,001.230 ms for memory PNG, while
peak working set is about 182.58 versus 474.46 MiB. ZIP/folder save retain neither both encoded PNGs nor a
second HGLOBAL copy. This is evidence for the bounded disk-staging choice on these inputs, not a universal bound.

## Outstanding work

Production open/import/save/export/clipboard job graphs, admission, per-path ordering, shared wait-pool IO,
parallel assets, pending/fault injection and measured UI/cancellation bounds remain F3b/F4b/F5/F6. File processing
is still called synchronously by those host paths. Earlier intermittent IO failures documented in the history
record did not recur in this matrix; their root cause remains unestablished and tracked in F6. P1/G1 stays open.

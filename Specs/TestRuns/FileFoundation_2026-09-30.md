# File classification and codec foundation — 2026-09-30

## Result

F1/F2 pass the full configuration matrix. Open batches classify once on a worker using owned counted paths;
the owner routes from the cached result. Cancellation, including a late Finished result, retains request
ownership and reservations until acknowledgement. Classification itself holds no history transaction.
Seven event-gated WARP host cases cover normal/empty input, cancellation before/after preparation, ordinary
failure, allocation failure and document replacement. Copied caller input, younger-action ordering, hidden
completion without Present and reservation release remain correct. The existing 84 host-flow cases pass.

The host-flow harness now pumps acknowledgement events until the color commit actually acquires its gate.
The earlier fixed number of UI-action drains could inspect the nonmutating classification stage instead of
the intended held color commit. Assertions about dependent writes, unchanged pixels and history stay intact.

Decode and encode use bands of at most 64 KiB or one row and worker-safe sparse reads. PNG tests cover literal
premultiplied alpha values, Gray8 and a 4097-row sparse image without materializing the source. JPEG tests use
independent white-matte values on both sides of the 256-row band boundary and a separate alpha stripe.
Precancel publishes empty detached outputs and preserves prior destinations. A locked overwrite preserves
distinguishable old bytes/content and removes only its owned temporary; unrelated sentinels survive.

## Gates and provenance

All x64 Debug, Release and ASan Debug `test.ps1` runs and ARM64 Debug, Release and ASan Debug cross-builds pass.
`validate-skills.ps1` validates 15 skills; `format.ps1 -Check` checks 238 files. ASan runs its detection probe
and stress; existing numeric document/composition/IO goldens, WARP self-test, UI smoke, crash probes and harness
captures pass. The 19 RAW corpus provenance checks pass; they do not qualify RAW decoding.

Logs: `.build/p1-file-foundation-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills and format-check logs.
The source/project/script freeze was checked before configurations and after the matrix:
SHA256 `3A84BE757F5C2C7C4E0BCB47F3F96B6A462A9D22B3C4B33A48D029BF2110B486`.
Release RedPrism SHA256 `EE9DE3DEA8302E845F7ADEC2B2D2A370C79877FD0581389C044862FAB9768050`;
IoTests SHA256 `88528582490754046573BA6FBFC469FAFBA8F476E63348456280C7EF543C0BA1`.

## Open limits

F3–F6 remain active. Project open/import/save/export/clipboard processing still needs complete worker graphs
and their admission/publication contracts. Classification's acknowledged overlapped wait occupies a compute
lane until shared wait-pool migration. Attribute lookup/file open and an individual WIC COM call cannot be
interrupted by these checkpoints. Pending-driver IO, in-flight WIC cancellation and flush/rename fault injection
were not exercised here. Existing orientation/ICC policy is preserved; this slice adds no exhaustive EXIF/ICC
fixture suite. No latency benchmark or worst-case cancellation guarantee is claimed. Native ARM64 execution,
other reference machines and Photoshop were not run. Overall P1.7 and G1 remain open.

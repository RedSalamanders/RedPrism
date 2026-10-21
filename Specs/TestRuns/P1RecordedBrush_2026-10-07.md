# P1 private brush command recording

Status: full Debug r342 foundation pass archived; later sample/return additions require fresh qualification

## Implementation

The primary implementation moves resource creation, segment upload and final dispatch recording to a private
same-device deferred context on the original admitted recorder lane. Captured density, segment buffers and
views return to the original epoch on every outcome. It preserves the complete ordered segment sum at each
pixel and the full tile's original mapping and index. Disjoint pixel regions bound each command; the initial
policy is 256/128/64/32 pixels per side for ≤16/64/256/>256 segments. These sizes need performance measurement
before normal activation. A command-source cancellation or failed record grants no GPU retirement authority.

Dynamic segments are uploaded in the same command list as their dispatch. Each list rebinds complete compute
state after `FinishCommandList(FALSE)`. The compatibility engine still binds its pipeline once per render.
The fixed HLSL retains the existing coverage equations, adding only full-tile dispatch origin/end bounds.
The shader and C++ constant layout agree at 80 bytes. Inputs validate actual worker/context/device, finite
representable geometry, supported tile extent and sorted unique keys before changing the epoch.

Microsoft's [deferred mapping contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/mapping-on-deferred-contexts)
describes command-local dynamic-resource aliases; the [FinishCommandList API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-finishcommandlist)
specifies default state for FALSE. The implementation records maps and bindings per streamed region.

## Independent controls and proof limits

The new WARP parent fixture captures initial settled density plus a provisional tail, records final geometry
off-owner, waits for the original producer and every command acknowledgement, then reads typed uint coverage
through the actual queue. Worker comparisons require byte-exact equality with a separate full-tile immediate
compute reference. Hard and soft cases include translated affine placement, two non-square tiles and 1,024
ordered segments; pixel regions cross the 32-pixel boundary. Incorrect immediate-context and recorder-thread
owner borrows refuse. Post-readback original-use acknowledgement precedes MTA epoch destruction and final refund.

The fixture's declared 128 MiB reservation bounds its small harness peak; it is not a production whole-stroke
admission estimate. Current tests do not qualify actual Append/Up integration, final display/rows publication,
all cancellation/device faults, throughput or G1. No legacy numeric golden is changed.

## Full x64 Debug r342 qualification

The canonical full `test.ps1 -Platform x64 -Configuration Debug` child PID 76220 ran
2026-10-07T21:32:26.735678Z–21:45:00.302919Z and exited 0. All four suites, build-time ISA
controls, crash probes, hidden WARP application self-test, licensed RAW corpus checks and harness
screenshots pass. All 103 application stages remain reached. The corrected r340 shutdown fixture,
visibility controls, cached two-device rebind and byte-exact worker brush controls pass on this source.

- Inputs: 969; freeze `0169FC16C46215A8F236D62224EDCC88C41AC3DB01F6CCCC09D7D1C370625B17`.
- Stdout: `95BEAC8B35F81338B19CC5DB4109EBFA89E0B25F4CD654387FC59A5C15456AB2`.
- Stderr: `302A4EBF455284AF34D079C522AC57CAFBA85A3A38697B5749C750B454DBF091`.
- Archive: `D:\RedPrismQualificationArchives\P1-recorded-brush-display-969-Debug342-qualified-2026-10-07`;
  1,270 copied products/evidence files, independently read back at 21:45:57.5007515Z.
- Source manifest: `EBF38F747AA98A3B229030B8D8B3718855FCC9D5EE4CDEB481C31FB2DD841E75`.
- Evidence manifest: `874F1CBCE541E78E5A38F6421C2C91D4456F58B7123105C37F787BE571B736FE`.
- Completion receipt: `6C5925AA65688E5D6FEB7000818ED1A09F2D6EADC7CD04E23947C397FA472537`.

This qualifies the reached Debug foundation/application states only. Release, real ASan, analysis,
ARM64 cross-builds, normal activation, actual asynchronous Append/Up and measured G1 remain due.
Subsequent exclusive-sample, lease-growth and epoch-return changes are a new unqualified candidate.

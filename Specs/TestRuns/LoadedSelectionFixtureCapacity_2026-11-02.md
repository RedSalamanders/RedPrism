# Loaded-selection fixture capacity

Hosted source `e650dc5018234943f1d117b1b617658c1569e9fd`, native qualification
[41362032887](https://github.com/RedSalamanders/RedPrism/actions/runs/41362032887), x64 job `125014360301`,
passed its Release build and earlier runtime stages, then failed the checkerboard loaded-selection refusal
fixture. Both commands settled with one additional notice each and no selection. Their actual error id was
`234` (`IDS_ERROR_WAND_MEMORY`), rather than the expected `666` (`IDS_ERROR_SELECTION_DETAILED`). The original
blank-layer no-op remained silent. Both traces show `Failed/E_OUTOFMEMORY` with no worker-publication trace.
The attempt did not record its admission snapshot, so its exact available-memory value remains unknown.

The complete loaded-selection plan conservatively includes Boolean output and outline coexistence even when
the command is expected to fail at the later tracer edge limit. A functional complexity test must first have
an adequate declared envelope. The correction moves the unchanged complete refusal suite into the existing
isolated two-worker Application owner with a stable 16 GiB logical snapshot. The 2100-square checkerboard,
8,000,000-edge limit, exact notices, pixel-budget controls and 15-second bounds remain unchanged.

Two additional real command controls use a 2 GiB snapshot and no other reservation. They require the exact
original `E_OUTOFMEMORY`, no source capture/readback, unchanged literal source/mask/selection/history and
joined zero-charge retirement. Production memory sampling and admission policy are unchanged. These controls
do not claim physical capacity, peak RSS, latency or completed hosted qualification. Each subsequent native
attempt retains its independent canonical source/profile receipts; preserve the earlier failure separately.

# Project aggregate pixel budgets before allocation — 2026-10-01

Status: complete configuration qualification passed for this bounded fix.

## Changed boundary

Both exploded and ZIP asset loaders now pass the current independent aggregate image/mask remainder into the
existing codec frame-dimension guard. An oversized later asset is refused before its output pixel buffer,
CopyPixels, adoption and thumbnail production. Attachment retains its defensive post-decode pixel-count check.
The hard 100 M + 100 M limits and accepted pixels/schema remain unchanged.

`ProjectLoadPixelLimits` can lower either aggregate total; negative values are invalid and larger values clamp
to the format limits. This ceiling is not a whole-project peak-memory reservation or host-worker migration.

## Independent regressions

The fixture has two 2×1 color images and two 2×1 Gray8 masks. A three-pixel image budget permits the first image
and mask, then refuses the second image: exactly two AssetDecoded reports and no document/active id published.
A three-pixel mask budget permits three assets, then refuses the second mask. Exact 4/4 budgets load all four
literal rasters unchanged. ZIP exercises its separate decoder callback with the same early refusal. Zero image
budget refuses the first asset with zero completed decodes; negative image/mask limits are Invalid. The existing
PNG remainder test additionally requires no image or thumbnail output on refusal.

These completed-asset counts distinguish the former late aggregate check, which reported the oversized asset's
decode before attachment rejected it. The production CopyFrame dimension check establishes the allocation
boundary; the tests allocate only tiny images.

## Gates

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 261 files |
| x64 Debug / Release / ASan Debug tests | Passed, including real sanitizer probe |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed (runtime unavailable) |

Logs: `.build/p1-project-budgets-driver.log`, `.build/p1-project-budgets-{platform}-{configuration}.log`,
`.build/p1-project-budgets-skills.log`, `.build/p1-project-budgets-format.log`.

Native ARM64 runtime is unavailable. No performance timing, RAW decoding, whole-graph load admission or G1
completion is inferred from this bounded correctness/resource fix.

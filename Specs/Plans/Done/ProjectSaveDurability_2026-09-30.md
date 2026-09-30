# Project save durability sidecar

## Implementation checklist

- [x] Implement unique exclusive sibling archive/staging paths and owned cleanup.
- [x] Check write, flush and transferred handle close before destination publication.
- [x] Store encoded PNG members and retain DEFLATE reader compatibility.
- [x] Add roundtrip, compression, cleanup and distinguishable failed-overwrite expectations.
- [x] Integrate the primary review fixes and run all required configuration gates.
- [x] Record qualification and reconcile specifications for the completed slice.

Status: DONE, bounded P1.7 sidecar. [Qualification evidence](../../TestRuns/ProjectSaveDurability_2026-09-30.md).
Owner: `Common/ProjectStore.cpp`, `Tests/IoTests/IoTests.cpp`

## Scope

Implement master plan §5.3 / D17 save durability for existing ZIP and exploded project paths. Preserve the `.prism`
schema, current reader compatibility, ZIP replacement fallback, and exploded `dest` → `dest.old` rotation. This
sidecar does not migrate IO to workers or change the PNG encoder.

## Requirements

- ZIP and exploded saves use unique, exclusive, same-directory `dest.~<guid>` temporary names.
- Cleanup removes only a temporary created by the current save. ZIP creation uses `ZIP_CREATE | ZIP_EXCL` and
  exploded staging uses exclusive directory creation.
- ZIP commit, subsequent `FlushFileBuffers`, and the flush handle close must succeed before destination replacement.
- Every exploded staged file must write, flush, and close successfully before rotation starts. Failed staging leaves
  the prior destination intact and the existing rollback/rotation behavior remains.
- PNG ZIP members use `ZIP_CM_STORE`; the manifest remains deflated. The reader continues to accept old DEFLATE
  members.

## Validation

`Tests/IoTests` covers ZIP compression methods, normal round trips, DEFLATE compatibility, locked-destination
replacement failure, unrelated same-prefix temp preservation, and owned staging cleanup. The primary build matrix
owns Debug, Release, and ASan Debug execution; this sidecar does not run builds.
